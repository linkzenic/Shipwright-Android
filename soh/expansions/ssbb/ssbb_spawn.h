#ifndef SSBB_SPAWN_H
#define SSBB_SPAWN_H

#include "z64.h"

// Separate CVar for SSBB Pikachu
// 0 = off, 1 = rest pose (DL only), 2 = animated (Wait3)
#define CVAR_SSBB_PIKACHU "gExpansions.SSBB.Pikachu"

#define SSBB_PIKACHU_OFF  0
#define SSBB_PIKACHU_REST 1
#define SSBB_PIKACHU_ANIM 2

// Call from Player update to update SSBB characters
void SSBBSpawn_Update(PlayState* play, Player* player);

// Call from Player draw to render SSBB characters
void SSBBSpawn_Draw(PlayState* play, Player* player);

#endif // SSBB_SPAWN_H
