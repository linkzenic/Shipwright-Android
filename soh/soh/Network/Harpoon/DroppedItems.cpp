// =============================================================================
// HarpoonDroppedItems — distributed P2P drop ledger.
//
// Every client keeps a local copy of the ledger. When a player dies (or
// drops via pause menu), they broadcast DEATH_DROP with the full item list
// at the death position. All peers add the entry. On scene entry, every
// client iterates the ledger and spawns ground actors for unclaimed drops
// in the current scene. On pickup, the local client broadcasts CLAIM so
// peers mark the item claimed and stop spawning it on future scene loads.
//
// 5-min despawn timer pauses while no one is in the scene (we don't track
// scene-occupancy globally; we just tick when the local player is in a
// scene that has drops — close enough for v1).
//
// All packets ride on existing ROOM.BROADCAST_EVENT envelopes.
// =============================================================================

#include "DroppedItems.h"
#include "Harpoon.h"

#include <spdlog/spdlog.h>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "mods/extended_inventory.h"
#include "soh/Enhancements/item-tables/ItemTableTypes.h"
extern PlayState* gPlayState;

// All C-linkage externs. The header (OTRGlobals.h:155) declares
// ItemTable_Retrieve OUTSIDE its own `extern "C"` block but the
// DEFINITION at OTRGlobals.cpp:2506 is `extern "C"` — the definition's
// linkage wins, so the linker exports the C-mangled symbol.
// RetrieveGetItemIDFromItemID is similarly `extern "C"` at its
// definition (OTRGlobals.cpp:1443). GetItemEntry_Draw lives in C
// source (z_draw.c). func_8002EBCC / func_8002ED80 are the matrix +
// billboard setup helpers we mirror from EnItem00_DrawRandomizedItem.
GetItemID    RetrieveGetItemIDFromItemID(ItemID itemID);
GetItemEntry ItemTable_Retrieve(int16_t getItemID);
void         GetItemEntry_Draw(PlayState* play, GetItemEntry entry);
void         func_8002EBCC(Actor* actor, PlayState* play, s32 flag);
void         func_8002ED80(Actor* actor, PlayState* play, s32 flag);
}

