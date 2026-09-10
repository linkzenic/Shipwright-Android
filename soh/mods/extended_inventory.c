#include "mods/nei_aligned_ui_paths.h"
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
#include "macros.h"    // ARRAY_COUNT — z64.h does not pull it in
#include "functions.h" // Item_Give, Player_UnsetMask (ExtInv_KeepMmMaskOrSell)
#include <string.h>
#include "assets/soh_assets.h"
#include "transformation_masks/transformation_masks.h"
#include "transformation_masks/kafei_form.h"
#include "transformation_masks/assets/mm_asset_loader.h"
#include "items/logic/weapon_upgrades.h" // NEI weapon-upgrade icon overrides
#include "expansions/sw97/sw97_config.h" // SW97_MEDALLIONS_ENABLED
#include "variables.h"                   // gItemIcons[158] con su tamaño REAL (aquí se declaraba incompleto y el
                                         // corte de abajo tenía que ir a mano, que es como se desfasó)
extern uint8_t gItemSlots[];
static ExtendedInventoryState sExtInvState = { .currentPage = 0, .pageSwitchTimer = 0 };

// Page 2 item layout (slots 24-47)
// Note: ITEM_ROCS_FEATHER_SKIJER at slot 24 is progressive - becomes ITEM_ROCS_CAPE when upgraded (shares slot)
// Slot 15 (actual slot 39) is FREE: the Desire Sensor that held it is a Sheikah Slate rune now.
const uint8_t gPage2Items[24] = { ITEM_ROCS_FEATHER_SKIJER,
                                  ITEM_WHIP,
                                  ITEM_SPINNER,
                                  ITEM_ELEMENTAL_WAND, // slot 27 — was ITEM_BOMB_ARROWS (now a flag)
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
                                  ITEM_NONE,
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
// Index 3 (slot 27) was AGE_REQ_ADULT for Bomb Arrows; the Elemental Wand that replaced it is
// age-free — the medallions gate it, not Link's age.
const uint8_t gPage2ItemAgeReqs[24] = { AGE_REQ_NONE, AGE_REQ_NONE,  AGE_REQ_NONE, AGE_REQ_NONE,  AGE_REQ_NONE,
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
    // MM Mask items: use per-mask age requirements from gPage3MaskAgeReqs
    // (kept on the page-3 table; NEI registry rows are no-op AGE_REQ_NONE).
    if (itemId >= ITEM_MM_MASK_POSTMAN && itemId <= ITEM_MM_MASK_FIERCE_DEITY) {
        for (int i = 0; i < 24; i++) {
            if (gPage3MaskItems[i] == itemId) {
                return gPage3MaskAgeReqs[i];
            }
        }
        return AGE_REQ_NONE;
    }
    // Page-2 custom items: unified NEI registry. Skijer's NEI
    const NeiItem* it = Nei_FindByItem(itemId);
    if (it != NULL) {
        return it->ageReq;
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

    // NEI: the Twilight clawshot upgrade makes the hookshot/longshot usable by child AND adult
    // (the clawshot is a child-friendly grapple). Owned-gated so child can still select it in the
    // kaleido to toggle clawshot mode.
    if (slot == SLOT_HOOKSHOT) {
        extern unsigned char TwilightUpgrade_HasClawshot(void);
        if (TwilightUpgrade_HasClawshot()) {
            return 9; // AGE_REQ_NONE
        }
    }

    // NEI: SLOT_TRADE_ADULT is no longer adult-only. It is the UNIFIED trade wheel — it also holds the
    // MM trade items (Moon's Tear, the 4 Title Deeds, Room Key, Letter to Kafei, Special Delivery), the
    // Pendant of Memories and the OoT *child* trade items. Vanilla marks the slot AGE_REQ_ADULT, which
    // greyed all of those out for child Link (they are obtained in MM / as child, so that was wrong).
    // Only the 11 genuine OoT adult-trade items (trade indices 0..10) keep the adult requirement.
    // Skijer's NEI
    if (slot == SLOT_TRADE_ADULT) {
        extern s32 TradeAdult_IndexOfItem(u8 item);
        s32 tradeIdx = TradeAdult_IndexOfItem(ExtInv_GetSlotItem(slot));
        if (tradeIdx > TRADE_ADULT_OOT_LAST) {
            return 9; // AGE_REQ_NONE
        }
    }

    // Vanilla slots (0-23) use the original array
    if (slot < 24) {
        return gSlotAgeReqs[slot];
    }
    // Custom slots (24-47): unified NEI registry. Skijer's NEI
    if (slot < 48) {
        const NeiItem* it = Nei_FindBySlot(slot);
        return it ? it->ageReq : 9;
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

    // NEI: the Great Fairy's Sword upgrade makes the Biggoron Sword (row 0 = swords, col 3 = BGS)
    // usable by BOTH child and adult (normally adult-only).
    if (row == 0 && col == 3 && WeaponUpgrade_HasGreatFairy()) {
        return 9; // AGE_REQ_NONE
    }

    return gEquipAgeReqs[row][col];
}

extern void* MmMasks_LoadNameTex(uint16_t itemId);
extern const char* MmMasks_GetNamePath(uint16_t itemId);

// Single source of truth for page-2 custom item icon + name-texture art.
// Both ExtInv_GetItemIcon and ExtInv_GetCustomItemNameTex index this table so
// the two associations can no longer drift apart.
//   icon == NULL  -> the icon getter falls through to its own special handling
//                    (used by ITEM_LANTERN, whose icon depends on fire type).
// Items needing dynamic/path-based art (Chateau Romani, MM masks, prop-hunt,
// SW97 medallions/arrows) are intentionally NOT in this table and stay handled
// by the surrounding special-case logic in each getter.
typedef struct {
    uint16_t itemId;
    void* icon;
    void* nameTex;
} CustomItemAsset;

static const CustomItemAsset sCustomItemAssets[] = {
    { ITEM_ROCS_FEATHER_SKIJER, (void*)gItemIconRocsFeatherTex, (void*)gRocsFeatherNameTex },            // 0x9D
    { ITEM_ROCS_CAPE, (void*)gItemIconRocsCapeTex, (void*)gRocsCapeNameTex },                            // 0x9E
    { ITEM_DESIRE_SENSOR, (void*)gItemIconDesireSensorTex, (void*)gDesireSensorNameTex },                // 0x9F
    { ITEM_HYLIAS_GRACE, (void*)gItemIconHyliaGraceTex, (void*)gHyliaGraceNameTex },                     // 0xA0
    { ITEM_ZONAI_PERMAFROST, (void*)gItemIconZonaiPermafrostTex, (void*)gZonaiPermafrostNameTex },       // 0xA1
    { ITEM_DEMISE_DESTRUCTION, (void*)gItemIconDemiseDestructionTex, (void*)gDemiseDestructionNameTex }, // 0xA2
    { ITEM_DEKU_LEAF, (void*)gItemIconDekuLeafTex, (void*)gDekuLeafNameTex },                            // 0xA3
    { ITEM_SWITCH_HOOK, (void*)gItemIconSwitchHookTex, (void*)gSwitchHookNameTex },                      // 0xA4
    { ITEM_MOGMA_MITTS, (void*)gItemIconMogmaMittsTex, (void*)gMogmaMittsNameTex },                      // 0xA5
    { ITEM_GUST_JAR, (void*)gItemIconGustJarTex, (void*)gGustJarNameTex },                               // 0xA6
    { ITEM_BALL_AND_CHAIN, (void*)gItemIconBallAndChainTex, (void*)gBallAndChainNameTex },               // 0xA7
    { ITEM_WHIP, (void*)gItemIconWhipTex, (void*)gWhipNameTex },                                         // 0xA8
    { ITEM_SPINNER, (void*)gItemIconSpinnerTex, (void*)gSpinnerNameTex },                                // 0xA9
    { ITEM_CANE_OF_SOMARIA, (void*)gItemIconCaneOfSomariaTex, (void*)gCaneOfSomariaNameTex },            // 0xAA
    { ITEM_DOMINION_ROD, (void*)gItemIconDominionRodTex, (void*)gDominionRodNameTex },                   // 0xAB
    { ITEM_TIME_GATE, (void*)gItemIconTimeGateTex, (void*)gTimeGateNameTex },                            // 0xAC
    // Bomb Arrows keeps its icon/name row even though it owns no inventory cell any more: the
    // wheel's corner badge and the get-item textbox still look them up by item id.
    { ITEM_BOMB_ARROWS, (void*)gItemIconBombArrowsTex, (void*)gBombArrowsNameTex }, // 0xAD
    // Elemental Wand's name is per-MODE, so it is resolved in ExtInv_GetCustomItemNameTex instead
    // of here. The row below is only the fallback; the icon is the same for every rod.
    { ITEM_ELEMENTAL_WAND, (void*)gItemIconElementalWandTex, (void*)gSandRodNameTex }, // 0xD0
    { ITEM_ROD_FIRE, (void*)gItemIconFireRodTex, (void*)gFireRodNameTex },             // 0xAE
    { ITEM_ROD_ICE, (void*)gItemIconIceRodTex, (void*)gIceRodNameTex },                // 0xAF
    { ITEM_ROD_LIGHT, (void*)gItemIconLightRodTex, (void*)gLightRodNameTex },          // 0xB0
    { ITEM_BEETLE, (void*)gItemIconBeetleTex, (void*)gBeetleNameTex },                 // 0xB1
    { ITEM_SHOVEL, (void*)gItemIconShovelTex, (void*)gShovelNameTex },                 // 0xB2
    { ITEM_MINISH_CAP, (void*)gItemIconMinishCapTex, (void*)gMinishCapNameTex },       // 0xB3
    // Lantern: name texture is constant, but the icon is chosen dynamically by
    // fire type -> icon left NULL so the icon getter handles it below.
    { ITEM_LANTERN, NULL, (void*)gLanternNameTex }, // 0xB4
    { ITEM_POKEBALL, (void*)gItemIconPokeballTex, (void*)gPokeballNameTex },
    // Mario Mask: slotless (page 2 is full) — ownership lives in
    // RAND_INF_OBTAINED_MARIO_MASK and unlocks MARIO MODE in the Crossover Items
    // form selector, which is also what reads this name texture. Skijer's NEI
    { ITEM_MARIO_MASK, (void*)gItemIconMarioMaskTex, (void*)gMarioMaskNameTex }, // 0xD6
    // Rito Mask: no page-2 cell of its own — it shares the Farore's Wind cell and
    // is reached with the kaleido cycle there (RitoItem_* in custom_forms.cpp).
    { ITEM_RITO_MASK, (void*)gItemIconRitoMaskTex, (void*)gRitoMaskNameTex }, // 0xD2
    // Bottle Randomizer extra items (Skijer's NEI). Net + Bottomless Bottle; SLOT_BOTTLE_3/4.
    { ITEM_NET, (void*)gItemIconNetTex, (void*)gNetNameTex },                                         // 0xF4
    { ITEM_BOTTOMLESS_BOTTLE, (void*)gItemIconBottomlessBottleTex, (void*)gBottomlessBottleNameTex }, // 0xF5
};

static const CustomItemAsset* ExtInv_FindCustomItemAsset(uint16_t itemId) {
    for (size_t i = 0; i < sizeof(sCustomItemAssets) / sizeof(sCustomItemAssets[0]); i++) {
        if (sCustomItemAssets[i].itemId == itemId) {
            return &sCustomItemAssets[i];
        }
    }
    return NULL;
}

void* ExtInv_GetCustomItemNameTex(uint16_t itemId, uint8_t language) {
    // 2026-08-06 page-2 additions — EXT (u16) ids; their IA4 name textures come from the
    // generate_names.py pipeline. Path strings, resolved by the RSP like every custom name.
    switch (itemId) {
        case EXT_ITEM_SHEIKAH_SLATE:
            return (void*)sNeiAligned_gSheikahSlateNameTex_153;
        case EXT_ITEM_PHANTOM_HOURGLASS:
            return (void*)sNeiAligned_gPhantomHourglassNameTex_149;
        case EXT_ITEM_SHADOW_CRYSTAL:
            return (void*)sNeiAligned_gShadowCrystalNameTex_152;
        case EXT_ITEM_ROD_OF_SEASONS:
            return (void*)sNeiAligned_gRodOfSeasonsNameTex_151;
        default:
            break;
    }

    // Quartz of Motion is Stone of Agony level 2 and shares its quest cell. The quest page's own
    // name index for that cell happens to BE ITEM_STONE_OF_AGONY, so this renames it. Skijer's NEI
    if (itemId == ITEM_STONE_OF_AGONY && Nei_Save()->quartzOwned) {
        return (void*)sNeiAligned_gQuartzOfMotionNameTex_150;
    }

    // Elemental Wand: one item id, six names — the name follows the active rod.
    if (itemId == ITEM_ELEMENTAL_WAND) {
        return Wand_ModeNameTex(Wand_GetMode());
    }
    // Dual Cane: same reasoning as the icon override — one item id, four names.
    if (itemId == ITEM_CANE_OF_SOMARIA && Nei_CaneOwned()) {
        switch (Nei_CaneGetType()) {
            case 1:
                return (void*)sNeiAligned_gTrirodNameTex_154;
            case 2:
                return (void*)sNeiAligned_gCaneOfPacciNameTex_145;
            case 3:
                return (void*)sNeiAligned_gUltrahandNameTex_155;
            default:
                break;
        }
    }
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
            return (void*)sNeiAligned_gItemNameChateauRomaniENGTex_74;
        return NULL;
    }
    // MM bottle-content custom items: name textures from mm.o2r (item_name_static), like Chateau.
    // Hylian Loach + Obaba's Drink only exist as JPN textures in MM. Skijer's NEI
    switch (itemId) {
        case ITEM_GOLD_DUST:
            return (void*)sNeiAligned_gItemNameGoldDustENGTex_85;
        case ITEM_HOT_SPRING_WATER:
            return (void*)sNeiAligned_gItemNameHotSpringWaterENGTex_89;
        case ITEM_DEKU_PRINCESS:
            return (void*)sNeiAligned_gItemNameDekuPrincessENGTex_78;
        case ITEM_SEAHORSE:
            return (void*)sNeiAligned_gItemNameSeaHorseENGTex_108;
        case ITEM_SPRING_WATER:
            return (void*)sNeiAligned_gItemNameSpringWaterENGTex_110;
        case ITEM_ZORA_EGG:
            return (void*)sNeiAligned_gItemNameZoraEggENGTex_113;
        case ITEM_HYLIAN_LOACH:
            return (void*)sNeiAligned_gItemNameHylianLoachJPNTex_90;
        case ITEM_OBABA_DRINK:
            return (void*)sNeiAligned_gItemNameObabasDrinkJPNTex_101;
        case ITEM_MAGIC_MUSHROOM:
            return (void*)sNeiAligned_gItemNameMagicalMushroomENGTex_96;
        // MM adult trade-quest items (Skijer's NEI) — names from mm.o2r item_name_static.
        case ITEM_MM_MOONS_TEAR:
            return (void*)sNeiAligned_gItemNameMoonsTearENGTex_99;
        case ITEM_MM_DEED_LAND:
            return (void*)sNeiAligned_gItemNameLandTitleDeedENGTex_94;
        case ITEM_MM_DEED_SWAMP:
            return (void*)sNeiAligned_gItemNameSwampTitleDeedENGTex_112;
        case ITEM_MM_DEED_MOUNTAIN:
            return (void*)sNeiAligned_gItemNameMountainTitleDeedENGTex_100;
        case ITEM_MM_DEED_OCEAN:
            return (void*)sNeiAligned_gItemNameOceanTitleDeedENGTex_102;
        case ITEM_MM_ROOM_KEY:
            return (void*)sNeiAligned_gItemNameRoomKeyENGTex_107;
        case ITEM_MM_LETTER_KAFEI:
            return (void*)sNeiAligned_gItemNameLetterToKafeiENGTex_95;
        case ITEM_MM_SPECIAL_DELIVERY:
            return (void*)sNeiAligned_gItemNameSpecialDeliveryToMamaENGTex_109;
        default:
            break;
    }
    // All page-2 custom item name textures live in the shared asset table.
    const CustomItemAsset* asset = ExtInv_FindCustomItemAsset(itemId);
    if (asset) {
        return asset->nameTex;
    }
    return NULL;
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
        case 0:
            return (void*)gItemIconVanishCapTex;
        case 1:
            return (void*)gItemIconMetalCapTex;
        case 2:
            return (void*)gItemIconWingCapTex;
        case 3:
            return (void*)gItemIconFireFlowerTex;
        default:
            return NULL;
    }
}

// mods/nei_save.cpp — Dual Cane context variables (see the icon override below).
uint8_t Nei_CaneOwned(void);
uint8_t Nei_CaneActiveSkill(void);

void* ExtInv_GetItemIcon(uint16_t itemId) {

    // Kafei lays SW97 landmines rather than throwing homing mice, so the slot has to read as one
    // while he is transformed and go back to the mouse the moment he is not. Skijer's NEI
    if (itemId == ITEM_BOMBCHU && KafeiForm_ReplacesBombchu()) {
        return (void*)sNeiAligned_gItemIconLandmineTex_123;
    }

    // 2026-08-06 page-2 additions — EXT (u16) inventory ids. Resolved FIRST: any generic fallback
    // below would index vanilla art with an id > 0xFF. Stand-in icons from OoT's own icon set;
    // TODO(user): real icons via the icon_item_custom PNG pipeline. Skijer's NEI
    switch (itemId) {
        case EXT_ITEM_SHEIKAH_SLATE:
            // Once any rune is lit the cell/HUD icon carries the ACTIVE rune's badge (wand idiom).
            if (Nei_Save()->slateRunesOwned != 0) {
                return Slate_RuneIcon(Slate_GetRune());
            }
            return (void*)sNeiAligned_gItemIconSheikahSlateTex_137;
        case EXT_ITEM_PHANTOM_HOURGLASS:
            return (void*)sNeiAligned_gItemIconPhantomHourglassTex_124;
        case EXT_ITEM_SHADOW_CRYSTAL:
            return (void*)sNeiAligned_gItemIconShadowCrystalTex_131;
        case EXT_ITEM_ROD_OF_SEASONS:
            return (void*)sNeiAligned_gItemIconRodOfSeasonsTex_125;
        default:
            break;
    }

    // Quartz of Motion — the same cell swap as its name above. 24x24, because the quest page draws
    // that cell at the quest-icon size and not at the item page's 32x32. Skijer's NEI
    if (itemId == ITEM_STONE_OF_AGONY && Nei_Save()->quartzOwned) {
        return (void*)sNeiAligned_gQuestIconQuartzOfMotionTex_144;
    }

    // ── Dual Cane: the cell's icon is simply which of the four is in hand ────
    // Four entries share one item id, so the icon cannot come from the id — it comes
    // from the context variable. Trirod and Ultrahand are their own entries here,
    // NOT the third level of the cane that unlocked them.
    //
    // OTR paths rather than the generated gItemIcon* symbols, because those only
    // exist after an asset re-extract; the FD sword override below does the same.
    if (itemId == ITEM_CANE_OF_SOMARIA && Nei_CaneOwned()) {
        switch (Nei_CaneGetType()) {
            case 1: // Trirod
                return (void*)sNeiAligned_gItemIconTrirodTex_142;
            case 2: // Cane of Pacci
                return (void*)sNeiAligned_gItemIconCaneOfPacciTex_121;
            case 3: // Ultrahand
                return (void*)sNeiAligned_gItemIconUltrahandTex_143;
            default:
                break; // Cane of Somaria keeps the cell's own icon
        }
    }

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
        // Return the OTR PATH (HD-mod-aware); the loader call is just the mm.o2r existence probe.
        if (MmAssets_LoadFDSwordIcon())
            return (void*)sNeiAligned_gItemIconFierceDeitySwordTex_20;
    }

    // NEI weapon upgrades — show the MM upgrade icon when the upgrade is owned and the matching
    // base weapon is equipped. The Kokiri top level (Gilded) and the BGS upgrade (GFS) each have
    // a display toggle in the Custom Items menu. Falls through to the vanilla icon (gItemIcons)
    // if mm.o2r lacks the asset. Real Master Sword keeps the vanilla Master icon (a glow is
    // applied at render time, not here).
    if (itemId == ITEM_SWORD_KOKIRI && WeaponUpgrade_KokiriLevel() >= 1) {
        u8 showGilded = WeaponUpgrade_HasGilded() && CVarGetInteger("gEnhancements.SkijerNEI.GildedUsesGildedLook", 1);
        const char* p = showGilded ? sNeiAligned_gItemIconGildedSwordTex_24
                                   : sNeiAligned_gItemIconRazorSwordTex_49;
        if (MmAssets_LoadResource(p)) // probe; return the PATH so the HD pack applies
            return (void*)p;
    }
    if (itemId == ITEM_SWORD_BGS && WeaponUpgrade_HasGreatFairy() &&
        CVarGetInteger("gEnhancements.SkijerNEI.BgsUsesGfsLook", 1)) {
        if (MmAssets_LoadResource(sNeiAligned_gItemIconGreatFairysSwordTex_28))
            return (void*)sNeiAligned_gItemIconGreatFairysSwordTex_28;
    }
    // Hammer → Iron Knuckle's Axe: show the axe icon while the upgrade is owned.
    if (itemId == ITEM_HAMMER && WeaponUpgrade_HasHammerAxe()) {
        return (void*)gItemIconDrillshaftTex;
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
            if (MmAssets_LoadHookshotIcon())
                return (void*)sNeiAligned_gItemIconHookshotTex_30;
            return (void*)gItemIconClawshotTex;
        }
        if (itemId == ITEM_BOOMERANG && TwilightUpgrade_IsGaleBoomerangActive()) {
            return (void*)gItemIconGaleBoomerangTex;
        }
    }

    // Pictograph Box shares the Lens of Truth slot. When the slot's pictobox mode is selected (kaleido
    // A-toggle), show the pictobox icon in the slot instead of the Lens — clear feedback for the swap,
    // mirroring the Clawshot/Gale overrides above. Skijer's NEI
    {
        extern unsigned char Picto_IsOwned(void);
        extern unsigned char Picto_IsOnLensActive(void);
        if (itemId == ITEM_LENS && Picto_IsOwned() && Picto_IsOnLensActive()) {
            // Return the OTR PATH (not the resolved MmAssets_LoadResource pointer) so gDPLoadTextureBlock
            // gets the name and Fast3D can substitute an MM HD texture pack (MM_Reloaded etc.) — a
            // resolved pointer draws base data at the wrong size and went blank under an HD pack. The
            // MmAssets_LoadResource call stays as the mm.o2r existence probe (falls through if absent).
            if (MmAssets_LoadResource(sNeiAligned_gItemIconPictographBoxTex_46)) {
                return (void*)sNeiAligned_gItemIconPictographBoxTex_46;
            }
        }
    }

    // Power Keg shares the Bomb slot. When power-keg mode is selected (kaleido A-toggle), show the
    // Power Keg icon in the slot instead of the Bomb. Skijer's NEI
    {
        extern unsigned char PowerKeg_IsOwned(void);
        extern unsigned char PowerKeg_IsOnBombActive(void);
        if (itemId == ITEM_BOMB && PowerKeg_IsOwned() && PowerKeg_IsOnBombActive()) {
            // Return the OTR PATH (see the pictobox note above) so the MM HD texture pack applies.
            if (MmAssets_LoadResource(sNeiAligned_gItemIconPowderKegTex_48)) {
                return (void*)sNeiAligned_gItemIconPowderKegTex_48;
            }
        }
    }

    // MM bottle-content custom items (Bottle Randomizer) — load the content icon from mm.o2r.
    // Placeholder texture names for now (TODO: swap to the exact mm.o2r names on test). Falls
    // through to the registry/fallback if mm.o2r isn't loaded. (Chateau Romani 0xB6 keeps its own
    // gItemIconChateauRomaniTex; Magic Mushroom 0xDD keeps its own.) Skijer's NEI
    {
        const char* p = NULL;
        if (itemId == ITEM_GOLD_DUST)
            p = sNeiAligned_gItemIconBottledGoldDustTex_6;
        else if (itemId == ITEM_HOT_SPRING_WATER)
            p = sNeiAligned_gItemIconHotSpringWaterTex_31;
        else if (itemId == ITEM_DEKU_PRINCESS)
            p = sNeiAligned_gItemIconBottledDekuPrincessTex_5;
        else if (itemId == ITEM_SEAHORSE)
            p = sNeiAligned_gItemIconBottledSeahorseTex_8;
        else if (itemId == ITEM_SPRING_WATER)
            p = sNeiAligned_gItemIconSpringWaterTex_52;
        else if (itemId == ITEM_ZORA_EGG)
            p = sNeiAligned_gItemIconBottledZoraEggTex_9;
        else if (itemId == ITEM_HYLIAN_LOACH)
            p = sNeiAligned_gItemIconBottledHylianLoachTex_7;
        else if (itemId == ITEM_OBABA_DRINK)
            p = sNeiAligned_gItemIconEmptyBottle2Tex_18;
        if (p && MmAssets_LoadResource(p))
            return (void*)p; // PATH -> HD-pack aware
    }

    // MM adult trade-quest items (Skijer's NEI) — shown in the SLOT_TRADE_ADULT 2D-grid wheel. Icons
    // from mm.o2r. The Pendant of Memories (ITEM_EXT_BOOTS_2) is the combat item, so it gets its icon
    // from the ext-equipment block below. Special Delivery to Mama reuses MM's "Letter to Mama" icon.
    {
        const char* p = NULL;
        if (itemId == ITEM_MM_MOONS_TEAR)
            p = sNeiAligned_gItemIconMoonsTearTex_41;
        else if (itemId == ITEM_MM_DEED_LAND)
            p = sNeiAligned_gItemIconLandDeedTex_35;
        else if (itemId == ITEM_MM_DEED_SWAMP)
            p = sNeiAligned_gItemIconSwampDeedTex_54;
        else if (itemId == ITEM_MM_DEED_MOUNTAIN)
            p = sNeiAligned_gItemIconMountainDeedTex_42;
        else if (itemId == ITEM_MM_DEED_OCEAN)
            p = sNeiAligned_gItemIconOceanDeedTex_43;
        else if (itemId == ITEM_MM_ROOM_KEY)
            p = sNeiAligned_gItemIconRoomKeyTex_51;
        else if (itemId == ITEM_MM_LETTER_KAFEI)
            p = sNeiAligned_gItemIconLetterToKafeiTex_36;
        else if (itemId == ITEM_MM_SPECIAL_DELIVERY)
            p = sNeiAligned_gItemIconLetterToMamaTex_37;
        if (p && MmAssets_LoadResource(p))
            return (void*)p; // PATH -> HD-pack aware
    }

    // Skijer's NEI boss_remains: MUST be resolved BEFORE the `itemId < 156` vanilla-array shortcut
    // below. Three of the four remains ids reclaim low slots (Odolwa 0x80=128, Goht 0x81=129,
    // Twinmold 0x89=137) that fall inside that range, so the shortcut used to hand back an unrelated
    // gItemIcons[] entry — which is why only Gyorg (0x9C = 156, just past the cutoff) looked right.
    // Their art exists only in mm.o2r; returning the PATH keeps HD packs working.
    switch (itemId) {
        case ITEM_MM_REMAINS_ODOLWA:
            return (void*)sNeiAligned_gItemIconOdolwasRemainsTex_44;
        case ITEM_MM_REMAINS_GOHT:
            return (void*)sNeiAligned_gItemIconGohtsRemainsTex_25;
        case ITEM_MM_REMAINS_GYORG:
            return (void*)sNeiAligned_gItemIconGyorgsRemainsTex_29;
        case ITEM_MM_REMAINS_TWINMOLD:
            return (void*)sNeiAligned_gItemIconTwinmoldsRemainsTex_55;
        default:
            break;
    }

    // El corte estaba escrito a mano como `< 156`, pero gItemIcons tiene 158 entradas: la pluma
    // SHIP-VANILLA (ITEM_ROCS_FEATHER = 0x9D = 157) quedaba JUSTO fuera, se escapaba a las ramas de
    // abajo, no encajaba en ninguna y acababa en el `gItemIcons[0]` de fallback — por eso se veía
    // como Deku Stick. Se usa ARRAY_COUNT para que no vuelva a desfasarse al crecer el array, y se
    // salta la entrada vacía (hay huecos "" en 0x82..0x9B) para no mandar basura a la RSP.
    if (itemId < ARRAY_COUNT(gItemIcons) && gItemIcons[itemId] != NULL &&
        ((const char*)gItemIcons[itemId])[0] != '\0') {
        return gItemIcons[itemId];
    }
    // ITEM_EXT_BOOTS_2 is the one shared id: as an INVENTORY / trade-wheel / C-button item it is the
    // Pendant of Memories, while grid slot (BOOTS, 2) is the Climb Boots (whose icon the kaleido reads
    // straight from ExtEquip_GetIcon, not from here). Skijer 2026-07-29
    if (itemId == ITEM_EXT_BOOTS_2) {
        void* pendantIcon = ExtEquip_GetPendantIcon();
        if (pendantIcon != NULL) {
            return pendantIcon;
        }
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
    // Page-2 custom items with a constant icon: unified NEI registry. Skijer's NEI
    // (ITEM_LANTERN has a NULL iconTex because its icon is dynamic; it falls
    // through to the dedicated case in the switch below.)
    {
        const NeiItem* it = Nei_FindByItem(itemId);
        if (it != NULL && it->iconTex != NULL) {
            return it->iconTex;
        }
    }
    switch (itemId) {
        // (boss_remains handled earlier — before the `itemId < 156` shortcut.)

        // Prop Hunt button icons (0xD7-0xDC). Shown only while a hider is
        // in "prop mode" — the C-buttons + D-pad display these cycling/
        // category hints instead of vanilla item art.
        case ITEM_PH_ICON_POT:
            return (void*)gItemIconPropHuntPotTex;
        case ITEM_PH_ICON_ENEMY:
            return (void*)gItemIconPropHuntEnemyTex;
        case ITEM_PH_ICON_NPC:
            return (void*)gItemIconPropHuntNpcTex;
        case ITEM_PH_ICON_CHANGE:
            return (void*)gItemIconPropHuntChangeTex;
        case ITEM_PH_ICON_PREV:
            return (void*)gItemIconPropHuntPrevTex;
        case ITEM_PH_ICON_NEXT:
            return (void*)gItemIconPropHuntNextTex;

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
            return (void*)sNeiAligned_gQuestIconMedallionForestTex_116;
        case ITEM_MEDALLION_FIRE:
            return (void*)sNeiAligned_gQuestIconMedallionFireTex_115;
        case ITEM_MEDALLION_WATER:
            return (void*)sNeiAligned_gQuestIconMedallionWaterTex_120;
        case ITEM_MEDALLION_SPIRIT:
            return (void*)sNeiAligned_gQuestIconMedallionSpiritTex_119;
        case ITEM_MEDALLION_SHADOW:
            return (void*)sNeiAligned_gQuestIconMedallionShadowTex_118;
        case ITEM_MEDALLION_LIGHT:
            return (void*)sNeiAligned_gQuestIconMedallionLightTex_117;

        // SW97 elemental shots no longer have item ids — the element is a flag and the medallion
        // icon is fetched directly via Sw97_ElementIcon(), which lands on the ITEM_MEDALLION_*
        // cases above.

        // Elemental Wand: one icon per rod, picked by the active mode.
        case ITEM_ELEMENTAL_WAND:
            return Wand_ModeIcon(Wand_GetMode());

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
            return Nei_GetOwnedItem((uint8_t)(48 + i)) == itemId; // Skijer's NEI
        }
    }
    return 0;
}

