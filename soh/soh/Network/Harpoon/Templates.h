#ifndef SOH_NETWORK_HARPOON_TEMPLATES_H
#define SOH_NETWORK_HARPOON_TEMPLATES_H
#ifdef __cplusplus

#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

extern "C" {
#include "z64.h"
}

namespace HarpoonTemplates {

// A full snapshot of a player's gameplay-relevant save state, used by the
// GM menu's "snapshot current player → save as template → apply to peer"
// flow. Templates are JSON-persisted in soh/harpoon/templates/<name>.json
// so they survive restarts.
//
// NOT included: heart pieces / heart containers (permanent progression),
// scene flags (story progress).
// Mirror of SavedSceneFlags in z64save.h. Lives in our namespace so the
// header doesn't have to drag z64.h around.
struct TplSceneFlags {
    uint32_t chest;
    uint32_t swch;
    uint32_t clear;
    uint32_t collect;
    uint32_t unk;
    uint32_t rooms;
    uint32_t floors;
};

struct Template {
    std::string name;

    // ---- Inventory ---------------------------------------------------------
    //   [ 0..23] vanilla OoT items
    //   [24..47] Shipwright custom items (Roc's Feather, Spinner, …)
    //   [48..71] MM masks
    uint8_t items[72];
    s8      ammo[16];

    // ---- Equipment / upgrades ---------------------------------------------
    uint32_t equipment;
    uint32_t upgrades;

    // ---- Resources --------------------------------------------------------
    int16_t  rupees;
    int16_t  magic;
    int16_t  magicCapacity;
    int16_t  healthCapacity;

    // ---- Quest items ------------------------------------------------------
    uint32_t questItems;
    uint8_t  dungeonItems[20];
    int8_t   dungeonKeys[19];   // small keys per dungeon (no keys for Barinade)
    int16_t  gsTokens;          // gold skulltula tokens counter

    // ---- GM movement restrictions ----------------------------------------
    bool     restrictNoClimb;
    bool     restrictNoGrab;
    bool     restrictNoCrawl;
    bool     restrictNoTalk;

    // ---- Save-state progression flags (full overwrite) -------------------
    uint16_t eventChkInf[14];
    uint16_t itemGetInf[4];
    uint16_t infTable[30];
    uint16_t eventInf[4];

    // Randomizer INF bitmask (variable length — see (RAND_INF_MAX+15)/16).
    std::vector<uint16_t> randomizerInf;

    // ---- File metadata ----------------------------------------------------
    int32_t  fileNum;
    uint8_t  playerName[8];
    uint8_t  filenameLanguage;

    // ---- Defense / Magic acquisition --------------------------------------
    uint8_t  isDoubleDefenseAcquired;
    int16_t  defenseHearts;
    int8_t   magicLevel;
    uint8_t  isMagicAcquired;
    uint8_t  isDoubleMagicAcquired;

    // ---- Time / counters --------------------------------------------------
    uint16_t dayTime;
    int32_t  totalDays;
    uint16_t deaths;
    uint8_t  bgsFlag;
    uint16_t swordHealth;
    int32_t  bgsDayCount;

    // ---- Entrance / scene state ------------------------------------------
    int32_t  entranceIndex;
    int32_t  cutsceneIndex;

    // ---- Timers -----------------------------------------------------------
    uint16_t naviTimer;
    int16_t  timerState;
    int16_t  timerSeconds;
    int16_t  subTimerState;
    int16_t  subTimerSeconds;

    // ---- Settings ---------------------------------------------------------
    uint8_t  audioSetting;
    int16_t  n64ddFlag;
    uint8_t  zTargetSetting;

    // ---- Randomizer-specific scalars -------------------------------------
    uint8_t  triforcePiecesCollected;
    uint8_t  bombchuUpgradeLevel;

    // ---- High scores ------------------------------------------------------
    int32_t  highScores[7];

    // ---- Gold skulltulas --------------------------------------------------
    int32_t  gsFlags[6];

    // ---- Per-scene saved flags (124 scenes) ------------------------------
    TplSceneFlags sceneFlags[124];
};

// ---- Public API ----
const std::vector<Template>& All();
const Template* Find(const std::string& name);

// Capture the LOCAL player's current state as a template. Writes to disk.
// Returns true on success.
bool SnapshotLocal(const std::string& name);

// Persist all in-memory templates to disk. Called automatically by
// SnapshotLocal / Delete; safe to call manually.
bool SaveAll();

// Load all templates from disk. Called once at module init.
void LoadAll();

// Delete a template by name (in-memory + on-disk file).
bool Delete(const std::string& name);

// Apply a template to a peer. Builds + broadcasts a TEMPLATE_APPLY
// payload; the target peer receives + overwrites their save state.
bool ApplyToPeer(uint32_t targetClientId, const std::string& name);

// Apply a template directly to the local save (used by host to set
// their own loadout from a template).
bool ApplyToLocal(const std::string& name);

// ---- Network ----
nlohmann::json BuildTemplateApplyPayload(uint32_t targetClientId,
                                          const Template& t);
void HandleTemplateApply(const nlohmann::json& payload);

// ---- Serialization (used by RemoteSaveEditor for peek payloads) ----
nlohmann::json SerializeTemplate(const Template& t);
Template       DeserializeTemplate(const nlohmann::json& j);

// Snapshot the LOCAL player's gSaveContext into the passed Template,
// without touching disk or the in-memory template list. Used by
// RemoteSaveEditor::HandlePeekRequest to answer the GM.
void CaptureLocalState(Template& t);

// Add or overwrite a template by name and persist all templates to
// disk. Returns false if name is empty.
bool SaveAsTemplate(const std::string& name, Template src);

}  // namespace HarpoonTemplates

#endif  // __cplusplus
#endif  // SOH_NETWORK_HARPOON_TEMPLATES_H
