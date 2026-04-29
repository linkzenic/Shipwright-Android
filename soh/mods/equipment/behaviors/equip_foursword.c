/**
 * equip_foursword.c - Four Sword (Extended Sword Slot 2)
 *
 * Charge mechanic:
 *   While shielding (R), hold B for 15 frames → Link raises sword (charge pose).
 *   All 3 clones spawn simultaneously in a triangle formation around Link.
 *   Each clone costs 12 MP (total 36 MP for 3 clones).
 *
 * Clones:
 *   - Follow Link every frame (fixed XZ offset from player set at spawn).
 *   - Mirror full pose: lower body from skelAnime.jointTable + upper body from
 *     upperJointTable, with upperLimbRot matrix correction → sword swings visible.
 *   - AC cylinder (enemy hits clone → clone dies with sparkle).
 *   - AT cylinder active during player sword swing → deal damage to enemies.
 *   - Item use mirrored via actor scan + megabonk velocity copy.
 *
 * Included by ext_equip_behavior.c (unity build).
 */

#define FOURSWORD_PAK_PATH "custom_items_resources/Equip_Four_Sword.pak"

#define FS_CHARGE_HOLD 15 // frames R+B held to arm charge
#define FS_CLONE_MAX 3
#define FS_CLONE_MP_COST 12       // MP per clone (36 total)
#define FS_FORMATION_RADIUS 80.0f // equilateral triangle radius (units)

#define FS_AC_RADIUS 18
#define FS_AC_HEIGHT 46

#define FS_AT_RADIUS 25
#define FS_AT_HEIGHT 60
#define FS_AT_Y_OFFSET 30 // cylinder centre above clone base

// ─── Collider definitions ─────────────────────────────────────────────────────

static ColliderCylinder sCloneAC[FS_CLONE_MAX];
static const ColliderCylinderInit sCloneACInit = {
    { COLTYPE_NONE, AT_NONE, AC_ON | AC_NO_DAMAGE | AC_TYPE_ENEMY, OC1_NONE, OC2_NONE, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK0, { 0x00000000, 0x00, 0x00 }, { 0xFFFFFFFF, 0x00, 0x00 }, TOUCH_NONE, BUMP_ON, OCELEM_NONE },
    { FS_AC_RADIUS, FS_AC_HEIGHT, 0, { 0, 0, 0 } },
};

static ColliderCylinder sCloneAT[FS_CLONE_MAX];
static const ColliderCylinderInit sCloneATInit = {
    { COLTYPE_NONE, AT_ON | AT_TYPE_PLAYER, AC_NONE, OC1_NONE, OC2_NONE, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK2,
      { 0x00000100, 0x00, 0x01 }, // same as player sword quad: dmgFlags, effect, damage=1
      { 0xFFCFFFFF, 0x00, 0x00 },
      TOUCH_ON | TOUCH_SFX_NORMAL,
      BUMP_NONE,
      OCELEM_NONE },
    { FS_AT_RADIUS, FS_AT_HEIGHT, 0, { 0, 0, 0 } },
};

// ─── Position helper ──────────────────────────────────────────────────────────

static Vec3f FourSword_GetClonePos(Player* player, int i) {
    Vec3f out;
    out.x = player->actor.world.pos.x + gExtEquipBehavior.fourSwordClones[i].offset.x;
    out.y = player->actor.world.pos.y + gExtEquipBehavior.fourSwordClones[i].offset.y;
    out.z = player->actor.world.pos.z + gExtEquipBehavior.fourSwordClones[i].offset.z;
    return out;
}

// ─── Formation helper ─────────────────────────────────────────────────────────

static Vec3f FourSword_FormationOffset(s16 yaw, int i) {
    static const f32 kAngles[3] = {
        0.0f,
        (2.0f * (f32)M_PI / 3.0f),
        (4.0f * (f32)M_PI / 3.0f),
    };
    f32 base = (f32)yaw * ((f32)M_PI / 32768.0f);
    f32 ang = base + kAngles[i];
    Vec3f out = { sinf(ang) * FS_FORMATION_RADIUS, 0.0f, cosf(ang) * FS_FORMATION_RADIUS };
    return out;
}

// ─── Spawn ────────────────────────────────────────────────────────────────────

