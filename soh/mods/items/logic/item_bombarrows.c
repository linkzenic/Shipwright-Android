/**
 * item_bombarrows.c - Bomb Arrows from Twilight Princess
 *
 * Controls:
 *   Hold C Button (< 3s):  Aim in first-person, release fires explosive arrow
 *   Hold C Button (>= 3s): Spawns a handheld bomb at Link's position
 *                          (vanilla bomb mechanics from there — grab, throw, flee)
 *
 * Features:
 *   - Acts like other elemental arrows/bullets: arrow flies, explodes on impact
 *   - Adult uses bow ammo + 1 bomb; child uses slingshot ammo + 1 bomb
 *   - No ammo consumed during charge — cancelling mid-charge costs nothing
 *   - The 3-second charge path is the ONLY way to spawn a bomb on Link
 *     (no more "fuse expires on Link" surprises during normal aim)
 *   - Authentic explosion at impact via real bomb actor with combined
 *     bomb + arrow damage flags
 */

#include "z64.h"
#include "item_bombarrows.h"
#include "../custom_items.h"
#include "../helpers/camera_helper.h"
#include "../helpers/equip_helper.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "overlays/actors/ovl_En_Arrow/z_en_arrow.h"
#include "overlays/actors/ovl_En_Bom/z_en_bom.h"
#include "assets/objects/gameplay_keep/gameplay_keep.h"

static void BombArrows_Stop(Player* p, PlayState* play);
static void BombArrows_FireArrow(Player* p, PlayState* play);
static void BombArrows_SpawnInstantBomb(PlayState* play, Vec3f* pos);
static s8 sBombArrowPrevInvinc = 0;
static s16 sBaChargeTimer = 0; // Frames C has been held continuously this charge cycle
static s8 sBaWasHeld = 0;      // Was C held last frame (release-edge detection for fire)
static s8 sBaShouldFire = 0;   // FireArrow sets this; upper action picks it up next frame
static u8 sBaUpperPrevHeld = 0; // Was C held last frame in upper action (press-edge detection for re-DRAW)

// Upper-action animation phase. Matches vanilla bow flow:
//   -1 = inactive
//    0 = DRAW       (gPlayerAnim_link_bow_bow_ready playing)
//    1 = WAIT       (gPlayerAnim_link_bow_bow_wait loop — C held, bow drawn)
//    2 = SHOOT      (gPlayerAnim_link_bow_bow_shoot — release, arrow leaves)
//    3 = SHOOT_END  (gPlayerAnim_link_bow_bow_shoot_end — recoil/lower)
//    4 = NEUTRAL    (no override — Link's main anim plays arms; bow held at side)
static s8 sBaAnimPhase = -1;

// Charge threshold: holding C this long with NO release fires the backfire —
// an instant explosion AT Link's position. Matches vanilla bomb fuse (~70
// frames / 1.2s) so the charge behaves like lighting a bomb fuse: hold too
// long and it blows in your face.
#define BOMBARROW_CHARGE_TO_BOMB_FRAMES 70

// Multi-shot tracking: every fired arrow is added to this ring. Each frame
// every live entry is polled for impact (hit flag OR collider AT_HIT) and the
// bomb is spawned at its landing position. Slots clear when the arrow dies
// (Actor.update == NULL) or its timer expires. Sized for "fast tap-fire while
// previous arrows are still in flight" without overflow.
//
// IMPORTANT — seed path (child Link / slingshot):
// EnArrow with ARROW_SEED params doesn't set hitFlags on impact. Instead,
// z_en_arrow.c:409 calls Actor_Kill(&this->actor) directly when the seed
// touches anything (wall, floor, or AC). That means by the time my tracker
// runs (one frame later) the actor is already dead — actor.update == NULL —
// and hitFlags is still 0. To still spawn the bomb at the landing point we
// cache each tracked arrow's world.pos every live frame, and when
// update goes NULL we spawn the bomb at the LAST cached position.
#define BA_MAX_TRACKED_ARROWS 8
static EnArrow* sBaTrackedArrows[BA_MAX_TRACKED_ARROWS] = { 0 };
static Vec3f sBaTrackedArrowLastPos[BA_MAX_TRACKED_ARROWS] = { { 0 } };

