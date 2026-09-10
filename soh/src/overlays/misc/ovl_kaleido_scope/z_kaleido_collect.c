#include "z_kaleido_scope.h"
#include <string.h> // Skijer's NEI: memcpy/strlen (MM name panel injection)
#include "textures/parameter_static/parameter_static.h"
#include "textures/icon_item_static/icon_item_static.h"
#include "textures/icon_item_24_static/icon_item_24_static.h" // dgQuestIconGoldSkulltulaTex (quad 21)
#include "soh/Enhancements/cosmetics/cosmeticsTypes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "mods/extended_inventory.h"
#include "mods/spiritual_stones/spiritual_stones.h"
#include "expansions/sw97/sw97_config.h"

extern const char* digitTextures[];

// Skijer's NEI: MM quest-status page (L-flip on the quest page; implementation at the end of this
// file — mirror of the 2ship-side OoT quest page).
void KaleidoScope_DrawMmQuestStatus(PlayState* play, GraphicsContext* gfxCtx);
// Skijer's NEI "Pause Play" (instant play): close the menu and auto-play the song in-world through
// the native FREE_PLAY success flow — for OoT's own songs this fires their REAL effects (warp songs
// warp, Sun's/Storms act) because OoT free-play handles them natively. Defined at the end of this file.
static void NeiPausePlay_Start(PlayState* play, u8 ocarinaSlot);

// Skijer 2026-07-29: the old "arrow mode" toggle is GONE. Picking which elemental arrow rides a
// C button is the job of the SW97 wheel on the item page (z_kaleido_item.c), so the quest page's
// shoulder button is free for the MM/OoT layout flip and a C press here always equips the medallion
// itself (the spell).

/**
 * SW97 Medallion equipping: when the CVar is enabled and the cursor is on a medallion the player
 * has, pressing a C-button equips that medallion (its spell) to that C-slot.
 * Returns true if a medallion was equipped (consume the input).
 */
static s32 Sw97_TryEquipMedallion(PlayState* play, Input* input) {
    if (!SW97_MEDALLIONS_ENABLED()) {
        return false;
    }

    PauseContext* pauseCtx = &play->pauseCtx;
    s16 cursorPoint = pauseCtx->cursorPoint[PAUSE_QUEST];

    // Only medallions (cursor points 0-5)
    if (cursorPoint < 0 || cursorPoint > 5) {
        return false;
    }

    // Must have the medallion
    if (!CHECK_QUEST_ITEM(cursorPoint)) {
        return false;
    }

    // Detect C-button or DPad press
    s32 targetCBtn = -1;
    if (CHECK_BTN_ALL(input->press.button, BTN_CLEFT)) {
        targetCBtn = 0;
    } else if (CHECK_BTN_ALL(input->press.button, BTN_CDOWN)) {
        targetCBtn = 1;
    } else if (CHECK_BTN_ALL(input->press.button, BTN_CRIGHT)) {
        targetCBtn = 2;
    } else if (CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0)) {
        if (CHECK_BTN_ALL(input->press.button, BTN_DUP)) {
            targetCBtn = 3;
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DDOWN)) {
            targetCBtn = 4;
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DLEFT)) {
            targetCBtn = 5;
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DRIGHT)) {
            targetCBtn = 6;
        }
    }

    if (targetCBtn < 0) {
        return false;
    }

    // Map cursor point (0-5) to medallion item ID
    // 0=Forest(0x66), 1=Fire(0x67), 2=Water(0x68), 3=Spirit(0x69), 4=Shadow(0x6A), 5=Light(0x6B)
    s32 medallionItem = cursorPoint + ITEM_MEDALLION_FOREST;

    s32 itemToEquip = medallionItem;

    // Equip to the target button
    s32 targetButtonIndex = targetCBtn + 1; // buttonItems[0] is B button
    gSaveContext.equips.buttonItems[targetButtonIndex] = itemToEquip;
    gSaveContext.equips.cButtonSlots[targetCBtn] = 0xFF; // Special marker: not from inventory
    Interface_LoadItemIcon1(play, targetButtonIndex);

    Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    return true;
}

