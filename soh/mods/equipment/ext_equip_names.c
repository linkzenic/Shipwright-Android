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
            return (void*)gIronKnuckleAxeNameTex;

        // Shields
        case ITEM_EXT_SHIELD_1:
            return (void*)gDivineShieldNameTex;
        case ITEM_EXT_SHIELD_2:
            return (void*)gSheikahShieldNameTex;
        case ITEM_EXT_SHIELD_3:
            return (void*)gShieldOfIkanaNameTex;

        // Tunics
        case ITEM_EXT_TUNIC_1:
            return (void*)gMagicCapeNameTex;
        case ITEM_EXT_TUNIC_2:
            return (void*)gSpiritBreastplateNameTex;
        case ITEM_EXT_TUNIC_3:
            return (void*)gChampionsTunicNameTex;

        // Boots
        case ITEM_EXT_BOOTS_1:
            return (void*)gPegasusAnkletNameTex;
        case ITEM_EXT_BOOTS_2:
            // Load from mm.o2r (MM's actual Pendant of Memories name texture)
            return (void*)"__OTR__item_name_static/gItemNamePendantOfMemoriesENGTex";
        case ITEM_EXT_BOOTS_3:
            return (void*)gWaterDragonScaleNameTex;

        default:
            return NULL;
    }
}
