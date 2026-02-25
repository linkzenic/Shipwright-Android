/**
 * mm_mask_wear.cpp - MM Mask Wearing System
 *
 * Draws MM mask DLs on Link's head using the head limb's transformation matrix.
 * Worn mask DLs come from mm.o2r, matching MM's D_801C0B20[] table.
 *
 * Each mask has a per-mask effect switch (empty stubs for now).
 * Extra rotation offsets per mask are provided for future fine-tuning.
 */

#include "z64.h"
#include "z64item.h"
#include "macros.h"
#include "functions.h"
#include <exception>

#include "mods/transformation_masks/mm_mask_wear.h"
#include "mods/transformation_masks/assets/mm_asset_loader.h"

// EnBom struct for bomb spawning (Blast Mask)
#include "overlays/actors/ovl_En_Bom/z_en_bom.h"

// For GameInteractor_ExecuteOnFlagSet (Don Gero frog rewards)
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

// =============================================================================
// Worn Mask DL OTR Paths (from MM D_801C0B20[] - z_player_lib.c line 2853)
// =============================================================================
// Indexed 0-23 matching ITEM_MM_MASK_POSTMAN(0xB7) through ITEM_MM_MASK_FIERCE_DEITY(0xCE).

static const char* sMmWornMaskDLPaths[24] = {
    "__OTR__objects/object_mask_posthat/object_mask_posthat_DL_000290",   // 0  Postman's Hat
    "__OTR__objects/object_mask_yofukasi/object_mask_yofukasi_DL_000490", // 1  All-Night Mask
    "__OTR__objects/object_mask_bakuretu/object_mask_bakuretu_DL_0005C0", // 2  Blast Mask
    "__OTR__objects/object_mask_stone/object_mask_stone_DL_000820",       // 3  Stone Mask
    "__OTR__objects/object_mask_bigelf/object_mask_bigelf_DL_0016F0",     // 4  Great Fairy Mask
    "__OTR__objects/gameplay_keep/gDekuMaskDL",                           // 5  Deku Mask
    "__OTR__objects/object_mask_ki_tan/object_mask_ki_tan_DL_0004A0",     // 6  Keaton Mask
    "__OTR__objects/object_mask_bree/object_mask_bree_DL_0003C0",         // 7  Bremen Mask
    "__OTR__objects/object_mask_rabit/object_mask_rabit_DL_000610",       // 8  Bunny Hood
    "__OTR__objects/object_mask_gero/gDonGeroMaskDL",                     // 9  Don Gero's Mask
    "__OTR__objects/object_mask_bu_san/object_mask_bu_san_DL_000710",     // 10 Mask of Scents
    "__OTR__objects/gameplay_keep/gGoronMaskDL",                          // 11 Goron Mask
    "__OTR__objects/object_mask_romerny/object_mask_romerny_DL_0007A0",   // 12 Romani Mask
    "__OTR__objects/object_mask_zacho/object_mask_zacho_DL_000700",       // 13 Circus Leader Mask
    "__OTR__objects/object_mask_kerfay/gKafeisMaskDL",                    // 14 Kafei's Mask
    "__OTR__objects/object_mask_meoto/object_mask_meoto_DL_0005A0",       // 15 Couple's Mask
    "__OTR__objects/object_mask_truth/object_mask_truth_DL_0001A0",       // 16 Mask of Truth
    "__OTR__objects/gameplay_keep/gZoraMaskDL",                           // 17 Zora Mask
    "__OTR__objects/object_mask_dancer/object_mask_dancer_DL_000EF0",     // 18 Kamaro's Mask
    "__OTR__objects/object_mask_gibudo/object_mask_gibudo_DL_000250",     // 19 Gibdo Mask
    "__OTR__objects/object_mask_json/object_mask_json_DL_0004C0",         // 20 Garo's Mask
    "__OTR__objects/object_mask_skj/object_mask_skj_DL_0009F0",           // 21 Captain's Hat
    "__OTR__objects/object_mask_kyojin/object_mask_kyojin_DL_000380",     // 22 Giant's Mask
    "__OTR__objects/gameplay_keep/gFierceDeityMaskDL",                    // 23 Fierce Deity Mask
};

