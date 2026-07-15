// Wind Waker-style light casting — game-side policy.
//
// Casts a pool of light from each point light source (torch, fairy, bomb flash, ...) onto the static
// world geometry, the way Wind Waker does: a many-sided polygon of light, tinted to the source colour,
// that conforms to and is occluded by the surfaces inside the light's reach. Crucially this affects
// ONLY the world — placed objects/actors keep their own cel shading (see ToonLighting.cpp), the inverse
// scoping of that feature.
//
// This module owns the OoT-specific policy: which lights exist, where they are in the world, and how the
// light volume (an icosphere) is sized/rotated. It runs from Play_Draw via the OnPlayDrawWorldLights
// hook, after the room is drawn and before the actor loop, so the pools land on the world and under the
// actors.
//
// Each pool is drawn with the stencil light-volume technique: two z-fail mask passes mark the world
// surfaces inside the icosphere, then a self-clearing composite tints those pixels with the light colour
// (see DrawLightPool). The renderer-side stencil support lives in libultraship (Fast::StencilMode); the
// WL_STENCIL_* values below must stay in sync with it.

#include <libultraship/bridge.h>
#include <ship/Context.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
// FrameInterpolation_Record* declarations used by the OPEN_DISPS/CLOSE_DISPS macros (include before them).
#include "soh/frame_interpolation.h"

#include <math.h>
#include <unordered_map>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
// For identifying Navi (Link's fairy): the EnElf struct + FairyType (FAIRY_NAVI) so its light can be
// matched by address and optionally skipped / sized separately.
#include "overlays/actors/ovl_En_Elf/z_en_elf.h"
}

// Slider defaults — match the GUI slider DefaultValue()s so a fresh install (CVar unset) renders the
// same as the slider's default position.
static constexpr float kDefaultSphereSize = 0.5f;     // × a light's radius (its own slider; typically
                                                      //   smaller than the cel-shading point-light range)
static constexpr float kDefaultRotationSpeed = 1.0f;  // × the Wind Waker two-axis tumble rate (1 = authentic)
// Wind Waker Bonbori spin rates (rad/s), scaled by the Rotation Speed slider. Two non-harmonic axes (no
// Z) tumble the low-poly silhouette so it never looks like it's just spinning in place.
static constexpr float kWWRotYRate = 0.598f; // 0xD0 units/frame @ 30 Hz = 34.28°/s
static constexpr float kWWRotXRate = 0.736f; // 0x100 units/frame @ 30 Hz = 42.19°/s
static constexpr float kDefaultIntensity = 0.2f;        // brightness of the cast pool
static constexpr float kDefaultNaviSphereSize = 0.75f;  // Navi's pool size (× radius), separate from torches
static constexpr float kDefaultNaviIntensity = 0.2f;     // Navi's pool brightness
static constexpr float kDefaultSizeFlicker = 1.0f;        // depth of the Wind Waker size pulse (1 = authentic ±5%)
static constexpr float kDefaultFlickerSpeed = 1.0f;       // Wind Waker flame flicker rate (new target ~ every 0.25s / speed)

// ---------------------------------------------------------------------------------------------------
// Wind Waker flame flicker (replaces the game's per-frame white-noise torch flicker)
// ---------------------------------------------------------------------------------------------------
//
// OoT flames re-randomise their brightness every game-update (Obj_Syokudai: Rand_ZeroOne()*127 + 128) —
// fast, jagged white noise. Wind Waker instead picks a new level every handful of frames and tweens
// between them, for a slow, organic flame.
//
// We apply the replacement AT THE SOURCE: Lights_PointSetColorAndRadius (the choke point all flame actors
// funnel through) calls WorldLighting_ApplyFlameFlicker when the toggle is on, so it reaches the scene
// lighting, the vanilla glow, and our cast pools at once. That choke point also carries non-flame lights
// (mirror-shield beam, lights switching off), so we only transform ones that are actually flickering — a
// flame's brightness jumps sharply frame-to-frame, which a delta threshold (plus a short sticky hold)
// detects.

typedef struct {
    f32 cur;        // tweened flicker ratio (output, ~0.6..1.0 of full bright)
    f32 prevTarget; // tween start
    f32 nextTarget; // tween end
    f32 phase;      // seconds into the current tween segment
    f32 maxSeen;    // peak incoming brightness = the light's "full bright" reference
    s32 lastInMax;  // previous incoming max (for jump detection)
    s32 hold;       // updates remaining of "this light is flickering" (sticky)
} FlameFlicker;

