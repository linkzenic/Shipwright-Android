/**
 * equip_pegasus.c - Pegasus Anklet (Extended Boots Slot 1)
 *
 * Behavior: B-hold dash with sword extended, wind barrier, wall bonk.
 * - Intercepts spin attack charge: when B is held after a swing, Link dashes forward
 * - Sword extended via limb rotation
 * - Wind barrier (greenish) in front costs 1 MP per 15 frames
 * - Without magic: still runs and deals damage, no barrier
 * - Wall collision = bonk → return to idle
 *
 * Included by ext_equip_behavior.c (unity build).
 */

// No extra includes — unity-built from ext_equip_behavior.c
// which inherits all includes from extended_equipment.c

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define PEGASUS_WINDUP_FRAMES   10
#define PEGASUS_DASH_SPEED      18.0f
#define PEGASUS_BONK_FRAMES     20
#define PEGASUS_BONK_RECOIL     -6.0f
#define PEGASUS_MAGIC_INTERVAL  15   // Drain 1 MP every N frames
#define PEGASUS_COL_RADIUS      50
#define PEGASUS_COL_HEIGHT      80
#define PEGASUS_COL_FORWARD     40.0f // Collider offset in front of Link

// Stab pose: hardcoded upper body joint rotations matching BGS forward thrust.
// Values in s16 (0x4000 = 90 degrees). Only upper body is overridden;
// lower body keeps the running animation.

// ---------------------------------------------------------------------------
// Collider
// ---------------------------------------------------------------------------
static ColliderCylinder sPegasusCol;

static ColliderCylinderInit sPegasusColInit = {
    { COLTYPE_NONE, AT_ON | AT_TYPE_PLAYER, AC_NONE,
      OC1_NONE, OC2_NONE, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK2,
      { 0x00000100, 0x00, 0x04 }, // DMG_SLASH, 4 damage
      { 0, 0, 0 },
      TOUCH_ON | TOUCH_SFX_NORMAL,
      BUMP_NONE,
      OCELEM_NONE },
    { PEGASUS_COL_RADIUS, PEGASUS_COL_HEIGHT, 0, { 0, 0, 0 } }
};

static void Pegasus_InitCollider(PlayState* play, Player* p) {
    if (gExtEquipBehavior.pegasusColInit)
        return;
    Collider_InitCylinder(play, &sPegasusCol);
    Collider_SetCylinder(play, &sPegasusCol, &p->actor, &sPegasusColInit);
    gExtEquipBehavior.pegasusColInit = 1;
}

static void Pegasus_UpdateCollider(PlayState* play, Player* p) {
    f32 sinY = Math_SinS(p->actor.shape.rot.y);
    f32 cosY = Math_CosS(p->actor.shape.rot.y);

    sPegasusCol.dim.pos.x = (s16)(p->actor.world.pos.x + sinY * PEGASUS_COL_FORWARD);
    sPegasusCol.dim.pos.y = (s16)(p->actor.world.pos.y);
    sPegasusCol.dim.pos.z = (s16)(p->actor.world.pos.z + cosY * PEGASUS_COL_FORWARD);

    sPegasusCol.base.atFlags |= AT_ON | AT_TYPE_PLAYER;
    CollisionCheck_SetAT(play, &play->colChkCtx, &sPegasusCol.base);

    // Check and clear hit
    if (sPegasusCol.base.atFlags & AT_HIT) {
        Audio_PlaySoundGeneral(NA_SE_IT_SWORD_STRIKE, &p->actor.world.pos, 4,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultReverb);
        sPegasusCol.base.atFlags &= ~AT_HIT;
    }
}

// ---------------------------------------------------------------------------
// Stop / Cleanup
// ---------------------------------------------------------------------------
static void Pegasus_Stop(Player* p) {
    gExtEquipBehavior.pegasusState = PEGASUS_IDLE;
    gExtEquipBehavior.pegasusTimer = 0;
    gExtEquipBehavior.pegasusMagicTick = 0;
    // (stab pose is per-frame, no state to reset)
    p->stateFlags1 &= ~PLAYER_STATE1_CHARGING_SPIN_ATTACK;
    p->stateFlags2 &= ~PLAYER_STATE2_DISABLE_ROTATION_Z_TARGET;
    p->actor.gravity = -1.2f;
    sPegasusCol.base.atFlags &= ~AT_ON;
}