void KaleidoScope_DrawQuestStatus(PlayState* play, GraphicsContext* gfxCtx) {
    // Skijer's NEI: L on the quest page flips to MM's Quest Status layout (mirror of the 2ship-side
    // OoT page). The flip persists via CVar; the MM page fully owns input/draw while active.
    //
    // 2026-07-29 FIX: this draw function runs for every kaleido page (the pages are all drawn while
    // the cube rotates), so an ungated L press flipped the quest layout from the item/map/equipment
    // pages too. 2ship does the flip inside the INPUT handler gated on PAUSE_QUEST
    // (z_kaleido_scope_NES.c); gate on the active page here for the same behavior.
    if ((play->pauseCtx.pageIndex == PAUSE_QUEST) && (play->pauseCtx.cursorSpecialPos == 0) &&
        (play->pauseCtx.state == 6) && (play->pauseCtx.unk_1E4 == 0) &&
        CHECK_BTN_ALL(play->state.input[0].press.button, BTN_L)) {
        // Don't flip pages mid-song (unk_1E4 != 0 = a demonstration/prompt is running).
        CVarSetInteger(CVAR_ENHANCEMENT("SkijerNEI.MmQuestPage"),
                       !CVarGetInteger(CVAR_ENHANCEMENT("SkijerNEI.MmQuestPage"), 0));
        Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
    if (CVarGetInteger(CVAR_ENHANCEMENT("SkijerNEI.MmQuestPage"), 0)) {
        KaleidoScope_DrawMmQuestStatus(play, gfxCtx);
        return;
    }

    static s16 D_8082A070[][4] = {
        { 255, 0, 0, 255 },
        { 255, 70, 0, 150 },
        { 255, 70, 0, 150 },
        { 255, 0, 0, 255 },
    };
    static s16 D_8082A090[][3] = {
        { 0, 0, 0 },  { 0, 0, 0 },  { 0, 0, 0 },    { 0, 0, 0 },   { 0, 0, 0 },   { 0, 0, 0 },
        { 0, 60, 0 }, { 90, 0, 0 }, { 0, 40, 110 }, { 80, 40, 0 }, { 70, 0, 90 }, { 90, 90, 0 },
    };
    static s16 D_8082A0D8[] = { 255, 255, 255, 255, 255, 255 };
    static s16 D_8082A0E4[] = { 255, 255, 255, 255, 255, 255 };
    static s16 D_8082A0F0[] = { 150, 150, 150, 150, 150, 150 };
    static s16 D_8082A0FC = 20;
    static s16 D_8082A100 = 0;
    static s16 D_8082A104 = 0;
    static s16 D_8082A108 = 0;
    static s16 D_8082A10C = 0;
    static s16 D_8082A110 = 0;
    static s16 D_8082A114 = 20;
    static s16 D_8082A118 = 0;
    static s16 D_8082A11C = 0;
    static s16 D_8082A120 = 0;
    static u8 D_8082A124[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    static void* D_8082A130[] = {
        gOcarinaBtnIconATex,     gOcarinaBtnIconCDownTex, gOcarinaBtnIconCRightTex,
        gOcarinaBtnIconCLeftTex, gOcarinaBtnIconCUpTex,
    };
    static u16 D_8082A144[] = {
        0xFFCC, 0xFFCC, 0xFFCC, 0xFFCC, 0xFFCC,
    };
    static s16 D_8082A150[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    static s16 D_8082A164[] = {
        150, 255, 100, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    };
    static s16 D_8082A17C[] = {
        255, 80, 150, 160, 100, 240, 255, 255, 255, 255, 255, 255,
    };
    static s16 D_8082A194[] = {
        100, 40, 255, 0, 255, 100, 255, 255, 255, 255, 255, 255,
    };
    static s8 D_8082A1AC[][4] = {
        { 0x05, 0x01, 0x05, 0xFE }, { 0x00, 0x02, 0x02, 0xFE }, { 0xFF, 0x13, 0x03, 0x01 }, { 0x04, 0x02, 0x11, 0x02 },
        { 0x05, 0x03, 0x18, 0x05 }, { 0xFF, 0xFF, 0x04, 0x00 }, { 0x0C, 0xFF, 0xFD, 0x07 }, { 0x0D, 0xFF, 0x06, 0x08 },
        { 0x0E, 0xFF, 0x07, 0x09 }, { 0x0F, 0xFF, 0x08, 0x0A }, { 0x10, 0xFF, 0x09, 0x0B }, { 0x11, 0xFF, 0x0A, 0x12 },
        { 0x17, 0x06, 0xFD, 0x0D }, { 0x17, 0x07, 0x0C, 0x0E }, { 0x17, 0x08, 0x0D, 0x0F }, { 0x18, 0x09, 0x0E, 0x10 },
        { 0x18, 0x0A, 0x0F, 0x11 }, { 0x18, 0x0B, 0x10, 0x03 }, { 0x02, 0xFF, 0x0B, 0x13 }, { 0x02, 0xFF, 0x12, 0x14 },
        { 0x02, 0xFF, 0x13, 0xFE }, { 0xFF, 0x17, 0xFD, 0x16 }, { 0xFF, 0x17, 0x15, 0x18 }, { 0x15, 0x0C, 0xFD, 0x18 },
        { 0xFF, 0x10, 0x16, 0x04 }, { 0x00, 0x00, 0x00, 0x00 },
    };
    PauseContext* pauseCtx = &play->pauseCtx;
    Input* input = &play->state.input[0];
    s16 sp226;
    s16 sp224;
    s16 sp222;
    s16 sp220;
    s16 phi_s0;
    s16 phi_s3;
    s16 sp21A;
    s16 sp218;
    s16 sp216;
    s16 pad1;
    s16 phi_v1;
    s16 pad2;
    s16 phi_s0_2;
    s16 sp208[3];

    OPEN_DISPS(gfxCtx);

    if (((pauseCtx->unk_1E4 == 0) || (pauseCtx->unk_1E4 == 5) || (pauseCtx->unk_1E4 == 8)) &&
        (pauseCtx->pageIndex == PAUSE_QUEST)) {
        if (CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0)) {
            if (CHECK_BTN_ALL(input->press.button, BTN_DLEFT)) {
                pauseCtx->stickRelX = -35;
            } else if (CHECK_BTN_ALL(input->press.button, BTN_DRIGHT)) {
                pauseCtx->stickRelX = 35;
            } else if (CHECK_BTN_ALL(input->press.button, BTN_DDOWN)) {
                pauseCtx->stickRelY = -35;
            } else if (CHECK_BTN_ALL(input->press.button, BTN_DUP)) {
                pauseCtx->stickRelY = 35;
            }
        }
        pauseCtx->cursorColorSet = 0;

        if (pauseCtx->cursorSpecialPos == 0) {
            pauseCtx->nameColorSet = 0;
            if ((pauseCtx->state != 6) || ((pauseCtx->stickRelX == 0) && (pauseCtx->stickRelY == 0))) {
                // No cursor movement
                sp216 = pauseCtx->cursorSlot[PAUSE_QUEST];
            } else {
                phi_s3 = pauseCtx->cursorPoint[PAUSE_QUEST];

                if ((pauseCtx->stickRelX < -30)) {
                    phi_s0 = D_8082A1AC[phi_s3][2];
                    if (phi_s0 == -3) {
                        KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_LEFT);
                        pauseCtx->unk_1E4 = 0;
                    } else {
                        while (phi_s0 >= 0) {
                            if ((s16)KaleidoScope_UpdateQuestStatusPoint(pauseCtx, phi_s0) != 0) {
                                break;
                            }
                            phi_s0 = D_8082A1AC[phi_s0][2];
                        }
                    }
                } else if ((pauseCtx->stickRelX > 30)) {
                    phi_s0 = D_8082A1AC[phi_s3][3];
                    if (phi_s0 == -2) {
                        KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_RIGHT);
                        pauseCtx->unk_1E4 = 0;
                    } else {
                        while (phi_s0 >= 0) {
                            if ((s16)KaleidoScope_UpdateQuestStatusPoint(pauseCtx, phi_s0) != 0) {
                                break;
                            }
                            phi_s0 = D_8082A1AC[phi_s0][3];
                        }
                    }
                }

                if ((pauseCtx->stickRelY < -30)) {
                    phi_s0 = D_8082A1AC[phi_s3][1];
                    while (phi_s0 >= 0) {
                        if ((s16)KaleidoScope_UpdateQuestStatusPoint(pauseCtx, phi_s0) != 0) {
                            break;
                        }
                        phi_s0 = D_8082A1AC[phi_s0][1];
                    }
                } else if ((pauseCtx->stickRelY > 30)) {
                    phi_s0 = D_8082A1AC[phi_s3][0];
                    while (phi_s0 >= 0) {
                        if ((s16)KaleidoScope_UpdateQuestStatusPoint(pauseCtx, phi_s0) != 0) {
                            break;
                        }
                        phi_s0 = D_8082A1AC[phi_s0][0];
                    }
                }

                if (phi_s3 != pauseCtx->cursorPoint[PAUSE_QUEST]) {
                    pauseCtx->unk_1E4 = 0;
                    Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                }

                if (pauseCtx->cursorPoint[PAUSE_QUEST] != 0x18) {
                    if (CHECK_QUEST_ITEM(pauseCtx->cursorPoint[PAUSE_QUEST])) {
                        if (pauseCtx->cursorPoint[PAUSE_QUEST] < 6) {
                            phi_s0_2 = pauseCtx->cursorPoint[PAUSE_QUEST] + 0x66;
                            osSyncPrintf("000 ccc=%d\n", phi_s0_2);
                        } else if (pauseCtx->cursorPoint[PAUSE_QUEST] < 0x12) {
                            phi_s0_2 = pauseCtx->cursorPoint[PAUSE_QUEST] + 0x54;
                            osSyncPrintf("111 ccc=%d\n", phi_s0_2);
                        } else {
                            phi_s0_2 = pauseCtx->cursorPoint[PAUSE_QUEST] + 0x5A;
                            osSyncPrintf("222 ccc=%d (%d, %d, %d)\n", phi_s0_2, pauseCtx->cursorPoint[PAUSE_QUEST],
                                         0x12, 0x6C);
                        }
                    } else {
                        phi_s0_2 = PAUSE_ITEM_NONE;
                        osSyncPrintf("999 ccc=%d (%d,  %d)\n", PAUSE_ITEM_NONE, pauseCtx->cursorPoint[PAUSE_QUEST],
                                     0x18);
                    }
                } else {
                    if ((gSaveContext.inventory.questItems & 0xF0000000) != 0) {
                        phi_s0_2 = 0x72;
                    } else {
                        phi_s0_2 = PAUSE_ITEM_NONE;
                    }
                    osSyncPrintf("888 ccc=%d (%d,  %d,  %x)\n", phi_s0_2, pauseCtx->cursorPoint[PAUSE_QUEST], 0x72,
                                 gSaveContext.inventory.questItems & 0xF0000000);
                }

                sp216 = pauseCtx->cursorPoint[PAUSE_QUEST];
                pauseCtx->cursorItem[pauseCtx->pageIndex] = phi_s0_2;
                pauseCtx->cursorSlot[pauseCtx->pageIndex] = sp216;
            }

            KaleidoScope_SetCursorVtx(pauseCtx, sp216 * 4, pauseCtx->questVtx);

            // C-button equips the hovered medallion. The shoulder button belongs to the MM/OoT layout
            // flip now (Skijer 2026-07-29) — it no longer toggles an arrow mode here.
            if ((pauseCtx->state == 6) && (pauseCtx->unk_1E4 == 0) && (pauseCtx->cursorSpecialPos == 0)) {
                Sw97_TryEquipMedallion(play, input);
                // Spiritual stones: A toggles passive buff, C/DPad equips for warp use.
                SpiritualStone_TryToggleAtCursor(play, input);
                SpiritualStone_TryEquipAtCursor(play, input);
                // Quartz of Motion (Stone of Agony level 2): A opens the
                // tracking-category list. It takes over pauseCtx->unk_1E4 while
                // open, so the driving happens in z_kaleido_scope_PAL.c.
                {
                    extern s32 Quartz_TryOpenAtCursor(PlayState * play, Input * input);
                    Quartz_TryOpenAtCursor(play, input);
                }
            }

            if ((pauseCtx->state == 6) && (pauseCtx->unk_1E4 == 0) && (pauseCtx->cursorSpecialPos == 0)) {
                if ((sp216 >= QUEST_SONG_MINUET) && (sp216 < QUEST_KOKIRI_EMERALD)) {
                    if (CHECK_QUEST_ITEM(pauseCtx->cursorPoint[PAUSE_QUEST])) {
                        sp216 = pauseCtx->cursorSlot[PAUSE_QUEST];
                        pauseCtx->ocarinaSongIdx = gOcarinaSongItemMap[sp216 - QUEST_SONG_MINUET];
                        D_8082A120 = 10;

                        for (phi_s3 = 0; phi_s3 < 8; phi_s3++) {
                            D_8082A124[phi_s3] = 0xFF;
                            D_8082A150[phi_s3] = 0;
                        }

                        D_8082A11C = 0;
                        AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_DEFAULT);
                        AudioOcarina_Start((1 << pauseCtx->ocarinaSongIdx) + 0x8000);
                        pauseCtx->ocarinaStaff = AudioOcarina_GetPlaybackStaff();
                        pauseCtx->ocarinaStaff->pos = 0;
                        pauseCtx->ocarinaStaff->state = 0xFF;
                        VREG(21) = -62;
                        VREG(22) = -56;
                        VREG(23) = -49;
                        VREG(24) = -46;
                        VREG(25) = -41;
                        pauseCtx->unk_1E4 = 8;
                        AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                    }
                }
            } else if (pauseCtx->unk_1E4 == 5) {
                if ((pauseCtx->stickRelX != 0) || (pauseCtx->stickRelY != 0)) {
                    pauseCtx->unk_1E4 = 0;
                    AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                }
            } else if (pauseCtx->unk_1E4 == 8) {
                if (CHECK_BTN_ALL(input->press.button, BTN_A) && (sp216 >= QUEST_SONG_MINUET) &&
                    (sp216 < QUEST_KOKIRI_EMERALD)) {
                    // Skijer's NEI "Pause Play" (instant play): with the enhancement on and an
                    // ocarina owned, A leaves the menu and plays the song in-world instantly —
                    // OoT's native free-play then runs the song's REAL effect (warp songs warp).
                    // ocarinaSongIdx was set when the cursor landed on this song.
                    if (CVarGetInteger(CVAR_ENHANCEMENT("SkijerNEI.PausePlay"), 0) &&
                        (INV_CONTENT(ITEM_OCARINA_TIME) != ITEM_NONE)) {
                        NeiPausePlay_Start(play, pauseCtx->ocarinaSongIdx);
                    } else {
                        pauseCtx->unk_1E4 = 9;
                        D_8082A120 = 10;
                    }
                }
            }
        } else if (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_LEFT) {
            if ((pauseCtx->stickRelX > 30)) {
                pauseCtx->cursorPoint[PAUSE_QUEST] = 0x15;
                pauseCtx->nameDisplayTimer = 0;
                pauseCtx->cursorSpecialPos = 0;
                sp216 = pauseCtx->cursorPoint[PAUSE_QUEST];
                KaleidoScope_SetCursorVtx(pauseCtx, sp216 * 4, pauseCtx->questVtx);
                Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                if (CHECK_QUEST_ITEM(pauseCtx->cursorPoint[PAUSE_QUEST])) {
                    phi_s0_2 = pauseCtx->cursorPoint[PAUSE_QUEST] + 0x5A;
                } else {
                    phi_s0_2 = PAUSE_ITEM_NONE;
                }
                sp216 = pauseCtx->cursorPoint[PAUSE_QUEST];
                pauseCtx->cursorItem[pauseCtx->pageIndex] = phi_s0_2;
                pauseCtx->cursorSlot[pauseCtx->pageIndex] = sp216;
            }
        } else {
            if ((pauseCtx->stickRelX < -30)) {
                pauseCtx->cursorPoint[PAUSE_QUEST] = 0;
                pauseCtx->nameDisplayTimer = 0;
                pauseCtx->cursorSpecialPos = 0;
                sp216 = pauseCtx->cursorPoint[PAUSE_QUEST];
                KaleidoScope_SetCursorVtx(pauseCtx, sp216 * 4, pauseCtx->questVtx);
                Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                if (CHECK_QUEST_ITEM(pauseCtx->cursorPoint[PAUSE_QUEST])) {
                    if (pauseCtx->cursorPoint[PAUSE_QUEST] < 6) {
                        phi_s0_2 = pauseCtx->cursorPoint[PAUSE_QUEST] + 0x66;
                    } else if (pauseCtx->cursorPoint[PAUSE_QUEST] < 0xC) {
                        phi_s0_2 = pauseCtx->cursorPoint[PAUSE_QUEST] + 0x4E;
                    } else {
                        phi_s0_2 = pauseCtx->cursorPoint[PAUSE_QUEST] + 0x69;
                    }
                } else {
                    phi_s0_2 = PAUSE_ITEM_NONE;
                }
                sp216 = pauseCtx->cursorPoint[PAUSE_QUEST];
                pauseCtx->cursorItem[pauseCtx->pageIndex] = phi_s0_2;
                pauseCtx->cursorSlot[pauseCtx->pageIndex] = sp216;
            }
        }

    } else {
        if (pauseCtx->unk_1E4 == 9) {
            pauseCtx->cursorColorSet = 8;

            if (--D_8082A120 == 0) {
                for (phi_s3 = 0; phi_s3 < 8; phi_s3++) {
                    D_8082A124[phi_s3] = 0xFF;
                    D_8082A150[phi_s3] = 0;
                }

                D_8082A11C = 0;
                VREG(21) = -62;
                VREG(22) = -56;
                VREG(23) = -49;
                VREG(24) = -46;
                VREG(25) = -41;
                sp216 = pauseCtx->cursorSlot[PAUSE_QUEST];
                AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_DEFAULT);
                AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_DEFAULT);
                pauseCtx->ocarinaSongIdx = gOcarinaSongItemMap[sp216 - QUEST_SONG_MINUET];
                AudioOcarina_SetPlaybackSong(pauseCtx->ocarinaSongIdx + 1, 1);
                pauseCtx->unk_1E4 = 2;
                pauseCtx->ocarinaStaff = AudioOcarina_GetPlaybackStaff();
                pauseCtx->ocarinaStaff->pos = 0;
                sp216 = pauseCtx->cursorSlot[PAUSE_QUEST];
                KaleidoScope_SetCursorVtx(pauseCtx, sp216 * 4, pauseCtx->questVtx);
            }
        } else {
            sp216 = pauseCtx->cursorSlot[PAUSE_QUEST];
            KaleidoScope_SetCursorVtx(pauseCtx, sp216 * 4, pauseCtx->questVtx);
        }
    }

    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
    gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);

    D_8082A0FC--;

    for (sp218 = 0, sp21A = 0; sp218 < 6; sp218++, sp21A += 4) {
        if ((D_8082A100 != 1) && (D_8082A100 != 3)) {
            phi_v1 = (D_8082A100 != 0) ? sp218 + 6 : sp218;

            if (D_8082A0FC != 0) {
                sp226 = ABS(D_8082A0D8[sp218] - D_8082A090[phi_v1][0]) / D_8082A0FC;
                sp224 = ABS(D_8082A0E4[sp218] - D_8082A090[phi_v1][1]) / D_8082A0FC;
                sp222 = ABS(D_8082A0F0[sp218] - D_8082A090[phi_v1][2]) / D_8082A0FC;
                if (D_8082A0D8[sp218] >= D_8082A090[phi_v1][0]) {
                    D_8082A0D8[sp218] -= sp226;
                } else {
                    D_8082A0D8[sp218] += sp226;
                }
                if (D_8082A0E4[sp218] >= D_8082A090[phi_v1][1]) {
                    D_8082A0E4[sp218] -= sp224;
                } else {
                    D_8082A0E4[sp218] += sp224;
                }
                if (D_8082A0F0[sp218] >= D_8082A090[phi_v1][2]) {
                    D_8082A0F0[sp218] -= sp222;
                } else {
                    D_8082A0F0[sp218] += sp222;
                }
            } else {
                D_8082A0D8[sp218] = D_8082A090[phi_v1][0];
                D_8082A0E4[sp218] = D_8082A090[phi_v1][1];
                D_8082A0F0[sp218] = D_8082A090[phi_v1][2];
            }
        }

        if (CHECK_QUEST_ITEM(sp218)) {
            gDPPipeSync(POLY_OPA_DISP++);
            gDPSetEnvColor(POLY_OPA_DISP++, D_8082A0D8[sp218], D_8082A0E4[sp218], D_8082A0F0[sp218], 0);
            gSPVertex(POLY_OPA_DISP++, &pauseCtx->questVtx[sp21A], 4, 0);

            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
            KaleidoScope_DrawQuadTextureRGBA32(gfxCtx, ExtInv_GetItemIcon(ITEM_MEDALLION_FOREST + sp218), 24, 24, 0);
        }
    }

    if (D_8082A0FC == 0) {
        D_8082A0FC = ZREG(61 + D_8082A100);
        if (++D_8082A100 >= 4) {
            D_8082A100 = 0;
        }
    }

    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
    gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 255);

    gDPLoadTextureBlock(POLY_OPA_DISP++, gSongNoteTex, G_IM_FMT_IA, G_IM_SIZ_8b, 16, 24, 0, G_TX_NOMIRROR | G_TX_WRAP,
                        G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);

    for (sp218 = 0; sp218 < QUEST_KOKIRI_EMERALD - QUEST_SONG_MINUET; sp218++, sp21A += 4) {
        if (CHECK_QUEST_ITEM(sp218 + QUEST_SONG_MINUET)) {
            if ((sp218 + QUEST_SONG_MINUET) == sp216) {
                pauseCtx->questVtx[sp21A + 0].v.ob[0] = pauseCtx->questVtx[sp21A + 2].v.ob[0] =
                    pauseCtx->questVtx[sp21A + 0].v.ob[0] - 2;

                pauseCtx->questVtx[sp21A + 1].v.ob[0] = pauseCtx->questVtx[sp21A + 3].v.ob[0] =
                    pauseCtx->questVtx[sp21A + 1].v.ob[0] + 4;

                pauseCtx->questVtx[sp21A + 0].v.ob[1] = pauseCtx->questVtx[sp21A + 1].v.ob[1] =
                    pauseCtx->questVtx[sp21A + 0].v.ob[1] + 2;

                pauseCtx->questVtx[sp21A + 2].v.ob[1] = pauseCtx->questVtx[sp21A + 3].v.ob[1] =
                    pauseCtx->questVtx[sp21A + 2].v.ob[1] - 4;
            }

            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, D_8082A164[sp218], D_8082A17C[sp218], D_8082A194[sp218],
                            pauseCtx->alpha);
            gSPVertex(POLY_OPA_DISP++, &pauseCtx->questVtx[sp21A], 4, 0);
            gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);
        }
    }

    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
    gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 255);

    for (sp218 = 0; sp218 < 3; sp218++, sp21A += 4) {
        if (CHECK_QUEST_ITEM(sp218 + 0x12)) {
            // Visual cue: stones whose passive is OFF render at 50% alpha so
            // the player can see at a glance which buffs are equipped.
            u8 stoneAlpha = SpiritualStone_IsPassiveActive(sp218) ? pauseCtx->alpha : (pauseCtx->alpha >> 1);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, stoneAlpha);
            gSPVertex(POLY_OPA_DISP++, &pauseCtx->questVtx[sp21A], 4, 0);
            KaleidoScope_DrawQuadTextureRGBA32(gfxCtx, ExtInv_GetItemIcon(ITEM_KOKIRI_EMERALD + sp218), 24, 24, 0);
        }
    }

    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);

    for (sp218 = 0; sp218 < 3; sp218++, sp21A += 4) {
        if (CHECK_QUEST_ITEM(sp218 + 0x15)) {
            gSPVertex(POLY_OPA_DISP++, &pauseCtx->questVtx[sp21A], 4, 0);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
            KaleidoScope_DrawQuadTextureRGBA32(gfxCtx, ExtInv_GetItemIcon(ITEM_STONE_OF_AGONY + sp218), 24, 24, 0);
        }
    }

    sp226 = ABS(D_8082A104 - D_8082A070[D_8082A118][0]) / D_8082A114;
    sp224 = ABS(D_8082A108 - D_8082A070[D_8082A118][1]) / D_8082A114;
    sp222 = ABS(D_8082A10C - D_8082A070[D_8082A118][2]) / D_8082A114;
    sp220 = ABS(D_8082A110 - D_8082A070[D_8082A118][3]) / D_8082A114;
    if (D_8082A104 >= D_8082A070[D_8082A118][0]) {
        D_8082A104 -= sp226;
    } else {
        D_8082A104 += sp226;
    }
    if (D_8082A108 >= D_8082A070[D_8082A118][1]) {
        D_8082A108 -= sp224;
    } else {
        D_8082A108 += sp224;
    }
    if (D_8082A10C >= D_8082A070[D_8082A118][2]) {
        D_8082A10C -= sp222;
    } else {
        D_8082A10C += sp222;
    }
    if (D_8082A110 >= D_8082A070[D_8082A118][3]) {
        D_8082A110 -= sp220;
    } else {
        D_8082A110 += sp220;
    }

    if (--D_8082A114 == 0) {
        D_8082A104 = D_8082A070[D_8082A118][0];
        D_8082A108 = D_8082A070[D_8082A118][1];
        D_8082A10C = D_8082A070[D_8082A118][2];
        D_8082A110 = D_8082A070[D_8082A118][3];
        D_8082A114 = ZREG(24 + D_8082A118);
        if (++D_8082A118 >= 4) {
            D_8082A118 = 0;
        }
    }

    if ((gSaveContext.inventory.questItems >> 0x1C) != 0) {
        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                          PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);

        if ((pauseCtx->state == 4) || (pauseCtx->state == 0x12)) {
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, D_8082A070[0][0], D_8082A070[0][1], D_8082A070[0][2],
                            pauseCtx->alpha);
        } else {
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, D_8082A104, D_8082A108, D_8082A10C, D_8082A110);
        }

        gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 255);
        gSPVertex(POLY_OPA_DISP++, &pauseCtx->questVtx[sp21A], 4, 0);

        POLY_OPA_DISP = KaleidoScope_QuadTextureIA8(
            POLY_OPA_DISP,
            ExtInv_GetItemIcon(0x79 + (((gSaveContext.inventory.questItems & 0xF0000000) & 0xF0000000) >> 0x1C)), 48,
            48, 0);
    }

    if (pauseCtx->state == 6) {
        Color_RGB8 aButtonColor = { 80, 150, 255 };
        if (CVarGetInteger(CVAR_COSMETIC("HUD.AButton.Changed"), 0)) {
            aButtonColor = CVarGetColor24(CVAR_COSMETIC("HUD.AButton.Value"), aButtonColor);
        } else if (CVarGetInteger(CVAR_COSMETIC("DefaultColorScheme"), COLORSCHEME_N64) == COLORSCHEME_GAMECUBE) {
            aButtonColor = (Color_RGB8){ 80, 255, 150 };
        }
        if (!GameInteractor_Should(VB_HAVE_OCARINA_NOTE_D4, true)) {
            aButtonColor = (Color_RGB8){ 191, 191, 191 };
        }

        Color_RGB8 cButtonsColor = { 255, 255, 50 };
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CButtons.Changed"), 0)) {
            cButtonsColor = CVarGetColor24(CVAR_COSMETIC("HUD.CButtons.Value"), cButtonsColor);
        }
        Color_RGB8 cUpButtonColor = cButtonsColor;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CUpButton.Changed"), 0)) {
            cUpButtonColor = CVarGetColor24(CVAR_COSMETIC("HUD.CUpButton.Value"), cUpButtonColor);
        }
        if (!GameInteractor_Should(VB_HAVE_OCARINA_NOTE_D5, true)) {
            cUpButtonColor = (Color_RGB8){ 191, 191, 191 };
        }

        Color_RGB8 cDownButtonColor = cButtonsColor;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.Changed"), 0)) {
            cDownButtonColor = CVarGetColor24(CVAR_COSMETIC("HUD.CDownButton.Value"), cDownButtonColor);
        }
        if (!GameInteractor_Should(VB_HAVE_OCARINA_NOTE_F4, true)) {
            cDownButtonColor = (Color_RGB8){ 191, 191, 191 };
        }

        Color_RGB8 cLeftButtonColor = cButtonsColor;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.Changed"), 0)) {
            cLeftButtonColor = CVarGetColor24(CVAR_COSMETIC("HUD.CLeftButton.Value"), cLeftButtonColor);
        }
        if (!GameInteractor_Should(VB_HAVE_OCARINA_NOTE_B4, true)) {
            cLeftButtonColor = (Color_RGB8){ 191, 191, 191 };
        }

        Color_RGB8 cRightButtonColor = cButtonsColor;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.Changed"), 0)) {
            cRightButtonColor = CVarGetColor24(CVAR_COSMETIC("HUD.CRightButton.Value"), cRightButtonColor);
        }
        if (!GameInteractor_Should(VB_HAVE_OCARINA_NOTE_A4, true)) {
            cRightButtonColor = (Color_RGB8){ 191, 191, 191 };
        }

        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

        sp21A += 4;
        if ((pauseCtx->cursorSpecialPos == 0) && (sp216 >= 6) && (sp216 < 0x12)) {
            if ((pauseCtx->unk_1E4 < 3) || (pauseCtx->unk_1E4 == 5) || (pauseCtx->unk_1E4 == 8)) {
                if (pauseCtx->cursorItem[pauseCtx->pageIndex] != PAUSE_ITEM_NONE) {
                    pauseCtx->cursorColorSet = 8;
                    if ((pauseCtx->unk_1E4 >= 2) && (pauseCtx->unk_1E4 < 7)) {
                        pauseCtx->cursorColorSet = 0;
                    }
                }
            }
        }

        if (pauseCtx->unk_1E4 == 2) {
            // Draw ocarina buttons as the song playback progresses
            // QUEST_QUAD_SONG_NOTE_A1 to QUEST_QUAD_SONG_NOTE_A8

            pauseCtx->ocarinaStaff = AudioOcarina_GetPlaybackStaff();

            if (pauseCtx->ocarinaStaff->pos != 0) {
                if (D_8082A11C + 1 == pauseCtx->ocarinaStaff->pos) {
                    D_8082A11C++;
                    D_8082A124[pauseCtx->ocarinaStaff->pos - 1] = pauseCtx->ocarinaStaff->buttonIndex;
                }

                for (sp218 = 0, phi_s3 = 0; sp218 < 8; sp218++, phi_s3 += 4, sp21A += 4) {
                    if (D_8082A124[sp218] == 0xFF) {
                        break;
                    }

                    if (D_8082A150[sp218] != 255) {
                        D_8082A150[sp218] += VREG(50);
                        if (D_8082A150[sp218] >= 255) {
                            D_8082A150[sp218] = 255;
                        }
                    }

                    pauseCtx->questVtx[sp21A + 0].v.ob[1] = pauseCtx->questVtx[sp21A + 1].v.ob[1] =
                        VREG(21 + D_8082A124[sp218]);

                    pauseCtx->questVtx[sp21A + 2].v.ob[1] = pauseCtx->questVtx[sp21A + 3].v.ob[1] =
                        pauseCtx->questVtx[sp21A + 0].v.ob[1] - 12;

                    gDPPipeSync(POLY_OPA_DISP++);

                    s16 Notes_alpha = D_8082A150[sp218];
                    if (D_8082A124[sp218] == 0) {
                        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, aButtonColor.r, aButtonColor.g, aButtonColor.b,
                                        Notes_alpha);
                    } else {
                        if (D_8082A124[sp218] == OCARINA_BTN_C_UP) {
                            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cUpButtonColor.r, cUpButtonColor.g, cUpButtonColor.b,
                                            Notes_alpha);
                        } else if (D_8082A124[sp218] == OCARINA_BTN_C_LEFT) {
                            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cLeftButtonColor.r, cLeftButtonColor.g,
                                            cLeftButtonColor.b, Notes_alpha);
                        } else if (D_8082A124[sp218] == OCARINA_BTN_C_RIGHT) {
                            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cRightButtonColor.r, cRightButtonColor.g,
                                            cRightButtonColor.b, Notes_alpha);
                        } else if (D_8082A124[sp218] == OCARINA_BTN_C_DOWN) {
                            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cDownButtonColor.r, cDownButtonColor.g,
                                            cDownButtonColor.b, Notes_alpha);
                        } else {
                            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cButtonsColor.r, cButtonsColor.g, cButtonsColor.b,
                                            Notes_alpha);
                        }
                    }

                    gDPSetEnvColor(POLY_OPA_DISP++, 10, 10, 10, 0);
                    gSPVertex(POLY_OPA_DISP++, &pauseCtx->questVtx[sp21A], 4, 0);

                    gDPLoadTextureBlock(POLY_OPA_DISP++, D_8082A130[D_8082A124[sp218]], G_IM_FMT_IA, G_IM_SIZ_8b, 16,
                                        16, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK,
                                        G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);

                    gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);
                }
            }
        } else if (((pauseCtx->unk_1E4 >= 4) && (pauseCtx->unk_1E4 <= 6)) || (pauseCtx->unk_1E4 == 8)) {
            // Draw the buttons for playing a song
            // QUEST_QUAD_SONG_NOTE_A1 to QUEST_QUAD_SONG_NOTE_A8
            sp224 = pauseCtx->ocarinaSongIdx;
            sp226 = gOcarinaSongButtons[sp224].numButtons;

            for (sp218 = sp21A, phi_s3 = 0; phi_s3 < sp226; phi_s3++, sp21A += 4) {
                pauseCtx->questVtx[sp21A + 0].v.ob[1] = pauseCtx->questVtx[sp21A + 1].v.ob[1] =
                    VREG(21 + gOcarinaSongButtons[sp224].buttonsIndex[phi_s3]);

                pauseCtx->questVtx[sp21A + 2].v.ob[1] = pauseCtx->questVtx[sp21A + 3].v.ob[1] =
                    pauseCtx->questVtx[sp21A + 0].v.ob[1] - 12;

                gDPPipeSync(POLY_OPA_DISP++);

                if (pauseCtx->unk_1E4 == 8) {
                    s16 Notes_alpha = 200;
                    if (gOcarinaSongButtons[sp224].buttonsIndex[phi_s3] == 0) {
                        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, aButtonColor.r, aButtonColor.g, aButtonColor.b,
                                        Notes_alpha);
                    } else {
                        if (gOcarinaSongButtons[sp224].buttonsIndex[phi_s3] == OCARINA_BTN_C_UP) {
                            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cUpButtonColor.r, cUpButtonColor.g, cUpButtonColor.b,
                                            Notes_alpha);
                        } else if (gOcarinaSongButtons[sp224].buttonsIndex[phi_s3] == OCARINA_BTN_C_LEFT) {
                            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cLeftButtonColor.r, cLeftButtonColor.g,
                                            cLeftButtonColor.b, Notes_alpha);
                        } else if (gOcarinaSongButtons[sp224].buttonsIndex[phi_s3] == OCARINA_BTN_C_RIGHT) {
                            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cRightButtonColor.r, cRightButtonColor.g,
                                            cRightButtonColor.b, Notes_alpha);
                        } else if (gOcarinaSongButtons[sp224].buttonsIndex[phi_s3] == OCARINA_BTN_C_DOWN) {
                            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cDownButtonColor.r, cDownButtonColor.g,
                                            cDownButtonColor.b, Notes_alpha);
                        } else {
                            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cButtonsColor.r, cButtonsColor.g, cButtonsColor.b,
                                            Notes_alpha);
                        }
                    }
                } else {
                    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 150, 150, 150, 150);
                }

                gDPSetEnvColor(POLY_OPA_DISP++, 10, 10, 10, 0);

                gSPVertex(POLY_OPA_DISP++, &pauseCtx->questVtx[sp21A], 4, 0);

                gDPLoadTextureBlock(POLY_OPA_DISP++, D_8082A130[gOcarinaSongButtons[sp224].buttonsIndex[phi_s3]],
                                    G_IM_FMT_IA, G_IM_SIZ_8b, 16, 16, 0, G_TX_NOMIRROR | G_TX_WRAP,
                                    G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);

                gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);
            }

            if (pauseCtx->unk_1E4 != 8) {
                pauseCtx->ocarinaStaff = AudioOcarina_GetPlayingStaff();

                if (pauseCtx->ocarinaStaff->pos != 0) {
                    if (D_8082A11C == (pauseCtx->ocarinaStaff->pos - 1)) {
                        if ((pauseCtx->ocarinaStaff->buttonIndex >= OCARINA_BTN_A) &&
                            (pauseCtx->ocarinaStaff->buttonIndex <= OCARINA_BTN_C_UP)) {
                            D_8082A124[pauseCtx->ocarinaStaff->pos - 1] = pauseCtx->ocarinaStaff->buttonIndex;
                            D_8082A124[pauseCtx->ocarinaStaff->pos] = 0xFF;
                            D_8082A11C++;
                        }
                    }
                }

                sp21A = sp218 + 32;
                phi_s3 = 0;
                for (; phi_s3 < 8; phi_s3++, sp21A += 4) {
                    if (D_8082A124[phi_s3] == 0xFF) {
                        continue;
                    }

                    if (D_8082A150[phi_s3] != 255) {
                        D_8082A150[phi_s3] += VREG(50);
                        if (D_8082A150[phi_s3] >= 255) {
                            D_8082A150[phi_s3] = 255;
                        }
                    }
                    pauseCtx->questVtx[sp21A + 0].v.ob[1] = pauseCtx->questVtx[sp21A + 1].v.ob[1] =
                        VREG(21 + D_8082A124[phi_s3]);

                    pauseCtx->questVtx[sp21A + 2].v.ob[1] = pauseCtx->questVtx[sp21A + 3].v.ob[1] =
                        pauseCtx->questVtx[sp21A + 0].v.ob[1] - 12;

                    gDPPipeSync(POLY_OPA_DISP++);

                    s16 Notes_alpha = D_8082A150[phi_s3];
                    if (D_8082A124[phi_s3] == 0) {
                        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, aButtonColor.r, aButtonColor.g, aButtonColor.b,
                                        Notes_alpha);
                    } else {
                        if (gOcarinaSongButtons[sp224].buttonsIndex[phi_s3] == OCARINA_BTN_C_UP) {
                            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cUpButtonColor.r, cUpButtonColor.g, cUpButtonColor.b,
                                            Notes_alpha);
                        } else if (gOcarinaSongButtons[sp224].buttonsIndex[phi_s3] == OCARINA_BTN_C_LEFT) {
                            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cLeftButtonColor.r, cLeftButtonColor.g,
                                            cLeftButtonColor.b, Notes_alpha);
                        } else if (gOcarinaSongButtons[sp224].buttonsIndex[phi_s3] == OCARINA_BTN_C_RIGHT) {
                            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cRightButtonColor.r, cRightButtonColor.g,
                                            cRightButtonColor.b, Notes_alpha);
                        } else if (gOcarinaSongButtons[sp224].buttonsIndex[phi_s3] == OCARINA_BTN_C_DOWN) {
                            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cDownButtonColor.r, cDownButtonColor.g,
                                            cDownButtonColor.b, Notes_alpha);
                        } else {
                            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cButtonsColor.r, cButtonsColor.g, cButtonsColor.b,
                                            Notes_alpha);
                        }
                    }

                    gDPSetEnvColor(POLY_OPA_DISP++, 10, 10, 10, 0);

                    gSPVertex(POLY_OPA_DISP++, &pauseCtx->questVtx[sp21A], 4, 0);

                    gDPLoadTextureBlock(POLY_OPA_DISP++, D_8082A130[D_8082A124[phi_s3]], G_IM_FMT_IA, G_IM_SIZ_8b, 16,
                                        16, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK,
                                        G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);

                    gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);
                }

                if (pauseCtx->unk_1E4 == 4) {
                    for (phi_s3 = 0; phi_s3 < 8; phi_s3++) {
                        D_8082A124[phi_s3] = 0xFF;
                        D_8082A150[phi_s3] = 0;
                    }

                    D_8082A11C = 0;
                    AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_DEFAULT);
                    AudioOcarina_Start((1 << pauseCtx->ocarinaSongIdx) + 0x8000);
                    pauseCtx->ocarinaStaff = AudioOcarina_GetPlaybackStaff();
                    pauseCtx->ocarinaStaff->pos = 0;
                    pauseCtx->ocarinaStaff->state = 0xFE;
                    pauseCtx->unk_1E4 = 5;
                }
            }
        }
    }

    if (CHECK_QUEST_ITEM(QUEST_SKULL_TOKEN)) {
        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                          PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
        gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 0);

        sp208[0] = sp208[1] = 0;
        sp208[2] = gSaveContext.inventory.gsTokens;

        while (sp208[2] >= 100) {
            sp208[0]++;
            sp208[2] -= 100;
        }

        while (sp208[2] >= 10) {
            sp208[1]++;
            sp208[2] -= 10;
        }

        gSPVertex(POLY_OPA_DISP++, &pauseCtx->questVtx[164], 24, 0);

        for (phi_s3 = 0, sp218 = 0, sp21A = 0; phi_s3 < 2; phi_s3++) {
            if (phi_s3 == 0) {
                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 0, 0, 0, pauseCtx->alpha);
            } else if (gSaveContext.inventory.gsTokens == 100) {
                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 200, 50, 50, pauseCtx->alpha);
            } else {
                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
            }

            phi_s0 = 0;
            for (sp21A = 0; sp21A < 3; sp21A++, sp218 += 4) {
                if ((sp21A >= 2) || (sp208[sp21A] != 0) || (phi_s0 != 0)) {
                    gDPLoadTextureBlock(POLY_OPA_DISP++, digitTextures[sp208[sp21A]], G_IM_FMT_I, G_IM_SIZ_8b, 8, 16, 0,
                                        G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                        G_TX_NOLOD, G_TX_NOLOD);

                    gSP1Quadrangle(POLY_OPA_DISP++, sp218, sp218 + 2, sp218 + 3, sp218 + 1, 0);

                    phi_s0 = 1;
                }
            }
        }
    }

    CLOSE_DISPS(gfxCtx);
}

