/**
 * item_lantern.c - Poe Lantern: catch fire, illuminate, apply elemental effects
 *
 * Uses gPoeLanternDL from object_poh. Bottle-swing action catches fire from
 * nearby sources. Fire persists until extinguished in Kaleido (long-press C).
 * When lit, adds a real point light source to Player.
 *
 * Swing freezes player in place (like bottle) until animation finishes.
 * On fire catch: plays catch animation → shows typed message → waits for close.
 */

#include "z64.h"
#include "../custom_items.h"
#include "../helpers/equip_helper.h"
#include "../helpers/fx_helper.h"
#include "macros.h"
#include "functions.h"
#include "item_lantern.h"
#include "objects/object_poh/object_poh.h"
#include "objects/gameplay_keep/gameplay_keep.h"

// ── Global: catch message pending ──────────────────────────────────────────
// Set to fire type (1-4) when fire is caught. ItemMessages.cpp reads this
// to build the catch message. Reset to 0 after message is shown.
u8 gLanternCatchPending = 0;

// ── Catchable fire source table ─────────────────────────────────────────────

static const LanternCatchEntry sCatchableFires[] = {
    { ACTOR_OBJ_SYOKUDAI,    LANTERN_FIRE_REGULAR }, // Lit torch
    { ACTOR_EN_BW,           LANTERN_FIRE_REGULAR }, // Torch slug
    { ACTOR_EN_LIGHT,        LANTERN_FIRE_REGULAR }, // General flame (orange — default)
    { ACTOR_EN_ICE_HONO,     LANTERN_FIRE_BLUE    }, // Blue fire
    { ACTOR_EN_POH,          LANTERN_FIRE_POE     }, // Poe enemy flame
    { ACTOR_BG_PO_SYOKUDAI,  LANTERN_FIRE_POE     }, // Poe torch (Forest Temple)
};
#define CATCHABLE_COUNT (sizeof(sCatchableFires) / sizeof(sCatchableFires[0]))

// ── Light source statics ────────────────────────────────────────────────────

static LightNode* sLanternLightNode = NULL;
static LightInfo  sLanternLightInfo;

// ── Helpers ─────────────────────────────────────────────────────────────────

static u8 Lantern_IsGreenFlame(Actor* actor) {
    if (actor->id != ACTOR_EN_LIGHT) return 0;
    s16 type = actor->params & 0x000F;
    // Type 4 = green flame in OOT (Spirit Temple green torches)
    return (type == 4 || type == 5) ? 1 : 0;
}

static LanternFireType Lantern_DetectFireType(Actor* actor) {
    for (u32 i = 0; i < CATCHABLE_COUNT; i++) {
        if (actor->id == sCatchableFires[i].actorId) {
            // Special case: En_Light can be green or regular
            if (actor->id == ACTOR_EN_LIGHT && Lantern_IsGreenFlame(actor)) {
                return LANTERN_FIRE_GREEN;
            }
            return sCatchableFires[i].fireType;
        }
    }
    return LANTERN_FIRE_NONE;
}

static void Lantern_UpdateLight(Player* p, PlayState* play) {
    u8 fireType = gCustomItemState.lanternFireType;

    if (fireType != LANTERN_FIRE_NONE && fireType < LANTERN_FIRE_MAX) {
        u8 r = sLanternLightColors[fireType][0];
        u8 g = sLanternLightColors[fireType][1];
        u8 b = sLanternLightColors[fireType][2];

        if (sLanternLightNode == NULL) {
            Lights_PointNoGlowSetInfo(&sLanternLightInfo,
                (s16)p->actor.world.pos.x, (s16)(p->actor.world.pos.y + 40),
                (s16)p->actor.world.pos.z, r, g, b, LANTERN_LIGHT_RADIUS);
            sLanternLightNode = LightContext_InsertLight(play, &play->lightCtx, &sLanternLightInfo);
        } else {
            Lights_PointNoGlowSetInfo(&sLanternLightInfo,
                (s16)p->actor.world.pos.x, (s16)(p->actor.world.pos.y + 40),
                (s16)p->actor.world.pos.z, r, g, b, LANTERN_LIGHT_RADIUS);
        }
    } else if (sLanternLightNode != NULL) {
        LightContext_RemoveLight(play, &play->lightCtx, sLanternLightNode);
        sLanternLightNode = NULL;
    }
}