// Trade-mask sale helper for En_Mm (Bunny Hood) / En_Heishi2 (Keaton Mask): if the
// player does NOT own the given MM counterpart mask, take the worn OOT trade mask and
// hand back ITEM_SOLD_OUT (vanilla behavior). When the MM counterpart IS owned the mask
// is permanent, so it is kept and no SOLD_OUT is given. Collapses the byte-identical
// idiom both actors previously inlined.
void ExtInv_KeepMmMaskOrSell(PlayState* play, uint16_t maskItem) {
    if (!ExtInv_HasMmMask(maskItem)) {
        Player_UnsetMask(play);
        Item_Give(play, ITEM_SOLD_OUT);
    }
}

// ── SW97 primed element + Elemental Wand (Skijer's NEI) ──────────────────────────────────────────
// Single source of truth for "which element is the bow/slingshot primed with" and "which rod is the
// wand showing". Hosted here (not in nei_save.cpp) because this file is plain C, already includes
// z64item.h + nei_save.h, already owns the element->medallion icon map, and every consumer — both
// kaleido overlays, z_parameter.c and ArrowCycle.cpp — already links against it. No new .c file
// means no .vcxproj edit.
//
// The element used to be encoded as WHICH item id sat on the C-button. It is a flag now; the button
// always holds the plain weapon. See the SW97_ELEM_* block in nei_save.h for why the order matters.

