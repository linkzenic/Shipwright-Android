#ifndef GARO_SKIN_H
#define GARO_SKIN_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialise Garo's Skin object once. Returns 1 on success.
// Called automatically by GaroSkin_Draw on first invocation.
s32 GaroSkin_Setup(PlayState* play);

// Free the Skin object (Vtx buffers, jointTable, etc.).
void GaroSkin_Teardown(PlayState* play);

// Draw Garo via OOT's Skin system, driven by Player's skelAnime.jointTable.
// Internally calls Skin_DrawImpl with player->actor.world.pos/rot/scale.
void GaroSkin_Draw(PlayState* play, Player* player);

// Per-frame attack-kit update. Called from z_player.c Player_Update after
// Player_UpdateCommon. Drives the tap-swing (3 sword projectiles) and the
// hold-charge → spin attack. No-op when Garo is not the active model.
void GaroForm_Update(PlayState* play, Player* player);

// Draw any live Garo sword projectiles. Called from z_player.c
// Player_DrawGameplay while the Garo skin is active.
void GaroForm_DrawProjectiles(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // GARO_SKIN_H
