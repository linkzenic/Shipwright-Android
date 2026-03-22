#include "expansions/ssbb/ssbb_spawn.h"
#include "expansions/ssbb/ssbb_character.h"
#include "expansions/ssbb/characters/pikachu_ssbb_register.h"
#include "expansions/ssbb/characters/pikachu_ssbb_dl.h"

static SSBBCharacterInstance sSSBBPikachuInst;
static s32 sSSBBPikachuDefIndex = -1;
static u8 sSSBBInitialized = 0;
static u8 sSSBBRegistered = 0;
static s32 sSSBBLastMode = 0;
static PlayState* sSSBBLastPlay = NULL;

void SSBBSpawn_Update(PlayState* play, Player* player) {
    s32 mode = CVarGetInteger(CVAR_SSBB_PIKACHU, SSBB_PIKACHU_OFF);

    if (mode == SSBB_PIKACHU_OFF) {
        sSSBBInitialized = 0;
        sSSBBLastMode = 0;
        return;
    }

    if (mode >= SSBB_PIKACHU_ANIM) {
        // Re-init on soft reset (play pointer changes) or mode change
        if (!sSSBBInitialized || mode != sSSBBLastMode || play != sSSBBLastPlay) {
            if (!sSSBBRegistered) {
                sSSBBPikachuDefIndex = pikachu_ssbb_Register();
                if (sSSBBPikachuDefIndex < 0)
                    return;
                sSSBBRegistered = 1;
            }
            // On soft reset, arena is wiped — NULL out old pointers before re-init
            if (sSSBBPikachuInst.def && sSSBBPikachuInst.def->skinMesh) {
                sSSBBPikachuInst.def->skinMesh->vtxBuf[0] = NULL;
                sSSBBPikachuInst.def->skinMesh->vtxBuf[1] = NULL;
            }
            SSBBChar_Init(&sSSBBPikachuInst, sSSBBPikachuDefIndex, play);
            SSBBChar_SetAnim(&sSSBBPikachuInst, 1, 1.0f);
            sSSBBLastPlay = play;
        }
        SSBBChar_Update(&sSSBBPikachuInst);
    }

    sSSBBInitialized = 1;
    sSSBBLastMode = mode;
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
    if (!sSSBBInitialized || CVarGetInteger(CVAR_SSBB_PIKACHU, SSBB_PIKACHU_OFF) == SSBB_PIKACHU_OFF)
        return;

    s32 mode = CVarGetInteger(CVAR_SSBB_PIKACHU, SSBB_PIKACHU_OFF);

    if (mode == SSBB_PIKACHU_REST) {
        // Static DL from Fast64 — proper geometry with lighting
        SSBBSpawn_DrawStaticDL(play, player);
    } else if (mode >= SSBB_PIKACHU_ANIM) {
        // mode 2 = animated, mode 3 = flat debug (all DLs same matrix), mode 4 = rest pose (zero rot)
        f32 yawRad = player->actor.shape.rot.y * (M_PI / 32768.0f);
        Vec3f pos;
        pos.x = player->actor.world.pos.x + sinf(yawRad) * 80.0f;
        pos.y = player->actor.world.pos.y;
        pos.z = player->actor.world.pos.z + cosf(yawRad) * 80.0f;
        Vec3s rot = { 0, player->actor.shape.rot.y + 0x8000, 0 };
        SSBBChar_Draw(&sSSBBPikachuInst, play, &pos, &rot);
    }
}
