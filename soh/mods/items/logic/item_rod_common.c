/**
 * item_rod_common.c - Shared core for the magic rods (Fire / Ice / Light)
 *
 * Unity-included from custom_items.c (alongside item_rod_fire/ice/light.c).
 * See item_rod_common.h for the RodConfig descriptor and the rationale.
 *
 * This is the START of a larger de-duplication. Only the genuinely-pure,
 * identical-modulo-a-constant pieces are migrated here so far. The per-rod .c
 * files still own everything else; the exact remaining steps + risks are
 * documented at the bottom of this file.
 */

#include "item_rod_common.h"

// -----------------------------------------------------------------------------
// RodCommon_CalcVelocity
//
// Verbatim port of FireRod_CalcVelocity / IceRod_CalcVelocity /
// LightRod_CalcVelocity. Those three were byte-identical except for the
// *_PROJ_SPEED literal used to seed the local velocity vector; that literal is
// now cfg->projSpeed.
// -----------------------------------------------------------------------------
void RodCommon_CalcVelocity(const RodConfig* cfg, Vec3f* outVel, s16 yaw, s16 pitch) {
    Vec3f localVel = { 0.0f, 0.0f, 0.0f };
    localVel.z = cfg->projSpeed;
    Matrix_Push();
    Matrix_RotateY(BINANG_TO_RAD(yaw), MTXMODE_NEW);
    Matrix_RotateX(BINANG_TO_RAD(pitch), MTXMODE_APPLY);
    Matrix_MultVec3f(&localVel, outVel);
    Matrix_Pop();
}

/* =============================================================================
 * REMAINING MIGRATION STEPS (intentionally deferred — see task notes)
 * =============================================================================
 *
 * Why it stopped here: the remaining "shared" functions are only shared in
 * SHAPE. In substance each one touches a block of PER-ROD runtime state that
 * lives as DISTINCT members of gCustomItemState (fireRodCharging vs
 * iceRodCharging vs lightRodCharging, *ChargeLevel, *State, *SpinActive,
 * *SpinRadius, *BlureIdx, the per-rod RodProjSet[] arrays, etc.) and a block of
 * per-rod file-static flags (sChargeButtonHeld, sSpinColliderInited, ...).
 * Pulling those into one implementation requires either:
 *   (a) a RodRuntime struct of pointers wired in each rod's RodConfig
 *       (u8* charging; f32* chargeLevel; u8* state; RodProjSet* sets;
 *        u8* spinColliderInited; s16* chargeHoldCounter; ...), or
 *   (b) passing those addresses through each shared call.
 * That is a wide, easy-to-get-subtly-wrong change with no compiler available
 * here, so it was deliberately left for a follow-up where it can be built and
 * play-tested. CalcVelocity is pure (no runtime state), which is why it was a
 * safe first move.
 *
 * Concrete next steps, lowest-risk first:
 *
 * 1. Wire Ice and Light to RodCommon_CalcVelocity too (Fire is already done):
 *    delete IceRod_CalcVelocity / LightRod_CalcVelocity and replace their call
 *    sites with RodCommon_CalcVelocity(&sIceRodConfig, ...) /
 *    (&sLightRodConfig, ...). Add a sIceRodConfig / sLightRodConfig with at
 *    least .projSpeed set (mirror the existing sFireRodConfig in
 *    item_rod_fire.c). Pure constant swap, no runtime-state risk.
 *
 * 2. Migrate the projectile-set pool helpers next, parameterized by a
 *    `RodProjSet* sets` pointer + `const ColliderCylinderInit* projColInit`
 *    (both already in RodConfig once you add the array pointer):
 *       _GetProjSets, _HasAnyActiveSet, _DestroySetColliders,
 *       _FindFreeSet, _InitSetColliders.
 *    These only need the array + the collider blob — no gCustomItemState.
 *    Add `RodProjSet* sets;` to RodConfig (or pass it explicitly).
 *
 * 3. Migrate _InitSingleProjectile / _InitTripleProjectile. They need
 *    cfg->projSpeed, cfg->singleTimerMax (Light=25 vs 30), cfg->slashRange,
 *    cfg->slashSpreadDeg, plus RodCommon_FindFreeSet/_InitSetColliders/
 *    _CalcVelocity from step 2.
 *
 * 4. Migrate _UpdateOneSet / _UpdateProjectile using the element hooks
 *    (cfg->onProjHit, cfg->updateCollider, cfg->spawnSparks), cfg->projRotZStep
 *    and cfg->approachStep. CAUTION: _UpdateProjectile also copies the active
 *    set into rod-specific gCustomItemState.*RodProj* fields for the Harpoon
 *    network visual (different members per rod) and Ice additionally calls
 *    IceRod_CheckRedIceMelt. Those two bits must stay as per-rod callbacks
 *    (add e.g. `RodSyncNetFn syncNet;` and an optional `RodPostUpdateFn`).
 *
 * 5. Migrate the charge state machine (_CanCharge/_StartCharge/_UpdateCharge/
 *    _ReleaseCharge/_CancelCharge) and first-person + ProcessSwing + reticle.
 *    These are the ones that need the RodRuntime pointer block (charging /
 *    chargeLevel / state / chargeButtonHeld / chargeHoldCounter / spinActive /
 *    spin radius / blureIdx). Light also overrides the charge aura color at max
 *    charge — keep that as a small per-rod branch or a cfg->maxChargeColor.
 *
 * Each step compiles and play-tests on its own; do NOT land a partial step.
 */
