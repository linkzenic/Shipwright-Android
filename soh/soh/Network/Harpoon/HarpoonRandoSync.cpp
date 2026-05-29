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
    if (!IsSaveLoaded() || isProcessingIncomingPacket || isHandlingUpdateTeamState) {
        return;
    }
    if (!syncItems) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            SPDLOG_WARN("[Harpoon] flag not broadcast — current room has sync_items=false "
                        "(sceneNum={} flagType={} flag={})", sceneNum, flagType, flag);
        }
        return;
    }
    SPDLOG_DEBUG("[Harpoon] SendPacket_SetFlag scene={} type={} flag={}",
                 sceneNum, flagType, flag);

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

    isProcessingIncomingPacket = true;
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
            isProcessingIncomingPacket = false;
            return;
        }
        if (sceneNum == SCENE_FOREST_TEMPLE && flagType == FLAG_SCENE_SWITCH && flag == 0x1B) {
            isProcessingIncomingPacket = false;
            return;
        }

        auto effect = new GameInteractionEffect::SetSceneFlag();
        effect->parameters[0] = sceneNum;
        effect->parameters[1] = flagType;
        effect->parameters[2] = flag;
        effect->Apply();
    }
    isProcessingIncomingPacket = false;
}

// ============================================================================
// UNSET_FLAG (ported from Anchor)
// ============================================================================