static void BombArrows_TrackArrow(EnArrow* arrow) {
    if (arrow == NULL) {
        return;
    }
    for (s32 i = 0; i < BA_MAX_TRACKED_ARROWS; i++) {
        if (sBaTrackedArrows[i] == NULL) {
            sBaTrackedArrows[i] = arrow;
            sBaTrackedArrowLastPos[i] = arrow->actor.world.pos; // seed the cache
            return;
        }
    }
    // No empty slot — overwrite the oldest. Acceptable tradeoff: the oldest
    // in-flight arrow loses its bomb-on-impact, but multi-tap-fire is preserved.
    sBaTrackedArrows[0] = arrow;
    sBaTrackedArrowLastPos[0] = arrow->actor.world.pos;
}

static void BombArrows_UpdateTrackedArrows(PlayState* play) {
    for (s32 i = 0; i < BA_MAX_TRACKED_ARROWS; i++) {
        EnArrow* arrow = sBaTrackedArrows[i];
        if (arrow == NULL) {
            continue;
        }
        if (arrow->actor.update == NULL) {
            // Actor destroyed mid-flight. For seeds this means "hit something
            // and Actor_Kill'd itself" (z_en_arrow.c:409). Spawn the bomb at
            // the LAST cached live position so child + slingshot bomb arrows
            // detonate on landing the same way adult + bow arrows do. False
            // positives on natural seed expiry are tolerated — seeds don't
            // expire mid-air without hitting something in practice.
            BombArrows_SpawnInstantBomb(play, &sBaTrackedArrowLastPos[i]);
            sBaTrackedArrows[i] = NULL;
            continue;
        }
        // Live this frame — refresh the cached position so we have a fresh
        // landing point next frame if the actor dies between updates.
        sBaTrackedArrowLastPos[i] = arrow->actor.world.pos;

        u8 hit = (arrow->hitFlags & 1) || (arrow->collider.base.atFlags & AT_HIT);
        if (hit) {
            // Adult / bow path — hitFlags is set BEFORE the arrow dies, so we
            // catch the impact here and place the bomb at the live position.
            BombArrows_SpawnInstantBomb(play, &arrow->actor.world.pos);
            Actor_Kill(&arrow->actor);
            sBaTrackedArrows[i] = NULL;
        } else if (arrow->timer <= 0) {
            sBaTrackedArrows[i] = NULL;
        } else {
            // Live arrow — keep the small fuse spark VFX + sound (visual feedback
            // that this arrow is the "bomb" variant).
            if ((play->gameplayFrames % 3) == 0) {
                Vec3f effVelocity = { 0.0f, 0.0f, 0.0f };
                Vec3f effAccel = { 0.0f, 0.0f, 0.0f };
                Color_RGBA8 primColor = { 255, 255, 150, 255 };
                Color_RGBA8 envColor = { 255, 0, 0, 0 };
                EffectSsGSpk_SpawnAccel(play, &arrow->actor, &arrow->actor.world.pos, &effVelocity, &effAccel,
                                        &primColor, &envColor, 40, 2);
            }
            func_8002F974(&arrow->actor, NA_SE_IT_BOMB_IGNIT - SFX_FLAG);
        }
    }
}

static void BombArrows_ClearTracking(void) {
    for (s32 i = 0; i < BA_MAX_TRACKED_ARROWS; i++) {
        sBaTrackedArrows[i] = NULL;
    }
}

// Check if player can use bomb arrows.
// Adult uses bow ammo, child uses slingshot ammo.
static u8 BombArrows_CanUse(Player* p, PlayState* play) {
    s32 ammoItem = LINK_IS_ADULT ? ITEM_BOW : ITEM_SLINGSHOT;
    if (AMMO(ammoItem) <= 0 || AMMO(ITEM_BOMB) <= 0)
        return 0;
    return 1;
}

// Consume 1 arrow/seed + 1 bomb (age-aware)
static void BombArrows_ConsumeAmmo(void) {
    Inventory_ChangeAmmo(LINK_IS_ADULT ? ITEM_BOW : ITEM_SLINGSHOT, -1);
    Inventory_ChangeAmmo(ITEM_BOMB, -1);
}

