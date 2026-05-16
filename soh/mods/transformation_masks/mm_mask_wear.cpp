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
#include "soh/frame_interpolation.h"
#include "mods/pak_loader/pak_loader.h"
#include <exception>
#include <libultraship/bridge.h>
#include "soh/cvar_prefixes.h"

#include "mods/transformation_masks/mm_mask_wear.h"
#include "mods/transformation_masks/assets/mm_asset_loader.h"
#include "mods/sound_translator/mm_sfx_ids.h"

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

// For Postman's Hat warp
extern "C" {
#include "mods/items/logic/item_postman_hat.h"
#include "mods/items/helpers/bremen_follower_actor.h"
#include "mods/items/helpers/mushroom_spot_actor.h"
}

// For Great Fairy Mask map overlay (same textures as minish_kaleido)
#include "textures/map_name_static/map_name_static.h"
#include "textures/icon_item_static/icon_item_static.h"
#include "textures/icon_item_field_static/icon_item_field_static.h"
#include "textures/icon_item_nes_static/icon_item_nes_static.h"
#include "textures/icon_item_ger_static/icon_item_ger_static.h"
#include "textures/icon_item_fra_static/icon_item_fra_static.h"
#include "textures/icon_item_jpn_static/icon_item_jpn_static.h"
#include "textures/icon_item_24_static/icon_item_24_static.h"

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
#define MM_MASK_IDX_DEKU 5
#define MM_MASK_IDX_DON_GERO 9
#define MM_MASK_IDX_GORON 11
#define MM_MASK_IDX_ROMANI 12
#define MM_MASK_IDX_ZORA 17
#define MM_MASK_IDX_GIBDO 19
#define MM_MASK_IDX_FIERCE_DEITY 23

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

// Bremen Mask state — transient (cleared in MmMaskWear_Clear)
static s32 sBremenMarching = 0;
static LinkAnimationHeader* sBremenMarchAnim = NULL;
static f32 sBremenMarchFrame = 0.0f;
static s32 sBremenBgmStarted = 0;
static s32 sBremenWallTurnLockout = 0;

// Mask of Scents state — transient
static LinkAnimationHeader* sScentsSniffAnim = NULL;
static f32 sScentsSniffFrame = 0.0f;
static s32 sScentsSniffActive = 0; // 1 = sniff anim playing (Link idle on ground)
static s32 sScentsPrevSfxFrame = -1; // last frame where we fired the pig-grunt SFX (avoids spam)
// Bremen Mask state — persistent (NOT cleared in MmMaskWear_Clear; cleared on death)
#define BREMEN_CHICK_GROWTH_FRAMES 1200 // 60 s @ 20 fps
static s32 sBremenWornTotalFrames = 0;
static s32 sBremenAdultCuccoSpawned = 0;

// Great Fairy Mask state
static s32 sGreatFairyMenuOpen = 0;
static s32 sGreatFairyMenuCursor = 0;
static s32 sGreatFairyInputSkip = 0; // Skip input on first frame (same-frame open/close guard)
static s8 sFairyStickHeld = 0;       // Analog stick debounce for map navigation
static s8 sFairyKaleidoInit = 0;     // Whether fairy kaleido has been initialized this session

// =============================================================================
// Fairy Kaleido: Pulse animation (same pattern as minish_kaleido)
// =============================================================================

static s16 sFairyPulsePrim[] = { 255, 150, 255 };
static s16 sFairyPulseTarget[][3] = {
    { 150, 255, 255 }, // Stage 0: Cyan
    { 255, 150, 255 }, // Stage 1: Pink
};
static s16 sFairyPulseStage = 0;
static s16 sFairyPulseTimer = 20;

static void FairyKaleido_UpdatePulse(void) {
    for (s32 c = 0; c < 3; c++) {
        s16 diff = sFairyPulseTarget[sFairyPulseStage][c] - sFairyPulsePrim[c];
        s16 step = diff / (sFairyPulseTimer > 0 ? sFairyPulseTimer : 1);
        sFairyPulsePrim[c] += step;
    }
    sFairyPulseTimer--;
    if (sFairyPulseTimer <= 0) {
        for (s32 c = 0; c < 3; c++)
            sFairyPulsePrim[c] = sFairyPulseTarget[sFairyPulseStage][c];
        sFairyPulseStage ^= 1;
        sFairyPulseTimer = 20;
    }
}

// =============================================================================
// Fairy Kaleido: Data tables for map overlay
// =============================================================================

// Stray Fairy textures from mm.o2r
static const char* sStrayFairyHeadTex = "__OTR__objects/gameplay_keep/gStrayFairyRightFacingHeadTex"; // IA8 32x32
static const char* sStrayFairyGlowTex = "__OTR__objects/gameplay_keep/gStrayFairyGlowTex";            // I4  16x16
static const char* sStrayFairyBodyTex = "__OTR__objects/gameplay_keep/gStrayFairyBodyTex";            // IA8 16x32
static const char* sStrayFairyWingTex = "__OTR__objects/gameplay_keep/gStrayFairyWingTex";            // IA8 16x16
// Pre-rendered full fairy sprite (RGBA32 32x24) — parameter_static
static const char* sStrayFairyFullTex = "__OTR__interface/parameter_static/gStrayFairyWoodfallIconTex"; // RGBA32 32x24
static const char* sStrayFairyGlowCircleTex =
    "__OTR__interface/parameter_static/gStrayFairyGlowingCircleIconTex"; // I4 32x24
static s32 sStrayFairyFullTexAvailable = -1;                             // -1=unknown, 0=no, 1=yes

// ---- Fairy warp animation state ----
static s32 sFairyWarpPhase = 0; // 0=none, 1=void-out (fading), 2=void-in (fading)
static s32 sFairyWarpTimer = 0;
static s8 sFairyWarpDestIdx = -1;

#define FAIRY_VOID_OUT_FRAMES 30 // ~1 second fade out
#define FAIRY_VOID_IN_FRAMES 30  // ~1 second fade in

// =============================================================================
// Great Fairy Mask - Hair Strand Physics (from MM z_player_lib.c:3297-3601)
// 3 strands × 3 chain links each. Segment 0x0B = 6 matrices (2 per strand).
// =============================================================================

// Chain link: position, velocity, orientation (from MM struct_801F58B0)
typedef struct {
    Vec3f pos;   // 0x00 - world position
    Vec3f vel;   // 0x0C - velocity
    s16 yaw;     // 0x18
    s16 pitch;   // 0x1A
} FairyHairLink; // size = 0x1C

static FairyHairLink sFairyHairStrands[3][3]; // 3 strands × 3 links
static s32 sFairyHairInited = 0;
static s32 sFairyHairActivated = 0;             // 1 = in Great Fairy Fountain (strands float up + particles)
static u32 sHairLastPhysicsFrame = 0xFFFFFFFFu; // Guard against double-draw per frame

// Strand root positions in head model space (from MM D_801C0C0C)
static Vec3f sHairRootPos[] = {
    { 174.0f, -1269.0f, -1.0f },
    { 401.0f, -729.0f, -701.0f },
    { 401.0f, -729.0f, 699.0f },
};

// Strand gravity targets (from MM D_801C0C30)
static Vec3f sHairTargetPos[] = {
    { 74.0f, -1269.0f, -1.0f },
    { 301.0f, -729.0f, -701.0f },
    { 301.0f, -729.0f, 699.0f },
};

// Chain constraint params (from MM D_801C0C54)
typedef struct {
    f32 length;    // 0x00
    s16 rotY;      // 0x04
    s16 rotX;      // 0x06
    Vec3f target;  // 0x08
    f32 maxLength; // 0x14
    s16 maxYaw;    // 0x18
    s16 maxPitch;  // 0x1A
} HairChainParam;  // size = 0x1C

static HairChainParam sHairChainParams[] = {
    { 0.0f, 0x0000, (s16)0x8000, { 0.0f, 0.0f, 0.0f }, 0.0f, 0x0000, 0x0000 },
    { 16.8f, 0x0000, 0x0000, { 0.0f, 0.0f, 0.0f }, 20.0f, 0x1388, 0x1388 },
    { 30.0f, 0x0000, 0x0000, { 0.0f, 0.0f, 0.0f }, 20.0f, 0x1F40, 0x2EE0 },
};

// D_801C0C00: offset for chain constraint target computation
static Vec3f sHairTargetOffset = { 0.0f, 20.0f, 0.0f };

// Initialize all strand links to a position (from MM func_80127B64)
static void FairyHair_Init(Vec3f* headPos) {
    for (s32 s = 0; s < 3; s++) {
        for (s32 i = 0; i < 3; i++) {
            Math_Vec3f_Copy(&sFairyHairStrands[s][i].pos, headPos);
            sFairyHairStrands[s][i].vel.x = 0.0f;
            sFairyHairStrands[s][i].vel.y = 0.0f;
            sFairyHairStrands[s][i].vel.z = 0.0f;
            sFairyHairStrands[s][i].yaw = 0;
            sFairyHairStrands[s][i].pitch = 0;
        }
    }
    sFairyHairInited = 1;
}

