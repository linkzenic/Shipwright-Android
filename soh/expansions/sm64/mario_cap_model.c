// Auto-extracted from the SM64 decomp model.inc.c by apps/sm64_model_extract.py.
// Flat-lit sub-model (no texture). Meant to be #included into the sm64 TU.

static const Lights1 mario_red_lights_group = gdSPDefLights1(
    0x7f, 0x00, 0x00,
    0xff, 0x00, 0x00, 0x28, 0x28, 0x28
);

static const Lights1 mario_brown2_lights_group = gdSPDefLights1(
    0x39, 0x03, 0x00,
    0x73, 0x06, 0x00, 0x28, 0x28, 0x28
);

static const Vtx mario_cap_unused_base_top_dl_vertex_group1[] = {
    {{{   -66,      2,    139}, 0, {     0,      0}, {0xb0, 0xbb, 0x45, 0xff}}},
    {{{     0,      0,    163}, 0, {     0,      0}, {0x00, 0xba, 0x69, 0xff}}},
    {{{   -31,     35,    118}, 0, {     0,      0}, {0xd0, 0x26, 0x6f, 0xff}}},
    {{{   -32,     17,    109}, 0, {     0,      0}, {0x00, 0x83, 0xf0, 0xff}}},
    {{{    33,     17,    109}, 0, {     0,      0}, {0xfb, 0x84, 0xea, 0xff}}},
    {{{   -95,     22,     46}, 0, {     0,      0}, {0xa8, 0xb9, 0x38, 0xff}}},
    {{{  -101,     10,     -7}, 0, {     0,      0}, {0xd8, 0x89, 0x11, 0xff}}},
    {{{   -70,    101,    113}, 0, {     0,      0}, {0xab, 0x16, 0x5b, 0xff}}},
    {{{  -135,     70,     23}, 0, {     0,      0}, {0x84, 0x15, 0x10, 0xff}}},
    {{{  -125,     38,    -45}, 0, {     0,      0}, {0x8d, 0xec, 0xd1, 0xff}}},
    {{{   -86,      1,    -60}, 0, {     0,      0}, {0xce, 0x8c, 0xf6, 0xff}}},
    {{{   -41,    144,     64}, 0, {     0,      0}, {0xdc, 0x79, 0x00, 0xff}}},
    {{{   -76,     84,    -60}, 0, {     0,      0}, {0xd5, 0x6e, 0xd3, 0xff}}},
    {{{   136,     70,     22}, 0, {     0,      0}, {0x7b, 0x16, 0x10, 0xff}}},
    {{{    71,    101,    113}, 0, {     0,      0}, {0x55, 0x16, 0x5b, 0xff}}},
    {{{    96,     22,     45}, 0, {     0,      0}, {0x48, 0xa4, 0x31, 0xff}}},
};

static const Vtx mario_cap_unused_base_top_dl_vertex_group2[] = {
    {{{    42,    144,     64}, 0, {     0,      0}, {0x2b, 0x76, 0x0d, 0xff}}},
    {{{   136,     70,     22}, 0, {     0,      0}, {0x7b, 0x16, 0x10, 0xff}}},
    {{{    76,     84,    -60}, 0, {     0,      0}, {0x2a, 0x6c, 0xcf, 0xff}}},
    {{{   103,     10,     -6}, 0, {     0,      0}, {0x42, 0x96, 0x12, 0xff}}},
    {{{   126,     38,    -46}, 0, {     0,      0}, {0x73, 0xec, 0xd0, 0xff}}},
    {{{    71,    101,    113}, 0, {     0,      0}, {0x55, 0x16, 0x5b, 0xff}}},
    {{{    96,     22,     45}, 0, {     0,      0}, {0x48, 0xa4, 0x31, 0xff}}},
    {{{    67,      2,    139}, 0, {     0,      0}, {0x50, 0xba, 0x44, 0xff}}},
    {{{    33,     17,    109}, 0, {     0,      0}, {0xfb, 0x84, 0xea, 0xff}}},
    {{{    33,     35,    118}, 0, {     0,      0}, {0x30, 0x26, 0x6e, 0xff}}},
    {{{    86,      1,    -60}, 0, {     0,      0}, {0x20, 0x86, 0xfe, 0xff}}},
    {{{     0,      0,    163}, 0, {     0,      0}, {0x00, 0xba, 0x69, 0xff}}},
    {{{   -31,     35,    118}, 0, {     0,      0}, {0xd0, 0x26, 0x6f, 0xff}}},
    {{{    53,      0,   -118}, 0, {     0,      0}, {0x2c, 0xb5, 0xa5, 0xff}}},
    {{{    49,     62,   -139}, 0, {     0,      0}, {0x32, 0x49, 0xa6, 0xff}}},
};

