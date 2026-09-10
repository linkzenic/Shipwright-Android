#include "mods/nei_aligned_ui_paths.h"
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
#include "mods/ext_buttons/ext_buttons.h" // Skijer's NEI — C-equipping u16 EXT items
#include "mods/nei_save.h"                // Skijer's NEI
#include "mods/transformation_masks/transformation_masks.h"
#include "mods/transformation_masks/custom_forms.h" // Skijer's NEI — Rito Mask shares the Farore's Wind cell
#include "mods/extended_inventory.c"
#include "mods/items/custom_items.h"
#include "mods/items/custom_bottles.h"         // Skijer's NEI — bottle randomizer wheels (A/B)
#include "mods/items/logic/item_lantern.h"     // LanternFireType enum (Vacía/Regular/Blue/Poe/Green)
#include "mods/items/logic/twilight_upgrade.h" // Clawshot / Gale Boomerang mode selectors
#include "expansions/sw97/sw97_config.h"

u8 gAmmoItems[] = {
    ITEM_STICK,   ITEM_NUT,  ITEM_BOMB, ITEM_BOW,  ITEM_NONE, ITEM_NONE, ITEM_SLINGSHOT, ITEM_NONE,
    ITEM_BOMBCHU, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_BEAN,      ITEM_NONE,
};

static s16 sEquipState = 0;
static s16 sEquipAnimTimer = 0;
static s16 sEquipMoveTimer = 10;

// Ammo-quad vertex offset per slot in the full 24-slot ammo layout (2 per slot). The old compact
// 8-slot table (sAmmoVtxOffset) is retired — the layout is always the full set now. Skijer's NEI
static s16 sAllAmmoVtxOffset[] = {
    0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46,
};

extern const char* _gAmmoDigit0Tex[];

s8 ItemInSlotUsesAmmo(s16 slot) {
    s16 item = ExtInv_GetSlotItem(slot); // Skijer's NEI
    // Bottomless Bottle: ALWAYS show a use-counter while owned — the number (0 when empty, else the
    // remaining uses) is what identifies it as the Bottomless Bottle. Skijer's NEI
    if (slot == SLOT_BOTTLE_4 && Bottle_BottomlessOwned()) {
        return 1;
    }
    return item == ITEM_STICK || item == ITEM_NUT || item == ITEM_BOMB || item == ITEM_BOW || item == ITEM_SLINGSHOT ||
           item == ITEM_BOMBCHU || item == ITEM_BEAN;
}

void KaleidoScope_DrawAmmoCount(PauseContext* pauseCtx, GraphicsContext* gfxCtx, s16 item, int slot) {
    if (!GameInteractor_Should(VB_DRAW_AMMO_COUNT, true, &item)) {
        return;
    }

    s16 ammo;
    s16 i;

    // The ammo vertex layout is now ALWAYS the full 24-slot table (see KaleidoScope_InitVertices), so
    // use the all-slots offsets directly — this also covers the Bottomless Bottle (SLOT_BOTTLE_4)
    // counter regardless of BetterAmmoRendering. Skijer's NEI
    s16 ammoVtx = sAllAmmoVtxOffset[slot];

    OPEN_DISPS(gfxCtx);

    ammo = AMMO(item);

    // Skijer's NEI — shared-slot counters show their OWN count, not the underlying vanilla item's ammo.
    {
        extern unsigned char PowerKeg_IsOwned(void);
        extern unsigned char PowerKeg_IsOnBombActive(void);
        extern unsigned char PowerKeg_GetCount(void);
        extern unsigned char Picto_IsOwned(void);
        extern unsigned char Picto_IsOnLensActive(void);
        if ((item == ITEM_BOMB) && PowerKeg_IsOwned() && PowerKeg_IsOnBombActive()) {
            ammo = PowerKeg_GetCount(); // Power Keg (Bomb slot): its own keg count
        } else if ((item == ITEM_LENS) && Picto_IsOwned() && Picto_IsOnLensActive()) {
            ammo = Nei_Save()->pictoHasPhoto ? 1 : 0; // Pictograph Box (Lens slot): 1 with a photo, else 0
        } else if (slot == SLOT_BOTTLE_4 && Bottle_BottomlessOwned()) {
            ammo = Bottle_BottomlessCount(); // Bottomless Bottle (SLOT_BOTTLE_4): its use counter (0 = empty)
        }
    }

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
        gSPVertex(POLY_OPA_DISP++, &pauseCtx->itemVtx[(ammoVtx + 31) * 4], 4, 0);

        gDPLoadTextureBlock(POLY_OPA_DISP++, ((u8*)_gAmmoDigit0Tex[i]), G_IM_FMT_IA, G_IM_SIZ_8b, 8, 8, 0,
                            G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                            G_TX_NOLOD);

        gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);
    }

    gSPVertex(POLY_OPA_DISP++, &pauseCtx->itemVtx[(ammoVtx + 32) * 4], 4, 0);

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

// Copy the 4 vertices of a slot quad and remap their texture coords to span a
// sizePx×sizePx texture (tc in s10.5 = sizePx<<5). Returns the temp Vtx (Graph_Alloc'd). Skijer's NEI
static Vtx* KaleidoScope_AllocRemappedQuad(GraphicsContext* gfxCtx, Vtx* srcQuad, s32 sizePx) {
    Vtx* v = (Vtx*)Graph_Alloc(gfxCtx, 4 * sizeof(Vtx));
    for (s32 i = 0; i < 4; i++) {
        v[i] = srcQuad[i];
    }
    v[0].v.tc[0] = 0;
    v[0].v.tc[1] = 0;
    v[1].v.tc[0] = sizePx << 5;
    v[1].v.tc[1] = 0;
    v[2].v.tc[0] = 0;
    v[2].v.tc[1] = sizePx << 5;
    v[3].v.tc[0] = sizePx << 5;
    v[3].v.tc[1] = sizePx << 5;
    return v;
}

