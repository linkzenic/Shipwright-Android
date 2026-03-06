/**
 * object_firerod.c - Fire Rod 3D model and draw functions
 *
 * Draws the red glowing rod and fire projectiles.
 * Uses gEffFire1DL (torch-style flame) for projectiles.
 */

#include "z64.h"
#include "../custom_items.h"
#include "../logic/item_rod_fire.h"
#include "macros.h"
#include "functions.h"
#include <math.h>

// Fire effect display list from gameplay_keep (torch flame)
#include "objects/gameplay_keep/gameplay_keep.h"

// Fire Rod model from fire_rodDL folder
#include "fire_rodDL/header.h"

// Flame texture scroll counter (for torch-style animation)
static s16 sFireRodFlameScroll = 0;

// Public display list reference for draw.cpp (give item)
Gfx* gFireRodGiveDL = g_fire_rod_dl;

// ============================================================================
// DRAW FUNCTION - Draws Fire Rod and active projectile
// Uses torch-style flame (gEffFire1DL) like En_Honotrap and Obj_Syokudai
// ============================================================================

void CustomItems_DrawFireRod(Player* player, PlayState* play) {
    if (!fireRodActive)
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
    gSPDisplayList(POLY_OPA_DISP++, g_fire_rod_dl);

    // Draw all active fireball sets (up to 5 concurrent sets)
    if (FireRod_HasAnyActiveSet()) {
        sFireRodFlameScroll -= 20;
        sFireRodFlameScroll &= 0x1FF;

        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gSPSegment(POLY_XLU_DISP++, 0x08,
                   Gfx_TwoTexScroll(play->state.gfxCtx, 0, 0, 0, 0x20, 0x40, 1, 0, sFireRodFlameScroll, 0x20, 0x80));
        gDPSetPrimColor(POLY_XLU_DISP++, 0x80, 0x80, 255, 255, 0, 255);
        gDPSetEnvColor(POLY_XLU_DISP++, 255, 0, 0, 0);

        s16 camYaw = Camera_GetCamDirYaw(GET_ACTIVE_CAM(play)) + 0x8000;

        RodProjSet* sets = FireRod_GetProjSets();
        for (s32 s = 0; s < ROD_MAX_PROJ_SETS; s++) {
            RodProjSet* set = &sets[s];
            if (!set->active)
                continue;

            f32 flameScale = set->scale * 0.0015f;
            if (flameScale < 0.001f)
                flameScale = 0.001f;

            // Draw all fireballs in this set (1-3)
            for (s32 p = 0; p < set->count; p++) {
                Matrix_Translate(set->pos[p].x, set->pos[p].y, set->pos[p].z, MTXMODE_NEW);
                Matrix_RotateY(camYaw * (M_PI / 0x8000), MTXMODE_APPLY);
                Matrix_Scale(flameScale, flameScale, flameScale, MTXMODE_APPLY);
                gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
                          G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                gSPDisplayList(POLY_XLU_DISP++, gEffFire1DL);
            }

            // Draw trail for center fireball of this set
            for (s32 i = 1; i < 4; i++) {
                Vec3f* trailPos = &set->trail[i];
                f32 trailScale = flameScale * (1.0f - (i * 0.25f));
                if (trailScale < 0.0005f)
                    continue;

                Matrix_Translate(trailPos->x, trailPos->y, trailPos->z, MTXMODE_NEW);
                Matrix_RotateY(camYaw * (M_PI / 0x8000), MTXMODE_APPLY);
                Matrix_Scale(trailScale, trailScale, trailScale, MTXMODE_APPLY);
                gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
                          G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                gSPDisplayList(POLY_XLU_DISP++, gEffFire1DL);
            }
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}
