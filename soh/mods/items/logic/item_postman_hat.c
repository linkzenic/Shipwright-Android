/**
 * item_postman_hat.c - Postman's Hat (Fast Travel via Mailboxes)
 *
 * Trigger:
 *   Wear Postman's Hat + press B while in overworld → open mailbox kaleido.
 *
 * Features:
 *   - 7 mailboxes placed across overworld (drawn as prop on POLY_OPA per scene).
 *   - Mailboxes unlock once the player walks within 150 units (unlock-on-visit).
 *   - Warps player to the selected mailbox via RESPAWN_MODE_TOP + TRANS_TYPE_FADE_BLACK_FAST.
 *   - Mail Dash: brief white streak overlay on exit, engine handles the fade.
 */

#include "z64.h"
#include "item_postman_hat.h"
#include "../custom_items.h"
#include "../helpers/equip_helper.h"
#include "../helpers/mailbox_actor.h"
#include "../../extended_inventory.h" // For SLOT_MM_MASK_POSTMAN
#include "macros.h"
#include "functions.h"
#include "variables.h"

extern Gfx* ResourceMgr_LoadGfxByName(const char* path);

// Defined in mm_mask_wear.cpp — returns the currently-worn MM mask item id
// (ITEM_NONE if none). Declared extern here to avoid pulling in the C++ header.
extern s32 TransformMasks_WearGetCurrent(void);

// True when the player owns the Postman's Hat (MM mask slot in extended inv).
static s32 PostmanHat_PlayerOwnsHat(void) {
    return gSaveContext.inventory.items[SLOT_MM_MASK_POSTMAN] == ITEM_MM_MASK_POSTMAN;
}

// True when the player is currently wearing the Postman's Hat on their head.
// Ownership alone is not enough — the hat must be equipped as a worn mask.
static s32 PostmanHat_PlayerIsWearingHat(void) {
    return TransformMasks_WearGetCurrent() == ITEM_MM_MASK_POSTMAN;
}

// ============================================================
// Mailbox table — 7 destinations
// ============================================================

const PostmanMailboxPoint sMailboxTable[POSTMAN_MAILBOX_COUNT] = {
    // #0 Kokiri Forest — near Mido's house
    {
        SCENE_KOKIRI_FOREST,
        -1,
        ENTR_KOKIRI_FOREST_0,
        { -1441.042f, -76.593f, -175.555f },
        16774,
        0,
        73, -12,
        "Kokiri Forest",
    },
    // #1 Market — in front of the fountain (day + night)
    {
        SCENE_MARKET_DAY,
        SCENE_MARKET_NIGHT,
        ENTR_MARKET_SOUTH_EXIT,
        { -4.156f, 0.0f, 124.724f },
        0,
        0,
        14, 4,
        "Hyrule Castle Town",
    },
    // #2 Kakariko Village — near the main gate guard
    {
        SCENE_KAKARIKO_VILLAGE,
        -1,
        ENTR_KAKARIKO_VILLAGE_FRONT_GATE,
        { -2162.320f, 138.0f, 1158.205f },
        -21923,
        0,
        38, 15,
        "Kakariko Village",
    },
    // #3 Lon Lon Ranch — entrance
    {
        SCENE_LON_LON_RANCH,
        -1,
        ENTR_LON_LON_RANCH_ENTRANCE,
        { 986.0f, 0.0f, -3376.015f },
        -16291,
        0,
        10, -15,
        "Lon Lon Ranch",
    },
    // #4 Death Mountain Trail
    {
        SCENE_DEATH_MOUNTAIN_TRAIL,
        -1,
        ENTR_DEATH_MOUNTAIN_TRAIL_BOTTOM_EXIT,
        { -540.559f, 1194.025f, -1858.751f },
        8402,
        0,
        35, 44,
        "Death Mtn Trail",
    },
    // #5 Zora's River
    {
        SCENE_ZORAS_RIVER,
        -1,
        ENTR_ZORAS_RIVER_WEST_EXIT,
        { 4095.814f, 960.0f, -1684.251f },
        -203,
        0,
        78, 18,
        "Zora's River",
    },
    // #6 Gerudo Valley — before the bridge
    {
        SCENE_GERUDO_VALLEY,
        -1,
        ENTR_GERUDO_VALLEY_EAST_EXIT,
        { 427.205f, 36.000f, 7.694f },
        16342,
        0,
        -51, 10,
        "Gerudo Valley",
    },
};

// ============================================================
// Unlock state — bitfield in gCustomItemState.postmanMailboxesVisited
// ============================================================

