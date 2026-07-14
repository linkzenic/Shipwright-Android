#include "z_kaleido_scope.h"
#include "textures/parameter_static/parameter_static.h"
#include "textures/icon_item_static/icon_item_static.h"
#include "soh/Enhancements/randomizer/ShuffleTradeItems.h"
#include "soh/Enhancements/randomizer/RocsFeatherCycle.h"
#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "soh/Enhancements/cosmetics/cosmeticsTypes.h"
#include "soh/OTRGlobals.h"

#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "mods/extended_inventory.h"
#include "mods/transformation_masks/transformation_masks.h"
#include "mods/extended_inventory.c"
#include "mods/items/custom_items.h"
#include "mods/items/logic/item_lantern.h" // LanternFireType enum (Vacía/Regular/Blue/Poe/Green)
#include "mods/items/logic/twilight_upgrade.h" // Clawshot / Gale Boomerang mode selectors
#include "expansions/sw97/sw97_config.h"

u8 gAmmoItems[] = {
    ITEM_STICK,   ITEM_NUT,  ITEM_BOMB, ITEM_BOW,  ITEM_NONE, ITEM_NONE, ITEM_SLINGSHOT, ITEM_NONE,
    ITEM_BOMBCHU, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_BEAN,      ITEM_NONE,
};

static s16 sEquipState = 0;
static s16 sEquipAnimTimer = 0;
static s16 sEquipMoveTimer = 10;

static s16 sAmmoVtxOffset[] = {
    0, 2, 4, 6, 99, 99, 8, 99, 10, 99, 99, 99, 99, 99, 12,
};

static s16 sAllAmmoVtxOffset[] = {
    0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46,
};

extern const char* _gAmmoDigit0Tex[];

// Twilight L-badge: draws gLButtonTex (vanilla 24x32 ia8) as a small 18x12
// quad CENTERED BELOW the item icon — mirrors the Roc's Feather
// "cycle extras" pattern (small mini-icons under the main item) instead of
// the overlapping corner badge we had before, which looked cramped.
//
// Implementation: separate static Vtx buffer so we don't mutate the
// kaleido's per-slot itemVtx array (which the cursor + equip animations
// also reach into). Caller passes the item slot's vertex offset `j` so we
// can derive the icon's top-left screen coords.
static Vtx sTwilightLBadgeVtx[4] = {
    { { { 0, 0, 0 }, 0, { 0,       0       }, { 0xFF, 0xFF, 0xFF, 0xFF } } },
    { { { 0, 0, 0 }, 0, { 24 << 5, 0       }, { 0xFF, 0xFF, 0xFF, 0xFF } } },
    { { { 0, 0, 0 }, 0, { 0,       32 << 5 }, { 0xFF, 0xFF, 0xFF, 0xFF } } },
    { { { 0, 0, 0 }, 0, { 24 << 5, 32 << 5 }, { 0xFF, 0xFF, 0xFF, 0xFF } } },
};

static void Kaleido_DrawTwilightLBadge(PauseContext* pauseCtx, GraphicsContext* gfxCtx, int j) {
    OPEN_DISPS(gfxCtx);

    // Icon is 32×32 anchored at itemVtx[j+0] (top-left). Badge goes
    // CENTERED HORIZONTALLY just below the icon's bottom edge.
    // Badge size: 18 wide × 14 tall (~75% scale of 24×32 texture; close
    // enough to native aspect that the L glyph stays readable).
    s16 icoX = pauseCtx->itemVtx[j + 0].v.ob[0];
    s16 icoY = pauseCtx->itemVtx[j + 0].v.ob[1];
    s16 bW = 18;
    s16 bH = 14;
    s16 bX = icoX + (32 - bW) / 2;   // (32 - 18) / 2 = 7 → centered under icon
    s16 bY = icoY - 32 - 2;          // 2px gap below the icon bottom (icoY - 32)

    sTwilightLBadgeVtx[0].v.ob[0] = bX;       sTwilightLBadgeVtx[0].v.ob[1] = bY;
    sTwilightLBadgeVtx[1].v.ob[0] = bX + bW;  sTwilightLBadgeVtx[1].v.ob[1] = bY;
    sTwilightLBadgeVtx[2].v.ob[0] = bX;       sTwilightLBadgeVtx[2].v.ob[1] = bY - bH;
    sTwilightLBadgeVtx[3].v.ob[0] = bX + bW;  sTwilightLBadgeVtx[3].v.ob[1] = bY - bH;

    gSPVertex(POLY_OPA_DISP++, sTwilightLBadgeVtx, 4, 0);
    POLY_OPA_DISP = KaleidoScope_QuadTextureIA8(POLY_OPA_DISP, (void*)gLButtonTex, 24, 32, 0);

    CLOSE_DISPS(gfxCtx);
}

// Returns 1 if the given item ID should display the Twilight L badge —
// i.e. the player owns the corresponding Twilight sub-upgrade.
static u8 Kaleido_ShouldDrawTwilightLBadge(s16 itemId) {
    if (itemId == ITEM_HOOKSHOT || itemId == ITEM_LONGSHOT) {
        return TwilightUpgrade_HasClawshot();
    }
    if (itemId == ITEM_BOOMERANG) {
        return TwilightUpgrade_HasGaleBoomerang();
    }
    return 0;
}

s8 ItemInSlotUsesAmmo(s16 slot) {
    s16 item = gSaveContext.inventory.items[slot];
    return item == ITEM_STICK || item == ITEM_NUT || item == ITEM_BOMB || item == ITEM_BOW || item == ITEM_SLINGSHOT ||
           item == ITEM_BOMBCHU || item == ITEM_BEAN;
}

void KaleidoScope_DrawAmmoCount(PauseContext* pauseCtx, GraphicsContext* gfxCtx, s16 item, int slot) {
    if (!GameInteractor_Should(VB_DRAW_AMMO_COUNT, true, &item)) {
        return;
    }

    s16 ammo;
    s16 i;

    OPEN_DISPS(gfxCtx);

    ammo = AMMO(item);

    gDPPipeSync(POLY_OPA_DISP++);

    if (!CHECK_AGE_REQ_SLOT(SLOT(item))) {
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 100, 100, 100, pauseCtx->alpha);
    } else {
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);

        if (ammo == 0) {
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 130, 130, 130, pauseCtx->alpha);
        } else if ((item == ITEM_BOMB && AMMO(item) == CUR_CAPACITY(UPG_BOMB_BAG)) ||
                   (item == ITEM_BOW && AMMO(item) == CUR_CAPACITY(UPG_QUIVER)) ||
                   (item == ITEM_SLINGSHOT && AMMO(item) == CUR_CAPACITY(UPG_BULLET_BAG)) ||
                   (item == ITEM_STICK && AMMO(item) == CUR_CAPACITY(UPG_STICKS)) ||
                   (item == ITEM_NUT && AMMO(item) == CUR_CAPACITY(UPG_NUTS)) || (item == ITEM_BOMBCHU && ammo == 50) ||
                   (item == ITEM_BEAN && ammo == 15) || GameInteractor_Should(VB_COLOR_AMMO_GREEN, false, item)) {
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 120, 255, 0, pauseCtx->alpha);
        }
    }

    for (i = 0; ammo >= 10; i++) {
        ammo -= 10;
    }

    gDPPipeSync(POLY_OPA_DISP++);

    if (i != 0) {
        gSPVertex(
            POLY_OPA_DISP++,
            &pauseCtx->itemVtx[((CVarGetInteger(CVAR_ENHANCEMENT("BetterAmmoRendering"), 0) ? sAllAmmoVtxOffset[slot]
                                                                                            : sAmmoVtxOffset[slot]) +
                                31) *
                               4],
            4, 0);

        gDPLoadTextureBlock(POLY_OPA_DISP++, ((u8*)_gAmmoDigit0Tex[i]), G_IM_FMT_IA, G_IM_SIZ_8b, 8, 8, 0,
                            G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                            G_TX_NOLOD);

        gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);
    }

    gSPVertex(POLY_OPA_DISP++,
              &pauseCtx->itemVtx[((CVarGetInteger(CVAR_ENHANCEMENT("BetterAmmoRendering"), 0) ? sAllAmmoVtxOffset[slot]
                                                                                              : sAmmoVtxOffset[slot]) +
                                  32) *
                                 4],
              4, 0);

    gDPLoadTextureBlock(POLY_OPA_DISP++, ((u8*)_gAmmoDigit0Tex[ammo]), G_IM_FMT_IA, G_IM_SIZ_8b, 8, 8, 0,
                        G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD);

    gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);

    CLOSE_DISPS(gfxCtx);
}

void KaleidoScope_SetCursorVtx(PauseContext* pauseCtx, u16 index, Vtx* vtx) {
    pauseCtx->cursorVtx[0].v.ob[0] = vtx[index].v.ob[0];
    pauseCtx->cursorVtx[0].v.ob[1] = vtx[index].v.ob[1];
    KaleidoScope_UpdateCursorSize(pauseCtx); // OTRTODO Why is this needed?
}

void KaleidoScope_SetItemCursorVtx(PauseContext* pauseCtx) {
    KaleidoScope_SetCursorVtx(pauseCtx, pauseCtx->cursorSlot[PAUSE_ITEM] * 4, pauseCtx->itemVtx);
}

#pragma region Item Cycling

s8 gCurrentItemCyclingSlot;

