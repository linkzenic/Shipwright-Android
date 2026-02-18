/**
 * transformation_masks.c - MM Transformation Masks Router
 *
 * Routes calls from z_player.c hooks to mm_player_form.cpp implementation.
 * Asset replacement getters remain here (they work independently).
 */

#include "mods/transformation_masks/transformation_masks.h"
#include "mods/transformation_masks/assets/mm_asset_loader.h"

// =============================================================================
// Forward declarations to mm_player_form.cpp (compiled separately as .cpp)
// =============================================================================

extern void MmForm_Init(PlayState* play, Player* player);
extern u8 MmForm_IsEnabled(void);
extern u8 MmForm_IsTransformed(void);
extern u8 MmForm_IsTransformedAny(void);
extern u8 MmForm_HasSkeleton(void);
extern u8 MmForm_IsFDSkinMode(void);
extern u8 MmForm_IsItemAllowed(s32 item);
extern u8 MmForm_IsSlotAllowed(u8 slot);
extern MmPlayerTransformation MmForm_GetCurrentForm(void);
extern u8 MmForm_OnWaterSwimAttempt(PlayState* play, Player* player);
extern TransformMaskId MmForm_GetMaskType(s32 item);
extern void MmForm_HandleMaskUse(PlayState* play, Player* player, s32 item);
extern void MmForm_Update(PlayState* play, Player* player);
extern void MmForm_Draw(PlayState* play, Player* player);
extern void MmForm_Reset(void);
extern f32 MmForm_GetCameraHeight(void);
extern u8 MmForm_BlocksLedgeGrab(void);

// mm_mask_wear.cpp
extern void MmMaskWear_Toggle(PlayState* play, Player* player, s32 itemId);
extern void MmMaskWear_Draw(PlayState* play, Player* player);
extern void MmMaskWear_Update(PlayState* play, Player* player);
extern s32 MmMaskWear_GetCurrent(void);
extern void MmMaskWear_Clear(void);

// =============================================================================
// No-op Action Function (replaces OOT actionFunc while transformed)
//
// OOT's Player_UpdateCommon (z_player.c ~line 11994) runs BEFORE actionFunc:
//   - invincibility timer, Player_UpdateInterface, Player_UpdateZTargeting
//   - Player_ProcessControlStick (populates prevControlStickMagnitude/Angle)
//   - Collision/gravity (Actor_MoveXZGravity, Player_ProcessSceneCollision)
// Then calls: this->actionFunc(this, play)  <-- lands here
// After: Player_UpdateCamAndSeqModes, Collider_UpdateCylinder, etc.
//
// We intentionally do nothing here. All gameplay comes from MmForm_Update.
// =============================================================================

void MmForm_OotNoopAction(Player* thisx, PlayState* play) {
    // Empty: OOT actions disabled while MM form is active.
}

// =============================================================================
// OOT Stick Magnitude Accessor
// sControlStickMagnitude is static to z_player.c; this file is #included there.
// This forward declaration is valid: multiple static declarations at file scope
// in the same TU refer to the same object (C11 6.9.2). The real definition with
// initializer is at z_player.c line 570, compiled later in the same TU.
// =============================================================================

static f32 sControlStickMagnitude;

f32 TransformMasks_GetStickMagnitude(void) {
    return sControlStickMagnitude;
}

// =============================================================================
// Routing to mm_player_form.cpp
// =============================================================================

u8 TransformMasks_IsEnabled(void) {
    return MmForm_IsEnabled();
}

u8 TransformMasks_IsTransformed(void) {
    return MmForm_IsTransformed();
}

u8 TransformMasks_HasSkeleton(void) {
    return MmForm_HasSkeleton();
}

TransformMaskId TransformMasks_GetMaskType(s32 item) {
    return MmForm_GetMaskType(item);
}

void TransformMasks_HandleMaskUse(PlayState* play, Player* player, s32 item) {
    MmMaskWear_Clear(); // Clear worn mask before transformation
    MmForm_HandleMaskUse(play, player, item);
}

void TransformMasks_Init(PlayState* play, Player* player) {
    MmForm_Init(play, player);
}

void TransformMasks_Update(PlayState* play, Player* player) {
    MmForm_Update(play, player);
}

void TransformMasks_Draw(PlayState* play, Player* player) {
    MmForm_Draw(play, player);
}

void TransformMasks_Reset(void) {
    MmForm_Reset();
    MmMaskWear_Clear();
}

u8 TransformMasks_IsFDSkinMode(void) {
    return MmForm_IsFDSkinMode();
}

u8 TransformMasks_IsTransformedAny(void) {
    return MmForm_IsTransformedAny();
}

u8 TransformMasks_IsItemAllowed(s32 item) {
    return MmForm_IsItemAllowed(item);
}

u8 TransformMasks_IsSlotAllowed(u8 slot) {
    return MmForm_IsSlotAllowed(slot);
}

MmPlayerTransformation MmPlayer_GetForm(void) {
    return MmForm_GetCurrentForm();
}

u8 TransformMasks_OnWaterSwimAttempt(PlayState* play, Player* player) {
    return MmForm_OnWaterSwimAttempt(play, player);
}

f32 TransformMasks_GetFormHeight(void) {
    return MmForm_GetCameraHeight();
}

u8 TransformMasks_BlocksLedgeGrab(void) {
    return MmForm_BlocksLedgeGrab();
}

// =============================================================================
// MM Mask Wearing Routing (to mm_mask_wear.cpp)
// =============================================================================

void TransformMasks_WearToggle(PlayState* play, Player* player, s32 itemId) {
    MmMaskWear_Toggle(play, player, itemId);
}

void TransformMasks_WearDraw(PlayState* play, Player* player) {
    MmMaskWear_Draw(play, player);
}

void TransformMasks_WearUpdate(PlayState* play, Player* player) {
    MmMaskWear_Update(play, player);
}

s32 TransformMasks_WearGetCurrent(void) {
    return MmMaskWear_GetCurrent();
}

void TransformMasks_WearClear(void) {
    MmMaskWear_Clear();
}

// Route to MmForm_GetFDHandDL for sword beam (FD_DL_SWORD_BEAM = 4)
extern Gfx* MmForm_GetFDSwordBeamDL(PlayState* play);
Gfx* TransformMasks_GetFDSwordBeamDL(PlayState* play) {
    return MmForm_GetFDSwordBeamDL(play);
}