namespace HarpoonDroppedItems {

namespace {

// The ledger lives at module scope. Every client builds it up from
// DEATH_DROP / SNAPSHOT events; there's no server-side copy.
std::vector<DropEntry> sLedger;

// Map a drop entry + item index back to the actor we spawned, so a
// CLAIM packet can kill the right ground actor.
struct SpawnedActorKey {
    uint64_t dropId;
    int32_t  itemIndex;
    Actor*   actor;
};
std::vector<SpawnedActorKey> sSpawned;

DropEntry* FindEntry(uint64_t dropId) {
    for (auto& e : sLedger) {
        if (e.dropId == dropId) return &e;
    }
    return nullptr;
}

void RemoveSpawnedFor(uint64_t dropId, int32_t itemIndex) {
    for (auto it = sSpawned.begin(); it != sSpawned.end(); ) {
        if (it->dropId == dropId &&
            (itemIndex < 0 || it->itemIndex == itemIndex)) {
            if (it->actor != nullptr) Actor_Kill(it->actor);
            it = sSpawned.erase(it);
        } else {
            ++it;
        }
    }
}

}  // anonymous namespace

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------

const std::vector<DropEntry>& GetLedger() { return sLedger; }

void ClearLedger() {
    for (auto& s : sSpawned) {
        if (s.actor != nullptr) Actor_Kill(s.actor);
    }
    sSpawned.clear();
    sLedger.clear();
}

uint64_t AddLocalDrop(uint32_t sourceCid, int16_t sceneNum,
                      float x, float y, float z,
                      std::vector<DroppedItem> items) {
    if (items.empty() || Harpoon::Instance == nullptr) return 0;
    DropEntry e;
    e.dropId         = ((uint64_t)sourceCid << 32) |
                       (Harpoon::Instance->nextLocalDropId++);
    e.sourceClientId = sourceCid;
    e.sceneNum       = sceneNum;
    e.x = x; e.y = y; e.z = z;
    e.elapsedMs = 0.0f;
    e.allClaimed = false;
    e.items = std::move(items);
    sLedger.push_back(std::move(e));
    return sLedger.back().dropId;
}

void IngestDrop(const nlohmann::json& payload) {
    uint64_t dropId = payload.value("dropId", (uint64_t)0);
    if (dropId == 0) return;
    if (FindEntry(dropId) != nullptr) return;  // dup
    DropEntry e;
    e.dropId         = dropId;
    e.sourceClientId = payload.value("sourceClientId", 0u);
    e.sceneNum       = payload.value("sceneNum", (int16_t)-1);
    e.x              = payload.value("x", 0.0f);
    e.y              = payload.value("y", 0.0f);
    e.z              = payload.value("z", 0.0f);
    e.elapsedMs      = payload.value("elapsedMs", 0.0f);
    e.allClaimed     = false;
    if (payload.contains("items") && payload["items"].is_array()) {
        for (const auto& it : payload["items"]) {
            DroppedItem di;
            di.itemId  = it.value("itemId", 0);
            di.count   = it.value("count", 1);
            di.kind    = it.value("kind", (int)KIND_INVENTORY);
            di.claimed = it.value("claimed", false);
            e.items.push_back(di);
        }
    }
    sLedger.push_back(std::move(e));

    // If the drop is in our current scene, spawn it immediately so the
    // peer doesn't have to reload the scene to see it.
    if (gPlayState != nullptr && e.sceneNum == (int16_t)gPlayState->sceneNum) {
        SpawnInScene(gPlayState);
    }
}

bool ClaimItem(uint64_t dropId, int32_t itemIndex) {
    DropEntry* e = FindEntry(dropId);
    if (e == nullptr) return false;
    if (itemIndex < 0 || itemIndex >= (int32_t)e->items.size()) return false;
    if (e->items[itemIndex].claimed) return false;
    e->items[itemIndex].claimed = true;
    // Kill the ground actor if one was spawned.
    RemoveSpawnedFor(dropId, itemIndex);
    // Mark the entire entry claimed if all items are.
    bool allClaimed = true;
    for (const auto& di : e->items) if (!di.claimed) { allClaimed = false; break; }
    e->allClaimed = allClaimed;
    return true;
}

void TickPickupPoll() {
    if (gPlayState == nullptr || Harpoon::Instance == nullptr) return;
    Player* lp = GET_PLAYER(gPlayState);
    if (lp == nullptr) return;
    constexpr f32 kPickupRadiusSq = 30.0f * 30.0f;
    Vec3f pos = lp->actor.world.pos;
    // Walk sSpawned and check XZ proximity. Snapshot first since we may
    // mutate sSpawned via ClaimItem inside OnLocalPickup.
    std::vector<SpawnedActorKey> snapshot = sSpawned;
    for (const auto& s : snapshot) {
        if (s.actor == nullptr) continue;
        f32 dx = pos.x - s.actor->world.pos.x;
        f32 dz = pos.z - s.actor->world.pos.z;
        if (dx * dx + dz * dz <= kPickupRadiusSq) {
            OnLocalPickup(s.dropId, s.itemIndex);
        }
    }
}

void TickExpiry() {
    if (gPlayState == nullptr) return;
    // Only tick if local player is in a scene that has at least one
    // unclaimed drop. Otherwise the timer stays frozen.
    s32 localScene = gPlayState->sceneNum;
    bool sceneOccupied = false;
    for (const auto& e : sLedger) {
        if (e.allClaimed) continue;
        if (e.sceneNum == localScene) { sceneOccupied = true; break; }
    }
    if (!sceneOccupied) return;

    constexpr float kFrameMs = 1000.0f / 20.0f;  // game logic 20 fps
    for (auto it = sLedger.begin(); it != sLedger.end(); ) {
        if (it->sceneNum != localScene) { ++it; continue; }
        it->elapsedMs += kFrameMs;
        if (it->elapsedMs >= kDespawnMs) {
            RemoveSpawnedFor(it->dropId, -1);
            it = sLedger.erase(it);
        } else {
            ++it;
        }
    }
}

// Custom update for our ground actors — replaces EN_ITEM00's default
// action so the engine's auto-pickup logic doesn't fire. Our own
// TickPickupPoll handles pickup detection + the broadcast/CLAIM flow.
// We do a slow Y-rotation here so the item visibly spins like a
// get-item cutscene model.
extern "C" void HarpoonGroundItem_Update(Actor* thisx, PlayState* play) {
    thisx->shape.rot.y += 0x400;  // ~4.5° per game tick
    (void)play;
}

// Custom draw for our ground actors. Renders the GetItemEntry's 3D
// model — works for ANY item registered in the item table: vanilla
// (Hookshot, Bow, Wallet, etc.), MM masks, custom mod items (Roc's
// Feather, Spinner, Deku Leaf, …). Mirrors the randomizer's
// EnItem00_DrawRandomizedItem (hook_handlers.cpp:556) but lives here
// so we don't depend on the randomizer's C++-mangled symbol.
extern "C" void HarpoonGroundItem_Draw(Actor* thisx, PlayState* play) {
    EnItem00* it = (EnItem00*)thisx;
    // Scale up — get-item models are tiny at world scale. 10.0× matches
    // the randomizer's "skip get-item animation" preset.
    Matrix_Scale(10.0f, 10.0f, 10.0f, MTXMODE_APPLY);
    func_8002EBCC(thisx, play, 0);
    func_8002ED80(thisx, play, 0);
    GetItemEntry_Draw(play, it->itemEntry);
}

// Pick the closest matching ITEM00_* params for ammo/equipment/quest
// drops — EN_ITEM00 has native visuals for those (arrows pack, bomb
// pack, shield, etc.). KIND_RUPEES and KIND_INVENTORY are NOT handled
// here — they use the GetItemEntry override path so the proper 3D
// model renders (wallet, hookshot, custom items, MM masks, etc.).
static s16 PickEnItem00Params(const DroppedItem& di) {
    switch (di.kind) {
        case KIND_AMMO:
            switch (di.itemId) {
                case SLOT_STICK:     return 0x0D;  // ITEM00_STICK
                case SLOT_NUT:       return 0x0C;  // ITEM00_NUTS
                case SLOT_BOMB:      return 0x04;  // ITEM00_BOMBS_A
                case SLOT_BOW:       return 0x0A;  // ITEM00_ARROWS_LARGE
                case SLOT_SLINGSHOT: return 0x10;  // ITEM00_SEEDS
                case SLOT_BOMBCHU:   return 0x1A;  // ITEM00_BOMBCHU
                default:             return 0x12;  // ITEM00_FLEXIBLE
            }
        case KIND_EQUIPMENT: return 0x16;  // ITEM00_SHIELD_HYLIAN (placeholder)
        case KIND_QUEST_ITEM:
        case KIND_DUNGEON_ITEM: return 0x06;  // ITEM00_HEART_PIECE (placeholder)
        // KIND_RUPEES + KIND_INVENTORY are handled separately via the
        // GetItemEntry override path (proper wallet / hookshot / etc.
        // 3D models). They should never reach this helper.
        default:             return 0x12;  // ITEM00_FLEXIBLE fallback
    }
}

void SpawnInScene(PlayState* play) {
    if (play == nullptr) return;
    s32 sceneNum = play->sceneNum;
    for (auto& e : sLedger) {
        if (e.allClaimed) continue;
        if (e.sceneNum != sceneNum) continue;
        for (int32_t i = 0; i < (int32_t)e.items.size(); i++) {
            DroppedItem& di = e.items[i];
            if (di.claimed) continue;
            // Skip if already spawned this scene-load.
            bool alreadySpawned = false;
            for (const auto& s : sSpawned) {
                if (s.dropId == e.dropId && s.itemIndex == i) {
                    alreadySpawned = true; break;
                }
            }
            if (alreadySpawned) continue;
            // Short-distance random spread — OoT items z-fight when
            // stacked at the same XZ. Each drop gets an 8-20u offset in
            // a random direction so the pile is visibly distinct.
            f32 angle = (f32)(rand() % 0x10000) * (3.14159265f / 32768.0f);
            f32 rad   = 8.0f + (f32)(rand() % 13);
            f32 ox = cosf(angle) * rad;
            f32 oz = sinf(angle) * rad;
            Vec3f spawnPos = { e.x + ox, e.y + 10.0f, e.z + oz };

            // For KIND_RUPEES and KIND_INVENTORY we use the randomizer's
            // pipeline so the ACTUAL 3D model renders on the ground
            // (wallet for rupees, hookshot model for hookshot, custom
            // item models for Roc's Feather / Spinner / etc., MM mask
            // models, …). Other kinds (ammo packs, equipment, quest
            // bits) don't have clean ITEM_*→GI_* mappings so they use
            // the EN_ITEM00 native visuals.
            //
            // The override flow:
            //   1) Item_DropCollectible2 with ITEM00_SOH_DUMMY spawns an
            //      EN_ITEM00 with no built-in visual or item.
            //   2) Set item00->itemEntry to the proper GetItemEntry —
            //      drives EnItem00_DrawRandomizedItem's rendering.
            //   3) Override actor->draw to the randomizer draw helper.
            //   4) Override actor->update to our stub so the engine's
            //      auto-pickup doesn't fire. Our TickPickupPoll handles
            //      pickup detection + CLAIM broadcast.
            Actor* a = nullptr;
            bool useGiEntry = false;
            int16_t giId = -1;
            if (di.kind == KIND_RUPEES) {
                // Visual = wallet model (NOT a wallet upgrade — pickup
                // gives N rupees per ApplyItemToLocalSave).
                giId = (int16_t)GI_WALLET_ADULT;
                useGiEntry = true;
            } else if (di.kind == KIND_INVENTORY) {
                // Visual = the actual item's 3D model. Look up via the
                // ITEM_* → GI_* map; if missing (custom mod items, etc.)
                // fall back to ITEM00_FLEXIBLE.
                GetItemID g = RetrieveGetItemIDFromItemID((ItemID)di.itemId);
                if (g != GI_MAX && g != GI_NONE) {
                    giId = (int16_t)g;
                    useGiEntry = true;
                }
            }

            if (useGiEntry) {
                EnItem00* item00 = Item_DropCollectible2(play, &spawnPos,
                                                         ITEM00_SOH_DUMMY);
                if (item00 != nullptr) {
                    item00->itemEntry      = ItemTable_Retrieve(giId);
                    item00->actor.draw     = HarpoonGroundItem_Draw;
                    item00->actor.update   = HarpoonGroundItem_Update;
                    item00->actor.velocity = { 0.0f, 0.0f, 0.0f };
                    a = &item00->actor;
                }
            } else {
                s16 params = PickEnItem00Params(di);
                a = Actor_Spawn(&play->actorCtx, play,
                                ACTOR_EN_ITEM00,
                                spawnPos.x, spawnPos.y, spawnPos.z,
                                0, 0, 0, params);
            }
            if (a != nullptr) {
                SpawnedActorKey k;
                k.dropId    = e.dropId;
                k.itemIndex = i;
                k.actor     = a;
                sSpawned.push_back(k);
            }
        }
    }
}

// ----------------------------------------------------------------------------
// Payload builders
// ----------------------------------------------------------------------------

static nlohmann::json _Envelope(const char* evt, nlohmann::json data) {
    nlohmann::json p;
    p["type"]       = "ROOM.BROADCAST_EVENT";
    p["event_name"] = evt;
    p["data"]       = std::move(data);
    return p;
}

nlohmann::json BuildDeathDropPayload(uint64_t dropId, uint32_t sourceCid,
                                      int16_t sceneNum, float x, float y, float z,
                                      const std::vector<DroppedItem>& items) {
    nlohmann::json d;
    d["dropId"]         = dropId;
    d["sourceClientId"] = sourceCid;
    d["sceneNum"]       = sceneNum;
    d["x"] = x; d["y"] = y; d["z"] = z;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& di : items) {
        nlohmann::json o;
        o["itemId"]  = di.itemId;
        o["count"]   = di.count;
        o["kind"]    = di.kind;
        arr.push_back(o);
    }
    d["items"] = arr;
    return _Envelope("HARPOON.DEATH_DROP", std::move(d));
}

