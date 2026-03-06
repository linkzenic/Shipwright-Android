#ifndef PIKACHU_BEHAVIOR_H
#define PIKACHU_BEHAVIOR_H

#include "z64.h"

#define CVAR_PIKACHU_MODE "gMods.Pikachu.Mode"

#define PIKACHU_MODE_OFF      0
#define PIKACHU_MODE_STATIC   1
#define PIKACHU_MODE_RESTPOSE 2
#define PIKACHU_MODE_ANIMATED 3

#define PIKACHU_SCALE 1.0f
#define PIKACHU_SPAWN_OFFSET 80.0f
#define PIKACHU_SPIN_SPEED 0x200

void PikachuBehavior_Update(PlayState* play, Player* player);

#endif
