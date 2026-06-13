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

// ── v8 new skills tuning ────────────────────────────────────────────────────
#define GARO_PARRY_WINDOW       20     // frames of active block window
#define GARO_PARRY_DAMAGE       4      // damage of the riposte slash
#define GARO_DASH_SPEED         14.0f  // sustained dash velocity
#define GARO_DASH_DAMAGE        4      // damage on dash impact (through enemies)
#define GARO_DASH_QUAD_BEG      5      // dash hitbox starts in front (units)
#define GARO_DASH_QUAD_FAR      75     // dash hitbox reach forward
#define GARO_DASH_QUAD_HALF_W   30     // dash hitbox width / 2
#define GARO_DASH_TURN_THRESH   0x2000 // stick yaw deviation that switches to spin variant (~45°)
#define GARO_BANISH_COOLDOWN    300    // frames (5s @ 60fps)
#define GARO_BANISH_OFFSET      50.0f  // distance behind target to teleport
#define GARO_BANISH_DAMAGE      4      // damage of banish slash
#define GARO_BANISH_VANISH_END  16     // frames before teleport (anim duration)
#define GARO_RIPOSTE_OFFSET     45.0f  // distance behind attacker for parry counter

#define GARO_GUARD_PATH         "objects/garo/gPlayerAnim_garo_guard"
#define GARO_DASHATTACK_PATH    "objects/garo/gPlayerAnim_garo_dashAttack"
#define GARO_SPINATTACK_PATH    "objects/garo/gPlayerAnim_garo_spinAttack"
#define GARO_COLLAPSE_PATH      "objects/garo/gPlayerAnim_garo_collapse"
#define GARO_APPEAR_PATH        "objects/garo/gPlayerAnim_garo_appear"
#define GARO_LOOKAROUND_PATH    "objects/garo/gPlayerAnim_garo_lookAround"

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
    GARO_SPINNING,   // wrolling spin attack — release of charge
    // ── v8 new skills ────────────────────────────────────────────────────────
    GARO_PARRY_GUARD,    // R-press → garo_guard, 20-frame parry window
    GARO_PARRY_RIPOSTE,  // successful parry → teleport behind attacker + slash
    GARO_DASH_ATTACK,    // A-hold no-target → garo_dashAttack, v=14 forward
    GARO_BANISH_VANISH,  // A-press w/ Z-target enemy → garo_collapse
    GARO_BANISH_APPEAR,  // after teleport → garo_appear
    GARO_BANISH_SLASH,   // garo_slashStart with damage 4 vs target
    GARO_LOOK_AROUND,    // A-press w/ Z-target non-enemy → garo_lookAround
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
    // Post-animation input buffer (Zora-style fluidity): when a swing anim
    // hits its end frame and B was NOT yet pressed, we hold for this many
    // extra ticks before transitioning to idle, so a slightly-late B-press
    // still chains. Mirrors gFormState.comboBufferTimer in mm_player_form.cpp.
    s16 comboBufferTimer;
    // ── Sword trail VFX (EffectBlure1) ────────────────────────────────────
    // index into the effect table (-1 = inactive). Vertex feed happens in
    // garo_post_limb.cpp during L_HAND limb draw (matrix in scope).
    s32 trailEffectIndex;
    u8 trailActive;
    // Last melee weapon animation observed — used to detect anim transitions
    // (slash start, combo finisher) without taking over Link's input.
    u8 lastMwa;
    // Latched "shurikens already thrown this combo step" flag, cleared when
    // mwa transitions to anything that's NOT a combo finisher.
    u8 shurikensThrownThisFinisher;
    // ── v8 new skills state ───────────────────────────────────────────────
    s16 banishCooldown;     // frames remaining before banish can be re-cast
    Actor* banishTarget;    // captured at BANISH_VANISH entry
    s16 parryWindowTimer;   // frames remaining in active parry window (≤ 20)
    Actor* parryAttacker;   // captured if parry was successful — riposte target
    GaroSword swords[GARO_SWORD_MAX_ACTIVE];
} sGaroAttack = {};

// Frames of grace after an anim ends to catch a late B-press (Zora uses 4).
#define GARO_COMBO_BUFFER_FRAMES 4

