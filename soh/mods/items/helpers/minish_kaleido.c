/**
 * minish_kaleido.c - Standalone overlay for The Minish Cap warp map
 *
 * Replicates the vanilla kaleido MAP page rendering on POLY_OPA_DISP
 * using 3D vertex quads + view/model matrix, exactly as z_kaleido_scope does.
 * Completely independent from z_kaleido_scope (no hooks into it).
 *
 * Game is frozen by setting pauseCtx->state != 0 (triggers isPaused).
 * z_play.c guards KaleidoScopeCall_Update/Draw when minishCapWarpMode is set,
 * calling MinishKaleido_Update/Draw instead.
 *
 * Navigation: analog stick nearest-neighbor between 10 pod soil points.
 * A = confirm warp, B/START = cancel.
 */

#include "minish_kaleido.h"
#include "../custom_items.h"
#include "../logic/item_minish_cap.h"
#include "textures/icon_item_static/icon_item_static.h"
#include "textures/icon_item_field_static/icon_item_field_static.h"
#include "textures/icon_item_nes_static/icon_item_nes_static.h"
#include "textures/icon_item_ger_static/icon_item_ger_static.h"
#include "textures/icon_item_fra_static/icon_item_fra_static.h"
#include "textures/icon_item_jpn_static/icon_item_jpn_static.h"
#include "textures/map_name_static/map_name_static.h"
#include "textures/icon_item_24_static/icon_item_24_static.h"

#include "assets/soh_assets.h"

// ============================================================
// Data tables for 9 pod soils
// ============================================================

// Area data per pod soil — CENTER position in kaleido coordinates
typedef struct {
    s16 centerX;
    s16 centerY;
    const char* nameTex[4]; // Position name textures [ENG, GER, FRA, JPN]
} MinishKaleidoSoilData;

// Uniform box half-dimensions (kaleido units)
#define UBOX_HW 12
#define UBOX_HH 8

static const MinishKaleidoSoilData sSoilAreaData[POD_SOIL_COUNT] = {
    // #0 Kokiri Forest
    { 73,
      -12,
      { gKokiriForestPositionNameENGTex, gKokiriForestPositionNameGERTex, gKokiriForestPositionNameFRATex,
        gKokiriForestPositionNameJPNTex } },
    // #1 Lost Woods
    { 58,
      -6,
      { gLostWoodsPositionNameENGTex, gLostWoodsPositionNameGERTex, gLostWoodsPositionNameFRATex,
        gLostWoodsPositionNameJPNTex } },
    // #2 Sacred Forest Meadow
    { 67,
      3,
      { gSacredForestMeadowPositionNameENGTex, gSacredForestMeadowPositionNameGERTex,
        gSacredForestMeadowPositionNameFRATex, gSacredForestMeadowPositionNameJPNTex } },
    // #3 Lake Hylia
    { -25,
      -50,
      { gLakeHyliaPositionNameENGTex, gLakeHyliaPositionNameGERTex, gLakeHyliaPositionNameFRATex,
        gLakeHyliaPositionNameJPNTex } },
    // #4 Graveyard
    { 60,
      29,
      { gGraveyardPositionNameENGTex, gGraveyardPositionNameGERTex, gGraveyardPositionNameFRATex,
        gGraveyardPositionNameJPNTex } },
    // #5 Death Mountain Trail
    { 35,
      44,
      { gDeathMountainTrailPositionNameENGTex, gDeathMountainTrailPositionNameGERTex,
        gDeathMountainTrailPositionNameFRATex, gDeathMountainTrailPositionNameJPNTex } },
    // #6 Death Mountain Crater
    { 40,
      52,
      { gDeathMountainCraterPositionNameENGTex, gDeathMountainCraterPositionNameGERTex,
        gDeathMountainCraterPositionNameFRATex, gDeathMountainCraterPositionNameJPNTex } },
    // #7 Desert Colossus
    { -93,
      29,
      { gDesertColossusPositionNameENGTex, gDesertColossusPositionNameGERTex, gDesertColossusPositionNameFRATex,
        gDesertColossusPositionNameJPNTex } },
    // #8 Gerudo Valley
    { -51,
      10,
      { gGerudoValleyPositionNameENGTex, gGerudoValleyPositionNameGERTex, gGerudoValleyPositionNameFRATex,
        gGerudoValleyPositionNameJPNTex } },
    // #9 Zora's River (always unlocked)
    { 78,
      18,
      { gZorasRiverPositionNameENGTex, gZorasRiverPositionNameGERTex, gZorasRiverPositionNameFRATex,
        gZorasRiverPositionNameJPNTex } },
};