static std::unordered_map<LightInfo*, FlameFlicker> sFlicker;

// A lazy flame: dim to 60-100% of full (depth 0.4), so the flicker is present but gentle.
static f32 FlickerRandomTarget() {
    return 0.6f + (Rand_ZeroOne() * 0.4f);
}

// Called from Lights_PointSetColorAndRadius (game-side) per light per update when the toggle is on.
// Replaces an actively-flickering light's white-noise brightness with a slow random-walk tween, scaling
// the channels to preserve hue; leaves steady/smooth lights untouched.
extern "C" void WorldLighting_ApplyFlameFlicker(LightInfo* info, u8* r, u8* g, u8* b) {
    if (info == NULL) {
        return;
    }
    s32 inMax = *r;
    if (*g > inMax) {
        inMax = *g;
    }
    if (*b > inMax) {
        inMax = *b;
    }
    if (inMax <= 0) {
        return; // light off — nothing to flicker
    }

    // Jump detection needs each light's previous sample, so this map holds an entry for every light that
    // passes through the choke point, not just flames. The set is small and the pointers are reused, so a
    // size cap suffices: if it ever grows past 256, drop everything (a one-frame reset, imperceptible)
    // rather than thread a per-frame prune through this hot path.
    if (sFlicker.size() > 256) {
        sFlicker.clear();
    }

    auto [it, isNew] = sFlicker.try_emplace(info);
    FlameFlicker& f = it->second;
    if (isNew) {
        f.cur = 1.0f;
        f.prevTarget = f.nextTarget = FlickerRandomTarget();
        f.phase = 0.0f;
        f.maxSeen = (f32)inMax;
        f.lastInMax = inMax;
        f.hold = 0;
    }

    // Detect white-noise flicker by a large frame-to-frame jump; keep a short sticky hold so an occasional
    // small step doesn't drop us out mid-flame.
    s32 delta = inMax - f.lastInMax;
    f.lastInMax = inMax;
    if (delta < 0) {
        delta = -delta;
    }
    if (delta > 12) {
        f.hold = 8;
    }
    // Track the light's full-bright level (slowly adapt so it follows a flame that genuinely dims).
    f.maxSeen *= 0.99f;
    if ((f32)inMax > f.maxSeen) {
        f.maxSeen = (f32)inMax;
    }

    if (f.hold <= 0) {
        return; // not a flickering flame — leave it alone
    }
    f.hold--;

    // Advance the slow random-walk tween: a new random target every `interval` seconds, eased between.
    f32 speed = CVarGetFloat(CVAR_ENHANCEMENT("Graphics.WorldLighting.FlickerSpeed"), kDefaultFlickerSpeed);
    if (speed < 0.05f) {
        speed = 0.05f;
    }
    f32 interval = 0.25f / speed; // seconds between new targets ("a handful of frames" at speed 1)
    f32 dt = (R_UPDATE_RATE > 0 ? R_UPDATE_RATE : 3) / 60.0f;

    f.phase += dt;
    while (f.phase >= interval) {
        f.phase -= interval;
        f.prevTarget = f.nextTarget;
        f.nextTarget = FlickerRandomTarget();
    }
    f32 t = (interval > 0.0001f) ? (f.phase / interval) : 1.0f;
    f32 eased = t * t * (3.0f - (2.0f * t)); // smoothstep — natural ease (vs linear); easy to revisit
    f.cur = f.prevTarget + ((f.nextTarget - f.prevTarget) * eased);

    // Replace the incoming (white-noise) magnitude with our tweened level, preserving hue.
    f32 desired = f.cur * f.maxSeen;
    f32 scale = desired / (f32)inMax;
    f32 nr = (f32)*r * scale, ng = (f32)*g * scale, nb = (f32)*b * scale;
    *r = (u8)(nr < 0.0f ? 0.0f : (nr > 255.0f ? 255.0f : nr));
    *g = (u8)(ng < 0.0f ? 0.0f : (ng > 255.0f ? 255.0f : ng));
    *b = (u8)(nb < 0.0f ? 0.0f : (nb > 255.0f ? 255.0f : nb));
}

