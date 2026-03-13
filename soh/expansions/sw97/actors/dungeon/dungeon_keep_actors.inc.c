/**
 * dungeon_keep_actors.c - Merged dungeon actor set for SW97 expansion
 *
 * Original: z64proto/sw97 team (Spaceworld '97 Experience)
 * Adapted for Ship of Harkinian (Shipwright)
 *
 * Contains: Crashbox, BetaFloorSwitch, FlameThrower, Floater, DungeonKeep dispatcher
 * Merged from ovl_Dungeon_Keep source files.
 */

#include "expansions/sw97/sw97_compat.h"
#include "expansions/sw97/sw97_config.h"

#include "objects/gameplay_dangeon_keep/gameplay_dangeon_keep.h"
#include "objects/object_hidan_objects/object_hidan_objects.h"
#include "overlays/effects/ovl_Effect_Ss_Kakera/z_eff_ss_kakera.h"

/* ========================================================================
 * Actor ID — assigned at runtime by sw97_router.c
 * ======================================================================== */
extern s16 gSw97ActorId_DungeonKeep;

/* ========================================================================
 * Header (from dungeon_keep_actors.h)
 * ======================================================================== */

#define DK_TYPE(params) (params & 0xFF)

#define FLAME_COUNT 5
#define Y_OFFSET 50.0f
#define FLAME_DIST_FACTOR 40.0f
#define FLAME_DURATION 100

typedef enum {
    /* 0x03 */ DK_CRASHBOX_LARGE = 0x3,
    /* 0x12 */ DK_ROLLING_BOULDER = 0x12,
    /* 0x15 */ DK_FLOOR_SWITCH = 0x15,
    /* 0x16 */ DK_FLAME_THROWER = 0x16,
    /* 0x18 */ DK_CRYSTAL_SWITCH_18 = 0x18,
    /* 0x19 */ DK_CRYSTAL_SWITCH = 0x19,
    /* 0x1A */ DK_EYE_SWITCH = 0x1A,
    /* 0x1B */ DK_EYE_SWITCH_1B = 0x1B,
    /* 0x1E */ DK_EYE_SWITCH_1E = 0x1E,
    /* 0x1F */ DK_EYE_SWITCH_1F = 0x1F,
    /* 0x23 */ DK_PUSH_BLOCK = 0x23,
    /* 0x24 */ DK_PUSH_BLOCK_SMALL = 0x24,
    /* 0x27 */ DK_FLOATER = 0x27,
    /* 0x28 */ DK_FLOATER_BIG = 0x28,
    /* 0x2B */ DK_CRASHBOX_SMALL = 0x2B,
    /* 0x2C */ DK_FLOOR_SWITCH_DEKU_TIMER = 0x2C
} DkType;

struct DungeonKeep;

typedef void (*DungeonKeepActionFunc)(struct DungeonKeep*, PlayState*);

typedef struct DungeonKeep {
    DynaPolyActor dyna;
    DungeonKeepActionFunc actionFunc;
    s32 timer;
    s32 timer2;
    ColliderCylinder collider;
    // Flame Thrower
    s16 burnFrame;
    ColliderJntSph colliderSph;
    ColliderJntSphElement colliderItems[FLAME_COUNT];
    s16 bankIndex;
    //
} DungeonKeep; // size = 0x014C

/* Forward declarations */
void BetaFloorSwitch_Init(DungeonKeep* this, PlayState* play);
void Floater_Init(DungeonKeep* this, PlayState* play);
void Crashbox_Init(DungeonKeep* this, PlayState* play);
void FlameThrower_Init(Actor* thisx, PlayState* play);
void FlameThrower_Destroy(Actor* thisx, PlayState* play);
void FlameThrower_Update(Actor* thisx, PlayState* play);
void FlameThrower_Draw(Actor* thisx, PlayState* play);

/* ========================================================================
 * Crashbox (crashbox.c)
 * ======================================================================== */

#define FLAGS_CRASHBOX 0x00000030

#define THIS ((DungeonKeep*)thisx)

void Crashbox_Destroy(Actor* thisx, PlayState* play);
void Crashbox_Update(Actor* thisx, PlayState* play);
void Crashbox_Draw(Actor* thisx, PlayState* play);

void Crashbox_Wait(DungeonKeep* this, PlayState* play);

static ColliderCylinderInit sDungeonCylinderInit = {
    {
        COLTYPE_NONE,
        AT_NONE,
        AC_ON | AC_TYPE_ALL,
        OC1_NONE,
        OC2_NONE,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK2,
        { 0x00000000, 0x00, 0x00 },
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_NONE,
        BUMP_ON,
        OCELEM_NONE,
    },
    { 50, 70, 0, { 0 } },
};

