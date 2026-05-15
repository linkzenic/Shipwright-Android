/**
 * bremen_follower_actor.h - Bremen Mask chick + adult cucco follower.
 *
 * Bremen Mask spawns a chick (ACTOR_EN_NWC) that follows Link. After 60 s
 * of total cumulative wear time (tracked persistently like Chateau Romani),
 * the chick is replaced by an adult cucco (ACTOR_EN_NIW) that continues to
 * follow pacifically across all scenes. Both actors are hijacked: their
 * update is replaced with a trail-position follower that never invokes the
 * original update (so the cucco never enters its aggro/swarm state machine).
 *
 * Only cleared on Link's death (callers must invoke BremenFollower_OnDeath).
 */

#ifndef BREMEN_FOLLOWER_ACTOR_H
#define BREMEN_FOLLOWER_ACTOR_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Spawn one chick at Link's current position. Returns the actor pointer.
Actor* BremenFollower_SpawnChick(PlayState* play, Player* player);

// Spawn one adult cucco at the given world position.
Actor* BremenFollower_SpawnAdult(PlayState* play, const Vec3f* pos, s16 yaw);

// Per-frame tick. Called from case 7 of MmMaskWear_Update. Handles:
//   - Lazy spawn of the chick (or adult after upgrade) when missing in the scene.
//   - Trail position updates so the follower lags ~0.5 s behind Link.
//   - Scene-transition respawn detection (frame-counter rewind).
void BremenFollower_Tick(PlayState* play, Player* player);

// Replace the chick with an adult cucco. Called when sBremenWornTotalFrames
// reaches the 60-second threshold.
void BremenFollower_UpgradeToAdult(PlayState* play, Player* player);

// Clear all follower state. Call this from the on-death hook (find
// MmMaskWear_DeactivateChateauRomani call site for the right place).
void BremenFollower_OnDeath(void);

// Internal query — returns 1 if the chick/adult has already been spawned this run.
u8 BremenFollower_IsAdult(void);

#ifdef __cplusplus
}
#endif

#endif // BREMEN_FOLLOWER_ACTOR_H
