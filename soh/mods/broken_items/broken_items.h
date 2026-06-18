/**
 * broken_items.h - "Broken Modes" transform selector (More Than Enough Items).
 *
 * The form selector (LINK / MARIO / PIKACHU) is the 3rd page of the Equipment
 * subscreen — the form icons sit in the grid where the swords/shields go and the
 * form's item name shows in the usual name spot. Reached with the equipment-page
 * change button (L / Z). Selecting a form sets the persistent mode CVars:
 *   - LINK MODE  (Ocarina)     -> normal Link (Mario / Pikachu off)
 *   - MARIO MODE (Mario Mask)  -> libsm64 Mario (gSm64Mario)
 *   - PIKACHU MODE (Pokeball)  -> Pikachu (gPikachuMode)
 *
 * Gated by the CVar gBrokenItems.Enabled. This file owns the shared form data +
 * toggle; the drawing/input lives in z_kaleido_equipment.c (it calls the
 * accessors below). The old Map-page overlay was removed. English-only on purpose.
 */

#ifndef BROKEN_ITEMS_H
#define BROKEN_ITEMS_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// CVar gate ("gBrokenItems.Enabled"). 1 = the transform selector is available.
s32 BrokenItems_Enabled(void);

// --- Equipment-page transform selector (z_kaleido_equipment.c) ---
// The transform options are drawn IN the equipment grid (where swords/shields go)
// as a 3rd equipment page; these expose the shared form data + toggle so there's
// one source of truth (the Map overlay above is the deprecated path).
s32 BrokenItems_FormCount(void);          // number of forms (Link / Mario / Pikachu)
const char* BrokenItems_FormName(s32 i);  // "LINK MODE" etc.
void* BrokenItems_FormIconTex(s32 i);     // grid icon texture for form i
u16 BrokenItems_FormItem(s32 i);          // OOT item whose NAME texture labels form i
s32 BrokenItems_CurrentForm(void);        // currently-equipped form index
void BrokenItems_EquipForm(PlayState* play, s32 i); // equip form i (sets the CVars)

#ifdef __cplusplus
}
#endif

#endif // BROKEN_ITEMS_H
