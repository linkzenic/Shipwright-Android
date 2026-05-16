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
#include "soh/Enhancements/enhancementTypes.h"  // BUNNY_HOOD_VANILLA / BUNNY_HOOD_FAST_AND_JUMP
extern "C" void Save_InitFile(int isDebug);

// Forward declaration — defined in soh/ResourceManagerHelpers.cpp. No public
// header exposes this prototype, so we declare it locally (same pattern as
// pak_loader.cpp and mods/items/logic/item_postman_hat.c).
extern "C" Gfx* ResourceMgr_LoadGfxByName(const char* path);

#include <cmath>
#include <algorithm>

// =============================================================================
// File-scope storage
// =============================================================================

namespace {

std::vector<HarpoonTriforceThief::MapDef> sMaps;
HarpoonTriforceThief::LocalState         sLocal;
nlohmann::json                           sSavePresetRaw;
bool                                     sLoaded = false;
s16                                       sTriforceSpinAngle = 0;

// Final-standings leaderboard. Populated by HandleRoundResult from the
// `leaderboard` array in the ROUND_RESULT payload. Sorted desc by
// carrySecs; ties share placement (rank stays the same, next rank
// skips ahead by the size of the tied group). Cleared in
// LocallyConfirmMap when the host starts the next round.
struct LeaderboardRow {
    u32         clientId;
    std::string name;
    s32         carrySecs;
    s32         rank;
};
std::vector<LeaderboardRow> sLeaderboard;

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
// Per-round scene-lock cluster (parity with PropHunt). One entry per TT map.
// All 6 TT maps are strict single-scene — no sub-area extensions.
// ---------------------------------------------------------------------------

namespace {
struct ClusterDef { const s8* scenes; s32 count; };

static const s8 sCluster_HyruleField[] = { SCENE_HYRULE_FIELD };
static const s8 sCluster_ZorasRiver[]  = { SCENE_ZORAS_RIVER };
static const s8 sCluster_Gerudo[]      = { SCENE_GERUDOS_FORTRESS };
static const s8 sCluster_Kokiri[]      = { SCENE_KOKIRI_FOREST };
static const s8 sCluster_Kakariko[]    = { SCENE_KAKARIKO_VILLAGE };
static const s8 sCluster_SFM[]         = { SCENE_SACRED_FOREST_MEADOW };

static const ClusterDef sClusterByMap[HarpoonTriforceThief::kMapCount] = {
    { sCluster_HyruleField, (s32)ARRAY_COUNT(sCluster_HyruleField) },
    { sCluster_ZorasRiver,  (s32)ARRAY_COUNT(sCluster_ZorasRiver)  },
    { sCluster_Gerudo,      (s32)ARRAY_COUNT(sCluster_Gerudo)      },
    { sCluster_Kokiri,      (s32)ARRAY_COUNT(sCluster_Kokiri)      },
    { sCluster_Kakariko,    (s32)ARRAY_COUNT(sCluster_Kakariko)    },
    { sCluster_SFM,         (s32)ARRAY_COUNT(sCluster_SFM)         },
};
}  // anon

bool IsSceneInRoundClusterTT(s32 mapIndex, s32 sceneNum) {
    if (mapIndex < 0 || mapIndex >= (s32)ARRAY_COUNT(sClusterByMap)) return false;
    const ClusterDef& def = sClusterByMap[mapIndex];
    for (s32 i = 0; i < def.count; i++) {
        if ((s32)def.scenes[i] == sceneNum) return true;
    }
    return false;
}

s32 GetReturnEntranceForInvalidExitTT(s32 mapIndex, s32 destSceneNum) {
    // v1: redirect to the round map's main entrance. Same simplification
    // as PropHunt. Per-(map, dest) precision can be layered later.
    (void)destSceneNum;
    return GetEntranceForMap(mapIndex);
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

// Forward declaration — `BuildLeaderboardFromPayload` is defined in a
// later anonymous namespace block (after `BuildRoundResultPayload`).
// `HandleRoundResult` below calls it, so without this forward declare
// the call doesn't resolve.
void BuildLeaderboardFromPayload(const nlohmann::json& p);

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

    // Reset per-round state that carries over from the previous round.
    // Without these resets, peers enter round 2+ with `roundEnded = true`
    // from the previous HandleRoundResult — the leaderboard modal stays
    // up, pickup paths that gate on `!roundEnded` reject every attempt,
    // and the carrier rupee drain doesn't even tick. The host clears all
    // of this in HostConfirmMap; peers need the same in this handler.
    sLocal.roundEnded             = false;
    sLocal.carrierClientId        = 0;
    sLocal.carrierRupeesRemaining = 0;
    sLocal.drainTickCounter       = 0;
    sLocal.preRoundCountdownFrames = 60;       // 3-second GET READY
    sLocal.triforceLanded         = true;
    sLocal.appliedBunnyHood       = false;
    sLocal.goFlashFrames          = 0;
    sLocal.dropFlyTimer           = 0;
    sLocal.dropVelX = sLocal.dropVelY = sLocal.dropVelZ = 0.0f;
    sLocal.pickupCooldownByCid.clear();
    sLocal.idleOnGroundFrames     = 0;
    sLeaderboard.clear();
    // Reset peer mirrors so leaderboard data + HUD don't show stale values.
    if (Harpoon::Instance != nullptr) {
        for (auto& [cid, c] : Harpoon::Instance->clients) {
            c.ttRupeesRemaining = 0;
        }
    }

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
    // Silent round end (parity with PropHunt's HandleRoundResult). Per user
    // spec: no winner notification text — players just see the leaderboard
    // modal. The winnerClientId is still logged for diagnostics.
    u32 winner = p.value("winnerClientId", 0u);
    s32 round  = p.value("roundIndex", sLocal.roundIndex);
    SPDLOG_INFO("[Harpoon][TriforceThief] round {} ended winner=cid{} (silent)",
                round, winner);
    (void)winner;

    // Build the leaderboard so DrawHud can render the centered modal.
    // Stays visible until LocallyConfirmMap / HostConfirmMap clears it on
    // the next round start.
    BuildLeaderboardFromPayload(p);

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

    // Embed a per-client leaderboard snapshot. Carry seconds is derived
    // from the rupee-drain timer: `roundWinSeconds - rupeesRemaining`.
    // Local row uses `carrierRupeesRemaining` (authoritative for us);
    // peer rows use `ttRupeesRemaining` (mirrored from their
    // CARRIER_TIMER_SYNC heartbeats).
    nlohmann::json lb = nlohmann::json::array();
    if (Harpoon::Instance != nullptr) {
        for (auto& [cid, c] : Harpoon::Instance->clients) {
            if (!c.online) continue;
            s32 rem;
            if (cid == Harpoon::Instance->ownClientId) {
                rem = sLocal.carrierRupeesRemaining;
            } else {
                rem = c.ttRupeesRemaining;
            }
            // Non-carriers / never-carried players show as 0 carry secs
            // (their counter is the initial 0 — treat as "never decremented
            // from full" by clamping to roundWinSeconds).
            if (rem <= 0) rem = sLocal.roundWinSeconds;
            s32 carrySecs = sLocal.roundWinSeconds - rem;
            if (carrySecs < 0) carrySecs = 0;
            nlohmann::json row;
            row["clientId"]  = cid;
            row["carrySecs"] = carrySecs;
            lb.push_back(row);
        }
    }
    d["leaderboard"] = lb;
    return _Envelope(kEvtRoundResult, std::move(d));
}

namespace {
// Build sLeaderboard from a ROUND_RESULT payload. Sort descending by
// carrySecs, then assign shared placement to ties (e.g. two 1st-place
// rows both get rank 1; the next group's rank is 3).
void BuildLeaderboardFromPayload(const nlohmann::json& p) {
    sLeaderboard.clear();
    if (!p.contains("leaderboard") || !p["leaderboard"].is_array()) return;
    for (const auto& e : p["leaderboard"]) {
        LeaderboardRow r;
        r.clientId  = e.value("clientId", 0u);
        r.carrySecs = e.value("carrySecs", 0);
        std::string fallback = "cid" + std::to_string(r.clientId);
        if (Harpoon::Instance != nullptr) {
            auto it = Harpoon::Instance->clients.find(r.clientId);
            if (it != Harpoon::Instance->clients.end() &&
                !it->second.name.empty()) {
                r.name = it->second.name;
            } else {
                r.name = fallback;
            }
        } else {
            r.name = fallback;
        }
        r.rank = 0;
        sLeaderboard.push_back(r);
    }
    std::sort(sLeaderboard.begin(), sLeaderboard.end(),
              [](const LeaderboardRow& a, const LeaderboardRow& b) {
                  return a.carrySecs > b.carrySecs;
              });
    // Shared placement on ties: 1, 1, 3, 3, 5, ...
    s32 rank = 1, sameCount = 0;
    s32 prevSecs = -1;
    for (auto& r : sLeaderboard) {
        if (r.carrySecs != prevSecs) {
            rank += sameCount;
            sameCount = 0;
        }
        r.rank = rank;
        sameCount++;
        prevSecs = r.carrySecs;
    }
}
}  // anon

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

