/**
 * ssbb_thunder_jolt.c — Pikachu's Thunder Jolt projectile
 *
 * Electric ball that travels forward, bouncing along the terrain.
 * Damages enemies with electric/magic damage on contact.
 * Disappears after 90 frames or on hit.
 */

#include "ssbb_thunder_jolt.h"
#include "z64.h"

#define JOLT_LIFETIME 90        // ~1.5 seconds
#define JOLT_SPEED 8.0f         // Forward speed
#define JOLT_BOUNCE_VEL 4.0f    // Bounce impulse
#define JOLT_GRAVITY -0.8f      // Gravity per frame
#define JOLT_RADIUS 15.0f       // Visual/collider radius
#define JOLT_DAMAGE 4           // Quarter-hearts

// Collider init — sphere AT (attacks enemies)
static ColliderSphereInit sJoltColliderInit = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_PLAYER,
        AC_NONE,
        OC1_ON | OC1_TYPE_ALL,
        OC2_TYPE_1,
        COLSHAPE_SPHERE,
    },
    {
        ELEMTYPE_UNK0,
        { 0x000A2024, 0x00, 0x00 },  // ARROW + SLINGSHOT + MAGIC_FIRE + MAGIC_LIGHT (like arrow)
        { 0x00000000, 0x00, 0x00 },
        TOUCH_ON | TOUCH_SFX_HARD,
        BUMP_NONE,
        OCELEM_ON,
    },
    { 0, { { 0, 0, 0 }, 15 }, 100 },
};

void SSBBThunderJolt_Init(Actor* thisx, PlayState* play) {
    SSBBThunderJolt* this = (SSBBThunderJolt*)thisx;

    // Set initial velocity in facing direction
    f32 yaw = thisx->world.rot.y * (3.14159265f / 32768.0f);
    thisx->velocity.x = sinf(yaw) * JOLT_SPEED;
    thisx->velocity.z = cosf(yaw) * JOLT_SPEED;
    thisx->velocity.y = 2.0f; // slight upward arc
    this->bounceVelY = 2.0f;

    thisx->gravity = JOLT_GRAVITY;
    thisx->minVelocityY = -10.0f;

    this->timer = JOLT_LIFETIME;
    this->hitSomething = 0;

    // Init collider
    Collider_InitSphere(play, &this->collider);
    Collider_SetSphere(play, &this->collider, thisx, &sJoltColliderInit);
    this->collider.info.toucher.damage = JOLT_DAMAGE;

    // Scale (small electric ball)
    Actor_SetScale(thisx, 0.01f);
}

void SSBBThunderJolt_Destroy(Actor* thisx, PlayState* play) {
    SSBBThunderJolt* this = (SSBBThunderJolt*)thisx;
    Collider_DestroySphere(play, &this->collider);
}

void SSBBThunderJolt_Update(Actor* thisx, PlayState* play) {
    SSBBThunderJolt* this = (SSBBThunderJolt*)thisx;

    // Countdown
    this->timer--;
    if (this->timer <= 0 || this->hitSomething) {
        // Spawn electric burst effect on death
        static Color_RGBA8 yellow = { 255, 230, 50, 255 };
        static Color_RGBA8 white = { 255, 255, 200, 255 };
        Vec3f zero = { 0, 0, 0 };
        EffectSsBlast_Spawn(play, &thisx->world.pos, &zero, &zero, &yellow, &white, 40, -3, 2, 6);
        Actor_Kill(thisx);
        return;
    }

    // Check if AT hit something
    if (this->collider.base.atFlags & AT_HIT) {
        this->hitSomething = 1;
        this->collider.base.atFlags &= ~AT_HIT;
    }

    // Move
    Actor_MoveForward(thisx);

    // Floor check for bouncing
    Actor_UpdateBgCheckInfo(play, thisx, 10.0f, JOLT_RADIUS, 0.0f,
                            UPDBGCHECKINFO_FLAG_0 | UPDBGCHECKINFO_FLAG_2);

    // Bounce off floor
    if (thisx->bgCheckFlags & BGCHECKFLAG_GROUND) {
        thisx->world.pos.y = thisx->floorHeight;
        thisx->velocity.y = JOLT_BOUNCE_VEL;
    }

    // Update collider position
    this->collider.dim.worldSphere.center.x = (s16)thisx->world.pos.x;
    this->collider.dim.worldSphere.center.y = (s16)thisx->world.pos.y;
    this->collider.dim.worldSphere.center.z = (s16)thisx->world.pos.z;
    this->collider.dim.worldSphere.radius = (s16)JOLT_RADIUS;

    CollisionCheck_SetAT(play, &play->colChkCtx, &this->collider.base);
    CollisionCheck_SetOC(play, &play->colChkCtx, &this->collider.base);

    // Electric sparkle particles
    if ((play->gameplayFrames % 3) == 0) {
        static Color_RGBA8 primYellow = { 255, 230, 50, 255 };
        static Color_RGBA8 envWhite = { 255, 255, 200, 200 };
        Vec3f sparkVel = { 0, 1.0f, 0 };
        Vec3f sparkAccel = { 0, 0, 0 };
        EffectSsBlast_Spawn(play, &thisx->world.pos, &sparkVel, &sparkAccel,
                            &primYellow, &envWhite, 15, -2, 1, 3);
    }
}

void SSBBThunderJolt_Draw(Actor* thisx, PlayState* play) {
    // Draw as a glowing sphere using OOT's existing sphere DL
    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);

    gDPPipeSync(POLY_XLU_DISP++);
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 230, 50, 180);
    gDPSetEnvColor(POLY_XLU_DISP++, 120, 180, 255, 128);

    Matrix_SetTranslateRotateYXZ(thisx->world.pos.x, thisx->world.pos.y, thisx->world.pos.z,
                                  &thisx->shape.rot);
    // Pulsing scale
    f32 pulse = 1.0f + 0.2f * sinf(play->gameplayFrames * 0.3f);
    Matrix_Scale(JOLT_RADIUS * pulse * 0.01f, JOLT_RADIUS * pulse * 0.01f,
                 JOLT_RADIUS * pulse * 0.01f, MTXMODE_APPLY);

    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    // Use gameplay_keep sphere DL (same as Navi's glow)
    gSPDisplayList(POLY_XLU_DISP++, gEffFlash1DL);

    CLOSE_DISPS(play->state.gfxCtx);
}

// ── Spawn helper (called from pikachu_form.cpp) ──────────────────────────────

void SSBBThunderJolt_Spawn(PlayState* play, Player* player) {
    f32 yaw = (f32)player->actor.world.rot.y * (3.14159265f / 32768.0f);
    f32 spawnDist = 30.0f;

    Actor_Spawn(&play->actorCtx, play,
                ACTOR_SSBB_THUNDER_JOLT,
                player->actor.world.pos.x + sinf(yaw) * spawnDist,
                player->actor.world.pos.y + 25.0f,
                player->actor.world.pos.z + cosf(yaw) * spawnDist,
                0, player->actor.world.rot.y, 0, 0, 0);
}