// Spawn sparkle particles when activated (from MM Player_DrawStrayFairyParticles)
static void FairyHair_SpawnParticles(PlayState* play, Vec3f* pos) {
    Vec3f sparkVel = { 0.0f, 0.3f, 0.0f };
    Vec3f sparkAccel = { 0.0f, -0.025f, 0.0f };
    Color_RGBA8 primColor = { 250, 100, 100, 0 };
    Color_RGBA8 envColor = { 0, 0, 100, 0 };
    Vec3f sparkPos;

    sparkVel.y = Rand_ZeroFloat(0.07f) + -0.1f;
    sparkAccel.y = Rand_ZeroFloat(0.1f) + 0.04f;

    f32 sign = (Rand_ZeroOne() < 0.5f) ? -1.0f : 1.0f;
    sparkVel.x = (Rand_ZeroFloat(0.2f) + 0.1f) * sign;
    sparkAccel.x = 0.1f * sign;

    sign = (Rand_ZeroOne() < 0.5f) ? -1.0f : 1.0f;
    sparkVel.z = (Rand_ZeroFloat(0.2f) + 0.1f) * sign;
    sparkAccel.z = 0.1f * sign;

    sparkPos.x = pos->x;
    sparkPos.y = Rand_ZeroFloat(15.0f) + pos->y;
    sparkPos.z = pos->z;

    EffectSsKiraKira_SpawnDispersed(play, &sparkPos, &sparkVel, &sparkAccel, &primColor, &envColor, -50, 11);
}

// VERBATIM port of MM func_80127DA4 (z_player_lib.c:3437-3542)
// Only changes: struct field names, activation check, Atan2S_XY→Atan2S swap
static void FairyHair_UpdateStrand(PlayState* play, FairyHairLink arg1[], HairChainParam arg2[], s32 arg3, Vec3f* arg4,
                                   Vec3f* arg5, u32* arg6) {
    FairyHairLink* phi_s1 = &arg1[1];
    Vec3f spB0;
    Vec3f spA4;
    f32 f22;
    f32 f28;
    f32 f24;
    f32 f20;
    f32 f0;
    f32 sp8C = -1.0f;
    s32 i;
    s16 s0;
    s16 s2;

    Math_Vec3f_Copy(&arg1->pos, arg4);
    Math_Vec3f_Diff(arg5, arg4, &spB0);
    // Math_Atan2S_XY(x,y) = Math_Atan2S(y,x)
    arg1->yaw = Math_Atan2S(spB0.x, spB0.z);
    arg1->pitch = Math_Atan2S(spB0.y, sqrtf(SQ(spB0.x) + SQ(spB0.z)));
    i = 1;
    arg2++;

    while (i < arg3) {
        // Save previous frame's angles for 1°/frame rotation limiter
        s16 oldYaw = phi_s1->yaw;
        s16 oldPitch = phi_s1->pitch;

        if (sFairyHairActivated) {
            if (*arg6 & 0x20) {
                sp8C = -0.2f;
            } else {
                sp8C = 0.2f;
            }

            *arg6 += 0x16;
            if (!(*arg6 & 1)) {
                FairyHair_SpawnParticles(play, &phi_s1->pos);
            }
        }
        Math_Vec3f_Sum(&phi_s1->pos, &phi_s1->vel, &phi_s1->pos);

        f0 = Math_Vec3f_DistXYZAndStoreDiff(&arg1->pos, &phi_s1->pos, &spB0);
        f28 = f0 - arg2->length;
        if (f0 == 0.0f) {
            spB0.x = 0.0f;
            spB0.y = arg2->length;
            spB0.z = 0.0f;
        }
        f20 = sqrtf(SQ(spB0.x) + SQ(spB0.z));

        if (f20 > 4.0f) {
            phi_s1->yaw = Math_Atan2S(spB0.x, spB0.z);
            s2 = phi_s1->yaw - arg1->yaw;

            if (ABS(s2) > 0x4000) {
                phi_s1->yaw = (s16)(phi_s1->yaw + 0x8000);
                f20 = -f20;
            }
        }

        phi_s1->pitch = Math_Atan2S(spB0.y, f20);

        s2 = phi_s1->yaw - arg1->yaw;
        s2 = CLAMP(s2, -arg2->maxYaw, arg2->maxYaw);
        phi_s1->yaw = arg1->yaw + s2;

        s0 = phi_s1->pitch - arg1->pitch;
        s0 = CLAMP(s0, -arg2->maxPitch, arg2->maxPitch);
        phi_s1->pitch = arg1->pitch + s0;

        // Hard 1°/frame rotation limiter (~182 binang units = 1°)
        {
            s16 deltaYaw = (s16)(phi_s1->yaw - oldYaw);
            s16 deltaPitch = (s16)(phi_s1->pitch - oldPitch);
            deltaYaw = CLAMP(deltaYaw, -0x00B6, 0x00B6);
            deltaPitch = CLAMP(deltaPitch, -0x00B6, 0x00B6);
            phi_s1->yaw = oldYaw + deltaYaw;
            phi_s1->pitch = oldPitch + deltaPitch;
            // Recompute relative angles so velocity uses clamped values
            s2 = phi_s1->yaw - arg1->yaw;
            s0 = phi_s1->pitch - arg1->pitch;
        }

        f20 = Math_CosS(phi_s1->pitch) * arg2->length;
        spA4.x = Math_SinS(phi_s1->yaw) * f20;
        spA4.z = Math_CosS(phi_s1->yaw) * f20;
        spA4.y = Math_SinS(phi_s1->pitch) * arg2->length;
        Math_Vec3f_Sum(&arg1->pos, &spA4, &phi_s1->pos);
        phi_s1->vel.x *= 0.9f;
        phi_s1->vel.z *= 0.9f;

        f22 = Math_CosS(s0) * f28;
        f24 = Math_SinS(s0) * f28;
        phi_s1->vel.y += sp8C;

        if (sFairyHairActivated) {
            phi_s1->vel.y = CLAMP(phi_s1->vel.y, -0.8f, 0.8f);
        } else {
            f20 = Math_SinS(arg1->pitch);
            phi_s1->vel.y += (((f22 * Math_CosS(arg1->pitch)) + (f24 * f20)) * 0.2f);
            phi_s1->vel.y = CLAMP(phi_s1->vel.y, -2.0f, 4.0f);
        }

        f20 = (f24 * Math_CosS(arg1->pitch)) - (Math_SinS(arg1->pitch) * f22);
        f22 = Math_CosS(s2) * f20;
        f24 = Math_SinS(s2) * f20;

        f20 = Math_SinS(arg1->yaw);

        phi_s1->vel.x += (((f24 * Math_CosS(arg1->yaw)) - (f22 * f20)) * 0.1f);
        phi_s1->vel.x = CLAMP(phi_s1->vel.x, -4.0f, 4.0f);

        f20 = Math_SinS(arg1->yaw);

        phi_s1->vel.z += (((f22 * Math_CosS(arg1->yaw)) + (f24 * f20)) * -0.1f);
        phi_s1->vel.z = CLAMP(phi_s1->vel.z, -4.0f, 4.0f);

        arg1++;
        phi_s1++;
        i++;
        arg2++;
    }
}

// Convert strand angles to Mtx for segment 0x0B (from MM func_80128388)
// VERBATIM port of MM func_80128388 (z_player_lib.c:3545-3566)
static void FairyHair_ComputeStrandMatrices(FairyHairLink arg0[], HairChainParam arg1[], s32 arg2, Mtx** arg3) {
    FairyHairLink* phi_s1 = &arg0[1];
    Vec3f sp58;
    Vec3s sp50;
    s32 i;

    sp58.y = 0.0f;
    sp58.z = 0.0f;
    sp50.x = 0;

    for (i = 1; i < arg2; i++) {
        sp58.x = arg1->length * 100.0f;
        sp50.z = arg1->rotX + (s16)(phi_s1->pitch - arg0->pitch);
        sp50.y = arg1->rotY + (s16)(phi_s1->yaw - arg0->yaw);
        Matrix_TranslateRotateZYX(&sp58, &sp50);
        Matrix_ToMtx(*arg3, (char*)__FILE__, __LINE__);
        (*arg3)++;
        arg0++;
        phi_s1++;
        arg1++;
    }
}

// Full draw: compute all 3 strands' matrices for segment 0x0B (from MM Player_DrawGreatFairysMask)
static void FairyHair_ComputeMatrices(PlayState* play, Player* player, Mtx* mtxBuffer) {
    Vec3f rootWorld, targetWorld;
    // Use play->gameplayFrames directly, like MM does (sp6C = play->gameplayFrames)
    u32 frame = play->gameplayFrames;

    // Only run physics simulation ONCE per game frame.
    // SoH may call player draw multiple times per frame (reflections, pause, etc.)
    // which would cause double-speed physics if not guarded.
    s32 doPhysics = (play->gameplayFrames != sHairLastPhysicsFrame);
    if (doPhysics) {
        sHairLastPhysicsFrame = play->gameplayFrames;
    }

    // Update constraint targets from model matrix
    Matrix_MultVec3f(&sHairTargetOffset, &sHairChainParams[1].target);
    {
        Vec3f* head = &player->bodyPartsPos[PLAYER_BODYPART_HEAD];
        Vec3f* waist = &player->bodyPartsPos[PLAYER_BODYPART_WAIST];
        sHairChainParams[2].target.x = head->x + (waist->x - head->x) * 0.2f;
        sHairChainParams[2].target.y = head->y + (waist->y - head->y) * 0.2f;
        sHairChainParams[2].target.z = head->z + (waist->z - head->z) * 0.2f;
    }

    for (s32 i = 0; i < 3; i++) {
        Matrix_MultVec3f(&sHairRootPos[i], &rootWorld);
        Matrix_MultVec3f(&sHairTargetPos[i], &targetWorld);

        if (doPhysics) {
            FairyHair_UpdateStrand(play, sFairyHairStrands[i], sHairChainParams, 3, &rootWorld, &targetWorld, &frame);
            frame += 11;
        }

        Matrix_Push();
        Matrix_Translate(sHairRootPos[i].x, sHairRootPos[i].y, sHairRootPos[i].z, MTXMODE_APPLY);
        FairyHair_ComputeStrandMatrices(sFairyHairStrands[i], sHairChainParams, 3, &mtxBuffer);
        Matrix_Pop();
    }
}

