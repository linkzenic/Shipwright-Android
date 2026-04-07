/**
 * Spirit Breastplate DL - Iron Knuckle armor from OOT decomp
 * Source: C:\Users\LENOVO\Documents\z_oot_decomp (object_ik)
 *
 * 4 separate DLs, each drawn with its own matrix using IK skeleton offsets.
 * Segments 0x08/0x09 replaced with inline PrimColor/EnvColor.
 * Textures from soh.otr (always available).
 */

#include "header.h"

// OOT textures from soh.otr
#define gIKMetalTex     "__OTR__objects/object_ik/gIronKnuckleMetalTex"
#define gIKChainMailTex "__OTR__objects/object_ik/gIronKnuckleChainMailTex"
#define gIKBigRivetTex  "__OTR__objects/object_ik/gIronKnuckleBigRivetTex"
#define gIKRivetTex     "__OTR__objects/object_ik/object_ik_Tex_011960"

// ============================================================================
// Front plate vertices (44) - object_ik_Vtx_011A60 (ORIGINAL)
// ============================================================================
static Vtx sFrontVtx[] = {
    VTX( 2288,  142,  654, 0x36C, -0x100, 0x1B, 0x2D, 0x6B, 0xFF),
    VTX( 2076, 1140,  289, 0x290,  -0x44, 0x14, 0x55, 0x52, 0xFF),
    VTX( 1780,  326,  706, 0x38B,   0xC4, 0x16, 0x31, 0x6B, 0xFF),
    VTX( 2076, 1140,  289, 0x290,  -0x44, 0x14, 0x55, 0x52, 0xFF),
    VTX( 1252, 1187,  410, 0x2D9,  0x29A, 0xF2, 0x6A, 0x35, 0xFF),
    VTX( 1780,  326,  706, 0x38B,   0xC4, 0x16, 0x31, 0x6B, 0xFF),
    VTX( 1780,  326, -706,  0x38,   0xC4, 0x16, 0x31, 0x95, 0xFF),
    VTX( 1252, 1187, -407,  0xEC,  0x29A, 0xEC, 0x5D, 0xB8, 0xFF),
    VTX( 2085, 1095, -307, 0x129,  -0x4B, 0x13, 0x62, 0xBE, 0xFF),
    VTX( 1780,  326, -706,  0x38,   0xC4, 0x16, 0x31, 0x95, 0xFF),
    VTX( 2288,  142, -654,  0x58, -0x100, 0x1B, 0x2D, 0x95, 0xFF),
    VTX(  -62, 1207,    3, 0x1E3,  0x72C, 0xFC, 0x77, 0x00, 0xFF),
    VTX(  128,  695,  980, 0x430,  0x683, 0xE4, 0x50, 0x54, 0xFF),
    VTX( 1290,  611,  654, 0x36C,  0x278, 0x03, 0x4B, 0x5D, 0xFF),
    VTX(  993,  957,    2, 0x1E3,  0x381, 0xBC, 0x62, 0x00, 0xFF),
    VTX( 1290,  611, -654,  0x58,  0x278, 0x03, 0x4C, 0xA4, 0xFF),
    VTX(  128,  695, -980, -0x6D,  0x683, 0xE4, 0x51, 0xAC, 0xFF),
    VTX(  -62, 1207,    3, 0x1E3,  0x72C, 0xFC, 0x77, 0x00, 0xFF),
    VTX(  128,  695, -980, -0x6D,  0x683, 0xE4, 0x51, 0xAC, 0xFF),
    VTX( -507,  923,    2, 0x1E3,  0x8B8, 0xBA, 0x61, 0x00, 0xFF),
    VTX( -109,  563, -896, -0x3A,  0x756, 0xC1, 0x3C, 0xAE, 0xFF),
    VTX(  128,  695,  980, 0x430,  0x683, 0xE4, 0x50, 0x54, 0xFF),
    VTX( -109,  563,  896, 0x3FE,  0x756, 0xC1, 0x3C, 0x52, 0xFF),
    VTX( 1780,  326, -706,  0x88,  0x242, 0x16, 0x31, 0x95, 0xFF),
    VTX(  181, -123,-1324,  0xDB,   0x22, 0x00, 0x32, 0x94, 0xFF),
    VTX( 1290,  611, -654, -0x5A,  0x1CC, 0x03, 0x4C, 0xA4, 0xFF),
    VTX(  128,  695, -980,-0x113,   0x70, 0xE4, 0x51, 0xAC, 0xFF),
    VTX( -109,  563, -896, -0xE0,   0x18, 0xC1, 0x3C, 0xAE, 0xFF),
    VTX( 1290,  611,  654, 0x104,   0xFC, 0x03, 0x4B, 0x5D, 0xFF),
    VTX( 1780,  326,  706, 0x205,   0x8E, 0x16, 0x31, 0x6B, 0xFF),
    VTX( 1252, 1187,  410, -0x11,   0x76, 0xF2, 0x6A, 0x35, 0xFF),
    VTX( 1252, 1187, -407, -0x12,   0x76, 0xEC, 0x5D, 0xB8, 0xFF),
    VTX( 1780,  326, -706, 0x205,   0x8E, 0x16, 0x31, 0x95, 0xFF),
    VTX( 1290,  611, -654, 0x104,   0xFC, 0x03, 0x4C, 0xA4, 0xFF),
    VTX(  993,  957,    2, 0x206,  0x127, 0xBC, 0x62, 0x00, 0xFF),
    VTX( 1290,  611,  654, 0x3A8,  0x12B, 0x03, 0x4B, 0x5D, 0xFF),
    VTX( 1252, 1187,  410, 0x30B,   0x6C, 0xF2, 0x6A, 0x35, 0xFF),
    VTX( 1252, 1187, -407, 0x100,   0x6C, 0xEC, 0x5D, 0xB8, 0xFF),
    VTX( 1290,  611, -654,  0x62,  0x12B, 0x03, 0x4C, 0xA4, 0xFF),
    VTX( 1290,  611,  654, -0x5A,  0x1CC, 0x03, 0x4B, 0x5D, 0xFF),
    VTX(  181, -123, 1324,  0xDB,   0x22, 0x00, 0x32, 0x6C, 0xFF),
    VTX( 1780,  326,  706,  0x88,  0x242, 0x16, 0x31, 0x6B, 0xFF),
    VTX(  128,  695,  980,-0x113,   0x70, 0xE4, 0x50, 0x54, 0xFF),
    VTX( -109,  563,  896, -0xE0,   0x18, 0xC1, 0x3C, 0x52, 0xFF),
};

