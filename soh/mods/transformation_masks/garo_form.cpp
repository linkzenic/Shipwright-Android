/**
 * garo_form.cpp - Garo Transformation Form (custom .o2r mod)
 *
 * Loads:
 *   * `nei/garo.o2r` Garo Skin-type skeleton (LimbType=Skin, all geometry on
 *     the Torso limb's SkinAnimatedLimbData). The .o2r is built by
 *     tools/glb_to_o2r.py --skin-skeleton.
 *
 * GaroForm_TryDrawSmoothSkin is called from z_player.c whenever the active
 * O2rLoader model is "garo". It invokes OOT's native Skin_DrawImpl driven by
 * the Player's jointTable, producing CPU-blended cross-bone seams.
 *
 * Attack kit (this file):
 *   B press → INSTANT swing combo (no detection delay). Each subsequent
 *             B-press during a slash window chains to the next slash. After
 *             SWING_3, auto-advances to RECOVER (Garo signature: slashStart
 *             ping-pong at 0.7x, throws one paralyzing knife).
 *   B hold  → if held continuously THROUGH SWING_1 (never released), exits to
 *             CHARGING (tremble loop) then to SPINNING on release. Mirrors
 *             OOT vanilla "press-and-hold B → charge spin attack".
 *
 * Animation runs on a form-exclusive SkelAnime (sFormSkelAnime) so OOT's
 * action func cannot interrupt it — same trick mm_player_form.cpp uses for
 * the Goron punch combo.
 */

#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "mods/transformation_masks/transformation_masks.h"
#include "mods/transformation_masks/garo_skin.h"
#include "mods/transformation_masks/garo_hybrid_render.h"
#include "mods/o2r_loader/o2r_loader.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/resource/type/PlayerAnimation.h"
#include "soh/resource/type/SohResourceType.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include <ship/resource/ResourceManager.h>
#include <libultraship/bridge.h>
#include <spdlog/spdlog.h>
#include <cmath>
#include <cstring>
#include <map>
#include <string>

#define GARO_SKEL_PATH "__OTR__objects/garo/gGaroSkel"

// ============================================================================
// Attack tuning
// ============================================================================
// 3-slash combo + Garo signature finisher.
//
//   SWING_1 → gPlayerAnim_link_last_hit_motion1, frames [0,25]   @ 2x speed
//   SWING_2 → gPlayerAnim_link_last_hit_motion1, frames [26,43]  @ 2x speed
//   SWING_3 → gPlayerAnim_link_normal_newside_jump_20f, full     @ 2x speed
//   RECOVER → gPlayerAnim_garo_slashStart, played forward then in reverse
//             ("ping-pong") @ 0.7x speed — Garo Master's signature attack.
//
// SWING_1 chain rules:
//   - B re-pressed during swing → chain to SWING_2
//   - B held continuously throughout swing → enter CHARGING (charge spin)
//   - Neither → return to IDLE
// SWING_2 chains to SWING_3 on B re-press. SWING_3 auto-advances to RECOVER.

// Frame ranges within each source anim (start..end inclusive).
#define GARO_SWING1_ANIM_BEG    0
#define GARO_SWING1_ANIM_END    25
#define GARO_SWING2_ANIM_BEG    26
#define GARO_SWING2_ANIM_END    43

// Playback speed per step. Swings doubled so the anim feels snappy (compensates
// for the 6-frame TAP_DETECT removal). RECOVER stays at 0.7 — slashStart is a
// long, deliberate motion and 0.7x gives the Garo Master "two-blade arc" feel.
#define GARO_SWING_PLAYSPEED    2.0f
#define GARO_RECOVER_PLAYSPEED  0.7f

// Knife throw events — `relative frames` counted from state entry. With
// playSpeed=2, stateTimer counts anim_frames * 2, so fire frames are halved
// relative to the source anim's natural frame numbers.
#define GARO_SWING_KNIFE_F1     5
#define GARO_SWING_KNIFE_F2     3
#define GARO_SWING_KNIFE_F3     3
#define GARO_RECOVER_KNIFE_F    8

// Melee quad hit windows (begin..end inclusive) per step, relative to
// stateTimer. With swing playSpeed=2 these are tight — wider windows compared
// to v4 since the player has less time to land hits visually.
#define GARO_SWING1_QUAD_BEG    3
#define GARO_SWING1_QUAD_END    13
#define GARO_SWING2_QUAD_BEG    2
#define GARO_SWING2_QUAD_END    10
#define GARO_SWING3_QUAD_BEG    2
#define GARO_SWING3_QUAD_END    8

#define GARO_SWING_DAMAGE       2     // sword-type melee swing damage

#define GARO_SIDEJUMP_PATH      "misc/link_animetion/gPlayerAnim_link_normal_newside_jump_20f_Data"

#define GARO_CHARGE_MIN_FRAMES  20
#define GARO_CHARGE_MAX_FRAMES  90
#define GARO_SPIN_MIN_FRAMES    30
#define GARO_SPIN_MAX_FRAMES    120
#define GARO_SPIN_SPEED         10.0f
#define GARO_SPIN_RATE          0x3000  // ~67° per frame visual yaw
#define GARO_SPIN_DAMAGE        2

