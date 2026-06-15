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
    MM_PLAYER_FORM_PIKACHU = 5,
    MM_PLAYER_FORM_GARO = 6,
    MM_PLAYER_FORM_GERUDO = 7,
    MM_PLAYER_FORM_MAX = 8
} MmPlayerTransformation;

// OOT mask type enum (for transformation mask identification)
typedef enum TransformMaskId {
    TRANSFORM_MASK_NONE = 0,
    TRANSFORM_MASK_GORON,
    TRANSFORM_MASK_ZORA,
    TRANSFORM_MASK_DEKU,
    TRANSFORM_MASK_FIERCE_DEITY,
    TRANSFORM_MASK_KEATON,
    TRANSFORM_MASK_GARO,
    TRANSFORM_MASK_GERUDO
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

// Redirect OOT voice SFX to MM equivalent voice for current form.
// Called from Player_PlayVoiceSfx when transformed, instead of suppressing.
void TransformMasks_PlayMmVoice(u16 ootVoiceSfxId, Vec3f* pos);

// Redirect OOT step/walk SFX to MM form-specific sample (Deku/Zora/Goron).
// Returns 1 if the MM SFX was played and the OOT step should be skipped;
// returns 0 for FD/Garo/Gerudo/Human so OOT handles it normally. Called from
// Player_PlaySteppingSfx and Player_PlayFloorSfxByAge in z_player.c.
u8 TransformMasks_TryPlayMmStepSfx(u16 ootStepSfxId, Vec3f* pos);

// FD skin mode: returns true when FD is active (OOT handles gameplay, only DLs swapped)
u8 TransformMasks_IsFDSkinMode(void);

// Returns true if ANY form is active (including FD skin mode)
u8 TransformMasks_IsTransformedAny(void);

// Dragon Scale: Zora swim for non-Zora forms (Adult Link only)
u8 TransformMasks_IsZoraSwimEnabled(void);
void TransformMasks_SetZoraSwimEnabled(u8 enabled);

// Load a DL from mm.o2r with hash pre-resolution (safe for drawing)
void* TransformMasks_LoadMmDL(const char* path);
u8 TransformMasks_DragonScaleEnterSwim(void* play, void* player);
void TransformMasks_DragonScaleSwimUpdate(void* play, void* player);
void TransformMasks_DragonScaleExitSwim(void* player);

// Item restriction: returns true if item is allowed for current form
u8 TransformMasks_IsItemAllowed(s32 item);

// Slot restriction: returns true if inventory slot (0-71) is allowed for current form
u8 TransformMasks_IsSlotAllowed(u8 slot);

// Per-form C-button item use interception (called in z_player.c before Player_UseItem).
// If the current form has a handler for this item, calls it and returns 1 (skip Player_UseItem).
// If the current form is active but has NO handler for the item, also returns 1 (block use).
// Returns 0 when not transformed (fall through to normal Player_UseItem).
u8 TransformMasks_HandleFormItemUse(PlayState* play, Player* player, s32 item);

TransformMaskId TransformMasks_GetMaskType(s32 item);
void TransformMasks_HandleMaskUse(PlayState* play, Player* player, s32 item);

// Dev: trigger a transformation directly without a mask item. Toggles between
// the requested form and Human if already in that form. Currently used for Garo
// (no Garo Mask item exists yet). Skip-cutscene unconditional.
void MmForm_DevTransformTo(PlayState* play, Player* player, MmPlayerTransformation form);

void TransformMasks_Init(PlayState* play, Player* player);
void TransformMasks_Update(PlayState* play, Player* player);
void TransformMasks_Draw(PlayState* play, Player* player);

// Strip BTN_B from a Player_Update input copy before Player_UpdateCommon runs.
// Centralizes the cases where B is reserved by a custom system (currently:
// Blast Mask + Great Fairy Mask reactions, Garo attack kit). Called from
// z_player.c right after the input copy.
void TransformMasks_FilterB(Input* input);

// Reset transformation state (call on scene transition, death, etc.)
void TransformMasks_Reset(void);

// Called on player death — deactivates Chateau Romani infinite magic.
void TransformMasks_OnDeath(void);

// =============================================================================
// Garo form hooks (defined in garo_form.cpp). Called from transformation_masks.c
// (OnDeath spawns 9 flame particles BEFORE MmForm_OnDeath rolls back the form)
// and from z_player.c revival sites (OnReset clears death-once flags).
// =============================================================================
void GaroForm_OnDeath(Player* player, PlayState* play);
void GaroForm_OnReset(void);

// Glass-cannon damage multiplier for Garo form (1.5x incoming). Returns 1.0f for
// non-Garo forms. Applied inside z_player.c func_80837B18_modified.
f32 MmForm_GetIncomingDamageMult(void);

// MM player voice action codes (subset). Confirmed against mm_decomp
// sPlayerVoiceSfxOffsets at voicebank_table.h (NA_SE_VO_LI_*).
//   ATTACK : NA_SE_VO_LI_SWORD_N   = 0x6800 + 0x00 (sword swing grunt)
//   DAMAGE : NA_SE_VO_LI_DAMAGE_S  = 0x6800 + 0x05 (damage-taken cry)
//   DEATH  : NA_SE_VO_LI_DOWN      = 0x6800 + 0x0B (knockdown / death)
// The base MM voice bank has no dedicated laugh/taunt slot — Garo laugh
// uses an OOT fallback SFX (NA_SE_VO_SK_LAUGH, Skull Kid taunt).
#define VOICE_ACTION_ATTACK 0x00
#define VOICE_ACTION_DAMAGE 0x05
#define VOICE_ACTION_DEATH  0x0B

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

// OOT floor type (sFloorType from z_player.c).
// 2=hot room floor, 3=lava floor, 4=sand, 5=slippery, 7=water lilies, 9=void, 12=deep sand.
s32 TransformMasks_GetFloorType(void);

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

// Get pre-loaded FD sword beam DL for rendering (per-frame safe copy from mm.o2r)
// Returns NULL if not loaded. Caller uses with gSPDisplayList on POLY_XLU_DISP.
Gfx* TransformMasks_GetFDSwordBeamDL(PlayState* play);

// Zora fin DLs for boomerang visual override (set by mm_player_form.cpp, read by z_en_boom.c)
// NULL when Zora assets not loaded. EnBoom with params==1 uses L, params==2 uses R.
extern Gfx* gZoraFinBoomerangLDL;
extern Gfx* gZoraFinBoomerangRDL;

// FD melee weapon quad registration (sword damage). Called from MmForm_PostLimbDraw.
// Defined in z_player_lib.c since it accesses static variables (D_80126080, etc.)
void Player_FDMeleeWeaponPostLimb(PlayState* play, Player* player);

// =============================================================================
// Network Visual State Accessors (for Harpoon multiplayer sync)
// =============================================================================

// Model type for network: 0=Link, 1=Goron, 2=Zora, 3=Deku, 4=FD
u8 TransformMasks_GetModelType(void);

// MM stateFlags3 (spike mode, roll active, etc.)
u32 TransformMasks_GetMmStateFlags3(void);

// MM horizontal speed
f32 TransformMasks_GetMmSpeedXZ(void);

// MM form skeleton joint table (NULL if not transformed or skeleton not loaded)
Vec3s* TransformMasks_GetFormJointTable(void);

// Number of valid joints in the form joint table (0 if not transformed)
s32 TransformMasks_GetFormJointCount(void);

// Current action ID (GoronActionId enum in mm_player_form.cpp)
s32 TransformMasks_GetGoronAction(void);

// Eye blink index (0=open, 1=half, 2=closed)
u8 TransformMasks_GetEyeIndex(void);

// Goron ball squash/stretch deformation factor
f32 TransformMasks_GetRollSquash(void);

// Goron spike mode counter (0=off, >0=active)
s16 TransformMasks_GetRollSpikeActive(void);

// Goron charge level counter
s16 TransformMasks_GetRollChargeLevel(void);

// Returns the item/model scale multiplier for the current form.
// FD = 1.5f (actor.scale 0.015f vs standard 0.01f), all others = 1.0f.
// Custom item draw functions should multiply their model scale by this value
// so items appear proportional to the current form's body size.
f32 TransformMasks_GetItemScale(void);

#ifdef __cplusplus
}
#endif

#endif // TRANSFORMATION_MASKS_H
