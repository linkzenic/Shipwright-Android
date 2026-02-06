/**
 * mm_player_data.c - MM Player Data Arrays
 *
 * Contains form-specific data arrays from 2Ship z_player.c
 * These are NOT compiled separately - they are #included
 */

#ifndef MM_PLAYER_DATA_C
#define MM_PLAYER_DATA_C

#include "mm_compat.h"

// =============================================================================
// PLAYER MASS BY FORM (from z_player.c line 7806)
// =============================================================================

u8 sMmPlayerMass[MM_PLAYER_FORM_MAX] = {
    100, // MM_PLAYER_FORM_FIERCE_DEITY
    200, // MM_PLAYER_FORM_GORON
    80,  // MM_PLAYER_FORM_ZORA
    20,  // MM_PLAYER_FORM_DEKU
    50,  // MM_PLAYER_FORM_HUMAN
};

// =============================================================================
// MASK-OFF ANIMATIONS BY FORM (from z_player.c line 7770, D_8085D160)
// These are the animations played when removing the transformation mask
// =============================================================================

// Note: These are OTR paths - need MM animation system
// For now, declare as strings until we have the animation loader
const char* sMmMaskOffAnims[MM_PLAYER_FORM_MAX] = {
    "__OTR__objects/gameplay_keep/gPlayerAnim_pz_maskoffstart", // FIERCE_DEITY
    "__OTR__objects/gameplay_keep/gPlayerAnim_pg_maskoffstart", // GORON
    "__OTR__objects/gameplay_keep/gPlayerAnim_pz_maskoffstart", // ZORA
    "__OTR__objects/gameplay_keep/gPlayerAnim_pn_maskoffstart", // DEKU
    "__OTR__objects/gameplay_keep/gPlayerAnim_cl_setmask",      // HUMAN
};

// =============================================================================
// OCARINA ANIMATIONS BY FORM
// =============================================================================

// Ocarina start animations (from z_player.c D_8085D17C)
const char* sMmOcarinaStartAnims[MM_PLAYER_FORM_MAX] = {
    "__OTR__objects/gameplay_keep/gPlayerAnim_link_normal_okarina_start", // FIERCE_DEITY (uses human)
    "__OTR__objects/gameplay_keep/gPlayerAnim_pg_gakkistart",             // GORON
    "__OTR__objects/gameplay_keep/gPlayerAnim_pz_gakkistart",             // ZORA
    "__OTR__objects/gameplay_keep/gPlayerAnim_pn_gakkistart",             // DEKU
    "__OTR__objects/gameplay_keep/gPlayerAnim_link_normal_okarina_start", // HUMAN
};

// Ocarina play animations (from z_player.c D_8085D190)
const char* sMmOcarinaPlayAnims[MM_PLAYER_FORM_MAX] = {
    "__OTR__objects/gameplay_keep/gPlayerAnim_link_normal_okarina_swing", // FIERCE_DEITY
    "__OTR__objects/gameplay_keep/gPlayerAnim_pg_gakkiplay",              // GORON
    "__OTR__objects/gameplay_keep/gPlayerAnim_pz_gakkiplay",              // ZORA
    "__OTR__objects/gameplay_keep/gPlayerAnim_pn_gakkiplay",              // DEKU
    "__OTR__objects/gameplay_keep/gPlayerAnim_link_normal_okarina_swing", // HUMAN
};

// =============================================================================
// DOOR ANIMATIONS BY FORM
// =============================================================================

// Door A (left) open animations
const char* sMmDoorAOpenAnims[MM_PLAYER_FORM_MAX] = {
    "__OTR__objects/gameplay_keep/gPlayerAnim_link_demo_doorA_open_free", // FIERCE_DEITY (free form)
    "__OTR__objects/gameplay_keep/gPlayerAnim_pg_doorA_open",             // GORON
    "__OTR__objects/gameplay_keep/gPlayerAnim_pz_doorA_open",             // ZORA
    "__OTR__objects/gameplay_keep/gPlayerAnim_pn_doorA_open",             // DEKU
    "__OTR__objects/gameplay_keep/gPlayerAnim_link_demo_doorA_open",      // HUMAN
};