        // Triforce HUD icon — single always-visible indicator. If the
        // Triforce world position projects to a point inside the viewport,
        // draw the icon there (visible through walls — ImGui foreground
        // sits on top of the 3D scene). If off-screen (behind camera or
        // outside the viewport rect), snap to the nearest screen edge
        // with a small rotation arrow pointing toward the off-screen
        // Triforce. Replaces the previous per-player compass arrows.
        if (gPlayState != nullptr && Harpoon::Instance != nullptr) {
            Vec3f world = { sLocal.triforceX, sLocal.triforceY + 30.0f,
                            sLocal.triforceZ };
            // If the Triforce is currently being carried, draw above the
            // carrier's head instead of at the stale ground coord.
            if (sLocal.carrierClientId != 0) {
                auto it = Harpoon::Instance->clients.find(sLocal.carrierClientId);
                if (it != Harpoon::Instance->clients.end()) {
                    world.x = it->second.posRot.pos.x;
                    world.y = it->second.posRot.pos.y + 80.0f;
                    world.z = it->second.posRot.pos.z;
                }
            }

            Vec3f proj; f32 w;
            func_8002BE04(gPlayState, &world, &proj, &w);
            auto vp = ImGui::GetMainViewport();
            ImDrawList* fg = ImGui::GetForegroundDrawList(vp);

            // NDC → viewport pixels. w<=0 means the target is behind the
            // camera; clamp it to a tiny positive number so we can still
            // derive a stable direction for the edge-snap path.
            bool behind = (w <= 0.0f);
            f32 ndcX = proj.x * w;
            f32 ndcY = proj.y * w * -1.0f;
            f32 vpx  = vp->Pos.x + vp->Size.x * (ndcX + 1.0f) * 0.5f;
            f32 vpy  = vp->Pos.y + vp->Size.y * (ndcY + 1.0f) * 0.5f;
            f32 cx   = vp->Pos.x + vp->Size.x * 0.5f;
            f32 cy   = vp->Pos.y + vp->Size.y * 0.5f;

            bool onScreen = !behind &&
                            vpx >= vp->Pos.x && vpx <= vp->Pos.x + vp->Size.x &&
                            vpy >= vp->Pos.y && vpy <= vp->Pos.y + vp->Size.y;

            auto drawTriforceIcon = [&](f32 px, f32 py, f32 scale) {
                // Three small filled triangles arranged in the Triforce
                // pattern (top + bottom-left + bottom-right). Gold.
                ImU32 gold = IM_COL32(255, 215, 0, 235);
                f32 s = 12.0f * scale;
                // Top triangle
                fg->AddTriangleFilled(ImVec2(px,         py - s),
                                       ImVec2(px - s*0.5f, py),
                                       ImVec2(px + s*0.5f, py),
                                       gold);
                // Bottom-left
                fg->AddTriangleFilled(ImVec2(px - s*0.5f, py),
                                       ImVec2(px - s,     py + s),
                                       ImVec2(px,         py + s),
                                       gold);
                // Bottom-right
                fg->AddTriangleFilled(ImVec2(px + s*0.5f, py),
                                       ImVec2(px,         py + s),
                                       ImVec2(px + s,     py + s),
                                       gold);
            };

            if (onScreen) {
                drawTriforceIcon(vpx, vpy, 1.0f);
            } else {
                // Off-screen: snap to nearest edge. Compute direction from
                // viewport center to the projected (possibly off-screen)
                // point and intersect with the viewport rect.
                f32 dx = vpx - cx, dy = vpy - cy;
                // If behind camera, the projection inverts direction —
                // flip so the icon shows on the opposite edge from camera
                // forward (i.e. behind us).
                if (behind) { dx = -dx; dy = -dy; }
                if (fabsf(dx) < 0.001f && fabsf(dy) < 0.001f) {
                    dy = -1.0f; // arbitrary up
                }
                f32 halfW = vp->Size.x * 0.45f;
                f32 halfH = vp->Size.y * 0.45f;
                f32 sx = (fabsf(dx) > 0.001f) ? halfW / fabsf(dx) : 1e9f;
                f32 sy = (fabsf(dy) > 0.001f) ? halfH / fabsf(dy) : 1e9f;
                f32 scl = fminf(sx, sy);
                f32 ex = cx + dx * scl;
                f32 ey = cy + dy * scl;
                drawTriforceIcon(ex, ey, 0.85f);

                // Small directional arrow next to the icon, pointing
                // away from the screen center (i.e. toward the off-screen
                // Triforce). Filled triangle.
                f32 a = atan2f(dy, dx);
                f32 arSize = 8.0f;
                f32 ax = ex + cosf(a) * 18.0f;
                f32 ay = ey + sinf(a) * 18.0f;
                f32 perpX =  sinf(a) * arSize * 0.7f;
                f32 perpY = -cosf(a) * arSize * 0.7f;
                f32 backX = -cosf(a) * arSize;
                f32 backY = -sinf(a) * arSize;
                fg->AddTriangleFilled(
                    ImVec2(ax, ay),
                    ImVec2(ax + backX + perpX, ay + backY + perpY),
                    ImVec2(ax + backX - perpX, ay + backY - perpY),
                    IM_COL32(255, 235, 100, 220));
            }
        }
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                            "Lobby — waiting for host to start.");
    }

    ImGui::End();

    // "GET READY" pre-round countdown — big centered text over the HUD.
    // Decrements in TickFrame; renders 3 / 2 / 1 / GO! as the frames count
    // down from 60 (3 sec at 20 fps).
    if (sLocal.preRoundCountdownFrames > 0) {
        auto vp = ImGui::GetMainViewport();
        ImDrawList* fg = ImGui::GetForegroundDrawList(vp);
        // 60..41 = "3", 40..21 = "2", 20..1 = "1", 0 = (handled by TickFrame which clears)
        const char* label;
        if (sLocal.preRoundCountdownFrames > 40)      label = "3";
        else if (sLocal.preRoundCountdownFrames > 20) label = "2";
        else                                          label = "1";
        ImFont* font = ImGui::GetFont();
        f32 scale = 6.0f;
        ImVec2 ts = font->CalcTextSizeA(font->FontSize * scale, FLT_MAX, 0.0f, label);
        f32 px = vp->Pos.x + vp->Size.x * 0.5f - ts.x * 0.5f;
        f32 py = vp->Pos.y + vp->Size.y * 0.5f - ts.y * 0.5f;
        // Shadow + foreground for legibility.
        fg->AddText(font, font->FontSize * scale, ImVec2(px + 3, py + 3),
                    IM_COL32(0, 0, 0, 200), label);
        fg->AddText(font, font->FontSize * scale, ImVec2(px, py),
                    IM_COL32(255, 215, 0, 255), label);
    } else if (sLocal.goFlashFrames > 0) {
        // One-shot "GO!" flash. Armed by TickFrame when the countdown hits
        // 0; decremented here each frame until it reaches 0 and the flash
        // disappears for the rest of the round.
        sLocal.goFlashFrames--;
        auto vp = ImGui::GetMainViewport();
        ImDrawList* fg = ImGui::GetForegroundDrawList(vp);
        ImFont* font = ImGui::GetFont();
        f32 scale = 6.0f;
        const char* label = "GO!";
        ImVec2 ts = font->CalcTextSizeA(font->FontSize * scale, FLT_MAX, 0.0f, label);
        f32 px = vp->Pos.x + vp->Size.x * 0.5f - ts.x * 0.5f;
        f32 py = vp->Pos.y + vp->Size.y * 0.5f - ts.y * 0.5f;
        u8 alpha = (u8)(255 * sLocal.goFlashFrames / 20);
        fg->AddText(font, font->FontSize * scale, ImVec2(px + 3, py + 3),
                    IM_COL32(0, 0, 0, alpha * 200 / 255), label);
        fg->AddText(font, font->FontSize * scale, ImVec2(px, py),
                    IM_COL32(120, 255, 120, alpha), label);
    }

    // Final-standings leaderboard modal at round end. Centered, persistent
    // until the host starts the next round (sLeaderboard.clear() in
    // LocallyConfirmMap clears it). Populated by HandleRoundResult.
    if (sLocal.roundEnded && !sLeaderboard.empty()) {
        auto vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2(vp->Pos.x + vp->Size.x * 0.5f,
                   vp->Pos.y + vp->Size.y * 0.5f),
            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGuiWindowFlags lbFlags = ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoInputs;
        ImGui::SetNextWindowBgAlpha(0.85f);
        if (ImGui::Begin("Final Standings", nullptr, lbFlags)) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f),
                                "  ROUND OVER  ");
            ImGui::Separator();
            for (const auto& r : sLeaderboard) {
                const char* suffix = (r.rank == 1) ? "st"
                                   : (r.rank == 2) ? "nd"
                                   : (r.rank == 3) ? "rd" : "th";
                ImVec4 col = (r.rank == 1) ? ImVec4(1.0f, 0.85f, 0.2f, 1.0f)
                            : (r.rank == 2) ? ImVec4(0.8f, 0.8f, 0.85f, 1.0f)
                            : (r.rank == 3) ? ImVec4(0.85f, 0.55f, 0.3f, 1.0f)
                                            : ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                ImGui::TextColored(col, "  %d%s  %-16s  %02d:%02d",
                                    r.rank, suffix, r.name.c_str(),
                                    r.carrySecs / 60, r.carrySecs % 60);
            }
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                "  Host starts the next round to dismiss.  ");
        }
        ImGui::End();
    }
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

    // 3-second "GET READY" pre-round countdown. While > 0, TickFrame freezes
    // the local player and short-circuits the rupee drain. 3 s × 20 fps = 60.
    sLocal.preRoundCountdownFrames = 60;
    sLocal.triforceLanded          = true;   // not airborne yet
    sLocal.appliedBunnyHood        = false;

    // Clear last round's leaderboard so the modal stops rendering.
    sLeaderboard.clear();

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
    //     CRITICAL: BgCheck_EntityRaycastFloor4 shoots a ray DOWN from the
    //     query position. If we already dropped BELOW the floor in this
    //     single frame (gravity * |vy| can be 20+ units / tick), the ray
    //     starts below the floor and returns BGCHECK_Y_MIN — the
    //     Triforce phases through the world and falls forever.
    //
    //     Fix: query from a Y above the max of prev/next position so the
    //     ray catches any floor we crossed during this step. Then snap up
    //     if nextPos.y is now below it.
    CollisionPoly* floorPoly = nullptr;
    s32 floorBgId = 0;
    Vec3f floorQueryPos = {
        nextPos.x,
        fmaxf(sLocal.dropPrevY, nextPos.y) + 20.0f,
        nextPos.z
    };
    f32 floorY = BgCheck_EntityRaycastFloor4(&gPlayState->colCtx, &floorPoly,
                                              &floorBgId, nullptr, &floorQueryPos);
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

