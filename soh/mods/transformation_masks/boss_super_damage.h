/**
 * boss_super_damage.h — Unified detection + VFX for FD/Pika Gigantamax boss hits.
 *
 * Bosses call BossSuperDamage_IsActive() in their hit handler. If true, they
 * choose their own "paralyzed?" condition (often = a specific actionFunc) and
 * either transition to stun or apply damage. Both branches typically spawn the
 * FHG flash VFX via BossSuperDamage_SpawnVfx().
 *
 * Replaces the previous per-boss `DMG_UNBLOCKABLE` bypass chunks, which broke
 * the original state machines.
 */

#ifndef BOSS_SUPER_DAMAGE_H
#define BOSS_SUPER_DAMAGE_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// True when a "super attack" hit should be treated as paralyze-or-damage:
//   - Pikachu Gigantamax is active (set by pikachu_form.cpp during attack frames)
//   - OR player is in Fierce Deity form and currently swinging the melee weapon
// Bosses call this in their hit handler before evaluating normal damage logic.
// Use for AC-hit handlers on LARGE targets where the swing is still active the
// frame the hit is read back.
u8 BossSuperDamage_IsActive(PlayState* play);

// True whenever the player is simply IN Fierce Deity form OR Pikachu Gigantamax
// mode (persistent — NOT gated on an active swing frame). Use for contact-based
// boss triggers (small/fast targets, or "touch the damage collider") where the
// AC/AT hit is detected one frame after the swing and the attack-frame check
// (BossSuperDamage_IsActive) would already have lapsed.
u8 BossSuperDamage_IsFormActive(PlayState* play);

// Geometric "did the super-form attack reach this point?" detector — bypasses the
// normal AT/AC dmgFlags match entirely. Use when a boss's bumper rejects the FD
// sword's damage flags (e.g. Barinade's boomerang-only support) but you still want
// FD/Pika to land. Returns 1 if the player (in FD/Pika form) is TOUCHING the target
// (body within `range`) OR the FD sword blade segment reaches it while swinging.
// Boomerang / normal play never match (gated on FD/Pika form), so vanilla
// behavior — and boomerang regression tests — are unaffected.
u8 BossSuperDamage_FormAttackReaches(PlayState* play, Vec3f* targetPos, f32 range);

// Effective "attack reach" of the current super-form attack, in world units, for
// a boss that wants ONE attack to break everything when the player is close enough:
//   - FD with the sword beam (full health) → long  reach (ranged "thunder").
//   - FD melee only (not full health)       → short reach (must be near).
//   - Pika Gigantamax with Thunder active    → long reach; melee → short reach.
//   - 0 when the player is NOT currently attacking in a super form.
// The boss compares this (plus its own body-size slack) against the player's
// distance to the boss body; when in range, all parts break/stun the same frame.
f32 BossSuperDamage_FormAttackRange(PlayState* play);

// Per-super-hit damage for the reworked bosses. Pikachu Gigantamax = 8 (max); Fierce Deity = 4
// (its MM Fierce Deity slash). Use in place of the old hardcoded `health -= 4`.
u8 BossSuperDamage_FormDamage(PlayState* play);

// Legacy soft-glow VFX (single-textured fuzzy sprite). Kept for parity with the
// FHG-flash family; new bosses should prefer the MM-style sparks below.
void BossSuperDamage_SpawnVfx(PlayState* play, Actor* boss, Vec3f* limbWorldPos, s16 scale, s16 count);

// ─── MM electric sparks (faithful port of Actor_DrawDamageEffects ELECTRIC_SPARKS)
//
// The EXACT effect Odolwa/Goht summon when hit by electric damage in MM
// (mm z_actor.c:5222-5269): a small (~30-unit) billboarded quad
// (gElectricSparkModelDL) drawn twice per limb, cycling 4 spark textures
// (gElectricSpark1-4Tex, one per gameplay frame) at random rotation + small
// random offset. Combiner (PRIM-ENV)*TEXEL+ENV → white PRIM core, blue ENV
// contour. Textures come from mm.o2r (objects/gameplay_keep); the material +
// quad are built inline. If mm.o2r is missing, the effect silently no-ops.
//
// Usage pattern (per boss):
//   1. On hit: call StartElectricSparks() with a frame duration (60-120 typical).
//   2. From the boss's Draw function (every frame): call DrawElectricSparks()
//      with an array of limb world positions and a base scale (1.0 = typical boss).
//
// The timer is tracked in a small internal side-table keyed by Actor*, so no
// boss struct changes are required. Slot is reclaimed when the timer expires.
// Alpha fades over the last 20 frames so the burst trails off cleanly.

// Refresh/start the spark timer for `boss`. Idempotent — calling again resets
// the timer instead of stacking. Idle bosses cost nothing (no slot consumed).
void BossSuperDamage_StartElectricSparks(Actor* boss, s16 durationFrames);

// Render 2 sparks per limb position this frame and decrement the timer. Safe to
// call every frame from the boss's Draw — does nothing if no active timer.
// `limbsPos` should be `limbCount` world-space anchors (the boss's joint
// positions, captured during PostLimbDraw). `scale` multiplies the spark size
// (1.0 ≈ 45-unit sparks; use 0.6-0.8 for tiny bosses, 1.5-2.5 for huge ones).
void BossSuperDamage_DrawElectricSparks(Actor* boss, PlayState* play, Vec3f* limbsPos, s32 limbCount, f32 scale);

#ifdef __cplusplus
}
#endif

#endif // BOSS_SUPER_DAMAGE_H
