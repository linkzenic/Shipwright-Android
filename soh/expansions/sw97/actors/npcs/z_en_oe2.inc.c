/**
 * z_en_oe2.c - Kokiri / Goron NPC actor for SW97
 *
 * Original: z64proto/sw97 team (Spaceworld '97 Experience)
 * Adapted for Ship of Harkinian (Shipwright)
 *
 * Init variable:
 *  0mmmmbbbtttttttt
 *
 *  mmmm        Object index (0-11)
 *  bbb         Text bank (0-7, maps to 0x10xx-0x70xx,0x71xx)
 *  tttttttt    Text ID
 *
 * TODO: change variable text stuff for more possible text IDs!
 */
#include "expansions/sw97/sw97_compat.h"
#include "expansions/sw97/sw97_config.h"

#include "vt.h"

#define FLAGS 0x02000019

#define THIS ((EnOE2*)thisx)

// ---------------------------------------------------------------------------
// Struct (merged from z_en_oe2.h)
// ---------------------------------------------------------------------------

struct EnOE2;

typedef void (*EnOE2InitFunc)(struct EnOE2*, PlayState*);
typedef void (*EnOE2UpdateFunc)(struct EnOE2*, PlayState*);
typedef void (*EnOE2DrawFunc)(struct EnOE2*, PlayState*);

typedef enum {
    OE2_INIT_MIDO_A,
    OE2_INIT_REDHEAD_GIRL,
    OE2_INIT_SARIA,
    OE2_INIT_REDHEAD_BOY_A,
    OE2_INIT_FADO_A,
    OE2_INIT_GIRL_BROWN_BAND,
    OE2_INIT_BOY_GREEN_CAP,
    OE2_INIT_GIRL_GREEN_BAND,
    OE2_INIT_BOY_MINT_CAP,
    OE2_INIT_SHOP_VENDOR,
    OE2_INIT_GIRL_GREEN_CAP,
    OE2_INIT_FADO_B,
    OE2_INIT_GORON
} EnOE2InitMode;

typedef enum {
    OE2_UPDATE_MIDO_A,
    OE2_UPDATE_REDHEAD_GIRL,
    OE2_UPDATE_SARIA,
    OE2_UPDATE_REDHEAD_BOY_A,
    OE2_UPDATE_FADO_A,
    OE2_UPDATE_GIRL_BROWN_BAND,
    OE2_UPDATE_BOY_GREEN_CAP,
    OE2_UPDATE_GIRL_GREEN_BAND,
    OE2_UPDATE_BOY_MINT_CAP,
    OE2_UPDATE_SHOP_VENDOR,
    OE2_UPDATE_GIRL_GREEN_CAP,
    OE2_UPDATE_FADO_B,
    OE2_UPDATE_GORON
} EnOE2UpdateMode;

typedef enum {
    OE2_DRAW_MIDO_A,
    OE2_DRAW_REDHEAD_GIRL,
    OE2_DRAW_SARIA,
    OE2_DRAW_REDHEAD_BOY_A,
    OE2_DRAW_FADO_A,
    OE2_DRAW_GIRL_BROWN_BAND,
    OE2_DRAW_BOY_GREEN_CAP,
    OE2_DRAW_GIRL_GREEN_BAND,
    OE2_DRAW_BOY_MINT_CAP,
    OE2_DRAW_SHOP_VENDOR,
    OE2_DRAW_GIRL_GREEN_CAP,
    OE2_DRAW_FADO_B,
    OE2_DRAW_GORON
} EnOE2DrawMode;

typedef enum { OE2_DRAWFLAG_DEBUG = 0x00000001 } EnOE2DrawFlags;

typedef struct EnOE2 {
    Actor actor;
    SkelAnime skelAnime;
    Vec3s jointTable[39];
    Vec3s morphTable[39];
    s8 npcType;
    s16 priObjectId;
    s16 secObjectId;
    s16 animObjectId;
    s32 priDrawObjBankIndex;
    s32 secDrawObjBankIndex;
    s32 animObjBankIndex;
    s16 eyeBlinkTimer;
    s16 eyeTextureIndex;
    void* eyeTexture;
    s16 mouthTextureIndex;
    void* mouthTexture;
    s32 drawFlags;
    s32 updateMode;
    s32 drawMode;
    ColliderCylinder collider;
    Vec3s unknownVector1;
    Vec3s unknownVector2;
    NpcInfo npcInfo;
    s16 goronState;
    void* goronEyeTexture;
} EnOE2;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

void EnOE2_Init(Actor* thisx, PlayState* play);
void EnOE2_Destroy(Actor* thisx, PlayState* play);
void EnOE2_Update(Actor* thisx, PlayState* play);
void EnOE2_Draw(Actor* thisx, PlayState* play);

void EnOE2_InitCommon(EnOE2* this, PlayState* play);

// ---------------------------------------------------------------------------
// External actor ID (assigned at runtime by SW97 system)
// ---------------------------------------------------------------------------

extern s16 gSw97ActorId_EnOE2;

// ---------------------------------------------------------------------------
// Collider
// ---------------------------------------------------------------------------

static ColliderCylinderInit sOe2CylinderInit = {
    {
        COLTYPE_NONE,
        AT_NONE,
        AC_NONE,
        OC1_ON | OC1_TYPE_ALL,
        OC2_TYPE_2,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK0,
        { 0x00000000, 0x00, 0x00 },
        { 0x00000000, 0x00, 0x00 },
        TOUCH_NONE,
        BUMP_ON,
        OCELEM_ON,
    },
    { 15, 15, 0, { 0, 0, 0 } },
};

// ---------------------------------------------------------------------------
// Object tables
// ---------------------------------------------------------------------------

static s16 sPrimaryDrawObjects[] = {
    OBJECT_OE1, OBJECT_OE2, OBJECT_OE3, OBJECT_OE4, OBJECT_OE5, OBJECT_OE2,      OBJECT_OE1,
    OBJECT_OE2, OBJECT_OE1, OBJECT_OE1, OBJECT_OE2, OBJECT_OE2, OBJECT_OF1D_MAP,
};

static s16 sSecondaryDrawObjects[] = {
    -1,         -1,         OBJECT_OE2,  OBJECT_OE2,  OBJECT_OE2,  OBJECT_OE6, OBJECT_OE7,
    OBJECT_OE8, OBJECT_OE9, OBJECT_OE10, OBJECT_OE11, OBJECT_OE12, -1,
};

