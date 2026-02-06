/**
 * @file mm_anim_loader.h
 * @brief Simple API to load and use MM animations in OOT
 *
 * USAGE:
 *   LinkAnimationHeader* anim = MmAnim_Load(MM_ANIM_LINK_FIGHTER_BACKTURN_JUMP);
 *   if (anim != NULL) {
 *       Player_AnimPlayOnce(play, this, anim);
 *   }
 *
 * ASSET PRIORITY:
 *   1. User mod o2r files (if any loaded)
 *   2. mm.o2r (base MM assets)
 *
 * CRITICAL: OOT and MM use IDENTICAL raw format for Link animations!
 * - 67 s16 per frame (66 components + 1 appearanceInfo)
 * - Same layout (root pos, root rot, limb rotations)
 * - Only fix needed: baseTransl (X=-57, Z=0)
 * - Uses LinkAnimationHeader (NOT AnimationHeader!)
 */

#ifndef MM_ANIM_LOADER_H
#define MM_ANIM_LOADER_H

#include "z64.h"

// Animation definitions
#include "../mm_sources/mm_anims.h"

// ============================================================================
// Main API
// ============================================================================

/**
 * Load MM animation as LinkAnimationHeader for use with OOT Player
 *
 * @param animId  Animation ID from MmAnimId enum
 * @return LinkAnimationHeader* ready for Player_AnimPlayOnce, or NULL if not available
 */
LinkAnimationHeader* MmAnim_Load(MmAnimId animId);

/**
 * Load MM animation by path (for custom/dynamic loading)
 *
 * @param path       OTR path (e.g., "misc/link_animetion/gPlayerAnim_link_normal_wait_Data")
 * @param frameCount Number of frames
 * @param limbCount  Number of limbs (22 for Human Link)
 * @return LinkAnimationHeader* or NULL if not available
 */
LinkAnimationHeader* MmAnim_LoadByPath(const char* path, s16 frameCount, u8 limbCount);

/**
 * Check if MM animations are available (mm.o2r loaded)
 */
s32 MmAnim_IsAvailable(void);

/**
 * Flush the animation cache, freeing all loaded animations
 * Call this when changing scenes or when memory is needed
 */
void MmAnim_FlushCache(void);

/**
 * Get cache statistics
 *
 * @param outEntryCount  Output: number of cached animations
 * @param outTotalBytes  Output: total memory used by cache
 */
void MmAnim_GetCacheStats(s32* outEntryCount, s32* outTotalBytes);

// ============================================================================
// Constants
// ============================================================================

// Cache size limit
#define MM_ANIM_CACHE_SIZE 64

// Frame format constants
#define MM_ANIM_FRAME_S16_COUNT 67 // 66 components + 1 appearanceInfo

// baseTransl correction values (same for OOT and MM)
#define MM_ANIM_BASE_TRANSL_X (-57)
#define MM_ANIM_BASE_TRANSL_Y (3377)
#define MM_ANIM_BASE_TRANSL_Z (0)

#endif // MM_ANIM_LOADER_H
