// =============================================================================
// HarpoonTemplates — GM-side player-state templates.
//
// The host snapshots their own gSaveContext into a named template, saves to
// soh/harpoon/templates/<name>.json, then applies it to a peer via a
// HARPOON.TEMPLATE_APPLY broadcast. The peer overwrites their local save
// state with the template's values (full overwrite, not merge).
// =============================================================================

#include "Templates.h"
#include "Harpoon.h"

#include <spdlog/spdlog.h>
#include <fstream>
#include <filesystem>

#include "libultraship/libultraship.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
}

namespace HarpoonTemplates {

namespace {
std::vector<Template> sTemplates;

std::filesystem::path ResolveTemplatesDir() {
    std::string root = Ship::Context::LocateFileAcrossAppDirs("harpoon", "soh");
    if (root.empty()) {
        // Fallback: ./harpoon/templates relative to cwd.
        return std::filesystem::path("harpoon") / "templates";
    }
    return std::filesystem::path(root) / "templates";
}

nlohmann::json TemplateToJson(const Template& t) {
    nlohmann::json o;
    o["name"] = t.name;

    // Inventory.
    o["items"] = nlohmann::json::array();
    for (auto v : t.items) o["items"].push_back((int)v);
    o["ammo"]  = nlohmann::json::array();
    for (auto v : t.ammo) o["ammo"].push_back((int)v);

    o["equipment"]      = t.equipment;
    o["upgrades"]       = t.upgrades;

    o["rupees"]         = (int)t.rupees;
    o["magic"]          = (int)t.magic;
    o["magicCapacity"]  = (int)t.magicCapacity;
    o["healthCapacity"] = (int)t.healthCapacity;

    o["questItems"]   = t.questItems;
    o["dungeonItems"] = nlohmann::json::array();
    for (auto v : t.dungeonItems) o["dungeonItems"].push_back((int)v);
    o["dungeonKeys"] = nlohmann::json::array();
    for (auto v : t.dungeonKeys)  o["dungeonKeys"].push_back((int)v);
    o["gsTokens"] = (int)t.gsTokens;

    o["restrictNoClimb"] = t.restrictNoClimb;
    o["restrictNoGrab"]  = t.restrictNoGrab;
    o["restrictNoCrawl"] = t.restrictNoCrawl;
    o["restrictNoTalk"]  = t.restrictNoTalk;

    // Progression flag arrays.
    o["eventChkInf"] = nlohmann::json::array();
    for (auto v : t.eventChkInf) o["eventChkInf"].push_back((int)v);
    o["itemGetInf"]  = nlohmann::json::array();
    for (auto v : t.itemGetInf)  o["itemGetInf"].push_back((int)v);
    o["infTable"]    = nlohmann::json::array();
    for (auto v : t.infTable)    o["infTable"].push_back((int)v);
    o["eventInf"]    = nlohmann::json::array();
    for (auto v : t.eventInf)    o["eventInf"].push_back((int)v);
    o["randomizerInf"] = nlohmann::json::array();
    for (auto v : t.randomizerInf) o["randomizerInf"].push_back((int)v);

    // File metadata.
    o["fileNum"]          = (int)t.fileNum;
    o["playerName"]       = nlohmann::json::array();
    for (auto v : t.playerName) o["playerName"].push_back((int)v);
    o["filenameLanguage"] = (int)t.filenameLanguage;

    // Defense / magic.
    o["isDoubleDefenseAcquired"] = (int)t.isDoubleDefenseAcquired;
    o["defenseHearts"]           = (int)t.defenseHearts;
    o["magicLevel"]              = (int)t.magicLevel;
    o["isMagicAcquired"]         = (int)t.isMagicAcquired;
    o["isDoubleMagicAcquired"]   = (int)t.isDoubleMagicAcquired;

    // Time / counters.
    o["dayTime"]     = (int)t.dayTime;
    o["totalDays"]   = (int)t.totalDays;
    o["deaths"]      = (int)t.deaths;
    o["bgsFlag"]     = (int)t.bgsFlag;
    o["swordHealth"] = (int)t.swordHealth;
    o["bgsDayCount"] = (int)t.bgsDayCount;

    // Entrance / scene state.
    o["entranceIndex"] = (int)t.entranceIndex;
    o["cutsceneIndex"] = (int)t.cutsceneIndex;

    // Timers.
    o["naviTimer"]       = (int)t.naviTimer;
    o["timerState"]      = (int)t.timerState;
    o["timerSeconds"]    = (int)t.timerSeconds;
    o["subTimerState"]   = (int)t.subTimerState;
    o["subTimerSeconds"] = (int)t.subTimerSeconds;

    // Settings.
    o["audioSetting"]   = (int)t.audioSetting;
    o["n64ddFlag"]      = (int)t.n64ddFlag;
    o["zTargetSetting"] = (int)t.zTargetSetting;

    // Randomizer-specific scalars.
    o["triforcePiecesCollected"] = (int)t.triforcePiecesCollected;
    o["bombchuUpgradeLevel"]     = (int)t.bombchuUpgradeLevel;

    // High scores + gsFlags.
    o["highScores"] = nlohmann::json::array();
    for (auto v : t.highScores) o["highScores"].push_back((int)v);
    o["gsFlags"]    = nlohmann::json::array();
    for (auto v : t.gsFlags)    o["gsFlags"].push_back((int)v);

    // Scene flags array — 124 scenes × 7 u32. Flattened to a single
    // 868-element array (saves nested-object overhead in JSON).
    o["sceneFlags"] = nlohmann::json::array();
    for (size_t i = 0; i < ARRAY_COUNT(t.sceneFlags); i++) {
        const auto& s = t.sceneFlags[i];
        o["sceneFlags"].push_back(s.chest);
        o["sceneFlags"].push_back(s.swch);
        o["sceneFlags"].push_back(s.clear);
        o["sceneFlags"].push_back(s.collect);
        o["sceneFlags"].push_back(s.unk);
        o["sceneFlags"].push_back(s.rooms);
        o["sceneFlags"].push_back(s.floors);
    }

    return o;
}

Template TemplateFromJson(const nlohmann::json& o) {
    Template t{};
    t.name = o.value("name", std::string());

    if (o.contains("items") && o["items"].is_array()) {
        for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.items) && i < o["items"].size(); i++) {
            t.items[i] = (uint8_t)o["items"][i].get<int>();
        }
    }
    if (o.contains("ammo") && o["ammo"].is_array()) {
        for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.ammo) && i < o["ammo"].size(); i++) {
            t.ammo[i] = (int8_t)o["ammo"][i].get<int>();
        }
    }
    t.equipment      = o.value("equipment",      0u);
    t.upgrades       = o.value("upgrades",       0u);
    t.rupees         = (int16_t)o.value("rupees",         0);
    t.magic          = (int16_t)o.value("magic",          0);
    t.magicCapacity  = (int16_t)o.value("magicCapacity",  0);
    t.healthCapacity = (int16_t)o.value("healthCapacity", 16 * 3);
    t.questItems     = o.value("questItems",     0u);
    if (o.contains("dungeonItems") && o["dungeonItems"].is_array()) {
        for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.dungeonItems) && i < o["dungeonItems"].size(); i++) {
            t.dungeonItems[i] = (uint8_t)o["dungeonItems"][i].get<int>();
        }
    }
    if (o.contains("dungeonKeys") && o["dungeonKeys"].is_array()) {
        for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.dungeonKeys) && i < o["dungeonKeys"].size(); i++) {
            t.dungeonKeys[i] = (int8_t)o["dungeonKeys"][i].get<int>();
        }
    }
    t.gsTokens = (int16_t)o.value("gsTokens", 0);

    t.restrictNoClimb = o.value("restrictNoClimb", false);
    t.restrictNoGrab  = o.value("restrictNoGrab",  false);
    t.restrictNoCrawl = o.value("restrictNoCrawl", false);
    t.restrictNoTalk  = o.value("restrictNoTalk",  false);

    if (o.contains("eventChkInf") && o["eventChkInf"].is_array()) {
        for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.eventChkInf) && i < o["eventChkInf"].size(); i++) {
            t.eventChkInf[i] = (uint16_t)o["eventChkInf"][i].get<int>();
        }
    }
    if (o.contains("itemGetInf") && o["itemGetInf"].is_array()) {
        for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.itemGetInf) && i < o["itemGetInf"].size(); i++) {
            t.itemGetInf[i] = (uint16_t)o["itemGetInf"][i].get<int>();
        }
    }
    if (o.contains("infTable") && o["infTable"].is_array()) {
        for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.infTable) && i < o["infTable"].size(); i++) {
            t.infTable[i] = (uint16_t)o["infTable"][i].get<int>();
        }
    }
    if (o.contains("eventInf") && o["eventInf"].is_array()) {
        for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.eventInf) && i < o["eventInf"].size(); i++) {
            t.eventInf[i] = (uint16_t)o["eventInf"][i].get<int>();
        }
    }
    if (o.contains("randomizerInf") && o["randomizerInf"].is_array()) {
        t.randomizerInf.reserve(o["randomizerInf"].size());
        for (const auto& v : o["randomizerInf"]) {
            t.randomizerInf.push_back((uint16_t)v.get<int>());
        }
    }

    // File metadata.
    t.fileNum          = (int32_t)o.value("fileNum", 0);
    if (o.contains("playerName") && o["playerName"].is_array()) {
        for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.playerName) && i < o["playerName"].size(); i++) {
            t.playerName[i] = (uint8_t)o["playerName"][i].get<int>();
        }
    }
    t.filenameLanguage = (uint8_t)o.value("filenameLanguage", 0);

    // Defense / magic.
    t.isDoubleDefenseAcquired = (uint8_t)o.value("isDoubleDefenseAcquired", 0);
    t.defenseHearts           = (int16_t)o.value("defenseHearts", 0);
    t.magicLevel              = (int8_t)o.value("magicLevel", 0);
    t.isMagicAcquired         = (uint8_t)o.value("isMagicAcquired", 0);
    t.isDoubleMagicAcquired   = (uint8_t)o.value("isDoubleMagicAcquired", 0);

    // Time / counters.
    t.dayTime     = (uint16_t)o.value("dayTime", 0);
    t.totalDays   = (int32_t)o.value("totalDays", 0);
    t.deaths      = (uint16_t)o.value("deaths", 0);
    t.bgsFlag     = (uint8_t)o.value("bgsFlag", 0);
    t.swordHealth = (uint16_t)o.value("swordHealth", 0);
    t.bgsDayCount = (int32_t)o.value("bgsDayCount", 0);

    t.entranceIndex = (int32_t)o.value("entranceIndex", 0);
    t.cutsceneIndex = (int32_t)o.value("cutsceneIndex", 0);

    t.naviTimer       = (uint16_t)o.value("naviTimer", 0);
    t.timerState      = (int16_t)o.value("timerState", 0);
    t.timerSeconds    = (int16_t)o.value("timerSeconds", 0);
    t.subTimerState   = (int16_t)o.value("subTimerState", 0);
    t.subTimerSeconds = (int16_t)o.value("subTimerSeconds", 0);

    t.audioSetting   = (uint8_t)o.value("audioSetting", 0);
    t.n64ddFlag      = (int16_t)o.value("n64ddFlag", 0);
    t.zTargetSetting = (uint8_t)o.value("zTargetSetting", 0);

    t.triforcePiecesCollected = (uint8_t)o.value("triforcePiecesCollected", 0);
    t.bombchuUpgradeLevel     = (uint8_t)o.value("bombchuUpgradeLevel", 0);

    if (o.contains("highScores") && o["highScores"].is_array()) {
        for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.highScores) && i < o["highScores"].size(); i++) {
            t.highScores[i] = (int32_t)o["highScores"][i].get<int>();
        }
    }
    if (o.contains("gsFlags") && o["gsFlags"].is_array()) {
        for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.gsFlags) && i < o["gsFlags"].size(); i++) {
            t.gsFlags[i] = (int32_t)o["gsFlags"][i].get<int>();
        }
    }
    if (o.contains("sceneFlags") && o["sceneFlags"].is_array()) {
        const auto& arr = o["sceneFlags"];
        // Flattened 7-per-scene encoding (see TemplateToJson).
        for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.sceneFlags); i++) {
            const size_t base = i * 7;
            if (base + 6 >= arr.size()) break;
            t.sceneFlags[i].chest   = arr[base + 0].get<uint32_t>();
            t.sceneFlags[i].swch    = arr[base + 1].get<uint32_t>();
            t.sceneFlags[i].clear   = arr[base + 2].get<uint32_t>();
            t.sceneFlags[i].collect = arr[base + 3].get<uint32_t>();
            t.sceneFlags[i].unk     = arr[base + 4].get<uint32_t>();
            t.sceneFlags[i].rooms   = arr[base + 5].get<uint32_t>();
            t.sceneFlags[i].floors  = arr[base + 6].get<uint32_t>();
        }
    }

    return t;
}

}  // anon

