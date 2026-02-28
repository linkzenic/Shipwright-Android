/**
 * ext_equip_names.c - Placeholder name textures for extended equipment
 *
 * For now, returns NULL for all extended equipment names.
 * The kaleido code handles NULL by not drawing a name.
 * Real name textures (IA4, 128x16) will be added later.
 *
 * Included by extended_equipment.c (unity build).
 */

// Placeholder: no name textures yet.
// When real names are added, define them here as:
// static u8 gExtSword1NameTex[128 * 16 / 2] = { ... }; // IA4 format

static void* ExtEquip_LookupNameTex(u16 itemId, u8 language) {
    (void)language;

    switch (itemId) {
        case ITEM_EXT_SWORD_1:
        case ITEM_EXT_SWORD_2:
        case ITEM_EXT_SWORD_3:
        case ITEM_EXT_SHIELD_1:
        case ITEM_EXT_SHIELD_2:
        case ITEM_EXT_SHIELD_3:
        case ITEM_EXT_TUNIC_1:
        case ITEM_EXT_TUNIC_2:
        case ITEM_EXT_TUNIC_3:
        case ITEM_EXT_BOOTS_1:
        case ITEM_EXT_BOOTS_2:
        case ITEM_EXT_BOOTS_3:
        default:
            return NULL; // No name texture yet
    }
}
