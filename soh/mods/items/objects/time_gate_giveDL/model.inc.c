/**
 * Gate of Time Model - Skyward Sword Style
 * Ornate circular gear with Triforce center and decorative rings
 * Blue/cyan base with golden Triforce accent
 */

#include "header.h"

// Color definitions
#define COL_BLUE_DARK_R   20    // Dark blue (outer rim)
#define COL_BLUE_DARK_G   60
#define COL_BLUE_DARK_B   120

#define COL_BLUE_MID_R    40    // Medium blue (main body)
#define COL_BLUE_MID_G    120
#define COL_BLUE_MID_B    180

#define COL_CYAN_R        80    // Cyan (decorative ring)
#define COL_CYAN_G        200
#define COL_CYAN_B        220

#define COL_TRIFORCE_R    200   // Light cyan (Triforce) - matches reference
#define COL_TRIFORCE_G    230
#define COL_TRIFORCE_B    255

#define COL_WHITE_R       255   // White glow (Triforce center)
#define COL_WHITE_G       255
#define COL_WHITE_B       255

#define DEPTH 10  // Half thickness

// Gate of Time vertices - Front face (z = +DEPTH)
static Vtx sGateVtxFront[] = {
    // ========== CENTER (Triforce area) ==========
    // Center point (white glow)
    VTX(   0,   0, DEPTH,  512, 512,  COL_WHITE_R, COL_WHITE_G, COL_WHITE_B, 0xFF), // 0 - center

    // Triforce vertices - top triangle (offset +1 to prevent z-fighting)
    VTX(   0,  35, DEPTH+1,  512, 512,  COL_TRIFORCE_R, COL_TRIFORCE_G, COL_TRIFORCE_B, 0xFF), // 1 - top
    VTX( -18,   5, DEPTH+1,  512, 512,  COL_TRIFORCE_R, COL_TRIFORCE_G, COL_TRIFORCE_B, 0xFF), // 2 - bottom-left
    VTX(  18,   5, DEPTH+1,  512, 512,  COL_TRIFORCE_R, COL_TRIFORCE_G, COL_TRIFORCE_B, 0xFF), // 3 - bottom-right

    // Triforce vertices - bottom-left triangle
    VTX( -18,   5, DEPTH+1,  512, 512,  COL_TRIFORCE_R, COL_TRIFORCE_G, COL_TRIFORCE_B, 0xFF), // 4
    VTX( -36, -25, DEPTH+1,  512, 512,  COL_TRIFORCE_R, COL_TRIFORCE_G, COL_TRIFORCE_B, 0xFF), // 5
    VTX(   0, -25, DEPTH+1,  512, 512,  COL_TRIFORCE_R, COL_TRIFORCE_G, COL_TRIFORCE_B, 0xFF), // 6

    // Triforce vertices - bottom-right triangle
    VTX(  18,   5, DEPTH+1,  512, 512,  COL_TRIFORCE_R, COL_TRIFORCE_G, COL_TRIFORCE_B, 0xFF), // 7
    VTX(   0, -25, DEPTH+1,  512, 512,  COL_TRIFORCE_R, COL_TRIFORCE_G, COL_TRIFORCE_B, 0xFF), // 8
    VTX(  36, -25, DEPTH+1,  512, 512,  COL_TRIFORCE_R, COL_TRIFORCE_G, COL_TRIFORCE_B, 0xFF), // 9

    // ========== INNER RING (cyan decorative) ==========
    VTX(  45,   0, DEPTH,  512, 512,  COL_CYAN_R, COL_CYAN_G, COL_CYAN_B, 0xFF), // 10
    VTX(  32,  32, DEPTH,  512, 512,  COL_CYAN_R, COL_CYAN_G, COL_CYAN_B, 0xFF), // 11
    VTX(   0,  45, DEPTH,  512, 512,  COL_CYAN_R, COL_CYAN_G, COL_CYAN_B, 0xFF), // 12
    VTX( -32,  32, DEPTH,  512, 512,  COL_CYAN_R, COL_CYAN_G, COL_CYAN_B, 0xFF), // 13
    VTX( -45,   0, DEPTH,  512, 512,  COL_CYAN_R, COL_CYAN_G, COL_CYAN_B, 0xFF), // 14
    VTX( -32, -32, DEPTH,  512, 512,  COL_CYAN_R, COL_CYAN_G, COL_CYAN_B, 0xFF), // 15
    VTX(   0, -45, DEPTH,  512, 512,  COL_CYAN_R, COL_CYAN_G, COL_CYAN_B, 0xFF), // 16
    VTX(  32, -32, DEPTH,  512, 512,  COL_CYAN_R, COL_CYAN_G, COL_CYAN_B, 0xFF), // 17

    // ========== MIDDLE RING (blue body) ==========
    VTX(  65,   0, DEPTH,  512, 512,  COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 18
    VTX(  46,  46, DEPTH,  512, 512,  COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 19
    VTX(   0,  65, DEPTH,  512, 512,  COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 20
    VTX( -46,  46, DEPTH,  512, 512,  COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 21
    VTX( -65,   0, DEPTH,  512, 512,  COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 22
    VTX( -46, -46, DEPTH,  512, 512,  COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 23
    VTX(   0, -65, DEPTH,  512, 512,  COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 24
    VTX(  46, -46, DEPTH,  512, 512,  COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 25

    // ========== OUTER RING (gear base) - 16 points for teeth ==========
    VTX(  80,   0, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 26
    VTX(  74,  31, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 27
    VTX(  57,  57, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 28
    VTX(  31,  74, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 29
    VTX(   0,  80, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 30
    VTX( -31,  74, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 31
    VTX( -57,  57, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 32
    VTX( -74,  31, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 33
    VTX( -80,   0, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 34
    VTX( -74, -31, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 35
    VTX( -57, -57, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 36
    VTX( -31, -74, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 37
    VTX(   0, -80, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 38
    VTX(  31, -74, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 39
    VTX(  57, -57, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 40
    VTX(  74, -31, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 41
};

// Gear teeth vertices (16 teeth) - Front face
static Vtx sTeethVtxFront[] = {
    // Tooth tips at radius 100, alternating with valleys at 80
    VTX( 100,   0, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 0
    VTX(  92,  38, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 1
    VTX(  71,  71, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 2
    VTX(  38,  92, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 3
    VTX(   0, 100, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 4
    VTX( -38,  92, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 5
    VTX( -71,  71, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 6
    VTX( -92,  38, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 7
    VTX(-100,   0, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 8
    VTX( -92, -38, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 9
    VTX( -71, -71, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 10
    VTX( -38, -92, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 11
    VTX(   0,-100, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 12
    VTX(  38, -92, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 13
    VTX(  71, -71, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 14
    VTX(  92, -38, DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 15
};

// Back face vertices (z = -DEPTH) - same structure, flipped normals
static Vtx sGateVtxBack[] = {
    // Center (white glow)
    VTX(   0,   0, -DEPTH,  512, 512,  COL_WHITE_R, COL_WHITE_G, COL_WHITE_B, 0xFF), // 0
    // Triforce top (offset -1 to prevent z-fighting)
    VTX(   0,  35, -DEPTH-1,  512, 512,  COL_TRIFORCE_R, COL_TRIFORCE_G, COL_TRIFORCE_B, 0xFF), // 1
    VTX( -18,   5, -DEPTH-1,  512, 512,  COL_TRIFORCE_R, COL_TRIFORCE_G, COL_TRIFORCE_B, 0xFF), // 2
    VTX(  18,   5, -DEPTH-1,  512, 512,  COL_TRIFORCE_R, COL_TRIFORCE_G, COL_TRIFORCE_B, 0xFF), // 3
    // Triforce bottom-left
    VTX( -18,   5, -DEPTH-1,  512, 512,  COL_TRIFORCE_R, COL_TRIFORCE_G, COL_TRIFORCE_B, 0xFF), // 4
    VTX( -36, -25, -DEPTH-1,  512, 512,  COL_TRIFORCE_R, COL_TRIFORCE_G, COL_TRIFORCE_B, 0xFF), // 5
    VTX(   0, -25, -DEPTH-1,  512, 512,  COL_TRIFORCE_R, COL_TRIFORCE_G, COL_TRIFORCE_B, 0xFF), // 6
    // Triforce bottom-right
    VTX(  18,   5, -DEPTH-1,  512, 512,  COL_TRIFORCE_R, COL_TRIFORCE_G, COL_TRIFORCE_B, 0xFF), // 7
    VTX(   0, -25, -DEPTH-1,  512, 512,  COL_TRIFORCE_R, COL_TRIFORCE_G, COL_TRIFORCE_B, 0xFF), // 8
    VTX(  36, -25, -DEPTH-1,  512, 512,  COL_TRIFORCE_R, COL_TRIFORCE_G, COL_TRIFORCE_B, 0xFF), // 9
    // Inner ring (cyan)
    VTX(  45,   0, -DEPTH,  512, 512,  COL_CYAN_R, COL_CYAN_G, COL_CYAN_B, 0xFF), // 10
    VTX(  32,  32, -DEPTH,  512, 512,  COL_CYAN_R, COL_CYAN_G, COL_CYAN_B, 0xFF), // 11
    VTX(   0,  45, -DEPTH,  512, 512,  COL_CYAN_R, COL_CYAN_G, COL_CYAN_B, 0xFF), // 12
    VTX( -32,  32, -DEPTH,  512, 512,  COL_CYAN_R, COL_CYAN_G, COL_CYAN_B, 0xFF), // 13
    VTX( -45,   0, -DEPTH,  512, 512,  COL_CYAN_R, COL_CYAN_G, COL_CYAN_B, 0xFF), // 14
    VTX( -32, -32, -DEPTH,  512, 512,  COL_CYAN_R, COL_CYAN_G, COL_CYAN_B, 0xFF), // 15
    VTX(   0, -45, -DEPTH,  512, 512,  COL_CYAN_R, COL_CYAN_G, COL_CYAN_B, 0xFF), // 16
    VTX(  32, -32, -DEPTH,  512, 512,  COL_CYAN_R, COL_CYAN_G, COL_CYAN_B, 0xFF), // 17
    // Middle ring (blue)
    VTX(  65,   0, -DEPTH,  512, 512,  COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 18
    VTX(  46,  46, -DEPTH,  512, 512,  COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 19
    VTX(   0,  65, -DEPTH,  512, 512,  COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 20
    VTX( -46,  46, -DEPTH,  512, 512,  COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 21
    VTX( -65,   0, -DEPTH,  512, 512,  COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 22
    VTX( -46, -46, -DEPTH,  512, 512,  COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 23
    VTX(   0, -65, -DEPTH,  512, 512,  COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 24
    VTX(  46, -46, -DEPTH,  512, 512,  COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 25
    // Outer ring (gear base)
    VTX(  80,   0, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 26
    VTX(  74,  31, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 27
    VTX(  57,  57, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 28
    VTX(  31,  74, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 29
    VTX(   0,  80, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 30
    VTX( -31,  74, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 31
    VTX( -57,  57, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 32
    VTX( -74,  31, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 33
    VTX( -80,   0, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 34
    VTX( -74, -31, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 35
    VTX( -57, -57, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 36
    VTX( -31, -74, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 37
    VTX(   0, -80, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 38
    VTX(  31, -74, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 39
    VTX(  57, -57, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 40
    VTX(  74, -31, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 41
};

// Back face teeth
static Vtx sTeethVtxBack[] = {
    VTX( 100,   0, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 0
    VTX(  92,  38, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 1
    VTX(  71,  71, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 2
    VTX(  38,  92, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 3
    VTX(   0, 100, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 4
    VTX( -38,  92, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 5
    VTX( -71,  71, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 6
    VTX( -92,  38, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 7
    VTX(-100,   0, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 8
    VTX( -92, -38, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 9
    VTX( -71, -71, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 10
    VTX( -38, -92, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 11
    VTX(   0,-100, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 12
    VTX(  38, -92, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 13
    VTX(  71, -71, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 14
    VTX(  92, -38, -DEPTH,  512, 512,  COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 15
};

Gfx g_timegate_dl[] = {
    gsSPClearGeometryMode(G_CULL_BACK | G_CULL_FRONT | G_LIGHTING | G_TEXTURE_GEN),
    gsSPSetGeometryMode(G_SHADE | G_SHADING_SMOOTH),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),

    // ========== FRONT FACE ==========
    gsSPVertex(sGateVtxFront, 42, 0),

    // Triforce (3 light cyan triangles) - CCW winding for front face
    gsSP1Triangle(1, 3, 2, 0),   // Top triangle (apex up)
    gsSP1Triangle(4, 6, 5, 0),   // Bottom-left triangle
    gsSP1Triangle(7, 9, 8, 0),   // Bottom-right triangle

    // Inner decorative ring (cyan) - center to inner ring
    gsSP2Triangles(0, 10, 11, 0,  0, 11, 12, 0),
    gsSP2Triangles(0, 12, 13, 0,  0, 13, 14, 0),
    gsSP2Triangles(0, 14, 15, 0,  0, 15, 16, 0),
    gsSP2Triangles(0, 16, 17, 0,  0, 17, 10, 0),

    // Inner to middle ring (cyan to blue)
    gsSP2Triangles(10, 18, 11, 0,  11, 18, 19, 0),
    gsSP2Triangles(11, 19, 12, 0,  12, 19, 20, 0),
    gsSP2Triangles(12, 20, 13, 0,  13, 20, 21, 0),
    gsSP2Triangles(13, 21, 14, 0,  14, 21, 22, 0),
    gsSP2Triangles(14, 22, 15, 0,  15, 22, 23, 0),
    gsSP2Triangles(15, 23, 16, 0,  16, 23, 24, 0),
    gsSP2Triangles(16, 24, 17, 0,  17, 24, 25, 0),
    gsSP2Triangles(17, 25, 10, 0,  10, 25, 18, 0),

    // Middle to outer ring (blue gradient)
    gsSP2Triangles(18, 26, 19, 0,  19, 26, 27, 0),
    gsSP2Triangles(19, 27, 28, 0,  19, 28, 20, 0),
    gsSP2Triangles(20, 28, 29, 0,  20, 29, 30, 0),
    gsSP2Triangles(20, 30, 21, 0,  21, 30, 31, 0),
    gsSP2Triangles(21, 31, 32, 0,  21, 32, 22, 0),
    gsSP2Triangles(22, 32, 33, 0,  22, 33, 34, 0),
    gsSP2Triangles(22, 34, 23, 0,  23, 34, 35, 0),
    gsSP2Triangles(23, 35, 36, 0,  23, 36, 24, 0),
    gsSP2Triangles(24, 36, 37, 0,  24, 37, 38, 0),
    gsSP2Triangles(24, 38, 25, 0,  25, 38, 39, 0),
    gsSP2Triangles(25, 39, 40, 0,  25, 40, 18, 0),
    gsSP2Triangles(18, 40, 41, 0,  18, 41, 26, 0),

    // ========== BACK FACE (reverse winding) ==========
    gsSPVertex(sGateVtxBack, 42, 0),

    // Triforce - CW winding for back face
    gsSP1Triangle(1, 2, 3, 0),
    gsSP1Triangle(4, 5, 6, 0),
    gsSP1Triangle(7, 8, 9, 0),

    // Inner ring
    gsSP2Triangles(0, 11, 10, 0,  0, 12, 11, 0),
    gsSP2Triangles(0, 13, 12, 0,  0, 14, 13, 0),
    gsSP2Triangles(0, 15, 14, 0,  0, 16, 15, 0),
    gsSP2Triangles(0, 17, 16, 0,  0, 10, 17, 0),

    // Inner to middle
    gsSP2Triangles(10, 11, 18, 0,  11, 19, 18, 0),
    gsSP2Triangles(11, 12, 19, 0,  12, 20, 19, 0),
    gsSP2Triangles(12, 13, 20, 0,  13, 21, 20, 0),
    gsSP2Triangles(13, 14, 21, 0,  14, 22, 21, 0),
    gsSP2Triangles(14, 15, 22, 0,  15, 23, 22, 0),
    gsSP2Triangles(15, 16, 23, 0,  16, 24, 23, 0),
    gsSP2Triangles(16, 17, 24, 0,  17, 25, 24, 0),
    gsSP2Triangles(17, 10, 25, 0,  10, 18, 25, 0),

    // Middle to outer
    gsSP2Triangles(18, 19, 26, 0,  19, 27, 26, 0),
    gsSP2Triangles(19, 28, 27, 0,  19, 20, 28, 0),
    gsSP2Triangles(20, 29, 28, 0,  20, 30, 29, 0),
    gsSP2Triangles(20, 21, 30, 0,  21, 31, 30, 0),
    gsSP2Triangles(21, 32, 31, 0,  21, 22, 32, 0),
    gsSP2Triangles(22, 33, 32, 0,  22, 34, 33, 0),
    gsSP2Triangles(22, 23, 34, 0,  23, 35, 34, 0),
    gsSP2Triangles(23, 36, 35, 0,  23, 24, 36, 0),
    gsSP2Triangles(24, 37, 36, 0,  24, 38, 37, 0),
    gsSP2Triangles(24, 25, 38, 0,  25, 39, 38, 0),
    gsSP2Triangles(25, 40, 39, 0,  25, 18, 40, 0),
    gsSP2Triangles(18, 41, 40, 0,  18, 26, 41, 0),

    // ========== OUTER EDGE (connecting front and back outer rings) ==========
    // Load both front and back outer rings
    gsSPVertex(&sGateVtxFront[26], 16, 0),  // Front outer ring at 0-15
    gsSPVertex(&sGateVtxBack[26], 16, 16),  // Back outer ring at 16-31

    gsSP2Triangles(0, 16, 1, 0,   1, 16, 17, 0),
    gsSP2Triangles(1, 17, 2, 0,   2, 17, 18, 0),
    gsSP2Triangles(2, 18, 3, 0,   3, 18, 19, 0),
    gsSP2Triangles(3, 19, 4, 0,   4, 19, 20, 0),
    gsSP2Triangles(4, 20, 5, 0,   5, 20, 21, 0),
    gsSP2Triangles(5, 21, 6, 0,   6, 21, 22, 0),
    gsSP2Triangles(6, 22, 7, 0,   7, 22, 23, 0),
    gsSP2Triangles(7, 23, 8, 0,   8, 23, 24, 0),
    gsSP2Triangles(8, 24, 9, 0,   9, 24, 25, 0),
    gsSP2Triangles(9, 25, 10, 0,  10, 25, 26, 0),
    gsSP2Triangles(10, 26, 11, 0, 11, 26, 27, 0),
    gsSP2Triangles(11, 27, 12, 0, 12, 27, 28, 0),
    gsSP2Triangles(12, 28, 13, 0, 13, 28, 29, 0),
    gsSP2Triangles(13, 29, 14, 0, 14, 29, 30, 0),
    gsSP2Triangles(14, 30, 15, 0, 15, 30, 31, 0),
    gsSP2Triangles(15, 31, 0, 0,  0, 31, 16, 0),

    // ========== GEAR TEETH ==========
    // Load front outer ring and teeth
    gsSPVertex(&sGateVtxFront[26], 16, 0),  // Outer ring base at 0-15
    gsSPVertex(sTeethVtxFront, 16, 16),      // Teeth tips at 16-31

    // Teeth triangles (alternating teeth pattern)
    gsSP2Triangles(0, 16, 1, 0,   1, 16, 17, 0),
    gsSP2Triangles(2, 18, 3, 0,   3, 18, 19, 0),
    gsSP2Triangles(4, 20, 5, 0,   5, 20, 21, 0),
    gsSP2Triangles(6, 22, 7, 0,   7, 22, 23, 0),
    gsSP2Triangles(8, 24, 9, 0,   9, 24, 25, 0),
    gsSP2Triangles(10, 26, 11, 0, 11, 26, 27, 0),
    gsSP2Triangles(12, 28, 13, 0, 13, 28, 29, 0),
    gsSP2Triangles(14, 30, 15, 0, 15, 30, 31, 0),

    // Back teeth
    gsSPVertex(&sGateVtxBack[26], 16, 0),
    gsSPVertex(sTeethVtxBack, 16, 16),

    gsSP2Triangles(0, 1, 16, 0,   1, 17, 16, 0),
    gsSP2Triangles(2, 3, 18, 0,   3, 19, 18, 0),
    gsSP2Triangles(4, 5, 20, 0,   5, 21, 20, 0),
    gsSP2Triangles(6, 7, 22, 0,   7, 23, 22, 0),
    gsSP2Triangles(8, 9, 24, 0,   9, 25, 24, 0),
    gsSP2Triangles(10, 11, 26, 0, 11, 27, 26, 0),
    gsSP2Triangles(12, 13, 28, 0, 13, 29, 28, 0),
    gsSP2Triangles(14, 15, 30, 0, 15, 31, 30, 0),

    // Teeth outer edges (connecting front and back teeth tips)
    gsSPVertex(sTeethVtxFront, 16, 0),
    gsSPVertex(sTeethVtxBack, 16, 16),

    gsSP2Triangles(0, 16, 1, 0,   1, 16, 17, 0),
    gsSP2Triangles(2, 18, 3, 0,   3, 18, 19, 0),
    gsSP2Triangles(4, 20, 5, 0,   5, 20, 21, 0),
    gsSP2Triangles(6, 22, 7, 0,   7, 22, 23, 0),
    gsSP2Triangles(8, 24, 9, 0,   9, 24, 25, 0),
    gsSP2Triangles(10, 26, 11, 0, 11, 26, 27, 0),
    gsSP2Triangles(12, 28, 13, 0, 13, 28, 29, 0),
    gsSP2Triangles(14, 30, 15, 0, 15, 30, 31, 0),

    gsSPEndDisplayList(),
};
