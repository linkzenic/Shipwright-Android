/**
 * object_shovel.c - Shovel 3D model draw functions
 *
 * Draws the shovel when Link is digging.
 * Uses forearm-to-hand vector to calculate hand direction/rotation.
 */
#include "z64.h"
#include "../custom_items.h"
#include "macros.h"
#include "functions.h"
#include <math.h>

// Shovel model from shovel_DL folder
#include "shovel_DL/gDampeShovelDL_mesh_001.h"
#include "shovel_DL/gDampeShovelDL_mesh_001.c"

// Public display list reference for draw.cpp (give item)
Gfx* gShovelGiveDL = gDampeShovelDL_mesh_001_opaque_dl;

// ============================================================================
// DRAW FUNCTION - Draws Shovel attached to BOTH hands
// Uses midpoint between hands for position, hand-to-hand vector for orientation
// ============================================================================

void CustomItems_DrawShovel(Player* player, PlayState* play) {
    if (!gCustomItemState.shovelAnimating && !gCustomItemState.shovelActive) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    // Get both hand positions
    Vec3f leftHand = player->bodyPartsPos[PLAYER_BODYPART_L_HAND];
    Vec3f rightHand = player->bodyPartsPos[PLAYER_BODYPART_R_HAND];

    // Calculate midpoint between both hands
    f32 midX = (leftHand.x + rightHand.x) * 0.5f;
    f32 midY = (leftHand.y + rightHand.y) * 0.5f;
    f32 midZ = (leftHand.z + rightHand.z) * 0.5f;

    // Calculate direction vector from right hand to left hand (shovel orientation)
    f32 dx = leftHand.x - rightHand.x;
    f32 dy = leftHand.y - rightHand.y;
    f32 dz = leftHand.z - rightHand.z;

    // Calculate yaw and pitch from hand-to-hand direction
    f32 shovelYaw = atan2f(dx, dz);
    f32 horizDist = sqrtf(dx * dx + dz * dz);
    f32 shovelPitch = atan2f(dy, horizDist);

    // Position at midpoint between hands
    Matrix_Translate(midX, midY, midZ, MTXMODE_NEW);

    // Apply orientation based on hand-to-hand vector
    Matrix_RotateY(shovelYaw, MTXMODE_APPLY);
    Matrix_RotateX(-shovelPitch, MTXMODE_APPLY);
    Matrix_RotateY(BINANG_TO_RAD(0x4000), MTXMODE_APPLY);

    Matrix_Scale(0.06f, 0.06f, 0.06f, MTXMODE_APPLY);

    gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, __FILE__, __LINE__),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, gDampeShovelDL_mesh_001_opaque_dl);

    CLOSE_DISPS(play->state.gfxCtx);
}