s32 KaleidoScope_UpdateQuestStatusPoint(PauseContext* pauseCtx, s32 point) {
    pauseCtx->cursorPoint[PAUSE_QUEST] = point;

    return 1;
}

// ============================================================================================
// Skijer's NEI — MM QUEST STATUS page (mirror of the 2ship-side OoT quest page). Press L on the
// quest page to flip. Shows MM's quest content: the 4 boss remains + the 12-song MM layout
// (MM-unique songs play their real MM slots 14-20; the truly-doubled Epona/Time/Storms rows are
// the NEI custom songs, slots 21-23; Saria/Sun rows are OoT's own native songs). A on an owned
// song plays it (auto-playback → play-it-yourself prompt); with the Pause Play enhancement + an
// ocarina, A instead closes the menu and plays it in-world instantly (native success flow).
// Ownership = Nei_Save()->mmQuestItems (FC_MMQ_* bits, FleetShipCombo/FleetComboIds.h).
// ============================================================================================
#include "mods/nei_save.h"
#include "soh/FleetShipCombo/FleetComboIds.h"
#include "soh/ResourceManagerHelpers.h" // ResourceMgr_LoadTexOrDListByName (mm.o2r textures)

void Player_UseItem(PlayState* play, Player* player, s32 item); // z_player.c (no header decl)
void MmBgm_RegisterSequences(void);

