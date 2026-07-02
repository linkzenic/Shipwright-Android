#include "WWSkyEnv.h"

#include <libultraship/bridge.h>

#include <math.h>

extern "C" {
#include "z64.h"
#include "variables.h"
}

#include "soh/cvar_prefixes.h"
#include <libultraship/libultraship.h>

// Shared horizon placement (defaults mirror the GUI sliders' DefaultValue()s).
#define CVAR_SKY_HORIZON_HEIGHT CVAR_ENHANCEMENT("Graphics.WWClouds.HorizonBandHeight")
#define CVAR_SKY_HORIZON_PARALLAX CVAR_ENHANCEMENT("Graphics.WWClouds.HorizonBandParallax")
static constexpr float kDefaultHorizonHeight = -408.0f;  // sits well against OoT's typical visible horizon
static constexpr float kDefaultHorizonParallax = 0.75f;  // mostly world-pinned (WW's vrbox factor is 0.09)

void WWSkyEnv_Wind(void* playPtr, float* dirX, float* dirZ, float* power) {
    PlayState* play = (PlayState*)playPtr;
    float dx = (float)play->envCtx.windDirection.x;
    float dz = (float)play->envCtx.windDirection.z;
    float len = sqrtf(dx * dx + dz * dz);
    if (len < 1.0f) {
        dx = 1.0f; // steady baseline breeze so the sky always drifts, like WW's sea
        dz = 0.3f;
        len = sqrtf(dx * dx + dz * dz);
    }
    *dirX = dx / len;
    *dirZ = dz / len;
    float p = play->envCtx.windSpeed / 255.0f;
    if (p < 0.0f) {
        p = 0.0f;
    } else if (p > 1.0f) {
        p = 1.0f;
    }
    *power = 0.3f + 0.6f * p;
}

float WWSkyEnv_HorizonY(void* playPtr) {
    PlayState* play = (PlayState*)playPtr;
    float height = CVarGetFloat(CVAR_SKY_HORIZON_HEIGHT, kDefaultHorizonHeight);
    float parallax = CVarGetFloat(CVAR_SKY_HORIZON_PARALLAX, kDefaultHorizonParallax);
    return play->view.eye.y * (1.0f - parallax) + height;
}

static float Clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// How the signals are derived (all verified in z_kankyo.c):
//
// - envCtx.unk_17 / unk_18 are the current/next skybox weather rows (0 = the vr_fine textures,
//   1 = vr_cloud). While a weather change is in flight (unk_19 >= 3), envCtx.skyboxBlend carries the
//   fine<->cloud crossfade ramp (~100 frames); in steady state skyboxBlend is reused for day/night
//   texture blending WITHIN a weather row, so it must only be read as cloudiness mid-transition.
// - Rain: unk_EE[0] is the target droplet count (typical storm values 20-48), unk_EE[1] the current
//   count eased toward it every frame — a ready-made smooth rain ramp. lightningMode is set for the
//   duration of a thunderstorm.
// - Fog: envCtx.lightSettings holds the scene's light settings double-lerped by time of day and the
//   weather light-config transition, so its fogColor already greys out in rain configs.
// Eased cloudiness state: clouds roll in quickly but clear out slowly. The game's own fine<->cloud
// skybox crossfade is a brisk ~5s both ways, which reads fine for OoT's subtle textures but makes our
// much-bolder sky snap back to blue while the storm's final lightning bolt is still due. Advanced once
// per game frame (the sampler is called by several sky features every frame).
static float sEasedCloudiness = -1.0f; // -1 = uninitialized
static uint32_t sLastFrame = 0xFFFFFFFF;
static int16_t sLastScene = -1;

