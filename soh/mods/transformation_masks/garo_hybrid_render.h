#ifndef GARO_HYBRID_RENDER_H
#define GARO_HYBRID_RENDER_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Hybrid Garo render — uses garo_hybrid.o2r (19-bone skeleton combining
 * MM Garo upper body + OOT Link adult lower body) with its own SkelAnime.
 *
 * Per-bone source split — always parallel, no mode CVar:
 *   - Body bones (head, torso, arms, legs) → Link's anim. Whatever Link
 *     is currently animating (swim, climb, walk, run, item draw…) drives
 *     the equivalent bone on the hybrid skeleton.
 *   - Cloak + sword bones (ROBE_TOP/BACK/LEFT/RIGHT/FRONT, L_SWORD, R_SWORD)
 *     → Garo's anim (default: gGaroIdleAnim). These swing/ondea independently
 *     of Link's body anim. The cloak's WORLD position still follows the
 *     body because cloak bones are children of body bones in the hybrid
 *     skeleton — Link rotates the shoulder, the cloak base translates with
 *     it; Garo's anim adds the sway on top.
 *
 * To change which Garo anim drives the cloak/swords, call:
 *   GaroHybrid_SetAnim(play, "__OTR__objects/object_jso/gGaroSlashLoopAnim");
 * (Use NULL or the idle path to reset.)
 */
s32  GaroHybrid_Setup(PlayState* play);
void GaroHybrid_Teardown(void);
void GaroHybrid_Update(PlayState* play, Player* player);
void GaroHybrid_Draw  (PlayState* play, Player* player);

/** Select a specific Garo animation by OTR path. NULL = restore idle. */
void GaroHybrid_SetAnim(PlayState* play, const char* otrPath);

#ifdef __cplusplus
}
#endif

#endif // GARO_HYBRID_RENDER_H
