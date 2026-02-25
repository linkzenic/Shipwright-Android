#ifndef HUNGER_GAMES_H
#define HUNGER_GAMES_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Button index sentinel for HG mode (0xFE = Boss Rush, 0xFF = Debug)
#define HG_BUTTON_INDEX 0xFD

// File select menu functions
void FileChoose_RotateToHungerGames(GameState* thisx);
void FileChoose_UpdateHungerGamesMenu(GameState* thisx);
void FileChoose_StartHungerGamesMenu(GameState* thisx);
void FileChoose_DrawHungerGamesMenuContents(FileChooseContext* ctx);

// Save initialization
void HungerGames_InitSave(void);

// Gameplay hooks
void HungerGames_OnPlayInit(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // HUNGER_GAMES_H
