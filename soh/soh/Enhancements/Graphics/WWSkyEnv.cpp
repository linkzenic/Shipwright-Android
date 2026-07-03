#include "WWSkyEnv.h"

#include <libultraship/bridge.h>

#include <math.h>

// FrameInterpolation_Record* declarations used by the OPEN_DISPS/CLOSE_DISPS macros (include before them).
#include "soh/frame_interpolation.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
}

#include "soh/cvar_prefixes.h"
#include <libultraship/libultraship.h>

// Shared horizon placement (defaults mirror the GUI sliders' DefaultValue()s).
#define CVAR_SKY_HORIZON_HEIGHT CVAR_ENHANCEMENT("Graphics.WWClouds.HorizonBandHeight")
#define CVAR_SKY_HORIZON_PARALLAX CVAR_ENHANCEMENT("Graphics.WWClouds.HorizonBandParallax")
static constexpr float kDefaultHorizonHeight = -408.0f;  // sits well against OoT's typical visible horizon
static constexpr float kDefaultHorizonParallax = 0.75f;  // mostly world-pinned (WW's vrbox factor is 0.09)

// Split-screen debug: clip all WW sky elements to the left half of the screen so the vanilla OoT skybox
// (still drawn underneath) shows through on the right — a live side-by-side A/B of our sky vs the textures.
#define CVAR_SKY_SPLIT_DEBUG CVAR_ENHANCEMENT("Graphics.WWSky.SplitDebug")

bool WWSkyEnv_SplitDebugEnabled() {
    return CVarGetInteger(CVAR_SKY_SPLIT_DEBUG, 0) != 0;
}

// Scissor POLY_OPA to the left half (Begin) then restore it to full screen (End). Every WW sky emitter draws
// into POLY_OPA_DISP, so each wraps its geometry in a Begin/End pair; End restores full so the world that
// draws after us into the same buffer is never clipped. gScreenWidth/Height are the game-virtual full extents
// the engine itself scissors to (z_rcp.c), so half-width is a clean vertical split independent of resolution.
void WWSkyEnv_SplitDebugBegin(void* playPtr) {
    if (!WWSkyEnv_SplitDebugEnabled()) {
        return;
    }
    PlayState* play = (PlayState*)playPtr;
    OPEN_DISPS(play->state.gfxCtx);
    gDPSetScissor(POLY_OPA_DISP++, G_SC_NON_INTERLACE, 0, 0, gScreenWidth / 2, gScreenHeight);
    CLOSE_DISPS(play->state.gfxCtx);
}

void WWSkyEnv_SplitDebugEnd(void* playPtr) {
    if (!WWSkyEnv_SplitDebugEnabled()) {
        return;
    }
    PlayState* play = (PlayState*)playPtr;
    OPEN_DISPS(play->state.gfxCtx);
    gDPSetScissor(POLY_OPA_DISP++, G_SC_NON_INTERLACE, 0, 0, gScreenWidth, gScreenHeight);
    CLOSE_DISPS(play->state.gfxCtx);
}

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

// Normalised sun elevation, straight off OoT's own sun-position formula (z_kankyo.c:314,
// sunPos.y = cos(dayTime - 0x8000)): +1 at noon, 0 as the sun crosses the horizon (06:00 / 18:00), -1 at
// midnight. We drive the palette off this rather than a clock schedule so the sky stays locked to the
// *visible* sun — the warm dusk peaks exactly as the sun touches the horizon and night falls only once it is
// below, the way Wind Waker's vrbox tracks its own sun. (OoT's lighting table D_8011FC1C turns the world to
// sunset ~2h before the sun geometry actually sets, which is why any clock-based schedule looked early.)
float WWSkyEnv_SunHeight(void) {
    return Math_CosS((s16)(gSaveContext.dayTime - 0x8000));
}

// WW's six sea-stage colours (dawn, morning, noon, evening, dusk, night) staged along the sun's descent,
// brightest slot first. Between two adjacent stops the palette blends from the upper (brighter) slot to the
// lower one as the sun drops; above the first stop it holds noon, below the last it holds night. The two
// day halves differ only in which warm slots they pass through — dawn/morning on the way up, evening/dusk on
// the way down — so we pick the table by AM/PM. Thresholds are sun elevations; easy to nudge while tuning.
typedef struct {
    float minHeight; // sun elevation at/above which this stop's slot applies
    uint8_t slot;    // index into a kSeaPalette weather row
} SunStop;

