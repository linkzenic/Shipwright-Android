/**
 * mm_player_struct.h - MM Player Struct and Enums
 *
 * Copied from 2Ship/MM decomp z64player.h
 * All identifiers prefixed with Mm/MM_ to avoid conflicts with OOT
 */

#ifndef MM_PLAYER_STRUCT_H
#define MM_PLAYER_STRUCT_H

#include "z64.h"

// =============================================================================
// MM-SPECIFIC CONSTANTS
// =============================================================================

#define MM_PLAYER_LIMB_MAX 0x16     // 22
#define MM_PLAYER_BODYPART_MAX 0x12 // 18

// MM AnimationFrame is same layout as OOT, just different limb count
typedef struct MmPlayerAnimationFrame {
    Vec3s frameTable[MM_PLAYER_LIMB_MAX]; // 0x108 bytes
    s16 appearanceInfo;
} MmPlayerAnimationFrame; // size = 0x10A

#define MM_PLAYER_LIMB_BUF_SIZE (ALIGN16(sizeof(MmPlayerAnimationFrame)) + 0xF)

// =============================================================================
// MM PLAYER TRANSFORMATION ENUM
// =============================================================================

typedef enum MmPlayerTransformation {
    MM_PLAYER_FORM_FIERCE_DEITY = 0,
    MM_PLAYER_FORM_GORON = 1,
    MM_PLAYER_FORM_ZORA = 2,
    MM_PLAYER_FORM_DEKU = 3,
    MM_PLAYER_FORM_HUMAN = 4,
    MM_PLAYER_FORM_MAX = 5
} MmPlayerTransformation;

// =============================================================================
// MM STATE FLAGS 1 (u32)
// =============================================================================

#define MM_PLAYER_STATE1_1 (1 << 0)
#define MM_PLAYER_STATE1_2 (1 << 1)
#define MM_PLAYER_STATE1_4 (1 << 2)
#define MM_PLAYER_STATE1_8 (1 << 3)
#define MM_PLAYER_STATE1_10 (1 << 4)
#define MM_PLAYER_STATE1_20 (1 << 5)
#define MM_PLAYER_STATE1_TALKING (1 << 6)
#define MM_PLAYER_STATE1_DEAD (1 << 7)
#define MM_PLAYER_STATE1_100 (1 << 8)
#define MM_PLAYER_STATE1_200 (1 << 9)
#define MM_PLAYER_STATE1_400 (1 << 10)
#define MM_PLAYER_STATE1_CARRYING_ACTOR (1 << 11)
#define MM_PLAYER_STATE1_CHARGING_SPIN_ATTACK (1 << 12)
#define MM_PLAYER_STATE1_2000 (1 << 13)
#define MM_PLAYER_STATE1_4000 (1 << 14)
#define MM_PLAYER_STATE1_Z_TARGETING (1 << 15)
#define MM_PLAYER_STATE1_FRIENDLY_ACTOR_FOCUS (1 << 16)
#define MM_PLAYER_STATE1_PARALLEL (1 << 17)
#define MM_PLAYER_STATE1_40000 (1 << 18)
#define MM_PLAYER_STATE1_80000 (1 << 19)
#define MM_PLAYER_STATE1_100000 (1 << 20)
#define MM_PLAYER_STATE1_200000 (1 << 21)
#define MM_PLAYER_STATE1_400000 (1 << 22)
#define MM_PLAYER_STATE1_800000 (1 << 23)
#define MM_PLAYER_STATE1_USING_ZORA_BOOMERANG (1 << 24)
#define MM_PLAYER_STATE1_ZORA_BOOMERANG_THROWN (1 << 25)
#define MM_PLAYER_STATE1_4000000 (1 << 26)
#define MM_PLAYER_STATE1_8000000 (1 << 27)
#define MM_PLAYER_STATE1_10000000 (1 << 28)
#define MM_PLAYER_STATE1_20000000 (1 << 29)
#define MM_PLAYER_STATE1_LOCK_ON_FORCED_TO_RELEASE (1 << 30)
#define MM_PLAYER_STATE1_80000000 (1 << 31)

// =============================================================================
// MM STATE FLAGS 2 (u32)
// =============================================================================

