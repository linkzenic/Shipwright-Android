#ifndef SOH_NETWORK_HARPOON_DROPPED_ITEMS_H
#define SOH_NETWORK_HARPOON_DROPPED_ITEMS_H

// C bridges for C-language callers (z_kaleido_item.c, etc.).
#ifdef __cplusplus
extern "C" {
#endif
    // Drop the item in inventory slot `slot` of the items tab. Removes
    // from the local save, broadcasts DEATH_DROP at the player's position
    // (a 1-item drop), peers see the ground actor on next scene-tick.
    // tabId: 0 = items, 1 = equipment, 2 = quest items.
    void HarpoonDrops_RequestDropFromPause(int tabId, int slot);

    // Drop N rupees. Removes from local rupees, broadcasts the drop.
    void HarpoonDrops_RequestDropRupees(int amount);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include <cstdint>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>

extern "C" {
#include "z64.h"
}

namespace HarpoonDroppedItems {

// What kind of "thing" a drop entry represents. Drives how the recipient
// applies it to their save state on pickup.
enum DropKind : int32_t {
    KIND_INVENTORY    = 0,  // gSaveContext.inventory.items[slot] = itemId
    KIND_EQUIPMENT    = 1,  // gSaveContext.inventory.equipment bitmask
    KIND_RUPEES       = 2,  // count → gSaveContext.rupees += count
    KIND_QUEST_ITEM   = 3,  // gSaveContext.inventory.questItems bit
    KIND_AMMO         = 4,  // gSaveContext.inventory.ammo[slot] = count
    KIND_DUNGEON_ITEM = 5,  // gSaveContext.inventory.dungeonItems[slot]
};

// One physical thing on the ground inside a drop pile.
struct DroppedItem {
    int32_t itemId   = 0;  // ITEM_* for KIND_INVENTORY; slot for AMMO/etc.
    int32_t count    = 1;  // rupees / ammo count
    int32_t kind     = KIND_INVENTORY;
    bool    claimed  = false;
};

// A pile of drops from a single death (or pause-menu single-item drop).
// Lives in every client's local ledger. Identified by dropId so peers
// can correlate claims/expiries across the network.
struct DropEntry {
    uint64_t                 dropId         = 0;
    uint32_t                 sourceClientId = 0;
    int16_t                  sceneNum       = -1;
    float                    x = 0.0f, y = 0.0f, z = 0.0f;
    float                    elapsedMs      = 0.0f;  // ticks only when scene occupied
    int64_t                  createdAtMs    = 0;     // wall-clock ms at creation, for hard expiry
    bool                     allClaimed     = false;
    std::vector<DroppedItem> items;
};

// Constants
constexpr float kDespawnMs = 5.0f * 60.0f * 1000.0f;  // 5 minutes
// Hard wall-clock ceiling on ledger entries: even if no peer ever enters
// the entry's scene to tick elapsedMs, drop it after 30 min. Prevents
// unbounded ledger growth across 24h+ sessions with frequent deaths.
constexpr int64_t kLedgerMaxAgeMs = 30LL * 60LL * 1000LL;  // 30 minutes

// ---- Ledger management ----
const std::vector<DropEntry>& GetLedger();
void ClearLedger();

// Build + add a new local drop. Returns the newly assigned dropId.
// Does NOT broadcast — caller is responsible for SendJsonToRemote.
uint64_t AddLocalDrop(uint32_t sourceCid, int16_t sceneNum,
                      float x, float y, float z,
                      std::vector<DroppedItem> items);

// Ingest a peer's DEATH_DROP / DROP_BROADCAST. Adds to local ledger if
// the dropId isn't already present.
void IngestDrop(const nlohmann::json& payload);

// Mark a single item in a drop as claimed (by dropId + item index).
// Returns true if the ledger had that entry+item.
bool ClaimItem(uint64_t dropId, int32_t itemIndex);

// Per-frame expiry tick — only runs if the local player is currently
// in a scene that has ≥ 1 unclaimed drop. Removes entries whose
// elapsedMs >= kDespawnMs.
void TickExpiry();

// Per-frame proximity poll. Checks if the local player is within pickup
// radius of any spawned ground actor; if so, fires OnLocalPickup. Cheap
// AABB-style XZ distance check; called once per game tick.
void TickPickupPoll();

// Called from OnSceneSpawnActors. Walks the ledger for unclaimed drops
// in the just-loaded scene and spawns one ground actor per item.
void SpawnInScene(PlayState* play);

// ---- Network payload builders ----
nlohmann::json BuildDeathDropPayload(uint64_t dropId, uint32_t sourceCid,
                                      int16_t sceneNum, float x, float y, float z,
                                      const std::vector<DroppedItem>& items);
nlohmann::json BuildDropClaimPayload(uint64_t dropId, int32_t itemIndex,
                                      uint32_t claimerCid);
nlohmann::json BuildLedgerRequestPayload();
nlohmann::json BuildLedgerSnapshotPayload();

// ---- Network packet handlers (called from HandleEvent dispatch) ----
void HandleDeathDrop(const nlohmann::json& payload);
void HandleDropClaim(const nlohmann::json& payload);
void HandleLedgerRequest(const nlohmann::json& envelope);
void HandleLedgerSnapshot(const nlohmann::json& payload);

// ---- Pickup callback — invoked by the ground-item actor when the
//      local player overlaps it. Applies the item to the local save
//      (or no-ops + still claims if duplicate), then broadcasts CLAIM.
void OnLocalPickup(uint64_t dropId, int32_t itemIndex);

// ---- Death-drop helpers ----
// Build a SOFT-DEATH drop list (2 random inventory items + 20% rupees).
// Reads gSaveContext; does NOT modify it.
std::vector<DroppedItem> BuildSoftDeathDrop();

// Build a GAME-OVER drop list (everything except heart containers /
// pieces / progression-permanent upgrades).
std::vector<DroppedItem> BuildGameOverDrop();

// After a drop list has been broadcast, strip the dropped items from
// the local save state so the dying player loses them. For game-over
// this also clears equipment + rupees + quest items.
void StripDroppedFromSave(const std::vector<DroppedItem>& items, bool isGameOver);

// One-shot helper: detect a death (call from OnPlayerHealthChange when
// HP transitions to 0). Picks soft/game-over branch by checking fairy
// bottles. Broadcasts + locally applies + strips save. Returns true if
// a drop actually fired.
bool TriggerLocalDeathDrop();

}  // namespace HarpoonDroppedItems

#endif  // __cplusplus
#endif  // SOH_NETWORK_HARPOON_DROPPED_ITEMS_H