// Vertices for the extra items
static Vtx sCycleExtraItemVtx[] = {
    // Left Item
    VTX(-48, 16, 0, 0 << 5, 0 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(-16, 16, 0, 32 << 5, 0 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(-48, -16, 0, 0 << 5, 32 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(-16, -16, 0, 32 << 5, 32 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
    // Right Item
    VTX(16, 16, 0, 0 << 5, 0 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(48, 16, 0, 32 << 5, 0 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(16, -16, 0, 0 << 5, 32 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(48, -16, 0, 32 << 5, 32 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
};

// Vertices for the circle behind the items
static Vtx sCycleCircleVtx[] = {
    // Left Item
    VTX(-56, 24, 0, 0 << 5, 0 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(-8, 24, 0, 48 << 5, 0 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(-56, -24, 0, 0 << 5, 48 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(-8, -24, 0, 48 << 5, 48 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
    // Right Item
    VTX(8, 24, 0, 0 << 5, 0 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(56, 24, 0, 48 << 5, 0 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(8, -24, 0, 0 << 5, 48 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(56, -24, 0, 48 << 5, 48 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
};

// Vertices for A button indicator (coordinates 1.5x larger than texture size)
static Vtx sCycleAButtonVtx[] = {
    VTX(-18, 12, 0, 0 << 5, 0 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(18, 12, 0, 24 << 5, 0 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(-18, -12, 0, 0 << 5, 16 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(18, -12, 0, 24 << 5, 16 << 5, 0xFF, 0xFF, 0xFF, 0xFF),
};

// Track animation timers for each inventory slot
static int sSlotCycleActiveAnimTimer[24] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// Renders a left and/or right item for any item slot that can support cycling
void KaleidoScope_DrawItemCycleExtras(PlayState* play, u8 slot, u8 canCycle, u8 leftItem, u8 rightItem) {
    PauseContext* pauseCtx = &play->pauseCtx;

    u8 isCycling = gCurrentItemCyclingSlot == slot;

    OPEN_DISPS(play->state.gfxCtx);

    // Update active cycling animation timer
    if (isCycling) {
        if (sSlotCycleActiveAnimTimer[slot] < 5) {
            sSlotCycleActiveAnimTimer[slot]++;
        }
    } else {
        if (sSlotCycleActiveAnimTimer[slot] > 0) {
            sSlotCycleActiveAnimTimer[slot]--;
        }
    }

    u8 slotItem = gSaveContext.inventory.items[slot];
    u8 showLeftItem = leftItem != ITEM_NONE && slotItem != leftItem;
    u8 showRightItem = rightItem != ITEM_NONE && slotItem != rightItem && leftItem != rightItem;

    // Render the extra cycle items if at least the left or right item are valid
    if (canCycle && slotItem != ITEM_NONE && (showLeftItem || showRightItem)) {
        Matrix_Push();

        Vtx* itemTopLeft = &pauseCtx->itemVtx[slot * 4];
        Vtx* itemBottomRight = &itemTopLeft[3];

        s16 halfX = (itemBottomRight->v.ob[0] - itemTopLeft->v.ob[0]) / 2;
        s16 halfY = (itemBottomRight->v.ob[1] - itemTopLeft->v.ob[1]) / 2;

        Matrix_Translate(itemTopLeft->v.ob[0] + halfX, itemTopLeft->v.ob[1] + halfY, 0, MTXMODE_APPLY);

        f32 animScale = (f32)(5 - sSlotCycleActiveAnimTimer[slot]) / 5;

        // When not cycling or actively animating, shrink and move the items under the main slot item
        if (!isCycling || sSlotCycleActiveAnimTimer[slot] < 5) {
            f32 finalScale = 1.0f - (0.675f * animScale);
            Matrix_Translate(0, -15.0f * animScale, 0, MTXMODE_APPLY);
            Matrix_Scale(finalScale, finalScale, 1.0f, MTXMODE_APPLY);
        }

        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

        // Render A button indicator when hovered and not cycling
        if (!isCycling && sSlotCycleActiveAnimTimer[slot] == 0 && pauseCtx->cursorSlot[PAUSE_ITEM] == slot &&
            pauseCtx->cursorSpecialPos == 0) {
            Color_RGB8 aButtonColor = { 0, 100, 255 };
            if (CVarGetInteger(CVAR_COSMETIC("HUD.AButton.Changed"), 0)) {
                aButtonColor = CVarGetColor24(CVAR_COSMETIC("HUD.AButton.Value"), aButtonColor);
            } else if (CVarGetInteger(CVAR_COSMETIC("DefaultColorScheme"), COLORSCHEME_N64) == COLORSCHEME_GAMECUBE) {
                aButtonColor = (Color_RGB8){ 0, 255, 100 };
            }

            gSPVertex(POLY_OPA_DISP++, sCycleAButtonVtx, 4, 0);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, aButtonColor.r, aButtonColor.g, aButtonColor.b, pauseCtx->alpha);
            gDPLoadTextureBlock(POLY_OPA_DISP++, gABtnSymbolTex, G_IM_FMT_IA, G_IM_SIZ_8b, 24, 16, 0,
                                G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 4, 4, G_TX_NOLOD, G_TX_NOLOD);
            gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);
        }

        // Render a dark circle behind the extra items when cycling
        if (isCycling) {
            gSPVertex(POLY_OPA_DISP++, sCycleCircleVtx, 8, 0);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 0, 0, 0, pauseCtx->alpha * (1.0f - animScale));
            gDPLoadTextureBlock_4b(POLY_OPA_DISP++, gPausePromptCursorTex, G_IM_FMT_I, 48, 48, 0,
                                   G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                   G_TX_NOLOD, G_TX_NOLOD);

            if (showLeftItem) {
                gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);
            }
            if (showRightItem) {
                gSP1Quadrangle(POLY_OPA_DISP++, 4, 6, 7, 5, 0);
            }
        }

        // Render left and right items
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
        gSPVertex(POLY_OPA_DISP++, sCycleExtraItemVtx, 8, 0);

        if (showLeftItem) {
            if (!CHECK_AGE_REQ_ITEM(leftItem) || ExtInv_IsTransformRestricted(leftItem)) {
                gDPSetGrayscaleColor(POLY_OPA_DISP++, 109, 109, 109, 255);
                gSPGrayscale(POLY_OPA_DISP++, true);
            }
            KaleidoScope_DrawQuadTextureRGBA32(play->state.gfxCtx, ExtInv_GetItemIcon(leftItem), 32, 32, 0);
            gSPGrayscale(POLY_OPA_DISP++, false);
        }
        if (showRightItem) {
            if (!CHECK_AGE_REQ_ITEM(rightItem) || ExtInv_IsTransformRestricted(rightItem)) {
                gDPSetGrayscaleColor(POLY_OPA_DISP++, 109, 109, 109, 255);
                gSPGrayscale(POLY_OPA_DISP++, true);
            }
            KaleidoScope_DrawQuadTextureRGBA32(play->state.gfxCtx, ExtInv_GetItemIcon(rightItem), 32, 32, 4);
            gSPGrayscale(POLY_OPA_DISP++, false);
        }

        Matrix_Pop();
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

void KaleidoScope_HandleItemCycleExtras(PlayState* play, u8 slot, bool canCycle, u8 leftItem, u8 rightItem,
                                        bool replaceCButtons) {
    Input* input = &play->state.input[0];
    PauseContext* pauseCtx = &play->pauseCtx;
    bool dpad = (CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0) && !CHECK_BTN_ALL(input->cur.button, BTN_CUP));
    u8 slotItem = gSaveContext.inventory.items[slot];
    u8 hasLeftItem = leftItem != ITEM_NONE && slotItem != leftItem;
    u8 hasRightItem = rightItem != ITEM_NONE && slotItem != rightItem && leftItem != rightItem;

    if (canCycle && pauseCtx->cursorSlot[PAUSE_ITEM] == slot && CHECK_BTN_ALL(input->press.button, BTN_A) &&
        (hasLeftItem || hasRightItem)) {
        Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        gCurrentItemCyclingSlot = gCurrentItemCyclingSlot == slot ? -1 : slot;
    }
    if (gCurrentItemCyclingSlot == slot) {
        pauseCtx->cursorColorSet = 8;
        if ((pauseCtx->stickRelX > 30 || pauseCtx->stickRelY > 30) ||
            dpad && CHECK_BTN_ANY(input->press.button, BTN_DRIGHT | BTN_DUP)) {
            Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            if (replaceCButtons) {
                for (int i = 1; i < ARRAY_COUNT(gSaveContext.equips.buttonItems); i++) {
                    if (gSaveContext.equips.buttonItems[i] == gSaveContext.inventory.items[slot]) {
                        if (CHECK_AGE_REQ_ITEM(rightItem)) {
                            gSaveContext.equips.buttonItems[i] = rightItem;
                            Interface_LoadItemIcon1(play, i);
                        } else {
                            gSaveContext.equips.buttonItems[i] = ITEM_NONE;
                        }
                        break;
                    }
                }
            }
            gSaveContext.inventory.items[slot] = rightItem;
        } else if ((pauseCtx->stickRelX < -30 || pauseCtx->stickRelY < -30) ||
                   dpad && CHECK_BTN_ANY(input->press.button, BTN_DLEFT | BTN_DDOWN)) {
            Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            if (replaceCButtons) {
                for (int i = 1; i < ARRAY_COUNT(gSaveContext.equips.buttonItems); i++) {
                    if (gSaveContext.equips.buttonItems[i] == gSaveContext.inventory.items[slot]) {
                        if (CHECK_AGE_REQ_ITEM(leftItem)) {
                            gSaveContext.equips.buttonItems[i] = leftItem;
                            Interface_LoadItemIcon1(play, i);
                        } else {
                            gSaveContext.equips.buttonItems[i] = ITEM_NONE;
                        }
                        break;
                    }
                }
            }
            gSaveContext.inventory.items[slot] = leftItem;
        }
        gCurrentItemCyclingSlot = pauseCtx->cursorSlot[PAUSE_ITEM] == slot ? slot : -1;
    }
}

bool CanMaskSelect() {
    if (IS_RANDO) {
        return ((CVarGetInteger(CVAR_ENHANCEMENT("MaskSelect"), 0) ||
                 Randomizer_GetSettingValue(RSK_MASK_QUEST) != RO_MASK_QUEST_VANILLA) &&
                Flags_GetRandomizerInf(RAND_INF_ZELDAS_LETTER) &&
                Flags_GetInfTable(INFTABLE_SHOWED_ZELDAS_LETTER_TO_GATE_GUARD)) ||
               Randomizer_GetSettingValue(RSK_MASK_QUEST) == RO_MASK_QUEST_SHUFFLE;
    }

    // only allow mask select when:
    // the shop is open:
    // * zelda's letter check: Flags_GetEventChkInf(EVENTCHKINF_OBTAINED_ZELDAS_LETTER)
    // * kak gate check: Flags_GetInfTable(INFTABLE_SHOWED_ZELDAS_LETTER_TO_GATE_GUARD)
    // and the mask quest is complete: Flags_GetEventChkInf(EVENTCHKINF_PAID_BACK_BUNNY_HOOD_FEE)
    return (CVarGetInteger(CVAR_ENHANCEMENT("MaskSelect"), 0) ||
            Randomizer_GetSettingValue(RSK_MASK_QUEST) != RO_MASK_QUEST_VANILLA) &&
           Flags_GetEventChkInf(EVENTCHKINF_PAID_BACK_BUNNY_HOOD_FEE) &&
           Flags_GetEventChkInf(EVENTCHKINF_OBTAINED_ZELDAS_LETTER) &&
           Flags_GetInfTable(INFTABLE_SHOWED_ZELDAS_LETTER_TO_GATE_GUARD);
}

// =============================================================================
// Gust Jar Element Cycle (in Kaleido item page)
// =============================================================================

extern void* ExtInv_GetItemIcon(uint16_t itemId);

// ── Lantern Kaleido Fire-Type Selector ───────────────────────────────────────
// Press A on the lantern in kaleido → opens an overlay with all ever-captured
// fire types (tracked persistently in gSaveContext.ship.lanternCapturedTypes)
// plus a "Vacía" / extinguish slot. Stick L/R cycles, press A confirms, B
// cancels. Replaces the old hold-C-to-extinguish shortcut.
#define LANTERN_SELECTOR_MAX 5

static u8  sLanternSelectorActive = 0;
static s8  sLanternSelectorCursor = 0;
static s32 sLanternSelectorStickHeld = 0;
static s32 sLanternAnimTimer = 0; // 0..5 — matches sSlotCycleActiveAnimTimer easing in DrawItemCycleExtras

// Tint colors per LanternFireType — used by the overlay draw to indicate which
// fuel is in each slot without needing dedicated icons.
//   0 NONE: dim gray (extinguished)
//   1 REGULAR: orange
//   2 BLUE: cyan
//   3 POE: magenta
//   4 GREEN: green
static const u8 sLanternTypeTint[5][3] = {
    { 110, 110, 110 },
    { 255, 140,  40 },
    {  60, 180, 255 },
    { 220,  80, 220 },
    {  80, 230, 100 },
};

// Forward declarations — defined below.
// Used by Lantern / Gust Jar / Arrow Wheel press-A handlers.
// leftSize / rightSize: texture native size in pixels (32 for item icons,
// 24 for quest medallion icons). Mod authors pass the actual size of the
// PNG they're displaying so UVs scale correctly inside the 32x32 quad.
static void KaleidoCycle_DrawRocStyle(PlayState* play, s32 visualSlot, u8 isCycling,
                                       u8 hasLeftItem, u8 hasRightItem,
                                       void* leftIconTex, void* rightIconTex,
                                       const u8* leftTint, const u8* rightTint,
                                       s32 leftSize, s32 rightSize);
static void ArrowWheel_Build(void);

static u8 Lantern_BuildSelectorEntries(u8 entries[LANTERN_SELECTOR_MAX]) {
    u8 count = 0;
    // "Vacía" / extinguish is always selectable
    entries[count++] = LANTERN_FIRE_NONE;
    for (u8 t = LANTERN_FIRE_REGULAR; t <= LANTERN_FIRE_GREEN; t++) {
        if (gSaveContext.ship.lanternCapturedTypes & (1 << t)) {
            entries[count++] = t;
        }
    }
    return count;
}

static void Lantern_HandleKaleidoSelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    Input* input = &play->state.input[0];

    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_LANTERN) {
        if (sLanternSelectorActive) {
            sLanternSelectorActive = 0;
            gCurrentItemCyclingSlot = -1;
        }
        return;
    }

    u8 entries[LANTERN_SELECTOR_MAX];
    u8 count = Lantern_BuildSelectorEntries(entries);

    // Roc's Feather-style cycle: A toggles cycle mode; stick L/R applies the
    // change immediately and updates the visible prev/next mini icons. Single
    // press of A confirms exit (no separate confirm cursor).
    bool dpad = (CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0) && !CHECK_BTN_ALL(input->cur.button, BTN_CUP));

    if (CHECK_BTN_ALL(input->press.button, BTN_A) && count > 1) {
        sLanternSelectorActive = !sLanternSelectorActive;
        if (sLanternSelectorActive) {
            gCurrentItemCyclingSlot = pauseCtx->cursorSlot[PAUSE_ITEM];
        } else {
            gCurrentItemCyclingSlot = -1;
        }
        Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    if (sLanternSelectorActive) {
        pauseCtx->cursorColorSet = 8;
        if ((pauseCtx->stickRelX > 30 || pauseCtx->stickRelY > 30) ||
            (dpad && CHECK_BTN_ANY(input->press.button, BTN_DRIGHT | BTN_DUP))) {
            // Cycle to next captured fire type.
            for (u8 i = 0; i < count; i++) {
                if (entries[i] == gCustomItemState.lanternFireType) {
                    gCustomItemState.lanternFireType = entries[(i + 1) % count];
                    gSaveContext.ship.lanternFireType = gCustomItemState.lanternFireType;
                    Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    break;
                }
            }
        } else if ((pauseCtx->stickRelX < -30 || pauseCtx->stickRelY < -30) ||
                   (dpad && CHECK_BTN_ANY(input->press.button, BTN_DLEFT | BTN_DDOWN))) {
            for (u8 i = 0; i < count; i++) {
                if (entries[i] == gCustomItemState.lanternFireType) {
                    gCustomItemState.lanternFireType = entries[(i + count - 1) % count];
                    gSaveContext.ship.lanternFireType = gCustomItemState.lanternFireType;
                    Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    break;
                }
            }
        }
        // Keep cycling slot in sync with cursor position (gets reset to -1 if
        // user moves cursor off the lantern).
        gCurrentItemCyclingSlot = pauseCtx->cursorSlot[PAUSE_ITEM];
    }

    // Legacy path (kept disabled — unreachable, A press above always returns).
    if (0 && CHECK_BTN_ALL(input->press.button, BTN_A)) {
        if (!sLanternSelectorActive) {
            // Open — cursor starts at current fire type if present in the list.
            sLanternSelectorActive = 1;
            sLanternSelectorCursor = 0;
            for (u8 i = 0; i < count; i++) {
                if (entries[i] == gCustomItemState.lanternFireType) {
                    sLanternSelectorCursor = i;
                    break;
                }
            }
            sLanternSelectorStickHeld = 0;
            // Use the existing kaleido cycling lock so other A handlers don't
            // also fire (equip-to-button, exit pause, etc.) while the overlay
            // is open. SLOT_LANTERN is the lantern's inventory slot.
            gCurrentItemCyclingSlot = SLOT_LANTERN;
            Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        } else {
            // Confirm — apply selection and close.
            u8 newType = entries[sLanternSelectorCursor];
            if (newType != gCustomItemState.lanternFireType) {
                gCustomItemState.lanternFireType = newType;
                gSaveContext.ship.lanternFireType = newType;
                Audio_PlaySoundGeneral(newType == LANTERN_FIRE_NONE ? NA_SE_EV_FIRE_PILLAR : NA_SE_EV_FLAME_IGNITION,
                                       &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultReverb);
            } else {
                Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
            sLanternSelectorActive = 0;
            gCurrentItemCyclingSlot = -1;
        }
        return;
    }

    if (!sLanternSelectorActive) {
        return;
    }

    // B cancels without applying.
    if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
        sLanternSelectorActive = 0;
        gCurrentItemCyclingSlot = -1;
        Audio_PlaySoundGeneral(NA_SE_SY_CANCEL, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    // Stick L/R cycles selection (with debounce so a held stick doesn't spam).
    s32 stickX = input->rel.stick_x;
    if (stickX > 30 && !sLanternSelectorStickHeld) {
        sLanternSelectorCursor = (sLanternSelectorCursor + 1) % count;
        sLanternSelectorStickHeld = 1;
        Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    } else if (stickX < -30 && !sLanternSelectorStickHeld) {
        sLanternSelectorCursor = (sLanternSelectorCursor + count - 1) % count;
        sLanternSelectorStickHeld = 1;
        Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    } else if (stickX > -20 && stickX < 20) {
        sLanternSelectorStickHeld = 0;
    }
}

// Draw the selector overlay around the lantern icon — shows each available
// fire type tinted by its color, with the cursor highlighted.
// New Lantern draw — uses the shared Roc's Feather helper. Shows prev/next
// captured fire types as left/right mini lantern icons tinted by their flame
// color, with the A-button hint when idle and dark circles when cycling.
static void Lantern_DrawKaleidoSelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_LANTERN) {
        // Helper handles timer decay when not on this slot.
        KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], 0, 0, 0, NULL, NULL, NULL, NULL, 0, 0);
        return;
    }

    u8 entries[LANTERN_SELECTOR_MAX];
    u8 count = Lantern_BuildSelectorEntries(entries);
    if (count <= 1) {
        return;
    }

    void* lanternTex = ExtInv_GetItemIcon(ITEM_LANTERN);
    if (lanternTex == NULL) {
        return;
    }

    // Find prev/next fire types relative to the current selection.
    u8 cur = gCustomItemState.lanternFireType;
    u8 prevType = cur, nextType = cur;
    for (u8 i = 0; i < count; i++) {
        if (entries[i] == cur) {
            prevType = entries[(i + count - 1) % count];
            nextType = entries[(i + 1) % count];
            break;
        }
    }

    // Lantern icon is 32x32.
    KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], sLanternSelectorActive,
                              /*hasLeftItem=*/1, /*hasRightItem=*/1,
                              lanternTex, lanternTex,
                              sLanternTypeTint[prevType], sLanternTypeTint[nextType],
                              /*leftSize=*/32, /*rightSize=*/32);
}

static void Lantern_DrawKaleidoSelector_OLD(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_LANTERN) {
        if (sLanternAnimTimer > 0) {
            sLanternAnimTimer--;
        }
        return;
    }

    // Animate cycle transition (same easing as mask/Nayru cycle so the lantern
    // selector visually matches the Roc's Feather pattern).
    if (sLanternSelectorActive) {
        if (sLanternAnimTimer < 5) {
            sLanternAnimTimer++;
        }
    } else {
        if (sLanternAnimTimer > 0) {
            sLanternAnimTimer--;
        }
    }

    u8 entries[LANTERN_SELECTOR_MAX];
    u8 count = Lantern_BuildSelectorEntries(entries);
    void* lanternTex = ExtInv_GetItemIcon(ITEM_LANTERN);
    if (lanternTex == NULL || count == 0) {
        return;
    }

    s32 cursorSlot = pauseCtx->cursorSlot[PAUSE_ITEM];
    s32 vtxIdx = cursorSlot * 4;

    OPEN_DISPS(play->state.gfxCtx);

    Matrix_Push();

    Vtx* itemTopLeft = &pauseCtx->itemVtx[vtxIdx];
    Vtx* itemBottomRight = &itemTopLeft[3];
    s16 halfX = (itemBottomRight->v.ob[0] - itemTopLeft->v.ob[0]) / 2;
    s16 halfY = (itemBottomRight->v.ob[1] - itemTopLeft->v.ob[1]) / 2;
    Matrix_Translate(itemTopLeft->v.ob[0] + halfX, itemTopLeft->v.ob[1] + halfY, 0, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    // ── A-button indicator (only when idle, signaling "press A to open") ──
    // Mirrors the Roc's Feather / Nayru / mask hint at z_kaleido_item.c:212.
    if (!sLanternSelectorActive && sLanternAnimTimer == 0 && pauseCtx->cursorSpecialPos == 0) {
        Color_RGB8 aButtonColor = { 0, 100, 255 };
        if (CVarGetInteger(CVAR_COSMETIC("HUD.AButton.Changed"), 0)) {
            aButtonColor = CVarGetColor24(CVAR_COSMETIC("HUD.AButton.Value"), aButtonColor);
        } else if (CVarGetInteger(CVAR_COSMETIC("DefaultColorScheme"), COLORSCHEME_N64) == COLORSCHEME_GAMECUBE) {
            aButtonColor = (Color_RGB8){ 0, 255, 100 };
        }
        gSPVertex(POLY_OPA_DISP++, sCycleAButtonVtx, 4, 0);
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, aButtonColor.r, aButtonColor.g, aButtonColor.b, pauseCtx->alpha);
        gDPLoadTextureBlock(POLY_OPA_DISP++, gABtnSymbolTex, G_IM_FMT_IA, G_IM_SIZ_8b, 24, 16, 0,
                            G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 4, 4, G_TX_NOLOD, G_TX_NOLOD);
        gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);
    }

    // ── Fire-type row (only when selector active) ──
    // Drawn above the lantern icon, each slot tinted by its color so the
    // player sees which captured types are available + which is selected.
    if (sLanternSelectorActive) {
        static const s16 sSlotSpacing = 22;
        s16 totalW = (count - 1) * sSlotSpacing;
        s16 startX = -(totalW / 2);
        s16 baseY = -32;

        for (u8 i = 0; i < count; i++) {
            u8 type = entries[i];
            const u8* tint = sLanternTypeTint[type];
            u8 alpha = (i == sLanternSelectorCursor) ? pauseCtx->alpha : (pauseCtx->alpha >> 1);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, tint[0], tint[1], tint[2], alpha);

            Vtx* slotVtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, 4 * sizeof(Vtx));
            for (s32 vi = 0; vi < 4; vi++) {
                slotVtx[vi] = pauseCtx->itemVtx[vtxIdx + vi];
                // Reposition relative to the (already-translated) cursor origin.
                slotVtx[vi].v.ob[0] = slotVtx[vi].v.ob[0] - itemTopLeft->v.ob[0] - halfX + startX + i * sSlotSpacing;
                slotVtx[vi].v.ob[1] = slotVtx[vi].v.ob[1] - itemTopLeft->v.ob[1] - halfY + baseY;
            }
            gSPVertex(POLY_OPA_DISP++, slotVtx, 4, 0);
            KaleidoScope_DrawQuadTextureRGBA32(play->state.gfxCtx, lanternTex, 32, 32, 0);
        }
    }

    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx);
}

// ── Roc's Feather-style cycle visual helper ─────────────────────────────────
// Renders the same UI as KaleidoScope_DrawItemCycleExtras (mask/Nayru/Roc's
// Feather pattern): A-button hint when idle, prev/next mini icons that
// expand to full size with a dark circle when cycling. Reusable across items
// that have their own state (lantern fire type, gust jar element, etc.)
// without needing to swap the inventory slot itself.
//
// Arguments:
//   visualSlot   - cursor visual position (pauseCtx->cursorSlot[PAUSE_ITEM])
//   isCycling    - whether the selector is in active cycle mode
//   hasLeftItem  - draw the left alternative
//   hasRightItem - draw the right alternative
//   leftIconTex  - RGBA32 texture for the left mini icon
//   rightIconTex - RGBA32 texture for the right mini icon
//   leftTint     - 3-byte RGB tint or NULL for white
//   rightTint    - 3-byte RGB tint or NULL for white
static void KaleidoCycle_DrawRocStyle(PlayState* play, s32 visualSlot, u8 isCycling,
                                       u8 hasLeftItem, u8 hasRightItem,
                                       void* leftIconTex, void* rightIconTex,
                                       const u8* leftTint, const u8* rightTint,
                                       s32 leftSize, s32 rightSize) {
    if (visualSlot < 0 || visualSlot >= (s32)ARRAY_COUNT(sSlotCycleActiveAnimTimer)) return;
    if (leftSize <= 0) leftSize = 32;
    if (rightSize <= 0) rightSize = 32;
    PauseContext* pauseCtx = &play->pauseCtx;

    OPEN_DISPS(play->state.gfxCtx);

    if (isCycling) {
        if (sSlotCycleActiveAnimTimer[visualSlot] < 5) sSlotCycleActiveAnimTimer[visualSlot]++;
    } else {
        if (sSlotCycleActiveAnimTimer[visualSlot] > 0) sSlotCycleActiveAnimTimer[visualSlot]--;
    }

    if (hasLeftItem || hasRightItem) {
        Matrix_Push();
        Vtx* itemTopLeft = &pauseCtx->itemVtx[visualSlot * 4];
        Vtx* itemBottomRight = &itemTopLeft[3];
        s16 halfX = (itemBottomRight->v.ob[0] - itemTopLeft->v.ob[0]) / 2;
        s16 halfY = (itemBottomRight->v.ob[1] - itemTopLeft->v.ob[1]) / 2;
        Matrix_Translate(itemTopLeft->v.ob[0] + halfX, itemTopLeft->v.ob[1] + halfY, 0, MTXMODE_APPLY);

        f32 animScale = (f32)(5 - sSlotCycleActiveAnimTimer[visualSlot]) / 5;
        if (!isCycling || sSlotCycleActiveAnimTimer[visualSlot] < 5) {
            f32 finalScale = 1.0f - (0.675f * animScale);
            Matrix_Translate(0, -15.0f * animScale, 0, MTXMODE_APPLY);
            Matrix_Scale(finalScale, finalScale, 1.0f, MTXMODE_APPLY);
        }
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

        // A-button hint when idle
        if (!isCycling && sSlotCycleActiveAnimTimer[visualSlot] == 0 && pauseCtx->cursorSpecialPos == 0) {
            Color_RGB8 aButtonColor = { 0, 100, 255 };
            if (CVarGetInteger(CVAR_COSMETIC("HUD.AButton.Changed"), 0)) {
                aButtonColor = CVarGetColor24(CVAR_COSMETIC("HUD.AButton.Value"), aButtonColor);
            } else if (CVarGetInteger(CVAR_COSMETIC("DefaultColorScheme"), COLORSCHEME_N64) == COLORSCHEME_GAMECUBE) {
                aButtonColor = (Color_RGB8){ 0, 255, 100 };
            }
            gSPVertex(POLY_OPA_DISP++, sCycleAButtonVtx, 4, 0);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, aButtonColor.r, aButtonColor.g, aButtonColor.b, pauseCtx->alpha);
            gDPLoadTextureBlock(POLY_OPA_DISP++, gABtnSymbolTex, G_IM_FMT_IA, G_IM_SIZ_8b, 24, 16, 0,
                                G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 4, 4, G_TX_NOLOD, G_TX_NOLOD);
            gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);
        }

        // Dark circles behind icons when cycling
        if (isCycling) {
            gSPVertex(POLY_OPA_DISP++, sCycleCircleVtx, 8, 0);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 0, 0, 0, (u8)(pauseCtx->alpha * (1.0f - animScale)));
            gDPLoadTextureBlock_4b(POLY_OPA_DISP++, gPausePromptCursorTex, G_IM_FMT_I, 48, 48, 0,
                                   G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                   G_TX_NOLOD, G_TX_NOLOD);
            if (hasLeftItem) gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);
            if (hasRightItem) gSP1Quadrangle(POLY_OPA_DISP++, 4, 6, 7, 5, 0);
        }

        // Left + right icons.
        // KaleidoScope_DrawQuadTextureRGBA32(gfxCtx, tex, w, h, vtxOff) renders
        // the texture at its native (w x h) into the quad starting at vtxOff
        // within sCycleExtraItemVtx (0 = left slot, 4 = right slot).
        gSPVertex(POLY_OPA_DISP++, sCycleExtraItemVtx, 8, 0);
        if (hasLeftItem && leftIconTex != NULL) {
            u8 r = leftTint ? leftTint[0] : 255;
            u8 g = leftTint ? leftTint[1] : 255;
            u8 b = leftTint ? leftTint[2] : 255;
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, r, g, b, pauseCtx->alpha);
            KaleidoScope_DrawQuadTextureRGBA32(play->state.gfxCtx, leftIconTex, leftSize, leftSize, 0);
        }
        if (hasRightItem && rightIconTex != NULL) {
            u8 r = rightTint ? rightTint[0] : 255;
            u8 g = rightTint ? rightTint[1] : 255;
            u8 b = rightTint ? rightTint[2] : 255;
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, r, g, b, pauseCtx->alpha);
            KaleidoScope_DrawQuadTextureRGBA32(play->state.gfxCtx, rightIconTex, rightSize, rightSize, 4);
        }

        Matrix_Pop();
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

// ── Twilight Upgrade Mode Selectors ─────────────────────────────────────────
// When the player owns the Twilight Upgrade, pressing A on Hookshot / Longshot
// (Clawshot toggle) or Boomerang (Gale Boomerang toggle) opens a 2-slot kaleido
// overlay that picks between vanilla mode and the upgraded mode. Mirrors the
// Lantern selector pattern exactly — same A-press flow, stick L/R navigation,
// A confirms / B cancels, animated A-button hint when idle.

#define TWILIGHT_TOGGLE_VANILLA 0
#define TWILIGHT_TOGGLE_UPGRADED 1
#define TWILIGHT_TOGGLE_SLOTS 2

// Clawshot selector state
static u8  sClawshotSelectorActive = 0;
static s8  sClawshotSelectorCursor = 0;
static s32 sClawshotSelectorStickHeld = 0;
static s32 sClawshotAnimTimer = 0;
// Gale Boomerang selector state
static u8  sGaleSelectorActive = 0;
static s8  sGaleSelectorCursor = 0;
static s32 sGaleSelectorStickHeld = 0;
static s32 sGaleAnimTimer = 0;

// Helper: returns 1 if the cursor item is a hookshot/longshot.
static u8 TwilightSel_IsHookshotItem(s32 item) {
    return item == ITEM_HOOKSHOT || item == ITEM_LONGSHOT;
}

// ── Clawshot Mode Selector (Hookshot/Longshot icon) ─────────────────────────
// 1:1 with the canonical ArrowWheel / Lantern / GustJar pattern:
//   - A press → toggle the selector ON/OFF (NA_SE_SY_DECIDE)
//   - While active: stick L/R (or D-pad L/R/Up/Down) advances cursor through
//     the two options (vanilla ↔ clawshot), each move plays NA_SE_SY_CURSOR
//     and immediately applies the new mode
//   - While active: pauseCtx->cursorColorSet = 8 (yellow cursor)
//   - A press again → confirm + exit selector
// pauseCtx->stickRelX/Y is already debounced by the kaleido (resets when the
// stick returns to center), so no manual debounce needed.
static void Clawshot_HandleKaleidoSelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    Input* input = &play->state.input[0];

    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (!TwilightSel_IsHookshotItem(cursorItem) || !TwilightUpgrade_HasClawshot()) {
        if (sClawshotSelectorActive) {
            sClawshotSelectorActive = 0;
            gCurrentItemCyclingSlot = -1;
        }
        return;
    }

    bool dpad = (CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0) && !CHECK_BTN_ALL(input->cur.button, BTN_CUP));

    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        sClawshotSelectorActive = !sClawshotSelectorActive;
        gCurrentItemCyclingSlot = sClawshotSelectorActive ? pauseCtx->cursorSlot[PAUSE_ITEM] : -1;
        Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    if (!sClawshotSelectorActive) return;
    pauseCtx->cursorColorSet = 8;
    gCurrentItemCyclingSlot = pauseCtx->cursorSlot[PAUSE_ITEM];

    // Two-position cursor: 0 = vanilla hookshot, 1 = clawshot. Stick/D-pad
    // flips between them; each flip immediately applies the new mode.
    s8 newMode = -1;
    if ((pauseCtx->stickRelX > 30 || pauseCtx->stickRelY > 30) ||
        (dpad && CHECK_BTN_ANY(input->press.button, BTN_DRIGHT | BTN_DUP))) {
        newMode = TwilightUpgrade_IsClawshotActive() ? 0 : 1;
    } else if ((pauseCtx->stickRelX < -30 || pauseCtx->stickRelY < -30) ||
               (dpad && CHECK_BTN_ANY(input->press.button, BTN_DLEFT | BTN_DDOWN))) {
        newMode = TwilightUpgrade_IsClawshotActive() ? 0 : 1;
    }

    if (newMode >= 0) {
        TwilightUpgrade_SetClawshotActive(newMode);
        Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
}

