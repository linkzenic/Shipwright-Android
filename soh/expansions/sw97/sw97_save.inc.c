/**
 * sw97_save.c - Save presets for SW97 tour system
 *
 * Original: z64proto/sw97 team (Spaceworld '97 Experience)
 * Adapted for Ship of Harkinian (Shipwright)
 *
 * Provides Save_InitSpaceWorld() which configures gSaveContext with
 * the appropriate inventory, equipment, and spawn for each tour option.
 */

// Tour preset IDs
typedef enum {
    SW97_PRESET_CHILD_DEFAULT, // Hyrule Tour: Link's House
    SW97_PRESET_CHILD_CASTLE,  // Hyrule Tour: Hyrule Castle
    SW97_PRESET_ADULT_DEFAULT, // Hyrule Tour: Hyrule Field (horse), Dungeon Tour: Special Course, Battle: Old Sutaru
    SW97_PRESET_CHILD_DC_DMT,  // Hyrule Tour: Death Mountain, Dungeon Tour: Dodongo's Cavern
    SW97_PRESET_DEKU_TREE,     // Dungeon Tour: Deku Tree
    SW97_PRESET_CHILD_GOHMA,   // Battle Tour: Gohma Boss
    SW97_PRESET_CHILD_KD,      // Battle Tour: King Dodongo
} Sw97SavePreset;

// ================================= Child Default (Full SW97 Experience) =================================
static ItemEquips sSw97ChildEquips = {
    { ITEM_SWORD_KOKIRI, ITEM_DINS_FIRE, ITEM_SLINGSHOT, ITEM_BOOMERANG },
    { SLOT_DINS_FIRE, SLOT_SLINGSHOT, SLOT_BOOMERANG },
    0x1111,
};

static Inventory sSw97ChildInventory = {
    // items[24]: stick, nut, bomb, 0xFF, 0xFF, dins_fire, slingshot, 0xFF,
    //            0xFF, 0xFF, 0xFF, farores_wind, boomerang, 0xFF, 0xFF, 0xFF,
    //            0xFF, nayrus_love, 0xFF..
    { ITEM_STICK,     ITEM_NUT, ITEM_BOMB, 0xFF, 0xFF, ITEM_DINS_FIRE,
      ITEM_SLINGSHOT, 0xFF,     0xFF,      0xFF, 0xFF, ITEM_FARORES_WIND,
      ITEM_BOOMERANG, 0xFF,     0xFF,      0xFF, 0xFF, ITEM_NAYRUS_LOVE,
      0xFF,           0xFF,     0xFF,      0xFF, 0xFF, 0xFF },
    { 5, 20, 10, 0, 0, 0, 30, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    0x1111,
    0x124208, // bomb bag + bullet bag
    0,
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0xFF },
    0,
    0,
};

// ================================= Child Castle =================================
static ItemEquips sSw97ChildCastleEquips = {
    { ITEM_SWORD_KOKIRI, ITEM_NONE, ITEM_NONE, ITEM_NONE },
    { SLOT_NONE, SLOT_NONE, SLOT_NONE },
    0x1111,
};

static Inventory sSw97ChildCastleInventory = {
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 1, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    0x1111,
    0x120200,
    0,
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0xFF },
    0,
    0,
};

// ================================= Child DC/DMT =================================
static ItemEquips sSw97ChildDCEquips = {
    { ITEM_SWORD_KOKIRI, ITEM_STICK, ITEM_NUT, ITEM_SLINGSHOT },
    { SLOT_STICK, SLOT_NUT, SLOT_SLINGSHOT },
    0x1111,
};

static Inventory sSw97ChildDCInventory = {
    { ITEM_STICK,     ITEM_NUT, 0xFF, 0xFF, 0xFF, 0xFF, ITEM_SLINGSHOT, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      ITEM_BOOMERANG, 0xFF,     0xFF, 0xFF, 0xFF, 0xFF, 0xFF,           0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 1, 10, 0, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    0x1111,
    0x124200,
    0,
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0xFF },
    0,
    0,
};