static s16 sAnimationObjects[] = {
    OBJECT_OE_ANIME, OBJECT_OE_ANIME, OBJECT_OE_ANIME, OBJECT_OE_ANIME, OBJECT_OE_ANIME,
    OBJECT_OE_ANIME, OBJECT_OE_ANIME, OBJECT_OE_ANIME, OBJECT_OE_ANIME, OBJECT_OE_ANIME,
    OBJECT_OE_ANIME, OBJECT_OE_ANIME, OBJECT_OE_ANIME,
};

// ---------------------------------------------------------------------------
// Extern data from objects
// ---------------------------------------------------------------------------

// Stub declarations for SW97-specific object data (D_0600XXXX segment symbols).
// These would normally be resolved from sw97.otr objects at runtime.
// Without the OTR, we provide zero-initialized stubs so the code links.
// The NPCs will not display correctly but won't crash (Init kills actor if object missing).

// skeletons
static SkeletonHeader D_06000260 = { 0 }; // oE1-oE5
static SkeletonHeader D_0600CC80 = { 0 }; // Goron (oF1)

// animations (object_oE_anime)
static AnimationHeader D_06001034 = { 0 }; // Kokiri greeting
static AnimationHeader D_060019BC = { 0 }; // Kokiri standing
static AnimationHeader D_0600242C = { 0 }; // Kokiri tiptoe
static AnimationHeader D_06002DC8 = { 0 }; // Misc 2-frame loops
static AnimationHeader D_060047D4 = { 0 }; // Goron wakeup?
static AnimationHeader D_06005044 = { 0 }; // Goron standing?

// display lists (single gSPEndDisplayList entry so they're valid but empty)
static Gfx D_06000AE0[] = { gsSPEndDisplayList() }; // oE6 head
static Gfx D_060006B0[] = { gsSPEndDisplayList() }; // oE7 head
static Gfx D_06000CA0[] = { gsSPEndDisplayList() }; // oE8 head
static Gfx D_06000800[] = { gsSPEndDisplayList() }; // oE9 head
static Gfx D_06000720[] = { gsSPEndDisplayList() }; // oE10 head
static Gfx D_060009F0[] = { gsSPEndDisplayList() }; // oE11 head
static Gfx D_06001020[] = { gsSPEndDisplayList() }; // oE12 head

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

void EnOE2_Init(Actor* thisx, PlayState* play) {
    EnOE2* this = THIS;
    s8 npcType;
    s16 priBankIndex;
    s16 secBankIndex;
    s16 animBankIndex;

    osSyncPrintf(VT_FGCOL(GREEN) "En_OE2 is initializing~~ (not that I can see these messages~~)\n" VT_RST);

    /* Extract NPC type, then store object info */
    npcType = (this->actor.params >> 11) & 0xF;
    if (npcType >= ARRAY_COUNT(sPrimaryDrawObjects)) {
        osSyncPrintf(VT_FGCOL(RED) "En_OE2 initialization failed, this->npcType == %d!\n" VT_RST, npcType);
        Actor_Kill(thisx);
        return;
    }
    this->npcType = npcType;
    this->priObjectId = sPrimaryDrawObjects[this->npcType];
    this->secObjectId = sSecondaryDrawObjects[this->npcType];
    this->animObjectId = sAnimationObjects[this->npcType];

    this->actor.textId = this->actor.params & 0x00FF;
    if (play->sceneNum == SCENE_KOKIRI_FOREST) {
        this->actor.textId |= 0x1000;
    } else {
        this->actor.textId |= 0x0900;
    }

    /* Grab primary, animation and (if needed) secondary object indices */
    priBankIndex = Object_GetIndex(&play->objectCtx, this->priObjectId);
    animBankIndex = Object_GetIndex(&play->objectCtx, this->animObjectId);
    if (this->secObjectId != -1) {
        secBankIndex = Object_GetIndex(&play->objectCtx, this->secObjectId);
    }

    /* If either primary or animation failed, kill actor */
    if ((priBankIndex < 0) || (animBankIndex < 0)) {
        osSyncPrintf(VT_FGCOL(RED) "En_OE2 initialization failed, this->npcType == %d!\n" VT_RST, this->npcType);
        Actor_Kill(thisx);
        return;
    }

    /* Check if primary and animation objects are loaded */
    if (Object_IsLoaded(&play->objectCtx, priBankIndex) && Object_IsLoaded(&play->objectCtx, animBankIndex)) {
        this->priDrawObjBankIndex = priBankIndex;
        this->animObjBankIndex = animBankIndex;

        /* Check if secondary object is loaded */
        if (Object_IsLoaded(&play->objectCtx, secBankIndex)) {
            this->secDrawObjBankIndex = secBankIndex;
        }

        /* Continue initializing */
        EnOE2_InitCommon(this, play);
    }
}

void EnOE2_Destroy(Actor* thisx, PlayState* play) {
    EnOE2* this = THIS;

    SkelAnime_Free(&this->skelAnime, play);
    Collider_DestroyCylinder(play, &this->collider);
}

void EnOE2_UseDrawObject(EnOE2* this, PlayState* play, s16 objBankIndex) {
    void* object;

    OPEN_DISPS(play->state.gfxCtx);

    object = play->objectCtx.status[objBankIndex].segment;
    gSegments[6] = VIRTUAL_TO_PHYSICAL(object);
    gSPSegment(POLY_OPA_DISP++, 0x06, object);

    CLOSE_DISPS(play->state.gfxCtx);
}

void EnOE2_UseAnimationObject(EnOE2* this, PlayState* play) {
    gSegments[6] = VIRTUAL_TO_PHYSICAL(play->objectCtx.status[this->animObjBankIndex].segment);
}

