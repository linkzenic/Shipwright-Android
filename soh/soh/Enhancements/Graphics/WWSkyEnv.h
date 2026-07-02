// Shared weather/environment sampling for the Wind Waker-style sky features (gradient, clouds, stars).
//
// Phase 1 of the per-scene sky plan (see wind-waker-style-docs/wind-waker-sky-scene-profiles-plan.md):
// derive a smooth cloudiness/storm signal and the scene's current fog colour from OoT's environment
// state, so the whole sky reacts to weather with no per-scene authoring.

#pragma once

#include <stdint.h>

typedef struct WWSkyWeather {
    float cloudiness;    // 0 = clear sky, 1 = fully overcast (follows the fine->cloud skybox transition)
    float storm;         // 0..1 rain/thunderstorm intensity (1 while lightning is active)
    uint8_t fogColor[3]; // the scene's current blended fog colour (time-of-day + weather config)
} WWSkyWeather;

// `play` is a PlayState* (void* to keep this header light, matching the GameInteractor hook style).
WWSkyWeather WWSkyEnv_Sample(void* play);
