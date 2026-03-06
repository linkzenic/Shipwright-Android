/**
 * @file object_link_zora.h
 * @brief MM Zora form assets - skeleton, DLs, textures
 *
 * OTR paths for mm.o2r. Use directly with gSPDisplayList or MmAssets_LoadResource.
 */

#ifndef MM_OBJECT_LINK_ZORA_H
#define MM_OBJECT_LINK_ZORA_H

// ============================================================================
// Skeleton
// ============================================================================

#define gLinkZoraSkel "__OTR__objects/object_link_zora/gLinkZoraSkel"

// ============================================================================
// Hand DLs
// ============================================================================

#define gLinkZoraLeftHandOpenDL "__OTR__objects/object_link_zora/gLinkZoraLeftHandOpenDL"
#define gLinkZoraLeftHandClosedDL "__OTR__objects/object_link_zora/gLinkZoraLeftHandClosedDL"
#define gLinkZoraLeftHandHoldBottleDL "__OTR__objects/object_link_zora/gLinkZoraLeftHandHoldBottleDL"
#define gLinkZoraRightHandOpenDL "__OTR__objects/object_link_zora/gLinkZoraRightHandOpenDL"
#define gLinkZoraRightHandClosedDL "__OTR__objects/object_link_zora/gLinkZoraRightHandClosedDL"

// ============================================================================
// Limb DLs
// ============================================================================

#define gLinkZoraTorsoDL "__OTR__objects/object_link_zora/gLinkZoraTorsoDL"
#define gLinkZoraHeadDL "__OTR__objects/object_link_zora/gLinkZoraHeadDL"
#define gLinkZoraHatDL "__OTR__objects/object_link_zora/gLinkZoraHatDL"
#define gLinkZoraCollarDL "__OTR__objects/object_link_zora/gLinkZoraCollarDL"
#define gLinkZoraWaistDL "__OTR__objects/object_link_zora/gLinkZoraWaistDL"
#define gLinkZoraLeftUpperArmDL "__OTR__objects/object_link_zora/gLinkZoraLeftUpperArmDL"
#define gLinkZoraLeftForearmDL "__OTR__objects/object_link_zora/gLinkZoraLeftForearmDL"
#define gLinkZoraRightUpperArmDL "__OTR__objects/object_link_zora/gLinkZoraRightUpperArmDL"
#define gLinkZoraRightForearmDL "__OTR__objects/object_link_zora/gLinkZoraRightForearmDL"
#define gLinkZoraLeftThighDL "__OTR__objects/object_link_zora/gLinkZoraLeftThighDL"
#define gLinkZoraLeftShinDL "__OTR__objects/object_link_zora/gLinkZoraLeftShinDL"
#define gLinkZoraLeftFootDL "__OTR__objects/object_link_zora/gLinkZoraLeftFootDL"
#define gLinkZoraRightThighDL "__OTR__objects/object_link_zora/gLinkZoraRightThighDL"
#define gLinkZoraRightShinDL "__OTR__objects/object_link_zora/gLinkZoraRightShinDL"
#define gLinkZoraRightFootDL "__OTR__objects/object_link_zora/gLinkZoraRightFootDL"

// ============================================================================
// Eye Textures
// ============================================================================

#define gLinkZoraEyesOpenTex "__OTR__objects/object_link_zora/gLinkZoraEyesOpenTex"
#define gLinkZoraEyesHalfTex "__OTR__objects/object_link_zora/gLinkZoraEyesHalfTex"
#define gLinkZoraEyesClosedTex "__OTR__objects/object_link_zora/gLinkZoraEyesClosedTex"
#define gLinkZoraEyesRightTex "__OTR__objects/object_link_zora/gLinkZoraEyesRightTex"
#define gLinkZoraEyesLeftTex "__OTR__objects/object_link_zora/gLinkZoraEyesLeftTex"
#define gLinkZoraEyesUpTex "__OTR__objects/object_link_zora/gLinkZoraEyesUpTex"
#define gLinkZoraEyesDownTex "__OTR__objects/object_link_zora/gLinkZoraEyesDownTex"
#define gLinkZoraEyesWincingTex "__OTR__objects/object_link_zora/gLinkZoraEyesWincingTex"

