/**
 * sw97_player_behavior.inc.c - Player behavior hooks for SW97 Medallion Spells
 *
 * Original actors: z64proto/sw97 team (Spaceworld '97 Experience)
 * Adapted for Ship of Harkinian (Shipwright)
 *
 * Provides CVar-gated hooks for:
 * - Magic spell actor spawning (6 spells mapped to spell indices 0-5)
 * - Helper functions for medallion/arrow item identification
 * - Medallion-to-arrow item conversion
 */

// Runtime actor IDs (set by sw97_init.cpp via ActorDB)
extern s16 gSw97ActorId_MagicFire;
extern s16 gSw97ActorId_MagicIce;
extern s16 gSw97ActorId_MagicLight;
extern s16 gSw97ActorId_MagicDark;
extern s16 gSw97ActorId_MagicSoul;
extern s16 gSw97ActorId_MagicWind;
extern s16 gSw97ActorId_ArrowFire;
extern s16 gSw97ActorId_ArrowIce;
extern s16 gSw97ActorId_ArrowLight;
extern s16 gSw97ActorId_ArrowDark;
extern s16 gSw97ActorId_ArrowSoul;
extern s16 gSw97ActorId_ArrowWind;

// SW97 magic spell costs: indices 0-5 match Player_ActionToMagicSpell output
// 0=Wind(12), 1=Soul(24), 2=Dark(12), 3=Ice(24), 4=Light(24), 5=Fire(12)
static u8 sSw97MagicSpellCosts[] = { 12, 24, 12, 24, 24, 12 };

// SW97 magic arrow costs: all 4 except light which is 8
static u8 sSw97MagicArrowCosts[] = { 4, 4, 8, 4, 4, 4 };

/**
 * Spawn the correct SW97 magic spell actor based on spell index.
 * Called from Player_SpawnMagicSpell in z_player.c.
 *
 * Spell index mapping (from Player_ActionToMagicSpell):
 *   0 = IA_MAGIC_SPELL_15 = Forest Medallion → MagicWind
 *   1 = IA_MAGIC_SPELL_16 = Spirit Medallion → MagicSoul
 *   2 = IA_MAGIC_SPELL_17 = Shadow Medallion → MagicDark
 *   3 = IA_FARORES_WIND   = Water Medallion  → MagicIce
 *   4 = IA_NAYRUS_LOVE    = Light Medallion  → MagicLight
 *   5 = IA_DINS_FIRE       = Fire Medallion   → MagicFire
 *
 * Returns the spawned actor, or NULL if SW97 spells are disabled.
 */
static Actor* Sw97_TrySpawnMagicSpell(PlayState* play, Player* player, s32 spell) {
    if (!SW97_MEDALLIONS_ENABLED()) {
        return NULL;
    }

    if (spell < 0 || spell >= 6) {
        return NULL;
    }

    // Shadow medallion heart→magic exchange is handled out-of-band in
    // soh/Enhancements/ShadowMedallionExchange.cpp via an OnPlayerUpdate hook,
    // so the exchange works even when the player has zero magic (otherwise the
    // cast flow short-circuits before reaching this function).

    s16* spellActorIds[] = {
        &gSw97ActorId_MagicWind,  // 0 = Forest
        &gSw97ActorId_MagicSoul,  // 1 = Spirit
        &gSw97ActorId_MagicDark,  // 2 = Shadow
        &gSw97ActorId_MagicIce,   // 3 = Water
        &gSw97ActorId_MagicLight, // 4 = Light
        &gSw97ActorId_MagicFire,  // 5 = Fire
    };

    s16 actorId = *spellActorIds[spell];
    if (actorId < 0) {
        return NULL;
    }

    Actor* spawned = Actor_Spawn(&play->actorCtx, play, actorId, player->actor.world.pos.x,
                                 player->actor.world.pos.y, player->actor.world.pos.z, 0, 0, 0, 0);

    // Tell teammates to spawn the same spell-effect actor on their side.
    // Spells follow the caster (attached_to_owner=1) — their visual stays
    // around the caster's dummy as long as the spell is active. Map the
    // spell index back to the corresponding HARPOON_VFX_KIND_SW97_MAGIC_*.
    if (spawned != NULL) {
        s32 vfxKindByIndex[] = {
            HARPOON_VFX_KIND_SW97_MAGIC_WIND,   // 0
            HARPOON_VFX_KIND_SW97_MAGIC_SOUL,   // 1
            HARPOON_VFX_KIND_SW97_MAGIC_DARK,   // 2
            HARPOON_VFX_KIND_SW97_MAGIC_ICE,    // 3
            HARPOON_VFX_KIND_SW97_MAGIC_LIGHT,  // 4
            HARPOON_VFX_KIND_SW97_MAGIC_FIRE,   // 5
        };
        Harpoon_NotifyVfxSpawn(spawned, vfxKindByIndex[spell], /*attachedToOwner=*/1);
    }
    return spawned;
}

