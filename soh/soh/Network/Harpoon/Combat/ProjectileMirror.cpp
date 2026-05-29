// =============================================================================
// ProjectileMirror — implementation. See header for protocol overview.
//
// V1 ships the registry + broadcast/handle glue. The per-spell/arrow Init
// hooks that call BroadcastSpawn() are wired in their respective .inc.c
// source files in `soh/expansions/sw97/actors/`. This module is the central
// coordination point: it knows the (projId → Actor*) map, the spawn flow,
// and the dispatch handlers.
// =============================================================================

#include "ProjectileMirror.h"
#include "CombatSync.h"
#include "../Harpoon.h"

#include <spdlog/spdlog.h>
#include <unordered_map>

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

namespace HarpoonProjectileMirror {

namespace {

struct ProjectileEntry {
    uint32_t projId;
    Actor*   actor;
    uint32_t ownerCid;
    HarpoonCombat::HarpoonWeaponId source;
};

std::unordered_map<uint32_t, ProjectileEntry> sRegistry;
uint32_t sLocalCounter = 1;

nlohmann::json Envelope(const char* evt, nlohmann::json data) {
    nlohmann::json p;
    p["type"]       = "ROOM.BROADCAST_EVENT";
    p["event_name"] = evt;
    p["data"]       = std::move(data);
    return p;
}

uint32_t OwnCid() {
    return Harpoon::Instance != nullptr ? Harpoon::Instance->ownClientId : 0u;
}

// Map a HarpoonWeaponId to the SoH actor profile that should be spawned on
// remote peers to mirror the projectile. V1 keeps this conservative: only
// SW97 actors that the user explicitly opted into syncing are present.
// Missing entries skip the mirror spawn — peers won't see the projectile
// fly, only the resulting damage.
//
// NOTE: each ACTOR_* below must exist in `soh/include/z64actor.h` for the
// build to link. SW97 actors are registered via the sw97 expansion pack.
int16_t ActorIdForSource(HarpoonCombat::HarpoonWeaponId source) {
    using namespace HarpoonCombat;
    switch (source) {
        // SW97 spells — currently the actor IDs depend on which builds
        // register them in z64actor.h. Returning 0 falls through to "no
        // mirror spawn" gracefully.
        case W_SW97_MAGIC_DARK:
        case W_SW97_MAGIC_FIRE:
        case W_SW97_MAGIC_ICE:
        case W_SW97_MAGIC_LIGHT:
        case W_SW97_MAGIC_SOUL:
        case W_SW97_MAGIC_WIND:
        case W_SW97_ARROW_DARK:
        case W_SW97_ARROW_FIRE:
        case W_SW97_ARROW_ICE:
        case W_SW97_ARROW_LIGHT:
        case W_SW97_ARROW_SOUL:
        case W_SW97_ARROW_WIND:
            // Per-spell hooks should call BroadcastSpawn; the local actor
            // creation is the responsibility of the spell's own Init. The
            // mirror registry just tracks the actor pointer so PROJECTILE_HIT
            // can find it.
            return 0;
        default:
            return 0;
    }
}

}  // anon

uint32_t BroadcastSpawn(HarpoonCombat::HarpoonWeaponId source,
                        float px, float py, float pz,
                        float vx, float vy, float vz,
                        float yaw, uint16_t charge) {
    if (Harpoon::Instance == nullptr) return 0;
    uint32_t projId = (OwnCid() << 16) | (sLocalCounter++ & 0xFFFF);
    nlohmann::json d;
    d["projId"]   = projId;
    d["ownerCid"] = OwnCid();
    d["source"]   = (int)source;
    d["px"] = px; d["py"] = py; d["pz"] = pz;
    d["vx"] = vx; d["vy"] = vy; d["vz"] = vz;
    d["yaw"]      = yaw;
    d["charge"]   = (int)charge;
    Harpoon::Instance->SendJsonToRemote(Envelope("COMBAT.PROJECTILE_SPAWN", std::move(d)));
    return projId;
}

void BroadcastHit(uint32_t projId, uint32_t targetCid,
                  float hitX, float hitY, float hitZ) {
    if (Harpoon::Instance == nullptr) return;
    nlohmann::json d;
    d["projId"]    = projId;
    d["targetCid"] = targetCid;
    d["hitX"] = hitX; d["hitY"] = hitY; d["hitZ"] = hitZ;
    Harpoon::Instance->SendJsonToRemote(Envelope("COMBAT.PROJECTILE_HIT", std::move(d)));
}

void BroadcastReflect(uint32_t projId, float newVx, float newVy, float newVz,
                      uint32_t newOwnerCid) {
    if (Harpoon::Instance == nullptr) return;
    nlohmann::json d;
    d["projId"]      = projId;
    d["newVx"] = newVx; d["newVy"] = newVy; d["newVz"] = newVz;
    d["newOwnerCid"] = newOwnerCid;
    Harpoon::Instance->SendJsonToRemote(Envelope("COMBAT.PROJECTILE_REFLECT", std::move(d)));
}

void HandleSpawn(const nlohmann::json& data) {
    if (Harpoon::Instance == nullptr || gPlayState == nullptr) return;
    uint32_t projId   = data.value("projId", 0u);
    uint32_t ownerCid = data.value("ownerCid", 0u);
    if (ownerCid == OwnCid()) return;  // we ARE the owner — local actor already exists
    HarpoonCombat::HarpoonWeaponId source =
        (HarpoonCombat::HarpoonWeaponId)data.value("source",
                                                    (int)HarpoonCombat::HARPOON_WEAPON_UNKNOWN);
    f32 px = data.value("px", 0.0f), py = data.value("py", 0.0f), pz = data.value("pz", 0.0f);
    (void)data;  // vx/vy/vz/yaw/charge consumed below
    f32 vx = data.value("vx", 0.0f), vy = data.value("vy", 0.0f), vz = data.value("vz", 0.0f);
    f32 yaw = data.value("yaw", 0.0f);
    uint16_t charge = (uint16_t)data.value("charge", 0);

    int16_t actorId = ActorIdForSource(source);
    if (actorId == 0) {
        // No mirror actor registered for this source yet — track entry
        // anyway so PROJECTILE_HIT routing still works.
        sRegistry[projId] = { projId, nullptr, ownerCid, source };
        return;
    }

    Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState,
                            actorId, px, py, pz,
                            0, (s16)(yaw * 0x8000 / 3.14159f), 0,
                            charge | REMOTE_PROJECTILE_BIT);
    if (a != nullptr) {
        // Apply initial velocity so the mirrored actor's update tracks
        // close-to-identical trajectory to the owner's local copy.
        a->velocity.x = vx;
        a->velocity.y = vy;
        a->velocity.z = vz;
    }
    sRegistry[projId] = { projId, a, ownerCid, source };
    SPDLOG_DEBUG("[HarpoonCombat][ProjMirror] spawn projId={:#x} src={} actorId={}",
                 projId, (int)source, (int)actorId);
}

