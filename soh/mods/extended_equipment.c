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
#include "transformation_masks/assets/mm_asset_loader.h"
#include <string.h>
#include <math.h>
#include "z64.h"
#include "z64player.h"
#include "z64save.h"
#include "functions.h"
#include "variables.h"

extern SaveContext gSaveContext;
extern s32 CVarGetInteger(const char* name, s32 defaultValue);
extern void CVarSetInteger(const char* name, s32 value);

// Somaria cane DL for Byrna draw
#include "items/objects/somaria_cane_DL/header.h"

// Unity build includes
#include "equipment/ext_equip_icons.c"
#include "equipment/ext_equip_names.c"
#include "equipment/ext_equip_behavior.c"

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
ExtendedEquipmentState gExtEquipState;
u8 gExtEquipSuppressIconOverride = 0;

#define EXT_EQUIP_PAGE_SWITCH_COOLDOWN 15

// ---------------------------------------------------------------------------
// Page management
// ---------------------------------------------------------------------------

void ExtEquip_Init(void) {
    memset(&gExtEquipState, 0, sizeof(gExtEquipState));
    memset(&gExtEquipBehavior, 0, sizeof(gExtEquipBehavior));

    // Load persisted equipped state from CVars (ownership is in gSaveContext.inventory.equipment upper bits)
    gExtEquipState.currentExtSword = (u8)CVarGetInteger(CVAR_EXT_EQUIP_SWORD, 0);
    gExtEquipState.currentExtShield = (u8)CVarGetInteger(CVAR_EXT_EQUIP_SHIELD, 0);
    gExtEquipState.currentExtTunic = (u8)CVarGetInteger(CVAR_EXT_EQUIP_TUNIC, 0);
    gExtEquipState.currentExtBoots = (u8)CVarGetInteger(CVAR_EXT_EQUIP_BOOTS, 0);

    // Clamp to valid range
    if (gExtEquipState.currentExtSword > 3)
        gExtEquipState.currentExtSword = 0;
    if (gExtEquipState.currentExtShield > 3)
        gExtEquipState.currentExtShield = 0;
    if (gExtEquipState.currentExtTunic > 3)
        gExtEquipState.currentExtTunic = 0;
    if (gExtEquipState.currentExtBoots > 3)
        gExtEquipState.currentExtBoots = 0;

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
    if (!ExtEquip_IsEnabled())
        return;

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
        case EQUIP_TYPE_SWORD:
            gExtEquipState.currentExtSword = index;
            break;
        case EQUIP_TYPE_SHIELD:
            gExtEquipState.currentExtShield = index;
            break;
        case EQUIP_TYPE_TUNIC:
            gExtEquipState.currentExtTunic = index;
            break;
        case EQUIP_TYPE_BOOTS:
            gExtEquipState.currentExtBoots = index;
            break;
    }
}

static const char* ExtEquip_GetCVarKey(s16 equipType) {
    switch (equipType) {
        case EQUIP_TYPE_SWORD:
            return CVAR_EXT_EQUIP_SWORD;
        case EQUIP_TYPE_SHIELD:
            return CVAR_EXT_EQUIP_SHIELD;
        case EQUIP_TYPE_TUNIC:
            return CVAR_EXT_EQUIP_TUNIC;
        case EQUIP_TYPE_BOOTS:
            return CVAR_EXT_EQUIP_BOOTS;
        default:
            return NULL;
    }
}

// ---------------------------------------------------------------------------
// Ownership
// ---------------------------------------------------------------------------

static u32 ExtEquip_GetBit(s16 equipType, u8 index) {
    return 1 << (EXT_EQUIP_OWNED_SHIFT + equipType * 3 + (index - 1));
}

u8 ExtEquip_HasItem(s16 equipType, u8 index) {
    if (index == 0 || index > 3 || equipType < 0 || equipType > 3)
        return 0;
    return (gSaveContext.inventory.equipment & ExtEquip_GetBit(equipType, index)) != 0;
}

void ExtEquip_GiveItem(s16 equipType, u8 index) {
    if (index == 0 || index > 3 || equipType < 0 || equipType > 3)
        return;
    gSaveContext.inventory.equipment |= ExtEquip_GetBit(equipType, index);
}

void ExtEquip_RemoveItem(s16 equipType, u8 index) {
    if (index == 0 || index > 3 || equipType < 0 || equipType > 3)
        return;
    gSaveContext.inventory.equipment &= ~ExtEquip_GetBit(equipType, index);
    // If currently equipped, unequip
    if (ExtEquip_GetCurrent(equipType) == index) {
        ExtEquip_Unequip(equipType);
    }
}