// Draws the small left/right flip — vanilla hookshot/longshot vs clawshot —
// via the shared KaleidoCycle_DrawRocStyle helper (same renderer Lantern,
// GustJar press-A, and arrows use). Left side: vanilla hookshot/longshot
// icon (taken directly from gItemIcons to bypass ExtInv's clawshot
// override). Right side: dedicated clawshot icon texture.
static void Clawshot_DrawKaleidoSelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (!TwilightSel_IsHookshotItem(cursorItem) || !TwilightUpgrade_HasClawshot()) {
        // Reset animation when leaving the hookshot/longshot cursor.
        if (sClawshotAnimTimer > 0) sClawshotAnimTimer--;
        return;
    }

    // Pull the vanilla icon directly so the left/prev side shows hookshot/longshot
    // even when ExtInv_GetItemIcon would swap to the clawshot placeholder.
    extern void* gItemIcons[];
    extern void* MmAssets_LoadHookshotIcon(void);
    void* vanillaTex = gItemIcons[cursorItem];
    // Prefer MM hookshot icon from mm.o2r (matches the in-inventory grid swap).
    // Falls back to the local placeholder PNG when mm.o2r isn't loaded.
    void* clawshotTex = MmAssets_LoadHookshotIcon();
    if (clawshotTex == NULL)
        clawshotTex = (void*)gItemIconClawshotTex;
    if (vanillaTex == NULL || clawshotTex == NULL) return;

    static const u8 sVanillaTint[3]  = { 255, 255, 255 };
    static const u8 sClawshotTint[3] = { 255, 255, 255 };

    KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], sClawshotSelectorActive,
                              /*hasLeft=*/1, /*hasRight=*/1,
                              vanillaTex, clawshotTex,
                              sVanillaTint, sClawshotTint,
                              /*leftSize=*/32, /*rightSize=*/32);
}

// ── Gale Boomerang Mode Selector (Boomerang icon) ───────────────────────────
static void Gale_HandleKaleidoSelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    Input* input = &play->state.input[0];

    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_BOOMERANG || !TwilightUpgrade_HasGaleBoomerang()) {
        if (sGaleSelectorActive) {
            sGaleSelectorActive = 0;
            gCurrentItemCyclingSlot = -1;
        }
        return;
    }

    // Same canonical ArrowWheel pattern as Clawshot above.
    bool dpad = (CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0) && !CHECK_BTN_ALL(input->cur.button, BTN_CUP));

    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        sGaleSelectorActive = !sGaleSelectorActive;
        gCurrentItemCyclingSlot = sGaleSelectorActive ? pauseCtx->cursorSlot[PAUSE_ITEM] : -1;
        Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    if (!sGaleSelectorActive) return;
    pauseCtx->cursorColorSet = 8;
    gCurrentItemCyclingSlot = pauseCtx->cursorSlot[PAUSE_ITEM];

    s8 newMode = -1;
    if ((pauseCtx->stickRelX > 30 || pauseCtx->stickRelY > 30) ||
        (dpad && CHECK_BTN_ANY(input->press.button, BTN_DRIGHT | BTN_DUP))) {
        newMode = TwilightUpgrade_IsGaleBoomerangActive() ? 0 : 1;
    } else if ((pauseCtx->stickRelX < -30 || pauseCtx->stickRelY < -30) ||
               (dpad && CHECK_BTN_ANY(input->press.button, BTN_DLEFT | BTN_DDOWN))) {
        newMode = TwilightUpgrade_IsGaleBoomerangActive() ? 0 : 1;
    }

    if (newMode >= 0) {
        TwilightUpgrade_SetGaleBoomerangActive(newMode);
        Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
}

