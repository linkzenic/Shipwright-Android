/**
 * extended_inventory.h - Extended inventory system for custom items
 *
 * Manages a multi-page inventory system (up to 72 total slots).
 * Page 1: Vanilla OOT items (slots 0-23)
 * Page 2: Custom items (slots 24-47)
 * Page 3: MM Masks (slots 48-71) — requires mm.o2r and CVar enabled
 *
 * Page switching: Press L/A button in pause menu to cycle pages.
 */
#ifndef EXTENDED_INVENTORY_H
#define EXTENDED_INVENTORY_H
#include <stdint.h>
#include <stdbool.h>
#include "z64item.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int currentPage;         // 0 = vanilla, 1 = custom items, 2 = MM masks
    int16_t pageSwitchTimer; // Cooldown to prevent rapid switching
} ExtendedInventoryState;

/**
 * @return Pointer to the global extended inventory state
 */
ExtendedInventoryState* ExtInv_GetState(void);

/**
 * Reset inventory state to defaults (page 0, no cooldown)
 */
void ExtInv_Reset(void);

/**
 * Update inventory state each frame (handles page switch cooldown)
 */
void ExtInv_Update(void);

/**
 * Clamp currentPage if it exceeds max pages (e.g., MM masks CVar toggled off)
 */
void ExtInv_ClampPage(void);

/**
 * @return true if page switch cooldown has elapsed
 */
bool ExtInv_CanSwitchPage(void);

/**
 * Cycle to next page (0 → 1 → 2 → 0, or fewer if MM masks disabled)
 */
void ExtInv_SwitchPage(void);

/**
 * @return Current inventory page (0, 1, or 2)
 */
int ExtInv_GetCurrentPage(void);

/**
 * @return Maximum number of pages (2 or 3 depending on MM masks CVar)
 */
int ExtInv_GetMaxPages(void);

/**
 * @return true if MM masks inventory CVar is enabled
 */
bool ExtInv_IsMmMasksEnabled(void);

/**
 * @return true if "Only Transformation Masks" sub-option is enabled
 */
bool ExtInv_IsOnlyTransformation(void);

/**
 * Convert visual slot (0-23) to actual inventory slot based on current page
 * @param visualSlot - The slot position shown on screen (0-23)
 * @return Actual inventory slot index (0-71)
 */
int ExtInv_GetInventorySlot(int visualSlot);

/**
 * @param slot - Inventory slot to check
 * @return true if slot belongs to current page
 */
bool ExtInv_IsSlotOnCurrentPage(uint8_t slot);

/**
 * @param slot - Inventory slot
 * @return Page number (0, 1, or 2) where this slot belongs
 */
int ExtInv_GetPageForSlot(uint8_t slot);

/**
 * @param itemId - Item ID (ITEM_xxx constant)
 * @return Age requirement (AGE_REQ_ADULT, AGE_REQ_CHILD, or AGE_REQ_NONE)
 */
uint8_t ExtInv_GetItemAgeReq(uint16_t itemId);

/**
 * @param slot - Inventory slot
 * @return Age requirement for items in this slot
 */
uint8_t ExtInv_GetSlotAgeReq(uint8_t slot);

/**
 * @param row - Equipment row (0=swords, 1=shields, 2=tunics, 3=boots)
 * @param col - Equipment column within row
 * @return Age requirement, accounting for transformation restrictions
 */
uint8_t ExtInv_GetEquipAgeReq(uint8_t row, uint8_t col);

/**
 * @param itemId - Custom item ID
 * @param language - Language index for localization
 * @return Pointer to item name texture, or NULL if not found
 */
void* ExtInv_GetCustomItemNameTex(uint16_t itemId, uint8_t language);

/**
 * @param itemId - Item ID
 * @return Pointer to item icon texture
 */
void* ExtInv_GetItemIcon(uint16_t itemId);

/**
 * @param itemId - Item ID
 * @return Inventory slot for this item, or 0xFF if not found
 */
