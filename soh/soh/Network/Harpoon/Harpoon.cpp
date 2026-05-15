#include "Harpoon.h"
#include "HarpoonBridge.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include <fstream>
#include <optional>
#include <set>
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
#include "mods/pak_loader/pak_loader.h"
#include "soh/Network/Harpoon/HarpoonBridge.h"
extern PlayState* gPlayState;
}
#include "soh/Enhancements/mod_menu.h"
#include "soh/Network/Harpoon/HarpoonSkinSync.h"
#include "soh/Network/Harpoon/PropHunt/PropHunt.h"
#include "soh/Network/Harpoon/TriforceThief/TriforceThief.h"
#include "soh/Network/Harpoon/DroppedItems.h"
#include "soh/Network/Harpoon/Templates.h"
#include "soh/Network/Harpoon/HarpoonGamemodeHud.h"

// File-scope extern for the global authorized-transition flag (defined in
// PropHunt.cpp). MSVC mangles function-scope `extern` declarations as
// namespace-qualified, so a function-local `extern bool ...` would fail
// to link. Declaring it at file scope here keeps the symbol resolution
// at the global namespace.
extern bool sHarpoonAuthorizedTransition;

// MARK: - Overrides

void Harpoon::Enable() {
    // Auto-switch: disconnect normal Anchor if active
    if (Anchor::Instance && Anchor::Instance->isConnected) {
        Anchor::Instance->Disable();
    }

    if (isEnabled) return;

    // Harpoon talks WebSocket (RFC 6455 plain ws://) — see HarpoonWebSocket.
    // We do NOT call Network::Enable() because that opens raw TCP+\0 which is
    // what the OTHER remotes (Anchor / Sail / CrowdControl) use. Network's
    // base class is preserved unchanged for them.
    if (!ws) {
        ws = std::make_unique<HarpoonWebSocket>();
        ws->SetOnConnected([this]() {
            isConnected = true;
            OnConnected();
        });
        ws->SetOnDisconnected([this]() {
            bool wasConnected = isConnected;
            isConnected = false;
            if (wasConnected) OnDisconnected();
        });
        ws->SetOnText([this](const std::string& text) {
            try {
                auto j = nlohmann::json::parse(text);
                OnIncomingJson(j);
            } catch (const std::exception& e) {
                SPDLOG_ERROR("[Harpoon] failed to parse incoming WS text: {}", e.what());
            }
        });
    }

    isEnabled = true;
    ownClientId = 0;
    sessionToken.clear();
    nextSeq = 1;

    // Best-effort: pre-load the Prop Hunt + Triforce Thief gamemode packs at
    // connect time so the data is ready by the time a room with that gamemode
    // is joined. Failure here is non-fatal — the pack simply won't be
    // advertised in installed_gamemodes.
    HarpoonPropHunt::Init();
    HarpoonTemplates::LoadAll();
    HarpoonTriforceThief::Init();
    HarpoonHud::Register();
    HarpoonPropHunt::RegisterMapSelectWindow();
    HarpoonTriforceThief::RegisterMapSelectWindow();

    std::string host = CVarGetString(CVAR_HARPOON("Host"), "localhost");
    int port = CVarGetInteger(CVAR_HARPOON("Port"), 8765);
    ws->Connect(host, (uint16_t)port);
}

void Harpoon::Disable() {
    if (ws) {
        ws->Disconnect();
    }
    isEnabled = false;
    isConnected = false;

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
    // Lazy-init the skin sync registry the first time we connect. The call
    // is idempotent — subsequent reconnects skip the heavy load.
    HarpoonSkinSync::InitO2rOverrides();
    // v2: handshake is just identity. Other state goes via separate primitives.
    SendPacket_Handshake();
    if (IsSaveLoaded()) {
        SendPacket_PlayerVisualState();
    }
    HarpoonSkinSync::Reset();
    RegisterHooks();
}

void Harpoon::OnDisconnected() {
    HarpoonSkinSync::Reset();
    RegisterHooks();
}

void Harpoon::SendJsonToRemote(nlohmann::json payload) {
    if (!isConnected || !ws) {
        return;
    }

    // Harpoon v2 envelope wrap: {type, seq, payload}.
    // Existing call sites pass a payload that already has `type` set inside
    // it; we extract it, drop it from the inner payload, and place it on the
    // envelope. Any other fields go into the inner `payload`.
    std::string type = payload.value("type", std::string(""));
    payload.erase("type");
    payload["clientId"] = ownClientId;  // kept inside payload as a convenience

    nlohmann::json envelope;
    envelope["type"] = type;
    envelope["seq"] = nextSeq++;
    envelope["payload"] = payload;

    // HarpoonWebSocket::SendText is thread-safe — send directly. The legacy
    // outgoingPacketQueue + ProcessOutgoingPackets path was tied to
    // Network::Run()'s SDL_net loop; Harpoon doesn't run that loop (uses its
    // own WS thread), so anything we enqueued there sat forever.
    ws->SendText(envelope.dump());
}

void Harpoon::OnIncomingJson(nlohmann::json envelope) {
    if (!envelope.contains("type")) {
        return;
    }

    // Harpoon v2 envelope unwrap: incoming is {type, seq, payload}. We flatten
    // to {type, ...inner...} so existing HandlePacket_* code keeps working.
    nlohmann::json payload;
    if (envelope.contains("payload") && envelope["payload"].is_object()) {
        payload = envelope["payload"];
    }
    payload["type"] = envelope["type"];

    if (!payload.contains("quiet")) {
        SPDLOG_DEBUG("[Harpoon] Received envelope:\n{}", envelope.dump());
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
            // ================================================================
            // HARPOON.* — connection lifecycle
            // ================================================================
            if (packetType == HPN_SERVER_INFO)               HandlePacket_ServerInfo(payload);
            else if (packetType == HPN_HANDSHAKE_ACK)        HandlePacket_HandshakeAck(payload);
            else if (packetType == HPN_ERROR)                HandlePacket_Error(payload);

            // ================================================================
            // ROOM.*
            // ================================================================
            else if (packetType == HPN_ROOM_JOINED)          HandlePacket_RoomJoined(payload);
            else if (packetType == HPN_ROOM_LEFT)            HandlePacket_RoomLeft(payload);
            else if (packetType == HPN_ROOM_LIST_RESPONSE)   HandlePacket_RoomList(payload);
            else if (packetType == HPN_ROOM_MEMBERS)         HandlePacket_AllClients(payload);
            else if (packetType == HPN_ROOM_MANIFEST)        HandlePacket_GamemodeManifest(payload);
            else if (packetType == HPN_ROOM_PHASE_CHANGED)   HandlePacket_PhaseChanged(payload);
            else if (packetType == HPN_ROOM_EVENT)           HandlePacket_RoomEvent(payload);

            // ================================================================
            // PLAYER.* — granular per-frame
            // ================================================================
            else if (packetType == HPN_PLAYER_TRANSFORM)         HandlePacket_PlayerTransform(payload);
            else if (packetType == HPN_PLAYER_SKELETON)          HandlePacket_PlayerSkeleton(payload);
            else if (packetType == HPN_PLAYER_LIMB_ROT)          HandlePacket_PlayerLimbRotations(payload);
            else if (packetType == HPN_PLAYER_ANIM_FLAGS)        HandlePacket_PlayerAnimationFlags(payload);
            else if (packetType == HPN_PLAYER_MOTION_VARS)       HandlePacket_PlayerMotionVars(payload);
            else if (packetType == HPN_PLAYER_BOW_STATE)         HandlePacket_PlayerBowState(payload);
            else if (packetType == HPN_PLAYER_HAND_TYPES)        HandlePacket_PlayerHandTypes(payload);
            else if (packetType == HPN_PLAYER_VISUAL_STATE)      HandlePacket_PlayerVisualState(payload);
            else if (packetType == HPN_PLAYER_EQUIP_VISIBLE)     HandlePacket_PlayerEquipVisible(payload);
            else if (packetType == HPN_PLAYER_FACE)              HandlePacket_PlayerFace(payload);
            else if (packetType == HPN_PLAYER_SCALE)             HandlePacket_PlayerScale(payload);
            else if (packetType == HPN_PLAYER_TRANSFORMATION)    HandlePacket_PlayerTransformation(payload);
            else if (packetType == HPN_PLAYER_GORON_STATE)       HandlePacket_PlayerGoronState(payload);
            else if (packetType == HPN_PLAYER_INVINCIBILITY)     HandlePacket_PlayerInvincibility(payload);
            else if (packetType == HPN_PLAYER_CUSTOM_ITEM)       HandlePacket_PlayerCustomItemState(payload);
            else if (packetType == HPN_PLAYER_FULL_STATE)        HandlePacket_PlayerFullState(payload);
            else if (packetType == HPN_PLAYER_KILL)              HandlePacket_PlayerDied(payload);

            // ================================================================
            // COMBAT.*
            // ================================================================
            else if (packetType == HPN_COMBAT_DAMAGE)         HandlePacket_Damage(payload);
            else if (packetType == HPN_COMBAT_DECOY_HIT)      HandlePacket_DecoyHit(payload);
            else if (packetType == HPN_COMBAT_CUSTOM_EFFECT)  HandlePacket_CustomEffect(payload);
            else if (packetType == HPN_COMBAT_SPAWN_DECOY) {
                // Mirror Scooter's decoy ring on the source client's
                // HarpoonClient. We store {pos, rotY, propCat/Idx/State} so
                // VB_ACTOR_POST_DRAW (or a dedicated decoy-draw hook) can
                // render the ghost prop at the decoy's world position.
                uint32_t src = payload.value("source", 0u);
                nlohmann::json inner = payload.contains("payload") ? payload["payload"]
                                       : payload.contains("data")    ? payload["data"]
                                                                     : payload;
                u8 slot = (u8)inner.value("slot", 0);
                if (slot < 3 && clients.find(src) != clients.end()) {
                    auto& c = clients[src];
                    c.somariaDecoyPos[slot].x = inner.value("x", 0.0f);
                    c.somariaDecoyPos[slot].y = inner.value("y", 0.0f);
                    c.somariaDecoyPos[slot].z = inner.value("z", 0.0f);
                    c.somariaDecoyRotY[slot]      = (s16)inner.value("rotY", 0);
                    c.somariaDecoyPropCat[slot]   = inner.value("propCat", 0);
                    c.somariaDecoyPropIdx[slot]   = inner.value("propIndex", 0);
                    c.somariaDecoyPropState[slot] = inner.value("propState", 0);
                    c.somariaDecoyActive[slot]    = 1;
                }
            }
            else if (packetType == HPN_COMBAT_DESTROY_DECOY) {
                uint32_t src = payload.value("source", 0u);
                nlohmann::json inner = payload.contains("payload") ? payload["payload"]
                                                                    : payload;
                u8 slot = (u8)inner.value("slot", 0);
                if (slot < 3 && clients.find(src) != clients.end()) {
                    clients[src].somariaDecoyActive[slot] = 0;
                }
            }

            // ================================================================
            // INVENTORY.* / SAVE.* (Anchor rando + general save sync)
            // ================================================================
            else if (packetType == HPN_INV_GIVE_ITEM)         HandlePacket_GiveItem(payload);
            else if (packetType == HPN_INV_DUNGEON_ITEMS)     HandlePacket_UpdateDungeonItems(payload);
            else if (packetType == HPN_INV_AMMO)              HandlePacket_UpdateBeansCount(payload);
            else if (packetType == HPN_SAVE_SET_FLAG)         HandlePacket_SetFlag(payload);
            else if (packetType == HPN_SAVE_UNSET_FLAG)       HandlePacket_UnsetFlag(payload);
            else if (packetType == HPN_SAVE_QUEST_STATE)      HandlePacket_SetCheckStatus(payload);
            else if (packetType == HPN_SAVE_TEAM_STATE)       HandlePacket_UpdateTeamState(payload);
            else if (packetType == HPN_SAVE_TEAM_REQUEST)     HandlePacket_RequestTeamState(payload);
            else if (packetType == HPN_SAVE_CUTSCENE)         HandlePacket_CutsceneTrigger(payload);
            else if (packetType == HPN_SAVE_GAME_COMPLETE)    HandlePacket_GameComplete(payload);
            else if (packetType == HPN_AUDIO_OCARINA)         HandlePacket_OcarinaSfx(payload);
            else if (packetType == HPN_APPEARANCE_SPAWN_VFX)  HandlePacket_SpawnVfxActor(payload);

            // ================================================================
            // WORLD.* / MAP.* / AUDIO.* / UI.*
            // ================================================================
            else if (packetType == HPN_WORLD_TRANSPORT)       HandlePacket_TeleportTo(payload);
            else if (packetType == HPN_MAP_ENTRANCE)          HandlePacket_EntranceDiscovered(payload);
            else if (packetType == HPN_AUDIO_SFX)             HandlePacket_PlayerSfx(payload);
            else if (packetType == HPN_UI_MESSAGE)            HandlePacket_ServerMsg(payload);

            // ================================================================
            // APPEARANCE.SKIN_SYNC.*
            // ================================================================
            else if (packetType == HPN_SKIN_ANNOUNCE)         HandlePacket_SkinSyncAnnounceCatalog(payload);
            else if (packetType == HPN_SKIN_UPDATE_SLOTS)     HandlePacket_SkinSyncUpdateSlots(payload);

            // No-op handlers for primitives the engine doesn't react to yet.
            // Keep them silent so we don't log "unknown" warnings for normal
            // server traffic.
            else if (packetType == "ROOM.GAMEMODE_CHANGED")   { /* no-op */ }
            else if (packetType == HPN_ROOM_GM_CONFIG)        { /* manifest already handled */ }
            else { SPDLOG_DEBUG("[Harpoon] unhandled type: {}", packetType); }
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
        SPDLOG_DEBUG("[Harpoon] RefreshClientActors: skip (save not loaded)");
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

    int spawned = 0, deferred = 0, skipped = 0;
    for (auto& [clientId, client] : clients) {
        if (!client.online || client.self) {
            skipped++;
            client.player = nullptr;
            continue;
        }
        // Defer spawn until we have a real position. Spawning at world origin
        // when no transform packet has arrived yet usually puts the dummy
        // outside the playable area — invisible even if all the EXILE checks
        // pass. HandlePacket_PlayerUpdate / Transform sets shouldRefreshActors
        // when the first real posRot arrives, which re-enters this loop.
        bool noPos = (client.posRot.pos.x == 0.0f && client.posRot.pos.y == 0.0f &&
                      client.posRot.pos.z == 0.0f);
        if (noPos) {
            client.player = nullptr;
            deferred++;
            SPDLOG_INFO("[Harpoon] RefreshClientActors: cid={} '{}' DEFER (no posRot yet, scene={} saveLoaded={})",
                        clientId, client.name, client.sceneNum, client.isSaveLoaded);
            continue;
        }

        spawningDummyPlayerForClientId = clientId;
        auto dummy =
            Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, client.posRot.pos.x, client.posRot.pos.y,
                        client.posRot.pos.z, client.posRot.rot.x, client.posRot.rot.y, client.posRot.rot.z, 0);
        client.player = (Player*)dummy;
        spawned++;
        SPDLOG_INFO("[Harpoon] RefreshClientActors: cid={} '{}' SPAWN at ({:.0f},{:.0f},{:.0f}) scene={} saveLoaded={}",
                    clientId, client.name,
                    client.posRot.pos.x, client.posRot.pos.y, client.posRot.pos.z,
                    client.sceneNum, client.isSaveLoaded);
    }
    spawningDummyPlayerForClientId = 0;
    SPDLOG_INFO("[Harpoon] RefreshClientActors done: spawned={} deferred={} skipped={} (own={})",
                spawned, deferred, skipped, ownClientId);
}

