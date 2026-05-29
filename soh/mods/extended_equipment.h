/**
 * extended_equipment.h - Extended equipment system (cheat)
 *
 * Adds 12 new equipment pieces (3 swords, 3 shields, 3 tunics, 3 boots)
 * accessible via L button on the pause menu equipment page.
 * All extended equipment is "owned" when the cheat CVar is enabled.
 *
 * Page switching: Press L on equipment screen to toggle vanilla/extended.
 */
#ifndef EXTENDED_EQUIPMENT_H
#define EXTENDED_EQUIPMENT_H

#include <libultraship/libultra.h>
#include "z64item.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// CVar keys
// ---------------------------------------------------------------------------
#define CVAR_EXT_EQUIP_ENABLED "gCheats.ExtEquip.Enabled"
// Extended equipment ownership bits in upper 16 of inventory.equipment
// Bit = 16 + equipType*3 + (index-1)
#define EXT_EQUIP_OWNED_SHIFT 16

// ---------------------------------------------------------------------------
// Extended equipment item IDs (for icon/name lookup, NOT stored in inventory)
// ---------------------------------------------------------------------------
#define ITEM_EXT_SWORD_1 0xE0
#define ITEM_EXT_SWORD_2 0xE1
#define ITEM_EXT_SWORD_3 0xE2
#define ITEM_EXT_SHIELD_1 0xE3
#define ITEM_EXT_SHIELD_2 0xE4
#define ITEM_EXT_SHIELD_3 0xE5
#define ITEM_EXT_TUNIC_1 0xE6
#define ITEM_EXT_TUNIC_2 0xE7
#define ITEM_EXT_TUNIC_3 0xE8
#define ITEM_EXT_BOOTS_1 0xE9
#define ITEM_EXT_BOOTS_2 0xEA
#define ITEM_EXT_BOOTS_3 0xEB

// ---------------------------------------------------------------------------
// Extended equipment indices (1-based, 0 = none)
// ---------------------------------------------------------------------------
typedef enum { EXT_EQUIP_NONE = 0, EXT_EQUIP_1 = 1, EXT_EQUIP_2 = 2, EXT_EQUIP_3 = 3, EXT_EQUIP_MAX = 4 } ExtEquipIndex;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
typedef struct {
    int equipPage;       // 0 = vanilla, 1 = extended
    s16 pageSwitchTimer; // Cooldown (15 frames)
    u8 currentExtSword;  // 0=none, 1-3=ext sword
    u8 currentExtShield; // 0=none, 1-3=ext shield
    u8 currentExtTunic;  // 0=none, 1-3=ext tunic
    u8 currentExtBoots;  // 0=none, 1-3=ext boots
} ExtendedEquipmentState;

extern ExtendedEquipmentState gExtEquipState;

// ---------------------------------------------------------------------------
// Page management
// ---------------------------------------------------------------------------

/** Initialize state from CVars */
void ExtEquip_Init(void);

/** Update per frame (cooldown timer) */
void ExtEquip_Update(void);

/** @return Current equipment page (0=vanilla, 1=extended) */
int ExtEquip_GetPage(void);

// ---------------------------------------------------------------------------
// Age requirements (per ext equipment piece)
// ---------------------------------------------------------------------------

/** @return Age requirement value (AGE_REQ_NONE=9, AGE_REQ_ADULT=0, AGE_REQ_CHILD=1) */
u8 ExtEquip_GetAgeReq(s16 equipType, u8 index);

/** @return 1 if Link's current age satisfies the requirement, 0 otherwise */
u8 ExtEquip_CheckAgeReq(s16 equipType, u8 index);

/** Toggle between vanilla and extended page */
void ExtEquip_SwitchPage(void);

/** @return true if page switch cooldown elapsed */
u8 ExtEquip_CanSwitch(void);

/** @return true if the extra equipment cheat is enabled */
u8 ExtEquip_IsEnabled(void);

// ---------------------------------------------------------------------------
// Equip / Unequip
// ---------------------------------------------------------------------------

/**
 * Equip an extended equipment piece.
 * @param equipType EQUIP_TYPE_SWORD/SHIELD/TUNIC/BOOTS
 * @param index     1-3 (ext equipment index)
 */
void ExtEquip_Equip(s16 equipType, u8 index);

/**
 * Unequip extended equipment of a given type (set to 0).
 * Called when vanilla equipment is equipped.
 * @param equipType EQUIP_TYPE_SWORD/SHIELD/TUNIC/BOOTS
 */
void ExtEquip_Unequip(s16 equipType);

/**
 * @param equipType EQUIP_TYPE_SWORD/SHIELD/TUNIC/BOOTS
 * @return Current extended equipment index (0=none, 1-3=equipped)
 */
u8 ExtEquip_GetCurrent(s16 equipType);