void Harpoon::SendPacket_UnsetFlag(s16 sceneNum, s16 flagType, s16 flag) {
    if (!IsSaveLoaded() || isProcessingIncomingPacket || isHandlingUpdateTeamState ||
        !syncItems) {
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

    isProcessingIncomingPacket = true;
    if (sceneNum == SCENE_ID_MAX) {
        auto effect = new GameInteractionEffect::UnsetFlag();
        effect->parameters[0] = flagType;
        effect->parameters[1] = flag;
        effect->Apply();

        if (flagType == FLAG_RANDOMIZER_INF &&
            (flag >= RAND_INF_ADULT_TRADES_HAS_POCKET_EGG && flag <= RAND_INF_ADULT_TRADES_HAS_CLAIM_CHECK)) {
            u16 itemToReplace = ITEM_POCKET_EGG;
            switch (flag) {
                case RAND_INF_ADULT_TRADES_HAS_POCKET_EGG:
                    itemToReplace = ITEM_POCKET_EGG;
                    break;
                case RAND_INF_ADULT_TRADES_HAS_POCKET_CUCCO:
                    itemToReplace = ITEM_POCKET_CUCCO;
                    break;
                case RAND_INF_ADULT_TRADES_HAS_COJIRO:
                    itemToReplace = ITEM_COJIRO;
                    break;
                case RAND_INF_ADULT_TRADES_HAS_ODD_MUSHROOM:
                    itemToReplace = ITEM_ODD_MUSHROOM;
                    break;
                case RAND_INF_ADULT_TRADES_HAS_ODD_POTION:
                    itemToReplace = ITEM_ODD_POTION;
                    break;
                case RAND_INF_ADULT_TRADES_HAS_SAW:
                    itemToReplace = ITEM_SAW;
                    break;
                case RAND_INF_ADULT_TRADES_HAS_SWORD_BROKEN:
                    itemToReplace = ITEM_SWORD_BROKEN;
                    break;
                case RAND_INF_ADULT_TRADES_HAS_PRESCRIPTION:
                    itemToReplace = ITEM_PRESCRIPTION;
                    break;
                case RAND_INF_ADULT_TRADES_HAS_FROG:
                    itemToReplace = ITEM_FROG;
                    break;
                case RAND_INF_ADULT_TRADES_HAS_EYEDROPS:
                    itemToReplace = ITEM_EYEDROPS;
                    break;
                case RAND_INF_ADULT_TRADES_HAS_CLAIM_CHECK:
                    itemToReplace = ITEM_CLAIM_CHECK;
                    break;
            }
            Inventory_ReplaceItem(gPlayState, itemToReplace, Randomizer_GetNextAdultTradeItem());
        }
    } else {
        if (sceneNum == SCENE_WATER_TEMPLE && flagType == FLAG_SCENE_SWITCH &&
            (flag == 0x1C || flag == 0x1D || flag == 0x1E)) {
            isProcessingIncomingPacket = false;
            return;
        }
        if (sceneNum == SCENE_FOREST_TEMPLE && flagType == FLAG_SCENE_SWITCH && flag == 0x1B) {
            isProcessingIncomingPacket = false;
            return;
        }

        auto effect = new GameInteractionEffect::UnsetSceneFlag();
        effect->parameters[0] = sceneNum;
        effect->parameters[1] = flagType;
        effect->parameters[2] = flag;
        effect->Apply();
    }
    isProcessingIncomingPacket = false;
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

// ============================================================================
// CUTSCENE_TRIGGER (story sync) — broadcast a freshly-set cutsceneIndex so
// other players in the same scene replay it. Best-effort; if a remote isn't
// in the same scene the packet is dropped.
// ============================================================================

void Harpoon::SendPacket_CutsceneTrigger(s32 cutsceneIndex, s16 sceneNum) {
    if (!IsSaveLoaded() || !syncCutscenes) {
        return;
    }
    nlohmann::json payload;
    payload["type"]          = HPN_SAVE_CUTSCENE;
    payload["cutsceneIndex"] = cutsceneIndex;
    payload["sceneNum"]      = sceneNum;
    SendJsonToRemote(payload);
}

void Harpoon::HandlePacket_CutsceneTrigger(nlohmann::json payload) {
    if (!IsSaveLoaded() || !syncCutscenes || gPlayState == nullptr) {
        return;
    }
    s16 sceneNum = payload.value("sceneNum", -1);
    if (sceneNum != gPlayState->sceneNum) {
        // Different scene — drop. The flag-sync layer will pick up most
        // permanent state changes anyway.
        return;
    }
    s32 cutsceneIndex = payload.value("cutsceneIndex", 0);
    if (cutsceneIndex == 0) {
        return;
    }
    isProcessingIncomingPacket = true;
    gSaveContext.cutsceneIndex = cutsceneIndex;
    isProcessingIncomingPacket = false;
}

// ============================================================================
// REQUEST_TEAM_STATE (story / rando) — late-joiner pulls the team's current
// save. Server forwards to other members; the first to respond pushes back
// via SAVE.UPDATE_TEAM_STATE which the existing handler applies.
// ============================================================================

void Harpoon::SendPacket_RequestTeamState() {
    if (!IsSaveLoaded() || !syncItems) {
        return;
    }
    nlohmann::json payload;
    payload["type"] = HPN_SAVE_TEAM_REQUEST;
    SendJsonToRemote(payload);
}

void Harpoon::HandlePacket_RequestTeamState(nlohmann::json payload) {
    // Some teammate just joined and wants the current team save. If we've
    // got one loaded, push it.
    if (!IsSaveLoaded() || !syncItems) {
        return;
    }
    SendPacket_UpdateTeamState();
}

// ============================================================================
// GAME_COMPLETE — broadcast when the local player kills final Ganon.
// ============================================================================

void Harpoon::SendPacket_GameComplete() {
    if (!IsSaveLoaded()) {
        return;
    }
    nlohmann::json payload;
    payload["type"] = HPN_SAVE_GAME_COMPLETE;
    SendJsonToRemote(payload);
}

void Harpoon::HandlePacket_GameComplete(nlohmann::json payload) {
    uint32_t clientId = payload.value("clientId",
                          payload.value("source", 0u));
    std::string senderName = "Someone";
    if (clients.contains(clientId)) {
        senderName = clients[clientId].name;
    }
    Notification::Emit({
        .prefix = senderName,
        .message = "killed Ganon!",
    });
}

// ============================================================================
// OCARINA_SFX — stream ocarina notes to teammates in the same scene.
// Ported 1:1 from Anchor (OcarinaSfx.cpp).
// ============================================================================

extern "C" {
extern f32 D_80130F28;
}

void Harpoon::SendPacket_OcarinaSfx(uint8_t note, float modulator, int8_t bend) {
    if (!IsSaveLoaded() || gPlayState == nullptr) {
        return;
    }

    nlohmann::json payload;
    payload["type"]      = HPN_AUDIO_OCARINA;
    payload["note"]      = note;
    payload["modulator"] = modulator;
    payload["bend"]      = bend;
    payload["quiet"]     = true;

    // Anchor's pattern: send a separate addressed copy per teammate in the
    // same scene. Server-side this becomes one broadcast filtered by scene
    // (or several `targetClientId` deliveries).
    for (auto& [clientId, client] : clients) {
        if (client.sceneNum == gPlayState->sceneNum && client.online &&
            client.isSaveLoaded && !client.self) {
            payload["targetClientId"] = clientId;
            SendJsonToRemote(payload);
        }
    }
}

void Harpoon::HandlePacket_OcarinaSfx(nlohmann::json payload) {
    uint32_t clientId = payload.value("clientId",
                          payload.value("source", 0u));
    if (!clients.contains(clientId) || clients[clientId].player == nullptr) {
        return;
    }

    auto& client = clients[clientId];
    uint8_t note   = payload.value("note", (uint8_t)0xFF);
    float modulator = payload.value("modulator", 1.0f);
    int8_t bend    = payload.value("bend", (int8_t)0);

    client.ocarinaModulator = modulator;
    client.ocarinaBend      = bend;

    if ((note != 0xFF) && (client.ocarinaNote != note)) {
        Audio_QueueCmdS8(0x6 << 24 | SEQ_PLAYER_SFX << 16 | 0xD07, client.ocarinaBend - 1);
        Audio_QueueCmdS8(0x6 << 24 | SEQ_PLAYER_SFX << 16 | 0xD05, note);
        Audio_PlaySoundGeneral(NA_SE_OC_OCARINA, &client.player->actor.projectedPos, 4,
                               &client.ocarinaModulator, &D_80130F28, &gSfxDefaultReverb);
    } else if ((client.ocarinaNote != 0xFF) && (note == 0xFF)) {
        Audio_StopSfxById(NA_SE_OC_OCARINA);
    }

    client.ocarinaNote = note;
}

// ============================================================================
// APPEARANCE.SPAWN_VFX_ACTOR — fire-and-forget visual actor broadcast.
//
// Used for sw97 medallion arrows + spells, FD beam, Zora fin throw, Deku
// bubble, and any other custom visual actor whose appearance shouldn't
// require the receiving client to recompute physics. The actor's own update
// runs deterministically; we only ship the spawn event.
//
// Owner tracking: when a remote-spawned VFX actor's AT collider hits the
// local player and PvP is enabled, the local client uses the owner registry
// to attribute damage to the right attacker (Phase 4).
// ============================================================================

#include <unordered_map>
namespace { std::unordered_map<const Actor*, uint32_t> sVfxActorOwners; }

void Harpoon::SetVfxActorOwner(const Actor* actor, uint32_t ownerClientId) {
    if (actor == nullptr) return;
    sVfxActorOwners[actor] = ownerClientId;
}

uint32_t Harpoon::GetVfxActorOwner(const Actor* actor) {
    if (actor == nullptr) return 0;
    auto it = sVfxActorOwners.find(actor);
    return it == sVfxActorOwners.end() ? 0 : it->second;
}

// Purge the VFX-actor → owner-clientId lookup table. Called on scene
// transitions (the engine recycles its actor pool on scene load, so any
// Actor* held here is dangling) and on disconnect (no peers left to route
// damage to). Without this, the map grew unbounded across a long session
// and stale Actor* pointers could collide with newly-spawned actors,
// causing damage routing to attribute hits to the wrong shooter.
void Harpoon::ClearVfxActorOwners() {
    sVfxActorOwners.clear();
}

void Harpoon::SendPacket_SpawnVfxActor(int16_t actorId, float posX, float posY, float posZ,
                                        int16_t rotX, int16_t rotY, int16_t rotZ,
                                        int16_t params, const char* vfxKind,
                                        bool attachedToOwner) {
    if (!IsSaveLoaded() || !isConnected) {
        return;
    }
    nlohmann::json payload;
    payload["type"]            = HPN_APPEARANCE_SPAWN_VFX;
    payload["actorId"]         = actorId;
    payload["posX"]            = posX;
    payload["posY"]            = posY;
    payload["posZ"]            = posZ;
    payload["rotX"]            = rotX;
    payload["rotY"]            = rotY;
    payload["rotZ"]            = rotZ;
    payload["params"]          = params;
    payload["vfxKind"]         = vfxKind ? vfxKind : "generic";
    payload["attachedToOwner"] = attachedToOwner;
    SendJsonToRemote(payload);
}

void Harpoon::HandlePacket_SpawnVfxActor(nlohmann::json payload) {
    if (!IsSaveLoaded() || gPlayState == nullptr) return;

    uint32_t ownerClientId = payload.value("clientId",
                                payload.value("source", 0u));
    if (ownerClientId == ownClientId) {
        // Echo of our own packet — ignore (we already spawned locally).
        return;
    }

    int16_t actorId = (int16_t)payload.value("actorId", 0);
    float px = payload.value("posX", 0.0f);
    float py = payload.value("posY", 0.0f);
    float pz = payload.value("posZ", 0.0f);
    int16_t rx = (int16_t)payload.value("rotX", 0);
    int16_t ry = (int16_t)payload.value("rotY", 0);
    int16_t rz = (int16_t)payload.value("rotZ", 0);
    int16_t params = (int16_t)payload.value("params", 0);
    std::string vfxKind = payload.value("vfxKind", std::string("generic"));

    Actor* spawned = Actor_Spawn(&gPlayState->actorCtx, gPlayState, actorId,
                                 px, py, pz, rx, ry, rz, params);
    if (spawned == nullptr) {
        SPDLOG_DEBUG("[Harpoon] HandlePacket_SpawnVfxActor: Actor_Spawn failed for id={} kind={}",
                     actorId, vfxKind);
        return;
    }
    SetVfxActorOwner(spawned, ownerClientId);
    SPDLOG_DEBUG("[Harpoon] spawned VFX actor id={} kind={} owner={}",
                 actorId, vfxKind, ownerClientId);
}
