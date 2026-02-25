#include "PvPAnchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/nametag.h"
#include "soh/ObjectExtension/ObjectExtension.h"
#include "soh/Network/Anchor/Anchor.h"

extern "C" {
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

// MARK: - Overrides

void PvPAnchor::Enable() {
    // Auto-switch: disconnect normal Anchor if active
    if (Anchor::Instance && Anchor::Instance->isConnected) {
        Anchor::Instance->Disable();
    }

    Network::Enable(CVarGetString(CVAR_PVP_ANCHOR("Host"), "localhost"),
                    CVarGetInteger(CVAR_PVP_ANCHOR("Port"), 43384));
    ownClientId = 0;
    gameState = PVP_HG_DISCONNECTED;
    isEliminated = false;
    killFeed.clear();
}

void PvPAnchor::Disable() {
    Network::Disable();
    clients.clear();
    gameState = PVP_HG_DISCONNECTED;
    isEliminated = false;
    killFeed.clear();
    RefreshClientActors();
}

void PvPAnchor::OnConnected() {
    SendPacket_Handshake();
    RegisterHooks();
    gameState = PVP_HG_LOBBY;
}

void PvPAnchor::OnDisconnected() {
    gameState = PVP_HG_DISCONNECTED;
    RegisterHooks();
}

void PvPAnchor::ProcessOutgoingPackets() {
    std::queue<nlohmann::json> packetsToSend;
    {
        std::lock_guard<std::mutex> lock(outgoingPacketQueueMutex);
        packetsToSend.swap(outgoingPacketQueue);
    }

    while (!packetsToSend.empty()) {
        nlohmann::json payload = packetsToSend.front();
        packetsToSend.pop();

        if (!payload.contains("quiet")) {
            SPDLOG_DEBUG("[PvPAnchor] Sending payload:\n{}", payload.dump());
        }
        Network::SendJsonToRemote(payload);
    }
}

void PvPAnchor::SendJsonToRemote(nlohmann::json payload) {
    if (!isConnected) {
        return;
    }

    payload["clientId"] = ownClientId;

    if (payload["type"] == PVP_HANDSHAKE) {
        Network::SendJsonToRemote(payload);
        return;
    }

    std::lock_guard<std::mutex> lock(outgoingPacketQueueMutex);
    outgoingPacketQueue.push(payload);
}

void PvPAnchor::OnIncomingJson(nlohmann::json payload) {
    if (!payload.contains("type")) {
        return;
    }

    if (!payload.contains("quiet")) {
        SPDLOG_DEBUG("[PvPAnchor] Received payload:\n{}", payload.dump());
    }

    std::lock_guard<std::mutex> lock(incomingPacketQueueMutex);
    incomingPacketQueue.push(payload);
}

void PvPAnchor::ProcessIncomingPacketQueue() {
    std::queue<nlohmann::json> packetsToProcess;
    {
        std::lock_guard<std::mutex> lock(incomingPacketQueueMutex);
        packetsToProcess.swap(incomingPacketQueue);
    }

    while (!packetsToProcess.empty()) {
        nlohmann::json payload = packetsToProcess.front();
        packetsToProcess.pop();

        std::string packetType = payload["type"].get<std::string>();

        try {
            if (packetType == PVP_ALL_CLIENTS)
                HandlePacket_AllClients(payload);
            else if (packetType == PVP_PLAYER_UPDATE)
                HandlePacket_PlayerUpdate(payload);
            else if (packetType == PVP_DAMAGE)
                HandlePacket_Damage(payload);
            else if (packetType == PVP_PLAYER_DIED)
                HandlePacket_PlayerDied(payload);
            else if (packetType == PVP_GAME_STATE)
                HandlePacket_GameState(payload);
            else if (packetType == PVP_WINNER)
                HandlePacket_Winner(payload);
            else if (packetType == PVP_SERVER_MSG)
                HandlePacket_ServerMsg(payload);
            else if (packetType == PVP_PLAYER_SFX)
                HandlePacket_PlayerSfx(payload);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("[PvPAnchor] Exception processing packet: {}", e.what());
        }
    }
}

// MARK: - Helpers

struct PvPDummyPlayerClientId {
    uint32_t clientId = 0;
};
static ObjectExtension::Register<PvPDummyPlayerClientId> PvPDummyPlayerClientIdRegister;

uint32_t PvPAnchor::GetDummyPlayerClientId(const Actor* actor) {
    const PvPDummyPlayerClientId* id = ObjectExtension::GetInstance().Get<PvPDummyPlayerClientId>(actor);
    return id != nullptr ? id->clientId : 0;
}

void PvPAnchor::SetDummyPlayerClientId(const Actor* actor, uint32_t clientId) {
    ObjectExtension::GetInstance().Set<PvPDummyPlayerClientId>(actor, PvPDummyPlayerClientId{ clientId });
}

void PvPAnchor::RefreshClientActors() {
    if (!IsSaveLoaded()) {
        return;
    }

    Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head;

    while (actor != NULL) {
        if (actor->id == ACTOR_EN_OE2 && actor->update == PvPDummyPlayer_Update) {
            NameTag_RemoveAllForActor(actor);
            Actor_Kill(actor);
        }
        actor = actor->next;
    }

    for (auto& [clientId, client] : clients) {
        if (!client.online || client.self) {
            continue;
        }

        spawningDummyPlayerForClientId = clientId;
        auto dummy =
            Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, client.posRot.pos.x, client.posRot.pos.y,
                        client.posRot.pos.z, client.posRot.rot.x, client.posRot.rot.y, client.posRot.rot.z, 0, false);
        client.player = (Player*)dummy;
    }
    spawningDummyPlayerForClientId = 0;
}

