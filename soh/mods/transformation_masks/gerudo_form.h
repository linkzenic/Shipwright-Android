/**
 * gerudo_form.h — Gerudo Form (OOT Gerudo Mask transformation, Garo-style)
 *
 * Wires the OOT Gerudo Mask to the O2rLoader's "gerudo" model. When the
 * cheat `gMods.GerudoMaskTransform` is on and Link equips the mask, we call
 * `O2rLoader_ForceModel("gerudo")`, which swaps player->skelAnime to the
 * native GeldB skeleton at `__OTR__objects/object_geldb/gGerudoRedSkel`. The
 * draw path (z_player.c → GerudoForm_DrawNullBody + GerudoHybrid_Draw)
 * renders the gerudo body with Link's tunic color and Link's anims driving
 * the body bones (per-bone source split, see gerudo_hybrid_render.h).
 *
 * Mask is removed → O2rLoader_ClearForcedModel → Link's vanilla skel/skin
 * returns. The toggle is edge-detected per-frame from an OnPlayerUpdate hook.
 *
 * Effects active while the mask is worn:
 *   - Sandstorm OFF in Haunted Wasteland (per-frame + on transition end).
 *   - Gerudo NPCs friendly: VB_GERUDOS_BE_FRIENDLY → true.
 *   - Skip card-give: VB_GIVE_ITEM_GERUDO_MEMBERSHIP_CARD → false (access is
 *     temporary; no QUEST_GERUDO_CARD is granted).
 *
 * The Ge1/Ge2/Ge3 actor patches still call GerudoForm_IsActive() — the
 * function stays in the public API and now returns true when the O2rLoader
 * has "gerudo" forced.
 */

#ifndef GERUDO_FORM_H
#define GERUDO_FORM_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// One-time init: registers VB + frame hooks. Idempotent.
void GerudoForm_Init(void);

// True if the O2rLoader currently has "gerudo" forced (which happens iff
// the cheat is on AND the OOT Gerudo Mask is equipped).
u8 GerudoForm_IsActive(void);

// Resolve Link's current tunic into a Color_RGB8, honouring the cosmetic
// CVar overrides (CVAR_COSMETIC("Link.KokiriTunic.Value"), etc). Used by
// gerudo_hybrid_render.cpp::OverrideLimbDraw to tint the gerudo outfit.
void GerudoForm_GetTunicColor(s32 tunic, Color_RGB8* out);

// Entry point called from z_player.c Player_Draw after the null-body pass.
// Routes to GerudoHybrid_Update + GerudoHybrid_Draw when "gerudo" is the
// active O2rLoader model. Returns 1 if it drew, 0 if it was a no-op.
s32 GerudoForm_TryDrawSmoothSkin(PlayState* play, Player* player);

#ifdef __cplusplus
}
#endif

#endif // GERUDO_FORM_H