// Back plate vertices (28) - object_ik_Vtx_011D20 (ORIGINAL)
static Vtx sBackVtx[] = {
    VTX(  216, -947,  723,  0x65, 0x7AB, 0xFA, 0x98, 0x3B, 0xFF),
    VTX(  216, -947, -723, 0x39B, 0x7AB, 0xFA, 0x98, 0xC5, 0xFF),
    VTX( 1255,-1299,   -2, 0x201, 0x4B3, 0x11, 0x8A, 0x00, 0xFF),
    VTX( 1794, -791,  670,  0x83, 0x329, 0x17, 0xC2, 0x64, 0xFF),
    VTX(  216, -947,  723,  0x65, 0x7AB, 0xFA, 0x98, 0x3B, 0xFF),
    VTX( 1255,-1299,   -2, 0x201, 0x4B3, 0x11, 0x8A, 0x00, 0xFF),
    VTX(  216, -947, -723, 0x39B, 0x7AB, 0xFA, 0x98, 0xC5, 0xFF),
    VTX( 1794, -791, -670, 0x37D, 0x329, 0x16, 0xC2, 0x9C, 0xFF),
    VTX(  216, -947, -723, 0x39B, 0x7AB, 0xFA, 0x98, 0xC5, 0xFF),
    VTX(  181, -123,-1324, 0x4F1, 0x7C4, 0x1C, 0xDA, 0x92, 0xFF),
    VTX( 1794, -791, -670, 0x37D, 0x329, 0x16, 0xC2, 0x9C, 0xFF),
    VTX( 1794, -791,  670,  0x83, 0x329, 0x17, 0xC2, 0x64, 0xFF),
    VTX(  181, -123, 1324, -0xF1, 0x7C4, 0x1C, 0xDA, 0x6E, 0xFF),
    VTX(  216, -947,  723,  0x65, 0x7AB, 0xFA, 0x98, 0x3B, 0xFF),
    VTX( 1780,  326, -706, 0x392, 0x333, 0x15, 0xFD, 0x8A, 0xFF),
    VTX( 2288,  142, -654, 0x374, 0x1C0, 0x0C, 0xFF, 0x89, 0xFF),
    VTX( 2281, -668, -648, 0x371, 0x1C5, 0x16, 0xC3, 0x9B, 0xFF),
    VTX( 2802, -861,   -1, 0x201,  0x48, 0x21, 0x8D, 0x00, 0xFF),
    VTX( 1255,-1299,   -2, 0x201, 0x4B3, 0x11, 0x8A, 0x00, 0xFF),
    VTX( 2281, -668,  645,  0x91, 0x1C5, 0x16, 0xC3, 0x64, 0xFF),
    VTX( 2288,  142,  654,  0x8C, 0x1C0, 0x0C, 0xFF, 0x77, 0xFF),
    VTX( 1780,  326,  706,  0x6E, 0x333, 0x15, 0xFD, 0x76, 0xFF),
    VTX( 1780,  326,  706,  0x88, 0x242, 0x15, 0xFD, 0x76, 0xFF),
    VTX(  181, -123, 1324,  0xDB,  0x22, 0x1C, 0xDA, 0x6E, 0xFF),
    VTX( 1794, -791,  670, 0x325, 0x1C6, 0x17, 0xC2, 0x64, 0xFF),
    VTX( 1794, -791, -670, 0x325, 0x1C6, 0x16, 0xC2, 0x9C, 0xFF),
    VTX(  181, -123,-1324,  0xDB,  0x22, 0x1C, 0xDA, 0x92, 0xFF),
    VTX( 1780,  326, -706,  0x88, 0x242, 0x15, 0xFD, 0x8A, 0xFF),
};

