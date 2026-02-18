/**
 * extended_inventory.c - Extended inventory system implementation
 *
 * Manages custom items in multiple inventory pages.
 * Page 1: Vanilla OOT items (slots 0-23)
 * Page 2: Custom items (slots 24-47)
 * Page 3: MM Masks (slots 48-71) — requires mm.o2r and CVar
 */

#include "extended_inventory.h"
#include "z64.h"
#include <string.h>
#include "items/custom_icons.c"
#include "transformation_masks/transformation_masks.h"
#include "transformation_masks/assets/mm_asset_loader.h"
extern void* gItemIcons[];
extern uint8_t gItemSlots[];
extern const unsigned char gItemIconRocsFeatherTex[];
extern const unsigned char gItemIconRocsCapeTex[];
extern const unsigned char gItemIconDesireSensorTex[];
extern const unsigned char gItemIconHyliaGraceTex[];
extern const unsigned char gItemIconZonaiPermafrostTex[];
extern const unsigned char gItemIconDemiseDestructionTex[];
extern const unsigned char gItemIconDekuLeafTex[];
extern const unsigned char gItemIconSwitchHookTex[];
extern const unsigned char gItemIconMogmaMittsTex[];
extern const unsigned char gItemIconGustJarTex[];
extern const unsigned char gItemIconBallAndChainTex[];
extern const unsigned char gItemIconWhipTex[];
extern const unsigned char gItemIconSpinnerTex[];
extern const unsigned char gItemIconCaneOfSomariaTex[];
extern const unsigned char gItemIconDominionRodTex[];
extern const unsigned char gItemIconTimeGateTex[];
extern const unsigned char gItemIconBombArrowsTex[];
extern const unsigned char gItemIconFireRodTex[];
extern const unsigned char gItemIconIceRodTex[];
extern const unsigned char gItemIconLightRodTex[];
extern const unsigned char gItemIconBeetleTex[];
extern const unsigned char gItemIconShovelTex[];
extern const unsigned char gItemIconPending1Tex[];
extern const unsigned char gItemIconPending2Tex[];
extern const unsigned char gItemIconPending3Tex[];
static ExtendedInventoryState sExtInvState = { .currentPage = 0, .pageSwitchTimer = 0 };

// Page 2 item layout (slots 24-47)
// Note: ITEM_ROCS_FEATHER_SKIJER at slot 24 is progressive - becomes ITEM_ROCS_CAPE when upgraded (shares slot)
// Slot 15 (actual slot 39) now has ITEM_DESIRE_SENSOR instead of ITEM_ROCS_CAPE
const uint8_t gPage2Items[24] = { ITEM_ROCS_FEATHER_SKIJER, ITEM_WHIP,      ITEM_SPINNER,
                                  ITEM_BOMB_ARROWS,         ITEM_ROD_FIRE,  ITEM_DEMISE_DESTRUCTION,
                                  ITEM_DEKU_LEAF,           ITEM_TIME_GATE, ITEM_BEETLE,
                                  ITEM_SWITCH_HOOK,         ITEM_ROD_ICE,   ITEM_ZONAI_PERMAFROST,
                                  ITEM_MOGMA_MITTS,         ITEM_GUST_JAR,  ITEM_BALL_AND_CHAIN,
                                  ITEM_DESIRE_SENSOR,       ITEM_ROD_LIGHT, ITEM_HYLIAS_GRACE,
                                  ITEM_PENDING_2,           ITEM_PENDING_1, ITEM_PENDING_3,
                                  ITEM_CANE_OF_SOMARIA,     ITEM_SHOVEL,    ITEM_DOMINION_ROD };

