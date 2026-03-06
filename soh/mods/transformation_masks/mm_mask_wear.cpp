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
#include <libultraship/bridge.h>
#include "soh/cvar_prefixes.h"

#include "mods/transformation_masks/mm_mask_wear.h"
#include "mods/transformation_masks/assets/mm_asset_loader.h"

// Tunic color table from z_player_lib.c (non-static, extern accessible)
extern "C" Color_RGB8 sTunicColors[];

// EnBom struct for bomb spawning (Blast Mask)
#include "overlays/actors/ovl_En_Bom/z_en_bom.h"

// For GameInteractor_ExecuteOnFlagSet (Don Gero frog rewards)
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

// For Kamaro's Mask → Darunia trigger (C header, needs extern "C" in C++)
extern "C" {
#include "overlays/actors/ovl_En_Du/z_en_du.h"
}

// For Kamaro's Mask → dance animation from MM (includes mm_anims.h for MmAnimId enum)
#include "mods/anim_translator/mm_anim_loader.h"

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
// Blast Mask rendering (matches MM's Player_DrawBlastMask in z_player_lib.c:3262)
//
// During cooldown: draws DL_000440 (scrolling texture, XLU) + DL_0005C0 (crossfade)
// Not in cooldown: draws DL_0005C0 (normal opaque worn mask)
// =============================================================================

// Cooldown DL: the special XLU DL with scrolling texture used during blast recovery
static const char* sBlastMaskCooldownDL = "__OTR__objects/object_mask_bakuretu/object_mask_bakuretu_DL_000440";

// D_801C0BC0: default env color for segment 0x09 (normal worn mask)
static Gfx sBlastMaskDefaultSeg9[] = {
    gsDPSetEnvColor(0, 0, 0, 255),
    gsSPEndDisplayList(),
};

// D_801C0BD0: XLU render mode for segment 0x09 (during cooldown crossfade)
static Gfx sBlastMaskXluSeg9[] = {
    gsDPSetRenderMode(AA_EN | Z_CMP | Z_UPD | IM_RD | CLR_ON_CVG | CVG_DST_WRAP | ZMODE_XLU | FORCE_BL |
                          G_RM_FOG_SHADE_A,
                      AA_EN | Z_CMP | Z_UPD | IM_RD | CLR_ON_CVG | CVG_DST_WRAP | ZMODE_XLU | FORCE_BL |
                          GBL_c2(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_1MA)),
    gsSPEndDisplayList(),
};

#define MM_MASK_IDX_ALL_NIGHT 1
#define MM_MASK_IDX_BLAST 2
#define MM_MASK_IDX_STONE 3
#define MM_MASK_IDX_GREAT_FAIRY 4
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
static s32 sDonGeroState = 0; // 0=idle, 1=giving reward
static s32 sDonGeroReward = GI_NONE;

// All-Night Mask state
static s32 sAllNightGsSpawned = 0; // Prevents re-spawning GS actors every frame

// Couple's Mask passive regen timer (same rate as Lens of Truth: 1 per 80 frames)
static s32 sCouplesMaskTimer = 0;

// Captain's Hat state
static s32 sCaptainHatSpawnTimer = 0;

// Kamaro's Mask state
static s32 sKamaroDancing = 0;
static LinkAnimationHeader* sKamaroDanceAnim = NULL;
static f32 sKamaroDanceFrame = 0.0f;
static s32 sDaruniaDanceTimer = 0; // Frames Darunia has been dancing with player

// Great Fairy Mask state
static s32 sGreatFairyMenuOpen = 0;
static s32 sGreatFairyMenuCursor = 0;
static s32 sGreatFairyInputSkip = 0; // Skip input on first frame (same-frame open/close guard)

