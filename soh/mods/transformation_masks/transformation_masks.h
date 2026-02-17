/**
 * transformation_masks.h - MM Transformation Masks for OOT
 *
 * Uses MmPlayer struct with hook system for OOT integration.
 * MmPlayer_InitFromOot() copies OOT Player -> MmPlayer
 * MmPlayer_Update() runs REAL MM code on MmPlayer
 * MmPlayer_SyncToOot() copies MmPlayer -> OOT Player
 */

#ifndef TRANSFORMATION_MASKS_H
#define TRANSFORMATION_MASKS_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// MM Player Form Enum (from 2Ship z64player.h)
// =============================================================================

typedef enum MmPlayerTransformation {
    MM_PLAYER_FORM_FIERCE_DEITY = 0,
    MM_PLAYER_FORM_GORON = 1,
    MM_PLAYER_FORM_ZORA = 2,
    MM_PLAYER_FORM_DEKU = 3,
    MM_PLAYER_FORM_HUMAN = 4,
    MM_PLAYER_FORM_MAX = 5
} MmPlayerTransformation;

// OOT mask type enum (for transformation mask identification)
typedef enum TransformMaskId {
    TRANSFORM_MASK_NONE = 0,
    TRANSFORM_MASK_GORON,
    TRANSFORM_MASK_ZORA,
    TRANSFORM_MASK_DEKU,
    TRANSFORM_MASK_FIERCE_DEITY
} TransformMaskId;

// =============================================================================
// MmPlayer Struct - Minimal version for hook system
// Full struct is in soh/mods/mm_sources/z64player.h
// =============================================================================

// Forward declare the full struct (defined in mm_sources/z64player.h)
struct MmPlayer;

// Simplified MmPlayer for the hook system - contains only fields we need to sync
typedef struct MmPlayerCore {
    // === Actor base (synced from OOT Actor) ===
    Vec3f worldPos;
    Vec3f prevPos;
    Vec3s shapeRot; // shape.rot in MM
    f32 scale;

    // === Movement (key differences from OOT) ===
    f32 speedXZ;   // MM: speedXZ, OOT: linearVelocity
    f32 ySpeed;    // Vertical velocity
    s16 yaw;       // Current facing
    s16 targetYaw; // Target facing

    // === State flags ===
    u32 stateFlags1;
    u32 stateFlags2;
    u32 stateFlags3; // MM has u32, OOT has u8 - CRITICAL DIFFERENCE

    // === Transformation system (MM-only) ===
    MmPlayerTransformation transformation;     // Current form
    MmPlayerTransformation prevTransformation; // Previous form (for cutscene)
    s16 transformationTimer;                   // Cutscene timer

    // === Form-specific fields ===
    union {
        struct {
            s16 actionVar1; // av1.actionVar1 - for Goron roll charge
            s16 actionVar2; // av2.actionVar2
        } av;
        struct {
            f32 rollSpeed;  // Goron roll speed
            u8 rollState;   // Goron roll state
            u8 spikeActive; // Spikes out?
        } goron;
    };

    // === Input (synced each frame) ===
    f32 controlStickMagnitude;
    s16 controlStickAngle;

    // === Collision ===
    f32 wallHeight;
    f32 ceilingHeight;
    f32 wallRadius;

    // === Health/Magic ===
    s8 health;
    s8 magic;

    // === Animation state (simplified) ===
    s32 skelAnimeFrameCount;
    f32 skelAnimeCurFrame;

} MmPlayerCore; // Minimal version

// Global MmPlayer instance
extern MmPlayerCore gMmPlayer;

// =============================================================================
// Hook System Functions
// =============================================================================

/**
 * Initialize MmPlayer from OOT Player state
 * Copies all relevant fields from OOT Player to MmPlayer
 * Call once when transformation starts
 */
void MmPlayer_InitFromOot(MmPlayerCore* mm, Player* ootPlayer, PlayState* play);

/**
 * Sync MmPlayer state back to OOT Player
 * Copies position, velocity, state flags back to OOT
 * Call after MmPlayer_Update each frame
 */
void MmPlayer_SyncToOot(MmPlayerCore* mm, Player* ootPlayer, PlayState* play);

/**
 * Update MmPlayer input from OOT input
 * Call each frame before MmPlayer_Update
 */
void MmPlayer_SyncInput(MmPlayerCore* mm, Player* ootPlayer, PlayState* play);

