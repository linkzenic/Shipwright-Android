#include "mods/nei_aligned_ui_paths.h"
/**
 * extended_equipment.c - Extended equipment system (cheat)
 *
 * Core system: page switching, equip/unequip, icon/name lookup, behavior dispatch.
 * Follows the same pattern as extended_inventory.c.
 *
 * When the cheat CVar is enabled, pressing L on the equipment page toggles
 * to a second page showing 12 new equipment pieces (3 per category).
 * Equipped state is persisted via CVars.
 */

#include "extended_equipment.h"
#include "nei_save.h" // Skijer's NEI
#include "transformation_masks/transformation_masks.h"
#include "transformation_masks/assets/mm_asset_loader.h"
#include "pak_loader/pak_loader.h"

// trade_items.c ships no header; declared locally, as randomizer.cpp and debugSaveEditor.cpp do.
#define TRADE_ADULT_PENDANT 19 // Pendant of Memories (== ITEM_EXT_BOOTS_2), mirrors trade_items.c
extern u8 TradeAdult_IsOwnedIndex(s32 index);
extern void TradeAdult_GiveIndex(s32 index);
#include <string.h>
#include <math.h>
#include "z64.h"
#include "z64player.h"
#include "z64save.h"
#include "functions.h"
#include "variables.h"

extern SaveContext gSaveContext;
extern s32 CVarGetInteger(const char* name, s32 defaultValue);
extern f32 CVarGetFloat(const char* name, f32 defaultValue);

// Cane of Byrna 3D model: blue-tinted variant of the Somaria cane, loaded from
// soh.o2r (objects/object_somaria/g_byrna_cane_dl — shares the Somaria tri
// geometry). No inline C model. LoadGfxByName crashes on a missing path, so gate.
extern u8 ResourceMgr_FileExists(const char* resName);
extern Gfx* ResourceMgr_LoadGfxByName(const char* path);

static Gfx* Byrna_GetCaneDL(void) {
    static Gfx* sCached = NULL;
    static u8 sTried = 0;
    if (!sTried) {
        sTried = 1;
        const char* otr = "__OTR__objects/object_somaria/g_byrna_cane_dl";
        if (ResourceMgr_FileExists(otr)) {
            sCached = ResourceMgr_LoadGfxByName(otr);
        }
    }
    return sCached;
}

// Trident (ext sword 3) held model: Phantom Ganon's lance — limb 9 of gPhantomGanonSkel,
// straight out of oot.o2r. Same vanilla-asset rule as the Byrna cane above: loaded by OTR
// path, nothing copied into soh.o2r. Skijer's NEI
static Gfx* Trident_GetLanceDL(void) {
    static Gfx* sCached = NULL;
    static u8 sTried = 0;
    if (!sTried) {
        sTried = 1;
        const char* otr = "__OTR__objects/object_gnd/gPhantomGanonSkelLimbsLimb_00C610DL_009298";
        if (ResourceMgr_FileExists(otr)) {
            sCached = ResourceMgr_LoadGfxByName(otr);
        }
    }
    return sCached;
}

// MANDATORY companion to the lance DL. That display list branches to segment 8 twice
// (gsSPDisplayList(0x08000001)) — the per-limb hook Phantom Ganon uses for his glow.
// Drawing it without pointing segment 8 at something valid makes the interpreter jump
// into whatever that segment last held and execute it as opcodes: the ASCII-opcode burst
// and then 0xC0000005. Randomizer_DrawExtTrident does exactly this for the same reason.
static const Gfx sTridentEmptyDL[] = {
    gsSPEndDisplayList(),
};

// ---------------------------------------------------------------------------
// TRIDENT PLACEMENT — all of it, baked. There is NO Item Editor section for the
// trident any more and nothing below reads a CVar: these constants ARE the values.
// The held transform is what the user dialled in on 2026-08-17 (read back out of
// shipofharkinian.json); the trail and the charge-glow numbers are the ones that
// shipped as defaults and were never moved off them (verified against the same
// file on 2026-08-18 — it holds no gItemEditor.Trident.Trail*/Thunder* keys at all).
// To retune any of these now, edit the number and rebuild.
// ---------------------------------------------------------------------------
// Held-lance placement, applied ON TOP of the Byrna-tuned limb transform in
// PostLimbDraw. Rotations are in degrees, offsets in the lance's own axes.
#define TRIDENT_HELD_SCALE 0.1f
#define TRIDENT_HELD_ROT_X (-53.5f)
#define TRIDENT_HELD_ROT_Y (-7.1f)
#define TRIDENT_HELD_ROT_Z (-50.5f)
#define TRIDENT_HELD_OFF_X 3000.0f
#define TRIDENT_HELD_OFF_Y (-1148.5f)
#define TRIDENT_HELD_OFF_Z (-2049.5f)
// Sword trail / melee quads along the lance, in the lance's own units (it is 14070
// long along +Z, bbox centre at +1485; the hand sits at local Z = -OFF_Z).
#define TRIDENT_TRAIL_ROT_X 0.0f
#define TRIDENT_TRAIL_ROT_Y 0.0f
#define TRIDENT_TRAIL_ROT_Z 0.0f
#define TRIDENT_TRAIL_TIP 8000.0f  // where the streak ends (toward the prongs)
#define TRIDENT_TRAIL_BASE 2000.0f // where it starts (just past the hand)
#define TRIDENT_TRAIL_WIDTH 2.0f   // sword's own width is in limb units; the lance frame is 0.5x
// Spin-attack CHARGE glow (En_M_Thunder's gSpinAttackChargingDL). Vanilla builds it
// off the raw hand matrix with a per-sword translate/scale — those numbers are shaped
// for a blade that is no longer drawn, so the trident gets the lance frame instead.
// These reproduce vanilla's adult-sword case (scale -1.2/-1.0/-0.7, the glow's long
// axis being +X) rescaled by 2.0: the lance frame carries base 5.0 x held 0.1 = 0.5,
// so one lance unit is half a hand unit.
#define TRIDENT_THUNDER_ROT_X 0.0f
#define TRIDENT_THUNDER_ROT_Y 0.0f
#define TRIDENT_THUNDER_ROT_Z 0.0f
#define TRIDENT_THUNDER_OFF 2000.0f // slide along the shaft, lance units (= trail base)
#define TRIDENT_THUNDER_LEN 2.4f    // along the shaft
#define TRIDENT_THUNDER_WIDTH 2.0f  // across it

// NEI Weapon Upgrades — the Hammer upgrade (Iron Knuckle's Axe) is driven from here,
// independent of the extended-equipment cheat. Accessors are defined in
// mods/items/logic/weapon_upgrades.c (linked via the custom_items.c TU).
#include "items/logic/weapon_upgrades.h"

// Unity build includes
#include "equipment/ext_equip_icons.c"
#include "equipment/ext_equip_names.c"
// Trident (ext sword 3) charged energy ball. Must come BEFORE ext_equip_behavior.c:
// equip_trident.c calls TridentChargeBall_Spawn(). Skijer's NEI
#include "actors/trident_charge_ball.h"
#include "actors/trident_charge_ball.c"
// equip_byrna.c calls ByrnaOrb_PressR/ThrowSeed(). Included BEFORE ext_equip_behavior.c
// so the accessors are declared by the time the behavior file uses them. Skijer's NEI
#include "items/objects/object_tornado.h" // byrna_orb.c: full-charge wind column
#include "actors/byrna_orb.h"
#include "actors/byrna_orb.c"
#include "equipment/ext_equip_behavior.c"

// Age requirements (mirror extended_inventory.h to avoid header cycle)
#ifndef AGE_REQ_NONE
#define AGE_REQ_NONE 9
#endif
#ifndef AGE_REQ_ADULT
#define AGE_REQ_ADULT LINK_AGE_ADULT
#endif
#ifndef AGE_REQ_CHILD
#define AGE_REQ_CHILD LINK_AGE_CHILD
#endif

// Per-piece age requirement: [equipType][index-1] (2026-07-29 layout)
//   SWORD:  Cane of Byrna,    Four Sword,    Trident
//   SHIELD: Goddess Shield,   Kite Shield,   Shield of Ikana
//   TUNIC:  Champion's Tunic, Magic Tunic,   Sage's
//   BOOTS:  Pegasus Boots,    Climb Boots,   Roc Boots
static const u8 sExtEquipAgeReqs[4][3] = {
    { AGE_REQ_NONE, AGE_REQ_CHILD, AGE_REQ_ADULT },
    { AGE_REQ_NONE, AGE_REQ_NONE, AGE_REQ_CHILD },
    { AGE_REQ_NONE, AGE_REQ_NONE, AGE_REQ_NONE }, // recolor tunics: any age (Champion/Spirit/Sage's)
    { AGE_REQ_NONE, AGE_REQ_NONE, AGE_REQ_NONE }, // Roc's Boots: any age (the ADULT was the old Dragon Scale's)
};

u8 ExtEquip_GetAgeReq(s16 equipType, u8 index) {
    if (equipType < 0 || equipType >= 4 || index < 1 || index > 3)
        return AGE_REQ_NONE;
    return sExtEquipAgeReqs[equipType][index - 1];
}

u8 ExtEquip_CheckAgeReq(s16 equipType, u8 index) {
    if (CVarGetInteger("gCheats.TimelessEquipment", 0))
        return 1;
    u8 req = ExtEquip_GetAgeReq(equipType, index);
    return (req == AGE_REQ_NONE) || (req == gSaveContext.linkAge);
}

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
ExtendedEquipmentState gExtEquipState;
u8 gExtEquipSuppressIconOverride = 0;
f32 gChampionSlowFactor = 1.0f;