// ---------------------------------------------------------------------------
// Apply sword-forward limb pose
// ---------------------------------------------------------------------------
static void Pegasus_ApplyPose(Player* p, PlayState* play) {
    // Load BGS stab frame 2 into upperJointTable (async — ready next frame)
    AnimationContext_SetLoadFrame(play, &gPlayerAnim_link_fighter_Lpierce_kiru, 2,
                                  p->skelAnime.limbCount, p->upperJointTable);

    // Copy upper body joints from upperJointTable (loaded previous frame) into jointTable
    // Lower body (ROOT=1, WAIST=2, LOWER=3, thighs=4/7, shins=5/8, feet=6/9) keeps run_free
    for (s32 i = PLAYER_LIMB_UPPER; i < PLAYER_LIMB_MAX; i++) {
        p->skelAnime.jointTable[i] = p->upperJointTable[i];
    }
}

// ---------------------------------------------------------------------------
// State: Idle - monitor for B-hold to start dash
// ---------------------------------------------------------------------------
static void Pegasus_StateIdle(Player* p, PlayState* play) {
    // Detect: B button held + on ground + has a sword (kokiri, master, etc.)
    u8 bHeld = CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_B);

    if (!bHeld)
        return;

    // Only activate when the charge attack is about to start (unk_858 building up)
    // or the player just finished a swing and is holding B
    if (!(p->actor.bgCheckFlags & 0x0001)) // Must be on ground
        return;
    if (p->stateFlags1 & PLAYER_STATE1_IN_WATER) // Not in water
        return;

    if (p->heldItemAction < PLAYER_IA_SWORD_MASTER || p->heldItemAction > PLAYER_IA_SWORD_BIGGORON) {
        // Check for Byrna (Kokiri IA) too
        if (p->heldItemAction != PLAYER_IA_SWORD_KOKIRI)
            return;
    }

    // Check if the charge is building (vanilla spin attack charge)
    if (p->unk_858 >= 0.1f || (p->stateFlags1 & PLAYER_STATE1_CHARGING_SPIN_ATTACK)) {
        // Intercept! Reset spin attack charge and enter windup
        p->unk_858 = 0.0f;
        gExtEquipBehavior.pegasusState = PEGASUS_WINDUP;
        gExtEquipBehavior.pegasusTimer = PEGASUS_WINDUP_FRAMES;
        p->stateFlags1 |= PLAYER_STATE1_CHARGING_SPIN_ATTACK;
        p->stateFlags2 |= PLAYER_STATE2_DISABLE_ROTATION_Z_TARGET;

        // Stab pose applied per-frame in Pegasus_StateRunning

        Audio_PlaySoundGeneral(NA_SE_PL_WALK_GROUND, &p->actor.world.pos, 4,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultReverb);
    }
}

// ---------------------------------------------------------------------------
// State: Windup - brief charge-up before dash
// ---------------------------------------------------------------------------
static void Pegasus_StateWindup(Player* p, PlayState* play) {
    u8 bHeld = CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_B);

    // Cancel if B released
    if (!bHeld) {
        Pegasus_Stop(p);
        return;
    }

    // Keep spin attack charge suppressed
    p->unk_858 = 0.0f;
    p->actor.speedXZ = 0.0f;
    p->linearVelocity = 0.0f;

    gExtEquipBehavior.pegasusTimer--;
    if (gExtEquipBehavior.pegasusTimer <= 0) {
        gExtEquipBehavior.pegasusState = PEGASUS_RUNNING;
        gExtEquipBehavior.pegasusMagicTick = 0;

        Pegasus_InitCollider(play, p);

        Audio_PlaySoundGeneral(NA_SE_IT_SWORD_SWING_HARD, &p->actor.world.pos, 4,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultReverb);
    }
}

