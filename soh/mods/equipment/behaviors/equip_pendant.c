/**
 * equip_pendant.c - Pendant of Memories (Ext Boots 2)
 *
 * Combat enhancement equipment with 3 attacks from other Zelda/Nintendo games:
 * #1 Mortal Draw (TP) — B near enemy + sheathed + still + no Z-target → devastating draw slash + hitstop
 * #2 Ground Pound (Smash) — aerial B → stall → fast fall → pogo bounce / shockwave on landing
 * #3 Parry Leap (WW) — Z-target + 3 side hops + B → parabolic arc over enemy, land behind
 *
 * Included by ext_equip_behavior.c (unity build from extended_equipment.c → z_player.c)
 */

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
typedef enum {
    PENDANT_IDLE,
    // Mortal Draw (TP)
    PENDANT_DRAW_SLASH,    // Draw-and-cut in one motion
    PENDANT_DRAW_HITSTOP,  // Dramatic freeze on hit
    PENDANT_DRAW_RECOVERY, // Brief recovery, sword stays out
    // Ground Pound (Smash)
    PENDANT_GPOUND_STALL,   // Pause in air before falling
    PENDANT_GPOUND_FALLING, // Fast falling with sword down
    PENDANT_GPOUND_LANDING, // Impact + shockwave recovery
    // Parry Leap (WW)
    PENDANT_PARRY_ARC,     // Parabolic arc over enemy
    PENDANT_PARRY_LANDING, // Landing recovery
} PendantState;

static PendantState sPendantState = PENDANT_IDLE;
static s16 sPendantTimer = 0;
static Actor* sPendantTarget = NULL;

// --- Mortal Draw constants ---
#define MORTAL_DRAW_RANGE 200.0f // Close combat range
#define MORTAL_DRAW_MIN_RANGE 20.0f
#define MORTAL_DRAW_HITSTOP 10      // Freeze frames on hit
#define MORTAL_DRAW_RECOVERY 15     // Recovery frames (sword stays drawn)
#define MORTAL_DRAW_SLASH_FRAMES 12 // Max slash duration before recovery
#define MORTAL_DRAW_DAMAGE 0xFF     // One-hit kill (255 quarter-hearts)

// --- Ground Pound constants ---
#define GPOUND_STALL_FRAMES 4       // Stall in air before falling
#define GPOUND_FALL_VELOCITY -18.0f // Fast fall initial velocity
#define GPOUND_FALL_GRAVITY -2.5f   // Fast fall gravity
#define GPOUND_LANDING_FRAMES 25    // Landing recovery
#define GPOUND_BOUNCE_VEL 8.0f      // Pogo bounce velocity

// --- Parry Leap constants ---
#define PARRY_ARC_FRAMES 22     // Total frames for the arc
#define PARRY_ARC_HEIGHT 80.0f  // Just above enemy head, skim over
#define PARRY_LAND_DIST 120.0f  // Distance behind enemy to land
#define PARRY_LANDING_FRAMES 12 // Landing recovery

// Parry Leap arc data
static Vec3f sPendantArcStart;
static Vec3f sPendantArcEnd;
static u8 sPendantParryHit = 0; // Only deal damage once per leap

// Side hop tracking (Parry Leap trigger)
static u8 sPendantSideHopCount;
static s16 sPendantHopTimer;
static u8 sPendantSpinReady = 0;
static u8 sPendantWasHopping = 0;

// Shared attack collider (ground pound shockwave + parry leap)
static ColliderCylinder sPendantAtkCol;
static u8 sPendantAtkColInit = 0;
static ColliderCylinderInit sPendantAtkColInit_data = {
    { COLTYPE_NONE, AT_ON | AT_TYPE_PLAYER, AC_NONE, OC1_NONE, OC2_NONE, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK2, { 0x00000100, 0x00, 0x08 }, { 0, 0, 0 }, TOUCH_ON | TOUCH_SFX_NORMAL, BUMP_NONE, OCELEM_NONE },
    { 80, 80, 0, { 0, 0, 0 } }
};

