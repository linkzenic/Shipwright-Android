#include "TriforceThief.h"
#include "../Harpoon.h"

#include <libultraship/libultraship.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <fstream>
#include <filesystem>

#include "soh/Notification/Notification.h"
// See PropHunt.cpp — same OPEN_DISPS / CLOSE_DISPS linkage workaround.
extern "C" {
    void FrameInterpolation_RecordOpenChild(const void* a, int b);
    void FrameInterpolation_RecordCloseChild(void);
}

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "mods/extended_inventory.h"
#include "soh_assets.h"
#include "objects/object_triforce_spot/object_triforce_spot.h"
#include "sequence.h"
extern PlayState* gPlayState;
extern GameState* gGameState;
}

#include "soh/SaveManager.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
extern "C" void Save_InitFile(int isDebug);

// Forward declaration — defined in soh/ResourceManagerHelpers.cpp. No public
// header exposes this prototype, so we declare it locally (same pattern as
// pak_loader.cpp and mods/items/logic/item_postman_hat.c).
extern "C" Gfx* ResourceMgr_LoadGfxByName(const char* path);

#include <cmath>

// =============================================================================
// File-scope storage
// =============================================================================

namespace {

std::vector<HarpoonTriforceThief::MapDef> sMaps;
HarpoonTriforceThief::LocalState         sLocal;
nlohmann::json                           sSavePresetRaw;
bool                                     sLoaded = false;
s16                                       sTriforceSpinAngle = 0;

std::string ResolvePackRoot() {
    std::string harpoonRoot = Ship::Context::LocateFileAcrossAppDirs("harpoon", "soh");
    std::error_code ec;
    if (harpoonRoot.empty()) {
        if (std::filesystem::exists(std::filesystem::path("harpoon"), ec)) {
            harpoonRoot = "harpoon";
        }
    }
    if (harpoonRoot.empty()) return {};
    auto packPath = std::filesystem::path(harpoonRoot) / "gamemodes" / "triforce_thief";
    if (!std::filesystem::exists(packPath, ec) || !std::filesystem::is_directory(packPath, ec)) {
        return {};
    }
    return packPath.string();
}

bool ReadJsonFile(const std::filesystem::path& path, nlohmann::json& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try { f >> out; }
    catch (const std::exception& e) {
        SPDLOG_WARN("[Harpoon][TriforceThief] failed to parse {}: {}", path.string(), e.what());
        return false;
    }
    return true;
}

// Maps in the order they appear in gamemode.yaml. Each entry must have a
// matching maps/<id>.json file in the pack.
struct MapInit {
    const char* id;
    const char* name;
    s32         entrance;
    const char* description;
};

constexpr MapInit kMapInit[HarpoonTriforceThief::kMapCount] = {
    { "hyrule_field",         "Hyrule Field",         205, "Wide open field — lots of room to run." },
    { "zora_river",           "Zora's River",         234, "Winding river with cliffs and waterfalls." },
    { "gerudo_fortress",      "Gerudo Fortress",      297, "Desert compound with rooftops and corridors." },
    { "kokiri_forest",        "Kokiri Forest",        238, "Peaceful village with bridges and trees." },
    { "kakariko_village",     "Kakariko Village",     219, "Mountain village with rooftops and alleys." },
    { "sacred_forest_meadow", "Sacred Forest Meadow", 252, "Forest maze with open clearings." },
};

bool LoadMap(const std::filesystem::path& mapsDir, const MapInit& init,
             HarpoonTriforceThief::MapDef& out) {
    out.id            = init.id;
    out.name          = init.name;
    out.entranceIndex = init.entrance;
    out.description   = init.description;

    auto path = mapsDir / (std::string(init.id) + ".json");
    nlohmann::json j;
    if (!ReadJsonFile(path, j)) {
        SPDLOG_WARN("[Harpoon][TriforceThief] map JSON missing: {}", path.string());
        return false;
    }
    if (!j.contains("spawn_points") || !j["spawn_points"].is_array()) {
        SPDLOG_WARN("[Harpoon][TriforceThief] {} missing 'spawn_points' array", path.string());
        return false;
    }
    if (j.contains("entrance")) {
        out.entranceIndex = (s32)j["entrance"].get<int>();
    }
    for (const auto& sp : j["spawn_points"]) {
        HarpoonTriforceThief::SpawnPoint p;
        p.x      = sp.value("x", 0.0f);
        p.y      = sp.value("y", 0.0f);
        p.z      = sp.value("z", 0.0f);
        p.origin = sp.value("origin", std::string());
        p.note   = sp.value("note", std::string());
        out.spawnPoints.push_back(p);
    }
    return true;
}

bool LoadSavePresetJson(const std::filesystem::path& packRoot) {
    auto p = packRoot / "presets" / "save.json";
    return ReadJsonFile(p, sSavePresetRaw);
}

}  // anon namespace

// =============================================================================
// Public API
// =============================================================================

