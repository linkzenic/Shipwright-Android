/**
 * equip_ikaxe.c - Iron Knuckle Axe (Extended Sword Slot 3)
 *
 * Pure Hammer IA — chunky, heavy, double damage.
 * No spin attack, no charge. Just heavy hammer swings.
 *
 * - Forces PLAYER_IA_HAMMER
 * - Chunky anim speeds (0.25x startup, 1.25x impact)
 * - Double damage via meleeWeaponQuads
 * - 2x hitbox reach (in z_player_lib.c)
 * - Slower walk speed
 *
 * Included by ext_equip_behavior.c (unity build).
 */

// Inline IK Axe DL (extracted from decomp, segments resolved)
#include "equipment/objects/ikaxe_DL/model.inc.c"

#include "overlays/actors/ovl_En_Boom/z_en_boom.h"

// Camera helper for first-person aim (from items module, linked separately)
extern void FirstPerson_Init(Player* player, PlayState* play);
extern void FirstPerson_Exit(Player* player, PlayState* play);
extern s16 FirstPerson_GetAimYaw(Player* player);
extern s16 FirstPerson_GetAimPitch(Player* player);

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define IKAXE_ANIM_SLOW        0.35f  // Slow windup start (heavy)
#define IKAXE_ANIM_FAST        1.50f  // Fast at impact
#define IKAXE_MAX_WALK_SPEED   6.0f   // Slower walk
#define IKAXE_DOUBLE_DAMAGE    2      // Damage multiplier
#define IKAXE_THROW_HOLD       15     // Hold B frames to throw
#define IKAXE_THROW_RETURN     30     // Flight frames before return
#define IKAXE_THROW_PARAMS     99     // En_Boom params for axe variant

// ---------------------------------------------------------------------------
// Tomahawk throw state
// ---------------------------------------------------------------------------
static s16 sIKAxeBHoldFrames = 0;
static u8 sIKAxeThrown = 0;
static u8 sIKAxeAimActive = 0;  // first-person aim is on

// ---------------------------------------------------------------------------
// Animation Speed Override (chunky)
// ---------------------------------------------------------------------------
static void IKAxe_ModifyAnimSpeed(Player* player) {
    if (player->meleeWeaponState == 0)
        return;

    f32 totalFrames = player->skelAnime.endFrame;
    if (totalFrames <= 0.0f)
        return;

    f32 progress = player->skelAnime.curFrame / totalFrames;

    // Smooth ease-in curve: starts slow (heavy windup), smoothly accelerates to impact.
    // t^2 gives a natural "weight" feel — no abrupt speed change.
    f32 t = progress * progress; // quadratic ease-in
    player->skelAnime.playSpeed = IKAXE_ANIM_SLOW + (IKAXE_ANIM_FAST - IKAXE_ANIM_SLOW) * t;
}

// ---------------------------------------------------------------------------
// Tomahawk Throw States
// ---------------------------------------------------------------------------
#define IKAXE_THROW_IDLE    0
#define IKAXE_THROW_CHARGING 1  // Holding B, aim pose
#define IKAXE_THROW_FLYING  2   // Axe in the air

static u8 sIKAxeThrowState = IKAXE_THROW_IDLE;