s32 PostmanHat_IsMailboxUnlocked(s32 idx) {
    if (idx < 0 || idx >= POSTMAN_MAILBOX_COUNT)
        return 0;
    return (gCustomItemState.postmanMailboxesVisited & (1 << idx)) != 0;
}

s32 PostmanHat_GetUnlockedCount(void) {
    s32 count = 0;
    for (s32 i = 0; i < POSTMAN_MAILBOX_COUNT; i++) {
        if (PostmanHat_IsMailboxUnlocked(i))
            count++;
    }
    return count;
}

static s32 PostmanHat_MailboxInScene(const PostmanMailboxPoint* m, s16 sceneNum) {
    return (m->sceneId == sceneNum) || (m->altSceneId >= 0 && m->altSceneId == sceneNum);
}

// XZ-plane distance check (ignores Y so multi-level scenes still unlock reliably)
static s32 PostmanHat_ProximityUnlock(Player* p, PlayState* play) {
    s32 unlockedThisFrame = 0;
    for (s32 i = 0; i < POSTMAN_MAILBOX_COUNT; i++) {
        const PostmanMailboxPoint* m = &sMailboxTable[i];
        if (!PostmanHat_MailboxInScene(m, play->sceneNum))
            continue;
        if (gCustomItemState.postmanMailboxesVisited & (1 << i))
            continue;
        f32 dx = p->actor.world.pos.x - m->pos.x;
        f32 dz = p->actor.world.pos.z - m->pos.z;
        if ((dx * dx) + (dz * dz) <= (150.0f * 150.0f)) {
            gCustomItemState.postmanMailboxesVisited |= (1 << i);
            unlockedThisFrame = 1;
        }
    }
    return unlockedThisFrame;
}

// Interaction radius/facing detection was moved into Mailbox_Update
// (mailbox_actor.c) where the actor offers a SPEAK prompt via func_8002F2CC
// and catches the accept with Actor_ProcessTalkRequest.

// ============================================================
// Warp execution
// ============================================================

static void PostmanHat_TriggerWarp(PlayState* play, s8 destIdx) {
    const PostmanMailboxPoint* dest = &sMailboxTable[destIdx];

    play->nextEntranceIndex = dest->entranceIndex;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_FADE_BLACK_FAST;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK_FAST;

    // Same respawn pattern as Minish Cap / Farore's Wind
    gSaveContext.respawn[RESPAWN_MODE_TOP].entranceIndex = dest->entranceIndex;
    gSaveContext.respawn[RESPAWN_MODE_TOP].pos.x = dest->pos.x;
    gSaveContext.respawn[RESPAWN_MODE_TOP].pos.y = dest->pos.y;
    gSaveContext.respawn[RESPAWN_MODE_TOP].pos.z = dest->pos.z;
    gSaveContext.respawn[RESPAWN_MODE_TOP].yaw = dest->yaw;
    gSaveContext.respawn[RESPAWN_MODE_TOP].playerParams = 0xDFF;
    gSaveContext.respawn[RESPAWN_MODE_TOP].roomIndex = dest->roomIndex;
    gSaveContext.respawnFlag = 3;
}

