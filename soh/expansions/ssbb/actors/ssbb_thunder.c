/**
 * ssbb_thunder.c — Pikachu's Thunder (Down-B)
 *
 * Lightning column from sky to Pikachu's position.
 * Large vertical cylinder AT collider, light arrow damage.
 * Active for entire animation. Darkens scene during effect.
 *
 * Triggered by Din's Fire / Demise Destruction C-button.
 */

#include "ssbb_thunder.h"
#include "z64.h"

#define THUNDER_LIFETIME 45      // ~0.75 seconds
#define THUNDER_CHARGE_FRAMES 8  // Frames before bolt appears
#define THUNDER_BOLT_FRAMES 25   // Active damage frames
#define THUNDER_FADE_FRAMES 12   // Fade out
#define THUNDER_RADIUS 90
#define THUNDER_HEIGHT 400       // Tall column
#define THUNDER_DAMAGE 16        // Strong — light arrow equivalent

static ColliderCylinderInit sThunderColliderInit = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_ALL,   // AT_TYPE_ALL so it hits walls/crates too
        AC_NONE,
        OC1_NONE,
        OC2_NONE,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK0,
        { 0x200E3048, 0x00, 0x00 },  // EXPLOSIVE + ARROW_LIGHT + MAGIC_ALL + UNBLOCKABLE
        { 0x00000000, 0x00, 0x00 },
        TOUCH_ON | TOUCH_SFX_HARD,
        BUMP_NONE,
        OCELEM_NONE,
    },
    { THUNDER_RADIUS, THUNDER_HEIGHT, 0, { 0, 0, 0 } },
};

void SSBBThunder_Init(Actor* thisx, PlayState* play) {
    SSBBThunder* this = (SSBBThunder*)thisx;

    this->timer = THUNDER_LIFETIME;
    this->phase = 0; // charging
    this->columnHeight = 0.0f;

    Collider_InitCylinder(play, &this->collider);
    Collider_SetCylinder(play, &this->collider, thisx, &sThunderColliderInit);
    this->collider.info.toucher.damage = THUNDER_DAMAGE;

    Actor_SetScale(thisx, 1.0f);

    // Darken scene
    Environment_AdjustLights(play, 0.0f, 300.0f, 0.05f, 0.0f);
}

void SSBBThunder_Destroy(Actor* thisx, PlayState* play) {
    SSBBThunder* this = (SSBBThunder*)thisx;
    Collider_DestroyCylinder(play, &this->collider);

    // Restore lighting
    Environment_AdjustLights(play, 0.0f, 850.0f, 0.2f, 0.0f);
}

void SSBBThunder_Update(Actor* thisx, PlayState* play) {
    SSBBThunder* this = (SSBBThunder*)thisx;
    s32 age = THUNDER_LIFETIME - this->timer;

    this->timer--;
    if (this->timer <= 0) {
        Actor_Kill(thisx);
        return;
    }

    // Phase transitions
    if (age < THUNDER_CHARGE_FRAMES) {
        this->phase = 0; // charging — no damage yet
        this->columnHeight = (f32)age / THUNDER_CHARGE_FRAMES * 100.0f;
    } else if (age < THUNDER_CHARGE_FRAMES + THUNDER_BOLT_FRAMES) {
        this->phase = 1; // bolt active — full damage
        this->columnHeight = THUNDER_HEIGHT;

        // Active hitbox
        Collider_UpdateCylinder(thisx, &this->collider);
        CollisionCheck_SetAT(play, &play->colChkCtx, &this->collider.base);
    } else {
        this->phase = 2; // fading
        f32 fadeProgress = (f32)(age - THUNDER_CHARGE_FRAMES - THUNDER_BOLT_FRAMES) / THUNDER_FADE_FRAMES;
        this->columnHeight = THUNDER_HEIGHT * (1.0f - fadeProgress);
    }

    // VFX: electric particles around the column
    if (this->phase <= 1 && (play->gameplayFrames % 2) == 0) {
        static Color_RGBA8 primYellow = { 255, 230, 50, 255 };
        static Color_RGBA8 envWhite = { 255, 255, 200, 255 };
        Vec3f zero = { 0, 0, 0 };

        for (s32 i = 0; i < 4; i++) {
            u16 angle = (u16)(i * 0x4000 + play->gameplayFrames * 0x800);
            Vec3f particlePos;
            particlePos.x = thisx->world.pos.x + Math_SinS((s16)angle) * (THUNDER_RADIUS * 0.7f);
            particlePos.y = thisx->world.pos.y + (f32)(play->gameplayFrames % 8) * 30.0f;
            particlePos.z = thisx->world.pos.z + Math_CosS((s16)angle) * (THUNDER_RADIUS * 0.7f);

            Vec3f upVel = { 0, 8.0f, 0 };
            EffectSsBlast_Spawn(play, &particlePos, &upVel, &zero,
                                &primYellow, &envWhite, 30, -3, 2, 5);
        }
    }

    // Screen shake during bolt
    if (this->phase == 1 && age == THUNDER_CHARGE_FRAMES) {
        s32 quakeIdx = Quake_Add(play->cameraPtrs[play->activeCamera], 3);
        Quake_SetSpeed(quakeIdx, 20000);
        Quake_SetQuakeValues(quakeIdx, 4, 0, 0, 0);
        Quake_SetCountdown(quakeIdx, 15);
    }
}

void SSBBThunder_Draw(Actor* thisx, PlayState* play) {
    SSBBThunder* this = (SSBBThunder*)thisx;

    if (this->columnHeight < 1.0f) return;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);

    // Lightning column: bright yellow/white vertical beam
    u8 alpha = (this->phase == 2) ? (u8)(128 * (this->columnHeight / THUNDER_HEIGHT)) : 200;

    gDPPipeSync(POLY_XLU_DISP++);
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 240, 100, alpha);
    gDPSetEnvColor(POLY_XLU_DISP++, 255, 255, 200, alpha / 2);

    Matrix_SetTranslateRotateYXZ(thisx->world.pos.x, thisx->world.pos.y, thisx->world.pos.z,
                                  &thisx->shape.rot);
    // Scale cylinder to match column dimensions
    f32 radiusScale = THUNDER_RADIUS * 0.01f;
    f32 heightScale = this->columnHeight * 0.01f;
    Matrix_Scale(radiusScale, heightScale, radiusScale, MTXMODE_APPLY);

    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, gEffFlash1DL);

    // Inner bright core (narrower, brighter)
    if (this->phase == 1) {
        gDPPipeSync(POLY_XLU_DISP++);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, 255);

        Matrix_SetTranslateRotateYXZ(thisx->world.pos.x, thisx->world.pos.y, thisx->world.pos.z,
                                      &thisx->shape.rot);
        Matrix_Scale(radiusScale * 0.3f, heightScale, radiusScale * 0.3f, MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
                  G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, gEffFlash1DL);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

void SSBBThunder_Spawn(PlayState* play, Player* player) {
    Actor_Spawn(&play->actorCtx, play,
                ACTOR_SSBB_THUNDER,
                player->actor.world.pos.x,
                player->actor.world.pos.y,
                player->actor.world.pos.z,
                0, 0, 0, 0, 0);
}