static void FourSword_SpawnClone(PlayState* play, Player* player, Vec3f worldSpawnPos) {
    int i = gExtEquipBehavior.fourSwordCloneCount;
    if (i >= FS_CLONE_MAX)
        return;
    if (gSaveContext.magic < FS_CLONE_MP_COST)
        return;

    gExtEquipBehavior.fourSwordClones[i].offset.x = worldSpawnPos.x - player->actor.world.pos.x;
    gExtEquipBehavior.fourSwordClones[i].offset.y = 0.0f;
    gExtEquipBehavior.fourSwordClones[i].offset.z = worldSpawnPos.z - player->actor.world.pos.z;
    gExtEquipBehavior.fourSwordClones[i].alive = 1;
    gExtEquipBehavior.fourSwordCloneCount++;

    gSaveContext.magic -= FS_CLONE_MP_COST;
    if (gSaveContext.magic < 0)
        gSaveContext.magic = 0;

    Vec3f vel = { 0.0f, 1.5f, 0.0f };
    Vec3f accel = { 0.0f, 0.0f, 0.0f };
    for (int p = 0; p < 5; p++) {
        Vec3f sparkPos = {
            worldSpawnPos.x + Rand_CenteredFloat(20.0f),
            worldSpawnPos.y + 30.0f + Rand_ZeroFloat(30.0f),
            worldSpawnPos.z + Rand_CenteredFloat(20.0f),
        };
        vel.x = Rand_CenteredFloat(2.0f);
        vel.z = Rand_CenteredFloat(2.0f);
        EffectSsKiraKira_SpawnSmall(play, &sparkPos, &vel, &accel, &(Color_RGBA8){ 120, 200, 255, 255 },
                                    &(Color_RGBA8){ 50, 100, 255, 0 });
    }
    Sfx_PlaySfxCentered(NA_SE_SY_LOCK_ON);
}

// ─── Kill ─────────────────────────────────────────────────────────────────────

static void FourSword_KillClone(Player* player, PlayState* play, int i) {
    Vec3f pos = FourSword_GetClonePos(player, i);
    gExtEquipBehavior.fourSwordClones[i].alive = 0;
    if (gExtEquipBehavior.fourSwordCloneCount > 0)
        gExtEquipBehavior.fourSwordCloneCount--;

    Vec3f vel = { 0.0f, 2.0f, 0.0f };
    Vec3f accel = { 0.0f, 0.0f, 0.0f };
    for (int p = 0; p < 8; p++) {
        Vec3f sparkPos = {
            pos.x + Rand_CenteredFloat(25.0f),
            pos.y + 30.0f + Rand_ZeroFloat(40.0f),
            pos.z + Rand_CenteredFloat(25.0f),
        };
        vel.x = Rand_CenteredFloat(3.0f);
        vel.z = Rand_CenteredFloat(3.0f);
        EffectSsKiraKira_SpawnSmall(play, &sparkPos, &vel, &accel, &(Color_RGBA8){ 180, 220, 255, 255 },
                                    &(Color_RGBA8){ 80, 130, 255, 0 });
    }
    Audio_PlaySoundGeneral(NA_SE_EN_FANTOM_DEAD, &pos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultReverb);

    if (gExtEquipBehavior.fourSwordColInit & (1 << i)) {
        sCloneAC[i].base.acFlags &= ~(AC_ON | AC_HIT);
        sCloneAT[i].base.atFlags &= ~AT_ON;
    }
}

// ─── Collider init ────────────────────────────────────────────────────────────

static void FourSword_InitCloneCollider(PlayState* play, Player* player, int i) {
    if (gExtEquipBehavior.fourSwordColInit & (1 << i))
        return;
    Collider_InitCylinder(play, &sCloneAC[i]);
    Collider_SetCylinder(play, &sCloneAC[i], &player->actor, &sCloneACInit);
    Collider_InitCylinder(play, &sCloneAT[i]);
    Collider_SetCylinder(play, &sCloneAT[i], &player->actor, &sCloneATInit);
    gExtEquipBehavior.fourSwordColInit |= (1 << i);
}

// ─── Collider update ──────────────────────────────────────────────────────────