static void Lantern_RemoveLight(PlayState* play) {
    if (sLanternLightNode != NULL) {
        LightContext_RemoveLight(play, &play->lightCtx, sLanternLightNode);
        sLanternLightNode = NULL;
    }
}

// ── Save sync helpers ───────────────────────────────────────────────────────

static void Lantern_SyncToSave(void) {
    gSaveContext.ship.lanternFireType = gCustomItemState.lanternFireType;
}

static void Lantern_SyncFromSave(void) {
    gCustomItemState.lanternFireType = gSaveContext.ship.lanternFireType;
}

// ── Fire Catch (during swing catch window) ──────────────────────────────────

u8 Lantern_TryCatch(Player* p, PlayState* play) {
    Vec3f playerPos = p->actor.world.pos;
    s16 playerYaw = p->actor.shape.rot.y;

    static const u8 categories[] = { ACTORCAT_ITEMACTION, ACTORCAT_ENEMY, ACTORCAT_PROP };

    for (u32 c = 0; c < 3; c++) {
        Actor* actor = play->actorCtx.actorLists[categories[c]].head;
        while (actor != NULL) {
            if (actor->update != NULL) {
                f32 dx = actor->world.pos.x - playerPos.x;
                f32 dz = actor->world.pos.z - playerPos.z;
                f32 distSq = dx * dx + dz * dz;

                if (distSq < SQ(LANTERN_CATCH_RANGE)) {
                    s16 angleToActor = Math_Atan2S(dx, dz);
                    s16 angleDiff = angleToActor - playerYaw;
                    if (angleDiff < 0) angleDiff = -angleDiff;
                    if (angleDiff > 0x4000) angleDiff = 0x7FFF - angleDiff;

                    if (angleDiff < 0x4000) {
                        LanternFireType type = Lantern_DetectFireType(actor);
                        if (type != LANTERN_FIRE_NONE) {
                            gCustomItemState.lanternFireType = type;
                            Lantern_SyncToSave();
                            Audio_PlayActorSound2(&p->actor, NA_SE_EV_FLAME_IGNITION);

                            // Poe catch = kill the Poe (like bottle catches fairy)
                            if (type == LANTERN_FIRE_POE &&
                                (actor->id == ACTOR_EN_POH || actor->id == ACTOR_EN_PO_FIELD ||
                                 actor->id == ACTOR_EN_PO_DESERT || actor->id == ACTOR_EN_PO_SISTERS)) {
                                Audio_PlayActorSound2(actor, NA_SE_EN_PO_LAUGH2);
                                Actor_Kill(actor);
                            }

                            return 1; // caught!
                        }
                    }
                }
            }
            actor = actor->next;
        }
    }
    return 0;
}

// Lantern_TryCatch and Lantern_ApplyFireEffects are called directly
// from Player_Action_SwingLantern in z_player.c (unity build — same TU).

// ── Fire Effects (swing while lit) ──────────────────────────────────────────

// En_Light params for fire colors:
// 0 = orange (regular), 2 = blue, 3 = green, 8 = poe pink
// En_Ice_Hono params: 1 = dropped blue fire (spreads + melts red ice)
static const s16 sLanternFlameParam[] = {
    0x0000, // NONE — unused
    0x0000, // REGULAR — orange fire (En_Light param 0)
    0x0000, // BLUE — uses En_Ice_Hono instead
    0x0008, // POE — poe pink fire (En_Light param 8)
    0x0003, // GREEN — green fire (En_Light param 3)
};

// Per-fire-type AT damage flags
static const u32 sLanternDmgFlags[] = {
    0,                // NONE
    DMG_ARROW_FIRE,   // REGULAR — lights torches, burns deku shields/cobwebs
    DMG_ARROW_ICE,    // BLUE — triggers ice/freeze interactions
    DMG_ARROW_LIGHT,  // POE — triggers light-sensitive actors
    DMG_MAGIC_FIRE,   // GREEN — generic fire
};

// Swing AT collider (follows lantern arc during swing frames)
static ColliderCylinder sSwingCol;
static u8 sSwingColInited = 0;

