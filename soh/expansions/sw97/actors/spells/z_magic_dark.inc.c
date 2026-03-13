/**
 * Original: z64proto/sw97 team (Spaceworld '97 Experience)
 * Adapted for Ship of Harkinian (Shipwright)
 *
 * File: z_magic_dark.c
 * Overlay: ovl_Magic_Dark
 * Description: Nayru's Love
 */

#include "expansions/sw97/sw97_compat.h"
#include "expansions/sw97/sw97_config.h"
#include "objects/gameplay_keep/gameplay_keep.h"

// ============================================================
// Struct (merged from z_magic_dark.h)
// ============================================================

typedef struct MagicDark {
    /* 0x0000 */ Actor actor;
    /* 0x014C */ s16 timer;
    /* 0x014E */ u8 primAlpha;
    /* 0x0150 */ Vec3f orbOffset;
    /* 0x015C */ f32 scale;
    /* 0x0160 */ char unk_160[0x4];
} MagicDark; // size = 0x0164

// Runtime actor ID (assigned by ActorDB in sw97_init.cpp)
extern s16 gSw97ActorId_MagicDark;

// ============================================================
// Forward declarations
// ============================================================

#define FLAGS 0x02000010
#define THIS ((MagicDark*)thisx)

void MagicDark_Init(Actor* thisx, PlayState* play);
void MagicDark_Destroy(Actor* thisx, PlayState* play);
void MagicDark_Update(Actor* thisx, PlayState* play);
void MagicDark_Draw(Actor* thisx, PlayState* play);
void MagicDark_OrbUpdate(Actor* thisx, PlayState* play);
void MagicDark_OrbDraw(Actor* thisx, PlayState* play);
void MagicDark_DiamondUpdate(Actor* thisx, PlayState* play);
void MagicDark_DiamondDraw(Actor* thisx, PlayState* play);

void MagicDark_DimLighting(PlayState* play, f32 intensity);

// ============================================================
// Graphics data (merged from z_magic_dark_gfx.c)
// ============================================================

static u64 sDiamondTex[] = {
    0x0000000000000000, /* placeholder - needs actual SW97 diamond texture data */
};

static Vtx sDiamondVerts[] = {
    VTX(0, 0, 64, 1024, 512, 0x00, 0x00, 0x78, 0xFF),    VTX(55, 0, 32, 1707, 512, 0x67, 0x00, 0x3C, 0xFF),
    VTX(0, 108, 0, 1365, 0, 0x00, 0x78, 0x00, 0xFF),     VTX(55, 0, -32, 2389, 512, 0x67, 0x00, 0xC4, 0xFF),
    VTX(0, 108, 0, 2048, 0, 0x00, 0x78, 0x00, 0xFF),     VTX(0, 0, -64, 3072, 512, 0x00, 0x00, 0x88, 0xFF),
    VTX(0, 108, 0, 2731, 0, 0x00, 0x78, 0x00, 0xFF),     VTX(-55, 0, -32, 3755, 512, 0x99, 0x00, 0xC4, 0xFF),
    VTX(0, 108, 0, 3413, 0, 0x00, 0x78, 0x00, 0xFF),     VTX(-55, 0, 32, 4437, 512, 0x98, 0x00, 0x3C, 0xFF),
    VTX(0, 108, 0, 4096, 0, 0x00, 0x78, 0x00, 0xFF),     VTX(-55, 0, 32, 341, 512, 0x98, 0x00, 0x3C, 0xFF),
    VTX(0, 108, 0, 683, 0, 0x00, 0x78, 0x00, 0xFF),      VTX(0, -108, 0, 683, 1024, 0x00, 0x88, 0x00, 0xFF),
    VTX(0, -108, 0, 3413, 1024, 0x00, 0x88, 0x00, 0xFF), VTX(0, -108, 0, 2731, 1024, 0x00, 0x88, 0x00, 0xFF),
    VTX(0, -108, 0, 2048, 1024, 0x00, 0x88, 0x00, 0xFF), VTX(0, -108, 0, 1365, 1024, 0x00, 0x88, 0x00, 0xFF),
    VTX(-55, 0, 32, 2389, 512, 0x98, 0x00, 0x3C, 0xFF),  VTX(-55, 0, -32, 1707, 512, 0x99, 0x00, 0xC4, 0xFF),
};