// Right pauldron vertices (14) - object_ik_Vtx_013C40 (ORIGINAL)
static Vtx sRightPauldronVtx[] = {
    VTX(   45,  164,   52, 0x196, 0x228, 0xFE, 0x77, 0xF5, 0xFF),
    VTX(  -78,  -49,  413, 0x113, 0x345, 0xFC, 0x3F, 0x66, 0xFF),
    VTX(  473,  105,  284, 0x31E, 0x289, 0x20, 0x64, 0x39, 0xFF),
    VTX(  473,  105,  284, 0x31E, 0x289, 0x20, 0x64, 0x39, 0xFF),
    VTX(  470, -613,  580, 0x2E4, 0x65A, 0x19, 0x26, 0x6F, 0xFF),
    VTX(  886,  -84,   52, 0x48F, 0x39A, 0x52, 0x56, 0xF7, 0xFF),
    VTX(  470, -613, -361, 0x2E4, 0x65A, 0x15, 0x1C, 0x8E, 0xFF),
    VTX( -126, -662, -314,  0xB7, 0x685, 0xF6, 0x0D, 0x8A, 0xFF),
    VTX(  -72,  -89, -254, 0x115, 0x37C, 0xFD, 0x33, 0x94, 0xFF),
    VTX(  -78,  -49,  413, 0x113, 0x345, 0xFC, 0x3F, 0x66, 0xFF),
    VTX( -239, -605,  522,  0x52, 0x633, 0xF7, 0x19, 0x74, 0xFF),
    VTX(  478,   65, -140, 0x320, 0x2C0, 0x1E, 0x5B, 0xB8, 0xFF),
    VTX(   45,  164,   52, 0x196, 0x228, 0xFE, 0x77, 0xF5, 0xFF),
    VTX(  473,  105,  284, 0x31E, 0x289, 0x20, 0x64, 0x39, 0xFF),
};

