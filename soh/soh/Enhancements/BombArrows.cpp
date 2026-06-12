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
static u8 sBombArrowButtons = 0;
static s16 sBombArrowShotWindow = 0;
static bool sBombSlotIsBombArrowMode = false;

struct PendingBombArrowEquip {
    bool active = false;
    s32 buttonIndex = -1;
    u8 item = ITEM_NONE;
};

static PendingBombArrowEquip sPendingEquip = {};

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

    return (sBombArrowButtons & (1 << buttonIndex)) != 0 && IsBowButtonItem(gSaveContext.equips.buttonItems[buttonIndex]);
}

static void SetBombArrowButton(s32 buttonIndex, bool enabled) {
    if (buttonIndex < 1 || buttonIndex > 7) {
        return;
    }

    if (enabled) {
        sBombArrowButtons |= (1 << buttonIndex);
    } else {
        sBombArrowButtons &= ~(1 << buttonIndex);
    }
}

static bool IsActiveBombArrowButton() {
    Player* player = GET_PLAYER(gPlayState);
    return player != nullptr && IsBombArrowButton(player->heldItemButton);
}

static bool IsBombArrowShotActive() {
    return IsActiveBombArrowButton() || sBombArrowShotWindow > 0;
}

static bool CanEquipBombArrow() {
    if (gPlayState == nullptr || !LINK_IS_ADULT || gSaveContext.minigameState ||
        gPlayState->sceneNum == SCENE_SHOOTING_GALLERY) {
        return false;
    }

    return INV_CONTENT(ITEM_BOW) == ITEM_BOW && INV_CONTENT(ITEM_BOMB) == ITEM_BOMB;
}

static bool CanUseBombArrow(bool requireActiveButton = true) {
    if (!CanEquipBombArrow()) {
        return false;
    }

    if (requireActiveButton && !IsBombArrowShotActive()) {
        return false;
    }

    return AMMO(ITEM_BOW) > 0 && AMMO(ITEM_BOMB) > 0;
}

static bool ShouldEquipBombArrowFromBombSlot(u16 item) {
    return item == ITEM_BOMB && sBombSlotIsBombArrowMode;
}

extern "C" void BombArrows_HandlePauseCursor(PlayState* play) {
    if (!CVAR_BOMB_ARROWS_VALUE || play == nullptr || !CanEquipBombArrow()) {
        sBombSlotIsBombArrowMode = false;
        return;
    }

    PauseContext* pauseCtx = &play->pauseCtx;
    if (pauseCtx->state != 6 || pauseCtx->unk_1E4 != 0 || pauseCtx->cursorSpecialPos != 0 ||
        pauseCtx->cursorSlot[PAUSE_ITEM] != SLOT_BOMB) {
        return;
    }

    Input* input = &play->state.input[0];
    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        sBombSlotIsBombArrowMode = !sBombSlotIsBombArrowMode;
        Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
}

extern "C" u8 BombArrows_IsBombSlotMode() {
    return CVAR_BOMB_ARROWS_VALUE && sBombSlotIsBombArrowMode && CanEquipBombArrow();
}

extern "C" u8 BombArrows_CanCycleBombSlot() {
    return CVAR_BOMB_ARROWS_VALUE && CanEquipBombArrow();
}

extern "C" void BombArrows_HandleSetupItemEquip(PlayState* play, u16* item, u16* slot) {
    if (!CVAR_BOMB_ARROWS_VALUE || play == nullptr || item == nullptr || slot == nullptr || *item != ITEM_BOMB ||
        !CanEquipBombArrow()) {
        return;
    }

    PauseContext* pauseCtx = &play->pauseCtx;
    s32 targetButtonIndex = pauseCtx->equipTargetCBtn + 1;
    if (targetButtonIndex < 1 || targetButtonIndex > 7) {
        return;
    }

    u8 equippedItem = gSaveContext.equips.buttonItems[targetButtonIndex];
    bool equipFromBombSlot = ShouldEquipBombArrowFromBombSlot(*item);
    if (!equipFromBombSlot && !IsBowButtonItem(equippedItem)) {
        return;
    }

    if (!IsBowButtonItem(equippedItem)) {
        equippedItem = ITEM_BOW;
    }

    sPendingEquip = { true, targetButtonIndex, equippedItem };
    SetBombArrowButton(targetButtonIndex, true);
    *item = equippedItem;
    *slot = SLOT_BOW;
}

