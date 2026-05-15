#include "Harpoon.h"
#include "PropHunt/PropHunt.h"
#include "TriforceThief/TriforceThief.h"
#include "DroppedItems.h"
#include <libultraship/libultraship.h>
#include <imgui.h>
#include <cstdlib>
#include <cstring>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/cosmetics/cosmeticsTypes.h"
#include "soh/frame_interpolation.h"
#include "soh/OTRGlobals.h"
#include "soh/Network/Anchor/Anchor.h"

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "objects/gameplay_keep/gameplay_keep.h"
extern PlayState* gPlayState;
extern MapData* gMapData;
float OTRGetDimensionFromLeftEdge(float v);
float OTRGetDimensionFromRightEdge(float v);
}

// Global one-frame flag set by PropHunt/TriforceThief TeleportToEntrance
// helpers to authorize an upcoming scene transition. The round-active
// blocker below cancels any TRANS_TRIGGER_START that wasn't preceded by
// setting this. Defined in PropHunt.cpp at file scope (outside any
// namespace).
extern bool sHarpoonAuthorizedTransition;

void Harpoon::RegisterHooks() {

    // Spawn dummy players when entering a new scene. Push a fresh
    // PLAYER.UPDATE_VISUAL_STATE so the server's AOI table knows our new
    // sceneNum — the per-frame TRANSFORM packets are filtered by
    // `same_scene_as=session` server-side, and without an updated VisualState
    // the server still thinks we're in the previous scene (or, on first
    // connect from title, scene 255).
    COND_HOOK(OnSceneSpawnActors, isConnected, [&]() {
        if (IsSaveLoaded()) {
            SendPacket_PlayerVisualState();
            RefreshClientActors();
        }
        // Prop Hunt: refresh the ghost-actor registry on every scene change.
        // SpawnGhostActors is currently a stub; the destroy half is still
        // correct to call so stale pointers from the previous scene are
        // cleared regardless.
        if (isPropHuntMode && gPlayState != nullptr) {
            HarpoonPropHunt::DestroyGhostActors(gPlayState);
            HarpoonPropHunt::SpawnGhostActors(gPlayState);
        }
        // Apply any pending role preset (hider/seeker kit) now that the
        // new scene has spawned — Inventory_* changes only stick post-load.
        HarpoonPropHunt::ProcessPendingInit();

        // Triforce Thief: announce scene-loaded so an armed cutscene timer
        // can flip ready and start the subcamera orbit. (Round start fires
        // CUTSCENE_BEGIN BEFORE the teleport, so cutsceneReady stays false
        // until this hook runs in the new scene.)
        if (currentRoomGameMode == "triforce_thief") {
            HarpoonTriforceThief::OnSceneLoaded();
        }

        // Dropped-item ledger: every scene load, materialize the ground
        // actors for any unclaimed drops whose `sceneNum` matches this
        // scene. Late joiners get a snapshot on connect (see RoomJoined
        // handler) so their ledger is populated before this fires.
        // Gated on RPG mode — other gamemodes shouldn't render drops
        // even if a peer accidentally broadcasts one.
        if (gPlayState != nullptr && currentRoomGameMode == "rpg") {
            HarpoonDroppedItems::SpawnInScene(gPlayState);
        }
    });

    // Intercept ACTOR_PLAYER spawns to create Harpoon dummy players
    COND_ID_HOOK(ShouldActorInit, ACTOR_PLAYER, isConnected, [&](void* actorRef, bool* should) {
        Actor* actor = (Actor*)actorRef;

        if (spawningDummyPlayerForClientId != 0) {
            SetDummyPlayerClientId(actor, spawningDummyPlayerForClientId);

            Actor_ChangeCategory(gPlayState, &gPlayState->actorCtx, actor, ACTORCAT_NPC);
            actor->id = ACTOR_EN_OE2;
            actor->category = ACTORCAT_NPC;
            actor->init = HarpoonDummyPlayer_Init;
            actor->update = HarpoonDummyPlayer_Update;
            actor->draw = HarpoonDummyPlayer_Draw;
            actor->destroy = HarpoonDummyPlayer_Destroy;
        }
    });

    // Send player update every frame
    COND_HOOK(OnPlayerUpdate, isConnected, [&]() {
        if (justLoadedSave) {
            justLoadedSave = false;
            // PULL teammates' state into our save (we're a late joiner). The
            // server forwards this to any teammate that responds with their
            // UpdateTeamState. PUSH'ing on load would clobber the team's
            // progress with our (likely empty) save.
            SendPacket_RequestTeamState();
        }

        // --- GM-mode movement restrictions ---
        // Apply the host-set restrict flags to the local player every
        // frame. Each restrict clears the corresponding stateFlag bits
        // so the engine refuses the action.
        {
            auto myIt = clients.find(ownClientId);
            if (myIt != clients.end() && gPlayState != nullptr) {
                Player* lp = GET_PLAYER(gPlayState);
                if (lp != nullptr) {
                    if (myIt->second.restrictNoClimb) {
                        lp->stateFlags1 &= ~(PLAYER_STATE1_CLIMBING_LADDER |
                                             PLAYER_STATE1_HANGING_OFF_LEDGE |
                                             PLAYER_STATE1_CLIMBING_LEDGE);
                    }
                    if (myIt->second.restrictNoGrab) {
                        lp->stateFlags1 &= ~PLAYER_STATE1_CARRYING_ACTOR;
                    }
                    if (myIt->second.restrictNoTalk) {
                        lp->stateFlags1 &= ~PLAYER_STATE1_TALKING;
                    }
                    // No-crawl: clear the engine's crawl state via the
                    // same flag mask pattern. (No dedicated bit — engine
                    // gates on stateFlags1+itemAction. Treat as a TODO if
                    // crawl-restriction observable behaviour is needed.)
                    (void)myIt->second.restrictNoCrawl;
                }
            }
        }

        // Defensive: ensure server knows we're in-game. OnSceneSpawnActors
        // normally fires this on every scene transition, but if the chain
        // breaks (e.g. our save loaded mid-flight from a previous failed
        // hook), the server's session.scene_num stays at 255 and AOI rejects
        // every transform we send → teammates never see us.
        if (!visualStateSentSinceLoad && IsSaveLoaded()) {
            visualStateSentSinceLoad = true;
            SendPacket_PlayerVisualState();
        }

        if (shouldRefreshActors) {
            shouldRefreshActors = false;
            RefreshClientActors();
        }

        // Cutscene trigger detection: when cutsceneIndex transitions from 0
        // (or different) to a fresh value, broadcast it so other players in
        // the same scene can replay it. Skip during incoming-packet apply to
        // avoid loops.
        static s32 lastCutsceneIndex = 0;
        s32 csIdx = gSaveContext.cutsceneIndex;
        if (csIdx != lastCutsceneIndex) {
            lastCutsceneIndex = csIdx;
            if (csIdx != 0 && !isProcessingIncomingPacket && gPlayState != nullptr) {
                SendPacket_CutsceneTrigger(csIdx, gPlayState->sceneNum);
            }
        }

        SendPacket_PlayerUpdate();

        // ---------------------------------------------------------------
        // Prop Hunt: R toggles "prop mode" (Scooter pattern). In prop mode:
        //   D-Left / D-Down / D-Right = pick category (Env / Enemies / NPCs)
        //   C-Left / C-Right          = cycle prop within category
        //   C-Down / B                = cycle state variant
        //   D-Up                       = spawn decoy at current pos
        // Outside prop mode the buttons run their vanilla action.
        //
        // Pause blocker (mirrors Scooter): hiders can't open pause/equipment
        // mid-round — would let them pick safe items the kit doesn't include.
        // Clear START presses and force pauseCtx.state=0 if it somehow opened.
        // ---------------------------------------------------------------
        // Allow prop toggle when local is Hider (during a round) OR when in
        // the LOBBY (no role). User spec: lobby is a no-stakes sandbox where
        // players can practice disguising. SET_DISGUISE broadcast still fires
        // so peers see the lobby disguise via HarpoonDummyPlayer.
        bool propInputAllowed = HarpoonPropHunt::IsHider() ||
                                (Harpoon::Instance != nullptr &&
                                 Harpoon::Instance->gameState == HARPOON_STATE_LOBBY);
        if (isPropHuntMode && propInputAllowed && gPlayState != nullptr) {
            Input* input = &gPlayState->state.input[0];
            static u8 sSavedButtonItems[8] = {};
            static u8 sSavedCButtonSlots[7] = {};
            static bool sSavedBindings = false;

            // Prop mode is now derived from propIndex (>=0 → prop visible)
            // rather than a separate boolean. This keeps the button-icon
            // overrides + render state always in sync — if propIndex
            // somehow got cleared by a different code path, we automatically
            // restore vanilla bindings. BigStartGameAs(Hider) seeds
            // propIndex=0 so hiders start disguised on join.
            auto& s = HarpoonPropHunt::GetLocalState();
            bool inPropMode = (s.propIndex >= 0 && s.propIndex < HarpoonPropHunt::kPropsPerCategory);

            // Damage-cooldown lockout: while propModeLockoutTimer > 0,
            // R refuses to re-enter prop mode (we'd let exit fire so the
            // user can still cancel a residual disguise, but no entry).
            bool lockedOut = (s.propModeLockoutTimer > 0);

            // R: toggle prop mode. Saves bindings the first time we ENTER
            // prop mode in this session; restores them when we EXIT.
            if (CHECK_BTN_ALL(input->press.button, BTN_R) && !lockedOut) {
                if (inPropMode) {
                    // Exit → vanilla Link.
                    s.propIndex = -1;
                    s.propState = 0;
                    if (sSavedBindings) {
                        for (int i = 0; i < 8; i++) gSaveContext.equips.buttonItems[i] = sSavedButtonItems[i];
                        for (int i = 0; i < 7; i++) gSaveContext.equips.cButtonSlots[i] = sSavedCButtonSlots[i];
                    }
                    SendJsonToRemote(HarpoonPropHunt::BuildSetDisguisePayload());
                    inPropMode = false;
                } else {
                    // Enter → first prop in current category.
                    s.propIndex = 0;
                    if (s.propCategory < 0) s.propCategory = HarpoonPropHunt::CAT_ENVIRONMENT;
                    if (!sSavedBindings) {
                        for (int i = 0; i < 8; i++) sSavedButtonItems[i]   = gSaveContext.equips.buttonItems[i];
                        for (int i = 0; i < 7; i++) sSavedCButtonSlots[i] = gSaveContext.equips.cButtonSlots[i];
                        sSavedBindings = true;
                    }
                    SendJsonToRemote(HarpoonPropHunt::BuildSetDisguisePayload());
                    inPropMode = true;
                }
                input->press.button &= ~BTN_R;
                input->cur.button   &= ~BTN_R;
            }

            // First frame in prop mode this session: snapshot bindings.
            if (inPropMode && !sSavedBindings) {
                for (int i = 0; i < 8; i++) sSavedButtonItems[i]   = gSaveContext.equips.buttonItems[i];
                for (int i = 0; i < 7; i++) sSavedCButtonSlots[i] = gSaveContext.equips.cButtonSlots[i];
                sSavedBindings = true;
                // Broadcast initial disguise — joiners need it for the
                // remote dummy render to pick up the right prop.
                SendJsonToRemote(HarpoonPropHunt::BuildSetDisguisePayload());
            }

            if (inPropMode) {
                // Re-apply prop hunt icons every frame (scene reloads or
                // post-init code can overwrite buttonItems otherwise).
                gSaveContext.equips.buttonItems[1] = ITEM_PH_ICON_PREV;    // C-Left:  prev prop
                gSaveContext.equips.buttonItems[2] = ITEM_PH_ICON_CHANGE;  // C-Down:  change state
                gSaveContext.equips.buttonItems[3] = ITEM_PH_ICON_NEXT;    // C-Right: next prop
                gSaveContext.equips.buttonItems[4] = ITEM_CANE_OF_SOMARIA; // D-Up:    decoy
                gSaveContext.equips.buttonItems[5] = ITEM_PH_ICON_ENEMY;   // D-Down:  Enemies category
                gSaveContext.equips.buttonItems[6] = ITEM_PH_ICON_POT;     // D-Left:  Environment category
                gSaveContext.equips.buttonItems[7] = ITEM_PH_ICON_NPC;     // D-Right: NPCs category
                // Prevent the engine from overwriting these via the
                // cButtonSlots → inventory lookup pipeline.
                for (int s = 0; s < 7; s++) {
                    gSaveContext.equips.cButtonSlots[s] = SLOT_NONE;
                }
            }

            if (inPropMode) {
                bool changed = false;
                // Category select (D-pad).
                if (CHECK_BTN_ALL(input->press.button, BTN_DLEFT)) {
                    auto& s = HarpoonPropHunt::GetLocalState();
                    s.propCategory = HarpoonPropHunt::CAT_ENVIRONMENT;
                    s.propIndex = 0; s.propState = 0;
                    changed = true;
                }
                if (CHECK_BTN_ALL(input->press.button, BTN_DDOWN)) {
                    auto& s = HarpoonPropHunt::GetLocalState();
                    s.propCategory = HarpoonPropHunt::CAT_ENEMIES;
                    s.propIndex = 0; s.propState = 0;
                    changed = true;
                }
                if (CHECK_BTN_ALL(input->press.button, BTN_DRIGHT)) {
                    auto& s = HarpoonPropHunt::GetLocalState();
                    s.propCategory = HarpoonPropHunt::CAT_NPCS;
                    s.propIndex = 0; s.propState = 0;
                    changed = true;
                }
                // Prop cycle (C-Left / C-Right).
                if (CHECK_BTN_ALL(input->press.button, BTN_CRIGHT))
                    changed |= HarpoonPropHunt::CyclePropIndex(+1);
                if (CHECK_BTN_ALL(input->press.button, BTN_CLEFT))
                    changed |= HarpoonPropHunt::CyclePropIndex(-1);
                // State cycle (C-Down / B).
                if (CHECK_BTN_ALL(input->press.button, BTN_CDOWN) ||
                    CHECK_BTN_ALL(input->press.button, BTN_B))
                    changed |= HarpoonPropHunt::CyclePropState(+1);
                // Decoy spawn (D-Up).
                if (CHECK_BTN_ALL(input->press.button, BTN_DUP)) {
                    HarpoonPropHunt::SpawnDecoy();
                }
                if (changed) {
                    SendJsonToRemote(HarpoonPropHunt::BuildSetDisguisePayload());
                }
                // Consume the buttons in prop mode so the vanilla actions
                // don't fire (don't pull out the sword, etc.).
                input->press.button &= ~(BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT |
                                          BTN_CLEFT | BTN_CRIGHT | BTN_CDOWN | BTN_CUP |
                                          BTN_A | BTN_B);
                input->cur.button   &= ~(BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT |
                                          BTN_CLEFT | BTN_CRIGHT | BTN_CDOWN | BTN_CUP);
            }

            // Strip Start presses + safety-close pause if it opened anyway.
            input->press.button &= ~BTN_START;
            input->cur.button   &= ~BTN_START;
            if (gPlayState->pauseCtx.state != 0) {
                gPlayState->pauseCtx.state = 0;
            }
        }

        // ---------------------------------------------------------------
        // Triforce Thief: D-pad cycling for map select.
        // D-Left  = prev map, D-Right = next map. Hover broadcast per
        // change so other clients update their map-select grid live.
        // ---------------------------------------------------------------
        if (HarpoonTriforceThief::IsInMapSelect() && gPlayState != nullptr) {
            Input* input = &gPlayState->state.input[0];
            bool changed = false;
            if (CHECK_BTN_ALL(input->press.button, BTN_DLEFT))  changed |= HarpoonTriforceThief::CycleHoveredMap(-1);
            if (CHECK_BTN_ALL(input->press.button, BTN_DRIGHT)) changed |= HarpoonTriforceThief::CycleHoveredMap(+1);
            if (changed) {
                s32 hovered = HarpoonTriforceThief::GetLocalState().hoveredMap;
                SendJsonToRemote(HarpoonTriforceThief::BuildMapHoverPayload(hovered));
            }
        }
    });

    // Process incoming packets on game thread
    COND_HOOK(OnGameFrameUpdate, isConnected, [&]() {
        ProcessIncomingPacketQueue();
        UpdateDecoys();
        HarpoonPropHunt::TickFrame();

        // Dropped-item ledger: per-frame pickup proximity check + 5-min
        // despawn tick (gated on local-player-in-scene-with-drops).
        // RPG mode only — no-op in other gamemodes.
        if (currentRoomGameMode == "rpg") {
            HarpoonDroppedItems::TickPickupPoll();
            HarpoonDroppedItems::TickExpiry();
        }

        // Round-active loading-zone blocker. Mirrors Scooter's
        // TriforceThief_DestroyGrottos hook. Kills any DoorAna (grotto)
        // actor that spawns and cancels any unauthorized scene transition,
        // so hiders / seekers / thieves can't escape the chosen map by
        // walking into a warp. Authorized scene changes are gated by
        // the gamemode's own teleport helpers (which clear the flag).
        bool inRoundActive  = gPlayState != nullptr &&
                              (gameState == HARPOON_STATE_PLAYING ||
                               gameState == HARPOON_STATE_HIDING_PHASE) &&
                              (isPropHuntMode ||
                               currentRoomGameMode == "triforce_thief");
        if (inRoundActive) {
            // Kill grottos. DoorAna lives in ACTORCAT_ITEMACTION; walk the
            // chain and Actor_Kill every match. Doing this every frame
            // means grottos that re-spawn after a room reload are caught
            // immediately too — this is the "invisible wall" for grottos.
            Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_ITEMACTION].head;
            while (a != nullptr) {
                Actor* next = a->next;
                if (a->id == ACTOR_DOOR_ANA) {
                    Actor_Kill(a);
                }
                a = next;
            }
            // Also kill blue/yellow warp pads (`ACTOR_DOOR_WARP1`) — boss
            // rooms / spirit temple etc. spawn these to send the player
            // out of the dungeon. We don't want them firing mid-round.
            Actor* b = gPlayState->actorCtx.actorLists[ACTORCAT_DOOR].head;
            while (b != nullptr) {
                Actor* nxt = b->next;
                if (b->id == ACTOR_DOOR_WARP1) {
                    Actor_Kill(b);
                }
                b = nxt;
            }

            // Cancel scene transitions unless our own teleport helper
            // armed `sAuthorizedTransition`. The helper sets it right
            // before calling TeleportToEntrance; we clear the flag here
            // once consumed so it can't leak into the next frame.
            // Use `::name` everywhere — MSVC mangles unqualified
            // function-scope `extern` as class/namespace-qualified.
            //
            // CRITICAL: only cancel `TRANS_TRIGGER_START` (= 20, the
            // "exiting an area" trigger). `TRANS_TRIGGER_END` (= -20) is
            // set BY THE ENGINE when arriving in a new area — cancelling
            // it traps the player in an infinite re-load loop because
            // the engine immediately re-issues it to finish the arrival
            // fade-in. Same for `respawnFlag`: only block its INITIAL
            // set (player walking off a cliff); after Authorized teleport
            // has consumed it, leave it alone for the engine's post-load
            // bookkeeping.
            if (gPlayState->transitionTrigger == TRANS_TRIGGER_START &&
                !::sHarpoonAuthorizedTransition) {
                // Full-circle scene-lock (PH only). If the engine's
                // `nextEntranceIndex` would land us in a scene that's part of
                // the round's cluster, let the transition happen. Otherwise
                // redirect `nextEntranceIndex` to bring the player back to
                // the round map. The fade animation still runs (so it feels
                // like walking through a door) but the destination is the
                // round map, not the out-of-bounds scene.
                bool redirected = false;
                s32 destEntr  = gPlayState->nextEntranceIndex;
                s32 destScene = -1;
                if (destEntr >= 0 && destEntr < (s32)ARRAY_COUNT(gEntranceTable)) {
                    destScene = (s32)gEntranceTable[destEntr].scene;
                }
                s32 mapIdx = (Harpoon::Instance != nullptr)
                                 ? Harpoon::Instance->confirmedMapIndex : -1;
                if (isPropHuntMode) {
                    if (mapIdx >= 0 && destScene >= 0 &&
                        !HarpoonPropHunt::IsSceneInRoundCluster(mapIdx, destScene)) {
                        s32 returnEntr = HarpoonPropHunt::GetReturnEntranceForInvalidExit(
                            mapIdx, destScene);
                        if (returnEntr >= 0) {
                            gPlayState->nextEntranceIndex = returnEntr;
                            gPlayState->transitionType    = TRANS_TYPE_FADE_BLACK;
                            ::sHarpoonAuthorizedTransition = true;
                            redirected = true;
                        }
                    }
                } else if (currentRoomGameMode == "triforce_thief") {
                    // Same full-circle redirect for TT: when the engine
                    // wants to load us into a scene outside the round's
                    // cluster, swap the entrance to the round map's main
                    // entrance instead. Player fades through the door and
                    // arrives back in the round map.
                    if (mapIdx >= 0 && destScene >= 0 &&
                        !HarpoonTriforceThief::IsSceneInRoundClusterTT(mapIdx, destScene)) {
                        s32 returnEntr = HarpoonTriforceThief::GetReturnEntranceForInvalidExitTT(
                            mapIdx, destScene);
                        if (returnEntr >= 0) {
                            gPlayState->nextEntranceIndex = returnEntr;
                            gPlayState->transitionType    = TRANS_TYPE_FADE_BLACK;
                            ::sHarpoonAuthorizedTransition = true;
                            redirected = true;
                        }
                    }
                }

                if (!redirected) {
                    // Backstop: cancel the unauthorized transition + restore
                    // control. The engine sets PLAYER_STATE1_LOADING |
                    // PLAYER_STATE1_IN_CUTSCENE in z_player.c when starting
                    // a loading-zone transition; cancelling the trigger
                    // alone leaves those flags set and the player stuck in
                    // the walking-into-door animation. Zero velocity too so
                    // the animation curve has no input to push forward.
                    gPlayState->transitionTrigger = TRANS_TRIGGER_OFF;
                    Player* locked = GET_PLAYER(gPlayState);
                    if (locked != nullptr) {
                        locked->stateFlags1 &= ~(PLAYER_STATE1_LOADING |
                                                 PLAYER_STATE1_IN_CUTSCENE);
                        locked->linearVelocity = 0.0f;
                        locked->actor.speedXZ  = 0.0f;
                    }
                }
            }
            // Note: respawnFlag block removed for now — it was wiping the
            // engine's legitimate post-arrival respawn state and causing
            // softlocks on scene entry. Void-out from cliffs is a rare
            // edge case; cancelling all respawnFlag values broke normal
            // map entry. If void-out becomes a problem, we'll add a
            // narrower guard (e.g. only block when player is actually
            // mid-fall) but not here.

            // Only clear the authorized-transition flag once the engine
            // has fully consumed the trigger (returned it to OFF). The
            // previous unconditional clear at end-of-block raced the
            // engine: TickFrame would call TeleportToEntrance (sets
            // trigger=START, flag=true), this block would skip its cancel
            // (correct), then clear the flag — and on the NEXT frame our
            // own redirect/cancel logic would fire because trigger was
            // still START. Result: authorized teleport got cancelled
            // mid-flight (e.g. seeker post-hide-phase teleport). Keeping
            // the flag set until trigger==OFF means multi-frame transitions
            // survive intact.
            if (gPlayState->transitionTrigger == TRANS_TRIGGER_OFF) {
                ::sHarpoonAuthorizedTransition = false;
            }
        }

        if (currentRoomGameMode == "triforce_thief") {
            HarpoonTriforceThief::TickFrame();
        }

        // Periodic ghost-actor respawn retry. The normal spawn path runs
        // from OnSceneSpawnActors but races with packet ordering — if we
        // join a prop_hunt room after the scene already loaded, that hook
        // never fires for our entry into Hyrule Field and the prop draw
        // path stays broken until a manual scene change. Re-attempt every
        // 5 seconds while we're in prop hunt mode AND the registry is empty.
        if (isPropHuntMode && gPlayState != nullptr &&
            !HarpoonPropHunt::AreGhostsReady()) {
            static s64 sLastSpawnRetry = 0;
            s64 nowMs = (s64)(ImGui::GetTime() * 1000.0);
            // 1-second cadence — fast enough that joining a room and entering
            // hide-mode feels instant, slow enough to not spam Actor_Spawn.
            if (nowMs - sLastSpawnRetry > 1000) {
                sLastSpawnRetry = nowMs;
                HarpoonPropHunt::SpawnGhostActors(gPlayState);
            }
        }

        // Z+L+R combo — context-dependent action:
        //  * LOBBY + admin → open map select (start a new round)
        //  * Mid-round (any player) → reload the current scene from its
        //    entrance. Useful as a soft-anti-softlock: stuck in a wall,
        //    fell into a hole, cutscene jammed, etc. — anyone can press
        //    the combo to teleport back to the round's spawn point
        //    without dragging everyone else with them.
        bool inPropHuntRoom    = isPropHuntMode;
        bool inTriforceRoom    = (currentRoomGameMode == "triforce_thief");
        if ((inPropHuntRoom || inTriforceRoom) && gPlayState != nullptr) {
            Input* input = &gPlayState->state.input[0];
            const u32 combo = BTN_L | BTN_R | BTN_Z;
            static bool sComboFired = false;
            if ((input->cur.button & combo) == combo) {
                if (!sComboFired && gameState != HARPOON_STATE_MAP_SELECT) {
                    sComboFired = true;
                    bool isLobby = (gameState == HARPOON_STATE_LOBBY);
                    // Host is the sole authority (admin concept removed).
                    bool isHostLocal = (ownClientId != 0 &&
                                        ownClientId == hostClientId);

                    if (isLobby && isHostLocal) {
                        // Lobby + admin → open the map-select overlay.
                        s32 modeInt = (s32)mapSelectMode;
                        nlohmann::json env;
                        env["type"] = "ROOM.BROADCAST_EVENT";
                        env["event_name"] = inPropHuntRoom
                            ? "PROP_HUNT.OPEN_MAP_SELECT"
                            : "TRIFORCE_THIEF.MAP_SELECT_BEGIN";
                        env["data"] = nlohmann::json::object();
                        env["data"]["mapSelectMode"] = modeInt;
                        env["data"]["windowSeconds"] = 15;
                        SendJsonToRemote(env);
                        // Locally dispatch through HandleEvent so the
                        // broadcaster runs the same handler peers do.
                        if (inTriforceRoom) {
                            HarpoonTriforceThief::HandleEvent(env);
                        } else {
                            HarpoonPropHunt::HandleEvent(env);
                        }
                    } else if (!isLobby) {
                        // Mid-round → reload the currently SELECTED round
                        // map's entrance (local-only, no broadcast). The
                        // previous version used gSaveContext.entranceIndex
                        // which gets baked to Hyrule Field (205) by
                        // ApplyBaseHealthMagic at round start and never
                        // updated by TeleportToEntrance — so every reload
                        // yanked the player back to the lobby, ending the
                        // round server-side. Use the per-gamemode
                        // GetEntranceFor*(confirmedMapIndex) helper instead.
                        if (gPlayState != nullptr) {
                            s32 reloadEntrance = gSaveContext.entranceIndex;
                            s32 mapIdx = (ownClientId != 0)
                                ? confirmedMapIndex : -1;
                            if (inPropHuntRoom && mapIdx >= 0) {
                                reloadEntrance =
                                    HarpoonPropHunt::GetEntranceForMapIndex(mapIdx);
                            } else if (inTriforceRoom && mapIdx >= 0) {
                                reloadEntrance =
                                    HarpoonTriforceThief::GetEntranceForMap(mapIdx);
                            }
                            gPlayState->linkAgeOnLoad     = gSaveContext.linkAge;
                            gPlayState->nextEntranceIndex = reloadEntrance;
                            gPlayState->transitionTrigger = TRANS_TRIGGER_START;
                            gPlayState->transitionType    = TRANS_TYPE_FADE_BLACK;
                            ::sHarpoonAuthorizedTransition = true;
                        }
                    }
                }
            } else {
                sComboFired = false;
            }

            // Random mode: the moment we enter map_select state and we're
            // the host, pick a random map and broadcast MAP_CONFIRMED. No
            // user input required — the overlay flashes by then transitions
            // out. Matches Scooter's HarpoonGameState handler at line 1053.
            static HarpoonGameState sPrevMapState = HARPOON_STATE_LOBBY;
            bool justEnteredMapSelect = (sPrevMapState != HARPOON_STATE_MAP_SELECT &&
                                          gameState == HARPOON_STATE_MAP_SELECT);
            sPrevMapState = gameState;
            bool isHost = (ownClientId != 0 && ownClientId == hostClientId);
            if (justEnteredMapSelect && isHost &&
                mapSelectMode == MAP_SELECT_RANDOM) {
                if (inPropHuntRoom) {
                    s32 mapCount = 10; // PROP_HUNT_MAP_SELECT_COUNT incl. RANDOM cell
                    s32 pick = (s32)(rand() % (mapCount - 1));  // skip the RANDOM cell itself
                    // Full round-start: picks seekers, assigns roles, broadcasts,
                    // teleports hiders. Seekers stay in lobby until hide-phase end.
                    HarpoonPropHunt::HostStartRound(pick);
                } else if (inTriforceRoom) {
                    s32 pick = (s32)(rand() % HarpoonTriforceThief::kMapCount);
                    HarpoonTriforceThief::HostConfirmMap(pick);
                }
            }
        }

        // Triforce Thief: when the local player walks into the Triforce
        // pickup AABB, broadcast a Pickup event AND apply locally — the
        // server-relay excludes the sender from its own broadcast, so
        // without the local apply our own `carrierClientId` would stay 0
        // and the VB_ACTOR_POST_DRAW hook wouldn't render the Triforce
        // above our head. Authority semantics (first-claim wins) are
        // resolved at the relay layer; if a race happens, the later
        // message overwrites in HandleTriforcePickup. Acceptable for v1.
        if (HarpoonTriforceThief::IsInRound() && gPlayState != nullptr) {
            Player* p = GET_PLAYER(gPlayState);
            if (p != nullptr) {
                if (HarpoonTriforceThief::ShouldPickupTriforce(
                        p->actor.world.pos.x, p->actor.world.pos.y, p->actor.world.pos.z)) {
                    HarpoonTriforceThief::GetLocalState().carrierClientId = ownClientId;
                    SendJsonToRemote(HarpoonTriforceThief::BuildTriforcePickupPayload(ownClientId));
                    // Seed / resume the rupee countdown and write it to
                    // gSaveContext.rupees so the rupee HUD shows the timer.
                    HarpoonTriforceThief::OnLocalPickup();
                }
            }
        }
    });

    // Triforce Thief: draw the world-space Triforce piece at the end of the
    // gameplay draw pass when nobody is carrying. The above-head indicator
    // for the carrier is handled separately via VB_ACTOR_POST_DRAW below.
    //
    // Prop Hunt: also render any active decoys here. Each decoy gets drawn
    // as the originator's chosen prop at the spawn world position, using a
    // throwaway "host actor" approach — we synthesize an Actor* with the
    // decoy's pos/rot and feed it to DrawHiderAsProp. Local decoys
    // (sDecoys[]) and remote ones (Harpoon::clients[cid].somariaDecoy*)
    // both rendered.
    COND_HOOK(OnPlayDrawEnd, isConnected, [&]() {
        if (HarpoonTriforceThief::IsInRound() && gPlayState != nullptr) {
            HarpoonTriforceThief::DrawTriforceOnGround(gPlayState);
        }
        if (isPropHuntMode && gPlayState != nullptr &&
            HarpoonPropHunt::AreGhostsReady()) {
            s32 mapIdx = HarpoonPropHunt::GetLocalState().confirmedMap;
            if (mapIdx < 0) mapIdx = 0;

            // Local decoys. NB: braced struct initializers inside a COND_HOOK
            // lambda explode the preprocessor (commas not protected by braces).
            // Assign fields individually.
            auto& locals = HarpoonPropHunt::GetLocalDecoys();
            for (const auto& d : locals) {
                if (!d.active) continue;
                Actor host;
                memset(&host, 0, sizeof(host));
                host.world.pos.x = d.x;
                host.world.pos.y = d.y;
                host.world.pos.z = d.z;
                host.shape.rot.y = d.rotY;
                HarpoonPropHunt::DrawHiderAsProp(&host, gPlayState,
                                                  d.propCat, d.propIndex, d.propState, mapIdx);
            }
            // Remote decoys (every other client's slots).
            for (auto& [cid, c] : clients) {
                if (c.self) continue;
                for (int i = 0; i < 3; i++) {
                    if (!c.somariaDecoyActive[i]) continue;
                    Actor host;
                    memset(&host, 0, sizeof(host));
                    host.world.pos   = c.somariaDecoyPos[i];
                    host.shape.rot.y = c.somariaDecoyRotY[i];
                    HarpoonPropHunt::DrawHiderAsProp(&host, gPlayState,
                                                     c.somariaDecoyPropCat[i],
                                                     c.somariaDecoyPropIdx[i],
                                                     c.somariaDecoyPropState[i],
                                                     mapIdx);
                }
            }
        }
    });

    // Triforce Thief: when the local carrier takes damage, drop the Triforce
    // at their current position so other thieves can pick it up.
    //
    // Prop Hunt: when the local hider's health hits 0, they're "eliminated"
    // and convert to a seeker for the rest of the round (Scooter behaviour
    // — keeps the round going instead of leaving the hider as a spectator).
    COND_HOOK(OnPlayerHealthChange, isConnected, [&](int16_t amount) {
        // --- RPG-mode death-drop ---
        // Gated on currentRoomGameMode == "rpg" so PH / TT / randomizer
        // don't accidentally fire drop-on-death. Detect HP=0 transition:
        // soft-death (fairy in bottle) drops 2 random items + 20% rupees;
        // game-over (no fairy) drops everything except heart upgrades.
        // Static prev-HP guard so the engine's i-frames + revive flow
        // doesn't re-trigger within the same death.
        if (currentRoomGameMode == "rpg") {
            static s16 sPrevHP = -1;
            s16 curHP = gSaveContext.health;
            if (sPrevHP > 0 && curHP <= 0) {
                HarpoonDroppedItems::TriggerLocalDeathDrop();
            }
            sPrevHP = curHP;
        }

        // --- Triforce Thief carrier drop on damage ---
        // Triforce knocked loose: pick a random landing 500-800 units away,
        // apply locally first (relay excludes sender), then broadcast. The
        // dropper gets a 90-frame pickup cooldown via HandleTriforceDrop.
        if (HarpoonTriforceThief::IsInRound() &&
            HarpoonTriforceThief::GetLocalState().carrierClientId == ownClientId &&
            amount < 0) {
            Player* p = GET_PLAYER(gPlayState);
            if (p != nullptr) {
                f32 sx = p->actor.world.pos.x;
                f32 sy = p->actor.world.pos.y + 30.0f;  // launch slightly above feet
                f32 sz = p->actor.world.pos.z;
                // Random horizontal direction + strong upward + outward kick.
                // The receiver's physics integration (gravity + BgCheck wall
                // / floor / ceiling) carries it the rest of the way — it
                // bounces off walls and settles on real geometry, so it
                // never leaves the scene.
                f32 angle = (f32)(rand() % 0x10000) * (3.14159265f / 32768.0f);
                f32 horizSpeed = 16.0f + (f32)(rand() % 6);  // 16–21 u/frame
                f32 vx = cosf(angle) * horizSpeed;
                f32 vz = sinf(angle) * horizSpeed;
                f32 vy = 14.0f;  // upward kick
                u32 me = ownClientId;
                // Local apply via synthetic event (the public API doesn't
                // expose HandleTriforceDrop, so we rebuild & dispatch the
                // event through the same path peers use).
                nlohmann::json env;
                env["type"]       = "ROOM.BROADCAST_EVENT";
                env["event_name"] = HarpoonTriforceThief::kEvtTriforceDrop;
                env["data"]       = nlohmann::json::object();
                env["data"]["dropperClientId"] = me;
                env["data"]["startX"] = sx;
                env["data"]["startY"] = sy;
                env["data"]["startZ"] = sz;
                env["data"]["velX"]   = vx;
                env["data"]["velY"]   = vy;
                env["data"]["velZ"]   = vz;
                HarpoonTriforceThief::HandleEvent(env);
                SendJsonToRemote(HarpoonTriforceThief::BuildTriforceDropPayload(
                    me, sx, sy, sz, vx, vy, vz));
            }
        }

        // --- Prop Hunt: damage handler (hider only) ---
        // Two-stage flow (Scooter parity):
        //   (a) Took damage but still alive (>1 heart) → auto-detransform.
        //       Hider can't stay hidden mid-fight; propIndex resets to -1,
        //       broadcast no-prop disguise, start a 10-sec lockout so
        //       they can't re-disguise immediately. The seeker who landed
        //       the hit gets a confirmation that the bush they shot was
        //       actually a player.
        //   (b) Health dropped to ≤ 1 heart → "death" — convert to seeker
        //       BEFORE the Game Over screen fires. Restore health, exit
        //       prop, change role, broadcast, teleport to the round map's
        //       entrance (NOT the lobby), apply seeker preset post-load.
        if (isPropHuntMode && HarpoonPropHunt::IsHider() && amount < 0) {
            auto& s = HarpoonPropHunt::GetLocalState();
            bool oneHeartLeft = (gSaveContext.health <= 16);
            if (!oneHeartLeft && s.propIndex >= 0) {
                // (a) Damage detransform. Reset prop state + broadcast.
                s.propIndex = -1;
                s.propState = 0;
                s.propModeLockoutTimer = 200; // ~10 sec (20 fps)
                SendJsonToRemote(HarpoonPropHunt::BuildSetDisguisePayload());
                SPDLOG_INFO("[Harpoon][PropHunt] hider took damage -> detransformed (lockout 10s)");
            }
            if (oneHeartLeft) {
                // (b) Die-to-seeker. Order matches Scooter:
                //   1. Restore health (4 hearts internally) so the engine's
                //      death/game-over flow never fires.
                //   2. Wipe prop state.
                //   3. Switch role to Seeker locally.
                //   4. Broadcast role change + elimination so peer rosters
                //      update and the kill feed gets a line.
                //   5. Set linkAge to CHILD (seekers are child Link), set
                //      pending init to "converted seeker" so the scene
                //      reload applies the full seeker inventory.
                //   6. Teleport to the round map's entrance.
                gSaveContext.health         = 4 * 16;
                gSaveContext.healthCapacity = 4 * 16;
                Player* pp = GET_PLAYER(gPlayState);
                if (pp != nullptr) pp->actor.colChkInfo.health = 4 * 16;
                s.propIndex             = -1;
                s.propState             = 0;
                s.propModeLockoutTimer  = 0;

                // Tell peers we're eliminated (kill feed).
                {
                    nlohmann::json env;
                    env["type"]       = "ROOM.BROADCAST_EVENT";
                    env["event_name"] = "PROP_HUNT.ELIMINATED";
                    env["data"]       = nlohmann::json::object();
                    env["data"]["victimClientId"] = ownClientId;
                    SendJsonToRemote(env);
                }
                // Local role swap + role broadcast.
                HarpoonPropHunt::GetLocalState().role = HarpoonPropHunt::Role::Seeker;
                SendJsonToRemote(HarpoonPropHunt::BuildRoleAssignPayload(
                    ownClientId, HarpoonPropHunt::Role::Seeker));

                // Teleport to the round map's entrance. PendingInit=3 =
                // "converted seeker": runs ApplySeekerSave once the new
                // scene's actors have spawned, so the kit is right.
                s32 mapIdx = (Harpoon::Instance != nullptr) ? confirmedMapIndex : -1;
                if (mapIdx < 0) mapIdx = s.confirmedMap;
                if (mapIdx >= 0) {
                    s32 entr = HarpoonPropHunt::GetEntranceForMapIndex(mapIdx);
                    gSaveContext.linkAge = LINK_AGE_CHILD;
                    HarpoonPropHunt::TeleportToEntrance(entr);
                    HarpoonPropHunt::SetPendingInit(3);
                }
                SPDLOG_INFO("[Harpoon][PropHunt] hider died -> converted to seeker on round map");
            }
        }
    });

    // Send SFX to other players
    COND_HOOK(OnPlayerSfx, isConnected, [&](u16 sfxId) { SendPacket_PlayerSfx(sfxId); });

    // Ocarina notes — only forwarded to teammates in the same scene.
    COND_HOOK(OnOcarinaNote, isConnected,
              [&](uint8_t note, float modulator, int8_t bend) { SendPacket_OcarinaSfx(note, modulator, bend); });

    // Load game → request team state (from Anchor)
    COND_HOOK(OnLoadGame, isConnected, [&](s16 fileNum) {
        justLoadedSave = true;
        // Force a fresh VisualState on next OnPlayerUpdate so the server
        // updates our session.scene_num / is_save_loaded promptly.
        visualStateSentSinceLoad = false;
    });

    // Sync full save state on save
    COND_HOOK(OnSaveFile, isConnected, [&](s16 fileNum, int sectionID) {
        if (sectionID == 0) {
            SendPacket_UpdateTeamState();
        }
    });

    // Sync items on receive (from Anchor — handles dungeon items separately)
    COND_HOOK(OnItemReceive, isConnected, [&](GetItemEntry itemEntry) {
        if (itemEntry.modIndex == MOD_NONE &&
            (itemEntry.itemId >= ITEM_KEY_BOSS && itemEntry.itemId <= ITEM_KEY_SMALL)) {
            SendPacket_UpdateDungeonItems();
            return;
        }

        SendPacket_GiveItem(itemEntry.tableId, itemEntry.getItemId);
    });

    // Sync dungeon key usage (from Anchor)
    COND_HOOK(OnDungeonKeyUsed, isConnected, [&](uint16_t mapIndex) { SendPacket_UpdateDungeonItems(); });

    // Flag sync hooks (from Anchor)
    COND_HOOK(OnFlagSet, isConnected,
              [&](s16 flagType, s16 flag) { SendPacket_SetFlag(SCENE_ID_MAX, flagType, flag); });

    COND_HOOK(OnFlagUnset, isConnected,
              [&](s16 flagType, s16 flag) { SendPacket_UnsetFlag(SCENE_ID_MAX, flagType, flag); });

    COND_HOOK(OnSceneFlagSet, isConnected,
              [&](s16 sceneNum, s16 flagType, s16 flag) { SendPacket_SetFlag(sceneNum, flagType, flag); });

    COND_HOOK(OnSceneFlagUnset, isConnected,
              [&](s16 sceneNum, s16 flagType, s16 flag) { SendPacket_UnsetFlag(sceneNum, flagType, flag); });

    // Rando check status sync (from Anchor)
    COND_HOOK(OnRandoSetCheckStatus, isConnected, [&](RandomizerCheck rc, RandomizerCheckStatus status) {
        if (!isHandlingUpdateTeamState) {
            SendPacket_SetCheckStatus(rc);
        }
    });

    COND_HOOK(OnRandoSetIsSkipped, isConnected, [&](RandomizerCheck rc, bool isSkipped) {
        if (!isHandlingUpdateTeamState) {
            SendPacket_SetCheckStatus(rc);
        }
    });

    // Entrance discovery sync (from Anchor)
    COND_HOOK(OnRandoEntranceDiscovered, isConnected,
              [&](u16 entranceIndex, u8 isReversedEntrance) { SendPacket_EntranceDiscovered(entranceIndex); });

    // Boss defeat → game complete (from Anchor). Only fires for the final
    // Ganon (ACTOR_BOSS_GANON2 = Ganondorf phase 2).
    COND_ID_HOOK(OnBossDefeat, ACTOR_BOSS_GANON2, isConnected,
                 [&](void* refActor) { SendPacket_GameComplete(); });

    // Apply tunic color from Harpoon client data
    COND_VB_SHOULD(VB_APPLY_TUNIC_COLOR, isConnected, {
        Actor* myPlayer = (Actor*)GET_PLAYER(gPlayState);
        Actor* actor = va_arg(args, Actor*);
        Color_RGB8* color = va_arg(args, Color_RGB8*);

        if (actor == myPlayer) {
            Color_RGBA8 ownColor = CVarGetColor(CVAR_HARPOON("Color.Value"), { 100, 255, 100 });
            color->r = ownColor.r;
            color->g = ownColor.g;
            color->b = ownColor.b;
            return;
        }

        uint32_t clientId = Harpoon::Instance->GetDummyPlayerClientId(actor);

        if (!Harpoon::Instance->clients.contains(clientId)) {
            return;
        }

        HarpoonClient& client = Harpoon::Instance->clients[clientId];
        color->r = client.color.r;
        color->g = client.color.g;
        color->b = client.color.b;
    });

    // Compass arrows for connected players on the minimap (mirrors Anchor's
    // OnMinimapDrawCompassIcons handler at HookHandlers.cpp:413). Iterates
    // every Harpoon client present in the same scene and draws the vanilla
    // gCompassArrowDL for each, tinted with the client's color. Generic over
    // any skin — we only read pos/rot/color from the client struct, never
    // touch the actor's draw path. Default-on; toggle via CVar.
    struct HarpoonCompassIcon {
        Vec3f pos;
        Vec3s rot;
        float scale;
        Color_RGB8 color;
    };
    COND_HOOK(OnMinimapDrawCompassIcons, isConnected, [&]() {
        if (!CVarGetInteger(CVAR_HARPOON("ShowOtherPlayersOnMinimap"), 1)) {
            return;
        }
        std::vector<HarpoonCompassIcon> icons;
        bool isInDungeon = gPlayState->sceneNum == SCENE_DEKU_TREE ||
                           gPlayState->sceneNum == SCENE_DODONGOS_CAVERN ||
                           gPlayState->sceneNum == SCENE_JABU_JABU ||
                           gPlayState->sceneNum == SCENE_FOREST_TEMPLE ||
                           gPlayState->sceneNum == SCENE_FIRE_TEMPLE ||
                           gPlayState->sceneNum == SCENE_WATER_TEMPLE ||
                           gPlayState->sceneNum == SCENE_SPIRIT_TEMPLE ||
                           gPlayState->sceneNum == SCENE_SHADOW_TEMPLE ||
                           gPlayState->sceneNum == SCENE_BOTTOM_OF_THE_WELL ||
                           gPlayState->sceneNum == SCENE_ICE_CAVERN;
        for (auto& [clientId, client] : Harpoon::Instance->clients) {
            if (client.self || !client.online) continue;
            if (client.sceneNum != gPlayState->sceneNum) continue;
            // Read pos/rot from the broadcast state (`posRot`) instead of
            // dereferencing `client.player`. The dummy actor pointer can be
            // stale across scene transitions / RefreshClientActors cycles
            // and the broadcast state is what the dummy is updated FROM
            // every frame anyway, so it's the same data + safer.
            icons.push_back(HarpoonCompassIcon{
                client.posRot.pos,
                client.posRot.rot,
                0.3f,
                client.color,
            });
        }
        // Local player drawn last so it sits on top of the others.
        Player* localPlayer = GET_PLAYER(gPlayState);
        if (localPlayer != nullptr) {
            Color_RGBA8 ownColor = CVarGetColor(CVAR_HARPOON("Color.Value"), { 100, 255, 100 });
            icons.push_back(HarpoonCompassIcon{
                localPlayer->actor.world.pos,
                localPlayer->actor.shape.rot,
                0.4f,
                { ownColor.r, ownColor.g, ownColor.b },
            });
        }

        // Adapted from Minimap_DrawCompassIcons / Anchor's mirror of it.
        s16 leftMinimapMargin   = CVarGetInteger(CVAR_COSMETIC("HUD.Margin.L"), 0);
        s16 rightMinimapMargin  = CVarGetInteger(CVAR_COSMETIC("HUD.Margin.R"), 0);
        s16 bottomMinimapMargin = CVarGetInteger(CVAR_COSMETIC("HUD.Margin.B"), 0);
        s16 xMarginsMinimap = 0;
        s16 yMarginsMinimap = 0;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.UseMargins"), 0) != 0) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosType"), 0) == ORIGINAL_LOCATION) {
                xMarginsMinimap = rightMinimapMargin;
            }
            yMarginsMinimap = bottomMinimapMargin;
        }
        s16 mapWidth     = isInDungeon ? R_DGN_MINIMAP_X : R_OW_MINIMAP_X;
        s16 mapStartPosX = isInDungeon ? 96 : gMapData->owMinimapWidth[R_MAP_INDEX];

        OPEN_DISPS(gPlayState->state.gfxCtx);
        Gfx_SetupDL_42Overlay(gPlayState->state.gfxCtx);
        for (auto& icon : icons) {
            gSPMatrix(OVERLAY_DISP++, &gMtxClear, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                              PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
            gDPSetEnvColor(OVERLAY_DISP++, 0, 0, 0, 255);
            gDPSetCombineMode(OVERLAY_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);

            s16 mirrorOffset =
                ((mapWidth / 2) - ((R_COMPASS_OFFSET_X / 10) - (mapStartPosX - SCREEN_WIDTH / 2))) * 2 * 10;
            s16 tempX = (s16)icon.pos.x;
            s16 tempZ = (s16)icon.pos.z;
            tempX /= R_COMPASS_SCALE_X * (CVarGetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0) ? -1 : 1);
            tempZ /= R_COMPASS_SCALE_Y;
            s16 tempXOffset =
                R_COMPASS_OFFSET_X + (CVarGetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0) ? mirrorOffset : 0);
            if (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosType"), 0) != ORIGINAL_LOCATION) {
                if (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosType"), 0) == ANCHOR_LEFT) {
                    if (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.UseMargins"), 0) != 0) {
                        xMarginsMinimap = leftMinimapMargin;
                    }
                    Matrix_Translate(
                        OTRGetDimensionFromLeftEdge((tempXOffset + (xMarginsMinimap * 10) + tempX +
                                                     (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosX"), 0) * 10)) /
                                                    10.0f),
                        (R_COMPASS_OFFSET_Y + ((yMarginsMinimap * 10) * -1) - tempZ +
                         ((CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosY"), 0) * 10) * -1)) /
                            10.0f,
                        0.0f, MTXMODE_NEW);
                } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosType"), 0) == ANCHOR_RIGHT) {
                    if (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.UseMargins"), 0) != 0) {
                        xMarginsMinimap = rightMinimapMargin;
                    }
                    Matrix_Translate(
                        OTRGetDimensionFromRightEdge((tempXOffset + (xMarginsMinimap * 10) + tempX +
                                                      (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosX"), 0) * 10)) /
                                                     10.0f),
                        (R_COMPASS_OFFSET_Y + ((yMarginsMinimap * 10) * -1) - tempZ +
                         ((CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosY"), 0) * 10) * -1)) /
                            10.0f,
                        0.0f, MTXMODE_NEW);
                } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosType"), 0) == ANCHOR_NONE) {
                    Matrix_Translate(
                        (tempXOffset + tempX + (CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosX"), 0) * 10) / 10.0f),
                        (R_COMPASS_OFFSET_Y + ((yMarginsMinimap * 10) * -1) - tempZ +
                         ((CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosY"), 0) * 10) * -1)) /
                            10.0f,
                        0.0f, MTXMODE_NEW);
                }
            } else {
                Matrix_Translate(OTRGetDimensionFromRightEdge((tempXOffset + (xMarginsMinimap * 10) + tempX) / 10.0f),
                                 (R_COMPASS_OFFSET_Y + ((yMarginsMinimap * 10) * -1) - tempZ) / 10.0f, 0.0f,
                                 MTXMODE_NEW);
            }
            Matrix_Scale(icon.scale, icon.scale, icon.scale, MTXMODE_APPLY);
            Matrix_RotateX(-1.6f, MTXMODE_APPLY);
            s16 rotation = ((0x7FFF - icon.rot.y) / 0x400) *
                           (CVarGetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0) ? -1 : 1);
            Matrix_RotateY(rotation / 10.0f, MTXMODE_APPLY);
            gSPMatrix(OVERLAY_DISP++, MATRIX_NEWMTX(gPlayState->state.gfxCtx),
                      G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

            gDPSetPrimColor(OVERLAY_DISP++, 0, 0xFF, icon.color.r, icon.color.g, icon.color.b, 255);
            gSPDisplayList(OVERLAY_DISP++, (Gfx*)gCompassArrowDL);
        }
        CLOSE_DISPS(gPlayState->state.gfxCtx);
    });

    // (VB_PLAYER_DRAW hook removed — replaced by the direct call to
    // HarpoonPropHunt_TryDrawLocalProp at the top of z_player.c
    // Player_Draw. The direct patch fires at every frame draw, returns
    // immediately on prop render, and isn't subject to whatever
    // condition timing made the VB version unreliable in our build.)

    // ----------------------------------------------------------------
    // VB_ACTOR_POST_DRAW — Triforce Thief carrier indicator: draw a
    // floating Triforce above the carrier's head.
    // args: PlayState*, Actor*
    // ----------------------------------------------------------------
    COND_VB_SHOULD(VB_ACTOR_POST_DRAW, isConnected, {
        PlayState* play = va_arg(args, PlayState*);
        Actor* actor    = va_arg(args, Actor*);
        if (play == nullptr || actor == nullptr) return;
        if (!HarpoonTriforceThief::IsInRound()) return;
        const auto& s = HarpoonTriforceThief::GetLocalState();
        if (s.carrierClientId == 0) return;

        // Is this actor the carrier? Local player matches own clientId;
        // remote dummies match via GetDummyPlayerClientId.
        Actor* myPlayer = (Actor*)GET_PLAYER(gPlayState);
        bool isCarrier = false;
        if (actor == myPlayer) {
            isCarrier = (s.carrierClientId == Harpoon::Instance->ownClientId);
        } else {
            uint32_t cid = Harpoon::Instance->GetDummyPlayerClientId(actor);
            isCarrier = (cid != 0 && cid == s.carrierClientId);
        }
        if (isCarrier) {
            HarpoonTriforceThief::DrawTriforceAboveHead(actor, play);
        }
    });

    // ----------------------------------------------------------------
    // OnVanillaBehavior — flip the engine's gameplay-time overlay on
    // while a PropHunt round is live. Mirrors Boss Rush 1:1
    // (BossRush.cpp:872 + BossRush.cpp:933). The renderer is
    // Interface_DrawTotalGameplayTimer (z_parameter.c:6536) which
    // already draws digit-textures at the configured HUD position when
    // both CVAR_GAMEPLAY_STATS("ShowIngameTimer") is on AND the VB hook
    // resolves true. No new renderer needed.
    //
    // `*should = true` (not `|=`) matches Boss Rush's intent — the VB
    // default is false, and we want the timer visible whenever ANY
    // gamemode that opts in says so.
    // ----------------------------------------------------------------
    COND_HOOK(OnVanillaBehavior, isConnected,
              [&](GIVanillaBehavior id, bool* should, va_list args) {
        switch (id) {
        case VB_SHOW_GAMEPLAY_TIMER: {
            // Always-visible timer while in any PropHunt room. Pause is
            // achieved by NOT advancing the underlying counter (only
            // increments while local is Hider; see PropHunt.cpp TickFrame).
            // In lobby / between rounds the timer just freezes at its last
            // value — visually present but stopped. Total survival time
            // across the session.
            if (isPropHuntMode) *should = true;
            break;
        }
        default: break;
        }
    });
}
