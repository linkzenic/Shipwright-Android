/**
 * item_switchhook.h - Switch Hook from Oracle of Ages
 *
 * Controls (hold-to-aim like Bomb Arrows):
 *   Hold C Button:   First-person aiming mode
 *   Release C:       Fire hook projectile
 *   Z-targeting:     Third-person aiming at target
 *
 * Features:
 *   - Swaps positions with swappable actors (pots, crates, certain enemies)
 *   - Deals hookshot damage to non-swappable actors and bounces back
 *   - Uses targeted actor (Z-target) for instant swap when available
 *   - Longshot distance (26 frames)
 *   - Usable by both child and adult Link
 *   - Hook tip rotated 180 degrees (reversed appearance)
 *   - Blue reticle during aiming (like Gust Jar suck mode)
 */

#ifndef ITEM_SWITCHHOOK_H
#define ITEM_SWITCHHOOK_H

#include "z64.h"
#include "../custom_items.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

#define SWITCHHOOK_TIMER 26       // Longshot distance (frames)
#define SWITCHHOOK_SPEED 20.0f    // Projectile speed
#define SWITCHHOOK_SWAP_FRAMES 15 // Frames for swap animation
#define SWITCHHOOK_DAMAGE 1       // Damage dealt to non-swappable actors

// ============================================================================
// STATES
// ============================================================================

#define SWITCHHOOK_STATE_IDLE 0       // Waiting for input
#define SWITCHHOOK_STATE_AIMING 1     // First-person aiming (hold C button)
#define SWITCHHOOK_STATE_SHOOTING 2   // Projectile flying forward
#define SWITCHHOOK_STATE_HIT_SWAP 3   // Hit swappable actor, performing swap
#define SWITCHHOOK_STATE_HIT_DAMAGE 4 // Hit non-swappable actor, damage + bounce
#define SWITCHHOOK_STATE_RETRACT 5    // Retracting back to player

// ============================================================================
// STATE ALIASES (shortcuts to gCustomItemState fields)
// ============================================================================

#define shActive gCustomItemState.switchHookActive
#define shState gCustomItemState.switchHookState
#define shFirstPerson gCustomItemState.switchHookFirstPerson
#define shProjPos gCustomItemState.switchHookProjPos
#define shProjYaw gCustomItemState.switchHookProjYaw
#define shProjPitch gCustomItemState.switchHookProjPitch
#define shTimer gCustomItemState.switchHookTimer
#define shTarget gCustomItemState.switchHookTarget
#define shLinkStartPos gCustomItemState.switchHookLinkStartPos
#define shTargetStartPos gCustomItemState.switchHookTargetStartPos
#define shSwapTimer gCustomItemState.switchHookSwapTimer
#define shCollider gCustomItemState.switchHookCollider
#define shButtonMask gCustomItemState.switchHookButtonMask
#define shVortexTimer gCustomItemState.switchHookVortexTimer

// ============================================================================
// ACTOR FLAG (for custom actors like Somaria Cubes)
// ============================================================================

#ifndef ACTOR_FLAG_SWITCHHOOKABLE
#define ACTOR_FLAG_SWITCHHOOKABLE (1 << 28)
#endif

// ============================================================================
// SWAP TABLE - Actors that can be swapped with
// ============================================================================

static const s16 sSwitchHookSwapTable[] = {
    // Objects (no pots or small crates)
    ACTOR_OBJ_KIBAKO2,  // Large crates
    ACTOR_EN_KANBAN,    // Signs
    ACTOR_OBJ_SYOKUDAI, // Torches
    ACTOR_EN_BOX,       // Chests

    // Enemies
    ACTOR_EN_POH,      // Poes/Ghosts
    ACTOR_EN_RR,       // Like Likes
    ACTOR_EN_AM,       // Armos
    ACTOR_EN_TITE,     // Tektites
    ACTOR_EN_ZF,       // Lizalfos/Dinolfos
    ACTOR_EN_RD,       // ReDeads/Gibdos
    ACTOR_EN_FLOORMAS, // Floormasters
    ACTOR_EN_WALLMAS,  // Wallmasters
    ACTOR_EN_FZ,       // Freezards
    ACTOR_EN_ANUBICE,  // Anubis

    // Special
    ACTOR_EN_KAKASI,  // Scarecrow
    ACTOR_EN_KAKASI2, // Scarecrow 2
    ACTOR_EN_KAKASI3, // Scarecrow 3
    ACTOR_EN_NIW,     // Cuccos
};
#define SWITCHHOOK_SWAP_COUNT (sizeof(sSwitchHookSwapTable) / sizeof(sSwitchHookSwapTable[0]))

// ============================================================================
// COLLIDER INIT
// ============================================================================

static ColliderQuadInit sSwitchHookQuadInit = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_PLAYER,
        AC_NONE,
        OC1_NONE,
        OC2_TYPE_PLAYER,
        COLSHAPE_QUAD,
    },
    {
        ELEMTYPE_UNK2,
        { 0x00000080, 0x00, 0x01 }, // Hookshot damage flags
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_ON | TOUCH_NEAREST | TOUCH_SFX_NORMAL,
        BUMP_NONE,
        OCELEM_NONE,
    },
    { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * Check if an actor can be swapped with.
 * @param actor The actor to check
 * @return 1 if swappable, 0 otherwise
 */
static inline s32 SwitchHook_CanSwap(Actor* actor) {
    u32 i;

    if (actor == NULL)
        return 0;

    // Check custom flag first (for Somaria Cubes, etc.)
    if (actor->flags & ACTOR_FLAG_SWITCHHOOKABLE) {
        return 1;
    }

    // Check swap table
    for (i = 0; i < SWITCHHOOK_SWAP_COUNT; i++) {
        if (actor->id == sSwitchHookSwapTable[i]) {
            return 1;
        }
    }

    return 0;
}

#endif // ITEM_SWITCHHOOK_H
