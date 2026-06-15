/**
 * extended_inventory.c - Extended inventory system implementation
 *
 * Manages custom items in multiple inventory pages.
 * Page 1: Vanilla OOT items (slots 0-23)
 * Page 2: Custom items (slots 24-47)
 * Page 3: MM Masks (slots 48-71) — requires mm.o2r and CVar
 */

#include "extended_inventory.h"
#include "extended_equipment.h"
#include "z64.h"
#include <string.h>
#include "assets/soh_assets.h"
#include "transformation_masks/transformation_masks.h"
#include "transformation_masks/assets/mm_asset_loader.h"
extern void* gItemIcons[];
extern uint8_t gItemSlots[];
static ExtendedInventoryState sExtInvState = { .currentPage = 0, .pageSwitchTimer = 0 };

// Page 2 item layout (slots 24-47)
// Note: ITEM_ROCS_FEATHER_SKIJER at slot 24 is progressive - becomes ITEM_ROCS_CAPE when upgraded (shares slot)
// Slot 15 (actual slot 39) now has ITEM_DESIRE_SENSOR instead of ITEM_ROCS_CAPE
const uint8_t gPage2Items[24] = { ITEM_ROCS_FEATHER_SKIJER,
                                  ITEM_WHIP,
                                  ITEM_SPINNER,
                                  ITEM_BOMB_ARROWS,
                                  ITEM_ROD_FIRE,
                                  ITEM_DEMISE_DESTRUCTION,
                                  ITEM_DEKU_LEAF,
                                  ITEM_TIME_GATE,
                                  ITEM_BEETLE,
                                  ITEM_SWITCH_HOOK,
                                  ITEM_ROD_ICE,
                                  ITEM_ZONAI_PERMAFROST,
                                  ITEM_MOGMA_MITTS,
                                  ITEM_GUST_JAR,
                                  ITEM_BALL_AND_CHAIN,
                                  ITEM_DESIRE_SENSOR,
                                  ITEM_ROD_LIGHT,
                                  ITEM_HYLIAS_GRACE,
                                  ITEM_LANTERN,
                                  ITEM_MINISH_CAP,
                                  ITEM_POKEBALL,
                                  ITEM_CANE_OF_SOMARIA,
                                  ITEM_SHOVEL,
                                  ITEM_DOMINION_ROD };

// Age requirements for page 2 items
// Roc's items (slot 0/24) = AGE_REQ_NONE (both adult and child can use Feather AND Cape)
// Desire Sensor (slot 15/39) = AGE_REQ_NONE (both adult and child can use)
const uint8_t gPage2ItemAgeReqs[24] = { AGE_REQ_NONE, AGE_REQ_NONE,  AGE_REQ_NONE, AGE_REQ_ADULT, AGE_REQ_NONE,
                                        AGE_REQ_NONE, AGE_REQ_CHILD, AGE_REQ_NONE, AGE_REQ_ADULT, AGE_REQ_CHILD,
                                        AGE_REQ_NONE, AGE_REQ_NONE,  AGE_REQ_NONE, AGE_REQ_CHILD, AGE_REQ_ADULT,
                                        AGE_REQ_NONE, AGE_REQ_NONE,  AGE_REQ_NONE, AGE_REQ_NONE,  AGE_REQ_CHILD,
                                        AGE_REQ_NONE, AGE_REQ_NONE,  AGE_REQ_NONE, AGE_REQ_NONE };

