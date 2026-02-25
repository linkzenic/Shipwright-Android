#include "PvPAnchor.h"
#include "soh/SohGui/SohMenu.h"
#include "soh/SohGui/MenuTypes.h"
#include <libultraship/libultraship.h>

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
} // namespace SohGui

#ifdef ENABLE_REMOTE_CONTROL

static const char* sHGStateNames[] = {
    "Disconnected",
    "Lobby",
    "Countdown",
    "Playing",
    "Spectating",
    "Finished",
};

static void PvPAnchorMainMenu(WidgetInfo& info) {
    auto pvp = PvPAnchor::Instance;
    if (pvp == nullptr) return;

    bool isConnected = pvp->isConnected;

    ImGui::BeginDisabled(isConnected);

    static char hostBuf[128];
    static char nameBuf[64];
    static bool initialized = false;

    if (!initialized) {
        strncpy(hostBuf, CVarGetString(CVAR_PVP_ANCHOR("Host"), "localhost"), sizeof(hostBuf) - 1);
        strncpy(nameBuf, CVarGetString(CVAR_PVP_ANCHOR("Name"), "Player"), sizeof(nameBuf) - 1);
        initialized = true;
    }

    ImGui::Text("PvP Arena - Hunger Games");
    ImGui::Separator();

    ImGui::Text("Host:");
    ImGui::SameLine();
    if (ImGui::InputText("##PvPHost", hostBuf, sizeof(hostBuf))) {
        CVarSetString(CVAR_PVP_ANCHOR("Host"), hostBuf);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    s32 port = CVarGetInteger(CVAR_PVP_ANCHOR("Port"), 43384);
    ImGui::Text("Port:");
    ImGui::SameLine();
    if (ImGui::InputInt("##PvPPort", &port)) {
        CVarSetInteger(CVAR_PVP_ANCHOR("Port"), port);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    ImGui::Text("Name:");
    ImGui::SameLine();
    if (ImGui::InputText("##PvPName", nameBuf, sizeof(nameBuf))) {
        CVarSetString(CVAR_PVP_ANCHOR("Name"), nameBuf);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    Color_RGBA8 color = CVarGetColor(CVAR_PVP_ANCHOR("Color.Value"), { 100, 255, 100 });
    float colorF[3] = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f };
    if (ImGui::ColorEdit3("Color", colorF)) {
        color.r = (u8)(colorF[0] * 255);
        color.g = (u8)(colorF[1] * 255);
        color.b = (u8)(colorF[2] * 255);
        CVarSetColor(CVAR_PVP_ANCHOR("Color.Value"), color);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    ImGui::EndDisabled();

    ImGui::Separator();

    if (!isConnected) {
        if (ImGui::Button("Connect to PvP Server")) {
            pvp->Enable();
        }
    } else {
        if (ImGui::Button("Disconnect")) {
            pvp->Disable();
        }

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Connected");

        ImGui::Separator();
        ImGui::Text("Game State: %s", sHGStateNames[pvp->gameState]);

        if (pvp->gameState == PVP_HG_COUNTDOWN) {
            ImGui::Text("Starting in: %d", pvp->countdownTimer);
        }

        if (pvp->gameState == PVP_HG_PLAYING) {
            ImGui::Text("Alive: %d", pvp->aliveCount);
            if (pvp->isEliminated) {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "ELIMINATED - Spectating");
            }
        }

        // Player list
        ImGui::Separator();
        ImGui::Text("Players:");
        for (auto& [clientId, client] : pvp->clients) {
            if (client.self) continue;

            ImVec4 nameColor;
            if (!client.isAlive && pvp->gameState == PVP_HG_PLAYING) {
                nameColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Gray = dead
            } else if (client.isReady) {
                nameColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green = ready
            } else {
                nameColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // White = not ready
            }
            ImGui::TextColored(nameColor, "  %s", client.name.c_str());
            if (!client.isAlive && pvp->gameState == PVP_HG_PLAYING) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "[DEAD]");
            }
            if (client.kills > 0) {
                ImGui::SameLine();
                ImGui::Text("(%d kills)", client.kills);
            }
        }

        // Lobby buttons
        if (pvp->gameState == PVP_HG_LOBBY) {
            ImGui::Separator();
            if (ImGui::Button("Ready")) {
                pvp->SendPacket_Ready();
            }
            ImGui::SameLine();
            if (ImGui::Button("Start Game (Host)")) {
                pvp->SendPacket_StartGame();
            }
        }

        // Kill feed
        if (!pvp->killFeed.empty()) {
            ImGui::Separator();
            ImGui::Text("Kill Feed:");
            for (auto& msg : pvp->killFeed) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "  %s", msg.c_str());
            }
        }
    }
}

void RegisterPvPAnchorMenu() {
    WidgetPath path = { "Network", "PvP Arena", SECTION_COLUMN_1 };
    SohGui::mSohMenu->AddWidget(path, "PvPAnchorMainMenu", WIDGET_CUSTOM)
        .CustomFunction(PvPAnchorMainMenu)
        .HideInSearch(true);
}

static RegisterMenuInitFunc pvpMenuInitFunc(RegisterPvPAnchorMenu);
#endif
