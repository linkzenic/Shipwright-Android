/**
 * item_rocscape.c - Roc's Cape from Four Swords Adventures
 *
 * MM Animation Support:
 *   When CVar "gEnhancements.RocsItemsUseMmAnims" is enabled:
 *   - Ground jump: MM backflip (plays once after 2 frame delay)
 *   - Double jump: MM roll jump (plays once)
 *
 * State tracking:
 *   rcMmAnimTimer > 0 means "in air due to Roc's item"
 *   rcMmAnimTimer < 0 means "pending animation" (waiting frames before playing)
 */

#include "z64.h"
#include "item_rocscape.h"
#include "../custom_items.h"
#include "../helpers/equip_helper.h"
#include "../helpers/fx_helper.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"

// MM Animation API
#include "mods/anim_translator/mm_anim_loader.h"

// Pending animation type (stored when waiting for delay)
static s32 sPendingAnimType = 0; // 0=none, 1=backflip, 2=roll jump

static s32 RocsCape_MmAnimEnabled(void) {
    return CVarGetInteger(ROCS_MM_ANIM_CVAR, 0) && MmAnim_IsAvailable();
}

void Handle_RocsCape(Player* p, PlayState* play) {
    ItemInputState in;
    ItemInput_Update(&in, ITEM_ROCS_CAPE, p, play);

    if (!in.wasEquipped)
        return;
    if (ItemInput_IsBlockedEx(p, play, 1))
        return;

    s32 isOnGround = (p->actor.bgCheckFlags & BGCHECKFLAG_GROUND);
    s32 inWater = (p->stateFlags1 & PLAYER_STATE1_IN_WATER);

    // Reset states when on ground
    if (isOnGround) {
        rcJumpCount = 0;
        rcMmAnimTimer = 0;
        sPendingAnimType = 0;
    }

    // Handle pending animation (negative timer = waiting frames)
    if (rcMmAnimTimer < 0) {
        rcMmAnimTimer++; // Count towards 0
        if (rcMmAnimTimer == 0) {
            // Delay finished, play the animation now
            LinkAnimationHeader* mmAnim = NULL;
            if (sPendingAnimType == 1) {
                mmAnim = MmAnim_Load(MM_ANIM_LINK_FIGHTER_BACKTURN_JUMP);
            } else if (sPendingAnimType == 2) {
                mmAnim = MmAnim_Load(MM_ANIM_LINK_NORMAL_NEWROLL_JUMP_20F);
            }
            if (mmAnim != NULL) {
                LinkAnimation_PlayOnce(play, &p->skelAnime, mmAnim);
            }
            sPendingAnimType = 0;
            rcMmAnimTimer = 999; // Now in "air by Roc's" state
        }
    }

    if (!in.isPressed)
        return;

    if (isOnGround || inWater) {
        // Ground/water jump (first jump)
        f32 jumpVel = inWater ? ROCSCAPE_WATER_JUMP_VELOCITY : ROCSCAPE_JUMP_VELOCITY;
        p->actor.velocity.y = jumpVel;
        Player_PlaySfx(p, LINK_IS_ADULT ? ROCSCAPE_SOUND_JUMP_ADULT : ROCSCAPE_SOUND_JUMP_CHILD);
        FX_SpawnSparkles(p, play);

        // Schedule MM animation after 2 frame delay (let OOT finish its animation change)
        if (RocsCape_MmAnimEnabled()) {
            rcMmAnimTimer = -2;   // Negative = pending, will count up to 0
            sPendingAnimType = 1; // Backflip
        }

    } else if (rcJumpCount == 0) {
        // Double jump (second jump, while in air)
        rcJumpCount = 1;
        p->actor.velocity.y = ROCSCAPE_DOUBLE_JUMP_VELOCITY;
        Player_PlaySfx(p, LINK_IS_ADULT ? ROCSCAPE_SOUND_DOUBLE_ADULT : ROCSCAPE_SOUND_DOUBLE_CHILD);
        FX_SpawnSparkles(p, play);

        // Spawn shockwave
        Vec3f shockwavePos;
        shockwavePos.x = p->actor.world.pos.x;
        shockwavePos.y = p->actor.floorHeight + 2.0f;
        shockwavePos.z = p->actor.world.pos.z;
        FX_SpawnShockwaveSmall(play, &shockwavePos, 60, 150);

        // Schedule MM animation after 2 frame delay
        if (RocsCape_MmAnimEnabled()) {
            rcMmAnimTimer = -2;
            sPendingAnimType = 2; // Roll jump
        }
    }
}
