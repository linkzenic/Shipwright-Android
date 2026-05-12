#ifndef SOH_NETWORK_HARPOON_GAMEMODE_HUD_H
#define SOH_NETWORK_HARPOON_GAMEMODE_HUD_H

#ifdef __cplusplus

#include <libultraship/libultraship.h>
#include <memory>

namespace HarpoonHud {

// Single GuiWindow that hosts both the Prop Hunt HUD and the Triforce Thief
// HUD. It is always-on while a Harpoon connection is active and the active
// gamemode is one of those two; otherwise its DrawElement is a no-op so it
// stays invisible. The actual contents come from
// HarpoonPropHunt::DrawHud() / HarpoonTriforceThief::DrawHud().
class Window final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void InitElement() override {}
    void UpdateElement() override {}
    void DrawElement() override;
};

// Register the singleton window with libultraship's Gui. Idempotent — safe to
// call from Harpoon::Enable() on each connect.
void Register();

}  // namespace HarpoonHud

#endif  // __cplusplus
#endif  // SOH_NETWORK_HARPOON_GAMEMODE_HUD_H
