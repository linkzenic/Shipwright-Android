// =============================================================================
// PikachuHud — ImGui HUD for the Broken-Modes Pikachu mode.
//
// Visual target: the "Esquinas Compactas (minimal)" mockup — hand-drawn sticker
// style (thick dark ink outlines #2c2825, cream fills #fffdf8, SOLID offset
// drop shadows, compact corner clusters):
//   * TOP-LEFT   — character card: round Pikachu portrait, HP bar (green) and
//                  G-MAX bar (pink, the magic meter), ESTADO status chip
//                  (pokeball by default; paralyzed/burned/freeze/sleep after
//                  the matching damage type / voluntary sleep).
//   * BOTTOM-LEFT— D-pad move diamond: Up = Gigantamax/Dragon (icon swaps with
//                  the available action), Down = Iron, Right = Dark, Left = Sleep.
//   * BOTTOM-RIGHT— face buttons: B = Electric (primary, thick ring), A =
//                  Fighting (swaps to Water = fast swim while swimming),
//                  X/C-Left = Jump, Y/C-Right = Quick Attack; plus the RB/C-Down
//                  grass-dash pill.
//
// Implemented as a Ship::GuiWindow exactly like Sm64CapsHud.cpp: Draw() runs
// inside the Gui frame (before ImGui::Render) so foreground-drawlist drawing
// works; the window self-registers on the first PikachuHud_DrawImGui() call
// (made every frame from Interface_Draw in z_parameter.c).
// =============================================================================

#include <imgui.h>
#include <cmath>
#include <memory>
#include <string>

#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <libultraship/bridge/consolevariablebridge.h>

extern "C" {
#include "z64.h"
extern PlayState* gPlayState;
extern SaveContext gSaveContext;

// Pikachu form state (pikachu_form.cpp / mm_player_form.cpp).
u8 MmForm_IsPikachuActive(void);
extern u8 gPikaStatus;          // 0 none, 1 paralyzed, 2 burned, 3 freeze, 4 sleep
extern u8 gPikaInWater;         // A slot shows the water (fast swim) icon
extern u8 gPikaGigantamaxMode;  // Gigantamax currently on
}

