/**
 * item_minish_cap.c - The Minish Cap (Fast Travel via Pod Soils)
 *
 * Controls:
 *   C Button: Open custom Minish Kaleido warp map
 *
 * Features:
 *   - Opens custom kaleido with full world map + area box indicators
 *   - Navigate between 9 pod soil locations with analog stick
 *   - Unlocked soils shown in color + name, locked in grey + skulltula
 *   - Warps player to the selected pod soil position
 *   - Pod soil is "unlocked" when its Gold Skulltula has been killed
 *   - Only works in overworld scenes (not dungeons)
 */

#include "z64.h"
#include "item_minish_cap.h"
#include "../custom_items.h"
#include "../helpers/equip_helper.h"
#include "../helpers/minish_kaleido.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"

// Pod Soil table: 9 confirmed ObjMakekinsuta placements in OOT (all overworld)
// Positions and GS flags extracted from OOT decomp scene actor lists
// areaIdx = world map area index used for area box position/texture in minish_kaleido.c
const PodSoilWarpPoint sPodSoilTable[POD_SOIL_COUNT] = {
    // #0 Kokiri Forest (spot04 room_0) - params 0x4D01
    {
        SCENE_KOKIRI_FOREST,
        ENTR_KOKIRI_FOREST_0,
        { 1190.0f, 0.0f, -480.0f },
        0x0000,
        12,   // gsGroup (EN_SW decrements by 1: 0x0C)
        0x01, // gsMask
        0,    // roomIndex (spot04_room_0)
        4,    // areaIdx (Kokiri Forest)
        "Kokiri Forest",
    },
    // #1 Lost Woods - Bridge area (spot10 room_5) - params 0x4E01
    {
        SCENE_LOST_WOODS,
        ENTR_LOST_WOODS_SOUTH_EXIT,
        { -1220.0f, 0.0f, 935.0f },
        0x0000,
        13,   // gsGroup (EN_SW decrements by 1: 0x0D)
        0x01, // gsMask
        5,    // roomIndex (spot10_room_5)
        10,   // areaIdx (Lost Woods)
        "Lost Woods",
    },
    // #2 Lost Woods - Forest Stage area (spot10 room_6) - params 0x4E02
    {
        SCENE_LOST_WOODS,
        ENTR_LOST_WOODS_SOUTH_EXIT,
        { 610.0f, 0.0f, -1770.0f },
        0x0000,
        13,   // gsGroup (EN_SW decrements by 1: 0x0D)
        0x02, // gsMask
        6,    // roomIndex (spot10_room_6)
        5,    // areaIdx (Sacred Forest Meadow)
        "Sacred Forest Meadow",
    },
    // #3 Lake Hylia (spot06 room_0) - params 0x5301
    {
        SCENE_LAKE_HYLIA,
        ENTR_LAKE_HYLIA_NORTH_EXIT,
        { -2602.0f, -1033.0f, 3617.0f },
        0x0000,
        18,   // gsGroup (EN_SW decrements by 1: 0x12)
        0x01, // gsMask
        0,    // roomIndex (spot06_room_0)
        6,    // areaIdx (Lake Hylia)
        "Lake Hylia",
    },
    // #4 Graveyard (spot02 room_1) - params 0x5101
    {
        SCENE_GRAVEYARD,
        ENTR_GRAVEYARD_ENTRANCE,
        { -715.0f, 120.0f, -340.0f },
        0x0000,
        16,   // gsGroup (EN_SW decrements by 1: 0x10)
        0x01, // gsMask
        1,    // roomIndex (spot02_room_1)
        2,    // areaIdx (Graveyard)
        "Graveyard",
    },
    // #5 Death Mountain Trail (spot16 room_0) - params 0x5002
    {
        SCENE_DEATH_MOUNTAIN_TRAIL,
        ENTR_DEATH_MOUNTAIN_TRAIL_BOTTOM_EXIT,
        { -1610.0f, 677.0f, -735.0f },
        0x0000,
        15,   // gsGroup (EN_SW decrements by 1: 0x0F)
        0x02, // gsMask
        0,    // roomIndex (spot16_room_0)
        16,   // areaIdx (Death Mountain Trail)
        "Death Mtn Trail",
    },
    // #6 Death Mountain Crater (spot17 room_1) - params 0x5001
    {
        SCENE_DEATH_MOUNTAIN_CRATER,
        ENTR_DEATH_MOUNTAIN_CRATER_UPPER_EXIT,
        { -127.0f, 421.0f, -168.0f },
        0x0000,
        15,   // gsGroup (EN_SW decrements by 1: 0x0F)
        0x01, // gsMask
        1,    // roomIndex (spot17_room_1)
        17,   // areaIdx (Death Mountain Crater)
        "Death Mtn Crater",
    },
    // #7 Desert Colossus (spot11 room_0) - params 0x5601
    {
        SCENE_DESERT_COLOSSUS,
        ENTR_DESERT_COLOSSUS_EAST_EXIT,
        { -1330.0f, 8.0f, 290.0f },
        0x0000,
        21,   // gsGroup (EN_SW decrements by 1: 0x15)
        0x01, // gsMask
        0,    // roomIndex (spot11_room_0)
        11,   // areaIdx (Desert Colossus)
        "Desert Colossus",
    },
    // #8 Gerudo Valley (spot09 room_0) - params 0x5401
    {
        SCENE_GERUDO_VALLEY,
        ENTR_GERUDO_VALLEY_EAST_EXIT,
        { -515.0f, -2051.0f, 110.0f },
        0x0000,
        19,   // gsGroup (EN_SW decrements by 1: 0x13)
        0x01, // gsMask
        0,    // roomIndex (spot09_room_0)
        9,    // areaIdx (Gerudo Valley)
        "Gerudo Valley",
    },
};

