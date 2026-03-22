#ifndef SSBB_THUNDER_JOLT_H
#define SSBB_THUNDER_JOLT_H

#include "z64.h"

// Thunder Jolt — bouncing electric ball projectile
// Spawned by Pikachu's SpecialN (B while still)
// Bounces along terrain, damages enemies on contact, disappears after timer/hit

typedef struct SSBBThunderJolt {
    Actor actor;
    ColliderSphere collider;
    s16 timer;       // Lifetime countdown (frames)
    f32 bounceVelY;  // Current vertical velocity for bouncing
    u8 hitSomething; // Set when collider registers AT hit
} SSBBThunderJolt;

void SSBBThunderJolt_Init(Actor* thisx, PlayState* play);
void SSBBThunderJolt_Destroy(Actor* thisx, PlayState* play);
void SSBBThunderJolt_Update(Actor* thisx, PlayState* play);
void SSBBThunderJolt_Draw(Actor* thisx, PlayState* play);

#endif // SSBB_THUNDER_JOLT_H