// Element -> medallion item id, for the icon composited behind the weapon.
static const uint16_t sSw97ElemIcon[SW97_ELEM_COUNT] = {
    ITEM_NONE,             // SW97_ELEM_NONE
    ITEM_MEDALLION_FIRE,   // SW97_ELEM_FIRE
    ITEM_MEDALLION_WATER,  // SW97_ELEM_ICE
    ITEM_MEDALLION_LIGHT,  // SW97_ELEM_LIGHT
    ITEM_MEDALLION_SHADOW, // SW97_ELEM_DARK
    ITEM_MEDALLION_SPIRIT, // SW97_ELEM_SOUL
    ITEM_MEDALLION_FOREST, // SW97_ELEM_WIND
    ITEM_BOMB_ARROWS,      // SW97_ELEM_BOMB
};
// Element -> quest flag that unlocks it. ONE ENTRY PER LINE on purpose: this table was briefly
// written wrapped across two lines with an extra leading 0, which shifted every element onto the
// WRONG medallion (owning Forest unlocked "fire", owning Light unlocked "dark", and so on) — which
// in turn let the wheel offer elemental arrows the player does not own. Skijer's NEI
static const uint8_t sSw97ElemQuest[SW97_ELEM_COUNT] = {
    0,                      // SW97_ELEM_NONE  (unused — NONE is always available)
    QUEST_MEDALLION_FIRE,   // SW97_ELEM_FIRE
    QUEST_MEDALLION_WATER,  // SW97_ELEM_ICE
    QUEST_MEDALLION_LIGHT,  // SW97_ELEM_LIGHT
    QUEST_MEDALLION_SHADOW, // SW97_ELEM_DARK
    QUEST_MEDALLION_SPIRIT, // SW97_ELEM_SOUL
    QUEST_MEDALLION_FOREST, // SW97_ELEM_WIND
    0,                      // SW97_ELEM_BOMB  (unused — ownership is Sw97_BombArrowsOwned)
};
// Vanilla elemental arrow that ALSO unlocks the element (-1 = no vanilla equivalent). Kept from the
// old ArrowWheel_Build so a player with fire arrows but no medallion still gets the fire entry.
static const int16_t sSw97ElemVanillaArrow[SW97_ELEM_COUNT] = {
    -1, ITEM_ARROW_FIRE, ITEM_ARROW_ICE, ITEM_ARROW_LIGHT, -1, -1, -1, -1,
};

