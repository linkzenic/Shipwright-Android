/**
 * sm64_mario.h - SM64 Mario for OOT via libsm64
 *
 * Public API for the libsm64 integration. All SM64 physics run inside
 * sm64.dll (compiled separately from the SM64 decomp). We just send
 * inputs + collision geometry and receive position + animated mesh.
 */

#ifndef SM64_MARIO_H
#define SM64_MARIO_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Called once when CVAR is first enabled. Loads ROM, initializes libsm64.
// Returns 1 on success, 0 on failure.
s32 Sm64Mario_Init(PlayState* play, Player* player);

// Called every frame when Mario mode is active.
void Sm64Mario_Update(PlayState* play, Player* player);

// Called from Player_Draw when Mario mode is active.
void Sm64Mario_Draw(PlayState* play, Player* player);

// Returns true if the Mario CVAR is enabled AND we're not in a post-
// scene-transition suspend window. The suspend window forces the same
// Reset→Init cycle that a CVAR toggle produces, which is empirically the
// only way to get actors interacting with Mario after a scene change.
u8 Sm64Mario_IsActive(void);

// Call at the TOP of Player_Update hook every frame, before any Mario
// logic. Handles the transition-suspend countdown — on scene change or
// LOADING rising edge, suspend Mario so IsActive returns false; Link gets
// a clean frame of normal state-machine ops; then resume and let Init
// recreate Mario cleanly. Same effect as a manual CVAR off/on toggle.
void Sm64Mario_TickTransitionSuspend(PlayState* play, Player* player);

// Returns true only when Mario is enabled AND a valid Mario id exists.
// Use this from draw hooks so Link renders normally while Mario is being
// (re)created — otherwise the hook would hide Link with nothing to replace it.
u8 Sm64Mario_IsReady(void);

// Stricter gate for the draw path: true only when Mario is ready AND the
// mesh buffer has triangles. Prevents the "both invisible" state where
// sSm64MarioId >= 0 but sm64_mario_tick hasn't populated the buffer yet.
u8 Sm64Mario_HasMesh(void);

// Pure CVAR check — true whenever the player has Mario mode toggled on,
// independently of the suspend cascade (IsActive returns false during
// the detransform window; this stays true). Used by the draw hook to
// keep Link's DL hidden across detransform cutscenes so the player
// never sees Link "pop in" during a door anim or item-get sequence.
// Purely visual — no impact on the suspend / Reset / Init logic.
u8 Sm64Mario_ShouldHideLink(void);

// True when Mario is using Lens of Truth — read by Sm64Mario_HasMesh so
// Mario's own mesh disappears while Lens is held (matches Ivan's
// shouldDraw=0 behavior in z_en_partner.c:617-622).
u8 Sm64Mario_LensActive(void);

// Items — Ivan-style item handling driven by Mario's input. Each frame from
// Sm64Mario_Update we read C-button (and optionally D-pad) presses and run
// the equivalent of EnPartner's UseItem. Items spawn at the player actor's
// position (which is Mario's, post position-writeback) facing Mario's yaw.
void Sm64Mario_HandleItems(PlayState* play, Player* player);

// Renders the lit deku stick model (gLinkChildLinkDekuStickDL) at Mario's
// hand whenever the player holds a deku stick C-button. Called from
// Sm64Mario_Draw so the stick appears in the same display list pass as
// Mario's body. No-op when the stick isn't being used.
void Sm64Mario_DrawHeldStick(PlayState* play);

// Mario Mask C-Down toggle — when CVar `gSm64MarioMaskForce` is set, force
// ITEM_MARIO_MASK into the C-Down slot every frame (the player can't
// unequip it through the kaleido subscreen) and treat C-Down presses as a
// toggle of `gSm64Mario`. Call from the Player_Update hook before the
// vanilla item-use handlers run, so the press gets consumed before
// Player_ItemAction sees it. No-op when the force CVar is off.
void Sm64MarioMask_ForceAndToggle(PlayState* play, Player* player);

// Item-state cleanup. Called from Sm64Mario_Reset on every detransform,
// scene suspend, and CVAR off. The WithPlayer variant additionally restores
// player->ivanDamageMultiplier / ivanFloating / hoverBootsTimer to defaults
// so a held Din's/Farore's spell doesn't leak into Link or Ivan-coop mode.
void Sm64Mario_ItemsReset(void);
void Sm64Mario_ItemsResetWithPlayer(Player* player);

// Cleanup when CVAR is disabled.
void Sm64Mario_Reset(void);

// Called on scene transition to reload collision surfaces.
void Sm64Mario_OnSceneChange(PlayState* play);

// Call from Player_Init (z_player.c:11510 area). Fires on every scene spawn —
// loading zones, warps, respawns. Drops the current Mario + mesh buffer and
// pins scene-change detection so Sm64Mario_Update recreates in the new scene.
// Same reliability pattern as TransformMasks_Init.
void Sm64Mario_OnPlayerInit(PlayState* play, Player* player);

// --- Combat bridge ---

// Intercepts enemy/hazard damage before Player_UpdateCommon. When Mario is
// ready, steals any AC_HIT on Link's bumper + the pending colChkInfo.damage,
// forwards it to sm64_mario_take_damage so Mario plays its knockback animation,
// then clears both so OOT's func_80837C0C never fires. No-op when Mario is off.
void Sm64Mario_InterceptDamage(PlayState* play, Player* player);

// Belt-and-suspenders cleanup after Player_UpdateCommon: clears
// PLAYER_STATE1_DAMAGED if something slipped through Intercept.
void Sm64Mario_ScrubDamageState(PlayState* play, Player* player);

// (Re)binds the Master-Sword punch collider to the current Player actor.
// Call from Player_Init after Sm64Mario_OnPlayerInit — the Player actor
// pointer changes across scene transitions so we re-init each time.
void Sm64Mario_InitAttackCollider(PlayState* play, Player* player);

// Positions the AT collider at Mario's fist/foot/impact zone during attack
// frames and arms it via CollisionCheck_SetAT. Call after Sm64Mario_Update.
void Sm64Mario_UpdateAttackCollider(PlayState* play, Player* player);

// --- Audio bridge ---

// Called from the audio thread (code_800E4FE0.c AudioMgr_CreateNextAudioBuffer)
// after AudioSynth_Update / MmDirectAudio_MixInto / PikaSfx_MixInto. Mixes
// libsm64's generated PCM (queued by Sm64Mario_Update on the game thread)
// into the stereo s16 output buffer at 32000 Hz. numSamples = stereo pairs.
void Sm64Audio_MixInto(int16_t* outBuf, uint32_t numSamples);

// Re-sync Mario's position to OOT Player after Player_UpdateCommon runs.
void Sm64Mario_SyncPositionToPlayer(PlayState* play, Player* player);

#ifdef __cplusplus
}
#endif

#endif // SM64_MARIO_H