// Transform backup: stores equipped ext equipment indices before transformation
static u8 sTransformBackup[4] = { 0 }; // [EQUIP_TYPE_SWORD..BOOTS]
static u8 sTransformBackupValid = 0;

#define EXT_EQUIP_PAGE_SWITCH_COOLDOWN 15

// ---------------------------------------------------------------------------
// Page management
// ---------------------------------------------------------------------------

// While ExtEquip_Init migrates a save, slot changes must not poke the half-built player.
static u8 sExtEquipInitInProgress = 0;

void ExtEquip_Init(void) {
    sExtEquipInitInProgress = 1;
    memset(&gExtEquipState, 0, sizeof(gExtEquipState));
    memset(&gExtEquipBehavior, 0, sizeof(gExtEquipBehavior));

    // Byrna orb: every spawned actor is already gone by the time Player_Init runs,
    // so the orb has to be forgotten (not killed) or its stale pointer would block
    // every future summon for the rest of the session. Skijer's NEI
    ByrnaOrb_Forget();

    // Load equipped state from save data (per-file, persisted only on game save) // Skijer's NEI
    gExtEquipState.currentExtSword = Nei_Save()->extEquipSword;
    gExtEquipState.currentExtShield = Nei_Save()->extEquipShield;
    gExtEquipState.currentExtTunic = Nei_Save()->extEquipTunic;
    gExtEquipState.currentExtBoots = Nei_Save()->extEquipBoots;

    // Migrate the old tunic layout (Cape/Spirit/Champion) to
    // Champion/Spirit/Sage's exactly once. Equipped implies owned.
    if (Nei_Save()->extTunicLayoutVersion < 1) {
        u8 hadCape = ExtEquip_HasItem(EQUIP_TYPE_TUNIC, 1);
        u8 hadChampion = ExtEquip_HasItem(EQUIP_TYPE_TUNIC, 3);
        u8 legacyEquippedTunic = gExtEquipState.currentExtTunic;

        ExtEquip_RemoveItem(EQUIP_TYPE_TUNIC, 1);
        ExtEquip_RemoveItem(EQUIP_TYPE_TUNIC, 3);
        if (hadCape || legacyEquippedTunic == 1) {
            Nei_Save()->capeOwned = 1;
        }
        if (hadChampion || legacyEquippedTunic == 3) {
            ExtEquip_GiveItem(EQUIP_TYPE_TUNIC, 1);
        }
        if (legacyEquippedTunic == 3) {
            gExtEquipState.currentExtTunic = 1;
            Nei_Save()->extEquipTunic = 1;
        } else if (legacyEquippedTunic == 1) {
            gExtEquipState.currentExtTunic = 0;
            Nei_Save()->extEquipTunic = 0;
        }
        Nei_Save()->extTunicLayoutVersion = 1;
    }
    // Migrate the old boots layout (Pegasus / Pendant / Water Dragon Scale) to the real-boots layout
    // (Pegasus / Climb / Roc). The Pendant keeps living on the left column, so its ownership goes to
    // the adult trade wheel (where MM's quest expects it anyway); the dead Dragon-Scale bit is cleared
    // so it can't read as "owns the Roc Boots". Equipped implies owned.
    if (Nei_Save()->extBootsLayoutVersion < 1) {
        if (ExtEquip_HasItem(EQUIP_TYPE_BOOTS, 2) || gExtEquipState.currentExtBoots == 2) {
            TradeAdult_GiveIndex(TRADE_ADULT_PENDANT);
        }
        ExtEquip_RemoveItem(EQUIP_TYPE_BOOTS, 2);
        ExtEquip_RemoveItem(EQUIP_TYPE_BOOTS, 3);
        if (gExtEquipState.currentExtBoots == 2 || gExtEquipState.currentExtBoots == 3) {
            gExtEquipState.currentExtBoots = 0;
            Nei_Save()->extEquipBoots = 0;
        }
        Nei_Save()->extBootsLayoutVersion = 1;
    }

    // Clamp to valid range
    if (gExtEquipState.currentExtSword > 3)
        gExtEquipState.currentExtSword = 0;
    if (gExtEquipState.currentExtShield > 3)
        gExtEquipState.currentExtShield = 0;
    if (gExtEquipState.currentExtTunic > 3)
        gExtEquipState.currentExtTunic = 0;
    if (gExtEquipState.currentExtBoots > 3)
        gExtEquipState.currentExtBoots = 0;

    // Generate placeholder icons
    ExtEquip_GenerateIcons();
    sExtEquipInitInProgress = 0;
}

void ExtEquip_Update(void) {
    if (gExtEquipState.pageSwitchTimer > 0) {
        gExtEquipState.pageSwitchTimer--;
    }

    // Trident charge ball: expire the post-impact super-damage grace window.
    // Ticked here (not from the ball's own update) so the window still closes
    // after the projectile has been killed. Skijer's NEI
    TridentChargeBall_Tick();

    // Byrna orb: same reason — the super-damage grace window AND the extract buff
    // timers have to keep counting down even when no orb is alive. Skijer's NEI
    ByrnaOrb_Tick();

    // Cheat switched off mid-game: take every ext piece off cleanly (cleanup + vanilla base) instead
    // of freezing its behavior mid-effect.
    if (!ExtEquip_IsEnabled()) {
        s16 t;

        gExtEquipState.equipPage = 0;
        for (t = EQUIP_TYPE_SWORD; t <= EQUIP_TYPE_BOOTS; t++) {
            if (ExtEquip_GetCurrent(t) != 0) {
                ExtEquip_SetSlot(t, 0);
            }
        }
    }
}

int ExtEquip_GetPage(void) {
    if (!ExtEquip_IsEnabled()) {
        return 0;
    }
    return gExtEquipState.equipPage;
}

void ExtEquip_SwitchPage(void) {
    if (!ExtEquip_IsEnabled())
        return;

    gExtEquipState.equipPage = (gExtEquipState.equipPage == 0) ? 1 : 0;
    gExtEquipState.pageSwitchTimer = EXT_EQUIP_PAGE_SWITCH_COOLDOWN;

    // When switching to vanilla page, restore original sword if Byrna was overriding it
    if (gExtEquipState.equipPage == 0 && gExtEquipBehavior.byrnaActive) {
        Byrna_Cleanup();
    }
}

u8 ExtEquip_CanSwitch(void) {
    return gExtEquipState.pageSwitchTimer <= 0;
}

u8 ExtEquip_IsEnabled(void) {
    return CVarGetInteger(CVAR_EXT_EQUIP_ENABLED, 0) != 0;
}

// ---------------------------------------------------------------------------
// Equip / Unequip
// ---------------------------------------------------------------------------

static void ExtEquip_SetCurrentByType(s16 equipType, u8 index) {
    switch (equipType) {
        case EQUIP_TYPE_SWORD:
            gExtEquipState.currentExtSword = index;
            Nei_Save()->extEquipSword = index; // Skijer's NEI
            break;
        case EQUIP_TYPE_SHIELD:
            gExtEquipState.currentExtShield = index;
            Nei_Save()->extEquipShield = index; // Skijer's NEI
            break;
        case EQUIP_TYPE_TUNIC:
            gExtEquipState.currentExtTunic = index;
            Nei_Save()->extEquipTunic = index; // Skijer's NEI
            break;
        case EQUIP_TYPE_BOOTS:
            gExtEquipState.currentExtBoots = index;
            Nei_Save()->extEquipBoots = index; // Skijer's NEI
            break;
    }
}

// ---------------------------------------------------------------------------
// Ownership
// ---------------------------------------------------------------------------

static u32 ExtEquip_GetBit(s16 equipType, u8 index) {
    return 1 << (EXT_EQUIP_OWNED_SHIFT + equipType * 3 + (index - 1));
}

u8 ExtEquip_HasItem(s16 equipType, u8 index) {
    if (index == 0 || index > 3 || equipType < 0 || equipType > 3)
        return 0;
    return (Nei_Save()->extEquipOwnedBits & ExtEquip_GetBit(equipType, index)) != 0; // Skijer's NEI
}

void ExtEquip_GiveItem(s16 equipType, u8 index) {
    if (index == 0 || index > 3 || equipType < 0 || equipType > 3)
        return;
    Nei_Save()->extEquipOwnedBits |= ExtEquip_GetBit(equipType, index); // Skijer's NEI
}

// ---------------------------------------------------------------------------
// The single writer of an equipped ext slot. Every path that changes a slot — kaleido, C button,
// give/remove, transforms, age swap, FleetSync, cheat toggle — goes through ExtEquip_SetSlot, so the
// outgoing piece is always cleaned up synchronously and the player/vanilla state never lags a frame.
// ---------------------------------------------------------------------------
void ExtEquip_RefreshPlayer(void) {
    // player->currentShield/Tunic/Boots + model group only follow the equipment nibbles through
    // Player_SetEquipmentData; nothing else refreshes them until the next scene load.
    if (gPlayState != NULL && !sExtEquipInitInProgress) {
        Player* player = GET_PLAYER(gPlayState);
        if (player != NULL) {
            Player_SetEquipmentData(gPlayState, player);
        }
    }
}

static void ExtEquip_ReloadBIcon(void) {
    if (gPlayState != NULL) {
        Interface_LoadItemIcon1(gPlayState, 0);
    }
}