// =============================================================================
// Extra rotation offsets per mask (s16 x, y, z)
// All 0 for now - adjust per-mask if positioning looks wrong.
// =============================================================================

static Vec3s sMmMaskRotOffset[24] = {
    { 0, 0, 0 }, // 0  Postman's Hat
    { 0, 0, 0 }, // 1  All-Night Mask
    { 0, 0, 0 }, // 2  Blast Mask
    { 0, 0, 0 }, // 3  Stone Mask
    { 0, 0, 0 }, // 4  Great Fairy Mask
    { 0, 0, 0 }, // 5  Deku Mask
    { 0, 0, 0 }, // 6  Keaton Mask
    { 0, 0, 0 }, // 7  Bremen Mask
    { 0, 0, 0 }, // 8  Bunny Hood
    { 0, 0, 0 }, // 9  Don Gero's Mask
    { 0, 0, 0 }, // 10 Mask of Scents
    { 0, 0, 0 }, // 11 Goron Mask
    { 0, 0, 0 }, // 12 Romani Mask
    { 0, 0, 0 }, // 13 Circus Leader Mask
    { 0, 0, 0 }, // 14 Kafei's Mask
    { 0, 0, 0 }, // 15 Couple's Mask
    { 0, 0, 0 }, // 16 Mask of Truth
    { 0, 0, 0 }, // 17 Zora Mask
    { 0, 0, 0 }, // 18 Kamaro's Mask
    { 0, 0, 0 }, // 19 Gibdo Mask
    { 0, 0, 0 }, // 20 Garo's Mask
    { 0, 0, 0 }, // 21 Captain's Hat
    { 0, 0, 0 }, // 22 Giant's Mask
    { 0, 0, 0 }, // 23 Fierce Deity Mask
};

// =============================================================================
// Blast Mask segment 0x09 default (matches MM's D_801C0BC0 in Player_DrawBlastMask)
// The Blast Mask DL references segment 0x09 for its material. Without this setup,
// segment 0x09 contains garbage → RSP crash.
// =============================================================================

static Gfx sBlastMaskDefaultSeg9[] = {
    gsDPSetEnvColor(0, 0, 0, 255),
    gsSPEndDisplayList(),
};

#define MM_MASK_IDX_ALL_NIGHT 1
#define MM_MASK_IDX_BLAST 2
#define MM_MASK_IDX_STONE 3
#define MM_MASK_IDX_DON_GERO 9
#define MM_MASK_IDX_ROMANI 12

// Blast Mask cooldown: 310 frames matching MM (z_player.c line 3873: this->blastMaskTimer = 310)
#define BLAST_MASK_COOLDOWN 310

// =============================================================================
// State
// =============================================================================

static s32 sCurrentMmMask = ITEM_NONE;
static s32 sBlastMaskCooldown = 0;

// Don Gero state
static s32 sDonGeroState = 0;    // 0=idle, 1=giving reward
static s32 sDonGeroReward = GI_NONE;

// All-Night Mask state
static s32 sAllNightGsSpawned = 0; // Prevents re-spawning GS actors every frame

// Couple's Mask passive regen timer (same rate as Lens of Truth: 1 per 80 frames)
static s32 sCouplesMaskTimer = 0;

// Chateau Romani state (persists across scene transitions, cleared on death)
static s32 sChateauRomaniActive = 0;
static s32 sChateauDidSetFlag = 0; // Track if WE set the rando inf flag

#define MM_MASK_ITEM_BASE ITEM_MM_MASK_POSTMAN
#define MM_MASK_COUNT 24

static inline s32 MaskItemToIndex(s32 itemId) {
    return itemId - MM_MASK_ITEM_BASE;
}

// =============================================================================
// All-Night Mask: Night-only Gold Skulltula spawn data
// Copied from soh/soh/Enhancements/QoL/DaytimeGS.cpp
// =============================================================================

struct NightGsEntry {
    u16 scene;
    u16 room;
    bool forChild;
    s16 id;
    Vec3s pos;
    Vec3s rot;
    s16 params;
};