void Crashbox_InitDynapoly(DungeonKeep* this, PlayState* play, CollisionHeader* collision, DynaPolyMoveFlag moveFlag) {
    s32 pad;
    CollisionHeader* colHeader = NULL;
    s32 pad2;

    DynaPolyActor_Init(&this->dyna, moveFlag);
    CollisionHeader_GetVirtual(collision, &colHeader);
    this->dyna.bgId = DynaPoly_SetBgActor(play, &play->colCtx.dyna, &this->dyna.actor, colHeader);

    if (this->dyna.bgId == BG_ACTOR_MAX) {
        osSyncPrintf("dynapoly is fucked\n");
    }
}

void Crashbox_Init(DungeonKeep* this, PlayState* play) {
    CollisionHeader* colHeader = NULL;

    this->dyna.actor.destroy = Crashbox_Destroy;
    this->dyna.actor.update = Crashbox_Update;
    this->dyna.actor.draw = Crashbox_Draw;

    this->timer = 5;

    Collider_InitCylinder(play, &this->collider);
    Collider_SetCylinder(play, &this->collider, &this->dyna.actor, &sDungeonCylinderInit);

    /* SW97: gCrashboxCol not available (no sw97.otr), skip dynapoly init */
    // Crashbox_InitDynapoly(this, play, &gCrashboxCol, DPM_PLAYER);
    this->dyna.actor.velocity.x = this->dyna.actor.velocity.y = this->dyna.actor.velocity.z = 0.0f;
    this->dyna.actor.gravity = -1.0f;

    switch (this->dyna.actor.params) {
        case DK_CRASHBOX_SMALL:
            Actor_SetScale(&this->dyna.actor, 0.5f);
            break;
        case DK_CRASHBOX_LARGE:
            Actor_SetScale(&this->dyna.actor, 1.0f);
            break;
    }

    this->dyna.actor.colChkInfo.mass = MASS_IMMOVABLE;
    this->actionFunc = Crashbox_Wait;
}

void Crashbox_Destroy(Actor* thisx, PlayState* play) {
    DungeonKeep* this = THIS;

    Collider_DestroyCylinder(play, &this->collider);
}

static Vec3f posOffsets[] = {
    { 60.0f, 0.0f, 0.0f },   { -60.0f, 0.0f, 0.0f },   { 0.0f, 0.0f, 60.0f },   { 0.0f, 0.0f, -60.0f },
    { 60.0f, 60.0f, 0.0f },  { -60.0f, 60.0f, 0.0f },  { 0.0f, 60.0f, 60.0f },  { 0.0f, 60.0f, -60.0f },
    { 60.0f, 120.0f, 0.0f }, { -60.0f, 120.0f, 0.0f }, { 0.0f, 120.0f, 60.0f }, { 0.0f, 120.0f, -60.0f },
};

void Crashbox_Break(DungeonKeep* this, PlayState* play) {
    s32 i;
    s32 j;
    Vec3f velocity;
    Vec3f pos;
    s16 arg5;
    Actor* thisx = &this->dyna.actor;
    f32 sin = Math_SinS(thisx->shape.rot.y);
    f32 cos = Math_CosS(thisx->shape.rot.y);
    f32 tmp1;
    f32 tmp2;
    s16 arg9;
    f32 scale;

    for (i = 0; i < 5; i++) {
        pos.y = (24 * i) + thisx->world.pos.y;
        for (j = 0; j < 5; j++) {
            tmp1 = 28 * (j - 2);

            pos.x = (tmp1 * cos) + thisx->world.pos.x;
            pos.z = -(tmp1 * sin) + thisx->world.pos.z;

            tmp1 = 6.0f * Rand_ZeroOne() * (j - 2);
            tmp2 = 6.0f * Rand_ZeroOne();

            velocity.x = (tmp2 * sin) + (tmp1 * cos);
            velocity.y = 34.0f * Rand_ZeroOne();
            velocity.z = (tmp2 * cos) - (tmp1 * sin);

            arg9 = ((Rand_ZeroOne() - 0.5f) * 14.0f * 1.6f) + 14.0f;
            arg9 *= 0.75f;
            arg5 = (arg9 > 20) ? 32 : 64;

            if (Rand_ZeroOne() < 5.0f) {
                arg5 |= 1;
            }

            EffectSsKakera_Spawn(play, &pos, &velocity, &thisx->world.pos, -650, arg5, 20, 20, 0, arg9, 2, 32, 100,
                                 KAKERA_COLOR_NONE, OBJECT_GAMEPLAY_DANGEON_KEEP, gBrownFragmentDL);
        }
    }

    for (i = 0; i < ARRAY_COUNT(posOffsets); i++) {
        Vec3f dustPos = this->dyna.actor.world.pos;
        Vec3f dustVel = { 0.0f, -4.0f, 0.0f };
        Vec3f dustAccel = { 0.0f, -0.1f, 0.0f };

        dustPos.x += posOffsets[i].x + (Rand_ZeroOne() * 10.0f);
        dustPos.y += posOffsets[i].y + (Rand_ZeroOne() * 10.0f);
        dustPos.z += posOffsets[i].z + (Rand_ZeroOne() * 10.0f);

        scale = (s16)((Rand_ZeroOne() * 1000) * 0.2f) + 1000;

        func_800287AC(play, &dustPos, &dustVel, &dustAccel, scale, 20, 100);
    }
}