namespace {

// ── Palette (from the mockup) ───────────────────────────────────────────────
constexpr ImU32 kInk = IM_COL32(44, 40, 37, 255);        // #2c2825 outlines/text
constexpr ImU32 kInkShadow = IM_COL32(44, 40, 37, 217);  // solid sticker shadow
constexpr ImU32 kCream = IM_COL32(255, 253, 248, 255);   // #fffdf8 panel fill
constexpr ImU32 kHpGreen = IM_COL32(109, 187, 90, 255);  // #6dbb5a
constexpr ImU32 kGmaxPink = IM_COL32(210, 81, 127, 255); // #d2517f
constexpr ImU32 kBadgeYellow = IM_COL32(224, 177, 58, 255); // #e0b13a (ESTADO ring)
constexpr ImU32 kMuted = IM_COL32(107, 100, 93, 255);    // #6b645d labels

struct PikaIcon {
    const char* name;    // GUI texture registration name
    const char* resPath; // OTR resource path (no __OTR__ prefix)
};

// Index aliases into kIcons.
enum {
    ICON_PIKACHU,
    ICON_FIGHTING,
    ICON_WATER,
    ICON_LIGHTNING,
    ICON_COLORLESS,
    ICON_GRASS,
    ICON_METAL,
    ICON_DARKNESS,
    ICON_PSYCHIC,
    ICON_DRAGON,
    ICON_PARALYZED,
    ICON_BURNED,
    ICON_FREEZE,
    ICON_SLEEP,
    ICON_COUNT,
};

const PikaIcon kIcons[ICON_COUNT] = {
    { "PikaHud_Pikachu", "textures/pikachu/gPikaIconPikachuTex" },
    { "PikaHud_Fighting", "textures/pikachu/gPikaIconFightingTex" },
    { "PikaHud_Water", "textures/pikachu/gPikaIconWaterTex" },
    { "PikaHud_Lightning", "textures/pikachu/gPikaIconLightningTex" },
    { "PikaHud_Colorless", "textures/pikachu/gPikaIconColorlessTex" },
    { "PikaHud_Grass", "textures/pikachu/gPikaIconGrassTex" },
    { "PikaHud_Metal", "textures/pikachu/gPikaIconMetalTex" },
    { "PikaHud_Darkness", "textures/pikachu/gPikaIconDarknessTex" },
    { "PikaHud_Psychic", "textures/pikachu/gPikaIconPsychicTex" },
    { "PikaHud_Dragon", "textures/pikachu/gPikaIconDragonTex" },
    { "PikaHud_Paralyzed", "textures/pikachu/gPikaIconParalyzedTex" },
    { "PikaHud_Burned", "textures/pikachu/gPikaIconBurnedTex" },
    { "PikaHud_Freeze", "textures/pikachu/gPikaIconFreezeTex" },
    { "PikaHud_Sleep", "textures/pikachu/gPikaIconSleepTex" },
};

bool sTexturesLoaded = false;
bool sIconAvailable[ICON_COUNT] = {};

void EnsureTextures() {
    if (sTexturesLoaded) {
        return;
    }
    // Gui::LoadGuiTexture null-derefs (Gui.cpp:1008) when the resource path is
    // missing — e.g. soh.o2r not yet repacked with the pikachu icons. Check the
    // virtual filesystem first and simply skip absent icons: the HUD then draws
    // its panels/shapes without images instead of crashing the game.
    auto ctx = Ship::Context::GetInstance();
    auto rm = ctx ? ctx->GetResourceManager() : nullptr;
    auto am = rm ? rm->GetArchiveManager() : nullptr;
    auto gui = ctx->GetWindow()->GetGui();
    for (int i = 0; i < ICON_COUNT; i++) {
        sIconAvailable[i] = (am != nullptr) && am->HasFile(std::string(kIcons[i].resPath));
        if (sIconAvailable[i]) {
            gui->LoadGuiTexture(kIcons[i].name, kIcons[i].resPath, ImVec4(1, 1, 1, 1));
        }
    }
    sTexturesLoaded = true;
}

ImTextureID IconTex(int idx) {
    if (!sIconAvailable[idx]) {
        return 0; // icon not in soh.o2r (repack pending) — draw without image
    }
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    return gui->GetTextureByName(kIcons[idx].name);
}

// ── Sticker primitives (solid offset shadow + thick ink outline) ─────────────
void StickerCircle(ImDrawList* dl, ImVec2 c, float r, ImU32 fill, float s, float ringMul = 1.0f) {
    dl->AddCircleFilled(ImVec2(c.x + 2.5f * s, c.y + 3.0f * s), r, kInkShadow, 32);
    dl->AddCircleFilled(c, r, fill, 32);
    dl->AddCircle(c, r, kInk, 32, 2.2f * s * ringMul);
}

void StickerRect(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 fill, float rounding, float s) {
    dl->AddRectFilled(ImVec2(mn.x + 3.0f * s, mn.y + 4.0f * s), ImVec2(mx.x + 3.0f * s, mx.y + 4.0f * s), kInkShadow,
                      rounding);
    dl->AddRectFilled(mn, mx, fill, rounding);
    dl->AddRect(mn, mx, kInk, rounding, 0, 2.2f * s);
}

void IconInCircle(ImDrawList* dl, ImVec2 c, float r, int iconIdx, float s, ImU32 fill = kCream,
                  float ringMul = 1.0f) {
    StickerCircle(dl, c, r, fill, s, ringMul);
    ImTextureID tex = IconTex(iconIdx);
    if (tex != 0) {
        float m = r * 0.72f; // icon margin inside the ring
        dl->AddImage(tex, ImVec2(c.x - m, c.y - m), ImVec2(c.x + m, c.y + m));
    }
}

// Small uppercase label centered under a point.
void Label(ImDrawList* dl, ImVec2 c, const char* text, float s) {
    ImVec2 sz = ImGui::CalcTextSize(text);
    dl->AddText(ImVec2(c.x - sz.x * 0.5f, c.y), kMuted, text);
    (void)s;
}

// Horizontal bar: cream track + colored fill + ink outline + tiny left label.
void Bar(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float ratio, ImU32 fillCol, const char* label, float s) {
    if (ratio < 0.0f) {
        ratio = 0.0f;
    }
    if (ratio > 1.0f) {
        ratio = 1.0f;
    }
    float rounding = (mx.y - mn.y) * 0.5f;
    dl->AddRectFilled(mn, mx, kCream, rounding);
    if (ratio > 0.01f) {
        dl->AddRectFilled(mn, ImVec2(mn.x + (mx.x - mn.x) * ratio, mx.y), fillCol, rounding);
    }
    dl->AddRect(mn, mx, kInk, rounding, 0, 1.8f * s);
    ImVec2 sz = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(mn.x - sz.x - 6.0f * s, mn.y + (mx.y - mn.y - sz.y) * 0.5f), kInk, label);
}

class PikachuHudWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void InitElement() override {}
    void DrawElement() override {}
    void UpdateElement() override {}
    void Draw() override;
};

void PikachuHudWindow::Draw() {
    // SYSTEM 2 only: the secret Broken-Modes Pikachu mode. A pokeball-item
    // transformation (system 1) keeps the vanilla OOT UI untouched.
    if (!CVarGetInteger("gPikachuMode", 0) || !MmForm_IsPikachuActive()) {
        return;
    }
    if (gPlayState == nullptr || gPlayState->pauseCtx.state != 0) {
        return;
    }
    // Style 0 (default): Pokemon-type icons are drawn over the vanilla OOT
    // buttons by Interface_Draw (PikaMode_ButtonIcon) — here we only add the
    // status card. Style 1: full corner HUD (clusters + LB/RB pills too).
    bool cornerStyle = CVarGetInteger("gPikaHud.Style", 0) == 1;
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    if (gui->GetMenuOrMenubarVisible()) {
        return;
    }
    EnsureTextures();

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList(vp);
    ImVec2 disp = vp->Size;
    float s = disp.y / 600.0f;
    if (s < 0.6f) {
        s = 0.6f;
    }

    // ════ TOP-LEFT: character card ═══════════════════════════════════════════
    {
        ImVec2 cardMin(16.0f * s, 16.0f * s);
        ImVec2 cardMax(cardMin.x + 250.0f * s, cardMin.y + 84.0f * s);
        StickerRect(dl, cardMin, cardMax, kCream, 14.0f * s, s);

        // Round portrait
        ImVec2 face(cardMin.x + 36.0f * s, (cardMin.y + cardMax.y) * 0.5f);
        IconInCircle(dl, face, 26.0f * s, ICON_PIKACHU, s);

        // Name
        dl->AddText(ImVec2(cardMin.x + 70.0f * s, cardMin.y + 8.0f * s), kInk, "PIKACHU");

        // HP (hearts) + G-MAX (magic) bars
        float hpRatio = (gSaveContext.healthCapacity > 0)
                            ? (float)gSaveContext.health / (float)gSaveContext.healthCapacity
                            : 0.0f;
        float mpRatio = (gSaveContext.magicCapacity > 0)
                            ? (float)gSaveContext.magic / (float)gSaveContext.magicCapacity
                            : 0.0f;
        float barX0 = cardMin.x + 102.0f * s;
        float barX1 = cardMax.x - 44.0f * s;
        Bar(dl, ImVec2(barX0, cardMin.y + 32.0f * s), ImVec2(barX1, cardMin.y + 42.0f * s), hpRatio, kHpGreen, "HP",
            s);
        Bar(dl, ImVec2(barX0, cardMin.y + 52.0f * s), ImVec2(barX1, cardMin.y + 62.0f * s), mpRatio, kGmaxPink,
            "G-MAX", s);

        // ESTADO chip (status): pokeball-ish default, else the status icon.
        ImVec2 chip(cardMax.x - 22.0f * s, (cardMin.y + cardMax.y) * 0.5f);
        if (gPikaStatus == 0) {
            // Drawn pokeball: red top half, cream bottom, ink band + button.
            float r = 14.0f * s;
            StickerCircle(dl, chip, r, kCream, s);
            dl->PathArcTo(chip, r * 0.86f, 3.14159265f, 2.0f * 3.14159265f, 24);
            dl->PathFillConvex(IM_COL32(214, 60, 50, 255));
            dl->AddLine(ImVec2(chip.x - r * 0.86f, chip.y), ImVec2(chip.x + r * 0.86f, chip.y), kInk, 2.0f * s);
            dl->AddCircleFilled(chip, 3.4f * s, kCream, 16);
            dl->AddCircle(chip, 3.4f * s, kInk, 16, 1.6f * s);
        } else {
            int idx = (gPikaStatus == 1)   ? ICON_PARALYZED
                      : (gPikaStatus == 2) ? ICON_BURNED
                      : (gPikaStatus == 3) ? ICON_FREEZE
                                           : ICON_SLEEP;
            ImU32 ring = (gPikaStatus == 1) ? kBadgeYellow : kCream;
            IconInCircle(dl, chip, 14.0f * s, idx, s, ring);
        }
        Label(dl, ImVec2(chip.x, chip.y + 18.0f * s), "ESTADO", s);
    }

    if (!cornerStyle) {
        return; // overlay style: move icons live on the OOT buttons themselves
    }

    // ════ LEFT: LB pill (crouch) + D-pad move diamond ════════════════════════
    // Raised off the bottom edge so it never covers the rupee/key counters.
    {
        float r = 17.0f * s;
        ImVec2 center(70.0f * s, disp.y - 190.0f * s);
        float gap = 40.0f * s;

        // LB pill — crouch (mirrors the RB grass pill on the right side).
        ImVec2 lbMin(center.x - 24.0f * s, center.y - gap - 52.0f * s);
        ImVec2 lbMax(lbMin.x + 92.0f * s, lbMin.y + 30.0f * s);
        StickerRect(dl, lbMin, lbMax, kCream, 15.0f * s, s);
        dl->AddText(ImVec2(lbMin.x + 8.0f * s, lbMin.y + 7.0f * s), kInk, "LB");
        dl->AddText(ImVec2(lbMin.x + 34.0f * s, lbMin.y + 7.0f * s), kMuted, "crouch");
        // Up: Gigantamax when on/affordable (placeholder pikachu icon), else Dragon charge.
        int upIcon = (gPikaGigantamaxMode || gSaveContext.magic >= 48) ? ICON_PIKACHU : ICON_DRAGON;
        IconInCircle(dl, ImVec2(center.x, center.y - gap), r, upIcon, s,
                     gPikaGigantamaxMode ? kBadgeYellow : kCream);
        IconInCircle(dl, ImVec2(center.x, center.y + gap), r, ICON_METAL, s);
        IconInCircle(dl, ImVec2(center.x + gap, center.y), r, ICON_DARKNESS, s);
        IconInCircle(dl, ImVec2(center.x - gap, center.y), r, ICON_PSYCHIC, s);
        Label(dl, ImVec2(center.x, center.y + gap + r + 6.0f * s), "D-PAD", s);
    }

    // ════ BOTTOM-RIGHT: face buttons + grass pill ════════════════════════════
    {
        float r = 17.0f * s;
        ImVec2 center(disp.x - 96.0f * s, disp.y - 84.0f * s);
        float gap = 40.0f * s;
        // B (right) = Electric — the primary move: bigger circle + thick ring.
        IconInCircle(dl, ImVec2(center.x + gap, center.y), r * 1.25f, ICON_LIGHTNING, s, kCream, 1.8f);
        // A (bottom) = Fighting, or Water (fast swim) while in water.
        IconInCircle(dl, ImVec2(center.x, center.y + gap), r, gPikaInWater ? ICON_WATER : ICON_FIGHTING, s);
        // X (left, physical → C-Left) = Jump (flying/colorless).
        IconInCircle(dl, ImVec2(center.x - gap, center.y), r, ICON_COLORLESS, s);
        // Y (top, physical → C-Right) = Quick Attack.
        IconInCircle(dl, ImVec2(center.x, center.y - gap), r, ICON_LIGHTNING, s);
        Label(dl, ImVec2(center.x, center.y + gap + r + 6.0f * s), "ABXY", s);

        // RB pill (physical → C-Down): grass dash.
        ImVec2 pillMin(center.x - gap - 24.0f * s, center.y - gap - 52.0f * s);
        ImVec2 pillMax(pillMin.x + 92.0f * s, pillMin.y + 30.0f * s);
        StickerRect(dl, pillMin, pillMax, kCream, 15.0f * s, s);
        dl->AddText(ImVec2(pillMin.x + 8.0f * s, pillMin.y + 7.0f * s), kInk, "RB");
        ImTextureID gtex = IconTex(ICON_GRASS);
        if (gtex != 0) {
            float m = 11.0f * s;
            ImVec2 gc(pillMax.x - 20.0f * s, (pillMin.y + pillMax.y) * 0.5f);
            dl->AddImage(gtex, ImVec2(gc.x - m, gc.y - m), ImVec2(gc.x + m, gc.y + m));
        }
    }
}

