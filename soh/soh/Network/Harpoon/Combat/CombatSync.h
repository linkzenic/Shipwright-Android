#ifndef SOH_NETWORK_HARPOON_COMBAT_COMBAT_SYNC_H
#define SOH_NETWORK_HARPOON_COMBAT_COMBAT_SYNC_H
#ifdef __cplusplus

// =============================================================================
// HarpoonCombat — cross-gamemode PvP combat layer.
//
// Provides:
//   - A 16-bit HarpoonWeaponId enum covering every PvP damage source in the
//     fork (vanilla items, transformations, SW97 spells/arrows, custom items,
//     extended equipment, masks).
//   - A data-driven damage table loaded from gamemode.yaml `damage_table:`.
//   - Status effects (burn DOT, freeze, blindness, heal, drain, knockback)
//     with broadcast + apply round-trip.
//   - Broadcast helpers for shield parries, projectile spawn/hit/reflect,
//     utility hits (hookshot pull, switch swap, gust blow, cane cube, lantern
//     reveal, fairy heal, zora barrier shock), mask-equip animations.
//
// Wire protocol (all `ROOM.BROADCAST_EVENT`):
//   COMBAT.APPLY_STATUS  { targetCid, effect, amount, durationFrames, sourceCid }
//   COMBAT.SHIELD_PARRY  { parryingCid, attackerCid, shieldType, effect, weaponSource }
//   COMBAT.SHIELD_REVIVE { cid, restoredHealth }
//   COMBAT.AURA_TICK     { ownerCid, kind, posXYZ }
//   COMBAT.UTILITY_HIT   { attackerCid, targetCid, kind, ix, iy, iz, fx, fy, fz }
//   PLAYER.MASK_EQUIP_START { cid, maskId }
//
// COMBAT.DEAL_DAMAGE is extended (backward-compatibly) with optional
// `weaponSource` (u16) and `statusDurationFrames` (u16) fields.
// =============================================================================

#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

extern "C" {
#include "z64.h"
}

namespace HarpoonCombat {

// ---- Weapon identifier --------------------------------------------------
//
// Categories are encoded in the high byte; the low byte enumerates within
// the category. Old `COMBAT.DEAL_DAMAGE` packets without a weaponSource
// field default to HARPOON_WEAPON_UNKNOWN.
// -------------------------------------------------------------------------

enum HarpoonWeaponId : uint16_t {
    HARPOON_WEAPON_UNKNOWN = 0xFFFF,

    // 0x00xx — Vanilla
    W_VAN_KOKIRI_SWORD       = 0x0001,
    W_VAN_MASTER_SWORD       = 0x0002,
    W_VAN_BGS                = 0x0003,
    W_VAN_BROKEN_KNIFE       = 0x0004,
    W_VAN_DEKU_STICK         = 0x0005,
    W_VAN_DEKU_NUT           = 0x0006,
    W_VAN_BOMB               = 0x0007,
    W_VAN_BOMBCHU            = 0x0008,
    W_VAN_BOW                = 0x0009,
    W_VAN_FIRE_ARROW         = 0x000A,
    W_VAN_ICE_ARROW          = 0x000B,
    W_VAN_LIGHT_ARROW        = 0x000C,
    W_VAN_SLINGSHOT          = 0x000D,
    W_VAN_HOOKSHOT           = 0x000E,
    W_VAN_LONGSHOT           = 0x000F,
    W_VAN_BOOMERANG          = 0x0010,
    W_VAN_HAMMER             = 0x0011,
    W_VAN_DINS_FIRE          = 0x0012,
    W_VAN_FARORES_WIND       = 0x0013,
    W_VAN_NAYRUS_LOVE        = 0x0014,

    // 0x01xx — Custom items (Page 2)
    W_ITM_SPINNER_RIDE        = 0x0100,
    W_ITM_SPINNER_HOMING      = 0x0101,
    W_ITM_FIRE_ROD_PROJ       = 0x0102,
    W_ITM_FIRE_ROD_SPIN       = 0x0103,
    W_ITM_ICE_ROD_PROJ        = 0x0104,
    W_ITM_ICE_ROD_WAVE        = 0x0105,
    W_ITM_LIGHT_ROD_PROJ      = 0x0106,
    W_ITM_LIGHT_ROD_BEAM      = 0x0107,
    W_ITM_BALL_AND_CHAIN      = 0x0108,
    W_ITM_BEETLE              = 0x0109,
    W_ITM_GUST_JAR_BLOW       = 0x010A,
    W_ITM_SWITCH_HOOK         = 0x010B,
    W_ITM_CANE_OF_SOMARIA     = 0x010C,
    W_ITM_LANTERN_REGULAR     = 0x010D,
    W_ITM_LANTERN_BLUE        = 0x010E,
    W_ITM_LANTERN_POE         = 0x010F,
    W_ITM_WHIP                = 0x0110,
    W_ITM_DEMISE_DIRECT       = 0x0111,
    W_ITM_DEMISE_AOE          = 0x0112,
    W_ITM_PEGASUS_CHARGE      = 0x0113,
    W_ITM_HYLIAS_GRACE_HEAL   = 0x0114,
    W_ITM_ZONAI_PERMAFROST    = 0x0115,
    W_ITM_BOMB_ARROW_DIRECT   = 0x0116,
    W_ITM_BOMB_ARROW_AOE      = 0x0117,
    W_ITM_DEKU_LEAF           = 0x0118,
    W_ITM_ROCS_FEATHER        = 0x0119,
    W_ITM_ROCS_CAPE           = 0x011A,