// ---------------------------------------------------------------------------
// State: Running - full speed dash with sword forward
// ---------------------------------------------------------------------------
static void Pegasus_StateRunning(Player* p, PlayState* play) {
    u8 bHeld = CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_B);

    // Stop if B released or in water
    if (!bHeld || (p->stateFlags1 & PLAYER_STATE1_IN_WATER)) {
        Pegasus_Stop(p);
        return;
    }

    // Keep spin attack suppressed
    p->unk_858 = 0.0f;
    p->stateFlags1 |= PLAYER_STATE1_CHARGING_SPIN_ATTACK;

    // Steering: use stick X for turning (negate: stick right = positive = turn right)
    f32 stickX = play->state.input[0].rel.stick_x;
    if (fabsf(stickX) > 10.0f) {
        p->actor.shape.rot.y -= (s16)(stickX * 5.0f);
    }
    p->actor.world.rot.y = p->actor.shape.rot.y;
    p->yaw = p->actor.shape.rot.y;

    // Use engine velocity so wall collision detection works (bgCheckFlags 0x200)
    p->linearVelocity = PEGASUS_DASH_SPEED;
    p->actor.speedXZ = PEGASUS_DASH_SPEED;

    // Force running animation on lower body (legs keep moving)
    // The skeleton plays this animation for ALL limbs, then ApplyPose
    // overrides only the upper body limbs — legs stay running
    if (p->skelAnime.animation != &gPlayerAnim_link_normal_run_free) {
        LinkAnimation_Change(play, &p->skelAnime, &gPlayerAnim_link_normal_run_free,
                             1.5f, 0.0f, Animation_GetLastFrame(&gPlayerAnim_link_normal_run_free),
                             ANIMMODE_LOOP, -6.0f);
    }

    // Apply stab pose on upper body (lower body keeps running anim)
    Pegasus_ApplyPose(p, play);

    // Collider + magic drain: only when has magic
    if (gSaveContext.magic > 0) {
        Pegasus_UpdateCollider(play, p);

        gExtEquipBehavior.pegasusMagicTick++;
        if (gExtEquipBehavior.pegasusMagicTick >= PEGASUS_MAGIC_INTERVAL) {
            gExtEquipBehavior.pegasusMagicTick = 0;
            gSaveContext.magic--;
            if (gSaveContext.magic < 0)
                gSaveContext.magic = 0;
        }
    } else {
        // No magic: disable collider
        sPegasusCol.base.atFlags &= ~AT_ON;
        gExtEquipBehavior.pegasusMagicTick = 0;
    }

    // Loop running sound
    func_8002F974(&p->actor, NA_SE_PL_WALK_GROUND - SFX_FLAG);

    // Wall bonk check — replicate roll bonk behavior from z_player.c:10059
    // Check bgCheckFlags 0x200 (PLAYER_WALL_INTERACT) which the engine sets
    // when linearVelocity-based movement hits a wall
    {
        Actor* ocCollidedActor = NULL;
        u8 doBonk = 0;

        // Wall collision (same flag roll uses)
        if (p->actor.bgCheckFlags & 0x200) {
            // Check angle to wall — must be roughly facing it
            s16 yawDiff = p->yaw - (s16)(p->actor.wallYaw + 0x8000);
            if (ABS(yawDiff) < 0x2000) {
                doBonk = 1;

                // Signal breakable crates (OBJ_KIBAKO2)
                if (p->actor.wallBgId != BGCHECK_SCENE) {
                    DynaPolyActor* wallPolyActor = DynaPoly_GetActor(&play->colCtx, p->actor.wallBgId);
                    if (wallPolyActor != NULL) {
                        wallPolyActor->actor.home.rot.z = 1;
                    }
                }
            }
        }

        // OC collision with trees (EN_WOOD02) — same as roll
        if (p->cylinder.base.ocFlags1 & OC1_HIT) {
            ocCollidedActor = p->cylinder.base.oc;
            if (ocCollidedActor != NULL && ocCollidedActor->id == ACTOR_EN_WOOD02) {
                if (ABS((s16)(p->actor.world.rot.y - ocCollidedActor->yawTowardsPlayer)) > 0x6000) {
                    ocCollidedActor->home.rot.y = 1; // Signal tree to drop
                    doBonk = 1;
                }
            }
        }

        if (doBonk) {
            // Bonk: play hip_down animation, reverse velocity, quake, sounds
            // (exact roll bonk from z_player.c:10079-10088)
            LinkAnimation_Change(play, &p->skelAnime, &gPlayerAnim_link_normal_hip_down_free,
                                 1.0f, 0.0f, Animation_GetLastFrame(&gPlayerAnim_link_normal_hip_down_free),
                                 ANIMMODE_ONCE, -6.0f);

            p->linearVelocity = -p->linearVelocity;
            p->stateFlags1 &= ~PLAYER_STATE1_CHARGING_SPIN_ATTACK;

            // Quake + rumble
            Quake_Add(Play_GetCamera(play, 0), 3);
            func_800AA000(255.0f, 20, 150, 0);

            // Bonk sounds
            Audio_PlaySoundGeneral(NA_SE_PL_BODY_HIT, &p->actor.world.pos, 4,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultReverb);
            Audio_PlaySoundGeneral(NA_SE_VO_LI_CLIMB_END, &p->actor.world.pos, 4,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultReverb);

            gExtEquipBehavior.pegasusState = PEGASUS_BONK;
            gExtEquipBehavior.pegasusTimer = 0; // Timer not used — wait for anim to finish
        }
    }
}

