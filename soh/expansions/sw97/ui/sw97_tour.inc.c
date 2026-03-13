/**
 * sw97_tour.c - SW97 Tour system (simplified for OOT scene fallback)
 *
 * Original: z64proto/sw97 team (Spaceworld '97 Experience)
 * Adapted for Ship of Harkinian (Shipwright)
 *
 * The full SW97 tour system replaces the file select with a custom
 * tour menu (Hyrule Tour, Dungeon Tour, Battle Tour, Extras).
 * This simplified version provides a quick-start function that
 * sets up a SW97 tour using OOT's existing scenes.
 *
 * Tour options and their OOT scene mappings:
 *
 * HYRULE TOUR:
 *   0: Link's House    → SCENE_LINKS_HOUSE (0x34)
 *   1: Hyrule Castle   → SCENE_HYRULE_CASTLE (0x5F)
 *   2: Hyrule Field    → SCENE_HYRULE_FIELD (0x51), adult on horse
 *   3: Death Mountain   → SCENE_DEATH_MOUNTAIN_TRAIL (0x60)
 *
 * DUNGEON TOUR:
 *   0: Deku Tree       → SCENE_DEKU_TREE (0x00)
 *   1: Dodongo's Cavern→ SCENE_DODONGOS_CAVERN (0x01)
 *   2: Special Course  → SCENE_GANONS_TOWER (0x0A), adult
 *
 * BATTLE TOUR:
 *   0: Gohma Boss      → SCENE_DEKU_TREE_BOSS (0x11)
 *   1: King Dodongo    → SCENE_DODONGOS_CAVERN_BOSS (0x12)
 *   2: Old Sutaru      → SCENE_INSIDE_GANONS_CASTLE (0x0D), adult
 */

// These are OOT scene IDs from SOH's z64scene.h
// We map SW97 tour options to the closest matching OOT scene

typedef enum {
    SW97_TOUR_HYRULE,
    SW97_TOUR_DUNGEON,
    SW97_TOUR_BATTLE,
    SW97_TOUR_EXTRAS,
} Sw97TourType;

// Entrance indices for OOT scenes (from SOH's entrance table)
// Format: scene entrance index from gEntranceTable
#define SW97_ENTRANCE_LINKS_HOUSE 0x00BB
#define SW97_ENTRANCE_HYRULE_CASTLE 0x025A
#define SW97_ENTRANCE_HYRULE_FIELD 0x01FD
#define SW97_ENTRANCE_DEATH_MT_TRAIL 0x013D
#define SW97_ENTRANCE_DEKU_TREE 0x0000
#define SW97_ENTRANCE_DODONGOS_CAVERN 0x0004
#define SW97_ENTRANCE_GANONS_TOWER 0x041B
#define SW97_ENTRANCE_DEKU_TREE_BOSS 0x040F
#define SW97_ENTRANCE_DODONGO_BOSS 0x040B
#define SW97_ENTRANCE_GANONS_CASTLE 0x0467

/**
 * Start a SW97 tour by configuring gSaveContext and setting the entrance.
 * This is called when the user selects a tour option.
 *
 * tourType: SW97_TOUR_HYRULE, SW97_TOUR_DUNGEON, SW97_TOUR_BATTLE
 * option: sub-menu index (0-3 for Hyrule, 0-2 for Dungeon/Battle)
 */
void Sw97_StartTour(s32 tourType, s32 option) {
    // Initialize common save context
    gSaveContext.gameMode = 0;
    gSaveContext.cutsceneIndex = 0xFFEF;
    gSaveContext.dayTime = 0x8000;
    gSaveContext.magicLevel = gSaveContext.magic = 0;
    gSaveContext.respawnFlag = 0;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].entranceIndex = -1;
    gSaveContext.seqId = (u8)NA_BGM_DISABLED;
    gSaveContext.natureAmbienceId = 0xFF;
    gSaveContext.showTitleCard = false;

    // Enable all buttons
    for (s32 i = 0; i < 5; i++) {
        gSaveContext.buttonStatus[i] = BTN_ENABLED;
    }

    switch (tourType) {
        case SW97_TOUR_HYRULE:
            switch (option) {
                case 0: // Link's House
                    Sw97_InitSave(SW97_PRESET_CHILD_DEFAULT);
                    gSaveContext.entranceIndex = SW97_ENTRANCE_LINKS_HOUSE;
                    break;
                case 1: // Hyrule Castle
                    Sw97_InitSave(SW97_PRESET_CHILD_CASTLE);
                    gSaveContext.entranceIndex = SW97_ENTRANCE_HYRULE_CASTLE;
                    break;
                case 2: // Hyrule Field (adult, on horse)
                    Sw97_InitSave(SW97_PRESET_ADULT_DEFAULT);
                    gSaveContext.entranceIndex = SW97_ENTRANCE_HYRULE_FIELD;
                    break;
                case 3: // Death Mountain Trail
                    Sw97_InitSave(SW97_PRESET_CHILD_DC_DMT);
                    gSaveContext.entranceIndex = SW97_ENTRANCE_DEATH_MT_TRAIL;
                    break;
            }
            break;

        case SW97_TOUR_DUNGEON:
            switch (option) {
                case 0: // Deku Tree
                    Sw97_InitSave(SW97_PRESET_DEKU_TREE);
                    gSaveContext.entranceIndex = SW97_ENTRANCE_DEKU_TREE;
                    break;
                case 1: // Dodongo's Cavern
                    Sw97_InitSave(SW97_PRESET_CHILD_DC_DMT);
                    gSaveContext.entranceIndex = SW97_ENTRANCE_DODONGOS_CAVERN;
                    break;
                case 2: // Special Course (adult)
                    Sw97_InitSave(SW97_PRESET_ADULT_DEFAULT);
                    gSaveContext.entranceIndex = SW97_ENTRANCE_GANONS_TOWER;
                    break;
            }
            break;

        case SW97_TOUR_BATTLE:
            switch (option) {
                case 0: // Gohma Boss
                    Sw97_InitSave(SW97_PRESET_CHILD_GOHMA);
                    gSaveContext.entranceIndex = SW97_ENTRANCE_DEKU_TREE_BOSS;
                    break;
                case 1: // King Dodongo
                    Sw97_InitSave(SW97_PRESET_CHILD_KD);
                    gSaveContext.entranceIndex = SW97_ENTRANCE_DODONGO_BOSS;
                    break;
                case 2: // Old Sutaru → Ganon's Castle (closest OOT match)
                    Sw97_InitSave(SW97_PRESET_ADULT_DEFAULT);
                    gSaveContext.entranceIndex = SW97_ENTRANCE_GANONS_CASTLE;
                    break;
            }
            break;
    }
}
