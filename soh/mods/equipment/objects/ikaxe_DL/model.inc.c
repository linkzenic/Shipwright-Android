/**
 * Iron Knuckle Axe DL - extracted from OOT decomp (object_ik)
 * Source: C:\Users\LENOVO\Documents\z_oot_decomp (object_ik)
 * DL: gIronKnuckleAxeDL (69 vtx)
 *
 * Segments 0x08/0x0A replaced with inline PrimColor/EnvColor DLs.
 * Textures from soh.otr (always available via OTR path).
 */

#include "align_asset_macro.h"

// OOT textures from soh.otr
#ifndef dgIKMetalTex2
#define dgIKMetalTex2 "__OTR__objects/object_ik/gIronKnuckleMetalTex"
static const ALIGN_ASSET(2) char gIKMetalTex2[] = dgIKMetalTex2;
#endif

#ifndef dgIKBlockPatternTex
#define dgIKBlockPatternTex "__OTR__objects/object_ik/gIronKnuckleBlockPatternTex"
static const ALIGN_ASSET(2) char gIKBlockPatternTex[] = dgIKBlockPatternTex;
#endif

#ifndef dgIKJewelTex
#define dgIKJewelTex "__OTR__objects/object_ik/gIronKnuckleJewelTex"
static const ALIGN_ASSET(2) char gIKJewelTex[] = dgIKJewelTex;
#endif

#ifndef dgIKTlut
#define dgIKTlut "__OTR__objects/object_ik/object_ik_Tlut_00F630"
static const ALIGN_ASSET(2) char gIKTlut[] = dgIKTlut;
#endif

#include "header.h"

// ============================================================================
// Segment 0x08 replacement: gold metal colors (params=0)
// func_80A761B0(gfxCtx, 245, 225, 155, 30, 30, 0)
// ============================================================================
static Gfx gfx_ikaxe_seg08[] = {
    gsDPPipeSync(),
    gsDPSetPrimColor(0, 0, 245, 225, 155, 255),
    gsDPSetEnvColor(30, 30, 0, 255),
    gsSPEndDisplayList(),
};

// ============================================================================
// Segment 0x0A replacement: white/silver metal colors (params=0)
// func_80A761B0(gfxCtx, 255, 255, 255, 20, 40, 30)
// ============================================================================
static Gfx gfx_ikaxe_seg0A[] = {
    gsDPPipeSync(),
    gsDPSetPrimColor(0, 0, 255, 255, 255, 255),
    gsDPSetEnvColor(20, 40, 30, 255),
    gsSPEndDisplayList(),
};