// ---------------------------------------------------------------------------
// State: Bonk - wall hit recovery
// ---------------------------------------------------------------------------
static void Pegasus_StateBonk(Player* p, PlayState* play) {
    // Lock input during bonk
    p->stateFlags1 &= ~PLAYER_STATE1_CHARGING_SPIN_ATTACK;

    // Decelerate (reverse velocity from bonk)
    Math_StepToF(&p->linearVelocity, 0.0f, 2.0f);
    p->actor.speedXZ = fabsf(p->linearVelocity);

    // Wait for hip_down animation to finish
    if (LinkAnimation_Update(play, &p->skelAnime)) {
        Pegasus_Stop(p);
    }
}

// Wind spell texture (from z_magic_wind.inc.c)
extern char sWindEffTexture[];

// ---------------------------------------------------------------------------
// Cone barrier vertices: tip in front (Y=0), base behind Link (Y=8000)
// 8 segments around the cone base, 1 tip vertex = 9 verts
// ---------------------------------------------------------------------------
static Vtx sPegasusConeFrontVtx[] = {
    // 0: Tip (front, converges to a point)
    VTX(0,     0,    0,  512, 2048, 0xFF, 0xFF, 0xFF, 0xFF),
    // 1-8: Base ring (behind Link, radius 4000)
    VTX( 4000, 8000,    0,    0,    0, 0xFF, 0xFF, 0xFF, 0x00),
    VTX( 2828, 8000, 2828,  256,    0, 0xFF, 0xFF, 0xFF, 0x00),
    VTX(    0, 8000, 4000,  512,    0, 0xFF, 0xFF, 0xFF, 0x00),
    VTX(-2828, 8000, 2828,  768,    0, 0xFF, 0xFF, 0xFF, 0x00),
    VTX(-4000, 8000,    0, 1024,    0, 0xFF, 0xFF, 0xFF, 0x00),
    VTX(-2828, 8000,-2828, 1280,    0, 0xFF, 0xFF, 0xFF, 0x00),
    VTX(    0, 8000,-4000, 1536,    0, 0xFF, 0xFF, 0xFF, 0x00),
    VTX( 2828, 8000,-2828, 1792,    0, 0xFF, 0xFF, 0xFF, 0x00),
};

