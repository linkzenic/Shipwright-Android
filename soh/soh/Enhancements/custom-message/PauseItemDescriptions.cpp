/**
 * PauseItemDescriptions.cpp - C-Up item descriptions in pause menu
 *
 * When the player presses C-Up while hovering over a custom item/equipment/mask
 * in the pause menu, a short utility-focused description textbox is displayed.
 */

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/custom-message/CustomMessageTypes.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "z64item.h"
#include "macros.h"
#include "variables.h"
#include "mods/extended_equipment.h"
#include "expansions/sw97/sw97_config.h"
}

// ---------------------------------------------------------------------------
// Description table: { itemId, textId, description }
// ---------------------------------------------------------------------------

struct ItemDescEntry {
    u16 itemId;
    u16 textId;
    const char* desc;
};

static const ItemDescEntry sCustomItemDescs[] = {
    { ITEM_ROCS_FEATHER_SKIJER, TEXT_DESC_ROCS_FEATHER, "Jump in ground and small jump from water." },
    { ITEM_ROCS_CAPE, TEXT_DESC_ROCS_CAPE, "Jump from ground or water. Press again&in the air for a double jump." },
    { ITEM_DESIRE_SENSOR, TEXT_DESC_DESIRE_SENSOR, "Sense major items in this area.&Costs 3 hearts. Randomizer only." },
    { ITEM_HYLIAS_GRACE, TEXT_DESC_HYLIAS_GRACE,
      "Fairy flight for 10s. Ignores walls.&A=up, B=down, L=sprint. 24 MP." },
    { ITEM_ZONAI_PERMAFROST, TEXT_DESC_ZONAI_PERMAFROST,
      "Stop time for 10s. Enemies, NPCs&and bosses freeze. Costs 12 magic." },
    { ITEM_DEMISE_DESTRUCTION, TEXT_DESC_DEMISE_DESTRUCTION,
      "Massive AoE explosion. Damages all&enemies in range. Ground only. 12 MP." },
    { ITEM_DEKU_LEAF, TEXT_DESC_DEKU_LEAF, "Ground: blow wind gust. Air: hold&to glide. Drains magic while gliding." },
    { ITEM_SWITCH_HOOK, TEXT_DESC_SWITCH_HOOK, "Aim and fire to swap positions&with objects and enemies." },
    { ITEM_MOGMA_MITTS, TEXT_DESC_MOGMA_MITTS, "Toggle to climb any wall.&Drains magic over time." },
    { ITEM_GUST_JAR, TEXT_DESC_GUST_JAR, "Pull enemies toward you, then push&them away. Hold C for element select." },
    { ITEM_BALL_AND_CHAIN, TEXT_DESC_BALL_AND_CHAIN,
      "Heavy thrown weapon. Breaks ice walls&and heavy objects. Hold C to charge.&C-Up to aim." },
    { ITEM_WHIP, TEXT_DESC_WHIP, "Grapple from any bar surface. Swing&with joystick. Release for momentum&launch." },
    { ITEM_SPINNER, TEXT_DESC_SPINNER, "Toggle to ride. A for homing dash&attack. Breaks rocks." },
    { ITEM_CANE_OF_SOMARIA, TEXT_DESC_CANE_OF_SOMARIA,
      "Create statues (max 3) that press&any switch. Hookable and throwable." },
    { ITEM_DOMINION_ROD, TEXT_DESC_DOMINION_ROD,
      "Fire orb to possess Beamos, Armos&or Anubis. Control them with analog+C." },
    { ITEM_TIME_GATE, TEXT_DESC_TIME_GATE, "Travel through time. Swap between&child and adult. Costs 48 magic." },
    { ITEM_BOMB_ARROWS, TEXT_DESC_BOMB_ARROWS,
      "Explosive arrows. Hold C to aim.&Consumes 1 arrow and 1 bomb per shot." },
    { ITEM_ROD_FIRE, TEXT_DESC_FIRE_ROD,
      "Slash=3 fireballs. Stab=long shot.&Jump=flamethrower. Spin=fire AoE.&C-Up to aim." },
    { ITEM_ROD_ICE, TEXT_DESC_ICE_ROD, "Slash=3 iceballs. Stab=long shot.&Jump=ice wave. Spin=ice AoE.&C-Up to aim." },
    { ITEM_ROD_LIGHT, TEXT_DESC_LIGHT_ROD, "Slash=3 orbs. Stab=long shot.&Jump=beam. Spin=light AoE.&C-Up to aim." },
    { ITEM_BEETLE, TEXT_DESC_BEETLE,
      "Launch remote beetle. Steer with&joystick. B=boost. Grabs items and&hits enemies." },
    { ITEM_SHOVEL, TEXT_DESC_SHOVEL, "Dig to uncover grottos, Gold&Skulltulas and graveyard rewards." },
    { ITEM_MINISH_CAP, TEXT_DESC_MINISH_CAP, "Fast travel to 10 pod soil spots.&Kill Gold Skulltulas to unlock them." },
    { ITEM_LANTERN, TEXT_DESC_LANTERN,
      "Swing near fire to catch it. 4 types.&Blue=melts red ice. Green=HP regen.&Poe/Green=free Lens. Swing=fire "
      "dmg." },
    { ITEM_CHATEAU_ROMANI, TEXT_DESC_CHATEAU_ROMANI, "Drink for infinite magic.&One-time consumable." },
    { ITEM_POKEBALL, TEXT_DESC_POKEBALL, "Transform into Pikachu.&Press again to revert." },
};