// The best vanilla shield the player OWNS for an ext shield to ride on — an unowned base gets
// stripped by the age-swap revalidation, leaving a drawn shield that can't be raised.
static u16 ExtEquip_OwnedShieldBase(u16 preferred) {
    static const u16 sByPreference[] = { EQUIP_VALUE_SHIELD_MIRROR, EQUIP_VALUE_SHIELD_HYLIAN,
                                         EQUIP_VALUE_SHIELD_DEKU };
    s32 i;

    for (i = 0; i < 3; i++) {
        u16 value = sByPreference[i];
        if (value > preferred) {
            continue;
        }
        if (CHECK_OWNED_EQUIP(EQUIP_TYPE_SHIELD, value - 1)) {
            return value;
        }
    }
    return EQUIP_VALUE_SHIELD_NONE;
}

// Vanilla state each slot value implies. index 0 = the slot was just vacated: a sword/shield ext
// piece leaves Link BARE (user decision — nothing he wore before is restored), ext tunics/boots
// fall back to Kokiri.
static void ExtEquip_ApplyVanillaBase(s16 equipType, u8 oldIndex, u8 index) {
    switch (equipType) {
        case EQUIP_TYPE_SWORD:
            // All three ride the B button as THEMSELVES (ExtPlayer_GetItemAction aliases their ids
            // to the one-hand sword action): the equipment nibble and the save never see a Kokiri
            // Sword the player may not own. Byrna joined them when it stopped being a cane drawn
            // over the real sword and became the Insect Glaive — without a branch here, switching
            // Trident -> Byrna left the Trident's id on B and the cane behaved like the lance.
            if (index >= 1 && index <= 3) {
                gSaveContext.equips.buttonItems[0] = ExtEquip_GetItemId(EQUIP_TYPE_SWORD, index);
                Flags_UnsetInfTable(INFTABLE_SWORDLESS);
                ExtEquip_ReloadBIcon();
            } else if (index == 0 && (oldIndex >= 1 && oldIndex <= 3)) {
                Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_NONE);
                gSaveContext.equips.buttonItems[0] = ITEM_NONE;
                Flags_SetInfTable(INFTABLE_SWORDLESS);
                ExtEquip_ReloadBIcon();
            }
            break;
        case EQUIP_TYPE_SHIELD:
            // Ikana rides the Mirror, the other two the Hylian — whichever of those is owned.
            if (index == 0) {
                Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_NONE);
            } else {
                Inventory_ChangeEquipment(
                    EQUIP_TYPE_SHIELD,
                    ExtEquip_OwnedShieldBase(index == 3 ? EQUIP_VALUE_SHIELD_MIRROR : EQUIP_VALUE_SHIELD_HYLIAN));
            }
            break;
        case EQUIP_TYPE_TUNIC:
            // Exclusive with the Goron/Zora tunic in both directions; both land on Kokiri.
            Inventory_ChangeEquipment(EQUIP_TYPE_TUNIC, EQUIP_VALUE_TUNIC_KOKIRI);
            break;
        case EQUIP_TYPE_BOOTS:
            // Exclusive with the Iron/Hover boots in both directions.
            Inventory_ChangeEquipment(EQUIP_TYPE_BOOTS, EQUIP_VALUE_BOOTS_KOKIRI);
            break;
    }
}

// Trident: only the Divine Shield (ext 1) or a Mirror (Ikana ext 3, or the vanilla Mirror) may be
// held with it.
u8 ExtEquip_TridentAllowsShield(u8 extIndex, u16 vanillaValue) {
    if (extIndex == 1 || extIndex == 3) {
        return 1;
    }
    return (extIndex == 0) && (vanillaValue == EQUIP_VALUE_SHIELD_MIRROR);
}

static void ExtEquip_ApplyTridentShieldPolicy(void) {
    if (ExtEquip_TridentAllowsShield(ExtEquip_GetCurrent(EQUIP_TYPE_SHIELD), CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD))) {
        return;
    }
    if (ExtEquip_HasItem(EQUIP_TYPE_SHIELD, 1) && ExtEquip_CheckAgeReq(EQUIP_TYPE_SHIELD, 1)) {
        ExtEquip_SetSlot(EQUIP_TYPE_SHIELD, 1);
        return;
    }
    ExtEquip_SetSlot(EQUIP_TYPE_SHIELD, 0);
    if (CHECK_OWNED_EQUIP(EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_MIRROR)) {
        Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_MIRROR);
        ExtEquip_RefreshPlayer();
    }
}

void ExtEquip_SetSlot(s16 equipType, u8 index) {
    u8 old;

    if (equipType < 0 || equipType > 3 || index > 3) {
        return;
    }
    old = ExtEquip_GetCurrent(equipType);
    if (old == index) {
        return;
    }
    if (old != 0) {
        ExtEquip_CleanupSlot(equipType, old);
    }
    ExtEquip_SetCurrentByType(equipType, index);
    ExtEquip_ApplyVanillaBase(equipType, old, index);
    if (equipType == EQUIP_TYPE_SWORD && index == 3) {
        ExtEquip_ApplyTridentShieldPolicy();
    }
    ExtEquip_RefreshPlayer();
}

// Child<->adult swaps the whole vanilla loadout; an age-restricted ext piece must come off with it.
void ExtEquip_ValidateForAge(void) {
    s16 t;

    for (t = EQUIP_TYPE_SWORD; t <= EQUIP_TYPE_BOOTS; t++) {
        u8 current = ExtEquip_GetCurrent(t);
        if (current != 0 && !ExtEquip_CheckAgeReq(t, current)) {
            ExtEquip_SetSlot(t, 0);
        }
    }
}

// FleetSync writes Nei_Save()->extEquip* directly; pull that back into the RAM copy every
// predicate/draw reads, without re-applying bases (the peer already did).
void ExtEquip_ResyncFromSave(void) {
    gExtEquipState.currentExtSword = Nei_Save()->extEquipSword;
    gExtEquipState.currentExtShield = Nei_Save()->extEquipShield;
    gExtEquipState.currentExtTunic = Nei_Save()->extEquipTunic;
    gExtEquipState.currentExtBoots = Nei_Save()->extEquipBoots;
    ExtEquip_RefreshPlayer();
}

void ExtEquip_RemoveItem(s16 equipType, u8 index) {
    if (index == 0 || index > 3 || equipType < 0 || equipType > 3)
        return;
    Nei_Save()->extEquipOwnedBits &= ~ExtEquip_GetBit(equipType, index); // Skijer's NEI
    if (ExtEquip_GetCurrent(equipType) == index) {
        ExtEquip_SetSlot(equipType, 0);
    }
}

// Retired slots (2026-07-15 rework): pieces that no longer live in the ext-equipment grid.
//   TUNIC 1 (Magic Cape)          -> moved to the equipment page's upgrade column (passive effect).
//   BOOTS 2 (Pendant of Memories) -> moved to the upgrade column (effect toggle there).
//   BOOTS 3 (Water Dragon Scale)  -> deleted; Zora swim is the Zora Tunic's permanent effect.
// Their OWNERSHIP bits remain meaningful (the new systems read them) — only the slot is dead.
// No slot is retired anymore (Skijer 2026-07-29). History: TUNIC 1 (Magic Cape -> left column, slot
// re-used by the Champion's Tunic), BOOTS 2 (Pendant of Memories -> left column, slot re-used by the
// CLIMB BOOTS), BOOTS 3 (Water Dragon Scale deleted, Zora swim is the Zora Tunic's effect; slot
// re-used by the ROC BOOTS). Kept as a function so the call sites stay put if a slot is ever parked.
u8 ExtEquip_SlotRetired(s16 equipType, u8 index) {
    (void)equipType;
    (void)index;
    return false;
}

// ---------------------------------------------------------------------------
// Upgrade-column passives: Magic Cape + Pendant of Memories (Skijer 2026-07-15).
// They live on the equipment page's upgrade column now (replacing the bomb-bag and quiver/bullet
// capacity icons). Ownership = the SAME extEquipOwnedBits they always had (TUNIC 1 / BOOTS 2).
//   Cape:    magic refund is ALWAYS active once owned; the A-toggle only hides the cloth on Link.
//   Pendant: the A-toggle enables/disables its whole moveset.
// ---------------------------------------------------------------------------
u8 ExtEquip_CapeOwned(void) {
    // Own bit now (Skijer 2026-07-16): the ext TUNIC-1 slot is a real recolor tunic (Champion's), so
    // the Cape has its own ownership flag, migrated off the old TUNIC-1 bit in ExtEquip_Init.
    return Nei_Save()->capeOwned;
}

void ExtEquip_GiveCape(void) {
    Nei_Save()->capeOwned = 1;
}

u8 ExtEquip_CapeVisible(void) {
    return ExtEquip_CapeOwned() && !Nei_Save()->capeHidden;
}

void ExtEquip_ToggleCapeVisibility(void) {
    Nei_Save()->capeHidden = !Nei_Save()->capeHidden;
}

// Pendant of Memories — TWO separate flags (Skijer 2026-07-31, user decision):
//
//   OWN     (Nei_Save()->pendantOwned)     the EQUIPMENT piece. Holding the pendant in the adult
//                                          trade slot GRANTS it, and from then on it is PERMANENT:
//                                          the trade item can be handed away, the equipment piece
//                                          cannot. This is what the kaleido equipment upgrade column
//                                          shows and lets you toggle, and what FleetSync carries.
//   EFFECT  (Nei_Save()->pendantEffectOff) the moveset on/off toggle (A on that cell).
//
// History: ownership used to be the retired BOOTS-2 grid bit, which nothing could clear; that was
// replaced by reading the adult trade wheel DIRECTLY, which went too far the other way — trading the
// pendant away silently deleted the equipment piece and its whole moveset. The trade slot now GRANTS
// the equipment flag instead of BEING it. (pendantOwned already existed in NeiSaveData and was
// serialized; it just had no reader.)
void ExtEquip_GivePendant(void) {
    Nei_Save()->pendantOwned = 1;
}

