/**
 * gerudo_form.cpp — Gerudo Form (OOT Gerudo Mask, Garo-style hybrid).
 *
 * See gerudo_form.h for the design overview.
 *
 * What this file owns:
 *   - Mask-edge-detect: polls player->currentMask each frame; on a rising
 *     edge (mask just equipped AND cheat on), calls O2rLoader_ForceModel
 *     ("gerudo"). On a falling edge, clears the forced model.
 *   - GameInteractor VB hooks: VB_GERUDOS_BE_FRIENDLY → true while active,
 *     VB_GIVE_ITEM_GERUDO_MEMBERSHIP_CARD → false (no card autograntee).
 *   - Sandstorm-OFF enforcement in Haunted Wasteland (per-frame + on
 *     transition end).
 *   - Haunted Wasteland "cross the desert" offer: the vanilla lost-warp runs
 *     untouched, but on spawning in the wasteland a skippable Yes/No textbox
 *     offers a direct warp to the opposite side of the desert (skip the maze).
 *   - GerudoForm_GetTunicColor helper used by the hybrid render to recolor
 *     the gerudo outfit with Link's current tunic.
 *
 * The skeleton swap, null-body PostLimbDraw, and gerudo body draw are
 * handled by:
 *   - O2rLoader_SwapSkeleton / O2rLoader_RestoreSkeleton  (o2r_loader.cpp)
 *   - GerudoForm_DrawNullBody                              (gerudo_post_limb.cpp)
 *   - GerudoHybrid_Update / GerudoHybrid_Draw              (gerudo_hybrid_render.cpp)
 *
 * Replaces the older skin-pack approach (alt/-pathed Link skeleton + idle
 * pose override on a sFormSkelAnime). That approach is now retired.
 */

#include "gerudo_form.h"
#include "mods/transformation_masks/transformation_masks.h" // MmPlayerTransformation, MM_PLAYER_FORM_GERUDO
#include "mods/o2r_loader/o2r_loader.h"

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
#include "soh/ResourceManagerHelpers.h"

#include <libultraship/bridge.h>
#include <spdlog/spdlog.h>
#include <cmath>
#include <cstring>

extern "C" {
#include "macros.h"        // GET_PLAYER, LINK_IS_ADULT
#include "variables.h"     // gSaveContext (used by LINK_IS_ADULT)
#include "functions.h"     // LinkAnimation_*, Animation_GetLastFrame, Math_SinS, Collider_*
#include "z64animation.h"
// ResourceMgr_LoadGfxByName / LoadAnimByName are already declared by
// soh/ResourceManagerHelpers.h (included above). Don't redeclare them here —
// a mismatched signature breaks the whole TU.
}

// Ground touch bit of Actor::bgCheckFlags. Not defined in any public header
// (OOT decomp just hardcodes `& 1`); we name it locally for readability.
#define BGCHECKFLAG_GROUND 0x0001

extern "C" PlayState* gPlayState;
extern "C" Color_RGB8 sTunicColors[];

#define CVAR_GERUDO_TRANSFORM "gMods.GerudoMaskTransform"

