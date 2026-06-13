/**
 * broken_items.h - "Broken Modes" pause subscreen (More Than Enough Items).
 *
 * A native kaleido page that behaves like the Equipment subscreen, reached
 * from the Map page with the page-change button. You navigate the modes with
 * the analog stick and press A to equip one. Only two modes exist for now:
 *   - LINK MODE  (Ocarina)     -> normal Link (Mario off)
 *   - MARIO MODE (Mario Mask)  -> libsm64 Mario (sets gSm64Mario, same effect
 *                                 the old Mario-Mask C-Down toggle had)
 *
 * Gated by the CVar gBrokenItems.Enabled. The whole feature is English-only
 * (UI + code) on purpose: it is for players and devs.
 *
 * Integration lives in z_kaleido_scope_PAL.c:
 *   - KaleidoScope_Update routes input here while active / on entry.
 *   - BrokenItems_DrawOverlay is called at the end of the kaleido draw (after
 *     the info panel) to draw the whole overlay on top of everything.
 */

#ifndef BROKEN_ITEMS_H
#define BROKEN_ITEMS_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// CVar gate ("gBrokenItems.Enabled"). 1 = the Map page can open Broken Modes.
s32 BrokenItems_Enabled(void);

// True while the Broken Modes page is the active subscreen.
s32 BrokenItems_IsActive(void);

// True on the frame the player asks to open Broken Modes (on the Map page,
// feature enabled, page-change button pressed). Checked by KaleidoScope_Update.
s32 BrokenItems_ShouldEnter(PlayState* play);

// Enter / leave the Broken Modes page (plays the page-scroll SFX).
void BrokenItems_Enter(PlayState* play);
void BrokenItems_Exit(PlayState* play);

// Per-frame input while active: stick = move cursor, A = equip the selected
// mode, R = back to Map, B/START = let the pause close normally.
void BrokenItems_Update(PlayState* play, Input* input);

// Draws the WHOLE Broken Modes overlay (own dark backdrop + slot boxes + item
// icons + cursor + names + control map) on top of everything. Call at the very
// end of the kaleido draw (after the info panel) so it covers the cube/prompts.
void BrokenItems_DrawOverlay(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // BROKEN_ITEMS_H
