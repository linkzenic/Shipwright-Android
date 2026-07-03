// Wind Waker-style gradient sky — a procedural reproduction of WW's vrbox sky, drawn over Ocarina of
// Time's textured sky.
//
// WW's sky is not a texture; it is a stack of vertex-coloured meshes tinted by scheduled colours:
//   - vr_sky.bdl: an opaque dome tinted vrSkyCol, with a horizon haze band tinted vrKasumiMaeCol whose
//     baked vertex alpha ramps 1 -> 0 from elevation 0° up to +6.6° (measured from the actual mesh).
//   - vr_uso_umi.bdl ("fake sea"): a solid ring tinted vrUsoUmiCol strictly below the horizon line —
//     the light backdrop the horizon cloud band sits against.
// The gradient SHAPE is fixed in the meshes; only the colours animate (time schedule + weather). This
// file mirrors that: a camera-centred dome with WW's measured three-zone profile, coloured every frame
// from the WW sea-stage palette schedule evaluated in WWSkyEnv.cpp.
//
// The dome is opaque and drawn (via the OnPlayDrawSkyGradient hook) right after OoT's skybox and before
// the stars / clouds / sun / moon / world, so it replaces the textured sky's look while everything else
// draws on top. Geometry is static (built once); only the vertex colours are rewritten each frame.

#include <libultraship/bridge.h>
#include <ship/Context.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
// FrameInterpolation_Record* declarations used by the OPEN_DISPS/CLOSE_DISPS macros (include before them).
#include "soh/frame_interpolation.h"
#include "WWSkyEnv.h"
#include "WWSkyFileSelect.h"

#include <math.h>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
}

// Mirror of the GUI slider default so a fresh install (CVar unset) renders like the slider's default.
static constexpr float kDefaultBrightness = 1.0f; // overall multiplier on the palette colours

#define CVAR_WWSKY_ENABLED CVAR_ENHANCEMENT("Graphics.WWSky.Enabled") // the "Use Sky" master toggle
#define CVAR_SKYGRAD_ENABLED CVAR_ENHANCEMENT("Graphics.WWSkyGradient.Enabled")
#define CVAR_SKYGRAD_BRIGHTNESS CVAR_ENHANCEMENT("Graphics.WWSkyGradient.Brightness")

// The kasumi haze band's extent, measured from vr_sky.bdl's baked vertex alpha: full haze at the
// horizon, fading out by +6.6° elevation. Fixed, like WW's mesh — the drama at sunset comes from the
// haze COLOUR turning vivid, not from the band growing (which is why the old Haze Band slider is gone).
static constexpr float kKasumiTopDeg = 6.6f;

