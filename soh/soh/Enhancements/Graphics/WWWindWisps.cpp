// Wind Waker-style wind wisps — the white streaks that curl through the sky on the wind.
//
// A port of WW's wind lines (dKankyo__Windline / WIND_EFF, via noclip's d_kankyo_wether.ts): each wisp
// is a point that flies along the wind, swerving on a sine wave (with a 20% chance of pulling a full
// loop-de-loop), fading in, cruising, then fading out and respawning ahead of the camera. In WW the
// visible streak is the particle trail the point leaves behind (JPA effect 0x31); we have no JPA, so
// the trail is rebuilt as a tapered translucent ribbon through the point's recent position history —
// visually equivalent.
//
// The ribbons are emitted into POLY_XLU with z-test on, so terrain and buildings occlude them properly
// (the XLU buffer executes after the world no matter when this hook appends to it).

#include <libultraship/bridge.h>
#include <ship/Context.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
#include "soh/frame_interpolation.h"
#include "WWSkyEnv.h"

#include <math.h>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
float OTRGetAspectRatio(void); // OTRGlobals.h declares this in its C-only section
}

#define CVAR_WWSKY_ENABLED CVAR_ENHANCEMENT("Graphics.WWSky.Enabled") // the "Use Sky" master toggle
#define CVAR_WISPS_ENABLED CVAR_ENHANCEMENT("Graphics.WWWindWisps.Enabled")
#define CVAR_WISPS_AMOUNT CVAR_ENHANCEMENT("Graphics.WWWindWisps.Amount")
#define CVAR_WISPS_SPEED CVAR_ENHANCEMENT("Graphics.WWWindWisps.Speed")

// Multiplier on WW's wind-driven count (10 x windPower). OoT's wind is usually calm (windPower sits at
// our 0.3 baseline), so WW's own 1x count yields only ~3 wisps; default to noclip's 4x density instead.
static constexpr float kDefaultAmount = 4.0f;
// Flight-speed multiplier. 1x is WW's own ~2800 units/s, which reads far faster inside OoT's 12800 far
// plane than in WW's huge spaces; 0.5x matches how noclip's wind lines read. Slowing the sim also
// shortens the trail (the trail is a fixed TIME window), pushing the streak toward noclip's short-fat
// comet look. Only the flight motion slows — the fade-in/out lifecycle stays at GameCube rate.
static constexpr float kDefaultSpeed = 0.5f;

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kTau = 2.0f * kPi;
// WW runs at 30 fps; this hook at 20 → scale per-frame steps by 30/20 (same convention as WWClouds).
static constexpr float kFrameScale = 1.5f;
// Binary angle to radians (WW's cM_s2rad).
static constexpr float kS2Rad = kTau / 65536.0f;

static constexpr int kMaxWisps = 50; // noclip's slot count (WW hardware uses 30)
static constexpr int kTrailLen = 16; // position history samples (~0.8s of flight at 20 fps)
// Cross-section brightness profile. WW's trail is a textured particle: a solid bright core with a
// quick falloff at the rim. A 3-vert ribbon (transparent edge - bright centre - transparent edge) is a
// linear gradient across the whole width, maximally bright only along an infinitely thin centreline —
// smudgy by construction. Instead, hold full alpha across a core and ramp to zero at a narrower edge.
static constexpr float kCoreHalfWidth = 12.0f;
static constexpr float kEdgeHalfWidth = 24.0f;

// One frozen trail sample — a stand-in for one of WW's trail particles. Everything is fixed at birth
// (position, ribbon side vector, alpha); only age-based fading changes afterwards. Recomputing any of
// these per frame makes the streak visibly squirm.
typedef struct {
    Vec3f pos;
    Vec3f side; // unit cross-section direction, frozen at emission
    float alpha;
} TrailPt;

