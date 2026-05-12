#ifndef SOH_NETWORK_HARPOON_PROP_HUNT_H
#define SOH_NETWORK_HARPOON_PROP_HUNT_H

#ifdef __cplusplus

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <array>

extern "C" {
#include "z64.h"
}

// HarpoonPropHunt — gamemode controller for the Prop Hunt mode.
//
// Data lives in <appdir>/harpoon/gamemodes/prop_hunt/ as JSON files:
//   - gamemode.yaml         (map list, default config)
//   - props/environment.json (10 props x up to 4 states; global)
//   - props/enemies.json    (9 maps x 10 entries)
//   - props/npcs.json       (9 maps x 10 entries)
//   - presets/save.json     (hider + seeker save inits)
//
// Network transport: ROOM.BROADCAST_EVENT envelopes whose `event` field starts
// with "PROP_HUNT.". Schema validation is client-side (server is gamemode-
// agnostic and just relays). See HarpoonPropHuntEvent_* constants below.

namespace HarpoonPropHunt {

// ---------------------------------------------------------------------------
// Data structs (loaded from JSON at Init())
// ---------------------------------------------------------------------------

struct PropVariant {
    s16 actorId;
    s16 params;
    f32 scale;
    f32 yOffset;
};

struct PropEntry {
    std::string name;
    std::vector<PropVariant> states;
};

constexpr s32 kCategoryCount = 3;     // Environment / Enemies / NPCs
constexpr s32 kPropsPerCategory = 10;
constexpr s32 kMapCount = 9;

enum Category : s32 {
    CAT_ENVIRONMENT = 0,
    CAT_ENEMIES     = 1,
    CAT_NPCS        = 2,
};

// Single global per-pack data block. Filled by Init() once at startup.
struct PropTables {
    std::array<PropEntry, kPropsPerCategory>  environment;
    std::array<std::array<PropEntry, kPropsPerCategory>, kMapCount> enemiesByMap;
    std::array<std::array<PropEntry, kPropsPerCategory>, kMapCount> npcsByMap;
    bool loaded = false;
};

// ---------------------------------------------------------------------------
// Map list (parallel to gamemode.yaml)
// ---------------------------------------------------------------------------

struct MapDef {
    std::string id;
    std::string name;
    s32 entranceIndex;
    std::string description;
};

// ---------------------------------------------------------------------------
// Per-client state
// ---------------------------------------------------------------------------

enum class Role : u8 { Unassigned = 0, Hider = 1, Seeker = 2, Eliminated = 3 };

struct LocalState {
    Role     role          = Role::Unassigned;
    s32      propCategory  = CAT_ENVIRONMENT;
    // -1 means "no prop picked yet — render as Link". IsLocalHiderWithProp
    // checks `propIndex >= 0`, so a value of 0 would always suppress the
    // vanilla Link draw, leaving the hider invisible until they enter prop
    // mode. Scooter starts at -1 and only flips to 0..9 when the hider
    // picks something inside prop mode.
    s32      propIndex     = -1;
    s32      propState     = 0;
    s32      confirmedMap  = -1;        // index into MapDef list
    bool     inHidePhase   = false;
    s32      hidePhaseFramesRemaining = 0;

