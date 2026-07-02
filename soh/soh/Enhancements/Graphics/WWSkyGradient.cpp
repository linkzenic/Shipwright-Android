// Wind Waker-style gradient sky — a procedural gradient dome drawn over Ocarina of Time's textured sky.
//
// Wind Waker's sky is not a texture: it is a dome whose smooth top-to-horizon gradient comes from the mesh
// itself, tinted at runtime by two time-of-day colours (an upper "sky" colour and a lower "horizon haze"
// colour). This recreates that with the same idea — a camera-centred sphere with a vertical colour gradient.
//
// Colour source: we can't reuse OoT's own environment colours for this — OoT bakes the blue into the sky
// *texture*, so its fog/ambient colours are desaturated (grey by day, near-black at night). Wind Waker's real
// colours live in its game data (l_vr_box_data, blended through a time-of-day schedule) and are not embedded
// in noclip's source, so instead we carry a hand-authored WW-matched palette below (blue day sky, warm
// dawn/dusk horizons, deep-navy night) keyframed over the day. TODO: later, source the exact WW values from
// an asset/o2r so a texture pack can supply or tweak them.
//
// The dome is opaque and drawn (via the OnPlayDrawSkyGradient hook) right after OoT's skybox and before the
// stars / sun / moon / world, so it replaces the textured sky's look while everything else still draws on
// top. Geometry is static (built once); only the vertex colours are rewritten each frame. No new assets.

#include <libultraship/bridge.h>
#include <ship/Context.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
// FrameInterpolation_Record* declarations used by the OPEN_DISPS/CLOSE_DISPS macros (include before them).
#include "soh/frame_interpolation.h"

#include <math.h>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
}

// Slider defaults — mirror the GUI slider DefaultValue()s so a fresh install (CVar unset) renders the same as
// the slider's default position.
static constexpr float kDefaultBrightness = 1.0f; // overall multiplier on the palette colours
static constexpr float kDefaultHazeBand = 0.5f;   // fraction of the upper hemisphere the haze fades across

#define CVAR_SKYGRAD_ENABLED CVAR_ENHANCEMENT("Graphics.WWSkyGradient.Enabled")
#define CVAR_SKYGRAD_BRIGHTNESS CVAR_ENHANCEMENT("Graphics.WWSkyGradient.Brightness")
#define CVAR_SKYGRAD_HAZEBAND CVAR_ENHANCEMENT("Graphics.WWSkyGradient.HazeBand")

// Hand-authored Wind Waker-style sky palette, keyframed over the day. `t` is the fraction of a day (OoT
// dayTime / 0x10000): 0.0 = midnight, 0.25 = 06:00, 0.5 = noon, 0.75 = 18:00. Each key holds the upper-sky
// colour and the horizon-haze colour; we linearly interpolate between adjacent keys (wrapping past the last).
// Approximated to WW's look rather than pulled from its data — see the file header.
typedef struct {
    float t;
    u8 sky[3];
    u8 horizon[3];
} SkyKey;

static const SkyKey sSkyPalette[] = {
    { 0.00f, { 20, 28, 60 }, { 45, 60, 95 } },     // midnight — deep navy
    { 0.23f, { 35, 50, 95 }, { 90, 85, 120 } },    // pre-dawn — warming
    { 0.27f, { 70, 120, 190 }, { 225, 150, 120 } }, // sunrise — warm horizon
    { 0.33f, { 80, 150, 220 }, { 175, 205, 225 } }, // morning
    { 0.50f, { 70, 145, 225 }, { 165, 210, 238 } }, // noon — vivid WW blue
    { 0.68f, { 80, 150, 215 }, { 185, 205, 220 } }, // afternoon
    { 0.74f, { 60, 85, 160 }, { 230, 120, 80 } },  // sunset — warm horizon
    { 0.80f, { 30, 40, 90 }, { 95, 70, 110 } },    // dusk — fading
    { 0.85f, { 20, 28, 60 }, { 45, 60, 95 } },     // night — back to deep navy
};

