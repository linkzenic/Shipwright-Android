#include "Harpoon.h"
#include "Combat/CombatSync.h"
#include "Combat/ProjectileMirror.h"
#include "PropHunt/PropHunt.h"
#include "TriforceThief/TriforceThief.h"
#include "DroppedItems.h"

extern "C" {
// Bridge for the local player's currently-worn MM transformation mask.
// Defined in soh/mods/transformation_masks/mm_mask_wear.cpp. We avoid
// pulling in the full mm_mask_wear.h to dodge the C-header rule.
s32 MmMaskWear_GetCurrent(void);

// Forward-declared struct for gCustomItemState. We only need direct
// access to a handful of fields used in PvP proximity checks. The
// full struct is defined in soh/mods/items/custom_items.h. We declare
// a parallel partial layout below as a C extern struct so MSVC C++
// accepts the symbol; the runtime memory layout is identical because
// the field order matches custom_items.h exactly through the last
// field we touch. (If anyone reorders earlier fields in CustomItemState,
// this layout will go stale — keep them in sync.)
}

// Pull in custom_items.h for the gCustomItemState symbol. The header is
// C++-safe (it self-wraps in extern "C" + #ifdef __cplusplus). Lives in
// soh/mods/ which the project includes as a search root.
#include "mods/items/custom_items.h"