uint16_t Sw97_ElementIcon(uint8_t elem) {
    return (elem < SW97_ELEM_COUNT) ? sSw97ElemIcon[elem] : ITEM_NONE;
}

// Which of the three randomizer treatments the seed picked for Bomb Arrows.
uint8_t BombArrows_RandoMode(void) {
    return (uint8_t)CVarGetInteger("gMods.BombArrows.Mode", BOMB_ARROWS_RANDO_OFF);
}

uint8_t Sw97_BombArrowsOwned(void) {
    extern u8 TwilightUpgrade_HasBombArrows(void);
    if (Nei_Save()->bombArrowsOwned || TwilightUpgrade_HasBombArrows()) {
        return 1;
    }
    // "Bomb Bag" mode: owning any bomb bag is the unlock. Evaluated live rather than latched so
    // toggling the option mid-file behaves.
    return (BombArrows_RandoMode() == BOMB_ARROWS_RANDO_BOMB_BAG) && (CUR_UPG_VALUE(UPG_BOMB_BAG) > 0);
}

uint8_t Sw97_ElementOwned(uint8_t elem) {
    if (elem == SW97_ELEM_NONE) {
        return 1; // the plain weapon is always an option
    }
    if (elem == SW97_ELEM_BOMB) {
        return Sw97_BombArrowsOwned();
    }
    if (elem >= SW97_ELEM_COUNT) {
        return 0;
    }
    if (CHECK_QUEST_ITEM(sSw97ElemQuest[elem])) {
        return 1;
    }
    // Vanilla-arrow fallback (fire/ice/light only).
    return (sSw97ElemVanillaArrow[elem] >= 0) &&
           (INV_CONTENT((uint16_t)sSw97ElemVanillaArrow[elem]) == (uint8_t)sSw97ElemVanillaArrow[elem]);
}