// Sample the palette at day-fraction f in [0,1): find the bracketing keys (the last key wraps to the first at
// t+1) and lerp. sky[]/horizon[] receive 0-255 colours.
static void SampleSkyPalette(float f, u8 sky[3], u8 horizon[3]) {
    int n = ARRAY_COUNT(sSkyPalette);
    for (int i = 0; i < n; i++) {
        float t0 = sSkyPalette[i].t;
        float t1 = (i + 1 < n) ? sSkyPalette[i + 1].t : sSkyPalette[0].t + 1.0f;
        if (f >= t0 && f < t1) {
            float u = (f - t0) / (t1 - t0);
            const SkyKey* a = &sSkyPalette[i];
            const SkyKey* b = &sSkyPalette[(i + 1) % n];
            for (int c = 0; c < 3; c++) {
                sky[c] = (u8)(a->sky[c] + (b->sky[c] - a->sky[c]) * u);
                horizon[c] = (u8)(a->horizon[c] + (b->horizon[c] - a->horizon[c]) * u);
            }
            return;
        }
    }
    // f before the first key (only if the first key's t > 0): fall back to the first key's colours.
    for (int c = 0; c < 3; c++) {
        sky[c] = sSkyPalette[0].sky[c];
        horizon[c] = sSkyPalette[0].horizon[c];
    }
}

// Dome tessellation. A camera-centred sphere emitted as a flat (non-indexed) triangle list, so emission is a
// trivial chunked walk. RADIUS just needs to sit between OoT's near (10) and far (12800) planes; the dome has
// no z-test and follows the camera, so its exact distance doesn't matter for layering.
static constexpr int kDomeRings = 16; // latitude bands (bottom pole → top pole)
static constexpr int kDomeSegs = 24;  // longitude divisions
static constexpr float kDomeRadius = 6000.0f;
#define DOME_VERTS (kDomeRings * kDomeSegs * 6) // 6 verts (2 tris) per quad
static Vtx sDomeVtx[DOME_VERTS];
static bool sDomeBuilt = false;

// ---------------------------------------------------------------------------------------------------
// Geometry (built once) and per-frame colouring
// ---------------------------------------------------------------------------------------------------

static void SetDomePos(Vtx* v, float x, float y, float z) {
    v->v.ob[0] = (s16)x;
    v->v.ob[1] = (s16)y;
    v->v.ob[2] = (s16)z;
    v->v.flag = 0;
    v->v.tc[0] = 0;
    v->v.tc[1] = 0;
}

// Build the sphere positions once. Colours are filled every frame (they depend on the time of day).
static void BuildDome() {
    int idx = 0;
    for (int ring = 0; ring < kDomeRings; ring++) {
        float phi0 = -M_PI / 2.0f + M_PI * ((float)ring / kDomeRings);
        float phi1 = -M_PI / 2.0f + M_PI * ((float)(ring + 1) / kDomeRings);
        float y0 = kDomeRadius * sinf(phi0), rc0 = kDomeRadius * cosf(phi0);
        float y1 = kDomeRadius * sinf(phi1), rc1 = kDomeRadius * cosf(phi1);
        for (int seg = 0; seg < kDomeSegs; seg++) {
            float lam0 = 2.0f * M_PI * ((float)seg / kDomeSegs);
            float lam1 = 2.0f * M_PI * ((float)(seg + 1) / kDomeSegs);
            float c0 = cosf(lam0), s0 = sinf(lam0), c1 = cosf(lam1), s1 = sinf(lam1);
            // Quad corners: (ring, seg) grid on the sphere.
            float ax = rc0 * c0, az = rc0 * s0; // v00
            float bx = rc0 * c1, bz = rc0 * s1; // v01
            float cx = rc1 * c0, cz = rc1 * s0; // v10
            float dx = rc1 * c1, dz = rc1 * s1; // v11
            // Two triangles (winding irrelevant — the dome is drawn double-sided).
            SetDomePos(&sDomeVtx[idx++], ax, y0, az);
            SetDomePos(&sDomeVtx[idx++], cx, y1, cz);
            SetDomePos(&sDomeVtx[idx++], dx, y1, dz);
            SetDomePos(&sDomeVtx[idx++], ax, y0, az);
            SetDomePos(&sDomeVtx[idx++], dx, y1, dz);
            SetDomePos(&sDomeVtx[idx++], bx, y0, bz);
        }
    }
    sDomeBuilt = true;
}

static u8 ClampU8(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : (u8)v);
}

