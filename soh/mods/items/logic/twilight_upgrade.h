/**
 * twilight_upgrade.h - Twilight Upgrade (TP-inspired item-mode upgrade)
 *
 * A single upgrade that, once obtained, unlocks three item-mode toggles:
 *   1. Clawshot mode  — A on hookshot/longshot reverses pull direction
 *                       (target → Link) and enables chain grappling.
 *   2. Bomb Arrows    — Bomb arrows appear in the arrow wheel without
 *                       requiring the AutoGrantOnBag CVar.
 *   3. Gale Boomerang — A on boomerang enables multi-target routing
 *                       (L/R add targets) + Z-target B-boost to boomerang
 *                       (Twilight Princess clawshot-jump style).
 *
 * Persistence: gSaveContext.ship.twilightUpgrade is a u8 bitfield where each
 * bit corresponds to one of the three unlocks. Initially all three bits flip
 * together when the upgrade is granted, but the bit layout lets future
 * randomizer integration shuffle them individually.
 */
#ifndef TWILIGHT_UPGRADE_H
#define TWILIGHT_UPGRADE_H

#include "z64.h"

#define TWILIGHT_UPGRADE_CLAWSHOT       (1 << 0)
#define TWILIGHT_UPGRADE_BOMB_ARROWS    (1 << 1)
#define TWILIGHT_UPGRADE_GALE_BOOMERANG (1 << 2)
#define TWILIGHT_UPGRADE_ALL \
    (TWILIGHT_UPGRADE_CLAWSHOT | TWILIGHT_UPGRADE_BOMB_ARROWS | TWILIGHT_UPGRADE_GALE_BOOMERANG)

#ifdef __cplusplus
extern "C" {
#endif

// Bit-level queries — returns 1 if the specific sub-upgrade is set.
u8 TwilightUpgrade_HasClawshot(void);
u8 TwilightUpgrade_HasBombArrows(void);
u8 TwilightUpgrade_HasGaleBoomerang(void);

// Whole-upgrade queries.
u8 TwilightUpgrade_IsObtained(void); // returns 1 if any bit is set
u8 TwilightUpgrade_IsFullyObtained(void); // returns 1 if all 3 bits are set

// Grant the full upgrade. Idempotent.
void TwilightUpgrade_Grant(void);

// Per-bit setters — set or clear a single sub-upgrade. Used by the save-flag
// UI in SohMenuNEI.cpp so each Twilight bit can be toggled on/off
// independently per save (the user wants to mix-and-match for testing /
// rando shuffling each bit individually).
void TwilightUpgrade_SetClawshot(u8 on);
void TwilightUpgrade_SetBombArrows(u8 on);
void TwilightUpgrade_SetGaleBoomerang(u8 on);

// Item-availability shortcuts. These combine the upgrade bit with the
// prerequisite item (e.g. Clawshot requires hookshot OR longshot to be useful).
u8 TwilightUpgrade_ClawshotAvailable(void); // upgrade + (hookshot || longshot)
u8 TwilightUpgrade_BombArrowsAvailable(void); // upgrade OR AutoGrantOnBag CVar OR explicit inv
u8 TwilightUpgrade_GaleBoomerangAvailable(void); // upgrade + boomerang owned

// Mode toggle accessors — read/write the active mode for each upgraded item.
// The mode is persisted in gSaveContext.ship and gets flipped via the kaleido
// selector (A on hookshot/longshot or boomerang). 0 = vanilla, 1 = upgraded.
u8 TwilightUpgrade_IsClawshotActive(void);
u8 TwilightUpgrade_IsGaleBoomerangActive(void);
void TwilightUpgrade_SetClawshotActive(u8 active);
void TwilightUpgrade_SetGaleBoomerangActive(u8 active);

#ifdef __cplusplus
}
#endif

#endif // TWILIGHT_UPGRADE_H
