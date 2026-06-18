// =============================================================================
// Sm64CapsHud — ImGui power-up HUD for SM64 Mario mode ("Star Spirit" panel).
//
// Drawn with ImGui's foreground draw list (real circles, arcs, icon textures
// and text) instead of the N64 HUD, anchored to the RIGHT edge of the screen.
// Four rows, top→bottom = Wing, Metal, Vanish, Fire. Each row is a charge
// ring (a real arc that drains green→yellow→red while ACTIVE and fills blue
// while RECHARGING), the cap icon INSIDE the ring, a seconds-remaining badge,
// and the D-pad bind shown as text to the LEFT of the ring. While a cap is
// ACTIVE its icon is replaced by the Mario-mask icon — the "press the cap's
// D-pad button again to disable" cue.
//
// IMPORTANT: this is implemented as a Ship::GuiWindow so its Draw() runs inside
// the Gui's frame (Gui::DrawElement, BEFORE ImGui::Render). Drawing from the
// game's Interface_Draw path instead does NOT work: that runs after the ImGui
// frame is rendered, so foreground-drawlist additions are discarded (and the
// no-arg GetForegroundDrawList() crashes there because CurrentWindow is null).
// The window self-registers on the first call to Sm64CapsHud_DrawImGui() (which
// Interface_Draw still calls every frame) — after that it draws itself.
//
// This is the ONE C++ file of the SM64 expansion (the rest is C #included into
// z_player.c). Timer state lives in C (sm64_mario_items.c); read here through
// the Sm64MarioCaps_* accessors.
// =============================================================================

#include <imgui.h>
#include <cmath>
#include <cfloat>
#include <memory>
#include <string>

#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <libultraship/libultraship.h>
#include <fast/Fast3dGui.h>

extern "C" {
#include "z64.h"
extern PlayState* gPlayState;
extern SaveContext gSaveContext;

// World→view projection (z_actor.c). projectedPos = clip-space x,y,z; *invW is
// the clamped inverse W. Screen NDC = projectedPos.xy * invW (see
// Actor_GetScreenPos). Used to float the HP dial above Mario's head.
void func_8002BE04(PlayState* play, Vec3f* worldPos, Vec3f* projectedPos, f32* invW);

// Cap timer accessors (sm64_mario_items.c). Slot index order matches the HUD
// top→bottom: 0 = Wing, 1 = Metal, 2 = Vanish, 3 = Fire.
uint8_t  Sm64MarioCaps_GetPhase(int32_t idx);            // 0 ready, 1 active, 2 cooldown
float    Sm64MarioCaps_GetCharge(int32_t idx);           // 0..1
int32_t  Sm64MarioCaps_GetRemainingSeconds(int32_t idx);
int32_t  Sm64MarioCaps_GetActiveIndex(void);

// Mario's independent health as 0..8 wedges (SM64 power-meter segments).
int32_t  Sm64Mario_GetHealthWedges(void);

// Resource existence check (used to fall back to a shipped button texture when
// the user's d-right.png hasn't been packed into the o2r yet).
uint8_t ResourceMgr_FileExists(const char* resName);
}

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr int kSlotCount = 4;
constexpr int kPhaseReady = 0;
constexpr int kPhaseActive = 1;
constexpr int kPhaseCooldown = 2;

struct CapSlot {
    const char* texName;   // GUI texture registration name
    const char* resPath;   // OTR resource path
    const char* bind;      // D-pad bind label (literal button text)
    int rot;               // d-right.png CW quarter-turns to point at this cap's D-pad dir
};