u8 ExtEquip_PendantOwned(void) {
    // Latch on observation: this catches every acquisition path (trade grant, rando, save load,
    // FleetSync) without having to hook each one. Idempotent, and it never clears.
    if (!Nei_Save()->pendantOwned && TradeAdult_IsOwnedIndex(TRADE_ADULT_PENDANT)) {
        ExtEquip_GivePendant();
    }
    return Nei_Save()->pendantOwned;
}

u8 ExtEquip_PendantActive(void) {
    return ExtEquip_PendantOwned() && !Nei_Save()->pendantEffectOff;
}

void ExtEquip_TogglePendantEffect(void) {
    Nei_Save()->pendantEffectOff = !Nei_Save()->pendantEffectOff;
}

// ---------------------------------------------------------------------------
// Extended RECOLOR tunics (Skijer 2026-07-16): the 3 ext tunic slots are now real recolor tunics
// (like vanilla Goron/Zora). They equip with Kokiri as the vanilla base and repaint Link's tunic env
// color in Player_DrawImpl. Predicates = "this ext tunic is currently equipped".
//   Slot 1 = Champion's Tunic (blue) — flurry rush + bullet time
//   Slot 2 = Spirit Tunic (orange w/ rupees, black without) — rupee-immunity + fire/water timer skip
//   Slot 3 = Sage's Tunic (white) — medallion-driven passive resistances
// ---------------------------------------------------------------------------
// Dedicated upgrade-column icons — the Cape/Pendant no longer live in the ext grid (the TUNIC-1 grid
// slot is Champion now), so their kaleido icons come from here, NOT ExtEquip_GetIcon(grid).
void* ExtEquip_GetCapeIcon(void) {
    return (void*)gItemIconMagicCapeTex;
}
void* ExtEquip_GetPendantIcon(void) {
    return (void*)sNeiAligned_gItemIconPendantOfMemoriesTex_45;
}

u8 ExtEquip_IsChampionTunic(void) {
    return ExtEquip_IsEnabled() && ExtEquip_GetCurrent(EQUIP_TYPE_TUNIC) == 1;
}
u8 ExtEquip_IsSpiritTunic(void) {
    return ExtEquip_IsEnabled() && ExtEquip_GetCurrent(EQUIP_TYPE_TUNIC) == 2;
}
u8 ExtEquip_IsSagesTunic(void) {
    return ExtEquip_IsEnabled() && ExtEquip_GetCurrent(EQUIP_TYPE_TUNIC) == 3;
}
// Spirit Tunic "has money" gate — its damage-immunity + fire/water-timer-skip only work with rupees.
u8 ExtEquip_HasSagesResistance(SagesResistance resistance) {
    static const s32 sQuestItems[] = {
        QUEST_MEDALLION_WATER,  QUEST_MEDALLION_FIRE,   QUEST_MEDALLION_LIGHT,
        QUEST_MEDALLION_SHADOW, QUEST_MEDALLION_SPIRIT, QUEST_MEDALLION_FOREST,
    };

    return ExtEquip_IsSagesTunic() && resistance >= SAGES_RESIST_ICE && resistance <= SAGES_RESIST_WIND &&
           CHECK_QUEST_ITEM(sQuestItems[resistance]);
}

// Sage's Tunic damage flash: when a medallion resistance absorbs a hit, the tunic briefly dyes
// itself with that medallion's color, then fades back to white. Continuous sources (hot rooms,
// fan wind) keep refreshing the timer, so the dye holds while the medallion is still "feeding"
// the tunic.
#define SAGES_FLASH_HOLD_FRAMES 20
#define SAGES_FLASH_FADE_FRAMES 30
static const u8 sSagesMedallionColors[6][3] = {
    { 60, 130, 235 }, // ICE     <- Water Medallion
    { 235, 60, 30 },  // FIRE    <- Fire Medallion
    { 245, 225, 80 }, // THUNDER <- Light Medallion
    { 155, 70, 220 }, // STUN    <- Shadow Medallion
    { 240, 140, 40 }, // FALL    <- Spirit Medallion
    { 70, 195, 90 },  // WIND    <- Forest Medallion
};
static s16 sSagesFlashTimer = 0;
static u8 sSagesFlashResist = 0;

void ExtEquip_SagesFlash(SagesResistance resistance) {
    if (resistance > SAGES_RESIST_WIND) {
        return;
    }
    sSagesFlashResist = resistance;
    sSagesFlashTimer = SAGES_FLASH_HOLD_FRAMES + SAGES_FLASH_FADE_FRAMES;
}

void ExtEquip_SagesFlashTick(void) {
    if (sSagesFlashTimer > 0) {
        sSagesFlashTimer--;
    }
}

void ExtEquip_SagesFlashReset(void) {
    sSagesFlashTimer = 0;
    sSagesFlashResist = 0;
}

void ExtEquip_GetSagesTunicColor(u8* r, u8* g, u8* b) {
    *r = 235;
    *g = 240;
    *b = 245;
    if (ExtEquip_IsSagesTunic() && sSagesFlashTimer > 0) {
        const u8* m = sSagesMedallionColors[sSagesFlashResist];
        s32 num = (sSagesFlashTimer >= SAGES_FLASH_FADE_FRAMES) ? SAGES_FLASH_FADE_FRAMES : sSagesFlashTimer;

        *r = (u8)(*r + (((s32)m[0] - *r) * num) / SAGES_FLASH_FADE_FRAMES);
        *g = (u8)(*g + (((s32)m[1] - *g) * num) / SAGES_FLASH_FADE_FRAMES);
        *b = (u8)(*b + (((s32)m[2] - *b) * num) / SAGES_FLASH_FADE_FRAMES);
    }
}

u8 ExtEquip_SpiritHasMoney(void) {
    return ExtEquip_IsSpiritTunic() && (gSaveContext.rupees > 0);
}

void ExtEquip_Equip(s16 equipType, u8 index) {
    if (index == 0 || index > 3)
        return;

    // Freed/retired slots can never be equipped (reserved for the new boots)
    if (ExtEquip_SlotRetired(equipType, index))
        return;

    // Pikachu cannot use extended equipment
    if (TransformMasks_IsTransformedAny() && MmForm_GetCurrentForm() == MM_PLAYER_FORM_PIKACHU)
        return;

    // Must own the item to equip it
    if (!ExtEquip_HasItem(equipType, index))
        return;

    // Age restriction
    if (!ExtEquip_CheckAgeReq(equipType, index))
        return;

    // The Trident only tolerates the Divine Shield or a Mirror.
    if (equipType == EQUIP_TYPE_SHIELD && ExtEquip_GetCurrent(EQUIP_TYPE_SWORD) == 3 &&
        !ExtEquip_TridentAllowsShield(index, 0))
        return;

    // Equipping the piece already worn toggles it off.
    ExtEquip_SetSlot(equipType, (ExtEquip_GetCurrent(equipType) == index) ? 0 : index);
}

void ExtEquip_Unequip(s16 equipType) {
    ExtEquip_SetSlot(equipType, 0);
}

// ---------------------------------------------------------------------------
// Transform integration
// ---------------------------------------------------------------------------

// A transform parks the ext pieces in RAM ONLY: Nei_Save keeps the loadout, so saving while
// transformed doesn't zero the four slots, and no vanilla base is touched (the form owns the body).
static void ExtEquip_SetCurrentRamOnly(s16 equipType, u8 index) {
    switch (equipType) {
        case EQUIP_TYPE_SWORD:
            gExtEquipState.currentExtSword = index;
            break;
        case EQUIP_TYPE_SHIELD:
            gExtEquipState.currentExtShield = index;
            break;
        case EQUIP_TYPE_TUNIC:
            gExtEquipState.currentExtTunic = index;
            break;
        case EQUIP_TYPE_BOOTS:
            gExtEquipState.currentExtBoots = index;
            break;
    }
}

void ExtEquip_UnequipForTransform(void) {
    if (!ExtEquip_IsEnabled())
        return;
    if (sTransformBackupValid)
        return; // Already backed up (form-to-form switch)

    sTransformBackup[EQUIP_TYPE_SWORD] = gExtEquipState.currentExtSword;
    sTransformBackup[EQUIP_TYPE_SHIELD] = gExtEquipState.currentExtShield;
    sTransformBackup[EQUIP_TYPE_TUNIC] = gExtEquipState.currentExtTunic;
    sTransformBackup[EQUIP_TYPE_BOOTS] = gExtEquipState.currentExtBoots;
    sTransformBackupValid = 1;

    for (s16 t = EQUIP_TYPE_SWORD; t <= EQUIP_TYPE_BOOTS; t++) {
        if (ExtEquip_GetCurrent(t) != 0) {
            ExtEquip_CleanupSlot(t, ExtEquip_GetCurrent(t));
            ExtEquip_SetCurrentRamOnly(t, 0);
        }
    }
}

void ExtEquip_RestoreFromTransform(void) {
    if (!sTransformBackupValid)
        return;
    if (!ExtEquip_IsEnabled()) {
        sTransformBackupValid = 0;
        return;
    }

    for (s16 t = EQUIP_TYPE_SWORD; t <= EQUIP_TYPE_BOOTS; t++) {
        if (sTransformBackup[t] != 0 && ExtEquip_HasItem(t, sTransformBackup[t])) {
            ExtEquip_SetCurrentRamOnly(t, sTransformBackup[t]);
        }
    }
    sTransformBackupValid = 0;
    ExtEquip_RefreshPlayer();
}

