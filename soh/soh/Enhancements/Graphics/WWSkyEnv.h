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

// The five scheduled sky colours, evaluated for the current time of day and weather. Values are Wind
// Waker's own sea-stage palette (extracted from stage.dzs Pale/Virt + the l_time_attribute schedule):
// sky = upper dome, kasumi = the thin horizon haze, usoUmi = below the horizon line, kumo/kumoCenter =
// cloud edge/centre tints. Weather blends the clear palette toward WW's rain palette by cloudiness.
typedef struct WWSkyColors {
    uint8_t sky[3];
    uint8_t kasumi[3];
    uint8_t usoUmi[3];
    uint8_t kumo[3];
    uint8_t kumoCenter[3];
} WWSkyColors;

// `play` is a PlayState* (void* to keep this header light, matching the GameInteractor hook style).
WWSkyWeather WWSkyEnv_Sample(void* play);
void WWSkyEnv_SampleColors(void* play, const WWSkyWeather* weather, WWSkyColors* out);

// The world-space Y of the sky's horizon line — WW translates the whole vrbox (sky dome, fake sea,
// cloud band) as one unit, so the gradient and the horizon clouds must share this. Combines the
// camera height, the Horizon Parallax factor (0 = follows the camera, 1 = pinned to world height)
// and the Horizon Height offset slider.
float WWSkyEnv_HorizonY(void* play);

// dKyw_get_wind_vecpow for OoT: unit wind direction (XZ) + wind power mapped into WW's 0.3/0.6/0.9
// bracket. Shared by the clouds, the horizon band scroll and the wind wisps.
void WWSkyEnv_Wind(void* play, float* dirX, float* dirZ, float* power);