// Same small-flip pattern as Clawshot above — left side shows the vanilla
// boomerang icon (taken directly from gItemIcons to bypass ExtInv's gale
// override), right side shows the gale boomerang icon texture.
static void Gale_DrawKaleidoSelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_BOOMERANG || !TwilightUpgrade_HasGaleBoomerang()) {
        if (sGaleAnimTimer > 0) sGaleAnimTimer--;
        return;
    }

    extern void* gItemIcons[];
    void* vanillaTex = gItemIcons[ITEM_BOOMERANG];
    void* galeTex = (void*)gItemIconGaleBoomerangTex;
    if (vanillaTex == NULL || galeTex == NULL) return;

    static const u8 sVanillaTint[3] = { 255, 255, 255 };
    static const u8 sGaleTint[3]    = { 255, 255, 255 };

    KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], sGaleSelectorActive,
                              /*hasLeft=*/1, /*hasRight=*/1,
                              vanillaTex, galeTex,
                              sVanillaTint, sGaleTint,
                              /*leftSize=*/32, /*rightSize=*/32);
}

// ── Gust Jar Kaleido Element Cycle ──────────────────────────────────────────
// Available elements based on owned medallions
static u8 sGustAvailElems[6];
static u8 sGustAvailCount = 0;
static u8 sGustElemCursor = 0;

static void GustJar_BuildKaleidoElements(void) {
    sGustAvailCount = 0;
    // Wind always available
    sGustAvailElems[sGustAvailCount++] = 0; // GUST_ELEMENT_WIND
    // Medallion order: Forest(Wind already), Fire, Water, Shadow, Spirit, Light
    static const s32 questItems[] = { QUEST_MEDALLION_FIRE, QUEST_MEDALLION_WATER, QUEST_MEDALLION_SHADOW,
                                      QUEST_MEDALLION_SPIRIT, QUEST_MEDALLION_LIGHT };
    static const u8 elements[] = { 1, 2, 3, 4, 5 }; // Fire, Ice, Shadow, Spirit, Light
    for (s32 i = 0; i < 5; i++) {
        if (CHECK_QUEST_ITEM(questItems[i])) {
            sGustAvailElems[sGustAvailCount++] = elements[i];
        }
    }
    // Find current cursor
    sGustElemCursor = 0;
    for (u8 i = 0; i < sGustAvailCount; i++) {
        if (sGustAvailElems[i] == gCustomItemState.gustJarElement) {
            sGustElemCursor = i;
            break;
        }
    }
}

static u8 sGustOverlayActive = 0;
static s16 sGustHoldTimer = 0;
#define GUST_KALEIDO_HOLD_FRAMES 20 // Hold C for 20 frames before overlay appears

// Press-A element selector (Roc's Feather style — mirrors lantern selector).
// Coexists with the hold-C wheel below; either trigger cycles elements.
static u8 sGustPressASelectorActive = 0;
static const u16 sGustElemToMedallion[6] = {
    ITEM_MEDALLION_FOREST, // WIND
    ITEM_MEDALLION_FIRE,   // FIRE
    ITEM_MEDALLION_WATER,  // ICE
    ITEM_MEDALLION_SHADOW, // SHADOW
    ITEM_MEDALLION_SPIRIT, // SPIRIT
    ITEM_MEDALLION_LIGHT,  // LIGHT
};

static void GustJar_HandlePressASelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    Input* input = &play->state.input[0];

    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_GUST_JAR) {
        if (sGustPressASelectorActive) {
            sGustPressASelectorActive = 0;
            gCurrentItemCyclingSlot = -1;
        }
        return;
    }

    GustJar_BuildKaleidoElements();
    if (sGustAvailCount <= 1) {
        return; // nothing to cycle
    }

    bool dpad = (CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0) && !CHECK_BTN_ALL(input->cur.button, BTN_CUP));

    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        sGustPressASelectorActive = !sGustPressASelectorActive;
        gCurrentItemCyclingSlot = sGustPressASelectorActive ? pauseCtx->cursorSlot[PAUSE_ITEM] : -1;
        Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    if (!sGustPressASelectorActive) return;
    pauseCtx->cursorColorSet = 8;
    gCurrentItemCyclingSlot = pauseCtx->cursorSlot[PAUSE_ITEM];

    if ((pauseCtx->stickRelX > 30 || pauseCtx->stickRelY > 30) ||
        (dpad && CHECK_BTN_ANY(input->press.button, BTN_DRIGHT | BTN_DUP))) {
        sGustElemCursor = (sGustElemCursor + 1) % sGustAvailCount;
        gCustomItemState.gustJarElement = sGustAvailElems[sGustElemCursor];
        Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    } else if ((pauseCtx->stickRelX < -30 || pauseCtx->stickRelY < -30) ||
               (dpad && CHECK_BTN_ANY(input->press.button, BTN_DLEFT | BTN_DDOWN))) {
        sGustElemCursor = (sGustElemCursor + sGustAvailCount - 1) % sGustAvailCount;
        gCustomItemState.gustJarElement = sGustAvailElems[sGustElemCursor];
        Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
}

static void GustJar_DrawPressASelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_GUST_JAR) {
        KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], 0, 0, 0, NULL, NULL, NULL, NULL, 0, 0);
        return;
    }

    GustJar_BuildKaleidoElements();
    if (sGustAvailCount <= 1) return;

    // Prev/next medallion icons relative to the current element.
    u8 cur = gCustomItemState.gustJarElement;
    u8 prevElem = cur, nextElem = cur;
    for (u8 i = 0; i < sGustAvailCount; i++) {
        if (sGustAvailElems[i] == cur) {
            prevElem = sGustAvailElems[(i + sGustAvailCount - 1) % sGustAvailCount];
            nextElem = sGustAvailElems[(i + 1) % sGustAvailCount];
            break;
        }
    }

    void* leftTex = ExtInv_GetItemIcon(sGustElemToMedallion[prevElem]);
    void* rightTex = ExtInv_GetItemIcon(sGustElemToMedallion[nextElem]);

    // Quest medallion icons are 24x24 (z_kaleido_collect.c:450).
    KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], sGustPressASelectorActive,
                              leftTex != NULL, rightTex != NULL,
                              leftTex, rightTex, NULL, NULL,
                              /*leftSize=*/24, /*rightSize=*/24);
}

static void GustJar_HandleElementCycle(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    Input* input = &play->state.input[0];

    // Check if cursor is on Gust Jar
    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_GUST_JAR) {
        sGustOverlayActive = 0;
        sGustHoldTimer = 0;
        return;
    }

    GustJar_BuildKaleidoElements();
    if (sGustAvailCount <= 1) {
        sGustOverlayActive = 0;
        sGustHoldTimer = 0;
        return;
    }

    // Hold any C-button — count frames, only activate overlay after threshold
    u8 cHeld = CHECK_BTN_ANY(input->cur.button, BTN_CLEFT | BTN_CDOWN | BTN_CRIGHT);
    if (cHeld) {
        sGustHoldTimer++;

        // Only activate overlay after holding for GUST_KALEIDO_HOLD_FRAMES
        if (sGustHoldTimer >= GUST_KALEIDO_HOLD_FRAMES) {
            if (!sGustOverlayActive) {
                // First frame of overlay — play sound
                Audio_PlaySoundGeneral(NA_SE_SY_CAMERA_ZOOM_UP, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
            sGustOverlayActive = 1;

            // Stick left/right to cycle through elements while overlay is shown
            s32 stickX = input->rel.stick_x;
            static s32 sGustStickHeld = 0;

            if (stickX > 30 && !sGustStickHeld) {
                sGustElemCursor = (sGustElemCursor + 1) % sGustAvailCount;
                gCustomItemState.gustJarElement = sGustAvailElems[sGustElemCursor];
                sGustStickHeld = 1;
                Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            } else if (stickX < -30 && !sGustStickHeld) {
                sGustElemCursor = (sGustElemCursor + sGustAvailCount - 1) % sGustAvailCount;
                gCustomItemState.gustJarElement = sGustAvailElems[sGustElemCursor];
                sGustStickHeld = 1;
                Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            } else if (stickX > -20 && stickX < 20) {
                sGustStickHeld = 0;
            }
        }
        // During the first 20 frames of hold, do nothing — equip proceeds normally
    } else {
        if (sGustOverlayActive) {
            // Released C-button → confirm selection
            Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
        sGustOverlayActive = 0;
        sGustHoldTimer = 0;
    }
}

static void GustJar_DrawElementCycle(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_GUST_JAR)
        return;

    GustJar_BuildKaleidoElements();
    if (sGustAvailCount <= 1)
        return;

    u8 curElem = sGustAvailElems[sGustElemCursor];

    static const u16 elemToMedallion[] = { ITEM_MEDALLION_FOREST, ITEM_MEDALLION_FIRE,   ITEM_MEDALLION_WATER,
                                           ITEM_MEDALLION_SHADOW, ITEM_MEDALLION_SPIRIT, ITEM_MEDALLION_LIGHT };

    // Get cursor slot vertex for positioning
    s32 cursorSlot = pauseCtx->cursorSlot[PAUSE_ITEM];
    s32 vtxIdx = cursorSlot * 4;

    OPEN_DISPS(play->state.gfxCtx);

    // Always draw selected medallion at half-alpha behind the Gust Jar icon
    // (same pattern as SW97 elemental arrows in z_kaleido_collect.c:440-465)
    if (curElem != 0) { // Not wind (default)
        void* medallionTex = ExtInv_GetItemIcon(elemToMedallion[curElem]);
        if (medallionTex != NULL) {
            // Medallion at 50% alpha (behind)
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha >> 1);
            gSPVertex(POLY_OPA_DISP++, &pauseCtx->itemVtx[vtxIdx], 4, 0);
            KaleidoScope_DrawQuadTextureRGBA32(play->state.gfxCtx, medallionTex, 24, 24, 0);

            // Gust Jar icon at full alpha (on top)
            void* gustTex = ExtInv_GetItemIcon(ITEM_GUST_JAR);
            if (gustTex != NULL) {
                // Remap texture coords to 32x32 for item icon overlay (75% scale like SW97)
                Vtx* overlayVtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, 4 * sizeof(Vtx));
                for (s32 vi = 0; vi < 4; vi++) {
                    overlayVtx[vi] = pauseCtx->itemVtx[vtxIdx + vi];
                }
                overlayVtx[0].v.tc[0] = 0;
                overlayVtx[0].v.tc[1] = 0;
                overlayVtx[1].v.tc[0] = 32 << 5;
                overlayVtx[1].v.tc[1] = 0;
                overlayVtx[2].v.tc[0] = 0;
                overlayVtx[2].v.tc[1] = 32 << 5;
                overlayVtx[3].v.tc[0] = 32 << 5;
                overlayVtx[3].v.tc[1] = 32 << 5;

                gDPPipeSync(POLY_OPA_DISP++);
                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
                gSPVertex(POLY_OPA_DISP++, overlayVtx, 4, 0);
                KaleidoScope_DrawQuadTextureRGBA32(play->state.gfxCtx, gustTex, 32, 32, 0);
            }
        }
    }

    // When overlay is active (C-button held): draw all available elements around cursor
    if (sGustOverlayActive) {
        static const s16 offsetX[] = { 0, 20, 20, -20, -20, 0 };
        static const s16 offsetY[] = { -22, -10, 10, -10, 10, 22 };

        for (u8 i = 0; i < sGustAvailCount; i++) {
            u8 elem = sGustAvailElems[i];
            void* tex = ExtInv_GetItemIcon(elemToMedallion[elem]);
            if (tex == NULL)
                continue;

            u8 alpha = (i == sGustElemCursor) ? pauseCtx->alpha : (pauseCtx->alpha >> 1);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, alpha);

            // Create offset vertices for this medallion
            Vtx* elemVtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, 4 * sizeof(Vtx));
            for (s32 vi = 0; vi < 4; vi++) {
                elemVtx[vi] = pauseCtx->itemVtx[vtxIdx + vi];
                elemVtx[vi].v.ob[0] += offsetX[elem];
                elemVtx[vi].v.ob[1] += offsetY[elem];
            }
            gSPVertex(POLY_OPA_DISP++, elemVtx, 4, 0);
            KaleidoScope_DrawQuadTextureRGBA32(play->state.gfxCtx, tex, 24, 24, 0);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

// ── Bow / Slingshot Elemental-Arrow Wheel ───────────────────────────────────
// Hold C on bow/slingshot inventory icon → radial of 6 medallion arrows + bomb.
// Release C to equip the SW97 arrow item to the C-slot that was held.
// Mirrors the GustJar wheel pattern above.

#define ARROW_WHEEL_HOLD_FRAMES 20
#define ARROW_WHEEL_MAX_ENTRIES 7 // 6 medallion arrows + bomb arrows

// Element ids 0-5 map to medallions (Wind=Forest, Fire, Ice=Water, Light, Shadow, Spirit).
// Id 6 = Bomb Arrow.
#define ARROW_WHEEL_ENTRY_BOMB    6
static const s32 sArrowWheelItem[ARROW_WHEEL_MAX_ENTRIES] = {
    ITEM_SW97_ARROW_WIND, ITEM_SW97_ARROW_FIRE,  ITEM_SW97_ARROW_ICE,  ITEM_SW97_ARROW_LIGHT,
    ITEM_SW97_ARROW_DARK, ITEM_SW97_ARROW_SOUL,  ITEM_BOMB_ARROWS,
};
static const s32 sArrowWheelMedallion[6] = {
    ITEM_MEDALLION_FOREST, ITEM_MEDALLION_FIRE,   ITEM_MEDALLION_WATER,
    ITEM_MEDALLION_LIGHT,  ITEM_MEDALLION_SHADOW, ITEM_MEDALLION_SPIRIT,
};
static const s32 sArrowWheelQuest[6] = {
    QUEST_MEDALLION_FOREST, QUEST_MEDALLION_FIRE,   QUEST_MEDALLION_WATER,
    QUEST_MEDALLION_LIGHT,  QUEST_MEDALLION_SHADOW, QUEST_MEDALLION_SPIRIT,
};
// Vanilla elemental-arrow inventory items per element (-1 if no vanilla equivalent)
static const s32 sArrowWheelVanillaArrow[6] = {
    -1, ITEM_ARROW_FIRE, ITEM_ARROW_ICE, ITEM_ARROW_LIGHT, -1, -1,
};
// Heptagonal radial layout (positions 0..6 around the cursor)
static const s16 sArrowWheelOffX[ARROW_WHEEL_MAX_ENTRIES] = {  0,  19,  24,  12, -12, -24, -19 };
static const s16 sArrowWheelOffY[ARROW_WHEEL_MAX_ENTRIES] = { -24, -15,   5,  20,  20,   5, -15 };

static u8  sArrowWheelEntries[ARROW_WHEEL_MAX_ENTRIES];
static u8  sArrowWheelAvailCount = 0;
static u8  sArrowWheelCursor = 0;
static u8  sArrowWheelOverlayActive = 0;
static s16 sArrowWheelHoldTimer = 0;
static s32 sArrowWheelLastCBtn = -1;
static s32 sArrowWheelStickHeld = 0;

// Press-A selector (Roc's Feather style) — alternative to the hold-C wheel.
static u8  sArrowWheelPressAActive = 0;

// Find which C-button currently has the bow/slingshot or a cycled arrow item.
// Returns the cursor index into sArrowWheelEntries (0..N-1) for the current
// item, or -1 if not found. Writes the C-button index (0=C-Left, 1=C-Down,
// 2=C-Right) into *outCBtn, or -1 if no relevant C-button.
static s8 ArrowWheel_GetCurrentEntry(s32* outCBtn) {
    *outCBtn = -1;
    for (s32 i = 1; i <= 3; i++) {
        u8 item = gSaveContext.equips.buttonItems[i];
        s8 entry = -1;
        if (item == ITEM_BOW || item == ITEM_SLINGSHOT) {
            entry = 0; // default to WIND
        } else if (item >= ITEM_SW97_ARROW_FIRE && item <= ITEM_SW97_ARROW_WIND) {
            for (u8 k = 0; k < 6; k++) {
                if ((s32)sArrowWheelItem[k] == (s32)item) {
                    entry = (s8)k;
                    break;
                }
            }
        } else if (item == ITEM_BOMB_ARROWS) {
            entry = ARROW_WHEEL_ENTRY_BOMB;
        }
        if (entry >= 0) {
            *outCBtn = i - 1;
            for (u8 k = 0; k < sArrowWheelAvailCount; k++) {
                if (sArrowWheelEntries[k] == (u8)entry) {
                    return (s8)k;
                }
            }
            return -1;
        }
    }
    return -1;
}

