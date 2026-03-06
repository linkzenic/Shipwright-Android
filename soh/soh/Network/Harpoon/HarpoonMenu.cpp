#include "Harpoon.h"
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
static const int OFFICIAL_PORT = 43384;

static const char* sGameStateNames[] = {
    "Disconnected",
    "Lobby",
    "Map Select",
    "Countdown",
    "Hiding Phase",
    "Playing",
    "Spectating",
    "Finished",
};

static bool sUseOfficialRemote = false;

static void HarpoonMainMenu(WidgetInfo& info) {
    auto harpoon = Harpoon::Instance;
    if (harpoon == nullptr) return;

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

    s32 port = CVarGetInteger(CVAR_HARPOON("Port"), 43384);
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
    // Settings (Sync & PVP)
    // ====================================================================

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "Settings");

    bool syncItems = harpoon->syncItems;
    if (ImGui::Checkbox("Sync Items & Flags##Harpoon", &syncItems)) {
        harpoon->syncItems = syncItems;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Sync items, flags, checks, entrances, and dungeon keys\n"
                     "between all connected players (for Randomizer).");
        ImGui::EndTooltip();
    }

    bool pvpEnabled = harpoon->pvpEnabled;
    if (ImGui::Checkbox("PVP##Harpoon", &pvpEnabled)) {
        harpoon->pvpEnabled = pvpEnabled;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("When enabled, players can damage and knockback each other.\n"
                     "When disabled, status effects (fire, ice, electric) are still\n"
                     "received but damage and knockback are ignored.");
        ImGui::EndTooltip();
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

        ImGui::Text("Room Name:");
        ImGui::SameLine();
        if (roomNameBuf[0] == '\0') {
            snprintf(roomNameBuf, sizeof(roomNameBuf), "%s's Room", nameBuf);
        }
        ImGui::InputText("##RoomName", roomNameBuf, sizeof(roomNameBuf));

        ImGui::Text("Password:");
        ImGui::SameLine();
        ImGui::InputText("##RoomPass", roomPassBuf, sizeof(roomPassBuf));

        if (ImGui::Button("Create Room")) {
            harpoon->SendPacket_RoomCreate(roomNameBuf, "randomizer", roomPassBuf);
        }

        ImGui::Separator();

        ImGui::Text("Room ID:");
        ImGui::SameLine();
        ImGui::InputText("##RoomId", roomIdBuf, sizeof(roomIdBuf));

        if (ImGui::Button("Join Room")) {
            harpoon->SendPacket_RoomJoin(roomIdBuf, roomPassBuf);
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            harpoon->SendPacket_RoomList();
        }

        // Room list
        if (!harpoon->roomList.empty()) {
            ImGui::Separator();
            ImGui::Text("Available Rooms:");
            for (auto& room : harpoon->roomList) {
                ImGui::PushID(room.roomId.c_str());
                ImGui::Text("  %s (%s) [%d/%d] %s%s",
                    room.name.c_str(),
                    room.gameMode.c_str(),
                    room.playerCount,
                    room.maxPlayers,
                    room.state.c_str(),
                    room.hasPassword ? " [PASS]" : "");
                ImGui::SameLine();
                if (ImGui::SmallButton("Join")) {
                    strncpy(roomIdBuf, room.roomId.c_str(), sizeof(roomIdBuf) - 1);
                    harpoon->SendPacket_RoomJoin(room.roomId.c_str(), roomPassBuf);
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
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "ID: %s | Mode: %s",
            harpoon->currentRoomId.c_str(), harpoon->currentRoomGameMode.c_str());
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
