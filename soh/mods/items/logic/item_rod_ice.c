/**
 * item_rod_ice.c - Ice Rod from A Link Between Worlds
 *
 * Controls:
 *   B Button:  Swing rod (uses sword mechanics)
 *   C-UP:      Toggle first-person aiming mode
 *
 * Attack Types:
 *   - Slash: 3 iceballs spread at +30/0/-30 degrees
 *   - Stab: Single long-range iceball
 *   - Jump Slash: Ice wave cone (6 colliders)
 *   - Spin Attack: Expanding ice cylinder
 *
 * Special: Freezes enemies on hit, backfire freezes Link
 */

#include "item_rod_ice.h"
#include "../helpers/equip_helper.h"
#include "../helpers/combat_helper.h"
#include "../helpers/fx_helper.h"
#include "../helpers/camera_helper.h"
#include "overlays/actors/ovl_Bg_Ice_Shelter/z_bg_ice_shelter.h"

static ItemEquipState sIceEquipState = { 0 };
static s8 sIcePrevInvinc = 0;
static u8 sIceLastSwingType = 0;
static u8 sIceJumpEffectSpawned = 0;
static u8 sIceChargeButtonHeld = 0;
static s16 sIceChargeHoldCounter = 0;
static f32 sIceSpinExpandProgress = 0.0f;
static u8 sIceSpinColliderInited = 0;
static u8 sIceWaveCollidersInited = 0;

// Multi-set projectile system (5 concurrent sets)
static RodProjSet sIceProjSets[ROD_MAX_PROJ_SETS];

static RodColor sIceRodColor = { ICE_ROD_PRIM_R, ICE_ROD_PRIM_G, ICE_ROD_PRIM_B, ICE_ROD_PRIM_A,
                                 ICE_ROD_ENV_R,  ICE_ROD_ENV_G,  ICE_ROD_ENV_B,  ICE_ROD_ENV_A };

extern int Player_IsZTargeting(Player* this);
extern void func_80837948(PlayState* play, Player* player, s32 meleeWeaponAnim);

// Aliases for wave/beam system (unchanged)
#define iceRodWaveActive gCustomItemState.iceRodWaveActive
#define iceRodWaveTimer gCustomItemState.iceRodWaveTimer
#define iceRodWavePos gCustomItemState.iceRodWavePos
#define iceRodWaveColliders gCustomItemState.iceRodWaveColliders

// =============================================================================
// BACKFIRE - Freezes Link when using ice rod without magic
// =============================================================================

// func_80837C0C: Handles player hit response including freeze
extern void func_80837C0C(PlayState* play, Player* this, s32 hitResponse, f32 damageSpeed, f32 damageRot,
                          s16 damageRotType, s32 invincibility);

static void IceRod_Backfire(Player* p, PlayState* play) {
    Audio_PlayActorSound2(&p->actor, ICE_ROD_SFX_BACKFIRE_HIT);
    Audio_PlayActorSound2(&p->actor, NA_SE_VO_LI_FREEZE);

    // Freeze Link using the ice trap response (PLAYER_HIT_RESPONSE_ICE_TRAP = 3)
    func_80837C0C(play, p, 3, 0.0f, 0.0f, 0, 20);
}

static u8 IceRod_CheckBackfire(Player* p, PlayState* play, s16 magicCost, u8 backfireChance) {
    if (ItemMagic_HasEnough(play, magicCost))
        return 0;

    u8 roll = (u8)(Rand_ZeroOne() * 100.0f);
    if (roll < backfireChance) {
        IceRod_Backfire(p, play);
        return 1;
    }

    Sfx_PlaySfxCentered(ICE_ROD_SFX_NO_MAGIC);
    return 1;
}

// =============================================================================
// MULTI-SET PROJECTILE SYSTEM - Up to 5 concurrent sets of 1-3 iceballs
// =============================================================================

// Multi-set accessors
RodProjSet* IceRod_GetProjSets(void) {
    return sIceProjSets;
}
u8 IceRod_HasAnyActiveSet(void) {
    for (s32 s = 0; s < ROD_MAX_PROJ_SETS; s++)
        if (sIceProjSets[s].active)
            return 1;
    return 0;
}
// Tear down stale colliders before reuse / on deactivation. See the
// matching helper in item_rod_fire.c for full rationale: the global
// collider pool was leaking across rod fires after ~2 hours of sustained
// shooting, breaking hit detection for every "aim/shoot" item.
static void IceRod_DestroySetColliders(RodProjSet* set, PlayState* play) {
    if (!set->collidersInited) return;
    for (s32 i = 0; i < 3; i++) {
        Collider_DestroyCylinder(play, &set->colliders[i]);
    }
    set->collidersInited = 0;
}

static RodProjSet* IceRod_FindFreeSet(PlayState* play) {
    for (s32 s = 0; s < ROD_MAX_PROJ_SETS; s++)
        if (!sIceProjSets[s].active)
            return &sIceProjSets[s];
    // All full — recycle oldest (lowest timer). Tear down its colliders
    // BEFORE reuse so stale collider records don't carry over.
    RodProjSet* oldest = &sIceProjSets[0];
    for (s32 s = 1; s < ROD_MAX_PROJ_SETS; s++)
        if (sIceProjSets[s].timer < oldest->timer)
            oldest = &sIceProjSets[s];
    IceRod_DestroySetColliders(oldest, play);
    return oldest;
}

static void IceRod_InitSetColliders(RodProjSet* set, Player* p, PlayState* play) {
    if (set->collidersInited)
        return;
    for (s32 i = 0; i < 3; i++) {
        Collider_InitCylinder(play, &set->colliders[i]);
        Collider_SetCylinder(play, &set->colliders[i], &p->actor, &sIceRodProjColInit);
    }
    set->collidersInited = 1;
}