static Gfx gfx_pegasus_cone[] = {
    gsDPPipeSync(),
    gsDPSetTextureLUT(G_TT_NONE),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
    gsDPLoadTextureBlock(sWindEffTexture, G_IM_FMT_I, G_IM_SIZ_8b, 64, 64, 0,
                         G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP,
                         6, 6, G_TX_NOLOD, G_TX_NOLOD),
    gsDPLoadMultiBlock(sWindEffTexture, 0x0100, 1, G_IM_FMT_I, G_IM_SIZ_8b, 64, 64, 0,
                       G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP,
                       6, 6, 14, 14),
    gsDPSetCombineLERP(TEXEL1, PRIMITIVE, PRIM_LOD_FRAC, TEXEL0, TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0,
                       PRIMITIVE, ENVIRONMENT, COMBINED, ENVIRONMENT, COMBINED, 0, SHADE, 0),
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_XLU_SURF2),
    gsSPClearGeometryMode(G_CULL_BACK | G_FOG | G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR),
    gsDPSetPrimColor(0, 0x80, 255, 255, 170, 255),
    gsDPSetEnvColor(100, 255, 50, 0),
    // Segment 0x08: animated tex scroll (set per-frame before drawing)
    gsSPDisplayList(0x08000001),
    gsSPVertex(sPegasusConeFrontVtx, 9, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsSP2Triangles(0, 3, 4, 0, 0, 4, 5, 0),
    gsSP2Triangles(0, 5, 6, 0, 0, 6, 7, 0),
    gsSP2Triangles(0, 7, 8, 0, 0, 8, 1, 0),
    gsSPEndDisplayList(),
};

// ---------------------------------------------------------------------------
// Draw: Wind cone barrier (called from ExtEquip_DrawDispatch)
// Cone tip in front of Link, base opens behind covering him
// ---------------------------------------------------------------------------
static void Pegasus_Draw(Player* p, PlayState* play) {
    if (gExtEquipBehavior.pegasusState != PEGASUS_RUNNING)
        return;

    if (gSaveContext.magic <= 0)
        return;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);

    Matrix_Push();
    // Position at Link's chest height, cone tip far in front
    f32 sinY = Math_SinS(p->actor.shape.rot.y);
    f32 cosY = Math_CosS(p->actor.shape.rot.y);
    Matrix_Translate(p->actor.world.pos.x + sinY * 80.0f,
                     p->actor.world.pos.y + 40.0f,
                     p->actor.world.pos.z + cosY * 80.0f, MTXMODE_NEW);
    Matrix_RotateY(BINANG_TO_RAD(p->actor.shape.rot.y), MTXMODE_APPLY);
    // Tip points forward
    Matrix_RotateX(BINANG_TO_RAD((s16)-0x4000), MTXMODE_APPLY);

    f32 scale = 0.015f;
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);

    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    // Animated texture scroll (same as wind spell)
    u32 frames = play->gameplayFrames;
    gSPSegment(POLY_XLU_DISP++, 0x08,
               Gfx_TwoTexScroll(play->state.gfxCtx, 0,
                                -(s32)(frames * 1), (s32)(frames * 20), 0x40, 0x40,
                                1, -(s32)(frames * 2), (s32)(frames * 10), 0x40, 0x40));

    gSPDisplayList(POLY_XLU_DISP++, gfx_pegasus_cone);

    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx);
}

// ---------------------------------------------------------------------------
// Anklet pendulum physics (wing charm swings with movement)
// ---------------------------------------------------------------------------
#define PEGASUS_PENDULUM_GRAVITY  0.015f
#define PEGASUS_PENDULUM_DAMPING  0.92f
#define PEGASUS_PENDULUM_ACCEL    0.003f
#define PEGASUS_PENDULUM_MAX      0.6f   // Max swing angle (radians, ~34 degrees)

