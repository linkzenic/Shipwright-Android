/**
 * @file mm_anims_data.c
 * @brief MM Animation definition table
 *
 * This file contains the path and frame count for each MM animation.
 * Paths point to raw animation data in mm.o2r (misc/link_animetion/*_Data)
 *
 * Frame counts are from MM decomp XML files.
 * TODO: Verify all frame counts by parsing MM decomp
 */

#include "mm_anims.h"

// ============================================================================
// Animation Definition Table
// ============================================================================

// Helper macro for Human Link animations (22 limbs)
#define LINK_ANIM(name, frames) \
    { "misc/link_animetion/" name "_Data", frames, MM_LIMB_COUNT_HUMAN }

// Helper macro for Goron animations (17 limbs)
#define GORON_ANIM(name, frames) \
    { "misc/link_animetion/" name "_Data", frames, MM_LIMB_COUNT_GORON }

// Helper macro for Zora animations (22 limbs)
#define ZORA_ANIM(name, frames) \
    { "misc/link_animetion/" name "_Data", frames, MM_LIMB_COUNT_ZORA }

// Helper macro for Deku animations (12 limbs)
#define DEKU_ANIM(name, frames) \
    { "misc/link_animetion/" name "_Data", frames, MM_LIMB_COUNT_DEKU }