// Great Fairy fountain teleport table
static const struct {
    const char* name;
    u16 entrance;
    s32 flagType; // 0=isMagicAcquired, 1=ITEMGETINF
    s32 flagValue;
} sGreatFairyFountains[] = {
    { "DMT - Magic", 0x0315, 0, 0 },
    { "DMC - Double Magic", 0x04BE, 1, ITEMGETINF_30 },
    { "OGC - Defense", 0x04C2, 1, ITEMGETINF_38 },
    { "ZF - Farore", 0x0371, 1, ITEMGETINF_19 },
    { "HC - Din", 0x0578, 1, ITEMGETINF_18 },
    { "Colossus - Nayru", 0x0588, 1, ITEMGETINF_1A },
};

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
        if (IS_DAY && gs->forChild == (bool)LINK_IS_CHILD && gs->scene == play->sceneNum &&
            gs->room == play->roomCtx.curRoom.num) {
            Actor_Spawn(&play->actorCtx, play, gs->id, gs->pos.x, gs->pos.y, gs->pos.z, gs->rot.x, gs->rot.y, gs->rot.z,
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
    EVENTCHKINF_SONGS_FOR_FROGS_ZL_SHIFT,    EVENTCHKINF_SONGS_FOR_FROGS_EPONA_SHIFT,
    EVENTCHKINF_SONGS_FOR_FROGS_SARIA_SHIFT, EVENTCHKINF_SONGS_FOR_FROGS_SUNS_SHIFT,
    EVENTCHKINF_SONGS_FOR_FROGS_SOT_SHIFT,   EVENTCHKINF_SONGS_FOR_FROGS_STORMS_SHIFT,
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
        sKamaroDancing = 0;
        sGreatFairyMenuOpen = 0;
        sCaptainHatSpawnTimer = 0;
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

// Restore tunic env color after mask DL to prevent color bleeding into subsequent limbs.
// Mask DLs from mm.o2r set their own env/prim colors which would otherwise tint the tunic.
static void RestoreTunicEnvColor(Player* player, Gfx** polyOpa) {
    s32 tunic = player->currentTunic;
    Color_RGB8 c = sTunicColors[tunic];

    if (tunic == PLAYER_TUNIC_KOKIRI && CVarGetInteger(CVAR_COSMETIC("Link.KokiriTunic.Changed"), 0)) {
        c = CVarGetColor24(CVAR_COSMETIC("Link.KokiriTunic.Value"), sTunicColors[PLAYER_TUNIC_KOKIRI]);
    } else if (tunic == PLAYER_TUNIC_GORON && CVarGetInteger(CVAR_COSMETIC("Link.GoronTunic.Changed"), 0)) {
        c = CVarGetColor24(CVAR_COSMETIC("Link.GoronTunic.Value"), sTunicColors[PLAYER_TUNIC_GORON]);
    } else if (tunic == PLAYER_TUNIC_ZORA && CVarGetInteger(CVAR_COSMETIC("Link.ZoraTunic.Changed"), 0)) {
        c = CVarGetColor24(CVAR_COSMETIC("Link.ZoraTunic.Value"), sTunicColors[PLAYER_TUNIC_ZORA]);
    }

    gDPPipeSync((*polyOpa)++);
    gDPSetEnvColor((*polyOpa)++, c.r, c.g, c.b, 0);
}

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

        if (idx == MM_MASK_IDX_BLAST) {
            // =================================================================
            // Blast Mask: MM-style two-DL crossfade (Player_DrawBlastMask)
            //
            // During cooldown:
            //   1. DL_000440 (scrolling texture) drawn with env alpha
            //   2. DL_0005C0 (normal mask) drawn with inverted alpha via seg 0x09
            // Not in cooldown:
            //   1. DL_0005C0 drawn normally with seg 0x09 = default env
            // =================================================================
            if (sBlastMaskCooldown > 0) {
                // Set up texture scroll on segment 0x08 (for DL_000440)
                // Matches MM's AnimatedMat TexScrollParams: {1,1,32,32}, {3,-2,32,32}
                gSPSegment(POLY_OPA_DISP++, 0x08,
                           (uintptr_t)Gfx_TwoTexScroll(play->state.gfxCtx, 0, play->gameplayFrames * 1,
                                                       play->gameplayFrames * 1, 32, 32, 1, play->gameplayFrames * 3,
                                                       (u32)(-(s32)(play->gameplayFrames * 2)), 32, 32));

                // Alpha: 255 during most of cooldown, fade out in last 10 frames
                s32 alpha;
                if (sBlastMaskCooldown <= 10) {
                    alpha = (s32)((sBlastMaskCooldown / 10.0f) * 255.0f);
                } else {
                    alpha = 255;
                }

                // Draw cooldown DL (000440) with env alpha
                gDPPipeSync(POLY_OPA_DISP++);
                gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, (u8)alpha);
                gSPDisplayList(POLY_OPA_DISP++, (Gfx*)sBlastMaskCooldownDL);

                // Set segment 0x09 = XLU render mode for the normal worn DL crossfade
                gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)sBlastMaskXluSeg9);
                gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, (u8)(255 - alpha));

                // Draw normal worn DL (0005C0) with inverted alpha (crossfade)
                gSPDisplayList(POLY_OPA_DISP++, (Gfx*)dlPath);
            } else {
                // Not in cooldown: set segment 0x09 default, draw normally
                gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)sBlastMaskDefaultSeg9);
                gSPDisplayList(POLY_OPA_DISP++, (Gfx*)dlPath);
            }
        } else if (idx == MM_MASK_IDX_GREAT_FAIRY) {
            // =================================================================
            // Great Fairy Mask: DL references segment 0x0B for 6 leaf matrices
            // (MM uses physics simulation for animated leaves; here static identity)
            // Also references 0x0D for head limb matrix (already set by player draw)
            // =================================================================
            Mtx* leafMtx = (Mtx*)Graph_Alloc(play->state.gfxCtx, 6 * sizeof(Mtx));
            if (leafMtx != NULL) {
                // Rotate 180° on Y so leaves point backward (they extend in +Z in DL space)
                Matrix_Push();
                Matrix_RotateY(M_PI, MTXMODE_APPLY);
                for (s32 i = 0; i < 6; i++) {
                    Matrix_ToMtx(&leafMtx[i], (char*)__FILE__, __LINE__);
                }
                Matrix_Pop();
                gSPSegment(POLY_OPA_DISP++, 0x0B, (uintptr_t)leafMtx);
            }
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)dlPath);
        } else {
            // =================================================================
            // All other masks: single DL draw
            // =================================================================
            if (rot->x != 0 || rot->y != 0 || rot->z != 0) {
                Matrix_Push();
                Matrix_RotateZYX(rot->x, rot->y, rot->z, MTXMODE_APPLY);
                gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
                          G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                gSPDisplayList(POLY_OPA_DISP++, (Gfx*)dlPath);
                Matrix_Pop();
            } else {
                gSPDisplayList(POLY_OPA_DISP++, (Gfx*)dlPath);
            }
        }

        // Restore tunic env color after mask DL — mask DLs from mm.o2r set their own
        // env/prim colors which would otherwise bleed into subsequent limb draws (tunic, arms).
        RestoreTunicEnvColor(player, &POLY_OPA_DISP);

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
                if (sBlastMaskCooldown == 0 && CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B)) {
                    Vec3f bombPos = player->actor.world.pos;

                    EnBom* bomb = (EnBom*)Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOM, bombPos.x, bombPos.y,
                                                      bombPos.z, 0, 0, 0, BOMB_BODY, true);

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
                        sBlastMaskCooldown = CVarGetInteger("gMods.BlastMask.Instant", 0) ? 1 : BLAST_MASK_COOLDOWN;
                    }
                }
                break;
            }
            case 3: // Stone Mask - effect handled in z_actor.c via MmMaskWear_IsStoneMaskActive()
                break;
            case 4: // Great Fairy Mask — A to claim reward in fountain + B for teleport
            {
                if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE))
                    break;

                // A) In fountain scenes: A press triggers reward (sets switch flag 0x38)
                if (play->sceneNum == SCENE_GREAT_FAIRYS_FOUNTAIN_MAGIC ||
                    play->sceneNum == SCENE_GREAT_FAIRYS_FOUNTAIN_SPELLS) {
                    if (CHECK_BTN_ALL(play->state.input[0].press.button, BTN_A)) {
                        Flags_SetSwitch(play, 0x38);
                    }
                    break;
                }

                // B) Outside fountains: B press opens teleport menu (pause-based, like Minish Cap)
                // Menu navigation is handled by MmMaskWear_GreatFairyWarpUpdate called from z_play.c
                if (!sGreatFairyMenuOpen && CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B)) {
                    // Don't open if pause menu is already open or transitioning
                    PauseContext* pauseCtx = &play->pauseCtx;
                    if (pauseCtx->state != 0 || pauseCtx->debugState != 0)
                        break;
                    if (play->transitionTrigger != TRANS_TRIGGER_OFF)
                        break;
                    if (play->gameOverCtx.state != GAMEOVER_INACTIVE)
                        break;

                    sGreatFairyMenuOpen = 1;
                    sGreatFairyMenuCursor = 0;
                    sGreatFairyInputSkip = 1; // Prevent same-frame close (B still pressed)
                    pauseCtx->state = 1;      // Freeze gameplay (Minish Cap pattern)
                    Audio_PlaySoundGeneral(NA_SE_SY_WIN_OPEN, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                }
                break;
            }
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
                if ((dx * dx + dz * dz) > FROG_LOG_RADIUS_SQ)
                    break;

                // Check A button press
                if (!CHECK_BTN_ALL(play->state.input[0].press.button, BTN_A))
                    break;

                // Check player is not dead/busy
                if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_WATER | PLAYER_STATE1_IN_CUTSCENE))
                    break;

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
            case 15: // Couple's Mask — passive regen (day=HP, night=MP)
            {
                // Target: full recovery in ~32 seconds (640 frames at 20fps)
                // Day: 10 hearts (160 HP) in 640 frames = 1 HP every 4 frames
                // Night: 96 magic in 640 frames = 1 MP every ~7 frames
                sCouplesMaskTimer--;
                if (sCouplesMaskTimer <= 0) {
                    if (IS_DAY) {
                        sCouplesMaskTimer = 4;
                        if (gSaveContext.health < gSaveContext.healthCapacity) {
                            gSaveContext.health++;
                        }
                    } else {
                        sCouplesMaskTimer = 7;
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
            case 18: // Kamaro's Mask — hold A to dance, triggers Darunia's joy
            {
                // Don't process while busy
                if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE)) {
                    sKamaroDancing = 0;
                    break;
                }

                // Hold A = dance, release A = stop (raw input, not filtered sp44)
                if (CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_A)) {
                    if (!sKamaroDancing) {
                        // Start dancing — load animation if needed
                        if (sKamaroDanceAnim == NULL && MmAssets_IsAvailable()) {
                            sKamaroDanceAnim = MmAnim_Load(MM_ANIM_ALINK_DANCE_LOOP);
                        }
                        if (sKamaroDanceAnim != NULL) {
                            sKamaroDancing = 1;
                            sKamaroDanceFrame = 0.0f;
                        }
                    }
                } else {
                    // A released → stop dancing
                    if (sKamaroDancing && EnDu_IsDancing()) {
                        // Stop Darunia's dance too (find him in Goron City)
                        Actor* npc = play->actorCtx.actorLists[ACTORCAT_NPC].head;
                        while (npc != NULL) {
                            if (npc->id == ACTOR_EN_DU) {
                                EnDu_StopDancing(npc, play);
                                break;
                            }
                            npc = npc->next;
                        }
                        sDaruniaDanceTimer = 0;
                    }
                    sKamaroDancing = 0;
                }

                // While dancing: override animation AFTER Player_UpdateCommon already ran
                // Input is zeroed in z_player.c when sKamaroDancing=true (via MmMaskWear_IsKamaroDancing)
                if (sKamaroDancing) {
                    // Set dance animation header (PlayLoop sets mode, endFrame, etc.)
                    LinkAnimation_PlayLoop(play, &player->skelAnime, sKamaroDanceAnim);

                    // Override curFrame with our tracked position (PlayLoop resets to 0)
                    player->skelAnime.curFrame = sKamaroDanceFrame;

                    // CRITICAL: Queue frame loading into jointTable so draw actually shows the dance.
                    // Without this, Player_UpdateCommon's SkelAnime_Update already queued the idle anim.
                    // Our SetLoadFrame runs after, so it overwrites idle data in AnimationContext_Update.
                    AnimationContext_SetLoadFrame(play, sKamaroDanceAnim, (s32)sKamaroDanceFrame,
                                                  player->skelAnime.limbCount, player->skelAnime.jointTable);

                    // Advance our tracked frame
                    sKamaroDanceFrame += 1.0f;
                    if (sKamaroDanceFrame >= player->skelAnime.animLength) {
                        sKamaroDanceFrame = 0.0f;
                    }

                    // Lock movement
                    player->linearVelocity = 0.0f;

                    // Check for Darunia in Goron City — dance together, then trigger reward
                    if (play->sceneNum == SCENE_GORON_CITY && !Flags_GetRandomizerInf(RAND_INF_DARUNIAS_JOY)) {
                        Actor* npc = play->actorCtx.actorLists[ACTORCAT_NPC].head;
                        while (npc != NULL) {
                            if (npc->id == ACTOR_EN_DU && npc->xzDistToPlayer < 200.0f) {
                                // Start Darunia dancing alongside player (no cutscene yet)
                                if (!EnDu_IsDancing()) {
                                    EnDu_StartDancing(npc, play);
                                    sDaruniaDanceTimer = 0;
                                }
                                sDaruniaDanceTimer++;

                                // After ~5 seconds of dancing together, trigger joy reward
                                if (sDaruniaDanceTimer >= 100) {
                                    EnDu_StopDancing(npc, play);
                                    EnDu_TriggerDaruniasJoy(npc, play);
                                    sKamaroDancing = 0;
                                    sDaruniaDanceTimer = 0;
                                }
                                break;
                            }
                            npc = npc->next;
                        }
                    }
                }
                break;
            }
            case 19: // Gibdo Mask
                break;
            case 20: // Garo's Mask
                break;
            case 21: // Captain's Hat — spawn giant Stalchildren/Stalfos at night in Hyrule Field
            {
                if (play->sceneNum != SCENE_HYRULE_FIELD || IS_DAY) {
                    sCaptainHatSpawnTimer = 0;
                    break;
                }

                // Don't spawn if player is busy
                if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE))
                    break;

                sCaptainHatSpawnTimer++;
                if (sCaptainHatSpawnTimer < 100) // Every ~5 seconds
                    break;

                // Count current enemies in scene, max 3
                s32 enemyCount = 0;
                Actor* enemy = play->actorCtx.actorLists[ACTORCAT_ENEMY].head;
                while (enemy != NULL) {
                    if (enemy->id == ACTOR_EN_SKB || enemy->id == ACTOR_EN_TEST) {
                        if (enemy->home.rot.z == 0x7FFF)
                            enemyCount++;
                    }
                    enemy = enemy->next;
                }

                if (enemyCount >= 3) {
                    sCaptainHatSpawnTimer = 80; // Check again sooner
                    break;
                }

                // Random spawn position 200-400 units from Link
                f32 angle = Rand_ZeroOne() * 65536.0f;
                f32 dist = 200.0f + Rand_ZeroOne() * 200.0f;
                f32 spawnX = player->actor.world.pos.x + Math_SinS((s16)angle) * dist;
                f32 spawnZ = player->actor.world.pos.z + Math_CosS((s16)angle) * dist;
                f32 spawnY = player->actor.world.pos.y;

                Actor* spawned;
                if (LINK_IS_ADULT) {
                    // Adult: Stalfos
                    spawned = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_TEST, spawnX, spawnY, spawnZ, 0, (s16)angle,
                                          0, 0, true);
                } else {
                    // Child: Giant Stalchild (params=10 → 2x scale, 2x speed)
                    spawned = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_SKB, spawnX, spawnY, spawnZ, 0, (s16)angle, 0,
                                          10, true);
                }

                if (spawned != NULL) {
                    spawned->home.rot.z = 0x7FFF; // Drop sentinel
                }

                sCaptainHatSpawnTimer = 0;
                break;
            }
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