// Page 3: MM Masks layout (slots 48-71)
// Row 0: Postman, AllNight, Blast, Stone, GreatFairy, Deku
// Row 1: Keaton, Bremen, Bunny, DonGero, Scents, Goron
// Row 2: Romani, CircusLeader, Kafei, Couple, Truth, Zora
// Row 3: Kamaro, Gibdo, Garo, Captain, Giant, FierceDeity
const uint8_t gPage3MaskItems[24] = {
    ITEM_MM_MASK_POSTMAN,     ITEM_MM_MASK_ALL_NIGHT,     ITEM_MM_MASK_BLAST,  ITEM_MM_MASK_STONE,
    ITEM_MM_MASK_GREAT_FAIRY, ITEM_MM_MASK_DEKU,          ITEM_MM_MASK_KEATON, ITEM_MM_MASK_BREMEN,
    ITEM_MM_MASK_BUNNY,       ITEM_MM_MASK_DON_GERO,      ITEM_MM_MASK_SCENTS, ITEM_MM_MASK_GORON,
    ITEM_MM_MASK_ROMANI,      ITEM_MM_MASK_CIRCUS_LEADER, ITEM_MM_MASK_KAFEI,  ITEM_MM_MASK_COUPLE,
    ITEM_MM_MASK_TRUTH,       ITEM_MM_MASK_ZORA,          ITEM_MM_MASK_KAMARO, ITEM_MM_MASK_GIBDO,
    ITEM_MM_MASK_GARO,        ITEM_MM_MASK_CAPTAIN,       ITEM_MM_MASK_GIANT,  ITEM_MM_MASK_FIERCE_DEITY,
};

// MM masks age requirements: regular masks = AGE_REQ_NONE, transformation masks = AGE_REQ_CHILD
// Transformation masks (Deku, Goron, Zora, Fierce Deity) are child-only unless TimelessEquipment cheat
const uint8_t gPage3MaskAgeReqs[24] = {
    AGE_REQ_NONE, AGE_REQ_NONE, AGE_REQ_NONE, AGE_REQ_NONE, AGE_REQ_NONE, AGE_REQ_CHILD, // [5]=Deku
    AGE_REQ_NONE, AGE_REQ_NONE, AGE_REQ_NONE, AGE_REQ_NONE, AGE_REQ_NONE, AGE_REQ_CHILD, // [11]=Goron
    AGE_REQ_NONE, AGE_REQ_NONE, AGE_REQ_NONE, AGE_REQ_NONE, AGE_REQ_NONE, AGE_REQ_CHILD, // [17]=Zora
    AGE_REQ_NONE, AGE_REQ_NONE, AGE_REQ_NONE, AGE_REQ_NONE, AGE_REQ_NONE, AGE_REQ_CHILD, // [23]=Fierce Deity
};
ExtendedInventoryState* ExtInv_GetState(void) {
    return &sExtInvState;
}
void ExtInv_Reset(void) {
    sExtInvState.currentPage = 0;
    sExtInvState.pageSwitchTimer = 0;
}
// Clamp page if custom items or MM masks CVar was toggled off
void ExtInv_ClampPage(void) {
    if (sExtInvState.currentPage == 1 && !ExtInv_IsCustomItemsEnabled()) {
        sExtInvState.currentPage = 0;
    }
    if (sExtInvState.currentPage == 2 && !ExtInv_IsMmMasksEnabled()) {
        sExtInvState.currentPage = 0;
    }
}
void ExtInv_Update(void) {
    if (sExtInvState.pageSwitchTimer > 0) {
        sExtInvState.pageSwitchTimer--;
    }
}
bool ExtInv_CanSwitchPage(void) {
    return sExtInvState.pageSwitchTimer == 0;
}
void ExtInv_SwitchPage(void) {
    int available[3];
    int count = 0;
    available[count++] = 0; // Page 0 always available
    if (ExtInv_IsCustomItemsEnabled())
        available[count++] = 1;
    if (ExtInv_IsMmMasksEnabled())
        available[count++] = 2;

    if (count <= 1)
        return; // Only page 0, can't switch

    // Find current page in available list, advance to next
    int curIdx = 0;
    for (int i = 0; i < count; i++) {
        if (available[i] == sExtInvState.currentPage) {
            curIdx = i;
            break;
        }
    }
    sExtInvState.currentPage = available[(curIdx + 1) % count];
    sExtInvState.pageSwitchTimer = 15;
}
int ExtInv_GetCurrentPage(void) {
    return sExtInvState.currentPage;
}
int ExtInv_GetMaxPages(void) {
    int count = 1; // Page 0 always available
    if (ExtInv_IsCustomItemsEnabled())
        count++;
    if (ExtInv_IsMmMasksEnabled())
        count++;
    return count;
}
bool ExtInv_IsCustomItemsEnabled(void) {
    // Default ON — NEI features are enabled by default.
    return CVarGetInteger("gMods.CustomItems.Enabled", 1) != 0;
}
bool ExtInv_IsMmMasksEnabled(void) {
    // Default ON — NEI features are enabled by default.
    return CVarGetInteger("gMods.MmMasks.InventoryEnabled", 1) != 0;
}
bool ExtInv_IsOnlyTransformation(void) {
    return CVarGetInteger("gMods.MmMasks.OnlyTransformation", 0) != 0;
}
int ExtInv_GetInventorySlot(int visualSlot) {
    return visualSlot + (sExtInvState.currentPage * 24);
}
bool ExtInv_IsSlotOnCurrentPage(uint8_t slot) {
    int pageStart = sExtInvState.currentPage * 24;
    int pageEnd = pageStart + 23;
    return (slot >= pageStart && slot <= pageEnd);
}
int ExtInv_GetPageForSlot(uint8_t slot) {
    if (slot >= 48)
        return 2;
    if (slot >= 24)
        return 1;
    return 0;
}
uint8_t ExtInv_GetItemAgeReq(uint16_t itemId) {
    for (int i = 0; i < 24; i++) {
        if (gPage2Items[i] == itemId) {
            return gPage2ItemAgeReqs[i];
        }
    }
    // MM Mask items: use per-mask age requirements from gPage3MaskAgeReqs
    if (itemId >= ITEM_MM_MASK_POSTMAN && itemId <= ITEM_MM_MASK_FIERCE_DEITY) {
        for (int i = 0; i < 24; i++) {
            if (gPage3MaskItems[i] == itemId) {
                return gPage3MaskAgeReqs[i];
            }
        }
        return AGE_REQ_NONE;
    }
    return 9;
}

