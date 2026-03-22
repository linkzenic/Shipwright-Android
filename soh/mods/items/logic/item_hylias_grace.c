/**
 * item_hylias_grace.c - Hylia's Grace (fairy transformation)
 *
 * Controls:
 *   C Button:     Activate (consumes 24 magic)
 *   A (flying):   Ascend (drains timer faster)
 *   B (flying):   Descend
 *   L (flying):   Sprint (drains timer faster)
 *   Analog:       Flight direction
 *
 * Features:
 *   - Transform into fairy for 10 seconds
 *   - Free flight ignores collision
 *   - Green fairy glow visual effect
 *   - Farore's Wind style warp animation
 * vanilla's 0.83f) to compensate. Timer-based chaining via R_UPDATE_RATE
 * ensures reliable state transitions without relying on animDone.
 *
 * No cooldown - can be used again immediately
 */

#include "z64.h"
#include "item_hylias_grace.h"
#include "../custom_items.h"
#include "../helpers/equip_helper.h"
#include "../helpers/fx_helper.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "objects/gameplay_keep/gameplay_keep.h"

extern void Player_Draw(Actor* thisx, PlayState* play);

static s8 sHGracePrevInvinc = 0;
static s32 sHGPhaseEnd = 0; // Absolute hgTimer value when current animation phase ends

// Saved fairy position — used to undo displacement from the normal collision
// response (Actor_UpdateBgCheckInfo / OC) that runs after our code each frame.
static Vec3f sFairyPos;
static u8 sFairyPosValid = 0;
static Vec3f sFairyVelocity = { 0.0f, 0.0f, 0.0f };
static f32 sFairyDimLevel = 0.0f;

// Ivan possess mode (Spirit Medallion → spawn real EnPartner controlled by Player 1)
static Actor* sIvanActor = NULL;
u8 gIvanPossessActive = 0; // Global flag — extern'd in z_player.c / z_collision_check.c

// Pink fairy skeleton (Hylia's Grace uses Ivan's 3D model with pink colors)
static SkelAnime sPinkFairySkel;
static Vec3s sPinkFairyJointTable[15];
static Vec3s sPinkFairyMorphTable[15];
static u8 sPinkFairySkelInited = 0;

static void HGrace_InitPinkFairySkel(PlayState* play) {
    if (sPinkFairySkelInited)
        return;
    SkelAnime_Init(play, &sPinkFairySkel, &gFairySkel, &gFairyAnim, sPinkFairyJointTable, sPinkFairyMorphTable, 15);
    sPinkFairySkelInited = 1;
}

// Forward declarations (defined after HGrace_IsPassableBarrier, used in HGrace_Stop)
static void HGrace_DimLighting(PlayState* play, f32 intensity);
static void HGrace_ResetLighting(PlayState* play);

// Halved from vanilla 0.83f to compensate for double LinkAnimation_Update
// (vanilla's Player_Action_Idle calls it once, we call it again — Demise pattern).
// Effective speed per tick: 0.415 * R_UPDATE_RATE, matching vanilla's 0.83 * R * 0.5
#define HGRACE_ANIM_SPEED 0.415f

// =============================================================================
// Fairy visual effects
// =============================================================================

static void HGrace_SpawnFairySparkles(Player* p, PlayState* play) {
    Vec3f accel = { 0.0f, 0.0f, 0.0f };
    Color_RGBA8 primColor, envColor;

    if (hgForcedBySpell) {
        primColor = (Color_RGBA8){ 255, 220, 100, 255 };
        envColor = (Color_RGBA8){ 200, 150, 30, 255 };
    } else {
        primColor = (Color_RGBA8){ 150, 255, 150, 255 };
        envColor = (Color_RGBA8){ 50, 200, 50, 255 };
    }

    for (u8 i = 0; i < 3; i++) {
        Vec3f pos;
        pos.x = p->actor.world.pos.x + Rand_CenteredFloat(15.0f);
        pos.y = p->actor.world.pos.y + 20.0f + Rand_CenteredFloat(10.0f);
        pos.z = p->actor.world.pos.z + Rand_CenteredFloat(15.0f);

        Vec3f vel;
        vel.x = Rand_CenteredFloat(1.0f);
        vel.y = Rand_ZeroFloat(1.0f);
        vel.z = Rand_CenteredFloat(1.0f);

        EffectSsKiraKira_SpawnFocused(play, &pos, &vel, &accel, &primColor, &envColor, 300, 12);
    }
}

