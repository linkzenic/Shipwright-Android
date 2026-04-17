#ifndef RANDODRAW_H
#define RANDODRAW_H
#pragma once

#include "../item-tables/ItemTableTypes.h"

typedef struct PlayState PlayState;

#ifdef __cplusplus
extern "C" {
#endif
void Randomizer_DrawSmallKey(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawMap(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawCompass(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawKeyRing(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawBossKey(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawBeanSprout(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawBossSoul(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawDoubleDefense(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawMasterSword(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawTriforcePiece(PlayState* play, GetItemEntry getItemEntry);
void Randomizer_DrawTriforcePieceGI(PlayState* play, GetItemEntry getItemEntry);
void Randomizer_DrawOcarinaButton(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawBronzeScale(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawPowerBracelet(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawLadder(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawKneePads(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawJabberNut(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawOpenChest(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawFishingPoleGI(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawSkeletonKey(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawMysteryItem(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawBombchuBagInLogic(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawBombchuBag(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawOverworldKey(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawRocsFeather(PlayState* play, GetItemEntry* getItemEntry);

// Custom 24 Items - Draw functions (Skijer)
void Randomizer_DrawRocsFeatherSkijer(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawRocsCape(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawWhip(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawSpinner(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawBombArrows(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawFireRod(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawIceRod(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawLightRod(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawDekuLeaf(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawSwitchHook(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawMogmaMitts(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawGustJar(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawBallAndChain(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawCaneOfSomaria(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawDominionRod(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawTimeGate(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawBeetle(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawShovel(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawHyliaGrace(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawZonaiPermafrost(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawDemiseDestruction(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawMagnesis(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawStasis(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawLantern(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawCryonis(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawDesireSensor(PlayState* play, GetItemEntry* getItemEntry);

// Custom items - Pokeball & Minish Cap
void Randomizer_DrawPokeball(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawMinishCap(PlayState* play, GetItemEntry* getItemEntry);

// Extended Equipment Get-Item 3D Models
void Randomizer_DrawExtCaneOfByrna(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawExtFourSword(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawExtIronKnuckleAxe(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawExtDivineShield(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawExtSheikahShield(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawExtShieldOfIkana(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawExtMagicCape(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawExtSpiritBreastplate(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawExtChampionsTunic(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawExtPegasusAnklet(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawExtPendantOfMemories(PlayState* play, GetItemEntry* getItemEntry);
void Randomizer_DrawExtWaterDragonScale(PlayState* play, GetItemEntry* getItemEntry);

// MM Mask Get-Item 3D Models (all 24, from mm.o2r)
void Randomizer_DrawMmMask(PlayState* play, GetItemEntry* getItemEntry);

// Chateau Romani bottle (from mm.o2r)
void Randomizer_DrawChateauRomani(PlayState* play, GetItemEntry* getItemEntry);

#define GET_ITEM_MYSTERY                                                                                 \
    {                                                                                                    \
        ITEM_NONE_FE, 0, 0, 0, 0, MOD_RANDOMIZER, MOD_RANDOMIZER, ITEM_NONE_FE, 0, false, ITEM_FROM_NPC, \
            ITEM_CATEGORY_JUNK, ITEM_NONE_FE, MOD_RANDOMIZER, (CustomDrawFunc)Randomizer_DrawMysteryItem \
    }
#ifdef __cplusplus
};
#endif

#endif