extern "C" void MmMaskWear_SetCurrent(s32 maskItem) {
    sCurrentMmMask = maskItem;
}

extern "C" void MmMaskWear_Clear(void) {
    sCurrentMmMask = ITEM_NONE;
    sBlastMaskCooldown = 0;
    sDonGeroState = 0;
    sDonGeroReward = GI_NONE;
    sAllNightGsSpawned = 0;
    sCouplesMaskTimer = 0;
    sCaptainHatSpawnTimer = 0;
    sKamaroDancing = 0;
    sDaruniaDanceTimer = 0;
    sGreatFairyMenuOpen = 0;
    sGreatFairyMenuCursor = 0;
    sGreatFairyInputSkip = 0;
    // NOTE: sChateauRomaniActive is NOT cleared here (persists across scenes, cleared on death)
    // NOTE: sKamaroDanceAnim is NOT cleared (cached animation, reusable)
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
    if (sChateauRomaniActive)
        return;

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
    if (!sChateauRomaniActive)
        return;

    sChateauRomaniActive = 0;
    // Only unset the flag if WE set it (not rando)
    if (sChateauDidSetFlag) {
        Flags_UnsetRandomizerInf(RAND_INF_HAS_INFINITE_MAGIC_METER);
        sChateauDidSetFlag = 0;
    }
}