static void HGrace_SpawnWarpSparkles(PlayState* play, Vec3f* center) {
    Vec3f pos, vel, accel;
    Color_RGBA8 primColor = { 100, 255, 150, 255 };
    Color_RGBA8 envColor = { 0, 200, 100, 255 };

    accel.x = accel.y = accel.z = 0.0f;

    for (u8 i = 0; i < 16; i++) {
        f32 angle = (f32)i * (65536.0f / 16.0f);
        s16 angleS = (s16)angle;

        pos.x = center->x + Math_SinS(angleS) * 30.0f;
        pos.y = center->y + 20.0f + Rand_ZeroFloat(20.0f);
        pos.z = center->z + Math_CosS(angleS) * 30.0f;

        vel.x = Math_SinS(angleS) * 2.0f;
        vel.y = Rand_ZeroFloat(3.0f) + 1.0f;
        vel.z = Math_CosS(angleS) * 2.0f;

        EffectSsKiraKira_SpawnFocused(play, &pos, &vel, &accel, &primColor, &envColor, 800, 25);
    }
}

// =============================================================================
// Fairy draw function (green fairy — replaces Link's model during fairy mode)
//
// Body: 3-layer glow circles (large dim + medium + bright core) using the
//       light-system pattern (dithering + gGlowCircleDL, billboarded).
// Wings: Vanilla gFairyWing1DL-4DL with segment 0x08 material setup matching
//        EnElf_Draw (Gfx_SetupDL_27Xlu, G_RM_ZB_CLD_SURF2, pulsating envAlpha).
// =============================================================================

static void HGrace_DrawFairy(Actor* thisx, PlayState* play) {
    Player* p = (Player*)thisx;
    if (!sPinkFairySkelInited)
        return;

    // Animate wings
    SkelAnime_Update(&sPinkFairySkel);

    Gfx* dListHead = Graph_Alloc(play->state.gfxCtx, sizeof(Gfx) * 4);

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_27Xlu(play->state.gfxCtx);

    // Pulsating env alpha (vanilla fairy breathing effect)
    s32 envAlpha = (play->gameplayFrames * 50) & 0x1FF;
    envAlpha = (envAlpha > 255) ? 511 - envAlpha : envAlpha;

    // Segment 0x08: PrimColor (pink inner) + RenderMode
    gSPSegment(POLY_XLU_DISP++, 0x08, dListHead);
    gDPPipeSync(dListHead++);
    gDPSetPrimColor(dListHead++, 0, 0x01, 255, 180, 220, 200); // Pink inner
    gDPSetRenderMode(dListHead++, G_RM_PASS, G_RM_ZB_CLD_SURF2);
    gSPEndDisplayList(dListHead++);

    // Env color (pink outer glow)
    gDPSetEnvColor(POLY_XLU_DISP++, 255, 100, 180, (u8)envAlpha);

    // Position + scale at fairy location
    Matrix_Translate(p->actor.world.pos.x, p->actor.world.pos.y + 20.0f, p->actor.world.pos.z, MTXMODE_NEW);
    f32 pulse = (Math_SinS(play->gameplayFrames * 4096) * 0.1f) + 1.0f;
    f32 s = 0.008f * pulse;
    Matrix_Scale(s, s, s, MTXMODE_APPLY);

    POLY_XLU_DISP = SkelAnime_DrawSkeleton2(play, &sPinkFairySkel, NULL, NULL, NULL, POLY_XLU_DISP);

    CLOSE_DISPS(play->state.gfxCtx);
}

// =============================================================================
// Stop / Start
// =============================================================================

static void HGrace_Stop(Player* p, PlayState* play) {
    if (!hgActive)
        return;

    // Clean up Ivan possess mode if active
    if (sIvanActor != NULL) {
        Actor_Kill(sIvanActor);
        sIvanActor = NULL;
    }
    gIvanPossessActive = 0;

    // Sync Link position to fairy before restoring
    if (sFairyPosValid) {
        p->actor.world.pos = sFairyPos;
    }

    // Restore Link
    p->actor.draw = Player_Draw;
    p->stateFlags1 &= ~(PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_INPUT_DISABLED);
    p->invincibilityTimer = 20;

    // Re-enable colliders
    p->cylinder.base.atFlags |= AT_ON;
    p->cylinder.base.acFlags |= AC_ON;
    p->cylinder.base.ocFlags1 |= OC1_ON;

    // Reset camera
    func_8005B1A4(Play_GetCamera(play, 0));

    // Reset SW97 spirit mode effects
    if (hgForcedBySpell) {
        HGrace_ResetLighting(play);
        sFairyDimLevel = 0.0f;
        p->stateFlags2 &= ~0x100000; // restore Navi
    }

    // Reset smooth velocity
    sFairyVelocity.x = sFairyVelocity.y = sFairyVelocity.z = 0.0f;

    // Only set cooldown if fairy mode was actually reached
    u8 wasFairy = (hgState == HGRACE_STATE_FAIRY || hgState == HGRACE_STATE_WARP_OUT);

    hgActive = 0;
    hgState = HGRACE_STATE_IDLE;
    hgSubPhase = 0;
    hgTimer = 0;
    hgFairy = NULL;
    hgForcedBySpell = 0;
    sFairyPosValid = 0;
    sPinkFairySkelInited = 0;

    // No cooldown - free to use again immediately
    (void)wasFairy;
}