static const NightGsEntry sNightOnlyGs[] = {
    // Graveyard
    { SCENE_GRAVEYARD, 1, true, ACTOR_EN_SW, { 156, 315, 795 }, { 16384, -32768, 0 }, -20096 },
    // ZF
    { SCENE_ZORAS_FOUNTAIN, 0, true, ACTOR_EN_SW, { -1891, 187, 1911 }, { 16384, 18022, 0 }, -19964 },
    // GF
    { SCENE_GERUDOS_FORTRESS, 0, false, ACTOR_EN_SW, { 1598, 999, -2008 }, { 16384, -16384, 0 }, -19198 },
    { SCENE_GERUDOS_FORTRESS, 1, false, ACTOR_EN_SW, { 3377, 1734, -4935 }, { 16384, 0, 0 }, -19199 },
    // Kak (adult)
    { SCENE_KAKARIKO_VILLAGE, 0, false, ACTOR_EN_SW, { -18, 540, 1800 }, { 0, -32768, 0 }, -20160 },
    // Kak (child)
    { SCENE_KAKARIKO_VILLAGE, 0, true, ACTOR_EN_SW, { -465, 377, -888 }, { 0, 28217, 0 }, -20222 },
    { SCENE_KAKARIKO_VILLAGE, 0, true, ACTOR_EN_SW, { 5, 686, -171 }, { 0, -32768, 0 }, -20220 },
    { SCENE_KAKARIKO_VILLAGE, 0, true, ACTOR_EN_SW, { 324, 270, 905 }, { 16384, 0, 0 }, -20216 },
    { SCENE_KAKARIKO_VILLAGE, 0, true, ACTOR_EN_SW, { -602, 120, 1120 }, { 16384, 0, 0 }, -20208 },
    // LLR
    { SCENE_LON_LON_RANCH, 0, true, ACTOR_EN_SW, { -2344, 180, 672 }, { 16384, 22938, 0 }, -29695 },
    { SCENE_LON_LON_RANCH, 0, true, ACTOR_EN_SW, { 808, 48, 326 }, { 16384, 0, 0 }, -29694 },
    { SCENE_LON_LON_RANCH, 0, true, ACTOR_EN_SW, { 997, 286, -2698 }, { 16384, -16384, 0 }, -29692 },
};

static void AllNightMask_SpawnNightGs(PlayState* play) {
    for (s32 i = 0; i < ARRAY_COUNT(sNightOnlyGs); i++) {
        const NightGsEntry* gs = &sNightOnlyGs[i];
        if (IS_DAY && gs->forChild == (bool)LINK_IS_CHILD &&
            gs->scene == play->sceneNum && gs->room == play->roomCtx.curRoom.num) {
            Actor_Spawn(&play->actorCtx, play, gs->id,
                        gs->pos.x, gs->pos.y, gs->pos.z,
                        gs->rot.x, gs->rot.y, gs->rot.z,
                        gs->params, false);
        }
    }
}

// =============================================================================
// Don Gero: Frog reward flag data (from z_en_fr.c sSongIndex/sSongIndexShift)
// =============================================================================

// Flags for gSaveContext.eventChkInf[13], matching z_en_fr.c sSongIndex[]
static const u16 sFrogFlags[] = { 0x0002, 0x0004, 0x0010, 0x0008, 0x0020, 0x0040, 0x0001 };
static const u16 sFrogShifts[] = {
    EVENTCHKINF_SONGS_FOR_FROGS_ZL_SHIFT,
    EVENTCHKINF_SONGS_FOR_FROGS_EPONA_SHIFT,
    EVENTCHKINF_SONGS_FOR_FROGS_SARIA_SHIFT,
    EVENTCHKINF_SONGS_FOR_FROGS_SUNS_SHIFT,
    EVENTCHKINF_SONGS_FOR_FROGS_SOT_SHIFT,
    EVENTCHKINF_SONGS_FOR_FROGS_STORMS_SHIFT,
    EVENTCHKINF_SONGS_FOR_FROGS_CHOIR_SHIFT,
};

// Frog log position in Zora's River (approximate center of ocarina spot)
#define FROG_LOG_X 990.0f
#define FROG_LOG_Z (-1220.0f)
#define FROG_LOG_RADIUS_SQ (200.0f * 200.0f)