// ---------------------------------------------------------------------------
// Ownership
// ---------------------------------------------------------------------------

/** @return true if the player owns this extended equipment piece */
u8 ExtEquip_HasItem(s16 equipType, u8 index);

/** Give the player an extended equipment piece */
void ExtEquip_GiveItem(s16 equipType, u8 index);

/** Remove an extended equipment piece from the player */
void ExtEquip_RemoveItem(s16 equipType, u8 index);

// ---------------------------------------------------------------------------
// Icons / Names
// ---------------------------------------------------------------------------

/**
 * Get icon texture for an extended equipment item.
 * @param equipType EQUIP_TYPE_SWORD/SHIELD/TUNIC/BOOTS
 * @param index     1-3
 * @return Pointer to 32x32 RGBA32 texture data
 */
void* ExtEquip_GetIcon(s16 equipType, u8 index);

/**
 * Get the extended item ID for a given equipment type and index.
 * @param equipType EQUIP_TYPE_SWORD/SHIELD/TUNIC/BOOTS
 * @param index     1-3
 * @return ITEM_EXT_xxx constant
 */
u16 ExtEquip_GetItemId(s16 equipType, u8 index);

/**
 * Toggle an extended equipment item from a C button press.
 * If the item's equipment type is already equipped with this index, unequip it.
 * Otherwise, equip it.
 * @param itemId ITEM_EXT_xxx constant (0xE0-0xEB)
 */
void ExtEquip_ToggleFromCButton(u16 itemId);

/**
 * Get name texture for an extended equipment item.
 * @param itemId ITEM_EXT_xxx constant
 * @param language Language index
 * @return Pointer to name texture, or NULL for placeholder
 */
void* ExtEquip_GetNameTex(u16 itemId, u8 language);

// ---------------------------------------------------------------------------
// Transform integration
// ---------------------------------------------------------------------------

/** Backup current ext equip state and unequip all. Called on transformation. */
void ExtEquip_UnequipForTransform(void);

/** Restore ext equip from backup. Called on detransformation to human. */
void ExtEquip_RestoreFromTransform(void);

/** Discard backup without restoring. Called on reset/reload/death. */
void ExtEquip_ClearTransformBackup(void);

// ---------------------------------------------------------------------------
// Divine Shield helpers (called from z_player_lib.c and z_player.c)
// ---------------------------------------------------------------------------
u8 DivineShield_IsWoodType(void);
u8 DivineShield_IsFireproof(void);
void DivineShield_OnShieldBlock(Player* player, PlayState* play);

// ---------------------------------------------------------------------------
// Shield of Ikana helpers (called from z_player.c at the bounce-detection point)
// ---------------------------------------------------------------------------
void Ikana_OnShieldBlock(Player* player, PlayState* play);

// ---------------------------------------------------------------------------
// Behavior state
// ---------------------------------------------------------------------------

typedef enum {
    PEGASUS_IDLE,
    PEGASUS_WINDUP,
    PEGASUS_RUNNING,
    PEGASUS_BONK,
} PegasusState;

typedef enum {
    DSCALE_INACTIVE,
    DSCALE_SWIMMING,
} DragonScaleState;

typedef struct {
    Vec3f offset; // relative offset from player world pos (set at spawn, Y = 0 keeps same ground height)
    u8 alive;     // 1 = active, 0 = dead / not spawned
} FourSwordClone;

typedef struct {
    // Cane of Byrna (Ext Sword 1)
    u8 byrnaSavedSwordEquip; // Original equips.equipment sword nibble
    u8 byrnaSavedButtonItem; // Original equips.buttonItems[0]
    f32 byrnaSavedSwordHealth; // Original swordHealth (GK durability)
    u8 byrnaSavedBgsFlag;      // Original bgsFlag (1=BGS, 0=GK)
    u8 byrnaActive;          // Whether Byrna has overridden sword state

    // Pegasus Anklet
    u8 pegasusState;
    s16 pegasusTimer;
    s16 pegasusMagicTick;
    u8 pegasusColInit;
    f32 pegasusWingAngle; // Pendulum angle for wing charm (radians)
    f32 pegasusWingVel;   // Pendulum angular velocity

    // Water Dragon Scale
    u8 dragonScaleState;
    s16 dragonScalePitch; // swim pitch angle
    s16 dragonScaleMagicTick;
    u8 dragonScaleColInit;

    // Iron Knuckle Axe (Ext Sword 3)
    u8 ikAxeSavedSwordEquip;
    u8 ikAxeSavedButtonItem;
    u8 ikAxeActive;
    u8 ikAxeDrawing; // 1 when hammer is out (hide vanilla sword DL), 0 in free mode

    // Four Sword (Ext Sword 2)
    u8 fourSwordSavedSwordEquip; // Original equips.equipment sword nibble
    u8 fourSwordSavedButtonItem; // Original equips.buttonItems[0]
    u8 fourSwordActive;                // pak loader is live
    s16 fourSwordBHoldTimer;           // frames B has been held while shielding
    u8 fourSwordCharging;              // 1 while charge is armed (B+shield >= threshold)
    u8 fourSwordCloneCount;            // number of currently alive clones (0-3)
    FourSwordClone fourSwordClones[3]; // per-clone data
    u8 fourSwordColInit;               // bitmask: bit i = colliders for clone i are initialised

    // Four Sword: rising-edge detection for Ivan-style item spawn
    u8 fourSwordPrevA73;       // previous player->unk_A73 (arrow/boomerang fire)
    u8 fourSwordPrevCarrying;  // previous PLAYER_STATE1_CARRYING_ACTOR bit
    u8 fourSwordPrevBoomerang; // previous (player->boomerangActor != NULL)
    s16 fourSwordItemCooldown; // global cooldown prevents actor spam (10 frames)
} ExtEquipBehaviorState;

