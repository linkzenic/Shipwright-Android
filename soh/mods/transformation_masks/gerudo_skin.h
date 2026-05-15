#ifndef GERUDO_SKIN_H
#define GERUDO_SKIN_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Fallback Skin-based render path for the gerudo form.
 *
 * The default path (GerudoHybrid_Draw) uses SkelAnime_DrawFlexOpa against
 * the OOT Gerudo Fighter's native 23-bone skeleton — that's what runs when
 * CVar `gGerudoHybrid.Disable == 0`. This skin path is reserved for a future
 * CPU-blended seam-smoothing variant (mirrors `GaroSkin_Draw`). For now it
 * is a no-op so the CVar has somewhere to land.
 */
s32  GerudoSkin_Setup(PlayState* play);
void GerudoSkin_Teardown(PlayState* play);
void GerudoSkin_Draw(PlayState* play, Player* player);

#ifdef __cplusplus
}
#endif

#endif // GERUDO_SKIN_H
