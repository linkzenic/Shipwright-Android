/**
 * Somaria Cubes - Elegy statues with hookshot + switchhook support
 * Uses MM Elegy of Emptiness shell models from mm.o2r
 * Uses actor hijacking on En_Lightbox
 *
 * Behavior is identical to the original colored cubes:
 * pick up, throw, wall bounce, switch pressing, hookshot, switchhook.
 * Only the visuals changed to elegy shell DLs.
 */

#include "somaria_cubes.h"
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "overlays/actors/ovl_En_Lightbox/z_en_lightbox.h"
#include "overlays/actors/ovl_Bg_Bdan_Switch/z_bg_bdan_switch.h"

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

static void SomariaCube_Update(Actor* thisx, PlayState* play);
static void SomariaCube_Draw(Actor* thisx, PlayState* play);
static void SomariaCube_DestroyFunc(Actor* thisx, PlayState* play);

#include "transformation_masks/transformation_masks.h"

static ActorFunc sOriginalDestroy = NULL;
static ActorFunc sSomariaCubeUpdate = SomariaCube_Update;

// ============================================================================
// ELEGY SHELL DISPLAY LISTS (indexed by form)
// ============================================================================

static const char* sShellDLists[ELEGY_FORM_MAX] = {
    gElegyShellHumanDL, // ELEGY_FORM_HUMAN
    gElegyShellGoronDL, // ELEGY_FORM_GORON
    gElegyShellZoraDL,  // ELEGY_FORM_ZORA
    gElegyShellDekuDL,  // ELEGY_FORM_DEKU
    gElegyShellHumanDL, // ELEGY_FORM_FD (Fierce Deity uses human shell)
};

// MM's EnTorch2_Draw calls Scene_SetRenderModeXlu(play, 0, 0x01) which sets
// segment 0x0C to sRenderModeSetNoneDL (all gsSPEndDisplayList). The elegy DLs
// reference 0x0C at offsets 0x00 and 0x10, so those calls become no-ops.
// The DLs have their own gsSPLoadGeometryMode calls for culling.
// Setting actual cull modes here would ADD cull bits on top of existing ones,
// making both CULL_BACK+CULL_FRONT active = everything invisible.
static Gfx sSegment0xC_Noop[] = {
    gsSPEndDisplayList(), // offset 0x00 (called by 0x0C000000)
    gsSPEndDisplayList(), // offset 0x08
    gsSPEndDisplayList(), // offset 0x10 (called by 0x0C000010)
    gsSPEndDisplayList(), // offset 0x18
};

// ============================================================================
// COLLIDER (AC for hookshot only - NO AT, NO OC)
// ============================================================================

static ColliderCylinderInit sColliderInit = {
    {
        COLTYPE_NONE,
        AT_NONE,
        AC_ON | AC_TYPE_PLAYER,
        OC1_NONE,
        OC2_NONE,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK0,
        { 0x00000000, 0x00, 0x00 },
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_NONE,
        BUMP_ON | BUMP_HOOKABLE,
        OCELEM_NONE,
    },
    { SOMARIA_CYL_RADIUS, SOMARIA_CYL_HEIGHT, 0, { 0, 0, 0 } },
};

// ============================================================================
// STATIC COLLIDER POOL (max colliders for local + remote cubes)
// Using static pool because Actor_Spawn only allocates sizeof(EnLightbox),
// so we CANNOT add extra fields to the actor struct!
// ============================================================================

typedef struct {
    ColliderCylinder collider;
    Actor* owner; // Which cube owns this slot (NULL = free)
    u8 initialized;
} ColliderSlot;

static ColliderSlot sColliderPool[SOMARIA_MAX_COLLIDERS] = { 0 };

static s8 SomariaCube_GetColliderSlot(Actor* actor) {
    for (s8 i = 0; i < SOMARIA_MAX_COLLIDERS; i++) {
        if (sColliderPool[i].owner == actor)
            return i;
    }
    return -1;
}