extern "C" {
// SW97 actor IDs, runtime-assigned by sw97_init.cpp's ActorDB::AddEntry.
// Forward-declared so we can detect SW97 spell/arrow spawns in OnActorInit
// without dragging the sw97 expansion headers in.
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

namespace {
HarpoonCombat::HarpoonWeaponId Sw97ActorIdToWeapon(s16 actorId) {
    using namespace HarpoonCombat;
    if (actorId == gSw97ActorId_MagicFire)  return W_SW97_MAGIC_FIRE;
    if (actorId == gSw97ActorId_MagicIce)   return W_SW97_MAGIC_ICE;
    if (actorId == gSw97ActorId_MagicLight) return W_SW97_MAGIC_LIGHT;
    if (actorId == gSw97ActorId_MagicDark)  return W_SW97_MAGIC_DARK;
    if (actorId == gSw97ActorId_MagicSoul)  return W_SW97_MAGIC_SOUL;
    if (actorId == gSw97ActorId_MagicWind)  return W_SW97_MAGIC_WIND;
    if (actorId == gSw97ActorId_ArrowFire)  return W_SW97_ARROW_FIRE;
    if (actorId == gSw97ActorId_ArrowIce)   return W_SW97_ARROW_ICE;
    if (actorId == gSw97ActorId_ArrowLight) return W_SW97_ARROW_LIGHT;
    if (actorId == gSw97ActorId_ArrowDark)  return W_SW97_ARROW_DARK;
    if (actorId == gSw97ActorId_ArrowSoul)  return W_SW97_ARROW_SOUL;
    if (actorId == gSw97ActorId_ArrowWind)  return W_SW97_ARROW_WIND;
    return HARPOON_WEAPON_UNKNOWN;
}
}  // anon
#include <libultraship/libultraship.h>
#include <imgui.h>
#include <cmath>
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

// 1-frame position rollback state for the loading-zone blocker.
// Snapshot Link's pos each "good" frame; on a "bad" frame (engine just set
// PLAYER_STATE1_LOADING from an exit poly), restore to this snapshot. Tiny
// rollback (~1 game tick) — distinct from the old "snap to round-start"
// approach that caused softlocks when the start pos was inside geometry.
static Vec3f sHarpoonLastSafePlayerPos = { 0.0f, 0.0f, 0.0f };
static bool  sHarpoonHasLastSafePos    = false;

// One-shot guard for the auto-jump recoil when engine forces a load. Set
// the first frame of a forced-load encounter, cleared when forced state
// ends. Prevents the jump animation from restarting every frame.
static bool sHarpoonAutoJumpArmed = false;

// File-scope arrays of actor IDs to kill in the round-active loading-zone
// blocker. Kept here (NOT inside the COND_HOOK lambda body) because the
// `COND_HOOK` macro is preprocessor-expanded and commas inside `{ ... }`
// brace-initializers are NOT shielded — putting these inside the lambda
// breaks the macro arg count. See memory `feedback_cond_hook_brace_init`.
// Door actors live in 3 different `ActorContext` categories, NOT all in
// `ACTORCAT_DOOR` as you'd expect:
//   ACTORCAT_ITEMACTION → Door_Ana (grottos), Door_Gerudo, Door_Warp1
//   ACTORCAT_DOOR       → En_Door (regular doors), Door_Shutter, En_Holl
//   ACTORCAT_BG         → Door_Killer (Ganon's Castle trial doors),
//                          Door_Toki (Door of Time)
//
// The previous kill loop iterated only ACTORCAT_DOOR with the full list,
// silently missing Door_Killer / Door_Toki / Door_Gerudo / Door_Warp1.
// Ganon's Castle trial doors (Door_Killer) are DynaPolyActors — their
// collision was blocking the player with an invisible wall even after
// EN_HOLL was kept alive. Fix: 3 separate kill lists, one per category.
static const s16 kHarpoonTtItemActionKill[] = {
    ACTOR_DOOR_ANA,
    ACTOR_DOOR_GERUDO,
    ACTOR_DOOR_WARP1,
    // Ganon's Castle magic barriers (the rainbow glow at each trial
    // entrance). They self-kill on Init when the corresponding
    // trial-completed flag is set, BUT Actor_Kill leaves their OC1
    // collider (`OC1_ON | OC1_TYPE_ALL`) active — it acts as an
    // invisible wall against the player. Forcing Actor_Delete via this
    // list triggers Collider_DestroyCylinder properly.
    ACTOR_DEMO_KEKKAI,
};
// TT door kill list — includes EN_HOLL so the invisible room-load planes
// don't drag the player into the next room when crossing a doorway.
static const s16 kHarpoonTtDoorKill[] = {
    ACTOR_EN_DOOR,
    ACTOR_DOOR_SHUTTER,
    ACTOR_EN_HOLL,
};
// ACTORCAT_BG kills — Door_Killer (trial doors in Ganon's Castle) and
// Door_Toki (Door of Time). Both are DynaPolyActors with collision; the
// kill triggers their Destroy callback which calls DynaPoly_DeleteBgActor
// and removes the wall.
static const s16 kHarpoonTtBgKill[] = {
    ACTOR_DOOR_KILLER,
    ACTOR_DOOR_TOKI,
};
// PropHunt only kills the scene-jumping ITEMACTION doors so peers can
// roam intra-cluster rooms via En_Door / En_Holl normally.
static const s16 kHarpoonPhItemActionKill[] = {
    ACTOR_DOOR_ANA,
    ACTOR_DOOR_WARP1,
};

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
        // CRITICAL leak fix: drop the VFX actor → owner map every scene
        // change. The engine recycles its actor pool on scene load so
        // every Actor* in the map is now dangling, AND the map was never
        // pruned otherwise — it grew unbounded across a session. Long
        // sessions saw stale Actor* collisions (engine reuses the slot)
        // route damage to the wrong shooter and eventually exhaust the
        // damage-routing logic.
        ClearVfxActorOwners();
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

        // ====================================================================
        // Cross-gamemode PvP combat: per-frame form-attack + utility-item
        // proximity checks. The existing collision-based damage broadcast
        // handles weapons with native AT colliders (vanilla swords, bombs,
        // SW97 spells with their own collider cylinder). This block covers
        // attacks WITHOUT a native collider — Goron roll contact, Zora
        // electric, Deku spin AOE, Pegasus charge, and utility-item
        // hostile interactions (hookshot pull with iron-boots inversion,
        // switch hook swap, gust jar blow, fairy heal touch, lantern reveal).
        // ====================================================================
        if (pvpEnabled && gPlayState != nullptr) {
            Player* lp = GET_PLAYER(gPlayState);
            if (lp != nullptr) {
                auto myIt = clients.find(ownClientId);
                HarpoonClient* myClient = (myIt != clients.end()) ? &myIt->second : nullptr;

                // Helper: iterate connected peers, calling cb(cid, peerClient).
                auto forEachPeer = [&](auto cb) {
                    for (auto& [cid, c] : clients) {
                        if (cid == ownClientId) continue;
                        if (!c.online) continue;
                        // Same scene only
                        if (c.sceneNum != (s16)gPlayState->sceneNum) continue;
                        cb(cid, c);
                    }
                };
                auto distSqToPeer = [&](const HarpoonClient& c) -> f32 {
                    f32 dx = lp->actor.world.pos.x - c.posRot.pos.x;
                    f32 dy = lp->actor.world.pos.y - c.posRot.pos.y;
                    f32 dz = lp->actor.world.pos.z - c.posRot.pos.z;
                    return dx * dx + dy * dy + dz * dz;
                };

                // --- Form attacks ---------------------------------------
                // Read transformation from the LOCAL mask system, not the
                // network-mirror field (which is only populated for REMOTE
                // peers). Same for meleeWeaponState (read off lp directly).
                {
                    s32 mask = MmMaskWear_GetCurrent();
                    bool isGoron = (mask == ITEM_MM_MASK_GORON);
                    bool isZora  = (mask == ITEM_MM_MASK_ZORA);
                    bool isDeku  = (mask == ITEM_MM_MASK_DEKU);
                    // Goron roll contact: detect via mask + high linear vel.
                    if (isGoron && lp->linearVelocity > 8.0f) {
                        forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                            if (distSqToPeer(c) <= 80.0f * 80.0f) {
                                Harpoon::Instance->SendPacket_Damage(
                                    cid, HARPOON_HIT_RESPONSE_NORMAL, 2);
                            }
                        });
                    }
                    // Zora electric — Zora form + attacking.
                    if (isZora && lp->meleeWeaponState > 0) {
                        forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                            if (distSqToPeer(c) <= 50.0f * 50.0f) {
                                Harpoon::Instance->SendPacket_Damage(
                                    cid, PLAYER_HIT_RESPONSE_ELECTRIC_SHOCK, 2);
                            }
                        });
                    }
                    // Deku spin/bubble — Deku form + attacking.
                    if (isDeku && lp->meleeWeaponState > 0) {
                        forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                            if (distSqToPeer(c) <= 40.0f * 40.0f) {
                                Harpoon::Instance->SendPacket_Damage(
                                    cid, HARPOON_HIT_RESPONSE_NORMAL, 1);
                            }
                        });
                    }
                }

                // --- Zora Barrier (Water Dragon Scale activable) -------
                // While the local player has the barrier active, any peer
                // within 60u takes 2♥/sec shock damage + 30fr stun. We
                // throttle to once-per-20-frames-per-peer (the existing
                // freezeTimer fires for the stun).
                if (myClient != nullptr && myClient->combatZoraBarrierActive) {
                    static int sZoraBarrierTickCounter = 0;
                    sZoraBarrierTickCounter++;
                    if ((sZoraBarrierTickCounter % 20) == 0) {
                        forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                            if (distSqToPeer(c) <= 60.0f * 60.0f) {
                                HarpoonCombat::BroadcastUtilityHit(
                                    ownClientId, cid,
                                    HarpoonCombat::UTIL_ZORA_BARRIER_SHOCK,
                                    0, 0, 0,
                                    lp->actor.world.pos.x,
                                    lp->actor.world.pos.y,
                                    lp->actor.world.pos.z);
                            }
                        });
                        // Drain magic: 1 unit per second (20 frames)
                        if (gSaveContext.magic > 0) gSaveContext.magic--;
                        if (gSaveContext.magic <= 0) {
                            myClient->combatZoraBarrierActive = 0;
                        }
                    }
                }

                // --- Hylia's Grace fairy heal-touch --------------------
                // While fairy form active, contacted peers heal 1♥.
                // Throttled to once per 30 frames so we don't spam-heal.
                if (gCustomItemState.hyliasGraceActive) {
                    static int sFairyHealTick = 0;
                    sFairyHealTick++;
                    if ((sFairyHealTick % 30) == 0) {
                        forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                            if (distSqToPeer(c) <= 30.0f * 30.0f) {
                                HarpoonCombat::BroadcastUtilityHit(
                                    ownClientId, cid,
                                    HarpoonCombat::UTIL_FAIRY_HEAL_TOUCH,
                                    0, 0, 0, 0, 0, 0);
                            }
                        });
                    }
                }

                // --- Lantern PvP touch (3 fire colors) -----------------
                // Local has Lantern swinging (lantern AT collider sweep
                // is already armed by item_lantern.c). For PvP we add
                // proximity-based effects: regular=1♥, blue=freeze 60fr,
                // poe=stun 90fr. Throttled to once per 15 frames.
                if (gCustomItemState.lanternFireType != 0) {
                    static int sLanternTickGate = 0;
                    sLanternTickGate++;
                    if ((sLanternTickGate % 15) == 0) {
                        forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                            if (distSqToPeer(c) > 35.0f * 35.0f) return;
                            switch (gCustomItemState.lanternFireType) {
                                case 1:  // regular fire
                                    Harpoon::Instance->SendPacket_Damage(
                                        cid, HARPOON_HIT_RESPONSE_FIRE, 1);
                                    HarpoonCombat::BroadcastApplyStatus(
                                        cid, HarpoonCombat::STATUS_BURN_DOT,
                                        1, 60, ownClientId);
                                    break;
                                case 2:  // blue fire
                                    HarpoonCombat::BroadcastApplyStatus(
                                        cid, HarpoonCombat::STATUS_FREEZE,
                                        0, 60, ownClientId);
                                    break;
                                case 3:  // poe fire
                                    HarpoonCombat::BroadcastApplyStatus(
                                        cid, HarpoonCombat::STATUS_STUN,
                                        0, 90, ownClientId);
                                    break;
                            }
                            HarpoonCombat::BroadcastUtilityHit(
                                ownClientId, cid,
                                HarpoonCombat::UTIL_LANTERN_REVEAL,
                                0, 0, 0,
                                lp->actor.world.pos.x,
                                lp->actor.world.pos.y,
                                lp->actor.world.pos.z);
                        });
                    }
                }

                // --- Gust Jar BLOW pushes peers in front cone ----------
                // Detection: ciGustJarMode == HARPOON_GUST_MODE_BLOW.
                // Cone: 45° in front of player, range 80u. Throttled.
                if (gCustomItemState.gustJarMode == 2 /*BLOW*/) {
                    static int sGustTickGate = 0;
                    sGustTickGate++;
                    if ((sGustTickGate % 10) == 0) {
                        s16 yaw = lp->actor.shape.rot.y;
                        f32 fwdX = Math_SinS(yaw);
                        f32 fwdZ = Math_CosS(yaw);
                        forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                            f32 dx = c.posRot.pos.x - lp->actor.world.pos.x;
                            f32 dz = c.posRot.pos.z - lp->actor.world.pos.z;
                            f32 d2 = dx * dx + dz * dz;
                            if (d2 > 80.0f * 80.0f) return;
                            f32 d = sqrtf(d2);
                            if (d < 1.0f) return;
                            // In-cone check via dot product (cos(45°) ≈ 0.707)
                            f32 dot = (dx * fwdX + dz * fwdZ) / d;
                            if (dot < 0.707f) return;
                            HarpoonCombat::BroadcastUtilityHit(
                                ownClientId, cid,
                                HarpoonCombat::UTIL_GUST_BLOW,
                                0, 0, 0,
                                fwdX * 25.0f, 8.0f, fwdZ * 25.0f);
                        });
                    }
                }

                // --- Switch Hook position swap on hit ------------------
                // ciSwitchHookState bit 2 indicates "extended hook is
                // currently attached to something". If attachedActor is
                // a peer dummy → swap positions (we move to where they
                // were, they teleport to where we were).
                if (gCustomItemState.switchHookState != 0) {
                    static u8 sSwitchHookPrevState = 0;
                    if (gCustomItemState.switchHookState != sSwitchHookPrevState &&
                        gCustomItemState.switchHookState >= 2) {
                        // Find closest peer to the projectile position.
                        Vec3f& projPos = gCustomItemState.switchHookProjPos;
                        f32 best = 80.0f * 80.0f;
                        uint32_t bestCid = 0;
                        for (auto& [cid, c] : clients) {
                            if (cid == ownClientId || !c.online) continue;
                            if (c.sceneNum != (s16)gPlayState->sceneNum) continue;
                            f32 dx = projPos.x - c.posRot.pos.x;
                            f32 dy = projPos.y - c.posRot.pos.y;
                            f32 dz = projPos.z - c.posRot.pos.z;
                            f32 d2 = dx*dx + dy*dy + dz*dz;
                            if (d2 <= best) { best = d2; bestCid = cid; }
                        }
                        if (bestCid != 0) {
                            // Swap: send the peer to our pos, move us to
                            // theirs. Both broadcast via UTIL_SWITCH_HOOK_SWAP.
                            HarpoonCombat::BroadcastUtilityHit(
                                ownClientId, bestCid,
                                HarpoonCombat::UTIL_SWITCH_HOOK_SWAP,
                                0, 0, 0,
                                lp->actor.world.pos.x,
                                lp->actor.world.pos.y,
                                lp->actor.world.pos.z);
                            // Teleport local to peer's last known pos.
                            auto pit = clients.find(bestCid);
                            if (pit != clients.end()) {
                                lp->actor.world.pos = pit->second.posRot.pos;
                            }
                        }
                    }
                    sSwitchHookPrevState = gCustomItemState.switchHookState;
                }

                // --- Hookshot iron-boots inversion --------------------
                // If local Link wears Iron Boots AND hookshot grabs a peer,
                // the hook pulls LOCAL to peer instead of peer to local.
                // We detect via Player.actor.parent (when hookshot attaches
                // to the dummy). This is heuristic: when stateFlags1 has
                // PLAYER_STATE1_HOOKSHOT_FLYING set AND local has iron boots
                // (currentBoots == PLAYER_BOOTS_IRON) AND hookActor->parent
                // is a peer dummy, we'd flip — but the actual physics is
                // already handled by vanilla engine since we're the one
                // hookshot-flying. So this is effectively automatic.
                // Just broadcast a notification event so peers know to
                // play a confused-tug animation on their dummy.
                if (myClient != nullptr &&
                    (lp->stateFlags1 & PLAYER_STATE1_HOOKSHOT_FALLING)) {
                    static bool sHookBroadcasted = false;
                    if (!sHookBroadcasted) {
                        sHookBroadcasted = true;
                        bool ironBoots = (lp->currentBoots == PLAYER_BOOTS_IRON);
                        // Find the closest peer in the hookshot's reach.
                        // No iron boots: pull THEM toward us (UTIL_HOOKSHOT_PULL_TARGET).
                        // Iron boots:    pull US toward them (UTIL_HOOKSHOT_PULL_SELF —
                        //                vanilla physics quirk where Link is too heavy
                        //                to drag the target so the rope snaps Link).
                        forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                            if (distSqToPeer(c) > 600.0f * 600.0f) return;
                            HarpoonCombat::BroadcastUtilityHit(
                                ownClientId, cid,
                                ironBoots ? HarpoonCombat::UTIL_HOOKSHOT_PULL_SELF
                                          : HarpoonCombat::UTIL_HOOKSHOT_PULL_TARGET,
                                0, 0, 0,
                                lp->actor.world.pos.x,
                                lp->actor.world.pos.y,
                                lp->actor.world.pos.z);
                        });
                    }
                    if (!(lp->stateFlags1 & PLAYER_STATE1_HOOKSHOT_FALLING)) sHookBroadcasted = false;
                }

                // --- Bomb Arrow direct + AOE --------------------------
                // Detection: ciBombArrowActive && local fires arrow. The
                // arrow itself is a vanilla EN_ARROW so its hit broadcasts
                // via vanilla path; we add the AOE on detonation tick.
                {
                    // Detect FLYING -> not-FLYING transition (impact).
                    // BOMBARROW_STATE_FLYING == 2. When state leaves 2,
                    // the arrow has hit something and exploded.
                    static u8 sBAStatePrev = 0;
                    u8 baState = gCustomItemState.bombArrowState;
                    if (sBAStatePrev == 2 && baState != 2) {
                        forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                            if (distSqToPeer(c) > 90.0f * 90.0f) return;
                            Harpoon::Instance->SendPacket_Damage(
                                cid, PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE, 2);
                        });
                    }
                    sBAStatePrev = baState;
                }

                // --- Water Dragon Zora Barrier toggle (C-Up tap) -------
                // Adult Link with Water Dragon Scale equipped: tapping C-Up
                // toggles the Zora Barrier (60u radius shock aura). Tracks
                // rising-edge to avoid spam. Costs 1 magic/sec while active.
                if (myClient != nullptr && gPlayState != nullptr) {
                    static bool sCUpWasDown = false;
                    bool cUp = CHECK_BTN_ALL(gPlayState->state.input[0].cur.button, BTN_CUP);
                    bool risingEdge = cUp && !sCUpWasDown;
                    sCUpWasDown = cUp;
                    // Only toggle if Adult + Water Dragon Scale equipped
                    // (extEquipBoots == 3 means Water Dragon Scale).
                    if (risingEdge && gSaveContext.linkAge == 0 &&
                        gSaveContext.ship.extEquipBoots == 3) {
                        myClient->combatZoraBarrierActive = !myClient->combatZoraBarrierActive;
                    }
                }

                // --- Stone Mask invisibility cancel on attack ---------
                // Local player attacking while wearing Stone Mask cancels
                // invisibility for 3 s. Detection: meleeWeaponState > 0 or
                // damageEffect set on a peer.
                if (myClient != nullptr &&
                    MmMaskWear_GetCurrent() == ITEM_MM_MASK_STONE &&
                    myClient->combatInvisSuppressFrames == 0 &&
                    lp->meleeWeaponState > 0) {
                    myClient->combatInvisSuppressFrames = 180;  // 3 s @ 60fps
                    HarpoonCombat::BroadcastApplyStatus(
                        ownClientId, HarpoonCombat::STATUS_INVISIBILITY,
                        0, 180, ownClientId);
                }
                if (myClient != nullptr && myClient->combatInvisSuppressFrames > 0) {
                    myClient->combatInvisSuppressFrames--;
                }

                // --- Ice Rod projectiles freeze peers ------------------
                // The Ice Rod fires up to 3 spinning projectiles tracked
                // in HarpoonClient.ciIceRodProj*. While active, any peer
                // within 25u of any projectile gets STATUS_FREEZE 3s.
                // Light damage tagged via SendPacket_Damage so the heart-
                // bar shows the hit; primary effect is the freeze.
                if (gCustomItemState.iceRodProjActive) {
                    static int sIceRodGate = 0;
                    sIceRodGate++;
                    if ((sIceRodGate % 8) == 0) {
                        Vec3f projs[3];
                        projs[0] = gCustomItemState.iceRodProjPos;
                        projs[1] = gCustomItemState.iceRodProjPos2;
                        projs[2] = gCustomItemState.iceRodProjPos3;
                        u8 count = gCustomItemState.iceRodProjCount;
                        if (count > 3) count = 3;
                        forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                            for (u8 i = 0; i < count; i++) {
                                f32 dx = projs[i].x - c.posRot.pos.x;
                                f32 dy = projs[i].y - c.posRot.pos.y;
                                f32 dz = projs[i].z - c.posRot.pos.z;
                                if (dx*dx + dy*dy + dz*dz <= 25.0f * 25.0f) {
                                    HarpoonCombat::BroadcastApplyStatus(
                                        cid, HarpoonCombat::STATUS_FREEZE,
                                        0, 180, ownClientId);
                                    return;
                                }
                            }
                        });
                    }
                }

                // --- Fire Rod projectiles burn peers -------------------
                if (gCustomItemState.fireRodProjActive) {
                    static int sFireRodGate = 0;
                    sFireRodGate++;
                    if ((sFireRodGate % 8) == 0) {
                        Vec3f projs[3];
                        projs[0] = gCustomItemState.fireRodProjPos;
                        projs[1] = gCustomItemState.fireRodProjPos2;
                        projs[2] = gCustomItemState.fireRodProjPos3;
                        u8 count = gCustomItemState.fireRodProjCount;
                        if (count > 3) count = 3;
                        forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                            for (u8 i = 0; i < count; i++) {
                                f32 dx = projs[i].x - c.posRot.pos.x;
                                f32 dy = projs[i].y - c.posRot.pos.y;
                                f32 dz = projs[i].z - c.posRot.pos.z;
                                if (dx*dx + dy*dy + dz*dz <= 25.0f * 25.0f) {
                                    Harpoon::Instance->SendPacket_Damage(
                                        cid, HARPOON_HIT_RESPONSE_FIRE, 2);
                                    HarpoonCombat::BroadcastApplyStatus(
                                        cid, HarpoonCombat::STATUS_BURN_DOT,
                                        1, 120, ownClientId);
                                    return;
                                }
                            }
                        });
                    }
                }

                // --- Light Rod projectiles deal LIGHT damage -----------
                // Light Rod uses the dedicated HARPOON_HIT_RESPONSE_LIGHT:
                // golden flash, medium kb, longer invuln (35fr).
                if (gCustomItemState.lightRodProjActive) {
                    static int sLightRodGate = 0;
                    sLightRodGate++;
                    if ((sLightRodGate % 8) == 0) {
                        Vec3f projs[3];
                        projs[0] = gCustomItemState.lightRodProjPos;
                        projs[1] = gCustomItemState.lightRodProjPos2;
                        projs[2] = gCustomItemState.lightRodProjPos3;
                        u8 count = gCustomItemState.lightRodProjCount;
                        if (count > 3) count = 3;
                        forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                            for (u8 i = 0; i < count; i++) {
                                f32 dx = projs[i].x - c.posRot.pos.x;
                                f32 dy = projs[i].y - c.posRot.pos.y;
                                f32 dz = projs[i].z - c.posRot.pos.z;
                                if (dx*dx + dy*dy + dz*dz <= 25.0f * 25.0f) {
                                    Harpoon::Instance->SendPacket_Damage(
                                        cid, HARPOON_HIT_RESPONSE_LIGHT, 2);
                                    return;
                                }
                            }
                        });
                    }
                }

                // --- Ball & Chain swing contact ------------------------
                // ciBallChainThrown == 1 while the sphere is mid-air at
                // the end of the chain. Range 80u, dmg 6♥ + KNOCKBACK_LARGE.
                if (gCustomItemState.ballAndChainThrown == 1) {
                    static int sBCGate = 0;
                    sBCGate++;
                    if ((sBCGate % 6) == 0) {
                        forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                            f32 dx = gCustomItemState.sharedProjectilePos.x - c.posRot.pos.x;
                            f32 dz = gCustomItemState.sharedProjectilePos.z - c.posRot.pos.z;
                            if (dx*dx + dz*dz > 25.0f * 25.0f) return;
                            Harpoon::Instance->SendPacket_Damage(
                                cid, PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE, 4);
                        });
                    }
                }

                // --- Beetle hit on return path ------------------------
                // ciBeetleState == 2 (return). Hits dummy peers in flight.
                if (gCustomItemState.beetleState != 0) {
                    static int sBeetleGate = 0;
                    sBeetleGate++;
                    if ((sBeetleGate % 4) == 0) {
                        forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                            f32 dx = gCustomItemState.beetlePos.x - c.posRot.pos.x;
                            f32 dy = gCustomItemState.beetlePos.y - c.posRot.pos.y;
                            f32 dz = gCustomItemState.beetlePos.z - c.posRot.pos.z;
                            if (dx*dx + dy*dy + dz*dz > 25.0f * 25.0f) return;
                            Harpoon::Instance->SendPacket_Damage(
                                cid, HARPOON_HIT_RESPONSE_STUN, 1);
                        });
                    }
                }

                // --- Whip lash hit -------------------------------------
                // ciWhipState == 1 (extending). The whip tip moves; on
                // crossing a peer, deal 2♥ + small knockback.
                if (gCustomItemState.whipState == 1) {
                    forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                        f32 dx = gCustomItemState.whipTipPos.x - c.posRot.pos.x;
                        f32 dz = gCustomItemState.whipTipPos.z - c.posRot.pos.z;
                        if (dx*dx + dz*dz > 30.0f * 30.0f) return;
                        Harpoon::Instance->SendPacket_Damage(
                            cid, HARPOON_HIT_RESPONSE_NORMAL, 1);
                    });
                }

                // --- Demise Destruction AOE on activation -------------
                // ciDemiseDestructionActive rising edge fires a 200u AOE
                // 2♥ blast + center 6♥ direct hit. The status broadcast
                // sends DRAIN to communicate the dark-magic theme.
                {
                    static u8 sDemisePrev = 0;
                    if (gCustomItemState.demiseDestructionActive == 1 && sDemisePrev == 0) {
                        forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                            f32 d2 = distSqToPeer(c);
                            if (d2 > 200.0f * 200.0f) return;
                            u8 dmg = (d2 <= 40.0f * 40.0f) ? 4 : 2;
                            Harpoon::Instance->SendPacket_Damage(
                                cid, PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE, dmg);
                        });
                    }
                    sDemisePrev = gCustomItemState.demiseDestructionActive;
                }

                // --- Zonai Permafrost AOE freeze ----------------------
                // ciZonaiPermafrostActive rising edge → 150u AOE freeze 5s.
                {
                    static u8 sPermafrostPrev = 0;
                    if (gCustomItemState.zonaiPermafrostActive == 1 && sPermafrostPrev == 0) {
                        forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                            if (distSqToPeer(c) > 150.0f * 150.0f) return;
                            HarpoonCombat::BroadcastApplyStatus(
                                cid, HarpoonCombat::STATUS_FREEZE,
                                0, 300, ownClientId);
                        });
                    }
                    sPermafrostPrev = gCustomItemState.zonaiPermafrostActive;
                }

                // --- Spinner ride pushes peers ------------------------
                // If local is on the Spinner (we approximate via the
                // dominion rod state field used by spinner) and moving,
                // peers in contact take 0 dmg but get a knockback.
                // Detection heuristic: linearVelocity > 12 + currentBoots
                // gives us "high speed" — sub-cases of Pegasus already
                // covered above. Spinner-specific is handled by the
                // SHARED_PROJ broadcast (homing top) elsewhere.

                // --- Cane of Byrna 1♥ aura (while held) ----------------
                // Hard to detect without a dedicated client field. Best-
                // effort: detect via extEquipSword == 1 (Cane of Byrna)
                // + meleeWeaponState > 0. Constant 1♥ aura while attacking.
                if (gSaveContext.ship.extEquipSword == 1 &&
                    lp->meleeWeaponState > 0) {
                    forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                        if (distSqToPeer(c) > 40.0f * 40.0f) return;
                        Harpoon::Instance->SendPacket_Damage(
                            cid, HARPOON_HIT_RESPONSE_NORMAL, 1);
                    });
                }

                // --- Four Sword clone proximity (extEquipSword == 2) ---
                if (gSaveContext.ship.extEquipSword == 2 &&
                    lp->meleeWeaponState > 0) {
                    forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                        if (distSqToPeer(c) > 60.0f * 60.0f) return;
                        Harpoon::Instance->SendPacket_Damage(
                            cid, HARPOON_HIT_RESPONSE_NORMAL, 2);
                    });
                }

                // --- Pendant Mortal Draw (extEquipBoots == 2) ----------
                if (gSaveContext.ship.extEquipBoots == 2 &&
                    lp->meleeWeaponState > 0) {
                    static s8 sMortalDrawPrev = 0;
                    if (sMortalDrawPrev == 0 && lp->meleeWeaponState > 5) {
                        forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                            if (distSqToPeer(c) > 35.0f * 35.0f) return;
                            Harpoon::Instance->SendPacket_Damage(
                                cid, PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE, 127);
                        });
                    }
                    sMortalDrawPrev = lp->meleeWeaponState;
                }

                // --- Blast Mask AOE -----------------------------------
                if (MmMaskWear_GetCurrent() == ITEM_MM_MASK_BLAST &&
                    lp->meleeWeaponState > 0) {
                    static int sBlastMaskGate = 0;
                    sBlastMaskGate++;
                    if ((sBlastMaskGate % 30) == 0) {
                        forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                            if (distSqToPeer(c) > 100.0f * 100.0f) return;
                            Harpoon::Instance->SendPacket_Damage(
                                cid, PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE, 4);
                        });
                    }
                }

                // --- Pegasus Anklet charge contact ---------------------
                // If LOCAL player is dashing at high speed (Pegasus dash)
                // AND has speed > threshold AND collides with a peer,
                // broadcast 4♥ + KNOCKBACK_LARGE.
                if (myClient != nullptr) {
                    f32 speed = lp->linearVelocity < 0 ? -lp->linearVelocity : lp->linearVelocity;
                    if (speed >= 16.0f) {  // Pegasus dash speed = 18.0f
                        forEachPeer([&](uint32_t cid, HarpoonClient& c) {
                            if (distSqToPeer(c) <= 40.0f * 40.0f) {
                                Harpoon::Instance->SendPacket_Damage(
                                    cid, PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE, 4);
                            }
                        });
                    }
                }
            }
        }
        // ====================================================================
        // End cross-gamemode PvP combat per-frame hook
        // ====================================================================

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

    // Cross-gamemode PvP combat: when a LOCAL SW97 spell/arrow actor
    // spawns (i.e. we cast a spell or fire an elemental arrow), broadcast
    // a PROJECTILE_SPAWN event so peers can render the mirrored actor.
    // Remote-mirrored spawns (HARPOON_REMOTE_PROJECTILE_BIT in params) are
    // skipped to avoid feedback loops.
    COND_HOOK(OnActorInit, isConnected, [&](void* actorVoid) {
        if (actorVoid == nullptr) return;
        Actor* actor = (Actor*)actorVoid;
        if (HarpoonCombat::IsRemoteProjectile(actor)) return;
        HarpoonCombat::HarpoonWeaponId weapon = Sw97ActorIdToWeapon(actor->id);
        if (weapon == HarpoonCombat::HARPOON_WEAPON_UNKNOWN) return;
        // Only broadcast if we're connected + in a PvP-enabled gamemode.
        if (!pvpEnabled) return;
        HarpoonProjectileMirror::BroadcastSpawn(
            weapon,
            actor->world.pos.x, actor->world.pos.y, actor->world.pos.z,
            actor->velocity.x,  actor->velocity.y,  actor->velocity.z,
            (f32)actor->shape.rot.y * (3.14159f / 32768.0f),
            0);
    });

    // Process incoming packets on game thread
    COND_HOOK(OnGameFrameUpdate, isConnected, [&]() {
        ProcessIncomingPacketQueue();
        UpdateDecoys();
        HarpoonPropHunt::TickFrame();

        // Cross-gamemode PvP combat: decrement burn DOT / freeze /
        // blindness / parry-window timers; ramp shield-raise counter.
        // Active in any gamemode that has pvp_enabled = true (the
        // module no-ops internally when pvpEnabled is false).
        HarpoonCombat::TickLocal();
        // Render the blindness overlay (Dark spell / Dark arrow) on top
        // of the world. Uses ImGui's foreground draw list so it stacks
        // with menus etc. correctly.
        HarpoonCombat::BlindnessEffect_Draw();

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
        // PropHunt uses the server-driven room state machine (countdown /
        // hiding-phase / playing), so gate it on `gameState`. Triforce
        // Thief: kill loading-zone actors UNCONDITIONALLY whenever the room
        // gamemode is TT — lobby, round, between rounds, any state. Per
        // user spec: "si es mode TT se mueren las loading zones, no importa
        // si es Lobby o lo que sea". Earlier gates (`gameState == PLAYING`
        // then `IsInRound()`) both failed: the server resets gameState to
        // LOBBY on peers via HandlePacket_GameState, and IsInRound() is
        // false in the lobby — so loading zones were never disabled there.
        // The actor-kill is harmless in the lobby (Hyrule Field warps just
        // die; the authorized round-start teleport bypasses the blocker via
        // sHarpoonAuthorizedTransition). The scene-exit-poly REDIRECT below
        // is still gated on IsInRound so the lobby only cancels exits
        // (keeps the player in HF) rather than warping them to a stale map.
        bool inRoundActive  = gPlayState != nullptr &&
                              ((isPropHuntMode &&
                                (gameState == HARPOON_STATE_PLAYING ||
                                 gameState == HARPOON_STATE_HIDING_PHASE)) ||
                               (currentRoomGameMode == "triforce_thief"));
        if (inRoundActive) {
            // ----- Mechanism (1): comprehensive door / loading-zone actor kill -----
            //
            // Strategy per user spec: instead of pushing the player away
            // from loading-zone actors, KILL both the actor that performs
            // the scene transition AND the actor that locks Link's
            // actions (forces him into the "walking into door" animation
            // mid-transition). For OoT, these are the same actor — every
            // door / grotto / warp actor handles BOTH its own cutscene
            // animation AND the eventual `transitionTrigger` set. Killing
            // the actor disables both behaviors at once.
            //
            // PropHunt + TT (both modes):
            //   ACTOR_DOOR_ANA   — grotto holes (down-pit transitions)
            //   ACTOR_DOOR_WARP1 — blue/yellow warp pads (boss / dungeon exit)
            //
            // TT ONLY (per user: "bloquea TT todas las puertas"):
            //   ACTOR_EN_DOOR       — regular hinged doors
            //   ACTOR_DOOR_SHUTTER  — dungeon shutter doors
            //   ACTOR_DOOR_KILLER   — wall-mounted locked doors
            //   ACTOR_DOOR_TOKI     — Door of Time (Hyrule Castle interior)
            //   ACTOR_DOOR_GERUDO   — Gerudo guard doors
            //
            // PH stays minimal because hider gameplay uses regular doors
            // legitimately (hide behind a door, look like a prop). TT's
            // gameplay is open-arena chase — no door interaction needed.
            //
            // Polygon-based scene exits (next block) handle the remainder
            // for both modes — overworld floor polys that aren't actors.
            bool isTT = (currentRoomGameMode == "triforce_thief");

            auto killByList = [&](s32 cat, const s16* ids, s32 count) {
                // CRITICAL: must use Actor_Delete (not Actor_Kill) for door
                // actors. Actor_Kill (z_actor.c:1211) only nulls update +
                // draw — it does NOT call the actor's `destroy` callback.
                // For DynaPolyActor doors (Door_Killer, Door_Shutter,
                // Door_Toki, En_Door, etc.) the destroy callback is what
                // calls DynaPoly_DeleteBgActor to remove the wall-shaped
                // dyna collision. With Actor_Kill alone the actor goes
                // invisible + inert but its collision stays as an
                // invisible wall — exactly the Ganon's-Castle symptom.
                //
                // Actor_Delete fully unlinks + destroys + frees, calling
                // destroy() so DynaPoly entries are unregistered the same
                // frame. We save `next` BEFORE Delete because the actor's
                // memory is freed inside the call (reading a->next after
                // would be UB).
                Actor* a = gPlayState->actorCtx.actorLists[cat].head;
                while (a != nullptr) {
                    Actor* next = a->next;
                    for (s32 i = 0; i < count; i++) {
                        if (a->id == ids[i]) {
                            Actor_Delete(&gPlayState->actorCtx, a, gPlayState);
                            break;
                        }
                    }
                    a = next;
                }
            };

            // Door actors live in THREE different actor categories. The
            // previous single-category sweep missed Door_Killer (in BG)
            // and Door_Toki (in BG) — both DynaPolyActors whose collision
            // persisted as "invisible walls" inside dungeons. Sweep each
            // category with its own per-mode kill list.
            if (isTT) {
                killByList(ACTORCAT_ITEMACTION, kHarpoonTtItemActionKill,
                           (s32)(sizeof(kHarpoonTtItemActionKill) /
                                 sizeof(kHarpoonTtItemActionKill[0])));
                killByList(ACTORCAT_DOOR, kHarpoonTtDoorKill,
                           (s32)(sizeof(kHarpoonTtDoorKill) /
                                 sizeof(kHarpoonTtDoorKill[0])));
                killByList(ACTORCAT_BG, kHarpoonTtBgKill,
                           (s32)(sizeof(kHarpoonTtBgKill) /
                                 sizeof(kHarpoonTtBgKill[0])));
            } else {
                // PropHunt: keep normal doors and EnHoll alive (hiders
                // need to traverse rooms within their assigned scene
                // cluster). Only kill scene-jumping warp doors.
                killByList(ACTORCAT_ITEMACTION, kHarpoonPhItemActionKill,
                           (s32)(sizeof(kHarpoonPhItemActionKill) /
                                 sizeof(kHarpoonPhItemActionKill[0])));
            }

            // ----- Mechanism (2) REMOVED -----
            // The previous poly-based snap-back ("wall of wind") teleported
            // the player back to the last safe position whenever an 8-dir
            // raycast hit a scene-exit polygon. Per user spec it caused
            // softlocks (snap target was sometimes inside geometry, or the
            // snap fought the engine's position-update each tick) and is
            // worse than just letting players occasionally fall into the
            // void. So: no proactive poly detection here. The kill block
            // above + the redirect block below are the only mechanisms.
            // If a player falls off the map, void respawn handles it
            // (engine respawn or our own out-of-bounds Triforce respawn).

            // ----- Mechanism (3): layered scene-exit-poly blocker -----
            // Scene-exit polys (collision-data, NOT actors) trigger an
            // engine path in z_player.c:5560-5660 that:
            //   - sets play->transitionTrigger = TRANS_TRIGGER_START
            //   - calls func_80838E70(...) → sets actionFunc to the
            //     scene-exit walk action (input-blocking; targets a
            //     position 400 units forward and drives Link toward it)
            //   - calls func_80835E44(play, CAM_SET_SCENE_TRANSITION) →
            //     camera zooms for the "walk-through-door" framing
            //   - sets PLAYER_STATE1_LOADING | PLAYER_STATE1_IN_CUTSCENE
            //
            // OnGameFrameUpdate runs at the END of GameState_Update
            // (game.c:356) — AFTER Player_Update has already ticked the
            // scene-exit-walk action func once. So we can undo everything
            // for next frame, but we still need to make sure next frame
            // the engine doesn't RE-enter the same setup. The gate at
            // z_player.c:5560 fires whenever Link is on an exit poly with
            // LOADING cleared and trigger == OFF. If we only rollback to
            // "last frame's pos" Link is STILL on the poly (that's where
            // he was last frame too) → infinite re-entry → loss of
            // control + camera stuck zoomed.
            //
            // Counter — small backward push so Link physically leaves
            // the poly + camera revert each forced frame:
            //   1. Snap pos to last safe + push ~40 units along -rot.y
            //      (the direction opposite to where he was walking). Tiny
            //      enough not to softlock, large enough to leave the poly.
            //   2. Revert camera setting to CAM_SET_NORMAL0.
            //   3. Reset actionFunc to Player_Action_Idle.
            //   4. Clear PLAYER_STATE1_LOADING | IN_CUTSCENE.
            //   5. Zero all velocity / speed fields.
            // Non-forced frames: snapshot Link's pos for next-frame rollback.
            {
                Player* lp = GET_PLAYER(gPlayState);
                bool engineForcedLoad =
                    (lp != nullptr) &&
                    !::sHarpoonAuthorizedTransition &&
                    (lp->stateFlags1 & PLAYER_STATE1_LOADING);

                if (engineForcedLoad) {
                    Vec3f basePos = sHarpoonHasLastSafePos
                                        ? sHarpoonLastSafePlayerPos
                                        : lp->actor.world.pos;
                    constexpr f32 kBackPushUnits = 40.0f;
                    f32 yawRad = (f32)lp->actor.world.rot.y *
                                 (3.14159265f / 32768.0f);
                    Vec3f pushed;
                    pushed.x = basePos.x - sinf(yawRad) * kBackPushUnits;
                    pushed.y = basePos.y;
                    pushed.z = basePos.z - cosf(yawRad) * kBackPushUnits;

                    lp->actor.world.pos = pushed;
                    lp->actor.prevPos   = pushed;

                    Camera* cam = Play_GetCamera(gPlayState, 0);
                    if (cam != nullptr) {
                        Camera_ChangeSetting(cam, CAM_SET_NORMAL0);
                    }

                    lp->stateFlags1 &= ~(PLAYER_STATE1_LOADING |
                                         PLAYER_STATE1_IN_CUTSCENE);

                    if (!sHarpoonAutoJumpArmed) {
                        // Small auto-hop on first frame of forced load.
                        // func_80838940 sets actionFunc to the jump
                        // action, applies vertical velocity (4.0f =
                        // small hop), plays jump SFX, sets JUMPING flag.
                        // Negative linearVelocity makes Link drift
                        // backward during the arc — visible "bounced
                        // off" recoil instead of stuck-in-idle. The
                        // jump action naturally lands Link and
                        // transitions back to idle/walking, breaking
                        // any animation lock the engine left behind.
                        func_80838940(lp, NULL, 4.0f, gPlayState,
                                      NA_SE_VO_LI_AUTO_JUMP);
                        lp->linearVelocity   = -2.0f;
                        lp->actor.speedXZ    = -2.0f;
                        sHarpoonAutoJumpArmed = true;
                    }
                    // Don't update the snapshot this frame.
                } else if (lp != nullptr) {
                    sHarpoonLastSafePlayerPos = lp->actor.world.pos;
                    sHarpoonHasLastSafePos    = true;
                    // Re-arm the auto-jump for the next encounter.
                    sHarpoonAutoJumpArmed = false;
                }
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
                } else if (currentRoomGameMode == "triforce_thief" &&
                           HarpoonTriforceThief::IsInRound()) {
                    // Same full-circle redirect for TT: when the engine
                    // wants to load us into a scene outside the round's
                    // cluster, swap the entrance to the round map's main
                    // entrance instead. Player fades through the door and
                    // arrives back in the round map. Gated on IsInRound so
                    // that in the LOBBY (Hyrule Field) we fall through to
                    // the cancel branch — keeping the player in HF instead
                    // of warping them to a stale confirmedMapIndex.
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
        //  * LOBBY + host → open map select (start a new round). Both modes.
        //  * Mid-round (PropHunt ONLY) → reload the current scene from its
        //    entrance: a soft-anti-softlock for stuck-in-wall / fell-in-hole
        //    / jammed-cutscene. The Triforce Thief mid-round reload was
        //    removed per user request — TT now kills loading zones reliably
        //    in every state, so the manual escape hatch is unnecessary.
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
                    } else if (!isLobby && inPropHuntRoom) {
                        // Mid-round → reload the currently SELECTED round
                        // map's entrance (local-only, no broadcast). The
                        // previous version used gSaveContext.entranceIndex
                        // which gets baked to Hyrule Field (205) by
                        // ApplyBaseHealthMagic at round start and never
                        // updated by TeleportToEntrance — so every reload
                        // yanked the player back to the lobby, ending the
                        // round server-side. Use the per-gamemode
                        // GetEntranceForMapIndex(confirmedMapIndex) helper.
                        //
                        // PropHunt ONLY — the TT anti-softlock reload
                        // safeguard was removed per user request: loading
                        // zones are now reliably killed in TT (any state),
                        // so the manual L+R+Z scene-reload escape hatch is
                        // no longer needed and just risked accidental use.
                        if (gPlayState != nullptr) {
                            s32 reloadEntrance = gSaveContext.entranceIndex;
                            s32 mapIdx = (ownClientId != 0)
                                ? confirmedMapIndex : -1;
                            if (mapIdx >= 0) {
                                reloadEntrance =
                                    HarpoonPropHunt::GetEntranceForMapIndex(mapIdx);
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
                // 25 u/frame upward (was 14). With GRAVITY = -1.5 per tick,
                // peak height ≈ v²/(2g) = 625/3 ≈ 208 units ≈ ~3 Link adult
                // heights — matches the "Link's house ladder" reference
                // discussed with Scooter. Also gates regrab naturally:
                // mid-flight pickups are blocked by `dropFlyTimer > 0` so
                // nobody can re-grab until the Triforce comes back down.
                f32 vy = 25.0f;
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
            // Triforce Thief now uses the engine timer1 (the underwater /
            // Death Mountain heat MM:SS HUD) instead of the gameplaystats
            // digit overlay — see TriforceThief.cpp's (a-0) block. So we
            // DON'T force the gameplay timer here; if we did, it would
            // render alongside our MM:SS timer and show the upward-counting
            // play-time stat.
            break;
        }
        default: break;
        }
    });
}
