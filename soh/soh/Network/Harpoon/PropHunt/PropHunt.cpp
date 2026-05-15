#include "PropHunt.h"
#include "../Harpoon.h"

#include <libultraship/libultraship.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <cstdlib>

#include "soh/Notification/Notification.h"
#include "soh/ActorDB.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
// SaveManager.h's `void Save_InitFile(int)` declaration lives inside the
// `#else` (C-only) branch of an `#ifdef __cplusplus`, so it's invisible to
// us. The symbol exists in SaveManager.cpp with C linkage — forward-declare
// it ourselves.
extern "C" void Save_InitFile(int isDebug);
// OPEN_DISPS / CLOSE_DISPS in macros.h redeclare these two symbols inline at
// every call site. Including frame_interpolation.h is not enough on MSVC —
// the in-block redeclaration inside the macro takes the linkage of the
// surrounding C++ context and the link step searches for the mangled name.
// Redeclaring them ourselves with explicit `extern "C"` linkage at file scope
// forces the linker to look up the C symbol.
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
#include "sequence.h"
#include "sfx.h"
extern PlayState* gPlayState;
extern GameState* gGameState;
// Object_Spawn is defined in z_scene.c but not declared in any public
// header. Needed by SpawnGhostActors to pre-load every prop's required
// object bank so all 10 props per category can spawn regardless of the
// scene's static object list. C linkage; ObjectContext comes from z64.h.
s32 Object_Spawn(ObjectContext* objectCtx, s16 objectId);
// ActorDB_Retrieve is declared in soh/ActorDB.h but ONLY inside the C-only
// branch of `#ifdef __cplusplus`. From the C++ side we'd have to go through
// the ActorDB class — easier to just forward-declare the C symbol here.
ActorDBEntry* ActorDB_Retrieve(int id);
// gMapLoading lives in z_actor.c. While == 1, Actor_Spawn rejects actors
// whose object isn't in the scene's bank; while == 0 it falls back to
// objBankIndex=0 (gameplay_keep). Scooter toggles this around the prop
// ghost spawn so cross-scene actors (Stalfos in Hyrule Field, etc.)
// still produce a non-null Actor* whose draw can be invoked.
extern int gMapLoading;

// Vanilla actor Draw functions referenced by FixDeferredDraw. All have
// external linkage (non-static at file scope in their respective z_xxx.c
// overlays) so the linker resolves these at static link time. We re-
// declare here because no public header exposes them — they're meant to
// be installed only through ActorInit.draw.
void ObjTsubo_Draw(Actor* thisx, PlayState* play);
void EnKusa_Draw(Actor* thisx, PlayState* play);
void EnIshi_Draw(Actor* thisx, PlayState* play);
void ObjBombiwa_Draw(Actor* thisx, PlayState* play);
void ObjHamishi_Draw(Actor* thisx, PlayState* play);
void EnItem00_Draw(Actor* thisx, PlayState* play);
void EnGs_Draw(Actor* thisx, PlayState* play);
void EnBox_Draw(Actor* thisx, PlayState* play);
void EnKanban_Draw(Actor* thisx, PlayState* play);
void ObjSyokudai_Draw(Actor* thisx, PlayState* play);
void ObjKibako_Draw(Actor* thisx, PlayState* play);
void ObjKibako2_Draw(Actor* thisx, PlayState* play);
void EnWallmas_Draw(Actor* thisx, PlayState* play);
void EnFloormas_Draw(Actor* thisx, PlayState* play);
void EnWf_Draw(Actor* thisx, PlayState* play);
void EnOkuta_Draw(Actor* thisx, PlayState* play);
void EnNiw_Draw(Actor* thisx, PlayState* play);
void EnZf_Draw(Actor* thisx, PlayState* play);
void EnCrow_Draw(Actor* thisx, PlayState* play);
void EnMa1_Draw(Actor* thisx, PlayState* play);
void EnSa_Draw(Actor* thisx, PlayState* play);
void EnTa_Draw(Actor* thisx, PlayState* play);
void EnDaiku_Draw(Actor* thisx, PlayState* play);
void EnHeishi1_Draw(Actor* thisx, PlayState* play);
void EnGo2_Draw(Actor* thisx, PlayState* play);
void EnTk_Draw(Actor* thisx, PlayState* play);
void EnDog_Draw(Actor* thisx, PlayState* play);
void EnCow_Draw(Actor* thisx, PlayState* play);
void EnDns_Draw(Actor* thisx, PlayState* play);
}

// Global "this scene transition was started by our gamemode code, NOT a
// player walking into a loading zone". Set true by every TeleportToEntrance
// in PropHunt + TriforceThief; consumed (and reset to false) every frame by
// the round-active blocker in HarpoonHookHandlers.cpp. Defined below the
// HarpoonPropHunt namespace closes; this forward decl lets the namespace's
// own code (PropHunt's TeleportToEntrance) reference it via `::name`.
bool sHarpoonAuthorizedTransition = false;

// =============================================================================
// File-scope storage
// =============================================================================