static void HGrace_Start(Player* p, PlayState* play) {
    if (hgActive)
        return;
    if (!ItemMagic_HasEnough(play, HGRACE_MAGIC_COST)) {
        Audio_PlaySoundGeneral(NA_SE_SY_ERROR, &p->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }
    if (!(p->actor.bgCheckFlags & BGCHECKFLAG_GROUND))
        return;
    if (p->stateFlags1 & PLAYER_STATE1_IN_WATER)
        return;

    hgActive = 1;
    hgState = HGRACE_STATE_CASTING;
    hgSubPhase = HGRACE_CAST_KAZE1;
    hgTimer = -2;
    ItemMagic_Consume(play, HGRACE_MAGIC_COST);
}

// =============================================================================
// State: Casting (Farore's Wind animation sequence)
//
// Uses DEMISE PATTERN for reliable animation handling:
// 1. LinkAnimation_Change at 0.415f (half vanilla speed)
// 2. Explicit LinkAnimation_Update call (double-update with vanilla's call)
//    → effective speed = 0.415 * R_UPDATE_RATE per tick = vanilla's 0.83 * R * 0.5
// 3. Timer-based chaining computed from R_UPDATE_RATE (never relies on animDone)
// =============================================================================

static void HGrace_ComputePhaseEnd(s32 baseTimer, f32 lastFrame) {
    f32 rate = HGRACE_ANIM_SPEED * R_UPDATE_RATE;
    if (rate < 0.1f)
        rate = 0.415f; // Safety fallback
    sHGPhaseEnd = baseTimer + (s32)(lastFrame / rate) + 1;
}

static void HGrace_StateCasting(Player* p, PlayState* play) {
    hgTimer++;

    // Deferred setup (Demise pattern): camera + stateFlags on first tick
    if (hgTimer == -1) {
        Camera_ChangeSetting(Play_GetCamera(play, 0), CAM_SET_TURN_AROUND);
        Camera_SetCameraData(Play_GetCamera(play, 0), 4, NULL, NULL, 10, 0, 0);
        p->stateFlags1 |= PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_INPUT_DISABLED;
    }

    // Lock player in place every frame
    p->stateFlags1 |= PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_INPUT_DISABLED;
    p->linearVelocity = 0.0f;
    p->actor.speedXZ = 0.0f;
    p->actor.velocity.x = p->actor.velocity.y = p->actor.velocity.z = 0.0f;

    // Start first animation on frame 0
    if (hgTimer == 0) {
        LinkAnimation_Change(play, &p->skelAnime, &gPlayerAnim_link_magic_kaze1, HGRACE_ANIM_SPEED, 0.0f,
                             Animation_GetLastFrame(&gPlayerAnim_link_magic_kaze1), ANIMMODE_ONCE, -8.0f);
        HGrace_ComputePhaseEnd(hgTimer, Animation_GetLastFrame(&gPlayerAnim_link_magic_kaze1));
        Audio_PlaySoundGeneral(NA_SE_VO_LI_MAGIC_FROL, &p->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }

    // Double-update: vanilla calls LinkAnimation_Update once, we call it again (Demise pattern)
    if (hgTimer >= 0) {
        LinkAnimation_Update(play, &p->skelAnime);
    }

    // Timer-based animation chaining (like Demise — does NOT rely on animDone)
    if (hgTimer > 0 && hgTimer >= sHGPhaseEnd) {
        switch (hgSubPhase) {
            case HGRACE_CAST_KAZE1:
                LinkAnimation_Change(play, &p->skelAnime, &gPlayerAnim_link_magic_kaze2, HGRACE_ANIM_SPEED, 0.0f,
                                     Animation_GetLastFrame(&gPlayerAnim_link_magic_kaze2), ANIMMODE_ONCE, 0.0f);
                HGrace_ComputePhaseEnd(hgTimer, Animation_GetLastFrame(&gPlayerAnim_link_magic_kaze2));
                hgSubPhase = HGRACE_CAST_KAZE2;
                break;

            case HGRACE_CAST_KAZE2:
                LinkAnimation_Change(play, &p->skelAnime, &gPlayerAnim_link_magic_kaze3, HGRACE_ANIM_SPEED, 0.0f,
                                     Animation_GetLastFrame(&gPlayerAnim_link_magic_kaze3), ANIMMODE_ONCE, 0.0f);
                HGrace_ComputePhaseEnd(hgTimer, Animation_GetLastFrame(&gPlayerAnim_link_magic_kaze3));
                hgSubPhase = HGRACE_CAST_KAZE3;
                break;

            case HGRACE_CAST_KAZE3:
                // Casting complete — transition to warp
                hgState = HGRACE_STATE_WARP_IN;
                hgTimer = 0;
                break;
        }
    }

    // Green sparkles during casting
    if (hgTimer > 10 && play->gameplayFrames % 4 == 0) {
        Vec3f sparklePos = p->actor.world.pos;
        sparklePos.y += 30.0f + Rand_ZeroFloat(30.0f);
        sparklePos.x += Rand_CenteredFloat(20.0f);
        sparklePos.z += Rand_CenteredFloat(20.0f);
        Vec3f vel = { 0.0f, 1.5f, 0.0f };
        Vec3f accel = { 0.0f, 0.0f, 0.0f };
        Color_RGBA8 primColor = { 150, 255, 150, 255 };
        Color_RGBA8 envColor = { 50, 200, 50, 255 };
        EffectSsKiraKira_SpawnFocused(play, &sparklePos, &vel, &accel, &primColor, &envColor, 600, 20);
    }
}

// =============================================================================
// State: Warp Enter (blue warp transition)
// =============================================================================

static void HGrace_StateWarpEnter(Player* p, PlayState* play) {
    p->stateFlags1 |= PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_INPUT_DISABLED;
    p->linearVelocity = 0.0f;
    p->actor.speedXZ = 0.0f;

    hgTimer++;

    if (hgTimer == 1) {
        LinkAnimation_Change(play, &p->skelAnime, &gPlayerAnim_link_demo_warp, HGRACE_ANIM_SPEED, 0.0f,
                             Animation_GetLastFrame(&gPlayerAnim_link_demo_warp), ANIMMODE_ONCE, -8.0f);
        Audio_PlaySoundGeneral(NA_SE_PL_MAGIC_WIND_WARP, &p->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }

    // Double-update (Demise pattern)
    if (hgTimer >= 1) {
        LinkAnimation_Update(play, &p->skelAnime);
    }

    // Blue-green warp sparkles
    if (hgTimer % 3 == 0) {
        Vec3f sparklePos = p->actor.world.pos;
        sparklePos.y += 40.0f;
        sparklePos.x += Rand_CenteredFloat(30.0f);
        sparklePos.z += Rand_CenteredFloat(30.0f);
        Vec3f vel = { 0.0f, 2.0f, 0.0f };
        Vec3f accel = { 0.0f, -0.1f, 0.0f };
        Color_RGBA8 primColor = { 100, 255, 200, 255 };
        Color_RGBA8 envColor = { 0, 150, 255, 255 };
        EffectSsKiraKira_SpawnFocused(play, &sparklePos, &vel, &accel, &primColor, &envColor, 600, 20);
    }

    // Screen flash at midpoint
    if (hgTimer == HGRACE_WARP_IN_DURATION / 2) {
        func_800AA000(400.0f, 200, 30, 100);
    }

    // Transition to fairy mode — draw fairy DL instead of Link
    if (hgTimer >= HGRACE_WARP_IN_DURATION) {
        HGrace_InitPinkFairySkel(play);
        p->actor.draw = HGrace_DrawFairy;
        p->invincibilityTimer = -1;

        // Disable all colliders
        p->cylinder.base.atFlags &= ~AT_ON;
        p->cylinder.base.acFlags &= ~AC_ON;
        p->cylinder.base.ocFlags1 &= ~OC1_ON;

        // Release camera so it follows the fairy during flight
        p->stateFlags1 &= ~PLAYER_STATE1_IN_ITEM_CS;
        func_8005B1A4(Play_GetCamera(play, 0));

        hgState = HGRACE_STATE_FAIRY;
        hgTimer = 0; // Toggle mode — no duration limit
    }
}

// =============================================================================
// Fairy collision bypass
// Scene geometry always blocks. ALL DynaPoly actors are passable (doors, shutters, etc.)
// =============================================================================

static s32 HGrace_IsPassableBarrier(PlayState* play, s32 bgId) {
    // Scene collision is never passable
    if (!DynaPoly_IsBgIdBgActor(bgId))
        return 0;

    // All DynaPoly actors are passable — fairy goes through everything
    return 1;
}

// =============================================================================
// State: Fairy (free flight mode)
// Link IS the fairy — fairy DL drawn, direct position control.
// Joystick = XZ movement (camera-relative), A = ascend (+4), B = descend (-4)
// L = sprint (2x speed, 2x timer drain)
// =============================================================================

static void HGrace_DimLighting(PlayState* play, f32 intensity) {
    if (play->roomCtx.curRoom.behaviorType1 != ROOM_BEHAVIOR_TYPE1_5) {
        intensity = CLAMP(intensity, 0.0f, 1.0f);
        f32 fogFactor = (intensity > 0.2f) ? (intensity - 0.2f) : 0.0f;
        play->envCtx.adjFogNear = (s16)((850.0f - play->envCtx.lightSettings.fogNear) * fogFactor);
        f32 colorFactor = CLAMP_MAX(intensity * 5.0f, 1.0f);
        for (s32 i = 0; i < ARRAY_COUNT(play->envCtx.adjFogColor); i++) {
            play->envCtx.adjFogColor[i] = -(s16)(play->envCtx.lightSettings.fogColor[i] * colorFactor);
        }
    }
}

static void HGrace_ResetLighting(PlayState* play) {
    play->envCtx.adjFogNear = 0;
    for (s32 i = 0; i < ARRAY_COUNT(play->envCtx.adjFogColor); i++) {
        play->envCtx.adjFogColor[i] = 0;
    }
}

static void HGrace_SpawnTrailSparkles(Player* p, PlayState* play, f32 actualSpeed) {
    // Spawn extra sparkles proportional to movement speed (0 at rest, up to 6 at full sprint)
    f32 maxSpeed =
        hgForcedBySpell ? (HGRACE_SW97_SPEED * HGRACE_SW97_SPRINT_MULT) : (HGRACE_SPEED * HGRACE_SPRINT_MULT);
    s32 count = (s32)(actualSpeed / maxSpeed * 6.0f);
    if (count < 1)
        return;
    if (count > 6)
        count = 6;

    Color_RGBA8 primColor, envColor;
    if (hgForcedBySpell) {
        primColor = (Color_RGBA8){ 255, 200, 80, 255 };
        envColor = (Color_RGBA8){ 200, 120, 20, 255 };
    } else {
        primColor = (Color_RGBA8){ 120, 255, 120, 255 };
        envColor = (Color_RGBA8){ 30, 180, 30, 255 };
    }

    Vec3f accel = { 0.0f, 0.0f, 0.0f };
    for (s32 i = 0; i < count; i++) {
        Vec3f pos;
        pos.x = p->actor.world.pos.x + Rand_CenteredFloat(10.0f);
        pos.y = p->actor.world.pos.y + 20.0f + Rand_CenteredFloat(8.0f);
        pos.z = p->actor.world.pos.z + Rand_CenteredFloat(10.0f);

        // Trail behind: velocity opposite to movement direction
        Vec3f vel;
        vel.x = -sFairyVelocity.x * 0.3f + Rand_CenteredFloat(0.5f);
        vel.y = -sFairyVelocity.y * 0.2f + Rand_ZeroFloat(0.5f);
        vel.z = -sFairyVelocity.z * 0.3f + Rand_CenteredFloat(0.5f);

        EffectSsKiraKira_SpawnFocused(play, &pos, &vel, &accel, &primColor, &envColor, 200, 10);
    }
}

// Check proximity to transition actor entries (doors/loading planes) and trigger room transitions.
// Uses transiActorCtx.list[] directly — works even if the door actor isn't spawned or reachable.
// Returns 1 if a door transition was triggered.
static s32 HGrace_CheckDoorTransition(Player* p, PlayState* play) {
    for (s32 i = 0; i < play->transiActorCtx.numActors; i++) {
        TransitionActorEntry* entry = &play->transiActorCtx.list[i];

        // Skip disabled entries (negative id means destroyed)
        if (entry->id < 0)
            continue;

        f32 dx = p->actor.world.pos.x - (f32)entry->pos.x;
        f32 dy = p->actor.world.pos.y - (f32)entry->pos.y;
        f32 dz = p->actor.world.pos.z - (f32)entry->pos.z;
        f32 xzDist = sqrtf(SQ(dx) + SQ(dz));

        // Trigger range: 100 units XZ, 80 units Y (generous — fairy needs to reach through walls)
        if (xzDist < 100.0f && fabsf(dy) < 80.0f) {
            // Determine side: dot product of fairy-relative pos with door facing
            f32 dot = dx * Math_SinS(entry->rotY) + dz * Math_CosS(entry->rotY);
            s32 side = (dot < 0.0f) ? 0 : 1;

            s8 targetRoom = entry->sides[side].room;
            if (targetRoom >= 0 && targetRoom != play->roomCtx.curRoom.num) {
                // Load the target room
                func_8009728C(play, &play->roomCtx, targetRoom);

                // Teleport fairy to the door position + push past it so it doesn't re-trigger
                f32 pushDir = (side == 0) ? 1.0f : -1.0f;
                p->actor.world.pos.x = (f32)entry->pos.x + Math_SinS(entry->rotY) * 80.0f * pushDir;
                p->actor.world.pos.y = (f32)entry->pos.y + 20.0f;
                p->actor.world.pos.z = (f32)entry->pos.z + Math_CosS(entry->rotY) * 80.0f * pushDir;
                sFairyPos = p->actor.world.pos;

                // Swap rooms
                func_80097534(play, &play->roomCtx);
                return 1;
            }
        }
    }
    return 0;
}

static void HGrace_StateFairy(Player* p, PlayState* play) {

    // Restore position: undo displacement from Actor_UpdateBgCheckInfo
    if (sFairyPosValid) {
        p->actor.world.pos = sFairyPos;
    }

    // First-frame setup: ensure camera is in smooth follow mode
    if (!sFairyPosValid) {
        Camera_ChangeSetting(Play_GetCamera(play, 0), CAM_SET_NORMAL0);
    }

    // INPUT_DISABLED prevents normal player actions; IN_ITEM_CS is NOT set
    // so the camera follows the fairy freely.
    p->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
    p->stateFlags1 &= ~PLAYER_STATE1_IN_ITEM_CS;
    p->actor.draw = HGrace_DrawFairy;

    // Update focus.pos so the camera tracks the fairy position
    p->actor.focus.pos.x = p->actor.world.pos.x;
    p->actor.focus.pos.y = p->actor.world.pos.y + 20.0f;
    p->actor.focus.pos.z = p->actor.world.pos.z;

    p->invincibilityTimer = -1;

    // Zero ALL engine velocity — we move position via smooth velocity
    p->linearVelocity = 0.0f;
    p->actor.speedXZ = 0.0f;
    p->actor.velocity.x = p->actor.velocity.y = p->actor.velocity.z = 0.0f;

    // Disable colliders every frame
    p->cylinder.base.atFlags &= ~AT_ON;
    p->cylinder.base.acFlags &= ~AC_ON;
    p->cylinder.base.ocFlags1 &= ~OC1_ON;

    // Read input
    u8 aBtn = CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_A);
    u8 bBtn = CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_B);
    u8 lBtn = CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_L);

    // Use OOT's native stick processing (func_80077D10 + Camera_GetInputDirYaw)
    // for camera-relative movement identical to normal gameplay.
    f32 stickMag;
    s16 stickAngle;
    func_80077D10(&stickMag, &stickAngle, &play->state.input[0]);
    s16 worldYaw = Camera_GetInputDirYaw(GET_ACTIVE_CAM(play)) + stickAngle;

    // Speed: SW97 spirit fairy is faster
    f32 speed = HGRACE_SPEED;
    f32 sprintMult = HGRACE_SPRINT_MULT;
    if (lBtn)
        speed *= sprintMult;

    // Target velocity from input
    f32 targetVY = 0.0f;
    if (aBtn)
        targetVY = speed;
    if (bBtn)
        targetVY = -speed;

    f32 targetVX = 0.0f, targetVZ = 0.0f;

    if (stickMag > 10.0f) {
        f32 normMag = CLAMP_MAX(stickMag / 60.0f, 1.0f);

        targetVX = Math_SinS(worldYaw) * speed * normMag;
        targetVZ = Math_CosS(worldYaw) * speed * normMag;
    }

    // Smooth velocity interpolation (acceleration / deceleration)
    f32 maxStep = speed * 0.5f;
    Math_SmoothStepToF(&sFairyVelocity.x, targetVX, 0.3f, maxStep, 0.01f);
    Math_SmoothStepToF(&sFairyVelocity.y, targetVY, 0.3f, maxStep, 0.01f);
    Math_SmoothStepToF(&sFairyVelocity.z, targetVZ, 0.3f, maxStep, 0.01f);

    // Smooth rotation: fairy faces movement direction
    if (fabsf(sFairyVelocity.x) > 0.5f || fabsf(sFairyVelocity.z) > 0.5f) {
        s16 moveYaw = Math_Atan2S(sFairyVelocity.x, sFairyVelocity.z);
        Math_SmoothStepToS(&p->actor.shape.rot.y, moveYaw, 5, 0x1000, 0x100);
    }

    // Apply smooth velocity
    Vec3f prevPos = p->actor.world.pos;
    Vec3f desiredPos;
    desiredPos.x = prevPos.x + sFairyVelocity.x;
    desiredPos.y = prevPos.y + sFairyVelocity.y;
    desiredPos.z = prevPos.z + sFairyVelocity.z;

    // No wall/ceiling collision — fairy passes through everything (walls, doors, actors).
    // Only floor constraint below.

    // Floor constraint: prevent going underground
    CollisionPoly* floorPoly = NULL;
    s32 floorBgId;
    f32 floor = BgCheck_EntityRaycastFloor5(play, &play->colCtx, &floorPoly, &floorBgId, &p->actor, &desiredPos);
    if (floor > BGCHECK_Y_MIN && desiredPos.y < floor + HGRACE_FAIRY_HOVER) {
        desiredPos.y = floor + HGRACE_FAIRY_HOVER;
        if (sFairyVelocity.y < 0.0f)
            sFairyVelocity.y = 0.0f;
    }

    p->actor.world.pos = desiredPos;

    // Save authoritative position
    sFairyPos = desiredPos;
    sFairyPosValid = 1;

    // Loading zone detection: check if fairy is over a floor polygon with an exit index.
    // This triggers scene transitions (doors, exits, grottos) while in fairy mode.
    if (floorPoly != NULL && play->transitionTrigger == TRANS_TRIGGER_OFF) {
        s32 exitIndex = SurfaceType_GetSceneExitIndex(&play->colCtx, floorPoly, floorBgId);
        if (exitIndex != 0) {
            // Deactivate fairy mode before transitioning
            HGrace_Stop(p, play);

            play->nextEntranceIndex = play->setupExitList[exitIndex - 1];
            if (IS_RANDO) {
                play->nextEntranceIndex = Entrance_OverrideNextIndex(play->nextEntranceIndex);
            }

            if (play->nextEntranceIndex == ENTR_RETURN_GROTTO) {
                gSaveContext.respawnFlag = 2;
                play->nextEntranceIndex = gSaveContext.respawn[RESPAWN_MODE_RETURN].entranceIndex;
                play->transitionType = TRANS_TYPE_FADE_WHITE;
                gSaveContext.nextTransitionType = TRANS_TYPE_FADE_WHITE;
            } else {
                gSaveContext.retainWeatherMode = 1;
                Scene_SetTransitionForNextEntrance(play);
            }
            play->transitionTrigger = TRANS_TRIGGER_START;
            return;
        }
    }

    // Door transition: check proximity to En_Door actors for room loading
    if (HGrace_CheckDoorTransition(p, play)) {
        return;
    }

    // Fairy sparkles + speed-proportional trail (always visible — no timer/flicker)
    f32 actualSpeed = sqrtf(SQ(sFairyVelocity.x) + SQ(sFairyVelocity.y) + SQ(sFairyVelocity.z));

    HGrace_SpawnFairySparkles(p, play);
    if (actualSpeed > 1.0f) {
        HGrace_SpawnTrailSparkles(p, play, actualSpeed);
    }

    // No timer — toggle mode. Fairy lasts until player presses the item button again.
}