// =============================================================================
// Toggle
// =============================================================================

extern "C" void MmMaskWear_Toggle(PlayState* play, Player* player, s32 itemId) {
    s32 idx = MaskItemToIndex(itemId);
    if (idx < 0 || idx >= MM_MASK_COUNT) {
        return;
    }

    if (sCurrentMmMask == itemId) {
        // Already wearing this mask - take it off
        sCurrentMmMask = ITEM_NONE;
    } else {
        // Put on this mask, clear any OOT mask
        sCurrentMmMask = itemId;
        player->currentMask = PLAYER_MASK_NONE;
    }

    Player_PlaySfx(&player->actor, NA_SE_PL_CHANGE_ARMS);
    player->stateFlags2 |= PLAYER_STATE2_FOOTSTEP;
}

// =============================================================================
// Draw (called from Player_PostLimbDrawGameplay for HEAD limb)
// =============================================================================

extern "C" void MmMaskWear_Draw(PlayState* play, Player* player) {
    if (sCurrentMmMask == ITEM_NONE) {
        return;
    }

    if (!MmAssets_IsAvailable()) {
        return;
    }

    s32 idx = MaskItemToIndex(sCurrentMmMask);
    if (idx < 0 || idx >= MM_MASK_COUNT) {
        return;
    }

    try {
        const char* dlPath = sMmWornMaskDLPaths[idx];
        Vec3s* rot = &sMmMaskRotOffset[idx];

        OPEN_DISPS(play->state.gfxCtx);

        // Blast Mask: set segment 0x09 (required by its material DL)
        if (idx == MM_MASK_IDX_BLAST) {
            if (sBlastMaskCooldown > 0) {
                // During cooldown: dark tint that gradually restores to normal
                // Lerp intensity from 0 (just exploded, black) to 255 (ready, normal)
                s32 intensity = 255 - (sBlastMaskCooldown * 255 / BLAST_MASK_COOLDOWN);
                if (intensity < 0) intensity = 0;
                if (intensity > 255) intensity = 255;

                // Build dynamic segment 0x09 with darkened env color
                Gfx* seg9 = (Gfx*)Graph_Alloc(play->state.gfxCtx, sizeof(Gfx) * 2);
                Gfx* seg9Head = seg9;
                gDPSetEnvColor(seg9++, intensity, intensity, intensity, 255);
                gSPEndDisplayList(seg9++);
                gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)seg9Head);
            } else {
                gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)sBlastMaskDefaultSeg9);
            }
        }

        if (rot->x != 0 || rot->y != 0 || rot->z != 0) {
            // Apply extra rotation offset if non-zero
            Matrix_Push();
            Matrix_RotateZYX(rot->x, rot->y, rot->z, MTXMODE_APPLY);

            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)dlPath);

            Matrix_Pop();
        } else {
            // No extra rotation - draw directly in head limb space
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)dlPath);
        }

        CLOSE_DISPS(play->state.gfxCtx);
    } catch (...) {
        // Prevent C++ exceptions from propagating through extern "C" boundary
    }
}

// =============================================================================
// Per-Mask Effect Update (empty stubs for now)
// =============================================================================

