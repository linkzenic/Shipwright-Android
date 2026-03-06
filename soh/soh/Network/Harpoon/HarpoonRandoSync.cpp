#include "Harpoon.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/Enhancements/randomizer/randomizer_entrance.h"
#include "soh/Network/Anchor/JsonConversions.hpp"
#include "soh/OTRGlobals.h"
#include "soh/Notification/Notification.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "soh/Enhancements/randomizer/ShuffleTradeItems.h"
extern PlayState* gPlayState;
}

// ============================================================================
// SET_FLAG (ported from Anchor)
// ============================================================================

void Harpoon::SendPacket_SetFlag(s16 sceneNum, s16 flagType, s16 flag) {
    if (!IsSaveLoaded() || !syncItems) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = HPN_SET_FLAG;
    payload["sceneNum"] = sceneNum;
    payload["flagType"] = flagType;
    payload["flag"] = flag;
    SendJsonToRemote(payload);
}

void Harpoon::HandlePacket_SetFlag(nlohmann::json payload) {
    if (!IsSaveLoaded() || !syncItems) {
        return;
    }

    s16 sceneNum = payload["sceneNum"].get<s16>();
    s16 flagType = payload["flagType"].get<s16>();
    s16 flag = payload["flag"].get<s16>();

    if (sceneNum == SCENE_ID_MAX) {
        auto effect = new GameInteractionEffect::SetFlag();
        effect->parameters[0] = flagType;
        effect->parameters[1] = flag;
        effect->Apply();

        if (flagType == FLAG_EVENT_CHECK_INF && flag == EVENTCHKINF_KING_ZORA_MOVED &&
            Inventory_HasSpecificBottle(ITEM_LETTER_RUTO)) {
            Inventory_ReplaceItem(gPlayState, ITEM_LETTER_RUTO, ITEM_BOTTLE);
        }
    } else {
        if (sceneNum == SCENE_WATER_TEMPLE && flagType == FLAG_SCENE_SWITCH &&
            (flag == 0x1C || flag == 0x1D || flag == 0x1E)) {
            return;
        }
        if (sceneNum == SCENE_FOREST_TEMPLE && flagType == FLAG_SCENE_SWITCH && flag == 0x1B) {
            return;
        }

        auto effect = new GameInteractionEffect::SetSceneFlag();
        effect->parameters[0] = sceneNum;
        effect->parameters[1] = flagType;
        effect->parameters[2] = flag;
        effect->Apply();
    }
}

// ============================================================================
// UNSET_FLAG (ported from Anchor)
// ============================================================================

void Harpoon::SendPacket_UnsetFlag(s16 sceneNum, s16 flagType, s16 flag) {
    if (!IsSaveLoaded() || !syncItems) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = HPN_UNSET_FLAG;
    payload["sceneNum"] = sceneNum;
    payload["flagType"] = flagType;
    payload["flag"] = flag;
    SendJsonToRemote(payload);
}

