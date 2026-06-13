/**
 * broken_items.c - "Broken Modes" pause subscreen. See broken_items.h.
 *
 * Renders INSIDE the Map pause page: the Map page's own stone/parchment frame
 * (KaleidoScope_DrawPageSections + sMapTexs) is kept, the dungeon/world map
 * image is replaced by two item-icon selectors drawn as quads on the map face
 * (Ocarina of Time = Link Mode, Mario Mask = Mario Mode). The selected mode's
 * name + control map are printed below with the game font (GfxPrint).
 */

#include "global.h"
#include "mods/extended_inventory.h" // ExtInv_GetItemIcon
#include "assets/soh_assets.h"       // gPikaIconPikachuTex (Pikachu mode selector icon)
#include "broken_items.h"

#define CVAR_BROKEN_ITEMS_ENABLED "gBrokenItems.Enabled"
#define CVAR_SM64_MARIO           "gSm64Mario"

// Pikachu MODE — persistent CVar like Mario's gSm64Mario. The per-frame watcher
// in mm_player_form.cpp (MmForm_Update) sees the CVar and holds the Pikachu form
// via the instant 5-frame flash (no mm.o2r transformation-cutscene anims).
// This MODE coexists with the Pokeball ITEM (extended inventory page 2), which
// keeps its classic transform flow + cutscene untouched.
#define CVAR_PIKACHU_MODE "gPikachuMode"

// ---------------------------------------------------------------------------
// Mode + control-map data (English on purpose). Keep action strings short.
// ---------------------------------------------------------------------------
typedef struct {
    const char* btn;
    const char* action;
} BrokenCtrl;

typedef struct {
    const char* name;
    const BrokenCtrl* controls;
    s32 controlCount;
} BrokenMode;

static const BrokenCtrl sLinkControls[] = {
    { "Stick", "Move / run" },
    { "A",     "Action/roll" },
    { "B",     "Sword" },
    { "C",     "Items" },
    { "Z",     "Z-target" },
    { "R",     "Shield" },
};

static const BrokenCtrl sMarioControls[] = {
    { "Stick", "Move (SM64)" },
    { "A",     "Jump x2/x3" },
    { "B",     "Fire/punch" },
    { "Z",     "Crouch/GP" },
    { "D-Dn",  "Wing Cap" },
    { "D-Lf",  "Metal Cap" },
    { "D-Rt",  "Vanish Cap" },
    { "D-Up",  "Fire (soon)" },
};

// Physical X / Y / RB are expected mapped to C-Left / C-Right / C-Down in the
// input editor (right stick stays free for the camera). Rebindable: gPikaBind.*.
static const BrokenCtrl sPikachuControls[] = {
    { "A",    "Fight/talk" },
    { "B",    "Electric" },
    { "R",    "Shield" },
    { "C-Lf", "Jump" },
    { "C-Rt", "Quick Atk" },
    { "C-Dn", "Grass dash" },
    { "D-Up", "GMax/Charge" },
    { "D-Dn", "Iron Tail" },
    { "D-Rt", "Dark bomb" },
    { "D-Lf", "Sleep" },
};

typedef enum {
    BROKEN_MODE_LINK,
    BROKEN_MODE_MARIO,
    BROKEN_MODE_PIKACHU,
    BROKEN_MODE_COUNT
} BrokenModeId;