nlohmann::json BuildDropClaimPayload(uint64_t dropId, int32_t itemIndex,
                                      uint32_t claimerCid) {
    nlohmann::json d;
    d["dropId"]          = dropId;
    d["itemIndex"]       = itemIndex;
    d["claimerClientId"] = claimerCid;
    return _Envelope("HARPOON.DROP_CLAIM", std::move(d));
}

nlohmann::json BuildLedgerRequestPayload() {
    return _Envelope("HARPOON.DROP_LEDGER_REQ", nlohmann::json::object());
}

nlohmann::json BuildLedgerSnapshotPayload() {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : sLedger) {
        if (e.allClaimed) continue;
        nlohmann::json o;
        o["dropId"]         = e.dropId;
        o["sourceClientId"] = e.sourceClientId;
        o["sceneNum"]       = e.sceneNum;
        o["x"] = e.x; o["y"] = e.y; o["z"] = e.z;
        o["elapsedMs"]      = e.elapsedMs;
        nlohmann::json items = nlohmann::json::array();
        for (const auto& di : e.items) {
            nlohmann::json io;
            io["itemId"]  = di.itemId;
            io["count"]   = di.count;
            io["kind"]    = di.kind;
            io["claimed"] = di.claimed;
            items.push_back(io);
        }
        o["items"] = items;
        arr.push_back(o);
    }
    nlohmann::json d;
    d["entries"] = arr;
    return _Envelope("HARPOON.DROP_LEDGER_SNAPSHOT", std::move(d));
}

