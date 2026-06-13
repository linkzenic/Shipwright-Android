/**
 * sm64_mario_render.c - Render libsm64 geometry using OOT display lists
 *
 * Single-pass render for all states. Per vertex: position + UV (atlas-
 * normalized × 32) + color. One combiner:
 *     out = mix(SHADE, TEXEL0, TEXEL0_ALPHA) = (TEXEL0 − SHADE) * TEXEL0_A + SHADE
 * When the texel's alpha is 0 (body triangles sample UV=(1,1) which is an
 * alpha=0 corner pixel), output = SHADE = vertex color. When alpha > 0
 * (eyes, mustache, M-cap, buttons), output crossfades to TEXEL0.
 *
 * Metal cap: instead of rendering twice or using G_TEXTURE_GEN_LINEAR (which
 * libultraship doesn't honor reliably), we sphere-map sample the real SM64
 * metal envmap (atlas tile 0, decoded by libsm64 from the MIO0 ROM blob and
 * copied to sSm64MetalTex) once per vertex using the vertex normal as the UV.
 * The sampled chrome color is written to vtx.cn — so the standard combiner
 * sees SHADE = chrome envmap pixel. Result:
 *   - Body verts (atlas UV=(1,1), TEXEL0_A=0) → output = SHADE = chrome body.
 *   - Face/M-logo/eye verts (atlas UV in own tile, TEXEL0_A>0) → output =
 *     mix(chrome, atlas) — face features visible with a chrome tint.
 * Per-pixel sphere-map sampling in CPU is functionally identical to what
 * G_TEXTURE_GEN_LINEAR does on real N64 hardware — same texture data,
 * same UV-from-normal formula. No shaders.
 *
 * Wing cap: tile indices 9 and 10 are mario_texture_wings_half_1/2 (32×64
 * alpha-cutout textures). Vertices whose UV lands in those tiles get
 * SHADE.A=0 so the alpha-aware combiner kills the wing-edge halo. The body
 * sentinel UV=(1,1) is explicitly excluded — without this guard, every body
 * vert would be misclassified as wing (u=1.0 falls in tile 10 after clamp),
 * making the body invisible.
 */

#include "z64.h"
#include "functions.h"

#define SM64_LIB_FN
#include "expansions/sm64/libsm64.h"

static u8* sMarioTextureAtlas = NULL;

// =============================================================================
// Wing-cap halo: authoritative tile classification.
// Per libsm64 references/libsm64/src/load_tex_data.h: wings are tiles 9 & 10.
// =============================================================================
#define SM64_ATLAS_NUM_TILES   (704 / 64)  // 11 tiles
static u8 sWingTileMask[SM64_ATLAS_NUM_TILES] = { 0 };
static u8 sWingTilesDetected = 0;

// =============================================================================
// Metal Mario envmap (REAL SM64 texture). libsm64's load_tex_data.c decodes
// the MIO0 blob from the ROM at init and puts mario_texture_metal at tile 0
// of the atlas (64×32 RGBA32). We copy that sub-rect into a standalone buffer
// for fast per-vertex sphere-map sampling in CPU.
// =============================================================================
#define SM64_METAL_TEX_W 64
#define SM64_METAL_TEX_H 32
static u8 sSm64MetalTex[SM64_METAL_TEX_W * SM64_METAL_TEX_H * 4];
static u8 sSm64MetalTexBuilt = 0;

static void Sm64Render_ExtractMetalTextureFromAtlas(void) {
    s32 x, y;
    if (sMarioTextureAtlas == NULL) return;
    for (y = 0; y < SM64_METAL_TEX_H; y++) {
        for (x = 0; x < SM64_METAL_TEX_W; x++) {
            s32 src = (y * SM64_TEXTURE_WIDTH + x) * 4;
            s32 dst = (y * SM64_METAL_TEX_W   + x) * 4;
            sSm64MetalTex[dst + 0] = sMarioTextureAtlas[src + 0];
            sSm64MetalTex[dst + 1] = sMarioTextureAtlas[src + 1];
            sSm64MetalTex[dst + 2] = sMarioTextureAtlas[src + 2];
            sSm64MetalTex[dst + 3] = sMarioTextureAtlas[src + 3];
        }
    }
    sSm64MetalTexBuilt = 1;
}