static void Lantern_InitSwingCollider(Player* p, PlayState* play) {
    if (sSwingColInited) return;

    static ColliderCylinderInit sColInit = {
        { COLTYPE_NONE, AT_ON | AT_TYPE_PLAYER, AC_NONE, OC1_NONE, OC2_NONE, COLSHAPE_CYLINDER },
        { ELEMTYPE_UNK2, { 0, 0x01, 0 }, { 0, 0, 0 }, TOUCH_ON | TOUCH_SFX_NORMAL, BUMP_NONE, OCELEM_NONE },
        { 25, 40, 0, { 0, 0, 0 } }
    };

    Collider_InitCylinder(play, &sSwingCol);
    Collider_SetCylinder(play, &sSwingCol, &p->actor, &sColInit);
    sSwingColInited = 1;
}

// Track ALL spawned flames (swing + grass) so they despawn with time
#define LANTERN_FLAME_LIFETIME 80
#define LANTERN_MAX_FLAMES     8

typedef struct {
    Actor* actor;
    s16 timer;
} FlameEntry;

static FlameEntry sFlames[LANTERN_MAX_FLAMES];

static void Lantern_TrackFlame(Actor* flame) {
    if (flame == NULL) return;
    for (s32 i = 0; i < LANTERN_MAX_FLAMES; i++) {
        if (sFlames[i].timer <= 0) {
            sFlames[i].actor = flame;
            sFlames[i].timer = LANTERN_FLAME_LIFETIME;
            return;
        }
    }
    // All slots full — kill oldest
    sFlames[0].timer = 0;
    if (sFlames[0].actor != NULL && sFlames[0].actor->update != NULL) {
        Actor_Kill(sFlames[0].actor);
    }
    sFlames[0].actor = flame;
    sFlames[0].timer = LANTERN_FLAME_LIFETIME;
}

static void Lantern_UpdateFlames(void) {
    for (s32 i = 0; i < LANTERN_MAX_FLAMES; i++) {
        if (sFlames[i].timer <= 0) continue;
        sFlames[i].timer--;
        if (sFlames[i].timer <= 0 && sFlames[i].actor != NULL) {
            if (sFlames[i].actor->update != NULL) {
                Actor_Kill(sFlames[i].actor);
            }
            sFlames[i].actor = NULL;
        }
    }
}

// Spawn one fire actor in front of Link (scale *.3 of previous)
static void Lantern_SpawnFireActor(Player* p, PlayState* play) {
    u8 fireType = gCustomItemState.lanternFireType;
    if (fireType == LANTERN_FIRE_NONE || fireType >= LANTERN_FIRE_MAX) return;

    s16 yaw = p->actor.shape.rot.y;
    f32 dist = 30.0f;
    f32 fx = p->actor.world.pos.x + Math_SinS(yaw) * dist;
    f32 fy = p->actor.world.pos.y; // Floor level
    f32 fz = p->actor.world.pos.z + Math_CosS(yaw) * dist;

    Actor* flame = NULL;
    if (fireType == LANTERN_FIRE_BLUE) {
        flame = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_ICE_HONO,
            fx, fy, fz, 0, 0, 0, 1, true);
    } else {
        flame = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_LIGHT,
            fx, fy, fz, 0, 0, 0, sLanternFlameParam[fireType], true);
    }

    // Scale to 1/3 of default size + track for timed despawn
    if (flame != NULL) {
        flame->scale.x *= 0.33f;
        flame->scale.y *= 0.33f;
        flame->scale.z *= 0.33f;
        Lantern_TrackFlame(flame);
    }
}

// ── Grass/Bush Burn System ──────────────────────────────────────────────────
// Grass catches fire with visible flame particles, burns for BURN_TIME frames.
// While burning: pushes PLAYER upward (thermal updraft) if nearby.
// Fire spreads to nearby grass/bushes. After burn: grass destroyed.

#define LANTERN_BURN_TIME         80   // Frames to burn (~4 sec at 20fps)
#define LANTERN_BURN_SPREAD_RANGE 80.0f
#define LANTERN_UPDRAFT_RANGE     60.0f  // Player gets launched if within this range
#define LANTERN_UPDRAFT_FORCE     8.0f   // Upward velocity applied to player
#define LANTERN_MAX_BURNING       16