// ============================================================================
// Vertices (69) - from gIronKnuckleAxeVtx
// ============================================================================
static Vtx sIKAxeVtx[] = {
    VTX(    69,    263,  -3977,   0x2F7,   0x1E8, 0xAC, 0x54, 0xF6, 0xFF), // 0
    VTX(   720,    263,  -3977,   0x1D9,   0x207, 0x54, 0x54, 0xF6, 0xFF), // 1
    VTX(   395,    -62,  -5261,   0x242,   0x3F9, 0x00, 0x00, 0x88, 0xFF), // 2
    VTX(   395,    -62,  -5261,   0x242,   0x3F9, 0x00, 0x00, 0x88, 0xFF), // 3
    VTX(    69,   -388,  -3977,   0x2F7,   0x1E8, 0xAC, 0xAC, 0xF6, 0xFF), // 4
    VTX(    69,    263,  -3977,   0x2F7,   0x1E8, 0xAC, 0x54, 0xF6, 0xFF), // 5
    VTX(   720,   -388,  -3977,   0x1D9,   0x207, 0x54, 0xAC, 0xF6, 0xFF), // 6
    VTX(   720,    263,  -3977,   0x1D9,   0x207, 0x54, 0x54, 0xF6, 0xFF), // 7
    VTX(  2039,    -62,  -2927,    0x4A,   0x282, 0x67, 0x00, 0x3D, 0xFF), // 8
    VTX(  1494,   -141,  -3291,   0x111,   0x27C, 0x02, 0x89, 0x0B, 0xFF), // 9
    VTX(  1486,   -219,  -4149,    0xAA,   0x48F, 0x08, 0x89, 0xF8, 0xFF), // 10
    VTX(  1201,    -62,  -2034,   0x177,    0x68, 0xB2, 0x00, 0x5A, 0xFF), // 11
    VTX(  1494,     16,  -3291,   0x111,   0x27C, 0x02, 0x77, 0x0B, 0xFF), // 12
    VTX(  1109,    -62,  -2842,   0x177,   0x24F, 0xB0, 0x00, 0x59, 0xFF), // 13
    VTX(  1494,     16,  -3291,   0x111,   0x27C, 0x02, 0x77, 0x0B, 0xFF), // 14
    VTX(  1486,     94,  -4149,    0xAA,   0x48F, 0x08, 0x77, 0xF8, 0xFF), // 15
    VTX(  1494,   -141,  -3291,   0x111,   0x27C, 0x02, 0x89, 0x0B, 0xFF), // 16
    VTX(  1219,    -62,  -5346,    0xF5,   0x833, 0xCB, 0x00, 0x95, 0xFF), // 17
    VTX(  1910,    -62,  -4677,    0x31,   0x6A0, 0x68, 0x00, 0xC4, 0xFF), // 18
    VTX(  1016,    -62,  -4507,   0x156,   0x63A, 0xC1, 0x00, 0x9B, 0xFF), // 19
    VTX(  2207,    -62,  -3688,    -0x9,   0x44C, 0x77, 0x00, 0xF6, 0xFF), // 20
    VTX(  -411,    -62,  -2034,   0x37C,    0x68, 0x4E, 0x00, 0x5A, 0xFF), // 21
    VTX(  -704,   -141,  -3291,   0x3A0,   0x27C, 0xFE, 0x89, 0x0B, 0xFF), // 22
    VTX(  -319,    -62,  -2842,   0x33F,   0x24F, 0x50, 0x00, 0x59, 0xFF), // 23
    VTX(  -696,     94,  -4149,   0x3C4,   0x48F, 0xF8, 0x77, 0xF8, 0xFF), // 24
    VTX( -1249,    -62,  -2927,   0x466,   0x282, 0x99, 0x00, 0x3D, 0xFF), // 25
    VTX(  -704,     16,  -3291,   0x3A0,   0x27C, 0xFE, 0x77, 0x0B, 0xFF), // 26
    VTX(  -696,   -219,  -4149,   0x3C4,   0x48F, 0xF8, 0x89, 0xF8, 0xFF), // 27
    VTX(  -429,    -62,  -5346,   0x305,   0x833, 0x35, 0x00, 0x95, 0xFF), // 28
    VTX(  -226,    -62,  -4507,   0x2E3,   0x63A, 0x3F, 0x00, 0x9B, 0xFF), // 29
    VTX( -1120,    -62,  -4677,   0x3FB,   0x6A0, 0x98, 0x00, 0xC4, 0xFF), // 30
    VTX( -1417,    -62,  -3688,   0x47F,   0x44C, 0x89, 0x00, 0xF6, 0xFF), // 31
    VTX(   534,     77,  -3724,   0x1CC,   0x63C, 0x68, 0x3A, 0xFF, 0xFF), // 32
    VTX(   534,   -158,    877,   0x1CC,  -0x222, 0x68, 0xC6, 0x01, 0xFF), // 33
    VTX(   395,   -202,  -3724,   0x155,   0x63C, 0x00, 0x89, 0xFE, 0xFF), // 34
    VTX(   256,     77,  -3724,    0xDF,   0x63C, 0x98, 0x3A, 0xFF, 0xFF), // 35
    VTX(   256,   -158,    877,    0xDF,  -0x222, 0x98, 0xC6, 0x01, 0xFF), // 36
    VTX(   395,    120,    877,   0x155,  -0x222, 0x00, 0x77, 0x02, 0xFF), // 37
    VTX(  1109,    -62,  -2842,   0x1F0,   0x200, 0xB0, 0x00, 0x59, 0xFF), // 38
    VTX(   395,    150,  -3909,    0xF0,   -0x80, 0x00, 0x77, 0xF9, 0xFF), // 39
    VTX(   395,    -62,  -3519,   0x1F0,   -0x80, 0x00, 0x00, 0x78, 0xFF), // 40
    VTX(  1494,     16,  -3291,   0x158,   0x200, 0x02, 0x77, 0x0B, 0xFF), // 41
    VTX(  1486,     94,  -4149,    0xA8,   0x200, 0x08, 0x77, 0xF8, 0xFF), // 42
    VTX(  1016,    -62,  -4507,    0x10,   0x200, 0xC1, 0x00, 0x9B, 0xFF), // 43
    VTX(   395,    -62,  -4153,    0x10,   -0x80, 0x00, 0x00, 0x88, 0xFF), // 44
    VTX(   395,   -275,  -3909,    0xF0,   -0x80, 0x00, 0x89, 0xF9, 0xFF), // 45
    VTX(  1016,    -62,  -4507,    0x10,   0x200, 0xC1, 0x00, 0x9B, 0xFF), // 46
    VTX(  1486,   -219,  -4149,    0xA8,   0x200, 0x08, 0x89, 0xF8, 0xFF), // 47
    VTX(  1494,   -141,  -3291,   0x158,   0x200, 0x02, 0x89, 0x0B, 0xFF), // 48
    VTX(    69,    263,  -3977,   0x216,   0x1F3, 0xAC, 0x54, 0xF6, 0xFF), // 49
    VTX(   395,    -62,  -1414,   0x100,  -0x1FF, 0x00, 0x00, 0x78, 0xFF), // 50
    VTX(   720,    263,  -3977,   -0x16,   0x1F3, 0x54, 0x54, 0xF6, 0xFF), // 51
    VTX(    69,   -388,  -3977,   0x216,   0x1F3, 0xAC, 0xAC, 0xF6, 0xFF), // 52
    VTX(   720,   -388,  -3977,   -0x16,   0x1F3, 0x54, 0xAC, 0xF6, 0xFF), // 53
    VTX(  -319,    -62,  -2842,   0x1F0,   0x200, 0x50, 0x00, 0x59, 0xFF), // 54
    VTX(  -704,   -141,  -3291,   0x158,   0x200, 0xFE, 0x89, 0x0B, 0xFF), // 55
    VTX(  -696,   -219,  -4149,    0xA8,   0x200, 0xF8, 0x89, 0xF8, 0xFF), // 56
    VTX(  -226,    -62,  -4507,    0x10,   0x200, 0x3F, 0x00, 0x9B, 0xFF), // 57
    VTX(  -226,    -62,  -4507,    0x10,   0x200, 0x3F, 0x00, 0x9B, 0xFF), // 58
    VTX(  -696,     94,  -4149,    0xA8,   0x200, 0xF8, 0x77, 0xF8, 0xFF), // 59
    VTX(  -704,     16,  -3291,   0x158,   0x200, 0xFE, 0x77, 0x0B, 0xFF), // 60
    VTX(   395,    -19,    405,   0x239,   0x247, 0x00, 0x00, 0x88, 0xFF), // 61
    VTX(   752,   -376,   1385,   0x1ED,    0x20, 0x54, 0xAC, 0x05, 0xFF), // 62
    VTX(    38,   -376,   1385,    0x57,   0x1B5, 0xAC, 0xAC, 0x05, 0xFF), // 63
    VTX(   752,    338,   1385,    0x57,   0x1B5, 0x54, 0x54, 0x05, 0xFF), // 64
    VTX(   395,    -19,   2204,    0x39,   -0x39, 0x00, 0x00, 0x78, 0xFF), // 65
    VTX(   752,    338,   1385,    0x57,   0x1B5, 0x54, 0x54, 0x05, 0xFF), // 66
    VTX(    38,    338,   1385,   0x1ED,    0x20, 0xAC, 0x54, 0x05, 0xFF), // 67
    VTX(    38,   -376,   1385,    0x57,   0x1B5, 0xAC, 0xAC, 0x05, 0xFF), // 68
};