static void Pendant_InitAtkCol(PlayState* play, Player* player) {
    if (!sPendantAtkColInit) {
        Collider_InitCylinder(play, &sPendantAtkCol);
        Collider_SetCylinder(play, &sPendantAtkCol, &player->actor, &sPendantAtkColInit_data);
        sPendantAtkColInit = 1;
    }
}

static void Pendant_UpdateAtkCol(PlayState* play, Player* player) {
    sPendantAtkCol.dim.pos.x = player->actor.world.pos.x;
    sPendantAtkCol.dim.pos.y = player->actor.world.pos.y;
    sPendantAtkCol.dim.pos.z = player->actor.world.pos.z;
    sPendantAtkCol.base.atFlags |= AT_ON;
    CollisionCheck_SetAT(play, &play->colChkCtx, &sPendantAtkCol.base);

    if (sPendantAtkCol.base.atFlags & AT_HIT) {
        Audio_PlaySoundGeneral(NA_SE_IT_SWORD_STRIKE, &player->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        sPendantAtkCol.base.atFlags &= ~AT_HIT;
    }
}

// ===================================================================
// #1  Mortal Draw (Twilight Princess)
//
// B + standing still + nearby enemy + sheathed + NOT Z-targeting
// → face enemy, devastating in-place draw slash, hitstop on hit
// ===================================================================
static u8 Pendant_CheckMortalDraw(Player* player, PlayState* play) {
    if (sPendantState != PENDANT_IDLE)
        return 0;

    // B pressed
    if (!CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B))
        return 0;

    // Sword must be sheathed
    if (Player_GetMeleeWeaponHeld(player) != 0)
        return 0;

    // Must NOT be Z-targeting (TP risk/reward: no lock-on)
    if (player->stateFlags1 & PLAYER_STATE1_Z_TARGETING)
        return 0;

    // On ground, standing still or barely moving
    if (!(player->actor.bgCheckFlags & 1))
        return 0;
    if (player->linearVelocity > 1.0f)
        return 0;
    if (player->stateFlags1 &
        (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_LOADING | PLAYER_STATE1_IN_ITEM_CS))
        return 0;

    // Scan for closest enemy within Mortal Draw range
    Actor* enemy = play->actorCtx.actorLists[ACTORCAT_ENEMY].head;
    f32 closestDist = MORTAL_DRAW_RANGE;
    Actor* best = NULL;
    while (enemy != NULL) {
        f32 d = Actor_WorldDistXZToActor(&player->actor, enemy);
        if (d < closestDist && d > MORTAL_DRAW_MIN_RANGE) {
            closestDist = d;
            best = enemy;
        }
        enemy = enemy->next;
    }
    if (best == NULL)
        return 0;

    sPendantTarget = best;
    return 1;
}

