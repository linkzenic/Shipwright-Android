#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ObjectExtension/ObjectExtension.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "overlays/actors/ovl_En_Arrow/z_en_arrow.h"
#include "overlays/actors/ovl_En_Bom/z_en_bom.h"
#include "overlays/misc/ovl_kaleido_scope/z_kaleido_scope.h"

extern PlayState* gPlayState;
}

#define CVAR_BOMB_ARROWS_NAME CVAR_ENHANCEMENT("BombArrows")
#define CVAR_BOMB_ARROWS_DEFAULT 0
#define CVAR_BOMB_ARROWS_VALUE CVarGetInteger(CVAR_BOMB_ARROWS_NAME, CVAR_BOMB_ARROWS_DEFAULT)

struct BombArrowData {
    bool consumedBomb = false;
    s16 fuse = 70;
};

static ObjectExtension::Register<BombArrowData> BombArrowDataRegister;

static bool IsBowArrow(EnArrow* arrow) {
    return arrow->actor.params >= ARROW_NORMAL_SILENT && arrow->actor.params <= ARROW_LIGHT;
}

static bool IsBowButtonItem(u8 item) {
    return item == ITEM_BOW || (item >= ITEM_BOW_ARROW_FIRE && item <= ITEM_BOW_ARROW_LIGHT);
}

static s32 GetPressedEquipButtonIndex(PlayState* play) {
    Input* input = &play->state.input[0];

    if (CHECK_BTN_ALL(input->press.button, BTN_CLEFT)) {
        return 1;
    }
    if (CHECK_BTN_ALL(input->press.button, BTN_CDOWN)) {
        return 2;
    }
    if (CHECK_BTN_ALL(input->press.button, BTN_CRIGHT)) {
        return 3;
    }
    if (CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0)) {
        if (CHECK_BTN_ALL(input->press.button, BTN_DUP)) {
            return 4;
        }
        if (CHECK_BTN_ALL(input->press.button, BTN_DDOWN)) {
            return 5;
        }
        if (CHECK_BTN_ALL(input->press.button, BTN_DLEFT)) {
            return 6;
        }
        if (CHECK_BTN_ALL(input->press.button, BTN_DRIGHT)) {
            return 7;
        }
    }

    return -1;
}

static bool IsBombArrowButton(s32 buttonIndex) {
    if (buttonIndex < 1 || buttonIndex > 7) {
        return false;
    }

    return IsBowButtonItem(gSaveContext.equips.buttonItems[buttonIndex]) &&
           gSaveContext.equips.cButtonSlots[buttonIndex - 1] == SLOT_BOMB;
}

static bool IsActiveBombArrowButton() {
    Player* player = GET_PLAYER(gPlayState);
    return player != nullptr && IsBombArrowButton(player->heldItemButton);
}

static bool CanUseBombArrow(bool requireActiveButton = true) {
    if (gPlayState == nullptr || !LINK_IS_ADULT || gSaveContext.minigameState ||
        gPlayState->sceneNum == SCENE_SHOOTING_GALLERY) {
        return false;
    }

    if (requireActiveButton && !IsActiveBombArrowButton()) {
        return false;
    }

    return INV_CONTENT(SLOT_BOW) == ITEM_BOW && INV_CONTENT(ITEM_BOMB) == ITEM_BOMB && AMMO(ITEM_BOW) > 0 &&
           AMMO(ITEM_BOMB) > 0;
}

static void EquipBombArrow(PlayState* play, u16 cursorSlot, u8 equippedBowItem) {
    PauseContext* pauseCtx = &play->pauseCtx;
    s16 animX = pauseCtx->itemVtx[cursorSlot * 4].v.ob[0] * 10;
    s16 animY = pauseCtx->itemVtx[cursorSlot * 4].v.ob[1] * 10;

    KaleidoScope_SetupItemEquip(play, equippedBowItem, SLOT_BOMB, animX, animY);
    Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

static void SpawnBombArrowExplosion(EnArrow* arrow) {
    EnBom* bomb =
        (EnBom*)Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_BOM, arrow->actor.world.pos.x,
                            arrow->actor.world.pos.y, arrow->actor.world.pos.z, 0, 0, 0, BOMB_BODY);

    if (bomb != nullptr) {
        bomb->timer = 0;
    }

    Actor_Kill(&arrow->actor);
}

static void OnBombArrowInit(void* actorRef) {
    EnArrow* arrow = (EnArrow*)actorRef;

    if (!CanUseBombArrow() || !IsBowArrow(arrow) || arrow->actor.parent != &GET_PLAYER(gPlayState)->actor) {
        return;
    }

    ObjectExtension::GetInstance().Set(&arrow->actor, BombArrowData{});
}

static void OnBombArrowUpdate(void* actorRef) {
    EnArrow* arrow = (EnArrow*)actorRef;
    auto bombArrowData = ObjectExtension::GetInstance().Get<BombArrowData>(&arrow->actor);

    if (bombArrowData == nullptr || arrow->actor.parent != nullptr) {
        return;
    }

    if (!bombArrowData->consumedBomb) {
        if (!CanUseBombArrow(false)) {
            ObjectExtension::GetInstance().Remove<BombArrowData>(&arrow->actor);
            return;
        }

        Inventory_ChangeAmmo(ITEM_BOMB, -1);
        bombArrowData->consumedBomb = true;
    }

    if (bombArrowData->fuse > 0) {
        bombArrowData->fuse--;
    }

    if (bombArrowData->fuse == 0 || arrow->touchedPoly || arrow->hitActor != nullptr || arrow->hitFlags != 0) {
        SpawnBombArrowExplosion(arrow);
    }
}

void RegisterBombArrows() {
    COND_VB_SHOULD(VB_EQUIP_ITEM_TO_C_BUTTON, CVAR_BOMB_ARROWS_VALUE, {
        PlayState* play = va_arg(args, PlayState*);
        u16 cursorSlot = va_arg(args, int);
        u16 cursorItem = va_arg(args, int);
        s32 targetButtonIndex = GetPressedEquipButtonIndex(play);

        if (targetButtonIndex == -1 || cursorItem != ITEM_BOMB || !CanUseBombArrow(false)) {
            return;
        }

        u8 equippedItem = gSaveContext.equips.buttonItems[targetButtonIndex];
        if (!IsBowButtonItem(equippedItem)) {
            return;
        }

        EquipBombArrow(play, cursorSlot, equippedItem);
        *should = false;
    });

    COND_ID_HOOK(OnActorInit, ACTOR_EN_ARROW, CVAR_BOMB_ARROWS_VALUE, OnBombArrowInit);
    COND_ID_HOOK(OnActorUpdate, ACTOR_EN_ARROW, CVAR_BOMB_ARROWS_VALUE, OnBombArrowUpdate);
}

static RegisterShipInitFunc initFunc(RegisterBombArrows, { CVAR_BOMB_ARROWS_NAME });
