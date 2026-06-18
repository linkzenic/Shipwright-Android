/**
 * item_rod_common.h - Shared core for the magic rods (Fire / Ice / Light)
 *
 * The three magic rods (item_rod_fire.c, item_rod_ice.c, item_rod_light.c) are
 * near-identical: a multi-set projectile system, a charge state machine, a
 * first-person aiming mode, a swing dispatcher and a reticle, all differing
 * only by tuning constants plus a handful of genuinely element-specific hooks
 * (Ice's red-ice melt, Light's undead paralysis / stun / sparkle, Fire's
 * flame explosion).
 *
 * This header declares:
 *   - The shared RodProjSet projectile-set struct (already used by all 3 rods).
 *   - A RodConfig descriptor that captures every per-rod knob (constants, the
 *     collider-init blobs, the body/glow color, and function pointers for the
 *     element-specific hooks) so the common implementation can be parameterized.
 *   - The shared functions that have been migrated into item_rod_common.c.
 *
 * MIGRATION STATUS (incremental, see item_rod_common.c):
 *   - RodCommon_CalcVelocity: migrated + proven (Fire wired as template).
 *   - Everything else still lives in the per-rod .c files; the remaining steps
 *     are documented at the bottom of item_rod_common.c.
 */

#ifndef ITEM_ROD_COMMON_H
#define ITEM_ROD_COMMON_H

#include "z64.h"
#include "../helpers/fx_helper.h" // RodColor

// Number of concurrent projectile sets per rod. Mirrors the value historically
// defined in each rod's own header; guarded so include order doesn't matter.
#ifndef ROD_MAX_PROJ_SETS
#define ROD_MAX_PROJ_SETS 5
#endif

// =============================================================================
// MULTI-SET PROJECTILE STRUCT (shared by fire/ice/light rods)
// Kept byte-identical to the definition in the per-rod headers and guarded by
// the same ROD_PROJ_SET_DEFINED token, so whichever header is seen first wins
// and the other definitions are skipped.
// =============================================================================
#ifndef ROD_PROJ_SET_DEFINED
#define ROD_PROJ_SET_DEFINED
typedef struct {
    Vec3f pos[3];
    Vec3f vel[3];
    Vec3f trail[6];
    s16 timer;
    f32 scale;
    f32 targetScale;
    s16 rotZ;
    u8 count;
    u8 active;
    s16 yaw;
    s16 pitch;
    ColliderCylinder colliders[3];
    u8 collidersInited;
} RodProjSet;
#endif // ROD_PROJ_SET_DEFINED

// =============================================================================
// PER-ROD DESCRIPTOR
//
// One static const RodConfig instance lives in each rod's .c file. It bundles
// the tuning constants + collider blobs + color + element hook function
// pointers. The shared item_rod_common.c implementation reads from this so the
// state-machine / projectile / first-person code exists exactly once.
//
// NOTE: the rods also keep a block of per-rod runtime state in gCustomItemState
// (e.g. fireRodCharging, iceRodCharging, ...). Those are distinct struct members
// per rod, so when more functions are migrated the RodConfig will additionally
// need a "runtime" sub-struct of pointers to each rod's scalars, OR the shared
// code must be handed the addresses explicitly. CalcVelocity (already migrated)
// is pure and needs none of that, which is why it was chosen as the template.
// =============================================================================

// Element-specific hit handler: called when a projectile collider registers a
// hit. `pos` is the projectile position, `p` the player. Implements the
// explosion / freeze / paralysis behavior unique to each element.
typedef void (*RodOnProjHitFn)(ColliderCylinder* col, Vec3f* pos, PlayState* play, Player* p);

// Element-specific projectile collider sizing (radius/height differ per rod).
typedef void (*RodUpdateColliderFn)(ColliderCylinder* col, Vec3f* pos, f32 scale, PlayState* play);

// Element-specific trailing sparkle/particle spawn for an in-flight projectile.
typedef void (*RodSpawnSparksFn)(PlayState* play, Vec3f* pos, f32 scale);

typedef struct {
    // ---- Projectile tuning ----
    f32 projSpeed;     // units/frame a projectile travels (was *_PROJ_SPEED)
    s16 projLifetime;  // frames (was *_PROJ_LIFETIME)
    s16 singleTimerMax; // clamp for single-projectile timer (Fire/Ice 30, Light 25)
    f32 slashRange;    // was *_SLASH_RANGE
    s16 slashSpreadDeg; // was *_SLASH_SPREAD (degrees)
    s16 projRotZStep;  // per-frame rotZ spin (Fire/Ice 5000, Light 6000)
    f32 approachStep;  // Math_ApproachF step for scale (Fire 0.4, Ice 0.4, Light 0.5)

    // ---- Magic / backfire ----
    s16 magicSlash, magicStab, magicJump, magicSpinSmall, magicSpinBig;
    u8 backfireSlash, backfireJump, backfireSpin;

    // ---- Charge tuning ----
    f32 chargeRate, chargeMin, chargeBig;
    s16 chargeHoldFrames;

    // ---- Spin tuning ----
    f32 spinSmallRadius, spinBigRadius;

    // ---- Visuals ----
    RodColor color;
    u8 reticleR, reticleG, reticleB;

    // ---- Collider init blobs (per element AT damage type) ----
    const ColliderCylinderInit* projColInit;
    const ColliderCylinderInit* spinColInit;

    // ---- Element-specific hooks ----
    RodOnProjHitFn onProjHit;          // Fire explosion / Ice freeze / Light stun
    RodUpdateColliderFn updateCollider; // radius/height formula
    RodSpawnSparksFn spawnSparks;       // in-flight trail particles
} RodConfig;

// =============================================================================
// MIGRATED SHARED FUNCTIONS
// =============================================================================

// Computes a forward velocity vector of magnitude cfg->projSpeed, rotated by
// `yaw` (around Y) then `pitch` (around X). Verbatim port of the per-rod
// *Rod_CalcVelocity helpers, parameterized by speed.
void RodCommon_CalcVelocity(const RodConfig* cfg, Vec3f* outVel, s16 yaw, s16 pitch);

#endif // ITEM_ROD_COMMON_H