#define GARO_SWORD_SPEED        18.0f
#define GARO_SWORD_LIFETIME     45
#define GARO_SWORD_HIT_RADIUS   35.0f  // 2x of v4 (was 20) — matches bigger sword model
#define GARO_SWORD_DAMAGE       2
#define GARO_SWORD_MAX_ACTIVE   12
#define GARO_SWORD_DRAW_SCALE   0.024f // 2x of v4 (was 0.012) — user requested bigger swords

#define GARO_PARALYZE_FRAMES    90    // freezeTimer applied on paralyzing knife hit

#define GARO_TREMBLE_PATH       "objects/garo/gPlayerAnim_garo_tremble"
#define GARO_SLASHSTART_PATH    "objects/garo/gPlayerAnim_garo_slashStart"
#define GARO_SWORD_DL_PATH      "__OTR__objects/object_jso/gGaroLeftSwordDL"

// 67 s16 per frame for the 21-limb Link/Garo PlayerAnimation format.
static constexpr s32 GARO_ANIM_S16_PER_FRAME = 67;

// ============================================================================
// State
// ============================================================================
enum GaroAttackState {
    GARO_IDLE = 0,
    GARO_SWING_1,    // last_hit_motion1 [0,25] @ 2x — instant on B-press
    GARO_SWING_2,    // last_hit_motion1 [26,43] @ 2x (B re-pressed during SWING_1)
    GARO_SWING_3,    // newside_jump_20f full @ 2x (B re-pressed during SWING_2)
    GARO_RECOVER,    // garo_slashStart ping-pong @ 0.7x (Garo signature finisher)
    GARO_CHARGING,   // tremble loop — entered if B held continuously thru SWING_1
    GARO_SPINNING    // wrolling spin attack — release of charge
};

typedef struct {
    u8 active;
    u8 paralyze;  // 1 = on hit, set target's freezeTimer instead of damage
    Vec3f pos;
    s16 yaw;
    s16 timer;
} GaroSword;

static struct {
    GaroAttackState state;
    s16 stateTimer;
    s16 spinTotal;
    // Goron-style chain buffer: sticky B-press flag set during a slash; checked
    // when the anim ends to decide whether to advance to the next slash or
    // return to idle. Mirrors gFormState.comboBPressed in mm_player_form.cpp.
    u8 comboBPressed;
    // Set the frame the player releases B during SWING_1. Used to distinguish
    // tap (released early → combo path) from hold (never released → CHARGING).
    u8 bReleasedDuringSwing;
    // Per-step "already fired the knife throw this frame run?" guard so re-entry
    // to the same frame doesn't double-fire.
    u8 firedThisStep;
    // RECOVER ping-pong phase: 0 = forward, 1 = reversed.
    u8 recoverPhase;
    GaroSword swords[GARO_SWORD_MAX_ACTIVE];
} sGaroAttack = {};

// Form-exclusive SkelAnime — runs the combo animation on its own buffers so
// OOT's action func can NEVER interrupt it. Each frame in combo we
// LinkAnimation_Update this and memcpy its jointTable over player->skelAnime's,
// so the visible pose is whatever the form skelAnime computed.
//
// Equivalent of gFormState.formSkelAnime in mm_player_form.cpp:2987-2993, but
// shares Link's skeleton (Garo doesn't change body topology, only textures).
static SkelAnime sFormSkelAnime;
static Vec3s sFormJointTable[PLAYER_LIMB_BUF_COUNT];
static Vec3s sFormMorphTable[PLAYER_LIMB_BUF_COUNT];
static u8 sFormSkelAnimeReady = 0;
static s8 sFormSkelAnimeAge = -1;  // tracks linkAge to detect adult/child swap

// Cached LinkAnimationHeader wrappers for raw PlayerAnimation resources from
// .o2r (garo.o2r anims have no header struct — just the s16 payload). Pointer
// stability matters since LinkAnimation_Change retains the address — std::map
// gives stable iterators across rehash.
static std::map<std::string, LinkAnimationHeader> sAnimWrappers;

// ============================================================================
// Animation loader (pattern from animationViewer.cpp:119-144)
// ============================================================================
static LinkAnimationHeader* GaroForm_LoadAnim(const char* path) {
    auto res = ResourceMgr_GetResourceByNameHandlingMQ(path);
    if (res == nullptr) {
        return nullptr;
    }

    uint32_t type = res->GetInitData()->Type;
    if (type == static_cast<uint32_t>(SOH::ResourceType::SOH_PlayerAnimation)) {
        auto playerAnim = std::static_pointer_cast<SOH::PlayerAnimation>(res);
        LinkAnimationHeader& wrapper = sAnimWrappers[path];
        size_t totalS16 = playerAnim->GetPointerSize() / sizeof(int16_t);
        wrapper.common.frameCount = (s16)(totalS16 / GARO_ANIM_S16_PER_FRAME);
        wrapper.segment = (void*)playerAnim->GetPointer();
        return &wrapper;
    }

    // AnimationHeader (indexed) shares a common prefix with LinkAnimationHeader.
    return (LinkAnimationHeader*)ResourceMgr_LoadAnimByName(path);
}