void EnOE2_UseTextureSegments(EnOE2* this, PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);

    if (this->eyeTexture != NULL) {
        gSPSegment(POLY_OPA_DISP++, 0x08, SEGMENTED_TO_VIRTUAL(this->eyeTexture));
    }
    if (this->mouthTexture != NULL) {
        gSPSegment(POLY_OPA_DISP++, 0x09, SEGMENTED_TO_VIRTUAL(this->mouthTexture));
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

void EnOE2_InitSkelAnime(EnOE2* this, PlayState* play, SkeletonHeader* skeletonHeader) {
    SkelAnime_Init(play, &this->skelAnime, SEGMENTED_TO_VIRTUAL(skeletonHeader), NULL, this->jointTable,
                   this->morphTable, 39);
}

s32 EnOE2_UpdateSkelAnime(EnOE2* this) {
    s32 ret = SkelAnime_Update(&this->skelAnime);
    if (ret) {
        Animation_Reverse(&this->skelAnime);
    }
    return ret;
}

Gfx* EnOE2_AllocEmptyDList(GraphicsContext* gfxCtx) {
    Gfx* dList;

    dList = GRAPH_ALLOC(gfxCtx, sizeof(Gfx));
    gSPEndDisplayList(dList);

    return dList;
}

void EnOE2_DrawSkeleton(EnOE2* this, PlayState* play, OverrideLimbDrawOpa overrideLimbDraw,
                        PostLimbDrawOpa postLimbDraw) {
    OPEN_DISPS(play->state.gfxCtx);

    if (this->goronEyeTexture != NULL) {
        gSPSegment(POLY_OPA_DISP++, 0x08, this->goronEyeTexture);
    }

    gSPSegment(POLY_OPA_DISP++, 0x0D, EnOE2_AllocEmptyDList(play->state.gfxCtx));
    gDPSetEnvColor(POLY_OPA_DISP++, 50, 120, 40, 255);
    SkelAnime_DrawOpa(play, this->skelAnime.skeleton, this->skelAnime.jointTable, overrideLimbDraw, postLimbDraw, this);

    CLOSE_DISPS(play->state.gfxCtx);
}

void EnOE2_ChangeAnimation(EnOE2* this, AnimationHeader* animation, u8 mode, f32 transitionRate, s32 reverse) {
    f32 frameCount;
    f32 startFrame;
    AnimationHeader* anim;
    f32 playbackSpeed;
    s16 frameCountS;

    anim = SEGMENTED_TO_VIRTUAL(animation);
    frameCountS = Animation_GetLastFrame(anim);

    if (!reverse) {
        startFrame = 0.0f;
        frameCount = frameCountS;
        playbackSpeed = 1.0f;
    } else {
        frameCount = 0.0f;
        startFrame = frameCountS;
        playbackSpeed = -1.0f;
    }

    Animation_Change(&this->skelAnime, anim, playbackSpeed, startFrame, frameCount, mode, transitionRate);
}

/*
    0: shouting guy
    1: hands on chest
    2: arms wide
    3: sitting with legs out
    4: fighting stance
    5: one arm bent, one arm out
    6: sitting above shop
    7: waving arms
    8: arms pumping forward
    9: bent knees to side and pumping arms up
    10: crossed legs / shy
*/
void EnOE2_PlayStaticAnim(EnOE2* this, u8 animationId) {
    Animation_Change(&this->skelAnime, &D_06002DC8, 0.1f, 2 * animationId, 2 * animationId + 1, ANIMMODE_ONCE_INTERP,
                     0.0f);
}

void EnOE2_UpdateEyes(EnOE2* this) {
    if (this->eyeBlinkTimer != 0) {
        this->eyeBlinkTimer--;
    }
    if (this->eyeBlinkTimer == 0) {
        this->eyeTextureIndex++;
        if (this->eyeTextureIndex >= 3) {
            this->eyeTextureIndex = 0;
            this->eyeBlinkTimer = ((s16)Rand_ZeroFloat(60.0f) + 0x14);
        }
    }
}

void EnOE2_UpdateMouth(EnOE2* this) {
    this->mouthTextureIndex = 0;
}

void EnOE2_LookAtPlayerSmoothStepMovement(EnOE2* this, PlayState* play) {
    s32 pad[2];
    Vec3s* vec1 = &this->npcInfo.headRot;
    Vec3s* vec2 = &this->npcInfo.torsoRot;

    Math_SmoothStepToS(&vec1->x, 0, 20, 6200, 100);
    Math_SmoothStepToS(&vec1->y, 0, 20, 6200, 100);

    Math_SmoothStepToS(&vec2->x, 0, 20, 6200, 100);
    Math_SmoothStepToS(&vec2->y, 0, 20, 6200, 100);
}

void EnOE2_RotateToPlayer(EnOE2* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    s16 angle = this->actor.yawTowardsPlayer - this->actor.shape.rot.y;

    // check to only look at player within certain distance and angle range, reset to default if player is out of range
    if ((angle < 14563) && (angle > -14563) && this->actor.xzDistToPlayer < 300.0f) {
        this->npcInfo.trackPos = player->actor.world.pos;

        // OE2 will look higher up if you're Adult Link, except gorons
        if (LINK_IS_ADULT && this->npcType != 12) {
            this->npcInfo.trackPos.y = (player->actor.world.pos.y + 40.0f);
        }

        this->npcInfo.yOffset = kREG(16) + 12.0f;

        Npc_TurnTowardsFocus(&this->actor, &this->npcInfo, kREG(17) + 0xC, 2);
        EnOE2_LookAtPlayerSmoothStepMovement(this, play);
    } else {
        Npc_TurnTowardsFocus(&this->actor, &this->npcInfo, 0, 1);
    }
}

void EnOE2_CheckMessageDisplay(EnOE2* this, PlayState* play) {
    /* If not already doing text, do text? */
    if (Actor_IsTalking(&this->actor, play) == 0 && this->actor.textId != 0) {
        Actor_RequestToTalkInRange(&this->actor, play, 50.0f + this->actor.colChkInfo.cylRadius);
    }
}

void EnOE2_CalculateHeadRotation(EnOE2* this, PlayState* play) {
    // EnOE2_RotateToPlayer(this, play);
}

void EnOE2_UpdateCollision(EnOE2* this, PlayState* play) {
    Actor_UpdateBgCheckInfo(play, &this->actor, 20.0f, 20.0f, 60.0f, 0x1D);
    Collider_UpdateCylinder(&this->actor, &this->collider);
    CollisionCheck_SetOC(play, &play->colChkCtx, &this->collider.base);
}

s32 EnOE2_OverrideLimbDrawHeadOnly(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* thisx) {
    EnOE2* this = THIS;
    Vec3s* waistangle = &this->npcInfo.torsoRot;
    Vec3s* neckangle = &this->npcInfo.headRot;

    OPEN_DISPS(play->state.gfxCtx);

    /* Is current limb the head? */
    if (limbIndex == 23) {
        /* Use secondary draw object, i.e. object of head */
        EnOE2_UseDrawObject(this, play, this->secDrawObjBankIndex);
        EnOE2_UseTextureSegments(this, play);

        /* Select correct head display list for object */
        switch (this->secObjectId) {
            case OBJECT_OE6:
                *dList = D_06000AE0;
                break;
            case OBJECT_OE7:
                *dList = D_060006B0;
                break;
            case OBJECT_OE8:
                *dList = D_06000CA0;
                break;
            case OBJECT_OE9:
                *dList = D_06000800;
                break;
            case OBJECT_OE10:
                *dList = D_06000720;
                break;
            case OBJECT_OE11:
                *dList = D_060009F0;
                break;
            case OBJECT_OE12:
                *dList = D_06001020;
                break;
            default:
                *dList = NULL;
                break;
        }
    }

    // head and waist rotation to look at player
    switch (limbIndex) {
        case 21:
            rot->x += waistangle->y;
            rot->y -= waistangle->x;
            break;
        case 23:
            rot->x += neckangle->y;
            rot->z += neckangle->x;
            break;
    }

    CLOSE_DISPS(play->state.gfxCtx);

    return false;
}

s32 EnOE2_OverrideLimbDrawTurnOnly(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* thisx) {
    EnOE2* this = THIS;
    Vec3s* waistangle = &this->npcInfo.torsoRot;
    Vec3s* neckangle = &this->npcInfo.headRot;

    // head and waist rotation to look at player
    switch (limbIndex) {
        case 21:
            rot->x += waistangle->y;
            rot->y -= waistangle->x;
            // make gorons bend down to Link with their upper body
            if (this->npcType == 12 && LINK_IS_CHILD) {
                rot->z += neckangle->x + 1500;
            }
            break;
        case 23:
            rot->x += neckangle->y;
            rot->z += neckangle->x;
            break;
    }

    OPEN_DISPS(play->state.gfxCtx);
    // set headband to blue
    if (this->npcType == 1) {
        if (limbIndex == 23) {
            gDPSetEnvColor(POLY_OPA_DISP++, 50, 255, 255, 255);
        } else {
            gDPSetEnvColor(POLY_OPA_DISP++, 50, 120, 40, 255);
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);

    return false;
}

void EnOE2_PostLimbDrawHeadOnly(PlayState* play, s32 limbIndex, Gfx** limbDList, Vec3s* rot, void* thisx) {
    EnOE2* this = THIS;

    /* Is current limb the head? */
    if (limbIndex == 23) {
        /* Restore primary draw object */
        EnOE2_UseDrawObject(this, play, this->priDrawObjBankIndex);
    }
}

s32 EnOE2_OverrideLimbDrawFado(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* thisx) {
    EnOE2* this = THIS;
    Vec3s* waistangle = &this->npcInfo.torsoRot;
    Vec3s* neckangle = &this->npcInfo.headRot;

    /* Are we between limbs 0 and 22, where the dlists come from oE2? */
    if ((limbIndex >= 0) && (limbIndex < 22)) {
        EnOE2_UseDrawObject(this, play, this->secDrawObjBankIndex);
    } else {
        EnOE2_UseDrawObject(this, play, this->priDrawObjBankIndex);
    }

    // head and waist rotation to look at player
    switch (limbIndex) {
        case 21:
            rot->x += waistangle->y;
            rot->y -= waistangle->x;
            break;
        case 23:
            rot->x += neckangle->y;
            rot->z += neckangle->x;
            break;
    }

    return false;
}

void EnOE2_InitMidoA(EnOE2* this, PlayState* play) {
    EnOE2_UseDrawObject(this, play, this->priDrawObjBankIndex);
    EnOE2_InitSkelAnime(this, play, &D_06000260);
    EnOE2_UseAnimationObject(this, play);
    EnOE2_ChangeAnimation(this, &D_060019BC, ANIMMODE_LOOP, 0.0f, false);
    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_ELF, this->actor.world.pos.x,
                       this->actor.world.pos.y, this->actor.world.pos.z, 0, 0, 0, 3);

    this->updateMode = OE2_UPDATE_MIDO_A;
    this->drawMode = OE2_DRAW_MIDO_A;
}

void EnOE2_UpdateMidoA(EnOE2* this, PlayState* play) {
    EnOE2_UpdateSkelAnime(this);
    EnOE2_UpdateEyes(this);
    // EnOE2_RotateToPlayer(this, play);
    EnOE2_UpdateCollision(this, play);
    EnOE2_CheckMessageDisplay(this, play);
}

void EnOE2_DrawMidoA(EnOE2* this, PlayState* play) {
    static void* eyeTextures[] = { 0x06004A08, 0x06004E08, 0x06005208 };
    this->eyeTexture = eyeTextures[this->eyeTextureIndex];
    EnOE2_UseTextureSegments(this, play);
    EnOE2_DrawSkeleton(this, play, EnOE2_OverrideLimbDrawTurnOnly, NULL);
}

void EnOE2_InitRedheadGirl(EnOE2* this, PlayState* play) {
    EnOE2_UseDrawObject(this, play, this->priDrawObjBankIndex);
    EnOE2_InitSkelAnime(this, play, &D_06000260);
    EnOE2_UseAnimationObject(this, play);
    EnOE2_ChangeAnimation(this, &D_0600242C, ANIMMODE_LOOP, 0.0f, false);
    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_ELF, this->actor.world.pos.x,
                       this->actor.world.pos.y, this->actor.world.pos.z, 0, 0, 0, 3);

    this->updateMode = OE2_UPDATE_REDHEAD_GIRL;
    this->drawMode = OE2_DRAW_REDHEAD_GIRL;
}

