/**
 * mushroom_spot_actor.h - Mask of Scents mushroom spot prop actor
 *
 * Hijacks ACTOR_EN_LIGHTBOX (mailbox pattern). When the Mask of Scents is
 * worn AND the spot is uncollected, the mushroom DL is drawn and a "speak"
 * prompt offers Bottle with Magic Mushroom on A press (requires empty bottle).
 *
 * Spots are gated on mm.o2r being loaded (MmAssets_IsLoaded). Collection
 * state is persisted in gCustomItemState.mushroomSpotsCollected (5 bits).
 */

#ifndef MUSHROOM_SPOT_ACTOR_H
#define MUSHROOM_SPOT_ACTOR_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MUSHROOM_SPOT_COUNT 5

// Per-spot world position + scene assignment.
typedef struct {
    s16 sceneId;
    u8  roomIndex;
    Vec3f pos;
} MushroomSpotPoint;

extern const MushroomSpotPoint sMushroomSpots[MUSHROOM_SPOT_COUNT];

// Spawn one spot actor. spotIdx is stashed in actor->home.rot.z so the
// Update handler can look up its state.
Actor* MushroomSpot_Spawn(PlayState* play, const Vec3f* pos, s16 yaw, s32 spotIdx);

// Identifies a hijacked mushroom spot actor (by update function pointer).
u8 MushroomSpot_IsActor(Actor* actor);

// Save flag helpers (uses gCustomItemState.mushroomSpotsCollected).
u8   MushroomSpot_IsCollected(s32 spotIdx);
void MushroomSpot_MarkCollected(s32 spotIdx);
void MushroomSpot_ResetAll(void);

// Per-frame spawn detector. Called from case 10 of MmMaskWear_Update.
// Detects scene change via frame-counter rewind (mailbox pattern) and
// spawns the spots whose sceneId matches play->sceneNum. Gated on
// MmAssets_IsLoaded(). Idempotent.
void MushroomSpots_Tick(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // MUSHROOM_SPOT_ACTOR_H
