/**
 * ext_equip_behavior.c - Behavior handlers for extended equipment
 *
 * Each equipment piece will eventually have its own behavior handler.
 * For now, all are no-op stubs.
 *
 * Included by extended_equipment.c (unity build).
 */

#include "z64player.h"
#include "z64.h"

// ---------------------------------------------------------------------------
// Sword behaviors (stubs)
// ---------------------------------------------------------------------------
static void ExtEquip_Behavior_Sword1(Player* player, PlayState* play) {
    (void)player;
    (void)play;
    // TODO: Implement Ext Sword 1 behavior
}

static void ExtEquip_Behavior_Sword2(Player* player, PlayState* play) {
    (void)player;
    (void)play;
    // TODO: Implement Ext Sword 2 behavior
}

static void ExtEquip_Behavior_Sword3(Player* player, PlayState* play) {
    (void)player;
    (void)play;
    // TODO: Implement Ext Sword 3 behavior
}

// ---------------------------------------------------------------------------
// Shield behaviors (stubs)
// ---------------------------------------------------------------------------
static void ExtEquip_Behavior_Shield1(Player* player, PlayState* play) {
    (void)player;
    (void)play;
}

static void ExtEquip_Behavior_Shield2(Player* player, PlayState* play) {
    (void)player;
    (void)play;
}

static void ExtEquip_Behavior_Shield3(Player* player, PlayState* play) {
    (void)player;
    (void)play;
}

// ---------------------------------------------------------------------------
// Tunic behaviors (stubs)
// ---------------------------------------------------------------------------
static void ExtEquip_Behavior_Tunic1(Player* player, PlayState* play) {
    (void)player;
    (void)play;
}

static void ExtEquip_Behavior_Tunic2(Player* player, PlayState* play) {
    (void)player;
    (void)play;
}

static void ExtEquip_Behavior_Tunic3(Player* player, PlayState* play) {
    (void)player;
    (void)play;
}

// ---------------------------------------------------------------------------
// Boots behaviors (stubs)
// ---------------------------------------------------------------------------
static void ExtEquip_Behavior_Boots1(Player* player, PlayState* play) {
    (void)player;
    (void)play;
}

static void ExtEquip_Behavior_Boots2(Player* player, PlayState* play) {
    (void)player;
    (void)play;
}

static void ExtEquip_Behavior_Boots3(Player* player, PlayState* play) {
    (void)player;
    (void)play;
}

// ---------------------------------------------------------------------------
// Behavior dispatch tables
// ---------------------------------------------------------------------------
typedef void (*ExtEquipBehaviorFunc)(Player*, PlayState*);

static const ExtEquipBehaviorFunc sExtSwordBehaviors[3] = {
    ExtEquip_Behavior_Sword1,
    ExtEquip_Behavior_Sword2,
    ExtEquip_Behavior_Sword3,
};

static const ExtEquipBehaviorFunc sExtShieldBehaviors[3] = {
    ExtEquip_Behavior_Shield1,
    ExtEquip_Behavior_Shield2,
    ExtEquip_Behavior_Shield3,
};

static const ExtEquipBehaviorFunc sExtTunicBehaviors[3] = {
    ExtEquip_Behavior_Tunic1,
    ExtEquip_Behavior_Tunic2,
    ExtEquip_Behavior_Tunic3,
};

static const ExtEquipBehaviorFunc sExtBootsBehaviors[3] = {
    ExtEquip_Behavior_Boots1,
    ExtEquip_Behavior_Boots2,
    ExtEquip_Behavior_Boots3,
};

static void ExtEquip_DispatchBehavior(Player* player, PlayState* play) {
    if (gExtEquipState.currentExtSword > 0 && gExtEquipState.currentExtSword <= 3) {
        sExtSwordBehaviors[gExtEquipState.currentExtSword - 1](player, play);
    }
    if (gExtEquipState.currentExtShield > 0 && gExtEquipState.currentExtShield <= 3) {
        sExtShieldBehaviors[gExtEquipState.currentExtShield - 1](player, play);
    }
    if (gExtEquipState.currentExtTunic > 0 && gExtEquipState.currentExtTunic <= 3) {
        sExtTunicBehaviors[gExtEquipState.currentExtTunic - 1](player, play);
    }
    if (gExtEquipState.currentExtBoots > 0 && gExtEquipState.currentExtBoots <= 3) {
        sExtBootsBehaviors[gExtEquipState.currentExtBoots - 1](player, play);
    }
}
