// =============================================================================
// HarpoonRemoteSaveEditor — GM-side editor that operates on a cached
// snapshot of a REMOTE peer's save (a HarpoonTemplates::Template). On
// "Apply" it reuses the existing TEMPLATE_APPLY broadcast so the peer
// overwrites their gSaveContext with the edited Template.
//
// The UI mirrors the vanilla SaveEditorWindow (debugSaveEditor.cpp) 1:1
// for every tab that maps onto Template-tracked state:
//   - Info           : file/player name/health/magic/rupees/time/timers/settings
//   - Inventory      : items[72] + ammo[16] (vanilla / custom / MM masks)
//   - Flags          : eventChkInf / itemGetInf / infTable / eventInf /
//                      randomizerInf + Saved Scene Flags + Gold Skulltulas
//   - Equipment      : equipment bitmask + upgrades (bullet bag, quiver,
//                      bomb bag, scale, strength, wallet, sticks, nuts)
//   - Quest Status   : medallions / stones / songs / GS count / PoH /
//                      dungeon items
// The Player tab is intentionally omitted — it edits live Player* / actor
// state, which is not part of the snapshot/apply round-trip.
// =============================================================================

#include "RemoteSaveEditor.h"
#include "Harpoon.h"
#include "Templates.h"

#include "soh/Enhancements/debugger/debugSaveEditor.h"
#include "soh/SohGui/ImGuiUtils.h"
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/SohGui/SohGui.hpp"
#include "soh/OTRGlobals.h"
#include "soh/util.h"

#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
#include <imgui.h>
#include <array>
#include <map>
#include <string>
#include <vector>
#include <chrono>
#include <cstdio>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"

// Forward-declared from mods/extended_inventory.c.
extern const uint8_t gPage2Items[24];
extern const uint8_t gPage3MaskItems[24];

// Tables defined in soh/src/code/z_inventory.c.
extern u32 gUpgradeMasks[];
extern u32 gUpgradeNegMasks[];
extern u8  gUpgradeShifts[];
extern u32 gGsFlagsMasks[];
extern u32 gGsFlagsShifts[];
extern u8  gAreaGsFlags[];
extern u8  gAmmoItems[];
}

using namespace UIWidgets;

