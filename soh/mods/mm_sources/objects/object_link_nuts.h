/**
 * @file object_link_nuts.h
 * @brief MM Deku form assets - skeleton, DLs, textures
 *
 * OTR paths for mm.o2r. Use directly with gSPDisplayList or MmAssets_LoadResource.
 */

#ifndef MM_OBJECT_LINK_NUTS_H
#define MM_OBJECT_LINK_NUTS_H

// ============================================================================
// Skeleton
// ============================================================================

#define gLinkDekuSkel "__OTR__objects/object_link_nuts/gLinkDekuSkel"

// ============================================================================
// Flower DLs
// ============================================================================

#define gLinkDekuClosedFlowerDL "__OTR__objects/object_link_nuts/gLinkDekuClosedFlowerDL"
#define gLinkDekuOpenFlowerDL "__OTR__objects/object_link_nuts/gLinkDekuOpenFlowerDL"

// ============================================================================
// Hand DLs
// ============================================================================

#define gLinkDekuLeftHandDL "__OTR__objects/object_link_nuts/gLinkDekuLeftHandDL"
#define gLinkDekuRightHandDL "__OTR__objects/object_link_nuts/gLinkDekuRightHandDL"

// ============================================================================
// Limb DLs
// ============================================================================

#define gLinkDekuTorsoDL "__OTR__objects/object_link_nuts/gLinkDekuTorsoDL"
#define gLinkDekuHeadDL "__OTR__objects/object_link_nuts/gLinkDekuHeadDL"
#define gLinkDekuHatDL "__OTR__objects/object_link_nuts/gLinkDekuHatDL"
#define gLinkDekuCollarDL "__OTR__objects/object_link_nuts/gLinkDekuCollarDL"
#define gLinkDekuWaistDL "__OTR__objects/object_link_nuts/gLinkDekuWaistDL"
#define gLinkDekuLeftUpperArmDL "__OTR__objects/object_link_nuts/gLinkDekuLeftUpperArmDL"
#define gLinkDekuLeftForearmDL "__OTR__objects/object_link_nuts/gLinkDekuLeftForearmDL"
#define gLinkDekuRightUpperArmDL "__OTR__objects/object_link_nuts/gLinkDekuRightUpperArmDL"
#define gLinkDekuRightForearmDL "__OTR__objects/object_link_nuts/gLinkDekuRightForearmDL"
#define gLinkDekuLeftThighDL "__OTR__objects/object_link_nuts/gLinkDekuLeftThighDL"
#define gLinkDekuLeftShinDL "__OTR__objects/object_link_nuts/gLinkDekuLeftShinDL"
#define gLinkDekuLeftFootDL "__OTR__objects/object_link_nuts/gLinkDekuLeftFootDL"
#define gLinkDekuRightThighDL "__OTR__objects/object_link_nuts/gLinkDekuRightThighDL"
#define gLinkDekuRightShinDL "__OTR__objects/object_link_nuts/gLinkDekuRightShinDL"
#define gLinkDekuRightFootDL "__OTR__objects/object_link_nuts/gLinkDekuRightFootDL"

// ============================================================================
// Limb Enum
// ============================================================================

typedef enum {
    LINK_DEKU_LIMB_NONE,
    LINK_DEKU_LIMB_ROOT,
    LINK_DEKU_LIMB_WAIST,
    LINK_DEKU_LIMB_LEFT_THIGH,
    LINK_DEKU_LIMB_LEFT_SHIN,
    LINK_DEKU_LIMB_LEFT_FOOT,
    LINK_DEKU_LIMB_RIGHT_THIGH,
    LINK_DEKU_LIMB_RIGHT_SHIN,
    LINK_DEKU_LIMB_RIGHT_FOOT,
    LINK_DEKU_LIMB_TORSO,
    LINK_DEKU_LIMB_LEFT_UPPER_ARM,
    LINK_DEKU_LIMB_LEFT_FOREARM,
    LINK_DEKU_LIMB_LEFT_HAND,
    LINK_DEKU_LIMB_RIGHT_UPPER_ARM,
    LINK_DEKU_LIMB_RIGHT_FOREARM,
    LINK_DEKU_LIMB_RIGHT_HAND,
    LINK_DEKU_LIMB_HEAD,
    LINK_DEKU_LIMB_HAT,
    LINK_DEKU_LIMB_COLLAR,
    LINK_DEKU_LIMB_MAX
} LinkDekuLimb;

#endif // MM_OBJECT_LINK_NUTS_H
