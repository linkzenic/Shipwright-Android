/**
 * @file anim_translator_inline_test.h
 * @brief Test wrapper for MM animations using the MmAnim_Load API
 */

#ifndef ANIM_TRANSLATOR_INLINE_TEST_H
#define ANIM_TRANSLATOR_INLINE_TEST_H

#include "z64animation.h"
#include "soh/ResourceManagerHelpers.h"
#include "mods/anim_translator/mm_anim_loader.h"
#include <libultraship/log/luslog.h>

// CVar to enable MM animation test
#define ANIM_TEST_CVAR "gEnhancements.SkijerNEITestMmAnims"

// Debug log
#define ANIMTEST_LOG(fmt, ...) lusprintf(__FILE__, __LINE__, LUSLOG_LEVEL_INFO, fmt, ##__VA_ARGS__)

/**
 * Check if MM animation test is enabled
 */
static inline s32 AnimTest_IsEnabled(void) {
    return CVarGetInteger(ANIM_TEST_CVAR, 0) && MmAnim_IsAvailable();
}

/**
 * Get MM backflip animation - FOR GROUND JUMP
 */
static inline LinkAnimationHeader* AnimTest_GetBackflip(void) {
    if (!AnimTest_IsEnabled())
        return NULL;
    return MmAnim_Load(MM_ANIM_LINK_FIGHTER_BACKTURN_JUMP);
}

/**
 * Get MM roll jump (somersault) - FOR DOUBLE JUMP (AIR)
 */
static inline LinkAnimationHeader* AnimTest_GetRollJump(void) {
    if (!AnimTest_IsEnabled())
        return NULL;
    return MmAnim_Load(MM_ANIM_LINK_NORMAL_NEWROLL_JUMP_20F);
}

/**
 * Get MM charge jump slash
 */
static inline LinkAnimationHeader* AnimTest_GetChargeJump(void) {
    if (!AnimTest_IsEnabled())
        return NULL;
    return MmAnim_Load(MM_ANIM_LINK_FIGHTER_LPOWER_JUMP_KIRU);
}

/**
 * Get MM front jump attack
 */
static inline LinkAnimationHeader* AnimTest_GetFrontJump(void) {
    if (!AnimTest_IsEnabled())
        return NULL;
    return MmAnim_Load(MM_ANIM_LINK_FIGHTER_FRONT_JUMP);
}

// Legacy names
#define AnimTranslatorTest_GetMmAnim AnimTest_GetBackflip

#endif // ANIM_TRANSLATOR_INLINE_TEST_H
