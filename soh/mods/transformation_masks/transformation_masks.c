/**
 * transformation_masks.c - MM Transformation Masks Router
 *
 * Routes calls from z_player.c hooks to mm_player_form.cpp implementation.
 * Asset replacement getters remain here (they work independently).
 */

#include "mods/transformation_masks/transformation_masks.h"
#include "mods/transformation_masks/assets/mm_asset_loader.h"

// =============================================================================
// Global MmPlayer instance (declared extern in transformation_masks.h)
// =============================================================================

MmPlayerCore gMmPlayer;

// Zora fin DLs for boomerang visual override (set by mm_player_form.cpp on load/unload)
Gfx* gZoraFinBoomerangLDL = NULL;
Gfx* gZoraFinBoomerangRDL = NULL;

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
extern u8 MmForm_IsZoraSwimEnabled(void);
extern void MmForm_SetZoraSwimEnabled(u8 enabled);
extern Gfx* MmForm_LoadAndPreResolveMmDL(const char* path);
extern u8 MmForm_DragonScaleEnterSwim(PlayState* play, Player* player);
extern void MmForm_DragonScaleSwimUpdate(PlayState* play, Player* player);
extern void MmForm_DragonScaleExitSwim(Player* player);

// mm_mask_wear.cpp
extern void MmMaskWear_Toggle(PlayState* play, Player* player, s32 itemId);
extern void MmMaskWear_Draw(PlayState* play, Player* player);
extern void MmMaskWear_Update(PlayState* play, Player* player);
extern s32 MmMaskWear_GetCurrent(void);
extern void MmMaskWear_Clear(void);
extern void MmMaskWear_DeactivateChateauRomani(void);

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
// OOT Floor Type Accessor
// sFloorType is static to z_player.c; same forward-declaration pattern as above.
// Real definition at z_player.c line 574, compiled later in the same TU.
// =============================================================================

static s32 sFloorType;

s32 TransformMasks_GetFloorType(void) {
    return sFloorType;
}

// =============================================================================
// OOT sControlInput accessor (for mask button scanning while transformed).
// Same forward-declaration pattern as sFloorType above.
// Real definition at z_player.c line 439, set at line 12137.
// =============================================================================

static Input* sControlInput;

// =============================================================================
// Routing to mm_player_form.cpp
// =============================================================================

u8 TransformMasks_IsEnabled(void) {
    return MmForm_IsEnabled();
}

u8 TransformMasks_IsTransformed(void) {
    return MmForm_IsTransformed();
}

// Redirect OOT voice SFX to MM equivalent for current form.
// OOT voice base = 0x6800 (NA_SE_VO_LI_SWORD_N).
// MM voiceSfxIdOffset per form (from 2Ship z_player.c sPlayerAgeProperties):
//   FD=0x00, Human=0x20, Deku=0x80, Zora=0xA0, Goron=0xC0
void TransformMasks_PlayMmVoice(u16 ootVoiceSfxId, Vec3f* pos) {
    if (!MmSfx_IsAvailable())
        return;

    // Compute action index: ootVoiceSfxId is the BASE sfxId (before OOT age offset)
    // e.g., NA_SE_VO_LI_DAMAGE_S = 0x6805, action = 5
    u16 action = ootVoiceSfxId - 0x6800;
    if (action >= 0x20)
        return; // Out of range

    // Get MM voice offset for current form
    u16 mmOffset;
    MmPlayerTransformation form = MmForm_GetCurrentForm();
    switch (form) {
        case MM_PLAYER_FORM_GORON:
            mmOffset = 0xC0;
            break;
        case MM_PLAYER_FORM_ZORA:
            mmOffset = 0xA0;
            break;
        case MM_PLAYER_FORM_DEKU:
            mmOffset = 0x80;
            break;
        case MM_PLAYER_FORM_FIERCE_DEITY:
            mmOffset = 0x00;
            break;
        default:
            return;
    }

    u16 mmSfxId = 0x6800 + mmOffset + action;
    MmSfx_PlayAtPos(mmSfxId, pos);
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
    // Scan C-button/D-pad for transformation mask presses.
    // Two cases where OOT's item pipeline does NOT reach Player_UseItem:
    //   1. Transformed: actionFunc = MmForm_OotNoopAction → pipeline bypassed entirely.
    //   2. Swimming (IN_WATER): surface swim actions (D610/D84C/DAB4) don't call
    //      Player_UpdateUpperBody → pipeline never runs. Only Zora mask allowed in water.
    u8 isNoop = (player->actionFunc == MmForm_OotNoopAction);
    u8 isPikachu = (MmForm_GetCurrentForm() == MM_PLAYER_FORM_PIKACHU && MmForm_IsTransformedAny());
    u8 isInWater = (player->stateFlags1 & PLAYER_STATE1_IN_WATER) != 0;

    if ((isNoop || isPikachu || isInWater) && sControlInput != NULL) {
        static const u16 sBtns[] = { BTN_CLEFT, BTN_CDOWN, BTN_CRIGHT };
        for (s32 i = 0; i < 3; i++) {
            if (CHECK_BTN_ALL(sControlInput->press.button, sBtns[i])) {
                s32 item = C_BTN_ITEM(i);
                if (item != ITEM_NONE && MmForm_GetMaskType(item) != TRANSFORM_MASK_NONE) {
                    // In water (not transformed): only Zora mask allowed
                    if (!isNoop && MmForm_GetMaskType(item) != TRANSFORM_MASK_ZORA)
                        break;
                    TransformMasks_HandleMaskUse(play, player, item);
                    break;
                }
            }
        }
        if (CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0) != 0) {
            static const u16 sDpad[] = { BTN_DUP, BTN_DDOWN, BTN_DLEFT, BTN_DRIGHT };
            for (s32 i = 0; i < 4; i++) {
                if (CHECK_BTN_ALL(sControlInput->press.button, sDpad[i])) {
                    s32 item = DPAD_ITEM(i);
                    if (item != ITEM_NONE && MmForm_GetMaskType(item) != TRANSFORM_MASK_NONE) {
                        if (!isNoop && MmForm_GetMaskType(item) != TRANSFORM_MASK_ZORA)
                            break;
                        TransformMasks_HandleMaskUse(play, player, item);
                        break;
                    }
                }
            }
        }
    }

    MmForm_Update(play, player);
}