void Crashbox_Wait(DungeonKeep* this, PlayState* play) {
    if (((this->collider.base.acFlags & AC_HIT) && (this->collider.info.acHitInfo->toucher.dmgFlags & DMG_EXPLOSIVE) &&
         (this->dyna.actor.bgCheckFlags & 1))) {
        Crashbox_Break(this, play);
        Audio_PlaySoundAtPosition(play, &this->dyna.actor.world.pos, 80, NA_SE_EV_WOODBOX_BREAK);
        Actor_Kill(&this->dyna.actor);
    } else {
        this->collider.base.acFlags &= ~AC_HIT;
    }
}

void Crashbox_Update(Actor* thisx, PlayState* play) {
    DungeonKeep* this = THIS;

    if (this->timer > 0) {
        this->timer--;
    }

    if (this->actionFunc != NULL) {
        this->actionFunc(this, play);
    }

    CollisionCheck_SetAC(play, &play->colChkCtx, &this->collider.base);
    Actor_MoveForwardXZ(&this->dyna.actor);
    Actor_UpdateBgCheckInfo(play, &this->dyna.actor, 7.5f, 35.0f, 0.0f, 0xC5);
    Collider_UpdateCylinder(&this->dyna.actor, &this->collider);

    if ((this->timer == 0) && (this->dyna.actor.bgCheckFlags & 2)) {
        Audio_PlayActorSound2(&this->dyna.actor, NA_SE_EV_BLOCK_BOUND);
    }
}

void Crashbox_Draw(Actor* thisx, PlayState* play) {
    /* SW97: gCrashboxDL not available (no sw97.otr), skip draw */
}

/* ========================================================================
 * Beta Floor Switch (beta_floor_switch.c)
 * ======================================================================== */

#define SCALE_UP 18.0f / 200.0f
#define SCALE_DOWN 10.0f / 2000.0f
#define SCALE_MOVE_DOWN 30.0f / 2000.0f
#define SCALE_MOVE_UP 50.0f / 2000.0f

void BetaFloorSwitch_Destroy(Actor* thisx, PlayState* play);
void BetaFloorSwitch_Update(Actor* thisx, PlayState* play);
void BetaFloorSwitch_Draw(Actor* thisx, PlayState* play);

void BetaFloorSwitch_Wait(DungeonKeep* this, PlayState* play);
void BetaFloorSwitch_Press(DungeonKeep* this, PlayState* play);
void BetaFloorSwitch_Pressed(DungeonKeep* this, PlayState* play);
void BetaFloorSwitch_Rise(DungeonKeep* this, PlayState* play);

void BetaFloorSwitch_InitDynapoly(DungeonKeep* this, PlayState* play, CollisionHeader* collision,
                                  DynaPolyMoveFlag moveFlag) {
    s32 pad;
    CollisionHeader* colHeader = NULL;
    s32 pad2;

    DynaPolyActor_Init(&this->dyna, moveFlag);
    CollisionHeader_GetVirtual(collision, &colHeader);
    this->dyna.bgId = DynaPoly_SetBgActor(play, &play->colCtx.dyna, &this->dyna.actor, colHeader);

    if (this->dyna.bgId == BG_ACTOR_MAX) {
        // Warning : move BG registration failure
        osSyncPrintf("Warning : move BG 登録失敗(%s %d)(name %d)(arg_data 0x%04x)\n", "../z_obj_switch.c", 531,
                     this->dyna.actor.id, this->dyna.actor.params);
    }
}

void BetaFloorSwitch_SetupWait(DungeonKeep* this) {
    this->dyna.actor.scale.y = SCALE_UP;
    this->actionFunc = BetaFloorSwitch_Wait;
}

void BetaFloorSwitch_SetupPress(DungeonKeep* this) {
    this->actionFunc = BetaFloorSwitch_Press;
}

void BetaFloorSwitch_SetupPressed(DungeonKeep* this) {
    this->dyna.actor.scale.y = SCALE_DOWN;
    this->actionFunc = BetaFloorSwitch_Pressed;
}