// ================================= Child Gohma =================================
static ItemEquips sSw97ChildGohmaEquips = {
    { ITEM_SWORD_KOKIRI, ITEM_STICK, ITEM_NUT, ITEM_SLINGSHOT },
    { SLOT_STICK, SLOT_NUT, SLOT_SLINGSHOT },
    0x1111,
};

static Inventory sSw97ChildGohmaInventory = {
    { ITEM_STICK,     ITEM_NUT, 0xFF, 0xFF, 0xFF, 0xFF, ITEM_SLINGSHOT, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      ITEM_BOOMERANG, 0xFF,     0xFF, 0xFF, 0xFF, 0xFF, 0xFF,           0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 1, 10, 0, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    0x1111,
    0x124200,
    0,
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0xFF },
    0,
    0,
};

// ================================= Child King Dodongo =================================
static ItemEquips sSw97ChildKDEquips = {
    { ITEM_SWORD_KOKIRI, ITEM_BOMB, ITEM_BOOMERANG, ITEM_SLINGSHOT },
    { SLOT_BOMB, SLOT_BOOMERANG, SLOT_SLINGSHOT },
    0x1111,
};

static Inventory sSw97ChildKDInventory = {
    { ITEM_STICK,     ITEM_NUT, ITEM_BOMB, 0xFF, 0xFF, 0xFF, ITEM_SLINGSHOT, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      ITEM_BOOMERANG, 0xFF,     0xFF,      0xFF, 0xFF, 0xFF, 0xFF,           0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 1, 10, 16, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    0x1111,
    0x124208,
    0,
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0xFF },
    0,
    0,
};

// ================================= Adult (Full SW97 Experience) =================================
static ItemEquips sSw97AdultEquips = {
    { ITEM_SWORD_MASTER, ITEM_BOW, ITEM_DINS_FIRE, ITEM_BOMB },
    { SLOT_BOW, SLOT_DINS_FIRE, SLOT_BOMB },
    0x1122,
};

static Inventory sSw97AdultInventory = {
    // items[24]: stick, nut, bomb, bow, fire_arrow, dins_fire, slingshot, ocarina,
    //            bombchu, hookshot, ice_arrow, farores_wind, boomerang, lens, bean, hammer,
    //            light_arrow, nayrus_love, bottle0..bottle3, child_trade, adult_trade
    { 0xFF,
      0xFF,
      ITEM_BOMB,
      ITEM_BOW,
      ITEM_ARROW_FIRE,
      ITEM_DINS_FIRE,
      0xFF,
      0xFF,
      ITEM_BOMBCHU,
      ITEM_HOOKSHOT,
      ITEM_ARROW_ICE,
      ITEM_FARORES_WIND,
      ITEM_BOOMERANG,
      ITEM_LENS,
      0xFF,
      ITEM_HAMMER,
      ITEM_ARROW_LIGHT,
      ITEM_NAYRUS_LOVE,
      0xFF,
      0xFF,
      0xFF,
      0xFF,
      0xFF,
      0xFF },
    // ammo[16]: sticks, nuts, bombs, arrows, fire_arr, 0, seeds, 0, chus, 0, 0, 0, 0, 0, beans, 0
    { 1, 10, 30, 30, 0, 0, 16, 0, 20, 0, 0, 0, 0, 0, 0, 0 },
    // equipment: bit flags for swords/shields/tunics/boots
    0x1127, // kokiri+master sword, all shields, kokiri+goron tunic, kokiri+iron boots
    // upgrades: packed nibbles for quiver/bomb bag/strength/scale/wallet
    0x125A09,                                                       // quiver 30, bomb bag 20, silver gauntlets
    0,                                                              // questItems
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // dungeonItems
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0xFF }, // dungeonKeys
    0,        // defenseHearts
    0,        // gsTokens
};