// ----------------------------------------------------------------------------
// Network handlers
// ----------------------------------------------------------------------------

void HandleDeathDrop(const nlohmann::json& payload) {
    IngestDrop(payload);
}

void HandleDropClaim(const nlohmann::json& payload) {
    uint64_t dropId    = payload.value("dropId", (uint64_t)0);
    int32_t  itemIndex = payload.value("itemIndex", -1);
    ClaimItem(dropId, itemIndex);
}

void HandleLedgerRequest(const nlohmann::json& /*envelope*/) {
    // Only host responds (avoid every peer flooding the requester).
    if (Harpoon::Instance == nullptr) return;
    bool isHost = (Harpoon::Instance->ownClientId != 0 &&
                   Harpoon::Instance->ownClientId == Harpoon::Instance->hostClientId);
    if (!isHost) return;
    Harpoon::Instance->SendJsonToRemote(BuildLedgerSnapshotPayload());
}

void HandleLedgerSnapshot(const nlohmann::json& payload) {
    if (!payload.contains("entries") || !payload["entries"].is_array()) return;
    for (const auto& entry : payload["entries"]) {
        IngestDrop(entry);
    }
    SPDLOG_INFO("[Harpoon][Drops] ingested {} ledger entries from snapshot",
                (int)payload["entries"].size());
}