static void IceRod_CalcVelocity(Vec3f* outVel, s16 yaw, s16 pitch) {
    Vec3f localVel = { 0.0f, 0.0f, ICE_ROD_PROJ_SPEED };
    Matrix_Push();
    Matrix_RotateY(BINANG_TO_RAD(yaw), MTXMODE_NEW);
    Matrix_RotateX(BINANG_TO_RAD(pitch), MTXMODE_APPLY);
    Matrix_MultVec3f(&localVel, outVel);
    Matrix_Pop();
}

// Spawns single projectile into a free set slot (stab, first-person)
static void IceRod_InitSingleProjectile(Player* p, PlayState* play, Vec3f* startPos, s16 yaw, s16 pitch, f32 maxRange) {
    RodProjSet* set = IceRod_FindFreeSet(play);
    IceRod_InitSetColliders(set, p, play);

    set->targetScale = 2.0f;
    set->active = 1;
    set->count = 1;
    set->pos[0] = *startPos;
    set->timer = (s16)(maxRange / ICE_ROD_PROJ_SPEED);
    if (set->timer < 10)
        set->timer = 10;
    if (set->timer > 30)
        set->timer = 30;

    set->scale = 0.0f;
    set->rotZ = 0;
    for (s32 i = 0; i < 6; i++)
        set->trail[i] = *startPos;

    set->yaw = yaw;
    set->pitch = pitch;
    IceRod_CalcVelocity(&set->vel[0], yaw, pitch);
}

// Spawns 3 iceballs spread into a free set slot (slash attack)
static void IceRod_InitTripleProjectile(Player* p, PlayState* play, Vec3f* startPos, s16 baseYaw, s16 pitch) {
    RodProjSet* set = IceRod_FindFreeSet(play);
    IceRod_InitSetColliders(set, p, play);

    set->targetScale = 2.0f;
    set->active = 1;
    set->count = 3;

    s16 spreadAngle = (s16)(ICE_ROD_SLASH_SPREAD * (0x10000 / 360));
    set->timer = (s16)(ICE_ROD_SLASH_RANGE / ICE_ROD_PROJ_SPEED);
    if (set->timer < 10)
        set->timer = 10;

    set->scale = 0.0f;
    set->rotZ = 0;

    // Center iceball
    set->pos[0] = *startPos;
    set->yaw = baseYaw;
    set->pitch = pitch;
    IceRod_CalcVelocity(&set->vel[0], baseYaw, pitch);
    for (s32 i = 0; i < 6; i++)
        set->trail[i] = *startPos;

    // Left iceball (-spread angle)
    set->pos[1] = *startPos;
    IceRod_CalcVelocity(&set->vel[1], baseYaw - spreadAngle, pitch);

    // Right iceball (+spread angle)
    set->pos[2] = *startPos;
    IceRod_CalcVelocity(&set->vel[2], baseYaw + spreadAngle, pitch);
}

static void IceRod_UpdateCollider(ColliderCylinder* col, Vec3f* pos, f32 scale, PlayState* play) {
    // Use larger collider sizes for better hit detection
    col->dim.radius = (s16)(scale * 5.0f + ICE_ROD_PROJ_RADIUS);
    col->dim.height = (s16)(scale * 8.0f + ICE_ROD_PROJ_HEIGHT);
    col->dim.pos.x = (s16)pos->x;
    col->dim.pos.y = (s16)pos->y;
    col->dim.pos.z = (s16)pos->z;
    CollisionCheck_SetAT(play, &play->colChkCtx, &col->base);
}

static u8 IceRod_CheckHit(ColliderCylinder* col, Vec3f* pos, PlayState* play, Player* p) {
    if (col->base.atFlags & AT_HIT) {
        // Spawn ice burst effect on hit
        EffectSsIcePiece_SpawnBurst(play, pos, 1.0f);
        Audio_PlayActorSound2(&p->actor, ICE_ROD_SFX_ICE_EXPLODE);

        // Force freeze on hit actor (works on all enemies regardless of their ice damage handling)
        if (col->base.at != NULL && col->base.at->update != NULL) {
            Actor* hitActor = col->base.at;
            if (hitActor->category == ACTORCAT_ENEMY || hitActor->category == ACTORCAT_BOSS) {
                // Set freeze timer to pause the actor
                hitActor->freezeTimer = ICE_ROD_FREEZE_DURATION;
                // Apply blue color filter for frozen appearance (0x4000 = blue tint)
                Actor_SetColorFilter(hitActor, 0x4000, 255, 0x2000, ICE_ROD_FREEZE_DURATION);
                // Spawn ice visual effect on the frozen enemy
                EffectSsEnIce_SpawnFlyingVec3f(play, hitActor, &hitActor->world.pos, 150, 150, 150, 250, 235, 245, 255,
                                               1.0f);
                Audio_PlayActorSound2(hitActor, NA_SE_PL_FREEZE_S);
            }
        }

        col->base.atFlags &= ~AT_HIT;
        return 1;
    }
    return 0;
}

// EffectSsEnIce_Spawn: Creates ice clump effects
static void IceRod_SpawnIceSparks(PlayState* play, Vec3f* pos, f32 scale) {
    Color_RGBA8 primColor = { ICE_ROD_PRIM_R, ICE_ROD_PRIM_G, ICE_ROD_PRIM_B, ICE_ROD_PRIM_A };
    Color_RGBA8 envColor = { ICE_ROD_ENV_R, ICE_ROD_ENV_G, ICE_ROD_ENV_B, ICE_ROD_ENV_A };
    Vec3f vel = { 0.0f, 0.0f, 0.0f };
    Vec3f accel = { 0.0f, -0.5f, 0.0f };

    for (s32 i = 0; i < 6; i++) {
        Vec3f sparkPos;
        sparkPos.x = pos->x + (Rand_ZeroOne() - 0.5f) * (scale * 15.0f);
        sparkPos.y = pos->y + (Rand_ZeroOne() - 0.5f) * (scale * 15.0f);
        sparkPos.z = pos->z + (Rand_ZeroOne() - 0.5f) * (scale * 15.0f);
        vel.x = (Rand_ZeroOne() - 0.5f) * 3.0f;
        vel.y = Rand_ZeroOne() * 2.0f;
        vel.z = (Rand_ZeroOne() - 0.5f) * 3.0f;
        EffectSsEnIce_Spawn(play, &sparkPos, scale * 0.3f, &vel, &accel, &primColor, &envColor, 15);
    }
}

