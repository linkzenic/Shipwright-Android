/**
 * equip_byrna.c - Cane of Byrna (Extended Sword Slot 1)
 *
 * Behavior: Biggoron Sword IA (long range, two-handed) + HP & MP recovery on hit.
 * - Forces PLAYER_IA_SWORD_BIGGORON for long reach
 * - Forces swordHealth > 0 so charge/spin attacks work
 * - Draws Somaria cane mesh with BLUE materials at 1.15x scale
 * - Follows left hand rotation (sword hand)
 * - On melee hit: recover HP + MP
 *
 * Included by ext_equip_behavior.c (unity build).
 */

// Somaria cane tri DLs + header included by ext_equip_behavior.c

// ---------------------------------------------------------------------------
// Blue material DLs (same structure as Somaria's red materials, but blue)
// ---------------------------------------------------------------------------
static Gfx gfx_byrna_cane_mat_body[] = {
    gsSPLoadGeometryMode(G_SHADE | G_FOG | G_CULL_BACK | G_ZBUFFER | G_SHADING_SMOOTH | G_LIGHTING),
    gsDPPipeSync(),
    gsDPSetCombineLERP(0, 0, 0, SHADE, 0, 0, 0, 1, COMBINED, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED),
    gsSPSetOtherMode(G_SETOTHERMODE_H, 4, 20,
                     G_TD_CLAMP | G_CYC_2CYCLE | G_AD_NOISE | G_CD_MAGICSQ | G_TP_PERSP | G_TL_TILE | G_TF_BILERP |
                         G_CK_NONE | G_PM_NPRIMITIVE | G_TT_NONE | G_TC_FILT),
    gsSPSetOtherMode(G_SETOTHERMODE_L, 0, 32, G_RM_FOG_SHADE_A | G_AC_NONE | G_ZS_PIXEL | G_RM_AA_ZB_OPA_SURF2),
    gsSPTexture(65535, 65535, 0, 0, 1),
    gsDPSetPrimColor(0, 0, 41, 80, 200, 255), // Blue body (was 177, 50, 41 red)
    gsSPEndDisplayList(),
};

static Gfx gfx_byrna_cane_mat_color[] = {
    gsSPLoadGeometryMode(G_SHADE | G_FOG | G_CULL_BACK | G_ZBUFFER | G_SHADING_SMOOTH | G_LIGHTING),
    gsDPPipeSync(),
    gsDPSetCombineLERP(0, 0, 0, SHADE, 0, 0, 0, 1, COMBINED, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED),
    gsSPSetOtherMode(G_SETOTHERMODE_H, 4, 20,
                     G_TD_CLAMP | G_CYC_2CYCLE | G_AD_NOISE | G_CD_MAGICSQ | G_TP_PERSP | G_TL_TILE | G_TF_BILERP |
                         G_CK_NONE | G_PM_NPRIMITIVE | G_TT_NONE | G_TC_FILT),
    gsSPSetOtherMode(G_SETOTHERMODE_L, 0, 32, G_RM_FOG_SHADE_A | G_AC_NONE | G_ZS_PIXEL | G_RM_AA_ZB_OPA_SURF2),
    gsSPTexture(65535, 65535, 0, 0, 1),
    gsDPSetPrimColor(0, 0, 80, 140, 255, 255), // Blue orb (was 56, 83, 113 dark)
    gsSPEndDisplayList(),
};

// Byrna main DL: blue materials + shared Somaria tri geometry
Gfx g_byrna_cane_dl[] = {
    gsDPPipeSync(),
    gsSPClearGeometryMode(G_CULL_BACK | G_FOG | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR),
    gsSPSetGeometryMode(G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_LIGHTING),
    gsSPDisplayList(gfx_byrna_cane_mat_body),
    gsSPDisplayList(gfx_somaria_cane_tri_0),
    gsSPDisplayList(gfx_byrna_cane_mat_color),
    gsSPDisplayList(gfx_somaria_cane_tri_1),
    gsSPEndDisplayList(),
};

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define BYRNA_HP_RECOVER 16         // HP recovered per hit
#define BYRNA_MP_RECOVER 4          // MP recovered per hit
#define BYRNA_SCALE (0.05f * 1.15f) // Somaria base scale * 1.15