// Latched "song played" event for the warp/effect pass (mirror of 2ship's gOotQuestSongToPlay):
// MM-page song index 0..11, or -1.
s16 gMmQuestSongToPlay = -1;

// Deterministic pause-play handoff consumed by z_message_PAL.c MSGMODE_OCARINA_PLAYING (ocarina
// slot index, or -1). Mirror of the 2ship-side gNeiPausePlayForcedSong.
s16 gNeiPausePlayForcedSong = -1;

// MM-page cursor point: 0-3 remains, 4-15 songs (MM layout order).
static s16 sMmPagePoint = 4;

// Pause-play phase machine (driven from Player_Update): 0 idle, 1 pull ocarina, 2 wait free-play.
static s16 sNeiPausePlaySong = -1;
static u8 sNeiPausePlayPhase = 0;
static s16 sNeiPausePlayTimer = 0;

// Song rows in MM's quest-page order → ocarina slot + FC_MMQ ownership bit (-1 = OoT-native row,
// owned via CHECK_QUEST_ITEM of the given quest item instead).
typedef struct {
    u8 ocarinaSlot;
    s8 fcBit;     // FC_MMQ bit index, or -1 → use questItem
    u8 questItem; // QUEST_SONG_* when fcBit < 0
} MmPageSongDef;

