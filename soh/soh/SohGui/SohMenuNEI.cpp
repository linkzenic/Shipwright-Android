#include "SohMenu.h"
#include "soh/OTRGlobals.h"
#include "UIWidgets.hpp"
#include "soh/ResourceManagerHelpers.h"
#include "soh/resource/type/PlayerAnimation.h"
#include "soh/resource/type/SohResourceType.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>
#include <string>

extern "C" {
#include <z64.h>
#include "variables.h"
#include "macros.h"
#include "functions.h"
#include "z64animation.h"
#include "transformation_masks/assets/mm_asset_loader.h"
#include "mods/transformation_masks/transformation_masks.h"
#include "mods/pak_loader/pak_loader.h"
#include "mods/voice_pack/voice_pack.h"
void TwilightUpgrade_Grant(void);
extern unsigned char TwilightUpgrade_HasClawshot(void);
extern unsigned char TwilightUpgrade_HasBombArrows(void);
extern unsigned char TwilightUpgrade_HasGaleBoomerang(void);
void TwilightUpgrade_SetClawshot(unsigned char on);
void TwilightUpgrade_SetBombArrows(unsigned char on);
void TwilightUpgrade_SetGaleBoomerang(unsigned char on);
void PikachuControls_OpenWindow(void); // pikachu_hud.cpp — Pikachu mode bindings window
extern PlayState* gPlayState;
u8 GerudoForm_IsActive(void); // gerudo_form.cpp

// MHR moveset overrides — TEMPORARY cataloguing tool, implemented at the
// bottom of THIS file (no separate TU so it stays easy to rip out later).
// z_player.c calls these via local extern decls at its 5 choke points
// (melee start, shield raise, shield loop, roll, dodge hops).
void MhrMoveset_Reload(void);
LinkAnimationHeader* MhrMoveset_GetMeleeAnim(s32 mwa);
LinkAnimationHeader* MhrMoveset_GetShieldAnim(s32 loopPhase);
LinkAnimationHeader* MhrMoveset_GetMoveAnim(s32 moveId);
}

namespace SohGui {

extern std::shared_ptr<SohMenu> mSohMenu;
using namespace UIWidgets;

// =============================================================================
// MHR Anims tab — anim-viewer-style browser over the converted MHR animations
// (gPlayerAnim_mhr_*) with per-anim NOTES, RENAME proposals and ACTION BINDINGS.
// Everything is persisted to nei/mhr_anim_notes.json:
//   - "note"/"rename" are the communication channel for offline repacking
//     (apps/mhr_to_oot reads them to alias the .o2r entries).
//   - "binding" is consumed live by soh/mods/mhr_moveset (z_player.c hooks):
//     B melee slots (PLAYER_MWA_*), R shield raise/loop, A roll, Z hops.
// =============================================================================
namespace {

constexpr const char* kMhrNotesPath = "nei/mhr_anim_notes.json";
constexpr s32 kMhrS16PerFrame = 67;

struct MhrNoteEntry {
    std::string path;
    char note[512] = "";
    char rename[96] = "";
    std::string binding;
};

std::vector<std::pair<std::string, std::string>> sMhrAnims; // {name, path}
std::map<std::string, MhrNoteEntry> sMhrNotes;              // keyed by anim name
char sMhrFilter[64] = "";
bool sMhrScanned = false;
bool sMhrNotesLoaded = false;
bool sMhrDirty = false;
std::string sMhrSelected;

bool sMhrPreview = false;
int sMhrAnimMode = ANIMMODE_LOOP;
float sMhrPlaySpeed = 1.0f;
bool sMhrForceRestart = false;
int sMhrFrameCount = 0;
std::string sMhrLastApplied;
uint32_t sMhrPlayerHook = 0;

// Pointer-stable wrappers, same pattern as the Animation Viewer.
std::map<std::string, LinkAnimationHeader> sMhrWrappers;

void MhrScanAnims() {
    sMhrAnims.clear();
    auto archiveManager = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager();
    if (archiveManager == nullptr) {
        return;
    }
    auto results = archiveManager->ListFiles("*PlayerAnim_mhr_*");
    if (results == nullptr) {
        return;
    }
    for (const auto& path : *results) {
        size_t pos = path.find_last_of('/');
        std::string name = (pos == std::string::npos) ? path : path.substr(pos + 1);
        sMhrAnims.emplace_back(name, path);
    }
    std::sort(sMhrAnims.begin(), sMhrAnims.end());
    sMhrScanned = true;
}

LinkAnimationHeader* MhrLoadAnim(const std::string& path) {
    auto res = ResourceMgr_GetResourceByNameHandlingMQ(path.c_str());
    if (res == nullptr) {
        return nullptr;
    }
    if (res->GetInitData()->Type == static_cast<uint32_t>(SOH::ResourceType::SOH_PlayerAnimation)) {
        auto playerAnim = std::static_pointer_cast<SOH::PlayerAnimation>(res);
        LinkAnimationHeader& wrapper = sMhrWrappers[path];
        size_t totalS16 = playerAnim->GetPointerSize() / sizeof(int16_t);
        wrapper.common.frameCount = (s16)(totalS16 / kMhrS16PerFrame);
        wrapper.segment = (void*)playerAnim->GetPointer();
        return &wrapper;
    }
    return (LinkAnimationHeader*)ResourceMgr_LoadAnimByName(path.c_str());
}

void MhrLoadNotes() {
    sMhrNotesLoaded = true;
    sMhrNotes.clear();
    std::ifstream f(kMhrNotesPath);
    if (!f.is_open()) {
        return;
    }
    try {
        nlohmann::json root;
        f >> root;
        if (root.contains("anims") && root["anims"].is_object()) {
            for (auto& [name, e] : root["anims"].items()) {
                MhrNoteEntry& entry = sMhrNotes[name];
                entry.path = e.value("path", std::string("misc/link_animetion/") + name);
                snprintf(entry.note, sizeof(entry.note), "%s", e.value("note", "").c_str());
                snprintf(entry.rename, sizeof(entry.rename), "%s", e.value("rename", "").c_str());
                entry.binding = e.value("binding", "");
            }
        }
    } catch (const std::exception&) {
        // Corrupt file: start empty. MhrSaveNotes() merges with the on-disk
        // file and stashes unparseable bytes as .corrupt, so nothing is lost.
    }
}

void MhrSaveNotes() {
    // MERGE with the on-disk file instead of blind overwrite. Keys never
    // touched this session are preserved — protects against losing notes
    // when the in-memory map started incomplete (failed parse, different
    // CWD when launched from VS, or pack_o2r.py migrating the file while
    // the game runs). Keys present in sMhrNotes always win; an entry the
    // user emptied deletes its key.
    nlohmann::json root = nlohmann::json::object();
    std::error_code ec;
    if (std::filesystem::exists(kMhrNotesPath, ec)) {
        try {
            std::ifstream f(kMhrNotesPath);
            f >> root;
            // Safety net: keep the pre-save content around.
            std::ofstream bak(std::string(kMhrNotesPath) + ".prev");
            bak << root.dump(1);
        } catch (const std::exception&) {
            // Unparseable: preserve the raw bytes before we overwrite.
            std::filesystem::copy_file(kMhrNotesPath, std::string(kMhrNotesPath) + ".corrupt",
                                       std::filesystem::copy_options::overwrite_existing, ec);
            root = nlohmann::json::object();
        }
    }
    if (!root.contains("anims") || !root["anims"].is_object()) {
        root["anims"] = nlohmann::json::object();
    }
    nlohmann::json& anims = root["anims"];
    for (const auto& [name, e] : sMhrNotes) {
        if (e.note[0] == '\0' && e.rename[0] == '\0' && e.binding.empty()) {
            anims.erase(name); // cleared this session -> delete its key
            continue;
        }
        anims[name] = { { "path", e.path }, { "note", e.note }, { "rename", e.rename }, { "binding", e.binding } };
    }
    root["version"] = 1;
    std::filesystem::create_directories("nei");
    std::ofstream f(kMhrNotesPath);
    f << root.dump(1);
    sMhrDirty = false;
    MhrMoveset_Reload();
}

// Re-applied every frame while preview is on — identical strategy to the
// Animation Viewer: only restart on a real change, otherwise just keep
// skelAnime pinned so curFrame advances naturally.
void MhrApplyPreview() {
    if (!sMhrPreview || gPlayState == nullptr || sMhrSelected.empty()) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) {
        return;
    }
    auto it = std::find_if(sMhrAnims.begin(), sMhrAnims.end(),
                           [](const auto& p) { return p.first == sMhrSelected; });
    if (it == sMhrAnims.end()) {
        return;
    }
    LinkAnimationHeader* anim = MhrLoadAnim(it->second);
    if (anim == nullptr) {
        return;
    }
    s16 lastFrame = Animation_GetLastFrame(anim);
    sMhrFrameCount = lastFrame;