// ---------------------------------------------------------------------------------------------------
// Icosphere geometry (the light volume)
// ---------------------------------------------------------------------------------------------------
//
// A level-2 icosphere: the 20-face icosahedron with each face subdivided once → 80 faces, the low poly
// count whose silhouette/cross-section gives Wind Waker's many-sided "polygon of light" (and whose
// rotation animates those edges). Built once at init.
//
// The N64 vertex cache holds only 32 vertices per gSPVertex load, so rather than juggle a shared 42-vert
// buffer across cache loads we expand to one vertex per face-corner (80 × 3 = 240) and emit in 8 chunks
// of 30 (= 10 faces). The geometry lives at base radius 100, scaled by radius × 0.01 at draw time (same
// convention as the cel-shading debug ring).

#define ICO_FACES 80
#define ICO_VERTS (ICO_FACES * 3) // 240
#define ICO_CHUNK_VERTS 30        // 10 faces per gSPVertex load (≤ 32 vertex cache)
#define ICO_CHUNKS (ICO_VERTS / ICO_CHUNK_VERTS)

// EmitIcosphere's gSP2Triangles calls hard-code vertex indices 0..29, i.e. exactly 10 triangles per chunk.
static_assert(ICO_CHUNK_VERTS == 30, "EmitIcosphere emits 30 vertices (10 triangles) per chunk");

static Vtx sIcoVtx[ICO_VERTS];
static bool sIcoBuilt = false;

// Write one icosphere vertex: project the (sub)icosahedron position onto the unit sphere, scale to the base
// radius 100. `shade` is a per-face brightness (flat across each face) baked into the vertex colour; the
// debug overlay multiplies the light colour by it for a 3D look, while the pools (PRIMITIVE-only combiner)
// ignore it. Untextured.
static void IcoWriteVert(s32 idx, f32 x, f32 y, f32 z, u8 shade) {
    f32 len = sqrtf((x * x) + (y * y) + (z * z));
    f32 s = (len > 0.0001f) ? (100.0f / len) : 0.0f;

    sIcoVtx[idx].v.ob[0] = (s16)lroundf(x * s);
    sIcoVtx[idx].v.ob[1] = (s16)lroundf(y * s);
    sIcoVtx[idx].v.ob[2] = (s16)lroundf(z * s);
    sIcoVtx[idx].v.flag = 0;
    sIcoVtx[idx].v.tc[0] = 0;
    sIcoVtx[idx].v.tc[1] = 0;
    sIcoVtx[idx].v.cn[0] = shade;
    sIcoVtx[idx].v.cn[1] = shade;
    sIcoVtx[idx].v.cn[2] = shade;
    sIcoVtx[idx].v.cn[3] = 255;
}