void EnOE2_UpdateRedheadGirl(EnOE2* this, PlayState* play) {
    EnOE2_UpdateSkelAnime(this);
    EnOE2_UpdateEyes(this);
    // EnOE2_RotateToPlayer(this, play);
    EnOE2_UpdateCollision(this, play);
    EnOE2_CheckMessageDisplay(this, play);
}

void EnOE2_DrawRedheadGirl(EnOE2* this, PlayState* play) {
    static void* eyeTextures[] = { 0x06003E70, 0x06004270, 0x06004670 };
    this->eyeTexture = eyeTextures[this->eyeTextureIndex];
    EnOE2_UseTextureSegments(this, play);
    EnOE2_DrawSkeleton(this, play, EnOE2_OverrideLimbDrawTurnOnly, NULL);
}

void EnOE2_InitSaria(EnOE2* this, PlayState* play) {
    EnOE2_UseDrawObject(this, play, this->priDrawObjBankIndex);
    EnOE2_InitSkelAnime(this, play, &D_06000260);
    EnOE2_UseAnimationObject(this, play);

    Animation_Change(&this->skelAnime, &D_06001034, 0.1f, 0, 4, ANIMMODE_ONCE_INTERP, 0.0f);
    // EnOE2_ChangeAnimation(this, &D_0600242C, ANIMMODE_LOOP, 0.0f, false);

    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_ELF, this->actor.world.pos.x,
                       this->actor.world.pos.y, this->actor.world.pos.z, 0, 0, 0, 3);

    this->updateMode = OE2_UPDATE_SARIA;
    this->drawMode = OE2_DRAW_SARIA;
}

