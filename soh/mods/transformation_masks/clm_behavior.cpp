/**
 * clm_behavior.cpp - Circus Leader's Mask (CLM) — Tax Collector interactions
 *
 * Architecture:
 *   1. Single global `OnOpenText` hook fires when any textbox opens.
 *   2. If CLM is worn AND the talkActor matches a registered adapter, we:
 *        - swap `*textId` to a CLM-specific custom message,
 *        - hijack `actor->update` (uniform offset across all Actors),
 *        - track per-actor state in gStates.
 *   3. Our `CLM_HijackedUpdate` runs in place of vanilla actor update each frame
 *      while the CLM dialogue is active. It detects player advance, closes the
 *      textbox, calls the adapter's grantReward, optionally waits for the
 *      item-get cutscene, then restores `actor->update` to vanilla.
 *
 * Adding a new NPC: add a CLMAdapter entry in kAdapters[] + a Build* message
 * function for each of its custom textIds.
 */

#include <unordered_map>
#include <spdlog/spdlog.h>

#include <soh/OTRGlobals.h>
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "soh/Enhancements/randomizer/static_data.h"

extern "C" {
#include "variables.h"
#include "functions.h"
#include "macros.h"
#include "mods/transformation_masks/mm_mask_wear.h"
#include "src/overlays/actors/ovl_En_Diving_Game/z_en_diving_game.h"

extern PlayState* gPlayState;
}

// ── Custom text IDs (0x9310–0x933F reserved for CLM) ────────────────────────
enum CLMTextId : uint16_t {
    // Shooting Gallery
    CLM_TEXT_SYATEKI_CHILD_FIRST  = 0x9310,
    CLM_TEXT_SYATEKI_CHILD_REPEAT = 0x9311,
    CLM_TEXT_SYATEKI_ADULT_FIRST  = 0x9312,
    CLM_TEXT_SYATEKI_ADULT_REPEAT = 0x9313,
    // Bombchu Bowling
    CLM_TEXT_BOWLING_FIRST        = 0x9314,
    CLM_TEXT_BOWLING_REPEAT       = 0x9315,
    // Ingo
    CLM_TEXT_INGO_CHILD           = 0x9316,
    CLM_TEXT_INGO_ADULT_PRETALON  = 0x9317,
    CLM_TEXT_INGO_ADULT_POSTTALON = 0x9318,
    CLM_TEXT_INGO_ALREADY         = 0x9319,
    // Talon (cucco game, child)
    CLM_TEXT_TALON_FIRST          = 0x931A,
    CLM_TEXT_TALON_ASLEEP         = 0x931B,
    // Adult Malon (sells cow)
    CLM_TEXT_MALON_BUY            = 0x931C,
    CLM_TEXT_MALON_BROKE          = 0x931D,
    CLM_TEXT_MALON_REPEAT         = 0x931E,
    // HBA Gerudo
    CLM_TEXT_HBA_FIRST            = 0x931F,
    CLM_TEXT_HBA_REPEAT           = 0x9320,
    // Fishing
    CLM_TEXT_FISHING_FIRST        = 0x9321,
    CLM_TEXT_FISHING_REPEAT       = 0x9322,
    // Treasure Chest
    CLM_TEXT_TAKARA_FIRST         = 0x9323,
    CLM_TEXT_TAKARA_REPEAT        = 0x9324,
    // Diving
    CLM_TEXT_DIVING_FIRST         = 0x9325,
    CLM_TEXT_DIVING_REPEAT        = 0x9326,
};

// HBA discriminator
#define GE1_TYPE_HORSEBACK_ARCHERY 0x45

// Bribe amounts for repeat CLM visits (per plan)
#define CLM_BRIBE_SYATEKI_CHILD 5
#define CLM_BRIBE_SYATEKI_ADULT 10
#define CLM_BRIBE_BOWLING       20
#define CLM_BRIBE_HBA           10
#define CLM_BRIBE_FISHING       5
#define CLM_BRIBE_TAKARA        20
#define CLM_BRIBE_DIVING        15
#define CLM_MALON_COW_PRICE     100

// ── CLM detection ───────────────────────────────────────────────────────────

static bool CLM_IsWorn() {
    return MmMaskWear_GetCurrent() == ITEM_MM_MASK_CIRCUS_LEADER;
}

// ── Per-actor interaction state ─────────────────────────────────────────────

enum class CLMPhase : uint8_t {
    TextShowing,    // CLM textbox visible
    WaitingForClose, // Player advanced; waiting for textbox to fully close (NONE state)
    TextClosed,     // Textbox fully gone; safe to grant reward
    RewardOffered,  // Vanilla Actor_OfferGetItem made; waiting for player to accept
    Done,           // Cleanup
};

typedef void (*ActorUpdateFunc)(Actor*, PlayState*);

struct CLMState {
    CLMPhase phase = CLMPhase::TextShowing;
    int16_t actorId = 0;
    s32 getItemId = 0;
    s16 bribeRupees = 0;
    bool firstTime = false;
    bool isChild = false;
    ActorUpdateFunc savedUpdate = nullptr;
};

static std::unordered_map<Actor*, CLMState> gStates;

// Per-actor snapshot of "last known good actionFunc" for actors whose vanilla
// state machine can get stuck on broken function pointers after CLM intercept
// (notably Diving Game). Captured each frame the actor is in a known-idle state
// (player not talking, no active CLM hijack), and restored in Done phase so the
// actor returns to its idle/Talk state for future interactions.
static std::unordered_map<Actor*, EnDivingGameActionFunc> gDivingSafeActionFunc;

// ── Direct rando delivery ───────────────────────────────────────────────────
//
// The rando system delivers items via two mechanisms:
//   1. OnFlagSet → RC queue → Item_DropCollectible (only fires when SkipGetItem-
//      Animation is enabled, AND excludes bombchu bowling explicitly).
//   2. GiveItemEntryWithoutActor on player update (gated by player state).
//
// To make CLM rando-weighted reliably for ALL checks (including bombchu bowling),
// we look up the RC's shuffled GetItemEntry and call GiveItemEntryFromActor
// directly. This triggers the standard item-get cutscene with the rando item.

