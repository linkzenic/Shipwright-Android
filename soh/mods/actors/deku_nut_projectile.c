/**
 * deku_nut_projectile.c — Implementation. See header for design rationale.
 *
 * Hijack pattern (same as somaria_cubes.c / spiritual_stone_statue.c):
 *   - Actor_Spawn(ACTOR_EN_LIGHTBOX, ...) — gives us a trivial real actor with
 *     correct lifetime + categorization. EnLightbox's own update/draw never
 *     gets a chance to run because we overwrite actor->update/draw before
 *     returning the actor pointer.
 *   - sNutPool[] holds per-actor state keyed by the actor pointer (we can't
 *     extend the struct — Actor_Spawn only allocates sizeof(EnLightbox)).
 *
 * The nut is INVISIBLE on purpose: the caller (Deku flight nut-drop path in
 * mm_player_form.cpp) already spawns EffectSsHahen_SpawnBurst + EffectSsExtra
 * for the "I dropped something" visual. This actor exists only to carry an AT
 * collider with DMG_DEKU_NUT damage=1 (the value MM uses) through a brief
 * gravity-driven trajectory, so anything beneath the player at the moment of
 * the drop actually takes damage.
 */

// NOTE: This file is text-included from mm_player_form.cpp (the .cpp is what's
// in CMake; this .c is not a standalone compilation unit). All OOT headers
// (z64, macros, functions, variables, collision_check, actor_table) are
// already in scope from the parent .cpp by the time we reach this include.
// All identifiers below are static so they're file-local to the parent TU.

#define DEKU_NUT_LIFETIME 30 // frames (~0.5s at 60fps) — caps the projectile's life
#define DEKU_NUT_RADIUS 18
#define DEKU_NUT_HEIGHT 28
#define DEKU_NUT_DAMAGE 1
#define DEKU_NUT_GRAVITY 1.2f
#define DEKU_NUT_MIN_VEL_Y -16.0f
#define DEKU_NUT_MAX 8 // simultaneous projectiles

typedef struct {
    Actor* owner; // NULL = free slot
    u8 lifetime;
    u8 colliderInited;
    Vec3f velocity;
    ColliderCylinder collider;
} DekuNutSlot;

static DekuNutSlot sNutPool[DEKU_NUT_MAX] = { 0 };

static s8 DekuNut_GetSlot(Actor* actor) {
    for (s8 i = 0; i < DEKU_NUT_MAX; i++) {
        if (sNutPool[i].owner == actor)
            return i;
    }
    return -1;
}

static s8 DekuNut_AllocSlot(Actor* actor) {
    for (s8 i = 0; i < DEKU_NUT_MAX; i++) {
        if (sNutPool[i].owner == NULL) {
            sNutPool[i].owner = actor;
            sNutPool[i].lifetime = DEKU_NUT_LIFETIME;
            sNutPool[i].colliderInited = 0;
            sNutPool[i].velocity.x = 0.0f;
            sNutPool[i].velocity.y = 0.0f;
            sNutPool[i].velocity.z = 0.0f;
            return i;
        }
    }
    return -1;
}

static void DekuNut_FreeSlot(s8 slot) {
    if (slot < 0 || slot >= DEKU_NUT_MAX)
        return;
    DekuNutSlot* nut = &sNutPool[slot];
    if (nut->colliderInited) {
        // OOT's Collider_DestroyCylinder takes (play, *), but the actor is
        // already being killed and play context may have moved on. Leaving
        // colliderInited=1 in a freed slot is safe because the slot is
        // re-initialized on next alloc.
        nut->colliderInited = 0;
    }
    nut->owner = NULL;
}

static ColliderCylinderInit sCylinderInit = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_PLAYER,
        AC_NONE,
        OC1_NONE,
        OC2_NONE,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK2,
        // DMG_DEKU_NUT = 1 << 0 = 0x01. Matches MM's flag for the nut drop.
        // Damage qty = 1, also matches MM.
        { DMG_DEKU_NUT, 0x00, DEKU_NUT_DAMAGE },
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_ON | TOUCH_NEAREST | TOUCH_SFX_NONE,
        BUMP_NONE,
        OCELEM_NONE,
    },
    { DEKU_NUT_RADIUS, DEKU_NUT_HEIGHT, 0, { 0, 0, 0 } },
};

static void DekuNut_Update(Actor* thisx, PlayState* play) {
    s8 slot = DekuNut_GetSlot(thisx);
    if (slot < 0) {
        // Lost our slot — kill the actor.
        Actor_Kill(thisx);
        return;
    }

    DekuNutSlot* nut = &sNutPool[slot];

    // Lazy collider init — we couldn't init it inside Spawn because the actor
    // hadn't been added to the actor list yet (Collider_SetCylinder needs the
    // actor pointer to be live).
    if (!nut->colliderInited) {
        Collider_InitCylinder(play, &nut->collider);
        Collider_SetCylinder(play, &nut->collider, thisx, &sCylinderInit);
        nut->colliderInited = 1;
    }

    // Gravity + clamp + integrate.
    nut->velocity.y -= DEKU_NUT_GRAVITY;
    if (nut->velocity.y < DEKU_NUT_MIN_VEL_Y)
        nut->velocity.y = DEKU_NUT_MIN_VEL_Y;
    thisx->world.pos.x += nut->velocity.x;
    thisx->world.pos.y += nut->velocity.y;
    thisx->world.pos.z += nut->velocity.z;

    // Ask OOT to update the actor's bgCheckFlags so we can detect ground impact.
    // Without this call, bgCheckFlags stays zero and the nut never gets a chance
    // to despawn early — it would always live the full 30 frames. The radius/
    // height here only affect the bg-check tests, not the AT cylinder above.
    Actor_UpdateBgCheckInfo(play, thisx, 20.0f, 20.0f, 20.0f, 0x1F);

    // Sync collider position to the actor — the cylinder rides at the actor
    // origin (no offset), so this is just an actor-pos copy.
    Collider_UpdateCylinder(thisx, &nut->collider);
    CollisionCheck_SetAT(play, &play->colChkCtx, &nut->collider.base);

    // Ground or lifetime expiry → kill.
    if (thisx->bgCheckFlags & 1 /* BGCHECKFLAG_GROUND */) {
        DekuNut_FreeSlot(slot);
        Actor_Kill(thisx);
        return;
    }
    if (nut->lifetime == 0) {
        DekuNut_FreeSlot(slot);
        Actor_Kill(thisx);
        return;
    }
    nut->lifetime--;
}

static void DekuNut_Draw(Actor* thisx, PlayState* play) {
    // Intentionally empty — the spawn site already drives the visual via
    // EffectSsHahen_SpawnBurst + EffectSsExtra. Adding a separate DL here
    // would mean loading object_dekunuts in the player object slot at
    // runtime, which is more invasive than necessary for this drive-by use.
    (void)thisx;
    (void)play;
}

static Actor* DekuNutProjectile_Spawn(PlayState* play, Vec3f* pos, Vec3f* vel) {
    Actor* actor = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_LIGHTBOX, pos->x, pos->y, pos->z, 0, 0, 0, 0);
    if (actor == NULL)
        return NULL;

    s8 slot = DekuNut_AllocSlot(actor);
    if (slot < 0) {
        Actor_Kill(actor);
        return NULL;
    }
    sNutPool[slot].velocity = *vel;

    // Override En_Lightbox's update/draw with ours.
    actor->update = DekuNut_Update;
    actor->draw = DekuNut_Draw;

    return actor;
}