// Spawn a real bomb that explodes instantly at impact position
// Combines both arrow and bomb damage types for full damage coverage
static void BombArrows_SpawnInstantBomb(PlayState* play, Vec3f* pos) {
    EnBom* bomb = (EnBom*)Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOM, pos->x, pos->y, pos->z, 0, 0, 0, BOMB_BODY);

    if (bomb != NULL) {
        // Timer=1 will decrement to 0 on first update and trigger explosion
        bomb->timer = 1;

        // Scale must be set (init chain sets to 0, normally set at timer=67)
        Actor_SetScale(&bomb->actor, 0.01f);

        // CRITICAL: Initialize explosion collider position manually.
        // Normally Collider_UpdateSpheres is called in EnBom_Draw while params==BOMB_BODY,
        // but since we explode on frame 1, Update runs first (params becomes BOMB_EXPLOSION)
        // and Draw never updates the collider position. Set it here so the explosion
        // collider is at the correct position when EnBom_Explode registers it.
        bomb->explosionCollider.elements[0].dim.worldSphere.center.x = (s16)pos->x;
        bomb->explosionCollider.elements[0].dim.worldSphere.center.y = (s16)pos->y;
        bomb->explosionCollider.elements[0].dim.worldSphere.center.z = (s16)pos->z;

        // Add arrow damage type (0x00000800) to bomb damage type (0x00000008)
        // This makes the explosion hit enemies vulnerable to either damage type
        bomb->explosionCollider.elements[0].info.toucher.dmgFlags |= 0x00000800;
    }
}

// Stop bomb arrow aim — does NOT clear the tracking ring. In-flight arrows
// keep flying and detonate on impact (vanilla bow parity: releasing aim
// doesn't despawn arrows already in the air).
static void BombArrows_Stop(Player* p, PlayState* play) {
    if (baFirstPerson) {
        FirstPerson_Exit(p, play);
        baFirstPerson = 0;
    }

    if (baBombActor != NULL && baBombActor->update != NULL) {
        Actor_Kill(baBombActor);
    }
    baBombActor = NULL;
    baArrowActor = NULL;

    baActive = 0;
    baState = BOMBARROW_STATE_IDLE;
    sBaChargeTimer = 0;
    sBaWasHeld = 0;
    sBaShouldFire = 0;
    sBaUpperPrevHeld = 0;
    sBaAnimPhase = -1;

    ItemEquip_PlayUnequipSFX(play, p);
}

// Start AIM — entering sustained aim mode. No persistent bomb spawn on entry.
// The bomb only appears either (a) as the arrow's payload on impact (the
// tracking loop spawns the instant bomb when arrow's hitFlags fire) or
// (b) AT Link's position when continuous charge reaches the backfire
// threshold. Ammo is consumed per-arrow at fire time, so just entering aim
// or cancelling costs nothing.
static void BombArrows_StartAim(Player* p, PlayState* play) {
    if (!BombArrows_CanUse(p, play)) {
        Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
        return;
    }

    baBombActor = NULL;
    baActive = 1;
    baState = BOMBARROW_STATE_CHARGING; // CHARGING is the sustained-aim logical state
    baArrowActor = NULL;
    sBaChargeTimer = 0;
    sBaWasHeld = 1; // entering on a C-press, so we're holding C this frame
    sBaShouldFire = 0;
    sBaUpperPrevHeld = 0; // upper action will detect press-edge on next call → DRAW
    sBaAnimPhase = -1;

    if (!Player_IsZTargeting(p)) {
        FirstPerson_Init(p, play);
        baFirstPerson = 1;
    } else {
        baFirstPerson = 0;
    }

    ItemEquip_PlayEquipSFX(play, p);
    Player_PlaySfx(p, NA_SE_IT_BOW_DRAW);
}

// (Removed BombArrows_SpawnHandheldBomb — old 70-frame-fuse backfire that
// players could dodge. Replaced by inline SpawnInstantBomb at Link in the
// aim handler so the backfire actually punishes the over-charge.)

// Get aim yaw based on camera mode
static s16 BombArrows_GetAimYaw(Player* p, PlayState* play) {
    if (baFirstPerson)
        return FirstPerson_GetAimYaw(p);
    if (Player_IsZTargeting(p) && p->focusActor != NULL)
        return Math_Vec3f_Yaw(&p->actor.world.pos, &p->focusActor->focus.pos);
    return p->actor.shape.rot.y;
}

// Get aim pitch
static s16 BombArrows_GetAimPitch(Player* p) {
    return baFirstPerson ? FirstPerson_GetAimPitch(p) : 0;
}

