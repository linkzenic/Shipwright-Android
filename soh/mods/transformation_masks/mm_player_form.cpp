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

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "mods/transformation_masks/transformation_masks.h"
#include "mods/transformation_masks/assets/mm_asset_loader.h"
#include "mods/anim_translator/mm_anim_loader.h"
}

#include "soh/OTRGlobals.h"

#include <libultraship/log/luslog.h>

#define MMFORM_LOG(fmt, ...) lusprintf(__FILE__, __LINE__, LUSLOG_LEVEL_INFO, fmt, ##__VA_ARGS__)

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
    { "objects/object_link_boy/gLinkFierceDeitySkel", MM_FORM_LIMB_COUNT,
      "misc/link_animetion/gPlayerAnim_link_fighter_wait_long_Data", 148,
      "misc/link_animetion/gPlayerAnim_link_normal_walk_free_Data", 17,
      "misc/link_animetion/gPlayerAnim_link_normal_run_free_Data", 17, 84.0f, 90.0f, 27.0f, 100, 24.0f, 68.0f, 0.0f },
    // GORON (index 1) - Idle is form-specific, walk/run use shared human anims
    { "objects/object_link_goron/gLinkGoronSkel", MM_FORM_LIMB_COUNT, "misc/link_animetion/gPlayerAnim_pg_wait_Data",
      79, "misc/link_animetion/gPlayerAnim_link_normal_walk_free_Data", 17,
      "misc/link_animetion/gPlayerAnim_link_normal_run_free_Data", 17, 70.0f, 90.0f, 19.5f, 200, 24.0f, 62.0f, 0.0f },
    // ZORA (index 2)
    { "objects/object_link_zora/gLinkZoraSkel", MM_FORM_LIMB_COUNT, "misc/link_animetion/gPlayerAnim_pz_wait_Data", 80,
      "misc/link_animetion/gPlayerAnim_link_normal_walk_free_Data", 17,
      "misc/link_animetion/gPlayerAnim_link_normal_run_free_Data", 17, 56.0f, 90.0f, 18.0f, 80, 20.0f, 58.0f, 0.0f },
    // DEKU (index 3)
    { "objects/object_link_nuts/gLinkDekuSkel", MM_FORM_LIMB_COUNT, "misc/link_animetion/gPlayerAnim_pn_wait_Data", 80,
      "misc/link_animetion/gPlayerAnim_link_normal_walk_free_Data", 17,
      "misc/link_animetion/gPlayerAnim_link_normal_run_free_Data", 17, 35.0f, 50.0f, 14.0f, 20, 16.0f, 40.0f, 0.0f },
    // HUMAN (index 4) - not used by transformation system
    { NULL, MM_FORM_LIMB_COUNT, NULL, 0, NULL, 0, NULL, 0, 40.0f, 60.0f, 14.0f, 50, 12.0f, 50.0f, 0.0f },
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

// Deku eye textures - Deku head DL may not use segment 0x08
// (Deku has simpler painted-on eyes, textures TBD when testing Deku form)
// TODO: Find Deku eye texture OTR paths in 2Ship assets if they exist

// Fierce Deity eye textures (from object_link_boy.h) - uses same names as human Link
static const char sFierceEyeOpen[] = "__OTR__objects/object_link_boy/gLinkFierceDeityEyesOpenTex";
static const char sFierceEyeHalf[] = "__OTR__objects/object_link_boy/gLinkFierceDeityEyesHalfTex";
static const char sFierceEyeClosed[] = "__OTR__objects/object_link_boy/gLinkFierceDeityEyesClosedTex";

// Zora mouth textures (from object_link_zora.h)
// Zora uses segment 0x09 for mouth. Other forms don't.
static const char sZoraMouthClosed[] = "__OTR__objects/object_link_zora/gLinkZoraMouthClosedTex";

