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

constexpr s32 kMapCount = 8;
constexpr s32 kSpawnPointsPerMap = 7;

// Lobby entrance (always Hyrule Field's main spawn, regardless of which
// maps are in the playable roster). 0x0CD = ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN.
constexpr s32 kHyruleFieldLobbyEntrance = 205;

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

    // Round-win timer.
    s32      roundWinSeconds        = 60;  // host-broadcast at round start
    s32      carrierRupeesRemaining = 0;   // legacy: kept so existing leaderboard
                                            // / sync code still compiles. Soft-
                                            // shadowed by carrierTimerFrames now.
    s32      drainTickCounter       = 0;   // legacy unused; kept for compat
    bool     roundEnded             = false;

    // Working value of the CURRENT carrier's team timer. Drained by the
    // wall-clock std::chrono drain block every frame while someone is
    // carrying. On pickup we snapshot the appropriate team's frozen
    // value into this; on drop we save it back. When carrierClientId
    // == 0 this is meaningless (the team-scoped stores below hold the
    // truth).
    s32      carrierTimerFrames     = 0;

    // Per-team frozen storage. Snapshotted on drop / round-start, and
    // reloaded into `carrierTimerFrames` on pickup based on which team
    // the picker is on. The HUD pin reads BOTH values every frame so
    // each team's MM:SS digits stay correct on screen regardless of
    // who's actively draining.
    s32      redTimerFrames         = 0;
    s32      blueTimerFrames        = 0;

    // Local player's team for this lobby session ("" = unassigned,
    // "red", "blue"). Set via UI button in the Network/Harpoon tab,
    // persisted to a CVar, and broadcast to peers via TEAM_ASSIGN.
    std::string team;

    // Current carrier's team, cached on TRIFORCE_PICKUP / TRIFORCE_DROP
    // so the per-frame drain doesn't need to look up the carrier in the
    // clients map. "" when no one carries (carrierClientId == 0).
    std::string currentCarrierTeam;

    // Last "Hurry! N seconds left!" threshold that has been fired this
    // round. Prevents the threshold from triggering more than once when
    // the timer briefly hovers around the boundary. -1 = no warning yet.
    s32      lastWarnSecond         = -1;

    // Last seconds-remaining value the carrier broadcast over
    // CARRIER_TIMER_SYNC. With the wall-clock drain the integer frame
    // count can skip over exact multiples of 20, so we broadcast whenever
    // the whole-second value CHANGES rather than on `frames % 20 == 0`
    // (which could be missed). -1 = nothing broadcast yet this round.
    s32      lastSyncSecond         = -1;

    // Wall-clock timestamp (ms) of the host's last STATE_SYNC broadcast.
    // The host emits an authoritative {carrierClientId, red, blue} packet
    // once per second so peers can converge on conflicting pickup races
    // or timer drift. 0 = no broadcast yet this round.
    s64      lastHostSyncMs         = 0;

    // Slow-mo accumulator for the final 5 seconds. While the carrier's
    // remaining time is <= 5 sec the descending counter ticks at 1/2.5
    // the normal rate (2.5× slower — "dramatic finale"). Also doubles as
    // the fractional-frame carry for the wall-clock drain (see below).
    f32      slowModeAccum          = 0.0f;

    // Wall-clock timestamp (ms, from ImGui::GetTime) of the carrier's
    // last timer drain. The countdown drains by REAL elapsed time, not
    // game-logic frames: frame counting froze the timer whenever the
    // carrier opened the pause menu (logic ticks stop) and drifted
    // between clients running at different framerates — both showed up
    // as cross-player timer desync. 0 = "re-seed on next carrier tick"
    // (set to 0 on pickup / round start so the first tick doesn't drain
    // a stale delta).
    s64      carrierTimerLastMs     = 0;

    // Persistent C-button (+ D-pad) item mappings across lobby ↔ round
    // transitions. ApplyThiefSave overwrites buttonItems[1..7] with the
    // preset's defaults every time a round starts or ends — clobbering
    // the user's custom binds (e.g. Hookshot on C-Left). We snapshot
    // every tick while in lobby, then restore after each preset apply.
    // Per user spec: ammo and inventory contents CAN be overwritten by
    // the kit; only the C-button item assignments stay.
    u8       savedCButtonItems[8]   = {};
    bool     hasSavedCButtons       = false;

    // 3-second "GET READY" countdown at round start. Decrements every tick
    // (20 fps), freezes the local player while > 0, releases on 0.
    s32      preRoundCountdownFrames = 0;

    // True once StepDropPhysics has detected a floor landing AND the
    // horizontal speed has settled below the threshold. New drops (knockout
    // / spawn / void respawn) reset this to false.
    bool     triforceLanded = true;

    // Carrier speed-buff bookkeeping. While carrying the Triforce we hijack
    // the SpeedModifier cheat path that z_player.c already reads at 5 sites
    // (`maxSpeed *= CVarGetFloat(CVAR_CHEAT("SpeedModifier.Value"), 1.0f)`)
    // and force a 1.5x multiplier. We save the user's prior CVars + the
    // engine-internal `gWalkSpeedToggle` global so dropping the Triforce
    // restores their original SpeedModifier configuration exactly.
    bool     appliedBunnyHood     = false;
    s32      savedBunnyHoodCVar   = 0;        // user's prior MMBunnyHood setting
    f32      savedSpeedModValue   = 1.0f;     // user's prior CVAR_CHEAT(SpeedModifier.Value)
    s32      savedSpeedToggleCVar = 0;        // user's prior CVAR_CHEAT(SpeedModifier.SpeedToggle)
    u8       savedWalkSpeedToggle = 0;        // user's prior global toggle state

    // One-shot "GO!" flash counter. Set to 20 (1 sec at 20 fps) when the
    // pre-round countdown reaches 0; decremented in DrawHud each frame.
    // Once it hits 0, never re-arms during the same round.
    s32      goFlashFrames = 0;

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

    // Host-side flag: HostConfirmMap defers Triforce spawn-pick + broadcast
    // until the new scene's actors are loaded (OnSceneLoaded). Set true when
    // the map is confirmed; consumed (cleared + spawn broadcast) the moment
    // OnSceneLoaded fires for the corresponding map. Peers ignore this flag.
    bool     pendingAnchorPick = false;
    s32      pendingAnchorMap  = -1;   // map index we're waiting to anchor in

    // Has the round's scene finished loading on this client? Set in
    // OnSceneLoaded, cleared at every round-start (HandleMapConfirmed /
    // HostConfirmMap). Lets HandleCutsceneBegin decide whether to flip
    // cutsceneReady=true immediately (scene already loaded) or wait for
    // OnSceneLoaded to flip it (scene still streaming). Without this,
    // peers whose scene loaded BEFORE the host-deferred CUTSCENE_BEGIN
    // arrives would never see cutsceneReady flip true.
    bool     sceneLoadedThisRound = false;
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
constexpr const char* kEvtTimerWarning     = "TRIFORCE_THIEF.TIMER_WARNING";
constexpr const char* kEvtCutsceneBegin    = "TRIFORCE_THIEF.CUTSCENE_BEGIN";
constexpr const char* kEvtRoundResult      = "TRIFORCE_THIEF.ROUND_RESULT";
// Host-only 1 Hz authoritative state sync. The host broadcasts its view of
// {carrierClientId, redTimerFrames, blueTimerFrames} every second so peers
// can resolve any conflict that crept in through race conditions (e.g. two
// pickup events at nearly the same time, or local timer drift). All fields
// snap to the host's value unconditionally — the host is the tie-breaker.
constexpr const char* kEvtStateSync        = "TRIFORCE_THIEF.STATE_SYNC";