// Dome tessellation: latitude rows placed non-uniformly so the thin haze band and the horizon line get
// enough vertices to resolve (vertex colours interpolate linearly across each band).
static const float kDomeElevations[] = {
    -90.0f, -40.0f, -15.0f, -5.0f, -1.0f, 0.0f, 1.65f, 3.3f, 4.95f, 6.6f, 12.0f, 20.0f, 35.0f, 60.0f, 90.0f,
};
static constexpr int kDomeRows = (int)(sizeof(kDomeElevations) / sizeof(kDomeElevations[0]));
static constexpr int kDomeSegs = 24; // longitude divisions
static constexpr float kDomeRadius = 6000.0f;
#define DOME_VERTS ((kDomeRows - 1) * kDomeSegs * 6) // 6 verts (2 tris) per quad
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
    for (int row = 0; row + 1 < kDomeRows; row++) {
        float phi0 = kDomeElevations[row] * (M_PI / 180.0f);
        float phi1 = kDomeElevations[row + 1] * (M_PI / 180.0f);
        float y0 = kDomeRadius * sinf(phi0), rc0 = kDomeRadius * cosf(phi0);
        float y1 = kDomeRadius * sinf(phi1), rc1 = kDomeRadius * cosf(phi1);
        for (int seg = 0; seg < kDomeSegs; seg++) {
            float lam0 = 2.0f * M_PI * ((float)seg / kDomeSegs);
            float lam1 = 2.0f * M_PI * ((float)(seg + 1) / kDomeSegs);
            float c0 = cosf(lam0), s0 = sinf(lam0), c1 = cosf(lam1), s1 = sinf(lam1);
            // Quad corners: (row, seg) grid on the sphere.
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

// WW's measured three-zone profile: usoUmi below the horizon line, kasumi haze at the horizon fading
// linearly into the sky colour by +6.6°, sky above.
static void ZoneColor(float elevDeg, const u8 sky[3], const u8 kasumi[3], const u8 usoUmi[3], u8 out[3]) {
    if (elevDeg < 0.0f) {
        out[0] = usoUmi[0];
        out[1] = usoUmi[1];
        out[2] = usoUmi[2];
    } else if (elevDeg >= kKasumiTopDeg) {
        out[0] = sky[0];
        out[1] = sky[1];
        out[2] = sky[2];
    } else {
        float t = elevDeg / kKasumiTopDeg; // linear, like the mesh's baked vertex alpha
        out[0] = (u8)(kasumi[0] + (sky[0] - kasumi[0]) * t);
        out[1] = (u8)(kasumi[1] + (sky[1] - kasumi[1]) * t);
        out[2] = (u8)(kasumi[2] + (sky[2] - kasumi[2]) * t);
    }
}

// Rewrite every vertex colour from the three current zone colours.
static void ColorDome(const u8 sky[3], const u8 kasumi[3], const u8 usoUmi[3]) {
    for (int i = 0; i < DOME_VERTS; i++) {
        Vtx* v = &sDomeVtx[i];
        float sinElev = (float)v->v.ob[1] / kDomeRadius; // -1 (bottom) .. +1 (top)
        if (sinElev > 1.0f) {
            sinElev = 1.0f;
        } else if (sinElev < -1.0f) {
            sinElev = -1.0f;
        }
        float elevDeg = asinf(sinElev) * (180.0f / M_PI);
        u8 col[3];
        ZoneColor(elevDeg, sky, kasumi, usoUmi, col);
        v->v.cn[0] = col[0];
        v->v.cn[1] = col[1];
        v->v.cn[2] = col[2];
        v->v.cn[3] = 255;
    }
}

// ---------------------------------------------------------------------------------------------------
// Emit the dome display list
// ---------------------------------------------------------------------------------------------------

#define DOME_VERTS_PER_CHUNK 30 // 30 ≤ the 32-vertex cache load limit; 10 triangles per chunk

// Emit the coloured dome over a bare (gfxCtx, view, horizonY) — no PlayState — so both the overworld hook
// and the file-select screen can share it. horizonY is the dome-centre height (WWSkyEnv_HorizonY[ForEye]).
static void EmitDome(GraphicsContext* gfxCtx, View* view, float horizonY) {
    OPEN_DISPS(gfxCtx);
    // Camera-epoch interpolation child, like the vanilla skybox (SkyboxDraw_Draw): the camera-follow
    // translate below interpolates between 20Hz frames but SNAPS on camera cuts — under the default
    // OPEN_DISPS child key it would lerp across the cut and the sky visibly slides for a frame.
    FrameInterpolation_RecordOpenChild(NULL, FrameInterpolation_GetCameraEpoch());

    // Modelview = translate to the eye horizontally; vertically the dome centre sits on the shared sky
    // horizon (WW moves the whole vrbox as one unit, so the haze/usoUmi line here stays locked to the
    // horizon cloud band). Vertices are eye-relative and small enough for s16.
    Matrix_Translate(view->eye.x, horizonY, view->eye.z, MTXMODE_NEW);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);

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

    FrameInterpolation_RecordCloseChild();
    CLOSE_DISPS(gfxCtx);
}

// ---------------------------------------------------------------------------------------------------
// Per-frame entry point (OnPlayDrawSkyGradient handler)
// ---------------------------------------------------------------------------------------------------

// Scale a sampled palette by the user's brightness trim and load it into the dome vertices.
static void ColorDomeFromColors(const WWSkyColors* colors, float brightness) {
    u8 sky[3], kasumi[3], usoUmi[3];
    for (int i = 0; i < 3; i++) {
        sky[i] = ClampU8((int)(colors->sky[i] * brightness));
        kasumi[i] = ClampU8((int)(colors->kasumi[i] * brightness));
        usoUmi[i] = ClampU8((int)(colors->usoUmi[i] * brightness));
    }
    ColorDome(sky, kasumi, usoUmi);
}

static void DrawSkyGradient(void* playPtr) {
    PlayState* play = (PlayState*)playPtr;

    // Only over the real overworld sky (same gate as the skybox draw in Play_Draw).
    if (play->skyboxId != SKYBOX_NORMAL_SKY || play->envCtx.skyboxDisabled) {
        return;
    }

    if (!sDomeBuilt) {
        BuildDome();
    }

    // WW's own scheduled sky colours (time of day + weather), scaled by the user's brightness trim.
    WWSkyWeather weather = WWSkyEnv_Sample(play);
    WWSkyColors colors;
    WWSkyEnv_SampleColors(play, &weather, &colors);
    ColorDomeFromColors(&colors, CVarGetFloat(CVAR_SKYGRAD_BRIGHTNESS, kDefaultBrightness));

    WWSkyEnv_SplitDebugBegin(play);
    EmitDome(play->state.gfxCtx, &play->view, WWSkyEnv_HorizonY(play));
    WWSkyEnv_SplitDebugEnd(play);
}

// ---------------------------------------------------------------------------------------------------
// File-select night sky (drawn from z_file_choose.c after SkyboxDraw_Draw — no PlayState there)
// ---------------------------------------------------------------------------------------------------

extern "C" void WWSky_DrawFileSelect(GraphicsContext* gfxCtx, View* view) {
    if (!CVarGetInteger(CVAR_WWSKY_ENABLED, 0)) {
        return; // master toggle off — leave the vanilla night skybox in place
    }

    // Gradient dome fixed to WW's night palette (the file-select screen has no time of day of its own).
    if (CVarGetInteger(CVAR_SKYGRAD_ENABLED, 1)) {
        if (!sDomeBuilt) {
            BuildDome();
        }
        WWSkyColors colors;
        WWSkyEnv_NightColors(&colors);
        ColorDomeFromColors(&colors, CVarGetFloat(CVAR_SKYGRAD_BRIGHTNESS, kDefaultBrightness));
        EmitDome(gfxCtx, view, WWSkyEnv_HorizonYForEye(view->eye.y));
    }

    // Twinkling starfield on top (checks its own enable toggle).
    WWNightSky_DrawFileSelectStars((void*)gfxCtx, (void*)view);
}

// ---------------------------------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------------------------------

void RegisterWWSkyGradient() {
    bool enabled = CVarGetInteger(CVAR_WWSKY_ENABLED, 0) && CVarGetInteger(CVAR_SKYGRAD_ENABLED, 1);
    COND_HOOK(OnPlayDrawSkyGradient, enabled, DrawSkyGradient);
}

static RegisterShipInitFunc initFunc(RegisterWWSkyGradient, { CVAR_WWSKY_ENABLED, CVAR_SKYGRAD_ENABLED });