static const ItemDescEntry sMaskDescs[] = {
    { ITEM_MM_MASK_ALL_NIGHT, TEXT_DESC_MASK_ALL_NIGHT, "Spawns night-only Gold Skulltulas&during daytime." },
    { ITEM_MM_MASK_BLAST, TEXT_DESC_MASK_BLAST, "Press B for instant explosion at&your feet. Has cooldown." },
    { ITEM_MM_MASK_STONE, TEXT_DESC_MASK_STONE, "Enemies ignore you completely." },
    { ITEM_MM_MASK_GREAT_FAIRY, TEXT_DESC_MASK_GREAT_FAIRY,
      "In fountain: A=claim reward.&B=teleport menu between fountains." },
    { ITEM_MM_MASK_DEKU, TEXT_DESC_MASK_DEKU, "Transform into Deku form.&Full moveset from Majora's Mask." },
    { ITEM_MM_MASK_BUNNY, TEXT_DESC_MASK_BUNNY, "Run 1.5x faster." },
    { ITEM_MM_MASK_DON_GERO, TEXT_DESC_MASK_DON_GERO, "At Zora's River frog log: A=collect&all frog rewards at once." },
    { ITEM_MM_MASK_GORON, TEXT_DESC_MASK_GORON, "Transform into Goron form.&Full moveset from Majora's Mask." },
    { ITEM_MM_MASK_ROMANI, TEXT_DESC_MASK_ROMANI, "Get milk from cows without&Epona's Song." },
    { ITEM_MM_MASK_COUPLE, TEXT_DESC_MASK_COUPLE, "Passive regen. Day=HP recovery.&Night=MP recovery." },
    { ITEM_MM_MASK_ZORA, TEXT_DESC_MASK_ZORA, "Transform into Zora form.&Full moveset from Majora's Mask." },
    { ITEM_MM_MASK_KAMARO, TEXT_DESC_MASK_KAMARO, "Hold A to dance. Dance near&Darunia for reward." },
    { ITEM_MM_MASK_CAPTAIN, TEXT_DESC_MASK_CAPTAIN,
      "Spawns Stalchildren (child) or Stalfos&(adult) at night in Hyrule Field." },
    { ITEM_MM_MASK_FIERCE_DEITY, TEXT_DESC_MASK_FIERCE_DEITY,
      "Transform into Fierce Deity form.&Full moveset from Majora's Mask." },
};

