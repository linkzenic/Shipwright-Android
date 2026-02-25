#include "HungerGames.h"
#include <libultraship/libultraship.h>
#include "soh/Enhancements/boss-rush/BossRush.h"
#include "soh/frame_interpolation.h"

#ifdef ENABLE_REMOTE_CONTROL
#include "soh/Network/PvPAnchor/PvPAnchor.h"
#endif

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "soh_assets.h"
#include "overlays/gamestates/ovl_file_choose/file_choose.h"
extern PlayState* gPlayState;
}

// ============================================================================
// File Select Menu Functions
// ============================================================================

void FileChoose_RotateToHungerGames(GameState* thisx) {
    FileChooseContext* fileChoose = (FileChooseContext*)thisx;

    if (fileChoose->configMode == CM_HUNGER_GAMES_TO_QUEST) {
        fileChoose->windowRot -= VREG(16);

        if (fileChoose->windowRot <= 314.0f) {
            fileChoose->windowRot = 314.0f;
            fileChoose->configMode = CM_START_QUEST_MENU;
        }
    } else {
        fileChoose->windowRot += VREG(16);

        if (fileChoose->windowRot >= 628.0f) {
            fileChoose->windowRot = 628.0f;
            fileChoose->configMode = CM_START_HUNGER_GAMES_MENU;
        }
    }
}

void FileChoose_StartHungerGamesMenu(GameState* thisx) {
    FileChooseContext* fileChoose = (FileChooseContext*)thisx;

    fileChoose->logoAlpha -= 25;
    fileChoose->hungerGamesUIAlpha = 0;

    if (fileChoose->logoAlpha <= 0) {
        fileChoose->logoAlpha = 0;
        fileChoose->configMode = CM_HUNGER_GAMES_MENU;
    }
}

void FileChoose_UpdateHungerGamesMenu(GameState* gameState) {
    FileChooseContext* fileChooseContext = (FileChooseContext*)gameState;
    Input* input = &fileChooseContext->state.input[0];

    // Fade in UI elements
    fileChooseContext->hungerGamesUIAlpha += 25;
    if (fileChooseContext->hungerGamesUIAlpha > 255) {
        fileChooseContext->hungerGamesUIAlpha = 255;
    }

    // B = return to quest menu
    if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
        fileChooseContext->configMode = CM_HUNGER_GAMES_TO_QUEST;
        return;
    }

    // A or START = load into game
    if (CHECK_BTN_ALL(input->press.button, BTN_START) || CHECK_BTN_ALL(input->press.button, BTN_A)) {
        Audio_PlaySoundGeneral(NA_SE_SY_FSEL_DECIDE_L, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        fileChooseContext->buttonIndex = HG_BUTTON_INDEX;
        fileChooseContext->menuMode = FS_MENU_MODE_SELECT;
        fileChooseContext->selectMode = SM_FADE_OUT;
        fileChooseContext->prevConfigMode = fileChooseContext->configMode;
        return;
    }
}

void FileChoose_DrawHungerGamesMenuContents(FileChooseContext* fileChooseContext) {
    s16 alpha = fileChooseContext->hungerGamesUIAlpha;

    // Title: "HUNGER GAMES" in red
    Interface_DrawTextLine(fileChooseContext->state.gfxCtx,
        (char*)"HUNGER GAMES", 105, 85, 255, 80, 80, alpha, 1.0f, true);

    // Info lines (0.8f scale, same as Boss Rush)
    Interface_DrawTextLine(fileChooseContext->state.gfxCtx,
        (char*)"PvP Battle Royale", 85, 110, 255, 255, 255, alpha, 0.8f, true);
    Interface_DrawTextLine(fileChooseContext->state.gfxCtx,
        (char*)"Full Inventory - Adult Link", 70, 130, 200, 200, 200, alpha, 0.8f, true);
    Interface_DrawTextLine(fileChooseContext->state.gfxCtx,
        (char*)"Hyrule Field", 100, 150, 200, 200, 200, alpha, 0.8f, true);

    // "Press A to Start" - pulsating alpha
    f32 pulse = 0.5f + 0.5f * sinf(fileChooseContext->arrowAnimTween * 3.14159f * 2.0f);
    s16 pulseAlpha = (s16)(alpha * pulse);
    if (pulseAlpha < 80) pulseAlpha = 80;
    Interface_DrawTextLine(fileChooseContext->state.gfxCtx,
        (char*)"Press A to Start", 90, 175, 255, 200, 50, pulseAlpha, 0.9f, true);
}

// ============================================================================
// Save Initialization - Full Inventory for Testing
// ============================================================================