extern "C" u8 BombArrows_HandleEquipCommit(PlayState* play, u16 targetButtonIndex, u16* item, u16* slot) {
    if (!CVAR_BOMB_ARROWS_VALUE || play == nullptr || item == nullptr || slot == nullptr || *item != ITEM_BOMB ||
        targetButtonIndex < 1 || targetButtonIndex > 7 || !CanEquipBombArrow()) {
        return false;
    }

    u8 equippedItem = gSaveContext.equips.buttonItems[targetButtonIndex];
    bool equipFromBombSlot = ShouldEquipBombArrowFromBombSlot(*item);
    if (!equipFromBombSlot && !IsBowButtonItem(equippedItem)) {
        return false;
    }

    if (!IsBowButtonItem(equippedItem)) {
        equippedItem = ITEM_BOW;
    }

    SetBombArrowButton(targetButtonIndex, true);
    *item = equippedItem;
    *slot = SLOT_BOW;
    return true;
}

static void ApplyPendingBombArrowEquip() {
    if (gPlayState == nullptr || !sPendingEquip.active || gPlayState->pauseCtx.unk_1E4 != 0) {
        return;
    }

    gSaveContext.equips.buttonItems[sPendingEquip.buttonIndex] = sPendingEquip.item;
    gSaveContext.equips.cButtonSlots[sPendingEquip.buttonIndex - 1] = SLOT_BOW;
    SetBombArrowButton(sPendingEquip.buttonIndex, true);
    Interface_LoadItemIcon1(gPlayState, sPendingEquip.buttonIndex);
    sPendingEquip = {};
}

static void CleanupBombArrowButtons() {
    if (sBombArrowShotWindow > 0) {
        sBombArrowShotWindow--;
    }

    for (s32 buttonIndex = 1; buttonIndex <= 7; buttonIndex++) {
        if (!IsBowButtonItem(gSaveContext.equips.buttonItems[buttonIndex])) {
            SetBombArrowButton(buttonIndex, false);
        }
    }
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
    COND_VB_SHOULD(VB_CHANGE_HELD_ITEM_AND_USE_ITEM, CVAR_BOMB_ARROWS_VALUE, {
        int32_t item = va_arg(args, int32_t);

        if (!IsBowButtonItem(item) || gPlayState == nullptr) {
            return;
        }

        s32 buttonIndex = GetPressedEquipButtonIndex(gPlayState);
        if (IsBombArrowButton(buttonIndex)) {
            sBombArrowShotWindow = 5;
        }
    });

    COND_ID_HOOK(OnActorInit, ACTOR_EN_ARROW, CVAR_BOMB_ARROWS_VALUE, OnBombArrowInit);
    COND_ID_HOOK(OnActorUpdate, ACTOR_EN_ARROW, CVAR_BOMB_ARROWS_VALUE, OnBombArrowUpdate);
    COND_HOOK(OnGameFrameUpdate, CVAR_BOMB_ARROWS_VALUE, [] {
        ApplyPendingBombArrowEquip();
        CleanupBombArrowButtons();
    });
    COND_HOOK(OnKaleidoUpdate, CVAR_BOMB_ARROWS_VALUE, [] {
        ApplyPendingBombArrowEquip();
        CleanupBombArrowButtons();
    });
}

static RegisterShipInitFunc initFunc(RegisterBombArrows, { CVAR_BOMB_ARROWS_NAME });