s32 MinishCap_IsPodSoilUnlocked(s32 idx) {
    if (idx < 0 || idx >= POD_SOIL_COUNT)
        return 0;
    return (GET_GS_FLAGS(sPodSoilTable[idx].gsGroup) & sPodSoilTable[idx].gsMask) != 0;
}

s32 MinishCap_GetUnlockedCount(void) {
    s32 count = 0;
    for (s32 i = 0; i < POD_SOIL_COUNT; i++) {
        if (MinishCap_IsPodSoilUnlocked(i))
            count++;
    }
    return count;
}

// Check if player is within range of any unlocked pod soil in the current scene
static s32 MinishCap_IsNearPodSoil(Player* p, PlayState* play) {
    for (s32 i = 0; i < POD_SOIL_COUNT; i++) {
        if (sPodSoilTable[i].sceneId != play->sceneNum)
            continue;
        if (!MinishCap_IsPodSoilUnlocked(i))
            continue;
        f32 dx = p->actor.world.pos.x - sPodSoilTable[i].pos.x;
        f32 dy = p->actor.world.pos.y - sPodSoilTable[i].pos.y;
        f32 dz = p->actor.world.pos.z - sPodSoilTable[i].pos.z;
        f32 distSq = (dx * dx) + (dy * dy) + (dz * dz);
        if (distSq <= (50.0f * 50.0f))
            return 1;
    }
    return 0;
}

void Player_InitMinishCapIA(PlayState* play, Player* this) {
    // No special init needed
}

// Scale constants for shrink/grow animation
#define MINISH_SCALE_NORMAL 0.01f // Player's normal scale
#define MINISH_SCALE_TINY 0.0005f // 5% of normal (starting size on arrival)
#define MINISH_SCALE_RATE 0.0005f // Step per frame (~19 frames for full transition)

static void MinishCap_TriggerWarp(PlayState* play, s8 destIdx) {
    const PodSoilWarpPoint* dest = &sPodSoilTable[destIdx];

    play->nextEntranceIndex = dest->entranceIndex;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_FADE_BLACK;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK_FAST;

    // Use RESPAWN_MODE_TOP + respawnFlag 3 (Farore's Wind pattern)
    // respawnFlag 3 maps to respawn[3-1] = respawn[2] = RESPAWN_MODE_TOP
    gSaveContext.respawn[RESPAWN_MODE_TOP].entranceIndex = dest->entranceIndex;
    gSaveContext.respawn[RESPAWN_MODE_TOP].pos.x = dest->pos.x;
    gSaveContext.respawn[RESPAWN_MODE_TOP].pos.y = dest->pos.y;
    gSaveContext.respawn[RESPAWN_MODE_TOP].pos.z = dest->pos.z;
    gSaveContext.respawn[RESPAWN_MODE_TOP].yaw = dest->rotY;
    gSaveContext.respawn[RESPAWN_MODE_TOP].playerParams = 0xDFF;
    gSaveContext.respawn[RESPAWN_MODE_TOP].roomIndex = dest->roomIndex;
    gSaveContext.respawnFlag = 3;
}