// ----------------------------------------------------------------------------
// Local pickup — invoked by the ground actor's collision hook.
// ----------------------------------------------------------------------------

namespace {
// Apply a dropped item to the local player's save state. Returns false
// if the player already owns a non-stackable item (silent absorb path).
bool ApplyItemToLocalSave(const DroppedItem& di) {
    switch (di.kind) {
        case KIND_INVENTORY: {
            // Vanilla items via engine's Item_Give (handles slot lookup,
            // ammo init, GI table). Custom items (>= 0x9C) fall through
            // to ExtInv_SetItemById. Note: Item_Give returns a u8 result
            // (typically the item it actually gave; ITEM_NONE means
            // "already have it"). If already-owned, we silent-absorb
            // (the ground actor still gets claimed/killed).
            if (gPlayState != nullptr) {
                if (di.itemId >= 0x9C) {
                    ExtInv_SetItemById((u8)di.itemId);
                } else {
                    Item_Give(gPlayState, (u8)di.itemId);
                }
            }
            return true;
        }
        case KIND_RUPEES: {
            gSaveContext.rupees += di.count;
            // Cap at wallet max — engine will clamp on next frame.
            return true;
        }
        case KIND_AMMO: {
            if (di.itemId >= 0 &&
                di.itemId < (int32_t)ARRAY_COUNT(gSaveContext.inventory.ammo)) {
                gSaveContext.inventory.ammo[di.itemId] += di.count;
            }
            return true;
        }
        case KIND_EQUIPMENT: {
            gSaveContext.inventory.equipment |= (u32)di.itemId;
            return true;
        }
        case KIND_QUEST_ITEM: {
            gSaveContext.inventory.questItems |= (u32)di.itemId;
            return true;
        }
        case KIND_DUNGEON_ITEM: {
            if (di.itemId >= 0 &&
                di.itemId < (int32_t)ARRAY_COUNT(gSaveContext.inventory.dungeonItems)) {
                gSaveContext.inventory.dungeonItems[di.itemId] = 1;
            }
            return true;
        }
        default: return false;
    }
}
}  // anon

