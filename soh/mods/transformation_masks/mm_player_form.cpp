/**
 * mm_player_form.cpp - MM Transformation Masks Form System
 *
 * Central hook connecting OOT z_player with MM transformation behavior.
 * Compiled separately as .cpp (CMakeLists picks up mods/*.cpp).
 * All public functions wrapped in extern "C" for C interop.
 *
 * Architecture:
 *   State machine manages transformation lifecycle.
 *   Separate SkelAnime for MM form (NOT replacing player->skelAnime).
 *   MM movement system overrides OOT velocity/yaw each frame.
 *   Draw override renders MM skeleton instead of OOT Link.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <exception>

// C headers - NOT wrapped in extern "C" because they already have their own
// __cplusplus guards internally, and wrapping them breaks Clang/GCC when
// C++ standard headers (like <memory>) get transitively included.
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "mods/transformation_masks/transformation_masks.h"
#include "mods/transformation_masks/assets/mm_asset_loader.h"
#include "mods/extended_inventory.h"
#include "mods/anim_translator/mm_anim_loader.h"
#include "mods/sound_translator/mm_sfx_ids.h"
#include "mods/mm_sources/objects/object_link_goron.h"
#include "mods/mm_sources/objects/object_link_zora.h"
#include "mods/mm_sources/objects/object_link_nuts.h"
#include "overlays/actors/ovl_En_Boom/z_en_boom.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include "mods/items/helpers/camera_helper.h"
#include "mods/items/helpers/equip_helper.h"

// Static helpers (all functions are static, no linkage issue)
#include "mods/transformation_masks/mm_form_combat.c"

#include "soh/OTRGlobals.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/frame_interpolation.h"

#include <libultraship/bridge.h>
#include <libultraship/log/luslog.h>
#include <ship/Context.h>
#include <ship/resource/Resource.h>
#include <ship/resource/ResourceManager.h>
#include <fast/resource/type/DisplayList.h>

#define MMFORM_LOG(fmt, ...) lusprintf(__FILE__, __LINE__, LUSLOG_LEVEL_INFO, fmt, ##__VA_ARGS__)

// Temporarily disable MM sounds. Set to 1 to disable all MM audio.
#define MM_SOUNDS_DISABLED 0

// =============================================================================
// Gameplay Constants (used throughout the file)
// =============================================================================

// bgCheckFlags bit 0 = on ground (OOT has no named macro for this)
#define MMFORM_ON_GROUND(player) ((player)->actor.bgCheckFlags & 1)

// Jump parameters (from OOT REG(19)/100 = 500/100 = 5.0)
#define MMFORM_JUMP_VELOCITY 5.0f

// Sidehop parameters (from 2Ship func_80839860 line 8162) - same for ALL forms
#define MMFORM_SIDEHOP_VEL_Y 3.5f
#define MMFORM_SIDEHOP_SPEED 8.5f

// Backflip parameters (from 2Ship func_80839860 line 8162) - same for ALL forms
#define MMFORM_BACKFLIP_VEL_Y 5.8f
#define MMFORM_BACKFLIP_SPEED 6.0f

// Gravity states for MmForm_GetGravity()
typedef enum MmFormGravityState {
    MMFORM_GRAVITY_NORMAL,    // Standard airborne: -1.2f (Player_Action_25 line 15165)
    MMFORM_GRAVITY_JUMP_KICK, // Jump kick: Zora=-0.8f, others=-1.2f (Player_Action_29 line 15382)
    MMFORM_GRAVITY_SWIM,      // In water: 0.0f
    MMFORM_GRAVITY_LEDGE,     // Hanging from ledge: 0.0f
    MMFORM_GRAVITY_ROLL_APEX, // Goron ground pound apex hover: -0.2f (Player_Action_96 line 20142)
    MMFORM_GRAVITY_ROLL_SLAM, // Goron ground pound slam: -10.0f (Player_Action_96 line 20145)
} MmFormGravityState;

// Deku flight flag bits (matching MM PLAYER_STATE3_* used in Player_Action_94)
#define DEKU_FLIGHT_RISING 0x200       // STATE3_200: still ascending after launch
#define DEKU_FLIGHT_GOLDEN 0x2000      // STATE3_2000: fully charged (charge >= 10)
#define DEKU_FLIGHT_FROM_SCENE 0x40000 // STATE3_40000: launched from scene floor (not dyna)
#define DEKU_FLIGHT_OPEN 0x1000000     // STATE3_1000000: flower opened, gliding active
#define DEKU_FLIGHT_UNDERGROUND 0x100  // STATE3_100: below -1500 depth (bud visible)

// Deku flight distance limits (from 2Ship D_8085D958[], z_player.c line 19079)
static const f32 sDekuFlightMaxDist[] = { 600.0f, 960.0f };

// Roll speed decay (from 2Ship Player_Action_10 line 14970)
#define MMFORM_ROLL_DECEL 2.0f

// Roll damage frames (from 2Ship sMeleeAttackAnimInfo: frames 8-18)
#define MMFORM_ROLL_HIT_START 8
#define MMFORM_ROLL_HIT_END 18

// Jump kick damage (from 2Ship D_8085D09C: { DMG_ZORA_PUNCH, 1, 2, 0, 0 })
#define MMFORM_JUMP_KICK_DAMAGE 2

// Speed mode for Player_GetMovementSpeedAndYaw (from z_player.c line 3976)
#define SPEED_MODE_LINEAR 0.0f
#define SPEED_MODE_CURVED 0.018f

// Animation speed for sidehop/backflip (from 2Ship z64player.h line 363)
// func_80834D50 uses Player_Anim_PlayOnceAdjusted which plays at 2/3 speed
#define PLAYER_ANIM_ADJUSTED_SPEED (2.0f / 3.0f)

// Flags that block movement input but DON'T trigger a full yield.
// DAMAGED is not in yield flags because the MM form has its own damage handler
// (MmForm_GoronAction_Damage) that manages knockback deceleration.
// This is a safety net for edge cases where Idle/Walk/Run runs with these flags set.
#define MMFORM_BLOCK_MOVEMENT_FLAGS (PLAYER_STATE1_DAMAGED | PLAYER_STATE1_LOADING)

// Water thresholds (from 2Ship/OOT ageProperties for Adult Link)
#define ZORA_SWIM_THRESHOLD 30.0f
#define ZORA_BUOYANCY_DEPTH 44.8f // ageProperties->unk_28 (buoyancy reference depth)
#define ZORA_SURFACE_DEPTH 36.0f  // ageProperties->unk_24 (surface detection)
#define ZORA_DEEP_THRESHOLD 68.0f // ageProperties->unk_30 (deep water / dolphin jump surface)

// Functions from z_player.c needed by form system (non-static, need extern "C" for C++ linkage)
extern "C" {
s32 Player_GetMovementSpeedAndYaw(Player* this_, f32* outSpeedTarget, s16* outYawTarget, f32 speedMode,
                                  PlayState* play);
void Player_PlayJumpingSfx(Player* this_);
void Player_PlayVoiceSfx(Player* this_, u16 sfxId);
}

// =============================================================================
// State Machine
// =============================================================================

typedef enum MmFormStateId {
    MMFORM_STATE_INACTIVE = 0,   // Not transformed
    MMFORM_STATE_TRANSFORMING,   // Playing transformation cutscene (or instant flash)
    MMFORM_STATE_ACTIVE,         // Transformed, running MM movement each frame
    MMFORM_STATE_DETRANSFORMING, // Reverting to human
} MmFormStateId;

// =============================================================================
// Per-Form Properties (from 2Ship sPlayerAgeProperties)
// =============================================================================

typedef struct {
    const char* skelPath;     // OTR path to skeleton in mm.o2r
    s32 limbCount;            // Number of limbs in skeleton
    const char* idleAnimPath; // OTR path to idle animation data
    s16 idleAnimFrames;       // Idle anim frame count (hint, actual from file)
    const char* walkAnimPath; // OTR path to walk animation data
    s16 walkAnimFrames;       // Walk anim frame count (hint)
    const char* runAnimPath;  // OTR path to run animation data
    s16 runAnimFrames;        // Run anim frame count (hint)
    f32 ceilingCheckHeight;
    f32 shadowScale;
    f32 wallCheckRadius;
    u8 mass;
    f32 cylinderRadius; // Collider cylinder radius
    f32 cylinderHeight; // Collider cylinder height
    f32 cylinderYShift; // Collider Y offset
    f32 rootAnimScale;  // ageProperties->unk_08: scales root position (jointTable[0]) during draw
                        // From 2Ship Player_OverrideLimbDrawGameplayCommon (z_player_lib.c:2419)
                        // This makes each form's skeleton sit at the correct height on the ground
    f32 cameraHeight;   // Player_GetHeight return value (EXACT from MM decomp z_actor.c:1374-1400)
                        // Used by camera system for eye height, get-item framing, etc.
} MmFormProperties;

// ALL MM form skeletons use 22 limbs (same as human Link).
// Confirmed: gPlayerAnim_pg_wait_Data = 10586 bytes = 79 frames * 134 bytes (22*3+1=67 s16).
// Forms share human Link walk/run anims via D_8085BE84 table, only idle is form-specific.
#define MM_FORM_LIMB_COUNT 22

// MM movement facts (from 2Ship research):
// - NO per-form speed cap or acceleration difference (all forms use same walk/run system)
// - Only Fierce Deity gets 1.5x speed multiplier in Player_CalcSpeedAndYawFromControlStick
// - Turn rate is 0xFA0 (4000) for all forms (Player_UpdateShapeYaw line 4933)
// - Walk anim rate = speedXZ * 0.3f + 1.0f (line 14648: func_8083EA44)
// - Walk → Run transition at speedTarget > 4.0f (line 14648)
// - All forms share walk/run anims from D_8085BE84 table, only idle is form-specific

static const MmFormProperties sFormProps[MM_PLAYER_FORM_MAX] = {
    // FIERCE_DEITY (index 0)
    // FD uses PLAYER_ANIMTYPE_3 (two-handed weapon) from D_8085BE84:
    //   idle = gPlayerAnim_link_fighter_wait_long (32 frames, from mm_anims_data.c)
    //   walk = gPlayerAnim_link_fighter_walk_long (17 frames)
    //   run  = gPlayerAnim_link_fighter_run_long  (16 frames)
    { "objects/object_link_boy/gLinkFierceDeitySkel", MM_FORM_LIMB_COUNT,
      "misc/link_animetion/gPlayerAnim_link_fighter_wait_long_Data", 32,
      "misc/link_animetion/gPlayerAnim_link_fighter_walk_long_Data", 17,
      "misc/link_animetion/gPlayerAnim_link_fighter_run_long_Data", 16, 84.0f, 90.0f, 27.0f, 100, 24.0f, 68.0f, 0.0f,
      1.5f, 124.0f }, // cameraHeight: MM Player_GetHeight for FD
    // GORON (index 1) - Idle is form-specific, walk/run use shared human anims
    // Shadow: 90.0f matches 2Ship DrawFeet. PostLimbDraw updates feetPos[] so DrawFeet works.
    { "objects/object_link_goron/gLinkGoronSkel", MM_FORM_LIMB_COUNT, "misc/link_animetion/gPlayerAnim_pg_wait_Data",
      79, "misc/link_animetion/gPlayerAnim_link_normal_walk_free_Data", 17,
      "misc/link_animetion/gPlayerAnim_link_normal_run_free_Data", 17, 70.0f, 90.0f, 19.5f, 200, 24.0f, 62.0f, 0.0f,
      0.74f, 80.0f }, // cameraHeight: MM Player_GetHeight for Goron (34 when curled, handled separately)
    // ZORA (index 2)
    { "objects/object_link_zora/gLinkZoraSkel", MM_FORM_LIMB_COUNT, "misc/link_animetion/gPlayerAnim_pz_wait_Data", 80,
      "misc/link_animetion/gPlayerAnim_link_normal_walk_free_Data", 17,
      "misc/link_animetion/gPlayerAnim_link_normal_run_free_Data", 17, 56.0f, 90.0f, 18.0f, 80, 20.0f, 58.0f, 0.0f,
      1.0f, 68.0f }, // cameraHeight: MM Player_GetHeight for Zora
    // DEKU (index 3) - cylinder matches MM default (all forms share radius=12, height=60)
    // From 2Ship z_player.c D_8085C2EC: sCylinderInit = { radius=12, height=60, yShift=0 }
    // Idle: Deku has NO pn_wait animation! Uses human link_normal_wait_free (72 frames)
    // From 2Ship Player_GetIdleAnim (z_player.c line 2773): Deku falls through to D_8085BE84 default
    { "objects/object_link_nuts/gLinkDekuSkel", MM_FORM_LIMB_COUNT,
      "misc/link_animetion/gPlayerAnim_link_normal_wait_free_Data", 72,
      "misc/link_animetion/gPlayerAnim_link_normal_walk_free_Data", 17,
      "misc/link_animetion/gPlayerAnim_link_normal_run_free_Data", 17, 35.0f, 50.0f, 14.0f, 20, 12.0f, 60.0f, 0.0f,
      0.3f, 36.0f }, // cameraHeight: MM Player_GetHeight for Deku
    // HUMAN (index 4) - not used by transformation system
    { NULL, MM_FORM_LIMB_COUNT, NULL, 0, NULL, 0, NULL, 0, 40.0f, 60.0f, 14.0f, 50, 12.0f, 50.0f, 0.0f, 1.0f,
      44.0f }, // cameraHeight: MM Player_GetHeight for Human
};

// =============================================================================
// Slot-Based Item Restriction (72 elements per form)
//
// Each array maps inventory slot (0-71) to allowed (1) or blocked (0).
// Page 1 (0-23): vanilla OOT items, Page 2 (24-47): custom items,
// Page 3 (48-71): MM masks (only transform masks 53/59/65/71 allowed).
// Used by MmForm_IsSlotAllowed() and MmForm_IsItemAllowed().
// =============================================================================
// clang-format off
static const u8 sSlotAllowedFD[72] = {
    // Page 1: STICK NUT  BOMB BOW  FIRE DIN  SLING OCA  BCHU HOOK ICE  FAR  BOOM LENS BEAN HAM  LITE NAY  BTL1 BTL2 BTL3 BTL4 TRD_A TRD_C
                0,   1,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   1,   0,   1,   0,   0,   1,   1,   1,   1,   0,    0,
    // Page 2: ROCS WHIP SPIN BARR FROD DEM  DLEF TGAT BEET SWHO IROD ZPER MOGM GJAR BCHN DSEN LROD HYLS PND2 PND1 PND3 CSOM SHVL DROD
                1,   0,   0,   0,   1,   0,   0,   0,   0,   0,   1,   0,   0,   0,   1,   0,   1,   0,   0,   0,   0,   0,   0,   0,
    // Page 3: POST ANGT BLST STON GFRY DEKU KEAT BREM BUNA DONG SCEN GORN ROMN CIRC KAFE COUP TRTH ZORA KAMA GIBD GARO CAPT GIAN FIER
                0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   1,
};
static const u8 sSlotAllowedGoron[72] = {
    // Page 1: STICK NUT  BOMB BOW  FIRE DIN  SLING OCA  BCHU HOOK ICE  FAR  BOOM LENS BEAN HAM  LITE NAY  BTL1 BTL2 BTL3 BTL4 TRD_A TRD_C
                0,   0,   1,   0,   0,   0,   0,    1,   1,   0,   0,   0,   0,   1,   0,   1,   0,   0,   1,   1,   1,   1,   0,    0,
    // Page 2: ROCS WHIP SPIN BARR FROD DEM  DLEF TGAT BEET SWHO IROD ZPER MOGM GJAR BCHN DSEN LROD HYLS PND2 PND1 PND3 CSOM SHVL DROD
                0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   0,   1,   0,   0,   0,   0,   0,   0,   0,   1,   0,
    // Page 3: POST ANGT BLST STON GFRY DEKU KEAT BREM BUNA DONG SCEN GORN ROMN CIRC KAFE COUP TRTH ZORA KAMA GIBD GARO CAPT GIAN FIER
                0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   1,
};
static const u8 sSlotAllowedZora[72] = {
    // Page 1: STICK NUT  BOMB BOW  FIRE DIN  SLING OCA  BCHU HOOK ICE  FAR  BOOM LENS BEAN HAM  LITE NAY  BTL1 BTL2 BTL3 BTL4 TRD_A TRD_C
                0,   0,   0,   1,   1,   0,   0,    1,   0,   1,   1,   0,   0,   0,   0,   0,   1,   0,   1,   1,   1,   1,   1,    1,
    // Page 2: ROCS WHIP SPIN BARR FROD DEM  DLEF TGAT BEET SWHO IROD ZPER MOGM GJAR BCHN DSEN LROD HYLS PND2 PND1 PND3 CSOM SHVL DROD
                0,   1,   0,   0,   0,   0,   0,   0,   1,   1,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   0,   1,
    // Page 3: POST ANGT BLST STON GFRY DEKU KEAT BREM BUNA DONG SCEN GORN ROMN CIRC KAFE COUP TRTH ZORA KAMA GIBD GARO CAPT GIAN FIER
                0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   1,
};
static const u8 sSlotAllowedDeku[72] = {
    // Page 1: STICK NUT  BOMB BOW  FIRE DIN  SLING OCA  BCHU HOOK ICE  FAR  BOOM LENS BEAN HAM  LITE NAY  BTL1 BTL2 BTL3 BTL4 TRD_A TRD_C
                1,   1,   0,   0,   0,   0,   0,    1,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   0,    0,
    // Page 2: ROCS WHIP SPIN BARR FROD DEM  DLEF TGAT BEET SWHO IROD ZPER MOGM GJAR BCHN DSEN LROD HYLS PND2 PND1 PND3 CSOM SHVL DROD
                1,   0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    // Page 3: POST ANGT BLST STON GFRY DEKU KEAT BREM BUNA DONG SCEN GORN ROMN CIRC KAFE COUP TRTH ZORA KAMA GIBD GARO CAPT GIAN FIER
                0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   1,
};
// clang-format on
static const u8* sFormSlotAllowed[MM_PLAYER_FORM_MAX] = {
    sSlotAllowedFD,    // FIERCE_DEITY
    sSlotAllowedGoron, // GORON
    sSlotAllowedZora,  // ZORA
    sSlotAllowedDeku,  // DEKU
    NULL               // HUMAN - No restrictions
};

// =============================================================================
// Face Texture System (from 2Ship z_player_lib.c Player_DrawImpl)
//
// Each form's head DL references segment 0x08 for eye textures.
// We set segment 0x08 before drawing to the correct blink-state texture.
// Goron: 4 eye textures (open, half, closed, surprised), no mouth segment.
// =============================================================================

// Goron eye textures (from object_link_goron.h)
static const char sGoronEyeOpen[] = "__OTR__objects/object_link_goron/gLinkGoronEyesOpenTex";
static const char sGoronEyeHalf[] = "__OTR__objects/object_link_goron/gLinkGoronEyesHalfTex";
static const char sGoronEyeClosed[] = "__OTR__objects/object_link_goron/gLinkGoronEyesClosedTex";
static const char sGoronEyeSurprised[] = "__OTR__objects/object_link_goron/gLinkGoronEyesSurprisedTex";

// Zora eye textures (from object_link_zora.h)
static const char sZoraEyeOpen[] = "__OTR__objects/object_link_zora/gLinkZoraEyesOpenTex";
static const char sZoraEyeHalf[] = "__OTR__objects/object_link_zora/gLinkZoraEyesHalfTex";
static const char sZoraEyeClosed[] = "__OTR__objects/object_link_zora/gLinkZoraEyesClosedTex";

// Deku eye textures - CONFIRMED: Deku has NO dynamic eye textures.
// From 2Ship z_player_lib.c line 1939: sPlayerEyesTextures[PLAYER_FORM_DEKU] = all NULL.
// Quote: "Only Human, Zora, and Goron will read the eye textures in the head limb display list.
// Fierce Deity and Deku will point this segment to garbage data, but it will be unread from."
// Deku's eyes are baked directly into gLinkDekuHeadDL (static/painted-on).

// Fierce Deity eye textures - NOT standalone resources in mm.o2r!
// From 2Ship z_player_lib.c line 1939: sPlayerEyesTextures[PLAYER_FORM_FIERCE_DEITY] defines them,
// but the comment says: "Fierce Deity and Deku will point this segment to garbage data, but it
// will be unread from." The FD head DL has eye textures baked in (does NOT read segment 0x08).
// Setting gSPSegment to a non-existent OTR path crashes in ResourceMgr_LoadIfDListByName.
// FIX: Use NULL (same as Deku) to skip the gSPSegment call entirely.

// Zora mouth textures (from object_link_zora.h)
// Zora uses segment 0x09 for mouth.
static const char sZoraMouthClosed[] = "__OTR__objects/object_link_zora/gLinkZoraMouthClosedTex";

// Fierce Deity mouth texture - same issue as eye textures above.
// FD head DL has mouth baked in, segment 0x09 is unread. OTR path doesn't exist → crash.
// static const char sFierceMouthClosed[] = "__OTR__objects/object_link_boy/gLinkFierceDeityMouthClosedTex";

// Per-form eye texture arrays indexed by eye state (0=open, 1=half, 2=closed, 3=special)
// From 2Ship sPlayerEyesTextures[playerForm][eyeIndex]
static const char* sFormEyeTextures[MM_PLAYER_FORM_MAX][4] = {
    // FIERCE_DEITY - No dynamic eye textures (baked into head DL, segment 0x08 unread)
    { NULL, NULL, NULL, NULL },
    // GORON
    { sGoronEyeOpen, sGoronEyeHalf, sGoronEyeClosed, sGoronEyeSurprised },
    // ZORA
    { sZoraEyeOpen, sZoraEyeHalf, sZoraEyeClosed, sZoraEyeOpen },
    // DEKU - No dynamic eye textures (eyes baked into gLinkDekuHeadDL, confirmed from 2Ship)
    { NULL, NULL, NULL, NULL },
    // HUMAN (not used)
    { NULL, NULL, NULL, NULL },
};

// =============================================================================
// Action IDs (forward declaration for MmFormState)
// Full documentation in Action State Machine section below.
// =============================================================================

typedef enum GoronActionId {
    GORON_ACT_IDLE = 0,         // Player_Action_Idle - standing, pg_wait loop
    GORON_ACT_WALK,             // Player_Action_5 - walking, link_normal_walk_free
    GORON_ACT_RUN,              // Player_Action_9 - running, link_normal_run_free
    GORON_ACT_PUNCH_A,          // Left punch (combo step 1) - Phase 4
    GORON_ACT_PUNCH_B,          // Right punch (combo step 2) - Phase 4
    GORON_ACT_PUNCH_C,          // Butt punch (combo step 3) - Phase 4
    GORON_ACT_PUNCH_END,        // Punch recovery - Phase 4
    GORON_ACT_ROLL_INIT,        // Curl animation (pg_maru_change) → enters GORON_ROLL
    GORON_ACT_GORON_ROLL,       // Goron ball rolling (Player_Action_96) with physics
    GORON_ACT_GORON_ROLL_JUMP,  // Ground pound jump phase (velocity.y = 14.0)
    GORON_ACT_GORON_ROLL_POUND, // Ground pound landing (quake + DMG_HAMMER_SWING)
    GORON_ACT_ROLL_UNCURL,      // Uncurl animation (pg_maru_change reversed) → idle
    GORON_ACT_DAMAGE,           // Knockback - Phase 5
    GORON_ACT_LAND,             // Landing recovery - Phase 5

    // Ground system actions (from 2Ship Player_Action_* functions)
    MMFORM_ACT_ZTARGET_IDLE,     // Z-target standing (link_normal_waitR/L_free)
    MMFORM_ACT_ZTARGET_WALK,     // Z-target strafing (side_walkL/R, back_walk)
    MMFORM_ACT_JUMP,             // Jumping upward (link_normal_jump)
    MMFORM_ACT_FALL,             // Falling (link_normal_fall)
    MMFORM_ACT_JUMP_KICK,        // Aerial B attack (pz_jumpAT for Zora, gravity -0.8f)
    MMFORM_ACT_SIDEHOP,          // Z + sideways + A (fighter_Lside/Rside_jump)
    MMFORM_ACT_BACKFLIP,         // Z + back + A (fighter_backturn_jump)
    MMFORM_ACT_ROLL,             // Running + A forward roll (link_normal_landing_roll_free)
    MMFORM_ACT_LEDGE_HANG,       // Hanging from ledge (link_normal_jump_climb_hold_free)
    MMFORM_ACT_LEDGE_CLIMB,      // Climbing up ledge (link_normal_jump_climb_up_free)
    MMFORM_ACT_SHIELD,           // R-button shield (Goron: curl, Zora: guard pose + barrier on R+B)
    MMFORM_ACT_DOOR,             // Door opening (pg_doorA/B_open) - yield to OOT
    MMFORM_ACT_CHEST,            // Chest opening (pg_Tbox_open) - yield to OOT
    MMFORM_ACT_DEKU_SPIN,        // Deku spin attack (Player_Action_95, pn_attack)
    MMFORM_ACT_DEKU_BUBBLE_AIM,  // Deku bubble aim (first-person, hold B to charge)
    MMFORM_ACT_DEKU_BUBBLE,      // Deku bubble fired (projectile in flight)
    MMFORM_ACT_DEKU_FLOWER,      // Deku flower burrow/charge/launch (Player_Action_93)
    MMFORM_ACT_DEKU_FLY,         // Deku flight/glide (Player_Action_94)
    MMFORM_ACT_DEKU_FALL_LOCKED, // Post-flight fall (controls disabled until ground/water)

    // Zora-specific actions (from 2Ship z_player.c)
    MMFORM_ACT_BOOMERANG_THROW,      // B weapon throw (Player_InitZoraBoomerangIA)
    MMFORM_ACT_BOOMERANG_WAIT,       // UNUSED — boomerang return now tracked in background
    MMFORM_ACT_BOOMERANG_CATCH,      // UNUSED — catch handled non-blockingly (SFX only, no anim interrupt)
    MMFORM_ACT_SWIM_IDLE,            // Surface float (Player_Action_54, link_swimer_swim_wait)
    MMFORM_ACT_SWIM_MOVE,            // Surface swim (directional)
    MMFORM_ACT_SWIM_FAST,            // Fast dolphin swim (Player_Action_56, pz_fishswim)
    MMFORM_ACT_SWIM_DASH,            // Swim dash burst (pz_waterroll, A press)
    MMFORM_ACT_SWIM_SURFACE_WALK,    // Surface walk (Player_Action_57, link_swimer_swim)
    MMFORM_ACT_SWIM_UNDERWATER_WALK, // Underwater walk / iron boots (Player_Action_58)
    MMFORM_ACT_DOLPHIN_JUMP,         // Dolphin jump arc (Player_Action_28, fishswim pose, locked input)
    MMFORM_ACT_CLIMB,                // Climbing wall/vine (pg_climb_upL/R loop) - OOT handles mechanics
    MMFORM_ACT_WATER_VOID,           // Goron entered deep water: curl → ball → void out
    MMFORM_ACT_HAZARD_VOID,          // Form hazard: freeze/lava/fire → void out
    MMFORM_ACT_OOT_ACTION,           // OOT has an active special action (item use, NPC talk, etc.) - yield to OOT
} GoronActionId;

// =============================================================================
// Form State (static global)
// =============================================================================

typedef struct {
    // Core state
    MmFormStateId state;
    MmPlayerTransformation currentForm;
    MmPlayerTransformation targetForm;
    u8 initialized;

    // Cutscene sub-state
    s16 cutsceneTimer;
    s16 flashAlpha;
    u8 cutscenePhase; // 0=pre-flash, 1=flash-build, 2=post-flash

    // Skeleton / Animation
    // SkelAnime_InitLink allocates jointTable/morphTable dynamically (avoids limb count assertion)
    SkelAnime formSkelAnime;
    s32 formLimbCount;
    s32 formDListCount;
    u8 skeletonLoaded;

    // Current animations (loaded from mm.o2r)
    LinkAnimationHeader* idleAnim;
    LinkAnimationHeader* walkAnim;
    LinkAnimationHeader* runAnim;

    // === Goron combat animations (Phase 2: batch loaded when form == GORON) ===
    // Punch combo (from 2Ship z_player.c D_8085D064, line 3569-3574)
    LinkAnimationHeader* punchA;     // pg_punchA (left punch)
    LinkAnimationHeader* punchB;     // pg_punchB (right punch)
    LinkAnimationHeader* punchC;     // pg_punchC (butt punch)
    LinkAnimationHeader* punchAEnd;  // pg_punchAend (recovery standing)
    LinkAnimationHeader* punchBEnd;  // pg_punchBend
    LinkAnimationHeader* punchCEnd;  // pg_punchCend
    LinkAnimationHeader* punchAEndR; // pg_punchAendR (recovery running)
    LinkAnimationHeader* punchBEndR; // pg_punchBendR
    LinkAnimationHeader* punchCEndR; // pg_punchCendR

    // Roll (from 2Ship Player_Action_96, line 19886)
    LinkAnimationHeader* maruChange; // pg_maru_change (curl -> ball)

    // Mask removal
    LinkAnimationHeader* maskOffStart; // pg_maskoffstart

    // === Deku combat animations (loaded when form == DEKU) ===
    // Spin attack (from 2Ship Player_Action_95, z_player.c line 19276)
    LinkAnimationHeader* dekuSpinAttack; // pn_attack (2 frames)
    // Bubble spit (from 2Ship func_808306F8 / Player_UpperAction_7)
    LinkAnimationHeader* dekuBowReady; // pn_tamahakidf (2 frames) - walk to ready / aim
    LinkAnimationHeader* dekuBowShoot; // pn_tamahaki (8 frames) - shooting animation
    // Guard pose (from 2Ship Player_ActionHandler_11, z_player.c line 8544)
    LinkAnimationHeader* dekuGuardAnim; // pn_gurd (4 frames) - shield/guard stance
    // Deku flower/flight animations (from 2Ship Player_Action_93/94)
    LinkAnimationHeader* dekuFlightLaunch;  // pn_kakku (12 frames, once) - launch spin
    LinkAnimationHeader* dekuFlightFlutter; // pn_batabata (14 frames, loop) - flutter glide
    LinkAnimationHeader* dekuFlightLand;    // pn_kakkufinish (15 frames, once) - close flower land
    LinkAnimationHeader* dekuFlightFall;    // pn_rakkafinish (11 frames, once) - fall recovery

    // Deku spin attack state (from 2Ship Player_Action_95)
    f32 dekuSpinSpeed;    // unk_B10[0]: spin angular velocity (starts 20000, decreases -800/frame)
    f32 dekuSpinTimer;    // unk_B10[1]: animation/duration counter (starts 0x30000 as float)
    u8 dekuSpinActive;    // Currently in spin attack
    s32 dekuSpinRotAccum; // Accumulated visual rotation (OOT's idle resets shape.rot.y, so we track separately)

    // === Deku bubble projectile (from 2Ship EN_ARROW ARROW_TYPE_DEKU_BUBBLE) ===
    // Uses MM's actor rotation system: world.rot → Actor_SetSpeeds → Actor_MoveWithGravity
    struct {
        u8 active;      // Bubble exists in world
        s8 state;       // unk_149: 0=just fired, 1=flying, -1=bounced
        Vec3f pos;      // Current world position
        Vec3f prevPos;  // Previous frame position (for collision line test)
        s16 rotX;       // world.rot.x (pitch) - wobbled each frame
        s16 rotY;       // world.rot.y (yaw) - wobbled each frame
        f32 hSpeed;     // Horizontal speed (from Actor_SetSpeeds: cos(rotX) * totalSpeed)
        f32 velY;       // Vertical velocity (from Actor_SetSpeeds: -sin(rotX) * totalSpeed)
        f32 scale;      // unk_144: current size, deflates from charge toward 1.0f
        s16 timer;      // unk_260: lifetime frames (99 when fired, dies at 0)
        s16 wobbleAccX; // unk_14A: wobble phase accumulator for rot.x
        s16 wobbleAccY; // unk_14C: wobble phase accumulator for rot.y
    } bubble;
    ColliderCylinder bubbleCollider; // AT collider for bubble projectile (slingshot/deku seed damage)
    u8 bubbleColliderInit;           // Whether collider has been initialized
    u8 bubbleCharging;               // Currently in charge/aim mode (holding B)
    f32 bubbleCharge;                // Charge level during aim (0.0 → 16.0)
    u8 bubbleChargeTimer;            // Frames held (fully charged at > 20)

    // Deku water hop (from 2Ship func_808373F8, z_player.c line 7191-7211)
    // Deku skips across water like a stone, 5 hops max, last hop → spin attack
    u8 dekuHopsRemaining; // remainingHopsCounter: starts at 5, resets on ground

    // === Deku Flower + Flight (from 2Ship Player_Action_93/94, z_player.c lines 18896-19273) ===
    f32 dekuFlowerDepth;    // unk_ABC: vertical offset (0 to -3900) during burrow
    f32 dekuFlowerVelocity; // unk_B48: sink/launch speed (-2000 initial, 2700 golden launch)
    u8 dekuFlowerPhase;     // av1.actionVar1 in Action_93: 0=sink, 1=compress, 2=charge, 3=launch
    u8 dekuFlowerCharge;    // av2.actionVar2 in Action_93: frames A held (golden at >=10, max 15)
    u8 dekuBudCounter;      // unk_B86[0]: flower bud opening counter (0-8, SFX at 8)
    Vec3f dekuLaunchPos;    // unk_AF0: world pos when launched (for flight distance tracking)
    // Flight state (from 2Ship Player_Action_94)
    u32 dekuFlightFlags;      // Tracks MM stateFlags3 bits for flight phases
    s16 dekuPetalSpeed;       // unk_B86[1]: petal rotation speed target (s16)
    s16 dekuPetalAngle;       // unk_B8A: accumulated petal rotation angle
    s16 dekuPitchAngle;       // unk_B8C: body pitch during flight
    s16 dekuRollAngle;        // unk_B8E: body roll during flight
    u16 dekuFlightTimer;      // av2.actionVar2 in Action_94: starts 9999, decrements
    s8 dekuFlightLaunchType;  // av1.actionVar1 in Action_94: -1=scene, 0=dyna, 1=golden
    u16 dekuSparkleAcc;       // unk_B66: sparkle particle accumulator
    f32 dekuSavedShadowScale; // Save/restore shadow scale during flower

    // === Shared damage/landing animations (all forms use these) ===
    // From 2Ship D_8085D0D4[] table (z_player.c line 5863):
    // 8 knockback anims indexed by [damage < 5 vs >= 5][front vs back][no lock-on vs lock-on]
    // "shit" = flinch (small damage), "hit" = stagger (big damage)
    // "R" suffix = lock-on variant, "anchor" prefix = lock-on variant for big hits
    LinkAnimationHeader* dmgAnims[8];
    // [0] = front_shit  (front, small, no lockon)
    // [1] = front_shitR (front, small, lockon)
    // [2] = back_shit   (back, small, no lockon)
    // [3] = back_shitR  (back, small, lockon)
    // [4] = front_hit   (front, big, no lockon)
    // [5] = anchor_front_hitR (front, big, lockon)
    // [6] = back_hit    (back, big, no lockon)
    // [7] = anchor_back_hitR  (back, big, lockon)
    // Strong knockback: launched into air (from 2Ship func_80833B18 line 5843-5847)
    // Used for acHitEffect 7 (shock), 4 (knockback), 9 (fire)
    LinkAnimationHeader* frontDownA;   // link_normal_front_downA (launched forward)
    LinkAnimationHeader* backDownA;    // link_normal_back_downA  (launched backward)
    LinkAnimationHeader* landing;      // link_normal_landing
    LinkAnimationHeader* shortLanding; // link_normal_short_landing

    // === Ground action animations (shared across all forms via D_8085BE84) ===
    LinkAnimationHeader* jumpAnim; // link_normal_jump (ascending)
    LinkAnimationHeader* fallAnim; // link_normal_fall (descending)
    LinkAnimationHeader* rollAnim; // link_normal_landing_roll_free (forward roll)

    // Z-target animations (from 2Ship D_8085BE84 column 0 = PLAYER_ANIMTYPE_DEFAULT)
    LinkAnimationHeader* ztargetIdleR;     // link_normal_waitR_free (right-facing Z-target idle)
    LinkAnimationHeader* ztargetIdleL;     // link_normal_waitL_free (left-facing Z-target idle)
    LinkAnimationHeader* ztargetSideWalkL; // link_normal_side_walkL_free (strafe left)
    LinkAnimationHeader* ztargetSideWalkR; // link_normal_side_walkR_free (strafe right)
    LinkAnimationHeader* ztargetBackWalk;  // link_normal_back_walk (walk backwards while locked on)

    // Defense/guard animations (from 2Ship D_8085BE84[PLAYER_ANIMGROUP_defense])
    // Zora uses ANIMTYPE_2 (armed) variants: link_normal_defense (3 frames)
    // + link_normal_defense_wait (4 frames) + link_normal_defense_end (4 frames)
    LinkAnimationHeader* defenseAnim;     // link_normal_defense (enter guard pose, ANIMTYPE_2)
    LinkAnimationHeader* defenseWaitAnim; // link_normal_defense_wait (hold guard loop)
    LinkAnimationHeader* defenseEndAnim;  // link_normal_defense_end (exit guard transition)

    // Evasive maneuver animations (from 2Ship Player_Action_29 / Player_Action_10)
    LinkAnimationHeader* sidehopL;    // fighter_Lside_jump
    LinkAnimationHeader* sidehopLEnd; // fighter_Lside_jump_end
    LinkAnimationHeader* sidehopR;    // fighter_Rside_jump
    LinkAnimationHeader* sidehopREnd; // fighter_Rside_jump_end
    LinkAnimationHeader* backflip;    // fighter_backturn_jump
    LinkAnimationHeader* backflipEnd; // fighter_backturn_jump_end

    // Jump kick (form-specific: Zora uses pz_jumpAT, others use shared)
    LinkAnimationHeader* jumpKick;    // pz_jumpAT (Zora) / NULL (forms without jump kick)
    LinkAnimationHeader* jumpKickEnd; // pz_jumpATend (Zora) / NULL

    // Ledge grab/climb
    LinkAnimationHeader* ledgeHang;     // link_normal_jump_climb_hold_free
    LinkAnimationHeader* ledgeClimb;    // link_normal_jump_climb_up_free
    LinkAnimationHeader* ledgeHangWait; // link_normal_jump_climb_wait_free

    // Door/chest animations (from 2Ship D_8085D118/D_8085D124/ageProperties->openChestAnim)
    LinkAnimationHeader* doorAOpen; // pg_doorA_open (left door)
    LinkAnimationHeader* doorBOpen; // pg_doorB_open (right door)
    LinkAnimationHeader* chestOpen; // pg_Tbox_open (chest opening)

    // === Ground state tracking ===
    u8 wasOnGround;    // Previous frame ground state (for edge detection)
    u8 jumpKickActive; // Jump kick collision active flag
    s16 sidehopDir;    // -1=left, +1=right (for sidehop direction)
    f32 rollSpeed;     // Initial roll speed (decays during roll)

    // Action state machine (Phase 3)
    // From 2Ship: Player_Action_Idle, Player_Action_5(walk), Player_Action_9(run), etc.
    s32 goronAction; // GoronActionId - current action
    s32 actionTimer; // Frames since action started

    // Speed flinch timer (from 2Ship func_80833B18 line 5973: this->unk_B64 = 20)
    // When moving fast and not locked on, damage causes flinch without knockback
    s16 flinchTimer;

    // Punch combo state (Phase 4)
    // From 2Ship: unk_ADD = combo counter, av2.actionVar2 = B pressed for combo
    u8 comboStep;     // 0=PunchA(left), 1=PunchB(right), 2=PunchC(butt)
    u8 comboBPressed; // B button pressed during current punch (for combo continuation)

    // Root motion data (from 2Ship ANIM_FLAG_ENABLE_MOVEMENT system)
    // Both Goron and Zora punches use animation root translation to drive forward movement.
    // From 2Ship func_80833864 line 5809: Player_AnimReplace_Setup with ANIM_FLAG_ENABLE_MOVEMENT.
    // The raw root X/Z values per frame are extracted from MM animation data before
    // baseTransl fix is applied (the fix zeros out root motion for rendering).
    // At runtime, per-frame deltas are computed and applied to actor.world.pos,
    // replicating SkelAnime_UpdateTranslation from 2Ship z_skelanime.c line 2037.
    struct {
        s16* rootX[3];     // Per-frame root X for punch A/B/C (raw animation units)
        s16* rootZ[3];     // Per-frame root Z for punch A/B/C
        s32 frameCount[3]; // Number of frames per punch animation
        s16 prevX;         // Previous frame root X (for delta computation)
        s16 prevZ;         // Previous frame root Z
        u8 active;         // Currently applying root motion
        u8 firstFrame;     // ANIM_FLAG_NOMOVE equivalent: skip delta on first frame
        u8 currentPunch;   // Which punch (0-2) is active for root motion lookup
    } rootMotion;

    // Blink system (from 2Ship FaceChange_UpdateBlinkingNonHuman)
    s16 blinkTimer; // Counts down, blink happens in last 3 frames
    u8 eyeIndex;    // 0=open, 1=half, 2=closed

    // Damage/knockback state (Phase 5)
    // From 2Ship func_80833B18 (z_player.c line 5877): knockback setup
    s16 damageTimer;  // Knockback safety timer (frames remaining, fallback if anim stalls)
    u8 knockbackType; // 0=small ground, 1=big launch, 3=freeze, 4=electric

    // Hazard void out state (MMFORM_ACT_HAZARD_VOID)
    // Sub-types: 0=freeze(Zora), 1=lava(Deku/Zora), 2=fire hit(Deku/Zora)
    u8 hazardVoidType;
    s16 hazardVoidTimer;

    // Goron shielding skeleton (separate from main form SkelAnime)
    // From 2Ship z_player.c line 11181: SkelAnime_InitFlex for gLinkGoronShieldingSkel
    SkelAnime shieldSkelAnime;
    u8 shieldSkelLoaded;

    // Shield damage protection collider (from 2Ship D_8085C318, z_player.c line 1686)
    // Separate from player->cylinder. Registered with AC when shielding.
    // AC_HARD causes projectiles to bounce off. AC_BOUNCED flag checked in damage handler.
    ColliderCylinder shieldCollider;
    u8 shieldColliderInitDone;

    // Shield directional control gate (from 2Ship av2.actionVar2 in Player_Action_18)
    // Set to 1 when shield animation finishes playing once. Enables pitch/yaw stick control.
    u8 shieldAv2;

    // Goron Roll state (from 2Ship Player_Action_96, z_player.c line 19886)
    f32 rollBallSpeed;       // unk_B08: actual ball speed (max 18.0f)
    f32 rollBounce;          // unk_B0C: bounce energy from wall hits
    f32 rollTilt;            // unk_B48: visual tilt from velocity changes
    f32 rollSquash;          // unk_ABC: squash/stretch deformation factor for ball visual
    s16 rollHomeYaw;         // actor.home.rot.y: real movement direction
    s16 rollChargeLevel;     // av1.actionVar1: charge counter (0→4→0x36+→spike)
    s16 rollSpinRate;        // av2.actionVar2: ball visual spin speed
    s16 rollSpikeActive;     // unk_B86[1]: spike mode counter (0=off, 1-7=active)
    s16 rollSfxCounter;      // unk_B86[0]: rolling SFX rotation counter
    s16 magicDrainTimer;     // from 2Ship z_parameter.c: magicConsumptionTimer (drain 1 magic per 10 frames)
    u8 rollWallBounceTimer;  // unk_B8C: frames to ignore input after wall bounce
    u8 rollNoInputTimer;     // unk_B8E: frames of zero input after spike disable
    u8 rollGroundPoundTimer; // unk_B8A: ground pound fall/pause timer

    // Ground pound crack visual (from 2Ship ACTOR_EN_TEST: dark circle on floor at impact)
    Vec3f groundPoundImpactPos;          // World position of impact
    CollisionPoly* groundPoundFloorPoly; // Floor polygon for orientation
    s16 groundPoundCrackTimer;           // Frames remaining (fades over ~30 frames)

    // =========================================================================
    // Zora Electric Barrier (from 2Ship func_8082F164/func_8082F1AC, z_player.c:2922-2981)
    // Flag-based system: barrier runs alongside any action (walk, swim, etc.)
    // NOT a separate action like before. R button sets barrierActive flag,
    // MmForm_UpdateBarrier() runs every frame to update intensity/light/damage.
    // =========================================================================
    s16 barrierIntensity;    // 0-255, ramps ±50/frame (from func_8082F1AC)
    u8 barrierActive;        // R button held (PLAYER_STATE1_10 equivalent)
    LightNode* barrierLight; // Orbiting point light around player
    LightInfo barrierLightInfo;
    ColliderCylinder barrierCollider; // Damage cylinder (r=60, h=80, DMG_ZORA_BARRIER)
    u8 barrierColliderInit;

    // =========================================================================
    // Zora Boomerang Fins (from 2Ship Player_InitZoraBoomerangIA, z_player.c:3470)
    // =========================================================================
    LinkAnimationHeader* cutterAttack;   // pz_cutterattack (throw anim)
    LinkAnimationHeader* cutterCatch;    // pz_cuttercatch (catch anim)
    LinkAnimationHeader* cutterWaitA;    // pz_cutterwaitA
    LinkAnimationHeader* cutterWaitB;    // pz_cutterwaitB
    LinkAnimationHeader* cutterWaitC;    // pz_cutterwaitC
    LinkAnimationHeader* cutterWaitAnim; // pz_cutterwaitanim (idle while fins flying)
    LinkAnimationHeader* bladeOn;        // pz_bladeon

    u8 boomerangState;      // 0=idle, 1=aiming, 2=throwing, 3=thrown/waiting
    s16 boomerangTimer;     // Frame counter for throw animation
    Actor* boomerangActorL; // Left fin (OOT ACTOR_EN_BOOM)
    Actor* boomerangActorR; // Right fin (OOT ACTOR_EN_BOOM)
    s16 boomerangAimYaw;    // Aim yaw offset from body facing (from MM func_80847190)
    s16 boomerangAimPitch;  // Aim pitch (vertical, from MM func_80847190)
    s16 boomerangLockedYaw; // Saved shape.rot.y when entering aim (forced every frame during aim/throw)

    // =========================================================================
    // Zora Swimming (from 2Ship Player_Action_54-58, z_player.c:16820-17072)
    // =========================================================================
    LinkAnimationHeader* fishSwim;     // pz_fishswim (fast dolphin swim)
    LinkAnimationHeader* waterRoll;    // pz_waterroll (swim dash barrel roll)
    LinkAnimationHeader* swimToWait;   // pz_swimtowait (transition to idle)
    LinkAnimationHeader* swimWaitAnim; // link_swimer_swim_wait (treading water idle)
    LinkAnimationHeader* swimAnim;     // link_swimer_swim (surface swim forward)

    // Climb anims (from 2Ship D_8085BE84 PLAYER_ANIMTYPE_DEFAULT column)
    LinkAnimationHeader* climbStartA; // pz_climb_startA
    LinkAnimationHeader* climbStartB; // pz_climb_startB
    LinkAnimationHeader* climbEndAL;  // pz_climb_endAL
    LinkAnimationHeader* climbEndAR;  // pz_climb_endAR
    LinkAnimationHeader* climbEndBL;  // pz_climb_endBL
    LinkAnimationHeader* climbEndBR;  // pz_climb_endBR
    LinkAnimationHeader* climbUpL;    // pz_climb_upL
    LinkAnimationHeader* climbUpR;    // pz_climb_upR

    u8 swimState;         // 0=not swimming, 1=surface, 2=fast, 3=dash
    s16 swimPitch;        // Body pitch for fast swim (unk_AAA equivalent)
    s16 swimRoll;         // Barrel roll angle (unk_B86[1] equivalent)
    f32 swimSpeed;        // Current swim speed
    s16 swimDashTimer;    // Dash burst timer (decays speed from 16→0)
    u8 zoraBoots;         // 0=ZORA_LAND (free swim), 1=ZORA_UNDERWATER (iron boots/sink)
    u8 fastSwimActive;    // Equivalent to PLAYER_STATE3_8000 (dolphin swim mode)
    s16 swimRollSmoothed; // Smoothed roll for draw (unk_B8E equivalent in MM)
    // Fast swim 3-phase state machine (from 2Ship Player_Action_56)
    u8 swimPhase;         // 0=waterroll transition, 1=active swimming, 2=exiting
    s16 swimPhaseCounter; // av2 equivalent (5 loops for waterroll→fishswim)
    f32 swimSpeedB48;     // unk_B48 — speed accumulator for cos/sin velocity split
    s16 swimYawRate;      // unk_B8A — stick X → yaw accumulation rate
    u8 swimExitFlag;      // unk_B86[0] — 0=swimming, 1=exiting swim
    s16 swimFloorTimer;   // unk_B8C — floor bounce cooldown during fast swim
    s16 bootToggleDelay;  // av2 equivalent for boot toggle (20 frames before dive allowed)

    // Whether form DL resources have been pinned (held alive in shared_ptrs)
    u8 formDLsPinned;

    // Saved OOT state for restoration
    f32 savedShadowScale;
    u8 savedMass;
    s16 savedStrength;                       // Original UPG_STRENGTH value (FD forces max strength)
    u8 savedTunic;                           // Original currentTunic (restored on detransform)
    PlayerAgeProperties* savedAgeProperties; // Original ageProperties pointer to restore

    // Per-form ageProperties override (copy of original with form-specific dimension fields).
    // OOT reads player->ageProperties-> for ALL size-dependent gameplay checks:
    // ledge grab/climb height, wall/grab/push detection, ceiling collision, water interaction, etc.
    PlayerAgeProperties formAgeProperties;
} MmFormState;

static MmFormState gFormState;

// Static variables to preserve form across scene transitions (survive memset of gFormState)
static MmPlayerTransformation sPendingReactivateForm = MM_PLAYER_FORM_HUMAN;
static u8 sPendingReactivate = 0;
static u8 sForceInstantTransform = 0; // Set to 1 for scene-transition reactivation

// Saved equips for pre-transform state (like vanilla child/adult equip swap)
static ItemEquips sPreTransformEquips;
static u8 sEquipsSaved = 0;

// Per-form gravity for different action states.
// Values from 2Ship z_player.c: Player_Action_25 (line 15165), Player_Action_29 (line 15382),
// Player_Action_96 (lines 20142/20145).
// =============================================================================
// Slot-Based Restriction Helpers
// =============================================================================

// Check if a slot is allowed for the current form (internal, uses gFormState)
static u8 MmForm_IsSlotAllowedInternal(u8 slot) {
    if (slot >= 72)
        return 1;
    if (gFormState.state != MMFORM_STATE_ACTIVE && gFormState.state != MMFORM_STATE_TRANSFORMING &&
        gFormState.state != MMFORM_STATE_DETRANSFORMING)
        return 1; // Not transformed = everything allowed
    if (gFormState.currentForm >= MM_PLAYER_FORM_HUMAN)
        return 1; // Human = no restrictions
    const u8* allowed = sFormSlotAllowed[gFormState.currentForm];
    if (allowed == NULL)
        return 1;
    return allowed[slot];
}

// =============================================================================
// C-Button Equip Save/Restore (like vanilla child/adult equip swap)
//
// On transform: save current equips, unequip blocked items from C-buttons.
// On detransform: restore saved equips (re-reading bottle/trade item contents).
// =============================================================================

static void MmForm_SaveAndRestrictEquips(PlayState* play) {
    // Save current equips
    memcpy(&sPreTransformEquips, &gSaveContext.equips, sizeof(ItemEquips));
    sEquipsSaved = 1;

    const u8* allowed = sFormSlotAllowed[gFormState.currentForm];
    if (allowed == NULL)
        return; // Human = no restrictions

    // Check each C-button and DPad button (indices 1-7, skip B button at 0)
    for (s32 i = 1; i < 8; i++) {
        u8 item = gSaveContext.equips.buttonItems[i];
        if (item == ITEM_NONE || item == ITEM_NONE_FE)
            continue;

        u8 slot = gSaveContext.equips.cButtonSlots[i - 1];
        if (slot >= 72 || slot == SLOT_NONE)
            continue;

        if (!allowed[slot]) {
            gSaveContext.equips.buttonItems[i] = ITEM_NONE;
            gSaveContext.equips.cButtonSlots[i - 1] = SLOT_NONE;
            Interface_LoadItemIcon1(play, i);
        }
    }
}

static void MmForm_RestoreEquips(PlayState* play) {
    if (!sEquipsSaved)
        return;

    for (s32 i = 1; i < 8; i++) {
        gSaveContext.equips.buttonItems[i] = sPreTransformEquips.buttonItems[i];
        gSaveContext.equips.cButtonSlots[i - 1] = sPreTransformEquips.cButtonSlots[i - 1];

        // For bottles and trade items, re-read current inventory contents
        // (bottle contents may have changed during transformation)
        u8 item = gSaveContext.equips.buttonItems[i];
        u8 slot = gSaveContext.equips.cButtonSlots[i - 1];
        if (slot < 72 && slot != SLOT_NONE) {
            if ((item >= ITEM_BOTTLE && item <= ITEM_POE) || (item >= ITEM_WEIRD_EGG && item <= ITEM_CLAIM_CHECK)) {
                gSaveContext.equips.buttonItems[i] = gSaveContext.inventory.items[slot];
            }
        }

        if (gSaveContext.equips.buttonItems[i] != ITEM_NONE) {
            Interface_LoadItemIcon1(play, i);
        }
    }

    sEquipsSaved = 0;
}

static f32 MmForm_GetGravity(MmFormGravityState gravState) {
    switch (gravState) {
        case MMFORM_GRAVITY_JUMP_KICK:
            return (gFormState.currentForm == MM_PLAYER_FORM_ZORA) ? -0.8f : -1.2f;
        case MMFORM_GRAVITY_SWIM:
        case MMFORM_GRAVITY_LEDGE:
            return 0.0f;
        case MMFORM_GRAVITY_ROLL_APEX:
            return -0.2f;
        case MMFORM_GRAVITY_ROLL_SLAM:
            return -10.0f;
        case MMFORM_GRAVITY_NORMAL:
        default:
            return -1.2f;
    }
}

// From 2Ship func_8083CBC4 (z_player.c line 9389)
// Air movement control during sidehops/backflips.
// Reads stick input each frame and adjusts speed + yaw for subtle in-air steering.
// If yaw difference > 90 degrees: decelerates to stop and snaps yaw.
// Otherwise: asymptotically adjusts speed toward speedTarget, rotates yaw toward yawTarget.
static s32 MmForm_AirControl(Player* player, f32 speedTarget, s16 yawTarget, f32 decelFactor, f32 stepRate,
                             f32 dampening, s16 yawStep) {
    s16 yawDiff = player->yaw - yawTarget;

    if (ABS(yawDiff) > 0x6000) {
        // Moving backwards relative to target: decelerate to stop, snap yaw
        if (!Math_StepToF(&player->linearVelocity, 0.0f, decelFactor)) {
            return false;
        }
        player->yaw = yawTarget;
    } else {
        // Normal: smoothly adjust speed and yaw toward stick targets
        Math_AsymStepToF(&player->linearVelocity, speedTarget, stepRate, dampening);
        Math_ScaledStepToS(&player->yaw, yawTarget, yawStep);
    }
    return true;
}

// Scale jump/sidehop/backflip velocity by form's rootAnimScale (unk_08 from MM PlayerAgeProperties).
// Pending damage info: written by OOT's func_808382DC, read by MmForm_CheckDamage
extern "C" MmFormPendingDamage gMmFormPendingDamage = { 0, 0, 0, NULL };

// =============================================================================
// Deep-Pin DL Resource System
// =============================================================================
//
// WHY: The Fast3D interpreter patches DL instructions IN-PLACE: VTX hash handlers
// (interpreter.cpp:3021) and texture hash handlers (interpreter.cpp:3405) write resolved
// raw pointers directly into Gfx.words.w1. On subsequent frames, these handlers see the
// large pointer value (offset > 0xFFFFF) and use it directly WITHOUT re-resolving.
// If the pointed-to vertex/texture resource gets removed from cache (by DirtyResources,
// UnloadResource, etc.), the patched pointer becomes dangling → crash 0xc0000005.
//
// Deep pin: holds ALL form object resources alive (DLs, VTX, TEX, etc.)
// so the resource manager never evicts them while a form is active.
// Each form uses a different object (object_link_goron, object_link_boy, etc.)
static std::shared_ptr<std::vector<std::shared_ptr<Ship::IResource>>> sPinnedFormResources;

// Object path prefixes per form (for bulk resource pinning)
static const char* sFormObjectPaths[MM_PLAYER_FORM_MAX] = {
    "objects/object_link_boy/*",   // FIERCE_DEITY
    "objects/object_link_goron/*", // GORON
    "objects/object_link_zora/*",  // ZORA
    "objects/object_link_nuts/*",  // DEKU
    NULL,                          // HUMAN (not used)
};

// Pin ALL resources for the given form's object from mm.o2r.
static void MmForm_PinFormResources(MmPlayerTransformation form) {
    sPinnedFormResources.reset();

    if (form < 0 || form >= MM_PLAYER_FORM_MAX || sFormObjectPaths[form] == NULL) {
        return;
    }

    auto resMgr = Ship::Context::GetInstance()->GetResourceManager();
    sPinnedFormResources = resMgr->LoadResources(sFormObjectPaths[form]);
    if (sPinnedFormResources != nullptr) {
        MMFORM_LOG("[MmForm] Deep-pinned %zu resources for form %d from %s", sPinnedFormResources->size(), form,
                   sFormObjectPaths[form]);
    } else {
        MMFORM_LOG("[MmForm] WARNING: Failed to bulk-load resources for form %d: %s", form, sFormObjectPaths[form]);
    }
}

static void MmForm_UnpinFormResources(void) {
    sPinnedFormResources.reset();
}

// =============================================================================
// Pre-loaded & Validated DL Pointers
// =============================================================================
//
// WHY: Loading DLs via OTR path strings every frame goes through ResourceMgr_LoadGfxByName,
// which returns &Instructions[0] from the Fast::DisplayList resource. If the DL from mm.o2r
// is missing G_ENDDL at the end, the interpreter reads past the Instructions buffer into
// adjacent heap memory, interpreting garbage bytes as GFX opcodes until it hits something
// fatal (like G_SETCIMG with an invalid segment address).
//
// FIX: Pre-load each DL at init time, validate it has G_ENDDL, and if not, create a safe
// copy with G_ENDDL appended. Cache the pointer and reuse it every frame.
static std::vector<Gfx> sCurledDLSafeCopy;
static std::vector<Gfx> sSpikeGeomDLSafeCopy;     // object_link_goron_DL_00C540 (lg_spike_model)
static std::vector<Gfx> sEnergyEffect1DLSafeCopy; // object_link_goron_DL_0127B0 (grt_01_model)
static std::vector<Gfx> sEnergyEffect2DLSafeCopy; // object_link_goron_DL_0134D0 (grt_02_model)
static std::vector<Gfx> sPunchDLSafeCopy;
static size_t sCurledDLCount = 0; // Pristine instruction count (for per-frame copy)
static size_t sSpikeGeomDLCount = 0;
static size_t sEnergyEffect1DLCount = 0;
static size_t sEnergyEffect2DLCount = 0;
static size_t sPunchDLCount = 0;
static Gfx* sCachedCurledDL = NULL;
static Gfx* sCachedSpikeGeomDL = NULL;
static Gfx* sCachedEnergyEffect1DL = NULL;
static Gfx* sCachedEnergyEffect2DL = NULL;
static Gfx* sCachedPunchDL = NULL;
static std::vector<Gfx> sBarrierDLSafeCopy;
static size_t sBarrierDLCount = 0;
static Gfx* sCachedBarrierDL = NULL;
static std::vector<Gfx> sZoraFinLDLSafeCopy; // object_link_zora_DL_00CC38 (left forearm fin)
static std::vector<Gfx> sZoraFinRDLSafeCopy; // object_link_zora_DL_00CDA0 (right forearm fin)
static size_t sZoraFinLDLCount = 0;
static size_t sZoraFinRDLCount = 0;
static Gfx* sCachedZoraFinLDL = NULL;
static Gfx* sCachedZoraFinRDL = NULL;
// Deku bubble projectile geometry DLs from MM gameplay_keep (from 2Ship z_en_arrow.c:716-738)
// gameplay_keep_DL_06F9F0 = stationary bubble (sphere mesh, XLU), gameplay_keep_DL_06FAE0 = moving bubble (compressed
// sphere, OPA)
#define dgMmDekuBubbleStillDL "__OTR__objects/gameplay_keep/gameplay_keep_DL_06F9F0"
#define dgMmDekuBubbleMoveDL "__OTR__objects/gameplay_keep/gameplay_keep_DL_06FAE0"
static const ALIGN_ASSET(2) char gMmDekuBubbleStillDL[] = dgMmDekuBubbleStillDL;
static const ALIGN_ASSET(2) char gMmDekuBubbleMoveDL[] = dgMmDekuBubbleMoveDL;
static std::vector<Gfx> sDekuBubbleStillDLSafeCopy;
static std::vector<Gfx> sDekuBubbleMoveDLSafeCopy;
static size_t sDekuBubbleStillDLCount = 0;
static size_t sDekuBubbleMoveDLCount = 0;
static Gfx* sCachedDekuBubbleStillDL = NULL;
static Gfx* sCachedDekuBubbleMoveDL = NULL;

// =============================================================================
// Fierce Deity Hand DLs (from object_link_boy in mm.o2r)
// =============================================================================
// FD hand models swap dynamically based on held item (sword, empty, bottle).
// These are loaded once during skeleton setup and swapped in MmForm_OverrideLimbDraw.
enum FDHandDLIndex {
    FD_DL_LEFT_HAND_SWORD = 0,
    FD_DL_LEFT_HAND_EMPTY,
    FD_DL_LEFT_HAND_BOTTLE,
    FD_DL_RIGHT_HAND_EMPTY,
    FD_DL_SWORD_BEAM, // gSwordBeamDL from gameplay_keep (for sword beam projectile)
    FD_DL_COUNT
};

static std::vector<Gfx> sFDHandDLSafeCopies[FD_DL_COUNT];
static size_t sFDHandDLCounts[FD_DL_COUNT] = { 0 };
static Gfx* sCachedFDHandDLs[FD_DL_COUNT] = { NULL };

// OTR paths for FD hand DLs (from object_link_boy.h)
static const char* sFDHandDLPaths[FD_DL_COUNT] = {
    "__OTR__objects/object_link_boy/gLinkFierceDeityLeftHandHoldingSwordDL",
    "__OTR__objects/object_link_boy/gLinkFierceDeityLeftHandEmptyDL",
    "__OTR__objects/object_link_boy/gLinkFierceDeityLeftHandHoldingBottleDL",
    "__OTR__objects/object_link_boy/gLinkFierceDeityRightHandEmptyDL",
    "__OTR__objects/gameplay_keep/gSwordBeamDL",
};

static Gfx* MmForm_LoadAndValidateDL(const char* otrPath, std::vector<Gfx>& safeCopy) {
    // Strip __OTR__ prefix for resource manager lookup
    const char* path = otrPath;
    if (strncmp(path, "__OTR__", 7) == 0) {
        path += 7;
    }

    auto resMgr = Ship::Context::GetInstance()->GetResourceManager();
    auto res = resMgr->LoadResourceProcess(path);
    if (!res) {
        MMFORM_LOG("[MmForm] Failed to load DL resource: %s", path);
        return NULL;
    }

    auto dlRes = std::dynamic_pointer_cast<Fast::DisplayList>(res);
    if (!dlRes) {
        MMFORM_LOG("[MmForm] WARNING: Resource is NOT a Fast::DisplayList: %s", path);
        // Fallback: try ResourceMgr_LoadGfxByName anyway
        Gfx* fallback = ResourceMgr_LoadGfxByName(otrPath);
        return fallback;
    }

    if (dlRes->Instructions.empty()) {
        MMFORM_LOG("[MmForm] WARNING: DisplayList has 0 instructions: %s", path);
        return NULL;
    }

    size_t count = dlRes->Instructions.size();
    Gfx& lastCmd = dlRes->Instructions[count - 1];
    uint8_t lastOpcode = (uint8_t)((lastCmd.words.w0 >> 24) & 0xFF);

    // === FULL DL SCAN: check for ALL opcodes including standard segmented commands ===
    {
        int otrSettimgHash = 0, otrVtxHash = 0, otrDlHash = 0, otrDlFilepath = 0;
        int otrMtx = 0, otrMovemem = 0, otrBranchZ = 0, otrMarker = 0;
        int stdSettimg = 0, stdVtx = 0, stdDl = 0, stdDlIndex = 0, stdMtx = 0, stdMovemem = 0;
        int dangerSetcimg = 0, dangerLoadUcode = 0;

        for (size_t i = 0; i < count; i++) {
            uint8_t op = (uint8_t)((dlRes->Instructions[i].words.w0 >> 24) & 0xFF);
            uintptr_t w1 = dlRes->Instructions[i].words.w1;

            switch (op) {
                // OTR expanded commands (2-instruction, data word follows)
                case 0x20:
                    otrSettimgHash++;
                    break;
                case 0x32:
                    otrVtxHash++;
                    break;
                case 0x31:
                    otrDlHash++;
                    break;
                case 0x36:
                    otrMtx++;
                    break;
                case 0x42:
                    otrMovemem++;
                    break;
                case 0x35:
                    otrBranchZ++;
                    break;
                case 0x33:
                    otrMarker++;
                    break;
                case 0x27:
                    otrDlFilepath++;
                    break;
                case 0x25:
                    break; // G_SETTIMG_OTR_FILEPATH
                case 0x24:
                    break; // G_VTX_OTR_FILEPATH

                // Standard commands that use SegAddr - should NOT appear in OTR DLs!
                case 0xFD: // G_SETTIMG - uses SegAddr(w1) for texture pointer
                    stdSettimg++;
                    MMFORM_LOG("[MmForm] WARNING: %s DL[%zu] std G_SETTIMG(0xFD) w1=0x%016llX (seg=%d)", path, i,
                               (unsigned long long)w1, (int)(w1 & 1));
                    break;
                case 0x01: // G_VTX (F3DEX2) - uses SegAddr(w1) for vertex pointer
                    stdVtx++;
                    MMFORM_LOG("[MmForm] WARNING: %s DL[%zu] std G_VTX(0x01) w1=0x%016llX", path, i,
                               (unsigned long long)w1);
                    break;
                case 0xDE: // G_DL (F3DEX2) - uses SegAddr(w1) for sub-DL pointer
                    stdDl++;
                    MMFORM_LOG("[MmForm] WARNING: %s DL[%zu] std G_DL(0xDE) w1=0x%016llX (seg=0x%02X off=0x%06X)", path,
                               i, (unsigned long long)w1, (int)((w1 >> 24) & 0xFF), (int)(w1 & 0x00FFFFFE));
                    break;
                case 0x3D: // G_DL_INDEX - uses SegAddr with index-to-offset conversion
                    stdDlIndex++;
                    MMFORM_LOG("[MmForm] WARNING: %s DL[%zu] std G_DL_INDEX(0x3D) w1=0x%016llX (seg=0x%02X idx=0x%06X)",
                               path, i, (unsigned long long)w1, (int)((w1 >> 24) & 0xFF), (int)(w1 & 0x00FFFFFF));
                    break;
                case 0xDA: // G_MTX (F3DEX2) - uses SegAddr(w1) for matrix pointer
                    stdMtx++;
                    MMFORM_LOG("[MmForm] WARNING: %s DL[%zu] std G_MTX(0xDA) w1=0x%016llX", path, i,
                               (unsigned long long)w1);
                    break;
                case 0xDC: // G_MOVEMEM - uses SegAddr(w1) for memory pointer
                    stdMovemem++;
                    MMFORM_LOG("[MmForm] WARNING: %s DL[%zu] std G_MOVEMEM(0xDC) w1=0x%016llX", path, i,
                               (unsigned long long)w1);
                    break;

                // Dangerous opcodes that should NEVER be in model DLs
                case 0xFF:
                    dangerSetcimg++;
                    MMFORM_LOG("[MmForm] DANGER: DL[%zu] G_SETCIMG(0xFF) w0=0x%016llX w1=0x%016llX", i,
                               (unsigned long long)dlRes->Instructions[i].words.w0, (unsigned long long)w1);
                    break;
                case 0xDD:
                    dangerLoadUcode++;
                    MMFORM_LOG("[MmForm] DANGER: DL[%zu] G_LOAD_UCODE(0xDD) w0=0x%016llX w1=0x%016llX", i,
                               (unsigned long long)dlRes->Instructions[i].words.w0, (unsigned long long)w1);
                    break;
                default:
                    break;
            }
        }
    }

    // ALWAYS store a pristine copy of the DL instructions.
    // The interpreter modifies DL entries IN-PLACE (writes cached pointers to w1),
    // so we need the original data to create fresh copies each frame.
    if (lastOpcode != 0xDF) {
        // Missing G_ENDDL - append one
        safeCopy.resize(count + 1);
        memcpy(safeCopy.data(), dlRes->Instructions.data(), count * sizeof(Gfx));
        safeCopy[count].words.w0 = (uintptr_t)0xDF << 24;
        safeCopy[count].words.w1 = 0;
    } else {
        // Has G_ENDDL - copy as-is
        safeCopy.resize(count);
        memcpy(safeCopy.data(), dlRes->Instructions.data(), count * sizeof(Gfx));
    }

    Gfx* ptr = &dlRes->Instructions[0];
    return ptr;
}

/**
 * Pre-resolve all OTR hash references in a display list (textures, vertices, sub-DLs).
 *
 * Walks the DL instruction-by-instruction, resolving all CRC64 hashes to validate:
 * 1. All texture/vertex hashes can be found in the archive
 * 2. All sub-DL hashes resolve to actual DisplayList resources (not textures!)
 * 3. Sub-DLs are recursively validated (max depth 4)
 *
 * Also pre-loads resources into the cache for the interpreter.
 */
static void MmForm_PreResolveDLHashes(Gfx* dl, const char* dlName, int depth) {
    if (dl == NULL || depth > 4)
        return;

    auto resMgr = Ship::Context::GetInstance()->GetResourceManager();
    auto archMgr = resMgr->GetArchiveManager();

    // Walk the DL instructions, properly skipping 2-instruction expanded commands
    for (int i = 0; i < 2048; i++) { // safety limit
        uint8_t opcode = (uint8_t)((dl[i].words.w0 >> 24) & 0xFF);

        if (opcode == 0xDF)
            break; // G_ENDDL - end of DL

        // G_SETTIMG_OTR_HASH (0x20) or G_VTX_OTR_HASH (0x32): 2-instruction command
        if (opcode == 0x20 || opcode == 0x32) {
            i++; // advance to hash data instruction
            uint64_t hash = ((uint64_t)dl[i].words.w0 << 32) | (uint64_t)dl[i].words.w1;

            const char* fileName = archMgr->HashToCString(hash);
            if (fileName == nullptr) {
                MMFORM_LOG("[MmForm] HASH FAIL in %s[%d]: opcode=0x%02X hash=0x%016llX → NOT FOUND!", dlName, i - 1,
                           opcode, (unsigned long long)hash);
            } else {
                // Pre-load the resource into cache
                auto res = resMgr->LoadResourceProcess(fileName);
                if (!res) {
                    MMFORM_LOG("[MmForm] LOAD FAIL in %s[%d]: %s (hash=0x%016llX)", dlName, i - 1, fileName,
                               (unsigned long long)hash);
                }
            }
            continue;
        }

        // G_DL_OTR_HASH (0x31): 2-instruction command calling a sub-DL by hash
        if (opcode == 0x31) {
            i++; // advance to hash data instruction
            uint64_t hash = ((uint64_t)dl[i].words.w0 << 32) | (uint64_t)dl[i].words.w1;

            const char* fileName = archMgr->HashToCString(hash);
            if (fileName == nullptr) {
                MMFORM_LOG("[MmForm] SUB-DL HASH FAIL in %s[%d]: hash=0x%016llX → NOT FOUND!", dlName, i - 1,
                           (unsigned long long)hash);
            } else {
                auto subRes = resMgr->LoadResourceProcess(fileName);
                if (subRes) {
                    auto subDL = std::dynamic_pointer_cast<Fast::DisplayList>(subRes);
                    if (subDL && !subDL->Instructions.empty()) {
                        // Verify sub-DL ends with G_ENDDL
                        size_t cnt = subDL->Instructions.size();
                        uint8_t lastOp = (uint8_t)((subDL->Instructions[cnt - 1].words.w0 >> 24) & 0xFF);
                        MmForm_PreResolveDLHashes(&subDL->Instructions[0], fileName, depth + 1);
                    } else if (subRes) {
                        // Resource loaded but NOT a DisplayList! This would crash the interpreter.
                        MMFORM_LOG("[MmForm] TYPE MISMATCH! %s[%d]: %s is NOT a DisplayList! "
                                   "The interpreter would execute non-DL data as commands → CRASH!",
                                   dlName, i - 1, fileName);
                    }
                } else {
                    MMFORM_LOG("[MmForm] SUB-DL LOAD FAIL in %s[%d]: %s", dlName, i - 1, fileName);
                }
            }
            continue;
        }

        // All other 2-instruction expanded OTR commands: skip the data word
        // G_MARKER (0x33), G_MTX_OTR (0x36), G_BRANCH_Z_OTR (0x35), G_MOVEMEM_OTR (0x42)
        if (opcode == 0x33 || opcode == 0x36 || opcode == 0x35 || opcode == 0x42) {
            i++; // skip data instruction
            continue;
        }
    }
}

// NOTE: MmForm_PatchBadSubDLs was removed — the composite DL approach was wrong.
// Instead, sub-DLs are drawn individually. Energy DLs (grt_01/grt_02) that reference
// segment 0x08 via standard G_DL(0xDE) are patched per-frame by MmForm_PatchSegmentedDL
// to use direct pointers to TwoTexScroll, bypassing segment table resolution entirely.

/**
 * Patch standard segmented G_DL commands in a per-frame DL copy to use direct pointers.
 *
 * mm.o2r DLs may contain:
 * - Standard G_DL (0xDE) with segmented addresses (e.g. 0x08000001 for TwoTexScroll)
 * - G_DL_INDEX (0x3D) with segment + index (e.g. seg=0x0C idx=2 for gCullFrontDList)
 *
 * The F3D interpreter resolves these via the segment table at render time. G_DL_INDEX
 * converts index to byte offset (index * sizeof(F3DGfx)), then adds to segment base.
 * This depends on gCullFrontDList being at exactly gCullBackDList + 2*16 bytes, which
 * is NOT guaranteed by the linker in Release builds.
 *
 * This function replaces ALL segmented G_DL/G_DL_INDEX commands targeting a specific
 * segment with direct-pointer G_DL commands, bypassing segment table resolution entirely.
 */
static int MmForm_PatchSegmentedDL(Gfx* dlCopy, size_t count, u8 targetSeg, Gfx* replacement) {
    if (dlCopy == NULL || replacement == NULL)
        return 0;

    int patched = 0;

    for (size_t i = 0; i < count; i++) {
        uint8_t op = (uint8_t)((dlCopy[i].words.w0 >> 24) & 0xFF);

        // Standard G_DL (0xDE) with segmented bit set (w1 & 1)
        if (op == 0xDE && (dlCopy[i].words.w1 & 1)) {
            uint8_t segNum = (uint8_t)((dlCopy[i].words.w1 >> 24) & 0xFF);
            if (segNum == targetSeg) {
                // Replace with direct pointer (bit 0 = 0 → non-segmented)
                dlCopy[i].words.w1 = (uintptr_t)replacement;
                patched++;
            }
        }

        // G_DL_INDEX (0x3D) also references segments — check same pattern
        if (op == 0x3D) {
            uint8_t segNum = (uint8_t)((dlCopy[i].words.w1 >> 24) & 0xFF);
            if (segNum == targetSeg) {
                // Convert to standard G_DL with direct pointer
                dlCopy[i].words.w0 = (dlCopy[i].words.w0 & ~((uintptr_t)0xFF << 24)) | ((uintptr_t)0xDE << 24);
                dlCopy[i].words.w1 = (uintptr_t)replacement;
                patched++;
            }
        }

        // Skip data words of 2-instruction expanded OTR commands
        if (op == 0x20 || op == 0x31 || op == 0x32 || op == 0x33 || op == 0x36 || op == 0x35 || op == 0x42) {
            i++; // skip hash/data instruction
        }
    }

    return patched;
}

/**
 * Patch G_DL_INDEX commands for segment 0x0C with direct pointers to cull DLs.
 *
 * mm.o2r DLs contain G_DL_INDEX(0x3D) with seg=0x0C and idx=0 (gCullBackDList)
 * or idx=2 (gCullFrontDList). The interpreter converts idx to byte offset
 * (idx * sizeof(F3DGfx) = idx*16), then adds to mSegmentPointers[0x0C].
 * This ASSUMES gCullFrontDList is exactly gCullBackDList + 32 bytes in memory.
 *
 * In Release builds, the MSVC linker may NOT place these arrays adjacently,
 * causing the computed address to point to garbage → interpreter executes
 * garbage as GFX commands → crash in GfxSpVertex.
 *
 * This function replaces these G_DL_INDEX commands with direct G_DL pointers
 * to the actual gCullBackDList/gCullFrontDList C arrays, bypassing the
 * segment table + offset calculation entirely.
 */
static int MmForm_PatchCullDLIndex(Gfx* dlCopy, size_t count) {
    if (dlCopy == NULL)
        return 0;

    int patched = 0;

    for (size_t i = 0; i < count; i++) {
        uint8_t op = (uint8_t)((dlCopy[i].words.w0 >> 24) & 0xFF);

        if (op == 0x3D) {
            uint8_t segNum = (uint8_t)((dlCopy[i].words.w1 >> 24) & 0xFF);
            uint32_t idx = (uint32_t)(dlCopy[i].words.w1 & 0x00FFFFFF);

            if (segNum == 0x0C) {
                // idx=0 → gCullBackDList, idx=2 → gCullFrontDList
                // (In MM, gCullBackDList has 2 Gfx entries, gCullFrontDList follows at index 2)
                Gfx* target = (idx >= 2) ? gCullFrontDList : gCullBackDList;

                // Convert G_DL_INDEX → standard G_DL with direct pointer
                dlCopy[i].words.w0 = (dlCopy[i].words.w0 & ~((uintptr_t)0xFF << 24)) | ((uintptr_t)0xDE << 24);
                dlCopy[i].words.w1 = (uintptr_t)target;
                patched++;
            }
        }

        // Also handle standard G_DL (0xDE) with segment 0x0C (unlikely but defensive)
        if (op == 0xDE && (dlCopy[i].words.w1 & 1)) {
            uint8_t segNum = (uint8_t)((dlCopy[i].words.w1 >> 24) & 0xFF);
            if (segNum == 0x0C) {
                uint32_t offset = dlCopy[i].words.w1 & 0x00FFFFFE;
                // offset 0 → gCullBackDList, offset >= 0x10 (N64 bytes) → gCullFrontDList
                Gfx* target = (offset >= 0x10) ? gCullFrontDList : gCullBackDList;
                dlCopy[i].words.w1 = (uintptr_t)target;
                patched++;
            }
        }

        // Skip data words of 2-instruction expanded OTR commands
        if (op == 0x20 || op == 0x31 || op == 0x32 || op == 0x33 || op == 0x36 || op == 0x35 || op == 0x42) {
            i++; // skip hash/data instruction
        }
    }

    return patched;
}

static void MmForm_PreloadGoronDLs(void) {
    sCachedCurledDL = MmForm_LoadAndValidateDL(gLinkGoronCurledDL, sCurledDLSafeCopy);
    // Load individual sub-DLs instead of composite gLinkGoronRollingSpikesAndEffectDL.
    // MM draws spike geometry (DL_00C540) on POLY_OPA_DISP and energy effects
    // (DL_0127B0, DL_0134D0) on POLY_XLU_DISP with alpha blending.
    sCachedSpikeGeomDL = MmForm_LoadAndValidateDL(object_link_goron_DL_00C540, sSpikeGeomDLSafeCopy);
    sCachedEnergyEffect1DL = MmForm_LoadAndValidateDL(object_link_goron_DL_0127B0, sEnergyEffect1DLSafeCopy);
    sCachedEnergyEffect2DL = MmForm_LoadAndValidateDL(object_link_goron_DL_0134D0, sEnergyEffect2DLSafeCopy);
    sCachedPunchDL = MmForm_LoadAndValidateDL(gLinkGoronGoronPunchEffectDL, sPunchDLSafeCopy);

    // Store pristine instruction counts for per-frame copy allocation
    sCurledDLCount = sCurledDLSafeCopy.size();
    sSpikeGeomDLCount = sSpikeGeomDLSafeCopy.size();
    sEnergyEffect1DLCount = sEnergyEffect1DLSafeCopy.size();
    sEnergyEffect2DLCount = sEnergyEffect2DLSafeCopy.size();
    sPunchDLCount = sPunchDLSafeCopy.size();
    // Pre-resolve OTR hashes in each DL to warm the cache
    if (sCachedCurledDL) {
        MmForm_PreResolveDLHashes(sCachedCurledDL, "gLinkGoronCurledDL", 0);
    }
    if (sCachedSpikeGeomDL) {
        MmForm_PreResolveDLHashes(sCachedSpikeGeomDL, "object_link_goron_DL_00C540", 0);
    }
    if (sCachedEnergyEffect1DL) {
        MmForm_PreResolveDLHashes(sCachedEnergyEffect1DL, "object_link_goron_DL_0127B0", 0);
    }
    if (sCachedEnergyEffect2DL) {
        MmForm_PreResolveDLHashes(sCachedEnergyEffect2DL, "object_link_goron_DL_0134D0", 0);
    }
    if (sCachedPunchDL) {
        MmForm_PreResolveDLHashes(sCachedPunchDL, "gLinkGoronGoronPunchEffectDL", 0);
    }
}

static void MmForm_PreloadZoraDLs(void) {
    sCachedBarrierDL = MmForm_LoadAndValidateDL(gLinkZoraBarrierDL, sBarrierDLSafeCopy);
    sBarrierDLCount = sBarrierDLSafeCopy.size();
    if (sCachedBarrierDL) {
        MmForm_PreResolveDLHashes(sCachedBarrierDL, "gLinkZoraBarrierDL", 0);
    }

    // Forearm fin/shield DLs (from 2Ship z_player_lib.c func_80126BD0 line 3001)
    // Physical fin blades on forearms — drawn at LEFT_FOREARM and RIGHT_FOREARM in PostLimbDraw
    sCachedZoraFinLDL = MmForm_LoadAndValidateDL(gLinkZoraLeftForearmShieldDL, sZoraFinLDLSafeCopy);
    sZoraFinLDLCount = sZoraFinLDLSafeCopy.size();
    if (sCachedZoraFinLDL) {
        MmForm_PreResolveDLHashes(sCachedZoraFinLDL, "gLinkZoraLeftForearmShieldDL", 0);
    }
    sCachedZoraFinRDL = MmForm_LoadAndValidateDL(gLinkZoraRightForearmShieldDL, sZoraFinRDLSafeCopy);
    sZoraFinRDLCount = sZoraFinRDLSafeCopy.size();
    if (sCachedZoraFinRDL) {
        MmForm_PreResolveDLHashes(sCachedZoraFinRDL, "gLinkZoraRightForearmShieldDL", 0);
    }
}

static void MmForm_PreloadDekuDLs(void) {
    // Deku bubble projectile geometry DLs from MM gameplay_keep
    // From 2Ship z_en_arrow.c:716-738: ARROW_TYPE_DEKU_BUBBLE draw
    sCachedDekuBubbleStillDL = MmForm_LoadAndValidateDL(gMmDekuBubbleStillDL, sDekuBubbleStillDLSafeCopy);
    sDekuBubbleStillDLCount = sDekuBubbleStillDLSafeCopy.size();
    if (sCachedDekuBubbleStillDL) {
        MmForm_PreResolveDLHashes(sCachedDekuBubbleStillDL, "gameplay_keep_DL_06F9F0", 0);
    }
    sCachedDekuBubbleMoveDL = MmForm_LoadAndValidateDL(gMmDekuBubbleMoveDL, sDekuBubbleMoveDLSafeCopy);
    sDekuBubbleMoveDLCount = sDekuBubbleMoveDLSafeCopy.size();
    if (sCachedDekuBubbleMoveDL) {
        MmForm_PreResolveDLHashes(sCachedDekuBubbleMoveDL, "gameplay_keep_DL_06FAE0", 0);
    }
}

static void MmForm_PreloadFDHandDLs(void) {
    for (int i = 0; i < FD_DL_COUNT; i++) {
        sCachedFDHandDLs[i] = MmForm_LoadAndValidateDL(sFDHandDLPaths[i], sFDHandDLSafeCopies[i]);
        sFDHandDLCounts[i] = sFDHandDLSafeCopies[i].size();
        if (sCachedFDHandDLs[i]) {
            MmForm_PreResolveDLHashes(sCachedFDHandDLs[i], sFDHandDLPaths[i], 0);
            MMFORM_LOG("[MmForm] Loaded FD hand DL %d: %s (%zu instructions)", i, sFDHandDLPaths[i],
                       sFDHandDLCounts[i]);
        } else {
            MMFORM_LOG("[MmForm] WARNING: Failed to load FD hand DL %d: %s", i, sFDHandDLPaths[i]);
        }
    }
}

// Get a per-frame safe copy of an FD hand DL for rendering
static Gfx* MmForm_GetFDHandDL(PlayState* play, FDHandDLIndex index) {
    if (index < 0 || index >= FD_DL_COUNT || sCachedFDHandDLs[index] == NULL || sFDHandDLCounts[index] == 0)
        return NULL;

    // Allocate per-frame copy from Graph_Alloc (GFX interpreter modifies DLs in-place)
    size_t count = sFDHandDLCounts[index];
    Gfx* dlCopy = (Gfx*)Graph_Alloc(play->state.gfxCtx, (count + 1) * sizeof(Gfx));
    memcpy(dlCopy, sFDHandDLSafeCopies[index].data(), count * sizeof(Gfx));
    // Ensure G_ENDDL terminator
    gSPEndDisplayList(&dlCopy[count]);
    // Defensive: patch any segment 0x08 refs to gEmptyDL (safe no-op)
    MmForm_PatchSegmentedDL(dlCopy, count, 0x08, gEmptyDL);
    // Patch G_DL_INDEX seg 0x0C → direct pointers to cull DLs
    MmForm_PatchCullDLIndex(dlCopy, count);
    return dlCopy;
}

// Public getter for sword beam DL (called from transformation_masks.c → EN_M_THUNDER)
extern "C" Gfx* MmForm_GetFDSwordBeamDL(PlayState* play) {
    return MmForm_GetFDHandDL(play, FD_DL_SWORD_BEAM);
}

static void MmForm_ClearCachedDLs(void) {
    sCachedCurledDL = NULL;
    sCachedSpikeGeomDL = NULL;
    sCachedEnergyEffect1DL = NULL;
    sCachedEnergyEffect2DL = NULL;
    sCachedPunchDL = NULL;
    sCachedBarrierDL = NULL;
    sCachedZoraFinLDL = NULL;
    sCachedZoraFinRDL = NULL;
    sCurledDLCount = 0;
    sSpikeGeomDLCount = 0;
    sEnergyEffect1DLCount = 0;
    sEnergyEffect2DLCount = 0;
    sPunchDLCount = 0;
    sBarrierDLCount = 0;
    sZoraFinLDLCount = 0;
    sZoraFinRDLCount = 0;
    sCurledDLSafeCopy.clear();
    sSpikeGeomDLSafeCopy.clear();
    sEnergyEffect1DLSafeCopy.clear();
    sEnergyEffect2DLSafeCopy.clear();
    sPunchDLSafeCopy.clear();
    sBarrierDLSafeCopy.clear();
    sZoraFinLDLSafeCopy.clear();
    sZoraFinRDLSafeCopy.clear();
    sCachedDekuBubbleStillDL = NULL;
    sCachedDekuBubbleMoveDL = NULL;
    sDekuBubbleStillDLCount = 0;
    sDekuBubbleMoveDLCount = 0;
    sDekuBubbleStillDLSafeCopy.clear();
    sDekuBubbleMoveDLSafeCopy.clear();
    // FD hand DLs
    for (int i = 0; i < FD_DL_COUNT; i++) {
        sCachedFDHandDLs[i] = NULL;
        sFDHandDLCounts[i] = 0;
        sFDHandDLSafeCopies[i].clear();
    }
}

// Shield collider init (from 2Ship D_8085C318, z_player.c line 1686-1704)
// COL_MATERIAL_METAL for metal bounce SFX, AC_HARD blocks attacks
static ColliderCylinderInit sShieldColliderInit = {
    {
        COLTYPE_METAL,
        AT_NONE,
        AC_ON | AC_HARD | AC_TYPE_ENEMY,
        OC1_NONE,
        OC2_TYPE_PLAYER,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK2,
        { 0x00100000, 0x00, 0x02 },
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_NONE,
        BUMP_ON,
        OCELEM_NONE,
    },
    { 30, 35, 0, { 0, 0, 0 } },
};

// =============================================================================
// Internal Helpers
// =============================================================================

static MmPlayerTransformation MmForm_MaskIdToForm(TransformMaskId maskId) {
    switch (maskId) {
        case TRANSFORM_MASK_GORON:
            return MM_PLAYER_FORM_GORON;
        case TRANSFORM_MASK_ZORA:
            return MM_PLAYER_FORM_ZORA;
        case TRANSFORM_MASK_DEKU:
            return MM_PLAYER_FORM_DEKU;
        case TRANSFORM_MASK_FIERCE_DEITY:
            return MM_PLAYER_FORM_FIERCE_DEITY;
        default:
            return MM_PLAYER_FORM_HUMAN;
    }
}

// Forward declarations (defined after MmForm_LoadFormSkeleton)
static void MmForm_FreeRootMotion(void);
static void MmForm_LoadPunchRootMotion(u8 punchIndex, MmAnimId animId);

static u8 MmForm_LoadFormSkeleton(PlayState* play, MmPlayerTransformation form) {
    const MmFormProperties* props = &sFormProps[form];

    if (props->skelPath == NULL) {
        MMFORM_LOG("[MmForm] No skeleton for form %d", form);
        return 0;
    }

    try {

        // Load skeleton from mm.o2r
        FlexSkeletonHeader* skelHeader = (FlexSkeletonHeader*)MmAssets_LoadResource(props->skelPath);
        if (skelHeader == NULL) {
            MMFORM_LOG("[MmForm] FAIL: skeleton not found in mm.o2r: %s", props->skelPath);
            MMFORM_LOG("[MmForm] Check: mm.o2r loaded=%d, available=%d", MmAssets_IsLoaded(), MmAssets_IsAvailable());
            return 0;
        }

        // Load idle animation
        gFormState.idleAnim = MmAnim_LoadByPath(props->idleAnimPath, props->idleAnimFrames, (u8)props->limbCount);
        if (gFormState.idleAnim == NULL) {
            MMFORM_LOG("[MmForm] FAIL: idle anim not found in mm.o2r: %s", props->idleAnimPath);
            MMFORM_LOG("[MmForm] This is FATAL - cannot transform without idle animation");
            return 0;
        }

        // Load walk animation
        gFormState.walkAnim = NULL;
        if (props->walkAnimPath != NULL) {
            gFormState.walkAnim = MmAnim_LoadByPath(props->walkAnimPath, props->walkAnimFrames, (u8)props->limbCount);
            if (gFormState.walkAnim == NULL) {}
        }

        // Load run animation (from 2Ship D_8085BE84: all forms share link_normal_run_free)
        gFormState.runAnim = NULL;
        if (props->runAnimPath != NULL) {
            gFormState.runAnim = MmAnim_LoadByPath(props->runAnimPath, props->runAnimFrames, (u8)props->limbCount);
            if (gFormState.runAnim == NULL) {}
        }

        // =========================================================================
        // Phase 2: Batch load form-specific combat animations
        // =========================================================================

        // Clear all combat anim pointers and root motion data
        gFormState.punchA = gFormState.punchB = gFormState.punchC = NULL;
        gFormState.punchAEnd = gFormState.punchBEnd = gFormState.punchCEnd = NULL;
        gFormState.punchAEndR = gFormState.punchBEndR = gFormState.punchCEndR = NULL;
        gFormState.maruChange = NULL;
        gFormState.climbUpL = gFormState.climbUpR = NULL;
        gFormState.maskOffStart = NULL;
        gFormState.doorAOpen = gFormState.doorBOpen = gFormState.chestOpen = NULL;
        gFormState.defenseAnim = gFormState.defenseWaitAnim = gFormState.defenseEndAnim = NULL;
        MmForm_FreeRootMotion();

        if (form == MM_PLAYER_FORM_GORON) {
            // Punch combo (from 2Ship D_8085D064, z_player.c line 3569-3574)
            gFormState.punchA = MmAnim_Load(MM_ANIM_PG_PUNCHA);
            gFormState.punchB = MmAnim_Load(MM_ANIM_PG_PUNCHB);
            gFormState.punchC = MmAnim_Load(MM_ANIM_PG_PUNCHC);
            gFormState.punchAEnd = MmAnim_Load(MM_ANIM_PG_PUNCHAEND);
            gFormState.punchBEnd = MmAnim_Load(MM_ANIM_PG_PUNCHBEND);
            gFormState.punchCEnd = MmAnim_Load(MM_ANIM_PG_PUNCHCEND);
            gFormState.punchAEndR = MmAnim_Load(MM_ANIM_PG_PUNCHAENDR);
            gFormState.punchBEndR = MmAnim_Load(MM_ANIM_PG_PUNCHBENDR);
            gFormState.punchCEndR = MmAnim_Load(MM_ANIM_PG_PUNCHCENDR);

            // Curl -> ball (for roll system, Phase 6)
            gFormState.maruChange = MmAnim_Load(MM_ANIM_PG_MARU_CHANGE);

            // Wall/vine climbing animations (from 2Ship ageProperties line 908-918)
            gFormState.climbStartA = MmAnim_Load(MM_ANIM_PG_CLIMB_STARTA);
            gFormState.climbStartB = MmAnim_Load(MM_ANIM_PG_CLIMB_STARTB);
            gFormState.climbUpL = MmAnim_Load(MM_ANIM_PG_CLIMB_UPL);
            gFormState.climbUpR = MmAnim_Load(MM_ANIM_PG_CLIMB_UPR);
            gFormState.climbEndAL = MmAnim_Load(MM_ANIM_PG_CLIMB_ENDAL);
            gFormState.climbEndAR = MmAnim_Load(MM_ANIM_PG_CLIMB_ENDAR);
            gFormState.climbEndBL = MmAnim_Load(MM_ANIM_PG_CLIMB_ENDBL);
            gFormState.climbEndBR = MmAnim_Load(MM_ANIM_PG_CLIMB_ENDBR);

            // Mask removal (for detransformation)
            gFormState.maskOffStart = MmAnim_Load(MM_ANIM_PG_MASKOFFSTART);

            s32 loaded = 0;
            if (gFormState.punchA)
                loaded++;
            if (gFormState.punchB)
                loaded++;
            if (gFormState.punchC)
                loaded++;
            if (gFormState.punchAEnd)
                loaded++;
            if (gFormState.punchBEnd)
                loaded++;
            if (gFormState.punchCEnd)
                loaded++;
            if (gFormState.punchAEndR)
                loaded++;
            if (gFormState.punchBEndR)
                loaded++;
            if (gFormState.punchCEndR)
                loaded++;
            if (gFormState.maruChange)
                loaded++;
            if (gFormState.maskOffStart)
                loaded++;

            // Root motion for Goron punches (from 2Ship: ANIM_FLAG_ENABLE_MOVEMENT)
            MmForm_LoadPunchRootMotion(0, MM_ANIM_PG_PUNCHA);
            MmForm_LoadPunchRootMotion(1, MM_ANIM_PG_PUNCHB);
            MmForm_LoadPunchRootMotion(2, MM_ANIM_PG_PUNCHC);

            // Load shielding skeleton (from 2Ship z_player.c line 11180-11182)
            // Separate skeleton with 4 limbs: Root, Body, Head, ArmsAndLegs
            // Uses gLinkGoronShieldingAnim ("pg_gurdmotion" = guard motion pose)
            {
                FlexSkeletonHeader* shieldSkel = (FlexSkeletonHeader*)MmAssets_LoadResource(gLinkGoronShieldingSkel);
                AnimationHeader* shieldAnim = (AnimationHeader*)MmAssets_LoadResource(gLinkGoronShieldingAnim);
                if (shieldSkel != NULL && shieldAnim != NULL) {
                    SkelAnime_InitFlex(play, &gFormState.shieldSkelAnime, shieldSkel, shieldAnim, NULL, NULL,
                                       LINK_GORON_SHIELDING_LIMB_MAX);
                    gFormState.shieldSkelLoaded = 1;
                } else {
                    gFormState.shieldSkelLoaded = 0;
                }
            }

            // Initialize shield damage protection collider (from 2Ship D_8085C318)
            // This collider blocks enemy attacks when Goron is curled/shielding
            {
                Player* initPlayer = (Player*)play->actorCtx.actorLists[ACTORCAT_PLAYER].head;
                if (initPlayer != NULL) {
                    Collider_InitCylinder(play, &gFormState.shieldCollider);
                    Collider_SetCylinder(play, &gFormState.shieldCollider, &initPlayer->actor, &sShieldColliderInit);
                    gFormState.shieldColliderInitDone = 1;
                }
            }

            // Door/chest animations (from 2Ship D_8085D118/D_8085D124/ageProperties->openChestAnim)
            gFormState.doorAOpen = MmAnim_Load(MM_ANIM_PG_DOORA_OPEN);
            gFormState.doorBOpen = MmAnim_Load(MM_ANIM_PG_DOORB_OPEN);
            gFormState.chestOpen = MmAnim_Load(MM_ANIM_PG_TBOX_OPEN);
        } else if (form == MM_PLAYER_FORM_ZORA) {
            // Zora punch combo (from 2Ship sMeleeAttackAnimInfo, z_player.c line 3575-3580)
            gFormState.punchA = MmAnim_Load(MM_ANIM_PZ_ATTACKA);
            gFormState.punchB = MmAnim_Load(MM_ANIM_PZ_ATTACKB);
            gFormState.punchC = MmAnim_Load(MM_ANIM_PZ_ATTACKC);
            gFormState.punchAEnd = MmAnim_Load(MM_ANIM_PZ_ATTACKAEND);
            gFormState.punchBEnd = MmAnim_Load(MM_ANIM_PZ_ATTACKBEND);
            gFormState.punchCEnd = MmAnim_Load(MM_ANIM_PZ_ATTACKCEND);
            gFormState.punchAEndR = MmAnim_Load(MM_ANIM_PZ_ATTACKAENDR);
            gFormState.punchBEndR = MmAnim_Load(MM_ANIM_PZ_ATTACKBENDR);
            gFormState.punchCEndR = MmAnim_Load(MM_ANIM_PZ_ATTACKCENDR);

            // Zora mask removal (for detransformation)
            gFormState.maskOffStart = MmAnim_Load(MM_ANIM_PZ_MASKOFFSTART);

            // Zora-specific jump kick (Phase 3: aerial B attack)
            // From 2Ship sMeleeAttackAnimInfo: PLAYER_MWA_ZORA_JUMPKICK_START
            // Gravity override: -0.8f (lighter than default, from 2Ship func_80834734 line 6357)
            gFormState.jumpKick = MmAnim_Load(MM_ANIM_PZ_JUMPAT);
            gFormState.jumpKickEnd = MmAnim_Load(MM_ANIM_PZ_JUMPATEND);

            s32 loaded = 0;
            if (gFormState.punchA)
                loaded++;
            if (gFormState.punchB)
                loaded++;
            if (gFormState.punchC)
                loaded++;
            if (gFormState.punchAEnd)
                loaded++;
            if (gFormState.punchBEnd)
                loaded++;
            if (gFormState.punchCEnd)
                loaded++;
            if (gFormState.punchAEndR)
                loaded++;
            if (gFormState.punchBEndR)
                loaded++;
            if (gFormState.punchCEndR)
                loaded++;
            if (gFormState.maskOffStart)
                loaded++;
            if (gFormState.jumpKick)
                loaded++;
            if (gFormState.jumpKickEnd)
                loaded++;

            // Root motion for Zora punches (from 2Ship: ANIM_FLAG_ENABLE_MOVEMENT)
            MmForm_LoadPunchRootMotion(0, MM_ANIM_PZ_ATTACKA);
            MmForm_LoadPunchRootMotion(1, MM_ANIM_PZ_ATTACKB);
            MmForm_LoadPunchRootMotion(2, MM_ANIM_PZ_ATTACKC);

            // Boomerang fin animations (from 2Ship Player_InitZoraBoomerangIA, z_player.c:3470)
            gFormState.cutterAttack = MmAnim_Load(MM_ANIM_PZ_CUTTERATTACK);
            gFormState.cutterCatch = MmAnim_Load(MM_ANIM_PZ_CUTTERCATCH);
            gFormState.cutterWaitA = MmAnim_Load(MM_ANIM_PZ_CUTTERWAITA);
            gFormState.cutterWaitB = MmAnim_Load(MM_ANIM_PZ_CUTTERWAITB);
            gFormState.cutterWaitC = MmAnim_Load(MM_ANIM_PZ_CUTTERWAITC);
            gFormState.cutterWaitAnim = MmAnim_Load(MM_ANIM_PZ_CUTTERWAITANIM);
            gFormState.bladeOn = MmAnim_Load(MM_ANIM_PZ_BLADEON);

            // Swimming animations (from 2Ship Player_Action_54-58, z_player.c:16820-17072)
            gFormState.fishSwim = MmAnim_Load(MM_ANIM_PZ_FISHSWIM);
            gFormState.waterRoll = MmAnim_Load(MM_ANIM_PZ_WATERROLL);
            gFormState.swimToWait = MmAnim_Load(MM_ANIM_PZ_SWIMTOWAIT);
            gFormState.swimWaitAnim = MmAnim_Load(MM_ANIM_LINK_SWIMER_SWIM_WAIT);
            gFormState.swimAnim = MmAnim_Load(MM_ANIM_LINK_SWIMER_SWIM);

            // Defense/guard animations (from 2Ship Player_ActionHandler_11, z_player.c:8542)
            // Zora (!Player_IsGoronOrDeku) uses D_8085BE84[PLAYER_ANIMGROUP_defense][modelAnimType]
            // From 2Ship D_8085BE84[PLAYER_ANIMGROUP_defense][PLAYER_ANIMTYPE_2]:
            //   Zora uses ARMED variants because Player_SetModelsForHoldingShield
            //   sets modelAnimType = PLAYER_ANIMTYPE_2 for non-Goron/Deku forms.
            //   Fallback to _free variants if armed ones don't load from mm.o2r.
            gFormState.defenseAnim = MmAnim_Load(MM_ANIM_LINK_NORMAL_DEFENSE);
            if (gFormState.defenseAnim == NULL) {
                gFormState.defenseAnim = MmAnim_Load(MM_ANIM_LINK_NORMAL_DEFENSE_FREE);
            }
            gFormState.defenseWaitAnim = MmAnim_Load(MM_ANIM_LINK_NORMAL_DEFENSE_WAIT);
            if (gFormState.defenseWaitAnim == NULL) {
                gFormState.defenseWaitAnim = MmAnim_Load(MM_ANIM_LINK_NORMAL_DEFENSE_WAIT_FREE);
            }
            gFormState.defenseEndAnim = MmAnim_Load(MM_ANIM_LINK_NORMAL_DEFENSE_END);
            if (gFormState.defenseEndAnim == NULL) {
                gFormState.defenseEndAnim = MmAnim_Load(MM_ANIM_LINK_NORMAL_DEFENSE_END_FREE);
            }
            MMFORM_LOG("[MmForm] Zora defense anims: enter=%s wait=%s end=%s", gFormState.defenseAnim ? "OK" : "FAIL",
                       gFormState.defenseWaitAnim ? "OK" : "FAIL", gFormState.defenseEndAnim ? "OK" : "FAIL");

            // Climb animations (from 2Ship D_8085BE84 PLAYER_ANIMTYPE_DEFAULT)
            gFormState.climbStartA = MmAnim_Load(MM_ANIM_PZ_CLIMB_STARTA);
            gFormState.climbStartB = MmAnim_Load(MM_ANIM_PZ_CLIMB_STARTB);
            gFormState.climbEndAL = MmAnim_Load(MM_ANIM_PZ_CLIMB_ENDAL);
            gFormState.climbEndAR = MmAnim_Load(MM_ANIM_PZ_CLIMB_ENDAR);
            gFormState.climbEndBL = MmAnim_Load(MM_ANIM_PZ_CLIMB_ENDBL);
            gFormState.climbEndBR = MmAnim_Load(MM_ANIM_PZ_CLIMB_ENDBR);
            gFormState.climbUpL = MmAnim_Load(MM_ANIM_PZ_CLIMB_UPL);
            gFormState.climbUpR = MmAnim_Load(MM_ANIM_PZ_CLIMB_UPR);

            // Door/chest animations (Zora-specific from 2Ship D_8085D118/D_8085D124)
            gFormState.doorAOpen = MmAnim_Load(MM_ANIM_PZ_DOORA_OPEN);
            gFormState.doorBOpen = MmAnim_Load(MM_ANIM_PZ_DOORB_OPEN);
            gFormState.chestOpen = MmAnim_Load(MM_ANIM_PZ_TBOX_OPEN);

            // Initialize shield damage protection collider for Zora guard stance
            // Same collider as Goron (from 2Ship D_8085C318, z_player.c line 1686)
            // Zora uses shieldCylinder for both defense (AC) and barrier (AT) in MM
            {
                Player* initPlayer = (Player*)play->actorCtx.actorLists[ACTORCAT_PLAYER].head;
                if (initPlayer != NULL) {
                    Collider_InitCylinder(play, &gFormState.shieldCollider);
                    Collider_SetCylinder(play, &gFormState.shieldCollider, &initPlayer->actor, &sShieldColliderInit);
                    gFormState.shieldColliderInitDone = 1;
                }
            }
        } else if (form == MM_PLAYER_FORM_DEKU) {
            // Deku mask removal (for detransformation)
            gFormState.maskOffStart = MmAnim_Load(MM_ANIM_PN_MASKOFFSTART);

            // Deku spin attack (from 2Ship Player_Action_95, z_player.c line 19276)
            // Triggered by A button on ground (from func_80839A84 line 8223)
            gFormState.dekuSpinAttack = MmAnim_Load(MM_ANIM_PN_ATTACK);

            // Deku bubble spit (from 2Ship func_808306F8 / Player_UpperAction_7)
            // pn_tamahakidf = walk2ready/aim pose, pn_tamahaki = shooting motion
            gFormState.dekuBowReady = MmAnim_Load(MM_ANIM_PN_TAMAHAKIDF);
            gFormState.dekuBowShoot = MmAnim_Load(MM_ANIM_PN_TAMAHAKI);

            // Deku guard pose (from 2Ship Player_ActionHandler_11, z_player.c line 8544)
            // Plays from frame 0 (not endFrame like Zora/Human). Shield DL scales in during frames 0-3.
            gFormState.dekuGuardAnim = MmAnim_Load(MM_ANIM_PN_GURD);

            // Deku flower/flight animations (from 2Ship Player_Action_93/94)
            gFormState.dekuFlightLaunch = MmAnim_Load(MM_ANIM_PN_KAKKU);     // 12 frames - launch spin
            gFormState.dekuFlightFlutter = MmAnim_Load(MM_ANIM_PN_BATABATA); // 14 frames - flutter glide loop
            gFormState.dekuFlightLand = MmAnim_Load(MM_ANIM_PN_KAKKUFINISH); // 15 frames - close flower land
            gFormState.dekuFlightFall = MmAnim_Load(MM_ANIM_PN_RAKKAFINISH); // 11 frames - fall recovery

            MMFORM_LOG("[MmForm] Deku anims: spin=%s, bowReady=%s, bowShoot=%s, guard=%s",
                       gFormState.dekuSpinAttack ? "OK" : "FAIL", gFormState.dekuBowReady ? "OK" : "FAIL",
                       gFormState.dekuBowShoot ? "OK" : "FAIL", gFormState.dekuGuardAnim ? "OK" : "FAIL");
            MMFORM_LOG("[MmForm] Deku flight anims: launch=%s, flutter=%s, land=%s, fall=%s",
                       gFormState.dekuFlightLaunch ? "OK" : "FAIL", gFormState.dekuFlightFlutter ? "OK" : "FAIL",
                       gFormState.dekuFlightLand ? "OK" : "FAIL", gFormState.dekuFlightFall ? "OK" : "FAIL");

            // Climb animations (from 2Ship ageProperties: Deku uses clink_normal_climb_* = child Link)
            gFormState.climbStartA = MmAnim_Load(MM_ANIM_CLINK_NORMAL_CLIMB_STARTA);
            gFormState.climbStartB = MmAnim_Load(MM_ANIM_CLINK_NORMAL_CLIMB_STARTB);
            gFormState.climbUpL = MmAnim_Load(MM_ANIM_CLINK_NORMAL_CLIMB_UPL);
            gFormState.climbUpR = MmAnim_Load(MM_ANIM_CLINK_NORMAL_CLIMB_UPR);
            gFormState.climbEndAL = MmAnim_Load(MM_ANIM_CLINK_NORMAL_CLIMB_ENDAL);
            gFormState.climbEndAR = MmAnim_Load(MM_ANIM_CLINK_NORMAL_CLIMB_ENDAR);
            gFormState.climbEndBL = MmAnim_Load(MM_ANIM_CLINK_NORMAL_CLIMB_ENDBL);
            gFormState.climbEndBR = MmAnim_Load(MM_ANIM_CLINK_NORMAL_CLIMB_ENDBR);

            // Door animations (from 2Ship D_8085D118/D_8085D124: pn_doorA/B_open)
            gFormState.doorAOpen = MmAnim_Load(MM_ANIM_PN_DOORA_OPEN);
            gFormState.doorBOpen = MmAnim_Load(MM_ANIM_PN_DOORB_OPEN);

            // Chest animation (from 2Ship ageProperties: pn_Tbox_open)
            gFormState.chestOpen = MmAnim_Load(MM_ANIM_PN_TBOX_OPEN);

        } else if (form == MM_PLAYER_FORM_FIERCE_DEITY) {
            // Fierce Deity mask removal (for detransformation)
            // From 2Ship D_8085D160[PLAYER_FORM_FIERCE_DEITY] = gPlayerAnim_pz_maskoffstart
            // FD shares the same mask-off animation as Zora
            gFormState.maskOffStart = MmAnim_Load(MM_ANIM_PZ_MASKOFFSTART);

            // Climb animations (from 2Ship ageProperties: FD uses clink_normal_climb_* = child Link)
            gFormState.climbStartA = MmAnim_Load(MM_ANIM_CLINK_NORMAL_CLIMB_STARTA);
            gFormState.climbStartB = MmAnim_Load(MM_ANIM_CLINK_NORMAL_CLIMB_STARTB);
            gFormState.climbUpL = MmAnim_Load(MM_ANIM_CLINK_NORMAL_CLIMB_UPL);
            gFormState.climbUpR = MmAnim_Load(MM_ANIM_CLINK_NORMAL_CLIMB_UPR);
            gFormState.climbEndAL = MmAnim_Load(MM_ANIM_CLINK_NORMAL_CLIMB_ENDAL);
            gFormState.climbEndAR = MmAnim_Load(MM_ANIM_CLINK_NORMAL_CLIMB_ENDAR);
            gFormState.climbEndBL = MmAnim_Load(MM_ANIM_CLINK_NORMAL_CLIMB_ENDBL);
            gFormState.climbEndBR = MmAnim_Load(MM_ANIM_CLINK_NORMAL_CLIMB_ENDBR);

            // Door animations (from 2Ship D_8085BE84: FD uses standard link_demo_doorA/B_link_free)
            gFormState.doorAOpen = MmAnim_Load(MM_ANIM_LINK_DEMO_DOORA_LINK_FREE);
            gFormState.doorBOpen = MmAnim_Load(MM_ANIM_LINK_DEMO_DOORB_LINK_FREE);

            // Chest animation (from 2Ship ageProperties: clink_demo_Tbox_open)
            gFormState.chestOpen = MmAnim_Load(MM_ANIM_CLINK_DEMO_TBOX_OPEN);
        }

        // Shared damage/landing animations (all forms use human Link anims)
        // From 2Ship D_8085D0D4[] table (z_player.c line 5863):
        // 8 knockback anims: [small front, small front lockon, small back, small back lockon,
        //                     big front, big front lockon, big back, big back lockon]
        gFormState.dmgAnims[0] = MmAnim_Load(MM_ANIM_LINK_NORMAL_FRONT_SHIT);  // front, small, no lockon
        gFormState.dmgAnims[1] = MmAnim_Load(MM_ANIM_LINK_NORMAL_FRONT_SHITR); // front, small, lockon
        gFormState.dmgAnims[2] = MmAnim_Load(MM_ANIM_LINK_NORMAL_BACK_SHIT);   // back, small, no lockon
        gFormState.dmgAnims[3] = MmAnim_Load(MM_ANIM_LINK_NORMAL_BACK_SHITR);  // back, small, lockon
        gFormState.dmgAnims[4] = MmAnim_Load(MM_ANIM_LINK_NORMAL_FRONT_HIT);   // front, big, no lockon
        gFormState.dmgAnims[5] = MmAnim_Load(MM_ANIM_LINK_ANCHOR_FRONT_HITR);  // front, big, lockon
        gFormState.dmgAnims[6] = MmAnim_Load(MM_ANIM_LINK_NORMAL_BACK_HIT);    // back, big, no lockon
        gFormState.dmgAnims[7] = MmAnim_Load(MM_ANIM_LINK_ANCHOR_BACK_HITR);   // back, big, lockon
        // Strong knockback anims (from 2Ship func_80833B18 line 5843-5847)
        gFormState.frontDownA = MmAnim_Load(MM_ANIM_LINK_NORMAL_FRONT_DOWNA); // launched forward
        gFormState.backDownA = MmAnim_Load(MM_ANIM_LINK_NORMAL_BACK_DOWNA);   // launched backward
        gFormState.landing = MmAnim_Load(MM_ANIM_LINK_NORMAL_LANDING);
        gFormState.shortLanding = MmAnim_Load(MM_ANIM_LINK_NORMAL_SHORT_LANDING);
        {
            s32 dmgLoaded = 0;
            for (s32 i = 0; i < 8; i++) {
                if (gFormState.dmgAnims[i])
                    dmgLoaded++;
            }
            if (gFormState.landing)
                dmgLoaded++;
            if (gFormState.shortLanding)
                dmgLoaded++;
        }

        // =========================================================================
        // Shared ground action animations (all forms use human Link anims)
        // From 2Ship D_8085BE84: Zora uses column 0 (PLAYER_ANIMTYPE_DEFAULT) for all shared anims
        // =========================================================================
        gFormState.jumpAnim = MmAnim_Load(MM_ANIM_LINK_NORMAL_JUMP);
        gFormState.fallAnim = MmAnim_Load(MM_ANIM_LINK_NORMAL_FALL);
        gFormState.rollAnim = MmAnim_Load(MM_ANIM_LINK_NORMAL_LANDING_ROLL_FREE);

        // Z-target (from 2Ship D_8085BE84[PLAYER_ANIMTYPE_DEFAULT])
        gFormState.ztargetIdleR = MmAnim_Load(MM_ANIM_LINK_NORMAL_WAITR_FREE);
        gFormState.ztargetIdleL = MmAnim_Load(MM_ANIM_LINK_NORMAL_WAITL_FREE);
        gFormState.ztargetSideWalkL = MmAnim_Load(MM_ANIM_LINK_NORMAL_SIDE_WALKL_FREE);
        gFormState.ztargetSideWalkR = MmAnim_Load(MM_ANIM_LINK_NORMAL_SIDE_WALKR_FREE);
        gFormState.ztargetBackWalk = MmAnim_Load(MM_ANIM_LINK_NORMAL_BACK_WALK);

        // Evasive maneuvers (from 2Ship Player_Action_29 / Player_Action_10)
        gFormState.sidehopL = MmAnim_Load(MM_ANIM_LINK_FIGHTER_LSIDE_JUMP);
        gFormState.sidehopLEnd = MmAnim_Load(MM_ANIM_LINK_FIGHTER_LSIDE_JUMP_END);
        gFormState.sidehopR = MmAnim_Load(MM_ANIM_LINK_FIGHTER_RSIDE_JUMP);
        gFormState.sidehopREnd = MmAnim_Load(MM_ANIM_LINK_FIGHTER_RSIDE_JUMP_END);
        gFormState.backflip = MmAnim_Load(MM_ANIM_LINK_FIGHTER_BACKTURN_JUMP);
        gFormState.backflipEnd = MmAnim_Load(MM_ANIM_LINK_FIGHTER_BACKTURN_JUMP_END);

        // Ledge grab/climb
        gFormState.ledgeHang = MmAnim_Load(MM_ANIM_LINK_NORMAL_JUMP_CLIMB_HOLD_FREE);
        gFormState.ledgeClimb = MmAnim_Load(MM_ANIM_LINK_NORMAL_JUMP_CLIMB_UP_FREE);
        gFormState.ledgeHangWait = MmAnim_Load(MM_ANIM_LINK_NORMAL_JUMP_CLIMB_WAIT_FREE);

        // Jump kick: form-specific (loaded per-form above) or NULL
        // Default NULL; Zora overrides above set pz_jumpAT/pz_jumpATend
        if (gFormState.jumpKick == NULL) {
            // Non-Zora forms: no aerial B attack (Goron has ground pound instead)
        }

        {
            s32 groundLoaded = 0;
            if (gFormState.jumpAnim)
                groundLoaded++;
            if (gFormState.fallAnim)
                groundLoaded++;
            if (gFormState.rollAnim)
                groundLoaded++;
            if (gFormState.ztargetIdleR)
                groundLoaded++;
            if (gFormState.ztargetIdleL)
                groundLoaded++;
            if (gFormState.ztargetSideWalkL)
                groundLoaded++;
            if (gFormState.ztargetSideWalkR)
                groundLoaded++;
            if (gFormState.ztargetBackWalk)
                groundLoaded++;
            if (gFormState.sidehopL)
                groundLoaded++;
            if (gFormState.sidehopR)
                groundLoaded++;
            if (gFormState.backflip)
                groundLoaded++;
            if (gFormState.ledgeHang)
                groundLoaded++;
            if (gFormState.ledgeClimb)
                groundLoaded++;
        }

        // Initialize SkelAnime with MM skeleton
        // Pass NULL for jointTable/morphTable to let SkelAnime_InitLink allocate them.
        // This avoids the limbBufCount == limbCount assertion (flags=9 adds +1 for root).
        gFormState.formLimbCount = props->limbCount;

        SkelAnime_InitLink(play, &gFormState.formSkelAnime, skelHeader, gFormState.idleAnim, 9, NULL, NULL,
                           props->limbCount);

        gFormState.formDListCount = gFormState.formSkelAnime.dListCount;
        gFormState.skeletonLoaded = 1;
        gFormState.goronAction = GORON_ACT_IDLE;
        gFormState.actionTimer = 0;
        gFormState.wasOnGround = 1;
        gFormState.jumpKickActive = 0;
        gFormState.sidehopDir = 0;
        gFormState.rollSpeed = 0.0f;
        gFormState.dekuHopsRemaining = 5; // From 2Ship z_player.c line 7573: resets to 5

        // Reset Deku flower/flight state
        gFormState.dekuFlowerDepth = 0.0f;
        gFormState.dekuFlowerVelocity = 0.0f;
        gFormState.dekuFlowerPhase = 0;
        gFormState.dekuFlowerCharge = 0;
        gFormState.dekuBudCounter = 0;
        gFormState.dekuLaunchPos = { 0.0f, 0.0f, 0.0f };
        gFormState.dekuFlightFlags = 0;
        gFormState.dekuPetalSpeed = 0;
        gFormState.dekuPetalAngle = 0;
        gFormState.dekuPitchAngle = 0;
        gFormState.dekuRollAngle = 0;
        gFormState.dekuFlightTimer = 0;
        gFormState.dekuFlightLaunchType = 0;
        gFormState.dekuSparkleAcc = 0;
        gFormState.dekuSavedShadowScale = 0.0f;

        // Deep-pin ALL form object resources from mm.o2r so they stay in cache.
        // The Fast3D interpreter patches DL instructions IN-PLACE with resolved raw pointers.
        // If the underlying VTX/TEX resources get evicted from cache, these pointers dangle → crash.
        // Pinning keeps all object_link_* resources alive for the duration of the form.
        MmForm_PinFormResources(form);
        gFormState.formDLsPinned = (sPinnedFormResources != nullptr) ? 1 : 0;

        // Form-specific: preload and validate special DLs
        if (form == MM_PLAYER_FORM_GORON) {
            MmForm_PreloadGoronDLs();
        } else if (form == MM_PLAYER_FORM_ZORA) {
            MmForm_ClearCachedDLs();
            MmForm_PreloadZoraDLs();
        } else if (form == MM_PLAYER_FORM_DEKU) {
            MmForm_ClearCachedDLs();
            MmForm_PreloadDekuDLs();
        } else if (form == MM_PLAYER_FORM_FIERCE_DEITY) {
            MmForm_ClearCachedDLs();
            MmForm_PreloadFDHandDLs();
        } else {
            MmForm_ClearCachedDLs();
        }

        // Initialize blink with random first interval (20-100 frames)
        gFormState.blinkTimer = 20 + (s16)(Rand_ZeroFloat(80.0f));
        gFormState.eyeIndex = 0;
        return 1;

    } catch (const std::exception& e) {
        MMFORM_LOG("[MmForm] Exception in LoadFormSkeleton(form=%d): %s", (int)form, e.what());
        return 0;
    } catch (...) {
        MMFORM_LOG("[MmForm] Unknown exception in LoadFormSkeleton(form=%d)", (int)form);
        return 0;
    }
}

static void MmForm_ApplyFormProperties(Player* player, MmPlayerTransformation form) {
    const MmFormProperties* props = &sFormProps[form];

    // Save OOT state for restoration
    gFormState.savedMass = player->actor.colChkInfo.mass;
    gFormState.savedShadowScale = player->actor.shape.shadowScale;
    gFormState.savedAgeProperties = player->ageProperties;
    gFormState.savedStrength = CUR_UPG_VALUE(UPG_STRENGTH);
    gFormState.savedTunic = player->currentTunic;

    // Form-specific tunic effects:
    // Zora form grants Zora Tunic (breathe underwater, no drowning timer)
    // Goron form grants Goron Tunic (heat/lava resistance)
    if (form == MM_PLAYER_FORM_ZORA) {
        player->currentTunic = PLAYER_TUNIC_ZORA;
    } else if (form == MM_PLAYER_FORM_GORON) {
        player->currentTunic = PLAYER_TUNIC_GORON;
    }

    // FD: force max strength (Gold Gauntlets = 3) for lifting heavy objects
    if (form == MM_PLAYER_FORM_FIERCE_DEITY) {
        Inventory_ChangeUpgrade(UPG_STRENGTH, 3);
    }

    // Apply form properties
    player->actor.colChkInfo.mass = props->mass;
    player->actor.shape.shadowScale = props->shadowScale;
    // Force yOffset to 0 (from 2Ship: unk_ABC=0, unk_AC0=0 for all forms when standing)
    player->actor.shape.yOffset = 0.0f;

    // Shadow: keep OOT's DrawFeet. MmForm_PostLimbDraw updates feetPos[] via
    // Actor_SetFeetPos so the foot shadows track the transformed skeleton.
    // MmForm_UpdateActive switches to DrawCircle for ball/shield (no PostLimbDraw).

    // Apply collider dimensions
    player->cylinder.dim.radius = (s16)props->cylinderRadius;
    player->cylinder.dim.height = (s16)props->cylinderHeight;
    player->cylinder.dim.yShift = (s16)props->cylinderYShift;

    // === Override ageProperties for form-specific size checks ===
    // OOT reads player->ageProperties-> for ALL size-dependent gameplay:
    // ledge grab height (unk_14), wall/push/grab detection (wallCheckRadius),
    // ceiling collision (ceilingCheckHeight), step-up heights (unk_18/unk_1C),
    // water interaction (unk_10/unk_24/unk_2C), movement scale (unk_08), etc.
    // We copy the current ageProperties (preserving animation pointers for climb etc.)
    // then override dimension fields with EXACT values from MM decomp sPlayerAgeProperties.
    {
        // EXACT per-form values from MM decomp z_player.c lines 742-1221.
        // These are NOT proportional to adult Link - each form has its own tuned values.
        static const struct {
            f32 unk_04, unk_08, unk_0C, unk_10, unk_14, unk_18, unk_1C, unk_20;
            f32 unk_24, unk_28, unk_2C, unk_30, unk_34;
            f32 wallCheckRadius, unk_3C, unk_40;
            f32 ceilingCheckHeight;
        } sMmAgeProps[MM_PLAYER_FORM_MAX] = {
            // FIERCE_DEITY (MM z_player.c:742-837)
            { 90.0f, 1.5f, 166.5f, 105.0f, 119.100006f, 88.5f, 61.5f, 28.5f, 54.0f, 75.0f, 84.0f, 102.0f, 70.0f, 27.0f,
              24.75f, 105.0f, 84.0f },
            // GORON (MM z_player.c:838-933)
            { 90.0f, 0.74f, 111.0f, 70.0f, 79.4f, 59.0f, 41.0f, 19.0f, 36.0f, 50.0f, 56.0f, 68.0f, 70.0f, 19.5f, 18.2f,
              80.0f, 70.0f },
            // ZORA (MM z_player.c:934-1029)
            { 90.0f, 1.0f, 111.0f, 70.0f, 79.4f, 59.0f, 41.0f, 19.0f, 36.0f, 50.0f, 56.0f, 68.0f, 70.0f, 18.0f, 23.0f,
              70.0f, 56.0f },
            // DEKU (MM z_player.c:1030-1125)
            { 50.0f, 0.3f, 71.0f, 50.0f, 49.0f, 39.0f, 27.0f, 19.0f, 8.0f, 13.6f, 24.0f, 24.0f, 70.0f, 14.0f, 12.0f,
              55.0f, 35.0f },
            // HUMAN (unused by transformation system)
            { 60.0f, 11.0f / 17.0f, 71.0f, 50.0f, 49.0f, 39.0f, 27.0f, 19.0f, 22.0f, 32.4f, 32.0f, 48.0f,
              70.0f * (11.0f / 17.0f), 14.0f, 12.0f, 55.0f, 40.0f },
        };

        const auto* mmProps = &sMmAgeProps[form];

        memcpy(&gFormState.formAgeProperties, player->ageProperties, sizeof(PlayerAgeProperties));

        gFormState.formAgeProperties.ceilingCheckHeight = mmProps->ceilingCheckHeight;
        gFormState.formAgeProperties.unk_04 = mmProps->unk_04;
        gFormState.formAgeProperties.unk_08 = mmProps->unk_08;
        gFormState.formAgeProperties.unk_0C = mmProps->unk_0C;
        gFormState.formAgeProperties.unk_10 = mmProps->unk_10;
        gFormState.formAgeProperties.unk_14 = mmProps->unk_14;
        gFormState.formAgeProperties.unk_18 = mmProps->unk_18;
        gFormState.formAgeProperties.unk_1C = mmProps->unk_1C;
        gFormState.formAgeProperties.unk_20 = mmProps->unk_20;
        gFormState.formAgeProperties.unk_24 = mmProps->unk_24;
        gFormState.formAgeProperties.unk_28 = mmProps->unk_28;
        gFormState.formAgeProperties.unk_2C = mmProps->unk_2C;
        gFormState.formAgeProperties.unk_30 = mmProps->unk_30;
        gFormState.formAgeProperties.unk_34 = mmProps->unk_34;
        gFormState.formAgeProperties.wallCheckRadius = mmProps->wallCheckRadius;
        gFormState.formAgeProperties.unk_3C = mmProps->unk_3C;
        gFormState.formAgeProperties.unk_40 = mmProps->unk_40;

        // Override animation pointers so OOT's action handlers use form-correct timing.
        // unk_98 = chest open anim (used by Player_Action_65 for chest timing)
        // unk_AC[0..3] = climb up anims (used by climbing action for position movement)
        // unk_C4[0..1] = climb start anims (grabbing wall from bottom)
        // unk_CC[0..1] = climb end anims (reaching top of wall)
        if (gFormState.chestOpen != NULL) {
            gFormState.formAgeProperties.unk_98 = gFormState.chestOpen;
        }
        if (gFormState.climbUpL != NULL && gFormState.climbUpR != NULL) {
            gFormState.formAgeProperties.unk_AC[0] = gFormState.climbUpL;
            gFormState.formAgeProperties.unk_AC[1] = gFormState.climbUpR;
            // unk_AC[2..3] = forward climb (vines), reuse same anims as regular climb
            gFormState.formAgeProperties.unk_AC[2] = gFormState.climbUpL;
            gFormState.formAgeProperties.unk_AC[3] = gFormState.climbUpR;
        }
        if (gFormState.climbStartA != NULL && gFormState.climbStartB != NULL) {
            gFormState.formAgeProperties.unk_C4[0] = gFormState.climbStartA;
            gFormState.formAgeProperties.unk_C4[1] = gFormState.climbStartB;
        }
        if (gFormState.climbEndBL != NULL && gFormState.climbEndBR != NULL) {
            gFormState.formAgeProperties.unk_CC[0] = gFormState.climbEndBR;
            gFormState.formAgeProperties.unk_CC[1] = gFormState.climbEndBL;
        }

        player->ageProperties = &gFormState.formAgeProperties;
    }
}

static void MmForm_RestoreOotState(Player* player) {
    // Restore original ageProperties pointer (before form override)
    if (gFormState.savedAgeProperties != NULL) {
        player->ageProperties = gFormState.savedAgeProperties;
        gFormState.savedAgeProperties = NULL;
    }

    player->actor.colChkInfo.mass = gFormState.savedMass;
    player->actor.shape.shadowScale = gFormState.savedShadowScale;
    player->actor.shape.shadowDraw = ActorShadow_DrawFeet;

    // Restore OOT actor scale (FD sets 0.015f, OOT default is 0.01f)
    Actor_SetScale(&player->actor, 0.01f);

    // Restore original strength level (FD forces Gold Gauntlets)
    Inventory_ChangeUpgrade(UPG_STRENGTH, gFormState.savedStrength);

    // Restore original tunic (Zora/Goron form overrides it)
    player->currentTunic = gFormState.savedTunic;

    // Restore default OOT Link collider
    player->cylinder.dim.radius = 12;
    player->cylinder.dim.height = 50;
    player->cylinder.dim.yShift = 0;

    // Clear any leftover roll state flags
    player->stateFlags2 &= ~(PLAYER_STATE2_DISABLE_ROTATION_Z_TARGET | PLAYER_STATE2_DISABLE_ROTATION_ALWAYS);
    player->actor.bgCheckFlags &= ~0x800;
    // Restore OOT input (may have been blocked during roll) and camera state
    player->stateFlags1 &= ~(PLAYER_STATE1_INPUT_DISABLED | PLAYER_STATE1_JUMPING);
    // Reset shape rotation (may be left over from ball rolling)
    player->actor.shape.rot.x = 0;
    player->actor.shape.rot.z = 0;

    // Restore gravity — but keep swim gravity if underwater so the player doesn't sink
    // during the detransform flash. OOT will set proper gravity once it takes over.
    if (player->actor.yDistToWater > ZORA_SWIM_THRESHOLD) {
        player->actor.gravity = 0.0f;
    } else {
        player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
    }

    // Cleanup Zora barrier
    gFormState.barrierIntensity = 0;
    gFormState.barrierActive = 0;
    // Reset fog tint from barrier screen effect
    if (gPlayState != NULL) {
        gPlayState->envCtx.adjFogColor[0] = 0;
        gPlayState->envCtx.adjFogColor[1] = 0;
        gPlayState->envCtx.adjFogColor[2] = 0;
        gPlayState->envCtx.adjFogNear = 0;
    }
    // Note: barrier light is cleaned up in MmForm_Reset or by scene unload

    // Cleanup swim state
    gFormState.swimState = 0;
    gFormState.swimPitch = 0;
    gFormState.swimRoll = 0;
    gFormState.zoraBoots = 0;
    gFormState.fastSwimActive = 0;
    gFormState.swimRollSmoothed = 0;

    // Cleanup boomerang (En_Boom actors self-destruct, just clear our tracking)
    gFormState.boomerangState = 0;
    gFormState.boomerangActorL = NULL;
    gFormState.boomerangActorR = NULL;
    gFormState.boomerangAimYaw = 0;
    gFormState.boomerangAimPitch = 0;
    player->stateFlags1 &= ~(PLAYER_STATE1_BOOMERANG_THROWN | PLAYER_STATE1_PARALLEL);
    player->boomerangActor = NULL;
    player->upperLimbRot.y = 0;
    player->upperLimbRot.x = 0;

    // Cleanup Deku bubble state
    gFormState.bubble.active = 0;
    if (gFormState.bubbleColliderInit) {
        Collider_ResetCylinderAT(gPlayState, &gFormState.bubbleCollider.base);
    }
    gFormState.bubbleCharging = 0;
    gFormState.bubbleCharge = 0.0f;
    gFormState.bubbleChargeTimer = 0;
    gFormState.dekuHopsRemaining = 5; // Reset water hop counter
    // Exit first-person mode if we were in bubble aim
    player->unk_6AD = 0;
    player->stateFlags1 &= ~(PLAYER_STATE1_FIRST_PERSON | PLAYER_STATE1_ITEM_IN_HAND | PLAYER_STATE1_READY_TO_FIRE);
    player->unk_834 = 0;

    // Cleanup Deku flower/flight state
    gFormState.dekuFlowerDepth = 0.0f;
    gFormState.dekuFlowerVelocity = 0.0f;
    gFormState.dekuFlowerPhase = 0;
    gFormState.dekuFlowerCharge = 0;
    gFormState.dekuBudCounter = 0;
    gFormState.dekuLaunchPos = { 0.0f, 0.0f, 0.0f };
    gFormState.dekuFlightFlags = 0;
    gFormState.dekuPetalSpeed = 0;
    gFormState.dekuPetalAngle = 0;
    gFormState.dekuPitchAngle = 0;
    gFormState.dekuRollAngle = 0;
    gFormState.dekuFlightTimer = 0;
    gFormState.dekuFlightLaunchType = 0;
    gFormState.dekuSparkleAcc = 0;
    gFormState.dekuSavedShadowScale = 0.0f;
}

// =============================================================================
// Blink System (from 2Ship FaceChange_UpdateBlinkingNonHuman, z_actor.c:4167)
//
// Goron uses: blinkIntervalBase=20, blinkIntervalRandRange=80, blinkDuration=3
// Timer counts down. Last 3 frames before reset = blink:
//   timer > 3: eyes open
//   timer == 3: eyes half (1 frame)
//   timer == 2 or 1: eyes closed (2 frames)
//   timer == 0: reset to new random interval
// =============================================================================

static void MmForm_UpdateBlink(void) {
    if (gFormState.blinkTimer > 0) {
        gFormState.blinkTimer--;
    }

    if (gFormState.blinkTimer == 0) {
        // Rand_S16Offset(20, 80) -> 20 to 100 frames between blinks
        gFormState.blinkTimer = 20 + (s16)(Rand_ZeroFloat(80.0f));
    }

    if (gFormState.blinkTimer > 3) {
        gFormState.eyeIndex = 0; // PLAYER_EYES_OPEN
    } else if (gFormState.blinkTimer == 3) {
        gFormState.eyeIndex = 1; // PLAYER_EYES_HALF
    } else {
        gFormState.eyeIndex = 2; // PLAYER_EYES_CLOSED
    }
}

// =============================================================================
// Root Motion System (from 2Ship ANIM_FLAG_ENABLE_MOVEMENT)
//
// Both Goron and Zora punches use animation root translation to drive movement.
// From 2Ship func_80833864 (z_player.c line 5809):
//   Player_AnimReplace_Setup(play, this, ANIM_FLAG_1 | ANIM_FLAG_ENABLE_MOVEMENT | ANIM_FLAG_NOMOVE);
//
// This sets up the animation system to extract per-frame root position deltas
// from jointTable[0] and apply them to actor.world.pos. The implementation is
// in SkelAnime_UpdateTranslation (z_skelanime.c line 2037):
//   diff.x = jointTable[0].x - prevTransl.x
//   diff.z = jointTable[0].z - prevTransl.z
//   Rotate diff by actor.shape.rot.y
//   Apply: actor.world.pos += diff * actor.scale * ageProperties->unk_08
//
// In OOT, ageProperties->unk_08 = 1.0f (z_player.c line 466), so:
//   world_delta = raw_delta * actor.scale.x = raw_delta * 0.01
//
// Since our MM→OOT animation converter strips root motion (forces X=-57, Z=0
// for correct rendering), we extract the raw root positions BEFORE the fix
// is applied and store them separately for runtime root motion computation.
// =============================================================================

/**
 * Extract raw root motion data from an MM animation resource.
 *
 * Loads the raw animation data from mm.o2r and extracts the root X/Z
 * position per frame BEFORE the baseTransl fix. The caller is responsible
 * for freeing the returned arrays.
 *
 * @param animId       MM animation ID
 * @param outRootX     Output: allocated array of root X per frame
 * @param outRootZ     Output: allocated array of root Z per frame
 * @param outFrameCount Output: number of frames
 * @return 1 on success, 0 on failure
 */
static s32 MmForm_ExtractAnimRootMotion(MmAnimId animId, s16** outRootX, s16** outRootZ, s32* outFrameCount) {
    // gMmAnims declared in mm_anims.h (included via mm_anim_loader.h)
    const MmAnimDef* def = &gMmAnims[animId];
    if (def->path == NULL || def->frameCount <= 0) {
        return 0;
    }

    size_t resourceSize = 0;
    void* resource = MmAssets_LoadResourceWithSize(def->path, &resourceSize);
    if (resource == NULL) {
        return 0;
    }

    s32 s16PerFrame = (def->limbCount * 3) + 1;
    s32 bytesPerFrame = s16PerFrame * (s32)sizeof(s16);
    s32 frameCount = (s32)(resourceSize / (size_t)bytesPerFrame);
    if (frameCount <= 0) {
        return 0;
    }

    s16* rootX = (s16*)malloc(frameCount * sizeof(s16));
    s16* rootZ = (s16*)malloc(frameCount * sizeof(s16));
    if (rootX == NULL || rootZ == NULL) {
        free(rootX);
        free(rootZ);
        return 0;
    }

    s16* raw = (s16*)resource;
    for (s32 f = 0; f < frameCount; f++) {
        s16* frameStart = raw + (f * s16PerFrame);
        rootX[f] = frameStart[0]; // Raw root X (before baseTransl fix)
        rootZ[f] = frameStart[2]; // Raw root Z (before baseTransl fix)
    }

    *outRootX = rootX;
    *outRootZ = rootZ;
    *outFrameCount = frameCount;
    return 1;
}

/**
 * Free all root motion data arrays.
 */
static void MmForm_FreeRootMotion(void) {
    for (s32 i = 0; i < 3; i++) {
        if (gFormState.rootMotion.rootX[i] != NULL) {
            free(gFormState.rootMotion.rootX[i]);
            gFormState.rootMotion.rootX[i] = NULL;
        }
        if (gFormState.rootMotion.rootZ[i] != NULL) {
            free(gFormState.rootMotion.rootZ[i]);
            gFormState.rootMotion.rootZ[i] = NULL;
        }
        gFormState.rootMotion.frameCount[i] = 0;
    }
    gFormState.rootMotion.active = 0;
}

/**
 * Load root motion data for a punch animation.
 *
 * @param punchIndex  0=PunchA, 1=PunchB, 2=PunchC
 * @param animId      MM animation ID
 */
static void MmForm_LoadPunchRootMotion(u8 punchIndex, MmAnimId animId) {
    if (punchIndex > 2)
        return;

    s16* rootX = NULL;
    s16* rootZ = NULL;
    s32 frameCount = 0;

    if (MmForm_ExtractAnimRootMotion(animId, &rootX, &rootZ, &frameCount)) {
        gFormState.rootMotion.rootX[punchIndex] = rootX;
        gFormState.rootMotion.rootZ[punchIndex] = rootZ;
        gFormState.rootMotion.frameCount[punchIndex] = frameCount;
    } else {
    }
}

/**
 * Start root motion tracking for a punch animation.
 * Called when a punch begins. Sets up prev position from frame 0
 * and enables the ANIM_FLAG_NOMOVE equivalent (skip first frame delta).
 *
 * @param punchIndex  0=PunchA, 1=PunchB, 2=PunchC
 */
static void MmForm_StartRootMotion(u8 punchIndex) {
    if (punchIndex > 2 || gFormState.rootMotion.rootX[punchIndex] == NULL) {
        gFormState.rootMotion.active = 0;
        return;
    }

    gFormState.rootMotion.currentPunch = punchIndex;
    gFormState.rootMotion.prevX = gFormState.rootMotion.rootX[punchIndex][0];
    gFormState.rootMotion.prevZ = gFormState.rootMotion.rootZ[punchIndex][0];
    gFormState.rootMotion.active = 1;
    gFormState.rootMotion.firstFrame = 1; // ANIM_FLAG_NOMOVE: skip delta on first frame
}

/**
 * Apply root motion for the current frame.
 * Replicates SkelAnime_UpdateTranslation from 2Ship z_skelanime.c line 2037:
 *   diff = current_root - prev_root
 *   Rotate diff by actor.shape.rot.y
 *   Scale by actor.scale.x (0.01) * ageProperties->unk_08 (1.0)
 *   Apply to actor.world.pos
 *
 * @param player  OOT Player pointer
 */
static void MmForm_ApplyRootMotion(Player* player) {
    if (!gFormState.rootMotion.active)
        return;

    u8 idx = gFormState.rootMotion.currentPunch;
    if (idx > 2 || gFormState.rootMotion.rootX[idx] == NULL)
        return;

    s32 frame = (s32)gFormState.formSkelAnime.curFrame;
    if (frame < 0)
        frame = 0;
    if (frame >= gFormState.rootMotion.frameCount[idx]) {
        frame = gFormState.rootMotion.frameCount[idx] - 1;
    }

    s16 curX = gFormState.rootMotion.rootX[idx][frame];
    s16 curZ = gFormState.rootMotion.rootZ[idx][frame];

    if (gFormState.rootMotion.firstFrame) {
        // ANIM_FLAG_NOMOVE: don't move on first frame, just save position
        // From 2Ship SkelAnime_UpdateTranslation line 2059: movementFlags & ANIM_FLAG_NOMOVE
        gFormState.rootMotion.prevX = curX;
        gFormState.rootMotion.prevZ = curZ;
        gFormState.rootMotion.firstFrame = 0;
        return;
    }

    // Compute raw delta (from 2Ship SkelAnime_UpdateTranslation line 2046-2047)
    f32 dx = (f32)(curX - gFormState.rootMotion.prevX);
    f32 dz = (f32)(curZ - gFormState.rootMotion.prevZ);

    // Save for next frame (from 2Ship line 2054-2055)
    gFormState.rootMotion.prevX = curX;
    gFormState.rootMotion.prevZ = curZ;

    if (dx == 0.0f && dz == 0.0f)
        return;

    // Rotate by player yaw (from 2Ship SkelAnime_UpdateTranslation line 2048-2051)
    f32 sinY = Math_SinS(player->actor.shape.rot.y);
    f32 cosY = Math_CosS(player->actor.shape.rot.y);
    f32 worldDX = dx * cosY + dz * sinY;
    f32 worldDZ = dz * cosY - dx * sinY;

    // Scale by actor.scale (0.01) * ageProperties->unk_08 (1.0 in OOT)
    // From 2Ship AnimTask_ActorMovement line 1249:
    //   actor->world.pos.x += diff.x * actor->scale.x * task->diffScale;
    f32 scale = player->actor.scale.x; // 0.01f
    player->actor.world.pos.x += worldDX * scale;
    player->actor.world.pos.z += worldDZ * scale;
}

/**
 * Stop root motion tracking (punch ended or interrupted).
 */
static void MmForm_StopRootMotion(void) {
    gFormState.rootMotion.active = 0;
}

// =============================================================================
// Action State Machine (Phase 3)
//
// From 2Ship, Goron has these action functions:
//   Player_Action_Idle (line 14727) - standing still, pg_wait
//   Player_Action_5 (line 14832)   - walking, link_normal_walk_free
//   Player_Action_9 (line 14888)   - running, link_normal_run_free
//   Player_Action_96 (line 19886)  - curl/roll (Phase 6)
//   Melee weapon actions            - punch combo (Phase 4)
//   func_80833B18 (line 5877)      - damage/knockback (Phase 5)
//
// Transitions (from 2Ship):
//   Idle → Walk/Run: speedTarget != 0.0f (line 14787)
//   Walk → Run:      speedTarget > 4.9f (line 14866)
//   Run → Walk:      speedTarget <= 4.9f (line 14910)
//   Walk/Run → Idle: speedTarget == 0.0f
//   A press (standing): curl → Player_Action_96 (line 8464-8469)
//   A press (moving):   curl → Player_Action_96 (line 8464)
//   B press (standing): punch combo (D_8085D064, line 3569-3574)
//   Damage:             highest priority, any state (line 5896)
// =============================================================================

// Forward declarations (defined later in this file)
static void MmForm_StartPunch(Player* player, PlayState* play);
static u8 MmForm_IsZTargeting(Player* player);
static s16 MmForm_GetStickRelAngle(Player* player, PlayState* play);
static s32 MmForm_GetStickDirection(Player* player);
static f32 MmForm_GetStickMagnitude(PlayState* play);
static void MmForm_CheckBarrierInput(Player* player, PlayState* play);
static void MmForm_UpdateBarrier(Player* player, PlayState* play);
static void MmForm_CheckBootToggle(Player* player, PlayState* play);
static void MmForm_StartBoomerangThrow(Player* player, PlayState* play);
static void MmForm_EnterSwimIdle(Player* player, PlayState* play);
static void MmForm_WaterBuoyancy(Player* player);
static void MmForm_InitBarrierCollider(Player* player, PlayState* play);
static void MmForm_PlaySfx(Player* player, u16 mmSfxId, u16 ootSfxId);
static void MmForm_DekuWaterHop(Player* player, PlayState* play);
static void MmForm_PlayAttackVoice(Player* player);
static void MmForm_StartDekuFlower(Player* player, PlayState* play);
static void MmForm_StartDekuFlightMidair(Player* player, PlayState* play);
static void MmForm_Action_DekuFlower(Player* player, PlayState* play);
static void MmForm_Action_DekuFly(Player* player, PlayState* play);
static void MmForm_Action_DekuFallLocked(Player* player, PlayState* play);
static void MmForm_EndDekuFly(Player* player, PlayState* play, LinkAnimationHeader* anim);

// Helper: set action and play animation
static void MmForm_SetAction(GoronActionId action, PlayState* play, LinkAnimationHeader* anim, f32 playSpeed, u8 mode) {
    gFormState.goronAction = action;
    gFormState.actionTimer = 0;
    if (anim != NULL) {
        LinkAnimation_Change(play, &gFormState.formSkelAnime, anim, playSpeed, 0.0f, Animation_GetLastFrame(anim), mode,
                             -8.0f);
    }
}

// ---------------------------------------------------------------------------
// Shield Entry Helper
//
// Handles shield entry for all forms. Sets up animation at entry time so
// we don't depend on actionTimer==0 in the action (which fails because
// gFormState.actionTimer++ runs BEFORE the action dispatch).
//
// Goron: plays SFX + sets SHIELDING flag (shieldSkelAnime already loaded)
// Zora: plays defense animation on formSkelAnime + SFX + SHIELDING flag
// ---------------------------------------------------------------------------
static s16 sShieldLockedYaw = 0;

static void MmForm_EnterShield(Player* player, PlayState* play) {
    player->linearVelocity = 0.0f;

    if (gFormState.currentForm == MM_PLAYER_FORM_DEKU && gFormState.dekuGuardAnim != NULL) {
        // From 2Ship Player_ActionHandler_11 (z_player.c line 8544):
        //   anim = &gPlayerAnim_pn_gurd;
        //   startFrame = 0.0f (plays from beginning, shield DL scales in during frames 0-3)
        // Deku guard is a crouch pose, NOT the standard defense animation.
        MmForm_SetAction(MMFORM_ACT_SHIELD, play, NULL, 0.0f, ANIMMODE_ONCE);
        // Play pn_gurd from frame 0 on formSkelAnime (MmForm_SetAction with NULL anim
        // doesn't set up formSkelAnime, so we do it manually)
        f32 endFrame = Animation_GetLastFrame(gFormState.dekuGuardAnim);
        LinkAnimation_Change(play, &gFormState.formSkelAnime, gFormState.dekuGuardAnim, 1.0f, 0.0f, endFrame,
                             ANIMMODE_ONCE, 0.0f);
    } else if (gFormState.currentForm == MM_PLAYER_FORM_ZORA) {
        // From 2Ship Player_ActionHandler_11 (line 8536): plays defense at endFrame (instant pose)
        // D_8085BE84[PLAYER_ANIMGROUP_defense][PLAYER_ANIMTYPE_2] = gPlayerAnim_link_normal_defense
        LinkAnimationHeader* shieldAnim = (LinkAnimationHeader*)gPlayerAnim_link_normal_defense;
        MmForm_SetAction(MMFORM_ACT_SHIELD, play, shieldAnim, 1.0f, ANIMMODE_ONCE);
    } else {
        // Goron: no form anim needed (uses shieldSkelAnime)
        MmForm_SetAction(MMFORM_ACT_SHIELD, play, NULL, 0.0f, ANIMMODE_ONCE);
    }

    // Save current facing direction. In MM, Player_ActionHandler_11 does NOT change yaw —
    // Link keeps whatever direction he was facing. We save it to force every frame
    // (ball-and-chain pattern) because OOT's Player_UpdateCommon overwrites shape.rot.y.
    sShieldLockedYaw = player->actor.shape.rot.y;

    // Reset av2 — directional control starts after animation finishes once (from 2Ship line 14905)
    gFormState.shieldAv2 = 0;

    // Reset upper body rotations (from 2Ship line 8565: this->upperLimbRot = {0,0,0})
    player->upperLimbRot.x = 0;
    player->upperLimbRot.y = 0;
    player->upperLimbRot.z = 0;

    // Set flags and SFX immediately at entry
    player->stateFlags1 |= PLAYER_STATE1_SHIELDING;
    if (gFormState.currentForm == MM_PLAYER_FORM_GORON) {
        MmForm_PlaySfx(player, MM_NA_SE_PL_GORON_SQUAT, NA_SE_PL_BODY_HIT);
    } else {
        Audio_PlayActorSound2(&player->actor, NA_SE_IT_SHIELD_BOUND);
    }
}

// ---------------------------------------------------------------------------
// Action: IDLE (from 2Ship Player_Action_Idle, line 14727)
//
// Plays pg_wait in a loop. Transitions to walk/run when stick is pushed.
// In MM, idle also handles turn-in-place (waitL2wait, waitR2wait) but we
// let OOT handle rotation and only sync the animation state.
// ---------------------------------------------------------------------------
static void MmForm_GoronAction_Idle(Player* player, PlayState* play) {
    f32 speed = player->linearVelocity;
    Input* input = &play->state.input[0];

    // Z-targeting → switch to Z-target idle
    // From 2Ship: Player_Action_Idle checks Player_CheckHostileLockOn for strafe mode
    if (MmForm_IsZTargeting(player)) {
        LinkAnimationHeader* ztAnim = gFormState.ztargetIdleR ? gFormState.ztargetIdleR : gFormState.idleAnim;
        MmForm_SetAction(MMFORM_ACT_ZTARGET_IDLE, play, ztAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }

    // R button → shield stance (from 2Ship Player_ActionHandler_11, line 8391)
    // Goron: ground curl with gLinkGoronShieldingSkel
    // Zora: guard pose with defense anim + barrier on R+B
    if (CHECK_BTN_ALL(input->cur.button, BTN_R)) {
        if (gFormState.currentForm == MM_PLAYER_FORM_GORON || gFormState.currentForm == MM_PLAYER_FORM_ZORA ||
            gFormState.currentForm == MM_PLAYER_FORM_DEKU) {
            MmForm_EnterShield(player, play);
            return;
        }
    }

    // B button → punch combo / bubble spit
    // Zora boomerang is B-HOLD after an action (punch/jump kick), not B-press
    if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
        if (gFormState.currentForm == MM_PLAYER_FORM_GORON) {
            MmForm_StartPunch(player, play);
            return;
        } else if (gFormState.currentForm == MM_PLAYER_FORM_ZORA) {
            MmForm_StartPunch(player, play);
            return;
        } else if (gFormState.currentForm == MM_PLAYER_FORM_DEKU && gFormState.dekuBowReady != NULL) {
            // Enter bubble aim mode (first-person camera, hold B to charge)
            MmForm_SetAction(MMFORM_ACT_DEKU_BUBBLE_AIM, play, gFormState.dekuBowReady, 1.0f, ANIMMODE_LOOP);
            player->linearVelocity = 0.0f;
            gFormState.bubbleCharging = 1;
            gFormState.bubbleCharge = 0.0f;
            gFormState.bubbleChargeTimer = 0;
            return;
        }
    }

    // B hold (Zora only) → boomerang throw (from 2Ship func_80830E30, z_player.c:4207)
    // In MM you must do an action first (punch/jump kick), THEN hold B to enter aim mode.
    // This check also triggers from PUNCH_END and other post-action states (see those handlers).
    if (CHECK_BTN_ALL(input->cur.button, BTN_B) && !CHECK_BTN_ALL(input->press.button, BTN_B)) {
        if (gFormState.currentForm == MM_PLAYER_FORM_ZORA && gFormState.boomerangState == 0 &&
            gFormState.cutterAttack != NULL) {
            MmForm_StartBoomerangThrow(player, play);
            return;
        }
    }

    // Deku Leaf C-button → flower burrow (ground) or flight (air)
    // On ground: costs 10 MP, enters flower burrow sequence (Player_Action_93)
    // In air: enters flight directly, no magic cost (Player_Action_94)
    if (gFormState.currentForm == MM_PLAYER_FORM_DEKU && gFormState.dekuFlightLaunch != NULL) {
        if (ItemHeld_IsButtonPressed(ITEM_DEKU_LEAF, player, play)) {
            if (MMFORM_ON_GROUND(player)) {
                // Ground: burrow into flower (costs 10 MP)
                if (gSaveContext.magic >= 10) {
                    gSaveContext.magic -= 10;
                    MmForm_StartDekuFlower(player, play);
                    return;
                }
            } else if (gFormState.goronAction != MMFORM_ACT_DEKU_FLY &&
                       gFormState.goronAction != MMFORM_ACT_DEKU_FLOWER &&
                       gFormState.goronAction != MMFORM_ACT_DEKU_FALL_LOCKED) {
                // Air: enter flight directly (free, no magic cost)
                MmForm_StartDekuFlightMidair(player, play);
                return;
            }
        }
    }

    // A button behavior depends on form
    // From 2Ship func_80839F98 (line 8462): Goron A+idle(speedXZ==0) → curl (pg_maru_change)
    // From 2Ship func_80839A84 (line 8223): Deku A → spin attack (Player_Action_95)
    // Other forms: A → jump
    // Ground check: in MM, action handlers only run from ground actions (implicit guarantee)
    if (CHECK_BTN_ALL(input->press.button, BTN_A) && MMFORM_ON_GROUND(player)) {
        if (gFormState.currentForm == MM_PLAYER_FORM_GORON) {
            // Goron: A while idle → curl into ball (Phase 6 placeholder)
            // From 2Ship: func_80839F98 speedXZ==0 → pg_maru_change at 2/3 speed, 7 frames
            if (gFormState.maruChange != NULL) {
                player->linearVelocity = 0.0f;
                MmForm_SetAction(GORON_ACT_ROLL_INIT, play, gFormState.maruChange, 0.67f, ANIMMODE_ONCE);
                MmForm_PlaySfx(player, MM_NA_SE_PL_GORON_TO_BALL, NA_SE_PL_BODY_HIT);
                return;
            }
        } else if (gFormState.currentForm == MM_PLAYER_FORM_DEKU) {
            // Deku: A → spin attack (from 2Ship func_80839A84, z_player.c line 8223)
            // Plays pn_attack anim, spins shape.rot.y with decaying speed
            if (gFormState.dekuSpinAttack != NULL) {
                MmForm_SetAction(MMFORM_ACT_DEKU_SPIN, play, gFormState.dekuSpinAttack, 1.0f, ANIMMODE_ONCE);
                gFormState.dekuSpinSpeed = 20000.0f;  // unk_B10[0] initial spin speed
                gFormState.dekuSpinTimer = 196608.0f; // unk_B10[1] = 0x30000 as float
                gFormState.dekuSpinActive = 1;
                gFormState.dekuSpinRotAccum = 0;
                player->stateFlags2 |= PLAYER_STATE2_DISABLE_ROTATION_ALWAYS;
                MmForm_PlaySfx(player, MM_NA_SE_PL_DEKUNUTS_ATTACK, NA_SE_PL_BODY_HIT);
                return;
            }
        }
        // Other forms (Zora, FD): A from idle does nothing in MM
    }

    // Override speed with MM LINEAR mode formula: stickMag * 0.8 * 0.14 = stickMag * 0.112
    // Same formula used by Z-target walk. Walk/run distinction handled by speed thresholds.
    {
        f32 stickMag = MmForm_GetStickMagnitude(play);
        f32 targetSpeed = stickMag * 0.112f;
        if (player->stateFlags1 & MMFORM_BLOCK_MOVEMENT_FLAGS) {
            targetSpeed = 0.0f;
        }
        if (gFormState.currentForm == MM_PLAYER_FORM_FIERCE_DEITY) {
            targetSpeed *= 1.5f;
        }
        Math_StepToF(&player->linearVelocity, targetSpeed, 1.5f);
        speed = player->linearVelocity;
    }

    // Transition: any movement → walk or run
    // From 2Ship line 14787: if (speedTarget != 0.0f) → func_8083A844
    if (speed >= 0.5f) {
        if (speed > 4.0f) {
            LinkAnimationHeader* runAnim = gFormState.runAnim
                                               ? gFormState.runAnim
                                               : (gFormState.walkAnim ? gFormState.walkAnim : gFormState.idleAnim);
            MmForm_SetAction(GORON_ACT_RUN, play, runAnim, 1.5f, ANIMMODE_LOOP);
        } else {
            LinkAnimationHeader* walkAnim = gFormState.walkAnim ? gFormState.walkAnim : gFormState.idleAnim;
            MmForm_SetAction(GORON_ACT_WALK, play, walkAnim, speed * 0.3f + 1.0f, ANIMMODE_LOOP);
        }
        return;
    }

    // Stay idle - animation ticks in dispatcher
}

// ---------------------------------------------------------------------------
// Action: WALK (from 2Ship Player_Action_5, line 14832)
//
// Walk animation with speed-proportional playback rate.
// From 2Ship line 14648: func_8083EA44(this, this->speedXZ * 0.3f + 1.0f)
// ---------------------------------------------------------------------------
static void MmForm_GoronAction_Walk(Player* player, PlayState* play) {
    f32 speed = player->linearVelocity;
    Input* input = &play->state.input[0];

    // Z-targeting → switch to Z-target walk/strafe
    if (MmForm_IsZTargeting(player)) {
        MmForm_SetAction(MMFORM_ACT_ZTARGET_WALK, play, NULL, 1.0f, ANIMMODE_LOOP);
        return;
    }

    // R button → shield stance (from 2Ship Player_ActionHandler_11)
    if (CHECK_BTN_ALL(input->cur.button, BTN_R)) {
        if (gFormState.currentForm == MM_PLAYER_FORM_GORON || gFormState.currentForm == MM_PLAYER_FORM_ZORA ||
            gFormState.currentForm == MM_PLAYER_FORM_DEKU) {
            MmForm_EnterShield(player, play);
            return;
        }
    }

    // B button → punch/bubble (Zora boomerang is B-HOLD after an action)
    if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
        if (gFormState.currentForm == MM_PLAYER_FORM_GORON) {
            MmForm_StartPunch(player, play);
            return;
        } else if (gFormState.currentForm == MM_PLAYER_FORM_ZORA) {
            MmForm_StartPunch(player, play);
            return;
        } else if (gFormState.currentForm == MM_PLAYER_FORM_DEKU && gFormState.dekuBowReady != NULL) {
            // Enter bubble aim mode (first-person camera, hold B to charge)
            MmForm_SetAction(MMFORM_ACT_DEKU_BUBBLE_AIM, play, gFormState.dekuBowReady, 1.0f, ANIMMODE_LOOP);
            player->linearVelocity = 0.0f;
            gFormState.bubbleCharging = 1;
            gFormState.bubbleCharge = 0.0f;
            gFormState.bubbleChargeTimer = 0;
            return;
        }
    }

    // Deku Leaf C-button → flower burrow (ground) or flight (air)
    if (gFormState.currentForm == MM_PLAYER_FORM_DEKU && gFormState.dekuFlightLaunch != NULL) {
        if (ItemHeld_IsButtonPressed(ITEM_DEKU_LEAF, player, play)) {
            if (MMFORM_ON_GROUND(player)) {
                if (gSaveContext.magic >= 10) {
                    gSaveContext.magic -= 10;
                    MmForm_StartDekuFlower(player, play);
                    return;
                }
            }
        }
    }

    // A button while walking - form-dependent
    // From 2Ship func_80839F98 (line 8462): Goron A+moving(speedXZ!=0) → func_80836B3C (roll attack)
    // From 2Ship func_80839A84 (line 8223): Deku A → spin attack
    // Other forms: A → jump
    // Ground check: in MM, action handlers only run from ground actions (implicit guarantee)
    if (CHECK_BTN_ALL(input->press.button, BTN_A) && MMFORM_ON_GROUND(player)) {
        if (gFormState.currentForm == MM_PLAYER_FORM_GORON) {
            // Goron: A while walking → curl init (Phase 6 placeholder)
            if (gFormState.maruChange != NULL) {
                MmForm_SetAction(GORON_ACT_ROLL_INIT, play, gFormState.maruChange, 0.67f, ANIMMODE_ONCE);
                MmForm_PlaySfx(player, MM_NA_SE_PL_GORON_TO_BALL, NA_SE_PL_BODY_HIT);
                return;
            }
        } else if (gFormState.currentForm == MM_PLAYER_FORM_DEKU && gFormState.dekuSpinAttack != NULL) {
            // Deku: A → spin attack (maintains movement speed during spin)
            MmForm_SetAction(MMFORM_ACT_DEKU_SPIN, play, gFormState.dekuSpinAttack, 1.0f, ANIMMODE_ONCE);
            gFormState.dekuSpinSpeed = 20000.0f;
            gFormState.dekuSpinTimer = 196608.0f;
            gFormState.dekuSpinActive = 1;
            gFormState.dekuSpinRotAccum = 0;
            player->stateFlags2 |= PLAYER_STATE2_DISABLE_ROTATION_ALWAYS;
            MmForm_PlaySfx(player, MM_NA_SE_PL_DEKUNUTS_ATTACK, NA_SE_PL_BODY_HIT);
            return;
        }
        // Other forms (Zora, FD): A while walking does nothing in MM
    }

    // Override speed with MM LINEAR mode formula: stickMag * 0.8 * 0.14 = stickMag * 0.112
    // Same formula used by Z-target walk. Walk/run distinction handled by speed thresholds.
    {
        f32 stickMag = MmForm_GetStickMagnitude(play);
        f32 targetSpeed = stickMag * 0.112f;
        if (player->stateFlags1 & MMFORM_BLOCK_MOVEMENT_FLAGS) {
            targetSpeed = 0.0f;
        }
        if (gFormState.currentForm == MM_PLAYER_FORM_FIERCE_DEITY) {
            targetSpeed *= 1.5f;
        }
        Math_StepToF(&player->linearVelocity, targetSpeed, 1.5f);
        speed = player->linearVelocity;
    }

    // Transition: stopped → idle
    if (speed < 0.5f) {
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }

    // Transition: fast → run (from 2Ship line 14866: speedTarget > 4.9f)
    // We use actual speed > 4.0f since OOT linearVelocity lags behind target
    if (speed > 4.0f) {
        LinkAnimationHeader* runAnim =
            gFormState.runAnim ? gFormState.runAnim : (gFormState.walkAnim ? gFormState.walkAnim : gFormState.idleAnim);
        MmForm_SetAction(GORON_ACT_RUN, play, runAnim, 1.5f, ANIMMODE_LOOP);
        return;
    }

    // Walk anim rate proportional to speed (from 2Ship func_8083EA44)
    gFormState.formSkelAnime.playSpeed = speed * 0.3f + 1.0f;
}

// ---------------------------------------------------------------------------
// Action: RUN (from 2Ship Player_Action_9, line 14888)
//
// Run animation with speed-scaled playback rate.
// ---------------------------------------------------------------------------
static void MmForm_GoronAction_Run(Player* player, PlayState* play) {
    f32 speed = player->linearVelocity;
    Input* input = &play->state.input[0];

    // Z-targeting → switch to Z-target walk/strafe
    if (MmForm_IsZTargeting(player)) {
        MmForm_SetAction(MMFORM_ACT_ZTARGET_WALK, play, NULL, 1.0f, ANIMMODE_LOOP);
        return;
    }

    // R button → shield stance (from 2Ship Player_ActionHandler_11)
    if (CHECK_BTN_ALL(input->cur.button, BTN_R)) {
        if (gFormState.currentForm == MM_PLAYER_FORM_GORON || gFormState.currentForm == MM_PLAYER_FORM_ZORA ||
            gFormState.currentForm == MM_PLAYER_FORM_DEKU) {
            MmForm_EnterShield(player, play);
            return;
        }
    }

    // B button → punch/bubble (Zora boomerang is B-HOLD after an action)
    if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
        if (gFormState.currentForm == MM_PLAYER_FORM_GORON) {
            MmForm_StartPunch(player, play);
            return;
        } else if (gFormState.currentForm == MM_PLAYER_FORM_ZORA) {
            MmForm_StartPunch(player, play);
            return;
        } else if (gFormState.currentForm == MM_PLAYER_FORM_DEKU && gFormState.dekuBowReady != NULL) {
            // Enter bubble aim mode (first-person camera, hold B to charge)
            MmForm_SetAction(MMFORM_ACT_DEKU_BUBBLE_AIM, play, gFormState.dekuBowReady, 1.0f, ANIMMODE_LOOP);
            player->linearVelocity = 0.0f;
            gFormState.bubbleCharging = 1;
            gFormState.bubbleCharge = 0.0f;
            gFormState.bubbleChargeTimer = 0;
            return;
        }
    }

    // Deku Leaf C-button → flower burrow (ground) or flight (air)
    if (gFormState.currentForm == MM_PLAYER_FORM_DEKU && gFormState.dekuFlightLaunch != NULL) {
        if (ItemHeld_IsButtonPressed(ITEM_DEKU_LEAF, player, play)) {
            if (MMFORM_ON_GROUND(player)) {
                if (gSaveContext.magic >= 10) {
                    gSaveContext.magic -= 10;
                    MmForm_StartDekuFlower(player, play);
                    return;
                }
            }
        }
    }

    // A button while running - form-dependent
    // From 2Ship: Goron A+running → curl/ball roll (Phase 6 placeholder)
    // From 2Ship func_80839A84: Deku A → spin attack (works from any ground state)
    // Other forms: A → forward roll
    if (CHECK_BTN_ALL(input->press.button, BTN_A) && MMFORM_ON_GROUND(player)) {
        if (gFormState.currentForm == MM_PLAYER_FORM_GORON) {
            // Goron: A while running → curl init (Phase 6 placeholder)
            if (gFormState.maruChange != NULL) {
                MmForm_SetAction(GORON_ACT_ROLL_INIT, play, gFormState.maruChange, 0.67f, ANIMMODE_ONCE);
                MmForm_PlaySfx(player, MM_NA_SE_PL_GORON_TO_BALL, NA_SE_PL_BODY_HIT);
                return;
            }
        } else if (gFormState.currentForm == MM_PLAYER_FORM_DEKU && gFormState.dekuSpinAttack != NULL) {
            // Deku: A → spin attack (maintains movement speed during spin)
            MmForm_SetAction(MMFORM_ACT_DEKU_SPIN, play, gFormState.dekuSpinAttack, 1.0f, ANIMMODE_ONCE);
            gFormState.dekuSpinSpeed = 20000.0f;
            gFormState.dekuSpinTimer = 196608.0f;
            gFormState.dekuSpinActive = 1;
            gFormState.dekuSpinRotAccum = 0;
            player->stateFlags2 |= PLAYER_STATE2_DISABLE_ROTATION_ALWAYS;
            MmForm_PlaySfx(player, MM_NA_SE_PL_DEKUNUTS_ATTACK, NA_SE_PL_BODY_HIT);
            return;
        } else if (gFormState.rollAnim != NULL) {
            gFormState.rollSpeed = 1.0f;             // > 0 = normal roll state (< 0 = bonked)
            player->yaw = player->actor.shape.rot.y; // Lock yaw to facing dir (from 2Ship func_80836B3C)
            player->actor.world.rot.y = player->actor.shape.rot.y;
            MmForm_SetAction(MMFORM_ACT_ROLL, play, gFormState.rollAnim, 1.25f, ANIMMODE_ONCE);
            MmForm_PlaySfx(player, MM_NA_SE_PL_LAND, NA_SE_PL_BODY_HIT);
            return;
        }
    }

    // Override speed with MM LINEAR mode formula: stickMag * 0.8 * 0.14 = stickMag * 0.112
    // Same formula used by Z-target walk. Walk/run distinction handled by speed thresholds.
    {
        f32 stickMag = MmForm_GetStickMagnitude(play);
        f32 targetSpeed = stickMag * 0.112f;
        if (player->stateFlags1 & MMFORM_BLOCK_MOVEMENT_FLAGS) {
            targetSpeed = 0.0f;
        }
        if (gFormState.currentForm == MM_PLAYER_FORM_FIERCE_DEITY) {
            targetSpeed *= 1.5f;
        }
        Math_StepToF(&player->linearVelocity, targetSpeed, 1.5f);
        speed = player->linearVelocity;
    }

    // Transition: stopped → idle
    if (speed < 0.5f) {
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }

    // Transition: slow → walk (from 2Ship line 14910: speedTarget <= 4.9f)
    if (speed <= 4.0f) {
        LinkAnimationHeader* walkAnim = gFormState.walkAnim ? gFormState.walkAnim : gFormState.idleAnim;
        MmForm_SetAction(GORON_ACT_WALK, play, walkAnim, speed * 0.3f + 1.0f, ANIMMODE_LOOP);
        return;
    }

    // Run anim rate scales with speed
    gFormState.formSkelAnime.playSpeed = speed * 0.15f + 1.0f;
}

// =============================================================================
// Phase 4: Punch Combo System (from 2Ship z_player.c)
//
// MM implementation (Player_Action_84, line 18761):
//   func_808335F4 selects punch from D_8085D094[1] indexed by unk_ADD
//   unk_ADD tracks combo: 0=PunchA(left), 1=PunchB(right), 2=PunchC(butt)
//   B press during punch sets av2.actionVar2 for combo continuation
//   At animation end: if av2.actionVar2 set, advance combo; else play recovery
//   Hit detection via func_8083FCF0 with frame windows from sMeleeAttackAnimInfo
//   Recovery anim selected by Player_CheckHostileLockOn (endR vs end)
//
// Our adaptation differences:
//   - Uses OOT's player->meleeWeaponQuads[0] with directional vertices (same as MM)
//   - DMG_HAMMER_SWING for damage type (same heavy blunt impact as Megaton Hammer)
//   - PLAYER_STATE1_HOSTILE_LOCK_ON check (identical to MM's Player_CheckHostileLockOn)
//   - comboStep/comboBPressed instead of unk_ADD/av2.actionVar2
//   - Forward movement from ANIM_FLAG_ENABLE_MOVEMENT (root motion from animation data)
//   - unk_3D0.unk_00 = 3 for Goron punches = visual afterimage (not movement, omitted)
// =============================================================================

// Punch hit frame windows (from 2Ship sMeleeAttackAnimInfo)
// { hitFrameStart, hitFrameEnd } - args passed to func_8083FCF0
// Goron: z_player.c line 3569-3574, Zora: line 3575-3580
static const u8 sGoronPunchFrames[][2] = {
    { 6, 8 },   // Punch A (left)  - PLAYER_MWA_GORON_PUNCH_LEFT
    { 12, 18 }, // Punch B (right) - PLAYER_MWA_GORON_PUNCH_RIGHT
    { 8, 14 },  // Punch C (butt)  - PLAYER_MWA_GORON_PUNCH_BUTT
};

static const u8 sZoraPunchFrames[][2] = {
    { 2, 5 },  // Punch A (left)  - PLAYER_MWA_ZORA_PUNCH_LEFT
    { 3, 8 },  // Punch B (combo) - PLAYER_MWA_ZORA_PUNCH_COMBO
    { 3, 10 }, // Punch C (kick)  - PLAYER_MWA_ZORA_PUNCH_KICK
};

// Damage values (from 2Ship D_8085D09C collider setup)
// Goron: { DMG_GORON_PUNCH, 2, 2, 0, 0 } → transformed damage = 0, but vanilla uses 2
// Zora: { DMG_ZORA_PUNCH, 1, 2, 0, 0 } → normal=1, strong=2
#define GORON_PUNCH_DAMAGE 2
#define ZORA_PUNCH_DAMAGE 1

// Get attack animation for combo step
static LinkAnimationHeader* MmForm_GetPunchAttackAnim(u8 step) {
    switch (step) {
        case 0:
            return gFormState.punchA;
        case 1:
            return gFormState.punchB;
        case 2:
            return gFormState.punchC;
        default:
            return NULL;
    }
}

// Get recovery animation for combo step
// lockedOn: replaces Player_CheckHostileLockOn → selects endR vs end anims
static LinkAnimationHeader* MmForm_GetPunchEndAnim(u8 step, u8 lockedOn) {
    if (lockedOn) {
        switch (step) {
            case 0:
                return gFormState.punchAEndR;
            case 1:
                return gFormState.punchBEndR;
            case 2:
                return gFormState.punchCEndR;
            default:
                return NULL;
        }
    }
    switch (step) {
        case 0:
            return gFormState.punchAEnd;
        case 1:
            return gFormState.punchBEnd;
        case 2:
            return gFormState.punchCEnd;
        default:
            return NULL;
    }
}

// Start punch combo (called from idle/walk/run when B is pressed)
// Works for both Goron and Zora forms
// Replaces: func_808335F4 (punch selection) + func_80833864 (melee weapon start)
//
// From 2Ship func_80833864 (line 5786):
//   Player_Anim_PlayOnceAdjusted(play, this, sMeleeAttackAnimInfo[meleeWeaponAnim].unk_0);
//   Player_AnimReplace_Setup(play, this, ANIM_FLAG_1 | ANIM_FLAG_ENABLE_MOVEMENT | ANIM_FLAG_NOMOVE);
//
// Player_AnimReplace_Setup calls Player_StopHorizontalMovement (speedXZ = 0),
// saves prevTransl, and sets moveFlags. The root motion system then drives
// forward movement from the animation's root translation data each frame.
static void MmForm_StartPunch(Player* player, PlayState* play) {
    if (gFormState.punchA == NULL)
        return;

    gFormState.comboStep = 0;
    gFormState.comboBPressed = 0;
    MmForm_DisablePunchQuad(player); // Clear any leftover quad state
    MmForm_SetAction(GORON_ACT_PUNCH_A, play, gFormState.punchA, 1.0f, ANIMMODE_ONCE);

    // Play attack voice (from 2Ship Player_AnimSfx_PlayVoice in melee handler)
    MmForm_PlayAttackVoice(player);

    // Play punch SFX
    // From 2Ship func_8082FA5C: Goron = NA_SE_IT_GORON_PUNCH_SWING
    //   Zora left/combo = NA_SE_IT_SWORD_SWING_HARD, kick = NA_SE_IT_GORON_PUNCH_SWING
    if (gFormState.currentForm == MM_PLAYER_FORM_ZORA) {
        MmForm_PlaySfx(player, MM_NA_SE_IT_GORON_PUNCH_SWING, NA_SE_IT_SWORD_SWING_HARD);
    } else {
        MmForm_PlaySfx(player, MM_NA_SE_PL_GORON_PUNCH, NA_SE_PL_BODY_HIT);
    }

    // Player_StopHorizontalMovement (from 2Ship Player_AnimReplace_Setup line 1979)
    player->linearVelocity = 0.0f;

    // Start root motion tracking for punch A (ANIM_FLAG_ENABLE_MOVEMENT)
    MmForm_StartRootMotion(0);
}

// ---------------------------------------------------------------------------
// Action: PUNCH (handles all 3 combo steps for Goron AND Zora)
// Replaces: Player_Action_84 (line 18761) for form-specific punches
//
// Frame flow per punch:
//   Goron: frame 0 to 5.0 = wind-up, 5.0 to hitEnd = active (early detection)
//   Zora:  frame 0 to hitStart = wind-up, hitStart to hitEnd = active (no early detect)
//   frame > hitEnd:     wind-down
//   frame >= endFrame:  combo check → next punch or recovery
// ---------------------------------------------------------------------------
static void MmForm_Action_Punch(Player* player, PlayState* play) {
    SkelAnime* skelAnime = &gFormState.formSkelAnime;
    if (skelAnime->animation == NULL) {
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }
    f32 curFrame = skelAnime->curFrame;
    f32 endFrame = Animation_GetLastFrame(skelAnime->animation);
    u8 step = gFormState.comboStep;
    u8 isGoron = (gFormState.currentForm == MM_PLAYER_FORM_GORON);

    // Hit detection (from 2Ship func_8083FCF0, line 10540-10551)
    // MM calls: func_8083FCF0(play, this,
    //     (this->transformation == PLAYER_FORM_GORON) ? 5.0f : 0.0f,
    //     attackInfoEntry->unk_C, attackInfoEntry->unk_D);
    // Goron: arg2=5.0f (early start), Zora: arg2=0.0f (no early start)
    //
    // Uses directional meleeWeaponQuads (NOT radial cylinder) with DMG_HAMMER_SWING
    // so enemies that react to hammer blows also react to Goron punches.
    const u8(*punchFrames)[2] = isGoron ? sGoronPunchFrames : sZoraPunchFrames;
    u8 hitStart = punchFrames[step][0];
    u8 hitEnd = punchFrames[step][1];
    f32 earlyStart = isGoron ? 5.0f : (f32)hitStart;
    u8 damage = isGoron ? GORON_PUNCH_DAMAGE : ZORA_PUNCH_DAMAGE;

    if (curFrame > (f32)hitEnd) {
        // Past hit window (func_8083FCF0: arg4 < curFrame → func_8082DC38)
        MmForm_DisablePunchQuad(player);
    } else if (curFrame >= earlyStart) {
        // In hit detection range - set up directional quad and submit to collision
        MmForm_EnablePunchQuad(player, play, step, damage);
    }

    // Forward movement: BOTH Goron and Zora use animation root motion
    // From 2Ship func_80833864 (line 5809): ALL punch MWAs get ANIM_FLAG_ENABLE_MOVEMENT
    // From 2Ship Player_Action_84 (line 18783): Math_StepToF(&speedXZ, 0, 5) decelerates
    // The REAL forward movement comes from root motion, not speedXZ.
    //
    // Additionally, Goron Punch A/B set unk_3D0.unk_00 = 3 (visual afterimage, NOT movement)
    Math_StepToF(&player->linearVelocity, 0.0f, 5.0f);

    // Apply root motion from animation data (replicates ANIM_FLAG_ENABLE_MOVEMENT)
    MmForm_ApplyRootMotion(player);

    // Combo continuation: check B press during non-final punches
    // (from 2Ship Player_Action_84 line 18833-18839)
    // MM: if (form && MWA != last_punch) { av2.actionVar2 |= BTN_B ? 1 : 0; }
    if (step < 2) {
        Input* input = &play->state.input[0];
        if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
            gFormState.comboBPressed = 1;
        }
    }

    // Animation finished? (ANIMMODE_ONCE: curFrame reaches endFrame and stays)
    // MM: if (PlayerAnimation_Update(play, &this->skelAnime)) { ... }
    if (curFrame >= endFrame) {
        // Disable directional quad AT
        MmForm_DisablePunchQuad(player);

        // Continue combo? (from 2Ship line 18801: sPlayerUseHeldItem = this->av2.actionVar2)
        // The combo advances if B was pressed and we're not on the last punch
        if (gFormState.comboBPressed && step < 2) {
            u8 nextStep = step + 1;
            LinkAnimationHeader* nextAnim = MmForm_GetPunchAttackAnim(nextStep);

            if (nextAnim != NULL) {
                gFormState.comboStep = nextStep;
                gFormState.comboBPressed = 0;
                GoronActionId nextAction = (GoronActionId)(GORON_ACT_PUNCH_A + nextStep);
                MmForm_SetAction(nextAction, play, nextAnim, 1.0f, ANIMMODE_ONCE);
                player->linearVelocity = 0.0f;

                // Play voice + SFX for combo continuation
                MmForm_PlayAttackVoice(player);
                if (gFormState.currentForm == MM_PLAYER_FORM_ZORA) {
                    // From 2Ship: kick (step 2) = NA_SE_IT_GORON_PUNCH_SWING, others = SWORD_SWING_HARD
                    u16 ootSfx = (nextStep == 2) ? NA_SE_IT_SWORD_SWING_HARD : NA_SE_IT_SWORD_SWING_HARD;
                    MmForm_PlaySfx(player, MM_NA_SE_IT_GORON_PUNCH_SWING, ootSfx);
                } else {
                    MmForm_PlaySfx(player, MM_NA_SE_PL_GORON_PUNCH, NA_SE_PL_BODY_HIT);
                }

                // Start root motion for next punch in combo
                MmForm_StartRootMotion(nextStep);

                return;
            }
        }

        // No combo / combo ended → recovery animation
        // MM: Player_CheckHostileLockOn(this) ? attackInfoEntry->unk_8 : attackInfoEntry->unk_4
        // OOT has the same flag: PLAYER_STATE1_HOSTILE_LOCK_ON (1 << 4)
        u8 lockedOn = (player->stateFlags1 & PLAYER_STATE1_HOSTILE_LOCK_ON) ? 1 : 0;
        LinkAnimationHeader* endAnim = MmForm_GetPunchEndAnim(step, lockedOn);

        // Stop root motion (punch is over)
        MmForm_StopRootMotion();

        if (endAnim != NULL) {
            MmForm_SetAction(GORON_ACT_PUNCH_END, play, endAnim, 1.0f, ANIMMODE_ONCE);
        } else {
            MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        }
    }
}

// ---------------------------------------------------------------------------
// Action: PUNCH_END (recovery animation after combo)
// Replaces: the idle transition at end of Player_Action_84
//
// MM: Player_SetAction(play, this, Player_Action_Idle, 1);
//     Player_Anim_PlayOnceWaterAdjustment(play, this, anim);
//     this->stateFlags3 |= PLAYER_STATE3_8;
// ---------------------------------------------------------------------------
static void MmForm_GoronAction_PunchEnd(Player* player, PlayState* play) {
    SkelAnime* skelAnime = &gFormState.formSkelAnime;
    if (skelAnime->animation == NULL) {
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }
    f32 curFrame = skelAnime->curFrame;
    f32 endFrame = Animation_GetLastFrame(skelAnime->animation);

    // Ensure quad is disabled during recovery
    MmForm_DisablePunchQuad(player);

    // Decelerate during recovery
    Math_StepToF(&player->linearVelocity, 0.0f, 5.0f);

    // B hold during/after punch recovery → boomerang aim (Zora only)
    // In MM, you do an action first (punch/kick), THEN hold B to enter boomerang mode.
    {
        Input* input = &play->state.input[0];
        if (CHECK_BTN_ALL(input->cur.button, BTN_B) && !CHECK_BTN_ALL(input->press.button, BTN_B)) {
            if (gFormState.currentForm == MM_PLAYER_FORM_ZORA && gFormState.boomerangState == 0 &&
                gFormState.cutterAttack != NULL) {
                MmForm_StartBoomerangThrow(player, play);
                return;
            }
        }
    }

    // Animation finished → back to idle
    if (curFrame >= endFrame) {
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
    }
}

// =============================================================================
// Phase 5: Damage + Knockback System (from 2Ship z_player.c)
//
// 2Ship reference (func_80833B18, line 5877 + func_80834600, line 6160):
//   func_80834600 dispatches damage by type (freeze/electric/normal/floor)
//   func_80833B18 handles knockback setup for normal damage:
//     - Applies HP damage via func_808339D4 (respects Giant Mask)
//     - Sets invincibility via func_80833998 (invincibilityTimer = 20)
//     - Speed flinch: speedXZ > 4.0 && !lockedOn → unk_B64=20, no knockback
//     - Small knockback (damage < 5): rumble 120, NO speed change
//     - Big knockback (damage >= 5): rumble 180, speedXZ = 23.0
//     - 8 animation variants from D_8085D0D4[] (see MmFormState.dmgAnims[8])
//     - Player_Action_20: Player_DecelerateToZero + Player_TryActionInterrupt(16.0f)
//
// Goron-specific (2Ship line 6205, 6273):
//   NO armor/damage reduction (takes full damage, no shield bounce)
//   Cannot parry (Player_IsGoronOrDeku excludes from shield bounce knockback)
//   Speed flinch (>4.0 moving, not locked on): hitstun only, no action change
//   Fire IMMUNITY: lava floors/hot rooms only (NOT fire projectiles)
//   acHitEffect mapping: 2=freeze(3), 3=electric(4), 7=shock, 9=fire
// =============================================================================

// Deceleration rate: REG(43)/100.0f = sBootData[boots][8]/100.0f = 800/100.0f = 8.0f
// This is the same value OOT's Player_DecelerateToZero uses.
#define DAMAGE_DECEL_RATE 8.0f

/**
 * Select knockback animation from D_8085D0D4[] equivalent.
 * From 2Ship func_80833B18 lines 5980-6002:
 *   animPtr = D_8085D0D4 (index 0)
 *   if (damage >= 5): animPtr += 4, speedXZ = 23.0
 *   if (ABS(relAngle) <= 0x4000): animPtr += 2  (hit from behind)
 *   if (Player_CheckHostileLockOn): animPtr += 1
 *
 * @return index into dmgAnims[8] array
 */
static s32 MmForm_SelectDamageAnim(s32 damage, s16 relAngle, u8 lockedOn) {
    s32 index = 0;
    if (damage >= 5)
        index += 4;
    if (ABS(relAngle) <= 0x4000)
        index += 2; // Back hit
    if (lockedOn)
        index += 1;
    return index;
}

/**
 * Check if player took damage and enter knockback state.
 * Called at the top of MmForm_UpdateActive each frame (highest priority).
 *
 * Replicates 2Ship func_80834600 (line 6160) → func_80833B18 (line 5877)
 * with the following differences from our previous version:
 *   - All 8 knockback animation variants (not just 2)
 *   - Light damage (<5) does NOT set knockback speed (MM behavior)
 *   - Heavy damage (>=5) sets speed = 23.0 (same as MM)
 *   - Speed flinch: sets flinchTimer=20, no action change (from 2Ship line 5973)
 *   - Form-specific voice SFX via MM audio system
 *   - stateFlags2 clearing (PLAYER_STATE2_GRABBED_BY_ENEMY)
 *   - Correct decel rate (8.0, not 2.0)
 *
 * Returns 1 if damage was taken (caller should return), 0 otherwise.
 */
// Forward declaration (defined after roll system)
static void MmForm_ClearRollAttack(Player* player);

static u8 MmForm_CheckDamage(Player* player, PlayState* play) {
    // Already in knockback, void out, or OOT action - clear pending and don't check again
    if (gFormState.goronAction == GORON_ACT_DAMAGE || gFormState.goronAction == MMFORM_ACT_WATER_VOID ||
        gFormState.goronAction == MMFORM_ACT_HAZARD_VOID || gFormState.goronAction == MMFORM_ACT_OOT_ACTION) {
        gMmFormPendingDamage.hasPending = 0;
        return 0;
    }

    // Shield block check (from 2Ship z_player.c line 6076-6106)
    // If shieldCollider's AC_BOUNCED is set, the attack was blocked by the shield
    if (gFormState.goronAction == MMFORM_ACT_SHIELD && gFormState.shieldColliderInitDone) {
        if (gFormState.shieldCollider.base.acFlags & AC_BOUNCED) {
            // Rumble (from 2Ship line 6083: Player_RequestRumble 180, 20, 100)
            func_800AA000(0.0f, 180, 20, 100);

            // Metal spark VFX + reflect SFX (from OOT z_collision_check.c line 3441)
            // CollisionCheck_SpawnShieldParticlesMetal plays NA_SE_IT_SHIELD_REFLECT_SW
            // and spawns white light streak particles at the hit position
            {
                Vec3f hitPos;
                hitPos.x = player->actor.world.pos.x;
                hitPos.y = player->actor.world.pos.y + 30.0f; // Shield center height
                hitPos.z = player->actor.world.pos.z;
                CollisionCheck_SpawnShieldParticlesMetal(play, &hitPos);
            }

            // Knockback (from 2Ship line 6101-6103)
            // Goron/Deku skip the shield bounce animation (line 6084: !Player_IsGoronOrDeku)
            // but still get the -18 speed knockback
            if (!(player->stateFlags1 & (PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_IN_CUTSCENE |
                                         PLAYER_STATE1_FIRST_PERSON | PLAYER_STATE1_CLIMBING_LEDGE))) {
                player->linearVelocity = -18.0f;
                player->yaw = player->actor.shape.rot.y;
            }

            gFormState.shieldCollider.base.acFlags &= ~(AC_BOUNCED | AC_HIT);
            gMmFormPendingDamage.hasPending = 0;
            return 0;
        }
    }

    // Invincible - can't take damage (from 2Ship func_80834600 line 6237)
    // Positive invincibilityTimer = invincible, negative = bounced-back state
    if (player->invincibilityTimer > 0) {
        gMmFormPendingDamage.hasPending = 0;
        return 0;
    }

    // Check for pending damage saved by OOT's func_808382DC.
    // OOT's Player_UpdateCommon processes AC_HIT before TransformMasks_Update runs,
    // then Collider_ResetCylinderAC clears the flag. Our check in func_808382DC
    // saves the damage info to gMmFormPendingDamage so we can process it here.
    if (!gMmFormPendingDamage.hasPending)
        return 0;

    // Consume pending damage
    s32 damage = gMmFormPendingDamage.damage;
    u8 acHitEffect = gMmFormPendingDamage.acHitEffect;
    Actor* attacker = gMmFormPendingDamage.attacker;
    gMmFormPendingDamage.hasPending = 0;

    if (damage <= 0) {
        return 0;
    }

    // Check if attacker has body hit SFX flag (from 2Ship line 6240)
    if (attacker != NULL && (attacker->flags & ACTOR_FLAG_SFX_FOR_PLAYER_BODY_HIT)) {
        Player_PlaySfx(&player->actor, NA_SE_PL_BODY_HIT);
    }

    // Determine damage effect type (from 2Ship func_80834600 lines 6120-6141)
    // acHitEffect → knockbackType mapping:
    //   0 = normal ground knockback (Player_Action_20)
    //   1 = strong launched knockback (Player_Action_21) - player gets launched into air
    //   3 = freeze (Player_Action_82)
    //   4 = electric shock (Player_Action_83)
    s32 knockbackType = 0;
    // acHitEffect already set from gMmFormPendingDamage above

    if (acHitEffect == 2) {
        knockbackType = 3; // Freeze
    } else if (acHitEffect == 3) {
        knockbackType = 4; // Electric shock
    } else if (acHitEffect == 7) {
        knockbackType = 1; // Strong knockback (shock) - launched into air
        player->bodyShockTimer = 40;
    } else if (acHitEffect == 9) {
        knockbackType = 1; // Strong knockback (fire) - launched into air
        // Deku/Zora: fire burns them → set body on fire + mark for hazard void
        // From 2Ship func_80834534 (z_player.c line 6014): fire sets bodyIsBurning
        // From 2Ship z_player.c line 5946: Deku/Zora die from fire
        if (gFormState.currentForm == MM_PLAYER_FORM_DEKU || gFormState.currentForm == MM_PLAYER_FORM_ZORA) {
            s32 i;
            for (i = 0; i < PLAYER_BODYPART_MAX; i++) {
                player->bodyFlameTimers[i] = Rand_S16Offset(0, 200);
            }
            player->bodyIsBurning = true;
            // Mark for void out after knockback animation finishes
            // (transition handled in MmForm_GoronAction_Damage)
            gFormState.hazardVoidType = 2; // fire hit
            gFormState.hazardVoidTimer = 0;
        }
    } else if (acHitEffect == 4) {
        knockbackType = 1; // Strong knockback (heavy hit) - launched into air
    } else {
        knockbackType = 0; // Normal ground knockback
    }

    // Apply HP damage (from 2Ship func_808339D4, line 5830)
    // Respects the damage multiplier enhancement CVar (same as OOT func_80837B18_modified)
    s32 modifiedDamage = damage * (1 << CVarGetInteger("gEnhancements.DamageMult", 0));
    Health_ChangeBy(play, -modifiedDamage);

    // Set invincibility timer (from 2Ship func_80833998, line 5815)
    if (player->invincibilityTimer >= 0) {
        player->invincibilityTimer = 20;
        player->damageFlickerAnimCounter = 0;
    }

    // Play damage SFX (from 2Ship func_80833B18 line 5883)
    Player_PlaySfx(&player->actor, NA_SE_PL_DAMAGE);

    // Play form-specific voice SFX via MM audio system
    // From 2Ship: Player_AnimSfx_PlayVoice(this, NA_SE_VO_LI_DAMAGE_S)
    // MM transforms use different voice banks per form
#if !MM_SOUNDS_DISABLED
    if (MmSfx_IsAvailable()) {
        u16 voiceSfx = 0;
        switch (gFormState.currentForm) {
            case MM_PLAYER_FORM_GORON:
                voiceSfx = MM_NA_SE_VO_GORON_DAMAGE_S;
                break;
            case MM_PLAYER_FORM_ZORA:
                voiceSfx = MM_NA_SE_VO_ZORA_DAMAGE_S;
                break;
            case MM_PLAYER_FORM_DEKU:
                voiceSfx = MM_NA_SE_VO_DEKU_DAMAGE_S;
                break;
            default:
                break;
        }
        if (voiceSfx != 0) {
            MmSfx_PlayAtPos(voiceSfx, &player->actor.projectedPos);
        }
    } else
#endif
    {
        // OOT Link voice (used when MM sounds disabled or mm.o2r unavailable)
        Audio_PlaySoundGeneral(NA_SE_VO_LI_DAMAGE_S, &player->actor.projectedPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }

    // Determine knockback direction (from 2Ship line 6263)
    s16 damageYaw;
    if (attacker != NULL) {
        damageYaw = Actor_WorldYawTowardActor(attacker, &player->actor);
    } else {
        damageYaw = player->actor.shape.rot.y + 0x8000;
    }

    // Relative angle (from 2Ship func_80833B18 line 5946: arg5 -= shape.rot.y)
    s16 relAngle = damageYaw - player->actor.shape.rot.y;

    // Clear grabbed state (from 2Ship func_8082FC60 called in func_80833B18 line 5977)
    player->stateFlags2 &= ~PLAYER_STATE2_GRABBED_BY_ENEMY;

    // Clean up boomerang state if damage interrupts aiming/throwing
    // Without this, boomerangState stays at 1 or 2, blocking future throws.
    if (gFormState.boomerangState == 1 || gFormState.boomerangState == 2) {
        gFormState.boomerangState = 0;
        gFormState.boomerangActorL = NULL;
        gFormState.boomerangActorR = NULL;
        gFormState.boomerangAimYaw = 0;
        gFormState.boomerangAimPitch = 0;
        player->upperLimbRot.y = 0;
        player->upperLimbRot.x = 0;
        player->stateFlags1 &= ~PLAYER_STATE1_BOOMERANG_THROWN;
        player->stateFlags1 &= ~PLAYER_STATE1_FIRST_PERSON;
        player->boomerangActor = NULL;
        // Reset camera from aim mode
        Camera* cam = Play_GetCamera(play, 0);
        Camera_ChangeMode(cam, CAM_MODE_NORMAL);
    }

    // Clean up swim visual state when taking damage while swimming
    // Reset pitch/roll so the model doesn't stay rotated during damage animation
    if (gFormState.currentForm == MM_PLAYER_FORM_ZORA && gFormState.swimState != 0) {
        gFormState.fastSwimActive = 0;
        gFormState.swimPitch = 0;
        gFormState.swimRoll = 0;
        gFormState.swimRollSmoothed = 0;
        gFormState.swimSpeedB48 = 0.0f;
        gFormState.swimYawRate = 0;
        player->actor.shape.rot.x = 0;
        player->actor.shape.rot.z = 0;
    }

    // === Handle special damage types (freeze, electric) ===
    if (knockbackType == 3) {
        // FREEZE: From 2Ship func_80833B18 line 5933-5940
        // In MM: Player_Action_82 with gPlayerAnim_link_normal_ice_down
        // We simplify: use front_hit anim with speed=0, long timer
        func_800AA000(0.0f, 255, 10, 40);
        Player_PlaySfx(&player->actor, NA_SE_PL_FREEZE_S);
        player->linearVelocity = 0.0f;
        player->actor.velocity.y = 0.0f;
        gFormState.damageTimer = 60;
        gFormState.knockbackType = 3;
        // Clean up ball state if hit during roll
        if (gFormState.goronAction == GORON_ACT_GORON_ROLL || gFormState.goronAction == GORON_ACT_GORON_ROLL_JUMP ||
            gFormState.goronAction == GORON_ACT_GORON_ROLL_POUND) {
            player->actor.shape.rot.x = 0;
            player->actor.shape.rot.z = 0;
            player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
            player->actor.shape.shadowScale = gFormState.savedShadowScale;
            player->stateFlags2 &= ~(PLAYER_STATE2_DISABLE_ROTATION_Z_TARGET | PLAYER_STATE2_DISABLE_ROTATION_ALWAYS);
            player->actor.bgCheckFlags &= ~0x800;
            player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
            MmForm_ClearRollAttack(player);
        }
        LinkAnimationHeader* anim = gFormState.dmgAnims[4]; // front_hit as freeze pose
        if (anim == NULL)
            anim = gFormState.idleAnim;
        MmForm_SetAction(GORON_ACT_DAMAGE, play, anim, 1.0f, ANIMMODE_ONCE);
        player->stateFlags1 |= PLAYER_STATE1_DAMAGED;
        MmForm_DisablePunchQuad(player);
        MmForm_StopRootMotion();
        // In water: keep swim gravity so Zora doesn't sink while frozen
        if (gFormState.currentForm == MM_PLAYER_FORM_ZORA && gFormState.swimState != 0) {
            player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_SWIM);
        }
        player->cylinder.base.acFlags &= ~AC_HIT;
        return 1;
    }

    if (knockbackType == 4) {
        // ELECTRIC SHOCK: From 2Ship func_80833B18 line 5941-5948
        // In MM: Player_Action_83 with gPlayerAnim_link_normal_electric_shock loop
        func_800AA000(0.0f, 255, 80, 150);
        player->linearVelocity = 0.0f;
        player->actor.velocity.y = 0.0f;
        player->bodyShockTimer = 40;
        gFormState.damageTimer = 40;
        gFormState.knockbackType = 4;
        // Clean up ball state if hit during roll
        if (gFormState.goronAction == GORON_ACT_GORON_ROLL || gFormState.goronAction == GORON_ACT_GORON_ROLL_JUMP ||
            gFormState.goronAction == GORON_ACT_GORON_ROLL_POUND) {
            player->actor.shape.rot.x = 0;
            player->actor.shape.rot.z = 0;
            player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
            player->actor.shape.shadowScale = gFormState.savedShadowScale;
            player->stateFlags2 &= ~(PLAYER_STATE2_DISABLE_ROTATION_Z_TARGET | PLAYER_STATE2_DISABLE_ROTATION_ALWAYS);
            player->actor.bgCheckFlags &= ~0x800;
            player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
            MmForm_ClearRollAttack(player);
        }
        LinkAnimationHeader* anim = gFormState.dmgAnims[4]; // front_hit as shock pose
        if (anim == NULL)
            anim = gFormState.idleAnim;
        MmForm_SetAction(GORON_ACT_DAMAGE, play, anim, 1.0f, ANIMMODE_ONCE);
        player->stateFlags1 |= PLAYER_STATE1_DAMAGED;
        MmForm_DisablePunchQuad(player);
        MmForm_StopRootMotion();
        // In water: keep swim gravity so Zora doesn't sink while shocked
        if (gFormState.currentForm == MM_PLAYER_FORM_ZORA && gFormState.swimState != 0) {
            player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_SWIM);
        }
        player->cylinder.base.acFlags &= ~AC_HIT;
        return 1;
    }

    // === Strong knockback (knockbackType == 1): launched into air ===
    // From 2Ship func_80833B18 line 5819-5850 (Player_Action_21)
    // Player gets launched with velocity.y = 5.0, speed = 4.0
    // Uses front_downA or back_downA animation (falling down)
    if (knockbackType == 1) {
        func_800AA000(0.0f, 255, 20, 150); // Strong rumble (from 2Ship line 5826)

        // Clean up ball state if hit during roll
        if (gFormState.goronAction == GORON_ACT_GORON_ROLL || gFormState.goronAction == GORON_ACT_GORON_ROLL_JUMP ||
            gFormState.goronAction == GORON_ACT_GORON_ROLL_POUND) {
            player->actor.shape.rot.x = 0;
            player->actor.shape.rot.z = 0;
            player->actor.shape.shadowScale = gFormState.savedShadowScale;
            player->stateFlags2 &= ~(PLAYER_STATE2_DISABLE_ROTATION_Z_TARGET | PLAYER_STATE2_DISABLE_ROTATION_ALWAYS);
            player->actor.bgCheckFlags &= ~0x800;
            player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
            MmForm_ClearRollAttack(player);
        }

        // Set knockback direction (from 2Ship func_80833B18 line 5885-5891)
        player->actor.shape.rot.y += relAngle;
        player->yaw = player->actor.shape.rot.y;
        player->actor.world.rot.y = player->actor.shape.rot.y;
        if (ABS(relAngle) > 0x4000) {
            player->actor.shape.rot.y += 0x8000;
        }

        // Launch into air (from 2Ship line 5839-5841)
        // In water: don't launch vertically, keep swim gravity (buoyancy handles Y)
        if (gFormState.currentForm == MM_PLAYER_FORM_ZORA && gFormState.swimState != 0) {
            player->linearVelocity = 4.0f;
            player->actor.velocity.y = 0.0f;
            player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_SWIM);
        } else {
            player->linearVelocity = 4.0f;
            player->actor.velocity.y = 5.0f;
            player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
            player->actor.bgCheckFlags &= ~1; // Clear ground flag (line 5850)
        }

        // Select animation based on hit direction (from 2Ship line 5843-5847)
        LinkAnimationHeader* anim;
        if (ABS(relAngle) > 0x4000) {
            anim = gFormState.frontDownA; // Hit from behind → launched forward
        } else {
            anim = gFormState.backDownA; // Hit from front → launched backward
        }
        if (anim == NULL)
            anim = gFormState.dmgAnims[4]; // Fallback to front_hit
        if (anim == NULL)
            anim = gFormState.idleAnim;

        gFormState.knockbackType = 1;
        gFormState.damageTimer = 60; // Longer timer for airborne recovery
        MmForm_SetAction(GORON_ACT_DAMAGE, play, anim, 1.0f, ANIMMODE_ONCE);
        player->stateFlags1 |= PLAYER_STATE1_DAMAGED;
        MmForm_DisablePunchQuad(player);
        MmForm_StopRootMotion();
        player->cylinder.base.acFlags &= ~AC_HIT;
        return 1;
    }

    // === Normal ground knockback (knockbackType == 0) ===

    // Speed flinch: moving fast and not locked on → no action change
    // From 2Ship func_80833B18 line 5851-5857
    if ((player->linearVelocity > 4.0f) && !(player->stateFlags1 & PLAYER_STATE1_HOSTILE_LOCK_ON)) {
        gFormState.flinchTimer = 20;
        func_800AA000(0.0f, 120, 20, 10);
        player->cylinder.base.acFlags &= ~AC_HIT;
        return 0; // No action change - player keeps moving
    }

    // Select animation from 8-variant table (from 2Ship D_8085D0D4, line 5859)
    u8 lockedOn = (player->stateFlags1 & PLAYER_STATE1_HOSTILE_LOCK_ON) ? 1 : 0;
    s32 animIndex = MmForm_SelectDamageAnim(damage, relAngle, lockedOn);
    LinkAnimationHeader* anim = gFormState.dmgAnims[animIndex];

    // Fallback chain
    if (anim == NULL)
        anim = gFormState.dmgAnims[4]; // front_hit
    if (anim == NULL)
        anim = gFormState.dmgAnims[0]; // front_shit
    if (anim == NULL)
        anim = gFormState.idleAnim;

    // Rumble + speed (from 2Ship func_80833B18 lines 5980-5994)
    if (damage >= 5) {
        // Heavy damage: strong rumble + knockback speed
        func_800AA000(0.0f, 180, 20, 100);
        player->linearVelocity = 23.0f;
    } else {
        // Light damage: mild rumble, NO speed change (MM behavior!)
        // From 2Ship line 5981: only the animPtr += 4 branch sets speedXZ = 23.0
        // The else branch does NOT touch speedXZ at all.
        func_800AA000(0.0f, 120, 20, 10);
    }

    // Save prev animation translation for root motion reset
    // From 2Ship func_8082DE50: this->skelAnime.prevTransl = this->skelAnime.jointTable[0]
    if (gFormState.formSkelAnime.jointTable != NULL) {
        gFormState.formSkelAnime.prevTransl = gFormState.formSkelAnime.jointTable[0];
    }

    // Set movement/facing direction (from 2Ship func_80833B18 lines 6005-6013)
    player->actor.shape.rot.y += relAngle;
    player->yaw = player->actor.shape.rot.y;
    player->actor.world.rot.y = player->actor.shape.rot.y;
    if (ABS(relAngle) > 0x4000) {
        player->actor.shape.rot.y += 0x8000;
    }

    // If hit during roll, clean up ball state (reset shape rotation, clear attack, restore flags)
    if (gFormState.goronAction == GORON_ACT_GORON_ROLL || gFormState.goronAction == GORON_ACT_GORON_ROLL_JUMP ||
        gFormState.goronAction == GORON_ACT_GORON_ROLL_POUND) {
        player->actor.shape.rot.x = 0;
        player->actor.shape.rot.z = 0;
        player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
        player->actor.shape.shadowScale = gFormState.savedShadowScale;
        player->stateFlags2 &= ~(PLAYER_STATE2_DISABLE_ROTATION_Z_TARGET | PLAYER_STATE2_DISABLE_ROTATION_ALWAYS);
        player->actor.bgCheckFlags &= ~0x800;
        player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
        MmForm_ClearRollAttack(player);
    }

    // Enter damage action
    gFormState.knockbackType = 0;
    gFormState.damageTimer = 40; // Safety timeout
    MmForm_SetAction(GORON_ACT_DAMAGE, play, anim, 1.0f, ANIMMODE_ONCE);

    // Disable punch quad and root motion
    MmForm_DisablePunchQuad(player);
    MmForm_StopRootMotion();

    // In water: keep swim gravity so buoyancy keeps Zora afloat during knockback
    if (gFormState.currentForm == MM_PLAYER_FORM_ZORA && gFormState.swimState != 0) {
        player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_SWIM);
    }

    // Exit first-person mode if we were in bubble aim
    if (gFormState.bubbleCharging) {
        FirstPerson_Exit(player, play);
        gFormState.bubbleCharging = 0;
        gFormState.bubbleCharge = 0.0f;
    }

    // Set state flags (from 2Ship func_80833B18 line 6015)
    player->stateFlags1 |= PLAYER_STATE1_DAMAGED;

    // Clear collision flag
    player->cylinder.base.acFlags &= ~AC_HIT;

    return 1;
}

// ---------------------------------------------------------------------------
// Action: DAMAGE (knockback animation)
// From 2Ship Player_Action_20 (line 15405) - Small ground knockback:
//   Player_DecelerateToZero(this)  → Math_StepToF(&speedXZ, 0.0f, R_DECELERATE_RATE/100.0f)
//   Player_TryActionInterrupt(play, this, &skelAnime, 16.0f) → early recovery at frame 16+
//   If animation finishes or interrupt → func_80836988 (return to idle)
//
// R_DECELERATE_RATE = REG(43) = sBootData[boots][8] = 800 → 800/100.0f = 8.0f
// ---------------------------------------------------------------------------
static void MmForm_GoronAction_Damage(Player* player, PlayState* play) {
    SkelAnime* skelAnime = &gFormState.formSkelAnime;
    if (skelAnime->animation == NULL) {
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }
    f32 curFrame = skelAnime->curFrame;
    f32 endFrame = Animation_GetLastFrame(skelAnime->animation);

    // Decelerate during knockback (from 2Ship Player_DecelerateToZero, line 5271)
    Math_StepToF(&player->linearVelocity, 0.0f, DAMAGE_DECEL_RATE);

    // Ensure punch quad stays disabled
    MmForm_DisablePunchQuad(player);

    // In water: keep buoyancy running so Zora doesn't sink to the ground
    u8 inWater = (gFormState.currentForm == MM_PLAYER_FORM_ZORA && gFormState.swimState != 0 &&
                  player->actor.yDistToWater > ZORA_SURFACE_DEPTH);
    if (inWater) {
        MmForm_WaterBuoyancy(player);
    }

    // Safety timer countdown
    gFormState.damageTimer--;

    // === Strong knockback (launched into air): wait for landing ===
    // From 2Ship Player_Action_21 (line 15384-15400)
    // Player must land on ground before recovering
    if (gFormState.knockbackType == 1) {
        // In water: skip landing wait, recover on timer or anim end
        if (inWater) {
            if (curFrame >= endFrame || gFormState.damageTimer <= 0) {
                player->linearVelocity = 0.0f;
                player->stateFlags1 &= ~PLAYER_STATE1_DAMAGED;
                gFormState.knockbackType = 0;
                MmForm_EnterSwimIdle(player, play);
            }
            return;
        }

        u8 onGround = (player->actor.bgCheckFlags & 1) != 0;

        if (onGround && player->actor.velocity.y <= 0.0f) {
            // Landed after launch - transition to landing recovery
            player->linearVelocity = 0.0f;
            player->actor.velocity.y = 0.0f;
            player->stateFlags1 &= ~PLAYER_STATE1_DAMAGED;
            gFormState.knockbackType = 0;

            // Play landing animation then go to idle
            LinkAnimationHeader* landAnim = gFormState.landing;
            if (landAnim == NULL)
                landAnim = gFormState.idleAnim;
            MmForm_SetAction(GORON_ACT_LAND, play, landAnim, 1.0f, ANIMMODE_ONCE);

            // Landing SFX and rumble
            Player_PlaySfx(&player->actor, NA_SE_PL_BOUND);
            func_800AA000(0.0f, 120, 20, 10);
        } else if (gFormState.damageTimer <= 0) {
            // Safety timeout: force recovery even if airborne
            player->linearVelocity = 0.0f;
            player->stateFlags1 &= ~PLAYER_STATE1_DAMAGED;
            gFormState.knockbackType = 0;
            MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        }
        return;
    }

    // === Normal/freeze/electric knockback: animation-based recovery ===

    // Zora freeze → void out (from 2Ship Player_Action_82 line 18277-18282)
    // After ~6 frames of freeze, Zora transitions to hazard void out.
    if (gFormState.knockbackType == 3 && gFormState.currentForm == MM_PLAYER_FORM_ZORA) {
        if (gFormState.actionTimer >= 6) {
            gFormState.goronAction = MMFORM_ACT_HAZARD_VOID;
            gFormState.hazardVoidType = 0; // freeze
            gFormState.hazardVoidTimer = 0;
            gFormState.actionTimer = 0;
            player->linearVelocity = 0.0f;
            player->actor.velocity.y = 0.0f;
            player->stateFlags1 &= ~PLAYER_STATE1_DAMAGED;
        }
        return;
    }

    // Deku/Zora fire hit → void out after knockback ends
    // From 2Ship func_80834534: fire sets bodyIsBurning, burns to death
    // hazardVoidType is set to 2 in MmForm_CheckDamage when Deku/Zora takes fire hit
    if (gFormState.hazardVoidType == 2 &&
        (gFormState.currentForm == MM_PLAYER_FORM_DEKU || gFormState.currentForm == MM_PLAYER_FORM_ZORA)) {
        if (curFrame >= endFrame || gFormState.damageTimer <= 0) {
            gFormState.goronAction = MMFORM_ACT_HAZARD_VOID;
            gFormState.actionTimer = 0;
            gFormState.hazardVoidTimer = 0;
            player->linearVelocity = 0.0f;
            player->stateFlags1 &= ~PLAYER_STATE1_DAMAGED;
        }
        return;
    }

    // Early recovery via input (from 2Ship Player_TryActionInterrupt, threshold=16.0f)
    // From Player_Action_20 line 15409: Player_TryActionInterrupt(play, this, &skelAnime, 16.0f)
    // If current frame >= 16 and player pushes stick, allow early exit to idle/walk
    if (curFrame >= 16.0f && gFormState.knockbackType == 0) {
        Input* input = &play->state.input[0];
        f32 stickMag = input->cur.stick_x * input->cur.stick_x + input->cur.stick_y * input->cur.stick_y;
        if (stickMag > 100.0f) { // ~10 magnitude threshold
            player->linearVelocity = 0.0f;
            player->stateFlags1 &= ~PLAYER_STATE1_DAMAGED;
            if (inWater) {
                MmForm_EnterSwimIdle(player, play);
            } else {
                MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
            }
            return;
        }
    }

    // Return to idle when animation finished OR safety timer expired
    if (curFrame >= endFrame || gFormState.damageTimer <= 0) {
        player->linearVelocity = 0.0f;
        player->stateFlags1 &= ~PLAYER_STATE1_DAMAGED;
        if (inWater) {
            MmForm_EnterSwimIdle(player, play);
        } else {
            MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        }
    }
}

// =============================================================================
// Ground System: Jump / Fall / Land
//
// From 2Ship Player_Action_14 (jump) and Player_Action_3 (fall):
//   Airborne detection: !(player->actor.bgCheckFlags & 1)
//   Jump: A press from idle → velocity.y = 5.0f (REG(19)/100 from OOT)
//   Fall: walked off ledge or velocity.y goes negative
//   Land: bgCheckFlags & 1 set after being airborne
//
// From 2Ship Player_Action_29 (jump kick/slash during fall):
//   Jump kick (Zora): gravity = -0.8f, damage frames 8-99
//   Shared: gravity = -1.2f (standard)
// =============================================================================

// (defines moved to top of file)

/**
 * Helper: play form-specific MM SFX. No OOT fallback.
 * Either the MM sound plays correctly, or nothing plays.
 *
 * @param player    OOT Player (for position)
 * @param mmSfxId   MM SFX ID (from mm_sfx_ids.h)
 * @param ootSfxId  Unused (kept for call-site compatibility)
 */
static void MmForm_PlaySfx(Player* player, u16 mmSfxId, u16 ootSfxId) {
#if MM_SOUNDS_DISABLED
    // MM audio disabled - play OOT fallback if available
    if (ootSfxId != 0) {
        Player_PlaySfx(&player->actor, ootSfxId);
    }
#else
    // Try MM audio first, fall back to OOT if MM fails
    if (MmSfx_IsAvailable() && mmSfxId != 0) {
        if (MmSfx_PlayAtPos(mmSfxId, &player->actor.projectedPos)) {
            return; // MM sound played successfully
        }
        MMFORM_LOG("[MmForm_PlaySfx] MM 0x%04X failed, fallback to OOT 0x%04X", mmSfxId, ootSfxId);
    } else if (mmSfxId != 0) {
        MMFORM_LOG("[MmForm_PlaySfx] MM not available (avail=%d), fallback to OOT 0x%04X", MmSfx_IsAvailable(),
                   ootSfxId);
    }
    // Fallback: play OOT equivalent
    if (ootSfxId != 0) {
        Player_PlaySfx(&player->actor, ootSfxId);
    }
#endif
}

/**
 * Helper: play form-specific attack voice via MM audio.
 * From 2Ship: each form has its own voice bank for attacks.
 */
static void MmForm_PlayAttackVoice(Player* player) {
#if !MM_SOUNDS_DISABLED
    if (MmSfx_IsAvailable()) {
        u16 voiceSfx = 0;
        switch (gFormState.currentForm) {
            case MM_PLAYER_FORM_GORON:
                voiceSfx = MM_NA_SE_VO_GORON_SWORD_N;
                break;
            case MM_PLAYER_FORM_ZORA:
                voiceSfx = MM_NA_SE_VO_ZORA_SWORD_N;
                break;
            case MM_PLAYER_FORM_DEKU:
                voiceSfx = MM_NA_SE_VO_DEKU_SWORD_N;
                break;
            default:
                break;
        }
        if (voiceSfx != 0) {
            MmSfx_PlayAtPos(voiceSfx, &player->actor.projectedPos);
        }
    }
#endif
}

/**
 * Helper: check if player is in any Z-targeting / strafe mode.
 * From 2Ship func_8082EEE0 (z_player.c line 287):
 *   return (this->stateFlags1 & (Z_TARGETING | HOSTILE_LOCK_ON | FRIENDLY_FOCUS)) != 0
 *
 * OOT flags checked:
 *   PLAYER_STATE1_HOSTILE_LOCK_ON (1<<4)       = locked onto enemy
 *   PLAYER_STATE1_Z_TARGETING (1<<15)          = Z button held (any mode)
 *   PLAYER_STATE1_FRIENDLY_ACTOR_FOCUS (1<<16) = locked onto NPC
 *   PLAYER_STATE1_PARALLEL (1<<17)             = Z-target without actor (free strafe)
 */
static u8 MmForm_IsZTargeting(Player* player) {
    // Boomerang flight forces Z-target mode (from MM: Player_UpperAction_14 calls Player_SetParallel)
    // This lets Zora strafe, jump attack with A+forward, etc. while fins are in the air.
    if (gFormState.boomerangState == 3)
        return 1;

    return (player->stateFlags1 & (PLAYER_STATE1_HOSTILE_LOCK_ON | PLAYER_STATE1_Z_TARGETING |
                                   PLAYER_STATE1_FRIENDLY_ACTOR_FOCUS | PLAYER_STATE1_PARALLEL))
               ? 1
               : 0;
}

/**
 * Helper: get relative angle between stick direction and player facing.
 * Returns the angle difference: stick world angle - player yaw.
 * Used for Z-target strafe direction detection.
 */
static s16 MmForm_GetStickRelAngle(Player* player, PlayState* play) {
    Input* input = &play->state.input[0];
    // From OOT func_80077D10 (z_lib.c line 211): Math_Atan2S(relY, -relX)
    // MUST use rel.stick (deadzone-adjusted) and Camera_GetInputDirYaw (input-mapped direction)
    // to match OOT's Player_ProcessControlStick exactly. Using cur.stick or Camera_GetCamDirYaw
    // produces wrong angles during Z-targeting (camDir != inputDir).
    s16 stickAngle = (s16)Math_Atan2S(input->rel.stick_y, -input->rel.stick_x);
    s16 camYaw = Camera_GetInputDirYaw(GET_ACTIVE_CAM(play));
    s16 worldStickAngle = stickAngle + camYaw;
    return worldStickAngle - player->actor.shape.rot.y;
}

/**
 * Helper: get OOT's pre-computed stick direction (from Player_ProcessControlStick).
 * Returns: 0=forward, 1=left, 2=backward, 3=right, -1=none (below threshold).
 * This matches OOT's direction calculation exactly (uses Camera_GetInputDirYaw + rel.stick).
 */
static s32 MmForm_GetStickDirection(Player* player) {
    return player->controlStickDirections[player->controlStickDataIndex];
}

/**
 * Helper: get stick magnitude (analog stick push distance).
 */
static f32 MmForm_GetStickMagnitude(PlayState* play) {
    Input* input = &play->state.input[0];
    f32 sx = (f32)input->rel.stick_x;
    f32 sy = (f32)input->rel.stick_y;
    return sqrtf(sx * sx + sy * sy);
}

// ---------------------------------------------------------------------------
// Action: JUMP (ascending after A press or being launched)
// From 2Ship Player_Action_14 (line 15066):
//   Plays link_normal_jump, transitions to FALL when velocity.y <= 0
//   B press during jump → jump kick
// ---------------------------------------------------------------------------
static void MmForm_Action_Jump(Player* player, PlayState* play) {
    // B press → jump kick (Zora only, from 2Ship sMeleeAttackAnimInfo)
    if (gFormState.jumpKick != NULL) {
        Input* input = &play->state.input[0];
        if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
            // From 2Ship func_80834734 (line 6357): jump kick setup
            // linearVelocity *= 1.1f (speed boost on kick start)
            // velocity.y *= 0.9f (slight downward pull)
            player->linearVelocity *= 1.1f;
            player->actor.velocity.y *= 0.9f;
            player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_JUMP_KICK);
            gFormState.jumpKickActive = 0;
            MmForm_SetAction(MMFORM_ACT_JUMP_KICK, play, gFormState.jumpKick, 1.0f, ANIMMODE_ONCE);
            return;
        }
    }

    // Transition: velocity.y <= 0 → falling
    if (player->actor.velocity.y <= 0.0f) {
        LinkAnimationHeader* fallAnim = gFormState.fallAnim ? gFormState.fallAnim : gFormState.idleAnim;
        MmForm_SetAction(MMFORM_ACT_FALL, play, fallAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }
}

// ---------------------------------------------------------------------------
// Action: FALL (descending/airborne)
// From 2Ship Player_Action_3 (line 14690):
//   Plays link_normal_fall loop. Lands when ground detected.
//   B press during fall → jump kick
// ---------------------------------------------------------------------------
static void MmForm_Action_Fall(Player* player, PlayState* play) {
    // B press → jump kick (Zora only)
    if (gFormState.jumpKick != NULL) {
        Input* input = &play->state.input[0];
        if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
            player->linearVelocity *= 1.1f;
            player->actor.velocity.y *= 0.9f;
            player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_JUMP_KICK);
            gFormState.jumpKickActive = 0;
            MmForm_SetAction(MMFORM_ACT_JUMP_KICK, play, gFormState.jumpKick, 1.0f, ANIMMODE_ONCE);
            return;
        }
    }

    // Landing is handled centrally in MmForm_UpdateActive (ground detection)
}

// ---------------------------------------------------------------------------
// Action: JUMP_KICK (aerial B attack / Z-target jump attack)
// From 2Ship Player_Action_29 (z_player.c:15382):
//   Zora: gravity -0.8f, pz_jumpAT (13 frames), damage frames 8-99
//   In-air: deceleration via func_8083CBC4 with stick steering
//   On land: play pz_jumpATend recovery
// ---------------------------------------------------------------------------
static void MmForm_Action_JumpKick(Player* player, PlayState* play) {
    f32 curFrame = gFormState.formSkelAnime.curFrame;

    // Prevent OOT's actionFunc from interfering (set every frame, cleared on landing).
    // OOT's Player_UpdateCommon clears this AFTER the actionFunc check, so setting here
    // persists through next frame's check. This lets us control linearVelocity/yaw directly.
    player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;

    // Maintain gravity (from MM: Zora = -0.8f)
    player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_JUMP_KICK);

    // In-air deceleration (from MM func_8083CBC4, z_player.c:9252)
    // Vanilla: Player_GetMovementSpeedAndYaw → func_8083CBC4(speed, yaw, 1.0f, 0.05f, 0.1f, 200)
    // This provides: natural speed decay, slight stick steering, brake on >135° turn
    if (!MMFORM_ON_GROUND(player)) {
        f32 speedTarget = 0.0f;
        s16 yawTarget = player->yaw;

        // Get desired direction from stick input (allows slight mid-air steering)
        Input* input = &play->state.input[0];
        f32 stickMagSq =
            (f32)(input->rel.stick_x * input->rel.stick_x) + (f32)(input->rel.stick_y * input->rel.stick_y);
        if (stickMagSq > 400.0f) { // ~20 stick magnitude
            s16 stickAngle = Math_Atan2S((f32)-input->rel.stick_x, (f32)-input->rel.stick_y);
            yawTarget = Camera_GetCamDirYaw(Play_GetCamera(play, 0)) + stickAngle;
            // Speed target from stick magnitude (same as SPEED_MODE_LINEAR)
            f32 mag = sqrtf(stickMagSq);
            if (mag > 60.0f)
                mag = 60.0f;
            speedTarget = mag * 0.05f; // 0-3.0 range
        }

        // func_8083CBC4 logic: decelerate with asymmetric stepping
        s16 yawDiff = player->yaw - yawTarget;

        f32 decelStep = 1.0f;
        f32 accelStep = 0.05f;
        f32 decelAlt = 0.1f;

        if (ABS(yawDiff) > 0x6000) {
            // >135° turn: decelerate to 0 first, then snap yaw
            if (Math_StepToF(&player->linearVelocity, 0.0f, decelStep)) {
                player->yaw = yawTarget;
            }
        } else {
            // Normal: smooth speed approach + yaw turning
            Math_AsymStepToF(&player->linearVelocity, speedTarget, accelStep, decelAlt);
            Math_SmoothStepToS(&player->yaw, yawTarget, 200, 0x190, 0xA);
        }
    }

    // Sync world rotation to our yaw (OOT pipeline uses world.rot.y for movement)
    player->actor.world.rot.y = player->yaw;

    // Damage detection (from MM sMeleeAttackAnimInfo[18]: hit frames 8-99)
    // DMG_ZORA_PUNCH → OOT DMG_JUMP_MASTER, damage=2, both quads [0] and [1]
    if (curFrame >= 8.0f && !gFormState.jumpKickActive) {
        gFormState.jumpKickActive = 1;
    }

    if (gFormState.jumpKickActive) {
        MmForm_EnableJumpKickQuad(player, play, MMFORM_JUMP_KICK_DAMAGE);
    }

    // Landing is handled centrally in MmForm_UpdateActive
}

// ---------------------------------------------------------------------------
// Action: SIDEHOP (Z + sideways + A)
// From 2Ship Player_Action_29 / func_808399A0 (line 8220):
//   velocity.y = 3.5f, speedXZ = 8.5f
//   Uses fighter_Lside_jump or fighter_Rside_jump
//   Gravity deceleration handles descent
//   On land → recovery anim (sidehopEnd) → idle
// ---------------------------------------------------------------------------
static void MmForm_Action_Sidehop(Player* player, PlayState* play) {
    // From 2Ship Player_Action_25 (line 15551): read stick for air control each frame
    f32 speedTarget;
    s16 yawTarget;
    Player_GetMovementSpeedAndYaw(player, &speedTarget, &yawTarget, SPEED_MODE_LINEAR, play);

    // From 2Ship Player_Action_25 lines 15589/15599: form-specific air control (func_8083CBC4)
    if (gFormState.currentForm == MM_PLAYER_FORM_DEKU) {
        MmForm_AirControl(player, speedTarget * 0.5f, yawTarget, 2.0f, 0.2f, 0.1f, 0x190);
    } else {
        MmForm_AirControl(player, speedTarget, yawTarget, 1.0f, 0.05f, 0.1f, 0xC8);
    }

    // Landing (skip first 3 frames to avoid ground re-trigger on initiation)
    if (gFormState.actionTimer >= 3 && MMFORM_ON_GROUND(player)) {
        // Sidehop landing: go straight to idle for fast chaining (no end anim)
        MmForm_PlaySfx(player, MM_NA_SE_PL_LAND, NA_SE_PL_LAND);
        if (MmForm_IsZTargeting(player)) {
            LinkAnimationHeader* ztAnim = gFormState.ztargetIdleR ? gFormState.ztargetIdleR : gFormState.idleAnim;
            MmForm_SetAction(MMFORM_ACT_ZTARGET_IDLE, play, ztAnim, 1.0f, ANIMMODE_LOOP);
        } else {
            MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        }
        return;
    }
}

// ---------------------------------------------------------------------------
// Action: BACKFLIP (Z + back + A)
// From 2Ship func_80839860 (line 8162):
//   velocity.y = 5.8f, speedXZ = 6.0f
//   Uses fighter_backturn_jump
//   Air control via func_8083CBC4 each frame
//   On land → straight to idle (no end anim for transformations)
// ---------------------------------------------------------------------------
static void MmForm_Action_Backflip(Player* player, PlayState* play) {
    // From 2Ship Player_Action_25 (line 15551): read stick for air control each frame
    f32 speedTarget;
    s16 yawTarget;
    Player_GetMovementSpeedAndYaw(player, &speedTarget, &yawTarget, SPEED_MODE_LINEAR, play);

    // From 2Ship Player_Action_25 lines 15589/15599: form-specific air control (func_8083CBC4)
    if (gFormState.currentForm == MM_PLAYER_FORM_DEKU) {
        MmForm_AirControl(player, speedTarget * 0.5f, yawTarget, 2.0f, 0.2f, 0.1f, 0x190);
    } else {
        MmForm_AirControl(player, speedTarget, yawTarget, 1.0f, 0.05f, 0.1f, 0xC8);
    }

    // Landing (skip first 3 frames to avoid ground re-trigger on initiation)
    if (gFormState.actionTimer >= 3 && MMFORM_ON_GROUND(player)) {
        // Backflip landing: go straight to idle (no end anim for transformations)
        MmForm_PlaySfx(player, MM_NA_SE_PL_LAND, NA_SE_PL_LAND);
        if (MmForm_IsZTargeting(player)) {
            LinkAnimationHeader* ztAnim = gFormState.ztargetIdleR ? gFormState.ztargetIdleR : gFormState.idleAnim;
            MmForm_SetAction(MMFORM_ACT_ZTARGET_IDLE, play, ztAnim, 1.0f, ANIMMODE_LOOP);
        } else {
            MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        }
        return;
    }
}

// ---------------------------------------------------------------------------
// Action: ROLL (running + A forward roll)
// From 2Ship Player_Action_10 (line 14966):
//   Uses link_normal_landing_roll_free
//   Speed decay: Math_StepToF(&speedXZ, 0, 2.0f)
//   Damage frames 8-18 (from sMeleeAttackAnimInfo)
//   At end → idle
// ---------------------------------------------------------------------------
static void MmForm_Action_Roll(Player* player, PlayState* play) {
    SkelAnime* skelAnime = &gFormState.formSkelAnime;
    if (skelAnime->animation == NULL) {
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }

    // From 2Ship Player_Action_26 line 15674
    player->stateFlags2 |= PLAYER_STATE2_DISABLE_ROTATION_ALWAYS;

    // Advance animation
    s32 animDone = LinkAnimation_Update(play, &gFormState.formSkelAnime);
    f32 curFrame = skelAnime->curFrame;

    // Roll attack: frames 8-18 (from 2Ship line 15680-15685: Player_SetCylinderForAttack DMG_NORMAL_ROLL)
    // Use DMG_SLASH_KOKIRI (basic sword) — breaks pots/crates but doesn't one-shot enemies
    if (curFrame >= 8.0f && curFrame < 18.0f) {
        ColliderQuad* quad = &player->meleeWeaponQuads[0];
        Collider_ResetQuadAT(play, &quad->base);
        MmForm_SetPunchQuadVertices(player, 2); // step=2: wide centered
        quad->base.atFlags = AT_ON | AT_TYPE_PLAYER;
        quad->info.toucher.dmgFlags = DMG_SLASH_KOKIRI; // Basic sword — breaks objects, weak to enemies
        quad->info.toucher.damage = 1;
        quad->info.toucherFlags = TOUCH_ON | TOUCH_NEAREST;
        CollisionCheck_SetAT(play, &play->colChkCtx, &quad->base);
    } else {
        MmForm_DisablePunchQuad(player);
    }

    // === BONK RECOVERY STATE (from OOT Player_Action_Roll z_player.c:9967-9974) ===
    // rollSpeed < 0 means we bonked (set by bonk detection below)
    if (gFormState.rollSpeed < 0.0f) {
        Math_StepToF(&player->linearVelocity, 0.0f, 2.0f); // OOT uses 2.0 decel rate
        MmForm_DisablePunchQuad(player);

        // Recovery: wait for deceleration to finish + some extra frames
        // actionTimer auto-increments in main loop, no need to add here
        if (gFormState.actionTimer > 15 || (gFormState.actionTimer > 5 && fabsf(player->linearVelocity) < 0.1f)) {
            player->linearVelocity = 0.0f;
            player->stateFlags2 &= ~PLAYER_STATE2_DISABLE_ROTATION_ALWAYS;
            MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        }
        return;
    }

    // === WALL BONK DETECTION (from OOT Player_Action_Roll, z_player.c:9987-10023) ===
    // OOT computes sWorldYawToTouchedWall at z_player.c:11543-11544:
    //   sWorldYawToTouchedWall = ABS(this->actor.wallYaw - this->actor.world.rot.y)
    // Bonks if < 0x2000 (wall normal roughly aligned with movement direction).
    // Speed threshold: 7.0 (from OOT z_player.c:9989)
    if (player->linearVelocity >= 7.0f) {
        u8 doBonk = 0;

        // Wall bonk (EXACT OOT formula: z_player.c:9991)
        if (player->actor.bgCheckFlags & 0x200) {
            s16 worldYawToTouchedWall = player->actor.wallYaw - player->actor.world.rot.y;
            worldYawToTouchedWall = ABS(worldYawToTouchedWall);
            if (worldYawToTouchedWall < 0x2000) {
                doBonk = 1;
            }
        }

        // OC cylinder collision with trees (from OOT z_player.c:9980-9983)
        if (!doBonk && (player->cylinder.base.ocFlags1 & OC1_HIT)) {
            Actor* ocActor = player->cylinder.base.oc;
            if (ocActor != NULL && ocActor->id == ACTOR_EN_WOOD02) {
                s16 actorYawDiff = (s16)(player->actor.world.rot.y - ocActor->yawTowardsPlayer);
                if (ABS(actorYawDiff) > 0x6000) {
                    doBonk = 1;
                }
            }
        }

        if (doBonk) {
            // Bonk! Full velocity reversal (from OOT z_player.c:10000)
            player->linearVelocity = -player->linearVelocity;
            gFormState.rollSpeed = -1.0f; // Mark bonked
            gFormState.actionTimer = 0;   // Reset timer for recovery countdown

            // Play hip_down bonk animation (OOT z_player.c:10011)
            // Standard 22-limb Link anim — works on all MM form skeletons
            {
                LinkAnimationHeader* hipDown = (LinkAnimationHeader*)gPlayerAnim_link_normal_hip_down_free;
                LinkAnimation_Change(play, &gFormState.formSkelAnime, hipDown, 1.0f, 0.0f,
                                     Animation_GetLastFrame(hipDown), ANIMMODE_ONCE, -6.0f);
            }

            // Quake + rumble (EXACT OOT z_player.c:10013-10016)
            {
                s32 quakeIdx = Quake_Add(GET_ACTIVE_CAM(play), 3);
                Quake_SetSpeed(quakeIdx, 33267);
                Quake_SetQuakeValues(quakeIdx, 3, 0, 0, 0);
                Quake_SetCountdown(quakeIdx, 12);
            }
            func_800AA000(0.0f, 255, 20, 150);
            Player_PlaySfx(&player->actor, NA_SE_PL_BODY_HIT);
            Audio_PlayActorSound2(&player->actor, NA_SE_VO_LI_CLIMB_END);

            MmForm_DisablePunchQuad(player);
            return;
        }
    }

    // === NORMAL ROLL MOVEMENT (from 2Ship Player_Action_26 line 15699-15721) ===

    // Frame 20+: end roll (from 2Ship line 15703)
    if (curFrame >= 20.0f) {
        MmForm_DisablePunchQuad(player);
        player->stateFlags2 &= ~PLAYER_STATE2_DISABLE_ROTATION_ALWAYS;
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }

    // Animation finished → idle
    if (animDone) {
        MmForm_DisablePunchQuad(player);
        player->stateFlags2 &= ~PLAYER_STATE2_DISABLE_ROTATION_ALWAYS;
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }

    // Speed from stick × 1.5, minimum 3.0 (from 2Ship line 15706-15714)
    f32 speedTarget = 0.0f;
    s16 yawTarget = player->actor.shape.rot.y;
    Player_GetMovementSpeedAndYaw(player, &speedTarget, &yawTarget, SPEED_MODE_LINEAR, play);
    speedTarget *= 1.5f;
    if (speedTarget < 3.0f) {
        speedTarget = 3.0f;
    }
    // Apply speed toward target (from 2Ship func_8083CB58)
    Math_StepToF(&player->linearVelocity, speedTarget, 2.0f);

    // Dust effects (from 2Ship line 15719: func_8083FBC4)
    MmForm_SpawnMovementDust(play, player);
}

// ---------------------------------------------------------------------------
// Action: Z-TARGET IDLE (standing while locked on)
// From 2Ship Player_Action_Idle when Z-targeting:
//   Uses link_normal_waitR_free or link_normal_waitL_free
//   Based on relative angle to target
//   A button + direction → sidehop/backflip
// ---------------------------------------------------------------------------
static void MmForm_Action_ZTargetIdle(Player* player, PlayState* play) {
    Input* input = &play->state.input[0];

    // If no longer Z-targeting → return to normal idle
    if (!MmForm_IsZTargeting(player)) {
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }

    // R button → shield stance (from 2Ship Player_ActionHandler_11)
    if (CHECK_BTN_ALL(input->cur.button, BTN_R)) {
        if (gFormState.currentForm == MM_PLAYER_FORM_GORON || gFormState.currentForm == MM_PLAYER_FORM_ZORA ||
            gFormState.currentForm == MM_PLAYER_FORM_DEKU) {
            MmForm_EnterShield(player, play);
            return;
        }
    }

    // B press → punch combo (Zora boomerang is B-HOLD after an action)
    if (gFormState.currentForm == MM_PLAYER_FORM_GORON || gFormState.currentForm == MM_PLAYER_FORM_ZORA) {
        if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
            MmForm_StartPunch(player, play);
            return;
        }
    }

    // A button + stick → sidehop or backflip
    // From 2Ship Player_ActionHandler_10 (line 8370):
    //   direction <= FORWARD → jump/jumpslash
    //   direction > FORWARD (LEFT/BACKWARD/RIGHT) → sidehop via func_80839860
    // From 2Ship func_80839860 (line 8293):
    //   yaw = rot.y + (direction << 0xE)
    //   direction & 1 (LEFT/RIGHT): vel_y=3.5, speed=8.5, SFX=NA_SE_PL_SKIP
    //   !(direction & 1) (BACK): vel_y=5.8, speed=6.0, SFX=NA_SE_PL_ROLL
    if (CHECK_BTN_ALL(input->press.button, BTN_A) && MMFORM_ON_GROUND(player)) {
        // Use OOT's pre-computed stick direction (from Player_ProcessControlStick).
        // This uses Camera_GetInputDirYaw which differs from Camera_GetCamDirYaw during Z-targeting.
        s32 direction = MmForm_GetStickDirection(player);

        if (direction >= 0) { // direction >= 0 means stick is pushed past threshold

            if (direction == 0) {
                // FORWARD + A while Z-targeting:
                // Goron → curl to roll (2Ship func_80836B3C line 6937: GORON → func_80836AD8)
                // Zora → jump attack kick (2Ship func_80839770 + func_808395F0)
                // Others → jump
                if (gFormState.currentForm == MM_PLAYER_FORM_GORON && gFormState.maruChange != NULL) {
                    player->linearVelocity = 0.0f;
                    MmForm_SetAction(GORON_ACT_ROLL_INIT, play, gFormState.maruChange, 0.67f, ANIMMODE_ONCE);
                    MmForm_PlaySfx(player, MM_NA_SE_PL_GORON_TO_BALL, NA_SE_PL_BODY_HIT);
                    return;
                }
                // Zora: forward+A during Z-target
                if (gFormState.currentForm == MM_PLAYER_FORM_ZORA) {
                    // On underwater floor (dive mode): roll instead of jump kick
                    if (gFormState.zoraBoots == 1 && gFormState.swimState > 0 && gFormState.rollAnim != NULL) {
                        gFormState.rollSpeed = 1.0f;
                        player->yaw = player->actor.shape.rot.y;
                        player->actor.world.rot.y = player->actor.shape.rot.y;
                        MmForm_SetAction(MMFORM_ACT_ROLL, play, gFormState.rollAnim, 1.25f, ANIMMODE_ONCE);
                        MmForm_PlaySfx(player, MM_NA_SE_PL_LAND, NA_SE_PL_BODY_HIT);
                        return;
                    }
                    // Normal (surface/air): jump kick
                    // From MM func_808395F0: base (5.0f, 5.0f) → Zora: 5.5f, 4.5f
                    if (gFormState.jumpKick != NULL) {
                        player->linearVelocity = 5.5f;
                        player->actor.velocity.y = 4.5f;
                        player->actor.bgCheckFlags &= ~1;
                        player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_JUMP_KICK);
                        player->yaw = player->actor.shape.rot.y;
                        player->actor.world.rot.y = player->yaw;
                        gFormState.jumpKickActive = 0;
                        gFormState.wasOnGround = 0;
                        MmForm_SetAction(MMFORM_ACT_JUMP_KICK, play, gFormState.jumpKick, 1.0f, ANIMMODE_ONCE);
                        MmForm_PlayAttackVoice(player);
                        MmForm_PlaySfx(player, MM_NA_SE_PL_JUMP, NA_SE_PL_JUMP);
                        return;
                    }
                }
                // Other forms: forward+A during Z-target does nothing
                // (In MM: Human/FD do jump slash, Deku does spin — not a regular jump)
            } else if (direction == 2) {
                // BACKWARD → backflip
                // From 2Ship: !(direction & 1) → vel_y=5.8, speed=6.0
                if (gFormState.backflip != NULL) {
                    player->actor.velocity.y = MMFORM_BACKFLIP_VEL_Y;
                    player->linearVelocity = MMFORM_BACKFLIP_SPEED;
                    player->yaw = player->actor.shape.rot.y + (s16)(direction << 0xE); // 0x8000
                    player->actor.world.rot.y = player->yaw;
                    player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
                    player->actor.bgCheckFlags &= ~1;
                    gFormState.wasOnGround = 0;
                    MmForm_SetAction(MMFORM_ACT_BACKFLIP, play, gFormState.backflip, PLAYER_ANIM_ADJUSTED_SPEED,
                                     ANIMMODE_ONCE);
                    MmForm_PlaySfx(player, MM_NA_SE_PL_JUMP, NA_SE_PL_ROLL);
                    return;
                }
            } else {
                // LEFT (1) or RIGHT (3) → sidehop
                // From 2Ship: direction & 1 → vel_y=3.5, speed=8.5
                // D_8085C2A4[1] = Lside_jump, D_8085C2A4[3] = Rside_jump
                LinkAnimationHeader* hopAnim = (direction == 1) ? gFormState.sidehopL : gFormState.sidehopR;
                if (hopAnim != NULL) {
                    player->actor.velocity.y = MMFORM_SIDEHOP_VEL_Y;
                    player->linearVelocity = MMFORM_SIDEHOP_SPEED;
                    player->yaw = player->actor.shape.rot.y + (s16)(direction << 0xE);
                    player->actor.world.rot.y = player->yaw;
                    player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
                    player->actor.bgCheckFlags &= ~1;
                    gFormState.wasOnGround = 0;
                    gFormState.sidehopDir = (direction == 1) ? -1 : 1;
                    MmForm_SetAction(MMFORM_ACT_SIDEHOP, play, hopAnim, PLAYER_ANIM_ADJUSTED_SPEED, ANIMMODE_ONCE);
                    MmForm_PlaySfx(player, MM_NA_SE_PL_JUMP, NA_SE_PL_SKIP);
                    return;
                }
            }
        } else {
            // No stick + A while Z-targeting:
            // No stick + A during Z-target = same as forward+A (form-specific action)
            // Goron → curl, Zora → jump kick, Deku → spin
            if (gFormState.currentForm == MM_PLAYER_FORM_GORON && gFormState.maruChange != NULL) {
                player->linearVelocity = 0.0f;
                MmForm_SetAction(GORON_ACT_ROLL_INIT, play, gFormState.maruChange, 0.67f, ANIMMODE_ONCE);
                MmForm_PlaySfx(player, MM_NA_SE_PL_GORON_TO_BALL, NA_SE_PL_BODY_HIT);
                return;
            }
            if (gFormState.currentForm == MM_PLAYER_FORM_ZORA) {
                // On underwater floor: roll instead of jump kick
                if (gFormState.zoraBoots == 1 && gFormState.swimState > 0 && gFormState.rollAnim != NULL) {
                    gFormState.rollSpeed = 1.0f;
                    player->yaw = player->actor.shape.rot.y;
                    player->actor.world.rot.y = player->actor.shape.rot.y;
                    MmForm_SetAction(MMFORM_ACT_ROLL, play, gFormState.rollAnim, 1.25f, ANIMMODE_ONCE);
                    MmForm_PlaySfx(player, MM_NA_SE_PL_LAND, NA_SE_PL_BODY_HIT);
                    return;
                }
                // Normal: jump kick
                // From MM func_808395F0: base (5.0f, 5.0f) → Zora: 5.5f, 4.5f
                if (gFormState.jumpKick != NULL) {
                    player->linearVelocity = 5.5f;
                    player->actor.velocity.y = 4.5f;
                    player->actor.bgCheckFlags &= ~1;
                    player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_JUMP_KICK);
                    player->yaw = player->actor.shape.rot.y;
                    player->actor.world.rot.y = player->yaw;
                    gFormState.jumpKickActive = 0;
                    gFormState.wasOnGround = 0;
                    MmForm_SetAction(MMFORM_ACT_JUMP_KICK, play, gFormState.jumpKick, 1.0f, ANIMMODE_ONCE);
                    MmForm_PlayAttackVoice(player);
                    MmForm_PlaySfx(player, MM_NA_SE_PL_JUMP, NA_SE_PL_JUMP);
                    return;
                }
            }
            if (gFormState.currentForm == MM_PLAYER_FORM_DEKU && gFormState.dekuSpinAttack != NULL) {
                MmForm_SetAction(MMFORM_ACT_DEKU_SPIN, play, gFormState.dekuSpinAttack, 1.0f, ANIMMODE_ONCE);
                gFormState.dekuSpinSpeed = 20000.0f;
                gFormState.dekuSpinTimer = 196608.0f;
                gFormState.dekuSpinActive = 1;
                gFormState.dekuSpinRotAccum = 0;
                player->stateFlags2 |= PLAYER_STATE2_DISABLE_ROTATION_ALWAYS;
                MmForm_PlaySfx(player, MM_NA_SE_PL_DEKUNUTS_ATTACK, NA_SE_PL_BODY_HIT);
                return;
            }
            // FD: no action from Z-target neutral A (jump slash not implemented)
        }
    }

    // Movement while Z-targeting → strafe
    f32 stickMag = MmForm_GetStickMagnitude(play);
    if (stickMag > 20.0f) {
        // Switch to Z-target walk/strafe
        MmForm_SetAction(MMFORM_ACT_ZTARGET_WALK, play, NULL, 1.0f, ANIMMODE_LOOP);
        return;
    }
}

// ---------------------------------------------------------------------------
// Action: Z-TARGET WALK (strafing while locked on)
// From 2Ship Player_Action_5 when Z-targeting:
//   Stick left → side_walkL_free, right → side_walkR_free
//   Stick back → back_walk, forward → normal walk
//   Speed proportional to stick magnitude
// ---------------------------------------------------------------------------
static void MmForm_Action_ZTargetWalk(Player* player, PlayState* play) {
    Input* input = &play->state.input[0];

    // If no longer Z-targeting → return to normal idle/walk
    if (!MmForm_IsZTargeting(player)) {
        f32 speed = player->linearVelocity;
        if (speed >= 0.5f) {
            LinkAnimationHeader* walkAnim = gFormState.walkAnim ? gFormState.walkAnim : gFormState.idleAnim;
            MmForm_SetAction(GORON_ACT_WALK, play, walkAnim, speed * 0.3f + 1.0f, ANIMMODE_LOOP);
        } else {
            MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        }
        return;
    }

    // R button → shield stance (from 2Ship Player_ActionHandler_11)
    if (CHECK_BTN_ALL(input->cur.button, BTN_R)) {
        if (gFormState.currentForm == MM_PLAYER_FORM_GORON || gFormState.currentForm == MM_PLAYER_FORM_ZORA ||
            gFormState.currentForm == MM_PLAYER_FORM_DEKU) {
            MmForm_EnterShield(player, play);
            return;
        }
    }

    // B press → boomerang/punch (Zora: boomerang first, punch if fins out)
    if (gFormState.currentForm == MM_PLAYER_FORM_GORON || gFormState.currentForm == MM_PLAYER_FORM_ZORA) {
        if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
            if (gFormState.currentForm == MM_PLAYER_FORM_ZORA && gFormState.boomerangState == 0 &&
                gFormState.cutterAttack != NULL) {
                MmForm_StartBoomerangThrow(player, play);
            } else {
                MmForm_StartPunch(player, play);
            }
            return;
        }
    }

    // A button + direction → evasive maneuver or jump attack
    // From 2Ship func_80839860: yaw = rot.y + (direction << 0xE)
    // From 2Ship func_80839770: direction 0 (FORWARD) → jump attack (Zora/Human only)
    if (CHECK_BTN_ALL(input->press.button, BTN_A) && MMFORM_ON_GROUND(player)) {
        // Use OOT's pre-computed stick direction (from Player_ProcessControlStick).
        s32 direction = MmForm_GetStickDirection(player);

        // Forward + A → Goron curl (2Ship func_80836B3C), Zora jump attack, others jump
        if (direction == 0) {
            if (gFormState.currentForm == MM_PLAYER_FORM_GORON && gFormState.maruChange != NULL) {
                player->linearVelocity = 0.0f;
                MmForm_SetAction(GORON_ACT_ROLL_INIT, play, gFormState.maruChange, 0.67f, ANIMMODE_ONCE);
                MmForm_PlaySfx(player, MM_NA_SE_PL_GORON_TO_BALL, NA_SE_PL_BODY_HIT);
                return;
            }
            if (gFormState.currentForm == MM_PLAYER_FORM_ZORA) {
                // On underwater floor (dive mode): roll instead of jump kick
                if (gFormState.zoraBoots == 1 && gFormState.swimState > 0 && gFormState.rollAnim != NULL) {
                    gFormState.rollSpeed = 1.0f;
                    player->yaw = player->actor.shape.rot.y;
                    player->actor.world.rot.y = player->actor.shape.rot.y;
                    MmForm_SetAction(MMFORM_ACT_ROLL, play, gFormState.rollAnim, 1.25f, ANIMMODE_ONCE);
                    MmForm_PlaySfx(player, MM_NA_SE_PL_LAND, NA_SE_PL_BODY_HIT);
                    return;
                }
                // Normal (surface/air): jump kick
                // From MM func_808395F0: base (5.0f, 5.0f) → Zora: 5.5f, 4.5f
                if (gFormState.jumpKick != NULL) {
                    player->linearVelocity = 5.5f;
                    player->actor.velocity.y = 4.5f;
                    player->actor.bgCheckFlags &= ~1;
                    player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_JUMP_KICK);
                    player->yaw = player->actor.shape.rot.y;
                    player->actor.world.rot.y = player->yaw;
                    gFormState.jumpKickActive = 0;
                    gFormState.wasOnGround = 0;
                    MmForm_SetAction(MMFORM_ACT_JUMP_KICK, play, gFormState.jumpKick, 1.0f, ANIMMODE_ONCE);
                    MmForm_PlayAttackVoice(player);
                    MmForm_PlaySfx(player, MM_NA_SE_PL_JUMP, NA_SE_PL_JUMP);
                    return;
                }
            }
        }

        if (direction == 2 && gFormState.backflip != NULL) {
            player->actor.velocity.y = MMFORM_BACKFLIP_VEL_Y;
            player->linearVelocity = MMFORM_BACKFLIP_SPEED;
            player->yaw = player->actor.shape.rot.y + (s16)(direction << 0xE);
            player->actor.world.rot.y = player->yaw;
            player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
            player->actor.bgCheckFlags &= ~1;
            gFormState.wasOnGround = 0;
            MmForm_SetAction(MMFORM_ACT_BACKFLIP, play, gFormState.backflip, PLAYER_ANIM_ADJUSTED_SPEED, ANIMMODE_ONCE);
            MmForm_PlaySfx(player, MM_NA_SE_PL_JUMP, NA_SE_PL_ROLL);
            return;
        } else if (direction == 1 || direction == 3) {
            LinkAnimationHeader* hopAnim = (direction == 1) ? gFormState.sidehopL : gFormState.sidehopR;
            if (hopAnim != NULL) {
                player->actor.velocity.y = MMFORM_SIDEHOP_VEL_Y;
                player->yaw = player->actor.shape.rot.y + (s16)(direction << 0xE);
                player->actor.world.rot.y = player->yaw;
                player->linearVelocity = MMFORM_SIDEHOP_SPEED;
                player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
                player->actor.bgCheckFlags &= ~1;
                gFormState.wasOnGround = 0;
                gFormState.sidehopDir = (direction == 1) ? -1 : 1;
                MmForm_SetAction(MMFORM_ACT_SIDEHOP, play, hopAnim, PLAYER_ANIM_ADJUSTED_SPEED, ANIMMODE_ONCE);
                MmForm_PlaySfx(player, MM_NA_SE_PL_JUMP, NA_SE_PL_SKIP);
                return;
            }
        }
    }

    // Determine strafe direction from stick
    f32 stickMag = MmForm_GetStickMagnitude(play);
    if (stickMag < 10.0f) {
        // Stopped → Z-target idle
        MmForm_SetAction(MMFORM_ACT_ZTARGET_IDLE, play,
                         gFormState.ztargetIdleR ? gFormState.ztargetIdleR : gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        player->linearVelocity = 0.0f;
        return;
    }

    s16 relAngle = MmForm_GetStickRelAngle(player, play);
    s16 absAngle = ABS(relAngle);

    // Select strafe animation based on direction
    // From 2Ship Player_UpdateControlStick (z_player.c line 2036-2050):
    //   direction = ((u16)(BINANG_SUB(worldYaw, rot.y) + 0x2000)) >> 14;
    //   0=FORWARD, 1=LEFT, 2=BACKWARD, 3=RIGHT
    // Note: direction 1 (LEFT) = relAngle > 0, direction 3 (RIGHT) = relAngle < 0
    // The animation names (Lside/Rside) match 2Ship's direction enum, NOT screen direction.
    LinkAnimationHeader* strafeAnim = NULL;
    s32 direction = MmForm_GetStickDirection(player);

    if (direction == 2) {
        // BACKWARD
        strafeAnim = gFormState.ztargetBackWalk;
        player->yaw = player->actor.shape.rot.y + 0x8000;
    } else if (direction == 1) {
        // LEFT (relAngle positive = stick pushed right on screen)
        strafeAnim = gFormState.ztargetSideWalkL;
        player->yaw = player->actor.shape.rot.y + 0x4000;
    } else if (direction == 3) {
        // RIGHT (relAngle negative = stick pushed left on screen)
        strafeAnim = gFormState.ztargetSideWalkR;
        player->yaw = player->actor.shape.rot.y + (s16)0xC000;
    } else {
        // FORWARD
        strafeAnim = gFormState.walkAnim;
        player->yaw = player->actor.shape.rot.y;
    }

    // Sync movement direction (Actor_MoveXZGravity uses world.rot.y, NOT player->yaw)
    player->actor.world.rot.y = player->yaw;

    // Apply movement speed from stick magnitude
    // From 2Ship Player_Action_14: uses SPEED_MODE_LINEAR which applies uniform 0.8f multiplier
    // then * 0.14f to all directions equally. NO direction-based speed scaling in real MM.
    // stickMag range [0..60], target speed range [0..~6.7] matching MM's LINEAR mode output.
    f32 targetSpeed = stickMag * 0.112f; // 0.8 * 0.14 = 0.112 (MM LINEAR mode)
    // Fierce Deity 1.5x speed boost (from 2Ship Player_CalcSpeedAndYawFromControlStick line 5236)
    if (gFormState.currentForm == MM_PLAYER_FORM_FIERCE_DEITY) {
        targetSpeed *= 1.5f;
    }
    Math_StepToF(&player->linearVelocity, targetSpeed, 1.5f);

    // Update animation
    if (strafeAnim != NULL && strafeAnim != gFormState.formSkelAnime.animation) {
        LinkAnimation_Change(play, &gFormState.formSkelAnime, strafeAnim, player->linearVelocity * 0.3f + 1.0f, 0.0f,
                             Animation_GetLastFrame(strafeAnim), ANIMMODE_LOOP, -8.0f);
    } else if (strafeAnim != NULL) {
        // Same animation - just update speed
        gFormState.formSkelAnime.playSpeed = player->linearVelocity * 0.3f + 1.0f;
    }
}

// ---------------------------------------------------------------------------
// Action: LEDGE_HANG (hanging from ledge)
// From 2Ship Player_Action_78 (line 18505):
//   Plays link_normal_jump_climb_hold_free / wait_free
//   Forward input → climb up
//   Back input or B → drop
// ---------------------------------------------------------------------------
static void MmForm_Action_LedgeHang(Player* player, PlayState* play) {
    Input* input = &play->state.input[0];

    // Hold position (zero movement while hanging)
    player->linearVelocity = 0.0f;
    player->actor.velocity.y = 0.0f;
    player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_LEDGE);

    // Forward input → climb up
    // From 2Ship: stick_y > 50 → start climb
    if (input->cur.stick_y > 50) {
        if (gFormState.ledgeClimb != NULL) {
            // Restore gravity before climbing
            player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
            MmForm_SetAction(MMFORM_ACT_LEDGE_CLIMB, play, gFormState.ledgeClimb, 1.0f, ANIMMODE_ONCE);
            return;
        }
    }

    // Back input or B → drop
    if (input->cur.stick_y < -50 || CHECK_BTN_ALL(input->press.button, BTN_B)) {
        player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
        LinkAnimationHeader* fallAnim = gFormState.fallAnim ? gFormState.fallAnim : gFormState.idleAnim;
        MmForm_SetAction(MMFORM_ACT_FALL, play, fallAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }
}

// ---------------------------------------------------------------------------
// Action: LEDGE_CLIMB (climbing up from ledge)
// From 2Ship Player_Action_79 (line 18562):
//   Plays climb animation. When finished, place player on top + idle.
//   Position correction: move player up by yDistToLedge
// ---------------------------------------------------------------------------
static void MmForm_Action_LedgeClimb(Player* player, PlayState* play) {
    SkelAnime* skelAnime = &gFormState.formSkelAnime;
    if (skelAnime->animation == NULL) {
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }
    f32 curFrame = skelAnime->curFrame;
    f32 endFrame = Animation_GetLastFrame(skelAnime->animation);

    // Keep zero horizontal movement during climb
    player->linearVelocity = 0.0f;

    // Animation finished → player is on top of ledge
    if (curFrame >= endFrame) {
        // Move player up to ledge top
        // From 2Ship: uses yDistToLedge for position correction
        // In OOT, the climb animation moves the player model up visually.
        // The final position should be set by bgCheck after gravity re-enables.
        player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
    }
}

// =============================================================================
// Goron Ball Roll System (from 2Ship Player_Action_96, z_player.c line 19886)
//
// Complete port of Goron ball rolling physics including:
//   - Directional control with 2.6x speed multiplier
//   - Wall bounce (angle reflection)
//   - Spike mode (charge by holding A + magic)
//   - Ground pound (B while rolling → jump → slam)
//   - Slope physics (speed from floorPitch)
//   - Ball rotation visual (shape.rot.x)
//   - Rolling SFX synced to rotation
//
// States:
//   GORON_ACT_GORON_ROLL       - Main rolling (on ground or airborne)
//   GORON_ACT_GORON_ROLL_JUMP  - Ground pound jump (ascending, av1=1)
//   GORON_ACT_GORON_ROLL_POUND - Ground pound landing (av1=2, quake+damage)
// =============================================================================

// =============================================================================
// Goron Water Void Out (Goron can't swim → curl into ball → void out)
//
// From 2Ship z_player.c line 8948-8973: Goron enters deep water → sinks → void out.
// In MM, Goron just sinks with gPlayerAnim_link_swimer_swim_down.
// Here we add a visual curl animation first (user-requested), then void out.
//
// Phases (tracked by actionTimer):
//   0 → Start: stop movement, begin curl animation (pg_maru_change)
//   1 → Curl playing: animation plays, player sinks slowly
//   2 → Ball form: curl done, show ball DL, continue sinking
//   3 → Void out triggered: Play_TriggerVoidOut called once
// =============================================================================

static void MmForm_Action_WaterVoidOut(Player* player, PlayState* play) {
    // actionTimer is auto-incremented (line 5245) before this handler runs.
    // We use rollGroundPoundTimer as the phase tracker (set to 0 at start).
    // Phases:
    //   0 = Start: begin curl animation, set phase to 1
    //   1 = Curl animation playing
    //   2 = Ball form (curl done), sinking, count frames
    //   3 = Void out triggered, waiting for fade

    player->linearVelocity = 0.0f;

    // Phase 0: First frame - start curl animation
    if (gFormState.rollGroundPoundTimer == 0) {
        player->actor.velocity.y = -2.0f; // Start sinking
        player->actor.gravity = -0.5f;    // Slow sink

        if (gFormState.maruChange != NULL && gFormState.currentForm == MM_PLAYER_FORM_GORON) {
            LinkAnimation_Change(play, &gFormState.formSkelAnime, gFormState.maruChange, 0.67f, 0.0f,
                                 Animation_GetLastFrame(gFormState.maruChange), ANIMMODE_ONCE, 4.0f);
        }

        // SFX: splash + Goron curl sound
        Player_PlaySfx(&player->actor, NA_SE_EV_DIVE_INTO_WATER);
        if (gFormState.currentForm == MM_PLAYER_FORM_GORON) {
            MmForm_PlaySfx(player, MM_NA_SE_PL_GORON_TO_BALL, NA_SE_PL_BODY_HIT);
        }

        gFormState.rollGroundPoundTimer = 1; // → Phase 1
        return;
    }

    // Phase 1: Curl animation playing
    if (gFormState.rollGroundPoundTimer == 1) {
        LinkAnimation_Update(play, &gFormState.formSkelAnime);

        u8 curlDone = 0;
        if (gFormState.maruChange != NULL && gFormState.currentForm == MM_PLAYER_FORM_GORON) {
            f32 endFrame = Animation_GetLastFrame(gFormState.maruChange);
            if (gFormState.formSkelAnime.curFrame >= endFrame - 0.5f) {
                curlDone = 1;
            }
        } else {
            // No curl anim (Deku form) - skip after short delay
            curlDone = (gFormState.actionTimer > 5);
        }

        if (curlDone) {
            gFormState.rollGroundPoundTimer = 2; // → Phase 2: ball sinking
            player->actor.gravity = -2.0f;       // Heavier sink as ball
        }
        return;
    }

    // Phase 2: Ball form, sinking. Wait 15 frames then void out
    if (gFormState.rollGroundPoundTimer >= 2 && gFormState.rollGroundPoundTimer < 100) {
        gFormState.rollSpinRate = 0; // No spin, just sinking
        player->actor.shape.rot.x = 0;
        gFormState.rollGroundPoundTimer++;

        if (gFormState.rollGroundPoundTimer >= 17) { // ~15 frames in ball
            gFormState.rollGroundPoundTimer = 100;   // → Phase 3
            Player_PlaySfx(&player->actor, NA_SE_OC_ABYSS);
            Play_TriggerVoidOut(play);
        }
        return;
    }

    // Phase 3: Void out triggered, waiting for scene transition fade
}

// ---------------------------------------------------------------------------
// MMFORM_ACT_HAZARD_VOID - Form-specific hazard void out
//
// Handles: freeze (Zora), lava (Deku/Zora), fire hit (Deku/Zora)
//
// From 2Ship Player_Action_82 (z_player.c line 18265): Zora freeze → void
// From 2Ship func_80834600 (z_player.c line 6162): Deku/Zora on lava → burn → death
// From 2Ship func_80834534 (z_player.c line 6014): fire hit sets body burning
//
// hazardVoidType sub-types:
//   0 = freeze (Zora): freeze 9 frames → ice break VFX → void out
//   1 = lava (Deku/Zora): set body on fire → burn 20 frames → void out
//   2 = fire hit (Deku/Zora): body already on fire from knockback → burn → void out
// ---------------------------------------------------------------------------
static void MmForm_Action_HazardVoidOut(Player* player, PlayState* play) {
    gFormState.hazardVoidTimer++;
    player->linearVelocity = 0.0f;

    switch (gFormState.hazardVoidType) {
        case 0: // FREEZE (Zora)
            // From 2Ship Player_Action_82 line 18277: Zora → void after ~9 frames
            player->actor.velocity.y = 0.0f;
            if (gFormState.hazardVoidTimer <= 9) {
                // Still frozen - keep ice visual (damageFlickerAnimCounter frozen at 0)
                return;
            }
            if (gFormState.hazardVoidTimer == 10) {
                // Ice break VFX
                EffectSsIcePiece_SpawnBurst(play, &player->actor.world.pos, player->actor.scale.x);
                Player_PlaySfx(&player->actor, NA_SE_PL_ICE_BROKEN);
                return;
            }
            // Phase 11+: Void out
            if (gFormState.hazardVoidTimer == 11) {
                Player_PlaySfx(&player->actor, NA_SE_OC_ABYSS);
                Play_TriggerVoidOut(play);
                gFormState.hazardVoidType = 255; // Mark as triggered
            }
            break;

        case 1: // LAVA (Deku/Zora)
            // Phase 1: Set body on fire (first frame only)
            if (gFormState.hazardVoidTimer == 1) {
                s32 i;
                for (i = 0; i < PLAYER_BODYPART_MAX; i++) {
                    player->bodyFlameTimers[i] = Rand_S16Offset(0, 200);
                }
                player->bodyIsBurning = true;
                // Damage voice
                Player_PlayVoiceSfx(player, NA_SE_VO_LI_FALL_L);
#if !MM_SOUNDS_DISABLED
                if (MmSfx_IsAvailable()) {
                    u16 voiceSfx = 0;
                    if (gFormState.currentForm == MM_PLAYER_FORM_ZORA)
                        voiceSfx = MM_NA_SE_VO_ZORA_DAMAGE_S;
                    else if (gFormState.currentForm == MM_PLAYER_FORM_DEKU)
                        voiceSfx = MM_NA_SE_VO_DEKU_DAMAGE_S;
                    if (voiceSfx != 0)
                        MmSfx_PlayAtPos(voiceSfx, &player->actor.projectedPos);
                }
#endif
                // Damage animation (front hit)
                LinkAnimationHeader* anim = gFormState.dmgAnims[4]; // front_hit
                if (anim == NULL)
                    anim = gFormState.idleAnim;
                LinkAnimation_Change(play, &gFormState.formSkelAnime, anim, 1.0f, 0.0f, Animation_GetLastFrame(anim),
                                     ANIMMODE_ONCE, 0.0f);
            }
            // Phase 1-20: Burning animation + continuous damage
            if (gFormState.hazardVoidTimer <= 20) {
                LinkAnimation_Update(play, &gFormState.formSkelAnime);
                // -1 HP per 4 frames (from 2Ship func_80834534 burn rate)
                if ((gFormState.hazardVoidTimer % 4) == 0) {
                    Health_ChangeBy(play, -1);
                }
                return;
            }
            // Phase 21+: Void out
            if (gFormState.hazardVoidTimer == 21) {
                Player_PlaySfx(&player->actor, NA_SE_OC_ABYSS);
                Play_TriggerVoidOut(play);
                gFormState.hazardVoidType = 255;
            }
            break;

        case 2: // FIRE HIT (Deku/Zora) - body already burning from knockback
            // Phase 1: Start burn animation
            if (gFormState.hazardVoidTimer == 1) {
                // Ensure body is still on fire (may have been partially extinguished)
                s32 i;
                for (i = 0; i < PLAYER_BODYPART_MAX; i++) {
                    if (player->bodyFlameTimers[i] < 100)
                        player->bodyFlameTimers[i] = Rand_S16Offset(80, 120);
                }
                player->bodyIsBurning = true;
                // Damage voice
                Player_PlayVoiceSfx(player, NA_SE_VO_LI_FALL_L);
                // Damage animation
                LinkAnimationHeader* anim = gFormState.dmgAnims[4]; // front_hit
                if (anim == NULL)
                    anim = gFormState.idleAnim;
                LinkAnimation_Change(play, &gFormState.formSkelAnime, anim, 1.0f, 0.0f, Animation_GetLastFrame(anim),
                                     ANIMMODE_ONCE, 0.0f);
            }
            // Phase 1-25: Burning + continuous damage
            if (gFormState.hazardVoidTimer <= 25) {
                LinkAnimation_Update(play, &gFormState.formSkelAnime);
                if ((gFormState.hazardVoidTimer % 4) == 0) {
                    Health_ChangeBy(play, -1);
                }
                return;
            }
            // Phase 26+: Void out
            if (gFormState.hazardVoidTimer == 26) {
                Player_PlaySfx(&player->actor, NA_SE_OC_ABYSS);
                Play_TriggerVoidOut(play);
                gFormState.hazardVoidType = 255;
            }
            break;

        default:
            // Already triggered (255), waiting for scene transition fade
            break;
    }
}

// Forward declaration
static void MmForm_Action_GoronRoll(Player* player, PlayState* play);

// Helper: Set player cylinder for roll attack
// From 2Ship Player_SetCylinderForAttack (z_player.c line 2901-2927)
static void MmForm_SetRollAttack(Player* player, u32 dmgFlags, s32 damage, s16 radius) {
    player->cylinder.base.atFlags = AT_ON | AT_TYPE_PLAYER;

    // OC flags: disable for large attacks (ground pound r=60), enable for normal (r=25)
    // From 2Ship: if (radius > 30) ocFlags1 = OC1_NONE else OC1_ON | OC1_TYPE_ALL
    if (radius > 30) {
        player->cylinder.base.ocFlags1 = OC1_NONE;
    } else {
        player->cylinder.base.ocFlags1 = OC1_ON | OC1_TYPE_ALL;
    }

    // Touch element setup (from 2Ship line 2913-2914)
    player->cylinder.info.elemType = ELEMTYPE_UNK2;
    player->cylinder.info.toucherFlags = TOUCH_ON | TOUCH_NEAREST | TOUCH_SFX_NORMAL;
    player->cylinder.info.toucher.dmgFlags = dmgFlags;
    player->cylinder.info.toucher.damage = damage;
    player->cylinder.dim.radius = radius;

    // Ground pound (r=60, dmg=4): disable AC so Goron can't take damage during slam
    // From 2Ship: if (dmgFlags & DMG_GORON_POUND) acFlags = AC_NONE
    if (radius > 30) {
        player->cylinder.base.acFlags = AC_NONE;
    }
}

// Helper: Clear roll attack flags, restore normal collider state
static void MmForm_ClearRollAttack(Player* player) {
    player->cylinder.base.atFlags = AT_NONE;
    player->cylinder.base.ocFlags1 = OC1_ON | OC1_TYPE_ALL;
    player->cylinder.base.acFlags = AC_ON | AC_TYPE_ENEMY;
    player->cylinder.info.toucherFlags = TOUCH_NONE;
    // Restore normal cylinder radius from form properties
    const MmFormProperties* props = &sFormProps[gFormState.currentForm];
    player->cylinder.dim.radius = (s16)props->cylinderRadius;
}

/**
 * Goron Ball Roll action handler.
 * Ported from 2Ship Player_Action_96 (z_player.c line 19886).
 *
 * Handles three sub-states:
 *   GORON_ACT_GORON_ROLL       - Main rolling physics
 *   GORON_ACT_GORON_ROLL_JUMP  - Ground pound jump phase
 *   GORON_ACT_GORON_ROLL_POUND - Ground pound landing + quake
 */
static void MmForm_Action_GoronRoll(Player* player, PlayState* play) {
    Input* input = &play->state.input[0];
    u8 onGround = MMFORM_ON_GROUND(player);

    // =====================================================================
    // EXIT CHECK: func_80857950 (2Ship line 19829-19841)
    // Called FIRST, before any physics. A-release → uncurl.
    // =====================================================================
    if (gFormState.goronAction == GORON_ACT_GORON_ROLL) {
        // Exit check: func_80857950 (2Ship line 19829-19841)
        // No spikes AND A button released → uncurl animation
        if ((gFormState.rollSpikeActive == 0) && !CHECK_BTN_ALL(input->cur.button, BTN_A)) {
            MmForm_ClearRollAttack(player);
            player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
            player->actor.shape.rot.x = 0;
            player->actor.shape.rot.z = 0;
            // Restore shadow scale to standing size (DrawFeet restored per-frame by UpdateActive)
            player->actor.shape.shadowScale = gFormState.savedShadowScale;
            // Clear roll state flags (from 2Ship: stateFlags3 cleared on uncurl)
            player->stateFlags2 &= ~(PLAYER_STATE2_DISABLE_ROTATION_Z_TARGET | PLAYER_STATE2_DISABLE_ROTATION_ALWAYS);
            player->actor.bgCheckFlags &= ~0x800;
            // Restore OOT input (was blocked during roll via sActionHandlerList12 equivalent)
            player->stateFlags1 &= ~(PLAYER_STATE1_INPUT_DISABLED | PLAYER_STATE1_JUMPING);
            // Position restore (from 2Ship func_80857950 line 19833)
            // Prevents visual pop when uncurling from ball DL back to skeleton
            Math_Vec3f_Copy(&player->actor.world.pos, &player->actor.prevPos);
            // From 2Ship func_80857950: pg_maru_change at -ADJUSTED_SPEED, start=7, end=0
            MmForm_PlaySfx(player, MM_NA_SE_PL_GORON_BALL_TO_GORON, NA_SE_PL_BODY_HIT);
            if (gFormState.maruChange != NULL) {
                // Play curl anim reversed: start at last frame, play backwards
                gFormState.goronAction = GORON_ACT_ROLL_UNCURL;
                LinkAnimation_Change(play, &gFormState.formSkelAnime, gFormState.maruChange, -0.67f,
                                     Animation_GetLastFrame(gFormState.maruChange), 0.0f, ANIMMODE_ONCE, 0.0f);
            } else {
                MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
            }
            return;
        }
    }

    // Tick bounce/wall timers
    if (gFormState.rollWallBounceTimer > 0)
        gFormState.rollWallBounceTimer--;
    if (gFormState.rollNoInputTimer > 0)
        gFormState.rollNoInputTimer--;

    // =====================================================================
    // Get stick input (speed target + yaw target)
    // From 2Ship: Player_GetMovementSpeedAndYaw → speedTarget *= 2.6f
    // =====================================================================
    f32 speedTarget = 0.0f;
    s16 yawTarget = player->yaw;

    if (gFormState.rollNoInputTimer == 0) {
        f32 stickMag = MmForm_GetStickMagnitude(play);
        if (stickMag > 10.0f) {
            // Calculate world-space stick angle
            // MUST use rel.stick and Camera_GetInputDirYaw to match OOT's input mapping
            Input* rollInput = &play->state.input[0];
            s16 stickAngle = (s16)Math_Atan2S(rollInput->rel.stick_y, -rollInput->rel.stick_x);
            s16 camYaw = Camera_GetInputDirYaw(GET_ACTIVE_CAM(play));
            yawTarget = stickAngle + camYaw;
            // Speed = stick magnitude normalized to max 8.0, then * 2.6
            speedTarget = (stickMag / 60.0f) * 8.0f * 2.6f;
        }
    }

    // =====================================================================
    // GROUND POUND sub-states (from 2Ship Player_Action_96 line 20120-20170)
    // =====================================================================
    if (gFormState.goronAction == GORON_ACT_GORON_ROLL_JUMP) {
        // Ground pound JUMP phase (av1 == 1)
        // From 2Ship: velocity.y peak → slow fall, then fast slam
        if (player->actor.velocity.y > 0.0f) {
            // Ascending: reduce gravity for floaty apex
            if ((player->actor.velocity.y + player->actor.gravity) < 0.0f) {
                player->actor.velocity.y = -player->actor.gravity;
            }
        } else {
            // Descending: set ground pound timer
            gFormState.rollGroundPoundTimer = 10; // unk_B8A = 0xA
            if (player->actor.velocity.y > -1.0f) {
                // Slow descent near apex
                player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_ROLL_APEX);
            } else {
                // FAST SLAM (from 2Ship: gravity = -10.0f)
                player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_ROLL_SLAM);
            }
        }

        // Landing detection for ground pound
        if (onGround && player->actor.velocity.y <= 0.0f) {
            // LAND → Ground pound impact!
            gFormState.goronAction = GORON_ACT_GORON_ROLL_POUND;
            gFormState.actionTimer = 0;
            player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL); // Restore normal gravity

            // Camera: clear JUMPING flag (landing → back to normal camera)
            player->stateFlags1 &= ~PLAYER_STATE1_JUMPING;

            // Player impact signal (from 2Ship: Actor_SetPlayerImpact)
            // actorCtx.unk_02 tells actors a player ground pound occurred
            play->actorCtx.unk_02 = 4;

            // Quake effect (simulates Actor_SetPlayerImpact PLAYER_IMPACT_GORON_GROUND_POUND)
            // MM uses: Actor_SetPlayerImpact(play, 0, 2, 100.0f, &pos) which triggers
            // Quake_Request internally. These values tuned to match MM's ground pound feel.
            s32 quakeIdx = Quake_Add(GET_ACTIVE_CAM(play), 3);
            if (quakeIdx != 0) {
                Quake_SetSpeed(quakeIdx, 27767);
                Quake_SetQuakeValues(quakeIdx, 7, 0, 0, 0);
                Quake_SetCountdown(quakeIdx, 20);
            }

            // Ground pound SFX (from 2Ship: Player_RequestQuakeAndRumble + NA_SE_PL_GORON_PUNCH)
            Player_PlaySfx(&player->actor, NA_SE_PL_BODY_HIT);
            MmForm_PlaySfx(player, MM_NA_SE_PL_GORON_SQUAT, NA_SE_PL_BODY_HIT);
            func_800AA000(0.0f, 255, 20, 150); // Rumble (MM values)

            // White shockwave effect (from 2Ship func_80857AEC line 19871)
            {
                Vec3f shockPos = player->actor.world.pos;
                Vec3f zeroVec = { 0.0f, 0.0f, 0.0f };
                EffectSsBlast_SpawnWhiteShockwave(play, &shockPos, &zeroVec, &zeroVec);
            }

            // Ground crack visual (from 2Ship: Actor_Spawn ACTOR_EN_TEST at impact point)
            // EN_TEST draws a dark circle with cracks on the floor that fades over ~30 frames.
            // OOT lacks KFSkelAnimeFlex, so we draw gCircleShadowDL as a dark impact decal.
            gFormState.groundPoundImpactPos = player->actor.world.pos;
            gFormState.groundPoundFloorPoly = player->actor.floorPoly;
            gFormState.groundPoundCrackTimer = 30;

            // Dust ring at impact (from 2Ship func_8083FBC4 - ground debris on impact)
            Actor_SpawnFloorDustRing(play, &player->actor, &player->actor.world.pos,
                                     player->actor.shape.shadowScale * 1.5f, 4, 8.0f, 500, 10, 1);

            // Reset ball speed to 0 on impact (from 2Ship: unk_B08 = 0.0f)
            gFormState.rollBallSpeed = 0.0f;
            gFormState.rollSpinRate = 0;

            // Attack: DMG_HAMMER_SWING, damage=4, radius=60
            // From 2Ship: Player_SetCylinderForAttack(this, DMG_GORON_POUND, 4, 60)
            MmForm_SetRollAttack(player, 0x02000000, 4, 60);
            CollisionCheck_SetAT(play, &play->colChkCtx, &player->cylinder.base);
        }

        // Ball spin continues during jump
        player->actor.shape.rot.x += gFormState.rollSpinRate;
        Math_ScaledStepToS(&player->actor.shape.rot.y, gFormState.rollHomeYaw, 0x7D0);
        return;
    }

    if (gFormState.goronAction == GORON_ACT_GORON_ROLL_POUND) {
        // Ground pound LANDING phase (av1 == 2)
        // Pause briefly after impact, then resume rolling
        // From 2Ship: unk_B8A counts down, then av1 → 4
        if (gFormState.rollGroundPoundTimer > 0) {
            gFormState.rollGroundPoundTimer--;
            player->linearVelocity = 0.0f;

            // Keep attack active on first frame only
            if (gFormState.actionTimer == 0) {
                MmForm_SetRollAttack(player, 0x02000000, 4, 60);
                CollisionCheck_SetAT(play, &play->colChkCtx, &player->cylinder.base);
                gFormState.actionTimer = 1;
            } else {
                MmForm_ClearRollAttack(player);
            }
        } else {
            // Resume rolling
            MmForm_ClearRollAttack(player);
            gFormState.rollChargeLevel = 4;
            gFormState.goronAction = GORON_ACT_GORON_ROLL;
        }

        player->actor.shape.rot.x += gFormState.rollSpinRate;
        return;
    }

    // =====================================================================
    // MAIN ROLLING STATE (GORON_ACT_GORON_ROLL)
    // From 2Ship Player_Action_96 line 19886
    // =====================================================================

    // --- Door interaction during roll (from 2Ship func_80840A30, line 19905-19910) ---
    // When rolling into a door at speed, reduce speed by 10x and disable spike mode
    if ((player->actor.bgCheckFlags & 8 /* BGCHECKFLAG_WALL */) && gFormState.rollBallSpeed >= 12.0f &&
        player->doorType != PLAYER_DOORTYPE_NONE) {
        player->linearVelocity *= 0.1f;
        gFormState.rollBallSpeed *= 0.1f;
        if (gFormState.rollSpikeActive > 0) {
            gFormState.rollSpikeActive = 0;
            gFormState.rollChargeLevel = 3;
            Magic_Reset(play);
        }
        Player_PlaySfx(&player->actor, NA_SE_PL_BODY_HIT);
    }

    // --- Wall bounce detection (from 2Ship line 19917-19928) ---
    if ((player->actor.bgCheckFlags & 8 /* BGCHECKFLAG_WALL */) && (gFormState.rollBallSpeed >= 12.0f)) {
        s16 wallAngle = player->actor.wallYaw + 0x8000;
        s16 relWallAngle = player->yaw - wallAngle;
        s16 bounceAngle = ((relWallAngle >= 0) ? 1 : -1) * ((ABS(relWallAngle) + 0x100) & ~0x1FF);

        player->yaw += (s16)(0x8000 - (bounceAngle * 2));
        gFormState.rollHomeYaw = player->yaw;
        player->actor.shape.rot.y = player->yaw;
        player->actor.world.rot.y = player->yaw;

        gFormState.rollBounce += gFormState.rollBallSpeed * 0.05f;
        gFormState.rollWallBounceTimer = 4;

        MmForm_PlaySfx(player, MM_NA_SE_IT_GORON_ROLLING_REFLECTION, NA_SE_PL_BODY_HIT);
    }

    // --- Spike mode management (from 2Ship line 19945-19984) ---
    if (gFormState.rollSpikeActive > 0) {
        speedTarget = 18.0f;
        Math_StepToS(&gFormState.rollChargeLevel, 4, 1);

        // Continuous magic drain while spikes active
        // From 2Ship z_parameter.c MAGIC_STATE_CONSUME_GORON_ZORA:
        // magicConsumptionTimer counts down each frame, drains 1 magic when it hits 0,
        // then resets to 10. Rate: 1 magic per 10 frames.
        gFormState.magicDrainTimer--;
        if (gFormState.magicDrainTimer <= 0) {
            if (gSaveContext.magic > 0) {
                gSaveContext.magic--;
            }
            gFormState.magicDrainTimer = 10;
        }

        // Spike mode deactivation conditions
        u8 deactivateSpike = 0;
        if (!CHECK_BTN_ALL(input->cur.button, BTN_A))
            deactivateSpike = 1;
        if (gSaveContext.magic <= 0)
            deactivateSpike = 1;
        if (gFormState.rollChargeLevel == 4 && gFormState.rollBallSpeed < 12.0f)
            deactivateSpike = 1;

        // Steep slope check: spike disabled on slopes > 0x3A98
        // From 2Ship line 20180: ABS(sp8E) + ABS(floorPitch) > 0x3A98
        s16 yawDiff = player->yaw - gFormState.rollHomeYaw;
        if ((ABS(yawDiff) + ABS(player->floorPitch)) > 0x3A98)
            deactivateSpike = 1;

        if (deactivateSpike) {
            if (Math_StepToS(&gFormState.rollSpikeActive, 0, 1)) {
                Magic_Reset(play);
                MmForm_PlaySfx(player, MM_NA_SE_PL_GORON_BALL_CHARGE_FAILED, NA_SE_PL_BODY_HIT);
            }
            gFormState.rollChargeLevel = 4;
        } else if (gFormState.rollSpikeActive < 7) {
            gFormState.rollSpikeActive++;
        }
    }

    if (onGround) {
        // === ON GROUND ROLLING ===

        // --- Ground pound: B press while rolling (NOT spike mode) ---
        // From 2Ship line 19524-19526: func_80857640(this, 14.0f, 0x1F40)
        // 0x1F40 is minimum spin rate (av2.actionVar2), NOT a yaw offset!
        // func_80857640: velocity.y=14, stop horizontal, min spin=0x1F40, av1=1, unk_B48=1.0
        if (gFormState.rollSpikeActive == 0 && CHECK_BTN_ALL(input->press.button, BTN_B)) {
            player->actor.velocity.y = 14.0f;
            player->linearVelocity = 0.0f; // Player_StopHorizontalMovement
            if (gFormState.rollSpinRate < 0x1F40) {
                gFormState.rollSpinRate = 0x1F40; // Minimum spin for ground pound
            }
            gFormState.rollChargeLevel = 1; // av1.actionVar1 = 1
            gFormState.rollTilt = 1.0f;     // unk_B48 = 1.0f
            gFormState.goronAction = GORON_ACT_GORON_ROLL_JUMP;
            gFormState.actionTimer = 0;
            player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
            MmForm_PlaySfx(player, MM_NA_SE_PL_GORON_BALLJUMP, NA_SE_PL_JUMP);
            // Camera: set JUMPING flag so OOT's camera selects CAM_MODE_JUMP
            // (elevated camera during jump, similar to MM's CAM_MODE_GORONJUMP)
            player->stateFlags1 |= PLAYER_STATE1_JUMPING;
            player->actor.shape.rot.x += gFormState.rollSpinRate;
            return;
        }

        // --- Spike charge system (from 2Ship line 19547-19558 + 19611-19619) ---
        // Two-phase: 1) Charge increments when spin >= 0x36B0 + magic available
        //            2) Spikes activate when charge >= 0x36 (54 frames)
        if (gFormState.rollSpikeActive == 0) {
            gFormState.rollBounce = 0.0f;

            // Phase 2 FIRST (from 2Ship line 19547-19558): check activation
            if (gFormState.rollChargeLevel >= 0x36) {
                // Initial 2 magic cost (from 2Ship: Magic_Consume(play, 2, MAGIC_CONSUME_GORON_ZORA))
                if (gSaveContext.magic >= 2) {
                    gSaveContext.magic -= 2;
                }
                gFormState.magicDrainTimer = 10; // Start continuous drain timer
                gFormState.rollBallSpeed = 18.0f;
                gFormState.rollSpikeActive = 1;
                MmForm_PlaySfx(player, MM_NA_SE_PL_GORON_BALL_CHARGE_DASH, NA_SE_PL_BODY_HIT);
            }

            // Phase 1: Charge increment (from 2Ship line 19611-19619)
            // MM does NOT check BTN_A here - charge is automatic when spinning fast
            if (gFormState.rollSpikeActive == 0) {
                if (gSaveContext.magicState == MAGIC_STATE_IDLE && gSaveContext.magic >= 2 &&
                    gFormState.rollSpinRate >= 0x36B0) { // No ABS - MM checks positive only
                    if (gFormState.rollChargeLevel < 0x100) {
                        gFormState.rollChargeLevel++;
                    }
#if !MM_SOUNDS_DISABLED
                    MmSfx_PlayAtPos(MM_NA_SE_PL_GORON_BALL_CHARGE, &player->actor.projectedPos);
#endif
                } else {
                    gFormState.rollChargeLevel = 4; // Reset (from 2Ship: av1 = 4)
                }
            }
        } else {
            gFormState.rollBounce = CLAMP(gFormState.rollBounce, 0.0f, 0.9f);

            // Spike deactivation on steep slope (from 2Ship line 19656-19664)
            // (ABS(yawDiff) + ABS(floorPitch)) > 0x3A98 → too steep, cancel spikes
            {
                s16 yawDiff = player->yaw - gFormState.rollHomeYaw;
                if ((ABS(yawDiff) + ABS(player->floorPitch)) > 0x3A98) {
                    gFormState.rollSpikeActive = 0;
                    gFormState.rollChargeLevel = 4;
                    gFormState.rollSpinRate = 0;
                    Magic_Reset(play);
                }
            }
        }

        // --- Core rolling physics (from 2Ship line 20093-20165) ---
        {
            s16 sp90 = player->yaw;
            s16 sp8E = player->yaw - gFormState.rollHomeYaw;
            f32 sp88 = Math_CosS(sp8E);

            // Speed from current trajectory
            f32 spBC = (1.0f - gFormState.rollBounce) * gFormState.rollBallSpeed * sp88;
            if ((spBC < 0.0f) || ((speedTarget == 0.0f) && (ABS(sp8E) > 0xFA0))) {
                spBC = 0.0f;
            }

            // Decay bounce
            Math_StepToF(&gFormState.rollBounce, 0.0f, fabsf(sp88) * 20.0f);

            // Decompose into XZ components
            f32 spAC = spBC * Math_SinS(gFormState.rollHomeYaw);
            f32 spA8 = spBC * Math_CosS(gFormState.rollHomeYaw);
            f32 spB4 = gFormState.rollBallSpeed * Math_SinS(player->yaw);
            f32 spB0 = gFormState.rollBallSpeed * Math_CosS(player->yaw);

            // Lateral drift
            f32 spA4 = spB4 - spAC;
            f32 spA0 = spB0 - spA8;

            player->linearVelocity = spBC;
            player->yaw = gFormState.rollHomeYaw;
            player->actor.world.rot.y = player->yaw;

            // Apply slope gravity
            // From 2Ship: Actor_GetSlopeDirection gives downward slope normal
            f32 slopeGravX = Math_SinS(player->floorPitch) * Math_SinS(player->actor.shape.rot.y);
            f32 slopeGravZ = Math_SinS(player->floorPitch) * Math_CosS(player->actor.shape.rot.y);

            if (gFormState.rollSpikeActive == 0) {
                f32 temp_ft4 = (0.6f * slopeGravX) + spA4;
                f32 temp_ft5 = (0.6f * slopeGravZ) + spA0;
                f32 temp_len = sqrtf(SQ(temp_ft4) + SQ(temp_ft5));
                f32 origLen = sqrtf(SQ(spA4) + SQ(spA0));

                if ((temp_len < origLen) || (temp_len < 6.0f)) {
                    spA4 = temp_ft4;
                    spA0 = temp_ft5;
                }
            }

            // Decelerate lateral drift
            f32 driftLen = sqrtf(SQ(spA4) + SQ(spA0));
            if (driftLen != 0.0f) {
                f32 reduced = driftLen - 0.3f;
                if (reduced < 0.0f)
                    reduced = 0.0f;
                f32 scale = reduced / driftLen;
                spA4 *= scale;
                spA0 *= scale;
            }

            // Apply friction/acceleration toward target speed
            // From 2Ship line 20056-20089:
            //   accel = av2.actionVar2 * 0.0003f (normal surface)
            //   accel = 0.08f (snow/ice/sand AND turnRate >= 0x7D0)
            //   decel = (Math_SinS(floorPitch) * 8.0f) + 0.6f
            f32 accel;
            s16 absSpinRate = (gFormState.rollSpinRate >= 0) ? gFormState.rollSpinRate : -gFormState.rollSpinRate;
            // From 2Ship: slippery surfaces (ice/sand) with high spin → 0.08 accel
            // OOT has NA_SE_PL_WALK_ICE and NA_SE_PL_WALK_SAND (no SNOW)
            if (absSpinRate >= 0x7D0 && (player->floorSfxOffset == (NA_SE_PL_WALK_ICE - SFX_FLAG) ||
                                         player->floorSfxOffset == (NA_SE_PL_WALK_SAND - SFX_FLAG))) {
                accel = 0.08f; // Slippery surfaces: higher accel
            } else {
                accel = 0.0003f * absSpinRate;
            }
            // NO minimum accel guard - MM does NOT have one (was invented).
            // Low spin rate = low accel = ball must build speed gradually.
            accel = CLAMP_MIN(accel, 0.0f);
            f32 decel = (Math_SinS(player->floorPitch) * 8.0f) + 0.6f;
            if (decel < 0.0f)
                decel = 0.0f;
            Math_AsymStepToF(&player->linearVelocity, speedTarget, accel, decel);

            // Turn rate (from 2Ship line 20146)
            s16 turnRate = (s16)(fabsf(player->linearVelocity) * 20.0f) + 300;
            if (turnRate < 100)
                turnRate = 100;
            Math_ScaledStepToS(&player->yaw, yawTarget, turnRate);

            // Recompose speed from components
            spBC = player->linearVelocity;
            gFormState.rollHomeYaw = player->yaw;
            player->yaw = sp90; // Restore visual yaw

            spAC = Math_SinS(gFormState.rollHomeYaw) * spBC;
            spA8 = Math_CosS(gFormState.rollHomeYaw) * spBC;

            spB4 = spAC + spA4;
            spB0 = spA8 + spA0;

            gFormState.rollBallSpeed = sqrtf(SQ(spB4) + SQ(spB0));
            if (gFormState.rollBallSpeed > 18.0f)
                gFormState.rollBallSpeed = 18.0f;

            player->yaw = Math_Atan2S(spB0, spB4);
        }

        // Slope-adjusted speed/velocity (from 2Ship line 20222-20223)
        player->linearVelocity = gFormState.rollBallSpeed * Math_CosS(player->floorPitch);
        player->actor.velocity.y = gFormState.rollBallSpeed * Math_SinS(player->floorPitch);
        player->actor.world.rot.y = player->yaw;

        // Height probes for lateral Z tilt (from 2Ship func_808573A8)
        // Raycast ground at left/right offsets perpendicular to roll direction
        // to calculate terrain tilt for visual ball lean
        {
            CollisionPoly* leftPoly = NULL;
            CollisionPoly* rightPoly = NULL;
            s32 leftBgId, rightBgId;
            f32 perpSin = Math_SinS(player->yaw + 0x4000); // perpendicular right
            f32 perpCos = Math_CosS(player->yaw + 0x4000);
            Vec3f leftPos = { player->actor.world.pos.x - perpSin * 30.0f, player->actor.world.pos.y + 60.0f,
                              player->actor.world.pos.z - perpCos * 30.0f };
            Vec3f rightPos = { player->actor.world.pos.x + perpSin * 30.0f, player->actor.world.pos.y + 60.0f,
                               player->actor.world.pos.z + perpCos * 30.0f };
            f32 leftY = BgCheck_EntityRaycastFloor3(&play->colCtx, &leftPoly, &leftBgId, &leftPos);
            f32 rightY = BgCheck_EntityRaycastFloor3(&play->colCtx, &rightPoly, &rightBgId, &rightPos);

            if (leftY > BGCHECK_Y_MIN && rightY > BGCHECK_Y_MIN) {
                // atan2(heightDiff, horizontalDist=60) gives tilt angle
                s16 tiltTarget = Math_Atan2S(60.0f, rightY - leftY);
                Math_ScaledStepToS(&player->actor.shape.rot.z, tiltTarget, 0x190);
            } else {
                Math_ScaledStepToS(&player->actor.shape.rot.z, 0, 0x190);
            }
        }

        // Rolling SFX - dual mode (from 2Ship line 19692-19702 + 19774-19783)
        // Mode 1 (av2==0): counter += speed * 800, trigger on zero-crossing
        // Mode 2 (av2!=0): trigger when shape.rot.x crosses zero
        if (gFormState.rollSpinRate == 0) {
            // Mode 1: slow roll / no spin (from 2Ship line 19692-19702)
            s16 prevCounter = gFormState.rollSfxCounter;
            s16 increment = (s16)(gFormState.rollBallSpeed * 800.0f);
            gFormState.rollSfxCounter += increment;
            if ((player->actor.bgCheckFlags & 1) && increment != 0 &&
                ((s32)(prevCounter + increment) * (s32)prevCounter) <= 0) {
#if !MM_SOUNDS_DISABLED
                MmSfx_PlayGoronRoll(&player->actor.projectedPos, gFormState.rollBallSpeed);
#endif
            }
        } else {
            // Mode 2: spinning (from 2Ship line 19774-19783)
            // Decay unk_B86[0] toward 0 using ABS(av2) as step
            Math_ScaledStepToS(&gFormState.rollSfxCounter, 0, ABS(gFormState.rollSpinRate));
            s16 prevRotX = player->actor.shape.rot.x;
            // rot.x was already updated by rollSpinRate earlier, check zero-crossing
            if ((player->actor.bgCheckFlags & 1) &&
                (((s32)(gFormState.rollSpinRate + prevRotX) * (s32)prevRotX) <= 0)) {
#if !MM_SOUNDS_DISABLED
                MmSfx_PlayGoronRoll(&player->actor.projectedPos, gFormState.rollBallSpeed);
#endif
            }
        }

        // Dust + slip SFX (from 2Ship func_808576BC VERBATIM, line 19331-19351)
        // Skid factor = difference between actual velocity and rotational speed
        if (player->actor.bgCheckFlags & 1) { // On ground only
            s32 skidFactor = (s32)(((player->actor.velocity.z * Math_CosS(player->yaw)) +
                                    (player->actor.velocity.x * Math_SinS(player->yaw))) *
                                   800.0f);
            skidFactor -= gFormState.rollSpinRate;
            skidFactor = ABS(skidFactor);

            // Slip SFX when skid > 0x1770 (from 2Ship line 19343)
            if (skidFactor > 0x1770) {
#if !MM_SOUNDS_DISABLED
                MmSfx_PlayAtPos(MM_NA_SE_PL_GORON_SLIP, &player->actor.projectedPos);
#endif
            }

            // Dust only when skid > 0x7D0 (from 2Ship line 19340)
            if (skidFactor > 0x7D0 && (gFormState.actionTimer % 2) == 0) {
                Color_RGBA8 dustPrim = { 170, 130, 90, 255 };
                Color_RGBA8 dustEnv = { 100, 80, 60, 255 };
                Vec3f dustPos = { player->actor.world.pos.x + Rand_CenteredFloat(10.0f), player->actor.world.pos.y,
                                  player->actor.world.pos.z + Rand_CenteredFloat(10.0f) };
                Vec3f dustVel = { -Math_SinS(player->yaw) * gFormState.rollBallSpeed * 0.1f, 1.5f,
                                  -Math_CosS(player->yaw) * gFormState.rollBallSpeed * 0.1f };
                Vec3f dustAccel = { 0.0f, 0.3f, 0.0f };
                s16 dustScale = (s16)((skidFactor >> 0xA) + 1.0f);
                s16 dustLife = (s16)((skidFactor >> 7) + 160);
                if (dustScale > 200)
                    dustScale = 200;
                if (dustLife > 255)
                    dustLife = 255;
                func_8002829C(play, &dustPos, &dustVel, &dustAccel, &dustPrim, &dustEnv, dustScale, 5);
            }
        }

    } else {
        // === AIRBORNE ROLLING ===
        // From 2Ship line 20225-20260

        // Reset Z tilt toward 0 (from 2Ship line 20227)
        Math_ScaledStepToS(&player->actor.shape.rot.z, 0, 0x190);
        gFormState.rollSfxCounter = 0;

        if (gFormState.rollSpikeActive > 0) {
            // Spike mode airborne: lower gravity, slow steer
            player->actor.gravity = -1.0f;
            Math_ScaledStepToS(&gFormState.rollHomeYaw, yawTarget, 0x190);

            gFormState.rollBallSpeed = sqrtf(SQ(player->linearVelocity) + SQ(player->actor.velocity.y)) *
                                       ((player->linearVelocity >= 0.0f) ? 1.0f : -1.0f);
            if (gFormState.rollBallSpeed > 18.0f)
                gFormState.rollBallSpeed = 18.0f;
        } else {
            // Normal airborne: standard gravity
            gFormState.rollTilt += player->actor.velocity.y * 0.005f;
            gFormState.rollBallSpeed = player->linearVelocity;
        }
    }

    // --- Visual rotation (from 2Ship line 20231-20237) ---
    Math_ScaledStepToS(&player->actor.shape.rot.y, gFormState.rollHomeYaw, 0x7D0);

    // Ball spin (from 2Ship line 20239-20240: av2.actionVar2 steps toward spDC)
    s32 spinTarget = (s32)(speedTarget * 900.0f);
    // Asymmetric step (inline, since OOT doesn't have Math_AsymStepToS)
    {
        s16 diff = (s16)(spinTarget - gFormState.rollSpinRate);
        s16 step = (diff >= 0) ? ((spinTarget >= 0) ? 0x7D0 : 0x4B0) : ((spinTarget >= 0) ? 0x4B0 : 0x3E8);
        if (ABS(diff) <= step) {
            gFormState.rollSpinRate = (s16)spinTarget;
        } else {
            gFormState.rollSpinRate += (diff > 0) ? step : -step;
        }
    }
    if (gFormState.rollSpinRate != 0) {
        player->actor.shape.rot.x += gFormState.rollSpinRate;
    }

    // --- Squash/stretch visual deformation (from 2Ship func_808577E0 VERBATIM) ---
    // unk_ABC = rollSquash (deformation amount), unk_B48 = rollTilt (velocity)
    // av2.actionVar2 = rollSpinRate (ball spin speed)
    // Target squash based on spin speed, velocity oscillates toward target
    {
        f32 temp_fa1 = (f32)ABS(gFormState.rollSpinRate) * 0.00004f;

        if (gFormState.rollSquash < temp_fa1) {
            gFormState.rollTilt += 0.08f;
        } else {
            gFormState.rollTilt += -0.07f;
        }

        gFormState.rollTilt = CLAMP(gFormState.rollTilt, -0.2f, 0.14f);
        if (fabsf(gFormState.rollTilt) < 0.12f) {
            if (Math_StepUntilF(&gFormState.rollSquash, temp_fa1, gFormState.rollTilt)) {
                gFormState.rollTilt = 0.0f;
            }
        } else {
            gFormState.rollSquash += gFormState.rollTilt;
            gFormState.rollSquash = CLAMP(gFormState.rollSquash, -0.7f, 0.3f);
        }
    }

    // --- Roll attack collision (from 2Ship line 20248-20256) ---
    if (gFormState.rollSpikeActive > 0) {
        // Spike roll: damage with DMG_HAMMER_SWING equivalent, damage=1, radius=25
        MmForm_SetRollAttack(player, 0x02000000, 1, 25);
        CollisionCheck_SetAT(play, &play->colChkCtx, &player->cylinder.base);
    } else if (gFormState.rollBallSpeed > 2.0f) {
        // Normal roll: damage with basic roll damage, damage=1, radius=25
        MmForm_SetRollAttack(player, 0x00000100, 1, 25);
        CollisionCheck_SetAT(play, &play->colChkCtx, &player->cylinder.base);
    } else {
        MmForm_ClearRollAttack(player);
    }
}

// ---------------------------------------------------------------------------
// Action: DEKU SPIN ATTACK (from 2Ship Player_Action_95, z_player.c line 19276)
//
// Deku spins on the spot, hitting nearby enemies with a cylinder collider.
// Spin speed (unk_B10[0]) starts at 20000 and decays by -800/frame.
// Visual rotation accumulated via dekuSpinRotAccum (absolute positioning because
// OOT's concurrent idle action resets shape.rot.y each frame).
// Ends when timer (unk_B10[1]) reaches 0 via Math_StepToF.
// ---------------------------------------------------------------------------
static void MmForm_Action_DekuSpin(Player* player, PlayState* play) {
    // From 2Ship Player_Action_95 line 19278: stateFlags2 bits
    // MM uses PLAYER_STATE2_20 | PLAYER_STATE2_40
    player->stateFlags2 |= PLAYER_STATE2_DISABLE_ROTATION_ALWAYS;

    // Tick animation
    LinkAnimation_Update(play, &gFormState.formSkelAnime);

    // Apply attack collider (cylinder, radius 30, 1 damage)
    // From 2Ship: Player_SetCylinderForAttack(this, DMG_DEKU_SPIN, 1, 30)
    MmForm_SetRollAttack(player, DMG_SLASH_MASTER | DMG_SLASH_KOKIRI | DMG_SPIN_KOKIRI | DMG_JUMP_KOKIRI, 1, 30);
    CollisionCheck_SetAT(play, &play->colChkCtx, &player->cylinder.base);

    // Save yaw BEFORE movement update (from 2Ship line 19285: s16 prevYaw = this->yaw)
    s16 prevYaw = player->yaw;

    // Movement during spin (from 2Ship lines 19286-19295: Player_GetMovementSpeedAndYaw + speed multiplier)
    // MM allows full stick-based movement during spin, with speed scaled by spin phase.
    {
        f32 speedTarget = 0.0f;
        s16 yawTarget = player->yaw;
        Player_GetMovementSpeedAndYaw(player, &speedTarget, &yawTarget, SPEED_MODE_CURVED, play);

        // Speed multiplier from MM: ~1.7× at spin start, ~0.1× as spin ends
        // From 2Ship: speedTarget *= 1.0f - (0.9f * ((11100.0f - unk_B10[0]) / 11100.0f))
        speedTarget *= 1.0f - (0.9f * ((11100.0f - gFormState.dekuSpinSpeed) / 11100.0f));

        // Anti-reversal check (from 2Ship func_8083A4A4): decelerate if trying to go >90° backwards
        s16 yawDiff = player->yaw - yawTarget;
        if (ABS(yawDiff) > 0x6000) {
            Math_StepToF(&player->linearVelocity, 0.0f, 1.5f);
        } else {
            // Apply movement toward target (from 2Ship func_8083CB58 → func_8083CB04)
            Math_StepToF(&player->linearVelocity, speedTarget, 1.5f);
            Math_SmoothStepToS(&player->yaw, yawTarget, 2, 0x320, 0x14);
        }
    }

    // Decay spin speed (from 2Ship line 19296: unk_B10[0] += -800.0f)
    gFormState.dekuSpinSpeed += -800.0f;

    // Accumulate visual rotation (from 2Ship line 19297)
    // MM: shape.rot.y += BINANG_ADD(TRUNCF_BINANG(unk_B10[0]), BINANG_SUB(this->yaw, prevYaw))
    // OOT FIX: Use accumulator because OOT's concurrent idle action resets shape.rot.y to yaw each frame.
    // Without this, the spin doesn't visually accumulate and the model trembles.
    gFormState.dekuSpinRotAccum += (s16)(gFormState.dekuSpinSpeed) + (s16)(player->yaw - prevYaw);
    player->actor.shape.rot.y = player->yaw + (s16)gFormState.dekuSpinRotAccum;

    // Camera fix: OOT copies shape.rot.y → focus.rot.y. Override so camera follows yaw, not spin.
    // Also clear head/body look rotation (from 2Ship func_80836D8C at spin entry)
    player->actor.focus.rot.y = player->yaw;
    player->actor.focus.rot.x = 0;
    player->actor.focus.rot.z = 0;

    // Check if spin is done (from 2Ship line 19299: Math_StepToF(&unk_B10[1], 0, unk_B10[0]))
    f32 absSpeed = fabsf(gFormState.dekuSpinSpeed);
    if (Math_StepToF(&gFormState.dekuSpinTimer, 0.0f, absSpeed)) {
        // Spin ended - return to idle or movement
        player->actor.shape.rot.y = player->yaw;
        player->stateFlags2 &= ~PLAYER_STATE2_DISABLE_ROTATION_ALWAYS;
        gFormState.dekuSpinActive = 0;

        // Restore collider
        MmForm_ClearRollAttack(player);

        if (player->linearVelocity > 1.0f) {
            MmForm_SetAction(GORON_ACT_RUN, play, gFormState.runAnim, 1.0f, ANIMMODE_LOOP);
        } else {
            MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        }
        return;
    }

    // Transition from spin anim to idle when past midpoint (from 2Ship line 19302-19305)
    if (gFormState.formSkelAnime.animation == gFormState.dekuSpinAttack && gFormState.dekuSpinTimer < 0.0f) {
        LinkAnimation_PlayOnceSetSpeed(play, &gFormState.formSkelAnime, gFormState.idleAnim, 1.0f);
    }

    // === VFX: Kirakira sparkles from waist (from 2Ship func_808566C0 line 19309) ===
    // func_808566C0(play, this, PLAYER_BODYPART_WAIST, 1.0f, 0.5f, 0.0f, 32)
    // Spawns EffectSsKiraKira with prim=(255,200,200) env=(255,255,0), reddish-yellow sparkles
    {
        Color_RGBA8 primColor = { 255, 200, 200, 0 };
        Color_RGBA8 envColor = { 255, 255, 0, 0 };
        Vec3f kiraPos;
        Vec3f kiraVel = { 0.0f, 0.3f, 0.0f };
        Vec3f kiraAccel = { 0.0f, -0.025f, 0.0f };

        f32 sign = (Rand_ZeroOne() < 0.5f) ? -1.0f : 1.0f;
        kiraVel.x = (Rand_ZeroFloat(0.5f) + 1.0f) * sign;

        sign = (Rand_ZeroOne() < 0.5f) ? -1.0f : 1.0f;
        kiraVel.z = (Rand_ZeroFloat(0.5f) + 1.0f) * sign;

        kiraPos.x = player->bodyPartsPos[PLAYER_BODYPART_WAIST].x;
        kiraPos.y = Rand_ZeroFloat(15.0f) + player->bodyPartsPos[PLAYER_BODYPART_WAIST].y;
        kiraPos.z = player->bodyPartsPos[PLAYER_BODYPART_WAIST].z;

        s16 kiraScale = (Rand_ZeroOne() < 0.5f) ? 2000 : -150;
        EffectSsKiraKira_SpawnDispersed(play, &kiraPos, &kiraVel, &kiraAccel, &primColor, &envColor, kiraScale, 32);
    }

    // === VFX: Dust when spinning fast (from 2Ship line 19311) ===
    // if (unk_B10[0] > 9500.0f) { func_8083F8A8(play, this, 2.0f, 1, 2.5f, 10, 18, true) }
    if (gFormState.dekuSpinSpeed > 9500.0f) {
        Vec3f dustPos = player->actor.world.pos;
        dustPos.y += 5.0f;
        Vec3f dustVel = { 0.0f, 1.0f, 0.0f };
        Vec3f dustAccel = { 0.0f, -0.1f, 0.0f };
        f32 angle = Rand_ZeroFloat(65536.0f);
        dustVel.x = Math_SinS((s16)angle) * 2.5f;
        dustVel.z = Math_CosS((s16)angle) * 2.5f;
        Color_RGBA8 dustPrim = { 200, 180, 130, 255 };
        Color_RGBA8 dustEnv = { 120, 100, 60, 255 };
        EffectSsDust_Spawn(play, 0, &dustPos, &dustVel, &dustAccel, &dustPrim, &dustEnv, 10, 18, 20, 0);
    }

    // Floor SFX (from 2Ship line 19315: Actor_PlaySfx_Flagged2 with NA_SE_PL_SLIP_LEVEL)
    if ((gFormState.actionTimer % 4) == 0) {
#if !MM_SOUNDS_DISABLED
        MmSfx_PlayAtPos(MM_NA_SE_PL_SLIP_LEVEL, &player->actor.projectedPos);
#else
        Player_PlaySfx(&player->actor, NA_SE_PL_SLIP_LEVEL);
#endif
    }
    gFormState.actionTimer++;
}

// ---------------------------------------------------------------------------
// DEKU WATER HOP (from 2Ship func_808373F8 + func_8083784C, z_player.c line 7151-7270)
//
// Deku skips across water like a stone, up to 5 hops.
// Each hop launches the player upward with increasing speed.
// The 5th hop (counter reaches 0) triggers a spin attack.
// Counter resets to 5 whenever Deku touches solid ground.
// SFX: NA_SE_PL_DEKUNUTS_JUMP through JUMP5 (pitch rises per hop)
// ---------------------------------------------------------------------------
static void MmForm_DekuWaterHop(Player* player, PlayState* play) {
    // Base jump speed: 8.0f (Deku minimum from 2Ship line 7161-7162)
    // With IREG defaults at 0, the clamp to 8.0 is the effective base speed
    f32 speed = 8.0f;

    // Hop modifier: later hops are higher (from 2Ship line 7192)
    // speed *= 0.3f + ((5 - remainingHopsCounter) * 0.18f)
    //   hop 1 (counter=5): 8 * 0.30 = 2.4 → clamped to 4.0
    //   hop 2 (counter=4): 8 * 0.48 = 3.84 → clamped to 4.0
    //   hop 3 (counter=3): 8 * 0.66 = 5.28
    //   hop 4 (counter=2): 8 * 0.84 = 6.72
    //   hop 5 (counter=1): 8 * 1.02 = 8.16
    speed *= 0.3f + ((5 - gFormState.dekuHopsRemaining) * 0.18f);
    if (speed < 4.0f) {
        speed = 4.0f;
    }

    // Snap position above water surface (from 2Ship line 7198)
    player->actor.world.pos.y += player->actor.yDistToWater;

    // Launch upward (from 2Ship: func_80834D50 sets velocity.y = speed)
    player->actor.velocity.y = speed;
    player->actor.bgCheckFlags &= ~1; // Force airborne
    gFormState.wasOnGround = 0;

    // Water splash effect (from 2Ship func_80837730, z_player.c line 7224-7245)
    // Splash at the water surface (current pos after snap = water level)
    {
        Vec3f splashPos = player->actor.world.pos;
        EffectSsGSplash_Spawn(play, &splashPos, NULL, NULL,
                              (speed <= 10.0f) ? 0 : 1, // type 0=small, 1=big
                              (s16)(speed * 50.0f));    // scale from velocity
    }

    // Play hop SFX: pitch increases per hop (from 2Ship line 7202)
    // NA_SE_PL_DEKUNUTS_JUMP5 + 1 - counter:
    //   counter=5 → JUMP (0x09B0, lowest pitch)
    //   counter=1 → JUMP5 (0x09B4, highest pitch)
    {
        u16 hopSfx = MM_NA_SE_PL_DEKUNUTS_JUMP5 + 1 - gFormState.dekuHopsRemaining;
        MmForm_PlaySfx(player, hopSfx, NA_SE_PL_JUMP);
    }

    // Decrement counter (from 2Ship line 7204)
    gFormState.dekuHopsRemaining--;

    // Last hop → trigger spin attack (from 2Ship line 7205-7208)
    if (gFormState.dekuHopsRemaining == 0 && gFormState.dekuSpinAttack != NULL) {
        // From 2Ship: stateFlags2 |= PLAYER_STATE2_80000, then func_808373A4(play, this)
        MmForm_SetAction(MMFORM_ACT_DEKU_SPIN, play, gFormState.dekuSpinAttack, 1.0f, ANIMMODE_ONCE);
        gFormState.dekuSpinSpeed = 20000.0f;
        gFormState.dekuSpinTimer = 196608.0f; // 0x30000 as float
        gFormState.dekuSpinActive = 1;
        gFormState.dekuSpinRotAccum = 0;
        player->stateFlags2 |= PLAYER_STATE2_DISABLE_ROTATION_ALWAYS;
        MmForm_PlaySfx(player, MM_NA_SE_PL_DEKUNUTS_ATTACK, NA_SE_PL_BODY_HIT);
    } else {
        // Normal hop → enter jump action (ascending phase)
        // Natural flow: JUMP → peak → FALL → water contact → next hop
        LinkAnimationHeader* jumpAnim = gFormState.jumpAnim ? gFormState.jumpAnim : gFormState.idleAnim;
        MmForm_SetAction(MMFORM_ACT_JUMP, play, jumpAnim, 1.0f, ANIMMODE_ONCE);
    }
}

// ===========================================================================
// DEKU BUBBLE SYSTEM (from 2Ship func_808306F8 + EN_ARROW ARROW_TYPE_DEKU_BUBBLE)
//
// Hold B → enter first-person aim, bubble charges at Deku's mouth
// Release B → fire bubble projectile with physics (arc, wobble, bounce)
// Magic: 2 MP to fire. No magic → tiny bubble, pops immediately.
// Fully charged bubble bounces off walls once before popping.
// Damage: Deku Seed type (dmgFlags = 0x00010000, same as OOT ARROW_SEED/slingshot)
// ===========================================================================

// Bubble AT collider: slingshot/deku seed damage (from OOT EN_ARROW dmgFlags[ARROW_SEED])
static ColliderCylinderInit sBubbleColliderInit = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_PLAYER,
        AC_NONE,
        OC1_NONE,
        OC2_TYPE_PLAYER,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK2,
        { 0x00010000, 0x00, 0x01 }, // dmgFlags = 0x00010000 (ARROW_SEED/slingshot bit 16), damage = 1
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_ON | TOUCH_NEAREST | TOUCH_SFX_NONE,
        BUMP_NONE,
        OCELEM_NONE,
    },
    { 20, 30, 0, { 0, 0, 0 } }, // radius=20, height=30 (scaled by bubble.scale in update)
};

static void MmForm_InitBubbleCollider(Player* player, PlayState* play) {
    if (!gFormState.bubbleColliderInit) {
        Collider_InitCylinder(play, &gFormState.bubbleCollider);
        Collider_SetCylinder(play, &gFormState.bubbleCollider, &player->actor, &sBubbleColliderInit);
        gFormState.bubbleColliderInit = 1;
    }
}

// Fire bubble projectile from current charge state
// From 2Ship func_8088A594 (z_en_arrow.c:196) release path + func_8088A514 speed setup
static void MmForm_FireBubble(Player* player, PlayState* play) {
    // Direction from first-person aim (in MM, this comes from head limb matrix via
    // Matrix_MtxFToYXZRot in z_player_lib.c:4119-4133. We use focus.rot as equivalent.)
    s16 aimYaw = player->actor.focus.rot.y;
    s16 aimPitch = player->actor.focus.rot.x;

    // Magic check: 2 MP to fire (from 2Ship sMagicArrowCosts[ARROW_MAGIC_DEKU_BUBBLE])
    u8 hasMagic = (gSaveContext.magic >= 2);
    if (hasMagic) {
        gSaveContext.magic -= 2;
    }

    // Bubble scale from charge (from 2Ship func_8088A594 line 255: CLAMP_MIN(unk_144, 3.5))
    f32 charge = gFormState.bubbleCharge;
    if (!hasMagic) {
        charge = 1.0f;
    }

    gFormState.bubble.scale = CLAMP_MIN(charge, 3.5f);

    // Set initial rotation from aim direction
    gFormState.bubble.rotX = aimPitch;
    gFormState.bubble.rotY = aimYaw;

    // Speed from 2Ship func_8088A514: totalSpeed = CLAMP(16.0 - unk_144, 1.0, 80.0)
    // Then Actor_SetSpeeds: hSpeed = cos(rot.x) * totalSpeed, velY = -sin(rot.x) * totalSpeed
    f32 totalSpeed = 16.0f - gFormState.bubble.scale;
    totalSpeed = CLAMP(totalSpeed, 1.0f, 80.0f);
    gFormState.bubble.hSpeed = Math_CosS(aimPitch) * totalSpeed;
    gFormState.bubble.velY = -Math_SinS(aimPitch) * totalSpeed;

    // Spawn position: Deku's mouth area (in MM: offset {1300, -400, 0} from head matrix)
    gFormState.bubble.pos.x = player->actor.world.pos.x + Math_SinS(aimYaw) * 20.0f;
    gFormState.bubble.pos.y = player->actor.world.pos.y + 30.0f;
    gFormState.bubble.pos.z = player->actor.world.pos.z + Math_CosS(aimYaw) * 20.0f;
    Math_Vec3f_Copy(&gFormState.bubble.prevPos, &gFormState.bubble.pos);

    gFormState.bubble.timer = (!hasMagic) ? 10 : 99; // unk_260 = 99 (2Ship line 257)
    gFormState.bubble.wobbleAccX = 0;                // unk_14A
    gFormState.bubble.wobbleAccY = 0;                // unk_14C
    gFormState.bubble.state = 0;                     // unk_149 = 0 (just fired)
    gFormState.bubble.active = 1;

    // Init AT collider for damage (slingshot/deku seed type)
    MmForm_InitBubbleCollider(player, play);

    // SFX (from 2Ship func_8088A594 line 241: Player_PlaySfx NA_SE_PL_DEKUNUTS_FIRE)
    MmForm_PlaySfx(player, MM_NA_SE_PL_DEKUNUTS_FIRE, NA_SE_IT_SHIELD_REFLECT_SW);
}

// ---------------------------------------------------------------------------
// Action: DEKU BUBBLE AIM (first-person camera, hold B to charge, release to fire)
// From 2Ship func_808306F8 (z_player.c:4064) + EN_ARROW charge logic
// ---------------------------------------------------------------------------
static void MmForm_Action_DekuBubbleAim(Player* player, PlayState* play) {
    LinkAnimation_Update(play, &gFormState.formSkelAnime);
    Input* input = &play->state.input[0];

    // First frame: enter first-person
    if (gFormState.bubbleChargeTimer == 0) {
        FirstPerson_Init(player, play);
#if !MM_SOUNDS_DISABLED
        MmSfx_PlayAtPos(MM_NA_SE_PL_DEKUNUTS_BUBLE_BREATH, &player->actor.projectedPos);
#endif
    }

    // Keep first-person mode active
    FirstPerson_Update(player, play);
    player->linearVelocity = 0.0f;

    // Apply head/upper body rotation to follow aim direction
    // From OOT func_80836AB8 (z_player.c:3760): headLimbRot tracks focus.rot during aiming.
    // MmForm_OverrideLimbDraw already applies headLimbRot to HEAD limb.
    // Without these, func_80847298 decays headLimbRot to zero every frame.
    {
        s16 aimPitch = player->actor.focus.rot.x;
        s16 aimYawRel = player->actor.focus.rot.y - player->actor.shape.rot.y;
        player->headLimbRot.x = aimPitch;
        player->headLimbRot.y = aimYawRel;
        player->upperLimbRot.x = aimPitch / 2;
        player->upperLimbRot.y = aimYawRel / 2;
        player->unk_6AE_rotFlags |= UNK6AE_ROT_FOCUS_X | UNK6AE_ROT_FOCUS_Y | UNK6AE_ROT_HEAD_X | UNK6AE_ROT_HEAD_Y |
                                    UNK6AE_ROT_UPPER_X | UNK6AE_ROT_UPPER_Y;
    }

    // Charge while B held (from 2Ship EN_ARROW: Math_SmoothStepToF 0.07f, 1.8f toward 16.0)
    if (CHECK_BTN_ALL(input->cur.button, BTN_B)) {
        Math_SmoothStepToF(&gFormState.bubbleCharge, 16.0f, 0.07f, 1.8f, 0.01f);
        gFormState.bubbleChargeTimer++;

        // Charging SFX loop (breath sound every 8 frames)
        if ((gFormState.bubbleChargeTimer % 8) == 0) {
#if !MM_SOUNDS_DISABLED
            MmSfx_PlayAtPos(MM_NA_SE_PL_DEKUNUTS_BUBLE_BREATH, &player->actor.projectedPos);
#endif
        }
    } else {
        // B released → fire
        MmForm_FireBubble(player, play);
        FirstPerson_Exit(player, play);
        gFormState.bubbleCharging = 0;

        // Switch to fire animation
        if (gFormState.dekuBowShoot != NULL) {
            MmForm_SetAction(MMFORM_ACT_DEKU_BUBBLE, play, gFormState.dekuBowShoot, 1.0f, ANIMMODE_ONCE);
        } else {
            MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        }
        return;
    }

    // A button cancels aim
    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        FirstPerson_Exit(player, play);
        gFormState.bubbleCharging = 0;
        gFormState.bubbleCharge = 0.0f;
#if !MM_SOUNDS_DISABLED
        MmSfx_Stop(MM_NA_SE_PL_DEKUNUTS_BUBLE_BREATH);
#endif
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }

    // Draw aiming reticle (green tint)
    FirstPerson_DrawReticle(player, play, 0, 100, 255, 100);
}

// ---------------------------------------------------------------------------
// Action: DEKU BUBBLE FIRE (animation playback after bubble is launched)
// From 2Ship: pn_tamahaki animation plays out, then return to idle
// ---------------------------------------------------------------------------
static void MmForm_Action_DekuBubble(Player* player, PlayState* play) {
    LinkAnimation_Update(play, &gFormState.formSkelAnime);
    gFormState.actionTimer++;

    // Animation plays out, then return to idle
    if (gFormState.actionTimer > 8) {
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
    }
}

// ---------------------------------------------------------------------------
// Update bubble projectile (called every frame from MmForm_UpdateActive)
// VERBATIM from 2Ship func_8088ACE0 (z_en_arrow.c:365-557) bubble paths:
//   - NO gravity (gravity = 0 for bubbles, unlike arrows)
//   - Deflation: Math_StepToF(&scale, 1.0, 0.4) — dies when reaching 1.0
//   - Wobble: sinusoidal oscillation added to rot.x/rot.y, speed recomputed
//   - Movement: Actor_MoveWithGravity pattern (vel from rot.y + speed)
//   - Wall collision: BgCheck_ProjectileLineTest → bounce or pop
// ---------------------------------------------------------------------------
static void MmForm_KillBubble(Player* player, PlayState* play) {
    gFormState.bubble.active = 0;
    if (gFormState.bubbleColliderInit) {
        Collider_ResetCylinderAT(play, &gFormState.bubbleCollider.base);
    }
}

static void MmForm_UpdateBubbleProjectile(Player* player, PlayState* play) {
    if (!gFormState.bubble.active)
        return;

    // === DEATH CHECK: timer expired (from 2Ship line 397: DECR(unk_260) == 0) ===
    gFormState.bubble.timer--;
    if (gFormState.bubble.timer <= 0) {
        // Pop SFX (from 2Ship line 430: NA_SE_IT_DEKUNUTS_BUBLE_VANISH)
        Player_PlaySfx(&player->actor, NA_SE_IT_SHIELD_REFLECT_SW);
        MmForm_KillBubble(player, play);
        return;
    }

    // === FIRST FRAME: set prevPos 10 units behind velocity (from 2Ship line 470-479) ===
    if (gFormState.bubble.state == 0) {
        f32 totalVelMag = sqrtf(SQ(gFormState.bubble.hSpeed) + SQ(gFormState.bubble.velY));
        f32 velX = Math_SinS(gFormState.bubble.rotY) * gFormState.bubble.hSpeed;
        f32 velZ = Math_CosS(gFormState.bubble.rotY) * gFormState.bubble.hSpeed;

        if (totalVelMag > 0.001f) {
            f32 ratio = 10.0f / totalVelMag;
            gFormState.bubble.prevPos.x = gFormState.bubble.pos.x - (velX * ratio);
            gFormState.bubble.prevPos.y = gFormState.bubble.pos.y - (gFormState.bubble.velY * ratio);
            gFormState.bubble.prevPos.z = gFormState.bubble.pos.z - (velZ * ratio);
        }
        gFormState.bubble.state = 1; // unk_149 = 1 (flying)
    }

    // === DEFLATION + WOBBLE (from 2Ship line 484-495) ===
    if (Math_StepToF(&gFormState.bubble.scale, 1.0f, 0.4f)) {
        // Fully deflated → force timer to 0 (dies next check, 2Ship line 485: unk_260 = 0)
        gFormState.bubble.timer = 0;
    } else {
        // Wobble rot.x (from 2Ship line 488-490)
        gFormState.bubble.wobbleAccX += (s16)(gFormState.bubble.scale * (500.0f + Rand_ZeroFloat(1400.0f)));
        gFormState.bubble.rotX += (s16)(500.0f * Math_SinS(gFormState.bubble.wobbleAccX));

        // Wobble rot.y (from 2Ship line 492-494)
        gFormState.bubble.wobbleAccY += (s16)(gFormState.bubble.scale * (500.0f + Rand_ZeroFloat(1400.0f)));
        gFormState.bubble.rotY += (s16)(500.0f * Math_SinS(gFormState.bubble.wobbleAccY));

        // Recompute speed from new rotation + shrinking size (from 2Ship func_8088A514)
        f32 totalSpeed = 16.0f - gFormState.bubble.scale;
        totalSpeed = CLAMP(totalSpeed, 1.0f, 80.0f);
        // Actor_SetSpeeds (z_actor.c:1277): speed = cos(rot.x) * totalSpeed, velY = -sin(rot.x) * totalSpeed
        gFormState.bubble.hSpeed = Math_CosS(gFormState.bubble.rotX) * totalSpeed;
        gFormState.bubble.velY = -Math_SinS(gFormState.bubble.rotX) * totalSpeed;
    }

    // Looping flight SFX (from 2Ship line 497: NA_SE_IT_DEKUNUTS_BUBLE_SHOT_LEVEL - SFX_FLAG)
    // (play every frame as flagged SFX)

    // Save prevPos for collision line test
    Math_Vec3f_Copy(&gFormState.bubble.prevPos, &gFormState.bubble.pos);

    // === MOVEMENT: Actor_MoveWithGravity pattern (z_actor.c:1209-1226) ===
    // vel.x = speed * sin(rot.y), vel.z = speed * cos(rot.y)
    // vel.y += gravity (gravity = 0 for bubbles, no terminal velocity check needed)
    f32 velX = gFormState.bubble.hSpeed * Math_SinS(gFormState.bubble.rotY);
    f32 velZ = gFormState.bubble.hSpeed * Math_CosS(gFormState.bubble.rotY);
    // Actor_UpdatePos: pos += vel
    gFormState.bubble.pos.x += velX;
    gFormState.bubble.pos.y += gFormState.bubble.velY;
    gFormState.bubble.pos.z += velZ;

    // === WALL COLLISION (from 2Ship line 530-537: BgCheck_ProjectileLineTest) ===
    {
        CollisionPoly* wallPoly = NULL;
        s32 bgId;
        Vec3f wallHit;

        if (BgCheck_EntityLineTest1(&play->colCtx, &gFormState.bubble.prevPos, &gFormState.bubble.pos, &wallHit,
                                    &wallPoly, true, true, true, true, &bgId)) {
            // Bounce check (from 2Ship line 404-416: flip rot.y by ~180 + random, flip velY)
            if (gFormState.bubble.state != -1) {
                // First bounce: reverse direction like 2Ship
                Math_Vec3f_Copy(&gFormState.bubble.pos, &gFormState.bubble.prevPos);
                gFormState.bubble.rotY += (s16)(0x8000 + (s16)(Rand_CenteredFloat(0x1F40)));
                gFormState.bubble.velY = -gFormState.bubble.velY;
                gFormState.bubble.state = -1; // unk_149 = -1 (bounced)
            } else {
                // Already bounced → pop (from 2Ship line 426-430)
                MmForm_KillBubble(player, play);
                EffectSsBubble_Spawn(play, &wallHit, 0.0f, 5.0f, 10.0f, 0.13f);
                Player_PlaySfx(&player->actor, NA_SE_IT_SHIELD_REFLECT_SW);
                return;
            }
        }
    }

    // === AT COLLIDER: submit for damage via OOT collision system ===
    // Uses slingshot/deku seed damage type (dmgFlags = 0x00010000)
    if (gFormState.bubbleColliderInit) {
        ColliderCylinder* cyl = &gFormState.bubbleCollider;

        // Scale radius by bubble size (min 10, max from scale * 3)
        s16 radius = (s16)(gFormState.bubble.scale * 3.0f);
        if (radius < 10)
            radius = 10;
        cyl->dim.radius = radius;
        cyl->dim.height = radius * 2;
        cyl->dim.pos.x = (s16)gFormState.bubble.pos.x;
        cyl->dim.pos.y = (s16)gFormState.bubble.pos.y - radius;
        cyl->dim.pos.z = (s16)gFormState.bubble.pos.z;

        // Only deal damage when not bounced (from 2Ship line 704: unk_149 >= 0)
        if (gFormState.bubble.state >= 0) {
            CollisionCheck_SetAT(play, &play->colChkCtx, &cyl->base);
        }

        // Check if AT hit an actor this frame (from 2Ship line 397: atFlags & AT_HIT)
        if (cyl->base.atFlags & AT_HIT) {
            MmForm_KillBubble(player, play);
            // Pop effects (from 2Ship line 426-430)
            EffectSsBubble_Spawn(play, &gFormState.bubble.pos, 0.0f, 5.0f, 15.0f, 0.15f);
            Player_PlaySfx(&player->actor, NA_SE_IT_SHIELD_REFLECT_SW);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Draw bubble projectile (called from MmForm_Draw)
// VERBATIM from 2Ship EnArrow_Draw (z_en_arrow.c:696-743) bubble section:
//   - Matrix from Actor_Draw: Matrix_SetTranslateRotateYXZ with shape.rot (= rotX/rotY)
//   - Scale: non-uniform stretch along Z based on hSpeed (elongates in travel direction)
//   - Moving (hSpeed > 0): OPA with solid color (we use XLU since we lack setup DL 06F380)
//   - Stationary (hSpeed == 0): XLU billboard with fading alpha
// ---------------------------------------------------------------------------
static void MmForm_DrawBubbleProjectile(Player* player, PlayState* play) {
    if (!gFormState.bubble.active)
        return;

    OPEN_DISPS(play->state.gfxCtx);

    // From 2Ship z_en_arrow.c:703-712 — stretch factor from horizontal speed
    f32 spA0 = (gFormState.bubble.hSpeed * 0.1f) + 1.0f; // Z stretch (travel direction)
    f32 sp9C = (1.0f / spA0);                            // X/Y squish (perpendicular)
    f32 bubScale = gFormState.bubble.scale;

    sp9C *= 0.002f;
    spA0 *= 0.002f;

    u8 usedMmDL = 0;

    if (gFormState.bubble.hSpeed > 0.0f && sDekuBubbleMoveDLCount > 0 && !sDekuBubbleMoveDLSafeCopy.empty()) {
        // === MOVING BUBBLE (from 2Ship line 730-738) ===
        // Orientation from stored rotX/rotY (= shape.rot, set by Actor_Draw in 2Ship)
        Vec3s bubbleRot = { gFormState.bubble.rotX, gFormState.bubble.rotY, 0 };
        Matrix_SetTranslateRotateYXZ(gFormState.bubble.pos.x, gFormState.bubble.pos.y, gFormState.bubble.pos.z,
                                     &bubbleRot);
        Matrix_Scale(bubScale * sp9C, bubScale * sp9C, bubScale * spA0, MTXMODE_APPLY);
        Matrix_Translate(0.0f, 0.0f, 460.0f, MTXMODE_APPLY);

        // XLU with translucency (2Ship uses OPA + setup DL 06F380 with hilite textures;
        // we skip setup DL since it needs segment 0x0F, use XLU with alpha instead)
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0,
                          PRIMITIVE);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x7F, 230, 225, 150, 170);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

        static const size_t DL_PAD_MOVE = 16;
        size_t total = sDekuBubbleMoveDLCount + DL_PAD_MOVE;
        Gfx* dlCopy = (Gfx*)Graph_Alloc(play->state.gfxCtx, total * sizeof(Gfx));
        memcpy(dlCopy, sDekuBubbleMoveDLSafeCopy.data(), sDekuBubbleMoveDLCount * sizeof(Gfx));
        for (size_t p = 0; p < DL_PAD_MOVE; p++) {
            dlCopy[sDekuBubbleMoveDLCount + p].words.w0 = (uintptr_t)0xDF << 24;
            dlCopy[sDekuBubbleMoveDLCount + p].words.w1 = 0;
        }
        // Defensive: patch any segment 0x08 refs to gEmptyDL (safe no-op)
        MmForm_PatchSegmentedDL(dlCopy, sDekuBubbleMoveDLCount, 0x08, gEmptyDL);
        // Patch G_DL_INDEX seg 0x0C → direct pointers to cull DLs
        MmForm_PatchCullDLIndex(dlCopy, sDekuBubbleMoveDLCount);
        gSPDisplayList(POLY_XLU_DISP++, dlCopy);
        usedMmDL = 1;

    } else if (sDekuBubbleStillDLCount > 0 && !sDekuBubbleStillDLSafeCopy.empty()) {
        // === STATIONARY BUBBLE: billboard (from 2Ship line 719-729) ===
        s32 alpha = 255 - (s32)(bubScale * 4.0f);
        if (alpha < 50)
            alpha = 50;

        // Billboard (from 2Ship: Matrix_ReplaceRotation(&gIdentityMtxF) + multiply by D_01000000)
        Vec3s bubbleRot = { gFormState.bubble.rotX, gFormState.bubble.rotY, 0 };
        Matrix_SetTranslateRotateYXZ(gFormState.bubble.pos.x, gFormState.bubble.pos.y, gFormState.bubble.pos.z,
                                     &bubbleRot);
        Matrix_Scale(bubScale * sp9C, bubScale * sp9C, bubScale * spA0, MTXMODE_APPLY);
        Matrix_Translate(0.0f, 0.0f, 460.0f, MTXMODE_APPLY);
        Matrix_ReplaceRotation(&play->billboardMtxF);

        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, PRIMITIVE, 0, 0, 0, ENVIRONMENT, 0, 0, 0, PRIMITIVE, 0, 0, 0,
                          ENVIRONMENT);
        gDPSetEnvColor(POLY_XLU_DISP++, 230, 225, 150, alpha);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x7F, 230, 225, 150, 255);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

        static const size_t DL_PAD_STILL = 16;
        size_t totalS = sDekuBubbleStillDLCount + DL_PAD_STILL;
        Gfx* dlCopyS = (Gfx*)Graph_Alloc(play->state.gfxCtx, totalS * sizeof(Gfx));
        memcpy(dlCopyS, sDekuBubbleStillDLSafeCopy.data(), sDekuBubbleStillDLCount * sizeof(Gfx));
        for (size_t p = 0; p < DL_PAD_STILL; p++) {
            dlCopyS[sDekuBubbleStillDLCount + p].words.w0 = (uintptr_t)0xDF << 24;
            dlCopyS[sDekuBubbleStillDLCount + p].words.w1 = 0;
        }
        // Defensive: patch any segment 0x08 refs to gEmptyDL (safe no-op)
        MmForm_PatchSegmentedDL(dlCopyS, sDekuBubbleStillDLCount, 0x08, gEmptyDL);
        // Patch G_DL_INDEX seg 0x0C → direct pointers to cull DLs
        MmForm_PatchCullDLIndex(dlCopyS, sDekuBubbleStillDLCount);
        gSPDisplayList(POLY_XLU_DISP++, dlCopyS);
        usedMmDL = 1;
    }

    // Fallback: OOT gEffBubbleDL if MM DLs not loaded from mm.o2r
    if (!usedMmDL) {
        Vec3s fbRot = { gFormState.bubble.rotX, gFormState.bubble.rotY, 0 };
        Matrix_SetTranslateRotateYXZ(gFormState.bubble.pos.x, gFormState.bubble.pos.y, gFormState.bubble.pos.z, &fbRot);
        Matrix_Scale(bubScale * sp9C, bubScale * sp9C, bubScale * spA0, MTXMODE_APPLY);
        Matrix_Translate(0.0f, 0.0f, 460.0f, MTXMODE_APPLY);

        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 230, 225, 150, 170);
        gDPSetEnvColor(POLY_XLU_DISP++, 150, 150, 100, 0);
        gSPSegment(POLY_XLU_DISP++, 0x08, (uintptr_t)gEffBubble1Tex);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gEffBubbleDL);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

// ===========================================================================
// DEKU FLOWER + FLIGHT SYSTEM
// From 2Ship Player_Action_93 (flower, z_player.c:18896) and Player_Action_94 (flight, z_player.c:19084)
//
// Triggered by Deku Leaf item:
//   Ground: C-button → 10 MP → burrow → charge → launch → flight
//   Air:    C-button → direct flight (no MP cost)
//
// Player_Action_93 (flower) has 4 phases via dekuFlowerPhase:
//   0 = sinking into ground (dekuFlowerDepth: 0 → -1000)
//   1 = compressing underground (dekuFlowerDepth: -1000 → -3900, actor squash/stretch)
//   2 = charging (hold A, dekuFlowerCharge 0-15, golden at >=10)
//   3 = launching upward (dekuFlowerDepth: -3900 → 0, then → DEKU_FLY)
//
// Player_Action_94 (flight) phases via dekuFlightFlags:
//   RISING: ascending after launch, gravity=-5.5, AT collider active
//   RISING+vel<0: flower opening transition (pn_kakku anim, frame>6 → boost vel.y=6)
//   OPEN: gliding, gravity=-0.2/terminal=-0.38, distance-based petal speed
//   A press or distance exceeded → close flower → DEKU_FALL_LOCKED
// ===========================================================================

// Helper: smooth step float toward target (from 2Ship func_80856888, z_player.c:19061-19077)
static s32 MmForm_StepToF(f32* value, f32 target, f32 step) {
    if (step != 0.0f) {
        if (target < *value) {
            step = -step;
        }
        *value += step;
        if (((*value - target) * step) >= 0.0f) {
            *value = target;
            return true;
        }
    } else if (target == *value) {
        return true;
    }
    return false;
}

// Helper: Deku kirakira sparkle effect from body part
// From 2Ship func_808566C0 (z_player.c:19018-19056)
static void MmForm_DekuSparkle(PlayState* play, Player* player, s32 bodyPart, f32 arg3, f32 arg4, f32 arg5, s32 life) {
    Color_RGBA8 primColor = { 255, 200, 200, 0 };
    Color_RGBA8 envColor = { 255, 255, 0, 0 };
    Vec3f vel = { 0.0f, 0.3f, 0.0f };
    Vec3f accel = { 0.0f, -0.025f, 0.0f };
    Vec3f pos;
    f32 sign;

    sign = (Rand_ZeroOne() < 0.5f) ? -1.0f : 1.0f;
    vel.x = (Rand_ZeroFloat(arg4) + arg3) * sign;
    accel.x = arg5 * sign;

    sign = (Rand_ZeroOne() < 0.5f) ? -1.0f : 1.0f;
    vel.z = (Rand_ZeroFloat(arg4) + arg3) * sign;
    accel.z = arg5 * sign;

    pos.x = player->bodyPartsPos[bodyPart].x;
    pos.y = Rand_ZeroFloat(15.0f) + player->bodyPartsPos[bodyPart].y;
    pos.z = player->bodyPartsPos[bodyPart].z;

    s16 scale = (Rand_ZeroOne() < 0.5f) ? 2000 : -150;
    EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &primColor, &envColor, scale, life);
}

// Helper: Pollen/dust particle effect during flower sequence
// From 2Ship func_80856110 (z_player.c:18878-18893)
// Uses OOT's EffectSsDust_Spawn as equivalent to MM's func_800B0EB0
static void MmForm_DekuPollenEffect(PlayState* play, Player* player, f32 yOffset, f32 velY, f32 accelY, s16 scale,
                                    s16 scaleStep, s16 life) {
    Vec3f pos;
    pos.x = player->actor.world.pos.x;
    pos.y = player->actor.world.pos.y + yOffset;
    pos.z = player->actor.world.pos.z;

    Color_RGBA8 primColor = { 255, 255, 55, 255 };
    Color_RGBA8 envColor = { 100, 50, 0, 0 };
    Vec3f vel = { 0.0f, velY, 0.0f };
    Vec3f accelV = { 0.0f, accelY, 0.0f };

    EffectSsDust_Spawn(play, 0, &pos, &vel, &accelV, &primColor, &envColor, scale, scaleStep, life, 0);
}

// Helper: Underground vibration effect (from 2Ship func_80856074, z_player.c:18872-18876)
static void MmForm_DekuUndergroundEffect(PlayState* play, Player* player) {
    EffectSsHahen_SpawnBurst(play, &player->actor.world.pos, 3.0f, 0, 4, 8, 2, -1, 10, NULL);
}

// Helper: Charge phase yaw control (from 2Ship func_80855F9C, z_player.c:18851-18858)
static void MmForm_DekuChargeYawUpdate(Player* player, PlayState* play) {
    f32 speedTarget;
    s16 yawTarget;

    player->stateFlags2 |= PLAYER_STATE2_DISABLE_ROTATION_ALWAYS;
    Player_GetMovementSpeedAndYaw(player, &speedTarget, &yawTarget, SPEED_MODE_CURVED, play);
    Math_ScaledStepToS(&player->yaw, yawTarget, 0x258);
}

// ---------------------------------------------------------------------------
// MmForm_StartDekuFlower - Enter flower burrow from ground
// From 2Ship func_80836DC0 (z_player.c:6979-6994)
// ---------------------------------------------------------------------------
static void MmForm_StartDekuFlower(Player* player, PlayState* play) {
    MmForm_SetAction(MMFORM_ACT_DEKU_FLOWER, play, gFormState.dekuSpinAttack, 1.0f, ANIMMODE_ONCE);
    player->linearVelocity = 0.0f;

    gFormState.dekuFlowerDepth = 0.0f;
    gFormState.dekuFlowerVelocity = -2000.0f;
    gFormState.dekuFlowerPhase = 0;
    gFormState.dekuFlowerCharge = 0;
    gFormState.dekuBudCounter = 0;
    gFormState.dekuFlightFlags = 0;
    gFormState.dekuPetalSpeed = 0;
    gFormState.dekuPetalAngle = 0;
    gFormState.dekuPitchAngle = 0;
    gFormState.dekuRollAngle = 0;
    gFormState.dekuSparkleAcc = 0;

    gFormState.dekuSavedShadowScale = player->actor.shape.shadowScale;
    player->actor.shape.shadowScale = 13.0f;

    // Reset head/body limb rotations (from 2Ship func_80836D8C, z_player.c:6966-6977)
    player->actor.focus.rot.x = 0;
    player->actor.focus.rot.z = 0;
    player->headLimbRot.x = 0;
    player->headLimbRot.y = 0;
    player->headLimbRot.z = 0;
    player->upperLimbRot.x = 0;
    player->upperLimbRot.y = 0;
    player->upperLimbRot.z = 0;

    MmForm_PlaySfx(player, MM_NA_SE_PL_DEKUNUTS_IN_GRD, NA_SE_PL_BODY_HIT);
}

// ---------------------------------------------------------------------------
// MmForm_StartDekuFlightMidair - Enter flight directly from midair
// ---------------------------------------------------------------------------
static void MmForm_StartDekuFlightMidair(Player* player, PlayState* play) {
    Math_Vec3f_Copy(&gFormState.dekuLaunchPos, &player->actor.world.pos);

    MmForm_SetAction(MMFORM_ACT_DEKU_FLY, play, gFormState.dekuFlightLaunch, 1.0f, ANIMMODE_ONCE);

    gFormState.dekuFlightFlags = DEKU_FLIGHT_RISING | DEKU_FLIGHT_GOLDEN;
    gFormState.dekuFlightLaunchType = 0; // Treat as normal dyna (allows flower opening)
    gFormState.dekuFlightTimer = 9999;
    gFormState.dekuPetalSpeed = 0;
    gFormState.dekuPetalAngle = 0;
    gFormState.dekuPitchAngle = 0;
    gFormState.dekuRollAngle = 0;
    gFormState.dekuSparkleAcc = 0;
    gFormState.dekuFlowerDepth = 0.0f;
    gFormState.dekuFlowerVelocity = 0.0f;
    gFormState.dekuFlowerPhase = 0;
    gFormState.dekuFlowerCharge = 10; // Treat as golden so it gets full range
    gFormState.dekuBudCounter = 0;
    gFormState.dekuSavedShadowScale = player->actor.shape.shadowScale;

    // Camera: set airborne flags so OOT camera tracks vertical movement
    player->stateFlags1 |= PLAYER_STATE1_JUMPING;
    player->fallStartHeight = player->actor.world.pos.y;
    Camera_ChangeMode(GET_ACTIVE_CAM(play), CAM_MODE_JUMP);

    MmForm_PlaySfx(player, MM_NA_SE_IT_DEKUNUTS_FLOWER_OPEN, NA_SE_PL_BODY_HIT);
}

// ---------------------------------------------------------------------------
// MmForm_EndDekuFly - Close flower and transition to post-flight fall
// From 2Ship func_808355D8 (z_player.c:6397-6402)
// ---------------------------------------------------------------------------
static void MmForm_EndDekuFly(Player* player, PlayState* play, LinkAnimationHeader* anim) {
    MmForm_SetAction(MMFORM_ACT_DEKU_FALL_LOCKED, play, anim, 1.0f, ANIMMODE_ONCE);
    gFormState.dekuFlightFlags &= ~(DEKU_FLIGHT_OPEN | DEKU_FLIGHT_RISING | DEKU_FLIGHT_GOLDEN);

    player->cylinder.dim.radius = (s16)sFormProps[gFormState.currentForm].cylinderRadius;
    player->cylinder.base.atFlags &= ~AT_ON;

    MmForm_PlaySfx(player, MM_NA_SE_IT_DEKUNUTS_FLOWER_CLOSE, NA_SE_PL_BODY_HIT);
}

// ---------------------------------------------------------------------------
// MmForm_Action_DekuFlower - Flower burrow/charge/launch
// VERBATIM port of Player_Action_93 (2Ship z_player.c:18896-19016)
// ---------------------------------------------------------------------------
static void MmForm_Action_DekuFlower(Player* player, PlayState* play) {
    f32 temp;

    LinkAnimation_Update(play, &gFormState.formSkelAnime);
    gFormState.actionTimer++;

    if (gFormState.dekuFlowerPhase == 0) {
        // Phase 0: Initial sinking (from 2Ship line 18909-18916)
        gFormState.dekuFlowerDepth += gFormState.dekuFlowerVelocity;
        if (gFormState.dekuFlowerDepth < -1000.0f) {
            gFormState.dekuFlowerDepth = -1000.0f;
            gFormState.dekuFlowerPhase = 1;
            gFormState.dekuFlowerVelocity = 0.0f;
        }
        MmForm_DekuUndergroundEffect(play, player);

    } else if (gFormState.dekuFlowerPhase == 1) {
        // Phase 1: Accelerating compression (from 2Ship line 18917-18944)
        gFormState.dekuFlowerVelocity += -22.0f;
        if (gFormState.dekuFlowerVelocity < -170.0f) {
            gFormState.dekuFlowerVelocity = -170.0f;
        }
        gFormState.dekuFlowerDepth += gFormState.dekuFlowerVelocity;

        if (gFormState.dekuFlowerDepth < -3900.0f) {
            gFormState.dekuFlowerDepth = -3900.0f;
            gFormState.dekuFlowerPhase = 2;
            player->actor.shape.rot.y = Camera_GetInputDirYaw(GET_ACTIVE_CAM(play));
            player->actor.scale.y = 0.01f;
            player->yaw = player->actor.world.rot.y = player->actor.shape.rot.y;
        } else {
            // Squash/stretch effect (from 2Ship line 18930-18932)
            temp = Math_SinS((s16)((1000.0f + gFormState.dekuFlowerDepth) * (-30.0f))) * 0.004f;
            player->actor.scale.y = 0.01f + temp;
            player->actor.scale.z = player->actor.scale.x = 0.01f - (gFormState.dekuFlowerVelocity * -0.000015f);
            // Rotate during compression (from 2Ship line 18934)
            player->actor.shape.rot.y += (s16)(gFormState.dekuFlowerVelocity * 130.0f);
        }
        MmForm_DekuUndergroundEffect(play, player);

    } else if (gFormState.dekuFlowerPhase == 2) {
        // Phase 2: Hold Deku Leaf C-button underground, release to launch
        // Velocity scales with hold time: 10 (instant release) to 40 (2 seconds)
        // 40 frames = 2 seconds at 20fps → velocity.y = 10 + min(charge,40) * 0.75

        if (!ItemHeld_IsButtonHeld(ITEM_DEKU_LEAF, player, play)) {
            // Released → check ceiling then launch
            CollisionPoly* poly;
            s32 bgId;
            Vec3f ceilPos;
            f32 ceilHeight;

            ceilPos.x = player->actor.world.pos.x;
            ceilPos.y = player->actor.world.pos.y - 20.0f;
            ceilPos.z = player->actor.world.pos.z;

            if (BgCheck_EntityCheckCeiling(&play->colCtx, &ceilHeight, &ceilPos, 30.0f, &poly, &bgId, &player->actor)) {
                // Ceiling blocked → stay underground, reset charge
                gFormState.dekuFlowerCharge = 0;
            } else {
                // Launch! Velocity.y = 10..40 based on hold time (model-space: /0.01 → 1000..4000)
                s32 clampedCharge = (gFormState.dekuFlowerCharge > 40) ? 40 : gFormState.dekuFlowerCharge;
                gFormState.dekuFlowerVelocity = 1000.0f + (clampedCharge * 75.0f);
                gFormState.dekuFlowerPhase = 3;
                // Treat as "golden" if held >= 20 frames (1 second) for extra flight range
                if (clampedCharge >= 20) {
                    gFormState.dekuFlowerCharge = 10; // Signals golden to phase 3 transition
                } else {
                    gFormState.dekuFlowerCharge = 0;
                }
                // Pollen burst on launch
                MmForm_DekuPollenEffect(play, player, 20.0f, 5.0f, -0.1f, 200, 30, 20);
                MmForm_DekuPollenEffect(play, player, 10.0f, 3.8f, -0.05f, 140, 23, 15);
                MmForm_PlaySfx(player, MM_NA_SE_PL_DEKUNUTS_OUT_GRD, NA_SE_PL_BODY_HIT);
            }
        } else {
            // Still holding → charge
            if (gFormState.dekuFlowerCharge < 40) {
                gFormState.dekuFlowerCharge++;
            }
            // Periodic pollen effect while charging (every 10 frames after first 5)
            if (gFormState.dekuFlowerCharge > 5 && (gFormState.dekuFlowerCharge % 10) == 0) {
                MmForm_DekuPollenEffect(play, player, 15.0f, 2.0f, -0.08f, 100, 15, 12);
            }
        }
        MmForm_DekuChargeYawUpdate(player, play);

    } else {
        // Phase 3: Launching upward (from 2Ship line 18965-19005)
        gFormState.dekuFlowerDepth += gFormState.dekuFlowerVelocity;

        temp = gFormState.dekuFlowerDepth;
        if (temp >= 0.0f) {
            // Emerged → transition to flight
            f32 speed = gFormState.dekuFlowerVelocity * player->actor.scale.y;
            s32 isGolden = (gFormState.dekuFlowerCharge >= 10);

            Math_Vec3f_Copy(&gFormState.dekuLaunchPos, &player->actor.world.pos);
            gFormState.dekuFlowerDepth = 0.0f;
            player->actor.world.pos.y += temp * player->actor.scale.y;
            player->actor.scale.x = player->actor.scale.y = player->actor.scale.z = 0.01f;

            MmForm_SetAction(MMFORM_ACT_DEKU_FLY, play, gFormState.dekuFlightLaunch, speed, ANIMMODE_ONCE);

            gFormState.dekuFlightFlags |= DEKU_FLIGHT_RISING;
            if (isGolden) {
                gFormState.dekuFlightFlags |= DEKU_FLIGHT_GOLDEN;
            }
            gFormState.dekuFlightFlags |= DEKU_FLIGHT_FROM_SCENE;
            gFormState.dekuFlightLaunchType = isGolden ? 1 : 0;
            gFormState.dekuFlightTimer = 9999;

            player->actor.shape.shadowScale = gFormState.dekuSavedShadowScale;

            // Launch velocity (from 2Ship func_80834CD0: velocity.y = speed)
            player->actor.velocity.y = speed;
            player->actor.bgCheckFlags &= ~0x1; // Clear BGCHECKFLAG_GROUND
            player->stateFlags1 |= PLAYER_STATE1_JUMPING;
            player->fallStartHeight = player->actor.world.pos.y;
            player->actor.shape.yOffset = 0.0f; // Reset visual offset from burrow
            Camera_ChangeMode(GET_ACTIVE_CAM(play), CAM_MODE_JUMP);

            // AT collider for launch damage
            {
                ColliderCylinder* cyl = &player->cylinder;
                cyl->info.toucher.dmgFlags = DMG_SLASH_MASTER;
                cyl->info.toucher.damage = 2;
                cyl->base.atFlags = AT_ON | AT_TYPE_PLAYER;
                cyl->dim.radius = 20;
                CollisionCheck_SetAT(play, &play->colChkCtx, &cyl->base);
            }
            return; // Don't execute gravity/velocity zeroing below
        } else if (gFormState.dekuFlowerDepth < 0.0f) {
            MmForm_DekuUndergroundEffect(play, player);
        }
    }

    // Bud counter (from 2Ship line 19007-19015)
    if (gFormState.dekuFlowerDepth < -1500.0f) {
        gFormState.dekuFlightFlags |= DEKU_FLIGHT_UNDERGROUND;
        if (gFormState.dekuBudCounter < 8) {
            gFormState.dekuBudCounter++;
            if (gFormState.dekuBudCounter == 8) {
                MmForm_PlaySfx(player, MM_NA_SE_PL_DEKUNUTS_BUD, NA_SE_PL_BODY_HIT);
            }
        }
    }

    // Apply depth as visual Y offset (from 2Ship z_player.c:12852: shape.yOffset = unk_ABC)
    player->actor.shape.yOffset = gFormState.dekuFlowerDepth;

    // Prevent OOT gravity/movement during flower
    player->actor.gravity = 0.0f;
    player->actor.velocity.y = 0.0f;
    player->linearVelocity = 0.0f;
}

// ---------------------------------------------------------------------------
// MmForm_Action_DekuFly - Flight/glide action
// VERBATIM port of Player_Action_94 (2Ship z_player.c:19084-19273)
// ---------------------------------------------------------------------------
static void MmForm_Action_DekuFly(Player* player, PlayState* play) {
    Input* input = &play->state.input[0];

    gFormState.actionTimer++;

    // Keep camera tracking the player in air (jump mode follows Y axis)
    player->stateFlags1 |= PLAYER_STATE1_JUMPING;
    Camera_ChangeMode(GET_ACTIVE_CAM(play), CAM_MODE_JUMP);

    // Ground landing (from 2Ship line 19093-19096: func_80837134)
    if (MMFORM_ON_GROUND(player)) {
        if (gFormState.formSkelAnime.animation == gFormState.dekuFlightFall ||
            gFormState.formSkelAnime.animation == gFormState.dekuFlightLand) {
            EffectSsHahen_SpawnBurst(play, &player->bodyPartsPos[PLAYER_BODYPART_L_HAND], 2.0f, 0, 4, 4, 2, -1, 10,
                                     NULL);
            EffectSsHahen_SpawnBurst(play, &player->bodyPartsPos[PLAYER_BODYPART_R_HAND], 2.0f, 0, 4, 4, 2, -1, 10,
                                     NULL);
        }
        gFormState.dekuFlightFlags = 0;
        player->actor.shape.shadowScale = gFormState.dekuSavedShadowScale;
        player->cylinder.dim.radius = (s16)sFormProps[gFormState.currentForm].cylinderRadius;
        player->cylinder.base.atFlags &= ~AT_ON;
        player->stateFlags1 &= ~(PLAYER_STATE1_INPUT_DISABLED | PLAYER_STATE1_JUMPING);
        Camera_ChangeMode(GET_ACTIVE_CAM(play), CAM_MODE_NORMAL);
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }

    // Rising phase (from 2Ship line 19098-19106)
    if ((player->actor.velocity.y > 0.0f) && (gFormState.dekuFlightFlags & DEKU_FLIGHT_RISING)) {
        player->actor.minVelocityY = -20.0f;
        player->actor.gravity = -5.5f;

        {
            ColliderCylinder* cyl = &player->cylinder;
            cyl->info.toucher.dmgFlags = DMG_SLASH_MASTER;
            cyl->info.toucher.damage = 2;
            cyl->base.atFlags = AT_ON | AT_TYPE_PLAYER;
            cyl->dim.radius = 20;
            CollisionCheck_SetAT(play, &play->colChkCtx, &cyl->base);
        }
        MmForm_DekuPollenEffect(play, player, 0.0f, 0.0f, -1.0f, 500, 0, 8);

        if (player->actor.bgCheckFlags & 0x10) {
            MmForm_EndDekuFly(player, play, gFormState.dekuFlightFall);
        }
        return;
    }

    // No golden charge → fall (from 2Ship line 19107-19108)
    if (!(gFormState.dekuFlightFlags & DEKU_FLIGHT_GOLDEN)) {
        MmForm_EndDekuFly(player, play, gFormState.dekuFlightFall);
        return;
    }

    // Flower opening phase (from 2Ship line 19109-19127)
    if (gFormState.dekuFlightFlags & DEKU_FLIGHT_RISING) {
        if (player->actor.velocity.y < 0.0f) {
            if (gFormState.dekuFlightLaunchType < 0) {
                MmForm_EndDekuFly(player, play, gFormState.dekuFlightFall);
                return;
            }
            LinkAnimation_Update(play, &gFormState.formSkelAnime);
            if (gFormState.formSkelAnime.curFrame > 6.0f) {
                player->actor.velocity.y = 6.0f;
                gFormState.dekuFlightFlags &= ~DEKU_FLIGHT_RISING;
                gFormState.dekuFlightFlags |= DEKU_FLIGHT_OPEN;
                MmForm_PlaySfx(player, MM_NA_SE_IT_DEKUNUTS_FLOWER_OPEN, NA_SE_PL_BODY_HIT);
            }
        }
        player->actor.minVelocityY = -10.0f;
        player->actor.gravity = -0.5f;
        player->cylinder.dim.radius = (s16)sFormProps[gFormState.currentForm].cylinderRadius;
        player->cylinder.base.atFlags &= ~AT_ON;
        return;
    }

    // A press → close flower (from 2Ship line 19128-19129)
    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        MmForm_EndDekuFly(player, play, gFormState.dekuFlightLand);
        return;
    }

    // === Main glide physics (from 2Ship line 19130-19270) ===
    {
        s16 petalTarget;
        f32 distRemaining;
        f32 speedTarget;
        s16 yawTarget;

        player->linearVelocity = sqrtf(SQ(player->actor.velocity.x) + SQ(player->actor.velocity.z));
        if (player->linearVelocity != 0.0f) {
            s16 velYaw = Math_Atan2S(player->actor.velocity.z, player->actor.velocity.x);
            s16 yawDiff = player->actor.shape.rot.y - velYaw;
            if (ABS(yawDiff) > 0x4000) {
                player->linearVelocity = -player->linearVelocity;
                velYaw += 0x8000;
            }
            player->yaw = velYaw;
        }

        if (gFormState.dekuFlightTimer > 0) {
            gFormState.dekuFlightTimer--;
        }

        f32 maxDist = sDekuFlightMaxDist[(gFormState.dekuFlightLaunchType > 0) ? 1 : 0];
        distRemaining = maxDist - Math_Vec3f_DistXZ(&player->actor.world.pos, &gFormState.dekuLaunchPos);

        LinkAnimation_Update(play, &gFormState.formSkelAnime);

        if ((gFormState.dekuFlightTimer != 0) && (distRemaining > 300.0f)) {
            petalTarget = 0x1770;
            if (gFormState.formSkelAnime.animation != gFormState.dekuFlightLaunch) {
                LinkAnimation_PlayOnceSetSpeed(play, &gFormState.formSkelAnime, gFormState.dekuFlightLand, 1.0f);
            } else if (LinkAnimation_OnFrame(&gFormState.formSkelAnime, 8.0f)) {
                s32 i;
                for (i = 0; i < 13; i++) {
                    MmForm_DekuSparkle(play, player, PLAYER_BODYPART_L_HAND, 0.6f, 1.0f, 0.8f, 17);
                    MmForm_DekuSparkle(play, player, PLAYER_BODYPART_R_HAND, 0.6f, 1.0f, 0.8f, 17);
                }
            }
        } else if ((gFormState.dekuFlightTimer == 0) || (distRemaining < 0.0f)) {
            petalTarget = 0;
            MmForm_EndDekuFly(player, play, gFormState.dekuFlightFall);
            return;
        } else {
            petalTarget = 0x1770 - (s16)((300.0f - distRemaining) * 10.0f);
            if (gFormState.formSkelAnime.animation != gFormState.dekuFlightFlutter) {
                LinkAnimation_Change(play, &gFormState.formSkelAnime, gFormState.dekuFlightFlutter, 1.0f, 0.0f,
                                     Animation_GetLastFrame(gFormState.dekuFlightFlutter), ANIMMODE_LOOP, -8.0f);
            } else if (LinkAnimation_OnFrame(&gFormState.formSkelAnime, 6.0f)) {
                Audio_PlayActorSound2(&player->actor, NA_SE_PL_WALK_GROUND);
            }
        }

        // Petal rotation (from 2Ship line 19198-19210)
        // Inline Math_AsymStepToS (OOT doesn't have it; both step values = 0x190)
        {
            s16 petalDiff = petalTarget - gFormState.dekuPetalSpeed;
            s16 petalStep = (petalDiff >= 0) ? 0x190 : 0x190;
            if (ABS(petalDiff) <= petalStep) {
                gFormState.dekuPetalSpeed = petalTarget;
            } else {
                gFormState.dekuPetalSpeed += (petalDiff > 0) ? petalStep : -petalStep;
            }
        }
        gFormState.dekuPetalAngle += gFormState.dekuPetalSpeed;

        {
            s32 absSpeed = ABS(gFormState.dekuPetalSpeed);
            if (absSpeed > 0xFA0) {
                gFormState.dekuSparkleAcc += (u16)(ABS(gFormState.dekuPetalSpeed) * 0.01f);
            }
        }
        if (gFormState.dekuSparkleAcc > 200) {
            gFormState.dekuSparkleAcc -= 200;
            MmForm_DekuSparkle(play, player, PLAYER_BODYPART_L_HAND, 0.0f, 1.0f, 0.0f, 32);
            MmForm_DekuSparkle(play, player, PLAYER_BODYPART_R_HAND, 0.0f, 1.0f, 0.0f, 32);
        }

        // Gravity during glide (from 2Ship line 19230-19237)
        if (player->actor.velocity.y < 0.0f) {
            if (petalTarget != 0) {
                player->actor.minVelocityY = -0.38f;
                player->actor.gravity = -0.2f;
            } else {
                player->actor.minVelocityY = (gFormState.dekuPetalSpeed * 0.0033f) + -20.0f;
                player->actor.gravity = (gFormState.dekuPetalSpeed * 0.00004f) + -1.2f;
            }
        }

        // Prevent OOT fall damage during flight
        player->fallStartHeight = player->actor.world.pos.y;

        // Movement steering (from 2Ship line 19241-19262)
        Player_GetMovementSpeedAndYaw(player, &speedTarget, &yawTarget, SPEED_MODE_LINEAR, play);

        f32 accelRate;
        if (speedTarget == 0.0f) {
            accelRate = 0.1f;
        } else {
            s16 ydiff = player->yaw - yawTarget;
            if (ABS(ydiff) > 0x4000) {
                speedTarget = -speedTarget;
                yawTarget += 0x8000;
            }
            accelRate = 0.25f;
        }

        Math_SmoothStepToS(&gFormState.dekuPitchAngle, (s16)(speedTarget * 600.0f), 8, 0xFA0, 0x64);
        Math_ScaledStepToS(&player->yaw, yawTarget, 0xFA);

        {
            s16 rollTarget = (s16)((s16)(yawTarget - player->yaw) * -2.0f);
            rollTarget = CLAMP(rollTarget, -0x1F40, 0x1F40);
            Math_SmoothStepToS(&gFormState.dekuRollAngle, rollTarget, 0x14, 0x320, 0x14);
        }

        speedTarget =
            (speedTarget * (gFormState.dekuPetalSpeed * 0.0004f)) * fabsf(Math_SinS(gFormState.dekuPitchAngle));
        MmForm_StepToF(&player->linearVelocity, speedTarget, accelRate);

        // Cap total speed to 8.0 (from 2Ship line 19264-19269)
        {
            f32 totalSpeed = sqrtf(SQ(player->linearVelocity) + SQ(player->actor.velocity.y));
            if (totalSpeed > 8.0f) {
                f32 speedScale = 8.0f / totalSpeed;
                player->linearVelocity *= speedScale;
                player->actor.velocity.y *= speedScale;
            }
        }
    }

    // Water hop during flight (from 2Ship func_808378FC, z_player.c:7263-7271)
    if (player->actor.velocity.y < 0.0f && player->actor.yDistToWater > 0.0f && gFormState.dekuHopsRemaining > 0 &&
        gSaveContext.health > 0) {
        gFormState.dekuFlightFlags = 0;
        player->cylinder.dim.radius = (s16)sFormProps[gFormState.currentForm].cylinderRadius;
        player->cylinder.base.atFlags &= ~AT_ON;
        MmForm_DekuWaterHop(player, play);
    }
}

// ---------------------------------------------------------------------------
// MmForm_Action_DekuFallLocked - Post-flight fall with disabled controls
// From 2Ship func_80833AA0 (z_player.c:5732)
// Controls disabled until touching ground or water.
// ---------------------------------------------------------------------------
static void MmForm_Action_DekuFallLocked(Player* player, PlayState* play) {
    gFormState.actionTimer++;

    player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;

    // Keep camera following during fall
    player->stateFlags1 |= PLAYER_STATE1_JUMPING;
    Camera_ChangeMode(GET_ACTIVE_CAM(play), CAM_MODE_FREEFALL);

    player->actor.gravity = -1.2f;
    player->actor.minVelocityY = -20.0f;

    LinkAnimation_Update(play, &gFormState.formSkelAnime);

    player->fallStartHeight = player->actor.world.pos.y;

    // Ground landing
    if (MMFORM_ON_GROUND(player)) {
        player->stateFlags1 &= ~(PLAYER_STATE1_INPUT_DISABLED | PLAYER_STATE1_JUMPING);
        gFormState.dekuFlightFlags = 0;
        Camera_ChangeMode(GET_ACTIVE_CAM(play), CAM_MODE_NORMAL);

        EffectSsHahen_SpawnBurst(play, &player->bodyPartsPos[PLAYER_BODYPART_L_HAND], 2.0f, 0, 4, 4, 2, -1, 10, NULL);
        EffectSsHahen_SpawnBurst(play, &player->bodyPartsPos[PLAYER_BODYPART_R_HAND], 2.0f, 0, 4, 4, 2, -1, 10, NULL);

        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }

    // Water → hop or void
    if (player->actor.yDistToWater > 0.0f) {
        player->stateFlags1 &= ~(PLAYER_STATE1_INPUT_DISABLED | PLAYER_STATE1_JUMPING);
        gFormState.dekuFlightFlags = 0;
        Camera_ChangeMode(GET_ACTIVE_CAM(play), CAM_MODE_NORMAL);

        if (gFormState.dekuHopsRemaining > 0 && gSaveContext.health > 0) {
            MmForm_DekuWaterHop(player, play);
        } else {
            gFormState.goronAction = MMFORM_ACT_WATER_VOID;
            gFormState.actionTimer = 0;
            gFormState.rollGroundPoundTimer = 0;
        }
    }
}

// ===========================================================================
// Zora Electric Barrier (from 2Ship func_8082F164/func_8082F1AC, z_player.c:2922-2981)
//
// R button activates barrier:
//   - Drains magic (MAGIC_CONSUME_GORON_ZORA equivalent)
//   - Intensity ramps 0-255 (Math_StepToS ±50/frame)
//   - Point light orbits player (sin/cos oscillation)
//   - Damage cylinder (AT type, radius 60)
//   - SFX: NA_SE_PL_ZORA_SPARK_BARRIER (looping)
//   - Draw: barrier DL scaled by intensity, vertex alpha modification
// ===========================================================================

// Barrier collider init data (from 2Ship Player_SetCylinderForAttack for ZORA_BARRIER)
static ColliderCylinderInit sBarrierColliderInit = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_PLAYER,
        AC_NONE,
        OC1_NONE,
        OC2_TYPE_PLAYER,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK2,
        { 0x00080000 | DMG_SLASH_KOKIRI | DMG_SPIN_KOKIRI | DMG_JUMP_KOKIRI, 0x00,
          0x02 }, // dmgFlags + Kokiri sword flags, damage=2
        { 0xF7CFFFFF, 0x00, 0x00 },
        TOUCH_ON | TOUCH_NEAREST | TOUCH_SFX_NORMAL,
        BUMP_NONE,
        OCELEM_NONE,
    },
    { 50, 80, 0, { 0, 0, 0 } }, // radius=50, height=80 (from 2Ship line 12769-12770)
};

static void MmForm_InitBarrierCollider(Player* player, PlayState* play) {
    if (!gFormState.barrierColliderInit) {
        Collider_InitCylinder(play, &gFormState.barrierCollider);
        Collider_SetCylinder(play, &gFormState.barrierCollider, &player->actor, &sBarrierColliderInit);
        gFormState.barrierColliderInit = 1;
    }
}

// Check barrier input - flag-based (from 2Ship func_8082F164, z_player.c:2923)
// Called from idle/walk/run/swim actions. Sets barrierActive flag without changing action.
// In MM, barrier is a flag (PLAYER_STATE1_10), not a separate action.
// Barrier input (from 2Ship func_8082F164, z_player.c:2923)
// Ground: R+B = barrier (from Player_Action_18 line 14914: func_8082F164(this, BTN_R | BTN_B))
// Water:  R   = barrier (from swim actions: func_8082F164(this, BTN_R))
static void MmForm_CheckBarrierInput(Player* player, PlayState* play) {
    if (gFormState.currentForm != MM_PLAYER_FORM_ZORA)
        return;

    Input* input = &play->state.input[0];

    // Determine required button combo based on context
    u16 barrierButtons;
    if (gFormState.swimState != 0) {
        barrierButtons = BTN_R; // Water: R only (from 2Ship swim actions)
    } else {
        barrierButtons = BTN_R | BTN_B; // Ground: R+B combo (from 2Ship Player_Action_18 line 14914)
    }

    if (CHECK_BTN_ALL(input->cur.button, barrierButtons) && gSaveContext.magic > 0) {
        gFormState.barrierActive = 1;

        // Init collider on first activation
        MmForm_InitBarrierCollider(player, play);

        // Insert point light if not already present
        if (gFormState.barrierLight == NULL) {
            Lights_PointNoGlowSetInfo(&gFormState.barrierLightInfo, (s16)player->actor.world.pos.x,
                                      (s16)player->actor.world.pos.y, (s16)player->actor.world.pos.z, 100, 200, 255,
                                      600);
            gFormState.barrierLight = LightContext_InsertLight(play, &play->lightCtx, &gFormState.barrierLightInfo);
        }
    } else {
        gFormState.barrierActive = 0;
    }
}

// Update barrier every frame - flag-based (from 2Ship func_8082F1AC, z_player.c:2929-2982)
// Runs regardless of current action. Updates intensity, light, damage collider.
// Player can move while barrier is active (not frozen like old action-based approach).
static void MmForm_UpdateBarrier(Player* player, PlayState* play) {
    if (gFormState.currentForm != MM_PLAYER_FORM_ZORA)
        return;

    s16 prevIntensity = gFormState.barrierIntensity;

    // Magic + intensity logic (from 2Ship func_8082F1AC line 2947-2961)
    if ((gSaveContext.magic != 0) && gFormState.barrierActive) {
        // Start magic drain if idle (from 2Ship: Magic_Consume(play, 0, MAGIC_CONSUME_GORON_ZORA))
        // OOT doesn't have GORON_ZORA mode, so manual drain at ~1 per 10 frames
        if (gSaveContext.magicState == MAGIC_STATE_IDLE) {
            if ((play->gameplayFrames % 10) == 0) {
                gSaveContext.magic--;
            }
        }

        // Ramp intensity up, proportional to remaining magic (from 2Ship line 2952-2958)
        s32 targetIntensity;
        f32 temp = 16.0f;
        if (gSaveContext.magic >= 16) {
            targetIntensity = 255;
        } else {
            targetIntensity = (s32)((gSaveContext.magic / temp) * 255.0f);
        }
        Math_StepToS(&gFormState.barrierIntensity, (s16)targetIntensity, 50);
    } else {
        // Fade out (from 2Ship line 2959-2961)
        if (Math_StepToS(&gFormState.barrierIntensity, 0, 50)) {
            // Fully faded — reset magic state if needed (from 2Ship: Magic_Reset)
            if (gSaveContext.magicState != MAGIC_STATE_IDLE) {
                Magic_Reset(play);
            }
        }
    }

    // Remove light when fully faded
    if (gFormState.barrierIntensity == 0 && prevIntensity == 0) {
        if (gFormState.barrierLight != NULL) {
            LightContext_RemoveLight(play, &play->lightCtx, gFormState.barrierLight);
            gFormState.barrierLight = NULL;
        }
        return;
    }

    // Update point light position (from 2Ship: orbiting with sin/cos)
    if (gFormState.barrierLight != NULL) {
        s16 angle1 = play->gameplayFrames * 7000;
        s16 angle2 = play->gameplayFrames * 14000;
        f32 sinA = Math_SinS(angle2) * 40.0f;
        f32 cosA = Math_CosS(angle2) * 40.0f;
        f32 sinB = Math_SinS(angle1) * sinA;
        f32 cosB = Math_CosS(angle1) * sinA;

        Lights_PointNoGlowSetInfo(&gFormState.barrierLightInfo, (s16)(player->actor.world.pos.x + cosA),
                                  (s16)(player->actor.world.pos.y + sinB), (s16)(player->actor.world.pos.z + cosB), 100,
                                  200, 255, 600);
    }

    // Set damage collider (from 2Ship Player_SetCylinderForAttack with DMG_ZORA_BARRIER)
    if (gFormState.barrierIntensity > 0 && gFormState.barrierColliderInit) {
        gFormState.barrierCollider.dim.pos.x = player->actor.world.pos.x;
        gFormState.barrierCollider.dim.pos.y = player->actor.world.pos.y;
        gFormState.barrierCollider.dim.pos.z = player->actor.world.pos.z;
        CollisionCheck_SetAT(play, &play->colChkCtx, &gFormState.barrierCollider.base);
    }

    // Screen tint (from 2Ship func_8082F1AC line 2969: Player_LerpEnvLighting)
    // MM uses sZoraBarrierEnvLighting = { {0,0,0}, {255,255,155}, fogColor={20,20,50}, 940, 5000 }
    // We approximate by blending fog color toward blue based on intensity ratio
    if (gFormState.barrierIntensity > 0) {
        f32 blend = gFormState.barrierIntensity / 255.0f;
        // Blend fog color toward (20, 20, 50) from MM's sZoraBarrierEnvLighting
        play->envCtx.adjFogColor[0] = (s16)(-blend * 20.0f); // Reduce red
        play->envCtx.adjFogColor[1] = (s16)(-blend * 20.0f); // Reduce green
        play->envCtx.adjFogColor[2] = (s16)(blend * 30.0f);  // Push blue
        // Adjust fog near for darker atmosphere (from MM fogNear=940)
        play->envCtx.adjFogNear = (s16)(-blend * 100.0f);
    } else if (prevIntensity > 0) {
        // Reset adjustments when barrier fully off
        play->envCtx.adjFogColor[0] = 0;
        play->envCtx.adjFogColor[1] = 0;
        play->envCtx.adjFogColor[2] = 0;
        play->envCtx.adjFogNear = 0;
    }

    // Looping SFX (from 2Ship: NA_SE_PL_ZORA_SPARK_BARRIER - SFX_FLAG)
    if (gFormState.barrierIntensity > 0) {
        MmForm_PlaySfx(player, MM_NA_SE_PL_ZORA_ELECTRIC_BARRIER, NA_SE_PL_BODY_HIT);
    }
}

// Boot mode toggle (from 2Ship func_8083A04C, z_player.c:8344-8357)
// B = ZORA_UNDERWATER (iron boots, sink), A = ZORA_LAND (free swim)
// Called from swim actions only.
static void MmForm_CheckBootToggle(Player* player, PlayState* play) {
    if (gFormState.currentForm != MM_PLAYER_FORM_ZORA)
        return;
    if (gFormState.swimState == 0)
        return;

    Input* input = &play->state.input[0];
    if (gFormState.zoraBoots == 1) { // UNDERWATER
        if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
            gFormState.zoraBoots = 0; // → LAND (free swim)
        }
        // Set dive delay when in swim idle (from 2Ship func_8083A04C line 8349-8351)
        if (gFormState.goronAction == MMFORM_ACT_SWIM_IDLE) {
            gFormState.bootToggleDelay = 20;
        }
    } else { // LAND
        if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
            gFormState.zoraBoots = 1; // → UNDERWATER (iron boots, sink)
            // Set dive delay (from 2Ship: av2 = 20 during Action_54)
            if (gFormState.goronAction == MMFORM_ACT_SWIM_IDLE) {
                gFormState.bootToggleDelay = 20;
            }
        }
    }
}

// ===========================================================================
// Zora Boomerang Fins (from 2Ship Player_UpperAction_12-16, z_player.c:14024-14134)
//
// Uses OOT's native ACTOR_EN_BOOM for projectile mechanics (collision, return,
// item pickup). Spawns TWO boomerangs (left and right fin) like MM.
//
// Flow (from 2Ship):
//   B hold → aiming mode (pz_cutterwaitanim loop)
//   B release → throw (pz_cutterattack, spawn 2 En_Boom at frame 6)
//   Wait → (pz_cutterwaitanim loop until both return)
//   Catch → (pz_cuttercatch) → idle
//
// MM spawns from hand bone positions with spread angles:
//   With lock-on:  left = rot.y + 0x36B0 (~30deg), right = rot.y - 0x36B0
//   Without:       left = rot.y - 0x190,            right = rot.y + 0x190
// ===========================================================================

// Helper: check if a spawned boomerang actor is still alive
static u8 MmForm_IsBoomerangAlive(Actor* boom) {
    return (boom != NULL && boom->update != NULL);
}

// Entry: B hold → aiming mode with cutterwaitanim loop
static void MmForm_StartBoomerangThrow(Player* player, PlayState* play) {
    if (gFormState.boomerangState != 0)
        return;

    // Play aiming wait animation (from 2Ship Player_UpperAction_12 → 13: cutterwaitanim loop)
    LinkAnimationHeader* aimAnim = gFormState.cutterWaitAnim;
    if (aimAnim == NULL)
        aimAnim = gFormState.idleAnim;

    MmForm_SetAction(MMFORM_ACT_BOOMERANG_THROW, play, aimAnim, 1.0f, ANIMMODE_LOOP);
    player->linearVelocity = 0.0f;

    gFormState.boomerangState = 1; // aiming
    gFormState.boomerangTimer = 0;
    gFormState.boomerangActorL = NULL;
    gFormState.boomerangActorR = NULL;

    // Initialize aim angles (from MM Player_Action_81 / func_80847190)
    gFormState.boomerangAimYaw = 0;
    gFormState.boomerangAimPitch = 0;

    // Save current facing direction — forced every frame during aim/throw so OOT can't rotate body
    gFormState.boomerangLockedYaw = player->actor.shape.rot.y;

    // Enter Z-target mode (from MM: boomerang aim = Z-targeting behavior)
    // This prevents body rotation and enables strafe/jump attack during flight.
    player->stateFlags1 |= PLAYER_STATE1_PARALLEL;

    // Camera: OOT's boomerang aiming camera (close behind player).
    // CAM_MODE_BOWARROW would be closer to first-person but isn't in all camera settings'
    // validModes, causing the camera to revert to NORMAL and kick the player out.
    // CAM_MODE_BOOMERANG is universally valid and provides a behind-the-shoulder view.
    Camera* cam = Play_GetCamera(play, 0);
    Camera_ChangeMode(cam, CAM_MODE_BOOMERANG);
}

// BOOMERANG_THROW action: aiming (state 1) then throwing (state 2)
static void MmForm_Action_BoomerangThrow(Player* player, PlayState* play) {
    LinkAnimation_Update(play, &gFormState.formSkelAnime);
    Input* input = &play->state.input[0];

    if (gFormState.boomerangState == 1) {
        // === AIMING PHASE: holding B, loop cutterwaitanim ===
        // From MM Player_Action_81 (z_player.c:18249) + func_80847190 (z_player.c:13261):
        //   Stick Y → pitch (aimPitch), Stick X → yaw (aimYaw)
        //   Upper body rotates to follow aim direction via upperLimbRot
        //   Body facing is LOCKED — Z-target mode (PARALLEL) + forced yaw override
        player->linearVelocity = 0.0f;
        player->stateFlags1 |= PLAYER_STATE1_PARALLEL;

        // CRITICAL: Set FIRST_PERSON every frame to prevent Player_UpdateCamAndSeqModes
        // (z_player.c:11714) from overwriting our camera mode. That function checks
        // PLAYER_STATE1_FIRST_PERSON and if set, skips the entire camera mode if-else chain.
        // Also re-assert CAM_MODE_BOOMERANG every frame (pseudo first-person behind shoulder).
        player->stateFlags1 |= PLAYER_STATE1_FIRST_PERSON;
        Camera_ChangeMode(Play_GetCamera(play, 0), CAM_MODE_BOOMERANG);
        // Force body facing every frame — OOT's actionFunc + Player_UpdateShapeYaw run before us,
        // so we override whatever rotation they applied. This guarantees zero body rotation during aim.
        player->actor.shape.rot.y = gFormState.boomerangLockedYaw;

        // Update aim from stick input (from MM func_80847190, z_player.c:13261-13296)
        // Pitch: stick_y * 0xF0 → smooth step to target
        {
            s16 pitchTarget = input->rel.stick_y * 0xF0;
            Math_SmoothStepToS(&gFormState.boomerangAimPitch, pitchTarget, 14, 0xFA0, 0x1E);
            // Clamp pitch (from MM: -0x36B0 to 0x36B0 ≈ ±73 degrees)
            gFormState.boomerangAimPitch = CLAMP(gFormState.boomerangAimPitch, -0x36B0, 0x36B0);
        }

        // Yaw: stick_x * -0x10 per frame (clamped increment), accumulates
        {
            s16 yawDelta = input->rel.stick_x * -0x10;
            yawDelta = CLAMP(yawDelta, -0xBB8, 0xBB8);
            gFormState.boomerangAimYaw += yawDelta;
            // Clamp total yaw (from MM: ±0x4AAA ≈ ±105 degrees)
            gFormState.boomerangAimYaw = CLAMP(gFormState.boomerangAimYaw, -0x4AAA, 0x4AAA);
        }

        // Apply aim to upper body rotation (from MM z_player_lib.c:2493-2516)
        // upperLimbRot.y = aim yaw relative to body facing (full value — yaw looks correct)
        // upperLimbRot.x = aim pitch (REDUCED for visual — full pitch used for spawn direction)
        // In MM, Player_UpdateUpperBody maps focus.rot to upperLimbRot with damping.
        // The upper body should only tilt subtly (±30°), not rotate fully.
        // Full aimPitch (±0x36B0) is kept in gFormState for boomerang spawn direction.
        player->upperLimbRot.y = gFormState.boomerangAimYaw;
        {
            // Scale pitch to ±0x1555 (~30°) for upper body visual
            s16 visualPitch = gFormState.boomerangAimPitch * 3 / 8;
            visualPitch = CLAMP(visualPitch, -0x1555, 0x1555);
            player->upperLimbRot.x = visualPitch;
        }

        // B released → transition to throw
        if (!CHECK_BTN_ALL(input->cur.button, BTN_B)) {
            // Exit first-person lock (let Player_UpdateCamAndSeqModes control camera again)
            player->stateFlags1 &= ~PLAYER_STATE1_FIRST_PERSON;

            if (gFormState.cutterAttack != NULL) {
                // Switch to throw animation
                LinkAnimation_Change(play, &gFormState.formSkelAnime, gFormState.cutterAttack, 1.0f, 0.0f,
                                     Animation_GetLastFrame(gFormState.cutterAttack), ANIMMODE_ONCE, -4.0f);
                gFormState.boomerangState = 2; // throwing
                gFormState.boomerangTimer = 0;

                // Keep rotation locked during throw anim, switch camera to follow-boomerang
                Camera* cam = Play_GetCamera(play, 0);
                Camera_ChangeMode(cam, CAM_MODE_FOLLOWBOOMERANG);
            } else {
                // No throw animation available, cancel
                gFormState.boomerangState = 0;
                MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
                // Reset camera
                Camera* cam = Play_GetCamera(play, 0);
                Camera_ChangeMode(cam, CAM_MODE_NORMAL);
                // Clear upper body rotation
                player->upperLimbRot.y = 0;
                player->upperLimbRot.x = 0;
            }
        }
    } else if (gFormState.boomerangState == 2) {
        // === THROWING PHASE: play cutterattack, spawn boomerangs at frame 6 ===
        // Keep body locked during throw animation (don't turn mid-throw)
        player->linearVelocity = 0.0f;
        player->stateFlags1 |= PLAYER_STATE1_PARALLEL;
        player->actor.shape.rot.y = gFormState.boomerangLockedYaw;
        gFormState.boomerangTimer++;

        // After throw anim finishes → force spawn if not yet done, then transition to wait
        f32 curFrame = gFormState.formSkelAnime.curFrame;
        f32 endFrame = Animation_GetLastFrame(gFormState.formSkelAnime.animation);
        u8 animDone = (curFrame >= endFrame - 0.5f);

        // If animation ends before frame 6, force timer forward so spawn happens now
        if (animDone && gFormState.boomerangTimer < 6) {
            gFormState.boomerangTimer = 6;
        }

        // From 2Ship Player_UpperAction_14: spawn at frame 6 (>= for safety)
        if (gFormState.boomerangTimer >= 6 && gFormState.boomerangState == 2) {
            // Use aim direction: body facing + aim yaw offset from aiming phase
            s16 rotY = player->actor.shape.rot.y + gFormState.boomerangAimYaw;
            s16 pitchX = gFormState.boomerangAimPitch;
            f32 posY = player->actor.world.pos.y + 50.0f;
            u8 hasTarget = (player->focusActor != NULL);

            // LEFT BOOMERANG (from 2Ship: left hand, spread angle)
            f32 posLX = player->actor.world.pos.x + Math_SinS(rotY - 0x2000) * 5.0f;
            f32 posLZ = player->actor.world.pos.z + Math_CosS(rotY - 0x2000) * 5.0f;
            s16 yawL = hasTarget ? (rotY + 0x36B0) : (rotY - 0x190);

            EnBoom* leftBoom = (EnBoom*)Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOOM, posLX, posY, posLZ, pitchX,
                                                    yawL, 0, 0, true);

            if (leftBoom != NULL) {
                // From MM: unk_1CC = unk_1CF(16) + 0x24(36) = 52 frames flight time
                leftBoom->returnTimer = 52;
                leftBoom->moveTo = player->focusActor;
                gFormState.boomerangActorL = &leftBoom->actor;
            }

            // RIGHT BOOMERANG (from 2Ship: right hand, opposite spread)
            f32 posRX = player->actor.world.pos.x + Math_SinS(rotY + 0x2000) * 5.0f;
            f32 posRZ = player->actor.world.pos.z + Math_CosS(rotY + 0x2000) * 5.0f;
            s16 yawR = hasTarget ? (rotY - 0x36B0) : (rotY + 0x190);

            EnBoom* rightBoom = (EnBoom*)Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOOM, posRX, posY, posRZ, pitchX,
                                                     yawR, 0, 0, true);

            if (rightBoom != NULL) {
                rightBoom->returnTimer = 52;
                rightBoom->moveTo = player->focusActor;
                gFormState.boomerangActorR = &rightBoom->actor;

                // Link left-right as parent-child like MM does
                if (leftBoom != NULL) {
                    leftBoom->actor.child = &rightBoom->actor;
                    rightBoom->actor.parent = &leftBoom->actor;
                }
            }

            // Set OOT state flag so En_Boom knows to return to player
            player->stateFlags1 |= PLAYER_STATE1_BOOMERANG_THROWN;
            // Point OOT's boomerangActor at left fin (for quick recall compatibility)
            if (leftBoom != NULL) {
                player->boomerangActor = &leftBoom->actor;
            }

            gFormState.boomerangState = 3; // thrown

            MmForm_PlaySfx(player, MM_NA_SE_PL_ZORA_BOOMERANG_THROW, NA_SE_IT_BOOMERANG_THROW);
            MmForm_PlayAttackVoice(player);
        }

        // Animation done and boomerangs spawned → return to idle (free movement)
        // In MM, boomerang is an UpperAction — lower body stays free to walk/run/jump attack.
        // We emulate this by returning to idle and tracking boomerangs in background.
        if (animDone && gFormState.boomerangState == 3) {
            // Clear aim rotation — no longer aiming
            player->upperLimbRot.y = 0;
            player->upperLimbRot.x = 0;
            gFormState.boomerangAimYaw = 0;
            gFormState.boomerangAimPitch = 0;
            gFormState.boomerangTimer = 0; // reset for timeout tracking in background

            // Enter Z-target idle — player can strafe, jump attack with A+forward, punch with B
            // From MM: Player_UpperAction_14 calls Player_SetParallel, making lower body free to act
            // in Z-target mode while upper body tracks boomerangs.
            player->stateFlags1 |= PLAYER_STATE1_PARALLEL;
            LinkAnimationHeader* ztAnim = gFormState.ztargetIdleR ? gFormState.ztargetIdleR : gFormState.idleAnim;
            MmForm_SetAction(MMFORM_ACT_ZTARGET_IDLE, play, ztAnim, 1.0f, ANIMMODE_LOOP);
        }
    }
}

// Background boomerang tracking — runs every frame from MmForm_UpdateActive
// when boomerangState == 3 (boomerangs in flight, player moves freely).
// In MM, this is Player_UpperAction_15 which returns false to let the lower body
// continue whatever action it's in (walk, run, jump attack, etc.).
static void MmForm_TrackBoomerangsInFlight(Player* player, PlayState* play) {
    if (gFormState.boomerangState != 3)
        return;

    // Check if both boomerangs have returned (actor killed = update is NULL)
    u8 leftDone = !MmForm_IsBoomerangAlive(gFormState.boomerangActorL);
    u8 rightDone = !MmForm_IsBoomerangAlive(gFormState.boomerangActorR);

    if (leftDone && rightDone) {
        // Both caught! Clean up state (no animation interruption — player keeps doing what they're doing)
        // In MM, Player_UpperAction_15 transitions to Player_UpperAction_16 (catch anim) but that's
        // upper-body only. Since we don't have upper/lower split, just play the SFX and reset state.
        gFormState.boomerangActorL = NULL;
        gFormState.boomerangActorR = NULL;
        player->stateFlags1 &= ~PLAYER_STATE1_BOOMERANG_THROWN;
        player->boomerangActor = NULL;
        gFormState.boomerangState = 0;

        // Clear forced PARALLEL if player isn't actually Z-targeting with button.
        // OOT re-evaluates PARALLEL each frame when Z is held, so safe to clear.
        if (!(player->stateFlags1 &
              (PLAYER_STATE1_HOSTILE_LOCK_ON | PLAYER_STATE1_Z_TARGETING | PLAYER_STATE1_FRIENDLY_ACTOR_FOCUS))) {
            player->stateFlags1 &= ~PLAYER_STATE1_PARALLEL;
        }

        MmForm_PlaySfx(player, MM_NA_SE_PL_ZORA_BOOMERANG_CATCH, NA_SE_PL_CATCH_BOOMERANG);
        return;
    }

    // Safety timeout: if boomerangs stuck for too long, force cleanup
    gFormState.boomerangTimer++;
    if (gFormState.boomerangTimer > 180) {
        if (MmForm_IsBoomerangAlive(gFormState.boomerangActorL)) {
            Actor_Kill(gFormState.boomerangActorL);
        }
        if (MmForm_IsBoomerangAlive(gFormState.boomerangActorR)) {
            Actor_Kill(gFormState.boomerangActorR);
        }
        gFormState.boomerangActorL = NULL;
        gFormState.boomerangActorR = NULL;
        player->stateFlags1 &= ~PLAYER_STATE1_BOOMERANG_THROWN;
        player->boomerangActor = NULL;
        gFormState.boomerangState = 0;
        if (!(player->stateFlags1 &
              (PLAYER_STATE1_HOSTILE_LOCK_ON | PLAYER_STATE1_Z_TARGETING | PLAYER_STATE1_FRIENDLY_ACTOR_FOCUS))) {
            player->stateFlags1 &= ~PLAYER_STATE1_PARALLEL;
        }
    }
}

// NOTE: MmForm_Action_BoomerangCatch removed — boomerang return is now handled
// non-blockingly by MmForm_TrackBoomerangsInFlight() in the background.
// Player continues whatever action they're in (walk, fight, etc.) when fins return.

// ===========================================================================
// Zora Swimming (from 2Ship Player_Action_54-58, z_player.c:16820-17072)
//
// Water entry: yDistToWater > threshold → enter swim
// Surface: idle float (link_swimer_swim_wait)
// Movement: stick → surface swim
// Fast swim: A held → pz_fishswim (body pitch from stick Y)
// Dash: A press → pz_waterroll (speed burst)
// Exit: touch ground while swimming → land
// Draw: root limb pitch/roll override
// ===========================================================================

// Swim visual effects (from 2Ship z_player.c:9090-9156)
// Bubbles when submerged, ripples at surface, splash on entry.
static void MmForm_SwimEffects(Player* player, PlayState* play) {
    if (player->actor.yDistToWater < 20.0f)
        return;

    // Ripples at water surface (from 2Ship: EffectSsGRipple_Spawn every ~15 units of movement)
    Vec3f ripplePos = { player->actor.world.pos.x, player->actor.world.pos.y + player->actor.yDistToWater,
                        player->actor.world.pos.z };

    if ((gFormState.actionTimer & 7) == 0 && fabsf(player->linearVelocity) > 0.5f) {
        EffectSsGRipple_Spawn(play, &ripplePos, 100, 500, 0);
    }

    // Bubbles when deeply submerged (from 2Ship z_player.c:9144-9152)
    if (player->actor.yDistToWater > ZORA_DEEP_THRESHOLD) {
        s32 bubbleCount = 0;
        if (gFormState.fastSwimActive) {
            // Zora-specific: based on roll rate + speed (from 2Ship line 9144-9146)
            f32 factor = (ABS(gFormState.swimYawRate) * 0.004f) + (gFormState.swimSpeedB48 * 0.38f);
            bubbleCount = (s32)factor;
            if (bubbleCount == 0 && (Rand_ZeroOne() < 0.2f))
                bubbleCount = 1;
        } else {
            // Normal: based on downward velocity (from 2Ship line 9149-9151)
            if (player->actor.velocity.y < 0.0f) {
                bubbleCount = (s32)(player->actor.velocity.y * -0.3f);
            }
            if (bubbleCount == 0 && (Rand_ZeroOne() < 0.1f))
                bubbleCount = 1;
        }
        if (bubbleCount > 8)
            bubbleCount = 8;

        Vec3f bubblePos = player->actor.world.pos;
        bubblePos.y += 20.0f;
        for (s32 i = 0; i < bubbleCount; i++) {
            EffectSsBubble_Spawn(play, &bubblePos, 20.0f, 10.0f, 20.0f, 0.13f);
        }
    }

    // Splash on water entry (from 2Ship z_player.c:9109)
    if (gFormState.actionTimer == 1 && gFormState.swimState == 1) {
        s16 splashScale = (s16)(fabsf(player->linearVelocity) * 50.0f + player->actor.yDistToWater * 5.0f);
        if (splashScale > 500)
            splashScale = 500;
        s16 splashType = (fabsf(player->linearVelocity) > 10.0f) ? 1 : 0;
        EffectSsGSplash_Spawn(play, &ripplePos, NULL, NULL, splashType, splashScale);
    }
}

// Buoyancy physics (from 2Ship func_808475B4, z_player.c:13317-13356)
// Handles: surface float, iron boots sink, deep water drag, terminal velocity.
// Called every frame from all swim actions instead of ad-hoc velocity assignments.
static void MmForm_WaterBuoyancy(Player* player) {
    f32 sp4;
    f32 var_ft4 = -5.0f;
    f32 buoyancyDepth = ZORA_BUOYANCY_DEPTH;

    f32 depthDelta = player->actor.yDistToWater - buoyancyDepth;
    if (player->actor.velocity.y < 0.0f) {
        buoyancyDepth += 1.0f;
    }

    if (player->actor.yDistToWater < buoyancyDepth) {
        // NEAR SURFACE — push down gently to keep at surface level
        // (from 2Ship line 13331-13332)
        f32 clamped = depthDelta;
        if (clamped < -0.4f)
            clamped = -0.4f;
        if (clamped > -0.1f)
            clamped = -0.1f;
        sp4 = clamped - ((player->actor.velocity.y <= 0.0f) ? 0.0f : player->actor.velocity.y * 0.5f);
    } else {
        // DEEP UNDERWATER
        if (!(player->stateFlags1 & PLAYER_STATE1_DEAD) && (gFormState.zoraBoots == 1) &&
            (player->actor.velocity.y >= -5.0f)) {
            // IRON BOOTS: constant sink at -0.3, terminal velocity -5.0
            // (from 2Ship line 13334-13336)
            sp4 = -0.3f;
        } else {
            // NORMAL BUOYANCY: push up with drag
            // (from 2Ship line 13340-13343)
            var_ft4 = 2.0f;
            f32 upForce = depthDelta;
            if (upForce < 0.1f)
                upForce = 0.1f;
            if (upForce > 0.4f)
                upForce = 0.4f;
            sp4 = ((player->actor.velocity.y >= 0.0f) ? 0.0f : player->actor.velocity.y * -0.3f) + upForce;
        }

        // Mark as submerged when deep enough (from 2Ship line 13346-13348)
        if (player->actor.yDistToWater > 100.0f) {
            player->stateFlags2 |= PLAYER_STATE2_UNDERWATER;
        }
    }

    // Apply buoyancy force and clamp to terminal velocity
    // (from 2Ship line 13351-13354)
    player->actor.velocity.y += sp4;
    if (((player->actor.velocity.y - var_ft4) * sp4) > 0.0f) {
        player->actor.velocity.y = var_ft4;
    }
    player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_SWIM);
}

static void MmForm_EnterSwimIdle(Player* player, PlayState* play) {
    MmForm_SetAction(MMFORM_ACT_SWIM_IDLE, play,
                     gFormState.swimWaitAnim ? gFormState.swimWaitAnim : gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
    gFormState.swimState = 1;
    gFormState.swimPitch = 0;
    gFormState.swimRoll = 0;
    gFormState.swimSpeed = 0.0f;
    gFormState.swimDashTimer = 0;
    gFormState.zoraBoots = 0; // Default to ZORA_LAND (free swim)
    gFormState.fastSwimActive = 0;
    gFormState.swimRollSmoothed = 0;
    // New 3-phase fast swim fields
    gFormState.swimPhase = 0;
    gFormState.swimPhaseCounter = 0;
    gFormState.swimSpeedB48 = 0.0f;
    gFormState.swimYawRate = 0;
    gFormState.swimExitFlag = 0;
    gFormState.swimFloorTimer = 0;
    gFormState.bootToggleDelay = 0;

    // Clean up stale state from land actions that may have been interrupted
    player->actor.shape.rot.x = 0; // Clear pitch rotation from floor/roll

    // CRITICAL: Prevent OOT's ground actionFunc from interfering with swim.
    // OOT's Player_UpdateCommon runs BEFORE our code:
    //   1. actionFunc → sets yaw/linearVelocity from stick (ground movement)
    //   2. speedXZ = linearVelocity, world.rot.y = yaw
    //   3. Actor_UpdateVelocityXZGravity → moves player in OOT's direction
    //   4. Player_UpdateShapeYaw → interpolates shape.rot.y toward yaw
    // All of this happens BEFORE TransformMasks_Update. To prevent interference:
    //   - PAUSE_ACTION_FUNC: blocks OOT's actionFunc from running next frame
    //     (Player_UpdateCommon clears it at the end, we re-set it each frame)
    //   - DISABLE_ROTATION_ALWAYS: blocks Player_UpdateShapeYaw from modifying shape.rot.y
    player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
    player->stateFlags2 |= PLAYER_STATE2_DISABLE_ROTATION_ALWAYS;
    player->stateFlags2 &= ~PLAYER_STATE2_DISABLE_ROTATION_Z_TARGET;

    // Gravity handled by MmForm_WaterBuoyancy (called from swim actions)
    player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_SWIM);
}

// Forward declaration — defined after SwimMove, used by SwimIdle/SurfaceWalk/UnderwaterWalk
static void MmForm_SwimMovement(Player* player, f32 speedTarget, s16 yawTarget);

// Surface idle (from 2Ship Player_Action_54, z_player.c:16820)
// Handles boot toggle (B→iron boots, A→free swim), buoyancy/sinking, and action transitions.
static void MmForm_Action_SwimIdle(Player* player, PlayState* play) {
    LinkAnimation_Update(play, &gFormState.formSkelAnime);
    Input* input = &play->state.input[0];

    // Visual effects (ripples, bubbles)
    MmForm_SwimEffects(player, play);

    // Barrier input (flag-based, from 2Ship func_8082F164 called in Action_54)
    MmForm_CheckBarrierInput(player, play);

    // Boot mode toggle (from 2Ship func_8083A04C)
    MmForm_CheckBootToggle(player, play);

    // Buoyancy (from 2Ship func_808475B4 — real MM physics)
    MmForm_WaterBuoyancy(player);

    // Boot toggle delay countdown
    if (gFormState.bootToggleDelay > 0) {
        gFormState.bootToggleDelay--;
    }

    if (gFormState.zoraBoots == 1) { // DIVE MODE (sinking)
        // When we hit the ocean floor → switch to normal land controls (idle, walk, Z-target, attacks).
        // Same controls as surface, but underwater. A = surface (handled in main update).
        // Unlike SWIM_UNDERWATER_WALK, this avoids oscillation and gives full land controls.
        if (MMFORM_ON_GROUND(player)) {
            player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
            // Restore OOT control for land controls on floor
            player->stateFlags2 &= ~PLAYER_STATE2_DISABLE_ROTATION_ALWAYS;
            player->stateFlags3 &= ~PLAYER_STATE3_PAUSE_ACTION_FUNC;
            MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
            // Keep swimState = 1 so we know we're still underwater
            MmForm_PlaySfx(player, MM_NA_SE_PL_LAND, NA_SE_PL_LAND);
            return;
        }
    } else { // ZORA_LAND (free swim)
        // A hold → dive into fast swim (from 2Ship func_80850734, line 16794-16808)
        // Condition: Zora, A cur (hold), no wind, LAND boots, no boot delay
        if (CHECK_BTN_ALL(input->cur.button, BTN_A) && gFormState.bootToggleDelay == 0) {
            if (gFormState.waterRoll != NULL) {
                // Start waterroll from frame 4 (from 2Ship line 16799: startFrame=4.0f)
                MmForm_SetAction(MMFORM_ACT_SWIM_FAST, play, gFormState.waterRoll, 1.0f, ANIMMODE_ONCE);
                LinkAnimation_Change(play, &gFormState.formSkelAnime, gFormState.waterRoll, 1.0f, 4.0f,
                                     Animation_GetLastFrame(gFormState.waterRoll), ANIMMODE_ONCE, -6.0f);
                gFormState.swimPhase = 0;                         // Phase 0: waterroll transition
                gFormState.swimPhaseCounter = 1;                  // Play waterroll once then transition
                gFormState.swimExitFlag = 0;                      // Not exiting (from 2Ship line 16802)
                gFormState.swimSpeedB48 = player->linearVelocity; // Save current speed (from 2Ship line 16803)
                player->actor.velocity.y = 0.0f;                  // (from 2Ship line 16804)
                gFormState.fastSwimActive = 1;                    // Visible from first frame
                gFormState.swimRoll = 0;                          // Reset roll for clean 2-rotation barrel roll
                gFormState.swimRollSmoothed = 0;
                MmForm_PlaySfx(player, MM_NA_SE_PL_ZORA_SWIM, NA_SE_PL_DIVE_BUBBLE);
                return;
            }
        }
    }

    // Exit water: on ground + above water surface → land
    if (player->actor.yDistToWater <= 0.0f && MMFORM_ON_GROUND(player)) {
        gFormState.swimState = 0;
        gFormState.zoraBoots = 0;
        gFormState.fastSwimActive = 0;
        player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
        player->stateFlags2 &= ~PLAYER_STATE2_DISABLE_ROTATION_ALWAYS;
        player->stateFlags3 &= ~PLAYER_STATE3_PAUSE_ACTION_FUNC;
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        MmForm_PlaySfx(player, MM_NA_SE_PL_LAND, NA_SE_PL_LAND);
        return;
    }

    // Movement from stick (from 2Ship Player_Action_54: Player_GetMovementSpeedAndYaw + func_8084748C)
    f32 speedTarget = 0.0f;
    s16 yawTarget = player->actor.shape.rot.y;

    Player_GetMovementSpeedAndYaw(player, &speedTarget, &yawTarget, SPEED_MODE_LINEAR, play);

    if (speedTarget != 0.0f) {
        // U-turn deceleration (from 2Ship line 16860-16862)
        // If angle > 90°, decelerate first before turning
        s16 yawDiff = (s16)(player->actor.shape.rot.y - yawTarget);
        if ((ABS(yawDiff) > 0x6000) && !Math_StepToF(&player->linearVelocity, 0.0f, 1.0f)) {
            MmForm_SwimMovement(player, 0.0f, yawTarget);
            return;
        }

        // Transition to surface walk (from 2Ship func_8083B73C)
        player->actor.shape.rot.y = yawTarget;
        player->yaw = yawTarget;
        player->actor.world.rot.y = yawTarget;
        MmForm_SetAction(MMFORM_ACT_SWIM_SURFACE_WALK, play,
                         gFormState.swimAnim ? gFormState.swimAnim : gFormState.walkAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }

    // Always update yaw + speed even when idle (from 2Ship: func_8084748C always called)
    MmForm_SwimMovement(player, speedTarget, yawTarget);
}

// Surface walk (from 2Ship Player_Action_57, z_player.c:17041-17072)
// Swimming on surface with stick-based movement. Transitions to fast swim on A.
static void MmForm_Action_SwimSurfaceWalk(Player* player, PlayState* play) {
    LinkAnimation_Update(play, &gFormState.formSkelAnime);
    Input* input = &play->state.input[0];

    // Visual effects (ripples, bubbles)
    MmForm_SwimEffects(player, play);

    // Buoyancy (from 2Ship func_808475B4)
    MmForm_WaterBuoyancy(player);

    // Barrier input (flag-based)
    MmForm_CheckBarrierInput(player, play);

    // Boot mode toggle
    MmForm_CheckBootToggle(player, play);

    // If boots switched to UNDERWATER → return to idle (will sink from there)
    // (from 2Ship Player_Action_57 line 17060: currentBoots >= UNDERWATER → func_808353DC)
    if (gFormState.zoraBoots == 1) {
        MmForm_SetAction(MMFORM_ACT_SWIM_IDLE, play,
                         gFormState.swimWaitAnim ? gFormState.swimWaitAnim : gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }

    // A hold → dive into fast swim (from 2Ship func_80850734, line 16794)
    if (CHECK_BTN_ALL(input->cur.button, BTN_A)) {
        if (gFormState.waterRoll != NULL) {
            // Start waterroll from frame 4, save current speed
            MmForm_SetAction(MMFORM_ACT_SWIM_FAST, play, gFormState.waterRoll, 1.0f, ANIMMODE_ONCE);
            LinkAnimation_Change(play, &gFormState.formSkelAnime, gFormState.waterRoll, 1.0f, 4.0f,
                                 Animation_GetLastFrame(gFormState.waterRoll), ANIMMODE_ONCE, -6.0f);
            gFormState.swimPhase = 0;
            gFormState.swimPhaseCounter = 5;
            gFormState.swimExitFlag = 0;
            gFormState.swimSpeedB48 = player->linearVelocity;
            player->actor.velocity.y = 0.0f;
            MmForm_PlaySfx(player, MM_NA_SE_PL_ZORA_SWIM, NA_SE_PL_DIVE_BUBBLE);
            return;
        }
    }

    // Exit water
    if (player->actor.yDistToWater <= 0.0f && MMFORM_ON_GROUND(player)) {
        gFormState.swimState = 0;
        gFormState.zoraBoots = 0;
        gFormState.fastSwimActive = 0;
        player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        MmForm_PlaySfx(player, MM_NA_SE_PL_LAND, NA_SE_PL_LAND);
        return;
    }

    // Movement from stick (from 2Ship Player_Action_57: Player_GetMovementSpeedAndYaw + func_80847FF8)
    f32 speedTarget = 0.0f;
    s16 yawTarget = player->actor.shape.rot.y;

    Player_GetMovementSpeedAndYaw(player, &speedTarget, &yawTarget, SPEED_MODE_LINEAR, play);

    // Check dive initiation first (from 2Ship: func_80850734 in Action_57)
    // Already handled above (A hold → dive)

    // Return to idle on no input or U-turn (from 2Ship line 17064-17066)
    s16 yawDiff = (s16)(player->actor.shape.rot.y - yawTarget);
    if ((speedTarget == 0.0f) || (ABS(yawDiff) > 0x6000)) {
        MmForm_SetAction(MMFORM_ACT_SWIM_IDLE, play,
                         gFormState.swimWaitAnim ? gFormState.swimWaitAnim : gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
    }

    // Always update speed + yaw (from 2Ship func_80847FF8 → func_8084748C)
    MmForm_SwimMovement(player, speedTarget, yawTarget);
}

// Legacy name redirect for action dispatch
static void MmForm_Action_SwimMove(Player* player, PlayState* play) {
    MmForm_Action_SwimSurfaceWalk(player, play);
}

// Swim speed + yaw update (from 2Ship func_8084748C, z_player.c:13298-13315)
// Animation-frame-based speed stepping + smooth yaw turning at 0x640 rate.
// Used by SwimIdle, SurfaceWalk, UnderwaterWalk — matches MM's real swim feel.
static void MmForm_SwimMovement(Player* player, f32 speedTarget, s16 yawTarget) {
    f32 incrStep = gFormState.formSkelAnime.curFrame - 10.0f;
    f32 maxSpeed = (R_RUN_SPEED_LIMIT / 100.0f);

    if (player->linearVelocity > maxSpeed) {
        player->linearVelocity = maxSpeed;
    }

    // Only accelerate during animation frames 10-26 (from 2Ship line 13304-13309)
    if ((0.0f < incrStep) && (incrStep < 16.0f)) {
        incrStep = fabsf(incrStep) * 0.5f;
    } else {
        speedTarget = 0.0f;
        incrStep = 0.0f;
    }

    Math_AsymStepToF(&player->linearVelocity, speedTarget, incrStep, (fabsf(player->linearVelocity) * 0.02f) + 0.1f);

    // Smooth yaw stepping (from 2Ship line 13315: Math_ScaledStepToS(&yaw, target, 0x640))
    Math_ScaledStepToS(&player->yaw, yawTarget, 0x640);
    player->actor.world.rot.y = player->yaw;
    player->actor.shape.rot.y = player->yaw;
}

// Helper: exit fast swim and return to idle (clears all swim state)
static void MmForm_ExitFastSwim(Player* player, PlayState* play) {
    gFormState.fastSwimActive = 0;
    gFormState.swimPitch = 0;
    gFormState.swimRoll = 0;
    gFormState.swimRollSmoothed = 0;
    gFormState.swimYawRate = 0;
    gFormState.swimExitFlag = 0;
    gFormState.swimFloorTimer = 0;
}

// Helper: exit swim entirely (leave water)
static void MmForm_ExitSwimToGround(Player* player, PlayState* play) {
    gFormState.swimState = 0;
    gFormState.zoraBoots = 0;
    player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
    // Restore OOT rotation control (was disabled during swim)
    player->stateFlags2 &= ~PLAYER_STATE2_DISABLE_ROTATION_ALWAYS;
    player->stateFlags3 &= ~PLAYER_STATE3_PAUSE_ACTION_FUNC;
    MmForm_ExitFastSwim(player, play);
    MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
}

// Speed update (from 2Ship func_80850BF8, z_player.c:16893-16904)
// Ramps swimSpeedB48 toward target with asymmetric step, applies yaw turning from stick X cosine curve.
static void MmForm_SwimSpeedUpdate(Player* player, PlayState* play, f32 speedTarget) {
    Input* input = &play->state.input[0];

    // Speed ramp (from 2Ship: Math_AsymStepToF(&unk_B48, arg1, 1.0f, fabsf(unk_B48)*0.01 + 0.4))
    Math_AsymStepToF(&gFormState.swimSpeedB48, speedTarget, 1.0f, (fabsf(gFormState.swimSpeedB48) * 0.01f) + 0.4f);

    // Yaw turning from stick X via cosine curve (from 2Ship line 16898-16903)
    f32 cosVal = Math_CosS((s16)(input->rel.stick_x * 0x10E));
    s16 yawDelta = (s16)(((input->rel.stick_x >= 0) ? 1 : -1) * (1.0f - cosVal) * -1100.0f);
    if (yawDelta < -0x1F40)
        yawDelta = -0x1F40;
    if (yawDelta > 0x1F40)
        yawDelta = 0x1F40;
    player->yaw += yawDelta;
    player->actor.world.rot.y = player->yaw;
    player->actor.shape.rot.y = player->yaw;
}

// Velocity from pitch (from 2Ship func_80850BA8, z_player.c:16888-16891)
static void MmForm_SwimApplyVelocity(Player* player) {
    player->linearVelocity = Math_CosS(gFormState.swimPitch) * gFormState.swimSpeedB48;
    player->actor.velocity.y = -Math_SinS(gFormState.swimPitch) * gFormState.swimSpeedB48;
}

// Dolphin jump — launch Zora out of water during fast swim (from 2Ship func_8083B3B4, z_player.c:8953-8978)
// Triggers when fast swimming near surface with upward pitch angle.
// Zora exits water in torpedo pose, arcs through air, and re-enters water into fast swim.
// Returns true if jump was initiated.
static s32 MmForm_CheckDolphinJump(Player* player, PlayState* play) {
    if (!gFormState.fastSwimActive)
        return 0;

    // Pitch check: must be angled upward (from 2Ship line 8960: unk_AAA < -0x1555)
    // Negative pitch = pointing upward (velocity.y = -sin(pitch) * speed → positive)
    if (gFormState.swimPitch >= -0x1555)
        return 0;

    // Surface proximity check (from 2Ship line 8961):
    // (depthInWater - velocity.y) < ageProperties->unk_30 (68.0f)
    f32 predictedDepth = player->actor.yDistToWater - player->actor.velocity.y;
    if (predictedDepth >= ZORA_DEEP_THRESHOLD)
        return 0;

    // Speed check (from 2Ship line 8966-8968)
    f32 launchSpeed = gFormState.swimSpeedB48 * 1.5f;
    if (launchSpeed > 13.5f)
        launchSpeed = 13.5f;
    if (launchSpeed < 2.0f)
        return 0;

    // Launch velocity from current pitch (from 2Ship line 8971-8972)
    player->linearVelocity = Math_CosS(gFormState.swimPitch) * launchSpeed;
    player->actor.velocity.y = -Math_SinS(gFormState.swimPitch) * launchSpeed;

    // Transition to dolphin jump action — KEEP swim visual state
    // From 2Ship: stateFlags3 |= PLAYER_STATE3_8000 stays set, unk_B86[1] preserved
    // DON'T clear: fastSwimActive, swimPitch, swimRoll (preserved for visual during arc)
    gFormState.swimExitFlag = 0;
    gFormState.swimYawRate = 0;
    player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
    player->actor.bgCheckFlags &= ~1; // Force airborne
    gFormState.wasOnGround = 0;

    // Fishswim animation — torpedo pose, locked (from 2Ship: Player_Action_28 with STATE3_8000)
    LinkAnimationHeader* swimAnim = gFormState.fishSwim ? gFormState.fishSwim : gFormState.idleAnim;
    MmForm_SetAction(MMFORM_ACT_DOLPHIN_JUMP, play, swimAnim, 1.0f, ANIMMODE_LOOP);

    // SFX (from 2Ship line 8976: NA_SE_EV_JUMP_OUT_WATER)
    Audio_PlayActorSound2(&player->actor, NA_SE_EV_JUMP_OUT_WATER);

    return 1;
}

// Dolphin jump action — Zora arcs through air in torpedo pose (from 2Ship Player_Action_28 with STATE3_8000)
// No input allowed. Automatically re-enters fast swim on water contact, or lands on ground.
static void MmForm_Action_DolphinJump(Player* player, PlayState* play) {
    LinkAnimation_Update(play, &gFormState.formSkelAnime);

    // Smooth pitch toward 0 (Zora levels out during arc)
    Math_SmoothStepToS(&gFormState.swimPitch, 0, 4, 0x500, 0x100);

    // Water re-entry: back in water deep enough AND falling
    if (player->actor.yDistToWater > ZORA_SWIM_THRESHOLD && player->actor.velocity.y <= 0.0f) {
        // Re-enter fast swim with waterroll (from 2Ship: re-entering water triggers waterroll)
        gFormState.swimState = 1;
        player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_SWIM);

        if (gFormState.waterRoll != NULL) {
            MmForm_SetAction(MMFORM_ACT_SWIM_FAST, play, gFormState.waterRoll, 1.0f, ANIMMODE_ONCE);
            LinkAnimation_Change(play, &gFormState.formSkelAnime, gFormState.waterRoll, 1.0f, 4.0f,
                                 Animation_GetLastFrame(gFormState.waterRoll), ANIMMODE_ONCE, -6.0f);
            gFormState.swimPhase = 0;
            gFormState.swimPhaseCounter = 1;
            gFormState.swimExitFlag = 0;
            gFormState.swimRoll = 0;
            gFormState.swimRollSmoothed = 0;
            gFormState.fastSwimActive = 1;
            gFormState.swimSpeedB48 = player->linearVelocity;
            player->actor.velocity.y = 0.0f;
            MmForm_PlaySfx(player, MM_NA_SE_PL_ZORA_DIVE, NA_SE_PL_DIVE_BUBBLE);
        } else {
            MmForm_EnterSwimIdle(player, play);
        }
        return;
    }

    // Ground contact: if still underwater → return to swim idle; if above water → exit to land
    if (MMFORM_ON_GROUND(player) && player->actor.velocity.y <= 0.0f) {
        if (player->actor.yDistToWater > ZORA_SWIM_THRESHOLD) {
            // Underwater floor contact → return to swim idle, don't exit swim
            MmForm_EnterSwimIdle(player, play);
            return;
        }
        // Above water → exit swim to ground
        gFormState.swimState = 0;
        gFormState.fastSwimActive = 0;
        gFormState.swimPitch = 0;
        gFormState.swimRoll = 0;
        gFormState.swimRollSmoothed = 0;
        gFormState.swimYawRate = 0;
        player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
        player->stateFlags2 &= ~PLAYER_STATE2_DISABLE_ROTATION_ALWAYS;
        player->stateFlags3 &= ~PLAYER_STATE3_PAUSE_ACTION_FUNC;
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }
}

// Fast swim — 3-phase state machine (from 2Ship Player_Action_56, z_player.c:16910-17039)
// Phase 0 (swimPhaseCounter > 0): Waterroll transition — barrel roll animation, speed kicks in at frame 13
// Phase 1 (swimPhaseCounter == 0, swimExitFlag == 0): Active swimming — pitch/roll/yaw control
// Phase 2 (swimExitFlag != 0): Exiting — smooth roll to 0, swimtowait, then idle
static void MmForm_Action_SwimFast(Player* player, PlayState* play) {
    Input* input = &play->state.input[0];
    f32 speedTarget = 0.0f;

    // Visual effects (ripples, bubbles)
    MmForm_SwimEffects(player, play);

    // Buoyancy (from 2Ship: func_808475B4 called in Player_Action_56)
    MmForm_WaterBuoyancy(player);

    // Barrier input (from 2Ship: func_8082F164 called in Action_56)
    MmForm_CheckBarrierInput(player, play);

    // Dolphin jump check BEFORE exit-water (from 2Ship z_player.c:8816-8841)
    // If fast swimming with speed, dolphin jump takes priority over ground exit
    if (gFormState.fastSwimActive && MmForm_CheckDolphinJump(player, play)) {
        return;
    }

    // Exit water check — only when slow or not fast swimming
    if (player->actor.yDistToWater <= 0.0f && MMFORM_ON_GROUND(player)) {
        MmForm_ExitSwimToGround(player, play);
        return;
    }

    // =============================================
    // PHASE 0: Waterroll transition (av2 != 0)
    // From 2Ship Player_Action_56 line 16937-16964
    // =============================================
    if (gFormState.swimPhaseCounter > 0) {
        // Check exit conditions (from 2Ship line 16938-16941)
        if (!CHECK_BTN_ALL(input->cur.button, BTN_A) || gFormState.zoraBoots == 1) {
            gFormState.swimExitFlag = 1;
        }

        // Update animation
        if (LinkAnimation_Update(play, &gFormState.formSkelAnime)) {
            // Animation finished one loop → decrement counter
            gFormState.swimPhaseCounter--;
            if (gFormState.swimPhaseCounter == 0) {
                if (gFormState.swimExitFlag != 0) {
                    // Exit: play swimtowait (from 2Ship line 16944-16946)
                    gFormState.fastSwimActive = 0;
                    if (gFormState.swimToWait != NULL) {
                        LinkAnimation_Change(play, &gFormState.formSkelAnime, gFormState.swimToWait, 1.0f, 0.0f,
                                             Animation_GetLastFrame(gFormState.swimToWait), ANIMMODE_ONCE, -6.0f);
                    }
                    gFormState.swimExitFlag = 2; // Mark as in exit animation
                } else {
                    // Continue: start fishswim loop (from 2Ship line 16948)
                    if (gFormState.fishSwim != NULL) {
                        LinkAnimation_Change(play, &gFormState.formSkelAnime, gFormState.fishSwim, 1.0f, 0.0f,
                                             Animation_GetLastFrame(gFormState.fishSwim), ANIMMODE_LOOP, -6.0f);
                    }
                    // Reset barrel roll for clean Phase 1 entry
                    gFormState.swimRoll = 0;
                    gFormState.swimRollSmoothed = 0;
                }
            }
        } else {
            // Get movement input for yaw steering during waterroll
            // (from 2Ship line 16952-16953: Player_GetMovementSpeedAndYaw + Math_ScaledStepToS)
            f32 tempSpeed = 0.0f;
            s16 yawTarget = player->actor.shape.rot.y;
            Player_GetMovementSpeedAndYaw(player, &tempSpeed, &yawTarget, SPEED_MODE_LINEAR, play);
            Math_ScaledStepToS(&player->yaw, yawTarget, 0x640);
            player->actor.world.rot.y = player->yaw;
            player->actor.shape.rot.y = player->yaw;

            // At frame >= 13: speed kicks in, set fastSwimActive
            // (from 2Ship line 16954-16960)
            if (gFormState.formSkelAnime.curFrame >= 13.0f) {
                speedTarget = 12.0f;
                gFormState.fastSwimActive = 1;

                // On the exact frame 13, set unk_B48 = 16
                if (gFormState.formSkelAnime.curFrame < 14.0f &&
                    gFormState.formSkelAnime.curFrame - gFormState.formSkelAnime.playSpeed < 13.0f) {
                    gFormState.swimSpeedB48 = 16.0f;
                }
            }
        }

        // Barrel roll during waterroll: 2 full rotations over ~14 frames (frame 4-18)
        // 2 * 0x10000 / 14 ≈ 0x2492 per frame
        gFormState.swimRoll += 0x2492;
        gFormState.swimRollSmoothed = gFormState.swimRoll; // Direct, no lag during barrel roll
    }
    // =============================================
    // PHASE 1: Active swimming (av2 == 0, exitFlag == 0)
    // From 2Ship Player_Action_56 line 16968-17021
    // =============================================
    else if (gFormState.swimExitFlag == 0) {
        LinkAnimation_Update(play, &gFormState.formSkelAnime);
        gFormState.fastSwimActive = 1;

        // Check dolphin jump (from 2Ship line 8816-8841: surface exit during fast swim)
        if (MmForm_CheckDolphinJump(player, play)) {
            return;
        }

        // Wall collision: dampen speed (MM relies on Actor_UpdateBgCheckInfo pushing out)
        if (player->actor.bgCheckFlags & 8) { // BGCHECKFLAG_WALL
            gFormState.swimSpeedB48 *= 0.5f;
            player->linearVelocity *= 0.5f;
        }

        // Check exit conditions (from 2Ship line 16971-16975)
        if (!CHECK_BTN_ALL(input->cur.button, BTN_A) || gFormState.zoraBoots != 0) {
            gFormState.swimExitFlag = 1;
            if (gFormState.swimToWait != NULL) {
                LinkAnimation_Change(play, &gFormState.formSkelAnime, gFormState.swimToWait, 1.0f, 0.0f,
                                     Animation_GetLastFrame(gFormState.swimToWait), ANIMMODE_ONCE, -6.0f);
            }
        } else {
            speedTarget = 9.0f;
            // Looping swim SFX (from 2Ship line 16978: NA_SE_PL_ZORA_SWIM_LV - SFX_FLAG)
            MmForm_PlaySfx(player, MM_NA_SE_PL_ZORA_SWIM, NA_SE_PL_SWIM);
        }

        // Pitch from stick Y (from 2Ship line 16982-16991)
        s16 pitchTarget = (s16)(input->rel.stick_y * 0xC8);
        // Floor bounce cooldown (from 2Ship line 16983-16986)
        if (gFormState.swimFloorTimer != 0) {
            gFormState.swimFloorTimer--;
            s16 floorLimit = (s16)(player->floorPitch - 0xFA0);
            if (pitchTarget > floorLimit)
                pitchTarget = floorLimit;
        }
        // Clamp pitch near surface (from 2Ship line 16988-16990)
        if (gFormState.swimPitch >= -0x1555 && player->actor.yDistToWater < (ZORA_SURFACE_DEPTH + 10.0f)) {
            if (pitchTarget < 0x7D0)
                pitchTarget = 0x7D0;
        }
        Math_SmoothStepToS(&gFormState.swimPitch, pitchTarget, 4, 0xFA0, 0x190);

        // Roll from stick X with accumulation (from 2Ship line 16994-17007)
        s16 rollTarget = (s16)(input->rel.stick_x * 0x64);
        if (Math_ScaledStepToS(&gFormState.swimYawRate, rollTarget, 0x384) && (rollTarget == 0)) {
            // Centered: smooth roll and smoothed roll toward 0
            Math_SmoothStepToS(&gFormState.swimRoll, 0, 4, 0x5DC, 0x64);
            Math_SmoothStepToS(&gFormState.swimRollSmoothed, gFormState.swimRoll, 2, 0x5DC, 0x64);
        } else {
            // Accumulate roll (from 2Ship line 16999-17001)
            s16 prevRoll = gFormState.swimRoll;
            s16 crossThreshold = (gFormState.swimYawRate < 0) ? -0x3A98 : 0x3A98;
            gFormState.swimRoll += gFormState.swimYawRate;
            Math_SmoothStepToS(&gFormState.swimRollSmoothed, gFormState.swimRoll, 2, 0x5DC, 0x64);
            // Barrel roll SFX on cross-over (from 2Ship line 17004-17006)
            if ((ABS(gFormState.swimYawRate) > 0xFA0) &&
                (((prevRoll + gFormState.swimYawRate) - crossThreshold) * (prevRoll - crossThreshold)) <= 0) {
                MmForm_PlaySfx(player, MM_NA_SE_PL_ZORA_SWIM, NA_SE_PL_SWIM);
            }
        }

        // Near-floor dust (from 2Ship line 17009-17011: sPlayerYDistToFloor < 20.0f)
        // Note: We don't have sPlayerYDistToFloor, approximate with bgCheckFlags
        // Skip for now — cosmetic only

        // Floor bounce (from 2Ship line 17023-17035)
        if (gFormState.swimFloorTimer < 8 && MMFORM_ON_GROUND(player)) {
            gFormState.swimPitch += (s16)((-player->floorPitch - gFormState.swimPitch) * 2);
            gFormState.swimFloorTimer = 15;
            Audio_PlayActorSound2(&player->actor, NA_SE_PL_BODY_BOUND);

            // Floor dust effect (from 2Ship func_80850D20: func_8083F8A8 with dust params)
            Vec3f dustPos = player->actor.world.pos;
            EffectSsGRipple_Spawn(play, &dustPos, 50, 300, 0);
        }
    }
    // =============================================
    // PHASE 2: Exiting (swimExitFlag != 0)
    // From 2Ship Player_Action_56 line 17012-17021
    // =============================================
    else {
        // Smooth roll to 0 (from 2Ship line 17013)
        Math_SmoothStepToS(&gFormState.swimRoll, 0, 4, 0xFA0, 0x190);
        Math_SmoothStepToS(&gFormState.swimRollSmoothed, gFormState.swimRoll, 2, 0x5DC, 0x64);

        // Allow re-dive early in exit (from 2Ship line 17014)
        if (gFormState.formSkelAnime.curFrame <= 5.0f) {
            if (CHECK_BTN_ALL(input->cur.button, BTN_A) && gFormState.waterRoll != NULL && gFormState.zoraBoots == 0) {
                // Re-initiate dive
                LinkAnimation_Change(play, &gFormState.formSkelAnime, gFormState.waterRoll, 1.0f, 4.0f,
                                     Animation_GetLastFrame(gFormState.waterRoll), ANIMMODE_ONCE, -6.0f);
                gFormState.swimPhase = 0;
                gFormState.swimPhaseCounter = 1; // Play waterroll once then transition
                gFormState.swimExitFlag = 0;
                gFormState.swimSpeedB48 = player->linearVelocity;
                player->actor.velocity.y = 0.0f;
                gFormState.fastSwimActive = 1; // Visible from first frame
                gFormState.swimRoll = 0;       // Reset roll for clean barrel roll
                gFormState.swimRollSmoothed = 0;
                MmForm_PlaySfx(player, MM_NA_SE_PL_ZORA_SWIM, NA_SE_PL_DIVE_BUBBLE);
                return;
            }
        }

        if (LinkAnimation_Update(play, &gFormState.formSkelAnime)) {
            // Exit animation done → swim idle (from 2Ship line 17016: func_808353DC)
            MmForm_ExitFastSwim(player, play);
            MmForm_SetAction(MMFORM_ACT_SWIM_IDLE, play,
                             gFormState.swimWaitAnim ? gFormState.swimWaitAnim : gFormState.idleAnim, 1.0f,
                             ANIMMODE_LOOP);
            return;
        }
    }

    // Apply speed and velocity (from 2Ship func_80850BF8 + func_80850BA8)
    MmForm_SwimSpeedUpdate(player, play, speedTarget);
    MmForm_SwimApplyVelocity(player);

    gFormState.actionTimer++;
}

// SWIM_DASH now redirects to SWIM_FAST (merged as Phase 0)
// Kept for backwards compatibility with action dispatch switch
static void MmForm_Action_SwimDash(Player* player, PlayState* play) {
    MmForm_Action_SwimFast(player, play);
}

// Underwater walk / iron boots (from 2Ship Player_Action_58, z_player.c:17074-17096)
// Walks on ocean floor with normal gravity. A toggles back to free swim.
static void MmForm_Action_SwimUnderwaterWalk(Player* player, PlayState* play) {
    LinkAnimation_Update(play, &gFormState.formSkelAnime);
    Input* input = &play->state.input[0];

    // Visual effects (bubbles when underwater)
    MmForm_SwimEffects(player, play);

    // Barrier input (flag-based)
    MmForm_CheckBarrierInput(player, play);

    // Boot mode toggle (A → LAND, will float up)
    MmForm_CheckBootToggle(player, play);

    // If boots switched to LAND → return to swim idle (float up)
    if (gFormState.zoraBoots == 0) {
        player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_SWIM);
        MmForm_SetAction(MMFORM_ACT_SWIM_IDLE, play,
                         gFormState.swimWaitAnim ? gFormState.swimWaitAnim : gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        return;
    }

    // Normal gravity on ocean floor
    player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);

    // Exit water (walked out of water body)
    if (player->actor.yDistToWater <= 0.0f && MMFORM_ON_GROUND(player)) {
        gFormState.swimState = 0;
        gFormState.zoraBoots = 0;
        gFormState.fastSwimActive = 0;
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
        MmForm_PlaySfx(player, MM_NA_SE_PL_LAND, NA_SE_PL_LAND);
        return;
    }

    // Movement from stick (from 2Ship Player_Action_58: Player_GetMovementSpeedAndYaw + func_80847FF8)
    f32 speedTarget = 0.0f;
    s16 yawTarget = player->actor.shape.rot.y;

    Player_GetMovementSpeedAndYaw(player, &speedTarget, &yawTarget, SPEED_MODE_LINEAR, play);

    if (speedTarget == 0.0f) {
        // No input on floor → stay idle on the floor (don't go back to SWIM_IDLE
        // which would cause buoyancy → ground → SWIM_UNDERWATER_WALK → oscillation)
        if (MMFORM_ON_GROUND(player)) {
            Math_StepToF(&player->linearVelocity, 0.0f, 1.0f);
            return;
        }
        // Not on ground anymore → return to swim idle (float/sink)
        MmForm_SetAction(MMFORM_ACT_SWIM_IDLE, play,
                         gFormState.swimWaitAnim ? gFormState.swimWaitAnim : gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
    }

    // Always update speed + yaw (from 2Ship func_80847FF8 → func_8084748C)
    MmForm_SwimMovement(player, speedTarget, yawTarget);
}

// Draw barrier visual (from 2Ship Player_DrawZoraShield, z_player_lib.c:2316)
// TWO draw modes depending on whether Zora is fast-swimming or not:
//
// Mode A - Ground / Iron boots (NOT fast swimming):
//   From z_player.c:13203-13209: RotateXS(-0x4000) + Translate(0,0,-1800)
//   Barrier faces FORWARD from player chest
//
// Mode B - Fast swim (PLAYER_STATE3_8000 / fastSwimActive):
//   From z_player_lib.c:2392-2399: RotateZS(roll) + RotateXS(-0x8000) + Translate(0,0,-4000)
//   Barrier wraps around body, oriented with swim pitch/roll
static void MmForm_DrawZoraBarrier(Player* player, PlayState* play) {
    if (gFormState.barrierIntensity <= 0)
        return;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);

    // Scale based on intensity (from 2Ship z_player_lib.c:2320: scale = unk_B62 * (10.0f / 51.0f))
    // MM original: 10.0f/51.0f (max ~50) but that's for underwater fast swim only.
    // Ground barrier is custom (MM doesn't draw barrier on ground), so use smaller scale.
    f32 scale;

    Matrix_Push();

    // MM draws barrier INSIDE the limb draw callback where skeleton scale (0.01) is active.
    // Our barrier is drawn in world space, so all offsets must be ×0.01 of MM's model-space values.
    // MM scale: unk_B62 * (10.0f / 51.0f) in model space = unk_B62 * (0.1f / 51.0f) in world space.
    if (gFormState.fastSwimActive) {
        scale = gFormState.barrierIntensity * (0.1f / 51.0f); // MM: 10.0f/51.0f model → 0.1f/51.0f world
        // === Mode B: Fast swim barrier (from 2Ship z_player_lib.c:2430-2443) ===
        f32 yAdj = (Math_CosS(gFormState.swimPitch) - 1.0f) * 2.0f; // MM: 200 model → 2 world
        Matrix_Translate(player->actor.world.pos.x, yAdj + player->actor.world.pos.y + 40.0f, player->actor.world.pos.z,
                         MTXMODE_NEW);
        Matrix_RotateY(BINANG_TO_RAD(player->actor.shape.rot.y), MTXMODE_APPLY);
        Matrix_RotateX(BINANG_TO_RAD(gFormState.swimPitch), MTXMODE_APPLY);
        Matrix_RotateZ(BINANG_TO_RAD(gFormState.swimRollSmoothed), MTXMODE_APPLY);
        Matrix_RotateX(M_PI, MTXMODE_APPLY);                 // 180 deg flip (-0x8000)
        Matrix_Translate(0.0f, 0.0f, -40.0f, MTXMODE_APPLY); // MM: -4000 model → -40 world
    } else {
        scale = gFormState.barrierIntensity * (0.05f / 51.0f); // Ground: smaller (no MM reference)
        // === Mode A: Ground barrier with R+B (from 2Ship z_player.c:13203-13209) ===
        Matrix_Translate(player->actor.world.pos.x, player->actor.world.pos.y + 40.0f, player->actor.world.pos.z,
                         MTXMODE_NEW);
        Matrix_RotateY(BINANG_TO_RAD(player->actor.shape.rot.y), MTXMODE_APPLY);
        Matrix_RotateX(BINANG_TO_RAD((s16)-0x4000), MTXMODE_APPLY); // -90 deg (forward-facing)
        Matrix_Translate(0.0f, 0.0f, -18.0f, MTXMODE_APPLY);        // MM: -1800 model → -18 world
    }

    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);

    gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    // Set segment 0x0C for MM DL compatibility (G_DL_INDEX references)
    gSPSegment(POLY_XLU_DISP++, 0x0C, (uintptr_t)gCullBackDList);

    // Set segments 0x0A/0x0B for animated material texture scroll
    // (from 2Ship object_link_zora_Matanimheader_012A80:
    //   Layer 0: xStep=-1, yStep=20, width=0x20, height=0x40
    //            xStep=-2, yStep=10, width=0x20, height=0x40
    //   Layer 1: xStep=3, yStep=20, width=0x20, height=0x40
    //            xStep=-12, yStep=10, width=0x40, height=0x20)
    {
        u32 frames = play->gameplayFrames;
        gSPSegment(POLY_XLU_DISP++, 0x0A,
                   (uintptr_t)Gfx_TwoTexScroll(play->state.gfxCtx, 0, -(s32)(frames * 1), (s32)(frames * 20), 0x20,
                                               0x40, 1, -(s32)(frames * 2), (s32)(frames * 10), 0x20, 0x40));
        gSPSegment(POLY_XLU_DISP++, 0x0B,
                   (uintptr_t)Gfx_TwoTexScroll(play->state.gfxCtx, 0, (s32)(frames * 3), (s32)(frames * 20), 0x20, 0x40,
                                               1, -(s32)(frames * 12), (s32)(frames * 10), 0x40, 0x20));
    }

    // Draw barrier DL from mm.o2r (per-frame safe copy with G_ENDDL padding)
    if (sBarrierDLCount > 0 && !sBarrierDLSafeCopy.empty()) {
        static const size_t DL_PADDING = 16;
        size_t totalCount = sBarrierDLCount + DL_PADDING;
        Gfx* dlCopy = (Gfx*)Graph_Alloc(play->state.gfxCtx, totalCount * sizeof(Gfx));
        memcpy(dlCopy, sBarrierDLSafeCopy.data(), sBarrierDLCount * sizeof(Gfx));
        for (size_t p = 0; p < DL_PADDING; p++) {
            dlCopy[sBarrierDLCount + p].words.w0 = (uintptr_t)0xDF << 24;
            dlCopy[sBarrierDLCount + p].words.w1 = 0;
        }
        // Defensive: patch any segment 0x08 refs to gEmptyDL (safe no-op)
        MmForm_PatchSegmentedDL(dlCopy, sBarrierDLCount, 0x08, gEmptyDL);
        // Patch G_DL_INDEX seg 0x0C → direct pointers to cull DLs
        MmForm_PatchCullDLIndex(dlCopy, sBarrierDLCount);
        gSPDisplayList(POLY_XLU_DISP++, dlCopy);
    }

    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx);
}

// ---------------------------------------------------------------------------
// Main Active Dispatcher (replaces MmForm_UpdateMovement)
//
// Called every frame when MMFORM_STATE_ACTIVE.
// Dispatches to the current action handler, then ticks animation.
//
// GROUND DETECTION: Centralized airborne/landing detection runs BEFORE
// action dispatch to handle transitions from ANY ground action to air
// and from ANY air action to landing.
// ---------------------------------------------------------------------------
static void MmForm_UpdateActive(Player* player, PlayState* play) {
    // In MM, Goron/Zora/Deku always use PLAYER_ANIMTYPE_DEFAULT (type 0 = free hands).
    // OOT sets modelAnimType based on equipment (sword=1, shield=2, etc.) but MM forms
    // don't have equipped weapons. Without this, OOT selects sword-holding animations
    // (link_normal_walk vs link_normal_walk_free) which look wrong on MM form skeletons.
    // From 2Ship z_player_lib.c:1476: all non-FD/non-Human forms forced to PLAYER_ANIMTYPE_DEFAULT.
    if (gFormState.currentForm != MM_PLAYER_FORM_FIERCE_DEITY) {
        player->modelAnimType = PLAYER_ANIMTYPE_0;
    }

    // Force yOffset to 0 every frame (from 2Ship: shape.yOffset = unk_ABC + unk_AC0, both 0 when standing).
    // OOT's ledge climbing sets negative yOffset that Math_StepToF returns to 0, but if
    // transformation interrupts that process, a stale value could persist. Force clean.
    player->actor.shape.yOffset = 0.0f;

    // Shadow type per frame: DrawFeet when PostLimbDraw runs (updates feetPos[]),
    // DrawCircle for Goron ball/shield where no skeleton traversal occurs.
    if (gFormState.currentForm == MM_PLAYER_FORM_GORON &&
        (gFormState.goronAction == GORON_ACT_GORON_ROLL || gFormState.goronAction == GORON_ACT_GORON_ROLL_JUMP ||
         gFormState.goronAction == GORON_ACT_GORON_ROLL_POUND || gFormState.goronAction == MMFORM_ACT_SHIELD)) {
        player->actor.shape.shadowDraw = ActorShadow_DrawCircle;
    } else {
        player->actor.shape.shadowDraw = ActorShadow_DrawFeet;
    }

    // NPC talk: play pg_wait loop, skip all input handling
    // From 2Ship Player_StartTalking (line 21618): Player_Anim_PlayLoop(this, Player_GetIdleAnim(this))
    // From 2Ship Player_GetIdleAnim (line 2773): Goron returns gPlayerAnim_pg_wait
    if (player->stateFlags1 & PLAYER_STATE1_TALKING) {
        if (gFormState.goronAction != GORON_ACT_IDLE || gFormState.formSkelAnime.animation != gFormState.idleAnim) {
            MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
            player->linearVelocity = 0.0f;
        }
        LinkAnimation_Update(play, &gFormState.formSkelAnime);
        MmForm_UpdateBlink();
        return;
    }

    // Door/chest yield: OOT handles mechanics, we play the correct MM animation
    // From 2Ship D_8085D118/D_8085D124 (door anims), ageProperties->openChestAnim
    // OOT's action handlers still run (actionFunc not replaced with no-op),
    // so OOT will start the door/chest action. We just match the visual.
    //
    // IMPORTANT: Skip this handler when GETTING_ITEM is set. GETTING_ITEM means OOT
    // is in the "hold item above head" phase (or ground pickup). We need to fall through
    // to the OOT yield system so OOT's get-item animation is copied to our skeleton,
    // showing the raised-arms pose and positioning the item via sGetItemRefPos.
    if ((player->stateFlags1 & PLAYER_STATE1_IN_CUTSCENE) && !(player->stateFlags1 & PLAYER_STATE1_TALKING) &&
        !(player->stateFlags1 & PLAYER_STATE1_GETTING_ITEM)) {
        // Swim actions must continue even during scene-transition cutscenes.
        // Without this, entering a loading zone underwater as Zora causes a softlock:
        // the IN_CUTSCENE flag from the scene transition overrides swim with idle,
        // the player stands underwater unable to move until the cutscene flag clears.
        u8 inSwimAction =
            (gFormState.goronAction == MMFORM_ACT_SWIM_IDLE || gFormState.goronAction == MMFORM_ACT_SWIM_MOVE ||
             gFormState.goronAction == MMFORM_ACT_SWIM_FAST || gFormState.goronAction == MMFORM_ACT_SWIM_DASH ||
             gFormState.goronAction == MMFORM_ACT_SWIM_SURFACE_WALK ||
             gFormState.goronAction == MMFORM_ACT_SWIM_UNDERWATER_WALK ||
             gFormState.goronAction == MMFORM_ACT_DOLPHIN_JUMP);
        if (!inSwimAction) {
            // Door handling: only HANDLE (knob) doors use form-specific push/pull animations.
            // SLIDING doors (boss doors), AJAR, and FAKE doors just need OOT's walk-through
            // pathing — OOT manages waypoints and movement for those via Player_Action_80845CA4.
            if (player->doorActor != NULL && gFormState.goronAction != MMFORM_ACT_DOOR &&
                gFormState.goronAction != MMFORM_ACT_OOT_ACTION) {
                if (player->doorType == PLAYER_DOORTYPE_HANDLE) {
                    // Knob door: use form-specific push/pull animation if available
                    LinkAnimationHeader* doorAnim =
                        (player->doorDirection < 0) ? gFormState.doorAOpen : gFormState.doorBOpen;
                    if (doorAnim != NULL) {
                        MmForm_SetAction(MMFORM_ACT_DOOR, play, doorAnim, 1.0f, ANIMMODE_ONCE);
                        player->linearVelocity = 0.0f;
                    } else {
                        // No form-specific door animation → yield to OOT for push/pull
                        gFormState.goronAction = MMFORM_ACT_OOT_ACTION;
                        gFormState.actionTimer = 0;
                    }
                } else {
                    // Sliding/ajar/fake doors: yield to OOT for walk-through pathing
                    gFormState.goronAction = MMFORM_ACT_OOT_ACTION;
                    gFormState.actionTimer = 0;
                }
            }
            // Already in door/chest action → update form animation and return
            if (gFormState.goronAction == MMFORM_ACT_DOOR || gFormState.goronAction == MMFORM_ACT_CHEST) {
                LinkAnimation_Update(play, &gFormState.formSkelAnime);
                MmForm_UpdateBlink();
                return;
            }
            // OOT_ACTION yield (door with no form anim, or other OOT-controlled cutscene)
            // OOT's jointTable (walk-forward, etc.) gets copied in MmForm_Draw.
            if (gFormState.goronAction == MMFORM_ACT_OOT_ACTION) {
                MmForm_UpdateBlink();
                return;
            }
            // Other cutscenes (non-get-item, non-door) → play idle
            if (gFormState.goronAction != GORON_ACT_IDLE) {
                MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
                player->linearVelocity = 0.0f;
            }
            LinkAnimation_Update(play, &gFormState.formSkelAnime);
            MmForm_UpdateBlink();
            return;
        }
        // If in swim action, fall through to normal action dispatch below
    }
    // Get-Item: OOT sets GETTING_ITEM for the entire get-item sequence (chest open + hold).
    // We yield directly to OOT so its animation (chest open → hold above head) is copied
    // to our skeleton via jointTable in MmForm_Draw. This ensures:
    //   1. The form shows OOT's get-item pose (raised arms)
    //   2. PostLimbDraw calculates correct hand position → sGetItemRefPos
    //   3. Player_DrawGetItem renders the item above the form's head
    // The GETTING_ITEM flag is also in MMFORM_OOT_YIELD_FLAGS so the yield check below
    // will handle it. We just make sure we don't interfere.
    // Return to idle after door/chest finishes (but not during get-item, which yields to OOT)
    if ((gFormState.goronAction == MMFORM_ACT_DOOR || gFormState.goronAction == MMFORM_ACT_CHEST) &&
        !(player->stateFlags1 & PLAYER_STATE1_GETTING_ITEM)) {
        MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
    }

    // Wall/vine climbing: OOT handles mechanics AND animation via yield system.
    // PLAYER_STATE1_CLIMBING_LADDER is in MMFORM_OOT_YIELD_FLAGS, so the form defers
    // to OOT and copies OOT's climbing jointTable to the MM skeleton in MmForm_Draw.

    // Safety: Goron cannot hang on ledges (from 2Ship z_player.c line 6209: Goron excluded)
    // If OOT somehow set this flag through a code path we didn't block, force drop.
    // Other forms (Zora, Deku, FD) CAN grab ledges.
    if (gFormState.currentForm == MM_PLAYER_FORM_GORON && (player->stateFlags1 & PLAYER_STATE1_HANGING_OFF_LEDGE)) {
        player->stateFlags1 &= ~PLAYER_STATE1_HANGING_OFF_LEDGE;
        player->actor.velocity.y = 0.0f;
        player->linearVelocity = 0.0f;
        MmForm_SetAction(MMFORM_ACT_FALL, play, gFormState.fallAnim ? gFormState.fallAnim : gFormState.idleAnim, 1.0f,
                         ANIMMODE_LOOP);
    }

    // Deku water hop: check at ANY water depth while falling
    // From 2Ship func_8083784C (z_player.c line 7247-7260):
    //   velocity.y < 0 (falling) AND depthInWater > 0 (touching water)
    //   AND remainingHopsCounter != 0 AND health != 0
    if (gFormState.currentForm == MM_PLAYER_FORM_DEKU && player->actor.yDistToWater > 0.0f &&
        player->actor.velocity.y < 0.0f && gFormState.dekuHopsRemaining > 0 && gSaveContext.health > 0) {
        MmForm_DekuWaterHop(player, play);
        return;
    }

    // Water interaction by form
    if (player->actor.yDistToWater > 20.0f) {
        if (gFormState.currentForm == MM_PLAYER_FORM_GORON || gFormState.currentForm == MM_PLAYER_FORM_DEKU) {
            // Goron sinks in deep water / Deku with no hops left → void out
            // From 2Ship: Goron excluded from diving (func_8083B3B4 line 8930)
            if (gFormState.goronAction != MMFORM_ACT_WATER_VOID && gFormState.goronAction != MMFORM_ACT_HAZARD_VOID) {
                gFormState.goronAction = MMFORM_ACT_WATER_VOID;
                gFormState.actionTimer = 0;
                gFormState.rollGroundPoundTimer = 0;
            }
            // Don't return here - fall through to action dispatch so
            // MmForm_Action_WaterVoidOut can run each frame and progress the void out.
        } else if (gFormState.currentForm == MM_PLAYER_FORM_ZORA && gFormState.swimState == 0 &&
                   player->actor.yDistToWater > ZORA_SWIM_THRESHOLD) {
            // Zora enters water → swim mode (from 2Ship: func_8083B3B4 → Player_Action_54)
            // Block swim entry only when already in a swim/water action.
            // All other actions (idle, walk, run, Z-target, fall, jump, OOT yield, etc.)
            // allow water entry so Zora can always transition to swim on contact.
            s32 curAct = gFormState.goronAction;
            u8 blockSwimEntry =
                (curAct == MMFORM_ACT_WATER_VOID || curAct == MMFORM_ACT_HAZARD_VOID ||
                 curAct == MMFORM_ACT_SWIM_IDLE || curAct == MMFORM_ACT_SWIM_MOVE || curAct == MMFORM_ACT_SWIM_FAST ||
                 curAct == MMFORM_ACT_SWIM_DASH || curAct == MMFORM_ACT_SWIM_SURFACE_WALK ||
                 curAct == MMFORM_ACT_SWIM_UNDERWATER_WALK || curAct == MMFORM_ACT_DOLPHIN_JUMP);
            if (!blockSwimEntry) {
                MmForm_EnterSwimIdle(player, play);
                MmForm_PlaySfx(player, MM_NA_SE_PL_ZORA_DIVE, NA_SE_PL_DIVE_BUBBLE);
                return;
            }
        }
    }

    // Lava/hot floor check by form
    // From 2Ship func_80834600 (z_player.c line 6152-6165):
    //   Goron: FULL immunity to lava/hot floor (not in water) - resets OOT timer every frame
    //   Deku/Zora: on floorType 2 (hot room) or 3 (lava), on ground, not underwater → burn → death
    if (MMFORM_ON_GROUND(player) && player->actor.yDistToWater < 0.0f) {
        s32 floorType = TransformMasks_GetFloorType();
        if (floorType == 2 || floorType == 3) {
            // Goron: full lava/fire immunity (from 2Ship z_player.c line 6152-6153)
            // OOT's Goron Tunic only DELAYS damage (60/120 frames), but MM Goron is IMMUNE.
            // Reset floorTypeTimer every frame so OOT's damage threshold (D_808544F4) is never reached.
            if (gFormState.currentForm == MM_PLAYER_FORM_GORON) {
                player->floorTypeTimer = 0;
                // Also clear body burn (OOT might set it from wall/floor damage surfaces)
                player->bodyIsBurning = false;
            } else if ((gFormState.currentForm == MM_PLAYER_FORM_DEKU ||
                        gFormState.currentForm == MM_PLAYER_FORM_ZORA) &&
                       gFormState.goronAction != MMFORM_ACT_HAZARD_VOID &&
                       gFormState.goronAction != MMFORM_ACT_WATER_VOID) {
                gFormState.goronAction = MMFORM_ACT_HAZARD_VOID;
                gFormState.hazardVoidType = 1; // lava
                gFormState.hazardVoidTimer = 0;
                gFormState.actionTimer = 0;
                // Initial damage (from 2Ship line 6167: colChkInfo.damage = 4)
                Health_ChangeBy(play, -4);
                // Set invincibility to prevent OOT's floor damage from also applying
                if (player->invincibilityTimer >= 0) {
                    player->invincibilityTimer = 20;
                    player->damageFlickerAnimCounter = 0;
                }
                player->floorTypeTimer = 0; // Reset OOT's floor damage timer
                return;
            }
        }
    }

    // Fierce Deity 1.5x speed multiplier - MOVED to walk/run/strafe action handlers.
    // Applying *= 1.5f here compounded every frame because OOT's actionFunc (still running)
    // sets linearVelocity via Math_StepToF, then we multiply, then next frame OOT starts
    // from the multiplied value → exponential blowup.
    // Fix: override linearVelocity with the correct FD target speed in each action handler,
    // matching 2Ship's approach in Player_CalcSpeedAndYawFromControlStick (line 5236).

    // Tick flinch timer (from 2Ship unk_B64, set by speed flinch in MmForm_CheckDamage)
    if (gFormState.flinchTimer > 0) {
        gFormState.flinchTimer--;
    }

    // Increment action timer (used by punch combo, damage, etc.)
    gFormState.actionTimer++;

    // Reset shield collider AC/AT flags each frame (from 2Ship z_player.c line 12842-12843)
    // Without this, AC_BOUNCED/AC_HIT flags stay stuck after one hit
    if (gFormState.shieldColliderInitDone) {
        Collider_ResetCylinderAC(play, &gFormState.shieldCollider.base);
    }

    // Damage/knockback: OOT handles natively via func_808382DC → func_80837C0C.
    // The form yields to OOT via PLAYER_STATE1_DAMAGED in MMFORM_OOT_YIELD_FLAGS.
    // OOT's jointTable (with knockback animation) is copied to the form skeleton in Draw.

    // =========================================================================
    // OOT Fallback Action System
    //
    // When OOT is running a special action (item use, NPC dialogue, cutscene,
    // carrying actor, getting item), we YIELD to OOT and let it handle everything.
    // The MM form skeleton copies OOT's jointTable in MmForm_Draw so the MM model
    // displays Link's OOT animation.
    //
    // This allows transformed forms to use bottles, ocarina, talk to NPCs, open
    // chests/doors, etc. without needing form-specific handlers for each case.
    // Sword/shield are already blocked by sSlotAllowed* (slot-based restriction).
    // =========================================================================
    {
// Flags that indicate OOT is running a special action we should yield to.
// When any of these are set, the MM form defers to OOT and copies OOT's jointTable
// so the MM skeleton displays OOT's animation (death, ledge hang, cutscene, etc.).
#define MMFORM_OOT_YIELD_FLAGS                                                                            \
    (PLAYER_STATE1_SWINGING_BOTTLE |   /* (1 << 1)  - Bottle swing/catch */                               \
     PLAYER_STATE1_DAMAGED |           /* (1 << 2)  - Damage/knockback (OOT handles anims+physics) */     \
     PLAYER_STATE1_TALKING |           /* (1 << 6)  - NPC dialogue */                                     \
     PLAYER_STATE1_DEAD |              /* (1 << 7)  - Game over / death sequence */                       \
     PLAYER_STATE1_FIRST_PERSON |      /* (1 << 9)  - First-person aim (hookshot, bow, etc.) */           \
     PLAYER_STATE1_GETTING_ITEM |      /* (1 << 10) - Item get cutscene */                                \
     PLAYER_STATE1_CARRYING_ACTOR |    /* (1 << 11) - Carrying/throwing actor */                          \
     PLAYER_STATE1_CLIMBING_LADDER |   /* (1 << 12) - Wall/vine/ladder climbing (OOT handles movement) */ \
     PLAYER_STATE1_HANGING_OFF_LEDGE | /* (1 << 13) - Hanging on ledge edge before climbing */            \
     PLAYER_STATE1_CLIMBING_LEDGE |    /* (1 << 14) - Medium/high ledge climb (OOT handles pos+anim) */   \
     PLAYER_STATE1_IN_ITEM_CS |        /* (1 << 28) - Item use cutscene (ocarina, etc.) */                \
     PLAYER_STATE1_IN_CUTSCENE         /* (1 << 29) - General cutscene */                                 \
    )
        // NOTE: PLAYER_STATE1_INPUT_DISABLED is NOT in yield flags because the MM form
        // uses it internally (Goron roll, Deku fall, transformation cutscenes).
        // OOT's INPUT_DISABLED states (scene transitions, events) always co-occur
        // with other flags (IN_CUTSCENE, TALKING, etc.) that already trigger yield.

        u32 yieldFlags = player->stateFlags1 & MMFORM_OOT_YIELD_FLAGS;

        // Don't yield for FIRST_PERSON when WE set it (Deku bubble aim / Zora boomerang aim)
        if (gFormState.bubbleCharging || gFormState.goronAction == MMFORM_ACT_DEKU_BUBBLE_AIM ||
            gFormState.goronAction == MMFORM_ACT_BOOMERANG_THROW) {
            yieldFlags &= ~(PLAYER_STATE1_FIRST_PERSON | PLAYER_STATE1_ITEM_IN_HAND | PLAYER_STATE1_READY_TO_FIRE);
        }

        // Don't yield for IN_CUTSCENE when in a swim action. Scene-transition cutscenes
        // set IN_CUTSCENE, but the swim must keep running or the player gets stuck standing
        // underwater unable to move after loading a zone as Zora.
        if (gFormState.goronAction == MMFORM_ACT_SWIM_IDLE || gFormState.goronAction == MMFORM_ACT_SWIM_MOVE ||
            gFormState.goronAction == MMFORM_ACT_SWIM_FAST || gFormState.goronAction == MMFORM_ACT_SWIM_DASH ||
            gFormState.goronAction == MMFORM_ACT_SWIM_SURFACE_WALK ||
            gFormState.goronAction == MMFORM_ACT_SWIM_UNDERWATER_WALK ||
            gFormState.goronAction == MMFORM_ACT_DOLPHIN_JUMP) {
            yieldFlags &= ~PLAYER_STATE1_IN_CUTSCENE;
        }

        if (yieldFlags) {
            // OOT has an active special action - yield to it
            if (gFormState.goronAction != MMFORM_ACT_OOT_ACTION) {
                // Entering yield: clean up form-specific state before OOT takes control

                // Clean up ball form state (shape rotation, flags, attack collider)
                if (gFormState.goronAction == GORON_ACT_GORON_ROLL ||
                    gFormState.goronAction == GORON_ACT_GORON_ROLL_JUMP ||
                    gFormState.goronAction == GORON_ACT_GORON_ROLL_POUND) {
                    player->actor.shape.rot.x = 0;
                    player->actor.shape.rot.z = 0;
                    player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);
                    player->actor.shape.shadowScale = gFormState.savedShadowScale;
                    player->stateFlags2 &=
                        ~(PLAYER_STATE2_DISABLE_ROTATION_Z_TARGET | PLAYER_STATE2_DISABLE_ROTATION_ALWAYS);
                    player->actor.bgCheckFlags &= ~0x800;
                    player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
                    MmForm_ClearRollAttack(player);
                }

                // Clean up swim state (pitch/roll, speed)
                if (gFormState.swimState != 0) {
                    gFormState.fastSwimActive = 0;
                    gFormState.swimPitch = 0;
                    gFormState.swimRoll = 0;
                    gFormState.swimRollSmoothed = 0;
                    gFormState.swimSpeedB48 = 0.0f;
                    gFormState.swimYawRate = 0;
                    player->actor.shape.rot.x = 0;
                    player->actor.shape.rot.z = 0;
                    gFormState.swimState = 0;
                }

                gFormState.goronAction = MMFORM_ACT_OOT_ACTION;
                gFormState.actionTimer = 0;
                // Don't zero linearVelocity during damage yield — OOT's knockback
                // system already set the correct knockback speed and we must not override it.
                if (!(yieldFlags & PLAYER_STATE1_DAMAGED)) {
                    player->linearVelocity = 0.0f;
                }
            }
            // Don't run form logic - OOT is in control.
            // jointTable copy happens in MmForm_Draw.
            return;
        }

        // Yield flags cleared - return to idle if we were yielding
        if (gFormState.goronAction == MMFORM_ACT_OOT_ACTION) {
            // Wait for OOT's one-shot animation to finish before accepting input.
            // OOT clears CLIMBING_LEDGE ~34 frames before the climb animation ends.
            // Without this check, the MM form returns to idle and accepts movement
            // while the climbing animation is still playing (player slides mid-climb).
            // Once OOT transitions to a loop animation (idle/walk), we know it's done.
            // Timeout after 120 frames (2 seconds) to prevent softlocks if OOT gets stuck.
            gFormState.actionTimer++;
            if (gFormState.actionTimer < 120 &&
                (player->skelAnime.mode == ANIMMODE_ONCE || player->skelAnime.mode == ANIMMODE_ONCE_INTERP)) {
                if (player->skelAnime.curFrame < Animation_GetLastFrame(player->skelAnime.animation) - 1.0f) {
                    player->linearVelocity = 0.0f;
                    return; // Stay yielded until OOT's animation finishes
                }
            }
            MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
            player->linearVelocity = 0.0f;
            return; // Don't process idle input on the transition frame
        }
    }

    // =========================================================================
    // Centralized Ground Detection
    //
    // From 2Ship: all action functions check bgCheckFlags to detect
    // landing/airborne transitions. We centralize this to handle
    // transitions from ANY action state consistently.
    //
    // bgCheckFlags & 1 = on ground (set by Actor_UpdateBgCheckInfo)
    // =========================================================================
    u8 onGround = MMFORM_ON_GROUND(player);
    u8 wasOnGround = gFormState.wasOnGround;
    gFormState.wasOnGround = onGround;

    // Deku water hop counter reset: resets to 5 every frame while on ground
    // From 2Ship z_player.c line 7572-7573: else { remainingHopsCounter = 5; }
    if (onGround && gFormState.currentForm == MM_PLAYER_FORM_DEKU) {
        gFormState.dekuHopsRemaining = 5;
    }

    // --- Leaving ground detection (ground → air) ---
    // If we were on ground last frame and now we're not, transition to jump/fall.
    // Skip if already in an airborne action (sidehop, backflip, jump, fall, jump kick).
    if (wasOnGround && !onGround) {
        s32 curAction = gFormState.goronAction;
        u8 isGroundAction =
            (curAction == GORON_ACT_IDLE || curAction == GORON_ACT_WALK || curAction == GORON_ACT_RUN ||
             curAction == GORON_ACT_PUNCH_END || curAction == MMFORM_ACT_ZTARGET_IDLE ||
             curAction == MMFORM_ACT_ZTARGET_WALK || curAction == MMFORM_ACT_ROLL || curAction == MMFORM_ACT_SHIELD);

        if (isGroundAction) {
            // Walked off edge or was launched - enter jump or fall
            if (player->actor.velocity.y > 0.0f) {
                LinkAnimationHeader* jumpAnim = gFormState.jumpAnim ? gFormState.jumpAnim : gFormState.idleAnim;
                MmForm_SetAction(MMFORM_ACT_JUMP, play, jumpAnim, 1.0f, ANIMMODE_ONCE);
            } else {
                LinkAnimationHeader* fallAnim = gFormState.fallAnim ? gFormState.fallAnim : gFormState.idleAnim;
                MmForm_SetAction(MMFORM_ACT_FALL, play, fallAnim, 1.0f, ANIMMODE_LOOP);
            }
        }
    }

    // --- Zora: air action entering water → swim ---
    // Jump attack, jump, fall, sidehop, backflip into water should start swimming,
    // not continue sinking with land gravity. Also covers OOT yield actions (e.g. items mid-air).
    if (gFormState.currentForm == MM_PLAYER_FORM_ZORA && gFormState.swimState == 0 &&
        player->actor.yDistToWater > ZORA_SWIM_THRESHOLD) {
        s32 airAct = gFormState.goronAction;
        if (airAct == MMFORM_ACT_JUMP || airAct == MMFORM_ACT_FALL || airAct == MMFORM_ACT_JUMP_KICK ||
            airAct == MMFORM_ACT_SIDEHOP || airAct == MMFORM_ACT_BACKFLIP || airAct == MMFORM_ACT_OOT_ACTION) {
            if (gFormState.jumpKickActive) {
                MmForm_DisableJumpKickQuads(player);
                gFormState.jumpKickActive = 0;
            }
            // Clear jump kick's PAUSE_ACTION_FUNC (EnterSwimIdle sets its own)
            player->stateFlags3 &= ~PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_SWIM);
            MmForm_EnterSwimIdle(player, play);
            MmForm_PlaySfx(player, MM_NA_SE_PL_ZORA_DIVE, NA_SE_PL_DIVE_BUBBLE);
        }
    }

    // --- Landing detection (air → ground) ---
    // If we were airborne last frame and now we're on ground, handle landing.
    if (!wasOnGround && onGround) {
        s32 curAction = gFormState.goronAction;
        u8 isAirAction =
            (curAction == MMFORM_ACT_JUMP || curAction == MMFORM_ACT_FALL || curAction == MMFORM_ACT_JUMP_KICK ||
             curAction == MMFORM_ACT_SIDEHOP || curAction == MMFORM_ACT_BACKFLIP);

        if (isAirAction) {
            // Disable any active damage quads (jump kick uses both [0] and [1])
            if (gFormState.jumpKickActive) {
                MmForm_DisableJumpKickQuads(player);
                gFormState.jumpKickActive = 0;
            }

            // Restore OOT control (jump kick pauses actionFunc)
            player->stateFlags3 &= ~PLAYER_STATE3_PAUSE_ACTION_FUNC;

            // Restore normal gravity (may have been overridden for jump kick)
            player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_NORMAL);

            // Select landing animation based on what we were doing and fall distance
            // From 2Ship func_80837134 (z_player.c line 7206):
            //   - Z-target evasive (sidehop/backflip): use specific end anim
            //   - fallDistance <= 80: short landing (minimal recovery)
            //   - fallDistance > 80: full landing animation
            s32 fallDist = player->fallDistance;

            if (curAction == MMFORM_ACT_JUMP_KICK && gFormState.jumpKickEnd != NULL) {
                // Jump kick landing → always play kick end recovery
                MmForm_SetAction(GORON_ACT_LAND, play, gFormState.jumpKickEnd, 1.0f, ANIMMODE_ONCE);
                MmForm_PlaySfx(player, MM_NA_SE_PL_LAND, NA_SE_PL_LAND);
            } else if (curAction == MMFORM_ACT_SIDEHOP) {
                // Sidehop landing: straight to idle (no end anim for transformations)
                MmForm_PlaySfx(player, MM_NA_SE_PL_LAND, NA_SE_PL_LAND);
                if (MmForm_IsZTargeting(player)) {
                    LinkAnimationHeader* ztAnim =
                        gFormState.ztargetIdleR ? gFormState.ztargetIdleR : gFormState.idleAnim;
                    MmForm_SetAction(MMFORM_ACT_ZTARGET_IDLE, play, ztAnim, 1.0f, ANIMMODE_LOOP);
                } else {
                    MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
                }
            } else if (curAction == MMFORM_ACT_BACKFLIP) {
                // Backflip landing: straight to idle (no end anim for transformations)
                MmForm_PlaySfx(player, MM_NA_SE_PL_LAND, NA_SE_PL_LAND);
                if (MmForm_IsZTargeting(player)) {
                    LinkAnimationHeader* ztAnim =
                        gFormState.ztargetIdleR ? gFormState.ztargetIdleR : gFormState.idleAnim;
                    MmForm_SetAction(MMFORM_ACT_ZTARGET_IDLE, play, ztAnim, 1.0f, ANIMMODE_LOOP);
                } else {
                    MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
                }
            } else {
                // Normal landing from jump/fall
                // From 2Ship: fallDistance <= 80 → short landing, else full landing
                if (fallDist <= 80) {
                    LinkAnimationHeader* landAnim = gFormState.shortLanding;
                    if (landAnim == NULL)
                        landAnim = gFormState.idleAnim;
                    MmForm_SetAction(GORON_ACT_LAND, play, landAnim, 1.5f, ANIMMODE_ONCE);
                } else {
                    LinkAnimationHeader* landAnim = gFormState.landing;
                    if (landAnim == NULL)
                        landAnim = gFormState.shortLanding;
                    if (landAnim == NULL)
                        landAnim = gFormState.idleAnim;
                    MmForm_SetAction(GORON_ACT_LAND, play, landAnim, 1.0f, ANIMMODE_ONCE);
                }
                MmForm_PlaySfx(player, MM_NA_SE_PL_LAND, NA_SE_PL_LAND);
            }

            // Clear linear velocity on hard landing only
            if (fallDist > 80) {
                Math_StepToF(&player->linearVelocity, 0.0f, 3.0f);
            }
        }
    }

    // --- Ledge detection ---
    // From 2Ship Player_ActionHandler_12 (line 6330): Goron CANNOT grab ledges/corners.
    // In MM, climbing is also blocked, but user wants Goron to climb surfaces.
    // Only non-Goron forms can grab ledges.
    // Require minimum 6 frames of falling + minimum fallDistance to prevent false triggers
    // from walking on uneven terrain (brief airborne moments would cause climb animation).
    if (gFormState.currentForm != MM_PLAYER_FORM_GORON && !onGround && gFormState.goronAction == MMFORM_ACT_FALL &&
        gFormState.actionTimer >= 6 && player->fallDistance > 20) {
        if (player->yDistToLedge > 10.0f && player->yDistToLedge < 70.0f && player->actor.wallBgId != BGCHECK_SCENE &&
            gFormState.ledgeHang != NULL) {
            // Grab ledge
            player->actor.velocity.y = 0.0f;
            player->linearVelocity = 0.0f;
            player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_LEDGE);
            MmForm_SetAction(MMFORM_ACT_LEDGE_HANG, play, gFormState.ledgeHang, 1.0f, ANIMMODE_ONCE);
            MmForm_PlaySfx(player, MM_NA_SE_PL_CLIMB_CLIFF, NA_SE_PL_CLIMB_CLIFF);
        }
    }

    // --- Goron ground-based ledge jump (medium ledges) ---
    // From OOT Player_ActionHandler_12 (line 5061): small ledge (type 1) is handled by OOT's
    // else-if branch with func_808389E8. But we block medium/high (type >= 2) for Goron in
    // Player_ActionHandler_12. So Goron medium ledges (type 2) are unhandled by OOT.
    // Here we give Goron a ground-based jump for medium ledges, matching the small jump formula.
    // From 2Ship: Goron treats medium as small (no separate climb animation).
    if (gFormState.currentForm == MM_PLAYER_FORM_GORON && onGround && player->ledgeClimbType == 2 &&
        player->ledgeClimbDelayTimer >= 3) {
        f32 jumpVel = (player->yDistToLedge * 0.08f) + 5.5f;
        player->actor.velocity.y = jumpVel;
        player->linearVelocity = 2.5f;
        player->actor.bgCheckFlags &= ~1;
        Player_PlayJumpingSfx(player);
        Player_PlayVoiceSfx(player, NA_SE_VO_LI_SWORD_N);
        LinkAnimationHeader* jumpAnim = gFormState.jumpAnim ? gFormState.jumpAnim : gFormState.idleAnim;
        MmForm_SetAction(MMFORM_ACT_JUMP, play, jumpAnim, 1.0f, ANIMMODE_ONCE);
    }

    // =========================================================================
    // Pre-dispatch: Midair Deku Leaf check (runs regardless of current action)
    // In MM, C-button item usage is checked globally. We check here so midair
    // (jump/fall/sidehop/backflip) states can trigger flight.
    // =========================================================================
    if (gFormState.currentForm == MM_PLAYER_FORM_DEKU && gFormState.dekuFlightLaunch != NULL &&
        !MMFORM_ON_GROUND(player) && gFormState.goronAction != MMFORM_ACT_DEKU_FLY &&
        gFormState.goronAction != MMFORM_ACT_DEKU_FLOWER && gFormState.goronAction != MMFORM_ACT_DEKU_FALL_LOCKED) {
        if (ItemHeld_IsButtonPressed(ITEM_DEKU_LEAF, player, play)) {
            MmForm_StartDekuFlightMidair(player, play);
        }
    }

    // =========================================================================
    // Underwater floor mode (dive active, Zora on ocean floor using land controls)
    // A = surface (deactivate dive, return to free swim)
    // B = normal attack (land controls)
    // Z-target + stick = roll (same as surface)
    // If walked off underwater ledge, re-enter swim idle to sink again.
    // If walked out of water entirely, exit swim mode.
    // =========================================================================
    if (gFormState.currentForm == MM_PLAYER_FORM_ZORA && gFormState.swimState > 0 && gFormState.zoraBoots == 1) {
        s32 act = gFormState.goronAction;
        u8 isSwimAction = (act == MMFORM_ACT_SWIM_IDLE || act == MMFORM_ACT_SWIM_MOVE || act == MMFORM_ACT_SWIM_FAST ||
                           act == MMFORM_ACT_SWIM_DASH || act == MMFORM_ACT_SWIM_SURFACE_WALK ||
                           act == MMFORM_ACT_SWIM_UNDERWATER_WALK || act == MMFORM_ACT_DOLPHIN_JUMP);
        // Intentional air actions (sidehop, backflip, roll) should complete without interference.
        // Only check "walked off ledge" for normal ground actions (idle, walk, Z-target).
        u8 isIntentionalAir = (act == MMFORM_ACT_SIDEHOP || act == MMFORM_ACT_BACKFLIP || act == MMFORM_ACT_ROLL ||
                               act == MMFORM_ACT_JUMP_KICK || act == MMFORM_ACT_JUMP || act == MMFORM_ACT_FALL);
        if (!isSwimAction && !isIntentionalAir) {
            // Using land controls on the ocean floor
            if (player->actor.yDistToWater <= 0.0f) {
                // Walked out of water → exit underwater mode entirely
                gFormState.swimState = 0;
                gFormState.zoraBoots = 0;
                player->stateFlags2 &= ~PLAYER_STATE2_DISABLE_ROTATION_ALWAYS;
                player->stateFlags3 &= ~PLAYER_STATE3_PAUSE_ACTION_FUNC;
            } else if (!MMFORM_ON_GROUND(player)) {
                // Walked off underwater ledge → sink back down (keep dive mode active)
                player->actor.gravity = MmForm_GetGravity(MMFORM_GRAVITY_SWIM);
                MmForm_SetAction(MMFORM_ACT_SWIM_IDLE, play,
                                 gFormState.swimWaitAnim ? gFormState.swimWaitAnim : gFormState.idleAnim, 1.0f,
                                 ANIMMODE_LOOP);
                // Keep zoraBoots = 1 so buoyancy sinks us back to the next floor
            } else {
                // On the floor: A = surface + fast swim (A is the swim button)
                // But NOT when Z-targeting — Z-target+A should do roll/sidehop/backflip
                // (handled by MmForm_Action_ZTargetIdle in the action dispatch)
                Input* input = &play->state.input[0];
                if (CHECK_BTN_ALL(input->press.button, BTN_A) && !MmForm_IsZTargeting(player)) {
                    // Deactivate dive → return to swim_wait (same as MM boot toggle).
                    // User can then hold A to enter fast swim normally.
                    // MmForm_EnterSwimIdle sets DISABLE_ROTATION_ALWAYS (prevents OOT yaw interference)
                    player->actor.shape.rot.x = 0;
                    MmForm_EnterSwimIdle(player, play);
                    MmForm_PlaySfx(player, MM_NA_SE_PL_ZORA_SWIM, NA_SE_PL_DIVE_BUBBLE);
                    return;
                }
                // Bubble effects while on the ocean floor
                MmForm_SwimEffects(player, play);
            }
        }
    }

    // =========================================================================
    // Pre-dispatch: Swim OOT isolation
    // OOT's Player_UpdateCommon runs BEFORE us each frame. Without intervention:
    //   - actionFunc sets yaw/linearVelocity from stick (ground movement logic)
    //   - Movement pipeline uses those to move the player in the wrong direction
    //   - Player_UpdateShapeYaw modifies shape.rot.y
    // Fix: PAUSE_ACTION_FUNC blocks actionFunc next frame (must re-set every frame
    // because Player_UpdateCommon clears it). DISABLE_ROTATION_ALWAYS blocks
    // Player_UpdateShapeYaw. Restore yaw from our shape.rot.y (untouched by OOT).
    // =========================================================================
    if (gFormState.currentForm == MM_PLAYER_FORM_ZORA && gFormState.swimState > 0 && gFormState.zoraBoots == 0) {
        player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
        player->yaw = player->actor.shape.rot.y;
        player->actor.world.rot.y = player->actor.shape.rot.y;
    }

    // =========================================================================
    // Action Dispatch
    // =========================================================================
    switch (gFormState.goronAction) {
        case GORON_ACT_IDLE:
            MmForm_GoronAction_Idle(player, play);
            break;
        case GORON_ACT_WALK:
            MmForm_GoronAction_Walk(player, play);
            break;
        case GORON_ACT_RUN:
            MmForm_GoronAction_Run(player, play);
            break;

        // Punch combo (from 2Ship Player_Action_84) - works for Goron AND Zora
        case GORON_ACT_PUNCH_A:
        case GORON_ACT_PUNCH_B:
        case GORON_ACT_PUNCH_C:
            MmForm_Action_Punch(player, play);
            break;
        case GORON_ACT_PUNCH_END:
            MmForm_GoronAction_PunchEnd(player, play);
            break;

        // Goron Roll System (from 2Ship Player_Action_96)
        case GORON_ACT_ROLL_INIT:
            // Curl animation (pg_maru_change) → enter ball roll when done
            // From 2Ship func_80839F98: plays at 2/3 speed, 7 frames
            if (gFormState.formSkelAnime.animation == NULL) {
                MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
                break;
            }
            if (gFormState.formSkelAnime.curFrame >=
                Animation_GetLastFrame(gFormState.formSkelAnime.animation) - 1.0f) {
                // Enter ball roll mode (from 2Ship func_80857A44, line 19387-19399)
                gFormState.rollSpinRate = (s16)(player->linearVelocity * 500.0f); // av2 = speedXZ * 500
                gFormState.rollBallSpeed = player->linearVelocity;                // unk_B08 = speedXZ
                gFormState.rollBounce = 0.0f;
                gFormState.rollTilt = 0.0f;
                gFormState.rollSquash = 0.0f;
                gFormState.rollHomeYaw = player->actor.shape.rot.y;
                gFormState.rollChargeLevel = 4; // Start at 4 (from 2Ship: av1.actionVar1 = 4)
                gFormState.rollSpikeActive = 0;
                gFormState.rollSfxCounter = 0;
                gFormState.rollWallBounceTimer = 0;
                gFormState.rollNoInputTimer = 0;
                gFormState.rollGroundPoundTimer = 0;
                gFormState.goronAction = GORON_ACT_GORON_ROLL;
                gFormState.actionTimer = 0;
                // Shadow: smaller circle during ball mode (from 2Ship z_player.c line 13353)
                // MmForm_UpdateActive sets DrawCircle per-frame for ball actions
                gFormState.savedShadowScale = player->actor.shape.shadowScale;
                player->actor.shape.shadowScale = 30.0f;
                // State flags: disable rotation during roll (from 2Ship stateFlags3 0x200)
                player->stateFlags2 |=
                    (PLAYER_STATE2_DISABLE_ROTATION_Z_TARGET | PLAYER_STATE2_DISABLE_ROTATION_ALWAYS);
                // bgCheckFlags: ball mode flag for collision system
                player->actor.bgCheckFlags |= 0x800;
                // Restrict OOT action handlers during roll (from 2Ship sActionHandlerList12)
                // Setting INPUT_DISABLED zeros OOT's input copy, preventing OOT from
                // starting attacks/dodges/etc. Our code reads play->state.input[0] directly.
                player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
            }
            break;

        case GORON_ACT_GORON_ROLL:
        case GORON_ACT_GORON_ROLL_JUMP:
        case GORON_ACT_GORON_ROLL_POUND:
            MmForm_Action_GoronRoll(player, play);
            break;

        case GORON_ACT_ROLL_UNCURL:
            // Uncurl animation (pg_maru_change reversed) → idle when done
            // From 2Ship func_80857950: plays at negative speed from frame 7→0
            Math_StepToF(&player->linearVelocity, 0.0f, 2.0f);
            if (gFormState.formSkelAnime.curFrame <= 1.0f) {
                MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
                player->linearVelocity = 0.0f;
            }
            break;

        // Phase 5: Damage knockback
        case GORON_ACT_DAMAGE:
            MmForm_GoronAction_Damage(player, play);
            break;

        case GORON_ACT_LAND:
            // Landing recovery: decelerate and return to idle when animation finishes
            Math_StepToF(&player->linearVelocity, 0.0f, DAMAGE_DECEL_RATE);
            if (gFormState.formSkelAnime.animation == NULL ||
                gFormState.formSkelAnime.curFrame >= Animation_GetLastFrame(gFormState.formSkelAnime.animation)) {
                MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
            }
            break;

        // Ground system: airborne actions
        case MMFORM_ACT_JUMP:
            MmForm_Action_Jump(player, play);
            break;
        case MMFORM_ACT_FALL:
            MmForm_Action_Fall(player, play);
            break;
        case MMFORM_ACT_JUMP_KICK:
            MmForm_Action_JumpKick(player, play);
            break;
        case MMFORM_ACT_SIDEHOP:
            MmForm_Action_Sidehop(player, play);
            break;
        case MMFORM_ACT_BACKFLIP:
            MmForm_Action_Backflip(player, play);
            break;

        // Ground system: ground actions
        case MMFORM_ACT_ROLL:
            MmForm_Action_Roll(player, play);
            break;
        case MMFORM_ACT_ZTARGET_IDLE:
            MmForm_Action_ZTargetIdle(player, play);
            break;
        case MMFORM_ACT_ZTARGET_WALK:
            MmForm_Action_ZTargetWalk(player, play);
            break;

        // Ledge actions
        case MMFORM_ACT_LEDGE_HANG:
            MmForm_Action_LedgeHang(player, play);
            break;
        case MMFORM_ACT_LEDGE_CLIMB:
            MmForm_Action_LedgeClimb(player, play);
            break;

        // Shield stance (from 2Ship Player_Action_18, z_player.c:14876-14980)
        // Goron: gLinkGoronShieldingSkel (separate 4-limb skeleton) + shieldCollider AC
        // Zora/Deku: formSkelAnime with link_normal_defense → defense_wait loop
        //       + barrier activation via R+B for Zora (func_8082F164, line 14914)
        //       + NO AC shieldCollider — just shrinks main cylinder (line 12797)
        // All forms: body yaw locked every frame (ball-and-chain pattern)
        case MMFORM_ACT_SHIELD: {
            Input* shieldInput = &play->state.input[0];
            player->linearVelocity = 0.0f;

            if (gFormState.currentForm == MM_PLAYER_FORM_GORON) {
                // === GORON SHIELD (curl pose with separate skeleton) ===
                // SFX and SHIELDING flag set in MmForm_EnterShield at entry.

                // Lock body yaw every frame (ball-and-chain pattern)
                player->actor.shape.rot.y = sShieldLockedYaw;
                player->actor.world.rot.y = sShieldLockedYaw;
                player->yaw = sShieldLockedYaw;
                player->linearVelocity = 0.0f;
                player->actor.velocity.y = 0.0f;

                // Tick shield animation (separate SkelAnime)
                if (gFormState.shieldSkelLoaded) {
                    SkelAnime_Update(&gFormState.shieldSkelAnime);
                }

                // Register shield collider for damage protection (Goron only)
                // From 2Ship z_player.c line 12773-12794: AC_ON | AC_HARD | AC_TYPE_ENEMY
                if (gFormState.shieldColliderInitDone) {
                    Collider_UpdateCylinder(&player->actor, &gFormState.shieldCollider);
                    CollisionCheck_SetAC(play, &play->colChkCtx, &gFormState.shieldCollider.base);
                    player->cylinder.dim.yShift = 0;
                    player->cylinder.dim.height = gFormState.shieldCollider.dim.height;
                }
            } else if (gFormState.currentForm == MM_PLAYER_FORM_ZORA) {
                // === ZORA SHIELD (guard pose + forearm fin blades + barrier) ===
                // From 2Ship Player_Action_18 (line 14876-14980).
                // Animation set up in MmForm_EnterShield at entry.

                // Lock body yaw every frame (ball-and-chain pattern)
                player->actor.shape.rot.y = sShieldLockedYaw;
                player->actor.world.rot.y = sShieldLockedYaw;
                player->yaw = sShieldLockedYaw;
                player->linearVelocity = 0.0f;

                // Tick form skeleton animation
                if (LinkAnimation_Update(play, &gFormState.formSkelAnime)) {
                    // defense (ONCE) finished → transition to defense_wait (LOOP)
                    // From 2Ship line 14901: !Player_IsGoronOrDeku → defense_wait
                    if (gFormState.formSkelAnime.mode >= ANIMMODE_ONCE) {
                        LinkAnimation_PlayLoop(play, &gFormState.formSkelAnime,
                                               (LinkAnimationHeader*)gPlayerAnim_link_normal_defense_wait);
                    }
                    gFormState.shieldAv2 = 1;
                }

                // Shrink main cylinder by 0.8x (from 2Ship z_player.c line 12797)
                {
                    const MmFormProperties* props = &sFormProps[gFormState.currentForm];
                    player->cylinder.dim.height = (s16)(props->cylinderHeight * 0.8f);
                }
            } else if (gFormState.currentForm == MM_PLAYER_FORM_DEKU) {
                // === DEKU SHIELD (crouch guard with pn_gurd + shield DL scaling) ===
                // From 2Ship Player_Action_18 (line 14876-14980).
                // Deku: NO defense_wait (Player_IsGoronOrDeku), but YES directional control.
                // pn_gurd plays from frame 0, shield DL scales in during frames 0-3.

                // Lock body yaw every frame (ball-and-chain pattern)
                player->actor.shape.rot.y = sShieldLockedYaw;
                player->actor.world.rot.y = sShieldLockedYaw;
                player->yaw = sShieldLockedYaw;
                player->linearVelocity = 0.0f;

                // Tick form skeleton animation (plays to end and stays, no loop)
                if (LinkAnimation_Update(play, &gFormState.formSkelAnime)) {
                    // Animation finished — enable directional control (av2=1)
                    gFormState.shieldAv2 = 1;
                }

                // Shrink main cylinder by 0.8x (from 2Ship z_player.c line 12797)
                {
                    const MmFormProperties* props = &sFormProps[gFormState.currentForm];
                    player->cylinder.dim.height = (s16)(props->cylinderHeight * 0.8f);
                }
            }

            // === Directional control (ALL non-Goron forms) ===
            // From 2Ship Player_Action_18 (z_player.c line 14918-14940):
            //   Runs when av2 != 0 (animation finished at least once).
            //   var_a1 = (yStick * cos(relYaw)) + (sin(relYaw) * xStick)  → pitch
            //   temp_ft5 = (xStick * cos(relYaw)) - (sin(relYaw) * yStick) → yaw
            //   CLAMP_MAX(var_a1, 0xDAC)
            //   Adaptive step sizes: ABS(diff) * 0.25f, CLAMP_MIN 0x64 (pitch) / 0x32 (yaw)
            if (gFormState.currentForm != MM_PLAYER_FORM_GORON && gFormState.shieldAv2 != 0) {
                Input* input = &play->state.input[0];
                f32 stickY = input->rel.stick_y * 180.0f;
                f32 stickX = input->rel.stick_x * -120.0f;

                Camera* cam = GET_ACTIVE_CAM(play);
                s16 camDirYaw = Camera_GetInputDirYaw(cam);
                s16 relYaw = player->actor.shape.rot.y - camDirYaw;

                s16 targetPitch = (s16)((stickY * Math_CosS(relYaw)) + (Math_SinS(relYaw) * stickX));
                s16 targetYaw = (s16)((stickX * Math_CosS(relYaw)) - (Math_SinS(relYaw) * stickY));

                if (targetPitch > 0xDAC)
                    targetPitch = 0xDAC;

                s16 pitchStep = ABS(targetPitch - player->actor.focus.rot.x) * 0.25f;
                if (pitchStep < 0x64)
                    pitchStep = 0x64;
                s16 yawStep = ABS(targetYaw - player->upperLimbRot.y) * 0.25f;
                if (yawStep < 0x32)
                    yawStep = 0x32;

                Math_ScaledStepToS(&player->actor.focus.rot.x, targetPitch, pitchStep);
                player->upperLimbRot.x = player->actor.focus.rot.x;
                Math_ScaledStepToS(&player->upperLimbRot.y, targetYaw, yawStep);
            }

            // Tell OOT to NOT decay our rotation values to zero (from 2Ship line 14979)
            // Without this, func_80847298 approaches upperLimbRot and focus.rot.x to 0 every frame.
            player->unk_6AE_rotFlags |= UNK6AE_ROT_FOCUS_X | UNK6AE_ROT_UPPER_X | UNK6AE_ROT_UPPER_Y;

            // Stay in shield while R is held
            if (!CHECK_BTN_ALL(shieldInput->cur.button, BTN_R)) {
                player->stateFlags1 &= ~PLAYER_STATE1_SHIELDING;

                // Restore player cylinder to form defaults
                const MmFormProperties* props = &sFormProps[gFormState.currentForm];
                player->cylinder.dim.radius = (s16)props->cylinderRadius;
                player->cylinder.dim.height = (s16)props->cylinderHeight;
                player->cylinder.dim.yShift = (s16)props->cylinderYShift;

                if (gFormState.currentForm == MM_PLAYER_FORM_GORON) {
                    MmForm_PlaySfx(player, MM_NA_SE_PL_GORON_BALL_TO_GORON, NA_SE_PL_BODY_HIT);
                } else {
                    // From 2Ship Player_Action_18 (line 15356): NA_SE_IT_SHIELD_REMOVE
                    Audio_PlayActorSound2(&player->actor, NA_SE_IT_SHIELD_REMOVE);
                }
                // Return to idle — MmForm_SetAction's -8.0f morph smoothly blends
                // from defense pose to idle pose (same as MM's func_80836A98)
                MmForm_SetAction(GORON_ACT_IDLE, play, gFormState.idleAnim, 1.0f, ANIMMODE_LOOP);
            }
            break;
        }

        // Deku actions
        case MMFORM_ACT_DEKU_SPIN:
            MmForm_Action_DekuSpin(player, play);
            break;
        case MMFORM_ACT_DEKU_BUBBLE_AIM:
            MmForm_Action_DekuBubbleAim(player, play);
            break;
        case MMFORM_ACT_DEKU_BUBBLE:
            MmForm_Action_DekuBubble(player, play);
            break;

        // Deku Flower + Flight (from 2Ship Player_Action_93/94)
        case MMFORM_ACT_DEKU_FLOWER:
            MmForm_Action_DekuFlower(player, play);
            break;
        case MMFORM_ACT_DEKU_FLY:
            MmForm_Action_DekuFly(player, play);
            break;
        case MMFORM_ACT_DEKU_FALL_LOCKED:
            MmForm_Action_DekuFallLocked(player, play);
            break;

        // Zora Boomerang Fins (from 2Ship Player_UpperAction_12-16)
        // Only THROW is a blocking action (aim + throw anim). After that, player returns to idle
        // and boomerangs are tracked in background by MmForm_TrackBoomerangsInFlight().
        case MMFORM_ACT_BOOMERANG_THROW:
            MmForm_Action_BoomerangThrow(player, play);
            break;

        // Zora Swimming (from 2Ship Player_Action_54-58)
        case MMFORM_ACT_SWIM_IDLE:
            MmForm_Action_SwimIdle(player, play);
            break;
        case MMFORM_ACT_SWIM_MOVE:
            MmForm_Action_SwimMove(player, play);
            break;
        case MMFORM_ACT_SWIM_FAST:
            MmForm_Action_SwimFast(player, play);
            break;
        case MMFORM_ACT_SWIM_DASH:
            MmForm_Action_SwimDash(player, play);
            break;
        case MMFORM_ACT_SWIM_SURFACE_WALK:
            MmForm_Action_SwimSurfaceWalk(player, play);
            break;
        case MMFORM_ACT_SWIM_UNDERWATER_WALK:
            MmForm_Action_SwimUnderwaterWalk(player, play);
            break;
        case MMFORM_ACT_DOLPHIN_JUMP:
            MmForm_Action_DolphinJump(player, play);
            break;

        case MMFORM_ACT_CLIMB:
            // Climbing now yields to OOT via CLIMBING_LADDER in yield flags.
            // This case should not be reached; safety fallback.
            break;

        case MMFORM_ACT_WATER_VOID:
            MmForm_Action_WaterVoidOut(player, play);
            break;

        case MMFORM_ACT_HAZARD_VOID:
            MmForm_Action_HazardVoidOut(player, play);
            break;

        case MMFORM_ACT_OOT_ACTION:
            // OOT is running a special action - we yielded before reaching this switch.
            // This case should not be reached (early return above), but handle gracefully.
            break;
    }

    // Zora barrier: flag-based system (from 2Ship func_8082F164 + func_8082F1AC)
    // CheckBarrierInput: sets barrierActive when R held + Zora + magic > 0
    // UpdateBarrier: updates intensity, light, damage collider every frame
    // Both run regardless of current action so barrier works during walk/swim/etc.
    if (gFormState.currentForm == MM_PLAYER_FORM_ZORA) {
        MmForm_CheckBarrierInput(player, play);
    }
    MmForm_UpdateBarrier(player, play);

    // Deku bubble projectile: update physics every frame (independent of action state)
    MmForm_UpdateBubbleProjectile(player, play);

    // Zora boomerang tracking: runs in background while player moves freely (state 3)
    // From MM Player_UpperAction_15: runs every frame, checks if boomerangs returned.
    // Player is NOT locked — can walk, run, jump attack, etc. during flight.
    MmForm_TrackBoomerangsInFlight(player, play);

    // Always tick animation (separate from OOT's player->skelAnime)
    // Skip for actions that handle their own animation updates internally
    {
        s32 act = gFormState.goronAction;
        u8 selfTicking =
            (act == MMFORM_ACT_SHIELD || act == MMFORM_ACT_BOOMERANG_THROW || act == MMFORM_ACT_SWIM_IDLE ||
             act == MMFORM_ACT_SWIM_SURFACE_WALK || act == MMFORM_ACT_SWIM_FAST || act == MMFORM_ACT_SWIM_DASH ||
             act == MMFORM_ACT_SWIM_UNDERWATER_WALK || act == MMFORM_ACT_DEKU_SPIN ||
             act == MMFORM_ACT_DEKU_BUBBLE_AIM || act == MMFORM_ACT_DEKU_BUBBLE || act == MMFORM_ACT_DEKU_FLOWER ||
             act == MMFORM_ACT_DEKU_FLY || act == MMFORM_ACT_DEKU_FALL_LOCKED ||
             act == MMFORM_ACT_OOT_ACTION); // OOT handles its own animation
        if (!selfTicking) {
            LinkAnimation_Update(play, &gFormState.formSkelAnime);
        }
    }
}

// =============================================================================
// Transformation Cutscene
// =============================================================================

static void MmForm_UpdateTransforming(Player* player, PlayState* play) {
    u8 instantTransform = CVarGetInteger("gMods.TransformMasks.InstantTransform", 0) || sForceInstantTransform;

    if (instantTransform) {
        // Instant transform: 5-frame flash
        gFormState.cutsceneTimer++;

        if (gFormState.cutsceneTimer == 1) {
            // Frame 1: Start flash, load skeleton
            gFormState.flashAlpha = 0;
            player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
            player->linearVelocity = 0.0f;

            if (!MmForm_LoadFormSkeleton(play, gFormState.targetForm)) {
                MMFORM_LOG("[MmForm] Skeleton load failed, aborting transform");
                player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
                gFormState.state = MMFORM_STATE_INACTIVE;
                MmForm_RestoreEquips(play);
                return;
            }

            MmForm_ApplyFormProperties(player, gFormState.targetForm);
            gFormState.currentForm = gFormState.targetForm;

            // Play flash SFX
#if !MM_SOUNDS_DISABLED
            if (MmSfx_IsAvailable()) {
                MmSfx_PlayTransformFlash();
            }
#endif
        }

        // Build flash up
        if (gFormState.cutsceneTimer <= 3) {
            gFormState.flashAlpha += 85;
            if (gFormState.flashAlpha > 255)
                gFormState.flashAlpha = 255;
        } else {
            // Fade flash down
            gFormState.flashAlpha -= 85;
            if (gFormState.flashAlpha < 0)
                gFormState.flashAlpha = 0;
        }

        // Done after 5 frames
        if (gFormState.cutsceneTimer >= 5) {
            gFormState.flashAlpha = 0;
            player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
            sForceInstantTransform = 0;

            // Clear IN_CUTSCENE from scene transition. The fade-in effect is managed
            // by play->transitionMode and renders regardless of this flag. Without
            // clearing it, the player is stuck: OOT zeros all input during IN_CUTSCENE,
            // pause is blocked, and exiting water causes a softlock from the yield handler.
            player->stateFlags1 &= ~PLAYER_STATE1_IN_CUTSCENE;

            gFormState.state = MMFORM_STATE_ACTIVE;
            MmForm_SaveAndRestrictEquips(play);

            // After scene-transition reactivation: if Zora is underwater, force swim entry
            // immediately so there's no gap where the player is stuck standing underwater.
            // OOT may have started its own swim action during the 5-frame transform delay;
            // our swim entry takes precedence and the form handles water from here.
            if (gFormState.currentForm == MM_PLAYER_FORM_ZORA && player->actor.yDistToWater > ZORA_SWIM_THRESHOLD) {
                MmForm_EnterSwimIdle(player, play);
                player->stateFlags1 |= PLAYER_STATE1_IN_WATER;
                MMFORM_LOG("[MmForm] Zora reactivated underwater, forced swim entry");
            }
        }
    } else {
        // Full cutscene (from 2Ship Player_Action_86)
        gFormState.cutsceneTimer++;

        switch (gFormState.cutscenePhase) {
            case 0: // Pre-flash: freeze player, play maskoff anim
                if (gFormState.cutsceneTimer == 1) {
                    player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
                    player->linearVelocity = 0.0f;
                    player->actor.velocity.y = 0.0f;

                    // Face away from camera
                    player->actor.shape.rot.y = Camera_GetCamDirYaw(GET_ACTIVE_CAM(play)) + 0x8000;
                    player->yaw = player->actor.shape.rot.y;

                    // Play transform voice SFX at frame 30 (handled below)
                }

                // SFX at specific frames (from 2Ship D_8085D8F0)
                if (gFormState.cutsceneTimer == 4) {
                    // NA_SE_IT_SET_TRANSFORM_MASK - putting on mask
                    Audio_PlaySoundGeneral(NA_SE_PL_PUT_OUT_ITEM, &player->actor.projectedPos, 4,
                                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                           &gSfxDefaultReverb);
                }

                // Build toward flash
                if (gFormState.cutsceneTimer >= 40) {
                    gFormState.cutscenePhase = 1;
                }
                break;

            case 1: // Flash build-up
                gFormState.flashAlpha += 45;
                if (gFormState.flashAlpha >= 255) {
                    gFormState.flashAlpha = 255;

                    // At peak flash: switch skeleton
                    if (!MmForm_LoadFormSkeleton(play, gFormState.targetForm)) {
                        MMFORM_LOG("[MmForm] Skeleton load failed during cutscene");
                        gFormState.flashAlpha = 0;
                        player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
                        gFormState.state = MMFORM_STATE_INACTIVE;
                        MmForm_RestoreEquips(play);
                        return;
                    }

                    MmForm_ApplyFormProperties(player, gFormState.targetForm);
                    gFormState.currentForm = gFormState.targetForm;

                    // Play flash SFX
#if !MM_SOUNDS_DISABLED
                    if (MmSfx_IsAvailable()) {
                        MmSfx_PlayTransformFlash();
                    }
#endif
                    Audio_PlaySoundGeneral(NA_SE_SY_HP_RECOVER, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);

                    gFormState.cutscenePhase = 2;
                }
                break;

            case 2: // Post-flash: fade, play idle, unfreeze
                gFormState.flashAlpha -= 20;
                if (gFormState.flashAlpha <= 0) {
                    gFormState.flashAlpha = 0;
                    player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
                    sForceInstantTransform = 0;

                    gFormState.state = MMFORM_STATE_ACTIVE;
                    MmForm_SaveAndRestrictEquips(play);
                }

                // Tick idle animation during fade
                LinkAnimation_Update(play, &gFormState.formSkelAnime);
                break;
        }
    }
}

static void MmForm_UpdateDetransforming(Player* player, PlayState* play) {
    u8 instantTransform = CVarGetInteger("gMods.TransformMasks.InstantTransform", 0);

    if (instantTransform) {
        // Instant de-transform: 5-frame flash
        gFormState.cutsceneTimer++;

        if (gFormState.cutsceneTimer == 1) {
            gFormState.flashAlpha = 0;
            player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
            player->linearVelocity = 0.0f;

            // Play flash SFX
#if !MM_SOUNDS_DISABLED
            if (MmSfx_IsAvailable()) {
                MmSfx_PlayTransformFlash();
            }
#endif
        }

        if (gFormState.cutsceneTimer <= 3) {
            gFormState.flashAlpha += 85;
            if (gFormState.flashAlpha > 255)
                gFormState.flashAlpha = 255;
        } else {
            gFormState.flashAlpha -= 85;
            if (gFormState.flashAlpha < 0)
                gFormState.flashAlpha = 0;
        }

        // At flash peak, restore OOT state
        if (gFormState.cutsceneTimer == 3) {
            MmForm_RestoreOotState(player);
            gFormState.currentForm = MM_PLAYER_FORM_HUMAN;
            gFormState.skeletonLoaded = 0;
        }

        if (gFormState.cutsceneTimer >= 5) {
            gFormState.flashAlpha = 0;
            player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
            gFormState.state = MMFORM_STATE_INACTIVE;
            MmForm_RestoreEquips(play);
        }
    } else {
        // Full de-transform cutscene
        gFormState.cutsceneTimer++;

        switch (gFormState.cutscenePhase) {
            case 0: // Pre-flash
                if (gFormState.cutsceneTimer == 1) {
                    player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
                    player->linearVelocity = 0.0f;
                    player->actor.velocity.y = 0.0f;
                    player->actor.shape.rot.y = Camera_GetCamDirYaw(GET_ACTIVE_CAM(play)) + 0x8000;
                    player->yaw = player->actor.shape.rot.y;
                }

                if (gFormState.cutsceneTimer >= 30) {
                    gFormState.cutscenePhase = 1;
                }

                // Tick animation during pre-flash
                LinkAnimation_Update(play, &gFormState.formSkelAnime);
                break;

            case 1: // Flash build
                gFormState.flashAlpha += 45;
                if (gFormState.flashAlpha >= 255) {
                    gFormState.flashAlpha = 255;

                    // At flash: restore OOT skeleton
                    MmForm_RestoreOotState(player);
                    gFormState.currentForm = MM_PLAYER_FORM_HUMAN;
                    gFormState.skeletonLoaded = 0;

#if !MM_SOUNDS_DISABLED
                    if (MmSfx_IsAvailable()) {
                        MmSfx_PlayTransformFlash();
                    }
#endif

                    gFormState.cutscenePhase = 2;
                }
                break;

            case 2: // Post-flash: fade, unfreeze
                gFormState.flashAlpha -= 20;
                if (gFormState.flashAlpha <= 0) {
                    gFormState.flashAlpha = 0;
                    player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
                    gFormState.state = MMFORM_STATE_INACTIVE;
                    MmForm_RestoreEquips(play);
                }
                break;
        }
    }
}

// =============================================================================
// OOT Animation Sharing
//
// Most MM form actions use "link_normal_*" animations which are IDENTICAL to OOT's.
// Instead of loading them from mm.o2r and playing on formSkelAnime separately,
// we copy OOT's player->skelAnime.jointTable directly. This ensures perfect sync
// with OOT's animation blending, upper/lower body system, and timing.
//
// Only form-specific actions (pg_*, pz_*, pn_*) use formSkelAnime's own animation.
//
// From MM decomp D_8085BE84: walk, run, jump, fall, roll, z-target, sidehop,
// backflip, ledge, damage, landing are ALL shared link_normal_* animations.
// Form-specific: door, chest, climb (Goron/Zora), mask off, idle (Goron/Zora).
// =============================================================================

static u8 MmForm_UsesOotAnim(void) {
    s32 act = gFormState.goronAction;

    // Generic OOT yield (items, dialogue, carrying, etc.)
    if (act == MMFORM_ACT_OOT_ACTION)
        return 1;

    // Shared link_normal_* / link_fighter_* animations played by OOT's skelAnime.
    // Note: SIDEHOP, BACKFLIP, ROLL are NOT here — they play MM anims on formSkelAnime.
    // DAMAGE and LAND: OOT handles all knockback/damage natively, form yields to OOT.
    if (act == GORON_ACT_WALK || act == GORON_ACT_RUN || act == MMFORM_ACT_JUMP || act == MMFORM_ACT_FALL ||
        act == MMFORM_ACT_ZTARGET_IDLE || act == MMFORM_ACT_ZTARGET_WALK || act == MMFORM_ACT_LEDGE_HANG ||
        act == MMFORM_ACT_LEDGE_CLIMB || act == GORON_ACT_DAMAGE || act == GORON_ACT_LAND) {
        return 1;
    }

    // Idle: Goron has pg_wait, Zora has pz_wait (form-specific).
    // Deku and FD use link_normal_wait_free / link_fighter_wait_long (shared).
    if (act == GORON_ACT_IDLE) {
        return (gFormState.currentForm != MM_PLAYER_FORM_GORON && gFormState.currentForm != MM_PLAYER_FORM_ZORA) ? 1
                                                                                                                 : 0;
    }

    // Climb: Goron has pg_climb_*, Zora has pz_climb_* (form-specific).
    // Climbing yields to OOT (CLIMBING_LADDER in yield flags), so act == MMFORM_ACT_OOT_ACTION
    // is already handled above. No special MMFORM_ACT_CLIMB check needed.

    // Everything else is form-specific (punch, roll, swim, spin, boomerang,
    // door, chest, shield, mask off, water void, etc.)
    return 0;
}

// =============================================================================
// Draw System
// =============================================================================

static s32 MmForm_OverrideLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* thisx) {
    Player* player = (Player*)thisx;

    // From 2Ship Player_OverrideLimbDrawGameplayCommon (z_player_lib.c line 2419):
    // Scale root position (jointTable[0]) by per-form rootAnimScale.
    if (limbIndex == 1) { // limbIndex 1 = root limb (SkelAnime uses 1-based indexing)
        s32 form = (s32)gFormState.currentForm;
        if (form >= 0 && form < MM_PLAYER_FORM_MAX) {
            if (form != MM_PLAYER_FORM_FIERCE_DEITY) {
                f32 scale = sFormProps[form].rootAnimScale;
                pos->x *= scale;
                pos->y *= scale;
                pos->z *= scale;
            }
        }

        // From MM z_player_lib.c:2388-2404 — swim pitch/roll for Zora fast swim.
        // MM manually takes over the root limb transform: translate (with pitch Y offset),
        // then pitch rotation, then roll rotation, then original rotation.
        // We zero pos/rot afterward so SkelAnime doesn't double-apply them.
        if (gFormState.currentForm == MM_PLAYER_FORM_ZORA && gFormState.fastSwimActive) {
            Vec3f origPos = *pos;
            Vec3s origRot = *rot;

            // Y offset: model dips when pitching (from MM: (cos(pitch) - 1.0) * 200.0)
            f32 yAdj = (Math_CosS(gFormState.swimPitch) - 1.0f) * 200.0f;

            Matrix_Translate(origPos.x, yAdj + origPos.y, origPos.z, MTXMODE_APPLY);
            Matrix_RotateX(BINANG_TO_RAD(gFormState.swimPitch), MTXMODE_APPLY);
            Matrix_RotateZ(BINANG_TO_RAD(gFormState.swimRollSmoothed), MTXMODE_APPLY);
            Matrix_RotateZYX(origRot.x, origRot.y, origRot.z, MTXMODE_APPLY);

            // Zero so SkelAnime's TranslateRotateZYX is identity (no double-apply)
            pos->x = 0.0f;
            pos->y = 0.0f;
            pos->z = 0.0f;
            rot->x = 0;
            rot->y = 0;
            rot->z = 0;
        }
    }

    // From OOT z_player_lib.c:1378-1380 — apply headLimbRot to HEAD limb
    if (limbIndex == PLAYER_LIMB_HEAD) {
        rot->x += player->headLimbRot.z;
        rot->y -= player->headLimbRot.y;
        rot->z += player->headLimbRot.x;
    }

    // From OOT z_player_lib.c:1387-1400 — apply upperLimbRot to UPPER limb
    // This is CRITICAL for shield directional control (stick → pitch/yaw).
    // Without these Matrix_Rotate calls, upperLimbRot values are set but never
    // visually applied, so the upper body doesn't move with the stick.
    if (limbIndex == PLAYER_LIMB_UPPER) {
        if (player->upperLimbRot.y != 0) {
            Matrix_RotateY(player->upperLimbRot.y * (M_PI / 0x8000), MTXMODE_APPLY);
        }
        if (player->upperLimbRot.x != 0) {
            Matrix_RotateX(player->upperLimbRot.x * (M_PI / 0x8000), MTXMODE_APPLY);
        }
        if (player->upperLimbRot.z != 0) {
            Matrix_RotateZ(player->upperLimbRot.z * (M_PI / 0x8000), MTXMODE_APPLY);
        }
    }

    // =========================================================================
    // Fierce Deity: swap hand DLs based on held item state
    // =========================================================================
    // OOT's Player_SetModels sets leftHandType/rightHandType based on equipped items.
    // We map those to FD-specific hand DLs from mm.o2r.
    // Items FD can use: Swords, Hammer, Fire/Ice/Light Rods, Ball and Chain, Bottles, Nuts.
    // For items with model baked into hand DL (Hammer), don't override — let OOT draw it.
    // For items drawn separately (Rods via CustomItems_Draw), use FD empty hand.
    if (gFormState.currentForm == MM_PLAYER_FORM_FIERCE_DEITY) {
        if (limbIndex == PLAYER_LIMB_L_HAND) {
            Gfx* fdDL = NULL;
            switch (player->leftHandType) {
                case PLAYER_MODELTYPE_LH_SWORD:
                case PLAYER_MODELTYPE_LH_SWORD_2:
                case PLAYER_MODELTYPE_LH_BGS:
                    // Holding a sword → show FD sword hand
                    fdDL = MmForm_GetFDHandDL(play, FD_DL_LEFT_HAND_SWORD);
                    break;
                case PLAYER_MODELTYPE_LH_BOTTLE:
                    // Holding a bottle → show FD bottle hand
                    fdDL = MmForm_GetFDHandDL(play, FD_DL_LEFT_HAND_BOTTLE);
                    break;
                case PLAYER_MODELTYPE_LH_HAMMER:
                    // Hammer model is baked into OOT's LH_HAMMER DL → don't override.
                    // Human Link's hand mesh shows, but hammer stays visible.
                    break;
                default:
                    // OPEN, CLOSED, BOOMERANG → FD empty hand.
                    // Rods/custom items are drawn separately (CustomItems_Draw).
                    fdDL = MmForm_GetFDHandDL(play, FD_DL_LEFT_HAND_EMPTY);
                    break;
            }
            if (fdDL != NULL) {
                *dList = fdDL;
            }
        } else if (limbIndex == PLAYER_LIMB_R_HAND) {
            // Right hand: show FD empty hand when in sword stance (no shield for FD).
            // For other items (hammer etc.), let OOT's right hand DL through.
            if (player->leftHandType == PLAYER_MODELTYPE_LH_SWORD ||
                player->leftHandType == PLAYER_MODELTYPE_LH_SWORD_2 ||
                player->leftHandType == PLAYER_MODELTYPE_LH_BGS || player->leftHandType == PLAYER_MODELTYPE_LH_OPEN) {
                Gfx* fdDL = MmForm_GetFDHandDL(play, FD_DL_RIGHT_HAND_EMPTY);
                if (fdDL != NULL) {
                    *dList = fdDL;
                }
            }
        } else if (limbIndex == PLAYER_LIMB_SHEATH) {
            // FD has no scabbard/sheath on back
            *dList = NULL;
        }
    }

    return 0;
}

// From z_player_lib.c - global (not static) used by Player_DrawGetItem to position
// the get-item model at the correct hand position during skeleton draw.
extern "C" Vec3f sGetItemRefPos;

// Mapping from PLAYER_LIMB index to PLAYER_BODYPART index.
// Mirrors OOT's D_80160000 system (z_player_lib.c:1340) which fills bodyPartsPos
// sequentially during skeleton traversal. We use an explicit table instead.
// -1 = no bodypart mapping (ROOT, LOWER, UPPER have no visible geometry).
static const s8 sLimbToBodyPart[PLAYER_LIMB_MAX] = {
    -1,                         // 0x00 PLAYER_LIMB_NONE
    -1,                         // 0x01 PLAYER_LIMB_ROOT
    PLAYER_BODYPART_WAIST,      // 0x02 PLAYER_LIMB_WAIST
    -1,                         // 0x03 PLAYER_LIMB_LOWER
    PLAYER_BODYPART_R_THIGH,    // 0x04 PLAYER_LIMB_R_THIGH
    PLAYER_BODYPART_R_SHIN,     // 0x05 PLAYER_LIMB_R_SHIN
    PLAYER_BODYPART_R_FOOT,     // 0x06 PLAYER_LIMB_R_FOOT
    PLAYER_BODYPART_L_THIGH,    // 0x07 PLAYER_LIMB_L_THIGH
    PLAYER_BODYPART_L_SHIN,     // 0x08 PLAYER_LIMB_L_SHIN
    PLAYER_BODYPART_L_FOOT,     // 0x09 PLAYER_LIMB_L_FOOT
    -1,                         // 0x0A PLAYER_LIMB_UPPER
    PLAYER_BODYPART_HEAD,       // 0x0B PLAYER_LIMB_HEAD
    PLAYER_BODYPART_HAT,        // 0x0C PLAYER_LIMB_HAT
    PLAYER_BODYPART_COLLAR,     // 0x0D PLAYER_LIMB_COLLAR
    PLAYER_BODYPART_L_SHOULDER, // 0x0E PLAYER_LIMB_L_SHOULDER
    PLAYER_BODYPART_L_FOREARM,  // 0x0F PLAYER_LIMB_L_FOREARM
    PLAYER_BODYPART_L_HAND,     // 0x10 PLAYER_LIMB_L_HAND
    PLAYER_BODYPART_R_SHOULDER, // 0x11 PLAYER_LIMB_R_SHOULDER
    PLAYER_BODYPART_R_FOREARM,  // 0x12 PLAYER_LIMB_R_FOREARM
    PLAYER_BODYPART_R_HAND,     // 0x13 PLAYER_LIMB_R_HAND
    PLAYER_BODYPART_SHEATH,     // 0x14 PLAYER_LIMB_SHEATH
    PLAYER_BODYPART_TORSO,      // 0x15 PLAYER_LIMB_TORSO
};

static void MmForm_PostLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* thisx) {
    Player* player = (Player*)thisx;
    Vec3f zeroVec = { 0.0f, 0.0f, 0.0f };

    // === 1. Store limb world positions in bodyPartsPos ===
    // Mirrors OOT z_player_lib.c:1842-1844 (D_80160000 system).
    // OOT systems that use bodyPartsPos: cylinder height calculation (z_player.c:12391),
    // fire/ice body effects (z_effect_soft_sprite_old_init.c), water splashes, etc.
    if (limbIndex > 0 && limbIndex < PLAYER_LIMB_MAX) {
        s8 bodyPart = sLimbToBodyPart[limbIndex];
        if (bodyPart >= 0) {
            Matrix_MultVec3f(&zeroVec, &player->bodyPartsPos[bodyPart]);
        }
    }

    // === 2. Update leftHandPos + carried actor support ===
    // OOT z_player_lib.c:1850 copies limb world pos → leftHandPos.
    // Then z_player_lib.c:1919-1934 updates carried actor rotation from hand matrix.
    // Without this, picked-up jars/bushes stay on the floor instead of following Link.
    if (limbIndex == PLAYER_LIMB_L_HAND) {
        Matrix_MultVec3f(&zeroVec, &player->leftHandPos);

        // FD melee weapon collision quads (sword damage registration).
        // OOT's Player_PostLimbDrawGameplay handles this at line 1904-1922, but FD replaces
        // PostLimbDraw with MmForm_PostLimbDraw, so we must call it explicitly here.
        if ((player->actor.scale.y >= 0.0f) && (player->meleeWeaponState != 0) &&
            gFormState.currentForm == MM_PLAYER_FORM_FIERCE_DEITY) {
            Player_FDMeleeWeaponPostLimb(play, player);
        }

        if (player->actor.scale.y >= 0.0f) {
            Actor* heldActor = player->heldActor;

            if (!Player_HoldsHookshot(player) && (heldActor != NULL)) {
                if (player->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) {
                    // Update carried actor rotation (OOT z_player_lib.c:1919-1930)
                    MtxF carryMtx;
                    Vec3s carryRot;

                    Matrix_Get(&carryMtx);
                    Matrix_MtxFToYXZRotS(&carryMtx, &carryRot, 0);

                    if (heldActor->flags & ACTOR_FLAG_CARRY_X_ROT_INFLUENCE) {
                        heldActor->world.rot.x = heldActor->shape.rot.x = carryRot.x - player->unk_3BC.x;
                    } else {
                        heldActor->world.rot.y = heldActor->shape.rot.y = player->actor.shape.rot.y + player->unk_3BC.y;
                    }
                }
            } else {
                // Store hand matrix for future carry operations (OOT z_player_lib.c:1932-1933)
                Matrix_Get(&player->mf_9E0);
                Matrix_MtxFToYXZRotS(&player->mf_9E0, &player->unk_3BC, 0);
            }
        }
    }

    // === 3. Update focus.pos at HEAD (Navi tracking, Z-targeting) ===
    // OOT z_player_lib.c:2077-2078: Matrix_MultVec3f(&D_801260D4, &this->actor.focus.pos)
    // D_801260D4 = { 1100.0f, -700.0f, 0.0f } (head offset from limb origin to eye level)
    if (limbIndex == PLAYER_LIMB_HEAD) {
        Vec3f headOffset = { 1100.0f, -700.0f, 0.0f };
        Matrix_MultVec3f(&headOffset, &player->actor.focus.pos);
    }

    // === 4. Update feet positions (ground dust effects, foot IK) ===
    // OOT z_player_lib.c:2080-2082
    if (limbIndex == PLAYER_LIMB_L_FOOT || limbIndex == PLAYER_LIMB_R_FOOT) {
        Actor_SetFeetPos(&player->actor, limbIndex, PLAYER_LIMB_L_FOOT, &zeroVec, PLAYER_LIMB_R_FOOT, &zeroVec);
    }

    // === 5. Update carried/held actor position at R_HAND ===
    // OOT z_player_lib.c:2051-2064: computes held object position as average of
    // bodyPartsPos[R_HAND] and leftHandPos, then copies to heldActor->world.pos.
    // Without this, picked-up jars/rocks stay at their original position and
    // get thrown from there instead of from Link's hands.
    if (limbIndex == PLAYER_LIMB_R_HAND) {
        if (player->actor.scale.y >= 0.0f) {
            Actor* heldActor = player->heldActor;

            if ((player->unk_862 != 0) || ((func_8002DD6C(player) == 0) && (heldActor != NULL))) {
                Vec3f getItemRefPos;

                if (!(player->stateFlags1 & PLAYER_STATE1_GETTING_ITEM) && (player->unk_862 != 0) &&
                    (player->exchangeItemId != EXCH_ITEM_NONE)) {
                    Math_Vec3f_Copy(&getItemRefPos, &player->leftHandPos);
                } else {
                    getItemRefPos.x = (player->bodyPartsPos[PLAYER_BODYPART_R_HAND].x + player->leftHandPos.x) * 0.5f;
                    getItemRefPos.y = (player->bodyPartsPos[PLAYER_BODYPART_R_HAND].y + player->leftHandPos.y) * 0.5f;
                    getItemRefPos.z = (player->bodyPartsPos[PLAYER_BODYPART_R_HAND].z + player->leftHandPos.z) * 0.5f;
                }

                // Set the global sGetItemRefPos so Player_DrawGetItem can find
                // the correct hand position (z_player_lib.c:2054-2058)
                Math_Vec3f_Copy(&sGetItemRefPos, &getItemRefPos);

                if (player->unk_862 == 0) {
                    Math_Vec3f_Copy(&heldActor->world.pos, &getItemRefPos);
                }
            }
        }
    }

    // === 6. Goron punch visual effect ===
    // From 2Ship z_player_lib.c func_80127488 line 3230.
    // Draws gLinkGoronGoronPunchEffectDL (red translucent) on the active hand
    // during punch hit frames. unk_3D0.unk_00 = 3 selects this DL in 2Ship.
    // D_801BFDD0[2] = { {255,0,0}, gLinkGoronGoronPunchEffectDL }
    // Left hand = LINK_GORON_LIMB_LEFT_HAND (12), Right hand = LINK_GORON_LIMB_RIGHT_HAND (15)
    if (gFormState.currentForm == MM_PLAYER_FORM_GORON &&
        (gFormState.goronAction >= GORON_ACT_PUNCH_A && gFormState.goronAction <= GORON_ACT_PUNCH_C)) {

        u8 step = gFormState.comboStep;
        u8 isHitFrame = 0;

        // Check if we're in the hit frame window for this punch
        f32 curFrame = gFormState.formSkelAnime.curFrame;
        if (step <= 2) {
            u8 hitStart = sGoronPunchFrames[step][0];
            u8 hitEnd = sGoronPunchFrames[step][1];
            f32 earlyStart = 5.0f; // Goron early detection (from 2Ship line 18773)
            isHitFrame = (curFrame >= earlyStart && curFrame <= (f32)hitEnd);
        }

        if (isHitFrame) {
            // Draw effect on left hand for punchA, right hand for punchB, waist area for punchC
            s32 targetLimb = -1;
            if (step == 0 && limbIndex == LINK_GORON_LIMB_LEFT_HAND)
                targetLimb = limbIndex;
            if (step == 1 && limbIndex == LINK_GORON_LIMB_RIGHT_HAND)
                targetLimb = limbIndex;
            if (step == 2 && limbIndex == LINK_GORON_LIMB_WAIST)
                targetLimb = limbIndex;

            if (targetLimb >= 0) {
                // Calculate alpha based on punch frame progress
                f32 hitStart = (f32)sGoronPunchFrames[step][0];
                f32 hitEnd = (f32)sGoronPunchFrames[step][1];
                f32 progress = (curFrame - hitStart) / (hitEnd - hitStart + 1.0f);
                u8 alpha = (u8)(200.0f * (1.0f - progress * 0.5f)); // Fade from 200 to 100

                OPEN_DISPS(play->state.gfxCtx);
                Gfx_SetupDL_25Xlu(play->state.gfxCtx);
                gDPSetEnvColor(POLY_XLU_DISP++, 255, 0, 0, alpha);
                gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                          G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                // Per-frame copy for punch DL (with G_ENDDL padding)
                if (sPunchDLCount > 0 && !sPunchDLSafeCopy.empty()) {
                    static const size_t DL_PADDING = 16;
                    size_t punchTotal = sPunchDLCount + DL_PADDING;
                    Gfx* punchCopy = (Gfx*)Graph_Alloc(play->state.gfxCtx, punchTotal * sizeof(Gfx));
                    memcpy(punchCopy, sPunchDLSafeCopy.data(), sPunchDLCount * sizeof(Gfx));
                    for (size_t p = 0; p < DL_PADDING; p++) {
                        punchCopy[sPunchDLCount + p].words.w0 = (uintptr_t)0xDF << 24;
                        punchCopy[sPunchDLCount + p].words.w1 = 0;
                    }
                    // Defensive: patch any segment 0x08 refs to gEmptyDL (safe no-op)
                    MmForm_PatchSegmentedDL(punchCopy, sPunchDLCount, 0x08, gEmptyDL);
                    // Patch G_DL_INDEX seg 0x0C → direct pointers to cull DLs
                    MmForm_PatchCullDLIndex(punchCopy, sPunchDLCount);
                    gSPDisplayList(POLY_XLU_DISP++, punchCopy);
                }
                CLOSE_DISPS(play->state.gfxCtx);
            }
        }
    }

    // === 7. Deku shield DL (guard pose) ===
    // From 2Ship z_player_lib.c:3995-4009 (Player_PostLimbDrawGameplay at PLAYER_LIMB_TORSO):
    //   When pn_gurd animation is playing, draw object_link_nuts_DL_00A348 with scaling.
    //   D_801C0410[] = { {0, {0,0,0}}, {2, {80,110,80}}, {3, {100,100,100}} }
    //   Scale interpolated by func_80124618 based on curFrame:
    //     frame 0: 0% (hidden), frame 2: 80/110/80%, frame 3+: 100% (full)
    if (gFormState.currentForm == MM_PLAYER_FORM_DEKU && gFormState.goronAction == MMFORM_ACT_SHIELD &&
        limbIndex == PLAYER_LIMB_TORSO) {
        // Interpolate scale based on animation frame (func_80124618 from MM)
        // D_801C0410: { {0, {0,0,0}}, {2, {80,110,80}}, {3, {100,100,100}} }
        f32 curFrame = gFormState.formSkelAnime.curFrame;
        f32 scX, scY, scZ;

        if (curFrame <= 0.0f) {
            scX = 0.0f;
            scY = 0.0f;
            scZ = 0.0f;
        } else if (curFrame < 2.0f) {
            // Lerp from {0,0,0} to {80,110,80} over frames 0-2
            f32 t = curFrame / 2.0f;
            scX = (80.0f * t) * 0.01f;
            scY = (110.0f * t) * 0.01f;
            scZ = (80.0f * t) * 0.01f;
        } else if (curFrame < 3.0f) {
            // Lerp from {80,110,80} to {100,100,100} over frames 2-3
            f32 t = curFrame - 2.0f;
            scX = (80.0f + 20.0f * t) * 0.01f;
            scY = (110.0f - 10.0f * t) * 0.01f;
            scZ = (80.0f + 20.0f * t) * 0.01f;
        } else {
            scX = 1.0f;
            scY = 1.0f;
            scZ = 1.0f;
        }

        if (scX > 0.001f) { // Don't draw if fully hidden
            OPEN_DISPS(play->state.gfxCtx);

            Matrix_Push();
            // Rotate 90° in Z to align the shield DL with the torso
            Matrix_RotateZ(M_PI / 2.0f, MTXMODE_APPLY);
            Matrix_Scale(scX, scY, scZ, MTXMODE_APPLY);

            gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                      G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gLinkDekuShieldDL);

            Matrix_Pop();

            CLOSE_DISPS(play->state.gfxCtx);
        }
    }

    // === 8. Zora forearm fin/shield DLs ===
    // From 2Ship z_player_lib.c func_80126BD0 (line 3001):
    // Draws fin/blade extensions on forearms. Called at PLAYER_LIMB_LEFT_FOREARM (arg2=0)
    // and PLAYER_LIMB_RIGHT_FOREARM (arg2=1) in MM's PostLimbDraw.
    // All MM form skeletons share the OOT Player skeleton hierarchy (22 limbs),
    // so limb indices in PostLimbDraw are PLAYER_LIMB_* constants.
    // PLAYER_LIMB_L_FOREARM=15, PLAYER_LIMB_R_FOREARM=18.
    // Scale: default (0.4, 0.6, 0.7), shield/boomerang (1.0, 1.0, 1.0).
    // HIDE FINS when boomerangs are thrown (boomerangState >= 2):
    //   In MM, the forearm fins ARE the boomerangs — they detach and fly as projectiles.
    //   From 2Ship z_player_lib.c:2998: if (this->rightHandType == PLAYER_MODELTYPE_RH_ZORA)
    //   Show fins only during idle (0) and aiming (1). Hide during throw anim (2) and flight (3).
    if (gFormState.currentForm == MM_PLAYER_FORM_ZORA && gFormState.boomerangState <= 1) {
        std::vector<Gfx>* finDLCopy = NULL;
        size_t finDLCount = 0;

        if (limbIndex == PLAYER_LIMB_L_FOREARM) {
            finDLCopy = &sZoraFinLDLSafeCopy;
            finDLCount = sZoraFinLDLCount;
        } else if (limbIndex == PLAYER_LIMB_R_FOREARM) {
            finDLCopy = &sZoraFinRDLSafeCopy;
            finDLCount = sZoraFinRDLCount;
        }

        if (finDLCopy != NULL && finDLCount > 0 && !finDLCopy->empty()) {
            // Scale: full during shield/boomerang, default (0.4, 0.6, 0.7) otherwise
            // From 2Ship func_80126BD0: heldItemAction == PLAYER_IA_ZORA_BOOMERANG → (1,1,1)
            //                           else → (0.4, 0.6, 0.7)
            f32 scX, scY, scZ;
            if (gFormState.goronAction == MMFORM_ACT_SHIELD || gFormState.boomerangState == 1) {
                // Full scale during shield or boomerang aiming (state 1 = holding B, fins extended)
                scX = 1.0f;
                scY = 1.0f;
                scZ = 1.0f;
            } else {
                scX = 0.4f;
                scY = 0.6f;
                scZ = 0.7f;
            }

            OPEN_DISPS(play->state.gfxCtx);
            Gfx_SetupDL_25Opa(play->state.gfxCtx);

            Matrix_Push();
            Matrix_Scale(scX, scY, scZ, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                      G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

            // Per-frame copy with G_ENDDL padding (safe DL pattern)
            {
                static const size_t DL_PADDING = 16;
                size_t totalCount = finDLCount + DL_PADDING;
                Gfx* dlCopy = (Gfx*)Graph_Alloc(play->state.gfxCtx, totalCount * sizeof(Gfx));
                memcpy(dlCopy, finDLCopy->data(), finDLCount * sizeof(Gfx));
                for (size_t p = 0; p < DL_PADDING; p++) {
                    dlCopy[finDLCount + p].words.w0 = (uintptr_t)0xDF << 24;
                    dlCopy[finDLCount + p].words.w1 = 0;
                }
                // Defensive: patch any segment 0x08 refs to gEmptyDL (safe no-op)
                MmForm_PatchSegmentedDL(dlCopy, finDLCount, 0x08, gEmptyDL);
                // Patch G_DL_INDEX seg 0x0C → direct pointers to cull DLs
                MmForm_PatchCullDLIndex(dlCopy, finDLCount);
                gSPDisplayList(POLY_OPA_DISP++, dlCopy);
            }

            Matrix_Pop();
            CLOSE_DISPS(play->state.gfxCtx);
        }
    }

    // === 9. Deku Flower Petals during flight ===
    // From 2Ship z_player_lib.c func_801271B0 (line 3114-3162):
    // Draws flower petals on left/right hands during flight animations.
    // Open flower DL when gliding (vel.y >= -6), closed when falling fast (vel.y < -6).
    // Petal rotation from dekuPetalAngle applied via Matrix_RotateXS.
    if (gFormState.currentForm == MM_PLAYER_FORM_DEKU &&
        (gFormState.goronAction == MMFORM_ACT_DEKU_FLY ||
         (gFormState.goronAction == MMFORM_ACT_DEKU_FALL_LOCKED && (gFormState.dekuFlightFlags & DEKU_FLIGHT_OPEN)))) {

        s32 handSide = -1; // 0=left, 1=right
        if (limbIndex == PLAYER_LIMB_L_HAND) {
            handSide = 0;
        } else if (limbIndex == PLAYER_LIMB_R_HAND) {
            handSide = 1;
        }

        if (handSide >= 0) {
            OPEN_DISPS(play->state.gfxCtx);

            Matrix_Push();

            // Translate to petal attachment point (from 2Ship line 3133)
            Matrix_Translate(0.0f, 150.0f, 0.0f, MTXMODE_APPLY);

            // Translate to flower position on arm (from 2Ship line 3141)
            Matrix_Translate(2150.0f, 0.0f, 0.0f, MTXMODE_APPLY);

            // Petal rotation (from 2Ship line 3142: Matrix_RotateXS(unk_B8A))
            Matrix_RotateX(gFormState.dekuPetalAngle * (M_PI / 32768.0f), MTXMODE_APPLY);

            gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                      G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

            // Choose closed or open flower based on descent speed (from 2Ship line 3149-3150)
            if (player->actor.velocity.y < -6.0f) {
                gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gLinkDekuClosedFlowerDL);
            } else {
                gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gLinkDekuOpenFlowerDL);
            }

            // Update hand body part position from petal tip (from 2Ship line 3152)
            {
                Vec3f tipZero = { 0.0f, 0.0f, 0.0f };
                s32 bodyPart = (handSide == 0) ? PLAYER_BODYPART_L_HAND : PLAYER_BODYPART_R_HAND;
                Matrix_MultVec3f(&tipZero, &player->bodyPartsPos[bodyPart]);
            }

            Matrix_Pop();

            CLOSE_DISPS(play->state.gfxCtx);
        }
    }
}

// =============================================================================
// Public API (extern "C")
// =============================================================================

extern "C" {

void MmForm_Init(PlayState* play, Player* player) {
    (void)play;

    // === Cleanup from previous scene ===
    // If the player was transformed in the old scene, we need to:
    // 1. Restore persistent gSaveContext changes (like strength upgrade for FD)
    // 2. Save the form so we can re-activate it after init
    if (gFormState.state == MMFORM_STATE_ACTIVE || gFormState.state == MMFORM_STATE_TRANSFORMING) {
        // Restore FD strength override (FD forces Gold Gauntlets=3 in gSaveContext)
        if (gFormState.currentForm == MM_PLAYER_FORM_FIERCE_DEITY && gFormState.savedStrength != 0) {
            Inventory_ChangeUpgrade(UPG_STRENGTH, gFormState.savedStrength);
        }

        // Restore C-button equips to pre-transform state (reactivation will re-save/restrict)
        MmForm_RestoreEquips(play);

        // Save form for re-activation after init (new player, new PlayState)
        sPendingReactivateForm = gFormState.currentForm;
        sPendingReactivate = 1;

        MMFORM_LOG("[MmForm] Scene transition while transformed as form %d, will re-activate", gFormState.currentForm);
    } else {
        sPendingReactivate = 0;
    }

    // Zero everything — formSkelAnime, cached pointers, etc. are all stale after scene change
    memset(&gFormState, 0, sizeof(gFormState));
    gFormState.state = MMFORM_STATE_INACTIVE;
    gFormState.currentForm = MM_PLAYER_FORM_HUMAN;
    gFormState.initialized = 1;

    // Auto-enable mask replacement CVars when Transformation Masks is enabled
    // This ensures Skull Mask → Deku, Spooky → Stone, Gerudo → Fierce Deity
    // are active without requiring the user to enable each one separately.
    s32 tmEnabled = CVarGetInteger("gMods.TransformMasks.Enabled", 0);
    u8 mmAvailable = MmAssets_IsAvailable();
    MMFORM_LOG("[MmForm] Init: TransformMasks.Enabled=%d, mm.o2r available=%d", tmEnabled, mmAvailable);

    if (tmEnabled && mmAvailable) {
        CVarSetInteger("gMods.TransformMasks.DekuReplacesSkull", 1);
        CVarSetInteger("gMods.TransformMasks.StoneReplacesSpooky", 1);
        CVarSetInteger("gMods.TransformMasks.FierceReplacesGerudo", 1);
        MMFORM_LOG("[MmForm] Auto-enabled: DekuReplacesSkull, StoneReplacesSpooky, FierceReplacesGerudo");
    }

    MMFORM_LOG("[MmForm] Initialized (mm.o2r=%d, enabled=%d, fierceActive=%d)", mmAvailable, tmEnabled,
               CVarGetInteger("gMods.TransformMasks.FierceReplacesGerudo", 0));
}

u8 MmForm_IsEnabled(void) {
    return CVarGetInteger("gMods.TransformMasks.Enabled", 0) && MmAssets_IsAvailable();
}

u8 MmForm_IsFDSkinMode(void) {
    return (gFormState.state == MMFORM_STATE_ACTIVE && gFormState.currentForm == MM_PLAYER_FORM_FIERCE_DEITY &&
            gFormState.skeletonLoaded);
}

u8 MmForm_IsTransformed(void) {
    // FD skin mode: OOT handles all gameplay, we only swap DLs.
    // Return false so all 5 z_player.c hooks treat FD as normal Adult Link.
    if (MmForm_IsFDSkinMode())
        return 0;
    return gFormState.state == MMFORM_STATE_ACTIVE || gFormState.state == MMFORM_STATE_TRANSFORMING ||
           gFormState.state == MMFORM_STATE_DETRANSFORMING;
}

u8 MmForm_IsTransformedAny(void) {
    return gFormState.state == MMFORM_STATE_ACTIVE || gFormState.state == MMFORM_STATE_TRANSFORMING ||
           gFormState.state == MMFORM_STATE_DETRANSFORMING;
}

u8 MmForm_IsSlotAllowed(u8 slot) {
    return MmForm_IsSlotAllowedInternal(slot);
}

MmPlayerTransformation MmForm_GetCurrentForm(void) {
    return (MmPlayerTransformation)gFormState.currentForm;
}

u8 MmForm_IsItemAllowed(s32 item) {
    if (item == ITEM_NONE || item == ITEM_NONE_FE)
        return 1;

    // Not transformed = everything allowed
    if (gFormState.state != MMFORM_STATE_ACTIVE && gFormState.state != MMFORM_STATE_TRANSFORMING &&
        gFormState.state != MMFORM_STATE_DETRANSFORMING)
        return 1;

    // Swords are equipment (no inventory slot). FD skin mode uses OOT's sword system,
    // so swords must be explicitly allowed or Player_UseItem blocks B-button attacks.
    if (item == ITEM_SWORD_KOKIRI || item == ITEM_SWORD_MASTER || item == ITEM_SWORD_BGS || item == ITEM_SWORD_KNIFE) {
        return (gFormState.currentForm == MM_PLAYER_FORM_FIERCE_DEITY) ? 1 : 0;
    }

    // OOT masks (Keaton..Truth) always blocked when transformed.
    // They share SLOT_TRADE_CHILD so slot-based check alone would incorrectly allow them.
    if (item >= ITEM_MASK_KEATON && item <= ITEM_MASK_TRUTH)
        return 0;

    // Look up slot for this item and check the slot-based array
    u8 slot = ExtInv_GetItemSlot((u16)item);
    if (slot != 0xFF) {
        return MmForm_IsSlotAllowedInternal(slot);
    }

    // Items without a slot (virtual combos like BOW_ARROW_FIRE): check parent slot
    if (item == ITEM_BOW_ARROW_FIRE || item == ITEM_BOW_ARROW_ICE || item == ITEM_BOW_ARROW_LIGHT) {
        return MmForm_IsSlotAllowedInternal(SLOT_BOW);
    }

    return 0; // Unknown item = blocked
}

u8 MmForm_HasSkeleton(void) {
    return gFormState.skeletonLoaded;
}

/**
 * Returns the camera height for the current form.
 * From MM decomp z_actor.c:1374-1400 (Player_GetHeight).
 * Used by Player_GetHeight() in OOT to fix camera positioning for transformed forms.
 */
f32 MmForm_GetCameraHeight(void) {
    if (gFormState.state == MMFORM_STATE_INACTIVE)
        return 0.0f;

    // Goron ball form: reduced height (34.0f) from MM decomp
    // MM: (stateFlags3 & PLAYER_STATE3_1000) ? 34.0f : 80.0f
    if (gFormState.currentForm == MM_PLAYER_FORM_GORON &&
        (gFormState.goronAction == GORON_ACT_GORON_ROLL || gFormState.goronAction == GORON_ACT_GORON_ROLL_JUMP ||
         gFormState.goronAction == GORON_ACT_GORON_ROLL_POUND)) {
        return 34.0f;
    }

    return sFormProps[gFormState.currentForm].cameraHeight;
}

/**
 * Returns 1 if the current form blocks ledge grabbing.
 * From MM decomp z_player.c:6209 (Player_ActionHandler_12):
 *   Goron: NEVER grabs ledges
 *   All other forms: CAN grab ledges (Deku limited by unk_14=49 height threshold)
 */
u8 MmForm_BlocksLedgeGrab(void) {
    if (gFormState.state != MMFORM_STATE_ACTIVE)
        return 0;
    return (gFormState.currentForm == MM_PLAYER_FORM_GORON) ? 1 : 0;
}

/**
 * Called from func_8083D36C when OOT wants to start swimming.
 * Returns 1 if swimming was blocked (Goron/Deku), 0 if allowed (Zora/FD/human).
 *
 * For Goron: starts the curl → ball → void out sequence.
 * Called every frame while in deep water, but only starts the sequence once.
 */
u8 MmForm_OnWaterSwimAttempt(PlayState* play, Player* player) {
    if (gFormState.state != MMFORM_STATE_ACTIVE)
        return 0;

    // Block ALL OOT water actions for ALL transformed forms.
    // Each form handles water in its own way via MmForm_Update:
    //   Goron/Deku: void out (MMFORM_ACT_WATER_VOID)
    //   Zora: MM swim system (MmForm_Action_SwimIdle/Fast/etc.)
    //   Fierce Deity: same as OOT Link (handled by OOT yield system)

    if (gFormState.currentForm == MM_PLAYER_FORM_DEKU) {
        // Deku: try water hop before void out
        // From 2Ship func_80850854 (z_player.c line 16811-16817):
        //   Deku + hopsRemaining + health > 0 + in water → hop
        if (gFormState.dekuHopsRemaining > 0 && gSaveContext.health > 0 && player->actor.yDistToWater > 0.0f &&
            gFormState.goronAction != MMFORM_ACT_JUMP) { // Not already mid-hop
            MmForm_DekuWaterHop(player, play);
        } else {
            // No hops left → void out
            if (gFormState.goronAction != MMFORM_ACT_WATER_VOID && gFormState.goronAction != MMFORM_ACT_HAZARD_VOID) {
                gFormState.goronAction = MMFORM_ACT_WATER_VOID;
                gFormState.actionTimer = 0;
                gFormState.rollGroundPoundTimer = 0;
            }
        }
    } else if (gFormState.currentForm == MM_PLAYER_FORM_GORON) {
        // Goron can't swim → start water void-out (only once)
        if (gFormState.goronAction != MMFORM_ACT_WATER_VOID && gFormState.goronAction != MMFORM_ACT_HAZARD_VOID) {
            gFormState.goronAction = MMFORM_ACT_WATER_VOID;
            gFormState.actionTimer = 0;
            gFormState.rollGroundPoundTimer = 0;
        }
    }

    return 1; // Block OOT swimming for ALL forms
}

TransformMaskId MmForm_GetMaskType(s32 item) {
    TransformMaskId result;
    switch (item) {
        // MM mask items (from 3rd inventory page)
        case ITEM_MM_MASK_GORON:
            result = TRANSFORM_MASK_GORON;
            break;
        case ITEM_MM_MASK_ZORA:
            result = TRANSFORM_MASK_ZORA;
            break;
        case ITEM_MM_MASK_DEKU:
            result = TRANSFORM_MASK_DEKU;
            break;
        case ITEM_MM_MASK_FIERCE_DEITY:
            result = TRANSFORM_MASK_FIERCE_DEITY;
            break;
        // OOT mask items (backward compat)
        case ITEM_MASK_GORON:
            result = TRANSFORM_MASK_GORON;
            break;
        case ITEM_MASK_ZORA:
            result = TRANSFORM_MASK_ZORA;
            break;
        default:
            result = TRANSFORM_MASK_NONE;
            break;
    }
    return result;
}

void MmForm_HandleMaskUse(PlayState* play, Player* player, s32 item) {

    if (!MmForm_IsEnabled())
        return;

    TransformMaskId maskId = MmForm_GetMaskType(item);
    if (maskId == TRANSFORM_MASK_NONE)
        return;

    MmPlayerTransformation targetForm = MmForm_MaskIdToForm(maskId);

    // If already transformed to the same form -> de-transform
    if (gFormState.state == MMFORM_STATE_ACTIVE && gFormState.currentForm == targetForm) {
        gFormState.state = MMFORM_STATE_DETRANSFORMING;
        gFormState.cutsceneTimer = 0;
        gFormState.cutscenePhase = 0;
        gFormState.flashAlpha = 0;
        // Clear stale action state
        gFormState.goronAction = GORON_ACT_IDLE;
        gFormState.rollSpikeActive = 0;
        gFormState.rollChargeLevel = 0;
        gFormState.rollSpinRate = 0;
        return;
    }

    // If already transformed to a different form -> de-transform first, then re-transform
    // For now: instant switch (future: chain cutscenes)
    if (gFormState.state == MMFORM_STATE_ACTIVE && gFormState.currentForm != targetForm) {
        MmForm_RestoreOotState(player);
        MmForm_RestoreEquips(play);
        gFormState.skeletonLoaded = 0;
        // Clear stale action/animation state to prevent crashes on new form
        gFormState.formSkelAnime.animation = NULL;
        gFormState.goronAction = GORON_ACT_IDLE;
        gFormState.rootMotion.firstFrame = 1;
        gFormState.rootMotion.prevX = 0;
        gFormState.rootMotion.prevZ = 0;
        gFormState.rollSpikeActive = 0;
        gFormState.rollChargeLevel = 0;
        gFormState.rollSpinRate = 0;
    }

    // Start transformation
    gFormState.targetForm = targetForm;
    gFormState.state = MMFORM_STATE_TRANSFORMING;
    gFormState.cutsceneTimer = 0;
    gFormState.cutscenePhase = 0;
    gFormState.flashAlpha = 0;
}

// FD skin mode: runs AFTER OOT's actionFunc sets linearVelocity and playSpeed.
// - Sets actor.scale to 0.015f (MM uses 0.015f for FD vs 0.01f for human)
// - Forces PLAYER_ANIMTYPE_3 (two-handed fighter animations) for correct FD stance
// - Overrides the target speed to 1.5x normal Link speed (only during normal movement)
static void MmForm_FDSkinSpeedBoost(Player* player, PlayState* play) {
    // MM FD actor.scale = 0.015f (z_player_lib.c func_80123140 line 638)
    // OOT default = 0.01f. Set every frame to prevent OOT from resetting it.
    Actor_SetScale(&player->actor, 0.015f);

    // Force two-handed fighter animation set for FD default stance.
    // In MM, FD always uses PLAYER_ANIMTYPE_3 because the Fierce Deity Sword is two-handed.
    // OOT would use ANIMTYPE_0/1/2 based on child Link's equipment, but FD should use
    // the fighter_*_long animations (idle, walk, run, sidestep, roll, z-target, etc.)
    // which already exist in OOT's gameplay_keep (D_80853914 column 3).
    // Only override base stance types (0-2). Types 3+ (bow, hookshot, explosives) are
    // already correct for the item being used.
    if (player->modelAnimType < PLAYER_ANIMTYPE_3) {
        player->modelAnimType = PLAYER_ANIMTYPE_3;
    }

    // FD BGS-equivalent behavior (damage, reach, trail, two-handed) is handled by overriding
    // Player_GetMeleeWeaponHeld and Player_HoldsTwoHandedWeapon in z_player_lib.c.
    // Do NOT force heldItemAction here — it causes an infinite equip/unequip animation loop
    // because OOT detects the mismatch between heldItemAction and itemAction each frame,
    // triggering Player_UpperAction_ChangeHeldItem endlessly.

    // Don't apply speed boost during non-movement states (ledge grab, climbing, cutscene, etc.)
    // Without this, linearVelocity carries momentum through ledge grabs → clip through floor.
    if (player->stateFlags1 &
        (PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_INPUT_DISABLED | PLAYER_STATE1_HANGING_OFF_LEDGE |
         PLAYER_STATE1_CLIMBING_LEDGE | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_DEAD | PLAYER_STATE1_GETTING_ITEM)) {
        return;
    }

    // Speed boost: 1.5x normal Link movement speed (only when grounded, pushing stick, and NOT attacking)
    // Without the meleeWeaponState gate, the boost overrides the attack action's deceleration
    // causing FD to rush forward at full speed during charge attacks regardless of stick direction.
    f32 stickMag = MmForm_GetStickMagnitude(play);
    if (stickMag > 0.5f && (player->actor.bgCheckFlags & 1) && (player->meleeWeaponState == 0)) {
        f32 fdTarget = stickMag * 0.168f; // 0.112 * 1.5
        Math_StepToF(&player->linearVelocity, fdTarget, 1.5f);
    }

    // NOTE: playSpeed is NOT modified. Slowing all animations by 2/3 to match the
    // speed boost causes one-shot animations (backflip, roll, sidehop) to never complete
    // within OOT's expected timeframes → player gets stuck. The slight visual skating
    // on walk/run is acceptable. FD's own MM animations (fighter_walk_long, etc.) are
    // designed for his stride length and can be loaded as a future enhancement.

    // Sword sparkle effect while Z-targeting an actor (visual indicator for sword beam)
    // From MM z_player_lib.c: EffectSsKirakira spawned along sword blade while targeting.
    // Use focusActor check instead of PLAYER_STATE1_HOSTILE_LOCK_ON (which may not be set
    // yet at this point in the frame — it's updated by Player_UpdateHostileLockOn AFTER
    // the action handler list runs in some action functions).
    if (player->focusActor != NULL) {
        for (int i = 0; i < 2; i++) {
            Vec3f sparklePos;
            f32 t = Rand_ZeroFloat(1.0f);
            sparklePos.x = player->bodyPartsPos[PLAYER_BODYPART_L_HAND].x + Rand_CenteredFloat(30.0f);
            sparklePos.y = player->bodyPartsPos[PLAYER_BODYPART_L_HAND].y + (t * 40.0f) + Rand_CenteredFloat(10.0f);
            sparklePos.z = player->bodyPartsPos[PLAYER_BODYPART_L_HAND].z + Rand_CenteredFloat(30.0f);
            Vec3f sparkleVel = { 0.0f, 0.3f, 0.0f };
            Vec3f sparkleAccel = { 0.0f, -0.01f, 0.0f };
            Color_RGBA8 primColor = { 100, 255, 255, 255 };
            Color_RGBA8 envColor = { 0, 100, 200, 0 };
            EffectSsKiraKira_SpawnDispersed(play, &sparklePos, &sparkleVel, &sparkleAccel, &primColor, &envColor, 1000,
                                            16);
        }
    }
}

void MmForm_Update(PlayState* play, Player* player) {
    if (!gFormState.initialized)
        return;

    // === Re-activate form after scene transition ===
    // sPendingReactivate is set by MmForm_Init when the player was transformed in the old scene.
    // We wait until the first Update frame (new PlayState is fully initialized) to re-activate.
    if (sPendingReactivate && MmForm_IsEnabled()) {
        sPendingReactivate = 0;
        MmPlayerTransformation form = sPendingReactivateForm;
        sPendingReactivateForm = MM_PLAYER_FORM_HUMAN;

        if (form != MM_PLAYER_FORM_HUMAN) {
            MMFORM_LOG("[MmForm] Re-activating form %d after scene transition", form);
            // Start instant transformation (skip cutscene)
            gFormState.targetForm = form;
            gFormState.state = MMFORM_STATE_TRANSFORMING;
            gFormState.cutsceneTimer = 0;
            gFormState.cutscenePhase = 0;
            gFormState.flashAlpha = 0;
            // Force instant transform for scene transitions regardless of CVar
            sForceInstantTransform = 1;
        }
    }

    // Update blink whenever skeleton is loaded (all non-inactive states)
    if (gFormState.skeletonLoaded) {
        MmForm_UpdateBlink();
    }

    switch (gFormState.state) {
        case MMFORM_STATE_INACTIVE:
            // Nothing to do
            break;

        case MMFORM_STATE_TRANSFORMING:
            MmForm_UpdateTransforming(player, play);
            break;

        case MMFORM_STATE_ACTIVE:
            if (gFormState.currentForm == MM_PLAYER_FORM_FIERCE_DEITY) {
                // FD skin mode: OOT handles all gameplay. Only apply speed boost.
                MmForm_FDSkinSpeedBoost(player, play);
                break;
            }
            // Run action state machine (idle/walk/run + future punch/roll/damage)
            MmForm_UpdateActive(player, play);
            break;

        case MMFORM_STATE_DETRANSFORMING:
            MmForm_UpdateDetransforming(player, play);
            break;
    }

    // Ground pound crack timer (decrement regardless of action state, persists across roll resume)
    if (gFormState.groundPoundCrackTimer > 0) {
        gFormState.groundPoundCrackTimer--;
    }
}

void MmForm_Draw(PlayState* play, Player* player) {
    if (gFormState.state == MMFORM_STATE_INACTIVE)
        return;

    OPEN_DISPS(play->state.gfxCtx);

    // Draw MM form skeleton (only when loaded)
    if (gFormState.skeletonLoaded) {
        // Setup render state
        Gfx_SetupDL_25Opa(play->state.gfxCtx);

        // Set segment 0x0C = gCullBackDList (mirrors MM's Player_DrawGameplay z_player.c:12926)
        // MM DLs contain G_DL_INDEX (0x3D) commands that reference segment 0x0C to call
        // gCullFrontDList (at offset 0x10 from gCullBackDList) for face culling setup.
        // Without this, segment 0x0C is unset and SegAddr resolves to garbage → crash.
        gSPSegment(POLY_OPA_DISP++, 0x0C, (uintptr_t)gCullBackDList);

        // Safety: initialize segment 0x08 to gEmptyDL on BOTH pipes BEFORE any mm.o2r DL draws.
        // PostLimbDraw may draw punch/fin effects on XLU before rolling code sets seg 0x08.
        // Without this, a stale segment 0x08 from a previous actor could cause garbage resolution.
        // Rolling code overwrites segment 0x08 later with the proper TwoTexScroll DL.
        gSPSegment(POLY_XLU_DISP++, 0x08, (uintptr_t)gEmptyDL);
        gSPSegment(POLY_XLU_DISP++, 0x0C, (uintptr_t)gCullBackDList);

        // Actor_Draw already set the correct matrix before calling Player_Draw:
        //   Matrix_SetTranslateRotateYXZ(pos.x, pos.y + yOffset*scale.y, pos.z, &shape.rot)
        //   Matrix_Scale(scale.x, scale.y, scale.z, MTXMODE_APPLY)
        // We use that matrix as-is. No need to create our own.

        // Damage flicker: red fog oscillation (from OOT z_player.c line 12744-12748)
        // OOT uses Gfx_SetFog2 with cosine-oscillating fog distance to create
        // the red flash effect during invincibility frames
        if (player->invincibilityTimer > 0) {
            s32 flickerValue = CLAMP(50 - player->invincibilityTimer, 8, 40);
            player->damageFlickerAnimCounter += flickerValue;
            s32 fogDist = 4000 - (s32)(Math_CosS(player->damageFlickerAnimCounter * 256) * 2000.0f);
            POLY_OPA_DISP = Gfx_SetFog2(POLY_OPA_DISP, 255, 0, 0, 0, 0, fogDist);
        }

        // Draw based on current action
        s32 isRolling =
            (gFormState.goronAction == GORON_ACT_GORON_ROLL || gFormState.goronAction == GORON_ACT_GORON_ROLL_JUMP ||
             gFormState.goronAction == GORON_ACT_GORON_ROLL_POUND ||
             // Water void out: show ball after curl completes (phase 2+)
             (gFormState.goronAction == MMFORM_ACT_WATER_VOID && gFormState.rollGroundPoundTimer >= 2));

        // Set face/mouth texture segments ONLY for skeleton draw (NOT ball DL).
        // Ball DL (gLinkGoronCurledDL) doesn't use head/eye textures.
        // Setting segment 0x08 to an eye texture OTR path while drawing the ball
        // could corrupt the segment table if the ball DL references segment 0x08.
        if (!isRolling || gFormState.currentForm != MM_PLAYER_FORM_GORON) {
            s32 form = (s32)gFormState.currentForm;
            u8 eyeIdx = gFormState.eyeIndex;
            if (eyeIdx > 3)
                eyeIdx = 0;

            if (form >= 0 && form < MM_PLAYER_FORM_MAX && sFormEyeTextures[form][eyeIdx] != NULL) {
                gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)sFormEyeTextures[form][eyeIdx]);
            }

            if (form == MM_PLAYER_FORM_ZORA) {
                gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)sZoraMouthClosed);
            }
            // FD mouth: baked into head DL (segment 0x09 unread), texture not in mm.o2r as standalone resource
        }

        if (isRolling && gFormState.currentForm == MM_PLAYER_FORM_GORON) {
            // Ball form: draw gLinkGoronCurledDL instead of skeleton
            // From 2Ship z_player.c line 13337-13373 (PLAYER_STATE3_1000 draw path)
            //
            // NOTE: The mm.o2r DL contains G_DL_INDEX (opcode 0x3D) commands that
            // reference segment 0x0C for face culling (gCullFrontDList). Segment 0x0C
            // is set above via gSPSegment. Without it, SegAddr resolves to garbage → crash.

            // Ball draw builds matrix from scratch:
            //   Translate(world.pos + yOffset) -> RotateY(shape.y) -> RotateZ(shape.z)
            //   -> Scale(1.15x) -> RotateX(shape.x = rolling spin)
            {
                f32 yOffset = 1200.0f * player->actor.scale.y;

                Matrix_Translate(player->actor.world.pos.x, player->actor.world.pos.y + yOffset,
                                 player->actor.world.pos.z, MTXMODE_NEW);
                Matrix_RotateY(player->actor.shape.rot.y * (M_PI / 0x8000), MTXMODE_APPLY);
                Matrix_RotateZ(player->actor.shape.rot.z * (M_PI / 0x8000), MTXMODE_APPLY);
                {
                    f32 sq = gFormState.rollSquash;
                    Matrix_Scale(player->actor.scale.x * 1.15f * (1.0f + sq),
                                 player->actor.scale.y * 1.15f * (1.0f - sq),
                                 player->actor.scale.z * 1.15f * (1.0f + sq), MTXMODE_APPLY);
                }
                Matrix_RotateX(player->actor.shape.rot.x * (M_PI / 0x8000), MTXMODE_APPLY);
            }

            gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 255);
            gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                      G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

            // Set segment 0x08 on OPA pipe for rolling DLs.
            // The spike DL contains G_DL(0xDE) referencing segment 0x08 for animated
            // materials (TwoTexScroll). Curled ball DL does NOT use seg 0x08 (confirmed
            // by runtime: 0 patches). We ALSO patch each DL copy to use direct pointers,
            // bypassing the segment table entirely.
            Gfx* twoTexScrollOpa;
            {
                u32 frames = play->gameplayFrames;
                twoTexScrollOpa =
                    Gfx_TwoTexScroll(play->state.gfxCtx, 0, 0, 0, 0x40, 0x40, 1, frames * 2, frames * 2, 0x40, 0x40);
                gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)twoTexScrollOpa);
            }

            // Draw curled ball DL from mm.o2r (per-frame copy with G_ENDDL padding)
            if (sCurledDLCount > 0 && !sCurledDLSafeCopy.empty()) {
                static const size_t DL_PADDING = 16;
                size_t totalCount = sCurledDLCount + DL_PADDING;
                Gfx* dlCopy = (Gfx*)Graph_Alloc(play->state.gfxCtx, totalCount * sizeof(Gfx));
                memcpy(dlCopy, sCurledDLSafeCopy.data(), sCurledDLCount * sizeof(Gfx));
                for (size_t p = 0; p < DL_PADDING; p++) {
                    dlCopy[sCurledDLCount + p].words.w0 = (uintptr_t)0xDF << 24;
                    dlCopy[sCurledDLCount + p].words.w1 = 0;
                }
                {
                    int pc08 = MmForm_PatchSegmentedDL(dlCopy, sCurledDLCount, 0x08, twoTexScrollOpa);
                    int pc0C = MmForm_PatchCullDLIndex(dlCopy, sCurledDLCount);
                    static u8 sLoggedCurled = 0;
                    if (!sLoggedCurled) {
                        MMFORM_LOG("[MmForm] CurledDL: patched %d seg0x08, %d seg0x0C refs (count=%zu)", pc08, pc0C,
                                   sCurledDLCount);
                        sLoggedCurled = 1;
                    }
                }
                gSPDisplayList(POLY_OPA_DISP++, dlCopy);
            }

            // === Spike geometry on POLY_OPA_DISP (from 2Ship z_player.c line 13115-13123) ===
            // Physical spike model drawn separately from energy effects.
            if (gFormState.rollSpikeActive > 0 && sSpikeGeomDLCount > 0 && !sSpikeGeomDLSafeCopy.empty()) {
                if (gFormState.rollSpikeActive < 3) {
                    f32 spikeScale = (f32)gFormState.rollSpikeActive / 3.0f;
                    Matrix_Scale(spikeScale, spikeScale, spikeScale, MTXMODE_APPLY);
                    gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                }

                {
                    static const size_t DL_PADDING = 16;
                    size_t total = sSpikeGeomDLCount + DL_PADDING;
                    Gfx* dlCopy = (Gfx*)Graph_Alloc(play->state.gfxCtx, total * sizeof(Gfx));
                    memcpy(dlCopy, sSpikeGeomDLSafeCopy.data(), sSpikeGeomDLCount * sizeof(Gfx));
                    for (size_t p = 0; p < DL_PADDING; p++) {
                        dlCopy[sSpikeGeomDLCount + p].words.w0 = (uintptr_t)0xDF << 24;
                        dlCopy[sSpikeGeomDLCount + p].words.w1 = 0;
                    }
                    {
                        int pc08 = MmForm_PatchSegmentedDL(dlCopy, sSpikeGeomDLCount, 0x08, twoTexScrollOpa);
                        int pc0C = MmForm_PatchCullDLIndex(dlCopy, sSpikeGeomDLCount);
                        static u8 sLoggedSpike = 0;
                        if (!sLoggedSpike) {
                            MMFORM_LOG("[MmForm] SpikeDL: patched %d seg0x08, %d seg0x0C refs (count=%zu)", pc08, pc0C,
                                       sSpikeGeomDLCount);
                            sLoggedSpike = 1;
                        }
                    }
                    gSPDisplayList(POLY_OPA_DISP++, dlCopy);
                }
            }

            // === Energy effects on POLY_XLU_DISP (from 2Ship z_player.c line 13128-13155) ===
            // grt_01_model (DL_0127B0) and grt_02_model (DL_0134D0) are translucent energy
            // effects drawn with alpha based on charge level. They contain gsSPDisplayList(0x08000000)
            // which references segment 0x08 (TwoTexScroll for animated texture).
            if (gFormState.rollSpikeActive < 3 && gFormState.rollChargeLevel >= 5 && sEnergyEffect1DLCount > 0 &&
                sEnergyEffect2DLCount > 0) {

                f32 chargeScale = (gFormState.rollChargeLevel - 4) * 0.02f;
                u8 alpha;

                // Alpha calculation (from 2Ship z_player.c line 13135-13139)
                if (gFormState.rollSpikeActive != 0) {
                    alpha = (-gFormState.rollSpikeActive * 0x55) + 0xFF;
                } else {
                    alpha = (u8)(200.0f * chargeScale);
                    if (alpha > 200)
                        alpha = 200;
                }

                // Scale for energy effect (from 2Ship z_player.c line 13141-13147)
                if (gFormState.rollSpikeActive != 0) {
                    chargeScale = 0.65f;
                }

                Matrix_Scale(1.0f, chargeScale, chargeScale, MTXMODE_APPLY);

                gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                          G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

                // TwoTexScroll sub-DL for animated texture on energy effects.
                // Replaces AnimatedMat_DrawXlu with Matanimheader_013138.
                // Allocated once and shared by both energy DL copies.
                Gfx* twoTexScrollDL;
                {
                    u32 frames = play->gameplayFrames;
                    twoTexScrollDL = Gfx_TwoTexScroll(play->state.gfxCtx, 0, 0, 0, 0x40, 0x40, 1, frames * 2,
                                                      frames * 2, 0x40, 0x40);
                }

                // Set segment 0x08 on XLU pipe as well (belt-and-suspenders with the patch below)
                gSPSegment(POLY_XLU_DISP++, 0x08, (uintptr_t)twoTexScrollDL);

                // Draw energy effect 1 (grt_01_model / DL_0127B0)
                // env color (155,0,0,alpha) from 2Ship z_player.c line 13151
                gDPSetEnvColor(POLY_XLU_DISP++, 155, 0, 0, alpha);

                {
                    static const size_t DL_PADDING = 16;
                    size_t total = sEnergyEffect1DLCount + DL_PADDING;
                    Gfx* dlCopy = (Gfx*)Graph_Alloc(play->state.gfxCtx, total * sizeof(Gfx));
                    memcpy(dlCopy, sEnergyEffect1DLSafeCopy.data(), sEnergyEffect1DLCount * sizeof(Gfx));
                    for (size_t p = 0; p < DL_PADDING; p++) {
                        dlCopy[sEnergyEffect1DLCount + p].words.w0 = (uintptr_t)0xDF << 24;
                        dlCopy[sEnergyEffect1DLCount + p].words.w1 = 0;
                    }
                    // Patch standard G_DL(0x08000001) → direct pointer to TwoTexScroll.
                    // Bypasses segment table resolution which can be corrupted by OTR
                    // texture path strings in Release builds.
                    {
                        int pc08 = MmForm_PatchSegmentedDL(dlCopy, sEnergyEffect1DLCount, 0x08, twoTexScrollDL);
                        int pc0C = MmForm_PatchCullDLIndex(dlCopy, sEnergyEffect1DLCount);
                        static u8 sLoggedE1 = 0;
                        if (!sLoggedE1) {
                            MMFORM_LOG("[MmForm] EnergyEffect1DL: patched %d seg0x08, %d seg0x0C refs (count=%zu)",
                                       pc08, pc0C, sEnergyEffect1DLCount);
                            sLoggedE1 = 1;
                        }
                    }
                    gSPDisplayList(POLY_XLU_DISP++, dlCopy);
                }

                // Draw energy effect 2 (grt_02_model / DL_0134D0)
                // Matanimheader_014684: ColorChanging with cycling env color
                // EnvColors cycle between (100,0,0,255) and (200,0,0,255)
                // PrimColor is set by the DL itself (255,0,0,255 lodFrac=0x80)
                {
                    u32 colorFrame = play->gameplayFrames % 2;
                    u8 envR = (colorFrame == 0) ? 100 : 200;
                    gDPSetEnvColor(POLY_XLU_DISP++, envR, 0, 0, 255);
                }

                {
                    static const size_t DL_PADDING = 16;
                    size_t total = sEnergyEffect2DLCount + DL_PADDING;
                    Gfx* dlCopy = (Gfx*)Graph_Alloc(play->state.gfxCtx, total * sizeof(Gfx));
                    memcpy(dlCopy, sEnergyEffect2DLSafeCopy.data(), sEnergyEffect2DLCount * sizeof(Gfx));
                    for (size_t p = 0; p < DL_PADDING; p++) {
                        dlCopy[sEnergyEffect2DLCount + p].words.w0 = (uintptr_t)0xDF << 24;
                        dlCopy[sEnergyEffect2DLCount + p].words.w1 = 0;
                    }
                    // Patch standard G_DL(0x08000001) → direct pointer to TwoTexScroll.
                    {
                        int pc08 = MmForm_PatchSegmentedDL(dlCopy, sEnergyEffect2DLCount, 0x08, twoTexScrollDL);
                        int pc0C = MmForm_PatchCullDLIndex(dlCopy, sEnergyEffect2DLCount);
                        static u8 sLoggedE2 = 0;
                        if (!sLoggedE2) {
                            MMFORM_LOG("[MmForm] EnergyEffect2DL: patched %d seg0x08, %d seg0x0C refs (count=%zu)",
                                       pc08, pc0C, sEnergyEffect2DLCount);
                            sLoggedE2 = 1;
                        }
                    }
                    gSPDisplayList(POLY_XLU_DISP++, dlCopy);
                }
            }
        } else if (gFormState.goronAction == MMFORM_ACT_SHIELD && gFormState.currentForm == MM_PLAYER_FORM_GORON &&
                   gFormState.shieldSkelLoaded) {
            // Shield mode: draw gLinkGoronShieldingSkel (4-limb guard pose skeleton)
            // From 2Ship z_player.c line 13408-13411: SkelAnime_DrawFlexOpa for unk_2C8
            SkelAnime_DrawFlexOpa(play, gFormState.shieldSkelAnime.skeleton, gFormState.shieldSkelAnime.jointTable,
                                  gFormState.shieldSkelAnime.dListCount, NULL, NULL, &player->actor);
        } else {
            // OOT animation sharing: for actions that use link_normal_* animations
            // (walk, run, jump, fall, roll, z-target, ledge, damage,
            // landing, idle for Deku/FD, generic OOT yield), copy OOT's jointTable
            // so the MM skeleton displays OOT's animation perfectly synced.
            // Both skeletons have 22 limbs → LIMB_BUF_COUNT(22) = 24 Vec3s entries.
            // OverrideLimbDraw still applies rootAnimScale for correct form height.
            if ((MmForm_UsesOotAnim() || gFormState.currentForm == MM_PLAYER_FORM_FIERCE_DEITY) &&
                player->skelAnime.jointTable != NULL && gFormState.formSkelAnime.jointTable != NULL) {
                memcpy(gFormState.formSkelAnime.jointTable, player->skelAnime.jointTable,
                       sizeof(Vec3s) * PLAYER_LIMB_BUF_COUNT);
            }

            // Draw the MM form skeleton (with OOT or form-specific joints)
            SkelAnime_DrawFlexOpa(play, gFormState.formSkelAnime.skeleton, gFormState.formSkelAnime.jointTable,
                                  gFormState.formDListCount, MmForm_OverrideLimbDraw, MmForm_PostLimbDraw,
                                  &player->actor);
        }
    }

    // === Underground flower petals (Deku Flower phase 1-2) ===
    // From 2Ship z_player.c:13030-13070: draws 3 flower petals at surface while underground.
    // MM uses D_8085D574[] = { DL_009C48, DL_009AB8, DL_009DB8 } with keyframe animation.
    // Simplified: draw 3 petals at 0°/120°/240° with scale based on bud counter (0-8).
    if (gFormState.currentForm == MM_PLAYER_FORM_DEKU && gFormState.goronAction == MMFORM_ACT_DEKU_FLOWER &&
        gFormState.dekuFlowerDepth < -1000.0f) {
        static Gfx* sPetalDLs[3] = {
            (Gfx*)gLinkDekuFlowerPetal1DL,
            (Gfx*)gLinkDekuFlowerPetal2DL,
            (Gfx*)gLinkDekuFlowerPetal3DL,
        };

        Gfx_SetupDL_25Opa(play->state.gfxCtx);

        // Scale petals based on bud counter (0→tiny, 8→full size)
        f32 budScale = 0.003f + (gFormState.dekuBudCounter * 0.0015f); // 0.003 → 0.015

        for (s32 p = 0; p < 3; p++) {
            Matrix_Translate(player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z,
                             MTXMODE_NEW);
            // Each petal at 120° offset (0x5555 = 120° in s16)
            Matrix_RotateY(BINANG_TO_RAD(player->actor.shape.rot.y + (s16)(p * 0x5555)), MTXMODE_APPLY);
            Matrix_Scale(budScale, budScale, budScale, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                      G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, sPetalDLs[p]);
        }
    }

    // === bodyPartsPos / leftHandPos / focus.pos ===
    // For the normal skeleton draw path: MmForm_PostLimbDraw fills bodyPartsPos,
    // leftHandPos, actor.focus.pos, and feetPos from actual limb world matrices
    // (like OOT's Player_PostLimbDrawGameplay in z_player_lib.c:1836).
    //
    // For draw paths without PostLimbDraw (ball form, shield skeleton):
    // use position-based fallback so cylinder calculation and effects still work.
    {
        u8 needsFallback = 0;

        // Ball form: no skeleton traversal, PostLimbDraw never called
        if (gFormState.currentForm == MM_PLAYER_FORM_GORON &&
            (gFormState.goronAction == GORON_ACT_GORON_ROLL || gFormState.goronAction == GORON_ACT_GORON_ROLL_JUMP ||
             gFormState.goronAction == GORON_ACT_GORON_ROLL_POUND ||
             (gFormState.goronAction == MMFORM_ACT_WATER_VOID && gFormState.rollGroundPoundTimer >= 2))) {
            needsFallback = 1;
        }
        // Shield skeleton: draws with NULL callbacks (no PostLimbDraw)
        if (gFormState.goronAction == MMFORM_ACT_SHIELD && gFormState.currentForm == MM_PLAYER_FORM_GORON) {
            needsFallback = 1;
        }

        if (needsFallback) {
            const MmFormProperties* props = &sFormProps[gFormState.currentForm];
            f32 midY = player->actor.world.pos.y + props->cylinderHeight * 0.5f;

            for (s32 i = 0; i < PLAYER_BODYPART_MAX; i++) {
                player->bodyPartsPos[i].x = player->actor.world.pos.x;
                player->bodyPartsPos[i].y = midY;
                player->bodyPartsPos[i].z = player->actor.world.pos.z;
            }
            // Feet at ground, head at top (for cylinder height)
            player->bodyPartsPos[PLAYER_BODYPART_L_FOOT].y = player->actor.world.pos.y;
            player->bodyPartsPos[PLAYER_BODYPART_R_FOOT].y = player->actor.world.pos.y;
            player->bodyPartsPos[PLAYER_BODYPART_HEAD].y = player->actor.world.pos.y + props->cylinderHeight - 10.0f;

            // Focus at center
            player->actor.focus.pos.x = player->actor.world.pos.x;
            player->actor.focus.pos.y = midY;
            player->actor.focus.pos.z = player->actor.world.pos.z;
        }
    }

    // Draw Deku bubble projectile (if active)
    MmForm_DrawBubbleProjectile(player, play);

    // Reset segments that MmForm set to MM-specific values.
    // These persist in the shared POLY_OPA/XLU buffers and will corrupt
    // subsequent actors (e.g. Poe Composer uses segment 0x08 for env color).
    gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)gEmptyDL);
    gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)gEmptyDL);
    gSPSegment(POLY_XLU_DISP++, 0x08, (uintptr_t)gEmptyDL);
    gSPSegment(POLY_XLU_DISP++, 0x09, (uintptr_t)gEmptyDL);
    gSPSegment(POLY_XLU_DISP++, 0x0A, (uintptr_t)gEmptyDL);
    gSPSegment(POLY_XLU_DISP++, 0x0B, (uintptr_t)gEmptyDL);
    gSPSegment(POLY_XLU_DISP++, 0x0C, (uintptr_t)gCullBackDList);

    // === Ground pound crack decal (from 2Ship ACTOR_EN_TEST) ===
    // Draws a dark circle on the floor at the ground pound impact position.
    // EN_TEST in MM uses KFSkelAnimeFlex (12-limb keyframe skeleton) which OOT lacks.
    // We use gCircleShadowDL with dark prim color on XLU as a simplified impact mark.
    // Alpha fades from 200 → 0 over 30 frames, matching EN_TEST's ~30 frame lifecycle.
    if (gFormState.groundPoundCrackTimer > 0 && gFormState.groundPoundFloorPoly != NULL) {
        MtxF floorMtx;
        f32 impactX = gFormState.groundPoundImpactPos.x;
        f32 impactY = gFormState.groundPoundImpactPos.y;
        f32 impactZ = gFormState.groundPoundImpactPos.z;

        // Get floor-aligned matrix (from z_actor.c ActorShadow_Draw / func_80038A28)
        func_80038A28(gFormState.groundPoundFloorPoly, impactX, impactY, impactZ, &floorMtx);
        Matrix_Put(&floorMtx);

        // Scale: large circle (~3x player shadow) to represent impact area
        // EN_TEST uses scale = params/100000 = 500/100000 = 0.005, with skeleton expanding it.
        // The circle shadow DL is unit-sized, so we scale to match the damage radius (~60 units).
        f32 crackScale = 3.0f;
        Matrix_Scale(crackScale, 1.0f, crackScale, MTXMODE_APPLY);

        // Alpha: fade from 200 → 0 over 30 frames (like EN_TEST's unk_209 counter)
        u8 crackAlpha = (u8)((gFormState.groundPoundCrackTimer * 200) / 30);

        // Setup XLU display list for translucent ground decal
        POLY_XLU_DISP = Gfx_SetupDL(POLY_XLU_DISP, 0x2C);
        gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED, 0, 0, 0,
                          COMBINED);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 0, 0, 0, crackAlpha);
        gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                  G_MTX_MODELVIEW | G_MTX_LOAD);
        gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gCircleShadowDL);
    }

    // Zora Electric Barrier draw (from 2Ship Player_DrawZoraShield, z_player_lib.c:2316)
    if (gFormState.currentForm == MM_PLAYER_FORM_ZORA && gFormState.barrierIntensity > 0) {
        MmForm_DrawZoraBarrier(player, play);
    }

    // Draw screen flash during transformation/detransformation
    // This is drawn even without skeleton (during cutscene build-up, OOT Link is visible underneath)
    if (gFormState.flashAlpha > 0) {
        Gfx_SetupDL_44Xlu(play->state.gfxCtx);

        gDPPipeSync(POLY_XLU_DISP++);
        gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0,
                          PRIMITIVE);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 220, 220, 220, (u8)gFormState.flashAlpha);
        gDPFillRectangle(POLY_XLU_DISP++, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
        gDPPipeSync(POLY_XLU_DISP++);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

void MmForm_Reset(void) {
    // Clear pending reactivation (manual detransform should not re-activate on next scene)
    sPendingReactivate = 0;
    sForceInstantTransform = 0;

    // Restore pre-transform equips (no icon reload, HUD refreshes next frame)
    if (sEquipsSaved) {
        for (s32 i = 1; i < 8; i++) {
            gSaveContext.equips.buttonItems[i] = sPreTransformEquips.buttonItems[i];
            gSaveContext.equips.cButtonSlots[i - 1] = sPreTransformEquips.cButtonSlots[i - 1];
        }
        sEquipsSaved = 0;
    }

    if (gFormState.state != MMFORM_STATE_INACTIVE) {
        MmForm_FreeRootMotion();

        // Cleanup Zora barrier light and fog tint
        // Note: barrierLight removal requires PlayState but Reset may be called
        // without one. The light will be cleaned up when the scene unloads.
        gFormState.barrierLight = NULL;
        gFormState.barrierIntensity = 0;
        gFormState.barrierActive = 0;
        gFormState.barrierColliderInit = 0;
        if (gPlayState != NULL) {
            gPlayState->envCtx.adjFogColor[0] = 0;
            gPlayState->envCtx.adjFogColor[1] = 0;
            gPlayState->envCtx.adjFogColor[2] = 0;
            gPlayState->envCtx.adjFogNear = 0;
        }

        // Cleanup boomerang state
        gFormState.boomerangState = 0;
        gFormState.boomerangActorL = NULL;
        gFormState.boomerangActorR = NULL;
        gFormState.boomerangAimYaw = 0;
        gFormState.boomerangAimPitch = 0;
        if (gPlayState != NULL) {
            Player* player = GET_PLAYER(gPlayState);
            player->stateFlags1 &= ~(PLAYER_STATE1_BOOMERANG_THROWN | PLAYER_STATE1_PARALLEL);
            player->boomerangActor = NULL;
            player->upperLimbRot.y = 0;
            player->upperLimbRot.x = 0;
        }

        // Cleanup hazard void state
        gFormState.hazardVoidType = 0;
        gFormState.hazardVoidTimer = 0;

        // Cleanup swim state
        gFormState.swimState = 0;
        gFormState.swimPitch = 0;
        gFormState.swimRoll = 0;
        gFormState.zoraBoots = 0;
        gFormState.fastSwimActive = 0;
        gFormState.swimRollSmoothed = 0;
        gFormState.swimPhase = 0;
        gFormState.swimPhaseCounter = 0;
        gFormState.swimSpeedB48 = 0.0f;
        gFormState.swimYawRate = 0;
        gFormState.swimExitFlag = 0;
        gFormState.swimFloorTimer = 0;
        gFormState.bootToggleDelay = 0;

        gFormState.state = MMFORM_STATE_INACTIVE;
        gFormState.currentForm = MM_PLAYER_FORM_HUMAN;
        gFormState.skeletonLoaded = 0;
        gFormState.flashAlpha = 0;
        gFormState.wasOnGround = 1;
        gFormState.jumpKickActive = 0;
        gFormState.sidehopDir = 0;
        gFormState.rollSpeed = 0.0f;
        gFormState.dekuHopsRemaining = 5;

        // Cleanup Deku flower/flight state
        gFormState.dekuFlowerDepth = 0.0f;
        gFormState.dekuFlowerVelocity = 0.0f;
        gFormState.dekuFlowerPhase = 0;
        gFormState.dekuFlowerCharge = 0;
        gFormState.dekuBudCounter = 0;
        gFormState.dekuLaunchPos = { 0.0f, 0.0f, 0.0f };
        gFormState.dekuFlightFlags = 0;
        gFormState.dekuPetalSpeed = 0;
        gFormState.dekuPetalAngle = 0;
        gFormState.dekuPitchAngle = 0;
        gFormState.dekuRollAngle = 0;
        gFormState.dekuFlightTimer = 0;
        gFormState.dekuFlightLaunchType = 0;
        gFormState.dekuSparkleAcc = 0;
        gFormState.dekuSavedShadowScale = 0.0f;

        gFormState.formDLsPinned = 0;
        MmForm_ClearCachedDLs();
        MmForm_UnpinFormResources();

        // Clear pending damage to prevent stale data
        gMmFormPendingDamage.hasPending = 0;

        // Clear ground pound crack visual
        gFormState.groundPoundCrackTimer = 0;
        gFormState.groundPoundFloorPoly = NULL;
    }
}

// =============================================================================
// Network Visual State Accessors (for Harpoon multiplayer sync)
// Expose gFormState fields to the networking system via C linkage.
// =============================================================================

u8 MmForm_GetModelType(void) {
    if (gFormState.state == MMFORM_STATE_INACTIVE)
        return 0;
    switch (gFormState.currentForm) {
        case MM_PLAYER_FORM_GORON:
            return 1;
        case MM_PLAYER_FORM_ZORA:
            return 2;
        case MM_PLAYER_FORM_DEKU:
            return 3;
        case MM_PLAYER_FORM_FIERCE_DEITY:
            return 4;
        default:
            return 0;
    }
}

// gMmPlayer is in the z_player.c TU; route through transformation_masks.c accessors.
extern u32 MmPlayerRaw_GetStateFlags3(void);
extern f32 MmPlayerRaw_GetSpeedXZ(void);

u32 MmForm_GetStateFlags3(void) {
    return MmPlayerRaw_GetStateFlags3();
}

f32 MmForm_GetSpeedXZ(void) {
    return MmPlayerRaw_GetSpeedXZ();
}

Vec3s* MmForm_GetJointTable(void) {
    if (!gFormState.skeletonLoaded || gFormState.formSkelAnime.jointTable == NULL)
        return NULL;
    return gFormState.formSkelAnime.jointTable;
}

s32 MmForm_GetJointCount(void) {
    if (!gFormState.skeletonLoaded)
        return 0;
    // formLimbCount + 2 for root position + root rotation (LIMB_BUF_COUNT pattern)
    return gFormState.formLimbCount + 2;
}

s32 MmForm_GetGoronAction(void) {
    return gFormState.goronAction;
}

u8 MmForm_GetEyeIndex(void) {
    return gFormState.eyeIndex;
}

f32 MmForm_GetRollSquash(void) {
    return gFormState.rollSquash;
}

s16 MmForm_GetRollSpikeActive(void) {
    return gFormState.rollSpikeActive;
}

s16 MmForm_GetRollChargeLevel(void) {
    return gFormState.rollChargeLevel;
}

} // extern "C"