// Renders a left and/or right item for any item slot that can support cycling. `forceShow` (used by
// the bottle wheel) shows the previews + A indicator even when prev/next share the slot's value —
// needed because multiple EMPTY bottles all read as ITEM_BOTTLE but are distinct slots. Skijer's NEI
static void KaleidoScope_DrawItemCycleExtrasImpl(PlayState* play, u8 slot, u8 canCycle, u8 leftItem, u8 rightItem,
                                                 u8 forceShow) {
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

    u16 slotItem = ExtInv_GetSlotItem(slot); // Skijer's NEI
    u8 showLeftItem = leftItem != ITEM_NONE && (forceShow || slotItem != leftItem);
    u8 showRightItem = rightItem != ITEM_NONE && (forceShow || (slotItem != rightItem && leftItem != rightItem));

    // Render the extra cycle items if at least the left or right item are valid.
    //
    // Candidates belong to the cell you are POINTING AT, not to every cell that happens to own a
    // wheel. Without this gate each wheel painted its two minis permanently: resting 15px below the
    // cell centre they landed on the top edge of the cell underneath, and the bottom row pushed them
    // outside the page frame. The A-button hint below was already hover-gated; the candidates were
    // not. Keeping the timer in the condition lets a cell you just left finish closing instead of
    // popping. Mirrors the same fix in 2Ship so the two inventories read identically. Skijer's NEI
    u8 hovered = (pauseCtx->cursorSlot[PAUSE_ITEM] == slot) && (pauseCtx->cursorSpecialPos == 0);

    if (canCycle && slotItem != ITEM_NONE && (showLeftItem || showRightItem) &&
        (hovered || isCycling || sSlotCycleActiveAnimTimer[slot] > 0)) {
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

// Public entry (value-based show gates) — trade/mask/etc. cyclers use this.
void KaleidoScope_DrawItemCycleExtras(PlayState* play, u8 slot, u8 canCycle, u8 leftItem, u8 rightItem) {
    KaleidoScope_DrawItemCycleExtrasImpl(play, slot, canCycle, leftItem, rightItem, false);
}

void KaleidoScope_HandleItemCycleExtras(PlayState* play, u8 slot, bool canCycle, u8 leftItem, u8 rightItem,
                                        bool replaceCButtons) {
    Input* input = &play->state.input[0];
    PauseContext* pauseCtx = &play->pauseCtx;
    bool dpad = (CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0) && !CHECK_BTN_ALL(input->cur.button, BTN_CUP));
    u16 slotItem = ExtInv_GetSlotItem(slot); // Skijer's NEI
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
                    if (gSaveContext.equips.buttonItems[i] == ExtInv_GetSlotItem(slot)) { // Skijer's NEI
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
            ExtInv_SetSlotItem(slot, rightItem); // Skijer's NEI
        } else if ((pauseCtx->stickRelX < -30 || pauseCtx->stickRelY < -30) ||
                   dpad && CHECK_BTN_ANY(input->press.button, BTN_DLEFT | BTN_DDOWN)) {
            Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            if (replaceCButtons) {
                for (int i = 1; i < ARRAY_COUNT(gSaveContext.equips.buttonItems); i++) {
                    if (gSaveContext.equips.buttonItems[i] == ExtInv_GetSlotItem(slot)) { // Skijer's NEI
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
            ExtInv_SetSlotItem(slot, leftItem); // Skijer's NEI
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
// plus a "Vacía" / extinguish slot. A toggles the wheel; while open, stick L/R
// cycles. Replaces the old hold-C-to-extinguish shortcut.
#define LANTERN_SELECTOR_MAX 5

static u8 sLanternSelectorActive = 0;

// Tint colors per LanternFireType — used by the overlay draw to indicate which
// fuel is in each slot without needing dedicated icons.
//   0 NONE: dim gray (extinguished)
//   1 REGULAR: orange
//   2 BLUE: cyan
//   3 POE: magenta
//   4 GREEN: green
static const u8 sLanternTypeTint[5][3] = {
    { 110, 110, 110 }, { 255, 140, 40 }, { 60, 180, 255 }, { 220, 80, 220 }, { 80, 230, 100 },
};

// Forward declarations — defined below.
// Used by Lantern / Gust Jar / Arrow Wheel press-A handlers.
// leftSize / rightSize: texture native size in pixels (32 for item icons,
// 24 for quest medallion icons). Mod authors pass the actual size of the
// PNG they're displaying so UVs scale correctly inside the 32x32 quad.
static void KaleidoCycle_DrawRocStyle(PlayState* play, s32 visualSlot, u8 isCycling, u8 hasLeftItem, u8 hasRightItem,
                                      void* leftIconTex, void* rightIconTex, const u8* leftTint, const u8* rightTint,
                                      s32 leftSize, s32 rightSize);
static void ArrowWheel_Build(void);

static u8 Lantern_BuildSelectorEntries(u8 entries[LANTERN_SELECTOR_MAX]) {
    u8 count = 0;
    // "Vacía" / extinguish is always selectable
    entries[count++] = LANTERN_FIRE_NONE;
    for (u8 t = LANTERN_FIRE_REGULAR; t <= LANTERN_FIRE_GREEN; t++) {
        if (Nei_Save()->lanternCapturedTypes & (1 << t)) { // Skijer's NEI
            entries[count++] = t;
        }
    }
    return count;
}

// ── Reusable press-A "wheel" selector ───────────────────────────────────────
// Shared input handler for the kaleido cycle-wheels (Lantern, Clawshot, Gale,
// Gust Jar, Arrow Wheel — and future ones). A toggles the wheel open/closed;
// while open, stick/D-pad L or R calls onCycle(dir) to apply the change.
//   onThisItem : cursor is on this wheel's item AND any ownership gate passed.
//   canToggle  : whether A may open/close (multi-value wheels pass count>1).
//   active     : the wheel's persistent open/closed flag.
//   onCycle    : applies the change for dir (+1 next / -1 prev) + plays cursor SFX.
// Skijer's NEI
static void KaleidoWheel_Run(PlayState* play, u8 onThisItem, u8 canToggle, u8* active,
                             void (*onCycle)(PlayState* play, s32 dir)) {
    PauseContext* pauseCtx = &play->pauseCtx;
    Input* input = &play->state.input[0];

    if (!onThisItem) {
        if (*active) {
            *active = 0;
            gCurrentItemCyclingSlot = -1;
        }
        return;
    }

    bool dpad = (CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0) && !CHECK_BTN_ALL(input->cur.button, BTN_CUP));

    if (CHECK_BTN_ALL(input->press.button, BTN_A) && canToggle) {
        *active = !*active;
        gCurrentItemCyclingSlot = *active ? pauseCtx->cursorSlot[PAUSE_ITEM] : -1;
        Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    if (!*active) {
        return;
    }
    pauseCtx->cursorColorSet = 8;
    gCurrentItemCyclingSlot = pauseCtx->cursorSlot[PAUSE_ITEM];

    if ((pauseCtx->stickRelX > 30 || pauseCtx->stickRelY > 30) ||
        (dpad && CHECK_BTN_ANY(input->press.button, BTN_DRIGHT | BTN_DUP))) {
        onCycle(play, +1);
    } else if ((pauseCtx->stickRelX < -30 || pauseCtx->stickRelY < -30) ||
               (dpad && CHECK_BTN_ANY(input->press.button, BTN_DLEFT | BTN_DDOWN))) {
        onCycle(play, -1);
    }
}

static void Lantern_Cycle(PlayState* play, s32 dir) {
    u8 entries[LANTERN_SELECTOR_MAX];
    u8 count = Lantern_BuildSelectorEntries(entries);
    for (u8 i = 0; i < count; i++) {
        if (entries[i] == gCustomItemState.lanternFireType) {
            u8 next = (dir > 0) ? (i + 1) % count : (i + count - 1) % count;
            gCustomItemState.lanternFireType = entries[next];
            Nei_Save()->lanternFireType = gCustomItemState.lanternFireType; // Skijer's NEI
            Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            break;
        }
    }
}

// ── Dual Cane: which cane the shared cell shows ─────────────────────────────
// Cane of Somaria and Cane of Pacci are ONE inventory cell (the player is nearly
// out of item slots), so which one you are carrying is a context variable rather
// than a second slot: NeiSaveData.caneType, flipped from here.
//
// This is the Lantern's pattern verbatim — press A on the cell, stick L/R picks,
// A confirms — because KaleidoWheel_Run already implements that input and
// KaleidoCycle_DrawRocStyle already draws it. Nothing new is needed.
//
// The toggle only appears once BOTH chains are owned: the two progressions are
// independent and one never grants the other, so a player who only ever found
// Somaria items has nothing to switch to.
static u8 sCaneSelectorActive = 0;

// C-side accessors (mods/items/logic/item_cane_of_somaria.c + mods/nei_save.cpp).
u8 Cane_GetType(void);
u8 Nei_CaneTypeOwned(u8 type);
void Nei_CaneSetType(u8 type);
u8 Nei_CaneTypeCount(void);
u8 Nei_CaneNextType(s8 dir);

// FOUR entries can live on this cell — Cane of Somaria, Trirod, Cane of Pacci and
// Ultrahand — because finishing a chain ADDS its end-item to the wheel instead of
// replacing the cane that led there. The wheel is worth opening from two onward.
static u8 Cane_WheelHasChoice(void) {
    return Nei_CaneTypeCount() > 1;
}

static void Cane_KaleidoCycle(PlayState* play, s32 dir) {
    // Walk to the next OWNED entry in that direction; locked ones are skipped.
    Nei_CaneSetType(Nei_CaneNextType((s8)((dir >= 0) ? 1 : -1)));
    Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

static void Cane_HandleKaleidoSelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    u8 onThisItem = (pauseCtx->cursorItem[PAUSE_ITEM] == ITEM_CANE_OF_SOMARIA);

    KaleidoWheel_Run(play, onThisItem, Cane_WheelHasChoice(), &sCaneSelectorActive, Cane_KaleidoCycle);
}

// The cell always holds ITEM_CANE_OF_SOMARIA, so both sides of the wheel show that
// same icon; the TINT is what says which cane you are about to carry, matching the
// in-hand model (Somaria red, Pacci yellow).
static const Color_RGB8 sCaneTint[4] = {
    { 255, 60, 60 },   // Cane of Somaria — red
    { 255, 140, 40 },  // Trirod
    { 255, 215, 70 },  // Cane of Pacci — yellow
    { 120, 200, 255 }, // Ultrahand
};

static void Cane_DrawKaleidoSelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;

    if (pauseCtx->cursorItem[PAUSE_ITEM] != ITEM_CANE_OF_SOMARIA) {
        KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], 0, 0, 0, NULL, NULL, NULL, NULL, 0, 0);
        return;
    }
    if (!Cane_WheelHasChoice()) {
        return;
    }

    void* caneTex = ExtInv_GetItemIcon(ITEM_CANE_OF_SOMARIA);
    if (caneTex == NULL) {
        return;
    }

    // Each side previews the entry that direction would land on, so the tint tells you
    // what you are about to switch to rather than what you already have.
    const Color_RGB8* prevTint = &sCaneTint[Nei_CaneNextType(-1)];
    const Color_RGB8* nextTint = &sCaneTint[Nei_CaneNextType(1)];

    KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], sCaneSelectorActive,
                              /*hasLeftItem=*/1, /*hasRightItem=*/1, caneTex, caneTex, prevTint, nextTint,
                              /*leftSize=*/32, /*rightSize=*/32);
}

static void Lantern_HandleKaleidoSelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;

    u8 onThisItem = (pauseCtx->cursorItem[PAUSE_ITEM] == ITEM_LANTERN);
    u8 entries[LANTERN_SELECTOR_MAX];
    u8 count = Lantern_BuildSelectorEntries(entries);

    KaleidoWheel_Run(play, onThisItem, count > 1, &sLanternSelectorActive, Lantern_Cycle);
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
                              /*hasLeftItem=*/1, /*hasRightItem=*/1, lanternTex, lanternTex, sLanternTypeTint[prevType],
                              sLanternTypeTint[nextType],
                              /*leftSize=*/32, /*rightSize=*/32);
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
static void KaleidoCycle_DrawRocStyle(PlayState* play, s32 visualSlot, u8 isCycling, u8 hasLeftItem, u8 hasRightItem,
                                      void* leftIconTex, void* rightIconTex, const u8* leftTint, const u8* rightTint,
                                      s32 leftSize, s32 rightSize) {
    if (visualSlot < 0 || visualSlot >= (s32)ARRAY_COUNT(sSlotCycleActiveAnimTimer))
        return;
    if (leftSize <= 0)
        leftSize = 32;
    if (rightSize <= 0)
        rightSize = 32;
    PauseContext* pauseCtx = &play->pauseCtx;

    OPEN_DISPS(play->state.gfxCtx);

    if (isCycling) {
        if (sSlotCycleActiveAnimTimer[visualSlot] < 5)
            sSlotCycleActiveAnimTimer[visualSlot]++;
    } else {
        if (sSlotCycleActiveAnimTimer[visualSlot] > 0)
            sSlotCycleActiveAnimTimer[visualSlot]--;
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
            if (hasLeftItem)
                gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);
            if (hasRightItem)
                gSP1Quadrangle(POLY_OPA_DISP++, 4, 6, 7, 5, 0);
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

// ── MM trade items in the existing adult-trade wheel (Skijer's NEI) ──────────
// The adult-trade slot already has the shared linear cycle wheel (KaleidoScope_HandleItemCycleExtras).
// We just feed it owned-trade-item prev/next so that same wheel cycles the MM items too. Data lives in
// trade_items.c.
extern s32 TradeAdult_OwnedCount(void);
extern s32 TradeAdult_OwnedAt(s32 ordinal);
extern u8 TradeAdult_ItemId(s32 index);
extern u8 TradeAdult_PrevItem(u8 cur);
extern u8 TradeAdult_NextItem(u8 cur);
extern void TradeAdult_FoldCurrent(u8 item);

#if 0  // Skijer's NEI — old custom 2D-grid handler/draw, replaced by feeding the existing linear wheel.
static void KaleidoTradeGrid_Handle(PlayState* play) {
    // Vanilla adult-trade shuffle keeps the linear cycle; the owned-grid is the non-rando path.
    if (IS_RANDO && Randomizer_GetSettingValue(RSK_SHUFFLE_ADULT_TRADE)) {
        return;
    }
    PauseContext* pauseCtx = &play->pauseCtx;
    Input* input = &play->state.input[0];
    s32 owned = TradeAdult_OwnedCount();

    // Make owned trade items visible: fill an empty adult-trade slot with the first owned one (like the
    // bottle wheel). Granting only sets the tradeAdultOwned bit, so without this the slot stays ITEM_NONE
    // and nothing shows in the slot or the wheel. Skijer's NEI
    if (owned > 0 && ExtInv_GetSlotItem(SLOT_TRADE_ADULT) == ITEM_NONE) {
        s32 first = TradeAdult_OwnedAt(0);
        if (first >= 0) {
            ExtInv_SetSlotItem(SLOT_TRADE_ADULT, TradeAdult_ItemId(first));
        }
    }

    // Detect "cursor on the adult-trade slot" the way the working press-A selectors (lantern/gust jar)
    // do: by the item under the cursor — the slot holds a trade item after auto-populate — with the raw
    // slot index as a fallback. (The old cursorSlot + cursorSpecialPos==0 check never matched, so A did
    // nothing.) Skijer's NEI
    u8 curItem = (u8)pauseCtx->cursorItem[PAUSE_ITEM];
    bool onSlot = (pauseCtx->cursorSlot[PAUSE_ITEM] == SLOT_TRADE_ADULT) || (TradeAdult_IndexOfItem(curItem) >= 0);

    if (!onSlot) {
        if (sTradeGridActive) {
            sTradeGridActive = 0;
            gCurrentItemCyclingSlot = -1;
        }
        return;
    }

    bool dpad = (CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0) && !CHECK_BTN_ALL(input->cur.button, BTN_CUP));

    if (!sTradeGridActive) {
        if (CHECK_BTN_ALL(input->press.button, BTN_A) && owned > 0) {
            sTradeGridActive = 1;
            gCurrentItemCyclingSlot = pauseCtx->cursorSlot[PAUSE_ITEM];
            s32 ord = TradeAdult_OrdinalOf(ExtInv_GetSlotItem(SLOT_TRADE_ADULT));
            sTradeGridCursor = (ord >= 0) ? ord : 0;
            sTradeGridStickReady = 0;
            Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
        return;
    }

    // Grid is open.
    pauseCtx->cursorColorSet = 8;
    gCurrentItemCyclingSlot = SLOT_TRADE_ADULT;

    if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
        sTradeGridActive = 0;
        gCurrentItemCyclingSlot = -1;
        Audio_PlaySoundGeneral(NA_SE_SY_CANCEL, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        s32 gi = TradeAdult_OwnedAt(sTradeGridCursor);
        if (gi >= 0) {
            u8 newItem = TradeAdult_ItemId(gi);
            u16 oldItem = ExtInv_GetSlotItem(SLOT_TRADE_ADULT);
            for (int i = 1; i < ARRAY_COUNT(gSaveContext.equips.buttonItems); i++) {
                if (gSaveContext.equips.buttonItems[i] == oldItem) {
                    gSaveContext.equips.buttonItems[i] = newItem;
                    Interface_LoadItemIcon1(play, i);
                    break;
                }
            }
            ExtInv_SetSlotItem(SLOT_TRADE_ADULT, newItem); // Skijer's NEI
        }
        sTradeGridActive = 0;
        gCurrentItemCyclingSlot = -1;
        Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    // One move per flick: re-arm when the stick returns near neutral.
    s32 sx = pauseCtx->stickRelX;
    s32 sy = pauseCtx->stickRelY;
    if (sx < 15 && sx > -15 && sy < 15 && sy > -15) {
        sTradeGridStickReady = 1;
    }
    if (sTradeGridStickReady) {
        s32 cur = sTradeGridCursor;
        if (sx > 30 || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DRIGHT))) {
            cur += 1;
        } else if (sx < -30 || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DLEFT))) {
            cur -= 1;
        } else if (sy < -30 || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DDOWN))) {
            cur += TRADE_GRID_COLS; // stick down -> next row
        } else if (sy > 30 || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DUP))) {
            cur -= TRADE_GRID_COLS; // stick up -> previous row
        }
        if (cur != sTradeGridCursor && cur >= 0 && cur < owned) {
            sTradeGridCursor = cur;
            sTradeGridStickReady = 0;
            Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
    }
}

