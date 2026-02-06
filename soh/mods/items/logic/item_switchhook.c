/**
 * item_switchhook.c - Switch Hook from Oracle of Ages
 *
 * Controls (hold-to-aim like Bomb Arrows):
 *   Hold C Button:   First-person aiming mode
 *   Release C:       Fire hook projectile
 *   Z-targeting:     Third-person aiming at target
 *
 * Features:
 *   - Swaps positions with swappable actors (pots, crates, certain enemies)
 *   - Deals hookshot damage to non-swappable actors and bounces back
 *   - Uses targeted actor (Z-target) for instant swap when available
 *   - Longshot distance (26 frames)
 *   - Usable by both child and adult Link
 *   - Hook tip rotated 180 degrees (reversed hookshot appearance)
 *   - Blue reticle during aiming (like Gust Jar suck mode)
 */

#include "z64.h"
#include "../custom_items.h"
#include "../helpers/camera_helper.h"
#include "../helpers/equip_helper.h"
#include "../helpers/fx_helper.h"
#include "item_switchhook.h"
#include "macros.h"
#include "functions.h"
#include "objects/object_link_boy/object_link_boy.h"
#include "../objects/switchhook_giveDL/header.h"

// ============================================================================
// STATIC VARIABLES
// ============================================================================

static u8 sColliderInited = 0;
static Vec3f sProjVel;
static Vec3f sZeroVec = { 0.0f, 0.0f, 0.0f };
static s8 sSwitchHookPrevInvinc = 0;
static s32 sShAnimState = -1;

// Use existing function from z_player.c
extern int Player_IsZTargeting(Player* this);

// Forward declarations
static void SwitchHook_FireHook(Player* p, PlayState* play);
static void SwitchHook_Stop(Player* p, PlayState* play);

// ============================================================================
// STOP - Clean up all state
// ============================================================================

static void SwitchHook_Stop(Player* p, PlayState* play) {
    // Exit first-person mode
    if (shFirstPerson) {
        FirstPerson_Exit(p, play);
        shFirstPerson = 0;
    }

    shActive = 0;
    shState = SWITCHHOOK_STATE_IDLE;
    shTarget = NULL;

    Audio_StopSfxById(NA_SE_IT_HOOKSHOT_CHAIN);
    ItemEquip_PlayUnequipSFX(play, p);
}

// ============================================================================
// GET AIM DIRECTION
// ============================================================================

static s16 SwitchHook_GetAimYaw(Player* p, PlayState* play) {
    if (shFirstPerson)
        return FirstPerson_GetAimYaw(p);
    if (Player_IsZTargeting(p) && p->focusActor != NULL)
        return Math_Vec3f_Yaw(&p->actor.world.pos, &p->focusActor->focus.pos);
    return p->actor.shape.rot.y;
}

static s16 SwitchHook_GetAimPitch(Player* p) {
    return shFirstPerson ? FirstPerson_GetAimPitch(p) : 0;
}

// ============================================================================
// START AIMING - Enter first-person mode (called on button press)
// Uses custom first-person that avoids slingshot display
// ============================================================================

static void SwitchHook_StartAiming(Player* p, PlayState* play) {
    // Guard against double-activation (like Beetle)
    if (shActive)
        return;

    // Initialize collider if needed
    if (!sColliderInited) {
        Collider_InitQuad(play, &shCollider);
        Collider_SetQuad(play, &shCollider, &p->actor, &sSwitchHookQuadInit);
        sColliderInited = 1;
    }

    shActive = 1;
    shState = SWITCHHOOK_STATE_AIMING;
    shFirstPerson = 1;
    shTarget = NULL;

    // Use boomerang animation like Beetle (hookshot animation might interfere)
    LinkAnimation_PlayLoop(play, &p->upperSkelAnime, &gPlayerAnim_link_boom_throw_waitR);

    // Enter first-person mode (exactly like Beetle)
    FirstPerson_Init(p, play);

    ItemEquip_PlayEquipSFX(play, p);
}

// ============================================================================
// UPDATE AIMING - Handle aiming state (follows BombArrows pattern exactly)
// ============================================================================