// Door B (right) open animations
const char* sMmDoorBOpenAnims[MM_PLAYER_FORM_MAX] = {
    "__OTR__objects/gameplay_keep/gPlayerAnim_link_demo_doorB_open_free", // FIERCE_DEITY
    "__OTR__objects/gameplay_keep/gPlayerAnim_pg_doorB_open",             // GORON
    "__OTR__objects/gameplay_keep/gPlayerAnim_pz_doorB_open",             // ZORA
    "__OTR__objects/gameplay_keep/gPlayerAnim_pn_doorB_open",             // DEKU
    "__OTR__objects/gameplay_keep/gPlayerAnim_link_demo_doorB_open",      // HUMAN
};

// =============================================================================
// GORON SPECIFIC DATA
// =============================================================================

// Goron curl animation
const char* sMmGoronCurlAnim = "__OTR__objects/gameplay_keep/gPlayerAnim_pg_maru_change";

// Goron wait animation (standing)
const char* sMmGoronWaitAnim = "__OTR__objects/gameplay_keep/gPlayerAnim_pg_wait";

// Goron walk animation
const char* sMmGoronWalkAnim = "__OTR__objects/gameplay_keep/gPlayerAnim_pg_walk";

// =============================================================================
// ZORA SPECIFIC DATA
// =============================================================================

// Zora swim idle animation
const char* sMmZoraSwimIdleAnim = "__OTR__objects/gameplay_keep/gPlayerAnim_link_swimer_swim_wait";

// Zora fast swim animation
const char* sMmZoraFastSwimAnim = "__OTR__objects/gameplay_keep/gPlayerAnim_pz_fishswim";

// =============================================================================
// DEKU SPECIFIC DATA
// =============================================================================

// Deku flower spin animation
const char* sMmDekuFlowerSpinAnim = "__OTR__objects/gameplay_keep/gPlayerAnim_pn_kakku";

// Deku flutter animation
const char* sMmDekuFlutterAnim = "__OTR__objects/gameplay_keep/gPlayerAnim_pn_batabata";

// Deku spin attack animation
const char* sMmDekuSpinAttackAnim = "__OTR__objects/gameplay_keep/gPlayerAnim_pn_attack";

// Deku throw distances (D_8085D958)
f32 sMmDekuThrowDistances[2] = { 600.0f, 960.0f };

// Deku hand offsets for particles (D_8085D960, D_8085D96C)
Vec3f sMmDekuLeftHandOffset = { -30.0f, 50.0f, 0.0f };
Vec3f sMmDekuRightHandOffset = { 30.0f, 50.0f, 0.0f };

// =============================================================================
// TRANSFORMATION TIMING DATA
// =============================================================================

// Cutscene timing by form (D_8085D910)
// Format: { startFrame, flashFrame, endFrame }
s16 sMmTransformTiming[MM_PLAYER_FORM_MAX][3] = {
    { 0, 14, 20 }, // FIERCE_DEITY
    { 0, 14, 20 }, // GORON
    { 0, 14, 20 }, // ZORA
    { 0, 14, 20 }, // DEKU
    { 0, 14, 20 }, // HUMAN
};

// Week event flags by mask (D_8085D908)
// Used to track which transformation was first used each cycle
u16 sMmTransformWeekEventFlags[4] = {
    0, // FIERCE_DEITY
    0, // GORON (WEEKEVENTREG_WORE_GORON_MASK)
    0, // ZORA (WEEKEVENTREG_WORE_ZORA_MASK)
    0, // DEKU (WEEKEVENTREG_WORE_DEKU_MASK)
};

#endif // MM_PLAYER_DATA_C