void ExtEquip_ClearTransformBackup(void) {
    sTransformBackupValid = 0;
    memset(sTransformBackup, 0, sizeof(sTransformBackup));
}

void ExtEquip_ToggleFromCButton(u16 itemId) {
    if (itemId < ITEM_EXT_SWORD_1 || itemId > ITEM_EXT_BOOTS_3)
        return;
    if (!ExtEquip_IsEnabled())
        return;

    // Pikachu cannot use extended equipment
    if (TransformMasks_IsTransformedAny() && MmForm_GetCurrentForm() == MM_PLAYER_FORM_PIKACHU)
        return;

    // Map itemId to equipType + index
    // ITEM_EXT_SWORD_1=0xE0, _2=0xE1, _3=0xE2
    // ITEM_EXT_SHIELD_1=0xE3, _2=0xE4, _3=0xE5
    // ITEM_EXT_TUNIC_1=0xE6, _2=0xE7, _3=0xE8
    // ITEM_EXT_BOOTS_1=0xE9, _2=0xEA, _3=0xEB
    u16 offset = itemId - ITEM_EXT_SWORD_1; // 0-11
    s16 equipType = offset / 3;             // 0=sword, 1=shield, 2=tunic, 3=boots
    u8 index = (offset % 3) + 1;            // 1-3

    // Age restriction (allow unequip even if age fails — player can always remove)
    u8 current = ExtEquip_GetCurrent(equipType);
    if (current != index && !ExtEquip_CheckAgeReq(equipType, index)) {
        Audio_PlaySoundGeneral(NA_SE_SY_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    // Toggle: if already equipped with this index, unequip; otherwise equip
    if (current == index) {
        ExtEquip_SetSlot(equipType, 0);
        Audio_PlaySoundGeneral(NA_SE_IT_SHIELD_REMOVE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    } else {
        ExtEquip_Equip(equipType, index);
        Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
}

u8 ExtEquip_GetCurrent(s16 equipType) {
    switch (equipType) {
        case EQUIP_TYPE_SWORD:
            return gExtEquipState.currentExtSword;
        case EQUIP_TYPE_SHIELD:
            return gExtEquipState.currentExtShield;
        case EQUIP_TYPE_TUNIC:
            return gExtEquipState.currentExtTunic;
        case EQUIP_TYPE_BOOTS:
            return gExtEquipState.currentExtBoots;
        default:
            return 0;
    }
}

// ---------------------------------------------------------------------------
// Icons / Names
// ---------------------------------------------------------------------------

// Icon lookup table: [type][index-1] = OTR path string
// Skijer 2026-07-29 (kaleido re-layout) — page 2 is now:
//   swords  Cane of Byrna (dummy, behavior moved to the GFS line) / Four Sword / Trident
//   shields Goddess Shield / Kite Shield / Shield of Ikana (MM mirror shield)
//   tunics  Champion's (blue) / Magic Tunic (orange) / Sage's (white) — recolor tunics
//   boots   Pegasus Boots / Climb Boots / Roc Boots (all three are REAL boots)
// Use the aligned generated arrays (g...), not raw string macros (dg...).
// The renderer's resource signature check requires an aligned address.
static const char* const sExtEquipIconPaths[4][3] = {
    // Swords
    { gItemIconCaneOfByrnaTex, gItemIconFourSwordTex, gItemIconTridentTex },
    // Shields
    { gItemIconGoddessShieldTex, gItemIconKiteShieldTex,
      sNeiAligned_gItemIconMirrorShieldTex_40 }, // Shield of Ikana (MM mirror shield)
    // Tunics
    { gItemIconChampionsTunicTex, gItemIconMagicTunicTex, gItemIconSagesTunicTex },
    // Boots
    { gItemIconPegasusBootsTex, gItemIconClimbBootsTex, gItemIconRocBootsTex },
};

void* ExtEquip_GetIcon(s16 equipType, u8 index) {
    if (equipType < 0 || equipType >= 4 || index < 1 || index > 3) {
        return NULL;
    }

    return (void*)sExtEquipIconPaths[equipType][index - 1];
}

u16 ExtEquip_GetItemId(s16 equipType, u8 index) {
    // Map (type, index) to ITEM_EXT_xxx
    // type 0 (sword): 0xE0 + (index-1)
    // type 1 (shield): 0xE3 + (index-1)
    // type 2 (tunic): 0xE6 + (index-1)
    // type 3 (boots): 0xE9 + (index-1)
    if (index < 1 || index > 3 || equipType < 0 || equipType >= 4) {
        return 0;
    }
    return ITEM_EXT_SWORD_1 + (equipType * 3) + (index - 1);
}

// Set by the equipment kaleido while it is naming a PAGE-2 GRID cell (same idiom as
// gExtEquipSuppressIconOverride). Only ITEM_EXT_BOOTS_2 (0xEA) is ambiguous: as an inventory/trade-wheel
// id it is the Pendant of Memories, as a grid slot it is the Climb Boots. Skijer 2026-07-29
u8 gExtEquipGridNameContext = 0;

void* ExtEquip_GetNameTex(u16 itemId, u8 language) {
    return ExtEquip_LookupNameTex(itemId, language);
}

// ---------------------------------------------------------------------------
// Behavior
// ---------------------------------------------------------------------------

ExtEquipBehaviorState gExtEquipBehavior;

void ExtEquip_UpdateBehavior(void* playerVoid, void* playVoid) {
    Player* player = (Player*)playerVoid;
    PlayState* play = (PlayState*)playVoid;

    // NEI weapon upgrades are NOT extended equipment — they run whenever the upgrade is owned,
    // regardless of the ext-equipment cheat. Gate on the local player (read global save state +
    // local input for the throw).
    if (gPlayState == NULL || player == GET_PLAYER(gPlayState)) {
        if (WeaponUpgrade_HasHammerAxe()) {
            IKAxe_Behavior(player, play);
        } else {
            IKAxe_Cleanup();
        }
        if (WeaponUpgrade_HasGreatFairy()) {
            GreatFairySword_Behavior(player, play);
        }

        // Zora Tunic swim and upgrade-column passives are ownership/equipment based,
        // not extended-page-cheat based.
        DragonScale_Behavior(player, play);
        if (ExtEquip_CapeVisible()) {
            MagicCape_Behavior(player, play);
        }
        MagicCape_Cleanup();
        if (ExtEquip_PendantActive()) {
            Pendant_Behavior(player, play);
        } else {
            Pendant_Reset();
        }
    }

    if (!ExtEquip_IsEnabled()) {
        Champion_Cleanup(play);
        // Kite Shield surfing takes the player over, so it must be released even when the cheat is
        // switched off mid-ride — the dispatcher below (where every other _Cleanup lives) never
        // runs in that case, and the surf would keep the shield hidden and fall damage off forever.
        KiteShield_Cleanup();
        return;
    }

    ExtEquip_DispatchBehavior(player, play);
}

// ---------------------------------------------------------------------------
// B-button suppression for ext pieces that own their own melee moveset.
//
// THE ORDERING PROBLEM: OOT's actionFunc runs at z_player.c:13633, but
// ExtEquip_UpdateBehavior only runs at :13970. So on the frame B is pressed,
// Player_ProcessItemButtons has ALREADY started a vanilla sword swing before the
// moveset gets a chance to look at the input — the custom clip then replaces it
// one frame later. That one-frame overlap is what reads in game as "sometimes
// it's the sword, sometimes it's the trident".
//
// The forms solve it by returning ITEM_NONE for the B slot so OOT never
// interprets B at all (see the Deku / mask branches in Player_GetItemOnButton).
// Same trick here, with one extra condition: only once the weapon is ALREADY
// drawn, because otherwise B could never draw it in the first place.
// Skijer's NEI
// ---------------------------------------------------------------------------
u8 ExtEquip_BlocksBButtonSword(void) {
    Player* player;

    if (!ExtEquip_IsEnabled()) {
        return 0;
    }
    // Trident (sword 3) drives its own combo/flurry/charge off raw B.
    if (gExtEquipState.currentExtSword != 3) {
        return 0;
    }
    if (gPlayState == NULL) {
        return 0;
    }
    player = GET_PLAYER(gPlayState);
    if (player == NULL) {
        return 0;
    }
    return (Player_GetMeleeWeaponHeld(player) != 0) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Ext-equipment PARRY dispatcher — ONE hook in z_player.c for all of page 2.
//
// This is the "no shield raised" parry path: it sits in the damage branch chain,
// ahead of the vanilla damage branches, for pieces whose guard is an ANIMATION
// rather than a real raised shield (so shieldQuad never bounces and the
// *_OnShieldBlock hook at z_player.c:5737 never fires for them). The Gerudo Dual
// Blades branch right above it is the same idea, form-side.
//
// To add a piece: add a case here. z_player.c does not change again.
//
// Each handler must self-gate on its own slot and return 1 only when it actually
// consumed the hit, because a stray 1 silently eats damage the player should
// have taken. Skijer's NEI
// ---------------------------------------------------------------------------
u8 ExtEquip_TryParry(void* playVoid, void* playerVoid) {
    PlayState* play = (PlayState*)playVoid;
    Player* player = (Player*)playerVoid;

    if (play == NULL || player == NULL) {
        return 0;
    }

    // --- swords ---
    // (The Trident is NOT here: its guard is a real equipped shield — Divine as a
    // child, Mirror as an adult — so its blocks are that shield's, with no parry of
    // its own on top.)
    // Cane of Byrna (sword 1) = Insect Glaive. The glaive itself has NO guard in
    // MHR and never gets one — what eats the hit here is the light orb, and only
    // while it is actually orbiting Link. Send it out to harvest and you are
    // open until it comes back; that trade is the whole point of the orb.
    // Skijer's NEI
    if (gExtEquipState.currentExtSword == 1 && ByrnaOrb_TryAbsorb(play)) {
        return 1;
    }

    return 0;
}

void ExtEquip_OnMeleeHit(void* playerVoid, void* playVoid) {
    Player* player = (Player*)playerVoid;
    PlayState* play = (PlayState*)playVoid;

    // Great Fairy's Sword recovers HP+MP on hit, independent of the ext-equipment cheat.
    if (WeaponUpgrade_HasGreatFairy() && player->heldItemAction == PLAYER_IA_SWORD_BIGGORON) {
        GreatFairySword_OnMeleeHit(player, play);
    }

    if (!ExtEquip_IsEnabled())
        return;

    ExtEquip_OnMeleeHitDispatch(player, play);
}

void ExtEquip_DrawBehavior(void* playerVoid, void* playVoid) {
    Player* player = (Player*)playerVoid;
    PlayState* play = (PlayState*)playVoid;

    // Skip remote dummy players. HarpoonDummyPlayer_Draw delegates to
    // Player_Draw for skeleton/anim parity, which routes here. But these draws
    // read GLOBAL state (the LOCAL player's slots / save) — drawing Four Sword
    // clones / Pegasus wind cone / Magic Cape / IK Axe reticle / Water-Dragon
    // barrier on remote dummies would render the local player's effects on every
    // peer's body. Gate on "this player is the local player actor".
    if (gPlayState != NULL) {
        Player* localPlayer = GET_PLAYER(gPlayState);
        if (player != localPlayer) {
            return;
        }
    }

    // Hammer upgrade reticle — independent of the ext-equipment cheat.
    if (WeaponUpgrade_HasHammerAxe()) {
        IKAxe_DrawReticle(player, play);
    }

    // Ownership/equipped passives draw independently of the extended-page cheat.
    DScale_Draw(player, play);
    if (ExtEquip_CapeVisible()) {
        MagicCape_Draw(player, play);
    }

    if (!ExtEquip_IsEnabled())
        return;

    ExtEquip_DrawDispatch(player, play);
}

// The lance's placement on top of the Byrna-tuned limb transform. Rotations first,
// then the offset, so the offsets run along the LANCE's own axes — "up" keeps
// meaning up the shaft whichever way the hand points. Shared by the model draw and
// by the sword trail / melee quads (ExtEquip_TridentTrailBegin), which is what makes
// the trail follow the lance instead of the invisible sword. Skijer's NEI
void ExtEquip_TridentApplyHeldTransform(void) {
    Matrix_RotateZYX((s16)(TRIDENT_HELD_ROT_X * 182.04f), (s16)(TRIDENT_HELD_ROT_Y * 182.04f),
                     (s16)(TRIDENT_HELD_ROT_Z * 182.04f), MTXMODE_APPLY);
    Matrix_Scale(TRIDENT_HELD_SCALE, TRIDENT_HELD_SCALE, TRIDENT_HELD_SCALE, MTXMODE_APPLY);
    Matrix_Translate(TRIDENT_HELD_OFF_X, TRIDENT_HELD_OFF_Y, TRIDENT_HELD_OFF_Z, MTXMODE_APPLY);
}

// Sword trail + melee quads for the trident: put the CURRENT matrix (the raw
// L_HAND limb matrix, as z_player_lib.c has it when it computes the trail) into the
// lance's own frame, so func_80090A28 / func_800906D4 measure the trail and the
// hitbox along the lance instead of along the sword that is no longer drawn.
//
// The chain is exactly the model's (Byrna base transform + the held transform
// above), then the trail extras from the Item Editor: an extra rotation to fine-tune
// where the streak sits on the shaft, RotateY(-90°) so the sword code's "+X is the
// blade" reads as the lance's +Z, a slide along the shaft for the base, and a
// width scale on the two axes across the blade. Returns 1 and leaves the matrix
// PUSHED (caller pops) when the trident is out; 0 and untouched otherwise.
// Skijer's NEI
u8 ExtEquip_TridentTrailBegin(void) {
    Player* player = (gPlayState != NULL) ? GET_PLAYER(gPlayState) : NULL;

    if (!ExtEquip_IsEnabled() || (gExtEquipState.currentExtSword != 3) || (player == NULL) ||
        (Player_GetMeleeWeaponHeld(player) == 0)) {
        return 0;
    }

    Matrix_Push();
    // z_player_lib.c's Byrna base (the block that draws ExtEquip_DrawSwordDL).
    Matrix_Translate(2028.26f, 267.2f, -33.82f, MTXMODE_APPLY);
    Matrix_RotateZYX(-0x8000, 0, 0x4000, MTXMODE_APPLY);
    Matrix_Scale(5.0f, 5.0f, 5.0f, MTXMODE_APPLY);
    ExtEquip_TridentApplyHeldTransform();
    // Trail extras.
    Matrix_RotateZYX((s16)(TRIDENT_TRAIL_ROT_X * 182.04f), (s16)(TRIDENT_TRAIL_ROT_Y * 182.04f),
                     (s16)(TRIDENT_TRAIL_ROT_Z * 182.04f), MTXMODE_APPLY);
    Matrix_RotateY(-M_PI / 2.0f, MTXMODE_APPLY); // sword +X -> lance +Z
    Matrix_Translate(TRIDENT_TRAIL_BASE, 0.0f, 0.0f, MTXMODE_APPLY);
    Matrix_Scale(1.0f, TRIDENT_TRAIL_WIDTH, TRIDENT_TRAIL_WIDTH, MTXMODE_APPLY);
    return 1;
}

// Spin-attack charge glow (En_M_Thunder). Called with the raw L_HAND matrix already
// current (Matrix_Mult(&player->mf_9E0, MTXMODE_NEW), which is exactly the matrix
// ExtEquip_TridentTrailBegin starts from) IN PLACE OF vanilla's per-sword
// translate/scale block, so the glow ends up lying along the lance instead of along
// the hidden sword. Leaves the matrix set for the caller — En_M_Thunder scales Y/Z
// after this for the pulse and never pops, same as vanilla. Returns 0 and touches
// nothing when the trident is not out. Skijer's NEI
u8 ExtEquip_TridentThunderTransform(void) {
    Player* player = (gPlayState != NULL) ? GET_PLAYER(gPlayState) : NULL;

    if (!ExtEquip_IsEnabled() || (gExtEquipState.currentExtSword != 3) || (player == NULL) ||
        (Player_GetMeleeWeaponHeld(player) == 0)) {
        return 0;
    }

    // The lance frame, identical to the trail's.
    Matrix_Translate(2028.26f, 267.2f, -33.82f, MTXMODE_APPLY);
    Matrix_RotateZYX(-0x8000, 0, 0x4000, MTXMODE_APPLY);
    Matrix_Scale(5.0f, 5.0f, 5.0f, MTXMODE_APPLY);
    ExtEquip_TridentApplyHeldTransform();
    Matrix_RotateZYX((s16)(TRIDENT_THUNDER_ROT_X * 182.04f), (s16)(TRIDENT_THUNDER_ROT_Y * 182.04f),
                     (s16)(TRIDENT_THUNDER_ROT_Z * 182.04f), MTXMODE_APPLY);
    Matrix_RotateY(-M_PI / 2.0f, MTXMODE_APPLY); // glow's long axis (+X) -> lance +Z
    // Translate before the scale, so the offset stays in the same lance units the
    // trail uses.
    Matrix_Translate(TRIDENT_THUNDER_OFF, 0.0f, 0.0f, MTXMODE_APPLY);
    // Negative like vanilla's: the charging DL is authored facing the other way.
    Matrix_Scale(-TRIDENT_THUNDER_LEN, -TRIDENT_THUNDER_WIDTH, -TRIDENT_THUNDER_WIDTH * 0.7f, MTXMODE_APPLY);
    Matrix_RotateX(16384.0f, MTXMODE_APPLY); // vanilla's, kept: spins the cross-section only
    return 1;
}

// Blade length the trail/quads should measure, in lance units — the sword code's
// D_80126080.x while ExtEquip_TridentTrailBegin's frame is current.
f32 ExtEquip_TridentTrailLength(void) {
    return TRIDENT_TRAIL_TIP - TRIDENT_TRAIL_BASE;
}

// Cane of Byrna in the sword hand, dialled in-game 2026-08-29 and baked. The
// Item Editor sliders that produced these are gone, same as the Trident's and the
// Slate's — retuning means editing here.
#define BYRNA_CANE_POS_X (-887.74f)
#define BYRNA_CANE_POS_Y 988.20f
#define BYRNA_CANE_POS_Z 54.18f
#define BYRNA_CANE_ROT_X 1.0f
#define BYRNA_CANE_ROT_Y 0.0f
#define BYRNA_CANE_ROT_Z 49.7f
#define BYRNA_CANE_SCALE_X 7.76f
#define BYRNA_CANE_SCALE_Y 10.45f
#define BYRNA_CANE_SCALE_Z 7.71f

// MEASURED from v_somaria_cane_vtx_0/_1 (262 verts): the cane's long axis is Y,
// spanning -416..+223. X spans 181 and Z only 47, so Y is unambiguous.
#define BYRNA_CANE_AXIS_MIN (-416.0f)
#define BYRNA_CANE_AXIS_MAX 223.0f
// The streak's own thickness. 1.0 leaves it at whatever the cane frame's scale
// already gives it.
#define BYRNA_TRAIL_WIDTH 1.0f

// Assumes the limb matrix is already on the stack (called from PostLimbDraw).
// Only the Byrna branch moved: the Iron Knuckle's Axe draws through this same
// matrix and must keep the placement it always had.
void ExtEquip_ApplySwordDLMatrix(void) {
    if (gExtEquipState.currentExtSword == 1) {
        Matrix_Translate(BYRNA_CANE_POS_X, BYRNA_CANE_POS_Y, BYRNA_CANE_POS_Z, MTXMODE_APPLY);
        Matrix_RotateZYX((s16)(BYRNA_CANE_ROT_X * 182.04f), (s16)(BYRNA_CANE_ROT_Y * 182.04f),
                         (s16)(BYRNA_CANE_ROT_Z * 182.04f), MTXMODE_APPLY);
        Matrix_Scale(BYRNA_CANE_SCALE_X, BYRNA_CANE_SCALE_Y, BYRNA_CANE_SCALE_Z, MTXMODE_APPLY);
        return;
    }

    Matrix_Translate(2028.26f, 267.2f, -33.82f, MTXMODE_APPLY);
    Matrix_RotateZYX(-0x8000, 0, 0x4000, MTXMODE_APPLY);
    Matrix_Scale(5.0f, 5.0f, 5.0f, MTXMODE_APPLY);
}

// Swing trail + melee quads in the CANE's frame instead of the hidden sword's,
// which is why the streak used to hang off in empty space. Mirrors
// ExtEquip_TridentTrailBegin; the caller pops the matrix.
u8 ExtEquip_ByrnaTrailBegin(void) {
    Player* player = (gPlayState != NULL) ? GET_PLAYER(gPlayState) : NULL;

    if (!ExtEquip_IsEnabled() || (gExtEquipState.currentExtSword != 1) || (player == NULL) ||
        (Player_GetMeleeWeaponHeld(player) == 0)) {
        return 0;
    }

    Matrix_Push();
    ExtEquip_ApplySwordDLMatrix();
    // The trail machinery lays the streak along +X; the cane's length is +Y.
    Matrix_RotateZ(-M_PI / 2.0f, MTXMODE_APPLY);
    Matrix_Translate(BYRNA_CANE_AXIS_MIN, 0.0f, 0.0f, MTXMODE_APPLY);
    Matrix_Scale(1.0f, BYRNA_TRAIL_WIDTH, BYRNA_TRAIL_WIDTH, MTXMODE_APPLY);
    return 1;
}

f32 ExtEquip_ByrnaTrailLength(void) {
    return BYRNA_CANE_AXIS_MAX - BYRNA_CANE_AXIS_MIN;
}

// Two-handed like the Biggoron's Sword, which is what takes the shield away. Fed
// to VB_PLAYER_HOLDS_TWO_HANDED_WEAPON rather than forcing heldItemAction: the old
// sword-slot hijack is exactly what was removed from this slot.
// Not gated on the weapon being drawn: the glaive is a pole weapon, so the shield
// stays on Link's back the whole time the slot is equipped, sheathed or not.
u8 ExtEquip_ByrnaIsTwoHanded(void* playerVoid) {
    Player* player = (Player*)playerVoid;

    return ExtEquip_IsEnabled() && (gExtEquipState.currentExtSword == 1) && (player != NULL);
}

void ExtEquip_DrawSwordDL(void* playVoid) {
    PlayState* play = (PlayState*)playVoid;

    // Hammer upgrade: draw the Iron Knuckle's Axe in place of the hammer DL.
    // IKAxe_DrawAxe self-guards on heldItemAction == HAMMER / throw state, and the
    // hammer DL itself is hidden via ExtEquip_ShouldHideSwordDL. Independent of cheat.
    if (WeaponUpgrade_HasHammerAxe()) {
        IKAxe_DrawAxe(play);
    }

    if (gExtEquipState.currentExtSword == 1) {
        // Byrna: draw blue cane DL only when sword is held (not sheathed)
        Player* drawPlayer = GET_PLAYER(play);
        if (Player_GetMeleeWeaponHeld(drawPlayer) != 0) {
            Gfx* byrnaDL = Byrna_GetCaneDL();
            if (byrnaDL != NULL) {
                OPEN_DISPS(play->state.gfxCtx);
                gSPDisplayList(POLY_OPA_DISP++, byrnaDL);
                CLOSE_DISPS(play->state.gfxCtx);
            }
        }
    }

    // Trident: Phantom Ganon's lance, drawn on the sword limb matrix. Skijer's NEI
    if (gExtEquipState.currentExtSword == 3) {
        Player* drawPlayer = GET_PLAYER(play);
        if (Player_GetMeleeWeaponHeld(drawPlayer) != 0) {
            Gfx* lanceDL = Trident_GetLanceDL();
            if (lanceDL != NULL) {
                OPEN_DISPS(play->state.gfxCtx);
                // The caller (z_player_lib.c PostLimbDraw) has already applied the
                // Byrna-tuned limb transform, which is sized for the Somaria cane.
                // The lance is authored in a very different scale (14070 units along
                // +Z, bbox centre at Z=+1485), so it needs its own correction on top.
                //
                // Placement DIALLED IN by the user 2026-08-17 and baked here as the
                // defaults, which is why the Item Editor no longer carries trident
                // sliders — the pass is done. The CVars are still read so the values
                // stay overridable, but nothing in the UI writes them any more.
                //
                // Rotations first, then the offset, so the offsets run along the LANCE's own
                // axes — "up" keeps meaning up the shaft whichever way the hand points.
                // Skijer's NEI
                Matrix_Push();
                ExtEquip_TridentApplyHeldTransform();
                gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)sTridentEmptyDL);
                gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                          G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                gSPDisplayList(POLY_OPA_DISP++, lanceDL);
                Matrix_Pop();
                CLOSE_DISPS(play->state.gfxCtx);
            }
        }
    }
}

// item_cane_of_somaria.c — the Dual Cane borrows the two-handed BGS model group for
// its stance, so the sword DL that group normally draws has to be suppressed.
u8 Cane_IsActive(void);

// mods/extended_player.h defines this, but pulling that header in here just for one
// constant drags the whole custom-item action table with it. Guarded so the real
// definition always wins if the include order ever changes.
#ifndef PLAYER_IA_NET
#define PLAYER_IA_NET 0x7E
#endif

u8 ExtEquip_ShouldHideSwordDL(void) {
    // Dual Cane: the BGS model group is used for the two-handed POSE only. Same
    // arrangement as the Cane of Byrna below, which also swaps the blade for its
    // own model. Checked before the ext-equipment gate because the cane is a
    // page-2 custom item, not ext equipment.
    if (Cane_IsActive()) {
        return 1;
    }

    // Net: it borrows Player_UpperAction_Sword so that drawing another item can take
    // it out of Link's hands, but that action makes the engine draw the equipped
    // BLADE too — which read as pulling the sword out on top of the net. The net has
    // its own model, so the sword's is suppressed exactly like the cane's.
    if (gPlayState != NULL) {
        Player* netPlayer = GET_PLAYER(gPlayState);

        if ((netPlayer != NULL) && (netPlayer->heldItemAction == PLAYER_IA_NET)) {
            return 1;
        }
    }

    // Hammer upgrade: hide the hammer DL only while the axe is actually being drawn
    // (in free mode / putaway, don't hide — vanilla shows the open hand). Independent
    // of the ext-equipment cheat.
    if (WeaponUpgrade_HasHammerAxe() && gExtEquipBehavior.ikAxeDrawing)
        return 1;

    if (!ExtEquip_IsEnabled())
        return 0;

    // Cane of Byrna replaces the sword model with its own draw
    if (gExtEquipState.currentExtSword == 1)
        return 1;

    // Trident: same arrangement — Phantom Ganon's lance replaces the blade. Skijer's NEI
    if (gExtEquipState.currentExtSword == 3)
        return 1;

    return 0;
}

// Goddess (1) and Kite (2) are wooden; the Shield of Ikana (3) is the MM Mirror Shield.
u8 ExtEquip_ShieldIsWooden(void) {
    if (!ExtEquip_IsEnabled()) {
        return 0;
    }
    return gExtEquipState.currentExtShield == 1 || gExtEquipState.currentExtShield == 2;
}

const char* ExtEquip_GetShieldDLOverride(void) {
    if (!ExtEquip_IsEnabled())
        return NULL;

    // Divine (1), Kite (2), Shield of Ikana (3): hide OOT shield, draw custom in PostLimbDraw
    if (gExtEquipState.currentExtShield >= 1 && gExtEquipState.currentExtShield <= 3)
        return "HIDE";

    return NULL;
}

// Cached MM Mirror Shield DLs (loaded once from mm.o2r with hash pre-resolution)
static Gfx* sCachedMmShieldHandDL = NULL;
static Gfx* sCachedMmShieldBackDL = NULL;
static u8 sMmShieldLoadAttempted = 0;

static void ExtEquip_LoadMmShieldDLs(void) {
    if (sMmShieldLoadAttempted)
        return;
    sMmShieldLoadAttempted = 1;

    sCachedMmShieldHandDL =
        (Gfx*)TransformMasks_LoadMmDL("objects/object_link_child/gLinkHumanRightHandHoldingMirrorShieldDL");
    // Use the plain shield DL (no embedded matrix) for back — we control the transform
    sCachedMmShieldBackDL = (Gfx*)TransformMasks_LoadMmDL("objects/object_link_child/gLinkHumanMirrorShieldDL");
}

// Shared draw for the cached MM Mirror Shield DLs (hand + back differ only in
// which cached DL is passed). Drawn on XLU to avoid corrupting the OPA pipeline
// (prevents black tint on the tunic).
static void DrawCachedShieldDL(void* playVoid, Gfx* dl) {
    PlayState* play = (PlayState*)playVoid;

    OPEN_DISPS(play->state.gfxCtx);

    Matrix_Push();
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, dl);
    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx);
}

// Custom soh.o2r shield models (brought in from kite_shield.blend via the
// blend_to_nei -> c2obj_nei pipeline). Cached gated loads.
//   slot 1 = Divine Shield (object_nei_divine_shield)
//   slot 2 = Kite Shield    (object_nei_kite_shield)
static Gfx* ExtEquip_GetCachedDL(const char* otr, Gfx** cache, u8* tried) {
    if (!*tried) {
        *tried = 1;
        if (ResourceMgr_FileExists(otr)) {
            *cache = ResourceMgr_LoadGfxByName(otr);
        }
    }
    return *cache;
}

static Gfx* ExtEquip_GetKiteShieldDL(void) {
    static Gfx* sCached = NULL;
    static u8 sTried = 0;
    return ExtEquip_GetCachedDL("__OTR__objects/object_nei_kite_shield/g_kite_shield_dl", &sCached, &sTried);
}

static Gfx* ExtEquip_GetDivineShieldDL(void) {
    static Gfx* sCached = NULL;
    static u8 sTried = 0;
    return ExtEquip_GetCachedDL("__OTR__objects/object_nei_divine_shield/g_divine_shield_dl", &sCached, &sTried);
}

// Shared transform that seats a custom shield model in Link's shield-limb space.
// The model is drawn relative to the sheath/hand limb matrix, whose LOCAL space is
// huge (~6000 N64 units across — the Hylian shield collider quad size in z_player_lib.c).
// Divine + Kite share this placement (both modeled in the same space).
// Final, visually-tuned values (degrees for rotation, N64 units for offset).
#define CUSTOM_SHIELD_SCALE 44.2f
#define CUSTOM_SHIELD_ROT_X (-95.0f * (M_PI / 180.0f))
#define CUSTOM_SHIELD_ROT_Y (-27.0f * (M_PI / 180.0f))
#define CUSTOM_SHIELD_ROT_Z (-99.0f * (M_PI / 180.0f))
#define CUSTOM_SHIELD_OFF_X (-508.0f)
#define CUSTOM_SHIELD_OFF_Y (-372.0f)
#define CUSTOM_SHIELD_OFF_Z (-5.0f)

static void DrawCustomShieldDL(void* playVoid, Gfx* dl) {
    if (dl == NULL)
        return;

    PlayState* play = (PlayState*)playVoid;
    OPEN_DISPS(play->state.gfxCtx);

    // Drawn on XLU (like the MM Ikana shield): a custom DL leaves its combiner/texture
    // state on the pipe; on OPA that bleeds onto the limbs drawn after it (black tunic).
    // The XLU pass runs after all OPA limbs, so the body stays clean.
    Matrix_Push();
    Matrix_Translate(CUSTOM_SHIELD_OFF_X, CUSTOM_SHIELD_OFF_Y, CUSTOM_SHIELD_OFF_Z, MTXMODE_APPLY);
    Matrix_RotateX(CUSTOM_SHIELD_ROT_X, MTXMODE_APPLY);
    Matrix_RotateY(CUSTOM_SHIELD_ROT_Y, MTXMODE_APPLY);
    Matrix_RotateZ(CUSTOM_SHIELD_ROT_Z, MTXMODE_APPLY);
    Matrix_Scale(CUSTOM_SHIELD_SCALE, CUSTOM_SHIELD_SCALE, CUSTOM_SHIELD_SCALE, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, dl);
    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx);
}