/**
 * Check if an item ID is a quest medallion (spell mode).
 */
static s32 Sw97_IsMedallionItem(s32 item) {
    return (item >= ITEM_MEDALLION_FOREST && item <= ITEM_MEDALLION_LIGHT);
}

/**
 * Check if an item ID is an SW97 arrow variant (arrow mode).
 */
static s32 Sw97_IsArrowItem(s32 item) {
    return (item >= ITEM_SW97_ARROW_FIRE && item <= ITEM_SW97_ARROW_WIND);
}

/**
 * Convert a medallion item to its corresponding SW97 arrow item.
 * Used by L+C swap in z_player.c.
 */
s32 Sw97_MedallionToArrowItem(s32 medallionItem) {
    switch (medallionItem) {
        case ITEM_MEDALLION_FIRE:
            return ITEM_SW97_ARROW_FIRE;
        case ITEM_MEDALLION_WATER:
            return ITEM_SW97_ARROW_ICE;
        case ITEM_MEDALLION_LIGHT:
            return ITEM_SW97_ARROW_LIGHT;
        case ITEM_MEDALLION_SHADOW:
            return ITEM_SW97_ARROW_DARK;
        case ITEM_MEDALLION_SPIRIT:
            return ITEM_SW97_ARROW_SOUL;
        case ITEM_MEDALLION_FOREST:
            return ITEM_SW97_ARROW_WIND;
        default:
            return ITEM_NONE;
    }
}

/**
 * Returns true while the Shadow Medallion spell (MagicDark) is active.
 * MagicDark drives gSaveContext.nayrusLoveTimer for its lifetime; in SW97 mode
 * the Shadow medallion replaces Nayru's Love (Light medallion is the new NL slot),
 * so a nonzero timer + SW97 enabled uniquely identifies "Shadow stealth is on".
 *
 * Consumed by z_actor.c so enemies/NPCs can't detect Link (same hook point as
 * MmMaskWear_IsStoneMaskActive).
 */
s32 Sw97_ShadowStealthActive(void) {
    if (!SW97_MEDALLIONS_ENABLED()) return 0;
    return gSaveContext.nayrusLoveTimer > 0;
}

/**
 * Shadow Medallion heart→magic exchange.
 *
 * Hold the C-button that has ITEM_MEDALLION_SHADOW for SHADOW_EXCHANGE_HOLD_FRAMES
 * frames → spend 3 hearts, gain 24 magic. Disarmed until release.
 *
 * Must work even at zero magic (the vanilla cast pipeline short-circuits before
 * reaching Sw97_TrySpawnMagicSpell when magic is insufficient — playing only the
 * "no magic" error sound — so the exchange has to live in a per-frame tick).
 *
 * Called from z_player.c Player_UpdateCommon each frame.
 */
#define SHADOW_EXCHANGE_HOLD_FRAMES 20
#define SHADOW_EXCHANGE_HEART_COST  (3 * 0x10)  // 3 hearts × 16 HP
#define SHADOW_EXCHANGE_MAGIC_GAIN  24