// Is `elem` a legal value for this weapon at all? Bombs never ride the slingshot.
static uint8_t Sw97_ElementAllowed(uint8_t isSling, uint8_t elem) {
    if (isSling && (elem == SW97_ELEM_BOMB)) {
        return 0;
    }
    return Sw97_ElementOwned(elem);
}

uint8_t Sw97_ElementCount(uint8_t isSling) {
    uint8_t n = 0;
    for (uint8_t e = 0; e < SW97_ELEM_COUNT; e++) {
        if (Sw97_ElementAllowed(isSling, e)) {
            n++;
        }
    }
    return n;
}

uint8_t Sw97_ElementAt(uint8_t isSling, uint8_t index) {
    uint8_t n = 0;
    for (uint8_t e = 0; e < SW97_ELEM_COUNT; e++) {
        if (Sw97_ElementAllowed(isSling, e)) {
            if (n == index) {
                return e;
            }
            n++;
        }
    }
    return SW97_ELEM_NONE;
}

uint8_t Sw97_GetElement(uint8_t isSling) {
    NeiSaveData* nei = Nei_Save();
    uint8_t e = isSling ? nei->sw97SlingElement : nei->sw97BowElement;
    if (!Sw97_ElementAllowed(isSling, e)) {
        // Self-heal: a medallion can be lost (or the option toggled) after the flag was set.
        e = SW97_ELEM_NONE;
        if (isSling) {
            nei->sw97SlingElement = e;
        } else {
            nei->sw97BowElement = e;
        }
    }
    return e;
}