static s8 SomariaCube_AllocCollider(PlayState* play, Actor* actor) {
    for (s8 i = 0; i < SOMARIA_MAX_COLLIDERS; i++) {
        if (sColliderPool[i].owner == NULL) {
            if (!sColliderPool[i].initialized) {
                Collider_InitCylinder(play, &sColliderPool[i].collider);
                sColliderPool[i].initialized = 1;
            }
            Collider_SetCylinder(play, &sColliderPool[i].collider, actor, &sColliderInit);
            sColliderPool[i].owner = actor;
            return i;
        }
    }
    // No free slot — try to reap dead owners. The Free path can be missed
    // if a cube is destroyed via Actor_Kill from another system (scene
    // unload, room transition, etc.) — its slot keeps a dangling owner
    // pointer that pins the slot forever. After enough churn, allocation
    // would silently fail (return -1) and new cubes spawn without
    // collision. Walking the pool to reap any slot whose owner's
    // `update` callback is NULL recovers those dead-owner slots.
    for (s8 i = 0; i < SOMARIA_MAX_COLLIDERS; i++) {
        if (sColliderPool[i].owner != NULL && sColliderPool[i].owner->update == NULL) {
            sColliderPool[i].owner = actor;
            Collider_SetCylinder(play, &sColliderPool[i].collider, actor, &sColliderInit);
            return i;
        }
    }
    return -1;
}

