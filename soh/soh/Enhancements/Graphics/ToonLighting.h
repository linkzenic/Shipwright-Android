#ifndef TOON_LIGHTING_H
#define TOON_LIGHTING_H

// C-callable surface of the Wind Waker-style cel/shadow module (implementation in ToonLighting.cpp).
// Kept minimal: the decompiled game code (C) only needs the shadow-receiver predicate, the cached
// feature switches (so the per-actor draw path never does string-keyed CVar lookups), and the lens
// bracket helpers for the one draw pass that runs outside the main actor loop.

#ifdef __cplusplus
extern "C" {
#endif

struct Actor;
struct GraphicsContext;

// True for the small curated set of actors that are really walkable floors (the castle-town drawbridge,
// the Gerudo Valley bridge, certain dungeon platforms). These are drawn before the actor-shadow volumes
// flush so shadows land on them like the static scene, and they never cast a shadow of their own.
int ToonLighting_IsShadowReceiver(struct Actor* actor);

// Cached once-per-frame feature switches (refreshed from the CVars at the top of each game frame).
// The draw code asks these instead of reading CVars per actor: a CVarGet* is a string-keyed hash
// lookup, far too expensive to repeat for every drawn actor every frame.
int ToonLighting_FeaturesActive(void);         // cel relight OR actor shadows on (gates the draw hook)
int ToonLighting_CelEnabled(void);             // cel relight on (gates the toon bracket)
int ToonLighting_ShadowsEnabled(void);         // actor shadows on (gates the shadow flush/disarm)
int ToonLighting_SuppressVanillaShadows(void); // shadows on AND set to hide the vanilla blob shadows

// Wrap Actor_DrawLensActors with these: lens actors draw through Actor_Draw after the main toon
// bracket closed, so the bracket must re-open around them (and the shadow capture must be disarmed
// after the last one) to keep the module's stream tracking in sync.
void ToonLighting_LensBracketBegin(struct GraphicsContext* gfxCtx);
void ToonLighting_LensBracketEnd(struct GraphicsContext* gfxCtx);

#ifdef __cplusplus
}
#endif

#endif // TOON_LIGHTING_H