void EnOE2_UpdateSaria(EnOE2* this, PlayState* play) {
    EnOE2_UpdateSkelAnime(this);
    EnOE2_UpdateEyes(this);
    EnOE2_UpdateMouth(this);
    // EnOE2_RotateToPlayer(this, play);
    EnOE2_UpdateCollision(this, play);
    EnOE2_CheckMessageDisplay(this, play);
}

void EnOE2_DrawSaria(EnOE2* this, PlayState* play) {
    static void* eyeTextures[] = { 0x06005120, 0x06005520, 0x06005920 };
    static void* mouthTextures[] = { 0x06005F60, 0x06006160, 0x06006360 };
    this->eyeTexture = eyeTextures[this->eyeTextureIndex];
    this->mouthTexture = mouthTextures[this->mouthTextureIndex];
    EnOE2_UseTextureSegments(this, play);
    EnOE2_DrawSkeleton(this, play, EnOE2_OverrideLimbDrawTurnOnly, NULL);
}

void EnOE2_InitRedheadBoyA(EnOE2* this, PlayState* play) {
    EnOE2_UseDrawObject(this, play, this->priDrawObjBankIndex);
    EnOE2_InitSkelAnime(this, play, &D_06000260);
    EnOE2_UseAnimationObject(this, play);
    EnOE2_ChangeAnimation(this, &D_060019BC, ANIMMODE_LOOP, 0.0f, false);
    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_ELF, this->actor.world.pos.x,
                       this->actor.world.pos.y, this->actor.world.pos.z, 0, 0, 0, 3);

    this->updateMode = OE2_UPDATE_REDHEAD_BOY_A;
    this->drawMode = OE2_DRAW_REDHEAD_BOY_A;
}

void EnOE2_UpdateRedheadBoyA(EnOE2* this, PlayState* play) {
    EnOE2_UpdateSkelAnime(this);
    // EnOE2_RotateToPlayer(this, play);
    EnOE2_UpdateCollision(this, play);
    EnOE2_CheckMessageDisplay(this, play);
}

void EnOE2_DrawRedheadBoyA(EnOE2* this, PlayState* play) {
    EnOE2_DrawSkeleton(this, play, EnOE2_OverrideLimbDrawTurnOnly, NULL);
}

// fado sitting anim => 06002DC8, frames 12-13
void EnOE2_InitFadoA(EnOE2* this, PlayState* play) {
    EnOE2_UseDrawObject(this, play, this->priDrawObjBankIndex);
    EnOE2_InitSkelAnime(this, play, &D_06000260);
    EnOE2_UseAnimationObject(this, play);
    EnOE2_PlayStaticAnim(this, 6);
    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_ELF, this->actor.world.pos.x,
                       this->actor.world.pos.y, this->actor.world.pos.z, 0, 0, 0, 3);
    ActorShape_Init(&this->actor.shape, 275.0f, &ActorShadow_DrawCircle, 15.0f);
    this->actor.targetArrowOffset = 40.0f;

    this->updateMode = OE2_UPDATE_FADO_A;
    this->drawMode = OE2_DRAW_FADO_A;
}

void EnOE2_UpdateFadoA(EnOE2* this, PlayState* play) {
    EnOE2_UpdateSkelAnime(this);
    EnOE2_UpdateEyes(this);
    EnOE2_UpdateCollision(this, play);
    EnOE2_CalculateHeadRotation(this, play);
    EnOE2_CheckMessageDisplay(this, play);
    Actor_SetFocus(&this->actor, 20.0f);
}

void EnOE2_DrawFadoA(EnOE2* this, PlayState* play) {
    static void* eyeTextures[] = { 0x06003640, 0x06003A40, 0x06003E40 };
    this->eyeTexture = eyeTextures[this->eyeTextureIndex];
    EnOE2_UseTextureSegments(this, play);
    EnOE2_DrawSkeleton(this, play, EnOE2_OverrideLimbDrawFado, NULL);
}

void EnOE2_InitGirlBrownBand(EnOE2* this, PlayState* play) {
    EnOE2_UseDrawObject(this, play, this->priDrawObjBankIndex);
    EnOE2_InitSkelAnime(this, play, &D_06000260);
    EnOE2_UseAnimationObject(this, play);
    EnOE2_PlayStaticAnim(this, 10);
    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_ELF, this->actor.world.pos.x,
                       this->actor.world.pos.y, this->actor.world.pos.z, 0, 0, 0, 3);

    this->updateMode = OE2_UPDATE_GIRL_BROWN_BAND;
    this->drawMode = OE2_DRAW_GIRL_BROWN_BAND;
}

void EnOE2_UpdateGirlBrownBand(EnOE2* this, PlayState* play) {
    EnOE2_UpdateSkelAnime(this);
    EnOE2_UpdateEyes(this);
    // EnOE2_RotateToPlayer(this, play);
    EnOE2_UpdateCollision(this, play);
    EnOE2_CheckMessageDisplay(this, play);
}

void EnOE2_DrawGirlBrownBand(EnOE2* this, PlayState* play) {
    static void* eyeTextures[] = { 0x060011D0, 0x060015D0, 0x060019D0 };
    this->eyeTexture = eyeTextures[this->eyeTextureIndex];
    EnOE2_DrawSkeleton(this, play, EnOE2_OverrideLimbDrawHeadOnly, EnOE2_PostLimbDrawHeadOnly);
}