// Sword trail length (game units). Master Sword uses 4000 (mm_player_form.cpp:13369).
// Garo uses a slightly shorter blade to feel snappier and avoid clipping with
// the body since Garo's slash anims swing close to the torso.
#define GARO_TRAIL_SWORD_LENGTH 3200.0f
// Vertex segment lifetime in frames (EffectBlureInit1.elemDuration). Zora uses 8.
#define GARO_TRAIL_ELEM_DURATION 8

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
// Sword trail VFX — Zora-style EffectBlure1 driven from PostLimbDraw L_HAND.
//
// Spawn at SWING_1 entry, kill when returning to IDLE. Vertex feed runs in
// garo_post_limb.cpp where the live bone matrix is in scope.
// ============================================================================
static void GaroAttack_SpawnTrail(PlayState* play) {
    // Kill any stale trail first — guards against re-entry without proper cleanup.
    if (sGaroAttack.trailActive) {
        Effect_Delete(play, sGaroAttack.trailEffectIndex);
        sGaroAttack.trailActive = 0;
        sGaroAttack.trailEffectIndex = -1;
    }

    // Garo palette: dark violet → near-black fade. Distinct from Zora cyan.
    EffectBlureInit1 blure = {};
    blure.p1StartColor[0] = 120;  blure.p1StartColor[1] = 60;
    blure.p1StartColor[2] = 200;  blure.p1StartColor[3] = 200;
    blure.p2StartColor[0] = 60;   blure.p2StartColor[1] = 30;
    blure.p2StartColor[2] = 140;  blure.p2StartColor[3] = 100;
    blure.p1EndColor[0] = 60;     blure.p1EndColor[1] = 30;
    blure.p1EndColor[2] = 140;    blure.p1EndColor[3] = 0;
    blure.p2EndColor[0] = 60;     blure.p2EndColor[1] = 30;
    blure.p2EndColor[2] = 140;    blure.p2EndColor[3] = 0;
    blure.elemDuration = GARO_TRAIL_ELEM_DURATION;
    blure.unkFlag = 0;
    blure.calcMode = 0;
    Effect_Add(play, &sGaroAttack.trailEffectIndex, EFFECT_BLURE1, 0, 0, &blure);
    sGaroAttack.trailActive = 1;
}

static void GaroAttack_KillTrail(PlayState* play) {
    if (!sGaroAttack.trailActive) return;
    Effect_Delete(play, sGaroAttack.trailEffectIndex);
    sGaroAttack.trailActive = 0;
    sGaroAttack.trailEffectIndex = -1;
}

// Public accessors so garo_post_limb.cpp can read trail state + feed vertices.
extern "C" u8 GaroAttack_IsTrailActive(void) {
    return sGaroAttack.trailActive;
}
extern "C" s32 GaroAttack_GetTrailEffectIndex(void) {
    return sGaroAttack.trailEffectIndex;
}