void BetaFloorSwitch_Wait(DungeonKeep* this, PlayState* play) {
    if (func_8004356C(&this->dyna)) {
        Flags_SetSwitch(play, (this->dyna.actor.params >> 8 & 0xFF));
        OnePointCutscene_AttentionSetSfx(play, &this->dyna.actor, NA_SE_SY_CORRECT_CHIME);
        BetaFloorSwitch_SetupPress(this);
    }
}

void BetaFloorSwitch_Press(DungeonKeep* this, PlayState* play) {
    this->dyna.actor.scale.y -= SCALE_MOVE_DOWN;

    if (this->dyna.actor.scale.y <= SCALE_DOWN) {
        BetaFloorSwitch_SetupPressed(this);
        Audio_PlayActorSound2(&this->dyna.actor, NA_SE_EV_FOOT_SWITCH);
        func_800AA000(this->dyna.actor.xyzDistToPlayerSq, 120, 20, 10);
    }
}

void BetaFloorSwitch_Pressed(DungeonKeep* this, PlayState* play) {
    if (!Flags_GetSwitch(play, (this->dyna.actor.params >> 8 & 0xFF))) {
        this->actionFunc = BetaFloorSwitch_Rise;
    }
}

void BetaFloorSwitch_Rise(DungeonKeep* this, PlayState* play) {
    this->dyna.actor.scale.y += SCALE_MOVE_UP;

    if (this->dyna.actor.scale.y >= SCALE_UP) {
        BetaFloorSwitch_SetupWait(this);
        Audio_PlayActorSound2(&this->dyna.actor, NA_SE_EV_FOOT_SWITCH);
        func_800AA000(this->dyna.actor.xyzDistToPlayerSq, 120, 20, 10);
    }
}

void BetaFloorSwitch_Init(DungeonKeep* this, PlayState* play) {
    this->dyna.actor.destroy = BetaFloorSwitch_Destroy;
    this->dyna.actor.update = BetaFloorSwitch_Update;
    this->dyna.actor.draw = BetaFloorSwitch_Draw;

    Actor_SetScale(&this->dyna.actor, 0.1f);
    /* SW97: gBetaFloorSwitchCol not available (no sw97.otr), skip dynapoly init */
    // BetaFloorSwitch_InitDynapoly(this, play, &gBetaFloorSwitchCol, DPM_PLAYER);

    this->dyna.actor.world.pos.y = this->dyna.actor.home.pos.y + 1.0f;
    this->dyna.actor.colChkInfo.mass = MASS_IMMOVABLE;

    if (Flags_GetSwitch(play, (this->dyna.actor.params >> 8 & 0xFF))) {
        BetaFloorSwitch_SetupPressed(this);
    } else {
        BetaFloorSwitch_SetupWait(this);
    }
}

void BetaFloorSwitch_Destroy(Actor* thisx, PlayState* play) {
    DungeonKeep* this = THIS;

    DynaPoly_DeleteBgActor(play, &play->colCtx.dyna, this->dyna.bgId);
}

void BetaFloorSwitch_Update(Actor* thisx, PlayState* play) {
    DungeonKeep* this = THIS;

    if (this->actionFunc != NULL) {
        this->actionFunc(this, play);
    }
}

void BetaFloorSwitch_Draw(Actor* thisx, PlayState* play) {
    /* SW97: gBetaFloorSwitchDL not available (no sw97.otr), skip draw */
}

/* ========================================================================
 * Flame Thrower (flame_thrower.c)
 * ======================================================================== */

void FlameThrower_PreUpdate(Actor* thisx, PlayState* play);