namespace HarpoonTriforceThief {

bool Init() {
    if (sLoaded) return true;

    std::string packRoot = ResolvePackRoot();
    if (packRoot.empty()) {
        SPDLOG_INFO("[Harpoon][TriforceThief] no pack at <appdir>/harpoon/gamemodes/triforce_thief — disabled");
        return false;
    }
    auto root = std::filesystem::path(packRoot);
    auto mapsDir = root / "maps";

    sMaps.clear();
    sMaps.reserve(kMapCount);

    bool ok = true;
    for (const auto& init : kMapInit) {
        MapDef def;
        bool mapOk = LoadMap(mapsDir, init, def);
        sMaps.push_back(std::move(def));
        ok &= mapOk;
    }
    ok &= LoadSavePresetJson(root);

    sLoaded = ok;
    if (ok) {
        SPDLOG_INFO("[Harpoon][TriforceThief] pack loaded from {} ({} maps)", packRoot, sMaps.size());
    } else {
        SPDLOG_WARN("[Harpoon][TriforceThief] pack at {} failed validation", packRoot);
    }
    return ok;
}

bool IsLoaded() { return sLoaded; }

const std::vector<MapDef>& GetMaps() { return sMaps; }

const MapDef* GetMap(s32 mapIdx) {
    if (mapIdx < 0 || (size_t)mapIdx >= sMaps.size()) return nullptr;
    return &sMaps[mapIdx];
}

const SpawnPoint* GetSpawnPoint(s32 mapIdx, s32 spawnIdx) {
    const MapDef* m = GetMap(mapIdx);
    if (!m) return nullptr;
    if (spawnIdx < 0 || (size_t)spawnIdx >= m->spawnPoints.size()) return nullptr;
    return &m->spawnPoints[spawnIdx];
}

s32 GetEntranceForMap(s32 mapIdx) {
    const MapDef* m = GetMap(mapIdx);
    return m ? m->entranceIndex : 205;
}

// ---------------------------------------------------------------------------
// Save preset application
// ---------------------------------------------------------------------------

namespace {

void ApplyCommonProgressionFlags(const nlohmann::json& common) {
    if (!common.is_object()) return;
    auto flags = common.value("progression_flags", nlohmann::json::object());
    gSaveContext.cutsceneIndex = (s32)common.value("cutscene_index", 0x8000);

    if (flags.value("carpenters_free", false)) {
        gSaveContext.eventChkInf[EVENTCHKINF_CARPENTERS_FREE_INDEX] |= EVENTCHKINF_CARPENTERS_FREE_MASK_ALL;
    }
    if (flags.value("gerudo_card", false)) {
        gSaveContext.inventory.questItems |= (1 << QUEST_GERUDO_CARD);
    }
    if (flags.value("king_zora_moved", false)) {
        SET_EVENTCHKINF(EVENTCHKINF_KING_ZORA_MOVED);
    }
    if (flags.value("all_scene_switches", false)) {
        for (int i = 0; i < 124; i++) {
            gSaveContext.sceneFlags[i].swch = 0xFFFFFFFF;
        }
    }
}

void ApplyBaseHealthMagic(const nlohmann::json& role) {
    gSaveContext.linkAge          = role.value("link_age", 0);
    gSaveContext.entranceIndex    = (s32)role.value("entrance_index",
                                       sSavePresetRaw["common"].value("entrance_index", 205));
    gSaveContext.healthCapacity   = (s16)role.value("health_capacity", 80);
    gSaveContext.health           = (s16)role.value("health", 80);
    gSaveContext.isMagicAcquired       = role.value("magic_acquired", true) ? 1 : 0;
    gSaveContext.isDoubleMagicAcquired = role.value("double_magic_acquired", true) ? 1 : 0;
    gSaveContext.magicLevel       = (s8)role.value("magic_level", 2);
    gSaveContext.magicCapacity    = (s16)role.value("magic_capacity", 96);
    gSaveContext.magic            = (s16)role.value("magic", 96);
    gSaveContext.magicState       = MAGIC_STATE_IDLE;
}

s32 ResolveUpgradeId(const std::string& name) {
    if (name == "STRENGTH")   return UPG_STRENGTH;
    if (name == "QUIVER")     return UPG_QUIVER;
    if (name == "BOMB_BAG")   return UPG_BOMB_BAG;
    if (name == "BULLET_BAG") return UPG_BULLET_BAG;
    if (name == "NUTS")       return UPG_NUTS;
    if (name == "STICKS")     return UPG_STICKS;
    if (name == "SCALE")      return UPG_SCALE;
    if (name == "WALLET")     return UPG_WALLET;
    SPDLOG_WARN("[Harpoon][TriforceThief] unknown upgrade '{}'", name);
    return -1;
}

// Resolve ITEM_* / SLOT_* / EQUIP_VALUE_* names. Same tables as
// PropHunt/PropHunt.cpp — kept duplicated for now to avoid pulling a shared
// header. Re-factor into Harpoon/SaveResolver.{h,cpp} once both modes are
// landing in the same PR.
s32 ResolveItemName(const std::string& n) {
    if (n == "ITEM_NONE")              return ITEM_NONE;
    if (n == "ITEM_STICK")             return ITEM_STICK;
    if (n == "ITEM_NUT")               return ITEM_NUT;
    if (n == "ITEM_BOMB")              return ITEM_BOMB;
    if (n == "ITEM_BOW")               return ITEM_BOW;
    if (n == "ITEM_ARROW_FIRE")        return ITEM_ARROW_FIRE;
    if (n == "ITEM_DINS_FIRE")         return ITEM_DINS_FIRE;
    if (n == "ITEM_SLINGSHOT")         return ITEM_SLINGSHOT;
    if (n == "ITEM_OCARINA_TIME")      return ITEM_OCARINA_TIME;
    if (n == "ITEM_BOMBCHU")           return ITEM_BOMBCHU;
    if (n == "ITEM_LONGSHOT")          return ITEM_LONGSHOT;
    if (n == "ITEM_HOOKSHOT")          return ITEM_HOOKSHOT;
    if (n == "ITEM_ARROW_ICE")         return ITEM_ARROW_ICE;
    if (n == "ITEM_FARORES_WIND")      return ITEM_FARORES_WIND;
    if (n == "ITEM_BOOMERANG")         return ITEM_BOOMERANG;
    if (n == "ITEM_LENS")              return ITEM_LENS;
    if (n == "ITEM_BEAN")              return ITEM_BEAN;
    if (n == "ITEM_HAMMER")            return ITEM_HAMMER;
    if (n == "ITEM_ARROW_LIGHT")       return ITEM_ARROW_LIGHT;
    if (n == "ITEM_NAYRUS_LOVE")       return ITEM_NAYRUS_LOVE;
    if (n == "ITEM_BOTTLE")            return ITEM_BOTTLE;
    if (n == "ITEM_MASK_BUNNY")        return ITEM_MASK_BUNNY;
    if (n == "ITEM_SWORD_KOKIRI")      return ITEM_SWORD_KOKIRI;
    if (n == "ITEM_SWORD_MASTER")      return ITEM_SWORD_MASTER;
    if (n == "ITEM_BOOTS_HOVER")       return ITEM_BOOTS_HOVER;
    if (n == "ITEM_ROCS_FEATHER_SKIJER") return ITEM_ROCS_FEATHER_SKIJER;
    if (n == "ITEM_ROCS_CAPE")         return ITEM_ROCS_CAPE;
    if (n == "ITEM_DEKU_LEAF")         return ITEM_DEKU_LEAF;
    if (n == "ITEM_SWITCH_HOOK")       return ITEM_SWITCH_HOOK;
    if (n == "ITEM_WHIP")              return ITEM_WHIP;
    if (n == "ITEM_CANE_OF_SOMARIA")   return ITEM_CANE_OF_SOMARIA;
    if (n == "ITEM_ROD_FIRE")          return ITEM_ROD_FIRE;
    if (n == "ITEM_ROD_ICE")           return ITEM_ROD_ICE;
    if (n == "ITEM_BEETLE")            return ITEM_BEETLE;
    SPDLOG_WARN("[Harpoon][TriforceThief] unknown item name '{}'", n);
    return ITEM_NONE;
}

s32 ResolveSlotName(const std::string& n) {
    if (n == "SLOT_STICK")        return SLOT_STICK;
    if (n == "SLOT_NUT")          return SLOT_NUT;
    if (n == "SLOT_BOMB")         return SLOT_BOMB;
    if (n == "SLOT_BOW")          return SLOT_BOW;
    if (n == "SLOT_ARROW_FIRE")   return SLOT_ARROW_FIRE;
    if (n == "SLOT_DINS_FIRE")    return SLOT_DINS_FIRE;
    if (n == "SLOT_SLINGSHOT")    return SLOT_SLINGSHOT;
    if (n == "SLOT_OCARINA")      return SLOT_OCARINA;
    if (n == "SLOT_BOMBCHU")      return SLOT_BOMBCHU;
    if (n == "SLOT_HOOKSHOT")     return SLOT_HOOKSHOT;
    if (n == "SLOT_ARROW_ICE")    return SLOT_ARROW_ICE;
    if (n == "SLOT_FARORES_WIND") return SLOT_FARORES_WIND;
    if (n == "SLOT_BOOMERANG")    return SLOT_BOOMERANG;
    if (n == "SLOT_LENS")         return SLOT_LENS;
    if (n == "SLOT_BEAN")         return SLOT_BEAN;
    if (n == "SLOT_HAMMER")       return SLOT_HAMMER;
    if (n == "SLOT_ARROW_LIGHT")  return SLOT_ARROW_LIGHT;
    if (n == "SLOT_NAYRUS_LOVE")  return SLOT_NAYRUS_LOVE;
    if (n == "SLOT_BOTTLE_1")     return SLOT_BOTTLE_1;
    if (n == "SLOT_BOTTLE_2")     return SLOT_BOTTLE_2;
    if (n == "SLOT_BOTTLE_3")     return SLOT_BOTTLE_3;
    if (n == "SLOT_BOTTLE_4")     return SLOT_BOTTLE_4;
    if (n == "SLOT_TRADE_CHILD")  return SLOT_TRADE_CHILD;
    if (n == "SLOT_TRADE_ADULT")  return SLOT_TRADE_ADULT;
    if (n == "SLOT_NONE")         return SLOT_NONE;
    if (n == "SLOT_ROCS")         return SLOT_ROCS;
    if (n == "SLOT_ROCS_CAPE")    return SLOT_ROCS_CAPE;
    if (n == "SLOT_DEKU_LEAF")    return SLOT_DEKU_LEAF;
    if (n == "SLOT_WHIP")         return SLOT_WHIP;
    if (n == "SLOT_SWITCH_HOOK")  return SLOT_SWITCH_HOOK;
    if (n == "SLOT_CANE_OF_SOMARIA") return SLOT_CANE_OF_SOMARIA;
    if (n == "SLOT_FIRE_ROD")     return SLOT_FIRE_ROD;
    if (n == "SLOT_ICE_ROD")      return SLOT_ICE_ROD;
    if (n == "SLOT_BEETLE")       return SLOT_BEETLE;
    SPDLOG_WARN("[Harpoon][TriforceThief] unknown slot name '{}'", n);
    return SLOT_NONE;
}

s32 ResolveEquipValueName(const std::string& n) {
    if (n == "EQUIP_VALUE_SWORD_KOKIRI")    return EQUIP_VALUE_SWORD_KOKIRI;
    if (n == "EQUIP_VALUE_SWORD_MASTER")    return EQUIP_VALUE_SWORD_MASTER;
    if (n == "EQUIP_VALUE_SWORD_BIGGORON")  return EQUIP_VALUE_SWORD_BIGGORON;
    if (n == "EQUIP_VALUE_SHIELD_DEKU")     return EQUIP_VALUE_SHIELD_DEKU;
    if (n == "EQUIP_VALUE_SHIELD_HYLIAN")   return EQUIP_VALUE_SHIELD_HYLIAN;
    if (n == "EQUIP_VALUE_SHIELD_MIRROR")   return EQUIP_VALUE_SHIELD_MIRROR;
    if (n == "EQUIP_VALUE_TUNIC_KOKIRI")    return EQUIP_VALUE_TUNIC_KOKIRI;
    if (n == "EQUIP_VALUE_TUNIC_GORON")     return EQUIP_VALUE_TUNIC_GORON;
    if (n == "EQUIP_VALUE_TUNIC_ZORA")      return EQUIP_VALUE_TUNIC_ZORA;
    if (n == "EQUIP_VALUE_BOOTS_KOKIRI")    return EQUIP_VALUE_BOOTS_KOKIRI;
    if (n == "EQUIP_VALUE_BOOTS_IRON")      return EQUIP_VALUE_BOOTS_IRON;
    if (n == "EQUIP_VALUE_BOOTS_HOVER")     return EQUIP_VALUE_BOOTS_HOVER;
    SPDLOG_WARN("[Harpoon][TriforceThief] unknown equip value '{}'", n);
    return 0;
}

s32 NumOrSlotName(const nlohmann::json& v) {
    if (v.is_number_integer()) return v.get<int>();
    if (v.is_string())         return ResolveSlotName(v.get<std::string>());
    return 0;
}
s32 NumOrItemName(const nlohmann::json& v) {
    if (v.is_number_integer()) return v.get<int>();
    if (v.is_string())         return ResolveItemName(v.get<std::string>());
    return ITEM_NONE;
}

void ApplyUpgrades(const nlohmann::json& upgrades) {
    if (!upgrades.is_object()) return;
    for (auto it = upgrades.begin(); it != upgrades.end(); ++it) {
        s32 upgId = ResolveUpgradeId(it.key());
        if (upgId >= 0) {
            Inventory_ChangeUpgrade(upgId, (s32)it.value());
        }
    }
}

void ApplyCvars(const nlohmann::json& cvars) {
    if (!cvars.is_object()) return;
    for (auto it = cvars.begin(); it != cvars.end(); ++it) {
        if (it.value().is_number_integer() || it.value().is_boolean()) {
            CVarSetInteger(it.key().c_str(), (s32)it.value().get<int>());
        } else if (it.value().is_string()) {
            CVarSetString(it.key().c_str(), it.value().get<std::string>().c_str());
        }
    }
    CVarSave();
}

// ---------------------------------------------------------------------------
// Equipment + inventory + ammo + button application — twin of PropHunt.cpp.
// ---------------------------------------------------------------------------

void ApplyEquipmentMask(const nlohmann::json& role) {
    gSaveContext.inventory.equipment = 0;
    if (role.value("equipment_all", false)) {
        gSaveContext.inventory.equipment |= (1 << EQUIP_INV_SWORD_KOKIRI);
        gSaveContext.inventory.equipment |= (1 << EQUIP_INV_SWORD_MASTER);
        gSaveContext.inventory.equipment |= (1 << EQUIP_INV_SWORD_BIGGORON);
        gSaveContext.inventory.equipment |= (1 << (4 + EQUIP_INV_SHIELD_DEKU));
        gSaveContext.inventory.equipment |= (1 << (4 + EQUIP_INV_SHIELD_HYLIAN));
        gSaveContext.inventory.equipment |= (1 << (4 + EQUIP_INV_SHIELD_MIRROR));
        gSaveContext.inventory.equipment |= (1 << (8 + EQUIP_INV_TUNIC_KOKIRI));
        gSaveContext.inventory.equipment |= (1 << (8 + EQUIP_INV_TUNIC_GORON));
        gSaveContext.inventory.equipment |= (1 << (8 + EQUIP_INV_TUNIC_ZORA));
        gSaveContext.inventory.equipment |= (1 << (12 + EQUIP_INV_BOOTS_KOKIRI));
        gSaveContext.inventory.equipment |= (1 << (12 + EQUIP_INV_BOOTS_IRON));
        gSaveContext.inventory.equipment |= (1 << (12 + EQUIP_INV_BOOTS_HOVER));
        return;
    }
    if (role.contains("equipment_mask_bits") && role["equipment_mask_bits"].is_array()) {
        for (const auto& b : role["equipment_mask_bits"]) {
            if (b.is_number_integer()) gSaveContext.inventory.equipment |= (1u << b.get<int>());
        }
    }
}

void ApplyEquipChoice(const nlohmann::json& equipObj) {
    if (!equipObj.is_object()) return;
    auto get = [&](const char* k) -> s32 {
        if (!equipObj.contains(k)) return 0;
        return ResolveEquipValueName(equipObj[k].get<std::string>());
    };
    if (equipObj.contains("sword"))  Inventory_ChangeEquipment(EQUIP_TYPE_SWORD,  get("sword"));
    if (equipObj.contains("shield")) Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, get("shield"));
    if (equipObj.contains("tunic"))  Inventory_ChangeEquipment(EQUIP_TYPE_TUNIC,  get("tunic"));
    if (equipObj.contains("boots"))  Inventory_ChangeEquipment(EQUIP_TYPE_BOOTS,  get("boots"));
}

void ApplyClearInventoryIfRequested(const nlohmann::json& role) {
    if (!role.value("inventory_clear", false)) return;
    for (size_t i = 0; i < ARRAY_COUNT(gSaveContext.inventory.items); i++) {
        gSaveContext.inventory.items[i] = ITEM_NONE;
    }
}

void ApplyItemsMap(const nlohmann::json& itemsObj) {
    if (!itemsObj.is_object()) return;
    for (auto it = itemsObj.begin(); it != itemsObj.end(); ++it) {
        const std::string& key = it.key();
        s32 slot;
        if (!key.empty() && std::isdigit((unsigned char)key[0])) {
            slot = std::atoi(key.c_str());
        } else {
            slot = ResolveSlotName(key);
        }
        s32 item = NumOrItemName(it.value());
        if (slot >= 0 && (size_t)slot < ARRAY_COUNT(gSaveContext.inventory.items)) {
            gSaveContext.inventory.items[slot] = (u8)item;
        }
    }
}

void ApplyPageStrategies(const nlohmann::json& role) {
    auto p2 = role.value("items_page2_strategy", std::string());
    if (p2 == "gPage2Items_with_RocsCape_at_24") {
        for (int i = 0; i < 24; i++) {
            if (i == 0) gSaveContext.inventory.items[24 + i] = ITEM_ROCS_CAPE;
            else        gSaveContext.inventory.items[24 + i] = gPage2Items[i];
        }
    }
    auto p3 = role.value("items_page3_strategy", std::string());
    if (p3 == "gPage3MaskItems") {
        for (int i = 0; i < 24; i++) {
            gSaveContext.inventory.items[48 + i] = gPage3MaskItems[i];
        }
    }
}

void ApplyAmmo(const nlohmann::json& role) {
    if (role.value("ammo_clear", false)) {
        for (size_t i = 0; i < ARRAY_COUNT(gSaveContext.inventory.ammo); i++) {
            gSaveContext.inventory.ammo[i] = 0;
        }
    }
    auto ammo = role.value("ammo", nlohmann::json::object());
    if (!ammo.is_object()) return;
    // First clear all, then apply specific slots. The TT preset doesn't
    // include ammo_clear (it has ammo set explicitly), so we clear here too
    // to match Scooter behaviour where ammo always starts at zero.
    for (size_t i = 0; i < ARRAY_COUNT(gSaveContext.inventory.ammo); i++) {
        gSaveContext.inventory.ammo[i] = 0;
    }
    for (auto it = ammo.begin(); it != ammo.end(); ++it) {
        const std::string& key = it.key();
        if (!key.empty() && key[0] == '_') continue;
        s32 slot = ResolveSlotName(key);
        if (slot >= 0 && (size_t)slot < ARRAY_COUNT(gSaveContext.inventory.ammo)) {
            gSaveContext.inventory.ammo[slot] = (s8)it.value().get<int>();
        }
    }
}

void ApplyButtonItems(const nlohmann::json& arr) {
    if (!arr.is_array()) return;
    for (size_t i = 0; i < arr.size() && i < 8; i++) {
        gSaveContext.equips.buttonItems[i] = (u8)NumOrItemName(arr[i]);
    }
}

void ApplyCButtonSlots(const nlohmann::json& arr) {
    if (!arr.is_array()) return;
    for (size_t i = 0; i < arr.size() && i < 7; i++) {
        gSaveContext.equips.cButtonSlots[i] = (u8)NumOrSlotName(arr[i]);
    }
}

}  // anon