const MmAnimDef gMmAnims[MM_ANIM_MAX] = {
    // ========================================
    // Normal/Idle animations
    // ========================================
    [MM_ANIM_LINK_NORMAL_WAIT] = LINK_ANIM("gPlayerAnim_link_normal_wait", 72),
    [MM_ANIM_LINK_NORMAL_WAIT_FREE] = LINK_ANIM("gPlayerAnim_link_normal_wait_free", 72),
    [MM_ANIM_LINK_NORMAL_WALK] = LINK_ANIM("gPlayerAnim_link_normal_walk", 17),
    [MM_ANIM_LINK_NORMAL_WALK_FREE] = LINK_ANIM("gPlayerAnim_link_normal_walk_free", 17),
    [MM_ANIM_LINK_NORMAL_WALK_ENDL] = LINK_ANIM("gPlayerAnim_link_normal_walk_endL", 8),
    [MM_ANIM_LINK_NORMAL_WALK_ENDR] = LINK_ANIM("gPlayerAnim_link_normal_walk_endR", 8),
    [MM_ANIM_LINK_NORMAL_WALK_ENDL_FREE] = LINK_ANIM("gPlayerAnim_link_normal_walk_endL_free", 8),
    [MM_ANIM_LINK_NORMAL_WALK_ENDR_FREE] = LINK_ANIM("gPlayerAnim_link_normal_walk_endR_free", 8),
    [MM_ANIM_LINK_NORMAL_RUN] = LINK_ANIM("gPlayerAnim_link_normal_run", 16),
    [MM_ANIM_LINK_NORMAL_RUN_FREE] = LINK_ANIM("gPlayerAnim_link_normal_run_free", 16),
    [MM_ANIM_LINK_NORMAL_RUN_JUMP] = LINK_ANIM("gPlayerAnim_link_normal_run_jump", 11),
    [MM_ANIM_LINK_NORMAL_RUN_JUMP_END] = LINK_ANIM("gPlayerAnim_link_normal_run_jump_end", 5),

    // Fall/Landing animations
    [MM_ANIM_LINK_NORMAL_FALL] = LINK_ANIM("gPlayerAnim_link_normal_fall", 4),
    [MM_ANIM_LINK_NORMAL_FALL_UP] = LINK_ANIM("gPlayerAnim_link_normal_fall_up", 8),
    [MM_ANIM_LINK_NORMAL_FALL_UP_FREE] = LINK_ANIM("gPlayerAnim_link_normal_fall_up_free", 8),
    [MM_ANIM_LINK_NORMAL_FALL_WAIT] = LINK_ANIM("gPlayerAnim_link_normal_fall_wait", 2),
    [MM_ANIM_LINK_NORMAL_LANDING] = LINK_ANIM("gPlayerAnim_link_normal_landing", 7),
    [MM_ANIM_LINK_NORMAL_LANDING_FREE] = LINK_ANIM("gPlayerAnim_link_normal_landing_free", 7),
    [MM_ANIM_LINK_NORMAL_LANDING_ROLL] = LINK_ANIM("gPlayerAnim_link_normal_landing_roll", 25),
    [MM_ANIM_LINK_NORMAL_LANDING_ROLL_FREE] = LINK_ANIM("gPlayerAnim_link_normal_landing_roll_free", 25),
    [MM_ANIM_LINK_NORMAL_LANDING_WAIT] = LINK_ANIM("gPlayerAnim_link_normal_landing_wait", 8),
    [MM_ANIM_LINK_NORMAL_SHORT_LANDING] = LINK_ANIM("gPlayerAnim_link_normal_short_landing", 4),
    [MM_ANIM_LINK_NORMAL_SHORT_LANDING_FREE] = LINK_ANIM("gPlayerAnim_link_normal_short_landing_free", 4),

    // Jump animations
    [MM_ANIM_LINK_NORMAL_JUMP] = LINK_ANIM("gPlayerAnim_link_normal_jump", 8),
    [MM_ANIM_LINK_NORMAL_JUMP_CLIMB_HOLD] = LINK_ANIM("gPlayerAnim_link_normal_jump_climb_hold", 3),
    [MM_ANIM_LINK_NORMAL_JUMP_CLIMB_HOLD_FREE] = LINK_ANIM("gPlayerAnim_link_normal_jump_climb_hold_free", 3),
    [MM_ANIM_LINK_NORMAL_JUMP_CLIMB_UP] = LINK_ANIM("gPlayerAnim_link_normal_jump_climb_up", 17),
    [MM_ANIM_LINK_NORMAL_JUMP_CLIMB_UP_FREE] = LINK_ANIM("gPlayerAnim_link_normal_jump_climb_up_free", 17),
    [MM_ANIM_LINK_NORMAL_JUMP_CLIMB_WAIT] = LINK_ANIM("gPlayerAnim_link_normal_jump_climb_wait", 2),
    [MM_ANIM_LINK_NORMAL_JUMP_CLIMB_WAIT_FREE] = LINK_ANIM("gPlayerAnim_link_normal_jump_climb_wait_free", 2),
    [MM_ANIM_LINK_NORMAL_250JUMP_START] = LINK_ANIM("gPlayerAnim_link_normal_250jump_start", 5),

    // Side/Back movement
    [MM_ANIM_LINK_NORMAL_SIDE_WALK] = LINK_ANIM("gPlayerAnim_link_normal_side_walk", 12),
    [MM_ANIM_LINK_NORMAL_SIDE_WALK_FREE] = LINK_ANIM("gPlayerAnim_link_normal_side_walk_free", 12),
    [MM_ANIM_LINK_NORMAL_SIDE_WALKL_FREE] = LINK_ANIM("gPlayerAnim_link_normal_side_walkL_free", 12),
    [MM_ANIM_LINK_NORMAL_SIDE_WALKR_FREE] = LINK_ANIM("gPlayerAnim_link_normal_side_walkR_free", 12),
    [MM_ANIM_LINK_NORMAL_BACK_WALK] = LINK_ANIM("gPlayerAnim_link_normal_back_walk", 12),
    [MM_ANIM_LINK_NORMAL_BACK_RUN] = LINK_ANIM("gPlayerAnim_link_normal_back_run", 12),
    [MM_ANIM_LINK_NORMAL_BACK_BRAKE] = LINK_ANIM("gPlayerAnim_link_normal_back_brake", 5),
    [MM_ANIM_LINK_NORMAL_BACK_BRAKE_END] = LINK_ANIM("gPlayerAnim_link_normal_back_brake_end", 4),
    [MM_ANIM_LINK_NORMAL_BACKSPACE] = LINK_ANIM("gPlayerAnim_link_normal_backspace", 18),

    // Damage animations
    [MM_ANIM_LINK_NORMAL_FRONT_HIT] = LINK_ANIM("gPlayerAnim_link_normal_front_hit", 10),
    [MM_ANIM_LINK_NORMAL_BACK_HIT] = LINK_ANIM("gPlayerAnim_link_normal_back_hit", 10),
    [MM_ANIM_LINK_NORMAL_FRONT_SHIT] = LINK_ANIM("gPlayerAnim_link_normal_front_shit", 10),
    [MM_ANIM_LINK_NORMAL_FRONT_SHITR] = LINK_ANIM("gPlayerAnim_link_normal_front_shitR", 10),
    [MM_ANIM_LINK_NORMAL_BACK_SHIT] = LINK_ANIM("gPlayerAnim_link_normal_back_shit", 10),
    [MM_ANIM_LINK_NORMAL_BACK_SHITR] = LINK_ANIM("gPlayerAnim_link_normal_back_shitR", 10),
    [MM_ANIM_LINK_NORMAL_FRONT_DOWNA] = LINK_ANIM("gPlayerAnim_link_normal_front_downA", 20),
    [MM_ANIM_LINK_NORMAL_FRONT_DOWNB] = LINK_ANIM("gPlayerAnim_link_normal_front_downB", 90),
    [MM_ANIM_LINK_NORMAL_FRONT_DOWN_WAKE] = LINK_ANIM("gPlayerAnim_link_normal_front_down_wake", 30),
    [MM_ANIM_LINK_NORMAL_BACK_DOWNA] = LINK_ANIM("gPlayerAnim_link_normal_back_downA", 20),
    [MM_ANIM_LINK_NORMAL_BACK_DOWNB] = LINK_ANIM("gPlayerAnim_link_normal_back_downB", 90),
    [MM_ANIM_LINK_NORMAL_BACK_DOWN_WAKE] = LINK_ANIM("gPlayerAnim_link_normal_back_down_wake", 30),
    [MM_ANIM_LINK_NORMAL_ELECTRIC_SHOCK] = LINK_ANIM("gPlayerAnim_link_normal_electric_shock", 26),
    [MM_ANIM_LINK_NORMAL_ELECTRIC_SHOCK_END] = LINK_ANIM("gPlayerAnim_link_normal_electric_shock_end", 8),
    [MM_ANIM_LINK_NORMAL_ICE_DOWN] = LINK_ANIM("gPlayerAnim_link_normal_ice_down", 20),
    [MM_ANIM_LINK_NORMAL_DAMAGE_RUN_FREE] = LINK_ANIM("gPlayerAnim_link_normal_damage_run_free", 12),

    // Hip/Sit animations
    [MM_ANIM_LINK_NORMAL_HIP_DOWN] = LINK_ANIM("gPlayerAnim_link_normal_hip_down", 20),
    [MM_ANIM_LINK_NORMAL_HIP_DOWN_FREE] = LINK_ANIM("gPlayerAnim_link_normal_hip_down_free", 20),
    [MM_ANIM_LINK_NORMAL_HIP_DOWN_LONG] = LINK_ANIM("gPlayerAnim_link_normal_hip_down_long", 20),

    // Defense animations
    [MM_ANIM_LINK_NORMAL_DEFENSE] = LINK_ANIM("gPlayerAnim_link_normal_defense", 3),
    [MM_ANIM_LINK_NORMAL_DEFENSE_FREE] = LINK_ANIM("gPlayerAnim_link_normal_defense_free", 3),
    [MM_ANIM_LINK_NORMAL_DEFENSE_WAIT] = LINK_ANIM("gPlayerAnim_link_normal_defense_wait", 4),
    [MM_ANIM_LINK_NORMAL_DEFENSE_WAIT_FREE] = LINK_ANIM("gPlayerAnim_link_normal_defense_wait_free", 4),
    [MM_ANIM_LINK_NORMAL_DEFENSE_END] = LINK_ANIM("gPlayerAnim_link_normal_defense_end", 4),
    [MM_ANIM_LINK_NORMAL_DEFENSE_END_FREE] = LINK_ANIM("gPlayerAnim_link_normal_defense_end_free", 4),
    [MM_ANIM_LINK_NORMAL_DEFENSE_HIT] = LINK_ANIM("gPlayerAnim_link_normal_defense_hit", 7),
    [MM_ANIM_LINK_NORMAL_DEFENSE_KIRU] = LINK_ANIM("gPlayerAnim_link_normal_defense_kiru", 7),

    // Climb animations
    [MM_ANIM_LINK_NORMAL_CLIMB_STARTA] = LINK_ANIM("gPlayerAnim_link_normal_climb_startA", 15),
    [MM_ANIM_LINK_NORMAL_CLIMB_STARTB] = LINK_ANIM("gPlayerAnim_link_normal_climb_startB", 15),
    [MM_ANIM_LINK_NORMAL_CLIMB_DOWN] = LINK_ANIM("gPlayerAnim_link_normal_climb_down", 30),
    [MM_ANIM_LINK_NORMAL_CLIMB_UP] = LINK_ANIM("gPlayerAnim_link_normal_climb_up", 24),
    [MM_ANIM_LINK_NORMAL_CLIMB_UPL] = LINK_ANIM("gPlayerAnim_link_normal_climb_upL", 8),
    [MM_ANIM_LINK_NORMAL_CLIMB_UPR] = LINK_ANIM("gPlayerAnim_link_normal_climb_upR", 8),
    [MM_ANIM_LINK_NORMAL_CLIMB_ENDAL] = LINK_ANIM("gPlayerAnim_link_normal_climb_endAL", 16),
    [MM_ANIM_LINK_NORMAL_CLIMB_ENDAR] = LINK_ANIM("gPlayerAnim_link_normal_climb_endAR", 16),
    [MM_ANIM_LINK_NORMAL_CLIMB_ENDBL] = LINK_ANIM("gPlayerAnim_link_normal_climb_endBL", 28),
    [MM_ANIM_LINK_NORMAL_CLIMB_ENDBR] = LINK_ANIM("gPlayerAnim_link_normal_climb_endBR", 28),

    // Front climb animations
    [MM_ANIM_LINK_NORMAL_FCLIMB_STARTA] = LINK_ANIM("gPlayerAnim_link_normal_Fclimb_startA", 6),
    [MM_ANIM_LINK_NORMAL_FCLIMB_STARTB] = LINK_ANIM("gPlayerAnim_link_normal_Fclimb_startB", 6),
    [MM_ANIM_LINK_NORMAL_FCLIMB_UPL] = LINK_ANIM("gPlayerAnim_link_normal_Fclimb_upL", 8),
    [MM_ANIM_LINK_NORMAL_FCLIMB_UPR] = LINK_ANIM("gPlayerAnim_link_normal_Fclimb_upR", 8),
    [MM_ANIM_LINK_NORMAL_FCLIMB_SIDEL] = LINK_ANIM("gPlayerAnim_link_normal_Fclimb_sideL", 8),
    [MM_ANIM_LINK_NORMAL_FCLIMB_SIDER] = LINK_ANIM("gPlayerAnim_link_normal_Fclimb_sideR", 8),
    [MM_ANIM_LINK_NORMAL_FCLIMB_HOLD2UPL] = LINK_ANIM("gPlayerAnim_link_normal_Fclimb_hold2upL", 12),

    // Push/Pull animations
    [MM_ANIM_LINK_NORMAL_PUSH_START] = LINK_ANIM("gPlayerAnim_link_normal_push_start", 12),
    [MM_ANIM_LINK_NORMAL_PUSHING] = LINK_ANIM("gPlayerAnim_link_normal_pushing", 20),
    [MM_ANIM_LINK_NORMAL_PUSH_WAIT] = LINK_ANIM("gPlayerAnim_link_normal_push_wait", 10),
    [MM_ANIM_LINK_NORMAL_PUSH_WAIT_END] = LINK_ANIM("gPlayerAnim_link_normal_push_wait_end", 10),
    [MM_ANIM_LINK_NORMAL_PUSH_END] = LINK_ANIM("gPlayerAnim_link_normal_push_end", 12),
    [MM_ANIM_LINK_NORMAL_PULL_START] = LINK_ANIM("gPlayerAnim_link_normal_pull_start", 12),
    [MM_ANIM_LINK_NORMAL_PULL_START_FREE] = LINK_ANIM("gPlayerAnim_link_normal_pull_start_free", 12),
    [MM_ANIM_LINK_NORMAL_PULLING] = LINK_ANIM("gPlayerAnim_link_normal_pulling", 20),
    [MM_ANIM_LINK_NORMAL_PULLING_FREE] = LINK_ANIM("gPlayerAnim_link_normal_pulling_free", 20),
    [MM_ANIM_LINK_NORMAL_PULL_END] = LINK_ANIM("gPlayerAnim_link_normal_pull_end", 12),
    [MM_ANIM_LINK_NORMAL_PULL_END_FREE] = LINK_ANIM("gPlayerAnim_link_normal_pull_end_free", 12),

    // Carry/Throw animations
    [MM_ANIM_LINK_NORMAL_TAKE_OUT] = LINK_ANIM("gPlayerAnim_link_normal_take_out", 14),
    [MM_ANIM_LINK_NORMAL_PUT] = LINK_ANIM("gPlayerAnim_link_normal_put", 12),
    [MM_ANIM_LINK_NORMAL_PUT_FREE] = LINK_ANIM("gPlayerAnim_link_normal_put_free", 12),
    [MM_ANIM_LINK_NORMAL_THROW] = LINK_ANIM("gPlayerAnim_link_normal_throw", 16),
    [MM_ANIM_LINK_NORMAL_THROW_FREE] = LINK_ANIM("gPlayerAnim_link_normal_throw_free", 16),
    [MM_ANIM_LINK_NORMAL_CARRYB] = LINK_ANIM("gPlayerAnim_link_normal_carryB", 8),
    [MM_ANIM_LINK_NORMAL_CARRYB_FREE] = LINK_ANIM("gPlayerAnim_link_normal_carryB_free", 8),
    [MM_ANIM_LINK_NORMAL_CARRYB_WAIT] = LINK_ANIM("gPlayerAnim_link_normal_carryB_wait", 30),
    [MM_ANIM_LINK_NORMAL_NOCARRY_FREE] = LINK_ANIM("gPlayerAnim_link_normal_nocarry_free", 12),
    [MM_ANIM_LINK_NORMAL_NOCARRY_FREE_END] = LINK_ANIM("gPlayerAnim_link_normal_nocarry_free_end", 12),
    [MM_ANIM_LINK_NORMAL_NOCARRY_FREE_WAIT] = LINK_ANIM("gPlayerAnim_link_normal_nocarry_free_wait", 30),

    // Hang animations
    [MM_ANIM_LINK_NORMAL_HANG_UP_DOWN] = LINK_ANIM("gPlayerAnim_link_normal_hang_up_down", 30),

    // Slope animations
    [MM_ANIM_LINK_NORMAL_UP_SLOPE_SLIP] = LINK_ANIM("gPlayerAnim_link_normal_up_slope_slip", 6),
    [MM_ANIM_LINK_NORMAL_UP_SLOPE_SLIP_END] = LINK_ANIM("gPlayerAnim_link_normal_up_slope_slip_end", 10),
    [MM_ANIM_LINK_NORMAL_UP_SLOPE_SLIP_END_FREE] = LINK_ANIM("gPlayerAnim_link_normal_up_slope_slip_end_free", 10),
    [MM_ANIM_LINK_NORMAL_UP_SLOPE_SLIP_END_LONG] = LINK_ANIM("gPlayerAnim_link_normal_up_slope_slip_end_long", 10),
    [MM_ANIM_LINK_NORMAL_DOWN_SLOPE_SLIP] = LINK_ANIM("gPlayerAnim_link_normal_down_slope_slip", 6),
    [MM_ANIM_LINK_NORMAL_DOWN_SLOPE_SLIP_END] = LINK_ANIM("gPlayerAnim_link_normal_down_slope_slip_end", 10),
    [MM_ANIM_LINK_NORMAL_DOWN_SLOPE_SLIP_END_FREE] = LINK_ANIM("gPlayerAnim_link_normal_down_slope_slip_end_free", 10),
    [MM_ANIM_LINK_NORMAL_DOWN_SLOPE_SLIP_END_LONG] = LINK_ANIM("gPlayerAnim_link_normal_down_slope_slip_end_long", 10),

    // Step up animations
    [MM_ANIM_LINK_NORMAL_100STEP_UP] = LINK_ANIM("gPlayerAnim_link_normal_100step_up", 17),
    [MM_ANIM_LINK_NORMAL_150STEP_UP] = LINK_ANIM("gPlayerAnim_link_normal_150step_up", 23),

    // Turn animations
    [MM_ANIM_LINK_NORMAL_45_TURN] = LINK_ANIM("gPlayerAnim_link_normal_45_turn", 5),
    [MM_ANIM_LINK_NORMAL_45_TURN_FREE] = LINK_ANIM("gPlayerAnim_link_normal_45_turn_free", 5),
    [MM_ANIM_LINK_NORMAL_WAITL2WAIT] = LINK_ANIM("gPlayerAnim_link_normal_waitL2wait", 5),
    [MM_ANIM_LINK_NORMAL_WAITR2WAIT] = LINK_ANIM("gPlayerAnim_link_normal_waitR2wait", 5),
    [MM_ANIM_LINK_NORMAL_WAIT2WAITR] = LINK_ANIM("gPlayerAnim_link_normal_wait2waitR", 5),

    // State change animations
    [MM_ANIM_LINK_NORMAL_FREE2FREE] = LINK_ANIM("gPlayerAnim_link_normal_free2free", 5),
    [MM_ANIM_LINK_NORMAL_FREE2FREEB] = LINK_ANIM("gPlayerAnim_link_normal_free2freeB", 5),
    [MM_ANIM_LINK_NORMAL_NORMAL2FREE] = LINK_ANIM("gPlayerAnim_link_normal_normal2free", 5),
    [MM_ANIM_LINK_NORMAL_FIGHTER2FREE] = LINK_ANIM("gPlayerAnim_link_normal_fighter2free", 5),
    [MM_ANIM_LINK_NORMAL_NORMAL2FIGHTER] = LINK_ANIM("gPlayerAnim_link_normal_normal2fighter", 5),
    [MM_ANIM_LINK_NORMAL_NORMAL2FIGHTER_FREE] = LINK_ANIM("gPlayerAnim_link_normal_normal2fighter_free", 5),
    [MM_ANIM_LINK_NORMAL_FREE2FIGHTER_FREE] = LINK_ANIM("gPlayerAnim_link_normal_free2fighter_free", 5),

    // Item/Check animations
    [MM_ANIM_LINK_NORMAL_CHECK] = LINK_ANIM("gPlayerAnim_link_normal_check", 10),
    [MM_ANIM_LINK_NORMAL_CHECK_FREE] = LINK_ANIM("gPlayerAnim_link_normal_check_free", 10),
    [MM_ANIM_LINK_NORMAL_CHECK_WAIT] = LINK_ANIM("gPlayerAnim_link_normal_check_wait", 4),
    [MM_ANIM_LINK_NORMAL_CHECK_WAIT_FREE] = LINK_ANIM("gPlayerAnim_link_normal_check_wait_free", 4),
    [MM_ANIM_LINK_NORMAL_CHECK_END] = LINK_ANIM("gPlayerAnim_link_normal_check_end", 10),
    [MM_ANIM_LINK_NORMAL_CHECK_END_FREE] = LINK_ANIM("gPlayerAnim_link_normal_check_end_free", 10),
    [MM_ANIM_LINK_NORMAL_GIVE_OTHER] = LINK_ANIM("gPlayerAnim_link_normal_give_other", 30),
    [MM_ANIM_LINK_NORMAL_BOX_KICK] = LINK_ANIM("gPlayerAnim_link_normal_box_kick", 30),

    // Talk animations
    [MM_ANIM_LINK_NORMAL_TALK_FREE] = LINK_ANIM("gPlayerAnim_link_normal_talk_free", 10),
    [MM_ANIM_LINK_NORMAL_TALK_FREE_WAIT] = LINK_ANIM("gPlayerAnim_link_normal_talk_free_wait", 4),

    // Bomb animations
    [MM_ANIM_LINK_NORMAL_NORMAL2BOM] = LINK_ANIM("gPlayerAnim_link_normal_normal2bom", 10),
    [MM_ANIM_LINK_NORMAL_FREE2BOM] = LINK_ANIM("gPlayerAnim_link_normal_free2bom", 10),
    [MM_ANIM_LINK_NORMAL_LONG2BOM] = LINK_ANIM("gPlayerAnim_link_normal_long2bom", 10),
    [MM_ANIM_LINK_NORMAL_LIGHT_BOM] = LINK_ANIM("gPlayerAnim_link_normal_light_bom", 14),
    [MM_ANIM_LINK_NORMAL_LIGHT_BOM_END] = LINK_ANIM("gPlayerAnim_link_normal_light_bom_end", 10),

    // Ocarina animations
    [MM_ANIM_LINK_NORMAL_OKARINA_START] = LINK_ANIM("gPlayerAnim_link_normal_okarina_start", 10),
    [MM_ANIM_LINK_NORMAL_OKARINA_END] = LINK_ANIM("gPlayerAnim_link_normal_okarina_end", 8),
    [MM_ANIM_LINK_NORMAL_OKARINA_SWING] = LINK_ANIM("gPlayerAnim_link_normal_okarina_swing", 10),

    // Redead attack
    [MM_ANIM_LINK_NORMAL_RE_DEAD_ATTACK] = LINK_ANIM("gPlayerAnim_link_normal_re_dead_attack", 48),
    [MM_ANIM_LINK_NORMAL_RE_DEAD_ATTACK_WAIT] = LINK_ANIM("gPlayerAnim_link_normal_re_dead_attack_wait", 4),

    // New roll/side jump
    [MM_ANIM_LINK_NORMAL_NEWROLL_JUMP_20F] = LINK_ANIM("gPlayerAnim_link_normal_newroll_jump_20f", 20),
    [MM_ANIM_LINK_NORMAL_NEWROLL_JUMP_END_20F] = LINK_ANIM("gPlayerAnim_link_normal_newroll_jump_end_20f", 8),
    [MM_ANIM_LINK_NORMAL_NEWSIDE_JUMP_20F] = LINK_ANIM("gPlayerAnim_link_normal_newside_jump_20f", 20),
    [MM_ANIM_LINK_NORMAL_NEWSIDE_JUMP_END_20F] = LINK_ANIM("gPlayerAnim_link_normal_newside_jump_end_20f", 8),

    // Water/Swim transitions
    [MM_ANIM_LINK_NORMAL_RUN_JUMP_WATER_FALL] = LINK_ANIM("gPlayerAnim_link_normal_run_jump_water_fall", 10),
    [MM_ANIM_LINK_NORMAL_RUN_JUMP_WATER_FALL_WAIT] = LINK_ANIM("gPlayerAnim_link_normal_run_jump_water_fall_wait", 4),

    // Free wait states
    [MM_ANIM_LINK_NORMAL_WAITL_FREE] = LINK_ANIM("gPlayerAnim_link_normal_waitL_free", 4),
    [MM_ANIM_LINK_NORMAL_WAITR_FREE] = LINK_ANIM("gPlayerAnim_link_normal_waitR_free", 4),

    // ========================================
    // Fighter (Sword) Animations
    // ========================================
    [MM_ANIM_LINK_FIGHTER_WAIT_LONG] = LINK_ANIM("gPlayerAnim_link_fighter_wait_long", 32),
    [MM_ANIM_LINK_FIGHTER_WAITL_LONG] = LINK_ANIM("gPlayerAnim_link_fighter_waitL_long", 4),
    [MM_ANIM_LINK_FIGHTER_WAITR_LONG] = LINK_ANIM("gPlayerAnim_link_fighter_waitR_long", 4),
    [MM_ANIM_LINK_FIGHTER_WAITL2WAIT_LONG] = LINK_ANIM("gPlayerAnim_link_fighter_waitL2wait_long", 5),
    [MM_ANIM_LINK_FIGHTER_WAITR2WAIT_LONG] = LINK_ANIM("gPlayerAnim_link_fighter_waitR2wait_long", 5),
    [MM_ANIM_LINK_FIGHTER_WAIT2WAITR_LONG] = LINK_ANIM("gPlayerAnim_link_fighter_wait2waitR_long", 5),

    // Movement with sword
    [MM_ANIM_LINK_FIGHTER_RUN] = LINK_ANIM("gPlayerAnim_link_fighter_run", 16),
    [MM_ANIM_LINK_FIGHTER_RUN_LONG] = LINK_ANIM("gPlayerAnim_link_fighter_run_long", 16),
    [MM_ANIM_LINK_FIGHTER_WALK_LONG] = LINK_ANIM("gPlayerAnim_link_fighter_walk_long", 17),
    [MM_ANIM_LINK_FIGHTER_WALK_ENDL_LONG] = LINK_ANIM("gPlayerAnim_link_fighter_walk_endL_long", 8),
    [MM_ANIM_LINK_FIGHTER_WALK_ENDR_LONG] = LINK_ANIM("gPlayerAnim_link_fighter_walk_endR_long", 8),
    [MM_ANIM_LINK_FIGHTER_SIDE_WALK_LONG] = LINK_ANIM("gPlayerAnim_link_fighter_side_walk_long", 12),
    [MM_ANIM_LINK_FIGHTER_SIDE_WALKL_LONG] = LINK_ANIM("gPlayerAnim_link_fighter_side_walkL_long", 12),
    [MM_ANIM_LINK_FIGHTER_SIDE_WALKR_LONG] = LINK_ANIM("gPlayerAnim_link_fighter_side_walkR_long", 12),
    [MM_ANIM_LINK_FIGHTER_DAMAGE_RUN] = LINK_ANIM("gPlayerAnim_link_fighter_damage_run", 12),
    [MM_ANIM_LINK_FIGHTER_DAMAGE_RUN_LONG] = LINK_ANIM("gPlayerAnim_link_fighter_damage_run_long", 12),

    // Sword transitions
    [MM_ANIM_LINK_FIGHTER_NORMAL2FIGHTER] = LINK_ANIM("gPlayerAnim_link_fighter_normal2fighter", 5),
    [MM_ANIM_LINK_FIGHTER_FIGHTER2LONG] = LINK_ANIM("gPlayerAnim_link_fighter_fighter2long", 5),

    // Normal slash
    [MM_ANIM_LINK_FIGHTER_NORMAL_KIRU] = LINK_ANIM("gPlayerAnim_link_fighter_normal_kiru", 8),
    [MM_ANIM_LINK_FIGHTER_NORMAL_KIRU_END] = LINK_ANIM("gPlayerAnim_link_fighter_normal_kiru_end", 10),
    [MM_ANIM_LINK_FIGHTER_NORMAL_KIRU_ENDR] = LINK_ANIM("gPlayerAnim_link_fighter_normal_kiru_endR", 10),
    [MM_ANIM_LINK_FIGHTER_NORMAL_KIRU_FINSH] = LINK_ANIM("gPlayerAnim_link_fighter_normal_kiru_finsh", 10),
    [MM_ANIM_LINK_FIGHTER_NORMAL_KIRU_FINSH_END] = LINK_ANIM("gPlayerAnim_link_fighter_normal_kiru_finsh_end", 12),

    // Left normal slash
    [MM_ANIM_LINK_FIGHTER_LNORMAL_KIRU] = LINK_ANIM("gPlayerAnim_link_fighter_Lnormal_kiru", 8),
    [MM_ANIM_LINK_FIGHTER_LNORMAL_KIRU_END] = LINK_ANIM("gPlayerAnim_link_fighter_Lnormal_kiru_end", 10),
    [MM_ANIM_LINK_FIGHTER_LNORMAL_KIRU_FINSH] = LINK_ANIM("gPlayerAnim_link_fighter_Lnormal_kiru_finsh", 10),
    [MM_ANIM_LINK_FIGHTER_LNORMAL_KIRU_FINSH_END] = LINK_ANIM("gPlayerAnim_link_fighter_Lnormal_kiru_finsh_end", 12),

    // Side slashes
    [MM_ANIM_LINK_FIGHTER_RSIDE_KIRU] = LINK_ANIM("gPlayerAnim_link_fighter_Rside_kiru", 8),
    [MM_ANIM_LINK_FIGHTER_RSIDE_KIRU_END] = LINK_ANIM("gPlayerAnim_link_fighter_Rside_kiru_end", 10),
    [MM_ANIM_LINK_FIGHTER_RSIDE_KIRU_FINSH] = LINK_ANIM("gPlayerAnim_link_fighter_Rside_kiru_finsh", 10),
    [MM_ANIM_LINK_FIGHTER_RSIDE_KIRU_FINSH_END] = LINK_ANIM("gPlayerAnim_link_fighter_Rside_kiru_finsh_end", 12),
    [MM_ANIM_LINK_FIGHTER_LSIDE_KIRU] = LINK_ANIM("gPlayerAnim_link_fighter_Lside_kiru", 8),
    [MM_ANIM_LINK_FIGHTER_LSIDE_KIRU_END] = LINK_ANIM("gPlayerAnim_link_fighter_Lside_kiru_end", 10),
    [MM_ANIM_LINK_FIGHTER_LSIDE_KIRU_FINSH] = LINK_ANIM("gPlayerAnim_link_fighter_Lside_kiru_finsh", 10),
    [MM_ANIM_LINK_FIGHTER_LSIDE_KIRU_FINSH_END] = LINK_ANIM("gPlayerAnim_link_fighter_Lside_kiru_finsh_end", 12),

    // Double side slashes
    [MM_ANIM_LINK_FIGHTER_LRSIDE_KIRU] = LINK_ANIM("gPlayerAnim_link_fighter_LRside_kiru", 8),
    [MM_ANIM_LINK_FIGHTER_LRSIDE_KIRU_END] = LINK_ANIM("gPlayerAnim_link_fighter_LRside_kiru_end", 10),
    [MM_ANIM_LINK_FIGHTER_LRSIDE_KIRU_FINSH] = LINK_ANIM("gPlayerAnim_link_fighter_LRside_kiru_finsh", 10),
    [MM_ANIM_LINK_FIGHTER_LRSIDE_KIRU_FINSH_END] = LINK_ANIM("gPlayerAnim_link_fighter_LRside_kiru_finsh_end", 12),
    [MM_ANIM_LINK_FIGHTER_LLSIDE_KIRU] = LINK_ANIM("gPlayerAnim_link_fighter_LLside_kiru", 8),
    [MM_ANIM_LINK_FIGHTER_LLSIDE_KIRU_END] = LINK_ANIM("gPlayerAnim_link_fighter_LLside_kiru_end", 10),
    [MM_ANIM_LINK_FIGHTER_LLSIDE_KIRU_FINSH] = LINK_ANIM("gPlayerAnim_link_fighter_LLside_kiru_finsh", 10),
    [MM_ANIM_LINK_FIGHTER_LLSIDE_KIRU_FINSH_END] = LINK_ANIM("gPlayerAnim_link_fighter_LLside_kiru_finsh_end", 12),

    // Pierce/Stab
    [MM_ANIM_LINK_FIGHTER_PIERCE_KIRU] = LINK_ANIM("gPlayerAnim_link_fighter_pierce_kiru", 8),
    [MM_ANIM_LINK_FIGHTER_PIERCE_KIRU_END] = LINK_ANIM("gPlayerAnim_link_fighter_pierce_kiru_end", 10),
    [MM_ANIM_LINK_FIGHTER_PIERCE_KIRU_FINSH] = LINK_ANIM("gPlayerAnim_link_fighter_pierce_kiru_finsh", 10),
    [MM_ANIM_LINK_FIGHTER_PIERCE_KIRU_FINSH_END] = LINK_ANIM("gPlayerAnim_link_fighter_pierce_kiru_finsh_end", 12),
    [MM_ANIM_LINK_FIGHTER_LPIERCE_KIRU] = LINK_ANIM("gPlayerAnim_link_fighter_Lpierce_kiru", 8),
    [MM_ANIM_LINK_FIGHTER_LPIERCE_KIRU_END] = LINK_ANIM("gPlayerAnim_link_fighter_Lpierce_kiru_end", 10),
    [MM_ANIM_LINK_FIGHTER_LPIERCE_KIRU_FINSH] = LINK_ANIM("gPlayerAnim_link_fighter_Lpierce_kiru_finsh", 10),
    [MM_ANIM_LINK_FIGHTER_LPIERCE_KIRU_FINSH_END] = LINK_ANIM("gPlayerAnim_link_fighter_Lpierce_kiru_finsh_end", 12),

    // Rolling slashes
    [MM_ANIM_LINK_FIGHTER_ROLLING_KIRU] = LINK_ANIM("gPlayerAnim_link_fighter_rolling_kiru", 10),
    [MM_ANIM_LINK_FIGHTER_ROLLING_KIRU_END] = LINK_ANIM("gPlayerAnim_link_fighter_rolling_kiru_end", 10),
    [MM_ANIM_LINK_FIGHTER_LROLLING_KIRU] = LINK_ANIM("gPlayerAnim_link_fighter_Lrolling_kiru", 10),
    [MM_ANIM_LINK_FIGHTER_LROLLING_KIRU_END] = LINK_ANIM("gPlayerAnim_link_fighter_Lrolling_kiru_end", 10),
    [MM_ANIM_LINK_FIGHTER_WROLLING_KIRU] = LINK_ANIM("gPlayerAnim_link_fighter_Wrolling_kiru", 10),
    [MM_ANIM_LINK_FIGHTER_WROLLING_KIRU_END] = LINK_ANIM("gPlayerAnim_link_fighter_Wrolling_kiru_end", 10),

    // Turn slashes
    [MM_ANIM_LINK_FIGHTER_TURN_KIRUL] = LINK_ANIM("gPlayerAnim_link_fighter_turn_kiruL", 10),
    [MM_ANIM_LINK_FIGHTER_TURN_KIRUL_END] = LINK_ANIM("gPlayerAnim_link_fighter_turn_kiruL_end", 10),
    [MM_ANIM_LINK_FIGHTER_TURN_KIRUR] = LINK_ANIM("gPlayerAnim_link_fighter_turn_kiruR", 10),
    [MM_ANIM_LINK_FIGHTER_TURN_KIRUR_END] = LINK_ANIM("gPlayerAnim_link_fighter_turn_kiruR_end", 10),

    // Power (spin) attacks
    [MM_ANIM_LINK_FIGHTER_POWER_KIRU_START] = LINK_ANIM("gPlayerAnim_link_fighter_power_kiru_start", 8),
    [MM_ANIM_LINK_FIGHTER_POWER_KIRU_STARTL] = LINK_ANIM("gPlayerAnim_link_fighter_power_kiru_startL", 8),
    [MM_ANIM_LINK_FIGHTER_POWER_KIRU_WAIT] = LINK_ANIM("gPlayerAnim_link_fighter_power_kiru_wait", 4),
    [MM_ANIM_LINK_FIGHTER_POWER_KIRU_WAIT_END] = LINK_ANIM("gPlayerAnim_link_fighter_power_kiru_wait_end", 6),
    [MM_ANIM_LINK_FIGHTER_POWER_KIRU_WALK] = LINK_ANIM("gPlayerAnim_link_fighter_power_kiru_walk", 17),
    [MM_ANIM_LINK_FIGHTER_POWER_KIRU_SIDE_WALK] = LINK_ANIM("gPlayerAnim_link_fighter_power_kiru_side_walk", 12),
    [MM_ANIM_LINK_FIGHTER_POWER_JUMP_KIRU_END] = LINK_ANIM("gPlayerAnim_link_fighter_power_jump_kiru_end", 20),

    // Left power attacks
    [MM_ANIM_LINK_FIGHTER_LPOWER_KIRU_START] = LINK_ANIM("gPlayerAnim_link_fighter_Lpower_kiru_start", 8),
    [MM_ANIM_LINK_FIGHTER_LPOWER_KIRU_WAIT] = LINK_ANIM("gPlayerAnim_link_fighter_Lpower_kiru_wait", 4),
    [MM_ANIM_LINK_FIGHTER_LPOWER_KIRU_WAIT_END] = LINK_ANIM("gPlayerAnim_link_fighter_Lpower_kiru_wait_end", 6),
    [MM_ANIM_LINK_FIGHTER_LPOWER_KIRU_WALK] = LINK_ANIM("gPlayerAnim_link_fighter_Lpower_kiru_walk", 17),
    [MM_ANIM_LINK_FIGHTER_LPOWER_KIRU_SIDE_WALK] = LINK_ANIM("gPlayerAnim_link_fighter_Lpower_kiru_side_walk", 12),
    [MM_ANIM_LINK_FIGHTER_LPOWER_JUMP_KIRU] = LINK_ANIM("gPlayerAnim_link_fighter_Lpower_jump_kiru", 10),
    [MM_ANIM_LINK_FIGHTER_LPOWER_JUMP_KIRU_END] = LINK_ANIM("gPlayerAnim_link_fighter_Lpower_jump_kiru_end", 10),
    [MM_ANIM_LINK_FIGHTER_LPOWER_JUMP_KIRU_HIT] = LINK_ANIM("gPlayerAnim_link_fighter_Lpower_jump_kiru_hit", 10),

    // Jump attacks
    [MM_ANIM_LINK_FIGHTER_JUMP_KIRU_FINSH] = LINK_ANIM("gPlayerAnim_link_fighter_jump_kiru_finsh", 20),
    [MM_ANIM_LINK_FIGHTER_JUMP_KIRU_FINSH_END] = LINK_ANIM("gPlayerAnim_link_fighter_jump_kiru_finsh_end", 10),
    [MM_ANIM_LINK_FIGHTER_JUMP_ROLLKIRU] = LINK_ANIM("gPlayerAnim_link_fighter_jump_rollkiru", 20),

    // Backturn jump (backflip slash) - TESTED AND WORKING!
    [MM_ANIM_LINK_FIGHTER_BACKTURN_JUMP] = LINK_ANIM("gPlayerAnim_link_fighter_backturn_jump", 15),
    [MM_ANIM_LINK_FIGHTER_BACKTURN_JUMP_END] = LINK_ANIM("gPlayerAnim_link_fighter_backturn_jump_end", 10),
    [MM_ANIM_LINK_FIGHTER_BACKTURN_JUMP_ENDR] = LINK_ANIM("gPlayerAnim_link_fighter_backturn_jump_endR", 10),

    // Front jump
    [MM_ANIM_LINK_FIGHTER_FRONT_JUMP] = LINK_ANIM("gPlayerAnim_link_fighter_front_jump", 15),
    [MM_ANIM_LINK_FIGHTER_FRONT_JUMP_END] = LINK_ANIM("gPlayerAnim_link_fighter_front_jump_end", 10),
    [MM_ANIM_LINK_FIGHTER_FRONT_JUMP_ENDR] = LINK_ANIM("gPlayerAnim_link_fighter_front_jump_endR", 10),

    // Side jumps
    [MM_ANIM_LINK_FIGHTER_LSIDE_JUMP] = LINK_ANIM("gPlayerAnim_link_fighter_Lside_jump", 15),
    [MM_ANIM_LINK_FIGHTER_LSIDE_JUMP_END] = LINK_ANIM("gPlayerAnim_link_fighter_Lside_jump_end", 10),
    [MM_ANIM_LINK_FIGHTER_LSIDE_JUMP_ENDL] = LINK_ANIM("gPlayerAnim_link_fighter_Lside_jump_endL", 10),
    [MM_ANIM_LINK_FIGHTER_RSIDE_JUMP] = LINK_ANIM("gPlayerAnim_link_fighter_Rside_jump", 15),
    [MM_ANIM_LINK_FIGHTER_RSIDE_JUMP_END] = LINK_ANIM("gPlayerAnim_link_fighter_Rside_jump_end", 10),
    [MM_ANIM_LINK_FIGHTER_RSIDE_JUMP_ENDR] = LINK_ANIM("gPlayerAnim_link_fighter_Rside_jump_endR", 10),

    // Rebound
    [MM_ANIM_LINK_FIGHTER_REBOUND] = LINK_ANIM("gPlayerAnim_link_fighter_rebound", 10),
    [MM_ANIM_LINK_FIGHTER_REBOUNDR] = LINK_ANIM("gPlayerAnim_link_fighter_reboundR", 10),
    [MM_ANIM_LINK_FIGHTER_REBOUND_LONG] = LINK_ANIM("gPlayerAnim_link_fighter_rebound_long", 10),
    [MM_ANIM_LINK_FIGHTER_REBOUND_LONGR] = LINK_ANIM("gPlayerAnim_link_fighter_rebound_longR", 10),

    // Landing roll
    [MM_ANIM_LINK_FIGHTER_LANDING_ROLL_LONG] = LINK_ANIM("gPlayerAnim_link_fighter_landing_roll_long", 25),

    // Defense with sword
    [MM_ANIM_LINK_FIGHTER_DEFENSE_LONG_HIT] = LINK_ANIM("gPlayerAnim_link_fighter_defense_long_hit", 10),

    // ========================================
    // Anchor (Z-Target) Animations
    // ========================================
    [MM_ANIM_LINK_ANCHOR_WAITL] = LINK_ANIM("gPlayerAnim_link_anchor_waitL", 4),
    [MM_ANIM_LINK_ANCHOR_WAITR] = LINK_ANIM("gPlayerAnim_link_anchor_waitR", 4),
    [MM_ANIM_LINK_ANCHOR_WAITL2DEFENSE] = LINK_ANIM("gPlayerAnim_link_anchor_waitL2defense", 3),
    [MM_ANIM_LINK_ANCHOR_WAITR2DEFENSE] = LINK_ANIM("gPlayerAnim_link_anchor_waitR2defense", 3),
    [MM_ANIM_LINK_ANCHOR_WAITL2DEFENSE_LONG] = LINK_ANIM("gPlayerAnim_link_anchor_waitL2defense_long", 3),
    [MM_ANIM_LINK_ANCHOR_WAITR2DEFENSE_LONG] = LINK_ANIM("gPlayerAnim_link_anchor_waitR2defense_long", 3),
    [MM_ANIM_LINK_ANCHOR_ANCHOR2FIGHTER] = LINK_ANIM("gPlayerAnim_link_anchor_anchor2fighter", 5),
    [MM_ANIM_LINK_ANCHOR_SIDE_WALKL] = LINK_ANIM("gPlayerAnim_link_anchor_side_walkL", 12),
    [MM_ANIM_LINK_ANCHOR_SIDE_WALKR] = LINK_ANIM("gPlayerAnim_link_anchor_side_walkR", 12),
    [MM_ANIM_LINK_ANCHOR_BACK_WALK] = LINK_ANIM("gPlayerAnim_link_anchor_back_walk", 12),
    [MM_ANIM_LINK_ANCHOR_BACK_BRAKE] = LINK_ANIM("gPlayerAnim_link_anchor_back_brake", 5),
    [MM_ANIM_LINK_ANCHOR_DEFENSE_HIT] = LINK_ANIM("gPlayerAnim_link_anchor_defense_hit", 7),
    [MM_ANIM_LINK_ANCHOR_FRONT_HITR] = LINK_ANIM("gPlayerAnim_link_anchor_front_hitR", 10),
    [MM_ANIM_LINK_ANCHOR_BACK_HITR] = LINK_ANIM("gPlayerAnim_link_anchor_back_hitR", 10),
    [MM_ANIM_LINK_ANCHOR_DEFENSE_LONG_HITL] = LINK_ANIM("gPlayerAnim_link_anchor_defense_long_hitL", 10),
    [MM_ANIM_LINK_ANCHOR_DEFENSE_LONG_HITR] = LINK_ANIM("gPlayerAnim_link_anchor_defense_long_hitR", 10),
    [MM_ANIM_LINK_ANCHOR_LANDINGR] = LINK_ANIM("gPlayerAnim_link_anchor_landingR", 7),

    // Anchor attacks - using placeholder frame counts
    [MM_ANIM_LINK_ANCHOR_NORMAL_KIRU_FINSH_ENDR] = LINK_ANIM("gPlayerAnim_link_anchor_normal_kiru_finsh_endR", 12),
    [MM_ANIM_LINK_ANCHOR_LNORMAL_KIRU_ENDR] = LINK_ANIM("gPlayerAnim_link_anchor_Lnormal_kiru_endR", 10),
    [MM_ANIM_LINK_ANCHOR_LNORMAL_KIRU_FINSH_ENDR] = LINK_ANIM("gPlayerAnim_link_anchor_Lnormal_kiru_finsh_endR", 12),
    [MM_ANIM_LINK_ANCHOR_PIERCE_KIRU_ENDR] = LINK_ANIM("gPlayerAnim_link_anchor_pierce_kiru_endR", 10),
    [MM_ANIM_LINK_ANCHOR_PIERCE_KIRU_FINSH_ENDR] = LINK_ANIM("gPlayerAnim_link_anchor_pierce_kiru_finsh_endR", 12),
    [MM_ANIM_LINK_ANCHOR_ROLLING_KIRU_ENDR] = LINK_ANIM("gPlayerAnim_link_anchor_rolling_kiru_endR", 10),
    [MM_ANIM_LINK_ANCHOR_LROLLING_KIRU_ENDR] = LINK_ANIM("gPlayerAnim_link_anchor_Lrolling_kiru_endR", 10),
    [MM_ANIM_LINK_ANCHOR_LSIDE_KIRU_ENDR] = LINK_ANIM("gPlayerAnim_link_anchor_Lside_kiru_endR", 10),
    [MM_ANIM_LINK_ANCHOR_LSIDE_KIRU_FINSH_ENDR] = LINK_ANIM("gPlayerAnim_link_anchor_Lside_kiru_finsh_endR", 12),
    [MM_ANIM_LINK_ANCHOR_RSIDE_KIRU_ENDR] = LINK_ANIM("gPlayerAnim_link_anchor_Rside_kiru_endR", 10),
    [MM_ANIM_LINK_ANCHOR_RSIDE_KIRU_FINSH_ENDR] = LINK_ANIM("gPlayerAnim_link_anchor_Rside_kiru_finsh_endR", 12),
    [MM_ANIM_LINK_ANCHOR_LRSIDE_KIRU_ENDR] = LINK_ANIM("gPlayerAnim_link_anchor_LRside_kiru_endR", 10),
    [MM_ANIM_LINK_ANCHOR_LRSIDE_KIRU_FINSH_ENDL] = LINK_ANIM("gPlayerAnim_link_anchor_LRside_kiru_finsh_endL", 12),
    [MM_ANIM_LINK_ANCHOR_LLSIDE_KIRU_ENDL] = LINK_ANIM("gPlayerAnim_link_anchor_LLside_kiru_endL", 10),
    [MM_ANIM_LINK_ANCHOR_LLSIDE_KIRU_FINSH_ENDR] = LINK_ANIM("gPlayerAnim_link_anchor_LLside_kiru_finsh_endR", 12),
    [MM_ANIM_LINK_ANCHOR_LPIERCE_KIRU_ENDL] = LINK_ANIM("gPlayerAnim_link_anchor_Lpierce_kiru_endL", 10),
    [MM_ANIM_LINK_ANCHOR_LPIERCE_KIRU_FINSH_ENDR] = LINK_ANIM("gPlayerAnim_link_anchor_Lpierce_kiru_finsh_endR", 12),

    // The rest will use placeholder frame counts (10) for now
    // TODO: Parse MM decomp XML to get actual frame counts

    // Bow animations
    [MM_ANIM_LINK_BOW_BOW_READY] = LINK_ANIM("gPlayerAnim_link_bow_bow_ready", 10),
    [MM_ANIM_LINK_BOW_BOW_WAIT] = LINK_ANIM("gPlayerAnim_link_bow_bow_wait", 4),
    [MM_ANIM_LINK_BOW_BOW_SHOOT_END] = LINK_ANIM("gPlayerAnim_link_bow_bow_shoot_end", 10),
    [MM_ANIM_LINK_BOW_BOW_SHOOT_NEXT] = LINK_ANIM("gPlayerAnim_link_bow_bow_shoot_next", 10),
    [MM_ANIM_LINK_BOW_WALK2READY] = LINK_ANIM("gPlayerAnim_link_bow_walk2ready", 10),
    [MM_ANIM_LINK_BOW_SIDE_WALK] = LINK_ANIM("gPlayerAnim_link_bow_side_walk", 12),
    [MM_ANIM_LINK_BOW_DEFENSE] = LINK_ANIM("gPlayerAnim_link_bow_defense", 3),
    [MM_ANIM_LINK_BOW_DEFENSE_WAIT] = LINK_ANIM("gPlayerAnim_link_bow_defense_wait", 4),

    // Hookshot animations
    [MM_ANIM_LINK_HOOK_SHOT_READY] = LINK_ANIM("gPlayerAnim_link_hook_shot_ready", 10),
    [MM_ANIM_LINK_HOOK_WAIT] = LINK_ANIM("gPlayerAnim_link_hook_wait", 4),
    [MM_ANIM_LINK_HOOK_WALK2READY] = LINK_ANIM("gPlayerAnim_link_hook_walk2ready", 10),
    [MM_ANIM_LINK_HOOK_FLY_START] = LINK_ANIM("gPlayerAnim_link_hook_fly_start", 8),
    [MM_ANIM_LINK_HOOK_FLY_WAIT] = LINK_ANIM("gPlayerAnim_link_hook_fly_wait", 4),

    // Bottle animations
    [MM_ANIM_LINK_BOTTLE_BUG_IN] = LINK_ANIM("gPlayerAnim_link_bottle_bug_in", 30),
    [MM_ANIM_LINK_BOTTLE_BUG_OUT] = LINK_ANIM("gPlayerAnim_link_bottle_bug_out", 20),
    [MM_ANIM_LINK_BOTTLE_BUG_MISS] = LINK_ANIM("gPlayerAnim_link_bottle_bug_miss", 30),
    [MM_ANIM_LINK_BOTTLE_FISH_IN] = LINK_ANIM("gPlayerAnim_link_bottle_fish_in", 30),
    [MM_ANIM_LINK_BOTTLE_FISH_OUT] = LINK_ANIM("gPlayerAnim_link_bottle_fish_out", 20),
    [MM_ANIM_LINK_BOTTLE_FISH_MISS] = LINK_ANIM("gPlayerAnim_link_bottle_fish_miss", 30),
    [MM_ANIM_LINK_BOTTLE_DRINK_DEMO_START] = LINK_ANIM("gPlayerAnim_link_bottle_drink_demo_start", 20),
    [MM_ANIM_LINK_BOTTLE_DRINK_DEMO_WAIT] = LINK_ANIM("gPlayerAnim_link_bottle_drink_demo_wait", 4),
    [MM_ANIM_LINK_BOTTLE_DRINK_DEMO_END] = LINK_ANIM("gPlayerAnim_link_bottle_drink_demo_end", 20),
    [MM_ANIM_LINK_BOTTLE_READ] = LINK_ANIM("gPlayerAnim_link_bottle_read", 30),
    [MM_ANIM_LINK_BOTTLE_READ_END] = LINK_ANIM("gPlayerAnim_link_bottle_read_end", 20),

    // Magic animations
    [MM_ANIM_LINK_MAGIC_TAME] = LINK_ANIM("gPlayerAnim_link_magic_tame", 30),
    [MM_ANIM_LINK_MAGIC_HONOO1] = LINK_ANIM("gPlayerAnim_link_magic_honoo1", 20),
    [MM_ANIM_LINK_MAGIC_HONOO2] = LINK_ANIM("gPlayerAnim_link_magic_honoo2", 20),
    [MM_ANIM_LINK_MAGIC_HONOO3] = LINK_ANIM("gPlayerAnim_link_magic_honoo3", 20),
    [MM_ANIM_LINK_MAGIC_KAZE1] = LINK_ANIM("gPlayerAnim_link_magic_kaze1", 20),
    [MM_ANIM_LINK_MAGIC_KAZE2] = LINK_ANIM("gPlayerAnim_link_magic_kaze2", 20),
    [MM_ANIM_LINK_MAGIC_KAZE3] = LINK_ANIM("gPlayerAnim_link_magic_kaze3", 20),
    [MM_ANIM_LINK_MAGIC_TAMASHII1] = LINK_ANIM("gPlayerAnim_link_magic_tamashii1", 20),
    [MM_ANIM_LINK_MAGIC_TAMASHII2] = LINK_ANIM("gPlayerAnim_link_magic_tamashii2", 20),
    [MM_ANIM_LINK_MAGIC_TAMASHII3] = LINK_ANIM("gPlayerAnim_link_magic_tamashii3", 20),

    // Hammer animations
    [MM_ANIM_LINK_HAMMER_NORMAL2LONG] = LINK_ANIM("gPlayerAnim_link_hammer_normal2long", 10),
    [MM_ANIM_LINK_HAMMER_LONG2LONG] = LINK_ANIM("gPlayerAnim_link_hammer_long2long", 10),
    [MM_ANIM_LINK_HAMMER_LONG2FREE] = LINK_ANIM("gPlayerAnim_link_hammer_long2free", 10),

    // Boom (Boomerang) animations
    [MM_ANIM_LINK_BOOM_THROW_WAITL] = LINK_ANIM("gPlayerAnim_link_boom_throw_waitL", 4),
    [MM_ANIM_LINK_BOOM_THROW_WAITR] = LINK_ANIM("gPlayerAnim_link_boom_throw_waitR", 4),

    // Silver (Heavy) lift animations
    [MM_ANIM_LINK_SILVER_WAIT] = LINK_ANIM("gPlayerAnim_link_silver_wait", 30),
    [MM_ANIM_LINK_SILVER_CARRY] = LINK_ANIM("gPlayerAnim_link_silver_carry", 8),
    [MM_ANIM_LINK_SILVER_THROW] = LINK_ANIM("gPlayerAnim_link_silver_throw", 16),

    // Swim animations
    [MM_ANIM_LINK_SWIMER_SWIM] = LINK_ANIM("gPlayerAnim_link_swimer_swim", 24),
    [MM_ANIM_LINK_SWIMER_SWIM_WAIT] = LINK_ANIM("gPlayerAnim_link_swimer_swim_wait", 4),
    [MM_ANIM_LINK_SWIMER_SWIM_GET] = LINK_ANIM("gPlayerAnim_link_swimer_swim_get", 20),
    [MM_ANIM_LINK_SWIMER_SWIM_HIT] = LINK_ANIM("gPlayerAnim_link_swimer_swim_hit", 10),
    [MM_ANIM_LINK_SWIMER_SWIM_DOWN] = LINK_ANIM("gPlayerAnim_link_swimer_swim_down", 20),
    [MM_ANIM_LINK_SWIMER_SWIM_15STEP_UP] = LINK_ANIM("gPlayerAnim_link_swimer_swim_15step_up", 15),
    [MM_ANIM_LINK_SWIMER_SWIM_DEEP_START] = LINK_ANIM("gPlayerAnim_link_swimer_swim_deep_start", 10),
    [MM_ANIM_LINK_SWIMER_SWIM_DEEP_END] = LINK_ANIM("gPlayerAnim_link_swimer_swim_deep_end", 10),
    [MM_ANIM_LINK_SWIMER_WAIT2SWIM_WAIT] = LINK_ANIM("gPlayerAnim_link_swimer_wait2swim_wait", 10),
    [MM_ANIM_LINK_SWIMER_LAND2SWIM_WAIT] = LINK_ANIM("gPlayerAnim_link_swimer_land2swim_wait", 10),
    [MM_ANIM_LINK_SWIMER_BACK_SWIM] = LINK_ANIM("gPlayerAnim_link_swimer_back_swim", 24),
    [MM_ANIM_LINK_SWIMER_LSIDE_SWIM] = LINK_ANIM("gPlayerAnim_link_swimer_Lside_swim", 24),
    [MM_ANIM_LINK_SWIMER_RSIDE_SWIM] = LINK_ANIM("gPlayerAnim_link_swimer_Rside_swim", 24),

    // Due to message length limits, remaining animations use placeholder frame count of 10
    // Full implementation would parse all frame counts from MM decomp XML

    // Demo/Cutscene - using 10 as placeholder
    [MM_ANIM_LINK_DEMO_TBOX_OPEN] = LINK_ANIM("gPlayerAnim_link_demo_Tbox_open", 30),
    [MM_ANIM_LINK_DEMO_GET_ITEMA] = LINK_ANIM("gPlayerAnim_link_demo_get_itemA", 40),
    [MM_ANIM_LINK_DEMO_GET_ITEMB] = LINK_ANIM("gPlayerAnim_link_demo_get_itemB", 40),
    [MM_ANIM_LINK_DEMO_DOORA_LINK] = LINK_ANIM("gPlayerAnim_link_demo_doorA_link", 30),
    [MM_ANIM_LINK_DEMO_DOORA_LINK_FREE] = LINK_ANIM("gPlayerAnim_link_demo_doorA_link_free", 30),
    [MM_ANIM_LINK_DEMO_DOORB_LINK] = LINK_ANIM("gPlayerAnim_link_demo_doorB_link", 30),
    [MM_ANIM_LINK_DEMO_DOORB_LINK_FREE] = LINK_ANIM("gPlayerAnim_link_demo_doorB_link_free", 30),
    [MM_ANIM_LINK_DEMO_WARP] = LINK_ANIM("gPlayerAnim_link_demo_warp", 30),
    [MM_ANIM_LINK_DEMO_BACK_TO_PAST] = LINK_ANIM("gPlayerAnim_link_demo_back_to_past", 30),
    [MM_ANIM_LINK_DEMO_RETURN_TO_PAST] = LINK_ANIM("gPlayerAnim_link_demo_return_to_past", 30),
    [MM_ANIM_LINK_DEMO_BIKKURI] = LINK_ANIM("gPlayerAnim_link_demo_bikkuri", 20),
    [MM_ANIM_LINK_DEMO_FURIMUKI] = LINK_ANIM("gPlayerAnim_link_demo_furimuki", 20),
    [MM_ANIM_LINK_DEMO_FURIMUKI2] = LINK_ANIM("gPlayerAnim_link_demo_furimuki2", 20),
    [MM_ANIM_LINK_DEMO_FURIMUKI2_WAIT] = LINK_ANIM("gPlayerAnim_link_demo_furimuki2_wait", 4),
    [MM_ANIM_LINK_DEMO_JIBUNMIRU] = LINK_ANIM("gPlayerAnim_link_demo_jibunmiru", 30),
    [MM_ANIM_LINK_DEMO_KAKEYORI] = LINK_ANIM("gPlayerAnim_link_demo_kakeyori", 20),
    [MM_ANIM_LINK_DEMO_KAKEYORI_WAIT] = LINK_ANIM("gPlayerAnim_link_demo_kakeyori_wait", 4),
    [MM_ANIM_LINK_DEMO_KAKEYORI_MIMAWASI] = LINK_ANIM("gPlayerAnim_link_demo_kakeyori_mimawasi", 30),
    [MM_ANIM_LINK_DEMO_KAKEYORI_MIOKURI] = LINK_ANIM("gPlayerAnim_link_demo_kakeyori_miokuri", 30),
    [MM_ANIM_LINK_DEMO_KAKEYORI_MIOKURI_WAIT] = LINK_ANIM("gPlayerAnim_link_demo_kakeyori_miokuri_wait", 4),
    [MM_ANIM_LINK_DEMO_KAOAGE] = LINK_ANIM("gPlayerAnim_link_demo_kaoage", 20),
    [MM_ANIM_LINK_DEMO_KAOAGE_WAIT] = LINK_ANIM("gPlayerAnim_link_demo_kaoage_wait", 4),
    [MM_ANIM_LINK_DEMO_KENMIRU1] = LINK_ANIM("gPlayerAnim_link_demo_kenmiru1", 30),
    [MM_ANIM_LINK_DEMO_KENMIRU1_WAIT] = LINK_ANIM("gPlayerAnim_link_demo_kenmiru1_wait", 4),
    [MM_ANIM_LINK_DEMO_KENMIRU2] = LINK_ANIM("gPlayerAnim_link_demo_kenmiru2", 30),
    [MM_ANIM_LINK_DEMO_KENMIRU2_WAIT] = LINK_ANIM("gPlayerAnim_link_demo_kenmiru2_wait", 4),
    [MM_ANIM_LINK_DEMO_KENMIRU2_MODORI] = LINK_ANIM("gPlayerAnim_link_demo_kenmiru2_modori", 20),
    [MM_ANIM_LINK_DEMO_KOUSAN] = LINK_ANIM("gPlayerAnim_link_demo_kousan", 30),
    [MM_ANIM_LINK_DEMO_LOOK_HAND] = LINK_ANIM("gPlayerAnim_link_demo_look_hand", 30),
    [MM_ANIM_LINK_DEMO_LOOK_HAND_WAIT] = LINK_ANIM("gPlayerAnim_link_demo_look_hand_wait", 4),
    [MM_ANIM_LINK_DEMO_NOZOKIKOMI] = LINK_ANIM("gPlayerAnim_link_demo_nozokikomi", 20),
    [MM_ANIM_LINK_DEMO_NOZOKIKOMI_WAIT] = LINK_ANIM("gPlayerAnim_link_demo_nozokikomi_wait", 4),
    [MM_ANIM_LINK_DEMO_SITA_WAIT] = LINK_ANIM("gPlayerAnim_link_demo_sita_wait", 4),
    [MM_ANIM_LINK_DEMO_UE] = LINK_ANIM("gPlayerAnim_link_demo_ue", 20),
    [MM_ANIM_LINK_DEMO_UE_WAIT] = LINK_ANIM("gPlayerAnim_link_demo_ue_wait", 4),
    [MM_ANIM_LINK_DEMO_ZELDAMIRU] = LINK_ANIM("gPlayerAnim_link_demo_zeldamiru", 30),
    [MM_ANIM_LINK_DEMO_ZELDAMIRU_WAIT] = LINK_ANIM("gPlayerAnim_link_demo_zeldamiru_wait", 4),
    [MM_ANIM_LINK_DEMO_GURAD] = LINK_ANIM("gPlayerAnim_link_demo_gurad", 10),
    [MM_ANIM_LINK_DEMO_GURAD_WAIT] = LINK_ANIM("gPlayerAnim_link_demo_gurad_wait", 4),
    [MM_ANIM_LINK_DEMO_GOMA_FURIMUKI] = LINK_ANIM("gPlayerAnim_link_demo_goma_furimuki", 20),
    [MM_ANIM_LINK_DEMO_BARU_OP1] = LINK_ANIM("gPlayerAnim_link_demo_baru_op1", 30),
    [MM_ANIM_LINK_DEMO_BARU_OP2] = LINK_ANIM("gPlayerAnim_link_demo_baru_op2", 30),
    [MM_ANIM_LINK_DEMO_BARU_OP3] = LINK_ANIM("gPlayerAnim_link_demo_baru_op3", 30),

    // ========================================
    // Goron Animations (pg_)
    // Frame counts are hints - actual count computed from file size in loader
    // ========================================

    // Goron idle (form-specific)
    [MM_ANIM_PG_WAIT] = GORON_ANIM("gPlayerAnim_pg_wait", 79),

    // Goron punch combo (from z_player.c line 3569-3574)
    [MM_ANIM_PG_PUNCHA] = GORON_ANIM("gPlayerAnim_pg_punchA", 12),
    [MM_ANIM_PG_PUNCHB] = GORON_ANIM("gPlayerAnim_pg_punchB", 19),
    [MM_ANIM_PG_PUNCHC] = GORON_ANIM("gPlayerAnim_pg_punchC", 20),

    // Goron punch end (recovery from standing)
    [MM_ANIM_PG_PUNCHAEND] = GORON_ANIM("gPlayerAnim_pg_punchAend", 13),
    [MM_ANIM_PG_PUNCHBEND] = GORON_ANIM("gPlayerAnim_pg_punchBend", 10),
    [MM_ANIM_PG_PUNCHCEND] = GORON_ANIM("gPlayerAnim_pg_punchCend", 10),

    // Goron punch end running (recovery while moving)
    [MM_ANIM_PG_PUNCHAENDR] = GORON_ANIM("gPlayerAnim_pg_punchAendR", 13),
    [MM_ANIM_PG_PUNCHBENDR] = GORON_ANIM("gPlayerAnim_pg_punchBendR", 10),
    [MM_ANIM_PG_PUNCHCENDR] = GORON_ANIM("gPlayerAnim_pg_punchCendR", 10),

    // Goron curl -> ball (from z_player.c line 8469, 19834)
    [MM_ANIM_PG_MARU_CHANGE] = GORON_ANIM("gPlayerAnim_pg_maru_change", 11),

    // Goron mask removal
    [MM_ANIM_PG_MASKOFFSTART] = GORON_ANIM("gPlayerAnim_pg_maskoffstart", 15),

    // Goron climb animations
    [MM_ANIM_PG_CLIMB_STARTA] = GORON_ANIM("gPlayerAnim_pg_climb_startA", 20),
    [MM_ANIM_PG_CLIMB_STARTB] = GORON_ANIM("gPlayerAnim_pg_climb_startB", 20),
    [MM_ANIM_PG_CLIMB_ENDAL] = GORON_ANIM("gPlayerAnim_pg_climb_endAL", 20),
    [MM_ANIM_PG_CLIMB_ENDAR] = GORON_ANIM("gPlayerAnim_pg_climb_endAR", 20),
    [MM_ANIM_PG_CLIMB_ENDBL] = GORON_ANIM("gPlayerAnim_pg_climb_endBL", 20),
    [MM_ANIM_PG_CLIMB_ENDBR] = GORON_ANIM("gPlayerAnim_pg_climb_endBR", 20),
    [MM_ANIM_PG_CLIMB_UPL] = GORON_ANIM("gPlayerAnim_pg_climb_upL", 10),
    [MM_ANIM_PG_CLIMB_UPR] = GORON_ANIM("gPlayerAnim_pg_climb_upR", 10),

    // Goron door animations
    [MM_ANIM_PG_DOORA_OPEN] = GORON_ANIM("gPlayerAnim_pg_doorA_open", 30),
    [MM_ANIM_PG_DOORB_OPEN] = GORON_ANIM("gPlayerAnim_pg_doorB_open", 30),

    // Goron treasure box
    [MM_ANIM_PG_TBOX_OPEN] = GORON_ANIM("gPlayerAnim_pg_Tbox_open", 30),

    // Goron instrument (drums)
    [MM_ANIM_PG_GAKKISTART] = GORON_ANIM("gPlayerAnim_pg_gakkistart", 20),
    [MM_ANIM_PG_GAKKIWAIT] = GORON_ANIM("gPlayerAnim_pg_gakkiwait", 4),
    [MM_ANIM_PG_GAKKIPLAY] = GORON_ANIM("gPlayerAnim_pg_gakkiplay", 4),
    [MM_ANIM_PG_GAKKIPLAYA] = GORON_ANIM("gPlayerAnim_pg_gakkiplayA", 10),
    [MM_ANIM_PG_GAKKIPLAYD] = GORON_ANIM("gPlayerAnim_pg_gakkiplayD", 10),
    [MM_ANIM_PG_GAKKIPLAYL] = GORON_ANIM("gPlayerAnim_pg_gakkiplayL", 10),
    [MM_ANIM_PG_GAKKIPLAYR] = GORON_ANIM("gPlayerAnim_pg_gakkiplayR", 10),
    [MM_ANIM_PG_GAKKIPLAYU] = GORON_ANIM("gPlayerAnim_pg_gakkiplayU", 10),

    // ========================================
    // Zora Animations (pz_)
    // Frame counts from MM decomp gameplay_keep.c
    // ========================================

    // Zora idle
    [MM_ANIM_PZ_WAIT] = ZORA_ANIM("gPlayerAnim_pz_wait", 59),

    // Zora punch combo (from z_player.c sMeleeAttackAnimInfo)
    [MM_ANIM_PZ_ATTACKA] = ZORA_ANIM("gPlayerAnim_pz_attackA", 7),
    [MM_ANIM_PZ_ATTACKB] = ZORA_ANIM("gPlayerAnim_pz_attackB", 10),
    [MM_ANIM_PZ_ATTACKC] = ZORA_ANIM("gPlayerAnim_pz_attackC", 20),

    // Zora punch end (recovery from standing)
    [MM_ANIM_PZ_ATTACKAEND] = ZORA_ANIM("gPlayerAnim_pz_attackAend", 9),
    [MM_ANIM_PZ_ATTACKBEND] = ZORA_ANIM("gPlayerAnim_pz_attackBend", 12),
    [MM_ANIM_PZ_ATTACKCEND] = ZORA_ANIM("gPlayerAnim_pz_attackCend", 10),

    // Zora punch end running (recovery while moving)
    [MM_ANIM_PZ_ATTACKAENDR] = ZORA_ANIM("gPlayerAnim_pz_attackAendR", 6),
    [MM_ANIM_PZ_ATTACKBENDR] = ZORA_ANIM("gPlayerAnim_pz_attackBendR", 6),
    [MM_ANIM_PZ_ATTACKCENDR] = ZORA_ANIM("gPlayerAnim_pz_attackCendR", 3),

    // Zora weapon equip
    [MM_ANIM_PZ_BLADEON] = ZORA_ANIM("gPlayerAnim_pz_bladeon", 9),

    // Zora boomerang (cutter)
    [MM_ANIM_PZ_CUTTERATTACK] = ZORA_ANIM("gPlayerAnim_pz_cutterattack", 9),
    [MM_ANIM_PZ_CUTTERCATCH] = ZORA_ANIM("gPlayerAnim_pz_cuttercatch", 6),
    [MM_ANIM_PZ_CUTTERWAITA] = ZORA_ANIM("gPlayerAnim_pz_cutterwaitA", 9),
    [MM_ANIM_PZ_CUTTERWAITB] = ZORA_ANIM("gPlayerAnim_pz_cutterwaitB", 9),
    [MM_ANIM_PZ_CUTTERWAITC] = ZORA_ANIM("gPlayerAnim_pz_cutterwaitC", 9),
    [MM_ANIM_PZ_CUTTERWAITANIM] = ZORA_ANIM("gPlayerAnim_pz_cutterwaitanim", 29),

    // Zora jump attack
    [MM_ANIM_PZ_JUMPAT] = ZORA_ANIM("gPlayerAnim_pz_jumpAT", 13),
    [MM_ANIM_PZ_JUMPATEND] = ZORA_ANIM("gPlayerAnim_pz_jumpATend", 13),

    // Zora swimming
    [MM_ANIM_PZ_FISHSWIM] = ZORA_ANIM("gPlayerAnim_pz_fishswim", 10),
    [MM_ANIM_PZ_WATERROLL] = ZORA_ANIM("gPlayerAnim_pz_waterroll", 18),
    [MM_ANIM_PZ_SWIMTOWAIT] = ZORA_ANIM("gPlayerAnim_pz_swimtowait", 10),

    // Zora climb animations
    [MM_ANIM_PZ_CLIMB_STARTA] = ZORA_ANIM("gPlayerAnim_pz_climb_startA", 30),
    [MM_ANIM_PZ_CLIMB_STARTB] = ZORA_ANIM("gPlayerAnim_pz_climb_startB", 56),
    [MM_ANIM_PZ_CLIMB_ENDAL] = ZORA_ANIM("gPlayerAnim_pz_climb_endAL", 27),
    [MM_ANIM_PZ_CLIMB_ENDAR] = ZORA_ANIM("gPlayerAnim_pz_climb_endAR", 27),
    [MM_ANIM_PZ_CLIMB_ENDBL] = ZORA_ANIM("gPlayerAnim_pz_climb_endBL", 56),
    [MM_ANIM_PZ_CLIMB_ENDBR] = ZORA_ANIM("gPlayerAnim_pz_climb_endBR", 56),
    [MM_ANIM_PZ_CLIMB_UPL] = ZORA_ANIM("gPlayerAnim_pz_climb_upL", 21),
    [MM_ANIM_PZ_CLIMB_UPR] = ZORA_ANIM("gPlayerAnim_pz_climb_upR", 21),

    // Zora door animations
    [MM_ANIM_PZ_DOORA_OPEN] = ZORA_ANIM("gPlayerAnim_pz_doorA_open", 100),
    [MM_ANIM_PZ_DOORB_OPEN] = ZORA_ANIM("gPlayerAnim_pz_doorB_open", 100),

    // Zora treasure box
    [MM_ANIM_PZ_TBOX_OPEN] = ZORA_ANIM("gPlayerAnim_pz_Tbox_open", 133),

    // Zora mask removal
    [MM_ANIM_PZ_MASKOFFSTART] = ZORA_ANIM("gPlayerAnim_pz_maskoffstart", 15),

    // Zora instrument (guitar)
    [MM_ANIM_PZ_GAKKISTART] = ZORA_ANIM("gPlayerAnim_pz_gakkistart", 15),
    [MM_ANIM_PZ_GAKKIPLAY] = ZORA_ANIM("gPlayerAnim_pz_gakkiplay", 7),

    // ========================================
    // Deku (pn_) animations - 22 limbs (same as human Link)
    // Frame counts from MM decomp link_animetion.xml
    // ========================================

    // Deku combat
    [MM_ANIM_PN_ATTACK] = DEKU_ANIM("gPlayerAnim_pn_attack", 2),
    [MM_ANIM_PN_GURD] = DEKU_ANIM("gPlayerAnim_pn_gurd", 4),
    [MM_ANIM_PN_TAMAHAKI] = DEKU_ANIM("gPlayerAnim_pn_tamahaki", 8),
    [MM_ANIM_PN_TAMAHAKIDF] = DEKU_ANIM("gPlayerAnim_pn_tamahakidf", 2),

    // Deku get item
    [MM_ANIM_PN_GETA] = DEKU_ANIM("gPlayerAnim_pn_getA", 22),
    [MM_ANIM_PN_GETB] = DEKU_ANIM("gPlayerAnim_pn_getB", 22),

    // Deku flight/flower
    [MM_ANIM_PN_BATABATA] = DEKU_ANIM("gPlayerAnim_pn_batabata", 14),
    [MM_ANIM_PN_KAKKU] = DEKU_ANIM("gPlayerAnim_pn_kakku", 12),
    [MM_ANIM_PN_KAKKUFINISH] = DEKU_ANIM("gPlayerAnim_pn_kakkufinish", 15),
    [MM_ANIM_PN_RAKKAFINISH] = DEKU_ANIM("gPlayerAnim_pn_rakkafinish", 11),

    // Deku drink/potion
    [MM_ANIM_PN_DRINK] = DEKU_ANIM("gPlayerAnim_pn_drink", 15),
    [MM_ANIM_PN_DRINKSTART] = DEKU_ANIM("gPlayerAnim_pn_drinkstart", 29),
    [MM_ANIM_PN_DRINKEND] = DEKU_ANIM("gPlayerAnim_pn_drinkend", 20),

    // Deku doors/chests
    [MM_ANIM_PN_DOORA_OPEN] = DEKU_ANIM("gPlayerAnim_pn_doorA_open", 100),
    [MM_ANIM_PN_DOORB_OPEN] = DEKU_ANIM("gPlayerAnim_pn_doorB_open", 100),
    [MM_ANIM_PN_TBOX_OPEN] = DEKU_ANIM("gPlayerAnim_pn_Tbox_open", 133),

    // Deku mask removal
    [MM_ANIM_PN_MASKOFFSTART] = DEKU_ANIM("gPlayerAnim_pn_maskoffstart", 15),

    // Deku instrument (pipes)
    [MM_ANIM_PN_GAKKISTART] = DEKU_ANIM("gPlayerAnim_pn_gakkistart", 12),
    [MM_ANIM_PN_GAKKIPLAY] = DEKU_ANIM("gPlayerAnim_pn_gakkiplay", 8),

    // ========================================
    // Human Link (alink_) special animations
    // ========================================

    // Kamaro's Mask dance (from gameplay_keep, 145 frames)
    [MM_ANIM_ALINK_DANCE_LOOP] = LINK_ANIM("gPlayerAnim_alink_dance_loop", 145),

    // ========================================
    // Bremen Mask + Mask of Scents (child Link)
    // ========================================
    // Bremen Mask: child Link marching with ocarina raised. MM Player_Action_11
    // uses this anim during the march. Frame count from MM decomp.
    [MM_ANIM_CLINK_NORMAL_OKARINA_WALK]  = LINK_ANIM("gPlayerAnim_clink_normal_okarina_walk", 20),
    [MM_ANIM_CLINK_NORMAL_OKARINA_WALKB] = LINK_ANIM("gPlayerAnim_clink_normal_okarina_walkB", 20),

    // Mask of Scents: child Link sniff/idle "msbowait" anim. Replaces the
    // default idle anim while wearing the mask. ~80 frames per MM analysis.
    [MM_ANIM_CL_MSBOWAIT] = LINK_ANIM("gPlayerAnim_cl_msbowait", 80),

    // Remaining animations use placeholder definitions
    // Each entry that isn't explicitly defined will have NULL path and 0 frames
};