// Public path-based anim loader so mm_player_form.cpp can fetch garo.o2r anims
// during the transformation cutscene without depending on MmAnim_LoadByPath
// (which is gated on mm.o2r availability and a non-zero frame count — both
// problems for Garo, whose anims live in nei/garo.o2r and have frame counts
// derived from PlayerAnimation resource size).
extern "C" LinkAnimationHeader* GaroForm_LoadAnimPublic(const char* path) {
    return GaroForm_LoadAnim(path);
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

// Forward decl: query MmForm's current active form. Allows the Garo skin draw
// to fire when Garo is active as a proper MmForm transformation (not only via
// the legacy O2rLoader skin-swap path).
extern "C" MmPlayerTransformation MmForm_GetCurrentForm(void);
// Forward decl: Z-target predicate from z_player.c. Not in functions.h —
// every other mod file (item_rod_*.c, item_switchhook.c) externs it locally.
extern "C" int Player_IsZTargeting(Player* this_);

extern "C" s32 GaroForm_TryDrawSmoothSkin(PlayState* play, Player* player) {
    // Activation paths: (1) legacy O2rLoader skin swap, (2) full MmForm transformation.
    u8 garoActive = 0;
    const char* name = O2rLoader_GetForcedName();
    if (name && std::strcmp(name, "garo") == 0) {
        garoActive = 1;
    } else if (MmForm_GetCurrentForm() == MM_PLAYER_FORM_GARO) {
        garoActive = 1;
    }
    if (!garoActive) return 0;

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
// Garo activity check — true if Garo is active via either the legacy O2rLoader
// skin-swap path OR the full MmForm transformation pipeline.
// ============================================================================
static bool GaroForm_IsActive() {
    if (O2rLoader_HasActiveModel()) {
        const char* name = O2rLoader_GetForcedName();
        if (name != nullptr && std::strcmp(name, "garo") == 0) return true;
    }
    if (MmForm_GetCurrentForm() == MM_PLAYER_FORM_GARO) return true;
    return false;
}

// ============================================================================
// Combo entry gating (Zora-style polish)
//
// Before starting a fresh combo from IDLE, validate that the player is in a
// state that ACCEPTS a melee swing. Mirrors the upstream-of-MmForm_StartPunch
// checks Zora/Gerudo benefit from by virtue of running through Idle action
// dispatch. Garo is invoked directly from MmForm_UpdateActive, so we replicate
// the gates here. Returns true if a swing is allowed to start RIGHT NOW.
// ============================================================================
static bool GaroForm_CanStartSwing(Player* player) {
    // Ground check — no air-swing (matches MMFORM_ON_GROUND).
    if (!(player->actor.bgCheckFlags & 1)) return false;
    // Already mid-attack on the OOT side (e.g. shield, item-use anim still
    // resolving). meleeWeaponState > 0 indicates an active OOT melee anim.
    if (player->meleeWeaponState > 0) return false;
    // Blocking state flags that the parent block-mask already filters out
    // *most* of, but a few transient ones (DAMAGED, USING_BOOMERANG, etc.)
    // can still slip through Garo's idle.
    const u32 swingBlockMask = PLAYER_STATE1_DAMAGED | PLAYER_STATE1_HOOKSHOT_FALLING |
                               PLAYER_STATE1_USING_BOOMERANG | PLAYER_STATE1_ITEM_IN_HAND |
                               PLAYER_STATE1_SWINGING_BOTTLE | PLAYER_STATE1_CARRYING_ACTOR |
                               PLAYER_STATE1_JUMPING | PLAYER_STATE1_FREEFALL;
    if (player->stateFlags1 & swingBlockMask) return false;
    return true;
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
// Classify Link's meleeWeaponAnimation into Garo slash override categories.
// SLASH = first hit, COMBO = second hit (finisher with shurikens).
// Returns: 0 = no override, 1 = first slash, 2 = combo finisher.
static u8 GaroForm_ClassifySlashMwa(u8 mwa) {
    switch (mwa) {
        case PLAYER_MWA_FORWARD_SLASH_1H: case PLAYER_MWA_FORWARD_SLASH_2H:
        case PLAYER_MWA_RIGHT_SLASH_1H:   case PLAYER_MWA_RIGHT_SLASH_2H:
        case PLAYER_MWA_LEFT_SLASH_1H:    case PLAYER_MWA_LEFT_SLASH_2H:
        case PLAYER_MWA_STAB_1H:          case PLAYER_MWA_STAB_2H:
            return 1;  // first slash
        case PLAYER_MWA_FORWARD_COMBO_1H: case PLAYER_MWA_FORWARD_COMBO_2H:
        case PLAYER_MWA_RIGHT_COMBO_1H:   case PLAYER_MWA_RIGHT_COMBO_2H:
        case PLAYER_MWA_LEFT_COMBO_1H:    case PLAYER_MWA_LEFT_COMBO_2H:
        case PLAYER_MWA_STAB_COMBO_1H:    case PLAYER_MWA_STAB_COMBO_2H:
            return 2;  // combo finisher
        default:
            return 0;  // other (spin, hammer, jumpslash — leave Link's vanilla anim alone)
    }
}

extern "C" void GaroForm_Update(PlayState* play, Player* player) {
    // Garo is a (mostly) passive MmForm. Link's action funcs run 1:1 — items,
    // swim, jump, sword physics, all untouched. The ONLY overrides are visual:
    //   1. Skin draw (CPU-blended Garo body — handled in MmForm_Draw)
    //   2. Slash combo anim override: when Link starts a sword slash, we
    //      replace the visible anim with Garo's `last_hit_motion1` on the
    //      form skelAnime + memcpy over player->skelAnime.jointTable each
    //      frame. Link's hit detection (meleeWeaponInfo tip/base) is computed
    //      from the bone matrix → naturally uses the Garo pose → damage works.
    //   3. On the combo finisher, throw 3 shuriken knives (extra damage).
    //   4. Sword trail VFX during the slash (fed in garo_post_limb.cpp).
    // Same pattern Zora uses for the punch combo (mm_player_form.cpp:3651,
    // 4264-4625) — no PAUSE_ACTION_FUNC, no velocity override.

    GaroAttack_UpdateSwords(play);  // tick live shurikens regardless

    if (!GaroForm_IsActive()) {
        // Garo deactivated — kill trail + reset transient state.
        GaroAttack_KillTrail(play);
        sGaroAttack.lastMwa = 0;
        sGaroAttack.shurikensThrownThisFinisher = 0;
        sGaroAttack.state = GARO_IDLE;
        return;
    }

    u8 mwa = player->meleeWeaponAnimation;
    u8 mws = player->meleeWeaponState;
    u8 slashCat = GaroForm_ClassifySlashMwa(mwa);
    bool isSlashing = (mws > 0) && (slashCat != 0);
    bool wasSlashing = (sGaroAttack.state == GARO_SWING_1 || sGaroAttack.state == GARO_SWING_2);

    // ── Slash start detection ─────────────────────────────────────────────
    // Edge: not slashing last frame, slashing this frame, OR slash type
    // changed (e.g. SLASH → COMBO mid-flight). Re-trigger the form anim.
    bool mwaChanged = (mwa != sGaroAttack.lastMwa);
    if (isSlashing && (!wasSlashing || (mwaChanged && slashCat == 2))) {
        if (slashCat == 1) {
            // First slash → motion1 frames [0,25] at 2x.
            GaroAttack_StartFormAnim(play,
                (LinkAnimationHeader*)gPlayerAnim_link_last_hit_motion1,
                (f32)GARO_SWING1_ANIM_BEG, (f32)GARO_SWING1_ANIM_END,
                GARO_SWING_PLAYSPEED);
            sGaroAttack.state = GARO_SWING_1;
        } else {
            // Combo finisher → motion1 frames [26,43] at 2x.
            GaroAttack_StartFormAnim(play,
                (LinkAnimationHeader*)gPlayerAnim_link_last_hit_motion1,
                (f32)GARO_SWING2_ANIM_BEG, (f32)GARO_SWING2_ANIM_END,
                GARO_SWING_PLAYSPEED);
            sGaroAttack.state = GARO_SWING_2;
        }
        sGaroAttack.firedThisStep = 0;
        // Spawn the trail at slash start (kept alive until slash ends).
        if (!sGaroAttack.trailActive) {
            GaroAttack_SpawnTrail(play);
        }
    }

    // ── Slash active ──────────────────────────────────────────────────────
    if (isSlashing) {
        // Advance the form anim + memcpy its jointTable over Link's. OOT's
        // action func already ran this frame and wrote Link's vanilla slash
        // pose into player->skelAnime.jointTable; we overwrite AFTER, so the
        // draw uses our Garo pose. Link's bone matrix is still computed from
        // the overwritten jointTable, so meleeWeaponInfo.tip/base also track
        // the Garo pose → hit detection works seamlessly.
        GaroAttack_AdvanceFormAnim(play, player);

        // Combo finisher → throw 3 shurikens (once per finisher).
        if (slashCat == 2 && !sGaroAttack.shurikensThrownThisFinisher) {
            GaroAttack_SpawnSwordTriple(player);
            sGaroAttack.shurikensThrownThisFinisher = 1;
            Audio_PlayActorSound2(&player->actor, NA_SE_IT_SWORD_SWING);
        }
    } else {
        // Not slashing → clear the per-combo latch + kill the trail.
        sGaroAttack.shurikensThrownThisFinisher = 0;
        if (sGaroAttack.trailActive) {
            GaroAttack_KillTrail(play);
        }
        sGaroAttack.state = GARO_IDLE;
    }

    sGaroAttack.lastMwa = mwa;
}