// Left pauldron vertices (14) - object_ik_Vtx_013F10 (ORIGINAL)
static Vtx sLeftPauldronVtx[] = {
    VTX(  473,  105, -284, 0x31E, 0x289, 0x20, 0x64, 0xC7, 0xFF),
    VTX(  -78,  -49, -413, 0x113, 0x345, 0xFC, 0x3F, 0x9A, 0xFF),
    VTX(   46,  164,  -52, 0x196, 0x228, 0xFE, 0x77, 0x0B, 0xFF),
    VTX(  886,  -84,  -52, 0x48F, 0x39A, 0x52, 0x56, 0x09, 0xFF),
    VTX(  470, -613, -580, 0x2E4, 0x65A, 0x19, 0x26, 0x91, 0xFF),
    VTX(  473,  105, -284, 0x31E, 0x289, 0x20, 0x64, 0xC7, 0xFF),
    VTX(  -72,  -89,  254, 0x115, 0x37C, 0xFD, 0x33, 0x6C, 0xFF),
    VTX( -126, -662,  314,  0xB7, 0x685, 0xF6, 0x0D, 0x76, 0xFF),
    VTX(  470, -613,  361, 0x2E4, 0x65A, 0x15, 0x1C, 0x72, 0xFF),
    VTX( -239, -605, -522,  0x52, 0x633, 0xF7, 0x19, 0x8C, 0xFF),
    VTX(  -78,  -49, -413, 0x113, 0x345, 0xFC, 0x3F, 0x9A, 0xFF),
    VTX(  478,   65,  140, 0x320, 0x2C0, 0x1E, 0x5B, 0x48, 0xFF),
    VTX(   46,  164,  -52, 0x196, 0x228, 0xFE, 0x77, 0x0B, 0xFF),
    VTX(  473,  105, -284, 0x31E, 0x289, 0x20, 0x64, 0xC7, 0xFF),
};