// Per-form eye texture arrays indexed by eye state (0=open, 1=half, 2=closed, 3=special)
// From 2Ship sPlayerEyesTextures[playerForm][eyeIndex]
static const char* sFormEyeTextures[MM_PLAYER_FORM_MAX][4] = {
    // FIERCE_DEITY
    { sFierceEyeOpen, sFierceEyeHalf, sFierceEyeClosed, sFierceEyeOpen },
    // GORON
    { sGoronEyeOpen, sGoronEyeHalf, sGoronEyeClosed, sGoronEyeSurprised },
    // ZORA
    { sZoraEyeOpen, sZoraEyeHalf, sZoraEyeClosed, sZoraEyeOpen },
    // DEKU (eye textures TBD - may not use segment 0x08)
    { NULL, NULL, NULL, NULL },
    // HUMAN (not used)
    { NULL, NULL, NULL, NULL },
};

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

    // Movement animation state (synced with OOT's actual speed)
    // From 2Ship: idle when stopped, walk at low speed, run at speedTarget > 4.0f
    u8 moveState; // 0=idle, 1=walk, 2=run

    // Blink system (from 2Ship FaceChange_UpdateBlinkingNonHuman)
    s16 blinkTimer; // Counts down, blink happens in last 3 frames
    u8 eyeIndex;    // 0=open, 1=half, 2=closed

    // Saved OOT state for restoration
    f32 savedShadowScale;
    u8 savedMass;
} MmFormState;

static MmFormState gFormState;

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

static u8 MmForm_LoadFormSkeleton(PlayState* play, MmPlayerTransformation form) {
    const MmFormProperties* props = &sFormProps[form];

    if (props->skelPath == NULL) {
        MMFORM_LOG("[MmForm] No skeleton for form %d", form);
        return 0;
    }

    // Load skeleton from mm.o2r
    FlexSkeletonHeader* skelHeader = (FlexSkeletonHeader*)MmAssets_LoadResource(props->skelPath);
    if (skelHeader == NULL) {
        MMFORM_LOG("[MmForm] Failed to load skeleton: %s", props->skelPath);
        return 0;
    }

    MMFORM_LOG("[MmForm] Loaded skeleton: %s (limbs=%d)", props->skelPath, props->limbCount);

    // Load idle animation
    gFormState.idleAnim = MmAnim_LoadByPath(props->idleAnimPath, props->idleAnimFrames, (u8)props->limbCount);
    if (gFormState.idleAnim == NULL) {
        MMFORM_LOG("[MmForm] Failed to load idle anim: %s", props->idleAnimPath);
        return 0;
    }

    // Load walk animation
    gFormState.walkAnim = NULL;
    if (props->walkAnimPath != NULL) {
        gFormState.walkAnim = MmAnim_LoadByPath(props->walkAnimPath, props->walkAnimFrames, (u8)props->limbCount);
        if (gFormState.walkAnim == NULL) {
            MMFORM_LOG("[MmForm] Walk anim not found, will use idle: %s", props->walkAnimPath);
        }
    }

    // Load run animation (from 2Ship D_8085BE84: all forms share link_normal_run_free)
    gFormState.runAnim = NULL;
    if (props->runAnimPath != NULL) {
        gFormState.runAnim = MmAnim_LoadByPath(props->runAnimPath, props->runAnimFrames, (u8)props->limbCount);
        if (gFormState.runAnim == NULL) {
            MMFORM_LOG("[MmForm] Run anim not found, will use walk: %s", props->runAnimPath);
        }
    }

    // Initialize SkelAnime with MM skeleton
    // Pass NULL for jointTable/morphTable to let SkelAnime_InitLink allocate them.
    // This avoids the limbBufCount == limbCount assertion (flags=9 adds +1 for root).
    gFormState.formLimbCount = props->limbCount;

    SkelAnime_InitLink(play, &gFormState.formSkelAnime, skelHeader, gFormState.idleAnim, 9, NULL, NULL,
                       props->limbCount);

    gFormState.formDListCount = gFormState.formSkelAnime.dListCount;
    gFormState.skeletonLoaded = 1;
    gFormState.moveState = 0;

    // Initialize blink with random first interval (20-100 frames)
    gFormState.blinkTimer = 20 + (s16)(Rand_ZeroFloat(80.0f));
    gFormState.eyeIndex = 0;

    MMFORM_LOG("[MmForm] Skeleton loaded successfully for form %d", form);
    return 1;
}

