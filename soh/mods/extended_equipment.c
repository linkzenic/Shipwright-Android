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
#include "transformation_masks/transformation_masks.h"
#include "transformation_masks/assets/mm_asset_loader.h"
#include "pak_loader/pak_loader.h"

extern MmPlayerTransformation MmForm_GetCurrentForm(void);
#include <string.h>
#include <math.h>
#include "z64.h"
#include "z64player.h"
#include "z64save.h"
#include "functions.h"
#include "variables.h"

extern SaveContext gSaveContext;
extern s32 CVarGetInteger(const char* name, s32 defaultValue);

// Somaria cane DL for Byrna draw
#include "items/objects/somaria_cane_DL/header.h"

// Unity build includes
#include "equipment/ext_equip_icons.c"
#include "equipment/ext_equip_names.c"
#include "equipment/ext_equip_behavior.c"

// Age requirements (mirror extended_inventory.h to avoid header cycle)
#ifndef AGE_REQ_NONE
#define AGE_REQ_NONE 9
#endif
#ifndef AGE_REQ_ADULT
#define AGE_REQ_ADULT LINK_AGE_ADULT
#endif
#ifndef AGE_REQ_CHILD
#define AGE_REQ_CHILD LINK_AGE_CHILD
#endif

// Per-piece age requirement: [equipType][index-1]
//   SWORD:  Byrna,            Four Sword,    Drillshaft
//   SHIELD: Divine Shield,    Gerudo Scim.,  Shield of Ikana
//   TUNIC:  Magic Cape,       Pending4,      Champion's Tunic
//   BOOTS:  Pegasus Anklet,   Pendant Mem.,  Water Dragon Scale
static const u8 sExtEquipAgeReqs[4][3] = {
    { AGE_REQ_NONE,  AGE_REQ_CHILD, AGE_REQ_ADULT },
    { AGE_REQ_NONE,  AGE_REQ_NONE,  AGE_REQ_CHILD },
    { AGE_REQ_NONE,  AGE_REQ_ADULT, AGE_REQ_ADULT },
    { AGE_REQ_NONE,  AGE_REQ_NONE,  AGE_REQ_ADULT },
};

u8 ExtEquip_GetAgeReq(s16 equipType, u8 index) {
    if (equipType < 0 || equipType >= 4 || index < 1 || index > 3)
        return AGE_REQ_NONE;
    return sExtEquipAgeReqs[equipType][index - 1];
}

u8 ExtEquip_CheckAgeReq(s16 equipType, u8 index) {
    if (CVarGetInteger("gCheats.TimelessEquipment", 0))
        return 1;
    u8 req = ExtEquip_GetAgeReq(equipType, index);
    return (req == AGE_REQ_NONE) || (req == gSaveContext.linkAge);
}

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
ExtendedEquipmentState gExtEquipState;
u8 gExtEquipSuppressIconOverride = 0;
f32 gChampionSlowFactor = 1.0f;

// Transform backup: stores equipped ext equipment indices before transformation
static u8 sTransformBackup[4] = { 0 }; // [EQUIP_TYPE_SWORD..BOOTS]
static u8 sTransformBackupValid = 0;

#define EXT_EQUIP_PAGE_SWITCH_COOLDOWN 15

// ---------------------------------------------------------------------------
// Page management
// ---------------------------------------------------------------------------