static void KaleidoTradeGrid_Draw(PlayState* play) {
    if (!sTradeGridActive) {
        return;
    }
    PauseContext* pauseCtx = &play->pauseCtx;
    s32 owned = TradeAdult_OwnedCount();
    if (owned <= 0) {
        return;
    }
    s32 cols = TRADE_GRID_COLS;

    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Push();

    Vtx* tl = &pauseCtx->itemVtx[SLOT_TRADE_ADULT * 4];
    Vtx* br = &tl[3];
    s16 halfX = (br->v.ob[0] - tl->v.ob[0]) / 2;
    s16 halfY = (br->v.ob[1] - tl->v.ob[1]) / 2;
    f32 cx = (f32)(tl->v.ob[0] + halfX);
    f32 cy = (f32)(tl->v.ob[1] + halfY);
    f32 cell = 2.4f * (f32)halfX; // halfX > 0; first-pass spacing (tune visually)

    for (s32 ord = 0; ord < owned; ord++) {
        s32 row = ord / cols;
        s32 col = ord % cols;
        f32 dx = ((f32)col - (f32)(cols - 1) * 0.5f) * cell;
        f32 dy = -(f32)(row + 1) * cell; // grid drops below the slot

        Matrix_Push();
        Matrix_Translate(cx + dx, cy + dy, 0, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

        // Highlight ring behind the selected cell.
        if (ord == sTradeGridCursor) {
            gSPVertex(POLY_OPA_DISP++, sCycleCircleVtx, 8, 0);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 230, 80, pauseCtx->alpha);
            gDPLoadTextureBlock_4b(POLY_OPA_DISP++, gPausePromptCursorTex, G_IM_FMT_I, 48, 48, 0,
                                   G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                   G_TX_NOLOD, G_TX_NOLOD);
            gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);
        }

        s32 gi = TradeAdult_OwnedAt(ord);
        void* icon = (gi >= 0) ? ExtInv_GetItemIcon(TradeAdult_ItemId(gi)) : NULL;
        if (icon != NULL) {
            gSPVertex(POLY_OPA_DISP++, sCycleExtraItemVtx, 8, 0);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
            KaleidoScope_DrawQuadTextureRGBA32(play->state.gfxCtx, icon, 32, 32, 0);
        }
        Matrix_Pop();
    }

    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}
#endif // Skijer's NEI — end of disabled 2D-grid code

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
static u8 sClawshotSelectorActive = 0;
static s32 sClawshotAnimTimer = 0;
// Gale Boomerang selector state
static u8 sGaleSelectorActive = 0;
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
// Two-position toggle: stick/D-pad in either direction flips vanilla ↔ clawshot.
static void Clawshot_Cycle(PlayState* play, s32 dir) {
    TwilightUpgrade_SetClawshotActive(TwilightUpgrade_IsClawshotActive() ? 0 : 1);
    Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

static void Clawshot_HandleKaleidoSelector(PlayState* play) {
    s32 cursorItem = play->pauseCtx.cursorItem[PAUSE_ITEM];
    u8 onThisItem = TwilightSel_IsHookshotItem(cursorItem) && TwilightUpgrade_HasClawshot();
    KaleidoWheel_Run(play, onThisItem, 1, &sClawshotSelectorActive, Clawshot_Cycle);
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
        if (sClawshotAnimTimer > 0)
            sClawshotAnimTimer--;
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
    if (vanillaTex == NULL || clawshotTex == NULL)
        return;

    static const u8 sVanillaTint[3] = { 255, 255, 255 };
    static const u8 sClawshotTint[3] = { 255, 255, 255 };

    KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], sClawshotSelectorActive,
                              /*hasLeft=*/1, /*hasRight=*/1, vanillaTex, clawshotTex, sVanillaTint, sClawshotTint,
                              /*leftSize=*/32, /*rightSize=*/32);
}

// ── Gale Boomerang Mode Selector (Boomerang icon) ───────────────────────────
// Two-position toggle: stick/D-pad in either direction flips vanilla ↔ gale.
static void Gale_Cycle(PlayState* play, s32 dir) {
    TwilightUpgrade_SetGaleBoomerangActive(TwilightUpgrade_IsGaleBoomerangActive() ? 0 : 1);
    Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

static void Gale_HandleKaleidoSelector(PlayState* play) {
    s32 cursorItem = play->pauseCtx.cursorItem[PAUSE_ITEM];
    u8 onThisItem = (cursorItem == ITEM_BOOMERANG) && TwilightUpgrade_HasGaleBoomerang();
    KaleidoWheel_Run(play, onThisItem, 1, &sGaleSelectorActive, Gale_Cycle);
}

// Same small-flip pattern as Clawshot above — left side shows the vanilla
// boomerang icon (taken directly from gItemIcons to bypass ExtInv's gale
// override), right side shows the gale boomerang icon texture.
static void Gale_DrawKaleidoSelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_BOOMERANG || !TwilightUpgrade_HasGaleBoomerang()) {
        if (sGaleAnimTimer > 0)
            sGaleAnimTimer--;
        return;
    }

    extern void* gItemIcons[];
    void* vanillaTex = gItemIcons[ITEM_BOOMERANG];
    void* galeTex = (void*)gItemIconGaleBoomerangTex;
    if (vanillaTex == NULL || galeTex == NULL)
        return;

    static const u8 sVanillaTint[3] = { 255, 255, 255 };
    static const u8 sGaleTint[3] = { 255, 255, 255 };

    KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], sGaleSelectorActive,
                              /*hasLeft=*/1, /*hasRight=*/1, vanillaTex, galeTex, sVanillaTint, sGaleTint,
                              /*leftSize=*/32, /*rightSize=*/32);
}

// ── Shovel <-> Dominion Rod selector (shared cell 46) — 2026-08-06 re-layout ─────────────────────
// The rod rides the shovel's cell as a wheel entry (its old cell 47 is the Rod of Seasons now).
// Ownership of each is its own NeiSaveData flag — a shared cell's value cannot say "both owned".
// Same A-toggle pattern as the Clawshot/Gale selectors; the cycle swaps the CELL item, and the
// icon/name follow the slot value on their own. Mirrors the MM wheel exactly. Skijer's NEI
static u8 sShovelSelectorActive = 0;

