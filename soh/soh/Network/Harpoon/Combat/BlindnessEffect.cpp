// =============================================================================
// BlindnessEffect — renders a full-screen black overlay while the local
// player's `combatBlindnessFrames` counter is > 0. Used by Dark spell /
// Dark arrow PvP hits.
//
// The render itself uses ImGui's foreground draw list — it's the simplest
// way to overlay a quad on top of the game without touching the Gfx
// pipeline. Alpha ramps in over the first 15 frames and ramps out over the
// last 30, so the blindness fades cleanly.
// =============================================================================

#include "CombatSync.h"
#include "../Harpoon.h"

#include <imgui.h>

extern "C" {
#include "z64.h"
extern PlayState* gPlayState;
}

namespace HarpoonCombat {

// Call once per frame from the SoH post-draw hook (or from libultraship
// frame-end). Reads combatBlindnessFrames on the LOCAL player's
// HarpoonClient entry and draws the overlay accordingly.
void BlindnessEffect_Draw() {
    if (Harpoon::Instance == nullptr) return;
    auto it = Harpoon::Instance->clients.find(Harpoon::Instance->ownClientId);
    if (it == Harpoon::Instance->clients.end()) return;
    uint16_t fr = it->second.combatBlindnessFrames;
    if (fr == 0) return;

    // Alpha envelope — peak near-opaque (0.985) so the player literally
    // cannot see. Ramps out over the last 45 frames so the recovery isn't
    // abrupt. Uses a slight purple/black tint to read as "dark magic"
    // rather than just a generic curtain.
    constexpr float kPeakAlpha = 0.985f;
    float alpha;
    if (fr < 45) {
        alpha = kPeakAlpha * ((float)fr / 45.0f);
    } else {
        alpha = kPeakAlpha;
    }
    if (alpha < 0.02f) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (dl == nullptr) return;
    ImVec2 size = ImGui::GetIO().DisplaySize;
    // Slight purple bias so the darkness reads as magical (R=8, G=0, B=14 / 255).
    ImU32 col = ImColor(0.031f, 0.0f, 0.055f, alpha);
    dl->AddRectFilled(ImVec2(0, 0), size, col);

    // Inner subtle vignette ring — slightly less opaque core so a tiny
    // hint of the world bleeds through the very center. Adds the SW97
    // dark-medallion feel: blackness with a faintly visible silhouette.
    if (alpha > 0.5f) {
        ImVec2 center = ImVec2(size.x * 0.5f, size.y * 0.55f);
        float radius = std::min(size.x, size.y) * 0.08f;
        ImU32 holeCol = ImColor(0.0f, 0.0f, 0.0f, alpha * 0.6f);
        dl->AddCircleFilled(center, radius, holeCol, 48);
    }
}

}  // namespace HarpoonCombat
