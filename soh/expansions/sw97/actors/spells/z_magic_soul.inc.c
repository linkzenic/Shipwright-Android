/**
 * Original: z64proto/sw97 team (Spaceworld '97 Experience)
 * Adapted for Ship of Harkinian (Shipwright)
 *
 * File: z_magic_soul.c
 * Overlay: ovl_Magic_Soul
 * Description: Nayru's Love
 */

#include "expansions/sw97/sw97_compat.h"
#include "expansions/sw97/sw97_config.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include "mods/items/custom_items.h"

// ============================================================
// Struct (merged from z_magic_soul.h)
// ============================================================

typedef struct MagicSoul {
    /* 0x0000 */ Actor actor;
    /* 0x014C */ s16 timer;
    /* 0x014E */ u8 primAlpha;
    /* 0x0150 */ Vec3f orbOffset;
    /* 0x015C */ f32 scale;
    /* 0x0160 */ char unk_160[0x4];
    f32 flashIntensity;
} MagicSoul; // size = 0x0164

// Runtime actor ID (assigned by ActorDB in sw97_init.cpp)
extern s16 gSw97ActorId_MagicSoul;

// SOH doesn't have gSaveContext.fairyTimer - use a file-static instead
static s16 sw97FairyTimer = 0;

// ============================================================
// Forward declarations
// ============================================================

#define FLAGS 0x02000030
#define THIS ((MagicSoul*)thisx)

void MagicSoul_Init(Actor* thisx, PlayState* play);
void MagicSoul_Destroy(Actor* thisx, PlayState* play);
void MagicSoul_Update(Actor* thisx, PlayState* play);
void MagicSoul_Draw(Actor* thisx, PlayState* play);
void MagicSoul_OrbUpdate(Actor* thisx, PlayState* play);
void MagicSoul_OrbDraw(Actor* thisx, PlayState* play);
void MagicSoul_DiamondUpdate(Actor* thisx, PlayState* play);
void MagicSoul_DiamondDraw(Actor* thisx, PlayState* play);

void MagicSoul_DimLighting(PlayState* play, f32 intensity);
void MagicSoul_UpdateFlash(Actor* this, f32 intensity);

// ============================================================
// Init / Destroy
// ============================================================

void MagicSoul_Init(Actor* thisx, PlayState* play) {
    MagicSoul* this = THIS;
    Player* player = PLAYER;

    if (LINK_IS_CHILD) {
        this->scale = 0.4f;
    } else {
        this->scale = 0.6f;
    }

    thisx->world.pos = player->actor.world.pos;
    Actor_SetScale(&this->actor, 0.0f);
    thisx->room = -1;

    if (sw97FairyTimer != 0) {
        thisx->update = MagicSoul_DiamondUpdate;
        thisx->draw = MagicSoul_DiamondDraw;
        thisx->scale.x = thisx->scale.z = this->scale * 1.6f;
        thisx->scale.y = this->scale * 0.8f;
        this->timer = 0;
        this->primAlpha = 0;
    } else {
        this->timer = 0;
        sw97FairyTimer = 0;
    }
}

void MagicSoul_Destroy(Actor* thisx, PlayState* play) {
    // When handing off to Hylia's Grace fairy system, skip cleanup —
    // the fairy system handles camera/magic/restore on its own.
    if (!gCustomItemState.hyliasGraceActive) {
        Camera_ChangeSetting(Play_GetCamera(play, MAIN_CAM), CAM_SET_NORMAL0);
        func_800876C8(play);
        Audio_PlayActorSound2(thisx, NA_SE_EV_TRIFORCE_FLASH);
    }
    sw97FairyTimer = 0;
}

// ============================================================
// Update functions
// ============================================================