// Pick a new random spawn point on the current map and teleport the
// Triforce there. Used on out-of-bounds (void / Y below -4000). Host
// broadcasts TRIFORCE_SPAWN so every peer sees the same new location.
void RespawnTriforceRandom() {
    if (Harpoon::Instance == nullptr) return;
    const MapDef* m = GetMap(sLocal.confirmedMap);
    if (m == nullptr || m->spawnPoints.empty()) return;
    s32 idx = (s32)(rand() % m->spawnPoints.size());
    const SpawnPoint& sp = m->spawnPoints[idx];
    sLocal.triforceX = sp.x;
    sLocal.triforceY = sp.y;
    sLocal.triforceZ = sp.z;
    sLocal.dropVelX = sLocal.dropVelY = sLocal.dropVelZ = 0.0f;
    sLocal.dropFlyTimer    = 0;
    sLocal.triforceLanded  = true;
    sLocal.currentSpawn    = idx;
    sLocal.carrierClientId = 0;
    bool isHost = (Harpoon::Instance->ownClientId != 0 &&
                   Harpoon::Instance->ownClientId == Harpoon::Instance->hostClientId);
    if (isHost) {
        Harpoon::Instance->SendJsonToRemote(
            BuildTriforceSpawnPayload(sLocal.confirmedMap, idx, sp.x, sp.y, sp.z));
    }
    SPDLOG_INFO("[Harpoon][TriforceThief] Triforce out-of-bounds — respawned at idx={} "
                "({:.0f}, {:.0f}, {:.0f})", idx, sp.x, sp.y, sp.z);
}