// Fold a pre-re-layout save into the new cell assignment. Idempotent, cheap; runs from the handle
// pass. 47 held the rod -> flag + clear; 46 backfills its flag; 44 pokeball -> flag + clear;
// 41 hylia -> clear (item retired).
static void Page2Relayout_Heal(void) {
    NeiSaveData* nei = Nei_Save();

    if (ExtInv_GetSlotItem(SLOT_ROD_OF_SEASONS) == ITEM_DOMINION_ROD) {
        nei->dominionOwned = 1;
        ExtInv_SetSlotItem(SLOT_ROD_OF_SEASONS, ITEM_NONE);
        if (ExtInv_GetSlotItem(SLOT_SHOVEL) == ITEM_NONE) {
            ExtInv_SetSlotItem(SLOT_SHOVEL, ITEM_DOMINION_ROD);
        }
    }
    if (ExtInv_GetSlotItem(SLOT_SHOVEL) == ITEM_SHOVEL) {
        nei->shovelOwned = 1;
    } else if (ExtInv_GetSlotItem(SLOT_SHOVEL) == ITEM_DOMINION_ROD) {
        nei->dominionOwned = 1;
    }
    if (ExtInv_GetSlotItem(SLOT_SHADOW_CRYSTAL) == ITEM_POKEBALL) {
        nei->pokeballOwned = 1;
        ExtInv_SetSlotItem(SLOT_SHADOW_CRYSTAL, ITEM_NONE);
    }
    if (ExtInv_GetSlotItem(SLOT_PHANTOM_HOURGLASS) == ITEM_HYLIAS_GRACE) {
        ExtInv_SetSlotItem(SLOT_PHANTOM_HOURGLASS, ITEM_NONE);
    }
}

static u8 ShovelSel_IsShovelCellItem(s32 item) {
    return item == ITEM_SHOVEL || item == ITEM_DOMINION_ROD;
}

static void Shovel_Cycle(PlayState* play, s32 dir) {
    u16 cur = ExtInv_GetSlotItem(SLOT_SHOVEL);
    u16 next = (cur == ITEM_SHOVEL) ? ITEM_DOMINION_ROD : ITEM_SHOVEL;
    ExtInv_SetSlotItem(SLOT_SHOVEL, next);
    Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

static void Shovel_HandleKaleidoSelector(PlayState* play) {
    NeiSaveData* nei;
    s32 cursorItem;
    u8 onThisItem;

    Page2Relayout_Heal();

    nei = Nei_Save();
    cursorItem = play->pauseCtx.cursorItem[PAUSE_ITEM];
    onThisItem = ShovelSel_IsShovelCellItem(cursorItem) && nei->shovelOwned && nei->dominionOwned;
    KaleidoWheel_Run(play, onThisItem, 1, &sShovelSelectorActive, Shovel_Cycle);
}

// Left side: the shovel; right side: the rod. Same small-flip renderer as Clawshot/Gale.
static void Shovel_DrawKaleidoSelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    NeiSaveData* nei = Nei_Save();

    if (!ShovelSel_IsShovelCellItem(cursorItem) || !nei->shovelOwned || !nei->dominionOwned) {
        return;
    }

    static const u8 sShovelTint[3] = { 255, 255, 255 };
    static const u8 sRodTint[3] = { 255, 255, 255 };

    KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], sShovelSelectorActive,
                              /*hasLeft=*/1, /*hasRight=*/1, (void*)gItemIconShovelTex, (void*)gItemIconDominionRodTex,
                              sShovelTint, sRodTint,
                              /*leftSize=*/32, /*rightSize=*/32);
}

// ── Pictograph Box selector (on the Lens of Truth slot) ─────────────────────
// A on the Lens of Truth (when the pictobox is owned) flips between the Lens and
// the Pictograph Box. Selecting the pictobox puts the Lens slot in "pictobox
// mode": its equipped C-button then enters the photo viewfinder. Skijer's NEI
extern unsigned char Picto_IsOwned(void);
extern unsigned char Picto_IsOnLensActive(void);
extern void Picto_SetOnLensActive(unsigned char on);
extern void* MmAssets_LoadResource(const char* path);

static u8 sPictoSelectorActive = 0;

// Flip toggled by the wheel cycle: Lens of Truth <-> Pictograph Box on the Lens slot.
static void Picto_Cycle(PlayState* play, s32 dir) {
    Picto_SetOnLensActive(Picto_IsOnLensActive() ? 0 : 1);
    Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

static void Picto_HandleKaleidoSelector(PlayState* play) {
    s32 cursorItem = play->pauseCtx.cursorItem[PAUSE_ITEM];
    u8 onThisItem = (cursorItem == ITEM_LENS) && Picto_IsOwned();
    // The flip only makes sense if you own BOTH the Lens and the pictobox. Without the real Lens the
    // slot is always the pictobox (Picto_IsOnLensActive forces it), so there's nothing to toggle.
    u8 haveRealLens = (gSaveContext.inventory.items[SLOT_LENS] == ITEM_LENS);
    // A on the Lens opens the flip wheel; stick L/R cycles Lens <-> Pictobox — same UX as the
    // Lantern/Clawshot/Gale selectors. The slot icon swaps via ExtInv_GetItemIcon for clear feedback.
    KaleidoWheel_Run(play, onThisItem, haveRealLens, &sPictoSelectorActive, Picto_Cycle);
}

// Hint overlay on the Lens slot: shows the Lens + Pictograph Box flip so the player knows A switches.
// The slot's main icon already reflects the current mode via ExtInv_GetItemIcon.
static void Picto_DrawKaleidoSelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_LENS || !Picto_IsOwned()) {
        return;
    }
    // Only show the Lens<->Pictobox flip when you own both; without the real Lens the slot is just the
    // pictobox (the ExtInv icon swap already shows it), so there's no alternative to flip to.
    if (gSaveContext.inventory.items[SLOT_LENS] != ITEM_LENS) {
        return;
    }

    extern void* gItemIcons[];
    void* lensTex = gItemIcons[ITEM_LENS];
    static const char* const sPictoIconPath = sNeiAligned_gItemIconPictographBoxTex_46;
    void* pictoTex = (void*)sPictoIconPath;
    if (lensTex == NULL) {
        return;
    }

    static const u8 sTint[3] = { 255, 255, 255 };
    KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], sPictoSelectorActive,
                              /*hasLeft=*/1, /*hasRight=*/1, lensTex, pictoTex, sTint, sTint,
                              /*leftSize=*/32, /*rightSize=*/32);
}

// ── Power Keg selector (on the Bomb slot) ───────────────────────────────────
// A on the Bomb slot (when the Power Keg is owned) flips between Bombs and the Power Keg. Selecting
// the keg puts the Bomb slot in "power keg mode": its equipped C-button then uses the keg in-game,
// gated by form + strength (FD/Goron, or Human/Gerudo with Silver Gauntlets+). Skijer's NEI
extern unsigned char PowerKeg_IsOwned(void);
extern unsigned char PowerKeg_IsOnBombActive(void);
extern void PowerKeg_SetOnBombActive(unsigned char on);

static u8 sPowerKegSelectorActive = 0;

static void PowerKeg_Cycle(PlayState* play, s32 dir) {
    PowerKeg_SetOnBombActive(PowerKeg_IsOnBombActive() ? 0 : 1);
    Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

static void PowerKeg_HandleKaleidoSelector(PlayState* play) {
    s32 cursorItem = play->pauseCtx.cursorItem[PAUSE_ITEM];
    u8 onThisItem = (cursorItem == ITEM_BOMB) && PowerKeg_IsOwned();
    KaleidoWheel_Run(play, onThisItem, 1, &sPowerKegSelectorActive, PowerKeg_Cycle);
}

static void PowerKeg_DrawKaleidoSelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_BOMB || !PowerKeg_IsOwned()) {
        return;
    }

    extern void* gItemIcons[];
    void* bombTex = gItemIcons[ITEM_BOMB];
    static const char* const sKegIconPath = sNeiAligned_gItemIconPowderKegTex_48;
    void* kegTex = (void*)sKegIconPath;
    if (bombTex == NULL) {
        return;
    }

    static const u8 sTint[3] = { 255, 255, 255 };
    KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], sPowerKegSelectorActive,
                              /*hasLeft=*/1, /*hasRight=*/1, bombTex, kegTex, sTint, sTint,
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

static void GustJar_Cycle(PlayState* play, s32 dir) {
    sGustElemCursor =
        (dir > 0) ? (sGustElemCursor + 1) % sGustAvailCount : (sGustElemCursor + sGustAvailCount - 1) % sGustAvailCount;
    gCustomItemState.gustJarElement = sGustAvailElems[sGustElemCursor];
    Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

static void GustJar_HandlePressASelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    u8 onThisItem = (pauseCtx->cursorItem[PAUSE_ITEM] == ITEM_GUST_JAR);

    if (onThisItem) {
        GustJar_BuildKaleidoElements();
    }
    // canToggle requires >1 element; when on the item with nothing to cycle,
    // KaleidoWheel_Run still closes the wheel if it was open (canToggle=0 skips A).
    KaleidoWheel_Run(play, onThisItem, sGustAvailCount > 1, &sGustPressASelectorActive, GustJar_Cycle);
}