static void SomariaCube_FreeCollider(PlayState* play, Actor* actor) {
    s8 slot = SomariaCube_GetColliderSlot(actor);
    if (slot >= 0) {
        sColliderPool[slot].owner = NULL;
        // Don't destroy - reuse the initialized collider
    }
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

void SomariaCube_PlaySound(Actor* actor, u16 sfxId) {
    Audio_PlaySoundGeneral(sfxId, &actor->projectedPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultReverb);
}

u8 SomariaCube_GetForm(Actor* actor) {
    if (actor == NULL)
        return ELEGY_FORM_HUMAN;
    s16 form = SOMARIA_GET_FORM(actor);
    if (form < 0 || form >= ELEGY_FORM_MAX)
        return ELEGY_FORM_HUMAN;
    return (u8)form;
}

// Check and activate Bg_Bdan_Switch (YELLOW_HEAVY type 0x01) when cube is on top
static void SomariaCube_TryActivateHeavySwitch(Actor* cube, PlayState* play) {
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_SWITCH].head;

    while (actor != NULL) {
        if (actor->id == ACTOR_BG_BDAN_SWITCH) {
            u8 switchType = actor->params & 0xFF;

            // Only affect YELLOW_HEAVY (0x01) switches
            if (switchType == YELLOW_HEAVY) {
                // Check if cube is within horizontal range of switch
                f32 dx = cube->world.pos.x - actor->world.pos.x;
                f32 dz = cube->world.pos.z - actor->world.pos.z;
                f32 distXZ = sqrtf(dx * dx + dz * dz);

                // Check if cube is above switch (within vertical tolerance)
                f32 dy = cube->world.pos.y - actor->world.pos.y;

                // Switch is about 40 units radius, cube needs to be on top
                if (distXZ < 40.0f && dy >= 0.0f && dy < 50.0f) {
                    // Activate the switch flag
                    u8 switchFlag = (actor->params >> 8) & 0x3F;
                    if (!Flags_GetSwitch(play, switchFlag)) {
                        Flags_SetSwitch(play, switchFlag);
                        SomariaCube_PlaySound(cube, NA_SE_EV_FOOT_SWITCH);
                        // Play the chime
                        Audio_PlaySoundGeneral(NA_SE_SY_CORRECT_CHIME, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    }
                }
            }
        }
        actor = actor->next;
    }
}

u8 SomariaCube_IsSomariaCube(Actor* actor) {
    if (actor == NULL || actor->update == NULL)
        return 0;
    return (actor->update == sSomariaCubeUpdate);
}

u8 SomariaCube_IsSwitchable(Actor* actor) {
    // All Somaria cubes are switchhookable
    return SomariaCube_IsSomariaCube(actor);
}

// ============================================================================
// GET CURRENT FORM (based on transformation masks system)
// ============================================================================

static u8 SomariaCube_GetCurrentForm(void) {
    if (!TransformMasks_IsTransformed())
        return ELEGY_FORM_HUMAN;

    // Map MM form enum (FD=0,Goron=1,Zora=2,Deku=3,Human=4)
    // to Elegy form enum (Human=0,Goron=1,Zora=2,Deku=3,FD=4)
    int mmForm = MmPlayer_GetForm();
    switch (mmForm) {
        case 1:
            return ELEGY_FORM_GORON;
        case 2:
            return ELEGY_FORM_ZORA;
        case 3:
            return ELEGY_FORM_DEKU;
        case 0:
            return ELEGY_FORM_FD;
        default:
            return ELEGY_FORM_HUMAN;
    }
}

// ============================================================================
// UPDATE (original behavior: spawn, idle, held, thrown)
// ============================================================================

static void SomariaCube_Update(Actor* thisx, PlayState* play) {
    SomariaCubeState state = SOMARIA_GET_STATE(thisx);
    s16 timer = SOMARIA_GET_TIMER(thisx);
    Player* player = GET_PLAYER(play);

    if (player == NULL)
        return;

    if (timer > 0) {
        SOMARIA_SET_TIMER(thisx, timer - 1);
        timer--;
    }

    switch (state) {
        case SOMARIA_STATE_SPAWN:
            if (thisx->scale.x < SOMARIA_CUBE_SCALE) {
                thisx->scale.x += SOMARIA_CUBE_SCALE / SOMARIA_SPAWN_FRAMES;
                thisx->scale.y = thisx->scale.z = thisx->scale.x;
            }
            if (timer == 0) {
                Actor_SetScale(thisx, SOMARIA_CUBE_SCALE);
                SOMARIA_SET_STATE(thisx, SOMARIA_STATE_IDLE);
            }
            Actor_MoveXZGravity(thisx);
            Actor_UpdateBgCheckInfo(play, thisx, 30.0f, 15.0f, 0.0f, 0x1D);
            break;

        case SOMARIA_STATE_IDLE:
            if (Actor_HasParent(thisx, play)) {
                SOMARIA_SET_STATE(thisx, SOMARIA_STATE_HELD);
                thisx->room = -1;
                break;
            }

            if (thisx->speedXZ > 0.1f && (thisx->bgCheckFlags & BGCHECKFLAG_WALL)) {
                thisx->world.rot.y = thisx->wallYaw;
                SomariaCube_PlaySound(thisx, NA_SE_EV_BOMB_BOUND);
                thisx->speedXZ *= 0.7f;
                thisx->bgCheckFlags &= ~BGCHECKFLAG_WALL;
            }

            if (thisx->bgCheckFlags & BGCHECKFLAG_GROUND) {
                Math_StepToF(&thisx->speedXZ, 0.0f, 1.0f);
                if ((thisx->bgCheckFlags & 0x2) && thisx->velocity.y < -4.0f) {
                    SomariaCube_PlaySound(thisx, NA_SE_EV_BLOCK_BOUND);
                    thisx->velocity.y *= -0.3f;
                }
                // Allow player to pick up the cube
                Actor_OfferCarry(thisx, play);
                // Check if cube is on a heavy weight switch (Bg_Bdan_Switch type 0x01)
                SomariaCube_TryActivateHeavySwitch(thisx, play);
            } else {
                Math_StepToF(&thisx->speedXZ, 0.0f, 0.2f);
            }

            Actor_MoveXZGravity(thisx);
            Actor_UpdateBgCheckInfo(play, thisx, 30.0f, 15.0f, 0.0f, 0x1D);
            break;

        case SOMARIA_STATE_HELD:
            if (Actor_HasNoParent(thisx, play)) {
                SOMARIA_SET_STATE(thisx, SOMARIA_STATE_THROWN);
                thisx->velocity.y = SOMARIA_THROW_VEL_Y;
                thisx->speedXZ = SOMARIA_THROW_SPEED_XZ;
                thisx->world.rot.y = player->actor.shape.rot.y;
            }
            break;

        case SOMARIA_STATE_THROWN:
            if (thisx->bgCheckFlags & BGCHECKFLAG_WALL) {
                thisx->world.rot.y = thisx->wallYaw;
                SomariaCube_PlaySound(thisx, NA_SE_EV_BOMB_BOUND);
                thisx->speedXZ *= 0.5f;
                thisx->bgCheckFlags &= ~BGCHECKFLAG_WALL;
            }

            if (thisx->bgCheckFlags & BGCHECKFLAG_GROUND) {
                SOMARIA_SET_STATE(thisx, SOMARIA_STATE_IDLE);
                SomariaCube_PlaySound(thisx, NA_SE_EV_BLOCK_BOUND);
            }

            Actor_MoveXZGravity(thisx);
            Actor_UpdateBgCheckInfo(play, thisx, 30.0f, 15.0f, 0.0f, 0x1D);
            break;
    }

    thisx->focus.pos = thisx->world.pos;
    thisx->focus.pos.y += 15.0f;

    // Update collider from pool (AC only for hookshot)
    s8 slot = SomariaCube_GetColliderSlot(thisx);
    if (slot >= 0) {
        Collider_UpdateCylinder(thisx, &sColliderPool[slot].collider);
        CollisionCheck_SetAC(play, &play->colChkCtx, &sColliderPool[slot].collider.base);
    }
}

// ============================================================================
// DRAW (MM Elegy Shell DLs — only visual change from original)
// ============================================================================

static void SomariaCube_Draw(Actor* thisx, PlayState* play) {
    if (thisx->scale.x <= 0.001f)
        return;

    u8 form = SomariaCube_GetForm(thisx);
    if (form >= ELEGY_FORM_MAX)
        form = ELEGY_FORM_HUMAN;

    const char* dlPath = sShellDLists[form];

    OPEN_DISPS(play->state.gfxCtx);

    // MM's EnTorch2_Draw sets segment 0x0C to no-op DLists via Scene_SetRenderModeXlu.
    // The elegy DLs call gsSPDisplayList(0x0C000000/0x0C000010) which become no-ops.
    gSPSegment(POLY_OPA_DISP++, 0x0C, sSegment0xC_Noop);

    gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 255);
    Gfx_DrawDListOpa(play, (Gfx*)dlPath);

    CLOSE_DISPS(play->state.gfxCtx);
}