void OnLocalPickup(uint64_t dropId, int32_t itemIndex) {
    DropEntry* e = FindEntry(dropId);
    if (e == nullptr) return;
    if (itemIndex < 0 || itemIndex >= (int32_t)e->items.size()) return;
    if (e->items[itemIndex].claimed) return;

    DroppedItem& di = e->items[itemIndex];
    ApplyItemToLocalSave(di);

    // Mark claimed locally and broadcast so peers stop spawning it.
    ClaimItem(dropId, itemIndex);
    if (Harpoon::Instance != nullptr) {
        Harpoon::Instance->SendJsonToRemote(
            BuildDropClaimPayload(dropId, itemIndex,
                                   Harpoon::Instance->ownClientId));
    }
}

// ----------------------------------------------------------------------------
// Death-drop helpers
// ----------------------------------------------------------------------------

namespace {

bool HasFairyInBottle() {
    // Bottle slots: gSaveContext.inventory.items[SLOT_BOTTLE_1..4]. In
    // OoT, the values at those slots are the bottle contents (ITEM_FAIRY,
    // ITEM_POTION_RED, etc.). Walk the four bottle slots.
    constexpr s32 BOTTLE_SLOTS[4] = { SLOT_BOTTLE_1, SLOT_BOTTLE_2,
                                      SLOT_BOTTLE_3, SLOT_BOTTLE_4 };
    for (s32 s : BOTTLE_SLOTS) {
        if (s < 0 || s >= (s32)ARRAY_COUNT(gSaveContext.inventory.items)) continue;
        if (gSaveContext.inventory.items[s] == ITEM_FAIRY) return true;
    }
    return false;
}

}  // anon

std::vector<DroppedItem> BuildSoftDeathDrop() {
    std::vector<DroppedItem> out;

    // Collect all non-empty inventory item slots.
    std::vector<s32> ownedSlots;
    for (s32 i = 0; i < (s32)ARRAY_COUNT(gSaveContext.inventory.items); i++) {
        if (gSaveContext.inventory.items[i] != ITEM_NONE) ownedSlots.push_back(i);
    }
    // Shuffle (Fisher-Yates with rand) and take 2.
    for (s32 i = (s32)ownedSlots.size() - 1; i > 0; i--) {
        s32 j = rand() % (i + 1);
        std::swap(ownedSlots[i], ownedSlots[j]);
    }
    s32 picks = (s32)ownedSlots.size();
    if (picks > 2) picks = 2;
    for (s32 i = 0; i < picks; i++) {
        DroppedItem di;
        di.itemId = gSaveContext.inventory.items[ownedSlots[i]];
        di.count  = 1;
        di.kind   = KIND_INVENTORY;
        out.push_back(di);
    }

    // 20% of carried rupees.
    s32 rupeeDrop = gSaveContext.rupees / 5;
    if (rupeeDrop > 0) {
        DroppedItem di;
        di.itemId = ITEM_RUPEE_GREEN;
        di.count  = rupeeDrop;
        di.kind   = KIND_RUPEES;
        out.push_back(di);
    }
    return out;
}

