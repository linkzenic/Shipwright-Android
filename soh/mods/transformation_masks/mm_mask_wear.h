/**
 * mm_mask_wear.h - MM Mask Wearing System
 *
 * Draws MM mask DLs on Link's head when equipped from inventory page 3.
 * Transformation masks (Deku/Goron/Zora/Fierce) still trigger transformation;
 * all other MM masks are drawn visually on Link's head.
 *
 * DLs are loaded from mm.o2r using OTR paths matching the MM decomp's
 * D_801C0B20[] worn mask table (z_player_lib.c line 2853).
 */

#ifndef MM_MASK_WEAR_H
#define MM_MASK_WEAR_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Toggle wearing an MM mask on/off.
// If already wearing this mask, removes it. Otherwise puts it on.
// Called from item use in z_player.c for non-transformation MM masks.
void MmMaskWear_Toggle(PlayState* play, Player* player, s32 itemId);

// Draw the currently worn MM mask on Link's head.
// Must be called from Player_PostLimbDrawGameplay when limbIndex == PLAYER_LIMB_HEAD,
// so the matrix is in the head limb's coordinate space.
void MmMaskWear_Draw(PlayState* play, Player* player);

// Per-mask effect update (called every frame when a mask is worn).
// Each mask has a switch case for unique effects (empty stubs for now).
void MmMaskWear_Update(PlayState* play, Player* player);

// Get the item ID of the currently worn MM mask (ITEM_NONE if not wearing any).
s32 MmMaskWear_GetCurrent(void);

// Set the currently worn MM mask item ID (for remote player rendering override).
void MmMaskWear_SetCurrent(s32 maskItem);

// Clear the currently worn MM mask (e.g. on scene transition, death, transformation).
void MmMaskWear_Clear(void);

// Returns true if Stone Mask is currently worn (used by z_actor.c for invisibility).
s32 MmMaskWear_IsStoneMaskActive(void);

// Returns true if Blast Mask is on cooldown (used for draw blackout).
s32 MmMaskWear_IsBlastCooldown(void);

// Returns true if All-Night Mask is currently worn (used by En_Sw and En_Wood02).
s32 MmMaskWear_IsAllNightMaskActive(void);

// Returns true if Gibdo Mask is currently worn (used by En_Rd to switch
// Redeads/Gibdos to a friendly + dancing state, mirroring MM behavior).
s32 MmMaskWear_IsGibdoMaskWorn(void);

// Chateau Romani: infinite magic system (persists across scenes, cleared on death).
s32 MmMaskWear_IsChateauRomaniActive(void);
void MmMaskWear_ActivateChateauRomani(void);
void MmMaskWear_DeactivateChateauRomani(void);

// Draw overlay for Great Fairy teleport menu (call from PlayState draw, after HUD).
void MmMaskWear_DrawOverlay(PlayState* play);

// Returns true if Kamaro dance is active (used by z_player.c to freeze input).
s32 MmMaskWear_IsKamaroDancing(void);

// Returns true if Great Fairy warp menu is active (used by z_play.c to override pause).
s32 MmMaskWear_IsGreatFairyWarpActive(void);

// Update for Great Fairy warp menu while game is paused (called from z_play.c).
void MmMaskWear_GreatFairyWarpUpdate(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // MM_MASK_WEAR_H