static void FourSword_UpdateCloneColliders(Player* player, PlayState* play) {
    for (int i = 0; i < FS_CLONE_MAX; i++) {
        if (!gExtEquipBehavior.fourSwordClones[i].alive)
            continue;

        FourSword_InitCloneCollider(play, player, i);

        Vec3f pos = FourSword_GetClonePos(player, i);

        // ── AC: check hit from PREVIOUS frame BEFORE SetAC resets AC_HIT ──────
        // CollisionCheck_SetAC internally calls sACResetFuncs which clears AC_HIT.
        // Must read the flag first, then re-register for the next frame.
        if (sCloneAC[i].base.acFlags & AC_HIT) {
            FourSword_KillClone(player, play, i);
            continue;
        }
        // actor = player: needed for collision type-matching (AC_NO_DAMAGE prevents HP loss).
        sCloneAC[i].base.actor = &player->actor;
        sCloneAC[i].dim.pos.x = (s16)pos.x;
        sCloneAC[i].dim.pos.y = (s16)pos.y;
        sCloneAC[i].dim.pos.z = (s16)pos.z;
        sCloneAC[i].base.acFlags |= AC_ON;
        CollisionCheck_SetAC(play, &play->colChkCtx, &sCloneAC[i].base);

        // ── AT: active during player sword swing ──────────────────────────────
        sCloneAT[i].base.actor = &player->actor; // must NOT be null for AT
        sCloneAT[i].dim.pos.x = (s16)pos.x;
        sCloneAT[i].dim.pos.y = (s16)(pos.y + FS_AT_Y_OFFSET);
        sCloneAT[i].dim.pos.z = (s16)pos.z;
        if (player->meleeWeaponState > 0) {
            sCloneAT[i].base.atFlags |= AT_ON;
            CollisionCheck_SetAT(play, &play->colChkCtx, &sCloneAT[i].base);
        } else {
            sCloneAT[i].base.atFlags &= ~AT_ON;
        }
    }
}

// ─── Charge animation ─────────────────────────────────────────────────────────

static void FourSword_ApplyChargeAnim(Player* player, PlayState* play) {
    AnimationContext_SetLoadFrame(play, &gPlayerAnim_link_fighter_power_kiru_wait, 0, player->skelAnime.limbCount,
                                  player->upperJointTable);
    for (s32 j = PLAYER_LIMB_UPPER; j < PLAYER_LIMB_MAX; j++) {
        player->skelAnime.jointTable[j] = player->upperJointTable[j];
    }
}

// ─── Item mirror ──────────────────────────────────────────────────────────────

// ─── Ivan-style item spawn ────────────────────────────────────────────────────
// Instead of scanning actor lists (fragile: infinite loops, Init overrides),
// detect the MOMENT Link fires/throws via player state rising edges, then
// spawn projectiles directly at clone positions. Same pattern as Ivan fairy
// (ovl_En_Partner/z_en_partner.c UseItem dispatch).

#define FS_ITEM_COOLDOWN 10 // frames between clone projectile spawns

