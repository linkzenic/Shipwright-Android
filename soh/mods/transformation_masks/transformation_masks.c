/**
 * transformation_masks.c - MM Transformation Masks Router
 *
 * Routes calls from z_player.c hooks to mm_player_form.cpp implementation.
 * Asset replacement getters remain here (they work independently).
 */

#include "mods/transformation_masks/transformation_masks.h"
#include "mods/transformation_masks/assets/mm_asset_loader.h"

// =============================================================================
// Forward declarations to mm_player_form.cpp (compiled separately as .cpp)
// =============================================================================

extern void MmForm_Init(PlayState* play, Player* player);
extern u8 MmForm_IsEnabled(void);
extern u8 MmForm_IsTransformed(void);
extern TransformMaskId MmForm_GetMaskType(s32 item);
extern void MmForm_HandleMaskUse(PlayState* play, Player* player, s32 item);
extern void MmForm_Update(PlayState* play, Player* player);
extern void MmForm_Draw(PlayState* play, Player* player);
extern void MmForm_Reset(void);

// =============================================================================
// Cached MM Assets (loaded once, used forever)
// =============================================================================

// Deku Mask (replaces Skull Mask)
static Gfx* sCachedDekuMaskDL = NULL;
static void* sCachedDekuMaskNameTex = NULL;
static u8 sDekuMaskDLLoaded = 0;
static u8 sDekuMaskNameLoaded = 0;

// Stone Mask (replaces Spooky Mask)
static Gfx* sCachedStoneMaskDL = NULL;
static void* sCachedStoneMaskNameTex = NULL;
static u8 sStoneMaskDLLoaded = 0;
static u8 sStoneMaskNameLoaded = 0;

// Fierce Deity Mask (replaces Gerudo Mask)
static Gfx* sCachedFierceMaskDL = NULL;
static void* sCachedFierceMaskNameTex = NULL;
static u8 sFierceMaskDLLoaded = 0;
static u8 sFierceMaskNameLoaded = 0;

// =============================================================================
// Replacement Check Functions
// =============================================================================

u8 TransformMasks_DekuReplacesSkull(void) {
    return MmAssets_IsReplacementActive("gMods.TransformMasks.DekuReplacesSkull");
}

u8 TransformMasks_StoneReplacesSpooky(void) {
    return MmAssets_IsReplacementActive("gMods.TransformMasks.StoneReplacesSpooky");
}

u8 TransformMasks_FierceReplacesGerudo(void) {
    return MmAssets_IsReplacementActive("gMods.TransformMasks.FierceReplacesGerudo");
}

// =============================================================================
// No-op Action Function (replaces OOT actionFunc while transformed)
//
// OOT's Player_UpdateCommon (z_player.c ~line 11994) runs BEFORE actionFunc:
//   - invincibility timer, Player_UpdateInterface, Player_UpdateZTargeting
//   - Player_ProcessControlStick (populates prevControlStickMagnitude/Angle)
//   - Collision/gravity (Actor_MoveXZGravity, Player_ProcessSceneCollision)
// Then calls: this->actionFunc(this, play)  <-- lands here
// After: Player_UpdateCamAndSeqModes, Collider_UpdateCylinder, etc.
//
// We intentionally do nothing here. All gameplay comes from MmForm_Update.
// =============================================================================

void MmForm_OotNoopAction(Player* thisx, PlayState* play) {
    // Empty: OOT actions disabled while MM form is active.
}

// =============================================================================
// Routing to mm_player_form.cpp
// =============================================================================

u8 TransformMasks_IsEnabled(void) {
    return MmForm_IsEnabled();
}

u8 TransformMasks_IsTransformed(void) {
    return MmForm_IsTransformed();
}

TransformMaskId TransformMasks_GetMaskType(s32 item) {
    return MmForm_GetMaskType(item);
}

void TransformMasks_HandleMaskUse(PlayState* play, Player* player, s32 item) {
    MmForm_HandleMaskUse(play, player, item);
}

void TransformMasks_Init(PlayState* play, Player* player) {
    MmForm_Init(play, player);
}

void TransformMasks_Update(PlayState* play, Player* player) {
    MmForm_Update(play, player);
}

void TransformMasks_Draw(PlayState* play, Player* player) {
    MmForm_Draw(play, player);
}