// Apply the selected entry to the given C-button.
static void ArrowWheel_ApplyEntry(PlayState* play, u8 entryIdx, s32 cBtn) {
    if (cBtn < 0 || cBtn > 2 || entryIdx >= sArrowWheelAvailCount) return;
    u8 entry = sArrowWheelEntries[entryIdx];
    s32 chosenItem = sArrowWheelItem[entry];
    s32 targetButtonIndex = cBtn + 1; // buttonItems[0] is B
    gSaveContext.equips.buttonItems[targetButtonIndex] = chosenItem;
    gSaveContext.equips.cButtonSlots[cBtn] = 0xFF; // not-from-inventory marker
    Interface_LoadItemIcon1(play, targetButtonIndex);
}

// Resolve a wheel entry to its display icon texture.
static void* ArrowWheel_GetEntryIcon(u8 entry) {
    if (entry == ARROW_WHEEL_ENTRY_BOMB) {
        return ExtInv_GetItemIcon(ITEM_BOMB_ARROWS);
    } else if (entry < 6) {
        return ExtInv_GetItemIcon(sArrowWheelMedallion[entry]);
    }
    return NULL;
}

static void ArrowWheel_HandlePressA(PlayState* play) {
    if (!SW97_MEDALLIONS_ENABLED()) {
        sArrowWheelPressAActive = 0;
        return;
    }
    PauseContext* pauseCtx = &play->pauseCtx;
    Input* input = &play->state.input[0];

    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_BOW && cursorItem != ITEM_SLINGSHOT) {
        if (sArrowWheelPressAActive) {
            sArrowWheelPressAActive = 0;
            gCurrentItemCyclingSlot = -1;
        }
        return;
    }

    ArrowWheel_Build();
    if (sArrowWheelAvailCount <= 1) return;

    s32 cBtn = -1;
    s8 curIdx = ArrowWheel_GetCurrentEntry(&cBtn);
    if (cBtn < 0) return; // bow/slingshot not on any C-button — nothing to cycle
    if (curIdx < 0) curIdx = 0;

    bool dpad = (CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0) && !CHECK_BTN_ALL(input->cur.button, BTN_CUP));

    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        sArrowWheelPressAActive = !sArrowWheelPressAActive;
        gCurrentItemCyclingSlot = sArrowWheelPressAActive ? pauseCtx->cursorSlot[PAUSE_ITEM] : -1;
        Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    if (!sArrowWheelPressAActive) return;
    pauseCtx->cursorColorSet = 8;
    gCurrentItemCyclingSlot = pauseCtx->cursorSlot[PAUSE_ITEM];

    s8 newIdx = -1;
    if ((pauseCtx->stickRelX > 30 || pauseCtx->stickRelY > 30) ||
        (dpad && CHECK_BTN_ANY(input->press.button, BTN_DRIGHT | BTN_DUP))) {
        newIdx = (curIdx + 1) % sArrowWheelAvailCount;
    } else if ((pauseCtx->stickRelX < -30 || pauseCtx->stickRelY < -30) ||
               (dpad && CHECK_BTN_ANY(input->press.button, BTN_DLEFT | BTN_DDOWN))) {
        newIdx = (curIdx + sArrowWheelAvailCount - 1) % sArrowWheelAvailCount;
    }

    if (newIdx >= 0) {
        ArrowWheel_ApplyEntry(play, newIdx, cBtn);
        Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
}

static void ArrowWheel_DrawPressA(PlayState* play) {
    if (!SW97_MEDALLIONS_ENABLED()) return;
    PauseContext* pauseCtx = &play->pauseCtx;
    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_BOW && cursorItem != ITEM_SLINGSHOT) {
        KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], 0, 0, 0, NULL, NULL, NULL, NULL, 0, 0);
        return;
    }

    ArrowWheel_Build();
    if (sArrowWheelAvailCount <= 1) return;

    s32 cBtn = -1;
    s8 curIdx = ArrowWheel_GetCurrentEntry(&cBtn);
    if (curIdx < 0) curIdx = 0;

    u8 prevEntry = sArrowWheelEntries[(curIdx + sArrowWheelAvailCount - 1) % sArrowWheelAvailCount];
    u8 nextEntry = sArrowWheelEntries[(curIdx + 1) % sArrowWheelAvailCount];

    void* leftTex = ArrowWheel_GetEntryIcon(prevEntry);
    void* rightTex = ArrowWheel_GetEntryIcon(nextEntry);

    // Per-entry native size: medallion icons (entries 0-5) are 24x24, bomb arrows
    // and bombchus (entries 6-7) are 32x32. Mixed prev/next is supported.
    s32 leftSize = (prevEntry < 6) ? 24 : 32;
    s32 rightSize = (nextEntry < 6) ? 24 : 32;

    KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], sArrowWheelPressAActive,
                              leftTex != NULL, rightTex != NULL,
                              leftTex, rightTex, NULL, NULL,
                              leftSize, rightSize);
}

static void ArrowWheel_Build(void) {
    sArrowWheelAvailCount = 0;
    for (s32 i = 0; i < 6; i++) {
        u8 hasMedallion = CHECK_QUEST_ITEM(sArrowWheelQuest[i]);
        u8 hasVanillaArrow = (sArrowWheelVanillaArrow[i] >= 0) &&
                             (INV_CONTENT(sArrowWheelVanillaArrow[i]) != ITEM_NONE);
        if (hasMedallion || hasVanillaArrow) {
            sArrowWheelEntries[sArrowWheelAvailCount++] = (u8)i;
        }
    }
    // ITEM_BOMB_ARROWS is a NEI custom item (0xAE); INV_CONTENT()/SLOT() would index
    // gItemSlots[56] out of bounds. Resolve the real extended-inventory slot instead.
    u8 baSlot = ExtInv_GetItemSlot(ITEM_BOMB_ARROWS);
    if (baSlot != 0xFF && gSaveContext.inventory.items[baSlot] != ITEM_NONE) {
        sArrowWheelEntries[sArrowWheelAvailCount++] = ARROW_WHEEL_ENTRY_BOMB;
    }
    if (sArrowWheelCursor >= sArrowWheelAvailCount) {
        sArrowWheelCursor = 0;
    }
}

