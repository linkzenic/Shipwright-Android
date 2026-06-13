/**
 * twilight_upgrade.c - Twilight Upgrade query/grant helpers.
 *
 * Logic lives in the per-item mod files (clawshot mode in z_player/arms_hook
 * hooks, gale boomerang in en_boom). This translation unit just owns the bit
 * accessors so other code doesn't need to know about the gSaveContext.ship
 * field layout.
 */
#include "twilight_upgrade.h"
#include "macros.h"
#include "functions.h"

u8 TwilightUpgrade_HasClawshot(void) {
    return (gSaveContext.ship.twilightUpgrade & TWILIGHT_UPGRADE_CLAWSHOT) != 0;
}

u8 TwilightUpgrade_HasBombArrows(void) {
    return (gSaveContext.ship.twilightUpgrade & TWILIGHT_UPGRADE_BOMB_ARROWS) != 0;
}

u8 TwilightUpgrade_HasGaleBoomerang(void) {
    return (gSaveContext.ship.twilightUpgrade & TWILIGHT_UPGRADE_GALE_BOOMERANG) != 0;
}

u8 TwilightUpgrade_IsObtained(void) {
    return gSaveContext.ship.twilightUpgrade != 0;
}

u8 TwilightUpgrade_IsFullyObtained(void) {
    return (gSaveContext.ship.twilightUpgrade & TWILIGHT_UPGRADE_ALL) == TWILIGHT_UPGRADE_ALL;
}

void TwilightUpgrade_Grant(void) {
    gSaveContext.ship.twilightUpgrade |= TWILIGHT_UPGRADE_ALL;
}

void TwilightUpgrade_SetClawshot(u8 on) {
    if (on) {
        gSaveContext.ship.twilightUpgrade |= TWILIGHT_UPGRADE_CLAWSHOT;
    } else {
        gSaveContext.ship.twilightUpgrade &= ~TWILIGHT_UPGRADE_CLAWSHOT;
    }
}

void TwilightUpgrade_SetBombArrows(u8 on) {
    if (on) {
        gSaveContext.ship.twilightUpgrade |= TWILIGHT_UPGRADE_BOMB_ARROWS;
    } else {
        gSaveContext.ship.twilightUpgrade &= ~TWILIGHT_UPGRADE_BOMB_ARROWS;
    }
}

void TwilightUpgrade_SetGaleBoomerang(u8 on) {
    if (on) {
        gSaveContext.ship.twilightUpgrade |= TWILIGHT_UPGRADE_GALE_BOOMERANG;
    } else {
        gSaveContext.ship.twilightUpgrade &= ~TWILIGHT_UPGRADE_GALE_BOOMERANG;
    }
}

u8 TwilightUpgrade_ClawshotAvailable(void) {
    if (!TwilightUpgrade_HasClawshot()) {
        return 0;
    }
    // Requires hookshot or longshot as the underlying weapon (clawshot is a mode of those).
    return (INV_CONTENT(ITEM_HOOKSHOT) == ITEM_HOOKSHOT) || (INV_CONTENT(ITEM_LONGSHOT) == ITEM_LONGSHOT);
}

u8 TwilightUpgrade_BombArrowsAvailable(void) {
    // Upgrade bit OR explicit ownership (auto-grant CVar populates the inv slot).
    if (TwilightUpgrade_HasBombArrows()) {
        return 1;
    }
    return INV_CONTENT(ITEM_BOMB_ARROWS) == ITEM_BOMB_ARROWS;
}

u8 TwilightUpgrade_GaleBoomerangAvailable(void) {
    if (!TwilightUpgrade_HasGaleBoomerang()) {
        return 0;
    }
    return INV_CONTENT(ITEM_BOOMERANG) == ITEM_BOOMERANG;
}

// Mode toggle accessors. Returning 0 when the upgrade isn't unlocked guards
// gameplay hooks so they don't accidentally apply modes the player hasn't
// earned (e.g. if the save bit got corrupted or set via debug without the
// upgrade flag).
u8 TwilightUpgrade_IsClawshotActive(void) {
    if (!TwilightUpgrade_HasClawshot()) {
        return 0;
    }
    return gSaveContext.ship.clawshotModeActive != 0;
}

u8 TwilightUpgrade_IsGaleBoomerangActive(void) {
    if (!TwilightUpgrade_HasGaleBoomerang()) {
        return 0;
    }
    return gSaveContext.ship.galeBoomerangModeActive != 0;
}

void TwilightUpgrade_SetClawshotActive(u8 active) {
    gSaveContext.ship.clawshotModeActive = active ? 1 : 0;
}

void TwilightUpgrade_SetGaleBoomerangActive(u8 active) {
    gSaveContext.ship.galeBoomerangModeActive = active ? 1 : 0;
}
