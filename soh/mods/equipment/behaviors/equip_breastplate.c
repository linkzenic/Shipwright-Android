/**
 * equip_breastplate.c - Spirit Breastplate (Extended Tunic Slot 2)
 *
 * Behavior: Magic Armor (TP-style) — rupee-cost damage immunity.
 * - Makes Link immune to all damage while wearing
 * - Each 1 HP of damage received costs 1 rupee
 * - No rupees = slow movement (cursed weight)
 * - With rupees: golden Iron Knuckle tint (Nabooru variant)
 * - Without rupees: dark Iron Knuckle tint
 * - Passive rupee drain: 1 rupee per 30 frames
 *
 * Included by ext_equip_behavior.c (unity build).
 */

// No extra includes — unity-built from ext_equip_behavior.c
extern void Rupees_ChangeBy(s16 rupeeChange);

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define BREASTPLATE_RUPEE_INTERVAL 30 // Passive drain: 1 rupee every N frames
#define BREASTPLATE_SLOW_MULT 0.5f    // Speed multiplier when broke

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static s16 sBreastplateRupeeTick = 0;

// ---------------------------------------------------------------------------
// Main Behavior
// ---------------------------------------------------------------------------
static void Breastplate_Behavior(Player* player, PlayState* play) {
    // Skip during cutscenes, dying, etc.
    if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_LOADING |
                               PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_GETTING_ITEM)) {
        return;
    }

    if (gSaveContext.rupees > 0) {
        // === Has rupees: full protection ===

        // Passive rupee drain
        sBreastplateRupeeTick++;
        if (sBreastplateRupeeTick >= BREASTPLATE_RUPEE_INTERVAL) {
            sBreastplateRupeeTick = 0;
            Rupees_ChangeBy(-1);
        }

        // Invincibility
        player->invincibilityTimer = -1;

        // Damage → rupee conversion
        if (player->cylinder.base.acFlags & AC_HIT) {
            s32 damage = player->actor.colChkInfo.damage;
            if (damage > 0) {
                s16 rupeeCost = (s16)damage;
                if (rupeeCost > gSaveContext.rupees) {
                    rupeeCost = (s16)gSaveContext.rupees;
                }
                Rupees_ChangeBy(-rupeeCost);

                Audio_PlaySoundGeneral(NA_SE_IT_SHIELD_BOUND, &player->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
        }
    } else {
        // === No rupees: heavy and slow ===
        sBreastplateRupeeTick = 0;

        // Slow movement (armor is heavy without magic power)
        player->linearVelocity *= BREASTPLATE_SLOW_MULT;
        player->actor.speedXZ *= BREASTPLATE_SLOW_MULT;
    }
}

// ---------------------------------------------------------------------------
// Tunic color tint: gold (has rupees) or dark (broke)
// Called from the tunic color system to override Link's tunic color
// ---------------------------------------------------------------------------
u8 Breastplate_IsActive(void) {
    return ExtEquip_IsEnabled() && gExtEquipState.currentExtTunic == 2;
}

// Returns 1 if player has rupees (gold mode), 0 if broke (dark mode)
u8 Breastplate_HasPower(void) {
    return gSaveContext.rupees > 0;
}

// ---------------------------------------------------------------------------
// Draw Iron Knuckle chest armor on Link's upper body
// Called from PostLimbDraw for PLAYER_LIMB_UPPER
// Uses inline DL from OOT decomp (no external object dependencies)
// Textures from soh.otr (always available)
// ---------------------------------------------------------------------------
#include "equipment/objects/breastplate_DL/model.inc.c"

// Helper: set color + alpha based on rupees
// Has rupees: golden, 20% alpha (51/255)
// No rupees:  golden, fully opaque (armor materializes when magic is spent)
#define BREASTPLATE_SET_MATERIAL()                                      \
    do {                                                                \
        if (gSaveContext.rupees > 0) {                                  \
            gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 225, 205, 115, 51);  \
            gDPSetEnvColor(POLY_XLU_DISP++, 25, 20, 0, 255);            \
        } else {                                                        \
            gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 225, 205, 115, 255); \
            gDPSetEnvColor(POLY_XLU_DISP++, 25, 20, 0, 255);            \
        }                                                               \
    } while (0)

// Base transform (user-tuned): Scale then translate to center on Link's torso
#define BP_SX 1.0f
#define BP_SY 0.7f
#define BP_SZ 1.2f
#define BP_TX -1000.0f
#define BP_TY 104.0f
#define BP_TZ 0.0f

static void Breastplate_DrawPiece(PlayState* play, Gfx* dl, f32 ikOffX, f32 ikOffY, f32 ikOffZ) {
    OPEN_DISPS(play->state.gfxCtx);

    Matrix_Push();
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    BREASTPLATE_SET_MATERIAL();

    // Apply base transform then IK skeleton offset
    Matrix_Scale(BP_SX, BP_SY, BP_SZ, MTXMODE_APPLY);
    Matrix_Translate(BP_TX + ikOffX, BP_TY + ikOffY, BP_TZ + ikOffZ, MTXMODE_APPLY);

    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, dl);
    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx);
}

static void Breastplate_Draw(PlayState* play) {
    // IK skeleton offsets from upper body root (Limb 1):
    // Chest plates:    (0, 0, 0)
    // R Pauldron:      (+1900, 0, -1184)
    // L Pauldron:      (+1900, 0, +1184)
    // Helmet marking:  (+2100, -200, 0)
    Breastplate_DrawPiece(play, gSpiritChestDL, 0.0f, 0.0f, 0.0f);
    Breastplate_DrawPiece(play, gSpiritPauldronRDL, 1900.0f, 0.0f, -1184.0f);
    Breastplate_DrawPiece(play, gSpiritPauldronLDL, 1900.0f, 0.0f, 1184.0f);
    Breastplate_DrawPiece(play, gSpiritHelmetMarkDL, 2100.0f, -200.0f, 0.0f);
}