WWSkyWeather WWSkyEnv_Sample(void* playPtr) {
    PlayState* play = (PlayState*)playPtr;
    EnvironmentContext* envCtx = &play->envCtx;
    WWSkyWeather w;

    float cur = (envCtx->unk_17 == 1) ? 1.0f : 0.0f;
    float next = (envCtx->unk_18 == 1) ? 1.0f : 0.0f;
    float cloudiness = cur;
    if (envCtx->unk_19 >= 3 && cur != next) {
        cloudiness = cur + (next - cur) * (envCtx->skyboxBlend / 255.0f);
    }

    float rain = Clamp01(envCtx->unk_EE[1] / 30.0f);
    // LIGHTNING_MODE_LAST means one more strike is still coming — the sky must stay stormy for it.
    bool lightning = envCtx->lightningMode == LIGHTNING_MODE_ON || envCtx->lightningMode == LIGHTNING_MODE_LAST;
    float storm = lightning ? 1.0f : rain;

    // Rain or pending lightning always implies a fully overcast sky, even while the skybox blend lags
    // (rolling in) or runs ahead (clearing before the last bolt).
    float target = Clamp01(cloudiness > storm ? cloudiness : storm);

    if (sEasedCloudiness < 0.0f || play->sceneNum != sLastScene) {
        sEasedCloudiness = target; // no easing across scene loads / first use
        sLastScene = play->sceneNum;
    }
    if (play->state.frames != sLastFrame) {
        sLastFrame = play->state.frames;
        float rate = (target > sEasedCloudiness) ? 0.06f : 0.012f; // ~1s to cloud over, ~10s to clear
        sEasedCloudiness += (target - sEasedCloudiness) * rate;
        if (fabsf(target - sEasedCloudiness) < 0.001f) {
            sEasedCloudiness = target;
        }
    }

    w.cloudiness = sEasedCloudiness;
    w.storm = storm;
    w.fogColor[0] = envCtx->lightSettings.fogColor[0];
    w.fogColor[1] = envCtx->lightSettings.fogColor[1];
    w.fogColor[2] = envCtx->lightSettings.fogColor[2];
    return w;
}

// ---------------------------------------------------------------------------------------------------
// Wind Waker's sea-stage sky palette (extracted from the user's unpacked WW ROM: sea/Stage.arc
// stage.dzs, EnvR colpat -> Colo -> Pale -> Virt) and the game's time schedule (l_time_attribute in
// d_kankyo_data). Six palette slots — dawn, morning, noon, evening, dusk, night — blended through the
// schedule below; weather blends the clear set toward the rain set (colpat 1).
// ---------------------------------------------------------------------------------------------------

typedef struct {
    uint8_t sky[3];
    uint8_t kasumi[3];
    uint8_t usoUmi[3];
    uint8_t kumo[3];
    uint8_t kumoCenter[3];
} SkySlot;

static const SkySlot kSeaPalette[2][6] = {
    { // clear (colpat 0)
      { { 79, 70, 78 }, { 180, 142, 121 }, { 113, 90, 73 }, { 74, 71, 79 }, { 108, 96, 92 } },       // dawn
      { { 180, 188, 201 }, { 241, 230, 220 }, { 193, 190, 197 }, { 247, 232, 216 }, { 255, 241, 223 } }, // morning
      { { 80, 120, 255 }, { 163, 210, 255 }, { 80, 120, 255 }, { 255, 255, 255 }, { 255, 255, 255 } },   // noon
      { { 219, 154, 99 }, { 236, 202, 137 }, { 200, 160, 100 }, { 233, 177, 108 }, { 208, 155, 98 } },   // evening
      { { 100, 80, 78 }, { 231, 199, 150 }, { 103, 88, 79 }, { 96, 85, 90 }, { 117, 94, 91 } },          // dusk
      { { 10, 50, 85 }, { 60, 75, 100 }, { 0, 49, 74 }, { 52, 86, 120 }, { 58, 100, 134 } } },           // night
    { // rain (colpat 1)
      { { 75, 71, 68 }, { 124, 112, 99 }, { 74, 73, 70 }, { 74, 69, 65 }, { 100, 87, 75 } },
      { { 120, 133, 127 }, { 164, 181, 182 }, { 122, 135, 127 }, { 161, 160, 150 }, { 176, 182, 167 } },
      { { 105, 130, 119 }, { 143, 161, 164 }, { 85, 107, 100 }, { 160, 180, 165 }, { 170, 190, 175 } },
      { { 127, 116, 89 }, { 78, 77, 61 }, { 71, 69, 52 }, { 130, 112, 84 }, { 108, 97, 82 } },
      { { 108, 99, 82 }, { 68, 67, 51 }, { 68, 65, 52 }, { 108, 97, 74 }, { 93, 87, 72 } },
      { { 21, 35, 33 }, { 33, 46, 42 }, { 15, 45, 46 }, { 50, 55, 56 }, { 45, 53, 59 } } },
};