// External vanilla array (trimmed to 24 entries)
extern uint8_t gSlotAgeReqs[];

uint8_t ExtInv_GetSlotAgeReq(uint8_t slot) {
    // Transformation mask override: the per-form allowlist IS the age requirement.
    // Allowed slots return 9 (AGE_REQ_NONE = always passes), restricted slots return
    // opposite age (always fails → greyed out). This lets child Link use adult items
    // if the form permits it (e.g., Zora can use bow regardless of Link's age).
    if (TransformMasks_IsEnabled() && TransformMasks_IsTransformedAny() && slot < 72) {
        if (ExtInv_IsSlotTransformRestricted(slot)) {
            extern SaveContext gSaveContext;
            return 1 - gSaveContext.linkAge;
        }
        return 9; // Allowed by form → bypass vanilla age check
    }

    // Vanilla slots (0-23) use the original array
    if (slot < 24) {
        return gSlotAgeReqs[slot];
    }
    // Custom slots (24-47) use gPage2ItemAgeReqs
    if (slot < 48) {
        return gPage2ItemAgeReqs[slot - 24];
    }
    // MM Mask slots (48-71) use gPage3MaskAgeReqs
    if (slot < 72) {
        return gPage3MaskAgeReqs[slot - 48];
    }
    return 9; // AGE_REQ_NONE for out-of-range
}

extern u8 gEquipAgeReqs[][4];

uint8_t ExtInv_GetEquipAgeReq(uint8_t row, uint8_t col) {
    // FD skin mode: allow swords (row 0) and shields (row 1), block tunics (row 2) and boots (row 3)
    if (TransformMasks_IsEnabled() && TransformMasks_IsFDSkinMode()) {
        extern SaveContext gSaveContext;
        if (row <= 1) {
            return 9; // AGE_REQ_NONE: swords/shields always available
        }
        // Tunics/boots: return opposite age to block them
        return 1 - gSaveContext.linkAge;
    }

    // Other transformations (Goron/Zora/Deku): block all equipment changes
    if (TransformMasks_IsEnabled() && TransformMasks_IsTransformedAny()) {
        extern SaveContext gSaveContext;
        return 1 - gSaveContext.linkAge;
    }

    return gEquipAgeReqs[row][col];
}

extern void* MmMasks_LoadNameTex(uint16_t itemId);
extern const char* MmMasks_GetNamePath(uint16_t itemId);