// ── Pikachu Controls window ──────────────────────────────────────────────────
// Opened from Skijer's NEI → Controls → "Pikachu Controls". Assigns the N64
// button for each SECRET-mode move (gPikaBind.*) and picks the mode UI style.
// Standard GuiWindow: the base Draw() wraps DrawElement() in an ImGui window.

struct PikaBindDef {
    const char* label;
    const char* cvar;
    int def;
};
const PikaBindDef kBindDefs[] = {
    { "Jump (X)", "gPikaBind.Jump", BTN_CLEFT },
    { "Quick Attack (Y)", "gPikaBind.QuickAttack", BTN_CRIGHT },
    { "Grass Dash (RB)", "gPikaBind.Grass", BTN_CDOWN },
    { "Gigantamax / Charge", "gPikaBind.Gmax", BTN_DUP },
    { "Iron Tail", "gPikaBind.Iron", BTN_DDOWN },
    { "Dark Bomb", "gPikaBind.Dark", BTN_DRIGHT },
    { "Sleep", "gPikaBind.Sleep", BTN_DLEFT },
};
const int kBindBtnMasks[] = { BTN_CLEFT, BTN_CRIGHT, BTN_CDOWN,  BTN_CUP, BTN_DUP,
                              BTN_DDOWN, BTN_DLEFT,  BTN_DRIGHT, BTN_Z };