// Slot order Wing, Metal, Vanish, Fire. Binds: Wing=D-Down, Metal=D-Left,
// Vanish=D-Right, Fire=D-Up (see Sm64Mario_HandleCapDpad in sm64_mario_items.c).
// rot: the base button texture (d-right.png) points RIGHT; rotate it CW to face
// each cap's D-pad direction. Down=1 (90°), Left=2 (180°), Right=0, Up=3 (270°).
const CapSlot kSlots[kSlotCount] = {
    { "Sm64Cap_Wing",   "textures/icon_item_custom/gItemIconWingCapTex",   "D-Down",  1 },
    { "Sm64Cap_Metal",  "textures/icon_item_custom/gItemIconMetalCapTex",  "D-Left",  2 },
    { "Sm64Cap_Vanish", "textures/icon_item_custom/gItemIconVanishCapTex", "D-Right", 0 },
    { "Sm64Cap_Fire",   "textures/icon_item_custom/gItemIconFireFlowerTex", "D-Up",   3 },
};

// Button indicators are raw PNGs (textures/buttons/), loaded via the InputViewer's
// LoadTextureFromRawImage path (NOT LoadGuiTexture, which is for compiled textures).
// d-right.png is rotated per-cap; CDown.png labels the C-Down item row.
const char* kDpadBtnTexName  = "Sm64DPadBtn";   // textures/buttons/d-right.png
const char* kCDownBtnTexName = "Sm64CDownBtn";  // textures/buttons/CDown.png

// HUD display order (top -> bottom). Swaps Wing and Fire visually (panel position
// only — bindings/timers stay tied to their real slot). Real slots: 0=Wing,
// 1=Metal, 2=Vanish, 3=Fire. Here: Fire(3) on top, Wing(0) at the bottom.
const int kDisplayOrder[kSlotCount] = { 3, 1, 2, 0 };

const char* kMaskTexName = "Sm64Cap_Mask";
const char* kMaskResPath = "textures/icon_item_custom/gItemIconMarioMaskTex";

bool sTexturesLoaded = false;

void EnsureTextures() {
    if (sTexturesLoaded) {
        return;
    }
    auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
    for (const auto& slot : kSlots) {
        gui->LoadGuiTexture(slot.texName, slot.resPath, ImVec4(1, 1, 1, 1));
    }
    gui->LoadGuiTexture(kMaskTexName, kMaskResPath, ImVec4(1, 1, 1, 1));
    // SM64 power-meter dial — 9 states (0..8 wedges). Replaces the OOT hearts.
    for (int n = 0; n <= 8; n++) {
        std::string name = "Sm64HP" + std::to_string(n);
        std::string path = "textures/mario_hp/gMarioHP" + std::to_string(n) + "Tex";
        gui->LoadGuiTexture(name, path, ImVec4(1, 1, 1, 1));
    }
    // Button indicators (raw PNGs). Use the user's d-right.png if it's been packed
    // into the o2r; otherwise fall back to the always-shipped DPadRight.png (same
    // RIGHT-pointing orientation, so the per-cap rotation still works). The guard
    // matters because LoadTextureFromResource dereferences a NULL resource (crash)
    // — never feed it a path that isn't in the archive.
    const char* dpadPath = ResourceMgr_FileExists("textures/buttons/d-right.png")
                               ? "textures/buttons/d-right.png"
                               : "textures/buttons/DPadRight.png";
    gui->LoadTextureFromRawImage(kDpadBtnTexName, dpadPath);
    gui->LoadTextureFromRawImage(kCDownBtnTexName, "textures/buttons/CDown.png");
    sTexturesLoaded = true;
}

// Draws a texture rotated CW by `rotQuarters` * 90° (0=right, 1=down, 2=left,
// 3=up for d-right.png). Keeps the screen square fixed and rotates the UVs:
// screen corner k samples texture corner (k - rot) mod 4 (TL,TR,BR,BL order).
void DrawRotatedImage(ImDrawList* dl, ImTextureID tex, ImVec2 center, float size, int rotQuarters, ImU32 col) {
    float h = size * 0.5f;
    ImVec2 p1(center.x - h, center.y - h); // screen TL
    ImVec2 p2(center.x + h, center.y - h); // screen TR
    ImVec2 p3(center.x + h, center.y + h); // screen BR
    ImVec2 p4(center.x - h, center.y + h); // screen BL
    const ImVec2 uv[4] = { ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1) };
    int r = ((rotQuarters % 4) + 4) % 4;
    dl->AddImageQuad(tex, p1, p2, p3, p4, uv[(0 - r + 4) % 4], uv[(1 - r + 4) % 4], uv[(2 - r + 4) % 4],
                     uv[(3 - r + 4) % 4], col);
}