namespace {

HarpoonPropHunt::PropTables       sTables;
std::vector<HarpoonPropHunt::MapDef> sMaps;
HarpoonPropHunt::LocalState       sLocal;
nlohmann::json                    sSavePresetRaw;   // presets/save.json contents
bool                              sLoaded = false;

// EVERYONE_CHOOSES vote window — counted in PropHunt TickFrame frames
// (~60 fps). sMapVoteArmed = "we're inside an active vote window";
// sMapVoteDeadline = frames left before the timeout fires.
s32  sMapVoteDeadline = 0;
bool sMapVoteArmed    = false;

// Path resolution — <appdir>/harpoon/gamemodes/prop_hunt/
std::string ResolvePackRoot() {
    std::string harpoonRoot = Ship::Context::LocateFileAcrossAppDirs("harpoon", "soh");
    std::error_code ec;
    if (harpoonRoot.empty()) {
        // Fallback to CWD/harpoon/ for dev builds where appdir isn't set up.
        if (std::filesystem::exists(std::filesystem::path("harpoon"), ec)) {
            harpoonRoot = "harpoon";
        }
    }
    if (harpoonRoot.empty()) return {};
    auto packPath = std::filesystem::path(harpoonRoot) / "gamemodes" / "prop_hunt";
    if (!std::filesystem::exists(packPath, ec) || !std::filesystem::is_directory(packPath, ec)) {
        return {};
    }
    return packPath.string();
}

bool ReadJsonFile(const std::filesystem::path& path, nlohmann::json& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        f >> out;
    } catch (const std::exception& e) {
        SPDLOG_WARN("[Harpoon][PropHunt] failed to parse {}: {}", path.string(), e.what());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Actor name -> ID resolver
// Only the names actually referenced in prop_hunt JSONs are included.
// Adding a new prop = add a row here. Unrecognised names log a warning and
// fall back to 0 (which the engine treats as ACTOR_PLAYER and will misbehave —
// the warning is the developer's signal to extend the table).
// ---------------------------------------------------------------------------
struct NamedId { const char* name; s16 id; };

constexpr NamedId kActorIdTable[] = {
    // Environment props
    { "OBJ_TSUBO",     ACTOR_OBJ_TSUBO     },
    { "OBJ_KIBAKO",    ACTOR_OBJ_KIBAKO    },
    { "OBJ_KIBAKO2",   ACTOR_OBJ_KIBAKO2   },
    { "OBJ_SYOKUDAI",  ACTOR_OBJ_SYOKUDAI  },
    { "OBJ_BOMBIWA",   ACTOR_OBJ_BOMBIWA   },
    { "OBJ_HAMISHI",   ACTOR_OBJ_HAMISHI   },
    { "EN_ISHI",       ACTOR_EN_ISHI       },
    { "EN_KUSA",       ACTOR_EN_KUSA       },
    { "EN_ITEM00",     ACTOR_EN_ITEM00     },
    { "EN_BOX",        ACTOR_EN_BOX        },
    { "EN_KANBAN",     ACTOR_EN_KANBAN     },
    { "EN_GS",         ACTOR_EN_GS         },

    // Enemies
    { "EN_RD",         ACTOR_EN_RD         },
    { "EN_FIREFLY",    ACTOR_EN_FIREFLY    },
    { "EN_TEST",       ACTOR_EN_TEST       },
    { "EN_SKB",        ACTOR_EN_SKB        },
    { "EN_WALLMAS",    ACTOR_EN_WALLMAS    },
    { "EN_FLOORMAS",   ACTOR_EN_FLOORMAS   },
    { "EN_CROW",       ACTOR_EN_CROW       },
    { "EN_WF",         ACTOR_EN_WF         },
    { "EN_TITE",       ACTOR_EN_TITE       },
    { "EN_AM",         ACTOR_EN_AM         },
    { "EN_DODONGO",    ACTOR_EN_DODONGO    },
    { "EN_DEKUNUTS",   ACTOR_EN_DEKUNUTS   },
    { "EN_NIW",        ACTOR_EN_NIW        },
    { "EN_DEKUBABA",   ACTOR_EN_DEKUBABA   },
    { "EN_OKUTA",      ACTOR_EN_OKUTA      },
    { "EN_ZF",         ACTOR_EN_ZF         },
    { "EN_MB",         ACTOR_EN_MB         },

    // NPCs
    { "EN_HEISHI1",    ACTOR_EN_HEISHI1    },
    { "EN_GO2",        ACTOR_EN_GO2        },
    { "EN_DOG",        ACTOR_EN_DOG        },
    { "EN_DAIKU",      ACTOR_EN_DAIKU      },
    { "EN_TK",         ACTOR_EN_TK         },
    { "EN_COW",        ACTOR_EN_COW        },
    { "EN_MA1",        ACTOR_EN_MA1        },
    { "EN_DNS",        ACTOR_EN_DNS        },
    { "EN_SA",         ACTOR_EN_SA         },
    { "EN_TA",         ACTOR_EN_TA         },
};

s16 ResolveActorName(const std::string& name) {
    for (const auto& row : kActorIdTable) {
        if (name == row.name) return row.id;
    }
    SPDLOG_WARN("[Harpoon][PropHunt] unknown actor name '{}' — extend kActorIdTable in HarpoonPropHunt.cpp", name);
    return 0;
}

// ---------------------------------------------------------------------------
// Variant parsing
// ---------------------------------------------------------------------------
HarpoonPropHunt::PropVariant ParseVariant(const nlohmann::json& j) {
    HarpoonPropHunt::PropVariant v{};
    v.actorId = ResolveActorName(j.value("actor", std::string()));
    v.params  = (s16)j.value("params", 0);
    v.scale   = j.value("scale", 0.01f);
    v.yOffset = j.value("y_offset", 0.0f);
    return v;
}

HarpoonPropHunt::PropEntry ParseEntry(const nlohmann::json& j) {
    HarpoonPropHunt::PropEntry e;
    e.name = j.value("name", std::string("?"));
    if (j.contains("states") && j["states"].is_array()) {
        // Multi-state form (environment.json)
        for (const auto& s : j["states"]) {
            e.states.push_back(ParseVariant(s));
        }
    } else {
        // Single-state form (enemies/npcs)
        e.states.push_back(ParseVariant(j));
    }
    return e;
}

// ---------------------------------------------------------------------------
// Per-file loaders
// ---------------------------------------------------------------------------

bool LoadEnvironmentJson(const std::filesystem::path& packRoot) {
    auto p = packRoot / "props" / "environment.json";
    nlohmann::json j;
    if (!ReadJsonFile(p, j)) return false;
    if (!j.contains("props") || !j["props"].is_array()) {
        SPDLOG_WARN("[Harpoon][PropHunt] environment.json missing 'props' array");
        return false;
    }
    s32 i = 0;
    for (const auto& entry : j["props"]) {
        if (i >= HarpoonPropHunt::kPropsPerCategory) break;
        sTables.environment[i++] = ParseEntry(entry);
    }
    return true;
}

bool LoadPerMapJson(const std::filesystem::path& path,
                    std::array<std::array<HarpoonPropHunt::PropEntry, HarpoonPropHunt::kPropsPerCategory>,
                               HarpoonPropHunt::kMapCount>& dest) {
    nlohmann::json j;
    if (!ReadJsonFile(path, j)) return false;
    if (!j.contains("by_map") || !j["by_map"].is_object()) return false;
    if (!j.contains("map_order") || !j["map_order"].is_array()) return false;

    s32 mapIdx = 0;
    for (const auto& mapName : j["map_order"]) {
        if (mapIdx >= HarpoonPropHunt::kMapCount) break;
        std::string key = mapName.get<std::string>();
        if (!j["by_map"].contains(key)) {
            mapIdx++;
            continue;
        }
        s32 i = 0;
        for (const auto& entry : j["by_map"][key]) {
            if (i >= HarpoonPropHunt::kPropsPerCategory) break;
            dest[mapIdx][i++] = ParseEntry(entry);
        }
        mapIdx++;
    }
    return true;
}

bool LoadSavePresetJson(const std::filesystem::path& packRoot) {
    auto p = packRoot / "presets" / "save.json";
    return ReadJsonFile(p, sSavePresetRaw);
}

bool LoadGamemodeYaml(const std::filesystem::path& packRoot) {
    // gamemode.yaml has the map list. For now we don't bring in a YAML parser
    // — we hardcode the 9 map ids in load order matching gamemode.yaml. The
    // user's pack ships the file but the C++ doesn't need to re-parse it
    // while the names + entrance indices match.
    (void)packRoot;
    sMaps = {
        {"kakariko_village", "Kakariko Village", 0x0DB, "Mountain village with rooftops and alleys."},
        {"death_mountain",   "Death Mountain",   0x013E,"Volcanic mountain with switchbacks and lava."},
        {"clock_town",       "Clock Town",       0x0129,"OoT-actor-compatible Termina hub."},
        {"gerudo_fortress",  "Gerudo Fortress",  0x0129,"Desert compound with rooftops and corridors."},
        {"forest_temple",    "Forest Temple",    0x0169,"Twisted temple with shifting rooms."},
        {"zora_river",       "Zora's River",     0x0EA, "Winding river with cliffs and waterfalls."},
        {"dodongo_cavern",   "Dodongo's Cavern", 0x0152,"Volcanic dungeon with multi-level chambers."},
        {"ganon_castle",     "Ganon's Castle",   0x0467,"Final dungeon with trials and corridors."},
        {"kokiri_forest",    "Kokiri Forest",    0x0EE, "Peaceful village with bridges and trees."},
    };
    return true;
}

}  // anon namespace

// =============================================================================
// Public API
// =============================================================================

namespace HarpoonPropHunt {

bool Init() {
    if (sLoaded) return true;

    std::string packRoot = ResolvePackRoot();
    if (packRoot.empty()) {
        SPDLOG_INFO("[Harpoon][PropHunt] no pack at <appdir>/harpoon/gamemodes/prop_hunt — disabled");
        return false;
    }
    auto root = std::filesystem::path(packRoot);

    bool ok = true;
    ok &= LoadGamemodeYaml(root);
    ok &= LoadEnvironmentJson(root);
    ok &= LoadPerMapJson(root / "props" / "enemies.json", sTables.enemiesByMap);
    ok &= LoadPerMapJson(root / "props" / "npcs.json",    sTables.npcsByMap);
    ok &= LoadSavePresetJson(root);

    sTables.loaded = ok;
    sLoaded = ok;
    if (ok) {
        SPDLOG_INFO("[Harpoon][PropHunt] pack loaded from {} ({} maps)", packRoot, sMaps.size());
    } else {
        SPDLOG_WARN("[Harpoon][PropHunt] pack at {} failed validation", packRoot);
    }
    return ok;
}

bool IsLoaded() { return sLoaded; }

const PropTables& GetTables() { return sTables; }
const std::vector<MapDef>& GetMaps() { return sMaps; }

const PropEntry* GetPropEntry(s32 category, s32 propIndex, s32 mapIdx) {
    if (propIndex < 0 || propIndex >= kPropsPerCategory) return nullptr;
    if (mapIdx < 0 || mapIdx >= kMapCount) mapIdx = 0;
    switch (category) {
        case CAT_ENVIRONMENT: return &sTables.environment[propIndex];
        case CAT_ENEMIES:     return &sTables.enemiesByMap[mapIdx][propIndex];
        case CAT_NPCS:        return &sTables.npcsByMap[mapIdx][propIndex];
        default:              return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Save preset application
//
// Reads the role section from presets/save.json and applies it to
// gSaveContext. Only vanilla items + upgrades are resolved here; custom items
// (Roc's Feather, Whip, Switch Hook, Cane of Somaria, etc.) require including
// mods/items/custom_items.h which the project policy keeps out of .cpp.
// Those slots are noted in the JSON and applied via a dedicated C bridge in
// a future commit (HarpoonPropHuntSave.c).
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
    gSaveContext.linkAge          = role.value("link_age", 1);
    gSaveContext.entranceIndex    = (s32)role.value("entrance_index",
                                       sSavePresetRaw["common"].value("entrance_index", 205));
    gSaveContext.healthCapacity   = (s16)role.value("health_capacity", 64);
    gSaveContext.health           = (s16)role.value("health", 64);
    gSaveContext.isMagicAcquired       = role.value("magic_acquired", true) ? 1 : 0;
    gSaveContext.isDoubleMagicAcquired = role.value("double_magic_acquired", true) ? 1 : 0;
    gSaveContext.magicLevel       = (s8)role.value("magic_level", 2);
    gSaveContext.magicCapacity    = (s16)role.value("magic_capacity", 96);
    gSaveContext.magic            = (s16)role.value("magic", 96);
    gSaveContext.magicState       = MAGIC_STATE_IDLE;
}

// Resolve UPG_* upgrade names — small lookup, ~8 entries.
s32 ResolveUpgradeId(const std::string& name) {
    if (name == "STRENGTH")   return UPG_STRENGTH;
    if (name == "QUIVER")     return UPG_QUIVER;
    if (name == "BOMB_BAG")   return UPG_BOMB_BAG;
    if (name == "BULLET_BAG") return UPG_BULLET_BAG;
    if (name == "NUTS")       return UPG_NUTS;
    if (name == "STICKS")     return UPG_STICKS;
    if (name == "SCALE")      return UPG_SCALE;
    if (name == "WALLET")     return UPG_WALLET;
    SPDLOG_WARN("[Harpoon][PropHunt] unknown upgrade '{}'", name);
    return -1;
}

// Resolve ITEM_* / SLOT_* names. JSON keys / values may also be raw integers,
// in which case the caller handles that path before calling these.
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
    // Custom items (enum values in z64item.h, behaviour in mods/items/).
    if (n == "ITEM_ROCS_FEATHER_SKIJER") return ITEM_ROCS_FEATHER_SKIJER;
    if (n == "ITEM_ROCS_CAPE")         return ITEM_ROCS_CAPE;
    if (n == "ITEM_DEKU_LEAF")         return ITEM_DEKU_LEAF;
    if (n == "ITEM_SWITCH_HOOK")       return ITEM_SWITCH_HOOK;
    if (n == "ITEM_WHIP")              return ITEM_WHIP;
    if (n == "ITEM_CANE_OF_SOMARIA")   return ITEM_CANE_OF_SOMARIA;
    if (n == "ITEM_ROD_FIRE")          return ITEM_ROD_FIRE;
    if (n == "ITEM_ROD_ICE")           return ITEM_ROD_ICE;
    if (n == "ITEM_BEETLE")            return ITEM_BEETLE;
    SPDLOG_WARN("[Harpoon][PropHunt] unknown item name '{}'", n);
    return ITEM_NONE;
}

s32 ResolveSlotName(const std::string& n) {
    // Vanilla SLOT_*
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
    // Page-2 custom slots — these are #define aliases in extended_inventory.h
    if (n == "SLOT_ROCS")         return SLOT_ROCS;
    if (n == "SLOT_ROCS_CAPE")    return SLOT_ROCS_CAPE;
    if (n == "SLOT_DEKU_LEAF")    return SLOT_DEKU_LEAF;
    if (n == "SLOT_WHIP")         return SLOT_WHIP;
    if (n == "SLOT_SWITCH_HOOK")  return SLOT_SWITCH_HOOK;
    if (n == "SLOT_CANE_OF_SOMARIA") return SLOT_CANE_OF_SOMARIA;
    if (n == "SLOT_FIRE_ROD")     return SLOT_FIRE_ROD;
    if (n == "SLOT_ICE_ROD")      return SLOT_ICE_ROD;
    if (n == "SLOT_BEETLE")       return SLOT_BEETLE;
    SPDLOG_WARN("[Harpoon][PropHunt] unknown slot name '{}'", n);
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
    SPDLOG_WARN("[Harpoon][PropHunt] unknown equip value '{}'", n);
    return 0;
}

// JSON value -> int. Accepts integer or named string.
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
// Equipment + inventory + ammo + button application
// ---------------------------------------------------------------------------

void ApplyEquipmentMask(const nlohmann::json& role) {
    // Always start clean before applying.
    gSaveContext.inventory.equipment = 0;

    if (role.value("equipment_all", false)) {
        // Full kit: 3 swords, 3 shields, 3 tunics, 3 boots.
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

    // Per-bit list.
    if (role.contains("equipment_mask_bits") && role["equipment_mask_bits"].is_array()) {
        for (const auto& b : role["equipment_mask_bits"]) {
            if (b.is_number_integer()) {
                gSaveContext.inventory.equipment |= (1u << b.get<int>());
            }
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

// Walk an object whose keys are slot names/integers and whose values are
// item names/integers. Direct-assigns to gSaveContext.inventory.items[slot].
void ApplyItemsMap(const nlohmann::json& itemsObj) {
    if (!itemsObj.is_object()) return;
    for (auto it = itemsObj.begin(); it != itemsObj.end(); ++it) {
        const std::string& key = it.key();
        s32 slot;
        // Key may be a numeric string or a SLOT_* name.
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
            if (i == 0) {
                gSaveContext.inventory.items[24 + i] = ITEM_ROCS_CAPE;
            } else {
                gSaveContext.inventory.items[24 + i] = gPage2Items[i];
            }
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
    // Iterate and skip any underscore-prefixed comment keys.
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
    // Engine has 7 c-button slots in cButtonSlots[].
    for (size_t i = 0; i < arr.size() && i < 7; i++) {
        gSaveContext.equips.cButtonSlots[i] = (u8)NumOrSlotName(arr[i]);
    }
}

void ApplyRoleSection(const std::string& roleKey) {
    if (sSavePresetRaw.is_null()) {
        SPDLOG_WARN("[Harpoon][PropHunt] save preset not loaded — skipping {} apply", roleKey);
        return;
    }
    if (!sSavePresetRaw.contains(roleKey)) {
        SPDLOG_WARN("[Harpoon][PropHunt] save preset missing '{}' section", roleKey);
        return;
    }

    auto& common = sSavePresetRaw["common"];
    auto& role   = sSavePresetRaw[roleKey];

    // Quest id — Scooter uses QUEST_PROP_HUNT (a custom value); this branch
    // hasn't introduced that enum yet, so we leave the quest id alone. Add
    // QUEST_PROP_HUNT to z64save.h and switch this assignment when the engine
    // needs the gate (e.g. for HUD overlay paths).

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

    SPDLOG_INFO("[Harpoon][PropHunt] applied '{}' save preset", roleKey);
}

}  // anon

void ApplyHiderSave()  { ApplyRoleSection("hider"); }
void ApplySeekerSave() {
    ApplyRoleSection("seeker");
    // Override: seekers start drained. TickSeekerPassiveRegen refills both
    // ammo and magic over time during PLAYING. Magic capacity forced to 96
    // (full double-magic) so the meter has room to fill regardless of the
    // underlying save's progression state. User spec: "inician en 0 como
    // en TT" — matches TT's ApplyAmmo zero-out + thief regen pattern.
    gSaveContext.magic              = 0;
    gSaveContext.magicCapacity      = 96;
    gSaveContext.isMagicAcquired    = true;
    gSaveContext.isDoubleMagicAcquired = true;
    for (size_t i = 0; i < ARRAY_COUNT(gSaveContext.inventory.ammo); i++) {
        gSaveContext.inventory.ammo[i] = 0;
    }
}

// ---------------------------------------------------------------------------
// Local state
// ---------------------------------------------------------------------------

LocalState& GetLocalState() { return sLocal; }
bool IsHider()      { return sLocal.role == Role::Hider; }
bool IsSeeker()     { return sLocal.role == Role::Seeker; }
bool IsEliminated() { return sLocal.role == Role::Eliminated; }

bool IsLocalHiderWithProp() {
    // True for "should we render local as a prop / broadcast disguise" —
    // includes the lobby (Hyrule Field, no round in flight) so players can
    // mess around as a prop while waiting for the next round. Round-only
    // mechanics (decoy spawn) gate on IsHider() separately.
    bool propValid = sLocal.propIndex >= 0 && sLocal.propIndex < kPropsPerCategory;
    if (!propValid) return false;
    if (IsHider()) return true;
    return (Harpoon::Instance != nullptr &&
            Harpoon::Instance->gameState == HARPOON_STATE_LOBBY);
}

// ---------------------------------------------------------------------------
// Cycling — clamp/wrap around per-category bounds
// ---------------------------------------------------------------------------

bool CyclePropCategory(s32 delta) {
    if (delta == 0) return false;
    s32 newCat = (sLocal.propCategory + delta + kCategoryCount) % kCategoryCount;
    if (newCat == sLocal.propCategory) return false;
    sLocal.propCategory = newCat;
    sLocal.propIndex = 0;
    sLocal.propState = 0;
    return true;
}

bool CyclePropIndex(s32 delta) {
    if (delta == 0) return false;
    s32 newIdx = (sLocal.propIndex + delta + kPropsPerCategory) % kPropsPerCategory;
    if (newIdx == sLocal.propIndex) return false;
    sLocal.propIndex = newIdx;
    sLocal.propState = 0;
    return true;
}

bool CyclePropState(s32 delta) {
    if (delta == 0) return false;
    s32 mapIdx = sLocal.confirmedMap >= 0 ? sLocal.confirmedMap : 0;
    const PropEntry* entry = GetPropEntry(sLocal.propCategory, sLocal.propIndex, mapIdx);
    if (entry == nullptr || entry->states.empty()) return false;
    s32 stateCount = (s32)entry->states.size();
    s32 newState = (sLocal.propState + delta + stateCount) % stateCount;
    if (newState == sLocal.propState) return false;
    sLocal.propState = newState;
    return true;
}

// ---------------------------------------------------------------------------
// Event dispatch
// ---------------------------------------------------------------------------

namespace {

void HandleRoleAssign(const nlohmann::json& p) {
    s32 target = p.value("targetClientId", 0);
    std::string roleStr = p.value("role", "hider");

    // Side-effect: update the targeted client's role in the global client map
    // so the player list in the menu reflects the assignment across all peers.
    // If they're now a seeker, also wipe their propIndex so the dummy-draw
    // gate (HarpoonDummyPlayer.cpp `client.propIndex >= 0`) stops rendering
    // them as a prop. Otherwise their last broadcast disguise would persist
    // and seekers would visually look like Pots to their teammates.
    if (Harpoon::Instance != nullptr && target != 0) {
        auto it = Harpoon::Instance->clients.find((u32)target);
        if (it != Harpoon::Instance->clients.end()) {
            it->second.role = roleStr;
            if (roleStr == "seeker" || roleStr == "eliminated") {
                it->second.propIndex = -1;
                it->second.propCategory = 0;
                it->second.propState = 0;
            }
        }
    }

    // Filter: zero / absent target = "to everyone". Non-zero applies only if
    // it matches our ownClientId.
    uint32_t ownId = Harpoon::Instance ? Harpoon::Instance->ownClientId : 0;
    if (target != 0 && (uint32_t)target != ownId) return;

    if      (roleStr == "seeker")     sLocal.role = Role::Seeker;
    else if (roleStr == "eliminated") sLocal.role = Role::Eliminated;
    else                              sLocal.role = Role::Hider;

    // Becoming a seeker / eliminated → clear our prop selection so the
    // z_player.c prop intercept (`IsLocalHiderWithProp` gate) returns 0
    // and the local player renders as vanilla Link. Also broadcast a
    // "no-prop" SET_DISGUISE so any peer whose ROLE_ASSIGN arrived BEFORE
    // our last heartbeat learns the disguise is dead. Without this,
    // peers' clients[us].propIndex can stay at the last broadcast value
    // and the dummy-draw gate keeps rendering us as a prop on their
    // screens until the next round.
    if (sLocal.role != Role::Hider) {
        sLocal.propIndex = -1;
        sLocal.propCategory = 0;
        sLocal.propState = 0;
        if (Harpoon::Instance != nullptr && Harpoon::Instance->isConnected) {
            Harpoon::Instance->SendJsonToRemote(BuildSetDisguisePayload());
        }
    }

    Notification::Emit({
        .prefix = "Prop Hunt",
        .message = (sLocal.role == Role::Hider)      ? "You are a HIDER"
                 : (sLocal.role == Role::Seeker)     ? "You are a SEEKER"
                 : (sLocal.role == Role::Eliminated) ? "You are ELIMINATED"
                                                    : "Role: unassigned",
        .remainingTime = 4.0f,
    });

    // Enter / switch role behaviour depends on round state:
    //   - LOBBY / MAP_SELECT: this is the initial assignment for a new
    //     round. Don't touch the scene — MAP_CONFIRMED arrives right after
    //     and handles the actual teleport + kit apply via SetPendingInit.
    //   - HIDING_PHASE / PLAYING: round is in flight. Either an admin
    //     override OR a race where MAP_CONFIRMED arrived before our role
    //     was known (RANDOM mode fires ROLE_ASSIGN + MAP_CONFIRMED in tight
    //     succession; the order at the peer isn't guaranteed). In either
    //     case we must take the player to the CORRECT scene for their new
    //     role — not InstantReloadScene the current one. Otherwise a peer
    //     whose MAP_CONFIRMED landed first stays in Hyrule Field forever
    //     with a hider kit.
    bool inHidePhase = (Harpoon::Instance != nullptr &&
                        Harpoon::Instance->gameState == HARPOON_STATE_HIDING_PHASE);
    bool inPlaying   = (Harpoon::Instance != nullptr &&
                        Harpoon::Instance->gameState == HARPOON_STATE_PLAYING);
    s32  mapIdx      = (Harpoon::Instance != nullptr) ? Harpoon::Instance->confirmedMapIndex : -1;
    bool inRound     = inHidePhase || inPlaying;

    if (inRound && gPlayState != nullptr && mapIdx >= 0 &&
        (sLocal.role == Role::Hider || sLocal.role == Role::Seeker)) {
        s32 roundEntr = GetEntranceForMapIndex(mapIdx);
        if (sLocal.role == Role::Hider) {
            // Hiders always go to the round map.
            gSaveContext.linkAge = LINK_AGE_CHILD;
            TeleportToEntrance(roundEntr);
            SetPendingInit(1);   // hider kit post-load
        } else {  // Role::Seeker
            if (inPlaying) {
                // Seeker in play phase → round map with seeker kit.
                gSaveContext.linkAge = LINK_AGE_CHILD;
                TeleportToEntrance(roundEntr);
                SetPendingInit(2);   // seeker kit post-load
            } else {
                // Seeker in hide phase → lobby. Only teleport if we're
                // not already in Hyrule Field (the lobby scene) so a
                // race-fixed assignment doesn't yank a peer who already
                // arrived correctly.
                if (gPlayState->sceneNum != SCENE_HYRULE_FIELD) {
                    gSaveContext.linkAge = LINK_AGE_CHILD;
                    TeleportToEntrance(ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN);
                    // No kit apply — seeker kit is granted at hide-phase end.
                }
            }
        }
    } else if (inRound && (sLocal.role == Role::Hider || sLocal.role == Role::Seeker)) {
        // Fallback for the rare case where confirmedMapIndex hasn't propagated
        // yet (defensive). InstantReloadScene swaps the kit in place — better
        // than nothing while we wait for MAP_CONFIRMED.
        ChangeRoleAndReload(sLocal.role);
    }
    // Else: not in round yet — just leave the role set; round-start flow
    // will pick it up.
}

// Takes the full envelope (not just data) so we can read `source`.
void HandleSetDisguise(const nlohmann::json& envelope,
                       const nlohmann::json& data) {
    // Read the broadcasting client's id from the envelope's `source` field
    // (set by the server's relay). Update that client's prop selection in
    // our roster so HarpoonDummyPlayer_Draw can render them as the prop.
    // Without this branch, the dummy player just renders as Link and the
    // seekers see right through the disguise.
    if (Harpoon::Instance == nullptr) return;
    s32 cat = data.value("category", 0);
    s32 idx = data.value("propIndex", -1);
    s32 st  = data.value("propState", 0);
    u32 src = envelope.value("source", 0u);
    if (src == 0) return;
    auto it = Harpoon::Instance->clients.find(src);
    if (it == Harpoon::Instance->clients.end()) return;
    it->second.propCategory = cat;
    it->second.propIndex    = idx;
    it->second.propState    = st;
    // If a non-trivial prop is being broadcast, the broadcaster IS a
    // hider — but ONLY upgrade the role if it isn't already an authoritative
    // role set by ROLE_ASSIGN. Specifically, don't overwrite "seeker" or
    // "eliminated" because a late / racing SET_DISGUISE heartbeat from
    // before the role change would otherwise re-promote a player who's
    // already been demoted to seeker, leaving them visible as a prop on
    // peers' screens. Also force propIndex back to -1 when we're rejecting
    // a "hider" upgrade — the broadcaster's last heartbeat shouldn't
    // resurrect the disguise on a seeker.
    if (idx >= 0) {
        if (it->second.role == "seeker" || it->second.role == "eliminated") {
            // Authoritative non-hider role wins — drop the stale disguise.
            it->second.propIndex = -1;
            it->second.propCategory = 0;
            it->second.propState = 0;
        } else {
            // Empty or "hider" — safe to set/keep as hider.
            it->second.role = "hider";
        }
    }
}

void HandleHidePhaseBegin(const nlohmann::json& p) {
    sLocal.inHidePhase = true;
    sLocal.hidePhaseFramesRemaining = p.value("durationFrames", 30 * 20);
    s32 sec = (sLocal.hidePhaseFramesRemaining + 19) / 20;
    Notification::Emit({
        .prefix = "Prop Hunt",
        .message = "Hide phase: " + std::to_string(sec) + "s",
        .remainingTime = 4.0f,
    });
}

void HandleHidePhaseEnd(const nlohmann::json& /*p*/) {
    Notification::Emit({
        .prefix = "Prop Hunt",
        .message = "Seekers released!",
        .remainingTime = 3.0f,
    });
    LocallyEndHidePhase();
}

void HandleEliminated(const nlohmann::json& p) {
    s32 victim = p.value("victimClientId", 0);
    if (victim == 0) return;

    // Backup path for the host's no-hiders count. The dying client also
    // broadcasts ROLE_ASSIGN(self, seeker), but if that packet is dropped
    // or arrives after ELIMINATED, the host's clients[victim].role stays
    // "hider" forever and the round never ends (manifests when host is
    // the seeker, so sLocal.role doesn't mask the off-by-one count). Two
    // independent signals — either one is sufficient to advance the count.
    if (Harpoon::Instance != nullptr) {
        auto it = Harpoon::Instance->clients.find((u32)victim);
        if (it != Harpoon::Instance->clients.end()) {
            it->second.role         = "seeker";
            it->second.propIndex    = -1;
            it->second.propCategory = 0;
            it->second.propState    = 0;
        }
    }

    // Defensive self-update too: if ELIMINATED arrived before our own
    // health-change handler converted us, sync sLocal here. Redundant but
    // harmless on the normal path.
    uint32_t ownId = Harpoon::Instance ? Harpoon::Instance->ownClientId : 0;
    if ((uint32_t)victim == ownId) {
        sLocal.role         = Role::Seeker;
        sLocal.propIndex    = -1;
        sLocal.propCategory = 0;
        sLocal.propState    = 0;
    }
}

void HandleRoundResult(const nlohmann::json& p) {
    // Silent round end — no winner notification, no text. Per user spec
    // ("nothing, silent teleport"): the round just ends and everyone is
    // returned to the lobby. Host re-starts the next round manually.
    (void)p;
    SPDLOG_INFO("[Harpoon][PropHunt] round ended -> silent teleport to lobby");
    sLocal.role = Role::Unassigned;
    sLocal.inHidePhase = false;
    sLocal.propIndex = -1;
    sLocal.propState = 0;
    sLocal.propModeLockoutTimer = 0;
    // Force everyone (including the seekers who triggered the win) back
    // to Hyrule Field as the lobby. ChangeRoleAndReload would respect
    // the current role; we just want a clean teleport to the lobby's
    // entrance. Cancel any in-progress player state first so the
    // transition isn't blocked by a damage cutscene / item pickup.
    if (Harpoon::Instance != nullptr) {
        Harpoon::Instance->gameState = HARPOON_STATE_LOBBY;
    }
    if (gPlayState != nullptr) {
        gSaveContext.linkAge = LINK_AGE_CHILD;
        TeleportToEntrance(ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN);
        SetPendingInit(4);  // reset-to-hider preset on next OnSceneSpawnActors
    }
}

}  // anon

void HandleEvent(const nlohmann::json& envelope) {
    // Wire format: ROOM.EVENT envelope from the server carries
    // {event_name: "...", data: {...}, source: clientId}. Caller in
    // Harpoon::HandlePacket_RoomEvent already filtered by prefix, but we
    // accept both the unwrapped envelope (legacy callers) and the standard
    // {event_name, data} layout.
    std::string evt = envelope.value("event_name", std::string());
    if (evt.empty()) evt = envelope.value("event", std::string());
    if (evt.empty()) return;

    const nlohmann::json& data = envelope.contains("data") && envelope["data"].is_object()
                                   ? envelope["data"] : envelope;

    if      (evt == kEvtRoleAssign)      HandleRoleAssign(data);
    else if (evt == kEvtSetDisguise)     HandleSetDisguise(envelope, data);
    else if (evt == kEvtHidePhaseBegin)  HandleHidePhaseBegin(data);
    else if (evt == kEvtHidePhaseEnd)    HandleHidePhaseEnd(data);
    else if (evt == kEvtEliminated)      HandleEliminated(data);
    else if (evt == kEvtRoundResult)     HandleRoundResult(data);
    else if (evt == "PROP_HUNT.OPEN_MAP_SELECT") {
        // Host triggered the map-select fullscreen overlay. Each peer
        // flips gameState locally so the GuiWindow draws.
        if (Harpoon::Instance != nullptr) {
            Harpoon::Instance->gameState = HARPOON_STATE_MAP_SELECT;
            Harpoon::Instance->mapSelectMode =
                (HarpoonMapSelectMode)data.value("mapSelectMode", (int)MAP_SELECT_HOST_CHOOSES);
            // Reset every peer's stale "I voted last round" flag — without
            // this an old `hasVoted=true` from the previous round makes
            // the host's tally fire immediately on the new window.
            for (auto& [cid, c] : Harpoon::Instance->clients) {
                c.hasVoted = false;
            }
        }
        // Arm a fresh vote window. Host's TickFrame ticks it down; on 0
        // (timeout) or "everyone voted" the tally + HostStartRound fires.
        sMapVoteArmed    = true;
        sMapVoteDeadline = 15 * 60;
    }
    else if (evt == "PROP_HUNT.MAP_CURSOR") {
        // Peer moved their cursor — update their per-client mapSelectIndex
        // so our navi rendering shows the right cell.
        u32 src = envelope.value("source", 0u);
        s32 idx = data.value("mapIndex", 0);
        if (Harpoon::Instance != nullptr && src != 0) {
            auto it = Harpoon::Instance->clients.find(src);
            if (it != Harpoon::Instance->clients.end()) {
                it->second.mapSelectIndex = idx;
            }
        }
    }
    else if (evt == "PROP_HUNT.MAP_VOTE") {
        // Peer cast a vote. We don't tally here — the host's client owns the
        // tally and broadcasts MAP_CONFIRMED when the round threshold hits.
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
    else if (evt == "PROP_HUNT.MAP_CONFIRMED") {
        s32 idx = data.value("mapIndex", 0);
        LocallyConfirmMap(idx);
    }
}

// ---------------------------------------------------------------------------
// Payload builders
// ---------------------------------------------------------------------------

// Payload builders return a {type, event_name, data} object ready to drop
// into Harpoon::SendJsonToRemote.
static nlohmann::json _Envelope(const char* evt, nlohmann::json data) {
    nlohmann::json p;
    p["type"]       = "ROOM.BROADCAST_EVENT";
    p["event_name"] = evt;
    p["data"]       = std::move(data);
    return p;
}

nlohmann::json BuildSetDisguisePayload() {
    nlohmann::json d;
    d["category"]  = sLocal.propCategory;
    d["propIndex"] = sLocal.propIndex;
    d["propState"] = sLocal.propState;
    return _Envelope(kEvtSetDisguise, std::move(d));
}

nlohmann::json BuildRoleAssignPayload(u32 targetClientId, Role role) {
    nlohmann::json d;
    d["targetClientId"] = targetClientId;
    d["role"] = (role == Role::Hider) ? "hider"
              : (role == Role::Seeker) ? "seeker"
              : (role == Role::Eliminated) ? "eliminated"
              : "unassigned";
    return _Envelope(kEvtRoleAssign, std::move(d));
}

nlohmann::json BuildHidePhaseBeginPayload(s32 durationFrames) {
    nlohmann::json d;
    d["durationFrames"] = durationFrames;
    return _Envelope(kEvtHidePhaseBegin, std::move(d));
}

nlohmann::json BuildHidePhaseEndPayload() {
    return _Envelope(kEvtHidePhaseEnd, nlohmann::json::object());
}

nlohmann::json BuildEliminatedPayload(u32 victimClientId, u32 killerClientId) {
    nlohmann::json d;
    d["victimClientId"] = victimClientId;
    d["killerClientId"] = killerClientId;
    return _Envelope(kEvtEliminated, std::move(d));
}

nlohmann::json BuildRoundResultPayload(const std::string& winnerSide) {
    nlohmann::json d;
    d["winnerSide"] = winnerSide;
    return _Envelope(kEvtRoundResult, std::move(d));
}

// ---------------------------------------------------------------------------
// HUD — top-left panel showing role, current prop selection, hide-phase timer.
// Caller is responsible for gating (only invoke when active gamemode is
// PropHunt); see HarpoonGamemodeHud.cpp.
// ---------------------------------------------------------------------------

void DrawHud() {
    static const char* kCategoryNames[] = { "ENVIRONMENT", "ENEMIES", "NPCS" };

    ImGui::SetNextWindowPos(ImVec2(12.0f, 80.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                              ImGuiWindowFlags_AlwaysAutoResize |
                              ImGuiWindowFlags_NoSavedSettings |
                              ImGuiWindowFlags_NoFocusOnAppearing |
                              ImGuiWindowFlags_NoNav |
                              ImGuiWindowFlags_NoInputs;
    if (!ImGui::Begin("PropHuntHUD", nullptr, flags)) { ImGui::End(); return; }

    const char* roleStr = "(no role)";
    ImVec4 roleColor(0.8f, 0.8f, 0.8f, 1.0f);
    switch (sLocal.role) {
        case Role::Hider:      roleStr = "HIDER";       roleColor = ImVec4(0.5f, 1.0f, 0.5f, 1.0f); break;
        case Role::Seeker:     roleStr = "SEEKER";      roleColor = ImVec4(1.0f, 0.5f, 0.5f, 1.0f); break;
        case Role::Eliminated: roleStr = "ELIMINATED";  roleColor = ImVec4(0.6f, 0.6f, 0.6f, 1.0f); break;
        case Role::Unassigned: roleStr = "Lobby";       break;
    }
    ImGui::TextColored(roleColor, "Prop Hunt — %s", roleStr);

    if (sLocal.inHidePhase) {
        s32 sec = (sLocal.hidePhaseFramesRemaining + 19) / 20;
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f),
                            "Hide phase: %ds", sec);
    }

    // Survival timer — Boss-Rush-style elapsed clock. Per user spec it
    // only ticks while the local player is a hider in an active round; it
    // pauses the moment they become a seeker or return to the lobby. We
    // display it only for hiders so seekers don't get a misleading frozen
    // readout. Shown MM:SS until we hit an hour, then HH:MM:SS.
    if (Harpoon::Instance != nullptr && IsHider() &&
        (Harpoon::Instance->gameState == HARPOON_STATE_HIDING_PHASE ||
         Harpoon::Instance->gameState == HARPOON_STATE_PLAYING)) {
        u32 frames = GetRoundElapsedFrames();
        u32 totalSec = frames / 20;  // 20 fps OoT logic frames
        u32 h = totalSec / 3600;
        u32 m = (totalSec % 3600) / 60;
        u32 s = totalSec % 60;
        if (h > 0) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
                                "Survival %02u:%02u:%02u", h, m, s);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
                                "Survival %02u:%02u", m, s);
        }
    }

    if (sLocal.role == Role::Hider) {
        s32 cat = sLocal.propCategory;
        const char* catName = (cat >= 0 && cat < 3) ? kCategoryNames[cat] : "?";
        s32 mapIdx = sLocal.confirmedMap >= 0 ? sLocal.confirmedMap : 0;
        const PropEntry* entry = GetPropEntry(cat, sLocal.propIndex, mapIdx);
        const char* propName = entry ? entry->name.c_str() : "?";
        s32 stateCount = entry ? (s32)entry->states.size() : 0;
        ImGui::Separator();
        ImGui::Text("Cat:  %s", catName);
        ImGui::Text("Prop: %s", propName);
        ImGui::Text("State: %d / %d", sLocal.propState + 1, stateCount);
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                            "D-Left = cat, D-Down = prop, D-Right = state");
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Ghost actor system — stubs.
//
// Spawning ghost actors requires Actor_SpawnAsChild (in functions.h) plus
// careful flag manipulation so they don't render or update. Rendering a
// hider as a prop requires either a per-limb player draw hook or a Vanilla
// Behavior override — neither is exposed in this branch yet. The functions
// below are wired and called from the right places, but no-op until both
// integration pieces land.
// ---------------------------------------------------------------------------

namespace {

// Per-variant ghost storage. Indexed [category][propIndex][propState].
// Each state in a prop entry's `states` vector gets its OWN spawned
// ghost actor — the actor's `params` are baked at Actor_Spawn time, so
// reusing a single state-0 ghost for higher states (the old 2D layout)
// gave the wrong DL for any actor whose draw branches on params
// (EN_KANBAN params=3, EN_BOX chest variants, OBJ_BOMBIWA / OBJ_HAMISHI
// for Boulder, EN_GS params=1/2, EN_ITEM00 params=3/6 for Heart, etc.).
// kStatesPerProp = 8 is overkill — the prop JSONs cap at 4 states today.
constexpr s32 kStatesPerProp = 8;
Actor* sGhostActors[kCategoryCount][kPropsPerCategory][kStatesPerProp] = {};

}  // anon

// No-op update that the engine can safely invoke each frame without firing
// the actor's actual AI. We keep the ghost actor alive (so its draw and
// object stay loaded) but suppress its behaviour.
static void GhostActorUpdateNoop(Actor* actor, PlayState* play) {
    (void)actor; (void)play;
}

// Spawn a single ghost actor — mirror of Scooter's PropHunt_SpawnGhost.
// The actor is born at y=-9999 (off-screen), AI replaced by a no-op, flags
// cleaned up so the engine doesn't try to target/render it on its own. We
// reposition it onto the player every frame via DrawHiderAsProp.
// If a prop actor's vanilla Init left `draw = NULL` (deferred to a
// WaitForObject action we'll never run, because we override Update with
// a no-op), assign the correct Draw function pointer manually. Mirrors
// Scooter's PropHunt_FixDeferredDraw — without this, every actor that
// uses the deferred-draw pattern (OBJ_TSUBO, EN_KUSA, EN_ISHI, EN_GS,
// EN_BOX, ...) ends up with draw=NULL → SpawnOneGhost kills it →
// DrawHiderAsProp falls back to Crate forever.
static void FixDeferredDraw(Actor* ghost, s16 actorId) {
    if (ghost->draw != nullptr) return;
    switch (actorId) {
    case ACTOR_OBJ_TSUBO:    ghost->draw = (ActorFunc)ObjTsubo_Draw;    break;
    case ACTOR_EN_KUSA:      ghost->draw = (ActorFunc)EnKusa_Draw;      break;
    case ACTOR_EN_ISHI:      ghost->draw = (ActorFunc)EnIshi_Draw;      break;
    case ACTOR_OBJ_BOMBIWA:  ghost->draw = (ActorFunc)ObjBombiwa_Draw;  break;
    case ACTOR_OBJ_HAMISHI:  ghost->draw = (ActorFunc)ObjHamishi_Draw;  break;
    case ACTOR_EN_ITEM00:    ghost->draw = (ActorFunc)EnItem00_Draw;    break;
    case ACTOR_EN_GS:        ghost->draw = (ActorFunc)EnGs_Draw;        break;
    case ACTOR_EN_BOX:       ghost->draw = (ActorFunc)EnBox_Draw;       break;
    case ACTOR_EN_KANBAN:    ghost->draw = (ActorFunc)EnKanban_Draw;    break;
    case ACTOR_OBJ_SYOKUDAI: ghost->draw = (ActorFunc)ObjSyokudai_Draw; break;
    case ACTOR_OBJ_KIBAKO:   ghost->draw = (ActorFunc)ObjKibako_Draw;   break;
    case ACTOR_OBJ_KIBAKO2:  ghost->draw = (ActorFunc)ObjKibako2_Draw;  break;
    case ACTOR_EN_WALLMAS:   ghost->draw = (ActorFunc)EnWallmas_Draw;   break;
    case ACTOR_EN_FLOORMAS:  ghost->draw = (ActorFunc)EnFloormas_Draw;  break;
    case ACTOR_EN_WF:        ghost->draw = (ActorFunc)EnWf_Draw;        break;
    case ACTOR_EN_OKUTA:     ghost->draw = (ActorFunc)EnOkuta_Draw;     break;
    case ACTOR_EN_NIW:       ghost->draw = (ActorFunc)EnNiw_Draw;       break;
    case ACTOR_EN_ZF:        ghost->draw = (ActorFunc)EnZf_Draw;        break;
    case ACTOR_EN_CROW:      ghost->draw = (ActorFunc)EnCrow_Draw;      break;
    case ACTOR_EN_MA1:       ghost->draw = (ActorFunc)EnMa1_Draw;       break;
    case ACTOR_EN_SA:        ghost->draw = (ActorFunc)EnSa_Draw;        break;
    case ACTOR_EN_TA:        ghost->draw = (ActorFunc)EnTa_Draw;        break;
    case ACTOR_EN_DAIKU:     ghost->draw = (ActorFunc)EnDaiku_Draw;     break;
    case ACTOR_EN_HEISHI1:   ghost->draw = (ActorFunc)EnHeishi1_Draw;   break;
    case ACTOR_EN_GO2:       ghost->draw = (ActorFunc)EnGo2_Draw;       break;
    case ACTOR_EN_TK:        ghost->draw = (ActorFunc)EnTk_Draw;        break;
    case ACTOR_EN_DOG:       ghost->draw = (ActorFunc)EnDog_Draw;       break;
    case ACTOR_EN_COW:       ghost->draw = (ActorFunc)EnCow_Draw;       break;
    case ACTOR_EN_DNS:       ghost->draw = (ActorFunc)EnDns_Draw;       break;
    default: break;
    }
}

// For actors whose required object depends on params bits (sObjectIds
// trick in their Init), return the OBJECT_* that contains the chosen
// variant's actual DL. Returns -1 to fall back to InitVars.objectId via
// ActorDB. Without this, OBJ_TSUBO with params=256 would get
// objBankIndex pointing at OBJECT_GAMEPLAY_KEEP (its InitVars value)
// instead of OBJECT_TSUBO (where gPotDL actually lives), and Draw would
// resolve segment 6 to the wrong bank.
static s16 ResolveBank(s16 actorId, s16 params) {
    switch (actorId) {
    case ACTOR_OBJ_TSUBO:
        // sObjectIds[(params >> 8) & 1] = { DANGEON_KEEP, TSUBO }
        return ((params >> 8) & 1) ? (s16)OBJECT_TSUBO
                                   : (s16)OBJECT_GAMEPLAY_DANGEON_KEEP;
    case ACTOR_EN_KUSA:
        // sObjectIds[params & 3] = { FIELD_KEEP, KUSA, KUSA, ... }
        return (params & 3) ? (s16)OBJECT_KUSA
                            : (s16)OBJECT_GAMEPLAY_FIELD_KEEP;
    case ACTOR_EN_ISHI:      return (s16)OBJECT_GAMEPLAY_FIELD_KEEP;
    case ACTOR_OBJ_KIBAKO:   return (s16)OBJECT_GAMEPLAY_DANGEON_KEEP;
    case ACTOR_OBJ_HAMISHI:  return (s16)OBJECT_GAMEPLAY_FIELD_KEEP;
    default:                 return -1;
    }
}

static Actor* SpawnOneGhost(PlayState* play, const PropVariant& v) {
    // CRITICAL: spawn at the local player's world position, NOT at
    // (0,-9999,0). Several prop actors (OBJ_TSUBO, EN_ISHI, EN_KUSA, ...)
    // call a floor-snap raycast in their Init function and Actor_Kill
    // themselves immediately if there's no floor below — i.e. always when
    // spawned at y=-9999. Spawning at the player's pos guarantees a valid
    // floor (player is always on solid ground). We move the ghost back to
    // y=-9999 below once Init succeeds; DrawHiderAsProp positions it onto
    // the player every frame anyway.
    f32 spawnX = 0.0f, spawnY = 0.0f, spawnZ = 0.0f;
    Player* localPlayer = GET_PLAYER(play);
    if (localPlayer != nullptr) {
        spawnX = localPlayer->actor.world.pos.x;
        spawnY = localPlayer->actor.world.pos.y;
        spawnZ = localPlayer->actor.world.pos.z;
    }

    // Some actors (e.g. EN_TEST Stalfos) ignore the spawn params if a
    // certain prerequisite isn't met and self-destruct in Init. Actor_Spawn
    // returns NULL or an actor with draw=NULL in that case — we check both
    // below and just skip that slot.
    Actor* ghost = Actor_Spawn(&play->actorCtx, play, v.actorId,
                                spawnX, spawnY, spawnZ,
                                0, 0, 0, v.params);
    if (ghost == nullptr) return nullptr;

    // CRITICAL: install the actor's Draw function manually for the
    // prop actors whose vanilla Init defers it (see FixDeferredDraw
    // comment). This must happen BEFORE the null-draw check below —
    // otherwise we'd reject every Pot/Bush/Boulder ghost.
    FixDeferredDraw(ghost, (s16)v.actorId);

    if (ghost->draw == nullptr) {
        // Actor truly has no known draw (not in FixDeferredDraw table OR
        // self-killed during Init). Mark it dead and skip the slot.
        Actor_Kill(ghost);
        return nullptr;
    }

    // objBankIndex fixup. For variant actors (OBJ_TSUBO with params bit 8,
    // EN_KUSA params&3, etc.) the bank that contains the chosen DL is
    // NOT the InitVars value — query ResolveBank first, fall back to
    // ActorDB's InitVars objectId only when there's no variant logic.
    // The pre-load step already added every bank that ResolveBank might
    // return, so Object_GetIndex will find it.
    s16 wantedBank = ResolveBank((s16)v.actorId, (s16)v.params);
    if (wantedBank <= 0) {
        ActorDBEntry* db = ActorDB_Retrieve((s16)v.actorId);
        if (db != nullptr && db->valid) wantedBank = (s16)db->objectId;
    }
    if (wantedBank > 0) {
        s32 bankIdx = Object_GetIndex(&play->objectCtx, wantedBank);
        if (bankIdx >= 0) {
            ghost->objBankIndex = (s8)bankIdx;
        }
    }

    // Clean flags: the engine should NOT auto-cull our ghosts and should NOT
    // target them with the Z-targeting reticle or damage AI.
    ghost->flags &= ~(ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE |
                      ACTOR_FLAG_FRIENDLY        | ACTOR_FLAG_DRAW_CULLING_DISABLED);
    ghost->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
    ghost->update  = GhostActorUpdateNoop;
    ghost->destroy = NULL;
    Actor_SetScale(ghost, v.scale);
    ghost->world.pos.y = -9999.0f;  // re-position in case Init moved it
    return ghost;
}

bool SpawnGhostActors(PlayState* play) {
    if (play == nullptr) return false;
    if (!sLoaded) return false;

    // Clear stale pointers from a previous scene first — all 3 axes.
    for (s32 c = 0; c < kCategoryCount; c++) {
        for (s32 i = 0; i < kPropsPerCategory; i++) {
            for (s32 s = 0; s < kStatesPerProp; s++) {
                sGhostActors[c][i][s] = nullptr;
            }
        }
    }

    // Always spawn enemy + NPC ghosts so the hider can cycle to those
    // categories even in the lobby (before the host clicks Confirm Map).
    // If no map is confirmed yet, default to map 0's table. The Scooter-style
    // gMapLoading=0 + pre-loaded objects + objBankIndex fixup below make
    // it safe to spawn cross-scene actors in any scene.
    s32 mapIdx = sLocal.confirmedMap;
    if (mapIdx < 0 || mapIdx >= kMapCount) mapIdx = 0;
    constexpr bool haveMap = true;  // legacy local — always spawn now

    // PRE-LOAD object banks for every prop. Without this, Actor_Spawn
    // fails whenever the scene's static object list doesn't include
    // the prop's required object (OBJ_TSUBO in Hyrule Field, etc.).
    //
    // We can't rely solely on `ActorDB_Retrieve(actor)->objectId` — that
    // returns the actor's STATIC InitVars value, which for many props
    // is OBJECT_GAMEPLAY_KEEP / OBJECT_GAMEPLAY_DANGEON_KEEP (a generic
    // shared bank). The actor's REAL skel/DL data lives in a SEPARATE
    // object the Init function looks up via params bit-masks (e.g.
    // OBJ_TSUBO with params bit 8 unset wants OBJECT_GAMEPLAY_DANGEON_KEEP,
    // params bit 8 set wants OBJECT_TSUBO). We hardcode the union of
    // every "real DL" object needed by our prop set so they're all in
    // the scene bank by the time Actor_Spawn fires.
    //
    // Dedup via std::set — bank has ~128 slots so the cost is negligible.
    {
        std::set<s16> wantedObjects;
        // Explicit list of objects every env/enemy/NPC prop might need.
        // Sourced by inspecting each actor's z_*.c Init function for
        // Object_GetIndex calls.
        static const s16 kExplicitObjects[] = {
            OBJECT_TSUBO,                 // OBJ_TSUBO (Pot — params bit 8 set)
            OBJECT_KUSA,                  // EN_KUSA  (Bush)
            OBJECT_KIBAKO2,               // OBJ_KIBAKO2 (Crate v2)
            OBJECT_BOMBIWA,               // OBJ_BOMBIWA (Boulder, bombable)
            OBJECT_BOX,                   // EN_BOX (Chest)
            OBJECT_GS,                    // EN_GS (Gossip Stone)
            OBJECT_KANBAN,                // EN_KANBAN (Sign)
            OBJECT_SYOKUDAI,              // OBJ_SYOKUDAI (Torch)
            // EN_ISHI (rocks), OBJ_HAMISHI (big boulder), OBJ_KIBAKO (Crate v1)
            // and some OBJ_TSUBO variants share these two keeps:
            OBJECT_GAMEPLAY_FIELD_KEEP,   // ISHI, HAMISHI
            OBJECT_GAMEPLAY_DANGEON_KEEP, // KIBAKO, TSUBO (params bit 8 clear)
        };
        for (s16 oid : kExplicitObjects) wantedObjects.insert(oid);
        // Also gather every prop's static InitVars objectId via ActorDB
        // — covers anything we forget in the explicit list above.
        auto collectObj = [&](const PropEntry* e) {
            if (e == nullptr || e->states.empty()) return;
            for (const auto& st : e->states) {
                ActorDBEntry* db = ActorDB_Retrieve((s16)st.actorId);
                if (db != nullptr && db->valid) wantedObjects.insert(db->objectId);
            }
        };
        for (s32 i = 0; i < kPropsPerCategory; i++) {
            collectObj(GetPropEntry(CAT_ENVIRONMENT, i, 0));
        }
        if (haveMap) {
            for (s32 c = CAT_ENEMIES; c <= CAT_NPCS; c++) {
                for (s32 i = 0; i < kPropsPerCategory; i++) {
                    collectObj(GetPropEntry(c, i, mapIdx));
                }
            }
        }
        for (s16 objectId : wantedObjects) {
            if (objectId <= 0) continue;
            // Skip if already loaded into the scene's bank.
            if (Object_GetIndex(&play->objectCtx, objectId) >= 0) continue;
            // Skip if the bank is full — Object_Spawn would assert.
            if (play->objectCtx.num >= OBJECT_EXCHANGE_BANK_MAX - 1) break;
            Object_Spawn(&play->objectCtx, objectId);
        }
    }

    s32 spawned = 0, failed = 0;

    // Scooter's hack — temporarily clear gMapLoading so Actor_Spawn allows
    // actors whose objects aren't in the current scene's static table
    // (returns a non-NULL actor with objBankIndex=0). Without this, e.g.
    // Stalfos can't spawn in Hyrule Field and the enemy category stays
    // empty. Restore the saved value after we're done.
    int savedMapLoading = gMapLoading;
    gMapLoading = 0;
    // Also defang the "room cleared → enemy spawn rejected" check in
    // Actor_Spawn (z_actor.c:3445). Hyrule Field's room is marked cleared
    // (no real enemies), which would block our enemy-category ghosts. Save
    // every room's clear bit, zero the whole word, restore after.
    u32 savedClearFlags = (play != nullptr) ? play->actorCtx.flags.clear : 0;
    if (play != nullptr) play->actorCtx.flags.clear = 0;

    // Spawn ONE GHOST PER VARIANT (state). The actor's `params` are baked
    // into Actor_Spawn — so without a separate ghost per state, props that
    // render differently per params (EN_KANBAN params=3 = directional sign,
    // EN_GS params=1/2 = different gossip stones, EN_BOX chest variants,
    // OBJ_BOMBIWA / OBJ_HAMISHI for boulder state>=1, EN_ITEM00 params=3/6
    // for hearts, EN_KUSA params=2 for forest bush, etc.) all rendered as
    // the state-0 actor with the wrong params. Now each state gets its
    // own ghost; DrawHiderAsProp picks the right one by propState.
    auto spawnAllStates = [&](s32 category, s32 i, const PropEntry* entry) {
        if (entry == nullptr || entry->states.empty()) return;
        s32 maxStates = (s32)entry->states.size();
        if (maxStates > kStatesPerProp) maxStates = kStatesPerProp;
        for (s32 s = 0; s < maxStates; s++) {
            Actor* ghost = SpawnOneGhost(play, entry->states[s]);
            if (ghost == nullptr) { failed++; continue; }
            sGhostActors[category][i][s] = ghost;
            spawned++;
        }
    };

    // Category 0: Environment (global table, always spawn).
    for (s32 i = 0; i < kPropsPerCategory; i++) {
        spawnAllStates(CAT_ENVIRONMENT, i, GetPropEntry(CAT_ENVIRONMENT, i, 0));
    }

    // Categories 1 + 2: Enemies + NPCs — now ALWAYS spawned. mapIdx is
    // clamped to 0..kMapCount-1 above so we always index into a valid
    // per-map row. Lets the hider cycle to NPC / Enemy categories in
    // the lobby before the host has confirmed a map.
    for (s32 c = CAT_ENEMIES; c <= CAT_NPCS; c++) {
        for (s32 i = 0; i < kPropsPerCategory; i++) {
            spawnAllStates(c, i, GetPropEntry(c, i, mapIdx));
        }
    }

    if (play != nullptr) play->actorCtx.flags.clear = savedClearFlags;
    gMapLoading = savedMapLoading;

    SPDLOG_INFO("[Harpoon][PropHunt] ghost actors: spawned={} failed={} map={} haveMap={}",
                spawned, failed, mapIdx, haveMap ? "yes" : "no");
    return spawned > 0;
}

void DestroyGhostActors(PlayState* /*play*/) {
    for (s32 c = 0; c < kCategoryCount; c++) {
        for (s32 i = 0; i < kPropsPerCategory; i++) {
            for (s32 s = 0; s < kStatesPerProp; s++) {
                sGhostActors[c][i][s] = nullptr;
            }
        }
    }
}

bool AreGhostsReady() {
    // Scan ALL slots (cat × prop × state) — return true as long as we
    // have at least one usable ghost. The previous check at
    // [CAT_ENVIRONMENT][0] alone was brittle: if the first env prop's
    // first state's actor (e.g. OBJ_TSUBO) wasn't in the current scene's
    // object bank, slot [0][0][0] stayed null even though many other
    // slots spawned successfully. Without this, every hider in Hyrule
    // Field (which doesn't ship OBJ_TSUBO in its bank) rendered as
    // vanilla Link instead of the prop they selected.
    for (s32 c = 0; c < kCategoryCount; c++) {
        for (s32 i = 0; i < kPropsPerCategory; i++) {
            for (s32 s = 0; s < kStatesPerProp; s++) {
                Actor* g = sGhostActors[c][i][s];
                if (g != nullptr && g->draw != nullptr) return true;
            }
        }
    }
    return false;
}

static u32 sRoundElapsedFrames = 0;

// Passive ammo + magic regeneration while the local player is a Seeker in
// the active round. Port of TT's TickPassiveRegen (TriforceThief.cpp:1343)
// with PH-specific cadence: ammo +1 every 30 frames (2/sec per slot),
// magic +8 every 60 frames (8/sec, fills 96-cap in ~12 sec). Hider role
// and lobby state never regenerate — gate is explicit. Each client runs
// this for themselves; no broadcast.
static void TickSeekerPassiveRegen() {
    if (Harpoon::Instance == nullptr) return;
    if (Harpoon::Instance->gameState != HARPOON_STATE_PLAYING) return;
    if (sLocal.role != Role::Seeker) return;
    if (gPlayState == nullptr) return;
    if (gSaveContext.gameMode != GAMEMODE_NORMAL) return;

    // Ammo: +1 per slot every 30 frames. Slots match TT's regen targets so
    // every seeker-usable ammo type refills uniformly.
    static s32 ammoTick = 0;
    if (++ammoTick >= 30) {
        ammoTick = 0;
        s32 maxBombs   = CAPACITY(UPG_BOMB_BAG,    CUR_UPG_VALUE(UPG_BOMB_BAG));
        s32 maxArrows  = CAPACITY(UPG_QUIVER,      CUR_UPG_VALUE(UPG_QUIVER));
        s32 maxSeeds   = CAPACITY(UPG_BULLET_BAG,  CUR_UPG_VALUE(UPG_BULLET_BAG));
        s32 maxNuts    = CAPACITY(UPG_NUTS,        CUR_UPG_VALUE(UPG_NUTS));
        s32 maxSticks  = CAPACITY(UPG_STICKS,      CUR_UPG_VALUE(UPG_STICKS));
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
        bump(SLOT_BOMBCHU,   50);   // chu count independent of bomb bag
    }

    // Magic: +8 every 60 frames. Cap at magicCapacity (forced to 96 by
    // ApplySeekerSave). isMagicAcquired is set to true by ApplySeekerSave
    // so this branch always reaches in seeker state.
    static s32 magicTick = 0;
    if (++magicTick >= 60) {
        magicTick = 0;
        if (gSaveContext.isMagicAcquired) {
            s16 maxMagic = gSaveContext.magicCapacity;
            if (maxMagic <= 0) maxMagic = 96;
            if (gSaveContext.magic < maxMagic) {
                s32 newMagic = gSaveContext.magic + 8;
                gSaveContext.magic = (s16)((newMagic > maxMagic) ? maxMagic : newMagic);
            }
        }
    }
}

void TickFrame() {
    bool isHost = (Harpoon::Instance != nullptr &&
                   Harpoon::Instance->ownClientId != 0 &&
                   Harpoon::Instance->ownClientId == Harpoon::Instance->hostClientId);

    // INVISIBLE WALL — mirrors Scooter's PropHunt_PushBackToSafe. While
    // a round is active, save the player's last "safe" position every
    // frame (when they're NOT close to a known loading-zone actor). The
    // moment they wander within range of a grotto / boss-warp, snap them
    // back to the safe pos and zero all velocity. Engine's transition
    // cancel is the backstop; the push-back keeps the player visibly
    // away from the trigger so it feels like an actual wall.
    if (Harpoon::Instance != nullptr &&
        Harpoon::Instance->isPropHuntMode &&
        gPlayState != nullptr &&
        (Harpoon::Instance->gameState == HARPOON_STATE_PLAYING ||
         Harpoon::Instance->gameState == HARPOON_STATE_HIDING_PHASE)) {
        Player* localPlayer = GET_PLAYER(gPlayState);
        if (localPlayer != nullptr) {
            // Scene-exit-polygon-based block (mirrors Scooter's
            // TriforceThief_PushBackToSafe + IsNearExit). Detect via the
            // engine's setupExitList: every loading-zone in the scene file
            // is a tagged floor poly whose `SurfaceType_GetSceneExitIndex`
            // returns non-zero. This catches ALL load zones (grottos, scene
            // exits, dungeon doors, water-warps) regardless of which actor
            // hosts them. The previous DOOR_ANA / DOOR_WARP1 actor scan
            // missed every scene-exit poly that isn't a door actor.
            //
            // Grace period: skip blocking for the first 60 frames after a
            // scene load so the engine's legitimate arrival respawn doesn't
            // get cancelled (PLAYER_STATE1_LOADING is set briefly post-
            // teleport). sFramesSinceLoad resets on every confirmed-map
            // teleport via the TeleportToEntrance helper.
            static Vec3f sLastSafePos = { 0, 0, 0 };
            static bool  sHasSafePos  = false;
            static s32   sFramesSinceLoad = 0;
            static s16   sPrevSceneNum    = -1;
            if (gPlayState->sceneNum != sPrevSceneNum) {
                sPrevSceneNum    = gPlayState->sceneNum;
                sFramesSinceLoad = 0;
                sHasSafePos      = false;
            } else if (sFramesSinceLoad < 1000) {
                sFramesSinceLoad++;
            }

            auto isBlockedExit = [&](CollisionPoly* poly, s32 bgId) -> bool {
                if (poly == nullptr || gPlayState->setupExitList == nullptr) return false;
                u32 exitIdx = SurfaceType_GetSceneExitIndex(
                    &gPlayState->colCtx, poly, bgId);
                return exitIdx != 0;  // any tagged exit = blocked
            };

            // Probe player's current floor + 8 outward rays at 30u radius
            // so the wall feels solid before they reach the trigger volume.
            auto isNearExit = [&]() -> bool {
                if (isBlockedExit(localPlayer->actor.floorPoly,
                                  localPlayer->actor.floorBgId)) {
                    return true;
                }
                constexpr f32 kProbeR = 30.0f;
                for (int i = 0; i < 8; i++) {
                    s16 ang = (s16)(i * (0x10000 / 8));
                    Vec3f p;
                    p.x = localPlayer->actor.world.pos.x + Math_SinS(ang) * kProbeR;
                    p.y = localPlayer->actor.world.pos.y + 50.0f;
                    p.z = localPlayer->actor.world.pos.z + Math_CosS(ang) * kProbeR;
                    CollisionPoly* outPoly = nullptr;
                    s32 outBgId = 0;
                    BgCheck_EntityRaycastFloor3(
                        &gPlayState->colCtx, &outPoly, &outBgId, &p);
                    if (isBlockedExit(outPoly, outBgId)) return true;
                }
                return false;
            };

            auto pushBackToSafe = [&]() {
                if (sHasSafePos) {
                    localPlayer->actor.world.pos = sLastSafePos;
                    localPlayer->actor.home.pos  = sLastSafePos;
                }
                localPlayer->linearVelocity    = 0.0f;
                localPlayer->actor.velocity.x  = 0.0f;
                localPlayer->actor.velocity.y  = 0.0f;
                localPlayer->actor.velocity.z  = 0.0f;
            };

            // Active block only after the grace period — first second of
            // scene-load is the engine's own arrival animation.
            if (sFramesSinceLoad > 60) {
                // Backstop: an unauthorized TRANS_TRIGGER_START reached us.
                // Cancel the trigger AND mode, clear the locking state
                // flags, push back. Mirrors Scooter's Layer-1 cancel.
                if (gPlayState->transitionTrigger == TRANS_TRIGGER_START &&
                    !::sHarpoonAuthorizedTransition) {
                    gPlayState->transitionTrigger = TRANS_TRIGGER_OFF;
                    gPlayState->transitionMode    = TRANS_MODE_OFF;
                    localPlayer->stateFlags1 &= ~(PLAYER_STATE1_LOADING |
                                                  PLAYER_STATE1_IN_CUTSCENE);
                    pushBackToSafe();
                }
                // Proactive: poly probe sees a tagged exit nearby.
                else if (isNearExit()) {
                    pushBackToSafe();
                }
                // Otherwise we're walking on safe ground — latch the pos
                // so the next push-back has somewhere to send us. Only
                // when no transition is in flight (mode==OFF) so we don't
                // capture a mid-transition position as "safe".
                else if (gPlayState->transitionMode == TRANS_MODE_OFF) {
                    sLastSafePos = localPlayer->actor.world.pos;
                    sHasSafePos  = true;
                }
            }
        }
    }

    // Heartbeat re-broadcast of our disguise. Without this, a seeker who
    // joins the room AFTER a hider entered prop mode never learns the
    // hider's prop selection — the input handler only sends SET_DISGUISE
    // on prop change. Every 2 seconds we re-send so late joiners
    // resolve to the correct prop within at most 2s of joining.
    static s32 sDisguiseHeartbeat = 0;
    if (Harpoon::Instance != nullptr && Harpoon::Instance->isConnected &&
        IsLocalHiderWithProp()) {
        if (--sDisguiseHeartbeat <= 0) {
            sDisguiseHeartbeat = 120;  // ~2 sec at 60 fps
            Harpoon::Instance->SendJsonToRemote(BuildSetDisguisePayload());
        }
    } else {
        sDisguiseHeartbeat = 0;
    }

    if (sLocal.inHidePhase && sLocal.hidePhaseFramesRemaining > 0) {
        sLocal.hidePhaseFramesRemaining -= 1;
        if (sLocal.hidePhaseFramesRemaining == 0) {
            // When the host's timer hits zero, BROADCAST hide-phase-end so
            // every peer transitions in lockstep (and seekers teleport in).
            // Non-host clients just trip the local fallback and wait for
            // the host's broadcast — but if the host disconnects, the
            // fallback prevents seekers from staying frozen forever.
            LocallyEndHidePhase();
            if (isHost && Harpoon::Instance != nullptr) {
                Harpoon::Instance->SendJsonToRemote(BuildHidePhaseEndPayload());
            }
        }
    }
    // Boss Rush–style survival timer. Ticks ONLY while:
    //   - the local player is a Hider (seekers see their last value, frozen)
    //   - we're in an active round (HIDING_PHASE or PLAYING — not lobby)
    // Per user spec: timer ticks indefinitely as long as you're a hider in
    // a map; pauses the moment you become a seeker or return to the lobby.
    if (Harpoon::Instance != nullptr &&
        IsHider() &&
        (Harpoon::Instance->gameState == HARPOON_STATE_HIDING_PHASE ||
         Harpoon::Instance->gameState == HARPOON_STATE_PLAYING)) {
        sRoundElapsedFrames++;
    }
    // Mirror our cumulative counter to the engine's playTimer every frame
    // so Interface_DrawTotalGameplayTimer renders our value. The engine
    // ticks game logic at 20 fps (see z_play.c:1180 `playTimer++` and the
    // comment in gameplaystats.h: "game time counts frames at 20fps/2"
    // — formatTimestampGameplayStat then divides by 10 again to get
    // decisecond precision, yielding 1 visible second per real second at
    // 20 ticks/sec). TickFrame fires at the same 20 fps via
    // OnGameFrameUpdate, so sRoundElapsedFrames advances 20 units per
    // real second too — 1:1 mapping, no multiplier. With *2 the timer
    // displayed 2x faster than the hide-phase countdown (which also
    // counts at *20-per-second). This overwrites the save's underlying
    // playTimer — fine because PH uses a sentinel fileNum (0xFD) that
    // never persists to disk.
    if (Harpoon::Instance != nullptr && Harpoon::Instance->isPropHuntMode) {
        gSaveContext.ship.stats.playTimer = (s32)sRoundElapsedFrames;
    }

    // Damage-cooldown countdown (per-frame). Set to 200 by the
    // OnPlayerHealthChange damage-detransform path; while > 0 the R
    // toggle and prop-cycle inputs refuse to re-disguise the hider.
    if (sLocal.propModeLockoutTimer > 0) {
        sLocal.propModeLockoutTimer--;
    }

    // Seeker passive regen — runs only for local seekers in PLAYING.
    // Internal gates ensure no effect when hider / in lobby / hide phase.
    TickSeekerPassiveRegen();

    // ROUND-END on no-hiders. Host-only. Only runs during PLAYING — NOT
    // HIDING_PHASE. During hide phase, peer role assignments may not yet
    // have propagated to the host's local clients map (or vice versa),
    // so a transient hiderCount=0 would falsely end the round one frame
    // after it started. By the time we hit PLAYING, every peer has
    // received ROLE_ASSIGN and the count is authoritative.
    //
    // Additional safety: the round only ends after we've ACTUALLY seen
    // hiderCount > 0 at least once during this PLAYING phase. Without
    // this gate, a degenerate state (host is seeker, peer-hider's role
    // packet hasn't reached the host's clients map yet because of
    // packet ordering / late join) makes the check fire one frame after
    // PLAYING begins and ends the round before anyone can play. Reset
    // on every PLAYING entry so we re-arm cleanly between rounds.
    static bool sSeenAnyHider = false;
    static HarpoonGameState sPrevTickState = HARPOON_STATE_LOBBY;
    if (Harpoon::Instance != nullptr) {
        HarpoonGameState now = Harpoon::Instance->gameState;
        if (now == HARPOON_STATE_PLAYING && sPrevTickState != HARPOON_STATE_PLAYING) {
            sSeenAnyHider = false;
        }
        sPrevTickState = now;
    }
    if (isHost && Harpoon::Instance != nullptr &&
        Harpoon::Instance->gameState == HARPOON_STATE_PLAYING) {
        static s32 sNoHiderTicks = 0;
        // Count peers in the clients map, then add ourselves from sLocal.
        // The host's own entry isn't reliably present in `clients` (server
        // rosters often exclude the recipient), so walking `clients` alone
        // misses the host-as-hider — that was making the round end the
        // moment PLAYING began when the host was the sole hider.
        s32 hiderCount = 0;
        uint32_t ownId = Harpoon::Instance->ownClientId;
        for (auto& [cid, c] : Harpoon::Instance->clients) {
            if (cid == ownId) continue;     // counted via sLocal below
            if (!c.online) continue;
            if (c.role == "hider") hiderCount++;
        }
        if (sLocal.role == Role::Hider) hiderCount++;

        if (hiderCount > 0) {
            sSeenAnyHider = true;
            sNoHiderTicks = 0;
        } else if (sSeenAnyHider) {
            sNoHiderTicks++;
            // 60 frames = ~1 sec at 60 fps — gives a recently-converted
            // hider time to broadcast their seeker role assignment before
            // we conclude the round.
            if (sNoHiderTicks >= 60) {
                sNoHiderTicks = 0;
                sSeenAnyHider = false;
                SPDLOG_INFO("[Harpoon][PropHunt] all hiders found -> ending round");
                nlohmann::json env;
                env["type"]       = "ROOM.BROADCAST_EVENT";
                env["event_name"] = "PROP_HUNT.ROUND_RESULT";
                env["data"]       = nlohmann::json::object();
                env["data"]["winnerSide"] = "seekers";
                Harpoon::Instance->SendJsonToRemote(env);
                // Local apply (relay excludes sender).
                HarpoonPropHunt::HandleEvent(env);
            }
        }
        // else: hider count == 0 but we've never seen one yet —
        // probably packet-ordering race on round start. Wait.
    }

    // Everyone-votes tally — only the host runs this. When every online
    // client has voted, pick the most-voted map (ties broken by lowest
    // index) and broadcast MAP_CONFIRMED. Without this the everyone-votes
    // mode would hang forever waiting on a manual A-press.
    if (isHost && Harpoon::Instance != nullptr &&
        Harpoon::Instance->gameState == HARPOON_STATE_MAP_SELECT &&
        Harpoon::Instance->mapSelectMode == MAP_SELECT_EVERYONE_CHOOSES) {
        // 15-second deadline mirrors TT's everyone-votes flow. Counted in
        // ProcessIncomingPacketQueue frames (~60 fps).
        constexpr s32 kVoteWindowFrames = 15 * 60;
        if (sMapVoteDeadline > 0) sMapVoteDeadline--;
        else if (sMapVoteDeadline == 0 && !sMapVoteArmed) {
            // First frame in EVERYONE_CHOOSES this round — arm the timer.
            sMapVoteDeadline = kVoteWindowFrames;
            sMapVoteArmed = true;
        }

        // Strict tally: every online player (host included) must press A
        // to register a vote. Non-voters drop at the deadline; the winner
        // is the most-voted map (ties → lowest index). No phantom vote for
        // the host's hovered cell — if they didn't press A, they don't
        // count.
        bool allVoted = true;
        s32 onlineCount = 0;
        for (auto& [cid, c] : Harpoon::Instance->clients) {
            if (!c.online) continue;
            onlineCount++;
            if (!c.hasVoted) { allVoted = false; }
        }
        bool timeout = (sMapVoteArmed && sMapVoteDeadline <= 0);
        if ((allVoted && onlineCount > 0) || timeout) {
            std::unordered_map<s32, s32> tally;
            for (auto& [cid, c] : Harpoon::Instance->clients) {
                if (c.online && c.hasVoted) tally[c.mapSelectIndex]++;
            }
            s32 winner = 0;
            if (tally.empty()) {
                // Nobody voted by the deadline — fall back to the first
                // map so we never stall. (Scooter's behaviour: random pick,
                // but a deterministic default is easier to debug.)
                winner = 0;
            } else {
                s32 best = -1;
                for (auto& [idx, count] : tally) {
                    if (count > best || (count == best && idx < winner)) {
                        best = count; winner = idx;
                    }
                }
            }
            for (auto& [cid, c] : Harpoon::Instance->clients) c.hasVoted = false;
            sMapVoteDeadline = 0;
            sMapVoteArmed    = false;
            HostStartRound(winner);
        }
    } else {
        // Out of MAP_SELECT / EVERYONE_CHOOSES — clear vote state so the
        // next round starts with a fresh deadline.
        sMapVoteDeadline = 0;
        sMapVoteArmed    = false;
    }
}

u32 GetRoundElapsedFrames() { return sRoundElapsedFrames; }
void ResetRoundElapsed()    { sRoundElapsedFrames = 0; }

// ---------------------------------------------------------------------------
// Local decoy ring — 3 slots, FIFO. Mirrors Scooter's somariaDecoy* fields
// but lives in our own namespace to keep CustomItemState out of the picture.
// ---------------------------------------------------------------------------

namespace {
std::array<DecoyEntry, kDecoyMax> sDecoys{};
u8                                sDecoyCount  = 0;
u8                                sDecoyOldest = 0;
}

const std::array<DecoyEntry, kDecoyMax>& GetLocalDecoys() { return sDecoys; }

void ClearDecoys() {
    for (auto& d : sDecoys) d.active = false;
    sDecoyCount  = 0;
    sDecoyOldest = 0;
}

// Visual + audio FX when a decoy is born — ported from Scooter's
// PropHunt_SpawnDecoy. Renders a quick white shockwave + 8 blue sparkles
// in a radial pattern and plays the "magic fire" SFX. Caller spawns the
// effects on the local player's position; remote clients see nothing
// because the broadcast carries only the decoy data (not the FX trigger).
// If you want peers to hear/see the FX too, broadcast a small "play VFX"
// event right after — left as a follow-up to keep the wire format tight.
static void PropHunt_SpawnDecoyFx(PlayState* play, Player* player) {
    Vec3f zeroVec = { 0.0f, 0.0f, 0.0f };
    Vec3f flashPos = player->actor.world.pos;
    flashPos.y += 20.0f;
    EffectSsBlast_SpawnWhiteShockwave(play, &flashPos, &zeroVec, &zeroVec);

    Color_RGBA8 primColor = { 80, 150, 255, 255 };
    Color_RGBA8 envColor  = { 40,  80, 200, 255 };
    Vec3f accel = { 0.0f, 0.0f, 0.0f };
    for (u8 i = 0; i < 8; i++) {
        // 8 evenly-spaced angles around the player (s16 angle wraps in 65536).
        s16 angleS = (s16)((f32)i * (65536.0f / 8.0f));
        Vec3f pos;
        pos.x = player->actor.world.pos.x + Math_SinS(angleS) * 25.0f;
        pos.y = player->actor.world.pos.y + 15.0f + Rand_ZeroFloat(10.0f);
        pos.z = player->actor.world.pos.z + Math_CosS(angleS) * 25.0f;
        Vec3f vel;
        vel.x = Math_SinS(angleS) * 1.0f;
        vel.y = Rand_ZeroFloat(1.5f) + 0.5f;
        vel.z = Math_CosS(angleS) * 1.0f;
        EffectSsKiraKira_SpawnFocused(play, &pos, &vel, &accel,
                                       &primColor, &envColor, 600, 25);
    }
    Audio_PlayActorSound2(&player->actor, NA_SE_PL_MAGIC_FIRE);
}

void SpawnDecoy() {
    // Decoys are a round-only mechanic — gate on Hider role explicitly
    // since IsLocalHiderWithProp now also returns true in the lobby.
    if (!IsHider()) return;
    if (!IsLocalHiderWithProp()) return;
    if (gPlayState == nullptr) return;
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) return;

    // Pick a slot — FIFO: replace oldest when full, otherwise fill empties.
    u8 slot;
    if (sDecoyCount >= kDecoyMax) {
        slot = sDecoyOldest;
        sDecoyOldest = (sDecoyOldest + 1) % kDecoyMax;
    } else {
        slot = 0;
        for (u8 i = 0; i < kDecoyMax; i++) {
            if (!sDecoys[i].active) { slot = i; break; }
        }
        sDecoyCount++;
    }

    DecoyEntry& d = sDecoys[slot];
    d.x = player->actor.world.pos.x;
    d.y = player->actor.world.pos.y;
    d.z = player->actor.world.pos.z;
    d.rotY      = player->actor.shape.rot.y;
    d.propCat   = sLocal.propCategory;
    d.propIndex = sLocal.propIndex;
    d.propState = sLocal.propState;
    d.active    = true;

    // Broadcast so peers render the decoy at the same spot. Uses the
    // existing COMBAT.SPAWN_DECOY primitive — our packet carries enough
    // data for receivers to draw the same prop in the same place.
    nlohmann::json env;
    env["type"]       = "COMBAT.SPAWN_DECOY";
    env["slot"]       = slot;
    nlohmann::json payload;
    payload["slot"]      = slot;
    payload["x"]         = d.x;
    payload["y"]         = d.y;
    payload["z"]         = d.z;
    payload["rotY"]      = d.rotY;
    payload["propCat"]   = d.propCat;
    payload["propIndex"] = d.propIndex;
    payload["propState"] = d.propState;
    env["payload"] = payload;
    if (Harpoon::Instance != nullptr) {
        Harpoon::Instance->SendJsonToRemote(env);
    }

    PropHunt_SpawnDecoyFx(gPlayState, player);
    SPDLOG_INFO("[Harpoon][PropHunt] decoy spawned slot={} cat={} idx={} state={}",
                slot, d.propCat, d.propIndex, d.propState);
}

// Host-side full "the round starts now" sequence. Mirrors Scooter's
// implicit Start Game flow that happens when the host's GameState machine
// transitions LOBBY → MAP_SELECT → HIDING_PHASE.
void HostStartRound(s32 mapIndex) {
    if (Harpoon::Instance == nullptr) return;
    bool isHost = (Harpoon::Instance->ownClientId != 0 &&
                   Harpoon::Instance->ownClientId == Harpoon::Instance->hostClientId);
    if (!isHost) return;

    // 1. Pick seekers. Honor any pre-staged `pendingRole` first (set via the
    //    menu's per-peer Hider/Seeker buttons while in lobby), then fill the
    //    remaining seeker slots from the priority queue picking from the pool
    //    of peers WITHOUT a pending role. After consumption every client's
    //    pendingRole gets cleared so the next round starts fresh.
    std::unordered_set<u32> seekerSet;
    std::unordered_set<u32> pendingHider;
    std::vector<u32>        unpinned;
    for (auto& [cid, c] : Harpoon::Instance->clients) {
        if (!c.online) continue;
        if (c.pendingRole == "seeker") {
            seekerSet.insert(cid);
        } else if (c.pendingRole == "hider") {
            pendingHider.insert(cid);
        } else {
            unpinned.push_back(cid);
        }
    }
    if (seekerSet.empty() && pendingHider.empty() && unpinned.empty()) {
        // No-one online — bootstrap with self so the priority queue has a
        // candidate. Matches old behaviour for single-client testing.
        unpinned.push_back(Harpoon::Instance->ownClientId);
    }
    s32 desiredSeekerCount = Host::GetSettings().seekerCount;
    s32 needed = desiredSeekerCount - (s32)seekerSet.size();
    if (needed > 0 && !unpinned.empty()) {
        auto picked = Host::PickNextSeekers(unpinned, needed);
        for (u32 cid : picked) seekerSet.insert(cid);
    }
    // Clear pendingRole on every client now that we've consumed it.
    for (auto& [cid, c] : Harpoon::Instance->clients) {
        c.pendingRole.clear();
    }
    // Keep `seekers` vector for the log message below.
    std::vector<u32> seekers(seekerSet.begin(), seekerSet.end());

    // 2. Resolve own role + apply locally (server relay excludes sender).
    bool ownIsSeeker = seekerSet.count(Harpoon::Instance->ownClientId) > 0;
    sLocal.role = ownIsSeeker ? Role::Seeker : Role::Hider;
    auto myIt = Harpoon::Instance->clients.find(Harpoon::Instance->ownClientId);
    if (myIt != Harpoon::Instance->clients.end()) {
        myIt->second.role = ownIsSeeker ? "seeker" : "hider";
    }
    // If we just became a seeker, drop our lobby disguise immediately and
    // tell every peer. Otherwise our 2-second SET_DISGUISE heartbeat keeps
    // broadcasting the lobby prop we had as a hider — peer rosters see
    // clients[host].propIndex >= 0 with role="seeker" never received
    // (host's role isn't broadcast), so they render us as that prop the
    // entire round.
    if (ownIsSeeker) {
        sLocal.propIndex    = -1;
        sLocal.propCategory = 0;
        sLocal.propState    = 0;
        if (myIt != Harpoon::Instance->clients.end()) {
            myIt->second.propIndex    = -1;
            myIt->second.propCategory = 0;
            myIt->second.propState    = 0;
        }
        if (Harpoon::Instance->isConnected) {
            Harpoon::Instance->SendJsonToRemote(BuildSetDisguisePayload());
        }
    }

    // 3. Broadcast role assignment per peer.
    for (auto& [cid, c] : Harpoon::Instance->clients) {
        if (cid == Harpoon::Instance->ownClientId) continue;
        bool peerIsSeeker = seekerSet.count(cid) > 0;
        Role r = peerIsSeeker ? Role::Seeker : Role::Hider;
        c.role = peerIsSeeker ? "seeker" : "hider";
        Harpoon::Instance->SendJsonToRemote(BuildRoleAssignPayload(cid, r));
    }

    // 4. Locally confirm the map (sets state, resets timer, teleports hider).
    LocallyConfirmMap(mapIndex);

    // 5. Broadcast MAP_CONFIRMED + HIDE_PHASE_BEGIN so peers transition.
    nlohmann::json env;
    env["type"]       = "ROOM.BROADCAST_EVENT";
    env["event_name"] = "PROP_HUNT.MAP_CONFIRMED";
    nlohmann::json d;
    d["mapIndex"] = mapIndex;
    env["data"]   = d;
    Harpoon::Instance->SendJsonToRemote(env);
    Harpoon::Instance->SendJsonToRemote(
        BuildHidePhaseBeginPayload(Host::GetSettings().hideSeconds * 20));

    SPDLOG_INFO("[Harpoon][PropHunt] HostStartRound map={} ownRole={} seekers={}",
                mapIndex, ownIsSeeker ? "seeker" : "hider", (int)seekers.size());
}

// "Apply MAP_CONFIRMED locally" — single source of truth used by every code
// path that confirms a map: local host (A-button or random mode), peer
// receiving the broadcast. The server's relay excludes the sender, so the
// host wouldn't otherwise execute this branch when broadcasting.
void LocallyConfirmMap(s32 mapIndex) {
    if (Harpoon::Instance != nullptr) {
        Harpoon::Instance->selectedMapIndex   = mapIndex;
        Harpoon::Instance->confirmedMapIndex  = mapIndex;
        Harpoon::Instance->gameState          = HARPOON_STATE_HIDING_PHASE;
    }
    sLocal.confirmedMap = mapIndex;
    // NOTE: sRoundElapsedFrames is NOT reset here. Per user spec the PH
    // timer is cumulative across all rounds in a session (total survival
    // time as hider). Only resets on room-join / disconnect.
    sLocal.inHidePhase = true;
    sLocal.hidePhaseFramesRemaining = Host::GetSettings().hideSeconds * 20;

    if (gPlayState != nullptr && sLocal.role == Role::Hider) {
        s32 entr = GetEntranceForMapIndex(mapIndex);
        gSaveContext.linkAge = LINK_AGE_CHILD;
        TeleportToEntrance(entr);
        SetPendingInit(1);  // hider preset post-load
    }
    // Seekers stay in the lobby (Hyrule Field) during the hide phase, then
    // teleport on LocallyEndHidePhase. Matches Scooter's two-stage flow.
}

void LocallyEndHidePhase() {
    sLocal.inHidePhase = false;
    sLocal.hidePhaseFramesRemaining = 0;
    if (Harpoon::Instance != nullptr) {
        Harpoon::Instance->gameState = HARPOON_STATE_PLAYING;
    }
    if (sLocal.role == Role::Seeker && gPlayState != nullptr &&
        Harpoon::Instance != nullptr && Harpoon::Instance->confirmedMapIndex >= 0) {
        s32 entr = GetEntranceForMapIndex(Harpoon::Instance->confirmedMapIndex);
        gSaveContext.linkAge = LINK_AGE_CHILD;
        TeleportToEntrance(entr);
        SetPendingInit(2);  // seeker preset post-load
    }
}

// Map-select index → entrance constant. Same order as kMaps in
// PropHuntMapSelect.cpp.
s32 GetEntranceForMapIndex(s32 mapIndex) {
    static const s32 kEntrances[] = {
        0x0DB,  // Kakariko Village
        0x13D,  // Death Mountain
        0x0098, // Bottom of the Well
        0x129,  // Gerudo Fortress
        0x169,  // Forest Temple
        0x0EA,  // Zora's River
        0x004,  // Dodongo's Cavern
        0x467,  // Ganon's Castle
        0x0EE,  // Kokiri Forest
    };
    if (mapIndex < 0 || mapIndex >= (s32)(sizeof(kEntrances) / sizeof(kEntrances[0]))) {
        return 0x0CD;  // Hyrule Field fallback
    }
    return kEntrances[mapIndex];
}

// ---------------------------------------------------------------------------
// Per-round scene-lock cluster tables. Each entry is the set of scenes
// considered "inside" the round for the given map index. Loading zones
// whose destination is outside the cluster get redirected back to the
// round map (see GetReturnEntranceForInvalidExit).
//
// Conservative v1: most maps are single-scene (strict lock). Zora's River
// allows the natural River → Domain → Fountain cluster since those are
// tightly linked and feel like one continuous area.
// ---------------------------------------------------------------------------

namespace {
struct ClusterDef { const s8* scenes; s32 count; };

static const s8 sCluster_Kakariko[]   = { SCENE_KAKARIKO_VILLAGE };
static const s8 sCluster_DeathMtn[]   = { SCENE_DEATH_MOUNTAIN_TRAIL };
static const s8 sCluster_BotW[]       = { SCENE_BOTTOM_OF_THE_WELL };
static const s8 sCluster_Gerudo[]     = { SCENE_GERUDOS_FORTRESS };
static const s8 sCluster_ForestTmp[]  = { SCENE_FOREST_TEMPLE };
static const s8 sCluster_ZorasRiver[] = {
    SCENE_ZORAS_RIVER, SCENE_ZORAS_DOMAIN, SCENE_ZORAS_FOUNTAIN,
};
static const s8 sCluster_Dodongo[]    = { SCENE_DODONGOS_CAVERN };
static const s8 sCluster_Ganon[]      = { SCENE_INSIDE_GANONS_CASTLE };
static const s8 sCluster_Kokiri[]     = { SCENE_KOKIRI_FOREST };

static const ClusterDef sClusterByMap[] = {
    { sCluster_Kakariko,   (s32)ARRAY_COUNT(sCluster_Kakariko)   },
    { sCluster_DeathMtn,   (s32)ARRAY_COUNT(sCluster_DeathMtn)   },
    { sCluster_BotW,       (s32)ARRAY_COUNT(sCluster_BotW)       },
    { sCluster_Gerudo,     (s32)ARRAY_COUNT(sCluster_Gerudo)     },
    { sCluster_ForestTmp,  (s32)ARRAY_COUNT(sCluster_ForestTmp)  },
    { sCluster_ZorasRiver, (s32)ARRAY_COUNT(sCluster_ZorasRiver) },
    { sCluster_Dodongo,    (s32)ARRAY_COUNT(sCluster_Dodongo)    },
    { sCluster_Ganon,      (s32)ARRAY_COUNT(sCluster_Ganon)      },
    { sCluster_Kokiri,     (s32)ARRAY_COUNT(sCluster_Kokiri)     },
};
}  // anon

bool IsSceneInRoundCluster(s32 mapIndex, s32 sceneNum) {
    if (mapIndex < 0 || mapIndex >= (s32)ARRAY_COUNT(sClusterByMap)) return false;
    const ClusterDef& def = sClusterByMap[mapIndex];
    for (s32 i = 0; i < def.count; i++) {
        if ((s32)def.scenes[i] == sceneNum) return true;
    }
    return false;
}

s32 GetReturnEntranceForInvalidExit(s32 mapIndex, s32 destSceneNum) {
    // v1: redirect to the round map's main entrance. Per-(map, dest)
    // precision (e.g. distinct return spawn depending on which exit was
    // taken) can be layered on top by extending this lookup.
    (void)destSceneNum;
    return GetEntranceForMapIndex(mapIndex);
}

// ===========================================================================
// Host-side state machine
// ===========================================================================

namespace Host {

namespace {
    Settings                       sSettings;
    std::unordered_set<u32>        sSeekerHistory;   // cids who've been seeker this rotation
    std::unordered_map<u32, u32>   sClientTimers;    // cid -> total seconds as hider this game
}

Settings& GetSettings() { return sSettings; }

void ResetSeekerHistory() {
    sSeekerHistory.clear();
}

bool HasBeenSeeker(u32 clientId) {
    return sSeekerHistory.find(clientId) != sSeekerHistory.end();
}

std::vector<u32> PickNextSeekers(const std::vector<u32>& candidates, s32 seekerCount) {
    if (candidates.empty() || seekerCount <= 0) return {};

    // Pool 1: clients who haven't been seeker yet this rotation.
    std::vector<u32> pool;
    for (u32 cid : candidates) {
        if (!HasBeenSeeker(cid)) pool.push_back(cid);
    }
    // If everyone has been seeker, reset history and rebuild pool.
    if (pool.empty()) {
        sSeekerHistory.clear();
        pool = candidates;
    }

    // Pick `seekerCount` random entries (without replacement) from the pool.
    std::vector<u32> chosen;
    while ((s32)chosen.size() < seekerCount && !pool.empty()) {
        s32 idx = (s32)(rand() % pool.size());
        u32 cid = pool[idx];
        chosen.push_back(cid);
        sSeekerHistory.insert(cid);
        pool.erase(pool.begin() + idx);
    }
    // If we ran out of pool but still need more seekers, dip into the
    // overall candidate list (allows repeats from prior rotations).
    while ((s32)chosen.size() < seekerCount) {
        u32 cid = candidates[rand() % candidates.size()];
        chosen.push_back(cid);
        sSeekerHistory.insert(cid);
    }
    return chosen;
}

u32 GetClientTimer(u32 clientId) {
    auto it = sClientTimers.find(clientId);
    return it == sClientTimers.end() ? 0u : it->second;
}

void AddClientTimerSeconds(u32 clientId, u32 seconds) {
    sClientTimers[clientId] += seconds;
}

void ResetClientTimer(u32 clientId) {
    sClientTimers[clientId] = 0;
}

}  // namespace Host

void TeleportToEntrance(s32 entranceIndex) {
    if (gPlayState == nullptr) return;
    // Keep linkAgeOnLoad in sync with the age the save preset just set so
    // Inventory_SwapAgeEquipment doesn't corrupt mid-transition.
    gPlayState->linkAgeOnLoad     = gSaveContext.linkAge;
    gPlayState->nextEntranceIndex = entranceIndex;
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType    = TRANS_TYPE_FADE_BLACK;
    // Authorize this transition with the round-active blocker (global
    // flag defined just below this namespace). Force global lookup
    // explicitly — MSVC mangles unqualified function-scope `extern` as
    // namespace-qualified, producing an unresolved symbol at link time.
    ::sHarpoonAuthorizedTransition = true;
}

void StartLocalRoundAs(Role role, s32 entranceIndex) {
    if (role == Role::Seeker) {
        ApplySeekerSave();
    } else {
        ApplyHiderSave();
    }
    // If caller passed 0, fall back to common.entrance_index (Hyrule Field).
    if (entranceIndex == 0 && !sSavePresetRaw.is_null() && sSavePresetRaw.contains("common")) {
        entranceIndex = sSavePresetRaw["common"].value("entrance_index", 205);
    }
    TeleportToEntrance(entranceIndex);
}

// ---------------------------------------------------------------------------
// "Big" game-start transition — mirror of Harpoon_InitPropHunt() in Scooter.
// Used when entering Prop Hunt from a non-gameplay state (title screen, file
// select). Sets up gSaveContext from scratch, applies the role preset, then
// fires the gamestate switch so the next frame runs Play_Init.
// ---------------------------------------------------------------------------

void BigStartGameAs(Role role) {
    // 1. Fresh save context.
    Save_InitFile(false);
    // 0xFD = Harpoon multiplayer sentinel. Harpoon::IsSaveLoaded() was
    // extended to accept it (alongside 0/1/2). DO NOT use a real slot here:
    // the tracker overlay autosaves on scene change, and a real slot will
    // get clobbered with our stub save state, corrupting it for next launch.
    // 0xFD is outside SaveManager::MaxFiles (=3), so it's never parsed at
    // startup even though autosave may write file254.sav to disk.
    gSaveContext.fileNum = 0xFD;

    // 2. Apply role preset — items, equipment, button bindings, progression
    //    flags, cvars. Entrance index gets set from common.entrance_index.
    if (role == Role::Seeker) {
        ApplySeekerSave();
    } else {
        ApplyHiderSave();
    }

    // 3. Standard gSaveContext fields the engine expects when transitioning
    //    from menu to gameplay. Lifted verbatim from FileChoose_LoadGame /
    //    Scooter's Harpoon_InitPropHunt.
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
    // Zero a batch of small fields the engine expects clean post-load.
    // Assigned individually so the implicit downcast warning doesn't fire on
    // the chained `forceRisingButtonAlphas = ... = magicCapacity = 0` form
    // Scooter uses verbatim.
    gSaveContext.forceRisingButtonAlphas = 0;
    gSaveContext.unk_13E8 = 0;
    gSaveContext.unk_13EA = 0;
    gSaveContext.unk_13EC = 0;
    gSaveContext.magicCapacity = 0;
    gSaveContext.magicFillTarget = gSaveContext.magic;
    // NB: Scooter zeroes magic here, but our preset already set it. Leave alone.
    gSaveContext.naviTimer = 0;

    // 4. Reinforce child Link for hider (the preset set linkAge but we want
    //    to be extra sure right before transition).
    if (role == Role::Hider) {
        gSaveContext.linkAge = LINK_AGE_CHILD;
    }

    // Seed sLocal.role from the requested preset. Without this, joining a
    // prop_hunt room applies the hider save but leaves sLocal.role at
    // Unassigned — so IsHider() returns false and the R-button prop toggle
    // in HarpoonHookHandlers.cpp:121 silently no-ops until the host later
    // broadcasts a ROLE_ASSIGN. The role still gets overridden by the next
    // HandleRoleAssign event (host's "Start Game" reshuffles seekers), so
    // this is purely a lobby-side default.
    sLocal.role = role;

    // Auto-enter prop mode for hiders so they render as a prop from the
    // first frame. Without this, the user spawns as visible vanilla Link
    // (a free giveaway to seekers) and has to know to press R to disguise.
    // Pressing R later still toggles back to Link / forward through props.
    if (role == Role::Hider) {
        if (sLocal.propCategory < 0 || sLocal.propCategory >= kCategoryCount) {
            sLocal.propCategory = CAT_ENVIRONMENT;
        }
        if (sLocal.propIndex < 0) sLocal.propIndex = 0;
        if (sLocal.propState < 0) sLocal.propState = 0;
    } else {
        // Becoming a seeker (or eliminated). Wipe any leftover prop state
        // from a previous hider stint and tell peers immediately — the
        // host never receives their own ROLE_ASSIGN broadcast, so without
        // this, peers' clients[host].propIndex stays at the last hider
        // heartbeat and the dummy-draw gate keeps rendering the host as
        // a prop on every seeker's screen.
        sLocal.propIndex = -1;
        sLocal.propCategory = 0;
        sLocal.propState = 0;
        if (Harpoon::Instance != nullptr && Harpoon::Instance->isConnected) {
            Harpoon::Instance->SendJsonToRemote(BuildSetDisguisePayload());
        }
    }

    // 5. Stop music, hand off to gameplay.
    Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_STOP);
    if (gGameState != nullptr) {
        gGameState->running = false;
        SET_NEXT_GAMESTATE(gGameState, Play_Init, PlayState);
    }
    GameInteractor_ExecuteOnLoadGame(gSaveContext.fileNum);

    SPDLOG_INFO("[Harpoon][PropHunt] BigStartGameAs role={} entrance=0x{:X}",
                role == Role::Hider ? "hider" :
                role == Role::Seeker ? "seeker" : "unassigned",
                (u32)gSaveContext.entranceIndex);
}

// ---------------------------------------------------------------------------
// Pending init — apply role preset on the *next* OnSceneSpawnActors after
// a teleport. The engine clobbers Inventory_* and equipment changes made
// mid-transition; deferring them until the scene has spawned keeps the kit
// intact.
// ---------------------------------------------------------------------------

static s32 sPendingInit = 0;

void SetPendingInit(s32 type) { sPendingInit = type; }

void ProcessPendingInit() {
    if (sPendingInit == 0) return;
    s32 t = sPendingInit;
    sPendingInit = 0;
    switch (t) {
        case 1: ApplyHiderSave();  break;  // hider
        case 2: ApplySeekerSave(); break;  // seeker
        case 3: ApplySeekerSave(); break;  // converted seeker (died as hider)
        case 4: ApplyHiderSave();  break;  // reset to hider (game over)
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Instant in-place scene reload, copy of soh/Enhancements/mods.cpp's
// SwitchAge() pattern minus the age toggle. linkAgeOnLoad is taken from
// gSaveContext.linkAge so a preset that changed age also reloads with that
// new age. Player position + yaw are preserved via RESPAWN_MODE_DOWN.
// ---------------------------------------------------------------------------

void InstantReloadScene() {
    if (gPlayState == nullptr) return;
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) return;

    gSaveContext.respawnFlag = 1;
    gPlayState->nextEntranceIndex = gSaveContext.entranceIndex;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].entranceIndex = gPlayState->nextEntranceIndex;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].roomIndex     = gPlayState->roomCtx.curRoom.num;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].pos           = player->actor.world.pos;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].yaw           = player->actor.shape.rot.y;
    // 0x0DFF = vanilla "down respawn" params for regular scenes.
    gSaveContext.respawn[RESPAWN_MODE_DOWN].playerParams = 0x0DFF;

    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType    = TRANS_TYPE_INSTANT;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK_FAST;