// Hand + back draws share the per-slot model dispatch (onBack picks hand vs sheath
// DL for the MM Mirror Shield; the custom models use one DL for both).
static void ExtEquip_DrawShieldCommon(void* playVoid, u8 onBack) {
    if (!ExtEquip_IsEnabled())
        return;

    // Kite Shield: while shield surfing the board is under his feet (ExtEquip_DrawKiteSurfBoard
    // from the ROOT limb), so neither the hand nor the back copy may draw. Skijer's NEI
    if (KiteSurf_IsRiding())
        return;

    switch (gExtEquipState.currentExtShield) {
        case 1: // Divine Shield: custom soh.o2r model
            DrawCustomShieldDL(playVoid, ExtEquip_GetDivineShieldDL());
            break;
        case 2: // Kite Shield: custom soh.o2r model
            DrawCustomShieldDL(playVoid, ExtEquip_GetKiteShieldDL());
            break;
        case 3: { // Shield of Ikana: MM Mirror Shield from mm.o2r
            ExtEquip_LoadMmShieldDLs();
            Gfx* mmDL = onBack ? sCachedMmShieldBackDL : sCachedMmShieldHandDL;
            if (mmDL != NULL)
                DrawCachedShieldDL(playVoid, mmDL);
            break;
        }
    }
}

