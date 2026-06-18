// NEI: upstream #6732 removed the ENABLE_REMOTE_CONTROL build flag (networking is now always
// available). NEI's Harpoon menu (this whole file, gated at the #ifdef below) and its SDL_net
// transport were still behind that flag, so the Harpoon tab vanished after the merge. Re-define
// it locally to keep the menu compiled in. SDL_net is now linked unconditionally.
#ifndef ENABLE_REMOTE_CONTROL
#define ENABLE_REMOTE_CONTROL
#endif

#include "Harpoon.h"
#include "HarpoonSkinSync.h"
#include "PropHunt/PropHunt.h"
#include "TriforceThief/TriforceThief.h"
#include "Templates.h"
#include "RemoteSaveEditor.h"
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
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    s32 port = CVarGetInteger(CVAR_HARPOON("Port"), 8765);  // Harpoon v2 default
    ImGui::Text("Port:");
    ImGui::SameLine();
    if (ImGui::InputInt("##HarpoonPort", &port)) {
        CVarSetInteger(CVAR_HARPOON("Port"), port);
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
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
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
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
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    ImGui::EndDisabled();

    Color_RGBA8 color = CVarGetColor(CVAR_HARPOON("Color.Value"), { 100, 255, 100 });
    float colorF[3] = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f };
    if (ImGui::ColorEdit3("Color", colorF)) {
        color.r = (u8)(colorF[0] * 255);
        color.g = (u8)(colorF[1] * 255);
        color.b = (u8)(colorF[2] * 255);
        CVarSetColor(CVAR_HARPOON("Color.Value"), color);
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
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

        // Host is the sole authority. Admin role removed — only the host
        // can manage gameplay (Start Game, set role, finish round, etc.).
        bool isHost = (harpoon->ownClientId != 0 &&
                       harpoon->ownClientId == harpoon->hostClientId);
        bool isAdmin = isHost;
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
            // Host-only Finish Round button. Forces the current round to end
            // (broadcasts ROUND_RESULT; HandleRoundResult teleports everyone
            // back to the lobby silently). Disabled outside an active round.
            bool roundInFlightPH =
                (harpoon->gameState == HARPOON_STATE_HIDING_PHASE ||
                 harpoon->gameState == HARPOON_STATE_PLAYING);
            ImGui::BeginDisabled(!roundInFlightPH);
            if (ImGui::Button("Finish Round")) {
                nlohmann::json env;
                env["type"]       = "ROOM.BROADCAST_EVENT";
                env["event_name"] = "PROP_HUNT.ROUND_RESULT";
                env["data"]       = nlohmann::json::object();
                env["data"]["winnerSide"] = "host_forced";
                harpoon->SendJsonToRemote(env);
                HarpoonPropHunt::HandleEvent(env);
            }
            ImGui::EndDisabled();
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

            // --- Player list with role badges + host-only role buttons -------
            // Hider/Seeker buttons act in two modes:
            //   - LOBBY / FINISHED → write c.pendingRole (next-round override),
            //     no broadcast. Consumed by HostStartRound.
            //   - HIDING_PHASE / PLAYING → broadcast ROLE_ASSIGN immediately
            //     (mid-round override, existing behaviour).
            bool roundInFlight =
                (harpoon->gameState == HARPOON_STATE_HIDING_PHASE ||
                 harpoon->gameState == HARPOON_STATE_PLAYING);

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
                ImGui::Text("%s%s", c.name.c_str(), c.self ? " (you)" : "");
                if (!c.pendingRole.empty()) {
                    ImGui::SameLine();
                    ImVec4 pendCol = (c.pendingRole == "seeker")
                        ? ImVec4(1.0f, 0.6f, 0.6f, 1.0f)
                        : ImVec4(0.6f, 1.0f, 0.6f, 1.0f);
                    ImGui::TextColored(pendCol, "→ pending: %s",
                                       c.pendingRole == "seeker" ? "SEEKER" : "HIDER");
                }
                bool isSeekerCandidate = HarpoonPropHunt::Host::HasBeenSeeker(cid);
                if (isSeekerCandidate) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(was seeker)");
                }

                // Host-only role buttons. Single source of authority.
                if (isHost) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Hider")) {
                        if (roundInFlight) {
                            c.role = "hider";
                            c.pendingRole.clear();
                            if (c.self) {
                                HarpoonPropHunt::ChangeRoleAndReload(HarpoonPropHunt::Role::Hider);
                            }
                            harpoon->SendJsonToRemote(
                                HarpoonPropHunt::BuildRoleAssignPayload(cid, HarpoonPropHunt::Role::Hider));
                        } else {
                            c.pendingRole = "hider";  // staged for next round
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Seeker")) {
                        if (roundInFlight) {
                            c.role = "seeker";
                            c.pendingRole.clear();
                            if (c.self) {
                                HarpoonPropHunt::ChangeRoleAndReload(HarpoonPropHunt::Role::Seeker);
                            }
                            harpoon->SendJsonToRemote(
                                HarpoonPropHunt::BuildRoleAssignPayload(cid, HarpoonPropHunt::Role::Seeker));
                        } else {
                            c.pendingRole = "seeker";
                        }
                    }
                    if (!c.pendingRole.empty()) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Clear")) {
                            c.pendingRole.clear();
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
                    // Team-mode cap per user spec: max 99 sec.
                    ImGui::SliderInt("##TTwinSec",
                                      &HarpoonTriforceThief::GetLocalState().roundWinSeconds,
                                      15, 99, "%d s");
                    if (HarpoonTriforceThief::GetLocalState().roundWinSeconds > 99) {
                        HarpoonTriforceThief::GetLocalState().roundWinSeconds = 99;
                    }
                    if (HarpoonTriforceThief::GetLocalState().roundWinSeconds < 5) {
                        HarpoonTriforceThief::GetLocalState().roundWinSeconds = 5;
                    }

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
                    // Disable Confirm-Map if any online client is teamless.
                    // The host gets a visible hint listing the offenders.
                    std::string teamlessHint;
                    bool anyTeamless = false;
                    for (auto& [cid, c] : harpoon->clients) {
                        if (!c.online) continue;
                        if (c.team.empty()) {
                            anyTeamless = true;
                            if (!teamlessHint.empty()) teamlessHint += ", ";
                            teamlessHint += c.name.empty()
                                              ? ("cid" + std::to_string(cid))
                                              : c.name;
                        }
                    }
                    ImGui::BeginDisabled(anyTeamless);
                    if (ImGui::Button("Confirm Map (start round)")) {
                        // Shared host-confirm path — applies locally and
                        // broadcasts MAP_CONFIRMED + ROUND_CONFIG + spawn.
                        HarpoonTriforceThief::HostConfirmMap(selectedMap);
                    }
                    ImGui::EndDisabled();
                    if (anyTeamless) {
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                            "  No team picked yet: %s", teamlessHint.c_str());
                    }
                    ImGui::EndDisabled();
                }
                // Host-only Finish Round button — forces the current TT
                // round to end (broadcasts ROUND_RESULT; HandleRoundResult
                // teleports everyone back to the lobby silently). Disabled
                // when no round is in flight.
                bool roundInFlightTT =
                    HarpoonTriforceThief::GetLocalState().inRound &&
                    !HarpoonTriforceThief::GetLocalState().roundEnded;
                ImGui::BeginDisabled(!isHost || !roundInFlightTT);
                if (ImGui::Button("Finish Round")) {
                    auto payload = HarpoonTriforceThief::BuildRoundResultPayload(
                        harpoon->ownClientId,
                        HarpoonTriforceThief::GetLocalState().roundIndex);
                    harpoon->SendJsonToRemote(payload);
                    HarpoonTriforceThief::HandleEvent(payload);
                }
                ImGui::EndDisabled();

                // --- Team picker (lobby only, per-user spec) --------------
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f),
                                    "Teams (lobby only):");
                const bool inLobby =
                    (harpoon->gameState == HARPOON_STATE_LOBBY);
                ImGui::BeginDisabled(!inLobby);
                if (ImGui::Button("Join Red")) {
                    HarpoonTriforceThief::SetLocalTeam("red");
                }
                ImGui::SameLine();
                if (ImGui::Button("Join Blue")) {
                    HarpoonTriforceThief::SetLocalTeam("blue");
                }
                ImGui::EndDisabled();
                if (!inLobby) {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                        "  Team switching disabled mid-round.");
                }

                // Per-client team badge list (mirrors PropHunt role list).
                for (auto& [cid, c] : harpoon->clients) {
                    ImGui::PushID((int)cid + 0x7700);
                    ImVec4 col(0.7f, 0.7f, 0.7f, 1.0f);
                    const char* tag = "(no team)";
                    if (c.team == "red") {
                        col = ImVec4(0.92f, 0.30f, 0.30f, 1.0f);
                        tag = "RED";
                    } else if (c.team == "blue") {
                        col = ImVec4(0.30f, 0.55f, 0.92f, 1.0f);
                        tag = "BLUE";
                    }
                    ImGui::TextColored(col, "  [%s]", tag);
                    ImGui::SameLine();
                    ImGui::TextColored(col, "%s%s",
                                       c.name.c_str(), c.self ? " (you)" : "");
                    ImGui::PopID();
                }
            }
        }

        // --------------------------------------------------------------
        // GM panel — host-only RP controls (templates, flag overrides,
        // peer teleport, host transfer). Only renders in RPG mode rooms
        // so PH / TT / randomizer hosts don't see RP-specific controls.
        // --------------------------------------------------------------
        if (isHost && harpoon->currentRoomGameMode == "rpg") {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 1.0f, 1.0f), "GM Controls");

            if (ImGui::CollapsingHeader("Templates")) {
                static char sNewTplName[64] = "";
                ImGui::SetNextItemWidth(180);
                ImGui::InputText("##tplname", sNewTplName, IM_ARRAYSIZE(sNewTplName));
                ImGui::SameLine();
                if (ImGui::Button("Snapshot current state") && sNewTplName[0] != '\0') {
                    HarpoonTemplates::SnapshotLocal(sNewTplName);
                }
                ImGui::Spacing();
                const auto& tpls = HarpoonTemplates::All();
                if (tpls.empty()) {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                        "  (no templates saved yet)");
                }
                for (const auto& tpl : tpls) {
                    ImGui::PushID(tpl.name.c_str());
                    ImGui::Text("  %s", tpl.name.c_str());
                    ImGui::SameLine();
                    if (ImGui::Button("Apply ▸ Me")) {
                        HarpoonTemplates::ApplyToLocal(tpl.name);
                    }
                    for (auto& [cid, c] : harpoon->clients) {
                        if (cid == harpoon->ownClientId || !c.online) continue;
                        ImGui::SameLine();
                        std::string lbl = "▸ " + c.name;
                        if (ImGui::SmallButton(lbl.c_str())) {
                            HarpoonTemplates::ApplyToPeer(cid, tpl.name);
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Delete")) {
                        HarpoonTemplates::Delete(tpl.name);
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
            }

            // Player-flags section removed — templates now control
            // can_climb / can_grab / can_crawl / can_talk per-peer via
            // the restrictNo* fields baked into each Template. To
            // restrict a peer, snapshot a template with the desired
            // restrict flags and apply it to them.

            // Remote Save Editor — open a save-editor-style window that
            // operates on a CACHED snapshot of a peer's gSaveContext.
            // The peer answers a peek request with their current save
            // state; the GM edits the cached Template; Apply uses the
            // existing TEMPLATE_APPLY broadcast to push the edited
            // state back to the peer. Host-only (the peer side also
            // gates this — see HandlePeekRequest).
            if (ImGui::CollapsingHeader("Remote Save Editor")) {
                ImGui::TextWrapped(
                    "Edit any peer's save state. Pick a player below to "
                    "open the editor window with their current snapshot.");
                ImGui::Spacing();
                bool anyPeer = false;
                for (auto& [cid, c] : harpoon->clients) {
                    if (cid == harpoon->ownClientId || !c.online) continue;
                    anyPeer = true;
                    ImGui::PushID((int)cid + 11000);
                    std::string label = c.name.empty()
                                          ? ("cid" + std::to_string(cid))
                                          : c.name;
                    ImGui::Text("  %s", label.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Edit save…")) {
                        HarpoonRemoteSaveEditor::OpenForPeer(cid);
                    }
                    ImGui::PopID();
                }
                if (!anyPeer) {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                        "  (no peers connected)");
                }
            }

            if (ImGui::CollapsingHeader("Teleport")) {
                for (auto& [cid, c] : harpoon->clients) {
                    if (cid == harpoon->ownClientId || !c.online) continue;
                    ImGui::PushID((int)cid);
                    ImGui::Text("  %s", c.name.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("To me") && gPlayState != nullptr) {
                        Player* lp = GET_PLAYER(gPlayState);
                        if (lp != nullptr) {
                            nlohmann::json env;
                            env["type"]       = "ROOM.BROADCAST_EVENT";
                            env["event_name"] = "HARPOON.PEER_TELEPORT";
                            nlohmann::json d;
                            d["targetClientId"] = cid;
                            d["entranceIndex"]  = (int)gSaveContext.entranceIndex;
                            d["x"] = lp->actor.world.pos.x;
                            d["y"] = lp->actor.world.pos.y;
                            d["z"] = lp->actor.world.pos.z;
                            d["toHostPos"] = true;
                            env["data"] = d;
                            harpoon->SendJsonToRemote(env);
                        }
                    }
                    ImGui::SameLine();
                    static int sSendEntr = 0x0CD;  // Hyrule Field default
                    ImGui::SetNextItemWidth(80);
                    ImGui::InputInt("##entr", &sSendEntr, 0, 0);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Send to scene")) {
                        nlohmann::json env;
                        env["type"]       = "ROOM.BROADCAST_EVENT";
                        env["event_name"] = "HARPOON.PEER_TELEPORT";
                        nlohmann::json d;
                        d["targetClientId"] = cid;
                        d["entranceIndex"]  = sSendEntr;
                        d["toHostPos"]      = false;
                        env["data"] = d;
                        harpoon->SendJsonToRemote(env);
                    }
                    ImGui::PopID();
                }
            }

            if (ImGui::CollapsingHeader("Host transfer")) {
                for (auto& [cid, c] : harpoon->clients) {
                    if (cid == harpoon->ownClientId || !c.online) continue;
                    ImGui::PushID((int)cid);
                    ImGui::Text("  %s", c.name.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Make host")) {
                        nlohmann::json env;
                        env["type"]       = "ROOM.BROADCAST_EVENT";
                        env["event_name"] = "HARPOON.HOST_TRANSFER";
                        nlohmann::json d;
                        d["newHostClientId"] = cid;
                        env["data"] = d;
                        harpoon->SendJsonToRemote(env);
                        // Apply locally too (relay excludes sender).
                        harpoon->hostClientId = cid;
                    }
                    ImGui::PopID();
                }
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