void ExtEquip_Init(void) {
    memset(&gExtEquipState, 0, sizeof(gExtEquipState));
    memset(&gExtEquipBehavior, 0, sizeof(gExtEquipBehavior));

    // Clear reserved bits 28-31 (may contain garbage from old saves where equipment was u16 + padding)
    gSaveContext.inventory.equipment &= 0x0FFFFFFF;

    // Load equipped state from save data (per-file, persisted only on game save)
    gExtEquipState.currentExtSword = gSaveContext.ship.extEquipSword;
    gExtEquipState.currentExtShield = gSaveContext.ship.extEquipShield;
    gExtEquipState.currentExtTunic = gSaveContext.ship.extEquipTunic;
    gExtEquipState.currentExtBoots = gSaveContext.ship.extEquipBoots;

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

    // When switching to vanilla page, restore original sword if Byrna was overriding it
    if (gExtEquipState.equipPage == 0 && gExtEquipBehavior.byrnaActive) {
        Byrna_Cleanup();
    }
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
            gSaveContext.ship.extEquipSword = index;
            break;
        case EQUIP_TYPE_SHIELD:
            gExtEquipState.currentExtShield = index;
            gSaveContext.ship.extEquipShield = index;
            break;
        case EQUIP_TYPE_TUNIC:
            gExtEquipState.currentExtTunic = index;
            gSaveContext.ship.extEquipTunic = index;
            break;
        case EQUIP_TYPE_BOOTS:
            gExtEquipState.currentExtBoots = index;
            gSaveContext.ship.extEquipBoots = index;
            break;
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

// Clear vanilla equipment base that was set for ext equipment.
// Called only from explicit toggle-off paths (not from vanilla equip path,
// which sets its own vanilla equipment before calling ExtEquip_Unequip).
static void ExtEquip_ClearVanillaEquip(s16 equipType) {
    switch (equipType) {
        case EQUIP_TYPE_SWORD:
            Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_NONE);
            gSaveContext.equips.buttonItems[0] = ITEM_NONE;
            break;
        case EQUIP_TYPE_SHIELD:
            Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_NONE);
            break;
        default:
            break;
    }
}

void ExtEquip_RemoveItem(s16 equipType, u8 index) {
    if (index == 0 || index > 3 || equipType < 0 || equipType > 3)
        return;
    gSaveContext.inventory.equipment &= ~ExtEquip_GetBit(equipType, index);
    // If currently equipped, unequip and clear vanilla base
    if (ExtEquip_GetCurrent(equipType) == index) {
        ExtEquip_Unequip(equipType);
        ExtEquip_ClearVanillaEquip(equipType);
    }
}

