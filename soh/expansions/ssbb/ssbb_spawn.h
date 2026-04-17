#ifndef SSBB_SPAWN_H
#define SSBB_SPAWN_H

#include "z64.h"

// Unified Pikachu behavior CVar: 0=Off, 1=Companion, 2=Transformation
#define CVAR_SSBB_PIKACHU "gMods.Pikachu.Behavior"

#define SSBB_PIKACHU_OFF 0
#define SSBB_PIKACHU_REST 1
#define SSBB_PIKACHU_ANIM 2

// Call from Player update to update SSBB characters
void SSBBSpawn_Update(PlayState* play, Player* player);

// Call from Player draw to render SSBB characters
void SSBBSpawn_Draw(PlayState* play, Player* player);

#endif // SSBB_SPAWN_H
