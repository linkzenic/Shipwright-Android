/**
 * sm64_mario_render.c - Render libsm64 geometry using OOT display lists
 *
 * Single-pass render. Per vertex: position + UV (atlas-normalized × 32) +
 * color (libsm64's baked-lighting vertex color). One combiner:
 *     out = mix(SHADE, TEXEL0, TEXEL0_ALPHA) = (TEXEL0 − SHADE) * TEXEL0_A + SHADE
 * Matches libsm64's GL33 reference fragment shader line for line. When the
 * texel's alpha is 0 (body triangles sample UV=(1,1) which is an alpha=0
 * corner pixel), output = SHADE = vertex color → red overalls / blue shirt /
 * skin. When alpha > 0 (eyes, mustache, M-cap, buttons), output crossfades
 * to TEXEL0 → face features become visible.
 */

#include "z64.h"
#include "functions.h"

#define SM64_LIB_FN
#include "expansions/sm64/libsm64.h"

static u8* sMarioTextureAtlas = NULL;

void Sm64Render_SetTextureAtlas(u8* atlas) {
    sMarioTextureAtlas = atlas;
}

// =============================================================================
// Chrome sphere-map texture for metal cap. 32×32 RGBA32 grayscale gradient
// (bright spot top-center, fading to dark at the edges) — emulates the
// classic SM64 / OOT metal reflection where surface normals project onto a
// chrome ball. With G_TEXTURE_GEN_LINEAR, the RDP samples this texture
// using each vertex normal as the UV, producing a moving reflective sheen.
// Built once on first metal-cap render and reused.
// =============================================================================
#define SM64_CHROME_TEX_SIZE 32
static u8 sSm64ChromeTexture[SM64_CHROME_TEX_SIZE * SM64_CHROME_TEX_SIZE * 4];
static u8 sSm64ChromeTextureBuilt = 0;