bool PvPAnchor::IsSaveLoaded() {
    if (gPlayState == nullptr) return false;
    if (GET_PLAYER(gPlayState) == nullptr) return false;
    if (gSaveContext.fileNum < 0 || gSaveContext.fileNum > 2) return false;
    if (gSaveContext.gameMode != GAMEMODE_NORMAL) return false;
    return true;
}

nlohmann::json PvPAnchor::PrepClientState() {
    nlohmann::json state;
    state["name"] = CVarGetString(CVAR_PVP_ANCHOR("Name"), "Player");
    Color_RGBA8 color = CVarGetColor(CVAR_PVP_ANCHOR("Color.Value"), { 100, 255, 100 });
    state["color"] = { {"r", color.r}, {"g", color.g}, {"b", color.b} };
    state["clientVersion"] = (char*)gGitCommitHash;
    state["isSaveLoaded"] = IsSaveLoaded();
    if (IsSaveLoaded()) {
        state["sceneNum"] = gPlayState->sceneNum;
        state["entranceIndex"] = gSaveContext.entranceIndex;
        state["linkAge"] = gSaveContext.linkAge;
    }
    return state;
}

// MARK: - Send Packets

void PvPAnchor::SendPacket_Handshake() {
    nlohmann::json payload;
    payload["type"] = PVP_HANDSHAKE;
    payload["clientState"] = PrepClientState();
    SendJsonToRemote(payload);
}

