#ifndef TOON_LIGHTING_H
#define TOON_LIGHTING_H

// C-callable surface of the Wind Waker-style cel/shadow module (implementation in ToonLighting.cpp).
// Kept minimal: the decompiled game code (C) only needs the shadow-receiver predicate so the actor
// draw loop can reorder a small whitelist of walkable "floor" actors ahead of the shadow flush.

#ifdef __cplusplus
extern "C" {
#endif

struct Actor;

// True for the small curated set of actors that are really walkable floors (the castle-town drawbridge,
// the Gerudo Valley bridge, certain dungeon platforms). These are drawn before the actor-shadow volumes
// flush so shadows land on them like the static scene, and they never cast a shadow of their own.
int ToonLighting_IsShadowReceiver(struct Actor* actor);

#ifdef __cplusplus
}
#endif

#endif // TOON_LIGHTING_H
