/**
 * gerudo_form.h — Gerudo Form (OOT Gerudo Mask transformation, Garo-style)
 *
 * Wires the OOT Gerudo Mask to the O2rLoader's "gerudo" model. When the
 * cheat `gMods.GerudoMaskTransform` is on and Link equips the mask, we call
 * `O2rLoader_ForceModel("gerudo")`. Link keeps his own skeleton and anims;
 * the gerudo look comes from a draw-time DL path-swap
 * (GerudoForm_OverrideLimbDraw) that redirects vanilla Link DL references to
 * the gerudo .o2r's `objects/gerudoPlayer/...` twins, tinted with Link's
 * current tunic color (see gerudo_hybrid_render.h).
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

// Retained no-op (always returns 0). The gerudo form now draws entirely
// through Link's own Player_DrawImpl with a DL path-swap, so there is no
// separate gerudo body pass to trigger here. Kept for ABI stability with any
// surviving z_player.c callsite.
s32 GerudoForm_TryDrawSmoothSkin(PlayState* play, Player* player);

// Custom sword DLs for dual-wield. Adult Link gets Master-Sword-styled custom
// scimitars, Child Link gets Kokiri-Sword-styled ones — both supplied by the
// gerudo .o2r. Returns NULL if the resource isn't present; caller should fall
// back to vanilla hand DLs in that case (cosmetic miss, not a crash).
Gfx* GerudoForm_GetSwordDL_L(void);
Gfx* GerudoForm_GetSwordDL_R(void);

// MM Gerudo combo bridge — implemented in mm_player_form.cpp. Called from
// Player_PostLimbDrawGameplay (z_player_lib.c) at the L_HAND / R_HAND limbs,
// where the bone matrix is in scope for Matrix_MultVec3f. PunchActiveThisFrame
// gates trail+hitbox setup to the active damage window; the R-trail index
// getter returns the EffectBlure slot spawned for the R sword (the L sword
// piggybacks on Link's vanilla meleeWeaponEffectIndex). Damage is the
// per-slash value flagged by the action handler.
u8  GerudoForm_PunchActiveThisFrame(void);
s32 GerudoForm_GetRightTrailEffectIndex(void);
u8  GerudoForm_GetCurrentDamage(void);

// Gerudo Mirror Shield is OOT's vanilla shield 1:1 — no MM-side pose override.
// GerudoForm_Update keeps heldItemAction pinned to the one-handed Master/Kokiri
// sword (so Player_HoldsTwoHandedWeapon stays false and the vanilla pipeline
// runs unmodified), and GerudoForm_GetSwordDL_R returns NULL while SHIELDING
// so R_HAND draws the equipped shield (path-swapped to gerudoPlayer/...).

#ifdef __cplusplus
}
#endif

#endif // GERUDO_FORM_H
