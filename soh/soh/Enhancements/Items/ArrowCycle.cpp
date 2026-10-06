#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "overlays/actors/ovl_En_Arrow/z_en_arrow.h"
s32 func_808351D4(Player* thisx, PlayState* play); // Arrow nocked
void EnArrow_Init(Actor* thisx, PlayState* play);
extern PlayState* gPlayState;
u8 BombArrows_CanCycleArrow(void);
u8 BombArrows_IsButtonBombArrow(s16 buttonIndex);
void BombArrows_SetArrowCycleButton(PlayState* play, s16 buttonIndex, u8 enabled);
void BombArrows_ClearOtherBowFamilyButtons(PlayState* play, s16 targetButtonIndex);
void BombArrows_UpdateArrowCycleArrow(Actor* arrowActor, u8 enabled);
}

#define CVAR_ARROW_CYCLE_NAME CVAR_ENHANCEMENT("BowArrowCycle")
#define CVAR_ARROW_CYCLE_DEFAULT 0
#define CVAR_ARROW_CYCLE_VALUE CVarGetInteger(CVAR_ARROW_CYCLE_NAME, CVAR_ARROW_CYCLE_DEFAULT)

static const s16 sMagicArrowCosts[] = { 4, 4, 8 };

static const s16 PLAYER_IA_BOW_BOMB_ARROW = -1;

static const s16 sArrowCycleOrder[] = {
    PLAYER_IA_BOW,
    PLAYER_IA_BOW_BOMB_ARROW,
    PLAYER_IA_BOW_FIRE,
    PLAYER_IA_BOW_ICE,
    PLAYER_IA_BOW_LIGHT,
};

static bool IsHoldingBow(Player* player) {
    return player->heldItemAction >= PLAYER_IA_BOW && player->heldItemAction <= PLAYER_IA_BOW_LIGHT;
}

static bool IsHoldingMagicBow(Player* player) {
    return player->heldItemAction >= PLAYER_IA_BOW_FIRE && player->heldItemAction <= PLAYER_IA_BOW_LIGHT;
}

static bool IsAimingBow(Player* player) {
    return IsHoldingBow(player) && ((player->unk_6AD == 2) || (player->upperActionFunc == func_808351D4));
}

static bool HasArrowType(s16 itemAction) {
    switch (itemAction) {
        case PLAYER_IA_BOW_BOMB_ARROW:
            return BombArrows_CanCycleArrow();
        case PLAYER_IA_BOW:
            return true;
        case PLAYER_IA_BOW_FIRE:
            return INV_CONTENT(ITEM_ARROW_FIRE) == ITEM_ARROW_FIRE && gSaveContext.magic >= sMagicArrowCosts[0];
        case PLAYER_IA_BOW_ICE:
            return INV_CONTENT(ITEM_ARROW_ICE) == ITEM_ARROW_ICE && gSaveContext.magic >= sMagicArrowCosts[1];
        case PLAYER_IA_BOW_LIGHT:
            return INV_CONTENT(ITEM_ARROW_LIGHT) == ITEM_ARROW_LIGHT && gSaveContext.magic >= sMagicArrowCosts[2];
        default:
            return false;
    }
}

static s32 GetBowItemForArrow(s16 itemAction) {
    switch (itemAction) {
        case PLAYER_IA_BOW_FIRE:
            return ITEM_BOW_ARROW_FIRE;
        case PLAYER_IA_BOW_ICE:
            return ITEM_BOW_ARROW_ICE;
        case PLAYER_IA_BOW_LIGHT:
            return ITEM_BOW_ARROW_LIGHT;
        default:
            return ITEM_BOW;
    }
}

static ArrowType GetArrowTypeForArrow(s16 itemAction) {
    switch (itemAction) {
        case PLAYER_IA_BOW_FIRE:
            return ARROW_FIRE;
        case PLAYER_IA_BOW_ICE:
            return ARROW_ICE;
        case PLAYER_IA_BOW_LIGHT:
            return ARROW_LIGHT;
        default:
            return ARROW_NORMAL;
    }
}

static bool CanCycleArrows() {
    Player* player = GET_PLAYER(gPlayState);

    return (LINK_IS_ADULT || CVarGetInteger(CVAR_CHEAT("TimelessEquipment"), 0)) && !gSaveContext.minigameState &&
           gPlayState->sceneNum != SCENE_SHOOTING_GALLERY && !(player->stateFlags1 & PLAYER_STATE1_ON_HORSE) &&
           player->rideActor == NULL && INV_CONTENT(SLOT_BOW) == ITEM_BOW &&
           (INV_CONTENT(ITEM_ARROW_FIRE) == ITEM_ARROW_FIRE || INV_CONTENT(ITEM_ARROW_ICE) == ITEM_ARROW_ICE ||
            INV_CONTENT(ITEM_ARROW_LIGHT) == ITEM_ARROW_LIGHT || BombArrows_CanCycleArrow());
}

static s16 GetCurrentArrowType(Player* player) {
    if (BombArrows_IsButtonBombArrow(player->heldItemButton)) {
        return PLAYER_IA_BOW_BOMB_ARROW;
    }
    return player->heldItemAction;
}