static void Pegasus_UpdateWingPhysics(Player* player) {
    f32 accel = player->actor.speedXZ * PEGASUS_PENDULUM_ACCEL;

    // Pendulum: gravity restores to center, movement pushes it
    gExtEquipBehavior.pegasusWingVel += (-sinf(gExtEquipBehavior.pegasusWingAngle) * PEGASUS_PENDULUM_GRAVITY) + accel;
    gExtEquipBehavior.pegasusWingVel *= PEGASUS_PENDULUM_DAMPING;

    gExtEquipBehavior.pegasusWingAngle += gExtEquipBehavior.pegasusWingVel;

    // Clamp
    if (gExtEquipBehavior.pegasusWingAngle > PEGASUS_PENDULUM_MAX)
        gExtEquipBehavior.pegasusWingAngle = PEGASUS_PENDULUM_MAX;
    if (gExtEquipBehavior.pegasusWingAngle < -PEGASUS_PENDULUM_MAX)
        gExtEquipBehavior.pegasusWingAngle = -PEGASUS_PENDULUM_MAX;
}

// ---------------------------------------------------------------------------
// Anklet torus DL (golden ring)
// ---------------------------------------------------------------------------
static Vtx sAnkletTorusVtx[] = {
    // 16-segment torus: outer ring (radius 300, tube radius 40)
    // Top ring (y=40)
    VTX(300,  40,   0, 0, 0, 0, 127, 0, 255),
    VTX(212,  40, 212, 0, 0, 0, 127, 0, 255),
    VTX(  0,  40, 300, 0, 0, 0, 127, 0, 255),
    VTX(-212, 40, 212, 0, 0, 0, 127, 0, 255),
    VTX(-300, 40,   0, 0, 0, 0, 127, 0, 255),
    VTX(-212, 40,-212, 0, 0, 0, 127, 0, 255),
    VTX(  0,  40,-300, 0, 0, 0, 127, 0, 255),
    VTX(212,  40,-212, 0, 0, 0, 127, 0, 255),
    // Bottom ring (y=-40)
    VTX(300, -40,   0, 0, 0, 0,-127, 0, 255),
    VTX(212, -40, 212, 0, 0, 0,-127, 0, 255),
    VTX(  0, -40, 300, 0, 0, 0,-127, 0, 255),
    VTX(-212,-40, 212, 0, 0, 0,-127, 0, 255),
    VTX(-300,-40,   0, 0, 0, 0,-127, 0, 255),
    VTX(-212,-40,-212, 0, 0, 0,-127, 0, 255),
    VTX(  0, -40,-300, 0, 0, 0,-127, 0, 255),
    VTX(212, -40,-212, 0, 0, 0,-127, 0, 255),
};

static Gfx gfx_anklet_torus[] = {
    gsDPPipeSync(),
    gsDPSetCycleType(G_CYC_1CYCLE),
    gsDPSetRenderMode(G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2),
    gsDPSetCombineMode(G_CC_PRIMITIVE, G_CC_PRIMITIVE),
    gsSPLoadGeometryMode(G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH),
    gsDPSetPrimColor(0, 0, 220, 180, 50, 255), // Gold
    gsSPVertex(sAnkletTorusVtx, 16, 0),
    gsSP2Triangles(0, 1, 9, 0, 0, 9, 8, 0),
    gsSP2Triangles(1, 2, 10, 0, 1, 10, 9, 0),
    gsSP2Triangles(2, 3, 11, 0, 2, 11, 10, 0),
    gsSP2Triangles(3, 4, 12, 0, 3, 12, 11, 0),
    gsSP2Triangles(4, 5, 13, 0, 4, 13, 12, 0),
    gsSP2Triangles(5, 6, 14, 0, 5, 14, 13, 0),
    gsSP2Triangles(6, 7, 15, 0, 6, 15, 14, 0),
    gsSP2Triangles(7, 0, 8, 0, 7, 8, 15, 0),
    gsSPEndDisplayList(),
};

// ---------------------------------------------------------------------------
// Wing DL (golden stylized wing shape - 4 triangles)
// ---------------------------------------------------------------------------
static Vtx sAnkletWingVtx[] = {
    // Wing shape: base at origin, extends outward (+Z) and upward (+Y)
    VTX(  0,    0,   0, 0, 0,  0,  0, 127, 255), // 0: base center
    VTX(  0,  100, 150, 0, 0,  0,  0, 127, 255), // 1: upper tip
    VTX(  0,   50, 300, 0, 0,  0,  0, 127, 255), // 2: far tip
    VTX(  0,  -30, 200, 0, 0,  0,  0, 127, 255), // 3: lower mid
    VTX(  0,  -50,  80, 0, 0,  0,  0, 127, 255), // 4: lower base
    VTX( 20,   30, 100, 0, 0,  0,  0, 127, 255), // 5: thickness (front)
};

