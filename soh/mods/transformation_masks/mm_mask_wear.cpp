/**
 * mm_mask_wear.cpp - MM Mask Wearing System
 *
 * Draws MM mask DLs on Link's head using the head limb's transformation matrix.
 * Worn mask DLs come from mm.o2r, matching MM's D_801C0B20[] table.
 *
 * Each mask has a per-mask effect switch (empty stubs for now).
 * Extra rotation offsets per mask are provided for future fine-tuning.
 */

extern "C" {
#include "z64.h"
#include "z64item.h"
#include "macros.h"
#include "functions.h"
}

#include "mods/transformation_masks/mm_mask_wear.h"
#include "mods/transformation_masks/assets/mm_asset_loader.h"

// =============================================================================
// Worn Mask DL OTR Paths (from MM D_801C0B20[] - z_player_lib.c line 2853)
// =============================================================================
// Indexed 0-23 matching ITEM_MM_MASK_POSTMAN(0xB7) through ITEM_MM_MASK_FIERCE_DEITY(0xCE).

static const char* sMmWornMaskDLPaths[24] = {
    "__OTR__objects/object_mask_posthat/object_mask_posthat_DL_000290",   // 0  Postman's Hat
    "__OTR__objects/object_mask_yofukasi/object_mask_yofukasi_DL_000490", // 1  All-Night Mask
    "__OTR__objects/object_mask_bakuretu/object_mask_bakuretu_DL_0005C0", // 2  Blast Mask
    "__OTR__objects/object_mask_stone/object_mask_stone_DL_000820",       // 3  Stone Mask
    "__OTR__objects/object_mask_bigelf/object_mask_bigelf_DL_0016F0",     // 4  Great Fairy Mask
    "__OTR__objects/gameplay_keep/gDekuMaskDL",                           // 5  Deku Mask
    "__OTR__objects/object_mask_ki_tan/object_mask_ki_tan_DL_0004A0",     // 6  Keaton Mask
    "__OTR__objects/object_mask_bree/object_mask_bree_DL_0003C0",         // 7  Bremen Mask
    "__OTR__objects/object_mask_rabit/object_mask_rabit_DL_000610",       // 8  Bunny Hood
    "__OTR__objects/object_mask_gero/gDonGeroMaskDL",                     // 9  Don Gero's Mask
    "__OTR__objects/object_mask_bu_san/object_mask_bu_san_DL_000710",     // 10 Mask of Scents
    "__OTR__objects/gameplay_keep/gGoronMaskDL",                          // 11 Goron Mask
    "__OTR__objects/object_mask_romerny/object_mask_romerny_DL_0007A0",   // 12 Romani Mask
    "__OTR__objects/object_mask_zacho/object_mask_zacho_DL_000700",       // 13 Circus Leader Mask
    "__OTR__objects/object_mask_kerfay/gKafeisMaskDL",                    // 14 Kafei's Mask
    "__OTR__objects/object_mask_meoto/object_mask_meoto_DL_0005A0",       // 15 Couple's Mask
    "__OTR__objects/object_mask_truth/object_mask_truth_DL_0001A0",       // 16 Mask of Truth
    "__OTR__objects/gameplay_keep/gZoraMaskDL",                           // 17 Zora Mask
    "__OTR__objects/object_mask_dancer/object_mask_dancer_DL_000EF0",     // 18 Kamaro's Mask
    "__OTR__objects/object_mask_gibudo/object_mask_gibudo_DL_000250",     // 19 Gibdo Mask
    "__OTR__objects/object_mask_json/object_mask_json_DL_0004C0",         // 20 Garo's Mask
    "__OTR__objects/object_mask_skj/object_mask_skj_DL_0009F0",           // 21 Captain's Hat
    "__OTR__objects/object_mask_kyojin/object_mask_kyojin_DL_000380",     // 22 Giant's Mask
    "__OTR__objects/gameplay_keep/gFierceDeityMaskDL",                    // 23 Fierce Deity Mask
};

// =============================================================================
// Extra rotation offsets per mask (s16 x, y, z)
// All 0 for now - adjust per-mask if positioning looks wrong.
// =============================================================================