    // 0x02xx — Transformations
    W_FORM_FD_BEAM            = 0x0200,
    W_FORM_FD_SPIN            = 0x0201,
    W_FORM_GORON_ROLL         = 0x0202,
    W_FORM_GORON_SPIKE        = 0x0203,
    W_FORM_ZORA_ELECTRIC      = 0x0204,
    W_FORM_ZORA_FIN           = 0x0205,
    W_FORM_ZORA_DIVE          = 0x0206,
    W_FORM_DEKU_SPIN          = 0x0207,
    W_FORM_DEKU_BUBBLE        = 0x0208,

    // 0x03xx — SW97 Spells
    W_SW97_MAGIC_DARK         = 0x0300,
    W_SW97_MAGIC_FIRE         = 0x0301,
    W_SW97_MAGIC_ICE          = 0x0302,
    W_SW97_MAGIC_LIGHT        = 0x0303,
    W_SW97_MAGIC_SOUL         = 0x0304,
    W_SW97_MAGIC_WIND         = 0x0305,

    // 0x04xx — SW97 Arrows
    W_SW97_ARROW_DARK         = 0x0400,
    W_SW97_ARROW_FIRE         = 0x0401,
    W_SW97_ARROW_ICE          = 0x0402,
    W_SW97_ARROW_LIGHT        = 0x0403,
    W_SW97_ARROW_SOUL         = 0x0404,
    W_SW97_ARROW_WIND         = 0x0405,

    // 0x05xx — Extended equipment
    W_EXT_CANE_OF_BYRNA       = 0x0500,
    W_EXT_FOUR_SWORD_CLONE    = 0x0501,
    W_EXT_PENDANT_MORTAL_DRAW = 0x0502,
    W_EXT_PENDANT_GROUND_POUND = 0x0503,
    W_EXT_ZORA_BARRIER_SHOCK  = 0x0504,