void Sw97_SetElement(uint8_t isSling, uint8_t elem) {
    if (!Sw97_ElementAllowed(isSling, elem)) {
        return;
    }
    if (isSling) {
        Nei_Save()->sw97SlingElement = elem;
    } else {
        Nei_Save()->sw97BowElement = elem;
    }
}

uint8_t Sw97_ElementNeighbor(uint8_t isSling, uint8_t elem, int32_t dir) {
    uint8_t n = Sw97_ElementCount(isSling);
    if (n <= 1) {
        return elem;
    }
    for (uint8_t i = 0; i < n; i++) {
        if (Sw97_ElementAt(isSling, i) == elem) {
            return Sw97_ElementAt(isSling, (uint8_t)((i + n + (dir > 0 ? 1 : -1)) % n));
        }
    }
    return Sw97_ElementAt(isSling, 0);
}

// THE accessor. Everything downstream — the arrow-type decode, the item action, the HUD composite,
// the pause grid — goes through this and nothing else, so the "CVar off" path stays byte-identical
// to the pre-refactor behavior and the bow-only rule for bombs lives in exactly one place.
uint8_t Sw97_EffectiveElement(uint8_t isSling) {
    if (!SW97_MEDALLIONS_ENABLED()) {
        return SW97_ELEM_NONE;
    }
    return Sw97_GetElement(isSling);
}

uint8_t Sw97_IsBowItem(uint16_t item) {
    return item == ITEM_BOW;
}
uint8_t Sw97_IsSlingItem(uint16_t item) {
    return item == ITEM_SLINGSHOT;
}

// The composite HUD icon is built from iconItemSegment[], which only refreshes when the button's
// item is (re)loaded. Changing the element does not change the item, so every setter has to ask for
// the reload by hand — otherwise layer 1 keeps the previous weapon and it reads as a texture bug.
void Sw97_RefreshButtonIcons(PlayState* play) {
    // i < 8, NOT i <= 8: buttonItems is u8[8] and iconItemSegment is exactly 8 pages of 0x1000, so
    // index 8 reads past the array and makes Interface_LoadItemIcon1 DMA a full page past the end of
    // the icon buffer — a silent heap smash. Skijer's NEI
    for (int32_t i = 1; i < ARRAY_COUNT(gSaveContext.equips.buttonItems); i++) {
        uint8_t item = gSaveContext.equips.buttonItems[i];
        if (Sw97_IsBowItem(item) || Sw97_IsSlingItem(item)) {
            Interface_LoadItemIcon1(play, i);
        }
    }
}

// Is Bomb Arrows the primed element on some button right now? Replaces the old
// IsItemEquipped(ITEM_BOMB_ARROWS), which scanned for a literal id that no longer lands there.
uint8_t Sw97_BombArrowsOnButton(void) {
    for (int32_t i = 0; i < ARRAY_COUNT(gSaveContext.equips.buttonItems); i++) {
        uint8_t item = gSaveContext.equips.buttonItems[i];
        if ((Sw97_IsBowItem(item) || Sw97_IsSlingItem(item)) &&
            (Sw97_EffectiveElement(Sw97_IsSlingItem(item)) == SW97_ELEM_BOMB)) {
            return 1;
        }
    }
    return 0;
}

// One-shot save migration off the per-element item ids. Idempotent via sw97LayoutVersion.
//
// The legacy ids are GONE from z64item.h, so they are spelled as raw values here on purpose — this
// function is the only place that must still recognise them, and hard-coding them keeps the enum
// free of ghosts. 0xD0..0xD5 were ITEM_SW97_ARROW_FIRE..WIND (0xD0 is the Elemental Wand now, which
// is exactly why an unmigrated save must not be left holding one).
void Sw97_MigrateLayout(PlayState* play) {
    NeiSaveData* nei = Nei_Save();
    if (nei->sw97LayoutVersion >= 1) {
        return;
    }

    // Buttons: B + 3 C + 4 D-pad.
    for (int32_t i = 0; i < 8; i++) {
        uint8_t item = gSaveContext.equips.buttonItems[i];
        if (item >= 0xD0 && item <= 0xD5) {
            nei->sw97BowElement = (uint8_t)(SW97_ELEM_FIRE + (item - 0xD0));
            gSaveContext.equips.buttonItems[i] = ITEM_BOW;
        } else if (item == ITEM_BOMB_ARROWS) {
            nei->sw97BowElement = SW97_ELEM_BOMB;
            gSaveContext.equips.buttonItems[i] = ITEM_BOW;
        } else {
            continue;
        }
        // The wheel used to mark these slots 0xFF ("not from inventory"). The button holds a real
        // inventory item now, so clear the marker or the slot stays unbindable.
        if (i >= 1 && i <= 3) {
            gSaveContext.equips.cButtonSlots[i - 1] = SLOT_BOW;
        }
    }

    // Page-2 slot 27 held ITEM_BOMB_ARROWS; the cell belongs to the Elemental Wand now.
    if (Nei_GetOwnedItem(SLOT_BOMB_ARROWS) == ITEM_BOMB_ARROWS) {
        nei->bombArrowsOwned = 1;
        Nei_SetOwnedItem(SLOT_BOMB_ARROWS, ITEM_NONE);
    }

    nei->sw97LayoutVersion = 1;
    Sw97_RefreshButtonIcons(play);
}

