/**
 * item_gustjar.c - Gust Jar (Minish Cap style)
 *
 * Two modes:
 *   ABSORB (hold C): Pull enemies/props toward nozzle, AT collider damages them.
 *                     Props break via their own AC_HIT handlers (proper drops).
 *                     Heat builds over 10s (blue→yellow→red ring).
 *   BLOW (auto after 10s absorb): Elemental cone pushes all non-boss enemies.
 *                     Element effects based on medallion selection.
 *
 * Long-press C (20 frames, while idle): Radial element picker overlay.
 * Tap C: Use with last selected element.
 */

#include "z64.h"
#include "item_gustjar.h"
#include "../custom_items.h"
#include "../helpers/camera_helper.h"
#include "../helpers/equip_helper.h"
#include "../helpers/fx_helper.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "objects/gameplay_keep/gameplay_keep.h"

void Player_InitGustJarIA(PlayState* play, Player* this) {
    Collider_InitCylinder(play, &gCustomItemState.gustJarCollider);
    Collider_SetCylinder(play, &gCustomItemState.gustJarCollider, &this->actor, &sGustJarColliderInit);
}

static void GustJar_ClearScaleCache(void); // Forward declaration

// =============================================================================
// Equip / Unequip
// =============================================================================

static void GustJar_Equip(PlayState* play, Player* player) {
    if (gjEquipped)
        return;
    gjEquipped = 1;
    gjMode = GUST_MODE_IDLE;
    gjHeatTimer = 0;
    gjBlowActive = 0;
    gjBlowTimer = 0;
    gjCooldownTimer = 0;

    if (Player_IsZTargeting(player)) {
        gjAimMode = 1;
        gjFirstPerson = 0;
    } else {
        gjAimMode = 0;
        FirstPerson_Init(player, play);
        gjFirstPerson = 1;
    }
    ItemEquip_PlayEquipSFX(play, player);
}

static void GustJar_Unequip(PlayState* play, Player* player) {
    if (!gjEquipped)
        return;
    if (gjFirstPerson) {
        FirstPerson_Exit(player, play);
        gjFirstPerson = 0;
    }
    GustJar_ClearScaleCache();
    gjEquipped = 0;
    gjMode = GUST_MODE_OFF;
    gjBlowActive = 0;
    gjHeatTimer = 0;
    gjBlowTimer = 0;
    gjCooldownTimer = 0;
    gjButtonMask = 0;
    Audio_StopSfxById(NA_SE_EV_WIND_TRAP);
    ItemEquip_PlayUnequipSFX(play, player);
}

// =============================================================================
// Aiming
// =============================================================================

static s16 GustJar_GetAimYaw(PlayState* play, Player* player) {
    // Z-targeting ALWAYS overrides — aim at focus actor regardless of aim mode
    if (Player_IsZTargeting(player) && player->focusActor != NULL) {
        return Math_Vec3f_Yaw(&player->actor.world.pos, &player->focusActor->focus.pos);
    }
    // Otherwise use aim mode
    if (gjAimMode == 0 && gjFirstPerson) {
        return FirstPerson_GetAimYaw(player);
    }
    return player->actor.shape.rot.y;
}

// =============================================================================
// Absorb Mode — pull actors + AT collider damage
// =============================================================================

static s32 GustJar_IsSuckable(Actor* actor) {
    // Props: check against suckable prop list
    if (actor->category == ACTORCAT_PROP) {
        for (s32 i = 0; i < GUST_SUCKABLE_PROP_COUNT; i++) {
            if (actor->id == sGustSuckableProps[i])
                return 1;
        }
        return 0;
    }
    // Enemies: check against suckable enemy list (small/medium only)
    if (actor->category == ACTORCAT_ENEMY) {
        for (s32 i = 0; i < GUST_SUCKABLE_ENEMY_COUNT; i++) {
            if (actor->id == sGustSuckableEnemies[i])
                return 1;
        }
        return 0;
    }
    return 0;
}

// =============================================================================
// Scale Cache (shrink actors as they get sucked in)
// =============================================================================

#define GUST_MAX_SCALED 16

static struct {
    Actor* actor;
    Vec3f originalScale;
} sScaleCache[GUST_MAX_SCALED];
static u8 sScaleCacheCount = 0;

static void GustJar_SaveScale(Actor* actor) {
    for (u8 i = 0; i < sScaleCacheCount; i++) {
        if (sScaleCache[i].actor == actor)
            return;
    }
    if (sScaleCacheCount < GUST_MAX_SCALED) {
        sScaleCache[sScaleCacheCount].actor = actor;
        sScaleCache[sScaleCacheCount].originalScale = actor->scale;
        sScaleCacheCount++;
    }
}