void ApplyThiefSave() {
    if (sSavePresetRaw.is_null() || !sSavePresetRaw.contains("thief")) {
        SPDLOG_WARN("[Harpoon][TriforceThief] save preset missing 'thief' section");
        return;
    }
    auto& common = sSavePresetRaw["common"];
    auto& role   = sSavePresetRaw["thief"];

    // Scooter sets QUEST_PROP_HUNT here (TT reuses the prop-hunt quest flag).
    // This branch hasn't introduced that enum yet; once added, set:
    //   gSaveContext.ship.quest.id = QUEST_PROP_HUNT;

    ApplyBaseHealthMagic(role);
    ApplyUpgrades(role.value("upgrades", nlohmann::json::object()));
    ApplyEquipmentMask(role);
    ApplyEquipChoice(role.value("equip", nlohmann::json::object()));
    ApplyClearInventoryIfRequested(role);
    ApplyItemsMap(role.value("items",        nlohmann::json::object()));
    ApplyItemsMap(role.value("items_page1",  nlohmann::json::object()));
    ApplyPageStrategies(role);
    ApplyAmmo(role);
    ApplyButtonItems(role.value("button_items",   nlohmann::json::array()));
    ApplyCButtonSlots(role.value("c_button_slots", nlohmann::json::array()));
    ApplyCommonProgressionFlags(common);
    ApplyCvars(common.value("cvars", nlohmann::json::object()));

    SPDLOG_INFO("[Harpoon][TriforceThief] applied 'thief' save preset");
}

// ---------------------------------------------------------------------------
// Local state
// ---------------------------------------------------------------------------

LocalState& GetLocalState() { return sLocal; }
// `sLocal.role` is never set today (no role-assignment events in TT). The
// canonical "am I the carrier" check is the network-synced carrierClientId.
bool IsLocalCarrier() {
    return Harpoon::Instance != nullptr &&
           sLocal.carrierClientId != 0 &&
           sLocal.carrierClientId == Harpoon::Instance->ownClientId;
}
bool IsInMapSelect()  { return sLocal.inMapSelect; }
bool IsInRound()      { return sLocal.inRound; }

bool CycleHoveredMap(s32 delta) {
    if (delta == 0) return false;
    s32 count = (s32)sMaps.size();
    if (count <= 0) return false;
    s32 next = (sLocal.hoveredMap + delta + count) % count;
    if (next == sLocal.hoveredMap) return false;
    sLocal.hoveredMap = next;
    return true;
}

// ---------------------------------------------------------------------------
// Event dispatch
// ---------------------------------------------------------------------------