bool Harpoon::IsSaveLoaded() {
    if (gPlayState == nullptr)
        return false;
    if (GET_PLAYER(gPlayState) == nullptr)
        return false;
    // Allow real slots 0/1/2 AND high sentinels (0xFD = Harpoon multiplayer,
    // 0xFE = Boss Rush, 0xFF = Debug). Using 0xFD for multiplayer keeps the
    // engine from clobbering real save slots — SaveManager::Init only scans
    // fileNum < MaxFiles (=3), so file254.sav is never parsed at startup
    // and any autosave writes there are harmless on next launch.
    if (gSaveContext.fileNum < 0)
        return false;
    if (gSaveContext.fileNum > 2 && gSaveContext.fileNum < 0xFD)
        return false;
    if (gSaveContext.gameMode != GAMEMODE_NORMAL)
        return false;
    return true;
}

// Resolve the display name of the locally-selected pak at the given slot.
// Returns an empty string when no pak is selected, the CVar is out of range,
// or PakLoader hasn't finished initializing.
static std::string GetLocalSkinName(const char* cvarName) {
    s32 idx = CVarGetInteger(cvarName, -1);
    if (idx < 0) return "";
    const char* name = PakLoader_GetModelName(idx);
    return name ? std::string(name) : "";
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

    // Skin sync: broadcast the display name of the LOCAL (mods/) pak selection.
    // Remote clients look the name up in THEIR harpoon/skins/ — missing
    // skins fall back to vanilla Link + a one-shot UI notification.
    state["adultSkin"] = GetLocalSkinName("gMods.PakLoader.AdultModel");
    state["childSkin"] = GetLocalSkinName("gMods.PakLoader.ChildModel");
    state["equipSkin"] = GetLocalSkinName("gMods.PakLoader.Equipment");
    {
        const char* forcedPtr = PakLoader_GetForcedModelName();
        state["forcedSkin"] = forcedPtr ? std::string(forcedPtr) : std::string("");
    }

    // .o2r mod list (handshake-only) — not used for rendering, just for
    // informing peers of potential global visual divergence.
    nlohmann::json o2rMods = nlohmann::json::array();
    for (const auto& m : ModMenu_GetEnabledMods()) {
        o2rMods.push_back(m);
    }
    state["o2rMods"] = o2rMods;

    return state;
}

// MARK: - Send Packets