static Vec3s sMmMaskRotOffset[24] = {
    { 0, 0, 0 }, // 0  Postman's Hat
    { 0, 0, 0 }, // 1  All-Night Mask
    { 0, 0, 0 }, // 2  Blast Mask
    { 0, 0, 0 }, // 3  Stone Mask
    { 0, 0, 0 }, // 4  Great Fairy Mask
    { 0, 0, 0 }, // 5  Deku Mask
    { 0, 0, 0 }, // 6  Keaton Mask
    { 0, 0, 0 }, // 7  Bremen Mask
    { 0, 0, 0 }, // 8  Bunny Hood
    { 0, 0, 0 }, // 9  Don Gero's Mask
    { 0, 0, 0 }, // 10 Mask of Scents
    { 0, 0, 0 }, // 11 Goron Mask
    { 0, 0, 0 }, // 12 Romani Mask
    { 0, 0, 0 }, // 13 Circus Leader Mask
    { 0, 0, 0 }, // 14 Kafei's Mask
    { 0, 0, 0 }, // 15 Couple's Mask
    { 0, 0, 0 }, // 16 Mask of Truth
    { 0, 0, 0 }, // 17 Zora Mask
    { 0, 0, 0 }, // 18 Kamaro's Mask
    { 0, 0, 0 }, // 19 Gibdo Mask
    { 0, 0, 0 }, // 20 Garo's Mask
    { 0, 0, 0 }, // 21 Captain's Hat
    { 0, 0, 0 }, // 22 Giant's Mask
    { 0, 0, 0 }, // 23 Fierce Deity Mask
};

// =============================================================================
// State
// =============================================================================

static s32 sCurrentMmMask = ITEM_NONE;

#define MM_MASK_ITEM_BASE ITEM_MM_MASK_POSTMAN
#define MM_MASK_COUNT 24

static inline s32 MaskItemToIndex(s32 itemId) {
    return itemId - MM_MASK_ITEM_BASE;
}

// =============================================================================
// Toggle
// =============================================================================

extern "C" void MmMaskWear_Toggle(PlayState* play, Player* player, s32 itemId) {
    s32 idx = MaskItemToIndex(itemId);
    if (idx < 0 || idx >= MM_MASK_COUNT) {
        return;
    }

    if (sCurrentMmMask == itemId) {
        // Already wearing this mask - take it off
        sCurrentMmMask = ITEM_NONE;
    } else {
        // Put on this mask, clear any OOT mask
        sCurrentMmMask = itemId;
        player->currentMask = PLAYER_MASK_NONE;
    }

    Player_PlaySfx(&player->actor, NA_SE_PL_CHANGE_ARMS);
    player->stateFlags2 |= PLAYER_STATE2_FOOTSTEP;
}

// =============================================================================
// Draw (called from Player_PostLimbDrawGameplay for HEAD limb)
// =============================================================================

extern "C" void MmMaskWear_Draw(PlayState* play, Player* player) {
    if (sCurrentMmMask == ITEM_NONE) {
        return;
    }

    if (!MmAssets_IsAvailable()) {
        return;
    }

    s32 idx = MaskItemToIndex(sCurrentMmMask);
    if (idx < 0 || idx >= MM_MASK_COUNT) {
        return;
    }

    const char* dlPath = sMmWornMaskDLPaths[idx];
    Vec3s* rot = &sMmMaskRotOffset[idx];

    OPEN_DISPS(play->state.gfxCtx);

    if (rot->x != 0 || rot->y != 0 || rot->z != 0) {
        // Apply extra rotation offset if non-zero
        Matrix_Push();
        Matrix_RotateZYX(rot->x, rot->y, rot->z, MTXMODE_APPLY);

        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)dlPath);

        Matrix_Pop();
    } else {
        // No extra rotation - draw directly in head limb space
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)dlPath);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

// =============================================================================
// Per-Mask Effect Update (empty stubs for now)
// =============================================================================

extern "C" void MmMaskWear_Update(PlayState* play, Player* player) {
    if (sCurrentMmMask == ITEM_NONE) {
        return;
    }

    s32 idx = MaskItemToIndex(sCurrentMmMask);

    switch (idx) {
        case 0: // Postman's Hat
            break;
        case 1: // All-Night Mask
            break;
        case 2: // Blast Mask
            break;
        case 3: // Stone Mask
            break;
        case 4: // Great Fairy Mask
            break;
        case 5: // Deku Mask (transformation - shouldn't reach here)
            break;
        case 6: // Keaton Mask
            break;
        case 7: // Bremen Mask
            break;
        case 8: // Bunny Hood
            break;
        case 9: // Don Gero's Mask
            break;
        case 10: // Mask of Scents
            break;
        case 11: // Goron Mask (transformation - shouldn't reach here)
            break;
        case 12: // Romani Mask
            break;
        case 13: // Circus Leader Mask
            break;
        case 14: // Kafei's Mask
            break;
        case 15: // Couple's Mask
            break;
        case 16: // Mask of Truth
            break;
        case 17: // Zora Mask (transformation - shouldn't reach here)
            break;
        case 18: // Kamaro's Mask
            break;
        case 19: // Gibdo Mask
            break;
        case 20: // Garo's Mask
            break;
        case 21: // Captain's Hat
            break;
        case 22: // Giant's Mask
            break;
        case 23: // Fierce Deity Mask (transformation - shouldn't reach here)
            break;
        default:
            break;
    }
}

// =============================================================================
// Queries
// =============================================================================

extern "C" s32 MmMaskWear_GetCurrent(void) {
    return sCurrentMmMask;
}

extern "C" void MmMaskWear_Clear(void) {
    sCurrentMmMask = ITEM_NONE;
}
