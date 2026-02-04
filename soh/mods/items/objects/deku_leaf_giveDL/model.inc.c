/**
 * Deku Leaf 3D model - Procedural leaf geometry
 * Used by both giveDL (pickup display) and object_dekuleaf (in-game rendering)
 * Vertices are rotated 90deg on X axis so the leaf appears standing up
 */

// Vertex colors
#define COL_SPINE_R     160
#define COL_SPINE_G     220
#define COL_SPINE_B     100
#define COL_MEMBRANE_R  90
#define COL_MEMBRANE_G  200
#define COL_MEMBRANE_B  90
#define COL_EDGE_R      50
#define COL_EDGE_G      160
#define COL_EDGE_B      60
#define COL_STEM_R      2
#define COL_STEM_G      15
#define COL_STEM_B      2

// Vertices rotated 90deg on X: {x, y, z} -> {x, -z, y}
Vtx g_dekuleaf_vtx[29] = {
    // Spine (central vein)
    {{ {0, -40, -10}, 0, {0, 0}, {COL_SPINE_R, COL_SPINE_G, COL_SPINE_B, 255} }},
    {{ {0, -10, 15}, 0, {0, 0}, {COL_SPINE_R, COL_SPINE_G, COL_SPINE_B, 255} }},
    {{ {0, 20, 30}, 0, {0, 0}, {COL_SPINE_R, COL_SPINE_G, COL_SPINE_B, 255} }},
    {{ {0, 50, 35}, 0, {0, 0}, {COL_SPINE_R, COL_SPINE_G, COL_SPINE_B, 255} }},
    {{ {0, 80, 40}, 0, {0, 0}, {COL_SPINE_R, COL_SPINE_G, COL_SPINE_B, 255} }},

    // Left veins
    {{ {-30, -20, 5}, 0, {0, 0}, {COL_SPINE_R, COL_SPINE_G, COL_SPINE_B, 255} }},
    {{ {-45, 10, 15}, 0, {0, 0}, {COL_SPINE_R, COL_SPINE_G, COL_SPINE_B, 255} }},
    {{ {-50, 40, 25}, 0, {0, 0}, {COL_SPINE_R, COL_SPINE_G, COL_SPINE_B, 255} }},
    {{ {-30, 70, 35}, 0, {0, 0}, {COL_SPINE_R, COL_SPINE_G, COL_SPINE_B, 255} }},

    // Right veins
    {{ {30, -20, 5}, 0, {0, 0}, {COL_SPINE_R, COL_SPINE_G, COL_SPINE_B, 255} }},
    {{ {45, 10, 15}, 0, {0, 0}, {COL_SPINE_R, COL_SPINE_G, COL_SPINE_B, 255} }},
    {{ {50, 40, 25}, 0, {0, 0}, {COL_SPINE_R, COL_SPINE_G, COL_SPINE_B, 255} }},
    {{ {30, 70, 35}, 0, {0, 0}, {COL_SPINE_R, COL_SPINE_G, COL_SPINE_B, 255} }},

    // Left edge (membrane)
    {{ {-40, -50, -10}, 0, {0, 0}, {COL_MEMBRANE_R, COL_MEMBRANE_G, COL_MEMBRANE_B, 255} }},
    {{ {-60, -10, -5}, 0, {0, 0}, {COL_EDGE_R, COL_EDGE_G, COL_EDGE_B, 255} }},
    {{ {-80, 20, 10}, 0, {0, 0}, {COL_MEMBRANE_R, COL_MEMBRANE_G, COL_MEMBRANE_B, 255} }},
    {{ {-65, 55, 20}, 0, {0, 0}, {COL_EDGE_R, COL_EDGE_G, COL_EDGE_B, 255} }},

    // Right edge (membrane)
    {{ {40, -50, -10}, 0, {0, 0}, {COL_MEMBRANE_R, COL_MEMBRANE_G, COL_MEMBRANE_B, 255} }},
    {{ {60, -10, -5}, 0, {0, 0}, {COL_EDGE_R, COL_EDGE_G, COL_EDGE_B, 255} }},
    {{ {80, 20, 10}, 0, {0, 0}, {COL_MEMBRANE_R, COL_MEMBRANE_G, COL_MEMBRANE_B, 255} }},
    {{ {65, 55, 20}, 0, {0, 0}, {COL_EDGE_R, COL_EDGE_G, COL_EDGE_B, 255} }},

    // Stem (handle)
    {{ {2, -45, -15}, 0, {0, 0}, {COL_STEM_R, COL_STEM_G, COL_STEM_B, 255} }},
    {{ {-2, -45, -15}, 0, {0, 0}, {COL_STEM_R, COL_STEM_G, COL_STEM_B, 255} }},
    {{ {-2, -41, -15}, 0, {0, 0}, {COL_STEM_R, COL_STEM_G, COL_STEM_B, 255} }},
    {{ {2, -41, -15}, 0, {0, 0}, {COL_STEM_R, COL_STEM_G, COL_STEM_B, 255} }},
    {{ {1, -55, -50}, 0, {0, 0}, {COL_STEM_R, COL_STEM_G, COL_STEM_B, 255} }},
    {{ {-1, -55, -50}, 0, {0, 0}, {COL_STEM_R, COL_STEM_G, COL_STEM_B, 255} }},
    {{ {-1, -51, -50}, 0, {0, 0}, {COL_STEM_R, COL_STEM_G, COL_STEM_B, 255} }},
    {{ {1, -51, -50}, 0, {0, 0}, {COL_STEM_R, COL_STEM_G, COL_STEM_B, 255} }},
};