void Harpoon::HandlePacket_UnsetFlag(nlohmann::json payload) {
    if (!IsSaveLoaded() || !syncItems) {
        return;
    }

    s16 sceneNum = payload["sceneNum"].get<s16>();
    s16 flagType = payload["flagType"].get<s16>();
    s16 flag = payload["flag"].get<s16>();

    if (sceneNum == SCENE_ID_MAX) {
        auto effect = new GameInteractionEffect::UnsetFlag();
        effect->parameters[0] = flagType;
        effect->parameters[1] = flag;
        effect->Apply();

        if (flagType == FLAG_RANDOMIZER_INF &&
            (flag >= RAND_INF_ADULT_TRADES_HAS_POCKET_EGG && flag <= RAND_INF_ADULT_TRADES_HAS_CLAIM_CHECK)) {
            u16 itemToReplace = ITEM_POCKET_EGG;
            switch (flag) {
                case RAND_INF_ADULT_TRADES_HAS_POCKET_EGG:     itemToReplace = ITEM_POCKET_EGG; break;
                case RAND_INF_ADULT_TRADES_HAS_POCKET_CUCCO:   itemToReplace = ITEM_POCKET_CUCCO; break;
                case RAND_INF_ADULT_TRADES_HAS_COJIRO:         itemToReplace = ITEM_COJIRO; break;
                case RAND_INF_ADULT_TRADES_HAS_ODD_MUSHROOM:   itemToReplace = ITEM_ODD_MUSHROOM; break;
                case RAND_INF_ADULT_TRADES_HAS_ODD_POTION:     itemToReplace = ITEM_ODD_POTION; break;
                case RAND_INF_ADULT_TRADES_HAS_SAW:            itemToReplace = ITEM_SAW; break;
                case RAND_INF_ADULT_TRADES_HAS_SWORD_BROKEN:   itemToReplace = ITEM_SWORD_BROKEN; break;
                case RAND_INF_ADULT_TRADES_HAS_PRESCRIPTION:   itemToReplace = ITEM_PRESCRIPTION; break;
                case RAND_INF_ADULT_TRADES_HAS_FROG:           itemToReplace = ITEM_FROG; break;
                case RAND_INF_ADULT_TRADES_HAS_EYEDROPS:       itemToReplace = ITEM_EYEDROPS; break;
                case RAND_INF_ADULT_TRADES_HAS_CLAIM_CHECK:    itemToReplace = ITEM_CLAIM_CHECK; break;
            }
            Inventory_ReplaceItem(gPlayState, itemToReplace, Randomizer_GetNextAdultTradeItem());
        }
    } else {
        if (sceneNum == SCENE_WATER_TEMPLE && flagType == FLAG_SCENE_SWITCH &&
            (flag == 0x1C || flag == 0x1D || flag == 0x1E)) {
            return;
        }
        if (sceneNum == SCENE_FOREST_TEMPLE && flagType == FLAG_SCENE_SWITCH && flag == 0x1B) {
            return;
        }

        auto effect = new GameInteractionEffect::UnsetSceneFlag();
        effect->parameters[0] = sceneNum;
        effect->parameters[1] = flagType;
        effect->parameters[2] = flag;
        effect->Apply();
    }
}

// ============================================================================
// SET_CHECK_STATUS (ported from Anchor)
// ============================================================================

static bool sIsResultOfCheckStatusHandling = false;

void Harpoon::SendPacket_SetCheckStatus(RandomizerCheck rc) {
    if (!IsSaveLoaded() || sIsResultOfCheckStatusHandling) {
        return;
    }

    auto randoContext = Rando::Context::GetInstance();

    nlohmann::json payload;
    payload["type"] = HPN_SET_CHECK_STATUS;
    payload["rc"] = rc;
    payload["status"] = randoContext->GetItemLocation(rc)->GetCheckStatus();
    payload["skipped"] = randoContext->GetItemLocation(rc)->GetIsSkipped();
    payload["quiet"] = true;
    SendJsonToRemote(payload);
}

void Harpoon::HandlePacket_SetCheckStatus(nlohmann::json payload) {
    if (!IsSaveLoaded() || !syncItems) {
        return;
    }

    auto randoContext = Rando::Context::GetInstance();

    RandomizerCheck rc = payload["rc"].get<RandomizerCheck>();
    RandomizerCheckStatus status = payload["status"].get<RandomizerCheckStatus>();
    bool skipped = payload["skipped"].get<bool>();

    sIsResultOfCheckStatusHandling = true;

    if (randoContext->GetItemLocation(rc)->GetCheckStatus() != status) {
        randoContext->GetItemLocation(rc)->SetCheckStatus(status);
    }
    if (randoContext->GetItemLocation(rc)->GetIsSkipped() != skipped) {
        randoContext->GetItemLocation(rc)->SetIsSkipped(skipped);
    }

    CheckTracker::RecalculateAllAreaTotals();
    CheckTracker::RecalculateAvailableChecks();
    sIsResultOfCheckStatusHandling = false;
}

// ============================================================================
// ENTRANCE_DISCOVERED (ported from Anchor)
// ============================================================================