uint8_t ExtInv_GetItemSlot(uint16_t itemId);
extern const uint8_t gPage2Items[24];
#define AGE_REQ_ADULT LINK_AGE_ADULT
#define AGE_REQ_CHILD LINK_AGE_CHILD
#define AGE_REQ_NONE 9
extern const uint8_t gPage2ItemAgeReqs[24];
// Roc's Feather Skijer and Roc's Cape share slot 24 (progressive upgrade system)
#define SLOT_ROCS 24                // Shared slot for Roc's Feather/Cape progressive
#define SLOT_ROCS_FEATHER_SKIJER 24 // Alias for compatibility
#define SLOT_ROCS_CAPE 24           // Now same slot as Feather (upgrade replaces it)
#define SLOT_WHIP 25
#define SLOT_SPINNER 26
#define SLOT_BOMB_ARROWS 27
#define SLOT_FIRE_ROD 28
#define SLOT_DEMISE_DESTRUCTION 29
#define SLOT_DEKU_LEAF 30
#define SLOT_TIME_GATE 31
#define SLOT_BEETLE 32
#define SLOT_SWITCH_HOOK 33
#define SLOT_ICE_ROD 34
#define SLOT_ZONAI_PERMAFROST 35
#define SLOT_MOGMA_MITTS 36
#define SLOT_GUST_JAR 37
#define SLOT_BALL_AND_CHAIN 38
#define SLOT_DESIRE_SENSOR 39
#define SLOT_LIGHT_ROD 40
#define SLOT_HYLIAS_GRACE 41
#define SLOT_PENDING_2 42
#define SLOT_PENDING_1 43
#define SLOT_PENDING_3 44
#define SLOT_CANE_OF_SOMARIA 45
#define SLOT_SHOVEL 46
#define SLOT_DOMINION_ROD 47

// Page 3: MM Mask slots (48-71)
#define SLOT_MM_MASK_POSTMAN 48
#define SLOT_MM_MASK_ALL_NIGHT 49
#define SLOT_MM_MASK_BLAST 50
#define SLOT_MM_MASK_STONE 51
#define SLOT_MM_MASK_GREAT_FAIRY 52
#define SLOT_MM_MASK_DEKU 53
#define SLOT_MM_MASK_KEATON 54
#define SLOT_MM_MASK_BREMEN 55
#define SLOT_MM_MASK_BUNNY 56
#define SLOT_MM_MASK_DON_GERO 57
#define SLOT_MM_MASK_SCENTS 58
#define SLOT_MM_MASK_GORON 59
#define SLOT_MM_MASK_ROMANI 60
#define SLOT_MM_MASK_CIRCUS_LEADER 61
#define SLOT_MM_MASK_KAFEI 62
#define SLOT_MM_MASK_COUPLE 63
#define SLOT_MM_MASK_TRUTH 64
#define SLOT_MM_MASK_ZORA 65
#define SLOT_MM_MASK_KAMARO 66
#define SLOT_MM_MASK_GIBDO 67
#define SLOT_MM_MASK_GARO 68
#define SLOT_MM_MASK_CAPTAIN 69
#define SLOT_MM_MASK_GIANT 70
#define SLOT_MM_MASK_FIERCE_DEITY 71