    // Sync target age with whatever the preset just set so the reload picks
    // up the right link model. If preset didn't change age, this is a no-op.
    gPlayState->linkAgeOnLoad = gSaveContext.linkAge;
}

void ChangeRoleAndReload(Role role) {
    if (gPlayState == nullptr) {
        // Not in gameplay yet — fall through to the big transition.
        BigStartGameAs(role);
        return;
    }
    if (role == Role::Seeker) {
        ApplySeekerSave();
    } else {
        ApplyHiderSave();
    }
    InstantReloadScene();
    SPDLOG_INFO("[Harpoon][PropHunt] role changed -> {} (in-place reload)",
                role == Role::Seeker ? "seeker" : "hider");
}

f32 GetPropVisualScale(s32 category, s32 propIndex, s32 propState, s32 mapIdx) {
    const PropEntry* entry = GetPropEntry(category, propIndex, mapIdx);
    if (entry == nullptr || entry->states.empty()) return 1.0f;
    if (propState < 0 || propState >= (s32)entry->states.size()) propState = 0;
    f32 s = entry->states[propState].scale;
    return (s > 0.0f) ? s : 1.0f;
}

Actor* GetGhostActor(s32 category, s32 propIndex, s32 propState) {
    if (category < 0 || category >= kCategoryCount) return nullptr;
    if (propIndex < 0 || propIndex >= kPropsPerCategory) return nullptr;
    if (propState < 0) propState = 0;
    if (propState >= kStatesPerProp) propState = kStatesPerProp - 1;
    return sGhostActors[category][propIndex][propState];
}