// Generate the expanded 240-vertex icosphere and bake the chunked display list that draws it. Idempotent.
static void BuildIcosphere() {
    if (sIcoBuilt) {
        return;
    }

    const f32 t = 1.618033988749895f; // golden ratio — icosahedron vertex constant
    const f32 base[12][3] = {
        { -1, t, 0 }, { 1, t, 0 },  { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t },  { 0, 1, t },
        { 0, -1, -t }, { 0, 1, -t }, { t, 0, -1 },  { t, 0, 1 },  { -t, 0, -1 }, { -t, 0, 1 },
    };
    // The 20 icosahedron faces, wound outward (CCW), so back/front culling is meaningful for the stencil
    // passes to come.
    const s32 faces[20][3] = {
        { 0, 11, 5 }, { 0, 5, 1 },  { 0, 1, 7 },   { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 },  { 5, 11, 4 },
        { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },  { 3, 9, 4 },   { 3, 4, 2 },  { 3, 2, 6 },   { 3, 6, 8 },
        { 3, 8, 9 },  { 4, 9, 5 },  { 2, 4, 11 },  { 6, 2, 10 }, { 8, 6, 7 },   { 9, 8, 1 },
    };

    // A fixed direction in the sphere's own space: each face's flat brightness is its outward direction dotted
    // with this, so the shading is baked into the geometry and tumbles WITH the sphere — making the two-axis
    // rotation readable in the debug overlay. (Roughly unit; exactness doesn't matter, it's just a gradient.)
    const f32 shadeDir[3] = { 0.40f, 0.72f, 0.57f };

    s32 w = 0;
    for (s32 f = 0; f < 20; f++) {
        const f32* a = base[faces[f][0]];
        const f32* b = base[faces[f][1]];
        const f32* c = base[faces[f][2]];
        // Edge midpoints (projected onto the sphere in IcoWriteVert).
        const f32 ab[3] = { (a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, (a[2] + b[2]) * 0.5f };
        const f32 bc[3] = { (b[0] + c[0]) * 0.5f, (b[1] + c[1]) * 0.5f, (b[2] + c[2]) * 0.5f };
        const f32 ca[3] = { (c[0] + a[0]) * 0.5f, (c[1] + a[1]) * 0.5f, (c[2] + a[2]) * 0.5f };
        // 4 sub-faces preserving the parent's outward winding.
        const f32* sub[4][3] = {
            { a, ab, ca },
            { ab, b, bc },
            { ca, bc, c },
            { ab, bc, ca },
        };
        for (s32 s = 0; s < 4; s++) {
            // Per-face brightness from the face centroid's outward direction (flat: all 3 corners share it).
            f32 cx = sub[s][0][0] + sub[s][1][0] + sub[s][2][0];
            f32 cy = sub[s][0][1] + sub[s][1][1] + sub[s][2][1];
            f32 cz = sub[s][0][2] + sub[s][1][2] + sub[s][2][2];
            f32 clen = sqrtf((cx * cx) + (cy * cy) + (cz * cz));
            f32 dot =
                (clen > 0.0001f) ? ((cx * shadeDir[0] + cy * shadeDir[1] + cz * shadeDir[2]) / clen) : 0.0f;
            u8 shade = (u8)((0.5f + (0.25f * (dot + 1.0f))) * 255.0f); // [0.5, 1.0] across the sphere
            for (s32 v = 0; v < 3; v++) {
                IcoWriteVert(w++, sub[s][v][0], sub[s][v][1], sub[s][v][2], shade);
            }
        }
    }

    sIcoBuilt = true;
}

// Emit the icosphere triangles into the active translucent display list (called live during draw, so the
// gSPVertex wrapper's OTR resource check / frame interpolation behave as expected). Must run inside an
// OPEN_DISPS scope with the modelview matrix already loaded.
static void EmitIcosphere(GraphicsContext* gfxCtx) {
    OPEN_DISPS(gfxCtx);
    for (s32 chunk = 0; chunk < ICO_CHUNKS; chunk++) {
        gSPVertex(POLY_OPA_DISP++, (uintptr_t)&sIcoVtx[chunk * ICO_CHUNK_VERTS], ICO_CHUNK_VERTS, 0);
        gSP2Triangles(POLY_OPA_DISP++, 0, 1, 2, 0, 3, 4, 5, 0);
        gSP2Triangles(POLY_OPA_DISP++, 6, 7, 8, 0, 9, 10, 11, 0);
        gSP2Triangles(POLY_OPA_DISP++, 12, 13, 14, 0, 15, 16, 17, 0);
        gSP2Triangles(POLY_OPA_DISP++, 18, 19, 20, 0, 21, 22, 23, 0);
        gSP2Triangles(POLY_OPA_DISP++, 24, 25, 26, 0, 27, 28, 29, 0);
    }
    CLOSE_DISPS(gfxCtx);
}

// ---------------------------------------------------------------------------------------------------
// Per-light draw
// ---------------------------------------------------------------------------------------------------

// Stencil modes — must match Fast::StencilMode in libultraship/include/fast/backends/gfx_rendering_api.h.
#define WL_STENCIL_OFF 0
#define WL_STENCIL_INCR 1      // z-fail count up (back faces)
#define WL_STENCIL_DECR 2      // z-fail count down (front faces)
#define WL_STENCIL_COMPOSITE 3 // fill where stencil != 0, self-clearing

// Per-light animation state (Wind Waker tumble + flicker random-walks). Keyed by the actor-owned,
// frame-stable LightInfo pointer; entries for lights that vanish are pruned each frame by generation.
typedef struct {
    f32 angleY, angleX; // Wind Waker two-axis tumble (accumulated radians)
    f32 sizeCur, sizeTarget, sizeTimer;    // WW size flicker: eased random-walk (the dominant pulse)
    f32 alphaCur, alphaTarget, alphaTimer; // WW brightness flicker: subtle eased random-walk
    f32 spawnRadius;                       // Navi only: eased pool radius, so she fades in/out vs popping
    u32 gen;
} WorldLightState;

static std::unordered_map<LightInfo*, WorldLightState> sLightState;
static u32 sStateGen = 0;

// Wind Waker's cLib_addCalc2 easing: move a fraction `speed30` of the remaining gap each 1/30 s tick,
// capped to `maxVel30` per tick (a first-order low-pass with a slew limit — no overshoot, no momentum).
// Made frame-rate independent for our dt.
static f32 WWEase(f32 cur, f32 target, f32 speed30, f32 maxVel30, f32 dt) {
    f32 ticks = dt * 30.0f;
    f32 step = (target - cur) * (1.0f - powf(1.0f - speed30, ticks));
    f32 cap = maxVel30 * ticks;
    if (step > cap) {
        step = cap;
    } else if (step < -cap) {
        step = -cap;
    }
    return cur + step;
}

// Get-or-create the per-light animation state, keyed by the actor-owned, frame-stable LightInfo pointer.
// First sighting seeds a per-light phase so torches don't move in lockstep. (The pool reads the light's
// LIVE colour/radius — the Wind Waker flame flicker already smooths the colour at the source, so no
// separate smoothing is needed here.)
static WorldLightState* WorldLightGetState(LightInfo* info) {
    auto [it, isNew] = sLightState.try_emplace(info);
    WorldLightState& s = it->second;
    if (isNew) {
        f32 phase = (f32)(((uintptr_t)info >> 4) & 0x3FF) / 1024.0f * (2.0f * M_PI);
        s.angleY = phase;
        s.angleX = phase * 0.7f;
        s.sizeCur = s.sizeTarget = 1.0f;
        s.sizeTimer = 0.0f;
        s.alphaCur = s.alphaTarget = 1.0f;
        s.alphaTimer = 0.0f;
        s.spawnRadius = 0.0f; // start hidden so a freshly-spawned light fades in rather than popping
    }
    s.gen = sStateGen;
    return &s;
}

// Load the modelview matrix for one light's volume: centre on the light, spin it (animated polygon
// edges), and scale to its reach. Wrapped in a frame-interpolation child so the spin is smooth at high
// FPS; the three stencil passes that follow reuse this loaded matrix (it persists until changed).
static void WorldLightLoadMatrix(PlayState* play, LightNode* node, LightPoint* p, f32 angleY, f32 angleX,
                                 f32 worldRadius) {
    OPEN_DISPS(play->state.gfxCtx);
    FrameInterpolation_RecordOpenChild(node, 0);
    {
        Vec3f yAxis = { 0.0f, 1.0f, 0.0f };
        Vec3f xAxis = { 1.0f, 0.0f, 0.0f };
        f32 scale = worldRadius * 0.01f; // icosphere geometry is at base radius 100
        // Wind Waker order: translate → rotY → rotX → scale (two non-harmonic axes tumble the silhouette).
        Matrix_Translate(p->x, p->y, p->z, MTXMODE_NEW);
        Matrix_RotateAxis(angleY, &yAxis, MTXMODE_APPLY);
        Matrix_RotateAxis(angleX, &xAxis, MTXMODE_APPLY);
        Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    }
    FrameInterpolation_RecordCloseChild();
    CLOSE_DISPS(play->state.gfxCtx);
}

// One stencil mask pass: render only the back OR only the front faces of the icosphere (cullMode chooses),
// updating the stencil by the z-fail count, writing NO colour. Invisibility comes from prim alpha 0 with a
// plain alpha blend (src*0 + dest = dest) and no alpha compare, so the fragment is never discarded and the
// stencil still updates. Depth is tested (against the world) but NOT written, so the scene depth is intact.
static void WorldLightMaskPass(PlayState* play, s32 stencilMode, u32 cullMode) {
    OPEN_DISPS(play->state.gfxCtx);
    gSPStencil(POLY_OPA_DISP++, stencilMode);
    gDPPipeSync(POLY_OPA_DISP++);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_FRONT | G_CULL_BACK);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_ZBUFFER | cullMode);
    gDPSetCombineLERP(POLY_OPA_DISP++, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0,
                      PRIMITIVE);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2); // z-test, no z-write
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 0, 0, 0, 0);                           // alpha 0 → invisible
    CLOSE_DISPS(play->state.gfxCtx);
    EmitIcosphere(play->state.gfxCtx);
}