static void FourSword_SpawnCloneProjectiles(Player* player, PlayState* play) {
    // Cooldown: prevent actor spam from rapid state changes
    if (gExtEquipBehavior.fourSwordItemCooldown > 0) {
        gExtEquipBehavior.fourSwordItemCooldown--;
        goto update_prev; // still update prev-state so edges aren't stale
    }

    u8 anyAlive = 0;
    for (int i = 0; i < FS_CLONE_MAX; i++) {
        if (gExtEquipBehavior.fourSwordClones[i].alive) {
            anyAlive = 1;
            break;
        }
    }
    if (!anyAlive)
        goto update_prev;

    // ── 1. Arrow / Slingshot / Deku Nut release ──────────────────────────────
    // player->unk_A73 is set to 4 at the exact frame of fire (z_player.c:3198).
    // Rising edge: was 0 (or !=4), now ==4.
    if (player->unk_A73 == 4 && gExtEquipBehavior.fourSwordPrevA73 != 4) {
        // Determine arrow type from heldItemAction
        s16 arrowType = ARROW_NORMAL;
        PlayerItemAction ia = player->heldItemAction;
        if (ia >= PLAYER_IA_BOW && ia <= PLAYER_IA_BOW_0E) {
            // Bow: PLAYER_IA_BOW=14, FIRE=15, ICE=16, LIGHT=17, ...
            // arrowType: NORMAL=2, FIRE=3, ICE=4, LIGHT=5, ...
            arrowType = ARROW_NORMAL + (ia - PLAYER_IA_BOW);
        } else if (ia == PLAYER_IA_SLINGSHOT) {
            arrowType = ARROW_SEED;
        } else {
            // Deku nut or other — check if nut was just thrown
            // (unk_A73=4 also set for boomerang at z_player.c:3501,3525)
            // Only spawn arrow if we're NOT in a boomerang throw
            if (player->boomerangActor != NULL && gExtEquipBehavior.fourSwordPrevBoomerang == 0) {
                goto skip_arrow; // boomerang edge, handled below
            }
            arrowType = ARROW_NUT;
        }

        for (int i = 0; i < FS_CLONE_MAX; i++) {
            if (!gExtEquipBehavior.fourSwordClones[i].alive)
                continue;
            Vec3f cp = FourSword_GetClonePos(player, i);
            // Ivan pattern (z_en_partner.c:209): spawn at clone pos, parent=NULL
            Actor* arrow = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_ARROW, cp.x, cp.y + 7.0f, cp.z,
                                       (arrowType == ARROW_NUT) ? 0x1000 : 0, // pitch: nuts lob upward
                                       player->actor.shape.rot.y, 0, arrowType);
            // parent stays NULL → EnArrow_Shoot fires it (unk_A73 is already 4)
            (void)arrow;
        }
        gExtEquipBehavior.fourSwordItemCooldown = FS_ITEM_COOLDOWN;
    }
skip_arrow:

    // ── 2. Bomb / held-object release ────────────────────────────────────────
    // PLAYER_STATE1_CARRYING_ACTOR cleared at throw/drop (z_player.c:1723).
    {
        u8 curCarrying = (player->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) ? 1 : 0;
        if (gExtEquipBehavior.fourSwordPrevCarrying && !curCarrying) {
            for (int i = 0; i < FS_CLONE_MAX; i++) {
                if (!gExtEquipBehavior.fourSwordClones[i].alive)
                    continue;
                Vec3f cp = FourSword_GetClonePos(player, i);
                // Ivan pattern (z_en_partner.c:263): always spawn EN_BOM
                Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOM, cp.x, cp.y + 7.0f, cp.z, 0, 0, 0, 0);
            }
            gExtEquipBehavior.fourSwordItemCooldown = FS_ITEM_COOLDOWN;
        }
    }

    // ── 3. Boomerang throw ───────────────────────────────────────────────────
    // player->boomerangActor transitions NULL→non-NULL (z_player.c:3514).
    {
        u8 curBoom = (player->boomerangActor != NULL) ? 1 : 0;
        if (curBoom && !gExtEquipBehavior.fourSwordPrevBoomerang) {
            for (int i = 0; i < FS_CLONE_MAX; i++) {
                if (!gExtEquipBehavior.fourSwordClones[i].alive)
                    continue;
                Vec3f cp = FourSword_GetClonePos(player, i);
                // Ivan pattern (z_player.c:411): slight forward offset
                f32 px = Math_SinS(player->actor.shape.rot.y) * 1.0f + cp.x;
                f32 pz = Math_CosS(player->actor.shape.rot.y) * 1.0f + cp.z;
                EnBoom* boom = (EnBoom*)Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOOM, px, cp.y + 7.0f, pz,
                                                    player->actor.focus.rot.x, player->actor.shape.rot.y, 0, 0);
                if (boom != NULL) {
                    boom->returnTimer = 20;
                }
            }
            gExtEquipBehavior.fourSwordItemCooldown = FS_ITEM_COOLDOWN;
        }
    }

update_prev:
    // Update previous-frame state for next frame's rising-edge detection
    gExtEquipBehavior.fourSwordPrevA73 = player->unk_A73;
    gExtEquipBehavior.fourSwordPrevCarrying = (player->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) ? 1 : 0;
    gExtEquipBehavior.fourSwordPrevBoomerang = (player->boomerangActor != NULL) ? 1 : 0;
}

// ─── Main behavior ────────────────────────────────────────────────────────────