static void ArrowWheel_Handle(PlayState* play) {
    if (!SW97_MEDALLIONS_ENABLED()) {
        sArrowWheelOverlayActive = 0;
        sArrowWheelHoldTimer = 0;
        return;
    }

    PauseContext* pauseCtx = &play->pauseCtx;
    Input* input = &play->state.input[0];

    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_BOW && cursorItem != ITEM_SLINGSHOT) {
        sArrowWheelOverlayActive = 0;
        sArrowWheelHoldTimer = 0;
        return;
    }

    ArrowWheel_Build();
    if (sArrowWheelAvailCount == 0) {
        sArrowWheelOverlayActive = 0;
        sArrowWheelHoldTimer = 0;
        return;
    }

    // Detect held C-button (priority CLEFT > CDOWN > CRIGHT)
    u16 btn = input->cur.button;
    s32 cBtn = -1;
    if (btn & BTN_CLEFT) cBtn = 0;
    else if (btn & BTN_CDOWN) cBtn = 1;
    else if (btn & BTN_CRIGHT) cBtn = 2;

    if (cBtn >= 0) {
        sArrowWheelLastCBtn = cBtn;
        sArrowWheelHoldTimer++;

        if (sArrowWheelHoldTimer >= ARROW_WHEEL_HOLD_FRAMES) {
            if (!sArrowWheelOverlayActive) {
                Audio_PlaySoundGeneral(NA_SE_SY_CAMERA_ZOOM_UP, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
            sArrowWheelOverlayActive = 1;

            s32 stickX = input->rel.stick_x;
            if (stickX > 30 && !sArrowWheelStickHeld) {
                sArrowWheelCursor = (sArrowWheelCursor + 1) % sArrowWheelAvailCount;
                sArrowWheelStickHeld = 1;
                Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            } else if (stickX < -30 && !sArrowWheelStickHeld) {
                sArrowWheelCursor = (sArrowWheelCursor + sArrowWheelAvailCount - 1) % sArrowWheelAvailCount;
                sArrowWheelStickHeld = 1;
                Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            } else if (stickX > -20 && stickX < 20) {
                sArrowWheelStickHeld = 0;
            }
        }
    } else {
        if (sArrowWheelOverlayActive && sArrowWheelLastCBtn >= 0 && sArrowWheelAvailCount > 0) {
            // Confirm: equip chosen SW97 arrow item to the recorded C-slot.
            // Overrides whatever the engine just bound (the bow itself) — same
            // marker pattern as z_kaleido_collect.c medallion equip.
            u8 entry = sArrowWheelEntries[sArrowWheelCursor];
            s32 chosenItem = sArrowWheelItem[entry];
            s32 targetCBtn = sArrowWheelLastCBtn;
            s32 targetButtonIndex = targetCBtn + 1; // buttonItems[0] is B
            gSaveContext.equips.buttonItems[targetButtonIndex] = chosenItem;
            gSaveContext.equips.cButtonSlots[targetCBtn] = 0xFF; // not-from-inventory marker
            Interface_LoadItemIcon1(play, targetButtonIndex);
            Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
        sArrowWheelOverlayActive = 0;
        sArrowWheelHoldTimer = 0;
    }
}

static void ArrowWheel_Draw(PlayState* play) {
    if (!SW97_MEDALLIONS_ENABLED()) return;

    PauseContext* pauseCtx = &play->pauseCtx;
    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_BOW && cursorItem != ITEM_SLINGSHOT) return;

    ArrowWheel_Build();
    if (!sArrowWheelOverlayActive || sArrowWheelAvailCount == 0) return;

    s32 cursorSlot = pauseCtx->cursorSlot[PAUSE_ITEM];
    s32 vtxIdx = cursorSlot * 4;

    OPEN_DISPS(play->state.gfxCtx);

    for (u8 i = 0; i < sArrowWheelAvailCount; i++) {
        u8 entry = sArrowWheelEntries[i];
        bool isBomb = (entry == ARROW_WHEEL_ENTRY_BOMB);
        bool is32px = isBomb; // bomb arrows icon is 32x32
        s32 iconItem;
        if (isBomb) {
            iconItem = ITEM_BOMB_ARROWS;
        } else {
            iconItem = sArrowWheelMedallion[entry];
        }
        void* tex = ExtInv_GetItemIcon(iconItem);
        if (tex == NULL) continue;

        u8 alpha = (i == sArrowWheelCursor) ? pauseCtx->alpha : (pauseCtx->alpha >> 1);
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, alpha);

        Vtx* elemVtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, 4 * sizeof(Vtx));
        for (s32 vi = 0; vi < 4; vi++) {
            elemVtx[vi] = pauseCtx->itemVtx[vtxIdx + vi];
            elemVtx[vi].v.ob[0] += sArrowWheelOffX[i];
            elemVtx[vi].v.ob[1] += sArrowWheelOffY[i];
        }

        if (is32px) {
            // 32x32 icons (bomb arrows / bombchu) — remap UVs to fit the slot quad.
            elemVtx[0].v.tc[0] = 0;        elemVtx[0].v.tc[1] = 0;
            elemVtx[1].v.tc[0] = 32 << 5;  elemVtx[1].v.tc[1] = 0;
            elemVtx[2].v.tc[0] = 0;        elemVtx[2].v.tc[1] = 32 << 5;
            elemVtx[3].v.tc[0] = 32 << 5;  elemVtx[3].v.tc[1] = 32 << 5;
            gSPVertex(POLY_OPA_DISP++, elemVtx, 4, 0);
            KaleidoScope_DrawQuadTextureRGBA32(play->state.gfxCtx, tex, 32, 32, 0);
        } else {
            gSPVertex(POLY_OPA_DISP++, elemVtx, 4, 0);
            KaleidoScope_DrawQuadTextureRGBA32(play->state.gfxCtx, tex, 24, 24, 0);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

void KaleidoScope_HandleItemCycles(PlayState* play) {
    // handle the mask select
    KaleidoScope_HandleItemCycleExtras(
        play, SLOT_TRADE_CHILD, CanMaskSelect(),
        IS_RANDO ? Randomizer_GetPrevChildTradeItem()
                 : (INV_CONTENT(ITEM_TRADE_CHILD) <= ITEM_MASK_KEATON || INV_CONTENT(ITEM_TRADE_CHILD) > ITEM_MASK_TRUTH
                        ? ITEM_MASK_TRUTH
                        : INV_CONTENT(ITEM_TRADE_CHILD) - 1),
        IS_RANDO ? Randomizer_GetNextChildTradeItem()
                 : (INV_CONTENT(ITEM_TRADE_CHILD) >= ITEM_MASK_TRUTH || INV_CONTENT(ITEM_TRADE_CHILD) < ITEM_MASK_KEATON
                        ? ITEM_MASK_KEATON
                        : INV_CONTENT(ITEM_TRADE_CHILD) + 1),
        true);

    // the slot age requirement for the child trade slot has to be updated
    // in case it currently holds a mask
    // to allow adult link to wear it if the setting is enabled
    gSlotAgeReqs[SLOT_TRADE_CHILD] =
        (CVarGetInteger(CVAR_ENHANCEMENT("AdultMasks"), 0) || CVarGetInteger(CVAR_CHEAT("TimelessEquipment"), 0)) &&
                INV_CONTENT(ITEM_TRADE_CHILD) >= ITEM_MASK_KEATON && INV_CONTENT(ITEM_TRADE_CHILD) <= ITEM_MASK_TRUTH
            ? AGE_REQ_NONE
            : AGE_REQ_CHILD;

    // also update the age requirements for the masks itself
    for (int i = ITEM_MASK_KEATON; i <= ITEM_MASK_TRUTH; i += 1) {
        gItemAgeReqs[i] =
            CVarGetInteger(CVAR_ENHANCEMENT("AdultMasks"), 0) || CVarGetInteger(CVAR_CHEAT("TimelessEquipment"), 0)
                ? AGE_REQ_NONE
                : AGE_REQ_CHILD;
    }

    // handle the adult trade select
    KaleidoScope_HandleItemCycleExtras(play, SLOT_TRADE_ADULT,
                                       IS_RANDO && Randomizer_GetSettingValue(RSK_SHUFFLE_ADULT_TRADE),
                                       Randomizer_GetPrevAdultTradeItem(), Randomizer_GetNextAdultTradeItem(), true);

    // Handle Nayru's Love/Roc's Feather
    KaleidoScope_HandleItemCycleExtras(play, SLOT_NAYRUS_LOVE, Randomizer_GetSettingValue(RSK_ROCS_FEATHER),
                                       Enhancement_GetPrevNayrusItem(), Enhancement_GetNextNayrusItem(), true);

    // Handle Gust Jar element cycle
    GustJar_HandleElementCycle(play);

    // Handle Bow/Slingshot elemental-arrow wheel (hold-C on bow or slingshot)
    ArrowWheel_Handle(play);

    // Handle Lantern fire-type selector (press A on lantern → stick L/R picks
    // between captured types + Vacía, press A confirms / B cancels)
    Lantern_HandleKaleidoSelector(play);

    // Twilight Upgrade mode toggles — A on hookshot/longshot (Clawshot) or
    // boomerang (Gale Boomerang) opens a 2-slot selector. Gated by the
    // corresponding twilightUpgrade bit so the toggles stay hidden until the
    // player obtains the upgrade.
    Clawshot_HandleKaleidoSelector(play);
    Gale_HandleKaleidoSelector(play);

    // Gust Jar press-A element selector (Roc's Feather style — coexists with
    // the hold-C wheel below).
    GustJar_HandlePressASelector(play);

    // Bow / Slingshot press-A arrow selector (Roc's Feather style — coexists
    // with the hold-C arrow wheel below).
    ArrowWheel_HandlePressA(play);
}

void KaleidoScope_DrawItemCycles(PlayState* play) {
    // draw the mask select
    KaleidoScope_DrawItemCycleExtras(
        play, SLOT_TRADE_CHILD, CanMaskSelect(),
        IS_RANDO ? Randomizer_GetPrevChildTradeItem()
                 : (INV_CONTENT(ITEM_TRADE_CHILD) <= ITEM_MASK_KEATON || INV_CONTENT(ITEM_TRADE_CHILD) > ITEM_MASK_TRUTH
                        ? ITEM_MASK_TRUTH
                        : INV_CONTENT(ITEM_TRADE_CHILD) - 1),
        IS_RANDO ? Randomizer_GetNextChildTradeItem()
                 : (INV_CONTENT(ITEM_TRADE_CHILD) >= ITEM_MASK_TRUTH || INV_CONTENT(ITEM_TRADE_CHILD) < ITEM_MASK_KEATON
                        ? ITEM_MASK_KEATON
                        : INV_CONTENT(ITEM_TRADE_CHILD) + 1));

    // draw the adult trade select
    KaleidoScope_DrawItemCycleExtras(play, SLOT_TRADE_ADULT,
                                     IS_RANDO && Randomizer_GetSettingValue(RSK_SHUFFLE_ADULT_TRADE),
                                     Randomizer_GetPrevAdultTradeItem(), Randomizer_GetNextAdultTradeItem());

    // Draw Nayru's Love/Roc's Feather
    KaleidoScope_DrawItemCycleExtras(play, SLOT_NAYRUS_LOVE, Randomizer_GetSettingValue(RSK_ROCS_FEATHER),
                                     Enhancement_GetPrevNayrusItem(), Enhancement_GetNextNayrusItem());

    // Draw Gust Jar element indicator
    GustJar_DrawElementCycle(play);

    // Draw Bow/Slingshot elemental-arrow wheel overlay
    ArrowWheel_Draw(play);

    // Draw Lantern fire-type selector overlay (only when active)
    Lantern_DrawKaleidoSelector(play);

    // Draw Twilight Upgrade mode toggles (Clawshot + Gale Boomerang)
    Clawshot_DrawKaleidoSelector(play);
    Gale_DrawKaleidoSelector(play);

    // Draw Gust Jar press-A selector (Roc's Feather visual)
    GustJar_DrawPressASelector(play);

    // Draw Bow / Slingshot press-A selector (Roc's Feather visual)
    ArrowWheel_DrawPressA(play);
}

bool IsItemCycling() {
    return gCurrentItemCyclingSlot != -1;
}

void KaleidoScope_ResetItemCycling() {
    gCurrentItemCyclingSlot = -1;
}

#pragma endregion

void KaleidoScope_DrawItemSelect(PlayState* play) {
    static s16 magicArrowEffectsR[] = { 255, 100, 255 };
    static s16 magicArrowEffectsG[] = { 0, 100, 255 };
    static s16 magicArrowEffectsB[] = { 0, 255, 100 };
    Input* input = &play->state.input[0];
    PauseContext* pauseCtx = &play->pauseCtx;
    u16 i;
    u16 j;
    u16 cursorItem;
    u16 cursorSlot = 0;
    u16 index;
    s16 cursorPoint;
    s16 cursorX;
    s16 cursorY;
    s16 oldCursorPoint;
    s16 moveCursorResult;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_42Opa(play->state.gfxCtx);

    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

    pauseCtx->cursorColorSet = 0;
    pauseCtx->nameColorSet = 0;

    // Update extended inventory pagination timer
    ExtInv_Update();

    if ((pauseCtx->state == 6) && (pauseCtx->unk_1E4 == 0) && (pauseCtx->pageIndex == PAUSE_ITEM)) {
        // Harpoon GM-mode: HOLD C-Up for 20 frames (~1/3 sec) while
        // hovering an inventory slot to drop the item. Multiplayer-only
        // (the C bridge no-ops if not in a Harpoon room). Holding (not
        // press-only) prevents accidental drops when the player taps
        // C-Up to switch into D-Pad swap mode. Counter resets when the
        // slot changes or C-Up is released.
        {
            static s32  sHarpoonHoldFrames = 0;
            static s16  sHarpoonHoldSlot   = -1;
            s16 curSlot = pauseCtx->cursorSlot[PAUSE_ITEM];
            if (CHECK_BTN_ALL(input->cur.button, BTN_CUP) && curSlot >= 0) {
                if (sHarpoonHoldSlot != curSlot) {
                    sHarpoonHoldSlot   = curSlot;
                    sHarpoonHoldFrames = 0;
                }
                sHarpoonHoldFrames++;
                if (sHarpoonHoldFrames == 20) {
                    extern void HarpoonDrops_RequestDropFromPause(int tabId, int slot);
                    HarpoonDrops_RequestDropFromPause(/*tabId=items*/0, curSlot);
                    // Continue counting so a long hold doesn't re-fire
                    // every frame — only the single fire at exactly 20.
                }
            } else {
                sHarpoonHoldFrames = 0;
                sHarpoonHoldSlot   = -1;
            }
        }
        bool dpad = (CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0) && !CHECK_BTN_ALL(input->cur.button, BTN_CUP));
        bool pauseAnyCursor =
            pauseCtx->cursorSpecialPos == 0 &&
            ((CVarGetInteger(CVAR_ENHANCEMENT("PauseAnyCursor"), 0) == PAUSE_ANY_CURSOR_RANDO_ONLY && IS_RANDO) ||
             (CVarGetInteger(CVAR_ENHANCEMENT("PauseAnyCursor"), 0) == PAUSE_ANY_CURSOR_ALWAYS_ON));

        moveCursorResult = 0 || IsItemCycling() || sGustOverlayActive || sArrowWheelOverlayActive;
        oldCursorPoint = pauseCtx->cursorPoint[PAUSE_ITEM];

        cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
        cursorSlot = pauseCtx->cursorSlot[PAUSE_ITEM];

        // Allow the inventory sub-page switch from the item grid or either page arrow.
        bool ngcMode = CVarGetInteger(CVAR_ENHANCEMENT("NGCKaleidoSwitcher"), 0) != 0;
        s16 freedBtn = ngcMode ? BTN_Z : BTN_L;

        if (ExtInv_CanSwitchPage() && CHECK_BTN_ALL(input->press.button, freedBtn) && !IsItemCycling()) {
            ExtInv_SwitchPage();
            Audio_PlaySoundGeneral(NA_SE_SY_HP_RECOVER, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            moveCursorResult = 2;
        }

        if (pauseCtx->cursorSpecialPos == 0) {
            pauseCtx->cursorColorSet = 4;

            if (cursorItem == PAUSE_ITEM_NONE) {
                pauseCtx->stickRelX = 40;
            }

            if ((ABS(pauseCtx->stickRelX) > 30) ||
                (dpad && CHECK_BTN_ANY(input->press.button, BTN_DLEFT | BTN_DRIGHT))) {
                cursorPoint = pauseCtx->cursorPoint[PAUSE_ITEM];
                cursorX = pauseCtx->cursorX[PAUSE_ITEM];
                cursorY = pauseCtx->cursorY[PAUSE_ITEM];

                osSyncPrintf("now=%d  ccc=%d\n", cursorPoint, cursorItem);

                // Seem necessary to match
                if (pauseCtx->cursorX[PAUSE_ITEM]) {}
                if (gSaveContext.inventory.items[pauseCtx->cursorPoint[PAUSE_ITEM]]) {}

                while (moveCursorResult == 0) {
                    if ((pauseCtx->stickRelX < -30) || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DLEFT))) {
                        if (pauseCtx->cursorX[PAUSE_ITEM] != 0) {
                            pauseCtx->cursorX[PAUSE_ITEM] -= 1;
                            pauseCtx->cursorPoint[PAUSE_ITEM] -= 1;
                            if ((gSaveContext.inventory.items[ExtInv_GetInventorySlot(
                                     pauseCtx->cursorPoint[PAUSE_ITEM])] != ITEM_NONE) ||
                                pauseAnyCursor) {
                                moveCursorResult = 1;
                            }
                        } else {
                            pauseCtx->cursorX[PAUSE_ITEM] = cursorX;
                            pauseCtx->cursorY[PAUSE_ITEM] += 1;

                            if (pauseCtx->cursorY[PAUSE_ITEM] >= 4) {
                                pauseCtx->cursorY[PAUSE_ITEM] = 0;
                            }

                            pauseCtx->cursorPoint[PAUSE_ITEM] =
                                pauseCtx->cursorX[PAUSE_ITEM] + (pauseCtx->cursorY[PAUSE_ITEM] * 6);

                            if (pauseCtx->cursorPoint[PAUSE_ITEM] >= 24) {
                                pauseCtx->cursorPoint[PAUSE_ITEM] = pauseCtx->cursorX[PAUSE_ITEM];
                            }

                            if (cursorY == pauseCtx->cursorY[PAUSE_ITEM]) {
                                pauseCtx->cursorX[PAUSE_ITEM] = cursorX;
                                pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;

                                KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_LEFT);

                                moveCursorResult = 2;
                            }
                        }
                    } else if ((pauseCtx->stickRelX > 30) || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DRIGHT))) {
                        if (pauseCtx->cursorX[PAUSE_ITEM] < 5) {
                            pauseCtx->cursorX[PAUSE_ITEM] += 1;
                            pauseCtx->cursorPoint[PAUSE_ITEM] += 1;
                            if ((gSaveContext.inventory.items[ExtInv_GetInventorySlot(
                                     pauseCtx->cursorPoint[PAUSE_ITEM])] != ITEM_NONE) ||
                                pauseAnyCursor) {
                                moveCursorResult = 1;
                            }
                        } else {
                            pauseCtx->cursorX[PAUSE_ITEM] = cursorX;
                            pauseCtx->cursorY[PAUSE_ITEM] += 1;

                            if (pauseCtx->cursorY[PAUSE_ITEM] >= 4) {
                                pauseCtx->cursorY[PAUSE_ITEM] = 0;
                            }

                            pauseCtx->cursorPoint[PAUSE_ITEM] =
                                pauseCtx->cursorX[PAUSE_ITEM] + (pauseCtx->cursorY[PAUSE_ITEM] * 6);

                            if (pauseCtx->cursorPoint[PAUSE_ITEM] >= 24) {
                                pauseCtx->cursorPoint[PAUSE_ITEM] = pauseCtx->cursorX[PAUSE_ITEM];
                            }

                            if (cursorY == pauseCtx->cursorY[PAUSE_ITEM]) {
                                pauseCtx->cursorX[PAUSE_ITEM] = cursorX;
                                pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;

                                KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_RIGHT);

                                moveCursorResult = 2;
                            }
                        }
                    }
                }

                if (moveCursorResult == 1) {
                    cursorItem =
                        gSaveContext.inventory.items[ExtInv_GetInventorySlot(pauseCtx->cursorPoint[PAUSE_ITEM])];
                }

                osSyncPrintf("【Ｘ cursor=%d(%) (cur_xpt=%d)(ok_fg=%d)(ccc=%d)(key_angle=%d)】  ",
                             pauseCtx->cursorPoint[PAUSE_ITEM], pauseCtx->cursorX[PAUSE_ITEM], moveCursorResult,
                             cursorItem, pauseCtx->cursorSpecialPos);
            }
        } else if (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_LEFT) {
            if ((pauseCtx->stickRelX > 30) || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DRIGHT))) {
                pauseCtx->nameDisplayTimer = 0;
                pauseCtx->cursorSpecialPos = 0;

                Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);

                cursorPoint = cursorX = cursorY = 0;
                while (true) {
                    if (gSaveContext.inventory.items[ExtInv_GetInventorySlot(cursorPoint)] != ITEM_NONE) {
                        pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;
                        pauseCtx->cursorX[PAUSE_ITEM] = cursorX;
                        pauseCtx->cursorY[PAUSE_ITEM] = cursorY;
                        moveCursorResult = 1;
                        break;
                    }

                    cursorY = cursorY + 1;
                    cursorPoint = cursorPoint + 6;
                    if (cursorY < 4) {
                        continue;
                    }

                    cursorY = 0;
                    cursorPoint = cursorX + 1;
                    cursorX = cursorPoint;
                    if (cursorX < 6) {
                        continue;
                    }

                    KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_RIGHT);
                    break;
                }
            }
        } else {
            if ((pauseCtx->stickRelX < -30) || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DLEFT))) {
                pauseCtx->nameDisplayTimer = 0;
                pauseCtx->cursorSpecialPos = 0;

                Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);

                cursorPoint = cursorX = 5;
                cursorY = 0;
                while (true) {
                    if (gSaveContext.inventory.items[ExtInv_GetInventorySlot(cursorPoint)] != ITEM_NONE) {
                        pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;
                        pauseCtx->cursorX[PAUSE_ITEM] = cursorX;
                        pauseCtx->cursorY[PAUSE_ITEM] = cursorY;
                        moveCursorResult = 1;
                        break;
                    }

                    cursorY = cursorY + 1;
                    cursorPoint = cursorPoint + 6;
                    if (cursorY < 4) {
                        continue;
                    }

                    cursorY = 0;
                    cursorPoint = cursorX - 1;
                    cursorX = cursorPoint;
                    if (cursorX >= 0) {
                        continue;
                    }

                    KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_LEFT);
                    break;
                }
            }
        }

        if (pauseCtx->cursorSpecialPos == 0) {
            if (cursorItem != PAUSE_ITEM_NONE) {
                if ((ABS(pauseCtx->stickRelY) > 30) ||
                    (dpad && CHECK_BTN_ANY(input->press.button, BTN_DDOWN | BTN_DUP))) {
                    moveCursorResult = 0 || IsItemCycling() || sGustOverlayActive || sArrowWheelOverlayActive;

                    cursorPoint = pauseCtx->cursorPoint[PAUSE_ITEM];
                    cursorY = pauseCtx->cursorY[PAUSE_ITEM];
                    while (moveCursorResult == 0) {
                        if ((pauseCtx->stickRelY > 30) || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DUP))) {
                            if (pauseCtx->cursorY[PAUSE_ITEM] != 0) {
                                pauseCtx->cursorY[PAUSE_ITEM] -= 1;
                                pauseCtx->cursorPoint[PAUSE_ITEM] -= 6;
                                if ((gSaveContext.inventory.items[ExtInv_GetInventorySlot(
                                         pauseCtx->cursorPoint[PAUSE_ITEM])] != ITEM_NONE) ||
                                    pauseAnyCursor) {
                                    moveCursorResult = 1;
                                }
                            } else {
                                pauseCtx->cursorY[PAUSE_ITEM] = cursorY;
                                pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;

                                moveCursorResult = 2;
                            }
                        } else if ((pauseCtx->stickRelY < -30) ||
                                   (dpad && CHECK_BTN_ALL(input->press.button, BTN_DDOWN))) {
                            if (pauseCtx->cursorY[PAUSE_ITEM] < 3) {
                                pauseCtx->cursorY[PAUSE_ITEM] += 1;
                                pauseCtx->cursorPoint[PAUSE_ITEM] += 6;
                                if ((gSaveContext.inventory.items[ExtInv_GetInventorySlot(
                                         pauseCtx->cursorPoint[PAUSE_ITEM])] != ITEM_NONE) ||
                                    pauseAnyCursor) {
                                    moveCursorResult = 1;
                                }
                            } else {
                                pauseCtx->cursorY[PAUSE_ITEM] = cursorY;
                                pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;

                                moveCursorResult = 2;
                            }
                        }
                    }

                    cursorPoint = PAUSE_ITEM;
                    osSyncPrintf("【Ｙ cursor=%d(%) (cur_ypt=%d)(ok_fg=%d)(ccc=%d)】  ",
                                 pauseCtx->cursorPoint[cursorPoint], pauseCtx->cursorY[PAUSE_ITEM], moveCursorResult,
                                 cursorItem);
                }
            }

            cursorSlot = pauseCtx->cursorPoint[PAUSE_ITEM];

            pauseCtx->cursorColorSet = 4;

            // Calculate inventory slot with page offset using modular system
            int inventorySlot = ExtInv_GetInventorySlot(pauseCtx->cursorPoint[PAUSE_ITEM]);

            if (moveCursorResult == 1) {
                cursorItem = gSaveContext.inventory.items[inventorySlot];
            } else if (moveCursorResult != 2) {
                cursorItem = gSaveContext.inventory.items[inventorySlot];
            }

            pauseCtx->cursorItem[PAUSE_ITEM] = cursorItem;
            pauseCtx->cursorSlot[PAUSE_ITEM] = cursorSlot;

            if (!CHECK_AGE_REQ_SLOT(inventorySlot)) {
                pauseCtx->nameColorSet = 1;
            }

            if (cursorItem != PAUSE_ITEM_NONE) {
                index = cursorSlot * 4; // required to match?
                KaleidoScope_SetCursorVtx(pauseCtx, index, pauseCtx->itemVtx);

                if ((pauseCtx->debugState == 0) && (pauseCtx->state == 6) && (pauseCtx->unk_1E4 == 0)) {
                    KaleidoScope_HandleItemCycles(play);
                    u16 buttonsToCheck = BTN_CLEFT | BTN_CDOWN | BTN_CRIGHT;
                    if (CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0) &&
                        (!CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0) ||
                         CHECK_BTN_ALL(input->cur.button, BTN_CUP))) {
                        buttonsToCheck |= BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT;
                    }
                    if (CHECK_BTN_ANY(input->press.button, buttonsToCheck) && !sGustOverlayActive &&
                        !sArrowWheelOverlayActive) {
                        if (CHECK_AGE_REQ_SLOT(inventorySlot) && (cursorItem != ITEM_SOLD_OUT) &&
                            (cursorItem != ITEM_NONE)) {
                            // Use inventorySlot (real slot 0-47) instead of cursorSlot (visual slot 0-23)
                            // This allows items from page 1 and page 2 with the same relative position to be equipped
                            // simultaneously
                            if (GameInteractor_Should(VB_EQUIP_ITEM_TO_C_BUTTON, true, play, inventorySlot,
                                                      cursorItem)) {
                                KaleidoScope_SetupItemEquip(play, cursorItem, inventorySlot,
                                                            pauseCtx->itemVtx[index].v.ob[0] * 10,
                                                            pauseCtx->itemVtx[index].v.ob[1] * 10);
                            }
                        } else {
                            Audio_PlaySoundGeneral(NA_SE_SY_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                        }
                    }
                }
            } else {
                pauseCtx->cursorVtx[0].v.ob[0] = pauseCtx->cursorVtx[2].v.ob[0] = pauseCtx->cursorVtx[1].v.ob[0] =
                    pauseCtx->cursorVtx[3].v.ob[0] = 0;

                pauseCtx->cursorVtx[0].v.ob[1] = pauseCtx->cursorVtx[1].v.ob[1] = pauseCtx->cursorVtx[2].v.ob[1] =
                    pauseCtx->cursorVtx[3].v.ob[1] = -200;
            }
        } else {
            pauseCtx->cursorItem[PAUSE_ITEM] = PAUSE_ITEM_NONE;
        }

        if (oldCursorPoint != pauseCtx->cursorPoint[PAUSE_ITEM]) {
            Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
    } else if ((pauseCtx->unk_1E4 == 3) && (pauseCtx->pageIndex == PAUSE_ITEM)) {
        KaleidoScope_SetCursorVtx(pauseCtx, cursorSlot * 4, pauseCtx->itemVtx);
        pauseCtx->cursorColorSet = 4;
    }

    gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
    gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 0);

    for (i = 0, j = 24 * 4; i < ARRAY_COUNT(gSaveContext.equips.cButtonSlots); i++, j += 4) {
        if ((gSaveContext.equips.buttonItems[i + 1] != ITEM_NONE) &&
            !((gSaveContext.equips.buttonItems[i + 1] >= ITEM_SHIELD_DEKU) &&
              (gSaveContext.equips.buttonItems[i + 1] <= ITEM_BOOTS_HOVER))) {
            gSPVertex(POLY_OPA_DISP++, &pauseCtx->itemVtx[j], 4, 0);
            POLY_OPA_DISP = KaleidoScope_QuadTextureIA8(POLY_OPA_DISP, gEquippedItemOutlineTex, 32, 32, 0);
        }
    }

    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

    for (i = j = 0; i < 24; i++, j += 4) {
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);

        int drawSlot = ExtInv_GetInventorySlot(i);
        if (gSaveContext.inventory.items[drawSlot] != ITEM_NONE) {
            if ((pauseCtx->unk_1E4 == 0) && (pauseCtx->pageIndex == PAUSE_ITEM) && (pauseCtx->cursorSpecialPos == 0)) {
                if (CHECK_AGE_REQ_SLOT(drawSlot)) {
                    if ((sEquipState == 2) && (i == 3)) {
                        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, magicArrowEffectsR[pauseCtx->equipTargetItem - 0xBF],
                                        magicArrowEffectsG[pauseCtx->equipTargetItem - 0xBF],
                                        magicArrowEffectsB[pauseCtx->equipTargetItem - 0xBF], pauseCtx->alpha);

                        pauseCtx->itemVtx[j + 0].v.ob[0] = pauseCtx->itemVtx[j + 2].v.ob[0] =
                            pauseCtx->itemVtx[j + 0].v.ob[0] - 2;

                        pauseCtx->itemVtx[j + 1].v.ob[0] = pauseCtx->itemVtx[j + 3].v.ob[0] =
                            pauseCtx->itemVtx[j + 0].v.ob[0] + 32;

                        pauseCtx->itemVtx[j + 0].v.ob[1] = pauseCtx->itemVtx[j + 1].v.ob[1] =
                            pauseCtx->itemVtx[j + 0].v.ob[1] + 2;

                        pauseCtx->itemVtx[j + 2].v.ob[1] = pauseCtx->itemVtx[j + 3].v.ob[1] =
                            pauseCtx->itemVtx[j + 0].v.ob[1] - 32;
                    } else if (i == cursorSlot) {
                        pauseCtx->itemVtx[j + 0].v.ob[0] = pauseCtx->itemVtx[j + 2].v.ob[0] =
                            pauseCtx->itemVtx[j + 0].v.ob[0] - 2;

                        pauseCtx->itemVtx[j + 1].v.ob[0] = pauseCtx->itemVtx[j + 3].v.ob[0] =
                            pauseCtx->itemVtx[j + 0].v.ob[0] + 32;

                        pauseCtx->itemVtx[j + 0].v.ob[1] = pauseCtx->itemVtx[j + 1].v.ob[1] =
                            pauseCtx->itemVtx[j + 0].v.ob[1] + 2;

                        pauseCtx->itemVtx[j + 2].v.ob[1] = pauseCtx->itemVtx[j + 3].v.ob[1] =
                            pauseCtx->itemVtx[j + 0].v.ob[1] - 32;
                    }
                }
            }

            gSPVertex(POLY_OPA_DISP++, &pauseCtx->itemVtx[j + 0], 4, 0);
            int itemId = gSaveContext.inventory.items[drawSlot];
            bool not_acquired = !CHECK_AGE_REQ_SLOT(drawSlot);
            if (not_acquired) {
                gDPSetGrayscaleColor(POLY_OPA_DISP++, 109, 109, 109, 255);
                gSPGrayscale(POLY_OPA_DISP++, true);
            }
            KaleidoScope_DrawQuadTextureRGBA32(play->state.gfxCtx, ExtInv_GetItemIcon(itemId), 32, 32, 0);
            gSPGrayscale(POLY_OPA_DISP++, false);
            // (Twilight L badge removed from kaleido — the A-press
            //  selector in Clawshot_/Gale_DrawKaleidoSelector now
            //  serves as the visual mode-toggle hint. The L hint stays
            //  on the C-button HUD for in-gameplay binding feedback.)
        }
    }

    if (pauseCtx->cursorSpecialPos == 0) {
        KaleidoScope_DrawCursor(play, PAUSE_ITEM);
    }

    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);

    u8 gBetterAmmoRendering = CVarGetInteger(CVAR_ENHANCEMENT("BetterAmmoRendering"), 0);

    if (ExtInv_GetCurrentPage() == 0) {
        for (i = 0; i < (gBetterAmmoRendering ? 24 : 15); i++) {
            if ((gBetterAmmoRendering ? ItemInSlotUsesAmmo(i) : gAmmoItems[i] != ITEM_NONE) &&
                (gSaveContext.inventory.items[i] != ITEM_NONE)) {
                KaleidoScope_DrawAmmoCount(pauseCtx, play->state.gfxCtx, gSaveContext.inventory.items[i], i);
            }
        }
    }

    KaleidoScope_DrawItemCycles(play);

    CLOSE_DISPS(play->state.gfxCtx);
}