// Composite pass: fill the masked region (stencil != 0 = world surface inside the volume) with the light's
// colour, zeroing the stencil as it goes (self-clearing, so the next light starts clean). Drawn with NO
// depth test — back faces cover the volume's whole screen footprint even when the camera is inside it — and
// the stencil mask alone confines the fill to the correct pixels.
static void WorldLightCompositePass(PlayState* play, const u8 col[3], u8 alpha) {
    OPEN_DISPS(play->state.gfxCtx);
    gSPStencil(POLY_OPA_DISP++, WL_STENCIL_COMPOSITE);
    gDPPipeSync(POLY_OPA_DISP++);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_ZBUFFER | G_CULL_BACK);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_CULL_FRONT); // back faces, no depth test
    gDPSetCombineLERP(POLY_OPA_DISP++, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0,
                      PRIMITIVE);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2); // no z-test
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, col[0], col[1], col[2], alpha);
    CLOSE_DISPS(play->state.gfxCtx);
    EmitIcosphere(play->state.gfxCtx);
}

// Cast one light's pool: the Wind Waker stencil light-volume technique. Two z-fail mask passes mark the
// world surfaces inside the icosphere (the polygon cross-section), then a self-clearing composite tints
// those pixels with the light colour. The mask is occluded by all opaque geometry (the world and actors,
// which are already in the depth buffer at this point), so pools conform and self-occlude correctly.
static void DrawLightPool(PlayState* play, LightNode* node, LightPoint* p, f32 worldRadius, f32 angleY, f32 angleX,
                          const u8 col[3], u8 alpha) {
    if (worldRadius <= 0.0f) {
        return;
    }
    WorldLightLoadMatrix(play, node, p, angleY, angleX, worldRadius);
    WorldLightMaskPass(play, WL_STENCIL_INCR, G_CULL_FRONT); // back faces  → stencil ++ (z-fail)
    WorldLightMaskPass(play, WL_STENCIL_DECR, G_CULL_BACK);  // front faces → stencil -- (z-fail)
    WorldLightCompositePass(play, col, alpha);
}

