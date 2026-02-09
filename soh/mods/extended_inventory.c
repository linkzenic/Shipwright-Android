/**
 * extended_inventory.c - Extended inventory system implementation
 *
 * Manages custom items in a second inventory page (slots 24-47).
 * Handles icon lookups, age requirements, and page switching.
 */

#include "extended_inventory.h"
#include "z64.h"
#include <string.h>
#include "items/custom_icons.c"
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
const uint8_t gPage2ItemAgeReqs[24] = { AGE_REQ_NONE,  AGE_REQ_ADULT, AGE_REQ_ADULT, AGE_REQ_ADULT, AGE_REQ_NONE,
                                        AGE_REQ_NONE,  AGE_REQ_CHILD, AGE_REQ_NONE,  AGE_REQ_ADULT, AGE_REQ_CHILD,
                                        AGE_REQ_NONE,  AGE_REQ_NONE,  AGE_REQ_ADULT, AGE_REQ_CHILD, AGE_REQ_ADULT,
                                        AGE_REQ_NONE,  AGE_REQ_NONE,  AGE_REQ_NONE,  AGE_REQ_ADULT, AGE_REQ_ADULT,
                                        AGE_REQ_ADULT, AGE_REQ_ADULT, AGE_REQ_NONE,  AGE_REQ_NONE };
ExtendedInventoryState* ExtInv_GetState(void) {
    return &sExtInvState;
}
void ExtInv_Reset(void) {
    sExtInvState.currentPage = 0;
    sExtInvState.pageSwitchTimer = 0;
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
    sExtInvState.currentPage ^= 1;
    sExtInvState.pageSwitchTimer = 15;
}
int ExtInv_GetCurrentPage(void) {
    return sExtInvState.currentPage;
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
    return (slot >= 24) ? 1 : 0;
}
uint8_t ExtInv_GetItemAgeReq(uint16_t itemId) {
    for (int i = 0; i < 24; i++) {
        if (gPage2Items[i] == itemId) {
            return gPage2ItemAgeReqs[i];
        }
    }
    return 9;
}

// External vanilla array (trimmed to 24 entries)
extern uint8_t gSlotAgeReqs[];

uint8_t ExtInv_GetSlotAgeReq(uint8_t slot) {
    // Vanilla slots (0-23) use the original array
    if (slot < 24) {
        return gSlotAgeReqs[slot];
    }
    // Custom slots (24-47) use gPage2ItemAgeReqs
    if (slot < 48) {
        return gPage2ItemAgeReqs[slot - 24];
    }
    return 9; // AGE_REQ_NONE for out-of-range
}
void* ExtInv_GetCustomItemNameTex(uint16_t itemId, uint8_t language) {
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
void* ExtInv_GetItemIcon(uint16_t itemId) {
    if (itemId < 156) {
        return gItemIcons[itemId];
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
    for (int i = 0; i < 24; i++) {
        if (gPage2Items[i] == itemId) {
            return 24 + i;
        }
    }
    return 0xFF;
}