static void GustJar_ShrinkActor(Actor* actor, f32 factor) {
    GustJar_SaveScale(actor);
    for (u8 i = 0; i < sScaleCacheCount; i++) {
        if (sScaleCache[i].actor == actor) {
            actor->scale.x = sScaleCache[i].originalScale.x * factor;
            actor->scale.y = sScaleCache[i].originalScale.y * factor;
            actor->scale.z = sScaleCache[i].originalScale.z * factor;
            return;
        }
    }
}

static void GustJar_ClearScaleCache(void) {
    for (u8 i = 0; i < sScaleCacheCount; i++) {
        if (sScaleCache[i].actor != NULL && sScaleCache[i].actor->update != NULL) {
            sScaleCache[i].actor->scale = sScaleCache[i].originalScale;
        }
    }
    sScaleCacheCount = 0;
}

// =============================================================================
// Freezard-style cone VFX (smoke particles in cone shape)
// =============================================================================

// Spawn smoke balls flowing TOWARD nozzle (suction cone)
void GustJar_SpawnSuckVFX(PlayState* play, Vec3f* nozzle, s16 aimYaw) {
    // 6 particles per frame spread in a cone, moving toward nozzle
    for (s32 i = 0; i < 6; i++) {
        f32 dist = 80.0f + Rand_ZeroFloat(140.0f);
        s16 spreadAngle = aimYaw + (s16)Rand_CenteredFloat(0x2000); // ~45° spread
        f32 spreadY = Rand_CenteredFloat(30.0f);

        Vec3f pos = {
            nozzle->x + Math_SinS(spreadAngle) * dist,
            nozzle->y + spreadY,
            nozzle->z + Math_CosS(spreadAngle) * dist,
        };
        // Velocity: toward nozzle (negative of outward direction)
        f32 speed = 8.0f + Rand_ZeroFloat(4.0f);
        Vec3f vel = {
            -Math_SinS(spreadAngle) * speed,
            Rand_CenteredFloat(1.0f),
            -Math_CosS(spreadAngle) * speed,
        };
        Vec3f accel = { 0, 0.6f, 0 }; // Slight upward buoyancy (like Freezard)

        Color_RGBA8 prim = { 195, 225, 235, 150 }; // Pale cyan (Freezard style)
        Color_RGBA8 env = { 150, 200, 220, 100 };
        func_8002836C(play, &pos, &vel, &accel, &prim, &env, 200, 30, 12);
    }
}

// Spawn smoke balls flowing AWAY from nozzle (blow cone), colored by element
void GustJar_SpawnBlowVFX(PlayState* play, Vec3f* nozzle, s16 aimYaw, u8 element) {
    const GustElementColor* col = &sGustElementColors[element];

    for (s32 i = 0; i < 6; i++) {
        s16 spreadAngle = aimYaw + (s16)Rand_CenteredFloat(0x2000);
        f32 startDist = 10.0f + Rand_ZeroFloat(20.0f);

        Vec3f pos = {
            nozzle->x + Math_SinS(spreadAngle) * startDist,
            nozzle->y + Rand_CenteredFloat(10.0f),
            nozzle->z + Math_CosS(spreadAngle) * startDist,
        };
        // Velocity: away from nozzle (Freezard blow direction)
        f32 speed = 15.0f + Rand_ZeroFloat(5.0f);
        Vec3f vel = {
            Math_SinS(spreadAngle) * speed,
            Rand_CenteredFloat(2.0f) - 1.0f,
            Math_CosS(spreadAngle) * speed,
        };
        Vec3f accel = { 0, 0.6f, 0 };

        func_8002836C(play, &pos, &vel, &accel, (Color_RGBA8*)&col->prim, (Color_RGBA8*)&col->env, 200, 25, 15);
    }
}

// =============================================================================
// Absorb Mode — pull actors + AT collider damage + shrink + Freezard suction VFX
// =============================================================================