const std::vector<Template>& All() { return sTemplates; }

const Template* Find(const std::string& name) {
    for (const auto& t : sTemplates) if (t.name == name) return &t;
    return nullptr;
}

void CaptureLocalState(Template& t) {
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.items); i++) {
        t.items[i] = gSaveContext.inventory.items[i];
    }
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.ammo); i++) {
        t.ammo[i] = gSaveContext.inventory.ammo[i];
    }
    t.equipment      = gSaveContext.inventory.equipment;
    t.upgrades       = gSaveContext.inventory.upgrades;
    t.rupees         = gSaveContext.rupees;
    t.magic          = gSaveContext.magic;
    t.magicCapacity  = gSaveContext.magicCapacity;
    t.healthCapacity = gSaveContext.healthCapacity;
    t.questItems     = gSaveContext.inventory.questItems;
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.dungeonItems); i++) {
        t.dungeonItems[i] = gSaveContext.inventory.dungeonItems[i];
    }
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.dungeonKeys); i++) {
        t.dungeonKeys[i] = gSaveContext.inventory.dungeonKeys[i];
    }
    t.gsTokens = gSaveContext.inventory.gsTokens;

    // Progression flag arrays.
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.eventChkInf); i++) {
        t.eventChkInf[i] = gSaveContext.eventChkInf[i];
    }
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.itemGetInf); i++) {
        t.itemGetInf[i] = gSaveContext.itemGetInf[i];
    }
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.infTable); i++) {
        t.infTable[i] = gSaveContext.infTable[i];
    }
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.eventInf); i++) {
        t.eventInf[i] = gSaveContext.eventInf[i];
    }
    constexpr size_t kRandInfLen = ARRAY_COUNT(gSaveContext.ship.randomizerInf);
    t.randomizerInf.assign(gSaveContext.ship.randomizerInf,
                            gSaveContext.ship.randomizerInf + kRandInfLen);

    // File metadata.
    t.fileNum = gSaveContext.fileNum;
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.playerName); i++) {
        t.playerName[i] = gSaveContext.playerName[i];
    }
    t.filenameLanguage = gSaveContext.ship.filenameLanguage;

    // Defense / magic acquisition.
    t.isDoubleDefenseAcquired = gSaveContext.isDoubleDefenseAcquired;
    t.defenseHearts           = gSaveContext.inventory.defenseHearts;
    t.magicLevel              = gSaveContext.magicLevel;
    t.isMagicAcquired         = gSaveContext.isMagicAcquired;
    t.isDoubleMagicAcquired   = gSaveContext.isDoubleMagicAcquired;

    // Time / counters.
    t.dayTime     = gSaveContext.dayTime;
    t.totalDays   = gSaveContext.totalDays;
    t.deaths      = gSaveContext.deaths;
    t.bgsFlag     = gSaveContext.bgsFlag;
    t.swordHealth = gSaveContext.swordHealth;
    t.bgsDayCount = gSaveContext.bgsDayCount;

    t.entranceIndex = gSaveContext.entranceIndex;
    t.cutsceneIndex = gSaveContext.cutsceneIndex;

    t.naviTimer       = gSaveContext.naviTimer;
    t.timerState      = gSaveContext.timerState;
    t.timerSeconds    = gSaveContext.timerSeconds;
    t.subTimerState   = gSaveContext.subTimerState;
    t.subTimerSeconds = gSaveContext.subTimerSeconds;

    t.audioSetting   = gSaveContext.audioSetting;
    t.n64ddFlag      = gSaveContext.n64ddFlag;
    t.zTargetSetting = gSaveContext.zTargetSetting;

    t.triforcePiecesCollected = gSaveContext.ship.quest.data.randomizer.triforcePiecesCollected;
    t.bombchuUpgradeLevel     = gSaveContext.ship.quest.data.randomizer.bombchuUpgradeLevel;

    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.highScores); i++) {
        t.highScores[i] = gSaveContext.highScores[i];
    }
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.gsFlags); i++) {
        t.gsFlags[i] = gSaveContext.gsFlags[i];
    }

    // Scene flags.
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.sceneFlags); i++) {
        const auto& s = gSaveContext.sceneFlags[i];
        t.sceneFlags[i].chest   = s.chest;
        t.sceneFlags[i].swch    = s.swch;
        t.sceneFlags[i].clear   = s.clear;
        t.sceneFlags[i].collect = s.collect;
        t.sceneFlags[i].unk     = s.unk;
        t.sceneFlags[i].rooms   = s.rooms;
        t.sceneFlags[i].floors  = s.floors;
    }

    // Local restrict flags — read off our own HarpoonClient entry.
    if (Harpoon::Instance != nullptr) {
        auto it = Harpoon::Instance->clients.find(Harpoon::Instance->ownClientId);
        if (it != Harpoon::Instance->clients.end()) {
            t.restrictNoClimb = it->second.restrictNoClimb;
            t.restrictNoGrab  = it->second.restrictNoGrab;
            t.restrictNoCrawl = it->second.restrictNoCrawl;
            t.restrictNoTalk  = it->second.restrictNoTalk;
        }
    }
}

