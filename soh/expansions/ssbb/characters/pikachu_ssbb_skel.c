#include "expansions/ssbb/characters/pikachu_ssbb_skel.h"

// ── Limb Definitions ─────────────────────────────────────────────────────
static StandardLimb pikachu_ssbb_limb_000 = { { 0, 0, 0 }, 1, 255, NULL };
static StandardLimb pikachu_ssbb_limb_001 = { { 0, 0, 0 }, 255, 2, NULL };
static StandardLimb pikachu_ssbb_limb_002 = { { 0, 0, 0 }, 3, 255, NULL };
static StandardLimb pikachu_ssbb_limb_003 = { { 0, 521, 0 }, 4, 255, NULL };
static StandardLimb pikachu_ssbb_limb_004 = { { 0, 0, 0 }, 5, 255, NULL };
static StandardLimb pikachu_ssbb_limb_005 = { { 0, -161, -87 }, 6, 47, NULL };
static StandardLimb pikachu_ssbb_limb_006 = { { 0, -66, 17 }, 255, 7, NULL };
static StandardLimb pikachu_ssbb_limb_007 = { { 206, -45, 45 }, 8, 13, NULL };
static StandardLimb pikachu_ssbb_limb_008 = { { 0, -190, 0 }, 9, 255, NULL };
static StandardLimb pikachu_ssbb_limb_009 = { { -1, -241, 0 }, 10, 11, NULL };
static StandardLimb pikachu_ssbb_limb_010 = { { -4, -18, 103 }, 255, 255, NULL };
static StandardLimb pikachu_ssbb_limb_011 = { { 0, -25, 0 }, 12, 255, NULL };
static StandardLimb pikachu_ssbb_limb_012 = { { 0, 0, 0 }, 255, 255, NULL };
static StandardLimb pikachu_ssbb_limb_013 = { { -206, -45, 45 }, 14, 19, NULL };
static StandardLimb pikachu_ssbb_limb_014 = { { 0, 190, 0 }, 15, 255, NULL };
static StandardLimb pikachu_ssbb_limb_015 = { { 1, 241, 0 }, 16, 17, NULL };
static StandardLimb pikachu_ssbb_limb_016 = { { 4, 18, -103 }, 255, 255, NULL };
static StandardLimb pikachu_ssbb_limb_017 = { { 0, 25, 0 }, 18, 255, NULL };
static StandardLimb pikachu_ssbb_limb_018 = { { 0, 0, 0 }, 255, 255, NULL };
static StandardLimb pikachu_ssbb_limb_019 = { { 0, -135, -270 }, 20, 23, NULL };
static StandardLimb pikachu_ssbb_limb_020 = { { 293, 0, 0 }, 21, 255, NULL };
static StandardLimb pikachu_ssbb_limb_021 = { { 262, 0, 0 }, 22, 255, NULL };
static StandardLimb pikachu_ssbb_limb_022 = { { 202, 0, 0 }, 255, 255, NULL };
static StandardLimb pikachu_ssbb_limb_023 = { { 0, 0, 0 }, 24, 255, NULL };
static StandardLimb pikachu_ssbb_limb_024 = { { 0, 225, 18 }, 25, 255, NULL };
static StandardLimb pikachu_ssbb_limb_025 = { { 41, -22, 58 }, 26, 32, NULL };
static StandardLimb pikachu_ssbb_limb_026 = { { 201, 34, 174 }, 27, 31, NULL };
static StandardLimb pikachu_ssbb_limb_027 = { { 113, -1, 0 }, 28, 255, NULL };
static StandardLimb pikachu_ssbb_limb_028 = { { 56, 0, 0 }, 29, 255, NULL };
static StandardLimb pikachu_ssbb_limb_029 = { { 44, 12, 0 }, 255, 30, NULL };
static StandardLimb pikachu_ssbb_limb_030 = { { 19, -12, 17 }, 255, 255, NULL };
static StandardLimb pikachu_ssbb_limb_031 = { { 212, 15, 153 }, 255, 255, NULL };
static StandardLimb pikachu_ssbb_limb_032 = { { 0, 133, 105 }, 33, 40, NULL };
static StandardLimb pikachu_ssbb_limb_033 = { { 0, 355, 57 }, 255, 34, NULL };
static StandardLimb pikachu_ssbb_limb_034 = { { 0, -13, 361 }, 255, 35, NULL };
static StandardLimb pikachu_ssbb_limb_035 = { { 200, 282, -9 }, 36, 37, NULL };
static StandardLimb pikachu_ssbb_limb_036 = { { 109, 0, 0 }, 255, 255, NULL };
static StandardLimb pikachu_ssbb_limb_037 = { { 0, 23, 120 }, 255, 38, NULL };
static StandardLimb pikachu_ssbb_limb_038 = { { -200, 282, -9 }, 39, 255, NULL };
static StandardLimb pikachu_ssbb_limb_039 = { { 109, 0, 0 }, 255, 255, NULL };
static StandardLimb pikachu_ssbb_limb_040 = { { -41, -22, 58 }, 41, 255, NULL };
static StandardLimb pikachu_ssbb_limb_041 = { { -201, -34, -174 }, 42, 46, NULL };
static StandardLimb pikachu_ssbb_limb_042 = { { -113, 1, 0 }, 43, 255, NULL };
static StandardLimb pikachu_ssbb_limb_043 = { { -56, 0, 0 }, 44, 255, NULL };
static StandardLimb pikachu_ssbb_limb_044 = { { -44, -12, 0 }, 255, 45, NULL };
static StandardLimb pikachu_ssbb_limb_045 = { { -19, 12, -17 }, 255, 255, NULL };
static StandardLimb pikachu_ssbb_limb_046 = { { -212, -15, -153 }, 255, 255, NULL };
static StandardLimb pikachu_ssbb_limb_047 = { { 0, -450, 0 }, 255, 255, NULL };