// Helmet marking vertices (51) - gIronKnuckleHelmetMarkingVtx (ORIGINAL)
static Vtx sHelmetMarkingVtx[] = {
    VTX(  976,  227,    0, 0x285, 0x74A, 0x6C, 0x32, 0x00, 0xFF),
    VTX( 1276, -251,    0, 0x590, -0x35, 0x77, 0xFF, 0x00, 0xFF),
    VTX(  901, -237, -353,-0x1AB, -0x5D, 0x46, 0x0C, 0xA0, 0xFF),
    VTX(  776,  227, -243,-0x1AB, 0x6F6, 0x4D, 0x28, 0xAD, 0xFF),
    VTX( 1276, -251,    0, 0x658, 0x25C, 0x77, 0xFF, 0x00, 0xFF),
    VTX(  976, -626, -246, -0x32,-0x1A7, 0x53, 0xF4, 0xAB, 0xFF),
    VTX(  901, -237, -353, -0xAF, 0x438, 0x46, 0x0C, 0xA0, 0xFF),
    VTX( 1276, -251,    0, 0x214, 0x5CC, 0x77, 0xFF, 0x00, 0xFF),
    VTX( 1101, -740,    0, 0x214,-0x1AE, 0x76, 0xED, 0x00, 0xFF),
    VTX(  976, -626, -246,-0x2BB,  0x58, 0x53, 0xF4, 0xAB, 0xFF),
    VTX(  976, -626,  246,-0x2BB,  0x58, 0x53, 0xF4, 0x55, 0xFF),
    VTX(  901, -237,  354, -0xAF, 0x438, 0x46, 0x0C, 0x60, 0xFF),
    VTX(  976, -626,  246, -0x32,-0x1A7, 0x53, 0xF4, 0x55, 0xFF),
    VTX(  776,  227,  243,-0x1AB, 0x6F6, 0x4D, 0x28, 0x53, 0xFF),
    VTX(  901, -237,  354,-0x1AB, -0x5D, 0x46, 0x0C, 0x60, 0xFF),
    VTX(  182, -797, -410, -0x17,-0x11A, 0x0C, 0xAB, 0xAD, 0xFF),
    VTX(   91, -634, -607, -0x66, 0x200, 0xFD, 0xD4, 0x91, 0xFF),
    VTX(  525, -495, -672, 0x34C, 0x1D9, 0x24, 0xDA, 0x95, 0xFF),
    VTX(  182, -797, -410, -0x49, 0x4F0, 0x0C, 0xAB, 0xAD, 0xFF),
    VTX(  525, -495, -672, 0x468, 0x1D2, 0x24, 0xDA, 0x95, 0xFF),
    VTX(  682, -770, -392, -0x49, -0xE6, 0x26, 0xBA, 0xA7, 0xFF),
    VTX(  525, -495, -672, 0x3A7,  0xCD, 0x24, 0xDA, 0x95, 0xFF),
    VTX(  840, -584, -382, -0x39, 0x292, 0x47, 0xDB, 0xA8, 0xFF),
    VTX(  682, -770, -392, -0x39, -0x62, 0x26, 0xBA, 0xA7, 0xFF),
    VTX(  525, -495, -672, 0x3AA, 0x177, 0x24, 0xDA, 0x95, 0xFF),
    VTX(  792, -294, -498, -0x40, 0x259, 0x3F, 0x05, 0x9B, 0xFF),
    VTX(  840, -584, -382, -0x40,-0x100, 0x47, 0xDB, 0xA8, 0xFF),
    VTX(   91, -634, -607, -0x49, 0x340, 0xFD, 0xD4, 0x91, 0xFF),
    VTX(  403, -168, -625, -0x49, -0xEF, 0x0D, 0x16, 0x8B, 0xFF),
    VTX(  525, -495, -672, 0x327, 0x107, 0x24, 0xDA, 0x95, 0xFF),
    VTX(  403, -168, -625, -0x55, 0x2DB, 0x0D, 0x16, 0x8B, 0xFF),
    VTX(  792, -294, -498, -0x55, -0xCD, 0x3F, 0x05, 0x9B, 0xFF),
    VTX(  525, -495, -672, 0x3DC, 0x131, 0x24, 0xDA, 0x95, 0xFF),
    VTX(  525, -495,  673, 0x3DC, 0x131, 0x24, 0xDA, 0x6B, 0xFF),
    VTX(  792, -294,  498, -0x55, -0xCD, 0x3F, 0x05, 0x65, 0xFF),
    VTX(  403, -168,  625, -0x55, 0x2DB, 0x0D, 0x16, 0x75, 0xFF),
    VTX(  525, -495,  673, 0x327, 0x107, 0x24, 0xDA, 0x6B, 0xFF),
    VTX(  403, -168,  625, -0x49, -0xEF, 0x0D, 0x16, 0x75, 0xFF),
    VTX(   91, -634,  607, -0x49, 0x340, 0xFD, 0xD4, 0x6F, 0xFF),
    VTX(  840, -584,  382, -0x40,-0x100, 0x47, 0xDB, 0x58, 0xFF),
    VTX(  792, -294,  498, -0x40, 0x259, 0x3F, 0x05, 0x65, 0xFF),
    VTX(  525, -495,  673, 0x3AA, 0x177, 0x24, 0xDA, 0x6B, 0xFF),
    VTX(  682, -770,  392, -0x39, -0x62, 0x26, 0xBA, 0x59, 0xFF),
    VTX(  840, -584,  382, -0x39, 0x292, 0x47, 0xDB, 0x58, 0xFF),
    VTX(  525, -495,  673, 0x3A7,  0xCD, 0x24, 0xDA, 0x6B, 0xFF),
    VTX(  682, -770,  392, -0x49, -0xE6, 0x26, 0xBA, 0x59, 0xFF),
    VTX(  525, -495,  673, 0x468, 0x1D2, 0x24, 0xDA, 0x6B, 0xFF),
    VTX(  182, -797,  410, -0x49, 0x4F0, 0x0C, 0xAB, 0x53, 0xFF),
    VTX(  525, -495,  673, 0x34C, 0x1D9, 0x24, 0xDA, 0x6B, 0xFF),
    VTX(   91, -634,  607, -0x66, 0x200, 0xFD, 0xD4, 0x6F, 0xFF),
    VTX(  182, -797,  410, -0x17,-0x11A, 0x0C, 0xAB, 0x53, 0xFF),
};

