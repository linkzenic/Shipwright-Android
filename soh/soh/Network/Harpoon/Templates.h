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
struct Template {
    std::string name;

    // Inventory — full extended-inventory coverage:
    //   [ 0..23] vanilla OoT items (hookshot, bow, ocarina, etc.)
    //   [24..47] Shipwright custom items (Roc's Feather, Spinner, Deku
    //            Leaf, Cane of Somaria, Shovel, etc.)
    //   [48..71] MM masks (Deku, Goron, Zora, Fierce Deity, etc.)
    // Loop bounds in Templates.cpp use ARRAY_COUNT so future inventory
    // expansion just bumps this array size.
    uint8_t items[72];
    s8      ammo[16];

    // Equipment.
    uint32_t equipment;
    uint32_t upgrades;

    // Resources.
    int16_t  rupees;
    int16_t  magic;
    int16_t  magicCapacity;
    int16_t  healthCapacity;

    // Quest items (songs, stones, medallions).
    uint32_t questItems;

    // Dungeon items per-dungeon.
    uint8_t  dungeonItems[20];

    // GM movement restrictions (applied alongside the inventory).
    bool     restrictNoClimb;
    bool     restrictNoGrab;
    bool     restrictNoCrawl;
    bool     restrictNoTalk;
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

}  // namespace HarpoonTemplates

#endif  // __cplusplus
#endif  // SOH_NETWORK_HARPOON_TEMPLATES_H