void ExtEquip_Equip(s16 equipType, u8 index) {
    if (index == 0 || index > 3)
        return;

    // Must own the item to equip it
    if (!ExtEquip_HasItem(equipType, index))
        return;

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

    // Set vanilla equipment base for ext equipment
    // Ext swords use Kokiri Sword as base (model + IA), ext shields use Mirror Shield
    switch (equipType) {
        case EQUIP_TYPE_SWORD:
            // Byrna (slot 1) uses Biggoron Sword IA for long reach
            if (index == 1) {
                Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_BIGGORON);
                gSaveContext.equips.buttonItems[0] = ITEM_SWORD_BGS;
            } else {
                Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_KOKIRI);
                gSaveContext.equips.buttonItems[0] = ITEM_SWORD_KOKIRI;
            }
            break;
        case EQUIP_TYPE_SHIELD:
            // Shield of Ikana (slot 3) uses Mirror Shield model
            if (index == 3) {
                Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_MIRROR);
            } else {
                Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_HYLIAN);
            }
            break;
        case EQUIP_TYPE_TUNIC:
            Inventory_ChangeEquipment(EQUIP_TYPE_TUNIC, EQUIP_VALUE_TUNIC_KOKIRI);
            break;
        case EQUIP_TYPE_BOOTS:
            // Ext boots are accessories — don't change vanilla boots
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

