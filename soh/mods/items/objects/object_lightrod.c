/**
 * object_lightrod.c - Light Rod 3D model and draw functions
 *
 * Draws the golden/yellow glowing rod and light projectiles.
 * Uses gPhantomEnergyBallDL (Phantom Ganon energy ball) for projectiles.
 */

#include "z64.h"
#include "../custom_items.h"
#include "macros.h"
#include "functions.h"
#include <math.h>

// Phantom Ganon energy ball display list (same as Dominion Rod)
#include "objects/object_fhg/object_fhg.h"

// Light Rod model from light_rodDL folder
#include "light_rodDL/header.h"

// Public display list reference for draw.cpp (give item)
Gfx* gLightRodGiveDL = g_light_rod_dl;

// ============================================================================
// LIGHT ROD ORB VISUAL CONSTANTS (same style as Dominion Rod)
// ============================================================================

#define LIGHTROD_ORB_SCALE 5.5f // Same as Dominion Rod

// Colors - golden/yellow variant of Dominion Rod's orange
#define LIGHTROD_ORB_PRIM_R 255
#define LIGHTROD_ORB_PRIM_G 255
#define LIGHTROD_ORB_PRIM_B 255
#define LIGHTROD_ORB_PRIM_A 200

#define LIGHTROD_ORB_ENV_R 255
#define LIGHTROD_ORB_ENV_G 255 // More yellow than Dominion Rod's 215
#define LIGHTROD_ORB_ENV_B 50
#define LIGHTROD_ORB_ENV_A 0

// ============================================================================
// DRAW FUNCTION - Draws Light Rod and active projectiles
// Uses exact same rendering approach as Dominion Rod
// ============================================================================

void CustomItems_DrawLightRod(Player* player, PlayState* play) {
    if (!gCustomItemState.lightRodActive)
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

    // Slight offset in local X, Y and Z
    Matrix_Translate(-0.5f, 5.0f, 0.5f, MTXMODE_APPLY);

    Matrix_Scale(0.05f, 0.05f, 0.05f, MTXMODE_APPLY);

    gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, __FILE__, __LINE__),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, g_light_rod_dl);

    // Draw transparent parts (light crystal) with same matrix
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, __FILE__, __LINE__),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, g_light_rod_xlu_dl);

    // Draw all active light balls (1-3 depending on attack type)
    // Uses EXACT same approach as Dominion Rod's CustomItems_DrawDominionRod
    if (gCustomItemState.lightRodProjActive) {
        u8 projCount = gCustomItemState.lightRodProjCount;

        // Rotation for energy ball effect (same formula as Dominion Rod)
        s16 rotZ = (play->gameplayFrames * 0x1000) + (s16)(Rand_ZeroOne() * 0x4000);

        // Draw light ball 1 (always) - exact same order as Dominion Rod
        Matrix_Translate(gCustomItemState.lightRodProjPos.x, gCustomItemState.lightRodProjPos.y,
                         gCustomItemState.lightRodProjPos.z, MTXMODE_NEW);
        Matrix_ReplaceRotation(&play->billboardMtxF);
        Matrix_Scale(LIGHTROD_ORB_SCALE, LIGHTROD_ORB_SCALE, LIGHTROD_ORB_SCALE, MTXMODE_APPLY);
        Matrix_RotateZ((rotZ / (f32)0x8000) * M_PI, MTXMODE_APPLY);

        Gfx_SetupDL_25Xlu(play->state.gfxCtx);

        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, LIGHTROD_ORB_PRIM_R, LIGHTROD_ORB_PRIM_G, LIGHTROD_ORB_PRIM_B,
                        LIGHTROD_ORB_PRIM_A);
        gDPSetEnvColor(POLY_XLU_DISP++, LIGHTROD_ORB_ENV_R, LIGHTROD_ORB_ENV_G, LIGHTROD_ORB_ENV_B, LIGHTROD_ORB_ENV_A);
        gDPPipeSync(POLY_XLU_DISP++);

        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, gPhantomEnergyBallDL);

        // Draw light ball 2 (slash spread)
        if (projCount >= 2) {
            Matrix_Translate(gCustomItemState.lightRodProjPos2.x, gCustomItemState.lightRodProjPos2.y,
                             gCustomItemState.lightRodProjPos2.z, MTXMODE_NEW);
            Matrix_ReplaceRotation(&play->billboardMtxF);
            Matrix_Scale(LIGHTROD_ORB_SCALE, LIGHTROD_ORB_SCALE, LIGHTROD_ORB_SCALE, MTXMODE_APPLY);
            Matrix_RotateZ(((rotZ + 0x5555) / (f32)0x8000) * M_PI, MTXMODE_APPLY);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, gPhantomEnergyBallDL);
        }

        // Draw light ball 3 (slash spread)
        if (projCount >= 3) {
            Matrix_Translate(gCustomItemState.lightRodProjPos3.x, gCustomItemState.lightRodProjPos3.y,
                             gCustomItemState.lightRodProjPos3.z, MTXMODE_NEW);
            Matrix_ReplaceRotation(&play->billboardMtxF);
            Matrix_Scale(LIGHTROD_ORB_SCALE, LIGHTROD_ORB_SCALE, LIGHTROD_ORB_SCALE, MTXMODE_APPLY);
            Matrix_RotateZ(((rotZ + 0xAAAA) / (f32)0x8000) * M_PI, MTXMODE_APPLY);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, gPhantomEnergyBallDL);
        }

        // Draw trail for center light ball (fading energy balls)
        for (s32 i = 1; i < 4; i++) {
            Vec3f* trailPos = &gCustomItemState.lightRodProjTrail[i];
            f32 trailScale = LIGHTROD_ORB_SCALE * (1.0f - (i * 0.25f));
            if (trailScale < 1.0f)
                continue;

            // Fade alpha for trail
            u8 trailAlpha = (u8)(LIGHTROD_ORB_PRIM_A * (1.0f - (i * 0.3f)));
            gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, LIGHTROD_ORB_PRIM_R, LIGHTROD_ORB_PRIM_G, LIGHTROD_ORB_PRIM_B,
                            trailAlpha);

            Matrix_Translate(trailPos->x, trailPos->y, trailPos->z, MTXMODE_NEW);
            Matrix_ReplaceRotation(&play->billboardMtxF);
            Matrix_Scale(trailScale, trailScale, trailScale, MTXMODE_APPLY);
            Matrix_RotateZ(((rotZ - (i * 0x2000)) / (f32)0x8000) * M_PI, MTXMODE_APPLY);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, gPhantomEnergyBallDL);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}
