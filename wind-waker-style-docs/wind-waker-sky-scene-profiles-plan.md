# Plan: per-scene, weather-aware Wind Waker sky

Status: **Phase 1 IMPLEMENTED (pending in-game confirmation); Phases 2-3 not started.**

Phase 1 lives in `WWSkyEnv.cpp/.h` (the shared weather sampler) + consumers in WWSkyGradient.cpp
(palette pulled toward scene fog by cloudiness, storm dim), WWClouds.cpp (coverage floor-raised by
cloudiness, drift ×(1+storm), tint toward fog) and WWNightSky.cpp (star target ×(1-cloudiness)).
Quick in-game test: play the Song of Storms outdoors — sky should grey over ~5s, clouds thicken and
speed up, band greys, and (at night) stars fade; all reverting after the storm passes.

## Current state (verified in code)

- **Sky gradient** (`WWSkyGradient.cpp`): colours come from a hand-authored global palette
  (`sSkyPalette`, 9 keyframes) sampled by `gSaveContext.skyboxTime` only. **No scene or weather
  influence at all.** (Deliberate at the time: OoT bakes the sky's blue into the skybox *texture*, so
  scene env colours are desaturated — grey fog by day, near-black at night — and can't be reused
  directly. The file header even left a TODO to source colours from an asset later.)
- **Clouds** (`WWClouds.cpp`): tint from a similar built-in day/night curve (`CloudTints`); coverage
  and drift from sliders. Weather affects them only via `envCtx.windDirection/windSpeed` (drift
  direction/speed) — which barely changes in practice. **No response to rain/storms.**
- **Stars**: time-of-day fade only.

## How OoT's env/weather system works (z_kankyo.c)

- Each scene ships a `lightSettingsList` (`EnvLightSettings`: ambient, two directional lights, fog
  colour, fog near/far) — the data the prelude-of-light editor patches.
- Outdoor scenes are driven by **light configs** (`envCtx.unk_1F` current, `unk_20` next): global
  schedule tables (`D_8011FB48[config]`) map skybox time → a pair of light-setting indexes
  (`unk_04` from, `unk_05` to) plus a lerp weight (`sp8C` via `Environment_LerpWeight`).
- **Weather = switching configs.** `gWeatherMode` / rain transitions select cloudy/rain configs
  (2/3/4…), whose schedule rows point at *different setting indexes* (clear uses 0–3; variants use
  4–7, 8–11, 20–23). Config changes crossfade over a timer (`envCtx.unk_22/unk_24` → weight `sp88`).
- Final blend (the money code, ~z_kankyo.c:996-1010):
  `result = LERP( LERP(set[cfg1F.from], set[cfg1F.to], sp8C), LERP(set[cfg20.from], set[cfg20.to], sp8C), sp88 )`.
- Sky-texture weather signal: `envCtx.unk_17`/`unk_18` (0 = fine, 1 = cloud) + `envCtx.skyboxBlend`
  select/blend the vr_fine ↔ vr_cloud skybox files → a ready-made 0..1 **cloudiness** signal.
- Storm extras: `envCtx.lightningMode` (storm active), `envCtx.gloomySkyMode`, rain strength counter
  (`envCtx.unk_EE[1]`, ramped by `func_800766C4`).

## The plan

### Phase 1 — global weather awareness (no authoring needed)

Make the sky react to weather everywhere, using signals that already exist:

1. Compute a smooth **cloudiness** 0..1 from `unk_17`/`unk_18` + `skyboxBlend`, and a **storm** flag
   from `lightningMode`/rain strength.
2. Gradient: blend the palette output toward the scene's *current* blended fog colour
   (`envCtx.lightSettings.fogColor`) by cloudiness — rain configs already carry grey fog, so the sky
   greys out with zero authoring. Slight darkening on storm.
3. Clouds: `strength = max(CoverageSlider, cloudiness)` (WW's own model: strength drives count, size,
   alpha); drift multiplier eased up under storm; cloud tint toward fog grey.
4. Stars: fade out by cloudiness (stars behind full overcast are wrong today).

Cheap, ships good defaults for every scene, and all of it stays as the fallback when no profile exists.

