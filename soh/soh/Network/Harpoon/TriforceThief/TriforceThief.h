#ifndef SOH_NETWORK_HARPOON_TRIFORCE_THIEF_H
#define SOH_NETWORK_HARPOON_TRIFORCE_THIEF_H

#ifdef __cplusplus

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>

extern "C" {
#include "z64.h"
}

// HarpoonTriforceThief — gamemode controller for the Triforce Thief mode.
//
// Data lives in <appdir>/harpoon/gamemodes/triforce_thief/ as JSON files:
//   - gamemode.yaml       (map list, default config)
//   - maps/<name>.json    (7 spawn points per map)
//   - presets/save.json   ("thief" save preset)
//
// Network transport: ROOM.BROADCAST_EVENT envelopes with event_name prefix
// "TRIFORCE_THIEF.". The server only relays; clients dispatch by event_name.

namespace HarpoonTriforceThief {

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

struct SpawnPoint {
    f32 x, y, z;
    std::string origin;   // "DOOR_ANA" / "EN_ITEM00" / etc. — debug origin tag
    std::string note;
};

struct MapDef {
    std::string id;        // "hyrule_field" / etc. — matches maps/<id>.json
    std::string name;
    s32 entranceIndex;
    std::string description;
    std::vector<SpawnPoint> spawnPoints;
};

constexpr s32 kMapCount = 6;
constexpr s32 kSpawnPointsPerMap = 7;

// ---------------------------------------------------------------------------
// Per-client state
// ---------------------------------------------------------------------------

enum class Role : u8 { Unassigned = 0, Thief = 1, Carrier = 2, Eliminated = 3 };

struct LocalState {
    Role     role            = Role::Unassigned;
    s32      confirmedMap    = -1;        // index into MapDef list
    s32      hoveredMap      = 0;         // map select UI cursor
    s32      currentSpawn    = -1;        // active spawn point on confirmedMap
    bool     inMapSelect     = false;
    bool     inRound         = false;
    u32      carrierClientId = 0;          // 0 = nobody carrying
    f32      triforceX = 0.0f, triforceY = 0.0f, triforceZ = 0.0f;
    s32      roundIndex      = 0;          // 1-based round number

    // Round-win timer (rupees-as-countdown).
    s32      roundWinSeconds        = 60;  // host-broadcast at round start
    s32      carrierRupeesRemaining = 0;   // 0 = "no time recorded yet"; persists across drops
    s32      drainTickCounter       = 0;   // frames-until-next-rupee-decrement
    bool     roundEnded             = false;

    // Drop fly-away physics. While > 0, the Triforce is airborne; pickups
    // are blocked. Velocity is integrated with gravity each frame; the
    // engine's BgCheck_* APIs detect walls / floor / ceiling and reflect
    // the velocity so it bounces and settles on real geometry. ttl is a
    // safety fuse (max airborne frames); the normal stop condition is
    // "on floor with low horizontal speed".
    s32      dropFlyTimer  = 0;   // ttl: max frames before forced settle
    f32      dropVelX      = 0.0f, dropVelY = 0.0f, dropVelZ = 0.0f;
    f32      dropPrevX     = 0.0f, dropPrevY = 0.0f, dropPrevZ = 0.0f;

    // Per-client pickup cooldown (frames). Set on the dropper when they lose
    // the Triforce so they can't immediately re-grab it. Other clients can
    // still pick up during the cooldown.
    std::unordered_map<u32, s32> pickupCooldownByCid;

    // Passive regen tick — fires every 60 frames while not the carrier.
    s32      regenTickCounter = 0;

    // Everyone-votes deadline. While > 0 we're collecting votes; on 0
    // (timeout) or earlier (all voted) the host tallies + HostConfirmMaps.
    // Set by HandleMapSelectBegin when mode == EVERYONE_CHOOSES.
    s32      mapVoteDeadline   = 0;

    // Idle/void respawn. Frames the Triforce has spent on the ground
    // untouched; if it crosses 10 sec OR drops into a void, respawn.
    s32      idleOnGroundFrames = 0;

