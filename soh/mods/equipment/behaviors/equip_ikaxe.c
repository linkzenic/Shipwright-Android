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

// FirstPerson aim helpers (from items/helpers/camera_helper.c, linked separately)
// Used by equip_ikaxe_throw.inc.c for C-Up aim mode

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define IKAXE_ANIM_SLOW 0.35f     // Slow windup start (heavy)
#define IKAXE_ANIM_FAST 1.50f     // Fast at impact
#define IKAXE_MAX_WALK_SPEED 6.0f // Slower walk
#define IKAXE_DOUBLE_DAMAGE 2     // Damage multiplier
#define IKAXE_THROW_HOLD 15       // Hold B frames to throw
#define IKAXE_THROW_RETURN 30     // Flight frames before return
#define IKAXE_THROW_PARAMS 99     // En_Boom params for axe variant

// ---------------------------------------------------------------------------
// Tomahawk throw state
// ---------------------------------------------------------------------------
static s16 sIKAxeBHoldFrames = 0;
static u8 sIKAxeThrown = 0;
static u8 sIKAxeAimActive = 0; // first-person aim is on

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
#define IKAXE_THROW_IDLE 0
#define IKAXE_THROW_CHARGING 1 // Holding B, aim pose
#define IKAXE_THROW_FLYING 2   // Axe in the air

static u8 sIKAxeThrowState = IKAXE_THROW_IDLE;

// Throw system in separate file for clarity
#include "equip_ikaxe_throw.inc.c"

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

    // Hammer on B — game handles equip/putaway naturally:
    // B press → equip hammer → swing → auto putaway → free anims
    // Only buttonItems[0] is needed: sItemActions[ITEM_HAMMER] → PLAYER_IA_HAMMER drives the swing.
    // Do NOT write inventory.items[SLOT_HAMMER]: that slot is what the pause menu reads to draw
    // the hammer icon, so writing it would falsely give the player a hammer they don't own.
    gSaveContext.equips.buttonItems[0] = ITEM_HAMMER;

    // DON'T force heldItemAction every frame — let the game manage it.
    // Hammer's natural cycle: press B → heldItemAction=HAMMER → swing → putaway → NONE

    u8 isHolding = (player->heldItemAction == PLAYER_IA_HAMMER);

    // Signal draw system: hide vanilla sword DL only when hammer is out
    gExtEquipBehavior.ikAxeDrawing = isHolding || (sIKAxeThrowState == IKAXE_THROW_CHARGING);

    // Double damage only when holding
    if (isHolding) {
        player->meleeWeaponQuads[0].info.toucher.damage = 4 * IKAXE_DOUBLE_DAMAGE;
        player->meleeWeaponQuads[1].info.toucher.damage = 4 * IKAXE_DOUBLE_DAMAGE;
    }

    // Tomahawk throw (R + B hold) — only when holding hammer
    IKAxe_UpdateThrow(player, play);

    // Walk cap + chunky anims only when holding and not throwing
    if (sIKAxeThrowState == IKAXE_THROW_IDLE && isHolding) {
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
    // Axe is flying — En_Boom draws it
    if (sIKAxeThrowState == IKAXE_THROW_FLYING) {
        return;
    }

    // Free mode (putaway) — no axe in hand
    Player* drawPlayer = GET_PLAYER(play);
    if (drawPlayer->heldItemAction != PLAYER_IA_HAMMER && sIKAxeThrowState != IKAXE_THROW_CHARGING) {
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

    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, gIKAxeInlineDL);

    CLOSE_DISPS(play->state.gfxCtx);
}