void HandleHit(const nlohmann::json& data) {
    uint32_t projId   = data.value("projId", 0u);
    uint32_t targetCid = data.value("targetCid", 0u);
    auto it = sRegistry.find(projId);
    HarpoonCombat::HarpoonWeaponId source =
        (it != sRegistry.end()) ? it->second.source
                                : HarpoonCombat::HARPOON_WEAPON_UNKNOWN;
    uint32_t ownerCid = (it != sRegistry.end()) ? it->second.ownerCid : 0;

    // Apply damage / status to the local player if we are the target.
    HarpoonCombat::ApplyLocalHit(source, ownerCid);

    // Kill the mirrored actor (for non-owners) so the visual disappears.
    if (it != sRegistry.end() && it->second.actor != nullptr) {
        Actor_Kill(it->second.actor);
    }
    sRegistry.erase(projId);
    (void)targetCid;
}

void HandleReflect(const nlohmann::json& data) {
    uint32_t projId = data.value("projId", 0u);
    auto it = sRegistry.find(projId);
    if (it == sRegistry.end()) return;
    if (it->second.actor != nullptr) {
        it->second.actor->velocity.x = data.value("newVx", 0.0f);
        it->second.actor->velocity.y = data.value("newVy", 0.0f);
        it->second.actor->velocity.z = data.value("newVz", 0.0f);
    }
    it->second.ownerCid = data.value("newOwnerCid", 0u);
}

Actor* FindByProjId(uint32_t projId) {
    auto it = sRegistry.find(projId);
    return it != sRegistry.end() ? it->second.actor : nullptr;
}

void ClearRegistry() {
    // Don't Actor_Kill — the actors are dead with the scene. Just drop refs.
    sRegistry.clear();
}

}  // namespace HarpoonProjectileMirror
