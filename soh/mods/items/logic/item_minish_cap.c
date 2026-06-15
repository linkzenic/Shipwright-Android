/**
 * item_minish_cap.c - The Minish Cap (Fast Travel via Pod Soils)
 *
 * Controls:
 *   C Button near an unlocked pod soil: Open custom Minish Kaleido warp map
 *   C Button away from pod soils:       Toggle Minish tiny mode (shrink/grow)
 *
 * Tiny mode:
 *   - Link shrinks to 10% scale (Minish size) and the camera zooms in with him
 *   - Movement speed drops to 20% of normal; everything else works as usual
 *   - Small enough to walk straight through crawlspace holes (no crawl anim)
 *   - Press the item button again to grow back (blocked under low ceilings)
 *   - ANY loading zone / scene reload automatically restores normal size
 *
 * Features:
 *   - Opens custom kaleido with full world map + area box indicators
 *   - Navigate between 10 pod soil locations with analog stick
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

// Pod Soil table: 10 OOT bean spots (all overworld)
// Positions and GS flags extracted from OOT decomp scene actor lists
// areaIdx = world map area index used for area box position/texture in minish_kaleido.c
// gsMask=0 means always unlocked (no skulltula required)
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
    // #9 Zora's River (spot03 room_0) - ObjBean params 0x1F03
    // Always unlocked (gsMask=0 sentinel)
    {
        SCENE_ZORAS_RIVER,
        ENTR_ZORAS_RIVER_WEST_EXIT,
        { -730.0f, 100.0f, -220.0f },
        0x4000, // Face downstream (west)
        0,      // gsGroup (unused — always unlocked)
        0x00,   // gsMask=0 → always unlocked sentinel
        0,      // roomIndex (spot03_room_0)
        3,      // areaIdx (Zora's River)
        "Zora's River",
    },
};

s32 MinishCap_IsPodSoilUnlocked(s32 idx) {
    if (idx < 0 || idx >= POD_SOIL_COUNT)
        return 0;
    // gsMask=0 sentinel means always unlocked (no skulltula required)
    if (sPodSoilTable[idx].gsMask == 0)
        return 1;
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

// ════════════════════════════════════════════════════════════════════════════
// Tiny mode (Minish-size toggle, used away from pod soils)
// ════════════════════════════════════════════════════════════════════════════

#define MINISH_TINY_SCALE 0.001f                                       // 10% of normal — Minish size
#define MINISH_TINY_FACTOR (MINISH_TINY_SCALE / MINISH_SCALE_NORMAL)   // 0.1
#define MINISH_TINY_SPEED_FACTOR 0.2f                                  // 20% of normal movement speed
#define MINISH_TINY_CAM_MIN 0.14f // camera zoom floor (keeps eye outside the near plane)
#define MINISH_TINY_WALL_SKIP_FRAMES 8 // wall-check-off linger after a crawlspace is last detected

// Declared in transformation_masks.h, which is included AFTER this file in the
// z_player.c unity build — forward-declare it here.
extern u8 TransformMasks_IsTransformed(void);

// Saved PlayerAgeProperties originals. The struct is a static table shared with
// the rest of z_player.c, so tiny mode scales the fields in place and restores
// them when the mode ends (or a scene loads).
static u8 sTinyAgePropsSaved = 0;
static PlayerAgeProperties* sTinyAgePropsPtr = NULL;
static f32 sTinyOrigCeiling;    // ceilingCheckHeight
static f32 sTinyOrigWallRadius; // wallCheckRadius
static f32 sTinyOrigWade;       // unk_2C (water wade depth threshold)

// Scene-load detection (postman hat pattern: sceneNum change OR frame rewind)
static s16 sTinyLastScene = -1;
static u32 sTinyLastFrames = 0;

// Frames left with the player's wall bgcheck disabled (crawlspace pass-through).
// Re-armed every frame the crawlspace is detected (in z_player.c), so this is just
// the linger after detection stops.
static s16 sTinyWallSkipTimer = 0;

// Saved body-cylinder radius. The per-frame code recomputes height/yShift on its
// own (they self-heal once Link is normal size), but it NEVER touches dim.radius,
// so we must capture and restore it ourselves or it stays shrunk after growing back.
static u8 sTinyCylSaved = 0;
static s16 sTinyOrigCylRadius = 0;

static void MinishTiny_RestoreCylinder(Player* p) {
    if (!sTinyCylSaved)
        return;
    if (p != NULL) {
        p->cylinder.dim.radius = sTinyOrigCylRadius;
        Collider_UpdateCylinder(&p->actor, &p->cylinder);
    }
    sTinyCylSaved = 0;
}

s32 MinishTiny_IsActive(void) {
    return gCustomItemState.minishTinyActive != 0;
}

f32 MinishTiny_GetSpeedFactor(void) {
    return gCustomItemState.minishTinyActive ? MINISH_TINY_SPEED_FACTOR : 1.0f;
}

// How small the scene-collision wall radius gets while tiny. The cylinder
// (combat/OC hitbox) already auto-shrinks from the scaled skeleton; this is the
// horizontal SCENE collider that decides how close Link can get to walls and
// how narrow a gap he fits through. 0.1 == proportional to the 0.1 visual scale
// (≈1.4 world units, about Link's rendered half-width at 0.001). Smaller never
// freezes movement (it only reduces wall blocking) — push toward 0.05 for a
// tighter squeeze if this still feels too big.
#define MINISH_TINY_WALLRADIUS_FACTOR 0.1f

static void MinishTiny_ApplyAgeProps(Player* p) {
    PlayerAgeProperties* props = p->ageProperties;

    if (sTinyAgePropsSaved)
        return;
    sTinyAgePropsPtr = props;
    sTinyOrigCeiling = props->ceilingCheckHeight;
    sTinyOrigWallRadius = props->wallCheckRadius;
    sTinyOrigWade = props->unk_2C;

    // ONLY the wall-check radius is shrunk. Deliberately NOT touched:
    //  - ceilingCheckHeight: the bgcheck ceiling probe is (ceil + yDelta) - 10;
    //    shrinking it pins world.pos.y / froze the player (the regression we hit).
    //  - unk_14/18/1C (ledge vault thresholds): shrinking them traps Link in the
    //    250jump vault. Ledge climbing is disabled while tiny instead.
    props->wallCheckRadius *= MINISH_TINY_WALLRADIUS_FACTOR;
    sTinyAgePropsSaved = 1;
}

static void MinishTiny_RestoreAgeProps(void) {
    if (!sTinyAgePropsSaved)
        return;
    sTinyAgePropsPtr->ceilingCheckHeight = sTinyOrigCeiling;
    sTinyAgePropsPtr->wallCheckRadius = sTinyOrigWallRadius;
    sTinyAgePropsPtr->unk_2C = sTinyOrigWade;
    sTinyAgePropsSaved = 0;
}

// Instantly end tiny mode and restore everything (loading zones, form changes)
static void MinishTiny_ForceReset(Player* p) {
    MinishTiny_RestoreAgeProps();
    MinishTiny_RestoreCylinder(p);
    gCustomItemState.minishTinyActive = 0;
    gCustomItemState.minishTinyAnim = 0;
    sTinyWallSkipTimer = 0;
    // The pod-soil warp arrival anim owns the scale ramp — don't fight it
    if (p != NULL && gCustomItemState.minishCapGrowing == 0 && gCustomItemState.minishCapShrinking == 0) {
        p->actor.scale.x = p->actor.scale.y = p->actor.scale.z = MINISH_SCALE_NORMAL;
    }
}

void MinishTiny_Update(Player* p, PlayState* play) {
    if (p == NULL || play == NULL)
        return;

    // ANY loading zone (scene change or same-scene reload/void-out) resets the mode
    s32 sceneLoaded = (play->sceneNum != sTinyLastScene) || (play->state.frames < sTinyLastFrames);
    sTinyLastScene = play->sceneNum;
    sTinyLastFrames = play->state.frames;
    if (sceneLoaded) {
        if (gCustomItemState.minishTinyActive || gCustomItemState.minishTinyAnim || sTinyAgePropsSaved) {
            MinishTiny_ForceReset(p);
        }
        return;
    }

    if (!gCustomItemState.minishTinyActive && gCustomItemState.minishTinyAnim == 0)
        return;

    // Transformation forms (FD/Pikachu/etc.) own the player scale — bail out
    if (TransformMasks_IsTransformed()) {
        MinishTiny_ForceReset(p);
        return;
    }

    if (gCustomItemState.minishTinyAnim == 1) {
        // Shrinking toward tiny
        p->actor.scale.x -= MINISH_SCALE_RATE;
        if (p->actor.scale.x <= MINISH_TINY_SCALE) {
            p->actor.scale.x = MINISH_TINY_SCALE;
            gCustomItemState.minishTinyAnim = 0;
        }
        p->actor.scale.y = p->actor.scale.z = p->actor.scale.x;
    } else if (gCustomItemState.minishTinyAnim == 2) {
        // Growing back to normal
        p->actor.scale.x += MINISH_SCALE_RATE;
        if (p->actor.scale.x >= MINISH_SCALE_NORMAL) {
            p->actor.scale.x = MINISH_SCALE_NORMAL;
            gCustomItemState.minishTinyAnim = 0;
            gCustomItemState.minishTinyActive = 0;
            MinishTiny_RestoreAgeProps();
            MinishTiny_RestoreCylinder(p);
        }
        p->actor.scale.y = p->actor.scale.z = p->actor.scale.x;
    } else {
        // Fully tiny: re-assert the scale every frame (other systems reset it to 0.01)
        p->actor.scale.x = p->actor.scale.y = p->actor.scale.z = MINISH_TINY_SCALE;
    }

    // ── Body cylinder (combat/OC collider) ───────────────────────────────────
    // The per-frame code in Player_UpdateCommon recomputes cylinder height/yShift
    // from the skeleton but NEVER touches dim.radius, so it stays pinned at 12 —
    // that's the "radius still huge" the player sees. Mirror Pikachu's approach
    // (pikachu_form.cpp: player->cylinder.dim.radius = 10; yShift = -15) but at
    // 75% of his size. We run AFTER Player_UpdateCommon (and its
    // CollisionCheck_SetOC) but BEFORE the frame's actual CollisionCheck pass, so
    // re-applying the dims with Collider_UpdateCylinder takes effect this frame.
    if (gCustomItemState.minishTinyActive) {
        if (!sTinyCylSaved) {
            sTinyOrigCylRadius = p->cylinder.dim.radius; // capture vanilla radius (12) before shrinking
            sTinyCylSaved = 1;
        }
        p->cylinder.dim.radius = (s16)(2.0f); // Pikachu radius 10 → 7
        p->cylinder.dim.height = (s16)(2.0f); // Pikachu body height ~35 → 26
        Collider_UpdateCylinder(&p->actor, &p->cylinder);
    }
}

// Armed by z_player.c each frame tiny Link faces a crawlspace, using the SAME
// sTouchedWallFlags vanilla reads for the "Enter on A" prompt (the interact-wall
// probe, which sees the crawlspace hole poly — the movement wall check only sees
// the solid rock around it). Keeps the wall check off for a short linger so brief
// detection gaps mid-tunnel don't snap the walls back and stop him.
void MinishTiny_ArmCrawlspace(void) {
    if (gCustomItemState.minishTinyActive && gCustomItemState.minishTinyAnim == 0) {
        sTinyWallSkipTimer = MINISH_TINY_WALL_SKIP_FRAMES;
    }
}

// Returns 1 while a pass-through window is open → caller drops the wall check so
// Link's forward velocity carries him straight through the crawlspace hole. The
// detection lives in z_player.c (MinishTiny_ArmCrawlspace); this just counts down.
s32 MinishTiny_CrawlspacePassthrough(Player* p, PlayState* play) {
    (void)p;
    (void)play;
    if (!gCustomItemState.minishTinyActive || gCustomItemState.minishTinyAnim != 0) {
        sTinyWallSkipTimer = 0;
        return 0;
    }
    if (sTinyWallSkipTimer > 0) {
        sTinyWallSkipTimer--;
        return 1;
    }
    return 0;
}

void MinishTiny_AdjustCameraView(Camera* camera, Vec3f* eye, Vec3f* at) {
    Player* p;
    f32 f;

    if (camera == NULL || camera->thisIdx != MAIN_CAM)
        return;
    if (!gCustomItemState.minishTinyActive && gCustomItemState.minishTinyAnim == 0)
        return;
    p = camera->player;
    if (p == NULL)
        return;

    // Scale the whole camera rig toward the player's position, following the
    // actor scale so the zoom eases in/out with the shrink/grow animation
    f = p->actor.scale.x / MINISH_SCALE_NORMAL;
    if (f >= 0.999f)
        return;
    if (f < MINISH_TINY_CAM_MIN)
        f = MINISH_TINY_CAM_MIN;

    eye->x = p->actor.world.pos.x + (eye->x - p->actor.world.pos.x) * f;
    eye->y = p->actor.world.pos.y + (eye->y - p->actor.world.pos.y) * f;
    eye->z = p->actor.world.pos.z + (eye->z - p->actor.world.pos.z) * f;
    at->x = p->actor.world.pos.x + (at->x - p->actor.world.pos.x) * f;
    at->y = p->actor.world.pos.y + (at->y - p->actor.world.pos.y) * f;
    at->z = p->actor.world.pos.z + (at->z - p->actor.world.pos.z) * f;
}

// Grow back, unless a ceiling within normal-Link height would embed him in geometry
static void MinishTiny_TryGrowBack(Player* p, PlayState* play) {
    Vec3f checkPos = p->actor.world.pos;
    CollisionPoly* ceilPoly = NULL;
    s32 ceilBgId;
    f32 ceilY;
    f32 normalCeilHeight = sTinyAgePropsSaved ? sTinyOrigCeiling : p->ageProperties->ceilingCheckHeight;

    checkPos.y += 2.0f;
    if (BgCheck_EntityCheckCeiling(&play->colCtx, &ceilY, &checkPos, normalCeilHeight, &ceilPoly, &ceilBgId,
                                   &p->actor)) {
        Audio_PlaySoundGeneral(NA_SE_SY_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    gCustomItemState.minishTinyAnim = 2;
    Audio_PlaySoundGeneral(NA_SE_SY_CAMERA_ZOOM_DOWN, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

static void MinishTiny_StartShrink(Player* p) {
    gCustomItemState.minishTinyActive = 1;
    gCustomItemState.minishTinyAnim = 1;
    MinishTiny_ApplyAgeProps(p);
    Audio_PlaySoundGeneral(NA_SE_SY_CAMERA_ZOOM_UP, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

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
        // Don't act if pause menu is already open or a transition is in progress
        if (pauseCtx->state != 0 || pauseCtx->debugState != 0)
            return;
        if (play->transitionTrigger != TRANS_TRIGGER_OFF)
            return;
        if (play->gameOverCtx.state != GAMEOVER_INACTIVE)
            return;

        // Tiny mode toggle off: pressing while tiny always grows back
        if (gCustomItemState.minishTinyActive) {
            if (gCustomItemState.minishTinyAnim == 0) {
                MinishTiny_TryGrowBack(p, play);
            }
            return;
        }

        // Away from pod soils: toggle tiny mode on instead of warping
        if (!MinishCap_IsNearPodSoil(p, play)) {
            if (TransformMasks_IsTransformed()) {
                // Transformation forms own the player scale — refuse
                Audio_PlaySoundGeneral(NA_SE_SY_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                return;
            }
            MinishTiny_StartShrink(p);
            return;
        }

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