namespace {

void HandleMapSelectBegin(const nlohmann::json& p) {
    sLocal.inMapSelect = true;
    sLocal.inRound = false;
    // CRITICAL: set the room-wide flags peers' overlays gate on. Without
    // these the TriforceThiefMapSelect overlay's first-frame check
    //   `if (harpoon->gameState != HARPOON_STATE_MAP_SELECT) return;`
    // returns early and the overlay never draws on peer screens — meaning
    // only the host (whose menu code sets gameState directly) sees it.
    // Also reset every peer's vote flag so a stale "voted last round"
    // doesn't make the host's tally fire immediately on a new window.
    if (Harpoon::Instance != nullptr) {
        Harpoon::Instance->gameState = HARPOON_STATE_MAP_SELECT;
        Harpoon::Instance->mapSelectMode =
            (HarpoonMapSelectMode)p.value("mapSelectMode",
                                          (int)Harpoon::Instance->mapSelectMode);
        for (auto& [cid, c] : Harpoon::Instance->clients) {
            c.hasVoted = false;
        }
    }
    // Vote deadline: only meaningful in EVERYONE_CHOOSES, but harmless to
    // arm always. Default 15s — matches the user-requested poll window.
    // Stored as frames at the same 60 fps cadence the rupee drain uses.
    s32 windowSec = p.value("windowSeconds", 15);
    if (windowSec < 5)   windowSec = 5;
    if (windowSec > 120) windowSec = 120;
    sLocal.mapVoteDeadline = windowSec * 60;
}

void HandleMapHover(const nlohmann::json& p) {
    sLocal.hoveredMap = p.value("mapIndex", 0);
}

void HandleMapConfirmed(const nlohmann::json& p) {
    sLocal.confirmedMap = p.value("mapIndex", 0);
    sLocal.inMapSelect = false;
    sLocal.inRound = true;
    sLocal.roundIndex += 1;
    // CRITICAL: flip the room-wide state off MAP_SELECT so the overlay
    // closes on every peer (the overlay's first gate is
    // `gameState != HARPOON_STATE_MAP_SELECT → return`). Without this,
    // peers stay on the fullscreen overlay even after the host confirms.
    if (Harpoon::Instance != nullptr) {
        Harpoon::Instance->gameState         = HARPOON_STATE_PLAYING;
        Harpoon::Instance->confirmedMapIndex = sLocal.confirmedMap;
    }
    const MapDef* m = GetMap(sLocal.confirmedMap);
    Notification::Emit({
        .prefix = "Triforce Thief",
        .message = "Round " + std::to_string(sLocal.roundIndex) + " — " +
                   (m ? m->name : std::string("?")),
        .remainingTime = 4.0f,
    });
    // Apply thief save preset and teleport to the confirmed map.
    StartLocalRoundOnMap(sLocal.confirmedMap);
}

void HandleTriforceSpawn(const nlohmann::json& p) {
    sLocal.carrierClientId = 0;
    sLocal.currentSpawn = p.value("spawnIndex", -1);
    sLocal.triforceX = p.value("x", 0.0f);
    sLocal.triforceY = p.value("y", 0.0f);
    sLocal.triforceZ = p.value("z", 0.0f);
    sLocal.dropFlyTimer = 0;
    sLocal.pickupCooldownByCid.clear();

    // Appear effect: golden flash SFX + announcement. (Full subcamera
    // orbit a la Scooter is deferred — this gives players a clear audio
    // cue that the Triforce just spawned.)
    Sfx_PlaySfxCentered(NA_SE_EV_TRIFORCE_FLASH);
    Notification::Emit({
        .prefix = "Triforce Thief",
        .message = "The Triforce has appeared!",
        .remainingTime = 4.0f,
    });
}

void HandleTriforcePickup(const nlohmann::json& p) {
    sLocal.carrierClientId = p.value("carrierClientId", 0u);
    sLocal.role = (sLocal.carrierClientId != 0) ? Role::Thief : Role::Thief;
    // Announce by display name, falling back to clientId.
    std::string who = "Someone";
    if (Harpoon::Instance != nullptr && sLocal.carrierClientId != 0) {
        auto it = Harpoon::Instance->clients.find(sLocal.carrierClientId);
        if (it != Harpoon::Instance->clients.end() && !it->second.name.empty()) {
            who = it->second.name;
        } else {
            who = "cid" + std::to_string(sLocal.carrierClientId);
        }
    }
    Notification::Emit({
        .prefix = "Triforce Thief",
        .message = who + " grabbed the Triforce!",
        .remainingTime = 4.0f,
    });
}

void HandleTriforceDrop(const nlohmann::json& p) {
    u32 dropper = p.value("dropperClientId", 0u);
    sLocal.carrierClientId = 0;

    // Launch position + initial velocity. Each client integrates the
    // same physics (deterministic given equal start state + shared scene
    // collision), so the Triforce lands in the same spot on every screen.
    f32 sx = p.value("startX", sLocal.triforceX);
    f32 sy = p.value("startY", sLocal.triforceY);
    f32 sz = p.value("startZ", sLocal.triforceZ);
    sLocal.triforceX = sx;
    sLocal.triforceY = sy;
    sLocal.triforceZ = sz;
    sLocal.dropPrevX = sx;
    sLocal.dropPrevY = sy;
    sLocal.dropPrevZ = sz;
    sLocal.dropVelX = p.value("velX", 0.0f);
    sLocal.dropVelY = p.value("velY", 12.0f);
    sLocal.dropVelZ = p.value("velZ", 0.0f);
    sLocal.dropFlyTimer = 120;   // ~2 sec safety fuse — physics settle earlier

    // 90-frame pickup cooldown on the dropper only — they have to chase it.
    // Other clients can grab it immediately once it lands.
    if (dropper != 0) {
        sLocal.pickupCooldownByCid[dropper] = 90;
    }

    Notification::Emit({
        .prefix = "Triforce Thief",
        .message = "Triforce knocked loose!",
        .remainingTime = 3.0f,
    });
}

void HandleRoundResult(const nlohmann::json& p) {
    u32 winner = p.value("winnerClientId", 0u);
    s32 round  = p.value("roundIndex", sLocal.roundIndex);
    SPDLOG_INFO("[Harpoon][TriforceThief] round {} winner=cid{}", round, winner);

    // Announce by display name.
    std::string who = "cid" + std::to_string(winner);
    if (Harpoon::Instance != nullptr && winner != 0) {
        auto it = Harpoon::Instance->clients.find(winner);
        if (it != Harpoon::Instance->clients.end() && !it->second.name.empty()) {
            who = it->second.name;
        }
    }
    Notification::Emit({
        .prefix = "Triforce Thief",
        .message = "Round " + std::to_string(round) + " winner: " + who,
        .remainingTime = 5.0f,
    });

    // Reset round state and teleport back to the lobby. The lobby is Hyrule
    // Field (map index 0); ApplyThiefSave on teleport resets gSaveContext.rupees
    // so the rupee HUD reads the preset default for the next round.
    sLocal.inRound                = false;
    sLocal.roundEnded             = true;
    sLocal.carrierClientId        = 0;
    sLocal.carrierRupeesRemaining = 0;
    sLocal.drainTickCounter       = 0;
    sLocal.dropFlyTimer           = 0;
    sLocal.pickupCooldownByCid.clear();
    sLocal.role                   = Role::Unassigned;

    // Tear down the appear-cutscene subcamera if it's still up (corner case:
    // the round ended in the middle of the intro orbit). Restore the main
    // camera so the player isn't stuck looking at a static subcam frame.
    if (sLocal.cutsceneSubCam != SUBCAM_FREE && gPlayState != nullptr) {
        Play_ChangeCameraStatus(gPlayState, MAIN_CAM, CAM_STAT_ACTIVE);
        Play_ClearCamera(gPlayState, sLocal.cutsceneSubCam);
    }
    sLocal.cutsceneSubCam = SUBCAM_FREE;
    sLocal.cutsceneTimer  = 0;
    sLocal.cutsceneReady  = false;
    if (Harpoon::Instance != nullptr) {
        Harpoon::Instance->gameState = HARPOON_STATE_LOBBY;
    }

    // Force-clear player blocking state so the lobby teleport actually fires
    // even if the local Link is dead, in a damage cutscene, or in an item
    // pickup CS. Without this, a player who died at the exact moment of
    // round-end would soft-stuck on the Game Over screen.
    if (gPlayState != nullptr) {
        Player* localPlayer = GET_PLAYER(gPlayState);
        if (localPlayer != nullptr) {
            localPlayer->stateFlags1 &= ~(PLAYER_STATE1_DEAD |
                                          PLAYER_STATE1_IN_ITEM_CS |
                                          PLAYER_STATE1_IN_CUTSCENE |
                                          PLAYER_STATE1_LOADING);
            localPlayer->invincibilityTimer = 0;
            localPlayer->actor.freezeTimer  = 0;
        }
        // Cancel any in-progress engine cutscene state too.
        gSaveContext.cutsceneIndex   = 0;
        gSaveContext.cutsceneTrigger = 0;
    }

    StartLocalRoundOnMap(0);
}

}  // anon

void HandleEvent(const nlohmann::json& envelope) {
    std::string evt = envelope.value("event_name", std::string());
    if (evt.empty()) evt = envelope.value("event", std::string());
    if (evt.empty()) return;

    const nlohmann::json& data = envelope.contains("data") && envelope["data"].is_object()
                                   ? envelope["data"] : envelope;

    if      (evt == kEvtMapSelectBegin)  HandleMapSelectBegin(data);
    else if (evt == kEvtMapHover)        HandleMapHover(data);
    else if (evt == kEvtMapConfirmed)    HandleMapConfirmed(data);
    else if (evt == kEvtTriforceSpawn)   HandleTriforceSpawn(data);
    else if (evt == kEvtTriforcePickup)  HandleTriforcePickup(data);
    else if (evt == kEvtTriforceDrop)    HandleTriforceDrop(data);
    else if (evt == kEvtRoundResult)     HandleRoundResult(data);
    else if (evt == kEvtMapVote) {
        // Peer cast a vote — host's TickFrame() tallies later.
        u32 src = envelope.value("source", 0u);
        s32 idx = data.value("mapIndex", 0);
        if (Harpoon::Instance != nullptr && src != 0) {
            auto it = Harpoon::Instance->clients.find(src);
            if (it != Harpoon::Instance->clients.end()) {
                it->second.mapSelectIndex = idx;
                it->second.hasVoted = true;
            }
        }
    }
    else if (evt == kEvtRoundConfig) {
        // Host broadcasts win-seconds at the start of each round so every
        // client agrees on how long the carrier needs to hold.
        s32 ws = data.value("winSeconds", 60);
        if (ws < 5)   ws = 5;
        if (ws > 600) ws = 600;
        sLocal.roundWinSeconds = ws;
    }
    else if (evt == kEvtCarrierTimerSync) {
        // Carrier broadcasting their remaining time — peers mirror it for
        // the HUD label "Alice: 42s" on the directional arrow.
        u32 src       = envelope.value("source", 0u);
        s32 remaining = data.value("rupeesRemaining", 0);
        if (Harpoon::Instance != nullptr && src != 0) {
            auto it = Harpoon::Instance->clients.find(src);
            if (it != Harpoon::Instance->clients.end()) {
                it->second.ttRupeesRemaining = remaining;
            }
        }
    }
    else if (evt == kEvtCutsceneBegin) {
        // Host broadcast that the round is starting — kick off the
        // Triforce-appear cutscene. We arm the timer here and flip
        // cutsceneReady only after OnSceneLoaded fires, so the orbit
        // doesn't try to run while the scene is still streaming in.
        sLocal.triforceX = data.value("x", sLocal.triforceX);
        sLocal.triforceY = data.value("y", sLocal.triforceY);
        sLocal.triforceZ = data.value("z", sLocal.triforceZ);
        sLocal.cutsceneTimer  = 100;   // ~5 sec at 20 game-fps
        sLocal.cutsceneReady  = false;
        sLocal.cutsceneSubCam = SUBCAM_FREE;
    }
}

// Called by HarpoonHookHandlers' OnSceneSpawnActors hook when we're in TT.
// Marks the round-start cutscene as "scene is ready, subcam may run now".
void OnSceneLoaded() {
    if (sLocal.cutsceneTimer > 0) {
        sLocal.cutsceneReady = true;
    }
}