    bool needRestart = sMhrForceRestart || (sMhrLastApplied != sMhrSelected) ||
                       (player->skelAnime.animation != (void*)anim);
    sMhrLastApplied = sMhrSelected;
    sMhrForceRestart = false;

    if (needRestart) {
        LinkAnimation_Change(gPlayState, &player->skelAnime, anim, sMhrPlaySpeed, 0.0f, (f32)lastFrame,
                             (u8)sMhrAnimMode, 0.0f);
    } else {
        player->skelAnime.playSpeed = sMhrPlaySpeed;
        player->skelAnime.endFrame = (f32)lastFrame;
    }
}

// ---------------------------------------------------------------------------
// MHR moveset binding backend (TEMPORARY — same TU as the tab on purpose).
// Binding keys stored in nei/mhr_anim_notes.json:
//   "mwa:<0..27>"  PLAYER_MWA_* melee slot (jump slash flows through these)
//   "shield:raise" / "shield:loop"
//   "move:roll" / "move:hop_fwd" / "move:hop_right" / "move:backflip" / "move:hop_left"
//   "action:1..4"  reserved future custom-action slots (recorded only)
// CVars: gMods.MhrMoveset.Enabled (default 0), gMods.MhrMoveset.Scope
//        (0 = Gerudo form only, 1 = always).
// ---------------------------------------------------------------------------

// Move ids: 0..3 match the controlStickDirection index of D_80853D4C.
constexpr int kMhrMoveRoll = 4;
constexpr int kMhrMoveMax = 5;

struct MhrBindingOption {
    const char* key;
    const char* label;
};

constexpr MhrBindingOption kMhrBindingOptions[] = {
    { "", "(none)" },
    // --- B: sword/melee (PLAYER_MWA_*) ---
    { "mwa:0", "B: Forward Slash 1H" },
    { "mwa:1", "B: Forward Slash 2H" },
    { "mwa:2", "B: Forward Combo 1H" },
    { "mwa:3", "B: Forward Combo 2H" },
    { "mwa:4", "B: Right Slash 1H" },
    { "mwa:5", "B: Right Slash 2H" },
    { "mwa:6", "B: Right Combo 1H" },
    { "mwa:7", "B: Right Combo 2H" },
    { "mwa:8", "B: Left Slash 1H" },
    { "mwa:9", "B: Left Slash 2H" },
    { "mwa:10", "B: Left Combo 1H" },
    { "mwa:11", "B: Left Combo 2H" },
    { "mwa:12", "B: Stab 1H" },
    { "mwa:13", "B: Stab 2H" },
    { "mwa:14", "B: Stab Combo 1H" },
    { "mwa:15", "B: Stab Combo 2H" },
    { "mwa:16", "B: Flip Slash (air)" },
    { "mwa:17", "B: Jump Slash (air)" },
    { "mwa:18", "B: Flip Slash (landing)" },
    { "mwa:19", "B: Jump Slash (landing)" },
    { "mwa:20", "B: Back Slash Right" },
    { "mwa:21", "B: Back Slash Left" },
    { "mwa:22", "B: Hammer Forward" },
    { "mwa:23", "B: Hammer Side" },
    { "mwa:24", "B: Spin Attack 1H" },
    { "mwa:25", "B: Spin Attack 2H" },
    { "mwa:26", "B: Big Spin 1H" },
    { "mwa:27", "B: Big Spin 2H" },
    // --- R: shield ---
    { "shield:raise", "R: Shield Raise" },
    { "shield:loop", "R: Shield Hold (loop)" },
    // --- A / Z-target movement ---
    { "move:roll", "A: Roll" },
    { "move:hop_fwd", "Z: Hop Forward" },
    { "move:hop_right", "Z: Sidehop Right" },
    { "move:backflip", "Z: Backflip" },
    { "move:hop_left", "Z: Sidehop Left" },
    // --- Future custom action slots (recorded only) ---
    { "action:1", "Action Slot 1 (future)" },
    { "action:2", "Action Slot 2 (future)" },
    { "action:3", "Action Slot 3 (future)" },
    { "action:4", "Action Slot 4 (future)" },
};

LinkAnimationHeader* sMhrMeleeAnims[PLAYER_MWA_MAX] = {};
LinkAnimationHeader* sMhrShieldAnims[2] = {};
LinkAnimationHeader* sMhrMoveAnims[kMhrMoveMax] = {};
bool sMhrBindingsLoaded = false;

void MhrAssignBinding(const std::string& key, const std::string& animPath) {
    LinkAnimationHeader* anim = MhrLoadAnim(animPath);
    if (anim == nullptr) {
        return;
    }
    if (key.rfind("mwa:", 0) == 0) {
        int idx = std::atoi(key.c_str() + 4);
        if (idx >= 0 && idx < PLAYER_MWA_MAX) {
            sMhrMeleeAnims[idx] = anim;
        }
    } else if (key == "shield:raise") {
        sMhrShieldAnims[0] = anim;
    } else if (key == "shield:loop") {
        sMhrShieldAnims[1] = anim;
    } else if (key == "move:hop_fwd") {
        sMhrMoveAnims[0] = anim;
    } else if (key == "move:hop_right") {
        sMhrMoveAnims[1] = anim;
    } else if (key == "move:backflip") {
        sMhrMoveAnims[2] = anim;
    } else if (key == "move:hop_left") {
        sMhrMoveAnims[3] = anim;
    } else if (key == "move:roll") {
        sMhrMoveAnims[kMhrMoveRoll] = anim;
    }
    // "action:*" keys: recorded in the JSON for planning, no runtime yet.
}

bool MhrMovesetActive() {
    if (!CVarGetInteger("gMods.MhrMoveset.Enabled", 0)) {
        return false;
    }
    if (CVarGetInteger("gMods.MhrMoveset.Scope", 0) == 1) {
        return true;
    }
    return GerudoForm_IsActive() != 0;
}

void MhrAnimNotesWidget(WidgetInfo& info) {
    if (sMhrPlayerHook == 0) {
        sMhrPlayerHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>(MhrApplyPreview);
    }
    if (!sMhrScanned) {
        MhrScanAnims();
    }
    if (!sMhrNotesLoaded) {
        MhrLoadNotes();
    }

    // --- Master toggles ---
    bool enabled = CVarGetInteger("gMods.MhrMoveset.Enabled", 0) != 0;
    if (ImGui::Checkbox("Enable MHR Moveset Bindings", &enabled)) {
        CVarSetInteger("gMods.MhrMoveset.Enabled", enabled ? 1 : 0);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    ImGui::SameLine();
    int scope = CVarGetInteger("gMods.MhrMoveset.Scope", 0);
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::Combo("Scope", &scope, "Gerudo form only\0Always\0")) {
        CVarSetInteger("gMods.MhrMoveset.Scope", scope);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload Bindings")) {
        MhrMoveset_Reload();
    }

    // --- Anim list ---
    ImGui::Separator();
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("Filter##MhrFilter", sMhrFilter, sizeof(sMhrFilter));
    ImGui::SameLine();
    if (ImGui::Button("Rescan")) {
        MhrScanAnims();
    }
    ImGui::SameLine();
    ImGui::Text("%zu anims", sMhrAnims.size());
    if (sMhrAnims.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f),
                           "No gPlayerAnim_mhr_* found — is nei/mhr_anims.o2r present?");
    }

    std::string filter(sMhrFilter);
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

    if (ImGui::BeginChild("MhrAnimList", ImVec2(0, 220), true)) {
        for (const auto& [name, path] : sMhrAnims) {
            if (!filter.empty()) {
                std::string lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower.find(filter) == std::string::npos) {
                    continue;
                }
            }
            auto noteIt = sMhrNotes.find(name);
            bool hasNote = noteIt != sMhrNotes.end() && noteIt->second.note[0] != '\0';
            bool hasBind = noteIt != sMhrNotes.end() && !noteIt->second.binding.empty();
            bool hasRename = noteIt != sMhrNotes.end() && noteIt->second.rename[0] != '\0';
            std::string label = name;
            if (hasBind) label += "  [BIND]";
            if (hasRename) label += "  [REN]";
            if (hasNote) label += "  [NOTE]";
            if (ImGui::Selectable(label.c_str(), sMhrSelected == name)) {
                if (sMhrDirty) {
                    MhrSaveNotes(); // autosave when moving between anims
                }
                sMhrSelected = name;
                sMhrForceRestart = true;
            }
        }
    }
    ImGui::EndChild();

    // --- Selected anim: preview + editor ---
    if (!sMhrSelected.empty()) {
        ImGui::Separator();
        ImGui::Text("Selected: %s  (%d frames)", sMhrSelected.c_str(), sMhrFrameCount);

        ImGui::Checkbox("Preview on Link", &sMhrPreview);
        ImGui::SameLine();
        if (ImGui::Button("Restart")) {
            sMhrForceRestart = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Loop", sMhrAnimMode == ANIMMODE_LOOP)) {
            sMhrAnimMode = ANIMMODE_LOOP;
            sMhrForceRestart = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Once", sMhrAnimMode == ANIMMODE_ONCE)) {
            sMhrAnimMode = ANIMMODE_ONCE;
            sMhrForceRestart = true;
        }
        ImGui::SetNextItemWidth(200.0f);
        ImGui::SliderFloat("Speed", &sMhrPlaySpeed, 0.0f, 3.0f, "%.2fx");
        if (gPlayState == nullptr) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "No active gameplay — load a save to preview.");
        }

        MhrNoteEntry& entry = sMhrNotes[sMhrSelected];
        if (entry.path.empty()) {
            auto it = std::find_if(sMhrAnims.begin(), sMhrAnims.end(),
                                   [](const auto& p) { return p.first == sMhrSelected; });
            if (it != sMhrAnims.end()) {
                entry.path = it->second;
            }
        }

        if (ImGui::InputTextMultiline("Note##MhrNote", entry.note, sizeof(entry.note), ImVec2(-1, 60))) {
            sMhrDirty = true;
        }
        UIWidgets::Tooltip("Free text for a description of the animation: what this anim is, what to use it for, etc.");
        if (ImGui::InputText("Rename to##MhrRename", entry.rename, sizeof(entry.rename))) {
            sMhrDirty = true;
        }
        UIWidgets::Tooltip("Proposed final name (e.g. gPlayerAnim_mhr_db_demon_dance). Applied on the next repack.");

        const char* curLabel = "(none)";
        for (const auto& opt : kMhrBindingOptions) {
            if (entry.binding == opt.key) {
                curLabel = opt.label;
                break;
            }
        }
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::BeginCombo("Bind to action##MhrBind", curLabel)) {
            for (const auto& opt : kMhrBindingOptions) {
                bool selected = (entry.binding == opt.key);
                if (ImGui::Selectable(opt.label, selected)) {
                    entry.binding = opt.key;
                    sMhrDirty = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        UIWidgets::Tooltip("Which Link action this anim should replace while the moveset is enabled:\n"
                           "B melee slots (slashes/combos/stabs/jump slash/spin), R shield, A roll, Z hops.\n"
                           "'Action Slot' entries are recorded for the future custom-action system.");
    }

    ImGui::Separator();
    if (ImGui::Button(sMhrDirty ? "Save Notes*" : "Save Notes")) {
        MhrSaveNotes();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("-> %s%s", kMhrNotesPath, sMhrDirty ? "  (unsaved changes)" : "");
}

} // namespace

// --- MHR moveset C API (consumed by z_player.c via local extern decls) ---

extern "C" void MhrMoveset_Reload(void) {
    std::fill(std::begin(sMhrMeleeAnims), std::end(sMhrMeleeAnims), nullptr);
    std::fill(std::begin(sMhrShieldAnims), std::end(sMhrShieldAnims), nullptr);
    std::fill(std::begin(sMhrMoveAnims), std::end(sMhrMoveAnims), nullptr);
    sMhrBindingsLoaded = true;

    // Only hit the disk when the notes were never loaded this session (e.g.
    // the lazy first call from z_player.c before the tab was ever opened).
    // Re-reading unconditionally here was the "my notes got overwritten" bug:
    // the first sword swing reloaded the file and silently discarded any
    // unsaved UI edits, so the next Save wrote the file without them.
    if (!sMhrNotesLoaded) {
        MhrLoadNotes();
    }
    int count = 0;
    for (const auto& [name, e] : sMhrNotes) {
        if (!e.binding.empty()) {
            MhrAssignBinding(e.binding, e.path);
            count++;
        }
    }
    SPDLOG_INFO("[MhrMoveset] loaded {} bindings from {}", count, kMhrNotesPath);
}

extern "C" LinkAnimationHeader* MhrMoveset_GetMeleeAnim(s32 mwa) {
    if (!MhrMovesetActive() || mwa < 0 || mwa >= PLAYER_MWA_MAX) {
        return nullptr;
    }
    if (!sMhrBindingsLoaded) {
        MhrMoveset_Reload();
    }
    return sMhrMeleeAnims[mwa];
}

extern "C" LinkAnimationHeader* MhrMoveset_GetShieldAnim(s32 loopPhase) {
    if (!MhrMovesetActive() || loopPhase < 0 || loopPhase > 1) {
        return nullptr;
    }
    if (!sMhrBindingsLoaded) {
        MhrMoveset_Reload();
    }
    return sMhrShieldAnims[loopPhase];
}

extern "C" LinkAnimationHeader* MhrMoveset_GetMoveAnim(s32 moveId) {
    if (!MhrMovesetActive() || moveId < 0 || moveId >= kMhrMoveMax) {
        return nullptr;
    }
    if (!sMhrBindingsLoaded) {
        MhrMoveset_Reload();
    }
    return sMhrMoveAnims[moveId];
}

// =============================================================================
// "Skijer's NEI" — dedicated top-level menu gathering every NEI feature into
// its own tabs (Masks, Spells, Pak Loader, Custom Items, Randomizer, Controls).
// Built as its own translation unit so the rest of Settings stays clean.
// Widgets are grouped by SIDEBAR (not file order), so each block below just
// (re)sets path.sidebarName to land in the right tab.
// =============================================================================
void SohMenu::AddMenuNEI() {
    WidgetPath path = { "Skijer's NEI", "Masks", SECTION_COLUMN_1 };

    AddMenuEntry("Skijer's NEI", CVAR_SETTING("Menu.SkijerNEISidebarSection"));
    AddSidebarEntry("Skijer's NEI", "Masks", 1);
    AddSidebarEntry("Skijer's NEI", "Spells", 1);
    AddSidebarEntry("Skijer's NEI", "Pak Loader", 3);
    AddSidebarEntry("Skijer's NEI", "Custom Items", 1);
    AddSidebarEntry("Skijer's NEI", "Randomizer", 1);
    AddSidebarEntry("Skijer's NEI", "Controls", 1);
    AddSidebarEntry("Skijer's NEI", "MHR Anims", 1);
    path.sectionName = "Skijer's NEI";

    // ===================== Tab: MHR Anims =====================
    // Anim-viewer-style browser + notes/rename/binding editor for the MHR
    // converted animations. See MhrAnimNotesWidget above.
    path.sidebarName = "MHR Anims";
    path.column = SECTION_COLUMN_1;
    AddWidget(path, "MHR Animation Notes & Bindings", WIDGET_CUSTOM)
        .CustomFunction(MhrAnimNotesWidget)
        .HideInSearch(true);

    // ===================== Tab: Custom Items =====================
    path.sidebarName = "Custom Items";
    path.column = SECTION_COLUMN_1;
    AddWidget(path, "Custom Items", WIDGET_SEPARATOR_TEXT);

    AddWidget(path, "Enable Extra Equipment", WIDGET_CVAR_CHECKBOX)
        .CVar("gCheats.ExtEquip.Enabled")
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Adds 12 new equipment pieces (3 swords, 3 shields, 3 tunics, 3 boots).\n"
            "Press L on the equipment page to toggle between vanilla and extended equipment.\n"
            "(The 'Add Extended Equipment to Rando' toggle in the Randomizer tab controls\n"
            "whether they are shuffled into the seed.)"));

    // Roc's Items MM Animations - requires mm.o2r
    AddWidget(path, "Roc's Items Use MM Animations", WIDGET_CVAR_CHECKBOX)
        .CVar("gEnhancements.RocsItemsUseMmAnims")
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!MmAssets_IsAvailable()) {
                CVarSetInteger("gEnhancements.RocsItemsUseMmAnims", 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Requires mm.o2r from 2Ship2Harkinian Keiichi Alfa 4.0.0.";
            }
        })
        .Options(CheckboxOptions().Tooltip("Use MM animations for Roc's Feather and Roc's Cape jumps.\n"
                                           "Roc's Feather: Backflip on ground jump.\n"
                                           "Roc's Cape: Backflip on ground jump, roll jump on double jump.\n\n"
                                           "REQUIRES: mm.o2r from 2Ship2Harkinian Keiichi Alfa 4.0.0"));

    AddWidget(path, "Invert Roc's Items Animations", WIDGET_CVAR_CHECKBOX)
        .CVar("gMods.RocsItems.InvertAnims")
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!CVarGetInteger("gEnhancements.RocsItemsUseMmAnims", 0)) {
                CVarSetInteger("gMods.RocsItems.InvertAnims", 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Enable 'Roc's Items Use MM Animations' first.";
            }
        })
        .Options(CheckboxOptions().Tooltip("Swaps the animation order for Roc's items.\n"
                                           "OFF: Ground = Backflip, Double = Roll Jump\n"
                                           "ON:  Ground = Roll Jump, Double = Backflip\n\n"
                                           "REQUIRES: Roc's Items Use MM Animations"));

    // NEI Aim Cycle — extends the vanilla BowArrowCycle cheat with L button
    // (previous direction), SW97 elemental arrow cycling, slingshot support,
    // and in-game Gust Jar element switching while in first-person aim.
    AddWidget(path, "NEI Aim Cycle (R/L while aiming)", WIDGET_CVAR_CHECKBOX)
        .CVar("gEnhancements.NeiAimCycle")
        .RaceDisable(false)
        .Options(CheckboxOptions().Tooltip(
            "Cycle between elements while aiming. R = next, L = previous.\n"
            "Applies to:\n"
            "  - Bow & Slingshot: cycles vanilla arrows OR SW97 elemental arrows\n"
            "    (whichever type is currently equipped on the C-button).\n"
            "  - Gust Jar (in first-person, IDLE): cycles unlocked elements\n"
            "    based on owned medallions. Hold C + press L+R together to\n"
            "    cycle the element AND switch to a stored-blow that fires on\n"
            "    C release (no auto-discharge).\n"
            "  - Arrow wheel: now also includes Bombchus when you own any.\n\n"
            "Extends the vanilla 'Bow Arrow Cycle' cheat — both can be enabled\n"
            "at the same time without conflict."));

    // Twilight Upgrade — per-bit save flags. Each checkbox sets/clears one bit
    // of gSaveContext.ship.twilightUpgrade so the player can mix-and-match
    // which sub-upgrades are unlocked on this save (eventually shuffled by
    // rando). State is read live from the save each frame via PreFunc → the
    // shadow bool, and writes go through TwilightUpgrade_Set* on Callback.
    AddWidget(path, "Twilight Upgrade Bits", WIDGET_SEPARATOR_TEXT);

    static bool sTwilightClawshotShadow = false;
    AddWidget(path, "Clawshot", WIDGET_CHECKBOX)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            sTwilightClawshotShadow = TwilightUpgrade_HasClawshot() != 0;
            info.valuePointer = &sTwilightClawshotShadow;
        })
        .Callback([](WidgetInfo& info) {
            TwilightUpgrade_SetClawshot(sTwilightClawshotShadow ? 1 : 0);
        })
        .Options(CheckboxOptions().Tooltip(
            "Unlocks the Clawshot mode on hookshot/longshot. Tap L during\n"
            "gameplay (with hookshot or longshot equipped) to toggle the\n"
            "reverse-pull behaviour (enemy → Link, pin to grappling point)."));

    static bool sTwilightBombArrowsShadow = false;
    AddWidget(path, "Bomb Arrows", WIDGET_CHECKBOX)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            sTwilightBombArrowsShadow = TwilightUpgrade_HasBombArrows() != 0;
            info.valuePointer = &sTwilightBombArrowsShadow;
        })
        .Callback([](WidgetInfo& info) {
            TwilightUpgrade_SetBombArrows(sTwilightBombArrowsShadow ? 1 : 0);
        })
        .Options(CheckboxOptions().Tooltip(
            "Auto-grants ITEM_BOMB_ARROWS into the inventory once a bomb bag\n"
            "is owned, and adds bomb arrows as a position in the SW97 R/L\n"
            "arrow cycle during bow aim."));

    static bool sTwilightGaleBoomerangShadow = false;
    AddWidget(path, "Gale Boomerang", WIDGET_CHECKBOX)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            sTwilightGaleBoomerangShadow = TwilightUpgrade_HasGaleBoomerang() != 0;
            info.valuePointer = &sTwilightGaleBoomerangShadow;
        })
        .Callback([](WidgetInfo& info) {
            TwilightUpgrade_SetGaleBoomerang(sTwilightGaleBoomerangShadow ? 1 : 0);
        })
        .Options(CheckboxOptions().Tooltip(
            "Unlocks the Gale Boomerang multi-target. During boomerang aim,\n"
            "L tap adds the current Z-target to the route (up to 4 targets,\n"
            "500 units max between consecutive targets)."));

    // Convenience: still expose the grant-all button as a one-click that
    // flips all three checkboxes on at once.
    AddWidget(path, "Grant All Twilight Bits", WIDGET_BUTTON)
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) {
            TwilightUpgrade_Grant();
        })
        .Options(ButtonOptions().Size(Sizes::Inline).Tooltip(
            "Sets all three Twilight Upgrade bits at once. Equivalent to\n"
            "checking the three checkboxes above. Idempotent."));

    // ===================== Tab: Spells =====================
    path.sidebarName = "Spells";
    path.column = SECTION_COLUMN_1;
    AddWidget(path, "Spells & Spiritual Stones", WIDGET_SEPARATOR_TEXT);

    // SW97 Medallion Spells — merged with the old "Enable Sage Spells" rando
    // toggle. This one checkbox now drives BOTH the C-button medallion casting
    // (gEnhancements.SkijerNEI.SW97Medallions) and the seed-locked elemental-
    // damage rando setting (RSK_SW97_SPELLS via CVAR_RANDOMIZER_SETTING).
    AddWidget(path, "SW97 Medallion Spells", WIDGET_CVAR_CHECKBOX)
        .CVar("gEnhancements.SkijerNEI.SW97Medallions")
        .RaceDisable(false)
        .PostFunc([](WidgetInfo& info) {
            CVarSetInteger(CVAR_RANDOMIZER_SETTING("SW97Spells"),
                           CVarGetInteger("gEnhancements.SkijerNEI.SW97Medallions", 0));
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        })
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Equip quest medallions to C-buttons from Quest Status.\n"
            "C = cast elemental spell, L+C = set elemental arrow/slingshot.\n"
            "Adult: elemental arrows. Child: elemental slingshot seeds.\n"
            "Also enables the seed-locked elemental-damage setting (Spell + Projectile\n"
            "paths), synced with 'Sage Spells' in the Randomizer menu.\n\n"
            "Credit: z64proto/sw97 team (spell/arrow actors)"));

    AddWidget(path, "Enable Spiritual Stones", WIDGET_CVAR_CHECKBOX)
        .CVar("gMods.SpiritualStones.Enabled")
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Master toggle for the custom Spiritual Stones behavior.\n"
            "ON: each owned stone toggles (A on the Quest page) a passive buff\n"
            "(Kokiri = faster walk, Goron = climb, Zora = swim), and held on a C/D-pad slot\n"
            "sets/warps to a saved waypoint statue.\n"
            "OFF: the stones behave like vanilla (no custom behavior)."));

    // ===================== Tab: Masks =====================
    path.sidebarName = "Masks";
    path.column = SECTION_COLUMN_1;
    AddWidget(path, "Mask Transformations", WIDGET_SEPARATOR_TEXT);

    AddWidget(path, "Kafei Mask Transform", WIDGET_CVAR_CHECKBOX)
        .CVar("gMods.KafeiMaskTransform")
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!std::filesystem::exists("nei/N64_Kafei.pak")) {
                CVarSetInteger("gMods.KafeiMaskTransform", 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Requires N64_Kafei.pak in nei/ folder.";
            }
        })
        .Options(CheckboxOptions().Tooltip("Wearing the Kafei Mask transforms Link into Kafei.\n"
                                           "Adult Link becomes Adult Kafei, Child Link becomes Child Kafei.\n"
                                           "Remove the mask to revert.\n\n"
                                           "REQUIRES: nei/N64_Kafei.pak"));

    AddWidget(path, "Gerudo Mask Transform", WIDGET_CVAR_CHECKBOX)
        .CVar("gMods.GerudoMaskTransform")
        .RaceDisable(false)
        .Options(CheckboxOptions().Tooltip(
            "Wearing the vanilla Gerudo Mask transforms Link into a Gerudo — uses a\n"
            "Link-rigged gerudo mesh, so all of Link's animations and equipment work\n"
            "exactly as normal. The body just looks gerudo.\n"
            "Effects while the mask is worn:\n"
            "  - Haunted Wasteland sandstorm is suppressed (no more getting lost).\n"
            "  - All Gerudos treat you as a fellow Gerudo (Ge1/Ge2/Ge3 are friendly,\n"
            "    GTG guard lets you in). Access is TEMPORARY — no Gerudo Card is granted.\n"
            "Remove the mask to revert.\n\n"
            "REQUIRES: nei/gerudo.o2r — built by tools/repack_gerudo_player.py from\n"
            "an artist-authored \"00 - Gerudo Player.o2r\" (Link-21-bone-rigged gerudo skin).\n"
            "Also ships 11 baked gerudo anims (visible in the anim viewer)."));

    // Garo Mask: gated by gMods.GaroMaskTransform (default ON). When OFF, the
    // Garo Mask stays a cosmetic mask (no transformation), matching the Gerudo
    // opt-out. Enforced in mm_player_form.cpp (MmForm_GetMaskType / HandleMaskUse).
    AddWidget(path, "Garo Mask Transform", WIDGET_CVAR_CHECKBOX)
        .CVar("gMods.GaroMaskTransform")
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Wearing the Garo Mask transforms Link into a Garo (custom garo.o2r skin\n"
            "with a slash-combo + shuriken finisher). Link's normal gameplay still runs\n"
            "1:1 — only the look and the slash combo change.\n"
            "OFF: the Garo Mask draws as a plain cosmetic mask (no transformation).\n\n"
            "REQUIRES: garo.o2r in the nei/ folder."));

    AddWidget(path, "MM Masks", WIDGET_SEPARATOR_TEXT);

    // Merged option: "Include MM Masks Inventory" + "Extra Mask Effects" are now
    // a single toggle. Enabling the MM masks page also enables the per-mask
    // visual effects and the transformation system. (ExtraEffects had no runtime
    // reader other than this menu, so the merge loses nothing.)
    AddWidget(path, "Include MM Masks (Inventory + Effects)", WIDGET_CVAR_CHECKBOX)
        .CVar("gMods.MmMasks.InventoryEnabled")
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!MmAssets_IsAvailable()) {
                CVarSetInteger("gMods.MmMasks.InventoryEnabled", 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Requires mm.o2r from 2Ship2Harkinian Keiichi Alfa 4.0.0.";
            }
        })
        .PostFunc([](WidgetInfo& info) {
            s32 on = CVarGetInteger("gMods.MmMasks.InventoryEnabled", 0);
            // One toggle drives the whole MM-mask feature set.
            CVarSetInteger("gMods.TransformMasks.ExtraEffects", on);
            if (on) {
                CVarSetInteger("gMods.TransformMasks.Enabled", 1);
            }
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        })
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Adds a 3rd inventory page with all 24 MM masks AND enables each mask's\n"
            "custom visual effects. Transformation masks (Deku, Goron, Zora, Fierce\n"
            "Deity) trigger transformations. Removes OOT Goron/Zora masks from the\n"
            "randomizer pool.\n\n"
            "REQUIRES: mm.o2r from 2Ship2Harkinian Keiichi Alfa 4.0.0"));

    AddWidget(path, "Enable Transformation Masks", WIDGET_CVAR_CHECKBOX)
        .CVar("gMods.TransformMasks.Enabled")
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!MmAssets_IsAvailable()) {
                CVarSetInteger("gMods.TransformMasks.Enabled", 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Requires mm.o2r from 2Ship2Harkinian Keiichi Alfa 4.0.0.\n"
                                                "Download 2Ship, extract your MM ROM, then copy mm.o2r here.";
            }
        })
        .Options(CheckboxOptions().Tooltip("Allows you to transform with certain masks like in Majora's Mask.\n"
                                           "Equip transformation masks from the MM Masks inventory page.\n\n"
                                           "REQUIRES: mm.o2r from 2Ship2Harkinian Keiichi Alfa 4.0.0"));

    AddWidget(path, "Instant Transform", WIDGET_CVAR_CHECKBOX)
        .CVar("gMods.TransformMasks.InstantTransform")
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!MmAssets_IsAvailable()) {
                CVarSetInteger("gMods.TransformMasks.InstantTransform", 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Requires mm.o2r from 2Ship2Harkinian Keiichi Alfa 4.0.0.";
            } else if (!CVarGetInteger("gMods.MmMasks.InventoryEnabled", 1)) {
                CVarSetInteger("gMods.TransformMasks.InstantTransform", 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Enable 'Include MM Masks' first.";
            }
        })
        .Options(CheckboxOptions().Tooltip("Skip the transformation cutscene animation.\n"
                                           "Transform instantly when equipping a transformation mask.\n\n"
                                           "REQUIRES: Include MM Masks + mm.o2r"));

    AddWidget(path, "Instant Blast Mask", WIDGET_CVAR_CHECKBOX)
        .CVar("gMods.BlastMask.Instant")
        .RaceDisable(false)
        .Options(CheckboxOptions().Tooltip("Removes the cooldown on Blast Mask.\n"
                                           "Normally there is a 310-frame (~5 second) cooldown between uses."));

    AddWidget(path, "Invisible Non-Transformation Masks", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("HideNonTransformationMasks"))
        .RaceDisable(false)
        .Options(CheckboxOptions().Tooltip(
            "Turns all MM non-transformation masks invisible while still maintaining their effects.\n"
            "Transformation masks (Deku, Goron, Zora, Fierce Deity) remain visible.\n"
            "Only affects MM masks; vanilla OOT child masks are unaffected (use Invisible Bunny Hood for OOT bunny hood)."));

    // Mute MM Audio (moved here from the Spells tab — it's a mask/MM-assets option).
    AddWidget(path, "Mute MM Audio", WIDGET_CVAR_CHECKBOX)
        .CVar("gEnhancements.SkijerNEI.MuteMmAudio")
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip("Mute all sounds from MM (mm.o2r).\n"
                                                              "Transformation mask SFX, voices, and instruments\n"
                                                              "will be silenced. OOT sounds play instead."));

    // ===================== Tab: Pak Loader =====================
    path.sidebarName = "Pak Loader";
    path.column = SECTION_COLUMN_1;
    AddWidget(path, "Custom Models (.pak)", WIDGET_SEPARATOR_TEXT);

    // Build model combobox maps per age (triggers lazy init of PakLoader)
    {
        s32 pakCount = PakLoader_GetModelCount();

        // Adult models
        std::map<int32_t, const char*> adultModelMap;
        adultModelMap[-1] = "Default Link";
        for (s32 i = 0; i < pakCount; i++) {
            if (PakLoader_ModelHasAdult(i)) {
                adultModelMap[i] = PakLoader_GetModelLabel(i);
            }
        }

        // Child models
        std::map<int32_t, const char*> childModelMap;
        childModelMap[-1] = "Default Link";
        for (s32 i = 0; i < pakCount; i++) {
            if (PakLoader_ModelHasChild(i)) {
                childModelMap[i] = PakLoader_GetModelLabel(i);
            }
        }

        AddWidget(path, "Enable Custom Player Model", WIDGET_CVAR_CHECKBOX)
            .CVar("gMods.PakLoader.Enabled")
            .RaceDisable(false)
            .PreFunc([](WidgetInfo& info) {
                if (PakLoader_GetModelCount() == 0) {
                    CVarSetInteger("gMods.PakLoader.Enabled", 0);
                    info.options->disabled = true;
                    info.options->disabledTooltip = "No .pak files found.\n"
                                                    "Place ModLoader64 .pak model files in the mods/ folder.";
                }
            })
            .PostFunc([](WidgetInfo& info) {
                if (CVarGetInteger("gMods.PakLoader.Enabled", 0)) {
                    s32 adultIdx = CVarGetInteger("gMods.PakLoader.AdultModel", -1);
                    s32 childIdx = CVarGetInteger("gMods.PakLoader.ChildModel", -1);
                    PakLoader_SelectAdultModel(adultIdx);
                    PakLoader_SelectChildModel(childIdx);
                } else {
                    PakLoader_SelectAdultModel(-1);
                    PakLoader_SelectChildModel(-1);
                }
            })
            .Options(CheckboxOptions().Tooltip("Replaces Link's model with a custom model from a .pak file.\n"
                                               "Place ModLoader64 zzplayas .pak files in the mods/ folder.\n"
                                               "You can choose different models for Adult and Child Link."));

        AddWidget(path, "Adult Link Model", WIDGET_CVAR_COMBOBOX)
            .CVar("gMods.PakLoader.AdultModel")
            .RaceDisable(false)
            .PreFunc([](WidgetInfo& info) {
                if (PakLoader_GetModelCount() == 0 || !CVarGetInteger("gMods.PakLoader.Enabled", 0)) {
                    info.options->disabled = true;
                }
                // Rebuild the comboMap from current sModels every frame so the
                // dropdown stays in sync if paks load after AddMenuNEI ran
                // (lazy init), or if Force* added entries at runtime. Also
                // clamp the CVar to -1 if it points outside the rebuilt map —
                // otherwise Combobox<int>::at() throws std::out_of_range and
                // crashes the renderer.
                auto opt = std::static_pointer_cast<UIWidgets::ComboboxOptions>(info.options);
                opt->comboMap.clear();
                opt->comboMap[-1] = "Default Link";
                s32 n = PakLoader_GetModelCount();
                for (s32 i = 0; i < n; i++) {
                    if (PakLoader_ModelHasAdult(i)) {
                        opt->comboMap[i] = PakLoader_GetModelLabel(i);
                    }
                }
                s32 v = CVarGetInteger("gMods.PakLoader.AdultModel", -1);
                if (v >= 0 && !opt->comboMap.count(v)) {
                    CVarSetInteger("gMods.PakLoader.AdultModel", -1);
                }
            })
            .PostFunc([](WidgetInfo& info) {
                if (CVarGetInteger("gMods.PakLoader.Enabled", 0)) {
                    PakLoader_SelectAdultModel(CVarGetInteger("gMods.PakLoader.AdultModel", -1));
                }
            })
            .Options(ComboboxOptions()
                         .ComboMap(adultModelMap)
                         .DefaultIndex(-1)
                         .Tooltip("Choose a custom model for Adult Link."));

        AddWidget(path, "Child Link Model", WIDGET_CVAR_COMBOBOX)
            .CVar("gMods.PakLoader.ChildModel")
            .RaceDisable(false)
            .PreFunc([](WidgetInfo& info) {
                if (PakLoader_GetModelCount() == 0 || !CVarGetInteger("gMods.PakLoader.Enabled", 0)) {
                    info.options->disabled = true;
                }
                auto opt = std::static_pointer_cast<UIWidgets::ComboboxOptions>(info.options);
                opt->comboMap.clear();
                opt->comboMap[-1] = "Default Link";
                s32 n = PakLoader_GetModelCount();
                for (s32 i = 0; i < n; i++) {
                    if (PakLoader_ModelHasChild(i)) {
                        opt->comboMap[i] = PakLoader_GetModelLabel(i);
                    }
                }
                s32 v = CVarGetInteger("gMods.PakLoader.ChildModel", -1);
                if (v >= 0 && !opt->comboMap.count(v)) {
                    CVarSetInteger("gMods.PakLoader.ChildModel", -1);
                }
            })
            .PostFunc([](WidgetInfo& info) {
                if (CVarGetInteger("gMods.PakLoader.Enabled", 0)) {
                    PakLoader_SelectChildModel(CVarGetInteger("gMods.PakLoader.ChildModel", -1));
                }
            })
            .Options(ComboboxOptions()
                         .ComboMap(childModelMap)
                         .DefaultIndex(-1)
                         .Tooltip("Choose a custom model for Child Link."));

        // Equipment Pack lists ANY pak that has at least one equipment DL —
        // dedicated zzequipment paks AND Combined paks (body + equipment) both
        // qualify. Selecting a Combined pak here uses its equipment alias
        // table; the body model side is controlled separately by the Adult /
        // Child dropdowns above.
        std::map<int32_t, const char*> equipModelMap;
        equipModelMap[-1] = "Default Equipment";
        for (s32 i = 0; i < pakCount; i++) {
            if (PakLoader_ModelHasAnyEquipment(i)) {
                equipModelMap[i] = PakLoader_GetModelLabel(i);
            }
        }

        if (equipModelMap.size() > 1) { // More than just "Default"
            AddWidget(path, "Equipment Pack", WIDGET_CVAR_COMBOBOX)
                .CVar("gMods.PakLoader.Equipment")
                .RaceDisable(false)
                .PreFunc([](WidgetInfo& info) {
                    auto opt = std::static_pointer_cast<UIWidgets::ComboboxOptions>(info.options);
                    opt->comboMap.clear();
                    opt->comboMap[-1] = "Default Equipment";
                    s32 n = PakLoader_GetModelCount();
                    for (s32 i = 0; i < n; i++) {
                        if (PakLoader_ModelHasAnyEquipment(i)) {
                            opt->comboMap[i] = PakLoader_GetModelLabel(i);
                        }
                    }
                    s32 v = CVarGetInteger("gMods.PakLoader.Equipment", -1);
                    if (v >= 0 && !opt->comboMap.count(v)) {
                        CVarSetInteger("gMods.PakLoader.Equipment", -1);
                    }
                })
                .PostFunc([](WidgetInfo& info) {
                    // Equipment works independently of the body-model toggle —
                    // pak_loader resolves vanilla fists/hands at draw time so an
                    // equipment-only selection (e.g. just a custom sword) is
                    // valid even when Enable Custom Player Model is off.
                    PakLoader_SelectEquipment(CVarGetInteger("gMods.PakLoader.Equipment", -1));
                })
                .Options(ComboboxOptions()
                             .ComboMap(equipModelMap)
                             .DefaultIndex(-1)
                             .Tooltip("Choose a custom equipment pack.\n"
                                      "Replaces swords, shields, and other items.\n"
                                      "Works on its own — you do NOT have to enable Custom Player Model."));
        }

        // ----- Voice Packs (Z64Online .pak with sounds/<HEX>/*.ogg) -----
        AddWidget(path, "Custom Link Voice", WIDGET_SEPARATOR_TEXT);

        std::map<int32_t, const char*> voicePackMap;
        voicePackMap[-1] = "None";
        for (s32 i = 0; i < VoicePack_GetCount(); i++) {
            voicePackMap[i] = VoicePack_GetName(i);
        }

        AddWidget(path, "Enable Custom Voice", WIDGET_CVAR_CHECKBOX)
            .CVar("gMods.VoicePack.Enabled")
            .RaceDisable(false)
            .PreFunc([](WidgetInfo& info) {
                if (VoicePack_GetCount() == 0) {
                    CVarSetInteger("gMods.VoicePack.Enabled", 0);
                    info.options->disabled = true;
                    info.options->disabledTooltip =
                        "No voice packs found.\nPlace Z64Online voice .pak files in the mods/ folder.";
                }
            })
            .PostFunc([](WidgetInfo& info) {
                if (CVarGetInteger("gMods.VoicePack.Enabled", 0)) {
                    VoicePack_Select(CVarGetInteger("gMods.VoicePack.Selection", -1));
                } else {
                    VoicePack_Select(-1);
                }
            })
            .Options(CheckboxOptions().Tooltip(
                "Replaces Link's voice grunts (sword swings, falls, damage, etc.)\n"
                "with samples from a Z64Online-format voice pak.\n"
                "Voice samples play as 2D audio (no positional attenuation)."));

        if (voicePackMap.size() > 1) {
            AddWidget(path, "Voice Pack", WIDGET_CVAR_COMBOBOX)
                .CVar("gMods.VoicePack.Selection")
                .RaceDisable(false)
                .PreFunc([](WidgetInfo& info) {
                    if (!CVarGetInteger("gMods.VoicePack.Enabled", 0)) {
                        info.options->disabled = true;
                    }
                    auto opt = std::static_pointer_cast<UIWidgets::ComboboxOptions>(info.options);
                    opt->comboMap.clear();
                    opt->comboMap[-1] = "None";
                    s32 n = VoicePack_GetCount();
                    for (s32 i = 0; i < n; i++) {
                        const char* nm = VoicePack_GetName(i);
                        opt->comboMap[i] = nm ? nm : "(unnamed)";
                    }
                    s32 v = CVarGetInteger("gMods.VoicePack.Selection", -1);
                    if (v >= 0 && !opt->comboMap.count(v)) {
                        CVarSetInteger("gMods.VoicePack.Selection", -1);
                    }
                })
                .PostFunc([](WidgetInfo& info) {
                    if (CVarGetInteger("gMods.VoicePack.Enabled", 0)) {
                        VoicePack_Select(CVarGetInteger("gMods.VoicePack.Selection", -1));
                    }
                })
                .Options(ComboboxOptions()
                             .ComboMap(voicePackMap)
                             .DefaultIndex(-1)
                             .Tooltip("Choose a voice pack.\n"
                                      "Selecting a pack decodes its OGG samples (lazy, ~one-time cost)."));

            AddWidget(path, "Voice Pack Volume", WIDGET_CVAR_SLIDER_FLOAT)
                .CVar("gMods.VoicePack.Volume")
                .RaceDisable(false)
                .PreFunc([](WidgetInfo& info) {
                    if (!CVarGetInteger("gMods.VoicePack.Enabled", 0)) {
                        info.options->disabled = true;
                    }
                })
                .Options(FloatSliderOptions()
                             .Tooltip("Mix gain for voice pack samples.")
                             .Min(0.0f)
                             .Max(2.0f)
                             .DefaultValue(1.0f)
                             .IsPercentage());
        }
    }

    // ===================== Tab: Randomizer =====================
    path.sidebarName = "Randomizer";
    path.column = SECTION_COLUMN_1;

    AddWidget(path, "Randomizer (seed-locked)", WIDGET_SEPARATOR_TEXT);

    AddWidget(path, "Enable Custom Items", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_RANDOMIZER_SETTING("SkijerCustomItems"))
        .PostFunc([](WidgetInfo& info) {
            // Mirror to the runtime CVar that gates page-2 visibility in the pause menu.
            CVarSetInteger("gMods.CustomItems.Enabled",
                           CVarGetInteger(CVAR_RANDOMIZER_SETTING("SkijerCustomItems"), 0));
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        })
        .Options(CheckboxOptions().Tooltip("Enables the 24 custom items on the second inventory page (seed-locked rando setting).\n"
                                           "When enabled, these items are also added to the randomizer pool and gated logic paths.\n"
                                           "When disabled, page 2 is inaccessible and items are not in rando.\n"
                                           "Synced with the same setting in the Randomizer menu."));

    AddWidget(path, "Add All MM Masks to Rando", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_RANDOMIZER_SETTING("MmMasksAll"))
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!MmAssets_IsAvailable()) {
                CVarSetInteger(CVAR_RANDOMIZER_SETTING("MmMasksAll"), 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Requires mm.o2r from 2Ship2Harkinian Keiichi Alfa 4.0.0.";
            } else if (!CVarGetInteger("gMods.MmMasks.InventoryEnabled", 0)) {
                CVarSetInteger(CVAR_RANDOMIZER_SETTING("MmMasksAll"), 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Enable 'Include MM Masks' first.";
            }
        })
        .PostFunc([](WidgetInfo& info) {
            if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("MmMasksAll"), 0)) {
                CVarSetInteger(CVAR_RANDOMIZER_SETTING("MmMasksTransform"), 0);
                Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            }
        })
        .Options(CheckboxOptions().Tooltip("Adds all 24 MM masks to the randomizer item pool.\n"
                                           "Masks can be found at random locations like custom items.\n"
                                           "Removes OOT Goron/Zora masks from pool.\n\n"
                                           "Seed-locked rando setting.\n"
                                           "REQUIRES: 'Include MM Masks' enabled"));

    AddWidget(path, "Add Transformation Masks to Rando", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_RANDOMIZER_SETTING("MmMasksTransform"))
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!MmAssets_IsAvailable()) {
                CVarSetInteger(CVAR_RANDOMIZER_SETTING("MmMasksTransform"), 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Requires mm.o2r from 2Ship2Harkinian Keiichi Alfa 4.0.0.";
            } else if (!CVarGetInteger("gMods.MmMasks.InventoryEnabled", 0)) {
                CVarSetInteger(CVAR_RANDOMIZER_SETTING("MmMasksTransform"), 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Enable 'Include MM Masks' first.";
            } else if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("MmMasksAll"), 0)) {
                CVarSetInteger(CVAR_RANDOMIZER_SETTING("MmMasksTransform"), 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "'Add All MM Masks to Rando' already includes transformation masks.";
            }
        })
        .Options(CheckboxOptions().Tooltip("Adds only the 4 transformation masks (Deku, Goron, Zora, Fierce Deity)\n"
                                           "to the randomizer item pool.\n"
                                           "Removes OOT Goron/Zora masks from pool.\n\n"
                                           "Seed-locked rando setting.\n"
                                           "REQUIRES: 'Include MM Masks' enabled"));

    AddWidget(path, "Add Extended Equipment to Rando", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_RANDOMIZER_SETTING("ExtEquipment"))
        .RaceDisable(false)
        .PostFunc([](WidgetInfo& info) {
            CVarSetInteger("gCheats.ExtEquip.Enabled",
                           CVarGetInteger(CVAR_RANDOMIZER_SETTING("ExtEquipment"), 0));
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        })
        .Options(CheckboxOptions().Tooltip(
            "Adds the 12 extended equipment pieces (3 swords, 3 shields, 3 tunics, 3 boots) to the randomizer pool.\n"
            "Press L on the equipment page to toggle between vanilla and extended equipment.\n\n"
            "Seed-locked rando setting (also enables the in-game equipment system)."));

    AddWidget(path, "Bomb Arrows: Auto-grant with Bomb Bag", WIDGET_CVAR_CHECKBOX)
        .CVar("gMods.BombArrows.AutoGrantOnBag")
        .RaceDisable(false)
        .Options(CheckboxOptions().Tooltip(
            "Cheat: automatically gives ITEM_BOMB_ARROWS the moment you obtain any bomb bag.\n"
            "When on, the new bow/slingshot arrow wheel will show a Bomb entry as soon as the\n"
            "bag is yours. Has no effect on the randomizer item pool."));

    // ----- Equipment Mix (same Pak Loader tab; uses columns 1-3) -----
    // Per-slot equipment override:
    //   1. Swords + Shields (6 dropdowns)
    //   2. Ranged + tools + ocarinas + boots + gauntlets + bracelet (13)
    //   3. Child masks (8)
    path.sidebarName = "Pak Loader";
    path.column = SECTION_COLUMN_1;

    AddWidget(path, "Per-slot equipment override", WIDGET_SEPARATOR_TEXT);
    AddWidget(path,
              "Each slot can pull from a different pak. 'Default' inherits from the main "
              "Equipment Pack dropdown (or vanilla if no pack selected). Sheathed and "
              "unsheathed pieces always come from the same source pak so the look stays "
              "consistent.",
              WIDGET_TEXT);

    AddWidget(path, "Reset all slots to Default", WIDGET_BUTTON)
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) {
            s32 n = PakLoader_GetSlotCount();
            char cvarName[96];
            for (s32 i = 0; i < n; i++) {
                snprintf(cvarName, sizeof(cvarName),
                         "gMods.PakLoader.SlotMix.%s", PakLoader_GetSlotKey(i));
                CVarSetInteger(cvarName, -1);
                PakLoader_SetSlotMix(i, -1);
            }
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        })
        .Options(ButtonOptions().Size(Sizes::Inline)
                                .Tooltip("Clear every per-slot override at once."));

    // Cached CVar-name strings per slot so the widgets get stable c_str() pointers.
    // AddMenuNEI is called once at boot, so static storage is fine.
    static std::vector<std::string> slotCVarNames;
    {
        s32 n = PakLoader_GetSlotCount();
        slotCVarNames.clear();
        slotCVarNames.reserve(n);
        for (s32 i = 0; i < n; i++) {
            slotCVarNames.emplace_back(std::string("gMods.PakLoader.SlotMix.") +
                                       PakLoader_GetSlotKey(i));
        }
    }

    // Slot-grouping for placement across the three columns. Indices must match
    // sSlotGroups[] order in pak_loader.cpp.
    //  0: Sword0 (Kokiri)        1: Sword1 (Master)      2: Sword2 (Biggoron)
    //  3: Shield0 (Deku)         4: Shield1 (Hylian)     5: Shield2 (Mirror)
    //  6: Bow                    7: Hookshot             8: Slingshot
    //  9: Boomerang             10: Hammer              11: DekuStick
    // 12: Bottle                13: OcarinaFairy        14: OcarinaTime
    // 15: IronBoots             16: HoverBoots          17: Gauntlets
    // 18: Bracelet              19: MaskSkull           20: MaskSpooky
    // 21: MaskKeaton            22: MaskTruth           23: MaskGoron
    // 24: MaskZora              25: MaskGerudo          26: MaskBunny

    auto addSlotWidget = [this, &path](s32 slotIdx) {
        const char* label = PakLoader_GetSlotLabel(slotIdx);
        const char* cvarName = slotCVarNames[slotIdx].c_str();

        AddWidget(path, label, WIDGET_CVAR_COMBOBOX)
            .CVar(cvarName)
            .RaceDisable(false)
            .PreFunc([slotIdx](WidgetInfo& info) {
                // Rebuild every frame so newly-loaded paks appear immediately
                // and removed paks disappear without a restart. Clamp the CVar
                // to -1 if it ends up pointing at a slot the chosen pak no
                // longer satisfies — defends against std::map::at out_of_range
                // crashes in Combobox<int>.
                auto opt = std::static_pointer_cast<UIWidgets::ComboboxOptions>(info.options);
                opt->comboMap.clear();
                opt->comboMap[-1] = "Default (inherit)";
                s32 n = PakLoader_GetModelCount();
                for (s32 i = 0; i < n; i++) {
                    if (PakLoader_PakProvidesSlot(i, slotIdx)) {
                        opt->comboMap[i] = PakLoader_GetModelLabel(i);
                    }
                }
                char cvarName[96];
                snprintf(cvarName, sizeof(cvarName),
                         "gMods.PakLoader.SlotMix.%s", PakLoader_GetSlotKey(slotIdx));
                s32 v = CVarGetInteger(cvarName, -1);
                if (v >= 0 && !opt->comboMap.count(v)) {
                    CVarSetInteger(cvarName, -1);
                }
            })
            .PostFunc([slotIdx](WidgetInfo& info) {
                char cvarName[96];
                snprintf(cvarName, sizeof(cvarName),
                         "gMods.PakLoader.SlotMix.%s", PakLoader_GetSlotKey(slotIdx));
                PakLoader_SetSlotMix(slotIdx, CVarGetInteger(cvarName, -1));
            })
            .Options(ComboboxOptions()
                         .DefaultIndex(-1)
                         .Tooltip("Pak that provides this piece. 'Default' = inherit "
                                  "from the Equipment Pack dropdown."));
    };

    // Column 1 — Swords (0..2) + Shields (3..5)
    AddWidget(path, "Swords", WIDGET_SEPARATOR_TEXT);
    for (s32 i = 0; i <= 2; i++) addSlotWidget(i);
    AddWidget(path, "Shields", WIDGET_SEPARATOR_TEXT);
    for (s32 i = 3; i <= 5; i++) addSlotWidget(i);

    // Column 2 — Ranged + Tools + Boots + Gauntlets + Bracelet (6..18)
    path.column = SECTION_COLUMN_2;
    AddWidget(path, "Ranged & Tools", WIDGET_SEPARATOR_TEXT);
    for (s32 i = 6; i <= 11; i++) addSlotWidget(i);   // Bow..DekuStick
    AddWidget(path, "Items", WIDGET_SEPARATOR_TEXT);
    addSlotWidget(12); // Bottle
    addSlotWidget(13); // OcarinaFairy
    addSlotWidget(14); // OcarinaTime
    AddWidget(path, "Boots & Gauntlets", WIDGET_SEPARATOR_TEXT);
    addSlotWidget(15); // IronBoots
    addSlotWidget(16); // HoverBoots
    addSlotWidget(17); // Gauntlets
    addSlotWidget(18); // Bracelet

    // Column 3 — Child masks (19..26)
    path.column = SECTION_COLUMN_3;
    AddWidget(path, "Child Masks", WIDGET_SEPARATOR_TEXT);
    for (s32 i = 19; i < PakLoader_GetSlotCount(); i++) addSlotWidget(i);

    // ===================== Tab: Controls =====================
    path.sidebarName = "Controls";
    path.column = SECTION_COLUMN_1;

    AddWidget(path, "Pause Menu", WIDGET_SEPARATOR_TEXT);

    // This dropdown picks the button used to change page WITHIN a kaleido page
    // (inventory sub-page, extended equipment, SW97 arrow mode, Broken Modes).
    // The OTHER shoulder button (+ R) changes BETWEEN kaleido pages.
    // Backed by NGCKaleidoSwitcher: 0 = in-page L (pages on Z), 1 = in-page Z (pages on L).
    // NOTE: each label MUST be >1 char or UIWidgets::Combobox hides it (it skips
    // single-character entries), which is why these aren't just "L"/"Z".
    static std::map<int32_t, const char*> inPageBtnMap = {
        { 0, "L button (default)" },
        { 1, "Z button" },
    };
    AddWidget(path, "In-Page Change Button (L / Z)", WIDGET_CVAR_COMBOBOX)
        .CVar(CVAR_ENHANCEMENT("NGCKaleidoSwitcher"))
        .RaceDisable(false)
        .Options(ComboboxOptions()
                     .ComboMap(inPageBtnMap)
                     .DefaultIndex(0)
                     .Tooltip("Which shoulder button changes page WITHIN the current kaleido page:\n"
                              "inventory sub-page (Items), extended equipment (Equipment), SW97 arrow mode\n"
                              "(Quest), and Broken Modes (Map). The OTHER shoulder button, together with R,\n"
                              "changes BETWEEN the kaleido pages.\n"
                              "L button (default): L changes within the page, Z + R change kaleido pages.\n"
                              "Z button: Z changes within the page, L + R change kaleido pages."));

    AddWidget(path, "Equip Items on D-Pad", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("DpadEquips"))
        .Options(CheckboxOptions().Tooltip("Allow equipping items to the D-Pad directions."));
    AddWidget(path, "D-Pad on Pause", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_SETTING("DPadOnPause"))
        .Options(CheckboxOptions().Tooltip("Use the D-Pad to navigate the pause menu."));

    AddWidget(path, "Camera", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Free Camera", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_SETTING("FreeLook.Enabled"))
        .RaceDisable(false)
        .Options(CheckboxOptions().Tooltip(
            "Enables free camera control (right stick / mouse).\n"
            "Same setting as Settings > Controls > Free Look — surfaced here for convenience."));

    AddWidget(path, "Transformation Controls", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Mario Controls", WIDGET_BUTTON)
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) { /* placeholder — SM64 Mario rebinding UI coming soon */ })
        .Options(ButtonOptions().Size(Sizes::Inline).Tooltip(
            "Rebind SM64 Mario mode controls. (Placeholder — per-move bindings coming in a future update.)"));
    // Pikachu Controls — opens a dedicated assignment window (pikachu_hud.cpp):
    // per-move N64 button binds (gPikaBind.*) + the mode UI style. Applies to
    // the SECRET Broken-Modes Pikachu mode only; the pokeball transformation
    // keeps items on C and the vanilla UI.
    AddWidget(path, "Pikachu Controls", WIDGET_BUTTON)
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) { PikachuControls_OpenWindow(); })
        .Options(ButtonOptions().Size(Sizes::Inline).Tooltip(
            "Open the Pikachu mode controls window: assign the N64 button for each move\n"
            "(Jump / Quick Attack / Grass / Gigantamax / Iron Tail / Dark / Sleep) and pick\n"
            "the mode UI style (icons over OOT buttons, or the corner HUD).\n"
            "Secret Broken-Modes Pikachu mode only — the pokeball transformation is untouched."));
}

} // namespace SohGui