extern "C" void MmMaskWear_Update(PlayState* play, Player* player) {
    if (sCurrentMmMask == ITEM_NONE) {
        return;
    }

    try {

        s32 idx = MaskItemToIndex(sCurrentMmMask);

        switch (idx) {
            case 0: // Postman's Hat
                break;
            case 1: // All-Night Mask — spawn night-only GS actors during daytime
            {
                if (IS_DAY && !sAllNightGsSpawned) {
                    AllNightMask_SpawnNightGs(play);
                    sAllNightGsSpawned = 1;
                }
                break;
            }
            case 2: // Blast Mask
            {
                // Decrement cooldown every frame
                if (sBlastMaskCooldown > 0) {
                    sBlastMaskCooldown--;
                }

                // B button press + no cooldown = spawn bomb (like BombArrows_SpawnInstantBomb)
                if (sBlastMaskCooldown == 0 &&
                    CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B)) {
                    Vec3f bombPos = player->actor.world.pos;

                    EnBom* bomb = (EnBom*)Actor_Spawn(
                        &play->actorCtx, play, ACTOR_EN_BOM,
                        bombPos.x, bombPos.y, bombPos.z,
                        0, 0, 0, BOMB_BODY, true);

                    if (bomb != NULL) {
                        // Timer=1: decrements to 0 on first update → triggers explosion
                        bomb->timer = 1;

                        // Scale must be set (init chain sets to 0, normally set at timer=67)
                        Actor_SetScale(&bomb->actor, 0.01f);

                        // CRITICAL: Set explosion collider position manually.
                        // With timer=1, Update runs before Draw can call Collider_UpdateSpheres,
                        // so the collider position would be at (0,0,0) without this.
                        bomb->explosionCollider.elements[0].dim.worldSphere.center.x = (s16)bombPos.x;
                        bomb->explosionCollider.elements[0].dim.worldSphere.center.y = (s16)bombPos.y;
                        bomb->explosionCollider.elements[0].dim.worldSphere.center.z = (s16)bombPos.z;

                        // Start cooldown (MM: this->blastMaskTimer = 310)
                        sBlastMaskCooldown = BLAST_MASK_COOLDOWN;
                    }
                }
                break;
            }
            case 3: // Stone Mask - effect handled in z_actor.c via MmMaskWear_IsStoneMaskActive()
                break;
            case 4: // Great Fairy Mask
                break;
            case 5: // Deku Mask (transformation - shouldn't reach here)
                break;
            case 6: // Keaton Mask
                break;
            case 7: // Bremen Mask
                break;
            case 8: // Bunny Hood
                break;
            case 9: // Don Gero's Mask — collect all frog rewards at Zora's River
            {
                if (play->sceneNum != SCENE_ZORAS_RIVER) {
                    sDonGeroState = 0;
                    break;
                }

                // State 1 (vanilla only): giving a reward, keep offering until accepted
                if (sDonGeroState == 1) {
                    if (Actor_HasParent(&player->actor, play)) {
                        player->actor.parent = NULL;
                        sDonGeroState = 0;
                    } else {
                        Actor_OfferGetItem(&player->actor, play, sDonGeroReward, 30.0f, 100.0f);
                    }
                    break;
                }

                // Check position near frog log
                f32 dx = player->actor.world.pos.x - FROG_LOG_X;
                f32 dz = player->actor.world.pos.z - FROG_LOG_Z;
                if ((dx * dx + dz * dz) > FROG_LOG_RADIUS_SQ) break;

                // Check A button press
                if (!CHECK_BTN_ALL(play->state.input[0].press.button, BTN_A)) break;

                // Check player is not dead/busy
                if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_WATER |
                                            PLAYER_STATE1_IN_CUTSCENE)) break;

                // Collect all unclaimed frog rewards
                s32 bestReward = GI_NONE;
                s32 anyUnclaimed = 0;

                for (s32 i = 0; i < 7; i++) {
                    if (!(gSaveContext.eventChkInf[EVENTCHKINF_SONGS_FOR_FROGS_INDEX] & sFrogFlags[i])) {
                        gSaveContext.eventChkInf[EVENTCHKINF_SONGS_FOR_FROGS_INDEX] |= sFrogFlags[i];
                        GameInteractor_ExecuteOnFlagSet(FLAG_EVENT_CHECK_INF,
                            (EVENTCHKINF_SONGS_FOR_FROGS_INDEX << 4) + sFrogShifts[i]);
                        anyUnclaimed = 1;

                        // Songs 0-4: purple rupee, 5 (storms) and 6 (choir): heart piece
                        if (i >= 5) {
                            bestReward = GI_HEART_PIECE;
                        } else if (bestReward != GI_HEART_PIECE) {
                            bestReward = GI_RUPEE_PURPLE;
                        }
                    }
                }

                if (!anyUnclaimed) {
                    // All rewards already claimed, don't give anything
                    break;
                }

                if (IS_RANDO) {
                    // In rando: flags are set, GameInteractor_ExecuteOnFlagSet already
                    // queued the randomized items. The rando queue system will give them
                    // automatically on player update. Don't also give a hardcoded reward.
                    break;
                }

                // Vanilla: give the best reward directly
                sDonGeroReward = bestReward;
                sDonGeroState = 1;
                Actor_OfferGetItem(&player->actor, play, sDonGeroReward, 30.0f, 100.0f);
                break;
            }
            case 10: // Mask of Scents
                break;
            case 11: // Goron Mask (transformation - shouldn't reach here)
                break;
            case 12: // Romani Mask — cow interaction handled in z_en_cow.c
                break;
            case 13: // Circus Leader Mask
                break;
            case 14: // Kafei's Mask
                break;
            case 15: // Couple's Mask — passive regen (day=HP, night=MP) at Lens of Truth rate
            {
                sCouplesMaskTimer--;
                if (sCouplesMaskTimer <= 0) {
                    sCouplesMaskTimer = 80; // Same rate as Lens of Truth drain
                    if (IS_DAY) {
                        // Recover 1 HP (quarter-heart units)
                        if (gSaveContext.health < gSaveContext.healthCapacity) {
                            gSaveContext.health++;
                        }
                    } else {
                        // Recover 1 MP
                        if (gSaveContext.magic < gSaveContext.magicCapacity) {
                            gSaveContext.magic++;
                        }
                    }
                }
                break;
            }
            case 16: // Mask of Truth
                break;
            case 17: // Zora Mask (transformation - shouldn't reach here)
                break;
            case 18: // Kamaro's Mask
                break;
            case 19: // Gibdo Mask
                break;
            case 20: // Garo's Mask
                break;
            case 21: // Captain's Hat
                break;
            case 22: // Giant's Mask
                break;
            case 23: // Fierce Deity Mask (transformation - shouldn't reach here)
                break;
            default:
                break;
        }

    } catch (...) {
        // Prevent C++ exceptions from propagating through extern "C" boundary
    }
}

