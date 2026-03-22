/**
 * equip_dragonscale.c - Water Dragon Scale (Extended Boots Slot 3)
 *
 * Activates REAL Zora swim mechanics (1:1) for Adult Link via public wrappers
 * in mm_player_form.cpp. Calls the same swim actions (idle, surface walk,
 * fast swim, dolphin jump) + barrier, buoyancy, speed ramp, barrel roll.
 * Link keeps his OOT model — formSkelAnime joints are synced to player->skelAnime.
 *
 * NO boot toggle (uses Iron Boots for sinking instead).
 * NO Zora tunic effect, NO stop timer.
 * Adult Link only.
 *
 * Included by ext_equip_behavior.c (unity build).
 */

// ---------------------------------------------------------------------------
// Blue sapphire scale color DL (replaces silver/golden color DLs)
// ---------------------------------------------------------------------------
static Gfx gfx_dragon_scale_color[] = {
    gsDPPipeSync(),
    gsDPSetPrimColor(0, 0x80, 30, 80, 200, 180), // Blue sapphire frame (semi-transparent)
    gsDPSetEnvColor(10, 40, 120, 0),             // Dark blue env
    gsSPEndDisplayList(),
};

static Gfx gfx_dragon_scale_water_color[] = {
    gsDPPipeSync(),
    gsDPSetPrimColor(0, 0x80, 60, 140, 255, 255), // Bright sapphire blue interior
    gsDPSetEnvColor(20, 80, 200, 0),              // Blue env
    gsSPEndDisplayList(),
};

// ---------------------------------------------------------------------------
// Draw scale pendant at waist (called from PostLimbDraw PLAYER_LIMB_WAIST)
// ---------------------------------------------------------------------------
static void DScale_DrawWaistScale(PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);

    Matrix_Push();
    // Offset: forward from belt, slightly down, centered
    Matrix_Translate(0.0f, -200.0f, -500.0f, MTXMODE_APPLY);
    Matrix_Scale(0.5f, 0.5f, 0.5f, MTXMODE_APPLY);

    // Animated water texture scroll (same as GetItem_DrawScale)
    u32 frames = play->gameplayFrames;
    gSPSegment(POLY_XLU_DISP++, 0x08,
               Gfx_TwoTexScroll(play->state.gfxCtx, 0, (s32)(frames * 2), -(s32)(frames * 2), 64, 64, 1,
                                (s32)(frames * 4), -(s32)(frames * 4), 32, 32));

    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    // Draw: blue frame color → scale geometry → blue water color → water geometry
    gSPDisplayList(POLY_XLU_DISP++, gfx_dragon_scale_color);
    gSPDisplayList(POLY_XLU_DISP++, ResourceMgr_LoadGfxByName("__OTR__objects/object_gi_scale/gGiScaleDL"));
    gSPDisplayList(POLY_XLU_DISP++, gfx_dragon_scale_water_color);
    gSPDisplayList(POLY_XLU_DISP++, ResourceMgr_LoadGfxByName("__OTR__objects/object_gi_scale/gGiScaleWaterDL"));

    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx);
}

// ---------------------------------------------------------------------------
// Blue sparkles when entering water with Dragon Scale
// ---------------------------------------------------------------------------
static s16 sDScaleSparkleTimer = 0;

static void DScale_TriggerSparkles(void) {
    sDScaleSparkleTimer = 30;
}

static void DScale_Draw(Player* p, PlayState* play) {
    if (sDScaleSparkleTimer <= 0)
        return;

    sDScaleSparkleTimer--;

    // Spawn blue sparkles around Link
    Color_RGBA8 primColor = { 100, 180, 255, 255 };
    Color_RGBA8 envColor = { 30, 80, 200, 255 };
    Vec3f accel = { 0.0f, 0.0f, 0.0f };

    for (u8 i = 0; i < 2; i++) {
        Vec3f pos;
        pos.x = p->actor.world.pos.x + Rand_CenteredFloat(30.0f);
        pos.y = p->actor.world.pos.y + 20.0f + Rand_CenteredFloat(20.0f);
        pos.z = p->actor.world.pos.z + Rand_CenteredFloat(30.0f);

        Vec3f vel;
        vel.x = Rand_CenteredFloat(1.0f);
        vel.y = Rand_ZeroFloat(1.5f);
        vel.z = Rand_CenteredFloat(1.0f);

        EffectSsKiraKira_SpawnFocused(play, &pos, &vel, &accel, &primColor, &envColor, 400, 15);
    }
}

// ---------------------------------------------------------------------------
// Main Behavior Entry
// ---------------------------------------------------------------------------
static void DragonScale_Behavior(Player* player, PlayState* play) {
    // If a real transformation mask is active, don't interfere
    if (TransformMasks_IsTransformed()) {
        if (TransformMasks_IsZoraSwimEnabled())
            TransformMasks_DragonScaleExitSwim(player);
        return;
    }

    // Skip during cutscenes, death, etc.
    if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_LOADING |
                               PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_GETTING_ITEM)) {
        if (TransformMasks_IsZoraSwimEnabled())
            TransformMasks_DragonScaleExitSwim(player);
        return;
    }

    // Don't override during climbing/ledge grab
    if (player->stateFlags1 &
        (PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LEDGE | PLAYER_STATE1_CLIMBING_LADDER)) {
        if (TransformMasks_IsZoraSwimEnabled())
            TransformMasks_DragonScaleExitSwim(player);
        return;
    }

    u8 inWater = (player->stateFlags1 & PLAYER_STATE1_IN_WATER) != 0;

    if (inWater && player->actor.yDistToWater > 30.0f) {
        if (!TransformMasks_IsZoraSwimEnabled()) {
            // First frame in water: enter Zora swim (loads anims from mm.o2r)
            if (!TransformMasks_DragonScaleEnterSwim(play, player)) {
                return; // mm.o2r not available
            }
            DScale_TriggerSparkles();
        }
        // Run real Zora swim logic (same actions as Zora form)
        TransformMasks_DragonScaleSwimUpdate(play, player);
    } else {
        if (TransformMasks_IsZoraSwimEnabled()) {
            TransformMasks_DragonScaleExitSwim(player);
        }
    }
}
