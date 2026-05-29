/**
 * spiritual_stones.h - Spiritual Stone passives + per-stone warp points.
 *
 * Three Spiritual Stones (Kokiri/Goron/Zora) gain new gameplay roles:
 *   1) Passive speed buff while owned, toggled by pressing A on the stone
 *      in the pause/quest screen. Per-stone CHECK_QUEST_ITEM gate.
 *   2) Equippable to a C-button slot via the SW97 medallion path (same
 *      sentinel: cButtonSlots[i] == 0xFF, buttonItems[i] = stone item id).
 *   3) Hold the bound C-button >= 60 frames to summon a recolored owl
 *      statue (one slot per stone, replaces previous). Tap (< 60 frames)
 *      while a statue exists opens a yes/no warp prompt that warps to it.
 *
 * All state is per-save and persisted via SaveManager.
 */
#ifndef SPIRITUAL_STONES_H
#define SPIRITUAL_STONES_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPIRITUAL_STONE_KOKIRI 0
#define SPIRITUAL_STONE_GORON  1
#define SPIRITUAL_STONE_ZORA   2
#define SPIRITUAL_STONE_COUNT  3

#define SPIRITUAL_STONE_SUMMON_HOLD_FRAMES 60

// Quest-page A toggle. Returns true if the cursor was on a stone the player
// owns and A was pressed — caller should consume the input frame.
s32 SpiritualStone_TryToggleAtCursor(PlayState* play, Input* input);

// SW97-parallel C/DPad equip for spiritual stones. Returns true on equip.
// Detection mirrors Sw97_TryEquipMedallion but scoped to quest cursorPoint
// 0x12..0x14 (Kokiri/Goron/Zora). Item IDs used are ITEM_KOKIRI_EMERALD etc.
s32 SpiritualStone_TryEquipAtCursor(PlayState* play, Input* input);

// Per-frame tick — runs from Player_UpdateCommon so the C-button hold timer
// stays in lockstep with player input. Drives statue summon (>=60f hold),
// arms the tap-to-warp prompt (release at <60f), and polls the message
// system to execute a queued warp once the prompt closes.
void SpiritualStone_TickHold(PlayState* play, Player* player);

// Passive accessors used by the three speed sites in z_player.c. Each one
// also checks the corresponding CHECK_QUEST_ITEM so the buff turns off
// instantly if the stone is removed (e.g. via debug or rando).
s32 SpiritualStone_KokiriWalkActive(void);
s32 SpiritualStone_GoronClimbActive(void);
s32 SpiritualStone_ZoraSwimActive(void);

// Raw passive toggle for a single stone (0..2 == Kokiri/Goron/Zora). No
// CHECK_QUEST_ITEM gate — for UI display only. Used by the kaleido quest
// page to fade out stones whose passive is disabled.
s32 SpiritualStone_IsPassiveActive(s32 stone);

#ifdef __cplusplus
}
#endif

#endif // SPIRITUAL_STONES_H
