/**
 * sm64_mario_render.c - Render libsm64 geometry using OOT display lists
 *
 * Two-pass like libsm64's GL20 renderer:
 * Pass 1: All triangles with vertex colors only (body colors)
 * Pass 2: All triangles again with texture, alpha-blended on top
 *         (eyes, mustache, buttons show through; body stays same because texAlpha=0)
 */

#include "z64.h"
#include "functions.h"

#define SM64_LIB_FN
#include "expansions/sm64/libsm64.h"

static u8* sMarioTextureAtlas = NULL;

void Sm64Render_SetTextureAtlas(u8* atlas) {
    sMarioTextureAtlas = atlas;
}

#define SM64_SCALE 0.25f
#define MAX_BATCH_VERTS 30

static void emitTris(PlayState* play, struct SM64MarioGeometryBuffers* buffers, float cx, float cy, float cz,
                     u8 useTexture) {
    u16 numTris = buffers->numTrianglesUsed;
    float* pos = buffers->position;
    float* col = buffers->color;
    float* uv = buffers->uv;
    Vtx* vtx;
    u16 vCount = 0;
    u16 i, v;

    OPEN_DISPS(play->state.gfxCtx);

    vtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, MAX_BATCH_VERTS * sizeof(Vtx));

    for (i = 0; i < numTris; i++) {
        for (v = 0; v < 3; v++) {
            u32 vIdx = (i * 3 + v) * 3;
            u32 uvIdx = (i * 3 + v) * 2;
            float px = cx + (pos[vIdx + 0] - cx) * SM64_SCALE;
            float py = cy + (pos[vIdx + 1] - cy) * SM64_SCALE;
            float pz = cz + (pos[vIdx + 2] - cz) * SM64_SCALE;

            vtx[vCount].v.ob[0] = (s16)px;
            vtx[vCount].v.ob[1] = (s16)py;
            vtx[vCount].v.ob[2] = (s16)pz;
            vtx[vCount].v.flag = 0;

            if (useTexture) {
                vtx[vCount].v.tc[0] = (s16)(uv[uvIdx + 0] * SM64_TEXTURE_WIDTH * 32.0f);
                vtx[vCount].v.tc[1] = (s16)(uv[uvIdx + 1] * SM64_TEXTURE_HEIGHT * 32.0f);
                // White vertex color so texture shows pure
                vtx[vCount].v.cn[0] = 255;
                vtx[vCount].v.cn[1] = 255;
                vtx[vCount].v.cn[2] = 255;
                vtx[vCount].v.cn[3] = 255;
            } else {
                vtx[vCount].v.tc[0] = 0;
                vtx[vCount].v.tc[1] = 0;
                vtx[vCount].v.cn[0] = (u8)(col[vIdx + 0] * 255.0f);
                vtx[vCount].v.cn[1] = (u8)(col[vIdx + 1] * 255.0f);
                vtx[vCount].v.cn[2] = (u8)(col[vIdx + 2] * 255.0f);
                vtx[vCount].v.cn[3] = 255;
            }
            vCount++;
        }

        if (vCount >= MAX_BATCH_VERTS) {
            gSPVertex(POLY_OPA_DISP++, vtx, vCount, 0);
            for (u16 t = 0; t < vCount / 3; t++)
                gSP1Triangle(POLY_OPA_DISP++, t * 3, t * 3 + 1, t * 3 + 2, 0);
            vtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, MAX_BATCH_VERTS * sizeof(Vtx));
            vCount = 0;
        }
    }

    if (vCount > 0) {
        gSPVertex(POLY_OPA_DISP++, vtx, vCount, 0);
        for (u16 t = 0; t < vCount / 3; t++)
            gSP1Triangle(POLY_OPA_DISP++, t * 3, t * 3 + 1, t * 3 + 2, 0);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

void Sm64Render_DrawMarioMesh(PlayState* play, struct SM64MarioGeometryBuffers* buffers, float cx, float cy, float cz) {
    if (buffers->numTrianglesUsed == 0)
        return;

    OPEN_DISPS(play->state.gfxCtx);

    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BOTH | G_FOG);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK);
    gSPMatrix(POLY_OPA_DISP++, &gMtxClear, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    // === PASS 1: Vertex colors (body) ===
    gDPSetCombineLERP(POLY_OPA_DISP++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE);
    gSPTexture(POLY_OPA_DISP++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);

    CLOSE_DISPS(play->state.gfxCtx);

    emitTris(play, buffers, cx, cy, cz, 0);

    // === PASS 2: Texture alpha-blended on top ===
    if (sMarioTextureAtlas != NULL) {
        OPEN_DISPS(play->state.gfxCtx);

        gDPPipeSync(POLY_OPA_DISP++);

        // Switch to XLU for alpha blending
        // TEXEL0 with TEXEL0_ALPHA as output alpha
        gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0);
        gDPSetRenderMode(POLY_XLU_DISP++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
        gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_CULL_BOTH | G_FOG);
        gSPSetGeometryMode(POLY_XLU_DISP++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK);
        gSPMatrix(POLY_XLU_DISP++, &gMtxClear, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

        gSPTexture(POLY_XLU_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        gDPSetTexturePersp(POLY_XLU_DISP++, G_TP_PERSP);
        gDPSetTextureFilter(POLY_XLU_DISP++, G_TF_BILERP);

        gDPSetTileCustom(POLY_XLU_DISP++, G_IM_FMT_RGBA, G_IM_SIZ_32b, SM64_TEXTURE_WIDTH, SM64_TEXTURE_HEIGHT, 0,
                         G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        gDPSetTextureImage(POLY_XLU_DISP++, G_IM_FMT_RGBA, G_IM_SIZ_32b, SM64_TEXTURE_WIDTH, sMarioTextureAtlas);
        gDPLoadSync(POLY_XLU_DISP++);
        gDPLoadTile(POLY_XLU_DISP++, G_TX_LOADTILE, 0, 0, (SM64_TEXTURE_WIDTH - 1) << 2,
                    (SM64_TEXTURE_HEIGHT - 1) << 2);

        CLOSE_DISPS(play->state.gfxCtx);

        // Emit textured tris to XLU - but emitTris uses POLY_OPA_DISP.
        // Need to emit to XLU instead. Inline it:
        {
            u16 numTris = buffers->numTrianglesUsed;
            float* pos = buffers->position;
            float* uv = buffers->uv;
            Vtx* vtx;
            u16 vCount = 0;
            u16 i, v;

            OPEN_DISPS(play->state.gfxCtx);

            vtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, MAX_BATCH_VERTS * sizeof(Vtx));

            for (i = 0; i < numTris; i++) {
                for (v = 0; v < 3; v++) {
                    u32 vIdx = (i * 3 + v) * 3;
                    u32 uvIdx = (i * 3 + v) * 2;
                    float px = cx + (pos[vIdx + 0] - cx) * SM64_SCALE;
                    float py = cy + (pos[vIdx + 1] - cy) * SM64_SCALE;
                    float pz = cz + (pos[vIdx + 2] - cz) * SM64_SCALE;

                    vtx[vCount].v.ob[0] = (s16)px;
                    vtx[vCount].v.ob[1] = (s16)py;
                    vtx[vCount].v.ob[2] = (s16)pz;
                    vtx[vCount].v.flag = 0;
                    vtx[vCount].v.tc[0] = (s16)(uv[uvIdx + 0] * SM64_TEXTURE_WIDTH * 32.0f);
                    vtx[vCount].v.tc[1] = (s16)(uv[uvIdx + 1] * SM64_TEXTURE_HEIGHT * 32.0f);
                    vtx[vCount].v.cn[0] = 255;
                    vtx[vCount].v.cn[1] = 255;
                    vtx[vCount].v.cn[2] = 255;
                    vtx[vCount].v.cn[3] = 255;
                    vCount++;
                }

                if (vCount >= MAX_BATCH_VERTS) {
                    gSPVertex(POLY_XLU_DISP++, vtx, vCount, 0);
                    for (u16 t = 0; t < vCount / 3; t++)
                        gSP1Triangle(POLY_XLU_DISP++, t * 3, t * 3 + 1, t * 3 + 2, 0);
                    vtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, MAX_BATCH_VERTS * sizeof(Vtx));
                    vCount = 0;
                }
            }

            if (vCount > 0) {
                gSPVertex(POLY_XLU_DISP++, vtx, vCount, 0);
                for (u16 t = 0; t < vCount / 3; t++)
                    gSP1Triangle(POLY_XLU_DISP++, t * 3, t * 3 + 1, t * 3 + 2, 0);
            }

            gDPPipeSync(POLY_XLU_DISP++);

            CLOSE_DISPS(play->state.gfxCtx);
        }
    }
}
