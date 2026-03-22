#ifndef SSBB_HITBOX_H
#define SSBB_HITBOX_H

#include "z64.h"

// ── SSBB Hitbox System ──────────────────────────────────────────────────────
// Positions attack colliders at bone world-space positions from the skinning system.
// Uses OOT's existing ColliderCylinder for AT (attack toucher).

// Damage type flags (OOT bit masks)
#define SSBB_DMG_SLASH 0x00000700u       // DMG_SLASH_KOKIRI | MASTER | GIANT
#define SSBB_DMG_BOMB 0x00000008u        // DMG_EXPLOSIVE
#define SSBB_DMG_ARROW 0x00000024u       // DMG_ARROW_NORMAL | DMG_SLINGSHOT
#define SSBB_DMG_BOOMERANG 0x00000010u   // DMG_BOOMERANG
#define SSBB_DMG_HAMMER 0x00000040u      // DMG_HAMMER_SWING
#define SSBB_DMG_MAGIC_FIRE 0x00020000u  // DMG_MAGIC_FIRE
#define SSBB_DMG_MAGIC_LIGHT 0x00080000u // DMG_MAGIC_LIGHT
#define SSBB_DMG_ELECTRIC (SSBB_DMG_MAGIC_LIGHT | SSBB_DMG_MAGIC_FIRE)

// Per-action hitbox definition
typedef struct {
    u8 boneIndex; // Bone to attach hitbox to (0 = actor center)
    Vec3f offset; // Offset from bone position
    f32 radius;   // Collider radius
    f32 height;   // Collider height
    s16 damage;   // Damage in quarter-hearts
    u32 dmgFlags; // OOT damage type bit mask
    u8 sfxType;   // TOUCH_SFX_WOOD, TOUCH_SFX_HARD, etc.
} SSBBHitboxDef;

// Pikachu-specific hitbox definitions per attack type
// These can be expanded per-character

// Standard electric attack (jab, tilts, aerials)
#define SSBB_HITBOX_ELECTRIC_SMALL \
    { 5, { 0, 0, 0 }, 25, 35, 4, SSBB_DMG_ELECTRIC, TOUCH_SFX_WOOD }
#define SSBB_HITBOX_ELECTRIC_MED \
    { 5, { 0, 0, 0 }, 35, 40, 6, SSBB_DMG_ELECTRIC, TOUCH_SFX_WOOD }
#define SSBB_HITBOX_ELECTRIC_LARGE \
    { 5, { 0, 0, 0 }, 50, 50, 8, SSBB_DMG_ELECTRIC, TOUCH_SFX_HARD }

// Smash attacks (stronger)
#define SSBB_HITBOX_FSMASH \
    { 5, { 20, 0, 0 }, 40, 45, 10, SSBB_DMG_ELECTRIC, TOUCH_SFX_HARD }
#define SSBB_HITBOX_USMASH \
    { 5, { 0, 20, 0 }, 35, 50, 12, SSBB_DMG_ELECTRIC, TOUCH_SFX_HARD }
#define SSBB_HITBOX_DSMASH \
    { 5, { 0, 0, 0 }, 45, 30, 10, SSBB_DMG_ELECTRIC, TOUCH_SFX_HARD }

// Thunder (down-B) — large AoE, light arrow damage
#define SSBB_HITBOX_THUNDER \
    { 0, { 0, 0, 0 }, 90, 200, 16, SSBB_DMG_MAGIC_LIGHT, TOUCH_SFX_HARD }

// Skull Bash — body hitbox while dashing
#define SSBB_HITBOX_SKULL_BASH \
    { 5, { 0, 0, 0 }, 30, 40, 8, SSBB_DMG_SLASH, TOUCH_SFX_HARD }

// Grab (hookshot) — wide forward cylinder
#define SSBB_HITBOX_GRAB \
    { 5, { 40, 0, 0 }, 55, 70, 2, SSBB_DMG_BOOMERANG, TOUCH_SFX_WOOD }

// Quick Attack — body hitbox during dash
#define SSBB_HITBOX_QUICK_ATK \
    { 5, { 0, 0, 0 }, 30, 60, 4, SSBB_DMG_BOOMERANG, TOUCH_SFX_WOOD }

// Hammer
#define SSBB_HITBOX_HAMMER \
    { 28, { 0, 0, 0 }, 40, 50, 12, SSBB_DMG_HAMMER, TOUCH_SFX_HARD }

#endif // SSBB_HITBOX_H
