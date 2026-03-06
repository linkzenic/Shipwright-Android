/**
 * extended_equipment.h - Extended equipment system (cheat)
 *
 * Adds 12 new equipment pieces (3 swords, 3 shields, 3 tunics, 3 boots)
 * accessible via L button on the pause menu equipment page.
 * All extended equipment is "owned" when the cheat CVar is enabled.
 *
 * Page switching: Press L on equipment screen to toggle vanilla/extended.
 */
#ifndef EXTENDED_EQUIPMENT_H
#define EXTENDED_EQUIPMENT_H

#include <libultraship/libultra.h>
#include "z64item.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// CVar keys
// ---------------------------------------------------------------------------
#define CVAR_EXT_EQUIP_ENABLED "gCheats.ExtEquip.Enabled"
#define CVAR_EXT_EQUIP_SWORD "gCheats.ExtEquip.Sword"
#define CVAR_EXT_EQUIP_SHIELD "gCheats.ExtEquip.Shield"
#define CVAR_EXT_EQUIP_TUNIC "gCheats.ExtEquip.Tunic"
#define CVAR_EXT_EQUIP_BOOTS "gCheats.ExtEquip.Boots"

// ---------------------------------------------------------------------------
// Extended equipment item IDs (for icon/name lookup, NOT stored in inventory)
// ---------------------------------------------------------------------------
#define ITEM_EXT_SWORD_1 0xD0
#define ITEM_EXT_SWORD_2 0xD1
#define ITEM_EXT_SWORD_3 0xD2
#define ITEM_EXT_SHIELD_1 0xD3
#define ITEM_EXT_SHIELD_2 0xD4
#define ITEM_EXT_SHIELD_3 0xD5
#define ITEM_EXT_TUNIC_1 0xD6
#define ITEM_EXT_TUNIC_2 0xD7
#define ITEM_EXT_TUNIC_3 0xD8
#define ITEM_EXT_BOOTS_1 0xD9
#define ITEM_EXT_BOOTS_2 0xDA
#define ITEM_EXT_BOOTS_3 0xDB

// ---------------------------------------------------------------------------
// Extended equipment indices (1-based, 0 = none)
// ---------------------------------------------------------------------------
typedef enum { EXT_EQUIP_NONE = 0, EXT_EQUIP_1 = 1, EXT_EQUIP_2 = 2, EXT_EQUIP_3 = 3, EXT_EQUIP_MAX = 4 } ExtEquipIndex;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
typedef struct {
    int equipPage;       // 0 = vanilla, 1 = extended
    s16 pageSwitchTimer; // Cooldown (15 frames)
    u8 currentExtSword;  // 0=none, 1-3=ext sword
    u8 currentExtShield; // 0=none, 1-3=ext shield
    u8 currentExtTunic;  // 0=none, 1-3=ext tunic
    u8 currentExtBoots;  // 0=none, 1-3=ext boots
} ExtendedEquipmentState;

extern ExtendedEquipmentState gExtEquipState;

// ---------------------------------------------------------------------------
// Page management
// ---------------------------------------------------------------------------

/** Initialize state from CVars */
void ExtEquip_Init(void);

/** Update per frame (cooldown timer) */
void ExtEquip_Update(void);

/** @return Current equipment page (0=vanilla, 1=extended) */
int ExtEquip_GetPage(void);

/** Toggle between vanilla and extended page */
void ExtEquip_SwitchPage(void);

/** @return true if page switch cooldown elapsed */
u8 ExtEquip_CanSwitch(void);

/** @return true if the extra equipment cheat is enabled */
u8 ExtEquip_IsEnabled(void);

// ---------------------------------------------------------------------------
// Equip / Unequip
// ---------------------------------------------------------------------------

/**
 * Equip an extended equipment piece.
 * @param equipType EQUIP_TYPE_SWORD/SHIELD/TUNIC/BOOTS
 * @param index     1-3 (ext equipment index)
 */
void ExtEquip_Equip(s16 equipType, u8 index);

/**
 * Unequip extended equipment of a given type (set to 0).
 * Called when vanilla equipment is equipped.
 * @param equipType EQUIP_TYPE_SWORD/SHIELD/TUNIC/BOOTS
 */
void ExtEquip_Unequip(s16 equipType);

/**
 * @param equipType EQUIP_TYPE_SWORD/SHIELD/TUNIC/BOOTS
 * @return Current extended equipment index (0=none, 1-3=equipped)
 */
u8 ExtEquip_GetCurrent(s16 equipType);

// ---------------------------------------------------------------------------
// Icons / Names
// ---------------------------------------------------------------------------

/**
 * Get icon texture for an extended equipment item.
 * @param equipType EQUIP_TYPE_SWORD/SHIELD/TUNIC/BOOTS
 * @param index     1-3
 * @return Pointer to 32x32 RGBA32 texture data
 */
void* ExtEquip_GetIcon(s16 equipType, u8 index);

/**
 * Get the extended item ID for a given equipment type and index.
 * @param equipType EQUIP_TYPE_SWORD/SHIELD/TUNIC/BOOTS
 * @param index     1-3
 * @return ITEM_EXT_xxx constant
 */
u16 ExtEquip_GetItemId(s16 equipType, u8 index);

/**
 * Get name texture for an extended equipment item.
 * @param itemId ITEM_EXT_xxx constant
 * @param language Language index
 * @return Pointer to name texture, or NULL for placeholder
 */
void* ExtEquip_GetNameTex(u16 itemId, u8 language);

// ---------------------------------------------------------------------------
// Behavior (stubs for now)
// ---------------------------------------------------------------------------

/**
 * Called per frame from Player_Update when extended equipment is active.
 * Dispatches to individual behavior handlers.
 */
void ExtEquip_UpdateBehavior(void* player, void* play);

#ifdef __cplusplus
}
#endif

#endif // EXTENDED_EQUIPMENT_H