typedef struct {
    Actor* actor;
    s16 timer;
} BurningEntry;

static BurningEntry sBurning[LANTERN_MAX_BURNING];

static u8 Lantern_IsBurning(Actor* actor) {
    for (s32 i = 0; i < LANTERN_MAX_BURNING; i++) {
        if (sBurning[i].actor == actor && sBurning[i].timer > 0) return 1;
    }
    return 0;
}

static void Lantern_Ignite(Actor* actor) {
    if (Lantern_IsBurning(actor)) return;
    for (s32 i = 0; i < LANTERN_MAX_BURNING; i++) {
        if (sBurning[i].timer <= 0) {
            sBurning[i].actor = actor;
            sBurning[i].timer = LANTERN_BURN_TIME;
            return;
        }
    }
}

// Called every frame — updates swing flame despawn + all burning grass/bushes
static void Lantern_UpdateBurning(PlayState* play) {
    Player* p = GET_PLAYER(play);

    // ── Despawn all tracked flames (swing + grass) ──
    Lantern_UpdateFlames();

    // ── Update each burning grass entry ──
    for (s32 i = 0; i < LANTERN_MAX_BURNING; i++) {
        if (sBurning[i].timer <= 0) continue;
        Actor* actor = sBurning[i].actor;

        // Actor already dead?
        if (actor == NULL || actor->update == NULL) {
            sBurning[i].timer = 0;
            sBurning[i].actor = NULL;
            continue;
        }

        sBurning[i].timer--;

        // ── Spawn visible flame on grass (first frame only) ──
        if (sBurning[i].timer == LANTERN_BURN_TIME - 1) {
            Actor* grassFlame = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_LIGHT,
                actor->world.pos.x, actor->world.pos.y, actor->world.pos.z,
                0, 0, 0, 0x0000, true);
            if (grassFlame != NULL) {
                grassFlame->scale.x *= 0.33f;
                grassFlame->scale.y *= 0.33f;
                grassFlame->scale.z *= 0.33f;
                Lantern_TrackFlame(grassFlame); // auto-despawn after lifetime
            }
        }

        // ── Updraft: push PLAYER upward if near burning grass ──
        {
            f32 dx = p->actor.world.pos.x - actor->world.pos.x;
            f32 dz = p->actor.world.pos.z - actor->world.pos.z;
            if ((dx * dx + dz * dz) < SQ(LANTERN_UPDRAFT_RANGE)) {
                if (p->actor.velocity.y < LANTERN_UPDRAFT_FORCE) {
                    p->actor.velocity.y = LANTERN_UPDRAFT_FORCE;
                }
            }
        }

        // ── Spread fire to nearby grass/bushes (1 second delay = 60 frames in) ──
        if (sBurning[i].timer == LANTERN_BURN_TIME - 60) {
            Actor* other = play->actorCtx.actorLists[ACTORCAT_PROP].head;
            while (other != NULL) {
                if (other->update != NULL && other != actor &&
                    (other->id == ACTOR_EN_KUSA || other->id == ACTOR_OBJ_MURE3)) {
                    f32 odx = other->world.pos.x - actor->world.pos.x;
                    f32 odz = other->world.pos.z - actor->world.pos.z;
                    if ((odx * odx + odz * odz) < SQ(LANTERN_BURN_SPREAD_RANGE)) {
                        Lantern_Ignite(other);
                    }
                }
                other = other->next;
            }
        }

        // ── Burn complete: destroy grass ──
        if (sBurning[i].timer <= 0) {
            Actor_Kill(actor);
            sBurning[i].actor = NULL;
        }
    }
}

// Ignite nearby grass/bushes in range (called ONCE on swing)
static void Lantern_IgniteNearbyGrass(Player* p, PlayState* play) {
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_PROP].head;
    while (actor != NULL) {
        if (actor->update != NULL &&
            (actor->id == ACTOR_EN_KUSA || actor->id == ACTOR_OBJ_MURE3)) {
            f32 dx = actor->world.pos.x - p->actor.world.pos.x;
            f32 dz = actor->world.pos.z - p->actor.world.pos.z;
            if ((dx * dx + dz * dz) < SQ(LANTERN_EFFECT_RANGE)) {
                Lantern_Ignite(actor);
            }
        }
        actor = actor->next;
    }
}

