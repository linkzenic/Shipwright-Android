#include "Harpoon.h"
#include "HarpoonSkinSync.h"
#include "PropHunt/PropHunt.h"
#include "TriforceThief/TriforceThief.h"
#include "soh/SohGui/SohMenu.h"
#include "soh/SohGui/MenuTypes.h"
#include <libultraship/libultraship.h>
#include <cstdlib>
#include <unordered_set>

extern "C" {
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
} // namespace SohGui

#ifdef ENABLE_REMOTE_CONTROL

// ============================================================================
// Harpoon Menu (Scooter-style layout)
// ============================================================================

static const char* OFFICIAL_HOST = "54.209.53.9";
static const int OFFICIAL_PORT = 8765; 

static const char* sGameStateNames[] = {
    "Disconnected", "Lobby", "Map Select", "Countdown", "Hiding Phase", "Playing", "Spectating", "Finished",
};

static bool sUseOfficialRemote = false;

static void HarpoonMainMenu(WidgetInfo& info) {
    auto harpoon = Harpoon::Instance;
    if (harpoon == nullptr)
        return;

    bool isConnected = harpoon->isConnected;
    bool isConnecting = harpoon->isEnabled && !isConnected;
    // Treat the session as "ready" only after HARPOON.SERVER_INFO has assigned
    // an ownClientId. Before that, the WebSocket is open but the handshake
    // hasn't been ACK'd — any ROOM.CREATE / ROOM.JOIN we send goes through
    // the pre-handshake gate and gets silently rejected.
    bool isReady = isConnected && harpoon->ownClientId != 0;
    bool inputLocked = isConnected || isConnecting;
    bool inRoom = isConnected && !harpoon->currentRoomId.empty();

    ImGui::Text("Harpoon - Multiplayer");
    ImGui::Separator();

    // ====================================================================
    // Connection Settings
    // ====================================================================

    static char hostBuf[128];
    static char nameBuf[64];
    static bool initialized = false;

    if (!initialized) {
        strncpy(hostBuf, CVarGetString(CVAR_HARPOON("Host"), "localhost"), sizeof(hostBuf) - 1);
        strncpy(nameBuf, CVarGetString(CVAR_HARPOON("Name"), "Player"), sizeof(nameBuf) - 1);
        initialized = true;
    }

    ImGui::BeginDisabled(inputLocked || sUseOfficialRemote);

    ImGui::Text("Host:");
    ImGui::SameLine();
    if (ImGui::InputText("##HarpoonHost", hostBuf, sizeof(hostBuf))) {
        CVarSetString(CVAR_HARPOON("Host"), hostBuf);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    s32 port = CVarGetInteger(CVAR_HARPOON("Port"), 8765);  // Harpoon v2 default
    ImGui::Text("Port:");
    ImGui::SameLine();
    if (ImGui::InputInt("##HarpoonPort", &port)) {
        CVarSetInteger(CVAR_HARPOON("Port"), port);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    ImGui::EndDisabled();

    // Official Remote toggle
    ImGui::BeginDisabled(inputLocked);
    if (ImGui::Button(sUseOfficialRemote ? "Custom Server" : "Official Remote")) {
        sUseOfficialRemote = !sUseOfficialRemote;
        if (sUseOfficialRemote) {
            strncpy(hostBuf, OFFICIAL_HOST, sizeof(hostBuf) - 1);
            CVarSetString(CVAR_HARPOON("Host"), OFFICIAL_HOST);
            CVarSetInteger(CVAR_HARPOON("Port"), OFFICIAL_PORT);
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
    }
    if (sUseOfficialRemote) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "Official");
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    // Player identity
    ImGui::BeginDisabled(inputLocked);

    ImGui::Text("Name:");
    ImGui::SameLine();
    if (ImGui::InputText("##HarpoonName", nameBuf, sizeof(nameBuf))) {
        CVarSetString(CVAR_HARPOON("Name"), nameBuf);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    ImGui::EndDisabled();

    Color_RGBA8 color = CVarGetColor(CVAR_HARPOON("Color.Value"), { 100, 255, 100 });
    float colorF[3] = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f };
    if (ImGui::ColorEdit3("Color", colorF)) {
        color.r = (u8)(colorF[0] * 255);
        color.g = (u8)(colorF[1] * 255);
        color.b = (u8)(colorF[2] * 255);
        CVarSetColor(CVAR_HARPOON("Color.Value"), color);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    ImGui::Separator();

    // Connect / Disconnect
    if (!isConnected && !isConnecting) {
        if (ImGui::Button("Connect")) {
            harpoon->Enable();
        }
    } else if (isConnecting) {
        ImGui::BeginDisabled(true);
        ImGui::Button("Connecting...");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Connecting...");
    } else {
        if (ImGui::Button("Disconnect")) {
            harpoon->Disable();
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Connected");
    }

    // ====================================================================
    // Room Browser (handshake ACK'd, not yet in a room)
    // ====================================================================

    if (isConnected && !isReady) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f),
                            "Waiting for handshake ACK...");
        ImGui::TextWrapped("The server hasn't issued our client id yet. "
                            "Room creation and joining will be enabled once "
                            "HARPOON.SERVER_INFO arrives.");
    }

    if (isReady && !inRoom) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "Rooms");

        static char roomIdBuf[64] = "";
        static char roomPassBuf[64] = "";
        static char roomNameBuf[64] = "";
        static char gameModeBuf[64] = "";

        // Discover installed gamemode packs (folders under harpoon/gamemodes/
        // that contain a gamemode.yaml). Re-scanned only when the user clicks
        // "Refresh", so the dropdown stays cheap to render.
        std::vector<std::string> gamemodes = HarpoonSkinSync::GetInstalledGamemodes();
        bool hasGamemodes = !gamemodes.empty();

        // First-time default: pick the first installed gamemode.
        if (gameModeBuf[0] == '\0' && hasGamemodes) {
            strncpy(gameModeBuf, gamemodes.front().c_str(), sizeof(gameModeBuf) - 1);
        }
        // If the previously-selected gamemode disappeared (folder removed),
        // reset to the first available so the dropdown isn't stuck on a stale
        // entry the user can't actually create with.
        if (hasGamemodes) {
            bool stillPresent = false;
            for (const auto& g : gamemodes) {
                if (g == gameModeBuf) { stillPresent = true; break; }
            }
            if (!stillPresent) {
                strncpy(gameModeBuf, gamemodes.front().c_str(), sizeof(gameModeBuf) - 1);
            }
        }

        ImGui::Text("Room Name:");
        ImGui::SameLine();
        if (roomNameBuf[0] == '\0') {
            snprintf(roomNameBuf, sizeof(roomNameBuf), "%s's Room", nameBuf);
        }
        ImGui::InputText("##RoomName", roomNameBuf, sizeof(roomNameBuf));

        // Gamemode dropdown — populated from harpoon/gamemodes/.
        ImGui::Text("Game Mode:");
        ImGui::SameLine();
        ImGui::BeginDisabled(!hasGamemodes);
        const char* preview = hasGamemodes ? gameModeBuf : "(none installed)";
        if (ImGui::BeginCombo("##GameMode", preview)) {
            for (const auto& gm : gamemodes) {
                bool isSelected = (gm == gameModeBuf);
                if (ImGui::Selectable(gm.c_str(), isSelected)) {
                    strncpy(gameModeBuf, gm.c_str(), sizeof(gameModeBuf) - 1);
                    gameModeBuf[sizeof(gameModeBuf) - 1] = '\0';
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh##Gamemodes")) {
            HarpoonSkinSync::GetInstalledGamemodes(/*forceRescan*/ true);
        }

        if (!hasGamemodes) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "No gamemodes installed.");
            ImGui::TextWrapped("Drop pack folders into harpoon/gamemodes/ "
                               "(each must contain gamemode.yaml), then click Refresh.");
        }

        ImGui::Text("Password:");
        ImGui::SameLine();
        ImGui::InputText("##RoomPass", roomPassBuf, sizeof(roomPassBuf));

        ImGui::BeginDisabled(!hasGamemodes);
        if (ImGui::Button("Create Room")) {
            SPDLOG_INFO("[Harpoon][Menu] Create Room clicked: name='{}' gm='{}' pass={} connected={} hasGamemodes={}",
                        roomNameBuf, gameModeBuf,
                        roomPassBuf[0] ? "(set)" : "(empty)",
                        harpoon->isConnected, hasGamemodes);
            harpoon->SendPacket_RoomCreate(roomNameBuf, gameModeBuf, roomPassBuf);
        }
        ImGui::EndDisabled();
        if (!hasGamemodes) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                "(button disabled — no gamemodes detected)");
        }

        ImGui::Separator();

        // Helper: check if a gamemode_id is in the local installed list.
        auto haveGamemode = [&gamemodes](const std::string& id) {
            for (const auto& g : gamemodes) {
                if (g == id) return true;
            }
            return false;
        };

        ImGui::Text("Room ID:");
        ImGui::SameLine();
        ImGui::InputText("##RoomId", roomIdBuf, sizeof(roomIdBuf));

        // Join-by-id can't tell us the gamemode in advance, so we let it
        // through — the server will reject (or the client will auto-leave on
        // ROOM.GAMEMODE_MANIFEST if the pack is missing).
        if (ImGui::Button("Join Room")) {
            harpoon->SendPacket_RoomJoin(roomIdBuf, roomPassBuf);
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            harpoon->SendPacket_RoomList();
        }

        // Room list — disable Join for rooms whose gamemode isn't installed
        // locally. The user gets a tooltip explaining why.
        if (!harpoon->roomList.empty()) {
            ImGui::Separator();
            ImGui::Text("Available Rooms:");
            for (auto& room : harpoon->roomList) {
                ImGui::PushID(room.roomId.c_str());
                bool gmInstalled = haveGamemode(room.gameMode);
                if (!gmInstalled) {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                                       "  %s (%s) [%d/%d] %s%s",
                                       room.name.c_str(), room.gameMode.c_str(),
                                       room.playerCount, room.maxPlayers,
                                       room.state.c_str(),
                                       room.hasPassword ? " [PASS]" : "");
                } else {
                    ImGui::Text("  %s (%s) [%d/%d] %s%s",
                                room.name.c_str(), room.gameMode.c_str(),
                                room.playerCount, room.maxPlayers,
                                room.state.c_str(),
                                room.hasPassword ? " [PASS]" : "");
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!gmInstalled);
                if (ImGui::SmallButton("Join")) {
                    strncpy(roomIdBuf, room.roomId.c_str(), sizeof(roomIdBuf) - 1);
                    harpoon->SendPacket_RoomJoin(room.roomId.c_str(), roomPassBuf);
                }
                ImGui::EndDisabled();
                if (!gmInstalled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Gamemode '%s' is not installed.\n"
                                "Drop the pack folder into harpoon/gamemodes/\n"
                                "and click Refresh.",
                                room.gameMode.c_str());
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
        }
    }

    // ====================================================================
    // In-Room View
    // ====================================================================

    if (inRoom) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "Room: %s", harpoon->currentRoomName.c_str());
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "ID: %s | Mode: %s", harpoon->currentRoomId.c_str(),
                           harpoon->currentRoomGameMode.c_str());
        ImGui::Text("State: %s", sGameStateNames[harpoon->gameState]);

        if (harpoon->gameState == HARPOON_STATE_COUNTDOWN) {
            ImGui::Text("Starting in: %d", harpoon->countdownTimer);
        }

        if (harpoon->gameState == HARPOON_STATE_PLAYING) {
            ImGui::Text("Alive: %d", harpoon->aliveCount);
            if (harpoon->isEliminated) {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "ELIMINATED - Spectating");
            }
        }

        // Player list
        ImGui::Separator();
        ImGui::Text("Players:");
        for (auto& [clientId, client] : harpoon->clients) {
            ImVec4 nameColor = ImVec4(client.color.r / 255.0f, client.color.g / 255.0f, client.color.b / 255.0f, 1.0f);
            ImGui::TextColored(nameColor, "  %s%s", client.name.c_str(), client.self ? " (You)" : "");

            if (client.isSaveLoaded) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[Scene %d]", client.sceneNum);
            }
        }

        ImGui::Spacing();

        // ================================================================
        // Per-gamemode host / round controls.
        //
        // The server is gamemode-agnostic: these buttons just send
        // ROOM.BROADCAST_EVENT with the appropriate event_name and the
        // matching client-side state machine (HarpoonPropHunt / HarpoonTriforceThief)
        // picks them up via Harpoon::HandlePacket_RoomEvent.
        // ================================================================

        // The initial host is implicitly an admin. Anyone the host has
        // promoted via PROP_HUNT.ADMIN_PROMOTE also counts. Use this for
        // any "manage gameplay" buttons (Start Game, set role, etc.).
        bool isHost = (harpoon->ownClientId != 0 &&
                       harpoon->ownClientId == harpoon->hostClientId);
        auto myIt = harpoon->clients.find(harpoon->ownClientId);
        bool myAdminFlag = (myIt != harpoon->clients.end() && myIt->second.isAdmin);
        bool isAdmin = isHost || myAdminFlag;
        (void)isAdmin;  // referenced by the per-gamemode sections below

        if (harpoon->currentRoomGameMode == "prop_hunt") {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Prop Hunt");

            auto& settings = HarpoonPropHunt::Host::GetSettings();

            // --- Host settings (admin only) ---------------------------------
            if (isAdmin) {
                ImGui::Text("Seekers:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120);
                ImGui::SliderInt("##SeekerCount", &settings.seekerCount, 1, 3);

                ImGui::Text("Hide phase:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120);
                ImGui::SliderInt("##HideSec", &settings.hideSeconds, 5, 180, "%d s");

                const char* mapModeNames[] = { "Host chooses", "Everyone votes", "Random" };
                ImGui::Text("Map selection:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(180);
                // Bind to settings.mapSelectMode AND mirror to the room-wide
                // harpoon->mapSelectMode so L+R+Z (which reads harpoon's
                // global) and Open Map Select always agree with the visible
                // dropdown selection. Two-way sync: pick up server changes
                // via harpoon->mapSelectMode → settings, and push local
                // changes back via settings → harpoon->mapSelectMode.
                if (settings.mapSelectMode != (int)harpoon->mapSelectMode) {
                    settings.mapSelectMode = (int)harpoon->mapSelectMode;
                }
                if (ImGui::Combo("##MapMode", &settings.mapSelectMode,
                                  mapModeNames, IM_ARRAYSIZE(mapModeNames))) {
                    harpoon->mapSelectMode = (HarpoonMapSelectMode)settings.mapSelectMode;
                }

                if (settings.mapSelectMode == 0) {
                    const char* mapNames[] = {
                        "Kakariko Village", "Death Mountain", "Clock Town",
                        "Gerudo Fortress",  "Forest Temple",  "Zora's River",
                        "Dodongo's Cavern", "Ganon's Castle", "Kokiri Forest",
                    };
                    ImGui::Text("Map:");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(180);
                    ImGui::Combo("##HostMap", &settings.selectedMap,
                                  mapNames, IM_ARRAYSIZE(mapNames));
                }

                ImGui::Spacing();
            }

            // --- Start Game button ------------------------------------------
            ImGui::BeginDisabled(!isAdmin);
            if (ImGui::Button("Start Game")) {
                // Collect candidate client ids (everyone in the room).
                std::vector<u32> candidates;
                for (auto& [cid, c] : harpoon->clients) {
                    if (c.online) candidates.push_back(cid);
                }
                if (candidates.empty()) candidates.push_back(harpoon->ownClientId);

                // Pick seekers with priority queue (no repeats until everyone has been seeker).
                auto seekers = HarpoonPropHunt::Host::PickNextSeekers(candidates, settings.seekerCount);
                std::unordered_set<u32> seekerSet(seekers.begin(), seekers.end());

                // Resolve our own role.
                HarpoonPropHunt::Role myRole =
                    seekerSet.count(harpoon->ownClientId) > 0
                        ? HarpoonPropHunt::Role::Seeker
                        : HarpoonPropHunt::Role::Hider;

                // 1. Big save-init transition (sets Hyrule Field child Link + role kit).
                HarpoonPropHunt::BigStartGameAs(myRole);

                // 2. Broadcast role assignment to every peer (server excludes sender).
                //    Each peer applies the role addressed to its own cid via the
                //    PROP_HUNT.ROLE_ASSIGN handler (which now triggers BigStartGameAs
                //    too — see HandleRoleAssign).
                for (auto& [cid, c] : harpoon->clients) {
                    if (cid == harpoon->ownClientId) continue;
                    HarpoonPropHunt::Role r = seekerSet.count(cid) > 0
                        ? HarpoonPropHunt::Role::Seeker
                        : HarpoonPropHunt::Role::Hider;
                    harpoon->SendJsonToRemote(
                        HarpoonPropHunt::BuildRoleAssignPayload(cid, r));
                }

                // 3. Schedule the hide phase to start once everyone has loaded.
                harpoon->SendJsonToRemote(
                    HarpoonPropHunt::BuildHidePhaseBeginPayload(settings.hideSeconds * 20));
                auto& ls = HarpoonPropHunt::GetLocalState();
                ls.inHidePhase = true;
                ls.hidePhaseFramesRemaining = settings.hideSeconds * 20;
            }
            ImGui::SameLine();
            if (ImGui::Button("End Hide Phase")) {
                HarpoonPropHunt::GetLocalState().inHidePhase = false;
                HarpoonPropHunt::GetLocalState().hidePhaseFramesRemaining = 0;
                harpoon->SendJsonToRemote(HarpoonPropHunt::BuildHidePhaseEndPayload());
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Seeker History")) {
                HarpoonPropHunt::Host::ResetSeekerHistory();
            }
            if (ImGui::Button("Open Map Select Screen")) {
                // Build the broadcast envelope and locally dispatch it
                // through HandleEvent — server-relay excludes the sender,
                // so without the local apply the host's tally state can
                // diverge from peers (e.g. stale hasVoted from prior round).
                // The PROP_HUNT.OPEN_MAP_SELECT handler sets gameState +
                // mapSelectMode for both host and peers consistently.
                nlohmann::json env;
                env["type"]       = "ROOM.BROADCAST_EVENT";
                env["event_name"] = "PROP_HUNT.OPEN_MAP_SELECT";
                env["data"]       = nlohmann::json::object();
                env["data"]["mapSelectMode"] = settings.mapSelectMode;
                harpoon->SendJsonToRemote(env);
                HarpoonPropHunt::HandleEvent(env);
            }
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                "Hider controls in-game: D-Left = next category, "
                                "D-Down = next prop, D-Right = next state.");

            // --- Player list with role badges + admin actions ----------------
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Players:");
            for (auto& [cid, c] : harpoon->clients) {
                ImGui::PushID((int)cid);
                ImVec4 rolColor(0.7f, 0.7f, 0.7f, 1.0f);
                const char* rolTag = "(lobby)";
                if (c.role == "hider")        { rolColor = ImVec4(0.5f, 1.0f, 0.5f, 1.0f); rolTag = "HIDER"; }
                else if (c.role == "seeker")  { rolColor = ImVec4(1.0f, 0.5f, 0.5f, 1.0f); rolTag = "SEEKER"; }

                ImGui::TextColored(rolColor, "  [%s]", rolTag);
                ImGui::SameLine();
                ImGui::Text("%s%s%s",
                            c.name.c_str(),
                            c.self ? " (you)" : "",
                            c.isAdmin ? " [admin]" : "");
                bool isSeekerCandidate = HarpoonPropHunt::Host::HasBeenSeeker(cid);
                if (isSeekerCandidate) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(was seeker)");
                }

                // Admin actions — visible to admins for every player including
                // self. Self-assign applies the role locally (the server-side
                // relay excludes the sender from receiving its own broadcast).
                if (isAdmin) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Hider")) {
                        c.role = "hider";
                        if (c.self) {
                            HarpoonPropHunt::ChangeRoleAndReload(HarpoonPropHunt::Role::Hider);
                        }
                        harpoon->SendJsonToRemote(
                            HarpoonPropHunt::BuildRoleAssignPayload(cid, HarpoonPropHunt::Role::Hider));
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Seeker")) {
                        c.role = "seeker";
                        if (c.self) {
                            HarpoonPropHunt::ChangeRoleAndReload(HarpoonPropHunt::Role::Seeker);
                        }
                        harpoon->SendJsonToRemote(
                            HarpoonPropHunt::BuildRoleAssignPayload(cid, HarpoonPropHunt::Role::Seeker));
                    }
                    if (!c.self) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton(c.isAdmin ? "-Admin" : "+Admin")) {
                            nlohmann::json env;
                            env["type"]       = "ROOM.BROADCAST_EVENT";
                            env["event_name"] = "PROP_HUNT.ADMIN_PROMOTE";
                            env["data"]       = { {"targetClientId", cid}, {"isAdmin", !c.isAdmin} };
                            harpoon->SendJsonToRemote(env);
                            c.isAdmin = !c.isAdmin;
                        }
                    }
                }
                ImGui::PopID();
            }
        }

        if (harpoon->currentRoomGameMode == "triforce_thief") {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Triforce Thief");

            const auto& maps = HarpoonTriforceThief::GetMaps();
            static int selectedMap = 0;
            if (selectedMap < 0) selectedMap = 0;
            if (selectedMap >= (int)maps.size()) selectedMap = 0;

            if (maps.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                    "Triforce Thief pack not loaded — check harpoon/gamemodes/triforce_thief/");
            } else {
                ImGui::Text("Map:");
                ImGui::SameLine();
                const char* preview = maps[selectedMap].name.c_str();
                ImGui::SetNextItemWidth(220);
                if (ImGui::BeginCombo("##TTMapPick", preview)) {
                    for (int i = 0; i < (int)maps.size(); i++) {
                        bool isSelected = (i == selectedMap);
                        if (ImGui::Selectable(maps[i].name.c_str(), isSelected)) {
                            selectedMap = i;
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                    "  %s", maps[selectedMap].description.c_str());

                // --- Host settings (admin only) -----------------------------
                if (isAdmin) {
                    ImGui::Text("Round-win seconds:");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(140);
                    ImGui::SliderInt("##TTwinSec",
                                      &HarpoonTriforceThief::GetLocalState().roundWinSeconds,
                                      15, 300, "%d s");

                    // Map-select mode picker — binds DIRECTLY to the
                    // room-wide mapSelectMode so L+R+Z and "Open Map Select"
                    // always use the latest dropdown value (instead of a
                    // stale local static that only synced on button-click).
                    int ttMapSelectMode = (int)harpoon->mapSelectMode;
                    const char* mapModeNames[] = { "Host chooses", "Everyone votes", "Random" };
                    ImGui::Text("Map selection:");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(180);
                    if (ImGui::Combo("##TTMapMode", &ttMapSelectMode,
                                      mapModeNames, IM_ARRAYSIZE(mapModeNames))) {
                        harpoon->mapSelectMode = (HarpoonMapSelectMode)ttMapSelectMode;
                    }

                    ImGui::Spacing();

                    ImGui::BeginDisabled(!isAdmin);
                    if (ImGui::Button("Open Map Select")) {
                        // Build the broadcast envelope and locally dispatch
                        // it through HandleEvent — server-relay excludes the
                        // sender, so without the local apply the host's
                        // mapVoteDeadline stays at 0, the TickFrame timeout
                        // fires on frame 1, and the overlay flashes itself
                        // closed. HandleMapSelectBegin also sets the
                        // room-wide gameState + mapSelectMode + per-client
                        // hasVoted resets in one place.
                        nlohmann::json env;
                        env["type"]       = "ROOM.BROADCAST_EVENT";
                        env["event_name"] = "TRIFORCE_THIEF.MAP_SELECT_BEGIN";
                        env["data"]       = nlohmann::json::object();
                        env["data"]["mapSelectMode"] = ttMapSelectMode;
                        env["data"]["windowSeconds"] = 15;
                        harpoon->SendJsonToRemote(env);
                        HarpoonTriforceThief::HandleEvent(env);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Confirm Map (start round)")) {
                        // Shared host-confirm path — applies locally and
                        // broadcasts MAP_CONFIRMED + ROUND_CONFIG + spawn.
                        HarpoonTriforceThief::HostConfirmMap(selectedMap);
                    }
                    ImGui::EndDisabled();
                }
                ImGui::BeginDisabled(!isAdmin);
                if (ImGui::Button("Respawn Triforce (random point)")) {
                    const auto* m = HarpoonTriforceThief::GetMap(
                        HarpoonTriforceThief::GetLocalState().confirmedMap);
                    if (m != nullptr && !m->spawnPoints.empty()) {
                        s32 idx = (s32)(rand() % m->spawnPoints.size());
                        const auto& sp = m->spawnPoints[idx];
                        auto& s = HarpoonTriforceThief::GetLocalState();
                        s.carrierClientId = 0;
                        s.currentSpawn = idx;
                        s.triforceX = sp.x;
                        s.triforceY = sp.y;
                        s.triforceZ = sp.z;
                        harpoon->SendJsonToRemote(
                            HarpoonTriforceThief::BuildTriforceSpawnPayload(
                                s.confirmedMap, idx, sp.x, sp.y, sp.z));
                    }
                }
                if (ImGui::Button("Declare Local Winner")) {
                    harpoon->SendJsonToRemote(
                        HarpoonTriforceThief::BuildRoundResultPayload(
                            harpoon->ownClientId,
                            HarpoonTriforceThief::GetLocalState().roundIndex));
                }
                ImGui::EndDisabled();
            }
        }

        if (ImGui::Button("Leave Room")) {
            harpoon->SendPacket_RoomLeave();
        }

        // Kill feed
        if (!harpoon->killFeed.empty()) {
            ImGui::Separator();
            ImGui::Text("Kill Feed:");
            for (auto& msg : harpoon->killFeed) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "  %s", msg.c_str());
            }
        }
    }
}

// ============================================================================
// Registration (auto-registers via static init)
// ============================================================================

void RegisterHarpoonMenu() {
    SohGui::mSohMenu->AddSidebarEntry("Network", "Harpoon", 1);
    WidgetPath path = { "Network", "Harpoon", SECTION_COLUMN_1 };
    SohGui::mSohMenu->AddWidget(path, "HarpoonMainMenu", WIDGET_CUSTOM)
        .CustomFunction(HarpoonMainMenu)
        .HideInSearch(true);
}

static RegisterMenuInitFunc harpoonMenuInitFunc(RegisterHarpoonMenu);
#endif
