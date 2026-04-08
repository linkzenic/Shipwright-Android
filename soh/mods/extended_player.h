/**
 * extended_player.h - Extended player item action system
 *
 * Maps custom item IDs (ITEM_xxx) to player actions (PLAYER_IA_xxx).
 * Provides lookup functions for item behavior, model groups, and initialization.
 *
 * Used by: z_player.c, kaleido_scope, item logic files
 */
#ifndef EXTENDED_PLAYER_H
#define EXTENDED_PLAYER_H
#include <stdint.h>
#include <stdbool.h>
#include "z64player.h"
#include "z64item.h"
#ifdef __cplusplus
extern "C" {
#endif

// Vanilla array sizes (these are the original array sizes before custom items)
#define VANILLA_SITEMACTIONS_SIZE 56 // Original sItemActions size (up to ITEM_CLAIM_CHECK)
#define VANILLA_PLAYER_IA_COUNT 67   // PLAYER_IA 0x00-0x42 (67 actions)

// Custom item range in ITEM_xxx enum
#define CUSTOM_ITEM_START ITEM_ROCS_FEATHER_SKIJER
#define CUSTOM_ITEM_END ITEM_MM_MASK_FIERCE_DEITY

// Custom PLAYER_IA range
#define CUSTOM_PLAYER_IA_START 0x43 // PLAYER_IA_ROCS_FEATHER_SKIJER
#define CUSTOM_PLAYER_IA_END 0x74   // PLAYER_IA_MM_MASK_FIERCE_DEITY

// MM Mask PLAYER_IA values (0x5D-0x74) — all no-op, transformation handled by item ID check
#define PLAYER_IA_MM_MASK_POSTMAN 0x5D
#define PLAYER_IA_MM_MASK_ALL_NIGHT 0x5E
#define PLAYER_IA_MM_MASK_BLAST 0x5F
#define PLAYER_IA_MM_MASK_STONE 0x60
#define PLAYER_IA_MM_MASK_GREAT_FAIRY 0x61
#define PLAYER_IA_MM_MASK_DEKU 0x62
#define PLAYER_IA_MM_MASK_KEATON 0x63
#define PLAYER_IA_MM_MASK_BREMEN 0x64
#define PLAYER_IA_MM_MASK_BUNNY 0x65
#define PLAYER_IA_MM_MASK_DON_GERO 0x66
#define PLAYER_IA_MM_MASK_SCENTS 0x67
#define PLAYER_IA_MM_MASK_GORON 0x68
#define PLAYER_IA_MM_MASK_ROMANI 0x69
#define PLAYER_IA_MM_MASK_CIRCUS_LEADER 0x6A
#define PLAYER_IA_MM_MASK_KAFEI 0x6B
#define PLAYER_IA_MM_MASK_COUPLE 0x6C
#define PLAYER_IA_MM_MASK_TRUTH 0x6D
#define PLAYER_IA_MM_MASK_ZORA 0x6E
#define PLAYER_IA_MM_MASK_KAMARO 0x6F
#define PLAYER_IA_MM_MASK_GIBDO 0x70
#define PLAYER_IA_MM_MASK_GARO 0x71
#define PLAYER_IA_MM_MASK_CAPTAIN 0x72
#define PLAYER_IA_MM_MASK_GIANT 0x73
#define PLAYER_IA_MM_MASK_FIERCE_DEITY 0x74

// ============================================================================
// FUNCTION POINTER TYPES
// ============================================================================
struct Player;
struct PlayState;
typedef int32_t (*ItemActionUpdateFunc)(struct Player* player, struct PlayState* play);
typedef void (*ItemActionInitFunc)(struct PlayState* play, struct Player* player);

// ============================================================================
// HELPER FUNCTIONS - Use these instead of directly accessing arrays
// ============================================================================

/**
 * Get the PLAYER_IA_xxx value for a given ITEM_xxx value.
 * Handles both vanilla and custom items using switch for custom items.
 */
int8_t ExtPlayer_GetItemAction(int32_t item);

/**
 * Get the model group for a given PLAYER_IA_xxx value.
 * Handles both vanilla and custom item actions.
 */
uint8_t ExtPlayer_GetActionModelGroup(int32_t itemAction);

/**
 * Get the update function for a given PLAYER_IA_xxx value.
 * Handles both vanilla and custom item actions.
 */
ItemActionUpdateFunc ExtPlayer_GetItemActionUpdateFunc(int32_t itemAction);

/**
 * Get the init function for a given PLAYER_IA_xxx value.
 * Handles both vanilla and custom item actions.
 */
ItemActionInitFunc ExtPlayer_GetItemActionInitFunc(int32_t itemAction);

/**
 * Check if an item ID is a custom item.
 */
static inline bool ExtPlayer_IsCustomItem(int32_t item) {
    return (item >= CUSTOM_ITEM_START && item <= CUSTOM_ITEM_END);
}

/**
 * Check if an item ID is an MM mask item.
 */
static inline bool ExtPlayer_IsMmMaskItem(int32_t item) {
    return (item >= ITEM_MM_MASK_POSTMAN && item <= ITEM_MM_MASK_FIERCE_DEITY);
}

/**
 * Check if a PLAYER_IA value is a custom action.
 */
static inline bool ExtPlayer_IsCustomItemAction(int32_t itemAction) {
    return (itemAction >= CUSTOM_PLAYER_IA_START && itemAction <= CUSTOM_PLAYER_IA_END);
}

#ifdef __cplusplus
}
#endif
#endif // EXTENDED_PLAYER_H