static const MmPageSongDef sMmPageSongs[12] = {
    { OCARINA_SONG_MM_SONATA, 6, 0 },              // Sonata of Awakening
    { OCARINA_SONG_MM_GORON_LULLABY, 7, 0 },       // Goron Lullaby
    { OCARINA_SONG_MM_NEW_WAVE, 8, 0 },            // New Wave Bossa Nova
    { OCARINA_SONG_MM_ELEGY, 9, 0 },               // Elegy of Emptiness
    { OCARINA_SONG_MM_OATH, 10, 0 },               // Oath to Order
    { OCARINA_SONG_SARIAS, -1, QUEST_SONG_SARIA }, // Saria's Song (OoT native)
    { OCARINA_SONG_NEI_COMMAND_MELODY, 12, 0 },    // Command Melody (replaces Song of Time row)
    { OCARINA_SONG_MM_HEALING, 13, 0 },            // Song of Healing
    { OCARINA_SONG_NEI_FUGUE_OF_HOME, 14, 0 },     // Fugue of Home (replaces Epona's row)
    { OCARINA_SONG_MM_SOARING, 15, 0 },            // Song of Soaring
    { OCARINA_SONG_NEI_BALLAD_OF_HERO, 16, 0 },    // Ballad of Hero (replaces Storms' row)
    { OCARINA_SONG_SUNS, -1, QUEST_SONG_SUN },     // Sun's Song (OoT native)
};

// Song-name textures fed to OoT's own bottom name panel (nameSegment path strings, IA4 128x16).
// MM names come from mm.o2r; Saria/Sun rows use OoT's native name textures; the three NEI custom
// songs come from soh/assets/custom/textures/item_name_custom (generate_names.py, same three names
// the 2ship OoT quest page uses — keep both repos in sync).
static const char* sMmPageSongNames[12] = {
    "__OTR__item_name_static/gItemNameSonataOfAwakeningENGTex",
    "__OTR__item_name_static/gItemNameGoronLullabyENGTex",
    "__OTR__item_name_static/gItemNameNewWaveBossaNovaENGTex",
    "__OTR__item_name_static/gItemNameElegyOfEmptynessENGTex",
    "__OTR__item_name_static/gItemNameOathToOrderENGTex",
    "__OTR__textures/item_name_static/gSariasSongItemNameENGTex",
    "__OTR__textures/item_name_custom/gCommandMelodyNameTex",
    "__OTR__item_name_static/gItemNameSongOfHealingENGTex",
    "__OTR__textures/item_name_custom/gFugueOfHomeNameTex",
    "__OTR__item_name_static/gItemNameSongOfSoaringENGTex",
    "__OTR__textures/item_name_custom/gBalladOfHeroNameTex",
    "__OTR__textures/item_name_static/gSunsSongItemNameENGTex",
};

// Name texture per CURSOR POINT (not per song), so the bottom name box works on the whole page and
// not only on the 12 song notes — a cursor sitting on a boss remains or the notebook used to force
// namedItem = PAUSE_ITEM_NONE and show nothing at all. NULL = intentionally unnamed.
// Points: 0-3 remains, 4 strength, 5 swim, 6-17 songs (see sMmPageSongNames), 18 notebook,
// 19 quiver/bullet bag, 20 bomb bag, 21 skull token (cursor never lands there), 22 heart piece.
// 4/5/19/20 are the OoT capacity upgrades whose name changes with the level; they stay unnamed until
// their per-level textures are wired, exactly like the equipment page's left column was.
static const char* sMmPagePointNames[23] = {
    "__OTR__item_name_static/gItemNameOdolwasRemainsENGTex",   // 0  Odolwa's Remains
    "__OTR__item_name_static/gItemNameGohtsRemainsENGTex",     // 1  Goht's Remains
    "__OTR__item_name_static/gItemNameGyorgsRemainsENGTex",    // 2  Gyorg's Remains
    "__OTR__item_name_static/gItemNameTwinmoldsRemainsENGTex", // 3  Twinmold's Remains
    NULL,                                                      // 4  strength upgrade
    NULL,                                                      // 5  swim upgrade
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL, // 6-11  songs
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,                                                     // 12-17 songs
    "__OTR__item_name_static/gItemNameBombersNotebookENGTex", // 18 Bombers' Notebook
    NULL,                                                     // 19 quiver / bullet bag
    NULL,                                                     // 20 bomb bag
    NULL,                                                     // 21 skull token (unused quad)
    "__OTR__item_name_static/gItemNamePieceOfHeartENGTex",    // 22 Heart Piece
};

static const char* sMmPageRemainsIcons[4] = {
    "__OTR__icon_item_static_yar/gItemIconOdolwasRemainsTex",
    "__OTR__icon_item_static_yar/gItemIconGohtsRemainsTex",
    "__OTR__icon_item_static_yar/gItemIconGyorgsRemainsTex",
    "__OTR__icon_item_static_yar/gItemIconTwinmoldsRemainsTex",
};

// Per-song note tint (MM-flavored palette; customs use their ring colors).
static const u8 sMmPageSongRGB[12][3] = {
    { 150, 255, 150 }, // Sonata — green
    { 255, 80, 60 },   // Goron Lullaby — red
    { 100, 180, 255 }, // New Wave — blue
    { 255, 160, 60 },  // Elegy — orange
    { 255, 90, 255 },  // Oath — purple
    { 135, 255, 130 }, // Saria — forest green
    { 255, 120, 255 }, // Command Melody — magenta (ring color)
    { 190, 140, 255 }, // Healing — violet
    { 255, 170, 50 },  // Fugue of Home — amber (ring color)
    { 255, 255, 140 }, // Soaring — pale yellow
    { 255, 230, 120 }, // Ballad of Hero — gold (ring color)
    { 255, 210, 80 },  // Sun — sun orange
};

static u8 MmPage_SongOwned(s16 songIdx) {
    const MmPageSongDef* def = &sMmPageSongs[songIdx];

    if (def->fcBit >= 0) {
        return (Nei_Save()->mmQuestItems & (1u << def->fcBit)) != 0;
    }
    return CHECK_QUEST_ITEM(def->questItem) ? 1 : 0;
}

// ---- MM's EXACT quest-page layout, ported verbatim from MM z_kaleido_scope_NES.c ----
// 23 content quads in MM QUEST_* point order (0-3 remains diamond, 4 shield, 5 sword, 6-11 lower
// song row, 12-17 upper song row, 18 notebook, 19 quiver, 20 bomb bag, 21 skull token (unused),
// 22 heart piece) + 8 played-note staff quads (23-30).
static const s16 sMmQuestQuadX[31] = {
    45,  78,  10,   45, 80, 11,   -109, -87, -65, -41, -19, -18, -109, -87, -65, -41,
    -19, -18, -103, 7,  82, -110, -54,  -98, -86, -74, -62, -50, -38,  -26, -14,
};
static const s16 sMmQuestQuadY[31] = {
    62, 42, 42, 20,  -9,  -9, -20, -20, -20, -20, -20, -20, 2,   2,   2,   2,
    2,  2,  54, -44, -44, 34, 58,  -52, -52, -52, -52, -52, -52, -52, -52,
};
static const s16 sMmQuestQuadW[31] = {
    32, 32, 32, 32, 32, 32, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 32, 32, 32, 24, 48, 16, 16, 16, 16, 16, 16, 16, 16,
};
static const s16 sMmQuestQuadH[31] = {
    32, 32, 32, 32, 32, 32, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 24, 32, 32, 32, 24, 48, 16, 16, 16, 16, 16, 16, 16, 16,
};

// MM's cursor link table (MM z_kaleido_collect.c sCursorPointLinks), verbatim: {up, down, left,
// right} per point; -1 = stay, -2 = page-switch left, -3 = page-switch right.
static const s16 sMmQuestCursorLinks[23][4] = {
    { -1, 3, 2, 1 },    // 0  Odolwa (remains diamond top)
    { 0, 4, 3, -3 },    // 1  Goht (right)
    { 0, 5, 22, 3 },    // 2  Gyorg (left)
    { 0, 4, 2, 1 },     // 3  Twinmold (bottom)
    { 1, 20, 5, -3 },   // 4  Shield
    { 2, 19, 16, 4 },   // 5  Sword
    { 12, -1, -2, 7 },  // 6  Sonata (lower row)
    { 13, -1, 6, 8 },   // 7  Goron Lullaby
    { 14, -1, 7, 9 },   // 8  New Wave Bossa Nova
    { 15, -1, 8, 10 },  // 9  Elegy
    { 16, -1, 9, 19 },  // 10 Oath
    { 17, -1, 10, 5 },  // 11 Saria
    { 18, 6, -2, 13 },  // 12 Command Melody (Time row, upper)
    { 18, 7, 12, 14 },  // 13 Healing
    { 22, 8, 13, 15 },  // 14 Fugue of Home (Epona row)
    { 22, 9, 14, 16 },  // 15 Soaring
    { 22, 10, 15, 5 },  // 16 Ballad of Hero (Storms row)
    { 22, 11, 16, 5 },  // 17 Sun
    { -1, 12, -2, 22 }, // 18 Bombers' Notebook
    { 5, -1, 10, 20 },  // 19 Quiver
    { 4, -1, 19, -3 },  // 20 Bomb Bag
    { 19, 12, -2, 22 }, // 21 Skull Token (unused quad)
    { -1, 16, 18, 2 },  // 22 Heart Piece
};

