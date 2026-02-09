/**
 * Desire Sensor (Wyvern Gem) Model
 * 8-pointed star gem with corner chevrons
 * Blue crystalline appearance with bright center
 */

#include "header.h"

// Color definitions
#define COL_WHITE_R 255 // Center glow
#define COL_WHITE_G 255
#define COL_WHITE_B 255

#define COL_BLUE_BRIGHT_R 120 // Inner rays
#define COL_BLUE_BRIGHT_G 180
#define COL_BLUE_BRIGHT_B 255

#define COL_BLUE_MID_R 50 // Mid rays
#define COL_BLUE_MID_G 100
#define COL_BLUE_MID_B 220

#define COL_BLUE_DARK_R 20 // Outer ray tips
#define COL_BLUE_DARK_G 60
#define COL_BLUE_DARK_B 180

#define COL_BLACK_R 5 // Dark edges
#define COL_BLACK_G 15
#define COL_BLACK_B 40

#define DEPTH 8 // Half thickness

// 8-pointed star vertices - Front face
static Vtx sGemVtxFront[] = {
    // ========== CENTER ==========
    VTX(0, 0, DEPTH, 512, 512, COL_WHITE_R, COL_WHITE_G, COL_WHITE_B, 0xFF), // 0 - center

    // ========== RAY TIPS (8 points) ==========
    // Cardinal rays (longer) - indices 1-4
    VTX(0, 90, DEPTH, 512, 512, COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF),  // 1 - N
    VTX(90, 0, DEPTH, 512, 512, COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF),  // 2 - E
    VTX(0, -90, DEPTH, 512, 512, COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 3 - S
    VTX(-90, 0, DEPTH, 512, 512, COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 4 - W

    // Diagonal rays - indices 5-8
    VTX(64, 64, DEPTH, 512, 512, COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF),   // 5 - NE
    VTX(64, -64, DEPTH, 512, 512, COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF),  // 6 - SE
    VTX(-64, -64, DEPTH, 512, 512, COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 7 - SW
    VTX(-64, 64, DEPTH, 512, 512, COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF),  // 8 - NW

    // ========== VALLEY POINTS (between rays, inner ring) ==========
    // indices 9-16
    VTX(25, 45, DEPTH, 512, 512, COL_BLUE_BRIGHT_R, COL_BLUE_BRIGHT_G, COL_BLUE_BRIGHT_B,
        0xFF), // 9  - between N and NE
    VTX(45, 25, DEPTH, 512, 512, COL_BLUE_BRIGHT_R, COL_BLUE_BRIGHT_G, COL_BLUE_BRIGHT_B,
        0xFF), // 10 - between NE and E
    VTX(45, -25, DEPTH, 512, 512, COL_BLUE_BRIGHT_R, COL_BLUE_BRIGHT_G, COL_BLUE_BRIGHT_B,
        0xFF), // 11 - between E and SE
    VTX(25, -45, DEPTH, 512, 512, COL_BLUE_BRIGHT_R, COL_BLUE_BRIGHT_G, COL_BLUE_BRIGHT_B,
        0xFF), // 12 - between SE and S
    VTX(-25, -45, DEPTH, 512, 512, COL_BLUE_BRIGHT_R, COL_BLUE_BRIGHT_G, COL_BLUE_BRIGHT_B,
        0xFF), // 13 - between S and SW
    VTX(-45, -25, DEPTH, 512, 512, COL_BLUE_BRIGHT_R, COL_BLUE_BRIGHT_G, COL_BLUE_BRIGHT_B,
        0xFF), // 14 - between SW and W
    VTX(-45, 25, DEPTH, 512, 512, COL_BLUE_BRIGHT_R, COL_BLUE_BRIGHT_G, COL_BLUE_BRIGHT_B,
        0xFF), // 15 - between W and NW
    VTX(-25, 45, DEPTH, 512, 512, COL_BLUE_BRIGHT_R, COL_BLUE_BRIGHT_G, COL_BLUE_BRIGHT_B,
        0xFF), // 16 - between NW and N

    // ========== CORNER CHEVRON PIECES ==========
    // NE corner chevron - indices 17-19
    VTX(75, 45, DEPTH, 512, 512, COL_BLACK_R, COL_BLACK_G, COL_BLACK_B, 0xFF),          // 17 - outer right
    VTX(55, 55, DEPTH, 512, 512, COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 18 - inner tip
    VTX(45, 75, DEPTH, 512, 512, COL_BLACK_R, COL_BLACK_G, COL_BLACK_B, 0xFF),          // 19 - outer top

    // SE corner chevron - indices 20-22
    VTX(75, -45, DEPTH, 512, 512, COL_BLACK_R, COL_BLACK_G, COL_BLACK_B, 0xFF),          // 20
    VTX(55, -55, DEPTH, 512, 512, COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 21
    VTX(45, -75, DEPTH, 512, 512, COL_BLACK_R, COL_BLACK_G, COL_BLACK_B, 0xFF),          // 22

    // SW corner chevron - indices 23-25
    VTX(-45, -75, DEPTH, 512, 512, COL_BLACK_R, COL_BLACK_G, COL_BLACK_B, 0xFF),          // 23
    VTX(-55, -55, DEPTH, 512, 512, COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 24
    VTX(-75, -45, DEPTH, 512, 512, COL_BLACK_R, COL_BLACK_G, COL_BLACK_B, 0xFF),          // 25

    // NW corner chevron - indices 26-28
    VTX(-75, 45, DEPTH, 512, 512, COL_BLACK_R, COL_BLACK_G, COL_BLACK_B, 0xFF),          // 26
    VTX(-55, 55, DEPTH, 512, 512, COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 27
    VTX(-45, 75, DEPTH, 512, 512, COL_BLACK_R, COL_BLACK_G, COL_BLACK_B, 0xFF),          // 28
};

// Back face vertices
static Vtx sGemVtxBack[] = {
    // Center
    VTX(0, 0, -DEPTH, 512, 512, COL_WHITE_R, COL_WHITE_G, COL_WHITE_B, 0xFF), // 0

    // Ray tips
    VTX(0, 90, -DEPTH, 512, 512, COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF),    // 1
    VTX(90, 0, -DEPTH, 512, 512, COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF),    // 2
    VTX(0, -90, -DEPTH, 512, 512, COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF),   // 3
    VTX(-90, 0, -DEPTH, 512, 512, COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF),   // 4
    VTX(64, 64, -DEPTH, 512, 512, COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF),   // 5
    VTX(64, -64, -DEPTH, 512, 512, COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF),  // 6
    VTX(-64, -64, -DEPTH, 512, 512, COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF), // 7
    VTX(-64, 64, -DEPTH, 512, 512, COL_BLUE_DARK_R, COL_BLUE_DARK_G, COL_BLUE_DARK_B, 0xFF),  // 8

    // Valley points
    VTX(25, 45, -DEPTH, 512, 512, COL_BLUE_BRIGHT_R, COL_BLUE_BRIGHT_G, COL_BLUE_BRIGHT_B, 0xFF),   // 9
    VTX(45, 25, -DEPTH, 512, 512, COL_BLUE_BRIGHT_R, COL_BLUE_BRIGHT_G, COL_BLUE_BRIGHT_B, 0xFF),   // 10
    VTX(45, -25, -DEPTH, 512, 512, COL_BLUE_BRIGHT_R, COL_BLUE_BRIGHT_G, COL_BLUE_BRIGHT_B, 0xFF),  // 11
    VTX(25, -45, -DEPTH, 512, 512, COL_BLUE_BRIGHT_R, COL_BLUE_BRIGHT_G, COL_BLUE_BRIGHT_B, 0xFF),  // 12
    VTX(-25, -45, -DEPTH, 512, 512, COL_BLUE_BRIGHT_R, COL_BLUE_BRIGHT_G, COL_BLUE_BRIGHT_B, 0xFF), // 13
    VTX(-45, -25, -DEPTH, 512, 512, COL_BLUE_BRIGHT_R, COL_BLUE_BRIGHT_G, COL_BLUE_BRIGHT_B, 0xFF), // 14
    VTX(-45, 25, -DEPTH, 512, 512, COL_BLUE_BRIGHT_R, COL_BLUE_BRIGHT_G, COL_BLUE_BRIGHT_B, 0xFF),  // 15
    VTX(-25, 45, -DEPTH, 512, 512, COL_BLUE_BRIGHT_R, COL_BLUE_BRIGHT_G, COL_BLUE_BRIGHT_B, 0xFF),  // 16

    // Corner chevrons
    VTX(75, 45, -DEPTH, 512, 512, COL_BLACK_R, COL_BLACK_G, COL_BLACK_B, 0xFF),            // 17
    VTX(55, 55, -DEPTH, 512, 512, COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF),   // 18
    VTX(45, 75, -DEPTH, 512, 512, COL_BLACK_R, COL_BLACK_G, COL_BLACK_B, 0xFF),            // 19
    VTX(75, -45, -DEPTH, 512, 512, COL_BLACK_R, COL_BLACK_G, COL_BLACK_B, 0xFF),           // 20
    VTX(55, -55, -DEPTH, 512, 512, COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF),  // 21
    VTX(45, -75, -DEPTH, 512, 512, COL_BLACK_R, COL_BLACK_G, COL_BLACK_B, 0xFF),           // 22
    VTX(-45, -75, -DEPTH, 512, 512, COL_BLACK_R, COL_BLACK_G, COL_BLACK_B, 0xFF),          // 23
    VTX(-55, -55, -DEPTH, 512, 512, COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF), // 24
    VTX(-75, -45, -DEPTH, 512, 512, COL_BLACK_R, COL_BLACK_G, COL_BLACK_B, 0xFF),          // 25
    VTX(-75, 45, -DEPTH, 512, 512, COL_BLACK_R, COL_BLACK_G, COL_BLACK_B, 0xFF),           // 26
    VTX(-55, 55, -DEPTH, 512, 512, COL_BLUE_MID_R, COL_BLUE_MID_G, COL_BLUE_MID_B, 0xFF),  // 27
    VTX(-45, 75, -DEPTH, 512, 512, COL_BLACK_R, COL_BLACK_G, COL_BLACK_B, 0xFF),           // 28
};

Gfx g_desire_sensor_dl[] = {
    gsSPClearGeometryMode(G_CULL_BACK | G_CULL_FRONT | G_LIGHTING | G_TEXTURE_GEN),
    gsSPSetGeometryMode(G_SHADE | G_SHADING_SMOOTH),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),

    // ========== FRONT FACE ==========
    gsSPVertex(sGemVtxFront, 29, 0),

    // 8-pointed star rays (center to tips, through valleys)
    // N ray
    gsSP2Triangles(0, 16, 1, 0, 0, 1, 9, 0),
    // NE ray
    gsSP2Triangles(0, 9, 5, 0, 0, 5, 10, 0),
    // E ray
    gsSP2Triangles(0, 10, 2, 0, 0, 2, 11, 0),
    // SE ray
    gsSP2Triangles(0, 11, 6, 0, 0, 6, 12, 0),
    // S ray
    gsSP2Triangles(0, 12, 3, 0, 0, 3, 13, 0),
    // SW ray
    gsSP2Triangles(0, 13, 7, 0, 0, 7, 14, 0),
    // W ray
    gsSP2Triangles(0, 14, 4, 0, 0, 4, 15, 0),
    // NW ray
    gsSP2Triangles(0, 15, 8, 0, 0, 8, 16, 0),

    // Corner chevrons (4 pieces)
    gsSP1Triangle(17, 18, 19, 0), // NE chevron
    gsSP1Triangle(20, 22, 21, 0), // SE chevron
    gsSP1Triangle(23, 24, 25, 0), // SW chevron
    gsSP1Triangle(26, 28, 27, 0), // NW chevron

    // ========== BACK FACE (reverse winding) ==========
    gsSPVertex(sGemVtxBack, 29, 0),

    // 8-pointed star rays
    gsSP2Triangles(0, 1, 16, 0, 0, 9, 1, 0),
    gsSP2Triangles(0, 5, 9, 0, 0, 10, 5, 0),
    gsSP2Triangles(0, 2, 10, 0, 0, 11, 2, 0),
    gsSP2Triangles(0, 6, 11, 0, 0, 12, 6, 0),
    gsSP2Triangles(0, 3, 12, 0, 0, 13, 3, 0),
    gsSP2Triangles(0, 7, 13, 0, 0, 14, 7, 0),
    gsSP2Triangles(0, 4, 14, 0, 0, 15, 4, 0),
    gsSP2Triangles(0, 8, 15, 0, 0, 16, 8, 0),

    // Corner chevrons
    gsSP1Triangle(17, 19, 18, 0),
    gsSP1Triangle(20, 21, 22, 0),
    gsSP1Triangle(23, 25, 24, 0),
    gsSP1Triangle(26, 27, 28, 0),

    // ========== EDGES (connecting front and back) ==========
    // Ray tip edges (8 tips)
    gsSPVertex(&sGemVtxFront[1], 8, 0), // Front tips 0-7
    gsSPVertex(&sGemVtxBack[1], 8, 8),  // Back tips 8-15

    // Connect N tip
    gsSP2Triangles(0, 8, 1, 0, 1, 8, 9, 0),
    // Connect NE tip
    gsSP2Triangles(4, 12, 5, 0, 5, 12, 13, 0),
    // Connect E tip
    gsSP2Triangles(1, 9, 2, 0, 2, 9, 10, 0),
    // Connect SE tip
    gsSP2Triangles(5, 13, 6, 0, 6, 13, 14, 0),
    // Connect S tip
    gsSP2Triangles(2, 10, 3, 0, 3, 10, 11, 0),
    // Connect SW tip
    gsSP2Triangles(6, 14, 7, 0, 7, 14, 15, 0),
    // Connect W tip
    gsSP2Triangles(3, 11, 0, 0, 0, 11, 8, 0),
    // Connect NW tip
    gsSP2Triangles(7, 15, 4, 0, 4, 15, 12, 0),

    gsSPEndDisplayList(),
};