// ============================================================================
// Form SkelAnime — uninterruptible combo animation driver
//
// Same architectural trick Goron uses (mm_player_form.cpp:2987-2993): keep a
// SkelAnime independent from player->skelAnime so OOT's action func can't
// touch the combo's animation state. The pose we produce is then memcpy'd
// over player->skelAnime.jointTable each frame — Garo's skin draw reads from
// player->skelAnime.jointTable, so visually it shows our combo pose.
// ============================================================================

static void GaroAttack_EnsureFormSkelAnime(PlayState* play) {
    if (sFormSkelAnimeReady && sFormSkelAnimeAge == gSaveContext.linkAge) {
        return;
    }
    SkelAnime_InitLink(play, &sFormSkelAnime,
                       gPlayerSkelHeaders[gSaveContext.linkAge],
                       (LinkAnimationHeader*)gPlayerAnim_link_normal_wait,
                       9, sFormJointTable, sFormMorphTable, PLAYER_LIMB_MAX);
    sFormSkelAnime.baseTransl.x = -57;
    sFormSkelAnime.baseTransl.y = 3377;
    sFormSkelAnime.baseTransl.z = 0;
    sFormSkelAnimeReady = 1;
    sFormSkelAnimeAge = gSaveContext.linkAge;
}

// startFrame/endFrame let each combo step play only a SECTION of its source
// anim (e.g. SWING_1 = motion1 0→25, SWING_2 = motion1 26→43). Pass endFrame
// < 0 to play through to the natural last frame of the anim. playSpeed can be
// negative to play in reverse (used by RECOVER's slashStart ping-pong).
static void GaroAttack_StartFormAnim(PlayState* play, LinkAnimationHeader* anim,
                                     f32 startFrame, f32 endFrame, f32 playSpeed) {
    if (anim == nullptr) return;
    GaroAttack_EnsureFormSkelAnime(play);
    if (endFrame < 0.0f) {
        endFrame = Animation_GetLastFrame(anim);
    }
    LinkAnimation_Change(play, &sFormSkelAnime, anim, playSpeed, startFrame, endFrame,
                         ANIMMODE_ONCE, -2.0f);
}

// Returns 1 when the current form animation reached its final frame this tick.
static s32 GaroAttack_AdvanceFormAnim(PlayState* play, Player* player) {
    if (!sFormSkelAnimeReady) return 0;
    s32 done = LinkAnimation_Update(play, &sFormSkelAnime);
    memcpy(player->skelAnime.jointTable, sFormJointTable,
           PLAYER_LIMB_MAX * sizeof(Vec3s));
    return done;
}

// ============================================================================
// Existing Garo form entry points
// ============================================================================
extern "C" FlexSkeletonHeader* GaroForm_LoadSkeleton(PlayState* play) {
    SkeletonHeader* hdr = ResourceMgr_LoadSkeletonByName(GARO_SKEL_PATH, NULL);
    if (hdr == NULL) {
        SPDLOG_WARN("[GaroForm] LoadSkeleton: NULL for {}", GARO_SKEL_PATH);
        return NULL;
    }
    return (FlexSkeletonHeader*)hdr;
}

extern "C" void GaroForm_Cleanup(void) {
    // Skin teardown handled by GaroSkin_Teardown — called by the engine on
    // scene transition via Play's heap reset.
    sGaroAttack = {};
}

extern "C" s32 GaroForm_TryDrawSmoothSkin(PlayState* play, Player* player) {
    const char* name = O2rLoader_GetForcedName();
    if (!name || std::strcmp(name, "garo") != 0) return 0;

    // Hybrid render path: 19-bone skeleton combining MM Garo upper body + OOT
    // Link adult lower body. Draws at player's world pos with own SkelAnime
    // running Garo native anims (gGaroIdleAnim by default). When the CVar
    // gGaroHybrid.AnimSource = 1, the hybrid jointTable is populated from
    // player->skelAnime instead so Link's vanilla anims drive the body.
    //
    // GaroSkin_Draw is the previous switchhook.glb-based path; kept as a
    // fallback if the hybrid skeleton fails to load (set CVar gGaroHybrid.
    // Disable = 1 to force the old path).
    if (CVarGetInteger("gGaroHybrid.Disable", 0) == 0) {
        GaroHybrid_Update(play, player);
        GaroHybrid_Draw(play, player);
    } else {
        GaroSkin_Draw(play, player);
    }

    // Draw any live sword projectiles in the same Garo draw pass — keeps the
    // z_player.c hook list small (one call instead of two).
    extern void GaroForm_DrawProjectiles(PlayState * play);
    GaroForm_DrawProjectiles(play);
    return 1;
}

// ============================================================================
// Garo activity check
// ============================================================================
static bool GaroForm_IsActive() {
    if (!O2rLoader_HasActiveModel()) return false;
    const char* name = O2rLoader_GetForcedName();
    return name != nullptr && std::strcmp(name, "garo") == 0;
}