// ── Poe fire: reveal ALL invisible Poes ─────────────────────────────────────

static void Lantern_RevealPoes(Player* p, PlayState* play) {
    if (gCustomItemState.lanternFireType != LANTERN_FIRE_POE) return;

    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].head;
    while (actor != NULL) {
        if (actor->update != NULL) {
            if (actor->id == ACTOR_EN_POH || actor->id == ACTOR_EN_PO_FIELD ||
                actor->id == ACTOR_EN_PO_DESERT || actor->id == ACTOR_EN_PO_SISTERS) {
                actor->shape.shadowAlpha = 255;
            }
        }
        actor = actor->next;
    }
}

// ── All fire effects combined (called ONCE per swing on first active frame) ─

void Lantern_ApplyFireEffects(Player* p, PlayState* play) {
    // Spawn one fire actor in front of Link
    Lantern_SpawnFireActor(p, play);
    Audio_PlayActorSound2(&p->actor, NA_SE_EV_FLAME_IGNITION);

    // All fire types: ignite nearby grass (burn over time + spread + updraft)
    Lantern_IgniteNearbyGrass(p, play);
}

// Update swing collider + trail VFX during active swing frames
static void Lantern_UpdateSwing(Player* p, PlayState* play) {
    if (!gCustomItemState.lanternSwinging) return;
    if (gCustomItemState.lanternCatchState != 0) return; // In catch/message state — no collider
    u8 fireType = gCustomItemState.lanternFireType;
    if (fireType == LANTERN_FIRE_NONE) return;

    s32 frame = gCustomItemState.lanternSwingFrame;

    // Only active collider during swing active frames
    if (frame >= LANTERN_CATCH_START && frame <= LANTERN_CATCH_END) {
        Lantern_InitSwingCollider(p, play);

        // CRITICAL: Always include DMG_ARROW_FIRE so ALL fire types light torches
        sSwingCol.info.toucher.dmgFlags = sLanternDmgFlags[fireType] | DMG_ARROW_FIRE;
        sSwingCol.info.toucher.damage = 0;

        // Collider follows arc in front of Link
        s16 yaw = p->actor.shape.rot.y;
        f32 t = (f32)(frame - LANTERN_CATCH_START) / (f32)(LANTERN_CATCH_END - LANTERN_CATCH_START);
        s16 arcYaw = (s16)(yaw + (s16)(0x3000 * (0.5f - t))); // sweep ±30°
        f32 reach = 25.0f;

        Vec3f tipPos;
        tipPos.x = p->actor.world.pos.x + Math_SinS(arcYaw) * reach;
        tipPos.y = p->actor.world.pos.y + 35.0f;
        tipPos.z = p->actor.world.pos.z + Math_CosS(arcYaw) * reach;

        sSwingCol.dim.pos.x = (s16)tipPos.x;
        sSwingCol.dim.pos.y = (s16)tipPos.y;
        sSwingCol.dim.pos.z = (s16)tipPos.z;
        CollisionCheck_SetAT(play, &play->colChkCtx, &sSwingCol.base);

        // Fire trail VFX (scale *.3: was 30, now 9)
        EffectSsEnFire_SpawnVec3f(play, &p->actor, &tipPos, 9, 0, 0, -1);
    }
}

// ── Green Fire Passive Healing ──────────────────────────────────────────────

static void Lantern_UpdateGreenHeal(Player* p, PlayState* play) {
    if (gCustomItemState.lanternFireType != LANTERN_FIRE_GREEN) {
        gCustomItemState.lanternHealTimer = 0;
        return;
    }

    gCustomItemState.lanternHealTimer++;
    if (gCustomItemState.lanternHealTimer >= LANTERN_GREEN_HEAL_RATE) {
        gCustomItemState.lanternHealTimer = 0;
        Health_ChangeBy(play, 4); // 1/4 heart

        // Green sparkle at player
        static Color_RGBA8 greenPrim = { 80, 255, 120, 255 };
        static Color_RGBA8 greenEnv  = { 40, 200, 80, 200 };
        Vec3f sparkPos = {
            p->actor.world.pos.x + Rand_CenteredFloat(20.0f),
            p->actor.world.pos.y + 30.0f + Rand_ZeroFloat(20.0f),
            p->actor.world.pos.z + Rand_CenteredFloat(20.0f)
        };
        Vec3f vel = { 0.0f, 2.0f, 0.0f };
        Vec3f accel = { 0.0f, -0.1f, 0.0f };
        EffectSsKiraKira_SpawnFocused(play, &sparkPos, &vel, &accel,
                                       &greenPrim, &greenEnv, 600, 20);
    }
}