void PvPAnchor::SendPacket_PlayerUpdate() {
    if (!IsSaveLoaded()) return;

    uint32_t currentPlayerCount = 0;
    for (auto& [clientId, client] : clients) {
        if (client.sceneNum == gPlayState->sceneNum && client.online && client.isSaveLoaded && !client.self) {
            currentPlayerCount++;
        }
    }
    if (currentPlayerCount == 0) return;

    Player* player = GET_PLAYER(gPlayState);
    nlohmann::json payload;

    payload["type"] = PVP_PLAYER_UPDATE;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["entranceIndex"] = gSaveContext.entranceIndex;
    payload["linkAge"] = gSaveContext.linkAge;
    payload["posRot"]["pos"] = { {"x", player->actor.world.pos.x}, {"y", player->actor.world.pos.y}, {"z", player->actor.world.pos.z} };
    payload["posRot"]["rot"] = { {"x", player->actor.shape.rot.x}, {"y", player->actor.shape.rot.y}, {"z", player->actor.shape.rot.z} };

    std::vector<int> jointArray;
    for (size_t i = 0; i < 24; i++) {
        Vec3s joint = player->skelAnime.jointTable[i];
        jointArray.push_back(joint.x);
        jointArray.push_back(joint.y);
        jointArray.push_back(joint.z);
    }
    payload["jointTable"] = jointArray;
    payload["prevTransl"] = { {"x", player->skelAnime.prevTransl.x}, {"y", player->skelAnime.prevTransl.y}, {"z", player->skelAnime.prevTransl.z} };
    payload["movementFlags"] = player->skelAnime.movementFlags;
    payload["upperLimbRot"] = { {"x", player->upperLimbRot.x}, {"y", player->upperLimbRot.y}, {"z", player->upperLimbRot.z} };
    payload["currentBoots"] = player->currentBoots;
    payload["currentShield"] = player->currentShield;
    payload["currentTunic"] = player->currentTunic;
    payload["stateFlags1"] = player->stateFlags1;
    payload["stateFlags2"] = player->stateFlags2 & ~PLAYER_STATE2_DISABLE_DRAW;
    payload["buttonItem0"] = gSaveContext.equips.buttonItems[0];
    payload["itemAction"] = player->itemAction;
    payload["heldItemAction"] = player->heldItemAction;
    payload["modelGroup"] = player->modelGroup;
    payload["invincibilityTimer"] = player->invincibilityTimer;
    payload["unk_862"] = player->unk_862;
    payload["unk_85C"] = player->unk_85C;
    payload["actionVar1"] = player->av1.actionVar1;

    // === Transformation data ===
    // TODO: Read from TransformMasks system when active
    payload["transformation"] = 0; // 0 = human/no transform
    payload["cylRadius"] = player->cylinder.dim.radius;
    payload["cylHeight"] = player->cylinder.dim.height;
    payload["cylYShift"] = player->cylinder.dim.yShift;
    payload["mmStateFlags3"] = 0;
    payload["mmSpeedXZ"] = 0.0f;

    payload["quiet"] = true;

    for (auto& [clientId, client] : clients) {
        if (client.sceneNum == gPlayState->sceneNum && client.online && client.isSaveLoaded && !client.self) {
            payload["targetClientId"] = clientId;
            SendJsonToRemote(payload);
        }
    }
}

void PvPAnchor::SendPacket_Damage(u32 clientId, u8 damageEffect, u8 damage) {
    nlohmann::json payload;
    payload["type"] = PVP_DAMAGE;
    payload["targetClientId"] = clientId;
    payload["damageEffect"] = damageEffect;
    payload["damage"] = damage;
    SendJsonToRemote(payload);
}

void PvPAnchor::SendPacket_PlayerDied() {
    nlohmann::json payload;
    payload["type"] = PVP_PLAYER_DIED;
    SendJsonToRemote(payload);
}

void PvPAnchor::SendPacket_PlayerSfx(u16 sfxId) {
    nlohmann::json payload;
    payload["type"] = PVP_PLAYER_SFX;
    payload["sfxId"] = sfxId;
    payload["quiet"] = true;
    SendJsonToRemote(payload);
}

void PvPAnchor::SendPacket_ChestOpened(s16 sceneNum, s16 flag) {
    nlohmann::json payload;
    payload["type"] = PVP_CHEST_OPENED;
    payload["sceneNum"] = sceneNum;
    payload["flag"] = flag;
    SendJsonToRemote(payload);
}

void PvPAnchor::SendPacket_Ready() {
    nlohmann::json payload;
    payload["type"] = PVP_READY;
    SendJsonToRemote(payload);
}

void PvPAnchor::SendPacket_StartGame() {
    nlohmann::json payload;
    payload["type"] = PVP_START_GAME;
    SendJsonToRemote(payload);
}

// MARK: - Handle Packets