static ColliderJntSphElementInit sJntSphElementsInit[] = {
    {
        {
            ELEMTYPE_UNK0,
            { 0x20000000, 0x01, 0x04 },
            { 0x00000000, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NONE,
            BUMP_NONE,
            OCELEM_NONE,
        },
        { 1, { { 0, 30, 40 }, 25 }, 100 },
    },
    {
        {
            ELEMTYPE_UNK0,
            { 0x20000000, 0x01, 0x04 },
            { 0x00000000, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NONE,
            BUMP_NONE,
            OCELEM_NONE,
        },
        { 1, { { 0, 32, 77 }, 32 }, 100 },
    },
    {
        {
            ELEMTYPE_UNK0,
            { 0x20000000, 0x01, 0x04 },
            { 0x00000000, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NONE,
            BUMP_NONE,
            OCELEM_NONE,
        },
        { 1, { { 0, 35, 130 }, 42 }, 100 },
    },
    {
        {
            ELEMTYPE_UNK0,
            { 0x20000000, 0x01, 0x04 },
            { 0x00000000, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NONE,
            BUMP_NONE,
            OCELEM_NONE,
        },
        { 1, { { -0, 35, 181 }, 52 }, 100 },
    },
    {
        {
            ELEMTYPE_UNK0,
            { 0x20000000, 0x01, 0x04 },
            { 0x00000000, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NONE,
            BUMP_NONE,
            OCELEM_NONE,
        },
        { 1, { { 0, 35, 235 }, 62 }, 100 },
    },
};

static ColliderJntSphInit sJntSphInit = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_ENEMY,
        AC_NONE,
        OC1_NONE,
        OC2_TYPE_2,
        COLSHAPE_JNTSPH,
    },
    ARRAY_COUNT(sJntSphElementsInit),
    sJntSphElementsInit,
};

static InitChainEntry sDungeonInitChain[] = {
    ICHAIN_VEC3F_DIV1000(scale, 100, ICHAIN_CONTINUE),
    ICHAIN_F32(uncullZoneScale, 400, ICHAIN_CONTINUE),
    ICHAIN_F32(uncullZoneForward, 1500, ICHAIN_STOP),
};

static u64* sFireballsTexs[] = {
    gFireTempleFireball0Tex, gFireTempleFireball1Tex, gFireTempleFireball2Tex, gFireTempleFireball3Tex,
    gFireTempleFireball4Tex, gFireTempleFireball5Tex, gFireTempleFireball6Tex, gFireTempleFireball7Tex,
};

void FlameThrower_Init(Actor* thisx, PlayState* play) {
    DungeonKeep* this = THIS;
    s32 i;

    // Set the main functions
    this->dyna.actor.destroy = FlameThrower_Destroy;

    // Set the object to not be gameplay_dangeon_keep anymore
    this->bankIndex = Object_GetIndex(&play->objectCtx, OBJECT_HIDAN_OBJECTS);
    if (this->bankIndex < 0) {
        Actor_Kill(&this->dyna.actor);
    } else {
        this->dyna.actor.update = FlameThrower_PreUpdate;
    }

    Actor_ProcessInitChain(&this->dyna.actor, sDungeonInitChain);
    Collider_InitJntSph(play, &this->colliderSph);
    Collider_SetJntSph(play, &this->colliderSph, &this->dyna.actor, &sJntSphInit, this->colliderItems);

    this->dyna.actor.flags = 0x00000030;

    for (i = 0; i < ARRAY_COUNT(this->colliderItems); i++) {
        this->colliderSph.elements[i].dim.worldSphere.radius = this->colliderSph.elements[i].dim.modelSphere.radius;
    }

    this->burnFrame = 0;

    this->timer = (this->dyna.actor.world.rot.y < 0x7FFF) ? 0 : FLAME_DURATION;
    this->timer2 = 0;
}

void FlameThrower_Destroy(Actor* thisx, PlayState* play) {
    DungeonKeep* this = THIS;

    Collider_DestroyJntSph(play, &this->colliderSph);
}

void FlameThrower_PreUpdate(Actor* thisx, PlayState* play) {
    DungeonKeep* this = THIS;

    if (Object_IsLoaded(&play->objectCtx, this->bankIndex)) {
        this->dyna.actor.objBankIndex = this->bankIndex;
        Actor_SetObjectDependency(play, &this->dyna.actor);
        this->dyna.actor.update = FlameThrower_Update;
        this->dyna.actor.draw = FlameThrower_Draw;
    }
}

void FlameThrower_Update(Actor* thisx, PlayState* play) {
    DungeonKeep* this = THIS;
    s32 i;
    ColliderJntSphElement* sphere;
    s32 pad;
    f32 yawSine;
    f32 yawCosine;

    // if the player is below the actor (in the lower room) dont update
    if (this->timer == 0) {
        // Is currently flaming
        this->timer2++;

        if (this->timer2 >= FLAME_DURATION) {
            this->timer = FLAME_DURATION;
            this->timer2 = 0;
        }
    } else {
        // Is currently waiting
        this->timer--;
    }

    this->burnFrame = (this->burnFrame + 1) % 8;

    yawSine = Math_SinS(this->dyna.actor.shape.rot.y);
    yawCosine = Math_CosS(this->dyna.actor.shape.rot.y);

    for (i = 0; i < ARRAY_COUNT(this->colliderItems); i++) {
        sphere = &this->colliderSph.elements[i];
        sphere->dim.worldSphere.center.x = this->dyna.actor.home.pos.x + yawCosine * sphere->dim.modelSphere.center.x +
                                           yawSine * sphere->dim.modelSphere.center.z;
        sphere->dim.worldSphere.center.y = (s16)this->dyna.actor.home.pos.y + sphere->dim.modelSphere.center.y;
        sphere->dim.worldSphere.center.z = (this->dyna.actor.home.pos.z - yawSine * sphere->dim.modelSphere.center.x) +
                                           yawCosine * sphere->dim.modelSphere.center.z;
    }

    // Set the current amount of spheres to use
    this->colliderSph.count = (this->timer > FLAME_COUNT) ? 0 : FLAME_COUNT - this->timer;
    if ((this->timer == 0) && (this->timer2 >= (FLAME_DURATION - FLAME_COUNT))) {
        this->colliderSph.count = FLAME_DURATION - this->timer2;
    }

    if (this->timer == 0) {
        Player* player = GET_PLAYER(play);

        if (player->actor.world.pos.y >= this->dyna.actor.world.pos.y - 300.0f) {
            // Is currently flaming and player is on this floor
            CollisionCheck_SetAT(play, &play->colChkCtx, &this->colliderSph.base);
            func_8002F974(&this->dyna.actor, NA_SE_EV_FIRE_PILLAR - SFX_FLAG);
        }
    }
}

Gfx* FlameThrower_DrawFireball(PlayState* play, DungeonKeep* this, s16 frame, MtxF* mf, s32 a, Gfx* displayList) {
    s32 index = (((this->burnFrame + frame) % 8) * 7) * (1.0f / 7.0f);

    gSPSegment(displayList++, 0x09, SEGMENTED_TO_VIRTUAL(sFireballsTexs[index]));

    frame++;

    gDPSetPrimColor(displayList++, 0, 1, 255, 255, 0, 150);
    gDPSetEnvColor(displayList++, 255, 0, 0, 255);

    mf->xx = mf->yy = mf->zz = (0.7f * frame) + 0.5f;
    mf->wx = this->dyna.actor.world.pos.x + ((Math_CosS(this->dyna.actor.shape.rot.x) * (FLAME_DIST_FACTOR * frame)) *
                                             (Math_SinS(this->dyna.actor.shape.rot.y)));
    mf->wy = (this->dyna.actor.world.pos.y + Y_OFFSET) + ((7.0f / 10.0f) * frame);
    mf->wz = this->dyna.actor.world.pos.z + ((Math_CosS(this->dyna.actor.shape.rot.x) * (FLAME_DIST_FACTOR * frame)) *
                                             (Math_CosS(this->dyna.actor.shape.rot.y)));

    gSPMatrix(displayList++,
              Matrix_MtxFToMtx(Matrix_CheckFloats(mf, "", 0), GRAPH_ALLOC(play->state.gfxCtx, sizeof(Mtx))),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(displayList++, gFireTempleFireballDL);

    return displayList;
}

void FlameThrower_Draw(Actor* thisx, PlayState* play) {
    DungeonKeep* this = THIS;
    s32 i;
    MtxF mf;

    i = (this->timer > FLAME_COUNT) ? 0 : FLAME_COUNT - this->timer;

    if ((this->timer == 0) && (this->timer2 >= (FLAME_DURATION - FLAME_COUNT))) {
        i = FLAME_DURATION - this->timer2;
    }

    OPEN_DISPS(play->state.gfxCtx);

    func_80093D18(play->state.gfxCtx);

    gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, "", 0), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    Matrix_MtxFCopy(&mf, &gMtxFClear);

    POLY_XLU_DISP = Gfx_CallSetupDL(POLY_XLU_DISP, 0x14);

    if (i > 0) {
        for (; i >= 0; i--) {
            POLY_XLU_DISP = FlameThrower_DrawFireball(play, this, i, &mf, 1, POLY_XLU_DISP);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

/* ========================================================================
 * Floater (floater.c)
 * ======================================================================== */

#define FLOATER_HEIGHT (f32)(this->dyna.actor.params >> 8 & 0xFF)

void Floater_Destroy(Actor* thisx, PlayState* play);
void Floater_Update(Actor* thisx, PlayState* play);
void Floater_Draw(Actor* thisx, PlayState* play);

void Floater_WaitDown(DungeonKeep* this, PlayState* play);
void Floater_Rise(DungeonKeep* this, PlayState* play);
void Floater_WaitUp(DungeonKeep* this, PlayState* play);

void Floater_InitDynapoly(DungeonKeep* this, PlayState* play, CollisionHeader* collision, DynaPolyMoveFlag moveFlag) {
    s32 pad;
    CollisionHeader* colHeader = NULL;
    s32 pad2;

    DynaPolyActor_Init(&this->dyna, moveFlag);
    CollisionHeader_GetVirtual(collision, &colHeader);
    this->dyna.bgId = DynaPoly_SetBgActor(play, &play->colCtx.dyna, &this->dyna.actor, colHeader);

    if (this->dyna.bgId == BG_ACTOR_MAX) {
        // Warning : move BG registration failure
        osSyncPrintf("Warning : move BG 登録失敗(%s %d)(name %d)(arg_data 0x%04x)\n", "../z_obj_switch.c", 531,
                     this->dyna.actor.id, this->dyna.actor.params);
    }
}

void Floater_Init(DungeonKeep* this, PlayState* play) {
    CollisionHeader* colHeader = NULL;

    this->dyna.actor.destroy = Floater_Destroy;
    this->dyna.actor.update = Floater_Update;
    this->dyna.actor.draw = Floater_Draw;

    /* SW97: gDLiftCol not available (no sw97.otr), skip dynapoly init */
    // Floater_InitDynapoly(this, play, &gDLiftCol, DPM_PLAYER | DPM_ENEMY);
    Actor_SetScale(&this->dyna.actor, 0.1f);
    if (DK_TYPE(this->dyna.actor.params) == DK_FLOATER_BIG) {
        this->dyna.actor.scale.y = 0.2f;
    }
    this->dyna.actor.colChkInfo.mass = MASS_IMMOVABLE;
    this->actionFunc = Floater_WaitDown;
}

void Floater_Fall(DungeonKeep* this, PlayState* play) {
    if (this->timer == 0) {
        if (this->dyna.actor.world.pos.y > this->dyna.actor.home.pos.y) {
            Audio_PlayActorSound2(&this->dyna.actor, NA_SE_EV_ELEVATOR_MOVE - SFX_FLAG);
            this->dyna.actor.velocity.y = -3.0f;
        } else if (this->dyna.actor.world.pos.y <= this->dyna.actor.home.pos.y) {
            this->dyna.actor.velocity.y = 0.0f;
            Audio_PlayActorSound2(&this->dyna.actor, NA_SE_EV_ELEVATOR_STOP);
            this->actionFunc = Floater_WaitDown;
        }
    }
}

void Floater_WaitUp(DungeonKeep* this, PlayState* play) {
    if (!(this->dyna.interactFlags & 2)) {
        this->timer = 12;
        this->actionFunc = Floater_Fall;
    }
}

void Floater_Rise(DungeonKeep* this, PlayState* play) {
    f32 top = this->dyna.actor.home.pos.y + FLOATER_HEIGHT;

    if (!(this->dyna.interactFlags & 2)) {
        this->actionFunc = Floater_Fall;
        this->dyna.actor.velocity.y = 0.0f;
        this->timer = 3;
    }

    if (this->timer == 0) {
        if (this->dyna.actor.world.pos.y < top) {
            Audio_PlayActorSound2(&this->dyna.actor, NA_SE_EV_ELEVATOR_MOVE - SFX_FLAG);
            this->dyna.actor.velocity.y = 3.0f;
        } else if (this->dyna.actor.world.pos.y >= top) {
            this->dyna.actor.velocity.y = 0.0f;
            this->actionFunc = Floater_WaitUp;
            Audio_PlayActorSound2(&this->dyna.actor, NA_SE_EV_ELEVATOR_STOP);
        }
    }
}

void Floater_WaitDown(DungeonKeep* this, PlayState* play) {
    if ((this->dyna.interactFlags & 2)) {
        this->actionFunc = Floater_Rise;
        this->timer = 12;
    }
}

void Floater_Destroy(Actor* thisx, PlayState* play) {
}

void Floater_Update(Actor* thisx, PlayState* play) {
    DungeonKeep* this = THIS;

    if (this->timer > 0) {
        this->timer--;
    }

    if (this->actionFunc != NULL) {
        this->actionFunc(this, play);
    }

    Actor_MoveForwardXZ(&this->dyna.actor);
}

void Floater_Draw(Actor* thisx, PlayState* play) {
    /* SW97: gDLiftBigDL / gDLiftDL not available (no sw97.otr), skip draw */
}

/* ========================================================================
 * DungeonKeep Dispatcher (dungeon_keep_actors.c)
 * ======================================================================== */

#define FLAGS 0x00000030

void DungeonKeep_Init(Actor* thisx, PlayState* play);
void DungeonKeep_Destroy(Actor* thisx, PlayState* play);
void DungeonKeep_Update(Actor* thisx, PlayState* play);

/* NOTE: ActorInit struct removed — actor ID assigned at runtime via gSw97ActorId_DungeonKeep */

void DungeonKeep_Init(Actor* thisx, PlayState* play) {
    DungeonKeep* this = THIS;

    switch (DK_TYPE(this->dyna.actor.params)) {
        case DK_CRASHBOX_SMALL:
        case DK_CRASHBOX_LARGE:
            Crashbox_Init(this, play);
            break;

        case DK_ROLLING_BOULDER:
            Actor_Spawn(&play->actorCtx, play, ACTOR_EN_GOROIWA, thisx->world.pos.x, thisx->world.pos.y,
                        thisx->world.pos.z, thisx->world.rot.x, thisx->world.rot.y, thisx->world.rot.z, 0x0000, false);
            Actor_Kill(thisx);
            break;

        case DK_FLOOR_SWITCH:
        case DK_FLOOR_SWITCH_DEKU_TIMER:
            BetaFloorSwitch_Init(this, play);
            break;

        case DK_FLAME_THROWER:
            FlameThrower_Init(&this->dyna.actor, play);
            break;

        case DK_CRYSTAL_SWITCH_18:
            Actor_Spawn(&play->actorCtx, play, ACTOR_OBJ_SWITCH, thisx->world.pos.x, thisx->world.pos.y,
                        thisx->world.pos.z, thisx->world.rot.x, thisx->world.rot.y, thisx->world.rot.z, 0x3F13, false);
            Actor_Kill(thisx);
            break;

        case DK_CRYSTAL_SWITCH:
            Actor_Spawn(&play->actorCtx, play, ACTOR_OBJ_SWITCH, thisx->world.pos.x, thisx->world.pos.y,
                        thisx->world.pos.z, thisx->world.rot.x, thisx->world.rot.y, thisx->world.rot.z, 0x3F03, false);
            Actor_Kill(thisx);
            break;

        case DK_EYE_SWITCH:
            Actor_Spawn(&play->actorCtx, play, ACTOR_OBJ_SWITCH, thisx->world.pos.x, thisx->world.pos.y,
                        thisx->world.pos.z, thisx->world.rot.x, thisx->world.rot.y, thisx->world.rot.z,
                        (thisx->params & 0x3F00) | 0x0002, false);
            Actor_Kill(thisx);
            break;

        case DK_EYE_SWITCH_1B:
            Actor_Spawn(&play->actorCtx, play, ACTOR_OBJ_SWITCH, thisx->world.pos.x, thisx->world.pos.y,
                        thisx->world.pos.z, thisx->world.rot.x, thisx->world.rot.y, thisx->world.rot.z,
                        (thisx->params & 0x3F00) | 0x0012, false);
            Actor_Kill(thisx);
            break;

        case DK_EYE_SWITCH_1E:
            Actor_Spawn(&play->actorCtx, play, ACTOR_OBJ_SWITCH, thisx->world.pos.x, thisx->world.pos.y,
                        thisx->world.pos.z, thisx->world.rot.x, thisx->world.rot.y, thisx->world.rot.z,
                        (thisx->params & 0x3F00) | 0x0082, false);
            Actor_Kill(thisx);
            break;

        case DK_EYE_SWITCH_1F:
            Actor_Spawn(&play->actorCtx, play, ACTOR_OBJ_SWITCH, thisx->world.pos.x, thisx->world.pos.y,
                        thisx->world.pos.z, thisx->world.rot.x, thisx->world.rot.y, thisx->world.rot.z,
                        (thisx->params & 0x3F00) | 0x0092, false);
            Actor_Kill(thisx);
            break;

        case DK_PUSH_BLOCK:
            Actor_Spawn(&play->actorCtx, play, ACTOR_OBJ_OSHIHIKI, thisx->world.pos.x, thisx->world.pos.y,
                        thisx->world.pos.z, thisx->world.rot.x, thisx->world.rot.y, thisx->world.rot.z, 0xFF02, false);
            break;

        case DK_PUSH_BLOCK_SMALL:
            Actor_Spawn(&play->actorCtx, play, ACTOR_OBJ_OSHIHIKI, thisx->world.pos.x, thisx->world.pos.y,
                        thisx->world.pos.z, thisx->world.rot.x, thisx->world.rot.y, thisx->world.rot.z, 0xFF00, false);
            break;

        case DK_FLOATER:
        case DK_FLOATER_BIG:
            Floater_Init(this, play);
            break;

        default:
            /* SW97: ACTOR_GREEN_CUBE is a SW97-specific actor, not available in OOT. Just kill. */
            Actor_Kill(thisx);
            break;
    }
}

void DungeonKeep_Destroy(Actor* thisx, PlayState* play) {
}

void DungeonKeep_Update(Actor* thisx, PlayState* play) {
}

void DK_DrawDebugText(DungeonKeep* this, PlayState* play, Gfx** buf) {
    GfxPrint* printer = alloca(sizeof(GfxPrint));

    GfxPrint_Init(printer);
    GfxPrint_Open(printer, *buf);
    GfxPrint_SetColor(printer, 255, 255, 255, 255);

    // add messages here
    // GfxPrint_SetPos(printer, 3, 20);
    // GfxPrint_Printf(printer, "init:%08X", DungeonKeep_Init);

    // close
    *buf = GfxPrint_Close(printer);
    GfxPrint_Destroy(printer);
}