static void MmForm_ApplyFormProperties(Player* player, MmPlayerTransformation form) {
    const MmFormProperties* props = &sFormProps[form];

    // Save OOT state for restoration
    gFormState.savedMass = player->actor.colChkInfo.mass;
    gFormState.savedShadowScale = player->actor.shape.shadowScale;

    // Apply form properties
    player->actor.colChkInfo.mass = props->mass;
    player->actor.shape.shadowScale = props->shadowScale;

    // Apply collider dimensions
    player->cylinder.dim.radius = (s16)props->cylinderRadius;
    player->cylinder.dim.height = (s16)props->cylinderHeight;
    player->cylinder.dim.yShift = (s16)props->cylinderYShift;

    MMFORM_LOG("[MmForm] Applied properties: mass=%d, shadow=%.0f, cyl=%.0f/%.0f", props->mass, props->shadowScale,
               props->cylinderRadius, props->cylinderHeight);
}

static void MmForm_RestoreOotState(Player* player) {
    player->actor.colChkInfo.mass = gFormState.savedMass;
    player->actor.shape.shadowScale = gFormState.savedShadowScale;

    // Restore default OOT Link collider
    player->cylinder.dim.radius = 12;
    player->cylinder.dim.height = 50;
    player->cylinder.dim.yShift = 0;

    MMFORM_LOG("[MmForm] Restored OOT state");
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
// Movement System (from 2Ship Player_GetMovementSpeedAndYaw)
// =============================================================================

static void MmForm_UpdateMovement(Player* player, PlayState* play) {
    // === Layer approach: OOT's actionFunc already handles velocity/yaw. ===
    // MM does NOT have per-form speed caps or acceleration differences.
    // All forms use the same Player_CalcSpeedAndYawFromControlStick (2Ship line 5205).
    // Only Fierce Deity gets 1.5x speed multiplier (line 5236).
    //
    // What we DO control:
    //   1. Animation state transitions (idle/walk/run) matching MM thresholds
    //   2. Animation playback rate proportional to speed (MM line 14648)
    //   3. Tick our separate SkelAnime

    f32 speed = player->linearVelocity;

    // Determine movement state (from 2Ship z_player.c):
    // - Idle: speed ~0 (Player_Action_Idle, line 14787: speedTarget == 0.0f)
    // - Walk: speed > 0 but <= 4.0f (Player_Action_2, line 14648)
    // - Run: speed > 4.0f (transition at line 14648: speedTarget > 4.0f → func_8083B030)
    u8 newState;
    if (speed < 0.5f) {
        newState = 0; // idle
    } else if (speed <= 4.0f) {
        newState = 1; // walk
    } else {
        newState = 2; // run
    }

    // Transition animation on state change
    if (newState != gFormState.moveState) {
        gFormState.moveState = newState;

        LinkAnimationHeader* targetAnim = NULL;
        f32 playSpeed = 1.0f;

        switch (newState) {
            case 0: // idle
                targetAnim = gFormState.idleAnim;
                playSpeed = 1.0f;
                break;
            case 1: // walk
                targetAnim = gFormState.walkAnim ? gFormState.walkAnim : gFormState.idleAnim;
                // MM walk anim rate: speedXZ * 0.3f + 1.0f (2Ship line 14648: func_8083EA44)
                playSpeed = speed * 0.3f + 1.0f;
                break;
            case 2: // run
                targetAnim = gFormState.runAnim ? gFormState.runAnim
                                                : (gFormState.walkAnim ? gFormState.walkAnim : gFormState.idleAnim);
                // MM run plays at ~1.5x base (scales with speed in later phases)
                playSpeed = 1.5f;
                break;
        }

        if (targetAnim != NULL) {
            LinkAnimation_Change(play, &gFormState.formSkelAnime, targetAnim, playSpeed, 0.0f,
                                 Animation_GetLastFrame(targetAnim), ANIMMODE_LOOP, -8.0f);
        }
    }

    // Update playback rate each frame for walk/run (proportional to speed)
    // From 2Ship line 14648: func_8083EA44(this, this->speedXZ * 0.3f + 1.0f)
    if (gFormState.moveState == 1) {
        gFormState.formSkelAnime.playSpeed = speed * 0.3f + 1.0f;
    } else if (gFormState.moveState == 2) {
        // Run anim rate scales less aggressively
        gFormState.formSkelAnime.playSpeed = speed * 0.15f + 1.0f;
    }

    // Tick MM form animation (separate from OOT's player->skelAnime)
    LinkAnimation_Update(play, &gFormState.formSkelAnime);
}

// =============================================================================
// Transformation Cutscene
// =============================================================================

static void MmForm_UpdateTransforming(Player* player, PlayState* play) {
    u8 instantTransform = CVarGetInteger("gMods.TransformMasks.InstantTransform", 0);

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
                return;
            }

            MmForm_ApplyFormProperties(player, gFormState.targetForm);
            gFormState.currentForm = gFormState.targetForm;

            // Play flash SFX
            if (MmSfx_IsAvailable()) {
                MmSfx_PlayTransformFlash();
            }
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

            gFormState.state = MMFORM_STATE_ACTIVE;
            MMFORM_LOG("[MmForm] Instant transform complete -> ACTIVE (form=%d)", gFormState.currentForm);
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
                if (gFormState.cutsceneTimer == 4 && MmSfx_IsAvailable()) {
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
                        return;
                    }

                    MmForm_ApplyFormProperties(player, gFormState.targetForm);
                    gFormState.currentForm = gFormState.targetForm;

                    // Play flash SFX
                    if (MmSfx_IsAvailable()) {
                        MmSfx_PlayTransformFlash();
                    }
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

                    gFormState.state = MMFORM_STATE_ACTIVE;
                    MMFORM_LOG("[MmForm] Cutscene transform complete -> ACTIVE (form=%d)", gFormState.currentForm);
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
            if (MmSfx_IsAvailable()) {
                MmSfx_PlayTransformFlash();
            }
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
            MMFORM_LOG("[MmForm] Instant de-transform complete -> INACTIVE");
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

                    if (MmSfx_IsAvailable()) {
                        MmSfx_PlayTransformFlash();
                    }

                    gFormState.cutscenePhase = 2;
                }
                break;

            case 2: // Post-flash: fade, unfreeze
                gFormState.flashAlpha -= 20;
                if (gFormState.flashAlpha <= 0) {
                    gFormState.flashAlpha = 0;
                    player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
                    gFormState.state = MMFORM_STATE_INACTIVE;
                    MMFORM_LOG("[MmForm] Cutscene de-transform complete -> INACTIVE");
                }
                break;
        }
    }
}