// Update sustained-aim state. Per frame:
//   - Maintain first-person mode based on Z-target
//   - Hold C: build the continuous charge timer
//   - Release C (after holding): fire one arrow + reset charge, STAY in aim
//   - Charge ≥ 180 frames continuously: backfire — spawn instant bomb AT Link
//   - B / other: exit aim
// Multi-shot is implicit: each hold/release cycle fires one arrow while
// the player remains in aim. Each fired arrow is added to the tracking ring
// so its impact spawns a bomb (handled in BombArrows_UpdateTrackedArrows).
static void BombArrows_UpdateAim(Player* p, PlayState* play, ItemInputState* in) {
    u8 isZTargeting = Player_IsZTargeting(p);
    if (baFirstPerson && isZTargeting) {
        FirstPerson_Exit(p, play);
        baFirstPerson = 0;
    } else if (!baFirstPerson && !isZTargeting) {
        FirstPerson_Init(p, play);
        baFirstPerson = 1;
    }

    if (baFirstPerson) {
        FirstPerson_Update(p, play);
    }

    // Cancel — pay nothing, just exit aim.
    if (CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B) || in->otherButtonPressed) {
        BombArrows_Stop(p, play);
        return;
    }

    // Ammo gate: if we've run out mid-aim, exit cleanly (no error spam).
    if (!BombArrows_CanUse(p, play)) {
        BombArrows_Stop(p, play);
        return;
    }

    u8 isHeld = in->isHeld;

    if (isHeld) {
        // Continuous charge build. Bomb fuse sizzle while held — sells the
        // "the bomb is burning down" feel, and matches the user's request
        // for the charge to sound/last like a real bomb fuse.
        func_8002F974(&p->actor, NA_SE_IT_BOMB_IGNIT - SFX_FLAG);
        sBaChargeTimer++;

        // Visual fuse cue: spit a fire spark off Link's right hand every
        // few frames. Particle scale grows with charge intensity so the
        // user can read how close to backfire they are without a HUD bar.
        if ((play->gameplayFrames % 2) == 0) {
            Vec3f sparkPos = p->bodyPartsPos[PLAYER_BODYPART_R_HAND];
            sparkPos.y += 8.0f;
            Vec3f sparkVel = { 0.0f, 1.5f, 0.0f };
            Vec3f sparkAccel = { 0.0f, -0.15f, 0.0f };
            Color_RGBA8 primColor = { 255, 220, 100, 255 };
            Color_RGBA8 envColor = { 255, 80, 0, 0 };
            s16 sparkScale = 30 + (sBaChargeTimer * 2); // grows from 30 to ~170 over the fuse
            EffectSsGSpk_SpawnAccel(play, &p->actor, &sparkPos, &sparkVel, &sparkAccel, &primColor, &envColor,
                                    sparkScale, 2);
        }

        if (sBaChargeTimer >= BOMBARROW_CHARGE_TO_BOMB_FRAMES) {
            // BACKFIRE — instant explosion at Link's position. The fuse path
            // (handheld bomb with timer=70) let players dodge; instant lock
            // matches "charging too long blows up in your face".
            //
            // Audible "fuse trigger" sound so the user gets a clear cue this
            // path fired — separately from the bomb's own explosion VFX/SFX,
            // which lands ~2 frames later when EnBom transitions to EXPLOSION.
            Audio_PlaySoundGeneral(NA_SE_IT_BOMB_EXPLOSION, &p->actor.world.pos, 4,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            Vec3f bombPos = p->actor.world.pos;
            bombPos.y += 20.0f; // chest-level so the AT collider hits Link squarely
            BombArrows_SpawnInstantBomb(play, &bombPos);
            Inventory_ChangeAmmo(ITEM_BOMB, -1); // bomb consumed by the backfire
            BombArrows_Stop(p, play);
            return;
        }
    }

    // Release-edge: was held last frame, not held now → fire one arrow.
    // baCharge > 0 prevents firing on an empty release (e.g. cancellation
    // path that briefly cleared isHeld). The upper action picks up
    // sBaShouldFire next frame and queues the bow_shoot animation.
    if (sBaWasHeld && !isHeld && sBaChargeTimer > 0) {
        BombArrows_FireArrow(p, play);
        sBaChargeTimer = 0;
        sBaShouldFire = 1;
        // stay in aim — DO NOT change baState / baActive / first-person.
    }

    sBaWasHeld = isHeld;
}