void ExtEquip_Equip(s16 equipType, u8 index) {
    if (index == 0 || index > 3)
        return;

    // Pikachu cannot use extended equipment
    if (TransformMasks_IsTransformedAny() && MmForm_GetCurrentForm() == MM_PLAYER_FORM_PIKACHU)
        return;

    // Must own the item to equip it
    if (!ExtEquip_HasItem(equipType, index))
        return;

    // Age restriction
    if (!ExtEquip_CheckAgeReq(equipType, index))
        return;

    // If already equipped, toggle off (unequip)
    u8 current = ExtEquip_GetCurrent(equipType);
    if (current == index) {
        ExtEquip_Unequip(equipType);
        ExtEquip_ClearVanillaEquip(equipType);
        return;
    }

    // Set extended equipment (also syncs to gSaveContext.ship)
    ExtEquip_SetCurrentByType(equipType, index);

    // Set vanilla equipment base for ext equipment
    // Ext swords use Kokiri Sword as base (model + IA), ext shields use Mirror Shield
    switch (equipType) {
        case EQUIP_TYPE_SWORD:
            // Ext swords don't change vanilla sword equipment or B button item.
            // The sword model/IA override is handled by the behavior/draw system.
            // This prevents giving BGS/Kokiri Sword if the player doesn't own them.
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
    // Restore sword state if Byrna was active
    if (equipType == EQUIP_TYPE_SWORD && gExtEquipBehavior.byrnaActive) {
        Byrna_Cleanup();
    }

    ExtEquip_SetCurrentByType(equipType, 0);
    // NOTE: vanilla equipment is NOT cleared here — callers that need it
    // (toggle-off, remove) call ExtEquip_ClearVanillaEquip separately.
    // The vanilla equip path (z_kaleido_equipment.c) calls ExtEquip_Unequip
    // after already setting vanilla equipment, so clearing here would undo it.
}

// ---------------------------------------------------------------------------
// Transform integration
// ---------------------------------------------------------------------------

void ExtEquip_UnequipForTransform(void) {
    if (!ExtEquip_IsEnabled())
        return;
    if (sTransformBackupValid)
        return; // Already backed up (form-to-form switch)

    sTransformBackup[EQUIP_TYPE_SWORD] = gExtEquipState.currentExtSword;
    sTransformBackup[EQUIP_TYPE_SHIELD] = gExtEquipState.currentExtShield;
    sTransformBackup[EQUIP_TYPE_TUNIC] = gExtEquipState.currentExtTunic;
    sTransformBackup[EQUIP_TYPE_BOOTS] = gExtEquipState.currentExtBoots;
    sTransformBackupValid = 1;

    for (s16 t = EQUIP_TYPE_SWORD; t <= EQUIP_TYPE_BOOTS; t++) {
        if (ExtEquip_GetCurrent(t) != 0) {
            ExtEquip_Unequip(t);
        }
    }
}

void ExtEquip_RestoreFromTransform(void) {
    if (!sTransformBackupValid)
        return;
    if (!ExtEquip_IsEnabled()) {
        sTransformBackupValid = 0;
        return;
    }

    for (s16 t = EQUIP_TYPE_SWORD; t <= EQUIP_TYPE_BOOTS; t++) {
        if (sTransformBackup[t] != 0 && ExtEquip_HasItem(t, sTransformBackup[t])) {
            ExtEquip_Equip(t, sTransformBackup[t]);
        }
    }
    sTransformBackupValid = 0;
}

void ExtEquip_ClearTransformBackup(void) {
    sTransformBackupValid = 0;
    memset(sTransformBackup, 0, sizeof(sTransformBackup));
}

void ExtEquip_ToggleFromCButton(u16 itemId) {
    if (itemId < ITEM_EXT_SWORD_1 || itemId > ITEM_EXT_BOOTS_3)
        return;

    // Pikachu cannot use extended equipment
    if (TransformMasks_IsTransformedAny() && MmForm_GetCurrentForm() == MM_PLAYER_FORM_PIKACHU)
        return;

    // Map itemId to equipType + index
    // ITEM_EXT_SWORD_1=0xE0, _2=0xE1, _3=0xE2
    // ITEM_EXT_SHIELD_1=0xE3, _2=0xE4, _3=0xE5
    // ITEM_EXT_TUNIC_1=0xE6, _2=0xE7, _3=0xE8
    // ITEM_EXT_BOOTS_1=0xE9, _2=0xEA, _3=0xEB
    u16 offset = itemId - ITEM_EXT_SWORD_1; // 0-11
    s16 equipType = offset / 3;             // 0=sword, 1=shield, 2=tunic, 3=boots
    u8 index = (offset % 3) + 1;            // 1-3

    // Age restriction (allow unequip even if age fails — player can always remove)
    u8 current = ExtEquip_GetCurrent(equipType);
    if (current != index && !ExtEquip_CheckAgeReq(equipType, index)) {
        Audio_PlaySoundGeneral(NA_SE_SY_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    // Toggle: if already equipped with this index, unequip; otherwise equip
    if (current == index) {
        ExtEquip_Unequip(equipType);
        ExtEquip_ClearVanillaEquip(equipType);
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
    // type 0 (sword): 0xE0 + (index-1)
    // type 1 (shield): 0xE3 + (index-1)
    // type 2 (tunic): 0xE6 + (index-1)
    // type 3 (boots): 0xE9 + (index-1)
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

    // Skip remote dummy players. HarpoonDummyPlayer_Draw delegates to
    // Player_Draw for skeleton/anim parity, which routes here. But the
    // extended-equipment draw dispatch reads GLOBAL gExtEquipState (the
    // LOCAL player's slot indices) — drawing Four Sword clones / Pegasus
    // wind cone / Magic Cape / IK Axe reticle / Water-Dragon barrier on
    // remote dummies would render the local player's effects on every
    // peer's body. Gate on "this player is the local player actor".
    if (gPlayState != NULL) {
        Player* localPlayer = GET_PLAYER(gPlayState);
        if (player != localPlayer) {
            return;
        }
    }

    ExtEquip_DrawDispatch(player, play);
}

void ExtEquip_DrawSwordDL(void* playVoid) {
    PlayState* play = (PlayState*)playVoid;

    if (gExtEquipState.currentExtSword == 1) {
        // Byrna: draw blue cane DL only when sword is held (not sheathed)
        Player* drawPlayer = GET_PLAYER(play);
        if (Player_GetMeleeWeaponHeld(drawPlayer) != 0) {
            OPEN_DISPS(play->state.gfxCtx);
            gSPDisplayList(POLY_OPA_DISP++, g_byrna_cane_dl);
            CLOSE_DISPS(play->state.gfxCtx);
        }
    } else if (gExtEquipState.currentExtSword == 3) {
        // IK Axe: draw Iron Knuckle Axe DL
        IKAxe_DrawAxe(play);
    }
}

u8 ExtEquip_ShouldHideSwordDL(void) {
    if (!ExtEquip_IsEnabled())
        return 0;

    // Cane of Byrna replaces the sword model with its own draw
    if (gExtEquipState.currentExtSword == 1)
        return 1;

    // IK Axe: only hide sword DL when hammer is active (drawn)
    // In free mode (putaway), don't hide — vanilla shows open hand naturally
    if (gExtEquipState.currentExtSword == 3) {
        return gExtEquipBehavior.ikAxeDrawing;
    }

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

// Shared draw for the cached MM Mirror Shield DLs (hand + back differ only in
// which cached DL is passed). Drawn on XLU to avoid corrupting the OPA pipeline
// (prevents black tint on the tunic).
static void DrawCachedShieldDL(void* playVoid, Gfx* dl) {
    PlayState* play = (PlayState*)playVoid;

    OPEN_DISPS(play->state.gfxCtx);

    Matrix_Push();
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, dl);
    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx);
}

void ExtEquip_DrawShieldDL(void* playVoid) {
    if (!ExtEquip_IsEnabled() || gExtEquipState.currentExtShield != 3)
        return;

    ExtEquip_LoadMmShieldDLs();
    if (sCachedMmShieldHandDL == NULL)
        return;

    DrawCachedShieldDL(playVoid, sCachedMmShieldHandDL);
}

// Draw MM Mirror Shield on Link's back (sheath position)
void ExtEquip_DrawShieldBackDL(void* playVoid) {
    if (!ExtEquip_IsEnabled() || gExtEquipState.currentExtShield != 3)
        return;

    ExtEquip_LoadMmShieldDLs();
    if (sCachedMmShieldBackDL == NULL)
        return;

    DrawCachedShieldDL(playVoid, sCachedMmShieldBackDL);
}

// Common prologue for the per-piece dispatch wrappers below: bail out unless
// the cheat is enabled AND the given slot is currently equipped with `index`.
// (ExtEquip_GetCurrent returns the same field these used to read directly.)
#define EXT_EQUIP_REQUIRE(type, index)                                      \
    if (!ExtEquip_IsEnabled() || ExtEquip_GetCurrent(type) != (index))      \
    return

void ExtEquip_DrawWaistScale(void* playVoid) {
    EXT_EQUIP_REQUIRE(EQUIP_TYPE_BOOTS, 3);

    PlayState* play = (PlayState*)playVoid;
    DScale_DrawWaistScale(play);
}

void ExtEquip_DrawAnklet(void* playVoid, s32 isRightFoot) {
    EXT_EQUIP_REQUIRE(EQUIP_TYPE_BOOTS, 1);

    PlayState* play = (PlayState*)playVoid;
    Pegasus_DrawAnklet(play, isRightFoot);
}

void ExtEquip_UpdateAnkletPhysics(void* playerVoid) {
    EXT_EQUIP_REQUIRE(EQUIP_TYPE_BOOTS, 1);

    Player* player = (Player*)playerVoid;
    Pegasus_UpdateWingPhysics(player);
}

void ExtEquip_CaptureCapeShoulderPos(s32 limbIndex) {
    EXT_EQUIP_REQUIRE(EQUIP_TYPE_TUNIC, 1);

    MagicCape_CaptureShoulderPos(limbIndex);
}

void ExtEquip_DrawBreastplate(void* playVoid) {
    EXT_EQUIP_REQUIRE(EQUIP_TYPE_TUNIC, 2);

    PlayState* play = (PlayState*)playVoid;
    Breastplate_Draw(play);
}

u8 ExtEquip_IkanaDeathSave(void* playVoid) {
    if (!Ikana_ShouldRevive())
        return 0;

    PlayState* play = (PlayState*)playVoid;
    Ikana_ConsumeDeathSave(play);
    return 1;
}
