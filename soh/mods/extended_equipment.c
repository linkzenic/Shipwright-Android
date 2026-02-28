/**
 * extended_equipment.c - Extended equipment system (cheat)
 *
 * Core system: page switching, equip/unequip, icon/name lookup, behavior dispatch.
 * Follows the same pattern as extended_inventory.c.
 *
 * When the cheat CVar is enabled, pressing L on the equipment page toggles
 * to a second page showing 12 new equipment pieces (3 per category).
 * Equipped state is persisted via CVars.
 */

#include "extended_equipment.h"
#include <string.h>
#include "z64.h"
#include "z64player.h"
#include "z64save.h"

extern SaveContext gSaveContext;
extern s32 CVarGetInteger(const char* name, s32 defaultValue);
extern void CVarSetInteger(const char* name, s32 value);
extern void Inventory_ChangeEquipment(s16 equipment, u16 value);

// Unity build includes
#include "equipment/ext_equip_icons.c"
#include "equipment/ext_equip_names.c"
#include "equipment/ext_equip_behavior.c"

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
ExtendedEquipmentState gExtEquipState;

#define EXT_EQUIP_PAGE_SWITCH_COOLDOWN 15

// ---------------------------------------------------------------------------
// Page management
// ---------------------------------------------------------------------------

void ExtEquip_Init(void) {
    memset(&gExtEquipState, 0, sizeof(gExtEquipState));

    // Load persisted equipped state from CVars
    gExtEquipState.currentExtSword  = (u8)CVarGetInteger(CVAR_EXT_EQUIP_SWORD, 0);
    gExtEquipState.currentExtShield = (u8)CVarGetInteger(CVAR_EXT_EQUIP_SHIELD, 0);
    gExtEquipState.currentExtTunic  = (u8)CVarGetInteger(CVAR_EXT_EQUIP_TUNIC, 0);
    gExtEquipState.currentExtBoots  = (u8)CVarGetInteger(CVAR_EXT_EQUIP_BOOTS, 0);

    // Clamp to valid range
    if (gExtEquipState.currentExtSword > 3) gExtEquipState.currentExtSword = 0;
    if (gExtEquipState.currentExtShield > 3) gExtEquipState.currentExtShield = 0;
    if (gExtEquipState.currentExtTunic > 3) gExtEquipState.currentExtTunic = 0;
    if (gExtEquipState.currentExtBoots > 3) gExtEquipState.currentExtBoots = 0;

    // Generate placeholder icons
    ExtEquip_GenerateIcons();
}

void ExtEquip_Update(void) {
    if (gExtEquipState.pageSwitchTimer > 0) {
        gExtEquipState.pageSwitchTimer--;
    }

    // If cheat was disabled, reset page and clear equipped state
    if (!ExtEquip_IsEnabled()) {
        gExtEquipState.equipPage = 0;
        // Don't clear equipped state here — it persists in CVars
        // and will be re-applied when cheat is re-enabled
    }
}

int ExtEquip_GetPage(void) {
    if (!ExtEquip_IsEnabled()) {
        return 0;
    }
    return gExtEquipState.equipPage;
}

void ExtEquip_SwitchPage(void) {
    if (!ExtEquip_IsEnabled()) return;

    gExtEquipState.equipPage = (gExtEquipState.equipPage == 0) ? 1 : 0;
    gExtEquipState.pageSwitchTimer = EXT_EQUIP_PAGE_SWITCH_COOLDOWN;
}

u8 ExtEquip_CanSwitch(void) {
    return gExtEquipState.pageSwitchTimer <= 0;
}

u8 ExtEquip_IsEnabled(void) {
    return CVarGetInteger(CVAR_EXT_EQUIP_ENABLED, 0) != 0;
}

// ---------------------------------------------------------------------------
// Equip / Unequip
// ---------------------------------------------------------------------------

static void ExtEquip_SetCurrentByType(s16 equipType, u8 index) {
    switch (equipType) {
        case EQUIP_TYPE_SWORD:  gExtEquipState.currentExtSword = index; break;
        case EQUIP_TYPE_SHIELD: gExtEquipState.currentExtShield = index; break;
        case EQUIP_TYPE_TUNIC:  gExtEquipState.currentExtTunic = index; break;
        case EQUIP_TYPE_BOOTS:  gExtEquipState.currentExtBoots = index; break;
    }
}