// ── Elemental Wand — six rods in one page-2 cell ─────────────────────────────────────────────────
static const uint8_t sWandQuest[WAND_MODE_COUNT] = {
    QUEST_MEDALLION_SPIRIT, // Sand Rod
    QUEST_MEDALLION_FOREST, // Tornado Rod
    QUEST_MEDALLION_WATER,  // Water Rod
    QUEST_MEDALLION_FIRE,   // Meteor Rod
    QUEST_MEDALLION_LIGHT,  // Storm Rod
    QUEST_MEDALLION_SHADOW, // Shadow Scepter
};
static const uint16_t sWandMedallion[WAND_MODE_COUNT] = {
    ITEM_MEDALLION_SPIRIT, ITEM_MEDALLION_FOREST, ITEM_MEDALLION_WATER,
    ITEM_MEDALLION_FIRE,   ITEM_MEDALLION_LIGHT,  ITEM_MEDALLION_SHADOW,
};

static void* const sWandNameTex[WAND_MODE_COUNT] = {
    (void*)gSandRodNameTex,   (void*)gTornadoRodNameTex, (void*)gWaterRodNameTex,
    (void*)gMeteorRodNameTex, (void*)gStormRodNameTex,   (void*)gShadowScepterNameTex,
};

// One icon for all six rods, Gust Jar idiom: the ELEMENT is the medallion the kaleido draws behind
// it, not a different staff sprite. Keeps the six modes reading as one item you retune.
void* Wand_ModeIcon(uint8_t mode) {
    (void)mode;
    return (void*)gItemIconElementalWandTex;
}
void* Wand_ModeNameTex(uint8_t mode) {
    return (mode < WAND_MODE_COUNT) ? sWandNameTex[mode] : sWandNameTex[0];
}

uint8_t Wand_RandoMode(void) {
    return (uint8_t)CVarGetInteger("gRandoSettings.ElementalWandShuffle", WAND_RANDO_MEDALLIONS);
}

uint16_t Wand_ModeMedallion(uint8_t mode) {
    return (mode < WAND_MODE_COUNT) ? sWandMedallion[mode] : ITEM_NONE;
}

// Which rods are usable. All three randomizer treatments share the SAME slot flag; they differ only
// in what unlocks an individual mode.
uint8_t Wand_ModeOwned(uint8_t mode) {
    if (mode >= WAND_MODE_COUNT) {
        return 0;
    }
    switch (Wand_RandoMode()) {
        case WAND_RANDO_SINGLE:
            return Nei_Save()->wandRodsOwned != 0; // one item lights all six
        case WAND_RANDO_ELEMENTAL:
            return (Nei_Save()->wandRodsOwned & (1 << mode)) != 0;
        case WAND_RANDO_MEDALLIONS:
        default:
            return CHECK_QUEST_ITEM(sWandQuest[mode]) != 0;
    }
}

void Wand_GrantMode(uint8_t mode) {
    if (mode >= WAND_MODE_COUNT) {
        return;
    }
    if (Wand_RandoMode() == WAND_RANDO_SINGLE) {
        Nei_Save()->wandRodsOwned = (1 << WAND_MODE_COUNT) - 1;
    } else {
        Nei_Save()->wandRodsOwned |= (1 << mode);
    }
    // Obtaining ANY rod hands over the slot if it isn't there yet.
    ExtInv_SetSlotItem(SLOT_ELEMENTAL_WAND, ITEM_ELEMENTAL_WAND);
}

// The writer for Wand_ModeOwned, kept next to it so the two can never disagree: a dev toggle has to
// write the field the ACTIVE treatment reads. Writing only wandRodsOwned looks like it works and
// then changes nothing under the default Medallions rule, which reads the quest medallion instead.
void Wand_SetModeOwned(uint8_t mode, uint8_t owned) {
    if (mode >= WAND_MODE_COUNT) {
        return;
    }

    uint8_t rule = Wand_RandoMode();

    if (owned && (rule != WAND_RANDO_MEDALLIONS)) {
        Wand_GrantMode(mode);
        return;
    }

    if (rule == WAND_RANDO_MEDALLIONS) {
        if (owned) {
            gSaveContext.inventory.questItems |= gBitFlags[sWandQuest[mode]];
        } else {
            gSaveContext.inventory.questItems &= ~gBitFlags[sWandQuest[mode]];
        }
    } else if (rule == WAND_RANDO_SINGLE) {
        Nei_Save()->wandRodsOwned = 0; // one flag lights all six, so it only clears wholesale
    } else {
        Nei_Save()->wandRodsOwned &= (uint8_t) ~(1 << mode);
    }

    if (owned) {
        ExtInv_SetSlotItem(SLOT_ELEMENTAL_WAND, ITEM_ELEMENTAL_WAND);
    } else if (Wand_ModeCount() == 0) {
        ExtInv_SetSlotItem(SLOT_ELEMENTAL_WAND, ITEM_NONE);
    }
}

uint8_t Wand_ModeCount(void) {
    uint8_t n = 0;
    for (uint8_t m = 0; m < WAND_MODE_COUNT; m++) {
        if (Wand_ModeOwned(m)) {
            n++;
        }
    }
    return n;
}

uint8_t Wand_ModeAt(uint8_t index) {
    uint8_t n = 0;
    for (uint8_t m = 0; m < WAND_MODE_COUNT; m++) {
        if (Wand_ModeOwned(m)) {
            if (n == index) {
                return m;
            }
            n++;
        }
    }
    return WAND_MODE_SAND;
}

uint8_t Wand_GetMode(void) {
    uint8_t m = Nei_Save()->wandMode;
    if (!Wand_ModeOwned(m)) {
        m = Wand_ModeAt(0);
        Nei_Save()->wandMode = m;
    }
    return m;
}

void Wand_SetMode(uint8_t mode) {
    if (Wand_ModeOwned(mode)) {
        Nei_Save()->wandMode = mode;
    }
}

uint8_t Wand_ModeNeighbor(uint8_t mode, int32_t dir) {
    uint8_t n = Wand_ModeCount();
    if (n <= 1) {
        return mode;
    }
    for (uint8_t i = 0; i < n; i++) {
        if (Wand_ModeAt(i) == mode) {
            return Wand_ModeAt((uint8_t)((i + n + (dir > 0 ? 1 : -1)) % n));
        }
    }
    return Wand_ModeAt(0);
}

// ── Sheikah Slate — five runes in one page-2 cell (wand idiom, no rando-mode split: each rune is
// always its own sibling item, "random" order comes from where the seed hides them) ──────────────
static void* const sSlateRuneMiniIcon[SLATE_RUNE_COUNT] = {
    (void*)sNeiAligned_gItemIconSlateRuneBombTex_138,
    (void*)sNeiAligned_gItemIconSlateRuneStasisTex_141,
    (void*)sNeiAligned_gItemIconSlateRuneCryonisTex_139,
    (void*)sNeiAligned_gItemIconSlateRuneMasterCycleTex_140,
    (void*)sNeiAligned_gItemIconDesireSensorTex_122,
};
static void* const sSlateRuneIcon[SLATE_RUNE_COUNT] = {
    (void*)sNeiAligned_gItemIconSheikahSlateBombTex_132,
    (void*)sNeiAligned_gItemIconSheikahSlateStasisTex_136,
    (void*)sNeiAligned_gItemIconSheikahSlateCryonisTex_133,
    (void*)sNeiAligned_gItemIconSheikahSlateMasterCycleTex_134,
    (void*)sNeiAligned_gItemIconSheikahSlateSensorTex_135,
};

