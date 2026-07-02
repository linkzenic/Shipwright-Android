// Wind Waker-style night sky — a procedural, twinkling starfield drawn over Ocarina of Time's night sky.
//
// This is a faithful port of the star field from The Wind Waker (as reverse-engineered by noclip.website's
// `dKankyo_star_Packet` in src/ZeldaWindWaker/d_kankyo_wether.ts). The stars are pure geometry — no textures,
// no new assets: each star is two overlapping triangles (a six-pointed billboard) coloured by vertex colour.
//
//   * The first 16 stars are a fixed bright constellation (WW's `hokuto_pos`).
//   * The rest are placed on an outward spiral around the camera.
//   * Every star follows the camera exactly (its position is an offset added to the eye), so the field has no
//     parallax and reads as infinitely far — only camera *rotation* moves the stars, like a real skybox.
//   * Twinkle is WW's exact mechanism: a single shared sine wave modulates each star's *size*. Small stars
//     pulse between visible and invisible; a per-star size offset staggers them into a shimmer.
//   * The number of visible stars is driven by the time of day, fading out at dawn and in at dusk.
//
// The one deviation forced by OoT's engine: OoT's far clip plane is 12800, but WW's constellation stars sit
// 13k–36k units out. Because the stars follow the camera (distance only affects clipping and on-screen size,
// never parallax), we uniformly scale every position *and* size by kDistanceScale so the farthest star fits
// inside the far plane — the on-screen result is identical (the scale cancels in the size/distance ratio).
//
// It runs entirely game-side via the OnPlayDrawSky hook (fired from Play_Draw right after the skybox draws,
// before the sun/moon and world) and emits a plain vertex-coloured display list — no renderer/shader changes.

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
static constexpr int kDefaultStarCount = 1000;   // WW's cap; scaled down by the time-of-day fade
static constexpr float kDefaultBrightness = 1.0f; // overall star alpha multiplier
static constexpr float kDefaultTwinkleSpeed = 1.0f;

#define CVAR_WWSKY_ENABLED CVAR_ENHANCEMENT("Graphics.WWSky.Enabled") // the "Replace Sky" master toggle
#define CVAR_NIGHTSKY_ENABLED CVAR_ENHANCEMENT("Graphics.WWNightSky.Enabled")
#define CVAR_NIGHTSKY_STARCOUNT CVAR_ENHANCEMENT("Graphics.WWNightSky.StarCount")
#define CVAR_NIGHTSKY_BRIGHTNESS CVAR_ENHANCEMENT("Graphics.WWNightSky.Brightness")
#define CVAR_NIGHTSKY_TWINKLE CVAR_ENHANCEMENT("Graphics.WWNightSky.TwinkleSpeed")

// Per-group distance scales. Because every star follows the camera with no parallax, a group's scale cancels
// out of its on-screen size (angular size = offset / distance), so we can scale the two groups independently
// to land each in a "good" range without changing how they look:
//   * Constellation stars are already huge (~13k-36k units) and would clip OoT's 12800 far plane, so we scale
//     them DOWN to fit.
//   * The small spiral stars are the opposite problem: at WW's scale their geometry is well under one world
//     unit across, and our vertices are s16 INTEGERS — so all six of a star's vertices truncate to the same
//     integer and the star collapses into degenerate, flickering triangles. We scale them UP so the geometry
//     spans many integer units (clean shapes) while a proportionally larger distance keeps the on-screen size
//     identical. Both scales keep positions inside the far plane.
static constexpr float kConstellationScale = 0.25f;
static constexpr float kSmallStarScale = 25.0f;

// Hard cap on stars. Its vertices live in a STATIC buffer (below), NOT the per-frame graphics arena: that
// arena is gfxCtx->polyOpa, the very same buffer the display-list commands grow into — allocating ~96KB of
// vertex data there each frame collides with the scene's commands and corrupts memory. WW uses 1000 stars.
#define WWNS_MAX_STARS 1000
static Vtx sStarVtx[WWNS_MAX_STARS * 6];

// OoT runs its game logic (and therefore Play_Draw / this hook) at 20 fps; WW's animation constants are per
// 30 fps frame. Scale by 30/20 so the twinkle and spin run at the same wall-clock rate WW does.
static constexpr float kFrameScale = 1.5f;

static constexpr float kPi = 3.14159265358979323846f;