static const BrokenMode sModes[BROKEN_MODE_COUNT] = {
    { "LINK MODE",    sLinkControls,    ARRAY_COUNT(sLinkControls) },
    { "MARIO MODE",   sMarioControls,   ARRAY_COUNT(sMarioControls) },
    { "PIKACHU MODE", sPikachuControls, ARRAY_COUNT(sPikachuControls) },
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static s16 sActive = 0;        // Broken Modes is the active subscreen
static s16 sCursor = 0;        // BrokenModeId under the cursor
static s16 sStickCooldown = 0; // debounce for stick-driven cursor moves

// ---------------------------------------------------------------------------
static void BrokenItems_PlaySfx(u16 sfxId) {
    Audio_PlaySoundGeneral(sfxId, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultReverb);
}

// The in-page change button — Broken Modes opens AND closes with it on the Map
// page. Matches the SohMenuNEI "In-Page Change Button (L / Z)" dropdown
// (NGCKaleidoSwitcher): 0 = "L button" -> BTN_L, 1 = "Z button" -> BTN_Z.
static s16 BrokenItems_ToggleBtn(void) {
    return (CVarGetInteger(CVAR_ENHANCEMENT("NGCKaleidoSwitcher"), 0) != 0) ? BTN_Z : BTN_L;
}

// Which mode is currently equipped, derived from the persistent mode CVars.
// (A pokeball-item transformation is intentionally NOT reflected here — that is
// the other, transient system and doesn't change the equipped MODE.)
static s32 BrokenItems_CurrentEquipped(void) {
    if (CVarGetInteger(CVAR_PIKACHU_MODE, 0) != 0) {
        return BROKEN_MODE_PIKACHU;
    }
    return (CVarGetInteger(CVAR_SM64_MARIO, 0) != 0) ? BROKEN_MODE_MARIO : BROKEN_MODE_LINK;
}

s32 BrokenItems_Enabled(void) {
    return CVarGetInteger(CVAR_BROKEN_ITEMS_ENABLED, 0);
}

s32 BrokenItems_IsActive(void) {
    return sActive;
}

s32 BrokenItems_ShouldEnter(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    Input* input = &play->state.input[0];
    return BrokenItems_Enabled() && !sActive && (pauseCtx->pageIndex == PAUSE_MAP) &&
           CHECK_BTN_ALL(input->press.button, BrokenItems_ToggleBtn());
}

void BrokenItems_Enter(PlayState* play) {
    (void)play;
    sActive = 1;
    sCursor = BrokenItems_CurrentEquipped();
    sStickCooldown = 0;
    BrokenItems_PlaySfx(NA_SE_SY_WIN_SCROLL_LEFT);
}

void BrokenItems_Exit(PlayState* play) {
    (void)play;
    sActive = 0;
    BrokenItems_PlaySfx(NA_SE_SY_WIN_SCROLL_RIGHT);
}

// Equips a mode (3-way, mutually exclusive CVars — like Mario, NOT a transform
// call). The Pikachu form itself is engaged by the gPikachuMode watcher in
// mm_player_form.cpp via the instant flash on the next gameplay frame. We
// deliberately do NOT touch gSm64MarioMaskForce so Mario is no longer bound to
// C-Down, and we never touch the pokeball-item transform flow.
static void BrokenItems_Equip(PlayState* play, s32 mode) {
    (void)play;
    CVarSetInteger(CVAR_SM64_MARIO, (mode == BROKEN_MODE_MARIO) ? 1 : 0);
    CVarSetInteger(CVAR_PIKACHU_MODE, (mode == BROKEN_MODE_PIKACHU) ? 1 : 0);
    CVarSave();
    BrokenItems_PlaySfx(NA_SE_SY_DECIDE);
}

void BrokenItems_Update(PlayState* play, Input* input) {
    if (!sActive) {
        return;
    }

    if (sStickCooldown > 0) {
        sStickCooldown--;
    }

    s16 stickX = input->rel.stick_x;
    s32 dpad = CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0);
    // Two slots side by side -> move with left/right (stick or D-pad).
    s32 left = (stickX < -30) || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DLEFT));
    s32 right = (stickX > 30) || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DRIGHT));

    if ((sStickCooldown == 0) && (left || right)) {
        if (left && (sCursor > 0)) {
            sCursor--;
            sStickCooldown = 8;
            BrokenItems_PlaySfx(NA_SE_SY_CURSOR);
        } else if (right && (sCursor < BROKEN_MODE_COUNT - 1)) {
            sCursor++;
            sStickCooldown = 8;
            BrokenItems_PlaySfx(NA_SE_SY_CURSOR);
        }
    }
    if (ABS(stickX) < 10) {
        sStickCooldown = 0; // re-centered: allow the next move immediately
    }

    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        BrokenItems_Equip(play, sCursor);
    }

    if (CHECK_BTN_ALL(input->press.button, BrokenItems_ToggleBtn())) {
        BrokenItems_Exit(play); // same in-page button toggles back to the Map
    } else if (CHECK_BTN_ALL(input->press.button, BTN_B) ||
               CHECK_BTN_ALL(input->press.button, BTN_START)) {
        sActive = 0; // let the normal pause-close path handle B / START
    }
}

// ===========================================================================
// Drawing
// ===========================================================================