void PvPAnchor::HandlePacket_AllClients(nlohmann::json payload) {
    if (payload.contains("ownClientId")) {
        ownClientId = payload["ownClientId"].get<uint32_t>();
    }

    if (payload.contains("clients")) {
        // Mark all existing as offline first
        for (auto& [id, client] : clients) {
            client.online = false;
        }

        for (auto& clientJson : payload["clients"]) {
            uint32_t clientId = clientJson["clientId"].get<uint32_t>();
            auto& client = clients[clientId];
            client.clientId = clientId;
            client.name = clientJson.value("name", "Player");
            if (clientJson.contains("color")) {
                client.color.r = clientJson["color"].value("r", (u8)100);
                client.color.g = clientJson["color"].value("g", (u8)255);
                client.color.b = clientJson["color"].value("b", (u8)100);
            }
            client.online = clientJson.value("online", true);
            client.self = (clientId == ownClientId);
            client.isSaveLoaded = clientJson.value("isSaveLoaded", false);
            client.sceneNum = clientJson.value("sceneNum", (s16)SCENE_ID_MAX);
            client.isAlive = clientJson.value("isAlive", true);
            client.isReady = clientJson.value("isReady", false);
            client.kills = clientJson.value("kills", (s16)0);
        }

        shouldRefreshActors = true;
    }
}

void PvPAnchor::HandlePacket_PlayerUpdate(nlohmann::json payload) {
    uint32_t clientId = payload["clientId"].get<uint32_t>();

    if (!clients.contains(clientId)) return;
    auto& client = clients[clientId];

    if (client.linkAge != payload.value("linkAge", (s32)LINK_AGE_ADULT)) {
        shouldRefreshActors = true;
    }

    client.sceneNum = payload.value("sceneNum", (s16)SCENE_ID_MAX);
    client.entranceIndex = payload.value("entranceIndex", (s32)0);
    client.linkAge = payload.value("linkAge", (s32)LINK_AGE_ADULT);

    if (payload.contains("posRot")) {
        auto& pr = payload["posRot"];
        if (pr.contains("pos")) {
            client.posRot.pos.x = pr["pos"].value("x", 0.0f);
            client.posRot.pos.y = pr["pos"].value("y", 0.0f);
            client.posRot.pos.z = pr["pos"].value("z", 0.0f);
        }
        if (pr.contains("rot")) {
            client.posRot.rot.x = pr["rot"].value("x", (s16)0);
            client.posRot.rot.y = pr["rot"].value("y", (s16)0);
            client.posRot.rot.z = pr["rot"].value("z", (s16)0);
        }
    }

    std::vector<int> jointArray = payload.value("jointTable", std::vector<int>{});
    jointArray.resize(24 * 3);
    for (int i = 0; i < 24; i++) {
        client.jointTable[i].x = jointArray[i * 3];
        client.jointTable[i].y = jointArray[i * 3 + 1];
        client.jointTable[i].z = jointArray[i * 3 + 2];
    }

    client.movementFlags = payload.value("movementFlags", (u8)0);
    if (payload.contains("prevTransl")) {
        client.prevTransl.x = payload["prevTransl"].value("x", (s16)0);
        client.prevTransl.y = payload["prevTransl"].value("y", (s16)0);
        client.prevTransl.z = payload["prevTransl"].value("z", (s16)0);
    }
    if (payload.contains("upperLimbRot")) {
        client.upperLimbRot.x = payload["upperLimbRot"].value("x", (s16)0);
        client.upperLimbRot.y = payload["upperLimbRot"].value("y", (s16)0);
        client.upperLimbRot.z = payload["upperLimbRot"].value("z", (s16)0);
    }
    client.currentBoots = payload.value("currentBoots", (s8)0);
    client.currentShield = payload.value("currentShield", (s8)0);
    client.currentTunic = payload.value("currentTunic", (s8)0);
    client.stateFlags1 = payload.value("stateFlags1", (u32)0);
    client.stateFlags2 = payload.value("stateFlags2", (u32)0);
    client.buttonItem0 = payload.value("buttonItem0", (u8)0);
    client.itemAction = payload.value("itemAction", (s8)0);
    client.heldItemAction = payload.value("heldItemAction", (s8)0);
    client.modelGroup = payload.value("modelGroup", (u8)0);
    client.invincibilityTimer = payload.value("invincibilityTimer", (s8)0);
    client.unk_862 = payload.value("unk_862", (s16)0);
    client.unk_85C = payload.value("unk_85C", (f32)0);
    client.actionVar1 = payload.value("actionVar1", (s8)0);

    // Transformation data
    client.transformation = payload.value("transformation", (u8)0);
    client.cylRadius = payload.value("cylRadius", (s16)30);
    client.cylHeight = payload.value("cylHeight", (s16)60);
    client.cylYShift = payload.value("cylYShift", (s16)0);
    client.mmStateFlags3 = payload.value("mmStateFlags3", (u32)0);
    client.mmSpeedXZ = payload.value("mmSpeedXZ", (f32)0);
}