// Debug overlay (dev tools): draw the light's icosphere as a ~50% translucent, depth-test-free ball tinted
// by the light colour and shaded per face (a brightness gradient baked into the geometry, so the two-axis
// tumble reads as 3D), making the volumes used for the pools visible (position/size/rotation/count). Back
// faces are culled for a solid look. Must run right after DrawLightPool for the same light — it reuses the
// modelview matrix that loaded.
static void DrawDebugSphere(PlayState* play, const u8 col[3]) {
    OPEN_DISPS(play->state.gfxCtx);
    gSPStencil(POLY_OPA_DISP++, WL_STENCIL_OFF); // not masked by the pool stencil
    gDPPipeSync(POLY_OPA_DISP++);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_ZBUFFER | G_CULL_FRONT);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK); // shade facets, solid front
    // colour = light colour * baked per-face shade; alpha = constant (~50%).
    gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, 0, SHADE, 0, 0, 0, 0, PRIMITIVE, PRIMITIVE, 0, SHADE, 0, 0, 0, 0,
                      PRIMITIVE);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2); // no z-test, so every sphere shows
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, col[0], col[1], col[2], 128);
    CLOSE_DISPS(play->state.gfxCtx);
    EmitIcosphere(play->state.gfxCtx);
}

// Map an intensity slider value (0..2) to a composite blend alpha (0..255).
static u8 WorldLightAlpha(f32 intensity) {
    f32 a = intensity * 0.5f;
    a = (a < 0.0f) ? 0.0f : ((a > 1.0f) ? 1.0f : a);
    return (u8)(a * 255.0f);
}