extern ExtEquipBehaviorState gExtEquipBehavior;

// Champion's Tunic slow factor — 1.0f normal, 0.15f during Flurry Rush / Bullet Time
// Used in z_actor.c Actor_UpdatePos to scale non-player actor movement.
extern f32 gChampionSlowFactor;

// ---------------------------------------------------------------------------
// Behavior
// ---------------------------------------------------------------------------

/**
 * Called per frame from Player_Update when extended equipment is active.
 * Dispatches to individual behavior handlers.
 */
void ExtEquip_UpdateBehavior(void* player, void* play);

/**
 * Called from z_player.c when melee weapon quads register a hit (AT_HIT).
 * Used by Cane of Byrna for MP recovery.
 */
void ExtEquip_OnMeleeHit(void* player, void* play);

/**
 * Called from z_player.c draw section for equipment-specific visuals
 * (barriers, auras, etc.).
 */
void ExtEquip_DrawBehavior(void* player, void* play);

/**
 * Returns 1 if the vanilla sword DL should be hidden (replaced by ext equipment draw).
 * Called from z_player_lib.c in the limb draw callback.
 */
u8 ExtEquip_ShouldHideSwordDL(void);

/**
 * Returns the MM Mirror Shield OTR path if Shield of Ikana is equipped, NULL otherwise.
 * Called from z_player_lib.c to override shield DL.
 */
const char* ExtEquip_GetShieldDLOverride(void);

/**
 * Draw the ext shield DL in the current matrix context (called from PostLimbDraw).
 * For Shield of Ikana: draws GI Mirror Shield model.
 */
void ExtEquip_DrawShieldDL(void* play);
void ExtEquip_DrawShieldBackDL(void* play);

/**
 * Draw Dragon Scale pendant at waist. Called from PostLimbDraw for PLAYER_LIMB_WAIST.
 */
void ExtEquip_DrawWaistScale(void* play);

/**
 * Draw the ext sword DL in the current matrix context (called from PostLimbDraw).
 * For Byrna: draws blue Somaria cane.
 */
void ExtEquip_DrawSwordDL(void* play);

/**
 * Draw anklet decoration on foot limbs (torus + fairy wings with pendulum).
 * Called from PostLimbDraw for PLAYER_LIMB_L_FOOT and PLAYER_LIMB_R_FOOT.
 * @param play PlayState
 * @param isRightFoot 1 for right foot, 0 for left foot
 */
void ExtEquip_DrawAnklet(void* play, s32 isRightFoot);

/**
 * Update pendulum physics for anklet wings. Called from Pegasus_Behavior.
 */
void ExtEquip_UpdateAnkletPhysics(void* player);

/**
 * Capture shoulder world positions for cloth physics (Magic Cape + Champion's Scarf).
 * Called from PostLimbDraw for PLAYER_LIMB_L_SHOULDER and PLAYER_LIMB_R_SHOULDER.
 * @param limbIndex The limb being drawn
 */
void ExtEquip_CaptureCapeShoulderPos(s32 limbIndex);

/**
 * Suppress icon override for ext equipment (used by kaleido equipment screen).
 * When set to 1, ExtInv_GetItemIcon won't replace sword/shield icons.
 */
extern u8 gExtEquipSuppressIconOverride;

// ---------------------------------------------------------------------------
// Shield of Ikana: Death Save
// ---------------------------------------------------------------------------

/** Check if Shield of Ikana should revive player instead of dying */
u8 ExtEquip_IkanaDeathSave(void* play);

/** Draw Spirit Breastplate (Iron Knuckle armor) on Link's torso.
 *  Called from PostLimbDraw for PLAYER_LIMB_UPPER. */
void ExtEquip_DrawBreastplate(void* play);

#ifdef __cplusplus
}
#endif

#endif // EXTENDED_EQUIPMENT_H
