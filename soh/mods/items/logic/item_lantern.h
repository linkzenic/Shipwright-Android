/**
 * item_lantern.h - Poe Lantern: catch fire, illuminate, use elemental effects
 *
 * Uses gPoeLanternDL from object_poh. Behaves like bottle swing but catches
 * fire instead of creatures. Fire persists until manually extinguished.
 * When lit, acts as a real point light source.
 */

#ifndef ITEM_LANTERN_H
#define ITEM_LANTERN_H

// ── Fire Types ──────────────────────────────────────────────────────────────

typedef enum {
    LANTERN_FIRE_NONE    = 0,
    LANTERN_FIRE_REGULAR = 1, // Orange — Obj_Syokudai (torches), En_Bw (torch slug), En_Light (orange)
    LANTERN_FIRE_BLUE    = 2, // Blue — En_Ice_Hono (blue fire)
    LANTERN_FIRE_POE     = 3, // Purple — En_Poh, Bg_Po_Syokudai (poe torches)
    LANTERN_FIRE_GREEN   = 4, // Green — En_Light (green params, Spirit Temple etc.)
    LANTERN_FIRE_MAX
} LanternFireType;

// ── Constants ───────────────────────────────────────────────────────────────

#define LANTERN_CATCH_RANGE     80.0f  // Max distance to catch fire source
#define LANTERN_EFFECT_RANGE   120.0f  // Max distance for fire effects on swing
#define LANTERN_LIGHT_RADIUS   200     // Point light radius when lit
#define LANTERN_KALEIDO_HOLD    20     // Frames to hold C in Kaleido to extinguish
#define LANTERN_GREEN_HEAL_RATE 150    // Frames between 1/4 heart heals (5 seconds)
#define TEXT_LANTERN_CATCH     0x00F9 // Custom textbox ID for fire catch messages

// Swing frame timing (matches bottle swing: gPlayerAnim_link_bottle_bug_miss)
#define LANTERN_CATCH_START      2     // First active catch frame
#define LANTERN_CATCH_END        5     // Last active catch frame
#define LANTERN_SWING_TOTAL     16     // Total swing animation frames

// ── Light Colors per fire type ──────────────────────────────────────────────

static const u8 sLanternLightColors[][3] = {
    {   0,   0,   0 }, // NONE — no light
    { 255, 180,  80 }, // REGULAR — warm orange
    {  80, 150, 255 }, // BLUE — icy blue
    { 180,  80, 255 }, // POE — ghostly purple
    {  80, 255, 120 }, // GREEN — spirit green
};

// ── Catchable fire sources ──────────────────────────────────────────────────

typedef struct {
    s16 actorId;
    LanternFireType fireType;
} LanternCatchEntry;

// Defined in item_lantern.c
// extern const LanternCatchEntry sCatchableFires[];

// ── Lantern catch message ──────────────────────────────────────────────────
// Set to a LanternFireType when fire is caught; ItemMessages.cpp reads it
// to build the textbox content. Reset to 0 after message is shown.
extern u8 gLanternCatchPending;

// Public API declared in item_lantern.c (unity build — no forward declarations needed here)

#endif // ITEM_LANTERN_H