void MagicSoul_DiamondUpdate(Actor* thisx, PlayState* play) {
    MagicSoul* this = THIS;
    Player* player = PLAYER;

    this->timer++;

    // Flash effect during transition
    if (this->timer < 20) {
        MagicSoul_UpdateFlash(&this->actor, 1);
    } else {
        MagicSoul_UpdateFlash(&this->actor, 0);
    }

    // Sound effect while waiting
    func_8002F974(&player->actor, NA_SE_PL_MAGIC_SOUL_NORMAL - SFX_FLAG);

    // Wait for player to leave spell action before activating fairy mode
    if (player->stateFlags1 & PLAYER_STATE1_IN_ITEM_CS) {
        return;
    }

    // Player is free — hand off to Hylia's Grace fairy system
    // Screen flash + sound
    func_800AA000(400.0f, 200, 30, 100);
    Audio_PlayActorSound2(thisx, NA_SE_EV_TRIFORCE_FLASH);

    // Activate fairy mode directly (skip casting/warp-in phases)
    gCustomItemState.hyliasGraceActive = 1;
    gCustomItemState.hyliasGraceState = 3;    // HGRACE_STATE_FAIRY
    gCustomItemState.hyliasGraceTimer = 1200; // HGRACE_SW97_DURATION (20 seconds)
    gCustomItemState.hyliasGraceForcedBySpell = 1;
    gCustomItemState.hyliasGraceSubPhase = 0;
    gCustomItemState.hyliasGraceFairy = NULL;

    // Kill self — Handle_HyliasGrace takes over fairy flight management next frame
    Actor_Kill(thisx);
}

void MagicSoul_DimLighting(PlayState* play, f32 intensity) {
    s32 i;
    f32 temp_f0;
    f32 phi_f0;

    if (play->roomCtx.curRoom.behaviorType1 != ROOM_BEHAVIOR_TYPE1_5) {
        intensity = CLAMP_MIN(intensity, 0.0f);
        intensity = CLAMP_MAX(intensity, 1.0f);
        phi_f0 = intensity - 0.2f;
        if (intensity < 0.2f) {
            phi_f0 = 0.0f;
        }
        play->envCtx.adjFogNear = (850.0f - play->envCtx.lightSettings.fogNear) * phi_f0;
        if (intensity == 0.0f) {
            for (i = 0; i < ARRAY_COUNT(play->envCtx.adjFogColor); i++) {
                play->envCtx.adjFogColor[i] = 0;
            }
        } else {
            temp_f0 = intensity * 5.0f;
            if (temp_f0 > 1.0f) {
                temp_f0 = 1.0f;
            }

            for (i = 0; i < ARRAY_COUNT(play->envCtx.adjFogColor); i++) {
                play->envCtx.adjFogColor[i] = -(s16)(play->envCtx.lightSettings.fogColor[i] * temp_f0);
            }
        }
    }
}

void MagicSoul_OrbUpdate(Actor* thisx, PlayState* play) {
    MagicSoul* this = THIS;
    s32 pad;
    Player* player = PLAYER;
    player->stateFlags2 |= 0x100000; // keep navi out

    // SW97-specific EnElf fields not in SOH:
    // if (this->timer == 35) {
    //     navi->active_mode = 8;
    // }

    func_8002F974(&player->actor, NA_SE_PL_MAGIC_SOUL_BALL - SFX_FLAG);
    if (this->timer < 35) {
        MagicSoul_UpdateFlash(&this->actor, 1);
        MagicSoul_DimLighting(play, this->timer * (1 / 45.0f));
        Math_SmoothStepToF(&thisx->scale.x, this->scale * (1 / 12.000001f), 0.05f, 0.01f, 0.0001f);
        Actor_SetScale(&this->actor, thisx->scale.x);
    } else if (this->timer < 45) {
        Audio_PlayActorSound2(&this->actor, NA_SE_EV_NABALL_VANISH);
    } else if (this->timer < 55) {
        Actor_SetScale(&this->actor, thisx->scale.x * 0.9f);
        Math_SmoothStepToF(&this->orbOffset.y, player->bodyPartsPos[0].y, 0.5f, 3.0f, 1.0f);
    } else {
        thisx->update = MagicSoul_DiamondUpdate;
        thisx->draw = MagicSoul_DiamondDraw;
        thisx->scale.x = thisx->scale.z = this->scale * 1.6f;
        thisx->scale.y = this->scale * 0.8f;
        this->timer = 0;
        this->primAlpha = 0;

        player->stateFlags1 &= ~0x30000000; // unfreeze actors
    }

    this->timer++;
}