extern "C" void HungerGames_InitSave(void) {
    gSaveContext.ship.quest.id = QUEST_HUNGER_GAMES;

    // Player name: "HGLink" (using OOT character encoding)
    // H=0x11, G=0x10, L=0x15, i=0x2C, n=0x31, k=0x2E, space=0x3E
    static u8 hgName[] = { 0x11, 0x10, 0x15, 0x2C, 0x31, 0x2E, 0x3E, 0x3E };
    for (int i = 0; i < ARRAY_COUNT(gSaveContext.playerName); i++) {
        gSaveContext.playerName[i] = hgName[i];
    }

    // Adult Link
    gSaveContext.linkAge = LINK_AGE_ADULT;

    // 20 hearts, double magic
    gSaveContext.healthCapacity = 20 * 16;
    gSaveContext.health = 20 * 16;
    gSaveContext.isMagicAcquired = 1;
    gSaveContext.isDoubleMagicAcquired = 1;
    gSaveContext.magicLevel = 2;
    gSaveContext.magic = 96;

    // All items
    u8 items[] = {
        ITEM_STICK,       ITEM_NUT,        ITEM_BOMB,       ITEM_BOW,
        ITEM_ARROW_FIRE,  ITEM_DINS_FIRE,  ITEM_SLINGSHOT,  ITEM_OCARINA_TIME,
        ITEM_BOMBCHU,     ITEM_LONGSHOT,   ITEM_ARROW_ICE,  ITEM_FARORES_WIND,
        ITEM_BOOMERANG,   ITEM_LENS,       ITEM_BEAN,       ITEM_HAMMER,
        ITEM_ARROW_LIGHT, ITEM_NAYRUS_LOVE,ITEM_BOTTLE,     ITEM_POTION_RED,
        ITEM_POTION_GREEN,ITEM_FAIRY,      ITEM_NONE,       ITEM_NONE,
    };
    for (int i = 0; i < ARRAY_COUNT(gSaveContext.inventory.items); i++) {
        gSaveContext.inventory.items[i] = items[i];
    }

    // Max ammo
    s8 ammo[] = { 30, 40, 40, 50, 0, 0, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < ARRAY_COUNT(gSaveContext.inventory.ammo); i++) {
        gSaveContext.inventory.ammo[i] = ammo[i];
    }

    // All equipment
    gSaveContext.inventory.equipment |= (1 << 0);  // Kokiri Sword
    gSaveContext.inventory.equipment |= (1 << 1);  // Master Sword
    gSaveContext.inventory.equipment |= (1 << 2);  // Biggoron Sword
    gSaveContext.inventory.equipment |= (1 << 4);  // Deku Shield
    gSaveContext.inventory.equipment |= (1 << 5);  // Hylian Shield
    gSaveContext.inventory.equipment |= (1 << 6);  // Mirror Shield
    gSaveContext.inventory.equipment |= (1 << 8);  // Kokiri Tunic
    gSaveContext.inventory.equipment |= (1 << 9);  // Goron Tunic
    gSaveContext.inventory.equipment |= (1 << 10); // Zora Tunic
    gSaveContext.inventory.equipment |= (1 << 12); // Kokiri Boots
    gSaveContext.inventory.equipment |= (1 << 13); // Iron Boots
    gSaveContext.inventory.equipment |= (1 << 14); // Hover Boots
    gSaveContext.bgsFlag = 1;

    // Max upgrades
    Inventory_ChangeUpgrade(UPG_QUIVER, 3);
    Inventory_ChangeUpgrade(UPG_BOMB_BAG, 3);
    Inventory_ChangeUpgrade(UPG_BULLET_BAG, 3);
    Inventory_ChangeUpgrade(UPG_STICKS, 3);
    Inventory_ChangeUpgrade(UPG_NUTS, 3);
    Inventory_ChangeUpgrade(UPG_STRENGTH, 3);  // Gold gauntlets
    Inventory_ChangeUpgrade(UPG_SCALE, 2);     // Gold scale

    // Equip: Master Sword, Hylian Shield, Kokiri Tunic, Kokiri Boots
    Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_MASTER);
    Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_HYLIAN);
    Inventory_ChangeEquipment(EQUIP_TYPE_TUNIC, EQUIP_VALUE_TUNIC_KOKIRI);
    Inventory_ChangeEquipment(EQUIP_TYPE_BOOTS, EQUIP_VALUE_BOOTS_KOKIRI);

    // Button items: Sword on B, Bow/Hammer/Bomb on C
    u8 buttonItems[] = { ITEM_SWORD_MASTER, ITEM_BOW, ITEM_HAMMER, ITEM_BOMB,
                         ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE };
    for (int i = 0; i < ARRAY_COUNT(gSaveContext.equips.buttonItems); i++) {
        gSaveContext.equips.buttonItems[i] = buttonItems[i];
    }

    u8 cButtonSlots[] = { SLOT_BOW, SLOT_HAMMER, SLOT_BOMB, SLOT_NONE, SLOT_NONE, SLOT_NONE, SLOT_NONE };
    for (int i = 0; i < ARRAY_COUNT(gSaveContext.equips.cButtonSlots); i++) {
        gSaveContext.equips.cButtonSlots[i] = cButtonSlots[i];
    }

    // Start at Hyrule Field
    gSaveContext.entranceIndex = ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN;

    // Skip boss cutscenes
    gSaveContext.eventChkInf[7] |= 0xFF;
    Flags_SetEventChkInf(EVENTCHKINF_USED_DEKU_TREE_BLUE_WARP);
    Flags_SetEventChkInf(EVENTCHKINF_USED_DODONGOS_CAVERN_BLUE_WARP);
    Flags_SetEventChkInf(EVENTCHKINF_USED_JABU_JABUS_BELLY_BLUE_WARP);
    Flags_SetEventChkInf(EVENTCHKINF_USED_FOREST_TEMPLE_BLUE_WARP);
    Flags_SetEventChkInf(EVENTCHKINF_USED_FIRE_TEMPLE_BLUE_WARP);
    Flags_SetEventChkInf(EVENTCHKINF_USED_WATER_TEMPLE_BLUE_WARP);
}

// ============================================================================
// Gameplay Hooks
// ============================================================================

extern "C" void HungerGames_OnPlayInit(PlayState* play) {
#ifdef ENABLE_REMOTE_CONTROL
    if (IS_HUNGER_GAMES && PvPAnchor::Instance && !PvPAnchor::Instance->isConnected) {
        PvPAnchor::Instance->Enable();
    }
#endif
}