namespace {

bool IsWearingGerudoMask() {
    if (gPlayState == nullptr) {
        return false;
    }
    Player* player = GET_PLAYER(gPlayState);
    return player != nullptr && player->currentMask == PLAYER_MASK_GERUDO;
}

// Edge-detection state for the mask toggle. Rising edge → ForceModel,
// falling edge → ClearForcedModel. Polled in OnPlayerUpdate.
bool sPrevWantGerudo = false;

// --- Haunted Wasteland "cross the desert" offer ----------------------------
// Gerudo desert skill. Rather than fighting the vanilla lost-warp (which loops
// you back to the entrance), we let it run completely — it works and never
// softlocks. THEN, once you've spawned in the wasteland (whether you entered
// legitimately or got looped back), we present a skippable Yes/No textbox
// offering to warp straight across to the OTHER side of the desert, skipping
// the maze. Pick "No" (or press B) to walk it yourself.
//
// Detecting "you're back at the entrance" is just "a transition into the
// wasteland finished" (OnTransitionEnd) — getting lost respawns you via a full
// transition, so the offer re-appears each time you end up back here.
constexpr uint16_t kWastelandWarpTextId = 0x9FB0; // free slot above spiritual_stones' 0x9FA0-0x9FA2
bool sWastelandOfferPending = false;  // entered the wasteland; show the offer once
bool sWastelandOfferOpen = false;     // offer textbox currently on screen
bool sWastelandOfferToFortress = false; // message/dest target: true = Fortress, false = Colossus
s16  sWastelandOfferDest = -1;        // entrance to warp to if accepted
bool sWastelandWarpChosen = false;    // our own warp transition is running

void GerudoForm_ResetWastelandOffer() {
    sWastelandOfferPending = false;
    sWastelandOfferOpen = false;
    sWastelandOfferDest = -1;
    sWastelandWarpChosen = false;
}

void GerudoForm_TickWastelandWarp(PlayState* play) {
    Player* player = GET_PLAYER(play);

    // 1. Offer textbox is up → watch for B (skip) or the choice closing.
    if (sWastelandOfferOpen) {
        Input* input = &play->state.input[0];
        bool skip = (input != nullptr) && CHECK_BTN_ALL(input->press.button, BTN_B);

        if (skip) {
            Message_CloseTextbox(play);
        }
        if (skip || play->msgCtx.msgMode == MSGMODE_NONE) {
            bool warp = !skip && (play->msgCtx.choiceIndex == 0); // choice 0 == "Yes"
            sWastelandOfferOpen = false;
            if (player != nullptr) {
                player->stateFlags1 &= ~PLAYER_STATE1_IN_CUTSCENE;
            }
            if (warp && sWastelandOfferDest >= 0) {
                sWastelandWarpChosen = true;
                play->nextEntranceIndex = sWastelandOfferDest;
                play->transitionType = TRANS_TYPE_FADE_BLACK_FAST;
                play->transitionTrigger = TRANS_TRIGGER_START;
            }
        }
        return;
    }

    // 2. Open the offer once we're in normal control (post-load, no textbox,
    //    no transition, player not already frozen).
    if (sWastelandOfferPending && !sWastelandWarpChosen &&
        play->msgCtx.msgMode == MSGMODE_NONE &&
        play->transitionTrigger == TRANS_TRIGGER_OFF && play->transitionMode == TRANS_MODE_OFF &&
        player != nullptr &&
        !(player->stateFlags1 & (PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_LOADING))) {
        sWastelandOfferPending = false;
        sWastelandOfferOpen = true;
        player->stateFlags1 |= PLAYER_STATE1_IN_CUTSCENE; // freeze while choosing
        Message_StartTextbox(play, kWastelandWarpTextId, nullptr);
    }
}

// Defined in mm_player_form.cpp.
extern "C" MmPlayerTransformation MmForm_GetCurrentForm(void);

void GerudoForm_OnPlayerUpdate() {
    if (gPlayState == nullptr) {
        return;
    }

    // ForceModel("gerudo") / ClearForcedModel are driven by the MM form
    // pipeline now (MmForm_LoadFormSkeleton on flash peak, MmForm_RestoreOotState
    // on detransform). Polling here is only a safety net — if the user pulls
    // the mask off via UI (not via re-press), make sure the skin clears.
    bool cheatOn = CVarGetInteger(CVAR_GERUDO_TRANSFORM, 0) != 0;
    bool want = cheatOn && IsWearingGerudoMask();
    if (!want && sPrevWantGerudo) {
        const char* cur = O2rLoader_GetForcedName();
        if (cur != nullptr && std::strcmp(cur, "gerudo") == 0 &&
            MmForm_GetCurrentForm() != MM_PLAYER_FORM_GERUDO) {
            O2rLoader_ClearForcedModel();
            SPDLOG_INFO("[GerudoForm] mask removed via UI — ClearForcedModel (safety net)");
        }
    }
    sPrevWantGerudo = want;

    // Sandstorm OFF in Haunted Wasteland (per-frame, so toggling the mask
    // mid-scene clears the sandstorm without re-entering the area), plus the
    // "cross the desert" offer.
    //
    // CRITICAL: never force OFF while a transition is running. Every wasteland
    // exit/entry uses a TRANS_TYPE_SANDSTORM_* wipe whose completion check
    // waits for sandstormPrimA/EnvA to fill (z_play.c TRANS_MODE_SANDSTORM) —
    // and those alphas only advance inside Environment_DrawSandstorm, which is
    // skipped entirely while sandstormState == SANDSTORM_OFF. Forcing OFF
    // mid-wipe therefore hangs the transition forever: the player freezes
    // (LOADING|IN_CUTSCENE) on the exit plane and can never leave the desert.
    if (GerudoForm_IsActive() && gPlayState->sceneNum == SCENE_HAUNTED_WASTELAND) {
        if (gPlayState->transitionTrigger == TRANS_TRIGGER_OFF && gPlayState->transitionMode == TRANS_MODE_OFF) {
            gPlayState->envCtx.sandstormState = SANDSTORM_OFF;
        }
        GerudoForm_TickWastelandWarp(gPlayState);
    } else if (sWastelandOfferOpen || sWastelandOfferPending) {
        // Form deactivated (or left the scene) with the offer pending — drop it
        // so the player isn't left frozen.
        Player* p = GET_PLAYER(gPlayState);
        if (p != nullptr) {
            p->stateFlags1 &= ~PLAYER_STATE1_IN_CUTSCENE;
        }
        GerudoForm_ResetWastelandOffer();
    }
}

void GerudoForm_OnTransitionEnd(int16_t sceneNum) {
    // A completed transition clears the in-flight offer/warp state. If we just
    // arrived in the wasteland (legit entry OR looped back from getting lost)
    // while wearing the mask, arm the "cross the desert" offer for the side
    // opposite the one we spawned at.
    GerudoForm_ResetWastelandOffer();

    if (sceneNum == SCENE_HAUNTED_WASTELAND && GerudoForm_IsActive() && gPlayState != nullptr) {
        gPlayState->envCtx.sandstormState = SANDSTORM_OFF;

        // Spawn 0 = entered from Gerudo Fortress (east) → offer Desert Colossus.
        // Spawn 1 = entered from Desert Colossus (west) → offer Gerudo Fortress.
        sWastelandOfferToFortress = (gPlayState->curSpawn != 0);
        sWastelandOfferDest = sWastelandOfferToFortress ? ENTR_GERUDOS_FORTRESS_GATE_EXIT
                                                        : ENTR_DESERT_COLOSSUS_EAST_EXIT;
        sWastelandOfferPending = true;
    }
}

} // namespace

extern "C" u8 GerudoForm_IsActive(void) {
    // Primary signal: MM form pipeline says we're Gerudo (ACTIVE / TRANSFORMING /
    // DETRANSFORMING). Fallback to O2rLoader for legacy code paths that fire
    // before the MM state updates (e.g. mid-cutscene draw hooks).
    if (MmForm_GetCurrentForm() == MM_PLAYER_FORM_GERUDO) {
        return 1;
    }
    if (!CVarGetInteger(CVAR_GERUDO_TRANSFORM, 0)) {
        return 0;
    }
    const char* cur = O2rLoader_GetForcedName();
    return (cur != nullptr && std::strcmp(cur, "gerudo") == 0) ? 1 : 0;
}