// Check if any ice rod projectile hits red ice (BG_ICE_SHELTER) and melt it
static u8 IceRod_CheckRedIceMelt(PlayState* play) {
    Actor* actor;
    Actor* next;
    u8 melted = 0;
    f32 projRadius = (f32)ICE_ROD_PROJ_RADIUS + 5.0f;

    for (actor = play->actorCtx.actorLists[ACTORCAT_BG].head; actor != NULL; actor = next) {
        next = actor->next;
        if (actor->id != ACTOR_BG_ICE_SHELTER)
            continue;

        BgIceShelter* ice = (BgIceShelter*)actor;
        f32 iceRadius = (f32)ice->cylinder1.dim.radius + projRadius;
        f32 iceHeight = (f32)ice->cylinder1.dim.height;
        u8 hit = 0;

        // Check all active projectile sets
        for (s32 s = 0; s < ROD_MAX_PROJ_SETS && !hit; s++) {
            RodProjSet* set = &sIceProjSets[s];
            if (!set->active)
                continue;

            for (s32 p = 0; p < set->count && !hit; p++) {
                f32 dx = set->pos[p].x - actor->world.pos.x;
                f32 dy = set->pos[p].y - actor->world.pos.y;
                f32 dz = set->pos[p].z - actor->world.pos.z;
                f32 xzDist = sqrtf(SQ(dx) + SQ(dz));

                if (xzDist < iceRadius && dy > -projRadius && dy < iceHeight + projRadius) {
                    hit = 1;
                }
            }
        }

        if (hit) {
            BgIceShelter_MeltInstantly(actor, play);
            melted = 1;
        }
    }
    return melted;
}

// Update a single projectile set
static void IceRod_UpdateOneSet(RodProjSet* set, Player* p, PlayState* play) {
    set->rotZ += 5000;

    if (set->timer > 0)
        set->timer--;
    if (set->timer == 0)
        set->targetScale = 0.0f;

    Math_ApproachF(&set->scale, set->targetScale, 0.2f, 0.4f);

    if (set->timer == 0 && set->scale < 0.1f) {
        set->active = 0;
        IceRod_DestroySetColliders(set, play);
        return;
    }

    // Update all projectile positions in this set
    for (s32 i = 0; i < set->count; i++) {
        set->pos[i].x += set->vel[i].x;
        set->pos[i].y += set->vel[i].y;
        set->pos[i].z += set->vel[i].z;
    }

    // Trail for center projectile
    for (s32 i = 4; i >= 0; i--)
        set->trail[i + 1] = set->trail[i];
    set->trail[0] = set->pos[0];

    // Sparks and sound
    if (set->scale >= 0.4f) {
        for (s32 i = 0; i < set->count; i++)
            IceRod_SpawnIceSparks(play, &set->pos[i], set->scale);
        Audio_PlayActorSound2(&p->actor, ICE_ROD_SFX_ICE_LOOP - SFX_FLAG);
    }

    // Colliders
    if (set->scale >= 0.6f) {
        for (s32 i = 0; i < set->count; i++)
            IceRod_UpdateCollider(&set->colliders[i], &set->pos[i], set->scale, play);
    }

    // Hit detection
    u8 anyHit = 0;
    for (s32 i = 0; i < set->count; i++)
        anyHit |= IceRod_CheckHit(&set->colliders[i], &set->pos[i], play, p);

    if (anyHit) {
        for (s32 i = 0; i < 3; i++)
            set->vel[i].x = set->vel[i].y = set->vel[i].z = 0.0f;
        set->timer = 0;
        set->targetScale = 0.0f;
    }
}

// Update ALL active projectile sets and sync gCustomItemState for network
static void IceRod_UpdateProjectile(Player* p, PlayState* play) {
    u8 anyActive = 0;
    for (s32 s = 0; s < ROD_MAX_PROJ_SETS; s++) {
        if (!sIceProjSets[s].active)
            continue;
        IceRod_UpdateOneSet(&sIceProjSets[s], p, play);
        if (sIceProjSets[s].active)
            anyActive = 1;
    }

    // Sync first active set to gCustomItemState for Harpoon network visual
    iceRodProjActive = anyActive;
    if (anyActive) {
        for (s32 s = 0; s < ROD_MAX_PROJ_SETS; s++) {
            if (sIceProjSets[s].active) {
                iceRodProjPos = sIceProjSets[s].pos[0];
                gCustomItemState.iceRodProjPos2 = sIceProjSets[s].pos[1];
                gCustomItemState.iceRodProjPos3 = sIceProjSets[s].pos[2];
                gCustomItemState.iceRodProjCount = sIceProjSets[s].count;
                iceRodProjScale = sIceProjSets[s].scale;
                memcpy(iceRodProjTrail, sIceProjSets[s].trail, sizeof(sIceProjSets[s].trail));
                break;
            }
        }
    }

    // Red ice melt check across all active sets
    if (anyActive)
        IceRod_CheckRedIceMelt(play);

    if (!anyActive)
        Audio_StopSfxById(ICE_ROD_SFX_ICE_LOOP);
}

// =============================================================================
// ICE WAVE SYSTEM - 6 ice effects with individual colliders (jump slash)
// =============================================================================