// ---------------------------------------------------------------------------
// Payload builders
// ---------------------------------------------------------------------------

static nlohmann::json _Envelope(const char* evt, nlohmann::json data) {
    nlohmann::json p;
    p["type"]       = "ROOM.BROADCAST_EVENT";
    p["event_name"] = evt;
    p["data"]       = std::move(data);
    return p;
}

nlohmann::json BuildMapSelectBeginPayload(s32 windowSeconds) {
    nlohmann::json d;
    d["windowSeconds"] = windowSeconds;
    return _Envelope(kEvtMapSelectBegin, std::move(d));
}

nlohmann::json BuildMapHoverPayload(s32 mapIdx) {
    nlohmann::json d;
    d["mapIndex"] = mapIdx;
    return _Envelope(kEvtMapHover, std::move(d));
}

nlohmann::json BuildMapConfirmedPayload(s32 mapIdx, s32 entranceIndex) {
    nlohmann::json d;
    d["mapIndex"]      = mapIdx;
    d["entranceIndex"] = entranceIndex;
    return _Envelope(kEvtMapConfirmed, std::move(d));
}

nlohmann::json BuildTriforceSpawnPayload(s32 mapIdx, s32 spawnIdx,
                                          f32 x, f32 y, f32 z) {
    nlohmann::json d;
    d["mapIndex"]   = mapIdx;
    d["spawnIndex"] = spawnIdx;
    d["x"] = x; d["y"] = y; d["z"] = z;
    return _Envelope(kEvtTriforceSpawn, std::move(d));
}

nlohmann::json BuildTriforcePickupPayload(u32 carrierClientId) {
    nlohmann::json d;
    d["carrierClientId"] = carrierClientId;
    return _Envelope(kEvtTriforcePickup, std::move(d));
}

nlohmann::json BuildTriforceDropPayload(u32 dropperClientId,
                                         f32 startX, f32 startY, f32 startZ,
                                         f32 velX,   f32 velY,   f32 velZ) {
    nlohmann::json d;
    d["dropperClientId"] = dropperClientId;
    d["startX"] = startX; d["startY"] = startY; d["startZ"] = startZ;
    d["velX"]   = velX;   d["velY"]   = velY;   d["velZ"]   = velZ;
    return _Envelope(kEvtTriforceDrop, std::move(d));
}

nlohmann::json BuildRoundResultPayload(u32 winnerClientId, s32 roundIndex) {
    nlohmann::json d;
    d["winnerClientId"] = winnerClientId;
    d["roundIndex"]     = roundIndex;
    return _Envelope(kEvtRoundResult, std::move(d));
}

nlohmann::json BuildRoundConfigPayload(s32 winSeconds) {
    nlohmann::json d;
    d["winSeconds"] = winSeconds;
    return _Envelope(kEvtRoundConfig, std::move(d));
}

nlohmann::json BuildCarrierTimerSyncPayload(s32 rupeesRemaining) {
    nlohmann::json d;
    d["rupeesRemaining"] = rupeesRemaining;
    return _Envelope(kEvtCarrierTimerSync, std::move(d));
}

nlohmann::json BuildCutsceneBeginPayload(f32 triforceX, f32 triforceY, f32 triforceZ) {
    nlohmann::json d;
    d["x"] = triforceX;
    d["y"] = triforceY;
    d["z"] = triforceZ;
    return _Envelope(kEvtCutsceneBegin, std::move(d));
}

// ---------------------------------------------------------------------------
// HUD — map-select grid + round status. Top-left always-visible panel.
// Caller gates by active gamemode.
// ---------------------------------------------------------------------------

void DrawHud() {
    ImGui::SetNextWindowPos(ImVec2(12.0f, 80.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                              ImGuiWindowFlags_AlwaysAutoResize |
                              ImGuiWindowFlags_NoSavedSettings |
                              ImGuiWindowFlags_NoFocusOnAppearing |
                              ImGuiWindowFlags_NoNav |
                              ImGuiWindowFlags_NoInputs;
    if (!ImGui::Begin("TriforceThiefHUD", nullptr, flags)) { ImGui::End(); return; }

    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Triforce Thief");

    if (sLocal.inMapSelect) {
        ImGui::Separator();
        ImGui::Text("Map select:");
        for (size_t i = 0; i < sMaps.size(); i++) {
            bool isHovered = ((s32)i == sLocal.hoveredMap);
            ImVec4 c = isHovered ? ImVec4(1.0f, 1.0f, 0.4f, 1.0f)
                                 : ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
            ImGui::TextColored(c, "  %s %s",
                                isHovered ? ">" : " ",
                                sMaps[i].name.c_str());
        }
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                            "D-Left / D-Right to cycle");
    } else if (sLocal.inRound) {
        ImGui::Separator();
        const MapDef* m = GetMap(sLocal.confirmedMap);
        ImGui::Text("Map:   %s", m ? m->name.c_str() : "?");
        ImGui::Text("Round: %d", sLocal.roundIndex);
        if (sLocal.carrierClientId != 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f),
                                "Carrier: cid%u", sLocal.carrierClientId);
        } else {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                                "Triforce: dropped");
        }

        // Directional arrows — one per other online player, plus one for
        // the Triforce when it's on the ground. Drawn on the foreground
        // draw list so they sit on top of the gameworld. Each arrow is
        // tinted with the player's chosen color; the carrier (if any) is
        // overridden to gold and gets a "<name>: <s>s" label showing
        // their carrier-timer broadcast.
        if (gPlayState != nullptr && Harpoon::Instance != nullptr) {
            Player* localPlayer = GET_PLAYER(gPlayState);
            if (localPlayer != nullptr) {
                Camera* cam = gPlayState->cameraPtrs[gPlayState->activeCamera];
                f32 camAngle = cam ? cam->camDir.y * (3.14159265f / 32768.0f) : 0.0f;
                auto vp = ImGui::GetMainViewport();
                float cx = vp->Pos.x + vp->Size.x * 0.5f;
                float cy = vp->Pos.y + vp->Size.y * 0.5f;
                float radius = fminf(vp->Size.x, vp->Size.y) * 0.4f;
                ImDrawList* fg = ImGui::GetForegroundDrawList(vp);

                auto drawArrow = [&](f32 tx, f32 tz, ImU32 col, const char* label) {
                    f32 dx = tx - localPlayer->actor.world.pos.x;
                    f32 dz = tz - localPlayer->actor.world.pos.z;
                    f32 dist = sqrtf(dx * dx + dz * dz);
                    if (dist <= 100.0f) return;
                    // See comment in old code re: sinf sign flip — OoT cam
                    // yaw rotates opposite to atan2's convention.
                    f32 angle    = atan2f(dx, dz);
                    f32 relAngle = angle - camAngle;
                    float ax = cx - sinf(relAngle) * radius;
                    float ay = cy - cosf(relAngle) * radius;
                    float sz = 13.0f;
                    float perpX =  cosf(relAngle) * sz;
                    float perpY =  sinf(relAngle) * sz;
                    float backX =  sinf(relAngle) * sz * 1.5f;
                    float backY =  cosf(relAngle) * sz * 1.5f;
                    fg->AddTriangleFilled(
                        ImVec2(ax, ay),
                        ImVec2(ax + perpX + backX, ay + perpY + backY),
                        ImVec2(ax - perpX + backX, ay - perpY + backY),
                        col);
                    if (label != nullptr && label[0] != '\0') {
                        ImVec2 ts = ImGui::CalcTextSize(label);
                        fg->AddText(ImVec2(ax - ts.x * 0.5f, ay + sz + 2),
                                    IM_COL32(255, 255, 255, 220), label);
                    }
                };

                // (1) Triforce on the ground.
                if (sLocal.carrierClientId == 0) {
                    drawArrow(sLocal.triforceX, sLocal.triforceZ,
                              IM_COL32(255, 215, 0, 230), "Triforce");
                }

                // (2) Every other online player.
                for (auto& [cid, c] : Harpoon::Instance->clients) {
                    if (!c.online || c.self) continue;
                    bool isCarrier = (cid == sLocal.carrierClientId);
                    ImU32 col = isCarrier
                        ? IM_COL32(255, 215, 0, 235)
                        : IM_COL32(c.color.r, c.color.g, c.color.b, 200);
                    char buf[64];
                    if (isCarrier) {
                        snprintf(buf, sizeof(buf), "%s: %ds",
                                 c.name.c_str(), c.ttRupeesRemaining);
                    } else {
                        snprintf(buf, sizeof(buf), "%s", c.name.c_str());
                    }
                    drawArrow(c.posRot.pos.x, c.posRot.pos.z, col, buf);
                }
            }
        }
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                            "Lobby — waiting for host to start.");
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// In-world rendering
// ---------------------------------------------------------------------------

namespace {

// Load the Triforce piece display list lazily — calling ResourceMgr each
// frame is cheap (internal cache) and we want fresh pointers across scene
// transitions.
Gfx* GetTriforceDL() {
    return ResourceMgr_LoadGfxByName(dgTriforcePieceCompletedDL);
}

}  // anon

void DrawTriforceOnGround(PlayState* play) {
    if (!sLocal.inRound) return;
    if (sLocal.carrierClientId != 0) return;  // Someone is carrying it.
    if (play == nullptr) return;

    Gfx* triforceDL = GetTriforceDL();
    if (triforceDL == nullptr) return;

    // Spin + bob.
    sTriforceSpinAngle += 0x300;
    f32 bobY = sinf((f32)play->state.frames * 0.08f) * 8.0f;

    // Manual OPEN_DISPS/CLOSE_DISPS expansion — see PropHunt.cpp for why.
    FrameInterpolation_RecordOpenChild(__FILE__, __LINE__);
    GraphicsContext* __gfxCtx = play->state.gfxCtx;
    Gfx* dispRefs[4];
    Graph_OpenDisps(dispRefs, __gfxCtx, __FILE__, __LINE__);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gDPSetEnvColor(POLY_XLU_DISP++, 255, 215, 0, 200);

    Matrix_Push();
    Matrix_Translate(sLocal.triforceX,
                     sLocal.triforceY + 30.0f + bobY,
                     sLocal.triforceZ, MTXMODE_NEW);
    Matrix_RotateY(sTriforceSpinAngle * (M_PI / 0x8000), MTXMODE_APPLY);
    Matrix_Scale(0.03f, 0.03f, 0.03f, MTXMODE_APPLY);

    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
              G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPDisplayList(POLY_XLU_DISP++, triforceDL);

    Matrix_Pop();

    Graph_CloseDisps(dispRefs, __gfxCtx, __FILE__, __LINE__);
    FrameInterpolation_RecordCloseChild();
}