// Opens the kaleido mailbox map. Bails on unsafe state to avoid softlocks:
// cutscene / transition / pause / game-over / other warp-mode / mid-dash.
void PostmanHat_TryTriggerWarpMode(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;

    if (!PostmanHat_PlayerOwnsHat() || !PostmanHat_PlayerIsWearingHat())
        return;
    if (pauseCtx->state != 0 || pauseCtx->debugState != 0)
        return;
    if (play->transitionTrigger != TRANS_TRIGGER_OFF)
        return;
    if (play->gameOverCtx.state != GAMEOVER_INACTIVE)
        return;
    if (Player_InCsMode(play))
        return;
    if (play->msgCtx.msgMode != MSGMODE_NONE)
        return;
    if (gCustomItemState.minishCapWarpMode || gCustomItemState.postmanHatWarpMode)
        return;
    if (gCustomItemState.postmanHatDashing || gCustomItemState.postmanHatArriving)
        return;
    if (PostmanHat_GetUnlockedCount() == 0) {
        Audio_PlaySoundGeneral(NA_SE_SY_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    gCustomItemState.postmanHatWarpMode = 1;
    gCustomItemState.postmanHatConfirmed = 0;
    gCustomItemState.postmanHatDestIdx = -1;
    gCustomItemState.postmanHatCursorIdx = 0;
    for (s32 i = 0; i < POSTMAN_MAILBOX_COUNT; i++) {
        if (PostmanHat_IsMailboxUnlocked(i)) {
            gCustomItemState.postmanHatCursorIdx = i;
            break;
        }
    }

    // Skip input on the first kaleido frame so the A press that opened the
    // menu (from the mailbox actor's talk-accept) doesn't leak through and
    // instantly confirm a destination. Great Fairy Mask uses the same pattern
    // (mm_mask_wear.cpp: sGreatFairyInputSkip).
    gCustomItemState.postmanHatInputSkip = 1;
    pauseCtx->state = 1;

    Audio_PlaySoundGeneral(NA_SE_SY_WIN_OPEN, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

// ============================================================
// Per-frame state machine (always called — also handles unlock-on-visit)
// ============================================================

#define MAIL_DASH_PRE_FRAMES 10

// Scene-load tracking for per-scene mailbox spawning.
//
// We must re-spawn mailboxes on EVERY scene load, including warping back to
// the same scene — because the scene transition rebuilds the PlayState from
// scratch and all our previously-spawned actors are gone. Checking just
// `sceneNum != last` misses the same-scene reload case.
//
// Trick: `play->state.frames` is part of the GameState and is re-initialized
// to 0 every time a fresh PlayState is constructed (i.e. on every scene
// load). If the frame counter ever goes backwards between two calls, we
// know a scene transition happened — even if sceneNum is unchanged.
static s16 sPostmanLastSceneSpawned = -1;
static u32 sPostmanLastFrameCount = 0;

static void PostmanHat_SpawnMailboxesForScene(PlayState* play) {
    for (s32 i = 0; i < POSTMAN_MAILBOX_COUNT; i++) {
        const PostmanMailboxPoint* m = &sMailboxTable[i];
        if (PostmanHat_MailboxInScene(m, play->sceneNum)) {
            Mailbox_Spawn(play, &m->pos, m->yaw, i);
        }
    }
}

void Handle_PostmanHat(Player* p, PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;

    // Detect a scene load as EITHER a sceneNum change OR a frame-counter
    // rewind (fresh PlayState → frames back to 0, catches same-scene warps).
    s32 sceneLoaded = (play->sceneNum != sPostmanLastSceneSpawned) ||
                      (play->state.frames < sPostmanLastFrameCount);
    if (sceneLoaded) {
        sPostmanLastSceneSpawned = play->sceneNum;
        PostmanHat_SpawnMailboxesForScene(play);
    }
    sPostmanLastFrameCount = play->state.frames;

    if (PostmanHat_ProximityUnlock(p, play)) {
        Audio_PlaySoundGeneral(NA_SE_SY_GET_ITEM, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }

    if (gCustomItemState.postmanHatArriving) {
        if (gCustomItemState.postmanHatTransitionTimer > 0) {
            gCustomItemState.postmanHatTransitionTimer--;
        } else {
            gCustomItemState.postmanHatArriving = 0;
        }
        return;
    }

    if (gCustomItemState.postmanHatDashing) {
        if (gCustomItemState.postmanHatTransitionTimer > 0) {
            gCustomItemState.postmanHatTransitionTimer--;
        } else {
            s8 destIdx = gCustomItemState.postmanHatDestIdx;
            gCustomItemState.postmanHatDashing = 0;
            gCustomItemState.postmanHatDestIdx = -1;
            gCustomItemState.postmanHatArriving = 1;
            gCustomItemState.postmanHatTransitionTimer = 20;
            if (destIdx >= 0 && destIdx < POSTMAN_MAILBOX_COUNT) {
                PostmanHat_TriggerWarp(play, destIdx);
            }
        }
        return;
    }

    if (gCustomItemState.postmanHatConfirmed && pauseCtx->state == 0) {
        gCustomItemState.postmanHatConfirmed = 0;
        gCustomItemState.postmanHatWarpMode = 0;
        gCustomItemState.postmanHatDashing = 1;
        gCustomItemState.postmanHatTransitionTimer = MAIL_DASH_PRE_FRAMES;
    }

    // A-press interaction is handled by the mailbox actor itself
    // (see mailbox_actor.c). No floating detection here.
}

// ============================================================
// In-world mailbox draw — replaced by the mailbox actor
// (soh/mods/items/helpers/mailbox_actor.c). The actor draws itself with
// correct segment bindings and carries its own collider + A-press handler.
// MailboxDrawer_DrawAllForScene is kept as a no-op so the existing
// call in z_play.c can be removed in a later pass without breaking
// intermediate builds.
// ============================================================

void MailboxDrawer_DrawAllForScene(PlayState* play) {
    (void)play;
}

// Unity-include the kaleido + mailbox actor bodies at the END of this file.
// Placing them at the tail keeps their local macros and statics out of
// earlier unity includes. This file has no further body after them.
#include "../helpers/postman_kaleido.c"
#include "../helpers/mailbox_actor.c"