static void IceRod_InitWaveColliders(Player* p, PlayState* play) {
    if (sIceWaveCollidersInited)
        return;

    for (s32 i = 0; i < ICE_ROD_WAVE_COUNT; i++) {
        Collider_InitCylinder(play, &iceRodWaveColliders[i]);
        Collider_SetCylinder(play, &iceRodWaveColliders[i], &p->actor, &sIceRodWaveColInit);
    }
    sIceWaveCollidersInited = 1;
}

static void IceRod_StartIceWave(Player* p, PlayState* play) {
    IceRod_InitWaveColliders(p, play);

    Vec3f impactPos = p->actor.world.pos;
    impactPos.y = p->actor.floorHeight + 5.0f;
    s16 playerYaw = p->actor.shape.rot.y;

    iceRodWaveActive = 1;
    iceRodWaveTimer = 30;

    // Position ice effects in a line going forward from Link
    for (s32 i = 0; i < ICE_ROD_WAVE_COUNT; i++) {
        f32 dist = (i + 1) * ICE_ROD_WAVE_SPACING;
        iceRodWavePos[i].x = impactPos.x + dist * Math_SinS(playerYaw);
        iceRodWavePos[i].y = impactPos.y + 10.0f;
        iceRodWavePos[i].z = impactPos.z + dist * Math_CosS(playerYaw);

        // EffectSsIcePiece_SpawnBurst: Spawns ice burst at position
        f32 scale = (ICE_ROD_WAVE_BASE_SCALE + (i * ICE_ROD_WAVE_SCALE_GROW)) / 100.0f;
        EffectSsIcePiece_SpawnBurst(play, &iceRodWavePos[i], scale);
    }

    Audio_PlayActorSound2(&p->actor, ICE_ROD_SFX_ICEWAVE);
    Audio_PlayActorSound2(&p->actor, ICE_ROD_SFX_ICE_LOOP);
}

static void IceRod_UpdateIceWave(Player* p, PlayState* play) {
    if (!iceRodWaveActive)
        return;

    if (iceRodWaveTimer > 0) {
        iceRodWaveTimer--;

        // Update colliders for all ice effects
        for (s32 i = 0; i < ICE_ROD_WAVE_COUNT; i++) {
            f32 scale = ICE_ROD_WAVE_BASE_SCALE + (i * ICE_ROD_WAVE_SCALE_GROW);
            // Use larger colliders for better hit detection
            iceRodWaveColliders[i].dim.radius = (s16)(scale * 80.0f + ICE_ROD_WAVE_RADIUS + 15);
            iceRodWaveColliders[i].dim.height = (s16)(scale * 100.0f + ICE_ROD_WAVE_HEIGHT + 30);
            iceRodWaveColliders[i].dim.pos.x = (s16)iceRodWavePos[i].x;
            iceRodWaveColliders[i].dim.pos.y = (s16)iceRodWavePos[i].y;
            iceRodWaveColliders[i].dim.pos.z = (s16)iceRodWavePos[i].z;
            CollisionCheck_SetAT(play, &play->colChkCtx, &iceRodWaveColliders[i].base);

            // Check for wave hits and force freeze
            if (iceRodWaveColliders[i].base.atFlags & AT_HIT) {
                if (iceRodWaveColliders[i].base.at != NULL && iceRodWaveColliders[i].base.at->update != NULL) {
                    Actor* hitActor = iceRodWaveColliders[i].base.at;
                    if (hitActor->category == ACTORCAT_ENEMY || hitActor->category == ACTORCAT_BOSS) {
                        hitActor->freezeTimer = ICE_ROD_FREEZE_DURATION;
                        Actor_SetColorFilter(hitActor, 0x4000, 255, 0x2000, ICE_ROD_FREEZE_DURATION);
                        EffectSsEnIce_SpawnFlyingVec3f(play, hitActor, &hitActor->world.pos, 150, 150, 150, 250, 235,
                                                       245, 255, 1.0f);
                        Audio_PlayActorSound2(hitActor, NA_SE_PL_FREEZE_S);
                    }
                }
                iceRodWaveColliders[i].base.atFlags &= ~AT_HIT;
            }
        }
    } else {
        iceRodWaveActive = 0;
        Audio_StopSfxById(ICE_ROD_SFX_ICE_LOOP);
    }
}

// =============================================================================
// ATTACK EFFECTS
// =============================================================================

// Slash: 3 iceballs spread at short range
static void IceRod_SlashEffect(Player* p, PlayState* play) {
    if (IceRod_CheckBackfire(p, play, ICE_ROD_MAGIC_SLASH, ICE_ROD_BACKFIRE_SLASH))
        return;
    ItemMagic_Consume(play, ICE_ROD_MAGIC_SLASH);

    Vec3f* tipPos = &p->meleeWeaponInfo[0].tip;
    s16 baseYaw, pitch;

    if (Player_IsZTargeting(p) && p->focusActor != NULL && p->focusActor->update != NULL) {
        Vec3f* targetPos = &p->focusActor->focus.pos;
        baseYaw = Math_Vec3f_Yaw(tipPos, targetPos);
        pitch = Math_Vec3f_Pitch(tipPos, targetPos);
    } else {
        baseYaw = p->actor.shape.rot.y;
        pitch = 0;
    }

    IceRod_InitTripleProjectile(p, play, tipPos, baseYaw, pitch);
    Audio_PlayActorSound2(&p->actor, ICE_ROD_SFX_SWING);
}