### Phase 2 — per-scene sky profiles (the editor's supplementary values)

A JSON resource delivered in a mods o2r (same channel as the scene-colour mods):

- Resource path: `windwaker/sky/<scene>.json` (scene keyed by the same identifier the editor uses;
  runtime looks it up by `play->sceneNum` through a name table). Loaded on scene init
  (`OnSceneInit` hook), cached, cleared on scene change.
- Shape (all fields optional; missing → Phase-1 behaviour):

```jsonc
{
  "version": 1,
  "bandHeight": -1400,          // per-scene: Death Mountain Trail wants a much lower horizon
  "bandParallax": 1.0,          // per-scene override
  "settings": {                 // keyed by LIGHT-SETTING INDEX, mirroring lightSettingsList
    "0": { "sky": { "top": [70,145,225], "horizon": [165,210,238],
                     "cloudTint": [255,255,255], "coverage": 0.3, "drift": 2.0, "stars": 1.0 } },
    "4": { "sky": { "top": [60,70,90], "horizon": [130,140,150],
                     "coverage": 1.0, "drift": 3.5, "stars": 0.0 } }  // this scene's rain setting
    // future channels ride the same entries: "act": {...}, "bg0": {...}, "fog": {...}
  }
}
```

- **Blending: piggyback on OoT's own double lerp.** Add a tiny `// SOH [Enhancement]` capture in
  the outdoor branch of `Environment_Update` (where `sp8C`/`sp88` are computed) that stores
  `{cfg1F.from, cfg1F.to, cfg20.from, cfg20.to, timeWeight, cfgWeight}` into an exported struct
  (the existing `D_8011FDCC/D0/D4` globals are close but masked `&3`, so they don't carry the raw
  setting indexes). Each frame, the sky evaluates the profile's settings through the *identical*
  lerp — so our values change with time of day AND crossfade through weather transitions in perfect
  sync with the scene's own colours. A thunderstorm automatically ramps coverage/drift because the
  rain config points at rain-setting indexes.
- Per-index fallback: any index the profile doesn't define evaluates to the Phase-1 result, so
  partial authoring works.
- Sliders become global trims multiplied on top of profile values (document in tooltips).

### Phase 3 — editor integration (prelude-of-light repo)

The editor already shows a scene's light settings per index; add a "Sky" panel per setting index
writing the JSON above into the same o2r it already produces. Nothing on the SoH side.

## Future direction: full WW-style environment lighting (design constraint, not scoped)

WW's env system is the same shape as OoT's (weather picks a palette set — "colpat"; a time schedule
blends within it) but ~15 colors wide per entry: actor ambient+diffuse (`actCol`), FOUR background
channels each ambient+diffuse (`bgCol[0..3]`), fog, and the vrbox/sky set. The long-term goal is a
"Wind Waker Lighting" mode in the prelude-of-light editor carrying WW's real palette values (rippable
from the unpacked WW ROM's stage `Pale`/`Colo`/`EnvR`/`Virt` chunks — formats documented in noclip's
d_stage.ts). Feasibility tiers: sky set = this plan; `actCol` = feed the existing cel-shading
pipeline per-scene ambient/diffuse (see the toon_shadow_color experiment); `bgCol` = HARD — most OoT
room geometry is unlit baked-vertex-color, so WW-style terrain tinting needs an interpreter-level
tint on room draws (precedent: light casting).

**Constraint on Phase 2 NOW: the profile schema must be channel-extensible** — per-setting entries
are namespaced groups (`"sky": {...}` today; `"act"`, `"bg0"`, `"fog"` later) plus a version field,
and the blend capture stays generic (indexes + weights, agnostic to what data rides on them).

## Risks / notes

- The capture hook sits in decomp code with unnamed fields (`unk_1F` etc.) — keep it one small
  SOH-marked block.
- Scenes that bypass configs (indoor `unk_BF` paths, water boxes overriding `unk_1F`) don't matter:
  the sky only draws for `SKYBOX_NORMAL_SKY`.
- `skyboxTime` vs `dayTime` subtleties are inherited for free by reusing the same schedule lookup.
- Star visibility × cloudiness applies in Phase 1 and stays profile-tunable via `stars`.