void Harpoon::SendPacket_EntranceDiscovered(u16 entranceIndex) {
    if (!IsSaveLoaded() || isProcessingIncomingPacket || !syncItems) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = HPN_ENTRANCE_DISCOVERED;
    payload["entranceIndex"] = entranceIndex;
    payload["quiet"] = true;
    SendJsonToRemote(payload);
}

void Harpoon::HandlePacket_EntranceDiscovered(nlohmann::json payload) {
    if (!IsSaveLoaded() || !syncItems) {
        return;
    }

    u16 entranceIndex = payload["entranceIndex"].get<u16>();
    Entrance_SetEntranceDiscovered(entranceIndex, 1);
}

// ============================================================================
// UPDATE_DUNGEON_ITEMS (ported from Anchor)
// ============================================================================

void Harpoon::SendPacket_UpdateDungeonItems() {
    if (!IsSaveLoaded() || !syncItems) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = HPN_UPDATE_DUNGEON_ITEMS;
    payload["mapIndex"] = gSaveContext.mapIndex;
    payload["dungeonItems"] = gSaveContext.inventory.dungeonItems[gSaveContext.mapIndex];
    payload["dungeonKeys"] = gSaveContext.inventory.dungeonKeys[gSaveContext.mapIndex];
    SendJsonToRemote(payload);
}

void Harpoon::HandlePacket_UpdateDungeonItems(nlohmann::json payload) {
    if (!IsSaveLoaded() || !syncItems) {
        return;
    }

    u16 mapIndex = payload["mapIndex"].get<u16>();
    gSaveContext.inventory.dungeonItems[mapIndex] = payload["dungeonItems"].get<u8>();
    gSaveContext.inventory.dungeonKeys[mapIndex] = payload["dungeonKeys"].get<s8>();
}

// ============================================================================
// TELEPORT_TO (ported from Anchor)
// ============================================================================

void Harpoon::SendPacket_TeleportTo(uint32_t clientId) {
    if (!IsSaveLoaded()) {
        return;
    }

    Player* player = GET_PLAYER(gPlayState);

    nlohmann::json payload;
    payload["type"] = HPN_TELEPORT_TO;
    payload["targetClientId"] = clientId;
    payload["entranceIndex"] = gSaveContext.entranceIndex;
    payload["roomIndex"] = gPlayState->roomCtx.curRoom.num;
    payload["posRot"] = player->actor.world;
    SendJsonToRemote(payload);
}

void Harpoon::HandlePacket_TeleportTo(nlohmann::json payload) {
    if (!IsSaveLoaded()) {
        return;
    }

    s32 entranceIndex = payload["entranceIndex"].get<s32>();
    s8 roomIndex = payload["roomIndex"].get<s8>();
    PosRot posRot = payload["posRot"].get<PosRot>();

    gPlayState->nextEntranceIndex = entranceIndex;
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType = TRANS_TYPE_INSTANT;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].entranceIndex = entranceIndex;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].roomIndex = roomIndex;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].pos = posRot.pos;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].yaw = posRot.rot.y;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].playerParams = 0xDFF;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK_FAST;
    gSaveContext.respawnFlag = 1;

    static HOOK_ID hookId = 0;
    hookId = REGISTER_VB_SHOULD(VB_INFLICT_VOID_DAMAGE, {
        *should = false;
        GameInteractor::Instance->UnregisterGameHookForID<GameInteractor::OnVanillaBehavior>(hookId);
    });
}

// ============================================================================
// UPDATE_BEANS_COUNT (ported from Anchor)
// ============================================================================

void Harpoon::SendPacket_UpdateBeansCount() {
    if (!IsSaveLoaded() || !syncItems) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = HPN_UPDATE_BEANS_COUNT;
    payload["amount"] = AMMO(ITEM_BEAN);
    payload["amountBought"] = BEANS_BOUGHT;
    SendJsonToRemote(payload);
}

void Harpoon::HandlePacket_UpdateBeansCount(nlohmann::json payload) {
    if (!IsSaveLoaded() || !syncItems) {
        return;
    }

    AMMO(ITEM_BEAN) = payload["amount"].get<s8>();
    BEANS_BOUGHT = payload["amountBought"].get<s8>();
}