// WW's fixed constellation ("hokuto") — 16 bright stars. Values are camera-relative offsets (pre-scale).
static const Vec3f sHokutoPos[16] = {
    { 13000, 10500, -16000 }, { 9400, 9800, -12646 }, { 10200, 11800, -13525 }, { 10300, 13450, -13525 },
    { 15000, 18400, -16162 }, { 12500, 19800, -15000 }, { 9179, 17200, -14404 }, { 9500, 9800, -12646 },
    { -7421, 31005, 18798 },  { -10937, 28000, 15000 }, { -10000, 24902, 18400 }, { -9400, 22500, 15900 },
    { -9179, 21300, 14300 },  { -10300, 22000, 21000 }, { -16000, 25500, 20000 }, { 0, 30000, 19000 },
};

// WW's four star tints (RGBA). Almost every star uses index 0 (blue-white); indices 6 & 8 use pink.
static const u8 sStarCol[4][4] = {
    { 0xDC, 0xE8, 0xFF, 0xFF }, // blue-white
    { 0xFF, 0xC8, 0xC8, 0xFF }, // pink
    { 0xFF, 0xFF, 0xC8, 0xFF }, // yellow
    { 0xC8, 0xC8, 0xFF, 0xFF }, // lavender
};

// Animation state, advanced once per game frame in DrawNightSky.
static double sAnimCounter = 0.0; // WW's star.animCounter; animWave = sin(this)
static float sRot = 0.0f;         // WW's packet rot (degrees) — spins the whole billboard field
static float sStarAmount = 0.0f;  // smoothed time-of-day star amount in [0,1]

// ---------------------------------------------------------------------------------------------------
// Small vector helpers (kept local so this file has no dependency on the game's matrix stack for math)
// ---------------------------------------------------------------------------------------------------