// ============================================================
// MAP page frame textures (same as sMapENGTexs in z_kaleido_scope_PAL.c)
// ============================================================

static void* sMapENGTexs[] = {
    gPauseMap00Tex,    gPauseMap01Tex, gPauseMap02Tex, gPauseMap03Tex, gPauseMap04Tex,
    gPauseMap10ENGTex, gPauseMap11Tex, gPauseMap12Tex, gPauseMap13Tex, gPauseMap14Tex,
    gPauseMap20Tex,    gPauseMap21Tex, gPauseMap22Tex, gPauseMap23Tex, gPauseMap24Tex,
};
static void* sMapGERTexs[] = {
    gPauseMap00Tex,    gPauseMap01Tex, gPauseMap02Tex, gPauseMap03Tex, gPauseMap04Tex,
    gPauseMap10GERTex, gPauseMap11Tex, gPauseMap12Tex, gPauseMap13Tex, gPauseMap14Tex,
    gPauseMap20Tex,    gPauseMap21Tex, gPauseMap22Tex, gPauseMap23Tex, gPauseMap24Tex,
};
static void* sMapFRATexs[] = {
    gPauseMap00Tex,    gPauseMap01Tex, gPauseMap02Tex, gPauseMap03Tex, gPauseMap04Tex,
    gPauseMap10FRATex, gPauseMap11Tex, gPauseMap12Tex, gPauseMap13Tex, gPauseMap14Tex,
    gPauseMap20Tex,    gPauseMap21Tex, gPauseMap22Tex, gPauseMap23Tex, gPauseMap24Tex,
};
static void* sMapJPNTexs[] = {
    gPauseMap00Tex,    gPauseMap01Tex, gPauseMap02Tex, gPauseMap03Tex, gPauseMap04Tex,
    gPauseMap10JPNTex, gPauseMap11Tex, gPauseMap12Tex, gPauseMap13Tex, gPauseMap14Tex,
    gPauseMap20Tex,    gPauseMap21Tex, gPauseMap22Tex, gPauseMap23Tex, gPauseMap24Tex,
};
static void** sMapTexs[] = { sMapENGTexs, sMapGERTexs, sMapFRATexs, sMapJPNTexs };

// Cloud textures and flag numbers (from z_kaleido_map_PAL.c)
static void* sCloudTexs[] = {
    gWorldMapCloud16Tex, gWorldMapCloud15Tex, gWorldMapCloud14Tex, gWorldMapCloud13Tex,
    gWorldMapCloud12Tex, gWorldMapCloud11Tex, gWorldMapCloud10Tex, gWorldMapCloud9Tex,
    gWorldMapCloud8Tex,  gWorldMapCloud7Tex,  gWorldMapCloud6Tex,  gWorldMapCloud5Tex,
    gWorldMapCloud4Tex,  gWorldMapCloud3Tex,  gWorldMapCloud2Tex,  gWorldMapCloud1Tex,
};
static u16 sCloudFlagNums[] = {
    0x05, 0x00, 0x13, 0x0E, 0x0F, 0x01, 0x02, 0x10, 0x12, 0x03, 0x07, 0x08, 0x09, 0x0C, 0x0B, 0x06,
};

// Cloud dimensions (D_8082AAEC and D_8082AB2C from z_kaleido_scope_PAL.c, first 16 entries)
static s16 sCloudWidths[] = {
    32, 112, 32, 48, 32, 32, 32, 48, 32, 64, 32, 48, 48, 48, 48, 64,
};
static s16 sCloudHeights[] = {
    24, 72, 13, 22, 19, 20, 19, 27, 14, 26, 22, 21, 49, 32, 45, 60,
};