// ============================================================================
// Attack collider (spinning slash)
//
// Mirrors mm_form_combat.c:57-141 — that helper is static and not exported,
// so we replicate the geometry math inline. Calls public OOT collision API
// (Collider_SetQuadVertices, Collider_ResetQuadAT, CollisionCheck_SetAT).
// ============================================================================
static void GaroAttack_EnableSpinQuad(Player* player, PlayState* play) {
    ColliderQuad* quad = &player->meleeWeaponQuads[0];

    // Forward-extending rectangular slab in front of the player. As shape.rot.y
    // rotates each frame, the quad sweeps the full 360° around the player.
    const f32 nearDist = 15.0f;
    const f32 farDist  = 55.0f;
    const f32 halfW    = 25.0f;
    const f32 yBottom  = 0.0f;
    const f32 yTop     = 50.0f;

    f32 sinYaw = Math_SinS(player->actor.shape.rot.y);
    f32 cosYaw = Math_CosS(player->actor.shape.rot.y);
    f32 rightX = cosYaw;
    f32 rightZ = -sinYaw;

    Vec3f pos = player->actor.world.pos;

    f32 farCX  = pos.x + sinYaw * farDist;
    f32 farCZ  = pos.z + cosYaw * farDist;
    f32 nearCX = pos.x + sinYaw * nearDist;
    f32 nearCZ = pos.z + cosYaw * nearDist;

    Vec3f a, b, c, d;
    a.x = farCX  - rightX * halfW; a.y = pos.y + yTop;    a.z = farCZ  - rightZ * halfW;
    b.x = farCX  + rightX * halfW; b.y = pos.y + yTop;    b.z = farCZ  + rightZ * halfW;
    c.x = nearCX + rightX * halfW; c.y = pos.y + yBottom; c.z = nearCZ + rightZ * halfW;
    d.x = nearCX - rightX * halfW; d.y = pos.y + yBottom; d.z = nearCZ - rightZ * halfW;

    Collider_ResetQuadAT(play, &quad->base);
    Collider_SetQuadVertices(quad, &a, &b, &c, &d);

    quad->base.atFlags = AT_ON | AT_TYPE_PLAYER;
    quad->info.toucher.dmgFlags = DMG_SLASH_MASTER;
    quad->info.toucher.damage = GARO_SPIN_DAMAGE;
    quad->info.toucherFlags = TOUCH_ON | TOUCH_NEAREST;

    CollisionCheck_SetAT(play, &play->colChkCtx, &quad->base);
}

static void GaroAttack_DisableSpinQuad(Player* player) {
    player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
}

// Forward-facing slash quad used during the combo swing hit windows. Same
// geometry as the spin quad but stamped only during the active hit frames of
// each slash (not every frame), matching Link's vanilla sword combo damage
// pattern. Generous reach so the swing reliably connects on close enemies.
static void GaroAttack_EnableSwingQuad(Player* player, PlayState* play) {
    ColliderQuad* quad = &player->meleeWeaponQuads[0];

    // Big slash hitbox: reaches further forward and wider than v4 since the
    // swing plays at 2x speed (player has less time to land the hit).
    const f32 nearDist = 5.0f;    // start closer to the body
    const f32 farDist  = 85.0f;   // longer reach (was 60)
    const f32 halfW    = 40.0f;   // wider arc (was 25)
    const f32 yBottom  = -5.0f;   // catches low enemies
    const f32 yTop     = 75.0f;   // catches taller ones

    f32 sinYaw = Math_SinS(player->actor.shape.rot.y);
    f32 cosYaw = Math_CosS(player->actor.shape.rot.y);
    f32 rightX = cosYaw;
    f32 rightZ = -sinYaw;

    Vec3f pos = player->actor.world.pos;
    f32 farCX  = pos.x + sinYaw * farDist;
    f32 farCZ  = pos.z + cosYaw * farDist;
    f32 nearCX = pos.x + sinYaw * nearDist;
    f32 nearCZ = pos.z + cosYaw * nearDist;

    Vec3f a, b, c, d;
    a.x = farCX  - rightX * halfW; a.y = pos.y + yTop;    a.z = farCZ  - rightZ * halfW;
    b.x = farCX  + rightX * halfW; b.y = pos.y + yTop;    b.z = farCZ  + rightZ * halfW;
    c.x = nearCX + rightX * halfW; c.y = pos.y + yBottom; c.z = nearCZ + rightZ * halfW;
    d.x = nearCX - rightX * halfW; d.y = pos.y + yBottom; d.z = nearCZ - rightZ * halfW;

    Collider_ResetQuadAT(play, &quad->base);
    Collider_SetQuadVertices(quad, &a, &b, &c, &d);

    quad->base.atFlags = AT_ON | AT_TYPE_PLAYER;
    quad->info.toucher.dmgFlags = DMG_SLASH_MASTER;
    quad->info.toucher.damage = GARO_SWING_DAMAGE;
    quad->info.toucherFlags = TOUCH_ON | TOUCH_NEAREST;

    CollisionCheck_SetAT(play, &play->colChkCtx, &quad->base);
}