    // Damage cooldown — when the hider takes a hit they auto-detransform
    // (propIndex = -1) and we lock them out of prop mode for ~10 sec so
    // they can't immediately re-disguise mid-fight. Decrements each frame
    // in PropHunt::TickFrame; while > 0 the R-toggle and prop-cycle inputs
    // refuse to re-enter prop mode.
    s32      propModeLockoutTimer = 0;
};

// ---------------------------------------------------------------------------
// Inner event tags for ROOM.BROADCAST_EVENT.event
// ---------------------------------------------------------------------------

constexpr const char* kEvtRoleAssign      = "PROP_HUNT.ROLE_ASSIGN";
constexpr const char* kEvtSetDisguise     = "PROP_HUNT.SET_DISGUISE";
constexpr const char* kEvtHidePhaseBegin  = "PROP_HUNT.HIDE_PHASE_BEGIN";
constexpr const char* kEvtHidePhaseEnd    = "PROP_HUNT.HIDE_PHASE_END";
constexpr const char* kEvtEliminated      = "PROP_HUNT.ELIMINATED";
constexpr const char* kEvtRoundResult     = "PROP_HUNT.ROUND_RESULT";

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Locate <appdir>/harpoon/gamemodes/prop_hunt/ and load all JSONs.
// Returns true if data is usable; false (and a logged warning) if absent.
bool Init();

// True if Init() has successfully loaded prop tables, maps, and save presets.
bool IsLoaded();

// ---------------------------------------------------------------------------
// Data accessors
// ---------------------------------------------------------------------------

const PropTables& GetTables();
const std::vector<MapDef>& GetMaps();
const PropEntry* GetPropEntry(s32 category, s32 propIndex, s32 mapIdx);

// ---------------------------------------------------------------------------
// Save preset application — mirrors Scooter's PropHunt_InitSave /
// PropHunt_InitSeekerSave / TriforceThief_InitSave logic. Reads from
// presets/save.json so the C++ stays gamemode-agnostic.
// ---------------------------------------------------------------------------

void ApplyHiderSave();
void ApplySeekerSave();

// ---------------------------------------------------------------------------
// Local state accessors
// ---------------------------------------------------------------------------

LocalState& GetLocalState();
bool IsHider();
bool IsSeeker();
bool IsEliminated();

// True only for hiders during the hide phase or active round (not seekers,
// not eliminated). Used by collision / draw hooks.
bool IsLocalHiderWithProp();

// ---------------------------------------------------------------------------
// Input cycling — call from per-frame input hook when in hider mode.
// dCategoryDelta: -1 to cycle category back, +1 forward, 0 to leave alone.
// dIndexDelta:    -1 to cycle prop index back, +1 forward, 0 to leave alone.
// dStateDelta:    -1/+1/0 for state variant.
// Returns true if any field changed (caller should broadcast SET_DISGUISE).
// ---------------------------------------------------------------------------

bool CyclePropCategory(s32 delta);
bool CyclePropIndex(s32 delta);
bool CyclePropState(s32 delta);

// ---------------------------------------------------------------------------
// Network event dispatch — called by Harpoon::OnIncomingJson when a
// ROOM.BROADCAST_EVENT arrives with event prefix "PROP_HUNT.".
// ---------------------------------------------------------------------------

void HandleEvent(const nlohmann::json& payload);

// Build outgoing payloads. The caller wraps them in a ROOM.BROADCAST_EVENT
// envelope and sends.
nlohmann::json BuildSetDisguisePayload();
nlohmann::json BuildRoleAssignPayload(u32 targetClientId, Role role);
nlohmann::json BuildHidePhaseBeginPayload(s32 durationFrames);
nlohmann::json BuildHidePhaseEndPayload();
nlohmann::json BuildEliminatedPayload(u32 victimClientId, u32 killerClientId);
nlohmann::json BuildRoundResultPayload(const std::string& winnerSide);

// ---------------------------------------------------------------------------
// HUD — call from a Ship::GuiWindow::DrawElement override when the active
// gamemode is Prop Hunt. Pulls state from GetLocalState() and renders a small
// always-visible ImGui block (top-left, no decorations) with the current
// role, prop selection, and hide-phase timer.
// ---------------------------------------------------------------------------

void DrawHud();

// Register the fullscreen map-select GuiWindow with libultraship's Gui. The
// window is registered once per process and only renders when
// `gameState == HARPOON_STATE_MAP_SELECT` and `currentRoomGameMode == "prop_hunt"`.
void RegisterMapSelectWindow();

// ---------------------------------------------------------------------------
// Ghost actor system (Phase 3c)
//
// Scooter spawns 30 invisible "ghost" actors per scene (3 categories x 10
// props x 1 state each — multi-state variants reuse the index-0 actor) so
// their `draw` function pointers can be invoked to render hider disguises at
// player positions.  Two pieces are required to land this:
//
//   1. SpawnGhostActors(play) / DestroyGhostActors(play): call
//      Actor_SpawnAsChild for each entry and store the returned Actor*. Hide
//      them by setting `actor->flags |= ACTOR_FLAG_NO_DRAW` and zeroing their
//      update function, so they don't visually appear or run AI but their
//      object loads in objectCtx.
//
//   2. A `VB_OVERRIDE_PLAYER_DRAW` (or per-limb) vanilla-behaviour hook so a
//      hider's local Link draws as the chosen prop. Remote hiders are easier:
//      HarpoonDummyPlayer.cpp can branch on `modelType >= 5` and call the
//      same draw helper. Both call DrawProp() below.
//
// Until both pieces ship, `SpawnGhostActors` is a no-op (returns false) and
// DrawProp() returns without doing anything if the ghost array is empty.
// ---------------------------------------------------------------------------

bool SpawnGhostActors(PlayState* play);
void DestroyGhostActors(PlayState* play);
bool AreGhostsReady();
Actor* GetGhostActor(s32 category, s32 propIndex, s32 propState = 0);

// Render `playerActor` as if it were the prop at (category, propIndex,
// propState). mapIdx resolves per-map enemy/NPC tables. Returns true if
// the prop was actually drawn — callers should fall back to vanilla draw
// when this returns false (avoids the "invisible" failure mode where
// suppressing vanilla without rendering anything leaves the player blank).
bool DrawHiderAsProp(Actor* playerActor, PlayState* play,
                     s32 category, s32 propIndex, s32 propState, s32 mapIdx);

// Per-frame tick — call from OnGameFrameUpdate to decrement countdown timers.
void TickFrame();

// Spawn a decoy at the player's current position + rotation using the
// hider's currently-selected prop (cat/idx/state). Stores in the local
// decoy ring (FIFO of 3); broadcasts to peers via COMBAT.SPAWN_DECOY so
// they render the same ghost at the same world position. No-op if not
// a hider or no prop selected yet.
void SpawnDecoy();

// Clear all local decoys (called on round end / role change).
void ClearDecoys();

// Decoy ring for the local hider — public so the menu / debug code can
// inspect counts. Remote-client decoys live on Harpoon::clients[cid]
// (somariaDecoy* fields).
constexpr s32 kDecoyMax = 3;
struct DecoyEntry {
    f32 x, y, z;
    s16 rotY;
    s32 propCat, propIndex, propState;
    bool active;
};
const std::array<DecoyEntry, kDecoyMax>& GetLocalDecoys();

// ---------------------------------------------------------------------------
// Host-side orchestration: state machine, seeker priority queue, role
// assignment. Only runs on the host's client; non-hosts ignore these.
//
// Mirrors Scooter's server-side state machine. Each broadcast goes through
// PROP_HUNT.STATE_SNAPSHOT so every peer sees the same authoritative view.
// ---------------------------------------------------------------------------

namespace Host {

// Host-configurable settings (admin only). Bound to the UI in HarpoonMenu.
struct Settings {
    s32 seekerCount  = 1;            // 1-3
    s32 hideSeconds  = 30;           // hide-phase length
    s32 mapSelectMode = 0;           // 0=host_chooses 1=everyone_votes 2=random
    s32 selectedMap   = 0;           // host's pick when mode=host_chooses
};
Settings& GetSettings();

// Picks the next round's seekers using the priority queue: any client who
// hasn't been seeker yet this rotation is eligible. When the pool empties
// (everyone has been seeker), the history resets and the rotation starts
// over. Returns the chosen seeker client ids.
std::vector<u32> PickNextSeekers(const std::vector<u32>& candidateClientIds,
                                  s32 seekerCount);

// Reset the rotation history (called when host changes / game restarts).
void ResetSeekerHistory();

// Has this client ever been seeker in the current rotation?
bool HasBeenSeeker(u32 clientId);

// Round elapsed-seconds for a given client (hider's survival timer). Per
// Scooter: timer is paused while not in hiding/playing state, and persists
// across rounds.
u32 GetClientTimer(u32 clientId);
void AddClientTimerSeconds(u32 clientId, u32 seconds);
void ResetClientTimer(u32 clientId);

}  // namespace Host

// Trigger a scene transition to `entranceIndex` (e.g. ENTR_HYRULE_FIELD_*).
// Re-applies linkAgeOnLoad so the age swap that the save preset performs
// doesn't corrupt Inventory_SwapAgeEquipment mid-transition. No-op if
// gPlayState is null.
void TeleportToEntrance(s32 entranceIndex);

// Convenience: apply the role's save preset AND teleport. Hider → child
// + entranceIndex from common.entrance_index. Seeker → same.
// Called automatically by HandleRoleAssign; exposed for host-side testing.
void StartLocalRoundAs(Role role, s32 entranceIndex);

// "Big" game start used to enter a Prop Hunt round from the title screen or
// any other non-gameplay state. Mirrors Scooter's Harpoon_InitPropHunt:
//   1. Save_InitFile(false) for a fresh save
//   2. Apply hider/seeker preset
//   3. Wipe / set the standard gSaveContext fields (gameMode, respawn,
//      timers, button status, audio cmd, ...) that the engine expects when
//      transitioning from menu to gameplay
//   4. Stop BGM
//   5. SET_NEXT_GAMESTATE(gGameState, Play_Init, PlayState) so the next
//      frame enters gameplay
//   6. Fire GameInteractor's OnLoadGame hook
// Call this exactly once per client per round-start when transitioning IN
// from title / file select; for round restarts during gameplay, prefer
// StartLocalRoundAs which just teleports + applies preset without the full
// gamestate re-init.
void BigStartGameAs(Role role);

// Per-frame init: apply the pending hider/seeker preset right after a scene
// load (mirrors Scooter's PropHunt_ProcessPendingInit). Engine-level
// Inventory_ChangeUpgrade calls only stick if invoked after the scene's
// actors spawn; setting them mid-transition gets clobbered.
void SetPendingInit(s32 type);   // 1=hider, 2=seeker, 3=converted seeker, 4=reset to hider
void ProcessPendingInit();

// In-place scene reload — mirrors the Instant Age Change cheat (mods.cpp's
// SwitchAge). Triggers a TRANS_TYPE_INSTANT transition back to the current
// entrance with the player's position/yaw preserved via RESPAWN_MODE_DOWN.
// Re-syncs gPlayState->linkAgeOnLoad from gSaveContext.linkAge so a role
// change that also toggled age picks up the new age this reload.
void InstantReloadScene();

// Resolve a map-select index to its real entrance constant (matches the
// kMaps table in PropHuntMapSelect.cpp). Returns ENTR_HYRULE_FIELD_* as
// fallback for invalid indices.
s32 GetEntranceForMapIndex(s32 mapIndex);

// Centralised "map was confirmed for this round" logic. Sets gameState
// to HIDING_PHASE, stores confirmedMapIndex, resets the round timer, and
// (if local is hider) teleports to the map's entrance + queues the hider
// pending-init. Idempotent / safe to call from:
//   - PropHuntMapSelect's host A-button confirm,
//   - HarpoonHookHandlers' random-mode auto-pick,
//   - HandleEvent's MAP_CONFIRMED dispatch (for peers).
// Without this the host's own client never teleports because the
// server-relay excludes the sender from MAP_CONFIRMED.
void LocallyConfirmMap(s32 mapIndex);

// Centralised "hide phase ended → seekers go in" logic. Sets
// gameState to PLAYING and teleports the local client if it's a seeker.
// Mirrors Scooter's "playing" state handler.
void LocallyEndHidePhase();

// Host-side "the round starts now" — runs the full Scooter-equivalent
// round-start sequence:
//   1. PickNextSeekers (priority queue, no repeats this rotation)
//   2. Set own role locally (server-relay excludes us from our own broadcast)
//   3. Broadcast PROP_HUNT.ROLE_ASSIGN to each peer with their assigned role
//   4. LocallyConfirmMap (sets gameState, teleports if hider, resets timer)
//   5. Broadcast PROP_HUNT.MAP_CONFIRMED + PROP_HUNT.HIDE_PHASE_BEGIN
// Call from any of the map-confirm code paths. No-op if not host.
void HostStartRound(s32 mapIndex);

// Total elapsed frames since the current round entered HIDING_PHASE. The
// Boss Rush–style timer in the HUD reads this. Reset by HandleHidePhaseBegin
// and incremented in TickFrame while inHidePhase or after.
u32 GetRoundElapsedFrames();
void ResetRoundElapsed();

// Apply the role's save preset locally and trigger an in-place reload so
// the new kit / age propagate to the visible player. If we're not yet in
// gameplay (gPlayState == null), routes to BigStartGameAs instead.
void ChangeRoleAndReload(Role role);

// ---------------------------------------------------------------------------
// TODO (Phase 3b) — ghost actor draw system
// Scooter spawns 30 invisible actors per scene at OnSceneSpawnActors and uses
// their `draw` function pointers to render hider disguises. That requires:
//   - Actor_SpawnAsChild with category override
//   - PROP_HUNT_ENABLE_DRAW define + Matrix_Push hooks
//   - Object_IsLoaded gating
// See HarpoonPropHunt.h (Scooter) lines 36-176 for the inline drawing helpers.
// Not implemented here yet.
// ---------------------------------------------------------------------------

}  // namespace HarpoonPropHunt

// C bridge for code that needs to query state without pulling in the namespace.
extern "C" {
    s32 HarpoonPropHunt_IsHider(void);
    s32 HarpoonPropHunt_IsSeeker(void);
    s32 HarpoonPropHunt_IsEliminated(void);
    s32 HarpoonPropHunt_GetLocalPropCategory(void);
    s32 HarpoonPropHunt_GetLocalPropIndex(void);
    s32 HarpoonPropHunt_GetLocalPropState(void);
    s32 HarpoonPropHunt_GetConfirmedMapIndex(void);
    // Direct prop-draw intercept for z_player.c Player_Draw. Returns 1 if a
    // prop was drawn at the player's transform (caller should `return` to
    // skip vanilla Link draw entirely), 0 to fall through to vanilla. Mirrors
    // Scooter's HarpoonPropHunt_DrawProp early-return pattern.
    s32 HarpoonPropHunt_TryDrawLocalProp(Actor* thisx, PlayState* play);
}

#endif  // __cplusplus
#endif  // SOH_NETWORK_HARPOON_PROP_HUNT_H
