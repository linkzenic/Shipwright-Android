#include "WWSkyEnv.h"

#include <libultraship/bridge.h>

extern "C" {
#include "z64.h"
#include "variables.h"
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
    float storm = (envCtx->lightningMode == LIGHTNING_MODE_ON) ? 1.0f : rain;

    // Rain always implies an overcast sky, even if the skybox transition hasn't caught up.
    w.cloudiness = Clamp01(cloudiness > rain ? cloudiness : rain);
    w.storm = storm;
    w.fogColor[0] = envCtx->lightSettings.fogColor[0];
    w.fogColor[1] = envCtx->lightSettings.fogColor[1];
    w.fogColor[2] = envCtx->lightSettings.fogColor[2];
    return w;
}
