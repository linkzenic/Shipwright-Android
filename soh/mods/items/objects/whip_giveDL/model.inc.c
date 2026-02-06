#include "header.h"

/**
 * Whip give item model — single continuous mesh
 * One vertex array, one DL, everything physically connected.
 *
 * Structure (bottom to top):
 *   Crystal octahedron (idx 0-5)  y=-52 to y=-2
 *   Body ring 0 (idx 6-9)        y=-2   (shared boundary with crystal top)
 *   Body ring 1 (idx 10-13)      y=20   (offset x+5,z+3 for S-curve)
 *   Body ring 2 (idx 14-17)      y=40   (shared boundary with head frustum)
 *   Head base diamond (idx 18-21) y=60
 *   Head mouth tip (idx 22)       y=88
 *   Left eye (idx 23-25)
 *   Right eye (idx 26-28)
 */

Vtx whip_give_vtx[29] = {
    // === CRYSTAL (purple octahedron) ===
    { { { 0, -2, 0 }, 0, { 0, 0 }, { 150, 60, 200, 255 } } },      // [0]  top
    { { { 15, -27, 15 }, 0, { 0, 0 }, { 140, 50, 190, 255 } } },   // [1]  FR
    { { { 15, -27, -15 }, 0, { 0, 0 }, { 120, 35, 170, 255 } } },  // [2]  BR
    { { { -15, -27, -15 }, 0, { 0, 0 }, { 120, 35, 170, 255 } } }, // [3]  BL
    { { { -15, -27, 15 }, 0, { 0, 0 }, { 140, 50, 190, 255 } } },  // [4]  FL
    { { { 0, -52, 0 }, 0, { 0, 0 }, { 90, 20, 140, 255 } } },      // [5]  bottom

    // === BODY RING 0 (y=-2, center 0,0) ===
    { { { 14, -2, 14 }, 0, { 0, 0 }, { 230, 90, 20, 255 } } },   // [6]  FR
    { { { -14, -2, 14 }, 0, { 0, 0 }, { 160, 55, 10, 255 } } },  // [7]  FL
    { { { -14, -2, -14 }, 0, { 0, 0 }, { 230, 90, 20, 255 } } }, // [8]  BL
    { { { 14, -2, -14 }, 0, { 0, 0 }, { 245, 160, 60, 255 } } }, // [9]  BR

    // === BODY RING 1 (y=20, center offset 5,3 for curve) ===
    { { { 19, 20, 17 }, 0, { 0, 0 }, { 230, 90, 20, 255 } } },   // [10] FR
    { { { -9, 20, 17 }, 0, { 0, 0 }, { 160, 55, 10, 255 } } },   // [11] FL
    { { { -9, 20, -11 }, 0, { 0, 0 }, { 230, 90, 20, 255 } } },  // [12] BL
    { { { 19, 20, -11 }, 0, { 0, 0 }, { 245, 160, 60, 255 } } }, // [13] BR

    // === BODY RING 2 (y=40, center 0,0) ===
    { { { 14, 40, 14 }, 0, { 0, 0 }, { 230, 90, 20, 255 } } },   // [14] FR
    { { { -14, 40, 14 }, 0, { 0, 0 }, { 160, 55, 10, 255 } } },  // [15] FL
    { { { -14, 40, -14 }, 0, { 0, 0 }, { 230, 90, 20, 255 } } }, // [16] BL
    { { { 14, 40, -14 }, 0, { 0, 0 }, { 245, 160, 60, 255 } } }, // [17] BR

    // === HEAD BASE (diamond at y=60) ===
    { { { -28, 60, 0 }, 0, { 0, 0 }, { 160, 55, 10, 255 } } },  // [18] left
    { { { 28, 60, 0 }, 0, { 0, 0 }, { 160, 55, 10, 255 } } },   // [19] right
    { { { 0, 60, 22 }, 0, { 0, 0 }, { 245, 160, 60, 255 } } },  // [20] front
    { { { 0, 60, -22 }, 0, { 0, 0 }, { 245, 160, 60, 255 } } }, // [21] back

    // === HEAD MOUTH TIP ===
    { { { 0, 88, 0 }, 0, { 0, 0 }, { 240, 100, 25, 255 } } }, // [22] mouth

    // === LEFT EYE (green triangle on front-left face) ===
    { { { -8, 78, 8 }, 0, { 0, 0 }, { 0, 230, 0, 255 } } },  // [23] top
    { { { -18, 68, 4 }, 0, { 0, 0 }, { 0, 230, 0, 255 } } }, // [24] outer
    { { { -3, 68, 14 }, 0, { 0, 0 }, { 0, 230, 0, 255 } } }, // [25] inner

    // === RIGHT EYE (green triangle on front-right face) ===
    { { { 8, 78, 8 }, 0, { 0, 0 }, { 0, 230, 0, 255 } } },  // [26] top
    { { { 3, 68, 14 }, 0, { 0, 0 }, { 0, 230, 0, 255 } } }, // [27] inner
    { { { 18, 68, 4 }, 0, { 0, 0 }, { 0, 230, 0, 255 } } }, // [28] outer
};