void TransformMasks_Draw(PlayState* play, Player* player) {
    MmForm_Draw(play, player);
}

void TransformMasks_Reset(void) {
    MmForm_Reset();
    MmMaskWear_Clear();
}

void TransformMasks_OnDeath(void) {
    MmMaskWear_DeactivateChateauRomani();
}

u8 TransformMasks_IsFDSkinMode(void) {
    return MmForm_IsFDSkinMode();
}

u8 TransformMasks_IsTransformedAny(void) {
    return MmForm_IsTransformedAny();
}

u8 TransformMasks_IsZoraSwimEnabled(void) {
    return MmForm_IsZoraSwimEnabled();
}

void TransformMasks_SetZoraSwimEnabled(u8 enabled) {
    MmForm_SetZoraSwimEnabled(enabled);
}

void* TransformMasks_LoadMmDL(const char* path) {
    return (void*)MmForm_LoadAndPreResolveMmDL(path);
}

u8 TransformMasks_DragonScaleEnterSwim(void* play, void* player) {
    return MmForm_DragonScaleEnterSwim((PlayState*)play, (Player*)player);
}

void TransformMasks_DragonScaleSwimUpdate(void* play, void* player) {
    MmForm_DragonScaleSwimUpdate((PlayState*)play, (Player*)player);
}

void TransformMasks_DragonScaleExitSwim(void* player) {
    MmForm_DragonScaleExitSwim((Player*)player);
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
    // Only draw worn MM mask for the real local player.
    // Remote/dummy Player actors share the same PostLimbDraw path but must not
    // render the local player's worn mask on their head.
    if (player != GET_PLAYER(play))
        return;
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

// =============================================================================
// Raw MmPlayer Accessors (gMmPlayer lives in this TU via z_player.c includes)
// mm_player_form.cpp is a separate TU and cannot access gMmPlayer directly.
// =============================================================================

u32 MmPlayerRaw_GetStateFlags3(void) {
    return gMmPlayer.stateFlags3;
}
f32 MmPlayerRaw_GetSpeedXZ(void) {
    return gMmPlayer.speedXZ;
}

// =============================================================================
// Network Visual State Routing (to mm_player_form.cpp)
// =============================================================================

extern u8 MmForm_GetModelType(void);
extern u32 MmForm_GetStateFlags3(void);
extern f32 MmForm_GetSpeedXZ(void);
extern Vec3s* MmForm_GetJointTable(void);
extern s32 MmForm_GetJointCount(void);
extern s32 MmForm_GetGoronAction(void);
extern u8 MmForm_GetEyeIndex(void);
extern f32 MmForm_GetRollSquash(void);
extern s16 MmForm_GetRollSpikeActive(void);
extern s16 MmForm_GetRollChargeLevel(void);

u8 TransformMasks_GetModelType(void) {
    return MmForm_GetModelType();
}
u32 TransformMasks_GetMmStateFlags3(void) {
    return MmForm_GetStateFlags3();
}
f32 TransformMasks_GetMmSpeedXZ(void) {
    return MmForm_GetSpeedXZ();
}
Vec3s* TransformMasks_GetFormJointTable(void) {
    return MmForm_GetJointTable();
}
s32 TransformMasks_GetFormJointCount(void) {
    return MmForm_GetJointCount();
}
s32 TransformMasks_GetGoronAction(void) {
    return MmForm_GetGoronAction();
}
u8 TransformMasks_GetEyeIndex(void) {
    return MmForm_GetEyeIndex();
}
f32 TransformMasks_GetRollSquash(void) {
    return MmForm_GetRollSquash();
}
s16 TransformMasks_GetRollSpikeActive(void) {
    return MmForm_GetRollSpikeActive();
}
s16 TransformMasks_GetRollChargeLevel(void) {
    return MmForm_GetRollChargeLevel();
}

// Route to MmForm_GetFDHandDL for sword beam (FD_DL_SWORD_BEAM = 4)
extern Gfx* MmForm_GetFDSwordBeamDL(PlayState* play);
Gfx* TransformMasks_GetFDSwordBeamDL(PlayState* play) {
    return MmForm_GetFDSwordBeamDL(play);
}

f32 TransformMasks_GetItemScale(void) {
    if (TransformMasks_IsFDSkinMode()) {
        return 1.5f; // FD actor.scale = 0.015f vs standard 0.01f
    }
    return 1.0f;
}