// =============================================================================
// Great Fairy Mask - Teleport Menu Overlay (GfxPrint)
// =============================================================================

extern "C" void MmMaskWear_DrawOverlay(PlayState* play) {
    if (!sGreatFairyMenuOpen)
        return;

    try {
        GraphicsContext* gfxCtx = play->state.gfxCtx;
        OPEN_DISPS(gfxCtx);

        // 1. Dark semi-transparent background on OVERLAY_DISP
        gDPPipeSync(OVERLAY_DISP++);
        gDPSetCycleType(OVERLAY_DISP++, G_CYC_1CYCLE);
        gDPSetCombineMode(OVERLAY_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
        gDPSetOtherMode(OVERLAY_DISP++,
                        G_AD_DISABLE | G_CD_DISABLE | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                            G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                        G_AC_NONE | G_ZS_PRIM | G_RM_CLD_SURF | G_RM_CLD_SURF2);
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 0, 0, 0, 160);
        gSPWideTextureRectangle(OVERLAY_DISP++, 0, 0, SCREEN_WIDTH << 2, SCREEN_HEIGHT << 2, G_TX_RENDERTILE, 0, 0, 0,
                                0);
        gDPPipeSync(OVERLAY_DISP++);

        // 2. GfxPrint text — write directly into OVERLAY_DISP buffer (large enough for all text).
        //    Previous approach used Graph_Alloc(512 entries) which overflowed because
        //    gSPTextureRectangle uses 3 Gfx entries per char (with shadow = ~8 entries/char).
        GfxPrint printer;
        GfxPrint_Init(&printer);
        GfxPrint_Open(&printer, OVERLAY_DISP);

        // Title
        GfxPrint_SetColor(&printer, 200, 255, 200, 255);
        GfxPrint_SetPos(&printer, 8, 6);
        GfxPrint_Printf(&printer, "Great Fairy Fountains");

        GfxPrint_SetColor(&printer, 150, 150, 150, 255);
        GfxPrint_SetPos(&printer, 8, 8);
        GfxPrint_Printf(&printer, "DPad:Select A:Warp B:Cancel");

        // List fountains
        for (s32 i = 0; i < 6; i++) {
            s32 discovered = 0;
            if (sGreatFairyFountains[i].flagType == 0) {
                discovered = gSaveContext.isMagicAcquired;
            } else {
                discovered = Flags_GetItemGetInf(sGreatFairyFountains[i].flagValue);
            }

            GfxPrint_SetPos(&printer, 10, 10 + i);

            if (i == sGreatFairyMenuCursor) {
                if (discovered) {
                    GfxPrint_SetColor(&printer, 255, 255, 100, 255);
                } else {
                    GfxPrint_SetColor(&printer, 200, 100, 100, 255);
                }
                GfxPrint_Printf(&printer, "> %s", sGreatFairyFountains[i].name);
            } else {
                if (discovered) {
                    GfxPrint_SetColor(&printer, 200, 200, 200, 255);
                } else {
                    GfxPrint_SetColor(&printer, 80, 80, 80, 255);
                }
                GfxPrint_Printf(&printer, "  %s", sGreatFairyFountains[i].name);
            }
        }

        OVERLAY_DISP = GfxPrint_Close(&printer);
        GfxPrint_Destroy(&printer);

        CLOSE_DISPS(gfxCtx);
    } catch (...) {}
}