Gfx whip_give_opaque_dl[] = {
    gsDPPipeSync(),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsSPClearGeometryMode(G_LIGHTING | G_CULL_BACK | G_CULL_FRONT | G_TEXTURE_GEN),
    gsSPSetGeometryMode(G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH),
    gsSPVertex(whip_give_vtx, 29, 0),

    // --- Crystal top pyramid (apex [0] → ring [1-4]) ---
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsSP2Triangles(0, 3, 4, 0, 0, 4, 1, 0),
    // --- Crystal bottom pyramid ([5] → ring [1-4]) ---
    gsSP2Triangles(5, 2, 1, 0, 5, 3, 2, 0),
    gsSP2Triangles(5, 4, 3, 0, 5, 1, 4, 0),

    // --- Body ring 0 bottom cap (seal body base at crystal top) ---
    gsSP2Triangles(0, 6, 7, 0, 0, 7, 8, 0),
    gsSP2Triangles(0, 8, 9, 0, 0, 9, 6, 0),

    // --- Body ring 0 → ring 1 (4 quads) ---
    gsSP2Triangles(6, 10, 11, 0, 6, 11, 7, 0),
    gsSP2Triangles(7, 11, 12, 0, 7, 12, 8, 0),
    gsSP2Triangles(8, 12, 13, 0, 8, 13, 9, 0),
    gsSP2Triangles(9, 13, 10, 0, 9, 10, 6, 0),

    // --- Body ring 1 → ring 2 (4 quads) ---
    gsSP2Triangles(10, 14, 15, 0, 10, 15, 11, 0),
    gsSP2Triangles(11, 15, 16, 0, 11, 16, 12, 0),
    gsSP2Triangles(12, 16, 17, 0, 12, 17, 13, 0),
    gsSP2Triangles(13, 17, 14, 0, 13, 14, 10, 0),

    // --- Frustum: body ring 2 [14-17] → head base [18-21] (8 tri) ---
    gsSP2Triangles(14, 15, 20, 0, 14, 20, 19, 0), // front, front-right
    gsSP2Triangles(15, 16, 18, 0, 15, 18, 20, 0), // left, left-front
    gsSP2Triangles(16, 17, 21, 0, 16, 21, 18, 0), // back, back-left
    gsSP2Triangles(17, 14, 19, 0, 17, 19, 21, 0), // right, right-back

    // --- Head upper pyramid (mouth [22] → base [18-21]) ---
    gsSP2Triangles(22, 20, 18, 0, 22, 19, 20, 0),
    gsSP2Triangles(22, 18, 21, 0, 22, 21, 19, 0),

    // --- Eyes ---
    gsSP2Triangles(23, 24, 25, 0, 26, 27, 28, 0),

    gsSPEndDisplayList(),
};