static void GustJar_Absorb(Player* player, PlayState* play, Vec3f* nozzle, s16 aimYaw) {
    gjHeatTimer++;

    // Looping wind sound
    func_8002F974(&player->actor, NA_SE_EV_WIND_TRAP - SFX_FLAG);

    // Freezard-style suction VFX (cone of particles toward nozzle)
    GustJar_SpawnSuckVFX(play, nozzle, aimYaw);

    // Position AT collider at nozzle for damage
    ColliderCylinder* col = &gjCollider;
    col->dim.pos.x = (s16)nozzle->x;
    col->dim.pos.y = (s16)nozzle->y;
    col->dim.pos.z = (s16)nozzle->z;
    col->base.atFlags |= AT_ON | AT_TYPE_PLAYER;
    CollisionCheck_SetAT(play, &play->colChkCtx, &col->base);
    if (col->base.atFlags & AT_HIT) {
        col->base.atFlags &= ~AT_HIT;
    }

    // Pull + shrink actors toward nozzle
    f32 rangeSq = SQ(GUST_RANGE_MAX);
    f32 shrinkStart = 120.0f; // Start shrinking at this distance
    ActorCategory categories[] = { ACTORCAT_ENEMY, ACTORCAT_PROP };
    for (s32 c = 0; c < 2; c++) {
        Actor* actor = play->actorCtx.actorLists[categories[c]].head;
        while (actor != NULL) {
            Actor* next = actor->next;
            if (actor->update != NULL && GustJar_IsSuckable(actor)) {
                f32 dx = nozzle->x - actor->world.pos.x;
                f32 dz = nozzle->z - actor->world.pos.z;
                f32 distXZSq = SQ(dx) + SQ(dz);

                if (distXZSq < rangeSq) {
                    f32 dy = fabsf(nozzle->y - actor->world.pos.y);
                    if (dy < LINK_HEIGHT_HITBOX) {
                        f32 norm = sqrtf(distXZSq);
                        if (norm > 1.0f) {
                            // Pull toward nozzle
                            f32 strength = 8.0f;
                            f32 invNorm = strength / norm;
                            actor->world.pos.x += dx * invNorm;
                            actor->world.pos.z += dz * invNorm;
                            if (actor->bgCheckFlags & BGCHECKFLAG_GROUND)
                                actor->world.pos.y += 2.0f;

                            // Shrink as they get closer
                            if (norm < shrinkStart) {
                                f32 factor = norm / shrinkStart;
                                if (factor < 0.15f)
                                    factor = 0.15f;
                                GustJar_ShrinkActor(actor, factor);
                            }
                        }
                    }
                }
            }
            actor = next;
        }
    }

    // Check overheat → transition to blow
    if (gjHeatTimer >= GUST_HEAT_MAX) {
        GustJar_ClearScaleCache();
        gjMode = GUST_MODE_BLOW;
        gjBlowActive = 1;
        gjBlowTimer = GUST_BLOW_DURATION;
        Audio_StopSfxById(NA_SE_EV_WIND_TRAP);
        Player_PlaySfx(player, NA_SE_PL_MAGIC_WIND_NORMAL);
    }
}

// =============================================================================
// Blow Mode — elemental cone VFX + strong push + collider sweep for env effects
// =============================================================================

// Element → AT collider dmgFlags mapping (for triggering torches, sun switches, etc.)
static u32 GustJar_GetElementDmgFlags(u8 element) {
    switch (element) {
        case GUST_ELEMENT_FIRE:
            return 0x00020800; // DMG_ARROW_FIRE | DMG_MAGIC_FIRE → lights torches
        case GUST_ELEMENT_ICE:
            return 0x00041000; // DMG_ARROW_ICE | DMG_MAGIC_ICE
        case GUST_ELEMENT_LIGHT:
            return 0x00282000; // DMG_ARROW_LIGHT | DMG_MAGIC_LIGHT | DMG_MIR_RAY → sun switches
        case GUST_ELEMENT_SHADOW:
            return 0x00020000; // DMG_MAGIC_FIRE (shadow burns)
        case GUST_ELEMENT_SPIRIT:
            return 0x00080000; // DMG_MAGIC_LIGHT
        case GUST_ELEMENT_WIND:
        default:
            return 0x00000048; // DMG_HAMMER_SWING | DMG_EXPLOSIVE (base push)
    }
}

// Element effects on regular enemies (NOT bosses — bosses use AT collider dmgFlags natively).
// Twinrova/Ganon stuns happen via the AT collider sweep with correct dmgFlags,
// not through freezeTimer (which they ignore).
static void GustJar_ApplyElementEffect(Actor* actor, PlayState* play, u8 element) {
    // Only affect regular enemies, not bosses
    if (actor->category != ACTORCAT_ENEMY)
        return;

    switch (element) {
        case GUST_ELEMENT_SHADOW:
            // Paralyze all small/medium enemies
            if (actor->colChkInfo.health <= 12) {
                actor->freezeTimer = 60;
                Actor_SetColorFilter(actor, 0x8000, 255, 0x2000, 60);
            }
            break;
        case GUST_ELEMENT_FIRE:
            // Burn enemies
            actor->freezeTimer = 30;
            Actor_SetColorFilter(actor, 0x4000, 255, 0x2000, 30);
            break;
        case GUST_ELEMENT_ICE:
            // Freeze enemies
            actor->freezeTimer = 80;
            Actor_SetColorFilter(actor, 0, 255, 0x2000, 80);
            break;
        case GUST_ELEMENT_LIGHT:
            // Stun enemies with light
            actor->freezeTimer = 50;
            Actor_SetColorFilter(actor, 0, 255, 0x2000, 50);
            break;
        case GUST_ELEMENT_SPIRIT:
            // Spirit stun (orange tint)
            actor->freezeTimer = 60;
            Actor_SetColorFilter(actor, 0x4000, 200, 0x2000, 60);
            break;
        default:
            break;
    }
}

