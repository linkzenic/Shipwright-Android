#ifndef GARO_POST_LIMB_H
#define GARO_POST_LIMB_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Drives Player's skeleton through SkelAnime_DrawFlexLod with mesh-suppressed
 * limbs so all per-limb side effects (bodyPartsPos, leftHandPos, focus.pos,
 * feetPos, shieldMf) fire correctly while Link's geometry stays hidden.
 * The Garo body is rendered separately by GaroSkin_Draw.
 */
void GaroForm_DrawNullBody(PlayState* play, Player* player, s32 lod);

#ifdef __cplusplus
}
#endif

#endif // GARO_POST_LIMB_H
