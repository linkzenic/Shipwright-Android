#include "HarpoonGamemodeHud.h"
#include "Harpoon.h"
#include "PropHunt/PropHunt.h"
#include "TriforceThief/TriforceThief.h"

#include <libultraship/libultraship.h>
#include <spdlog/spdlog.h>

namespace HarpoonHud {

void Window::DrawElement() {
    if (Harpoon::Instance == nullptr || !Harpoon::Instance->isConnected) return;

    switch (Harpoon::Instance->activeGameMode) {
        case HARPOON_MODE_PROP_HUNT:
            HarpoonPropHunt::DrawHud();
            break;
        // No dedicated enum yet for Triforce Thief — gate on the data being
        // loaded + the room's gamemode id instead. Triforce Thief is the only
        // mode that flips inMapSelect / inRound flags via its own events.
        default:
            if (HarpoonTriforceThief::IsLoaded() &&
                (HarpoonTriforceThief::IsInMapSelect() ||
                 HarpoonTriforceThief::IsInRound())) {
                HarpoonTriforceThief::DrawHud();
            }
            break;
    }
}

void Register() {
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    if (gui == nullptr) return;

    static const char* kName = "HarpoonGamemodeHud";
    static const char* kCVar = "gOpenWindows.HarpoonGamemodeHud";

    // Don't double-register — Gui::AddGuiWindow logs an error on duplicate.
    if (gui->GetGuiWindow(kName) != nullptr) {
        return;
    }

    auto window = std::make_shared<HarpoonHud::Window>(kCVar, kName);
    gui->AddGuiWindow(window);
    window->Show();
    SPDLOG_INFO("[Harpoon][HUD] gamemode HUD registered");
}

}  // namespace HarpoonHud