bool DrawHiderAsProp(Actor* playerActor, PlayState* play,
                     s32 category, s32 propIndex, s32 propState, s32 mapIdx) {
    // Rate-limited debug logging — fires every ~5s while a hider with a prop
    // selected is trying to render. Helps diagnose which guard is failing
    // when the prop doesn't show up.
    static s64 sLastLog = 0;
    s64 nowMs = (s64)(ImGui::GetTime() * 1000.0);
    bool shouldLog = (nowMs - sLastLog) > 5000;

    if (!AreGhostsReady()) {
        if (shouldLog) {
            SPDLOG_WARN("[Harpoon][PropHunt] DrawHiderAsProp: ghosts not ready");
            sLastLog = nowMs;
        }
        return false;
    }
    if (playerActor == nullptr || play == nullptr) return false;

    const PropEntry* entry = GetPropEntry(category, propIndex, mapIdx);
    if (entry == nullptr) {
        if (shouldLog) {
            SPDLOG_WARN("[Harpoon][PropHunt] DrawHiderAsProp: entry NULL cat={} idx={} map={}",
                        category, propIndex, mapIdx);
            sLastLog = nowMs;
        }
        return false;
    }
    if (propState < 0 || propState >= (s32)entry->states.size()) propState = 0;
    const PropVariant& v = entry->states[propState];

    // Helper: check whether a ghost is usable (non-null, has draw, object loaded).
    auto ghostUsable = [&](Actor* g) -> bool {
        if (g == nullptr || g->draw == nullptr) return false;
        if (g->objBankIndex < 0) return true;
        return Object_IsLoaded(&play->objectCtx, g->objBankIndex);
    };

    // Per-state lookup. propState was already clamped to entry->states
    // range above; clamp again for safety against the kStatesPerProp
    // storage cap (kept as a hard upper bound for the static array).
    s32 stateIdx = propState;
    if (stateIdx >= kStatesPerProp) stateIdx = kStatesPerProp - 1;

    Actor* ghost = sGhostActors[category][propIndex][stateIdx];
    if (!ghostUsable(ghost)) {
        // Fallback: scan ALL slots (cat × prop × state) and use the first
        // usable ghost. This covers the case where the requested variant's
        // actor isn't in the current scene's object bank. Without this
        // fallback the hider stays visible as vanilla Link instead of
        // disguising — picking ANY available prop is far better than
        // exposing the hider's identity to seekers.
        Actor* alt = nullptr;
        s32 altCat = -1, altIdx = -1, altState = -1;
        for (s32 c = 0; c < kCategoryCount && alt == nullptr; c++) {
            for (s32 i = 0; i < kPropsPerCategory && alt == nullptr; i++) {
                for (s32 s = 0; s < kStatesPerProp; s++) {
                    Actor* g = sGhostActors[c][i][s];
                    if (ghostUsable(g)) {
                        alt = g; altCat = c; altIdx = i; altState = s; break;
                    }
                }
            }
        }
        if (alt == nullptr) {
            if (shouldLog) {
                SPDLOG_WARN("[Harpoon][PropHunt] DrawHiderAsProp: no usable ghost anywhere "
                            "(req cat={} idx={} state={} actorId=0x{:X})",
                            category, propIndex, propState, v.actorId);
                sLastLog = nowMs;
            }
            return false;
        }
        if (shouldLog) {
            SPDLOG_INFO("[Harpoon][PropHunt] DrawHiderAsProp: fallback cat={} idx={} state={} "
                        "(requested cat={} idx={} state={} unusable)",
                        altCat, altIdx, altState, category, propIndex, propState);
            sLastLog = nowMs;
        }
        ghost = alt;
    }

    // Save target actor state.
    Vec3f savedPos = ghost->world.pos;
    Vec3s savedRot = ghost->shape.rot;
    Vec3f savedScale = ghost->scale;
    f32   savedYOffset = ghost->shape.yOffset;

    ghost->world.pos = playerActor->world.pos;
    ghost->shape.rot = playerActor->shape.rot;
    // NB: scale + yOffset come from THIS variant — they're already baked
    // into the spawned ghost by SpawnOneGhost's Actor_SetScale call, so
    // we re-apply here defensively in case the actor's Init or Update
    // overwrote them. Same for yOffset.
    ghost->scale.x = ghost->scale.y = ghost->scale.z = v.scale;
    ghost->shape.yOffset = v.yOffset;

    Matrix_Push();
    Matrix_SetTranslateRotateYXZ(
        ghost->world.pos.x,
        ghost->world.pos.y + (v.yOffset * v.scale),
        ghost->world.pos.z,
        &ghost->shape.rot);
    Matrix_Scale(v.scale, v.scale, v.scale, MTXMODE_APPLY);

    // Segment 6 needs to point at the ghost's object bank for the actor's
    // own draw function to resolve assets correctly. Mirrors Scooter's
    // PropHunt_SetupGhostSegment helper. We can't use OPEN_DISPS/CLOSE_DISPS
    // here — those macros redeclare FrameInterpolation_RecordOpen/CloseChild
    // inline at the call site, and MSVC mangles them with C++ linkage when
    // the surrounding scope is a C++ namespace. We expand manually.
    Actor_SetObjectDependency(play, ghost);
    {
        FrameInterpolation_RecordOpenChild(__FILE__, __LINE__);
        GraphicsContext* __gfxCtx = play->state.gfxCtx;
        Gfx* dispRefs[4];
        Graph_OpenDisps(dispRefs, __gfxCtx, __FILE__, __LINE__);
        gSPSegment(POLY_OPA_DISP++, 0x06,
                   (uintptr_t)play->objectCtx.status[ghost->objBankIndex].segment);
        gSPSegment(POLY_XLU_DISP++, 0x06,
                   (uintptr_t)play->objectCtx.status[ghost->objBankIndex].segment);
        Graph_CloseDisps(dispRefs, __gfxCtx, __FILE__, __LINE__);
        FrameInterpolation_RecordCloseChild();
    }

    ghost->draw(ghost, play);
    Matrix_Pop();

    ghost->world.pos = savedPos;
    ghost->shape.rot = savedRot;
    ghost->scale = savedScale;
    ghost->shape.yOffset = savedYOffset;
    return true;
}

}  // namespace HarpoonPropHunt