#define MM_PLAYER_STATE2_1 (1 << 0)
#define MM_PLAYER_STATE2_CAN_ACCEPT_TALK_OFFER (1 << 1)
#define MM_PLAYER_STATE2_4 (1 << 2)
#define MM_PLAYER_STATE2_8 (1 << 3)
#define MM_PLAYER_STATE2_10 (1 << 4)
#define MM_PLAYER_STATE2_20 (1 << 5)
#define MM_PLAYER_STATE2_40 (1 << 6)
#define MM_PLAYER_STATE2_80 (1 << 7)
#define MM_PLAYER_STATE2_100 (1 << 8)
#define MM_PLAYER_STATE2_FORCE_SAND_FLOOR_SOUND (1 << 9)
#define MM_PLAYER_STATE2_400 (1 << 10)
#define MM_PLAYER_STATE2_800 (1 << 11)
#define MM_PLAYER_STATE2_1000 (1 << 12)
#define MM_PLAYER_STATE2_LOCK_ON_WITH_SWITCH (1 << 13)
#define MM_PLAYER_STATE2_4000 (1 << 14)
#define MM_PLAYER_STATE2_8000 (1 << 15)
#define MM_PLAYER_STATE2_10000 (1 << 16)
#define MM_PLAYER_STATE2_20000 (1 << 17)
#define MM_PLAYER_STATE2_40000 (1 << 18)
#define MM_PLAYER_STATE2_80000 (1 << 19)
#define MM_PLAYER_STATE2_100000 (1 << 20)
#define MM_PLAYER_STATE2_200000 (1 << 21)
#define MM_PLAYER_STATE2_400000 (1 << 22)
#define MM_PLAYER_STATE2_800000 (1 << 23)
#define MM_PLAYER_STATE2_1000000 (1 << 24)
#define MM_PLAYER_STATE2_2000000 (1 << 25)
#define MM_PLAYER_STATE2_4000000 (1 << 26)
#define MM_PLAYER_STATE2_USING_OCARINA (1 << 27)
#define MM_PLAYER_STATE2_IDLE_FIDGET (1 << 28)
#define MM_PLAYER_STATE2_20000000 (1 << 29)
#define MM_PLAYER_STATE2_40000000 (1 << 30)
#define MM_PLAYER_STATE2_80000000 (1 << 31)

// =============================================================================
// MM STATE FLAGS 3 (u32) - OOT only has u8 for stateFlags3!
// =============================================================================

#define MM_PLAYER_STATE3_1 (1 << 0)
#define MM_PLAYER_STATE3_2 (1 << 1)
#define MM_PLAYER_STATE3_4 (1 << 2)
#define MM_PLAYER_STATE3_8 (1 << 3)
#define MM_PLAYER_STATE3_10 (1 << 4)
#define MM_PLAYER_STATE3_20 (1 << 5)
#define MM_PLAYER_STATE3_40 (1 << 6)
#define MM_PLAYER_STATE3_FLYING_WITH_HOOKSHOT (1 << 7)
#define MM_PLAYER_STATE3_100 (1 << 8)
#define MM_PLAYER_STATE3_200 (1 << 9)
#define MM_PLAYER_STATE3_400 (1 << 10)
#define MM_PLAYER_STATE3_800 (1 << 11)
#define MM_PLAYER_STATE3_1000 (1 << 12) // Goron spike mode
#define MM_PLAYER_STATE3_2000 (1 << 13) // Deku flight
#define MM_PLAYER_STATE3_4000 (1 << 14)
#define MM_PLAYER_STATE3_8000 (1 << 15) // Zora fast swim boost
#define MM_PLAYER_STATE3_10000 (1 << 16)
#define MM_PLAYER_STATE3_20000 (1 << 17)
#define MM_PLAYER_STATE3_40000 (1 << 18)
#define MM_PLAYER_STATE3_80000 (1 << 19) // Goron roll active
#define MM_PLAYER_STATE3_100000 (1 << 20)
#define MM_PLAYER_STATE3_200000 (1 << 21)
#define MM_PLAYER_STATE3_400000 (1 << 22)
#define MM_PLAYER_STATE3_ZORA_BOOMERANG_CAUGHT (1 << 23)
#define MM_PLAYER_STATE3_1000000 (1 << 24)
#define MM_PLAYER_STATE3_2000000 (1 << 25)
#define MM_PLAYER_STATE3_4000000 (1 << 26)
#define MM_PLAYER_STATE3_8000000 (1 << 27)
#define MM_PLAYER_STATE3_10000000 (1 << 28)
#define MM_PLAYER_STATE3_20000000 (1 << 29)
#define MM_PLAYER_STATE3_START_CHANGING_HELD_ITEM (1 << 30)
#define MM_PLAYER_STATE3_HOSTILE_LOCK_ON (1 << 31)