// ---------------------------------------------------------------------------
// Melee Hit Callback
// ---------------------------------------------------------------------------
static void Byrna_OnMeleeHit(Player* player, PlayState* play) {
    s32 damage = 0;

    if (player->meleeWeaponQuads[0].base.atFlags & AT_HIT) {
        damage = player->meleeWeaponQuads[0].info.toucher.damage;
    } else if (player->meleeWeaponQuads[1].base.atFlags & AT_HIT) {
        damage = player->meleeWeaponQuads[1].info.toucher.damage;
    }

    if (damage <= 0)
        return;

    // Recover 16 HP per hit
    Health_ChangeBy(play, BYRNA_HP_RECOVER);

    // Recover 16 MP per hit
    gSaveContext.magic += BYRNA_MP_RECOVER;
    if (gSaveContext.magic > gSaveContext.magicCapacity) {
        gSaveContext.magic = gSaveContext.magicCapacity;
    }
}

// ---------------------------------------------------------------------------
// Per-frame Behavior
// ---------------------------------------------------------------------------
static void Byrna_Behavior(Player* player, PlayState* play) {
    // Skip during cutscenes, dying, loading, etc.
    if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_LOADING |
                               PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_GETTING_ITEM)) {
        return;
    }

    // Save original sword state before overriding (only once)
    if (!gExtEquipBehavior.byrnaActive) {
        gExtEquipBehavior.byrnaSavedSwordEquip =
            (gSaveContext.equips.equipment >> gEquipShifts[EQUIP_TYPE_SWORD]) & 0xF;
        gExtEquipBehavior.byrnaSavedButtonItem = gSaveContext.equips.buttonItems[0];
        gExtEquipBehavior.byrnaSavedSwordHealth = gSaveContext.swordHealth;
        gExtEquipBehavior.byrnaSavedBgsFlag = gSaveContext.bgsFlag;
        gExtEquipBehavior.byrnaActive = 1;
    }

    // Only force BGS IA when player is actively holding a sword (not sheathed/NONE, not C-button items)
    if (player->heldItemAction == PLAYER_IA_SWORD_MASTER || player->heldItemAction == PLAYER_IA_SWORD_KOKIRI) {
        player->heldItemAction = PLAYER_IA_SWORD_BIGGORON;
    }

    // Force BGS equipment so the sword system works
    Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_BIGGORON);

    // Keep B button showing BGS item (icon override handled by ExtInv_GetItemIcon)
    gSaveContext.equips.buttonItems[0] = ITEM_SWORD_BGS;

    // Force bgsFlag = 1 so the game treats this as true BGS (no durability loss).
    // Without this, if the player owns Giant's Knife (bgsFlag=0), swings would
    // decrement swordHealth and eventually "break" the GK.
    gSaveContext.bgsFlag = 1;

    // Force swordHealth > 0 so spin attack / charge attack works
    // (BGS normally breaks when swordHealth reaches 0)
    if (gSaveContext.swordHealth <= 0.0f) {
        gSaveContext.swordHealth = 8.0f;
    }
}

// Restore original sword state when Byrna is unequipped
static void Byrna_Cleanup(void) {
    if (!gExtEquipBehavior.byrnaActive)
        return;

    // Restore original sword equipment
    Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, gExtEquipBehavior.byrnaSavedSwordEquip);
    gSaveContext.equips.buttonItems[0] = gExtEquipBehavior.byrnaSavedButtonItem;
    gSaveContext.swordHealth = gExtEquipBehavior.byrnaSavedSwordHealth;
    gSaveContext.bgsFlag = gExtEquipBehavior.byrnaSavedBgsFlag;
    gExtEquipBehavior.byrnaActive = 0;
}

// Draw is now handled by PostLimbDraw in z_player_lib.c via ExtEquip_DrawSwordDL
// This ensures the cane follows the exact same rotation as the sword during swings
