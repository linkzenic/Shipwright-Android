/**
 * object_icerod.c - Ice Rod 3D model and draw functions
 *
 * Draws the cyan/blue glowing rod and ice projectiles.
 * Uses gEffFire1DL with ice colors for projectiles.
 */

#include "z64.h"
#include "../custom_items.h"
#include "macros.h"
#include "functions.h"
#include <math.h>

// Fire effect display list from gameplay_keep (same as Fire Rod)
#include "objects/gameplay_keep/gameplay_keep.h"

// Ice Rod model from ice_rodDL folder
#include "ice_rodDL/header.h"

// Texture scroll counter for ice ball animation
static s16 sIceRodBallScroll = 0;

// Public display list reference for draw.cpp (give item)
Gfx* gIceRodGiveDL = g_ice_rod_dl;

// ============================================================================
// DRAW FUNCTION - Draws Ice Rod and active projectiles
// Uses ice crystal/spark effects for visual
// ============================================================================

void CustomItems_DrawIceRod(Player* player, PlayState* play) {
    if (!gCustomItemState.iceRodActive)
        return;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    // Get forearm and hand positions to calculate hand direction
    Vec3f forearmPos = player->bodyPartsPos[PLAYER_BODYPART_L_FOREARM];
    Vec3f handPos = player->bodyPartsPos[PLAYER_BODYPART_L_HAND];

    // Calculate direction vector from forearm to hand
    f32 dx = handPos.x - forearmPos.x;
    f32 dy = handPos.y - forearmPos.y;
    f32 dz = handPos.z - forearmPos.z;

    // Calculate yaw and pitch from direction
    f32 handYaw = atan2f(dx, dz);
    f32 horizDist = sqrtf(dx * dx + dz * dz);
    f32 handPitch = atan2f(dy, horizDist);

    // Position at hand
    Matrix_Translate(handPos.x, handPos.y, handPos.z, MTXMODE_NEW);

    // Apply hand rotation
    Matrix_RotateY(handYaw, MTXMODE_APPLY);
    Matrix_RotateX(-handPitch, MTXMODE_APPLY);
    Matrix_RotateY(BINANG_TO_RAD(0x4000), MTXMODE_APPLY);

    // Slight offset in local X and Z
    Matrix_Translate(-0.5f, 0.0f, 0.5f, MTXMODE_APPLY);

    Matrix_Scale(0.05f, 0.05f, 0.05f, MTXMODE_APPLY);

    gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, __FILE__, __LINE__),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, g_ice_rod_dl);

    // Draw transparent parts (ice crystal) with same matrix
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, __FILE__, __LINE__),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, g_ice_rod_xlu_dl);

    // Draw all active ice balls (1-3 depending on attack type)
    // Ice projectiles are drawn as glowing translucent spheres
    if (gCustomItemState.iceRodProjActive) {
        f32 baseScale = gCustomItemState.iceRodProjScale;
        u8 projCount = gCustomItemState.iceRodProjCount;

        // Update texture scroll for animation
        sIceRodBallScroll -= 10;
        sIceRodBallScroll &= 0x1FF;

        Gfx_SetupDL_25Xlu(play->state.gfxCtx);

        // CRITICAL: Set up texture segment before using gEffFire1DL (same as Fire Rod)
        gSPSegment(POLY_XLU_DISP++, 0x08,
                   Gfx_TwoTexScroll(play->state.gfxCtx, 0, 0, 0, 0x20, 0x40, 1, 0, sIceRodBallScroll, 0x20, 0x80));

        // Ice colors: cyan to white
        gDPSetPrimColor(POLY_XLU_DISP++, 0x80, 0x80, 200, 255, 255, 200);
        gDPSetEnvColor(POLY_XLU_DISP++, 0, 100, 255, 128);

        s16 camYaw = Camera_GetCamDirYaw(GET_ACTIVE_CAM(play)) + 0x8000;
        f32 iceScale = baseScale * 0.002f;
        if (iceScale < 0.001f)
            iceScale = 0.001f;

        // Draw ice ball 1 (always)
        Matrix_Translate(gCustomItemState.iceRodProjPos.x, gCustomItemState.iceRodProjPos.y,
                         gCustomItemState.iceRodProjPos.z, MTXMODE_NEW);
        Matrix_RotateY(camYaw * (M_PI / 0x8000), MTXMODE_APPLY);
        Matrix_Scale(iceScale, iceScale, iceScale, MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        // Use ice arrow effect display list if available, otherwise simple sphere
        gSPDisplayList(POLY_XLU_DISP++, gEffFire1DL); // Placeholder - will look like blue fire

        // Draw ice ball 2 (slash spread)
        if (projCount >= 2) {
            Matrix_Translate(gCustomItemState.iceRodProjPos2.x, gCustomItemState.iceRodProjPos2.y,
                             gCustomItemState.iceRodProjPos2.z, MTXMODE_NEW);
            Matrix_RotateY(camYaw * (M_PI / 0x8000), MTXMODE_APPLY);
            Matrix_Scale(iceScale, iceScale, iceScale, MTXMODE_APPLY);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, gEffFire1DL);
        }

        // Draw ice ball 3 (slash spread)
        if (projCount >= 3) {
            Matrix_Translate(gCustomItemState.iceRodProjPos3.x, gCustomItemState.iceRodProjPos3.y,
                             gCustomItemState.iceRodProjPos3.z, MTXMODE_NEW);
            Matrix_RotateY(camYaw * (M_PI / 0x8000), MTXMODE_APPLY);
            Matrix_Scale(iceScale, iceScale, iceScale, MTXMODE_APPLY);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, gEffFire1DL);
        }

        // Draw trail for center ice ball (fading ice particles)
        for (s32 i = 1; i < 4; i++) {
            Vec3f* trailPos = &gCustomItemState.iceRodProjTrail[i];
            f32 trailScale = iceScale * (1.0f - (i * 0.25f));
            if (trailScale < 0.0005f)
                continue;

            Matrix_Translate(trailPos->x, trailPos->y, trailPos->z, MTXMODE_NEW);
            Matrix_RotateY(camYaw * (M_PI / 0x8000), MTXMODE_APPLY);
            Matrix_Scale(trailScale, trailScale, trailScale, MTXMODE_APPLY);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, gEffFire1DL);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}