bool SnapshotLocal(const std::string& name) {
    if (name.empty()) return false;
    Template t{};
    t.name = name;
    CaptureLocalState(t);
    // Overwrite existing template of same name.
    for (auto& existing : sTemplates) {
        if (existing.name == name) { existing = t; return SaveAll(); }
    }
    sTemplates.push_back(t);
    return SaveAll();
}

bool SaveAsTemplate(const std::string& name, Template src) {
    if (name.empty()) return false;
    src.name = name;
    for (auto& existing : sTemplates) {
        if (existing.name == name) { existing = src; return SaveAll(); }
    }
    sTemplates.push_back(src);
    return SaveAll();
}

nlohmann::json SerializeTemplate(const Template& t) {
    return TemplateToJson(t);
}

Template DeserializeTemplate(const nlohmann::json& j) {
    return TemplateFromJson(j);
}

bool SaveAll() {
    auto dir = ResolveTemplatesDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    bool allOk = true;
    for (const auto& t : sTemplates) {
        auto path = dir / (t.name + ".json");
        std::ofstream f(path);
        if (!f.is_open()) { allOk = false; continue; }
        f << TemplateToJson(t).dump(2);
    }
    return allOk;
}

void LoadAll() {
    sTemplates.clear();
    auto dir = ResolveTemplatesDir();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return;
    for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        std::ifstream f(entry.path());
        if (!f.is_open()) continue;
        nlohmann::json j;
        try {
            f >> j;
        } catch (...) { continue; }
        sTemplates.push_back(TemplateFromJson(j));
    }
    SPDLOG_INFO("[Harpoon][Templates] loaded {} templates from {}",
                (int)sTemplates.size(), dir.string());
}