static void GustJar_DrawPressASelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_GUST_JAR) {
        KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], 0, 0, 0, NULL, NULL, NULL, NULL, 0, 0);
        return;
    }

    GustJar_BuildKaleidoElements();
    if (sGustAvailCount <= 1)
        return;

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
    KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], sGustPressASelectorActive, leftTex != NULL,
                              rightTex != NULL, leftTex, rightTex, NULL, NULL,
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

    // Get cursor slot vertex for positioning
    s32 cursorSlot = pauseCtx->cursorSlot[PAUSE_ITEM];
    s32 vtxIdx = cursorSlot * 4;

    OPEN_DISPS(play->state.gfxCtx);

    // Always draw selected medallion at half-alpha behind the Gust Jar icon
    // (same pattern as SW97 elemental arrows in z_kaleido_collect.c:440-465)
    if (curElem != 0) { // Not wind (default)
        void* medallionTex = ExtInv_GetItemIcon(sGustElemToMedallion[curElem]);
        if (medallionTex != NULL) {
            // Medallion at 50% alpha (behind)
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha >> 1);
            gSPVertex(POLY_OPA_DISP++, &pauseCtx->itemVtx[vtxIdx], 4, 0);
            KaleidoScope_DrawQuadTextureRGBA32(play->state.gfxCtx, medallionTex, 24, 24, 0);

            // Gust Jar icon at full alpha (on top)
            void* gustTex = ExtInv_GetItemIcon(ITEM_GUST_JAR);
            if (gustTex != NULL) {
                // Remap texture coords to 32x32 for item icon overlay (75% scale like SW97)
                Vtx* overlayVtx = KaleidoScope_AllocRemappedQuad(play->state.gfxCtx, &pauseCtx->itemVtx[vtxIdx], 32);

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
            void* tex = ExtInv_GetItemIcon(sGustElemToMedallion[elem]);
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
#define ARROW_WHEEL_MAX_ENTRIES SW97_ELEM_COUNT // plain + 6 medallions + bomb arrows

// Skijer's NEI: the wheel used to swap WHICH ITEM sat on the C-button (six ITEM_SW97_ARROW_* ids
// plus ITEM_BOMB_ARROWS) and mark the slot 0xFF "not from inventory". It now writes a FLAG and never
// touches the button's item at all — the button keeps the plain bow/slingshot and the medallion is
// composited over the HUD icon. Entries hold SW97_ELEM_* values; ownership, ordering and the
// bow-only rule for bombs all come from the shared Sw97_Element* helpers in extended_inventory.c,
// so the kaleido, the R/L cycle and the shot decode can never disagree about them.

// Octagonal radial layout (positions 0..7 around the cursor)
static const s16 sArrowWheelOffX[ARROW_WHEEL_MAX_ENTRIES] = { 0, 17, 24, 17, 0, -17, -24, -17 };
static const s16 sArrowWheelOffY[ARROW_WHEEL_MAX_ENTRIES] = { -24, -17, 0, 17, 24, 17, 0, -17 };

static u8 sArrowWheelEntries[ARROW_WHEEL_MAX_ENTRIES];
static u8 sArrowWheelAvailCount = 0;
static u8 sArrowWheelCursor = 0;
static u8 sArrowWheelOverlayActive = 0;
static s16 sArrowWheelHoldTimer = 0;
static s32 sArrowWheelLastCBtn = -1;
static s32 sArrowWheelStickHeld = 0;
// Which weapon the wheel is currently serving (0 = bow, 1 = slingshot). The two carry independent
// elements, and bombs only exist on the bow, so the entry list differs per weapon.
static u8 sArrowWheelIsSling = 0;

// Press-A selector (Roc's Feather style) — alternative to the hold-C wheel.
static u8 sArrowWheelPressAActive = 0;

// Find a C-button holding the weapon this wheel serves. Returns the cursor index into
// sArrowWheelEntries for the currently primed element, or -1 if it is not in the list. Writes the
// C-button index (0=C-Left, 1=C-Down, 2=C-Right) into *outCBtn, or -1 if no C-button has it.
static s8 ArrowWheel_GetCurrentEntry(s32* outCBtn) {
    *outCBtn = -1;
    for (s32 i = 1; i <= 3; i++) {
        u8 item = gSaveContext.equips.buttonItems[i];
        u8 isSling = Sw97_IsSlingItem(item);
        if (!Sw97_IsBowItem(item) && !isSling) {
            continue;
        }
        if (isSling != sArrowWheelIsSling) {
            continue; // that button holds the OTHER weapon; its element is a separate flag
        }
        *outCBtn = i - 1;
        break;
    }
    u8 elem = Sw97_GetElement(sArrowWheelIsSling);
    for (u8 k = 0; k < sArrowWheelAvailCount; k++) {
        if (sArrowWheelEntries[k] == elem) {
            return (s8)k;
        }
    }
    return -1;
}

// Apply the selected entry. Note what is NOT here any more: no buttonItems[] write and no
// cButtonSlots[cBtn] = 0xFF. The icon reload still is — the composite HUD icon is built from
// iconItemSegment[], which would otherwise keep showing the previous element.
static void ArrowWheel_ApplyEntry(PlayState* play, u8 entryIdx, s32 cBtn) {
    if (entryIdx >= sArrowWheelAvailCount)
        return;
    Sw97_SetElement(sArrowWheelIsSling, sArrowWheelEntries[entryIdx]);
    if (cBtn >= 0 && cBtn <= 2) {
        Interface_LoadItemIcon1(play, cBtn + 1); // buttonItems[0] is B
    }
    Sw97_RefreshButtonIcons(play); // any other button holding the same weapon
}

// Resolve a wheel entry to its display icon texture. SW97_ELEM_NONE has no medallion — it shows the
// bare weapon, which is exactly what "no element primed" means.
static void* ArrowWheel_GetEntryIcon(u8 entry) {
    if (entry == SW97_ELEM_NONE) {
        return ExtInv_GetItemIcon(sArrowWheelIsSling ? ITEM_SLINGSHOT : ITEM_BOW);
    }
    return ExtInv_GetItemIcon(Sw97_ElementIcon(entry));
}

// Medallion icons are 24x24; the bare weapon and the bomb-arrows icon are 32x32.
static s32 ArrowWheel_GetEntryIconSize(u8 entry) {
    return ((entry == SW97_ELEM_NONE) || (entry == SW97_ELEM_BOMB)) ? 32 : 24;
}

// Per-call context for ArrowWheel_Cycle, stashed by the handler after its
// inline gates pass (the wheel callback can't take extra args).
static s8 sArrowWheelCycleCurIdx = 0;
static s32 sArrowWheelCycleCBtn = -1;

static void ArrowWheel_Cycle(PlayState* play, s32 dir) {
    s8 newIdx = (dir > 0) ? (sArrowWheelCycleCurIdx + 1) % sArrowWheelAvailCount
                          : (sArrowWheelCycleCurIdx + sArrowWheelAvailCount - 1) % sArrowWheelAvailCount;
    ArrowWheel_ApplyEntry(play, newIdx, sArrowWheelCycleCBtn);
    Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

// ── Elemental Wand — six rods in one page-2 cell ─────────────────────────────
// Same press-A idiom as the Dual Cane above: A opens, stick L/R picks, A confirms. What makes this
// one worth its slot is that six items share one cell — the cell's ICON and NAME follow the active
// mode (ExtInv_GetItemIcon / ExtInv_GetCustomItemNameTex resolve them from Wand_GetMode), and the
// preview on each side is the MEDALLION that unlocks the neighbouring rod, so the wheel doubles as a
// reminder of what you are still missing.
static u8 sWandSelectorActive = 0;

static void Wand_KaleidoCycle(PlayState* play, s32 dir) {
    Wand_SetMode(Wand_ModeNeighbor(Wand_GetMode(), dir));
    Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

static void Wand_HandleKaleidoSelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    u8 onThisItem = (pauseCtx->cursorItem[PAUSE_ITEM] == ITEM_ELEMENTAL_WAND);

    KaleidoWheel_Run(play, onThisItem, Wand_ModeCount() > 1, &sWandSelectorActive, Wand_KaleidoCycle);
}

// All six rods share one staff icon, so the element has to come from somewhere else: the active
// medallion at half alpha covering the cell, with the wand put back on top at 75% (GustJar idiom).
static void Wand_DrawMedallionBackdrop(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    void* medallionTex = ExtInv_GetItemIcon(Wand_ModeMedallion(Wand_GetMode()));
    void* wandTex = ExtInv_GetItemIcon(ITEM_ELEMENTAL_WAND);

    if ((medallionTex == NULL) || (wandTex == NULL)) {
        return;
    }

    Vtx* cellVtx = &pauseCtx->itemVtx[pauseCtx->cursorSlot[PAUSE_ITEM] * 4];
    Vtx* wandVtx = KaleidoScope_AllocRemappedQuad(play->state.gfxCtx, cellVtx, 32);

    OPEN_DISPS(play->state.gfxCtx);

    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha >> 1);
    gSPVertex(POLY_OPA_DISP++, cellVtx, 4, 0);
    KaleidoScope_DrawQuadTextureRGBA32(play->state.gfxCtx, medallionTex, 24, 24, 0);

    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
    gSPVertex(POLY_OPA_DISP++, wandVtx, 4, 0);
    KaleidoScope_DrawQuadTextureRGBA32(play->state.gfxCtx, wandTex, 32, 32, 0);

    CLOSE_DISPS(play->state.gfxCtx);
}

static void Wand_DrawKaleidoSelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;

    if (pauseCtx->cursorItem[PAUSE_ITEM] != ITEM_ELEMENTAL_WAND) {
        KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], 0, 0, 0, NULL, NULL, NULL, NULL, 0, 0);
        return;
    }

    // Before the wheel gate: with a single rod owned there is nothing to cycle, but you still need
    // to see WHICH rod it is.
    Wand_DrawMedallionBackdrop(play);

    if (Wand_ModeCount() <= 1) {
        return;
    }

    // Previews are the medallions of the neighbouring rods (24x24 quest icons), not the rods
    // themselves — you pick a rod by recognising its element.
    void* leftTex = ExtInv_GetItemIcon(Wand_ModeMedallion(Wand_ModeNeighbor(Wand_GetMode(), -1)));
    void* rightTex = ExtInv_GetItemIcon(Wand_ModeMedallion(Wand_ModeNeighbor(Wand_GetMode(), 1)));

    KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], sWandSelectorActive, leftTex != NULL,
                              rightTex != NULL, leftTex, rightTex, NULL, NULL, 24, 24);
}

// ── Sheikah Slate — four runes in one page-2 cell ────────────────────────────
// Wand idiom over the EXT (u16) slate id: the cell's ICON follows the active rune
// (ExtInv_GetItemIcon returns the slate-with-badge composite from Slate_GetRune), and the previews
// on each side are the neighbouring runes' 24x24 glyphs — the gust-jar "mini icons on top" look.
static u8 sSlateSelectorActive = 0;

static void Slate_KaleidoCycle(PlayState* play, s32 dir) {
    Slate_SetRune(Slate_RuneNeighbor(Slate_GetRune(), dir));
    Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

static void Slate_HandleKaleidoSelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    u8 onThisItem = (pauseCtx->cursorItem[PAUSE_ITEM] == EXT_ITEM_SHEIKAH_SLATE);

    KaleidoWheel_Run(play, onThisItem, Slate_RuneCount() > 1, &sSlateSelectorActive, Slate_KaleidoCycle);
}

static void Slate_DrawKaleidoSelector(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;

    if (pauseCtx->cursorItem[PAUSE_ITEM] != EXT_ITEM_SHEIKAH_SLATE) {
        KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], 0, 0, 0, NULL, NULL, NULL, NULL, 0, 0);
        return;
    }
    if (Slate_RuneCount() <= 1) {
        return;
    }

    void* leftTex = Slate_RuneMiniIcon(Slate_RuneNeighbor(Slate_GetRune(), -1));
    void* rightTex = Slate_RuneMiniIcon(Slate_RuneNeighbor(Slate_GetRune(), 1));

    KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], sSlateSelectorActive, leftTex != NULL,
                              rightTex != NULL, leftTex, rightTex, NULL, NULL, 32, 32);
}