// Helper: set base SW97 player data fields directly in gSaveContext
static void Sw97_InitPlayerData(void) {
    // newf
    memset(gSaveContext.newf, 0, sizeof(gSaveContext.newf));
    // deaths
    gSaveContext.deaths = 0;
    // playerName = "LINK"
    gSaveContext.playerName[0] = 0x15;
    gSaveContext.playerName[1] = 0x12;
    gSaveContext.playerName[2] = 0x17;
    gSaveContext.playerName[3] = 0x14;
    gSaveContext.playerName[4] = 0x3E;
    gSaveContext.playerName[5] = 0x3E;
    gSaveContext.playerName[6] = 0x3E;
    gSaveContext.playerName[7] = 0x3E;
    // n64ddFlag
    gSaveContext.n64ddFlag = 0;
    // health
    gSaveContext.healthCapacity = 8 * 0x10; // 8 hearts
    gSaveContext.health = 8 * 0x10;
    // magic
    gSaveContext.magicLevel = 0;
    gSaveContext.magic = 0x30;
    // rupees
    gSaveContext.rupees = 0;
    // swordHealth
    gSaveContext.swordHealth = 8;
    // naviTimer
    gSaveContext.naviTimer = 0;
    // magic flags
    gSaveContext.isMagicAcquired = 0;
    gSaveContext.isDoubleMagicAcquired = 0;
    gSaveContext.isDoubleDefenseAcquired = 0;
    // bgsFlag
    gSaveContext.bgsFlag = 0;
    // ocarinaGameRoundNum
    gSaveContext.ocarinaGameRoundNum = 1;
    // child/adult equips (empty)
    memset(&gSaveContext.childEquips, 0, sizeof(ItemEquips));
    memset(&gSaveContext.adultEquips, 0, sizeof(ItemEquips));
    // savedSceneNum
    gSaveContext.savedSceneNum = 0x34;
}

/**
 * Initialize gSaveContext for a SW97 tour preset.
 * Sets up player data, inventory, equips for the selected tour option.
 */
void Sw97_InitSave(s32 preset) {
    // Base player data
    Sw97_InitPlayerData();

    switch (preset) {
        case SW97_PRESET_CHILD_DEFAULT:
            gSaveContext.equips = sSw97ChildEquips;
            gSaveContext.inventory = sSw97ChildInventory;
            gSaveContext.linkAge = 1; // child
            break;

        case SW97_PRESET_CHILD_CASTLE:
            gSaveContext.equips = sSw97ChildCastleEquips;
            gSaveContext.inventory = sSw97ChildCastleInventory;
            gSaveContext.linkAge = 1;
            break;

        case SW97_PRESET_ADULT_DEFAULT:
            gSaveContext.equips = sSw97AdultEquips;
            gSaveContext.inventory = sSw97AdultInventory;
            gSaveContext.linkAge = 0; // adult
            break;

        case SW97_PRESET_CHILD_DC_DMT:
            gSaveContext.equips = sSw97ChildDCEquips;
            gSaveContext.inventory = sSw97ChildDCInventory;
            gSaveContext.linkAge = 1;
            break;

        case SW97_PRESET_DEKU_TREE:
            gSaveContext.equips = sSw97ChildEquips;
            gSaveContext.inventory = sSw97ChildInventory;
            gSaveContext.linkAge = 1;
            break;

        case SW97_PRESET_CHILD_GOHMA:
            gSaveContext.equips = sSw97ChildGohmaEquips;
            gSaveContext.inventory = sSw97ChildGohmaInventory;
            gSaveContext.linkAge = 1;
            break;

        case SW97_PRESET_CHILD_KD:
            gSaveContext.equips = sSw97ChildKDEquips;
            gSaveContext.inventory = sSw97ChildKDInventory;
            gSaveContext.linkAge = 1;
            break;

        default:
            gSaveContext.equips = sSw97ChildEquips;
            gSaveContext.inventory = sSw97ChildInventory;
            gSaveContext.linkAge = 1;
            break;
    }

    // Common setup
    gSaveContext.isMagicAcquired = 1;
    gSaveContext.isDoubleMagicAcquired = (preset == SW97_PRESET_ADULT_DEFAULT) ? 1 : 0;
    gSaveContext.magicLevel = (preset == SW97_PRESET_ADULT_DEFAULT) ? 2 : 1;
    gSaveContext.magic = (preset == SW97_PRESET_ADULT_DEFAULT) ? 0x60 : 0x30;
    gSaveContext.magicState = 0;
    gSaveContext.nayrusLoveTimer = 0;

    // Adult preset gets 20 hearts
    if (preset == SW97_PRESET_ADULT_DEFAULT) {
        gSaveContext.healthCapacity = 20 * 0x10;
        gSaveContext.health = 20 * 0x10;
    }
}