// =============================================================================
// MM PLAYER ACTION FUNCTION TYPES
// =============================================================================

struct MmPlayer;
typedef void (*MmPlayerActionFunc)(struct MmPlayer* this, PlayState* play);
typedef void (*MmPlayerUpperActionFunc)(struct MmPlayer* this, PlayState* play);
typedef void (*MmAfterPutAwayFunc)(PlayState* play, struct MmPlayer* this);

// =============================================================================
// MM PLAYER AGE PROPERTIES (transformation properties)
// =============================================================================

typedef struct MmPlayerAgeProperties {
    /* 0x00 */ f32 ceilingCheckHeight;
    /* 0x04 */ f32 shadowScale;
    /* 0x08 */ f32 unk_08;
    /* 0x0C */ f32 unk_0C;
    /* 0x10 */ f32 unk_10;
    /* 0x14 */ f32 unk_14;
    /* 0x18 */ f32 unk_18;
    /* 0x1C */ f32 unk_1C;
    /* 0x20 */ f32 unk_20;
    /* 0x24 */ f32 unk_24;
    /* 0x28 */ f32 wallCheckRadius;
    /* 0x2C */ f32 unk_2C;
    /* 0x30 */ f32 unk_30;
    /* 0x34 */ f32 unk_34;
    /* 0x38 */ Vec3s jointTableInterp[4];
    /* 0x50 */ u16 unk_50;
    /* 0x54 */ f32 unk_54;
    /* 0x58 */ f32 unk_58;
    /* 0x5C */ f32 unk_5C;
    /* 0x60 */ f32 unk_60;
    /* 0x64 */ f32 unk_64;
    /* 0x68 */ f32 unk_68;
    /* 0x6C */ f32 unk_6C;
    /* 0x70 */ f32 unk_70;
    /* 0x74 */ f32 unk_74;
    /* 0x78 */ f32 unk_78;
    /* 0x7C */ f32 unk_7C;
    /* 0x80 */ f32 unk_80;
    /* 0x84 */ u32 voiceSfxIdOffset;
    /* 0x88 */ u16 surfaceSfxIdOffset;
    /* 0x8A */ u16 unk_8A;
    /* 0x8C */ void* unk_8C;    // AnimationHeader*
    /* 0x90 */ void* unk_90;    // AnimationHeader*
    /* 0x94 */ void* unk_94;    // AnimationHeader*
    /* 0x98 */ void* unk_98;    // AnimationHeader*
    /* 0x9C */ void* unk_9C;    // AnimationHeader*
    /* 0xA0 */ void* unk_A0;    // AnimationHeader*
    /* 0xA4 */ void* unk_A4;    // AnimationHeader*
    /* 0xA8 */ void* unk_A8;    // AnimationHeader*
    /* 0xAC */ void* unk_AC;    // AnimationHeader*
    /* 0xB0 */ void* unk_B0;    // PlayerAnimationHeader*
    /* 0xB4 */ void* unk_B4[4]; // PlayerAnimationHeader*
    /* 0xC4 */ void* unk_C4[2]; // PlayerAnimationHeader*
    /* 0xCC */ void* unk_CC[2]; // PlayerAnimationHeader*
    /* 0xD4 */ void* unk_D4[2]; // PlayerAnimationHeader*
} MmPlayerAgeProperties;        // size = 0xDC

// =============================================================================
// MM WEAPON INFO
// =============================================================================

typedef struct MmWeaponInfo {
    s32 active;
    Vec3f tip;
    Vec3f base;
} MmWeaponInfo;

// =============================================================================
// MM PLAYER STRUCT - EXACT COPY FROM MM Z64PLAYER.H
// Size: 0xD78 bytes
// =============================================================================

