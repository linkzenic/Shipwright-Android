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
u8 BossSuperDamage_IsActive(PlayState* play);

// Legacy soft-glow VFX (single-textured fuzzy sprite). Kept for parity with the
// FHG-flash family; new bosses should prefer the MM-style sparks below.
void BossSuperDamage_SpawnVfx(PlayState* play, Actor* boss, Vec3f* limbWorldPos, s16 scale, s16 count);

// ─── OOT lightning sparks (gGanonLightningDL from ovl_Boss_Ganon2)
//
// Same visual as the Dark Beast Ganon transformation cutscene — sharp white
// lightning-bolt sprites radiating outward from the enemy's limbs. Uses an
// OOT-native asset (always available in oot.otr), no mm.o2r dependency.
//
// Usage pattern (per boss):
//   1. On hit: call StartElectricSparks() with a frame duration (60-120 typical).
//   2. From the boss's Draw function (every frame): call DrawElectricSparks()
//      with an array of limb world positions and a base scale (1.0 = typical boss).
//
// The timer is tracked in a small internal side-table keyed by Actor*, so no
// boss struct changes are required. Slot is reclaimed when the timer expires.
// Alpha fades over the last 30 frames so the burst trails off cleanly.

// Refresh/start the spark timer for `boss`. Idempotent — calling again resets
// the timer instead of stacking. Idle bosses cost nothing (no slot consumed).
void BossSuperDamage_StartElectricSparks(Actor* boss, s16 durationFrames);

// Render 2 lightning bolts per limb position this frame and decrement the timer.
// Safe to call every frame from the boss's Draw — does nothing if no active timer.
// `limbsPos` should be `limbCount` world-space anchors (typically the boss's
// head, body center, limb tips). `scale` multiplies the bolt size (1.0 = ~40-80
// unit-tall bolts; use 0.6-0.8 for tiny bosses, 1.5-2.5 for huge ones).
void BossSuperDamage_DrawElectricSparks(Actor* boss, PlayState* play, Vec3f* limbsPos, s32 limbCount, f32 scale);

#ifdef __cplusplus
}
#endif

#endif // BOSS_SUPER_DAMAGE_H