void Sw97_TickShadowExchange(PlayState* play, Player* player) {
    if (!SW97_MEDALLIONS_ENABLED()) return;
    if (play == NULL || player == NULL) return;

    // Find which C-slot has the Shadow medallion. buttonItems[0]=B, [1..3]=C-LDR.
    u16 medallionMask = 0;
    if (gSaveContext.equips.buttonItems[1] == ITEM_MEDALLION_SHADOW) medallionMask |= BTN_CLEFT;
    if (gSaveContext.equips.buttonItems[2] == ITEM_MEDALLION_SHADOW) medallionMask |= BTN_CDOWN;
    if (gSaveContext.equips.buttonItems[3] == ITEM_MEDALLION_SHADOW) medallionMask |= BTN_CRIGHT;

    static s16 sShadowHoldFrames = 0;
    static u8 sShadowExchanged = 0;

    if (medallionMask == 0) {
        sShadowHoldFrames = 0;
        sShadowExchanged = 0;
        return;
    }

    u16 cur = play->state.input[0].cur.button;
    if (!(cur & medallionMask)) {
        sShadowHoldFrames = 0;
        sShadowExchanged = 0;
        return;
    }

    sShadowHoldFrames++;
    if (sShadowExchanged) return;
    if (sShadowHoldFrames < SHADOW_EXCHANGE_HOLD_FRAMES) return;
    if (gSaveContext.health <= SHADOW_EXCHANGE_HEART_COST) return;

    gSaveContext.health -= SHADOW_EXCHANGE_HEART_COST;
    gSaveContext.magic += SHADOW_EXCHANGE_MAGIC_GAIN;
    if (gSaveContext.magic > gSaveContext.magicCapacity) {
        gSaveContext.magic = gSaveContext.magicCapacity;
    }
    Audio_PlayActorSound2(&player->actor, NA_SE_SY_GET_RUPY);
    sShadowExchanged = 1;
}

/**
 * Shadow-element blindness — per-actor stealth.
 *
 * When Shadow ARROW (ARROW_SW97_0C) hits an enemy, OR when the Gust Jar's
 * Shadow-element BLOW pushes an enemy, the target is "blinded" for ~10
 * seconds: z_actor.c's distance-to-player calculation is spoofed to 32000
 * (same mechanism as Stone Mask + Shadow Medallion stealth), so the enemy
 * stops tracking Link until the timer expires.
 *
 * Storage is a small static table indexed by Actor*. Capacity 32 is plenty
 * for the worst-case crowd you'd reasonably blind in one fight. New tags
 * upsert (longer-of duration); expired slots are reused.
 *
 * `Sw97_TickBlindness` MUST be called once per frame from z_player.c so
 * `framesRemaining` actually counts down.
 */
#define SW97_BLIND_TABLE_SIZE 32
#define SW97_BLIND_DURATION   300  // 10 sec at SW97's 30fps timer convention

typedef struct {
    Actor* actor;
    s16    framesRemaining;
} Sw97BlindEntry;

static Sw97BlindEntry sSw97Blinded[SW97_BLIND_TABLE_SIZE];

void Sw97_TagBlinded(Actor* actor, s16 frames) {
    if (actor == NULL || actor->update == NULL) return;
    s32 empty = -1;
    for (s32 i = 0; i < SW97_BLIND_TABLE_SIZE; i++) {
        if (sSw97Blinded[i].actor == actor) {
            if (frames > sSw97Blinded[i].framesRemaining) {
                sSw97Blinded[i].framesRemaining = frames;
            }
            return;
        }
        if (sSw97Blinded[i].actor == NULL && empty < 0) empty = i;
    }
    if (empty >= 0) {
        sSw97Blinded[empty].actor = actor;
        sSw97Blinded[empty].framesRemaining = frames;
    }
}

s32 Sw97_IsBlinded(Actor* actor) {
    if (actor == NULL) return 0;
    for (s32 i = 0; i < SW97_BLIND_TABLE_SIZE; i++) {
        if (sSw97Blinded[i].actor == actor && sSw97Blinded[i].framesRemaining > 0) {
            return 1;
        }
    }
    return 0;
}

void Sw97_TickBlindness(void) {
    for (s32 i = 0; i < SW97_BLIND_TABLE_SIZE; i++) {
        if (sSw97Blinded[i].actor == NULL) continue;
        // Drop dead actors immediately so we don't keep their pointer.
        if (sSw97Blinded[i].actor->update == NULL) {
            sSw97Blinded[i].actor = NULL;
            sSw97Blinded[i].framesRemaining = 0;
            continue;
        }
        if (--sSw97Blinded[i].framesRemaining <= 0) {
            sSw97Blinded[i].actor = NULL;
            sSw97Blinded[i].framesRemaining = 0;
        }
    }
}