static void GustJar_Blow(Player* player, PlayState* play, Vec3f* nozzle, s16 aimYaw) {
    gjBlowTimer--;

    // Freezard-style blow VFX
    GustJar_SpawnBlowVFX(play, nozzle, aimYaw, gjElement);

    // Wind sound
    func_8002F974(&player->actor, NA_SE_EV_WIND_TRAP - SFX_FLAG);

    // === AT COLLIDER SWEEP along cone for environmental effects ===
    // Place collider at 3 positions along the cone so torches/sun switches get hit
    ColliderCylinder* col = &gjCollider;
    u32 elemDmgFlags = GustJar_GetElementDmgFlags(gjElement);
    col->info.toucher.dmgFlags = elemDmgFlags;
    col->info.toucher.damage = 0; // VFX only, no HP damage
    col->info.toucher.effect = 0;
    col->base.atFlags |= AT_ON | AT_TYPE_PLAYER;

    // Sweep 3 positions: near(40), mid(100), far(160) along aim direction
    static const f32 sweepDists[] = { 40.0f, 100.0f, 160.0f };
    for (s32 s = 0; s < 3; s++) {
        col->dim.pos.x = (s16)(nozzle->x + Math_SinS(aimYaw) * sweepDists[s]);
        col->dim.pos.y = (s16)(nozzle->y);
        col->dim.pos.z = (s16)(nozzle->z + Math_CosS(aimYaw) * sweepDists[s]);
        col->dim.radius = 40; // Wider than default
        CollisionCheck_SetAT(play, &play->colChkCtx, &col->base);
    }
    if (col->base.atFlags & AT_HIT) {
        col->base.atFlags &= ~AT_HIT;
    }

    // === STRONG PUSH on all ACTORCAT_ENEMY in cone (NOT bosses) ===
    f32 pushForce = (gjElement == GUST_ELEMENT_WIND) ? 40.0f : 20.0f;
    f32 rangeSq = SQ(GUST_RANGE_BLOW);

    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].head;
    while (actor != NULL) {
        Actor* next = actor->next;
        if (actor->update != NULL) {
            f32 dx = actor->world.pos.x - nozzle->x;
            f32 dz = actor->world.pos.z - nozzle->z;
            f32 distSq = SQ(dx) + SQ(dz);

            if (distSq < rangeSq && distSq > 1.0f) {
                s16 angleToActor = Math_Atan2S(dx, dz);
                s16 angleDiff = angleToActor - aimYaw;
                if (angleDiff < 0)
                    angleDiff = -angleDiff;
                if (angleDiff > 0x7FFF)
                    angleDiff = (s16)(0xFFFF - angleDiff);

                if (angleDiff < GUST_BLOW_CONE_HALF_ANGLE) {
                    f32 dist = sqrtf(distSq);

                    // Strong push — direct position displacement + velocity
                    f32 force = pushForce * (1.0f - dist / GUST_RANGE_BLOW);
                    if (force < 2.0f)
                        force = 2.0f;
                    actor->world.pos.x += (dx / dist) * force;
                    actor->world.pos.z += (dz / dist) * force;
                    actor->velocity.x += (dx / dist) * force * 0.5f;
                    actor->velocity.z += (dz / dist) * force * 0.5f;
                    actor->velocity.y += 5.0f;
                    actor->bgCheckFlags &= ~BGCHECKFLAG_GROUND; // Lift off ground

                    // Element-specific stun/freeze
                    GustJar_ApplyElementEffect(actor, play, gjElement);
                }
            }
        }
        actor = next;
    }

    // Blow timer expired
    if (gjBlowTimer <= 0) {
        gjMode = GUST_MODE_IDLE;
        gjBlowActive = 0;
        gjHeatTimer = 0;
        gjCooldownTimer = GUST_COOLDOWN;
        Audio_StopSfxById(NA_SE_EV_WIND_TRAP);
    }
}

// Element selection is handled in Kaleido (z_kaleido_item.c), not during gameplay.