static Gfx sDiamondTexDList[] = {
    gsDPPipeSync(),
    gsDPSetTextureLUT(G_TT_NONE),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
    gsDPLoadTextureBlock(gEffUnknown10Tex, G_IM_FMT_I, G_IM_SIZ_8b, 32, 32, 0, G_TX_NOMIRROR | G_TX_WRAP,
                         G_TX_NOMIRROR | G_TX_WRAP, 5, 5, G_TX_NOLOD, 1),
    gsDPLoadMultiBlock(sDiamondTex, 0x0100, 1, G_IM_FMT_I, G_IM_SIZ_8b, 32, 64, 0, G_TX_NOMIRROR | G_TX_WRAP,
                       G_TX_MIRROR | G_TX_WRAP, 5, 6, 13, 13),
    gsDPSetCombineLERP(TEXEL1, PRIMITIVE, ENV_ALPHA, TEXEL0, TEXEL1, TEXEL0, ENVIRONMENT, TEXEL0, PRIMITIVE,
                       ENVIRONMENT, COMBINED, ENVIRONMENT, COMBINED, 0, PRIMITIVE, 0),
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_XLU_SURF2),
    gsSPClearGeometryMode(G_FOG | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR),
    gsSPSetGeometryMode(G_CULL_BACK | G_LIGHTING),
    gsSPEndDisplayList(),
};

static Gfx sDiamondVertsDList[] = {
    gsSPVertex(sDiamondVerts, 20, 0),           gsSP2Triangles(0, 1, 2, 0, 1, 3, 4, 0),
    gsSP2Triangles(3, 5, 6, 0, 5, 7, 8, 0),     gsSP2Triangles(7, 9, 10, 0, 11, 0, 12, 0),
    gsSP2Triangles(13, 0, 11, 0, 14, 7, 5, 0),  gsSP2Triangles(15, 5, 3, 0, 16, 3, 1, 0),
    gsSP2Triangles(17, 1, 0, 0, 16, 18, 19, 0), gsSPEndDisplayList(),
};

// ============================================================
// Init / Destroy
// ============================================================

void MagicDark_Init(Actor* thisx, PlayState* play) {
    MagicDark* this = THIS;
    Player* player = PLAYER;

    if (LINK_IS_CHILD) {
        this->scale = 0.4f;
    } else {
        this->scale = 0.6f;
    }

    thisx->world.pos = player->actor.world.pos;
    Actor_SetScale(&this->actor, 0.0f);
    thisx->room = -1;

    if (gSaveContext.nayrusLoveTimer != 0) {
        thisx->update = MagicDark_DiamondUpdate;
        thisx->draw = MagicDark_DiamondDraw;
        thisx->scale.x = thisx->scale.z = this->scale * 1.6f;
        thisx->scale.y = this->scale * 0.8f;
        this->timer = 0;
        this->primAlpha = 0;
    } else {
        this->timer = 0;
        gSaveContext.nayrusLoveTimer = 0;
    }
}

void MagicDark_Destroy(Actor* thisx, PlayState* play) {
    if (gSaveContext.nayrusLoveTimer == 0) {
        func_800876C8(play);
    }
}

// ============================================================
// Update functions
// ============================================================

