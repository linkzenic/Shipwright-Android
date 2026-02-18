/**
 * mm_form_combat.c - Combat helpers for MM form system
 *
 * Contains: directional hit quad setup, dust effects, damage helpers.
 * #included from mm_player_form.cpp (not compiled separately).
 *
 * These functions use OOT's existing collision system:
 *   - player->meleeWeaponQuads[0] for directional hit detection
 *   - DMG_HAMMER_SWING for Goron punch damage type (same heavy blunt impact)
 *   - Collider_SetQuadVertices for quad geometry
 *   - CollisionCheck_SetAT for registering with collision system
 */

#ifndef MM_FORM_COMBAT_C
#define MM_FORM_COMBAT_C

#include "z64.h"
#include "functions.h"
#include "variables.h"

// =============================================================================
// Directional Quad Hit Detection for Goron Punches
//
// Instead of a radial cylinder (360 degrees), we set up a ColliderQuad
// positioned in front of the Goron based on the punch type:
//   Punch A (left):  Quad offset to front-left
//   Punch B (right): Quad offset to front-right
//   Punch C (butt):  Wider quad centered lower (ground slam)
//
// Uses player->meleeWeaponQuads[0] which is already initialized by OOT's
// Player_Init (Collider_InitQuad + Collider_SetQuad with D_80854650).
// =============================================================================

// Per-punch quad geometry parameters
// { forwardNear, forwardFar, sideOffset, halfWidth, yBottom, yTop }
static const f32 sGoronPunchQuadParams[][6] = {
    // Step 0 - Punch A (left fist): extends 20-55 forward, offset 15 left, height 20-55
    { 20.0f, 55.0f, -15.0f, 15.0f, 20.0f, 55.0f },
    // Step 1 - Punch B (right fist): extends 20-55 forward, offset 15 right, height 20-55
    { 20.0f, 55.0f, 15.0f, 15.0f, 20.0f, 55.0f },
    // Step 2 - Punch C (butt slam): extends -10 to 30 (behind to front), centered, height 5-30
    { -10.0f, 30.0f, 0.0f, 30.0f, 5.0f, 30.0f },
    // Step 3 - Jump kick (Zora): extends 10-70 forward, centered, height 0-40 (foot/leg level)
    // From MM: Zora jump kick lunges forward with legs extended, long reach kick
    { 10.0f, 70.0f, 0.0f, 18.0f, 0.0f, 40.0f },
};

/**
 * Set directional quad vertices for Goron punch hit detection.
 *
 * Creates a rectangular hitbox in front of the player, oriented by yaw.
 * The quad is a 3D plane defined by 4 corners (a,b = far edge, c,d = near edge).
 *
 * @param player  OOT Player pointer
 * @param step    Combo step (0=left, 1=right, 2=butt, 3=jump kick)
 */
static void MmForm_SetPunchQuadVertices(Player* player, u8 step) {
    if (step > 3)
        step = 0;

    const f32* params = sGoronPunchQuadParams[step];
    f32 nearDist = params[0];
    f32 farDist = params[1];
    f32 sideOff = params[2];
    f32 halfW = params[3];
    f32 yBottom = params[4];
    f32 yTop = params[5];

    f32 sinYaw = Math_SinS(player->yaw);
    f32 cosYaw = Math_CosS(player->yaw);

    // Right vector (perpendicular to forward in XZ plane)
    f32 rightX = cosYaw;
    f32 rightZ = -sinYaw;

    Vec3f pos = player->actor.world.pos;

    // Far edge center (tip of punch)
    f32 farCX = pos.x + sinYaw * farDist + rightX * sideOff;
    f32 farCZ = pos.z + cosYaw * farDist + rightZ * sideOff;

    // Near edge center (close to body)
    f32 nearCX = pos.x + sinYaw * nearDist + rightX * sideOff;
    f32 nearCZ = pos.z + cosYaw * nearDist + rightZ * sideOff;

    // 4 vertices: a,b = far top/bottom, c,d = near top/bottom
    // Quad layout: a---b (far edge, top and bottom spread by halfW)
    //              |   |
    //              d---c (near edge)
    Vec3f a, b, c, d;

    a.x = farCX - rightX * halfW;
    a.y = pos.y + yTop;
    a.z = farCZ - rightZ * halfW;

    b.x = farCX + rightX * halfW;
    b.y = pos.y + yTop;
    b.z = farCZ + rightZ * halfW;

    c.x = nearCX + rightX * halfW;
    c.y = pos.y + yBottom;
    c.z = nearCZ + rightZ * halfW;

    d.x = nearCX - rightX * halfW;
    d.y = pos.y + yBottom;
    d.z = nearCZ - rightZ * halfW;

    Collider_SetQuadVertices(&player->meleeWeaponQuads[0], &a, &b, &c, &d);
}

/**
 * Configure and submit punch quad for collision checking.
 *
 * Sets damage type to DMG_HAMMER_SWING (same heavy blunt impact as Megaton Hammer).
 * From 2Ship: Goron punches use meleeWeaponQuads with directional sweep.
 * From OOT: DMG_HAMMER_SWING = (1 << 6) = 0x00000040.
 *
 * @param player  OOT Player pointer
 * @param play    PlayState
 * @param step    Combo step
 * @param damage  Damage amount
 */
