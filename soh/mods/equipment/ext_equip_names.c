#include "mods/nei_aligned_ui_paths.h"
/**
 * ext_equip_names.c - Name textures for extended equipment
 *
 * Returns OTR path pointers for each equipment item's name texture.
 * Textures are 128x16 IA4 PNGs in soh/assets/custom/textures/item_name_custom/.
 *
 * Included by extended_equipment.c (unity build).
 */

#include "assets/soh_assets.h"

static void* ExtEquip_LookupNameTex(u16 itemId, u8 language) {
    (void)language;

    switch (itemId) {
        // Swords
        case ITEM_EXT_SWORD_1:
            return (void*)gCaneOfByrnaNameTex;
        case ITEM_EXT_SWORD_2:
            return (void*)gFourSwordNameTex;
        case ITEM_EXT_SWORD_3:
            return (void*)gTridentNameTex;

        // Shields
        case ITEM_EXT_SHIELD_1:
            return (void*)gGoddessShieldNameTex;
        case ITEM_EXT_SHIELD_2:
            return (void*)gKiteShieldNameTex;
        case ITEM_EXT_SHIELD_3:
            return (void*)gShieldOfIkanaNameTex;

        // Tunics — recolor tunics: 1=Champion (blue), 2=Magic Tunic (orange), 3=Sage's (white)
        case ITEM_EXT_TUNIC_1:
            return (void*)gChampionsTunicNameTex;
        case ITEM_EXT_TUNIC_2:
            return (void*)gMagicTunicNameTex;
        case ITEM_EXT_TUNIC_3:
            return (void*)gSagesTunicNameTex;

        // Boots — all three are REAL boots now (Skijer 2026-07-29)
        case ITEM_EXT_BOOTS_1:
            return (void*)gPegasusBootsNameTex;
        case ITEM_EXT_BOOTS_2:
            // Shared id: the grid slot is the Climb Boots, the inventory/trade-wheel item with this id
            // is still the Pendant of Memories (mm.o2r name texture). Skijer 2026-07-29
            return gExtEquipGridNameContext ? (void*)gClimbBootsNameTex
                                            : (void*)sNeiAligned_gItemNamePendantOfMemoriesENGTex_103;
        case ITEM_EXT_BOOTS_3:
            return (void*)gRocBootsNameTex;

        default:
            return NULL;
    }
}
