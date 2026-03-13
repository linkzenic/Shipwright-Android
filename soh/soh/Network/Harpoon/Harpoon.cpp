#include "Harpoon.h"
#include "HarpoonBridge.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/nametag.h"
#include "soh/ObjectExtension/ObjectExtension.h"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/JsonConversions.hpp"
#include "soh/Notification/Notification.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/SohGui/ImGuiUtils.h"
#include "soh/Enhancements/item-tables/ItemTableManager.h"

extern "C" {
#include "variables.h"
#include "functions.h"
#include "mods/transformation_masks/transformation_masks.h"
#include "mods/items/custom_items.h"
#include "mods/extended_inventory.h"
#include "mods/actors/somaria_cubes.h"
#include "soh/Network/Harpoon/HarpoonBridge.h"
extern PlayState* gPlayState;
}

// MARK: - Overrides

void Harpoon::Enable() {
    // Auto-switch: disconnect normal Anchor if active
    if (Anchor::Instance && Anchor::Instance->isConnected) {
        Anchor::Instance->Disable();
    }

    Network::Enable(CVarGetString(CVAR_HARPOON("Host"), "localhost"), CVarGetInteger(CVAR_HARPOON("Port"), 43384));
    ownClientId = 0;
}

void Harpoon::Disable() {
    Network::Disable();
    // Kill remote somaria cubes before clearing clients (pointers would be lost)
    if (IsSaveLoaded()) {
        for (auto& [clientId, client] : clients) {
            for (int i = 0; i < 3; i++) {
                if (client.remoteCubeActors[i] != NULL) {
                    Actor_Kill(client.remoteCubeActors[i]);
                    client.remoteCubeActors[i] = NULL;
                }
            }
        }
    }
    clients.clear();
    RefreshClientActors();
}

void Harpoon::OnConnected() {
    SendPacket_Handshake();
    RegisterHooks();
}

void Harpoon::OnDisconnected() {
    RegisterHooks();
}

void Harpoon::ProcessOutgoingPackets() {
    std::queue<nlohmann::json> packetsToSend;
    {
        std::lock_guard<std::mutex> lock(outgoingPacketQueueMutex);
        packetsToSend.swap(outgoingPacketQueue);
    }

    while (!packetsToSend.empty()) {
        nlohmann::json payload = packetsToSend.front();
        packetsToSend.pop();

        if (!payload.contains("quiet")) {
            SPDLOG_DEBUG("[Harpoon] Sending payload:\n{}", payload.dump());
        }
        Network::SendJsonToRemote(payload);
    }
}

void Harpoon::SendJsonToRemote(nlohmann::json payload) {
    if (!isConnected) {
        return;
    }

    payload["clientId"] = ownClientId;

    if (payload["type"] == HPN_HANDSHAKE) {
        Network::SendJsonToRemote(payload);
        return;
    }

    std::lock_guard<std::mutex> lock(outgoingPacketQueueMutex);
    outgoingPacketQueue.push(payload);
}

void Harpoon::OnIncomingJson(nlohmann::json payload) {
    if (!payload.contains("type")) {
        return;
    }

    if (!payload.contains("quiet")) {
        SPDLOG_DEBUG("[Harpoon] Received payload:\n{}", payload.dump());
    }

    std::lock_guard<std::mutex> lock(incomingPacketQueueMutex);
    incomingPacketQueue.push(payload);
}

void Harpoon::ProcessIncomingPacketQueue() {
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
            if (packetType == HPN_ALL_CLIENTS)
                HandlePacket_AllClients(payload);
            else if (packetType == HPN_PLAYER_UPDATE)
                HandlePacket_PlayerUpdate(payload);
            else if (packetType == HPN_DAMAGE)
                HandlePacket_Damage(payload);
            else if (packetType == HPN_PLAYER_DIED)
                HandlePacket_PlayerDied(payload);
            else if (packetType == HPN_SERVER_MSG)
                HandlePacket_ServerMsg(payload);
            else if (packetType == HPN_PLAYER_SFX)
                HandlePacket_PlayerSfx(payload);
            else if (packetType == HPN_GIVE_ITEM)
                HandlePacket_GiveItem(payload);
            else if (packetType == HPN_UPDATE_TEAM_STATE)
                HandlePacket_UpdateTeamState(payload);
            // Scooter handlers
            else if (packetType == HPN_GAME_STATE)
                HandlePacket_GameState(payload);
            else if (packetType == HPN_WINNER)
                HandlePacket_Winner(payload);
            else if (packetType == "HARPOON_SERVER_INFO")
                HandlePacket_ServerInfo(payload);
            else if (packetType == "ROOM_JOINED")
                HandlePacket_RoomJoined(payload);
            else if (packetType == "ROOM_LEFT")
                HandlePacket_RoomLeft(payload);
            else if (packetType == "ROOM_LIST")
                HandlePacket_RoomList(payload);
            else if (packetType == HPN_ROLE_CHANGE)
                HandlePacket_RoleChange(payload);
            else if (packetType == HPN_MAP_VOTE)
                HandlePacket_MapVote(payload);
            else if (packetType == HPN_DECOY_HIT)
                HandlePacket_DecoyHit(payload);
            else if (packetType == HPN_CUSTOM_DAMAGE)
                HandlePacket_CustomDamage(payload);
            else if (packetType == HPN_CUSTOM_EFFECT)
                HandlePacket_CustomEffect(payload);
            // Anchor rando handlers
            else if (packetType == HPN_SET_FLAG)
                HandlePacket_SetFlag(payload);
            else if (packetType == HPN_UNSET_FLAG)
                HandlePacket_UnsetFlag(payload);
            else if (packetType == HPN_SET_CHECK_STATUS)
                HandlePacket_SetCheckStatus(payload);
            else if (packetType == HPN_ENTRANCE_DISCOVERED)
                HandlePacket_EntranceDiscovered(payload);
            else if (packetType == HPN_UPDATE_DUNGEON_ITEMS)
                HandlePacket_UpdateDungeonItems(payload);
            else if (packetType == HPN_TELEPORT_TO)
                HandlePacket_TeleportTo(payload);
            else if (packetType == HPN_UPDATE_BEANS_COUNT)
                HandlePacket_UpdateBeansCount(payload);
        } catch (const std::exception& e) { SPDLOG_ERROR("[Harpoon] Exception processing packet: {}", e.what()); }
    }
}

// MARK: - Helpers

struct HarpoonDummyPlayerClientId {
    uint32_t clientId = 0;
};
static ObjectExtension::Register<HarpoonDummyPlayerClientId> HarpoonDummyPlayerClientIdRegister;

uint32_t Harpoon::GetDummyPlayerClientId(const Actor* actor) {
    const HarpoonDummyPlayerClientId* id = ObjectExtension::GetInstance().Get<HarpoonDummyPlayerClientId>(actor);
    return id != nullptr ? id->clientId : 0;
}

void Harpoon::SetDummyPlayerClientId(const Actor* actor, uint32_t clientId) {
    ObjectExtension::GetInstance().Set<HarpoonDummyPlayerClientId>(actor, HarpoonDummyPlayerClientId{ clientId });
}