// =============================================================================
// State: Warp Exit (Link reappears)
// =============================================================================

static void HGrace_StateWarpExit(Player* p, PlayState* play) {
    p->stateFlags1 |= PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_INPUT_DISABLED;
    p->linearVelocity = 0.0f;
    p->actor.speedXZ = 0.0f;
    p->actor.velocity.x = p->actor.velocity.y = p->actor.velocity.z = 0.0f;

    // Every frame: force position back to fairy location, ignoring collision pushback.
    // Actor_UpdateBgCheckInfo runs after us and displaces world.pos if inside geometry;
    // we undo that displacement each frame so Link teleports cleanly.
    if (sFairyPosValid) {
        p->actor.world.pos = sFairyPos;
    }

    hgTimer++;

    if (hgTimer == 1) {
        // Restore Link visibility
        p->actor.draw = Player_Draw;

        // Re-enable colliders
        p->cylinder.base.atFlags |= AT_ON;
        p->cylinder.base.acFlags |= AC_ON;
        p->cylinder.base.ocFlags1 |= OC1_ON;

        // Snap to floor if close enough — also update sFairyPos so subsequent
        // frame restores use the floor-corrected Y position.
        CollisionPoly* floorPoly = NULL;
        s32 bgId;
        f32 floor = BgCheck_EntityRaycastFloor5(play, &play->colCtx, &floorPoly, &bgId, &p->actor, &p->actor.world.pos);
        if (floor > BGCHECK_Y_MIN && (p->actor.world.pos.y - floor) < 100.0f) {
            p->actor.world.pos.y = floor;
            sFairyPos.y = floor;
            p->actor.bgCheckFlags |= BGCHECKFLAG_GROUND;
        }

        HGrace_SpawnWarpSparkles(play, &p->actor.world.pos);
        Audio_PlaySoundGeneral(NA_SE_PL_MAGIC_WIND_WARP, &p->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        func_800AA000(200.0f, 150, 20, 80);
    }

    // Brief exit transition
    if (hgTimer >= HGRACE_WARP_OUT_DURATION) {
        p->stateFlags1 &= ~(PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_INPUT_DISABLED);
        p->invincibilityTimer = 20;
        func_8005B1A4(Play_GetCamera(play, 0));

        // Reset SW97 spirit mode effects
        if (hgForcedBySpell) {
            HGrace_ResetLighting(play);
            sFairyDimLevel = 0.0f;
            p->stateFlags2 &= ~0x100000; // restore Navi
        }

        sFairyPosValid = 0;
        hgActive = 0;
        hgState = HGRACE_STATE_IDLE;
        hgForcedBySpell = 0;
        sFairyVelocity.x = sFairyVelocity.y = sFairyVelocity.z = 0.0f;
        // No cooldown
    }
}

// =============================================================================
// State: Ivan Possess (Spirit Medallion — real EnPartner controlled by P1)
// =============================================================================

static void HGrace_StateIvan(Player* p, PlayState* play) {
    extern s16 gEnPartnerId;

    // First frame: spawn Ivan at Link's position, hide Link
    if (sIvanActor == NULL) {
        sIvanActor = Actor_Spawn(&play->actorCtx, play, gEnPartnerId, p->actor.world.pos.x,
                                 p->actor.world.pos.y + Player_GetHeight(p) + 5.0f, p->actor.world.pos.z, 0, 0, 0, 0,
                                 true); // params=0 → reads input[0] (Player 1)
        gIvanPossessActive = 1;
        p->actor.draw = NULL;

        // Flash + sound on enter
        func_800AA000(200.0f, 150, 20, 80);
        Audio_PlayActorSound2(&p->actor, NA_SE_EV_TRIFORCE_FLASH);
    }

    // Every frame: Link follows Ivan (invisible but present for camera + damage)
    p->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
    p->linearVelocity = 0.0f;
    p->actor.speedXZ = 0.0f;
    p->actor.velocity.x = p->actor.velocity.y = p->actor.velocity.z = 0.0f;
    p->actor.draw = NULL; // Invisible

    // Sync Link's position to Ivan so camera follows and Link can take damage
    if (sIvanActor != NULL) {
        p->actor.world.pos = sIvanActor->world.pos;
        p->actor.focus.pos.x = sIvanActor->world.pos.x;
        p->actor.focus.pos.y = sIvanActor->world.pos.y;
        p->actor.focus.pos.z = sIvanActor->world.pos.z;
    }

    // If Ivan was killed externally (scene change, etc.), restore Link
    if (sIvanActor != NULL && sIvanActor->update == NULL) {
        sIvanActor = NULL;
    }

    // Toggle off: Spirit Medallion button pressed again (debounce: hgTimer > 10)
    u16 spiritBtn = ItemInput_GetEquippedButton(ITEM_MEDALLION_SPIRIT, play);
    u8 toggleOff = (spiritBtn && (play->state.input[0].press.button & spiritBtn) && hgTimer > 10);

    // Also toggle off if Ivan died
    if (sIvanActor == NULL && hgTimer > 10)
        toggleOff = 1;

    if (toggleOff) {
        // Kill Ivan if still alive
        if (sIvanActor != NULL) {
            Actor_Kill(sIvanActor);
            sIvanActor = NULL;
        }
        gIvanPossessActive = 0;

        // Restore Link
        p->actor.draw = Player_Draw;
        p->stateFlags1 &= ~(PLAYER_STATE1_INPUT_DISABLED | PLAYER_STATE1_IN_ITEM_CS);
        p->invincibilityTimer = 20;

        // Flash + sound
        func_800AA000(200.0f, 150, 20, 80);
        Audio_PlayActorSound2(&p->actor, NA_SE_EV_TRIFORCE_FLASH);

        // Reset lighting (spirit mode dims)
        HGrace_ResetLighting(play);
        sFairyDimLevel = 0.0f;
        p->stateFlags2 &= ~0x100000; // restore Navi

        hgActive = 0;
        hgState = HGRACE_STATE_IDLE;
        hgForcedBySpell = 0;
        return;
    }

    hgTimer++;
}

// =============================================================================
// Public API
// =============================================================================

void Handle_HyliasGrace(Player* p, PlayState* play) {
    if (!hgForcedBySpell) {
        // Normal path: item-based activation (Hylia's Grace on C-button)
        ItemInputState in;
        ItemInput_Update(&in, ITEM_HYLIAS_GRACE, p, play);

        if (!in.wasEquipped) {
            if (hgActive)
                HGrace_Stop(p, play);
            return;
        }
        if (ItemInput_CheckDamage(p, &sHGracePrevInvinc)) {
            HGrace_Stop(p, play);
            return;
        }

        // Cannot use in water
        if (p->stateFlags1 & PLAYER_STATE1_IN_WATER)
            return;

        if (!hgActive) {
            // Only block on otherButtonPressed when NOT active.
            // During fairy mode, A/B are used for ascend/descend — must not cancel spell.
            if (in.otherButtonPressed)
                return;
            if (ItemInput_IsBlocked(p, play))
                return;
            // No cooldown - can use immediately after previous use
            if (in.isPressed)
                HGrace_Start(p, play);
            return;
        }

        // Toggle off: press Hylia's Grace again during fairy flight → warp-out
        if (hgState == HGRACE_STATE_FAIRY && in.isPressed) {
            hgState = HGRACE_STATE_WARP_OUT;
            hgTimer = 0;
            sFairyVelocity.x = sFairyVelocity.y = sFairyVelocity.z = 0.0f;
        }
    } else {
        // Forced by spell (Spirit Medallion) — skip item-equip checks, still check damage/water
        if (ItemInput_CheckDamage(p, &sHGracePrevInvinc)) {
            HGrace_Stop(p, play);
            return;
        }
        if (p->stateFlags1 & PLAYER_STATE1_IN_WATER) {
            HGrace_Stop(p, play);
            return;
        }
    }

    switch (hgState) {
        case HGRACE_STATE_CASTING:
            HGrace_StateCasting(p, play);
            break;
        case HGRACE_STATE_WARP_IN:
            HGrace_StateWarpEnter(p, play);
            break;
        case HGRACE_STATE_FAIRY:
            HGrace_StateFairy(p, play);
            break;
        case HGRACE_STATE_WARP_OUT:
            HGrace_StateWarpExit(p, play);
            break;
        case HGRACE_STATE_IVAN:
            HGrace_StateIvan(p, play);
            break;
        default:
            HGrace_Stop(p, play);
            break;
    }
}

void Player_InitHyliasGraceIA(PlayState* play, Player* p) {
    if (hgActive)
        return;
    hgState = HGRACE_STATE_IDLE;
    hgSubPhase = 0;
    hgTimer = 0;
    hgFairy = NULL;
}

s32 Player_UpperAction_HyliasGrace(Player* p, PlayState* play) {
    return 0;
}