// =============================================================================
// Draw System
// =============================================================================

static s32 MmForm_OverrideLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* thisx) {
    // Default: draw all limbs as-is from MM skeleton
    return 0;
}

static void MmForm_PostLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* thisx) {
    // Future: per-limb effects (Goron spikes, etc.)
}

// =============================================================================
// Public API (extern "C")
// =============================================================================

extern "C" {

void MmForm_Init(PlayState* play, Player* player) {
    (void)play;
    (void)player;

    memset(&gFormState, 0, sizeof(gFormState));
    gFormState.state = MMFORM_STATE_INACTIVE;
    gFormState.currentForm = MM_PLAYER_FORM_HUMAN;
    gFormState.initialized = 1;

    MMFORM_LOG("[MmForm] Initialized (mm.o2r available=%d)", MmAssets_IsAvailable());
}

u8 MmForm_IsEnabled(void) {
    return CVarGetInteger("gMods.TransformMasks.Enabled", 0) && MmAssets_IsAvailable();
}

u8 MmForm_IsTransformed(void) {
    return gFormState.state == MMFORM_STATE_ACTIVE || gFormState.state == MMFORM_STATE_TRANSFORMING ||
           gFormState.state == MMFORM_STATE_DETRANSFORMING;
}

TransformMaskId MmForm_GetMaskType(s32 item) {
    switch (item) {
        case ITEM_MASK_GORON:
            return TRANSFORM_MASK_GORON;
        case ITEM_MASK_ZORA:
            return TRANSFORM_MASK_ZORA;
        case ITEM_MASK_SKULL:
            return TRANSFORM_MASK_DEKU;
        case ITEM_MASK_GERUDO:
            return TRANSFORM_MASK_FIERCE_DEITY;
        default:
            return TRANSFORM_MASK_NONE;
    }
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
        MMFORM_LOG("[MmForm] De-transform requested (form=%d)", gFormState.currentForm);
        gFormState.state = MMFORM_STATE_DETRANSFORMING;
        gFormState.cutsceneTimer = 0;
        gFormState.cutscenePhase = 0;
        gFormState.flashAlpha = 0;
        return;
    }

    // If already transformed to a different form -> de-transform first, then re-transform
    // For now: instant switch (future: chain cutscenes)
    if (gFormState.state == MMFORM_STATE_ACTIVE && gFormState.currentForm != targetForm) {
        MMFORM_LOG("[MmForm] Switching form %d -> %d", gFormState.currentForm, targetForm);
        MmForm_RestoreOotState(player);
        gFormState.skeletonLoaded = 0;
    }

    // Start transformation
    MMFORM_LOG("[MmForm] Transform requested -> form %d", targetForm);
    gFormState.targetForm = targetForm;
    gFormState.state = MMFORM_STATE_TRANSFORMING;
    gFormState.cutsceneTimer = 0;
    gFormState.cutscenePhase = 0;
    gFormState.flashAlpha = 0;
}