// =============================================================================
// Kamaro Dance query
// =============================================================================

extern "C" s32 MmMaskWear_IsKamaroDancing(void) {
    return sKamaroDancing;
}

// =============================================================================
// Great Fairy Warp — pause-based update (called from z_play.c when paused)
// =============================================================================

extern "C" s32 MmMaskWear_IsGreatFairyWarpActive(void) {
    return sGreatFairyMenuOpen;
}

extern "C" void MmMaskWear_GreatFairyWarpUpdate(PlayState* play) {
    if (!sGreatFairyMenuOpen)
        return;

    // Skip input on the first frame to prevent same-frame open/close.
    // The B press that opened the menu is still active in this frame's input,
    // so the cancel check below would immediately close the menu.
    if (sGreatFairyInputSkip) {
        sGreatFairyInputSkip = 0;
        return;
    }

    Input* input = &play->state.input[0];

    // D-pad navigation
    if (CHECK_BTN_ALL(input->press.button, BTN_DUP)) {
        sGreatFairyMenuCursor--;
        if (sGreatFairyMenuCursor < 0)
            sGreatFairyMenuCursor = 5;
        Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
    if (CHECK_BTN_ALL(input->press.button, BTN_DDOWN)) {
        sGreatFairyMenuCursor++;
        if (sGreatFairyMenuCursor > 5)
            sGreatFairyMenuCursor = 0;
        Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }

    // B cancels
    if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
        sGreatFairyMenuOpen = 0;
        play->pauseCtx.state = 0;
        Audio_PlaySoundGeneral(NA_SE_SY_WIN_CLOSE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    // A confirms warp
    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        s32 sel = sGreatFairyMenuCursor;
        s32 discovered = 0;
        if (sGreatFairyFountains[sel].flagType == 0) {
            discovered = gSaveContext.isMagicAcquired;
        } else {
            discovered = Flags_GetItemGetInf(sGreatFairyFountains[sel].flagValue);
        }

        if (discovered) {
            sGreatFairyMenuOpen = 0;
            play->pauseCtx.state = 0; // Unpause so transition can process
            play->nextEntranceIndex = sGreatFairyFountains[sel].entrance;
            play->transitionTrigger = TRANS_TRIGGER_START;
            play->transitionType = TRANS_TYPE_FADE_BLACK;
            Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        } else {
            Audio_PlaySoundGeneral(NA_SE_SY_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
    }
}