static const ItemDescEntry sSw97ArrowDescs[] = {
    { ITEM_SW97_ARROW_FIRE, TEXT_DESC_SW97_ARROW_FIRE, "Fire elemental arrow. 4 MP per shot." },
    { ITEM_SW97_ARROW_ICE, TEXT_DESC_SW97_ARROW_ICE, "Ice elemental arrow. 4 MP per shot." },
    { ITEM_SW97_ARROW_LIGHT, TEXT_DESC_SW97_ARROW_LIGHT, "Light elemental arrow. 8 MP per shot." },
    { ITEM_SW97_ARROW_DARK, TEXT_DESC_SW97_ARROW_DARK, "Dark elemental arrow. 4 MP per shot." },
    { ITEM_SW97_ARROW_SOUL, TEXT_DESC_SW97_ARROW_SOUL, "Soul elemental arrow. 4 MP per shot." },
    { ITEM_SW97_ARROW_WIND, TEXT_DESC_SW97_ARROW_WIND, "Wind elemental arrow. 4 MP per shot." },
};

static const ItemDescEntry sExtEquipDescs[] = {
    { ITEM_EXT_SWORD_1, TEXT_DESC_EXT_BYRNA, "BGS reach. Recover HP+MP on&melee hit." },
    { ITEM_EXT_SWORD_2, TEXT_DESC_EXT_FOUR_SWORD,
      "R+B to charge. Spawns 3 clones&(36 MP). Clones mirror your attacks." },
    { ITEM_EXT_SWORD_3, TEXT_DESC_EXT_IK_AXE, "Hammer attacks. 2x damage, 2x reach.&Slower walk. Hold B to throw." },
    { ITEM_EXT_SHIELD_1, TEXT_DESC_EXT_DIVINE_SHIELD,
      "Fire immune. Block within 10 frames&to stun all nearby enemies." },
    { ITEM_EXT_SHIELD_2, TEXT_DESC_EXT_GERUDO_SCIMITAR, "Surfing shield. (coming soon)" },
    { ITEM_EXT_SHIELD_3, TEXT_DESC_EXT_SHIELD_IKANA,
      "Perfect guard drains enemy HP.&Death save: revive once with 3 hearts." },
    { ITEM_EXT_TUNIC_1, TEXT_DESC_EXT_MAGIC_CAPE, "Ganondorf's cape. Reduces magic&cost by half." },
    { ITEM_EXT_TUNIC_2, TEXT_DESC_EXT_BREASTPLATE,
      "Damage immunity. Costs rupees per&hit. No rupees = slow movement." },
    { ITEM_EXT_TUNIC_3, TEXT_DESC_EXT_CHAMPION_TUNIC,
      "Flurry Rush on dodge. Bullet Time&when aiming in air. 15% world speed." },
    { ITEM_EXT_BOOTS_1, TEXT_DESC_EXT_PEGASUS_ANKLET,
      "Hold B to dash with sword. Wind&barrier drains 1 MP/15 frames." },
    { ITEM_EXT_BOOTS_2, TEXT_DESC_EXT_PENDANT_MEMORIES,
      "Mortal Draw near enemies. Ground&Pound in air. Parry Leap after 3&side hops." },
    { ITEM_EXT_BOOTS_3, TEXT_DESC_EXT_WATER_DRAGON_SCALE, "Zora swim. Barrel roll, dolphin jump.&Adult only." },
};

