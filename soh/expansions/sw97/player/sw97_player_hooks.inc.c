/**
 * sw97_player_hooks.c - Hat physics integration hooks for Player
 *
 * Original: z64proto/sw97 team (Spaceworld '97 Experience)
 * Adapted for Ship of Harkinian (Shipwright)
 *
 * This file provides the hook functions that integrate the SW97 hat physics
 * system into SOH's player drawing pipeline. Call points:
 *
 * 1. Sw97_HatPhysics_CaptureHead() — called from Player_PostLimbDrawGameplay
 *    when limbIndex == PLAYER_LIMB_HEAD, captures the head bone matrix
 *
 * 2. Sw97_HatPhysics_DrawAfterSkeleton() — called from Player_DrawGameplay
 *    after Player_DrawImpl, updates sphere centers and draws the physics hat
 */
#include "sw97_compat.h"
#include "sw97_config.h"
#include "../physics/z64physics.h"
#include "../physics/physics_data.h"

// Callback for Physics_DrawDynamicStrand — positions each hat limb's matrix
static void Sw97_HatPhysicsCallback(s32 limbIndex, void* arg1, void* arg2) {
    if (limbIndex != 0) {
        return;
    }

    Matrix_Put(sHatPhysicsStrand[gSaveContext.linkAge].head.mtxF);
    Matrix_Translate(sHatOffsets[gSaveContext.linkAge].x, sHatOffsets[gSaveContext.linkAge].y,
                     sHatOffsets[gSaveContext.linkAge].z, MTXMODE_APPLY);
    SW97_Matrix_RotateX_f(90.0f, MTXMODE_APPLY);
    SW97_Matrix_RotateY_f(sHatPhysicsStrand[gSaveContext.linkAge].rigidity.rot.y, MTXMODE_APPLY);
    SW97_Matrix_RotateZ_f(sHatPhysicsStrand[gSaveContext.linkAge].rigidity.rot.z, MTXMODE_APPLY);
}

/**
 * Called from Player_PostLimbDrawGameplay when limbIndex == PLAYER_LIMB_HEAD.
 * Captures the current matrix stack state as the head bone position for hat physics.
 */
void Sw97_HatPhysics_CaptureHead(void) {
    if (!SW97_HAT_PHYSICS()) {
        return;
    }

    Physics_GetHeadProperties(&sHatPhysicsStrand[gSaveContext.linkAge], &sHatOffsets[gSaveContext.linkAge], false);
}

/**
 * Called from Player_DrawGameplay after Player_DrawImpl returns.
 * Updates body collision spheres, floor position, and draws the physics hat.
 *
 * In SW97 this was inside Player_DrawImpl itself (z_player_lib.c:885-928).
 * Here we call it separately to avoid modifying Player_DrawImpl.
 */
void Sw97_HatPhysics_DrawAfterSkeleton(PlayState* play, Player* player) {
    if (!SW97_HAT_PHYSICS()) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);

    {
        s32 bgId;

        // Align collision spheres to player body parts
        sPhysicsSphereCenterList[0] = player->bodyPartsPos[PLAYER_BODYPART_HEAD];
        sPhysicsSphereCenterList[1] = player->bodyPartsPos[PLAYER_BODYPART_WAIST];
        sPhysicsSphereCenterList[2] = player->bodyPartsPos[PLAYER_BODYPART_L_FOREARM];
        sPhysicsSphereCenterList[3] = player->bodyPartsPos[PLAYER_BODYPART_L_HAND];
        sPhysicsSphereCenterList[4] = player->bodyPartsPos[PLAYER_BODYPART_R_FOREARM];
        sPhysicsSphereCenterList[5] = player->bodyPartsPos[PLAYER_BODYPART_R_HAND];

        // Update floor Y from collision
        sHatPhysicsStrand[gSaveContext.linkAge].info.floorY = BgCheck_EntityRaycastFloor4(
            &play->colCtx, &player->actor.floorPoly, &bgId, &player->actor, &player->actor.world.pos);

        // Draw the physics hat
        POLY_OPA_DISP =
            Physics_DrawDynamicStrand(play->state.gfxCtx, POLY_OPA_DISP, sHatPhysicsJoints,
                                      &sHatPhysicsStrand[gSaveContext.linkAge], Sw97_HatPhysicsCallback, play, NULL);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}
