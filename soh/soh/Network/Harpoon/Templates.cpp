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
    o["name"]   = t.name;
    o["items"]  = nlohmann::json::array();
    for (auto v : t.items) o["items"].push_back((int)v);
    o["ammo"]   = nlohmann::json::array();
    for (auto v : t.ammo) o["ammo"].push_back((int)v);
    o["equipment"]      = t.equipment;
    o["upgrades"]       = t.upgrades;
    o["rupees"]         = (int)t.rupees;
    o["magic"]          = (int)t.magic;
    o["magicCapacity"]  = (int)t.magicCapacity;
    o["healthCapacity"] = (int)t.healthCapacity;
    o["questItems"]     = t.questItems;
    o["dungeonItems"]   = nlohmann::json::array();
    for (auto v : t.dungeonItems) o["dungeonItems"].push_back((int)v);
    o["restrictNoClimb"] = t.restrictNoClimb;
    o["restrictNoGrab"]  = t.restrictNoGrab;
    o["restrictNoCrawl"] = t.restrictNoCrawl;
    o["restrictNoTalk"]  = t.restrictNoTalk;
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
    t.restrictNoClimb = o.value("restrictNoClimb", false);
    t.restrictNoGrab  = o.value("restrictNoGrab",  false);
    t.restrictNoCrawl = o.value("restrictNoCrawl", false);
    t.restrictNoTalk  = o.value("restrictNoTalk",  false);
    return t;
}

}  // anon

const std::vector<Template>& All() { return sTemplates; }

const Template* Find(const std::string& name) {
    for (const auto& t : sTemplates) if (t.name == name) return &t;
    return nullptr;
}

bool SnapshotLocal(const std::string& name) {
    if (name.empty()) return false;
    Template t{};
    t.name = name;
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
    // Overwrite existing template of same name.
    for (auto& existing : sTemplates) {
        if (existing.name == name) { existing = t; return SaveAll(); }
    }
    sTemplates.push_back(t);
    return SaveAll();
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
