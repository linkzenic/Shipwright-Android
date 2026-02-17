/**
 * @file object_link_goron.h
 * @brief MM Goron form assets - skeleton, DLs, textures
 *
 * OTR paths for mm.o2r. Use directly with gSPDisplayList or MmAssets_LoadResource.
 * Pure #define format to avoid linker issues with static variables.
 */

#ifndef MM_OBJECT_LINK_GORON_H
#define MM_OBJECT_LINK_GORON_H

// ============================================================================
// Skeleton
// ============================================================================

#define gLinkGoronSkel "__OTR__objects/object_link_goron/gLinkGoronSkel"
#define gLinkGoronShieldingSkel "__OTR__objects/object_link_goron/gLinkGoronShieldingSkel"
#define gLinkGoronShieldingAnim "__OTR__objects/object_link_goron/gLinkGoronShieldingAnim"

// Shielding skeleton has 4 limbs (from 2Ship: Root, Body, Head, ArmsAndLegs)
#define LINK_GORON_SHIELDING_LIMB_MAX 5

// ============================================================================
// Ball/Curled Form DLs
// ============================================================================

#define gLinkGoronCurledDL "__OTR__objects/object_link_goron/gLinkGoronCurledDL"
#define gLinkGoronRollingSpikesAndEffectDL "__OTR__objects/object_link_goron/gLinkGoronRollingSpikesAndEffectDL"

// Individual sub-DLs of gLinkGoronRollingSpikesAndEffectDL
// (for drawing spike geometry on OPA and energy effects on XLU separately)
#define object_link_goron_DL_00C540 \
    "__OTR__objects/object_link_goron/object_link_goron_DL_00C540" // lg_spike_model (physical spikes)
#define object_link_goron_DL_0127B0 \
    "__OTR__objects/object_link_goron/object_link_goron_DL_0127B0" // grt_01_model (energy effect 1)
#define object_link_goron_DL_0134D0 \
    "__OTR__objects/object_link_goron/object_link_goron_DL_0134D0" // grt_02_model (energy effect 2)

// ============================================================================
// Hand DLs
// ============================================================================

#define gLinkGoronLeftHandOpenDL "__OTR__objects/object_link_goron/gLinkGoronLeftHandOpenDL"
#define gLinkGoronLeftHandClosedDL "__OTR__objects/object_link_goron/gLinkGoronLeftHandClosedDL"
#define gLinkGoronLeftHandHoldBottleDL "__OTR__objects/object_link_goron/gLinkGoronLeftHandHoldBottleDL"
#define gLinkGoronRightHandOpenDL "__OTR__objects/object_link_goron/gLinkGoronRightHandOpenDL"
#define gLinkGoronRightHandClosedDL "__OTR__objects/object_link_goron/gLinkGoronRightHandClosedDL"

// ============================================================================
// Limb DLs
// ============================================================================

#define gLinkGoronTorsoDL "__OTR__objects/object_link_goron/gLinkGoronTorsoDL"
#define gLinkGoronHeadDL "__OTR__objects/object_link_goron/gLinkGoronHeadDL"
#define gLinkGoronHatDL "__OTR__objects/object_link_goron/gLinkGoronHatDL"
#define gLinkGoronCollarDL "__OTR__objects/object_link_goron/gLinkGoronCollarDL"
#define gLinkGoronWaistDL "__OTR__objects/object_link_goron/gLinkGoronWaistDL"
#define gLinkGoronLeftUpperArmDL "__OTR__objects/object_link_goron/gLinkGoronLeftUpperArmDL"
#define gLinkGoronLeftForearmDL "__OTR__objects/object_link_goron/gLinkGoronLeftForearmDL"
#define gLinkGoronRightUpperArmDL "__OTR__objects/object_link_goron/gLinkGoronRightUpperArmDL"
#define gLinkGoronRightForearmDL "__OTR__objects/object_link_goron/gLinkGoronRightForearmDL"
#define gLinkGoronLeftThighDL "__OTR__objects/object_link_goron/gLinkGoronLeftThighDL"
#define gLinkGoronLeftShinDL "__OTR__objects/object_link_goron/gLinkGoronLeftShinDL"
#define gLinkGoronLeftFootDL "__OTR__objects/object_link_goron/gLinkGoronLeftFootDL"
#define gLinkGoronRightThighDL "__OTR__objects/object_link_goron/gLinkGoronRightThighDL"
#define gLinkGoronRightShinDL "__OTR__objects/object_link_goron/gLinkGoronRightShinDL"
#define gLinkGoronRightFootDL "__OTR__objects/object_link_goron/gLinkGoronRightFootDL"

// ============================================================================
// Effect DLs
// ============================================================================

#define gLinkGoronGoronPunchEffectDL "__OTR__objects/object_link_goron/gLinkGoronGoronPunchEffectDL"

// ============================================================================
// Eye Textures
// ============================================================================

#define gLinkGoronEyesOpenTex "__OTR__objects/object_link_goron/gLinkGoronEyesOpenTex"
#define gLinkGoronEyesHalfTex "__OTR__objects/object_link_goron/gLinkGoronEyesHalfTex"
#define gLinkGoronEyesClosedTex "__OTR__objects/object_link_goron/gLinkGoronEyesClosedTex"
#define gLinkGoronEyesSurprisedTex "__OTR__objects/object_link_goron/gLinkGoronEyesSurprisedTex"

// ============================================================================
// Limb Enum
// ============================================================================

typedef enum {
    LINK_GORON_LIMB_NONE,
    LINK_GORON_LIMB_ROOT,
    LINK_GORON_LIMB_WAIST,
    LINK_GORON_LIMB_LEFT_THIGH,
    LINK_GORON_LIMB_LEFT_SHIN,
    LINK_GORON_LIMB_LEFT_FOOT,
    LINK_GORON_LIMB_RIGHT_THIGH,
    LINK_GORON_LIMB_RIGHT_SHIN,
    LINK_GORON_LIMB_RIGHT_FOOT,
    LINK_GORON_LIMB_TORSO,
    LINK_GORON_LIMB_LEFT_UPPER_ARM,
    LINK_GORON_LIMB_LEFT_FOREARM,
    LINK_GORON_LIMB_LEFT_HAND,
    LINK_GORON_LIMB_RIGHT_UPPER_ARM,
    LINK_GORON_LIMB_RIGHT_FOREARM,
    LINK_GORON_LIMB_RIGHT_HAND,
    LINK_GORON_LIMB_HEAD,
    LINK_GORON_LIMB_HAT,
    LINK_GORON_LIMB_COLLAR,
    LINK_GORON_LIMB_MAX
} LinkGoronLimb;

#endif // MM_OBJECT_LINK_GORON_H