// ============================================================================
// Mouth Textures
// ============================================================================

#define gLinkZoraMouthClosedTex "__OTR__objects/object_link_zora/gLinkZoraMouthClosedTex"
#define gLinkZoraMouthHalfTex "__OTR__objects/object_link_zora/gLinkZoraMouthHalfTex"
#define gLinkZoraMouthOpenTex "__OTR__objects/object_link_zora/gLinkZoraMouthOpenTex"
#define gLinkZoraMouthSmileTex "__OTR__objects/object_link_zora/gLinkZoraMouthSmileTex"

// ============================================================================
// Limb Enum
// ============================================================================

typedef enum {
    LINK_ZORA_LIMB_NONE,
    LINK_ZORA_LIMB_ROOT,
    LINK_ZORA_LIMB_WAIST,
    LINK_ZORA_LIMB_LEFT_THIGH,
    LINK_ZORA_LIMB_LEFT_SHIN,
    LINK_ZORA_LIMB_LEFT_FOOT,
    LINK_ZORA_LIMB_RIGHT_THIGH,
    LINK_ZORA_LIMB_RIGHT_SHIN,
    LINK_ZORA_LIMB_RIGHT_FOOT,
    LINK_ZORA_LIMB_TORSO,
    LINK_ZORA_LIMB_LEFT_UPPER_ARM,
    LINK_ZORA_LIMB_LEFT_FOREARM,
    LINK_ZORA_LIMB_LEFT_HAND,
    LINK_ZORA_LIMB_RIGHT_UPPER_ARM,
    LINK_ZORA_LIMB_RIGHT_FOREARM,
    LINK_ZORA_LIMB_RIGHT_HAND,
    LINK_ZORA_LIMB_HEAD,
    LINK_ZORA_LIMB_HAT,
    LINK_ZORA_LIMB_COLLAR,
    LINK_ZORA_LIMB_MAX
} LinkZoraLimb;

// ============================================================================
// Forearm Fin/Shield DLs (from 2Ship func_80126BD0, z_player_lib.c:3001)
// Zora's fin blades extend from forearms during guard, swimming, and attacks.
// Drawn at PLAYER_LIMB_LEFT_FOREARM and PLAYER_LIMB_RIGHT_FOREARM in PostLimbDraw.
// ============================================================================

#define gLinkZoraLeftForearmShieldDL "__OTR__objects/object_link_zora/object_link_zora_DL_00CC38"
#define gLinkZoraRightForearmShieldDL "__OTR__objects/object_link_zora/object_link_zora_DL_00CDA0"

// ============================================================================
// Guitar DLs (from z_player_lib.c L_HAND override during gakki animations)
// ============================================================================

#define gLinkZoraGuitarDL "__OTR__objects/object_link_zora/object_link_zora_DL_00E2A0"
#define gLinkZoraGuitarHandDL "__OTR__objects/object_link_zora/object_link_zora_DL_00E088"

// ============================================================================
// Electric Barrier Assets (from 2Ship object_link_zora offsets 0x11210-0x12A80)
// Used by Player_DrawZoraShield / func_8082F1AC
// ============================================================================

// Pure #define format (matches object_link_goron.h style, no ALIGN_ASSET dependency)
#define gLinkZoraBarrierDL "__OTR__objects/object_link_zora/object_link_zora_DL_011760"
#define gLinkZoraBarrierVtx "__OTR__objects/object_link_zora/object_link_zora_Vtx_011210"
#define gLinkZoraBarrierAlpha "__OTR__objects/object_link_zora/object_link_zora_U8_011710"
#define gLinkZoraBarrierMatAnim "__OTR__objects/object_link_zora/object_link_zora_Matanimheader_012A80"

#endif // MM_OBJECT_LINK_ZORA_H
