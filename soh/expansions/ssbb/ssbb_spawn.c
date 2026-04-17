#include "expansions/ssbb/ssbb_spawn.h"
#include "expansions/ssbb/ssbb_character.h"
#include "expansions/ssbb/ssbb_anim.h"
#include "expansions/ssbb/characters/pikachu_ssbb_register.h"
#include "expansions/ssbb/characters/pikachu_ssbb_dl.h"

// Old static system (kept for static DL mode)
static SSBBCharacterInstance sSSBBPikachuInst;
static s32 sSSBBPikachuDefIndex = -1;
static u8 sSSBBInitialized = 0;
static u8 sSSBBRegistered = 0;
static s32 sSSBBLastMode = 0;
static PlayState* sSSBBLastPlay = NULL;

void SSBBSpawn_Update(PlayState* play, Player* player) {
    // Companion disabled — only transformation mode via Pokeball
}

static void SSBBSpawn_DrawStaticDL(PlayState* play, Player* player) {
    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    Matrix_Push();

    // Position in front of Link
    f32 yawRad = player->actor.shape.rot.y * (M_PI / 32768.0f);
    Vec3f pos;
    pos.x = player->actor.world.pos.x + sinf(yawRad) * 80.0f;
    pos.y = player->actor.world.pos.y;
    pos.z = player->actor.world.pos.z + cosf(yawRad) * 80.0f;
    Vec3s rot = { 0, player->actor.shape.rot.y + 0x8000, 0 };

    Matrix_SetTranslateRotateYXZ(pos.x, pos.y, pos.z, &rot);
    // Fast64 model is ~930 units tall, scale to ~45 units (Pikachu height)
    Matrix_Scale(0.05f, 0.05f, 0.05f, MTXMODE_APPLY);

    gDPPipeSync(POLY_OPA_DISP++);
    gSPSegment(POLY_OPA_DISP++, 0x0C, gCullBackDList);

    // Push matrix to RSP so the DL vertices are transformed
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    // Draw the Fast64 DL — has its own materials, lighting, and geometry
    gSPDisplayList(POLY_OPA_DISP++, polygon0_opaque_dl);

    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx);
}

void SSBBSpawn_Draw(PlayState* play, Player* player) {
    // Companion disabled — only transformation mode via Pokeball
}