// =============================================================================
// Draw
// =============================================================================

void CustomItems_DrawGustJar(Player* this, PlayState* play) {
    GustJarPot_Draw(this, play);
}

// =============================================================================
// Main Handler
// =============================================================================

void Handle_GustJar(Player* this, PlayState* play) {
    // Ensure collider is initialized
    if (gjCollider.base.shape != COLSHAPE_CYLINDER) {
        Player_InitGustJarIA(play, this);
    }

    ItemInputState input;
    static s8 prevInvincibility = 0;
    ItemInput_Update(&input, ITEM_GUST_JAR, this, play);

    if (!input.wasEquipped) {
        if (gjEquipped)
            GustJar_Unequip(play, this);
        return;
    }
    if (ItemInput_IsBlocked(this, play)) {
        if (gjEquipped)
            GustJar_Unequip(play, this);
        return;
    }

    gjButtonMask = input.equippedButton;

    if (ItemInput_CheckDamage(this, &prevInvincibility)) {
        GustJar_Unequip(play, this);
        return;
    }

    u8 btnPressed = input.isPressed;
    u8 btnHeld = input.isHeld;

    if (!gjEquipped) {
        if (btnPressed) {
            GustJar_Equip(play, this);
            if (!gjFirstPerson)
                return;
        } else {
            return;
        }
    }

    // C-Up toggles aim mode
    if (CHECK_BTN_ALL(play->state.input[0].press.button, BTN_CUP)) {
        if (gjAimMode == 2) {
            if (Player_IsZTargeting(this)) {
                gjAimMode = 1;
            } else {
                gjAimMode = 0;
                FirstPerson_Init(this, play);
                gjFirstPerson = 1;
            }
        } else {
            if (gjFirstPerson) {
                FirstPerson_Exit(this, play);
                gjFirstPerson = 0;
            }
            gjAimMode = 2;
        }
        ItemEquip_PlayEquipSFX(play, this);
        return;
    }

    // Other button pressed → unequip
    if (input.otherButtonPressed) {
        GustJar_Unequip(play, this);
        return;
    }

    // Auto-switch aim modes based on Z-targeting
    u8 isZTargeting = Player_IsZTargeting(this);
    if (gjAimMode == 0 && isZTargeting) {
        FirstPerson_Exit(this, play);
        gjFirstPerson = 0;
        gjAimMode = 1;
    } else if (gjAimMode == 1 && !isZTargeting) {
        FirstPerson_Init(this, play);
        gjFirstPerson = 1;
        gjAimMode = 0;
    }

    // First-person update
    if (gjAimMode == 0 && gjFirstPerson) {
        FirstPerson_Update(this, play);
    }

    // Calculate nozzle position
    s16 aimYaw = GustJar_GetAimYaw(play, this);
    s16 aimPitch = gjFirstPerson ? FirstPerson_GetAimPitch(this) : 0;
    Vec3f nozzle = this->actor.world.pos;
    nozzle.y += 25.0f;
    f32 hDist = 35.0f * Math_CosS(aimPitch);
    nozzle.x += Math_SinS(aimYaw) * hDist;
    nozzle.z += Math_CosS(aimYaw) * hDist;
    nozzle.y -= Math_SinS(aimPitch) * 35.0f;

    // Cooldown tick
    if (gjCooldownTimer > 0) {
        gjCooldownTimer--;
    }

    // Heat decay when idle (not absorbing)
    if (gjMode == GUST_MODE_IDLE && gjHeatTimer > 0) {
        gjHeatTimer -= 2;
        if (gjHeatTimer < 0)
            gjHeatTimer = 0;
    }

    // ===== BLOW MODE (automatic, runs until timer expires) =====
    if (gjMode == GUST_MODE_BLOW) {
        GustJar_Blow(this, play, &nozzle, aimYaw);
        return;
    }

    // ===== ABSORB MODE (hold C-button) =====
    static u8 wasHeld = 0;
    u8 isHeld = btnHeld && !btnPressed;

    if (wasHeld && !btnHeld) {
        // Released button → back to idle
        if (gjMode == GUST_MODE_ABSORB) {
            GustJar_ClearScaleCache();
            gjMode = GUST_MODE_IDLE;
        }
        wasHeld = 0;
        return;
    }

    if (isHeld && gjCooldownTimer <= 0) {
        if (gjMode == GUST_MODE_IDLE) {
            gjMode = GUST_MODE_ABSORB;
        }
        if (gjMode == GUST_MODE_ABSORB) {
            GustJar_Absorb(this, play, &nozzle, aimYaw);
        }
        wasHeld = 1;
    }
}