// MM's real per-song note tints (sQuestSongsPrimRed/Green/Blue, verbatim).
static const u8 sMmQuestSongRGB[12][3] = {
    { 150, 255, 100 }, // Sonata
    { 255, 80, 40 },   // Goron Lullaby
    { 100, 150, 255 }, // New Wave Bossa Nova
    { 255, 160, 0 },   // Elegy
    { 255, 100, 255 }, // Oath
    { 255, 240, 100 }, // Saria
    { 255, 255, 255 }, // Time row (Command Melody)
    { 255, 255, 255 }, // Healing
    { 255, 255, 255 }, // Epona row (Fugue of Home)
    { 255, 255, 255 }, // Soaring
    { 255, 255, 255 }, // Storms row (Ballad of Hero)
    { 255, 255, 255 }, // Sun
};

// Song-row order on the page: point 6..17 maps 1:1 onto sMmPageSongs[] (already in MM row order).
#define MMQ_POINT_TO_SONG(point) ((point)-6)

// AudioOcarina_Start song-flag encoding: vanilla songs = bit N; slots 14+ = param bits N+2 (the 16-bit
// mode flags 0x4000/0x8000 sit between).
static u32 MmPage_SongFlag(u8 slot) {
    return (slot < 14) ? (1u << slot) : (1u << (slot + 2));
}

static void NeiPausePlay_Start(PlayState* play, u8 ocarinaSlot) {
    PauseContext* pauseCtx = &play->pauseCtx;

    sNeiPausePlaySong = ocarinaSlot;
    sNeiPausePlayPhase = 1;
    sNeiPausePlayTimer = 200;
    gNeiPausePlayForcedSong = -1;

    // Close the pause menu exactly like the B/Start path (z_kaleido_scope_PAL.c:4108-4112).
    AudioOcarina_SetInstrument(0);
    pauseCtx->state = 0x12;
    WREG(2) = -6240;
    func_800F64E0(0);
    pauseCtx->unk_1E4 = 0;
}

// Called every frame from Player_Update. Mirror of the 2ship-side machine: once gameplay resumes,
// pull out the ocarina; when the free-play staff opens, force instant success (handoff consumed by
// z_message_PAL.c's MSGMODE_OCARINA_PLAYING, which then runs the native replay/fanfare flow).
void NeiPausePlay_Update(PlayState* play, Player* player) {
    MessageContext* msgCtx = &play->msgCtx;

    if (sNeiPausePlayPhase == 0) {
        return;
    }
    if (--sNeiPausePlayTimer <= 0) {
        sNeiPausePlayPhase = 0;
        sNeiPausePlaySong = -1;
        return;
    }

    switch (sNeiPausePlayPhase) {
        case 1:
            if ((play->pauseCtx.state == 0) && (msgCtx->msgMode == MSGMODE_NONE)) {
                u8 ocarina = INV_CONTENT(ITEM_OCARINA_TIME);

                Player_UseItem(play, player, (ocarina == ITEM_NONE) ? ITEM_OCARINA_FAIRY : ocarina);
                sNeiPausePlayPhase = 2;
            }
            break;

        case 2:
            if ((msgCtx->ocarinaAction == OCARINA_ACTION_FREE_PLAY) && (msgCtx->msgMode == MSGMODE_OCARINA_PLAYING)) {
                gNeiPausePlayForcedSong = sNeiPausePlaySong;
                sNeiPausePlayPhase = 0;
                sNeiPausePlaySong = -1;
            }
            break;

        default:
            sNeiPausePlayPhase = 0;
            break;
    }
}

// Alloc + fill one textured quad from the MM layout tables (top-left anchored like MM's vtx).
static Vtx* MmPage_AllocQuadRect(GraphicsContext* gfxCtx, s16 left, s16 top, s16 w, s16 h, s16 texW, s16 texH,
                                 s16 grow) {
    Vtx* v = Graph_Alloc(gfxCtx, 4 * sizeof(Vtx));
    s32 k;

    v[0].v.ob[0] = v[2].v.ob[0] = left - grow;
    v[1].v.ob[0] = v[3].v.ob[0] = left + w + grow;
    v[0].v.ob[1] = v[1].v.ob[1] = top + grow;
    v[2].v.ob[1] = v[3].v.ob[1] = top - h - grow;
    v[0].v.tc[0] = v[2].v.tc[0] = 0;
    v[1].v.tc[0] = v[3].v.tc[0] = texW << 5;
    v[0].v.tc[1] = v[1].v.tc[1] = 0;
    v[2].v.tc[1] = v[3].v.tc[1] = texH << 5;
    for (k = 0; k < 4; k++) {
        v[k].v.ob[2] = 0;
        v[k].v.flag = 0;
        v[k].v.cn[0] = v[k].v.cn[1] = v[k].v.cn[2] = 255;
        v[k].v.cn[3] = 255;
    }
    return v;
}

// Draw one mm.o2r texture at a layout quad. fmt: 0 = RGBA32, 1 = IA8. Textures resolve by OTR path
// through the resource manager, so texture packs/mods can replace them (mod support).
static void MmPage_DrawQuadTex(GraphicsContext* gfxCtx, s16 quad, const char* texPath, s16 texW, s16 texH, s16 fmt,
                               s16 grow) {
    Vtx* v;

    // Existence probe only. The DRAW passes the OTR PATH to gDPLoadTextureBlock (below), NOT a
    // resolved pointer — so Fast3D applies the MM HD texture-pack substitution by name (MM_Reloaded
    // etc.), exactly like the mask icons. Passing a resolved MmAssets_LoadResource pointer bypasses
    // HD (draws base data at the wrong size → blank), which is why pictobox/powerkeg went missing.
    // NULL-check BEFORE OPEN_DISPS: the macro pair opens/closes a scope in soh.
    if (ResourceMgr_LoadTexOrDListByName(texPath) == NULL) {
        return;
    }

    OPEN_DISPS(gfxCtx);
    // Use the EXACT render state OoT's quest page uses for its RGBA32 + IA icons (medallions, song
    // notes): the LERP combine with ENV=black, so color = TEXEL0 (full RGBA / tinted intensity) and
    // alpha = TEXEL0.a * PRIM.a. G_CC_MODULATERGBA_PRIM did NOT render the 32x32 RGBA32 icons here —
    // remains/shield/sword were invisible. PRIM is left to the caller (per-icon tint / alpha).
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
    gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 255);
    v = MmPage_AllocQuadRect(gfxCtx, sMmQuestQuadX[quad], sMmQuestQuadY[quad], sMmQuestQuadW[quad], sMmQuestQuadH[quad],
                             texW, texH, grow);
    gSPVertex(POLY_OPA_DISP++, v, 4, 0);
    if (fmt == 0) {
        gDPLoadTextureBlock(POLY_OPA_DISP++, texPath, G_IM_FMT_RGBA, G_IM_SIZ_32b, texW, texH, 0,
                            G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                            G_TX_NOLOD);
    } else {
        gDPLoadTextureBlock(POLY_OPA_DISP++, texPath, G_IM_FMT_IA, G_IM_SIZ_8b, texW, texH, 0,
                            G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                            G_TX_NOLOD);
    }
    gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);
    CLOSE_DISPS(gfxCtx);
}

// Frame the hovered quad with the 4-corner cursor at the item's REAL size.
//
// The vanilla KaleidoScope_UpdateCursorSize only knows OoT's fixed quest-item sizes and keys off
// pauseCtx->cursorSlot[PAUSE_QUEST] — which this MM page never sets — AND it runs in the UPDATE
// pass, before this draw pass repositions the cursor. So the drawn brackets ended up a stale,
// wrong size (OoT's 8x8 "song" preset around MM's 16x24 notes / 48x48 heart piece) → the cursor
// "barely pointed at" the item. We spread the 16 cursor vertices ourselves, from the item's own
// w/h, here in the draw pass — the authoritative write right before KaleidoScope_DrawCursor.
// Vertex layout mirrors KaleidoScope_UpdateCursorSize (z_kaleido_scope_PAL.c): 4 corner pieces of
// 16x16, piece j drawn as gSP1Quadrangle(j, j+2, j+3, j+1). (left, top) is the quad's top-left.
static void MmPage_FrameCursor(PauseContext* pauseCtx, s16 left, s16 top, s16 w, s16 h) {
    Vtx* cv = pauseCtx->cursorVtx;
    s16 m = 2;               // outward margin around the item
    s16 L = left - m;        // frame top-left x
    s16 T = top + m;         // frame top-left y (+y is up)
    s16 tX = w + 2 * m - 16; // gap between the left and right corner pieces
    s16 tY = h + 2 * m - 16; // gap between the top and bottom corner pieces

    if (tX < 2) {
        tX = 2;
    }
    if (tY < 2) {
        tY = 2;
    }

    // top-left piece (vtx 0-3)
    cv[0].v.ob[0] = cv[2].v.ob[0] = L;
    cv[1].v.ob[0] = cv[3].v.ob[0] = L + 16;
    cv[0].v.ob[1] = cv[1].v.ob[1] = T;
    cv[2].v.ob[1] = cv[3].v.ob[1] = T - 16;
    // top-right piece (vtx 4-7)
    cv[4].v.ob[0] = cv[6].v.ob[0] = L + tX;
    cv[5].v.ob[0] = cv[7].v.ob[0] = L + tX + 16;
    cv[4].v.ob[1] = cv[5].v.ob[1] = T;
    cv[6].v.ob[1] = cv[7].v.ob[1] = T - 16;
    // bottom-left piece (vtx 8-11)
    cv[8].v.ob[0] = cv[10].v.ob[0] = L;
    cv[9].v.ob[0] = cv[11].v.ob[0] = L + 16;
    cv[8].v.ob[1] = cv[9].v.ob[1] = T - tY;
    cv[10].v.ob[1] = cv[11].v.ob[1] = T - tY - 16;
    // bottom-right piece (vtx 12-15)
    cv[12].v.ob[0] = cv[14].v.ob[0] = L + tX;
    cv[13].v.ob[0] = cv[15].v.ob[0] = L + tX + 16;
    cv[12].v.ob[1] = cv[13].v.ob[1] = T - tY;
    cv[14].v.ob[1] = cv[15].v.ob[1] = T - tY - 16;
}