// ============================================================================
// Sword projectiles
// ============================================================================
static Vec3f GaroAttack_HandOrigin(Player* player) {
    Vec3f origin = player->leftHandPos;
    if (origin.y == 0.0f) {
        // Fallback if hand pos not populated yet (very first frame).
        origin = player->actor.world.pos;
        origin.y += 30.0f;
    }
    return origin;
}

static void GaroAttack_SpawnOne(GaroSword src, Player* player) {
    for (s32 slot = 0; slot < GARO_SWORD_MAX_ACTIVE; slot++) {
        GaroSword* sw = &sGaroAttack.swords[slot];
        if (!sw->active) {
            *sw = src;
            sw->active = 1;
            sw->timer = GARO_SWORD_LIFETIME;
            return;
        }
    }
}

// Throw 3 knives in a -15°/0°/+15° fan from the player's hand.
static void GaroAttack_SpawnSwordTriple(Player* player) {
    Vec3f origin = GaroAttack_HandOrigin(player);
    s16 baseYaw = player->actor.shape.rot.y;
    const s16 spreads[3] = { -0xAAA, 0, +0xAAA }; // ~−15°, 0°, +15°

    for (s32 i = 0; i < 3; i++) {
        GaroSword tmp = {};
        tmp.pos = origin;
        tmp.yaw = baseYaw + spreads[i];
        tmp.paralyze = 0;
        GaroAttack_SpawnOne(tmp, player);
    }
}

// Throw a single straight-ahead paralyzing knife. Used at the recover/roll
// frame of the swing combo — distinctive single throw rather than spread.
static void GaroAttack_SpawnSwordParalyze(Player* player) {
    GaroSword tmp = {};
    tmp.pos = GaroAttack_HandOrigin(player);
    tmp.yaw = player->actor.shape.rot.y;
    tmp.paralyze = 1;
    GaroAttack_SpawnOne(tmp, player);
}

static void GaroAttack_UpdateSwords(PlayState* play) {
    for (s32 i = 0; i < GARO_SWORD_MAX_ACTIVE; i++) {
        GaroSword* sw = &sGaroAttack.swords[i];
        if (!sw->active) continue;

        // Advance forward along yaw at constant speed.
        f32 sinYaw = Math_SinS(sw->yaw);
        f32 cosYaw = Math_CosS(sw->yaw);
        sw->pos.x += sinYaw * GARO_SWORD_SPEED;
        sw->pos.z += cosYaw * GARO_SWORD_SPEED;

        sw->timer--;
        if (sw->timer <= 0) {
            sw->active = 0;
            continue;
        }

        // Proximity hit: iterate enemy actors, hit the first within
        // GARO_SWORD_HIT_RADIUS, then despawn the sword. Paralyzing knives
        // stun via freezeTimer; normal knives deal sword damage.
        Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].head;
        while (actor != NULL) {
            f32 dx = actor->world.pos.x - sw->pos.x;
            f32 dy = actor->world.pos.y + actor->shape.yOffset - sw->pos.y;
            f32 dz = actor->world.pos.z - sw->pos.z;
            f32 distSq = dx * dx + dy * dy + dz * dz;
            if (distSq < (GARO_SWORD_HIT_RADIUS * GARO_SWORD_HIT_RADIUS)) {
                if (sw->paralyze) {
                    // Stun: freezeTimer halts the actor's Update for N frames.
                    // Same mechanism Deku Nuts use (z_player.c sets freezeTimer
                    // on lit-on-fire / stunned enemies).
                    actor->freezeTimer = GARO_PARALYZE_FRAMES;
                } else {
                    actor->colChkInfo.damage = GARO_SWORD_DAMAGE;
                    actor->colChkInfo.damageEffect = 0;
                    Actor_ApplyDamage(actor);
                }
                sw->active = 0;
                break;
            }
            actor = actor->next;
        }
    }
}