void Harpoon::SendPacket_Handshake() {
    // Harpoon v2 HARPOON.HANDSHAKE — flat {name, color, clientVersion,
    // installedGamemodes}. The server is gamemode-agnostic; we tell it which
    // gamemodes we have installed locally so it can filter the room browser
    // it sends back. Rooms whose gamemode_id we don't have are invisible to
    // us — that's the privacy mechanism for custom packs.
    nlohmann::json clientState = PrepClientState();
    nlohmann::json payload;
    payload["type"] = HPN_HANDSHAKE;
    // Protocol marker — server rejects anything else. Soft barrier to keep
    // the server from being repurposed as a generic WS relay.
    payload["protocol"] = "harpoon";
    payload["name"] = clientState.value("name", std::string("Player"));
    payload["color"] = clientState.value("color", nlohmann::json{ {"r", 100}, {"g", 255}, {"b", 100} });
    payload["clientVersion"] = clientState.value("clientVersion", std::string(""));
    nlohmann::json gms = nlohmann::json::array();
    for (const auto& gid : HarpoonSkinSync::GetInstalledGamemodes()) {
        gms.push_back(gid);
    }
    payload["installedGamemodes"] = gms;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_O2rModList() {
    // v2: APPEARANCE.SKIN_SYNC.ANNOUNCE_CATALOG. The server schema accepts
    // `mods` and `syncMods` as aliases for `enabled_mods` and `sync_catalog`.
    nlohmann::json payload;
    payload["type"] = HPN_SKIN_ANNOUNCE;
    nlohmann::json mods = nlohmann::json::array();
    std::string modsStr;
    for (const auto& m : ModMenu_GetEnabledMods()) {
        mods.push_back(m);
        if (!modsStr.empty()) modsStr += ", ";
        modsStr += m;
    }
    payload["mods"] = mods;
    // Also broadcast our harpoon/skins registry — names of mods we
    // have available to render OTHER players. Lets remotes suppress the
    // "you have mod X they don't" notification when X is in this list
    // (we can render them correctly via our override path).
    nlohmann::json syncMods = nlohmann::json::array();
    std::string syncStr;
    for (const auto& m : HarpoonSkinSync::GetOverrideNames()) {
        syncMods.push_back(m);
        if (!syncStr.empty()) syncStr += ", ";
        syncStr += m;
    }
    payload["syncMods"] = syncMods;
    SPDLOG_INFO("[Harpoon] SendPacket_O2rModList: {} mods=[{}] sync=[{}]",
                (int)mods.size(), modsStr, syncStr);
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_PlayerUpdate() {
    if (!IsSaveLoaded())
        return;

    // Server applies AOI (`same_scene_as=session`) — no need to duplicate the
    // filter here. Anchor doesn't have one and works. Local filtering creates
    // a chicken-and-egg race: if our roster's view of teammates is briefly
    // stale (sceneNum lagging the server), we skip sending and the dummy on
    // their side never gets a transform → spawned at world origin and exiled.
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
    // Hand types — these drive which hand DL the engine picks (open / closed
    // / sword / bow / etc.) at draw time. Without syncing them explicitly,
    // the dummy's hand model can lag behind the remote's actual item state
    // (e.g. remote draws sword, dummy still shows open fist).
    payload["leftHandType"] = player->leftHandType;
    payload["rightHandType"] = player->rightHandType;
    payload["sheathType"] = player->sheathType;
    // Per-frame visual state — these change every frame as the player
    // moves / animates / aims, and the engine reads them to pick the
    // correct hand/item DL at draw time. See z_player_lib.c:1547+ where
    // open hand becomes closed when speedXZ > 2.0 (running).
    payload["speedXZ"] = player->actor.speedXZ;
    payload["meleeWeaponState"] = player->meleeWeaponState;
    payload["fpModeFlag"] = player->unk_6AD;
    payload["bowStringDraw"] = player->unk_858;
    payload["bowArrowState"] = player->unk_860;
    payload["bowDrawAnimFrame"] = player->unk_834;
    payload["headLimbRotX"] = player->headLimbRot.x;
    payload["headLimbRotY"] = player->headLimbRot.y;
    payload["headLimbRotZ"] = player->headLimbRot.z;
    payload["upperLimbYawSecondary"] = player->upperLimbYawSecondary;
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

    // Skin sync — broadcast currently-selected local pak display names.
    // Kept in the per-frame update (not just handshake) so late changes in the
    // local menu propagate without having to reconnect.
    std::string adultSkinName = GetLocalSkinName("gMods.PakLoader.AdultModel");
    std::string childSkinName = GetLocalSkinName("gMods.PakLoader.ChildModel");
    std::string equipSkinName = GetLocalSkinName("gMods.PakLoader.Equipment");
    payload["adultSkin"] = adultSkinName;
    payload["childSkin"] = childSkinName;
    payload["equipSkin"] = equipSkinName;
    // Forced model overrides (Kafei mask transform, Champion's Tunic, etc.) —
    // these are runtime PakLoader_ForceModel calls, NOT user menu selections.
    // Broadcast separately so remotes can mirror Kafei when activated.
    const char* forcedSkinPtr = PakLoader_GetForcedModelName();
    std::string forcedSkinName = forcedSkinPtr ? std::string(forcedSkinPtr) : "";
    payload["forcedSkin"] = forcedSkinName;
    {
        // Once-per-(name-fingerprint) diagnostic so we can see exactly what
        // skin we're broadcasting without spamming each frame.
        static std::string sLastFingerprint;
        std::string fp = adultSkinName + "|" + childSkinName + "|" + equipSkinName + "|" + forcedSkinName;
        if (sLastFingerprint != fp) {
            SPDLOG_INFO("[Harpoon] Broadcast skin: adult='{}' child='{}' equip='{}' forced='{}'",
                        adultSkinName, childSkinName, equipSkinName, forcedSkinName);
            sLastFingerprint = fp;
        }
    }

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
            payload["ciGustJarMode"] = ci->gustJarMode;
            payload["ciGustJarElement"] = ci->gustJarElement;
            payload["ciGustJarBlowActive"] = ci->gustJarBlowActive;
            payload["ciGustJarHeatTimer"] = ci->gustJarHeatTimer;
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
        // ── Phase 1 sync ──────────────────────────────────────────────
        if (ciFlags & CI_FLAG_ROCS_FEATHER) {
            payload["ciRocsJumpCount"] = ci->rocsJumpCount;
            payload["ciRocsMmAnimTimer"] = ci->rocsMmAnimTimer;
        }
        if (ciFlags & CI_FLAG_BOMB_ARROW) {
            payload["ciBombArrowState"] = ci->bombArrowState;
        }
        // CI_FLAG_DEMISE_DESTRUCTION carries no extra fields beyond the flag.
        if (ciFlags & CI_FLAG_HYLIAS_GRACE) {
            payload["ciHyliasGraceState"]          = ci->hyliasGraceState;
            payload["ciHyliasGraceSubPhase"]       = ci->hyliasGraceSubPhase;
            payload["ciHyliasGraceTimer"]          = ci->hyliasGraceTimer;
            payload["ciHyliasGraceForcedBySpell"]  = ci->hyliasGraceForcedBySpell;
        }
        if (ciFlags & CI_FLAG_ZONAI_PERMAFROST) {
            payload["ciZonaiPermafrostState"]    = ci->zonaiPermafrostState;
            payload["ciZonaiPermafrostSubPhase"] = ci->zonaiPermafrostSubPhase;
            payload["ciZonaiPermafrostTimer"]    = ci->zonaiPermafrostTimer;
        }
        if (ciFlags & CI_FLAG_LANTERN) {
            payload["ciLanternFireType"]    = ci->lanternFireType;
            payload["ciLanternSwinging"]    = ci->lanternSwinging;
            payload["ciLanternEquipped"]    = ci->lanternEquipped;
            payload["ciLanternSwingFrame"]  = ci->lanternSwingFrame;
        }
        if (ciFlags & CI_FLAG_MINISH_CAP) {
            payload["ciMinishCapWarpMode"]  = ci->minishCapWarpMode;
            payload["ciMinishCapShrinking"] = ci->minishCapShrinking;
            payload["ciMinishCapGrowing"]   = ci->minishCapGrowing;
        }
        if (ciFlags & CI_FLAG_POSTMAN_HAT) {
            payload["ciPostmanHatDashing"]           = ci->postmanHatDashing;
            payload["ciPostmanHatArriving"]          = ci->postmanHatArriving;
            payload["ciPostmanHatTransitionTimer"]   = ci->postmanHatTransitionTimer;
        }
        if (ciFlags & CI_FLAG_DESIRE_SENSOR) {
            payload["ciDesireSensorState"]   = ci->desireSensorState;
            payload["ciDesireSensorTimer"]   = ci->desireSensorTimer;
            payload["ciDesireSensorResult"]  = ci->desireSensorResult;
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

    // v2: one send. Server broadcasts to same-scene members automatically
    // based on the sender's `scene_num` (tracked from PLAYER.UPDATE_VISUAL_STATE).
    SendJsonToRemote(payload);
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
    // Server broadcasts the room's current host in every ROOM.MEMBERS_UPDATED.
    // Persist it so the menu's "isHost" check (ownClientId == hostClientId)
    // works — without this, hostClientId stays 0 forever and every "host
    // only" UI gates closed regardless of who actually created the room.
    if (payload.contains("hostClientId")) {
        hostClientId = payload["hostClientId"].get<uint32_t>();
    }
    bool roomMembershipChanged = false;
    if (payload.contains("clients")) {
        size_t prevOnlineCount = 0;
        for (auto& [id, client] : clients) {
            if (client.online) prevOnlineCount++;
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
            client.role = clientJson.value("role", std::string());
        }

        size_t newOnlineCount = 0;
        for (auto& [id, client] : clients) {
            if (client.online) newOnlineCount++;
        }
        roomMembershipChanged = (newOnlineCount != prevOnlineCount);

        // Diagnostic: dump roster so we can see what each peer looks like to us.
        std::string members;
        for (auto& [id, c] : clients) {
            char buf[96];
            snprintf(buf, sizeof(buf), "%u'%s'(scn=%d sl=%d on=%d%s)%s",
                     id, c.name.c_str(), c.sceneNum, (int)c.isSaveLoaded,
                     (int)c.online, c.self ? " SELF" : "",
                     members.empty() ? "" : ", ");
            members = std::string(buf) + (members.empty() ? "" : (", " + members));
        }
        SPDLOG_INFO("[Harpoon] ROOM.MEMBERS_UPDATED own={} count={} -> [{}]",
                    ownClientId, (int)clients.size(), members);

        shouldRefreshActors = true;
    }

    // Re-broadcast our enabled .o2r mod list whenever the room membership
    // changes — the list is normally only sent right after joining, but if a
    // peer joins AFTER us their initial PVP_ALL_CLIENTS won't carry our list
    // (it was sent before they were a member, so the server's relay dropped it
    // for them). Triggering a re-send on every membership change ensures every
    // peer eventually has every other peer's list, which is what the per-actor
    // override sync needs.
    if (roomMembershipChanged) {
        SendPacket_O2rModList();
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
        // If we deferred the spawn earlier (no posRot known), this is the
        // packet that lets us actually spawn — schedule a refresh.
        bool hasPos = (client.posRot.pos.x != 0.0f || client.posRot.pos.y != 0.0f ||
                       client.posRot.pos.z != 0.0f);
        if (client.player == nullptr && hasPos && client.online) {
            shouldRefreshActors = true;
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
    client.leftHandType = payload.value("leftHandType", (s8)0);
    client.rightHandType = payload.value("rightHandType", (s8)0);
    client.sheathType = payload.value("sheathType", (s8)0);
    client.speedXZ = payload.value("speedXZ", 0.0f);
    client.meleeWeaponState = payload.value("meleeWeaponState", (s8)0);
    client.fpModeFlag = payload.value("fpModeFlag", (u8)0);
    client.bowStringDraw = payload.value("bowStringDraw", 0.0f);
    client.bowArrowState = payload.value("bowArrowState", (s16)0);
    client.bowDrawAnimFrame = payload.value("bowDrawAnimFrame", (s16)0);
    client.headLimbRot.x = payload.value("headLimbRotX", (s16)0);
    client.headLimbRot.y = payload.value("headLimbRotY", (s16)0);
    client.headLimbRot.z = payload.value("headLimbRotZ", (s16)0);
    client.upperLimbYawSecondary = payload.value("upperLimbYawSecondary", (s16)0);
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

    // Skin sync — names of the remote's selected pak slots (resolved at draw time
    // against harpoon/skins/). Absent / empty → fall back to vanilla Link.
    std::string newAdultSkin = payload.value("adultSkin", std::string(""));
    std::string newChildSkin = payload.value("childSkin", std::string(""));
    std::string newEquipSkin = payload.value("equipSkin", std::string(""));
    std::string newForcedSkin = payload.value("forcedSkin", std::string(""));
    if (newAdultSkin != client.adultSkinName || newChildSkin != client.childSkinName ||
        newEquipSkin != client.equipSkinName || newForcedSkin != client.forcedSkinName) {
        SPDLOG_INFO("[Harpoon] Received skin update for '{}' (id={}): adult='{}' child='{}' equip='{}' forced='{}'",
                    client.name, clientId, newAdultSkin, newChildSkin, newEquipSkin, newForcedSkin);
    }
    client.adultSkinName = newAdultSkin;
    client.childSkinName = newChildSkin;
    client.equipSkinName = newEquipSkin;
    client.forcedSkinName = newForcedSkin;

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
        client.ciGustJarMode = payload.value("ciGustJarMode", (u8)0);
        client.ciGustJarElement = payload.value("ciGustJarElement", (u8)0);
        client.ciGustJarBlowActive = payload.value("ciGustJarBlowActive", (u8)0);
        client.ciGustJarHeatTimer = payload.value("ciGustJarHeatTimer", (s16)0);
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
    // ── Phase 1 sync receive ─────────────────────────────────────────────
    client.ciRocsFeatherJumpActive    = (ciFlags & CI_FLAG_ROCS_FEATHER) ? 1 : 0;
    client.ciBombArrowActive          = (ciFlags & CI_FLAG_BOMB_ARROW) ? 1 : 0;
    client.ciDemiseDestructionActive  = (ciFlags & CI_FLAG_DEMISE_DESTRUCTION) ? 1 : 0;
    client.ciHyliasGraceActive        = (ciFlags & CI_FLAG_HYLIAS_GRACE) ? 1 : 0;
    client.ciZonaiPermafrostActive    = (ciFlags & CI_FLAG_ZONAI_PERMAFROST) ? 1 : 0;
    client.ciDesireSensorActive       = (ciFlags & CI_FLAG_DESIRE_SENSOR) ? 1 : 0;
    if (ciFlags & CI_FLAG_ROCS_FEATHER) {
        client.ciRocsJumpCount    = payload.value("ciRocsJumpCount", (u8)0);
        client.ciRocsMmAnimTimer  = payload.value("ciRocsMmAnimTimer", (s16)0);
    }
    if (ciFlags & CI_FLAG_BOMB_ARROW) {
        client.ciBombArrowState = payload.value("ciBombArrowState", (u8)0);
    }
    if (ciFlags & CI_FLAG_HYLIAS_GRACE) {
        client.ciHyliasGraceState         = payload.value("ciHyliasGraceState", (u8)0);
        client.ciHyliasGraceSubPhase      = payload.value("ciHyliasGraceSubPhase", (u8)0);
        client.ciHyliasGraceTimer         = payload.value("ciHyliasGraceTimer", (s16)0);
        client.ciHyliasGraceForcedBySpell = payload.value("ciHyliasGraceForcedBySpell", (u8)0);
    }
    if (ciFlags & CI_FLAG_ZONAI_PERMAFROST) {
        client.ciZonaiPermafrostState    = payload.value("ciZonaiPermafrostState", (u8)0);
        client.ciZonaiPermafrostSubPhase = payload.value("ciZonaiPermafrostSubPhase", (u8)0);
        client.ciZonaiPermafrostTimer    = payload.value("ciZonaiPermafrostTimer", (s16)0);
    }
    if (ciFlags & CI_FLAG_LANTERN) {
        client.ciLanternFireType   = payload.value("ciLanternFireType", (u8)0);
        client.ciLanternSwinging   = payload.value("ciLanternSwinging", (u8)0);
        client.ciLanternEquipped   = payload.value("ciLanternEquipped", (u8)0);
        client.ciLanternSwingFrame = payload.value("ciLanternSwingFrame", (s16)0);
    }
    if (ciFlags & CI_FLAG_MINISH_CAP) {
        client.ciMinishCapWarpMode  = payload.value("ciMinishCapWarpMode", (u8)0);
        client.ciMinishCapShrinking = payload.value("ciMinishCapShrinking", (u8)0);
        client.ciMinishCapGrowing   = payload.value("ciMinishCapGrowing", (u8)0);
    }
    if (ciFlags & CI_FLAG_POSTMAN_HAT) {
        client.ciPostmanHatDashing         = payload.value("ciPostmanHatDashing", (u8)0);
        client.ciPostmanHatArriving        = payload.value("ciPostmanHatArriving", (u8)0);
        client.ciPostmanHatTransitionTimer = payload.value("ciPostmanHatTransitionTimer", (s16)0);
    }
    if (ciFlags & CI_FLAG_DESIRE_SENSOR) {
        client.ciDesireSensorState  = payload.value("ciDesireSensorState", (u8)0);
        client.ciDesireSensorTimer  = payload.value("ciDesireSensorTimer", (s16)0);
        client.ciDesireSensorResult = payload.value("ciDesireSensorResult", (u8)0);
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

    // Per-response knockback/element tuning. The damage table on the dummy
    // side already maps each weapon to one of these response codes; the
    // receiver branches on it to pick knockback speed/yVel/invincibility and
    // (for utility weapons like the wind blow) zero out damage.
    f32 knockSpeed  = 4.0f;
    f32 knockYVel   = 5.0f;
    s32 invTimer    = 20;
    s32 finalDamage = damage;

    switch (damageEffect) {
    case PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE:  // Megaton Hammer, Ball & Chain
        knockSpeed = 14.0f; knockYVel = 10.0f; invTimer = 25;
        break;
    case HARPOON_HIT_RESPONSE_WIND_BLOW:       // Deku Leaf gust / Gust Jar
        knockSpeed = 18.0f; knockYVel = 4.0f;  invTimer = 15;
        finalDamage = 0;                        // pure utility — strips carrier without HP loss
        break;
    case PLAYER_HIT_RESPONSE_ICE_TRAP:         // Ice arrow / Ice rod
        self->actor.freezeTimer = 60;
        Actor_SetColorFilter(&self->actor, 0, 0xFF, 0, 60);
        knockSpeed = 0.0f; knockYVel = 0.0f; invTimer = 60;
        break;
    case PLAYER_HIT_RESPONSE_ELECTRIC_SHOCK:   // Light arrow
        self->actor.freezeTimer = 20;
        Actor_SetColorFilter(&self->actor, 0, 0xFF, 0, 24);
        knockSpeed = 2.0f; knockYVel = 3.0f; invTimer = 20;
        break;
    case HARPOON_HIT_RESPONSE_STUN:
        self->actor.freezeTimer = 20;
        Actor_SetColorFilter(&self->actor, 0, 0xFF, 0, 24);
        knockSpeed = 0.0f; knockYVel = 0.0f;
        break;
    case HARPOON_HIT_RESPONSE_FIRE:            // Fire arrow / Fire rod / Din's Fire
        knockSpeed = 5.0f; knockYVel = 6.0f; invTimer = 30;
        break;
    default:  // NORMAL + unknown — keep legacy feel
        break;
    }

    self->actor.colChkInfo.damage = finalDamage * 8;

    // The server's relay tags the attacker as both `clientId` (legacy /
    // Scooter compat) and `source` (Python idiom). Read both — whichever
    // exists. Without this the lookup falls back to 0 → clients.contains(0)
    // is false → func_80837C0C never fires → receiver feels nothing.
    uint32_t attackerClientId = payload.value("clientId", (uint32_t)0);
    if (attackerClientId == 0) {
        attackerClientId = payload.value("source", (uint32_t)0);
    }
    if (clients.contains(attackerClientId) && clients[attackerClientId].player != nullptr) {
        Player* attacker = clients[attackerClientId].player;
        func_80837C0C(gPlayState, self, damageEffect, knockSpeed, knockYVel,
                      Actor_WorldYawTowardActor(&attacker->actor, &self->actor), invTimer);
    } else {
        // Still apply damage even if we can't find the attacker (e.g. they
        // disconnected mid-hit). Use yaw 0 — knockback won't aim correctly
        // but at least HP drops.
        func_80837C0C(gPlayState, self, damageEffect, knockSpeed, knockYVel, 0, invTimer);
        SPDLOG_DEBUG("[Harpoon] damage from unknown attacker cid={} — applied without knockback target",
                     attackerClientId);
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

// Counter ported from Anchor (GiveItem.cpp). Bumped on every incoming ice trap
// applied locally; decremented (not re-broadcast) on the next OUTGOING ice
// trap. Without this, A's incoming ice trap fires the local OnItemReceive
// hook → broadcasts back to A → ∞ loop.
static uint8_t sIncomingIceTrapsFromHarpoon = 0;

void Harpoon::SendPacket_GiveItem(u16 modId, s16 getItemId) {
    if (!IsSaveLoaded() || isProcessingIncomingPacket) {
        return;
    }
    if (!syncItems) {
        // Surface this once-per-session so a misconfigured pack (e.g. user
        // joined a default room with sync_items=false) is visible in logs
        // instead of silently dropping every pickup.
        static bool warned = false;
        if (!warned) {
            warned = true;
            SPDLOG_WARN("[Harpoon] item not broadcast — current room has sync_items=false "
                        "(modId={} getItemId={})", modId, getItemId);
        }
        return;
    }

    // Ice trap loop guard.
    if (modId == MOD_RANDOMIZER && getItemId == RG_ICE_TRAP && sIncomingIceTrapsFromHarpoon > 0) {
        sIncomingIceTrapsFromHarpoon--;
        return;
    }

    // Don't broadcast a Master Sword pickup from inside the final Ganon fight —
    // the engine forces-equips it temporarily and it doesn't represent real
    // progression for the team.
    if (modId == MOD_RANDOMIZER && getItemId == RG_MASTER_SWORD &&
        gPlayState != nullptr && gPlayState->sceneNum == SCENE_GANON_BOSS) {
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
            sIncomingIceTrapsFromHarpoon++;  // loop guard, see SendPacket_GiveItem
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
    // Suppress OnRandoSetCheckStatus / OnRandoSetIsSkipped re-broadcasts that
    // would otherwise rate-limit-storm the server: applying the team save
    // touches every check, each of which would otherwise fire SendPacket_SetCheckStatus.
    isHandlingUpdateTeamState = true;

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

        // Anchor parity: keep team's earliest input timestamp + creation time.
        gSaveContext.ship.stats.firstInput = loadedData.ship.stats.firstInput;
        gSaveContext.ship.stats.fileCreatedAt = loadedData.ship.stats.fileCreatedAt;

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

    isHandlingUpdateTeamState = false;
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
    // v2: HARPOON.SERVER_INFO carries `client_id` and `session_token`.
    // Legacy: HARPOON_SERVER_INFO carried `ownClientId`.
    if (payload.contains("client_id")) {
        ownClientId = payload["client_id"].get<uint32_t>();
    } else if (payload.contains("ownClientId")) {
        ownClientId = payload["ownClientId"].get<uint32_t>();
    }
    if (payload.contains("session_token")) {
        sessionToken = payload["session_token"].get<std::string>();
    } else if (payload.contains("sessionToken")) {
        sessionToken = payload["sessionToken"].get<std::string>();
    }
    SPDLOG_INFO("[Harpoon] HARPOON.SERVER_INFO ownClientId={} token={}",
                ownClientId, sessionToken.empty() ? std::string("<none>") : sessionToken.substr(0, 8) + "…");
}

// Tiny line-based reader for a few specific keys inside a gamemode.yaml's
// top-level `default_config:` block. We only need the booleans pvp_enabled,
// sync_items, sync_cutscenes — adding a yaml-cpp dependency for that would
// be excessive. The parser scans for the `default_config:` line and then
// reads indented `key: value` lines until indentation drops, so it correctly
// ignores other `pvp_enabled` keys nested in unrelated sections.
static void ApplyLocalGamemodeManifest(
        const std::string& gid,
        bool& pvpEnabled, bool& syncItems, bool& syncCutscenes,
        bool& supportsVoting, bool& supportsMapSelect,
        bool& supportsZTarget, bool& supportsRoundFlow) {
    auto path = HarpoonSkinSync::GetGamemodeManifestPath(gid);
    if (path.empty()) {
        SPDLOG_INFO("[Harpoon] no local manifest for '{}' — keeping current defaults "
                    "(pvp={} syncItems={} syncCutscenes={})", gid, pvpEnabled, syncItems, syncCutscenes);
        return;
    }
    std::ifstream f(path);
    if (!f.is_open()) {
        SPDLOG_WARN("[Harpoon] failed to open manifest '{}'", path.string());
        return;
    }
    auto trim = [](std::string s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        return (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
    };
    auto parseBool = [](std::string v) {
        std::string lo;
        for (char c : v) lo.push_back((char)tolower((unsigned char)c));
        if (lo == "true" || lo == "yes" || lo == "1") return std::optional<bool>{true};
        if (lo == "false" || lo == "no" || lo == "0") return std::optional<bool>{false};
        return std::optional<bool>{};
    };
    bool inDefaultConfig = false;
    int defaultConfigIndent = -1;
    std::string line;
    while (std::getline(f, line)) {
        // Strip CR (Windows line endings).
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Skip pure-comment / blank lines.
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;

        // Top-level `default_config:` switches us into the block. Anything
        // un-indented (`key:` at column 0) drops us back out.
        size_t indent = 0;
        while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) indent++;

        if (indent == 0) {
            // Starting a new top-level key. Are we entering or exiting default_config?
            if (t.rfind("default_config:", 0) == 0) {
                inDefaultConfig = true;
                defaultConfigIndent = -1; // Set on first nested line.
                continue;
            }
            inDefaultConfig = false;
            continue;
        }
        if (!inDefaultConfig) continue;

        if (defaultConfigIndent < 0) defaultConfigIndent = (int)indent;
        if ((int)indent < defaultConfigIndent) {
            inDefaultConfig = false;
            continue;
        }

        size_t colon = t.find(':');
        if (colon == std::string::npos) continue;
        std::string key = trim(t.substr(0, colon));
        std::string val = trim(t.substr(colon + 1));
        // Strip inline comments.
        size_t hash = val.find('#');
        if (hash != std::string::npos) val = trim(val.substr(0, hash));

        if (key == "pvp_enabled") {
            if (auto b = parseBool(val)) pvpEnabled = *b;
        } else if (key == "sync_items") {
            if (auto b = parseBool(val)) syncItems = *b;
        } else if (key == "sync_cutscenes") {
            if (auto b = parseBool(val)) syncCutscenes = *b;
        } else if (key == "supports_voting") {
            if (auto b = parseBool(val)) supportsVoting = *b;
        } else if (key == "supports_map_select") {
            if (auto b = parseBool(val)) supportsMapSelect = *b;
        } else if (key == "supports_z_target") {
            if (auto b = parseBool(val)) supportsZTarget = *b;
        } else if (key == "supports_round_flow") {
            if (auto b = parseBool(val)) supportsRoundFlow = *b;
        }
    }
    SPDLOG_INFO("[Harpoon] applied local manifest '{}': "
                "pvp_enabled={} sync_items={} sync_cutscenes={} "
                "supports_voting={} supports_map_select={} "
                "supports_z_target={} supports_round_flow={}",
                gid, pvpEnabled, syncItems, syncCutscenes,
                supportsVoting, supportsMapSelect, supportsZTarget, supportsRoundFlow);
}

void Harpoon::HandlePacket_RoomJoined(nlohmann::json payload) {
    // v2 uses snake_case (room_id / room_name / gamemode_id).
    currentRoomId       = payload.value("room_id",
                            payload.value("roomId", std::string("")));
    currentRoomName     = payload.value("room_name",
                            payload.value("roomName", std::string("")));
    currentRoomGameMode = payload.value("gamemode_id",
                            payload.value("gameMode", std::string("")));
    gameState = HARPOON_STATE_LOBBY;

    // Seed capability defaults per known gamemode, then let the local yaml
    // manifest override below. If a manifest is missing (clean install,
    // gamemode pack not yet shipped), these built-in defaults keep the right
    // generic features active so the round flow works out of the box.
    supportsVoting    = false;
    supportsMapSelect = false;
    supportsZTarget   = false;
    supportsRoundFlow = false;
    if (currentRoomGameMode == "randomizer") {
        activeGameMode = HARPOON_MODE_RANDOMIZER;
    } else if (currentRoomGameMode == "hunger_games") {
        activeGameMode = HARPOON_MODE_HUNGER_GAMES;
    } else if (currentRoomGameMode == "prop_hunt") {
        activeGameMode = HARPOON_MODE_PROP_HUNT;
        isPropHuntMode = true;
        // PH: voting + map-select + round-flow on, Z-target OFF
        // (disguised hiders shouldn't be auto-locked by seekers).
        supportsVoting    = true;
        supportsMapSelect = true;
        supportsZTarget   = false;
        supportsRoundFlow = true;
        // Lobby auto-transport: as soon as we're in a prop_hunt room, kick
        // every client into Hyrule Field as child Link with the hider preset.
        // Matches Scooter's "joining the room = entering the game" UX.
        // Roles get reassigned later when the host clicks Start Game.
        localRole = "hider";
        HarpoonPropHunt::BigStartGameAs(HarpoonPropHunt::Role::Hider);
    } else if (currentRoomGameMode == "triforce_thief") {
        // TT: voting + map-select + round-flow on, Z-target ON
        // (thieves can lock onto each other to land hits / steal).
        supportsVoting    = true;
        supportsMapSelect = true;
        supportsZTarget   = true;
        supportsRoundFlow = true;
        // Adult Link, full inventory thief preset, drop into Hyrule Field
        // as the round lobby. Round actually starts when the host confirms
        // a map (the menu's "Confirm Map" or in-overlay A button).
        HarpoonTriforceThief::BigStartGame();
    } else if (currentRoomGameMode == "rpg") {
        // RPG Mode: GM-driven roleplay. No rounds, no map vote, no auto
        // round-end. The host shapes the session via inventory templates
        // (HarpoonTemplates), per-peer movement-flag restrictions, peer
        // teleports, and host transfer. Players drop items on death (or
        // voluntarily via C-Up in the pause menu); the distributed drop
        // ledger persists across scene loads.
        supportsVoting    = false;
        supportsMapSelect = false;
        supportsZTarget   = true;
        supportsRoundFlow = false;
        // Vanilla item sync OFF — the GM controls inventory via templates
        // and vanilla diffs would clobber GM-applied loadouts.
        syncItems = false;
        // No automatic teleport — TT's BigStartGame teleports to Hyrule
        // Field with the thief preset. For RPG we let the player land
        // wherever their save was loaded; the GM can move them via the
        // "To me" / "Send to scene" buttons from the GM panel.
    }

    // Apply default_config from our locally-installed gamemode pack. The
    // server is gamemode-agnostic and never broadcasts a manifest, so without
    // this every joined room would inherit pvpEnabled's process default
    // (true) — making PvP fire even in randomizer-no-pvp rooms, and damage
    // never get filtered for receivers in coop rooms.
    ApplyLocalGamemodeManifest(currentRoomGameMode,
                               pvpEnabled, syncItems, syncCutscenes,
                               supportsVoting, supportsMapSelect,
                               supportsZTarget, supportsRoundFlow);

    // Announce our .o2r mod list once we're actually in a room — the server
    // relays room-scoped events only to room members, so this must happen
    // after room-joined (not right after handshake).
    HarpoonSkinSync::Reset();
    SendPacket_O2rModList();

    // Reset local drop ledger (last room's drops aren't ours) and ask peers
    // for a snapshot so we see drops that happened before we joined.
    HarpoonDroppedItems::ClearLedger();
    SendJsonToRemote(HarpoonDroppedItems::BuildLedgerRequestPayload());

    SPDLOG_INFO("[Harpoon] Joined room '{}' ({}) mode={} pvp={} caps[v={} m={} z={} r={}]",
                currentRoomName, currentRoomId, currentRoomGameMode, pvpEnabled,
                supportsVoting, supportsMapSelect, supportsZTarget, supportsRoundFlow);
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
    // Session-scoped PH cumulative timer resets on room leave. Next room
    // join starts the on-screen timer at 00:00 again.
    isPropHuntMode = false;
    HarpoonPropHunt::ResetRoundElapsed();
    RefreshClientActors();
    SPDLOG_INFO("[Harpoon] Left room");
}

void Harpoon::HandlePacket_RoomList(nlohmann::json payload) {
    roomList.clear();
    if (payload.contains("rooms")) {
        for (auto& roomJson : payload["rooms"]) {
            RoomInfo info;
            // v2 server emits camelCase aliases here too (Room.to_dict()).
            info.roomId      = roomJson.value("roomId",
                                   roomJson.value("room_id", std::string("")));
            info.name        = roomJson.value("name", std::string(""));
            info.gameMode    = roomJson.value("gameMode",
                                   roomJson.value("gamemode_id", std::string("")));
            info.hasPassword = roomJson.value("hasPassword",
                                   roomJson.value("has_password", false));
            info.playerCount = roomJson.value("playerCount",
                                   roomJson.value("player_count", 0));
            info.maxPlayers  = roomJson.value("maxPlayers",
                                   roomJson.value("max_players", 16));
            info.state       = roomJson.value("phase",
                                   roomJson.value("state", std::string("lobby")));
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
    SPDLOG_INFO("[Harpoon] SendPacket_RoomCreate name='{}' gameMode='{}' connected={}",
                name ? name : "(null)", gameMode ? gameMode : "(null)", isConnected);
    if (!isConnected) {
        SPDLOG_WARN("[Harpoon] SendPacket_RoomCreate: not connected — packet dropped");
        return;
    }
    if (!name || name[0] == '\0') {
        SPDLOG_WARN("[Harpoon] SendPacket_RoomCreate: empty room name — packet dropped");
        return;
    }
    if (!gameMode || gameMode[0] == '\0') {
        SPDLOG_WARN("[Harpoon] SendPacket_RoomCreate: empty gameMode — packet dropped");
        return;
    }
    nlohmann::json payload;
    payload["type"] = HPN_ROOM_CREATE;
    payload["name"] = name;
    payload["gameMode"] = gameMode;
    if (password && password[0] != '\0') {
        payload["password"] = password;
    }
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_RoomJoin(const char* roomId, const char* password) {
    nlohmann::json payload;
    payload["type"] = HPN_ROOM_JOIN;
    payload["roomId"] = roomId;       // server schema accepts both roomId and room_id
    if (password && password[0] != '\0') {
        payload["password"] = password;
    }
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_RoomLeave() {
    nlohmann::json payload;
    payload["type"] = HPN_ROOM_LEAVE;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_RoomList() {
    nlohmann::json payload;
    payload["type"] = HPN_ROOM_LIST;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_SetTeam(const char* team) {
    // v2: TEAM.ASSIGN with target=self (server defaults to sender if no target).
    nlohmann::json payload;
    payload["type"] = "TEAM.ASSIGN";
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
    // Prop Hunt seeker-vs-decoy collision. Mirrors Scooter's logic
    // (HarpoonHookHandlers.cpp:1704 there). When a seeker swings their
    // sword and is within ~50 units of a remote hider's decoy, the
    // seeker takes a 40-frame ice-freeze + the decoy is destroyed +
    // an ice VFX bursts. Decoys are stored on each remote hider's
    // HarpoonClient::somariaDecoy* fields (synced via the existing
    // COMBAT.SPAWN_DECOY / COMBAT.DESTROY_DECOY messages).
    if (!isPropHuntMode || gPlayState == nullptr) return;
    // Only seekers can trigger a decoy hit. Hiders' own decoys are not
    // authoritative — they only broadcast position and render.
    if (!HarpoonPropHunt::IsSeeker()) return;

    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) return;

    // Hit conditions — seeker triggers a decoy by ANY contact form:
    //   - swinging melee (sword/hammer/etc.) — meleeWeaponState != 0
    //   - pressing A (action) or B (item use: bombs, arrows, etc.)
    //   - just walking into the decoy (proximity within 35u)
    // The proximity radius is tighter than the swing radius (50u) so that
    // it's intentional contact, not passive flyby.
    Input* input = (gPlayState != nullptr) ? &gPlayState->state.input[0] : nullptr;
    bool swinging      = (player->meleeWeaponState != 0);
    bool pressedAttack = (input != nullptr) &&
                         CHECK_BTN_ANY(input->press.button,
                                       BTN_A | BTN_B | BTN_CLEFT | BTN_CDOWN | BTN_CRIGHT);
    bool wantSwingHit  = swinging || pressedAttack;

    Vec3f sp = player->actor.world.pos;
    // Base radii (Link-sized prop = 1.0 scale). Per-decoy radii are these
    // values multiplied by the decoy's prop scale so a tiny rupee decoy
    // can only be triggered by close contact and a big chest decoy triggers
    // from farther — matches the visible prop size on screen.
    constexpr f32 kBaseHitRadius     = 50.0f;
    constexpr f32 kBaseContactRadius = 35.0f;

    for (auto& [cid, cl] : clients) {
        if (cl.self) continue;
        if (cl.role != "hider") continue;
        if (cl.sceneNum != gPlayState->sceneNum) continue;
        for (u8 i = 0; i < 3; i++) {
            if (!cl.somariaDecoyActive[i]) continue;
            if (cl.somariaDecoyPropIdx[i] < 0) continue;
            f32 dx = sp.x - cl.somariaDecoyPos[i].x;
            f32 dy = sp.y - cl.somariaDecoyPos[i].y;
            f32 dz = sp.z - cl.somariaDecoyPos[i].z;
            f32 d2 = dx * dx + dy * dy + dz * dz;
            // Scale by the decoy's prop visual scale, clamped to a sane
            // band. Same clamps as HarpoonDummyPlayer's cylinder sizing.
            s32 dMap = confirmedMapIndex; if (dMap < 0) dMap = 0;
            f32 ds = HarpoonPropHunt::GetPropVisualScale(
                cl.somariaDecoyPropCat[i], cl.somariaDecoyPropIdx[i],
                cl.somariaDecoyPropState[i], dMap);
            if (ds < 0.3f) ds = 0.3f;
            if (ds > 2.5f) ds = 2.5f;
            f32 hitR     = kBaseHitRadius     * ds;
            f32 contactR = kBaseContactRadius * ds;
            f32 hitR2     = hitR * hitR;
            f32 contactR2 = contactR * contactR;
            // Trigger when:
            //   (a) within attack radius AND swinging/pressing attack, OR
            //   (b) within contact radius (walked into it, any state).
            bool hit = (d2 < contactR2) ||
                       (wantSwingHit && d2 < hitR2);
            if (!hit) continue;

            // Hit! Ice shatter at decoy position, freeze seeker, kill decoy.
            Vec3f hitPos = cl.somariaDecoyPos[i];
            Vec3f zv = { 0.0f, 0.0f, 0.0f };
            EffectSsDeadDb_Spawn(gPlayState, &hitPos, &zv, &zv,
                                 100, 10, 150, 200, 255, 200, 100, 150, 255, 0, 14, 1);
            Color_RGBA8 icP = { 200, 230, 255, 220 };
            Color_RGBA8 icE = { 100, 150, 255, 160 };
            for (int j = 0; j < 8; j++) {
                Vec3f pp = hitPos;
                pp.x += Rand_CenteredFloat(30.0f);
                pp.y += Rand_ZeroFloat(40.0f);
                pp.z += Rand_CenteredFloat(30.0f);
                Vec3f pv = { Rand_CenteredFloat(3.0f),
                             Rand_ZeroFloat(4.0f) + 1.0f,
                             Rand_CenteredFloat(3.0f) };
                EffectSsKiraKira_SpawnFocused(gPlayState, &pp, &pv, &zv, &icP, &icE, 800, 30);
            }
            Audio_PlaySoundGeneral(NA_SE_IT_SHIELD_REFLECT_SW, &hitPos, 4,
                                   &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultReverb);

            // Local kill — peer will resync on next SPAWN_DECOY broadcast.
            cl.somariaDecoyActive[i] = 0;
            if (cl.somariaDecoyCount > 0) cl.somariaDecoyCount--;

            // Freeze the seeker (us) as the penalty.
            player->actor.freezeTimer = 40;
            Actor_SetColorFilter(&player->actor, 0x4000, 0xFF, 0, 40);
            Audio_PlayActorSound2(&player->actor, NA_SE_PL_FREEZE_S);

            // Ice encasing VFX bursting around the frozen seeker.
            Vec3f seekerCenter = player->actor.world.pos;
            seekerCenter.y += 30.0f;
            EffectSsIcePiece_SpawnBurst(gPlayState, &seekerCenter, 0.8f);

            // Tell the hider their decoy got triggered (so they can VFX +
            // mark it dead in their local ring). v2 uses COMBAT.DECOY_HIT.
            nlohmann::json payload;
            payload["type"]           = HPN_COMBAT_DECOY_HIT;
            payload["targetClientId"] = cid;
            payload["decoySlot"]      = (s32)i;
            SendJsonToRemote(payload);

            return;  // one hit per frame is enough
        }
    }
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
    // PvP gate is the per-room `pvpEnabled` flag. The previous gate
    // (Harpoon_IsPvpActive) blocked sending unless gameState left LOBBY,
    // which only Prop Hunt manipulates — randomizer-pvp rooms stay in LOBBY
    // forever, so custom damage never transmitted there. Receiver still
    // honours pvpEnabled in HandlePacket_Damage so a no-pvp room ignores
    // the inbound damage even if a buggy sender transmits.
    if (!Harpoon::Instance->isConnected)
        return;
    if (!Harpoon::Instance->pvpEnabled)
        return;
    if (!Harpoon_IsDummyPlayer(hitActor))
        return;

    uint32_t clientId = Harpoon::Instance->GetDummyPlayerClientId(hitActor);
    if (clientId == 0)
        return;

    Player* localPlayer = GET_PLAYER(gPlayState);

    nlohmann::json payload;
    payload["type"] = Harpoon::HPN_COMBAT_DAMAGE;  // v2: COMBAT.DEAL_DAMAGE
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
    // Same PvP gate logic as Harpoon_SendCustomDamage — pvpEnabled (per-room)
    // instead of gameState (per-gamemode). See comment above.
    if (!Harpoon::Instance->isConnected)
        return;
    if (!Harpoon::Instance->pvpEnabled)
        return;
    if (!Harpoon_IsDummyPlayer(hitActor))
        return;

    uint32_t clientId = Harpoon::Instance->GetDummyPlayerClientId(hitActor);
    if (clientId == 0)
        return;

    nlohmann::json payload;
    payload["type"] = Harpoon::HPN_COMBAT_CUSTOM_EFFECT;  // v2: COMBAT.CUSTOM_EFFECT
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

void Harpoon_NotifyVfxSpawn(Actor* spawned, s32 vfxKindCode, u8 attachedToOwner) {
    if (Harpoon::Instance == nullptr || !Harpoon::Instance->isConnected) {
        return;
    }
    if (spawned == nullptr) return;

    // Map enum → string. Receiver doesn't need this for the spawn itself,
    // it's a tag for client-side filtering.
    const char* kind = "generic";
    switch (vfxKindCode) {
        case HARPOON_VFX_KIND_SW97_ARROW_FIRE:  kind = "sw97_arrow_fire";  break;
        case HARPOON_VFX_KIND_SW97_ARROW_ICE:   kind = "sw97_arrow_ice";   break;
        case HARPOON_VFX_KIND_SW97_ARROW_LIGHT: kind = "sw97_arrow_light"; break;
        case HARPOON_VFX_KIND_SW97_ARROW_DARK:  kind = "sw97_arrow_dark";  break;
        case HARPOON_VFX_KIND_SW97_ARROW_SOUL:  kind = "sw97_arrow_soul";  break;
        case HARPOON_VFX_KIND_SW97_ARROW_WIND:  kind = "sw97_arrow_wind";  break;
        case HARPOON_VFX_KIND_SW97_MAGIC_FIRE:  kind = "sw97_magic_fire";  break;
        case HARPOON_VFX_KIND_SW97_MAGIC_ICE:   kind = "sw97_magic_ice";   break;
        case HARPOON_VFX_KIND_SW97_MAGIC_LIGHT: kind = "sw97_magic_light"; break;
        case HARPOON_VFX_KIND_SW97_MAGIC_DARK:  kind = "sw97_magic_dark";  break;
        case HARPOON_VFX_KIND_SW97_MAGIC_SOUL:  kind = "sw97_magic_soul";  break;
        case HARPOON_VFX_KIND_SW97_MAGIC_WIND:  kind = "sw97_magic_wind";  break;
        case HARPOON_VFX_KIND_FD_BEAM:          kind = "fd_beam";          break;
        case HARPOON_VFX_KIND_ZORA_FIN:         kind = "zora_fin";         break;
        case HARPOON_VFX_KIND_DEKU_BUBBLE:      kind = "deku_bubble";      break;
        case HARPOON_VFX_KIND_GORON_ROCK:       kind = "goron_rock";       break;
        case HARPOON_VFX_KIND_HYLIAS_FAIRY:     kind = "hylias_fairy";     break;
        default: break;
    }

    // Tag the locally-spawned actor so its hits route through PvP.
    Harpoon::Instance->SetVfxActorOwner(spawned, Harpoon::Instance->ownClientId);

    Harpoon::Instance->SendPacket_SpawnVfxActor(
        spawned->id,
        spawned->world.pos.x, spawned->world.pos.y, spawned->world.pos.z,
        spawned->world.rot.x, spawned->world.rot.y, spawned->world.rot.z,
        spawned->params, kind, attachedToOwner != 0);
}

} // extern "C"

// MARK: - Skin sync handlers

void Harpoon::HandlePacket_O2rModList(nlohmann::json payload) {
    if (!payload.contains("clientId")) {
        SPDLOG_INFO("[Harpoon] HandlePacket_O2rModList: dropped (no clientId in payload)");
        return;
    }
    uint32_t clientId = payload["clientId"].get<uint32_t>();
    if (!clients.contains(clientId)) {
        SPDLOG_INFO("[Harpoon] HandlePacket_O2rModList: dropped (clientId={} not in clients map)", clientId);
        return;
    }
    auto& client = clients[clientId];

    client.enabledO2rMods.clear();
    std::string modsStr;
    if (payload.contains("mods") && payload["mods"].is_array()) {
        for (const auto& m : payload["mods"]) {
            if (m.is_string()) {
                client.enabledO2rMods.push_back(m.get<std::string>());
                if (!modsStr.empty()) modsStr += ", ";
                modsStr += m.get<std::string>();
            }
        }
    }

    client.harpoonSyncMods.clear();
    std::string syncStr;
    if (payload.contains("syncMods") && payload["syncMods"].is_array()) {
        for (const auto& m : payload["syncMods"]) {
            if (m.is_string()) {
                client.harpoonSyncMods.push_back(m.get<std::string>());
                if (!syncStr.empty()) syncStr += ", ";
                syncStr += m.get<std::string>();
            }
        }
    }
    SPDLOG_INFO("[Harpoon] HandlePacket_O2rModList: client='{}' (id={}) {} mods=[{}] sync=[{}]",
                client.name, clientId, (int)client.enabledO2rMods.size(), modsStr, syncStr);

    // Divergence check (dedupe happens inside NotifyO2rDivergence via HarpoonSkinSync's set).
    HarpoonSkinSync::NotifyO2rDivergence(clientId, client.name, client.enabledO2rMods,
                                         client.harpoonSyncMods);
}

// ============================================================================
// MARK: - Harpoon v2 protocol — connection lifecycle handlers
// ============================================================================

void Harpoon::HandlePacket_HandshakeAck(nlohmann::json payload) {
    if (payload.contains("client_id")) {
        ownClientId = payload["client_id"].get<uint32_t>();
    } else if (payload.contains("clientId")) {
        ownClientId = payload["clientId"].get<uint32_t>();
    }
    if (payload.contains("session_token")) {
        sessionToken = payload["session_token"].get<std::string>();
    } else if (payload.contains("sessionToken")) {
        sessionToken = payload["sessionToken"].get<std::string>();
    }
    SPDLOG_INFO("[Harpoon] HANDSHAKE_ACK ownClientId={} token={}",
                ownClientId, sessionToken.substr(0, 8) + "…");
}

void Harpoon::HandlePacket_Error(nlohmann::json payload) {
    std::string code = payload.value("code", std::string("unknown"));
    std::string message = payload.value("message", std::string(""));
    SPDLOG_WARN("[Harpoon] server error: code={} message={}", code, message);
    killFeed.push_back("[server] " + code + ": " + message);
    if (killFeed.size() > 5) killFeed.erase(killFeed.begin());
    // Surface as a toast too so users see it outside the in-room view (e.g.
    // when ROOM.CREATE is rejected and the user is still on the lobby screen
    // — kill feed only renders inside a room).
    Notification::Emit({
        .prefix = "Harpoon error",
        .message = code + (message.empty() ? "" : " — " + message),
        .remainingTime = 6.0f,
    });
}

void Harpoon::HandlePacket_GamemodeManifest(nlohmann::json payload) {
    if (!payload.contains("manifest") || !payload["manifest"].is_object()) return;
    currentGamemodeManifest = payload["manifest"];
    std::string gid = currentGamemodeManifest.value("gamemode_id", std::string("?"));
    std::string name = currentGamemodeManifest.value("name", gid);
    SPDLOG_INFO("[Harpoon] received gamemode manifest: id={} name='{}'", gid, name);

    // Verify the pack is actually installed locally. Joining a room whose
    // gamemode we don't have means we can't load its assets (save preset,
    // maps, prop tables, world modifications), so bail out gracefully.
    auto installed = HarpoonSkinSync::GetInstalledGamemodes();
    bool found = false;
    for (const auto& g : installed) {
        if (g == gid) { found = true; break; }
    }
    if (!found) {
        SPDLOG_WARN("[Harpoon] room uses gamemode '{}' which is not installed locally — leaving room", gid);
        // Throttle: only notify the user once per missing gid per session.
        // Without this every spurious manifest broadcast (room list refresh,
        // re-join after auto-leave, etc.) would stack a duplicate toast.
        static std::set<std::string> alreadyWarnedMissing;
        if (alreadyWarnedMissing.insert(gid).second) {
            std::string msg = "Gamemode '" + gid + "' not installed — drop the pack into harpoon/gamemodes/";
            killFeed.push_back(msg);
            if (killFeed.size() > 5) killFeed.erase(killFeed.begin());
            Notification::Emit({
                .prefix = "Harpoon",
                .message = msg,
            });
        }
        SendPacket_RoomLeave();
        return;
    }

    // Apply gamemode-driven defaults. The manifest's `default_config` is the
    // authoritative source for sync_items / pvp_enabled — they're a property
    // of the gamemode, not of the player. The user shouldn't be able to
    // disable PvP in a Hunger Games room or enable item sync in a Story room.
    if (currentGamemodeManifest.contains("default_config") &&
        currentGamemodeManifest["default_config"].is_object()) {
        const auto& cfg = currentGamemodeManifest["default_config"];
        if (cfg.contains("sync_items")) {
            syncItems = cfg["sync_items"].get<bool>();
            SPDLOG_INFO("[Harpoon] gamemode '{}' sets sync_items={}", gid, syncItems);
        }
        if (cfg.contains("pvp_enabled")) {
            pvpEnabled = cfg["pvp_enabled"].get<bool>();
            SPDLOG_INFO("[Harpoon] gamemode '{}' sets pvp_enabled={}", gid, pvpEnabled);
        }
        if (cfg.contains("sync_cutscenes")) {
            syncCutscenes = cfg["sync_cutscenes"].get<bool>();
            SPDLOG_INFO("[Harpoon] gamemode '{}' sets sync_cutscenes={}", gid, syncCutscenes);
        }
    }
}

void Harpoon::HandlePacket_RoomEvent(nlohmann::json payload) {
    // Generic broadcast event from another client (ROOM.BROADCAST_EVENT).
    // Dispatch by event-name prefix so gamemodes can carry their own
    // sub-protocol on top of the relay channel.
    std::string eventName = payload.value("event_name", std::string(""));
    if (eventName.empty() && payload.contains("event")) {
        eventName = payload.value("event", std::string(""));
    }
    if (eventName.rfind("PROP_HUNT.", 0) == 0) {
        HarpoonPropHunt::HandleEvent(payload);
        return;
    }
    if (eventName.rfind("TRIFORCE_THIEF.", 0) == 0) {
        HarpoonTriforceThief::HandleEvent(payload);
        return;
    }
    // Generic HARPOON.* sub-protocol — dropped-item ledger, GM controls.
    if (eventName.rfind("HARPOON.", 0) == 0) {
        const nlohmann::json& data = payload.contains("data") && payload["data"].is_object()
                                       ? payload["data"] : payload;
        if      (eventName == "HARPOON.DEATH_DROP")            HarpoonDroppedItems::HandleDeathDrop(data);
        else if (eventName == "HARPOON.DROP_CLAIM")            HarpoonDroppedItems::HandleDropClaim(data);
        else if (eventName == "HARPOON.DROP_LEDGER_REQ")       HarpoonDroppedItems::HandleLedgerRequest(payload);
        else if (eventName == "HARPOON.DROP_LEDGER_SNAPSHOT")  HarpoonDroppedItems::HandleLedgerSnapshot(data);
        else if (eventName == "HARPOON.TEMPLATE_APPLY")        HarpoonTemplates::HandleTemplateApply(data);
        else if (eventName == "HARPOON.FLAG_OVERRIDE")         HandleHarpoonFlagOverride(data);
        else if (eventName == "HARPOON.PEER_TELEPORT")         HandleHarpoonPeerTeleport(data);
        else if (eventName == "HARPOON.HOST_TRANSFER")         HandleHarpoonHostTransfer(data);
        else SPDLOG_DEBUG("[Harpoon] HARPOON.* unknown event {}", eventName);
        return;
    }
    SPDLOG_DEBUG("[Harpoon] ROOM.EVENT name={} (ignored)", eventName);
}

void Harpoon::HandlePacket_PhaseChanged(nlohmann::json payload) {
    std::string phase = payload.value("phase", std::string("lobby"));
    SPDLOG_INFO("[Harpoon] phase -> {}", phase);
}

// ----------------------------------------------------------------------------
// GM event handlers
// ----------------------------------------------------------------------------

void Harpoon::HandleHarpoonFlagOverride(const nlohmann::json& data) {
    // Update the per-peer restrict bools so every client's GM panel
    // shows the same state. The TARGET client also applies them to the
    // engine each frame (see HookHandlers OnPlayerUpdate).
    uint32_t target = data.value("targetClientId", 0u);
    if (target == 0) return;
    auto it = clients.find(target);
    if (it == clients.end()) return;
    it->second.restrictNoClimb = data.value("noClimb", false);
    it->second.restrictNoGrab  = data.value("noGrab",  false);
    it->second.restrictNoCrawl = data.value("noCrawl", false);
    it->second.restrictNoTalk  = data.value("noTalk",  false);
}

void Harpoon::HandleHarpoonPeerTeleport(const nlohmann::json& data) {
    // Only the targeted peer reacts.
    uint32_t target = data.value("targetClientId", 0u);
    if (target != ownClientId) return;
    s32 entrance = data.value("entranceIndex", -1);
    f32 px = data.value("x", 0.0f);
    f32 py = data.value("y", 0.0f);
    f32 pz = data.value("z", 0.0f);
    bool toHostPos = data.value("toHostPos", false);

    if (gPlayState == nullptr) return;
    if (entrance >= 0) {
        gPlayState->linkAgeOnLoad     = gSaveContext.linkAge;
        gPlayState->nextEntranceIndex = entrance;
        gPlayState->transitionTrigger = TRANS_TRIGGER_START;
        gPlayState->transitionType    = TRANS_TYPE_FADE_BLACK;
        ::sHarpoonAuthorizedTransition = true;
        if (toHostPos) {
            // Land on host's exact position after the transition.
            gSaveContext.respawnFlag = 1;
            gSaveContext.respawn[RESPAWN_MODE_DOWN].entranceIndex = entrance;
            gSaveContext.respawn[RESPAWN_MODE_DOWN].pos = { px, py, pz };
        }
    } else {
        // In-place teleport (same scene).
        Player* lp = GET_PLAYER(gPlayState);
        if (lp != nullptr) {
            lp->actor.world.pos = { px, py, pz };
            lp->actor.home.pos  = { px, py, pz };
            lp->actor.velocity.x = lp->actor.velocity.y = lp->actor.velocity.z = 0.0f;
            lp->linearVelocity   = 0.0f;
        }
    }
    SPDLOG_INFO("[Harpoon][GM] peer teleport received: entrance={} toHostPos={}",
                entrance, toHostPos);
}

void Harpoon::HandleHarpoonHostTransfer(const nlohmann::json& data) {
    uint32_t newHost = data.value("newHostClientId", 0u);
    if (newHost == 0) return;
    hostClientId = newHost;
    SPDLOG_INFO("[Harpoon][GM] host transferred to cid={}", newHost);
}

// ============================================================================
// MARK: - Harpoon v2 — granular PLAYER.UPDATE_* receivers
//
// These each populate a slice of the HarpoonClient struct so the dummy player
// renders correctly even when the remote sends granular updates instead of
// a single PLAYER.UPDATE_FULL_STATE blob.
// ============================================================================

static HarpoonClient* _LookupClient(nlohmann::json& payload) {
    if (!payload.contains("clientId") && !payload.contains("source"))
        return nullptr;
    uint32_t clientId = payload.contains("clientId")
                         ? payload["clientId"].get<uint32_t>()
                         : payload["source"].get<uint32_t>();
    auto& clients = Harpoon::Instance->clients;
    auto it = clients.find(clientId);
    return it == clients.end() ? nullptr : &it->second;
}

void Harpoon::HandlePacket_PlayerTransform(nlohmann::json payload) {
    auto* c = _LookupClient(payload);
    if (!c) return;
    if (payload.contains("posRot")) {
        auto& pr = payload["posRot"];
        if (pr.contains("pos")) {
            c->posRot.pos.x = pr["pos"].value("x", 0.0f);
            c->posRot.pos.y = pr["pos"].value("y", 0.0f);
            c->posRot.pos.z = pr["pos"].value("z", 0.0f);
        }
        if (pr.contains("rot")) {
            c->posRot.rot.x = pr["rot"].value("x", (s16)0);
            c->posRot.rot.y = pr["rot"].value("y", (s16)0);
            c->posRot.rot.z = pr["rot"].value("z", (s16)0);
        }
        // Trigger deferred spawn if we now have a real position.
        bool hasPos = (c->posRot.pos.x != 0.0f || c->posRot.pos.y != 0.0f ||
                       c->posRot.pos.z != 0.0f);
        if (c->player == nullptr && hasPos && c->online) {
            shouldRefreshActors = true;
        }
    }
    if (payload.contains("prevTransl")) {
        c->prevTransl.x = payload["prevTransl"].value("x", (s16)0);
        c->prevTransl.y = payload["prevTransl"].value("y", (s16)0);
        c->prevTransl.z = payload["prevTransl"].value("z", (s16)0);
    }
    c->movementFlags = payload.value("movementFlags", c->movementFlags);
}

void Harpoon::HandlePacket_PlayerSkeleton(nlohmann::json payload) {
    auto* c = _LookupClient(payload);
    if (!c) return;
    auto jointArray = payload.value("jointTable", std::vector<int>{});
    jointArray.resize(24 * 3);
    for (int i = 0; i < 24; i++) {
        c->jointTable[i].x = jointArray[i * 3];
        c->jointTable[i].y = jointArray[i * 3 + 1];
        c->jointTable[i].z = jointArray[i * 3 + 2];
    }
    c->movementFlags = payload.value("movementFlags", c->movementFlags);
}

void Harpoon::HandlePacket_PlayerLimbRotations(nlohmann::json payload) {
    auto* c = _LookupClient(payload);
    if (!c) return;
    if (payload.contains("upperLimbRot")) {
        c->upperLimbRot.x = payload["upperLimbRot"].value("x", (s16)0);
        c->upperLimbRot.y = payload["upperLimbRot"].value("y", (s16)0);
        c->upperLimbRot.z = payload["upperLimbRot"].value("z", (s16)0);
    }
    c->headLimbRot.x = payload.value("headLimbRotX", c->headLimbRot.x);
    c->headLimbRot.y = payload.value("headLimbRotY", c->headLimbRot.y);
    c->headLimbRot.z = payload.value("headLimbRotZ", c->headLimbRot.z);
    c->upperLimbYawSecondary = payload.value("upperLimbYawSecondary", c->upperLimbYawSecondary);
}

void Harpoon::HandlePacket_PlayerAnimationFlags(nlohmann::json payload) {
    auto* c = _LookupClient(payload);
    if (!c) return;
    c->stateFlags1 = payload.value("stateFlags1", c->stateFlags1);
    c->stateFlags2 = payload.value("stateFlags2", c->stateFlags2);
    c->actionVar1 = payload.value("actionVar1", c->actionVar1);
    c->modelGroup = payload.value("modelGroup", c->modelGroup);
}

void Harpoon::HandlePacket_PlayerMotionVars(nlohmann::json payload) {
    auto* c = _LookupClient(payload);
    if (!c) return;
    c->speedXZ = payload.value("speedXZ", c->speedXZ);
    c->meleeWeaponState = payload.value("meleeWeaponState", c->meleeWeaponState);
    c->fpModeFlag = payload.value("fpModeFlag", c->fpModeFlag);
}

void Harpoon::HandlePacket_PlayerBowState(nlohmann::json payload) {
    auto* c = _LookupClient(payload);
    if (!c) return;
    c->bowStringDraw = payload.value("bowStringDraw", c->bowStringDraw);
    c->bowArrowState = payload.value("bowArrowState", c->bowArrowState);
    c->bowDrawAnimFrame = payload.value("bowDrawAnimFrame", c->bowDrawAnimFrame);
}

void Harpoon::HandlePacket_PlayerHandTypes(nlohmann::json payload) {
    auto* c = _LookupClient(payload);
    if (!c) return;
    c->leftHandType = payload.value("leftHandType", c->leftHandType);
    c->rightHandType = payload.value("rightHandType", c->rightHandType);
    c->sheathType = payload.value("sheathType", c->sheathType);
}

void Harpoon::HandlePacket_PlayerVisualState(nlohmann::json payload) {
    auto* c = _LookupClient(payload);
    if (!c) return;
    s16 newScene = payload.value("sceneNum", c->sceneNum);
    s32 newAge   = payload.value("linkAge",  c->linkAge);
    bool newSaveLoaded = payload.value("isSaveLoaded", c->isSaveLoaded);
    // Spawn or kill the teammate's dummy when their scene/age/save-loaded
    // status changes. Without this, joining a teammate already in a scene or
    // walking into their scene later wouldn't trigger the dummy spawn — they
    // stayed invisible even though their TRANSFORM packets were arriving.
    if (newScene != c->sceneNum || newAge != c->linkAge ||
        newSaveLoaded != c->isSaveLoaded) {
        SPDLOG_INFO("[Harpoon] VisualState cid={} scene {}->{} age {}->{} saveLoaded {}->{}",
                    c->clientId, c->sceneNum, newScene, c->linkAge, newAge,
                    (int)c->isSaveLoaded, (int)newSaveLoaded);
        shouldRefreshActors = true;
    }
    c->isSaveLoaded   = newSaveLoaded;
    c->sceneNum       = newScene;
    c->entranceIndex  = payload.value("entranceIndex", c->entranceIndex);
    c->linkAge        = newAge;
}

void Harpoon::HandlePacket_PlayerEquipVisible(nlohmann::json payload) {
    auto* c = _LookupClient(payload);
    if (!c) return;
    c->currentBoots = payload.value("currentBoots", c->currentBoots);
    c->currentShield = payload.value("currentShield", c->currentShield);
    c->currentTunic = payload.value("currentTunic", c->currentTunic);
    c->buttonItem0 = payload.value("buttonItem0", c->buttonItem0);
    c->itemAction = payload.value("itemAction", c->itemAction);
    c->heldItemAction = payload.value("heldItemAction", c->heldItemAction);
    c->currentMask = payload.value("currentMask", c->currentMask);
    c->wornMask = payload.value("wornMask", c->wornMask);
}

void Harpoon::HandlePacket_PlayerFace(nlohmann::json payload) {
    auto* c = _LookupClient(payload);
    if (!c) return;
    c->face = payload.value("face", c->face);
    c->eyeIndex = payload.value("eyeIndex", c->eyeIndex);
}

void Harpoon::HandlePacket_PlayerScale(nlohmann::json payload) {
    auto* c = _LookupClient(payload);
    if (!c) return;
    c->scaleX = payload.value("scaleX", c->scaleX);
    c->scaleY = payload.value("scaleY", c->scaleY);
    c->scaleZ = payload.value("scaleZ", c->scaleZ);
}

void Harpoon::HandlePacket_PlayerTransformation(nlohmann::json payload) {
    auto* c = _LookupClient(payload);
    if (!c) return;
    c->transformation = payload.value("transformation", c->transformation);
    c->cylRadius = payload.value("cylRadius", c->cylRadius);
    c->cylHeight = payload.value("cylHeight", c->cylHeight);
    c->cylYShift = payload.value("cylYShift", c->cylYShift);
    c->mmStateFlags3 = payload.value("mmStateFlags3", c->mmStateFlags3);
    c->mmSpeedXZ = payload.value("mmSpeedXZ", c->mmSpeedXZ);
}

void Harpoon::HandlePacket_PlayerGoronState(nlohmann::json payload) {
    auto* c = _LookupClient(payload);
    if (!c) return;
    c->goronAction = payload.value("goronAction", c->goronAction);
    c->rollSquash = payload.value("rollSquash", c->rollSquash);
    c->rollSpikeActive = payload.value("rollSpikeActive", c->rollSpikeActive);
    c->rollChargeLevel = payload.value("rollChargeLevel", c->rollChargeLevel);
}

void Harpoon::HandlePacket_PlayerCustomItemState(nlohmann::json payload) {
    auto* c = _LookupClient(payload);
    if (!c) return;
    // Custom items: unwrap a single-item payload that names the item via
    // item_id/itemId and contains the rest of its state. Same handler logic
    // as the legacy big-blob update — just smaller scope.
    HandlePacket_PlayerUpdate(payload);
}

void Harpoon::HandlePacket_PlayerInvincibility(nlohmann::json payload) {
    auto* c = _LookupClient(payload);
    if (!c) return;
    c->invincibilityTimer = payload.value("value", c->invincibilityTimer);
}

void Harpoon::HandlePacket_PlayerKill(nlohmann::json payload) {
    HandlePacket_PlayerDied(payload);
}

void Harpoon::HandlePacket_PlayerFullState(nlohmann::json payload) {
    // Backwards-compat: full per-frame blob. Reuses the old handler that
    // already knows how to populate every field of HarpoonClient.
    HandlePacket_PlayerUpdate(payload);
}

void Harpoon::HandlePacket_SkinSyncAnnounceCatalog(nlohmann::json payload) {
    // Renamed from PVP_O2R_MOD_LIST. Server uses the same payload fields
    // (mods, syncMods) thanks to the schema's `populate_by_name`.
    HandlePacket_O2rModList(payload);
}

void Harpoon::HandlePacket_SkinSyncUpdateSlots(nlohmann::json payload) {
    auto* c = _LookupClient(payload);
    if (!c) return;
    // Either flat fields (legacy) or { slots: { adult, child, equipment, forced } }
    std::string adult, child, equip, forced;
    if (payload.contains("slots") && payload["slots"].is_object()) {
        adult = payload["slots"].value("adult", std::string(""));
        child = payload["slots"].value("child", std::string(""));
        equip = payload["slots"].value("equipment", std::string(""));
        forced = payload["slots"].value("forced", std::string(""));
    } else {
        adult = payload.value("adultSkin", std::string(""));
        child = payload.value("childSkin", std::string(""));
        equip = payload.value("equipSkin", std::string(""));
        forced = payload.value("forcedSkin", std::string(""));
    }
    if (adult != c->adultSkinName || child != c->childSkinName || equip != c->equipSkinName ||
        forced != c->forcedSkinName) {
        SPDLOG_INFO("[Harpoon] SKIN slots updated for '{}' (id={}): adult='{}' child='{}' equip='{}' forced='{}'",
                    c->name, c->clientId, adult, child, equip, forced);
    }
    c->adultSkinName = adult;
    c->childSkinName = child;
    c->equipSkinName = equip;
    c->forcedSkinName = forced;
}

// ============================================================================
// MARK: - Harpoon v2 — granular PLAYER.UPDATE_* senders
//
// These read from the local Player object and emit ONE primitive each. By
// default `SendPacket_PlayerUpdate` (the public one called by the per-frame
// hook) bundles everything into PLAYER.UPDATE_FULL_STATE for efficiency, but
// the granular methods are available for code that wants finer control or
// for testing per-primitive rate limits.
// ============================================================================

void Harpoon::SendPacket_Resume(const std::string& token) {
    nlohmann::json payload;
    payload["type"] = HPN_RESUME;
    payload["session_token"] = token;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_PlayerTransform() {
    if (!IsSaveLoaded()) return;
    Player* player = GET_PLAYER(gPlayState);
    nlohmann::json payload;
    payload["type"] = HPN_PLAYER_TRANSFORM;
    payload["posRot"]["pos"] = { { "x", player->actor.world.pos.x },
                                 { "y", player->actor.world.pos.y },
                                 { "z", player->actor.world.pos.z } };
    payload["posRot"]["rot"] = { { "x", player->actor.shape.rot.x },
                                 { "y", player->actor.shape.rot.y },
                                 { "z", player->actor.shape.rot.z } };
    payload["prevTransl"] = { { "x", player->skelAnime.prevTransl.x },
                              { "y", player->skelAnime.prevTransl.y },
                              { "z", player->skelAnime.prevTransl.z } };
    payload["movementFlags"] = player->skelAnime.movementFlags;
    payload["quiet"] = true;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_PlayerSkeleton() {
    if (!IsSaveLoaded()) return;
    Player* player = GET_PLAYER(gPlayState);
    nlohmann::json payload;
    payload["type"] = HPN_PLAYER_SKELETON;

    Vec3s* srcJointTable = player->skelAnime.jointTable;
    s32 srcJointCount = 24;
    u8 modelType = TransformMasks_GetModelType();
    if (modelType > 0) {
        Vec3s* mmJoints = TransformMasks_GetFormJointTable();
        s32 mmCount = TransformMasks_GetFormJointCount();
        if (mmJoints != NULL && mmCount > 0) { srcJointTable = mmJoints; srcJointCount = mmCount; }
    }
    std::vector<int> jointArray;
    for (s32 i = 0; i < 24; i++) {
        if (i < srcJointCount && srcJointTable != NULL) {
            jointArray.push_back(srcJointTable[i].x);
            jointArray.push_back(srcJointTable[i].y);
            jointArray.push_back(srcJointTable[i].z);
        } else {
            jointArray.push_back(0); jointArray.push_back(0); jointArray.push_back(0);
        }
    }
    payload["jointTable"] = jointArray;
    payload["movementFlags"] = player->skelAnime.movementFlags;
    payload["quiet"] = true;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_PlayerLimbRotations() {
    if (!IsSaveLoaded()) return;
    Player* player = GET_PLAYER(gPlayState);
    nlohmann::json payload;
    payload["type"] = HPN_PLAYER_LIMB_ROT;
    payload["upperLimbRot"] = { { "x", player->upperLimbRot.x },
                                { "y", player->upperLimbRot.y },
                                { "z", player->upperLimbRot.z } };
    payload["headLimbRotX"] = player->headLimbRot.x;
    payload["headLimbRotY"] = player->headLimbRot.y;
    payload["headLimbRotZ"] = player->headLimbRot.z;
    payload["upperLimbYawSecondary"] = player->upperLimbYawSecondary;
    payload["quiet"] = true;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_PlayerAnimationFlags() {
    if (!IsSaveLoaded()) return;
    Player* player = GET_PLAYER(gPlayState);
    nlohmann::json payload;
    payload["type"] = HPN_PLAYER_ANIM_FLAGS;
    payload["stateFlags1"] = player->stateFlags1;
    payload["stateFlags2"] = player->stateFlags2 & ~PLAYER_STATE2_DISABLE_DRAW;
    payload["actionVar1"] = player->av1.actionVar1;
    payload["modelGroup"] = player->modelGroup;
    payload["quiet"] = true;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_PlayerMotionVars() {
    if (!IsSaveLoaded()) return;
    Player* player = GET_PLAYER(gPlayState);
    nlohmann::json payload;
    payload["type"] = HPN_PLAYER_MOTION_VARS;
    payload["speedXZ"] = player->actor.speedXZ;
    payload["meleeWeaponState"] = player->meleeWeaponState;
    payload["fpModeFlag"] = player->unk_6AD;
    payload["quiet"] = true;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_PlayerBowState() {
    if (!IsSaveLoaded()) return;
    Player* player = GET_PLAYER(gPlayState);
    nlohmann::json payload;
    payload["type"] = HPN_PLAYER_BOW_STATE;
    payload["bowStringDraw"] = player->unk_858;
    payload["bowArrowState"] = player->unk_860;
    payload["bowDrawAnimFrame"] = player->unk_834;
    payload["quiet"] = true;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_PlayerHandTypes() {
    if (!IsSaveLoaded()) return;
    Player* player = GET_PLAYER(gPlayState);
    nlohmann::json payload;
    payload["type"] = HPN_PLAYER_HAND_TYPES;
    payload["leftHandType"] = player->leftHandType;
    payload["rightHandType"] = player->rightHandType;
    payload["sheathType"] = player->sheathType;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_PlayerVisualState() {
    if (!IsSaveLoaded()) return;
    nlohmann::json payload;
    payload["type"] = HPN_PLAYER_VISUAL_STATE;
    payload["isSaveLoaded"] = true;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["entranceIndex"] = gSaveContext.entranceIndex;
    payload["linkAge"] = gSaveContext.linkAge;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_PlayerEquipVisible() {
    if (!IsSaveLoaded()) return;
    Player* player = GET_PLAYER(gPlayState);
    nlohmann::json payload;
    payload["type"] = HPN_PLAYER_EQUIP_VISIBLE;
    payload["currentBoots"] = player->currentBoots;
    payload["currentShield"] = player->currentShield;
    payload["currentTunic"] = player->currentTunic;
    payload["buttonItem0"] = gSaveContext.equips.buttonItems[0];
    payload["itemAction"] = player->itemAction;
    payload["heldItemAction"] = player->heldItemAction;
    payload["currentMask"] = player->currentMask;
    payload["wornMask"] = TransformMasks_WearGetCurrent();
    payload["quiet"] = true;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_PlayerFace() {
    if (!IsSaveLoaded()) return;
    Player* player = GET_PLAYER(gPlayState);
    nlohmann::json payload;
    payload["type"] = HPN_PLAYER_FACE;
    payload["face"] = player->actor.shape.face;
    payload["quiet"] = true;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_PlayerScale() {
    if (!IsSaveLoaded()) return;
    Player* player = GET_PLAYER(gPlayState);
    nlohmann::json payload;
    payload["type"] = HPN_PLAYER_SCALE;
    payload["scaleX"] = player->actor.scale.x;
    payload["scaleY"] = player->actor.scale.y;
    payload["scaleZ"] = player->actor.scale.z;
    payload["quiet"] = true;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_PlayerTransformation() {
    if (!IsSaveLoaded()) return;
    Player* player = GET_PLAYER(gPlayState);
    nlohmann::json payload;
    payload["type"] = HPN_PLAYER_TRANSFORMATION;
    u8 modelType = TransformMasks_GetModelType();
    payload["transformation"] = modelType;
    payload["cylRadius"] = player->cylinder.dim.radius;
    payload["cylHeight"] = player->cylinder.dim.height;
    payload["cylYShift"] = player->cylinder.dim.yShift;
    payload["mmStateFlags3"] = TransformMasks_GetMmStateFlags3();
    payload["mmSpeedXZ"] = TransformMasks_GetMmSpeedXZ();
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_PlayerGoronState() {
    if (!IsSaveLoaded()) return;
    u8 modelType = TransformMasks_GetModelType();
    if (modelType == 0) return;  // only meaningful when transformed
    nlohmann::json payload;
    payload["type"] = HPN_PLAYER_GORON_STATE;
    payload["goronAction"] = TransformMasks_GetGoronAction();
    payload["rollSquash"] = TransformMasks_GetRollSquash();
    payload["rollSpikeActive"] = TransformMasks_GetRollSpikeActive();
    payload["rollChargeLevel"] = TransformMasks_GetRollChargeLevel();
    payload["quiet"] = true;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_PlayerInvincibility() {
    if (!IsSaveLoaded()) return;
    Player* player = GET_PLAYER(gPlayState);
    nlohmann::json payload;
    payload["type"] = HPN_PLAYER_INVINCIBILITY;
    payload["value"] = player->invincibilityTimer;
    payload["quiet"] = true;
    SendJsonToRemote(payload);
}

void Harpoon::SendPacket_PlayerCustomItemState() {
    // Emits ONE primitive per active custom item with its full state.
    // The new server primitive PLAYER.UPDATE_CUSTOM_ITEM_STATE is permissive
    // (extra="allow") so we can dump all the per-item fields.
    if (!IsSaveLoaded()) return;
    CustomItemState* ci = &gCustomItemState;

    auto emit = [&](const char* itemId, std::function<void(nlohmann::json&)> fill) {
        nlohmann::json payload;
        payload["type"] = HPN_PLAYER_CUSTOM_ITEM;
        payload["itemId"] = itemId;
        fill(payload);
        payload["quiet"] = true;
        SendJsonToRemote(payload);
    };

    if (ci->beetleActive) emit("beetle", [&](nlohmann::json& p) {
        p["pos"] = { ci->beetlePos.x, ci->beetlePos.y, ci->beetlePos.z };
        p["rot"] = { ci->beetleRot.x, ci->beetleRot.y, ci->beetleRot.z };
        p["wingScale"] = ci->beetleWingScale;
        p["state"] = ci->beetleState;
    });
    if (ci->gustJarMode > 0) emit("gust_jar", [&](nlohmann::json& p) {
        p["mode"] = ci->gustJarMode;
        p["element"] = ci->gustJarElement;
        p["blowActive"] = ci->gustJarBlowActive;
        p["heatTimer"] = ci->gustJarHeatTimer;
    });
    if (ci->fireRodActive) emit("fire_rod", [&](nlohmann::json& p) {
        p["active"] = ci->fireRodProjActive;
        p["count"] = ci->fireRodProjCount;
        p["rodType"] = ci->fireRodProjType;
        p["scale"] = ci->fireRodProjScale;
        p["pos1"] = { ci->fireRodProjPos.x, ci->fireRodProjPos.y, ci->fireRodProjPos.z };
        p["pos2"] = { ci->fireRodProjPos2.x, ci->fireRodProjPos2.y, ci->fireRodProjPos2.z };
        p["pos3"] = { ci->fireRodProjPos3.x, ci->fireRodProjPos3.y, ci->fireRodProjPos3.z };
    });
    if (ci->iceRodActive) emit("ice_rod", [&](nlohmann::json& p) {
        p["active"] = ci->iceRodProjActive;
        p["count"] = ci->iceRodProjCount;
        p["scale"] = ci->iceRodProjScale;
        p["pos1"] = { ci->iceRodProjPos.x, ci->iceRodProjPos.y, ci->iceRodProjPos.z };
        p["pos2"] = { ci->iceRodProjPos2.x, ci->iceRodProjPos2.y, ci->iceRodProjPos2.z };
        p["pos3"] = { ci->iceRodProjPos3.x, ci->iceRodProjPos3.y, ci->iceRodProjPos3.z };
    });
    if (ci->lightRodActive) emit("light_rod", [&](nlohmann::json& p) {
        p["active"] = ci->lightRodProjActive;
        p["count"] = ci->lightRodProjCount;
        p["pos1"] = { ci->lightRodProjPos.x, ci->lightRodProjPos.y, ci->lightRodProjPos.z };
        p["pos2"] = { ci->lightRodProjPos2.x, ci->lightRodProjPos2.y, ci->lightRodProjPos2.z };
        p["pos3"] = { ci->lightRodProjPos3.x, ci->lightRodProjPos3.y, ci->lightRodProjPos3.z };
    });
    if (ci->ballAndChainThrown) emit("ball_chain", [&](nlohmann::json& p) {
        p["thrown"] = ci->ballAndChainThrown;
        p["timer"] = ci->timer2;
        p["pos"] = { ci->sharedProjectilePos.x, ci->sharedProjectilePos.y, ci->sharedProjectilePos.z };
    });
    if (ci->whipActive) emit("whip", [&](nlohmann::json& p) {
        p["state"] = ci->whipState;
        p["tipPos"] = { ci->whipTipPos.x, ci->whipTipPos.y, ci->whipTipPos.z };
        p["attachPos"] = { ci->whipAttachPos.x, ci->whipAttachPos.y, ci->whipAttachPos.z };
        p["attachNormal"] = { ci->whipAttachNormal.x, ci->whipAttachNormal.y, ci->whipAttachNormal.z };
    });
    if (ci->dekuLeafGliding || ci->dekuLeafBlowing) emit("deku_leaf", [&](nlohmann::json& p) {
        p["gliding"] = ci->dekuLeafGliding;
        p["blowing"] = ci->dekuLeafBlowing;
        p["animTimer"] = ci->dekuLeafAnimTimer;
    });
    if (ci->shovelAnimating) emit("shovel", [&](nlohmann::json& p) {
        p["animating"] = ci->shovelAnimating;
    });
    if (ci->dominionRodActive) emit("dominion_rod", [&](nlohmann::json& p) {
        p["state"] = ci->dominionRodState;
        p["orbPos"] = { ci->dominionRodOrbPos.x, ci->dominionRodOrbPos.y, ci->dominionRodOrbPos.z };
    });
    if (ci->switchHookActive) emit("switch_hook", [&](nlohmann::json& p) {
        p["state"] = ci->switchHookState;
        p["projPos"] = { ci->switchHookProjPos.x, ci->switchHookProjPos.y, ci->switchHookProjPos.z };
    });
    if (ci->timeGateActive) emit("time_gate", [&](nlohmann::json& p) {
        p["itemVisible"] = ci->timeGateItemVisible;
        p["portalActive"] = ci->timeGatePortalActive;
        p["portalAlpha"] = ci->timeGatePortalAlpha;
        p["portalScale"] = ci->timeGatePortalScale;
    });
}

void Harpoon::SendPacket_PlayerKill() {
    nlohmann::json payload;
    payload["type"] = HPN_PLAYER_KILL;
    SendJsonToRemote(payload);
}