// Fountain positions on the OOT world map (kaleido coordinates)
typedef struct {
    s16 centerX;
    s16 centerY;
    const char* nameTex[4]; // [ENG, GER, FRA, JPN]
} FairyKaleidoData;

#define FAIRY_BOX_HW 12
#define FAIRY_BOX_HH 8
#define FAIRY_FOUNTAIN_COUNT 6

static const FairyKaleidoData sFairyAreaData[FAIRY_FOUNTAIN_COUNT] = {
    // #0 DMT - Magic
    { 32,
      42,
      { gDeathMountainTrailPositionNameENGTex, gDeathMountainTrailPositionNameGERTex,
        gDeathMountainTrailPositionNameFRATex, gDeathMountainTrailPositionNameJPNTex } },
    // #1 DMC - Double Magic
    { 42,
      52,
      { gDeathMountainCraterPositionNameENGTex, gDeathMountainCraterPositionNameGERTex,
        gDeathMountainCraterPositionNameFRATex, gDeathMountainCraterPositionNameJPNTex } },
    // #2 OGC - Defense
    { 12,
      24,
      { gGanonsCastlePositionNameENGTex, gGanonsCastlePositionNameGERTex, gGanonsCastlePositionNameFRATex,
        gGanonsCastlePositionNameJPNTex } },
    // #3 ZF - Farore's Wind
    { 82,
      26,
      { gZorasFountainPositionNameENGTex, gZorasFountainPositionNameGERTex, gZorasFountainPositionNameFRATex,
        gZorasFountainPositionNameJPNTex } },
    // #4 HC - Din's Fire
    { -2,
      14,
      { gHyruleCastlePositionNameENGTex, gHyruleCastlePositionNameGERTex, gHyruleCastlePositionNameFRATex,
        gHyruleCastlePositionNameJPNTex } },
    // #5 Colossus - Nayru's Love
    { -90,
      28,
      { gDesertColossusPositionNameENGTex, gDesertColossusPositionNameGERTex, gDesertColossusPositionNameFRATex,
        gDesertColossusPositionNameJPNTex } },
};

// Pedestal positions per fountain (where Link plays Zelda's Lullaby)
// daiyousei_izumi (magic upgrades: DMT/DMC/OGC): (-22, 10, -798)
// yousei_izumi_yoko (spell upgrades: ZF/HC/Colossus): (-21, 10, -802)
static const Vec3f sFairyPedestalPos[FAIRY_FOUNTAIN_COUNT] = {
    { -22.0f, 10.0f, -798.0f }, // DMT
    { -22.0f, 10.0f, -798.0f }, // DMC
    { -22.0f, 10.0f, -798.0f }, // OGC
    { -21.0f, 10.0f, -802.0f }, // ZF
    { -21.0f, 10.0f, -802.0f }, // HC
    { -21.0f, 10.0f, -802.0f }, // Colossus
};

// Coordinate conversion: kaleido coords → screen 10.2 fixed-point (1.2x scale, centered)
#define FK_YSHIFT 96
#define FKX(kx) (640 + (s32)(kx)*24 / 5)
#define FKY(ky) (480 - FK_YSHIFT - (s32)(ky)*24 / 5)

// Map frame textures (same as minish_kaleido / z_kaleido_scope_PAL.c)
static const char* sFairyMapENGTexs[] = {
    gPauseMap00Tex,    gPauseMap01Tex, gPauseMap02Tex, gPauseMap03Tex, gPauseMap04Tex,
    gPauseMap10ENGTex, gPauseMap11Tex, gPauseMap12Tex, gPauseMap13Tex, gPauseMap14Tex,
    gPauseMap20Tex,    gPauseMap21Tex, gPauseMap22Tex, gPauseMap23Tex, gPauseMap24Tex,
};
static const char* sFairyMapGERTexs[] = {
    gPauseMap00Tex,    gPauseMap01Tex, gPauseMap02Tex, gPauseMap03Tex, gPauseMap04Tex,
    gPauseMap10GERTex, gPauseMap11Tex, gPauseMap12Tex, gPauseMap13Tex, gPauseMap14Tex,
    gPauseMap20Tex,    gPauseMap21Tex, gPauseMap22Tex, gPauseMap23Tex, gPauseMap24Tex,
};
static const char* sFairyMapFRATexs[] = {
    gPauseMap00Tex,    gPauseMap01Tex, gPauseMap02Tex, gPauseMap03Tex, gPauseMap04Tex,
    gPauseMap10FRATex, gPauseMap11Tex, gPauseMap12Tex, gPauseMap13Tex, gPauseMap14Tex,
    gPauseMap20Tex,    gPauseMap21Tex, gPauseMap22Tex, gPauseMap23Tex, gPauseMap24Tex,
};
static const char* sFairyMapJPNTexs[] = {
    gPauseMap00Tex,    gPauseMap01Tex, gPauseMap02Tex, gPauseMap03Tex, gPauseMap04Tex,
    gPauseMap10JPNTex, gPauseMap11Tex, gPauseMap12Tex, gPauseMap13Tex, gPauseMap14Tex,
    gPauseMap20Tex,    gPauseMap21Tex, gPauseMap22Tex, gPauseMap23Tex, gPauseMap24Tex,
};
static const char** sFairyMapTexs[] = { sFairyMapENGTexs, sFairyMapGERTexs, sFairyMapFRATexs, sFairyMapJPNTexs };

// Cloud textures and flag numbers (from z_kaleido_map_PAL.c)
static const char* sFairyCloudTexs[] = {
    gWorldMapCloud16Tex, gWorldMapCloud15Tex, gWorldMapCloud14Tex, gWorldMapCloud13Tex,
    gWorldMapCloud12Tex, gWorldMapCloud11Tex, gWorldMapCloud10Tex, gWorldMapCloud9Tex,
    gWorldMapCloud8Tex,  gWorldMapCloud7Tex,  gWorldMapCloud6Tex,  gWorldMapCloud5Tex,
    gWorldMapCloud4Tex,  gWorldMapCloud3Tex,  gWorldMapCloud2Tex,  gWorldMapCloud1Tex,
};
static u16 sFairyCloudFlagNums[] = {
    0x05, 0x00, 0x13, 0x0E, 0x0F, 0x01, 0x02, 0x10, 0x12, 0x03, 0x07, 0x08, 0x09, 0x0C, 0x0B, 0x06,
};
static s16 sFairyCloudWidths[] = {
    32, 112, 32, 48, 32, 32, 32, 48, 32, 64, 32, 48, 48, 48, 48, 64,
};
static s16 sFairyCloudHeights[] = {
    24, 72, 13, 22, 19, 20, 19, 27, 14, 26, 22, 21, 49, 32, 45, 60,
};
static s16 sFairyCloudPosX[] = {
    0x002F, 0xFFCF, 0xFFEF, 0xFFF1, 0xFFF7, 0x0018, 0x002B, 0x000E,
    0x0009, 0x0026, 0x0052, 0x0047, 0xFFB4, 0xFFA9, 0xFF94, 0xFFCA,
};
static s16 sFairyCloudPosY[] = {
    0x000F, 0x0028, 0x000B, 0x002D, 0x0034, 0x0025, 0x0024, 0x0039,
    0x0036, 0x0021, 0x001F, 0x002D, 0x0020, 0x002A, 0x0031, 0xFFF6,
};

// "CURRENT POSITION" title textures per language
static const char* sFairyCurrentPosTitleTexs[] = {
    gPauseCurrentPositionENGTex,
    gPauseCurrentPositionGERTex,
    gPauseCurrentPositionFRATex,
    gPauseCurrentPositionJPNTex,
};

// =============================================================================
// Fairy Kaleido: Nearest-neighbor navigation (same algorithm as minish_kaleido)
// =============================================================================

static s8 FairyKaleido_FindNearest(s8 currentIdx, s16 stickX, s16 stickY) {
    f32 stickMag = sqrtf((f32)(stickX * stickX + stickY * stickY));
    if (stickMag < 30.0f)
        return -1;

    f32 stickAngle = atan2f((f32)stickX, (f32)stickY);
    f32 curCX = sFairyAreaData[currentIdx].centerX;
    f32 curCY = sFairyAreaData[currentIdx].centerY;

    s8 bestIdx = -1;
    f32 bestScore = -1.0f;

    for (s32 i = 0; i < FAIRY_FOUNTAIN_COUNT; i++) {
        if (i == currentIdx)
            continue;

        f32 dx = sFairyAreaData[i].centerX - curCX;
        f32 dy = sFairyAreaData[i].centerY - curCY;
        f32 dist = sqrtf(dx * dx + dy * dy);
        if (dist < 1.0f)
            continue;

        f32 candidateAngle = atan2f(dx, dy);
        f32 angleDiff = candidateAngle - stickAngle;

        while (angleDiff > M_PI)
            angleDiff -= 2.0f * M_PI;
        while (angleDiff < -M_PI)
            angleDiff += 2.0f * M_PI;
        if (angleDiff < 0)
            angleDiff = -angleDiff;

        if (angleDiff > M_PI / 2.0f)
            continue;

        f32 score = cosf(angleDiff) / dist;
        if (score > bestScore) {
            bestScore = score;
            bestIdx = i;
        }
    }

    return bestIdx;
}

// Discovery check for a fountain (reuses sGreatFairyFountains table)
static s32 FairyKaleido_IsDiscovered(s32 idx);

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

// Randomizer inf flags for each fountain (same order as sGreatFairyFountains)
static const RandomizerInf sFairyFountainRandoInf[] = {
    RAND_INF_DMT_GREAT_FAIRY_REWARD,     // 0: DMT - Magic
    RAND_INF_DMC_GREAT_FAIRY_REWARD,     // 1: DMC - Double Magic
    RAND_INF_OGC_GREAT_FAIRY_REWARD,     // 2: OGC - Defense
    RAND_INF_ZF_GREAT_FAIRY_REWARD,      // 3: ZF - Farore
    RAND_INF_HC_GREAT_FAIRY_REWARD,      // 4: HC - Din
    RAND_INF_COLOSSUS_GREAT_FAIRY_REWARD // 5: Colossus - Nayru
};