extern "C" void GaroForm_DrawProjectiles(PlayState* play) {
    if (!GaroForm_IsActive()) return;

    // Cache lookup of the sword DL. Refreshed every frame since the resource
    // manager owns the lifetime of the underlying Gfx*.
    Gfx* swordDL = ResourceMgr_LoadGfxByName(GARO_SWORD_DL_PATH);
    if (swordDL == NULL) return;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    for (s32 i = 0; i < GARO_SWORD_MAX_ACTIVE; i++) {
        GaroSword* sw = &sGaroAttack.swords[i];
        if (!sw->active) continue;

        Matrix_Push();
        Matrix_Translate(sw->pos.x, sw->pos.y, sw->pos.z, MTXMODE_NEW);
        // Rotate so the blade points along its yaw of travel.
        Matrix_RotateY((f32)sw->yaw * (M_PI / 0x8000), MTXMODE_APPLY);
        Matrix_Scale(GARO_SWORD_DRAW_SCALE, GARO_SWORD_DRAW_SCALE, GARO_SWORD_DRAW_SCALE, MTXMODE_APPLY);

        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
                  G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, swordDL);

        Matrix_Pop();
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

// ============================================================================
// Helpers
// ============================================================================
static f32 GaroForm_StickMag(PlayState* play) {
    s8 x = play->state.input[0].cur.stick_x;
    s8 y = play->state.input[0].cur.stick_y;
    f32 mag = sqrtf((f32)(x * x + y * y));
    return (mag > 80.0f) ? 1.0f : mag / 80.0f;
}

// Camera-relative stick angle. Matches OOT's input-direction yaw used by
// Player_GetMovementSpeedAndYaw: cameraInputYaw + stickAngleFromY.
static s16 GaroForm_StickAngle(PlayState* play) {
    s8 x = play->state.input[0].cur.stick_x;
    s8 y = play->state.input[0].cur.stick_y;
    s16 stickYaw = Math_Atan2S((f32)y, -(f32)x); // atan2(y, -x) → forward = up-stick
    s16 camYaw = Camera_GetInputDirYaw(GET_ACTIVE_CAM(play));
    return camYaw + stickYaw;
}

// ============================================================================
// Main update (called from z_player.c after Player_UpdateCommon)
// ============================================================================
extern "C" void GaroForm_Update(PlayState* play, Player* player) {
    // Always tick live projectiles, even when Garo just deactivated this frame
    // — lets them finish their flight rather than vanishing instantly.
    GaroAttack_UpdateSwords(play);

    if (!GaroForm_IsActive()) {
        if (sGaroAttack.state != GARO_IDLE) {
            // Garo turned off mid-action: reset only the state machine. Live
            // projectiles already handled above.
            sGaroAttack.state = GARO_IDLE;
            sGaroAttack.stateTimer = 0;
            sGaroAttack.comboBPressed = 0;
            sGaroAttack.firedThisStep = 0;
        }
        return;
    }

    // Blocking states that should yield to OOT (text, cutscenes, loads, ledge).
    const u32 blockMask = PLAYER_STATE1_LOADING | PLAYER_STATE1_TALKING |
                          PLAYER_STATE1_DEAD | PLAYER_STATE1_GETTING_ITEM |
                          PLAYER_STATE1_CARRYING_ACTOR | PLAYER_STATE1_CLIMBING_LEDGE |
                          PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_FIRST_PERSON |
                          PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_IN_ITEM_CS |
                          PLAYER_STATE1_IN_CUTSCENE;
    if (player->stateFlags1 & blockMask) {
        if (sGaroAttack.state != GARO_IDLE) {
            sGaroAttack.state = GARO_IDLE;
            sGaroAttack.stateTimer = 0;
            sGaroAttack.comboBPressed = 0;
            sGaroAttack.firedThisStep = 0;
            GaroAttack_DisableSpinQuad(player);
        }
        return;
    }

    // OOT's input gating ran in Player_Update (line ~13117 — B was stripped from
    // sp44 before Player_UpdateCommon). The raw `play->state.input[0]` still has
    // the original B bits, which is what we want here.
    Input* input = &play->state.input[0];
    bool bHold = CHECK_BTN_ALL(input->cur.button, BTN_B) != 0;
    bool bPress = CHECK_BTN_ALL(input->press.button, BTN_B) != 0;

    switch (sGaroAttack.state) {
        case GARO_IDLE: {
            // INSTANT swing: B-press immediately starts SWING_1 (no tap-detect
            // delay). Charge attack is decided at end of SWING_1 based on
            // whether the player kept B held continuously.
            if (bPress) {
                GaroAttack_StartFormAnim(play,
                    (LinkAnimationHeader*)gPlayerAnim_link_last_hit_motion1,
                    (f32)GARO_SWING1_ANIM_BEG, (f32)GARO_SWING1_ANIM_END,
                    GARO_SWING_PLAYSPEED);
                sGaroAttack.state = GARO_SWING_1;
                sGaroAttack.stateTimer = 0;
                sGaroAttack.comboBPressed = 0;
                sGaroAttack.firedThisStep = 0;
                sGaroAttack.bReleasedDuringSwing = 0;
            }
            break;
        }

        // ────────────────────────────────────────────────────────────────────
        // SWING_1 / SWING_2 / SWING_3 / RECOVER — Goron-style chained combo.
        //
        // Each state plays a single anim ONCE on the form-exclusive skelAnime.
        // SWING_1 and SWING_2 chain to the next only if the player presses B
        // at any point during the slash (sticky `comboBPressed` flag, same as
        // mm_player_form.cpp:3766-3811). SWING_3 auto-advances to RECOVER.
        // RECOVER returns to IDLE on anim end.
        // ────────────────────────────────────────────────────────────────────
        case GARO_SWING_1:
        case GARO_SWING_2:
        case GARO_SWING_3:
        case GARO_RECOVER: {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->actor.velocity.x = 0;
            player->actor.velocity.z = 0;
            player->linearVelocity = 0;

            // Buffer B-press for chain (SWING_1/2 only) and track if B was ever
            // released during SWING_1 — used to distinguish "tap" (release seen,
            // re-press chains combo) from "hold" (never released, enter charge).
            if (bPress && (sGaroAttack.state == GARO_SWING_1 ||
                           sGaroAttack.state == GARO_SWING_2)) {
                sGaroAttack.comboBPressed = 1;
            }
            if (!bHold && sGaroAttack.state == GARO_SWING_1) {
                sGaroAttack.bReleasedDuringSwing = 1;
            }

            // Knife throw event for this step.
            s16 fireFrame = 0;
            bool paralyze = false;
            s16 quadBeg = 0, quadEnd = -1;  // inclusive window; -1 = no melee quad
            switch (sGaroAttack.state) {
                case GARO_SWING_1:
                    fireFrame = GARO_SWING_KNIFE_F1;
                    quadBeg = GARO_SWING1_QUAD_BEG;
                    quadEnd = GARO_SWING1_QUAD_END;
                    break;
                case GARO_SWING_2:
                    fireFrame = GARO_SWING_KNIFE_F2;
                    quadBeg = GARO_SWING2_QUAD_BEG;
                    quadEnd = GARO_SWING2_QUAD_END;
                    break;
                case GARO_SWING_3:
                    fireFrame = GARO_SWING_KNIFE_F3;
                    quadBeg = GARO_SWING3_QUAD_BEG;
                    quadEnd = GARO_SWING3_QUAD_END;
                    break;
                case GARO_RECOVER:
                    // Single paralysis knife fires on phase 0 (forward swing)
                    // only — phase 1 (reverse) is purely visual return.
                    if (sGaroAttack.recoverPhase == 0) {
                        fireFrame = GARO_RECOVER_KNIFE_F;
                        paralyze = true;
                    } else {
                        fireFrame = 0x7FFF;  // unreachable sentinel → never fires
                    }
                    break;
                default: break;
            }

            // Fire knife at the configured frame (once per step entry).
            if (sGaroAttack.stateTimer >= fireFrame && !sGaroAttack.firedThisStep) {
                if (paralyze) {
                    GaroAttack_SpawnSwordParalyze(player);
                } else {
                    GaroAttack_SpawnSwordTriple(player);
                }
                sGaroAttack.firedThisStep = 1;
                Audio_PlayActorSound2(&player->actor, NA_SE_IT_SWORD_SWING);
            }

            // Melee hit window — sword physically damages enemies in range,
            // mirroring vanilla Link's combo. No melee for RECOVER (only
            // the paralysis projectile).
            bool inHitWindow = (quadEnd >= 0) &&
                               (sGaroAttack.stateTimer >= quadBeg) &&
                               (sGaroAttack.stateTimer <= quadEnd);
            if (inHitWindow) {
                GaroAttack_EnableSwingQuad(player, play);
            } else {
                player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
            }

            // Advance the form animation. AdvanceFormAnim returns 1 the frame
            // its anim hits endFrame; that's our cue to chain or exit.
            s32 done = GaroAttack_AdvanceFormAnim(play, player);
            sGaroAttack.stateTimer++;
            if (!done) break;

            // Anim finished → decide next state.
            player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
            sGaroAttack.firedThisStep = 0;
            sGaroAttack.stateTimer = 0;

            switch (sGaroAttack.state) {
                case GARO_SWING_1:
                    // Priority: combo press > hold-for-charge > idle.
                    if (sGaroAttack.comboBPressed) {
                        sGaroAttack.comboBPressed = 0;
                        GaroAttack_StartFormAnim(play,
                            (LinkAnimationHeader*)gPlayerAnim_link_last_hit_motion1,
                            (f32)GARO_SWING2_ANIM_BEG, (f32)GARO_SWING2_ANIM_END,
                            GARO_SWING_PLAYSPEED);
                        sGaroAttack.state = GARO_SWING_2;
                    } else if (!sGaroAttack.bReleasedDuringSwing && bHold) {
                        // Player held B continuously through SWING_1 → enter
                        // the charge spin attack. Same trigger as vanilla
                        // OOT "press-and-hold B to charge spin attack".
                        LinkAnimationHeader* tremble = GaroForm_LoadAnim(GARO_TREMBLE_PATH);
                        if (tremble != nullptr) {
                            LinkAnimation_Change(play, &player->skelAnime, tremble, 1.0f, 0.0f,
                                                 Animation_GetLastFrame(tremble), ANIMMODE_LOOP, -4.0f);
                        }
                        sGaroAttack.state = GARO_CHARGING;
                    } else {
                        sGaroAttack.state = GARO_IDLE;
                    }
                    break;
                case GARO_SWING_2:
                    if (sGaroAttack.comboBPressed) {
                        sGaroAttack.comboBPressed = 0;
                        GaroAttack_StartFormAnim(play,
                            GaroForm_LoadAnim(GARO_SIDEJUMP_PATH), 0.0f, -1.0f,
                            GARO_SWING_PLAYSPEED);
                        sGaroAttack.state = GARO_SWING_3;
                    } else {
                        sGaroAttack.state = GARO_IDLE;
                    }
                    break;
                case GARO_SWING_3:
                    // Auto-advance to the Garo signature finisher: slashStart
                    // played forward, then in reverse (ping-pong) at 0.7x.
                    GaroAttack_StartFormAnim(play,
                        GaroForm_LoadAnim(GARO_SLASHSTART_PATH), 0.0f, -1.0f,
                        GARO_RECOVER_PLAYSPEED);
                    sGaroAttack.state = GARO_RECOVER;
                    sGaroAttack.recoverPhase = 0;
                    break;
                case GARO_RECOVER:
                    if (sGaroAttack.recoverPhase == 0) {
                        // Forward done → play in reverse for the second swing
                        // of the ping-pong.
                        LinkAnimationHeader* slashStart =
                            GaroForm_LoadAnim(GARO_SLASHSTART_PATH);
                        if (slashStart != nullptr) {
                            f32 lastFrame = Animation_GetLastFrame(slashStart);
                            GaroAttack_StartFormAnim(play, slashStart, lastFrame, 0.0f,
                                                     -GARO_RECOVER_PLAYSPEED);
                            sGaroAttack.recoverPhase = 1;
                        } else {
                            sGaroAttack.state = GARO_IDLE;
                        }
                    } else {
                        sGaroAttack.state = GARO_IDLE;
                        sGaroAttack.comboBPressed = 0;
                        sGaroAttack.recoverPhase = 0;
                    }
                    break;
                default: break;
            }
            break;
        }

        case GARO_CHARGING: {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->actor.velocity.x = 0;
            player->actor.velocity.z = 0;
            player->linearVelocity = 0;

            LinkAnimationHeader* tremble = GaroForm_LoadAnim(GARO_TREMBLE_PATH);
            if (tremble != nullptr && player->skelAnime.animation != (void*)tremble) {
                LinkAnimation_Change(play, &player->skelAnime, tremble, 1.0f, 0.0f,
                                     Animation_GetLastFrame(tremble), ANIMMODE_LOOP, 0.0f);
            }

            if (sGaroAttack.stateTimer < GARO_CHARGE_MAX_FRAMES) {
                sGaroAttack.stateTimer++;
            }

            if (!bHold) {
                if (sGaroAttack.stateTimer >= GARO_CHARGE_MIN_FRAMES) {
                    f32 t = (f32)(sGaroAttack.stateTimer - GARO_CHARGE_MIN_FRAMES) /
                            (f32)(GARO_CHARGE_MAX_FRAMES - GARO_CHARGE_MIN_FRAMES);
                    if (t > 1.0f) t = 1.0f;
                    sGaroAttack.spinTotal = (s16)(GARO_SPIN_MIN_FRAMES +
                                                  t * (GARO_SPIN_MAX_FRAMES - GARO_SPIN_MIN_FRAMES));

                    LinkAnimationHeader* spin = (LinkAnimationHeader*)gPlayerAnim_link_fighter_Wrolling_kiru;
                    LinkAnimation_Change(play, &player->skelAnime, spin, 2.0f, 0.0f,
                                         Animation_GetLastFrame(spin), ANIMMODE_LOOP, -2.0f);
                    sGaroAttack.state = GARO_SPINNING;
                    sGaroAttack.stateTimer = 0;
                    Audio_PlayActorSound2(&player->actor, NA_SE_IT_SWORD_SWING_HARD);
                } else {
                    // Released too early — abort to idle.
                    sGaroAttack.state = GARO_IDLE;
                    sGaroAttack.stateTimer = 0;
                }
            }
            break;
        }

        case GARO_SPINNING: {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;

            LinkAnimationHeader* spin = (LinkAnimationHeader*)gPlayerAnim_link_fighter_Wrolling_kiru;
            if (player->skelAnime.animation != (void*)spin) {
                LinkAnimation_Change(play, &player->skelAnime, spin, 2.0f, 0.0f,
                                     Animation_GetLastFrame(spin), ANIMMODE_LOOP, 0.0f);
            }

            // Stick steering: when stick is pushed, smoothly turn world.rot.y
            // toward the camera-relative stick angle. When neutral, keep
            // current direction.
            f32 stickMag = GaroForm_StickMag(play);
            if (stickMag > 0.3f) {
                s16 stickAngle = GaroForm_StickAngle(play);
                Math_ScaledStepToS(&player->actor.world.rot.y, stickAngle, 0x1000);
            }

            // Forward velocity along current world yaw.
            f32 yawRad = (f32)player->actor.world.rot.y * (M_PI / 0x8000);
            player->actor.velocity.x = sinf(yawRad) * GARO_SPIN_SPEED;
            player->actor.velocity.z = cosf(yawRad) * GARO_SPIN_SPEED;
            player->linearVelocity = GARO_SPIN_SPEED;

            // Visual yaw spin: independent of world.rot.y so the body whirls
            // while still moving along the stick direction.
            player->actor.shape.rot.y += GARO_SPIN_RATE;

            GaroAttack_EnableSpinQuad(player, play);

            sGaroAttack.stateTimer++;
            if (sGaroAttack.stateTimer >= sGaroAttack.spinTotal) {
                player->actor.shape.rot.y = player->actor.world.rot.y;
                player->actor.velocity.x = 0;
                player->actor.velocity.z = 0;
                player->linearVelocity = 0;
                GaroAttack_DisableSpinQuad(player);
                sGaroAttack.state = GARO_IDLE;
                sGaroAttack.stateTimer = 0;
            }
            break;
        }
    }
}