void KaleidoScope_SetupItemEquip(PlayState* play, u16 item, u16 slot, s16 animX, s16 animY) {
    Input* input = &play->state.input[0];
    PauseContext* pauseCtx = &play->pauseCtx;
    KaleidoScope_ResetItemCycling();

    if (CHECK_BTN_ALL(input->press.button, BTN_CLEFT)) {
        pauseCtx->equipTargetCBtn = 0;
    } else if (CHECK_BTN_ALL(input->press.button, BTN_CDOWN)) {
        pauseCtx->equipTargetCBtn = 1;
    } else if (CHECK_BTN_ALL(input->press.button, BTN_CRIGHT)) {
        pauseCtx->equipTargetCBtn = 2;
    } else if (CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0)) {
        if (CHECK_BTN_ALL(input->press.button, BTN_DUP)) {
            pauseCtx->equipTargetCBtn = 3;
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DDOWN)) {
            pauseCtx->equipTargetCBtn = 4;
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DLEFT)) {
            pauseCtx->equipTargetCBtn = 5;
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DRIGHT)) {
            pauseCtx->equipTargetCBtn = 6;
        }
    }

    // SM64 Mario mode: C-Left / C-Right and the whole D-pad are reserved for
    // Mario's moves (Cappy / Roll / power-up caps), so block equipping OOT items
    // onto them — only C-Down (equipTargetCBtn == 1) stays free (it becomes
    // Mario's item slot). The real equips are never cleared (just hidden while
    // Mario), so they reappear on Link; this only stops NEW assignments.
    if (CVarGetInteger("gSm64Mario", 0) != 0 && pauseCtx->equipTargetCBtn != 1) {
        Audio_PlaySoundGeneral(NA_SE_SY_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    // NEI's MM mask IDs overlap the range vanilla uses internally for its
    // magic-arrow equip animation (0xBF+). That animation indexes three-entry
    // arrow color arrays with the item ID and its overlay texture command is
    // also unsafe for dynamically resolved OTR paths. Equip masks immediately;
    // the pause grid and live HUD still render their managed HD textures.
    if (item >= ITEM_MM_MASK_POSTMAN && item <= ITEM_MM_MASK_FIERCE_DEITY) {
        uint16_t targetButtonIndex = pauseCtx->equipTargetCBtn + 1;
        for (uint16_t otherSlotIndex = 0; otherSlotIndex < ARRAY_COUNT(gSaveContext.equips.cButtonSlots);
             otherSlotIndex++) {
            uint16_t otherButtonIndex = otherSlotIndex + 1;
            if (otherSlotIndex == pauseCtx->equipTargetCBtn) {
                continue;
            }
            if (slot == gSaveContext.equips.cButtonSlots[otherSlotIndex]) {
                if (gSaveContext.equips.buttonItems[targetButtonIndex] != ITEM_NONE) {
                    gSaveContext.equips.buttonItems[otherButtonIndex] =
                        gSaveContext.equips.buttonItems[targetButtonIndex];
                    gSaveContext.equips.cButtonSlots[otherSlotIndex] =
                        gSaveContext.equips.cButtonSlots[pauseCtx->equipTargetCBtn];
                    Interface_LoadItemIcon2(play, otherButtonIndex);
                } else {
                    gSaveContext.equips.buttonItems[otherButtonIndex] = ITEM_NONE;
                    gSaveContext.equips.cButtonSlots[otherSlotIndex] = SLOT_NONE;
                }
            }
        }

        gSaveContext.equips.buttonItems[targetButtonIndex] = item;
        gSaveContext.equips.cButtonSlots[pauseCtx->equipTargetCBtn] = slot;
        Interface_LoadItemIcon1(play, targetButtonIndex);
        pauseCtx->unk_1E4 = 0;
        Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    pauseCtx->equipTargetItem = item;
    pauseCtx->equipTargetSlot = slot;
    pauseCtx->unk_1E4 = 3;
    pauseCtx->equipAnimX = animX;
    pauseCtx->equipAnimY = animY;
    pauseCtx->equipAnimAlpha = 255;
    sEquipAnimTimer = 0;
    sEquipState = 3;
    sEquipMoveTimer = 10;
    if ((pauseCtx->equipTargetItem == ITEM_ARROW_FIRE) || (pauseCtx->equipTargetItem == ITEM_ARROW_ICE) ||
        (pauseCtx->equipTargetItem == ITEM_ARROW_LIGHT)) {
        if (CVarGetInteger(CVAR_ENHANCEMENT("SkipArrowAnimation"), 0)) {
            Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        } else {
            u16 index = 0;
            if (pauseCtx->equipTargetItem == ITEM_ARROW_ICE) {
                index = 1;
            }
            if (pauseCtx->equipTargetItem == ITEM_ARROW_LIGHT) {
                index = 2;
            }
            Audio_PlaySoundGeneral(NA_SE_SY_SET_FIRE_ARROW + index, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            pauseCtx->equipTargetItem = 0xBF + index;
            sEquipState = 0;
            pauseCtx->equipAnimAlpha = 0;
            sEquipMoveTimer = 6;
        }
    } else {
        Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
}

// TODO update for final positions
static s16 sCButtonPosX[] = { 66, 90, 114, 110, 110, 86, 134 };
static s16 sCButtonPosY[] = { 110, 92, 110, 76, 44, 62, 62 };

void KaleidoScope_UpdateItemEquip(PlayState* play) {
    static s16 D_8082A488 = 0;
    PauseContext* pauseCtx = &play->pauseCtx;
    Vtx* bowItemVtx;
    u16 offsetX;
    u16 offsetY;

    s16 Top_HUD_Margin = CVarGetInteger(CVAR_COSMETIC("HUD.Margin.T"), 0);
    s16 Left_HUD_Margin = CVarGetInteger(CVAR_COSMETIC("HUD.Margin.L"), 0);
    s16 Right_HUD_Margin = CVarGetInteger(CVAR_COSMETIC("HUD.Margin.R"), 0);
    s16 Bottom_HUD_Margin = CVarGetInteger(CVAR_COSMETIC("HUD.Margin.B"), 0);

    s16 X_Margins_CL;
    s16 X_Margins_CR;
    s16 X_Margins_CD;
    s16 Y_Margins_CL;
    s16 Y_Margins_CR;
    s16 Y_Margins_CD;
    s16 X_Margins_BtnB;
    s16 Y_Margins_BtnB;
    s16 X_Margins_DPad_Items;
    s16 Y_Margins_DPad_Items;
    if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.UseMargins"), 0) != 0) {
        if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosType"), 0) == ORIGINAL_LOCATION) {
            X_Margins_BtnB = Right_HUD_Margin;
        };
        Y_Margins_BtnB = (Top_HUD_Margin * -1);
    } else {
        X_Margins_BtnB = 0;
        Y_Margins_BtnB = 0;
    }
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.UseMargins"), 0) != 0) {
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosType"), 0) == ORIGINAL_LOCATION) {
            X_Margins_CL = Right_HUD_Margin;
        };
        Y_Margins_CL = (Top_HUD_Margin * -1);
    } else {
        X_Margins_CL = 0;
        Y_Margins_CL = 0;
    }
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.UseMargins"), 0) != 0) {
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosType"), 0) == ORIGINAL_LOCATION) {
            X_Margins_CR = Right_HUD_Margin;
        };
        Y_Margins_CR = (Top_HUD_Margin * -1);
    } else {
        X_Margins_CR = 0;
        Y_Margins_CR = 0;
    }
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.UseMargins"), 0) != 0) {
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosType"), 0) == ORIGINAL_LOCATION) {
            X_Margins_CD = Right_HUD_Margin;
        };
        Y_Margins_CD = (Top_HUD_Margin * -1);
    } else {
        X_Margins_CD = 0;
        Y_Margins_CD = 0;
    }
    if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.UseMargins"), 0) != 0) {
        if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) == ORIGINAL_LOCATION) {
            X_Margins_DPad_Items = Right_HUD_Margin;
        };
        Y_Margins_DPad_Items = (Top_HUD_Margin * -1);
    } else {
        X_Margins_DPad_Items = 0;
        Y_Margins_DPad_Items = 0;
    }
    const s16 ItemIconPos_ori[7][2] = { { C_LEFT_BUTTON_X + X_Margins_CL, C_LEFT_BUTTON_Y + Y_Margins_CL },
                                        { C_DOWN_BUTTON_X + X_Margins_CD, C_DOWN_BUTTON_Y + Y_Margins_CD },
                                        { C_RIGHT_BUTTON_X + X_Margins_CR, C_RIGHT_BUTTON_Y + Y_Margins_CR },
                                        { DPAD_UP_X + X_Margins_DPad_Items, DPAD_UP_Y + Y_Margins_DPad_Items },
                                        { DPAD_DOWN_X + X_Margins_DPad_Items, DPAD_DOWN_Y + Y_Margins_DPad_Items },
                                        { DPAD_LEFT_X + X_Margins_DPad_Items, DPAD_LEFT_Y + Y_Margins_DPad_Items },
                                        { DPAD_RIGHT_X + X_Margins_DPad_Items, DPAD_RIGHT_Y + Y_Margins_DPad_Items } };
    s16 DPad_ItemsOffset[4][2] = {
        { 7, -8 }, // Up
        { 7, 24 }, // Down
        { -9, 8 }, // Left
        { 23, 8 }, // Right
    };             //(X,Y) Used with custom position to place it properly.

    // DPadItems
    if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) != ORIGINAL_LOCATION) {
        sCButtonPosY[3] =
            CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosY"), 0) + Y_Margins_DPad_Items + DPad_ItemsOffset[0][1]; // Up
        sCButtonPosY[4] =
            CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosY"), 0) + Y_Margins_DPad_Items + DPad_ItemsOffset[1][1]; // Down
        sCButtonPosY[5] =
            CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosY"), 0) + Y_Margins_DPad_Items + DPad_ItemsOffset[2][1]; // Left
        sCButtonPosY[6] =
            CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosY"), 0) + Y_Margins_DPad_Items + DPad_ItemsOffset[3][1]; // Right
        if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) == ANCHOR_LEFT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.UseMargins"), 0) != 0) {
                X_Margins_DPad_Items = Left_HUD_Margin;
            };
            sCButtonPosX[3] = OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) +
                                                          X_Margins_DPad_Items + DPad_ItemsOffset[0][0]);
            sCButtonPosX[4] = OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) +
                                                          X_Margins_DPad_Items + DPad_ItemsOffset[1][0]);
            sCButtonPosX[5] = OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) +
                                                          X_Margins_DPad_Items + DPad_ItemsOffset[2][0]);
            sCButtonPosX[6] = OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) +
                                                          X_Margins_DPad_Items + DPad_ItemsOffset[3][0]);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) == ANCHOR_RIGHT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.UseMargins"), 0) != 0) {
                X_Margins_DPad_Items = Right_HUD_Margin;
            };
            sCButtonPosX[3] = OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) +
                                                           X_Margins_DPad_Items + DPad_ItemsOffset[0][0]);
            sCButtonPosX[4] = OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) +
                                                           X_Margins_DPad_Items + DPad_ItemsOffset[1][0]);
            sCButtonPosX[5] = OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) +
                                                           X_Margins_DPad_Items + DPad_ItemsOffset[2][0]);
            sCButtonPosX[6] = OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) +
                                                           X_Margins_DPad_Items + DPad_ItemsOffset[3][0]);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) == ANCHOR_NONE) {
            sCButtonPosX[3] = CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) + DPad_ItemsOffset[0][0];
            sCButtonPosX[4] = CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) + DPad_ItemsOffset[1][0];
            sCButtonPosX[5] = CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) + DPad_ItemsOffset[2][0];
            sCButtonPosX[6] = CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) + DPad_ItemsOffset[3][0];
        }
    } else {
        sCButtonPosX[3] = OTRGetDimensionFromRightEdge(ItemIconPos_ori[3][0]);
        sCButtonPosX[4] = OTRGetDimensionFromRightEdge(ItemIconPos_ori[4][0]);
        sCButtonPosX[5] = OTRGetDimensionFromRightEdge(ItemIconPos_ori[5][0]);
        sCButtonPosX[6] = OTRGetDimensionFromRightEdge(ItemIconPos_ori[6][0]);
        sCButtonPosY[3] = ItemIconPos_ori[3][1];
        sCButtonPosY[4] = ItemIconPos_ori[4][1];
        sCButtonPosY[5] = ItemIconPos_ori[5][1];
        sCButtonPosY[6] = ItemIconPos_ori[6][1];
    }
    // C button Left
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosType"), 0) != ORIGINAL_LOCATION) {
        sCButtonPosY[0] = CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosY"), 0) + Y_Margins_CL;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosType"), 0) == ANCHOR_LEFT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.UseMargins"), 0) != 0) {
                X_Margins_CL = Left_HUD_Margin;
            };
            sCButtonPosX[0] =
                OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosX"), 0) + X_Margins_CL);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosType"), 0) == ANCHOR_RIGHT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.UseMargins"), 0) != 0) {
                X_Margins_CL = Right_HUD_Margin;
            };
            sCButtonPosX[0] =
                OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosX"), 0) + X_Margins_CL);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosType"), 0) == ANCHOR_NONE) {
            sCButtonPosX[0] = CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosX"), 0);
        }
    } else {
        sCButtonPosX[0] = OTRGetRectDimensionFromRightEdge(ItemIconPos_ori[0][0]);
        sCButtonPosY[0] = ItemIconPos_ori[0][1];
    }
    // C Button down
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosType"), 0) != ORIGINAL_LOCATION) {
        sCButtonPosY[1] = CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosY"), 0) + Y_Margins_CD;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosType"), 0) == ANCHOR_LEFT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.UseMargins"), 0) != 0) {
                X_Margins_CD = Left_HUD_Margin;
            };
            sCButtonPosX[1] =
                OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosX"), 0) + X_Margins_CD);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosType"), 0) == ANCHOR_RIGHT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.UseMargins"), 0) != 0) {
                X_Margins_CD = Right_HUD_Margin;
            };
            sCButtonPosX[1] =
                OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosX"), 0) + X_Margins_CD);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosType"), 0) == ANCHOR_NONE) {
            sCButtonPosX[1] = CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosX"), 0);
        }
    } else {
        sCButtonPosX[1] = OTRGetRectDimensionFromRightEdge(ItemIconPos_ori[1][0]);
        sCButtonPosY[1] = ItemIconPos_ori[1][1];
    }
    // C button Right
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosType"), 0) != ORIGINAL_LOCATION) {
        sCButtonPosY[2] = CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosY"), 0) + Y_Margins_CR;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosType"), 0) == ANCHOR_LEFT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.UseMargins"), 0) != 0) {
                X_Margins_CR = Left_HUD_Margin;
            };
            sCButtonPosX[2] =
                OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosX"), 0) + X_Margins_CR);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosType"), 0) == ANCHOR_RIGHT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.UseMargins"), 0) != 0) {
                X_Margins_CR = Right_HUD_Margin;
            };
            sCButtonPosX[2] =
                OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosX"), 0) + X_Margins_CR);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosType"), 0) == ANCHOR_NONE) {
            sCButtonPosX[2] = CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosX"), 0);
        }
    } else {
        sCButtonPosX[2] = OTRGetRectDimensionFromRightEdge(ItemIconPos_ori[2][0]);
        sCButtonPosY[2] = ItemIconPos_ori[2][1];
    }

    sCButtonPosX[0] = sCButtonPosX[0] - 160;
    sCButtonPosY[0] = 120 - sCButtonPosY[0];
    sCButtonPosX[1] = sCButtonPosX[1] - 160;
    sCButtonPosY[1] = 120 - sCButtonPosY[1];
    sCButtonPosX[2] = sCButtonPosX[2] - 160;
    sCButtonPosY[2] = 120 - sCButtonPosY[2];
    sCButtonPosX[3] = sCButtonPosX[3] - 160;
    sCButtonPosY[3] = 120 - sCButtonPosY[3];
    sCButtonPosX[4] = sCButtonPosX[4] - 160;
    sCButtonPosY[4] = 120 - sCButtonPosY[4];
    sCButtonPosX[5] = sCButtonPosX[5] - 160;
    sCButtonPosY[5] = 120 - sCButtonPosY[5];
    sCButtonPosX[6] = sCButtonPosX[6] - 160;
    sCButtonPosY[6] = 120 - sCButtonPosY[6];

    if (sEquipState == 0) {
        pauseCtx->equipAnimAlpha += 14;
        if (pauseCtx->equipAnimAlpha > 255) {
            pauseCtx->equipAnimAlpha = 254;
            sEquipState++;
        }
        sEquipAnimTimer = 5;
        return;
    }

    if (sEquipState == 2) {
        D_8082A488--;

        if (D_8082A488 == 0) {
            pauseCtx->equipTargetItem -= 0xBF - ITEM_BOW_ARROW_FIRE;
            if (!CVarGetInteger(CVAR_ENHANCEMENT("SeparateArrows"), 0)) {
                pauseCtx->equipTargetSlot = SLOT_BOW;
            }
            sEquipMoveTimer = 6;
            WREG(90) = 320;
            WREG(87) = WREG(91);
            sEquipState++;
            Audio_PlaySoundGeneral(NA_SE_SY_SYNTH_MAGIC_ARROW, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
        return;
    }

    if (sEquipState == 1) {
        bowItemVtx = &pauseCtx->itemVtx[12];
        offsetX = ABS(pauseCtx->equipAnimX - bowItemVtx->v.ob[0] * 10) / sEquipMoveTimer;
        offsetY = ABS(pauseCtx->equipAnimY - bowItemVtx->v.ob[1] * 10) / sEquipMoveTimer;
    } else {
        offsetX = ABS(pauseCtx->equipAnimX - sCButtonPosX[pauseCtx->equipTargetCBtn] * 10) / sEquipMoveTimer;
        offsetY = ABS(pauseCtx->equipAnimY - sCButtonPosY[pauseCtx->equipTargetCBtn] * 10) / sEquipMoveTimer;
    }

    if ((pauseCtx->equipTargetItem >= 0xBF) && (pauseCtx->equipAnimAlpha < 254)) {
        pauseCtx->equipAnimAlpha += 14;
        if (pauseCtx->equipAnimAlpha > 255) {
            pauseCtx->equipAnimAlpha = 254;
        }
        sEquipAnimTimer = 5;
        return;
    }

    if (sEquipAnimTimer == 0) {
        WREG(90) -= WREG(87) / sEquipMoveTimer;
        WREG(87) -= WREG(87) / sEquipMoveTimer;

        if (sEquipState == 1) {
            if (pauseCtx->equipAnimX >= (pauseCtx->itemVtx[12].v.ob[0] * 10)) {
                pauseCtx->equipAnimX -= offsetX;
            } else {
                pauseCtx->equipAnimX += offsetX;
            }

            if (pauseCtx->equipAnimY >= (pauseCtx->itemVtx[12].v.ob[1] * 10)) {
                pauseCtx->equipAnimY -= offsetY;
            } else {
                pauseCtx->equipAnimY += offsetY;
            }
        } else {
            if (pauseCtx->equipAnimX >= sCButtonPosX[pauseCtx->equipTargetCBtn] * 10) {
                pauseCtx->equipAnimX -= offsetX;
            } else {
                pauseCtx->equipAnimX += offsetX;
            }

            if (pauseCtx->equipAnimY >= sCButtonPosY[pauseCtx->equipTargetCBtn] * 10) {
                pauseCtx->equipAnimY -= offsetY;
            } else {
                pauseCtx->equipAnimY += offsetY;
            }
        }

        sEquipMoveTimer--;

        if (sEquipMoveTimer == 0) {
            if (sEquipState == 1) {
                sEquipState++;
                D_8082A488 = 4;
                return;
            }

            osSyncPrintf("\n＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝\n");

            // Skipping the arrow animation: need to change the item's type and
            // slot when it hits the button since it didn't get set earlier
            if (pauseCtx->equipTargetItem == ITEM_ARROW_FIRE || pauseCtx->equipTargetItem == ITEM_ARROW_ICE ||
                pauseCtx->equipTargetItem == ITEM_ARROW_LIGHT) {
                switch (pauseCtx->equipTargetItem) {
                    case ITEM_ARROW_FIRE:
                        pauseCtx->equipTargetItem = ITEM_BOW_ARROW_FIRE;
                        break;
                    case ITEM_ARROW_ICE:
                        pauseCtx->equipTargetItem = ITEM_BOW_ARROW_ICE;
                        break;
                    case ITEM_ARROW_LIGHT:
                        pauseCtx->equipTargetItem = ITEM_BOW_ARROW_LIGHT;
                        break;
                }
                if (!CVarGetInteger(CVAR_ENHANCEMENT("SeparateArrows"), 0)) {
                    pauseCtx->equipTargetSlot = SLOT_BOW;
                }
            }

            // If the item is on another button already, swap the two
            uint16_t targetButtonIndex = pauseCtx->equipTargetCBtn + 1;
            for (uint16_t otherSlotIndex = 0; otherSlotIndex < ARRAY_COUNT(gSaveContext.equips.cButtonSlots);
                 otherSlotIndex++) {
                uint16_t otherButtonIndex = otherSlotIndex + 1;
                if (otherSlotIndex == pauseCtx->equipTargetCBtn) {
                    continue;
                }

                if (pauseCtx->equipTargetSlot == gSaveContext.equips.cButtonSlots[otherSlotIndex]) {
                    // Assign the other button to the target's current item
                    if (gSaveContext.equips.buttonItems[targetButtonIndex] != ITEM_NONE) {
                        gSaveContext.equips.buttonItems[otherButtonIndex] =
                            gSaveContext.equips.buttonItems[targetButtonIndex];
                        gSaveContext.equips.cButtonSlots[otherSlotIndex] =
                            gSaveContext.equips.cButtonSlots[pauseCtx->equipTargetCBtn];
                        Interface_LoadItemIcon2(play, otherButtonIndex);
                    } else {
                        gSaveContext.equips.buttonItems[otherButtonIndex] = ITEM_NONE;
                        gSaveContext.equips.cButtonSlots[otherSlotIndex] = SLOT_NONE;
                    }
                    // break; // 'Assume there is only one possible pre-existing equip'
                }

                // Fix for Equip Dupe
                if (pauseCtx->equipTargetItem == ITEM_BOW) {
                    if (gSaveContext.equips.buttonItems[otherButtonIndex] >= ITEM_BOW_ARROW_FIRE &&
                        gSaveContext.equips.buttonItems[otherButtonIndex] <= ITEM_BOW_ARROW_LIGHT &&
                        !CVarGetInteger(CVAR_ENHANCEMENT("SeparateArrows"), 0)) {
                        gSaveContext.equips.buttonItems[otherButtonIndex] =
                            gSaveContext.equips.buttonItems[targetButtonIndex];
                        gSaveContext.equips.cButtonSlots[otherSlotIndex] =
                            gSaveContext.equips.cButtonSlots[pauseCtx->equipTargetCBtn];
                        Interface_LoadItemIcon2(play, otherButtonIndex);
                    }
                }
            }

            gSaveContext.equips.buttonItems[targetButtonIndex] = pauseCtx->equipTargetItem;
            gSaveContext.equips.cButtonSlots[pauseCtx->equipTargetCBtn] = pauseCtx->equipTargetSlot;
            Interface_LoadItemIcon1(play, targetButtonIndex);

            pauseCtx->unk_1E4 = 0;
            sEquipMoveTimer = 10;
            WREG(90) = 320;
            WREG(87) = WREG(91);
        }
    } else {
        sEquipAnimTimer--;
        if (sEquipAnimTimer == 0) {
            pauseCtx->equipAnimAlpha = 255;
        }
    }
}
