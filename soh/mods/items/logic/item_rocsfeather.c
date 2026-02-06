/**
 * item_rocsfeather.c - Roc's Feather from Oracle games
 *
 * Controls:
 *   C Button: High jump (works on ground and in water)
 *
 * Features:
 *   - Single high jump with sparkle effects
 *   - Reduced jump velocity in water
 *
 * MM Animation Support:
 *   When CVar "gEnhancements.RocsItemsUseMmAnims" is enabled:
 *   - Ground jump: MM backflip (plays once after 2 frame delay)
 *
 * State tracking:
 *   rfMmAnimTimer > 0 means "in air due to Roc's item"
 *   rfMmAnimTimer < 0 means "pending animation" (waiting frames before playing)
 */

#include "z64.h"
#include "item_rocsfeather.h"
#include "../custom_items.h"
#include "../helpers/equip_helper.h"
#include "../helpers/fx_helper.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"

// MM Animation API
#include "mods/anim_translator/mm_anim_loader.h"

// Pending animation type (stored when waiting for delay)
static s32 sRfPendingAnimType = 0; // 0=none, 1=backflip

static s32 RocsFeather_MmAnimEnabled(void) {
    return CVarGetInteger(ROCS_MM_ANIM_CVAR, 0) && MmAnim_IsAvailable();
}

void Handle_RocsFeather(Player* p, PlayState* play) {
    ItemInputState in;
    ItemInput_Update(&in, ITEM_ROCS_FEATHER_SKIJER, p, play);

    if (!in.wasEquipped)
        return;
    if (ItemInput_IsBlockedEx(p, play, 1))
        return; // Skip water blocker - Roc's Feather works in water

    s32 isOnGround = (p->actor.bgCheckFlags & BGCHECKFLAG_GROUND);
    s32 inWater = (p->stateFlags1 & PLAYER_STATE1_IN_WATER);

    // Reset states when on ground
    if (isOnGround) {
        rfMmAnimTimer = 0;
        sRfPendingAnimType = 0;
    }

    // Handle pending animation (negative timer = waiting frames)
    if (rfMmAnimTimer < 0) {
        rfMmAnimTimer++; // Count towards 0
        if (rfMmAnimTimer == 0) {
            // Delay finished, play the animation now
            if (sRfPendingAnimType == 1) {
                LinkAnimationHeader* mmAnim = MmAnim_Load(MM_ANIM_LINK_FIGHTER_BACKTURN_JUMP);
                if (mmAnim != NULL) {
                    LinkAnimation_PlayOnce(play, &p->skelAnime, mmAnim);
                }
            }
            sRfPendingAnimType = 0;
            rfMmAnimTimer = 999; // Now in "air by Roc's" state
        }
    }

    if (!in.isPressed)
        return;

    // Can jump on ground OR in water (with reduced force)
    if (isOnGround || inWater) {
        f32 jumpVel = inWater ? ROCSFEATHER_WATER_JUMP_VELOCITY : ROCSFEATHER_JUMP_VELOCITY;
        p->actor.velocity.y = jumpVel;
        Player_PlaySfx(p, LINK_IS_ADULT ? ROCSFEATHER_SOUND_JUMP_ADULT : ROCSFEATHER_SOUND_JUMP_CHILD);
        FX_SpawnSparkles(p, play);

        // Schedule MM animation after 2 frame delay (let OOT finish its animation change)
        if (RocsFeather_MmAnimEnabled()) {
            rfMmAnimTimer = -2;     // Negative = pending, will count up to 0
            sRfPendingAnimType = 1; // Backflip
        }
    }
}