void* ExtInv_GetCustomItemNameTex(uint16_t itemId, uint8_t language) {
    // MM Mask items: return OTR path string so the RSP resolves actual texture
    // dimensions from resource metadata (HD mod textures render at native resolution).
    if (itemId >= ITEM_MM_MASK_POSTMAN && itemId <= ITEM_MM_MASK_FIERCE_DEITY) {
        const char* path = MmMasks_GetNamePath(itemId);
        if (path)
            return (void*)path;
        return NULL;
    }
    // Chateau Romani: name texture from mm.o2r
    if (itemId == ITEM_CHATEAU_ROMANI) {
        if (MmAssets_GetChateauIconPath()) // checks availability
            return (void*)"__OTR__item_name_static/gItemNameChateauRomaniENGTex";
        return NULL;
    }
    switch (itemId) {
        case ITEM_ROCS_FEATHER_SKIJER: // 0x9D
            return (void*)gRocsFeatherNameTex;
        case ITEM_ROCS_CAPE: // 0x9E
            return (void*)gRocsCapeNameTex;
        case ITEM_DESIRE_SENSOR: // 0x9F
            return (void*)gDesireSensorNameTex;
        case ITEM_HYLIAS_GRACE: // 0xA0
            return (void*)gHyliaGraceNameTex;
        case ITEM_ZONAI_PERMAFROST: // 0xA1
            return (void*)gZonaiPermafrostNameTex;
        case ITEM_DEMISE_DESTRUCTION: // 0xA2
            return (void*)gDemiseDestructionNameTex;
        case ITEM_DEKU_LEAF: // 0xA3
            return (void*)gDekuLeafNameTex;
        case ITEM_SWITCH_HOOK: // 0xA4
            return (void*)gSwitchHookNameTex;
        case ITEM_MOGMA_MITTS: // 0xA5
            return (void*)gMogmaMittsNameTex;
        case ITEM_GUST_JAR: // 0xA6
            return (void*)gGustJarNameTex;
        case ITEM_BALL_AND_CHAIN: // 0xA7
            return (void*)gBallAndChainNameTex;
        case ITEM_WHIP: // 0xA8
            return (void*)gWhipNameTex;
        case ITEM_SPINNER: // 0xA9
            return (void*)gSpinnerNameTex;
        case ITEM_CANE_OF_SOMARIA: // 0xAA
            return (void*)gCaneOfSomariaNameTex;
        case ITEM_DOMINION_ROD: // 0xAB
            return (void*)gDominionRodNameTex;
        case ITEM_TIME_GATE: // 0xAC
            return (void*)gTimeGateNameTex;
        case ITEM_BOMB_ARROWS: // 0xAD
            return (void*)gBombArrowsNameTex;
        case ITEM_ROD_FIRE: // 0xAE
            return (void*)gFireRodNameTex;
        case ITEM_ROD_ICE: // 0xAF
            return (void*)gIceRodNameTex;
        case ITEM_ROD_LIGHT: // 0xB0
            return (void*)gLightRodNameTex;
        case ITEM_BEETLE: // 0xB1
            return (void*)gBeetleNameTex;
        case ITEM_SHOVEL: // 0xB2
            return (void*)gShovelNameTex;
        case ITEM_MINISH_CAP: // 0xB3
            return (void*)gMinishCapNameTex;
        case ITEM_LANTERN: // 0xB4
            return (void*)gLanternNameTex;
        case ITEM_POKEBALL:
            return (void*)gPokeballNameTex;
        default:
            return NULL;
    }
}
extern void* MmMasks_LoadIcon(uint16_t itemId);
extern const char* MmMasks_GetIconPath(uint16_t itemId);
extern void* MmAssets_LoadFDSwordIcon(void);
extern const char* MmAssets_GetChateauIconPath(void);

// SM64 Mario caps — direct icon lookup, decoupled from the OOT spells. The caps
// are their own custom behavior (D-pad → Sm64Mario_HandleCapDpad), not an
// extension of Din's/Nayru's/Farore's. cap: 0 = Vanish, 1 = Metal, 2 = Wing,
// 3 = Fire Flower. Used by the corner power-up HUD draw in z_parameter.c.
void* ExtInv_GetCapIcon(uint8_t cap) {
    switch (cap) {
        case 0: return (void*)gItemIconVanishCapTex;
        case 1: return (void*)gItemIconMetalCapTex;
        case 2: return (void*)gItemIconWingCapTex;
        case 3: return (void*)gItemIconFireFlowerTex;
        default: return NULL;
    }
}