void ExtEquip_DrawShieldDL(void* playVoid) {
    ExtEquip_DrawShieldCommon(playVoid, 0);
}

// Draw the ext shield on Link's back (sheath position)
void ExtEquip_DrawShieldBackDL(void* playVoid) {
    ExtEquip_DrawShieldCommon(playVoid, 1);
}

// Kite Shield SHIELD SURFING: the board under Link's feet. Called from Player_PostLimbDraw on
// PLAYER_LIMB_ROOT, so the matrix is his body root rather than the shield limb — hence its own
// transform block instead of the CUSTOM_SHIELD_* one (that space is ~6000 units across, this one
// is not). All six values are CVar-tunable because seating a board by eye is a rebuild each time.
// Skijer's NEI
void ExtEquip_DrawKiteSurfBoard(void* playVoid) {
    Gfx* dl;
    PlayState* play;
    f32 ageScale;

    if (!ExtEquip_IsEnabled() || !KiteSurf_IsRiding())
        return;

    dl = ExtEquip_GetKiteShieldDL();
    if (dl == NULL)
        return;

    // Child Link's limb space is 11/17 of adult's, so one set of adult values serves both ages:
    // offsets and size scale, rotations do not. Exposed as a CVar only so the fraction can be
    // nudged if it ever looks off on a custom player model.
    ageScale = LINK_IS_ADULT ? 1.0f : KSURF_BOARD_CHILD_RATIO;

    play = (PlayState*)playVoid;
    OPEN_DISPS(play->state.gfxCtx);

    // XLU for the same reason DrawCustomShieldDL uses it: a custom DL leaves its combiner state on
    // the pipe and would black out every OPA limb drawn after it.
    Matrix_Push();
    Matrix_Translate(KSURF_BOARD_OFF_X * ageScale, KSURF_BOARD_OFF_Y * ageScale, KSURF_BOARD_OFF_Z * ageScale,
                     MTXMODE_APPLY);

    // Trick spin, AFTER the translate and BEFORE the placement rotations: that makes it turn about
    // the vertical through the board's own centre (a shuvit) instead of swinging it around Link.
    if (sKSurf.boardSpin != 0) {
        Matrix_RotateY(sKSurf.boardSpin * (M_PI / 32768.0f), MTXMODE_APPLY);
    }

    Matrix_RotateZYX((s16)(KSURF_BOARD_ROT_X * 182.04f), (s16)(KSURF_BOARD_ROT_Y * 182.04f),
                     (s16)(KSURF_BOARD_ROT_Z * 182.04f), MTXMODE_APPLY);
    {
        f32 scale = KSURF_BOARD_SCALE * ageScale;

        Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    }
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, dl);
    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx);
}