// Team assignment broadcast — fires when a player clicks Join Red / Join
// Blue in the Harpoon menu. Mirrors PropHunt's RoleAssign event pattern.
// Carries { targetClientId, team } where team is "red" or "blue". The
// peer handler updates `clients[targetClientId].team` AND .color (team
// drives the nametag tint and the friendly-fire gate).
constexpr const char* kEvtTeamAssign       = "TRIFORCE_THIEF.TEAM_ASSIGN";

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

// Per-round scene-lock (parity with PropHunt). Returns true if `sceneNum`
// belongs to the allowed scene cluster for the round whose host-selected
// map is `mapIndex`. Used by the OnGameFrameUpdate loading-zone interceptor.
bool IsSceneInRoundClusterTT(s32 mapIndex, s32 sceneNum);

// Full-circle return entrance for an invalid out-of-cluster exit. v1
// returns the round map's main entrance (same simplification as PH).
s32 GetReturnEntranceForInvalidExitTT(s32 mapIndex, s32 destSceneNum);

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
nlohmann::json BuildRoundResultPayload(u32 winnerClientId, s32 roundIndex,
                                       const std::string& winnerTeam = "");

// Team color helper. Returns the canonical RGB tint we use everywhere
// (nametag text, Triforce env color, menu badge). Unknown team string
// returns vanilla gold so the Triforce on the ground stays gold per spec.
struct TeamColor { u8 r, g, b; };
TeamColor TeamRGB(const std::string& team);

// Set the LOCAL player's team. Persists to CVAR_GENERAL("TriforceThief.Team")
// and broadcasts a TEAM_ASSIGN event so peers update their view + nametag.
void SetLocalTeam(const std::string& team);
nlohmann::json BuildRoundConfigPayload(s32 winSeconds);
nlohmann::json BuildCarrierTimerSyncPayload(s32 rupeesRemaining);
// Host-only 1 Hz authoritative state broadcast. Carries both the current
// carrierClientId and BOTH teams' frame-precise timers. Peers apply all
// three fields unconditionally to converge on the host's view.
nlohmann::json BuildStateSyncPayload(u32 carrierClientId,
                                     s32 redTimerFrames,
                                     s32 blueTimerFrames);

// Team assignment broadcast. Sent locally when a player picks Red/Blue
// in the menu; relayed by the server to every other client. Receiver
// updates clients[targetClientId].team + .color so nametag tint + the
// friendly-fire gate converge across the room.
nlohmann::json BuildTeamAssignPayload(u32 targetClientId,
                                      const std::string& team);
// Per-second warning broadcast — fires once when the carrier's countdown
// crosses 10/5/4/3/2/1 seconds remaining. Peers' HandleEvent dispatches
// it to a Notification::Emit so everyone sees the alert at the same time.
nlohmann::json BuildTimerWarningPayload(s32 secondsLeft);
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

// Return players to the lobby (Hyrule Field) after a round ends. Decoupled
// from the playable map list — lobby is always HF regardless of what's in
// slot 0 of `kMapInit`. Applies thief save + restores C-buttons + teleports
// to `kHyruleFieldLobbyEntrance`.
void TeleportToLobby();

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
