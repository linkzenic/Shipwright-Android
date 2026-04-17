/**
 * sm64_mario.h - SM64 Mario for OOT via libsm64
 *
 * Public API for the libsm64 integration. All SM64 physics run inside
 * sm64.dll (compiled separately from the SM64 decomp). We just send
 * inputs + collision geometry and receive position + animated mesh.
 */

#ifndef SM64_MARIO_H
#define SM64_MARIO_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Called once when CVAR is first enabled. Loads ROM, initializes libsm64.
// Returns 1 on success, 0 on failure.
s32 Sm64Mario_Init(PlayState* play, Player* player);

// Called every frame when Mario mode is active.
void Sm64Mario_Update(PlayState* play, Player* player);

// Called from Player_Draw when Mario mode is active.
void Sm64Mario_Draw(PlayState* play, Player* player);

// Returns true if the Mario CVAR is enabled.
u8 Sm64Mario_IsActive(void);

// Cleanup when CVAR is disabled.
void Sm64Mario_Reset(void);

// Called on scene transition to reload collision surfaces.
void Sm64Mario_OnSceneChange(PlayState* play);

// Re-sync Mario's position to OOT Player after Player_UpdateCommon runs.
void Sm64Mario_SyncPositionToPlayer(PlayState* play, Player* player);

#ifdef __cplusplus
}
#endif

#endif // SM64_MARIO_H