// Implementation of FairyKaleido_IsDiscovered (forward-declared above, needs sGreatFairyFountains)
static s32 FairyKaleido_IsDiscovered(s32 idx) {
    if (idx < 0 || idx >= FAIRY_FOUNTAIN_COUNT)
        return 0;
    // In randomizer, check the rando inf flag (vanilla flags may not be set)
    if (IS_RANDO)
        return Flags_GetRandomizerInf(sFairyFountainRandoInf[idx]);
    // Vanilla: check original flags
    if (sGreatFairyFountains[idx].flagType == 0)
        return gSaveContext.isMagicAcquired;
    return Flags_GetItemGetInf(sGreatFairyFountains[idx].flagValue);
}

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
                        gs->params);
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
    // Kafei Mask Transform: toggle between Kafei model and Link (no visible mask on face)
    if (itemId == ITEM_MM_MASK_KAFEI && CVarGetInteger("gMods.KafeiMaskTransform", 0)) {
        if (PakLoader_HasForcedModel()) {
            PakLoader_ClearForcedModel();
        } else {
            PakLoader_ForceModel("nei/N64_Kafei.pak");
        }
        Player_PlaySfx(&player->actor, NA_SE_PL_CHANGE_ARMS);
        player->stateFlags2 |= PLAYER_STATE2_FOOTSTEP;
        return;
    }

    s32 idx = MaskItemToIndex(itemId);
    if (idx < 0 || idx >= MM_MASK_COUNT) {
        return;
    }

    if (sCurrentMmMask == itemId) {
        // Already wearing this mask - take it off
        sCurrentMmMask = ITEM_NONE;
        if (sKamaroDancing || sBremenBgmStarted) {
            Audio_QueueSeqCmd(NA_BGM_STOP);
            sBremenBgmStarted = 0;
        }
        sKamaroDancing = 0;
        sBremenMarching = 0;
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

    bool isTransformation = (idx == MM_MASK_IDX_DEKU || idx == MM_MASK_IDX_GORON ||
                             idx == MM_MASK_IDX_ZORA || idx == MM_MASK_IDX_FIERCE_DEITY);
    if (!isTransformation && CVarGetInteger(CVAR_ENHANCEMENT("HideNonTransformationMasks"), 0)) {
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
            // Great Fairy Mask: DL references segment 0x0B for 6 hair strand matrices
            // (3 strands × 2 joints each, computed by FairyHair_ComputeMatrices)
            // Also references 0x0D for head limb matrix (already set by player draw)
            // =================================================================
            Mtx* leafMtx = (Mtx*)Graph_Alloc(play->state.gfxCtx, 6 * sizeof(Mtx));
            if (leafMtx != NULL) {
                if (sFairyHairInited) {
                    FairyHair_ComputeMatrices(play, player, leafMtx);
                } else {
                    // Fallback: identity matrices until physics initialized
                    for (s32 i = 0; i < 6; i++) {
                        Matrix_ToMtx(&leafMtx[i], (char*)__FILE__, __LINE__);
                    }
                }
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
            case 0: // Postman's Hat — interaction happens via the mailbox
                    // actor's A-press (see soh/mods/items/helpers/mailbox_actor.c).
                    // The hat itself does nothing on B while worn.
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
                                                      bombPos.z, 0, 0, 0, BOMB_BODY);

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
            case 4: // Great Fairy Mask — hair physics + A to claim reward in fountain + B for teleport
            {
                // === Hair Physics: init on first frame, update every frame ===
                sFairyHairActivated = (play->sceneNum == SCENE_GREAT_FAIRYS_FOUNTAIN_MAGIC ||
                                       play->sceneNum == SCENE_GREAT_FAIRYS_FOUNTAIN_SPELLS);
                if (!sFairyHairInited) {
                    FairyHair_Init(&player->bodyPartsPos[PLAYER_BODYPART_HEAD]);
                }

                // === Fairy Warp Void-Out: fade Link transparent + sparkle particles ===
                if (sFairyWarpPhase == 1) {
                    sFairyWarpTimer++;
                    f32 t = (f32)sFairyWarpTimer / (f32)FAIRY_VOID_OUT_FRAMES;
                    if (t > 1.0f)
                        t = 1.0f;

                    // Fade Link's alpha from 255 → 0
                    player->actor.shape.shadowAlpha = (u8)(255.0f * (1.0f - t));

                    // Spawn orbiting sparkle particles (MM EnElforg_CirclePlayer pattern)
                    {
                        Vec3f sparkVel = { 0.0f, 0.3f, 0.0f };
                        Vec3f sparkAccel = { 0.0f, -0.025f, 0.0f };
                        Color_RGBA8 primColor = { 250, 100, 100, 0 };
                        Color_RGBA8 envColor = { 0, 0, 100, 0 };

                        for (s32 i = 0; i < 5; i++) {
                            s16 angle = (s16)(sFairyWarpTimer * 0x1000 + i * (0x10000 / 5));
                            f32 radius = 20.0f;
                            Vec3f sparkPos;
                            sparkPos.x = player->actor.world.pos.x + Math_SinS(angle) * radius;
                            sparkPos.z = player->actor.world.pos.z + Math_CosS(angle) * radius;
                            sparkPos.y = player->bodyPartsPos[PLAYER_BODYPART_WAIST].y +
                                         8.0f * Math_SinS((s16)(sFairyWarpTimer * 0x200 + i * 0x2000));
                            EffectSsKiraKira_SpawnDispersed(play, &sparkPos, &sparkVel, &sparkAccel, &primColor,
                                                            &envColor, -50, 11);
                        }
                    }

                    if (sFairyWarpTimer >= FAIRY_VOID_OUT_FRAMES) {
                        // Fade done — trigger the scene transition
                        sFairyWarpPhase = 2;
                        sFairyWarpTimer = 0;

                        s8 destIdx = sFairyWarpDestIdx;
                        if (destIdx >= 0 && destIdx < FAIRY_FOUNTAIN_COUNT) {
                            play->nextEntranceIndex = sGreatFairyFountains[destIdx].entrance;
                            play->transitionTrigger = TRANS_TRIGGER_START;
                            play->transitionType = TRANS_TYPE_FADE_WHITE;
                            gSaveContext.nextTransitionType = TRANS_TYPE_FADE_WHITE_FAST;

                            gSaveContext.respawn[RESPAWN_MODE_TOP].entranceIndex =
                                sGreatFairyFountains[destIdx].entrance;
                            gSaveContext.respawn[RESPAWN_MODE_TOP].pos.x = sFairyPedestalPos[destIdx].x;
                            gSaveContext.respawn[RESPAWN_MODE_TOP].pos.y = sFairyPedestalPos[destIdx].y;
                            gSaveContext.respawn[RESPAWN_MODE_TOP].pos.z = sFairyPedestalPos[destIdx].z;
                            gSaveContext.respawn[RESPAWN_MODE_TOP].yaw = 0;
                            gSaveContext.respawn[RESPAWN_MODE_TOP].playerParams = 0xDFF;
                            gSaveContext.respawn[RESPAWN_MODE_TOP].roomIndex = 0;
                            gSaveContext.respawnFlag = 3;
                        }
                    }
                    break; // Skip all other case 4 logic during void-out
                }

                // === Fairy Warp Void-In: fade Link back in + sparkle particles ===
                if (sFairyWarpPhase == 2) {
                    sFairyWarpTimer++;

                    // Play Great Fairy appear sound on first frame of void-in
                    if (sFairyWarpTimer == 1) {
                        player->actor.shape.shadowAlpha = 0;
                        Audio_PlaySoundGeneral(NA_SE_EV_GREAT_FAIRY_APPEAR, &gSfxDefaultPos, 4,
                                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                               &gSfxDefaultReverb);
                    }

                    f32 t = (f32)sFairyWarpTimer / (f32)FAIRY_VOID_IN_FRAMES;
                    if (t > 1.0f)
                        t = 1.0f;

                    // Fade Link's alpha from 0 → 255
                    player->actor.shape.shadowAlpha = (u8)(255.0f * t);

                    // Spawn orbiting sparkle particles
                    {
                        Vec3f sparkVel = { 0.0f, 0.3f, 0.0f };
                        Vec3f sparkAccel = { 0.0f, -0.025f, 0.0f };
                        Color_RGBA8 primColor = { 250, 100, 100, 0 };
                        Color_RGBA8 envColor = { 0, 0, 100, 0 };

                        for (s32 i = 0; i < 5; i++) {
                            s16 angle = (s16)(sFairyWarpTimer * 0x1000 + i * (0x10000 / 5));
                            f32 radius = 20.0f;
                            Vec3f sparkPos;
                            sparkPos.x = player->actor.world.pos.x + Math_SinS(angle) * radius;
                            sparkPos.z = player->actor.world.pos.z + Math_CosS(angle) * radius;
                            sparkPos.y = player->bodyPartsPos[PLAYER_BODYPART_WAIST].y +
                                         8.0f * Math_SinS((s16)(sFairyWarpTimer * 0x200 + i * 0x2000));
                            EffectSsKiraKira_SpawnDispersed(play, &sparkPos, &sparkVel, &sparkAccel, &primColor,
                                                            &envColor, -50, 11);
                        }
                    }

                    if (sFairyWarpTimer >= FAIRY_VOID_IN_FRAMES) {
                        // Fade in done — restore full alpha and end warp
                        player->actor.shape.shadowAlpha = 255;
                        sFairyWarpPhase = 0;
                        sFairyWarpTimer = 0;
                        sFairyWarpDestIdx = -1;
                    }
                    break; // Skip all other case 4 logic during void-in
                }

                if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE))
                    break;

                // A) In fountain scenes: A press triggers reward (sets switch flag 0x38)
                if (play->sceneNum == SCENE_GREAT_FAIRYS_FOUNTAIN_MAGIC ||
                    play->sceneNum == SCENE_GREAT_FAIRYS_FOUNTAIN_SPELLS) {
                    if (CHECK_BTN_ALL(play->state.input[0].press.button, BTN_A)) {
                        Flags_SetSwitch(play, 0x38);
                    }
                    // Don't break — fall through to B check so teleport works from inside fountains
                }

                // B) B press opens teleport menu (works both inside and outside fountains)
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
            case 7: // Bremen Mask — 1:1 MM march + chick/cucco follower
            {
                // === Persistent wear-time counter (cucco upgrade gate) ===
                // Cap at threshold + 10 so we never overflow but still drive
                // the upgrade exactly once.
                if (sBremenWornTotalFrames < BREMEN_CHICK_GROWTH_FRAMES + 10) {
                    sBremenWornTotalFrames++;
                }

                // === Threshold transition: chick → adult cucco ===
                if (sBremenWornTotalFrames >= BREMEN_CHICK_GROWTH_FRAMES && !sBremenAdultCuccoSpawned) {
                    BremenFollower_UpgradeToAdult(play, player);
                    sBremenAdultCuccoSpawned = 1;
                }

                // === Follower tick (spawn/respawn + trail) ===
                BremenFollower_Tick(play, player);

                // === Bail during cutscenes / dead / loading ===
                if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE |
                                           PLAYER_STATE1_LOADING | PLAYER_STATE1_IN_ITEM_CS |
                                           PLAYER_STATE1_GETTING_ITEM)) {
                    sBremenMarching = 0;
                    if (sBremenBgmStarted) {
                        Audio_QueueSeqCmd(NA_BGM_STOP);
                        sBremenBgmStarted = 0;
                    }
                    break;
                }

                // === Start march on B press (ground + not already marching) ===
                if (!sBremenMarching &&
                    (player->actor.bgCheckFlags & 1 /* BGCHECKFLAG_GROUND */) &&
                    CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B)) {

                    // Try to load the MM march anim. NOTE: the OOT anim
                    // `gPlayerAnim_clink_normal_okarina_walk` doesn't exist as
                    // a static symbol in SoH (it was MM-only) and the MM_ANIM
                    // enum entry currently has no path mapping in mm_anims_data.c,
                    // so this load may return NULL — the march still starts.
                    if (sBremenMarchAnim == NULL && MmAssets_IsAvailable()) {
                        sBremenMarchAnim = MmAnim_Load(MM_ANIM_CLINK_NORMAL_OKARINA_WALK);
                    }

                    // Activate march regardless of anim availability. Animation
                    // is cosmetic; the actual march mechanic (auto-walk +
                    // wall-bonk-spin) is independent.
                    sBremenMarching = 1;
                    sBremenMarchFrame = 0.0f;
                    sBremenWallTurnLockout = 0;
                    player->itemAction = PLAYER_IA_OCARINA_OF_TIME;
                    if (!sBremenBgmStarted) {
                        // BGM playback for the Bremen march. The current MM
                        // audio loader (mm_asset_loader.cpp) only handles
                        // single-sample SFX from Soundfont_0 — it can't play
                        // MM sequence files like NA_BGM_BREMEN_MARCH (0x53 in
                        // MM). Until an MM seq loader exists, use OOT's
                        // Kaepora Gaebora theme as a marchy placeholder
                        // fanfare. Swap to MM_BGM_BREMEN_MARCH once
                        // MmDirectAudio supports sequences.
                        Audio_PlayFanfare(NA_BGM_OWL);
                        sBremenBgmStarted = 1;
                    }
                }

                // === While marching: animate + fixed forward speed + wall bonk ===
                if (sBremenMarching) {
                    // Release B = stop (1:1 MM stop condition)
                    if (!CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_B)) {
                        sBremenMarching = 0;
                        if (sBremenBgmStarted) {
                            // Stop the placeholder OOT BGM. NA_BGM_STOP works
                            // for both Audio_PlayFanfare and Audio_QueueSeqCmd
                            // started tracks.
                            Audio_QueueSeqCmd(NA_BGM_STOP);
                            sBremenBgmStarted = 0;
                        }
                        break;
                    }

                    // Drive MM march anim (Kamaro pattern: PlayLoop + SetLoadFrame + manual frame).
                    if (sBremenMarchAnim != NULL) {
                        LinkAnimation_PlayLoop(play, &player->skelAnime, sBremenMarchAnim);
                        player->skelAnime.curFrame = sBremenMarchFrame;
                        AnimationContext_SetLoadFrame(play, sBremenMarchAnim, (s32)sBremenMarchFrame,
                                                      player->skelAnime.limbCount, player->skelAnime.jointTable);
                        sBremenMarchFrame += 1.0f;
                        if (sBremenMarchFrame >= player->skelAnime.animLength) {
                            sBremenMarchFrame = 0.0f;
                        }
                    }

                    // NOTE: MM's PLAYER_STATE3_20000000 is (1 << 29), which would
                    // overflow OOT's u8 stateFlags3. The sBremenMarching static +
                    // MmMaskWear_IsBremenMarching() query provide the routing
                    // signal for the z_player.c stick-zero hook instead.

                    // Oscillating march speed — matches MM Player_Action_11:
                    //   speedXZ *= cos(frame * 1000) * 0.4f
                    // This creates the rhythmic step-step-step movement.
                    f32 yaw = player->actor.shape.rot.y;
                    const f32 BREMEN_BASE_SPEED = 6.0f;
                    f32 frame = sBremenMarchFrame; // animation phase (0..animLength)
                    f32 cycle = Math_CosS((s16)((s32)(frame * 1000.0f) & 0xFFFF)) * 0.4f;
                    f32 step = BREMEN_BASE_SPEED * (0.6f + cycle); // never below 0.2 * BASE
                    if (step < 1.0f) step = 1.0f;                  // floor so Link still moves
                    player->linearVelocity = step;
                    player->actor.velocity.x = Math_SinS(yaw) * step;
                    player->actor.velocity.z = Math_CosS(yaw) * step;
                    player->yaw = yaw;

                    // Even without the anim, advance the phase for the speed cycle.
                    if (sBremenMarchAnim == NULL) {
                        sBremenMarchFrame += 1.0f;
                        if (sBremenMarchFrame >= 30.0f) sBremenMarchFrame = 0.0f;
                    }

                    // Wall bonk → spin 180° and keep marching (MM 1:1).
                    if (sBremenWallTurnLockout > 0) {
                        sBremenWallTurnLockout--;
                    } else if (player->actor.bgCheckFlags & 8 /* BGCHECKFLAG_WALL */) {
                        player->actor.shape.rot.y += 0x8000;
                        player->yaw = player->actor.shape.rot.y;
                        sBremenWallTurnLockout = 8; // ~0.4 s debounce
                    }
                }
                break;
            }
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
            case 10: // Mask of Scents — sniff fidget anim + SFX + Lost Woods spots
            {
                // === Sniff fidget anim (MM gPlayerAnim_cl_msbowait, 1:1) ===
                // Plays while Link is on the ground, idle (no velocity, no
                // cutscene), no item-use action. Override the idle every frame
                // with our tracked phase, same Kamaro pattern.
                u8 onGround = (player->actor.bgCheckFlags & 1) != 0;
                u8 idle = (player->linearVelocity < 0.5f) && onGround;
                u8 blocked = (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE |
                                                     PLAYER_STATE1_LOADING | PLAYER_STATE1_IN_ITEM_CS |
                                                     PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_TALKING)) != 0;

                if (idle && !blocked) {
                    // Lazy-load the MM sniff anim.
                    if (sScentsSniffAnim == NULL && MmAssets_IsAvailable()) {
                        sScentsSniffAnim = MmAnim_Load(MM_ANIM_CL_MSBOWAIT);
                    }
                    if (sScentsSniffAnim != NULL) {
                        if (!sScentsSniffActive) {
                            sScentsSniffActive = 1;
                            sScentsSniffFrame = 0.0f;
                            sScentsPrevSfxFrame = -1;
                        }
                        LinkAnimation_PlayLoop(play, &player->skelAnime, sScentsSniffAnim);
                        player->skelAnime.curFrame = sScentsSniffFrame;
                        AnimationContext_SetLoadFrame(play, sScentsSniffAnim, (s32)sScentsSniffFrame,
                                                      player->skelAnime.limbCount, player->skelAnime.jointTable);

                        // === Pig-grunt SFX at MM frames 4 / 12 / 30 / 61 / 68 ===
                        // sFidgetAnimSfxPigGrunt in MM uses NA_SE_VO_LI_POO_WAIT
                        // (voicebank index 0x21 → MM SFX ID 0x6821). Loaded from
                        // mm.o2r's Soundfont_0 via the same MmSfx path that the
                        // Goron/Zora/Deku/FD forms use for their voice SFX.
                        s32 fi = (s32)sScentsSniffFrame;
                        if (fi != sScentsPrevSfxFrame) {
                            if (fi == 4 || fi == 12 || fi == 30 || fi == 61 || fi == 68) {
                                if (MmSfx_IsAvailable()) {
                                    MmSfx_PlayAtPos(MM_NA_SE_VO_LI_POO_WAIT,
                                                    &player->actor.projectedPos);
                                } else {
                                    // Fallback when mm.o2r isn't loaded: OOT voice.
                                    Sfx_PlaySfxCentered(NA_SE_VO_LI_RELAX);
                                }
                            }
                            sScentsPrevSfxFrame = fi;
                        }

                        // Advance phase.
                        sScentsSniffFrame += 1.0f;
                        if (sScentsSniffFrame >= player->skelAnime.animLength) {
                            sScentsSniffFrame = 0.0f;
                            sScentsPrevSfxFrame = -1;
                        }
                    }
                } else {
                    sScentsSniffActive = 0;
                }

                // Spot spawn detector (mailbox-style frame-rewind detection).
                // Gated internally on MmAssets_IsLoaded() — gracefully no-ops
                // when mm.o2r isn't available.
                MushroomSpots_Tick(play);
                break;
            }
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
                            if (!sKamaroDancing) {
                                // BGM playback for the Kamaro dance. MM uses
                                // NA_BGM_KAMARO_DANCE (0x55 in MM) but the MM
                                // audio loader currently can't play sequences.
                                // OOT's Lon Lon Ranch Ingo theme is the closest
                                // dance/fanfare BGM available — swap once MM
                                // seq playback exists.
                                Audio_PlayFanfare(NA_BGM_INGO);
                            }
                            sKamaroDancing = 1;
                            sKamaroDanceFrame = 0.0f;
                        }
                    }
                } else {
                    // A released → stop dancing
                    if (sKamaroDancing) {
                        Audio_QueueSeqCmd(NA_BGM_STOP);
                        if (EnDu_IsDancing()) {
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
                    spawned =
                        Actor_Spawn(&play->actorCtx, play, ACTOR_EN_TEST, spawnX, spawnY, spawnZ, 0, (s16)angle, 0, 0);
                } else {
                    // Child: Giant Stalchild (params=10 → 2x scale, 2x speed)
                    spawned =
                        Actor_Spawn(&play->actorCtx, play, ACTOR_EN_SKB, spawnX, spawnY, spawnZ, 0, (s16)angle, 0, 10);
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
    sFairyKaleidoInit = 0;
    sFairyStickHeld = 0;
    sFairyHairInited = 0;
    sHairLastPhysicsFrame = 0xFFFFFFFFu;
    // Bremen transient state (persistent counter + cucco flag handled separately on death)
    sBremenMarching = 0;
    sBremenMarchFrame = 0.0f;
    sBremenWallTurnLockout = 0;
    if (sBremenBgmStarted) {
        Audio_QueueSeqCmd(NA_BGM_STOP);
    }
    sBremenBgmStarted = 0;
    // Mask of Scents transient state (anim cache persists, like Kamaro's).
    sScentsSniffActive = 0;
    sScentsSniffFrame = 0.0f;
    sScentsPrevSfxFrame = -1;
    // NOTE: sFairyWarpPhase is NOT cleared here — it must persist through scene transitions
    // so void-in animation plays after arriving at the fountain. It self-clears when done.
    // NOTE: sChateauRomaniActive is NOT cleared here (persists across scenes, cleared on death)
    // NOTE: sKamaroDanceAnim is NOT cleared (cached animation, reusable)
    // NOTE: sBremenWornTotalFrames / sBremenAdultCuccoSpawned are NOT cleared here (only on death,
    //       same persistence model as sChateauRomaniActive). See MmMaskWear_OnDeath.
}

extern "C" void MmMaskWear_OnDeath(void) {
    // Death = full reset of Bremen progression (chick + cucco follower).
    sBremenWornTotalFrames = 0;
    sBremenAdultCuccoSpawned = 0;
    BremenFollower_OnDeath();
}

extern "C" s32 MmMaskWear_IsBremenMarching(void) {
    return sBremenMarching != 0;
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

extern "C" s32 MmMaskWear_IsGibdoMaskWorn(void) {
    return (sCurrentMmMask != ITEM_NONE) && (MaskItemToIndex(sCurrentMmMask) == MM_MASK_IDX_GIBDO);
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

// Draw orbiting full-body stray fairy sprites during warp animation
// Uses pre-rendered RGBA32 32x24 sprite from parameter_static if available,
// otherwise composites from gameplay_keep parts (glow + body + head + wings)
#define FAIRY_ORBIT_COUNT 5

static void FairyOrbit_Draw(PlayState* play) {
    if (sFairyWarpPhase == 0 || !MmAssets_IsAvailable())
        return;

    // Check once if pre-rendered full fairy texture is available in mm.o2r
    if (sStrayFairyFullTexAvailable < 0) {
        sStrayFairyFullTexAvailable =
            MmAssets_ResourceExists("interface/parameter_static/gStrayFairyWoodfallIconTex") ? 1 : 0;
    }

    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);

    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCycleType(OVERLAY_DISP++, G_CYC_1CYCLE);
    gDPSetTextureFilter(OVERLAY_DISP++, G_TF_BILERP);
    gDPSetTextureLUT(OVERLAY_DISP++, G_TT_NONE);
    Gfx_SetupDL_39Overlay(gfxCtx);

    // Screen center in 10.2 fixed-point (320x240 → 640,480)
    s32 cx = 640;
    s32 cy = 480;

    // Orbit radius: expand during void-out, contract during void-in
    f32 baseRadius;
    if (sFairyWarpPhase == 1) {
        f32 t = (f32)sFairyWarpTimer / (f32)FAIRY_VOID_OUT_FRAMES;
        baseRadius = 80.0f + 120.0f * t;
    } else {
        f32 t = (f32)sFairyWarpTimer / (f32)FAIRY_VOID_IN_FRAMES;
        baseRadius = 200.0f - 120.0f * t;
    }

    for (s32 i = 0; i < FAIRY_ORBIT_COUNT; i++) {
        s16 angle = (s16)(sFairyWarpTimer * 0x800 + i * (0x10000 / FAIRY_ORBIT_COUNT));
        f32 rx = baseRadius;
        f32 ry = baseRadius * 0.65f;

        s32 fx = cx + (s32)(Math_SinS(angle) * rx);
        s32 fy = cy + (s32)(Math_CosS(angle) * ry);
        fy += (s32)(8.0f * Math_SinS((s16)(sFairyWarpTimer * 0x200 + i * 0x2000)));

        u8 alpha = (u8)(200 + 55 * Math_SinS((s16)(sFairyWarpTimer * 0x1000 + i * 0x3000)));

        if (sStrayFairyFullTexAvailable == 1) {
            // === Pre-rendered RGBA32 32x24 full fairy sprite (best quality) ===

            // Glow circle behind (I4 32x24)
            gDPPipeSync(OVERLAY_DISP++);
            gDPSetCombineLERP(OVERLAY_DISP++, 1, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0, 1, 0, PRIMITIVE, 0, TEXEL0,
                              0, PRIMITIVE, 0);
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 180, 255, (u8)(alpha * 2 / 3));
            {
                // Glow circle: 48x36 in 10.2 = ~12x9 pixels (slightly larger than fairy)
                s32 gw = 48;
                s32 gh = 36;
                gDPLoadTextureBlock_4b(OVERLAY_DISP++, sStrayFairyGlowCircleTex, G_IM_FMT_I, 32, 24, 0,
                                       G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK,
                                       G_TX_NOLOD, G_TX_NOLOD);
                gSPWideTextureRectangle(OVERLAY_DISP++, fx - gw, fy - gh, fx + gw, fy + gh, G_TX_RENDERTILE, 0, 0,
                                        32 * 4096 / (gw * 2), 24 * 4096 / (gh * 2));
            }

            // Full fairy sprite on top (RGBA32 32x24)
            gDPPipeSync(OVERLAY_DISP++);
            gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, alpha);
            {
                // Fairy sprite: 40x30 in 10.2 = ~10x7.5 pixels
                s32 fw = 40;
                s32 fh = 30;
                gDPLoadTextureBlock(OVERLAY_DISP++, sStrayFairyFullTex, G_IM_FMT_RGBA, G_IM_SIZ_32b, 32, 24, 0,
                                    G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK,
                                    G_TX_NOLOD, G_TX_NOLOD);
                gSPWideTextureRectangle(OVERLAY_DISP++, fx - fw, fy - fh, fx + fw, fy + fh, G_TX_RENDERTILE, 0, 0,
                                        32 * 4096 / (fw * 2), 24 * 4096 / (fh * 2));
            }
        } else {
            // === Fallback: composite from gameplay_keep parts ===
            // All coordinates relative to (fx, fy) = center of fairy

            // Glow circle behind (I4 16x16)
            gDPPipeSync(OVERLAY_DISP++);
            gDPSetCombineLERP(OVERLAY_DISP++, 1, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0, 1, 0, PRIMITIVE, 0, TEXEL0,
                              0, PRIMITIVE, 0);
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 180, 255, (u8)(alpha * 2 / 3));
            {
                s32 gs = 36;
                gDPLoadTextureBlock_4b(OVERLAY_DISP++, sStrayFairyGlowTex, G_IM_FMT_I, 16, 16, 0,
                                       G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK,
                                       G_TX_NOLOD, G_TX_NOLOD);
                gSPWideTextureRectangle(OVERLAY_DISP++, fx - gs, fy - gs, fx + gs, fy + gs, G_TX_RENDERTILE, 0, 0,
                                        16 * 4096 / (gs * 2), 16 * 4096 / (gs * 2));
            }

            // Switch to IA modulate for body parts
            gDPPipeSync(OVERLAY_DISP++);
            gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 210, 255, alpha);

            // Body (IA8 16x32) at center
            {
                s32 bw = 10;
                s32 bTop = -4;
                s32 bBot = 32;
                gDPLoadTextureBlock(OVERLAY_DISP++, sStrayFairyBodyTex, G_IM_FMT_IA, G_IM_SIZ_8b, 16, 32, 0,
                                    G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK,
                                    G_TX_NOLOD, G_TX_NOLOD);
                gSPWideTextureRectangle(OVERLAY_DISP++, fx - bw, fy + bTop, fx + bw, fy + bBot, G_TX_RENDERTILE, 0, 0,
                                        16 * 4096 / (bw * 2), 32 * 4096 / (bBot - bTop));
            }

            // Head (IA8 32x32) on top
            {
                s32 hw = 14;
                s32 hTop = -32;
                s32 hBot = -4;
                gDPLoadTextureBlock(OVERLAY_DISP++, sStrayFairyHeadTex, G_IM_FMT_IA, G_IM_SIZ_8b, 32, 32, 0,
                                    G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK,
                                    G_TX_NOLOD, G_TX_NOLOD);
                gSPWideTextureRectangle(OVERLAY_DISP++, fx - hw, fy + hTop, fx + hw, fy + hBot, G_TX_RENDERTILE, 0, 0,
                                        32 * 4096 / (hw * 2), 32 * 4096 / (hBot - hTop));
            }

            // Left wing (IA8 16x16)
            {
                s32 wXL = fx - 24;
                s32 wXR = fx - 6;
                s32 wYT = fy - 16;
                s32 wYB = fy + 4;
                gDPLoadTextureBlock(OVERLAY_DISP++, sStrayFairyWingTex, G_IM_FMT_IA, G_IM_SIZ_8b, 16, 16, 0,
                                    G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK,
                                    G_TX_NOLOD, G_TX_NOLOD);
                gSPWideTextureRectangle(OVERLAY_DISP++, wXL, wYT, wXR, wYB, G_TX_RENDERTILE, 0, 0,
                                        16 * 4096 / (wXR - wXL), 16 * 4096 / (wYB - wYT));
            }

            // Right wing (IA8 16x16)
            {
                s32 wXL = fx + 6;
                s32 wXR = fx + 24;
                s32 wYT = fy - 16;
                s32 wYB = fy + 4;
                gDPLoadTextureBlock(OVERLAY_DISP++, sStrayFairyWingTex, G_IM_FMT_IA, G_IM_SIZ_8b, 16, 16, 0,
                                    G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK,
                                    G_TX_NOLOD, G_TX_NOLOD);
                gSPWideTextureRectangle(OVERLAY_DISP++, wXL, wYT, wXR, wYB, G_TX_RENDERTILE, 0, 0,
                                        16 * 4096 / (wXR - wXL), 16 * 4096 / (wYB - wYT));
            }
        }
    }

    gDPPipeSync(OVERLAY_DISP++);
    CLOSE_DISPS(gfxCtx);
}