static const Vtx mario_cap_unused_base_top_dl_vertex_group3[] = {
    {{{   -76,     84,    -60}, 0, {     0,      0}, {0xd5, 0x6e, 0xd3, 0xff}}},
    {{{   -41,    144,     64}, 0, {     0,      0}, {0xdc, 0x79, 0x00, 0xff}}},
    {{{    76,     84,    -60}, 0, {     0,      0}, {0x2a, 0x6c, 0xcf, 0xff}}},
    {{{     0,    110,    143}, 0, {     0,      0}, {0x00, 0x34, 0x73, 0xff}}},
    {{{    42,    144,     64}, 0, {     0,      0}, {0x2b, 0x76, 0x0d, 0xff}}},
    {{{   -70,    101,    113}, 0, {     0,      0}, {0xab, 0x16, 0x5b, 0xff}}},
    {{{    71,    101,    113}, 0, {     0,      0}, {0x55, 0x16, 0x5b, 0xff}}},
    {{{    49,     62,   -139}, 0, {     0,      0}, {0x32, 0x49, 0xa6, 0xff}}},
    {{{   126,     38,    -46}, 0, {     0,      0}, {0x73, 0xec, 0xd0, 0xff}}},
    {{{   -52,      0,   -118}, 0, {     0,      0}, {0xd2, 0x9d, 0xc1, 0xff}}},
    {{{   -49,     62,   -138}, 0, {     0,      0}, {0xce, 0x1a, 0x8f, 0xff}}},
    {{{    53,      0,   -118}, 0, {     0,      0}, {0x2c, 0xb5, 0xa5, 0xff}}},
    {{{  -125,     38,    -45}, 0, {     0,      0}, {0x8d, 0xec, 0xd1, 0xff}}},
    {{{    86,      1,    -60}, 0, {     0,      0}, {0x20, 0x86, 0xfe, 0xff}}},
    {{{   -86,      1,    -60}, 0, {     0,      0}, {0xce, 0x8c, 0xf6, 0xff}}},
};

static const Vtx mario_cap_unused_base_bottom_dl_vertex[] = {
    {{{    86,      1,    -60}, 0, {     0,      0}, {0x20, 0x86, 0xfe, 0xff}}},
    {{{   -86,      1,    -60}, 0, {     0,      0}, {0xce, 0x8c, 0xf6, 0xff}}},
    {{{   -52,      0,   -118}, 0, {     0,      0}, {0xd2, 0x9d, 0xc1, 0xff}}},
    {{{    33,     17,    109}, 0, {     0,      0}, {0xfb, 0x84, 0xea, 0xff}}},
    {{{   -32,     17,    109}, 0, {     0,      0}, {0x00, 0x83, 0xf0, 0xff}}},
    {{{  -101,     10,     -7}, 0, {     0,      0}, {0xd8, 0x89, 0x11, 0xff}}},
    {{{    96,     22,     45}, 0, {     0,      0}, {0x48, 0xa4, 0x31, 0xff}}},
    {{{   103,     10,     -6}, 0, {     0,      0}, {0x42, 0x96, 0x12, 0xff}}},
    {{{    53,      0,   -118}, 0, {     0,      0}, {0x2c, 0xb5, 0xa5, 0xff}}},
};

const Gfx mario_cap_unused_base_top_dl[] = {
    gsSPVertex(mario_cap_unused_base_top_dl_vertex_group1, 16, 0),
    gsSP2Triangles( 0,  1,  2, 0x0,  3,  4,  1, 0x0),
    gsSP2Triangles( 3,  1,  0, 0x0,  3,  5,  6, 0x0),
    gsSP2Triangles( 3,  0,  5, 0x0,  5,  7,  8, 0x0),
    gsSP2Triangles( 5,  8,  6, 0x0,  0,  2,  5, 0x0),
    gsSP2Triangles( 6,  9, 10, 0x0,  8,  9,  6, 0x0),
    gsSP2Triangles(11,  8,  7, 0x0,  8, 12,  9, 0x0),
    gsSP2Triangles(12,  8, 11, 0x0, 13, 14, 15, 0x0),
    gsSPVertex(mario_cap_unused_base_top_dl_vertex_group2, 15, 0),
    gsSP2Triangles( 0,  1,  2, 0x0,  3,  4,  1, 0x0),
    gsSP2Triangles( 4,  2,  1, 0x0,  5,  1,  0, 0x0),
    gsSP2Triangles( 1,  6,  3, 0x0,  6,  7,  8, 0x0),
    gsSP2Triangles( 9,  7,  6, 0x0, 10,  4,  3, 0x0),
    gsSP2Triangles( 9, 11,  7, 0x0,  7, 11,  8, 0x0),
    gsSP2Triangles(12, 11,  9, 0x0, 13, 14,  4, 0x0),
    gsSPVertex(mario_cap_unused_base_top_dl_vertex_group3, 15, 0),
    gsSP2Triangles( 0,  1,  2, 0x0,  1,  3,  4, 0x0),
    gsSP2Triangles( 5,  3,  1, 0x0,  1,  4,  2, 0x0),
    gsSP2Triangles( 4,  3,  6, 0x0,  0,  2,  7, 0x0),
    gsSP2Triangles( 8,  7,  2, 0x0,  9, 10, 11, 0x0),
    gsSP2Triangles(12, 10,  9, 0x0,  7, 10,  0, 0x0),
    gsSP2Triangles(10,  7, 11, 0x0,  0, 10, 12, 0x0),
    gsSP2Triangles(11,  8, 13, 0x0, 14, 12,  9, 0x0),
    gsSPEndDisplayList(),
};

const Gfx mario_cap_unused_base_bottom_dl[] = {
    gsSPVertex(mario_cap_unused_base_bottom_dl_vertex, 9, 0),
    gsSP2Triangles( 0,  1,  2, 0x0,  3,  4,  5, 0x0),
    gsSP2Triangles( 6,  3,  5, 0x0,  7,  6,  5, 0x0),
    gsSP2Triangles( 0,  7,  5, 0x0,  0,  5,  1, 0x0),
    gsSP1Triangle( 2,  8,  0, 0x0),
    gsSPEndDisplayList(),
};

const Gfx mario_cap_unused_base_dl[] = {
    gsSPDisplayList(mario_cap_unused_base_top_dl),
    gsSPLight(&mario_brown2_lights_group.l, 1),
    gsSPLight(&mario_brown2_lights_group.a, 2),
    gsSPDisplayList(mario_cap_unused_base_bottom_dl),
    gsSPEndDisplayList(),
};