void EnOE2_InitBoyGreenCap(EnOE2* this, PlayState* play) {
    EnOE2_UseDrawObject(this, play, this->priDrawObjBankIndex);
    EnOE2_InitSkelAnime(this, play, &D_06000260);
    EnOE2_UseAnimationObject(this, play);
    EnOE2_PlayStaticAnim(this, 0);
    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_ELF, this->actor.world.pos.x,
                       this->actor.world.pos.y, this->actor.world.pos.z, 0, 0, 0, 3);

    this->updateMode = OE2_UPDATE_BOY_GREEN_CAP;
    this->drawMode = OE2_DRAW_BOY_GREEN_CAP;
}

void EnOE2_UpdateBoyGreenCap(EnOE2* this, PlayState* play) {
    EnOE2_UpdateSkelAnime(this);
    EnOE2_UpdateEyes(this);
    // EnOE2_RotateToPlayer(this, play);
    EnOE2_UpdateCollision(this, play);
    EnOE2_CheckMessageDisplay(this, play);
}

void EnOE2_DrawBoyGreenCap(EnOE2* this, PlayState* play) {
    static void* eyeTextures[] = { 0x06001458, 0x06001858, 0x06001C58 };
    this->eyeTexture = eyeTextures[this->eyeTextureIndex];
    EnOE2_DrawSkeleton(this, play, EnOE2_OverrideLimbDrawHeadOnly, EnOE2_PostLimbDrawHeadOnly);
}

void EnOE2_InitGirlGreenBand(EnOE2* this, PlayState* play) {
    EnOE2_UseDrawObject(this, play, this->priDrawObjBankIndex);
    EnOE2_InitSkelAnime(this, play, &D_06000260);
    EnOE2_UseAnimationObject(this, play);
    EnOE2_PlayStaticAnim(this, 2);
    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_ELF, this->actor.world.pos.x,
                       this->actor.world.pos.y, this->actor.world.pos.z, 0, 0, 0, 3);

    this->updateMode = OE2_UPDATE_GIRL_GREEN_BAND;
    this->drawMode = OE2_DRAW_GIRL_GREEN_BAND;

    this->collider.dim.radius = 30;
    this->collider.dim.height = 120;
}

void EnOE2_UpdateGirlGreenBand(EnOE2* this, PlayState* play) {
    EnOE2_UpdateSkelAnime(this);
    EnOE2_UpdateEyes(this);
    EnOE2_UpdateCollision(this, play);
    EnOE2_CheckMessageDisplay(this, play);
    Actor_SetFocus(&this->actor, 30.0f);
}

void EnOE2_DrawGirlGreenBand(EnOE2* this, PlayState* play) {
    static void* eyeTextures[] = { 0x06001448, 0x06001848, 0x06001C48 };
    this->eyeTexture = eyeTextures[this->eyeTextureIndex];
    EnOE2_DrawSkeleton(this, play, EnOE2_OverrideLimbDrawHeadOnly, EnOE2_PostLimbDrawHeadOnly);
}

void EnOE2_InitBoyMintCap(EnOE2* this, PlayState* play) {
    EnOE2_UseDrawObject(this, play, this->priDrawObjBankIndex);
    EnOE2_InitSkelAnime(this, play, &D_06000260);
    EnOE2_UseAnimationObject(this, play);
    EnOE2_PlayStaticAnim(this, 5);
    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_ELF, this->actor.world.pos.x,
                       this->actor.world.pos.y, this->actor.world.pos.z, 0, 0, 0, 3);

    this->updateMode = OE2_UPDATE_BOY_MINT_CAP;
    this->drawMode = OE2_DRAW_BOY_MINT_CAP;
}

void EnOE2_UpdateBoyMintCap(EnOE2* this, PlayState* play) {
    EnOE2_UpdateSkelAnime(this);
    EnOE2_UpdateEyes(this);
    // EnOE2_RotateToPlayer(this, play);
    EnOE2_UpdateCollision(this, play);
    EnOE2_CheckMessageDisplay(this, play);
}

void EnOE2_DrawBoyMintCap(EnOE2* this, PlayState* play) {
    static void* eyeTextures[] = { 0x06000E90, 0x06001290, 0x06001690 };
    this->eyeTexture = eyeTextures[this->eyeTextureIndex];
    EnOE2_DrawSkeleton(this, play, EnOE2_OverrideLimbDrawHeadOnly, EnOE2_PostLimbDrawHeadOnly);
}

void EnOE2_InitShopVendor(EnOE2* this, PlayState* play) {
    EnOE2_UseDrawObject(this, play, this->priDrawObjBankIndex);
    EnOE2_InitSkelAnime(this, play, &D_06000260);
    EnOE2_UseAnimationObject(this, play);
    EnOE2_PlayStaticAnim(this, 3);
    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_ELF, this->actor.world.pos.x,
                       this->actor.world.pos.y, this->actor.world.pos.z, 0, 0, 0, 3);

    this->updateMode = OE2_UPDATE_SHOP_VENDOR;
    this->drawMode = OE2_DRAW_SHOP_VENDOR;
}

void EnOE2_UpdateShopVendor(EnOE2* this, PlayState* play) {
    EnOE2_UpdateSkelAnime(this);
    EnOE2_UpdateEyes(this);
    EnOE2_UpdateCollision(this, play);
    EnOE2_CheckMessageDisplay(this, play);
    Actor_SetFocus(&this->actor, 20.0f);
}

void EnOE2_DrawShopVendor(EnOE2* this, PlayState* play) {
    static void* eyeTextures[] = { 0x06001190, 0x06001190, 0x06001190 };
    this->eyeTexture = eyeTextures[this->eyeTextureIndex];
    EnOE2_DrawSkeleton(this, play, EnOE2_OverrideLimbDrawHeadOnly, EnOE2_PostLimbDrawHeadOnly);
}

void EnOE2_InitGirlGreenCap(EnOE2* this, PlayState* play) {
    EnOE2_UseDrawObject(this, play, this->priDrawObjBankIndex);
    EnOE2_InitSkelAnime(this, play, &D_06000260);
    EnOE2_UseAnimationObject(this, play);
    EnOE2_PlayStaticAnim(this, 4);
    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_ELF, this->actor.world.pos.x,
                       this->actor.world.pos.y, this->actor.world.pos.z, 0, 0, 0, 3);

    this->updateMode = OE2_UPDATE_GIRL_GREEN_CAP;
    this->drawMode = OE2_DRAW_GIRL_GREEN_CAP;
}