static const char* ExtEquip_GetCVarKey(s16 equipType) {
    switch (equipType) {
        case EQUIP_TYPE_SWORD:  return CVAR_EXT_EQUIP_SWORD;
        case EQUIP_TYPE_SHIELD: return CVAR_EXT_EQUIP_SHIELD;
        case EQUIP_TYPE_TUNIC:  return CVAR_EXT_EQUIP_TUNIC;
        case EQUIP_TYPE_BOOTS:  return CVAR_EXT_EQUIP_BOOTS;
        default: return NULL;
    }
}

void ExtEquip_Equip(s16 equipType, u8 index) {
    if (index == 0 || index > 3) return;

    // If already equipped, toggle off (unequip)
    u8 current = ExtEquip_GetCurrent(equipType);
    if (current == index) {
        ExtEquip_Unequip(equipType);
        return;
    }

    // Set extended equipment
    ExtEquip_SetCurrentByType(equipType, index);

    // Persist to CVar
    const char* cvarKey = ExtEquip_GetCVarKey(equipType);
    if (cvarKey) {
        CVarSetInteger(cvarKey, index);
    }

    // Clear vanilla equipment for this type
    // Swords/shields → set to NONE; tunics/boots → revert to Kokiri
    switch (equipType) {
        case EQUIP_TYPE_SWORD:
            Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_NONE);
            gSaveContext.equips.buttonItems[0] = ITEM_NONE;
            break;
        case EQUIP_TYPE_SHIELD:
            Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_NONE);
            break;
        case EQUIP_TYPE_TUNIC:
            Inventory_ChangeEquipment(EQUIP_TYPE_TUNIC, EQUIP_VALUE_TUNIC_KOKIRI);
            break;
        case EQUIP_TYPE_BOOTS:
            Inventory_ChangeEquipment(EQUIP_TYPE_BOOTS, EQUIP_VALUE_BOOTS_KOKIRI);
            break;
    }
}

void ExtEquip_Unequip(s16 equipType) {
    ExtEquip_SetCurrentByType(equipType, 0);

    const char* cvarKey = ExtEquip_GetCVarKey(equipType);
    if (cvarKey) {
        CVarSetInteger(cvarKey, 0);
    }
}

u8 ExtEquip_GetCurrent(s16 equipType) {
    switch (equipType) {
        case EQUIP_TYPE_SWORD:  return gExtEquipState.currentExtSword;
        case EQUIP_TYPE_SHIELD: return gExtEquipState.currentExtShield;
        case EQUIP_TYPE_TUNIC:  return gExtEquipState.currentExtTunic;
        case EQUIP_TYPE_BOOTS:  return gExtEquipState.currentExtBoots;
        default: return 0;
    }
}

// ---------------------------------------------------------------------------
// Icons / Names
// ---------------------------------------------------------------------------

void* ExtEquip_GetIcon(s16 equipType, u8 index) {
    if (equipType < 0 || equipType >= 4 || index < 1 || index > 3) {
        return NULL;
    }

    // Ensure icons are generated
    ExtEquip_GenerateIcons();

    return sExtEquipIconBufs[equipType][index - 1];
}

u16 ExtEquip_GetItemId(s16 equipType, u8 index) {
    // Map (type, index) to ITEM_EXT_xxx
    // type 0 (sword): 0xD0 + (index-1)
    // type 1 (shield): 0xD3 + (index-1)
    // type 2 (tunic): 0xD6 + (index-1)
    // type 3 (boots): 0xD9 + (index-1)
    if (index < 1 || index > 3 || equipType < 0 || equipType >= 4) {
        return 0;
    }
    return ITEM_EXT_SWORD_1 + (equipType * 3) + (index - 1);
}

void* ExtEquip_GetNameTex(u16 itemId, u8 language) {
    return ExtEquip_LookupNameTex(itemId, language);
}

// ---------------------------------------------------------------------------
// Behavior
// ---------------------------------------------------------------------------

void ExtEquip_UpdateBehavior(void* playerVoid, void* playVoid) {
    if (!ExtEquip_IsEnabled()) return;

    Player* player = (Player*)playerVoid;
    PlayState* play = (PlayState*)playVoid;

    ExtEquip_DispatchBehavior(player, play);
}