// ============================================================
// Draw functions
// ============================================================

void MagicSoul_DiamondDraw(Actor* thisx, PlayState* play) {
    MagicSoul* this = THIS;

    if (this->flashIntensity > 0) {
        OPEN_DISPS(play->state.gfxCtx);

        POLY_XLU_DISP = func_800937C0(POLY_XLU_DISP);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 200, (s32)(225.0f * this->flashIntensity) & 0xFF);
        gDPSetAlphaDither(POLY_XLU_DISP++, G_AD_DISABLE);
        gDPSetColorDither(POLY_XLU_DISP++, G_CD_DISABLE);
        gDPFillRectangle(POLY_XLU_DISP++, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);

        CLOSE_DISPS(play->state.gfxCtx);
    }
}

void MagicSoul_UpdateFlash(Actor* thisx, f32 intensity) {
    MagicSoul* this = THIS;

    if (this->flashIntensity < intensity) {
        this->flashIntensity += 0.10f;
    } else if (this->flashIntensity > intensity) {
        this->flashIntensity -= 0.10f;
    }

    this->flashIntensity = CLAMP_MIN(this->flashIntensity, 0.0f);
    this->flashIntensity = CLAMP_MAX(this->flashIntensity, 1.0f);
}

void MagicSoul_OrbDraw(Actor* thisx, PlayState* play) {
    MagicSoul* this = THIS;
    Vec3f pos;
    Player* player = PLAYER;
    s32 pad;
    f32 sp6C = play->state.frames & 0x1F;

    if (this->timer < 32) {
        pos.x = (player->bodyPartsPos[12].x + player->bodyPartsPos[15].x) * 0.5f;
        pos.y = (player->bodyPartsPos[12].y + player->bodyPartsPos[15].y) * 0.5f;
        pos.z = (player->bodyPartsPos[12].z + player->bodyPartsPos[15].z) * 0.5f;
        if (this->timer > 20) {
            pos.y += (this->timer - 20) * 1.4f;
        }
        this->orbOffset = pos;
    } else if (this->timer < 130) {
        pos = this->orbOffset;
    } else {
        return;
    }

    pos.x -= (this->actor.scale.x * 300.0f * Math_SinS(Camera_GetCamDirYaw(GET_ACTIVE_CAM(play))) *
              Math_CosS(Camera_GetCamDirPitch(GET_ACTIVE_CAM(play))));
    pos.y -= (this->actor.scale.x * 300.0f * Math_SinS(Camera_GetCamDirPitch(GET_ACTIVE_CAM(play))));
    pos.z -= (this->actor.scale.x * 300.0f * Math_CosS(Camera_GetCamDirYaw(GET_ACTIVE_CAM(play))) *
              Math_CosS(Camera_GetCamDirPitch(GET_ACTIVE_CAM(play))));

    OPEN_DISPS(play->state.gfxCtx);

    func_80093D84(play->state.gfxCtx);
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x80, 255, 255, 170, 255);
    gDPSetEnvColor(POLY_XLU_DISP++, 255, 255, 0, 255);
    Matrix_Translate(pos.x, pos.y, pos.z, MTXMODE_NEW);
    Matrix_Scale(this->actor.scale.x, this->actor.scale.y, this->actor.scale.z, MTXMODE_APPLY);
    Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
    Matrix_Push();
    gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, "../z_magic_soul.c", 632),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    Matrix_RotateZ(sp6C * (M_PI / 32), MTXMODE_APPLY);
    gSPDisplayList(POLY_XLU_DISP++, gEffFlash1DL);
    Matrix_Pop();
    Matrix_RotateZ(-sp6C * (M_PI / 32), MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, "../z_magic_soul.c", 639),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, gEffFlash1DL);

    CLOSE_DISPS(play->state.gfxCtx);
}

// Wrappers for sw97_init.cpp registration (initial update/draw = Orb phase)
void MagicSoul_Update(Actor* thisx, PlayState* play) {
    MagicSoul_OrbUpdate(thisx, play);
}

void MagicSoul_Draw(Actor* thisx, PlayState* play) {
    MagicSoul_OrbDraw(thisx, play);
}