static void buildChromeTexture(void) {
    if (sSm64ChromeTextureBuilt) return;
    s32 x, y;
    f32 cx = SM64_CHROME_TEX_SIZE * 0.5f;
    f32 cy = SM64_CHROME_TEX_SIZE * 0.4f;        // bright spot slightly above center (sun)
    f32 maxDist = SM64_CHROME_TEX_SIZE * 0.7f;
    for (y = 0; y < SM64_CHROME_TEX_SIZE; y++) {
        for (x = 0; x < SM64_CHROME_TEX_SIZE; x++) {
            f32 dx = (f32)x - cx;
            f32 dy = (f32)y - cy;
            f32 d = sqrtf(dx*dx + dy*dy) / maxDist;
            if (d > 1.0f) d = 1.0f;
            // Quadratic falloff: bright center 240, dark edge 80.
            s32 v = (s32)(240.0f - d * d * 160.0f);
            if (v < 80)  v = 80;
            if (v > 255) v = 255;
            s32 idx = (y * SM64_CHROME_TEX_SIZE + x) * 4;
            sSm64ChromeTexture[idx + 0] = (u8)v;     // R
            sSm64ChromeTexture[idx + 1] = (u8)v;     // G
            sSm64ChromeTexture[idx + 2] = (u8)((v * 230) / 255);  // B (slight cool tint)
            sSm64ChromeTexture[idx + 3] = 255;       // A (fully opaque, alpha modulated by combiner)
        }
    }
    sSm64ChromeTextureBuilt = 1;
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

// Single-pass triangle emission: position × SM64_SCALE, UV scaled into s10.5
// atlas-space texel coords, and libsm64's baked-lighting vertex color.
// The combiner (set up by the caller) decides per-pixel whether the texel
// (non-zero alpha) or vertex color (zero alpha) wins.
//
// `ox/oy/oz` is an OOT-scale translation added to every vertex. In normal
// play it's ≈ 0 (Mario was just ticked, mesh already sits at Link's pos).
// During a no-tick cutscene defer it's the delta between Link's current
// pos and Mario's last-ticked pos, so the stale mesh visually follows
// Link through the scripted cutscene moves.
//
// `vAlpha` (0-255) is the per-vertex alpha. 255 = fully opaque (normal
// path), <255 = translucent (vanish cap).
// `useXlu` selects which OOT display-list bucket the geometry submits to.
// XLU pass runs after OPA with proper alpha blending — required for
// translucent rendering. OPA pass treats geometry as opaque regardless
// of vertex alpha.
// `metalTint` (1 = metal cap active) overrides per-vertex colors with a
// chrome silver — libsm64's gfx_adapter strips the SM64 environment-mapped
// metal material when emitting the geometry buffer (only color + atlas UV
// survive), so we have to fake the metallic look on the OOT-side render.
// Body keeps its baked-in lighting falloff for shading; just the hue
// shifts to grayscale.
static void emitTrisSingle(PlayState* play, struct SM64MarioGeometryBuffers* buffers,
                           float ox, float oy, float oz, u8 vAlpha, u8 useXlu, u8 metalTint) {
    u16 numTris = buffers->numTrianglesUsed;
    float* pos = buffers->position;
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

            // libsm64 already baked directional lighting into col[] — do
            // not re-light on the N64 side or Mario would be double-lit.
            // Scale by SM64_BODY_BRIGHTNESS (0.8) to match the pre-darkened
            // atlas — otherwise the body would look brighter than the face
            // textures after atlas darkening.
            //
            // Metal tint: convert the per-vertex color to luminance
            // (0.30R + 0.59G + 0.11B), shift toward 220 (silver), and clamp.
            // Keeps the directional-lighting variation (so the metal still
            // shows surface curvature) but drops all chroma so Mario reads
            // as chrome.
            {
                u32 r = (u32)(col[vIdx + 0] * 255.0f);
                u32 g = (u32)(col[vIdx + 1] * 255.0f);
                u32 b = (u32)(col[vIdx + 2] * 255.0f);
                u32 ar = (r * SM64_BODY_BRIGHTNESS_NUM / SM64_BODY_BRIGHTNESS_DEN);
                u32 ag = (g * SM64_BODY_BRIGHTNESS_NUM / SM64_BODY_BRIGHTNESS_DEN);
                u32 ab = (b * SM64_BODY_BRIGHTNESS_NUM / SM64_BODY_BRIGHTNESS_DEN);
                if (metalTint) {
                    // Drop chroma from vertex color so the body shows as
                    // gray (instead of original red/blue) while still
                    // preserving the directional-lighting falloff (so the
                    // metal still has shading variation).
                    u32 lum = (30 * ar + 59 * ag + 11 * ab) / 100;
                    ar = lum;
                    ag = lum;
                    ab = lum;
                }
                vtx[vCount].v.cn[0] = (u8)ar;
                vtx[vCount].v.cn[1] = (u8)ag;
                vtx[vCount].v.cn[2] = (u8)ab;
            }
            vtx[vCount].v.cn[3] = vAlpha;
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

// =============================================================================
// Chrome-sheen second pass for metal cap. Uses G_TEXTURE_GEN_LINEAR — the
// RDP projects each vertex's normal onto the bound texture as if the model
// is wrapped in a chrome ball reflection. Output is added on top of the
// regular Mario pass via XLU additive blending, producing the classic
// SM64 / OOT metallic shimmer.
// =============================================================================
static void Sm64Render_DrawMetalSheen(PlayState* play, struct SM64MarioGeometryBuffers* buffers,
                                      float ox, float oy, float oz) {
    u16 numTris = buffers->numTrianglesUsed;
    float* pos = buffers->position;
    float* nrm = buffers->normal;
    Vtx* vtx;
    u16 vCount = 0;
    u16 i, v;

    if (numTris == 0 || nrm == NULL) return;

    OPEN_DISPS(play->state.gfxCtx);

    // XLU pass + additive blend: chrome adds to the base color.
    gDPPipeSync(POLY_XLU_DISP++);
    gDPSetCycleType(POLY_XLU_DISP++, G_CYC_1CYCLE);
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);

    // No backface culling, no fog, no built-in lighting (we already have
    // baked color from libsm64). G_TEXTURE_GEN + G_TEXTURE_GEN_LINEAR turn
    // on sphere-mapping — RDP derives texture UVs from each vertex normal.
    gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_CULL_BOTH | G_FOG);
    gSPSetGeometryMode(POLY_XLU_DISP++,
        G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPMatrix(POLY_XLU_DISP++, &gMtxClear, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    // Bind procedural chrome gradient (32×32 RGBA32). Loaded once via
    // buildChromeTexture(); same path as the main atlas binding.
    gSPTexture(POLY_XLU_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetTexturePersp(POLY_XLU_DISP++, G_TP_PERSP);
    gDPSetTextureFilter(POLY_XLU_DISP++, G_TF_BILERP);
    gDPSetTileCustom(POLY_XLU_DISP++, G_IM_FMT_RGBA, G_IM_SIZ_32b,
                     SM64_CHROME_TEX_SIZE, SM64_CHROME_TEX_SIZE, 0,
                     G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                     G_TX_NOLOD, G_TX_NOLOD);
    gDPSetTextureImage(POLY_XLU_DISP++, G_IM_FMT_RGBA, G_IM_SIZ_32b,
                       SM64_CHROME_TEX_SIZE, sSm64ChromeTexture);
    gDPLoadSync(POLY_XLU_DISP++);
    gDPLoadTile(POLY_XLU_DISP++, G_TX_LOADTILE, 0, 0,
                (SM64_CHROME_TEX_SIZE - 1) << 2, (SM64_CHROME_TEX_SIZE - 1) << 2);

    // Combiner: output = TEXEL0 (chrome gradient) with alpha from PRIMITIVE
    // controlling the additive intensity. Lower primitive alpha = subtler
    // sheen; higher = stronger reflection. ~140/255 = ~55% strength —
    // visible reflective look without washing out the base Mario.
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, 140);
    gDPSetCombineLERP(POLY_XLU_DISP++,
        // RGB = TEXEL0 (chrome gradient sample)
        0, 0, 0, TEXEL0,         0, 0, 0, PRIMITIVE,
        0, 0, 0, TEXEL0,         0, 0, 0, PRIMITIVE);

    vtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, MAX_BATCH_VERTS * sizeof(Vtx));

    for (i = 0; i < numTris; i++) {
        for (v = 0; v < 3; v++) {
            u32 vIdx = (i * 3 + v) * 3;
            float px = pos[vIdx + 0] * SM64_SCALE + ox;
            float py = pos[vIdx + 1] * SM64_SCALE + oy;
            float pz = pos[vIdx + 2] * SM64_SCALE + oz;

            vtx[vCount].n.ob[0] = (s16)px;
            vtx[vCount].n.ob[1] = (s16)py;
            vtx[vCount].n.ob[2] = (s16)pz;
            vtx[vCount].n.flag  = 0;
            // For G_TEXTURE_GEN_LINEAR we use the .n vertex variant
            // (signed normals). RDP reads n[0..2] as the normal vector
            // (-128..127), projects onto sphere → UV.
            vtx[vCount].n.n[0] = (s8)(nrm[vIdx + 0] * 127.0f);
            vtx[vCount].n.n[1] = (s8)(nrm[vIdx + 1] * 127.0f);
            vtx[vCount].n.n[2] = (s8)(nrm[vIdx + 2] * 127.0f);
            vtx[vCount].n.a    = 255;
            // tc[] is ignored when G_TEXTURE_GEN_LINEAR — RDP overwrites.
            vtx[vCount].n.tc[0] = 0;
            vtx[vCount].n.tc[1] = 0;
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

    CLOSE_DISPS(play->state.gfxCtx);
}

void Sm64Render_DrawMarioMesh(PlayState* play, struct SM64MarioGeometryBuffers* buffers,
                               float cx, float cy, float cz, u8 translucent, u8 metalTint) {
    // cx/cy/cz is an OOT-scale translation added to every vertex. The
    // caller (Sm64Mario_Draw) computes it as (linkPos - marioLastTickPos)
    // so the mesh can be reused across frames where libsm64 wasn't ticked.
    //
    // `translucent` (set by caller when the SM64 vanish cap is active)
    // switches submission to POLY_XLU_DISP with an alpha-blending render
    // mode + a combiner that takes alpha from SHADE — vertex alpha is
    // dropped to ~100/255 for the translucent ghost-look.
    if (buffers->numTrianglesUsed == 0 || sMarioTextureAtlas == NULL)
        return;

    OPEN_DISPS(play->state.gfxCtx);

    // Vanish-cap path: emit into XLU bucket so OOT's alpha-blend pass
    // composites Mario behind XLU actors. Opaque path stays in OPA bucket.
    Gfx** dispList = translucent ? &POLY_XLU_DISP : &POLY_OPA_DISP;

    // Belt-and-suspenders pipe + tile sync to absorb whatever state the
    // previous actor left behind (fixed the Zora's Fountain ImportTextureI4
    // crash, where Destructible Wall's tile leaked into our draw).
    gDPPipeSync((*dispList)++);

    // Metal cap uses 2-cycle so we can post-multiply the mix(SHADE, TEXEL0)
    // result by a silver PRIMITIVE color — that tints the textured face /
    // M-logo / buttons too, not just the SHADE-driven body. With 1-cycle
    // the textured pixels stayed bright (eyes/M-logo white) and Mario looked
    // "still very white". 2-cycle uniform tint = consistent metal look.
    gDPSetCycleType((*dispList)++, metalTint ? G_CYC_2CYCLE : G_CYC_1CYCLE);
    if (translucent) {
        gDPSetRenderMode((*dispList)++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
    } else if (metalTint) {
        // 2-cycle render mode for opaque metal pass.
        gDPSetRenderMode((*dispList)++, G_RM_PASS, G_RM_AA_ZB_OPA_SURF2);
    } else {
        gDPSetRenderMode((*dispList)++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    }

    // G_LIGHTING OFF: libsm64's gfx_adapter already bakes directional
    // lighting (0.5 + 0.5*max(0, dot(normal, light_dir))) into the vertex
    // colors it gives us. Enabling N64 lighting here would double-light.
    // G_SHADE + G_SHADING_SMOOTH reads the vertex color through to SHADE.
    //
    // No backface culling — libsm64 emits some geometry (like wing-cap
    // wings) as single-sided planes; with G_CULL_BACK the wings would
    // disappear when the camera is on the "wrong" side. Drawing both
    // faces costs ~2× fillrate but Mario's poly count is small (<2000
    // tris) so it's negligible. Also fixes the issue where mesh interior
    // walls flicker when geometry self-intersects.
    gSPClearGeometryMode((*dispList)++, G_LIGHTING | G_CULL_BOTH | G_FOG);
    gSPSetGeometryMode((*dispList)++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    gSPMatrix((*dispList)++, &gMtxClear, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    // Atlas binding (same for both paths).
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
    //   Metal: 2-cycle. Cycle 1 does the standard mix(SHADE, TEXEL0,
    //     TEXEL0_ALPHA), Cycle 2 multiplies that result by PRIMITIVE
    //     (silver). Net: every pixel of Mario gets silver-tinted —
    //     textures show through but desaturated and darkened for the
    //     metallic look. Textured pixels (face, M-logo, buttons) no
    //     longer leak white through.
    //   Translucent (vanish): 1-cycle mix with SHADE alpha for fade.
    //   Default: 1-cycle vanilla mix(SHADE, TEXEL0, TEXEL0_ALPHA).
    if (metalTint) {
        // Iron base tint — desaturate Mario to medium gray so the chrome
        // sheen overlay (second pass below) reads cleanly on top.
        // Primitive 130 multiplied with everything → uniform darkening
        // across body + textured face features. Sheen pass adds the
        // reflection highlights.
        gDPSetPrimColor((*dispList)++, 0, 0, 130, 130, 145, 255);
        gDPSetCombineLERP((*dispList)++,
            TEXEL0, SHADE, TEXEL0_ALPHA, SHADE,   0, 0, 0, 1,
            COMBINED, 0, PRIMITIVE, 0,            0, 0, 0, COMBINED);
    } else if (translucent) {
        gDPSetCombineLERP((*dispList)++,
            TEXEL0, SHADE, TEXEL0_ALPHA, SHADE,   0, 0, 0, SHADE,
            TEXEL0, SHADE, TEXEL0_ALPHA, SHADE,   0, 0, 0, SHADE);
    } else {
        gDPSetCombineLERP((*dispList)++,
            TEXEL0, SHADE, TEXEL0_ALPHA, SHADE,   0, 0, 0, 1,
            TEXEL0, SHADE, TEXEL0_ALPHA, SHADE,   0, 0, 0, 1);
    }

    CLOSE_DISPS(play->state.gfxCtx);

    // Vanish cap: vertex alpha = 100 (~40% transparency, ghostly translucent).
    // Normal: 255 (opaque).
    u8 vAlpha = translucent ? 100 : 255;
    emitTrisSingle(play, buffers, cx, cy, cz, vAlpha, translucent, metalTint);

    // ----- Metal cap: chrome sheen overlay pass -----
    // OOT-style metallic effect via sphere-mapping. Same Mario triangles
    // get rendered a second time with a chrome gradient texture and
    // G_TEXTURE_GEN_LINEAR — the RDP computes UVs from per-vertex normals,
    // sampling the chrome gradient like a metal-ball reflection.
    // Additive XLU pass adds the sheen ON TOP of the iron-tinted base
    // pass that just ran, giving Mario the classic SM64 metal-cap shine
    // (bright reflective highlights moving across his surface as he turns).
    if (metalTint) {
        buildChromeTexture();
        Sm64Render_DrawMetalSheen(play, buffers, cx, cy, cz);
    }
}