    // 0x06xx — Masks
    W_MASK_BLAST_BOMB         = 0x0600,
};

// ---- Element type (for shield reflection / status routing) --------------
enum HarpoonElementType : uint8_t {
    ELEMENT_NONE     = 0,
    ELEMENT_FIRE     = 1,
    ELEMENT_ICE      = 2,
    ELEMENT_LIGHT    = 3,
    ELEMENT_DARK     = 4,
    ELEMENT_SOUL     = 5,
    ELEMENT_WIND     = 6,
    ELEMENT_ELECTRIC = 7,
    ELEMENT_HEAL     = 8,
};

// ---- Status effect kinds ------------------------------------------------
enum HarpoonStatusEffect : uint8_t {
    STATUS_NONE      = 0,
    STATUS_BURN_DOT  = 1,
    STATUS_FREEZE    = 2,
    STATUS_BLINDNESS = 3,
    STATUS_HEAL      = 4,
    STATUS_DRAIN     = 5,
    STATUS_STUN      = 6,
    STATUS_PUSH      = 7,
    STATUS_INVISIBILITY = 8,
};

// ---- Shield kinds for parry routing -------------------------------------
enum HarpoonShieldKind : uint8_t {
    SHIELD_NONE      = 0,
    SHIELD_DEKU      = 1,
    SHIELD_HYLIAN    = 2,
    SHIELD_MIRROR    = 3,
    SHIELD_DIVINE    = 4,
    SHIELD_IKANA     = 5,
    SHIELD_GERUDO    = 6,
};

enum HarpoonParryEffect : uint8_t {
    PARRY_NONE       = 0,
    PARRY_FREEZE_AOE = 1,   // Divine Shield
    PARRY_SOUL_DRAIN = 2,   // Shield of Ikana
    PARRY_REFLECT    = 3,   // Mirror Shield
    PARRY_STAGGER    = 4,   // generic / Hylian
};

enum HarpoonUtilityHitKind : uint8_t {
    UTIL_NONE              = 0,
    UTIL_HOOKSHOT_PULL_TARGET = 1, // target yanked to attacker
    UTIL_HOOKSHOT_PULL_SELF   = 2, // attacker yanked to target (iron-boots inversion)
    UTIL_SWITCH_HOOK_SWAP  = 3,
    UTIL_GUST_BLOW         = 4,
    UTIL_CANE_CUBE_SPAWN   = 5,
    UTIL_LANTERN_REVEAL    = 6,
    UTIL_FAIRY_HEAL_TOUCH  = 7,
    UTIL_ZORA_BARRIER_SHOCK = 8,
};

// =========================================================================
// Lifecycle / module-level
// =========================================================================

// Load the gamemode-driven damage table from a `damage_table:` JSON object
// (parsed out of the gamemode manifest). Missing keys fall back to built-in
// defaults. Negative values are interpreted as HEAL amounts.
void LoadDamageTable(const nlohmann::json& damageTableJson);

// Lookup damage value for a given weapon. Returns 0 if the weapon isn't in
// the table — callers should still apply status effects in that case.
int8_t DamageFor(HarpoonWeaponId weapon);

// Lookup canonical YAML string key for a weapon (for diagnostics + table
// lookup). Returns "unknown" for unmapped IDs.
const char* KeyFor(HarpoonWeaponId weapon);

// Returns true if the given weapon is an elemental projectile that the
// Mirror Shield should reflect.
bool IsElementalReflectable(HarpoonWeaponId weapon);

// Returns the element type for a weapon (used by reflect, freeze ticks, etc.)
HarpoonElementType ElementOf(HarpoonWeaponId weapon);

// Called once per game frame from OnGameFrameUpdate. Walks the local
// status timers (burnDotFrames, freezeFrames, blindnessFrames) and
// applies/decrements them.
void TickLocal();

// Renders the blindness overlay on top of the screen. Called once per
// frame from OnGameFrameUpdate. Lives in BlindnessEffect.cpp.
void BlindnessEffect_Draw();

// =========================================================================
// Broadcast helpers (call from local-player attack resolution)
// =========================================================================

// Inspect the AT attacker actor + apply the appropriate status effect to
// the target peer based on weapon class. SW97 fire arrow -> burn DOT,
// SW97 ice arrow -> freeze, SW97 dark arrow -> blindness, SW97 light
// arrow -> heal target, SW97 soul arrow -> drain. No-op for vanilla
// weapons (the existing damage path is sufficient).
// NOTE: caller must have the global `Actor` type already in scope (via
// z64.h or similar) — same convention as IsLocalPlayerActor below.
void ApplyStatusFromAttacker(uint32_t targetCid, Actor* attacker);

// Apply a status effect to a remote player. Broadcasts COMBAT.APPLY_STATUS.
// `amount` is in damage units (16 per heart); negative = heal.
void BroadcastApplyStatus(uint32_t targetCid, HarpoonStatusEffect effect,
                          int16_t amount, uint16_t durationFrames,
                          uint32_t sourceCid);

// Broadcast a shield parry event (Divine AOE freeze, Ikana soul drain,
// Mirror reflect, generic stagger).
void BroadcastShieldParry(uint32_t parryingCid, uint32_t attackerCid,
                          HarpoonShieldKind shieldType,
                          HarpoonParryEffect effect,
                          HarpoonWeaponId weaponSource);

// Broadcast Ikana death-save activation (3♥ revive with dark-purple flash).
void BroadcastShieldRevive(uint32_t cid, int16_t restoredHealth);

// Broadcast an aura tick (Zora Barrier shock, lantern reveal field, etc.)
// so peers can render the visual + apply contact effects locally.
void BroadcastAuraTick(uint32_t ownerCid, HarpoonUtilityHitKind kind,
                       float x, float y, float z);

// Broadcast a utility-item hostile interaction (hookshot pull, switch swap,
// gust blow, cane cube spawn, lantern reveal, fairy heal-touch, zora
// barrier shock).
void BroadcastUtilityHit(uint32_t attackerCid, uint32_t targetCid,
                         HarpoonUtilityHitKind kind,
                         int32_t ix, int32_t iy, int32_t iz,
                         float fx, float fy, float fz);

// Broadcast the mask-equip cutscene start so peers can play the donning
// animation in sync.
void BroadcastMaskEquipStart(uint8_t maskId);

// =========================================================================
// Receive handlers (called from Harpoon::HandlePacket_RoomEvent dispatch)
// =========================================================================

void HandleApplyStatus(const nlohmann::json& data);
void HandleShieldParry(const nlohmann::json& data);
void HandleShieldRevive(const nlohmann::json& data);
void HandleAuraTick(const nlohmann::json& data);
void HandleUtilityHit(const nlohmann::json& data);
void HandleMaskEquipStart(const nlohmann::json& data);

// =========================================================================
// Helpers used by the dummy player + form-attack hooks
// =========================================================================

// Returns true if the given actor is the LOCAL player (not a remote dummy).
// Used to gate per-equipment visual effects (Four Sword clones, Pegasus
// cone) so they don't bleed onto remote dummy players.
bool IsLocalPlayerActor(Actor* actor);

// Returns true if the given Actor* is a remote-mirrored projectile actor
// (one spawned by ProjectileMirror::HandleSpawn). These actors must not
// fire their own damage events — the OWNER fires PROJECTILE_HIT.
bool IsRemoteProjectile(Actor* actor);

// Apply a hit from a known weapon source to the LOCAL player. Resolves the
// damage table value, picks the right damageEffect knockback, applies
// status effects if any. Called from the dummy player's collision-resolved
// hit path AND from HandleApplyStatus on receipt.
void ApplyLocalHit(HarpoonWeaponId weapon, uint32_t attackerCid);

}  // namespace HarpoonCombat

#endif  // __cplusplus
#endif  // SOH_NETWORK_HARPOON_COMBAT_COMBAT_SYNC_H