// Stab: Single iceball at long range - uses weapon direction from base to tip
static void IceRod_StabEffect(Player* p, PlayState* play) {
    if (IceRod_CheckBackfire(p, play, ICE_ROD_MAGIC_STAB, ICE_ROD_BACKFIRE_SLASH))
        return;
    ItemMagic_Consume(play, ICE_ROD_MAGIC_STAB);

    Vec3f* tipPos = &p->meleeWeaponInfo[0].tip;
    Vec3f* basePos = &p->meleeWeaponInfo[0].base;
    s16 yaw, pitch;

    if (Player_IsZTargeting(p) && p->focusActor != NULL && p->focusActor->update != NULL) {
        Vec3f* targetPos = &p->focusActor->focus.pos;
        yaw = Math_Vec3f_Yaw(tipPos, targetPos);
        pitch = Math_Vec3f_Pitch(tipPos, targetPos);
    } else {
        // Use weapon direction (base to tip) for stab direction
        yaw = Math_Vec3f_Yaw(basePos, tipPos);
        pitch = Math_Vec3f_Pitch(basePos, tipPos);
    }

    IceRod_InitSingleProjectile(p, play, tipPos, yaw, pitch, ICE_ROD_PROJ_LIFETIME * ICE_ROD_PROJ_SPEED);
    Audio_PlayActorSound2(&p->actor, ICE_ROD_SFX_SWING);
}

// Jump Slash: Ice wave cone
static void IceRod_JumpEffect(Player* p, PlayState* play) {
    if (IceRod_CheckBackfire(p, play, ICE_ROD_MAGIC_JUMP, ICE_ROD_BACKFIRE_JUMP))
        return;
    ItemMagic_Consume(play, ICE_ROD_MAGIC_JUMP);
    IceRod_StartIceWave(p, play);
}

// First Person: Fires stab in aimed direction
static void IceRod_FirstPersonFire(Player* p, PlayState* play) {
    if (IceRod_CheckBackfire(p, play, ICE_ROD_MAGIC_STAB, ICE_ROD_BACKFIRE_SLASH))
        return;
    ItemMagic_Consume(play, ICE_ROD_MAGIC_STAB);

    s16 aimYaw = FirstPerson_GetAimYaw(p);
    s16 aimPitch = FirstPerson_GetAimPitch(p);

    Vec3f startPos;
    startPos.x = p->actor.world.pos.x + 30.0f * Math_SinS(aimYaw);
    startPos.y = p->actor.world.pos.y + 40.0f;
    startPos.z = p->actor.world.pos.z + 30.0f * Math_CosS(aimYaw);

    IceRod_InitSingleProjectile(p, play, &startPos, aimYaw, aimPitch, ICE_ROD_PROJ_LIFETIME * ICE_ROD_PROJ_SPEED);
    Audio_PlayActorSound2(&p->actor, ICE_ROD_SFX_SWING);
}

// =============================================================================
// SWING PROCESSING
// =============================================================================

static void IceRod_SwingParticles(Player* p, PlayState* play) {
    Vec3f* tipPos = &p->meleeWeaponInfo[0].tip;
    Vec3f* basePos = &p->meleeWeaponInfo[0].base;

    if ((play->gameplayFrames % 2) == 0) {
        FX_SpawnRodSwingParticles(play, tipPos, &sIceRodColor);
    }

    if (iceRodBlureIdx >= 0) {
        FX_AddSwordTrailVertex(iceRodBlureIdx, basePos, tipPos);
    }

    if ((play->gameplayFrames % 8) == 0) {
        Audio_PlayActorSound2(&p->actor, ICE_ROD_SFX_ICE_IGNITE);
    }
}

static u8 IceRod_IsSpinAttack(u8 mwa) {
    return (mwa == PLAYER_MWA_SPIN_ATTACK_1H || mwa == PLAYER_MWA_SPIN_ATTACK_2H || mwa == PLAYER_MWA_BIG_SPIN_1H ||
            mwa == PLAYER_MWA_BIG_SPIN_2H);
}

// =============================================================================
// SPIN ICE CYLINDER
// =============================================================================

static void IceRod_StartSpinIce(Player* p, PlayState* play, u8 isBigSpin) {
    if (!sIceSpinColliderInited) {
        Collider_InitCylinder(play, &iceRodSpinCollider);
        Collider_SetCylinder(play, &iceRodSpinCollider, &p->actor, &sIceRodSpinColInit);
        sIceSpinColliderInited = 1;
    }

    iceRodSpinActive = 1;
    iceRodSpinIsBig = isBigSpin;
    iceRodSpinRadius = 50.0f;
    iceRodSpinMaxRadius = isBigSpin ? ICE_ROD_SPIN_BIG_RADIUS : ICE_ROD_SPIN_SMALL_RADIUS;
    sIceSpinExpandProgress = 0.0f;
    Audio_PlayActorSound2(&p->actor, ICE_ROD_SFX_ICE_CAST);
}

static void IceRod_UpdateSpinIce(Player* p, PlayState* play) {
    if (!iceRodSpinActive)
        return;

    f32 expandSpeed = iceRodSpinIsBig ? 30.0f : 15.0f;
    iceRodSpinRadius += expandSpeed;
    if (iceRodSpinRadius >= iceRodSpinMaxRadius)
        iceRodSpinRadius = iceRodSpinMaxRadius;

    sIceSpinExpandProgress = iceRodSpinRadius / iceRodSpinMaxRadius;
    if (sIceSpinExpandProgress > 1.0f)
        sIceSpinExpandProgress = 1.0f;

    iceRodSpinCollider.dim.radius = (s16)iceRodSpinRadius;
    iceRodSpinCollider.dim.height = 80;
    iceRodSpinCollider.dim.pos.x = (s16)p->actor.world.pos.x;
    iceRodSpinCollider.dim.pos.y = (s16)p->actor.world.pos.y;
    iceRodSpinCollider.dim.pos.z = (s16)p->actor.world.pos.z;

    CollisionCheck_SetAT(play, &play->colChkCtx, &iceRodSpinCollider.base);

    // Check for spin attack hits and force freeze
    if (iceRodSpinCollider.base.atFlags & AT_HIT) {
        if (iceRodSpinCollider.base.at != NULL && iceRodSpinCollider.base.at->update != NULL) {
            Actor* hitActor = iceRodSpinCollider.base.at;
            if (hitActor->category == ACTORCAT_ENEMY || hitActor->category == ACTORCAT_BOSS) {
                hitActor->freezeTimer = ICE_ROD_FREEZE_DURATION;
                Actor_SetColorFilter(hitActor, 0x4000, 255, 0x2000, ICE_ROD_FREEZE_DURATION);
                EffectSsEnIce_SpawnFlyingVec3f(play, hitActor, &hitActor->world.pos, 150, 150, 150, 250, 235, 245, 255,
                                               1.0f);
                EffectSsIcePiece_SpawnBurst(play, &hitActor->world.pos, 1.0f);
                Audio_PlayActorSound2(hitActor, NA_SE_PL_FREEZE_S);
            }
        }
        iceRodSpinCollider.base.atFlags &= ~AT_HIT;
    }

    // Draw ice cylinder (blue to white based on progress)
    FX_DrawSpinFireCylinder(play, p, iceRodSpinRadius, iceRodSpinIsBig, &sIceRodColor);

    if ((play->gameplayFrames % 6) == 0) {
        Audio_PlayActorSound2(&p->actor, ICE_ROD_SFX_ICE_IGNITE);
    }
}

