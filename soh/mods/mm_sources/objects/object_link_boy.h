/**
 * @file object_link_boy.h
 * @brief MM Fierce Deity form assets - skeleton, DLs, textures
 *
 * OTR paths for mm.o2r. Use directly with gSPDisplayList or MmAssets_LoadResource.
 */

#ifndef MM_OBJECT_LINK_BOY_H
#define MM_OBJECT_LINK_BOY_H

// ============================================================================
// Skeleton
// ============================================================================

#define gLinkFierceDeitySkel "__OTR__objects/object_link_boy/gLinkFierceDeitySkel"

// ============================================================================
// Weapon DLs
// ============================================================================

#define gLinkFierceDeitySwordDL "__OTR__objects/object_link_boy/gLinkFierceDeitySwordDL"
#define gLinkFierceDeityBottleDL "__OTR__objects/object_link_boy/gLinkFierceDeityBottleDL"

// ============================================================================
// Hand DLs
// ============================================================================

#define gLinkFierceDeityLeftHandHoldingSwordDL "__OTR__objects/object_link_boy/gLinkFierceDeityLeftHandHoldingSwordDL"
#define gLinkFierceDeityLeftHandEmptyDL "__OTR__objects/object_link_boy/gLinkFierceDeityLeftHandEmptyDL"
#define gLinkFierceDeityLeftHandHoldingBottleDL "__OTR__objects/object_link_boy/gLinkFierceDeityLeftHandHoldingBottleDL"
#define gLinkFierceDeityRightHandHoldingSwordDL "__OTR__objects/object_link_boy/gLinkFierceDeityRightHandHoldingSwordDL"
#define gLinkFierceDeityRightHandEmptyDL "__OTR__objects/object_link_boy/gLinkFierceDeityRightHandEmptyDL"

// ============================================================================
// Limb DLs
// ============================================================================

#define gLinkFierceDeityTorsoDL "__OTR__objects/object_link_boy/gLinkFierceDeityTorsoDL"
#define gLinkFierceDeityHeadDL "__OTR__objects/object_link_boy/gLinkFierceDeityHeadDL"
#define gLinkFierceDeityHatDL "__OTR__objects/object_link_boy/gLinkFierceDeityHatDL"
#define gLinkFierceDeityCollarDL "__OTR__objects/object_link_boy/gLinkFierceDeityCollarDL"
#define gLinkFierceDeityWaistDL "__OTR__objects/object_link_boy/gLinkFierceDeityWaistDL"
#define gLinkFierceDeityLeftUpperArmDL "__OTR__objects/object_link_boy/gLinkFierceDeityLeftUpperArmDL"
#define gLinkFierceDeityLeftForearmDL "__OTR__objects/object_link_boy/gLinkFierceDeityLeftForearmDL"
#define gLinkFierceDeityRightUpperArmDL "__OTR__objects/object_link_boy/gLinkFierceDeityRightUpperArmDL"
#define gLinkFierceDeityRightForearmDL "__OTR__objects/object_link_boy/gLinkFierceDeityRightForearmDL"
#define gLinkFierceDeityLeftThighDL "__OTR__objects/object_link_boy/gLinkFierceDeityLeftThighDL"
#define gLinkFierceDeityLeftShinDL "__OTR__objects/object_link_boy/gLinkFierceDeityLeftShinDL"
#define gLinkFierceDeityLeftFootDL "__OTR__objects/object_link_boy/gLinkFierceDeityLeftFootDL"
#define gLinkFierceDeityRightThighDL "__OTR__objects/object_link_boy/gLinkFierceDeityRightThighDL"
#define gLinkFierceDeityRightShinDL "__OTR__objects/object_link_boy/gLinkFierceDeityRightShinDL"
#define gLinkFierceDeityRightFootDL "__OTR__objects/object_link_boy/gLinkFierceDeityRightFootDL"

// ============================================================================
// Face Textures
// ============================================================================

#define gLinkFierceDeityEyesOpenTex "__OTR__objects/object_link_boy/gLinkFierceDeityEyesOpenTex"
#define gLinkFierceDeityEyesHalfTex "__OTR__objects/object_link_boy/gLinkFierceDeityEyesHalfTex"
#define gLinkFierceDeityEyesClosedTex "__OTR__objects/object_link_boy/gLinkFierceDeityEyesClosedTex"
#define gLinkFierceDeityMouthClosedTex "__OTR__objects/object_link_boy/gLinkFierceDeityMouthClosedTex"

// ============================================================================
// Limb Enum
// ============================================================================

typedef enum {
    LINK_FIERCE_DEITY_LIMB_NONE,
    LINK_FIERCE_DEITY_LIMB_ROOT,
    LINK_FIERCE_DEITY_LIMB_WAIST,
    LINK_FIERCE_DEITY_LIMB_LEFT_THIGH,
    LINK_FIERCE_DEITY_LIMB_LEFT_SHIN,
    LINK_FIERCE_DEITY_LIMB_LEFT_FOOT,
    LINK_FIERCE_DEITY_LIMB_RIGHT_THIGH,
    LINK_FIERCE_DEITY_LIMB_RIGHT_SHIN,
    LINK_FIERCE_DEITY_LIMB_RIGHT_FOOT,
    LINK_FIERCE_DEITY_LIMB_TORSO,
    LINK_FIERCE_DEITY_LIMB_LEFT_UPPER_ARM,
    LINK_FIERCE_DEITY_LIMB_LEFT_FOREARM,
    LINK_FIERCE_DEITY_LIMB_LEFT_HAND,
    LINK_FIERCE_DEITY_LIMB_RIGHT_UPPER_ARM,
    LINK_FIERCE_DEITY_LIMB_RIGHT_FOREARM,
    LINK_FIERCE_DEITY_LIMB_RIGHT_HAND,
    LINK_FIERCE_DEITY_LIMB_HEAD,
    LINK_FIERCE_DEITY_LIMB_HAT,
    LINK_FIERCE_DEITY_LIMB_COLLAR,
    LINK_FIERCE_DEITY_LIMB_MAX
} LinkFierceDeityLimb;

#endif // MM_OBJECT_LINK_BOY_H