// ============================================================================
// Metal armor material setup (shared by all pieces)
// ============================================================================
#define SPIRIT_METAL_SETUP \
    gsDPPipeSync(), \
    gsDPSetTextureLUT(G_TT_NONE), \
    gsSPTexture(0x0BB8, 0x0BB8, 0, G_TX_RENDERTILE, G_ON), \
    gsDPLoadTextureBlock_4b(gIKMetalTex, G_IM_FMT_I, 32, 64, 0, \
                            G_TX_MIRROR | G_TX_WRAP, G_TX_MIRROR | G_TX_WRAP, \
                            5, 6, G_TX_NOLOD, G_TX_NOLOD), \
    gsDPSetCombineLERP(PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, PRIMITIVE, \
                       COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED), \
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2), \
    gsSPClearGeometryMode(G_CULL_BACK), \
    gsSPSetGeometryMode(G_FOG | G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR)

// ============================================================================
// DL 1: Chest plates (front + back) — IK offset (0, 0, 0)
// ============================================================================
Gfx gSpiritChestDL[] = {
    SPIRIT_METAL_SETUP,
    gsSPVertex(&sFrontVtx[0], 3, 0),
    gsSP1Triangle(0, 1, 2, 0),
    gsSPVertex(&sFrontVtx[3], 14, 0),
    gsSP2Triangles(0, 1, 2, 0, 3, 4, 5, 0),
    gsSP2Triangles(5, 1, 0, 0, 5, 4, 1, 0),
    gsSP2Triangles(6, 5, 7, 0, 8, 9, 10, 0),
    gsSP2Triangles(10, 11, 8, 0, 12, 13, 8, 0),
    gsSP1Triangle(11, 12, 8, 0),
    gsDPPipeSync(),
    gsSPVertex(&sFrontVtx[17], 6, 0),
    gsSP2Triangles(0, 1, 2, 0, 2, 1, 3, 0),
    gsSP2Triangles(2, 4, 0, 0, 5, 4, 2, 0),
    // Chain mail
    gsDPPipeSync(),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
    gsDPLoadTextureBlock(gIKChainMailTex, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0,
                         G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_CLAMP,
                         4, 4, G_TX_NOLOD, G_TX_NOLOD),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0, 0, 0, 0, PRIMITIVE,
                       COMBINED, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2),
    gsSPClearGeometryMode(G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR),
    gsSPVertex(&sFrontVtx[23], 21, 0),
    gsSP2Triangles(0, 1, 2, 0, 2, 1, 3, 0),
    gsSP2Triangles(1, 4, 3, 0, 5, 6, 7, 0),
    gsSP2Triangles(8, 9, 10, 0, 11, 12, 13, 0),
    gsSP2Triangles(14, 11, 13, 0, 15, 11, 14, 0),
    gsSP2Triangles(16, 17, 18, 0, 19, 17, 16, 0),
    gsSP1Triangle(19, 20, 17, 0),
    // Back plate
    gsDPPipeSync(),
    gsSPTexture(0x0BB8, 0x0BB8, 0, G_TX_RENDERTILE, G_ON),
    gsDPLoadTextureBlock_4b(gIKMetalTex, G_IM_FMT_I, 32, 64, 0,
                            G_TX_MIRROR | G_TX_WRAP, G_TX_MIRROR | G_TX_WRAP,
                            5, 6, G_TX_NOLOD, G_TX_NOLOD),
    gsDPSetCombineLERP(PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, PRIMITIVE,
                       COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2),
    gsSPClearGeometryMode(G_CULL_BACK),
    gsSPSetGeometryMode(G_FOG | G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR),
    gsSPVertex(&sBackVtx[0], 3, 0),
    gsSP1Triangle(0, 1, 2, 0),
    gsSPVertex(&sBackVtx[3], 5, 0),
    gsSP2Triangles(0, 1, 2, 0, 2, 3, 4, 0),
    gsDPPipeSync(),
    gsSPVertex(&sBackVtx[8], 14, 0),
    gsSP2Triangles(0, 1, 2, 0, 3, 4, 5, 0),
    gsSP2Triangles(6, 7, 8, 0, 2, 6, 8, 0),
    gsSP2Triangles(8, 9, 10, 0, 8, 10, 2, 0),
    gsSP2Triangles(3, 10, 11, 0, 10, 9, 11, 0),
    gsSP2Triangles(11, 12, 13, 0, 11, 13, 3, 0),
    gsDPPipeSync(),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
    gsDPLoadTextureBlock(gIKChainMailTex, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0,
                         G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_CLAMP,
                         4, 4, G_TX_NOLOD, G_TX_NOLOD),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0, 0, 0, 0, PRIMITIVE,
                       COMBINED, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2),
    gsSPClearGeometryMode(G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR),
    gsSPVertex(&sBackVtx[22], 6, 0),
    gsSP2Triangles(0, 1, 2, 0, 3, 4, 5, 0),
    gsSPEndDisplayList(),
};