// "CURRENT POSITION" title textures per language
static void* sCurrentPosTitleTexs[] = {
    gPauseCurrentPositionENGTex,
    gPauseCurrentPositionGERTex,
    gPauseCurrentPositionFRATex,
    gPauseCurrentPositionJPNTex,
};

// MAP page frame vertex colors for pageIndex=4 (from func_80823A0C pageColors array)
static const Color_RGBA8 sMapPageColors[] = {
    { 80, 40, 30, 255 },
    { 140, 60, 60, 255 },
    { 140, 60, 60, 255 },
    { 80, 40, 30, 255 },
};

// ============================================================
// Pulsing color for selected area box
// ============================================================

static s16 sMinishPulsePrim[] = { 100, 255, 255 };
static s16 sMinishPulseTarget[][3] = {
    { 255, 255, 100 },
    { 100, 255, 255 },
};
static s16 sMinishPulseStage = 0;
static s16 sMinishPulseTimer = 20;

static void MinishKaleido_UpdatePulse(void) {
    for (s32 c = 0; c < 3; c++) {
        s16 diff = sMinishPulseTarget[sMinishPulseStage][c] - sMinishPulsePrim[c];
        s16 step = diff / (sMinishPulseTimer > 0 ? sMinishPulseTimer : 1);
        sMinishPulsePrim[c] += step;
    }
    sMinishPulseTimer--;
    if (sMinishPulseTimer <= 0) {
        for (s32 c = 0; c < 3; c++)
            sMinishPulsePrim[c] = sMinishPulseTarget[sMinishPulseStage][c];
        sMinishPulseStage ^= 1;
        sMinishPulseTimer = 20;
    }
}

// ============================================================
// Navigation: nearest-neighbor in stick direction
// ============================================================

static s8 sStickHeld = 0;
static s8 sMinishInitialized = 0;