/**
 * Give SW97 full inventory to the current save context (mid-game cheat).
 * Unlike Sw97_InitSave(), this ADDS items without wiping existing save data.
 * Gives: all 3 OOT spells, bow, all 3 arrow types, bombs, bombchus,
 * hookshot, hammer, lens, boomerang, slingshot, sticks, nuts.
 * Sets double magic and fills health/magic/ammo.
 */
void Sw97_GiveFullInventory(void) {
    // Spells
    gSaveContext.inventory.items[SLOT_DINS_FIRE] = ITEM_DINS_FIRE;
    gSaveContext.inventory.items[SLOT_FARORES_WIND] = ITEM_FARORES_WIND;
    gSaveContext.inventory.items[SLOT_NAYRUS_LOVE] = ITEM_NAYRUS_LOVE;

    // Ranged weapons
    gSaveContext.inventory.items[SLOT_BOW] = ITEM_BOW;
    gSaveContext.inventory.items[SLOT_ARROW_FIRE] = ITEM_ARROW_FIRE;
    gSaveContext.inventory.items[SLOT_ARROW_ICE] = ITEM_ARROW_ICE;
    gSaveContext.inventory.items[SLOT_ARROW_LIGHT] = ITEM_ARROW_LIGHT;
    gSaveContext.inventory.items[SLOT_SLINGSHOT] = ITEM_SLINGSHOT;

    // Items
    gSaveContext.inventory.items[SLOT_BOMB] = ITEM_BOMB;
    gSaveContext.inventory.items[SLOT_BOMBCHU] = ITEM_BOMBCHU;
    gSaveContext.inventory.items[SLOT_HOOKSHOT] = ITEM_HOOKSHOT;
    gSaveContext.inventory.items[SLOT_HAMMER] = ITEM_HAMMER;
    gSaveContext.inventory.items[SLOT_LENS] = ITEM_LENS;
    gSaveContext.inventory.items[SLOT_BOOMERANG] = ITEM_BOOMERANG;
    gSaveContext.inventory.items[SLOT_STICK] = ITEM_STICK;
    gSaveContext.inventory.items[SLOT_NUT] = ITEM_NUT;

    // Ammo
    gSaveContext.inventory.ammo[SLOT_STICK] = 10;
    gSaveContext.inventory.ammo[SLOT_NUT] = 30;
    gSaveContext.inventory.ammo[SLOT_BOMB] = 30;
    gSaveContext.inventory.ammo[SLOT_BOW] = 30;
    gSaveContext.inventory.ammo[SLOT_SLINGSHOT] = 30;
    gSaveContext.inventory.ammo[SLOT_BOMBCHU] = 30;

    // Equipment: kokiri+master sword, all shields, kokiri+goron tunic, kokiri+iron boots
    gSaveContext.inventory.equipment |= 0x1127;

    // Upgrades: quiver 30, bomb bag 20, bullet bag
    gSaveContext.inventory.upgrades |= 0x125A09;

    // Magic: double magic
    gSaveContext.isMagicAcquired = 1;
    gSaveContext.isDoubleMagicAcquired = 1;
    gSaveContext.magicLevel = 2;
    gSaveContext.magic = 0x60;
    gSaveContext.magicState = 0;

    // Health: 20 hearts, full
    gSaveContext.healthCapacity = 20 * 0x10;
    gSaveContext.health = 20 * 0x10;

    // Sword health
    gSaveContext.swordHealth = 8;
}