// ============================================================================
// DL 2: Right pauldron — IK offset (+1900, 0, -1184)
// ============================================================================
Gfx gSpiritPauldronRDL[] = {
    SPIRIT_METAL_SETUP,
    gsSPVertex(&sRightPauldronVtx[0], 3, 0),
    gsSP1Triangle(0, 1, 2, 0),
    gsSPVertex(&sRightPauldronVtx[3], 11, 0),
    gsSP2Triangles(0, 1, 2, 0, 3, 4, 5, 0),
    gsSP2Triangles(6, 7, 1, 0, 2, 3, 8, 0),
    gsSP2Triangles(5, 8, 3, 0, 1, 0, 6, 0),
    gsSP2Triangles(9, 8, 5, 0, 10, 8, 9, 0),
    gsSP1Triangle(10, 2, 8, 0),
    gsSPEndDisplayList(),
};

// ============================================================================
// DL 3: Left pauldron — IK offset (+1900, 0, +1184)
// ============================================================================
Gfx gSpiritPauldronLDL[] = {
    SPIRIT_METAL_SETUP,
    gsSPVertex(&sLeftPauldronVtx[0], 3, 0),
    gsSP1Triangle(0, 1, 2, 0),
    gsSPVertex(&sLeftPauldronVtx[3], 11, 0),
    gsSP2Triangles(0, 1, 2, 0, 3, 4, 5, 0),
    gsSP2Triangles(1, 6, 7, 0, 8, 5, 0, 0),
    gsSP2Triangles(5, 8, 3, 0, 7, 2, 1, 0),
    gsSP2Triangles(3, 8, 9, 0, 9, 8, 10, 0),
    gsSP1Triangle(8, 0, 10, 0),
    gsSPEndDisplayList(),
};

// ============================================================================
// DL 4: Helmet marking — IK offset (+2100, -200, 0)
// ============================================================================
Gfx gSpiritHelmetMarkDL[] = {
    gsDPPipeSync(),
    gsDPSetTextureLUT(G_TT_NONE),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
    gsDPLoadTextureBlock(gIKBigRivetTex, G_IM_FMT_IA, G_IM_SIZ_8b, 32, 32, 0,
                         G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                         5, 5, G_TX_NOLOD, G_TX_NOLOD),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0, 0, 0, 0, PRIMITIVE,
                       COMBINED, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2),
    gsSPClearGeometryMode(G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR),
    gsSPSetGeometryMode(G_CULL_BACK | G_FOG | G_LIGHTING),
    gsSPVertex(&sHelmetMarkingVtx[0], 15, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsSP2Triangles(4, 5, 6, 0, 7, 8, 9, 0),
    gsSP2Triangles(10, 8, 7, 0, 11, 12, 4, 0),
    gsSP2Triangles(13, 14, 1, 0, 13, 1, 0, 0),
    gsDPPipeSync(),
    gsDPLoadTextureBlock(gIKRivetTex, G_IM_FMT_IA, G_IM_SIZ_8b, 16, 16, 0,
                         G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_WRAP,
                         4, 4, G_TX_NOLOD, G_TX_NOLOD),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2),
    gsSPVertex(&sHelmetMarkingVtx[15], 32, 0),
    gsSP2Triangles(0, 1, 2, 0, 3, 4, 5, 0),
    gsSP2Triangles(6, 7, 8, 0, 9, 10, 11, 0),
    gsSP2Triangles(12, 13, 14, 0, 15, 16, 17, 0),
    gsSP2Triangles(18, 19, 20, 0, 21, 22, 23, 0),
    gsSP2Triangles(24, 25, 26, 0, 27, 28, 29, 0),
    gsSPVertex(&sHelmetMarkingVtx[45], 6, 0),
    gsSP2Triangles(0, 1, 2, 0, 3, 4, 5, 0),
    gsSPEndDisplayList(),
};
