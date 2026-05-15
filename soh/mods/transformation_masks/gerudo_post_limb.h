#ifndef GERUDO_POST_LIMB_H
#define GERUDO_POST_LIMB_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Drives Player's skeleton through SkelAnime_DrawFlexLod with mesh-suppressed
 * limbs so all per-limb side effects (bodyPartsPos, leftHandPos, focus.pos,
 * feetPos, shieldMf) fire correctly while Link's mesh stays hidden.
 *
 * Called from z_player.c when the active O2rLoader model is "gerudo". At that
 * point player->skelAnime.skeleton has been swapped to the 23-bone GeldB
 * skel by O2rLoader_SwapSkeleton, so the limbIndex passed to our callbacks
 * is in the GELDB_LIMB_* enum space, not PLAYER_LIMB_* — our PostLimbDraw
 * translates GeldB limbs → Link bodyParts via sGerudoLimbToBodyPart.
 *
 * The gerudo body itself is rendered separately by GerudoHybrid_Draw.
 */
void GerudoForm_DrawNullBody(PlayState* play, Player* player, s32 lod);

#ifdef __cplusplus
}
#endif

#endif // GERUDO_POST_LIMB_H