// Rewrite every vertex colour from the two current stops. The gradient is keyed off each vertex's height
// (already baked into ob[1]): horizon colour at/below the horizon, easing to the darker upper-sky colour over
// the lower `hazeBand` of the upper hemisphere.
static void ColorDome(const u8 horizon[3], const u8 top[3], float hazeBand) {
    if (hazeBand < 0.01f) {
        hazeBand = 0.01f;
    }
    for (int i = 0; i < DOME_VERTS; i++) {
        Vtx* v = &sDomeVtx[i];
        float elevation = (float)v->v.ob[1] / kDomeRadius; // -1 (bottom) .. +1 (top)
        float e = elevation / hazeBand;
        if (e < 0.0f) {
            e = 0.0f;
        } else if (e > 1.0f) {
            e = 1.0f;
        }
        float t = e * e * (3.0f - 2.0f * e); // smoothstep
        v->v.cn[0] = (u8)(horizon[0] + (top[0] - horizon[0]) * t);
        v->v.cn[1] = (u8)(horizon[1] + (top[1] - horizon[1]) * t);
        v->v.cn[2] = (u8)(horizon[2] + (top[2] - horizon[2]) * t);
        v->v.cn[3] = 255;
    }
}

// ---------------------------------------------------------------------------------------------------
// Emit the dome display list
// ---------------------------------------------------------------------------------------------------

#define DOME_VERTS_PER_CHUNK 30 // 30 ≤ the 32-vertex cache load limit; 10 triangles per chunk

static void EmitDome(PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);

    // Modelview = translate to the eye, so the dome follows the camera (like the skybox). Vertices are
    // eye-relative and small enough for s16; the camera view lives in the (interpolated) projection matrix.
    Matrix_Translate(play->view.eye.x, play->view.eye.y, play->view.eye.z, MTXMODE_NEW);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);

    gDPPipeSync(POLY_OPA_DISP++);
    // Unlit, vertex-coloured, double-sided (viewed from inside), no fog.
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_FRONT | G_CULL_BACK | G_FOG);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_SHADE | G_SHADING_SMOOTH);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    // No z-test / no z-write: the dome draws over the whole skybox; the stars, sun/moon and world all draw
    // after this pass and paint over it. Alpha is 255 so the XLU blend fully replaces the sky (opaque).
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);

    for (int first = 0; first < DOME_VERTS; first += DOME_VERTS_PER_CHUNK) {
        int verts = DOME_VERTS - first;
        if (verts > DOME_VERTS_PER_CHUNK) {
            verts = DOME_VERTS_PER_CHUNK;
        }
        gSPVertex(POLY_OPA_DISP++, (uintptr_t)&sDomeVtx[first], verts, 0);
        for (int t = 0; t + 6 <= verts; t += 6) {
            gSP2Triangles(POLY_OPA_DISP++, t + 0, t + 1, t + 2, 0, t + 3, t + 4, t + 5, 0);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

// ---------------------------------------------------------------------------------------------------
// Per-frame entry point (OnPlayDrawSkyGradient handler)
// ---------------------------------------------------------------------------------------------------

static void DrawSkyGradient(void* playPtr) {
    PlayState* play = (PlayState*)playPtr;

    // Only over the real overworld sky (same gate as the skybox draw in Play_Draw).
    if (play->skyboxId != SKYBOX_NORMAL_SKY || play->envCtx.skyboxDisabled) {
        return;
    }

    if (!sDomeBuilt) {
        BuildDome();
    }

    // Sky/horizon colours from the hand-authored WW palette at the current time of day, scaled by brightness.
    float dayFrac = (float)gSaveContext.skyboxTime / 65536.0f;
    u8 sky[3], horizon[3];
    SampleSkyPalette(dayFrac, sky, horizon);

    float brightness = CVarGetFloat(CVAR_SKYGRAD_BRIGHTNESS, kDefaultBrightness);
    for (int i = 0; i < 3; i++) {
        sky[i] = ClampU8((int)(sky[i] * brightness));
        horizon[i] = ClampU8((int)(horizon[i] * brightness));
    }

    float hazeBand = CVarGetFloat(CVAR_SKYGRAD_HAZEBAND, kDefaultHazeBand);
    ColorDome(horizon, sky, hazeBand);
    EmitDome(play);
}

// ---------------------------------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------------------------------

void RegisterWWSkyGradient() {
    bool enabled = CVarGetInteger(CVAR_SKYGRAD_ENABLED, 0);
    COND_HOOK(OnPlayDrawSkyGradient, enabled, DrawSkyGradient);
}

static RegisterShipInitFunc initFunc(RegisterWWSkyGradient, { CVAR_SKYGRAD_ENABLED });