// ============================================================================
// DESTROY
// ============================================================================

static void SomariaCube_DestroyFunc(Actor* thisx, PlayState* play) {
    // Free collider from pool
    SomariaCube_FreeCollider(play, thisx);

    if (sOriginalDestroy != NULL) {
        sOriginalDestroy(thisx, play);
    }
}

// ============================================================================
// SPAWN
// ============================================================================

Actor* SomariaCube_Spawn(PlayState* play, Vec3f* pos, s16 yaw) {
    Actor* cube = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_LIGHTBOX, pos->x, pos->y, pos->z, 0, yaw, 0, 0);

    if (cube == NULL)
        return NULL;

    EnLightbox* lightbox = (EnLightbox*)cube;

    if (sOriginalDestroy == NULL) {
        sOriginalDestroy = cube->destroy;
    }

    // Hijack actor functions
    cube->update = SomariaCube_Update;
    cube->draw = SomariaCube_Draw;
    cube->destroy = SomariaCube_DestroyFunc;

    // Remove En_Lightbox's DynaPoly
    if (lightbox->dyna.bgId != BGACTOR_NEG_ONE) {
        DynaPoly_DeleteBgActor(play, &play->colCtx.dyna, lightbox->dyna.bgId);
        lightbox->dyna.bgId = BGACTOR_NEG_ONE;
    }

    // Allocate collider from static pool (AC for hookshot)
    SomariaCube_AllocCollider(play, cube);

    // Physics
    cube->gravity = SOMARIA_GRAVITY;
    cube->minVelocityY = SOMARIA_MIN_VEL_Y;

    // Flags
    cube->flags |= ACTOR_FLAG_CAN_PRESS_SWITCHES;
    cube->flags |= ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER;
    cube->flags |= ACTOR_FLAG_SWITCHHOOKABLE;

    // No shadow
    cube->shape.shadowDraw = NULL;
    cube->shape.shadowScale = 0.0f;

    // Set form based on current transformation
    u8 form = SomariaCube_GetCurrentForm();
    SOMARIA_SET_FORM(cube, form);

    // Spawn animation (scale from 0 to SOMARIA_CUBE_SCALE)
    Actor_SetScale(cube, 0.0f);
    SOMARIA_SET_STATE(cube, SOMARIA_STATE_SPAWN);
    SOMARIA_SET_TIMER(cube, SOMARIA_SPAWN_FRAMES);

    // Sound
    SomariaCube_PlaySound(cube, NA_SE_PL_MAGIC_FIRE);

    return cube;
}