void Harpoon::RefreshClientActors() {
    if (!IsSaveLoaded()) {
        return;
    }

    // Kill all remote somaria cubes first
    for (auto& [clientId, client] : clients) {
        for (int i = 0; i < 3; i++) {
            if (client.remoteCubeActors[i] != NULL) {
                Actor_Kill(client.remoteCubeActors[i]);
                client.remoteCubeActors[i] = NULL;
            }
        }
    }

    Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head;

    while (actor != NULL) {
        if (actor->id == ACTOR_EN_OE2 && actor->update == HarpoonDummyPlayer_Update) {
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

bool Harpoon::IsSaveLoaded() {
    if (gPlayState == nullptr)
        return false;
    if (GET_PLAYER(gPlayState) == nullptr)
        return false;
    if (gSaveContext.fileNum > 2 && gSaveContext.fileNum < 0xFE)
        return false;
    if (gSaveContext.gameMode != GAMEMODE_NORMAL)
        return false;
    return true;
}

nlohmann::json Harpoon::PrepClientState() {
    nlohmann::json state;
    state["name"] = CVarGetString(CVAR_HARPOON("Name"), "Player");
    Color_RGBA8 color = CVarGetColor(CVAR_HARPOON("Color.Value"), { 100, 255, 100 });
    state["color"] = { { "r", color.r }, { "g", color.g }, { "b", color.b } };
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

void Harpoon::SendPacket_Handshake() {
    nlohmann::json payload;
    payload["type"] = HPN_HANDSHAKE;
    payload["clientState"] = PrepClientState();
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_PlayerUpdate() {
    if (!IsSaveLoaded())
        return;

    uint32_t currentPlayerCount = 0;
    for (auto& [clientId, client] : clients) {
        if (client.sceneNum == gPlayState->sceneNum && client.online && client.isSaveLoaded && !client.self) {
            currentPlayerCount++;
        }
    }
    if (currentPlayerCount == 0)
        return;

    Player* player = GET_PLAYER(gPlayState);
    nlohmann::json payload;

    payload["type"] = HPN_PLAYER_UPDATE;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["entranceIndex"] = gSaveContext.entranceIndex;
    payload["linkAge"] = gSaveContext.linkAge;
    payload["posRot"]["pos"] = { { "x", player->actor.world.pos.x },
                                 { "y", player->actor.world.pos.y },
                                 { "z", player->actor.world.pos.z } };
    payload["posRot"]["rot"] = { { "x", player->actor.shape.rot.x },
                                 { "y", player->actor.shape.rot.y },
                                 { "z", player->actor.shape.rot.z } };

    // Read joint table from MM form when transformed, OOT player otherwise
    std::vector<int> jointArray;
    Vec3s* srcJointTable = player->skelAnime.jointTable;
    s32 srcJointCount = 24;

    u8 modelType = TransformMasks_GetModelType();
    if (modelType > 0) {
        Vec3s* mmJoints = TransformMasks_GetFormJointTable();
        s32 mmCount = TransformMasks_GetFormJointCount();
        if (mmJoints != NULL && mmCount > 0) {
            srcJointTable = mmJoints;
            srcJointCount = mmCount;
        }
    }

    for (s32 i = 0; i < 24; i++) {
        if (i < srcJointCount && srcJointTable != NULL) {
            jointArray.push_back(srcJointTable[i].x);
            jointArray.push_back(srcJointTable[i].y);
            jointArray.push_back(srcJointTable[i].z);
        } else {
            jointArray.push_back(0);
            jointArray.push_back(0);
            jointArray.push_back(0);
        }
    }
    payload["jointTable"] = jointArray;
    payload["prevTransl"] = { { "x", player->skelAnime.prevTransl.x },
                              { "y", player->skelAnime.prevTransl.y },
                              { "z", player->skelAnime.prevTransl.z } };
    payload["movementFlags"] = player->skelAnime.movementFlags;
    payload["upperLimbRot"] = { { "x", player->upperLimbRot.x },
                                { "y", player->upperLimbRot.y },
                                { "z", player->upperLimbRot.z } };
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

    // Transformation data (read from TransformMasks system)
    // modelType: 0=human, 1=Goron, 2=Zora, 3=Deku, 4=FD — matches HarpoonDummyPlayer cache mapping
    payload["transformation"] = modelType;
    payload["cylRadius"] = player->cylinder.dim.radius;
    payload["cylHeight"] = player->cylinder.dim.height;
    payload["cylYShift"] = player->cylinder.dim.yShift;
    payload["mmStateFlags3"] = TransformMasks_GetMmStateFlags3();
    payload["mmSpeedXZ"] = TransformMasks_GetMmSpeedXZ();

    // OOT visual state
    payload["currentMask"] = player->currentMask;
    payload["wornMask"] = TransformMasks_WearGetCurrent();
    payload["face"] = player->actor.shape.face;
    payload["scaleX"] = player->actor.scale.x;
    payload["scaleY"] = player->actor.scale.y;
    payload["scaleZ"] = player->actor.scale.z;

    // MM form-specific visual data
    payload["goronAction"] = modelType > 0 ? TransformMasks_GetGoronAction() : 0;
    payload["eyeIndex"] = modelType > 0 ? TransformMasks_GetEyeIndex() : (u8)0;
    payload["rollSquash"] = modelType > 0 ? TransformMasks_GetRollSquash() : 0.0f;
    payload["rollSpikeActive"] = modelType > 0 ? TransformMasks_GetRollSpikeActive() : (s16)0;
    payload["rollChargeLevel"] = modelType > 0 ? TransformMasks_GetRollChargeLevel() : (s16)0;

    // Custom item visual state
    payload["ciFlags"] = gCustomItemState.spinnerActive ? CI_FLAG_SPINNER : 0;
    {
        u32 ciFlags = 0;
        CustomItemState* ci = &gCustomItemState;
        if (ci->spinnerActive)
            ciFlags |= CI_FLAG_SPINNER;
        if (ci->gustJarMode > 0)
            ciFlags |= CI_FLAG_GUSTJAR;
        if (ci->ballAndChainThrown)
            ciFlags |= CI_FLAG_BALLCHAIN;
        if (ci->shovelAnimating)
            ciFlags |= CI_FLAG_SHOVEL;
        if (ci->beetleActive)
            ciFlags |= CI_FLAG_BEETLE;
        if (ci->dominionRodActive)
            ciFlags |= CI_FLAG_DOMINION_ROD;
        if (ci->somariaActive)
            ciFlags |= CI_FLAG_SOMARIA;
        if (ci->mogmaMittsActive)
            ciFlags |= CI_FLAG_MOGMA_MITTS;
        if (ci->whipActive)
            ciFlags |= CI_FLAG_WHIP;
        if (ci->timeGateActive)
            ciFlags |= CI_FLAG_TIME_GATE;
        if (ci->switchHookActive)
            ciFlags |= CI_FLAG_SWITCH_HOOK;
        if (ci->dekuLeafGliding || ci->dekuLeafBlowing)
            ciFlags |= CI_FLAG_DEKU_LEAF;
        if (ci->fireRodActive)
            ciFlags |= CI_FLAG_FIRE_ROD;
        if (ci->iceRodActive)
            ciFlags |= CI_FLAG_ICE_ROD;
        if (ci->lightRodActive)
            ciFlags |= CI_FLAG_LIGHT_ROD;
        payload["ciFlags"] = ciFlags;

        if (ciFlags & CI_FLAG_BEETLE) {
            payload["ciBeetlePos"] = { ci->beetlePos.x, ci->beetlePos.y, ci->beetlePos.z };
            payload["ciBeetleRot"] = { ci->beetleRot.x, ci->beetleRot.y, ci->beetleRot.z };
            payload["ciBeetleWingScale"] = ci->beetleWingScale;
            payload["ciBeetleState"] = ci->beetleState;
        }
        if (ciFlags & CI_FLAG_GUSTJAR) {
            payload["ciGustJarProjPos"] = { ci->gustJarProjPos.x, ci->gustJarProjPos.y, ci->gustJarProjPos.z };
            payload["ciGustJarProjActive"] = ci->gustJarProjectileActive;
            payload["ciGustJarAmmoType"] = ci->gustJarAmmoType;
            payload["ciGustJarMode"] = ci->gustJarMode;
            payload["ciGustJarProjYaw"] = ci->gustJarProjYaw;
        }
        if (ciFlags & CI_FLAG_FIRE_ROD) {
            payload["ciFireRodProjActive"] = ci->fireRodProjActive;
            payload["ciFireRodProjCount"] = ci->fireRodProjCount;
            payload["ciFireRodProjType"] = ci->fireRodProjType;
            payload["ciFireRodProjScale"] = ci->fireRodProjScale;
            payload["ciFireRodProjPos"] = { ci->fireRodProjPos.x, ci->fireRodProjPos.y, ci->fireRodProjPos.z };
            payload["ciFireRodProjPos2"] = { ci->fireRodProjPos2.x, ci->fireRodProjPos2.y, ci->fireRodProjPos2.z };
            payload["ciFireRodProjPos3"] = { ci->fireRodProjPos3.x, ci->fireRodProjPos3.y, ci->fireRodProjPos3.z };
        }
        if (ciFlags & CI_FLAG_ICE_ROD) {
            payload["ciIceRodProjActive"] = ci->iceRodProjActive;
            payload["ciIceRodProjCount"] = ci->iceRodProjCount;
            payload["ciIceRodProjScale"] = ci->iceRodProjScale;
            payload["ciIceRodProjPos"] = { ci->iceRodProjPos.x, ci->iceRodProjPos.y, ci->iceRodProjPos.z };
            payload["ciIceRodProjPos2"] = { ci->iceRodProjPos2.x, ci->iceRodProjPos2.y, ci->iceRodProjPos2.z };
            payload["ciIceRodProjPos3"] = { ci->iceRodProjPos3.x, ci->iceRodProjPos3.y, ci->iceRodProjPos3.z };
        }
        if (ciFlags & CI_FLAG_LIGHT_ROD) {
            payload["ciLightRodProjActive"] = ci->lightRodProjActive;
            payload["ciLightRodProjCount"] = ci->lightRodProjCount;
            payload["ciLightRodProjPos"] = { ci->lightRodProjPos.x, ci->lightRodProjPos.y, ci->lightRodProjPos.z };
            payload["ciLightRodProjPos2"] = { ci->lightRodProjPos2.x, ci->lightRodProjPos2.y, ci->lightRodProjPos2.z };
            payload["ciLightRodProjPos3"] = { ci->lightRodProjPos3.x, ci->lightRodProjPos3.y, ci->lightRodProjPos3.z };
        }
        if (ciFlags & CI_FLAG_BALLCHAIN) {
            payload["ciBallChainThrown"] = ci->ballAndChainThrown;
            payload["ciTimer2"] = ci->timer2;
            payload["ciSharedProjPos"] = { ci->sharedProjectilePos.x, ci->sharedProjectilePos.y,
                                           ci->sharedProjectilePos.z };
        }
        if (ciFlags & CI_FLAG_WHIP) {
            payload["ciWhipState"] = ci->whipState;
            payload["ciWhipTipPos"] = { ci->whipTipPos.x, ci->whipTipPos.y, ci->whipTipPos.z };
            payload["ciWhipAttachPos"] = { ci->whipAttachPos.x, ci->whipAttachPos.y, ci->whipAttachPos.z };
            payload["ciWhipAttachNormal"] = { ci->whipAttachNormal.x, ci->whipAttachNormal.y, ci->whipAttachNormal.z };
        }
        if (ciFlags & CI_FLAG_DEKU_LEAF) {
            payload["ciDekuLeafGliding"] = ci->dekuLeafGliding;
            payload["ciDekuLeafBlowing"] = ci->dekuLeafBlowing;
            payload["ciDekuLeafAnimTimer"] = ci->dekuLeafAnimTimer;
        }
        if (ciFlags & CI_FLAG_SHOVEL) {
            payload["ciShovelAnimating"] = ci->shovelAnimating;
        }
        if (ciFlags & CI_FLAG_DOMINION_ROD) {
            payload["ciDominionRodState"] = ci->dominionRodState;
            payload["ciDominionRodOrbPos"] = { ci->dominionRodOrbPos.x, ci->dominionRodOrbPos.y,
                                               ci->dominionRodOrbPos.z };
        }
        if (ciFlags & CI_FLAG_SWITCH_HOOK) {
            payload["ciSwitchHookState"] = ci->switchHookState;
            payload["ciSwitchHookProjPos"] = { ci->switchHookProjPos.x, ci->switchHookProjPos.y,
                                               ci->switchHookProjPos.z };
        }
        if (ciFlags & CI_FLAG_TIME_GATE) {
            payload["ciTimeGateItemVisible"] = ci->timeGateItemVisible;
            payload["ciTimeGatePortalActive"] = ci->timeGatePortalActive;
            payload["ciTimeGatePortalAlpha"] = ci->timeGatePortalAlpha;
            payload["ciTimeGatePortalScale"] = ci->timeGatePortalScale;
        }
    }

    // Somaria cubes
    {
        nlohmann::json cubeArray = nlohmann::json::array();
        for (int i = 0; i < SOMARIA_MAX_CUBES; i++) {
            Actor* cube = gCustomItemState.somariaBlocks[i];
            if (cube != NULL && cube->update != NULL) {
                cubeArray.push_back({ { "p", { cube->world.pos.x, cube->world.pos.y, cube->world.pos.z } },
                                      { "s", SOMARIA_GET_STATE(cube) },
                                      { "c", SOMARIA_GET_FORM(cube) },
                                      { "sc", cube->scale.x },
                                      { "r", cube->shape.rot.y } });
            }
        }
        if (!cubeArray.empty()) {
            payload["somCubes"] = cubeArray;
        }
    }

    payload["quiet"] = true;

    for (auto& [clientId, client] : clients) {
        if (client.sceneNum == gPlayState->sceneNum && client.online && client.isSaveLoaded && !client.self) {
            payload["targetClientId"] = clientId;
            SendJsonToRemote(payload);
        }
    }
}

void Harpoon::SendPacket_Damage(u32 clientId, u8 damageEffect, u8 damage) {
    nlohmann::json payload;
    payload["type"] = HPN_DAMAGE;
    payload["targetClientId"] = clientId;
    payload["damageEffect"] = damageEffect;
    payload["damage"] = damage;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_PlayerDied() {
    nlohmann::json payload;
    payload["type"] = HPN_PLAYER_DIED;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_PlayerSfx(u16 sfxId) {
    nlohmann::json payload;
    payload["type"] = HPN_PLAYER_SFX;
    payload["sfxId"] = sfxId;
    payload["quiet"] = true;
    SendJsonToRemote(payload);
}

// MARK: - Handle Packets

void Harpoon::HandlePacket_AllClients(nlohmann::json payload) {
    if (payload.contains("ownClientId")) {
        ownClientId = payload["ownClientId"].get<uint32_t>();
    }
    if (payload.contains("clients")) {
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
        }

        shouldRefreshActors = true;
    }
}

void Harpoon::HandlePacket_PlayerUpdate(nlohmann::json payload) {
    uint32_t clientId = payload["clientId"].get<uint32_t>();

    if (!clients.contains(clientId))
        return;
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

    // OOT visual state
    client.currentMask = payload.value("currentMask", (u8)0);
    client.wornMask = payload.value("wornMask", (s32)ITEM_NONE);
    client.face = payload.value("face", (s16)0);
    client.scaleX = payload.value("scaleX", 0.01f);
    client.scaleY = payload.value("scaleY", 0.01f);
    client.scaleZ = payload.value("scaleZ", 0.01f);

    // MM form visual state
    client.goronAction = payload.value("goronAction", (s32)0);
    client.eyeIndex = payload.value("eyeIndex", (u8)0);
    client.rollSquash = payload.value("rollSquash", 0.0f);
    client.rollSpikeActive = payload.value("rollSpikeActive", (s16)0);
    client.rollChargeLevel = payload.value("rollChargeLevel", (s16)0);

    // Custom item visual state
    u32 ciFlags = payload.value("ciFlags", (u32)0);
    client.customItemFlags = ciFlags;

    if (ciFlags & CI_FLAG_BEETLE) {
        auto bp = payload.value("ciBeetlePos", std::vector<float>{ 0, 0, 0 });
        client.ciBeetlePos = { bp[0], bp[1], bp[2] };
        auto br = payload.value("ciBeetleRot", std::vector<int>{ 0, 0, 0 });
        client.ciBeetleRot = { (s16)br[0], (s16)br[1], (s16)br[2] };
        client.ciBeetleWingScale = payload.value("ciBeetleWingScale", 0.0f);
        client.ciBeetleState = payload.value("ciBeetleState", (u8)0);
    }
    if (ciFlags & CI_FLAG_GUSTJAR) {
        auto gp = payload.value("ciGustJarProjPos", std::vector<float>{ 0, 0, 0 });
        client.ciGustJarProjPos = { gp[0], gp[1], gp[2] };
        client.ciGustJarProjActive = payload.value("ciGustJarProjActive", (u8)0);
        client.ciGustJarAmmoType = payload.value("ciGustJarAmmoType", (u8)0);
        client.ciGustJarMode = payload.value("ciGustJarMode", (u8)0);
        client.ciGustJarProjYaw = payload.value("ciGustJarProjYaw", (s16)0);
    }
    if (ciFlags & CI_FLAG_FIRE_ROD) {
        client.ciFireRodProjActive = payload.value("ciFireRodProjActive", (u8)0);
        client.ciFireRodProjCount = payload.value("ciFireRodProjCount", (u8)0);
        client.ciFireRodProjType = payload.value("ciFireRodProjType", (u8)0);
        client.ciFireRodProjScale = payload.value("ciFireRodProjScale", 0.0f);
        auto fp1 = payload.value("ciFireRodProjPos", std::vector<float>{ 0, 0, 0 });
        client.ciFireRodProjPos = { fp1[0], fp1[1], fp1[2] };
        auto fp2 = payload.value("ciFireRodProjPos2", std::vector<float>{ 0, 0, 0 });
        client.ciFireRodProjPos2 = { fp2[0], fp2[1], fp2[2] };
        auto fp3 = payload.value("ciFireRodProjPos3", std::vector<float>{ 0, 0, 0 });
        client.ciFireRodProjPos3 = { fp3[0], fp3[1], fp3[2] };
    }
    if (ciFlags & CI_FLAG_ICE_ROD) {
        client.ciIceRodProjActive = payload.value("ciIceRodProjActive", (u8)0);
        client.ciIceRodProjCount = payload.value("ciIceRodProjCount", (u8)0);
        client.ciIceRodProjScale = payload.value("ciIceRodProjScale", 0.0f);
        auto ip1 = payload.value("ciIceRodProjPos", std::vector<float>{ 0, 0, 0 });
        client.ciIceRodProjPos = { ip1[0], ip1[1], ip1[2] };
        auto ip2 = payload.value("ciIceRodProjPos2", std::vector<float>{ 0, 0, 0 });
        client.ciIceRodProjPos2 = { ip2[0], ip2[1], ip2[2] };
        auto ip3 = payload.value("ciIceRodProjPos3", std::vector<float>{ 0, 0, 0 });
        client.ciIceRodProjPos3 = { ip3[0], ip3[1], ip3[2] };
    }
    if (ciFlags & CI_FLAG_LIGHT_ROD) {
        client.ciLightRodProjActive = payload.value("ciLightRodProjActive", (u8)0);
        client.ciLightRodProjCount = payload.value("ciLightRodProjCount", (u8)0);
        auto lp1 = payload.value("ciLightRodProjPos", std::vector<float>{ 0, 0, 0 });
        client.ciLightRodProjPos = { lp1[0], lp1[1], lp1[2] };
        auto lp2 = payload.value("ciLightRodProjPos2", std::vector<float>{ 0, 0, 0 });
        client.ciLightRodProjPos2 = { lp2[0], lp2[1], lp2[2] };
        auto lp3 = payload.value("ciLightRodProjPos3", std::vector<float>{ 0, 0, 0 });
        client.ciLightRodProjPos3 = { lp3[0], lp3[1], lp3[2] };
    }
    if (ciFlags & CI_FLAG_BALLCHAIN) {
        client.ciBallChainThrown = payload.value("ciBallChainThrown", (u8)0);
        client.ciTimer2 = payload.value("ciTimer2", (s16)0);
        auto sp = payload.value("ciSharedProjPos", std::vector<float>{ 0, 0, 0 });
        client.ciSharedProjPos = { sp[0], sp[1], sp[2] };
    }
    if (ciFlags & CI_FLAG_WHIP) {
        client.ciWhipState = payload.value("ciWhipState", (u8)0);
        auto wt = payload.value("ciWhipTipPos", std::vector<float>{ 0, 0, 0 });
        client.ciWhipTipPos = { wt[0], wt[1], wt[2] };
        auto wa = payload.value("ciWhipAttachPos", std::vector<float>{ 0, 0, 0 });
        client.ciWhipAttachPos = { wa[0], wa[1], wa[2] };
        auto wn = payload.value("ciWhipAttachNormal", std::vector<float>{ 0, 0, 0 });
        client.ciWhipAttachNormal = { wn[0], wn[1], wn[2] };
    }
    if (ciFlags & CI_FLAG_DEKU_LEAF) {
        client.ciDekuLeafGliding = payload.value("ciDekuLeafGliding", (u8)0);
        client.ciDekuLeafBlowing = payload.value("ciDekuLeafBlowing", (u8)0);
        client.ciDekuLeafAnimTimer = payload.value("ciDekuLeafAnimTimer", (s16)0);
    }
    if (ciFlags & CI_FLAG_SHOVEL) {
        client.ciShovelAnimating = payload.value("ciShovelAnimating", (u8)0);
    }
    if (ciFlags & CI_FLAG_DOMINION_ROD) {
        client.ciDominionRodState = payload.value("ciDominionRodState", (u8)0);
        auto dp = payload.value("ciDominionRodOrbPos", std::vector<float>{ 0, 0, 0 });
        client.ciDominionRodOrbPos = { dp[0], dp[1], dp[2] };
    }
    if (ciFlags & CI_FLAG_SWITCH_HOOK) {
        client.ciSwitchHookState = payload.value("ciSwitchHookState", (u8)0);
        auto shp = payload.value("ciSwitchHookProjPos", std::vector<float>{ 0, 0, 0 });
        client.ciSwitchHookProjPos = { shp[0], shp[1], shp[2] };
    }
    if (ciFlags & CI_FLAG_TIME_GATE) {
        client.ciTimeGateItemVisible = payload.value("ciTimeGateItemVisible", (u8)0);
        client.ciTimeGatePortalActive = payload.value("ciTimeGatePortalActive", (u8)0);
        client.ciTimeGatePortalAlpha = payload.value("ciTimeGatePortalAlpha", 0.0f);
        client.ciTimeGatePortalScale = payload.value("ciTimeGatePortalScale", 0.0f);
    }

    // Somaria cubes
    client.remoteCubeCount = 0;
    if (payload.contains("somCubes")) {
        auto& cubes = payload["somCubes"];
        for (size_t i = 0; i < cubes.size() && i < 3; i++) {
            auto& c = cubes[i];
            auto pos = c.value("p", std::vector<float>{ 0, 0, 0 });
            client.remoteCubes[i].pos = { pos[0], pos[1], pos[2] };
            client.remoteCubes[i].state = c.value("s", (u8)0);
            client.remoteCubes[i].form = c.value("c", (u8)0);
            client.remoteCubes[i].scale = c.value("sc", 0.0f);
            client.remoteCubes[i].rotY = c.value("r", (s16)0);
            client.remoteCubeCount++;
        }
    }
}

void Harpoon::HandlePacket_Damage(nlohmann::json payload) {
    if (!IsSaveLoaded())
        return;

    u8 damageEffect = payload.value("damageEffect", (u8)0);
    u8 damage = payload.value("damage", (u8)0);

    Player* self = GET_PLAYER(gPlayState);

    if (Player_InBlockingCsMode(gPlayState, self))
        return;
    if (self->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_IN_CUTSCENE))
        return;

    // PVP off: apply status effects only (stun/freeze), no damage or knockback
    if (!pvpEnabled) {
        if (damageEffect == HARPOON_HIT_RESPONSE_STUN) {
            self->actor.freezeTimer = 20;
            Actor_SetColorFilter(&self->actor, 0, 0xFF, 0, 24);
        }
        return;
    }

    self->actor.colChkInfo.damage = damage * 8;

    if (damageEffect == HARPOON_HIT_RESPONSE_STUN) {
        self->actor.freezeTimer = 20;
        Actor_SetColorFilter(&self->actor, 0, 0xFF, 0, 24);
    } else if (damageEffect == HARPOON_HIT_RESPONSE_FIRE) {
        self->actor.colChkInfo.damage = damage * 8;
    }

    uint32_t attackerClientId = payload.value("clientId", (uint32_t)0);
    if (clients.contains(attackerClientId) && clients[attackerClientId].player != nullptr) {
        Player* attacker = clients[attackerClientId].player;
        func_80837C0C(gPlayState, self, damageEffect, 4.0f, 5.0f,
                      Actor_WorldYawTowardActor(&attacker->actor, &self->actor), 20);
    }
}

void Harpoon::HandlePacket_PlayerDied(nlohmann::json payload) {
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

void Harpoon::HandlePacket_ServerMsg(nlohmann::json payload) {
    std::string msg = payload.value("message", "");
    if (!msg.empty()) {
        killFeed.push_back(msg);
        if (killFeed.size() > 5) {
            killFeed.erase(killFeed.begin());
        }
    }
}

void Harpoon::HandlePacket_PlayerSfx(nlohmann::json payload) {
    // TODO: Play remote player SFX at their position
}

// MARK: - Item Sync

void Harpoon::SendPacket_GiveItem(u16 modId, s16 getItemId) {
    if (!IsSaveLoaded() || isProcessingIncomingPacket || !syncItems) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = HPN_GIVE_ITEM;
    payload["modId"] = modId;
    payload["getItemId"] = getItemId;
    SendJsonToRemote(payload);
}

void Harpoon::HandlePacket_GiveItem(nlohmann::json payload) {
    if (!IsSaveLoaded() || !syncItems) {
        return;
    }

    uint32_t clientId = payload["clientId"].get<uint32_t>();
    std::string senderName = "Unknown";
    if (clients.contains(clientId)) {
        senderName = clients[clientId].name;
    }

    u16 modId = payload["modId"].get<u16>();
    u16 getItemId = payload["getItemId"].get<u16>();

    isProcessingIncomingPacket = true;

    // Custom items (range 0x9C-0xB5)
    if (modId == MOD_NONE && getItemId >= 0x9C && getItemId <= 0xB5) {
        ExtInv_SetItemById(getItemId);
        Audio_PlayFanfare(NA_BGM_ITEM_GET | 0x900);
        Notification::Emit({
            .prefix = senderName,
            .message = "found",
            .suffix = SohUtils::GetItemName(getItemId),
        });
        isProcessingIncomingPacket = false;
        return;
    }

    GetItemEntry getItemEntry;
    if (modId == MOD_NONE) {
        getItemEntry = ItemTableManager::Instance->RetrieveItemEntry(MOD_NONE, getItemId);
    } else {
        getItemEntry = Rando::StaticData::RetrieveItem(static_cast<RandomizerGet>(getItemId)).GetGIEntry_Copy();
    }

    if (getItemEntry.modIndex == MOD_NONE) {
        if (getItemEntry.getItemId == GI_SWORD_BGS) {
            gSaveContext.bgsFlag = true;
        }
        Item_Give(gPlayState, getItemEntry.itemId);
    } else if (getItemEntry.modIndex == MOD_RANDOMIZER) {
        if (getItemEntry.getItemId == RG_ICE_TRAP) {
            gSaveContext.ship.pendingIceTrapCount++;
        } else {
            Randomizer_Item_Give(gPlayState, getItemEntry);
        }
    }

    // Full heal if getting a heart container or piece
    if (getItemEntry.gid == GID_HEART_CONTAINER || getItemEntry.gid == GID_HEART_PIECE) {
        gSaveContext.healthAccumulator = 0x140;
    }

    // Handle 4th heart piece
    s32 heartPieces = (s32)(gSaveContext.inventory.questItems & 0xF0000000) >> (QUEST_HEART_PIECE + 4);
    if (heartPieces >= 4) {
        gSaveContext.inventory.questItems &= ~0xF0000000;
        gSaveContext.inventory.questItems += (heartPieces % 4) << (QUEST_HEART_PIECE + 4);
        gSaveContext.healthCapacity += 0x10 * (heartPieces / 4);
        gSaveContext.health += 0x10 * (heartPieces / 4);
    }

    if (getItemEntry.getItemCategory != ITEM_CATEGORY_JUNK) {
        if (getItemEntry.modIndex == MOD_NONE) {
            Notification::Emit({
                .itemIcon = GetTextureForItemId(getItemEntry.itemId),
                .prefix = senderName,
                .message = "found",
                .suffix = SohUtils::GetItemName(getItemEntry.itemId),
            });
        } else if (getItemEntry.modIndex == MOD_RANDOMIZER) {
            Notification::Emit({
                .prefix = senderName,
                .message = "found",
                .suffix = Rando::StaticData::RetrieveItem((RandomizerGet)getItemEntry.getItemId).GetName().english,
            });
        }
    }

    isProcessingIncomingPacket = false;
}

void Harpoon::SendPacket_UpdateTeamState() {
    if (!IsSaveLoaded() || !syncItems) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = HPN_UPDATE_TEAM_STATE;
    payload["state"] = gSaveContext;

    // Manually update current scene flags
    payload["state"]["sceneFlags"][gPlayState->sceneNum * 4] = gPlayState->actorCtx.flags.chest;
    payload["state"]["sceneFlags"][gPlayState->sceneNum * 4 + 1] = gPlayState->actorCtx.flags.swch;
    payload["state"]["sceneFlags"][gPlayState->sceneNum * 4 + 2] = gPlayState->actorCtx.flags.clear;
    payload["state"]["sceneFlags"][gPlayState->sceneNum * 4 + 3] = gPlayState->actorCtx.flags.collect;

    if (IS_RANDO) {
        auto randoContext = Rando::Context::GetInstance();
        payload["state"]["rando"] = nlohmann::json::object();
        payload["state"]["rando"]["itemLocations"] = nlohmann::json::array();
        for (int i = 0; i < RC_MAX; i++) {
            payload["state"]["rando"]["itemLocations"][i] = nlohmann::json::array();
            payload["state"]["rando"]["itemLocations"][i][0] = randoContext->GetItemLocation(i)->GetCheckStatus();
            payload["state"]["rando"]["itemLocations"][i][1] = (u8)randoContext->GetItemLocation(i)->GetIsSkipped();
        }
    }

    SendJsonToRemote(payload);
}

void Harpoon::HandlePacket_UpdateTeamState(nlohmann::json payload) {
    if (!syncItems) {
        return;
    }

    isProcessingIncomingPacket = true;

    if (payload.contains("state")) {
        SaveContext loadedData = payload["state"].get<SaveContext>();

        gSaveContext.healthCapacity = loadedData.healthCapacity;
        gSaveContext.magicLevel = loadedData.magicLevel;
        gSaveContext.magicCapacity = gSaveContext.magic = loadedData.magicCapacity;
        gSaveContext.isMagicAcquired = loadedData.isMagicAcquired;
        gSaveContext.isDoubleMagicAcquired = loadedData.isDoubleMagicAcquired;
        gSaveContext.isDoubleDefenseAcquired = loadedData.isDoubleDefenseAcquired;
        gSaveContext.bgsFlag = loadedData.bgsFlag;
        gSaveContext.swordHealth = loadedData.swordHealth;
        gSaveContext.ship.quest = loadedData.ship.quest;

        for (int i = 0; i < 124; i++) {
            if (i == SCENE_WATER_TEMPLE) {
                u32 mask = (1 << 0x1C) | (1 << 0x1D) | (1 << 0x1E);
                loadedData.sceneFlags[i].swch =
                    (loadedData.sceneFlags[i].swch & ~mask) | (gSaveContext.sceneFlags[i].swch & mask);
            }
            if (i == SCENE_FOREST_TEMPLE) {
                u32 mask = (1 << 0x1B);
                loadedData.sceneFlags[i].swch =
                    (loadedData.sceneFlags[i].swch & ~mask) | (gSaveContext.sceneFlags[i].swch & mask);
            }
            gSaveContext.sceneFlags[i] = loadedData.sceneFlags[i];
            if (IsSaveLoaded() && gPlayState->sceneNum == i) {
                gPlayState->actorCtx.flags.chest = loadedData.sceneFlags[i].chest;
                gPlayState->actorCtx.flags.swch = loadedData.sceneFlags[i].swch;
                gPlayState->actorCtx.flags.clear = loadedData.sceneFlags[i].clear;
                gPlayState->actorCtx.flags.collect = loadedData.sceneFlags[i].collect;
            }
        }

        for (int i = 0; i < 14; i++) {
            gSaveContext.eventChkInf[i] = loadedData.eventChkInf[i];
        }
        for (int i = 0; i < 4; i++) {
            gSaveContext.itemGetInf[i] = loadedData.itemGetInf[i];
        }
        // Skip last row of infTable, don't want to sync swordless flag
        for (int i = 0; i < 29; i++) {
            gSaveContext.infTable[i] = loadedData.infTable[i];
        }
        for (int i = 0; i < ceil((RAND_INF_MAX + 15) / 16); i++) {
            gSaveContext.ship.randomizerInf[i] = loadedData.ship.randomizerInf[i];
        }
        for (int i = 0; i < 6; i++) {
            gSaveContext.gsFlags[i] = loadedData.gsFlags[i];
        }

        // Restore bottle contents (unless it's ruto's letter)
        for (int i = 0; i < 4; i++) {
            if (gSaveContext.inventory.items[SLOT_BOTTLE_1 + i] != ITEM_NONE &&
                gSaveContext.inventory.items[SLOT_BOTTLE_1 + i] != ITEM_LETTER_RUTO) {
                loadedData.inventory.items[SLOT_BOTTLE_1 + i] = gSaveContext.inventory.items[SLOT_BOTTLE_1 + i];
            }
        }
        // Restore ammo if it's non-zero, unless it's beans
        for (int i = 0; i < ARRAY_COUNT(gSaveContext.inventory.ammo); i++) {
            if (gSaveContext.inventory.ammo[i] != 0 && i != SLOT(ITEM_BEAN) && i != SLOT(ITEM_BEAN + 1)) {
                loadedData.inventory.ammo[i] = gSaveContext.inventory.ammo[i];
            }
        }

        gSaveContext.inventory = loadedData.inventory;

        if (IS_RANDO && payload["state"].contains("rando")) {
            auto randoContext = Rando::Context::GetInstance();
            for (int i = 0; i < RC_MAX; i++) {
                OTRGlobals::Instance->gRandoContext->GetItemLocation(i)->SetCheckStatus(
                    payload["state"]["rando"]["itemLocations"][i][0].get<RandomizerCheckStatus>());
                OTRGlobals::Instance->gRandoContext->GetItemLocation(i)->SetIsSkipped(
                    payload["state"]["rando"]["itemLocations"][i][1].get<u8>());
            }
        }

        Notification::Emit({
            .message = "Save updated from teammate",
        });
    }

    isProcessingIncomingPacket = false;
}

// MARK: - Scooter Handlers (Custom Damage, Effects, Game State, Rooms)

void Harpoon::HandlePacket_CustomDamage(nlohmann::json payload) {
    if (!IsSaveLoaded())
        return;
    Player* self = GET_PLAYER(gPlayState);
    if (Player_InBlockingCsMode(gPlayState, self))
        return;
    if (self->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_IN_CUTSCENE))
        return;

    s32 customType = payload.value("customDamageType", (s32)0);
    s32 damage = payload.value("damage", (s32)1);
    f32 attackerX = payload.value("attackerX", 0.0f);
    f32 attackerY = payload.value("attackerY", 0.0f);
    f32 attackerZ = payload.value("attackerZ", 0.0f);

    Vec3f attackerPos = { attackerX, attackerY, attackerZ };
    s16 yawToAttacker = Math_Vec3f_Yaw(&attackerPos, &self->actor.world.pos);

    // PVP off: apply status effects only (freeze/stun/color), no damage or knockback
    if (!pvpEnabled) {
        switch (customType) {
            case HARPOON_CUSTOM_DMG_ICE:
                self->actor.freezeTimer = 40;
                Actor_SetColorFilter(&self->actor, 0x4000, 0xFF, 0, 40);
                break;
            case HARPOON_CUSTOM_DMG_ELECTRIC:
                Actor_SetColorFilter(&self->actor, 0x8000, 0xFF, 0, 30);
                break;
            case HARPOON_CUSTOM_DMG_AOE_STUN:
                self->actor.freezeTimer = 30;
                Actor_SetColorFilter(&self->actor, 0, 0xFF, 0, 30);
                break;
            case HARPOON_CUSTOM_DMG_BOOMERANG:
                self->actor.freezeTimer = 20;
                Actor_SetColorFilter(&self->actor, 0, 0xFF, 0, 24);
                break;
            case HARPOON_CUSTOM_DMG_ZORA_FINS:
                self->actor.freezeTimer = 15;
                Actor_SetColorFilter(&self->actor, 0, 0xFF, 0, 18);
                break;
            case HARPOON_CUSTOM_DMG_FIRE:
                Actor_SetColorFilter(&self->actor, 0x4000, 0xFF, 0, 20);
                break;
            default:
                break;
        }
        return;
    }

    self->actor.colChkInfo.damage = damage * 8;

    switch (customType) {
        case HARPOON_CUSTOM_DMG_FIRE:
            func_80837C0C(gPlayState, self, HARPOON_HIT_RESPONSE_FIRE, 4.0f, 5.0f, yawToAttacker, 20);
            break;
        case HARPOON_CUSTOM_DMG_ICE:
            self->actor.freezeTimer = 40;
            Actor_SetColorFilter(&self->actor, 0x4000, 0xFF, 0, 40);
            func_80837C0C(gPlayState, self, HARPOON_HIT_RESPONSE_NORMAL, 2.0f, 3.0f, yawToAttacker, 20);
            break;
        case HARPOON_CUSTOM_DMG_ELECTRIC:
            func_80837C0C(gPlayState, self, PLAYER_HIT_RESPONSE_ELECTRIC_SHOCK, 3.0f, 4.0f, yawToAttacker, 20);
            break;
        case HARPOON_CUSTOM_DMG_HEAVY:
            func_80837C0C(gPlayState, self, HARPOON_HIT_RESPONSE_NORMAL, 24.0f, 12.0f, yawToAttacker, 20);
            break;
        case HARPOON_CUSTOM_DMG_BOMB:
            func_80837C0C(gPlayState, self, HARPOON_HIT_RESPONSE_NORMAL, 8.0f, 8.0f, yawToAttacker, 20);
            break;
        case HARPOON_CUSTOM_DMG_AOE_STUN:
            self->actor.freezeTimer = 30;
            Actor_SetColorFilter(&self->actor, 0, 0xFF, 0, 30);
            func_80837C0C(gPlayState, self, HARPOON_HIT_RESPONSE_STUN, 2.0f, 3.0f, yawToAttacker, 20);
            break;
        case HARPOON_CUSTOM_DMG_LAUNCH:
            self->actor.velocity.y = 18.0f;
            func_80837C0C(gPlayState, self, HARPOON_HIT_RESPONSE_NORMAL, 6.0f, 8.0f, yawToAttacker, 20);
            break;
        case HARPOON_CUSTOM_DMG_NORMAL:
            func_80837C0C(gPlayState, self, HARPOON_HIT_RESPONSE_NORMAL, 4.0f, 5.0f, yawToAttacker, 20);
            break;
        case HARPOON_CUSTOM_DMG_BOOMERANG:
            self->actor.freezeTimer = 20;
            Actor_SetColorFilter(&self->actor, 0, 0xFF, 0, 24);
            func_80837C0C(gPlayState, self, HARPOON_HIT_RESPONSE_STUN, 2.0f, 3.0f, yawToAttacker, 20);
            break;
        case HARPOON_CUSTOM_DMG_GORON_ROLL:
            func_80837C0C(gPlayState, self, HARPOON_HIT_RESPONSE_NORMAL, 16.0f, 10.0f, yawToAttacker, 20);
            break;
        case HARPOON_CUSTOM_DMG_GORON_PUNCH:
            func_80837C0C(gPlayState, self, HARPOON_HIT_RESPONSE_NORMAL, 6.0f, 6.0f, yawToAttacker, 20);
            break;
        case HARPOON_CUSTOM_DMG_ZORA_FINS:
            self->actor.freezeTimer = 15;
            Actor_SetColorFilter(&self->actor, 0, 0xFF, 0, 18);
            func_80837C0C(gPlayState, self, HARPOON_HIT_RESPONSE_NORMAL, 4.0f, 5.0f, yawToAttacker, 20);
            break;
        case HARPOON_CUSTOM_DMG_FD_BEAM:
            func_80837C0C(gPlayState, self, HARPOON_HIT_RESPONSE_NORMAL, 8.0f, 6.0f, yawToAttacker, 20);
            break;
        default:
            func_80837C0C(gPlayState, self, HARPOON_HIT_RESPONSE_NORMAL, 4.0f, 5.0f, yawToAttacker, 20);
            break;
    }
}

void Harpoon::HandlePacket_CustomEffect(nlohmann::json payload) {
    if (!IsSaveLoaded())
        return;
    Player* self = GET_PLAYER(gPlayState);

    s32 effectType = payload.value("effectType", (s32)0);
    f32 attackerX = payload.value("attackerX", 0.0f);
    f32 attackerY = payload.value("attackerY", 0.0f);
    f32 attackerZ = payload.value("attackerZ", 0.0f);

    switch (effectType) {
        case HARPOON_CUSTOM_EFFECT_PULL: {
            f32 dx = attackerX - self->actor.world.pos.x;
            f32 dz = attackerZ - self->actor.world.pos.z;
            f32 dist = sqrtf(dx * dx + dz * dz);
            if (dist > 1.0f) {
                f32 speed = 15.0f;
                self->actor.velocity.x = (dx / dist) * speed;
                self->actor.velocity.z = (dz / dist) * speed;
                self->actor.velocity.y = 5.0f;
            }
            self->actor.freezeTimer = 10;
            break;
        }
        case HARPOON_CUSTOM_EFFECT_SWAP: {
            Vec3f myPos = self->actor.world.pos;
            self->actor.world.pos.x = attackerX;
            self->actor.world.pos.y = attackerY;
            self->actor.world.pos.z = attackerZ;
            Vec3f zeroVec = { 0.0f, 0.0f, 0.0f };
            EffectSsDeadDb_Spawn(gPlayState, &myPos, &zeroVec, &zeroVec, 100, 10, 255, 255, 255, 200, 150, 200, 255, 0,
                                 14, 1);
            EffectSsDeadDb_Spawn(gPlayState, &self->actor.world.pos, &zeroVec, &zeroVec, 100, 10, 255, 255, 255, 200,
                                 150, 200, 255, 0, 14, 1);
            Audio_PlayActorSound2(&self->actor, NA_SE_EV_LINK_WARP);
            break;
        }
        case HARPOON_CUSTOM_EFFECT_PUPPET: {
            self->actor.freezeTimer = 60;
            Actor_SetColorFilter(&self->actor, 0x8000, 0xFF, 0, 60);
            break;
        }
    }
}

void Harpoon::HandlePacket_GameState(nlohmann::json payload) {
    std::string state = payload.value("state", "lobby");

    if (state == "lobby")
        gameState = HARPOON_STATE_LOBBY;
    else if (state == "map_select") {
        gameState = HARPOON_STATE_MAP_SELECT;
        for (auto& [cid, c] : clients) {
            c.hasVoted = false;
        }
    } else if (state == "map_select_confirmed") {
        selectedMapIndex = payload.value("mapIndex", (s32)0);
        gameState = HARPOON_STATE_COUNTDOWN;
    } else if (state == "countdown")
        gameState = HARPOON_STATE_COUNTDOWN;
    else if (state == "hiding_phase")
        gameState = HARPOON_STATE_HIDING_PHASE;
    else if (state == "playing")
        gameState = HARPOON_STATE_PLAYING;
    else if (state == "finished")
        gameState = HARPOON_STATE_FINISHED;

    countdownTimer = payload.value("timer", (s32)0);
    aliveCount = payload.value("aliveCount", (s32)0);

    if (payload.contains("mapSelectMode")) {
        mapSelectMode = (HarpoonMapSelectMode)payload.value("mapSelectMode", 0);
    }

    if (payload.contains("seekerCountdown")) {
        seekerCountdownSeconds = payload.value("seekerCountdown", (s32)0);
    }

    if (gameState == HARPOON_STATE_PLAYING) {
        isEliminated = false;
    }

    // Set roles for all clients if provided
    if (payload.contains("clientRoles")) {
        auto& roles = payload["clientRoles"];
        for (auto& [key, val] : roles.items()) {
            uint32_t cid = std::stoul(key);
            if (clients.contains(cid)) {
                clients[cid].role = val.get<std::string>();
            }
        }
    }

    if (gameState == HARPOON_STATE_LOBBY) {
        for (auto& [cid, c] : clients) {
            c.role = "";
            c.propCategory = 0;
            c.propIndex = -1;
            c.propState = 0;
        }
    }
}

void Harpoon::HandlePacket_Winner(nlohmann::json payload) {
    gameState = HARPOON_STATE_FINISHED;
    std::string winnerName = payload.value("name", "Unknown");
    s16 winnerKills = payload.value("kills", (s16)0);
    std::string msg = winnerName + " wins with " + std::to_string(winnerKills) + " kills!";
    killFeed.push_back(msg);
}

void Harpoon::HandlePacket_ServerInfo(nlohmann::json payload) {
    if (payload.contains("ownClientId")) {
        ownClientId = payload["ownClientId"].get<uint32_t>();
    }
}

void Harpoon::HandlePacket_RoomJoined(nlohmann::json payload) {
    currentRoomId = payload.value("roomId", std::string(""));
    currentRoomName = payload.value("roomName", std::string(""));
    currentRoomGameMode = payload.value("gameMode", std::string(""));
    gameState = HARPOON_STATE_LOBBY;

    if (currentRoomGameMode == "randomizer") {
        activeGameMode = HARPOON_MODE_RANDOMIZER;
    } else if (currentRoomGameMode == "hunger_games") {
        activeGameMode = HARPOON_MODE_HUNGER_GAMES;
    }

    SPDLOG_INFO("[Harpoon] Joined room '{}' ({}) mode={}", currentRoomName, currentRoomId, currentRoomGameMode);
}

void Harpoon::HandlePacket_RoomLeft(nlohmann::json payload) {
    currentRoomId.clear();
    currentRoomName.clear();
    currentRoomGameMode.clear();
    activeGameMode = HARPOON_MODE_NONE;
    clients.clear();
    gameState = HARPOON_STATE_LOBBY;
    killFeed.clear();
    isEliminated = false;
    RefreshClientActors();
    SPDLOG_INFO("[Harpoon] Left room");
}

void Harpoon::HandlePacket_RoomList(nlohmann::json payload) {
    roomList.clear();
    if (payload.contains("rooms")) {
        for (auto& roomJson : payload["rooms"]) {
            RoomInfo info;
            info.roomId = roomJson.value("roomId", std::string(""));
            info.name = roomJson.value("name", std::string(""));
            info.gameMode = roomJson.value("gameMode", std::string(""));
            info.hasPassword = roomJson.value("hasPassword", false);
            info.playerCount = roomJson.value("playerCount", 0);
            info.maxPlayers = roomJson.value("maxPlayers", 16);
            info.state = roomJson.value("state", std::string("lobby"));
            roomList.push_back(info);
        }
    }
}

void Harpoon::HandlePacket_RoleChange(nlohmann::json payload) {
    uint32_t clientId = payload.value("clientId", (uint32_t)0);
    std::string newRole = payload.value("newRole", std::string(""));
    std::string message = payload.value("message", std::string(""));

    if (clients.contains(clientId)) {
        clients[clientId].role = newRole;
    }

    if (!message.empty()) {
        killFeed.push_back(message);
        if (killFeed.size() > 5) {
            killFeed.erase(killFeed.begin());
        }
    }
}

void Harpoon::HandlePacket_MapVote(nlohmann::json payload) {
    if (payload.contains("votedClients")) {
        auto& voted = payload["votedClients"];
        for (auto& cid : voted) {
            uint32_t id = cid.get<uint32_t>();
            if (clients.contains(id)) {
                clients[id].hasVoted = true;
            }
        }
    }
}

void Harpoon::HandlePacket_DecoyHit(nlohmann::json payload) {
    u8 slot = payload.value("decoySlot", (u8)0xFF);
    if (slot >= 3)
        return;

    CustomItemState* ci = &gCustomItemState;
    if (!ci->somariaBlocks[slot])
        return;

    if (gPlayState != nullptr) {
        Vec3f pos = ci->somariaBlocks[slot]->world.pos;
        Vec3f zeroVec = { 0.0f, 0.0f, 0.0f };
        EffectSsDeadDb_Spawn(gPlayState, &pos, &zeroVec, &zeroVec, 100, 10, 150, 200, 255, 200, 100, 150, 255, 0, 14,
                             1);
    }
}

// MARK: - Room/Team Send Packets (from Scooter)

void Harpoon::SendPacket_ChestOpened(s16 sceneNum, s16 flag) {
    nlohmann::json payload;
    payload["type"] = HPN_CHEST_OPENED;
    payload["sceneNum"] = sceneNum;
    payload["flag"] = flag;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_Ready() {
    nlohmann::json payload;
    payload["type"] = HPN_READY;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_StartGame(const char* gameMode) {
    nlohmann::json payload;
    payload["type"] = HPN_START_GAME;
    payload["gameMode"] = gameMode;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_RoomCreate(const char* name, const char* gameMode, const char* password) {
    nlohmann::json payload;
    payload["type"] = "ROOM_CREATE";
    payload["name"] = name;
    payload["gameMode"] = gameMode;
    if (password && password[0] != '\0') {
        payload["password"] = password;
    }
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_RoomJoin(const char* roomId, const char* password) {
    nlohmann::json payload;
    payload["type"] = "ROOM_JOIN";
    payload["roomId"] = roomId;
    if (password && password[0] != '\0') {
        payload["password"] = password;
    }
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_RoomLeave() {
    nlohmann::json payload;
    payload["type"] = "ROOM_LEAVE";
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_RoomList() {
    nlohmann::json payload;
    payload["type"] = "ROOM_LIST";
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_SetTeam(const char* team) {
    nlohmann::json payload;
    payload["type"] = "PVP_SET_TEAM";
    payload["team"] = team;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_MapConfirm(s32 mapIndex) {
    nlohmann::json payload;
    payload["type"] = HPN_MAP_CONFIRM;
    payload["mapIndex"] = mapIndex;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_RoleChange(const char* newRole) {
    nlohmann::json payload;
    payload["type"] = HPN_ROLE_CHANGE;
    payload["newRole"] = newRole;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_MapVote(s32 mapIndex) {
    nlohmann::json payload;
    payload["type"] = HPN_MAP_VOTE;
    payload["mapIndex"] = mapIndex;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_DecoyHit(u32 targetClientId, u8 decoySlot) {
    nlohmann::json payload;
    payload["type"] = HPN_DECOY_HIT;
    payload["targetClientId"] = targetClientId;
    payload["decoySlot"] = decoySlot;
    SendJsonToRemote(payload);
}

void Harpoon::UpdateDecoys() {
    // Placeholder — prop hunt decoy sparkle/hit detection will be added when prop hunt is fully ported
}

// MARK: - HarpoonBridge C-callable implementations

extern "C" {

s32 Harpoon_IsDummyPlayer(Actor* actor) {
    if (actor == NULL)
        return 0;
    if (actor->id != ACTOR_EN_OE2)
        return 0;
    if (actor->update != HarpoonDummyPlayer_Update)
        return 0;
    return 1;
}

s32 Harpoon_IsPvpActive(void) {
    if (!Harpoon::Instance)
        return 0;
    if (!Harpoon::Instance->isConnected)
        return 0;
    HarpoonGameState state = Harpoon::Instance->gameState;
    return (state != HARPOON_STATE_LOBBY && state != HARPOON_STATE_COUNTDOWN && state != HARPOON_STATE_DISCONNECTED);
}

void Harpoon_SendCustomDamage(Actor* hitActor, s32 damageType, s32 damage) {
    if (!Harpoon::Instance)
        return;
    if (!Harpoon_IsPvpActive())
        return;
    if (!Harpoon_IsDummyPlayer(hitActor))
        return;

    uint32_t clientId = Harpoon::Instance->GetDummyPlayerClientId(hitActor);
    if (clientId == 0)
        return;

    Player* localPlayer = GET_PLAYER(gPlayState);

    nlohmann::json payload;
    payload["type"] = "PVP_CUSTOM_DAMAGE";
    payload["targetClientId"] = clientId;
    payload["customDamageType"] = damageType;
    payload["damage"] = damage;
    payload["attackerX"] = localPlayer->actor.world.pos.x;
    payload["attackerY"] = localPlayer->actor.world.pos.y;
    payload["attackerZ"] = localPlayer->actor.world.pos.z;
    payload["attackerYaw"] = localPlayer->actor.shape.rot.y;
    Harpoon::Instance->SendJsonToRemote(payload);

    Player* dummyPlayer = (Player*)hitActor;
    dummyPlayer->invincibilityTimer = 20;
}

void Harpoon_SendCustomEffect(Actor* hitActor, s32 effectType, Vec3f* attackerPos, s16 attackerYaw) {
    if (!Harpoon::Instance)
        return;
    if (!Harpoon_IsPvpActive())
        return;
    if (!Harpoon_IsDummyPlayer(hitActor))
        return;

    uint32_t clientId = Harpoon::Instance->GetDummyPlayerClientId(hitActor);
    if (clientId == 0)
        return;

    nlohmann::json payload;
    payload["type"] = "PVP_CUSTOM_EFFECT";
    payload["targetClientId"] = clientId;
    payload["effectType"] = effectType;
    payload["attackerX"] = attackerPos->x;
    payload["attackerY"] = attackerPos->y;
    payload["attackerZ"] = attackerPos->z;
    payload["attackerYaw"] = attackerYaw;
    Harpoon::Instance->SendJsonToRemote(payload);
}

s32 Harpoon_CheckAndSendDamage(ColliderCylinder* col, s32 damageType, s32 damage) {
    if (!(col->base.atFlags & AT_HIT))
        return 0;

    Actor* hitActor = col->base.at;
    if (hitActor == NULL)
        return 0;
    if (!Harpoon_IsDummyPlayer(hitActor))
        return 0;

    Harpoon_SendCustomDamage(hitActor, damageType, damage);
    col->base.atFlags &= ~AT_HIT;
    return 1;
}

} // extern "C"
