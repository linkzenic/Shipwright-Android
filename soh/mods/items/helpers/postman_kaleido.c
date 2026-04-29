/**
 * postman_kaleido.c - Standalone overlay for the Postman's Hat warp map.
 *
 * Mirrors minish_kaleido.c visually (same map frame + world map + clouds),
 * but renders a mailbox icon at each mailbox position instead of pod-soil
 * boxes. Navigation identical (analog-stick nearest-neighbour).
 *
 * Game is frozen by setting pauseCtx->state != 0 (triggers isPaused).
 * z_play.c redirects KaleidoScopeCall_Update/Draw when postmanHatWarpMode is set.
 */

#include "postman_kaleido.h"
#include "../custom_items.h"
#include "../logic/item_postman_hat.h"
#include "textures/icon_item_static/icon_item_static.h"
#include "textures/icon_item_field_static/icon_item_field_static.h"
#include "textures/icon_item_nes_static/icon_item_nes_static.h"
#include "textures/icon_item_ger_static/icon_item_ger_static.h"
#include "textures/icon_item_fra_static/icon_item_fra_static.h"
#include "textures/icon_item_jpn_static/icon_item_jpn_static.h"
#include "textures/map_name_static/map_name_static.h"
#include "textures/icon_item_24_static/icon_item_24_static.h"

#include "assets/soh_assets.h"

// MM Letter-to-Kafei icon (RGBA32, 32x32) from mm.o2r (icon_item_static_yar).
// Used as the selector icon in the postman kaleido, mirroring how the Minish
// Cap kaleido uses gItemIconPecoriTex for its selector.
#include "mods/mm_sources/archives/icon_item_static_yar.h"

// ============================================================
// MAP page frame (same as z_kaleido_scope_PAL sPostmanMapENGTexs)
// ============================================================

static void* sPostmanMapENGTexs[] = {
    gPauseMap00Tex,    gPauseMap01Tex, gPauseMap02Tex, gPauseMap03Tex, gPauseMap04Tex,
    gPauseMap10ENGTex, gPauseMap11Tex, gPauseMap12Tex, gPauseMap13Tex, gPauseMap14Tex,
    gPauseMap20Tex,    gPauseMap21Tex, gPauseMap22Tex, gPauseMap23Tex, gPauseMap24Tex,
};
static void* sPostmanMapGERTexs[] = {
    gPauseMap00Tex,    gPauseMap01Tex, gPauseMap02Tex, gPauseMap03Tex, gPauseMap04Tex,
    gPauseMap10GERTex, gPauseMap11Tex, gPauseMap12Tex, gPauseMap13Tex, gPauseMap14Tex,
    gPauseMap20Tex,    gPauseMap21Tex, gPauseMap22Tex, gPauseMap23Tex, gPauseMap24Tex,
};
static void* sPostmanMapFRATexs[] = {
    gPauseMap00Tex,    gPauseMap01Tex, gPauseMap02Tex, gPauseMap03Tex, gPauseMap04Tex,
    gPauseMap10FRATex, gPauseMap11Tex, gPauseMap12Tex, gPauseMap13Tex, gPauseMap14Tex,
    gPauseMap20Tex,    gPauseMap21Tex, gPauseMap22Tex, gPauseMap23Tex, gPauseMap24Tex,
};
static void* sPostmanMapJPNTexs[] = {
    gPauseMap00Tex,    gPauseMap01Tex, gPauseMap02Tex, gPauseMap03Tex, gPauseMap04Tex,
    gPauseMap10JPNTex, gPauseMap11Tex, gPauseMap12Tex, gPauseMap13Tex, gPauseMap14Tex,
    gPauseMap20Tex,    gPauseMap21Tex, gPauseMap22Tex, gPauseMap23Tex, gPauseMap24Tex,
};
static void** sPostmanMapTexs[] = { sPostmanMapENGTexs, sPostmanMapGERTexs, sPostmanMapFRATexs, sPostmanMapJPNTexs };

