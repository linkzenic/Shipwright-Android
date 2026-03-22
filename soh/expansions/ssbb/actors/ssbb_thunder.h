#ifndef SSBB_THUNDER_H
#define SSBB_THUNDER_H

#include "z64.h"

// Thunder — Lightning column from sky
// Spawned by Pikachu's SpecialLw (Din's Fire / Demise on C-button)
// Vertical lightning bolt + ground shockwave, light arrow damage
// Active for the entire animation duration (~30 frames)

typedef struct SSBBThunder {
    Actor actor;
    ColliderCylinder collider;  // Tall cylinder for the lightning column
    s16 timer;                  // Lifetime
    f32 columnHeight;           // Current visual height (grows from 0 to max)
    u8 phase;                   // 0=charging, 1=bolt active, 2=fading
} SSBBThunder;

void SSBBThunder_Init(Actor* thisx, PlayState* play);
void SSBBThunder_Destroy(Actor* thisx, PlayState* play);
void SSBBThunder_Update(Actor* thisx, PlayState* play);
void SSBBThunder_Draw(Actor* thisx, PlayState* play);
void SSBBThunder_Spawn(PlayState* play, Player* player);

#endif // SSBB_THUNDER_H