// Fire a single arrow — sustained aim version. Player stays in aim (baActive
// remains 1, baState stays CHARGING, first-person stays on) so the next
// hold/release cycle can fire again. The fired arrow is added to the
// tracking ring so its impact spawns a bomb at the landing point.
static void BombArrows_FireArrow(Player* p, PlayState* play) {
    s16 aimYaw = BombArrows_GetAimYaw(p, play);
    s16 aimPitch = BombArrows_GetAimPitch(p);

    BombArrows_ConsumeAmmo();

    s8 arrowParam = LINK_IS_ADULT ? ARROW_NORMAL : ARROW_SEED;
    Actor* arrow =
        Actor_SpawnAsChild(&play->actorCtx, &p->actor, play, ACTOR_EN_ARROW, p->actor.world.pos.x,
                           p->actor.world.pos.y + 40.0f, p->actor.world.pos.z, aimPitch, aimYaw, 0, arrowParam);

    if (arrow != NULL) {
        arrow->world.rot.x = aimPitch;
        arrow->world.rot.y = aimYaw;
        arrow->shape.rot.x = aimPitch;
        arrow->shape.rot.y = aimYaw;

        // Detach from player so it flies on its own (vanilla bow detach pattern).
        p->heldActor = arrow;
        p->unk_A73 = 4;
        arrow->parent = NULL;
        p->actor.child = NULL;
        p->heldActor = NULL;

        // Track for bomb-on-impact.
        BombArrows_TrackArrow((EnArrow*)arrow);
    }

    Player_PlaySfx(p, NA_SE_IT_ARROW_SHOT);
    Player_PlaySfx(p, NA_SE_IT_BOW_FLICK);

    // baState / baActive / baFirstPerson intentionally left as-is — sustained aim.
}

// Old FLYING state removed — multi-arrow impact tracking lives in
// BombArrows_UpdateTrackedArrows (called every frame from Handle_BombArrows
// regardless of aim state, so in-flight arrows still detonate even after the
// player exits aim).

// Main handler
void Handle_BombArrows(Player* p, PlayState* play) {
    // Always update tracked arrows so in-flight bomb arrows still explode on
    // impact even after the player exits aim or swaps items.
    BombArrows_UpdateTrackedArrows(play);

    ItemInputState in;
    ItemInput_Update(&in, ITEM_BOMB_ARROWS, p, play);
    baButtonMask = in.equippedButton;

    if (!in.wasEquipped) {
        if (baActive)
            BombArrows_Stop(p, play);
        return;
    }

    if (ItemInput_IsBlocked(p, play)) {
        if (baActive)
            BombArrows_Stop(p, play);
        return;
    }

    if (ItemInput_CheckDamage(p, &sBombArrowPrevInvinc)) {
        if (baActive)
            BombArrows_Stop(p, play);
        return;
    }

    if (!baActive) {
        // IDLE — press C to enter sustained aim.
        if (in.isPressed) {
            BombArrows_StartAim(p, play);
        }
        return;
    }

    // AIMING (sustained) — one logical state, hold/release fires arrows,
    // backfire if continuous charge exceeds threshold.
    BombArrows_UpdateAim(p, play, &in);
}

// Cycle entry: called from ArrowCycle.cpp when the user rotates from a SW97
// arrow INTO bomb arrows mid-aim. The caller has already updated buttonItems
// and player->heldItemAction/itemAction; we just install the bomb-arrows
// aim state so Handle_BombArrows / Player_UpperAction_BombArrows pick up
// next frame as if the user had just pressed C themselves.
//
// Assumes the player is currently holding C (the cycle only fires during aim,
// which is itself C-held). sBaWasHeld=1 so the first release-edge fires.
void BombArrows_EnterFromCycle(Player* p, PlayState* play) {
    baActive = 1;
    baState = BOMBARROW_STATE_CHARGING;
    baBombActor = NULL;
    baArrowActor = NULL;
    // Inherit first-person from the vanilla bow aim we were just in.
    baFirstPerson = (p->stateFlags1 & PLAYER_STATE1_FIRST_PERSON) ? 1 : 0;

    sBaChargeTimer = 0;
    sBaWasHeld = 1;
    sBaShouldFire = 0;
    sBaUpperPrevHeld = 0; // upper action will detect press-edge → DRAW next frame
    sBaAnimPhase = -1;

    Player_PlaySfx(p, NA_SE_IT_BOW_DRAW);
}

// Cycle exit: called from ArrowCycle.cpp when the user rotates OUT of bomb
// arrows into a SW97 arrow mid-aim. Clears bomb-arrows aim state but does
// NOT touch first-person (the caller is about to hand control to the
// vanilla bow aim flow). The tracking ring is preserved — in-flight bomb
// arrows still detonate on impact.
void BombArrows_ExitFromCycle(Player* p, PlayState* play) {
    // Don't call FirstPerson_Exit here — vanilla bow will keep us in FP
    // via its own state machine. Clearing baFirstPerson is sufficient.
    baActive = 0;
    baState = BOMBARROW_STATE_IDLE;
    baBombActor = NULL;
    baArrowActor = NULL;
    baFirstPerson = 0;

    sBaChargeTimer = 0;
    sBaWasHeld = 0;
    sBaShouldFire = 0;
    sBaUpperPrevHeld = 0;
    sBaAnimPhase = -1;
}