// Cloud textures + flag numbers (copied from z_kaleido_map_PAL.c)
static void* sPostmanCloudTexs[] = {
    gWorldMapCloud16Tex, gWorldMapCloud15Tex, gWorldMapCloud14Tex, gWorldMapCloud13Tex,
    gWorldMapCloud12Tex, gWorldMapCloud11Tex, gWorldMapCloud10Tex, gWorldMapCloud9Tex,
    gWorldMapCloud8Tex,  gWorldMapCloud7Tex,  gWorldMapCloud6Tex,  gWorldMapCloud5Tex,
    gWorldMapCloud4Tex,  gWorldMapCloud3Tex,  gWorldMapCloud2Tex,  gWorldMapCloud1Tex,
};
static u16 sPostmanCloudFlagNums[] = {
    0x05, 0x00, 0x13, 0x0E, 0x0F, 0x01, 0x02, 0x10, 0x12, 0x03, 0x07, 0x08, 0x09, 0x0C, 0x0B, 0x06,
};
static s16 sPostmanCloudWidths[] = {
    32, 112, 32, 48, 32, 32, 32, 48, 32, 64, 32, 48, 48, 48, 48, 64,
};
static s16 sPostmanCloudHeights[] = {
    24, 72, 13, 22, 19, 20, 19, 27, 14, 26, 22, 21, 49, 32, 45, 60,
};
static s16 sPostmanCloudPosX[] = {
    0x002F, 0xFFCF, 0xFFEF, 0xFFF1, 0xFFF7, 0x0018, 0x002B, 0x000E,
    0x0009, 0x0026, 0x0052, 0x0047, 0xFFB4, 0xFFA9, 0xFF94, 0xFFCA,
};
static s16 sPostmanCloudPosY[] = {
    0x000F, 0x0028, 0x000B, 0x002D, 0x0034, 0x0025, 0x0024, 0x0039,
    0x0036, 0x0021, 0x001F, 0x002D, 0x0020, 0x002A, 0x0031, 0xFFF6,
};

// Per-mailbox area name texture (IA8, 80x32) — order matches sMailboxTable.
// Only ENG for now; per-language arrays can be added mirroring minish_kaleido.
static void* sPostmanNameTexsENG[POSTMAN_MAILBOX_COUNT] = {
    gKokiriForestPositionNameENGTex,
    gMarketPositionNameENGTex,
    gKakarikoVillagePositionNameENGTex,
    gLonLonRanchPositionNameENGTex,
    gDeathMountainTrailPositionNameENGTex,
    gZorasRiverPositionNameENGTex,
    gGerudoValleyPositionNameENGTex,
};

// ============================================================
// Pulsing color for selected mailbox
// ============================================================

static s16 sPostmanPulsePrim[] = { 100, 255, 255 };
static s16 sPostmanPulseTarget[][3] = {
    { 255, 255, 100 },
    { 100, 255, 255 },
};
static s16 sPostmanPulseStage = 0;
static s16 sPostmanPulseTimer = 20;

static void PostmanKaleido_UpdatePulse(void) {
    for (s32 c = 0; c < 3; c++) {
        s16 diff = sPostmanPulseTarget[sPostmanPulseStage][c] - sPostmanPulsePrim[c];
        s16 step = diff / (sPostmanPulseTimer > 0 ? sPostmanPulseTimer : 1);
        sPostmanPulsePrim[c] += step;
    }
    sPostmanPulseTimer--;
    if (sPostmanPulseTimer <= 0) {
        for (s32 c = 0; c < 3; c++)
            sPostmanPulsePrim[c] = sPostmanPulseTarget[sPostmanPulseStage][c];
        sPostmanPulseStage ^= 1;
        sPostmanPulseTimer = 20;
    }
}

// ============================================================
// Analog-stick navigation (nearest-neighbour in stick direction)
// ============================================================

static s8 sPostmanStickHeld = 0;
static s8 sPostmanInitialized = 0;