static RandomizerCheck CLM_ResolveRandoCheck(const CLMState& s) {
    switch (s.actorId) {
        case ACTOR_EN_SYATEKI_MAN:
            return s.isChild ? RC_MARKET_SHOOTING_GALLERY_REWARD
                             : RC_KAK_SHOOTING_GALLERY_REWARD;
        case ACTOR_EN_BOM_BOWL_MAN:
            // Progressive: first prize then second prize
            return Flags_GetItemGetInf(ITEMGETINF_11)
                       ? RC_MARKET_BOMBCHU_BOWLING_SECOND_PRIZE
                       : RC_MARKET_BOMBCHU_BOWLING_FIRST_PRIZE;
        case ACTOR_EN_TA:
            return RC_LLR_TALONS_CHICKENS;
        case ACTOR_EN_GE1:
            return Flags_GetInfTable(INFTABLE_190) ? RC_GF_HBA_1500_POINTS
                                                    : RC_GF_HBA_1000_POINTS;
        case ACTOR_FISHING:
            return s.isChild ? RC_LH_CHILD_FISHING : RC_LH_ADULT_FISHING;
        case ACTOR_EN_TAKARA_MAN:
            return RC_MARKET_TREASURE_CHEST_GAME_REWARD;
        case ACTOR_EN_DIVING_GAME:
            return RC_ZD_DIVING_MINIGAME;
        default:
            return RC_UNKNOWN_CHECK;
    }
}

// Returns true if a rando item-get cutscene was started (caller waits for accept).
static bool CLM_TryDirectRandoDelivery(Actor* actor, PlayState* play, CLMState& s) {
    if (!IS_RANDO) return false;

    RandomizerCheck rc = CLM_ResolveRandoCheck(s);
    if (rc == RC_UNKNOWN_CHECK) return false;

    auto loc = Rando::Context::GetInstance()->GetItemLocation(rc);
    if (loc == nullptr || loc->HasObtained() ||
        loc->GetPlacedRandomizerGet() == RG_NONE) {
        SPDLOG_INFO("[CLM] Rando direct: RC 0x{:X} not deliverable (already obtained or no placement)",
                    static_cast<uint32_t>(rc));
        return false;
    }

    auto vanillaRG = Rando::StaticData::GetLocation(rc)->GetVanillaItem();
    GetItemEntry entry = Rando::Context::GetInstance()->GetFinalGIEntry(
        rc, true, (GetItemID)Rando::StaticData::RetrieveItem(vanillaRG).GetItemID());

    SPDLOG_INFO("[CLM] Rando direct delivery: RC 0x{:X}, item mod={} id={}",
                static_cast<uint32_t>(rc), entry.modIndex, entry.itemId);

    GiveItemEntryFromActor(actor, play, entry, 2000.0f, 1000.0f);
    // Mark the check as collected so the queue handler doesn't try again
    loc->SetCheckStatus(RCSHOW_COLLECTED);
    return true;
}

// ── Adapter ─────────────────────────────────────────────────────────────────

struct CLMAdapter {
    int16_t actorId;
    bool (*shouldIntercept)(Actor* actor);
    uint16_t (*resolveTextId)(Actor* actor, CLMState& outState);
    // Returns true if a vanilla `Actor_OfferGetItem` was actually made (need to
    // wait for the player to accept it). Returns false otherwise (rando intercepted
    // the VB hook, or the reward is purely flag/rupee-based, or the adapter triggers
    // a scene transition). Returning false skips RewardOffered phase to avoid
    // doubling rewards in randomizer mode.
    bool (*grantReward)(Actor* actor, PlayState* play, CLMState& state);
    // If true, vanilla update is passed through during CLM dialogue (keeps
    // animations/blink working). Set false for actors with complex CS-locking
    // talk handlers (e.g. Diving Game) where vanilla update can softlock.
    bool passVanillaUpdate;
};

// ─────────────────────────────────────────────────────────────────────────────
// 1. Shooting Gallery (En_Syateki_Man) — child & adult
// ─────────────────────────────────────────────────────────────────────────────

static bool Syateki_ShouldIntercept(Actor* actor) { return true; }

static uint16_t Syateki_ResolveTextId(Actor* actor, CLMState& s) {
    bool isChild = !LINK_IS_ADULT;
    bool already = isChild ? Flags_GetItemGetInf(ITEMGETINF_0D)
                           : Flags_GetItemGetInf(ITEMGETINF_0E);
    s.isChild = isChild;
    s.firstTime = !already;
    s.bribeRupees = isChild ? CLM_BRIBE_SYATEKI_CHILD : CLM_BRIBE_SYATEKI_ADULT;

    if (isChild) {
        if (CUR_UPG_VALUE(UPG_BULLET_BAG) == 1)      s.getItemId = GI_BULLET_BAG_40;
        else if (CUR_UPG_VALUE(UPG_BULLET_BAG) > 1)  s.getItemId = GI_BULLET_BAG_50;
        else                                          s.getItemId = GI_RUPEE_PURPLE;
        return already ? CLM_TEXT_SYATEKI_CHILD_REPEAT : CLM_TEXT_SYATEKI_CHILD_FIRST;
    } else {
        switch (CUR_UPG_VALUE(UPG_QUIVER)) {
            case 1: s.getItemId = GI_QUIVER_40; break;
            case 2: s.getItemId = GI_QUIVER_50; break;
            default: s.getItemId = GI_RUPEE_PURPLE; break;
        }
        return already ? CLM_TEXT_SYATEKI_ADULT_REPEAT : CLM_TEXT_SYATEKI_ADULT_FIRST;
    }
}

