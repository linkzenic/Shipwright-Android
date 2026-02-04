/**
 * Time Gate Item Header
 * Age-swapping spell - costs 48 MP, plays Nayru's Love cast animation,
 * Link hovers in a warp animation, then a Yes/No prompt asks to change age.
 * If Yes: consume magic, call SwitchAge() (scene reloads with opposite age).
 * If No: cancel animation, return control, no magic consumed.
 * No cooldown.
 */

#ifndef ITEM_TIME_GATE_H
#define ITEM_TIME_GATE_H

#include "z64.h"
#include "../custom_items.h"

// States
#define TGATE_STATE_IDLE        0
#define TGATE_STATE_CASTING     1   // Nayru's Love animation (tamashii1 -> tamashii2 -> tamashii3)
#define TGATE_STATE_HOVERING    2   // Warp hover animation + Yes/No textbox
#define TGATE_STATE_SWITCHING   3   // User chose Yes - consume magic, SwitchAge()
#define TGATE_STATE_CANCEL      4   // User chose No - exit animation, release control

// Casting sub-phases (Nayru's Love 3-part animation)
#define TGATE_CAST_TAMASHII1    0
#define TGATE_CAST_TAMASHII2    1
#define TGATE_CAST_TAMASHII3    2

// Magic
#define TGATE_MAGIC_COST        48

// Timing
#define TGATE_HOVER_SETTLE      10  // Frames to settle into hover before showing textbox
#define TGATE_CANCEL_DURATION   30  // Frames for cancel/exit animation

// Animation frame triggers
#define TGATE_CAST_ITEM_FRAME   10  // Frame when Link "places" item (show item + spawn portal)
#define TGATE_HOVER_LOOP_START  (Animation_GetLastFrame(&gPlayerAnim_link_demo_warp) - 2.0f)  // Last 2 frames for hover loop

// Custom text ID (0x9213 - unused range in custom message space)
#define TEXT_TIME_GATE_PROMPT    0x9213

// State aliases
#define tgActive        gCustomItemState.timeGateActive
#define tgState         gCustomItemState.timeGateState
#define tgSubPhase      gCustomItemState.timeGateSubPhase
#define tgTimer         gCustomItemState.timeGateTimer
#define tgPromptShown   gCustomItemState.timeGatePromptShown
#define tgItemVisible   gCustomItemState.timeGateItemVisible
#define tgPortalActive  gCustomItemState.timeGatePortalActive
#define tgPortalAlpha   gCustomItemState.timeGatePortalAlpha
#define tgPortalScale   gCustomItemState.timeGatePortalScale

#endif // ITEM_TIME_GATE_H