static void IceRod_StopSpinIce(void) {
    iceRodSpinActive = 0;
    iceRodSpinRadius = 0.0f;
    sIceSpinExpandProgress = 0.0f;
    Audio_StopSfxById(ICE_ROD_SFX_ICE_CAST);
}

static void IceRod_ProcessSwing(Player* p, PlayState* play) {
    u8 mwa = p->meleeWeaponAnimation;

    IceRod_SwingParticles(p, play);

    if (IceRod_IsSpinAttack(mwa)) {
        if (!iceRodSpinActive) {
            u8 isBigSpin = (mwa == PLAYER_MWA_BIG_SPIN_1H || mwa == PLAYER_MWA_BIG_SPIN_2H);
            IceRod_StartSpinIce(p, play, isBigSpin);
        }
        IceRod_UpdateSpinIce(p, play);
    } else {
        if (iceRodSpinActive)
            IceRod_StopSpinIce();
    }

    if (mwa == sIceLastSwingType)
        return;
    sIceLastSwingType = mwa;

    switch (mwa) {
        case PLAYER_MWA_FORWARD_SLASH_1H:
        case PLAYER_MWA_FORWARD_SLASH_2H:
        case PLAYER_MWA_FORWARD_COMBO_1H:
        case PLAYER_MWA_FORWARD_COMBO_2H:
        case PLAYER_MWA_RIGHT_SLASH_1H:
        case PLAYER_MWA_RIGHT_SLASH_2H:
        case PLAYER_MWA_RIGHT_COMBO_1H:
        case PLAYER_MWA_RIGHT_COMBO_2H:
        case PLAYER_MWA_LEFT_SLASH_1H:
        case PLAYER_MWA_LEFT_SLASH_2H:
        case PLAYER_MWA_LEFT_COMBO_1H:
        case PLAYER_MWA_LEFT_COMBO_2H:
            IceRod_SlashEffect(p, play);
            break;

        case PLAYER_MWA_STAB_1H:
        case PLAYER_MWA_STAB_2H:
        case PLAYER_MWA_STAB_COMBO_1H:
        case PLAYER_MWA_STAB_COMBO_2H:
            IceRod_StabEffect(p, play);
            break;

        case PLAYER_MWA_FLIPSLASH_START:
        case PLAYER_MWA_JUMPSLASH_START:
            sIceJumpEffectSpawned = 0;
            break;

        case PLAYER_MWA_FLIPSLASH_FINISH:
        case PLAYER_MWA_JUMPSLASH_FINISH:
            if (!sIceJumpEffectSpawned) {
                IceRod_JumpEffect(p, play);
                sIceJumpEffectSpawned = 1;
            }
            break;

        default:
            break;
    }
}

// =============================================================================
// CHARGE ATTACK
// =============================================================================

static u8 IceRod_CanCharge(Player* p, PlayState* play) {
    if (p->meleeWeaponState > 0)
        return 0;
    if (p->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_DAMAGED |
                          PLAYER_STATE1_LOADING | PLAYER_STATE1_HOOKSHOT_FALLING))
        return 0;
    if (!(p->actor.bgCheckFlags & 1))
        return 0;
    if (p->stateFlags2 & PLAYER_STATE2_HOPPING)
        return 0;
    return 1;
}

static void IceRod_StartCharge(Player* p, PlayState* play) {
    iceRodCharging = 1;
    iceRodChargeLevel = 0.0f;
    iceRodChargeReady = 0;
    iceRodChargeTimer = 0;
    iceRodState = ICE_ROD_STATE_CHARGING;
    Audio_PlayActorSound2(&p->actor, ICE_ROD_SFX_CHARGE);
}

static void IceRod_UpdateCharge(Player* p, PlayState* play) {
    if (!iceRodCharging)
        return;

    iceRodChargeTimer++;

    if (iceRodChargeLevel < 1.0f) {
        iceRodChargeLevel += ICE_ROD_CHARGE_RATE;
        if (iceRodChargeLevel > 1.0f)
            iceRodChargeLevel = 1.0f;
    }

    if (!iceRodChargeReady && iceRodChargeLevel >= ICE_ROD_CHARGE_MIN) {
        iceRodChargeReady = 1;
        Audio_PlayActorSound2(&p->actor, ICE_ROD_SFX_CHARGE);
    }

    if (iceRodChargeLevel >= ICE_ROD_CHARGE_BIG &&
        iceRodChargeTimer == (s16)(ICE_ROD_CHARGE_BIG / ICE_ROD_CHARGE_RATE)) {
        Audio_PlayActorSound2(&p->actor, ICE_ROD_SFX_CHARGE);
    }

    FX_DrawChargeAura(play, p, iceRodChargeLevel, &sIceRodColor);

    if ((play->gameplayFrames % 3) == 0) {
        Vec3f* tipPos = &p->meleeWeaponInfo[0].tip;
        FX_SpawnRodSwingParticles(play, tipPos, &sIceRodColor);
    }

    if ((play->gameplayFrames % 12) == 0) {
        Audio_PlayActorSound2(&p->actor, ICE_ROD_SFX_ICE_IGNITE);
    }
}