// Common prologue for the per-piece dispatch wrappers below: bail out unless
// the cheat is enabled AND the given slot is currently equipped with `index`.
// (ExtEquip_GetCurrent returns the same field these used to read directly.)
#define EXT_EQUIP_REQUIRE(type, index)                                 \
    if (!ExtEquip_IsEnabled() || ExtEquip_GetCurrent(type) != (index)) \
    return

// ExtEquip_DrawWaistScale removed — the Water Dragon Scale item (and its waist pendant model) no
// longer exists; Zora swim is the Zora Tunic's permanent effect (equip_dragonscale.c driver).

// ExtEquip_DrawAnklet / ExtEquip_UpdateAnkletPhysics removed (Skijer 2026-07-15): the Pegasus
// Anklet's model is now the RED-recolored vanilla hover boots drawn in Player_DrawImpl.

void ExtEquip_CaptureCapeShoulderPos(s32 limbIndex) {
    // Cape decoupled from the ext-tunic slot (Skijer 2026-07-15): capture whenever the cloth draws.
    if (!ExtEquip_CapeVisible())
        return;

    MagicCape_CaptureShoulderPos(limbIndex);
}

// ExtEquip_DrawBreastplate removed (Skijer 2026-07-16): Spirit Tunic is a recolor tunic now, no armor
// overlay. Kept as an empty stub so the PostLimbDraw call site needs no edit.
void ExtEquip_DrawBreastplate(void* playVoid) {
    (void)playVoid;
}

u8 ExtEquip_IkanaDeathSave(void* playVoid) {
    if (!Ikana_ShouldRevive())
        return 0;

    PlayState* play = (PlayState*)playVoid;
    Ikana_ConsumeDeathSave(play);
    return 1;
}
