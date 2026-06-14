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
    u8 slot = SLOT_NONE;
    bool isBombArrow = false;
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

    return (sBombArrowButtons & (1 << buttonIndex)) != 0 &&
           (gSaveContext.equips.buttonItems[buttonIndex] == ITEM_BOMB ||
            IsBowButtonItem(gSaveContext.equips.buttonItems[buttonIndex]));
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

extern "C" u8 BombArrows_IsButtonBombArrow(s16 buttonIndex) {
    return CVAR_BOMB_ARROWS_VALUE && IsBombArrowButton(buttonIndex);
}

extern "C" s16 BombArrows_GetEffectiveAmmo() {
    s16 arrowAmmo = AMMO(ITEM_BOW);
    s16 bombAmmo = AMMO(ITEM_BOMB);
    return arrowAmmo < bombAmmo ? arrowAmmo : bombAmmo;
}

extern "C" s16 BombArrows_GetEffectiveMaxAmmo() {
    s16 arrowCapacity = CUR_CAPACITY(UPG_QUIVER);
    s16 bombCapacity = CUR_CAPACITY(UPG_BOMB_BAG);
    return arrowCapacity < bombCapacity ? arrowCapacity : bombCapacity;
}

extern "C" u8 BombArrows_GetEffectiveButtonItem(s16 buttonIndex, u8 item) {
    if (CVAR_BOMB_ARROWS_VALUE && item == ITEM_BOMB && IsBombArrowButton(buttonIndex)) {
        return ITEM_BOW;
    }

    return item;
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
    if (!CVAR_BOMB_ARROWS_VALUE || play == nullptr || item == nullptr || slot == nullptr || !CanEquipBombArrow()) {
        return;
    }

    PauseContext* pauseCtx = &play->pauseCtx;
    s32 targetButtonIndex = pauseCtx->equipTargetCBtn + 1;
    if (targetButtonIndex < 1 || targetButtonIndex > 7) {
        return;
    }

    if (*item == ITEM_BOW) {
        sPendingEquip = { true, targetButtonIndex, ITEM_BOW, SLOT_BOW, false };
        return;
    }

    if (!ShouldEquipBombArrowFromBombSlot(*item)) {
        return;
    }

    sPendingEquip = { true, targetButtonIndex, ITEM_BOMB, SLOT_BOMB, true };
}

extern "C" u8 BombArrows_HandleEquipCommit(PlayState* play, u16 targetButtonIndex, u16* item, u16* slot) {
    if (!CVAR_BOMB_ARROWS_VALUE || play == nullptr || item == nullptr || slot == nullptr || targetButtonIndex < 1 ||
        targetButtonIndex > 7) {
        return false;
    }

    if (sPendingEquip.active && sPendingEquip.buttonIndex == targetButtonIndex) {
        bool isBombArrow = sPendingEquip.isBombArrow;
        *item = sPendingEquip.item;
        *slot = sPendingEquip.slot;
        SetBombArrowButton(targetButtonIndex, isBombArrow);
        sPendingEquip = {};
        return isBombArrow;
    }

    if (IsBowButtonItem(*item) || *item == ITEM_BOMB) {
        SetBombArrowButton(targetButtonIndex, false);
        return false;
    }

    if (!CanEquipBombArrow() || !ShouldEquipBombArrowFromBombSlot(*item)) {
        return false;
    }

    *item = ITEM_BOMB;
    SetBombArrowButton(targetButtonIndex, true);
    *slot = SLOT_BOMB;
    return true;
}

static void CleanupBombArrowButtons() {
    if (sBombArrowShotWindow > 0) {
        sBombArrowShotWindow--;
    }

    for (s32 buttonIndex = 1; buttonIndex <= 7; buttonIndex++) {
        if (gSaveContext.equips.buttonItems[buttonIndex] != ITEM_BOMB &&
            !IsBowButtonItem(gSaveContext.equips.buttonItems[buttonIndex])) {
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
    Player* player = GET_PLAYER(gPlayState);

    if (player == nullptr || !CanUseBombArrow(false) || !IsBowArrow(arrow)) {
        return;
    }

    if (!IsActiveBombArrowButton() && sBombArrowShotWindow <= 0) {
        return;
    }

    ObjectExtension::GetInstance().Set(&arrow->actor, BombArrowData{});
    sBombArrowShotWindow = 8;
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
        (void)va_arg(args, int32_t);

        if (gPlayState == nullptr) {
            return;
        }

        s32 buttonIndex = GetPressedEquipButtonIndex(gPlayState);
        if (IsBombArrowButton(buttonIndex)) {
            sBombArrowShotWindow = 8;
        }
    });

    COND_ID_HOOK(OnActorInit, ACTOR_EN_ARROW, CVAR_BOMB_ARROWS_VALUE, OnBombArrowInit);
    COND_ID_HOOK(OnActorUpdate, ACTOR_EN_ARROW, CVAR_BOMB_ARROWS_VALUE, OnBombArrowUpdate);
    COND_HOOK(OnGameFrameUpdate, CVAR_BOMB_ARROWS_VALUE, [] {
        CleanupBombArrowButtons();
    });
    COND_HOOK(OnKaleidoUpdate, CVAR_BOMB_ARROWS_VALUE, [] {
        CleanupBombArrowButtons();
    });
}

static RegisterShipInitFunc initFunc(RegisterBombArrows, { CVAR_BOMB_ARROWS_NAME });
