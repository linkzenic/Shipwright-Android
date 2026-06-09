#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ObjectExtension/ObjectExtension.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "overlays/actors/ovl_En_Arrow/z_en_arrow.h"
#include "overlays/actors/ovl_En_Bom/z_en_bom.h"

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

static bool CanUseBombArrow() {
    if (gPlayState == nullptr || !LINK_IS_ADULT || gSaveContext.minigameState ||
        gPlayState->sceneNum == SCENE_SHOOTING_GALLERY) {
        return false;
    }

    return INV_CONTENT(SLOT_BOW) == ITEM_BOW && INV_CONTENT(ITEM_BOMB) == ITEM_BOMB && AMMO(ITEM_BOW) > 0 &&
           AMMO(ITEM_BOMB) > 0;
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
        if (!CanUseBombArrow()) {
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
    COND_ID_HOOK(OnActorInit, ACTOR_EN_ARROW, CVAR_BOMB_ARROWS_VALUE, OnBombArrowInit);
    COND_ID_HOOK(OnActorUpdate, ACTOR_EN_ARROW, CVAR_BOMB_ARROWS_VALUE, OnBombArrowUpdate);
}

static RegisterShipInitFunc initFunc(RegisterBombArrows, { CVAR_BOMB_ARROWS_NAME });