extern "C" void MmMaskWear_DrawOverlay(PlayState* play) {
    // Draw orbiting fairy sprites during warp animation
    FairyOrbit_Draw(play);

    if (!sGreatFairyMenuOpen)
        return;

    try {
        GraphicsContext* gfxCtx = play->state.gfxCtx;
        s8 curIdx = sGreatFairyMenuCursor;
        s16 lang = gSaveContext.language;
        if (lang < 0 || lang > 3)
            lang = 0;

        OPEN_DISPS(gfxCtx);

        // ---- 1. Semi-transparent dark background (25% alpha) ----
        gDPPipeSync(OVERLAY_DISP++);
        gDPSetCycleType(OVERLAY_DISP++, G_CYC_1CYCLE);
        gDPSetCombineMode(OVERLAY_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
        gDPSetOtherMode(OVERLAY_DISP++,
                        G_AD_DISABLE | G_CD_DISABLE | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                            G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                        G_AC_NONE | G_ZS_PRIM | G_RM_CLD_SURF | G_RM_CLD_SURF2);
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 0, 0, 0, 64);
        gSPWideTextureRectangle(OVERLAY_DISP++, 0, 0, SCREEN_WIDTH << 2, SCREEN_HEIGHT << 2, G_TX_RENDERTILE, 0, 0, 0,
                                0);
        gDPPipeSync(OVERLAY_DISP++);

        // ---- 2. Frame (IA8, 80x32 tiles, 3 columns x 5 rows) ----
        {
            Gfx_SetupDL_39Overlay(gfxCtx);
            gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

            static s16 sColX[] = { -120, -40, 40, 120 };
            static s16 sRowY[] = { 80, 48, 16, -16, -48, -80 };
            static u8 sColR[] = { 80, 110, 80 };
            static u8 sColG[] = { 40, 60, 40 };
            static u8 sColB[] = { 100, 130, 100 };

            for (s16 col = 0; col < 3; col++) {
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, sColR[col], sColG[col], sColB[col], 255);
                s32 xl = FKX(sColX[col]);
                s32 xh = FKX(sColX[col + 1]);
                s32 dsdx = 80 * 4096 / (xh - xl);

                for (s16 row = 0; row < 5; row++) {
                    s32 yl = FKY(sRowY[row]);
                    s32 yh = FKY(sRowY[row + 1]);
                    s32 dtdy = 32 * 4096 / (yh - yl);

                    gDPLoadTextureBlock(OVERLAY_DISP++, sFairyMapTexs[lang][col * 5 + row], G_IM_FMT_IA, G_IM_SIZ_8b,
                                        80, 32, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK,
                                        G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
                    gSPWideTextureRectangle(OVERLAY_DISP++, xl, yl, xh, yh, G_TX_RENDERTILE, 0, 0, dsdx, dtdy);
                }
            }
        }

        // ---- 3. Map (CI8 216x128, 15 strips) ----
        {
            gDPPipeSync(OVERLAY_DISP++);
            gDPSetTextureFilter(OVERLAY_DISP++, G_TF_POINT);
            gDPLoadTLUT_pal256(OVERLAY_DISP++, gWorldMapImageTLUT);
            gDPSetTextureLUT(OVERLAY_DISP++, G_TT_RGBA16);
            gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, 255);

            s32 mapXL = FKX(-108);
            s32 mapXH = FKX(108);
            s32 mapDsdx = 216 * 4096 / (mapXH - mapXL);

            for (s16 i = 0; i < 15; i++) {
                s16 stripH = (i < 14) ? 9 : 2;
                s16 ky0 = 58 - i * 9;
                s16 ky1 = ky0 - stripH;
                s32 syl = FKY(ky0);
                s32 syh = FKY(ky1);
                s32 mapDtdy = stripH * 4096 / (syh - syl);

                gDPLoadMultiTile(OVERLAY_DISP++, gWorldMapImageTex, 0, G_TX_RENDERTILE, G_IM_FMT_CI, G_IM_SIZ_8b, 216,
                                 128, 0, i * 9, 215, i * 9 + stripH - 1, 0, G_TX_WRAP | G_TX_NOMIRROR,
                                 G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
                gDPSetTileSize(OVERLAY_DISP++, G_TX_RENDERTILE, 0, 0, (216 - 1) << G_TEXTURE_IMAGE_FRAC,
                               (stripH - 1) << G_TEXTURE_IMAGE_FRAC);
                gSPWideTextureRectangle(OVERLAY_DISP++, mapXL, syl, mapXH, syh, G_TX_RENDERTILE, 0, 0, mapDsdx,
                                        mapDtdy);
            }
        }

        // ---- 4. Clouds (I4 textures, hide undiscovered areas) ----
        {
            gDPPipeSync(OVERLAY_DISP++);
            gDPSetTextureFilter(OVERLAY_DISP++, G_TF_BILERP);
            gDPSetTextureLUT(OVERLAY_DISP++, G_TT_NONE);
            Gfx_SetupDL_39Overlay(gfxCtx);
            gDPSetCombineLERP(OVERLAY_DISP++, 1, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0, 1, 0, PRIMITIVE, 0, TEXEL0,
                              0, PRIMITIVE, 0);
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 235, 235, 235, 255);

            for (s16 i = 0; i < 16; i++) {
                if (!(gSaveContext.worldMapAreaData & gBitFlags[sFairyCloudFlagNums[i]])) {
                    s32 cxl = FKX(sFairyCloudPosX[i]);
                    s32 cyl = FKY(sFairyCloudPosY[i]);
                    s32 cxh = FKX(sFairyCloudPosX[i] + sFairyCloudWidths[i]);
                    s32 cyh = FKY(sFairyCloudPosY[i] - sFairyCloudHeights[i]);
                    s32 cDsdx = sFairyCloudWidths[i] * 4096 / (cxh - cxl);
                    s32 cDtdy = sFairyCloudHeights[i] * 4096 / (cyh - cyl);

                    gDPLoadTextureBlock_4b(OVERLAY_DISP++, sFairyCloudTexs[i], G_IM_FMT_I, sFairyCloudWidths[i],
                                           sFairyCloudHeights[i], 0, G_TX_WRAP | G_TX_NOMIRROR,
                                           G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
                    gSPWideTextureRectangle(OVERLAY_DISP++, cxl, cyl, cxh, cyh, G_TX_RENDERTILE, 0, 0, cDsdx, cDtdy);
                }
            }
        }

        // ---- 5. Area boxes for fountain locations (2px fill-rect outlines) ----
        {
            gDPPipeSync(OVERLAY_DISP++);
            gDPSetCycleType(OVERLAY_DISP++, G_CYC_FILL);

            for (s16 i = 0; i < FAIRY_FOUNTAIN_COUNT; i++) {
                s32 discovered = FairyKaleido_IsDiscovered(i);
                u8 r, g, b;

                if (i == curIdx) {
                    r = (u8)sFairyPulsePrim[0];
                    g = (u8)sFairyPulsePrim[1];
                    b = (u8)sFairyPulsePrim[2];
                } else if (discovered) {
                    r = 150;
                    g = 255;
                    b = 200;
                } else {
                    r = 100;
                    g = 100;
                    b = 100;
                }

                u32 packed =
                    (GPACK_RGBA5551(r >> 3, g >> 3, b >> 3, 1) << 16) | GPACK_RGBA5551(r >> 3, g >> 3, b >> 3, 1);
                gDPSetFillColor(OVERLAY_DISP++, packed);

                s32 x1 = FKX(sFairyAreaData[i].centerX - FAIRY_BOX_HW) >> 2;
                s32 y1 = FKY(sFairyAreaData[i].centerY + FAIRY_BOX_HH) >> 2;
                s32 x2 = FKX(sFairyAreaData[i].centerX + FAIRY_BOX_HW) >> 2;
                s32 y2 = FKY(sFairyAreaData[i].centerY - FAIRY_BOX_HH) >> 2;

                gDPFillRectangle(OVERLAY_DISP++, x1, y1, x2, y1 + 2);
                gDPFillRectangle(OVERLAY_DISP++, x1, y2 - 2, x2, y2);
                gDPFillRectangle(OVERLAY_DISP++, x1, y1, x1 + 2, y2);
                gDPFillRectangle(OVERLAY_DISP++, x2 - 2, y1, x2, y2);
            }
            gDPPipeSync(OVERLAY_DISP++);
        }

        // ---- 5b. Stray Fairy icon at selected box (glow + head, from mm.o2r) ----
        if (curIdx >= 0 && curIdx < FAIRY_FOUNTAIN_COUNT && MmAssets_IsAvailable()) {
            static s16 sFairyFlickerTimer = 0;
            sFairyFlickerTimer++;

            if ((sFairyFlickerTimer % 16) < 11) {
                s32 pcX = sFairyAreaData[curIdx].centerX;
                s32 pcY = sFairyAreaData[curIdx].centerY;

                // -- Glow circle (I4 16x16) behind fairy --
                gDPPipeSync(OVERLAY_DISP++);
                gDPSetCycleType(OVERLAY_DISP++, G_CYC_1CYCLE);
                gDPSetTextureFilter(OVERLAY_DISP++, G_TF_BILERP);
                gDPSetTextureLUT(OVERLAY_DISP++, G_TT_NONE);
                Gfx_SetupDL_39Overlay(gfxCtx);
                gDPSetCombineLERP(OVERLAY_DISP++, 1, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0, 1, 0, PRIMITIVE, 0,
                                  TEXEL0, 0, PRIMITIVE, 0);
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 180, 255, 140);
                {
                    s32 gXL = FKX(pcX - 14);
                    s32 gYL = FKY(pcY + 14);
                    s32 gXH = FKX(pcX + 14);
                    s32 gYH = FKY(pcY - 14);
                    s32 gDsdx = 16 * 4096 / (gXH - gXL);
                    s32 gDtdy = 16 * 4096 / (gYH - gYL);
                    gDPLoadTextureBlock_4b(OVERLAY_DISP++, sStrayFairyGlowTex, G_IM_FMT_I, 16, 16, 0,
                                           G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK,
                                           G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
                    gSPWideTextureRectangle(OVERLAY_DISP++, gXL, gYL, gXH, gYH, G_TX_RENDERTILE, 0, 0, gDsdx, gDtdy);
                }

                // -- Head (IA8 32x32) centered, large --
                gDPPipeSync(OVERLAY_DISP++);
                gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 210, 255, 255);
                {
                    s32 hXL = FKX(pcX - 10);
                    s32 hYL = FKY(pcY + 10);
                    s32 hXH = FKX(pcX + 10);
                    s32 hYH = FKY(pcY - 10);
                    s32 hDsdx = 32 * 4096 / (hXH - hXL);
                    s32 hDtdy = 32 * 4096 / (hYH - hYL);
                    gDPLoadTextureBlock(OVERLAY_DISP++, sStrayFairyHeadTex, G_IM_FMT_IA, G_IM_SIZ_8b, 32, 32, 0,
                                        G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK,
                                        G_TX_NOLOD, G_TX_NOLOD);
                    gSPWideTextureRectangle(OVERLAY_DISP++, hXL, hYL, hXH, hYH, G_TX_RENDERTILE, 0, 0, hDsdx, hDtdy);
                }
                gDPPipeSync(OVERLAY_DISP++);
            }
        }

        // ---- 6. Text labels on parchment (bottom-right of map, vanilla position) ----
        if (curIdx >= 0 && curIdx < FAIRY_FOUNTAIN_COUNT) {
            s32 discovered = FairyKaleido_IsDiscovered(curIdx);

            gDPSetCycleType(OVERLAY_DISP++, G_CYC_1CYCLE);
            gDPSetTextureFilter(OVERLAY_DISP++, G_TF_BILERP);
            gDPSetTextureLUT(OVERLAY_DISP++, G_TT_NONE);
            Gfx_SetupDL_39Overlay(gfxCtx);

            // "CURRENT POSITION" title (I4, 64x8)
            gDPSetCombineLERP(OVERLAY_DISP++, 1, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0, 1, 0, PRIMITIVE, 0, TEXEL0,
                              0, PRIMITIVE, 0);
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 0, 0, 0, 255);

            {
                s32 cpXL = FKX(20);
                s32 cpYL = FKY(-26);
                s32 cpXH = FKX(84);
                s32 cpYH = FKY(-34);
                s32 cpDsdx = 64 * 4096 / (cpXH - cpXL);
                s32 cpDtdy = 8 * 4096 / (cpYH - cpYL);

                gDPLoadTextureBlock_4b(OVERLAY_DISP++, sFairyCurrentPosTitleTexs[lang], G_IM_FMT_I, 64, 8, 0,
                                       G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK,
                                       G_TX_NOLOD, G_TX_NOLOD);
                gSPWideTextureRectangle(OVERLAY_DISP++, cpXL, cpYL, cpXH, cpYH, G_TX_RENDERTILE, 0, 0, cpDsdx, cpDtdy);
            }

            gDPPipeSync(OVERLAY_DISP++);

            // Area name (IA8, 80x32) below title
            gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                              PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);

            if (discovered) {
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 200, 255, 220, 255); // Light green for fairy
            } else {
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 120, 120, 120, 255);
            }
            gDPSetEnvColor(OVERLAY_DISP++, 0, 0, 0, 0);

            {
                s32 nXL = FKX(19);
                s32 nYL = FKY(-36);
                s32 nXH = FKX(99);
                s32 nYH = FKY(-68);
                s32 nDsdx = 80 * 4096 / (nXH - nXL);
                s32 nDtdy = 32 * 4096 / (nYH - nYL);

                gDPLoadTextureBlock(OVERLAY_DISP++, sFairyAreaData[curIdx].nameTex[lang], G_IM_FMT_IA, G_IM_SIZ_8b, 80,
                                    32, 0, G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK,
                                    G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
                gSPWideTextureRectangle(OVERLAY_DISP++, nXL, nYL, nXH, nYH, G_TX_RENDERTILE, 0, 0, nDsdx, nDtdy);
            }
        }

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
    if (sGreatFairyInputSkip) {
        sGreatFairyInputSkip = 0;
        return;
    }

    // Initialize kaleido state on first frame
    if (!sFairyKaleidoInit) {
        sFairyKaleidoInit = 1;
        // Start cursor at first discovered fountain
        for (s32 i = 0; i < FAIRY_FOUNTAIN_COUNT; i++) {
            if (FairyKaleido_IsDiscovered(i)) {
                sGreatFairyMenuCursor = i;
                break;
            }
        }
        sFairyPulsePrim[0] = 255;
        sFairyPulsePrim[1] = 150;
        sFairyPulsePrim[2] = 255;
        sFairyPulseStage = 0;
        sFairyPulseTimer = 20;
        sFairyStickHeld = 0;
    }

    FairyKaleido_UpdatePulse();

    Input* input = &play->state.input[0];
    s8 curIdx = sGreatFairyMenuCursor;

    // Analog stick navigation (nearest-neighbor, same as minish_kaleido)
    s16 stickX = input->rel.stick_x;
    s16 stickY = input->rel.stick_y;
    f32 stickMag = sqrtf((f32)(stickX * stickX + stickY * stickY));

    if (stickMag > 30.0f) {
        if (!sFairyStickHeld) {
            s8 nextIdx = FairyKaleido_FindNearest(curIdx, stickX, stickY);
            if (nextIdx >= 0) {
                sGreatFairyMenuCursor = nextIdx;
                Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
            sFairyStickHeld = 1;
        }
    } else {
        sFairyStickHeld = 0;
    }

    curIdx = sGreatFairyMenuCursor;

    // B cancels
    if (CHECK_BTN_ALL(input->press.button, BTN_B) || CHECK_BTN_ALL(input->press.button, BTN_START)) {
        sGreatFairyMenuOpen = 0;
        sFairyKaleidoInit = 0;
        play->pauseCtx.state = 0;
        Audio_PlaySoundGeneral(NA_SE_SY_CANCEL, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    // A confirms warp — start void-out animation instead of immediate transition
    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        s32 sel = sGreatFairyMenuCursor;
        s32 discovered = FairyKaleido_IsDiscovered(sel);

        if (discovered) {
            // Close the kaleido overlay and unpause so world renders during void-out
            sGreatFairyMenuOpen = 0;
            sFairyKaleidoInit = 0;
            play->pauseCtx.state = 0;

            // Start void-out animation (transition happens after shrink completes)
            sFairyWarpPhase = 1;
            sFairyWarpTimer = 0;
            sFairyWarpDestIdx = (s8)sel;

            // Play Great Fairy appear sound + confirm
            Audio_PlaySoundGeneral(NA_SE_EV_GREAT_FAIRY_APPEAR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        } else {
            Audio_PlaySoundGeneral(NA_SE_SY_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
    }
}