// l_time_attribute, converted from WW's 0..360-degree day (15°/hour, 0 = midnight) to a 0..1 day
// fraction — the same convention as OoT's dayTime/0x10000, so it maps directly onto skyboxTime.
typedef struct {
    float t0, t1;
    uint8_t slotA, slotB;
} SkySchedule;

static const SkySchedule kSchedule[] = {
    { 0.0000f, 0.2500f, 5, 5 }, // 00:00-06:00 night
    { 0.2500f, 0.2917f, 5, 0 }, // 06:00-07:00 night -> dawn
    { 0.2917f, 0.3333f, 0, 1 }, // 07:00-08:00 dawn -> morning
    { 0.3333f, 0.4167f, 1, 2 }, // 08:00-10:00 morning -> noon
    { 0.4167f, 0.7500f, 2, 2 }, // 10:00-18:00 noon
    { 0.7500f, 0.7917f, 2, 3 }, // 18:00-19:00 noon -> evening
    { 0.7917f, 0.8333f, 3, 4 }, // 19:00-20:00 evening -> dusk
    { 0.8333f, 0.8750f, 4, 5 }, // 20:00-21:00 dusk -> night
    { 0.8750f, 1.0000f, 5, 5 }, // 21:00-24:00 night
};

static uint8_t LerpU8(uint8_t a, uint8_t b, float t) {
    return (uint8_t)(a + (b - a) * t);
}

void WWSkyEnv_SampleColors(void* playPtr, const WWSkyWeather* weather, WWSkyColors* out) {
    float dayFrac = (float)gSaveContext.skyboxTime / 65536.0f;

    const SkySchedule* e = &kSchedule[0];
    for (size_t i = 0; i < sizeof(kSchedule) / sizeof(kSchedule[0]); i++) {
        if (dayFrac >= kSchedule[i].t0 && dayFrac < kSchedule[i].t1) {
            e = &kSchedule[i];
            break;
        }
    }
    float u = (dayFrac - e->t0) / (e->t1 - e->t0);
    float c = weather->cloudiness;

    for (int i = 0; i < 3; i++) {
        // time blend within each weather set, then clear -> rain by cloudiness
        uint8_t clearSky = LerpU8(kSeaPalette[0][e->slotA].sky[i], kSeaPalette[0][e->slotB].sky[i], u);
        uint8_t rainSky = LerpU8(kSeaPalette[1][e->slotA].sky[i], kSeaPalette[1][e->slotB].sky[i], u);
        out->sky[i] = LerpU8(clearSky, rainSky, c);

        uint8_t clearKas = LerpU8(kSeaPalette[0][e->slotA].kasumi[i], kSeaPalette[0][e->slotB].kasumi[i], u);
        uint8_t rainKas = LerpU8(kSeaPalette[1][e->slotA].kasumi[i], kSeaPalette[1][e->slotB].kasumi[i], u);
        out->kasumi[i] = LerpU8(clearKas, rainKas, c);

        uint8_t clearUso = LerpU8(kSeaPalette[0][e->slotA].usoUmi[i], kSeaPalette[0][e->slotB].usoUmi[i], u);
        uint8_t rainUso = LerpU8(kSeaPalette[1][e->slotA].usoUmi[i], kSeaPalette[1][e->slotB].usoUmi[i], u);
        out->usoUmi[i] = LerpU8(clearUso, rainUso, c);

        uint8_t clearKumo = LerpU8(kSeaPalette[0][e->slotA].kumo[i], kSeaPalette[0][e->slotB].kumo[i], u);
        uint8_t rainKumo = LerpU8(kSeaPalette[1][e->slotA].kumo[i], kSeaPalette[1][e->slotB].kumo[i], u);
        out->kumo[i] = LerpU8(clearKumo, rainKumo, c);

        uint8_t clearKC = LerpU8(kSeaPalette[0][e->slotA].kumoCenter[i], kSeaPalette[0][e->slotB].kumoCenter[i], u);
        uint8_t rainKC = LerpU8(kSeaPalette[1][e->slotA].kumoCenter[i], kSeaPalette[1][e->slotB].kumoCenter[i], u);
        out->kumoCenter[i] = LerpU8(clearKC, rainKC, c);
    }
}
