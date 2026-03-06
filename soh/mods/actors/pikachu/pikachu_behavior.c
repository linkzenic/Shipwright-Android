/**
 * Pikachu Actor Behavior - 4 modes via CVAR
 *
 * Uses actor hijacking on En_Lightbox.
 *
 * CVAR: "gMods.Pikachu.Mode" (integer, default 0)
 *   0 = Off (kill actor)
 *   1 = Static DL (spinning)
 *   2 = Skeleton rest pose (no animation)
 *   3 = Skeleton + Wait animation
 *
 * Console:
 *   set gMods.Pikachu.Mode 1
 *   set gMods.Pikachu.Mode 2
 *   set gMods.Pikachu.Mode 3
 *   set gMods.Pikachu.Mode 0
 */

#include "pikachu_behavior.h"
#include "pikachu.h"
#include "pikachu_skel.h"
#include "pika_wait.h"
#include "overlays/actors/ovl_En_Lightbox/z_en_lightbox.h"

// ============================================================================
// STATE
// ============================================================================

static Actor* sPikachuActor = NULL;
static ActorFunc sOrigPikachuDestroy = NULL;
static s32 sPikachuCurrentMode = PIKACHU_MODE_OFF;

// SkelAnime (static storage since we can't extend EnLightbox)
static SkelAnime sPikachuSkelAnime;
static Vec3s sPikachuJointTable[ARMATURE_NUM_LIMBS];
static Vec3s sPikachuMorphTable[ARMATURE_NUM_LIMBS];
static s32 sPikachuSkelInited = 0;

// ============================================================================
// SKEL INIT HELPERS
// ============================================================================

static void Pikachu_InitSkel(PlayState* play, s32 withAnim) {
    if (sPikachuSkelInited) {
        return;
    }
    SkelAnime_InitFlex(play, &sPikachuSkelAnime, &Armature,
                       withAnim ? &ArmatureRunAnim : NULL,
                       sPikachuJointTable, sPikachuMorphTable, ARMATURE_NUM_LIMBS);
    if (withAnim) {
        Animation_PlayLoop(&sPikachuSkelAnime, &ArmatureRunAnim);
    }
    sPikachuSkelInited = 1;
}

// ============================================================================
// DRAW - Mode 1: Static display list (spinning)
// ============================================================================

static void Pikachu_DrawStatic(Actor* thisx, PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, Mesh_003_rdmobj01_opaque_dl);

    CLOSE_DISPS(play->state.gfxCtx);
}

// ============================================================================
// DRAW - Mode 2: Skeleton rest pose (no animation update)
// ============================================================================

static void Pikachu_DrawRestPose(Actor* thisx, PlayState* play) {
    if (!sPikachuSkelInited) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    SkelAnime_DrawFlexOpa(play, sPikachuSkelAnime.skeleton,
                          sPikachuSkelAnime.jointTable,
                          sPikachuSkelAnime.dListCount,
                          NULL, NULL, thisx);

    CLOSE_DISPS(play->state.gfxCtx);
}

// ============================================================================
// DRAW - Mode 3: Skeleton + Animation
// ============================================================================

static void Pikachu_DrawAnimated(Actor* thisx, PlayState* play) {
    if (!sPikachuSkelInited) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    SkelAnime_DrawFlexOpa(play, sPikachuSkelAnime.skeleton,
                          sPikachuSkelAnime.jointTable,
                          sPikachuSkelAnime.dListCount,
                          NULL, NULL, thisx);

    CLOSE_DISPS(play->state.gfxCtx);
}

// ============================================================================
// UPDATE
// ============================================================================