// No-op now — the previous architecture used GerudoHybrid_Update/Draw to
// render the gerudo body through a separate skel. Current pipeline uses a
// Link-rigged gerudo skin so Player_DrawImpl handles everything. Kept so
// z_player.c's older callsite (if any survives) still links cleanly.
extern "C" s32 GerudoForm_TryDrawSmoothSkin(PlayState* play, Player* player) {
    (void)play;
    (void)player;
    return 0;
}

// Dual-wield sword DLs sourced from gerudo.o2r. Same DL for both hands and
// both ages — the adult Master Sword DL is used universally. The right-hand
// bone matrix mirrors it naturally so the second sword orients correctly,
// and the child skel's smaller bone scale shrinks the sword proportionally
// so it doesn't look oversized on child Link.
namespace {
constexpr const char* kGerudoSword = "__OTR__objects/gerudoPlayer/object_link_boy/gLinkAdultLeftHandHoldingMasterSwordNearDL";

// Gerudo-skinned R-hand shield DLs (already in gerudo.o2r from the repack).
// Indexed visually by the equipped shield. We resolve these manually because
// vanilla's right-hand draw path runs the DL through ResourceMgr_LoadGfxByName
// BEFORE the gerudo-hybrid path-swap can act, turning the path string into a
// real Gfx* — so the swap (which expects a path-prefix match) can't intercept.
// Returning the gerudo path directly from the override sets *dList before the
// resolve happens.
constexpr const char* kGerudoShieldAdultHylian = "__OTR__objects/gerudoPlayer/object_link_boy/gLinkAdultRightHandHoldingHylianShieldNearDL";
constexpr const char* kGerudoShieldAdultMirror = "__OTR__objects/gerudoPlayer/object_link_boy/gLinkAdultRightHandHoldingMirrorShieldNearDL";
constexpr const char* kGerudoShieldChildDeku   = "__OTR__objects/gerudoPlayer/object_link_child/gLinkChildRightFistAndDekuShieldNearDL";

// === Phase 2: Sword visibility state machine ===
// FREE   — empty hands (like Zora's small fins at idle). Default after a
//          transformation or after sheathing.
// COMBAT — dual scimitars in both hands. Entered when the gerudo combo lands a
//          slash (gerudoQuadsActive). Sticky: stays out for kCombatIdleFrames
//          past the last hit so a chained combo doesn't sheath between slashes.
//
// SHIELD is not a separate mode — it's gated on PLAYER_STATE1_SHIELDING from
// the vanilla shield action, layered ON TOP of whatever sword mode is current:
//   * L_HAND keeps drawing the sword DL (the one Link's holding to shield).
//   * R_HAND draws the equipped shield via the path-swap (returns NULL here).
// On shield release we drop back to FREE so the user sees the "double swords"
// disappear with the rest of the shield exit, matching the user's spec
// ("guardar las double swords y sacar el shield" — and inverse on release).
enum class GerudoSwordMode : u8 { FREE = 0, COMBAT = 1 };
GerudoSwordMode gSwordMode = GerudoSwordMode::FREE;
u32 gCombatIdleTimer = 0;
bool gWasShielding = false;
// 120 frames @ 60 fps = ~2 s of inactivity before the swords sheath. Long
// enough to chain slashes without flicker, short enough that the user sees
// the swords go away after a real pause.
constexpr u32 kCombatIdleFrames = 120;
} // namespace

// ResourceMgr_LoadGfxByName crashes (null deref on `res->Instructions[0]`) if
// the path doesn't exist in any loaded .o2r. Gate every call with FileExists.
static Gfx* SafeLoadGfx(const char* path) {
    if (path == nullptr || !ResourceMgr_FileExists(path)) return nullptr;
    return ResourceMgr_LoadGfxByName(path);
}