/**
 * Cucco Mode — Soul arrow + Cucco → 30-second transformation.
 *
 * Triggered by `ArrowSoul_TryTransform` when the soul arrow hits an `EN_NIW`.
 * Visual: Cucco model swap on the Player draw function. Movement: a Flappy
 * Bird-style flap (A press while airborne = upward burst, reduced gravity for
 * slow fall). Bow / slingshot shots become elemental eggs (free, no magic
 * cost). R = spawn 3 attack-cuccos orbiting Link. B in air = peck dive.
 *
 * State is global so other systems (player draw hook, input intercept, egg
 * spawner) can query without threading through a parameter.
 */
#define CUCCO_MODE_FRAMES   1800  // 30 sec @ 60fps tick
#define CUCCO_FLAP_VELOCITY 8.0f   // upward burst per A press (Flappy Bird)
#define CUCCO_GRAVITY      -0.7f   // soft fall — gets countered by flap
#define CUCCO_HOVER         5.0f   // floor clamp margin (same as HGRACE_FAIRY_HOVER)
#define CUCCO_SPEED         5.0f
#define CUCCO_SPRINT_MULT   2.0f
#define CUCCO_MAX_VY_DOWN  -6.0f

s32 gSw97CuccoModeActive  = 0;
s32 gSw97CuccoModeTimer   = 0;
// Puppet EN_NIW that mirrors Link's position so the player sees a Cucco
// instead of Link. Pattern mirrors HGrace's Ivan possess mode.
Actor* gSw97CuccoPuppet = NULL;
// Saved player draw — restored on exit. NULL'd while active so Link is
// invisible (the puppet provides the visual).
void* gSw97CuccoSavedDraw = NULL;
// Authoritative cucco position — saved at end of each tick, restored at the
// start of the next so Actor_UpdateBgCheckInfo can't displace Link out of
// our custom movement. Direct port of sFairyPos/sFairyPosValid from HGrace.
Vec3f gSw97CuccoPos = { 0.0f, 0.0f, 0.0f };
u8    gSw97CuccoPosValid = 0;
Vec3f gSw97CuccoVel = { 0.0f, 0.0f, 0.0f };

void Sw97_StartCuccoMode(void) {
    if (gSw97CuccoModeActive) return; // idempotent
    gSw97CuccoModeActive = 1;
    gSw97CuccoModeTimer  = CUCCO_MODE_FRAMES;
    gSw97CuccoPosValid   = 0;
    gSw97CuccoVel.x = gSw97CuccoVel.y = gSw97CuccoVel.z = 0.0f;
    // Puppet spawn + flag setup deferred to first tick — needs PlayState/Player.
}

void Sw97_EndCuccoMode(void) {
    if (!gSw97CuccoModeActive) return;
    gSw97CuccoModeActive = 0;
    gSw97CuccoModeTimer  = 0;
    gSw97CuccoPosValid   = 0;
    if (gSw97CuccoPuppet != NULL && gSw97CuccoPuppet->update != NULL) {
        Actor_Kill(gSw97CuccoPuppet);
    }
    gSw97CuccoPuppet = NULL;
    // Player flag cleanup + draw restoration happens in the inactive branch
    // of Sw97_TickCuccoMode on the next frame.
}

s32 Sw97_IsCuccoModeActive(void) {
    return gSw97CuccoModeActive;
}

#include "overlays/actors/ovl_En_Niw/z_en_niw.h"

// Scene tracking — clear stale puppet pointer when the scene changes,
// otherwise the next tick reads through a freed actor pointer → crash.
static s32 gSw97CuccoLastScene = -1;

