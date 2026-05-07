#pragma once

#ifdef __cplusplus

#include <string>
#include <vector>

namespace HarpoonSkinSync {

// ============================================================================
// Initialisation
// ============================================================================
// Called once at game startup (after Ship::Context is ready). Scans
// harpoon_skin_sync/ for .o2r files, opens each via Ship::O2rArchive WITHOUT
// mounting it globally, and pre-loads every DL whose name ends in "DL" into
// the override map keyed by .o2r filename + OTR path.
void InitO2rOverrides();


// ============================================================================
// Per-actor remote render
// ============================================================================
// HarpoonDummyPlayer_Draw calls these to push every override-style .o2r the
// remote has globally enabled (broadcast in their enabledO2rMods list) onto
// the active-overrides stack for the duration of one Player_Draw call. The
// pak_loader hook then redirects matching gSPDisplayList paths to the
// override Gfx* via HarpoonSkinSync_GetDLOverride (declared as plain C below).
void BeginRemoteOverrides(const std::vector<std::string>& enabledMods);
void EndRemoteOverrides();

// Vanilla skeleton accessors. Pre-loaded at HarpoonSkinSync::InitO2rOverrides()
// — which runs BEFORE InitMods() mounts user .o2r mods — so the cached pointer
// references the unmodified Link skeleton. Used by HarpoonDummyPlayer_Draw to
// swap a dummy's skelAnime.skeleton when no per-player .pak skin is active,
// preventing the LOCAL user's globally-mounted skeleton mods (which redefine
// limb path names) from contaminating the REMOTE's dummy. Returns nullptr if
// the resource manager wasn't ready at init time (game starting in some weird
// mode); callers should fall through to the engine's default behaviour.
void** GetVanillaLinkLimbTable(bool isAdult);
int   GetVanillaLinkDListCount(bool isAdult);

// Active-override Link skeleton accessors. When a remote dummy has at least
// one matching override that bundles its own gLink*Skel (community packers
// often store these at `alt/objects/object_link_*/`), the dummy's skelAnime
// is swapped to walk the override's skeleton instead of vanilla — so the
// override's custom limb-DL paths (e.g. `bone003_*_layer_Opaque`) actually
// get queried at runtime and resolved against the override's dlsByPath map.
// Without this, vanilla skel walks vanilla limb names and the override's
// bone${N}_* DLs are never queried, producing chimera renders (some vanilla
// limbs + a few override-replaced ones at vanilla path names). Returns
// nullptr / 0 when no active override has a skeleton for the given age.
void** GetActiveOverrideLinkLimbTable(bool isAdult);
int   GetActiveOverrideLinkDListCount(bool isAdult);

// ============================================================================
// Notifications (UI)
// ============================================================================

// Warn once per session when a remote player reports a pak skin name that
// we can't resolve locally (not present in mods/harpoon_skin_sync/).
void NotifyMissingPak(uint32_t clientId, const std::string& playerName, const std::string& skinName);

// Compare our enabled .o2r mod list against a remote's and emit one
// divergence notification per unique (clientId, direction, modName) tuple.
// Suppressed for any mod the local user has installed in harpoon_skin_sync/
// (because the dummy is rendered with that override at draw time).
// Same as above, but also takes the remote's harpoon_skin_sync registry
// (the names of mods THEY have available to render others). Suppresses
// the "you have mod X that they don't" notification when X is in their
// sync registry (they CAN render us with it). Without this, the warning
// fires even when the other side has the mod available — just not
// mounted globally.
void NotifyO2rDivergence(uint32_t clientId, const std::string& playerName,
                         const std::vector<std::string>& remoteMods,
                         const std::vector<std::string>& remoteSyncMods);

// Returns the names of all overrides loaded from the local
// harpoon_skin_sync/ folder. Broadcast to remotes so they can see what
// mods we can render them with — suppressing one-sided warnings.
std::vector<std::string> GetOverrideNames();

// Clear the dedupe cache + active override stack (call on disconnect /
// fresh connect).
void Reset();

} // namespace HarpoonSkinSync

#endif // __cplusplus

// C-callable hook for pak_loader's gSPDisplayList override path. Forward-
// declared as a plain C function so pak_loader.cpp (and any other C++ TU)
// can declare `extern "C" Gfx* HarpoonSkinSync_GetDLOverride(const char*);`
// at its call site without dragging in the std::vector / std::string types
// of the C++ namespace above.

#ifdef __cplusplus
extern "C" {
#endif

// Called from z_player_lib.c for the hand / sheath / waist limb branches in
// Player_OverrideLimbDrawGameplayCommon. Those branches resolve their DL
// pointer with `ResourceMgr_LoadGfxByName(path)` — a GLOBAL ArchiveManager
// lookup that returns whatever the local user's mods/ has at that path, then
// hands a real Gfx* to gSPDisplayList. By that point the engine no longer
// sees an `__OTR__...` string, so PakLoader / HarpoonSkinSync_GetDLOverride
// never get a chance to intercept — and the dummy ends up wearing the
// LOCAL user's modded hands on top of a remote skin's body.
//
// During a Harpoon dummy draw (sInRemoteDraw == true) this returns:
//   1. an active override's matching DL (if the remote's skin bundles its
//      own hand/sheath at the canonical OOT path);
//   2. otherwise the patched-vanilla copy from sVanillaDLs (every OTR
//      command pre-resolved against oot.o2r so no global lookup happens
//      while the bytecode runs);
//   3. NULL when neither is available — caller falls back to the original
//      `ResourceMgr_LoadGfxByName(path)` so we never break the local
//      player's own draw.
// `otrPath` is the raw `__OTR__objects/...` string from the DL pointer
// table (what `dLists[sDListsLodOffset]` actually contains before the
// global-stack resolution). Returns void* to keep this header free of any
// libultra/gbi.h transitive include — callers cast back to `Gfx*`.
void* HarpoonSkinSync_ResolvePlayerLimbDL(const char* otrPath);

#ifdef __cplusplus
} // extern "C"
#endif
