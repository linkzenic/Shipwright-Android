/**
 * equip_ikana.c - Shield of Ikana (Extended Shield Slot 3)
 *
 * Behavior: MM Mirror Shield model + rupee-cost damage immunity.
 * - Uses OOT's Mirror Shield model (EQUIP_VALUE_SHIELD_MIRROR)
 * - Makes Link immune to all damage
 * - Each 1 HP of damage received costs 1 rupee instead
 * - No rupees = Jynxed: inverted stick, swapped A/B, slower movement
 * - No passive rupee drain — only costs on damage
 *
 * Included by ext_equip_behavior.c (unity build).
 */

// No extra includes — unity-built from ext_equip_behavior.c
extern void Rupees_ChangeBy(s16 rupeeChange);

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define IKANA_RUPEE_INTERVAL 30 // Passive drain: 1 rupee every N frames
#define IKANA_JYNXED_SPEED_MULT 0.5f

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static s16 sIkanaRupeeTick = 0;

// ---------------------------------------------------------------------------
// Jynxed effect: invert controls when out of rupees
// ---------------------------------------------------------------------------
static void Ikana_ApplyJynxed(Player* p, PlayState* play) {
    Input* input = &play->state.input[0];

    // Invert stick X and Y
    input->rel.stick_x = -input->rel.stick_x;
    input->rel.stick_y = -input->rel.stick_y;
    input->cur.stick_x = -input->cur.stick_x;
    input->cur.stick_y = -input->cur.stick_y;

    // Swap A and B buttons (both press and cur)
    u16 pressA = input->press.button & BTN_A;
    u16 pressB = input->press.button & BTN_B;
    input->press.button &= ~(BTN_A | BTN_B);
    if (pressA)
        input->press.button |= BTN_B;
    if (pressB)
        input->press.button |= BTN_A;

    u16 curA = input->cur.button & BTN_A;
    u16 curB = input->cur.button & BTN_B;
    input->cur.button &= ~(BTN_A | BTN_B);
    if (curA)
        input->cur.button |= BTN_B;
    if (curB)
        input->cur.button |= BTN_A;

    // Slow movement
    p->linearVelocity *= IKANA_JYNXED_SPEED_MULT;
    p->actor.speedXZ *= IKANA_JYNXED_SPEED_MULT;
}

// ---------------------------------------------------------------------------
// Main Behavior
// ---------------------------------------------------------------------------
static void Ikana_Behavior(Player* player, PlayState* play) {
    // Skip during cutscenes, dying, etc.
    if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_LOADING |
                               PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_GETTING_ITEM)) {
        return;
    }

    // Passive rupee drain: 1 rupee per 30 frames
    sIkanaRupeeTick++;
    if (sIkanaRupeeTick >= IKANA_RUPEE_INTERVAL) {
        sIkanaRupeeTick = 0;
        if (gSaveContext.rupees > 0) {
            Rupees_ChangeBy(-1);
        }
    }

    // Check if broke → apply Jynxed effect
    if (gSaveContext.rupees <= 0) {
        Ikana_ApplyJynxed(player, play);
    }

    // Invincibility: -1 = permanent (like Nayru's Love)
    player->invincibilityTimer = -1;

    // Damage → rupee conversion: 1 HP damage = 1 rupee
    if (player->cylinder.base.acFlags & AC_HIT) {
        s32 damage = player->actor.colChkInfo.damage;
        if (damage > 0 && gSaveContext.rupees > 0) {
            s16 rupeeCost = (s16)damage;
            if (rupeeCost > gSaveContext.rupees) {
                rupeeCost = (s16)gSaveContext.rupees;
            }
            Rupees_ChangeBy(-rupeeCost);

            Audio_PlaySoundGeneral(NA_SE_IT_SHIELD_BOUND, &player->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
    }
}