ImU32 ChargeColor(int phase, float charge) {
    if (phase == kPhaseCooldown) {
        return IM_COL32(0x34, 0xB6, 0xFF, 255); // recharging blue
    }
    if (charge > 0.55f) {
        return IM_COL32(0x5F, 0xD2, 0x3A, 255); // green
    }
    if (charge > 0.28f) {
        return IM_COL32(0xF4, 0xC4, 0x2A, 255); // yellow
    }
    return IM_COL32(0xFF, 0x4D, 0x3D, 255); // red
}

// GuiWindow whose Draw() runs during the Gui frame (before ImGui::Render), so
// our foreground-drawlist draws actually render. All gating + drawing lives in
// Draw(); the Element overrides are unused.
class Sm64CapsHudWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void InitElement() override {}
    void DrawElement() override {}
    void UpdateElement() override {}
    void Draw() override;
};

void Sm64CapsHudWindow::Draw() {
    if (!CVarGetInteger("gSm64Mario", 0)) {
        return;
    }
    if (gPlayState == nullptr || gPlayState->pauseCtx.state != 0) {
        return; // hide while paused / in the subscreen
    }
    auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
    if (gui->GetMenuOrMenubarVisible()) {
        return; // don't draw over the port menu
    }

    EnsureTextures();

    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) {
        return;
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList(viewport);
    if (dl == nullptr) {
        return;
    }
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    if (disp.x < 1.0f || disp.y < 1.0f) {
        return;
    }
    ImFont* font = ImGui::GetFont();
    if (font == nullptr) {
        return;
    }

    // Resolution-independent scale (designed against a 600px-tall window).
    float s = disp.y / 600.0f;
    if (s < 0.6f) {
        s = 0.6f;
    }

    const float diameter = 48.0f * s;
    const float radius = diameter * 0.5f;
    const float ringThick = 5.0f * s;
    const float iconSize = diameter * 0.60f;
    const float pitch = diameter + 7.0f * s;  // tighter: rings nearly touch (was +20)
    const float rightMargin = 26.0f * s;
    const float topY = 56.0f * s;
    const float timerFontSize = 15.0f * s;
    const float btnSize = 22.0f * s;          // D-pad / C-Down indicator size
    const float btnGap = 7.0f * s;            // gap between ring and its button

    const float centerX = disp.x - rightMargin - radius;

    // SM64 power-meter HP dial: like SM64, it only appears WHEN HEALTH CHANGES
    // (damage/heal), floats ABOVE MARIO's head, then fades out after a few
    // seconds. State persists across frames; timed off ImGui delta-time so it's
    // framerate-independent.
    {
        static int sPrevWedges = -1;
        static float sShowTimeLeft = 0.0f; // seconds remaining visible
        static bool sInit = false;

        int wedges = Sm64Mario_GetHealthWedges(); // Mario's own 0..8 segments
        if (!sInit) {
            sPrevWedges = wedges;
            sInit = true;
        }
        if (wedges != sPrevWedges) {
            sShowTimeLeft = 3.0f; // show for 3s after a change
            sPrevWedges = wedges;
        }

        if (sShowTimeLeft > 0.0f) {
            sShowTimeLeft -= ImGui::GetIO().DeltaTime;

            // GET_PLAYER lives in macros.h (not included here) — inline it.
            Player* player =
                (gPlayState != nullptr)
                    ? (Player*)gPlayState->actorCtx.actorLists[ACTORCAT_PLAYER].head
                    : nullptr;
            if (player != nullptr) {
                // Project a point above Mario's head to screen pixels.
                Vec3f world = player->actor.world.pos;
                world.y += 64.0f; // above the head (OOT units)
                Vec3f proj;
                f32 invW;
                func_8002BE04(gPlayState, &world, &proj, &invW);
                float sx = (proj.x * invW * 0.5f + 0.5f) * disp.x;
                float sy = (proj.y * invW * -0.5f + 0.5f) * disp.y;

                // Only draw when the projected point is sanely on-screen.
                if (sx > -disp.x && sx < disp.x * 2.0f && sy > -disp.y && sy < disp.y * 2.0f) {
                    std::string hpName = "Sm64HP" + std::to_string(wedges);
                    ImTextureID hpTex = gui->GetTextureByName(hpName);
                    if (hpTex != (ImTextureID)0) {
                        ImVec2 nat = gui->GetTextureSize(hpName);
                        if (nat.y > 0.0f) {
                            float k = (52.0f * s) / nat.y; // target ~52px tall
                            float wpx = nat.x * k;
                            float hpx = nat.y * k;
                            // Fade out over the last 0.6s.
                            float a = (sShowTimeLeft < 0.6f) ? (sShowTimeLeft / 0.6f) : 1.0f;
                            if (a < 0.0f) {
                                a = 0.0f;
                            }
                            ImU32 col = IM_COL32(255, 255, 255, (int)(a * 255.0f));
                            ImVec2 p(sx - wpx * 0.5f, sy - hpx); // centered, bottom at the point
                            dl->AddImage(hpTex, p, ImVec2(p.x + wpx, p.y + hpx), ImVec2(0, 0), ImVec2(1, 1), col);
                        }
                    }
                }
            }
        }
    }

    int activeIdx = Sm64MarioCaps_GetActiveIndex();

    for (int i = 0; i < kSlotCount; i++) {
        int slot = kDisplayOrder[i]; // panel position i shows this real cap slot
        int phase = Sm64MarioCaps_GetPhase(slot);
        float charge = Sm64MarioCaps_GetCharge(slot);
        bool isActive = (slot == activeIdx);

        float cy = topY + i * pitch + radius;
        ImVec2 center(centerX, cy);

        // Ring track (full dark circle) + charge arc. No filled hub — the
        // wheel background is fully transparent so only the ring + icon show.
        dl->AddCircle(center, radius, IM_COL32(0, 0, 0, 130), 64, ringThick);
        if (charge > 0.001f) {
            ImU32 col = ChargeColor(phase, charge);
            float a0 = -kPi * 0.5f;                 // start at top
            float a1 = a0 + charge * (kPi * 2.0f);  // clockwise
            dl->PathArcTo(center, radius, a0, a1, 64);
            dl->PathStroke(col, 0, ringThick);
        }

        // Icon (or Mario mask while ACTIVE). Dimmed while recharging.
        const char* texName = isActive ? kMaskTexName : kSlots[slot].texName;
        ImTextureID tex = gui->GetTextureByName(texName);
        if (tex != (ImTextureID)0) {
            ImU32 tint = (phase == kPhaseCooldown) ? IM_COL32(120, 120, 120, 255) : IM_COL32(255, 255, 255, 255);
            ImVec2 iconMin(center.x - iconSize * 0.5f, center.y - iconSize * 0.5f);
            ImVec2 iconMax(center.x + iconSize * 0.5f, center.y + iconSize * 0.5f);
            dl->AddImage(tex, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), tint);
        }

        // D-pad activation button (d-right.png rotated to this cap's direction),
        // to the LEFT of the ring — Genshin-style "press this to use it" cue.
        ImTextureID dpadTex = gui->GetTextureByName(kDpadBtnTexName);
        if (dpadTex != (ImTextureID)0) {
            ImVec2 btnCenter(center.x - radius - btnGap - btnSize * 0.5f, center.y);
            ImU32 btnTint = (phase == kPhaseCooldown) ? IM_COL32(150, 150, 150, 210) : IM_COL32(255, 255, 255, 255);
            DrawRotatedImage(dl, dpadTex, btnCenter, btnSize, kSlots[slot].rot, btnTint);
        }

        // Timer badge — seconds remaining, only while ACTIVE or COOLDOWN.
        if (phase != kPhaseReady) {
            int secs = Sm64MarioCaps_GetRemainingSeconds(slot);
            std::string txt = std::to_string(secs);
            ImVec2 tsz = font->CalcTextSizeA(timerFontSize, FLT_MAX, 0.0f, txt.c_str());
            ImVec2 badgeCenter(center.x + radius * 0.78f, center.y + radius * 0.78f);
            float padX = 4.0f * s;
            float padY = 2.0f * s;
            ImU32 chargeCol = ChargeColor(phase, charge);
            // Darken the charge color for the chip background.
            ImU32 chipCol = IM_COL32((int)((chargeCol & 0xFF) / 3), (int)(((chargeCol >> 8) & 0xFF) / 3),
                                     (int)(((chargeCol >> 16) & 0xFF) / 3), 230);
            ImVec2 bmin(badgeCenter.x - tsz.x * 0.5f - padX, badgeCenter.y - tsz.y * 0.5f - padY);
            ImVec2 bmax(badgeCenter.x + tsz.x * 0.5f + padX, badgeCenter.y + tsz.y * 0.5f + padY);
            dl->AddRectFilled(bmin, bmax, chipCol, 6.0f * s);
            dl->AddRect(bmin, bmax, IM_COL32(0, 0, 0, 180), 6.0f * s, 0, 1.5f * s);
            dl->AddText(font, timerFontSize, ImVec2(badgeCenter.x - tsz.x * 0.5f, badgeCenter.y - tsz.y * 0.5f),
                        IM_COL32(255, 255, 255, 255), txt.c_str());
        }
    }

    // 5th row: the C-Down ITEM slot, BELOW the 4 caps in the same column. The item
    // (bomb / bombchu / deku nut / ocarina) is assigned + filled in by the C-Down
    // item system (#5, pending); for now this lays out the slot ring + the C-Down
    // button so the position is locked in. When #5 lands, its icon draws in the ring.
    {
        float cy = topY + kSlotCount * pitch + radius;
        ImVec2 center(centerX, cy);
        dl->AddCircle(center, radius, IM_COL32(0, 0, 0, 130), 64, ringThick);
        ImTextureID cdTex = gui->GetTextureByName(kCDownBtnTexName);
        if (cdTex != (ImTextureID)0) {
            ImVec2 btnCenter(center.x - radius - btnGap - btnSize * 0.5f, center.y);
            dl->AddImage(cdTex, ImVec2(btnCenter.x - btnSize * 0.5f, btnCenter.y - btnSize * 0.5f),
                         ImVec2(btnCenter.x + btnSize * 0.5f, btnCenter.y + btnSize * 0.5f));
        }
    }
}

std::shared_ptr<Sm64CapsHudWindow> sHudWindow = nullptr;

} // namespace

// Called every frame from Interface_Draw (z_parameter.c). On the first call it
// registers the GuiWindow with the port's Gui; after that the window draws
// itself at the correct point in the ImGui frame, so this is a cheap no-op.
extern "C" void Sm64CapsHud_DrawImGui(void) {
    if (sHudWindow != nullptr) {
        return;
    }
    auto ctx = Ship::Context::GetRawInstance();
    if (ctx == nullptr) {
        return;
    }
    auto window = ctx->GetWindow();
    if (window == nullptr) {
        return;
    }
    auto gui = window->GetGui();
    if (gui == nullptr) {
        return;
    }
    sHudWindow = std::make_shared<Sm64CapsHudWindow>("gSm64CapsHudWindow", "SM64 Caps HUD");
    gui->AddGuiWindow(sHudWindow);
}
