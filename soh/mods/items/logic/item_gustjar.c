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

// Returns the medallion ITEM_* matching the gust jar's current element, or -1
// if the element is WIND (default — no medallion overlay needed).
s32 GustJar_GetActiveMedallionItem(void) {
    switch (gjElement) {
        case GUST_ELEMENT_FIRE:   return ITEM_MEDALLION_FIRE;
        case GUST_ELEMENT_ICE:    return ITEM_MEDALLION_WATER;
        case GUST_ELEMENT_SHADOW: return ITEM_MEDALLION_SHADOW;
        case GUST_ELEMENT_SPIRIT: return ITEM_MEDALLION_SPIRIT;
        case GUST_ELEMENT_LIGHT:  return ITEM_MEDALLION_LIGHT;
        default:                  return -1; // WIND or unknown
    }
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

    // Equip does NOT enter first-person. Link starts in "holding the gust jar"
    // free-roam state with the carry pose applied each frame. C-Up explicitly
    // toggles first-person aim. Z-target works independently (rotates Link
    // toward focusActor like the bow, without forcing first-person).
    gjFirstPerson = 0;
    gjAimMode = 0;
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
    // Priority: Z-target (lock-on) → first-person → Link's facing yaw.
    if (Player_IsZTargeting(player) && player->focusActor != NULL) {
        return Math_Vec3f_Yaw(&player->actor.world.pos, &player->focusActor->focus.pos);
    }
    if (gjFirstPerson) {
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

    // Cap heat at max — DO NOT auto-transition to blow. The blow is now
    // manual: SUCK direction fires it on C-release (proportional to charge);
    // BLOW direction fires it directly on C-hold. Per user request: blow
    // never releases automatically.
    if (gjHeatTimer > GUST_HEAT_MAX) {
        gjHeatTimer = GUST_HEAT_MAX;
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
        // No cooldown — the old GUST_COOLDOWN (120 frames) was for the legacy
        // auto-overheat behaviour. With manual SUCK/BLOW direction control,
        // the user should be able to immediately press C again to suck or
        // blow without a 2-second lockout.
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
// Hold Pose — override Link's arm joints with frame 8 of carryB_free
// =============================================================================
//
// While the gust jar is equipped, Link visually holds it in front of his chest
// using the vanilla "carry pot with both hands" pose (gPlayerAnim_link_normal_
// carryB_free at frame 8). We extract that exact frame's joint rotations and
// copy ONLY the 6 arm bones (shoulder/forearm/hand × L/R) on top of whatever
// animation Link is currently playing — walk, run, idle, jump, etc. all
// continue normally; only the arms snap to the carry pose. Same pattern as
// item_ballchain.c (BallChain_SetEquipPose), but data-driven from the anim
// asset instead of hardcoded magic numbers.

static void GustJar_ApplyCarryPose(Player* player, PlayState* play) {
    Vec3s frameBuf[PLAYER_LIMB_MAX];

    // AnimationContext_SetLoadFrame does an immediate synchronous memcpy of
    // the frame's joint table into frameBuf (z_skelanime.c:909) — values are
    // available right away.
    AnimationContext_SetLoadFrame(play, (LinkAnimationHeader*)&gPlayerAnim_link_normal_carryB_free,
                                  8, PLAYER_LIMB_MAX, frameBuf);

    player->skelAnime.jointTable[PLAYER_LIMB_L_SHOULDER] = frameBuf[PLAYER_LIMB_L_SHOULDER];
    player->skelAnime.jointTable[PLAYER_LIMB_L_FOREARM]  = frameBuf[PLAYER_LIMB_L_FOREARM];
    player->skelAnime.jointTable[PLAYER_LIMB_L_HAND]     = frameBuf[PLAYER_LIMB_L_HAND];
    player->skelAnime.jointTable[PLAYER_LIMB_R_SHOULDER] = frameBuf[PLAYER_LIMB_R_SHOULDER];
    player->skelAnime.jointTable[PLAYER_LIMB_R_FOREARM]  = frameBuf[PLAYER_LIMB_R_FOREARM];
    player->skelAnime.jointTable[PLAYER_LIMB_R_HAND]     = frameBuf[PLAYER_LIMB_R_HAND];
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
            // Equip and fall through to the rest of Handle_GustJar so the
            // pose, Z-target rotation, and C-button absorb/blow handler all
            // run on the SAME frame as the equip. The old early-return only
            // made sense when equip auto-entered first-person and needed a
            // settle frame — the new flow never does that, so returning here
            // just delays the first suck/blow by one frame and occasionally
            // dropped fast C taps.
            GustJar_Equip(play, this);
        } else {
            return;
        }
    }

    // C-Up: explicit toggle of first-person aim. No other state changes here —
    // Z-target rotation, pose override, and cone aim are handled separately
    // and work in BOTH first-person and free-roam states.
    if (CHECK_BTN_ALL(play->state.input[0].press.button, BTN_CUP)) {
        if (gjFirstPerson) {
            FirstPerson_Exit(this, play);
            gjFirstPerson = 0;
            gjAimMode = 0;
        } else {
            FirstPerson_Init(this, play);
            gjFirstPerson = 1;
            gjAimMode = 0;
            this->unk_834 = 14; // 14-frame camera transition (bow pattern)
        }
        ItemEquip_PlayEquipSFX(play, this);
        return;
    }

    // Other button pressed → unequip. R is excluded here (locally for the
    // gust jar): R is used by the in-game element cycle / shield-suppress
    // logic and must NOT trigger an unequip. We re-derive the check inline
    // instead of trusting ItemInput_CheckOtherButtons, which lists R as an
    // "action button" globally for all other items.
    {
        static const u16 sGjUnequipButtons = BTN_A | BTN_B | BTN_START | BTN_CLEFT | BTN_CDOWN | BTN_CRIGHT |
                                             BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT;
        u16 mask = sGjUnequipButtons & ~input.equippedButton;
        if (play->state.input[0].press.button & mask) {
            GustJar_Unequip(play, this);
            return;
        }
    }

    // Z-target rotation (bow-style): smooth-rotate Link toward the focusActor
    // when locked on. Works regardless of first-person state. This matches the
    // bow's Z-target behaviour where Player_GetMovementSpeedAndYaw aligns the
    // player yaw to focusActor every frame without forcing first-person.
    u8 isZTargeting = Player_IsZTargeting(this);
    if (isZTargeting && this->focusActor != NULL) {
        s16 targetYaw = Math_Vec3f_Yaw(&this->actor.world.pos, &this->focusActor->focus.pos);
        Math_ScaledStepToS(&this->actor.shape.rot.y, targetYaw, 0x800);
        this->actor.world.rot.y = this->actor.shape.rot.y;
        this->yaw = this->actor.shape.rot.y;
    }

    // First-person update — only when explicitly toggled on via C-Up.
    if (gjFirstPerson) {
        FirstPerson_Update(this, play);
    }

    // Hold pose — override Link's arm joints every frame so he visually holds
    // the gust jar in front of him with both hands, regardless of what
    // animation his lower body is playing (walk, run, idle, jump, ...).
    GustJar_ApplyCarryPose(this, play);

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

    // ===== BLOW MODE dispatch =====
    // Two flavors share GUST_MODE_BLOW:
    //   - SUCK direction: timed blow set on C-release. Runs until gjBlowTimer
    //     decays to 0 (handled inside GustJar_Blow).
    //   - BLOW direction (manual): hold C blows continuously. Release C stops
    //     immediately (no timer involvement). Heat is not consumed here.
    if (gjMode == GUST_MODE_BLOW) {
        if (gjBlowDir == GUST_DIR_BLOW && !btnHeld) {
            // Manual blow ended — C released. Reset to IDLE.
            GustJar_ClearScaleCache();
            gjMode = GUST_MODE_IDLE;
            gjBlowActive = 0;
            gjBlowTimer = 0;
            Audio_StopSfxById(NA_SE_EV_WIND_TRAP);
            return;
        }
        GustJar_Blow(this, play, &nozzle, aimYaw);
        return;
    }

    // ===== L+R COMBO: toggle SUCK/BLOW direction =====
    // L+R simultaneously toggles gjBlowDir between SUCK (default — hold C
    // absorbs, release C blows proportional to charge) and BLOW (manual —
    // hold C directly blows with current element, no charge mechanic).
    // Works in any mode while the gust jar is equipped.
    u8 lrCurr = CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_L) &&
                CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_R);
    u8 lrPress = CHECK_BTN_ALL(play->state.input[0].press.button, BTN_L) ||
                 CHECK_BTN_ALL(play->state.input[0].press.button, BTN_R);
    u8 lrToggled = 0;
    if (lrCurr && lrPress) {
        gjBlowDir = (gjBlowDir == GUST_DIR_SUCK) ? GUST_DIR_BLOW : GUST_DIR_SUCK;
        Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &this->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        lrToggled = 1;

        // Reset to a clean IDLE so the new direction takes effect on the
        // next C action — otherwise toggling mid-ABSORB or mid-BLOW leaves
        // gjMode set in the OLD direction and the next C-press silently
        // does nothing (user-reported bug after SUCK→BLOW toggle).
        if (gjMode == GUST_MODE_ABSORB || gjMode == GUST_MODE_BLOW) {
            GustJar_ClearScaleCache();
            Audio_StopSfxById(NA_SE_EV_WIND_TRAP);
            gjMode = GUST_MODE_IDLE;
            gjBlowActive = 0;
            gjBlowTimer = 0;
            gjHeatTimer = 0;
        }

        // Consume L/R so neither the cycle nor the shield handler also fires.
        play->state.input[0].cur.button &= ~(BTN_L | BTN_R);
        play->state.input[0].press.button &= ~(BTN_L | BTN_R);
    }

    // ===== SINGLE R/L: cycle element =====
    // Works ANY time the gust jar is equipped (IDLE, ABSORB, BLOW — any
    // state, any aim mode). Skipped if the L+R combo just toggled the
    // direction this frame.
    if (!lrToggled) {
        s8 cycleDir = 0;
        if (CHECK_BTN_ALL(play->state.input[0].press.button, BTN_R)) {
            cycleDir = 1;
        } else if (CHECK_BTN_ALL(play->state.input[0].press.button, BTN_L)) {
            cycleDir = -1;
        }

        if (cycleDir != 0) {
            static const s32 sCycleQuestItems[] = { QUEST_MEDALLION_FIRE, QUEST_MEDALLION_WATER,
                                                    QUEST_MEDALLION_SHADOW, QUEST_MEDALLION_SPIRIT,
                                                    QUEST_MEDALLION_LIGHT };
            static const u8 sCycleElements[] = { GUST_ELEMENT_FIRE, GUST_ELEMENT_ICE, GUST_ELEMENT_SHADOW,
                                                 GUST_ELEMENT_SPIRIT, GUST_ELEMENT_LIGHT };
            u8 available[6];
            u8 count = 0;
            available[count++] = GUST_ELEMENT_WIND;
            for (s32 i = 0; i < 5; i++) {
                if (CHECK_QUEST_ITEM(sCycleQuestItems[i])) {
                    available[count++] = sCycleElements[i];
                }
            }

            if (count > 1) {
                u8 currentIdx = 0;
                for (u8 i = 0; i < count; i++) {
                    if (available[i] == gjElement) {
                        currentIdx = i;
                        break;
                    }
                }
                if (cycleDir > 0) {
                    currentIdx = (currentIdx + 1) % count;
                } else {
                    currentIdx = (currentIdx + count - 1) % count;
                }
                gjElement = available[currentIdx];
                Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &this->actor.world.pos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
        }
    }

    // Defensive R/L swallow on play->state.input[0]: the sp44 copy is filtered
    // earlier by GustJar_FilterPlayerInput (z_player.c, before Player_UpdateCommon),
    // which prevents shield. This pass also clears the raw input so downstream
    // consumers in the same frame (other custom items, ExtEquip, etc.) don't
    // re-trigger off the same R/L press. Fires whenever the gust jar is the
    // active C-button item.
    if (gjEquipped) {
        play->state.input[0].cur.button &= ~(BTN_L | BTN_R);
        play->state.input[0].press.button &= ~(BTN_L | BTN_R);
    }

    // ===== C-BUTTON HANDLING (direction-aware) =====
    static u8 wasHeld = 0;
    u8 isHeld = btnHeld && !btnPressed;

    if (wasHeld && !btnHeld) {
        // C released. Two cases by direction:
        if (gjBlowDir == GUST_DIR_SUCK && gjMode == GUST_MODE_ABSORB) {
            // SUCK direction: discharge the accumulated charge as a timed blow.
            // Blow duration = gjHeatTimer / 2 (half the suck time). If no
            // charge was built, no blow — just return to IDLE.
            s16 blowDuration = gjHeatTimer / 2;
            GustJar_ClearScaleCache();
            if (blowDuration > 0) {
                gjMode = GUST_MODE_BLOW;
                gjBlowActive = 1;
                gjBlowTimer = blowDuration;
                Audio_StopSfxById(NA_SE_EV_WIND_TRAP);
                Player_PlaySfx(this, NA_SE_PL_MAGIC_WIND_NORMAL);
            } else {
                gjMode = GUST_MODE_IDLE;
            }
            gjHeatTimer = 0;
        } else if (gjMode == GUST_MODE_ABSORB) {
            // BLOW direction with C released from absorb (shouldn't normally
            // happen since BLOW direction starts blow directly, but safe path).
            GustJar_ClearScaleCache();
            gjMode = GUST_MODE_IDLE;
            gjHeatTimer = 0;
        }
        wasHeld = 0;
        return;
    }

    if (isHeld && gjCooldownTimer <= 0) {
        if (gjMode == GUST_MODE_IDLE) {
            // Start suck or blow based on direction.
            if (gjBlowDir == GUST_DIR_BLOW) {
                gjMode = GUST_MODE_BLOW;
                gjBlowActive = 1;
                gjBlowTimer = 0x7FFF; // sentinel — manual mode reads btnHeld, not timer
                Player_PlaySfx(this, NA_SE_PL_MAGIC_WIND_NORMAL);
            } else {
                gjMode = GUST_MODE_ABSORB;
            }
        }
        if (gjMode == GUST_MODE_ABSORB) {
            GustJar_Absorb(this, play, &nozzle, aimYaw);
        }
        wasHeld = 1;
    }
}

// Called from Player_Update BEFORE Player_UpdateCommon runs — strips L/R from
// the local sp44 copy so the vanilla shield action handler (which reads
// sControlInput->cur.button, the COPY) never sees the press. Handle_GustJar
// runs much later in the frame and was too late to prevent shield latching.
//
// The actual in-game cycle logic in Handle_GustJar reads play->state.input[0]
// (NOT this filtered copy), so cycling/L+R combo still see the raw bits.
//
// Gate: gjEquipped. Whenever the gust jar is the live C-button item, R must
// never raise the shield — R/L are reserved for element cycling (IDLE+aim) or
// the L+R combo (ABSORB). This covers ALL aim modes: first-person (gjAimMode
// 0 / gjFirstPerson), Z-target (gjAimMode 1), and static (gjAimMode 2).
// Independent of the NeiAimCycle CVar — the user explicitly asked: while
// gust jar is in play, R never shields. Period.
void GustJar_FilterPlayerInput(Input* input) {
    if (input == NULL) {
        return;
    }
    if (!gjEquipped) {
        return;
    }
    input->cur.button &= ~(BTN_L | BTN_R);
    input->press.button &= ~(BTN_L | BTN_R);
    input->rel.button &= ~(BTN_L | BTN_R);
}
