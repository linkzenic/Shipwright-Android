#include "Harpoon.h"
#include "HarpoonSkinSync.h"
#include "soh/SohGui/SohMenu.h"
#include "soh/SohGui/MenuTypes.h"
#include <libultraship/libultraship.h>

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
    // Room Browser (connected but not in a room)
    // ====================================================================

    if (isConnected && !inRoom) {
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
            harpoon->SendPacket_RoomCreate(roomNameBuf, gameModeBuf, roomPassBuf);
        }
        ImGui::EndDisabled();

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