void Sw97_TickCuccoMode(PlayState* play, Player* player) {
    // ─── INACTIVE: restore Link cleanly ─────────────────────────────────
    if (!gSw97CuccoModeActive) {
        if (gSw97CuccoSavedDraw != NULL && player != NULL) {
            player->actor.draw = (ActorFunc)gSw97CuccoSavedDraw;
            gSw97CuccoSavedDraw = NULL;
            player->cylinder.base.atFlags  |= AT_ON;
            player->cylinder.base.acFlags  |= AC_ON;
            player->cylinder.base.ocFlags1 |= OC1_ON;
            player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
            player->invincibilityTimer = 20;
            Audio_PlayActorSound2(&player->actor, NA_SE_EV_CHICKEN_CRY_M);
            func_800AA000(200.0f, 150, 20, 80);
        }
        gSw97CuccoLastScene = -1;
        return;
    }

    if (--gSw97CuccoModeTimer <= 0) {
        Sw97_EndCuccoMode();
        return;
    }

    if (player == NULL || play == NULL) return;

    // Scene change → puppet memory is gone, force-reset our state cleanly.
    if (gSw97CuccoLastScene >= 0 && gSw97CuccoLastScene != play->sceneNum) {
        gSw97CuccoPuppet   = NULL;
        gSw97CuccoPosValid = 0;
        gSw97CuccoVel.x = gSw97CuccoVel.y = gSw97CuccoVel.z = 0.0f;
    }
    gSw97CuccoLastScene = play->sceneNum;

    // ─── Restore authoritative position FIRST (HGrace pattern) ──────────
    if (gSw97CuccoPosValid) {
        player->actor.world.pos = gSw97CuccoPos;
    }

    // ─── First-frame setup ──────────────────────────────────────────────
    if (gSw97CuccoPuppet == NULL || gSw97CuccoPuppet->update == NULL) {
        gSw97CuccoPuppet = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_NIW,
                                       player->actor.world.pos.x,
                                       player->actor.world.pos.y,
                                       player->actor.world.pos.z,
                                       0, 0, 0, 0xE);
        if (gSw97CuccoPuppet != NULL && gSw97CuccoSavedDraw == NULL) {
            gSw97CuccoSavedDraw = (void*)player->actor.draw;
            player->actor.draw = NULL;
            Camera_ChangeSetting(Play_GetCamera(play, 0), CAM_SET_NORMAL0);
            Audio_PlayActorSound2(&player->actor, NA_SE_EV_CHICKEN_CRY_M);
            func_800AA000(200.0f, 150, 20, 80);
        }
    }

    // ─── HGrace pattern: input disabled, custom movement ───────────────
    player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
    player->stateFlags1 &= ~PLAYER_STATE1_IN_ITEM_CS;

    player->actor.focus.pos.x = player->actor.world.pos.x;
    player->actor.focus.pos.y = player->actor.world.pos.y + 20.0f;
    player->actor.focus.pos.z = player->actor.world.pos.z;

    player->invincibilityTimer = -1;

    player->linearVelocity = 0.0f;
    player->actor.speedXZ = 0.0f;
    player->actor.velocity.x = player->actor.velocity.y = player->actor.velocity.z = 0.0f;

    player->cylinder.base.atFlags  &= ~AT_ON;
    player->cylinder.base.acFlags  &= ~AC_ON;
    player->cylinder.base.ocFlags1 &= ~OC1_ON;

    // ─── Read input (raw, HGrace pattern) ───────────────────────────────
    u8 aPress = CHECK_BTN_ALL(play->state.input[0].press.button, BTN_A);
    u8 lBtn   = CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_L);

    f32 stickMag;
    s16 stickAngle;
    func_80077D10(&stickMag, &stickAngle, &play->state.input[0]);
    s16 worldYaw = Camera_GetInputDirYaw(GET_ACTIVE_CAM(play)) + stickAngle;

    f32 speed = CUCCO_SPEED;
    if (lBtn) speed *= CUCCO_SPRINT_MULT;

    f32 targetVX = 0.0f, targetVZ = 0.0f;
    if (stickMag > 10.0f) {
        f32 normMag = stickMag / 60.0f;
        if (normMag > 1.0f) normMag = 1.0f;
        targetVX = Math_SinS(worldYaw) * speed * normMag;
        targetVZ = Math_CosS(worldYaw) * speed * normMag;
    }

    // Smooth XZ (same coefficients as HGrace).
    f32 maxStep = speed * 0.5f;
    Math_SmoothStepToF(&gSw97CuccoVel.x, targetVX, 0.3f, maxStep, 0.01f);
    Math_SmoothStepToF(&gSw97CuccoVel.z, targetVZ, 0.3f, maxStep, 0.01f);

    // Flappy Bird Y: A press = upward burst, otherwise integrate soft gravity.
    if (aPress) {
        gSw97CuccoVel.y = CUCCO_FLAP_VELOCITY;
        Audio_PlayActorSound2(&player->actor, NA_SE_EV_CHICKEN_CRY_A);
        if (gSw97CuccoPuppet != NULL) {
            ((EnNiw*)gSw97CuccoPuppet)->unk_2A6 = 2;
        }
    } else {
        gSw97CuccoVel.y += CUCCO_GRAVITY;
        if (gSw97CuccoVel.y < CUCCO_MAX_VY_DOWN) gSw97CuccoVel.y = CUCCO_MAX_VY_DOWN;
    }

    // Face movement direction.
    if (fabsf(gSw97CuccoVel.x) > 0.5f || fabsf(gSw97CuccoVel.z) > 0.5f) {
        s16 moveYaw = Math_Atan2S(gSw97CuccoVel.x, gSw97CuccoVel.z);
        Math_SmoothStepToS(&player->actor.shape.rot.y, moveYaw, 5, 0x1000, 0x100);
        player->actor.world.rot.y = player->actor.shape.rot.y;
    }

    // ─── Position application + floor clamp (HGrace verbatim) ───────────
    Vec3f desiredPos;
    desiredPos.x = player->actor.world.pos.x + gSw97CuccoVel.x;
    desiredPos.y = player->actor.world.pos.y + gSw97CuccoVel.y;
    desiredPos.z = player->actor.world.pos.z + gSw97CuccoVel.z;

    CollisionPoly* floorPoly = NULL;
    s32 floorBgId;
    f32 floor = BgCheck_EntityRaycastFloor5(play, &play->colCtx, &floorPoly, &floorBgId,
                                            &player->actor, &desiredPos);
    if (floor > BGCHECK_Y_MIN && desiredPos.y < floor + CUCCO_HOVER) {
        desiredPos.y = floor + CUCCO_HOVER;
        if (gSw97CuccoVel.y < 0.0f) gSw97CuccoVel.y = 0.0f;
    }

    player->actor.world.pos = desiredPos;
    gSw97CuccoPos = desiredPos;
    gSw97CuccoPosValid = 1;

    // ─── Auto-exit on loading zone (mirrors HGrace's exit detection) ────
    // Stepping onto a floor polygon with an exit index ends cucco mode
    // cleanly so the puppet is killed before scene transition runs —
    // otherwise the EnNiw pointer dangles into the next scene and the
    // tick crashes when it derefs it.
    if (floorPoly != NULL && play->transitionTrigger == TRANS_TRIGGER_OFF) {
        s32 exitIndex = SurfaceType_GetSceneExitIndex(&play->colCtx, floorPoly, floorBgId);
        if (exitIndex != 0) {
            Sw97_EndCuccoMode();
            return;
        }
    }

    // ─── Mirror puppet + animate ────────────────────────────────────────
    if (gSw97CuccoPuppet != NULL) {
        gSw97CuccoPuppet->world.pos = desiredPos;
        gSw97CuccoPuppet->shape.rot.y = player->actor.shape.rot.y;
        gSw97CuccoPuppet->world.rot.y = player->actor.shape.rot.y;
        gSw97CuccoPuppet->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
        gSw97CuccoPuppet->velocity.x = 0.0f;
        gSw97CuccoPuppet->velocity.y = 0.0f;
        gSw97CuccoPuppet->velocity.z = 0.0f;
        gSw97CuccoPuppet->gravity = 0.0f;
        gSw97CuccoPuppet->speedXZ = 0.0f;
        EnNiw* niw = (EnNiw*)gSw97CuccoPuppet;
        niw->collider.base.atFlags  = 0;
        niw->collider.base.acFlags  = 0;
        niw->collider.base.ocFlags1 = 0;
        niw->collider.info.ocElemFlags = 0;
        f32 speedMag = sqrtf(SQ(gSw97CuccoVel.x) + SQ(gSw97CuccoVel.z));
        f32 animRate = 1.0f + speedMag * 0.2f;
        if (animRate > 3.0f) animRate = 3.0f;
        niw->skelAnime.playSpeed = animRate;
        SkelAnime_Update(&niw->skelAnime);
    }
}