void EnOE2_UpdateGirlGreenCap(EnOE2* this, PlayState* play) {
    EnOE2_UpdateSkelAnime(this);
    EnOE2_UpdateEyes(this);
    // EnOE2_RotateToPlayer(this, play);
    EnOE2_UpdateCollision(this, play);
    EnOE2_CheckMessageDisplay(this, play);
}

void EnOE2_DrawGirlGreenCap(EnOE2* this, PlayState* play) {
    static void* eyeTextures[] = { 0x06001170, 0x06001570, 0x06001970 };
    this->eyeTexture = eyeTextures[this->eyeTextureIndex];
    EnOE2_DrawSkeleton(this, play, EnOE2_OverrideLimbDrawHeadOnly, EnOE2_PostLimbDrawHeadOnly);
}

void EnOE2_InitFadoB(EnOE2* this, PlayState* play) {
    EnOE2_UseDrawObject(this, play, this->priDrawObjBankIndex);
    EnOE2_InitSkelAnime(this, play, &D_06000260);
    EnOE2_UseAnimationObject(this, play);
    EnOE2_ChangeAnimation(this, &D_0600242C, ANIMMODE_LOOP, 0.0f, false);
    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_ELF, this->actor.world.pos.x,
                       this->actor.world.pos.y, this->actor.world.pos.z, 0, 0, 0, 3);

    this->updateMode = OE2_UPDATE_FADO_B;
    this->drawMode = OE2_DRAW_FADO_B;
}

void EnOE2_UpdateFadoB(EnOE2* this, PlayState* play) {
    EnOE2_UpdateSkelAnime(this);
    EnOE2_UpdateEyes(this);
    // EnOE2_RotateToPlayer(this, play);
    EnOE2_UpdateCollision(this, play);
    EnOE2_CheckMessageDisplay(this, play);
}

void EnOE2_DrawFadoB(EnOE2* this, PlayState* play) {
    static void* eyeTextures[] = { 0x06001800, 0x06001C00, 0x06002000 };
    this->eyeTexture = eyeTextures[this->eyeTextureIndex];
    EnOE2_DrawSkeleton(this, play, EnOE2_OverrideLimbDrawHeadOnly, EnOE2_PostLimbDrawHeadOnly);
}

static u64 D_06004640 = 0; // Goron eye texture 1 (stub)
static u64 D_06006300 = 0; // Goron eye texture 2 (stub)
static u64 D_06006B40 = 0; // Goron eye texture 3 (stub)

static void* eye1Tex = NULL;
static void* eye2Tex = NULL;
static void* eye3Tex = NULL;

void EnOE2_InitGoron(EnOE2* this, PlayState* play) {
    EnOE2_UseDrawObject(this, play, this->priDrawObjBankIndex);
    SkelAnime_Init(play, &this->skelAnime, SEGMENTED_TO_VIRTUAL(&D_0600CC80), NULL, this->jointTable, this->morphTable,
                   39);
    eye1Tex = SEGMENTED_TO_VIRTUAL(&D_06004640);
    eye2Tex = SEGMENTED_TO_VIRTUAL(&D_06006300);
    eye3Tex = SEGMENTED_TO_VIRTUAL(&D_06006B40);

    EnOE2_UseAnimationObject(this, play);
    Animation_Change(&this->skelAnime, SEGMENTED_TO_VIRTUAL(&D_060047D4), 0.0f, 0.0f, 0.0f, ANIMMODE_ONCE, 0.0f);

    this->updateMode = OE2_UPDATE_GORON;
    this->drawMode = OE2_DRAW_GORON;
    this->actor.flags &= ~1;

    this->goronState = 0;

    if (play->sceneNum == SCENE_CHAMBER_OF_THE_SAGES) {
        this->goronState = 999;
        this->actor.flags |= 1;
        EnOE2_ChangeAnimation(this, &D_06005044, ANIMMODE_LOOP, 0.0f, false);
    }
}

void EnOE2_UpdateGoron(EnOE2* this, PlayState* play) {
    Player* player = GET_PLAYER(play);

    switch (this->goronState) {
        case 0:
            EnOE2_UpdateSkelAnime(this);
            if (this->actor.xzDistToPlayer <= 200.0f && this->actor.yDistToPlayer <= 100.0f) {
                this->goronState = 1;
                EnOE2_ChangeAnimation(this, &D_060047D4, ANIMMODE_ONCE, 0.0f, false);
            }
            break;
        case 1:
            if (EnOE2_UpdateSkelAnime(this)) {
                this->goronState = 2;
                this->actor.flags |= 1;
                EnOE2_ChangeAnimation(this, &D_06005044, ANIMMODE_LOOP, 0.0f, false);
            }
            this->npcInfo.trackPos = player->actor.world.pos;
            Npc_TurnTowardsFocus(&this->actor, &this->npcInfo, kREG(17) + 0xC, 4);
            break;
        case 2:
            EnOE2_UpdateSkelAnime(this);
            if (this->actor.xzDistToPlayer >= 400.0f || this->actor.yDistToPlayer >= 100.0f) {
                this->goronState = 3;
                this->actor.flags &= ~1;
                EnOE2_ChangeAnimation(this, &D_060047D4, ANIMMODE_ONCE, 0.0f, true);
            }
            // EnOE2_RotateToPlayer(this, play);
            break;
        case 3:
            if (EnOE2_UpdateSkelAnime(this)) {
                this->goronState = 0;
                Animation_Change(&this->skelAnime, SEGMENTED_TO_VIRTUAL(&D_060047D4), 0.0f, 0.0f, 0.0f, ANIMMODE_ONCE,
                                 0.0f);
            }
            break;
        default:
            EnOE2_UpdateSkelAnime(this);
    }

    EnOE2_UpdateCollision(this, play);
    EnOE2_CheckMessageDisplay(this, play);
    Actor_SetFocus(&this->actor, 60.0f);
}

void EnOE2_DrawGoron(EnOE2* this, PlayState* play) {

    if (((play->state.frames % 0x40) >= 2) && ((play->state.frames % 0x40) < 0x6)) {
        this->goronEyeTexture = eye3Tex;
    } else if ((play->state.frames % 0x40) < 0x8) {
        this->goronEyeTexture = eye2Tex;
    } else {
        this->goronEyeTexture = eye1Tex;
    }
    EnOE2_DrawSkeleton(this, play, EnOE2_OverrideLimbDrawTurnOnly, NULL);
}