std::vector<DroppedItem> BuildGameOverDrop() {
    std::vector<DroppedItem> out;

    // All inventory items.
    for (s32 i = 0; i < (s32)ARRAY_COUNT(gSaveContext.inventory.items); i++) {
        u8 it = gSaveContext.inventory.items[i];
        if (it == ITEM_NONE) continue;
        DroppedItem di;
        di.itemId = it;
        di.count  = 1;
        di.kind   = KIND_INVENTORY;
        out.push_back(di);
    }

    // All ammo (per-slot count). We encode the slot index in itemId
    // because there's no shared item-id for "ammo of type X".
    for (s32 i = 0; i < (s32)ARRAY_COUNT(gSaveContext.inventory.ammo); i++) {
        s32 cnt = gSaveContext.inventory.ammo[i];
        if (cnt <= 0) continue;
        DroppedItem di;
        di.itemId = i;
        di.count  = cnt;
        di.kind   = KIND_AMMO;
        out.push_back(di);
    }

    // Rupees.
    if (gSaveContext.rupees > 0) {
        DroppedItem di;
        di.itemId = ITEM_RUPEE_GREEN;
        di.count  = gSaveContext.rupees;
        di.kind   = KIND_RUPEES;
        out.push_back(di);
    }

    // Equipment bitmask (sword/shield/tunic/boots). Drop as a single
    // big drop; on pickup we OR the bits back in.
    if (gSaveContext.inventory.equipment != 0) {
        DroppedItem di;
        di.itemId = (s32)gSaveContext.inventory.equipment;
        di.count  = 1;
        di.kind   = KIND_EQUIPMENT;
        out.push_back(di);
    }

    // Quest-items bitmask (songs, stones, medallions). Heart pieces
    // / heart containers are tracked separately and are NOT dropped.
    if (gSaveContext.inventory.questItems != 0) {
        DroppedItem di;
        di.itemId = (s32)gSaveContext.inventory.questItems;
        di.count  = 1;
        di.kind   = KIND_QUEST_ITEM;
        out.push_back(di);
    }

    // Dungeon items per-dungeon (boss key, compass, map, etc.).
    for (s32 i = 0; i < (s32)ARRAY_COUNT(gSaveContext.inventory.dungeonItems); i++) {
        if (gSaveContext.inventory.dungeonItems[i] == 0) continue;
        DroppedItem di;
        di.itemId = i;
        di.count  = gSaveContext.inventory.dungeonItems[i];
        di.kind   = KIND_DUNGEON_ITEM;
        out.push_back(di);
    }

    return out;
}

void StripDroppedFromSave(const std::vector<DroppedItem>& items, bool isGameOver) {
    for (const auto& di : items) {
        switch (di.kind) {
            case KIND_INVENTORY: {
                for (s32 i = 0; i < (s32)ARRAY_COUNT(gSaveContext.inventory.items); i++) {
                    if (gSaveContext.inventory.items[i] == di.itemId) {
                        gSaveContext.inventory.items[i] = ITEM_NONE;
                        break;
                    }
                }
                break;
            }
            case KIND_AMMO: {
                if (di.itemId >= 0 &&
                    di.itemId < (s32)ARRAY_COUNT(gSaveContext.inventory.ammo)) {
                    gSaveContext.inventory.ammo[di.itemId] = 0;
                }
                break;
            }
            case KIND_RUPEES: {
                gSaveContext.rupees -= di.count;
                if (gSaveContext.rupees < 0) gSaveContext.rupees = 0;
                break;
            }
            case KIND_EQUIPMENT: {
                if (isGameOver) gSaveContext.inventory.equipment = 0;
                break;
            }
            case KIND_QUEST_ITEM: {
                if (isGameOver) gSaveContext.inventory.questItems = 0;
                break;
            }
            case KIND_DUNGEON_ITEM: {
                if (di.itemId >= 0 &&
                    di.itemId < (s32)ARRAY_COUNT(gSaveContext.inventory.dungeonItems)) {
                    gSaveContext.inventory.dungeonItems[di.itemId] = 0;
                }
                break;
            }
            default: break;
        }
    }
}

bool TriggerLocalDeathDrop() {
    if (Harpoon::Instance == nullptr || gPlayState == nullptr) return false;
    Player* lp = GET_PLAYER(gPlayState);
    if (lp == nullptr) return false;

    bool isGameOver = !HasFairyInBottle();
    std::vector<DroppedItem> drop = isGameOver ? BuildGameOverDrop()
                                               : BuildSoftDeathDrop();
    if (drop.empty()) return false;

    Vec3f pos = lp->actor.world.pos;
    uint64_t dropId = AddLocalDrop(Harpoon::Instance->ownClientId,
                                    (int16_t)gPlayState->sceneNum,
                                    pos.x, pos.y, pos.z, drop);
    if (dropId == 0) return false;

    // Broadcast.
    Harpoon::Instance->SendJsonToRemote(
        BuildDeathDropPayload(dropId, Harpoon::Instance->ownClientId,
                              (int16_t)gPlayState->sceneNum,
                              pos.x, pos.y, pos.z, drop));

    // Strip from local save.
    StripDroppedFromSave(drop, isGameOver);

    SPDLOG_INFO("[Harpoon][Drops] death-drop fired: cid={} scene={} items={} game_over={}",
                Harpoon::Instance->ownClientId, gPlayState->sceneNum,
                (int)drop.size(), isGameOver);
    return true;
}

}  // namespace HarpoonDroppedItems