static void* pikachu_ssbb_limb_table[48] = {
    &pikachu_ssbb_limb_000, &pikachu_ssbb_limb_001, &pikachu_ssbb_limb_002, &pikachu_ssbb_limb_003,
    &pikachu_ssbb_limb_004, &pikachu_ssbb_limb_005, &pikachu_ssbb_limb_006, &pikachu_ssbb_limb_007,
    &pikachu_ssbb_limb_008, &pikachu_ssbb_limb_009, &pikachu_ssbb_limb_010, &pikachu_ssbb_limb_011,
    &pikachu_ssbb_limb_012, &pikachu_ssbb_limb_013, &pikachu_ssbb_limb_014, &pikachu_ssbb_limb_015,
    &pikachu_ssbb_limb_016, &pikachu_ssbb_limb_017, &pikachu_ssbb_limb_018, &pikachu_ssbb_limb_019,
    &pikachu_ssbb_limb_020, &pikachu_ssbb_limb_021, &pikachu_ssbb_limb_022, &pikachu_ssbb_limb_023,
    &pikachu_ssbb_limb_024, &pikachu_ssbb_limb_025, &pikachu_ssbb_limb_026, &pikachu_ssbb_limb_027,
    &pikachu_ssbb_limb_028, &pikachu_ssbb_limb_029, &pikachu_ssbb_limb_030, &pikachu_ssbb_limb_031,
    &pikachu_ssbb_limb_032, &pikachu_ssbb_limb_033, &pikachu_ssbb_limb_034, &pikachu_ssbb_limb_035,
    &pikachu_ssbb_limb_036, &pikachu_ssbb_limb_037, &pikachu_ssbb_limb_038, &pikachu_ssbb_limb_039,
    &pikachu_ssbb_limb_040, &pikachu_ssbb_limb_041, &pikachu_ssbb_limb_042, &pikachu_ssbb_limb_043,
    &pikachu_ssbb_limb_044, &pikachu_ssbb_limb_045, &pikachu_ssbb_limb_046, &pikachu_ssbb_limb_047
};

FlexSkeletonHeader pikachu_ssbb_skeleton = { { pikachu_ssbb_limb_table, 48, 0 }, 0 };
