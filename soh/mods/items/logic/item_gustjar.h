/**
 * Gust Jar Item Header
 * Absorb mode (pull + damage) + Blow mode (elemental cone push)
 */

#ifndef ITEM_GUSTJAR_H
#define ITEM_GUSTJAR_H

#include "z64.h"
#include "../custom_items.h"

// Range
#define GUST_RANGE_MAX 220.0f
#define GUST_RANGE_BLOW 200.0f
#define GUST_BLOW_CONE_HALF_ANGLE 0x2000 // ~45 degrees in s16
#define LINK_HEIGHT_HITBOX 80.0f

// Timers
#define GUST_HEAT_MAX 300      // 10 seconds to overheat (absorb → blow)
#define GUST_BLOW_DURATION 300 // 10 seconds of blow
#define GUST_COOLDOWN 120      // 2 seconds after blow ends

// Modes
#define GUST_MODE_OFF 0
#define GUST_MODE_IDLE 1
#define GUST_MODE_ABSORB 2
#define GUST_MODE_BLOW 3
#define GUST_MODE_ELEMENT_SELECT 4

// Element types (selected from medallions)
typedef enum {
    GUST_ELEMENT_WIND = 0,   // Default (no medallion needed, Forest)
    GUST_ELEMENT_FIRE = 1,   // Fire Medallion
    GUST_ELEMENT_ICE = 2,    // Water Medallion
    GUST_ELEMENT_SHADOW = 3, // Shadow Medallion
    GUST_ELEMENT_SPIRIT = 4, // Spirit Medallion
    GUST_ELEMENT_LIGHT = 5,  // Light Medallion
    GUST_ELEMENT_COUNT = 6,
} GustJarElement;

// Actor IDs
#ifndef ACTOR_EN_SW
#define ACTOR_EN_SW 0x0095
#endif
#ifndef ACTOR_EN_SI
#define ACTOR_EN_SI 0x0096
#endif

// State aliases
#define gjEquipped gCustomItemState.gustJarEquipped
#define gjMode gCustomItemState.gustJarMode
#define gjElement gCustomItemState.gustJarElement
#define gjBlowActive gCustomItemState.gustJarBlowActive
#define gjHeatTimer gCustomItemState.gustJarHeatTimer
#define gjBlowTimer gCustomItemState.gustJarBlowTimer
#define gjCooldownTimer gCustomItemState.gustJarCooldownTimer
#define gjTimer gCustomItemState.gustJarTimer
#define gjCollider gCustomItemState.gustJarCollider
#define gjFirstPerson gCustomItemState.gustJarFirstPersonActive
#define gjAimMode gCustomItemState.gustJarAimMode
#define gjButtonMask gCustomItemState.gustJarButtonMask

// Collider init — DMG_HAMMER_SWING (bit 6) | DMG_EXPLOSIVE (bit 3) = 0x48
// This triggers AC_HIT on pots, rocks, grass, crates via their own break handlers
static ColliderCylinderInit sGustJarColliderInit = { { COLTYPE_NONE, AT_ON | AT_TYPE_PLAYER, AC_NONE, OC1_NONE,
                                                       OC2_TYPE_PLAYER, COLSHAPE_CYLINDER },
                                                     { ELEMTYPE_UNK0,
                                                       { 0x00000048, 0x00, 0x08 },
                                                       { 0x00000000, 0x00, 0x00 },
                                                       TOUCH_ON | TOUCH_SFX_NONE,
                                                       BUMP_NONE,
                                                       OCELEM_NONE },
                                                     { 30, 30, 0, { 0, 0, 0 } } };

// Suckable prop actor IDs (for attraction loop)
static const s16 sGustSuckableProps[] = {
    ACTOR_OBJ_TSUBO,  // Pots
    ACTOR_OBJ_KIBAKO, // Small crates
    ACTOR_EN_KUSA,    // Grass/bushes
    ACTOR_EN_ISHI,    // Rocks (small only)
};
#define GUST_SUCKABLE_PROP_COUNT (sizeof(sGustSuckableProps) / sizeof(sGustSuckableProps[0]))

// Suckable enemy actor IDs (small/medium enemies that get pulled in by absorb)
static const s16 sGustSuckableEnemies[] = {
    0x0013, // Keese (all types: normal, fire, ice)
    0x001B, // Tektite
    0x0037, // Skulltula
    0x0095, // Skullwalltula
    0x0034, // Biri (small jellyfish)
    0x002D, // Bubble/Shabom
    0x0069, // Fire/Ice Skull (En_Bb)
    0x002B, // Gohma Larva
    0x002F, // Baby Dodongo
    0x0055, // Deku Baba
    0x0060, // Mad Scrub / Deku Scrub
    0x0038, // Torch Slug
    0x003A, // Stinger
    0x00C5, // Shell Blade
    0x006B, // Flying Floor Tile
    0x001C, // Leever
    0x01C0, // Guay (crow)
    0x01B0, // Stalchild
    0x00EC, // Spike (En_Ny)
    0x000D, // Poe
};
#define GUST_SUCKABLE_ENEMY_COUNT (sizeof(sGustSuckableEnemies) / sizeof(sGustSuckableEnemies[0]))

// Element colors for blow cone VFX
typedef struct {
    Color_RGBA8 prim;
    Color_RGBA8 env;
} GustElementColor;

static const GustElementColor sGustElementColors[GUST_ELEMENT_COUNT] = {
    [GUST_ELEMENT_WIND] = { { 200, 200, 200, 255 }, { 255, 255, 255, 200 } },
    [GUST_ELEMENT_FIRE] = { { 255, 80, 0, 255 }, { 255, 200, 0, 200 } },
    [GUST_ELEMENT_ICE] = { { 80, 180, 255, 255 }, { 150, 220, 255, 200 } },
    [GUST_ELEMENT_SHADOW] = { { 130, 50, 180, 255 }, { 80, 0, 130, 200 } },
    [GUST_ELEMENT_SPIRIT] = { { 255, 150, 50, 255 }, { 255, 200, 100, 200 } },
    [GUST_ELEMENT_LIGHT] = { { 255, 255, 100, 255 }, { 255, 255, 200, 200 } },
};

// Medallion quest items mapped to elements
static const struct {
    s32 questItem;
    u8 element;
} sGustMedallionMap[GUST_ELEMENT_COUNT] = {
    { QUEST_MEDALLION_FOREST, GUST_ELEMENT_WIND },   { QUEST_MEDALLION_FIRE, GUST_ELEMENT_FIRE },
    { QUEST_MEDALLION_WATER, GUST_ELEMENT_ICE },     { QUEST_MEDALLION_SHADOW, GUST_ELEMENT_SHADOW },
    { QUEST_MEDALLION_SPIRIT, GUST_ELEMENT_SPIRIT }, { QUEST_MEDALLION_LIGHT, GUST_ELEMENT_LIGHT },
};

// Particle spawn helpers — exposed so the Harpoon dummy update can replay the
// suck/blow cones for remote players (the local Update path that normally
// spawns these never runs for a dummy). This header is C-only (designated
// initializers above), so C++ consumers must forward-declare these directly
// rather than #include this file.
void GustJar_SpawnSuckVFX(PlayState* play, Vec3f* nozzle, s16 aimYaw);
void GustJar_SpawnBlowVFX(PlayState* play, Vec3f* nozzle, s16 aimYaw, u8 element);

#endif // ITEM_GUSTJAR_H
