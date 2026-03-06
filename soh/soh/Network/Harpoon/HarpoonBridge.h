#ifndef HARPOON_BRIDGE_H
#define HARPOON_BRIDGE_H

/**
 * HarpoonBridge.h - C-callable interface for custom item PVP damage
 *
 * Custom items (Fire Rod, Ice Rod, etc.) are written in C and cannot call
 * Harpoon C++ methods directly. This bridge provides C-callable functions
 * that custom item code can use to detect dummy players and send damage.
 */

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Custom damage types (match receiver-side switch in HandlePacket_CustomDamage)
typedef enum {
    HARPOON_CUSTOM_DMG_FIRE = 0,       // Fire Rod - burn
    HARPOON_CUSTOM_DMG_ICE = 1,        // Ice Rod - freeze
    HARPOON_CUSTOM_DMG_ELECTRIC = 2,   // Light Rod - shock
    HARPOON_CUSTOM_DMG_HEAVY = 3,      // Ball and Chain - strong knockback
    HARPOON_CUSTOM_DMG_BOMB = 4,       // Bomb Arrows - explosive
    HARPOON_CUSTOM_DMG_AOE_STUN = 5,   // Demise's Destruction - AoE stun
    HARPOON_CUSTOM_DMG_LAUNCH = 6,     // Deku Leaf - upward launch
    HARPOON_CUSTOM_DMG_NORMAL = 7,     // Spinner/Kokiri sword equivalent
    HARPOON_CUSTOM_DMG_BOOMERANG = 8,  // Beetle - boomerang stun
    HARPOON_CUSTOM_DMG_GORON_ROLL = 9,
    HARPOON_CUSTOM_DMG_GORON_PUNCH = 10,
    HARPOON_CUSTOM_DMG_ZORA_FINS = 11,
    HARPOON_CUSTOM_DMG_FD_BEAM = 12,
} HarpoonCustomDamageType;

// Custom effect types for special interactions
typedef enum {
    HARPOON_CUSTOM_EFFECT_PULL = 0,    // Whip - pull target toward attacker
    HARPOON_CUSTOM_EFFECT_SWAP = 1,    // Switch Hook - swap positions
    HARPOON_CUSTOM_EFFECT_PUPPET = 2,  // Dominion Rod - puppet control
} HarpoonCustomEffectType;

// Returns 1 if the actor is a Harpoon dummy player, 0 otherwise
s32 Harpoon_IsDummyPlayer(Actor* actor);

// Returns 1 if PVP is currently active (not LOBBY/COUNTDOWN/DISCONNECTED)
s32 Harpoon_IsPvpActive(void);

// Send custom damage to a dummy player's owner.
// hitActor: the dummy player actor that was hit
// damageType: HarpoonCustomDamageType enum value
// damage: amount in quarter-hearts (multiplied by 8 on receiver)
void Harpoon_SendCustomDamage(Actor* hitActor, s32 damageType, s32 damage);

// Send a special PVP effect (whip pull, switch hook swap, dominion rod puppet).
// hitActor: the dummy player actor
// effectType: HarpoonCustomEffectType enum value
// attackerPos: local player's position (for direction calculations on receiver)
// attackerYaw: local player's facing direction
void Harpoon_SendCustomEffect(Actor* hitActor, s32 effectType, Vec3f* attackerPos, s16 attackerYaw);

// Convenience: check if a collider hit a dummy player and send damage.
// Returns 1 if hit was consumed (caller should clear AT_HIT and skip normal logic).
// col: the AT collider that has AT_HIT set
// damageType: HarpoonCustomDamageType
// damage: quarter-hearts
s32 Harpoon_CheckAndSendDamage(ColliderCylinder* col, s32 damageType, s32 damage);

#ifdef __cplusplus
}
#endif

#endif // HARPOON_BRIDGE_H