const char* kBindBtnNames[] = { "C-Left", "C-Right", "C-Down", "C-Up", "D-Up",
                                "D-Down", "D-Left",  "D-Right", "Z" };
constexpr int kBindBtnCount = 9;

class PikachuControlsWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void InitElement() override {}
    void UpdateElement() override {}
    void DrawElement() override {
        ImGui::TextWrapped("Controls for the SECRET Pikachu mode (Broken Modes). The classic "
                           "pokeball transformation is untouched (items on C, vanilla UI).");
        ImGui::TextWrapped("Map physical X / Y / RB to C-Left / C-Right / C-Down in the input "
                           "editor so the right stick stays free for the camera.");
        ImGui::Separator();

        const char* styles[] = { "Icons over OOT buttons", "Pikachu corner HUD" };
        int style = CVarGetInteger("gPikaHud.Style", 0);
        if (style < 0 || style > 1) {
            style = 0;
        }
        if (ImGui::Combo("UI Style", &style, styles, 2)) {
            CVarSetInteger("gPikaHud.Style", style);
            CVarSave();
        }
        ImGui::Separator();

        for (const auto& bind : kBindDefs) {
            int cur = CVarGetInteger(bind.cvar, bind.def);
            int idx = 0;
            for (int i = 0; i < kBindBtnCount; i++) {
                if (kBindBtnMasks[i] == cur) {
                    idx = i;
                    break;
                }
            }
            if (ImGui::Combo(bind.label, &idx, kBindBtnNames, kBindBtnCount)) {
                CVarSetInteger(bind.cvar, kBindBtnMasks[idx]);
                CVarSave();
            }
        }
    }
};

std::shared_ptr<PikachuHudWindow> sHudWindow = nullptr;
std::shared_ptr<PikachuControlsWindow> sControlsWindow = nullptr;

} // namespace

// Opens (registering on first use) the Pikachu control-assignment window.
// Called from the "Pikachu Controls" button in SohMenuNEI.cpp.
extern "C" void PikachuControls_OpenWindow(void) {
    auto ctx = Ship::Context::GetInstance();
    if (ctx == nullptr || ctx->GetWindow() == nullptr) {
        return;
    }
    auto gui = ctx->GetWindow()->GetGui();
    if (gui == nullptr) {
        return;
    }
    if (sControlsWindow == nullptr) {
        sControlsWindow = std::make_shared<PikachuControlsWindow>("gPikaControlsWindow", "Pikachu Controls");
        gui->AddGuiWindow(sControlsWindow);
    }
    sControlsWindow->Show();
}

// Called every frame from Interface_Draw (z_parameter.c). On the first call it
// registers the GuiWindow with the port's Gui; after that the window draws
// itself at the correct point in the ImGui frame, so this is a cheap no-op.
// (Same self-registration pattern as Sm64CapsHud_DrawImGui.)
extern "C" void PikachuHud_DrawImGui(void) {
    if (sHudWindow != nullptr) {
        return;
    }
    auto ctx = Ship::Context::GetInstance();
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
    sHudWindow = std::make_shared<PikachuHudWindow>("gPikachuHudWindow", "Pikachu HUD");
    gui->AddGuiWindow(sHudWindow);
}