static const ItemDescEntry sMedallionDescs[] = {
    { ITEM_MEDALLION_FOREST, TEXT_DESC_MEDALLION_FOREST, "Wind spell. 12 MP.&L to switch to arrow mode." },
    { ITEM_MEDALLION_FIRE, TEXT_DESC_MEDALLION_FIRE, "Fire spell. 12 MP.&L to switch to arrow mode." },
    { ITEM_MEDALLION_WATER, TEXT_DESC_MEDALLION_WATER, "Ice spell. 24 MP.&L to switch to arrow mode." },
    { ITEM_MEDALLION_SPIRIT, TEXT_DESC_MEDALLION_SPIRIT, "Soul spell. 24 MP.&L to switch to arrow mode." },
    { ITEM_MEDALLION_SHADOW, TEXT_DESC_MEDALLION_SHADOW, "Dark spell. 12 MP.&L to switch to arrow mode." },
    { ITEM_MEDALLION_LIGHT, TEXT_DESC_MEDALLION_LIGHT, "Light spell. 24 MP.&L to switch to arrow mode." },
};

// ---------------------------------------------------------------------------
// Lookup: item ID + page -> text ID (or 0)
// ---------------------------------------------------------------------------

extern "C" u16 PauseItemDesc_GetTextId(u16 cursorItem, s32 pageIndex) {
    // Custom items + masks + SW97 arrows on ITEM pages
    if (pageIndex == PAUSE_ITEM) {
        for (size_t i = 0; i < ARRAY_COUNT(sCustomItemDescs); i++) {
            if (sCustomItemDescs[i].itemId == cursorItem)
                return sCustomItemDescs[i].textId;
        }
        for (size_t i = 0; i < ARRAY_COUNT(sMaskDescs); i++) {
            if (sMaskDescs[i].itemId == cursorItem)
                return sMaskDescs[i].textId;
        }
        for (size_t i = 0; i < ARRAY_COUNT(sSw97ArrowDescs); i++) {
            if (sSw97ArrowDescs[i].itemId == cursorItem)
                return sSw97ArrowDescs[i].textId;
        }
    }

    // Extended equipment on EQUIP page
    if (pageIndex == PAUSE_EQUIP) {
        for (size_t i = 0; i < ARRAY_COUNT(sExtEquipDescs); i++) {
            if (sExtEquipDescs[i].itemId == cursorItem)
                return sExtEquipDescs[i].textId;
        }
    }

    // SW97 Medallions on QUEST page (only when SW97 enabled)
    if (pageIndex == PAUSE_QUEST && SW97_MEDALLIONS_ENABLED()) {
        for (size_t i = 0; i < ARRAY_COUNT(sMedallionDescs); i++) {
            if (sMedallionDescs[i].itemId == cursorItem)
                return sMedallionDescs[i].textId;
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Message hook: build and load description into font
// ---------------------------------------------------------------------------

static void BuildDescMessage(const char* desc, uint16_t* textId, bool* loadFromMessageTable) {
    CustomMessage msg = CustomMessage(desc, desc, desc);
    msg.Format();
    msg.LoadIntoFont();
    *loadFromMessageTable = false;
}

// All description tables for single-hook lookup
static const ItemDescEntry* sAllDescs[] = {
    sCustomItemDescs, sMaskDescs, sSw97ArrowDescs, sExtEquipDescs, sMedallionDescs,
};
static const size_t sAllDescCounts[] = {
    ARRAY_COUNT(sCustomItemDescs), ARRAY_COUNT(sMaskDescs),      ARRAY_COUNT(sSw97ArrowDescs),
    ARRAY_COUNT(sExtEquipDescs),   ARRAY_COUNT(sMedallionDescs),
};

// Single hook for all descriptions: fires on ANY OnOpenText, checks if textId matches
static void OnOpenTextDescHook(uint16_t* textId, bool* loadFromMessageTable) {
    for (size_t t = 0; t < ARRAY_COUNT(sAllDescs); t++) {
        for (size_t i = 0; i < sAllDescCounts[t]; i++) {
            if (sAllDescs[t][i].textId == *textId) {
                BuildDescMessage(sAllDescs[t][i].desc, textId, loadFromMessageTable);
                return;
            }
        }
    }
}

// Register all description hooks
static void RegisterPauseItemDescriptions() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnOpenText>(OnOpenTextDescHook);
}

static RegisterShipInitFunc initPauseDescs(RegisterPauseItemDescriptions);