// ---------------------------------------------------------------------------
// Function tables
// ---------------------------------------------------------------------------

static EnOE2InitFunc sInitFuncs[] = {
    EnOE2_InitMidoA,      EnOE2_InitRedheadGirl,   EnOE2_InitSaria,        EnOE2_InitRedheadBoyA,
    EnOE2_InitFadoA,      EnOE2_InitGirlBrownBand, EnOE2_InitBoyGreenCap,  EnOE2_InitGirlGreenBand,
    EnOE2_InitBoyMintCap, EnOE2_InitShopVendor,    EnOE2_InitGirlGreenCap, EnOE2_InitFadoB,
    EnOE2_InitGoron,
};

static EnOE2UpdateFunc sUpdateFuncs[] = {
    EnOE2_UpdateMidoA,      EnOE2_UpdateRedheadGirl,   EnOE2_UpdateSaria,        EnOE2_UpdateRedheadBoyA,
    EnOE2_UpdateFadoA,      EnOE2_UpdateGirlBrownBand, EnOE2_UpdateBoyGreenCap,  EnOE2_UpdateGirlGreenBand,
    EnOE2_UpdateBoyMintCap, EnOE2_UpdateShopVendor,    EnOE2_UpdateGirlGreenCap, EnOE2_UpdateFadoB,
    EnOE2_UpdateGoron,
};

static EnOE2DrawFunc sDrawFuncs[] = {
    EnOE2_DrawMidoA,      EnOE2_DrawRedheadGirl,   EnOE2_DrawSaria,        EnOE2_DrawRedheadBoyA,
    EnOE2_DrawFadoA,      EnOE2_DrawGirlBrownBand, EnOE2_DrawBoyGreenCap,  EnOE2_DrawGirlGreenBand,
    EnOE2_DrawBoyMintCap, EnOE2_DrawShopVendor,    EnOE2_DrawGirlGreenCap, EnOE2_DrawFadoB,
    EnOE2_DrawGoron,
};

// ---------------------------------------------------------------------------
// Common init / Update / Draw
// ---------------------------------------------------------------------------

void EnOE2_InitCommon(EnOE2* this, PlayState* play) {
    /* Configure misc actor stuffs */
    Actor_SetScale(&this->actor, 0.01f);
    ActorShape_Init(&this->actor.shape, 0.0f, &ActorShadow_DrawCircle, 15.0f);
    this->actor.targetMode = 2;
    this->actor.gravity = -3.0f;
    this->actor.colChkInfo.mass = MASS_IMMOVABLE;

    /* Configure collision */
    Collider_InitCylinder(play, &this->collider);
    Collider_SetCylinder(play, &this->collider, &this->actor, &sOe2CylinderInit);

    /* Run individual init functions */
    if (sInitFuncs[this->npcType] == NULL) {
        osSyncPrintf(VT_FGCOL(RED) "En_OE2 initialization failed, sInitFuncs[this->npcType] == NULL!\n" VT_RST);
        Actor_Kill(&this->actor);
        return;
    }
    sInitFuncs[this->npcType](this, play);

    if (this->actor.child != NULL && play->sceneNum == SCENE_CHAMBER_OF_THE_SAGES) {
        Actor_Kill(this->actor.child);
    }

    osSyncPrintf(VT_FGCOL(GREEN) "En_OE2 initialization done!\n" VT_RST);
}

void EnOE2_Update(Actor* thisx, PlayState* play) {
    EnOE2* this = THIS;
    s32 updateMode = this->updateMode;

    Actor_SetFocus(&this->actor, 40.0f);

    if ((updateMode < 0) || (updateMode >= ARRAY_COUNT(sUpdateFuncs)) || sUpdateFuncs[updateMode] == NULL) {
        osSyncPrintf(VT_FGCOL(RED) "En_OE2 update failed, this->updateMode == %d\n" VT_RST, updateMode);
    } else {
        EnOE2_UseAnimationObject(this, play);
        sUpdateFuncs[updateMode](this, play);
    }

    if (func_8010BDBC(&play->msgCtx) == 4 && func_80106BC8(play)) { // selected a choice
        if (this->actor.textId == 0x1003 || this->actor.textId == 0x1004) {
            if (play->msgCtx.choiceIndex == 0) {
                func_8010B720(play, 0x1005);
            } else {
                func_8010B720(play, 0x1006);
            }
        }
    }

    EnOE2_RotateToPlayer(this, play);

    // apply gravity
    Actor_MoveForwardXZ(&this->actor);
    Actor_UpdateBgCheckInfo(play, &this->actor, 0.0f, 0.0f, 0.0f, 4);
}

void EnOE2_Draw(Actor* thisx, PlayState* play) {
    EnOE2* this = THIS;
    s32 drawMode = this->drawMode;

    Gfx* newDList;
    Gfx* polyOpaP;
    GfxPrint printer;

    OPEN_DISPS(play->state.gfxCtx);

    func_80093C80(play);
    func_80093D84(play->state.gfxCtx);

    if (this->drawFlags & OE2_DRAWFLAG_DEBUG) {

        newDList = Graph_GfxPlusOne(polyOpaP = POLY_OPA_DISP);
        gSPDisplayList(OVERLAY_DISP++, newDList);

        GfxPrint_Init(&printer);
        GfxPrint_Open(&printer, newDList);
        GfxPrint_SetColor(&printer, 255, 155, 255, 255);
        GfxPrint_SetPos(&printer, 3, 8);
        GfxPrint_Printf(&printer, "p:%04x s:%04x a:%04x", this->priObjectId, this->secObjectId, this->animObjectId);
        newDList = GfxPrint_Close(&printer);
        GfxPrint_Destroy(&printer);

        gSPEndDisplayList(newDList++);
        Graph_BranchDlist(polyOpaP, newDList);
        POLY_OPA_DISP = newDList;
    }

    if ((drawMode < 0) || (drawMode >= ARRAY_COUNT(sDrawFuncs)) || sDrawFuncs[drawMode] == NULL) {
        osSyncPrintf(VT_FGCOL(RED) "En_OE2 draw failed, this->drawMode == %d\n" VT_RST, drawMode);
    } else {
        EnOE2_UseDrawObject(this, play, this->priDrawObjBankIndex);
        sDrawFuncs[drawMode](this, play);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}
