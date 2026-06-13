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

// This file is meant to be #included from mm_player_form.cpp (it relies on
// the static MmFormState gFormState that lives there). If VS picks it up as
// a standalone compilation unit (which the build system does because the
// extension is .c), compile to nothing so the build doesn't fail on
// gFormState being undeclared. The include from mm_player_form.cpp
// #defines MMFORM_COMBAT_AS_INCLUDE right before the #include.
#ifdef MMFORM_COMBAT_AS_INCLUDE

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
 * Caller picks dmgFlags per form:
 *   Goron → DMG_HAMMER_SWING (heavy blunt, mirrors MM's DMG_GORON_PUNCH)
 *   Zora  → DMG_SLASH_MASTER (sword swing, mirrors MM's DMG_ZORA_PUNCH on the
 *           combo punches; jump kick uses DMG_JUMP_MASTER via EnableJumpKickQuad)
 *
 * @param player    OOT Player pointer
 * @param play      PlayState
 * @param step      Combo step
 * @param damage    Damage amount
 * @param dmgFlags  Damage type bitmask (see DMG_* in z64collision_check.h)
 */
static void MmForm_EnablePunchQuad(Player* player, PlayState* play, u8 step, u8 damage, u32 dmgFlags) {
    ColliderQuad* quad = &player->meleeWeaponQuads[0];

    // Reset previous frame's AT state
    Collider_ResetQuadAT(play, &quad->base);

    // Defense: explicitly disable the OTHER melee quad and the body cylinder so any
    // leftover flags from a prior form (e.g. Goron roll set cylinder.dmgFlags =
    // DMG_HAMMER_SWING and we transformed to Zora mid-roll) don't register as a
    // second attack with hammer damage and break hammer rocks during a Zora combo.
    player->meleeWeaponQuads[1].base.atFlags &= ~AT_ON;
    player->meleeWeaponQuads[1].info.toucher.dmgFlags = 0;
    player->cylinder.base.atFlags &= ~AT_ON;
    player->cylinder.info.toucher.dmgFlags = 0;

    // Set directional vertices for this punch type
    MmForm_SetPunchQuadVertices(player, step);

    quad->base.atFlags = AT_ON | AT_TYPE_PLAYER;
    quad->info.toucher.dmgFlags = dmgFlags;
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

// =============================================================================
// Wall Hit Detection During Punch
//
// Replicates MM's func_808401F4 (z_player.c:10513) — runs once per punch frame
// to detect a wall/dyna in front of the punch and either recoil the player or
// allow a dyna actor to take damage normally.
//
// Two flavours, matching MM:
//   Goron form (line 10549): heavy hammer-style recoil (-18 speed), spawn dust,
//     play NA_SE_IT_HAMMER_HIT, screen quake. EXCEPT when the wall is a
//     DynaPoly actor that the punch quad already hit this/prev frame — then
//     don't recoil so the actor's AC handler can register the break (this is
//     what lets Goron Pound smash Bg_Hidan_Dalm totems instead of bouncing).
//   Non-Goron / Zora (line 10583): lighter recoil (-14 speed), spawn shield
//     spark particles, play NA_SE_IT_WALL_HIT_HARD. No action change — but
//     caller must skip enabling the punch quad this frame so the AT collider
//     doesn't reach through the wall to hit something behind it.
// =============================================================================

// OoT functions defined in z_player.c but not exposed in headers. Wrapped in
// extern "C" because this file is #included from mm_player_form.cpp and would
// otherwise get C++ name mangling on the call sites. `this` is a C++ reserved
// word so the parameter is named `player` here.
#ifdef __cplusplus
extern "C" {
#endif
void Player_RequestQuake(PlayState* play, s32 speed, s32 y, s32 countdown);
void Player_RequestRumble(Player* player, s32 sourceStrength, s32 duration, s32 decreaseRate, s32 distSq);
#ifdef __cplusplus
}
#endif

typedef enum {
    MMFORM_WALL_HIT_NONE = 0,
    MMFORM_WALL_HIT_GORON = 1, // Goron path: recoil applied, caller should end the punch
    MMFORM_WALL_HIT_ZORA = 2,  // Zora path: recoil applied, caller must skip AT enable this frame
} MmFormWallHitResult;

/**
 * Detect a wall in front of the punch and apply MM-style recoil.
 *
 * @param player    OOT Player pointer
 * @param play      PlayState
 * @param step      Current combo step (0..3) — controls reach/height
 * @param isGoron   1 = Goron heavy-recoil path, 0 = Zora light-recoil path
 * @param curFrame  Animation curFrame (gFormState.formSkelAnime.curFrame)
 * @param minFrame  Earliest curFrame the check is allowed to fire. MM's
 *                  func_808401F4 gates on `meleeWeaponState >= 1`, which is
 *                  only set AFTER the quad has fired once. We emulate that by
 *                  requiring the caller to pass `earlyStart + 1.0f` so the
 *                  quad has had at least one frame to register an AT_HIT —
 *                  this is what lets the Goron dyna-actor exception fire on
 *                  breakables like Bg_Hidan_Dalm (otherwise the very first
 *                  frame would always recoil before the quad could hit).
 */
static MmFormWallHitResult MmForm_CheckWallHit(Player* player, PlayState* play, u8 step, u8 isGoron, f32 curFrame,
                                               f32 minFrame) {
    if (curFrame < minFrame) {
        return MMFORM_WALL_HIT_NONE;
    }
    // Already bouncing off something this attack — MM line 10518-10519.
    if ((player->meleeWeaponQuads[0].base.atFlags & AT_BOUNCED) ||
        (player->meleeWeaponQuads[1].base.atFlags & AT_BOUNCED)) {
        return MMFORM_WALL_HIT_NONE;
    }
    // Don't fire while already recoiling — MM line 10530, 10583.
    if (player->linearVelocity < 0.0f) {
        return MMFORM_WALL_HIT_NONE;
    }

    if (step > 3) {
        step = 0;
    }

    // Use the same quad geometry the puñetazo uses, so the wall ray matches
    // the fist position (sGoronPunchQuadParams is defined above).
    const f32* params = sGoronPunchQuadParams[step];
    f32 farDist = params[1];
    f32 yMid = (params[4] + params[5]) * 0.5f;

    f32 sinYaw = Math_SinS(player->yaw);
    f32 cosYaw = Math_CosS(player->yaw);

    Vec3f rayStart;
    rayStart.x = player->actor.world.pos.x;
    rayStart.y = player->actor.world.pos.y + yMid;
    rayStart.z = player->actor.world.pos.z;

    // Extend 10 units past the punch tip (matches MM's `+10.0f` in line 10538).
    f32 reach = farDist + 10.0f;
    Vec3f rayEnd;
    rayEnd.x = player->actor.world.pos.x + sinYaw * reach;
    rayEnd.y = rayStart.y;
    rayEnd.z = player->actor.world.pos.z + cosYaw * reach;

    CollisionPoly* poly = NULL;
    s32 bgId = BGCHECK_SCENE;
    Vec3f hitPos;

    if (!BgCheck_EntityLineTest1(&play->colCtx, &rayStart, &rayEnd, &hitPos, &poly, true, false, false, true, &bgId)) {
        return MMFORM_WALL_HIT_NONE;
    }
    if (poly == NULL) {
        return MMFORM_WALL_HIT_NONE;
    }
    if (SurfaceType_IsIgnoredByEntities(&play->colCtx, poly, bgId)) {
        return MMFORM_WALL_HIT_NONE;
    }

    if (isGoron) {
        // Dyna-actor exception (MM line 10565-10574): if the wall belongs to a
        // dyna actor that our punch quad already hit, suppress the recoil so
        // the actor's AC handler can break it (Bg_Hidan_Dalm totem, etc.).
        //
        // Candidate-tracking extension: the wall raycast extends 10 units past
        // the punch tip, so a breakable DynaPoly (Bg_Bombwall etc.) is detected
        // BEFORE the punch quad has had a chance to AT_HIT it — especially when
        // Goron is closing distance via root-motion (the AT_HIT visible here is
        // from last frame's collision pass, but Goron may have only entered
        // quad range THIS frame). Without grace, recoil fires before any
        // damage can land → inconsistent breakable destruction. So we hold the
        // dyna as a pending candidate and give it a few frames for the AT-AC
        // exchange to register before bouncing.
        if (bgId != BGCHECK_SCENE) {
            DynaPolyActor* dyna = DynaPoly_GetActor(&play->colCtx, bgId);
            if (dyna != NULL) {
                // Already AT_HIT'd this frame? Permanent skip + clear candidate.
                if ((player->meleeWeaponQuads[0].base.atFlags & AT_HIT) &&
                    (&dyna->actor == player->meleeWeaponQuads[0].base.at)) {
                    gFormState.punchWallPendingDyna = NULL;
                    return MMFORM_WALL_HIT_NONE;
                }
                if ((player->meleeWeaponQuads[1].base.atFlags & AT_HIT) &&
                    (&dyna->actor == player->meleeWeaponQuads[1].base.at)) {
                    gFormState.punchWallPendingDyna = NULL;
                    return MMFORM_WALL_HIT_NONE;
                }

                // No AT_HIT yet — defer recoil. First sighting of this dyna or
                // a different dyna than previously tracked: (re)start the
                // 4-frame grace window. Same dyna already pending: tick down.
                if (gFormState.punchWallPendingDyna != &dyna->actor) {
                    gFormState.punchWallPendingDyna = &dyna->actor;
                    gFormState.punchWallPendingFrames = 4;
                    return MMFORM_WALL_HIT_NONE;
                }
                if (gFormState.punchWallPendingFrames > 0) {
                    gFormState.punchWallPendingFrames--;
                    return MMFORM_WALL_HIT_NONE;
                }
                // Grace expired — fall through to recoil.
                gFormState.punchWallPendingDyna = NULL;
            }
        }

        // Heavy hammer impact (MM line 10554-10562 + func_808400CC).
        Player_PlaySfx(&player->actor, NA_SE_IT_HAMMER_HIT);
        Player_RequestQuake(play, 27767, 7, 20);
        EffectSsHahen_SpawnBurst(play, &hitPos, 4.0f, 0, 12, 6, 3, -1, 10, NULL);
        player->linearVelocity = -18.0f;
        Player_RequestRumble(player, 180, 20, 100, 0);

        // Disable AT immediately — recoil starts now.
        player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
        player->meleeWeaponQuads[1].base.atFlags &= ~AT_ON;
        return MMFORM_WALL_HIT_GORON;
    }

    // Zora / lighter forms (MM line 10583-10605).
    CollisionCheck_SpawnShieldParticles(play, &hitPos);
    Player_PlaySfx(&player->actor, NA_SE_IT_WALL_HIT_HARD);
    player->linearVelocity = -14.0f;
    Player_RequestRumble(player, 180, 20, 100, 0);
    // Suppress AT this frame so the swing can't reach past the wall.
    player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
    player->meleeWeaponQuads[1].base.atFlags &= ~AT_ON;
    return MMFORM_WALL_HIT_ZORA;
}

#endif // MMFORM_COMBAT_AS_INCLUDE

#endif // MM_FORM_COMBAT_C