static void ArrowWheel_HandlePressA(PlayState* play) {
    // Hard disable: SW97 medallions off → never cycle (kept inline; doesn't
    // fit the wheel's onThisItem close path).
    if (!SW97_MEDALLIONS_ENABLED()) {
        sArrowWheelPressAActive = 0;
        return;
    }
    PauseContext* pauseCtx = &play->pauseCtx;

    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_BOW && cursorItem != ITEM_SLINGSHOT) {
        if (sArrowWheelPressAActive) {
            sArrowWheelPressAActive = 0;
            gCurrentItemCyclingSlot = -1;
        }
        return;
    }

    sArrowWheelIsSling = (cursorItem == ITEM_SLINGSHOT);
    ArrowWheel_Build();
    if (sArrowWheelAvailCount <= 1)
        return;

    // Resolve which C-button holds this weapon. If none does, fall back to C-Left as the target so
    // the icon still refreshes somewhere visible; the flag itself is set regardless of any button,
    // so cycling works even with the weapon unequipped. Skijer's NEI (Sw97).
    s32 cBtn = -1;
    s8 curIdx = ArrowWheel_GetCurrentEntry(&cBtn);
    if (cBtn < 0) {
        cBtn = 0; // C-Left as default target
    }
    if (curIdx < 0)
        curIdx = 0;
    sArrowWheelCycleCurIdx = curIdx;
    sArrowWheelCycleCBtn = cBtn;

    KaleidoWheel_Run(play, 1, 1, &sArrowWheelPressAActive, ArrowWheel_Cycle);
}

static void ArrowWheel_DrawPressA(PlayState* play) {
    if (!SW97_MEDALLIONS_ENABLED())
        return;
    PauseContext* pauseCtx = &play->pauseCtx;
    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_BOW && cursorItem != ITEM_SLINGSHOT) {
        KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], 0, 0, 0, NULL, NULL, NULL, NULL, 0, 0);
        return;
    }

    sArrowWheelIsSling = (cursorItem == ITEM_SLINGSHOT);
    ArrowWheel_Build();
    if (sArrowWheelAvailCount <= 1)
        return;

    s32 cBtn = -1;
    s8 curIdx = ArrowWheel_GetCurrentEntry(&cBtn);
    if (curIdx < 0)
        curIdx = 0;

    u8 prevEntry = sArrowWheelEntries[(curIdx + sArrowWheelAvailCount - 1) % sArrowWheelAvailCount];
    u8 nextEntry = sArrowWheelEntries[(curIdx + 1) % sArrowWheelAvailCount];

    void* leftTex = ArrowWheel_GetEntryIcon(prevEntry);
    void* rightTex = ArrowWheel_GetEntryIcon(nextEntry);

    // Per-entry native size — medallions are 24x24, the bare weapon and bomb arrows 32x32. Mixed
    // prev/next is supported.
    s32 leftSize = ArrowWheel_GetEntryIconSize(prevEntry);
    s32 rightSize = ArrowWheel_GetEntryIconSize(nextEntry);

    KaleidoCycle_DrawRocStyle(play, pauseCtx->cursorSlot[PAUSE_ITEM], sArrowWheelPressAActive, leftTex != NULL,
                              rightTex != NULL, leftTex, rightTex, NULL, NULL, leftSize, rightSize);
}

// Rebuild the entry list for whichever weapon the cursor is on. Ownership (medallion OR the vanilla
// elemental arrow) and the bomb-arrows unlock all live in Sw97_ElementOwned now, so the old
// duplicated CHECK_QUEST_ITEM table and the ExtInv_GetItemSlot(ITEM_BOMB_ARROWS) lookup are gone.
static void ArrowWheel_Build(void) {
    sArrowWheelAvailCount = Sw97_ElementCount(sArrowWheelIsSling);
    if (sArrowWheelAvailCount > ARROW_WHEEL_MAX_ENTRIES) {
        sArrowWheelAvailCount = ARROW_WHEEL_MAX_ENTRIES;
    }
    for (u8 i = 0; i < sArrowWheelAvailCount; i++) {
        sArrowWheelEntries[i] = Sw97_ElementAt(sArrowWheelIsSling, i);
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

    sArrowWheelIsSling = (cursorItem == ITEM_SLINGSHOT);
    ArrowWheel_Build();
    if (sArrowWheelAvailCount == 0) {
        sArrowWheelOverlayActive = 0;
        sArrowWheelHoldTimer = 0;
        return;
    }

    // Detect held C-button (priority CLEFT > CDOWN > CRIGHT)
    u16 btn = input->cur.button;
    s32 cBtn = -1;
    if (btn & BTN_CLEFT)
        cBtn = 0;
    else if (btn & BTN_CDOWN)
        cBtn = 1;
    else if (btn & BTN_CRIGHT)
        cBtn = 2;

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
            // Confirm: prime the chosen element. The button keeps whatever weapon the engine just
            // bound — we no longer overwrite buttonItems or set the 0xFF "not from inventory"
            // marker, so the slot stays a normal inventory binding.
            ArrowWheel_ApplyEntry(play, sArrowWheelCursor, sArrowWheelLastCBtn);
            Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
        sArrowWheelOverlayActive = 0;
        sArrowWheelHoldTimer = 0;
    }
}

static void ArrowWheel_Draw(PlayState* play) {
    if (!SW97_MEDALLIONS_ENABLED())
        return;

    PauseContext* pauseCtx = &play->pauseCtx;
    s32 cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
    if (cursorItem != ITEM_BOW && cursorItem != ITEM_SLINGSHOT)
        return;

    sArrowWheelIsSling = (cursorItem == ITEM_SLINGSHOT);
    ArrowWheel_Build();
    if (!sArrowWheelOverlayActive || sArrowWheelAvailCount == 0)
        return;

    s32 cursorSlot = pauseCtx->cursorSlot[PAUSE_ITEM];
    s32 vtxIdx = cursorSlot * 4;

    OPEN_DISPS(play->state.gfxCtx);

    for (u8 i = 0; i < sArrowWheelAvailCount; i++) {
        u8 entry = sArrowWheelEntries[i];
        bool is32px = (ArrowWheel_GetEntryIconSize(entry) == 32);
        void* tex = ArrowWheel_GetEntryIcon(entry);
        if (tex == NULL)
            continue;

        u8 alpha = (i == sArrowWheelCursor) ? pauseCtx->alpha : (pauseCtx->alpha >> 1);
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, alpha);

        Vtx* elemVtx;
        if (is32px) {
            // 32x32 icons (bomb arrows / bombchu) — remap UVs to fit the slot quad.
            elemVtx = KaleidoScope_AllocRemappedQuad(play->state.gfxCtx, &pauseCtx->itemVtx[vtxIdx], 32);
        } else {
            elemVtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, 4 * sizeof(Vtx));
            for (s32 vi = 0; vi < 4; vi++) {
                elemVtx[vi] = pauseCtx->itemVtx[vtxIdx + vi];
            }
        }
        for (s32 vi = 0; vi < 4; vi++) {
            elemVtx[vi].v.ob[0] += sArrowWheelOffX[i];
            elemVtx[vi].v.ob[1] += sArrowWheelOffY[i];
        }

        gSPVertex(POLY_OPA_DISP++, elemVtx, 4, 0);
        KaleidoScope_DrawQuadTextureRGBA32(play->state.gfxCtx, tex, is32px ? 32 : 24, is32px ? 32 : 24, 0);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

// Bottle wheel selector — INDEX-based (Skijer's NEI). Unlike KaleidoScope_HandleItemCycleExtras (which
// swaps by VALUE and so can't move between two identical empty bottles), this steps the ACTIVE SLOT so
// every physical bottle — empty ones included — is reachable, and the wheel stays cyclable even when
// every bottle is empty. A opens/closes the selector; stick L/R steps the slot and updates the visible
// slot + any C-button showing it.
static void Bottle_WheelHandle(PlayState* play, u8 wheel, u8 kaleidoSlot) {
    Input* input = &play->state.input[0];
    PauseContext* pauseCtx = &play->pauseCtx;
    bool dpad = (CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0) && !CHECK_BTN_ALL(input->cur.button, BTN_CUP));

    // Keep the visible slot showing a bottle the wheel actually owns (covers give/drink/catch that
    // changed it out of kaleido, and the initial projection).
    u16 first = Bottle_WheelFirstItem(wheel);
    if (first != ITEM_NONE && !Bottle_WheelContains(wheel, ExtInv_GetSlotItem(kaleidoSlot))) {
        ExtInv_SetSlotItem(kaleidoSlot, (u8)first);
    }

    if (Bottle_WheelBottleCount(wheel) < 2) {
        return; // 0 or 1 bottle: nothing to cycle
    }

    if (pauseCtx->cursorSlot[PAUSE_ITEM] == kaleidoSlot && CHECK_BTN_ALL(input->press.button, BTN_A)) {
        Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        gCurrentItemCyclingSlot = gCurrentItemCyclingSlot == kaleidoSlot ? -1 : kaleidoSlot;
    }
    if (gCurrentItemCyclingSlot == kaleidoSlot) {
        pauseCtx->cursorColorSet = 8;
        s8 dir = 0;
        if ((pauseCtx->stickRelX > 30 || pauseCtx->stickRelY > 30) ||
            (dpad && CHECK_BTN_ANY(input->press.button, BTN_DRIGHT | BTN_DUP))) {
            dir = 1;
        } else if ((pauseCtx->stickRelX < -30 || pauseCtx->stickRelY < -30) ||
                   (dpad && CHECK_BTN_ANY(input->press.button, BTN_DLEFT | BTN_DDOWN))) {
            dir = -1;
        }
        if (dir != 0) {
            Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            u16 oldItem = ExtInv_GetSlotItem(kaleidoSlot);
            u8 newItem = (u8)Bottle_WheelStep(wheel, dir);
            // Update the C-button equipped to THIS slot (if any) to the new bottle.
            for (int i = 1; i < ARRAY_COUNT(gSaveContext.equips.buttonItems); i++) {
                if (gSaveContext.equips.cButtonSlots[i - 1] == kaleidoSlot &&
                    gSaveContext.equips.buttonItems[i] == oldItem) {
                    gSaveContext.equips.buttonItems[i] = newItem;
                    Interface_LoadItemIcon1(play, i);
                    break;
                }
            }
            ExtInv_SetSlotItem(kaleidoSlot, newItem);
        }
        gCurrentItemCyclingSlot = pauseCtx->cursorSlot[PAUSE_ITEM] == kaleidoSlot ? kaleidoSlot : -1;
    }
}