// Called each frame; returns when ttl runs out or physics settle.
void AnimateDropFly() {
    if (sLocal.dropFlyTimer <= 0) return;
    bool settled = StepDropPhysics();
    sLocal.dropFlyTimer--;
    // Out-of-bounds detection: if the Triforce fell into the void, respawn
    // at a fresh random spawn point. Same threshold the engine uses for
    // player void-out in z_player.c:5633.
    if (sLocal.triforceY < -4000.0f) {
        RespawnTriforceRandom();
        return;
    }
    if (settled || sLocal.dropFlyTimer == 0) {
        sLocal.dropFlyTimer    = 0;
        sLocal.dropVelX = sLocal.dropVelY = sLocal.dropVelZ = 0.0f;
        sLocal.triforceLanded  = true;
    } else {
        sLocal.triforceLanded  = false;
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

    // (a-1) "GET READY" pre-round countdown. While > 0, freeze the local
    //       player and skip every other gameplay tick path. Runs BEFORE
    //       the cutscene tick so the cutscene's own freeze logic doesn't
    //       clobber the player state we're forcing here.
    if (sLocal.preRoundCountdownFrames > 0 && gPlayState != nullptr) {
        Player* lp = GET_PLAYER(gPlayState);
        if (lp != nullptr) {
            // Re-apply freeze every tick — the engine may clear it after
            // its own subsystems run.
            lp->actor.freezeTimer = 2;
            lp->stateFlags1 |= PLAYER_STATE1_IN_CUTSCENE;
        }
        sLocal.preRoundCountdownFrames--;
        if (sLocal.preRoundCountdownFrames == 0 && lp != nullptr) {
            // Final tick — release. Arm the one-shot "GO!" flash (decremented
            // in DrawHud; resets to 0 and never re-arms during this round).
            lp->stateFlags1 &= ~PLAYER_STATE1_IN_CUTSCENE;
            lp->actor.freezeTimer = 0;
            sLocal.goFlashFrames = 20;  // 1 sec at 20 fps
        }
        return;  // skip everything else during pre-round
    }

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

    // (a.5) Bunny Hood speed buff. The engine's speed math at z_player.c:7829
    //       is gated on TWO conditions:
    //         (1) player->currentMask == PLAYER_MASK_BUNNY
    //         (2) CVar MMBunnyHood == BUNNY_HOOD_FAST_AND_JUMP
    //       Additionally, Player_Update at z_player.c:2637-2651 CLEARS
    //       currentMask each frame if the Bunny Hood isn't equipped on a
    //       C-button (or D-pad with DpadEquips). Setting currentMask alone
    //       gets wiped on the next frame — the buff lasts ~1 frame.
    //
    //       Full fix: while carrying, we (a) force the CVar to FAST_AND_JUMP,
    //       (b) write ITEM_MASK_BUNNY to a C-button slot, (c) write
    //       PLAYER_MASK_BUNNY to currentMask. On drop we restore all three.
    if (gPlayState != nullptr) {
        Player* lp = GET_PLAYER(gPlayState);
        if (lp != nullptr) {
            bool shouldBuff =
                (sLocal.inRound && !sLocal.roundEnded && IsLocalCarrier());
            if (shouldBuff && !sLocal.appliedBunnyHood) {
                sLocal.savedMaskBeforeBuff = lp->currentMask;
                sLocal.savedBunnyHoodCVar  = CVarGetInteger(
                    CVAR_ENHANCEMENT("MMBunnyHood"), BUNNY_HOOD_VANILLA);
                CVarSetInteger(CVAR_ENHANCEMENT("MMBunnyHood"),
                                BUNNY_HOOD_FAST_AND_JUMP);
                // Hijack a C-button slot (CDown = slot 2) so the mask-sync
                // at z_player.c:2637 sees the bunny mask on a button.
                sLocal.buffMaskSlot      = 2;
                sLocal.savedMaskSlotItem = gSaveContext.equips.buttonItems[2];
                gSaveContext.equips.buttonItems[2] = ITEM_MASK_BUNNY;
                sLocal.appliedBunnyHood = true;
            }
            if (sLocal.appliedBunnyHood) {
                if (shouldBuff) {
                    // Reassert each frame in case some other system clears
                    // the slot (e.g. inventory refresh on scene change).
                    if (sLocal.buffMaskSlot >= 0 &&
                        gSaveContext.equips.buttonItems[sLocal.buffMaskSlot] != ITEM_MASK_BUNNY) {
                        gSaveContext.equips.buttonItems[sLocal.buffMaskSlot] = ITEM_MASK_BUNNY;
                    }
                    lp->currentMask = PLAYER_MASK_BUNNY;
                } else {
                    // Restore everything we replaced.
                    lp->currentMask = sLocal.savedMaskBeforeBuff;
                    CVarSetInteger(CVAR_ENHANCEMENT("MMBunnyHood"),
                                    sLocal.savedBunnyHoodCVar);
                    if (sLocal.buffMaskSlot >= 0) {
                        gSaveContext.equips.buttonItems[sLocal.buffMaskSlot] =
                            sLocal.savedMaskSlotItem;
                        sLocal.buffMaskSlot = -1;
                    }
                    sLocal.appliedBunnyHood = false;
                }
            }
        }
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
    //
    //     User spec: if the Triforce is currently resting AT a valid
    //     spawn point, do NOT run the idle timer. It only ticks when the
    //     Triforce was dropped somewhere else (after a knockout) and
    //     remained untouched. Sitting at a spawn point indefinitely is
    //     fine — players race to it on round start / after an OOB respawn.
    if (sLocal.inRound && !sLocal.roundEnded && sLocal.carrierClientId == 0 &&
        sLocal.dropFlyTimer == 0 && Harpoon::Instance != nullptr) {
        const MapDef* m = GetMap(sLocal.confirmedMap);
        bool inVoid = false;
        if (m != nullptr && sLocal.currentSpawn >= 0 &&
            (size_t)sLocal.currentSpawn < m->spawnPoints.size()) {
            f32 spawnY = m->spawnPoints[sLocal.currentSpawn].y;
            if (sLocal.triforceY < spawnY - 200.0f) inVoid = true;
        }
        // Is the Triforce currently within ~40u of ANY spawn point on
        // this map? If so, skip the idle counter entirely.
        bool atSpawnPoint = false;
        if (m != nullptr) {
            constexpr f32 kAtSpawnRadiusSq = 40.0f * 40.0f;
            for (const auto& sp : m->spawnPoints) {
                f32 dx = sLocal.triforceX - sp.x;
                f32 dy = sLocal.triforceY - sp.y;
                f32 dz = sLocal.triforceZ - sp.z;
                if (dx * dx + dy * dy + dz * dz < kAtSpawnRadiusSq) {
                    atSpawnPoint = true;
                    break;
                }
            }
        }
        if (!atSpawnPoint) {
            sLocal.idleOnGroundFrames++;
        } else {
            sLocal.idleOnGroundFrames = 0;
        }
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