static void SwitchHook_UpdateAiming(Player* p, PlayState* play, ItemInputState* in) {
    u8 isZTargeting;

    // Handle Z-targeting transitions
    isZTargeting = Player_IsZTargeting(p);
    if (shFirstPerson && isZTargeting) {
        FirstPerson_Exit(p, play);
        shFirstPerson = 0;
    } else if (!shFirstPerson && !isZTargeting) {
        FirstPerson_Init(p, play);
        shFirstPerson = 1;
    }

    // Keep first-person updated
    if (shFirstPerson) {
        FirstPerson_Update(p, play);
    }

    // Fire hook when button released
    if (!in->isHeld) {
        SwitchHook_FireHook(p, play);
        return;
    }

    // Cancel with B or other button
    if (CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B) || in->otherButtonPressed) {
        SwitchHook_Stop(p, play);
        return;
    }
}

// ============================================================================
// FIRE HOOK - Launch the projectile (called on button release)
// ============================================================================

static void SwitchHook_FireHook(Player* p, PlayState* play) {
    s16 aimYaw;
    s16 aimPitch;
    f32 cosPitch;

    // Get aim direction BEFORE exiting first-person
    aimYaw = SwitchHook_GetAimYaw(p, play);
    aimPitch = SwitchHook_GetAimPitch(p);

    // Exit first-person after getting aim direction
    if (shFirstPerson) {
        FirstPerson_Exit(p, play);
        shFirstPerson = 0;
    }

    // If Z-targeting a swappable actor, do instant swap
    if (Player_IsZTargeting(p) && p->focusActor != NULL) {
        aimYaw = Math_Vec3f_Yaw(&p->actor.world.pos, &p->focusActor->focus.pos);
        aimPitch = Math_Vec3f_Pitch(&p->actor.world.pos, &p->focusActor->focus.pos);

        if (SwitchHook_CanSwap(p->focusActor)) {
            shTarget = p->focusActor;
            Math_Vec3f_Copy(&shLinkStartPos, &p->actor.world.pos);
            Math_Vec3f_Copy(&shTargetStartPos, &shTarget->world.pos);
            shSwapTimer = 0;
            shVortexTimer = 0;
            shState = SWITCHHOOK_STATE_HIT_SWAP;
            Audio_PlaySoundGeneral(NA_SE_EV_WARP_HOLE, &p->actor.projectedPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            Player_PlaySfx(p, NA_SE_IT_HOOKSHOT_CHAIN);
            return;
        }
    }

    // Start position at player's hand
    shProjPos.x = p->unk_3C8.x;
    shProjPos.y = p->unk_3C8.y;
    shProjPos.z = p->unk_3C8.z;

    // Fallback if hand position is zero
    if (shProjPos.x == 0.0f && shProjPos.y == 0.0f && shProjPos.z == 0.0f) {
        shProjPos.x = p->actor.world.pos.x;
        shProjPos.y = p->actor.world.pos.y + 40.0f;
        shProjPos.z = p->actor.world.pos.z;
    }

    shProjYaw = aimYaw;
    shProjPitch = aimPitch;

    // Calculate velocity (no gravity for straight flight like hookshot)
    cosPitch = Math_CosS(aimPitch);
    sProjVel.x = SWITCHHOOK_SPEED * Math_SinS(aimYaw) * cosPitch;
    sProjVel.y = -SWITCHHOOK_SPEED * Math_SinS(aimPitch);
    sProjVel.z = SWITCHHOOK_SPEED * Math_CosS(aimYaw) * cosPitch;

    // Set timer (longshot distance)
    shTimer = SWITCHHOOK_TIMER;
    shTarget = NULL;
    shState = SWITCHHOOK_STATE_SHOOTING;

    // Play hookshot fire sound
    Player_PlaySfx(p, NA_SE_IT_HOOKSHOT_CHAIN);
}

// ============================================================================
// FIND NEARBY SWITCHABLE ACTOR - Scout ahead for switchable targets
// ============================================================================

static Actor* SwitchHook_FindNearbySwappable(PlayState* play, Vec3f* scoutPos, f32 detectRadius) {
    Actor* actor;
    f32 distWorld;
    f32 distFocus;
    f32 dx;
    f32 dy;
    f32 dz;
    s32 category;

    // Check all actor categories
    for (category = 0; category < ACTORCAT_MAX; category++) {
        actor = play->actorCtx.actorLists[category].head;
        while (actor != NULL) {
            if (actor->update != NULL && SwitchHook_CanSwap(actor)) {
                // Check distance to world.pos
                dx = actor->world.pos.x - scoutPos->x;
                dy = actor->world.pos.y - scoutPos->y;
                dz = actor->world.pos.z - scoutPos->z;
                distWorld = sqrtf(SQ(dx) + SQ(dy) + SQ(dz));

                // Also check distance to focus.pos (Z-target point)
                dx = actor->focus.pos.x - scoutPos->x;
                dy = actor->focus.pos.y - scoutPos->y;
                dz = actor->focus.pos.z - scoutPos->z;
                distFocus = sqrtf(SQ(dx) + SQ(dy) + SQ(dz));

                // Use the closer of the two
                if (distWorld < detectRadius || distFocus < detectRadius) {
                    return actor;
                }
            }
            actor = actor->next;
        }
    }
    return NULL;
}

// ============================================================================
// UPDATE PROJECTILE - Move and check for collisions
// ============================================================================

static void SwitchHook_UpdateProjectile(Player* p, PlayState* play) {
    Vec3f prevPos;
    Vec3f newPos;
    Vec3f scoutPos;
    CollisionPoly* poly;
    s32 bgId;
    Vec3f quadVerts[4];
    f32 halfWidth = 15.0f;
    f32 halfHeight = 15.0f;
    f32 perpX;
    f32 perpZ;
    f32 scoutDist = 10.0f;
    f32 detectRadius = 30.0f;
    Actor* swappableActor;

    Math_Vec3f_Copy(&prevPos, &shProjPos);

    // Move projectile (straight line, no gravity)
    shProjPos.x += sProjVel.x;
    shProjPos.y += sProjVel.y;
    shProjPos.z += sProjVel.z;

    // Calculate scout position (ahead of projectile)
    scoutPos.x = shProjPos.x + (sProjVel.x * scoutDist / SWITCHHOOK_SPEED);
    scoutPos.y = shProjPos.y + (sProjVel.y * scoutDist / SWITCHHOOK_SPEED);
    scoutPos.z = shProjPos.z + (sProjVel.z * scoutDist / SWITCHHOOK_SPEED);

    // Scout ahead for switchable actors
    swappableActor = SwitchHook_FindNearbySwappable(play, &scoutPos, detectRadius);
    if (swappableActor != NULL) {
        // Found a switchable actor - perform swap
        shTarget = swappableActor;
        Math_Vec3f_Copy(&shLinkStartPos, &p->actor.world.pos);
        Math_Vec3f_Copy(&shTargetStartPos, &swappableActor->world.pos);
        shSwapTimer = 0;
        shVortexTimer = 0;
        shState = SWITCHHOOK_STATE_HIT_SWAP;
        Audio_PlaySoundGeneral(NA_SE_EV_WARP_HOLE, &shProjPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }

    // Update collider quad for damage (only hits non-switchable actors)
    perpX = Math_CosS(shProjYaw) * halfWidth;
    perpZ = -Math_SinS(shProjYaw) * halfWidth;

    quadVerts[0].x = shProjPos.x - perpX;
    quadVerts[0].y = shProjPos.y + halfHeight;
    quadVerts[0].z = shProjPos.z - perpZ;

    quadVerts[1].x = shProjPos.x + perpX;
    quadVerts[1].y = shProjPos.y + halfHeight;
    quadVerts[1].z = shProjPos.z + perpZ;

    quadVerts[2].x = shProjPos.x + perpX;
    quadVerts[2].y = shProjPos.y - halfHeight;
    quadVerts[2].z = shProjPos.z + perpZ;

    quadVerts[3].x = shProjPos.x - perpX;
    quadVerts[3].y = shProjPos.y - halfHeight;
    quadVerts[3].z = shProjPos.z - perpZ;

    Collider_SetQuadVertices(&shCollider, &quadVerts[0], &quadVerts[1], &quadVerts[2], &quadVerts[3]);
    CollisionCheck_SetAT(play, &play->colChkCtx, &shCollider.base);

    // Check for non-switchable actor collision (damage + bounce)
    if (shCollider.base.atFlags & AT_HIT) {
        shState = SWITCHHOOK_STATE_HIT_DAMAGE;
        Audio_PlaySoundGeneral(NA_SE_IT_HOOKSHOT_REFLECT, &shProjPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        shCollider.base.atFlags &= ~AT_HIT;
        return;
    }

    // Check for wall/floor collision
    if (BgCheck_EntityLineTest1(&play->colCtx, &prevPos, &shProjPos, &newPos, &poly, true, true, true, true, &bgId)) {
        Math_Vec3f_Copy(&shProjPos, &newPos);
        shState = SWITCHHOOK_STATE_RETRACT;
        Audio_PlaySoundGeneral(NA_SE_IT_HOOKSHOT_REFLECT, &shProjPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        CollisionCheck_SpawnShieldParticlesMetal(play, &shProjPos);
        return;
    }

    // Timer expired - retract
    if (--shTimer <= 0) {
        shState = SWITCHHOOK_STATE_RETRACT;
    }

    // Chain sound while shooting
    func_8002F8F0(&p->actor, NA_SE_IT_HOOKSHOT_CHAIN - SFX_FLAG);
}

// ============================================================================
// PERFORM SWAP - Animate position exchange
// ============================================================================

static void SwitchHook_PerformSwap(Player* p, PlayState* play) {
    f32 t;
    f32 easeT;
    Vec3f linkNewPos;
    Vec3f targetNewPos;
    Color_RGBA8 vortexColor = { 100, 200, 255, 255 };

    if (shTarget == NULL || shTarget->update == NULL) {
        shState = SWITCHHOOK_STATE_IDLE;
        shActive = 0;
        Audio_StopSfxById(NA_SE_IT_HOOKSHOT_CHAIN);
        return;
    }

    shSwapTimer++;
    shVortexTimer++;

    // Make Link invulnerable during swap and zero velocity
    p->invincibilityTimer = 10;
    p->actor.velocity.x = 0.0f;
    p->actor.velocity.y = 0.0f;
    p->actor.velocity.z = 0.0f;
    p->linearVelocity = 0.0f;

    // Prevent collision updates during swap
    p->actor.bgCheckFlags = 0;

    t = (f32)shSwapTimer / (f32)SWITCHHOOK_SWAP_FRAMES;
    if (t > 1.0f)
        t = 1.0f;

    // Smooth easing
    easeT = t * t * (3.0f - 2.0f * t);

    linkNewPos.x = shLinkStartPos.x + (shTargetStartPos.x - shLinkStartPos.x) * easeT;
    linkNewPos.y = shLinkStartPos.y + (shTargetStartPos.y - shLinkStartPos.y) * easeT;
    linkNewPos.z = shLinkStartPos.z + (shTargetStartPos.z - shLinkStartPos.z) * easeT;

    targetNewPos.x = shTargetStartPos.x + (shLinkStartPos.x - shTargetStartPos.x) * easeT;
    targetNewPos.y = shTargetStartPos.y + (shLinkStartPos.y - shTargetStartPos.y) * easeT;
    targetNewPos.z = shTargetStartPos.z + (shLinkStartPos.z - shTargetStartPos.z) * easeT;

    // Set position directly (bypass collision) - set all position fields
    p->actor.world.pos.x = linkNewPos.x;
    p->actor.world.pos.y = linkNewPos.y;
    p->actor.world.pos.z = linkNewPos.z;
    p->actor.prevPos.x = linkNewPos.x;
    p->actor.prevPos.y = linkNewPos.y;
    p->actor.prevPos.z = linkNewPos.z;
    p->actor.home.pos.x = linkNewPos.x;
    p->actor.home.pos.y = linkNewPos.y;
    p->actor.home.pos.z = linkNewPos.z;

    if (shTarget != NULL && shTarget->update != NULL) {
        shTarget->world.pos.x = targetNewPos.x;
        shTarget->world.pos.y = targetNewPos.y;
        shTarget->world.pos.z = targetNewPos.z;
        shTarget->prevPos.x = targetNewPos.x;
        shTarget->prevPos.y = targetNewPos.y;
        shTarget->prevPos.z = targetNewPos.z;
        shTarget->velocity.x = 0.0f;
        shTarget->velocity.y = 0.0f;
        shTarget->velocity.z = 0.0f;
        shTarget->bgCheckFlags = 0;
    }

    // Spawn cyan vortex particles
    if ((shVortexTimer % 3) == 0) {
        EffectSsKiraKira_SpawnDispersed(play, &linkNewPos, &sZeroVec, &sZeroVec, &vortexColor, &vortexColor, 2000, 20);
        EffectSsKiraKira_SpawnDispersed(play, &targetNewPos, &sZeroVec, &sZeroVec, &vortexColor, &vortexColor, 2000,
                                        20);
    }

    if (shSwapTimer >= SWITCHHOOK_SWAP_FRAMES) {
        // Force final position (bypass collision completely)
        p->actor.world.pos.x = shTargetStartPos.x;
        p->actor.world.pos.y = shTargetStartPos.y;
        p->actor.world.pos.z = shTargetStartPos.z;
        p->actor.prevPos.x = shTargetStartPos.x;
        p->actor.prevPos.y = shTargetStartPos.y;
        p->actor.prevPos.z = shTargetStartPos.z;
        p->actor.home.pos.x = shTargetStartPos.x;
        p->actor.home.pos.y = shTargetStartPos.y;
        p->actor.home.pos.z = shTargetStartPos.z;
        p->actor.velocity.x = 0.0f;
        p->actor.velocity.y = 0.0f;
        p->actor.velocity.z = 0.0f;
        p->linearVelocity = 0.0f;

        if (shTarget != NULL && shTarget->update != NULL) {
            shTarget->world.pos.x = shLinkStartPos.x;
            shTarget->world.pos.y = shLinkStartPos.y;
            shTarget->world.pos.z = shLinkStartPos.z;
            shTarget->prevPos.x = shLinkStartPos.x;
            shTarget->prevPos.y = shLinkStartPos.y;
            shTarget->prevPos.z = shLinkStartPos.z;
            shTarget->home.pos.x = shLinkStartPos.x;
            shTarget->home.pos.y = shLinkStartPos.y;
            shTarget->home.pos.z = shLinkStartPos.z;
            shTarget->velocity.x = 0.0f;
            shTarget->velocity.y = 0.0f;
            shTarget->velocity.z = 0.0f;
        }

        Audio_PlaySoundGeneral(NA_SE_EV_ROLL_STAND, &p->actor.projectedPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);

        Audio_StopSfxById(NA_SE_IT_HOOKSHOT_CHAIN);

        shTarget = NULL;
        shState = SWITCHHOOK_STATE_IDLE;
        shActive = 0;
    }
}

// ============================================================================
// RETRACT - Pull hook back to player
// ============================================================================

static void SwitchHook_Retract(Player* p, PlayState* play) {
    Vec3f handPos;
    Vec3f toPlayer;
    f32 dist;
    f32 speed;
    f32 invDist;

    handPos = p->unk_3C8;

    if (handPos.x == 0.0f && handPos.y == 0.0f && handPos.z == 0.0f) {
        handPos.x = p->actor.world.pos.x;
        handPos.y = p->actor.world.pos.y + 40.0f;
        handPos.z = p->actor.world.pos.z;
    }

    toPlayer.x = handPos.x - shProjPos.x;
    toPlayer.y = handPos.y - shProjPos.y;
    toPlayer.z = handPos.z - shProjPos.z;

    dist = sqrtf(SQ(toPlayer.x) + SQ(toPlayer.y) + SQ(toPlayer.z));

    if (dist < 30.0f) {
        shState = SWITCHHOOK_STATE_IDLE;
        shActive = 0;
        Audio_StopSfxById(NA_SE_IT_HOOKSHOT_CHAIN);
        return;
    }

    speed = 30.0f;
    invDist = speed / dist;

    shProjPos.x += toPlayer.x * invDist;
    shProjPos.y += toPlayer.y * invDist;
    shProjPos.z += toPlayer.z * invDist;
}

// ============================================================================
// MAIN HANDLER - Following BombArrows pattern exactly
// ============================================================================

void Handle_SwitchHook(Player* p, PlayState* play) {
    ItemInputState in;
    ItemInput_Update(&in, ITEM_SWITCH_HOOK, p, play);
    shButtonMask = in.equippedButton;

    if (!in.wasEquipped) {
        if (shActive)
            SwitchHook_Stop(p, play);
        return;
    }

    // Like Beetle: if not active, check for activation and return early
    // This avoids the blocked check for initial activation
    if (!shActive) {
        if (in.isPressed)
            SwitchHook_StartAiming(p, play);
        return;
    }

    // Check blocked/damage only when active (not during swap)
    if (shState != SWITCHHOOK_STATE_HIT_SWAP) {
        if (ItemInput_IsBlocked(p, play)) {
            SwitchHook_Stop(p, play);
            return;
        }

        if (ItemInput_CheckDamage(p, &sSwitchHookPrevInvinc)) {
            SwitchHook_Stop(p, play);
            return;
        }
    }

    switch (shState) {
        case SWITCHHOOK_STATE_AIMING:
            SwitchHook_UpdateAiming(p, play, &in);
            break;
        case SWITCHHOOK_STATE_SHOOTING:
            SwitchHook_UpdateProjectile(p, play);
            break;
        case SWITCHHOOK_STATE_HIT_SWAP:
            SwitchHook_PerformSwap(p, play);
            break;
        case SWITCHHOOK_STATE_HIT_DAMAGE:
        case SWITCHHOOK_STATE_RETRACT:
            SwitchHook_Retract(p, play);
            break;
        default:
            shState = SWITCHHOOK_STATE_IDLE;
            break;
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void Player_InitSwitchHookIA(PlayState* play, Player* p) {
    shActive = 0;
    shState = SWITCHHOOK_STATE_IDLE;
    shTarget = NULL;
    shButtonMask = 0;
    shFirstPerson = 0;
    sColliderInited = 0;
    sShAnimState = -1;
    p->stateFlags1 |= PLAYER_STATE1_ITEM_IN_HAND;
}

// ============================================================================
// UPPER ACTION - Following BombArrows pattern exactly
// ============================================================================

s32 Player_UpperAction_SwitchHook(Player* this, PlayState* play) {
    // Idle: let lower body control everything
    if (!shActive) {
        sShAnimState = -1;
        return 0;
    }

    // Detect state transitions and start appropriate animation
    if ((s32)shState != sShAnimState) {
        sShAnimState = shState;
        switch (shState) {
            case SWITCHHOOK_STATE_AIMING:
                LinkAnimation_PlayOnce(play, &this->upperSkelAnime, &gPlayerAnim_link_hook_shot_ready);
                break;
            case SWITCHHOOK_STATE_SHOOTING:
            case SWITCHHOOK_STATE_HIT_DAMAGE:
            case SWITCHHOOK_STATE_RETRACT:
                LinkAnimation_PlayOnce(play, &this->upperSkelAnime, &gPlayerAnim_link_hook_shot_ready);
                break;
            case SWITCHHOOK_STATE_HIT_SWAP:
                // Keep current animation during swap
                break;
        }
    }

    // Advance animation and handle transitions when finished
    if (LinkAnimation_Update(play, &this->upperSkelAnime)) {
        switch (shState) {
            case SWITCHHOOK_STATE_AIMING:
                // Hold the ready pose while aiming (don't restart)
                break;
            case SWITCHHOOK_STATE_SHOOTING:
            case SWITCHHOOK_STATE_HIT_DAMAGE:
            case SWITCHHOOK_STATE_RETRACT:
                // Hold pose while hook is out
                break;
            default:
                break;
        }
    }

    return 1;
}

// ============================================================================
// DRAW SWITCHHOOK IN LINK'S HAND - Uses vanilla hookshot DL
// ============================================================================

void CustomItems_DrawSwitchHookInHand(Player* player, PlayState* play) {
    Vec3f handPos;
    s16 handYaw;

    // Only draw when active and in aiming state
    if (!shActive)
        return;
    if (shState != SWITCHHOOK_STATE_AIMING)
        return;

    // Get hand position (right hand for hookshot-style items)
    handPos = player->bodyPartsPos[PLAYER_BODYPART_R_HAND];

    // Offset forward from hand
    handYaw = player->actor.shape.rot.y;
    handPos.x += Math_SinS(handYaw) * 8.0f;
    handPos.z += Math_CosS(handYaw) * 8.0f;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    // Purple/cyan tint for switch hook (distinguishes from regular hookshot)
    gDPSetEnvColor(POLY_OPA_DISP++, 100, 180, 220, 255);

    // Position and rotate - vanilla hookshot scale
    Matrix_Translate(handPos.x, handPos.y, handPos.z, MTXMODE_NEW);
    Matrix_RotateY(handYaw * (M_PI / 32768.0f), MTXMODE_APPLY);
    Matrix_RotateX(-M_PI / 4.0f, MTXMODE_APPLY); // Angle forward
    Matrix_RotateY(M_PI, MTXMODE_APPLY);         // Flip 180 degrees (switched appearance)
    Matrix_Scale(0.01f, 0.01f, 0.01f, MTXMODE_APPLY);

    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, gLinkAdultHookshotTipDL);

    CLOSE_DISPS(play->state.gfxCtx);
}

// ============================================================================
// DRAW HOOKSHOT AND CHAIN
// ============================================================================

void CustomItems_DrawSwitchHook(Player* player, PlayState* play) {
    Vec3f handPos;
    Vec3f chainStart;
    Vec3f chainEnd;
    Vec3f chainDir;
    f32 chainLen;
    f32 distXZ;

    // Only draw chain/hook when shooting, retracting, or swapping
    if (!shActive)
        return;
    if (shState == SWITCHHOOK_STATE_IDLE || shState == SWITCHHOOK_STATE_AIMING)
        return;

    // Get hand position
    handPos = player->unk_3C8;
    if (handPos.x == 0.0f && handPos.y == 0.0f && handPos.z == 0.0f) {
        handPos.x = player->actor.world.pos.x;
        handPos.y = player->actor.world.pos.y + 40.0f;
        handPos.z = player->actor.world.pos.z;
    }

    // Determine chain endpoints
    if (shState == SWITCHHOOK_STATE_HIT_SWAP && shTarget != NULL) {
        chainStart = player->actor.world.pos;
        chainStart.y += 40.0f;
        chainEnd = shTarget->world.pos;
        chainEnd.y += 20.0f;
    } else {
        chainStart = handPos;
        chainEnd = shProjPos;
    }

    // Calculate chain direction
    chainDir.x = chainEnd.x - chainStart.x;
    chainDir.y = chainEnd.y - chainStart.y;
    chainDir.z = chainEnd.z - chainStart.z;

    chainLen = sqrtf(SQ(chainDir.x) + SQ(chainDir.y) + SQ(chainDir.z));
    if (chainLen < 1.0f)
        return;

    distXZ = sqrtf(SQ(chainDir.x) + SQ(chainDir.z));

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    // Purple tint for hook
    gDPSetEnvColor(POLY_OPA_DISP++, 180, 80, 220, 255);

    // Draw hook tip at end (rotated 180 degrees)
    Matrix_Translate(chainEnd.x, chainEnd.y, chainEnd.z, MTXMODE_NEW);
    Matrix_RotateY(Math_FAtan2F(chainDir.x, chainDir.z), MTXMODE_APPLY);
    Matrix_RotateX(Math_FAtan2F(-chainDir.y, distXZ), MTXMODE_APPLY);
    Matrix_RotateY(M_PI, MTXMODE_APPLY);
    Matrix_Scale(0.01f, 0.01f, 0.01f, MTXMODE_APPLY);

    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, gLinkAdultHookshotTipDL);

    // Draw chain from tip toward hand
    Matrix_Translate(chainEnd.x, chainEnd.y, chainEnd.z, MTXMODE_NEW);
    Matrix_RotateY(Math_FAtan2F(-chainDir.x, -chainDir.z), MTXMODE_APPLY);
    Matrix_RotateX(Math_FAtan2F(chainDir.y, distXZ), MTXMODE_APPLY);
    Matrix_Scale(0.015f, 0.015f, chainLen * 0.01f, MTXMODE_APPLY);

    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, gLinkAdultHookshotChainDL);

    CLOSE_DISPS(play->state.gfxCtx);
}

// ============================================================================
// DRAW RETICLE - Blue reticle during aiming (like Gust Jar suck mode)
// ============================================================================

void CustomItems_DrawSwitchHookReticle(Player* player, PlayState* play) {
    if (!shFirstPerson || shState != SWITCHHOOK_STATE_AIMING)
        return;

    // Blue reticle (0, 100, 255) like Gust Jar suck mode
    FirstPerson_DrawReticle(player, play, 0.0f, 0, 100, 255);
}