// ============================================================================
// Main DL
// ============================================================================
Gfx gIKAxeInlineDL[] = {
    // Part 1: Metal blade tip (env-mapped metal texture)
    gsDPPipeSync(),
    gsDPSetTextureLUT(G_TT_NONE),
    gsSPTexture(0x0BB8, 0x0BB8, 0, G_TX_RENDERTILE, G_ON),
    gsDPLoadTextureBlock_4b(gIKMetalTex2, G_IM_FMT_I, 32, 64,
        0, G_TX_MIRROR | G_TX_WRAP, G_TX_MIRROR | G_TX_WRAP, 5, 6, G_TX_NOLOD, G_TX_NOLOD),
    gsDPSetCombineLERP(PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, 0, COMBINED, 0, SHADE, 0, 0, 0, 0, 1),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_TEX_EDGE2),
    gsSPSetGeometryMode(G_CULL_BACK | G_FOG | G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR),
    gsDPSetPrimColor(0, 0, 255, 255, 255, 255),
    // Segment 0x08 inline: gold metal
    gsSPDisplayList(gfx_ikaxe_seg08),
    gsSPVertex(sIKAxeVtx, 3, 0),
    gsSP1Triangle(0, 1, 2, 0),
    gsSPVertex(&sIKAxeVtx[3], 5, 0),
    gsSP2Triangles(0, 1, 2, 0, 3, 0, 4, 0),
    gsSP1Triangle(1, 0, 3, 0),

    // Part 2: Double-blade faces (env-mapped)
    gsDPPipeSync(),
    // Segment 0x0A inline: silver/white metal
    gsSPDisplayList(gfx_ikaxe_seg0A),
    gsSPTexture(0x0DAC, 0x0DAC, 0, G_TX_RENDERTILE, G_ON),
    gsSPVertex(&sIKAxeVtx[8], 24, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 3, 1, 0),
    gsSP2Triangles(3, 4, 5, 0, 3, 0, 6, 0),
    gsSP2Triangles(0, 7, 6, 0, 3, 5, 8, 0),
    gsSP2Triangles(9, 10, 2, 0, 9, 11, 7, 0),
    gsSP2Triangles(2, 11, 9, 0, 7, 10, 9, 0),
    gsSP2Triangles(0, 12, 7, 0, 2, 12, 0, 0),
    gsSP2Triangles(10, 12, 2, 0, 7, 12, 10, 0),
    gsSP2Triangles(13, 14, 15, 0, 16, 17, 18, 0),
    gsSP2Triangles(17, 13, 18, 0, 13, 15, 18, 0),
    gsSP2Triangles(13, 17, 14, 0, 17, 19, 14, 0),
    gsSP2Triangles(20, 21, 19, 0, 20, 22, 16, 0),
    gsSP2Triangles(16, 21, 20, 0, 16, 23, 17, 0),
    gsSP2Triangles(19, 22, 20, 0, 17, 23, 19, 0),
    gsSP2Triangles(19, 23, 22, 0, 22, 23, 16, 0),

    // Part 3: Handle/shaft (block pattern texture)
    gsDPPipeSync(),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
    gsDPLoadTextureBlock(gIKBlockPatternTex, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16,
        0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_CLAMP, 4, 4, G_TX_NOLOD, G_TX_NOLOD),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0, 0, 0, 0, 1, COMBINED, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2),
    gsSPClearGeometryMode(G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR),
    gsDPSetPrimColor(0, 0, 255, 255, 255, 255),
    gsSPVertex(&sIKAxeVtx[32], 29, 0),
    gsSP2Triangles(0, 1, 2, 0, 3, 2, 4, 0),
    gsSP2Triangles(1, 4, 2, 0, 0, 3, 5, 0),
    gsSP2Triangles(3, 4, 5, 0, 1, 0, 5, 0),
    gsSP2Triangles(6, 7, 8, 0, 6, 9, 7, 0),
    gsSP2Triangles(9, 10, 7, 0, 10, 11, 7, 0),
    gsSP2Triangles(11, 12, 7, 0, 13, 12, 14, 0),
    gsSP2Triangles(13, 14, 15, 0, 13, 15, 16, 0),
    gsSP2Triangles(13, 16, 6, 0, 8, 13, 6, 0),
    gsSP2Triangles(17, 18, 19, 0, 17, 20, 18, 0),
    gsSP2Triangles(20, 21, 18, 0, 19, 18, 21, 0),
    gsSP2Triangles(22, 13, 8, 0, 22, 23, 13, 0),
    gsSP2Triangles(23, 24, 13, 0, 24, 25, 13, 0),
    gsSP2Triangles(25, 12, 13, 0, 7, 12, 26, 0),
    gsSP2Triangles(7, 26, 27, 0, 7, 27, 28, 0),
    gsSP2Triangles(7, 28, 22, 0, 8, 7, 22, 0),

    // Part 4: Jewel at base (CI8 texture with TLUT)
    gsDPPipeSync(),
    gsDPSetTextureLUT(G_TT_RGBA16),
    gsDPLoadTextureBlock(gIKJewelTex, G_IM_FMT_CI, G_IM_SIZ_8b, 16, 16,
        0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 4, 4, G_TX_NOLOD, G_TX_NOLOD),
    gsDPLoadTLUT_pal256(gIKTlut),
    gsSPVertex(&sIKAxeVtx[61], 8, 0),
    gsSP2Triangles(0, 1, 2, 0, 3, 1, 0, 0),
    gsSP2Triangles(3, 4, 1, 0, 2, 1, 4, 0),
    gsSP2Triangles(5, 6, 4, 0, 7, 4, 6, 0),
    gsSP2Triangles(7, 6, 0, 0, 0, 6, 5, 0),

    gsSPEndDisplayList(),
};
