/**
 * sw97_scene_table.c - SW97 scene table and entrance definitions
 *
 * Original: z64proto/sw97 team (Spaceworld '97 Experience)
 * Adapted for Ship of Harkinian (Shipwright)
 *
 * SW97 has 53 scenes with a completely different scene table from OOT.
 * Scene IDs 0x00-0x34 map to unique SW97 scenes (dungeons, overworld,
 * interiors, test rooms).
 *
 * For the initial port, tours use OOT scenes as fallbacks.
 * When inline scene data is added (Phase 7 complete), this table
 * will route to Sw97_LoadScene() for custom scene loading.
 */

// SW97 Scene IDs (from references/sw97/include/z64scene.h)
typedef enum {
    SW97_SCENE_FSTDAN = 0x00,
    SW97_SCENE_DODONGOS_CAVERN = 0x01,
    SW97_SCENE_SYOTES_OLD = 0x02,
    SW97_SCENE_SYOTES2_OLD = 0x03,
    SW97_SCENE_TEST_MAP = 0x04,
    SW97_SCENE_UNFINISHED_DEKU = 0x05,
    SW97_SCENE_UNFINISHED_GOHMA = 0x06,
    SW97_SCENE_OLD_DEPTH_TEST = 0x07,
    SW97_SCENE_I_SHOP = 0x08,
    SW97_SCENE_HYRULE_FIELD = 0x09,
    SW97_SCENE_OLD_KAKARIKO = 0x0A,
    SW97_SCENE_OLD_GRAVEYARD = 0x0B,
    SW97_SCENE_OLD_LOST_WOODS = 0x0C,
    SW97_SCENE_KOKIRI_FOREST = 0x0D,
    SW97_SCENE_OLD_SFM = 0x0E,
    SW97_SCENE_OLD_LAKE_HYLIA = 0x0F,
    SW97_SCENE_OLD_ZORAS_RIVER = 0x10,
    SW97_SCENE_OLD_POND = 0x11,
    SW97_SCENE_GERUDO_VALLEY = 0x12,
    SW97_SCENE_HYRULE_CASTLE = 0x13,
    SW97_SCENE_DEATH_MT_TRAIL = 0x14,
    SW97_SCENE_DEATH_MT_CRATER = 0x15,
    SW97_SCENE_UNK_16 = 0x16,
    SW97_SCENE_UNK_17 = 0x17,
    SW97_SCENE_PR_MARKET_1 = 0x18,
    SW97_SCENE_PR_MARKET_2 = 0x19,
    SW97_SCENE_FIRE_TEMPLE = 0x1A,
    SW97_SCENE_FOREST_TEMPLE = 0x1B,
    SW97_SCENE_ARCHERY = 0x1C,
    SW97_SCENE_OLD_SASATEST = 0x1D,
    SW97_SCENE_PR_BEHIND_TOT = 0x1E,
    SW97_SCENE_OLD_TESTROOM = 0x1F,
    SW97_SCENE_DEKU_TREE = 0x20,
    SW97_SCENE_JABU_TEST = 0x21,
    SW97_SCENE_CHAMBER_SAGES = 0x22,
    SW97_SCENE_PR_OUTSIDE_TOT = 0x23,
    SW97_SCENE_FAIRY_FOUNTAIN = 0x24,
    SW97_SCENE_TEMPLE_OF_TIME = 0x25,
    SW97_SCENE_UNF_FOREST_TMP = 0x26,
    SW97_SCENE_LOD_TEST = 0x27,
    SW97_SCENE_OLD_SUTARU = 0x28,
    SW97_SCENE_UNF_FIRE_TEMPLE = 0x29,
    SW97_SCENE_PR_LINKS_HOUSE = 0x2A,
    SW97_SCENE_PR_KOKIRI_1 = 0x2B,
    SW97_SCENE_UNK_2C = 0x2C,
    SW97_SCENE_OLD_HYRULE_FIELD = 0x2D,
    SW97_SCENE_UNK_2E = 0x2E,
    SW97_SCENE_WATER_TEMPLE = 0x2F,
    SW97_SCENE_PR_KOKIRI_2 = 0x30,
    SW97_SCENE_GROTTOS = 0x31,
    SW97_SCENE_POE_RACE = 0x32,
    SW97_SCENE_SPECIAL_COURSE = 0x32,
    SW97_SCENE_UNK_33 = 0x33,
    SW97_SCENE_MAX = 0x34,
} Sw97SceneId;

/**
 * Map SW97 scene IDs to the closest OOT scene for fallback loading.
 * Returns the OOT scene entrance index, or -1 if no mapping exists.
 *
 * When inline scene data is added, this function will be replaced with
 * Sw97_LoadScene() that reads from compiled C struct data.
 */
static s32 Sw97_GetOotEntrance(s32 sw97SceneId, s32 spawnIndex) {
    switch (sw97SceneId) {
        case SW97_SCENE_HYRULE_FIELD:
            return 0x01FD;
        case SW97_SCENE_KOKIRI_FOREST:
            return 0x00EE;
        case SW97_SCENE_HYRULE_CASTLE:
            return 0x025A;
        case SW97_SCENE_DEATH_MT_TRAIL:
            return 0x013D;
        case SW97_SCENE_DEATH_MT_CRATER:
            return 0x014D;
        case SW97_SCENE_GERUDO_VALLEY:
            return 0x0117;
        case SW97_SCENE_DEKU_TREE:
            return 0x0000;
        case SW97_SCENE_DODONGOS_CAVERN:
            return 0x0004;
        case SW97_SCENE_FIRE_TEMPLE:
            return 0x0165;
        case SW97_SCENE_FOREST_TEMPLE:
            return 0x0169;
        case SW97_SCENE_WATER_TEMPLE:
            return 0x0010;
        case SW97_SCENE_TEMPLE_OF_TIME:
            return 0x0053;
        case SW97_SCENE_PR_LINKS_HOUSE:
            return 0x00BB;
        case SW97_SCENE_CHAMBER_SAGES:
            return 0x006B;
        case SW97_SCENE_FAIRY_FOUNTAIN:
            return 0x036D;
        default:
            return -1;
    }
}