// OnPlayDrawWorldLights handler: walk the frame's active point lights and cast a pool from each. Runs
// once per frame inside Play_Draw's display-list scope (after the room, before actors).
static void DrawWorldLights(void* playPtr) {
    PlayState* play = (PlayState*)playPtr;
    if (play == NULL) {
        return;
    }

    BuildIcosphere();

    f32 sizeMult = CVarGetFloat(CVAR_ENHANCEMENT("Graphics.WorldLighting.SphereSize"), kDefaultSphereSize);
    // "Use Wind Waker default movement" pins the tumble + size pulse to the authentic 1x (and the GUI
    // disables those two sliders); otherwise the sliders drive them.
    bool wwMovement = CVarGetInteger(CVAR_ENHANCEMENT("Graphics.WorldLighting.WWDefaultMovement"), 1);
    f32 rotSpeed =
        wwMovement ? 1.0f : CVarGetFloat(CVAR_ENHANCEMENT("Graphics.WorldLighting.RotationSpeed"), kDefaultRotationSpeed);
    f32 sizeFlicker =
        wwMovement ? 1.0f : CVarGetFloat(CVAR_ENHANCEMENT("Graphics.WorldLighting.SizeFlicker"), kDefaultSizeFlicker);
    f32 intensity = CVarGetFloat(CVAR_ENHANCEMENT("Graphics.WorldLighting.Intensity"), kDefaultIntensity);
    bool showSpheres = CVarGetInteger(CVAR_DEVELOPER_TOOLS("WorldLighting.ShowLightSpheres"), 0);

    // Seconds-per-draw from R_UPDATE_RATE (3 = 20 fps normal play) keeps slider rates labelled in
    // per-second units regardless of update rate; frame interpolation replays the draw without re-running
    // this, so it doesn't double-count. (Spin is advanced per-light below, modulated by brightness.)
    f32 dt = (R_UPDATE_RATE > 0 ? R_UPDATE_RATE : 3) / 60.0f;

    f32 naviIntensity = CVarGetFloat(CVAR_ENHANCEMENT("Graphics.WorldLighting.NaviIntensity"), kDefaultNaviIntensity);
    u8 alpha = WorldLightAlpha(intensity);
    u8 naviAlpha = WorldLightAlpha(naviIntensity);

    // Bump the generation so lights not seen this frame can be pruned afterward.
    sStateGen++;

    // Navi's light (Link's fairy) bounces fast, so its pool pops; let players exclude it and size it
    // separately from torches. Identify Navi via the player's navi actor (En_Elf with FAIRY_NAVI params)
    // and match its two LightInfos by address.
    bool useNavi = CVarGetInteger(CVAR_ENHANCEMENT("Graphics.WorldLighting.UseNaviLight"), 1);
    f32 naviSize = CVarGetFloat(CVAR_ENHANCEMENT("Graphics.WorldLighting.NaviSphereSize"), kDefaultNaviSphereSize);
    // Identify Navi's two lights by address so her pool can be sized/brightened separately. Her colour is
    // tinted at the source (EnElf_UpdateLights), so the pool's live colour already carries the Navi tint.
    LightInfo* naviGlow = NULL;
    LightInfo* naviNoGlow = NULL;
    {
        Player* player = GET_PLAYER(play);
        if ((player != NULL) && (player->naviActor != NULL) && (player->naviActor->id == ACTOR_EN_ELF) &&
            (player->naviActor->params == FAIRY_NAVI)) {
            EnElf* navi = (EnElf*)player->naviActor;
            naviGlow = &navi->lightInfoGlow;
            naviNoGlow = &navi->lightInfoNoGlow;
        }
    }

    LightNode* node = play->lightCtx.listHead;
    while (node != NULL) {
        LightInfo* info = node->info;
        if ((info != NULL) && (info->type != LIGHT_DIRECTIONAL)) {
            bool isNavi = (info == naviGlow) || (info == naviNoGlow);
            if (!isNavi || useNavi) {
                WorldLightState* s = WorldLightGetState(info);
                LightPoint* p = &info->params.point;
                // Live colour: the flame flicker and the Navi tint are both applied at the source, so the
                // pool inherits them directly.
                u8 col[3] = { p->color[0], p->color[1], p->color[2] };

                // Wind Waker two-axis tumble, scaled by the Rotation Speed slider (same for every light).
                s->angleY += kWWRotYRate * rotSpeed * dt;
                s->angleX += kWWRotXRate * rotSpeed * dt;
                if (s->angleY > (2.0f * M_PI)) {
                    s->angleY -= (2.0f * M_PI);
                }
                if (s->angleX > (2.0f * M_PI)) {
                    s->angleX -= (2.0f * M_PI);
                }

                // Wind Waker SIZE flicker (the dominant pulse): re-roll a target every 0.10-0.30 s and ease
                // toward it (slew-capped). Decoupled from the game's brightness noise, so it reads slow and
                // organic. Navi isn't a flame, so it keeps a steady size.
                f32 worldRadius;
                if (isNavi) {
                    // Navi blinks on/off fast — her glow radius snaps full<->0 as she ducks into Link's
                    // pocket (she's hidden, not destroyed, so the light keeps coming through at radius 0).
                    // Ease the radius to/from zero so the pool fades instead of popping.
                    f32 target = (p->radius > 0) ? (f32)p->radius : 0.0f;
                    s->spawnRadius = WWEase(s->spawnRadius, target, 0.3f, 10000.0f, dt);
                    if ((target <= 0.0f) && (s->spawnRadius < 0.5f)) {
                        s->spawnRadius = 0.0f; // fully out — DrawLightPool skips radius <= 0, so no wasted passes
                    }
                    worldRadius = s->spawnRadius * naviSize;
                } else {
                    s->sizeTimer -= dt;
                    if (s->sizeTimer <= 0.0f) {
                        s->sizeTimer = 0.10f + (Rand_ZeroOne() * 0.20f);                       // [0.10, 0.30) s
                        s->sizeTarget = 1.0f + ((Rand_ZeroOne() - 0.5f) * 0.10f * sizeFlicker); // ±5% × depth
                    }
                    s->sizeCur = WWEase(s->sizeCur, s->sizeTarget, 0.4f, 0.05f, dt);
                    worldRadius = p->radius * sizeMult * s->sizeCur;
                }

                // Wind Waker BRIGHTNESS flicker (subtle fine grain): a faster, gentle eased random-walk on
                // the pool alpha. Kept subtle per the reference ("do not make the brightness flicker hard").
                u8 thisAlpha;
                if (isNavi) {
                    thisAlpha = naviAlpha;
                } else {
                    s->alphaTimer -= dt;
                    if (s->alphaTimer <= 0.0f) {
                        s->alphaTimer = Rand_ZeroOne() * 0.167f;           // [0, 0.167) s
                        s->alphaTarget = 0.90f + (Rand_ZeroOne() * 0.10f); // [0.90, 1.0]
                    }
                    s->alphaCur = WWEase(s->alphaCur, s->alphaTarget, 1.0f, 0.08f, dt);
                    f32 a = (f32)alpha * s->alphaCur;
                    thisAlpha = (u8)(a < 0.0f ? 0.0f : (a > 255.0f ? 255.0f : a));
                }

                DrawLightPool(play, node, &info->params.point, worldRadius, s->angleY, s->angleX, col, thisAlpha);
                if (showSpheres && (worldRadius > 0.0f)) {
                    DrawDebugSphere(play, col); // reuses DrawLightPool's loaded matrix
                }
            }
        }
        node = node->next;
    }

    // Prune per-light state for lights not seen this frame (despawned torches, an excluded Navi, etc.).
    for (auto it = sLightState.begin(); it != sLightState.end();) {
        if (it->second.gen != sStateGen) {
            it = sLightState.erase(it);
        } else {
            ++it;
        }
    }

    // Reset the stencil mode so the actors drawn after this pass (and everything downstream) render
    // normally. The mode persists in the renderer until changed, so this is required even when no lights
    // were cast (cheap: an empty flush + a no-op mode set).
    OPEN_DISPS(play->state.gfxCtx);
    gSPStencil(POLY_OPA_DISP++, WL_STENCIL_OFF);
    CLOSE_DISPS(play->state.gfxCtx);
}

// ---------------------------------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------------------------------

void RegisterWorldLighting() {
    // Only hook while enabled, so a disabled feature adds no per-frame work. Off by default — most players
    // want the cel shading; light casting is opt-in.
    bool enabled = CVarGetInteger(CVAR_ENHANCEMENT("Graphics.WorldLighting.Enabled"), 0);
    COND_HOOK(OnPlayDrawWorldLights, enabled, DrawWorldLights);
}

static RegisterShipInitFunc initFunc(RegisterWorldLighting, { CVAR_ENHANCEMENT("Graphics.WorldLighting.Enabled") });