static void Sm64Render_ClassifyAtlasTiles(void) {
    s32 t;
    for (t = 0; t < SM64_ATLAS_NUM_TILES; t++) sWingTileMask[t] = 0;
    sWingTileMask[9]  = 1;
    sWingTileMask[10] = 1;
    sWingTilesDetected = 1;
}

void Sm64Render_SetTextureAtlas(u8* atlas) {
    sMarioTextureAtlas = atlas;
    Sm64Render_ClassifyAtlasTiles();
    Sm64Render_ExtractMetalTextureFromAtlas();
}

// Returns 1 if (fu, fv) is a real wing-texture sample (tile 9 or 10) — NOT
// the body's alpha-corner sentinel at exact UV=(1,1). Without the body-
// sentinel guard, body verts (UV=(1,1)) would be misclassified as wing
// because u=1.0 clamps to tile 10.
static u8 Sm64Render_IsWingTile(f32 fu, f32 fv) {
    s32 tile;
    if (!sWingTilesDetected) return 0;
    // Body sentinel guard: anything at or above the atlas corner is body fill.
    if (fu >= 0.999f || fv >= 0.999f) return 0;
    tile = (s32)(fu * (f32)SM64_ATLAS_NUM_TILES);
    if (tile < 0) tile = 0;
    if (tile >= SM64_ATLAS_NUM_TILES) tile = SM64_ATLAS_NUM_TILES - 1;
    return sWingTileMask[tile];
}

// Must equal 1 / SM64_WORLD_SCALE in sm64_mario.c. libsm64 emits mesh vertices
// as absolute positions in the SM64-scale world (OOT coords × SM64_WORLD_SCALE),
// so multiplying by SM64_SCALE converts them back to OOT-scale absolute coords.
#define SM64_SCALE 0.25f
#define MAX_BATCH_VERTS 30

// Brightness multiplier applied to libsm64's baked vertex colors. Atlas
// pixels are pre-darkened at init time (sm64_mario.c Sm64_InitLibrary) so
// the two codepaths render at matching intensity.
#define SM64_BODY_BRIGHTNESS_NUM 4
#define SM64_BODY_BRIGHTNESS_DEN 5

// Per-vertex chrome lookup. Sphere-map UVs from the normal, sample the
// stored metal envmap. Same UV formula G_TEXTURE_GEN does on real N64:
//   u = (nx + 1) / 2,  v = (ny + 1) / 2
// Result is the RGB color of that pixel in the SM64 metal envmap.
static inline void Sm64Render_SampleChrome(const f32* nrm, u8* outR, u8* outG, u8* outB) {
    f32 sphU = (nrm[0] + 1.0f) * 0.5f;
    f32 sphV = (nrm[1] + 1.0f) * 0.5f;
    s32 mtx, mty, mtIdx;
    if (sphU < 0.0f) sphU = 0.0f; if (sphU > 1.0f) sphU = 1.0f;
    if (sphV < 0.0f) sphV = 0.0f; if (sphV > 1.0f) sphV = 1.0f;
    mtx = (s32)(sphU * (f32)(SM64_METAL_TEX_W - 1));
    mty = (s32)(sphV * (f32)(SM64_METAL_TEX_H - 1));
    mtIdx = (mty * SM64_METAL_TEX_W + mtx) * 4;
    *outR = sSm64MetalTex[mtIdx + 0];
    *outG = sSm64MetalTex[mtIdx + 1];
    *outB = sSm64MetalTex[mtIdx + 2];
}