// ---------------------------------------------------------------------------
// Tomahawk Throw (hold B → first-person aim → release → parabolic arc)
// ---------------------------------------------------------------------------
static void IKAxe_UpdateThrow(Player* player, PlayState* play) {
    u8 holdingB = CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_B);

    switch (sIKAxeThrowState) {
        case IKAXE_THROW_IDLE: {
            // Throw: R + B (shield + B) when drawn
            u8 holdingR = CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_R);
            u8 isDrawn = (player->heldItemAction >= PLAYER_IA_SWORD_MASTER &&
                          player->heldItemAction <= PLAYER_IA_HAMMER);

            if (holdingR && holdingB && isDrawn && player->meleeWeaponState == 0) {
                sIKAxeBHoldFrames++;
                if (sIKAxeBHoldFrames >= IKAXE_THROW_HOLD) {
                    sIKAxeThrowState = IKAXE_THROW_CHARGING;

                    // Enter first-person aim like Zora does — bypass vanilla camera
                    player->unk_6AD = 2;
                    player->stateFlags1 |= PLAYER_STATE1_FIRST_PERSON;
                    sIKAxeAimActive = 1;

                    player->linearVelocity = 0.0f;
                    player->actor.speedXZ = 0.0f;
                }
            } else if (!holdingB || !holdingR) {
                sIKAxeBHoldFrames = 0;
            }
            break;
        }

        case IKAXE_THROW_CHARGING:
            // Lock movement during aim
            player->linearVelocity = 0.0f;
            player->actor.speedXZ = 0.0f;

            if (!holdingB) {
                // Exit aim mode
                if (sIKAxeAimActive) {
                    player->unk_6AD = 0;
                    player->stateFlags1 &= ~PLAYER_STATE1_FIRST_PERSON;
                    sIKAxeAimActive = 0;
                }

                // Play throw animation
                LinkAnimation_PlayOnce(play, &player->skelAnime,
                    (LinkAnimationHeader*)&gPlayerAnim_link_boom_throwR);

                // Launch direction from aim
                s16 yaw = FirstPerson_GetAimYaw(player);
                s16 pitch = FirstPerson_GetAimPitch(player);

                // If Z-targeting, aim at target
                if (player->focusActor != NULL) {
                    yaw = Math_Vec3f_Yaw(&player->actor.world.pos, &player->focusActor->focus.pos);
                    pitch = Math_Vec3f_Pitch(&player->actor.world.pos, &player->focusActor->focus.pos);
                }

                f32 posX = Math_SinS(yaw) * 20.0f + player->actor.world.pos.x;
                f32 posZ = Math_CosS(yaw) * 20.0f + player->actor.world.pos.z;

                EnBoom* axe = (EnBoom*)Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOOM,
                    posX, player->actor.world.pos.y + 40.0f, posZ,
                    pitch, yaw, 0,
                    IKAXE_THROW_PARAMS);

                if (axe != NULL) {
                    axe->moveTo = NULL;  // No homing — straight arc
                    axe->returnTimer = IKAXE_THROW_RETURN;
                    player->stateFlags1 |= PLAYER_STATE1_BOOMERANG_THROWN;
                    player->boomerangActor = &axe->actor;
                    sIKAxeThrown = 1;
                    sIKAxeThrowState = IKAXE_THROW_FLYING;

                    Audio_PlaySoundGeneral(NA_SE_IT_SWORD_SWING_HARD, &player->actor.world.pos, 4,
                                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                           &gSfxDefaultReverb);
                } else {
                    sIKAxeThrowState = IKAXE_THROW_IDLE;
                }
                sIKAxeBHoldFrames = 0;
            }
            break;

        case IKAXE_THROW_FLYING:
            // Axe is in the air — no melee until it returns
            player->meleeWeaponState = 0;

            // Axe returned and was caught → back to idle
            if (!(player->stateFlags1 & PLAYER_STATE1_BOOMERANG_THROWN)) {
                sIKAxeThrown = 0;
                sIKAxeThrowState = IKAXE_THROW_IDLE;
                // Catch animation
                LinkAnimation_PlayOnce(play, &player->skelAnime,
                    (LinkAnimationHeader*)&gPlayerAnim_link_boom_catch);
                Audio_PlaySoundGeneral(NA_SE_IT_SWORD_PICKOUT, &player->actor.world.pos, 4,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultReverb);
            }
            break;
    }
}