// Age requirements for page 2 items
// Roc's items (slot 0/24) = AGE_REQ_NONE (both adult and child can use Feather AND Cape)
// Desire Sensor (slot 15/39) = AGE_REQ_NONE (both adult and child can use)
const uint8_t gPage2ItemAgeReqs[24] = { AGE_REQ_NONE, AGE_REQ_NONE,  AGE_REQ_NONE, AGE_REQ_ADULT, AGE_REQ_NONE,
                                        AGE_REQ_NONE, AGE_REQ_CHILD, AGE_REQ_NONE, AGE_REQ_ADULT, AGE_REQ_CHILD,
                                        AGE_REQ_NONE, AGE_REQ_NONE,  AGE_REQ_NONE, AGE_REQ_CHILD, AGE_REQ_ADULT,
                                        AGE_REQ_NONE, AGE_REQ_NONE,  AGE_REQ_NONE, AGE_REQ_NONE,  AGE_REQ_NONE,
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

// All MM masks are usable by both child and adult
const uint8_t gPage3MaskAgeReqs[24] = {
    AGE_REQ_NONE, AGE_REQ_NONE,  AGE_REQ_NONE, AGE_REQ_NONE,  AGE_REQ_NONE, AGE_REQ_CHILD, AGE_REQ_NONE, AGE_REQ_NONE,
    AGE_REQ_NONE, AGE_REQ_NONE,  AGE_REQ_NONE, AGE_REQ_CHILD, AGE_REQ_NONE, AGE_REQ_NONE,  AGE_REQ_NONE, AGE_REQ_NONE,
    AGE_REQ_NONE, AGE_REQ_CHILD, AGE_REQ_NONE, AGE_REQ_NONE,  AGE_REQ_NONE, AGE_REQ_NONE,  AGE_REQ_NONE, AGE_REQ_CHILD,
};
ExtendedInventoryState* ExtInv_GetState(void) {
    return &sExtInvState;
}
void ExtInv_Reset(void) {
    sExtInvState.currentPage = 0;
    sExtInvState.pageSwitchTimer = 0;
}
// Clamp page if MM masks CVar was toggled off while on page 2
void ExtInv_ClampPage(void) {
    int maxPages = ExtInv_GetMaxPages();
    if (sExtInvState.currentPage >= maxPages) {
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
    int maxPages = ExtInv_GetMaxPages();
    sExtInvState.currentPage = (sExtInvState.currentPage + 1) % maxPages;
    sExtInvState.pageSwitchTimer = 15;
}
int ExtInv_GetCurrentPage(void) {
    return sExtInvState.currentPage;
}
int ExtInv_GetMaxPages(void) {
    if (CVarGetInteger("gMods.MmMasks.InventoryEnabled", 0))
        return 3;
    return 2;
}
bool ExtInv_IsMmMasksEnabled(void) {
    return CVarGetInteger("gMods.MmMasks.InventoryEnabled", 0) != 0;
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

void* ExtInv_GetCustomItemNameTex(uint16_t itemId, uint8_t language) {
    // MM Mask items: load name texture from mm.o2r
    if (itemId >= ITEM_MM_MASK_POSTMAN && itemId <= ITEM_MM_MASK_FIERCE_DEITY) {
        return MmMasks_LoadNameTex(itemId);
    }
    extern const unsigned char gRocsFeatherNameTex[];
    extern const unsigned char gRocsCapeNameTex[];
    extern const unsigned char gDesireSensorNameTex[];
    extern const unsigned char gDekuLeafNameTex[];
    extern const unsigned char gSwitchHookNameTex[];
    extern const unsigned char gMogmaMittsNameTex[];
    extern const unsigned char gGustJarNameTex[];
    extern const unsigned char gBallAndChainNameTex[];
    extern const unsigned char gWhipNameTex[];
    extern const unsigned char gSpinnerNameTex[];
    extern const unsigned char gCaneOfSomariaNameTex[];
    extern const unsigned char gDominionRodNameTex[];
    extern const unsigned char gTimeGateNameTex[];
    extern const unsigned char gBombArrowsNameTex[];
    extern const unsigned char gFireRodNameTex[];
    extern const unsigned char gIceRodNameTex[];
    extern const unsigned char gLightRodNameTex[];
    extern const unsigned char gBeetleNameTex[];
    extern const unsigned char gShovelNameTex[];
    extern const unsigned char gHyliaGraceNameTex[];
    extern const unsigned char gZonaiPermafrostNameTex[];
    extern const unsigned char gDemiseDestructionNameTex[];
    extern const unsigned char gPending1NameTex[];
    extern const unsigned char gPending2NameTex[];
    extern const unsigned char gPending3NameTex[];
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
        case ITEM_PENDING_1: // 0xB3
            return (void*)gPending1NameTex;
        case ITEM_PENDING_2: // 0xB4
            return (void*)gPending2NameTex;
        case ITEM_PENDING_3: // 0xB6
            return (void*)gPending3NameTex;
        default:
            return NULL;
    }
}
extern void* MmMasks_LoadIcon(uint16_t itemId);
extern void* MmAssets_LoadFDSwordIcon(void);

void* ExtInv_GetItemIcon(uint16_t itemId) {
    // FD skin mode: show FD sword icon for any equipped sword
    if (TransformMasks_IsFDSkinMode() && (itemId == ITEM_SWORD_KOKIRI || itemId == ITEM_SWORD_MASTER ||
                                          itemId == ITEM_SWORD_BGS || itemId == ITEM_SWORD_KNIFE)) {
        void* fdIcon = MmAssets_LoadFDSwordIcon();
        if (fdIcon)
            return fdIcon;
    }

    if (itemId < 156) {
        return gItemIcons[itemId];
    }
    // MM Mask items: load icon from mm.o2r
    if (itemId >= ITEM_MM_MASK_POSTMAN && itemId <= ITEM_MM_MASK_FIERCE_DEITY) {
        // Bunny Hood: use OOT icon (same appearance, enables OOT behavior)
        if (itemId == ITEM_MM_MASK_BUNNY) {
            return gItemIcons[ITEM_MASK_BUNNY];
        }
        void* icon = MmMasks_LoadIcon(itemId);
        if (icon)
            return icon;
        return gItemIcons[0]; // Fallback
    }
    switch (itemId) {
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
        case ITEM_PENDING_1: // 0xB3
            return (void*)gItemIconPending1Tex;
        case ITEM_PENDING_2: // 0xB4
            return (void*)gItemIconPending2Tex;
        case ITEM_PENDING_3: // 0xB6
            return (void*)gItemIconPending3Tex;
        default:
            return gItemIcons[0];
    }
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