// Fired on every mode transition. Vanilla unsheath/sheath SFX so the swap
// reads as a sword draw instead of a silent pop.
static void GerudoForm_PlayModeSfx(Player* p, u16 sfxId) {
    if (p == nullptr) return;
    Audio_PlaySoundGeneral(sfxId, &p->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

// Reset state on form exit so re-equipping the mask later starts clean.
extern "C" void GerudoForm_ResetSwordMode(void) {
    gSwordMode = GerudoSwordMode::FREE;
    gCombatIdleTimer = 0;
    gWasShielding = false;
}

extern "C" Gfx* GerudoForm_GetSwordDL_L(void) {
    if (!GerudoForm_IsActive()) return nullptr;
    // SHIELDING overrides FREE — vanilla draws the sword on L_HAND while
    // blocking, so we must keep returning the sword DL even from FREE mode.
    if (gPlayState != nullptr) {
        Player* p = GET_PLAYER(gPlayState);
        if (p != nullptr && (p->stateFlags1 & PLAYER_STATE1_SHIELDING)) {
            return SafeLoadGfx(kGerudoSword);
        }
    }
    if (gSwordMode == GerudoSwordMode::FREE) return nullptr;
    return SafeLoadGfx(kGerudoSword);
}

extern "C" Gfx* GerudoForm_GetSwordDL_R(void) {
    if (!GerudoForm_IsActive()) return nullptr;
    // While the vanilla Mirror Shield action is up, the right hand holds the
    // EQUIPPED shield. We return the gerudo-skinned shield DL directly here
    // instead of NULL because vanilla's right-hand draw path resolves the DL
    // (ResourceMgr_LoadGfxByName) BEFORE the GerudoForm_OverrideLimbDraw
    // path-swap can run — so the swap can't convert the vanilla shield path
    // into the gerudo one. Setting *dList from the override bypasses that
    // resolve entirely. The vanilla shield MECHANICS still fire (rightHandType
    // = RH_SHIELD is set in the shield actionFunc via
    // Player_SetModelsForHoldingShield, gated by Player_HoldsTwoHandedWeapon
    // returning false — see GerudoForm_Update's heldItemAction force).
    if (gPlayState != nullptr) {
        Player* p = GET_PLAYER(gPlayState);
        if (p != nullptr && (p->stateFlags1 & PLAYER_STATE1_SHIELDING)) {
            const char* path = nullptr;
            if (LINK_IS_ADULT) {
                switch (p->currentShield) {
                    case PLAYER_SHIELD_HYLIAN: path = kGerudoShieldAdultHylian; break;
                    case PLAYER_SHIELD_MIRROR: path = kGerudoShieldAdultMirror; break;
                    default:                   path = kGerudoShieldAdultMirror; break; // Mirror fallback
                }
            } else {
                // Child only has the Deku shield gerudo-skinned in the .o2r.
                path = kGerudoShieldChildDeku;
            }
            Gfx* shield = SafeLoadGfx(path);
            return shield; // NULL → falls through to vanilla; non-NULL → drawn
        }
    }
    if (gSwordMode == GerudoSwordMode::FREE) return nullptr;
    // Combat: dual-scimitar — same DL as left hand, right-hand bone matrix
    // mirrors it naturally so the second sword orients correctly.
    return SafeLoadGfx(kGerudoSword);
}

extern "C" void GerudoForm_GetTunicColor(s32 tunic, Color_RGB8* out) {
    if (out == nullptr) {
        return;
    }
    if (tunic < PLAYER_TUNIC_KOKIRI || tunic > PLAYER_TUNIC_ZORA) {
        tunic = PLAYER_TUNIC_KOKIRI;
    }
    Color_RGB8 c = sTunicColors[tunic];

    if (tunic == PLAYER_TUNIC_KOKIRI && CVarGetInteger(CVAR_COSMETIC("Link.KokiriTunic.Changed"), 0)) {
        c = CVarGetColor24(CVAR_COSMETIC("Link.KokiriTunic.Value"), sTunicColors[PLAYER_TUNIC_KOKIRI]);
    } else if (tunic == PLAYER_TUNIC_GORON && CVarGetInteger(CVAR_COSMETIC("Link.GoronTunic.Changed"), 0)) {
        c = CVarGetColor24(CVAR_COSMETIC("Link.GoronTunic.Value"), sTunicColors[PLAYER_TUNIC_GORON]);
    } else if (tunic == PLAYER_TUNIC_ZORA && CVarGetInteger(CVAR_COSMETIC("Link.ZoraTunic.Changed"), 0)) {
        c = CVarGetColor24(CVAR_COSMETIC("Link.ZoraTunic.Value"), sTunicColors[PLAYER_TUNIC_ZORA]);
    }
    *out = c;
}

extern "C" void GerudoForm_Init(void) {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

    // Gerudo NPCs treat the player as a friendly Gerudo while the mask is worn.
    REGISTER_VB_SHOULD(VB_GERUDOS_BE_FRIENDLY, {
        if (GerudoForm_IsActive()) {
            *should = true;
        }
    });

    // Skip the forced card-giving path while the mask is worn — access is
    // temporary, no QUEST_GERUDO_CARD is granted.
    REGISTER_VB_SHOULD(VB_GIVE_ITEM_GERUDO_MEMBERSHIP_CARD, {
        if (GerudoForm_IsActive()) {
            *should = false;
        }
    });

    // En_GeldB (Gerudo Fighter miniboss) — don't throw the player in jail
    // while the mask is worn. The miniboss is a duel test, not a fortress
    // patrol; Gerudo lore says they wouldn't capture one of their own.
    REGISTER_VB_SHOULD(VB_GERUDO_FIGHTER_THROW_LINK_TO_JAIL, {
        if (GerudoForm_IsActive()) {
            *should = false;
        }
    });

    // Desert skill — Haunted Wasteland "cross the desert" offer.
    // The vanilla lost-warp runs untouched (no interception → no softlock).
    // Once we spawn in the wasteland (legit entry or looped back from getting
    // lost), GerudoForm_OnTransitionEnd arms a skippable Yes/No offer to warp
    // straight to the opposite side of the desert; GerudoForm_TickWastelandWarp
    // opens it in normal control and performs the warp (or B/No to walk it).

    // Offer message — Yes/No, injected on demand for our custom textId. The
    // destination side is chosen from the spawn point in OnTransitionEnd.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnOpenText>(
        [](uint16_t* textId, bool* loadFromMessageTable) {
            if (*textId != kWastelandWarpTextId) {
                return;
            }
            const char* body =
                sWastelandOfferToFortress
                    ? "Cross the desert to the&Gerudo Fortress?\x1B%gYes&No%w"
                    : "Cross the desert to the&Desert Colossus?\x1B%gYes&No%w";
            CustomMessage msg(body);
            msg.AutoFormat();
            msg.LoadIntoFont();
            *loadFromMessageTable = false;
        });

    COND_HOOK(OnTransitionEnd, true, GerudoForm_OnTransitionEnd);
    COND_HOOK(OnPlayerUpdate, true, GerudoForm_OnPlayerUpdate);
}

// ShipInit-registered entry point. Hooks check the cheat CVar at call time,
// so no need to register/unregister on CVar changes.
static void GerudoForm_RegisterShipInit() {
    GerudoForm_Init();
}
static RegisterShipInitFunc sGerudoFormInitFunc(GerudoForm_RegisterShipInit, {});

// ============================================================================
// Combat state machine — dual-scimitar slash combo, jump attack, block-mirror
//
// Architecturally simpler than Garo's form-exclusive SkelAnime: Gerudo runs as
// a "skin mode" (MmForm_IsGerudoSkinMode → true), so OOT's actionFunc keeps
// driving walk/run/jump/swim/climb. We only intervene during B-slashes and
// R-blocks. The trick: set PLAYER_STATE3_PAUSE_ACTION_FUNC during attack
// frames so OOT's actionFunc skips its tick — the slash anim runs on
// player->skelAnime undisturbed.
//
// TODO (combat balancing):
//   - Bone-attached collision quads for L/R-hand sword damage (parallel to
//     mm_form_combat.c's Goron punch quads, with Master Sword damage tier).
//   - In-air detection for A-press → jump attack composite (jump_rollkiru →
//     Lpower_jump_kiru_end). Currently A falls through to OOT (vanilla jump).
//   - Block-mirror reflect actor: spawn an effect actor in front of Link
//     during R-hold that catches inbound projectiles (En_Arrow, En_Boom,
//     En_Nuts, etc.) and bounces them with v.x/v.z inverted. Currently the
//     block plays the kf_hanare_loop anim but doesn't reflect — damage just
//     gets absorbed by the OOT shield check (which Link doesn't have, so
//     nothing actually mitigates yet).
// ============================================================================

namespace {

enum GerudoCombatState : u8 {
    GCS_IDLE = 0,
    GCS_SLASH_1,
    GCS_SLASH_2,
    GCS_SLASH_3,
    GCS_JUMP_ATTACK,    // Phase 4: A-press while airborne
    GCS_JUMP_ATTACK_END,// Phase 4: chained Lpower_jump_kiru_end recovery
    GCS_BLOCK,
    GCS_RECOVER,
};

struct GerudoCombat {
    GerudoCombatState state;
    s16 stateTimer;
    u8 comboBPressed; // sticky: B was pressed during current swing → chain next
};

GerudoCombat sCombat = { GCS_IDLE, 0, 0 };

// Damage tier per the user's plan: always Master Sword damage (2 hearts = 4
// damage points), finisher AOE 2x (8), jump attack 1.5x (~6). Reflected
// projectiles inherit their original damage when they hit the original owner.
constexpr s16 kSlashDamage    = 4;
constexpr s16 kFinisherDamage = 8;
constexpr s16 kJumpAtkDamage  = 6;

// Active frame windows per attack. Quad ON during [beg,end]. Tuned to match
// the swing arcs visually — adjust if the anim phases change.
constexpr s16 kSlash1QuadBeg = 2;
constexpr s16 kSlash1QuadEnd = 10;
constexpr s16 kSlash2QuadBeg = 2;
constexpr s16 kSlash2QuadEnd = 10;
constexpr s16 kFinisherQuadBeg = 4;
constexpr s16 kFinisherQuadEnd = 22;  // longer — Wrolling spin sweeps 360°
constexpr s16 kJumpAtkQuadBeg = 2;
constexpr s16 kJumpAtkQuadEnd = 14;

// Anim resource paths. Cached on first access via ResourceMgr_LoadAnimByName.
// Picked by the user during planning (see plan file).
constexpr const char* kAnimSlash1   = "__OTR__objects/gameplay_keep/gPlayerAnim_link_normal_light_bom";
constexpr const char* kAnimSlash2   = "__OTR__objects/gameplay_keep/gPlayerAnim_link_fighter_Lnormal_kiru";
constexpr const char* kAnimFinisher = "__OTR__objects/gameplay_keep/gPlayerAnim_link_fighter_Wrolling_kiru";
constexpr const char* kAnimBlock    = "__OTR__misc/link_animetion/gPlayerAnim_kf_hanare_loop";
constexpr const char* kAnimJumpAtk1 = "__OTR__objects/gameplay_keep/gPlayerAnim_link_fighter_jump_rollkiru";
constexpr const char* kAnimJumpAtk2 = "__OTR__objects/gameplay_keep/gPlayerAnim_link_fighter_Lpower_jump_kiru_end";

LinkAnimationHeader* LoadAnim(const char* path) {
    // Gate every load — the underlying resource mgr crashes on missing paths.
    // ResourceMgr_LoadAnimByName returns AnimationHeaderCommon*; LinkAnimationHeader
    // shares the common prefix (frameCount + segment), so the cast is safe.
    if (path == nullptr || !ResourceMgr_FileExists(path)) return nullptr;
    return (LinkAnimationHeader*)ResourceMgr_LoadAnimByName(path);
}

void StartAnim(PlayState* play, Player* player, const char* path, f32 playSpeed) {
    LinkAnimationHeader* anim = LoadAnim(path);
    if (anim == nullptr) {
        SPDLOG_WARN("[GerudoForm] anim not found: {}", path);
        return;
    }
    f32 endFrame = Animation_GetLastFrame(anim);
    LinkAnimation_Change(play, &player->skelAnime, anim, playSpeed, 0.0f, endFrame, ANIMMODE_ONCE, -2.0f);
}

void ResetCombat() {
    sCombat.state = GCS_IDLE;
    sCombat.stateTimer = 0;
    sCombat.comboBPressed = 0;
}

// Enable a forward-extending sword damage quad on player->meleeWeaponQuads[0].
// Replicates the geometry math from mm_form_combat.c / garo_form.cpp's spin
// quad (those helpers are static — we inline it here).
void EnableSlashQuad(Player* player, PlayState* play, f32 nearDist, f32 farDist, f32 halfW,
                     f32 yBottom, f32 yTop, s16 damage) {
    ColliderQuad* quad = &player->meleeWeaponQuads[0];

    f32 sinYaw = Math_SinS(player->actor.shape.rot.y);
    f32 cosYaw = Math_CosS(player->actor.shape.rot.y);
    f32 rightX = cosYaw;
    f32 rightZ = -sinYaw;

    Vec3f pos = player->actor.world.pos;

    f32 farCX  = pos.x + sinYaw * farDist;
    f32 farCZ  = pos.z + cosYaw * farDist;
    f32 nearCX = pos.x + sinYaw * nearDist;
    f32 nearCZ = pos.z + cosYaw * nearDist;

    Vec3f a, b, c, d;
    a.x = farCX  - rightX * halfW; a.y = pos.y + yTop;    a.z = farCZ  - rightZ * halfW;
    b.x = farCX  + rightX * halfW; b.y = pos.y + yTop;    b.z = farCZ  + rightZ * halfW;
    c.x = nearCX + rightX * halfW; c.y = pos.y + yBottom; c.z = nearCZ + rightZ * halfW;
    d.x = nearCX - rightX * halfW; d.y = pos.y + yBottom; d.z = nearCZ - rightZ * halfW;

    Collider_ResetQuadAT(play, &quad->base);
    Collider_SetQuadVertices(quad, &a, &b, &c, &d);

    quad->base.atFlags = AT_ON | AT_TYPE_PLAYER;
    quad->info.toucher.dmgFlags = DMG_SLASH_MASTER;
    quad->info.toucher.damage = (u8)damage;
    quad->info.toucherFlags = TOUCH_ON | TOUCH_NEAREST;

    CollisionCheck_SetAT(play, &play->colChkCtx, &quad->base);
}

void DisableSlashQuad(Player* player) {
    player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
}

// During BLOCK: walk the actor list looking for projectile actors in a frontal
// cone, flip their velocity → reflected. Owner is reassigned to the player so
// they hit the original shooter on collision.
//
// Coverage:
//   En_Arrow      (arrows including fire/ice/light)
//   En_Boom       (boomerangs)
//   En_Nutsball   (Deku Scrub nutsballs)
//   En_Bom_Chu    (Bombchu)
// Other projectile actors (En_Bom, En_Fire_Rock, En_Eg, En_Elf) intentionally
// skipped — bombs are slow and player-thrown; the Mirror Shield in vanilla
// only reflects light/fire/ice rays + arrows + nuts.
void ReflectProjectiles(PlayState* play, Player* player) {
    constexpr f32 kReflectRadius = 80.0f;
    constexpr f32 kFrontalDot = 0.3f; // cos(~72°) — wide-ish frontal cone

    f32 sinYaw = Math_SinS(player->actor.shape.rot.y);
    f32 cosYaw = Math_CosS(player->actor.shape.rot.y);

    auto tryReflect = [&](Actor* it) {
        for (; it != nullptr; it = it->next) {
            // Only reflect projectiles that are NOT owned by the player (avoid
            // re-reflecting our own arrows mid-flight).
            if (it->id != ACTOR_EN_ARROW && it->id != ACTOR_EN_BOOM &&
                it->id != ACTOR_EN_NUTSBALL && it->id != ACTOR_EN_BOM_CHU) {
                continue;
            }
            f32 dx = it->world.pos.x - player->actor.world.pos.x;
            f32 dz = it->world.pos.z - player->actor.world.pos.z;
            f32 dist2 = dx * dx + dz * dz;
            if (dist2 > kReflectRadius * kReflectRadius) continue;

            f32 dist = sqrtf(dist2);
            if (dist < 0.001f) continue;

            // Frontal-cone test: projectile must be in front of player and
            // moving toward them (velocity facing the player).
            f32 dirX = dx / dist;
            f32 dirZ = dz / dist;
            f32 dotFacing = dirX * sinYaw + dirZ * cosYaw;
            if (dotFacing < kFrontalDot) continue;

            // Velocity facing test — only reflect inbound, not outbound.
            f32 vMag = sqrtf(it->velocity.x * it->velocity.x + it->velocity.z * it->velocity.z);
            if (vMag < 0.5f) continue;
            f32 vIntoPlayer = -(it->velocity.x * dirX + it->velocity.z * dirZ) / vMag;
            if (vIntoPlayer < 0.3f) continue;

            // Reflect: invert horizontal velocity, reassign owner so the
            // projectile damages whoever shot it on the return trip.
            it->velocity.x = -it->velocity.x;
            it->velocity.z = -it->velocity.z;
            it->speedXZ = -it->speedXZ;
            it->world.rot.y += 0x8000;
            it->shape.rot.y = it->world.rot.y;
            // Reassign parent so the projectile won't ignore its original owner
            // when colliding back.
            if (it->parent != nullptr && it->parent != &player->actor) {
                Actor* originalShooter = it->parent;
                it->parent = &player->actor;
                (void)originalShooter; // (kept for future targeting if needed)
            }
        }
    };

    tryReflect(play->actorCtx.actorLists[ACTORCAT_ENEMY].head);
    tryReflect(play->actorCtx.actorLists[ACTORCAT_ITEMACTION].head);
    tryReflect(play->actorCtx.actorLists[ACTORCAT_MISC].head);
}

bool IsAirborne(Player* player) {
    return !(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND);
}

} // namespace

extern "C" void GerudoForm_Update(PlayState* play, Player* player) {
    if (!GerudoForm_IsActive()) {
        if (sCombat.state != GCS_IDLE) {
            ResetCombat();
            player->stateFlags3 &= ~PLAYER_STATE3_PAUSE_ACTION_FUNC;
        }
        // Reset sword-mode state so the next time the mask is equipped we
        // start in FREE with no stale shield/idle tracking.
        GerudoForm_ResetSwordMode();
        return;
    }

    // === Mirror Shield = 1:1 fallback to vanilla Link ===
    // Root cause of every prior shield failure: Gerudo inherits whatever sword the
    // player had equipped before transforming. If that was Biggoron Sword,
    // Player_HoldsTwoHandedWeapon returns true and Player_SetModelsForHoldingShield
    // refuses to set rightHandType = RH_SHIELD → shieldQuad never arms, light
    // reflection never fires. Fix: force heldItemAction to the one-handed Master
    // (adult) / Kokiri (child) sword EVERY frame while Gerudo is active. Vanilla
    // then runs its full Mirror Shield pipeline on R-press — defense crouch, sword
    // sparks, projectile bounce, light reflection, shield-walk movement, all 1:1.
    // Combat damage isn't affected: the gerudo combo writes gerudoQuadDamage
    // directly on the melee weapon quads independent of heldItemAction.
    if (play != nullptr && player != nullptr) {
        s8 desiredIA = LINK_IS_ADULT ? PLAYER_IA_SWORD_MASTER : PLAYER_IA_SWORD_KOKIRI;
        // Sync heldItemAction AND itemAction, but ONLY when Link is currently
        // holding a two-handed weapon (BGS / Hammer) — that's the state that
        // blocks Player_SetModelsForHoldingShield from promoting rightHandType
        // to RH_SHIELD. For any other heldItemAction (bow, hookshot, sword
        // master/kokiri already, item use, NONE, etc.) we leave it alone so
        // vanilla item use / C-button items / temporary equipment swaps all
        // run unmodified. This is the rule "1:1 vanilla unless explicit".
        if (player->heldItemAction >= PLAYER_IA_SWORD_BIGGORON &&
            player->heldItemAction <= PLAYER_IA_HAMMER) {
            player->heldItemAction = desiredIA;
            player->itemAction = desiredIA;
        }
        // Gerudo blocks the shield equip slot, so if the player hadn't equipped
        // a shield before transforming we default to MIRROR EVERY frame —
        // otherwise R-press would silently no-op on the first frame
        // (currentShield == NONE fails the ActionHandler_11 gate). If a real
        // shield IS equipped (Deku / Hylian / Mirror) we leave it alone — the
        // "respective model" intent — so the equipped one draws. Doing this
        // pre-actionFunc would be ideal, but TransformMasks_Update runs after
        // Player_UpdateCommon; forcing it here still wins because
        // currentShield persists frame-to-frame and ActionHandler_11 reads
        // the value set at the end of the previous tick.
        if (player->currentShield == PLAYER_SHIELD_NONE) {
            player->currentShield = PLAYER_SHIELD_MIRROR;
        }
    }

    // === Sword visibility state machine (FREE / COMBAT) ===
    // FREE → COMBAT: gerudoQuadsActive flips on (a real slash landed). Plays the
    //     vanilla unsheath SFX so the swords pop in with a "sword draw" cue.
    // COMBAT → FREE: SHIELD just released, OR no slash for kCombatIdleFrames.
    //     Plays the vanilla sheath SFX — the "Zora fins shrinking" analog the
    //     user asked for.
    // SHIELDING is the third (implicit) mode: GetSwordDL_L/R already handle it
    // (L = sword, R = shield via vanilla path-swap), so the mode doesn't change
    // while shielding — but on shield release we drop back to FREE.
    if (player != nullptr) {
        bool nowShielding = (player->stateFlags1 & PLAYER_STATE1_SHIELDING) != 0;

        // SHIELD release → FREE (matches user spec: shield ends = swords gone).
        if (gWasShielding && !nowShielding && gSwordMode == GerudoSwordMode::COMBAT) {
            gSwordMode = GerudoSwordMode::FREE;
            gCombatIdleTimer = 0;
            GerudoForm_PlayModeSfx(player, NA_SE_IT_SWORD_PUTAWAY);
        }
        gWasShielding = nowShielding;

        // Active slash refreshes COMBAT mode + resets the idle timer.
        if (GerudoForm_PunchActiveThisFrame()) {
            if (gSwordMode == GerudoSwordMode::FREE) {
                GerudoForm_PlayModeSfx(player, NA_SE_IT_SWORD_PICKOUT);
            }
            gSwordMode = GerudoSwordMode::COMBAT;
            gCombatIdleTimer = 0;
        } else if (gSwordMode == GerudoSwordMode::COMBAT && !nowShielding) {
            // No active slash + not shielding: count toward auto-putaway.
            gCombatIdleTimer++;
            if (gCombatIdleTimer >= kCombatIdleFrames) {
                gSwordMode = GerudoSwordMode::FREE;
                gCombatIdleTimer = 0;
                GerudoForm_PlayModeSfx(player, NA_SE_IT_SWORD_PUTAWAY);
            }
        }
    }

    // When the MM form pipeline owns Gerudo (cutscene + flash + MM action loop),
    // the combat is dispatched through MmForm_StartPunch / MmForm_Action_Punch on
    // gFormState.formSkelAnime — running our local state machine on
    // player->skelAnime in parallel would fight the form for control. The local
    // path stays as a fallback for the legacy O2rLoader-only entrypoint.
    if (MmForm_GetCurrentForm() == MM_PLAYER_FORM_GERUDO) {
        if (sCombat.state != GCS_IDLE) {
            ResetCombat();
            player->stateFlags3 &= ~PLAYER_STATE3_PAUSE_ACTION_FUNC;
        }
        return;
    }

    if (play == nullptr || player == nullptr) return;

    // Skip combat while in cutscene / dead / climbing — let OOT handle it.
    const u32 blockMask = PLAYER_STATE1_DEAD | PLAYER_STATE1_LOADING | PLAYER_STATE1_CLIMBING_LADDER |
                          PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_IN_CUTSCENE;
    if (player->stateFlags1 & blockMask) {
        if (sCombat.state != GCS_IDLE) {
            ResetCombat();
            player->stateFlags3 &= ~PLAYER_STATE3_PAUSE_ACTION_FUNC;
        }
        return;
    }

    // Read raw input — TransformMasks_FilterB stripped B/R from the copy that
    // OOT's actionFunc sees, but play->state.input[0] still has the raw bits.
    Input* input = &play->state.input[0];
    bool bPress = CHECK_BTN_ALL(input->press.button, BTN_B) != 0;
    bool rHold = CHECK_BTN_ALL(input->cur.button, BTN_R) != 0;
    bool aPress = CHECK_BTN_ALL(input->press.button, BTN_A) != 0;

    sCombat.stateTimer++;

    switch (sCombat.state) {
        case GCS_IDLE: {
            // R hold takes priority over B press — block-mirror stance.
            if (rHold) {
                StartAnim(play, player, kAnimBlock, 1.0f);
                sCombat.state = GCS_BLOCK;
                sCombat.stateTimer = 0;
                player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
                // Stop horizontal momentum
                player->linearVelocity = 0;
                break;
            }
            // A-press while airborne → jump attack composite. Ground A still
            // falls through to OOT (vanilla jump/sidehop/talk).
            if (aPress && IsAirborne(player)) {
                StartAnim(play, player, kAnimJumpAtk1, 1.5f);
                sCombat.state = GCS_JUMP_ATTACK;
                sCombat.stateTimer = 0;
                player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
                break;
            }
            if (bPress) {
                StartAnim(play, player, kAnimSlash1, 1.5f);
                sCombat.state = GCS_SLASH_1;
                sCombat.stateTimer = 0;
                sCombat.comboBPressed = 0;
                player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
                player->linearVelocity = 0;
            }
            break;
        }
        case GCS_SLASH_1:
        case GCS_SLASH_2:
        case GCS_SLASH_3: {
            // Freeze movement during swings.
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->actor.velocity.x = 0;
            player->actor.velocity.z = 0;
            player->linearVelocity = 0;

            // Buffer B-press for chain.
            if (bPress) sCombat.comboBPressed = 1;

            // Damage quad active during the swing arc window.
            s16 quadBeg, quadEnd, quadDamage;
            f32 nearDist, farDist, halfW;
            if (sCombat.state == GCS_SLASH_3) {
                // Finisher = wide rolling spin → AOE 360° (we approximate by a
                // very long quad in front; the rotating shape.rot.y sweeps it).
                quadBeg = kFinisherQuadBeg;
                quadEnd = kFinisherQuadEnd;
                quadDamage = kFinisherDamage;
                nearDist = 5.0f;
                farDist  = 55.0f;
                halfW    = 35.0f;
            } else {
                quadBeg = (sCombat.state == GCS_SLASH_1) ? kSlash1QuadBeg : kSlash2QuadBeg;
                quadEnd = (sCombat.state == GCS_SLASH_1) ? kSlash1QuadEnd : kSlash2QuadEnd;
                quadDamage = kSlashDamage;
                nearDist = 10.0f;
                farDist  = 45.0f;
                halfW    = 22.0f;
            }
            if (sCombat.stateTimer >= quadBeg && sCombat.stateTimer <= quadEnd) {
                EnableSlashQuad(player, play, nearDist, farDist, halfW, 0.0f, 50.0f, quadDamage);
            } else {
                DisableSlashQuad(player);
            }

            s32 done = LinkAnimation_Update(play, &player->skelAnime);
            if (done) {
                DisableSlashQuad(player);
                if (sCombat.state == GCS_SLASH_1 && sCombat.comboBPressed) {
                    StartAnim(play, player, kAnimSlash2, 1.5f);
                    sCombat.state = GCS_SLASH_2;
                    sCombat.stateTimer = 0;
                    sCombat.comboBPressed = 0;
                } else if (sCombat.state == GCS_SLASH_2 && sCombat.comboBPressed) {
                    StartAnim(play, player, kAnimFinisher, 1.0f);
                    sCombat.state = GCS_SLASH_3;
                    sCombat.stateTimer = 0;
                    sCombat.comboBPressed = 0;
                } else {
                    // End of combo — return to idle.
                    sCombat.state = GCS_RECOVER;
                    sCombat.stateTimer = 0;
                }
            }
            break;
        }
        case GCS_JUMP_ATTACK: {
            // Don't freeze vertical movement — Link should still fall.
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            if (sCombat.stateTimer >= kJumpAtkQuadBeg && sCombat.stateTimer <= kJumpAtkQuadEnd) {
                EnableSlashQuad(player, play, 5.0f, 50.0f, 30.0f, -10.0f, 50.0f, kJumpAtkDamage);
            } else {
                DisableSlashQuad(player);
            }
            s32 done = LinkAnimation_Update(play, &player->skelAnime);
            if (done) {
                DisableSlashQuad(player);
                StartAnim(play, player, kAnimJumpAtk2, 1.5f);
                sCombat.state = GCS_JUMP_ATTACK_END;
                sCombat.stateTimer = 0;
            }
            break;
        }
        case GCS_JUMP_ATTACK_END: {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            s32 done = LinkAnimation_Update(play, &player->skelAnime);
            // Once we hit the ground OR the end-anim finishes, recover.
            if (done || !IsAirborne(player)) {
                sCombat.state = GCS_RECOVER;
                sCombat.stateTimer = 0;
            }
            break;
        }
        case GCS_BLOCK: {
            // Stay in block while R is held. Anim loops naturally (ANIMMODE_ONCE
            // ends on last frame; the LinkAnimation_Update poll keeps us frozen
            // there — that's the "swords planted" pose).
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->linearVelocity = 0;
            player->actor.velocity.x = 0;
            player->actor.velocity.z = 0;
            LinkAnimation_Update(play, &player->skelAnime);
            // Mirror Shield reflect: scan nearby projectiles every frame and
            // bounce inbound ones back at their owner.
            ReflectProjectiles(play, player);
            if (!rHold) {
                sCombat.state = GCS_IDLE;
                sCombat.stateTimer = 0;
                player->stateFlags3 &= ~PLAYER_STATE3_PAUSE_ACTION_FUNC;
            }
            break;
        }
        case GCS_RECOVER: {
            // Brief recovery before allowing the next combo.
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->linearVelocity = 0;
            DisableSlashQuad(player);
            if (sCombat.stateTimer > 6) {
                sCombat.state = GCS_IDLE;
                sCombat.stateTimer = 0;
                player->stateFlags3 &= ~PLAYER_STATE3_PAUSE_ACTION_FUNC;
            }
            break;
        }
    }
}