// ----------------------------------------------------------------------------
// C bridges (called from z_kaleido_item.c when the player presses C-Up
// while hovering an inventory slot, etc.)
// ----------------------------------------------------------------------------

extern "C" void HarpoonDrops_RequestDropFromPause(int tabId, int slot) {
    using namespace HarpoonDroppedItems;
    if (Harpoon::Instance == nullptr || gPlayState == nullptr) return;
    if (!Harpoon::Instance->isConnected) return;
    // RPG-mode only — other gamemodes use vanilla inventory.
    if (Harpoon::Instance->currentRoomGameMode != "rpg") return;

    Player* lp = GET_PLAYER(gPlayState);
    if (lp == nullptr) return;
    Vec3f pos = lp->actor.world.pos;

    std::vector<DroppedItem> drop;
    if (tabId == 0) {
        // Items tab.
        if (slot < 0 || slot >= (int)ARRAY_COUNT(gSaveContext.inventory.items)) return;
        u8 it = gSaveContext.inventory.items[slot];
        if (it == ITEM_NONE) return;
        DroppedItem di;
        di.itemId = it;
        di.count  = 1;
        di.kind   = KIND_INVENTORY;
        drop.push_back(di);
        gSaveContext.inventory.items[slot] = ITEM_NONE;
    } else if (tabId == 1) {
        // Equipment tab — drop the entire equipment bitmask. Slot acts
        // as a "which equipment-type bit-block" hint (0=sword, 1=shield,
        // 2=tunic, 3=boots); for v1 we drop everything as one entry.
        if (gSaveContext.inventory.equipment == 0) return;
        DroppedItem di;
        di.itemId = (int)gSaveContext.inventory.equipment;
        di.count  = 1;
        di.kind   = KIND_EQUIPMENT;
        drop.push_back(di);
        gSaveContext.inventory.equipment = 0;
        (void)slot;
    } else if (tabId == 2) {
        // Quest items tab — same approach: drop the whole bitmask.
        if (gSaveContext.inventory.questItems == 0) return;
        DroppedItem di;
        di.itemId = (int)gSaveContext.inventory.questItems;
        di.count  = 1;
        di.kind   = KIND_QUEST_ITEM;
        drop.push_back(di);
        gSaveContext.inventory.questItems = 0;
        (void)slot;
    } else {
        return;
    }

    uint64_t dropId = AddLocalDrop(Harpoon::Instance->ownClientId,
                                    (int16_t)gPlayState->sceneNum,
                                    pos.x, pos.y, pos.z, drop);
    if (dropId == 0) return;
    Harpoon::Instance->SendJsonToRemote(
        BuildDeathDropPayload(dropId, Harpoon::Instance->ownClientId,
                              (int16_t)gPlayState->sceneNum,
                              pos.x, pos.y, pos.z, drop));
}

extern "C" void HarpoonDrops_RequestDropRupees(int amount) {
    using namespace HarpoonDroppedItems;
    if (amount <= 0) return;
    if (Harpoon::Instance == nullptr || gPlayState == nullptr) return;
    if (!Harpoon::Instance->isConnected) return;
    if (Harpoon::Instance->currentRoomGameMode != "rpg") return;
    if (amount > gSaveContext.rupees) amount = gSaveContext.rupees;
    if (amount <= 0) return;

    Player* lp = GET_PLAYER(gPlayState);
    if (lp == nullptr) return;
    Vec3f pos = lp->actor.world.pos;

    std::vector<DroppedItem> drop;
    DroppedItem di;
    di.itemId = ITEM_RUPEE_GREEN;
    di.count  = amount;
    di.kind   = KIND_RUPEES;
    drop.push_back(di);
    gSaveContext.rupees -= amount;

    uint64_t dropId = AddLocalDrop(Harpoon::Instance->ownClientId,
                                    (int16_t)gPlayState->sceneNum,
                                    pos.x, pos.y, pos.z, drop);
    if (dropId == 0) return;
    Harpoon::Instance->SendJsonToRemote(
        BuildDeathDropPayload(dropId, Harpoon::Instance->ownClientId,
                              (int16_t)gPlayState->sceneNum,
                              pos.x, pos.y, pos.z, drop));
}