// Single-pass triangle emission. Combiner is constant — vertex color (SHADE)
// drives the appearance for body verts (texel alpha=0), texture drives it
// for face/M-logo/eye verts (texel alpha>0). For metal cap, SHADE is the
// sphere-mapped chrome envmap sample; otherwise it's libsm64's baked
// directional lighting.
//
// `ox/oy/oz` — OOT-scale translation added to every vertex (cutscene defer).
// `vAlpha` (0-255) — per-vertex alpha. 100 = ghostly translucent (vanish).
// `useXlu` — OPA or XLU bucket.
// `metalActive` — sample chrome envmap per vertex into the SHADE color.
// `wingCapActive` — drop SHADE.A to 0 for wing-tile verts so the wing-cap
//   combiner kills the alpha-cutout halo.
static void emitTrisSingle(PlayState* play, struct SM64MarioGeometryBuffers* buffers,
                           float ox, float oy, float oz, u8 vAlpha, u8 useXlu,
                           u8 metalActive, u8 wingCapActive, u8 fireActive) {
    u16 numTris = buffers->numTrianglesUsed;
    float* pos = buffers->position;
    float* nrm = buffers->normal;
    float* col = buffers->color;
    float* uv  = buffers->uv;
    Vtx* vtx;
    u16 vCount = 0;
    u16 i, v;

    OPEN_DISPS(play->state.gfxCtx);

    vtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, MAX_BATCH_VERTS * sizeof(Vtx));

    for (i = 0; i < numTris; i++) {
        for (v = 0; v < 3; v++) {
            u32 vIdx  = (i * 3 + v) * 3;
            u32 uvIdx = (i * 3 + v) * 2;
            float px = pos[vIdx + 0] * SM64_SCALE + ox;
            float py = pos[vIdx + 1] * SM64_SCALE + oy;
            float pz = pos[vIdx + 2] * SM64_SCALE + oz;

            vtx[vCount].v.ob[0] = (s16)px;
            vtx[vCount].v.ob[1] = (s16)py;
            vtx[vCount].v.ob[2] = (s16)pz;
            vtx[vCount].v.flag  = 0;

            // N64 s10.5: normalized_uv × texel_count × 32.
            vtx[vCount].v.tc[0] = (s16)(uv[uvIdx + 0] * SM64_TEXTURE_WIDTH  * 32.0f);
            vtx[vCount].v.tc[1] = (s16)(uv[uvIdx + 1] * SM64_TEXTURE_HEIGHT * 32.0f);

            // Vertex color = SHADE.
            //   Default: libsm64's baked directional lighting × brightness.
            //   Metal:   sphere-mapped real metal envmap sample (chrome).
            if (metalActive && nrm != NULL && sSm64MetalTexBuilt) {
                u8 cr, cg, cb;
                Sm64Render_SampleChrome(&nrm[vIdx], &cr, &cg, &cb);
                vtx[vCount].v.cn[0] = cr;
                vtx[vCount].v.cn[1] = cg;
                vtx[vCount].v.cn[2] = cb;
            } else {
                u32 r = (u32)(col[vIdx + 0] * 255.0f);
                u32 g = (u32)(col[vIdx + 1] * 255.0f);
                u32 b = (u32)(col[vIdx + 2] * 255.0f);
                // Fire Mario (classic): only the CLOTHES change — cap+shirt RED →
                // WHITE, overalls BLUE → RED — and SKIN/face stay untouched. The
                // cloth materials are PURE red/blue (g,b≈0 or r,g≈0), so test by
                // RATIO (g*3 < r): skin is tan (r high but g≈193 → g*3 ≫ r), gloves
                // are white (r=g=b), shoes/hair are brown (mixed) — all excluded.
                // The kept channel's intensity preserves each vert's baked shading.
                if (fireActive) {
                    if (r > 50 && g * 3 < r && b * 3 < r) {
                        g = r;
                        b = r; // pure red (cap+shirt) → shaded white
                    } else if (b > 50 && r * 3 < b && g * 3 < b) {
                        r = b;
                        g = b / 6;
                        b = b / 6; // pure blue (overalls) → shaded red
                    }
                }
                u32 ar = (r * SM64_BODY_BRIGHTNESS_NUM / SM64_BODY_BRIGHTNESS_DEN);
                u32 ag = (g * SM64_BODY_BRIGHTNESS_NUM / SM64_BODY_BRIGHTNESS_DEN);
                u32 ab = (b * SM64_BODY_BRIGHTNESS_NUM / SM64_BODY_BRIGHTNESS_DEN);
                vtx[vCount].v.cn[0] = (u8)ar;
                vtx[vCount].v.cn[1] = (u8)ag;
                vtx[vCount].v.cn[2] = (u8)ab;
            }

            // Wing-cap halo fix. Only true wing-texture verts (tile 9 or 10,
            // and NOT the body's UV=(1,1) corner sentinel) get SHADE.A=0.
            if (wingCapActive && Sm64Render_IsWingTile((f32)uv[uvIdx + 0], (f32)uv[uvIdx + 1])) {
                vtx[vCount].v.cn[3] = 0;
            } else {
                vtx[vCount].v.cn[3] = vAlpha;
            }
            vCount++;
        }

        if (vCount >= MAX_BATCH_VERTS) {
            if (useXlu) {
                gSPVertex(POLY_XLU_DISP++, vtx, vCount, 0);
                for (u16 t = 0; t < vCount / 3; t++)
                    gSP1Triangle(POLY_XLU_DISP++, t * 3, t * 3 + 1, t * 3 + 2, 0);
            } else {
                gSPVertex(POLY_OPA_DISP++, vtx, vCount, 0);
                for (u16 t = 0; t < vCount / 3; t++)
                    gSP1Triangle(POLY_OPA_DISP++, t * 3, t * 3 + 1, t * 3 + 2, 0);
            }
            vtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, MAX_BATCH_VERTS * sizeof(Vtx));
            vCount = 0;
        }
    }

    if (vCount > 0) {
        if (useXlu) {
            gSPVertex(POLY_XLU_DISP++, vtx, vCount, 0);
            for (u16 t = 0; t < vCount / 3; t++)
                gSP1Triangle(POLY_XLU_DISP++, t * 3, t * 3 + 1, t * 3 + 2, 0);
        } else {
            gSPVertex(POLY_OPA_DISP++, vtx, vCount, 0);
            for (u16 t = 0; t < vCount / 3; t++)
                gSP1Triangle(POLY_OPA_DISP++, t * 3, t * 3 + 1, t * 3 + 2, 0);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

void Sm64Render_DrawMarioMesh(PlayState* play, struct SM64MarioGeometryBuffers* buffers,
                               float cx, float cy, float cz, u8 translucent, u8 metalTint, u8 wingCap,
                               u8 fireActive) {
    // Single-pass for all states. Standard combiner mix(SHADE, TEXEL0, T0_A).
    // emitTrisSingle decides what SHADE encodes per vertex:
    //   Normal: libsm64 baked lighting (red overalls / skin / blue shirt).
    //   Metal:  sphere-mapped real SM64 metal envmap sample (chrome).
    //   Wing:   same as normal, with SHADE.A=0 on wing-tile verts.
    //   Vanish: same as normal, all verts dropped to alpha=100 (ghost).
    if (buffers->numTrianglesUsed == 0 || sMarioTextureAtlas == NULL)
        return;

    // Bucket selection:
    //   Vanish:   XLU (translucent ghost — needs per-pixel alpha blend).
    //   Wing cap: OPA with TEX_EDGE alpha-cutout (Z-write enabled so the
    //             eyes correctly draw in front of the face, and back-of-head
    //             polygons occlude the nose when viewed from behind).
    //   Default / Metal: OPA.
    u8 useXluForMario = translucent;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx** dispList = useXluForMario ? &POLY_XLU_DISP : &POLY_OPA_DISP;

    // Belt-and-suspenders pipe sync to absorb whatever state the previous
    // actor left behind (fixed the Zora's Fountain ImportTextureI4 crash,
    // where Destructible Wall's tile leaked into our draw).
    gDPPipeSync((*dispList)++);
    gDPSetCycleType((*dispList)++, G_CYC_1CYCLE);

    if (translucent) {
        gDPSetRenderMode((*dispList)++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
    } else if (wingCap) {
        // TEX_EDGE: alpha-cutout render mode (CVG_X_ALPHA — combiner alpha
        // multiplies coverage). Wing-edge texels with combiner alpha=0 get
        // zero coverage and write nothing (no halo, no Z). All other pixels
        // (body, face, eyes, M-logo, wing fill) get full coverage and write
        // Z normally, so depth ordering works correctly.
        gDPSetRenderMode((*dispList)++, G_RM_AA_ZB_TEX_EDGE, G_RM_AA_ZB_TEX_EDGE2);
        gDPSetAlphaCompare((*dispList)++, G_AC_THRESHOLD);
        gDPSetBlendColor((*dispList)++, 0, 0, 0, 128);
    } else {
        gDPSetRenderMode((*dispList)++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    }

    // G_LIGHTING OFF — libsm64 already baked lighting (or we wrote chrome
    // directly). No backface culling — single-sided wings.
    gSPClearGeometryMode((*dispList)++, G_LIGHTING | G_CULL_BOTH | G_FOG);
    gSPSetGeometryMode((*dispList)++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    gSPMatrix((*dispList)++, &gMtxClear, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    // Atlas binding (Mario's face / M-logo / eyes / buttons / wings live here).
    gSPTexture((*dispList)++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetTexturePersp((*dispList)++, G_TP_PERSP);
    gDPSetTextureFilter((*dispList)++, G_TF_BILERP);
    gDPSetTileCustom((*dispList)++, G_IM_FMT_RGBA, G_IM_SIZ_32b,
                     SM64_TEXTURE_WIDTH, SM64_TEXTURE_HEIGHT, 0,
                     G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                     G_TX_NOLOD, G_TX_NOLOD);
    gDPSetTextureImage((*dispList)++, G_IM_FMT_RGBA, G_IM_SIZ_32b,
                       SM64_TEXTURE_WIDTH, sMarioTextureAtlas);
    gDPLoadSync((*dispList)++);
    gDPLoadTile((*dispList)++, G_TX_LOADTILE, 0, 0,
                (SM64_TEXTURE_WIDTH - 1) << 2, (SM64_TEXTURE_HEIGHT - 1) << 2);

    // Combiner.
    //   Translucent (vanish): mix(SHADE, TEXEL0, T0_A) with SHADE alpha.
    //   Wing cap: alpha-aware — output alpha = clamp(TEXEL0_A + SHADE_A).
    //   Default + metal: vanilla mix(SHADE, TEXEL0, T0_A). For metal,
    //     emitTrisSingle has set SHADE = sphere-mapped envmap pixel.
    if (translucent) {
        gDPSetCombineLERP((*dispList)++,
            TEXEL0, SHADE, TEXEL0_ALPHA, SHADE,   0, 0, 0, SHADE,
            TEXEL0, SHADE, TEXEL0_ALPHA, SHADE,   0, 0, 0, SHADE);
    } else if (wingCap) {
        gDPSetEnvColor((*dispList)++, 0, 0, 0, 255);
        gDPSetCombineLERP((*dispList)++,
            TEXEL0, SHADE, TEXEL0_ALPHA, SHADE,   TEXEL0, 0, ENVIRONMENT, SHADE,
            TEXEL0, SHADE, TEXEL0_ALPHA, SHADE,   TEXEL0, 0, ENVIRONMENT, SHADE);
    } else {
        gDPSetCombineLERP((*dispList)++,
            TEXEL0, SHADE, TEXEL0_ALPHA, SHADE,   0, 0, 0, 1,
            TEXEL0, SHADE, TEXEL0_ALPHA, SHADE,   0, 0, 0, 1);
    }

    CLOSE_DISPS(play->state.gfxCtx);

    u8 vAlpha = translucent ? 100 : 255;
    emitTrisSingle(play, buffers, cx, cy, cz, vAlpha, useXluForMario, metalTint, wingCap, fireActive);
}
