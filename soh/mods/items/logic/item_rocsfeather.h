/**
 * Roc's Feather Item Header
 * High jump item - can use in water with reduced force
 *
 * Part of Progressive Roc's system:
 * - Base item: Roc's Feather (single jump)
 * - Upgrade: Roc's Cape (single jump + double jump)
 * - Both items share SLOT_ROCS (slot 24)
 * - Both usable by Adult and Child (AGE_REQ_NONE)
 */

#ifndef ITEM_ROCSFEATHER_H
#define ITEM_ROCSFEATHER_H

#include "z64.h"
#include "../custom_items.h"

// Jump velocities
#define ROCSFEATHER_JUMP_VELOCITY 11.0f
#define ROCSFEATHER_WATER_JUMP_VELOCITY 5.5f

// Sound (age-related)
#define ROCSFEATHER_SOUND_JUMP_ADULT NA_SE_VO_LI_AUTO_JUMP
#define ROCSFEATHER_SOUND_JUMP_CHILD NA_SE_VO_LI_AUTO_JUMP_KID

// CVar for MM animations (shared with Roc's Cape)
#define ROCS_MM_ANIM_CVAR "gEnhancements.RocsItemsUseMmAnims"

// State aliases (shared with Roc's Cape - progressive slot, never both active)
#define rfMmAnimTimer gCustomItemState.rocsMmAnimTimer

#endif // ITEM_ROCSFEATHER_H