#pragma once

#include <libultraship/libultraship.h>

#ifdef __cplusplus
#include <string>
#include <vector>

class ModMenuWindow : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override{};
};

// Returns the list of enabled .o2r mod filenames (no extension) — used by Harpoon
// skin sync to inform remote clients of globally-mounted mods for divergence warnings.
const std::vector<std::string>& ModMenu_GetEnabledMods();
#endif