/**
 * Main MmPlayer update - runs MM action logic
 * Uses the synced MmPlayerCore state
 */
void MmPlayer_Update(MmPlayerCore* mm, PlayState* play);

// =============================================================================
// Transformation State
// =============================================================================

/**
 * Check if currently in MM transformation mode
 */
u8 MmPlayer_IsTransformed(void);

/**
 * Get current MM form
 */
MmPlayerTransformation MmPlayer_GetForm(void);

/**
 * Start transformation to a new form
 * @param targetForm The form to transform into
 * @param skipCutscene If true, skip the transformation cutscene
 */
void MmPlayer_StartTransformation(PlayState* play, MmPlayerTransformation targetForm, u8 skipCutscene);

// No-op action function (C linkage, replaces OOT actionFunc while transformed)
void MmForm_OotNoopAction(Player* thisx, PlayState* play);

// =============================================================================
// Pending Damage System
//
// OOT's func_808382DC in Player_UpdateCommon handles damage (AC_HIT) BEFORE
// TransformMasks_Update runs. Then Collider_ResetCylinderAC clears the AC_HIT
// flag. To let the MM form system handle its own damage:
//   1. func_808382DC saves hit info here and skips OOT processing when transformed
//   2. MmForm_CheckDamage reads this instead of checking AC_HIT directly
// =============================================================================
typedef struct {
    u8 hasPending;   // 1 if damage detected this frame, 0 otherwise
    s32 damage;      // actor.colChkInfo.damage
    u8 acHitEffect;  // actor.colChkInfo.acHitEffect
    Actor* attacker; // cylinder.base.ac (may be NULL)
} MmFormPendingDamage;

extern MmFormPendingDamage gMmFormPendingDamage;

// Core state queries
u8 TransformMasks_IsEnabled(void);
u8 TransformMasks_IsTransformed(void);
u8 TransformMasks_HasSkeleton(void);

// FD skin mode: returns true when FD is active (OOT handles gameplay, only DLs swapped)
u8 TransformMasks_IsFDSkinMode(void);

// Returns true if ANY form is active (including FD skin mode)
u8 TransformMasks_IsTransformedAny(void);

// Item restriction: returns true if item is allowed for current form
u8 TransformMasks_IsItemAllowed(s32 item);

TransformMaskId TransformMasks_GetMaskType(s32 item);
void TransformMasks_HandleMaskUse(PlayState* play, Player* player, s32 item);
void TransformMasks_Init(PlayState* play, Player* player);
void TransformMasks_Update(PlayState* play, Player* player);
void TransformMasks_Draw(PlayState* play, Player* player);

// Reset transformation state (call on scene transition, death, etc.)
void TransformMasks_Reset(void);

// Water entry: called when player enters deep water (swim depth).
// Returns 1 if swimming was blocked (Goron/Deku can't swim), 0 if allowed (Zora/FD).
u8 TransformMasks_OnWaterSwimAttempt(PlayState* play, Player* player);

// Camera height for current form (from MM Player_GetHeight). Returns 0 if not transformed.
f32 TransformMasks_GetFormHeight(void);

// Returns 1 if current form blocks ledge grab (only Goron). Returns 0 otherwise.
u8 TransformMasks_BlocksLedgeGrab(void);

// OOT processed control stick magnitude (0-60, normalized to circle).
// Defined in transformation_masks.c which is compiled inside z_player.c and sees the static.
f32 TransformMasks_GetStickMagnitude(void);

// =============================================================================
// MM Mask Wearing (non-transformation masks drawn on Link's head)
// =============================================================================

// Toggle wearing an MM mask. Transformation masks are handled separately.
void TransformMasks_WearToggle(PlayState* play, Player* player, s32 itemId);

// Draw the currently worn MM mask (call from PostLimbDraw for HEAD limb).
void TransformMasks_WearDraw(PlayState* play, Player* player);

// Per-mask effect update (call each frame).
void TransformMasks_WearUpdate(PlayState* play, Player* player);

// Get current worn MM mask item ID (ITEM_NONE if none).
s32 TransformMasks_WearGetCurrent(void);

// Clear worn MM mask (scene transition, death, etc.).
void TransformMasks_WearClear(void);

#ifdef __cplusplus
}
#endif

#endif // TRANSFORMATION_MASKS_H