static void IceRod_ReleaseCharge(Player* p, PlayState* play) {
    if (!iceRodCharging)
        return;

    s32 spinType;
    u8 isBigSpin = 0;
    s16 magicCost;

    if (iceRodChargeLevel >= ICE_ROD_CHARGE_BIG) {
        spinType = PLAYER_MWA_BIG_SPIN_1H;
        magicCost = ICE_ROD_MAGIC_SPIN_BIG;
        isBigSpin = 1;
    } else if (iceRodChargeLevel >= ICE_ROD_CHARGE_MIN) {
        spinType = PLAYER_MWA_SPIN_ATTACK_1H;
        magicCost = ICE_ROD_MAGIC_SPIN_SMALL;
    } else {
        iceRodCharging = 0;
        iceRodChargeLevel = 0.0f;
        iceRodChargeReady = 0;
        iceRodState = ICE_ROD_STATE_EQUIPPED;
        return;
    }

    if (IceRod_CheckBackfire(p, play, magicCost, ICE_ROD_BACKFIRE_SPIN)) {
        iceRodCharging = 0;
        iceRodChargeLevel = 0.0f;
        iceRodChargeReady = 0;
        iceRodState = ICE_ROD_STATE_EQUIPPED;
        return;
    }

    ItemMagic_Consume(play, magicCost);
    func_80837948(play, p, spinType);

    iceRodCharging = 0;
    iceRodChargeLevel = 0.0f;
    iceRodChargeReady = 0;
    iceRodState = ICE_ROD_STATE_SWINGING;
    Audio_PlayActorSound2(&p->actor, ICE_ROD_SFX_SWING);
}

static void IceRod_CancelCharge(Player* p) {
    Audio_StopSfxById(ICE_ROD_SFX_CHARGE);
    iceRodCharging = 0;
    iceRodChargeLevel = 0.0f;
    iceRodChargeReady = 0;
    iceRodState = ICE_ROD_STATE_EQUIPPED;
    sIceChargeButtonHeld = 0;
    sIceChargeHoldCounter = 0;
}

// =============================================================================
// FIRST PERSON MODE - Toggle with C-UP, exit on other buttons
// =============================================================================

static void IceRod_EnterFirstPerson(Player* p, PlayState* play) {
    FirstPerson_Init(p, play);
    iceRodFirstPerson = 1;
    iceRodState = ICE_ROD_STATE_AIMING;
}

static void IceRod_ExitFirstPerson(Player* p, PlayState* play) {
    FirstPerson_Exit(p, play);
    iceRodFirstPerson = 0;
    iceRodState = ICE_ROD_STATE_EQUIPPED;
}

static void IceRod_UpdateFirstPerson(Player* p, PlayState* play, ItemInputState* in) {
    FirstPerson_Update(p, play);

    // Fire on equipped C-button press
    if (in->isPressed) {
        IceRod_FirstPersonFire(p, play);
    }

    // Toggle off with C-UP
    if (CHECK_BTN_ALL(play->state.input[0].press.button, BTN_CUP)) {
        IceRod_ExitFirstPerson(p, play);
        return;
    }

    // Exit on A, B, other C-buttons, or damage/cutscene
    u16 exitButtons = BTN_A | BTN_B | BTN_CLEFT | BTN_CRIGHT | BTN_CDOWN;
    if (in->equippedButton)
        exitButtons &= ~in->equippedButton;

    if (CHECK_BTN_ANY(play->state.input[0].press.button, exitButtons) ||
        (p->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_DAMAGED))) {
        IceRod_ExitFirstPerson(p, play);
    }
}

// =============================================================================
// EQUIP/UNEQUIP
// =============================================================================

static void IceRod_OnEquip(PlayState* play, Player* p) {
    iceRodActive = 1;
    iceRodState = ICE_ROD_STATE_EQUIPPED;
    sIceLastSwingType = 0;
    sIceJumpEffectSpawned = 0;
    iceRodProjActive = 0;
    gCustomItemState.iceRodProjCount = 0;
    iceRodFirstPerson = 0;
    iceRodWaveActive = 0;
    for (s32 s = 0; s < ROD_MAX_PROJ_SETS; s++) {
        IceRod_DestroySetColliders(&sIceProjSets[s], play);
        sIceProjSets[s].active = 0;
    }

    iceRodCharging = 0;
    iceRodChargeLevel = 0.0f;
    iceRodChargeReady = 0;
    iceRodChargeTimer = 0;
    sIceChargeButtonHeld = 0;
    sIceChargeHoldCounter = 0;

    iceRodBlureIdx = FX_InitSwordTrail(play, &sIceRodColor);
    ItemEquip_PlayEquipSFX(play, p);
}

static void IceRod_OnUnequip(PlayState* play, Player* p) {
    if (iceRodFirstPerson)
        IceRod_ExitFirstPerson(p, play);

    iceRodActive = 0;
    iceRodState = ICE_ROD_STATE_INACTIVE;
    sIceLastSwingType = 0;
    iceRodProjActive = 0;
    gCustomItemState.iceRodProjCount = 0;
    iceRodWaveActive = 0;
    for (s32 s = 0; s < ROD_MAX_PROJ_SETS; s++)
        sIceProjSets[s].active = 0;
    Audio_StopSfxById(ICE_ROD_SFX_ICE_LOOP);
    Audio_StopSfxById(ICE_ROD_SFX_CHARGE);
    Audio_StopSfxById(ICE_ROD_SFX_ICE_CAST);

    iceRodCharging = 0;
    iceRodChargeLevel = 0.0f;
    iceRodChargeReady = 0;
    iceRodChargeTimer = 0;
    sIceChargeButtonHeld = 0;
    sIceChargeHoldCounter = 0;

    if (iceRodBlureIdx >= 0) {
        FX_KillSwordTrail(play, iceRodBlureIdx);
        iceRodBlureIdx = -1;
    }

    if (iceRodSpinActive)
        IceRod_StopSpinIce();
    ItemEquip_PlayUnequipSFX(play, p);
}