void PvPAnchor::HandlePacket_Damage(nlohmann::json payload) {
    if (!IsSaveLoaded()) return;

    u8 damageEffect = payload.value("damageEffect", (u8)0);
    u8 damage = payload.value("damage", (u8)0);

    Player* self = GET_PLAYER(gPlayState);

    if (Player_InBlockingCsMode(gPlayState, self)) return;
    if (self->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_IN_CUTSCENE)) return;

    self->actor.colChkInfo.damage = damage * 8;

    if (damageEffect == PVP_HIT_RESPONSE_STUN) {
        self->actor.freezeTimer = 20;
        Actor_SetColorFilter(&self->actor, 0, 0xFF, 0, 24);
    } else if (damageEffect == PVP_HIT_RESPONSE_FIRE) {
        // Apply fire damage
        self->actor.colChkInfo.damage = damage * 8;
    }

    // Apply knockback from the attacker's direction
    uint32_t attackerClientId = payload.value("clientId", (uint32_t)0);
    if (clients.contains(attackerClientId) && clients[attackerClientId].player != nullptr) {
        Player* attacker = clients[attackerClientId].player;
        func_80837C0C(gPlayState, self, damageEffect, 4.0f, 5.0f,
                      Actor_WorldYawTowardActor(&attacker->actor, &self->actor), 20);
    }
}

void PvPAnchor::HandlePacket_PlayerDied(nlohmann::json payload) {
    uint32_t clientId = payload.value("clientId", (uint32_t)0);
    std::string killedName = "Unknown";

    if (clients.contains(clientId)) {
        clients[clientId].isAlive = false;
        killedName = clients[clientId].name;
    }

    std::string msg = payload.value("message", killedName + " was eliminated!");
    killFeed.push_back(msg);
    if (killFeed.size() > 5) {
        killFeed.erase(killFeed.begin());
    }

    aliveCount = payload.value("aliveCount", (s32)0);
}

void PvPAnchor::HandlePacket_GameState(nlohmann::json payload) {
    std::string state = payload.value("state", "lobby");

    if (state == "lobby") gameState = PVP_HG_LOBBY;
    else if (state == "countdown") gameState = PVP_HG_COUNTDOWN;
    else if (state == "playing") gameState = PVP_HG_PLAYING;
    else if (state == "finished") gameState = PVP_HG_FINISHED;

    countdownTimer = payload.value("timer", (s32)0);
    aliveCount = payload.value("aliveCount", (s32)0);

    if (gameState == PVP_HG_PLAYING) {
        isEliminated = false;
    }
}

void PvPAnchor::HandlePacket_Winner(nlohmann::json payload) {
    gameState = PVP_HG_FINISHED;
    std::string winnerName = payload.value("name", "Unknown");
    s16 winnerKills = payload.value("kills", (s16)0);
    std::string msg = winnerName + " wins with " + std::to_string(winnerKills) + " kills!";
    killFeed.push_back(msg);
}

void PvPAnchor::HandlePacket_ServerMsg(nlohmann::json payload) {
    std::string msg = payload.value("message", "");
    if (!msg.empty()) {
        killFeed.push_back(msg);
        if (killFeed.size() > 5) {
            killFeed.erase(killFeed.begin());
        }
    }
}

void PvPAnchor::HandlePacket_PlayerSfx(nlohmann::json payload) {
    // TODO: Play remote player SFX at their position
}