static void FourSword_Behavior(Player* player, PlayState* play) {
    if (!gExtEquipBehavior.fourSwordActive) {
        gExtEquipBehavior.fourSwordSavedSwordEquip =
            (gSaveContext.equips.equipment >> gEquipShifts[EQUIP_TYPE_SWORD]) & 0xF;
        gExtEquipBehavior.fourSwordSavedButtonItem = gSaveContext.equips.buttonItems[0];
        PakLoader_ForceEquipment(FOURSWORD_PAK_PATH);
        gExtEquipBehavior.fourSwordActive = 1;
    }

    if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_LOADING |
                               PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_GETTING_ITEM)) {
        return;
    }

    // Force Kokiri Sword as the base so the sword action system works
    // (PakLoader only overrides visuals, not the equipment/action state)
    Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_KOKIRI);
    gSaveContext.equips.buttonItems[0] = ITEM_SWORD_KOKIRI;

    u8 isShielding = (player->stateFlags1 & PLAYER_STATE1_SHIELDING) ? 1 : 0;
    u8 bHeld = CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_B) ? 1 : 0;

    if (isShielding && bHeld) {
        if (!gExtEquipBehavior.fourSwordCharging)
            gExtEquipBehavior.fourSwordBHoldTimer++;

        if (!gExtEquipBehavior.fourSwordCharging && gExtEquipBehavior.fourSwordBHoldTimer >= FS_CHARGE_HOLD) {
            gExtEquipBehavior.fourSwordCharging = 1;
            Sfx_PlaySfxCentered(NA_SE_SY_ATTENTION_ON);

            s16 yaw = player->actor.shape.rot.y;
            for (int i = 0; i < FS_CLONE_MAX; i++) {
                if (gSaveContext.magic < FS_CLONE_MP_COST)
                    break;
                Vec3f off = FourSword_FormationOffset(yaw, i);
                Vec3f spawnPos = {
                    player->actor.world.pos.x + off.x,
                    player->actor.world.pos.y,
                    player->actor.world.pos.z + off.z,
                };
                FourSword_SpawnClone(play, player, spawnPos);
            }
        }
    } else {
        gExtEquipBehavior.fourSwordBHoldTimer = 0;
        gExtEquipBehavior.fourSwordCharging = 0;
    }

    if (gExtEquipBehavior.fourSwordCharging) {
        FourSword_ApplyChargeAnim(player, play);
    }

    FourSword_UpdateCloneColliders(player, play);

    // Ivan-style: detect item use via rising edges, spawn at clone positions
    FourSword_SpawnCloneProjectiles(player, play);
}

// ─── Cleanup ──────────────────────────────────────────────────────────────────

static void FourSword_Cleanup(void) {
    if (gExtEquipBehavior.fourSwordActive) {
        PakLoader_ClearForcedEquipment();
        Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, gExtEquipBehavior.fourSwordSavedSwordEquip);
        gSaveContext.equips.buttonItems[0] = gExtEquipBehavior.fourSwordSavedButtonItem;
        gExtEquipBehavior.fourSwordActive = 0;
    }
    gExtEquipBehavior.fourSwordCharging = 0;
    gExtEquipBehavior.fourSwordBHoldTimer = 0;
    gExtEquipBehavior.fourSwordCloneCount = 0;
    for (int i = 0; i < FS_CLONE_MAX; i++) {
        gExtEquipBehavior.fourSwordClones[i].alive = 0;
        if (gExtEquipBehavior.fourSwordColInit & (1 << i)) {
            sCloneAC[i].base.acFlags &= ~(AC_ON | AC_HIT);
            sCloneAT[i].base.atFlags &= ~AT_ON;
        }
    }
    gExtEquipBehavior.fourSwordColInit = 0;
    gExtEquipBehavior.fourSwordItemCooldown = 0;
    gExtEquipBehavior.fourSwordPrevA73 = 0;
    gExtEquipBehavior.fourSwordPrevCarrying = 0;
    gExtEquipBehavior.fourSwordPrevBoomerang = 0;
}

