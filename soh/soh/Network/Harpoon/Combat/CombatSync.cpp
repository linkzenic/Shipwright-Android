// =============================================================================
// HarpoonCombat — implementation of the cross-gamemode PvP combat layer.
// See CombatSync.h for the public surface + wire protocol overview.
// =============================================================================

#include "CombatSync.h"
#include "../Harpoon.h"

#include <spdlog/spdlog.h>
#include <unordered_map>
#include <algorithm>

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

namespace HarpoonCombat {

namespace {

// ---- Default damage table -----------------------------------------------
//
// Damage values in *hearts* (will be multiplied by 16 to get engine units).
// Negative values are HEAL amounts. 0 = "status effect only / no damage".
// The gamemode YAML overrides any of these via `damage_table:` keys.
//
// The key here matches the canonical name returned by KeyFor() so a
// gamemode tuner can search for "master_sword: N" in the YAML.

struct WeaponMeta {
    HarpoonWeaponId    id;
    const char*        key;
    int8_t             defaultDamage;
    HarpoonElementType element;
};

const WeaponMeta kWeaponTable[] = {
    // Vanilla
    { W_VAN_KOKIRI_SWORD,        "kokiri_sword",        1, ELEMENT_NONE },
    { W_VAN_MASTER_SWORD,        "master_sword",        2, ELEMENT_NONE },
    { W_VAN_BGS,                 "biggoron_sword",      4, ELEMENT_NONE },
    { W_VAN_BROKEN_KNIFE,        "broken_knife",        1, ELEMENT_NONE },
    { W_VAN_DEKU_STICK,          "deku_stick",          1, ELEMENT_NONE },
    { W_VAN_DEKU_NUT,            "deku_nut",            0, ELEMENT_NONE },
    { W_VAN_BOMB,                "bomb",                4, ELEMENT_FIRE },
    { W_VAN_BOMBCHU,             "bombchu",             4, ELEMENT_FIRE },
    { W_VAN_BOW,                 "bow",                 1, ELEMENT_NONE },
    { W_VAN_FIRE_ARROW,          "fire_arrow",          3, ELEMENT_FIRE },
    { W_VAN_ICE_ARROW,           "ice_arrow",           2, ELEMENT_ICE  },
    { W_VAN_LIGHT_ARROW,         "light_arrow",         3, ELEMENT_LIGHT },
    { W_VAN_SLINGSHOT,           "slingshot",           1, ELEMENT_NONE },
    { W_VAN_HOOKSHOT,            "hookshot",            1, ELEMENT_NONE },
    { W_VAN_LONGSHOT,            "longshot",            1, ELEMENT_NONE },
    { W_VAN_BOOMERANG,           "boomerang",           1, ELEMENT_NONE },
    { W_VAN_HAMMER,              "hammer",              6, ELEMENT_NONE },
    { W_VAN_DINS_FIRE,           "dins_fire",           4, ELEMENT_FIRE },
    { W_VAN_FARORES_WIND,        "farores_wind",        0, ELEMENT_NONE },
    { W_VAN_NAYRUS_LOVE,         "nayrus_love",         0, ELEMENT_NONE },

    // Custom
    { W_ITM_SPINNER_RIDE,        "spinner_ride",        0, ELEMENT_NONE },
    { W_ITM_SPINNER_HOMING,      "spinner_homing",      2, ELEMENT_NONE },
    { W_ITM_FIRE_ROD_PROJ,       "fire_rod_proj",       4, ELEMENT_FIRE },
    { W_ITM_FIRE_ROD_SPIN,       "fire_rod_spin",       8, ELEMENT_FIRE },
    { W_ITM_ICE_ROD_PROJ,        "ice_rod_proj",        4, ELEMENT_ICE  },
    { W_ITM_ICE_ROD_WAVE,        "ice_rod_wave",        8, ELEMENT_ICE  },
    { W_ITM_LIGHT_ROD_PROJ,      "light_rod_proj",      4, ELEMENT_LIGHT },
    { W_ITM_LIGHT_ROD_BEAM,      "light_rod_beam",      8, ELEMENT_LIGHT },
    { W_ITM_BALL_AND_CHAIN,      "ball_and_chain",      6, ELEMENT_NONE },
    { W_ITM_BEETLE,              "beetle",              1, ELEMENT_NONE },
    { W_ITM_GUST_JAR_BLOW,       "gust_jar_blow",       0, ELEMENT_WIND },
    { W_ITM_SWITCH_HOOK,         "switch_hook",         0, ELEMENT_NONE },
    { W_ITM_CANE_OF_SOMARIA,     "cane_of_somaria",     0, ELEMENT_NONE },
    { W_ITM_LANTERN_REGULAR,     "lantern_burn_regular", 1, ELEMENT_FIRE },
    { W_ITM_LANTERN_BLUE,        "lantern_burn_blue",   0, ELEMENT_ICE  },
    { W_ITM_LANTERN_POE,         "lantern_burn_poe",    0, ELEMENT_DARK },
    { W_ITM_WHIP,                "whip",                2, ELEMENT_NONE },
    { W_ITM_DEMISE_DIRECT,       "demise_direct",       6, ELEMENT_NONE },
    { W_ITM_DEMISE_AOE,          "demise_aoe",          2, ELEMENT_NONE },
    { W_ITM_PEGASUS_CHARGE,      "pegasus_charge",      4, ELEMENT_NONE },
    { W_ITM_HYLIAS_GRACE_HEAL,   "hylia_grace_heal",   -1, ELEMENT_HEAL },
    { W_ITM_ZONAI_PERMAFROST,    "zonai_permafrost",    0, ELEMENT_ICE  },
    { W_ITM_BOMB_ARROW_DIRECT,   "bomb_arrow_direct",   2, ELEMENT_FIRE },
    { W_ITM_BOMB_ARROW_AOE,      "bomb_arrow_aoe",      4, ELEMENT_FIRE },
    { W_ITM_DEKU_LEAF,           "deku_leaf",           0, ELEMENT_WIND },
    { W_ITM_ROCS_FEATHER,        "rocs_feather",        0, ELEMENT_NONE },
    { W_ITM_ROCS_CAPE,           "rocs_cape",           0, ELEMENT_NONE },

    // Transformations
    { W_FORM_FD_BEAM,            "fd_beam",             4, ELEMENT_LIGHT },
    { W_FORM_FD_SPIN,            "fd_spin",             6, ELEMENT_NONE },
    { W_FORM_GORON_ROLL,         "goron_roll",          2, ELEMENT_NONE },
    { W_FORM_GORON_SPIKE,        "goron_spike",         4, ELEMENT_NONE },
    { W_FORM_ZORA_ELECTRIC,      "zora_electric",       2, ELEMENT_ELECTRIC },
    { W_FORM_ZORA_FIN,           "zora_fin",            3, ELEMENT_NONE },
    { W_FORM_ZORA_DIVE,          "zora_dive",           4, ELEMENT_NONE },
    { W_FORM_DEKU_SPIN,          "deku_spin",           1, ELEMENT_NONE },
    { W_FORM_DEKU_BUBBLE,        "deku_bubble",         1, ELEMENT_NONE },

    // SW97 spells
    { W_SW97_MAGIC_DARK,         "magic_dark_sw97",     0, ELEMENT_DARK  },
    { W_SW97_MAGIC_FIRE,         "magic_fire_sw97",     3, ELEMENT_FIRE  },
    { W_SW97_MAGIC_ICE,          "magic_ice_sw97",      0, ELEMENT_ICE   },
    { W_SW97_MAGIC_LIGHT,        "magic_light_sw97",   -3, ELEMENT_HEAL  },
    { W_SW97_MAGIC_SOUL,         "magic_soul_sw97",     1, ELEMENT_SOUL  },
    { W_SW97_MAGIC_WIND,         "magic_wind_sw97",     0, ELEMENT_WIND  },

    // SW97 arrows
    { W_SW97_ARROW_DARK,         "dark_arrow_sw97",     3, ELEMENT_DARK  },
    { W_SW97_ARROW_FIRE,         "fire_arrow_sw97",     3, ELEMENT_FIRE  },
    { W_SW97_ARROW_ICE,          "ice_arrow_sw97",      2, ELEMENT_ICE   },
    { W_SW97_ARROW_LIGHT,        "light_arrow_sw97",   -3, ELEMENT_HEAL  },
    { W_SW97_ARROW_SOUL,         "soul_arrow_sw97",     1, ELEMENT_SOUL  },
    { W_SW97_ARROW_WIND,         "wind_arrow_sw97",     1, ELEMENT_WIND  },

    // Extended equipment
    { W_EXT_CANE_OF_BYRNA,       "cane_of_byrna",       1, ELEMENT_NONE  },
    { W_EXT_FOUR_SWORD_CLONE,    "four_sword_clone",    2, ELEMENT_NONE  },
    { W_EXT_PENDANT_MORTAL_DRAW, "pendant_mortal_draw", 127, ELEMENT_NONE }, // capped at int8_t max
    { W_EXT_PENDANT_GROUND_POUND,"pendant_ground_pound",4, ELEMENT_NONE  },
    { W_EXT_ZORA_BARRIER_SHOCK,  "zora_barrier_shock",  2, ELEMENT_ELECTRIC },

    // Masks
    { W_MASK_BLAST_BOMB,         "blast_mask",          4, ELEMENT_FIRE  },
};

constexpr size_t kWeaponCount = sizeof(kWeaponTable) / sizeof(kWeaponTable[0]);

// Runtime damage table (overridable by gamemode YAML). Defaults from
// kWeaponTable on init; LoadDamageTable() patches via string key.
std::unordered_map<uint16_t, int8_t> sDamageById;

void InitDefaults() {
    if (!sDamageById.empty()) return;
    for (const auto& w : kWeaponTable) {
        sDamageById[w.id] = w.defaultDamage;
    }
}

// Status durations — overridable.
struct StatusDurations {
    uint16_t burnShortFrames     = 120;
    uint16_t burnLongFrames      = 180;
    uint16_t freezeShortFrames   = 180;
    uint16_t freezeLongFrames    = 300;
    uint16_t blindnessShortFrames = 300;
    uint16_t blindnessLongFrames  = 600;
    uint16_t stunShortFrames     = 20;
    uint16_t stunLongFrames      = 60;
} sDurations;

nlohmann::json Envelope(const char* evt, nlohmann::json data) {
    nlohmann::json p;
    p["type"]       = "ROOM.BROADCAST_EVENT";
    p["event_name"] = evt;
    p["data"]       = std::move(data);
    return p;
}

bool HasInstance() { return Harpoon::Instance != nullptr; }

uint32_t OwnCid() {
    return HasInstance() ? Harpoon::Instance->ownClientId : 0u;
}

bool PvpOn() {
    return HasInstance() && Harpoon::Instance->pvpEnabled;
}

}  // anon

// =========================================================================
// Public API
// =========================================================================

void LoadDamageTable(const nlohmann::json& damageTableJson) {
    InitDefaults();
    if (!damageTableJson.is_object()) return;
    for (const auto& w : kWeaponTable) {
        if (damageTableJson.contains(w.key)) {
            sDamageById[w.id] = (int8_t)damageTableJson.value(w.key, (int)w.defaultDamage);
        }
    }
    SPDLOG_INFO("[HarpoonCombat] damage table loaded ({} entries)",
                (int)sDamageById.size());
}

int8_t DamageFor(HarpoonWeaponId weapon) {
    InitDefaults();
    auto it = sDamageById.find((uint16_t)weapon);
    return it != sDamageById.end() ? it->second : 0;
}

const char* KeyFor(HarpoonWeaponId weapon) {
    for (const auto& w : kWeaponTable) {
        if (w.id == weapon) return w.key;
    }
    return "unknown";
}

HarpoonElementType ElementOf(HarpoonWeaponId weapon) {
    for (const auto& w : kWeaponTable) {
        if (w.id == weapon) return w.element;
    }
    return ELEMENT_NONE;
}

bool IsElementalReflectable(HarpoonWeaponId weapon) {
    // Mirror Shield reflects elemental projectiles. Element types FIRE/ICE/
    // LIGHT/DARK/SOUL/WIND from SW97, plus vanilla Fire/Ice/Light arrows
    // and the rod beams.
    switch (weapon) {
        case W_VAN_FIRE_ARROW:
        case W_VAN_ICE_ARROW:
        case W_VAN_LIGHT_ARROW:
        case W_ITM_FIRE_ROD_PROJ:
        case W_ITM_ICE_ROD_PROJ:
        case W_ITM_LIGHT_ROD_PROJ:
        case W_ITM_LIGHT_ROD_BEAM:
        case W_SW97_MAGIC_DARK:
        case W_SW97_MAGIC_FIRE:
        case W_SW97_MAGIC_ICE:
        case W_SW97_MAGIC_LIGHT:
        case W_SW97_MAGIC_SOUL:
        case W_SW97_MAGIC_WIND:
        case W_SW97_ARROW_DARK:
        case W_SW97_ARROW_FIRE:
        case W_SW97_ARROW_ICE:
        case W_SW97_ARROW_LIGHT:
        case W_SW97_ARROW_SOUL:
        case W_SW97_ARROW_WIND:
        case W_FORM_FD_BEAM:
            return true;
        default:
            return false;
    }
}

// =========================================================================
// Status ticking
// =========================================================================
//
// Local-player status state lives in HarpoonClient (for own client). The
// per-frame ticker decrements timers and applies DOT/heal/freeze effects.
// =========================================================================

void TickLocal() {
    if (!HasInstance() || !gPlayState) return;
    uint32_t cid = OwnCid();
    if (cid == 0) return;
    auto it = Harpoon::Instance->clients.find(cid);
    if (it == Harpoon::Instance->clients.end()) return;
    HarpoonClient& c = it->second;

    // Burn DOT — 1♥/sec while > 0. 20-frame cadence (engine runs at 20 logic
    // frames per second). Apply locally; broadcast a damage tick so peers
    // see HP delta.
    if (c.combatBurnDotFrames > 0) {
        c.combatBurnDotFrames--;
        Player* lp = GET_PLAYER(gPlayState);
        // Spawn 1-2 fire flames around Link every 4 frames so the burning
        // is visually constant for the entire DOT duration (matches
        // Flare Dance's continuous flame loop).
        if (lp != nullptr && (c.combatBurnDotFrames % 4) == 0) {
            Vec3f firePos = lp->actor.world.pos;
            firePos.x += ((rand() % 80) - 40) * 0.4f;
            firePos.y += 10.0f + (rand() % 40);
            firePos.z += ((rand() % 80) - 40) * 0.4f;
            EffectSsEnFire_SpawnVec3f(gPlayState, &lp->actor, &firePos,
                                      60, 0, 0, -1);
        }
        // Panic run: while burning, force Link to keep sprinting forward
        // — same panic mechanic real Zelda enemies use when on fire.
        // We just bump linearVelocity each tick; the engine handles the
        // running animation when speed > walking threshold.
        if (lp != nullptr) {
            if (lp->linearVelocity < 10.0f) lp->linearVelocity = 10.0f;
        }
        if ((c.combatBurnDotFrames % 20) == 0 && gSaveContext.health > 0) {
            gSaveContext.health = (s16)std::max(0, (int)gSaveContext.health - 16);
        }
    }

    // Freeze — engine has its own freezeTimer; we just decrement our mirror.
    if (c.combatFreezeFrames > 0) {
        c.combatFreezeFrames--;
        // Mirror to engine. The engine's actor.freezeTimer auto-decrements.
        Player* lp = GET_PLAYER(gPlayState);
        if (lp != nullptr && lp->actor.freezeTimer < c.combatFreezeFrames) {
            lp->actor.freezeTimer = c.combatFreezeFrames;
        }
    }

    // Blindness — just tick the counter; the BlindnessEffect renderer reads
    // it each frame and draws a black overlay while > 0.
    if (c.combatBlindnessFrames > 0) {
        c.combatBlindnessFrames--;
    }

    // Mask-equip frames countdown (animation duration).
    if (c.combatMaskEquipFrames > 0) {
        c.combatMaskEquipFrames--;
    }

    // Shield raise frames — increment while shielding so the parry window
    // detector can read it. Reset to 0 when not shielding.
    Player* lp = GET_PLAYER(gPlayState);
    if (lp != nullptr) {
        bool shielding = (lp->stateFlags1 & PLAYER_STATE1_SHIELDING) != 0;
        if (shielding) {
            if (c.combatShieldRaiseFrames < 255) c.combatShieldRaiseFrames++;
        } else {
            c.combatShieldRaiseFrames = 0;
        }
    }
}

// =========================================================================
// Broadcast helpers
// =========================================================================

void BroadcastApplyStatus(uint32_t targetCid, HarpoonStatusEffect effect,
                          int16_t amount, uint16_t durationFrames,
                          uint32_t sourceCid) {
    if (!HasInstance()) return;
    nlohmann::json d;
    d["targetCid"]       = targetCid;
    d["effect"]          = (int)effect;
    d["amount"]          = (int)amount;
    d["durationFrames"]  = (int)durationFrames;
    d["sourceCid"]       = sourceCid;
    Harpoon::Instance->SendJsonToRemote(Envelope("COMBAT.APPLY_STATUS", std::move(d)));
}

void BroadcastShieldParry(uint32_t parryingCid, uint32_t attackerCid,
                          HarpoonShieldKind shieldType,
                          HarpoonParryEffect effect,
                          HarpoonWeaponId weaponSource) {
    if (!HasInstance()) return;
    nlohmann::json d;
    d["parryingCid"]   = parryingCid;
    d["attackerCid"]   = attackerCid;
    d["shieldType"]    = (int)shieldType;
    d["effect"]        = (int)effect;
    d["weaponSource"]  = (int)weaponSource;
    Harpoon::Instance->SendJsonToRemote(Envelope("COMBAT.SHIELD_PARRY", std::move(d)));
}

void BroadcastShieldRevive(uint32_t cid, int16_t restoredHealth) {
    if (!HasInstance()) return;
    nlohmann::json d;
    d["cid"]             = cid;
    d["restoredHealth"]  = (int)restoredHealth;
    Harpoon::Instance->SendJsonToRemote(Envelope("COMBAT.SHIELD_REVIVE", std::move(d)));
}

void BroadcastAuraTick(uint32_t ownerCid, HarpoonUtilityHitKind kind,
                       float x, float y, float z) {
    if (!HasInstance()) return;
    nlohmann::json d;
    d["ownerCid"]  = ownerCid;
    d["kind"]      = (int)kind;
    d["x"] = x; d["y"] = y; d["z"] = z;
    Harpoon::Instance->SendJsonToRemote(Envelope("COMBAT.AURA_TICK", std::move(d)));
}

void BroadcastUtilityHit(uint32_t attackerCid, uint32_t targetCid,
                         HarpoonUtilityHitKind kind,
                         int32_t ix, int32_t iy, int32_t iz,
                         float fx, float fy, float fz) {
    if (!HasInstance()) return;
    nlohmann::json d;
    d["attackerCid"] = attackerCid;
    d["targetCid"]   = targetCid;
    d["kind"]        = (int)kind;
    d["ix"] = ix; d["iy"] = iy; d["iz"] = iz;
    d["fx"] = fx; d["fy"] = fy; d["fz"] = fz;
    Harpoon::Instance->SendJsonToRemote(Envelope("COMBAT.UTILITY_HIT", std::move(d)));
}

void BroadcastMaskEquipStart(uint8_t maskId) {
    if (!HasInstance()) return;
    nlohmann::json d;
    d["cid"]    = OwnCid();
    d["maskId"] = (int)maskId;
    Harpoon::Instance->SendJsonToRemote(Envelope("PLAYER.MASK_EQUIP_START", std::move(d)));
}

// =========================================================================
// Receive handlers
// =========================================================================

void HandleApplyStatus(const nlohmann::json& data) {
    if (!HasInstance() || !gPlayState) return;
    uint32_t target = data.value("targetCid", 0u);
    if (target != OwnCid()) return;
    if (!PvpOn()) return;

    HarpoonStatusEffect eff = (HarpoonStatusEffect)data.value("effect", 0);
    int16_t  amount         = (int16_t)data.value("amount", 0);
    uint16_t duration       = (uint16_t)data.value("durationFrames", 0u);
    auto it = Harpoon::Instance->clients.find(target);
    if (it == Harpoon::Instance->clients.end()) return;
    HarpoonClient& c = it->second;

    Player* lp = GET_PLAYER(gPlayState);
    if (lp == nullptr) return;

    switch (eff) {
        case STATUS_BURN_DOT: {
            // Goron form = immune.
            if (c.transformation == 1 /* MM_PLAYER_FORM_GORON */) break;
            c.combatBurnDotFrames = std::max(c.combatBurnDotFrames, (uint16_t)duration);
            // Spawn fire particles around Link + red tint, mirroring the
            // Flare Dance burning effect.
            for (int i = 0; i < 6; i++) {
                Vec3f firePos = lp->actor.world.pos;
                firePos.x += ((rand() % 100) - 50) * 0.4f;
                firePos.y += 10.0f + (rand() % 40);
                firePos.z += ((rand() % 100) - 50) * 0.4f;
                EffectSsEnFire_SpawnVec3f(gPlayState, &lp->actor, &firePos,
                                          80, 0, 0, -1);
            }
            Actor_SetColorFilter(&lp->actor, 0x4000, 0xFF, 0, (s16)duration);
            // Knockback push from the fire hit + small damage on apply.
            func_80837C0C(gPlayState, lp, HARPOON_HIT_RESPONSE_FIRE,
                          5.0f, 6.0f, 0, 20);
            Audio_PlaySoundGeneral(NA_SE_EV_BURNING,
                                   &lp->actor.projectedPos, 4,
                                   &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultReverb);
            break;
        }
        case STATUS_FREEZE: {
            c.combatFreezeFrames = duration;
            // Engine's ICE_TRAP path: encases Link in an ice block, plays
            // the freeze SFX, locks input. Matches Freezard behaviour.
            func_80837C0C(gPlayState, lp, PLAYER_HIT_RESPONSE_ICE_TRAP,
                          0.0f, 0.0f, 0, (s32)duration);
            // Spawn ice crystals around Link for VFX (matches Freezard
            // EnFz_SpawnIceSmokeFreeze on player hit).
            Color_RGBA8 primIce = { 170, 255, 255, 255 };
            Color_RGBA8 envIce  = { 100, 150, 255, 0 };
            for (int i = 0; i < 8; i++) {
                Vec3f icePos = lp->actor.world.pos;
                icePos.x += ((rand() % 100) - 50) * 0.6f;
                icePos.y += (rand() % 60);
                icePos.z += ((rand() % 100) - 50) * 0.6f;
                Vec3f iceVel = { 0.0f, 1.0f, 0.0f };
                Vec3f iceAcc = { 0.0f, -0.1f, 0.0f };
                EffectSsEnIce_Spawn(gPlayState, &icePos, 0.5f, &iceVel,
                                    &iceAcc, &primIce, &envIce, 60);
            }
            Audio_PlaySoundGeneral(NA_SE_PL_FREEZE_S,
                                   &lp->actor.projectedPos, 4,
                                   &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultReverb);
            break;
        }
        case STATUS_BLINDNESS: {
            c.combatBlindnessFrames = duration;
            // Play the dark spell cast SFX (boss laugh / dark aura) so
            // the player knows blindness has started.
            Audio_PlaySoundGeneral(NA_SE_EN_FANTOM_LAUGH,
                                   &lp->actor.projectedPos, 4,
                                   &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultReverb);
            break;
        }
        case STATUS_HEAL: {
            // Amount in damage-units (16 = 1 heart). Negative = heal amount.
            int16_t heal = amount < 0 ? (int16_t)(-amount) : amount;
            gSaveContext.health = (s16)std::min(
                (int)gSaveContext.healthCapacity,
                (int)gSaveContext.health + heal * 16);
            break;
        }
        case STATUS_DRAIN: {
            // Drain N hearts from local player; the source player heals.
            int16_t drain = amount > 0 ? amount : (int16_t)-amount;
            gSaveContext.health = (s16)std::max(0, (int)gSaveContext.health - drain * 16);
            break;
        }
        case STATUS_STUN: {
            lp->actor.freezeTimer = (s16)duration;
            Actor_SetColorFilter(&lp->actor, 0, 0xFF, 0, (s16)duration);
            break;
        }
        case STATUS_INVISIBILITY:
            // Local invis is owned by mask system; nothing to do.
            break;
        case STATUS_PUSH:
        case STATUS_NONE:
        default:
            break;
    }
}

void HandleShieldParry(const nlohmann::json& data) {
    if (!HasInstance() || !gPlayState) return;
    uint32_t parryingCid = data.value("parryingCid", 0u);
    uint32_t attackerCid = data.value("attackerCid", 0u);
    HarpoonShieldKind shieldType = (HarpoonShieldKind)data.value("shieldType", 0);
    HarpoonParryEffect effect    = (HarpoonParryEffect)data.value("effect", 0);
    HarpoonWeaponId weaponSource = (HarpoonWeaponId)data.value("weaponSource",
                                                                (int)HARPOON_WEAPON_UNKNOWN);
    (void)shieldType; (void)weaponSource;

    if (!PvpOn()) return;

    switch (effect) {
        case PARRY_FREEZE_AOE: {
            // Divine Shield AOE — peers within radius of parry-er get frozen.
            // The parry-er broadcasted this; if we're within range of the
            // parry-er's player actor (live position), freeze us.
            if (attackerCid == OwnCid() || parryingCid == OwnCid()) {
                // Skip self-freeze of the attacker (already applied by Divine
                // Shield local AOE) or parry-er.
                break;
            }
            // Distance check
            auto pit = Harpoon::Instance->clients.find(parryingCid);
            if (pit == Harpoon::Instance->clients.end()) break;
            Player* lp = GET_PLAYER(gPlayState);
            if (lp == nullptr) break;
            f32 dx = lp->actor.world.pos.x - pit->second.posRot.pos.x;
            f32 dz = lp->actor.world.pos.z - pit->second.posRot.pos.z;
            if (dx * dx + dz * dz <= 150.0f * 150.0f) {
                lp->actor.freezeTimer = 40;
                Actor_SetColorFilter(&lp->actor, 0, 0xFF, 0, 40);
            }
            break;
        }
        case PARRY_SOUL_DRAIN: {
            // Ikana — attacker takes 1/4 heart; parry-er heals 1/2 heart.
            if (attackerCid == OwnCid()) {
                gSaveContext.health = (s16)std::max(0, (int)gSaveContext.health - 4);
            }
            if (parryingCid == OwnCid()) {
                gSaveContext.health = (s16)std::min(
                    (int)gSaveContext.healthCapacity, (int)gSaveContext.health + 8);
            }
            break;
        }
        case PARRY_REFLECT:
            // Handled in ProjectileMirror::HandleReflect.
            break;
        case PARRY_STAGGER:
            // Generic stun on attacker.
            if (attackerCid == OwnCid()) {
                Player* lp = GET_PLAYER(gPlayState);
                if (lp != nullptr) {
                    lp->actor.freezeTimer = 15;
                    Actor_SetColorFilter(&lp->actor, 0, 0xFF, 0, 15);
                }
            }
            break;
        default:
            break;
    }
}

void HandleShieldRevive(const nlohmann::json& data) {
    if (!HasInstance() || !gPlayState) return;
    uint32_t cid = data.value("cid", 0u);
    int16_t restored = (int16_t)data.value("restoredHealth", 48);
    if (cid != OwnCid()) return;
    gSaveContext.health = restored;
}

void HandleAuraTick(const nlohmann::json& data) {
    // The aura tick is mostly visual on receive — peers can render the
    // VFX at the given position. Damage application is via UTILITY_HIT.
    (void)data;
}

void HandleUtilityHit(const nlohmann::json& data) {
    if (!HasInstance() || !gPlayState) return;
    uint32_t attackerCid = data.value("attackerCid", 0u);
    uint32_t targetCid   = data.value("targetCid", 0u);
    HarpoonUtilityHitKind kind = (HarpoonUtilityHitKind)data.value("kind", 0);
    if (!PvpOn()) return;

    Player* lp = GET_PLAYER(gPlayState);
    if (lp == nullptr) return;
    bool weAreTarget   = (targetCid   == OwnCid());
    bool weAreAttacker = (attackerCid == OwnCid());

    auto getAttackerClient = [&]() -> HarpoonClient* {
        auto it = Harpoon::Instance->clients.find(attackerCid);
        return (it != Harpoon::Instance->clients.end()) ? &it->second : nullptr;
    };
    auto getTargetClient = [&]() -> HarpoonClient* {
        auto it = Harpoon::Instance->clients.find(targetCid);
        return (it != Harpoon::Instance->clients.end()) ? &it->second : nullptr;
    };

    switch (kind) {
        case UTIL_HOOKSHOT_PULL_TARGET: {
            // Target was pulled to attacker. If we're the target, snap to
            // the attacker's position.
            if (weAreTarget) {
                HarpoonClient* a = getAttackerClient();
                if (a != nullptr) {
                    lp->actor.world.pos = a->posRot.pos;
                }
            }
            break;
        }
        case UTIL_HOOKSHOT_PULL_SELF: {
            // Attacker pulled to target (iron-boots inversion). If we're
            // the attacker, snap to target.
            if (weAreAttacker) {
                HarpoonClient* t = getTargetClient();
                if (t != nullptr) {
                    lp->actor.world.pos = t->posRot.pos;
                }
            }
            break;
        }
        case UTIL_SWITCH_HOOK_SWAP: {
            // Both peers swap. We swap with the other party.
            if (weAreTarget) {
                HarpoonClient* a = getAttackerClient();
                if (a != nullptr) lp->actor.world.pos = a->posRot.pos;
            } else if (weAreAttacker) {
                HarpoonClient* t = getTargetClient();
                if (t != nullptr) lp->actor.world.pos = t->posRot.pos;
            }
            break;
        }
        case UTIL_GUST_BLOW: {
            // Push target 80 u away from attacker.
            if (weAreTarget) {
                HarpoonClient* a = getAttackerClient();
                if (a != nullptr) {
                    f32 dx = lp->actor.world.pos.x - a->posRot.pos.x;
                    f32 dz = lp->actor.world.pos.z - a->posRot.pos.z;
                    f32 len = sqrtf(dx * dx + dz * dz);
                    if (len > 0.01f) {
                        lp->actor.world.pos.x += dx / len * 80.0f;
                        lp->actor.world.pos.z += dz / len * 80.0f;
                    }
                }
            }
            break;
        }
        case UTIL_LANTERN_REVEAL: {
            // Force-visible 3 s if local has Stone Mask invis active.
            if (weAreTarget) {
                auto* t = getTargetClient();
                if (t != nullptr) {
                    // Suppress invis briefly — implementation-defined; the
                    // mask wear system reads this flag.
                    t->combatInvisSuppressFrames = 60;
                }
            }
            break;
        }
        case UTIL_FAIRY_HEAL_TOUCH: {
            if (weAreTarget) {
                gSaveContext.health = (s16)std::min(
                    (int)gSaveContext.healthCapacity,
                    (int)gSaveContext.health + 16);
            }
            break;
        }
        case UTIL_ZORA_BARRIER_SHOCK: {
            // Apply 2♥ + stun if local is the target.
            if (weAreTarget) {
                gSaveContext.health = (s16)std::max(0, (int)gSaveContext.health - 32);
                lp->actor.freezeTimer = 30;
                Actor_SetColorFilter(&lp->actor, 0, 0xFF, 0, 30);
            }
            break;
        }
        case UTIL_CANE_CUBE_SPAWN:
            // Handled by projectile-mirror (cube as a tracked actor).
            break;
        default:
            break;
    }
}

void HandleMaskEquipStart(const nlohmann::json& data) {
    if (!HasInstance()) return;
    uint32_t cid = data.value("cid", 0u);
    uint8_t maskId = (uint8_t)data.value("maskId", 0);
    auto it = Harpoon::Instance->clients.find(cid);
    if (it == Harpoon::Instance->clients.end()) return;
    it->second.combatMaskEquipFrames = 120;  // 2-second don animation
    (void)maskId;  // The mask itself is in HarpoonClient::wornMask already.
}

// =========================================================================
// Helpers
// =========================================================================

bool IsLocalPlayerActor(Actor* actor) {
    if (actor == nullptr || gPlayState == nullptr) return false;
    Player* lp = GET_PLAYER(gPlayState);
    return (actor == &lp->actor);
}

bool IsRemoteProjectile(Actor* actor) {
    if (actor == nullptr) return false;
    // Bit 15 of params marks remote-mirrored projectiles. The
    // ProjectileMirror module sets this bit when spawning.
    return (actor->params & 0x8000) != 0;
}

void ApplyLocalHit(HarpoonWeaponId weapon, uint32_t attackerCid) {
    if (!HasInstance() || !gPlayState) return;
    if (!PvpOn()) return;
    Player* lp = GET_PLAYER(gPlayState);
    if (lp == nullptr) return;
    int8_t dmg = DamageFor(weapon);

    // Heal-class
    if (dmg < 0) {
        gSaveContext.health = (s16)std::min(
            (int)gSaveContext.healthCapacity,
            (int)gSaveContext.health + (-dmg) * 16);
        return;
    }

    // Apply damage via existing engine path.
    if (dmg > 0) {
        gSaveContext.health = (s16)std::max(0, (int)gSaveContext.health - dmg * 16);
    }

    // Status effects per weapon — duration knobs from sDurations.
    auto& d = sDurations;
    auto cidIt = Harpoon::Instance->clients.find(OwnCid());
    HarpoonClient* c = (cidIt != Harpoon::Instance->clients.end()) ? &cidIt->second : nullptr;

    switch (weapon) {
        case W_SW97_MAGIC_FIRE:
        case W_SW97_ARROW_FIRE:
        case W_VAN_FIRE_ARROW:
        case W_ITM_FIRE_ROD_PROJ: {
            if (c && c->transformation != 1 /* Goron */) {
                c->combatBurnDotFrames = weapon == W_SW97_MAGIC_FIRE ? d.burnLongFrames
                                                                    : d.burnShortFrames;
            }
            break;
        }
        case W_SW97_MAGIC_ICE:
        case W_VAN_ICE_ARROW:
        case W_SW97_ARROW_ICE:
        case W_ITM_ZONAI_PERMAFROST: {
            uint16_t fr = (weapon == W_SW97_MAGIC_ICE || weapon == W_ITM_ZONAI_PERMAFROST)
                              ? d.freezeLongFrames : d.freezeShortFrames;
            if (c) c->combatFreezeFrames = fr;
            lp->actor.freezeTimer = fr;
            Actor_SetColorFilter(&lp->actor, 0, 0xFF, 0, (s16)fr);
            break;
        }
        case W_SW97_MAGIC_DARK: {
            if (c) c->combatBlindnessFrames = d.blindnessLongFrames;
            break;
        }
        case W_SW97_ARROW_DARK: {
            if (c) c->combatBlindnessFrames = d.blindnessShortFrames;
            break;
        }
        case W_FORM_ZORA_ELECTRIC:
        case W_EXT_ZORA_BARRIER_SHOCK: {
            lp->actor.freezeTimer = 30;
            Actor_SetColorFilter(&lp->actor, 0, 0xFF, 0, 30);
            break;
        }
        case W_FORM_DEKU_BUBBLE:
        case W_VAN_BOOMERANG:
        case W_ITM_BEETLE:
        case W_VAN_DEKU_NUT: {
            lp->actor.freezeTimer = d.stunShortFrames;
            Actor_SetColorFilter(&lp->actor, 0, 0xFF, 0, d.stunShortFrames);
            break;
        }
        default:
            break;
    }
    (void)attackerCid;
}

// ---------------------------------------------------------------------------
// SW97 actor-id lookups + weapon-source-to-status mapping
// ---------------------------------------------------------------------------

extern "C" {
extern s16 gSw97ActorId_MagicFire;
extern s16 gSw97ActorId_MagicIce;
extern s16 gSw97ActorId_MagicLight;
extern s16 gSw97ActorId_MagicDark;
extern s16 gSw97ActorId_MagicSoul;
extern s16 gSw97ActorId_MagicWind;
extern s16 gSw97ActorId_ArrowFire;
extern s16 gSw97ActorId_ArrowIce;
extern s16 gSw97ActorId_ArrowLight;
extern s16 gSw97ActorId_ArrowDark;
extern s16 gSw97ActorId_ArrowSoul;
extern s16 gSw97ActorId_ArrowWind;
}

void ApplyStatusFromAttacker(uint32_t targetCid, Actor* attacker) {
    if (attacker == nullptr || Harpoon::Instance == nullptr) return;
    s16 id = attacker->id;

    // Magic spells — wide-area / heavy
    if (id == gSw97ActorId_MagicFire) {
        BroadcastApplyStatus(targetCid, STATUS_BURN_DOT, 1, 180,
                              Harpoon::Instance->ownClientId);
    } else if (id == gSw97ActorId_MagicIce) {
        BroadcastApplyStatus(targetCid, STATUS_FREEZE, 0, 300,
                              Harpoon::Instance->ownClientId);
    } else if (id == gSw97ActorId_MagicDark) {
        BroadcastApplyStatus(targetCid, STATUS_BLINDNESS, 0, 600,
                              Harpoon::Instance->ownClientId);
    } else if (id == gSw97ActorId_MagicLight) {
        // Light = HEAL; reverse the damage that was forwarded by the
        // damage path. The target peer applies a +3 heart heal instead.
        BroadcastApplyStatus(targetCid, STATUS_HEAL, 3 * 16, 0,
                              Harpoon::Instance->ownClientId);
    } else if (id == gSw97ActorId_MagicSoul) {
        BroadcastApplyStatus(targetCid, STATUS_DRAIN, 16, 0,
                              Harpoon::Instance->ownClientId);
    } else if (id == gSw97ActorId_MagicWind) {
        BroadcastApplyStatus(targetCid, STATUS_PUSH, 0, 30,
                              Harpoon::Instance->ownClientId);
    }
    // Elemental arrows — narrower windows
    else if (id == gSw97ActorId_ArrowFire) {
        BroadcastApplyStatus(targetCid, STATUS_BURN_DOT, 1, 120,
                              Harpoon::Instance->ownClientId);
    } else if (id == gSw97ActorId_ArrowIce) {
        BroadcastApplyStatus(targetCid, STATUS_FREEZE, 0, 180,
                              Harpoon::Instance->ownClientId);
    } else if (id == gSw97ActorId_ArrowDark) {
        BroadcastApplyStatus(targetCid, STATUS_BLINDNESS, 0, 300,
                              Harpoon::Instance->ownClientId);
    } else if (id == gSw97ActorId_ArrowLight) {
        BroadcastApplyStatus(targetCid, STATUS_HEAL, 3 * 16, 0,
                              Harpoon::Instance->ownClientId);
    } else if (id == gSw97ActorId_ArrowSoul) {
        BroadcastApplyStatus(targetCid, STATUS_DRAIN, 16, 0,
                              Harpoon::Instance->ownClientId);
    } else if (id == gSw97ActorId_ArrowWind) {
        BroadcastApplyStatus(targetCid, STATUS_PUSH, 0, 20,
                              Harpoon::Instance->ownClientId);
    }
}

}  // namespace HarpoonCombat