void Handle_MinishCap(Player* p, PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;

    // === Grow animation on arrival (persists across scene transition) ===
    if (gCustomItemState.minishCapGrowing) {
        if (gCustomItemState.minishCapGrowing == 1) {
            // First frame after scene load: snap to tiny scale
            p->actor.scale.x = p->actor.scale.y = p->actor.scale.z = MINISH_SCALE_TINY;
            gCustomItemState.minishCapGrowing = 2;
        }
        // Grow toward normal
        p->actor.scale.x += MINISH_SCALE_RATE;
        if (p->actor.scale.x >= MINISH_SCALE_NORMAL) {
            p->actor.scale.x = p->actor.scale.y = p->actor.scale.z = MINISH_SCALE_NORMAL;
            gCustomItemState.minishCapGrowing = 0;
        } else {
            p->actor.scale.y = p->actor.scale.z = p->actor.scale.x;
        }
        return; // Block other input while growing
    }

    // === Shrink animation BEFORE transition (player visible shrinking) ===
    if (gCustomItemState.minishCapShrinking) {
        p->actor.scale.x -= MINISH_SCALE_RATE;
        if (p->actor.scale.x <= MINISH_SCALE_TINY) {
            // Shrink done — NOW trigger the scene transition
            p->actor.scale.x = p->actor.scale.y = p->actor.scale.z = MINISH_SCALE_TINY;
            gCustomItemState.minishCapShrinking = 0;
            gCustomItemState.minishCapGrowing = 1; // Will grow on arrival

            s8 destIdx = gCustomItemState.minishCapDestIdx;
            gCustomItemState.minishCapDestIdx = -1;
            if (destIdx >= 0 && destIdx < POD_SOIL_COUNT) {
                MinishCap_TriggerWarp(play, destIdx);
            }
        } else {
            p->actor.scale.y = p->actor.scale.z = p->actor.scale.x;
        }
        return; // Block other input while shrinking
    }

    // === Post-warp confirm: start shrinking (transition comes AFTER shrink) ===
    if (gCustomItemState.minishCapConfirmed && pauseCtx->state == 0) {
        gCustomItemState.minishCapConfirmed = 0;
        gCustomItemState.minishCapWarpMode = 0;
        // Keep minishCapDestIdx — used when shrink finishes
        gCustomItemState.minishCapShrinking = 1;
        return;
    }

    // Don't process input while warp mode is active (handled by kaleido hook)
    if (gCustomItemState.minishCapWarpMode)
        return;

    // Normal item input handling
    ItemInputState input;
    ItemInput_Update(&input, ITEM_MINISH_CAP, p, play);
    if (!input.wasEquipped)
        return;
    if (ItemInput_IsBlocked(p, play))
        return;

    if (input.isPressed) {
        // Must be near an unlocked pod soil to use
        if (!MinishCap_IsNearPodSoil(p, play)) {
            Audio_PlaySoundGeneral(NA_SE_SY_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            return;
        }

        // Don't open if pause menu is already open or transitioning
        if (pauseCtx->state != 0 || pauseCtx->debugState != 0)
            return;
        if (play->transitionTrigger != TRANS_TRIGGER_OFF)
            return;
        if (play->gameOverCtx.state != GAMEOVER_INACTIVE)
            return;

        // Set warp mode flag and freeze gameplay
        gCustomItemState.minishCapWarpMode = 1;
        gCustomItemState.minishCapConfirmed = 0;
        gCustomItemState.minishCapDestIdx = -1;
        gCustomItemState.minishCapCursorIdx = 0;

        // Freeze gameplay by setting pauseCtx->state != 0
        pauseCtx->state = 1;

        Audio_PlaySoundGeneral(NA_SE_SY_WIN_OPEN, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
}