// Player_OverrideLimbDrawGameplayDefault handles ALL per-limb overrides:
// upperLimbRot matrix corrections at PLAYER_LIMB_UPPER (via Common), plus
// equipment DL selection for L_HAND / R_HAND / SHEATH / WAIST. Passing player
// as arg makes the clone show the same sword+shield+sheath as Link himself.
extern s32 Player_OverrideLimbDrawGameplayDefault(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot,
                                                  void* arg);

// ─── Clone tunic colors (Four Swords Adventures style) ───────────────────────
// Red, Blue, Purple — one per clone. Applied via gDPSetEnvColor before drawing
// the skeleton, same mechanism Player_DrawImpl uses for tunic tinting.
static const Color_RGB8 sFourSwordCloneColors[FS_CLONE_MAX] = {
    { 180, 20, 20 },  // Clone 0: Red Link
    { 20, 50, 180 },  // Clone 1: Blue Link
    { 130, 20, 180 }, // Clone 2: Purple Link
};

// ─── Draw ─────────────────────────────────────────────────────────────────────
// Full pose = lower body + upper body (already merged in skelAnime.jointTable).
// Player_OverrideLimbDrawGameplayDefault handles equipment DLs + upperLimbRot.
// Each clone gets a distinct tunic color via gDPSetEnvColor before draw.

static void FourSword_Draw(Player* player, PlayState* play) {
    u8 anyAlive = 0;
    for (int i = 0; i < FS_CLONE_MAX; i++) {
        if (gExtEquipBehavior.fourSwordClones[i].alive) {
            anyAlive = 1;
            break;
        }
    }
    if (!anyAlive)
        return;

    // skelAnime.jointTable already has the correctly merged pose (lower + upper body)
    // after AnimationContext_Update runs before the draw phase. Copy it into a local
    // buffer so the override callback can safely write to rot[] without corrupting
    // the real player joint table (e.g. leg IK in func_8008F87C writes back to rot).
    Vec3s blended[PLAYER_LIMB_BUF_COUNT];
    s32 lc = player->skelAnime.limbCount;
    for (s32 j = 0; j <= lc; j++) {
        blended[j] = player->skelAnime.jointTable[j];
    }

    f32 yawRad = (f32)player->actor.shape.rot.y * ((f32)M_PI / 32768.0f);

    OPEN_DISPS(play->state.gfxCtx);

    for (int i = 0; i < FS_CLONE_MAX; i++) {
        if (!gExtEquipBehavior.fourSwordClones[i].alive)
            continue;

        Vec3f pos = FourSword_GetClonePos(player, i);

        // Four Swords Adventures tunic tint: override env color per clone
        const Color_RGB8* c = &sFourSwordCloneColors[i];
        gDPSetEnvColor(POLY_OPA_DISP++, c->r, c->g, c->b, 0);

        // Re-copy blended[] for each clone — the override callback may modify
        // rot entries in place (leg IK), so subsequent clones need fresh data.
        for (s32 j = 0; j <= lc; j++) {
            blended[j] = player->skelAnime.jointTable[j];
        }

        Matrix_Push();
        Matrix_Translate(pos.x, pos.y, pos.z, MTXMODE_NEW);
        Matrix_RotateY(yawRad, MTXMODE_APPLY);
        Matrix_Scale(player->actor.scale.x, player->actor.scale.y, player->actor.scale.z, MTXMODE_APPLY);

        SkelAnime_DrawFlexOpa(play, player->skelAnime.skeleton, blended, player->skelAnime.dListCount,
                              Player_OverrideLimbDrawGameplayDefault, // full equipment DLs + upperLimbRot
                              NULL, player);

        Matrix_Pop();

        // Custom items: temporarily swap player world pos so draw functions
        // (spinner, gust jar, ball chain, rods, etc.) position at the clone.
        // Same approach as Harpoon's visual sync for dummy players.
        Vec3f savedPos = player->actor.world.pos;
        player->actor.world.pos = pos;
        CustomItems_OverrideDraw(player, play);
        player->actor.world.pos = savedPos;
    }

    // Restore Link's original tunic color so subsequent draws aren't tinted
    extern Color_RGB8 sTunicColors[];
    Color_RGB8* orig = &sTunicColors[player->currentTunic];
    gDPSetEnvColor(POLY_OPA_DISP++, orig->r, orig->g, orig->b, 0);

    CLOSE_DISPS(play->state.gfxCtx);
}