namespace HarpoonRemoteSaveEditor {

namespace {

// ----------------------------------------------------------------------------
// State
// ----------------------------------------------------------------------------

uint32_t sTargetCid = 0;
std::map<uint32_t, HarpoonTemplates::Template> sSnapshots;
std::map<uint32_t, std::chrono::steady_clock::time_point> sSnapshotTime;

char sSaveAsName[64] = "";

bool IsHostLocal() {
    if (Harpoon::Instance == nullptr) return false;
    return Harpoon::Instance->ownClientId != 0 &&
           Harpoon::Instance->ownClientId == Harpoon::Instance->hostClientId;
}

std::string PeerLabel(uint32_t cid) {
    if (Harpoon::Instance == nullptr) return "cid" + std::to_string(cid);
    auto it = Harpoon::Instance->clients.find(cid);
    if (it == Harpoon::Instance->clients.end() || it->second.name.empty()) {
        return "cid" + std::to_string(cid);
    }
    return it->second.name;
}

HarpoonTemplates::Template* GetActiveSnapshot() {
    if (sTargetCid == 0) return nullptr;
    auto it = sSnapshots.find(sTargetCid);
    if (it == sSnapshots.end()) return nullptr;
    return &it->second;
}

float SnapshotAgeSeconds(uint32_t cid) {
    auto it = sSnapshotTime.find(cid);
    if (it == sSnapshotTime.end()) return -1.0f;
    auto now = std::chrono::steady_clock::now();
    auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
    return (float)dur.count() / 1000.0f;
}

// Bordered auto-sized child — mirror of debugSaveEditor.cpp's file-scope
// helper. Used to wrap flag-grid sections.
template <typename T>
void DrawGroupWithBorder(T&& drawFunc, std::string section) {
    ImGui::BeginChild(std::string("##" + section).c_str(), ImVec2(0, 0),
                      ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_Borders |
                          ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    drawFunc();
    ImGui::EndGroup();
    ImGui::EndChild();
}

// z2ASCII / decodeNTSCPlayerNameChar mirrors of debugSaveEditor.cpp —
// needed for showing the editable player name in Info tab.
char z2ASCII(int code) {
    int ret;
    if (code < 10)                       ret = code + 0x30;
    else if (code >= 10 && code < 36)    ret = code + 0x37;
    else if (code >= 36 && code < 62)    ret = code + 0x3D;
    else if (code == 62)                 ret = code - 0x1E;
    else if (code == 63 || code == 64)   ret = code - 0x12;
    else                                  ret = code;
    return char(ret);
}

// ----------------------------------------------------------------------------
// Upgrade helpers — read/write the upgrades bitfield in Template using
// the same gUpgradeMasks / gUpgradeShifts the engine uses. Replaces the
// engine's Inventory_ChangeUpgrade for remote editing.
// ----------------------------------------------------------------------------

s32 TplUpgValue(const HarpoonTemplates::Template* t, int upg) {
    return (s32)((t->upgrades & gUpgradeMasks[upg]) >> gUpgradeShifts[upg]);
}

void TplSetUpgValue(HarpoonTemplates::Template* t, int upg, s32 value) {
    t->upgrades = (t->upgrades & gUpgradeNegMasks[upg]) |
                  (((u32)value) << gUpgradeShifts[upg]);
}

// Mirror of debugSaveEditor.cpp::DrawUpgradeIcon, but reads/writes a
// Template's upgrades bitfield instead of gSaveContext via the engine.
void DrawUpgradeIconForTemplate(HarpoonTemplates::Template* t,
                                const std::string& categoryName,
                                int categoryId,
                                const std::vector<uint8_t>& items) {
    static const char* upgradePopupPicker = "remoteUpgradePopupPicker";
    ImGui::PushID((categoryName + "_remoteUpg").c_str());

    PushStyleButton(Colors::DarkGray);
    auto value = (size_t)TplUpgValue(t, categoryId);
    uint8_t item = value < items.size() ? items[value] : ITEM_NONE;
    if (item != ITEM_NONE) {
        const ItemMapEntry& slotEntry = itemMapping[item];
        if (ImGui::ImageButton((slotEntry.name + "##remoteUpgBtn").c_str(),
                               Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName(slotEntry.name),
                               ImVec2(48.0f, 48.0f), ImVec2(0, 0), ImVec2(1, 1))) {
            ImGui::OpenPopup(upgradePopupPicker);
        }
    } else {
        if (ImGui::Button("##remoteUpgEmpty",
                          ImVec2(48.0f, 48.0f) + ImGui::GetStyle().FramePadding * 2)) {
            ImGui::OpenPopup(upgradePopupPicker);
        }
    }
    PopStyleButton();
    Tooltip(categoryName.c_str());

    if (ImGui::BeginPopup(upgradePopupPicker)) {
        for (size_t pickerIndex = 0; pickerIndex < items.size(); pickerIndex++) {
            if ((pickerIndex % 8) != 0) ImGui::SameLine();
            PushStyleButton(Colors::DarkGray);
            if (items[pickerIndex] == ITEM_NONE) {
                if (ImGui::Button("##remoteUpgNone",
                                  ImVec2(48.0f, 48.0f) + ImGui::GetStyle().FramePadding * 2)) {
                    TplSetUpgValue(t, categoryId, (s32)pickerIndex);
                    ImGui::CloseCurrentPopup();
                }
                Tooltip("None");
            } else {
                const ItemMapEntry& slotEntry = itemMapping[items[pickerIndex]];
                bool ret = ImGui::ImageButton(
                    (slotEntry.name + "##remoteUpgPick").c_str(),
                    Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName(slotEntry.name),
                    ImVec2(48.0f, 48.0f), ImVec2(0, 0), ImVec2(1, 1));
                if (ret) {
                    TplSetUpgValue(t, categoryId, (s32)pickerIndex);
                    ImGui::CloseCurrentPopup();
                }
                Tooltip(SohUtils::GetItemName(slotEntry.id).c_str());
            }
            PopStyleButton();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

// Mirror of debugSaveEditor.cpp::DrawUpgrade (text-combo variant).
void DrawUpgradeComboForTemplate(HarpoonTemplates::Template* t,
                                 const std::string& categoryName,
                                 int categoryId,
                                 const std::vector<std::string>& names) {
    ImGui::Text("%s", categoryName.c_str());
    ImGui::SameLine();
    ImGui::PushID((categoryName + "_remoteUpgCombo").c_str());
    PushStyleCombobox(THEME_COLOR);
    ImGui::AlignTextToFramePadding();
    auto value = (size_t)TplUpgValue(t, categoryId);
    auto name = value < names.size() ? names[value].c_str() : "Glitched";
    if (ImGui::BeginCombo("##remoteUpgC", name)) {
        for (size_t i = 0; i < names.size(); i++) {
            if (ImGui::Selectable(names[i].c_str())) {
                TplSetUpgValue(t, categoryId, (s32)i);
            }
        }
        ImGui::EndCombo();
    }
    PopStyleCombobox();
    ImGui::PopID();
    Tooltip(categoryName.c_str());
}

// ----------------------------------------------------------------------------
// Flag-table helpers
// ----------------------------------------------------------------------------

uint16_t* TemplateFlagEntry(HarpoonTemplates::Template* t, FlagTableType type, size_t row) {
    switch (type) {
        case EVENT_CHECK_INF:
            if (row >= ARRAY_COUNT(t->eventChkInf)) return nullptr;
            return &t->eventChkInf[row];
        case ITEM_GET_INF:
            if (row >= ARRAY_COUNT(t->itemGetInf)) return nullptr;
            return &t->itemGetInf[row];
        case INF_TABLE:
            if (row >= ARRAY_COUNT(t->infTable)) return nullptr;
            return &t->infTable[row];
        case EVENT_INF:
            if (row >= ARRAY_COUNT(t->eventInf)) return nullptr;
            return &t->eventInf[row];
        case RANDOMIZER_INF:
            if (row >= t->randomizerInf.size()) return nullptr;
            return &t->randomizerInf[row];
        default:
            return nullptr;
    }
}

void DrawTemplateFlagRow(const FlagTable& flagTable, uint16_t row, uint16_t& flags) {
    ImGui::PushID((std::to_string(row) + flagTable.name + "_remote").c_str());
    for (int32_t flagIndex = 15; flagIndex >= 0; flagIndex--) {
        ImGui::SameLine();
        ImGui::PushID(flagIndex);
        bool hasDescription = !!flagTable.flagDescriptions.contains(row * 16 + flagIndex);
        uint32_t bitMask = 1u << flagIndex;
        PushStyleCheckbox(hasDescription ? THEME_COLOR : Colors::DarkGray);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
        bool flag = (flags & bitMask) != 0;
        if (ImGui::Checkbox("##rcheck", &flag)) {
            if (flag) flags |= bitMask;
            else      flags &= (uint16_t)~bitMask;
        }
        ImGui::PopStyleVar();
        PopStyleCheckbox();
        if (ImGui::IsItemHovered() && hasDescription) {
            ImGui::BeginTooltip();
            uint16_t index = row * 16 + flagIndex;
            const char* desc = flagTable.flagDescriptions.at(index);
            ImGui::Text("0x%02X: %s", index, UIWidgets::WrappedText(desc, 60).c_str());
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}

// ----------------------------------------------------------------------------
// Info tab — 1:1 mirror of debugSaveEditor.cpp::DrawInfoTab against Template
// ----------------------------------------------------------------------------

void DrawInfoTab(HarpoonTemplates::Template* t) {
    // File number combo.
    static const std::map<int32_t, const char*> fileNumMap = {
        { 0, "File 1" }, { 1, "File 2" }, { 2, "File 3" },
    };
    if (t->fileNum >= 0 && t->fileNum <= 2) {
        Combobox("File Number", &t->fileNum, fileNumMap,
                 ComboboxOptions().Color(THEME_COLOR).Tooltip("Current File Number"));
    } else {
        PushStyleInput(THEME_COLOR);
        ImGui::PushItemWidth(ImGui::GetFontSize() * 6);
        ImGui::InputScalar("File Number", ImGuiDataType_S32, &t->fileNum);
        ImGui::PopItemWidth();
        PopStyleInput();
    }

    // Player name (PAL encoding renders inline; NTSC encoding lookup is
    // skipped to keep this self-contained — we still show raw bytes).
    ImU16 one = 1;
    std::string name;
    for (int i = 0; i < 8; i++) {
        if (t->filenameLanguage == 0 /* PAL */) {
            name += z2ASCII(t->playerName[i]);
        } else {
            // NTSC: just show raw byte as ?
            name += (char)('?');
        }
    }
    name += '\0';

    ImGui::PushItemWidth(ImGui::GetFontSize() * 6);
    ImGui::Text("Name: %s", name.c_str());
    Tooltip("Player Name");
    std::string nameID;
    for (int i = 0; i < 8; i++) {
        nameID = z2ASCII(i);
        if (i % 4 != 0) ImGui::SameLine();
        PushStyleInput(THEME_COLOR);
        ImGui::InputScalar(nameID.c_str(), ImGuiDataType_U8, &t->playerName[i], &one, NULL);
        PopStyleInput();
    }

    // Filename language combo.
    static const std::map<uint8_t, const char*> filenameLanguageMap = {
        { 0, "PAL" }, { 1, "NTSC JPN" }, { 2, "NTSC ENG" },
    };
    Combobox("Player Name Language", &t->filenameLanguage, filenameLanguageMap,
             ComboboxOptions().Color(THEME_COLOR).Tooltip("Encoding used for Player Name"));

    // Max Health (using intermediate per vanilla).
    int16_t healthIntermediary = t->healthCapacity;
    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Max Health", ImGuiDataType_S16, &healthIntermediary);
    PopStyleInput();
    if (ImGui::IsItemDeactivated()) {
        t->healthCapacity = healthIntermediary;
    }
    Tooltip("Maximum health. 16 units per full heart");

    // Double Defense.
    bool isDoubleDefenseAcquired = t->isDoubleDefenseAcquired != 0;
    if (Checkbox("Double Defense", &isDoubleDefenseAcquired,
                 CheckboxOptions().Color(THEME_COLOR).Tooltip("Is double defense unlocked?"))) {
        t->isDoubleDefenseAcquired = isDoubleDefenseAcquired ? 1 : 0;
        t->defenseHearts = isDoubleDefenseAcquired ? 20 : 0;
    }

    // Magic Level combo.
    static const std::map<int8_t, const char*> magicLevelMap = {
        { 0, "None" }, { 1, "Single" }, { 2, "Double" },
    };
    if (Combobox("Magic Level", &t->magicLevel, magicLevelMap,
                 ComboboxOptions().Color(THEME_COLOR).Tooltip("Current magic level"))) {
        t->isMagicAcquired = t->magicLevel > 0 ? 1 : 0;
        t->isDoubleMagicAcquired = t->magicLevel == 2 ? 1 : 0;
    }
    t->magicCapacity = t->magicLevel * 0x30;
    if (t->magic > t->magicCapacity) t->magic = t->magicCapacity;

    int32_t magic = (int32_t)t->magic;
    if (SliderInt("Magic", &magic,
                  IntSliderOptions().Color(THEME_COLOR).Min(0)
                                    .Max(t->magicCapacity)
                                    .Tooltip("Current magic. 48 units per magic level"))) {
        t->magic = (int8_t)magic;
    }

    // Rupees.
    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Rupees", ImGuiDataType_S16, &t->rupees);
    Tooltip("Current rupees");
    PopStyleInput();

    // Time of day.
    int32_t dayTimeI = (int32_t)t->dayTime;
    if (SliderInt("Time", &dayTimeI,
                  IntSliderOptions().Color(THEME_COLOR).Min(0).Max(0xFFFF).Tooltip("Time of day"))) {
        t->dayTime = (uint16_t)dayTimeI;
    }
    if (Button("Dawn", ButtonOptions().Color(THEME_COLOR).Size(Sizes::Inline)))     t->dayTime = 0x4000;
    ImGui::SameLine();
    if (Button("Noon", ButtonOptions().Color(THEME_COLOR).Size(Sizes::Inline)))     t->dayTime = 0x8000;
    ImGui::SameLine();
    if (Button("Sunset", ButtonOptions().Color(THEME_COLOR).Size(Sizes::Inline)))   t->dayTime = 0xC001;
    ImGui::SameLine();
    if (Button("Midnight", ButtonOptions().Color(THEME_COLOR).Size(Sizes::Inline))) t->dayTime = 0;

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Total Days", ImGuiDataType_S32, &t->totalDays);
    Tooltip("Total number of days elapsed since the start of the game");
    PopStyleInput();

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Deaths", ImGuiDataType_U16, &t->deaths);
    Tooltip("Total number of deaths");
    PopStyleInput();

    bool bgs = t->bgsFlag != 0;
    if (Checkbox("Has BGS", &bgs,
                 CheckboxOptions().Color(THEME_COLOR)
                    .Tooltip("Is Biggoron sword unlocked? Replaces Giant's knife"))) {
        t->bgsFlag = bgs ? 1 : 0;
    }

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Sword Health", ImGuiDataType_U16, &t->swordHealth);
    Tooltip("Giant's knife health. Default is 8. Must be >0 for Biggoron sword to work");
    PopStyleInput();

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Bgs Day Count", ImGuiDataType_S32, &t->bgsDayCount);
    Tooltip("Total number of days elapsed since receiving claim check from Biggoron");
    PopStyleInput();

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Entrance Index", ImGuiDataType_S32, &t->entranceIndex);
    Tooltip("From which entrance did Link arrive?");
    PopStyleInput();

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Cutscene Index", ImGuiDataType_S32, &t->cutsceneIndex);
    Tooltip("Which cutscene is this?");
    PopStyleInput();

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Navi Timer", ImGuiDataType_U16, &t->naviTimer);
    Tooltip("Navi wants to talk at 600 units, decides not to at 3000.");
    PopStyleInput();

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Timer State", ImGuiDataType_S16, &t->timerState);
    Tooltip("Heat timer, race timer, etc. Has white font");
    PopStyleInput();

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Timer Seconds", ImGuiDataType_S16, &t->timerSeconds, &one, NULL);
    Tooltip("Time, in seconds");
    PopStyleInput();

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Sub-Timer State", ImGuiDataType_S16, &t->subTimerState);
    Tooltip("Trade timer, Ganon collapse timer, etc. Has yellow font");
    PopStyleInput();

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Sub-Timer Seconds", ImGuiDataType_S16, &t->subTimerSeconds, &one, NULL);
    Tooltip("Time, in seconds");
    PopStyleInput();

    static const std::map<uint8_t, const char*> audioMap = {
        { 0, "Stereo" }, { 1, "Mono" }, { 2, "Headset" }, { 3, "Surround" },
    };
    Combobox("Audio", &t->audioSetting, audioMap,
             ComboboxOptions().Color(THEME_COLOR).Tooltip("Sound setting"));

    bool n64dd = t->n64ddFlag != 0;
    if (Checkbox("64 DD file?", &n64dd,
                 CheckboxOptions().Color(THEME_COLOR)
                    .Tooltip("WARNING! If you save, your file may be locked! Use caution!"))) {
        t->n64ddFlag = n64dd ? 1 : 0;
    }

    static const std::map<uint8_t, const char*> zTargetMap = {
        { 0, "Switch" }, { 1, "Hold" },
    };
    Combobox("Z Target Mode", &t->zTargetSetting, zTargetMap,
             ComboboxOptions().Color(THEME_COLOR).Tooltip("Z-Targeting behavior"));

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Triforce Pieces", ImGuiDataType_U8, &t->triforcePiecesCollected);
    Tooltip("Currently obtained Triforce Pieces. For Triforce Hunt.");
    PopStyleInput();

    // GM movement restrictions (Template-only extension).
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.5f, 1.0f), "Movement Restrictions (GM)");
    ImGui::Checkbox("Restrict climb", &t->restrictNoClimb);
    ImGui::Checkbox("Restrict grab",  &t->restrictNoGrab);
    ImGui::Checkbox("Restrict crawl", &t->restrictNoCrawl);
    ImGui::Checkbox("Restrict talk",  &t->restrictNoTalk);

    // Minigame high scores.
    ImGui::PushItemWidth(ImGui::GetFontSize() * 10);
    static const std::array<const char*, 7> minigameHS = {
        "Horseback Archery", "Big Poe Points", "Fishing", "Malon's Obstacle Course",
        "Running Man Race",  "?",              "Dampe's Race"
    };
    if (ImGui::TreeNode("Minigames##remoteMin")) {
        for (int i = 0; i < 7; i++) {
            if (i == 5) continue;  // unused
            PushStyleInput(THEME_COLOR);
            ImGui::InputScalar(minigameHS[i], ImGuiDataType_S32, &t->highScores[i], &one, NULL);
            PopStyleInput();
        }
        ImGui::TreePop();
    }
    ImGui::PopItemWidth();

    ImGui::PopItemWidth();
}

// ----------------------------------------------------------------------------
// Inventory tab — 1:1 mirror of debugSaveEditor.cpp::DrawInventoryTab
// ----------------------------------------------------------------------------

constexpr float kInvImageSize = 48.0f;

void DrawInventoryTab(HarpoonTemplates::Template* t) {
    static bool restrictToValid = true;
    Checkbox("Restrict to valid items", &restrictToValid,
             CheckboxOptions().Color(THEME_COLOR)
                .Tooltip("Restricts items and ammo to only what is possible to legally acquire in-game"));

    ImGui::Text("Vanilla Inventory (Page 1)");
    ImGui::Separator();
    static int32_t sSelectedIndex = -1;
    const char* itemPopupPicker = "remoteItemPopupPicker";

    for (int32_t y = 0; y < 4; y++) {
        for (int32_t x = 0; x < 6; x++) {
            int32_t index = x + y * 6;
            ImGui::PushID(index);
            if (x != 0) ImGui::SameLine();

            uint8_t item = t->items[index];
            PushStyleButton(Colors::DarkGray);
            if (item != ITEM_NONE) {
                const ItemMapEntry* slotEntryPtr = nullptr;
                auto it = itemMapping.find(item);
                if (it != itemMapping.end()) slotEntryPtr = &it->second;
                else {
                    auto cit = customItemMapping.find(item);
                    if (cit != customItemMapping.end()) slotEntryPtr = &cit->second;
                }
                if (slotEntryPtr) {
                    const ItemMapEntry& slotEntry = *slotEntryPtr;
                    if (ImGui::ImageButton(
                            slotEntry.name.c_str(),
                            Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName(slotEntry.name),
                            ImVec2(kInvImageSize, kInvImageSize), ImVec2(0, 0), ImVec2(1, 1))) {
                        sSelectedIndex = index;
                        ImGui::OpenPopup(itemPopupPicker);
                    }
                }
            } else {
                if (ImGui::Button("##itemNoneR",
                                  ImVec2(kInvImageSize, kInvImageSize) + ImGui::GetStyle().FramePadding * 2)) {
                    sSelectedIndex = index;
                    ImGui::OpenPopup(itemPopupPicker);
                }
            }
            PopStyleButton();

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            if (ImGui::BeginPopup(itemPopupPicker)) {
                PushStyleButton(Colors::DarkGray);
                if (ImGui::Button("##itemNonePickerR",
                                  ImVec2(kInvImageSize, kInvImageSize) + ImGui::GetStyle().FramePadding * 2)) {
                    t->items[sSelectedIndex] = ITEM_NONE;
                    ImGui::CloseCurrentPopup();
                }
                PopStyleButton();
                Tooltip("None");

                std::vector<ItemMapEntry> possibleItems;
                if (restrictToValid) {
                    for (int slotIndex = 0; slotIndex < 56; slotIndex++) {
                        int testIndex = (sSelectedIndex == SLOT_BOTTLE_1 || sSelectedIndex == SLOT_BOTTLE_2 ||
                                         sSelectedIndex == SLOT_BOTTLE_3 || sSelectedIndex == SLOT_BOTTLE_4)
                                            ? SLOT_BOTTLE_1
                                            : sSelectedIndex;
                        if (gItemSlots[slotIndex] == testIndex) {
                            possibleItems.push_back(itemMapping[slotIndex]);
                        }
                    }
                } else {
                    for (const auto& entry : itemMapping) possibleItems.push_back(entry.second);
                }

                for (size_t pickerIndex = 0; pickerIndex < possibleItems.size(); pickerIndex++) {
                    if (((pickerIndex + 1) % 8) != 0) ImGui::SameLine();
                    const ItemMapEntry& slotEntry = possibleItems[pickerIndex];
                    PushStyleButton(Colors::DarkGray);
                    bool ret = ImGui::ImageButton(
                        slotEntry.name.c_str(),
                        Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName(slotEntry.name),
                        ImVec2(kInvImageSize, kInvImageSize), ImVec2(0, 0), ImVec2(1, 1));
                    PopStyleButton();
                    if (ret) {
                        t->items[sSelectedIndex] = (uint8_t)slotEntry.id;
                        ImGui::CloseCurrentPopup();
                    }
                    Tooltip(SohUtils::GetItemName(slotEntry.id).c_str());
                }
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar();
            ImGui::PopID();
        }
    }

    ImGui::Spacing();
    ImGui::Text("Ammo");
    for (uint32_t ammoIndex = 0, drawnAmmoItems = 0; ammoIndex < 16; ammoIndex++) {
        uint8_t item = gAmmoItems[ammoIndex];
        if (item == ITEM_NONE) continue;
        if (drawnAmmoItems != 0) ImGui::SameLine();
        drawnAmmoItems++;
        ImGui::PushID((int)ammoIndex + 5000);
        ImGui::PushItemWidth(kInvImageSize);
        ImGui::BeginGroup();
        auto mapIt = itemMapping.find(item);
        if (mapIt != itemMapping.end()) {
            ImGui::Image(Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName(mapIt->second.name),
                         ImVec2(kInvImageSize, kInvImageSize));
        }
        PushStyleInput(THEME_COLOR);
        ImGui::InputScalar("##ammoInputR", ImGuiDataType_S8, &t->ammo[ammoIndex]);
        PopStyleInput();
        ImGui::EndGroup();
        ImGui::PopItemWidth();
        ImGui::PopID();
    }

    // Custom items.
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Custom Items Inventory (Page 2)", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Give All Custom Items (Max)##remote")) {
            for (int i = 0; i < 24; i++) {
                if (i == 0) t->items[24 + i] = ITEM_ROCS_CAPE;
                else        t->items[24 + i] = gPage2Items[i];
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear All Custom Items##remote")) {
            for (int i = 24; i < 48; i++) t->items[i] = ITEM_NONE;
        }
        ImGui::Spacing();

        static int32_t sSelectedCustom = -1;
        const char* customItemPopupPicker = "remoteCustomPicker";
        for (int32_t y = 0; y < 4; y++) {
            for (int32_t x = 0; x < 6; x++) {
                int32_t visualIndex = x + y * 6;
                int32_t slotIndex = 24 + visualIndex;
                ImGui::PushID(1000 + slotIndex);
                if (x != 0) ImGui::SameLine();
                uint8_t item = t->items[slotIndex];
                bool clicked = false;
                if (item != ITEM_NONE) {
                    auto it = customItemMapping.find(item);
                    if (it != customItemMapping.end()) {
                        const ItemMapEntry& slotEntry = it->second;
                        auto tex = Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName(slotEntry.name);
                        if (tex) {
                            clicked = ImGui::ImageButton(slotEntry.name.c_str(), tex,
                                                          ImVec2(kInvImageSize, kInvImageSize), ImVec2(0, 0), ImVec2(1, 1));
                        } else {
                            PushStyleButton(Colors::DarkGray);
                            clicked = ImGui::Button(slotEntry.name.c_str(),
                                                     ImVec2(kInvImageSize, kInvImageSize) + ImGui::GetStyle().FramePadding * 2);
                            PopStyleButton();
                        }
                    } else {
                        char buttonLabel[64];
                        snprintf(buttonLabel, sizeof(buttonLabel), "0x%02X##customslotR%d", item, slotIndex);
                        PushStyleButton(Colors::DarkGray);
                        clicked = ImGui::Button(buttonLabel,
                                                ImVec2(kInvImageSize, kInvImageSize) + ImGui::GetStyle().FramePadding * 2);
                        PopStyleButton();
                    }
                } else {
                    PushStyleButton(Colors::DarkGray);
                    clicked = ImGui::Button("##customItemNoneR",
                                             ImVec2(kInvImageSize, kInvImageSize) + ImGui::GetStyle().FramePadding * 2);
                    PopStyleButton();
                }
                if (clicked) {
                    sSelectedCustom = slotIndex;
                    ImGui::OpenPopup(customItemPopupPicker);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Slot %d", slotIndex);
                    if (item != ITEM_NONE) ImGui::Text("Item ID: 0x%02X", item);
                    ImGui::EndTooltip();
                }

                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
                if (ImGui::BeginPopup(customItemPopupPicker)) {
                    PushStyleButton(Colors::DarkGray);
                    if (ImGui::Button("##customItemNonePickerR",
                                      ImVec2(kInvImageSize, kInvImageSize) + ImGui::GetStyle().FramePadding * 2)) {
                        t->items[sSelectedCustom] = ITEM_NONE;
                        ImGui::CloseCurrentPopup();
                    }
                    PopStyleButton();
                    Tooltip("None");
                    for (int32_t pickerIndex = 0; pickerIndex < 24; pickerIndex++) {
                        if (((pickerIndex + 1) % 8) != 0) ImGui::SameLine();
                        uint8_t customItemId = gPage2Items[pickerIndex];
                        auto it = customItemMapping.find(customItemId);
                        bool ret = false;
                        if (it != customItemMapping.end()) {
                            const ItemMapEntry& entry = it->second;
                            auto tex = Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName(entry.name);
                            if (tex) {
                                ret = ImGui::ImageButton(entry.name.c_str(), tex,
                                                          ImVec2(kInvImageSize, kInvImageSize), ImVec2(0, 0), ImVec2(1, 1));
                            } else {
                                PushStyleButton(Colors::DarkGray);
                                ret = ImGui::Button(entry.name.c_str(),
                                                     ImVec2(kInvImageSize, kInvImageSize) + ImGui::GetStyle().FramePadding * 2);
                                PopStyleButton();
                            }
                            Tooltip(entry.name.c_str());
                        } else {
                            char pickerLabel[64];
                            snprintf(pickerLabel, sizeof(pickerLabel), "0x%02X##pickerR%d", customItemId, pickerIndex);
                            PushStyleButton(Colors::DarkGray);
                            ret = ImGui::Button(pickerLabel, ImVec2(kInvImageSize, kInvImageSize));
                            PopStyleButton();
                        }
                        if (ret) {
                            t->items[sSelectedCustom] = customItemId;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    // Roc's Cape upgrade
                    ImGui::Spacing();
                    ImGui::Text("Upgrades:");
                    {
                        auto it = customItemMapping.find(ITEM_ROCS_CAPE);
                        bool ret = false;
                        if (it != customItemMapping.end()) {
                            const ItemMapEntry& entry = it->second;
                            auto tex = Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName(entry.name);
                            if (tex) {
                                ret = ImGui::ImageButton(entry.name.c_str(), tex,
                                                          ImVec2(kInvImageSize, kInvImageSize), ImVec2(0, 0), ImVec2(1, 1));
                            } else {
                                PushStyleButton(Colors::DarkGray);
                                ret = ImGui::Button("ITEM_ROCS_CAPE##pickerCapeR",
                                                     ImVec2(kInvImageSize, kInvImageSize) + ImGui::GetStyle().FramePadding * 2);
                                PopStyleButton();
                            }
                        } else {
                            PushStyleButton(Colors::DarkGray);
                            ret = ImGui::Button("ITEM_ROCS_CAPE##pickerCapeR",
                                                 ImVec2(kInvImageSize, kInvImageSize) + ImGui::GetStyle().FramePadding * 2);
                            PopStyleButton();
                        }
                        if (ret) {
                            t->items[sSelectedCustom] = ITEM_ROCS_CAPE;
                            ImGui::CloseCurrentPopup();
                        }
                        Tooltip("Roc's Cape (upgrade)\nShares slot 24 with Roc's Feather");
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopStyleVar();
                ImGui::PopID();
            }
        }
    }

    // MM Masks.
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("MM Masks Inventory (Page 3)")) {
        static const char* sMmMaskNames[24] = {
            "Postman's Hat", "All-Night Mask", "Blast Mask",   "Stone Mask",      "Great Fairy Mask", "Deku Mask",
            "Keaton Mask",   "Bremen Mask",    "Bunny Hood",   "Don Gero's Mask", "Mask of Scents",   "Goron Mask",
            "Romani's Mask", "Circus Leader",  "Kafei's Mask", "Couple's Mask",   "Mask of Truth",    "Zora Mask",
            "Kamaro's Mask", "Gibdo Mask",     "Garo Mask",    "Captain's Hat",   "Giant's Mask",     "Fierce Deity",
        };
        if (ImGui::Button("Give All MM Masks##remote")) {
            for (int i = 0; i < 24; i++) t->items[48 + i] = gPage3MaskItems[i];
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear All MM Masks##remote")) {
            for (int i = 48; i < 72; i++) t->items[i] = ITEM_NONE;
        }
        ImGui::SameLine();
        if (ImGui::Button("Give Random MM Mask##remote")) {
            std::vector<int> emptySlots;
            for (int i = 0; i < 24; i++) {
                if (t->items[48 + i] == ITEM_NONE) emptySlots.push_back(i);
            }
            if (!emptySlots.empty()) {
                int r = emptySlots[rand() % emptySlots.size()];
                t->items[48 + r] = gPage3MaskItems[r];
            }
        }
        ImGui::Spacing();

        for (int32_t y = 0; y < 4; y++) {
            for (int32_t x = 0; x < 6; x++) {
                int32_t visualIndex = x + y * 6;
                int32_t slotIndex = 48 + visualIndex;
                ImGui::PushID(2000 + slotIndex);
                if (x != 0) ImGui::SameLine();
                uint8_t item = t->items[slotIndex];
                const char* maskName = sMmMaskNames[visualIndex];
                bool hasItem = (item != ITEM_NONE);
                auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
                auto tex = gui->GetTextureByName(maskName);
                if (tex) {
                    PushStyleButton(Colors::DarkGray);
                    bool clicked;
                    if (hasItem) {
                        clicked = ImGui::ImageButton(maskName, tex, ImVec2(kInvImageSize, kInvImageSize),
                                                      ImVec2(0, 0), ImVec2(1, 1));
                    } else {
                        clicked = ImGui::ImageButton(maskName, tex, ImVec2(kInvImageSize, kInvImageSize),
                                                      ImVec2(0, 0), ImVec2(1, 1),
                                                      ImVec4(0, 0, 0, 0), ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
                    }
                    PopStyleButton();
                    if (clicked) {
                        if (hasItem) t->items[slotIndex] = ITEM_NONE;
                        else         t->items[slotIndex] = gPage3MaskItems[visualIndex];
                    }
                } else {
                    char buttonLabel[64];
                    if (hasItem) {
                        snprintf(buttonLabel, sizeof(buttonLabel), "%s##mmslotR%d", maskName, slotIndex);
                        PushStyleButton(Colors::Green);
                    } else {
                        snprintf(buttonLabel, sizeof(buttonLabel), "---##mmslotR%d", slotIndex);
                        PushStyleButton(Colors::DarkGray);
                    }
                    if (ImGui::Button(buttonLabel,
                                      ImVec2(kInvImageSize + 20, kInvImageSize) + ImGui::GetStyle().FramePadding * 2)) {
                        if (hasItem) t->items[slotIndex] = ITEM_NONE;
                        else         t->items[slotIndex] = gPage3MaskItems[visualIndex];
                    }
                    PopStyleButton();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Slot %d: %s", slotIndex, maskName);
                    if (hasItem) ImGui::Text("Item ID: 0x%02X", item);
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
        }
    }
}

// ----------------------------------------------------------------------------
// Flags tab — 1:1 with vanilla (Saved Scene Flags + GS + flag tables).
// Skips "Player State" + "Current Scene" trees (live engine state).
// ----------------------------------------------------------------------------

void DrawFlagsTab(HarpoonTemplates::Template* t) {
    // Saved Scene Flags
    if (ImGui::TreeNode("Saved Scene Flags##remote")) {
        static uint32_t selectedSceneFlagMap = 0;
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Map");
        ImGui::SameLine();
        PushStyleCombobox(THEME_COLOR);
        if (ImGui::BeginCombo("##MapR", SohUtils::GetSceneName(selectedSceneFlagMap).c_str())) {
            for (int32_t sceneIndex = 0; sceneIndex < SCENE_ID_MAX && sceneIndex < (int32_t)ARRAY_COUNT(t->sceneFlags); sceneIndex++) {
                if (ImGui::Selectable(SohUtils::GetSceneName(sceneIndex).c_str())) {
                    selectedSceneFlagMap = sceneIndex;
                }
            }
            ImGui::EndCombo();
        }
        PopStyleCombobox();

        auto& scene = t->sceneFlags[selectedSceneFlagMap];

        DrawGroupWithBorder([&]() {
            ImGui::Text("Switch");
            DrawFlagArray32("SwitchR", scene.swch, THEME_COLOR);
        }, "RSavedSwitch");

        ImGui::SameLine();

        DrawGroupWithBorder([&]() {
            ImGui::Text("Clear");
            DrawFlagArray32("ClearR", scene.clear, THEME_COLOR);
        }, "RSavedClear");

        DrawGroupWithBorder([&]() {
            ImGui::Text("Collect");
            DrawFlagArray32("CollectR", scene.collect, THEME_COLOR);
        }, "RSavedCollect");

        ImGui::SameLine();

        DrawGroupWithBorder([&]() {
            ImGui::Text("Chest");
            DrawFlagArray32("ChestR", scene.chest, THEME_COLOR);
        }, "RSavedChest");

        DrawGroupWithBorder([&]() {
            ImGui::Text("Rooms");
            DrawFlagArray32("RoomsR", scene.rooms, THEME_COLOR);
        }, "RSavedRooms");

        ImGui::SameLine();

        DrawGroupWithBorder([&]() {
            ImGui::Text("Floors");
            DrawFlagArray32("FloorsR", scene.floors, THEME_COLOR);
        }, "RSavedFloors");

        ImGui::TreePop();
    }

    // Gold Skulltulas
    DrawGroupWithBorder([&]() {
        PushStyleCombobox(THEME_COLOR);
        static size_t selectedGsMap = 0;
        // gsMapping is the const std::vector<const char*> declared in
        // debugSaveEditor.cpp - we don't have it here, so use scene name
        // for the dropdown label instead (close enough).
        ImGui::Text("Gold Skulltulas");
        // Just iterate the 22 areas (matches gsMapping size).
        static const char* gsAreaNames[22] = {
            "Deku Tree", "Dodongo's Cavern", "Inside Jabu-Jabu's Belly", "Forest Temple",
            "Fire Temple", "Water Temple", "Spirit Temple", "Shadow Temple",
            "Bottom of the Well", "Ice Cavern", "Hyrule Field", "Lon Lon Ranch",
            "Kokiri Forest", "Lost Woods, Sacred Forest Meadow",
            "Castle Town and Ganon's Castle", "Death Mountain Trail, Goron City",
            "Kakariko Village", "Zora Fountain, River", "Lake Hylia",
            "Gerudo Valley", "Gerudo Fortress", "Desert Colossus, Haunted Wasteland",
        };
        if (ImGui::BeginCombo("##GSMapR", gsAreaNames[selectedGsMap])) {
            for (size_t i = 0; i < 22; i++) {
                if (ImGui::Selectable(gsAreaNames[i])) selectedGsMap = i;
            }
            ImGui::EndCombo();
        }
        PopStyleCombobox();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Flags");
        // Read/write the gsFlags array using the engine's mask/shift tables.
        u32 currentFlags = ((u32)t->gsFlags[selectedGsMap >> 2] & gGsFlagsMasks[selectedGsMap & 3])
                            >> gGsFlagsShifts[selectedGsMap & 3];
        u32 allFlags = (selectedGsMap < 22) ? gAreaGsFlags[selectedGsMap] : 0xFF;
        u32 setMask = 1;
        while (allFlags != 0) {
            bool isSet = (currentFlags & 0x1) == 0x1;
            ImGui::SameLine();
            ImGui::PushID((int)(allFlags + selectedGsMap * 1000));
            PushStyleCheckbox(THEME_COLOR);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
            if (ImGui::Checkbox("##gsR", &isSet)) {
                u32 word = (u32)t->gsFlags[selectedGsMap >> 2];
                u32 curArea = (word & gGsFlagsMasks[selectedGsMap & 3]) >> gGsFlagsShifts[selectedGsMap & 3];
                if (isSet) curArea |= setMask;
                else       curArea &= ~setMask;
                word = (word & ~gGsFlagsMasks[selectedGsMap & 3])
                       | ((curArea << gGsFlagsShifts[selectedGsMap & 3]) & gGsFlagsMasks[selectedGsMap & 3]);
                t->gsFlags[selectedGsMap >> 2] = (int32_t)word;
            }
            ImGui::PopStyleVar();
            PopStyleCheckbox();
            ImGui::PopID();
            allFlags >>= 1;
            currentFlags >>= 1;
            setMask <<= 1;
        }
    }, "RGoldSkulltulas");

    // Flag tables (eventChkInf, itemGetInf, infTable, eventInf, randomizerInf).
    static std::map<std::string, ImGuiTextFilter> sFlagFilters;
    for (const FlagTable& flagTable : flagTables) {
        if (ImGui::TreeNode(flagTable.name)) {
            ImGui::PushID((std::string(flagTable.name) + "_remote").c_str());
            ImGuiTextFilter& flagFilter = sFlagFilters[flagTable.name];
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16);
            PushStyleInput(THEME_COLOR);
            flagFilter.Draw();
            PopStyleInput();
            ImGui::Spacing();

            size_t rows = flagTable.size + 1;
            if (flagTable.flagTableType == RANDOMIZER_INF) {
                rows = t->randomizerInf.size();
            }

            if (!flagFilter.IsActive()) {
                for (size_t j = 0; j < rows; j++) {
                    uint16_t* slot = TemplateFlagEntry(t, flagTable.flagTableType, j);
                    if (slot == nullptr) continue;
                    DrawGroupWithBorder([&]() {
                        if (j == 0) {
                            for (int k = 0xF; k >= 0; k--) {
                                ImGui::SameLine(37.5f + ((0xF - k) * 33.8f));
                                ImGui::Text("%X", k);
                            }
                        }
                        ImGui::Text("%s", fmt::format("{:<2X}", j).c_str());
                        DrawTemplateFlagRow(flagTable, (uint16_t)j, *slot);
                    }, std::string(flagTable.name) + "_remote");
                }
            } else {
                bool hasMatches = false;
                for (size_t row = 0; row < rows; row++) {
                    uint16_t* slot = TemplateFlagEntry(t, flagTable.flagTableType, row);
                    if (slot == nullptr) continue;
                    for (int32_t flagIndex = 15; flagIndex >= 0; flagIndex--) {
                        uint16_t index = (uint16_t)(row * 16 + flagIndex);
                        auto descIt = flagTable.flagDescriptions.find(index);
                        const char* desc = descIt != flagTable.flagDescriptions.end()
                                              ? descIt->second : "";
                        std::string searchable = fmt::format("0x{:02X} {}", index, desc);
                        if (!flagFilter.PassFilter(searchable.c_str())) continue;
                        hasMatches = true;
                        ImGui::PushID(index);
                        bool hasDesc = descIt != flagTable.flagDescriptions.end();
                        uint32_t bitMask = 1u << flagIndex;
                        PushStyleCheckbox(hasDesc ? THEME_COLOR : Colors::DarkGray);
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
                        bool flag = ((*slot) & bitMask) != 0;
                        if (ImGui::Checkbox("##rsearch", &flag)) {
                            if (flag) *slot |=  (uint16_t)bitMask;
                            else       *slot &= (uint16_t)~bitMask;
                        }
                        ImGui::PopStyleVar();
                        PopStyleCheckbox();
                        ImGui::SameLine();
                        if (hasDesc) ImGui::TextWrapped("0x%02X: %s", index, desc);
                        else         ImGui::Text("0x%02X", index);
                        ImGui::PopID();
                    }
                }
                if (!hasMatches) ImGui::Text("No flags match the current search.");
            }
            ImGui::PopID();
            ImGui::TreePop();
        }
    }
}

// ----------------------------------------------------------------------------
// Equipment tab — 1:1 mirror.
// ----------------------------------------------------------------------------

void DrawEquipmentTab(HarpoonTemplates::Template* t) {
    const std::vector<uint8_t> equipmentValues = {
        ITEM_SWORD_KOKIRI, ITEM_SWORD_MASTER,  ITEM_SWORD_BGS,     ITEM_SWORD_BROKEN,
        ITEM_SHIELD_DEKU,  ITEM_SHIELD_HYLIAN, ITEM_SHIELD_MIRROR, ITEM_NONE,
        ITEM_TUNIC_KOKIRI, ITEM_TUNIC_GORON,   ITEM_TUNIC_ZORA,    ITEM_NONE,
        ITEM_BOOTS_KOKIRI, ITEM_BOOTS_IRON,    ITEM_BOOTS_HOVER,   ITEM_NONE,
    };
    for (size_t i = 0; i < equipmentValues.size(); i++) {
        if (equipmentValues[i] == ITEM_NONE) continue;
        if ((i % 4) != 0) ImGui::SameLine();
        ImGui::PushID((int)i + 7000);
        uint32_t bitMask = 1u << i;
        bool hasEquip = (bitMask & t->equipment) != 0;
        auto it = itemMapping.find(equipmentValues[i]);
        if (it != itemMapping.end()) {
            const ItemMapEntry& entry = it->second;
            PushStyleButton(Colors::DarkGray);
            bool ret = ImGui::ImageButton(
                (entry.name + "##eqR").c_str(),
                Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName(
                    hasEquip ? entry.name : entry.nameFaded),
                ImVec2(kInvImageSize, kInvImageSize), ImVec2(0, 0), ImVec2(1, 1));
            PopStyleButton();
            if (ret) {
                if (hasEquip) t->equipment &= ~bitMask;
                else           t->equipment |=  bitMask;
            }
            Tooltip(SohUtils::GetItemName(entry.id).c_str());
        }
        ImGui::PopID();
    }

    // Upgrade icons (Bullet Bag, Quiver, Bomb Bag, Scale, Strength) +
    // text combos (Wallet, Stick, Nuts). Categories per gUpgradeShifts:
    //  0=Quiver, 1=Bomb Bag, 2=Scale, 3=Strength, 4=Wallet, 5=Bullet Bag,
    //  6=Sticks, 7=Nuts. (See z_inventory.c — different order from vanilla
    //  editor's UPG_* macros.)
    const std::vector<uint8_t> bulletBagValues = { ITEM_NONE, ITEM_BULLET_BAG_30,
                                                    ITEM_BULLET_BAG_40, ITEM_BULLET_BAG_50 };
    DrawUpgradeIconForTemplate(t, "Bullet Bag", UPG_BULLET_BAG, bulletBagValues);
    ImGui::SameLine();
    const std::vector<uint8_t> quiverValues = { ITEM_NONE, ITEM_QUIVER_30,
                                                ITEM_QUIVER_40, ITEM_QUIVER_50 };
    DrawUpgradeIconForTemplate(t, "Quiver", UPG_QUIVER, quiverValues);
    ImGui::SameLine();
    const std::vector<uint8_t> bombBagValues = { ITEM_NONE, ITEM_BOMB_BAG_20,
                                                  ITEM_BOMB_BAG_30, ITEM_BOMB_BAG_40 };
    DrawUpgradeIconForTemplate(t, "Bomb Bag", UPG_BOMB_BAG, bombBagValues);
    ImGui::SameLine();
    const std::vector<uint8_t> scaleValues = { ITEM_NONE, ITEM_SCALE_SILVER, ITEM_SCALE_GOLDEN };
    DrawUpgradeIconForTemplate(t, "Scale", UPG_SCALE, scaleValues);
    ImGui::SameLine();
    const std::vector<uint8_t> strengthValues = { ITEM_NONE, ITEM_BRACELET,
                                                   ITEM_GAUNTLETS_SILVER, ITEM_GAUNTLETS_GOLD };
    DrawUpgradeIconForTemplate(t, "Strength", UPG_STRENGTH, strengthValues);

    const std::vector<std::string> walletNames = { "Child (99)", "Adult (200)",
                                                    "Giant (500)", "Tycoon (999)" };
    DrawUpgradeComboForTemplate(t, "Wallet", UPG_WALLET, walletNames);
    const std::vector<std::string> stickNames = { "None", "10", "20", "30" };
    DrawUpgradeComboForTemplate(t, "Deku Stick Capacity", UPG_STICKS, stickNames);
    const std::vector<std::string> nutNames = { "None", "20", "30", "40" };
    DrawUpgradeComboForTemplate(t, "Deku Nut Capacity", UPG_NUTS, nutNames);

    // Bombchu Bag capacity (rando-only field, but always-rendered here so
    // GM can adjust if peer is rando).
    ImGui::Spacing();
    ImGui::Separator();
    const std::vector<std::string> bombchuNames = { "None", "20", "30", "50" };
    ImGui::Text("%s", "Bombchu Bag Capacity");
    ImGui::SameLine();
    ImGui::PushID("Bombchu Bag Capacity R");
    PushStyleCombobox(THEME_COLOR);
    ImGui::AlignTextToFramePadding();
    auto value = t->bombchuUpgradeLevel;
    auto bcname = value < bombchuNames.size() ? bombchuNames[value].c_str() : "Glitched";
    if (ImGui::BeginCombo("##upgradeRb", bcname)) {
        for (size_t i = 0; i < bombchuNames.size(); i++) {
            if (ImGui::Selectable(bombchuNames[i].c_str())) {
                t->bombchuUpgradeLevel = (uint8_t)i;
            }
        }
        ImGui::EndCombo();
    }
    PopStyleCombobox();
    ImGui::PopID();
    Tooltip("Bombchu Bag Capacity (rando)");
}

// ----------------------------------------------------------------------------
// Quest Status tab — 1:1 mirror.
// ----------------------------------------------------------------------------

void DrawQuestItemButtonForTemplate(HarpoonTemplates::Template* t, uint32_t item) {
    auto it = questMapping.find(item);
    if (it == questMapping.end()) return;
    const QuestMapEntry& entry = it->second;
    uint32_t bitMask = 1u << entry.id;
    bool hasQuestItem = (bitMask & t->questItems) != 0;
    PushStyleButton(Colors::DarkGray);
    bool ret = ImGui::ImageButton(
        (entry.name + "##qR").c_str(),
        Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName(
            hasQuestItem ? entry.name : entry.nameFaded),
        ImVec2(kInvImageSize, kInvImageSize), ImVec2(0, 0), ImVec2(1, 1));
    if (ret) {
        if (hasQuestItem) t->questItems &= ~bitMask;
        else               t->questItems |=  bitMask;
    }
    PopStyleButton();
    Tooltip(SohUtils::GetQuestItemName(entry.id).c_str());
}

void DrawDungeonItemButtonForTemplate(HarpoonTemplates::Template* t,
                                       uint32_t item, uint32_t scene) {
    auto it = itemMapping.find(item);
    if (it == itemMapping.end()) return;
    const ItemMapEntry& entry = it->second;
    uint32_t bitMask = 1u << (entry.id - ITEM_KEY_BOSS);
    bool hasItem = (bitMask & t->dungeonItems[scene]) != 0;
    PushStyleButton(Colors::DarkGray);
    bool ret = ImGui::ImageButton(
        (entry.name + "##dR").c_str(),
        Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName(
            hasItem ? entry.name : entry.nameFaded),
        ImVec2(kInvImageSize, kInvImageSize), ImVec2(0, 0), ImVec2(1, 1));
    if (ret) {
        if (hasItem) t->dungeonItems[scene] &= ~bitMask;
        else          t->dungeonItems[scene] |=  bitMask;
    }
    PopStyleButton();
    Tooltip(SohUtils::GetItemName(entry.id).c_str());
}

void DrawQuestStatusTab(HarpoonTemplates::Template* t) {
    for (int32_t i = QUEST_MEDALLION_FOREST; i < QUEST_MEDALLION_LIGHT + 1; i++) {
        if (i != QUEST_MEDALLION_FOREST) ImGui::SameLine();
        DrawQuestItemButtonForTemplate(t, i);
    }
    for (int32_t i = QUEST_KOKIRI_EMERALD; i < QUEST_ZORA_SAPPHIRE + 1; i++) {
        if (i != QUEST_KOKIRI_EMERALD) ImGui::SameLine();
        DrawQuestItemButtonForTemplate(t, i);
    }
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(kInvImageSize, kInvImageSize) + ImGui::GetStyle().FramePadding * 2);
    ImGui::SameLine();
    DrawQuestItemButtonForTemplate(t, QUEST_STONE_OF_AGONY);
    ImGui::SameLine();
    DrawQuestItemButtonForTemplate(t, QUEST_GERUDO_CARD);

    for (const auto& [quest, entry] : songMapping) {
        if (entry.id != QUEST_SONG_MINUET && entry.id != QUEST_SONG_LULLABY) {
            ImGui::SameLine();
        }
        uint32_t bitMask = 1u << entry.id;
        bool hasQuestItem = (bitMask & t->questItems) != 0;
        PushStyleButton(Colors::DarkGray);
        bool ret = ImGui::ImageButton(
            (entry.name + "##qsR").c_str(),
            Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName(
                hasQuestItem ? entry.name : entry.nameFaded),
            ImVec2(32.0f, 48.0f), ImVec2(0, 0), ImVec2(1, 1));
        PopStyleButton();
        if (ret) {
            if (hasQuestItem) t->questItems &= ~bitMask;
            else               t->questItems |=  bitMask;
        }
        Tooltip(SohUtils::GetQuestItemName(entry.id).c_str());
    }

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("GS Count", ImGuiDataType_S16, &t->gsTokens);
    PopStyleInput();
    InsertHelpHoverText("Number of gold skulltula tokens acquired");

    uint32_t skullBit = 1u << QUEST_SKULL_TOKEN;
    bool gsUnlocked = (skullBit & t->questItems) != 0;
    if (Checkbox("GS unlocked", &gsUnlocked, CheckboxOptions().Color(THEME_COLOR))) {
        if (gsUnlocked) t->questItems |=  skullBit;
        else             t->questItems &= ~skullBit;
    }
    InsertHelpHoverText("If unlocked, enables showing the gold skulltula count in the quest status menu");

    int32_t pohCount = (t->questItems & 0xF0000000) >> 28;
    PushStyleCombobox(THEME_COLOR);
    if (ImGui::BeginCombo("PoH count##R", std::to_string(pohCount).c_str())) {
        for (int32_t i = 0; i < 4; i++) {
            if (ImGui::Selectable(std::to_string(i).c_str(), pohCount == i)) {
                t->questItems &= ~0xF0000000;
                t->questItems |= (uint32_t)(i << 28);
            }
        }
        ImGui::EndCombo();
    }
    PopStyleCombobox();
    InsertHelpHoverText("The number of pieces of heart acquired towards the next heart container");

    DrawGroupWithBorder([&]() {
        ImGui::Text("Dungeon Items");
        static int32_t dungeonItemsScene = SCENE_DEKU_TREE;
        PushStyleCombobox(THEME_COLOR);
        if (ImGui::BeginCombo("##DungeonSelectR", SohUtils::GetSceneName(dungeonItemsScene).c_str())) {
            for (int32_t di = SCENE_DEKU_TREE; di < SCENE_JABU_JABU_BOSS + 1; di++) {
                if (ImGui::Selectable(SohUtils::GetSceneName(di).c_str(), di == dungeonItemsScene)) {
                    dungeonItemsScene = di;
                }
            }
            ImGui::EndCombo();
        }
        PopStyleCombobox();

        DrawDungeonItemButtonForTemplate(t, ITEM_KEY_BOSS, dungeonItemsScene);
        ImGui::SameLine();
        DrawDungeonItemButtonForTemplate(t, ITEM_COMPASS, dungeonItemsScene);
        ImGui::SameLine();
        DrawDungeonItemButtonForTemplate(t, ITEM_DUNGEON_MAP, dungeonItemsScene);

        if (dungeonItemsScene != SCENE_JABU_JABU_BOSS &&
            dungeonItemsScene < (int32_t)ARRAY_COUNT(t->dungeonKeys)) {
            float lineHeight = ImGui::GetTextLineHeightWithSpacing();
            auto keyMap = itemMapping.find(ITEM_KEY_SMALL);
            if (keyMap != itemMapping.end()) {
                ImGui::Image(Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName(keyMap->second.name),
                             ImVec2(lineHeight, lineHeight));
            }
            ImGui::SameLine();
            PushStyleInput(THEME_COLOR);
            ImGui::InputScalar("##KeysR", ImGuiDataType_S8, &t->dungeonKeys[dungeonItemsScene]);
            PopStyleInput();
        } else {
            ImGui::Text("Barinade's Lair does not have small keys");
        }
    }, "RDungeonItems");
}

// ----------------------------------------------------------------------------
// Header (peer combo + actions)
// ----------------------------------------------------------------------------

void DrawHeader() {
    auto* harpoon = Harpoon::Instance;

    std::string current = sTargetCid == 0 ? "(none)" : PeerLabel(sTargetCid);
    ImGui::Text("Peer:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    PushStyleCombobox(THEME_COLOR);
    if (ImGui::BeginCombo("##remotePeer", current.c_str())) {
        if (harpoon != nullptr) {
            for (auto& [cid, c] : harpoon->clients) {
                if (cid == harpoon->ownClientId || !c.online) continue;
                std::string lbl = c.name.empty() ? ("cid" + std::to_string(cid)) : c.name;
                lbl += "  [cid" + std::to_string(cid) + "]";
                if (ImGui::Selectable(lbl.c_str(), cid == sTargetCid)) {
                    sTargetCid = cid;
                    RequestPeek(cid);
                }
            }
        }
        ImGui::EndCombo();
    }
    PopStyleCombobox();

    ImGui::SameLine();
    if (ImGui::Button("Refresh") && sTargetCid != 0) RequestPeek(sTargetCid);
    ImGui::SameLine();

    HarpoonTemplates::Template* snap = GetActiveSnapshot();
    ImGui::BeginDisabled(snap == nullptr);
    if (ImGui::Button("Apply ▸ peer") && snap != nullptr && harpoon != nullptr) {
        harpoon->SendJsonToRemote(
            HarpoonTemplates::BuildTemplateApplyPayload(sTargetCid, *snap));
        SPDLOG_INFO("[Harpoon][RemoteEdit] applied edited snapshot to cid={}", sTargetCid);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    PushStyleInput(THEME_COLOR);
    ImGui::InputTextWithHint("##saveAsTpl", "template name", sSaveAsName, IM_ARRAYSIZE(sSaveAsName));
    PopStyleInput();
    ImGui::SameLine();
    ImGui::BeginDisabled(snap == nullptr || sSaveAsName[0] == '\0');
    if (ImGui::Button("Save as template")) {
        HarpoonTemplates::SaveAsTemplate(sSaveAsName, *snap);
        sSaveAsName[0] = '\0';
    }
    ImGui::EndDisabled();

    if (sTargetCid != 0) {
        float age = SnapshotAgeSeconds(sTargetCid);
        if (age < 0.0f) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                                "Snapshot: pending… (press Refresh)");
        } else {
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f),
                                "Snapshot age: %.1fs", age);
        }
    }
    ImGui::Separator();
}

void ResetBaseOptions() {}  // matches vanilla's no-op pattern between tabs

}  // anon namespace

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------

void OpenForPeer(uint32_t targetClientId) {
    sTargetCid = targetClientId;
    RequestPeek(targetClientId);
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    if (gui != nullptr) {
        auto w = gui->GetGuiWindow("Remote Save Editor");
        if (w != nullptr) w->Show();
    }
}

void RequestPeek(uint32_t targetClientId) {
    if (Harpoon::Instance == nullptr) return;
    if (!IsHostLocal()) return;
    Harpoon::Instance->SendJsonToRemote(BuildPeekRequestPayload(targetClientId));
}

nlohmann::json BuildPeekRequestPayload(uint32_t targetClientId) {
    nlohmann::json p;
    p["type"]       = "ROOM.BROADCAST_EVENT";
    p["event_name"] = "HARPOON.SAVE_PEEK_REQUEST";
    nlohmann::json d;
    d["targetClientId"]    = targetClientId;
    d["requesterClientId"] = Harpoon::Instance != nullptr
                                ? Harpoon::Instance->ownClientId : 0u;
    p["data"] = d;
    return p;
}

nlohmann::json BuildPeekResponsePayload(uint32_t requesterClientId) {
    HarpoonTemplates::Template t{};
    HarpoonTemplates::CaptureLocalState(t);
    nlohmann::json p;
    p["type"]       = "ROOM.BROADCAST_EVENT";
    p["event_name"] = "HARPOON.SAVE_PEEK_RESPONSE";
    nlohmann::json d = HarpoonTemplates::SerializeTemplate(t);
    d["ownerClientId"]     = Harpoon::Instance != nullptr
                                ? Harpoon::Instance->ownClientId : 0u;
    d["requesterClientId"] = requesterClientId;
    p["data"] = d;
    return p;
}

void HandlePeekRequest(const nlohmann::json& data) {
    if (Harpoon::Instance == nullptr) return;
    uint32_t target = data.value("targetClientId", 0u);
    uint32_t reqCid = data.value("requesterClientId", 0u);
    if (target != Harpoon::Instance->ownClientId) return;
    if (reqCid == 0 || reqCid != Harpoon::Instance->hostClientId) {
        SPDLOG_WARN("[Harpoon][RemoteEdit] peek request from non-host cid={} ignored",
                    reqCid);
        return;
    }
    Harpoon::Instance->SendJsonToRemote(BuildPeekResponsePayload(reqCid));
}

void HandlePeekResponse(const nlohmann::json& data) {
    if (Harpoon::Instance == nullptr) return;
    uint32_t requester = data.value("requesterClientId", 0u);
    uint32_t owner     = data.value("ownerClientId", 0u);
    if (requester != Harpoon::Instance->ownClientId) return;
    if (owner == 0) return;
    sSnapshots[owner]    = HarpoonTemplates::DeserializeTemplate(data);
    sSnapshotTime[owner] = std::chrono::steady_clock::now();
    SPDLOG_INFO("[Harpoon][RemoteEdit] cached snapshot for cid={}", owner);
}

// ----------------------------------------------------------------------------
// Window class
// ----------------------------------------------------------------------------

void RemoteSaveEditorWindow::InitElement() {
    // Icons are registered by the vanilla SaveEditorWindow + RegisterImGuiItemIcons.
}

void RemoteSaveEditorWindow::DrawElement() {
    PushStyleTabs(THEME_COLOR);
    ImGui::PushFont(OTRGlobals::Instance->fontMonoLarger);

    if (!IsHostLocal()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                            "The Remote Save Editor is host-only.");
        ImGui::TextWrapped(
            "Only the room host (GM) can peek and apply peer save state. "
            "If you should be GM, perform a host transfer from the Harpoon menu.");
        ImGui::PopFont();
        PopStyleTabs();
        return;
    }

    DrawHeader();

    HarpoonTemplates::Template* snap = GetActiveSnapshot();
    if (snap == nullptr) {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                            "Select a peer to load their save snapshot.");
        ImGui::PopFont();
        PopStyleTabs();
        return;
    }

    if (ImGui::BeginTabBar("RemoteSaveContextTabBar",
                          ImGuiTabBarFlags_NoCloseWithMiddleMouseButton)) {
        ResetBaseOptions();
        if (ImGui::BeginTabItem("Info")) {
            DrawInfoTab(snap);
            ImGui::EndTabItem();
        }
        ResetBaseOptions();
        if (ImGui::BeginTabItem("Inventory")) {
            DrawInventoryTab(snap);
            ImGui::EndTabItem();
        }
        ResetBaseOptions();
        if (ImGui::BeginTabItem("Flags")) {
            DrawFlagsTab(snap);
            ImGui::EndTabItem();
        }
        ResetBaseOptions();
        if (ImGui::BeginTabItem("Equipment")) {
            DrawEquipmentTab(snap);
            ImGui::EndTabItem();
        }
        ResetBaseOptions();
        if (ImGui::BeginTabItem("Quest Status")) {
            DrawQuestStatusTab(snap);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::PopFont();
    PopStyleTabs();
}

}  // namespace HarpoonRemoteSaveEditor