static const SunStop kStopsPM[] = { // afternoon -> night (sun descending)
    { 0.50f, 2 },                   // high sun      -> noon        (~16:00)
    { 0.15f, 3 },                   // sinking       -> evening warm(~17:26)
    { 0.00f, 4 },                   // at horizon    -> dusk (peak) (18:00 sunset)
    { -0.15f, 5 },                  // below horizon -> night       (~18:34)
};
static const SunStop kStopsAM[] = { // night -> morning (sun rising), same thresholds mirrored
    { 0.50f, 2 },                   // high sun      -> noon        (~08:00)
    { 0.15f, 1 },                   // just risen    -> morning     (~06:34)
    { 0.00f, 0 },                   // at horizon    -> dawn (peak)  (06:00 sunrise)
    { -0.15f, 5 },                  // below horizon -> night       (~05:26)
};

static uint8_t LerpU8(uint8_t a, uint8_t b, float t) {
    return (uint8_t)(a + (b - a) * t);
}

// Pick the two palette slots and blend factor for the current sun height. result = lerp(slotA, slotB, u),
// with slotB the brighter (upper) slot so u -> 1 near it, matching LerpU8(a, b, u) = a + (b - a) * u.
static void SunSlots(float h, bool pm, int* slotA, int* slotB, float* u) {
    const SunStop* s = pm ? kStopsPM : kStopsAM;
    const int n = 4;
    if (h >= s[0].minHeight) { // sun high: hold the brightest slot
        *slotA = *slotB = s[0].slot;
        *u = 0.0f;
        return;
    }
    for (int i = 1; i < n; i++) {
        if (h >= s[i].minHeight) { // bracket [i-1] (upper/brighter) .. [i] (lower)
            *slotA = s[i].slot;
            *slotB = s[i - 1].slot;
            *u = (h - s[i].minHeight) / (s[i - 1].minHeight - s[i].minHeight); // 1 near upper, 0 near lower
            return;
        }
    }
    *slotA = *slotB = s[n - 1].slot; // sun below the last stop: hold night
    *u = 0.0f;
}

void WWSkyEnv_SampleColors(void* playPtr, const WWSkyWeather* weather, WWSkyColors* out) {
    // Blend between two palette slots by the sun's elevation (not the clock), so the colours track the
    // visible sun. dayTime's top half tells rising from setting (elevation alone is symmetric about noon).
    int slotA, slotB;
    float u;
    SunSlots(WWSkyEnv_SunHeight(), gSaveContext.dayTime >= 0x8000, &slotA, &slotB, &u);
    float c = weather->cloudiness;

    for (int i = 0; i < 3; i++) {
        // sun-elevation blend within each weather set, then clear -> rain by cloudiness
        uint8_t clearSky = LerpU8(kSeaPalette[0][slotA].sky[i], kSeaPalette[0][slotB].sky[i], u);
        uint8_t rainSky = LerpU8(kSeaPalette[1][slotA].sky[i], kSeaPalette[1][slotB].sky[i], u);
        out->sky[i] = LerpU8(clearSky, rainSky, c);

        uint8_t clearKas = LerpU8(kSeaPalette[0][slotA].kasumi[i], kSeaPalette[0][slotB].kasumi[i], u);
        uint8_t rainKas = LerpU8(kSeaPalette[1][slotA].kasumi[i], kSeaPalette[1][slotB].kasumi[i], u);
        out->kasumi[i] = LerpU8(clearKas, rainKas, c);

        uint8_t clearUso = LerpU8(kSeaPalette[0][slotA].usoUmi[i], kSeaPalette[0][slotB].usoUmi[i], u);
        uint8_t rainUso = LerpU8(kSeaPalette[1][slotA].usoUmi[i], kSeaPalette[1][slotB].usoUmi[i], u);
        out->usoUmi[i] = LerpU8(clearUso, rainUso, c);

        uint8_t clearKumo = LerpU8(kSeaPalette[0][slotA].kumo[i], kSeaPalette[0][slotB].kumo[i], u);
        uint8_t rainKumo = LerpU8(kSeaPalette[1][slotA].kumo[i], kSeaPalette[1][slotB].kumo[i], u);
        out->kumo[i] = LerpU8(clearKumo, rainKumo, c);

        uint8_t clearKC = LerpU8(kSeaPalette[0][slotA].kumoCenter[i], kSeaPalette[0][slotB].kumoCenter[i], u);
        uint8_t rainKC = LerpU8(kSeaPalette[1][slotA].kumoCenter[i], kSeaPalette[1][slotB].kumoCenter[i], u);
        out->kumoCenter[i] = LerpU8(clearKC, rainKC, c);
    }
}