// Played-note recorder for the in-menu minigame staff (mirror of MM's D_8082A124 accumulator).
static u8 sMmPlayedNotes[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static s16 sMmPlayedNotesNum = 0;

void KaleidoScope_DrawMmQuestStatus(PlayState* play, GraphicsContext* gfxCtx) {
    // Per-note staff Y (MM VREG(21)..VREG(25): A, C-down, C-right, C-left, C-up).
    static const s16 sMmNoteY[5] = { -62, -56, -49, -46, -41 };
    static void* sMmNoteTexs[5] = { gOcarinaBtnIconATex, gOcarinaBtnIconCDownTex, gOcarinaBtnIconCRightTex,
                                    gOcarinaBtnIconCLeftTex, gOcarinaBtnIconCUpTex };
    PauseContext* pauseCtx = &play->pauseCtx;
    Input* input = &play->state.input[0];
    s16 i;

    // Skijer 2026-07-31 FIX — the single worst bug on this page.
    //
    // KaleidoScope_DrawPages calls KaleidoScope_DrawQuestStatus TWICE: once for the quest page as an
    // *adjacent* page whenever pageIndex != PAUSE_QUEST (z_kaleido_scope_PAL.c:1341), and once in the
    // switch when it IS the active page (:1436). So this function runs once per frame no matter which
    // page the player is looking at — and nothing here checked pageIndex. Everything below that
    // touches shared PauseContext state was therefore firing from the item/equipment/map pages:
    //
    //   * the cursor nav wrote pauseCtx->cursorSpecialPos, which is global, not per page — the active
    //     page's cursor jumped to its page-scroll arrow out of nowhere ("no puedo mover el kaleido");
    //   * pressing A anywhere set pauseCtx->unk_1E4 = 2 (song demonstration), and every page's update
    //     is gated on unk_1E4 == 0, so the whole kaleido froze. Same for the boss-remains equip and
    //     the strength toggle (which parks unk_1E4 = 7);
    //   * the name block wrote pauseCtx->namedItem and memcpy'd into pauseCtx->nameSegment every
    //     frame, and DrawPages runs BEFORE DrawInfoPanel (:3342 then :3351), so it always won: the
    //     item page showed either this page's song name instead of the item's, or no name at all when
    //     sMmPagePoint was parked on a cell without one.
    //
    // Drawing the page itself is fine from anywhere (that is the point of the adjacent-page pass);
    // input and shared state are not.
    u8 isActivePage = (pauseCtx->pageIndex == PAUSE_QUEST);

    MmBgm_RegisterSequences(); // idempotent - makes the MM fanfare sequences resolvable

    OPEN_DISPS(gfxCtx);

    gDPPipeSync(POLY_OPA_DISP++);

    // --- Boss remains (RGBA32 32x32, MM diamond positions; drawn only when owned, like MM) ---
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
    for (i = 0; i < 4; i++) {
        if (Nei_Save()->mmQuestItems & (1u << i)) {
            MmPage_DrawQuadTex(gfxCtx, i, sMmPageRemainsIcons[i], 32, 32, 0, 0);
        }
    }

    // --- STRENGTH / SWIM upgrades (Skijer 2026-07-29) ---
    // These two quads used to hold decorative copies of the equipped sword and shield. Both live on
    // the equipment page, so the quads now carry the two capacity upgrades that moved OFF that page's
    // left column: quad 4 = strength (bracelet/gauntlets), quad 5 = swim (silver/golden scale).
    {
        static const char* sStrengthTexs[3] = {
            gItemIconGoronsBraceletTex,
            gItemIconSilverGauntletsTex,
            gItemIconGoldenGauntletsTex,
        };
        static const char* sScaleTexs[2] = {
            gItemIconScaleSilverTex,
            gItemIconScaleGoldenTex,
        };
        s32 upg = CUR_UPG_VALUE(UPG_STRENGTH);

        if (upg > 0) {
            // The ToggleStrength enhancement greys the icon out while strength is disabled — same
            // visual rule the equipment page used before the move.
            u8 strengthOff = CVarGetInteger(CVAR_ENHANCEMENT("ToggleStrength"), 0) &&
                             CVarGetInteger(CVAR_ENHANCEMENT("StrengthDisabled"), 0);
            gDPPipeSync(POLY_OPA_DISP++);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255,
                            strengthOff ? (pauseCtx->alpha >> 1) : pauseCtx->alpha);
            MmPage_DrawQuadTex(gfxCtx, 4, sStrengthTexs[((upg > 3) ? 3 : upg) - 1], 32, 32, 0, 0);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
        }
        upg = CUR_UPG_VALUE(UPG_SCALE);
        if (upg > 0) {
            MmPage_DrawQuadTex(gfxCtx, 5, sScaleTexs[((upg > 2) ? 2 : upg) - 1], 32, 32, 0, 0);
        }
    }

    // --- Bombers' Notebook / Quiver-or-Bullet-Bag / Bomb Bag (MM positions, OoT upgrade values) ---
    if (Nei_Save()->mmQuestItems & FC_MMQ_BOMBERS_NOTEBOOK) {
        MmPage_DrawQuadTex(gfxCtx, 18, "__OTR__icon_item_static_yar/gItemIconBombersNotebookTex", 32, 32, 0, 0);
    }
    {
        static const char* sQuiverTexs[3] = { "__OTR__icon_item_static_yar/gItemIconQuiver30Tex",
                                              "__OTR__icon_item_static_yar/gItemIconQuiver40Tex",
                                              "__OTR__icon_item_static_yar/gItemIconQuiver50Tex" };
        static const char* sBulletBagTexs[3] = { gItemIconBulletBag30Tex, gItemIconBulletBag40Tex,
                                                 gItemIconBulletBag50Tex };
        static const char* sBombBagTexs[3] = { "__OTR__icon_item_static_yar/gItemIconBombBag20Tex",
                                               "__OTR__icon_item_static_yar/gItemIconBombBag30Tex",
                                               "__OTR__icon_item_static_yar/gItemIconBombBag40Tex" };
        // As child (or with no bow yet) this cell is the slingshot's BULLET BAG, exactly like OoT's
        // own upgrade column did — that is the "Quiver / Bullet as child" cell.
        s32 upg = CUR_UPG_VALUE(UPG_QUIVER);

        if (upg > 0) {
            MmPage_DrawQuadTex(gfxCtx, 19, sQuiverTexs[((upg > 3) ? 3 : upg) - 1], 32, 32, 0, 0);
        } else {
            upg = CUR_UPG_VALUE(UPG_BULLET_BAG);
            if (upg > 0) {
                MmPage_DrawQuadTex(gfxCtx, 19, sBulletBagTexs[((upg > 3) ? 3 : upg) - 1], 32, 32, 0, 0);
            }
        }
        upg = CUR_UPG_VALUE(UPG_BOMB_BAG);
        if (upg > 0) {
            MmPage_DrawQuadTex(gfxCtx, 20, sBombBagTexs[((upg > 3) ? 3 : upg) - 1], 32, 32, 0, 0);
        }
    }

    // --- Gold Skulltula token (quad 21) ---
    // The quad has always been in MM's layout table and the cursor could land on it, but nothing was
    // ever drawn here, so the slot read as broken. OoT's token count lives in gsTokens; the icon is
    // OoT's own 24x24 quest icon (the count itself is shown by the name/counter box). Skijer 2026-07-29
    if (gSaveContext.inventory.gsTokens > 0) {
        MmPage_DrawQuadTex(gfxCtx, 21, gQuestIconGoldSkulltulaTex, 24, 24, 0, 0);
    }

    // --- Heart piece count (MM 48x48 IA8 pie icons; OoT stores the count in questItems bits 28+) ---
    {
        u32 hpCount = (gSaveContext.inventory.questItems & 0xF0000000) >> 28;

        if ((hpCount >= 1) && (hpCount <= 3)) {
            static const char* sHeartPieceTexs[3] = { "__OTR__icon_item_static_yar/gItemIconHeartPiece1Tex",
                                                      "__OTR__icon_item_static_yar/gItemIconHeartPiece2Tex",
                                                      "__OTR__icon_item_static_yar/gItemIconHeartPiece3Tex" };
            gDPPipeSync(POLY_OPA_DISP++);
            gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 70, 50, pauseCtx->alpha);
            MmPage_DrawQuadTex(gfxCtx, 22, sHeartPieceTexs[hpCount - 1], 48, 48, 1, 0);
        }
    }

    // --- The 12 songs (MM note icon IA8 16x24, MM tints + positions; drawn only when owned) ---
    {
        const char* notePath = "__OTR__icon_item_static_yar/gItemIconSongNoteTex";

        if (ResourceMgr_LoadTexOrDListByName(notePath) != NULL) {
            gDPPipeSync(POLY_OPA_DISP++);
            // Same LERP combine as OoT's own song notes (ENV=black → color = tint * TEXEL0 intensity).
            gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                              PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
            gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 255);
            for (i = 0; i < 12; i++) {
                s16 quad = 6 + i;
                Vtx* v;

                if (!MmPage_SongOwned(i)) {
                    continue;
                }
                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, sMmQuestSongRGB[i][0], sMmQuestSongRGB[i][1],
                                sMmQuestSongRGB[i][2], pauseCtx->alpha);
                v = MmPage_AllocQuadRect(gfxCtx, sMmQuestQuadX[quad], sMmQuestQuadY[quad], 16, 24, 16, 24, 0);
                gSPVertex(POLY_OPA_DISP++, v, 4, 0);
                gDPLoadTextureBlock(POLY_OPA_DISP++, notePath, G_IM_FMT_IA, G_IM_SIZ_8b, 16, 24, 0,
                                    G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                    G_TX_NOLOD, G_TX_NOLOD);
                gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);
            }
        }
    }

    // --- Note staff (MM quads 23-30), PHASED like the 2ship-side quest page ---
    //   idle + hovering an owned song -> the song's FULL fingering, bright (the note preview).
    //   auto-playback (state 1)      -> notes reveal one-by-one as they play.
    //   play-it-yourself (state 2)   -> the full fingering DIMMED (the target) + the notes you have
    //                                    played lit on top. A = blue, C = yellow (MM colors).
    {
        s16 hoverSong =
            ((sMmPagePoint >= 6) && (sMmPagePoint <= 17) && MmPage_SongOwned(MMQ_POINT_TO_SONG(sMmPagePoint)))
                ? (s16)sMmPageSongs[MMQ_POINT_TO_SONG(sMmPagePoint)].ocarinaSlot
                : -1;
        u8 playing = (pauseCtx->unk_1E4 == 2) || (pauseCtx->unk_1E4 == 5);
        s16 staffSong = playing ? (s16)pauseCtx->ocarinaSongIdx : hoverSong;

        if (playing) {
            OcarinaStaff* staff =
                (pauseCtx->unk_1E4 == 2) ? AudioOcarina_GetPlaybackStaff() : AudioOcarina_GetPlayingStaff();

            if ((staff->pos == 1) && (sMmPlayedNotesNum == 8)) {
                sMmPlayedNotesNum = 0; // input wrapped past 8 notes (vanilla parity)
            }
            if ((staff->pos != 0) && (sMmPlayedNotesNum == staff->pos - 1) && (staff->buttonIndex <= 4)) {
                sMmPlayedNotes[staff->pos - 1] = staff->buttonIndex;
                if (staff->pos < 8) {
                    sMmPlayedNotes[staff->pos] = 0xFF;
                }
                sMmPlayedNotesNum = staff->pos;
            }
        }

        if (staffSong >= 0) {
            u8 len = gOcarinaSongButtons[staffSong].numButtons;

            gDPPipeSync(POLY_OPA_DISP++);
            gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                              PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
            gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 255);

            for (i = 0; (i < len) && (i < 8); i++) {
                u8 note;
                u8 bright;
                u8 alpha;
                Vtx* v;

                if (pauseCtx->unk_1E4 == 2) {
                    // playback reveal: only what has sounded so far
                    if ((i >= sMmPlayedNotesNum) || (sMmPlayedNotes[i] > 4)) {
                        break;
                    }
                    note = sMmPlayedNotes[i];
                    bright = 1;
                } else if (pauseCtx->unk_1E4 == 5) {
                    // prompt: dim target from the fingering; brighten the notes already played
                    if ((i < sMmPlayedNotesNum) && (sMmPlayedNotes[i] <= 4)) {
                        note = sMmPlayedNotes[i];
                        bright = 1;
                    } else {
                        note = gOcarinaSongButtons[staffSong].buttonsIndex[i];
                        bright = 0;
                    }
                } else {
                    // hover preview: the full fingering
                    note = gOcarinaSongButtons[staffSong].buttonsIndex[i];
                    bright = 1;
                }
                if (note > 4) {
                    continue;
                }
                alpha = bright ? pauseCtx->alpha : (pauseCtx->alpha >> 2);
                if (note == 0) {
                    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 80, 150, 255, alpha); // A = blue (MM)
                } else {
                    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 50, alpha); // C = yellow (MM)
                }
                v = MmPage_AllocQuadRect(gfxCtx, sMmQuestQuadX[23 + i], sMmNoteY[note], 16, 16, 16, 16, 0);
                gSPVertex(POLY_OPA_DISP++, v, 4, 0);
                gDPLoadTextureBlock(POLY_OPA_DISP++, sMmNoteTexs[note], G_IM_FMT_IA, G_IM_SIZ_8b, 16, 16, 0,
                                    G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                    G_TX_NOLOD, G_TX_NOLOD);
                gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);
            }
        }
    }

    // --- Song name via OoT's OWN bottom name panel (1:1 UX): feed nameSegment with the MM name
    // texture path and keep namedItem == cursorItem so KaleidoScope_UpdateNamePanel never overwrites
    // it; when not on a named song, force PAUSE_ITEM_NONE so no stale OoT name bleeds through. ---
    if (isActivePage) {
        static s16 sLastNamedPoint = -100;
        s16 songIdx = ((sMmPagePoint >= 6) && (sMmPagePoint <= 17)) ? MMQ_POINT_TO_SONG(sMmPagePoint) : -1;
        const char* nameTex = NULL;

        // Songs name themselves only when learned (vanilla shows nothing for an unlearned slot);
        // every other cell takes its name straight from the point table.
        if (songIdx >= 0) {
            if (MmPage_SongOwned(songIdx)) {
                nameTex = sMmPageSongNames[songIdx];
            }
        } else if ((sMmPagePoint >= 0) && (sMmPagePoint < (s16)ARRAY_COUNT(sMmPagePointNames))) {
            nameTex = sMmPagePointNames[sMmPagePoint];
        }

        // The draw path (KaleidoScope_QuadTextureIA4 on nameSegment) hands the OTR path straight to
        // the resource manager with no null check, so an archive that does not carry the texture is a
        // crash, not a blank box. Probe first, exactly like the equipment page's own name lookups.
        if ((nameTex != NULL) && !ResourceMgr_FileExists(nameTex)) {
            nameTex = NULL;
        }

        // Keep the shared cursor slot in step with our own point. Vanilla's DrawQuestStatus returns
        // before it can do this, so cursorSlot[PAUSE_QUEST] stayed frozen at whatever the OoT layout
        // last hovered — and the info panel still decides by it: the "A - Play Melody" prompt
        // (z_kaleido_scope_PAL.c:2129), the name blink (:2324) and KaleidoScope_UpdateCursorSize
        // (:3439) all read cursorSlot[PAUSE_QUEST]. That is why the prompt appeared and disappeared
        // with no relation to where the cursor actually was.
        pauseCtx->cursorPoint[PAUSE_QUEST] = sMmPagePoint;
        pauseCtx->cursorSlot[PAUSE_QUEST] = sMmPagePoint;

        if (nameTex != NULL) {
            if (sLastNamedPoint != sMmPagePoint) {
                memcpy(pauseCtx->nameSegment, nameTex, strlen(nameTex) + 1);
                sLastNamedPoint = sMmPagePoint;
            }
            pauseCtx->nameDisplayTimer = 0;
            // Sentinel above the vanilla item range: keeps namedItem == cursorItem so
            // KaleidoScope_UpdateNamePanel leaves our nameSegment alone next update.
            pauseCtx->namedItem = pauseCtx->cursorItem[PAUSE_QUEST] = 0x100 + sMmPagePoint;
        } else {
            pauseCtx->namedItem = pauseCtx->cursorItem[PAUSE_QUEST] = PAUSE_ITEM_NONE;
            sLastNamedPoint = -100;
        }
    }

    // --- Song play via OoT's NATIVE ocarina machine (unk_1E4), exactly like the vanilla quest page
    // and the 2ship MM page: state 2 = demonstration (the melody plays note-by-note, driven by the
    // top-level pause switch in z_kaleido_scope_PAL.c), state 5 = "play it yourself" prompt. We start
    // the demonstration (A-press below) and hand it to the prompt once it finishes (native 2 -> 4). ---
    if (isActivePage && (pauseCtx->unk_1E4 == 5)) {
        // Skijer 2026-07-31 FIX: the "play it yourself" prompt had no way out. Vanilla's quest page
        // aborts it the moment the stick moves (see the unk_1E4 == 5 arm of KaleidoScope_DrawQuestStatus
        // above); this page only ever handled states 4 and 0, so once A started a song the player was
        // pinned — no cursor, and no page change either, since KaleidoScope_HandlePageToggles only runs
        // while the menu is idle. Worse for the NEI custom songs, whose recognition may never report
        // back at all. Same abort as vanilla.
        if ((pauseCtx->stickRelX != 0) || (pauseCtx->stickRelY != 0)) {
            pauseCtx->unk_1E4 = 0;
            AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
        }
    } else if (isActivePage && (pauseCtx->unk_1E4 == 4)) {
        // Demonstration finished (native driver set 2 -> 4). Start the play-it-yourself prompt (mirror
        // of vanilla collect.c's unk_1E4 == 4 handler). Custom songs (slots 21-23) recognize through
        // the widened AudioOcarina_Start param encoding (MmPage_SongFlag).
        for (i = 0; i < 8; i++) {
            sMmPlayedNotes[i] = 0xFF;
        }
        sMmPlayedNotesNum = 0;
        AudioOcarina_SetInstrument(1);
        AudioOcarina_Start(MmPage_SongFlag(pauseCtx->ocarinaSongIdx) + 0x8000);
        pauseCtx->ocarinaStaff = AudioOcarina_GetPlayingStaff();
        pauseCtx->ocarinaStaff->pos = 0;
        pauseCtx->ocarinaStaff->state = 0xFE;
        pauseCtx->unk_1E4 = 5;
    } else if (isActivePage && (pauseCtx->unk_1E4 == 0) && (pauseCtx->state == 6)) {
        // --- Cursor navigation via MM's exact link table (idle only; the ocarina owns input during
        // states 2/5/6, so nav + A must not run then) ---
        // The `state == 6` (PAUSE_STATE_MAIN) check mirrors vanilla, which refuses to move the cursor
        // outside it — without it the cursor moved during the open/close animation and the save prompt.
        s16 point = sMmPagePoint;
        s16 next = point;

        if (pauseCtx->cursorSpecialPos == 0) {
            if (pauseCtx->stickRelX < -30) {
                next = sMmQuestCursorLinks[point][2];
            } else if (pauseCtx->stickRelX > 30) {
                next = sMmQuestCursorLinks[point][3];
            } else if (pauseCtx->stickRelY > 30) {
                next = sMmQuestCursorLinks[point][0];
            } else if (pauseCtx->stickRelY < -30) {
                next = sMmQuestCursorLinks[point][1];
            }

            if (next == -2) {
                pauseCtx->cursorSpecialPos = PAUSE_CURSOR_PAGE_LEFT;
                Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            } else if (next == -3) {
                pauseCtx->cursorSpecialPos = PAUSE_CURSOR_PAGE_RIGHT;
                Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            } else if ((next >= 0) && (next != point) && (next != 21)) { // 21 = unused skull quad
                sMmPagePoint = next;
                Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
        } else if (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_LEFT) {
            if (pauseCtx->stickRelX > 30) {
                pauseCtx->cursorSpecialPos = 0;
                sMmPagePoint = 6;
                Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
        } else if (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_RIGHT) {
            if (pauseCtx->stickRelX < -30) {
                pauseCtx->cursorSpecialPos = 0;
                sMmPagePoint = 1;
                Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
        }

        // --- A on an owned song: pause-play (menu closes, plays in-world) or in-menu playback ---
        if ((pauseCtx->state == 6) && (pauseCtx->cursorSpecialPos == 0) && (sMmPagePoint >= 6) &&
            (sMmPagePoint <= 17) && CHECK_BTN_ALL(input->press.button, BTN_A) &&
            MmPage_SongOwned(MMQ_POINT_TO_SONG(sMmPagePoint))) {
            u8 slot = sMmPageSongs[MMQ_POINT_TO_SONG(sMmPagePoint)].ocarinaSlot;

            gMmQuestSongToPlay = MMQ_POINT_TO_SONG(sMmPagePoint);

            if (CVarGetInteger(CVAR_ENHANCEMENT("SkijerNEI.PausePlay"), 0) &&
                (INV_CONTENT(ITEM_OCARINA_TIME) != ITEM_NONE)) {
                NeiPausePlay_Start(play, slot);
            } else {
                // Learn-it minigame: start MM/OoT's NATIVE demonstration (unk_1E4 = 2). The audio plays
                // the melody note-by-note and the top-level pause switch drives it to completion; then
                // our unk_1E4 == 4 handler above starts the play-it-yourself prompt. 1:1 with vanilla.
                for (i = 0; i < 8; i++) {
                    sMmPlayedNotes[i] = 0xFF;
                }
                sMmPlayedNotesNum = 0;
                VREG(21) = -62;
                VREG(22) = -56;
                VREG(23) = -49;
                VREG(24) = -46;
                VREG(25) = -41;
                AudioOcarina_SetInstrument(1);
                AudioOcarina_SetInstrument(1);
                pauseCtx->ocarinaSongIdx = slot;
                AudioOcarina_SetPlaybackSong(slot + 1, 1);
                pauseCtx->unk_1E4 = 2;
                pauseCtx->ocarinaStaff = AudioOcarina_GetPlaybackStaff();
                pauseCtx->ocarinaStaff->pos = 0;
            }
        }

        // --- C/D press on an owned boss remains (points 0..3): equip it to that button (Skijer's NEI
        //     boss_remains — sentinel equip, same idiom as Sw97_TryEquipMedallion above). The module
        //     gates on ownership (mmQuestItems bit) and plays the decide sfx itself. ---
        if ((pauseCtx->state == 6) && (pauseCtx->cursorSpecialPos == 0) && (sMmPagePoint >= 0) && (sMmPagePoint <= 3)) {
            extern s32 BossRemains_TryEquipAtCursor(PlayState * play, Input * input, s16 item);
            static const s16 sMmRemainsItems[4] = { ITEM_MM_REMAINS_ODOLWA, ITEM_MM_REMAINS_GOHT, ITEM_MM_REMAINS_GYORG,
                                                    ITEM_MM_REMAINS_TWINMOLD };
            BossRemains_TryEquipAtCursor(play, input, sMmRemainsItems[sMmPagePoint]);
        }

        // --- A on the STRENGTH cell (quad 4) toggles strength, the interaction that came over with
        //     the cell from the equipment page's left column (ToggleStrength enhancement). The icon
        //     draws at half alpha while disabled. Skijer 2026-07-29 ---
        if ((pauseCtx->state == 6) && (pauseCtx->cursorSpecialPos == 0) && (sMmPagePoint == 4) &&
            (pauseCtx->unk_1E4 == 0) && CHECK_BTN_ALL(input->press.button, BTN_A) &&
            CVarGetInteger(CVAR_ENHANCEMENT("ToggleStrength"), 0) && (CUR_UPG_VALUE(UPG_STRENGTH) > 0)) {
            CVarSetInteger(CVAR_ENHANCEMENT("StrengthDisabled"),
                           !CVarGetInteger(CVAR_ENHANCEMENT("StrengthDisabled"), 0));
            Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            pauseCtx->unk_1E4 = 7; // same input cooldown the equipment page used
        }
    }

    // --- Cursor: frame the hovered quad at its real size (fixes the tiny/offset cursor; see
    //     MmPage_FrameCursor). Runs after the update-pass UpdateCursorSize, so this wins.
    //     Only on the active page: pauseCtx->cursorVtx is the ONE cursor shared by every page, and
    //     from the adjacent-page pass this was spreading it over the item/equipment page's cursor. ---
    if (isActivePage && (pauseCtx->cursorSpecialPos == 0)) {
        MmPage_FrameCursor(pauseCtx, sMmQuestQuadX[sMmPagePoint], sMmQuestQuadY[sMmPagePoint],
                           sMmQuestQuadW[sMmPagePoint], sMmQuestQuadH[sMmPagePoint]);
    }

    CLOSE_DISPS(gfxCtx);
}