void KaleidoScope_HandleItemCycles(PlayState* play) {
    // handle the mask select — only on the vanilla item page (0); on pages 1/2 the cell holds a
    // custom item / MM mask, so the wheel must not respond there (same as the bottle wheels).
    // Skijer's NEI. The IS_RANDO term is upstream's: in rando the wheel stays usable even when
    // CanMaskSelect() says no, and that has to keep working on page 0.
    if (ExtInv_GetCurrentPage() == 0)
        KaleidoScope_HandleItemCycleExtras(play, SLOT_TRADE_CHILD, IS_RANDO || CanMaskSelect(),
                                           IS_RANDO ? Randomizer_GetPrevChildTradeItem()
                                                    : (INV_CONTENT(ITEM_TRADE_CHILD) <= ITEM_MASK_KEATON ||
                                                               INV_CONTENT(ITEM_TRADE_CHILD) > ITEM_MASK_TRUTH
                                                           ? ITEM_MASK_TRUTH
                                                           : INV_CONTENT(ITEM_TRADE_CHILD) - 1),
                                           IS_RANDO ? Randomizer_GetNextChildTradeItem()
                                                    : (INV_CONTENT(ITEM_TRADE_CHILD) >= ITEM_MASK_TRUTH ||
                                                               INV_CONTENT(ITEM_TRADE_CHILD) < ITEM_MASK_KEATON
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
    // Adult-trade wheel: the shared linear cycle wheel, fed with EVERY owned trade item — a held
    // vanilla/rando item folded in, plus the granted MM ones — so the one wheel cycles them all (A opens,
    // stick L/R cycles). Only on the vanilla item page (0) — on pages 1/2 the cell is a custom item /
    // MM mask, so the wheel must not respond there (same as the bottle wheels). Skijer's NEI
    if (ExtInv_GetCurrentPage() == 0) {
        u16 tradeCur = ExtInv_GetSlotItem(SLOT_TRADE_ADULT);
        TradeAdult_FoldCurrent(tradeCur);
        // The child chain's non-mask items (Weird Egg / Cucco / Zelda's Letter, trade indices 20-22)
        // live in SLOT_TRADE_CHILD, which this wheel never reads — so nothing was ever setting their
        // ownership bits and they could not reach the unified wheel (or MM). Fold them from their own
        // slot. Masks in that slot are ignored: TradeAdult_IndexOfItem returns -1 for them.
        // Skijer 2026-07-30
        TradeAdult_FoldCurrent(ExtInv_GetSlotItem(SLOT_TRADE_CHILD));
        if (tradeCur == ITEM_NONE && TradeAdult_OwnedCount() > 0) {
            tradeCur = TradeAdult_ItemId(TradeAdult_OwnedAt(0)); // show the first owned item in an empty slot
            ExtInv_SetSlotItem(SLOT_TRADE_ADULT, tradeCur);
        }
        KaleidoScope_HandleItemCycleExtras(play, SLOT_TRADE_ADULT, TradeAdult_OwnedCount() > 1,
                                           TradeAdult_PrevItem(tradeCur), TradeAdult_NextItem(tradeCur), true);
    }

    // Bottle Randomizer wheels A/B (Skijer's NEI). Wheel A holds bottleSlots[0..3], Wheel B holds
    // bottleSlots[4..7] (the bottle inventory edited in the save editor). Show a real bottle from
    // that wheel in the visible slot, then reuse the trade-slot cycler to swap among them. Only
    // syncs when the wheel actually has bottles, so vanilla bottles are untouched when the rando
    // isn't active. ONLY on page 1 — on pages 2/3 the visual bottle cells are different items
    // (custom items / MM masks), so the wheel must not run there.
    if (ExtInv_GetCurrentPage() == 0) {
        // Index-based selectors so every bottle (empty ones included) is reachable, always cyclable.
        Bottle_WheelHandle(play, BOTTLE_WHEEL_A, SLOT_BOTTLE_1);
        Bottle_WheelHandle(play, BOTTLE_WHEEL_B, SLOT_BOTTLE_2);
    }

    // Handle Nayru's Love/Roc's Feather
    KaleidoScope_HandleItemCycleExtras(play, SLOT_NAYRUS_LOVE, Randomizer_GetSettingValue(RSK_ROCS_FEATHER),
                                       Enhancement_GetPrevNayrusItem(), Enhancement_GetNextNayrusItem(), true);

    // Handle Farore's Wind/Rito Mask — same idea, page 0 only (on pages 1/2 that
    // grid position is a different item entirely). Skijer's NEI
    if (ExtInv_GetCurrentPage() == 0) {
        RitoItem_SyncCell(); // records what this file owns, seeds an empty cell
        KaleidoScope_HandleItemCycleExtras(play, SLOT_FARORES_WIND, RitoItem_CanCycle(), RitoItem_OtherItem(),
                                           RitoItem_OtherItem(), true);
    }

    // Handle Gust Jar element cycle
    GustJar_HandleElementCycle(play);

    // Handle Bow/Slingshot elemental-arrow wheel (hold-C on bow or slingshot)
    ArrowWheel_Handle(play);

    // Handle Lantern fire-type selector (press A on lantern → stick L/R picks
    // between captured types + Vacía, press A confirms / B cancels)
    Lantern_HandleKaleidoSelector(play);

    // Dual Cane cane-type toggle (A on the cell) — Somaria <-> Pacci.
    Cane_HandleKaleidoSelector(play);

    // Shovel <-> Dominion Rod (shared cell 46; also folds pre-re-layout saves). Skijer's NEI
    Shovel_HandleKaleidoSelector(play);

    // Elemental Wand rod selector (A on the cell) — six rods share the page-2 slot the Bomb Arrows
    // used to occupy. Skijer's NEI
    Wand_HandleKaleidoSelector(play);

    // Sheikah Slate rune selector (A on the cell) — four runes share the slate cell. Skijer's NEI
    Slate_HandleKaleidoSelector(play);

    // Twilight Upgrade mode toggles — A on hookshot/longshot (Clawshot) or
    // boomerang (Gale Boomerang) opens a 2-slot selector. Gated by the
    // corresponding twilightUpgrade bit so the toggles stay hidden until the
    // player obtains the upgrade.
    Clawshot_HandleKaleidoSelector(play);
    Gale_HandleKaleidoSelector(play);

    // Pictograph Box flip on the Lens of Truth slot (A → Lens ↔ Pictobox). Skijer's NEI
    Picto_HandleKaleidoSelector(play);

    // Power Keg flip on the Bomb slot (A → Bomb ↔ Power Keg). Skijer's NEI
    PowerKeg_HandleKaleidoSelector(play);

    // Gust Jar press-A element selector (Roc's Feather style — coexists with
    // the hold-C wheel below).
    GustJar_HandlePressASelector(play);

    // Bow / Slingshot press-A arrow selector (Roc's Feather style — coexists
    // with the hold-C arrow wheel below).
    ArrowWheel_HandlePressA(play);
}

void KaleidoScope_DrawItemCycles(PlayState* play) {
    // draw the mask select
    // mask-select overlay only on the vanilla item page (0) — pages 1/2 show custom items / masks. Skijer's NEI
    // IS_RANDO term from upstream, same reasoning as the input handler above.
    if (ExtInv_GetCurrentPage() == 0)
        KaleidoScope_DrawItemCycleExtras(play, SLOT_TRADE_CHILD, IS_RANDO || CanMaskSelect(),
                                         IS_RANDO ? Randomizer_GetPrevChildTradeItem()
                                                  : (INV_CONTENT(ITEM_TRADE_CHILD) <= ITEM_MASK_KEATON ||
                                                             INV_CONTENT(ITEM_TRADE_CHILD) > ITEM_MASK_TRUTH
                                                         ? ITEM_MASK_TRUTH
                                                         : INV_CONTENT(ITEM_TRADE_CHILD) - 1),
                                         IS_RANDO ? Randomizer_GetNextChildTradeItem()
                                                  : (INV_CONTENT(ITEM_TRADE_CHILD) >= ITEM_MASK_TRUTH ||
                                                             INV_CONTENT(ITEM_TRADE_CHILD) < ITEM_MASK_KEATON
                                                         ? ITEM_MASK_KEATON
                                                         : INV_CONTENT(ITEM_TRADE_CHILD) + 1));

    // draw the adult trade select — only on the vanilla item page (0), like the mask select above. Skijer's NEI
    if (ExtInv_GetCurrentPage() == 0) {
        u16 tradeCur = ExtInv_GetSlotItem(SLOT_TRADE_ADULT);
        KaleidoScope_DrawItemCycleExtras(play, SLOT_TRADE_ADULT, TradeAdult_OwnedCount() > 1,
                                         TradeAdult_PrevItem(tradeCur), TradeAdult_NextItem(tradeCur));
    }

    // Bottle Randomizer wheels A/B draw (Skijer's NEI) — page 1 only (see HandleItemCycles). Previews
    // are the prev/next SLOT (index-based) with forceShow, so the mini-icons + A indicator appear even
    // between identical EMPTY bottles.
    if (ExtInv_GetCurrentPage() == 0) {
        KaleidoScope_DrawItemCycleExtrasImpl(play, SLOT_BOTTLE_1, Bottle_WheelBottleCount(BOTTLE_WHEEL_A) > 1,
                                             Bottle_WheelPeek(BOTTLE_WHEEL_A, -1), Bottle_WheelPeek(BOTTLE_WHEEL_A, 1),
                                             true);
        KaleidoScope_DrawItemCycleExtrasImpl(play, SLOT_BOTTLE_2, Bottle_WheelBottleCount(BOTTLE_WHEEL_B) > 1,
                                             Bottle_WheelPeek(BOTTLE_WHEEL_B, -1), Bottle_WheelPeek(BOTTLE_WHEEL_B, 1),
                                             true);
    }

    // Draw Nayru's Love/Roc's Feather
    KaleidoScope_DrawItemCycleExtras(play, SLOT_NAYRUS_LOVE, Randomizer_GetSettingValue(RSK_ROCS_FEATHER),
                                     Enhancement_GetPrevNayrusItem(), Enhancement_GetNextNayrusItem());

    // Draw Farore's Wind/Rito Mask (see HandleItemCycles — page 0 only). Skijer's NEI
    if (ExtInv_GetCurrentPage() == 0) {
        KaleidoScope_DrawItemCycleExtras(play, SLOT_FARORES_WIND, RitoItem_CanCycle(), RitoItem_OtherItem(),
                                         RitoItem_OtherItem());
    }

    // Draw Gust Jar element indicator
    GustJar_DrawElementCycle(play);

    // Draw Bow/Slingshot elemental-arrow wheel overlay
    ArrowWheel_Draw(play);

    // Draw Lantern fire-type selector overlay (only when active)
    Lantern_DrawKaleidoSelector(play);

    // Dual Cane cane-type toggle overlay.
    Cane_DrawKaleidoSelector(play);

    // Shovel <-> Dominion Rod overlay (shared cell 46). Skijer's NEI
    Shovel_DrawKaleidoSelector(play);

    // Elemental Wand rod selector overlay. Skijer's NEI
    Wand_DrawKaleidoSelector(play);

    // Sheikah Slate rune selector overlay. Skijer's NEI
    Slate_DrawKaleidoSelector(play);

    // Draw Twilight Upgrade mode toggles (Clawshot + Gale Boomerang)
    Clawshot_DrawKaleidoSelector(play);
    Gale_DrawKaleidoSelector(play);

    // Pictograph Box flip overlay on the Lens of Truth slot. Skijer's NEI
    Picto_DrawKaleidoSelector(play);

    // Power Keg flip overlay on the Bomb slot. Skijer's NEI
    PowerKeg_DrawKaleidoSelector(play);

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
            static s32 sHarpoonHoldFrames = 0;
            static s16 sHarpoonHoldSlot = -1;
            s16 curSlot = pauseCtx->cursorSlot[PAUSE_ITEM];
            if (CHECK_BTN_ALL(input->cur.button, BTN_CUP) && curSlot >= 0) {
                if (sHarpoonHoldSlot != curSlot) {
                    sHarpoonHoldSlot = curSlot;
                    sHarpoonHoldFrames = 0;
                }
                sHarpoonHoldFrames++;
                if (sHarpoonHoldFrames == 20) {
                    extern void HarpoonDrops_RequestDropFromPause(int tabId, int slot);
                    HarpoonDrops_RequestDropFromPause(/*tabId=items*/ 0, curSlot);
                    // Continue counting so a long hold doesn't re-fire
                    // every frame — only the single fire at exactly 20.
                }
            } else {
                sHarpoonHoldFrames = 0;
                sHarpoonHoldSlot = -1;
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

        if (pauseCtx->cursorSpecialPos == 0) {
            pauseCtx->cursorColorSet = 4;

            // Inventory sub-page switch (vanilla / custom items / MM masks).
            // It uses whichever shoulder button is NOT bound to kaleido tab
            // switching: tab switching (KaleidoScope_HandlePageToggles) uses L
            // by default (NGCKaleidoSwitcher) and Z when the switcher is off, so
            // this takes the freed button and the two never collide.
            bool ngcMode = CVarGetInteger(CVAR_ENHANCEMENT("NGCKaleidoSwitcher"), 0) != 0;
            s16 freedBtn = ngcMode ? BTN_Z : BTN_L;

            if (ExtInv_CanSwitchPage() && CHECK_BTN_ALL(input->press.button, freedBtn) && !IsItemCycling()) {
                ExtInv_SwitchPage();
                Audio_PlaySoundGeneral(NA_SE_SY_HP_RECOVER, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                moveCursorResult = 2;
            }

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
                if (ExtInv_GetSlotItem(pauseCtx->cursorPoint[PAUSE_ITEM])) {} // Skijer's NEI

                while (moveCursorResult == 0) {
                    if ((pauseCtx->stickRelX < -30) || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DLEFT))) {
                        if (pauseCtx->cursorX[PAUSE_ITEM] != 0) {
                            pauseCtx->cursorX[PAUSE_ITEM] -= 1;
                            pauseCtx->cursorPoint[PAUSE_ITEM] -= 1;
                            if ((ExtInv_GetSlotItem(ExtInv_GetInventorySlot(pauseCtx->cursorPoint[PAUSE_ITEM])) !=
                                 ITEM_NONE) || // Skijer's NEI
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
                            if ((ExtInv_GetSlotItem(ExtInv_GetInventorySlot(pauseCtx->cursorPoint[PAUSE_ITEM])) !=
                                 ITEM_NONE) || // Skijer's NEI
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
                        ExtInv_GetSlotItem(ExtInv_GetInventorySlot(pauseCtx->cursorPoint[PAUSE_ITEM])); // Skijer's NEI
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
                    if (ExtInv_GetSlotItem(ExtInv_GetInventorySlot(cursorPoint)) != ITEM_NONE) { // Skijer's NEI
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
                    if (ExtInv_GetSlotItem(ExtInv_GetInventorySlot(cursorPoint)) != ITEM_NONE) { // Skijer's NEI
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
                                if ((ExtInv_GetSlotItem(ExtInv_GetInventorySlot(pauseCtx->cursorPoint[PAUSE_ITEM])) !=
                                     ITEM_NONE) || // Skijer's NEI
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
                                if ((ExtInv_GetSlotItem(ExtInv_GetInventorySlot(pauseCtx->cursorPoint[PAUSE_ITEM])) !=
                                     ITEM_NONE) || // Skijer's NEI
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
                cursorItem = ExtInv_GetSlotItem(inventorySlot); // Skijer's NEI
            } else if (moveCursorResult != 2) {
                cursorItem = ExtInv_GetSlotItem(inventorySlot); // Skijer's NEI
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
        if (ExtInv_GetSlotItem(drawSlot) != ITEM_NONE) { // Skijer's NEI
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
            int itemId = ExtInv_GetSlotItem(drawSlot); // Skijer's NEI
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

            // Skijer's NEI — Ultrashot: while owned, the hookshot cell keeps the Longshot ICON; a
            // small Light medallion on the cell's TOP-RIGHT corner (+ the "Ultrashot" name tex) is
            // what tells it apart. Suppressed while the Twilight clawshot MODE is on (claw icon).
            if ((drawSlot == SLOT_HOOKSHOT) && (itemId == ITEM_LONGSHOT) && Nei_Save()->ultrashotOwned &&
                !TwilightUpgrade_IsClawshotActive()) {
                Vtx* cellVtx = &pauseCtx->itemVtx[j + 0];
                Vtx* mv = (Vtx*)Graph_Alloc(play->state.gfxCtx, 4 * sizeof(Vtx));
                s16 cx = (cellVtx[0].v.ob[0] + cellVtx[3].v.ob[0]) / 2;
                s16 cy = (cellVtx[0].v.ob[1] + cellVtx[3].v.ob[1]) / 2;
                s16 mSize = 14;
                s16 mx0 = cx + 18 - mSize; // marker's right edge 2px past the 32px cell's right edge
                s16 myTop = cy + 18;       // marker's top edge 2px past the cell's top edge
                s32 mvi;

                for (mvi = 0; mvi < 4; mvi++) {
                    mv[mvi] = cellVtx[0];
                }
                mv[0].v.ob[0] = mx0;
                mv[0].v.ob[1] = myTop;
                mv[0].v.tc[0] = 0;
                mv[0].v.tc[1] = 0;
                mv[1].v.ob[0] = mx0 + mSize;
                mv[1].v.ob[1] = myTop;
                mv[1].v.tc[0] = 24 << 5;
                mv[1].v.tc[1] = 0;
                mv[2].v.ob[0] = mx0;
                mv[2].v.ob[1] = myTop - mSize;
                mv[2].v.tc[0] = 0;
                mv[2].v.tc[1] = 24 << 5;
                mv[3].v.ob[0] = mx0 + mSize;
                mv[3].v.ob[1] = myTop - mSize;
                mv[3].v.tc[0] = 24 << 5;
                mv[3].v.tc[1] = 24 << 5;

                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
                gSPVertex(POLY_OPA_DISP++, mv, 4, 0);
                KaleidoScope_DrawQuadTextureRGBA32(
                    play->state.gfxCtx, (u8*)sNeiAligned_gQuestIconMedallionLightTex_117, 24, 24,
                    0);
            }
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
            // Skijer's NEI: the Pictograph Box on the Lens slot shows a 0/1 photo counter even though the
            // Lens isn't a vanilla ammo item. Force ITEM_LENS so DrawAmmoCount's override picks it up.
            u8 pictoOnLens = (i == SLOT_LENS) && Picto_IsOwned() && Picto_IsOnLensActive();
            s16 dispItem = pictoOnLens ? ITEM_LENS : gSaveContext.inventory.items[i];
            if (((gBetterAmmoRendering ? ItemInSlotUsesAmmo(i) : gAmmoItems[i] != ITEM_NONE) || pictoOnLens) &&
                (dispItem != ITEM_NONE)) {
                KaleidoScope_DrawAmmoCount(pauseCtx, play->state.gfxCtx, dispItem, i);
            }
        }
        // Bottomless Bottle counter: SLOT_BOTTLE_4 (slot 21) is past the default 15-slot loop above, so
        // draw its use-counter here too — the counter is what identifies a multi-use content. Skijer's NEI
        if (!gBetterAmmoRendering && ItemInSlotUsesAmmo(SLOT_BOTTLE_4) &&
            gSaveContext.inventory.items[SLOT_BOTTLE_4] != ITEM_NONE) {
            KaleidoScope_DrawAmmoCount(pauseCtx, play->state.gfxCtx, gSaveContext.inventory.items[SLOT_BOTTLE_4],
                                       SLOT_BOTTLE_4);
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

            // Skijer's NEI: a u16 EXT id (>= 0x0200, e.g. the Sheikah Slate) does not fit the u8
            // buttonItems array, so it rides the ext-button marker — ITEM_EXT_BUTTON parks here and
            // the real id in the parallel u16 store, which every icon site resolves. Its home slot
            // is not a vanilla inventory slot either, hence the 0xFF sentinel.
            if (pauseCtx->equipTargetItem >= 0x0200) {
                ExtButton_SetItem(targetButtonIndex, pauseCtx->equipTargetItem);
                gSaveContext.equips.cButtonSlots[pauseCtx->equipTargetCBtn] = 0xFF;
            } else {
                ExtButton_ClearItem(targetButtonIndex); // drop any stale ext id this button carried
                gSaveContext.equips.buttonItems[targetButtonIndex] = pauseCtx->equipTargetItem;
                gSaveContext.equips.cButtonSlots[pauseCtx->equipTargetCBtn] = pauseCtx->equipTargetSlot;
            }
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