bool Delete(const std::string& name) {
    bool removed = false;
    for (auto it = sTemplates.begin(); it != sTemplates.end(); ) {
        if (it->name == name) { it = sTemplates.erase(it); removed = true; }
        else ++it;
    }
    if (removed) {
        auto dir = ResolveTemplatesDir();
        std::error_code ec;
        std::filesystem::remove(dir / (name + ".json"), ec);
    }
    return removed;
}

// ----------------------------------------------------------------------------
// Apply paths
// ----------------------------------------------------------------------------

namespace {
void ApplyToSave(const Template& t) {
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.items); i++) {
        gSaveContext.inventory.items[i] = t.items[i];
    }
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.ammo); i++) {
        gSaveContext.inventory.ammo[i] = t.ammo[i];
    }
    gSaveContext.inventory.equipment    = t.equipment;
    gSaveContext.inventory.upgrades     = t.upgrades;
    gSaveContext.rupees                 = t.rupees;
    gSaveContext.magic                  = t.magic;
    gSaveContext.magicCapacity          = t.magicCapacity;
    gSaveContext.healthCapacity         = t.healthCapacity;
    gSaveContext.health                 = t.healthCapacity;  // refill on apply
    gSaveContext.inventory.questItems   = t.questItems;
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.dungeonItems); i++) {
        gSaveContext.inventory.dungeonItems[i] = t.dungeonItems[i];
    }
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.dungeonKeys); i++) {
        gSaveContext.inventory.dungeonKeys[i] = t.dungeonKeys[i];
    }
    gSaveContext.inventory.gsTokens = t.gsTokens;

    // Progression flag arrays — full overwrite.
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.eventChkInf); i++) {
        gSaveContext.eventChkInf[i] = t.eventChkInf[i];
    }
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.itemGetInf); i++) {
        gSaveContext.itemGetInf[i] = t.itemGetInf[i];
    }
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.infTable); i++) {
        gSaveContext.infTable[i] = t.infTable[i];
    }
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.eventInf); i++) {
        gSaveContext.eventInf[i] = t.eventInf[i];
    }
    {
        const size_t kRandInfLen = ARRAY_COUNT(gSaveContext.ship.randomizerInf);
        const size_t copyLen = std::min(kRandInfLen, t.randomizerInf.size());
        for (size_t i = 0; i < copyLen; i++) {
            gSaveContext.ship.randomizerInf[i] = t.randomizerInf[i];
        }
    }

    // File metadata.
    gSaveContext.fileNum = t.fileNum;
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.playerName); i++) {
        gSaveContext.playerName[i] = t.playerName[i];
    }
    gSaveContext.ship.filenameLanguage = t.filenameLanguage;

    // Defense / magic acquisition.
    gSaveContext.isDoubleDefenseAcquired = t.isDoubleDefenseAcquired;
    gSaveContext.inventory.defenseHearts = t.defenseHearts;
    gSaveContext.magicLevel              = t.magicLevel;
    gSaveContext.isMagicAcquired         = t.isMagicAcquired;
    gSaveContext.isDoubleMagicAcquired   = t.isDoubleMagicAcquired;

    // Time / counters.
    gSaveContext.dayTime     = t.dayTime;
    gSaveContext.totalDays   = t.totalDays;
    gSaveContext.deaths      = t.deaths;
    gSaveContext.bgsFlag     = t.bgsFlag;
    gSaveContext.swordHealth = t.swordHealth;
    gSaveContext.bgsDayCount = t.bgsDayCount;

    gSaveContext.entranceIndex = t.entranceIndex;
    gSaveContext.cutsceneIndex = t.cutsceneIndex;

    gSaveContext.naviTimer       = t.naviTimer;
    gSaveContext.timerState      = t.timerState;
    gSaveContext.timerSeconds    = t.timerSeconds;
    gSaveContext.subTimerState   = t.subTimerState;
    gSaveContext.subTimerSeconds = t.subTimerSeconds;

    gSaveContext.audioSetting   = t.audioSetting;
    gSaveContext.n64ddFlag      = t.n64ddFlag;
    gSaveContext.zTargetSetting = t.zTargetSetting;

    gSaveContext.ship.quest.data.randomizer.triforcePiecesCollected = t.triforcePiecesCollected;
    gSaveContext.ship.quest.data.randomizer.bombchuUpgradeLevel     = t.bombchuUpgradeLevel;

    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.highScores); i++) {
        gSaveContext.highScores[i] = t.highScores[i];
    }
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.gsFlags); i++) {
        gSaveContext.gsFlags[i] = t.gsFlags[i];
    }

    // Scene flags.
    for (size_t i = 0; i < (size_t)ARRAY_COUNT(t.sceneFlags); i++) {
        auto& s = gSaveContext.sceneFlags[i];
        const auto& src = t.sceneFlags[i];
        s.chest   = src.chest;
        s.swch    = src.swch;
        s.clear   = src.clear;
        s.collect = src.collect;
        s.unk     = src.unk;
        s.rooms   = src.rooms;
        s.floors  = src.floors;
    }

    if (Harpoon::Instance != nullptr) {
        auto it = Harpoon::Instance->clients.find(Harpoon::Instance->ownClientId);
        if (it != Harpoon::Instance->clients.end()) {
            it->second.restrictNoClimb = t.restrictNoClimb;
            it->second.restrictNoGrab  = t.restrictNoGrab;
            it->second.restrictNoCrawl = t.restrictNoCrawl;
            it->second.restrictNoTalk  = t.restrictNoTalk;
        }
    }
}
}  // anon