void MagicDark_DiamondUpdate(Actor* thisx, PlayState* play) {
    MagicDark* this = THIS;
    u8 phi_a0;
    Player* player = PLAYER;
    s16 pad;
    s16 nayrusLoveTimer = gSaveContext.nayrusLoveTimer;
    s32 msgMode = play->msgCtx.msgMode;

    if ((msgMode == 0xD) || (msgMode == 0x11)) {
        Actor_Kill(thisx);
        return;
    }

    if (nayrusLoveTimer >= 1200) {
        player->invincibilityTimer = 0;
        gSaveContext.nayrusLoveTimer = 0;
        MagicDark_DimLighting(play, 0);
        Actor_Kill(thisx);
        return;
    }

    player->invincibilityTimer = -100;
    thisx->scale.x = thisx->scale.z = this->scale;

    if (this->timer < 20) {
        thisx->scale.x = thisx->scale.z = (1.6f - (this->timer * 0.03f)) * this->scale;
        thisx->scale.y = ((this->timer * 0.01f) + 0.8f) * this->scale;
    } else {
        thisx->scale.x = thisx->scale.z = this->scale;
        thisx->scale.y = this->scale;
    }

    thisx->scale.x *= 1.3f;
    thisx->scale.z *= 1.3f;

    phi_a0 = (this->timer < 20) ? (this->timer * 12) : 255;

    if (nayrusLoveTimer >= 1180) {
        this->primAlpha = 15595 - (nayrusLoveTimer * 13);
        if (nayrusLoveTimer & 1) {
            this->primAlpha = (u8)(this->primAlpha >> 1);
        }
    } else if (nayrusLoveTimer >= 1100) {
        this->primAlpha = (u8)(nayrusLoveTimer << 7) + 127;
    } else {
        this->primAlpha = 255;
    }

    if (this->primAlpha > phi_a0) {
        this->primAlpha = phi_a0;
    }

    thisx->world.rot.y += 0x3E8;
    thisx->shape.rot.y = thisx->world.rot.y + Camera_GetCamDirYaw(GET_ACTIVE_CAM(play));
    this->timer++;
    gSaveContext.nayrusLoveTimer = nayrusLoveTimer + 1;

    if (nayrusLoveTimer < 1100) {
        MagicDark_DimLighting(play, 1.0f);
        Audio_PlaySoundGeneral(NA_SE_EV_FANTOM_WARP_L - SFX_FLAG, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    } else {
        MagicDark_DimLighting(play, -0.0075f * (-1199.0f + nayrusLoveTimer));
        Audio_PlaySoundGeneral(NA_SE_EV_FANTOM_WARP_S - SFX_FLAG, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
}

void MagicDark_DimLighting(PlayState* play, f32 intensity) {
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

void MagicDark_OrbUpdate(Actor* thisx, PlayState* play) {
    MagicDark* this = THIS;
    s32 pad;
    Player* player = PLAYER;

    func_8002F974(&this->actor, NA_SE_PL_MAGIC_SOUL_BALL - SFX_FLAG);
    if (this->timer < 35) {
        MagicDark_DimLighting(play, this->timer * (1 / 45.0f));
        Math_SmoothStepToF(&thisx->scale.x, this->scale * (1 / 12.000001f), 0.05f, 0.01f, 0.0001f);
        Actor_SetScale(&this->actor, thisx->scale.x);
    } else if (this->timer < 55) {
        Actor_SetScale(&this->actor, thisx->scale.x * 0.9f);
        Math_SmoothStepToF(&this->orbOffset.y, player->bodyPartsPos[0].y, 0.5f, 3.0f, 1.0f);
    } else {
        thisx->update = MagicDark_DiamondUpdate;
        thisx->draw = MagicDark_DiamondDraw;
        thisx->scale.x = thisx->scale.z = this->scale * 1.6f;
        thisx->scale.y = this->scale * 0.8f;
        this->timer = 0;
        this->primAlpha = 0;
    }

    this->timer++;
}

// ============================================================
// Draw functions
// ============================================================

void MagicDark_DiamondDraw(Actor* thisx, PlayState* play) {
    MagicDark* this = THIS;
    s32 pad;
    u16 gameplayFrames = play->gameplayFrames;

    OPEN_DISPS(play->state.gfxCtx);

    func_80093D84(play->state.gfxCtx);

    {
        Player* player = PLAYER;
        f32 heightDiff;

        this->actor.world.pos.x = player->bodyPartsPos[0].x;
        this->actor.world.pos.z = player->bodyPartsPos[0].z;
        heightDiff = player->bodyPartsPos[0].y - this->actor.world.pos.y;
        if (heightDiff < -2.0f) {
            this->actor.world.pos.y = player->bodyPartsPos[0].y + 2.0f;
        } else if (heightDiff > 2.0f) {
            this->actor.world.pos.y = player->bodyPartsPos[0].y - 2.0f;
        }
        Matrix_Translate(this->actor.world.pos.x, this->actor.world.pos.y, this->actor.world.pos.z, MTXMODE_NEW);
        Matrix_Scale(this->actor.scale.x, this->actor.scale.y, this->actor.scale.z, MTXMODE_APPLY);
        Matrix_RotateY(this->actor.shape.rot.y * (M_PI / 0x8000), MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, "../z_magic_dark.c", 553),
                  G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 0, 0, 0, (s32)(this->primAlpha * 0.6f) & 0xFF);
        gDPSetEnvColor(POLY_XLU_DISP++, 0, 0, 0, 128);
        gSPDisplayList(POLY_XLU_DISP++, sDiamondTexDList);
        gSPDisplayList(POLY_XLU_DISP++, Gfx_TwoTexScroll(play->state.gfxCtx, 0, gameplayFrames * 2, gameplayFrames * -4,
                                                         32, 32, 1, 0, gameplayFrames * -16, 64, 32));
        gSPDisplayList(POLY_XLU_DISP++, sDiamondVertsDList);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

void MagicDark_OrbDraw(Actor* thisx, PlayState* play) {
    MagicDark* this = THIS;
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
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x80, 50, 50, 50, 255);
    gDPSetEnvColor(POLY_XLU_DISP++, 50, 50, 50, 255);
    Matrix_Translate(pos.x, pos.y, pos.z, MTXMODE_NEW);
    Matrix_Scale(this->actor.scale.x, this->actor.scale.y, this->actor.scale.z, MTXMODE_APPLY);
    Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
    Matrix_Push();
    gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, "../z_magic_dark.c", 632),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    Matrix_RotateZ(sp6C * (M_PI / 32), MTXMODE_APPLY);
    gSPDisplayList(POLY_XLU_DISP++, gEffFlash1DL);
    Matrix_Pop();
    Matrix_RotateZ(-sp6C * (M_PI / 32), MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, "../z_magic_dark.c", 639),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, gEffFlash1DL);

    CLOSE_DISPS(play->state.gfxCtx);
}

// Wrappers for sw97_init.cpp registration (initial update/draw = Orb phase)
void MagicDark_Update(Actor* thisx, PlayState* play) {
    MagicDark_OrbUpdate(thisx, play);
}

void MagicDark_Draw(Actor* thisx, PlayState* play) {
    MagicDark_OrbDraw(thisx, play);
}
