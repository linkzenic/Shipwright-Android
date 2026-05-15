#ifndef GERUDO_HYBRID_RENDER_H
#define GERUDO_HYBRID_RENDER_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Hybrid Gerudo render — uses the OOT Gerudo Fighter (GeldB) native 23-bone
 * skeleton (`__OTR__objects/object_geldb/gGerudoRedSkel`) with its own
 * SkelAnime running gerudo native anims.
 *
 * Per-bone source split (always merged, no mode CVar):
 *   - Body bones (TORSO, HEAD, UPPER_ARMs, FOREARMs, HANDs, THIGHs, SHINs,
 *     FEET) get Link's anim values via runtime retarget — so climb/swim/
 *     walk/run/item-poses drive the gerudo body just like Link's.
 *   - NECK, PONYTAIL, VEIL, WRISTs, SWORDs, WAIST stay at gerudo's native
 *     anim values (default: gGerudoRedNeutralAnim), so the head accents and
 *     swords swing independently of Link's body anim.
 *   - PONYTAIL gets an additional velocity-driven inertia sway on top —
 *     lags behind body motion, smooth restore when stopping.
 *
 * Tunic tinting: OverrideLimbDraw intercepts TORSO/clothing limbs and emits
 * `gDPSetEnvColor` using Link's current tunic color (Kokiri / Goron / Zora,
 * with cosmetic CVar override support).
 *
 * To switch the gerudo native anim (e.g. play gGerudoRedSlashAnim during a
 * sword swing), call:
 *   GerudoHybrid_SetAnim(play, "__OTR__objects/object_geldb/gGerudoRedSlashAnim");
 * (Use NULL to reset to the default neutral idle.)
 */
s32  GerudoHybrid_Setup(PlayState* play);
void GerudoHybrid_Teardown(void);
void GerudoHybrid_Update(PlayState* play, Player* player);
void GerudoHybrid_Draw  (PlayState* play, Player* player);

/** Select a specific gerudo native animation by OTR path. NULL = restore neutral. */
void GerudoHybrid_SetAnim(PlayState* play, const char* otrPath);

/**
 * Wrapper around Player_OverrideLimbDrawGameplayDefault (or any chained
 * override). After the chained call sets `*dList`, this function rewrites
 * vanilla `__OTR__objects/object_link_boy/...` paths to
 * `__OTR__objects/gerudoPlayer/object_link_boy/...` so the gerudo o2r's
 * versions of body parts + items render instead. The rewrite uses a buffer
 * allocated from the gfx frame arena (Graph_Alloc) — safe for the rest of
 * the frame.
 *
 * Pair with GerudoForm_SetChainedOverride before calling Player_DrawImpl.
 */
s32 GerudoForm_OverrideLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList,
                                Vec3f* pos, Vec3s* rot, void* arg);

/** Sets the next override that GerudoForm_OverrideLimbDraw forwards to.
 *  Pass the original `overrideLimbDraw` from Player_DrawGameplay. */
void GerudoForm_SetChainedOverride(void* fn);

#ifdef __cplusplus
}
#endif

#endif // GERUDO_HYBRID_RENDER_H