// ============================================================================
// REMOTE CUBE SUPPORT (Harpoon multiplayer)
// ============================================================================

static void SomariaCube_UpdateRemote(Actor* thisx, PlayState* play) {
    thisx->focus.pos = thisx->world.pos;
    thisx->focus.pos.y += 30.0f;

    s8 slot = SomariaCube_GetColliderSlot(thisx);
    if (slot >= 0) {
        Collider_UpdateCylinder(thisx, &sColliderPool[slot].collider);
        CollisionCheck_SetAC(play, &play->colChkCtx, &sColliderPool[slot].collider.base);
    }
}

static ActorFunc sSomariaCubeUpdateRemote = SomariaCube_UpdateRemote;

u8 SomariaCube_IsRemoteCube(Actor* actor) {
    if (actor == NULL || actor->update == NULL)
        return 0;
    return (actor->update == sSomariaCubeUpdateRemote);
}

Actor* SomariaCube_SpawnRemote(PlayState* play, Vec3f* pos, s16 yaw, u8 form) {
    Actor* cube = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_LIGHTBOX, pos->x, pos->y, pos->z, 0, yaw, 0, 0);
    if (cube == NULL)
        return NULL;

    EnLightbox* lightbox = (EnLightbox*)cube;

    if (sOriginalDestroy == NULL) {
        sOriginalDestroy = cube->destroy;
    }

    cube->update = SomariaCube_UpdateRemote;
    cube->draw = SomariaCube_Draw;
    cube->destroy = SomariaCube_DestroyFunc;

    if (lightbox->dyna.bgId != BGACTOR_NEG_ONE) {
        DynaPoly_DeleteBgActor(play, &play->colCtx.dyna, lightbox->dyna.bgId);
        lightbox->dyna.bgId = BGACTOR_NEG_ONE;
    }

    SomariaCube_AllocCollider(play, cube);

    cube->gravity = SOMARIA_GRAVITY;
    cube->minVelocityY = SOMARIA_MIN_VEL_Y;
    cube->flags |= ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER;
    cube->flags |= ACTOR_FLAG_SWITCHHOOKABLE;
    cube->shape.shadowDraw = NULL;
    cube->shape.shadowScale = 0.0f;
    cube->room = -1;

    if (form >= ELEGY_FORM_MAX)
        form = ELEGY_FORM_HUMAN;
    SOMARIA_SET_FORM(cube, form);
    SOMARIA_SET_STATE(cube, SOMARIA_STATE_IDLE);

    Actor_SetScale(cube, SOMARIA_CUBE_SCALE);

    return cube;
}

void SomariaCube_UpdateRemotePos(Actor* cube, Vec3f* pos, f32 scale, s16 rotY) {
    if (cube == NULL)
        return;
    Math_Vec3f_Copy(&cube->world.pos, pos);
    cube->shape.rot.y = rotY;
    Actor_SetScale(cube, scale);
}