static void Pikachu_Update(Actor* thisx, PlayState* play) {
    s32 mode = CVarGetInteger(CVAR_PIKACHU_MODE, PIKACHU_MODE_OFF);

    if (mode == PIKACHU_MODE_OFF) {
        Actor_Kill(thisx);
        sPikachuActor = NULL;
        sPikachuSkelInited = 0;
        return;
    }

    // Mode changed
    if (mode != sPikachuCurrentMode) {
        sPikachuSkelInited = 0;

        if (mode == PIKACHU_MODE_RESTPOSE) {
            Pikachu_InitSkel(play, 0);
            thisx->draw = Pikachu_DrawRestPose;
        } else if (mode == PIKACHU_MODE_ANIMATED) {
            Pikachu_InitSkel(play, 1);
            thisx->draw = Pikachu_DrawAnimated;
        } else {
            thisx->draw = Pikachu_DrawStatic;
        }
        sPikachuCurrentMode = mode;
    }

    if (mode == PIKACHU_MODE_STATIC) {
        thisx->shape.rot.y += PIKACHU_SPIN_SPEED;
    } else if (mode == PIKACHU_MODE_ANIMATED && sPikachuSkelInited) {
        SkelAnime_Update(&sPikachuSkelAnime);
    }
}

// ============================================================================
// DESTROY
// ============================================================================

static void Pikachu_Destroy(Actor* thisx, PlayState* play) {
    if (thisx == sPikachuActor) {
        sPikachuActor = NULL;
        sPikachuSkelInited = 0;
        sPikachuCurrentMode = PIKACHU_MODE_OFF;
    }

    if (sOrigPikachuDestroy != NULL) {
        sOrigPikachuDestroy(thisx, play);
    }
}

// ============================================================================
// MAIN UPDATE - Called from z_player.c
// ============================================================================

void PikachuBehavior_Update(PlayState* play, Player* player) {
    s32 mode = CVarGetInteger(CVAR_PIKACHU_MODE, PIKACHU_MODE_OFF);

    if (mode == PIKACHU_MODE_OFF) {
        return;
    }

    // Already alive
    if (sPikachuActor != NULL && sPikachuActor->update != NULL) {
        return;
    }

    sPikachuActor = NULL;
    sPikachuSkelInited = 0;

    // Spawn in front of player
    Vec3f spawnPos = player->actor.world.pos;
    spawnPos.x += PIKACHU_SPAWN_OFFSET * Math_SinS(player->actor.shape.rot.y);
    spawnPos.z += PIKACHU_SPAWN_OFFSET * Math_CosS(player->actor.shape.rot.y);

    Actor* actor = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_LIGHTBOX,
                               spawnPos.x, spawnPos.y, spawnPos.z,
                               0, 0, 0, 0, true);

    if (actor == NULL) {
        return;
    }

    EnLightbox* lightbox = (EnLightbox*)actor;

    if (sOrigPikachuDestroy == NULL) {
        sOrigPikachuDestroy = actor->destroy;
    }

    actor->update = Pikachu_Update;
    actor->destroy = Pikachu_Destroy;

    // Init based on mode
    if (mode == PIKACHU_MODE_RESTPOSE) {
        Pikachu_InitSkel(play, 0);
        actor->draw = Pikachu_DrawRestPose;
    } else if (mode == PIKACHU_MODE_ANIMATED) {
        Pikachu_InitSkel(play, 1);
        actor->draw = Pikachu_DrawAnimated;
    } else {
        actor->draw = Pikachu_DrawStatic;
    }
    sPikachuCurrentMode = mode;

    // Remove DynaPoly
    if (lightbox->dyna.bgId != BGACTOR_NEG_ONE) {
        DynaPoly_DeleteBgActor(play, &play->colCtx.dyna, lightbox->dyna.bgId);
        lightbox->dyna.bgId = BGACTOR_NEG_ONE;
    }

    Actor_SetScale(actor, PIKACHU_SCALE);

    actor->shape.shadowDraw = NULL;
    actor->shape.shadowScale = 0.0f;
    actor->gravity = 0.0f;
    actor->minVelocityY = 0.0f;

    sPikachuActor = actor;
}