// Item icon for each mode (the selectors). Pikachu uses its own custom texture
// (gPikaIconPikachuTex), resolved in the draw loop instead of via item id.
static u16 BrokenItems_ModeIcon(s32 mode) {
    return (mode == BROKEN_MODE_MARIO) ? ITEM_MARIO_MASK : ITEM_OCARINA_TIME;
}

static void* BrokenItems_ModeIconTex(s32 mode) {
    if (mode == BROKEN_MODE_PIKACHU) {
        // Custom icon lives in soh.o2r (textures/pikachu/). If the archive
        // hasn't been repacked with it yet, fall back to the pokeball item
        // icon (always shipped) instead of feeding Fast3D a missing path.
        extern uint8_t ResourceMgr_FileExists(const char* resName);
        if (ResourceMgr_FileExists(dgPikaIconPikachuTex)) {
            return (void*)gPikaIconPikachuTex;
        }
        return ExtInv_GetItemIcon(ITEM_POKEBALL);
    }
    return ExtInv_GetItemIcon(BrokenItems_ModeIcon(mode));
}

// ---- 2D fill primitives (take + return the display-list ptr) ----------------

// Enters a translucent fill mode (primitive color only). Mirrors the vanilla
// TransitionFade setup (z_fbdemo_fade.c): 1CYCLE + CLD_SURF + combine = PRIM.
static Gfx* BI_BeginFill(Gfx* gfx) {
    gDPPipeSync(gfx++);
    gDPSetCombineLERP(gfx++, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    gDPSetRenderMode(gfx++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
    return gfx;
}
static Gfx* BI_FillRect(Gfx* gfx, s32 x0, s32 y0, s32 x1, s32 y1, u8 r, u8 g, u8 b, u8 a) {
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, a);
    gDPFillRectangle(gfx++, x0, y0, x1, y1);
    return gfx;
}
// Hollow border of thickness t (4 thin rects).
static Gfx* BI_FrameRect(Gfx* gfx, s32 x0, s32 y0, s32 x1, s32 y1, s32 t, u8 r, u8 g, u8 b, u8 a) {
    gfx = BI_FillRect(gfx, x0, y0, x1, y0 + t, r, g, b, a);
    gfx = BI_FillRect(gfx, x0, y1 - t, x1, y1, r, g, b, a);
    gfx = BI_FillRect(gfx, x0, y0, x0 + t, y1, r, g, b, a);
    gfx = BI_FillRect(gfx, x1 - t, y0, x1, y1, r, g, b, a);
    return gfx;
}

// Overlay layout in vanilla 320x240 space (centered on widescreen via ox).
#define BI_BOX_W   56
#define BI_BOX_H   56
#define BI_BOX_Y   40
#define BI_ICON_INSET 12 // 32px icon inside a 56 box

// Three slots centered on x=160 with an 80px pitch: lefts 52 / 132 / 212.
static s32 BI_BoxX(s32 i) {
    return 52 + i * 80;
}

// Draws the WHOLE Broken Modes overlay (own dark backdrop + slot boxes + item
// icons + cursor + names + control map), on top of everything. Called at the
// very end of the kaleido draw so it covers the cube, prompts and info box.
void BrokenItems_DrawOverlay(PlayState* play) {
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    GfxPrint printer;
    const BrokenMode* mode;
    s32 ox, equipped, i;
    static u16 sPulse = 0;
    u8 cur;

    if (!sActive) {
        return;
    }

    ox = (gScreenWidth - SCREEN_WIDTH) / 2;
    if (ox < 0) {
        ox = 0;
    }
    equipped = BrokenItems_CurrentEquipped();
    mode = &sModes[sCursor];
    sPulse += 0x600;
    cur = (u8)(175.0f + 70.0f * Math_SinS(sPulse));

    OPEN_DISPS(gfxCtx);

    // ---- backdrop + panels (fill mode) --------------------------------------
    POLY_OPA_DISP = BI_BeginFill(POLY_OPA_DISP);
    // near-opaque dark backdrop covering the whole screen (hides the kaleido)
    POLY_OPA_DISP = BI_FillRect(POLY_OPA_DISP, 0, 0, gScreenWidth, gScreenHeight, 8, 11, 18, 246);
    // gold accent lines (top under the title, bottom above the footer)
    POLY_OPA_DISP = BI_FillRect(POLY_OPA_DISP, ox + 24, 24, ox + 296, 26, 217, 169, 63, 255);
    POLY_OPA_DISP = BI_FillRect(POLY_OPA_DISP, ox + 24, 176, ox + 296, 178, 217, 169, 63, 255);
    // two selector slot boxes + equipped (gold) / cursor (green, pulsing) frames
    for (i = 0; i < BROKEN_MODE_COUNT; i++) {
        s32 bx = ox + BI_BoxX(i);
        POLY_OPA_DISP = BI_FillRect(POLY_OPA_DISP, bx, BI_BOX_Y, bx + BI_BOX_W, BI_BOX_Y + BI_BOX_H, 30, 38, 24, 235);
        if (i == equipped) {
            POLY_OPA_DISP = BI_FrameRect(POLY_OPA_DISP, bx - 2, BI_BOX_Y - 2, bx + BI_BOX_W + 2, BI_BOX_Y + BI_BOX_H + 2,
                                         2, 217, 169, 63, 255);
        }
        if (i == sCursor) {
            POLY_OPA_DISP = BI_FrameRect(POLY_OPA_DISP, bx - 4, BI_BOX_Y - 4, bx + BI_BOX_W + 4, BI_BOX_Y + BI_BOX_H + 4,
                                         3, 125, 255, 90, cur);
        }
    }

    // ---- item icons (2D, centered in each box) ------------------------------
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetTextureFilter(POLY_OPA_DISP++, G_TF_BILERP);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);
    for (i = 0; i < BROKEN_MODE_COUNT; i++) {
        void* icon = BrokenItems_ModeIconTex(i);
        s32 ix = ox + BI_BoxX(i) + BI_ICON_INSET;
        s32 iy = BI_BOX_Y + BI_ICON_INSET;
        if (icon == NULL) {
            continue;
        }
        gDPLoadTextureBlock(POLY_OPA_DISP++, icon, G_IM_FMT_RGBA, G_IM_SIZ_32b, 32, 32, 0, G_TX_NOMIRROR | G_TX_WRAP,
                            G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        gSPTextureRectangle(POLY_OPA_DISP++, ix << 2, iy << 2, (ix + 32) << 2, (iy + 32) << 2, G_TX_RENDERTILE, 0, 0,
                            1 << 10, 1 << 10);
    }

    // ---- text ---------------------------------------------------------------
    gDPPipeSync(POLY_OPA_DISP++);
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, POLY_OPA_DISP);
    printer.baseX = ox * 4; // GfxPrint posX is in quarter-pixels

    // title (centered)
    GfxPrint_SetColor(&printer, 255, 233, 160, 255);
    GfxPrint_SetPos(&printer, 14, 1);
    GfxPrint_Printf(&printer, "%s", "BROKEN MODES");

    // selected mode name (centered) + ON tag, below the slot boxes
    GfxPrint_SetColor(&printer, 255, 255, 255, 255);
    GfxPrint_SetPos(&printer, 15, 13);
    GfxPrint_Printf(&printer, "%s", mode->name);
    if (sCursor == equipped) {
        GfxPrint_SetColor(&printer, 125, 255, 90, 255);
        GfxPrint_SetPos(&printer, 26, 13);
        GfxPrint_Printf(&printer, "%s", "[ON]");
    }

    // control map (header + two balanced columns; %-6s = gap between button and
    // action). perCol scales with the list so 10-entry movesets (Pikachu) fit
    // in rows 17..21 without colliding with the footer at 23.
    GfxPrint_SetColor(&printer, 255, 233, 160, 255);
    GfxPrint_SetPos(&printer, 16, 15);
    GfxPrint_Printf(&printer, "%s", "CONTROLS");
    {
        s32 perCol = (mode->controlCount + 1) / 2;
        for (i = 0; i < mode->controlCount; i++) {
            s32 col = (i < perCol) ? 3 : 21;
            s32 row = 17 + (i % perCol);
            GfxPrint_SetColor(&printer, 230, 230, 220, 255);
            GfxPrint_SetPos(&printer, col, row);
            GfxPrint_Printf(&printer, "%-6s%s", mode->controls[i].btn, mode->controls[i].action);
        }
    }

    // footer hint
    GfxPrint_SetColor(&printer, 180, 180, 180, 255);
    GfxPrint_SetPos(&printer, 6, 23);
    GfxPrint_Printf(&printer, "A EQUIP    %s MAP    B CLOSE",
                    (BrokenItems_ToggleBtn() == BTN_L) ? "L" : "Z");

    POLY_OPA_DISP = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);

    CLOSE_DISPS(gfxCtx);
}