    // Triforce-appear cutscene. Subcamera orbits the Triforce for
    // `cutsceneTimer` frames at radius 300 / height 150. Mirrors
    // Scooter's HARPOON_STATE_TRIFORCE_CUTSCENE behaviour.
    s32      cutsceneTimer   = 0;     // frames remaining; 0 = inactive
    bool     cutsceneReady   = false; // set true after scene finishes loading
    s16      cutsceneSubCam  = 0;     // SUBCAM_FREE when not active
};

// ---------------------------------------------------------------------------
// Inner event tags
// ---------------------------------------------------------------------------

constexpr const char* kEvtMapSelectBegin   = "TRIFORCE_THIEF.MAP_SELECT_BEGIN";
constexpr const char* kEvtMapHover         = "TRIFORCE_THIEF.MAP_HOVER";
constexpr const char* kEvtMapVote          = "TRIFORCE_THIEF.MAP_VOTE";
constexpr const char* kEvtMapConfirmed     = "TRIFORCE_THIEF.MAP_CONFIRMED";
constexpr const char* kEvtRoundConfig      = "TRIFORCE_THIEF.ROUND_CONFIG";
constexpr const char* kEvtTriforceSpawn    = "TRIFORCE_THIEF.TRIFORCE_SPAWN";
constexpr const char* kEvtTriforcePickup   = "TRIFORCE_THIEF.TRIFORCE_PICKUP";
constexpr const char* kEvtTriforceDrop     = "TRIFORCE_THIEF.TRIFORCE_DROP";
constexpr const char* kEvtCarrierTimerSync = "TRIFORCE_THIEF.CARRIER_TIMER_SYNC";
constexpr const char* kEvtCutsceneBegin    = "TRIFORCE_THIEF.CUTSCENE_BEGIN";
constexpr const char* kEvtRoundResult      = "TRIFORCE_THIEF.ROUND_RESULT";

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Locate <appdir>/harpoon/gamemodes/triforce_thief/ and load all JSONs.
bool Init();
bool IsLoaded();

// ---------------------------------------------------------------------------
// Data accessors
// ---------------------------------------------------------------------------

const std::vector<MapDef>& GetMaps();
const MapDef* GetMap(s32 mapIdx);
const SpawnPoint* GetSpawnPoint(s32 mapIdx, s32 spawnIdx);
s32 GetEntranceForMap(s32 mapIdx);

// ---------------------------------------------------------------------------
// Save preset — mirrors Scooter's TriforceThief_InitSave().
// ---------------------------------------------------------------------------

void ApplyThiefSave();

// ---------------------------------------------------------------------------
// Local state
// ---------------------------------------------------------------------------

LocalState& GetLocalState();
bool IsLocalCarrier();
bool IsInMapSelect();
bool IsInRound();

// ---------------------------------------------------------------------------
// Map select UI cursor — clients in host-chooses mode read GetLocalState();
// in everyone-chooses they cycle here and broadcast MapHover.
// ---------------------------------------------------------------------------

bool CycleHoveredMap(s32 delta);

// ---------------------------------------------------------------------------
// Network event dispatch
// ---------------------------------------------------------------------------

void HandleEvent(const nlohmann::json& envelope);

// Payload builders (return ready-to-send {type, event_name, data}).
nlohmann::json BuildMapSelectBeginPayload(s32 windowSeconds);
nlohmann::json BuildMapHoverPayload(s32 mapIdx);
nlohmann::json BuildMapConfirmedPayload(s32 mapIdx, s32 entranceIndex);
nlohmann::json BuildTriforceSpawnPayload(s32 mapIdx, s32 spawnIdx,
                                          f32 x, f32 y, f32 z);
nlohmann::json BuildTriforcePickupPayload(u32 carrierClientId);
nlohmann::json BuildTriforceDropPayload(u32 dropperClientId,
                                         f32 startX, f32 startY, f32 startZ,
                                         f32 velX,   f32 velY,   f32 velZ);
nlohmann::json BuildRoundResultPayload(u32 winnerClientId, s32 roundIndex);
nlohmann::json BuildRoundConfigPayload(s32 winSeconds);
nlohmann::json BuildCarrierTimerSyncPayload(s32 rupeesRemaining);
nlohmann::json BuildCutsceneBeginPayload(f32 triforceX, f32 triforceY, f32 triforceZ);

// Notify the TT cutscene state machine that the new scene has finished
// loading and the subcamera orbit can start. Called by the
// OnSceneSpawnActors hook when the active gamemode is triforce_thief.
void OnSceneLoaded();

// ---------------------------------------------------------------------------
// HUD — analogous to PropHunt::DrawHud. Renders map-select grid + round
// info when in TT mode.
// ---------------------------------------------------------------------------

void DrawHud();

// ---------------------------------------------------------------------------
// In-world rendering — invoke from a per-frame draw hook (Harpoon hooks the
// post-actor-draw point). When the Triforce is on the ground these draw a
// spinning, bobbing translucent Triforce at its world position; when carried
// they draw a smaller indicator above the carrier's head.
// Both functions are no-ops when not in an active round.
// ---------------------------------------------------------------------------

void DrawTriforceOnGround(PlayState* play);
void DrawTriforceAboveHead(Actor* actor, PlayState* play);

// Carrier pickup detection — call once per frame with the local player's
// position. If the Triforce is on the ground and the player is within the
// pickup radius, returns true; caller is expected to broadcast Pickup.
bool ShouldPickupTriforce(f32 px, f32 py, f32 pz);

// Trigger a scene transition to `entranceIndex`. Same semantics as the
// PropHunt::TeleportToEntrance helper.
void TeleportToEntrance(s32 entranceIndex);

// Convenience: apply thief save preset + teleport to the given map's
// entrance. Called automatically by HandleMapConfirmed.
void StartLocalRoundOnMap(s32 mapIndex);

// "Big" game start used to enter Triforce Thief from the title screen
// or file select. Mirrors HarpoonPropHunt::BigStartGameAs but for TT:
// always adult Link with the full inventory thief preset, drops the
// player into Hyrule Field as the round lobby. Round actually begins
// when the host confirms a map (sends TRIFORCE_THIEF.MAP_CONFIRMED).
void BigStartGame();

// Per-frame entry point — called from the Harpoon OnGameFrameUpdate hook
// when the active room gamemode is `triforce_thief`. Handles carrier rupee
// drain, win-condition check, and host-side map-vote tally.
void TickFrame();

// Seed / resume the carrier-timer rupee count and write it to
// gSaveContext.rupees so the standard rupee HUD displays it. Called by
// the hook handler right after a local pickup is detected. Per-carrier
// persistent: subsequent pickups by the same client resume from the
// remaining count instead of reseting to roundWinSeconds.
void OnLocalPickup();

// Host-only: apply a chosen map locally and broadcast the round-start
// envelope set (MAP_CONFIRMED + ROUND_CONFIG + TRIFORCE_SPAWN). Reused by
// the overlay A-press path, vote-tally, and random auto-pick.
void HostConfirmMap(s32 mapIdx);

// Fullscreen 3x2 map-select overlay (analogous to PropHunt's). Reuses
// the same map_select/*.png assets (bg, navi, navi_white).
void RegisterMapSelectWindow();

// ---------------------------------------------------------------------------
// TODO (Phase 4b) — engine integration
//
// Scooter implements:
//   - TriforceThief_DrawTriforceOnGround(PlayState*) — spinning bobbing Triforce
//   - TriforceThief_DrawTriforceAboveHead(Actor*, PlayState*) — carrier indicator
//   - TriforceThief_KillActorAtSpawn(Vec3f*)            — clear NPC/item at spawn
//   - TriforceThief_TeleportToMap(int entranceIndex)    — host triggers scene change
//   - TriforceThief_UpdateMusic(void)                   — per-round BGM ramp
//   - TriforceThief_UpdateResourceDrops(PlayState*)     — recovery hearts/rupees
//   - Pickup collision: when local Link enters Triforce AABB, send Pickup
//   - Drop trigger: when carrier takes damage or hits a wall, send Drop
//
// See Scooter HarpoonTriforceThief.cpp for the reference logic.
// ---------------------------------------------------------------------------

}  // namespace HarpoonTriforceThief

extern "C" {
    s32 HarpoonTriforceThief_IsLoaded(void);
    s32 HarpoonTriforceThief_IsLocalCarrier(void);
    s32 HarpoonTriforceThief_GetConfirmedMap(void);
    s32 HarpoonTriforceThief_GetEntranceForMap(s32 mapIdx);
}

#endif  // __cplusplus
#endif  // SOH_NETWORK_HARPOON_TRIFORCE_THIEF_H