// =============================================================================
// MAIN HANDLER
// =============================================================================

void Handle_IceRod(Player* p, PlayState* play) {
    IceRod_UpdateProjectile(p, play);
    IceRod_UpdateIceWave(p, play);

    ItemInputState in;
    ItemInput_Update(&in, ITEM_ROD_ICE, p, play);
    iceRodButtonMask = in.equippedButton;

    if (!in.wasEquipped) {
        if (iceRodActive)
            IceRod_OnUnequip(play, p);
        sIceEquipState.isEquipped = 0;
        return;
    }

    // C-UP toggles first person mode
    if (iceRodActive && !iceRodFirstPerson && !iceRodCharging && p->meleeWeaponState == 0) {
        if (CHECK_BTN_ALL(play->state.input[0].press.button, BTN_CUP)) {
            IceRod_EnterFirstPerson(p, play);
            return;
        }
    }

    if (iceRodFirstPerson) {
        IceRod_UpdateFirstPerson(p, play, &in);
        return;
    }

    if (!iceRodActive) {
        if (ItemInput_IsBlockedEx(p, play, 1))
            return;
    } else {
        u32 criticalBlocks = (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_LOADING |
                              PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_TALKING | PLAYER_STATE1_GETTING_ITEM |
                              PLAYER_STATE1_DAMAGED | PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LEDGE |
                              PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_HOOKSHOT_FALLING);
        if (p->stateFlags1 & criticalBlocks) {
            if (iceRodCharging)
                IceRod_CancelCharge(p);
            // Stop every looped SFX the rod can be holding active so cutscenes / damage / talking
            // don't leave audio playing forever. Idempotent — Audio_StopSfxById is safe to call on
            // sounds that aren't currently playing.
            Audio_StopSfxById(ICE_ROD_SFX_ICE_LOOP);
            Audio_StopSfxById(ICE_ROD_SFX_CHARGE);
            Audio_StopSfxById(ICE_ROD_SFX_ICE_CAST);
            return;
        }
    }

    if (ItemInput_CheckDamage(p, &sIcePrevInvinc)) {
        if (iceRodCharging)
            IceRod_CancelCharge(p);
        if (iceRodActive)
            IceRod_OnUnequip(play, p);
        sIceEquipState.isEquipped = 0;
        return;
    }

    ItemEquip_Update(&sIceEquipState, &in, IceRod_OnEquip, IceRod_OnUnequip, p, play);

    if (!iceRodActive)
        return;

    if (iceRodCharging) {
        if (in.isHeld)
            IceRod_UpdateCharge(p, play);
        else
            IceRod_ReleaseCharge(p, play);
    } else if (in.isHeld && IceRod_CanCharge(p, play)) {
        if (!sIceChargeButtonHeld) {
            sIceChargeButtonHeld = 1;
            sIceChargeHoldCounter = 0;
        }
        sIceChargeHoldCounter++;
        if (sIceChargeHoldCounter >= ICE_ROD_CHARGE_HOLD_FRAMES) {
            IceRod_StartCharge(p, play);
        }
    } else {
        sIceChargeButtonHeld = 0;
        sIceChargeHoldCounter = 0;
    }

    if (p->meleeWeaponState > 0) {
        iceRodState = ICE_ROD_STATE_SWINGING;
        IceRod_ProcessSwing(p, play);
        if (iceRodCharging)
            IceRod_CancelCharge(p);
    } else {
        if (!iceRodCharging) {
            iceRodState = ICE_ROD_STATE_EQUIPPED;
            sIceLastSwingType = 0;
        }
        if (iceRodSpinActive)
            IceRod_StopSpinIce();
    }
}

// =============================================================================
// INIT
// =============================================================================

void Player_InitIceRodIA(PlayState* play, Player* p) {
    iceRodActive = 1;
    iceRodState = ICE_ROD_STATE_EQUIPPED;
    sIceLastSwingType = 0;
    sIceJumpEffectSpawned = 0;
    iceRodProjActive = 0;
    gCustomItemState.iceRodProjCount = 0;
    iceRodBlureIdx = -1;
    iceRodFirstPerson = 0;
    iceRodButtonMask = 0;
    iceRodWaveActive = 0;

    iceRodCharging = 0;
    iceRodChargeLevel = 0.0f;
    iceRodChargeReady = 0;
    iceRodChargeTimer = 0;
    sIceChargeButtonHeld = 0;
    sIceChargeHoldCounter = 0;

    // Init all projectile sets
    for (s32 s = 0; s < ROD_MAX_PROJ_SETS; s++) {
        sIceProjSets[s].active = 0;
        sIceProjSets[s].collidersInited = 0;
    }
    IceRod_InitWaveColliders(p, play);

    if (!sIceSpinColliderInited) {
        Collider_InitCylinder(play, &iceRodSpinCollider);
        Collider_SetCylinder(play, &iceRodSpinCollider, &p->actor, &sIceRodSpinColInit);
        sIceSpinColliderInited = 1;
    }

    iceRodSpinActive = 0;
    iceRodSpinRadius = 0.0f;
    sIceSpinExpandProgress = 0.0f;

    iceRodBlureIdx = FX_InitSwordTrail(play, &sIceRodColor);
}

void CustomItems_DrawIceRodReticle(Player* p, PlayState* play) {
    if (!iceRodFirstPerson || iceRodState != ICE_ROD_STATE_AIMING)
        return;
    FirstPerson_DrawReticle(p, play, 0.0f, ICE_ROD_RETICLE_R, ICE_ROD_RETICLE_G, ICE_ROD_RETICLE_B);
}
