/**
 * spiritual_stone_statue.h - MM Owl Statue actor for spiritual-stone warp points.
 *
 * Spawned via actor-hijack on ACTOR_EN_LIGHTBOX (same pattern as somaria_cubes).
 * Static decorative statue, recolored per stone (Kokiri/Goron/Zora).
 *
 * This is a C file that gets #include'd by the orchestrator (spiritual_stones.cpp),
 * matching how somaria_cubes.c is consumed by item_cane_of_somaria.c. No vcxproj
 * compile entry is needed.
 */

#ifndef SPIRITUAL_STONE_STATUE_H
#define SPIRITUAL_STONE_STATUE_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Spawn a tinted owl statue at the given position. Returns the actor or NULL.
// stone: 0=Kokiri (green), 1=Goron (red), 2=Zora (blue).
Actor* SpiritualStoneStatue_Spawn(PlayState* play, Vec3f* pos, s16 rotY, int stone);

// Identify whether an actor is one of our statues (so callers can avoid
// double-spawning or accidentally hijacking it).
u8 SpiritualStoneStatue_IsStatue(Actor* actor);

#ifdef __cplusplus
}
#endif

#endif // SPIRITUAL_STONE_STATUE_H