static bool Syateki_GrantReward(Actor* actor, PlayState* play, CLMState& s) {
    if (!s.firstTime) {
        Rupees_ChangeBy(s.bribeRupees);
        return false;
    }
    if (IS_RANDO) {
        // Direct rando delivery already calls GiveItemEntryFromActor which sets up
        // the player's item-get cutscene independently. No need to wait in
        // RewardOffered — return false to go straight to Done.
        CLM_TryDirectRandoDelivery(actor, play, s);
        return false;
    }
    Actor_OfferGetItem(actor, play, s.getItemId, 2000.0f, 1000.0f);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. Bombchu Bowling (En_Bom_Bowl_Man)
// ─────────────────────────────────────────────────────────────────────────────

static bool Bowling_ShouldIntercept(Actor* actor) { return true; }

static uint16_t Bowling_ResolveTextId(Actor* actor, CLMState& s) {
    // Vanilla bowling has two distinct rewards:
    //   ITEMGETINF_11 — bomb bag (first prize slot, prize 0)
    //   ITEMGETINF_12 — heart piece (second prize slot, prize 1)
    // Progressive: first CLM visit grants the bomb bag, second grants the heart piece,
    // subsequent visits give a bribe.
    bool gotBag = Flags_GetItemGetInf(ITEMGETINF_11);
    bool gotHP  = Flags_GetItemGetInf(ITEMGETINF_12);
    bool bothDone = gotBag && gotHP;
    s.firstTime = !bothDone;
    s.bribeRupees = CLM_BRIBE_BOWLING;

    if (!gotBag) {
        // First reward path: bomb bag upgrade based on current capacity
        if (CUR_UPG_VALUE(UPG_BOMB_BAG) == 0)      s.getItemId = GI_BOMB_BAG_20;
        else if (CUR_UPG_VALUE(UPG_BOMB_BAG) == 1) s.getItemId = GI_BOMB_BAG_30;
        else if (CUR_UPG_VALUE(UPG_BOMB_BAG) == 2) s.getItemId = GI_BOMB_BAG_40;
        else                                       s.getItemId = GI_RUPEE_PURPLE;
    } else if (!gotHP) {
        // Second reward path: heart piece
        s.getItemId = GI_HEART_PIECE;
    } else {
        s.getItemId = 0;
    }

    return bothDone ? CLM_TEXT_BOWLING_REPEAT : CLM_TEXT_BOWLING_FIRST;
}

static bool Bowling_GrantReward(Actor* actor, PlayState* play, CLMState& s) {
    if (!s.firstTime) {
        Rupees_ChangeBy(s.bribeRupees);
        return false;
    }
    if (IS_RANDO) {
        // Direct rando delivery already calls GiveItemEntryFromActor which sets up
        // the player's item-get cutscene independently. No need to wait in
        // RewardOffered — return false to go straight to Done.
        CLM_TryDirectRandoDelivery(actor, play, s);
        return false;
    }
    Actor_OfferGetItem(actor, play, s.getItemId, 2000.0f, 1000.0f);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. Ingo (En_In) — special: grants Epona permanently
// ─────────────────────────────────────────────────────────────────────────────

static bool Ingo_ShouldIntercept(Actor* actor) { return true; }

static uint16_t Ingo_ResolveTextId(Actor* actor, CLMState& s) {
    bool isChild = !LINK_IS_ADULT;
    bool alreadyHasEpona = Flags_GetEventChkInf(EVENTCHKINF_EPONA_OBTAINED);
    bool talonReturned = Flags_GetEventChkInf(EVENTCHKINF_TALON_RETURNED_FROM_CASTLE);
    s.isChild = isChild;
    s.firstTime = false;
    s.getItemId = 0;
    s.bribeRupees = 0;

    if (isChild)            return CLM_TEXT_INGO_CHILD;
    if (alreadyHasEpona)    return CLM_TEXT_INGO_ALREADY;

    s.firstTime = true; // signal grantReward to set the flag
    return talonReturned ? CLM_TEXT_INGO_ADULT_POSTTALON : CLM_TEXT_INGO_ADULT_PRETALON;
}

static bool Ingo_GrantReward(Actor* actor, PlayState* play, CLMState& s) {
    if (s.firstTime) {
        // Set the "free ranch" flags: Talon returned (ranch is no longer Ingo's)
        // and Epona obtained. The entrance cutscene system also sets EPONA_OBTAINED
        // automatically when Link arrives at one of the Epona-jump entrances, but
        // setting it manually here is harmless (the table accepts EPONA_OBTAINED
        // even if already set — see z_demo.c:2198).
        Flags_SetEventChkInf(EVENTCHKINF_EPONA_OBTAINED);
        Flags_SetEventChkInf(EVENTCHKINF_TALON_RETURNED_FROM_CASTLE);
        SPDLOG_INFO("[CLM] Ingo: set EPONA_OBTAINED + TALON_RETURNED_FROM_CASTLE");

        // Random Epona-jumping-fence entrance into Hyrule Field. These 3 entrances
        // are listed in z_demo.c:70-72 as `gHyruleField{South,East,West}EponaJumpCs`
        // and trigger the Epona-jumping cutscene that drops Link into Hyrule Field
        // riding Epona. Picking randomly mimics the vanilla "exit ranch on Epona"
        // workflow but at a random side of the ranch.
        static const s16 kEponaExits[] = {
            ENTR_HYRULE_FIELD_11, // south
            ENTR_HYRULE_FIELD_12, // west
            ENTR_HYRULE_FIELD_13, // east
        };
        s16 pick = kEponaExits[Rand_Next() % 3];

        play->nextEntranceIndex = pick;
        play->transitionType = TRANS_TYPE_FADE_WHITE;
        play->transitionTrigger = TRANS_TRIGGER_START;
        gSaveContext.timerState = TIMER_STATE_OFF;
        // Do NOT set nextCutsceneIndex — keep it < 0xFFF0 so the entrance cutscene
        // table (z_demo.c:2197) fires the Epona-jump cinematic.

        SPDLOG_INFO("[CLM] Ingo: transition to Hyrule Field entrance 0x{:X} (Epona jump)", pick);
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Talon (En_Ta) — cucco game, child era, Lon Lon ranch house
// ─────────────────────────────────────────────────────────────────────────────

static bool Talon_ShouldIntercept(Actor* actor) {
    return !LINK_IS_ADULT && gPlayState->sceneNum == SCENE_LON_LON_BUILDINGS;
}

static uint16_t Talon_ResolveTextId(Actor* actor, CLMState& s) {
    bool already = Flags_GetItemGetInf(ITEMGETINF_TALON_BOTTLE);
    s.firstTime = !already;
    s.bribeRupees = 0;
    s.getItemId = already ? 0 : GI_MILK_BOTTLE;
    return already ? CLM_TEXT_TALON_ASLEEP : CLM_TEXT_TALON_FIRST;
}

static bool Talon_GrantReward(Actor* actor, PlayState* play, CLMState& s) {
    if (!s.firstTime) return false; // asleep; no reward
    if (IS_RANDO) {
        // Direct rando delivery already calls GiveItemEntryFromActor which sets up
        // the player's item-get cutscene independently. No need to wait in
        // RewardOffered — return false to go straight to Done.
        CLM_TryDirectRandoDelivery(actor, play, s);
        return false;
    }
    Actor_OfferGetItem(actor, play, s.getItemId, 2000.0f, 1000.0f);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. Adult Malon (En_Ma3) — sells Link's Cow for 100 rupees
// ─────────────────────────────────────────────────────────────────────────────

static bool Malon_ShouldIntercept(Actor* actor) { return LINK_IS_ADULT; }

static uint16_t Malon_ResolveTextId(Actor* actor, CLMState& s) {
    bool already = Flags_GetEventChkInf(EVENTCHKINF_WON_COW_IN_MALONS_RACE);
    bool canAfford = gSaveContext.rupees >= CLM_MALON_COW_PRICE;
    s.bribeRupees = 0;
    if (already) {
        s.firstTime = false;
        s.getItemId = 0;
        return CLM_TEXT_MALON_REPEAT;
    }
    if (!canAfford) {
        s.firstTime = false;
        s.getItemId = 0;
        return CLM_TEXT_MALON_BROKE;
    }
    s.firstTime = true;
    // Vanilla Malon doesn't grant an item — the cow is "given" by setting the
    // EVENTCHKINF_WON_COW_IN_MALONS_RACE flag (enables the cow to be milkable in ranch).
    s.getItemId = 0;
    return CLM_TEXT_MALON_BUY;
}

static bool Malon_GrantReward(Actor* actor, PlayState* play, CLMState& s) {
    if (s.firstTime) {
        Rupees_ChangeBy(-CLM_MALON_COW_PRICE);
        Flags_SetEventChkInf(EVENTCHKINF_WON_COW_IN_MALONS_RACE);
        SPDLOG_INFO("[CLM] Malon: cow sold for {} rupees", CLM_MALON_COW_PRICE);
    }
    return false; // flag-only, no item-get cutscene
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. Horseback Archery Gerudo (En_Ge1)
// ─────────────────────────────────────────────────────────────────────────────

static bool HBA_ShouldIntercept(Actor* actor) {
    return (actor->params & 0xFF) == GE1_TYPE_HORSEBACK_ARCHERY;
}

static uint16_t HBA_ResolveTextId(Actor* actor, CLMState& s) {
    // Vanilla HBA has two distinct score-gated rewards:
    //   1000 score → INFTABLE_190 (heart piece)
    //   1500 score → ITEMGETINF_0F (quiver upgrade)
    // Progressive: first CLM visit gives heart piece, second gives quiver,
    // subsequent visits give a bribe.
    bool gotHP    = Flags_GetInfTable(INFTABLE_190);
    bool gotQuiver = Flags_GetItemGetInf(ITEMGETINF_0F);
    bool bothDone = gotHP && gotQuiver;
    s.firstTime = !bothDone;
    s.bribeRupees = CLM_BRIBE_HBA;

    if (!gotHP) {
        s.getItemId = GI_HEART_PIECE;
    } else if (!gotQuiver) {
        switch (CUR_UPG_VALUE(UPG_QUIVER)) {
            case 1: s.getItemId = GI_QUIVER_40; break;
            case 2: s.getItemId = GI_QUIVER_50; break;
            default: s.getItemId = GI_RUPEE_PURPLE; break;
        }
    } else {
        s.getItemId = 0;
    }

    return bothDone ? CLM_TEXT_HBA_REPEAT : CLM_TEXT_HBA_FIRST;
}

static bool HBA_GrantReward(Actor* actor, PlayState* play, CLMState& s) {
    if (!s.firstTime) {
        Rupees_ChangeBy(s.bribeRupees);
        return false;
    }
    if (IS_RANDO) {
        // Direct rando delivery already calls GiveItemEntryFromActor which sets up
        // the player's item-get cutscene independently. No need to wait in
        // RewardOffered — return false to go straight to Done.
        CLM_TryDirectRandoDelivery(actor, play, s);
        return false;
    }
    Actor_OfferGetItem(actor, play, s.getItemId, 2000.0f, 1000.0f);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. Fishing Pond Owner (Fishing actor with params == 1)
// ─────────────────────────────────────────────────────────────────────────────

static bool Fishing_ShouldIntercept(Actor* actor) {
    return actor->params == 1;
}

static uint16_t Fishing_ResolveTextId(Actor* actor, CLMState& s) {
    bool isChild = !LINK_IS_ADULT;
    s32 fishHS = HIGH_SCORE(HS_FISHING);
    bool already = isChild ? (fishHS & HS_FISH_PRIZE_CHILD) : (fishHS & HS_FISH_PRIZE_ADULT);
    s.isChild = isChild;
    s.firstTime = !already;
    s.bribeRupees = CLM_BRIBE_FISHING;
    s.getItemId = already ? 0 : (isChild ? GI_HEART_PIECE : GI_SCALE_GOLDEN);
    return already ? CLM_TEXT_FISHING_REPEAT : CLM_TEXT_FISHING_FIRST;
}

static bool Fishing_GrantReward(Actor* actor, PlayState* play, CLMState& s) {
    if (!s.firstTime) {
        Rupees_ChangeBy(s.bribeRupees);
        return false;
    }
    if (IS_RANDO) {
        // Direct rando delivery already calls GiveItemEntryFromActor which sets up
        // the player's item-get cutscene independently. No need to wait in
        // RewardOffered — return false to go straight to Done.
        CLM_TryDirectRandoDelivery(actor, play, s);
        return false;
    }
    Actor_OfferGetItem(actor, play, s.getItemId, 2000.0f, 1000.0f);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. Treasure Chest Game (En_Takara_Man) — child only
// ─────────────────────────────────────────────────────────────────────────────

static bool Takara_ShouldIntercept(Actor* actor) { return !LINK_IS_ADULT; }

static uint16_t Takara_ResolveTextId(Actor* actor, CLMState& s) {
    // The treasure-chest-game's actual rando check is the heart piece reward
    // (RC_MARKET_TREASURE_CHEST_GAME_REWARD → ItemGetInf(0x1B)). The door key is
    // just an access gate. CLM grants the heart piece as the "tax" so the check
    // is rando-weighted.
    s.firstTime = !Flags_GetItemGetInf(ITEMGETINF_1B);
    s.bribeRupees = CLM_BRIBE_TAKARA;
    s.getItemId = s.firstTime ? GI_HEART_PIECE : 0;
    return s.firstTime ? CLM_TEXT_TAKARA_FIRST : CLM_TEXT_TAKARA_REPEAT;
}

static bool Takara_GrantReward(Actor* actor, PlayState* play, CLMState& s) {
    if (!s.firstTime) {
        Rupees_ChangeBy(s.bribeRupees);
        return false;
    }
    if (IS_RANDO) {
        // Direct rando delivery already calls GiveItemEntryFromActor which sets up
        // the player's item-get cutscene independently. No need to wait in
        // RewardOffered — return false to go straight to Done.
        CLM_TryDirectRandoDelivery(actor, play, s);
        return false;
    }
    Actor_OfferGetItem(actor, play, s.getItemId, 2000.0f, 1000.0f);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 9. Diving Game (En_Diving_Game) — adult, Zora's Domain
// ─────────────────────────────────────────────────────────────────────────────

static bool Diving_ShouldIntercept(Actor* actor) { return true; }

static uint16_t Diving_ResolveTextId(Actor* actor, CLMState& s) {
    bool already = Flags_GetEventChkInf(EVENTCHKINF_OBTAINED_SILVER_SCALE);
    s.firstTime = !already;
    s.bribeRupees = CLM_BRIBE_DIVING;
    s.getItemId = already ? 0 : GI_SCALE_SILVER;
    return already ? CLM_TEXT_DIVING_REPEAT : CLM_TEXT_DIVING_FIRST;
}

static bool Diving_GrantReward(Actor* actor, PlayState* play, CLMState& s) {
    if (!s.firstTime) {
        Rupees_ChangeBy(s.bribeRupees);
        return false;
    }
    if (IS_RANDO) {
        // Direct rando delivery already calls GiveItemEntryFromActor which sets up
        // the player's item-get cutscene independently. No need to wait in
        // RewardOffered — return false to go straight to Done.
        CLM_TryDirectRandoDelivery(actor, play, s);
        return false;
    }
    Actor_OfferGetItem(actor, play, s.getItemId, 2000.0f, 1000.0f);
    return true;
}

// ── Adapter table ───────────────────────────────────────────────────────────

static const CLMAdapter kAdapters[] = {
    { ACTOR_EN_SYATEKI_MAN,  Syateki_ShouldIntercept, Syateki_ResolveTextId, Syateki_GrantReward, true  },
    { ACTOR_EN_BOM_BOWL_MAN, Bowling_ShouldIntercept, Bowling_ResolveTextId, Bowling_GrantReward, true  },
    { ACTOR_EN_IN,           Ingo_ShouldIntercept,    Ingo_ResolveTextId,    Ingo_GrantReward,    true  },
    { ACTOR_EN_TA,           Talon_ShouldIntercept,   Talon_ResolveTextId,   Talon_GrantReward,   true  },
    { ACTOR_EN_MA3,          Malon_ShouldIntercept,   Malon_ResolveTextId,   Malon_GrantReward,   true  },
    { ACTOR_EN_GE1,          HBA_ShouldIntercept,     HBA_ResolveTextId,     HBA_GrantReward,     true  },
    { ACTOR_FISHING,         Fishing_ShouldIntercept, Fishing_ResolveTextId, Fishing_GrantReward, true  },
    { ACTOR_EN_TAKARA_MAN,   Takara_ShouldIntercept,  Takara_ResolveTextId,  Takara_GrantReward,  true  },
    // Diving Game: vanilla EnDivingGame_Talk has a CS-locking talk-accept branch
    // and a HandlePlayChoice handler that mismatches our EVENT-type message.
    // Skip vanilla update entirely during CLM dialogue to avoid softlock.
    { ACTOR_EN_DIVING_GAME,  Diving_ShouldIntercept,  Diving_ResolveTextId,  Diving_GrantReward,  false },
};

static const CLMAdapter* FindAdapter(int16_t actorId) {
    for (const auto& a : kAdapters) {
        if (a.actorId == actorId) return &a;
    }
    return nullptr;
}

// ── Post-accept flag setting (called after Actor_HasParent flips true) ──────

static void CLM_PostAcceptItem(Actor* actor, CLMState& s) {
    switch (s.actorId) {
        case ACTOR_EN_SYATEKI_MAN:
            if (s.isChild) {
                Flags_SetItemGetInf(ITEMGETINF_0D);
            } else if (GameInteractor_Should(VB_BE_ELIGIBLE_FOR_ADULT_SHOOTING_GAME_REWARD,
                                              (s.getItemId == GI_QUIVER_40) || (s.getItemId == GI_QUIVER_50),
                                              actor)) {
                Flags_SetItemGetInf(ITEMGETINF_0E);
            }
            break;
        case ACTOR_EN_BOM_BOWL_MAN:
            // Flag-based progression: set whichever bowling-prize flag is missing.
            // This works regardless of whether s.getItemId is the bomb bag, heart
            // piece, or a fallback purple rupee (for maxed-out players).
            if (!Flags_GetItemGetInf(ITEMGETINF_11)) {
                Flags_SetItemGetInf(ITEMGETINF_11);
            } else {
                Flags_SetItemGetInf(ITEMGETINF_12);
            }
            break;
        case ACTOR_EN_TA:
            Flags_SetItemGetInf(ITEMGETINF_TALON_BOTTLE);
            break;
        case ACTOR_EN_MA3:
            Flags_SetEventChkInf(EVENTCHKINF_WON_COW_IN_MALONS_RACE);
            break;
        case ACTOR_EN_GE1:
            if (!Flags_GetInfTable(INFTABLE_190)) {
                Flags_SetInfTable(INFTABLE_190);
            } else {
                Flags_SetItemGetInf(ITEMGETINF_0F);
            }
            break;
        case ACTOR_FISHING:
            if (s.isChild) {
                HIGH_SCORE(HS_FISHING) |= HS_FISH_PRIZE_CHILD;
            } else {
                HIGH_SCORE(HS_FISHING) |= HS_FISH_PRIZE_ADULT;
            }
            break;
        case ACTOR_EN_TAKARA_MAN:
            // ITEMGETINF_1B (=27) is the treasure-chest-game reward check (vanilla
            // heart piece for winning the chest game). Setting this flag also
            // triggers rando's OnFlagSet → queue → shuffled item drop.
            Flags_SetItemGetInf(ITEMGETINF_1B);
            break;
        case ACTOR_EN_DIVING_GAME:
            Flags_SetEventChkInf(EVENTCHKINF_OBTAINED_SILVER_SCALE);
            break;
    }
}

// ── Hijacked actor->update — runs in place of vanilla update during dialogue ──

static void CLM_HijackedUpdate(Actor* actor, PlayState* play) {
    auto it = gStates.find(actor);
    if (it == gStates.end()) return;
    auto& state = it->second;
    const CLMAdapter* adapter = FindAdapter(state.actorId);

    bool passVanilla = (adapter != nullptr) && adapter->passVanillaUpdate;

    switch (state.phase) {
        case CLMPhase::TextShowing: {
            // Pass through vanilla update so the NPC keeps animating (skel/blink/etc).
            // Vanilla Talk's textId/numTextBox checks won't match our CLM EVENT message,
            // so the inner state-machine guards no-op while text is up. Skipped for
            // actors flagged unsafe (e.g. Diving Game) — they stay in idle pose.
            if (passVanilla && state.savedUpdate != nullptr) {
                // Keep ACTOR_FLAG_TALK cleared every frame in case anything sets it
                actor->flags &= ~ACTOR_FLAG_TALK;
                state.savedUpdate(actor, play);
            }
            u8 ms = Message_GetState(&play->msgCtx);
            if (ms == TEXT_STATE_EVENT && Message_ShouldAdvance(play)) {
                Message_CloseTextbox(play);
                state.phase = CLMPhase::WaitingForClose;
            } else if (ms == TEXT_STATE_NONE) {
                // Already closed (e.g. external close)
                state.phase = CLMPhase::TextClosed;
            }
            break;
        }
        case CLMPhase::WaitingForClose: {
            if (passVanilla && state.savedUpdate != nullptr) {
                actor->flags &= ~ACTOR_FLAG_TALK;
                state.savedUpdate(actor, play);
            }
            if (Message_GetState(&play->msgCtx) == TEXT_STATE_NONE) {
                state.phase = CLMPhase::TextClosed;
            }
            break;
        }
        case CLMPhase::TextClosed: {
            // grantReward returns true only if a vanilla Actor_OfferGetItem was made
            // (and the player needs to accept it). Returns false when:
            //   - Rando intercepted via VB hook (should=false) — rando wants to give its
            //     own reward, but it triggers on the ITEMGETINF flag transition.
            //   - The reward is flag/rupee-only (Ingo, Malon, repeat-visit bribe).
            //   - There's no first-time reward (s.firstTime == false).
            bool offeredVanilla = adapter ? adapter->grantReward(actor, play, state) : false;
            SPDLOG_INFO("[CLM] TextClosed → grantReward returned offeredVanilla={} (firstTime={}, getItemId=0x{:X})",
                        offeredVanilla, state.firstTime, state.getItemId);

            // CRITICAL for rando: when vanilla offer was skipped because rando intercepted
            // (state.firstTime is true but the VB returned false), we still need to set
            // the ITEMGETINF/EVENTCHKINF flag so rando's reward delivery system fires.
            // Vanilla actors do this in their FinishPrize-equivalent state via the
            // `!GameInteractor_Should(...)` short-circuit (e.g. z_en_syateki_man.c:428).
            if (!offeredVanilla && state.firstTime) {
                SPDLOG_INFO("[CLM] Rando-intercept path: setting flag immediately for actorId=0x{:X}", state.actorId);
                CLM_PostAcceptItem(actor, state);
            }

            state.phase = offeredVanilla ? CLMPhase::RewardOffered : CLMPhase::Done;
            break;
        }
        case CLMPhase::RewardOffered: {
            if (Actor_HasParent(actor, play)) {
                CLM_PostAcceptItem(actor, state);
                actor->parent = NULL;
                state.phase = CLMPhase::Done;
            } else if (!IS_RANDO) {
                // Vanilla: re-offer until player accepts (in case they walked away briefly).
                Actor_OfferGetItem(actor, play, state.getItemId, 2000.0f, 1000.0f);
            }
            // Rando: GiveItemEntryFromActor already set up the cutscene; player is locked in.
            break;
        }
        case CLMPhase::Done: {
            if (state.savedUpdate != nullptr) {
                actor->update = state.savedUpdate;
            }
            // Safety net: some actors (e.g. Diving Game) call Player_SetCsAction-
            // WithHaltedActors which sets player->csAction = N and halts actors.
            // If anything in vanilla flow set this, fully release it so the
            // player isn't softlocked.
            Player* player = GET_PLAYER(play);
            if (player != nullptr && player->csAction != 0) {
                SPDLOG_INFO("[CLM] Done: clearing residual csAction={}", player->csAction);
                player->csAction = 0;
                player->csActor = nullptr;
                player->cv.haltActorsDuringCsAction = false;
            }

            // Diving Game: vanilla flow may have set actionFunc to HandlePlayChoice
            // before our intercept, leaving the actor stuck in a state that doesn't
            // offer further talks. Restore the snapshot so future interactions work.
            if (state.actorId == ACTOR_EN_DIVING_GAME) {
                auto safeIt = gDivingSafeActionFunc.find(actor);
                if (safeIt != gDivingSafeActionFunc.end()) {
                    EnDivingGame* dg = reinterpret_cast<EnDivingGame*>(actor);
                    dg->actionFunc = safeIt->second;
                    dg->state = ENDIVINGGAME_STATE_NOTPLAYING;
                    dg->phase = 0;
                    dg->unk_292 = 0; // TEXT_STATE_NONE
                    SPDLOG_INFO("[CLM] Done: restored Diving Game actionFunc to safe snapshot");
                }
            }
            gStates.erase(it);
            break;
        }
    }
}

// ── Global OnOpenText hook: the speak-intercept point ───────────────────────

static void CLM_OnAnyTextOpens(uint16_t* textId, bool* loadFromMessageTable) {
    if (!CLM_IsWorn()) return;

    PlayState* play = gPlayState;
    if (play == nullptr) return;

    Actor* talkActor = GET_PLAYER(play)->talkActor;
    if (talkActor == nullptr) return;

    const CLMAdapter* adapter = FindAdapter(talkActor->id);
    if (adapter == nullptr) return;
    if (!adapter->shouldIntercept(talkActor)) return;

    // Don't double-intercept if already hijacked
    if (gStates.find(talkActor) != gStates.end()) return;

    SPDLOG_INFO("[CLM] Intercepting talk: actorId=0x{:X}, vanilla textId=0x{:X}", talkActor->id, *textId);

    auto& state = gStates[talkActor];
    state.phase = CLMPhase::TextShowing;
    state.actorId = talkActor->id;
    state.savedUpdate = talkActor->update;

    *textId = adapter->resolveTextId(talkActor, state);
    SPDLOG_INFO("[CLM] Swapped to CLM textId=0x{:X} (firstTime={}, item=0x{:X})", *textId, state.firstTime, state.getItemId);

    // Clear ACTOR_FLAG_TALK so vanilla actor update doesn't see the talk request
    // and enter its talk-accept branch (which for some actors like Diving Game
    // calls Player_SetCsActionWithHaltedActors and locks the player). Our CLM
    // dialog flow handles the talk entirely on its own.
    talkActor->flags &= ~ACTOR_FLAG_TALK;

    // If vanilla update already ran THIS frame (before my hook fired) and set
    // a CS action that would lock the player, release it now so the player can
    // advance our CLM dialog freely.
    Player* player = GET_PLAYER(play);
    if (player != nullptr && player->csAction != 0) {
        SPDLOG_INFO("[CLM] OnOpenText: clearing pre-existing csAction={} on intercept", player->csAction);
        player->csAction = 0;
        player->csActor = nullptr;
        player->cv.haltActorsDuringCsAction = false;
    }

    talkActor->update = CLM_HijackedUpdate;
}

// ── OnOpenText handlers: build each custom CLM message ──────────────────────

#define CLM_BUILD_MSG(name, body)                                                      \
    static void name(uint16_t* textId, bool* loadFromMessageTable) {                   \
        CustomMessage msg = CustomMessage(body);                                       \
        msg.AutoFormat();                                                              \
        msg.LoadIntoFont();                                                            \
        *loadFromMessageTable = false;                                                 \
    }

CLM_BUILD_MSG(BuildSyatekiChildFirst,
    "Oh! A-a royal inspector?!^"
    "Kid, you came for the weekly tally?^"
    "Business has been slow -- only rats and crows want to test their aim these days.^"
    "Here, take this as tribute. Tell His Majesty I'm behind on... paperwork.")
CLM_BUILD_MSG(BuildSyatekiChildRepeat,
    "Oh... back again, inspector?^"
    "Business is still slow, sir. Here, take a little something for your trouble...")
CLM_BUILD_MSG(BuildSyatekiAdultFirst,
    "Ah! The King's tax man! Welcome, welcome, good sir!^"
    "Great King Ganondorf's patrols have been so... thorough this moon.^"
    "Truly, a golden age for commerce!^"
    "Take this quiver -- no, PLEASE, I insist! A loyal subject is always eager to contribute!^"
    "L-long may the King reign!")
CLM_BUILD_MSG(BuildSyatekiAdultRepeat,
    "Please send the Great King my warmest regards, good sir!^"
    "Take this humble offering -- a loyal subject's duty!")

CLM_BUILD_MSG(BuildBowlingFirst,
    "Well, WELL... a tax collector? For the KING himself?^"
    "My, my -- those royal robes must hide a very... generous purse, don't they?^"
    "Come, sit closer. A man of your means deserves the VIP treatment.^"
    "Take this little prize -- on the house. Next visit, you bring me something shiny.")
CLM_BUILD_MSG(BuildBowlingRepeat,
    "Back again, handsome? Still no jewelry? Tsk, tsk...^"
    "Here, take a few rupees and run along now. I'm a busy woman.")

CLM_BUILD_MSG(BuildIngoChild,
    "That face...^"
    "It's so familiar to me, in fact, it looks like me, but with a great depression.^"
    "Like someone who has had dreams, but couldn't reach them because...^"
    "Kid, take that off, please!^"
    "I can't focus on work thinking about that!")
CLM_BUILD_MSG(BuildIngoAdultPreTalon,
    "That-!^"
    "That face, it's... it's me! But...^"
    "...No, it's my inner self.^"
    "I see it so clearly, I thought taking this ranch would bring me joy, but...^"
    "Why do I still feel...sad?^"
    "...Talon, he was a lazy bum, but, he was also a friend.^"
    "Yes, I can see it all so clearly.^"
    "Kid, you have shown me the error of my ways, now I must make things right.^"
    "I can't offer much, but I will allow you to take the red horse.^"
    "Actually, I was training it for Ganondorf, but Malon spoke highly of you.^"
    "I entrust you to take good care of her.^"
    "And if Talon comes back, he can have the ranch, I accept my role.")
CLM_BUILD_MSG(BuildIngoAdultPostTalon,
    "That mask...^"
    "Yes, that was me mere moments ago.^"
    "Talon made sure I learned my lesson, and not just through words.^"
    "Though, that's not the entire truth...^"
    "I was feeling like that mask even before he came back.^"
    "I can't understand the feeling, but, I must push past it.^"
    "You don't have Malon's song, do you?^"
    "She has taken a liking to you, kid, so...^"
    "With their permission, you can have her horse.^"
    "You'd just have to find the horse yourself if you lose her.")
CLM_BUILD_MSG(BuildIngoAlready,
    "...yes, the horse is yours, kid. Take her.")

CLM_BUILD_MSG(BuildTalonFirst,
    "Hur hur hur... a tax man? For the cuccos?^"
    "Well I'll be -- they're finally regulating poultry. 'Bout time...^"
    "Tell ya what, pardner. Take this bottle o' Lon Lon milk. Call it a... henhouse health fee, or somethin'.^"
    "Now excuse me, I was in the middle of a fine nap...")
CLM_BUILD_MSG(BuildTalonAsleep,
    "...zzz... zzz... hur hur... cuccos...")

CLM_BUILD_MSG(BuildMalonBuy,
    "Pffft -- hahahahaha!^"
    "Fairy boy? Is that YOU under there?!^"
    "A tax collector? With those skinny arms? Oh goddesses, I'm gonna cry laughing...^"
    "Okay, okay -- tell you what, 'tax man.' Just for the laughs...^"
    "...How about I sell you a cow? Yeah, you heard me. A real live cow.^"
    "100 rupees. And no haggling with that face.^"
    "Heeheehee. Pleasure doing business with you, 'officer.'")
CLM_BUILD_MSG(BuildMalonBroke,
    "Pffft -- hahahaha!^"
    "Fairy boy, is that YOU?^"
    "A 'tax collector' who's BROKE? Oh that's the funniest thing I've heard all year.^"
    "Come back when you've actually got coin, eh?")
CLM_BUILD_MSG(BuildMalonRepeat,
    "Already sold you one, fairy boy! Now shoo!")

CLM_BUILD_MSG(BuildHBAFirst,
    "HOW did you get past the guards?!^"
    "No men are allowed in this fortress -- ESPECIALLY not HYLIAN ones!^"
    "...Of course. Another tax collector. Lord Ganondorf bleeds his OWN people dry now.^"
    "His 'archery tariff' is strangling the training program. We used to rule this desert -- now we're just accountants for his crown.^"
    "FINE. Take the prize. And tell him every sister here curses his name.")
CLM_BUILD_MSG(BuildHBARepeat,
    "Still here? Walk out before I change my mind.^"
    "Take a few rupees. Don't come back.")

CLM_BUILD_MSG(BuildFishingFirst,
    "Eh? A tax man? Out here? You walked ALL this way in those boots?^"
    "Listen, between you and me, the pond's been pretty dry since the King changed...^"
    "Take this and say you inspected. Nothin's biting anyway.")
CLM_BUILD_MSG(BuildFishingRepeat,
    "Still nothin' bitin'. Beat it.^"
    "Here -- a few coins. Tell the King I'm cooperatin'.")

CLM_BUILD_MSG(BuildTakaraFirst,
    "Shhh! Shhh! Keep it DOWN! A tax collector?! Here?!^"
    "Look, officially I run a 'children's entertainment venue.' Off the books it's... less family-friendly.^"
    "Here -- the prize from my best chest. Consider the paperwork filed. And you never saw me, understood?")
CLM_BUILD_MSG(BuildTakaraRepeat,
    "Still clean! Nothin' to audit! Take some coin and GO!")

CLM_BUILD_MSG(BuildDivingFirst,
    "A Hyrulean tax collector? In Zora waters?^"
    "...Our treaty with the surface throne is clear -- Zora's Domain pays in fish, not in rupees.^"
    "But the surface kings always want more coin, don't they?^"
    "Take this scale. Tell your king the fountain was inspected and found compliant. Tell him NOTHING of the rest.")
CLM_BUILD_MSG(BuildDivingRepeat,
    "Still compliant. Leave the fountain be.^"
    "A small token for your troubles, collector.")

// ── Diving Game actionFunc snapshot ─────────────────────────────────────────
//
// Vanilla EnDivingGame_Talk transitions actionFunc to EnDivingGame_HandlePlayChoice
// when ProcessTalkRequest fires. After CLM bypasses the dialog, vanilla can be
// left in HandlePlayChoice indefinitely — actor stops offering talks. We snapshot
// the safe Talk actionFunc each frame the actor is idle, then restore on Done.

static void CLM_OnDivingActorUpdate(void* actorRef) {
    EnDivingGame* dg = static_cast<EnDivingGame*>(actorRef);
    Player* player = (gPlayState != nullptr) ? GET_PLAYER(gPlayState) : nullptr;
    if (player == nullptr) return;
    // Skip if dialog is happening or our CLM is intercepting — actionFunc is
    // probably HandlePlayChoice or a transient state we don't want to capture.
    if (player->stateFlags1 & PLAYER_STATE1_TALKING) return;
    if (gStates.find(&dg->actor) != gStates.end()) return;
    // Skip if state isn't NOTPLAYING (could be in minigame)
    if (dg->state != ENDIVINGGAME_STATE_NOTPLAYING) return;
    // Skip if a talk request is mid-flight
    if (dg->actor.flags & ACTOR_FLAG_TALK) return;

    gDivingSafeActionFunc[&dg->actor] = dg->actionFunc;
}

// ── Scene change cleanup (avoid dangling actor* in gStates after scene reload) ──

static void CLM_OnSceneInit(int16_t sceneNum) {
    if (!gStates.empty()) {
        SPDLOG_INFO("[CLM] Clearing {} stale CLM state entries on scene change", gStates.size());
        gStates.clear();
    }
    gDivingSafeActionFunc.clear();
}

// ── Registration ────────────────────────────────────────────────────────────

static void CLM_RegisterHooks() {
    SPDLOG_INFO("[CLM] CLM_RegisterHooks() — Circus Leader's Mask hooks registering");

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnOpenText>(CLM_OnAnyTextOpens);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(CLM_OnSceneInit);
    // Snapshot Diving Game's safe actionFunc each frame for post-CLM recovery
    GameInteractor::Instance->RegisterGameHookForID<GameInteractor::OnActorUpdate>(
        ACTOR_EN_DIVING_GAME, CLM_OnDivingActorUpdate);

    auto reg = [](uint16_t id, void (*fn)(uint16_t*, bool*)) {
        GameInteractor::Instance->RegisterGameHookForID<GameInteractor::OnOpenText>(id, fn);
    };

    reg(CLM_TEXT_SYATEKI_CHILD_FIRST,  BuildSyatekiChildFirst);
    reg(CLM_TEXT_SYATEKI_CHILD_REPEAT, BuildSyatekiChildRepeat);
    reg(CLM_TEXT_SYATEKI_ADULT_FIRST,  BuildSyatekiAdultFirst);
    reg(CLM_TEXT_SYATEKI_ADULT_REPEAT, BuildSyatekiAdultRepeat);
    reg(CLM_TEXT_BOWLING_FIRST,        BuildBowlingFirst);
    reg(CLM_TEXT_BOWLING_REPEAT,       BuildBowlingRepeat);
    reg(CLM_TEXT_INGO_CHILD,           BuildIngoChild);
    reg(CLM_TEXT_INGO_ADULT_PRETALON,  BuildIngoAdultPreTalon);
    reg(CLM_TEXT_INGO_ADULT_POSTTALON, BuildIngoAdultPostTalon);
    reg(CLM_TEXT_INGO_ALREADY,         BuildIngoAlready);
    reg(CLM_TEXT_TALON_FIRST,          BuildTalonFirst);
    reg(CLM_TEXT_TALON_ASLEEP,         BuildTalonAsleep);
    reg(CLM_TEXT_MALON_BUY,            BuildMalonBuy);
    reg(CLM_TEXT_MALON_BROKE,          BuildMalonBroke);
    reg(CLM_TEXT_MALON_REPEAT,         BuildMalonRepeat);
    reg(CLM_TEXT_HBA_FIRST,            BuildHBAFirst);
    reg(CLM_TEXT_HBA_REPEAT,           BuildHBARepeat);
    reg(CLM_TEXT_FISHING_FIRST,        BuildFishingFirst);
    reg(CLM_TEXT_FISHING_REPEAT,       BuildFishingRepeat);
    reg(CLM_TEXT_TAKARA_FIRST,         BuildTakaraFirst);
    reg(CLM_TEXT_TAKARA_REPEAT,        BuildTakaraRepeat);
    reg(CLM_TEXT_DIVING_FIRST,         BuildDivingFirst);
    reg(CLM_TEXT_DIVING_REPEAT,        BuildDivingRepeat);

    SPDLOG_INFO("[CLM] hooks registered OK ({} adapters)", sizeof(kAdapters) / sizeof(kAdapters[0]));
}

static RegisterShipInitFunc initFunc(CLM_RegisterHooks);