typedef struct MmPlayer {
    /* 0x000 */ Actor actor;
    /* 0x144 */ s8 currentShield;
    /* 0x145 */ s8 currentBoots;
    /* 0x146 */ s8 heldItemButton;
    /* 0x147 */ s8 heldItemAction;
    /* 0x148 */ u8 heldItemId;
    /* 0x149 */ s8 prevBoots;
    /* 0x14A */ s8 itemAction;
    /* 0x14B */ u8 transformation; // MmPlayerTransformation
    /* 0x14C */ u8 modelGroup;
    /* 0x14D */ u8 nextModelGroup;
    /* 0x14E */ s8 itemChangeType;
    /* 0x14F */ u8 modelAnimType;
    /* 0x150 */ u8 leftHandType;
    /* 0x151 */ u8 rightHandType;
    /* 0x152 */ u8 sheathType;
    /* 0x153 */ u8 currentMask;
    /* 0x154 */ s8 unk_154;
    /* 0x155 */ u8 prevMask;
    /* 0x156 */ u8 pad_156[2];
    /* 0x158 */ Gfx** rightHandDLists;
    /* 0x15C */ Gfx** leftHandDLists;
    /* 0x160 */ Gfx** sheathDLists;
    /* 0x164 */ Gfx** waistDLists;
    /* 0x168 */ u8 unk_168[0x4C];
    /* 0x1B4 */ s16 unk_1B4;
    /* 0x1B6 */ u8 unk_1B6[0x2];
    /* 0x1B8 */ u8 giObjectLoading;
    /* 0x1B9 */ u8 pad_1B9[3];
    /* 0x1BC */ DmaRequest giObjectDmaRequest;
    /* 0x1DC */ OSMesgQueue giObjectLoadQueue;
    /* 0x1F4 */ OSMesg giObjectLoadMsg;
    /* 0x1F8 */ void* giObjectSegment;
    /* 0x1FC */ u8 maskObjectLoadState;
    /* 0x1FD */ s8 maskId;
    /* 0x1FE */ u8 pad_1FE[2];
    /* 0x200 */ DmaRequest maskDmaRequest;
    /* 0x220 */ OSMesgQueue maskObjectLoadQueue;
    /* 0x238 */ OSMesg maskObjectLoadMsg;
    /* 0x23C */ void* maskObjectSegment;
    /* 0x240 */ SkelAnime skelAnime;
    /* 0x284 */ SkelAnime skelAnimeUpper;
    /* 0x2C8 */ SkelAnime unk_2C8;
    /* 0x30C */ Vec3s jointTable[5];
    /* 0x32A */ Vec3s morphTable[5];
    /* 0x348 */ u8 faceChange[4]; // Simplified from FaceChange
    /* 0x34C */ Actor* heldActor;
    /* 0x350 */ PosRot leftHandWorld;
    /* 0x364 */ Actor* rightHandActor;
    /* 0x368 */ PosRot rightHandWorld;
    /* 0x37C */ s8 doorType;
    /* 0x37D */ s8 doorDirection;
    /* 0x37E */ s8 doorTimer;
    /* 0x37F */ s8 doorNext;
    /* 0x380 */ Actor* doorActor;
    /* 0x384 */ s16 getItemId;
    /* 0x386 */ u16 getItemDirection;
    /* 0x388 */ Actor* interactRangeActor;
    /* 0x38C */ s8 mountSide;
    /* 0x38D */ u8 pad_38D[3];
    /* 0x390 */ Actor* rideActor;
    /* 0x394 */ u8 csAction;
    /* 0x395 */ u8 prevCsAction;
    /* 0x396 */ u8 cueId;
    /* 0x397 */ u8 unk_397;
    /* 0x398 */ Actor* csActor;
    /* 0x39C */ u8 unk_39C[0x4];
    /* 0x3A0 */ Vec3f unk_3A0;
    /* 0x3AC */ Vec3f unk_3AC;
    /* 0x3B8 */ u16 unk_3B8;
    /* 0x3BA */ union {
        s16 haltActorsDuringCsAction;
        s16 doorBgCamIndex;
    } cv;
    /* 0x3BC */ s16 subCamId;
    /* 0x3BE */ u8 pad_3BE[2];
    /* 0x3C0 */ Vec3f unk_3C0;
    /* 0x3CC */ s16 unk_3CC;
    /* 0x3CE */ s8 unk_3CE;
    /* 0x3CF */ u8 unk_3CF;
    /* 0x3D0 */ u8 unk_3D0[0x114]; // struct_80122D44_arg1 - large struct, opaque for now
    /* 0x4E4 */ u8 unk_4E4[0x20];
    /* 0x504 */ LightNode* lightNode;
    /* 0x508 */ LightInfo lightInfo;
    /* 0x518 */ ColliderCylinder cylinder;
    /* 0x564 */ ColliderQuad meleeWeaponQuads[2];
    /* 0x664 */ ColliderQuad shieldQuad;
    /* 0x6E4 */ ColliderCylinder shieldCylinder;
    /* 0x730 */ Actor* focusActor;
    /* 0x734 */ u8 unk_734[0x4];
    /* 0x738 */ s32 zTargetActiveTimer;
    /* 0x73C */ s32 meleeWeaponEffectIndex[3];
    /* 0x748 */ MmPlayerActionFunc actionFunc;
    /* 0x74C */ u8 jointTableBuffer[MM_PLAYER_LIMB_BUF_SIZE];
    /* 0x7EB */ u8 morphTableBuffer[MM_PLAYER_LIMB_BUF_SIZE];
    /* 0x88A */ u8 blendTableBuffer[MM_PLAYER_LIMB_BUF_SIZE];
    /* 0x929 */ u8 jointTableUpperBuffer[MM_PLAYER_LIMB_BUF_SIZE];
    /* 0x9C8 */ u8 morphTableUpperBuffer[MM_PLAYER_LIMB_BUF_SIZE];
    /* 0xA67 */ u8 pad_A67;
    /* 0xA68 */ MmPlayerAgeProperties* ageProperties;
    /* 0xA6C */ u32 stateFlags1;
    /* 0xA70 */ u32 stateFlags2;
    /* 0xA74 */ u32 stateFlags3; // MM uses u32, OOT uses u8!
    /* 0xA78 */ Actor* autoLockOnActor;
    /* 0xA7C */ Actor* zoraBoomerangActor;
    /* 0xA80 */ Actor* tatlActor;
    /* 0xA84 */ s16 tatlTextId;
    /* 0xA86 */ s8 csId;
    /* 0xA87 */ s8 exchangeItemAction;
    /* 0xA88 */ Actor* talkActor;
    /* 0xA8C */ f32 talkActorDistance;
    /* 0xA90 */ Actor* ocarinaInteractionActor;
    /* 0xA94 */ f32 ocarinaInteractionDistance;
    /* 0xA98 */ u8 unk_A98[0x4];
    /* 0xA9C */ f32 secretRumbleCharge;
    /* 0xAA0 */ f32 closestSecretDistSq;
    /* 0xAA4 */ s8 idleType;
    /* 0xAA5 */ u8 unk_AA5;
    /* 0xAA6 */ u16 unk_AA6_rotFlags;
    /* 0xAA8 */ s16 upperLimbYawSecondary;
    /* 0xAAA */ s16 unk_AAA;
    /* 0xAAC */ Vec3s headLimbRot;
    /* 0xAB2 */ Vec3s upperLimbRot;
    /* 0xAB8 */ f32 unk_AB8;
    /* 0xABC */ f32 unk_ABC;
    /* 0xAC0 */ f32 unk_AC0;
    /* 0xAC4 */ MmPlayerUpperActionFunc upperActionFunc;
    /* 0xAC8 */ f32 skelAnimeUpperBlendWeight;
    /* 0xACC */ s16 unk_ACC;
    /* 0xACE */ s8 unk_ACE;
    /* 0xACF */ u8 putAwayCooldownTimer;
    /* 0xAD0 */ f32 speedXZ; // MM: speedXZ, OOT: linearVelocity
    /* 0xAD4 */ s16 yaw;
    /* 0xAD6 */ s16 parallelYaw;
    /* 0xAD8 */ u16 underwaterTimer;
    /* 0xADA */ s8 meleeWeaponAnimation;
    /* 0xADB */ s8 meleeWeaponState;
    /* 0xADC */ s8 unk_ADC;
    /* 0xADD */ s8 unk_ADD;
    /* 0xADE */ u8 controlStickDataIndex;
    /* 0xADF */ s8 controlStickSpinAngles[4];
    /* 0xAE3 */ s8 controlStickDirections[4];
    /* 0xAE7 */ union {
        s8 actionVar1;
        s8 startedAnim;
        s8 facingUpSlope;
    } av1;
    /* 0xAE8 */ union {
        s16 actionVar2;
        s16 fallDamageStunTimer;
        s16 animDelayTimer;
        s16 csDelayTimer;
        s16 playedLandingSfx;
    } av2;
    /* 0xAEA */ u8 pad_AEA[2];
    /* 0xAEC */ f32 unk_AEC;
    /* 0xAF0 */ union {
        Vec3f unk_AF0[2];
        f32 arr_AF0[6];
    };
    /* 0xB08 */ f32 unk_B08; // Goron roll speed
    /* 0xB0C */ f32 unk_B0C; // Goron accumulated roll distance
    /* 0xB10 */ f32 unk_B10[6];
    /* 0xB28 */ s16 unk_B28;
    /* 0xB2A */ s8 getItemDrawIdPlusOne;
    /* 0xB2B */ s8 unk_B2B;
    /* 0xB2C */ f32 windSpeed;
    /* 0xB30 */ s16 windAngleX;
    /* 0xB32 */ s16 windAngleY;
    /* 0xB34 */ f32 unk_B34;
    /* 0xB38 */ f32 unk_B38;
    /* 0xB3C */ f32 unk_B3C;
    /* 0xB40 */ f32 unk_B40;
    /* 0xB44 */ f32 unk_B44;
    /* 0xB48 */ f32 unk_B48; // Vertical velocity for slam
    /* 0xB4C */ s16 unk_B4C;
    /* 0xB4E */ s16 turnRate;
    /* 0xB50 */ f32 unk_B50;
    /* 0xB54 */ f32 yDistToLedge;
    /* 0xB58 */ f32 distToInteractWall;
    /* 0xB5C */ u8 ledgeClimbType;
    /* 0xB5D */ u8 ledgeClimbDelayTimer;
    /* 0xB5E */ u8 textboxBtnCooldownTimer;
    /* 0xB5F */ u8 unk_B5F;
    /* 0xB60 */ u16 blastMaskTimer;
    /* 0xB62 */ s16 unk_B62;
    /* 0xB64 */ u8 unk_B64;
    /* 0xB65 */ u8 bodyShockTimer;
    /* 0xB66 */ u8 unk_B66;
    /* 0xB67 */ u8 remainingHopsCounter; // Deku water hop count
    /* 0xB68 */ s16 fallStartHeight;
    /* 0xB6A */ s16 fallDistance;
    /* 0xB6C */ s16 floorPitch;
    /* 0xB6E */ s16 floorPitchAlt;
    /* 0xB70 */ s16 unk_B70;
    /* 0xB72 */ u16 floorSfxOffset;
    /* 0xB74 */ u8 unk_B74;
    /* 0xB75 */ u8 unk_B75;
    /* 0xB76 */ s16 unk_B76;
    /* 0xB78 */ f32 unk_B78;
    /* 0xB7C */ f32 unk_B7C;
    /* 0xB80 */ f32 pushedSpeed;
    /* 0xB84 */ s16 pushedYaw;
    /* 0xB86 */ s16 unk_B86[2]; // Goron spike mode counters
    /* 0xB8A */ s16 unk_B8A;    // Goron specific
    /* 0xB8C */ s16 unk_B8C;    // Wall reflection cooldown
    /* 0xB8E */ s16 unk_B8E;
    /* 0xB90 */ s16 unk_B90;
    /* 0xB92 */ s16 unk_B92;
    /* 0xB94 */ s16 unk_B94;
    /* 0xB96 */ s16 unk_B96;
    /* 0xB98 */ MmWeaponInfo meleeWeaponInfo[3];
    /* 0xBEC */ Vec3f bodyPartsPos[MM_PLAYER_BODYPART_MAX];
    /* 0xCC4 */ MtxF leftHandMf;
    /* 0xD04 */ MtxF shieldMf;
    /* 0xD44 */ u8 bodyIsBurning;
    /* 0xD45 */ u8 bodyFlameTimers[MM_PLAYER_BODYPART_MAX];
    /* 0xD57 */ u8 unk_D57;
    /* 0xD58 */ MmAfterPutAwayFunc afterPutAwayFunc;
    /* 0xD5C */ s8 invincibilityTimer;
    /* 0xD5D */ u8 floorTypeTimer;
    /* 0xD5E */ u8 floorProperty;
    /* 0xD5F */ u8 prevFloorType;
    /* 0xD60 */ f32 prevControlStickMagnitude;
    /* 0xD64 */ s16 prevControlStickAngle;
    /* 0xD66 */ u16 prevFloorSfxOffset;
    /* 0xD68 */ s16 unk_D68;
    /* 0xD6A */ s8 unk_D6A;
    /* 0xD6B */ u8 unk_D6B;
    /* 0xD6C */ Vec3f unk_D6C;
} MmPlayer; // size = 0xD78

// =============================================================================
// GLOBAL MMPLAYER INSTANCE
// =============================================================================

extern MmPlayer gMmPlayer;

#endif // MM_PLAYER_STRUCT_H