// =============================================================================
// C bridge
// =============================================================================

extern "C" {

s32 HarpoonPropHunt_IsActive(void) {
    return (Harpoon::Instance != nullptr && Harpoon::Instance->isPropHuntMode) ? 1 : 0;
}
s32 HarpoonPropHunt_IsHider(void)            { return HarpoonPropHunt::IsHider()      ? 1 : 0; }
s32 HarpoonPropHunt_IsSeeker(void)           { return HarpoonPropHunt::IsSeeker()     ? 1 : 0; }
s32 HarpoonPropHunt_IsEliminated(void)       { return HarpoonPropHunt::IsEliminated() ? 1 : 0; }
s32 HarpoonPropHunt_GetLocalPropCategory(void){ return HarpoonPropHunt::GetLocalState().propCategory; }
s32 HarpoonPropHunt_GetLocalPropIndex(void)  { return HarpoonPropHunt::GetLocalState().propIndex; }
s32 HarpoonPropHunt_GetLocalPropState(void)  { return HarpoonPropHunt::GetLocalState().propState; }
s32 HarpoonPropHunt_GetConfirmedMapIndex(void){ return HarpoonPropHunt::GetLocalState().confirmedMap; }

// Direct prop-draw intercept for z_player.c Player_Draw. Called every
// frame from the actor draw callback BEFORE Player_DrawGameplay runs.
// Returns 1 when we successfully rendered a prop at the player's
// transform — caller must `return` immediately to skip vanilla Link
// draw (skel/limb work, sword trails, etc.). Returns 0 to let vanilla
// proceed (we're not a hider, no prop selected, ghosts not ready, or
// the requested ghost actor isn't loadable this frame).
s32 HarpoonPropHunt_TryDrawLocalProp(Actor* thisx, PlayState* play) {
    using namespace HarpoonPropHunt;
    if (thisx == nullptr || play == nullptr) return 0;
    if (Harpoon::Instance == nullptr || !Harpoon::Instance->isPropHuntMode) return 0;
    if (!IsLocalHiderWithProp()) return 0;
    if (!AreGhostsReady())       return 0;
    const auto& s = GetLocalState();
    s32 mapIdx = (s.confirmedMap >= 0) ? s.confirmedMap : 0;
    return DrawHiderAsProp(thisx, play, s.propCategory, s.propIndex, s.propState, mapIdx)
             ? 1 : 0;
}

}  // extern "C"
