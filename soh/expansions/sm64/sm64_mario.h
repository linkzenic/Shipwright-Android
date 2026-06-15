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

// True only while the Vanish Cap is worn. Forces NoClip (z_bgcheck.c) so Mario
// phases walls but floor-based loading zones still trigger.
u8 Sm64Mario_IsVanishActive(void);

// True while Mario is doing his boss-room "super attack" (the spin, ACT_TWIRLING).
// Read by boss_super_damage (transformation_masks.c) so the reworked bosses take
// FD-style paralyze-or-damage from Mario's spin. Gated to boss rooms implicitly:
// only those bosses query the super-damage system.
u8 Sm64Mario_IsSuperAttacking(void);

// Mario's independent health as a 0..8 wedge count (SM64 power-meter segments).
// Read by the HUD HP dial. 8 hits to die; decoupled from Link's heart count.
s32 Sm64Mario_GetHealthWedges(void);

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

// --- Mario-mode power-up timer / cooldown (sm64_mario_items.c) ---
// Four D-pad power-ups, each with a USE timer and a proportional TIMEOUT
// (cooldown = usedFraction * maxCooldown). Only one is ACTIVE at a time.
// Slot index order matches the corner HUD top→bottom:
//   0 = Wing, 1 = Metal, 2 = Vanish, 3 = Fire.
#define SM64_CAP_HUD_SLOT_COUNT 4
#define SM64_CAP_PHASE_READY    0
#define SM64_CAP_PHASE_ACTIVE   1
#define SM64_CAP_PHASE_COOLDOWN 2

// Advance the active use timer + every cooling slot by one frame. Called from
// the normal path of Sm64Mario_HandleItems, so it only ticks while Mario mode
// is genuinely active — naturally frozen during suspend / cutscene / mode-off.
void Sm64MarioCaps_Tick(void);

// Drop the currently-active cap into its proportional cooldown. Called from
// Sm64Mario_Reset (mode off / detransform) and Sm64Mario_OnPlayerInit (scene
// change). Does not clear the persistent per-cap timer state.
void Sm64MarioCaps_OnSuspend(void);

// HUD read accessors (used by the corner power-up HUD in z_parameter.c).
u8  Sm64MarioCaps_GetPhase(s32 idx);            // SM64_CAP_PHASE_*
f32 Sm64MarioCaps_GetCharge(s32 idx);           // 0..1 (ACTIVE drains, COOLDOWN fills, READY=1)
s32 Sm64MarioCaps_GetRemainingSeconds(s32 idx); // whole seconds left in ACTIVE/COOLDOWN (0 if READY)
s32 Sm64MarioCaps_GetActiveIndex(void);         // active slot index, or -1
u8  Sm64MarioCaps_IsFireActive(void);           // true while the Fire cap (D-Up) is active

// Fire Flower: launch a bouncing fireball forward on a fresh B press (Fire cap
// only). The ball arcs with gravity, bounces off floors, and deals fire damage.
void Sm64Mario_FireballOnBPress(PlayState* play, Player* player);

// Advance every in-flight Fire Flower fireball one frame (gravity, bounce, fire
// collider, flame VFX, despawn). Call unconditionally each frame from
// Sm64Mario_Update so balls finish even after the Fire cap toggles off.
void Sm64Mario_UpdateFireballs(PlayState* play);

// Free all in-flight fireballs + their colliders. Called on detransform / scene
// change / suspend (Sm64MarioCaps_OnSuspend) so fire colliders never leak.
void Sm64Mario_KillAllFireballs(void);

// Boss super-damage hooks: a Fire Flower fireball in flight is treated as an
// active super attack by boss_super_damage so the fire can break/kill bosses.
u8 Sm64Mario_FireballActive(void);                 // any fireball in flight
u8 Sm64Mario_FireballNear(Vec3f* pos, f32 range);  // a fireball within range of pos

// Draw one camera-facing flame billboard per in-flight fireball at its absolute
// world position (independent of Mario's facing). Call from Sm64Mario_Draw.
void Sm64Mario_DrawFireballs(PlayState* play);

// Cappy (Odyssey thrown cap) — throw it (C-Left) in one of four variants,
// advance the projectile (out/hover/orbit/return + bounce detection + stun
// collider) each frame, draw it, and free it on suspend. `homing` locks the
// flight onto the nearest enemy (ignored for SPIN).
#define SM64_CAPPY_FWD   0  // forward
#define SM64_CAPPY_DIVE  2  // fast down-forward (air throw)
#define SM64_CAPPY_SPIN  3  // orbits Mario (wide hit)
void Sm64Cappy_Throw(PlayState* play, s32 mode, u8 homing);
void Sm64Cappy_Update(PlayState* play);
void Sm64Cappy_Draw(PlayState* play);
void Sm64Cappy_Kill(void);

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
