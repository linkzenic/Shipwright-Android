/**
 * object_gustjar_pot.c - Gust Jar 3D model and draw functions
 *
 * Draws the jar in Link's hands when using the Gust Jar.
 * Model: Custom jar DLs (jar_body_dl, jar_decoration_dl)
 */
#include "objects/object_vase/object_vase.h"

extern Gfx jar_body_dl[];
extern Gfx jar_decoration_dl[];

static void GustJarPot_Draw(Player* player, PlayState* play) {
    if (!gCustomItemState.gustJarEquipped)
        return;

    OPEN_DISPS(play->state.gfxCtx);

    // 1. Calculate position — midpoint between Link's two hands (same pattern
    // as ball-and-chain bcBallPos). The carry pose puts both hands in front of
    // his chest; placing the jar at the midpoint makes it look held.
    Vec3f* leftHand = &player->bodyPartsPos[PLAYER_BODYPART_L_HAND];
    Vec3f* rightHand = &player->bodyPartsPos[PLAYER_BODYPART_R_HAND];
    Vec3f potPos;
    potPos.x = (leftHand->x + rightHand->x) * 0.5f;
    potPos.y = (leftHand->y + rightHand->y) * 0.5f;
    potPos.z = (leftHand->z + rightHand->z) * 0.5f;
    s16 yaw = player->actor.shape.rot.y;

    // 2. Setup render state (OPA for solid geometry)
    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    // 3. Transformation matrix — rotate with Link so the jar tracks his facing.
    Matrix_Translate(potPos.x, potPos.y, potPos.z, MTXMODE_NEW);
    Matrix_RotateY(BINANG_TO_RAD(yaw), MTXMODE_APPLY);
    Matrix_RotateX(DEG_TO_RAD(90.0f), MTXMODE_APPLY);
    Matrix_Scale(1.5f, 1.5f, 1.5f, MTXMODE_APPLY);

    gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, __FILE__, __LINE__),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    // 4. Draw body (gray/base)
    gSPDisplayList(POLY_OPA_DISP++, jar_body_dl);

    // 5. Draw decoration (recolorable) — color by SUCK/BLOW direction.
    //    SUCK = blue, BLOW = red. Brightness scales with heat (more charge →
    //    more saturated color) so the visual still feeds back the charge level.
    {
        f32 heat = (f32)gCustomItemState.gustJarHeatTimer / 300.0f;
        if (heat > 1.0f)
            heat = 1.0f;
        if (heat < 0.0f)
            heat = 0.0f;
        u8 r, g, b;
        if (gCustomItemState.gustJarBlowDir == 1 /* GUST_DIR_BLOW */) {
            // Red gradient (dim red → bright red as heat builds)
            r = (u8)(180 + 75 * heat);
            g = (u8)(40 * (1.0f - heat));
            b = (u8)(40 * (1.0f - heat));
        } else {
            // Blue gradient (dim blue → bright blue as heat builds)
            r = (u8)(60 * (1.0f - heat));
            g = (u8)(120 + 60 * heat);
            b = (u8)(200 + 55 * heat);
        }
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, r, g, b, 255);
    }

    gSPDisplayList(POLY_OPA_DISP++, jar_decoration_dl);

    CLOSE_DISPS(play->state.gfxCtx);
}