Gfx g_dekuleaf_dl[] = {
    gsSPClearGeometryMode(G_CULL_BACK | G_CULL_FRONT | G_LIGHTING | G_TEXTURE_GEN),
    gsSPSetGeometryMode(G_SHADE | G_SHADING_SMOOTH),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),

    gsSPVertex(g_dekuleaf_vtx, 29, 0),

    // Right side surface
    gsSP2Triangles(0, 9, 1, 0, 1, 9, 10, 0),
    gsSP2Triangles(1, 10, 2, 0, 2, 10, 11, 0),
    gsSP2Triangles(2, 11, 3, 0, 3, 11, 12, 0),
    gsSP1Triangle(3, 12, 4, 0),

    // Left side surface
    gsSP2Triangles(0, 1, 5, 0, 1, 6, 5, 0),
    gsSP2Triangles(1, 2, 6, 0, 2, 7, 6, 0),
    gsSP2Triangles(2, 3, 7, 0, 3, 8, 7, 0),
    gsSP1Triangle(3, 4, 8, 0),

    // Right edge wings
    gsSP2Triangles(9, 17, 10, 0, 10, 17, 18, 0),
    gsSP2Triangles(10, 18, 11, 0, 11, 18, 19, 0),
    gsSP2Triangles(11, 19, 12, 0, 12, 19, 20, 0),

    // Left edge wings
    gsSP2Triangles(5, 6, 13, 0, 6, 14, 13, 0),
    gsSP2Triangles(6, 7, 14, 0, 7, 15, 14, 0),
    gsSP2Triangles(7, 8, 15, 0, 8, 16, 15, 0),

    // Back fills
    gsSP2Triangles(0, 5, 13, 0, 0, 17, 9, 0),

    // Stem faces
    gsSP2Triangles(21, 25, 22, 0, 22, 25, 26, 0),
    gsSP2Triangles(22, 26, 23, 0, 23, 26, 27, 0),
    gsSP2Triangles(23, 27, 24, 0, 24, 27, 28, 0),
    gsSP2Triangles(24, 28, 21, 0, 21, 28, 25, 0),
    gsSP2Triangles(25, 28, 26, 0, 26, 28, 27, 0),

    // Stem to leaf connection
    gsSP2Triangles(0, 21, 22, 0, 0, 22, 23, 0),

    gsSPEndDisplayList(),
};
