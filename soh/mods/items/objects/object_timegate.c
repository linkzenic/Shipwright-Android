/**
 * object_timegate.c - Time Gate draw functions
 *
 * Draws the time gate item in Link's hand during casting
 * and the blue warp portal effect on the ground.
 */

#include "z64.h"
#include "../custom_items.h"
#include "../logic/item_time_gate.h"
#include "macros.h"
#include "functions.h"
#include "objects/object_warp1/object_warp1.h"
#include "time_gate_giveDL/header.h"

// Portal animation state (local to avoid cluttering CustomItemState)
static f32 sPortalScrollOffset = 0.0f;

/**
 * Draw the time gate item in Link's hand during casting animation
 */
void CustomItems_DrawTimeGate(Player* player, PlayState* play) {
    if (!tgItemVisible)
        return;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    // Position at Link's left hand (he's placing the item)
    Vec3f handPos = player->bodyPartsPos[PLAYER_BODYPART_L_HAND];

    f32 forwardOffset = 5.0f;
    f32 downOffset = -10.0f; // Lower it towards the ground

    Matrix_Translate(handPos.x + Math_SinS(player->actor.shape.rot.y) * forwardOffset, handPos.y + downOffset,
                     handPos.z + Math_CosS(player->actor.shape.rot.y) * forwardOffset, MTXMODE_NEW);

    // Rotate to face forward and tilt slightly
    Matrix_RotateY(BINANG_TO_RAD(player->actor.shape.rot.y), MTXMODE_APPLY);
    Matrix_RotateX(BINANG_TO_RAD(-0x1000), MTXMODE_APPLY); // Tilt forward

    // Scale appropriate for hand-held size
    Matrix_Scale(0.008f, 0.008f, 0.008f, MTXMODE_APPLY);

    gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, __FILE__, __LINE__),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, g_timegate_dl);

    CLOSE_DISPS(play->state.gfxCtx);
}

/**
 * Draw the blue warp portal on the ground
 * Based on DoorWarp1_DrawWarp but simplified for our use case
 */
void CustomItems_DrawTimeGatePortal(Player* player, PlayState* play) {
    if (!tgPortalActive || tgPortalAlpha <= 0.0f)
        return;

    OPEN_DISPS(play->state.gfxCtx);

    // Update scroll animation
    sPortalScrollOffset += 15.0f;
    if (sPortalScrollOffset > 512.0f)
        sPortalScrollOffset -= 512.0f;

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);

    // Blue portal colors (time-themed, slightly purple tint)
    u8 alpha = (u8)tgPortalAlpha;
    gDPSetPrimColor(POLY_XLU_DISP++, 0x00, 0x80, 180, 200, 255, alpha);
    gDPSetEnvColor(POLY_XLU_DISP++, 50, 100, 255, 255);

    gDPSetColorDither(POLY_XLU_DISP++, G_CD_DISABLE);
    gDPSetColorDither(POLY_XLU_DISP++, G_AD_NOTPATTERN | G_CD_MAGICSQ);

    // Position portal at Link's feet
    Vec3f portalPos = player->actor.world.pos;
    portalPos.y += 1.0f; // Slightly above ground

    Matrix_Translate(portalPos.x, portalPos.y, portalPos.z, MTXMODE_NEW);

    gSPSegment(POLY_XLU_DISP++, 0x0A, MATRIX_NEWMTX(play->state.gfxCtx));
    Matrix_Push();

    // Setup texture scrolling
    u32 scrollTime = (u32)sPortalScrollOffset;
    gSPSegment(POLY_XLU_DISP++, 0x08,
               Gfx_TwoTexScroll(play->state.gfxCtx, 0, scrollTime & 0xFF, -((s16)(scrollTime * 2) & 511), 0x100, 0x100,
                                1, scrollTime & 0xFF, -((s16)(scrollTime * 2) & 511), 0x100, 0x100));

    // Scale the portal with grow effect
    f32 baseScale = 0.8f * tgPortalScale;   // Slightly smaller than boss warp
    f32 heightScale = 0.3f * tgPortalScale; // Lower height

    Matrix_Translate(0.0f, heightScale * 230.0f, 0.0f, MTXMODE_APPLY);
    Matrix_Scale(baseScale, 1.0f, baseScale, MTXMODE_APPLY);

    gSPSegment(POLY_XLU_DISP++, 0x09, MATRIX_NEWMTX(play->state.gfxCtx));
    gSPDisplayList(POLY_XLU_DISP++, gWarpPortalDL);

    Matrix_Pop();

    // Draw light rays (inner part) with slightly different scroll
    if (tgPortalAlpha > 128.0f) {
        f32 rayAlpha = (tgPortalAlpha - 128.0f) * 2.0f;
        if (rayAlpha > 255.0f)
            rayAlpha = 255.0f;

        gDPSetPrimColor(POLY_XLU_DISP++, 0x00, 0x80, 200, 220, 255, (u8)rayAlpha);
        gDPSetEnvColor(POLY_XLU_DISP++, 100, 150, 255, 255);

        u32 scrollTime2 = scrollTime * 2;
        gSPSegment(POLY_XLU_DISP++, 0x08,
                   Gfx_TwoTexScroll(play->state.gfxCtx, 0, scrollTime2 & 0xFF, -((s16)scrollTime & 511), 0x100, 0x100,
                                    1, scrollTime2 & 0xFF, -((s16)scrollTime & 511), 0x100, 0x100));

        f32 innerScale = 0.6f * tgPortalScale;
        Matrix_Translate(0.0f, heightScale * 60.0f, 0.0f, MTXMODE_APPLY);
        Matrix_Scale(innerScale, 1.0f, innerScale, MTXMODE_APPLY);

        gSPSegment(POLY_XLU_DISP++, 0x09, MATRIX_NEWMTX(play->state.gfxCtx));
        gSPDisplayList(POLY_XLU_DISP++, gWarpPortalDL);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}