bool ApplyToLocal(const std::string& name) {
    const Template* t = Find(name);
    if (t == nullptr) return false;
    ApplyToSave(*t);
    return true;
}

nlohmann::json BuildTemplateApplyPayload(uint32_t targetClientId,
                                          const Template& t) {
    nlohmann::json p;
    p["type"]            = "ROOM.BROADCAST_EVENT";
    p["event_name"]      = "HARPOON.TEMPLATE_APPLY";
    nlohmann::json d     = TemplateToJson(t);
    d["targetClientId"]  = targetClientId;
    p["data"]            = d;
    return p;
}

bool ApplyToPeer(uint32_t targetClientId, const std::string& name) {
    const Template* t = Find(name);
    if (t == nullptr || Harpoon::Instance == nullptr) return false;
    Harpoon::Instance->SendJsonToRemote(BuildTemplateApplyPayload(targetClientId, *t));
    return true;
}

void HandleTemplateApply(const nlohmann::json& payload) {
    if (Harpoon::Instance == nullptr) return;
    uint32_t target = payload.value("targetClientId", 0u);
    if (target != Harpoon::Instance->ownClientId) return;
    Template t = TemplateFromJson(payload);
    ApplyToSave(t);
    SPDLOG_INFO("[Harpoon][Templates] applied template '{}' from host", t.name);
}

}  // namespace HarpoonTemplates