void DrawTriforceAboveHead(Actor* actor, PlayState* play) {
    if (!sLocal.inRound) return;
    if (actor == nullptr || play == nullptr) return;

    Gfx* triforceDL = GetTriforceDL();
    if (triforceDL == nullptr) return;

    sTriforceSpinAngle += 0x200;

    // Manual OPEN_DISPS/CLOSE_DISPS expansion — see PropHunt.cpp for why.
    FrameInterpolation_RecordOpenChild(__FILE__, __LINE__);
    GraphicsContext* __gfxCtx = play->state.gfxCtx;
    Gfx* dispRefs[4];
    Graph_OpenDisps(dispRefs, __gfxCtx, __FILE__, __LINE__);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gDPSetEnvColor(POLY_XLU_DISP++, 255, 223, 0, 230);

    Matrix_Push();
    Matrix_Translate(actor->world.pos.x,
                     actor->world.pos.y + 85.0f,
                     actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(sTriforceSpinAngle * (M_PI / 0x8000), MTXMODE_APPLY);
    Matrix_Scale(0.015f, 0.015f, 0.015f, MTXMODE_APPLY);

    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
              G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPDisplayList(POLY_XLU_DISP++, triforceDL);

    Matrix_Pop();
    Graph_CloseDisps(dispRefs, __gfxCtx, __FILE__, __LINE__);
    FrameInterpolation_RecordCloseChild();
}

bool ShouldPickupTriforce(f32 px, f32 py, f32 pz) {
    if (!sLocal.inRound || sLocal.roundEnded) return false;
    if (sLocal.carrierClientId != 0) return false;
    if (sLocal.dropFlyTimer > 0) return false;   // mid-flight, untouchable
    // Per-client cooldown: the player who just dropped it can't re-grab
    // it for 90 frames. Everyone else can.
    if (Harpoon::Instance != nullptr) {
        auto it = sLocal.pickupCooldownByCid.find(Harpoon::Instance->ownClientId);
        if (it != sLocal.pickupCooldownByCid.end() && it->second > 0) return false;
    }
    // 60 unit radius in 3D + 100 unit vertical leeway. Matches Scooter's
    // PVP pickup AABB roughly.
    f32 dx = px - sLocal.triforceX;
    f32 dz = pz - sLocal.triforceZ;
    f32 dy = py - sLocal.triforceY;
    if (dy < -50.0f || dy > 150.0f) return false;
    return (dx * dx + dz * dz) <= (60.0f * 60.0f);
}

}  // namespace HarpoonTriforceThief

// Bridge to the global flag defined in PropHunt.cpp. Declaring `extern`
// inside the HarpoonTriforceThief namespace would resolve to
// `HarpoonTriforceThief::sHarpoonAuthorizedTransition` (link error);
// declaring at global scope here forces correct lookup of the global
// symbol. Re-open the namespace below for the rest of the file.
extern bool sHarpoonAuthorizedTransition;

