#ifndef GERUDO_HYBRID_RENDER_H
#define GERUDO_HYBRID_RENDER_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Gerudo form render — path-swap override (NOT a separate skeleton).
 *
 * The gerudo form is purely visual: Link keeps his own skeleton, anims, and
 * Player_DrawImpl walk. The gerudo .o2r (nei/gerudo.o2r) ships a complete,
 * Link-rigged gerudo mesh under a parallel namespace
 *   `objects/gerudoPlayer/object_link_boy/X`  (and .../object_link_child/X)
 * mirroring the vanilla
 *   `objects/object_link_boy/X`
 * leaf-for-leaf — every body part, item DL, sword/shield hold, etc. has a
 * gerudo twin at the same name.
 *
 * GerudoForm_OverrideLimbDraw wraps Link's normal limb-draw override
 * (Player_OverrideLimbDrawGameplayDefault, passed in via
 * GerudoForm_SetChainedOverride). For each limb it:
 *   1. snapshots the limb's own DL,
 *   2. runs the chained vanilla override (which may overwrite *dList with a
 *      hardcoded vanilla `__OTR__objects/object_link_boy/...` path),
 *   3. if a gerudo-native DL was present, restores it; otherwise rewrites the
 *      vanilla prefix to the `objects/gerudoPlayer/...` prefix so the gerudo
 *      resource resolves instead.
 * The rewritten path is allocated from the gfx frame arena (Graph_Alloc),
 * valid for the rest of the frame.
 *
 * The swap is gated on the gerudo form being active (mask equipped via
 * O2rLoader). When not transformed the wrapper still runs but every path
 * stays vanilla, so Link looks normal. There is no separate gerudo SkelAnime,
 * no null-body pass, and no native GeldB skeleton in this path — the form is
 * driven entirely by Link's own draw with DL strings swapped underneath it.
 *
 * Tunic tinting (Kokiri / Goron / Zora, with cosmetic CVar overrides) is
 * resolved by GerudoForm_GetTunicColor (gerudo_form.cpp).
 */

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
