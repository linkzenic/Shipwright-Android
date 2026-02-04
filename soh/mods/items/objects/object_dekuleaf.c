/**
 * object_dekuleaf.c - Deku Leaf 3D model and draw functions
 *
 * Draws the leaf when held and during gliding/swinging.
 * Model: Custom procedural leaf geometry from deku_leaf_giveDL/
 */

#include "z64.h"
#include "../custom_items.h"
#include "../logic/item_dekuleaf.h"
#include "deku_leaf_giveDL/header.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"

// Angle to radians conversion for s16 angles
#define DEKULEAF_ANGLE_TO_RAD  (M_PI / 0x8000)

static void DekuLeaf_SetupGeometryMode(GraphicsContext* gfxCtx) {
    OPEN_DISPS(gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_CULL_BACK | G_LIGHTING | G_TEXTURE_GEN);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    CLOSE_DISPS(gfxCtx);
}

static void DekuLeaf_RestoreGeometryMode(GraphicsContext* gfxCtx) {
    OPEN_DISPS(gfxCtx);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_CULL_BACK | G_LIGHTING);
    CLOSE_DISPS(gfxCtx);
}

static void DekuLeaf_DrawModel(PlayState* play, f32 posX, f32 posY, f32 posZ, s16 rotY, f32 scale) {
    OPEN_DISPS(play->state.gfxCtx);

    Matrix_Translate(posX, posY, posZ, MTXMODE_NEW);
    Matrix_RotateY((rotY * DEKULEAF_ANGLE_TO_RAD) + M_PI, MTXMODE_APPLY);
    // Counter-rotate X to restore original orientation (model vertices are rotated 90deg for giveDL)
    Matrix_RotateX(-M_PI / 2, MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);

    gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, __FILE__, __LINE__),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, g_dekuleaf_dl);

    CLOSE_DISPS(play->state.gfxCtx);
}

void CustomItems_DrawDekuLeaf(Player* p, PlayState* play) {
    if (!dlGliding && !dlBlowing) return;

    DekuLeaf_SetupGeometryMode(play->state.gfxCtx);

    if (dlGliding) {
        // Gliding: draw above Link at a fixed position
        f32 posX = p->actor.world.pos.x;
        f32 posY = p->actor.world.pos.y + 42.0f;
        f32 posZ = p->actor.world.pos.z;
        s16 rotY = p->actor.shape.rot.y;
        f32 scale = 0.2f;

        DekuLeaf_DrawModel(play, posX, posY, posZ, rotY, scale);
    } else if (dlBlowing) {
        // Blowing: draw attached to LEFT hand with frame-based scale
        Vec3f handPos = p->bodyPartsPos[PLAYER_BODYPART_L_HAND];
        s16 rotY = p->actor.shape.rot.y;

        // Determine scale based on current animation frame
        f32 scale;
        if (dlAnimTimer >= DEKULEAF_ATTACK_FRAME_START && dlAnimTimer <= DEKULEAF_ATTACK_FRAME_END) {
            scale = DEKULEAF_ATTACK_SCALE;
        } else {
            scale = DEKULEAF_HOLD_SCALE;
        }

        f32 forwardOffset = 3.0f;
        f32 posX = handPos.x + Math_SinS(rotY) * forwardOffset;
        f32 posY = handPos.y + 5.0f;
        f32 posZ = handPos.z + Math_CosS(rotY) * forwardOffset;

        DekuLeaf_DrawModel(play, posX, posY, posZ, rotY, scale);
    }

    DekuLeaf_RestoreGeometryMode(play->state.gfxCtx);
}