// ---------------------------------------------------------------------------
// Per-frame Behavior
// ---------------------------------------------------------------------------
static void IKAxe_Behavior(Player* player, PlayState* play) {
    if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_LOADING |
                               PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_GETTING_ITEM)) {
        return;
    }

    // Save original state (only once)
    if (!gExtEquipBehavior.ikAxeActive) {
        gExtEquipBehavior.ikAxeSavedSwordEquip =
            (gSaveContext.equips.equipment >> gEquipShifts[EQUIP_TYPE_SWORD]) & 0xF;
        gExtEquipBehavior.ikAxeSavedButtonItem = gSaveContext.equips.buttonItems[0];
        gExtEquipBehavior.ikAxeActive = 1;
    }

    // B button shows as sword for sheathe/unsheathe, icon overridden by ExtInv_GetItemIcon
    gSaveContext.equips.buttonItems[0] = ITEM_SWORD_BGS;
    gSaveContext.swordHealth = 8;
    gSaveContext.inventory.items[SLOT_HAMMER] = ITEM_HAMMER;

    // Detect if weapon is drawn (sword IA = just unsheathed, or hammer IA = already forced)
    u8 isDrawn = (player->heldItemAction >= PLAYER_IA_SWORD_MASTER &&
                  player->heldItemAction <= PLAYER_IA_HAMMER);

    // Force Hammer IA when drawn — hammer attacks (slash/stab/spin all as hammer)
    if (isDrawn && sIKAxeThrowState != IKAXE_THROW_FLYING) {
        player->heldItemAction = PLAYER_IA_HAMMER;
    }

    // Double damage when drawn
    if (isDrawn) {
        player->meleeWeaponQuads[0].info.toucher.damage = 4 * IKAXE_DOUBLE_DAMAGE;
        player->meleeWeaponQuads[1].info.toucher.damage = 4 * IKAXE_DOUBLE_DAMAGE;
    }

    // Tomahawk throw (hold B → release) — only when drawn
    IKAxe_UpdateThrow(player, play);

    // Walk cap + chunky anims only when drawn and idle
    if (sIKAxeThrowState == IKAXE_THROW_IDLE && isDrawn) {
        if (player->meleeWeaponState == 0 && player->linearVelocity > IKAXE_MAX_WALK_SPEED) {
            player->linearVelocity = IKAXE_MAX_WALK_SPEED;
        }
        IKAxe_ModifyAnimSpeed(player);
    }
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------
static void IKAxe_Cleanup(void) {
    if (!gExtEquipBehavior.ikAxeActive)
        return;

    // Exit aim mode if we were charging when unequipped
    if (sIKAxeAimActive) {
        // Can't call FirstPerson_Exit without player/play, just clear flags
        sIKAxeAimActive = 0;
    }
    sIKAxeThrowState = IKAXE_THROW_IDLE;
    sIKAxeBHoldFrames = 0;
    sIKAxeThrown = 0;

    Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, gExtEquipBehavior.ikAxeSavedSwordEquip);
    gSaveContext.equips.buttonItems[0] = gExtEquipBehavior.ikAxeSavedButtonItem;
    gExtEquipBehavior.ikAxeActive = 0;
}

// ---------------------------------------------------------------------------
// Draw — IK Axe DL on XLU
// ---------------------------------------------------------------------------
static void IKAxe_DrawAxe(PlayState* play) {
    // Axe is flying — En_Boom draws it, don't draw on player
    if (sIKAxeThrowState == IKAXE_THROW_FLYING) {
        return;
    }

    // Sheathed — axe drawn on back via PLAYER_LIMB_SHEATH, not in hand
    Player* drawPlayer = GET_PLAYER(play);
    if (drawPlayer->heldItemAction == PLAYER_IA_NONE) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    Matrix_Translate(-100.0f, -350.0f, 0.0f, MTXMODE_APPLY);
    Matrix_RotateZYX(0x4000, 0, 0, MTXMODE_APPLY);

    if (sIKAxeThrowState == IKAXE_THROW_CHARGING) {
        // Small axe in hand during aim
        Matrix_Scale(0.07f, 0.07f, 0.07f, MTXMODE_APPLY);
    } else {
        Matrix_Scale(0.15f, 0.15f, 0.15f, MTXMODE_APPLY);
    }

    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, gIKAxeInlineDL);

    CLOSE_DISPS(play->state.gfxCtx);
}