// =========================================================================
// C-linkage bridges — for invocation from C source in soh/mods/.
// Each wraps the corresponding C++ broadcast helper with default args so
// the call site is one line of C code.
// =========================================================================

extern "C" {

// Shield parry broadcast bridge. shieldType + effect are passed as ints
// (the underlying enums are uint8_t but C can't see them).
void HarpoonCombat_BroadcastShieldParry_C(int shieldType, int effect) {
    if (Harpoon::Instance == nullptr) return;
    uint32_t parryingCid = Harpoon::Instance->ownClientId;
    HarpoonCombat::BroadcastShieldParry(
        parryingCid, 0u /* attackerCid unknown at parry time */,
        (HarpoonCombat::HarpoonShieldKind)shieldType,
        (HarpoonCombat::HarpoonParryEffect)effect,
        HarpoonCombat::HARPOON_WEAPON_UNKNOWN);
}

// Shield revive broadcast — Ikana once-per-scene death save.
void HarpoonCombat_BroadcastShieldRevive_C(int restoredHealth) {
    if (Harpoon::Instance == nullptr) return;
    HarpoonCombat::BroadcastShieldRevive(
        Harpoon::Instance->ownClientId, (int16_t)restoredHealth);
}

// Mask equip start broadcast — called when local player begins the
// don-animation for a transformation mask.
void HarpoonCombat_BroadcastMaskEquipStart_C(int maskId) {
    HarpoonCombat::BroadcastMaskEquipStart((uint8_t)maskId);
}

// Apply a hit-by-weapon to the LOCAL player. Used by per-form attack
// hooks where the dummy collision didn't trigger the existing damage
// path (e.g. Goron roll contact in HarpoonHookHandlers).
void HarpoonCombat_ApplyLocalHit_C(int weaponId, unsigned int attackerCid) {
    HarpoonCombat::ApplyLocalHit(
        (HarpoonCombat::HarpoonWeaponId)weaponId, attackerCid);
}

// Broadcast a status effect to a remote target (burn DOT, freeze,
// blindness, heal, drain). Most-impactful for form attacks + per-item
// hits that the existing damage path doesn't carry status info for.
void HarpoonCombat_BroadcastApplyStatus_C(unsigned int targetCid, int effect,
                                           int amount, int durationFrames) {
    if (Harpoon::Instance == nullptr) return;
    HarpoonCombat::BroadcastApplyStatus(
        targetCid,
        (HarpoonCombat::HarpoonStatusEffect)effect,
        (int16_t)amount, (uint16_t)durationFrames,
        Harpoon::Instance->ownClientId);
}

// Broadcast a utility hit (hookshot pull, switch hook swap, gust blow,
// fairy heal touch, zora barrier shock, lantern reveal).
void HarpoonCombat_BroadcastUtilityHit_C(unsigned int targetCid, int kind,
                                         float fx, float fy, float fz) {
    if (Harpoon::Instance == nullptr) return;
    HarpoonCombat::BroadcastUtilityHit(
        Harpoon::Instance->ownClientId, targetCid,
        (HarpoonCombat::HarpoonUtilityHitKind)kind,
        0, 0, 0, fx, fy, fz);
}

}  // extern "C"