static s8 MinishKaleido_FindNearest(s8 currentIdx, s16 stickX, s16 stickY) {
    f32 stickMag = sqrtf((f32)(stickX * stickX + stickY * stickY));
    if (stickMag < 30.0f)
        return -1;

    f32 stickAngle = atan2f((f32)stickX, (f32)stickY);

    f32 curCX = sSoilAreaData[currentIdx].centerX;
    f32 curCY = sSoilAreaData[currentIdx].centerY;

    s8 bestIdx = -1;
    f32 bestScore = -1.0f;

    for (s32 i = 0; i < POD_SOIL_COUNT; i++) {
        if (i == currentIdx)
            continue;

        f32 dx = sSoilAreaData[i].centerX - curCX;
        f32 dy = sSoilAreaData[i].centerY - curCY;
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

// ============================================================
// Update (called from z_play.c when minishCapWarpMode is active)
// ============================================================

void MinishKaleido_Update(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    Input* input = &play->state.input[0];

    if (!sMinishInitialized) {
        sMinishInitialized = 1;

        gCustomItemState.minishCapCursorIdx = 0;
        for (s32 i = 0; i < POD_SOIL_COUNT; i++) {
            if (MinishCap_IsPodSoilUnlocked(i)) {
                gCustomItemState.minishCapCursorIdx = i;
                break;
            }
        }

        sMinishPulsePrim[0] = 100;
        sMinishPulsePrim[1] = 255;
        sMinishPulsePrim[2] = 255;
        sMinishPulseStage = 0;
        sMinishPulseTimer = 20;
        sStickHeld = 0;
    }

    MinishKaleido_UpdatePulse();

    s8 curIdx = gCustomItemState.minishCapCursorIdx;

    s16 stickX = input->rel.stick_x;
    s16 stickY = input->rel.stick_y;
    f32 stickMag = sqrtf((f32)(stickX * stickX + stickY * stickY));

    if (stickMag > 30.0f) {
        if (!sStickHeld) {
            s8 nextIdx = MinishKaleido_FindNearest(curIdx, stickX, stickY);
            if (nextIdx >= 0) {
                gCustomItemState.minishCapCursorIdx = nextIdx;
                Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
            sStickHeld = 1;
        }
    } else {
        sStickHeld = 0;
    }

    curIdx = gCustomItemState.minishCapCursorIdx;

    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        if (MinishCap_IsPodSoilUnlocked(curIdx)) {
            gCustomItemState.minishCapDestIdx = curIdx;
            gCustomItemState.minishCapConfirmed = 1;
            pauseCtx->state = 0;
            gCustomItemState.minishCapWarpMode = 0;
            sMinishInitialized = 0;

            Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        } else {
            Audio_PlaySoundGeneral(NA_SE_SY_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
        return;
    }

    if (CHECK_BTN_ALL(input->press.button, BTN_B) || CHECK_BTN_ALL(input->press.button, BTN_START)) {
        gCustomItemState.minishCapWarpMode = 0;
        gCustomItemState.minishCapConfirmed = 0;
        gCustomItemState.minishCapDestIdx = -1;
        pauseCtx->state = 0;
        sMinishInitialized = 0;

        Audio_PlaySoundGeneral(NA_SE_SY_CANCEL, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
}

// ============================================================
// Vertex helpers
// ============================================================
// Draw — 2D overlay on OVERLAY_DISP using texture rectangles
// Scaled 1.2x from kaleido coordinates, centered on 320x240 screen
// ============================================================

// Convert kaleido coords to screen 10.2 fixed-point (scale 6/5, centered, shifted up 24px)
#define MK_YSHIFT 96
#define MKX(kx) (640 + (s32)(kx)*24 / 5)
#define MKY(ky) (480 - MK_YSHIFT - (s32)(ky)*24 / 5)

// Cloud positions in kaleido coords (from D_8082AEC0/D_8082AF78 for pageIndex=4)
static s16 sCloudPosX[] = {
    0x002F, 0xFFCF, 0xFFEF, 0xFFF1, 0xFFF7, 0x0018, 0x002B, 0x000E,
    0x0009, 0x0026, 0x0052, 0x0047, 0xFFB4, 0xFFA9, 0xFF94, 0xFFCA,
};
static s16 sCloudPosY[] = {
    0x000F, 0x0028, 0x000B, 0x002D, 0x0034, 0x0025, 0x0024, 0x0039,
    0x0036, 0x0021, 0x001F, 0x002D, 0x0020, 0x002A, 0x0031, 0xFFF6,
};

void MinishKaleido_Draw(PlayState* play) {
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    s8 curIdx = gCustomItemState.minishCapCursorIdx;
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
    gSPWideTextureRectangle(OVERLAY_DISP++, 0, 0, SCREEN_WIDTH << 2, SCREEN_HEIGHT << 2, G_TX_RENDERTILE, 0, 0, 0, 0);
    gDPPipeSync(OVERLAY_DISP++);

    // ---- 2. Frame (IA8, 80x32 tiles, 3 columns x 5 rows = 240x160 kaleido units) ----
    // Frame column X edges: -120, -40, +40, +120
    // Frame row Y edges: +80, +48, +16, -16, -48, -80
    {
        Gfx_SetupDL_39Overlay(gfxCtx);
        gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

        static s16 sColX[] = { -120, -40, 40, 120 };
        static s16 sRowY[] = { 80, 48, 16, -16, -48, -80 };
        // Approximate per-column color (average of vertex gradient in vanilla)
        static u8 sColR[] = { 110, 140, 110 };
        static u8 sColG[] = { 50, 60, 50 };
        static u8 sColB[] = { 45, 60, 45 };

        for (s16 col = 0; col < 3; col++) {
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, sColR[col], sColG[col], sColB[col], 255);

            s32 xl = MKX(sColX[col]);
            s32 xh = MKX(sColX[col + 1]);
            s32 dsdx = 80 * 4096 / (xh - xl);

            for (s16 row = 0; row < 5; row++) {
                s32 yl = MKY(sRowY[row]);
                s32 yh = MKY(sRowY[row + 1]);
                s32 dtdy = 32 * 4096 / (yh - yl);

                gDPLoadTextureBlock(OVERLAY_DISP++, sMapTexs[lang][col * 5 + row], G_IM_FMT_IA, G_IM_SIZ_8b, 80, 32, 0,
                                    G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                    G_TX_NOLOD, G_TX_NOLOD);
                gSPWideTextureRectangle(OVERLAY_DISP++, xl, yl, xh, yh, G_TX_RENDERTILE, 0, 0, dsdx, dtdy);
            }
        }
    }

    // ---- 3. Map (CI8 216x128, 15 strips) ----
    // Map spans from kaleido (-108, 58) to (108, -70)
    {
        gDPPipeSync(OVERLAY_DISP++);
        gDPSetTextureFilter(OVERLAY_DISP++, G_TF_POINT);
        gDPLoadTLUT_pal256(OVERLAY_DISP++, gWorldMapImageTLUT);
        gDPSetTextureLUT(OVERLAY_DISP++, G_TT_RGBA16);
        gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, 255);

        s32 mapXL = MKX(-108);
        s32 mapXH = MKX(108);
        s32 mapDsdx = 216 * 4096 / (mapXH - mapXL);

        for (s16 i = 0; i < 15; i++) {
            s16 stripH = (i < 14) ? 9 : 2;
            s16 ky0 = 58 - i * 9;
            s16 ky1 = ky0 - stripH;
            s32 syl = MKY(ky0);
            s32 syh = MKY(ky1);
            s32 mapDtdy = stripH * 4096 / (syh - syl);

            gDPLoadMultiTile(OVERLAY_DISP++, gWorldMapImageTex, 0, G_TX_RENDERTILE, G_IM_FMT_CI, G_IM_SIZ_8b, 216, 128,
                             0, i * 9, 215, i * 9 + stripH - 1, 0, G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR,
                             G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            gDPSetTileSize(OVERLAY_DISP++, G_TX_RENDERTILE, 0, 0, (216 - 1) << G_TEXTURE_IMAGE_FRAC,
                           (stripH - 1) << G_TEXTURE_IMAGE_FRAC);
            gSPWideTextureRectangle(OVERLAY_DISP++, mapXL, syl, mapXH, syh, G_TX_RENDERTILE, 0, 0, mapDsdx, mapDtdy);
        }
    }

    // ---- 4. Clouds (I4 textures, hide undiscovered areas) ----
    {
        gDPPipeSync(OVERLAY_DISP++);
        gDPSetTextureFilter(OVERLAY_DISP++, G_TF_BILERP);
        gDPSetTextureLUT(OVERLAY_DISP++, G_TT_NONE);
        Gfx_SetupDL_39Overlay(gfxCtx);
        gDPSetCombineLERP(OVERLAY_DISP++, 1, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0, 1, 0, PRIMITIVE, 0, TEXEL0, 0,
                          PRIMITIVE, 0);
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 235, 235, 235, 255);

        for (s16 i = 0; i < 16; i++) {
            if (!(gSaveContext.worldMapAreaData & gBitFlags[sCloudFlagNums[i]])) {
                s32 cxl = MKX(sCloudPosX[i]);
                s32 cyl = MKY(sCloudPosY[i]);
                s32 cxh = MKX(sCloudPosX[i] + sCloudWidths[i]);
                s32 cyh = MKY(sCloudPosY[i] - sCloudHeights[i]);
                s32 cDsdx = sCloudWidths[i] * 4096 / (cxh - cxl);
                s32 cDtdy = sCloudHeights[i] * 4096 / (cyh - cyl);

                gDPLoadTextureBlock_4b(OVERLAY_DISP++, sCloudTexs[i], G_IM_FMT_I, sCloudWidths[i], sCloudHeights[i], 0,
                                       G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK,
                                       G_TX_NOLOD, G_TX_NOLOD);
                gSPWideTextureRectangle(OVERLAY_DISP++, cxl, cyl, cxh, cyh, G_TX_RENDERTILE, 0, 0, cDsdx, cDtdy);
            }
        }
    }

    // ---- 5. Area boxes for pod soils (uniform size, fill-rect outlines) ----
    {
        gDPPipeSync(OVERLAY_DISP++);
        gDPSetCycleType(OVERLAY_DISP++, G_CYC_FILL);

        for (s16 i = 0; i < POD_SOIL_COUNT; i++) {
            s32 unlocked = MinishCap_IsPodSoilUnlocked(i);
            u8 r, g, b;

            if (i == curIdx) {
                r = (u8)sMinishPulsePrim[0];
                g = (u8)sMinishPulsePrim[1];
                b = (u8)sMinishPulsePrim[2];
            } else if (unlocked) {
                r = 100;
                g = 255;
                b = 255;
            } else {
                r = 100;
                g = 100;
                b = 100;
            }

            u32 packed = (GPACK_RGBA5551(r >> 3, g >> 3, b >> 3, 1) << 16) | GPACK_RGBA5551(r >> 3, g >> 3, b >> 3, 1);
            gDPSetFillColor(OVERLAY_DISP++, packed);

            // Box pixel coords from center (convert 10.2 to pixels via >> 2)
            s32 x1 = MKX(sSoilAreaData[i].centerX - UBOX_HW) >> 2;
            s32 y1 = MKY(sSoilAreaData[i].centerY + UBOX_HH) >> 2;
            s32 x2 = MKX(sSoilAreaData[i].centerX + UBOX_HW) >> 2;
            s32 y2 = MKY(sSoilAreaData[i].centerY - UBOX_HH) >> 2;

            // 2px outline: top, bottom, left, right
            gDPFillRectangle(OVERLAY_DISP++, x1, y1, x2, y1 + 2);
            gDPFillRectangle(OVERLAY_DISP++, x1, y2 - 2, x2, y2);
            gDPFillRectangle(OVERLAY_DISP++, x1, y1, x1 + 2, y2);
            gDPFillRectangle(OVERLAY_DISP++, x2 - 2, y1, x2, y2);
        }

        gDPPipeSync(OVERLAY_DISP++);
    }

    // ---- 5b. Pecori icon at selected box (flicker) ----
    if (curIdx >= 0 && curIdx < POD_SOIL_COUNT) {
        static s16 sPecoriFlickerTimer = 0;
        sPecoriFlickerTimer++;

        if ((sPecoriFlickerTimer % 16) < 11) {
            gDPSetCycleType(OVERLAY_DISP++, G_CYC_1CYCLE);
            gDPSetTextureFilter(OVERLAY_DISP++, G_TF_BILERP);
            gDPSetTextureLUT(OVERLAY_DISP++, G_TT_NONE);
            Gfx_SetupDL_39Overlay(gfxCtx);
            gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, 255);

            s32 pcX = sSoilAreaData[curIdx].centerX;
            s32 pcY = sSoilAreaData[curIdx].centerY;
            s32 pXL = MKX(pcX - 10);
            s32 pYL = MKY(pcY + 10);
            s32 pXH = MKX(pcX + 10);
            s32 pYH = MKY(pcY - 10);
            s32 pDsdx = 32 * 4096 / (pXH - pXL);
            s32 pDtdy = 32 * 4096 / (pYH - pYL);

            gDPLoadTextureBlock(OVERLAY_DISP++, gItemIconPecoriTex, G_IM_FMT_RGBA, G_IM_SIZ_32b, 32, 32, 0,
                                G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK,
                                G_TX_NOLOD, G_TX_NOLOD);
            gSPWideTextureRectangle(OVERLAY_DISP++, pXL, pYL, pXH, pYH, G_TX_RENDERTILE, 0, 0, pDsdx, pDtdy);
            gDPPipeSync(OVERLAY_DISP++);
        }
    }

    // ---- 6. Text on parchment (vanilla position, bottom-right of map) ----
    // Vanilla kaleido coords: "CURRENT POSITION" at (28,-26), area name at (19,-36)
    if (curIdx >= 0 && curIdx < POD_SOIL_COUNT) {
        s32 unlocked = MinishCap_IsPodSoilUnlocked(curIdx);

        // Restore 1-cycle mode after fill rects
        gDPSetCycleType(OVERLAY_DISP++, G_CYC_1CYCLE);
        gDPSetTextureFilter(OVERLAY_DISP++, G_TF_BILERP);
        gDPSetTextureLUT(OVERLAY_DISP++, G_TT_NONE);
        Gfx_SetupDL_39Overlay(gfxCtx);

        // "CURRENT POSITION" title (I4, 64x8) on parchment
        gDPSetCombineLERP(OVERLAY_DISP++, 1, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0, 1, 0, PRIMITIVE, 0, TEXEL0, 0,
                          PRIMITIVE, 0);
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 0, 0, 0, 255);

        {
            s32 cpXL = MKX(20);
            s32 cpYL = MKY(-26);
            s32 cpXH = MKX(84);
            s32 cpYH = MKY(-34);
            s32 cpDsdx = 64 * 4096 / (cpXH - cpXL);
            s32 cpDtdy = 8 * 4096 / (cpYH - cpYL);

            gDPLoadTextureBlock_4b(OVERLAY_DISP++, sCurrentPosTitleTexs[lang], G_IM_FMT_I, 64, 8, 0,
                                   G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK,
                                   G_TX_NOLOD, G_TX_NOLOD);
            gSPWideTextureRectangle(OVERLAY_DISP++, cpXL, cpYL, cpXH, cpYH, G_TX_RENDERTILE, 0, 0, cpDsdx, cpDtdy);
        }

        gDPPipeSync(OVERLAY_DISP++);

        // Skulltula icon (RGBA32, 24x24) — left of name when locked
        if (!unlocked) {
            gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 200, 200, 200, 255);

            s32 gsXL = MKX(5);
            s32 gsYL = MKY(-38);
            s32 gsXH = MKX(19);
            s32 gsYH = MKY(-54);
            s32 gsDsdx = 24 * 4096 / (gsXH - gsXL);
            s32 gsDtdy = 24 * 4096 / (gsYH - gsYL);

            gDPLoadTextureBlock(OVERLAY_DISP++, gQuestIconGoldSkulltulaTex, G_IM_FMT_RGBA, G_IM_SIZ_32b, 24, 24, 0,
                                G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK,
                                G_TX_NOLOD, G_TX_NOLOD);
            gSPWideTextureRectangle(OVERLAY_DISP++, gsXL, gsYL, gsXH, gsYH, G_TX_RENDERTILE, 0, 0, gsDsdx, gsDtdy);
            gDPPipeSync(OVERLAY_DISP++);
        }

        // Area name (IA8, 80x32) below title on parchment
        // Unlocked: cyan text (150,255,255). Locked: desaturated grey (120,120,120)
        gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                          PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);

        if (unlocked) {
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 150, 255, 255, 255);
        } else {
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 120, 120, 120, 255);
        }
        gDPSetEnvColor(OVERLAY_DISP++, 0, 0, 0, 0);

        {
            s32 nXL = MKX(19);
            s32 nYL = MKY(-36);
            s32 nXH = MKX(99);
            s32 nYH = MKY(-68);
            s32 nDsdx = 80 * 4096 / (nXH - nXL);
            s32 nDtdy = 32 * 4096 / (nYH - nYL);

            gDPLoadTextureBlock(OVERLAY_DISP++, sSoilAreaData[curIdx].nameTex[lang], G_IM_FMT_IA, G_IM_SIZ_8b, 80, 32,
                                0, G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK,
                                G_TX_NOLOD, G_TX_NOLOD);
            gSPWideTextureRectangle(OVERLAY_DISP++, nXL, nYL, nXH, nYH, G_TX_RENDERTILE, 0, 0, nDsdx, nDtdy);
        }
    }

    CLOSE_DISPS(gfxCtx);
}