void Player_InitBombArrowsIA(PlayState* play, Player* p) {
    baActive = 0;
    baState = BOMBARROW_STATE_IDLE;
    baBombActor = NULL;
    baArrowActor = NULL;
    baFirstPerson = 0;
    baButtonMask = 0;
    sBaShouldFire = 0;
    sBaUpperPrevHeld = 0;
    sBaAnimPhase = -1;
    p->stateFlags1 |= PLAYER_STATE1_ITEM_IN_HAND;
}

// Upper-body animation — full vanilla bow phase machine mirroring how
// elemental arrows look frame-for-frame:
//
//   press C    →  DRAW   (bow_ready, ~15 frames raising/notching)
//   DRAW done  →  WAIT   (bow_wait loop while held, drawn pose)
//   release C  →  SHOOT  (bow_shoot, arrow leaves)
//   SHOOT done →  SHOOT_END (bow_shoot_end, brief recoil/lower)
//   END done   →  NEUTRAL (return 0 — Link's main anim drives arms)
//   press C    →  DRAW again
//
// Reading C live (not the cached sBaWasHeld) lets the upper body react
// the SAME frame as the input edge — no 1-frame lag where bow_wait lingers
// after release.
s32 Player_UpperAction_BombArrows(Player* this, PlayState* play) {
    this->rightHandType = PLAYER_MODELTYPE_RH_BOW_SLINGSHOT;

    if (!baActive) {
        sBaShouldFire = 0;
        sBaUpperPrevHeld = 0;
        sBaAnimPhase = -1;
        return 0;
    }

    u8 isHeld = (baButtonMask != 0) && CHECK_BTN_ANY(play->state.input[0].cur.button, baButtonMask);
    u8 pressEdge = isHeld && !sBaUpperPrevHeld;
    sBaUpperPrevHeld = isHeld;

    // Fire pulse — interrupt anything to SHOOT (release-edge from UpdateAim).
    if (sBaShouldFire) {
        sBaAnimPhase = 2;
        LinkAnimation_PlayOnce(play, &this->upperSkelAnime, &gPlayerAnim_link_bow_bow_shoot);
        sBaShouldFire = 0;
    }

    // Press-edge from NEUTRAL or first entry → DRAW.
    if (pressEdge && (sBaAnimPhase == 4 || sBaAnimPhase == -1)) {
        sBaAnimPhase = 0;
        LinkAnimation_PlayOnce(play, &this->upperSkelAnime, &gPlayerAnim_link_bow_bow_ready);
    }

    // Advance current anim and handle phase transitions on completion.
    if (sBaAnimPhase >= 0 && LinkAnimation_Update(play, &this->upperSkelAnime)) {
        switch (sBaAnimPhase) {
            case 0: // DRAW done
                if (isHeld) {
                    sBaAnimPhase = 1;
                    LinkAnimation_PlayLoop(play, &this->upperSkelAnime, &gPlayerAnim_link_bow_bow_wait);
                } else {
                    // Released mid-DRAW — no fire happened, just settle to NEUTRAL.
                    sBaAnimPhase = 4;
                }
                break;
            case 2: // SHOOT done → recoil
                sBaAnimPhase = 3;
                LinkAnimation_PlayOnce(play, &this->upperSkelAnime, &gPlayerAnim_link_bow_bow_shoot_end);
                break;
            case 3: // SHOOT_END done → NEUTRAL
                sBaAnimPhase = 4;
                break;
            // WAIT (1) loops indefinitely — nothing on completion.
            // NEUTRAL (4) has no active anim — nothing on completion.
            default:
                break;
        }
    }

    // NEUTRAL releases the override so Link's lower-body anim drives the
    // arms — bow visibly lowers between shots, which fixes the "always
    // charging" look.
    if (sBaAnimPhase == 4) {
        return 0;
    }

    return 1;
}

// Draw reticle when aiming bomb arrows
void CustomItems_DrawBombArrowsReticle(Player* p, PlayState* play) {
    if (!baFirstPerson || baState != BOMBARROW_STATE_CHARGING)
        return;

    // Use shared reticle function - red color (255, 0, 0)
    FirstPerson_DrawReticle(p, play, 0.0f, 255, 0, 0);
}