static void MmForm_EnablePunchQuad(Player* player, PlayState* play, u8 step, u8 damage) {
    ColliderQuad* quad = &player->meleeWeaponQuads[0];

    // Reset previous frame's AT state
    Collider_ResetQuadAT(play, &quad->base);

    // Set directional vertices for this punch type
    MmForm_SetPunchQuadVertices(player, step);

    // Configure damage: DMG_HAMMER_SWING (same as Megaton Hammer normal swing)
    // This means enemies vulnerable to hammer are also vulnerable to Goron punch
    quad->base.atFlags = AT_ON | AT_TYPE_PLAYER;
    quad->info.toucher.dmgFlags = DMG_HAMMER_SWING;
    quad->info.toucher.damage = damage;
    quad->info.toucherFlags = TOUCH_ON | TOUCH_NEAREST;

    // Submit quad for collision checking this frame
    CollisionCheck_SetAT(play, &play->colChkCtx, &quad->base);
}

/**
 * Disable punch quad hit detection (quad[0] only).
 * Called when punch is outside hit frames or transitions to recovery/idle.
 */
static void MmForm_DisablePunchQuad(Player* player) {
    player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
}

/**
 * Disable both melee weapon quads (used after jump kick which sets both).
 */
static void MmForm_DisableJumpKickQuads(Player* player) {
    player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
    player->meleeWeaponQuads[1].base.atFlags &= ~AT_ON;
}

/**
 * Configure and submit jump kick quads for collision checking.
 *
 * From MM z_player.c func_80833728/func_8083375C:
 *   - DMG_ZORA_PUNCH (1 << 0x17) in MM → maps to DMG_JUMP_MASTER (1 << 0x1B) in OOT
 *     (jumping physical attack, most combat enemies are vulnerable)
 *   - Damage: 2 (dmgHumanStrong / dmgTransformedStrong from MM D_8085D09C)
 *   - ATELEM_ON | ATELEM_NEAREST
 *   - Uses BOTH meleeWeaponQuads[0] and [1] (from MM line 5661-5662)
 *   - Hit frames 8-99 (from sMeleeAttackAnimInfo index 18)
 *
 * @param player  OOT Player pointer
 * @param play    PlayState
 * @param damage  Damage amount (2 for Zora jump kick)
 */
static void MmForm_EnableJumpKickQuad(Player* player, PlayState* play, u8 damage) {
    // Set jump kick geometry (step=3: forward-extending, foot/leg height)
    MmForm_SetPunchQuadVertices(player, 3);

    // Copy quad[0] vertices to quad[1] (MM sets both quads identically)
    Collider_SetQuadVertices(&player->meleeWeaponQuads[1], &player->meleeWeaponQuads[0].dim.quad[2],
                             &player->meleeWeaponQuads[0].dim.quad[3], &player->meleeWeaponQuads[0].dim.quad[0],
                             &player->meleeWeaponQuads[0].dim.quad[1]);

    // Configure both quads with jump attack damage
    // DMG_JUMP_MASTER = OOT equivalent of MM's DMG_ZORA_PUNCH (jumping physical attack)
    for (s32 i = 0; i < 2; i++) {
        ColliderQuad* quad = &player->meleeWeaponQuads[i];
        Collider_ResetQuadAT(play, &quad->base);
        quad->base.atFlags = AT_ON | AT_TYPE_PLAYER;
        quad->info.toucher.dmgFlags = DMG_JUMP_MASTER;
        quad->info.toucher.damage = damage;
        quad->info.toucherFlags = TOUCH_ON | TOUCH_NEAREST;
        CollisionCheck_SetAT(play, &play->colChkCtx, &quad->base);
    }
}

// =============================================================================
// Dust Spawn Effects
//
// From 2Ship func_8083FBC4 (z_player.c line 10511):
// Spawns dust/debris at player feet during fast movement.
// Checks floor type to determine effect:
//   Ground/Sand → dust clouds
//   Snow → snow particles
// Used during: roll, punch ground impact, fast running
//
// OOT equivalents:
//   Actor_SpawnFloorDustRing (z_actor.c) - ring of dust around actor
//   EffectSsDust_Spawn (effect_ss_dust.c) - individual dust particles
// =============================================================================

/**
 * Spawn dust at player position based on floor type.
 * From 2Ship func_8083FBC4. Uses OOT's Actor_SpawnFloorDustRing.
 *
 * @param play    PlayState
 * @param player  OOT Player pointer
 * @return 1 if dust spawned, 0 if floor type has no dust
 */
static s32 MmForm_SpawnMovementDust(PlayState* play, Player* player) {
    // Check floor type for appropriate dust effect
    // OOT uses floorSfxOffset to categorize floor material
    u16 floorSfx = player->floorSfxOffset;

    // Ground, sand, or dirt floors
    if (floorSfx == (NA_SE_PL_WALK_GROUND - SFX_FLAG) || floorSfx == (NA_SE_PL_WALK_SAND - SFX_FLAG) ||
        floorSfx == (NA_SE_PL_WALK_DIRT - SFX_FLAG)) {

        Actor_SpawnFloorDustRing(play, &player->actor, &player->actor.world.pos, player->actor.shape.shadowScale, 1,
                                 8.0f, 500, 10, 1);
        return 1;
    }

    return 0;
}

#endif // MM_FORM_COMBAT_C
