#ifndef PIKACHU_BEHAVIOR_H
#define PIKACHU_BEHAVIOR_H

#include "z64.h"

#define CVAR_PIKACHU_MODE "gMods.Pikachu.Mode"
#define CVAR_PIKACHU_EYES "gMods.Pikachu.Eyes"
#define CVAR_PIKACHU_MOUTH "gMods.Pikachu.Mouth"
#define CVAR_PIKACHU_TAIL "gMods.Pikachu.Tail"

#define PIKACHU_MODE_OFF 0
#define PIKACHU_MODE_STATIC 1
#define PIKACHU_MODE_SKEL 2

#define PIKACHU_SCALE 0.05f
#define PIKACHU_SPAWN_OFFSET 80.0f

void PikachuBehavior_Update(PlayState* play, Player* player);

#endif
