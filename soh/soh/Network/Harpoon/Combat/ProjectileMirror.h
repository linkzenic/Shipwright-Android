#ifndef SOH_NETWORK_HARPOON_COMBAT_PROJECTILE_MIRROR_H
#define SOH_NETWORK_HARPOON_COMBAT_PROJECTILE_MIRROR_H
#ifdef __cplusplus

// =============================================================================
// ProjectileMirror — spawn-only synchronization of SW97 spells/arrows + form
// projectiles (FD beams, Deku bubbles, Zora fins, Cane of Somaria cubes).
//
// The local-owner of a projectile spawns the actor and broadcasts
// COMBAT.PROJECTILE_SPAWN with (type, pos, vel, charge). Each peer spawns
// a mirrored copy with the HARPOON_REMOTE_PROJECTILE_BIT set in params so
// the actor's collision routines know not to broadcast their own hits.
//
// When the owner's local actor confirms a hit on a remote player, the
// owner broadcasts COMBAT.PROJECTILE_HIT with (projId, targetCid). Peers
// kill the mirrored actor + the target peer applies damage.
//
// COMBAT.PROJECTILE_REFLECT { projId, newVelXYZ, newOwnerCid } is fired
// by a peer whose Mirror Shield bounced the projectile; recipients update
// the mirrored actor's velocity and re-attribute ownership.
// =============================================================================

#include <cstdint>
#include <nlohmann/json.hpp>
#include "CombatSync.h"

extern "C" {
#include "z64.h"
}

namespace HarpoonProjectileMirror {

// Bit in `Actor::params` set on remote-mirrored projectile actors so their
// collision routines skip the broadcast path.
constexpr uint16_t REMOTE_PROJECTILE_BIT = 0x8000;

// Broadcast a projectile spawn. Returns the assigned projId (caller stores
// this in its own actor instance so PROJECTILE_HIT can reference it).
uint32_t BroadcastSpawn(HarpoonCombat::HarpoonWeaponId source,
                        float px, float py, float pz,
                        float vx, float vy, float vz,
                        float yaw, uint16_t charge);

// Broadcast a confirmed hit-on-player.
void BroadcastHit(uint32_t projId, uint32_t targetCid,
                  float hitX, float hitY, float hitZ);

// Broadcast a Mirror Shield reflect — reverse velocity and reassign owner.
void BroadcastReflect(uint32_t projId, float newVx, float newVy, float newVz,
                      uint32_t newOwnerCid);

// Network event handlers (called from Harpoon.cpp dispatch).
void HandleSpawn(const nlohmann::json& data);
void HandleHit(const nlohmann::json& data);
void HandleReflect(const nlohmann::json& data);

// Look up the local Actor* for a given projId (so an actor's own update
// can find itself in the registry). Returns nullptr if not found.
Actor* FindByProjId(uint32_t projId);

// Cleanup hook — called when a scene unloads. Stale projectile entries
// are dropped.
void ClearRegistry();

}  // namespace HarpoonProjectileMirror

#endif  // __cplusplus
#endif  // SOH_NETWORK_HARPOON_COMBAT_PROJECTILE_MIRROR_H