static void Pendant_StartMortalDraw(Player* player, PlayState* play) {
    sPendantState = PENDANT_DRAW_SLASH;
    sPendantTimer = 0;

    // Face enemy
    s16 yaw = Actor_WorldYawTowardActor(&player->actor, sPendantTarget);
    player->actor.shape.rot.y = yaw;
    player->yaw = yaw;

    // No movement — TP Mortal Draw is in-place
    player->linearVelocity = 0.0f;
    player->actor.velocity.x = 0.0f;
    player->actor.velocity.z = 0.0f;

    // Fast iaijutsu draw slash animation
    LinkAnimation_Change(play, &player->skelAnime, &gPlayerAnim_link_fighter_power_kiru_start, 2.0f, 0.0f,
                         Animation_GetLastFrame(&gPlayerAnim_link_fighter_power_kiru_start), ANIMMODE_ONCE, -3.0f);

    // Devastating damage — one-hit kill on most enemies
    func_80837948(play, player, PLAYER_MWA_JUMPSLASH_START);
    player->meleeWeaponQuads[0].info.toucher.damage = MORTAL_DRAW_DAMAGE;
    player->meleeWeaponQuads[1].info.toucher.damage = MORTAL_DRAW_DAMAGE;

    Audio_PlaySoundGeneral(NA_SE_IT_SWORD_SWING_HARD, &player->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

static void Pendant_UpdateMortalDraw(Player* player, PlayState* play) {
    sPendantTimer++;
    player->linearVelocity = 0.0f;

    // Check for sword quad hit → enter hitstop
    u8 hit = 0;
    if (player->meleeWeaponQuads[0].base.atFlags & AT_HIT)
        hit = 1;
    if (player->meleeWeaponQuads[1].base.atFlags & AT_HIT)
        hit = 1;

    if (hit) {
        sPendantState = PENDANT_DRAW_HITSTOP;
        sPendantTimer = 0;

        // Dramatic camera quake
        s16 quakeIdx = Quake_Add(play->cameraPtrs[0], 3);
        Quake_SetSpeed(quakeIdx, 20000);
        Quake_SetQuakeValues(quakeIdx, 8, 0, 0, 0);
        Quake_SetCountdown(quakeIdx, MORTAL_DRAW_HITSTOP);

        // Rumble
        func_800AA000(180.0f, 14, 100, 0);

        // Heavy impact sound
        Audio_PlaySoundGeneral(NA_SE_IT_SWORD_STRIKE, &player->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    // If slash finishes without hit, go to recovery
    if (sPendantTimer > MORTAL_DRAW_SLASH_FRAMES) {
        sPendantState = PENDANT_DRAW_RECOVERY;
        sPendantTimer = 0;
    }
}

static void Pendant_UpdateDrawHitstop(Player* player, PlayState* play) {
    sPendantTimer++;

    // Freeze Link during hitstop (enemies have their own built-in hit-freeze)
    player->skelAnime.playSpeed = 0.0f;
    player->linearVelocity = 0.0f;
    player->actor.velocity.x = 0.0f;
    player->actor.velocity.z = 0.0f;

    if (sPendantTimer >= MORTAL_DRAW_HITSTOP) {
        sPendantState = PENDANT_DRAW_RECOVERY;
        sPendantTimer = 0;
        player->skelAnime.playSpeed = 1.0f;
    }
}

static void Pendant_UpdateDrawRecovery(Player* player, PlayState* play) {
    sPendantTimer++;
    player->linearVelocity = 0.0f;

    // No resheathe — sword stays drawn (like TP)
    if (sPendantTimer >= MORTAL_DRAW_RECOVERY) {
        sPendantState = PENDANT_IDLE;
        sPendantTarget = NULL;
    }
}

// ===================================================================
// #2  Ground Pound (Smash Bros)
//
// In air + B + sword held → stall → fast fall → pogo bounce / shockwave
// ===================================================================
static u8 Pendant_CheckGroundPound(Player* player, PlayState* play) {
    if (sPendantState != PENDANT_IDLE)
        return 0;
    if (!(player->stateFlags3 & PLAYER_STATE3_MIDAIR))
        return 0;
    if (Player_GetMeleeWeaponHeld(player) == 0)
        return 0;
    if (!CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B))
        return 0;
    return 1;
}

static void Pendant_StartGroundPound(Player* player, PlayState* play) {
    sPendantState = PENDANT_GPOUND_STALL;
    sPendantTimer = 0;

    // Stall: freeze in air
    player->actor.velocity.y = 0.0f;
    player->actor.gravity = 0.0f;
    player->linearVelocity = 0.0f;
    player->actor.velocity.x = 0.0f;
    player->actor.velocity.z = 0.0f;

    // Master Sword pedestal plant anim — frozen at frame 0 during stall (sword raised)
    LinkAnimation_Change(play, &player->skelAnime, &gPlayerAnim_link_demo_return_to_past, 0.0f, 0.0f,
                         Animation_GetLastFrame(&gPlayerAnim_link_demo_return_to_past), ANIMMODE_ONCE, -3.0f);

    // Init collider for pogo bounce
    Pendant_InitAtkCol(play, player);

    Audio_PlaySoundGeneral(NA_SE_IT_SWORD_SWING_HARD, &player->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

static void Pendant_UpdateGPoundStall(Player* player, PlayState* play) {
    sPendantTimer++;

    // Hold in air — zero everything
    player->actor.velocity.y = 0.0f;
    player->actor.gravity = 0.0f;
    player->linearVelocity = 0.0f;

    if (sPendantTimer >= GPOUND_STALL_FRAMES) {
        // Transition to fast fall
        sPendantState = PENDANT_GPOUND_FALLING;
        sPendantTimer = 0;

        player->actor.velocity.y = GPOUND_FALL_VELOCITY;
        player->actor.gravity = GPOUND_FALL_GRAVITY;

        // Set up sword damage FIRST (this changes player action and overrides anim)
        func_80837948(play, player, PLAYER_MWA_JUMPSLASH_START);

        // Sword slams down — force pedestal plant anim AFTER func_80837948
        LinkAnimation_Change(play, &player->skelAnime, &gPlayerAnim_link_demo_return_to_past, 2.0f, 0.0f,
                             Animation_GetLastFrame(&gPlayerAnim_link_demo_return_to_past), ANIMMODE_ONCE, 0.0f);
    }
}

static void Pendant_UpdateGPoundFalling(Player* player, PlayState* play) {
    sPendantTimer++;
    player->actor.gravity = GPOUND_FALL_GRAVITY;
    player->linearVelocity = 0.0f;

    // Force pedestal plant anim every frame (OOT's melee action tries to override)
    if (player->skelAnime.animation != &gPlayerAnim_link_demo_return_to_past) {
        LinkAnimation_Change(play, &player->skelAnime, &gPlayerAnim_link_demo_return_to_past, 2.0f, 0.0f,
                             Animation_GetLastFrame(&gPlayerAnim_link_demo_return_to_past), ANIMMODE_ONCE, 0.0f);
    }

    // --- Pogo bounce: check AT_HIT from previous frame BEFORE re-registering ---
    if (sPendantAtkCol.base.atFlags & AT_HIT) {
        // Bounce up like Smash dair pogo
        player->actor.velocity.y = GPOUND_BOUNCE_VEL;
        player->actor.gravity = -1.2f;
        sPendantState = PENDANT_IDLE;
        sPendantAtkCol.base.atFlags &= ~(AT_HIT | AT_ON);

        Audio_PlaySoundGeneral(NA_SE_IT_SWORD_STRIKE, &player->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    // Register collider at current position for pogo detection next frame
    sPendantAtkCol.dim.pos.x = player->actor.world.pos.x;
    sPendantAtkCol.dim.pos.y = player->actor.world.pos.y;
    sPendantAtkCol.dim.pos.z = player->actor.world.pos.z;
    sPendantAtkCol.base.atFlags |= AT_ON;
    CollisionCheck_SetAT(play, &play->colChkCtx, &sPendantAtkCol.base);

    // --- Ground impact ---
    if (player->actor.bgCheckFlags & 1) {
        sPendantState = PENDANT_GPOUND_LANDING;
        sPendantTimer = 0;

        // Shockwave collider
        Pendant_UpdateAtkCol(play, player);

        // Screen shake + rumble
        func_800AA000(255.0f, 20, 150, 0);
        s16 quakeIdx = Quake_Add(play->cameraPtrs[0], 3);
        Quake_SetSpeed(quakeIdx, 28000);
        Quake_SetQuakeValues(quakeIdx, 14, 2, 100, 0);
        Quake_SetCountdown(quakeIdx, 16);

        Audio_PlaySoundGeneral(NA_SE_IT_HAMMER_HIT, &player->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);

        // Landing animation
        LinkAnimation_Change(play, &player->skelAnime, &gPlayerAnim_link_normal_landing, 1.0f, 0.0f,
                             Animation_GetLastFrame(&gPlayerAnim_link_normal_landing), ANIMMODE_ONCE, -6.0f);

        player->actor.gravity = -1.0f;
    }

    // Timeout safety
    if (sPendantTimer > 60) {
        sPendantState = PENDANT_IDLE;
        player->actor.gravity = -1.2f;
    }
}

static void Pendant_UpdateGPoundLanding(Player* player, PlayState* play) {
    sPendantTimer++;
    player->linearVelocity = 0.0f;

    // Shockwave collider active for first few frames
    if (sPendantTimer < 6) {
        Pendant_UpdateAtkCol(play, player);
    }

    if (sPendantTimer > GPOUND_LANDING_FRAMES) {
        sPendantState = PENDANT_IDLE;
    }
}

// ===================================================================
// #3  Parry Leap (Wind Waker)
//
// Z-target + 3 consecutive side hops → B → parabolic arc over enemy
// ===================================================================
static u8 Pendant_CheckParryLeap(Player* player, PlayState* play) {
    if (sPendantState != PENDANT_IDLE)
        return 0;

    // Must be Z-targeting
    if (!(player->stateFlags1 & PLAYER_STATE1_Z_TARGETING)) {
        sPendantSideHopCount = 0;
        sPendantSpinReady = 0;
        sPendantWasHopping = 0;
        return 0;
    }

    // Count hops (rising edge of HOPPING flag)
    u8 isHopping = (player->stateFlags2 & PLAYER_STATE2_HOPPING) ? 1 : 0;
    if (isHopping && !sPendantWasHopping) {
        sPendantSideHopCount++;
        sPendantHopTimer = 0;

        if (sPendantSideHopCount >= 3 && !sPendantSpinReady) {
            sPendantSpinReady = 1;
            Sfx_PlaySfxCentered(NA_SE_SY_LOCK_ON);
        }
    }
    sPendantWasHopping = isHopping;

    // B triggers the leap after 3 hops
    if (sPendantSpinReady) {
        if (CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B)) {
            sPendantSpinReady = 0;
            sPendantSideHopCount = 0;

            if (player->focusActor == NULL)
                return 0;
            return 1;
        }
    }

    // Decay if too long between hops
    sPendantHopTimer++;
    if (sPendantHopTimer > 60) {
        sPendantSideHopCount = 0;
        sPendantSpinReady = 0;
    }

    return 0;
}

static void Pendant_StartParryLeap(Player* player, PlayState* play) {
    sPendantState = PENDANT_PARRY_ARC;
    sPendantTimer = 0;
    sPendantTarget = player->focusActor;
    sPendantParryHit = 0;

    // Arc start: current position
    sPendantArcStart = player->actor.world.pos;

    // Arc end: behind enemy along approach direction
    f32 dx = sPendantTarget->world.pos.x - player->actor.world.pos.x;
    f32 dz = sPendantTarget->world.pos.z - player->actor.world.pos.z;
    f32 distXZ = sqrtf(dx * dx + dz * dz);
    if (distXZ < 1.0f)
        distXZ = 1.0f;
    f32 dirX = dx / distXZ;
    f32 dirZ = dz / distXZ;

    sPendantArcEnd.x = sPendantTarget->world.pos.x + dirX * PARRY_LAND_DIST;
    sPendantArcEnd.z = sPendantTarget->world.pos.z + dirZ * PARRY_LAND_DIST;
    sPendantArcEnd.y = sPendantArcStart.y;

    // Kill all velocity, disable gravity — arc controls position directly
    player->actor.velocity.x = 0.0f;
    player->actor.velocity.y = 0.0f;
    player->actor.velocity.z = 0.0f;
    player->linearVelocity = 0.0f;
    player->actor.gravity = 0.0f;
    player->actor.bgCheckFlags &= ~1;
    player->stateFlags3 |= PLAYER_STATE3_MIDAIR;
    player->invincibilityTimer = -(PARRY_ARC_FRAMES + 5); // Negative = no red flash

    // Jump slash damage
    func_80837948(play, player, PLAYER_MWA_JUMPSLASH_START);

    // Face enemy
    s16 yawToEnemy = Actor_WorldYawTowardActor(&player->actor, sPendantTarget);
    player->actor.shape.rot.y = yawToEnemy;
    player->yaw = yawToEnemy;

    // Spinning roll animation (WW parry spin)
    LinkAnimation_Change(play, &player->skelAnime, &gPlayerAnim_link_normal_landing_roll, 3.0f, 0.0f,
                         Animation_GetLastFrame(&gPlayerAnim_link_normal_landing_roll), ANIMMODE_LOOP, -3.0f);

    // Init attack collider
    Pendant_InitAtkCol(play, player);

    Audio_PlaySoundGeneral(NA_SE_IT_SWORD_SWING_HARD, &player->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

static void Pendant_UpdateParryArc(Player* player, PlayState* play) {
    sPendantTimer++;

    // Force roll animation (OOT's action system tries to override)
    if (player->skelAnime.animation != &gPlayerAnim_link_normal_landing_roll) {
        LinkAnimation_Change(play, &player->skelAnime, &gPlayerAnim_link_normal_landing_roll, 3.0f, 0.0f,
                             Animation_GetLastFrame(&gPlayerAnim_link_normal_landing_roll), ANIMMODE_LOOP, -3.0f);
    }

    // --- Parabolic arc interpolation ---
    f32 t = (f32)sPendantTimer / (f32)PARRY_ARC_FRAMES;
    if (t > 1.0f)
        t = 1.0f;

    // XZ: linear interpolation from start to end
    player->actor.world.pos.x = sPendantArcStart.x + (sPendantArcEnd.x - sPendantArcStart.x) * t;
    player->actor.world.pos.z = sPendantArcStart.z + (sPendantArcEnd.z - sPendantArcStart.z) * t;

    // Y: linear base + parabolic arc offset (4*t*(1-t) peaks at 1.0 when t=0.5)
    f32 baseY = sPendantArcStart.y + (sPendantArcEnd.y - sPendantArcStart.y) * t;
    f32 arcOffset = PARRY_ARC_HEIGHT * 4.0f * t * (1.0f - t);
    player->actor.world.pos.y = baseY + arcOffset;

    // Zero velocity so OOT doesn't interfere
    player->linearVelocity = 0.0f;
    player->actor.velocity.x = 0.0f;
    player->actor.velocity.y = 0.0f;
    player->actor.velocity.z = 0.0f;
    player->actor.gravity = 0.0f;

    // Disable body collision
    player->cylinder.base.ocFlags1 &= ~OC1_ON;

    // Attack collider at ENEMY position — one hit only
    if (sPendantTarget != NULL && !sPendantParryHit) {
        sPendantAtkCol.dim.pos.x = sPendantTarget->world.pos.x;
        sPendantAtkCol.dim.pos.y = sPendantTarget->world.pos.y;
        sPendantAtkCol.dim.pos.z = sPendantTarget->world.pos.z;
        sPendantAtkCol.base.atFlags |= AT_ON;
        CollisionCheck_SetAT(play, &play->colChkCtx, &sPendantAtkCol.base);

        if (sPendantAtkCol.base.atFlags & AT_HIT) {
            sPendantParryHit = 1;
            sPendantAtkCol.base.atFlags &= ~(AT_HIT | AT_ON);

            // Kill sword quads too — prevent multi-hit from melee system
            player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
            player->meleeWeaponQuads[1].base.atFlags &= ~AT_ON;
            player->meleeWeaponState = 0;

            Audio_PlaySoundGeneral(NA_SE_IT_SWORD_STRIKE, &player->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
    }

    // Also suppress sword quads if already hit (melee system re-enables them each frame)
    if (sPendantParryHit) {
        player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
        player->meleeWeaponQuads[1].base.atFlags &= ~AT_ON;
        player->meleeWeaponState = 0;
    }

    // Face enemy throughout the arc
    if (sPendantTarget != NULL) {
        s16 yaw = Actor_WorldYawTowardActor(&player->actor, sPendantTarget);
        player->actor.shape.rot.y = yaw;
        player->yaw = yaw;
    }

    // Arc complete → landing
    if (sPendantTimer >= PARRY_ARC_FRAMES) {
        sPendantState = PENDANT_PARRY_LANDING;
        sPendantTimer = 0;
        player->actor.gravity = -1.2f;

        LinkAnimation_Change(play, &player->skelAnime, &gPlayerAnim_link_normal_landing, 1.5f, 0.0f,
                             Animation_GetLastFrame(&gPlayerAnim_link_normal_landing), ANIMMODE_ONCE, -3.0f);

        Audio_PlaySoundGeneral(NA_SE_PL_WALK_GROUND, &player->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }

    // Wall abort
    if (player->actor.bgCheckFlags & 0x08) {
        sPendantState = PENDANT_PARRY_LANDING;
        sPendantTimer = 0;
        player->actor.gravity = -1.2f;
    }
}

static void Pendant_UpdateParryLanding(Player* player, PlayState* play) {
    sPendantTimer++;

    if ((player->actor.bgCheckFlags & 1) || sPendantTimer > PARRY_LANDING_FRAMES) {
        sPendantState = PENDANT_IDLE;
        sPendantTarget = NULL;
        player->linearVelocity = 0.0f;
        player->actor.velocity.x = 0.0f;
        player->actor.velocity.y = 0.0f;
        player->actor.velocity.z = 0.0f;
        player->stateFlags3 &= ~PLAYER_STATE3_MIDAIR;
        player->cylinder.base.ocFlags1 |= OC1_ON;
    }
}

// ===================================================================
// Main dispatch
// ===================================================================
static void Pendant_Behavior(Player* player, PlayState* play) {
    switch (sPendantState) {
        case PENDANT_IDLE:
            if (Pendant_CheckMortalDraw(player, play)) {
                Pendant_StartMortalDraw(player, play);
            } else if (Pendant_CheckGroundPound(player, play)) {
                Pendant_StartGroundPound(player, play);
            } else if (Pendant_CheckParryLeap(player, play)) {
                Pendant_StartParryLeap(player, play);
            }
            break;

        case PENDANT_DRAW_SLASH:
            Pendant_UpdateMortalDraw(player, play);
            break;
        case PENDANT_DRAW_HITSTOP:
            Pendant_UpdateDrawHitstop(player, play);
            break;
        case PENDANT_DRAW_RECOVERY:
            Pendant_UpdateDrawRecovery(player, play);
            break;

        case PENDANT_GPOUND_STALL:
            Pendant_UpdateGPoundStall(player, play);
            break;
        case PENDANT_GPOUND_FALLING:
            Pendant_UpdateGPoundFalling(player, play);
            break;
        case PENDANT_GPOUND_LANDING:
            Pendant_UpdateGPoundLanding(player, play);
            break;

        case PENDANT_PARRY_ARC:
            Pendant_UpdateParryArc(player, play);
            break;
        case PENDANT_PARRY_LANDING:
            Pendant_UpdateParryLanding(player, play);
            break;
    }
}

static void Pendant_Reset(void) {
    sPendantState = PENDANT_IDLE;
    sPendantTimer = 0;
    sPendantSideHopCount = 0;
    sPendantHopTimer = 0;
    sPendantTarget = NULL;
    sPendantSpinReady = 0;
    sPendantWasHopping = 0;
}