static s16 GetNextArrowType(s16 currentArrowType) {
    int currentIndex = 0;
    for (int i = 0; i < (int)ARRAY_COUNT(sArrowCycleOrder); i++) {
        if (sArrowCycleOrder[i] == currentArrowType) {
            currentIndex = i;
            break;
        }
    }

    for (int offset = 1; offset <= (int)ARRAY_COUNT(sArrowCycleOrder); offset++) {
        int nextIndex = (currentIndex + offset) % ARRAY_COUNT(sArrowCycleOrder);
        if (HasArrowType(sArrowCycleOrder[nextIndex])) {
            return sArrowCycleOrder[nextIndex];
        }
    }

    return PLAYER_IA_BOW;
}

static void UpdateButtonAlpha(s16 flashAlpha, bool isButtonBow, u16* buttonAlpha) {
    if (isButtonBow) {
        *buttonAlpha = flashAlpha;
    }
}

static void UpdateEquippedBow(PlayState* play, s16 arrowType, s16 targetButtonIndex) {
    if (targetButtonIndex < 1 || targetButtonIndex > 7) {
        return;
    }

    bool isBombArrow = arrowType == PLAYER_IA_BOW_BOMB_ARROW;
    s32 bowItem = GetBowItemForArrow(arrowType);

    BombArrows_ClearOtherBowFamilyButtons(play, targetButtonIndex);
    BombArrows_SetArrowCycleButton(play, targetButtonIndex, isBombArrow);
    if (isBombArrow) {
        return;
    }

    gSaveContext.equips.buttonItems[targetButtonIndex] = bowItem;
    if (targetButtonIndex <= 3) {
        gSaveContext.equips.cButtonSlots[targetButtonIndex - 1] = SLOT_BOW;
        Interface_LoadItemIcon1(play, targetButtonIndex);
    }

    gSaveContext.buttonStatus[targetButtonIndex] = BTN_ENABLED;
}

bool ArrowCycleMain() {
    if (gPlayState == nullptr || !CanCycleArrows()) {
        return false;
    }

    Player* player = GET_PLAYER(gPlayState);
    if (player->heldActor != NULL && player->heldActor->id == ACTOR_EN_ARROW) {
        if (IsHoldingMagicBow(player) && gSaveContext.magicState != MAGIC_STATE_IDLE && player->heldActor == NULL) {
            Audio_PlaySfxGeneral(NA_SE_SY_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            return true;
        }

        // reset magic state to IDLE before cycling to prevent error sound
        gSaveContext.magicState = MAGIC_STATE_IDLE;

        s16 nextArrow = GetNextArrowType(GetCurrentArrowType(player));
        player->heldItemAction = nextArrow == PLAYER_IA_BOW_BOMB_ARROW ? PLAYER_IA_BOW : nextArrow;
        player->itemAction = player->heldItemAction;
        Actor* arrow = player->heldActor;

        if (arrow->child != NULL) {
            Actor_Kill(arrow->child);
            arrow->child = NULL;
        }
        arrow->params = GetArrowTypeForArrow(nextArrow);
        UpdateEquippedBow(gPlayState, nextArrow, player->heldItemButton);
        EnArrow_Init(arrow, gPlayState);
        BombArrows_UpdateArrowCycleArrow(arrow, nextArrow == PLAYER_IA_BOW_BOMB_ARROW);
        return true;
    }
    return false;
}

void RegisterArrowCycle() {
    // suppress shield input when R is held while aiming to allow arrow cycling
    COND_VB_SHOULD(VB_EXECUTE_PLAYER_ACTION_FUNC, CVAR_ARROW_CYCLE_VALUE, {
        Player* player = (Player*)va_arg(args, void*);
        Input* input = (Input*)va_arg(args, void*);
        if (IsAimingBow(player) && CHECK_BTN_ANY(input->press.button, BTN_R)) {
            if (ArrowCycleMain()) {
                input->cur.button &= ~BTN_R;
                input->press.button &= ~BTN_R;
            }
        }
    });

    // don't consume magic on draw, but check if we have enough to fire
    COND_VB_SHOULD(VB_PLAYER_ARROW_MAGIC_CONSUMPTION, CVAR_ARROW_CYCLE_VALUE, {
        Player* player = va_arg(args, Player*);
        int32_t magicArrowType = va_arg(args, int32_t);
        int32_t* arrowType = va_arg(args, int32_t*);

        if (gSaveContext.magic < sMagicArrowCosts[magicArrowType]) {
            *arrowType = ARROW_NORMAL;
        } else {
            *should = false;
        }
    });

    COND_VB_SHOULD(VB_EN_ARROW_MAGIC_CONSUMPTION, CVAR_ARROW_CYCLE_VALUE, {
        EnArrow* arrow = va_arg(args, EnArrow*);

        if (arrow->actor.params < ARROW_FIRE || arrow->actor.params > ARROW_LIGHT) {
            return;
        }

        int32_t magicArrowType = arrow->actor.params - ARROW_FIRE;
        Magic_RequestChange(gPlayState, sMagicArrowCosts[magicArrowType], MAGIC_CONSUME_NOW);
    });
}

static RegisterShipInitFunc initFunc(RegisterArrowCycle, { CVAR_ARROW_CYCLE_NAME });