// One wind line (WW WIND_EFF), plus the trail history that stands in for its particle trail.
typedef struct {
    int state; // 0 idle, 1 fading in / cruising, 2 fading out
    Vec3f basePos;
    Vec3f animPos;
    float respawnTimer; // idle frames left before this slot may spawn again
    float stateTimer;
    float alpha;
    float swerveAnimCounter;
    float swerveAngleXZ;
    float swerveAngleY;
    float loopCounter;
    bool doLoop;
    TrailPt trail[kTrailLen]; // ring buffer, world space
    int trailCount;
    int trailNext;
    Vec3f lastSide; // fallback when the flight direction degenerates against the view ray
} WindEff;

static WindEff sWisps[kMaxWisps];
static u32 sFrameCounter = 0;

// Ribbon vertices, one region per wisp: per trail sample a 4-vert cross-section (transparent edge,
// solid core pair, transparent edge). One region PER WISP is load-bearing: gSPVertex records a pointer that Fast3D
// only dereferences when the XLU list executes at the end of the frame, after every wisp has been
// emitted. With a single shared scratch buffer, every ribbon rendered the last-emitted wisp's
// geometry and vertex alpha — all trails wobbled in lockstep and vanished together whenever the
// last-drawn wisp changed.
static Vtx sWispVtx[kMaxWisps][kTrailLen * 4];

// ---------------------------------------------------------------------------------------------------
// Small helpers (local RNG; faithful cLib_addCalc / cLib_addCalcAngleRad ports)
// ---------------------------------------------------------------------------------------------------