void TransformMasks_Reset(void) {
    MmForm_Reset();
}

// =============================================================================
// Asset Replacement Getters
// =============================================================================

/**
 * Get replacement DL for worn mask (on Link's face)
 * @param playerMask PLAYER_MASK_* value (1-8)
 * @return MM mask DL if replacement active, else NULL (use OOT default)
 *
 * NOTE: Uses WORN DLs from gameplay_keep or object_mask_*, NOT Get Item DLs!
 * - Get Item DLs (object_gi_*) are for the spinning 3D model when receiving item
 * - Worn DLs (gameplay_keep, object_mask_*) are for mask attached to Link's face
 *
 * From 2Ship z_player_lib.c D_801C0B20[] array:
 * - gDekuMaskDL (gameplay_keep) for PLAYER_MASK_DEKU
 * - object_mask_stone_DL_000820 for PLAYER_MASK_STONE
 * - gFierceDeityMaskDL (gameplay_keep) for PLAYER_MASK_FIERCE_DEITY
 */
Gfx* TransformMasks_GetMaskDL(s32 playerMask) {
    // PLAYER_MASK_SKULL = 2 -> Deku Mask (worn on face)
    if (playerMask == 2 && TransformMasks_DekuReplacesSkull()) {
        if (!sDekuMaskDLLoaded) {
            sCachedDekuMaskDL = (Gfx*)MmAssets_LoadDekuMaskWornDL();
            sDekuMaskDLLoaded = 1;
            printf("[TransformMasks] Deku Mask WORN DL: %p\n", (void*)sCachedDekuMaskDL);
        }
        return sCachedDekuMaskDL;
    }

    // PLAYER_MASK_SPOOKY = 3 -> Stone Mask (worn on face)
    if (playerMask == 3 && TransformMasks_StoneReplacesSpooky()) {
        if (!sStoneMaskDLLoaded) {
            sCachedStoneMaskDL = (Gfx*)MmAssets_LoadStoneMaskWornDL();
            sStoneMaskDLLoaded = 1;
            printf("[TransformMasks] Stone Mask WORN DL: %p\n", (void*)sCachedStoneMaskDL);
        }
        return sCachedStoneMaskDL;
    }

    // PLAYER_MASK_GERUDO = 7 -> Fierce Deity Mask (worn on face)
    if (playerMask == 7 && TransformMasks_FierceReplacesGerudo()) {
        if (!sFierceMaskDLLoaded) {
            sCachedFierceMaskDL = (Gfx*)MmAssets_LoadFierceMaskWornDL();
            sFierceMaskDLLoaded = 1;
            printf("[TransformMasks] Fierce Deity Mask WORN DL: %p\n", (void*)sCachedFierceMaskDL);
        }
        return sCachedFierceMaskDL;
    }

    return NULL;
}

/**
 * Get replacement name texture for inventory display
 * @param itemId ITEM_MASK_* value
 * @return MM name texture if replacement active, else NULL (use OOT default)
 */
void* TransformMasks_GetMaskNameTex(s32 itemId) {
    // ITEM_MASK_SKULL = 0x25 -> Deku Mask
    if (itemId == 0x25 && TransformMasks_DekuReplacesSkull()) {
        if (!sDekuMaskNameLoaded) {
            sCachedDekuMaskNameTex = MmAssets_LoadDekuMaskNameText();
            sDekuMaskNameLoaded = 1;
        }
        return sCachedDekuMaskNameTex;
    }

    // ITEM_MASK_SPOOKY = 0x26 -> Stone Mask
    if (itemId == 0x26 && TransformMasks_StoneReplacesSpooky()) {
        if (!sStoneMaskNameLoaded) {
            sCachedStoneMaskNameTex = MmAssets_LoadStoneMaskNameText();
            sStoneMaskNameLoaded = 1;
        }
        return sCachedStoneMaskNameTex;
    }

    // ITEM_MASK_GERUDO = 0x2A -> Fierce Deity Mask
    if (itemId == 0x2A && TransformMasks_FierceReplacesGerudo()) {
        if (!sFierceMaskNameLoaded) {
            sCachedFierceMaskNameTex = MmAssets_LoadFierceMaskNameText();
            sFierceMaskNameLoaded = 1;
        }
        return sCachedFierceMaskNameTex;
    }

    return NULL; // No replacement, use OOT default
}