static Gfx gfx_anklet_wing[] = {
    gsDPPipeSync(),
    gsDPSetCycleType(G_CYC_1CYCLE),
    gsDPSetRenderMode(G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2),
    gsDPSetCombineMode(G_CC_PRIMITIVE, G_CC_PRIMITIVE),
    gsSPLoadGeometryMode(G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH),
    gsDPSetPrimColor(0, 0, 255, 220, 100, 200), // Golden translucent
    gsSPVertex(sAnkletWingVtx, 6, 0),
    gsSP2Triangles(0, 1, 5, 0, 1, 2, 5, 0),  // Upper wing
    gsSP2Triangles(0, 5, 4, 0, 5, 2, 3, 0),  // Lower wing
    gsSPEndDisplayList(),
};

// ---------------------------------------------------------------------------
// Draw anklet on foot limb (called from PostLimbDraw in z_player_lib.c)
// ---------------------------------------------------------------------------
static void Pegasus_DrawAnklet(PlayState* play, s32 isRightFoot) {
    OPEN_DISPS(play->state.gfxCtx);

    // === Golden ring at ankle ===
    // Use Golden Goddess Light Ring DL from gameplay
    // Mirror Z offset for left foot to center the ring
    f32 zOffset = isRightFoot ? 0.0f : -50.0f;
    Matrix_Push();
    Matrix_Translate(0.0f, -300.0f, zOffset, MTXMODE_APPLY);
    Matrix_RotateX(M_PI / 2.0f, MTXMODE_APPLY); // Lay flat around the ankle
    Matrix_Scale(0.015f, 0.015f, 0.015f, MTXMODE_APPLY);

    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, ResourceMgr_LoadGfxByName("__OTR__objects/object_light_ring/gGoldenGoddessLightRingDL"));
    Matrix_Pop();

    // === Golden wings (inline geometry, pendulum physics) ===
    // Position wings on the outer side of the shin
    f32 sideOffset = isRightFoot ? -200.0f : 200.0f;

    Matrix_Push();
    Matrix_Translate(sideOffset, -300.0f, 0.0f, MTXMODE_APPLY);

    // Apply pendulum swing
    Matrix_RotateZ(gExtEquipBehavior.pegasusWingAngle, MTXMODE_APPLY);

    // Wing scale
    Matrix_Scale(1.0f, 1.0f, 1.0f, MTXMODE_APPLY);

    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, gfx_anklet_wing);

    // Mirror wing on other side
    Matrix_Scale(1.0f, 1.0f, -1.0f, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, gfx_anklet_wing);

    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx);
}

// ---------------------------------------------------------------------------
// Main Behavior Entry
// ---------------------------------------------------------------------------
static void Pegasus_Behavior(Player* player, PlayState* play) {
    // Skip during cutscenes, etc.
    if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE |
                                PLAYER_STATE1_LOADING | PLAYER_STATE1_IN_ITEM_CS |
                                PLAYER_STATE1_GETTING_ITEM)) {
        if (gExtEquipBehavior.pegasusState != PEGASUS_IDLE)
            Pegasus_Stop(player);
        return;
    }

    // Always update wing pendulum physics (even when not dashing)
    Pegasus_UpdateWingPhysics(player);

    switch (gExtEquipBehavior.pegasusState) {
        case PEGASUS_IDLE:
            Pegasus_StateIdle(player, play);
            break;
        case PEGASUS_WINDUP:
            Pegasus_StateWindup(player, play);
            break;
        case PEGASUS_RUNNING:
            Pegasus_StateRunning(player, play);
            break;
        case PEGASUS_BONK:
            Pegasus_StateBonk(player, play);
            break;
        default:
            gExtEquipBehavior.pegasusState = PEGASUS_IDLE;
            break;
    }
}