void* Slate_RuneMiniIcon(uint8_t rune) {
    return (rune < SLATE_RUNE_COUNT) ? sSlateRuneMiniIcon[rune] : sSlateRuneMiniIcon[0];
}
void* Slate_RuneIcon(uint8_t rune) {
    return (rune < SLATE_RUNE_COUNT) ? sSlateRuneIcon[rune] : sSlateRuneIcon[0];
}

uint8_t Slate_RuneOwned(uint8_t rune) {
    if (rune >= SLATE_RUNE_COUNT) {
        return 0;
    }
    return (Nei_Save()->slateRunesOwned & (1 << rune)) != 0;
}

void Slate_GrantRune(uint8_t rune) {
    if (rune >= SLATE_RUNE_COUNT) {
        return;
    }
    Nei_Save()->slateRunesOwned |= (1 << rune);
    // The freshly obtained rune becomes the active one — this is also what makes the get-item
    // textbox icon (resolved through Slate_GetRune) show the rune that was just granted.
    Nei_Save()->slateMode = rune;
    // Obtaining ANY rune hands over the slate itself if it isn't there yet.
    ExtInv_GiveItem(SLOT_SHEIKAH_SLATE, EXT_ITEM_SHEIKAH_SLATE);
}

uint8_t Slate_RuneCount(void) {
    uint8_t n = 0;
    for (uint8_t r = 0; r < SLATE_RUNE_COUNT; r++) {
        if (Slate_RuneOwned(r)) {
            n++;
        }
    }
    return n;
}

uint8_t Slate_RuneAt(uint8_t index) {
    uint8_t n = 0;
    for (uint8_t r = 0; r < SLATE_RUNE_COUNT; r++) {
        if (Slate_RuneOwned(r)) {
            if (n == index) {
                return r;
            }
            n++;
        }
    }
    return SLATE_RUNE_BOMB;
}

uint8_t Slate_GetRune(void) {
    uint8_t r = Nei_Save()->slateMode;
    if (!Slate_RuneOwned(r)) {
        r = Slate_RuneAt(0);
        Nei_Save()->slateMode = r;
    }
    return r;
}

void Slate_SetRune(uint8_t rune) {
    if (Slate_RuneOwned(rune)) {
        Nei_Save()->slateMode = rune;
    }
}

uint8_t Slate_RuneNeighbor(uint8_t rune, int32_t dir) {
    uint8_t n = Slate_RuneCount();
    if (n <= 1) {
        return rune;
    }
    for (uint8_t i = 0; i < n; i++) {
        if (Slate_RuneAt(i) == rune) {
            return Slate_RuneAt((uint8_t)((i + n + (dir > 0 ? 1 : -1)) % n));
        }
    }
    return Slate_RuneAt(0);
}

// ── Rod of Seasons — four seasons in one page-2 cell (slate idiom: each season is its own sibling
// item, "random" order comes from where the seed hides them) ─────────────────────────────────────
static void* const sSeasonIcon[SEASON_SLOTS] = {
    (void*)sNeiAligned_gItemIconSeasonSpringTex_128,
    (void*)sNeiAligned_gItemIconSeasonSummerTex_129,
    (void*)sNeiAligned_gItemIconSeasonAutumnTex_126,
    (void*)sNeiAligned_gItemIconSeasonWinterTex_130,
    (void*)sNeiAligned_gItemIconSeasonOffTex_127,
};

// The emblem colour of each coin on the staff, lifted to flame brightness — the raw texture values
// are too dark to read as fire. Hue is the coin's; only the level moved.
static const uint8_t sSeasonColor[SEASON_SLOTS][3] = {
    { 6, 235, 64 },  // Spring — Clover_Coin green
    { 235, 5, 7 },   // Summer — Sun_Coin red
    { 235, 166, 6 }, // Autumn — Buttons_Coin gold
    { 4, 105, 235 }, // Winter — Hex_Coin blue
    { 40, 36, 48 },  // Off — the blank coin
};

void* Seasons_SeasonIcon(uint8_t season) {
    return (season < SEASON_SLOTS) ? sSeasonIcon[season] : sSeasonIcon[0];
}

void Seasons_SeasonColor(uint8_t season, uint8_t* r, uint8_t* g, uint8_t* b) {
    const uint8_t* c = sSeasonColor[(season < SEASON_SLOTS) ? season : SEASON_SPRING];

    *r = c[0];
    *g = c[1];
    *b = c[2];
}

uint8_t Seasons_SeasonOwned(uint8_t season) {
    if (season == SEASON_OFF) {
        return 1; // the blank coin comes with the rod
    }
    if (season >= SEASON_COUNT) {
        return 0;
    }
    return (Nei_Save()->seasonsOwned & (1 << season)) != 0;
}

void Seasons_GrantSeason(uint8_t season) {
    if (season >= SEASON_COUNT) {
        return;
    }
    Nei_Save()->seasonsOwned |= (1 << season);
    // The freshly obtained season becomes the active one, which is also what makes the get-item
    // textbox icon (resolved through Seasons_GetSeason) show the season that was just granted.
    Nei_Save()->season = season;
    // Obtaining ANY season hands over the rod itself if it isn't there yet.
    ExtInv_GiveItem(SLOT_ROD_OF_SEASONS, EXT_ITEM_ROD_OF_SEASONS);
}

uint8_t Seasons_SeasonCount(void) {
    uint8_t n = 0;
    for (uint8_t s = 0; s < SEASON_COUNT; s++) {
        if (Seasons_SeasonOwned(s)) {
            n++;
        }
    }
    return n;
}

uint8_t Seasons_SeasonAt(uint8_t index) {
    uint8_t n = 0;
    for (uint8_t s = 0; s < SEASON_COUNT; s++) {
        if (Seasons_SeasonOwned(s)) {
            if (n == index) {
                return s;
            }
            n++;
        }
    }
    return SEASON_SPRING;
}

uint8_t Seasons_GetSeason(void) {
    uint8_t s = Nei_Save()->season;
    if (!Seasons_SeasonOwned(s)) {
        s = Seasons_SeasonAt(0);
        Nei_Save()->season = s;
    }
    return s;
}

void Seasons_SetSeason(uint8_t season) {
    if (Seasons_SeasonOwned(season)) {
        Nei_Save()->season = season;
    }
}

uint8_t Seasons_SeasonNeighbor(uint8_t season, int32_t dir) {
    uint8_t n = Seasons_SeasonCount();
    if (n <= 1) {
        return season;
    }
    for (uint8_t i = 0; i < n; i++) {
        if (Seasons_SeasonAt(i) == season) {
            return Seasons_SeasonAt((uint8_t)((i + n + (dir > 0 ? 1 : -1)) % n));
        }
    }
    return Seasons_SeasonAt(0);
}

uint8_t ExtInv_GetItemSlot(uint16_t itemId) {
    if (itemId < 52) {
        return gItemSlots[itemId];
    }
    // Rito Mask has no cell of its own: it lives in the Farore's Wind cell and is
    // reached by cycling there, the same shared-slot idea as Roc's Feather in the
    // Nayru's Love cell. Skijer's NEI
    if (itemId == ITEM_RITO_MASK) {
        return SLOT_FARORES_WIND;
    }
    // Page 2 items (incl. ROCS_CAPE -> shared SLOT_ROCS): unified NEI registry. Skijer's NEI
    const NeiItem* it = Nei_FindByItem(itemId);
    if (it != NULL && it->slot != NEI_NO_SLOT) {
        return it->slot;
    }
    // Page 3 MM Mask items
    for (int i = 0; i < 24; i++) {
        if (gPage3MaskItems[i] == itemId) {
            return 48 + i;
        }
    }
    return 0xFF;
}