void* ExtInv_GetItemIcon(uint16_t itemId) {

    // Extended equipment: override A button icon when ext sword/shield is active
    // Suppressed during kaleido equipment screen so vanilla icons show there
    if (ExtEquip_IsEnabled() && !gExtEquipSuppressIconOverride) {
        u8 extSword = ExtEquip_GetCurrent(EQUIP_TYPE_SWORD);
        if (extSword > 0 && (itemId == ITEM_SWORD_KOKIRI || itemId == ITEM_SWORD_MASTER || itemId == ITEM_SWORD_BGS ||
                             itemId == ITEM_SWORD_KNIFE)) {
            void* icon = ExtEquip_GetIcon(EQUIP_TYPE_SWORD, extSword);
            if (icon)
                return icon;
        }
    }

    // FD skin mode: show FD sword icon for any equipped sword
    if (TransformMasks_IsFDSkinMode() && (itemId == ITEM_SWORD_KOKIRI || itemId == ITEM_SWORD_MASTER ||
                                          itemId == ITEM_SWORD_BGS || itemId == ITEM_SWORD_KNIFE)) {
        void* fdIcon = MmAssets_LoadFDSwordIcon();
        if (fdIcon)
            return fdIcon;
    }

    // SM64 caps are NO LONGER tied to the OOT spells — they're custom behavior
    // triggered straight from the D-pad (Sm64Mario_HandleCapDpad). The cap icons
    // are looked up directly via ExtInv_GetCapIcon (below), so there's no
    // spell→cap icon override here anymore.

    // SM64 Mario mask — the toggle item that locks to C-Down via
    // gSm64MarioMaskForce. Pressing C-Down with this equipped flips
    // gSm64Mario on/off (handled in mod_menu / z_player hook).
    if (itemId == ITEM_MARIO_MASK) {
        return (void*)gItemIconMarioMaskTex;
    }

    // Twilight Upgrade icon swap — when the corresponding mode is active
    // (persistent toggle via A in kaleido), swap hookshot/longshot/
    // boomerang icons to the upgraded variant.
    //
    // Clawshot specifically uses the MM (Majora's Mask) hookshot icon
    // straight from mm.o2r so the visual is 1:1 with TP's clawshot. The
    // local placeholder PNG (gItemIconClawshotTex) is only used if mm.o2r
    // isn't loaded — keeps the rest of the system working in environments
    // without the MM archive. Gale boomerang still uses its local
    // placeholder; no MM equivalent ported yet.
    {
        extern unsigned char TwilightUpgrade_IsClawshotActive(void);
        extern unsigned char TwilightUpgrade_IsGaleBoomerangActive(void);
        if ((itemId == ITEM_HOOKSHOT || itemId == ITEM_LONGSHOT) && TwilightUpgrade_IsClawshotActive()) {
            void* mmIcon = MmAssets_LoadHookshotIcon();
            if (mmIcon)
                return mmIcon;
            return (void*)gItemIconClawshotTex;
        }
        if (itemId == ITEM_BOOMERANG && TwilightUpgrade_IsGaleBoomerangActive()) {
            return (void*)gItemIconGaleBoomerangTex;
        }
    }

    if (itemId < 156) {
        return gItemIcons[itemId];
    }
    // Extended equipment items (0xE0-0xEB): return ext equip icon
    // Must check BEFORE MM masks since ranges overlap
    if (itemId >= ITEM_EXT_SWORD_1 && itemId <= ITEM_EXT_BOOTS_3) {
        u8 equipType = (itemId - ITEM_EXT_SWORD_1) / 3; // 0=sword,1=shield,2=tunic,3=boots
        u8 index = (itemId - ITEM_EXT_SWORD_1) % 3 + 1; // 1-3
        void* icon = ExtEquip_GetIcon(equipType, index);
        if (icon)
            return icon;
        return gItemIcons[0];
    }
    // MM Mask items: return OTR path string so the RSP resolves actual texture
    // dimensions from resource metadata (HD mod textures render at native resolution).
    if (itemId >= ITEM_MM_MASK_POSTMAN && itemId <= ITEM_MM_MASK_FIERCE_DEITY) {
        // Bunny Hood: use OOT icon (same appearance, enables OOT behavior)
        if (itemId == ITEM_MM_MASK_BUNNY) {
            return gItemIcons[ITEM_MASK_BUNNY];
        }
        const char* path = MmMasks_GetIconPath(itemId);
        if (path)
            return (void*)path;
        return gItemIcons[0]; // Fallback
    }
    switch (itemId) {
        // Prop Hunt button icons (0xD7-0xDC). Shown only while a hider is
        // in "prop mode" — the C-buttons + D-pad display these cycling/
        // category hints instead of vanilla item art.
        case ITEM_PH_ICON_POT:    return (void*)gItemIconPropHuntPotTex;
        case ITEM_PH_ICON_ENEMY:  return (void*)gItemIconPropHuntEnemyTex;
        case ITEM_PH_ICON_NPC:    return (void*)gItemIconPropHuntNpcTex;
        case ITEM_PH_ICON_CHANGE: return (void*)gItemIconPropHuntChangeTex;
        case ITEM_PH_ICON_PREV:   return (void*)gItemIconPropHuntPrevTex;
        case ITEM_PH_ICON_NEXT:   return (void*)gItemIconPropHuntNextTex;

        case ITEM_ROCS_FEATHER_SKIJER: // 0x9D
            return (void*)gItemIconRocsFeatherTex;
        case ITEM_ROCS_CAPE: // 0x9E
            return (void*)gItemIconRocsCapeTex;
        case ITEM_DESIRE_SENSOR: // 0x9F
            return (void*)gItemIconDesireSensorTex;
        case ITEM_HYLIAS_GRACE: // 0xA0
            return (void*)gItemIconHyliaGraceTex;
        case ITEM_ZONAI_PERMAFROST: // 0xA1
            return (void*)gItemIconZonaiPermafrostTex;
        case ITEM_DEMISE_DESTRUCTION: // 0xA2
            return (void*)gItemIconDemiseDestructionTex;
        case ITEM_DEKU_LEAF: // 0xA3
            return (void*)gItemIconDekuLeafTex;
        case ITEM_SWITCH_HOOK: // 0xA4
            return (void*)gItemIconSwitchHookTex;
        case ITEM_MOGMA_MITTS: // 0xA5
            return (void*)gItemIconMogmaMittsTex;
        case ITEM_GUST_JAR: // 0xA6
            return (void*)gItemIconGustJarTex;
        case ITEM_BALL_AND_CHAIN: // 0xA7
            return (void*)gItemIconBallAndChainTex;
        case ITEM_WHIP: // 0xA8
            return (void*)gItemIconWhipTex;
        case ITEM_SPINNER: // 0xA9
            return (void*)gItemIconSpinnerTex;
        case ITEM_CANE_OF_SOMARIA: // 0xAA
            return (void*)gItemIconCaneOfSomariaTex;
        case ITEM_DOMINION_ROD: // 0xAB
            return (void*)gItemIconDominionRodTex;
        case ITEM_TIME_GATE: // 0xAC
            return (void*)gItemIconTimeGateTex;
        case ITEM_BOMB_ARROWS: // 0xAD
            return (void*)gItemIconBombArrowsTex;
        case ITEM_ROD_FIRE: // 0xAE
            return (void*)gItemIconFireRodTex;
        case ITEM_ROD_ICE: // 0xAF
            return (void*)gItemIconIceRodTex;
        case ITEM_ROD_LIGHT: // 0xB0
            return (void*)gItemIconLightRodTex;
        case ITEM_BEETLE: // 0xB1
            return (void*)gItemIconBeetleTex;
        case ITEM_SHOVEL: // 0xB2
            return (void*)gItemIconShovelTex;
        case ITEM_MINISH_CAP: // 0xB3
            return (void*)gItemIconMinishCapTex;
        case ITEM_LANTERN: { // 0xB4
            extern u8 Lantern_GetFireType(void);
            switch (Lantern_GetFireType()) {
                case 1:
                    return (void*)gItemIconLanternFireTex; // Regular (orange)
                case 2:
                    return (void*)gItemIconLanternBlueTex; // Blue
                case 3:
                    return (void*)gItemIconLanternPoeTex; // Poe (purple)
                case 4:
                    return (void*)gItemIconLanternGreenTex; // Green
                default:
                    return (void*)gItemIconLanternTex; // Unlit
            }
        }
        case ITEM_POKEBALL:
            return (void*)gItemIconPokeballTex;

        // SW97 Medallion items (spell mode — show medallion quest icons)
        case ITEM_MEDALLION_FOREST:
            return (void*)"__OTR__textures/icon_item_24_static/gQuestIconMedallionForestTex";
        case ITEM_MEDALLION_FIRE:
            return (void*)"__OTR__textures/icon_item_24_static/gQuestIconMedallionFireTex";
        case ITEM_MEDALLION_WATER:
            return (void*)"__OTR__textures/icon_item_24_static/gQuestIconMedallionWaterTex";
        case ITEM_MEDALLION_SPIRIT:
            return (void*)"__OTR__textures/icon_item_24_static/gQuestIconMedallionSpiritTex";
        case ITEM_MEDALLION_SHADOW:
            return (void*)"__OTR__textures/icon_item_24_static/gQuestIconMedallionShadowTex";
        case ITEM_MEDALLION_LIGHT:
            return (void*)"__OTR__textures/icon_item_24_static/gQuestIconMedallionLightTex";

        // SW97 Arrow items (arrow mode — SAME medallion icons)
        case ITEM_SW97_ARROW_FIRE:
            return (void*)"__OTR__textures/icon_item_24_static/gQuestIconMedallionFireTex";
        case ITEM_SW97_ARROW_ICE:
            return (void*)"__OTR__textures/icon_item_24_static/gQuestIconMedallionWaterTex";
        case ITEM_SW97_ARROW_LIGHT:
            return (void*)"__OTR__textures/icon_item_24_static/gQuestIconMedallionLightTex";
        case ITEM_SW97_ARROW_DARK:
            return (void*)"__OTR__textures/icon_item_24_static/gQuestIconMedallionShadowTex";
        case ITEM_SW97_ARROW_SOUL:
            return (void*)"__OTR__textures/icon_item_24_static/gQuestIconMedallionSpiritTex";
        case ITEM_SW97_ARROW_WIND:
            return (void*)"__OTR__textures/icon_item_24_static/gQuestIconMedallionForestTex";

        case ITEM_CHATEAU_ROMANI: { // 0xB5
            const char* path = MmAssets_GetChateauIconPath();
            if (path)
                return (void*)path;
            return gItemIcons[ITEM_MILK_BOTTLE]; // Fallback to milk icon
        }
        default:
            return gItemIcons[0];
    }
}
// Returns 1 if the player owns the given MM mask item (extended inventory page 3, slots 48-71).
// Used by the trade-mask sale actors (En_Heishi2/Keaton, En_Mm/Bunny): masks with an MM
// counterpart are permanent items — selling them grants the reward without losing the mask.
int32_t ExtInv_HasMmMask(uint16_t itemId) {
    if (itemId < ITEM_MM_MASK_POSTMAN || itemId > ITEM_MM_MASK_FIERCE_DEITY) {
        return 0;
    }
    for (int i = 0; i < 24; i++) {
        if (gPage3MaskItems[i] == itemId) {
            extern SaveContext gSaveContext;
            return gSaveContext.inventory.items[48 + i] == itemId;
        }
    }
    return 0;
}

uint8_t ExtInv_GetItemSlot(uint16_t itemId) {
    if (itemId < 52) {
        return gItemSlots[itemId];
    }
    // Special case: ITEM_ROCS_CAPE shares slot with ITEM_ROCS_FEATHER_SKIJER (progressive upgrade)
    if (itemId == ITEM_ROCS_CAPE) {
        return SLOT_ROCS; // Same as SLOT_ROCS_FEATHER_SKIJER (24)
    }
    // Page 2 items
    for (int i = 0; i < 24; i++) {
        if (gPage2Items[i] == itemId) {
            return 24 + i;
        }
    }
    // Page 3 MM Mask items
    for (int i = 0; i < 24; i++) {
        if (gPage3MaskItems[i] == itemId) {
            return 48 + i;
        }
    }
    return 0xFF;
}