static Vec3f VAdd(Vec3f a, Vec3f b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}
static Vec3f VScale(Vec3f a, float s) {
    return { a.x * s, a.y * s, a.z * s };
}
static Vec3f VCross(Vec3f a, Vec3f b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
static Vec3f VNorm(Vec3f a) {
    float len = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
    if (len < 1e-6f) {
        return { 0.0f, 0.0f, 0.0f };
    }
    return { a.x / len, a.y / len, a.z / len };
}

// WW's cM_s2rad: s16 binary angle → radians.
static float S2Rad(int s) {
    return (float)s * (kPi * 2.0f / 65536.0f);
}

// ---------------------------------------------------------------------------------------------------
// Time-of-day → star amount (WW's wether_move_star schedule, mapped onto OoT's [0,0xFFFF] skybox clock)
// ---------------------------------------------------------------------------------------------------

// OoT's skyboxTime and WW's curTime are both "fraction of a day", so the map is linear: 0 = midnight,
// 0x4000 = 06:00, 0x8000 = noon, 0xC000 = 18:00. WW fades stars out over [60,75] (dawn) and in over
// [270,315] (dusk) of its 0..360 clock.
static float StarAmountForTime(u16 skyboxTime) {
    float ww = ((float)skyboxTime / 65536.0f) * 360.0f;
    if (ww >= 60.0f && ww < 75.0f) {
        return 1.0f - (ww - 60.0f) / 15.0f; // dawn: fade out
    } else if (ww >= 75.0f && ww < 270.0f) {
        return 0.0f; // day
    } else if (ww >= 270.0f && ww < 315.0f) {
        return (ww - 270.0f) / 45.0f; // dusk: fade in
    }
    return 1.0f; // night
}

// ---------------------------------------------------------------------------------------------------
// Star field generation (a faithful port of dKankyo_star_Packet.draw)
// ---------------------------------------------------------------------------------------------------

// Fill one star's six vertices (two triangles) into buf[base..base+5]. center/half are already in the
// scaled, camera-relative space; b/c/d are the camera-facing billboard corners (unit-ish, ~0.9 long).
static void WriteStar(Vtx* buf, int base, Vec3f center, float half, Vec3f b, Vec3f c, Vec3f d, const u8* col) {
    // Triangle 1 at +half, triangle 2 at -half (a six-pointed star). A near-zero half makes the star vanish,
    // which is exactly how the small stars twinkle off.
    const Vec3f offsets[6] = {
        VScale(b, half),  VScale(c, half),  VScale(d, half),
        VScale(b, -half), VScale(c, -half), VScale(d, -half),
    };
    for (int i = 0; i < 6; i++) {
        Vec3f p = VAdd(center, offsets[i]);
        Vtx* v = &buf[base + i];
        v->v.ob[0] = (s16)p.x;
        v->v.ob[1] = (s16)p.y;
        v->v.ob[2] = (s16)p.z;
        v->v.flag = 0;
        v->v.tc[0] = 0;
        v->v.tc[1] = 0;
        v->v.cn[0] = col[0];
        v->v.cn[1] = col[1];
        v->v.cn[2] = col[2];
        v->v.cn[3] = col[3];
    }
}

// Build the whole star vertex buffer for this frame. Returns the star count actually written.
static int BuildStars(Vtx* buf, int starCount, const View* view, float alphaMul) {
    const float animWave = (float)sin(sAnimCounter);

    // Camera basis for the billboards: the star quads always face the camera. right/up span the view plane.
    Vec3f eye = view->eye;
    Vec3f fwd = VNorm({ view->lookAt.x - eye.x, view->lookAt.y - eye.y, view->lookAt.z - eye.z });
    Vec3f right = VNorm(VCross(fwd, view->up));
    Vec3f up = VNorm(VCross(right, fwd));

    // WW's star-corner offsets (view-plane 2D), rotated by the field's spin, then lifted into world space.
    const float rotRad = sRot * (kPi / 180.0f);
    const float cr = cosf(rotRad), sr = sinf(rotRad);
    const float base2d[3][2] = { { 0.0f, 0.9f }, { 0.9f, -0.45f }, { -0.9f, -0.45f } };
    Vec3f corner[3];
    for (int k = 0; k < 3; k++) {
        float x = base2d[k][0] * cr - base2d[k][1] * sr;
        float y = base2d[k][0] * sr + base2d[k][1] * cr;
        corner[k] = VAdd(VScale(right, x), VScale(up, y));
    }

    // Spiral state for the procedural (non-constellation) stars.
    float radius = 0.0f, angle = -kPi, angleIncr = 0.0f;

    for (int i = 0; i < starCount; i++) {
        Vec3f local;
        float half;
        if (i < 16) {
            // Constellation: large, steady, barely-twinkling bright stars.
            half = (i < 8 ? 190.0f : 290.0f) + animWave;
            local = sHokutoPos[i];
        } else {
            // Spiral placement + the size twinkle (folds back above 1 so stars blink at staggered times).
            float scale = animWave + 0.066f * (i & 0x0F);
            if (scale > 1.0f) {
                scale = 1.0f - (scale - 1.0f);
            }
            half = scale;

            float radiusXZ = 1.0f - (radius / 202.0f);
            local.x = radiusXZ * -300.0f * sinf(angle);
            local.y = radius + 45.0f;
            local.z = radiusXZ * 300.0f * cosf(angle);

            angle += angleIncr;
            angleIncr += S2Rad(0x09C4);
            radius += 1.0f + 3.0f * (radius / 8000000.0f); // WW: radius / (200^3)
            if (radius > 200.0f) {
                radius = (20.0f * i) / 1000.0f;
            }
        }

        // Star colour: mostly blue-white, with stars 6 & 8 pink (matches WW's whichColor selection).
        int whichColor;
        if (i == 6 || i == 8) {
            whichColor = 1;
        } else if ((i & 0x3F) == 0) {
            whichColor = (i >> 4) & 0x03;
        } else {
            whichColor = 0;
        }
        u8 col[4] = { sStarCol[whichColor][0], sStarCol[whichColor][1], sStarCol[whichColor][2],
                      (u8)(sStarCol[whichColor][3] * alphaMul) };

        // Scale position and size together by the group's scale into the far-plane-safe, camera-relative
        // space (the eye offset is applied by the modelview matrix, so these coords stay in s16 range). The
        // small-star scale is large so their integer geometry resolves cleanly; see kSmallStarScale.
        float groupScale = (i < 16) ? kConstellationScale : kSmallStarScale;
        Vec3f center = VScale(local, groupScale);
        float finalHalf = half * groupScale;

        WriteStar(buf, i * 6, center, finalHalf, corner[0], corner[1], corner[2], col);
    }
    return starCount;
}

// ---------------------------------------------------------------------------------------------------
// Emit the star display list
// ---------------------------------------------------------------------------------------------------

#define STARS_PER_CHUNK 5             // 5 stars × 6 verts = 30 ≤ the 32-vertex cache load limit
#define VERTS_PER_CHUNK (STARS_PER_CHUNK * 6)

static void EmitStars(PlayState* play, Vtx* buf, int starCount) {
    OPEN_DISPS(play->state.gfxCtx);

    // Modelview = translate to the eye. Vertices are eye-relative, so the field follows the camera (no
    // parallax) and stays within s16 range. The camera view itself lives in the projection matrix, which is
    // frame-interpolated, so the stars reproject smoothly when the camera turns even though we rebuild the
    // geometry at 20 fps.
    Matrix_Translate(play->view.eye.x, play->view.eye.y, play->view.eye.z, MTXMODE_NEW);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);

    gDPPipeSync(POLY_OPA_DISP++);
    // Unlit, vertex-coloured, double-sided (the ±half triangles wind both ways), no fog.
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_FRONT | G_CULL_BACK | G_FOG);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_SHADE | G_SHADING_SMOOTH);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE); // output = vertex colour & alpha
    // No z-test / no z-write: stars draw over the whole skybox, then the sun/moon and world (drawn after this
    // pass, with z) paint over them where they exist — so the moon and terrain occlude the stars for free.
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);

    for (int first = 0; first < starCount; first += STARS_PER_CHUNK) {
        int stars = starCount - first;
        if (stars > STARS_PER_CHUNK) {
            stars = STARS_PER_CHUNK;
        }
        gSPVertex(POLY_OPA_DISP++, (uintptr_t)&buf[first * 6], stars * 6, 0);
        for (int s = 0; s < stars; s++) {
            int b = s * 6;
            gSP2Triangles(POLY_OPA_DISP++, b + 0, b + 1, b + 2, 0, b + 3, b + 4, b + 5, 0);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

// ---------------------------------------------------------------------------------------------------
// Per-frame entry point (OnPlayDrawSky handler)
// ---------------------------------------------------------------------------------------------------

static void DrawNightSky(void* playPtr) {
    PlayState* play = (PlayState*)playPtr;

    // Only over the real overworld sky. Indoor / cutscene-map / disabled-skybox scenes have no night sky to
    // put stars on, matching the skybox-draw condition in Play_Draw.
    if (play->skyboxId != SKYBOX_NORMAL_SKY || play->envCtx.skyboxDisabled) {
        return;
    }

    const float twinkleSpeed = CVarGetFloat(CVAR_NIGHTSKY_TWINKLE, kDefaultTwinkleSpeed);

    // Advance animation once per game frame (WW's per-frame increments, rescaled to OoT's 20 fps logic).
    sAnimCounter += 0.01 * kFrameScale * twinkleSpeed;
    sRot += 1.0f * kFrameScale;

    // Ease the star amount toward its time-of-day target so dawn/dusk transitions don't pop (WW smooths it too).
    float target = StarAmountForTime(gSaveContext.skyboxTime);
    sStarAmount += (target - sStarAmount) * 0.1f;
    if (sStarAmount < 0.001f) {
        return; // fully daytime — nothing to draw
    }

    int maxStars = CVarGetInteger(CVAR_NIGHTSKY_STARCOUNT, kDefaultStarCount);
    if (maxStars > WWNS_MAX_STARS) {
        maxStars = WWNS_MAX_STARS;
    }
    int starCount = (int)(sStarAmount * (float)maxStars);
    if (starCount <= 0) {
        return;
    }
    if (starCount > WWNS_MAX_STARS) {
        starCount = WWNS_MAX_STARS;
    }

    float brightness = CVarGetFloat(CVAR_NIGHTSKY_BRIGHTNESS, kDefaultBrightness);
    // Fade overall alpha with the star amount too (on top of WW's count-based fade) for a smoother dawn/dusk.
    float alphaMul = sStarAmount * brightness;
    if (alphaMul > 1.0f) {
        alphaMul = 1.0f;
    }

    // Rebuild the static vertex buffer each frame (kept out of the polyOpa arena — see WWNS_MAX_STARS).
    BuildStars(sStarVtx, starCount, &play->view, alphaMul);
    EmitStars(play, sStarVtx, starCount);
}

// ---------------------------------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------------------------------

void RegisterWWNightSky() {
    // Only hook while enabled, so a disabled feature adds no per-frame work. Off by default — opt-in.
    bool enabled = CVarGetInteger(CVAR_WWSKY_ENABLED, 0) && CVarGetInteger(CVAR_NIGHTSKY_ENABLED, 1);
    COND_HOOK(OnPlayDrawSky, enabled, DrawNightSky);
}

static RegisterShipInitFunc initFunc(RegisterWWNightSky, { CVAR_WWSKY_ENABLED, CVAR_NIGHTSKY_ENABLED });