void ExtEquip_ToggleFromCButton(u16 itemId) {
    if (itemId < ITEM_EXT_SWORD_1 || itemId > ITEM_EXT_BOOTS_3)
        return;

    // Map itemId to equipType + index
    // ITEM_EXT_SWORD_1=0xD0, _2=0xD1, _3=0xD2
    // ITEM_EXT_SHIELD_1=0xD3, _2=0xD4, _3=0xD5
    // ITEM_EXT_TUNIC_1=0xD6, _2=0xD7, _3=0xD8
    // ITEM_EXT_BOOTS_1=0xD9, _2=0xDA, _3=0xDB
    u16 offset = itemId - ITEM_EXT_SWORD_1; // 0-11
    s16 equipType = offset / 3;             // 0=sword, 1=shield, 2=tunic, 3=boots
    u8 index = (offset % 3) + 1;            // 1-3

    // Toggle: if already equipped with this index, unequip; otherwise equip
    u8 current = ExtEquip_GetCurrent(equipType);
    if (current == index) {
        ExtEquip_Unequip(equipType);
        Audio_PlaySoundGeneral(NA_SE_IT_SHIELD_REMOVE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    } else {
        ExtEquip_Equip(equipType, index);
        Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
}

u8 ExtEquip_GetCurrent(s16 equipType) {
    switch (equipType) {
        case EQUIP_TYPE_SWORD:
            return gExtEquipState.currentExtSword;
        case EQUIP_TYPE_SHIELD:
            return gExtEquipState.currentExtShield;
        case EQUIP_TYPE_TUNIC:
            return gExtEquipState.currentExtTunic;
        case EQUIP_TYPE_BOOTS:
            return gExtEquipState.currentExtBoots;
        default:
            return 0;
    }
}

// ---------------------------------------------------------------------------
// Icons / Names
// ---------------------------------------------------------------------------

// Icon lookup table: [type][index-1] = OTR path string
static const char* sExtEquipIconPaths[4][3] = {
    // Swords
    { dgItemIconCaneOfByrnaTex, dgItemIconFourSwordTex, dgItemIconDrillshaftTex },
    // Shields
    { dgItemIconDivineShieldTex, dgItemIconGerudoScimitarTex,
      "__OTR__icon_item_static_yar/gItemIconMirrorShieldTex" }, // Shield of Ikana (MM mirror shield)
    // Tunics
    { dgItemIconMagicCapeTex, dgItemIconPending4Tex, dgItemIconChampionsTunicTex },
    // Boots
    { dgItemIconPegasusAnkletTex,
      "__OTR__icon_item_static_yar/gItemIconPendantOfMemoriesTex", // mm.o2r
      dgItemIconWaterDragonScaleTex },
};

void* ExtEquip_GetIcon(s16 equipType, u8 index) {
    if (equipType < 0 || equipType >= 4 || index < 1 || index > 3) {
        return NULL;
    }

    return (void*)sExtEquipIconPaths[equipType][index - 1];
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

ExtEquipBehaviorState gExtEquipBehavior;

void ExtEquip_UpdateBehavior(void* playerVoid, void* playVoid) {
    if (!ExtEquip_IsEnabled())
        return;

    Player* player = (Player*)playerVoid;
    PlayState* play = (PlayState*)playVoid;

    ExtEquip_DispatchBehavior(player, play);
}

void ExtEquip_OnMeleeHit(void* playerVoid, void* playVoid) {
    if (!ExtEquip_IsEnabled())
        return;

    Player* player = (Player*)playerVoid;
    PlayState* play = (PlayState*)playVoid;

    ExtEquip_OnMeleeHitDispatch(player, play);
}

void ExtEquip_DrawBehavior(void* playerVoid, void* playVoid) {
    if (!ExtEquip_IsEnabled())
        return;

    Player* player = (Player*)playerVoid;
    PlayState* play = (PlayState*)playVoid;

    ExtEquip_DrawDispatch(player, play);
}

void ExtEquip_DrawSwordDL(void* playVoid) {
    PlayState* play = (PlayState*)playVoid;

    if (gExtEquipState.currentExtSword == 1) {
        // Byrna: draw blue cane DL (defined in equip_byrna.c)
        OPEN_DISPS(play->state.gfxCtx);
        gSPDisplayList(POLY_OPA_DISP++, g_byrna_cane_dl);
        CLOSE_DISPS(play->state.gfxCtx);
    }
}

u8 ExtEquip_ShouldHideSwordDL(void) {
    if (!ExtEquip_IsEnabled())
        return 0;

    // Cane of Byrna replaces the sword model with its own draw
    if (gExtEquipState.currentExtSword == 1)
        return 1;

    return 0;
}

const char* ExtEquip_GetShieldDLOverride(void) {
    if (!ExtEquip_IsEnabled())
        return NULL;

    // Shield of Ikana (slot 3): hide OOT mirror shield, draw custom in PostLimbDraw
    if (gExtEquipState.currentExtShield == 3)
        return "HIDE";

    return NULL;
}

// Cached MM Mirror Shield DLs (loaded once from mm.o2r with hash pre-resolution)
static Gfx* sCachedMmShieldHandDL = NULL;
static Gfx* sCachedMmShieldBackDL = NULL;
static u8 sMmShieldLoadAttempted = 0;

static void ExtEquip_LoadMmShieldDLs(void) {
    if (sMmShieldLoadAttempted)
        return;
    sMmShieldLoadAttempted = 1;

    sCachedMmShieldHandDL =
        (Gfx*)TransformMasks_LoadMmDL("objects/object_link_child/gLinkHumanRightHandHoldingMirrorShieldDL");
    // Use the plain shield DL (no embedded matrix) for back — we control the transform
    sCachedMmShieldBackDL = (Gfx*)TransformMasks_LoadMmDL("objects/object_link_child/gLinkHumanMirrorShieldDL");
}

void ExtEquip_DrawShieldDL(void* playVoid) {
    if (!ExtEquip_IsEnabled() || gExtEquipState.currentExtShield != 3)
        return;

    ExtEquip_LoadMmShieldDLs();
    if (sCachedMmShieldHandDL == NULL)
        return;

    PlayState* play = (PlayState*)playVoid;

    OPEN_DISPS(play->state.gfxCtx);

    // Draw on XLU to avoid corrupting OPA pipeline (prevents black tint on tunic)
    Matrix_Push();
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, sCachedMmShieldHandDL);
    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx);
}

// Draw MM Mirror Shield on Link's back (sheath position)
void ExtEquip_DrawShieldBackDL(void* playVoid) {
    if (!ExtEquip_IsEnabled() || gExtEquipState.currentExtShield != 3)
        return;

    ExtEquip_LoadMmShieldDLs();
    if (sCachedMmShieldBackDL == NULL)
        return;

    PlayState* play = (PlayState*)playVoid;

    OPEN_DISPS(play->state.gfxCtx);

    // Draw on XLU to avoid corrupting OPA pipeline
    Matrix_Push();
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, sCachedMmShieldBackDL);
    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx);
}

void ExtEquip_DrawWaistScale(void* playVoid) {
    if (!ExtEquip_IsEnabled())
        return;
    if (gExtEquipState.currentExtBoots != 3)
        return;

    PlayState* play = (PlayState*)playVoid;
    DScale_DrawWaistScale(play);
}

void ExtEquip_DrawAnklet(void* playVoid, s32 isRightFoot) {
    if (!ExtEquip_IsEnabled())
        return;
    if (gExtEquipState.currentExtBoots != 1)
        return;

    PlayState* play = (PlayState*)playVoid;
    Pegasus_DrawAnklet(play, isRightFoot);
}

void ExtEquip_UpdateAnkletPhysics(void* playerVoid) {
    if (!ExtEquip_IsEnabled())
        return;
    if (gExtEquipState.currentExtBoots != 1)
        return;

    Player* player = (Player*)playerVoid;
    Pegasus_UpdateWingPhysics(player);
}

void ExtEquip_CaptureCapeShoulderPos(s32 limbIndex) {
    if (!ExtEquip_IsEnabled())
        return;
    if (gExtEquipState.currentExtTunic != 1)
        return;

    MagicCape_CaptureShoulderPos(limbIndex);
}