// =============================================================================
// Queries
// =============================================================================

extern "C" s32 MmMaskWear_GetCurrent(void) {
    return sCurrentMmMask;
}

extern "C" void MmMaskWear_Clear(void) {
    sCurrentMmMask = ITEM_NONE;
    sBlastMaskCooldown = 0;
    sDonGeroState = 0;
    sDonGeroReward = GI_NONE;
    sAllNightGsSpawned = 0;
    sCouplesMaskTimer = 0;
    // NOTE: sChateauRomaniActive is NOT cleared here (persists across scenes, cleared on death)
}

extern "C" s32 MmMaskWear_IsStoneMaskActive(void) {
    return (sCurrentMmMask != ITEM_NONE) && (MaskItemToIndex(sCurrentMmMask) == MM_MASK_IDX_STONE);
}

extern "C" s32 MmMaskWear_IsBlastCooldown(void) {
    return sBlastMaskCooldown > 0;
}

extern "C" s32 MmMaskWear_IsAllNightMaskActive(void) {
    return (sCurrentMmMask != ITEM_NONE) && (MaskItemToIndex(sCurrentMmMask) == MM_MASK_IDX_ALL_NIGHT);
}

extern "C" s32 MmMaskWear_IsChateauRomaniActive(void) {
    return sChateauRomaniActive;
}

extern "C" void MmMaskWear_ActivateChateauRomani(void) {
    if (sChateauRomaniActive) return;

    sChateauRomaniActive = 1;
    // If rando didn't already set infinite magic, we set it
    if (!Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_MAGIC_METER)) {
        Flags_SetRandomizerInf(RAND_INF_HAS_INFINITE_MAGIC_METER);
        sChateauDidSetFlag = 1;
    } else {
        sChateauDidSetFlag = 0; // Rando already had it, don't touch on deactivate
    }
}

extern "C" void MmMaskWear_DeactivateChateauRomani(void) {
    if (!sChateauRomaniActive) return;

    sChateauRomaniActive = 0;
    // Only unset the flag if WE set it (not rando)
    if (sChateauDidSetFlag) {
        Flags_UnsetRandomizerInf(RAND_INF_HAS_INFINITE_MAGIC_METER);
        sChateauDidSetFlag = 0;
    }
}