static s8 PostmanKaleido_FindNearest(s8 currentIdx, s16 stickX, s16 stickY) {
    f32 stickMag = sqrtf((f32)(stickX * stickX + stickY * stickY));
    if (stickMag < 30.0f)
        return -1;

    f32 stickAngle = atan2f((f32)stickX, (f32)stickY);

    f32 curCX = sMailboxTable[currentIdx].mapCX;
    f32 curCY = sMailboxTable[currentIdx].mapCY;

    s8 bestIdx = -1;
    f32 bestScore = -1.0f;

    for (s32 i = 0; i < POSTMAN_MAILBOX_COUNT; i++) {
        if (i == currentIdx)
            continue;

        f32 dx = sMailboxTable[i].mapCX - curCX;
        f32 dy = sMailboxTable[i].mapCY - curCY;
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
// Update
// ============================================================

void PostmanKaleido_Update(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    Input* input = &play->state.input[0];

    if (!sPostmanInitialized) {
        sPostmanInitialized = 1;

        gCustomItemState.postmanHatCursorIdx = 0;
        for (s32 i = 0; i < POSTMAN_MAILBOX_COUNT; i++) {
            if (PostmanHat_IsMailboxUnlocked(i)) {
                gCustomItemState.postmanHatCursorIdx = i;
                break;
            }
        }

        sPostmanPulsePrim[0] = 100;
        sPostmanPulsePrim[1] = 255;
        sPostmanPulsePrim[2] = 255;
        sPostmanPulseStage = 0;
        sPostmanPulseTimer = 20;
        sPostmanStickHeld = 0;
    }

    PostmanKaleido_UpdatePulse();

    // Skip input on the first frame after the kaleido opens. Without this,
    // the A press from the mailbox's talk-accept leaks into this Update in
    // the same tick: Mailbox_Update runs earlier in the frame and sets
    // pauseCtx->state=1, then z_play.c routes the rest of that same frame
    // to PostmanKaleido_Update, which would see A still pressed and
    // instantly confirm a destination. Great Fairy Mask uses the same guard
    // (mm_mask_wear.cpp: sGreatFairyInputSkip). Pulse animation still runs
    // above so the selected icon starts moving from frame 1.
    if (gCustomItemState.postmanHatInputSkip) {
        gCustomItemState.postmanHatInputSkip = 0;
        return;
    }

    s8 curIdx = gCustomItemState.postmanHatCursorIdx;

    s16 stickX = input->rel.stick_x;
    s16 stickY = input->rel.stick_y;
    f32 stickMag = sqrtf((f32)(stickX * stickX + stickY * stickY));

    if (stickMag > 30.0f) {
        if (!sPostmanStickHeld) {
            s8 nextIdx = PostmanKaleido_FindNearest(curIdx, stickX, stickY);
            if (nextIdx >= 0) {
                gCustomItemState.postmanHatCursorIdx = nextIdx;
                Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
            sPostmanStickHeld = 1;
        }
    } else {
        sPostmanStickHeld = 0;
    }

    curIdx = gCustomItemState.postmanHatCursorIdx;

    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        if (PostmanHat_IsMailboxUnlocked(curIdx)) {
            gCustomItemState.postmanHatDestIdx = curIdx;
            gCustomItemState.postmanHatConfirmed = 1;
            pauseCtx->state = 0;
            gCustomItemState.postmanHatWarpMode = 0;
            sPostmanInitialized = 0;

            Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        } else {
            Audio_PlaySoundGeneral(NA_SE_SY_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
        return;
    }

    if (CHECK_BTN_ALL(input->press.button, BTN_B) || CHECK_BTN_ALL(input->press.button, BTN_START)) {
        gCustomItemState.postmanHatWarpMode = 0;
        gCustomItemState.postmanHatConfirmed = 0;
        gCustomItemState.postmanHatDestIdx = -1;
        pauseCtx->state = 0;
        sPostmanInitialized = 0;

        Audio_PlaySoundGeneral(NA_SE_SY_CANCEL, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
}

// ============================================================
// Draw
// ============================================================

// MKX / MKY / MK_YSHIFT are also defined in minish_kaleido.c with the same
// values. Guarded so a standalone TU still compiles while a unity TU after
// minish_kaleido.c silently reuses those definitions.
#ifndef MK_YSHIFT
#define MK_YSHIFT 96
#define MKX(kx) (640 + (s32)(kx)*24 / 5)
#define MKY(ky) (480 - MK_YSHIFT - (s32)(ky)*24 / 5)
#endif

// Mailbox icon half-dimensions in kaleido units.
enum {
    MAILBOX_HW = 8,
    MAILBOX_HH = 10,
};

void PostmanKaleido_Draw(PlayState* play) {
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    s8 curIdx = gCustomItemState.postmanHatCursorIdx;
    s16 lang = gSaveContext.language;
    if (lang < 0 || lang > 3)
        lang = 0;

    OPEN_DISPS(gfxCtx);

    // ---- 1. Semi-transparent dark background ----
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

    // ---- 2. MAP page frame ----
    {
        Gfx_SetupDL_39Overlay(gfxCtx);
        gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

        static s16 sColX[] = { -120, -40, 40, 120 };
        static s16 sRowY[] = { 80, 48, 16, -16, -48, -80 };
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

                gDPLoadTextureBlock(OVERLAY_DISP++, sPostmanMapTexs[lang][col * 5 + row], G_IM_FMT_IA, G_IM_SIZ_8b, 80, 32, 0,
                                    G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                    G_TX_NOLOD, G_TX_NOLOD);
                gSPWideTextureRectangle(OVERLAY_DISP++, xl, yl, xh, yh, G_TX_RENDERTILE, 0, 0, dsdx, dtdy);
            }
        }
    }

    // ---- 3. World map ----
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

    // ---- 4. Clouds (hide undiscovered areas) ----
    {
        gDPPipeSync(OVERLAY_DISP++);
        gDPSetTextureFilter(OVERLAY_DISP++, G_TF_BILERP);
        gDPSetTextureLUT(OVERLAY_DISP++, G_TT_NONE);
        Gfx_SetupDL_39Overlay(gfxCtx);
        gDPSetCombineLERP(OVERLAY_DISP++, 1, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0, 1, 0, PRIMITIVE, 0, TEXEL0, 0,
                          PRIMITIVE, 0);
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 235, 235, 235, 255);

        for (s16 i = 0; i < 16; i++) {
            if (!(gSaveContext.worldMapAreaData & gBitFlags[sPostmanCloudFlagNums[i]])) {
                s32 cxl = MKX(sPostmanCloudPosX[i]);
                s32 cyl = MKY(sPostmanCloudPosY[i]);
                s32 cxh = MKX(sPostmanCloudPosX[i] + sPostmanCloudWidths[i]);
                s32 cyh = MKY(sPostmanCloudPosY[i] - sPostmanCloudHeights[i]);
                s32 cDsdx = sPostmanCloudWidths[i] * 4096 / (cxh - cxl);
                s32 cDtdy = sPostmanCloudHeights[i] * 4096 / (cyh - cyl);

                gDPLoadTextureBlock_4b(OVERLAY_DISP++, sPostmanCloudTexs[i], G_IM_FMT_I, sPostmanCloudWidths[i], sPostmanCloudHeights[i], 0,
                                       G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK,
                                       G_TX_NOLOD, G_TX_NOLOD);
                gSPWideTextureRectangle(OVERLAY_DISP++, cxl, cyl, cxh, cyh, G_TX_RENDERTILE, 0, 0, cDsdx, cDtdy);
            }
        }
    }

    // ---- 5. Mailbox outlines (non-selected only) ----
    // The selected mailbox gets rendered as the Letter-to-Kafei icon in step
    // 5b instead — no box, no outline, just the icon with flicker.
    {
        gDPPipeSync(OVERLAY_DISP++);
        gDPSetCycleType(OVERLAY_DISP++, G_CYC_FILL);

        for (s16 i = 0; i < POSTMAN_MAILBOX_COUNT; i++) {
            if (i == curIdx)
                continue; // selected → drawn as icon, not as a box

            s32 unlocked = PostmanHat_IsMailboxUnlocked(i);
            u8 r, g, b;

            if (unlocked) {
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

            s32 x1 = MKX(sMailboxTable[i].mapCX - MAILBOX_HW) >> 2;
            s32 y1 = MKY(sMailboxTable[i].mapCY + MAILBOX_HH) >> 2;
            s32 x2 = MKX(sMailboxTable[i].mapCX + MAILBOX_HW) >> 2;
            s32 y2 = MKY(sMailboxTable[i].mapCY - MAILBOX_HH) >> 2;

            // 2px outline — top, bottom, left, right
            gDPFillRectangle(OVERLAY_DISP++, x1, y1, x2, y1 + 2);
            gDPFillRectangle(OVERLAY_DISP++, x1, y2 - 2, x2, y2);
            gDPFillRectangle(OVERLAY_DISP++, x1, y1, x1 + 2, y2);
            gDPFillRectangle(OVERLAY_DISP++, x2 - 2, y1, x2, y2);
        }

        gDPPipeSync(OVERLAY_DISP++);
    }

    // ---- 5b. Letter-to-Kafei icon at selected mailbox (flicker, no box) ----
    // Mirrors minish_kaleido.c step 5b (Pecori icon). Uses MM's
    // gItemIconLetterToKafeiTex from mm.o2r. 12-out-of-16-frame duty cycle
    // gives a gentle flicker without being distracting.
    if (curIdx >= 0 && curIdx < POSTMAN_MAILBOX_COUNT) {
        static s16 sLetterFlickerTimer = 0;
        sLetterFlickerTimer++;

        if ((sLetterFlickerTimer % 16) < 12) {
            gDPSetCycleType(OVERLAY_DISP++, G_CYC_1CYCLE);
            gDPSetTextureFilter(OVERLAY_DISP++, G_TF_BILERP);
            gDPSetTextureLUT(OVERLAY_DISP++, G_TT_NONE);
            Gfx_SetupDL_39Overlay(gfxCtx);
            gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
            // Pulsing prim color for a subtle breathing effect on the icon.
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, (u8)sPostmanPulsePrim[0], (u8)sPostmanPulsePrim[1],
                            (u8)sPostmanPulsePrim[2], 255);

            // Icon half-extents in kaleido units (a bit bigger than the box
            // outline so it reads as a distinct selector, not a fill).
            s32 icX = sMailboxTable[curIdx].mapCX;
            s32 icY = sMailboxTable[curIdx].mapCY;
            s32 iXL = MKX(icX - 10);
            s32 iYL = MKY(icY + 10);
            s32 iXH = MKX(icX + 10);
            s32 iYH = MKY(icY - 10);
            s32 iDsdx = 32 * 4096 / (iXH - iXL);
            s32 iDtdy = 32 * 4096 / (iYH - iYL);

            gDPLoadTextureBlock(OVERLAY_DISP++, gItemIconLetterToKafeiTex, G_IM_FMT_RGBA, G_IM_SIZ_32b, 32, 32, 0,
                                G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK,
                                G_TX_NOLOD, G_TX_NOLOD);
            gSPWideTextureRectangle(OVERLAY_DISP++, iXL, iYL, iXH, iYH, G_TX_RENDERTILE, 0, 0, iDsdx, iDtdy);
            gDPPipeSync(OVERLAY_DISP++);
        }
    }

    // ---- 6. Current mailbox name (IA8 80x32) on parchment ----
    // Cyan when unlocked, grey when locked — mirrors minish_kaleido's step 6.
    // Also naturally returns the pipe to G_CYC_1CYCLE, preventing the fill
    // mode leak that hits "Unhandled OP code" in the next overlay pass.
    if (curIdx >= 0 && curIdx < POSTMAN_MAILBOX_COUNT) {
        s32 unlocked = PostmanHat_IsMailboxUnlocked(curIdx);

        gDPPipeSync(OVERLAY_DISP++);
        gDPSetCycleType(OVERLAY_DISP++, G_CYC_1CYCLE);
        gDPSetTextureFilter(OVERLAY_DISP++, G_TF_BILERP);
        gDPSetTextureLUT(OVERLAY_DISP++, G_TT_NONE);
        Gfx_SetupDL_39Overlay(gfxCtx);

        gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                          PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);

        if (unlocked) {
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 150, 255, 255, 255);
        } else {
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 120, 120, 120, 255);
        }
        gDPSetEnvColor(OVERLAY_DISP++, 0, 0, 0, 0);

        s32 nXL = MKX(19);
        s32 nYL = MKY(-36);
        s32 nXH = MKX(99);
        s32 nYH = MKY(-68);
        s32 nDsdx = 80 * 4096 / (nXH - nXL);
        s32 nDtdy = 32 * 4096 / (nYH - nYL);

        gDPLoadTextureBlock(OVERLAY_DISP++, sPostmanNameTexsENG[curIdx], G_IM_FMT_IA, G_IM_SIZ_8b, 80, 32, 0,
                            G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK,
                            G_TX_NOLOD, G_TX_NOLOD);
        gSPWideTextureRectangle(OVERLAY_DISP++, nXL, nYL, nXH, nYH, G_TX_RENDERTILE, 0, 0, nDsdx, nDtdy);
    } else {
        // Defensive: still restore 1-cycle mode even if curIdx is out-of-range.
        gDPSetCycleType(OVERLAY_DISP++, G_CYC_1CYCLE);
    }

    gDPPipeSync(OVERLAY_DISP++);

    CLOSE_DISPS(gfxCtx);
}