static u32 sRng = 0x77123455u;
static float Rnd01() {
    sRng = sRng * 1664525u + 1013904223u;
    return (float)(sRng >> 8) * (1.0f / 16777216.0f);
}
static float RndF(float m) {
    return Rnd01() * m;
}
static float RndFX(float m) {
    return (Rnd01() * 2.0f - 1.0f) * m;
}
static float Sat(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static float AddCalc(float src, float target, float speed, float maxVel, float minVel) {
    float delta = target - src;
    float vel = speed * delta;
    float mag = fabsf(vel);
    if (mag < minVel) {
        mag = minVel;
    }
    if (mag > maxVel) {
        mag = maxVel;
    }
    if (mag > fabsf(delta)) {
        return target;
    }
    return src + (delta < 0.0f ? -mag : mag);
}

// Angle variant: shortest-path delta, and `speed` is a divisor (WW convention), not a rate.
static float AddCalcAngle(float src, float target, float speed, float maxVel, float minVel) {
    float da = fmodf(target - src, kTau);
    float delta = fmodf(2.0f * da, kTau) - da;
    float vel = delta / speed;
    float mag = fabsf(vel);
    if (mag < minVel) {
        mag = minVel;
    }
    if (mag > maxVel) {
        mag = maxVel;
    }
    if (mag > fabsf(delta)) {
        return src + delta;
    }
    return src + (delta < 0.0f ? -mag : mag);
}

// ---------------------------------------------------------------------------------------------------
// Simulation (port of dKyr_windline_move, WW-original constants)
// ---------------------------------------------------------------------------------------------------

static void SpawnWisp(PlayState* play, WindEff* e, float windX, float windZ) {
    // Spawn ahead of the camera (dKy_set_eyevect_calc2 with 4000), lifted 1000 up. WW's helper zeroes
    // the view vector's Y so the base always sits ahead HORIZONTALLY at camera height + lift — with
    // the full 3D forward, the normal gameplay camera (pitched slightly down at Link) dragged the base
    // to head height, where wisps read huge and fast. Clamp the pitch at horizontal instead of zeroing
    // it: level views get WW's placement, and a camera aimed at the sky still spawns high overhead.
    Vec3f fwd = { play->view.lookAt.x - play->view.eye.x, play->view.lookAt.y - play->view.eye.y,
                  play->view.lookAt.z - play->view.eye.z };
    if (fwd.y < 0.0f) {
        fwd.y = 0.0f;
    }
    float fl = sqrtf(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
    if (fl < 0.001f) {
        fl = 1.0f;
    }
    // WW spawns 4000 ahead inside a ~100k view distance; inside OoT's 12.8k bubble that reads close
    // and fat, so push the base toward the outer half — smaller on screen and slower angular motion is
    // most of what "vast" looks like. The lift (WW uses +1000) scales up with the distance to keep the
    // wisps at the same elevation in the view, well up in the sky.
    e->basePos.x = play->view.eye.x + fwd.x / fl * 6000.0f;
    e->basePos.y = play->view.eye.y + fwd.y / fl * 6000.0f + 2200.0f;
    e->basePos.z = play->view.eye.z + fwd.z / fl * 6000.0f;

    // Lateral scatter, sized to the screen. WW scatters a fixed ±2000 in world XZ (noclip ±10000) —
    // fine on a fixed 4:3 screen, but the port runs at arbitrary aspect and a fixed world-unit spread
    // covers only the middle of a wide window (±4000 happened to almost exactly fill a 4:3 frame at
    // this spawn distance, so wisps never entered the outer thirds of a widescreen view). Size the
    // spread from the actual frustum at the spawn distance — half-width = tan(fovy/2) x aspect x
    // distance — padded a little past the edges, and scatter along the camera's screen-right axis.
    float aspect = OTRGetAspectRatio();
    if (aspect < 0.1f) {
        aspect = 4.0f / 3.0f;
    }
    float halfW = tanf(play->view.fovy * 0.5f * kPi / 180.0f) * aspect * 6000.0f * 1.15f;
    if (halfW > 8500.0f) {
        halfW = 8500.0f; // an 11.5k recycle bubble can't cover a super-ultrawide edge to edge
    }
    // Horizontal screen-right; degenerate looking straight up/down, where any horizontal axis is fine.
    Vec3f right = { fwd.z, 0.0f, -fwd.x };
    float rl = sqrtf(right.x * right.x + right.z * right.z);
    if (rl < 0.001f) {
        right.x = 1.0f;
        right.z = 0.0f;
        rl = 1.0f;
    }
    float lateral = RndFX(halfW) / rl;
    float depth = RndFX(2500.0f);
    float upwind = 2000.0f + RndF(2000.0f);
    e->animPos.x = right.x * lateral + fwd.x / fl * depth - windX * upwind;
    e->animPos.y = RndFX(2400.0f) + fwd.y / fl * depth;
    e->animPos.z = right.z * lateral + fwd.z / fl * depth - windZ * upwind;
    // Pull an outlier back inside the far-recycle radius (spawn cap 10500 < recycle 11500) so a fresh
    // wisp never starts its life already being faded back out: take the largest s in [0,1] with
    // |(basePos - eye) + s * animPos| <= cap (the base itself is always well inside).
    {
        float bx = e->basePos.x - play->view.eye.x;
        float by = e->basePos.y - play->view.eye.y;
        float bz = e->basePos.z - play->view.eye.z;
        float aa = e->animPos.x * e->animPos.x + e->animPos.y * e->animPos.y + e->animPos.z * e->animPos.z;
        float ab = e->animPos.x * bx + e->animPos.y * by + e->animPos.z * bz;
        float bb = bx * bx + by * by + bz * bz - 10500.0f * 10500.0f;
        float disc = ab * ab - aa * bb;
        if (aa > 0.001f && disc > 0.0f) {
            float s = (-ab + sqrtf(disc)) / aa;
            if (s < 1.0f) {
                e->animPos.x *= s;
                e->animPos.y *= s;
                e->animPos.z *= s;
            }
        }
    }
    // Stand-in for WW's ground check, raised well above the eye line: below it, OoT's terrain z-tests
    // the wisp away — and since flight is horizontal, spawn altitude is cruising altitude, so this
    // floor is also what keeps a wisp from ever buzzing past at head height.
    float minY = play->view.eye.y + 1500.0f;
    if (e->basePos.y + e->animPos.y < minY) {
        e->animPos.y = minY - e->basePos.y + RndF(1500.0f);
    }

    e->swerveAnimCounter = RndF(kTau);
    e->swerveAngleXZ = atan2f(windX, windZ);
    e->swerveAngleY = 0.0f;
    e->loopCounter = 0.0f;
    e->doLoop = Rnd01() < 0.2f;
    e->stateTimer = 0.0f;
    e->alpha = 0.0f;
    e->trailCount = 0;
    e->trailNext = 0;
    e->state = 1;
}

// `dt` paces the fade lifecycle (GameCube wall-clock); `mdt` = dt x the Speed slider paces the flight
// motion — swerve, loops and travel all slow together so the path SHAPE stays WW's.
static void UpdateWisps(PlayState* play, int count, float dt, float mdt) {
    float windX, windZ, windPow;
    WWSkyEnv_Wind(play, &windX, &windZ, &windPow);

    sFrameCounter++;

    // First activation: spread the initial spawns across a whole lifecycle so the population starts
    // decorrelated instead of arriving as one wave.
    static bool sSeeded = false;
    if (!sSeeded) {
        for (int j = 0; j < kMaxWisps; j++) {
            sWisps[j].respawnTimer = RndF(200.0f);
        }
        sSeeded = true;
    }

    for (int i = 0; i < kMaxWisps; i++) {
        WindEff* e = &sWisps[i];

        if (e->state == 0) {
            // Random respawn gap rather than WW's round-robin stagger. The fade lifecycle is nearly
            // deterministic (~170 frames), so any fixed spawn cadence with a shorter full cycle
            // phase-locks the population into spawn-together/die-together waves (WW and noclip only
            // avoid this because their cycle, 8 frames x 40 slots, is longer than the lifetime).
            // Fresh jitter every generation keeps the slots decorrelated forever.
            e->respawnTimer -= dt;
            if (i < count && e->respawnTimer <= 0.0f) {
                SpawnWisp(play, e, windX, windZ);
            }
            continue;
        }

        // Swerve: a sine-driven wobble on top of easing back toward the wind direction.
        e->swerveAnimCounter += kS2Rad * 800.0f * mdt;
        float swerveAnimMag = kS2Rad * (250.0f - 0.2f * 250.0f * (1.0f - windPow));
        float change = mdt * swerveAnimMag * sinf(e->swerveAnimCounter);
        e->swerveAngleY += change;
        e->swerveAngleXZ += (i & 1) ? change : -change;

        if (e->stateTimer <= 0.5f || !e->doLoop) {
            // WW's per-tick ease, rescaled to wall-clock: the speed divisor shrinks by the timestep and
            // the per-frame velocity caps grow by it (same convention as WWClouds).
            float targetXZ = atan2f(windX, windZ);
            e->swerveAngleXZ =
                AddCalcAngle(e->swerveAngleXZ, targetXZ, 10.0f / mdt, kS2Rad * 1000.0f * mdt, kS2Rad * 1.0f * mdt);
            e->swerveAngleY =
                AddCalcAngle(e->swerveAngleY, 0.0f, 10.0f / mdt, kS2Rad * 1000.0f * mdt, kS2Rad * 1.0f * mdt);
        } else {
            // The signature move: pull a full vertical loop, then resume cruising. 2000/tick is
            // noclip's rate (it slows WW hardware's 3600 by 1.8x for a bigger, statelier loop).
            float loopStep = kS2Rad * 2000.0f * mdt;
            e->loopCounter += loopStep;
            e->swerveAngleY += loopStep;
            if (e->loopCounter > kS2Rad * 60535.0f) {
                e->doLoop = false;
            }
        }

        float swerveT = Sat(e->swerveAnimCounter / kTau);
        float mag = (1.3f * 80.0f - 0.2f * 80.0f * (1.0f - windPow)) * swerveT;
        e->animPos.x += cosf(e->swerveAngleY) * sinf(e->swerveAngleXZ) * mag * mdt;
        e->animPos.y += sinf(e->swerveAngleY) * mag * mdt;
        e->animPos.z += cosf(e->swerveAngleY) * cosf(e->swerveAngleXZ) * mag * mdt;

        // Record the head into the trail ring buffer.
        Vec3f head = { e->basePos.x + e->animPos.x, e->basePos.y + e->animPos.y, e->basePos.z + e->animPos.z };
        {
            // OoT's far plane is 12800: a wisp beyond it is invisible flight time (WW's view distance
            // is ~8x larger, so it never recycles). Force the fade-out so the slot frees soon — but
            // never kill on the spot: an instant kill pops the whole trail off screen in one frame,
            // and a fast camera can sweep several wisps past the radius together.
            float ddx = head.x - play->view.eye.x, ddy = head.y - play->view.eye.y, ddz = head.z - play->view.eye.z;
            if (ddx * ddx + ddy * ddy + ddz * ddz > 11500.0f * 11500.0f) {
                if (e->state == 1) {
                    e->state = 2;
                }
                if (e->stateTimer > 0.45f) {
                    e->stateTimer = 0.45f; // below the 0.5 threshold, so the alpha ramp-down starts now
                }
            }
        }
        {
            TrailPt* pt = &e->trail[e->trailNext];
            pt->pos = head;
            pt->alpha = e->alpha;
            // Freeze the ribbon side vector at emission: flight direction x view ray at this moment.
            int prevIdx = (e->trailNext - 1 + kTrailLen) % kTrailLen;
            Vec3f along;
            if (e->trailCount > 0) {
                along.x = head.x - e->trail[prevIdx].pos.x;
                along.y = head.y - e->trail[prevIdx].pos.y;
                along.z = head.z - e->trail[prevIdx].pos.z;
            } else {
                along.x = windX;
                along.y = 0.0f;
                along.z = windZ;
            }
            Vec3f toEye = { play->view.eye.x - head.x, play->view.eye.y - head.y, play->view.eye.z - head.z };
            Vec3f side = { along.y * toEye.z - along.z * toEye.y, along.z * toEye.x - along.x * toEye.z,
                           along.x * toEye.y - along.y * toEye.x };
            float sl = sqrtf(side.x * side.x + side.y * side.y + side.z * side.z);
            if (sl > 0.001f) {
                side.x /= sl;
                side.y /= sl;
                side.z /= sl;
                e->lastSide = side;
            } else {
                side = e->lastSide;
            }
            pt->side = side;
        }
        e->trailNext = (e->trailNext + 1) % kTrailLen;
        if (e->trailCount < kTrailLen) {
            e->trailCount++;
        }

        // The AddCalc velocity caps are per-frame quantities in WW's 30 fps sim; scale them (and the
        // alpha ramp) by dt so the fade-in/cruise/fade-out lifecycle runs at GameCube wall-clock speed
        // — unscaled, wisps stayed invisible ~50% longer after spawning.
        float maxVel = 0.08f + 0.008f * ((float)i / 30.0f);
        if (e->state == 1) {
            e->stateTimer = AddCalc(e->stateTimer, 1.0f, 0.3f * dt, 0.1f * maxVel * dt, 0.01f * dt);
            if (e->stateTimer >= 1.0f) {
                e->state = 2;
            }
            if (e->stateTimer > 0.5f) {
                e->alpha = AddCalc(e->alpha, 1.0f, 0.5f * dt, 0.05f * dt, 0.001f * dt);
            }
        } else {
            e->stateTimer =
                AddCalc(e->stateTimer, 0.0f, 0.5f * dt, maxVel * (0.1f + 0.01f * ((float)i / 30.0f)) * dt, 0.01f * dt);
            if (e->stateTimer < 0.5f) {
                e->alpha = AddCalc(e->alpha, 0.0f, 0.5f * dt, 0.05f * dt, 0.001f * dt);
            }
            if (e->stateTimer <= 0.0f) {
                e->state = 0;
                e->respawnTimer = RndF(60.0f);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// Ribbon rendering
// ---------------------------------------------------------------------------------------------------

static void WriteWispVtx(Vtx* v, float x, float y, float z, const u8 col[3], u8 alpha) {
    v->v.ob[0] = (s16)x;
    v->v.ob[1] = (s16)y;
    v->v.ob[2] = (s16)z;
    v->v.flag = 0;
    v->v.tc[0] = 0;
    v->v.tc[1] = 0;
    v->v.cn[0] = col[0];
    v->v.cn[1] = col[1];
    v->v.cn[2] = col[2];
    v->v.cn[3] = alpha;
}

// Build and emit one wisp's ribbon from its frozen trail particles: a 4-vert cross-section per
// sample (transparent edge, solid bright core, transparent edge). Positions and side vectors never
// change after emission — only age-based alpha/width fading — so the streak is world-anchored like
// WW's particle trail.
static void EmitWisp(PlayState* play, const WindEff* e, int slot, const u8 col[3], float alphaScale) {
    int n = e->trailCount;
    if (n < 2) {
        return;
    }
    Vtx* vtx = sWispVtx[slot];
    int tail = (e->trailNext - n + kTrailLen) % kTrailLen;
    Vec3f origin = e->trail[tail].pos; // any fixed reference works; verts are exact world positions

    OPEN_DISPS(play->state.gfxCtx);
    // Interpolation child keyed by (wisp, game frame): the frame part never repeats, so this child
    // never matches the previous game frame and the matrix below is used VERBATIM by every replayed
    // frame. It must be: the ribbon is world-anchored geometry whose origin (the oldest trail sample)
    // advances every sim tick with the vertices compensating — lerping the matrix while the vertices
    // snap made the whole trail swim sideways. Worse, under the default OPEN_DISPS key all wisps
    // shared one label and matched BY DRAW ORDER, so when the visible set changed a trail lerped
    // toward another wisp's origin entirely. (The camera still glides: the interpolated view matrix
    // is applied on top of this exact model matrix.)
    FrameInterpolation_RecordOpenChild(e, (int)sFrameCounter);
    Matrix_Translate(origin.x, origin.y, origin.z, MTXMODE_NEW);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);

    for (int k = 0; k < n; k++) {
        const TrailPt* pt = &e->trail[(tail + k) % kTrailLen];
        float age = (float)(n - 1 - k) / (kTrailLen - 1); // 0 = just emitted, 1 = oldest possible
        // Stretched-oval silhouette: WW's streak is fattest mid-trail and pinches to a point at both
        // ends (a head-widest taper reads as a fairy with a glowing head instead). Elliptical width,
        // uniform alpha along the trail — the tips vanish by geometry, which keeps the streak crisp
        // and means the oldest sample expiring from the ring buffer never pops visibly.
        float widthScale = sqrtf(4.0f * age * (1.0f - age));

        u8 a = (u8)(255.0f * alphaScale * pt->alpha);
        float coreW = kCoreHalfWidth * widthScale;
        float edgeW = kEdgeHalfWidth * widthScale;
        float px = pt->pos.x - origin.x, py = pt->pos.y - origin.y, pz = pt->pos.z - origin.z;
        Vtx* row = &vtx[k * 4];
        WriteWispVtx(&row[0], px - pt->side.x * edgeW, py - pt->side.y * edgeW, pz - pt->side.z * edgeW, col, 0);
        WriteWispVtx(&row[1], px - pt->side.x * coreW, py - pt->side.y * coreW, pz - pt->side.z * coreW, col, a);
        WriteWispVtx(&row[2], px + pt->side.x * coreW, py + pt->side.y * coreW, pz + pt->side.z * coreW, col, a);
        WriteWispVtx(&row[3], px + pt->side.x * edgeW, py + pt->side.y * edgeW, pz + pt->side.z * edgeW, col, 0);
    }

    // Emit per segment (8 verts = two cross-sections, 6 triangles).
    for (int k = 0; k + 1 < n; k++) {
        gSPVertex(POLY_XLU_DISP++, (uintptr_t)&vtx[k * 4], 8, 0);
        gSP2Triangles(POLY_XLU_DISP++, 0, 1, 5, 0, 0, 5, 4, 0);
        gSP2Triangles(POLY_XLU_DISP++, 1, 2, 6, 0, 1, 6, 5, 0);
        gSP2Triangles(POLY_XLU_DISP++, 2, 3, 7, 0, 2, 7, 6, 0);
    }

    FrameInterpolation_RecordCloseChild();
    CLOSE_DISPS(play->state.gfxCtx);
}

static void EmitWispState(PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_XLU_DISP++);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_FOG | G_CULL_FRONT | G_CULL_BACK);
    gSPSetGeometryMode(POLY_XLU_DISP++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
    gDPSetCombineMode(POLY_XLU_DISP++, G_CC_SHADE, G_CC_SHADE);
    // Z-test on (terrain occludes the wisps), no z-write, soft alpha blend. Cycle type and alpha
    // compare set explicitly — inherited RDP state, see the note in WWClouds.cpp.
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
    gDPSetCycleType(POLY_XLU_DISP++, G_CYC_1CYCLE);
    gDPSetAlphaCompare(POLY_XLU_DISP++, G_AC_NONE);
    CLOSE_DISPS(play->state.gfxCtx);
}

// ---------------------------------------------------------------------------------------------------
// Per-frame entry point (OnPlayDrawSkyClouds handler — the ribbons go to POLY_XLU, which executes
// after the world regardless of when this hook appends)
// ---------------------------------------------------------------------------------------------------

static void DrawWindWisps(void* playPtr) {
    PlayState* play = (PlayState*)playPtr;

    if (play->skyboxId != SKYBOX_NORMAL_SKY || play->envCtx.skyboxDisabled) {
        return;
    }

    float windX, windZ, windPow;
    WWSkyEnv_Wind(play, &windX, &windZ, &windPow);

    // WW: 10 lines at full wind. The Amount slider scales that (noclip renders 4x for density).
    float amount = CVarGetFloat(CVAR_WISPS_AMOUNT, kDefaultAmount);
    int count = (int)(10.0f * windPow * amount);
    if (count > kMaxWisps) {
        count = kMaxWisps;
    }

    float speed = CVarGetFloat(CVAR_WISPS_SPEED, kDefaultSpeed);
    if (speed < 0.1f) {
        speed = 0.1f; // mdt divides the angle-ease speed, so keep it strictly positive
    }
    UpdateWisps(play, count, kFrameScale, kFrameScale * speed);

    // Wisp tint/brightness ride the scheduled cloud-centre colour, so night wisps dim like WW's
    // (which scale alpha by the ambient brightness squared).
    WWSkyWeather weather = WWSkyEnv_Sample(play);
    WWSkyColors colors;
    WWSkyEnv_SampleColors(play, &weather, &colors);
    float colorAvg = (colors.kumoCenter[0] + colors.kumoCenter[1] + colors.kumoCenter[2]) / (3.0f * 255.0f);

    bool any = false;
    for (int i = 0; i < kMaxWisps; i++) {
        if (sWisps[i].state != 0 && sWisps[i].alpha > 0.01f) {
            any = true;
            break;
        }
    }
    if (!any) {
        return;
    }

    EmitWispState(play);
    for (int i = 0; i < kMaxWisps; i++) {
        WindEff* e = &sWisps[i];
        if (e->state == 0 || e->alpha <= 0.01f) {
            continue;
        }
        Vec3f head = { e->basePos.x + e->animPos.x, e->basePos.y + e->animPos.y, e->basePos.z + e->animPos.z };
        float dx = head.x - play->view.eye.x, dy = head.y - play->view.eye.y, dz = head.z - play->view.eye.z;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        float distFade = dist / 200.0f;
        if (distFade > 1.0f) {
            distFade = 1.0f;
        }
        float alphaFade = windPow * distFade * colorAvg * colorAvg;
        if (alphaFade < 0.5f) {
            alphaFade = 0.5f;
        }
        EmitWisp(play, e, i, colors.kumoCenter, alphaFade * e->alpha);
    }
}

// ---------------------------------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------------------------------

void RegisterWWWindWisps() {
    bool enabled = CVarGetInteger(CVAR_WWSKY_ENABLED, 0) && CVarGetInteger(CVAR_WISPS_ENABLED, 1);
    COND_HOOK(OnPlayDrawSkyClouds, enabled, DrawWindWisps);
}

static RegisterShipInitFunc initFunc(RegisterWWWindWisps, { CVAR_WWSKY_ENABLED, CVAR_WISPS_ENABLED });