void MmForm_Update(PlayState* play, Player* player) {
    if (!gFormState.initialized)
        return;

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
            // Run MM movement system
            MmForm_UpdateMovement(player, play);
            break;

        case MMFORM_STATE_DETRANSFORMING:
            MmForm_UpdateDetransforming(player, play);
            break;
    }
}

void MmForm_Draw(PlayState* play, Player* player) {
    // Only draw when we have a loaded skeleton
    if (!gFormState.skeletonLoaded)
        return;
    if (gFormState.state == MMFORM_STATE_INACTIVE)
        return;

    OPEN_DISPS(play->state.gfxCtx);

    // Setup render state
    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    // Actor_Draw already set the correct matrix before calling Player_Draw:
    //   Matrix_SetTranslateRotateYXZ(pos.x, pos.y + yOffset*scale.y, pos.z, &shape.rot)
    //   Matrix_Scale(scale.x, scale.y, scale.z, MTXMODE_APPLY)
    // We use that matrix as-is. No need to create our own.

    // Damage flicker effect
    if (player->invincibilityTimer > 0) {
        s32 flickerValue = CLAMP(50 - player->invincibilityTimer, 8, 40);
        player->damageFlickerAnimCounter += flickerValue;
    }

    // Set face texture segments (from 2Ship Player_DrawImpl)
    // Segment 0x08 = eye texture. The head DL in each form's skeleton references this segment.
    // Same pattern as OOT z_player_lib.c line 1079: gSPSegment(0x08, SEGMENTED_TO_VIRTUAL(eyeTex))
    {
        s32 form = (s32)gFormState.currentForm;
        u8 eyeIdx = gFormState.eyeIndex;
        if (eyeIdx > 3)
            eyeIdx = 0;

        if (form >= 0 && form < MM_PLAYER_FORM_MAX && sFormEyeTextures[form][eyeIdx] != NULL) {
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)sFormEyeTextures[form][eyeIdx]);
        }

        // Segment 0x09 = mouth texture (only Zora uses this)
        if (form == MM_PLAYER_FORM_ZORA) {
            gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)sZoraMouthClosed);
        }
    }

    // Draw the MM form skeleton
    SkelAnime_DrawFlexOpa(play, gFormState.formSkelAnime.skeleton, gFormState.formSkelAnime.jointTable,
                          gFormState.formDListCount, MmForm_OverrideLimbDraw, MmForm_PostLimbDraw, &player->actor);

    // Draw screen flash during transformation/detransformation
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
    if (gFormState.state != MMFORM_STATE_INACTIVE) {
        MMFORM_LOG("[MmForm] Reset called, forcing INACTIVE");
        gFormState.state = MMFORM_STATE_INACTIVE;
        gFormState.currentForm = MM_PLAYER_FORM_HUMAN;
        gFormState.skeletonLoaded = 0;
        gFormState.flashAlpha = 0;
    }
}

} // extern "C"