extern const uint8_t gPage3MaskItems[24];
extern const uint8_t gPage3MaskAgeReqs[24];
static inline uint8_t ExtInv_GetPage2AgeReq(uint8_t slot) {
    if (slot >= 24 && slot < 48) {
        return gPage2ItemAgeReqs[slot - 24];
    }
    return 9;
}
static inline bool ExtInv_CheckAgeReqForSlot(uint8_t slot, bool isAdult) {
    uint8_t req = ExtInv_GetSlotAgeReq(slot);
    return (req == 9) || (req == 0 && isAdult) || (req == 1 && !isAdult);
}
static inline bool ExtInv_ShouldRenderGrayscale(uint8_t slot, bool isAdult) {
    return !ExtInv_CheckAgeReqForSlot(slot, isAdult);
}
static inline void ExtInv_InitializePage2Items(void) {
    extern SaveContext gSaveContext;
    for (int i = 0; i < 24; i++) {
        if (gSaveContext.inventory.items[24 + i] == ITEM_NONE) {
            gSaveContext.inventory.items[24 + i] = gPage2Items[i];
        }
    }
}
static inline void ExtInv_ClearPage2Items(void) {
    extern SaveContext gSaveContext;
    for (int i = 24; i < 48; i++) {
        gSaveContext.inventory.items[i] = ITEM_NONE;
    }
}
static inline void ExtInv_GiveItem(uint8_t slot, uint8_t itemId) {
    extern SaveContext gSaveContext;
    if (slot >= 24 && slot < 48) {
        gSaveContext.inventory.items[slot] = itemId;
    }
}
static inline void ExtInv_SetItemById(uint8_t itemId) {
    extern SaveContext gSaveContext;
    uint8_t slot = ExtInv_GetItemSlot(itemId);
    if (slot != 0xFF) {
        gSaveContext.inventory.items[slot] = itemId;
    }
}

// Page 3: MM Mask helpers
// "Only Transformation" mode: transformation masks go to rightmost column (positions 5,11,17,23)
// Deku=pos5(slot53), Goron=pos11(slot59), Zora=pos17(slot65), FierceDeity=pos23(slot71)
#define SLOT_MM_ONLY_DEKU 53
#define SLOT_MM_ONLY_GORON 59
#define SLOT_MM_ONLY_ZORA 65
#define SLOT_MM_ONLY_FIERCE 71

static inline void ExtInv_InitializePage3Masks(void) {
    // No-op: masks are given individually (save editor, randomizer, etc.)
    // This function exists for future initialization if needed
}
static inline void ExtInv_ClearPage3Masks(void) {
    extern SaveContext gSaveContext;
    for (int i = 48; i < 72; i++) {
        gSaveContext.inventory.items[i] = ITEM_NONE;
    }
}
static inline void ExtInv_GiveMask(uint8_t slot, uint8_t itemId) {
    extern SaveContext gSaveContext;
    if (slot >= 48 && slot < 72) {
        gSaveContext.inventory.items[slot] = itemId;
    }
}

static inline void ExtInv_VerifyConsistency(void) {
    const int expectedSize = 24;
    const int actualSize = sizeof(gPage2Items) / sizeof(gPage2Items[0]);
    if (actualSize != expectedSize) {}
    const int ageReqSize = sizeof(gPage2ItemAgeReqs) / sizeof(gPage2ItemAgeReqs[0]);
    if (ageReqSize != expectedSize) {}
}

// =============================================================================
// Transformation Mask Item Restriction
//
// When a transformation mask is active, items not in the form's allow list
// are restricted (grayed out in KaleidoScope, can't be equipped/used).
// This integrates with CHECK_AGE_REQ_ITEM/SLOT macros so all existing
// usage sites automatically get the restriction without per-site changes.
// =============================================================================

extern u8 TransformMasks_IsEnabled(void);
extern u8 TransformMasks_IsTransformedAny(void);
extern u8 TransformMasks_IsFDSkinMode(void);
extern u8 TransformMasks_IsItemAllowed(s32 item);

// Returns true if item is restricted by active transformation mask
static inline bool ExtInv_IsTransformRestricted(int itemId) {
    if (itemId == ITEM_NONE)
        return false; // Empty slot = no restriction
    if (!TransformMasks_IsEnabled() || !TransformMasks_IsTransformedAny())
        return false;
    return !TransformMasks_IsItemAllowed(itemId);
}

// Returns true if the item in a given slot is restricted by active transformation mask
static inline bool ExtInv_IsSlotTransformRestricted(uint8_t slot) {
    extern SaveContext gSaveContext;
    if (slot >= 72)
        return false; // Out of range
    uint8_t itemId = gSaveContext.inventory.items[slot];
    return ExtInv_IsTransformRestricted(itemId);
}

#ifdef __cplusplus
}
#endif
#endif