// ── Public API ──────────────────────────────────────────────────────────────

// Called from ExtInv_GetItemIcon (extended_inventory.c) to get fire type
// without needing to include custom_items.h from the kaleido unity build.
u8 Lantern_GetFireType(void) {
    return gCustomItemState.lanternFireType;
}

void Player_InitLanternIA(PlayState* play, Player* this) {
    // Nothing special needed on equip
}

void Handle_Lantern(Player* p, PlayState* play) {
    ItemInputState input;
    ItemInput_Update(&input, ITEM_LANTERN, p, play);

    // Sync fire type from save on first frame / after load
    Lantern_SyncFromSave();

    // lanternEquipped is set by Player_Action_SwingLantern while active.
    // Clear it when another item takes over (heldItemAction changed away from lantern).
    if (p->heldItemAction != PLAYER_IA_LANTERN && p->heldItemAction != PLAYER_IA_NONE) {
        gCustomItemState.lanternEquipped = 0;
    }

    // ALWAYS update — even when not equipped
    Lantern_UpdateLight(p, play);
    Lantern_UpdateGreenHeal(p, play);
    Lantern_UpdateSwing(p, play);
    Lantern_UpdateBurning(play);

    // Poe fire: reveal Poes every frame while lit
    Lantern_RevealPoes(p, play);

    if (!input.wasEquipped)
        return;
    if (ItemInput_IsBlocked(p, play))
        return;

    // ── Start swing on C-button press ───────────────────────────────────
    // Entire swing/catch/message flow handled by Player_Action_SwingLantern in z_player.c
    if (input.isPressed) {
        extern void Player_StartLanternSwing(Player* this, PlayState* play);
        Player_StartLanternSwing(p, play);
    }
}

s32 Player_UpperAction_Lantern(Player* this, PlayState* play) {
    return 0;
}

// ── Draw ────────────────────────────────────────────────────────────────────

void CustomItems_DrawLantern(Player* p, PlayState* play) {
    Vec3f handPos = p->bodyPartsPos[PLAYER_BODYPART_L_HAND];
    s16 handYaw = p->actor.shape.rot.y;
    u8 fireType = gCustomItemState.lanternFireType;

    OPEN_DISPS(play->state.gfxCtx);

    // Common transform: hand position, flipped 180° (was upside down), scale 0.4
    Matrix_Translate(handPos.x, handPos.y, handPos.z, MTXMODE_NEW);
    Matrix_RotateY(handYaw * (M_PI / 32768.0f), MTXMODE_APPLY);
    Matrix_RotateX(M_PI, MTXMODE_APPLY);  // Flip 180° — DL was upside down
    Matrix_Scale(0.004f, 0.004f, 0.004f, MTXMODE_APPLY); // 0.01 * 0.4 = 0.004

    if (fireType != LANTERN_FIRE_NONE && fireType < LANTERN_FIRE_MAX) {
        // ── LIT: Draw opaque, tinted by fire type color ──
        u8 r = sLanternLightColors[fireType][0];
        u8 g = sLanternLightColors[fireType][1];
        u8 b = sLanternLightColors[fireType][2];

        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        gDPSetEnvColor(POLY_OPA_DISP++, r, g, b, 255);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
                  G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gPoeLanternDL);
    } else {
        // ── UNLIT: Draw semi-transparent, dark tint ──
        Gfx_SetupDL_27Xlu(play->state.gfxCtx);
        gDPSetEnvColor(POLY_XLU_DISP++, 40, 40, 50, 120);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
                  G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gPoeLanternDL);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}