namespace HarpoonTriforceThief {

void TeleportToEntrance(s32 entranceIndex) {
    if (gPlayState == nullptr) return;
    gPlayState->linkAgeOnLoad     = gSaveContext.linkAge;
    gPlayState->nextEntranceIndex = entranceIndex;
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType    = TRANS_TYPE_FADE_BLACK;
    ::sHarpoonAuthorizedTransition = true;
}

void StartLocalRoundOnMap(s32 mapIndex) {
    ApplyThiefSave();
    s32 entrance = GetEntranceForMap(mapIndex);
    TeleportToEntrance(entrance);
}

// Carrier pickup: seed (first time this round) or resume (per-carrier
// persistent) the rupee countdown and mirror it into gSaveContext.rupees
// so the rupee HUD displays it. Invoked by HarpoonHookHandlers right
// after the local pickup AABB check fires.
void OnLocalPickup() {
    if (sLocal.carrierRupeesRemaining <= 0) {
        sLocal.carrierRupeesRemaining = sLocal.roundWinSeconds;
    }
    gSaveContext.rupees = sLocal.carrierRupeesRemaining;
    sLocal.drainTickCounter = 60;
}

// Host-only round starter — applies map locally and broadcasts the
// envelope set so every peer agrees on map, round-win seconds, and the
// Triforce spawn point. Shared by the overlay A-press path, the host
// vote tally, and the random auto-pick.
void HostConfirmMap(s32 mapIdx) {
    if (Harpoon::Instance == nullptr) return;

    // 1) Local apply.
    StartLocalRoundOnMap(mapIdx);
    sLocal.confirmedMap           = mapIdx;
    sLocal.inMapSelect            = false;
    sLocal.inRound                = true;
    sLocal.roundEnded             = false;
    sLocal.carrierClientId        = 0;
    sLocal.carrierRupeesRemaining = 0;
    sLocal.drainTickCounter       = 0;
    sLocal.roundIndex            += 1;
    Harpoon::Instance->gameState  = HARPOON_STATE_PLAYING;

    // 2) Broadcast: MAP_CONFIRMED + ROUND_CONFIG.
    s32 entrance = GetEntranceForMap(mapIdx);
    Harpoon::Instance->SendJsonToRemote(BuildMapConfirmedPayload(mapIdx, entrance));
    Harpoon::Instance->SendJsonToRemote(BuildRoundConfigPayload(sLocal.roundWinSeconds));

    // 3) Pick a random spawn point on this map and broadcast the spawn.
    const MapDef* m = GetMap(mapIdx);
    if (m != nullptr && !m->spawnPoints.empty()) {
        s32 spIdx = (s32)(rand() % m->spawnPoints.size());
        const SpawnPoint& sp = m->spawnPoints[spIdx];
        sLocal.currentSpawn = spIdx;
        sLocal.triforceX = sp.x;
        sLocal.triforceY = sp.y;
        sLocal.triforceZ = sp.z;
        Harpoon::Instance->SendJsonToRemote(
            BuildTriforceSpawnPayload(mapIdx, spIdx, sp.x, sp.y, sp.z));
        // 4) Round-start cutscene — broadcast so every peer kicks off the
        //    Triforce-appear subcamera orbit at the same world position.
        //    Local apply too, since the server-relay excludes the sender.
        sLocal.cutsceneTimer  = 100;
        sLocal.cutsceneReady  = false;
        sLocal.cutsceneSubCam = SUBCAM_FREE;
        Harpoon::Instance->SendJsonToRemote(
            BuildCutsceneBeginPayload(sp.x, sp.y, sp.z));
    }

    // 5) Reset peer-side vote flags + carrier mirrors so next round starts clean.
    for (auto& [cid, c] : Harpoon::Instance->clients) {
        c.hasVoted = false;
        c.ttRupeesRemaining = 0;
    }
    sLocal.mapVoteDeadline    = 0;
    sLocal.idleOnGroundFrames = 0;
}

namespace {

// Integrate one physics step for the airborne Triforce. Reads/writes
// sLocal.triforce{X,Y,Z} and sLocal.dropVel{X,Y,Z}. Uses the engine's
// BgCheck_* queries to detect wall / floor / ceiling hits and reflects
// the velocity so it bounces and eventually settles. Returns true once
// the Triforce has come to rest (caller should clear dropFlyTimer).
//
// Tunables (chosen by feel; tweak if it lands too short / long):
//   GRAVITY          : -1.5 units/frame²  (Y velocity decrement)
//   WALL_RADIUS      :  20 units          (sphere-vs-wall query radius)
//   WALL_HEIGHT      :  30 units          (vertical extent of the sphere)
//   CEILING_HEIGHT   :  30 units          (head-clearance test offset)
//   WALL_DAMP        :  0.65              (energy retained after wall hit)
//   FLOOR_DAMP       :  0.5               (vy reversed and damped on floor)
//   SETTLE_SPEED_SQ  :  2.5²              (|vxz|² + |vy|² threshold to stop)
bool StepDropPhysics() {
    if (gPlayState == nullptr) return false;

    constexpr f32 GRAVITY        = -1.5f;
    constexpr f32 WALL_RADIUS    = 20.0f;
    constexpr f32 WALL_HEIGHT    = 30.0f;
    constexpr f32 CEILING_HEIGHT = 30.0f;
    constexpr f32 WALL_DAMP      = 0.65f;
    constexpr f32 FLOOR_DAMP     = 0.5f;
    constexpr f32 SETTLE_SPEED   = 2.5f;

    // Save previous (wall query needs both prev + next positions).
    sLocal.dropPrevX = sLocal.triforceX;
    sLocal.dropPrevY = sLocal.triforceY;
    sLocal.dropPrevZ = sLocal.triforceZ;

    // Apply gravity and integrate.
    sLocal.dropVelY += GRAVITY;
    Vec3f nextPos = {
        sLocal.triforceX + sLocal.dropVelX,
        sLocal.triforceY + sLocal.dropVelY,
        sLocal.triforceZ + sLocal.dropVelZ
    };

    // (1) Wall sweep — slide+reflect against scene walls. Uses the prev
    //     position so the engine can step along the segment and find the
    //     first wall hit between frames.
    Vec3f prevPos = { sLocal.dropPrevX, sLocal.dropPrevY, sLocal.dropPrevZ };
    Vec3f resolvedPos = nextPos;
    CollisionPoly* wallPoly = nullptr;
    s32 wallBgId = 0;
    if (BgCheck_EntitySphVsWall3(&gPlayState->colCtx, &resolvedPos, &nextPos,
                                 &prevPos, WALL_RADIUS, &wallPoly, &wallBgId,
                                 nullptr, WALL_HEIGHT)) {
        if (wallPoly != nullptr) {
            // Reflect horizontal velocity across wall normal. OoT wall
            // normals are unit Vec3s scaled by 0x7FFF in some builds and
            // raw f32 in others — z64bgcheck.h declares them as f32 in
            // CollisionPoly::normal, so we use them directly.
            f32 nx = wallPoly->normal.x / 32767.0f;
            f32 nz = wallPoly->normal.z / 32767.0f;
            f32 dot = sLocal.dropVelX * nx + sLocal.dropVelZ * nz;
            sLocal.dropVelX = (sLocal.dropVelX - 2.0f * dot * nx) * WALL_DAMP;
            sLocal.dropVelZ = (sLocal.dropVelZ - 2.0f * dot * nz) * WALL_DAMP;
        }
        nextPos = resolvedPos;
    }

    // (2) Ceiling — invert vy on hit (and clamp Y down to the safe value).
    f32 ceilY = 0.0f;
    CollisionPoly* ceilPoly = nullptr;
    s32 ceilBgId = 0;
    if (sLocal.dropVelY > 0.0f &&
        BgCheck_EntityCheckCeiling(&gPlayState->colCtx, &ceilY, &nextPos,
                                   CEILING_HEIGHT, &ceilPoly, &ceilBgId, nullptr)) {
        nextPos.y = ceilY;
        sLocal.dropVelY = -sLocal.dropVelY * FLOOR_DAMP;
    }

    // (3) Floor — snap to floor Y when we'd fall below it; bounce or settle.
    CollisionPoly* floorPoly = nullptr;
    s32 floorBgId = 0;
    f32 floorY = BgCheck_EntityRaycastFloor4(&gPlayState->colCtx, &floorPoly,
                                              &floorBgId, nullptr, &nextPos);
    bool onFloor = false;
    if (floorY > BGCHECK_Y_MIN && nextPos.y <= floorY + 0.5f) {
        nextPos.y = floorY;
        onFloor = true;
        if (sLocal.dropVelY < 0.0f) {
            sLocal.dropVelY = -sLocal.dropVelY * FLOOR_DAMP;
        }
        // Floor friction so it doesn't slide forever.
        sLocal.dropVelX *= 0.85f;
        sLocal.dropVelZ *= 0.85f;
    }

    sLocal.triforceX = nextPos.x;
    sLocal.triforceY = nextPos.y;
    sLocal.triforceZ = nextPos.z;

    // Settle condition: on floor + low total speed.
    f32 speedSq = sLocal.dropVelX * sLocal.dropVelX +
                  sLocal.dropVelY * sLocal.dropVelY +
                  sLocal.dropVelZ * sLocal.dropVelZ;
    return onFloor && speedSq < (SETTLE_SPEED * SETTLE_SPEED);
}

// Called each frame; returns when ttl runs out or physics settle.
void AnimateDropFly() {
    if (sLocal.dropFlyTimer <= 0) return;
    bool settled = StepDropPhysics();
    sLocal.dropFlyTimer--;
    if (settled || sLocal.dropFlyTimer == 0) {
        sLocal.dropFlyTimer = 0;
        sLocal.dropVelX = sLocal.dropVelY = sLocal.dropVelZ = 0.0f;
    }
}

// Mirror of Scooter's TriforceThief_UpdateResourceDrops. Fires every 60
// frames when the local player is NOT the carrier — +1 ammo per slot
// (capped at upgrade max), +4 magic, +4 health (1 quarter-heart/sec).
void TickPassiveRegen() {
    if (IsLocalCarrier()) return;
    if (gPlayState == nullptr) return;
    if (gSaveContext.gameMode != GAMEMODE_NORMAL) return;

    sLocal.regenTickCounter++;
    if (sLocal.regenTickCounter < 60) return;
    sLocal.regenTickCounter = 0;

    // --- Ammo refill (capped at upgrade maxes). CAPACITY() reads the
    // gUpgradeCapacities table for the current upgrade level.
    s32 maxBombs   = CAPACITY(UPG_BOMB_BAG,   CUR_UPG_VALUE(UPG_BOMB_BAG));
    s32 maxArrows  = CAPACITY(UPG_QUIVER,    CUR_UPG_VALUE(UPG_QUIVER));
    s32 maxSeeds   = CAPACITY(UPG_BULLET_BAG,CUR_UPG_VALUE(UPG_BULLET_BAG));
    s32 maxNuts    = CAPACITY(UPG_NUTS,      CUR_UPG_VALUE(UPG_NUTS));
    s32 maxSticks  = CAPACITY(UPG_STICKS,    CUR_UPG_VALUE(UPG_STICKS));
    auto bump = [](s32 slot, s32 maxVal) {
        if (slot < 0 || slot >= (s32)ARRAY_COUNT(gSaveContext.inventory.ammo)) return;
        if (gSaveContext.inventory.ammo[slot] < maxVal) {
            gSaveContext.inventory.ammo[slot]++;
        }
    };
    bump(SLOT_BOMB,      maxBombs);
    bump(SLOT_BOW,       maxArrows);
    bump(SLOT_SLINGSHOT, maxSeeds);
    bump(SLOT_NUT,       maxNuts);
    bump(SLOT_STICK,     maxSticks);
    bump(SLOT_BOMBCHU,   50);  // chu count is independent of bomb bag

    // --- Magic refill (+4 per tick).
    if (gSaveContext.isMagicAcquired) {
        s16 maxMagic = gSaveContext.isDoubleMagicAcquired ? 96 : 48;
        if (gSaveContext.magic < maxMagic) {
            s32 newMagic = gSaveContext.magic + 4;
            gSaveContext.magic = (s16)((newMagic > maxMagic) ? maxMagic : newMagic);
        }
    }

    // --- HP refill (+4 per tick = 1 quarter-heart/sec).
    if (gSaveContext.health < gSaveContext.healthCapacity) {
        s32 newHealth = gSaveContext.health + 4;
        gSaveContext.health = (s16)((newHealth > gSaveContext.healthCapacity)
                                     ? gSaveContext.healthCapacity
                                     : newHealth);
    }
}

// (Auto-drop on damage is handled by the engine's OnPlayerHealthChange
// hook in HarpoonHookHandlers.cpp — fires exactly once per damage event
// and avoids the false-positives of frame-polling gSaveContext.health.)

}  // anon

// Per-frame TT logic. Responsibilities:
//   (a) carrier-only: drain 1 rupee per 60 frames; on 0 → broadcast
//       ROUND_RESULT and tear down to lobby.
//   (b) host-only during MAP_SELECT + everyone_votes: tally votes when
//       every online peer has voted, then HostConfirmMap(winner).
//   (c) animate Triforce fly-away, decrement per-client cooldowns.
//   (d) auto-drop when the local carrier takes damage.
//   (e) passive regen (ammo/magic/HP) when not carrier.
// Tick the Triforce-appear cutscene. While active:
//   - player is frozen (PLAYER_STATE1_IN_CUTSCENE) so they can't move
//   - subcamera orbits the Triforce position (radius 300, height 150)
//   - timer counts down only when cutsceneReady (scene done loading)
//   - on timer 0: tear down subcam, unfreeze player, reset state
// Camera math mirrors Scooter: angle = timer * 0.06f, look-at offset y+30.
void TickCutscene() {
    if (sLocal.cutsceneTimer <= 0) return;
    if (gPlayState == nullptr) return;
    Player* localPlayer = GET_PLAYER(gPlayState);
    if (localPlayer == nullptr) return;

    // Freeze the player throughout the cutscene (including scene-load wait).
    localPlayer->stateFlags1 |= PLAYER_STATE1_IN_CUTSCENE;

    if (!sLocal.cutsceneReady) {
        // Scene still loading — keep player frozen, hold the timer.
        return;
    }

    // Create subcamera on the first ready frame.
    if (sLocal.cutsceneSubCam == SUBCAM_FREE) {
        sLocal.cutsceneSubCam = Play_CreateSubCamera(gPlayState);
        if (sLocal.cutsceneSubCam == SUBCAM_FREE) {
            // Engine ran out of camera slots — bail without freezing forever.
            sLocal.cutsceneTimer = 0;
            localPlayer->stateFlags1 &= ~PLAYER_STATE1_IN_CUTSCENE;
            return;
        }
        Play_ChangeCameraStatus(gPlayState, MAIN_CAM, CAM_STAT_WAIT);
        Play_ChangeCameraStatus(gPlayState, sLocal.cutsceneSubCam, CAM_STAT_ACTIVE);
        // Audio cue on the first ready frame.
        Sfx_PlaySfxCentered(NA_SE_EV_TRIFORCE_FLASH);
    }

    // Orbit the camera around the Triforce.
    f32 angle = (f32)sLocal.cutsceneTimer * 0.06f;
    f32 camDist   = 300.0f;
    f32 camHeight = 150.0f;
    Vec3f at  = { sLocal.triforceX, sLocal.triforceY + 30.0f, sLocal.triforceZ };
    Vec3f eye = {
        sLocal.triforceX + sinf(angle) * camDist,
        sLocal.triforceY + camHeight,
        sLocal.triforceZ + cosf(angle) * camDist
    };
    Play_CameraSetAtEye(gPlayState, sLocal.cutsceneSubCam, &at, &eye);

    // Countdown — tear down on 0.
    sLocal.cutsceneTimer--;
    if (sLocal.cutsceneTimer <= 0) {
        if (sLocal.cutsceneSubCam != SUBCAM_FREE) {
            Play_ChangeCameraStatus(gPlayState, MAIN_CAM, CAM_STAT_ACTIVE);
            Play_ClearCamera(gPlayState, sLocal.cutsceneSubCam);
            sLocal.cutsceneSubCam = SUBCAM_FREE;
        }
        sLocal.cutsceneReady = false;
        localPlayer->stateFlags1 &= ~PLAYER_STATE1_IN_CUTSCENE;
    }
}

void TickFrame() {
    if (Harpoon::Instance == nullptr || !Harpoon::Instance->isConnected) return;

    // (a0) Triforce-appear cutscene (round start). Runs before everything
    //      else so we don't process pickups / damage while frozen.
    TickCutscene();

    // (c) animate fly + decrement cooldowns.
    AnimateDropFly();
    for (auto it = sLocal.pickupCooldownByCid.begin();
              it != sLocal.pickupCooldownByCid.end(); ) {
        if (--it->second <= 0) it = sLocal.pickupCooldownByCid.erase(it);
        else ++it;
    }

    // (d) passive regen for non-carriers (only during a live round).
    //     Auto-drop on damage lives in the OnPlayerHealthChange engine hook.
    if (sLocal.inRound && !sLocal.roundEnded) {
        TickPassiveRegen();
    }

    // (a) Carrier rupee drain.
    if (sLocal.inRound && !sLocal.roundEnded && IsLocalCarrier()) {
        if (--sLocal.drainTickCounter <= 0) {
            sLocal.drainTickCounter = 60;
            if (sLocal.carrierRupeesRemaining > 0) {
                sLocal.carrierRupeesRemaining--;
            }
            gSaveContext.rupees = sLocal.carrierRupeesRemaining;
            // Broadcast every second so peers' arrow labels stay live.
            Harpoon::Instance->SendJsonToRemote(
                BuildCarrierTimerSyncPayload(sLocal.carrierRupeesRemaining));

            if (sLocal.carrierRupeesRemaining <= 0) {
                sLocal.roundEnded = true;
                u32 winner = Harpoon::Instance->ownClientId;
                Harpoon::Instance->SendJsonToRemote(
                    BuildRoundResultPayload(winner, sLocal.roundIndex));
                // Apply locally too — relay excludes the sender.
                nlohmann::json local;
                local["winnerClientId"] = winner;
                local["roundIndex"]     = sLocal.roundIndex;
                HandleRoundResult(local);
            }
        }
    }

    // (b) Host map-vote tally. Two ways the host can resolve a vote:
    //     - everybody marked hasVoted (including self via overlay A-press)
    //     - the deadline expired (default 30 sec from MAP_SELECT_BEGIN)
    //   On tally, only actual voters count — no phantom default-0 vote for
    //   anyone who didn't press A. If nobody voted at all, pick random so
    //   we never stall.
    bool isHost = (Harpoon::Instance->ownClientId != 0 &&
                   Harpoon::Instance->ownClientId == Harpoon::Instance->hostClientId);
    if (isHost &&
        Harpoon::Instance->gameState   == HARPOON_STATE_MAP_SELECT &&
        Harpoon::Instance->mapSelectMode == MAP_SELECT_EVERYONE_CHOOSES) {
        if (sLocal.mapVoteDeadline > 0) sLocal.mapVoteDeadline--;

        // Pull-style poll: every online player (host included) must press A
        // to cast a vote within `mapVoteDeadline` frames (15s default).
        // - All voted → tally immediately
        // - Deadline expired → tally with whoever voted; non-voters drop
        // The most-voted map wins (ties → lowest index). If absolutely
        // nobody voted by the deadline, pick random so we never stall.
        bool allVoted = true;
        s32 onlineCount = 0;
        for (auto& [cid, c] : Harpoon::Instance->clients) {
            if (!c.online) continue;
            onlineCount++;
            if (!c.hasVoted) { allVoted = false; }
        }
        bool timeout = (sLocal.mapVoteDeadline <= 0);
        if ((allVoted && onlineCount > 0) || timeout) {
            std::unordered_map<s32, s32> tally;
            for (auto& [cid, c] : Harpoon::Instance->clients) {
                if (!c.online) continue;
                if (c.hasVoted) {            // strict: only counted votes
                    tally[c.mapSelectIndex]++;
                }
            }
            s32 winner = 0;
            if (tally.empty()) {
                // No votes by deadline — pick random so we never stall.
                winner = (s32)(rand() % kMapCount);
            } else {
                s32 best = -1;
                for (auto& [idx, count] : tally) {
                    if (count > best || (count == best && idx < winner)) {
                        best   = count;
                        winner = idx;
                    }
                }
            }
            HostConfirmMap(winner);  // also resets mapVoteDeadline to 0
        }
    }

    // (f) Idle / void respawn. While the Triforce is on the ground and
    //     no one's carrying it, count frames; after 10 sec respawn it at
    //     a random spawn point. Also respawn immediately if the Y has
    //     dropped well below the original ground (void / pit).
    if (sLocal.inRound && !sLocal.roundEnded && sLocal.carrierClientId == 0 &&
        sLocal.dropFlyTimer == 0 && Harpoon::Instance != nullptr) {
        const MapDef* m = GetMap(sLocal.confirmedMap);
        bool inVoid = false;
        if (m != nullptr && sLocal.currentSpawn >= 0 &&
            (size_t)sLocal.currentSpawn < m->spawnPoints.size()) {
            f32 spawnY = m->spawnPoints[sLocal.currentSpawn].y;
            if (sLocal.triforceY < spawnY - 200.0f) inVoid = true;
        }
        sLocal.idleOnGroundFrames++;
        if (inVoid || sLocal.idleOnGroundFrames >= 600) {
            sLocal.idleOnGroundFrames = 0;
            // Only the host respawns it so we don't double-spawn. Peers
            // receive TRIFORCE_SPAWN as usual.
            bool isHostNow = (Harpoon::Instance->ownClientId != 0 &&
                              Harpoon::Instance->ownClientId == Harpoon::Instance->hostClientId);
            if (isHostNow && m != nullptr && !m->spawnPoints.empty()) {
                s32 idx = (s32)(rand() % m->spawnPoints.size());
                const SpawnPoint& sp = m->spawnPoints[idx];
                sLocal.currentSpawn = idx;
                sLocal.triforceX = sp.x;
                sLocal.triforceY = sp.y;
                sLocal.triforceZ = sp.z;
                Harpoon::Instance->SendJsonToRemote(
                    BuildTriforceSpawnPayload(sLocal.confirmedMap, idx, sp.x, sp.y, sp.z));
            }
        }
    } else {
        sLocal.idleOnGroundFrames = 0;
    }
}

// "Big" game start — title / file select → into gameplay as adult Link
// with the full thief inventory, sitting in Hyrule Field as the round
// lobby. Mirrors HarpoonPropHunt::BigStartGameAs.
void BigStartGame() {
    Save_InitFile(false);
    // 0xFD = Harpoon multiplayer sentinel. See PropHunt.cpp / Harpoon.cpp:
    // using a real slot causes autosave to clobber the user's save on disk.
    gSaveContext.fileNum = 0xFD;

    ApplyThiefSave();

    // Standard gSaveContext fields the engine expects post-load.
    gSaveContext.gameMode = GAMEMODE_NORMAL;
    gSaveContext.respawn[0].entranceIndex = ENTR_LOAD_OPENING;
    gSaveContext.respawnFlag = 0;
    gSaveContext.seqId = (u8)NA_BGM_DISABLED;
    gSaveContext.natureAmbienceId = 0xFF;
    gSaveContext.showTitleCard = true;
    gSaveContext.dogParams = 0;
    gSaveContext.timerState = TIMER_STATE_OFF;
    gSaveContext.subTimerState = SUBTIMER_STATE_OFF;
    gSaveContext.eventInf[0] = 0;
    gSaveContext.eventInf[1] = 0;
    gSaveContext.eventInf[2] = 0;
    gSaveContext.eventInf[3] = 0;
    gSaveContext.unk_13EE = 0x32;
    gSaveContext.nayrusLoveTimer = 0;
    gSaveContext.healthAccumulator = 0;
    gSaveContext.magicState = MAGIC_STATE_IDLE;
    gSaveContext.prevMagicState = MAGIC_STATE_IDLE;
    gSaveContext.forcedSeqId = NA_BGM_GENERAL_SFX;
    gSaveContext.skyboxTime = 0;
    gSaveContext.nextTransitionType = TRANS_NEXT_TYPE_DEFAULT;
    gSaveContext.nextCutsceneIndex = 0xFFEF;
    gSaveContext.cutsceneTrigger = 0;
    gSaveContext.chamberCutsceneNum = 0;
    gSaveContext.nextDayTime = 0xFFFF;
    gSaveContext.retainWeatherMode = 0;
    for (size_t i = 0; i < ARRAY_COUNT(gSaveContext.buttonStatus); i++) {
        gSaveContext.buttonStatus[i] = BTN_ENABLED;
    }
    gSaveContext.forceRisingButtonAlphas = 0;
    gSaveContext.unk_13E8 = 0;
    gSaveContext.unk_13EA = 0;
    gSaveContext.unk_13EC = 0;
    gSaveContext.magicCapacity = 0;
    gSaveContext.magicFillTarget = gSaveContext.magic;
    gSaveContext.naviTimer = 0;

    // TT is always adult Link per the thief preset.
    gSaveContext.linkAge = LINK_AGE_ADULT;

    Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_STOP);
    if (gGameState != nullptr) {
        gGameState->running = false;
        SET_NEXT_GAMESTATE(gGameState, Play_Init, PlayState);
    }
    GameInteractor_ExecuteOnLoadGame(gSaveContext.fileNum);

    SPDLOG_INFO("[Harpoon][TriforceThief] BigStartGame entrance=0x{:X}",
                (u32)gSaveContext.entranceIndex);
}

}  // namespace HarpoonTriforceThief

// =============================================================================
// C bridge
// =============================================================================

extern "C" {

s32 HarpoonTriforceThief_IsLoaded(void)         { return HarpoonTriforceThief::IsLoaded() ? 1 : 0; }
s32 HarpoonTriforceThief_IsLocalCarrier(void)   { return HarpoonTriforceThief::IsLocalCarrier() ? 1 : 0; }
s32 HarpoonTriforceThief_GetConfirmedMap(void)  { return HarpoonTriforceThief::GetLocalState().confirmedMap; }
s32 HarpoonTriforceThief_GetEntranceForMap(s32 mapIdx) {
    return HarpoonTriforceThief::GetEntranceForMap(mapIdx);
}

}  // extern "C"
