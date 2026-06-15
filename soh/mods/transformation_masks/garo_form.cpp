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

// BGCHECKFLAG_GROUND isn't exposed in a global header; other mods that need
// it inline it locally (see equip_champion.c, gerudo_form.cpp, etc.).
#ifndef BGCHECKFLAG_GROUND
#define BGCHECKFLAG_GROUND 0x0001
#endif

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
// v9: shrunk from 35 → 18. Ninja-knife precision; players didn't like the
// generous radius reading as a "magnet" hit. Still wide enough that a
// straight-fired knife reliably tags an adjacent enemy.
#define GARO_SWORD_HIT_RADIUS   18.0f
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

// ── v9.1 banish: stun + shadow-ball travel ──────────────────────────────
#define GARO_BANISH_STUN_FRAMES 60      // target freezeTimer set on banish trigger
#define GARO_BANISH_SHADOW_LEN  18      // frames for the shadow ball to cross
#define GARO_BANISH_STUN_RADIUS 80.0f   // particle ring radius around stunned target

// ── v9.1 parry: AOE freeze on perfect parry ─────────────────────────────
#define GARO_PARRY_AOE_RADIUS   180.0f  // enemies inside this radius are frozen
#define GARO_PARRY_FREEZE_FRAMES 40     // freezeTimer applied to AOE targets

#define GARO_GUARD_PATH         "objects/garo/gPlayerAnim_garo_guard"
#define GARO_DASHATTACK_PATH    "objects/garo/gPlayerAnim_garo_dashAttack"
#define GARO_SPINATTACK_PATH    "objects/garo/gPlayerAnim_garo_spinAttack"
#define GARO_COLLAPSE_PATH      "objects/garo/gPlayerAnim_garo_collapse"
#define GARO_APPEAR_PATH        "objects/garo/gPlayerAnim_garo_appear"
#define GARO_LOOKAROUND_PATH    "objects/garo/gPlayerAnim_garo_lookAround"

#define GARO_PARALYZE_FRAMES    90    // freezeTimer applied on paralyzing knife hit

#define GARO_TREMBLE_PATH       "objects/garo/gPlayerAnim_garo_tremble"
#define GARO_SLASHSTART_PATH    "objects/garo/gPlayerAnim_garo_slashStart"
#define GARO_TAKEOUTBOMB_PATH   "objects/garo/gPlayerAnim_garo_takeOutBomb"
#define GARO_LAUGH_PATH         "objects/garo/gPlayerAnim_garo_laugh"
// v10 A-button overhaul anims — all camelCase, verified in garo.o2r.
#define GARO_APPEARDRAWSWORDS_PATH "objects/garo/gPlayerAnim_garo_appearDrawSwords"
#define GARO_BOUNCE_PATH           "objects/garo/gPlayerAnim_garo_bounce"
#define GARO_JUMPBACK_PATH         "objects/garo/gPlayerAnim_garo_jumpBack"
#define GARO_SLASHLOOP_PATH        "objects/garo/gPlayerAnim_garo_slashLoop"
#define GARO_DRAWSWORDS_PATH       "objects/garo/gPlayerAnim_garo_drawSwords"
#define GARO_SWORD_DL_PATH      "__OTR__objects/object_jso/gGaroLeftSwordDL"

// v10 A-button damage values
#define GARO_SHADOW_BALL_DAMAGE 6   // appearDrawSwords frame-20 strike, master sword
#define GARO_JUMP_ATTACK_DAMAGE 4   // Z+A+forward leap, mid-flight quad
#define GARO_LAND_STRIKE_DAMAGE 8   // post-AIR_SLASH landing quad (frame 12+)
#define GARO_SHADOW_BALL_HIT_F  20  // elapsed frame at which SB quad fires
#define GARO_LAND_STRIKE_HIT_F  12  // elapsed frame at which land-strike quad fires
#define GARO_LAND_STRIKE_TAIL_F 3   // extra frames quad stays live after anim end

// ── v9 rod mode tuning ──────────────────────────────────────────────────
// Charge tier thresholds (frames B held in GARO_ROD_AIM). Damage scales
// linearly with the tier so a tap-and-release does a token 1 damage while
// a full hold (≥90 frames) delivers the max 4. The timer caps at 120 so
// holding longer doesn't help.
#define GARO_ROD_CHARGE_MAX     120
#define GARO_ROD_CHARGE_TIER2   30
#define GARO_ROD_CHARGE_TIER3   60
#define GARO_ROD_CHARGE_TIER4   90
#define GARO_ROD_ELEMENT_COUNT  4  // normal / fire / ice / light
// Rod release → IDLE cooldown. Prevents B-spam (release → instant B-press
// re-fires SWING_1 the very next frame, looking like a rapid-fire bug).
// 10 frames is enough that an organic re-press feels intentional.
#define GARO_ROD_RELEASE_CD     10
// Post-kill laugh chance — 20% means most kills are silent, a few trigger
// the taunt (mirrors Garo Master MM behavior).
#define GARO_LAUGH_CHANCE       0.20f

// 67 s16 per frame for the 21-limb Link/Garo PlayerAnimation format.
static constexpr s32 GARO_ANIM_S16_PER_FRAME = 67;

// ============================================================================
// State
// ============================================================================
enum GaroAttackState {
    GARO_IDLE = 0,
    GARO_SWING_1,    // last_hit_motion1 [0,25] @ 2x — instant on B-press
    GARO_SWING_2,    // last_hit_motion1 [26,43] @ 2x (B re-pressed during SWING_1)
    GARO_SWING_3,    // last_hit_motion1 [44,81] @ 2x (B re-pressed during SWING_2)
    GARO_RECOVER,    // garo_slashStart ping-pong @ 0.7x (Garo signature finisher)
    GARO_ROD_AIM,    // v9: tremble loop, charge ramp 0..120, L/R cycle element
                     // (renamed from GARO_CHARGING; old "spin attack" path
                     // is dead and removed)
    // ── v8 new skills ────────────────────────────────────────────────────────
    GARO_PARRY_GUARD,    // R-press → garo_guard, 20-frame parry window
    GARO_PARRY_RIPOSTE,  // successful parry → teleport behind attacker + slash
    GARO_DASH_ATTACK,    // A-hold no-target → garo_dashAttack, v=14 forward
    GARO_BANISH_VANISH,  // A-press w/ Z-target enemy → stun target + garo_collapse
    GARO_BANISH_SHADOW,  // v9.1 — shadow ball travels Garo → target
    GARO_BANISH_APPEAR,  // shadow arrives → teleport Garo + garo_appear
    GARO_BANISH_SLASH,   // garo_slashStart with damage 4 vs target
    GARO_LOOK_AROUND,    // A-press w/ Z-target non-enemy → garo_lookAround
    // ── v9 ──────────────────────────────────────────────────────────────────
    GARO_LAUGH_TAUNT,    // 20% post-kill garo_laugh — non-pausing
    // ── v10 A-button overhaul ──────────────────────────────────────────────
    GARO_SHADOW_BALL,    // Z+A stationary (no enemy lock-on): invuln+invisible
                         // appearDrawSwords slash. Lock-on enemy still routes
                         // through the older BANISH 4-state chain (kept).
    GARO_SIDEHOP_L,      // Z+A stick-left  → garo_bounce, no damage
    GARO_SIDEHOP_R,      // Z+A stick-right → garo_bounce, no damage
    GARO_BACKFLIP,       // Z+A stick-back  → garo_jumpBack, 2x distance
    GARO_JUMP_ATTACK,    // Z+A stick-fwd + speed>0 → garo_appear leap (dmg 4)
    GARO_AIR_SLASH,      // B in mid-air → garo_slashLoop loop (no damage)
    GARO_LAND_STRIKE,    // AIR_SLASH lands → garo_drawSwords (dmg 8 @ F12+)
};

// Garo projectile discriminator. KNIFE = standard 3-shuriken throw / combo
// finisher (proximity hit, DMG_SLASH_MASTER, GARO_SWORD_DAMAGE). PARALYZE =
// single forward knife that sets freezeTimer instead of damage. ROD_ORB =
// v9 magical projectile fired from rod mode — carries an element dmgFlag
// (DMG_ARROW_NORMAL/FIRE/ICE/LIGHT) and a charge-tier damage 1..4. The flag
// goes through a real AC quad so boss vulnerability masks (Phantom Ganon,
// Ganon2) accept it.
enum GaroProjKind {
    GARO_PROJ_KNIFE = 0,
    GARO_PROJ_PARALYZE = 1,
    GARO_PROJ_ROD_ORB = 2,
};

typedef struct {
    u8 active;
    u8 kind;        // GaroProjKind — replaces the legacy "paralyze" flag
    Vec3f pos;
    s16 yaw;
    s16 timer;
    // ── ROD_ORB-only fields (ignored for KNIFE / PARALYZE) ──────────────
    u8  rodElement; // 0=normal, 1=fire, 2=ice, 3=light (visual tint + cycle)
    u8  rodDamage;  // 1..4, charge-tier resolved at fire time
    u32 rodDmgFlag; // DMG_ARROW_NORMAL/FIRE/ICE/LIGHT (combined with
                    // DMG_SLASH_MASTER at AC time so restrictive enemies
                    // still take the hit).
} GaroSword;

static struct {
    GaroAttackState state;
    s16 stateTimer;
    // ── v9 rod mode state (replaced legacy spinTotal) ───────────────────
    s16 rodChargeTimer;  // 0..120 — frames B has been held in ROD_AIM. Damage
                         // tier picks at release (1/30/60/90 thresholds).
    u8  rodElement;      // 0=normal, 1=fire, 2=ice, 3=light. Cycled via L/R.
    u8  rodSfxPlayed;    // "fully charged" SFX latch — fires once per aim.
    s16 rodReleaseCD;    // frames after release where B-press cannot re-fire
                         // a slash (prevents rapid alternate-press spam loop).
    u8  laughPending;    // set when an enemy dies; next IDLE frame rolls 20%.
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
    // ── v9.1 banish shadow-ball travel ────────────────────────────────────
    Vec3f shadowBallStart;  // Garo's pos at moment of VANISH end (ball origin)
    Vec3f shadowBallEnd;    // target's pos snapshot (ball destination)
    s16   shadowBallTimer;  // 0..N, increments each SHADOW frame
    // ── v10 A-button overhaul ─────────────────────────────────────────────
    u8  shadowBallInvulnActive;  // 1 while SB owns DISABLE_DRAW + invuln
    u8  shadowBallSlashFired;    // edge-latch so SB quad enables exactly once
    u8  hopDir;                  // 0=back, 1=left-side, 2=right-side, 3=fwd-jump
    s16 hopAirTimer;             // frames spent airborne in current sidehop/backflip
    u8  airSlashActive;          // set on B-in-air, cleared on land
    u8  landStrikeFired;         // edge-latch for LAND_STRIKE quad-SFX
    s16 landStrikeTailFrames;    // counts frames AFTER anim end (tail-out 3f)
    s16 jumpAttackHitStart;      // first elapsed frame JUMP_ATTACK quad is live
    // ── v9 death/reset state ─────────────────────────────────────────────
    // GaroForm_OnDeath spawns 9 flame particles once per death. The flag is
    // cleared on revival (Ikana/Fairy) and on scene reload (MmForm_Reset).
    u8 deathFlamesSpawned;
    // Rising-edge jump multiplier — applied once per jump in MmForm_UpdateActive.
    u8 prevJumping;
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
// v9 rod mode helpers — element table + damage tier resolution
// ============================================================================
// Maps GaroSword.rodElement → AC damage flag. Element 0 (normal) routes the
// hit as DMG_ARROW_NORMAL so non-elemental enemies still take damage; the
// other three (fire/ice/light) hit element-vulnerable bosses (Phantom Ganon
// = arrow flags, Ganon2 weakpoint = DMG_ARROW_LIGHT, Dodongo = fire, etc.).
static u32 GaroAttack_GetRodDmgFlag(u8 element) {
    switch (element) {
        case 1:  return DMG_ARROW_FIRE;
        case 2:  return DMG_ARROW_ICE;
        case 3:  return DMG_ARROW_LIGHT;
        default: return DMG_ARROW_NORMAL;
    }
}

// Resolves the charge-tier damage from the live timer. Capped 1..4 so a
// rapid-fire shot still inflicts something while a fully-charged release
// (≥ tier 4 threshold) deals 4 — same scale as the parry/banish counter
// strikes so element vulnerability is the differentiator, not raw numbers.
static u8 GaroAttack_GetRodDamage(s16 chargeTimer) {
    if (chargeTimer < GARO_ROD_CHARGE_TIER2) return 1;
    if (chargeTimer < GARO_ROD_CHARGE_TIER3) return 2;
    if (chargeTimer < GARO_ROD_CHARGE_TIER4) return 3;
    return 4;
}

// Centralized reset for rod-mode latches. Called on release, on interrupt
// (parry / banish / damage break), and on form change. rodElement is NOT
// cleared — the last-selected element persists across aims (UI continuity).
static void GaroForm_LeaveRodState(void) {
    sGaroAttack.rodChargeTimer = 0;
    sGaroAttack.rodSfxPlayed   = 0;
}

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

// Forward decl of the rod-orb init flag — the actual storage lives near the
// UpdateSwords helpers further down, but GaroForm_Cleanup needs to clear it
// before that block is reachable in TU order.
static u8 sRodOrbQuadsInited;

extern "C" void GaroForm_Cleanup(void) {
    // Skin teardown handled by GaroSkin_Teardown — called by the engine on
    // scene transition via Play's heap reset.
    sGaroAttack = {};
    // Rod-orb AC quads are bound to the prior scene's Play* via
    // Collider_SetQuad. After a scene transition the parent Actor pointer
    // (player) and Play* are stale, so we re-init lazily on the next rod
    // fire. Without this reset, EnsureRodOrbQuads would early-return and
    // StampRodOrbQuad would write to a quad whose base->ac context is gone.
    sRodOrbQuadsInited = 0;
}

// ============================================================================
// v9 — Death / Reset hooks
//
// GaroForm_OnDeath fires SYNCHRONOUSLY from TransformMasks_OnDeath BEFORE
// MmForm_OnDeath rolls back the form. Spawns 9 EffectSsDFire flame particles
// in a ring around the body (MM Garo Master death canon). The OOT death
// cutscene that follows still renders Link normally; the flames live in the
// effect system independent of Player_Draw so they remain visible.
//
// GaroForm_OnReset clears once-per-life flags. Invoked at:
//   - Ikana shield revival (z_player.c)
//   - Fairy revival (z_player.c)
//   - Scene reload / form change (MmForm_Reset)
// ============================================================================
#define GARO_DEATH_FLAME_COUNT  9
#define GARO_DEATH_FLAME_RADIUS 20.0f

extern "C" void GaroForm_OnDeath(Player* player, PlayState* play) {
    if (player == NULL || play == NULL) return;
    if (sGaroAttack.deathFlamesSpawned) return;

    Vec3f center = player->actor.world.pos;
    center.y += 5.0f;
    for (s32 i = 0; i < GARO_DEATH_FLAME_COUNT; i++) {
        f32 ang = (f32)i * ((f32)M_PI * 2.0f / (f32)GARO_DEATH_FLAME_COUNT);
        Vec3f pos = {
            center.x + cosf(ang) * GARO_DEATH_FLAME_RADIUS,
            center.y,
            center.z + sinf(ang) * GARO_DEATH_FLAME_RADIUS,
        };
        Vec3f vel   = { cosf(ang) * 0.5f, 1.5f, sinf(ang) * 0.5f };
        Vec3f accel = { 0.0f, 0.1f, 0.0f };
        EffectSsDFire_Spawn(play, &pos, &vel, &accel, 100, 35, 255, 8, 12);
    }
    sGaroAttack.deathFlamesSpawned = 1;
    // Stal-family death sample doubles as the Garo collapse cry — the
    // dedicated MM Garo death voice lives in mm.o2r and will be wired
    // through TransformMasks_PlayMmVoice once samples ship.
    Audio_PlayActorSound2(&player->actor, NA_SE_EN_STAL_DEAD);
}

extern "C" void GaroForm_OnReset(void) {
    sGaroAttack.deathFlamesSpawned = 0;
    sGaroAttack.prevJumping = 0;
}

// Accessors for the rising-edge jump multiplier, used by MmForm_UpdateActive
// glass-cannon physics. The state lives inside sGaroAttack so it shares the
// same reset / cleanup lifecycle as the rest of the Garo combat machine.
extern "C" u8 GaroForm_GetPrevJumping(void) {
    return sGaroAttack.prevJumping;
}

extern "C" void GaroForm_SetPrevJumping(u8 v) {
    sGaroAttack.prevJumping = v;
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

    // v10: respect PLAYER_STATE2_DISABLE_DRAW for the Garo skin too. The
    // vanilla flag only suppresses Link's own draw path — our hybrid /
    // smooth-skin draw runs independently, so without this guard the
    // shadow-ball / banish "invisible" effect was a no-op visually.
    // Projectiles still draw (orbs, knives) because they're tied to
    // world state, not Link's visibility.
    if (player->stateFlags2 & PLAYER_STATE2_DISABLE_DRAW) {
        extern void GaroForm_DrawProjectiles(PlayState * play);
        GaroForm_DrawProjectiles(play);
        return 1;
    }

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

    // v10: aligned with vanilla Link spin reach (researcher #1) — slightly
    // wider than the swing quad (sweeps around the body) but same forward
    // reach as Link's spin attack. Forward-extending rectangular slab in
    // front of the player; as shape.rot.y rotates each frame, the quad
    // sweeps the full 360° around Garo.
    const f32 nearDist = 10.0f;
    const f32 farDist  = 60.0f;
    const f32 halfW    = 35.0f;
    const f32 yBottom  = 0.0f;
    const f32 yTop     = 55.0f;

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
    // v10: vanilla pattern is DMG_SLASH_MASTER (0x200) for spin/jump-attack
    // and (0x100) for normal swings. Spin variant uses the jump-attack bit
    // so enemies that filter on that bit (Iron Knuckle, etc.) accept it.
    quad->info.toucher.dmgFlags = DMG_SLASH_MASTER | 0x00000100;
    quad->info.toucher.damage = GARO_SPIN_DAMAGE;
    quad->info.toucherFlags = TOUCH_ON | TOUCH_NEAREST;

    CollisionCheck_SetAT(play, &play->colChkCtx, &quad->base);
}

static void GaroAttack_DisableSpinQuad(Player* player) {
    player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
}

// v10.1 cylinder-style AOE quad for the landing strike. Symmetric around
// Garo's position (not forward-extending) so enemies in ANY direction
// within radius get clipped — matches the user spec "collider con cilindro
// grande". The quad is a vertical slab spanning a wide square footprint:
// 130u halfSize per axis = 260u × 260u footprint, 0..130u tall. Quad is
// 4 vertices defining a vertical rectangle; we orient it E-W so the slab
// covers a wide area when CollisionCheck_SetAT processes it. Enemies whose
// AC bumper intersects the slab volume get hit regardless of facing.
static void GaroAttack_EnableLandStrikeQuad(Player* player, PlayState* play) {
    ColliderQuad* quad = &player->meleeWeaponQuads[0];

    const f32 halfSize = 130.0f;  // 260u total radius
    const f32 yBottom  = -20.0f;
    const f32 yTop     = 130.0f;

    Vec3f pos = player->actor.world.pos;

    // World-space axis-aligned vertical slab spanning the player's
    // surroundings. Two top vertices (left/right) and two bottom vertices.
    Vec3f a = { pos.x - halfSize, pos.y + yTop,    pos.z };
    Vec3f b = { pos.x + halfSize, pos.y + yTop,    pos.z };
    Vec3f c = { pos.x + halfSize, pos.y + yBottom, pos.z };
    Vec3f d = { pos.x - halfSize, pos.y + yBottom, pos.z };

    Collider_ResetQuadAT(play, &quad->base);
    Collider_SetQuadVertices(quad, &a, &b, &c, &d);

    quad->base.atFlags = AT_ON | AT_TYPE_PLAYER;
    quad->info.toucher.dmgFlags = DMG_SLASH_MASTER | 0x00000100 | 0x00000200;
    quad->info.toucher.damage = GARO_LAND_STRIKE_DAMAGE;
    quad->info.toucherFlags = TOUCH_ON | TOUCH_NEAREST;

    CollisionCheck_SetAT(play, &play->colChkCtx, &quad->base);
}

// Forward-facing slash quad used during the combo swing hit windows. Same
// geometry as the spin quad but stamped only during the active hit frames of
// each slash (not every frame), matching Link's vanilla sword combo damage
// pattern. Generous reach so the swing reliably connects on close enemies.
static void GaroAttack_EnableSwingQuad(Player* player, PlayState* play) {
    ColliderQuad* quad = &player->meleeWeaponQuads[0];

    // v10: aligned with vanilla Link sword-swing reach (researcher #1).
    // Vanilla uses bone-positioned quads (D_80854650) which we can't
    // mirror exactly in form-local space, but matching the rough
    // dimensions makes Garo's hits feel like Link's rather than the
    // earlier over-reaching box. 60u forward × 50u wide × 60u tall.
    const f32 nearDist = 10.0f;
    const f32 farDist  = 60.0f;
    const f32 halfW    = 25.0f;
    const f32 yBottom  = 0.0f;
    const f32 yTop     = 60.0f;

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
    // v10: vanilla dmgFlags pattern. Bit 0x100 matches Link's 1H sword
    // (D_80854488 table); ORing it onto DMG_SLASH_MASTER means restrictive
    // enemy AC masks that check either bit accept Garo's hits identically
    // to Link's. Damage value is overwritten per-state by the caller
    // (combo=2, parry=4, banish=4, shadow_ball=6, land_strike=8, etc.).
    quad->info.toucher.dmgFlags = DMG_SLASH_MASTER | 0x00000100;
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
            // Rod orbs (longer lifetime) pre-fill src.timer; knives spawn with
            // src.timer == 0 and fall back to the legacy 45-frame default.
            if (sw->timer <= 0) {
                sw->timer = GARO_SWORD_LIFETIME;
            }
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
        tmp.kind = GARO_PROJ_KNIFE;
        GaroAttack_SpawnOne(tmp, player);
    }
}

// Throw a single straight-ahead paralyzing knife. Used at the recover/roll
// frame of the swing combo — distinctive single throw rather than spread.
static void GaroAttack_SpawnSwordParalyze(Player* player) {
    GaroSword tmp = {};
    tmp.pos = GaroAttack_HandOrigin(player);
    tmp.yaw = player->actor.shape.rot.y;
    tmp.kind = GARO_PROJ_PARALYZE;
    GaroAttack_SpawnOne(tmp, player);
}

// ── v9 rod orb projectile ───────────────────────────────────────────────
// Fired when B is released from GARO_ROD_AIM. Damage tier and element flag
// are passed in from the release path (see ROD_AIM state handler). The orb
// travels forward from the player's left hand at GARO_ROD_ORB_SPEED and dies
// on its first AC bumper hit OR when its lifetime expires. The
// DMG_SLASH_MASTER flag is OR'd in at AC time as a fallback for
// restrictive-AC enemies that don't accept arrow flags (Like-Like,
// Iron Knuckle), so the orb still hits them.
#define GARO_ROD_ORB_SPEED      12.0f
#define GARO_ROD_ORB_LIFETIME   60

static void GaroAttack_SpawnRodOrb(Player* player, u8 element, u8 damage, u32 dmgFlag) {
    GaroSword tmp = {};
    tmp.pos = GaroAttack_HandOrigin(player);
    tmp.yaw = player->actor.shape.rot.y;
    tmp.kind = GARO_PROJ_ROD_ORB;
    tmp.rodElement = element;
    tmp.rodDamage = damage;
    tmp.rodDmgFlag = dmgFlag;
    tmp.timer = GARO_ROD_ORB_LIFETIME;  // overrides SpawnOne's default 45-frame fallback
    GaroAttack_SpawnOne(tmp, player);
}

// ── v9 rod orb AC quad pool ─────────────────────────────────────────────
// One quad per sword slot so multiple in-flight orbs can independently
// register damage on the same frame. Init is lazy: the first rod orb
// triggers GaroAttack_EnsureRodOrbQuads which configures all 12 quads at
// once. The quads stay valid for the lifetime of the scene; reset on scene
// reload via GaroForm_Cleanup (sGaroAttack zeroing) — although the
// ColliderQuad internals are pointer-free so survival across reloads is
// harmless.
//
// The dmgFlags mask 0xFFCFFFFF is the canonical "accepts everything except
// reflection" pattern used by Link's sword quad — combined with per-orb
// rodDmgFlag at SetAT time, this lets enemies with restrictive AC masks
// (Iron Knuckle, Like-Like) still take the hit while element-vulnerable
// bosses (Phantom Ganon, Ganon2) get routed to their light/fire/ice paths.
static ColliderQuad sRodOrbQuads[GARO_SWORD_MAX_ACTIVE];
// sRodOrbQuadsInited is forward-declared near GaroForm_Cleanup so the
// cleanup hook can reset it without re-ordering this block.

static ColliderQuadInit sRodOrbQuadInit = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_PLAYER,
        AC_NONE,
        OC1_NONE,
        OC2_TYPE_PLAYER,
        COLSHAPE_QUAD,
    },
    {
        ELEMTYPE_UNK0,
        { 0xFFCFFFFF, 0x00, 0x10 },
        { 0x00000000, 0x00, 0x00 },
        TOUCH_ON | TOUCH_NEAREST | TOUCH_SFX_NORMAL,
        BUMP_NONE,
        OCELEM_NONE,
    },
    // ColliderQuadDimInit wraps a Vec3f quad[4], so the literal needs THREE
    // brace layers: struct → array → per-Vec3f. (Matches z_en_boom.c:52.)
    { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
};

static void GaroAttack_EnsureRodOrbQuads(PlayState* play, Player* player) {
    if (sRodOrbQuadsInited) return;
    for (s32 i = 0; i < GARO_SWORD_MAX_ACTIVE; i++) {
        Collider_InitQuad(play, &sRodOrbQuads[i]);
        Collider_SetQuad(play, &sRodOrbQuads[i], &player->actor, &sRodOrbQuadInit);
    }
    sRodOrbQuadsInited = 1;
}

static void GaroAttack_StampRodOrbQuad(PlayState* play, Player* player, GaroSword* sw, s32 quadIdx) {
    ColliderQuad* quad = &sRodOrbQuads[quadIdx];

    // ~12-unit cube around the orb's current pos. Symmetric so the orb hits
    // enemies from any approach angle equally — element semantics are about
    // weakness routing, not directional contact.
    const f32 half = 8.0f;
    Vec3f a = { sw->pos.x - half, sw->pos.y + half, sw->pos.z };
    Vec3f b = { sw->pos.x + half, sw->pos.y + half, sw->pos.z };
    Vec3f c = { sw->pos.x + half, sw->pos.y - half, sw->pos.z };
    Vec3f d = { sw->pos.x - half, sw->pos.y - half, sw->pos.z };

    Collider_ResetQuadAT(play, &quad->base);
    Collider_SetQuadVertices(quad, &a, &b, &c, &d);

    quad->base.atFlags = AT_ON | AT_TYPE_PLAYER;
    // Combine element flag with DMG_SLASH_MASTER so restrictive-AC enemies
    // (those that only accept weapon flags, not arrow flags) still take the
    // hit. Element-vulnerable enemies route via the matching arrow bit.
    quad->info.toucher.dmgFlags = sw->rodDmgFlag | DMG_SLASH_MASTER;
    quad->info.toucher.damage = sw->rodDamage;
    quad->info.toucherFlags = TOUCH_ON | TOUCH_NEAREST;

    CollisionCheck_SetAT(play, &play->colChkCtx, &quad->base);
}

static void GaroAttack_UpdateSwords(PlayState* play) {
    Player* player = GET_PLAYER(play);

    for (s32 i = 0; i < GARO_SWORD_MAX_ACTIVE; i++) {
        GaroSword* sw = &sGaroAttack.swords[i];
        if (!sw->active) continue;

        // Travel speed depends on the projectile kind. Knives keep the legacy
        // 18.0f speed; rod orbs glide slower at 12.0f (longer lifetime, more
        // tracking time for the player to aim with).
        f32 speed = (sw->kind == GARO_PROJ_ROD_ORB) ? GARO_ROD_ORB_SPEED : GARO_SWORD_SPEED;

        // Advance forward along yaw.
        f32 sinYaw = Math_SinS(sw->yaw);
        f32 cosYaw = Math_CosS(sw->yaw);
        sw->pos.x += sinYaw * speed;
        sw->pos.z += cosYaw * speed;

        sw->timer--;
        if (sw->timer <= 0) {
            sw->active = 0;
            continue;
        }

        // v9: motion trail. One small dust per knife per frame, spawned just
        // BEHIND the blade so the trail reads as backward motion. Skipped
        // for rod orbs since those carry their own XLU tinted draw + element
        // semantics; mixing in dust would muddy the visual.
        if (sw->kind != GARO_PROJ_ROD_ORB) {
            Vec3f trailPos = {
                sw->pos.x - sinYaw * 8.0f,  // 8 units behind the tip
                sw->pos.y,
                sw->pos.z - cosYaw * 8.0f,
            };
            Vec3f zeroVel = { 0.0f, 0.0f, 0.0f };
            Vec3f gravityAccel = { 0.0f, -0.05f, 0.0f };  // light fall
            // Garo violet → fade-to-dark. Matches the sword-swing trail color.
            Color_RGBA8 primColor = { 160,  80, 220, 200 };
            Color_RGBA8 envColor  = {  60,  20, 120, 0 };
            EffectSsDust_Spawn(play, 0, &trailPos, &zeroVel, &gravityAccel,
                               &primColor, &envColor,
                               /* scale */ 60, /* scaleStep */ -3,
                               /* life  */ 10, /* updateMode */ 0);
        }

        if (sw->kind == GARO_PROJ_ROD_ORB) {
            // Rod orb: route through the AC bumper system with proper
            // element dmgFlags so boss vulnerability masks accept the hit.
            // Orbs persist for their full lifetime (piercing semantics) —
            // a fire orb sweeping through a row of enemies is intended.
            GaroAttack_EnsureRodOrbQuads(play, player);
            GaroAttack_StampRodOrbQuad(play, player, sw, i);
            continue;
        }

        // KNIFE / PARALYZE: proximity hit against the enemy actor list, then
        // despawn on the first contact. Knives deal sword damage; paralyze
        // sets freezeTimer (Deku Nut stun mechanism).
        Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].head;
        while (actor != NULL) {
            f32 dx = actor->world.pos.x - sw->pos.x;
            f32 dy = actor->world.pos.y + actor->shape.yOffset - sw->pos.y;
            f32 dz = actor->world.pos.z - sw->pos.z;
            f32 distSq = dx * dx + dy * dy + dz * dz;
            if (distSq < (GARO_SWORD_HIT_RADIUS * GARO_SWORD_HIT_RADIUS)) {
                if (sw->kind == GARO_PROJ_PARALYZE) {
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

// Rod-orb tint colors per element. Indexed by GaroSword.rodElement.
// 0=normal (white) 1=fire (red) 2=ice (cyan) 3=light (gold).
static const u8 sRodOrbColors[4][3] = {
    { 255, 255, 255 }, // normal
    { 255, 128,  48 }, // fire
    { 128, 224, 255 }, // ice
    { 255, 224,  64 }, // light
};

extern "C" void GaroForm_DrawProjectiles(PlayState* play) {
    if (!GaroForm_IsActive()) return;

    // Cache lookup of the sword DL. Refreshed every frame since the resource
    // manager owns the lifetime of the underlying Gfx*.
    Gfx* swordDL = ResourceMgr_LoadGfxByName(GARO_SWORD_DL_PATH);
    if (swordDL == NULL) return;

    OPEN_DISPS(play->state.gfxCtx);

    for (s32 i = 0; i < GARO_SWORD_MAX_ACTIVE; i++) {
        GaroSword* sw = &sGaroAttack.swords[i];
        if (!sw->active) continue;

        // Rod orbs draw on XLU with element tint via PrimColor. Knives draw on
        // OPA with default lighting. We toggle pipelines per-orb to keep the
        // existing knife visuals byte-identical to v8.
        if (sw->kind == GARO_PROJ_ROD_ORB) {
            Gfx_SetupDL_25Xlu(play->state.gfxCtx);
            const u8* c = sRodOrbColors[sw->rodElement & 0x3];
            gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, c[0], c[1], c[2], 200);

            Matrix_Push();
            Matrix_Translate(sw->pos.x, sw->pos.y, sw->pos.z, MTXMODE_NEW);
            Matrix_RotateY((f32)sw->yaw * (M_PI / 0x8000), MTXMODE_APPLY);
            // Slight pulse so the orb reads as magical (not a static blade).
            // GARO_SWORD_DRAW_SCALE * 1.5 so the orb visibly fills the AC quad.
            f32 s = GARO_SWORD_DRAW_SCALE * 1.5f;
            Matrix_Scale(s, s, s, MTXMODE_APPLY);

            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
                      G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, swordDL);

            Matrix_Pop();
            continue;
        }

        Gfx_SetupDL_25Opa(play->state.gfxCtx);

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
// ============================================================================
// Garo full moveset — Goron-style action dispatch
//
// Architecture (mirrors mm_player_form.cpp Goron):
//   - IDLE: NO PAUSE_ACTION_FUNC. Link's vanilla actionFunc runs → items,
//     swim, jump, walk/run all work 1:1. IDLE just observes raw input and
//     transitions to combat states when triggered.
//   - Combat states (SWING_A/B/C, PARRY, DASH, BANISH, ROD_AIM, etc.):
//     SET PAUSE_ACTION_FUNC → Link's actionFunc is suppressed, the form
//     drives the pose via formSkelAnime + memcpy + handles its own quad.
//   - B is stripped before Player_UpdateCommon (TransformMasks_FilterB), so
//     OOT's slash action never starts — combat is fully Garo-owned.
// ============================================================================

static void GaroForm_ResetToIdle(Player* player) {
    sGaroAttack.state = GARO_IDLE;
    sGaroAttack.stateTimer = 0;
    sGaroAttack.comboBPressed = 0;
    sGaroAttack.bReleasedDuringSwing = 0;
    sGaroAttack.firedThisStep = 0;
    sGaroAttack.comboBufferTimer = 0;
    sGaroAttack.parryWindowTimer = 0;
    sGaroAttack.parryAttacker = NULL;
    sGaroAttack.banishTarget = NULL;
    sGaroAttack.shurikensThrownThisFinisher = 0;
    // v10 defensive cleanup — any state that takes ownership of these
    // suppression / latch fields MUST roll them back when bailing out
    // (mask swap, death, scene reload all flow through here eventually).
    player->stateFlags2 &= ~PLAYER_STATE2_DISABLE_DRAW;
    sGaroAttack.shadowBallInvulnActive = 0;
    sGaroAttack.shadowBallSlashFired = 0;
    sGaroAttack.airSlashActive = 0;
    sGaroAttack.landStrikeFired = 0;
    sGaroAttack.landStrikeTailFrames = 0;
    sGaroAttack.hopAirTimer = 0;
    // v10.1: sidehop / backflip temporarily rotates world.rot.y to drive
    // the engine's lateral / backward motion via linearVelocity. We MUST
    // restore world.rot.y to match shape.rot.y so the next "forward
    // movement" intent (running, swinging) doesn't inherit the rotated yaw.
    player->actor.world.rot.y = player->actor.shape.rot.y;
    GaroAttack_DisableSpinQuad(player);
    if (sGaroAttack.trailActive) {
        // Trail killed lazily via centralized check (top of GaroForm_Update).
    }
}

// SWING_A : motion1[0,25]   1st slash, ~12 frames @2x
// SWING_B : motion1[26,43]  2nd slash, ~9 frames @2x
// SWING_C : motion1[44,81]  3rd slash (finisher), ~19 frames @2x — throws shurikens
#define GARO_SWINGA_ANIM_BEG    0
#define GARO_SWINGA_ANIM_END    25
#define GARO_SWINGB_ANIM_BEG    26
#define GARO_SWINGB_ANIM_END    43
#define GARO_SWINGC_ANIM_BEG    44
#define GARO_SWINGC_ANIM_END    81
#define GARO_SWINGA_KNIFE_F     5    // relative stateTimer for knife throw
#define GARO_SWINGB_KNIFE_F     3
#define GARO_SWINGC_KNIFE_F     6    // finisher: 3 shurikens
#define GARO_SWINGA_HIT_BEG     2
#define GARO_SWINGA_HIT_END     11
#define GARO_SWINGB_HIT_BEG     1
#define GARO_SWINGB_HIT_END     8
#define GARO_SWINGC_HIT_BEG     3
#define GARO_SWINGC_HIT_END     14

static void GaroForm_StartSwing(PlayState* play, Player* player, GaroAttackState which) {
    f32 beg = 0.0f, end = 0.0f;
    switch (which) {
        case GARO_SWING_1: beg = GARO_SWINGA_ANIM_BEG; end = GARO_SWINGA_ANIM_END; break;
        case GARO_SWING_2: beg = GARO_SWINGB_ANIM_BEG; end = GARO_SWINGB_ANIM_END; break;
        case GARO_SWING_3: beg = GARO_SWINGC_ANIM_BEG; end = GARO_SWINGC_ANIM_END; break;
        default: return;
    }
    GaroAttack_StartFormAnim(play,
        (LinkAnimationHeader*)gPlayerAnim_link_last_hit_motion1, beg, end,
        GARO_SWING_PLAYSPEED);
    sGaroAttack.state = which;
    sGaroAttack.stateTimer = 0;
    sGaroAttack.firedThisStep = 0;
    sGaroAttack.comboBPressed = 0;
    sGaroAttack.comboBufferTimer = 0;
    if (!sGaroAttack.trailActive) {
        GaroAttack_SpawnTrail(play);
    }
}

extern "C" void GaroForm_Update(PlayState* play, Player* player) {
    // ── Trail VFX lifetime ───────────────────────────────────────────────
    // Sword trail belongs to the slash sequence + parry riposte + banish slash.
    // Centralized kill so individual exit paths don't need to remember.
    GaroAttack_UpdateSwords(play);

    if (!GaroForm_IsActive()) {
        GaroAttack_KillTrail(play);
        GaroForm_ResetToIdle(player);
        return;
    }

    bool trailWanted = (sGaroAttack.state == GARO_SWING_1 || sGaroAttack.state == GARO_SWING_2 ||
                       sGaroAttack.state == GARO_SWING_3 || sGaroAttack.state == GARO_PARRY_RIPOSTE ||
                       sGaroAttack.state == GARO_BANISH_SLASH);
    if (!trailWanted && sGaroAttack.trailActive) {
        GaroAttack_KillTrail(play);
    }

    // ── Cooldown ticks ───────────────────────────────────────────────────
    if (sGaroAttack.banishCooldown > 0) sGaroAttack.banishCooldown--;
    if (sGaroAttack.rodReleaseCD  > 0) sGaroAttack.rodReleaseCD--;

    // ── Blocking state guard ──────────────────────────────────────────────
    // Talking, cutscene, dead, climbing ledge, hooked, etc. — bail to IDLE
    // and DON'T touch any Garo state this frame. Link is doing something OOT
    // that must take precedence.
    const u32 hardBlockMask = PLAYER_STATE1_LOADING | PLAYER_STATE1_TALKING |
                              PLAYER_STATE1_DEAD | PLAYER_STATE1_GETTING_ITEM |
                              PLAYER_STATE1_CARRYING_ACTOR | PLAYER_STATE1_CLIMBING_LEDGE |
                              PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_FIRST_PERSON |
                              PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_IN_ITEM_CS |
                              PLAYER_STATE1_IN_CUTSCENE;
    if (player->stateFlags1 & hardBlockMask) {
        if (sGaroAttack.state != GARO_IDLE) {
            GaroAttack_KillTrail(play);
            GaroForm_ResetToIdle(player);
        }
        return;
    }
    // v9.1: DAMAGED is a "soft" block — every state except PARRY_GUARD bails
    // to idle on damage so Link's knockback anim plays freely. PARRY_GUARD
    // intentionally observes the flag (when our invincibility somehow
    // failed to catch the hit) and handles it via parry/absorb logic.
    if ((player->stateFlags1 & PLAYER_STATE1_DAMAGED) &&
        sGaroAttack.state != GARO_PARRY_GUARD) {
        if (sGaroAttack.state != GARO_IDLE) {
            GaroAttack_KillTrail(play);
            GaroForm_ResetToIdle(player);
        }
        return;
    }

    // ── v9: post-kill detection via enemy-list snapshot diff ─────────────
    // Each frame we record the current enemy pointer set, then compare to
    // last frame's snapshot — any pointer that vanished is treated as a
    // kill / despawn for laughPending purposes. Cheap heuristic; false
    // positives (enemy despawned for non-Garo reasons) cost only a 20%
    // RNG roll. Pointer reuse is a known minor flaw, fine for the laugh
    // rate.
    {
        static uintptr_t sPrevEnemyIds[64];
        static u8        sPrevEnemyCount = 0;
        uintptr_t        curIds[64];
        u8               curCount = 0;
        for (Actor* enemy = play->actorCtx.actorLists[ACTORCAT_ENEMY].head;
             enemy != NULL && curCount < 64; enemy = enemy->next) {
            curIds[curCount++] = (uintptr_t)enemy;
        }
        // Only diff if BOTH frames had enemies. If sPrevEnemyCount == 0 then
        // any vanish event is meaningless (no previous state to compare to);
        // if curCount == 0 then comparing the inner loop short-circuits but
        // every prev id would "vanish", triggering a false laugh whenever
        // the player enters a scene with no enemies. Cap at min count to
        // skip the diff in those edge cases.
        if (sPrevEnemyCount > 0 && curCount > 0) {
            for (u8 i = 0; i < sPrevEnemyCount; i++) {
                u8 stillAlive = 0;
                for (u8 j = 0; j < curCount; j++) {
                    if (sPrevEnemyIds[i] == curIds[j]) { stillAlive = 1; break; }
                }
                if (!stillAlive) {
                    sGaroAttack.laughPending = 1;
                    break;
                }
            }
        }
        if (curCount > 0) {
            memcpy(sPrevEnemyIds, curIds, sizeof(uintptr_t) * curCount);
        }
        sPrevEnemyCount = curCount;
    }

    // ── Raw input read ───────────────────────────────────────────────────
    // B is stripped from sp44 by TransformMasks_FilterB, but the raw
    // play->state.input[0] still has it. That's what we read.
    Input* input = &play->state.input[0];
    bool bHold  = CHECK_BTN_ALL(input->cur.button, BTN_B) != 0;
    bool bPress = CHECK_BTN_ALL(input->press.button, BTN_B) != 0;
    bool rPress = CHECK_BTN_ALL(input->press.button, BTN_R) != 0;
    bool aPress = CHECK_BTN_ALL(input->press.button, BTN_A) != 0;
    bool aHold  = CHECK_BTN_ALL(input->cur.button, BTN_A) != 0;
    bool zHeld  = CHECK_BTN_ALL(input->cur.button, BTN_Z) != 0;
    // v9: BTN_L / BTN_R own rod-mode element cycling (read inline in the
    // ROD_AIM state via input->press.button). No standalone lPress alias —
    // the legacy `lPress = BTN_Z` was unused after the v8 refactor.

    Actor* zTarget = NULL;
    if (Player_IsZTargeting(player) && player->focusActor != NULL) {
        zTarget = player->focusActor;
    }
    bool zEnemy = (zTarget != NULL) && (zTarget->category == ACTORCAT_ENEMY);

    // ── State dispatch ───────────────────────────────────────────────────
    switch (sGaroAttack.state) {

        case GARO_IDLE: {
            // No PAUSE_ACTION_FUNC — Link's actionFunc keeps running. Items,
            // swim, jump, walk/run, OOT shield, all work 1:1.

            // v10 stick orientation — uses OOT's canonical
            // `controlStickDirections[]` (relative to player's facing yaw),
            // populated by Player_UpdateCommon via sControlStickWorldYaw.
            // PLAYER_STICK_DIR_NONE=-1, FORWARD=0, LEFT=1, BACKWARD=2,
            // RIGHT=3. This is the same source the vanilla backflip code
            // reads (z_player.c:4755), so our gating matches OOT's exactly.
            s8 stickDir = player->controlStickDirections[player->controlStickDataIndex];
            bool stickForwardActive = (stickDir == PLAYER_STICK_DIR_FORWARD);
            bool stickBack          = (stickDir == PLAYER_STICK_DIR_BACKWARD);
            bool stickSideL         = (stickDir == PLAYER_STICK_DIR_LEFT);
            bool stickSideR         = (stickDir == PLAYER_STICK_DIR_RIGHT);
            bool stickActive        = (stickDir != PLAYER_STICK_DIR_NONE);
            // "stickForwardOk" for the existing FilterB-mirrored test (anything
            // that isn't back / sideways → fine for Garo A overrides).
            bool stickForwardOk = !stickBack && !stickSideL && !stickSideR;
            bool onGround = (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) != 0;
            bool movingFwd = (player->linearVelocity > 0.5f);

            // R-press → parry guard. Gated on !bHold so a B-hold rod entry
            // doesn't immediately interrupt itself with a parry if R was
            // tapped to cycle elements (rod cycle owns R while bHold is
            // active, parry owns R when B is idle).
            if (rPress && !bHold) {
                LinkAnimationHeader* guard = GaroForm_LoadAnim(GARO_GUARD_PATH);
                if (guard != NULL) {
                    GaroAttack_StartFormAnim(play, guard, 0.0f, -1.0f, 1.0f);
                }
                sGaroAttack.state = GARO_PARRY_GUARD;
                sGaroAttack.stateTimer = 0;
                sGaroAttack.parryWindowTimer = GARO_PARRY_WINDOW;
                sGaroAttack.parryAttacker = NULL;
                break;
            }
            // ─── v10 Z+A air slash entry ─────────────────────────────────
            // B-press in mid-air → AIR_SLASH. Lives in IDLE because Garo
            // stays in IDLE while Link's actionFunc owns vanilla jump/fall.
            // We gate on rodReleaseCD so a rod-shot release → jump → B
            // doesn't accidentally swing during the no-fire window.
            if (bPress && !onGround && !sGaroAttack.airSlashActive &&
                sGaroAttack.rodReleaseCD == 0) {
                LinkAnimationHeader* loop = GaroForm_LoadAnim(GARO_SLASHLOOP_PATH);
                if (loop != NULL) {
                    GaroAttack_StartFormAnim(play, loop, 0.0f, -1.0f, 1.0f);
                }
                sGaroAttack.state = GARO_AIR_SLASH;
                sGaroAttack.stateTimer = 0;
                sGaroAttack.airSlashActive = 1;
                sGaroAttack.landStrikeFired = 0;
                if (!sGaroAttack.trailActive) GaroAttack_SpawnTrail(play);
                Audio_PlayActorSound2(&player->actor, NA_SE_IT_SWORD_SWING);
                break;
            }

            // ─── v10 Z-targeted A dispatch (idle/back/side/forward) ──────
            // Z held + A press → fan out to new states based on stick.
            // Only fires when grounded; A-in-air is the air-slash branch
            // above. Lock-on-enemy + stationary still routes to the
            // legacy 4-state BANISH chain (cinematic teleport-behind);
            // lock-on-empty stationary routes to the new SHADOW_BALL
            // (in-place invuln-slash that ignores target).
            if (aPress && zTarget != NULL && onGround &&
                sGaroAttack.rodReleaseCD == 0) {
                // Backflip — 2x distance. The engine reads linearVelocity
                // along world.rot.y in Actor_MoveForward each frame, so
                // setting velocity.x/z directly is overridden next tick.
                // Trick: rotate world.rot.y by 180° at entry while leaving
                // shape.rot.y alone so the visual stays facing forward.
                // GaroForm_ResetToIdle restores world.rot.y from shape.rot.y
                // when the hop ends, preventing yaw drift.
                if (stickBack) {
                    LinkAnimationHeader* anim = GaroForm_LoadAnim(GARO_JUMPBACK_PATH);
                    if (anim != NULL) {
                        GaroAttack_StartFormAnim(play, anim, 0.0f, -1.0f, 1.0f);
                    }
                    player->actor.world.rot.y = player->actor.shape.rot.y + 0x8000;
                    player->actor.velocity.y = 5.8f;
                    player->linearVelocity   = 12.0f;
                    sGaroAttack.hopDir = 0;
                    sGaroAttack.hopAirTimer = 0;
                    sGaroAttack.state = GARO_BACKFLIP;
                    sGaroAttack.stateTimer = 0;
                    Audio_PlayActorSound2(&player->actor, NA_SE_VO_LI_AUTO_JUMP);
                    break;
                }
                // Sidehop left / right — 1.5x distance (linearVelocity 12.75
                // vs vanilla 8.5). Same world.rot.y trick as backflip so the
                // engine propels Garo laterally while the visual body stays
                // facing the Z-target lock.
                if (stickSideL || stickSideR) {
                    LinkAnimationHeader* anim = GaroForm_LoadAnim(GARO_BOUNCE_PATH);
                    if (anim != NULL) {
                        GaroAttack_StartFormAnim(play, anim, 0.0f, -1.0f, 1.0f);
                    }
                    player->actor.world.rot.y = player->actor.shape.rot.y +
                                                (stickSideL ? -0x4000 : 0x4000);
                    player->actor.velocity.y = 4.5f;     // bumped from 3.5 so hop is visible
                    player->linearVelocity   = 12.75f;   // 1.5x vanilla 8.5
                    sGaroAttack.hopDir = stickSideL ? 1 : 2;
                    sGaroAttack.hopAirTimer = 0;
                    sGaroAttack.state = stickSideL ? GARO_SIDEHOP_L : GARO_SIDEHOP_R;
                    sGaroAttack.stateTimer = 0;
                    Audio_PlayActorSound2(&player->actor, NA_SE_VO_LI_AUTO_JUMP);
                    break;
                }
                // Forward jump-attack — 2x distance, retains run speed.
                if (stickForwardActive && movingFwd) {
                    LinkAnimationHeader* anim = GaroForm_LoadAnim(GARO_APPEAR_PATH);
                    if (anim != NULL) {
                        GaroAttack_StartFormAnim(play, anim, 0.0f, -1.0f, 1.5f);
                    }
                    f32 keepFwd = player->linearVelocity;
                    if (keepFwd < 10.0f) keepFwd = 10.0f;  // 2x vanilla floor
                    player->actor.velocity.y = 7.5f;
                    player->linearVelocity   = keepFwd;
                    player->actor.velocity.x = Math_SinS(player->actor.shape.rot.y) * keepFwd;
                    player->actor.velocity.z = Math_CosS(player->actor.shape.rot.y) * keepFwd;
                    sGaroAttack.hopDir = 3;
                    sGaroAttack.hopAirTimer = 0;
                    sGaroAttack.jumpAttackHitStart = 4;
                    sGaroAttack.state = GARO_JUMP_ATTACK;
                    sGaroAttack.stateTimer = 0;
                    Audio_PlayActorSound2(&player->actor, NA_SE_VO_LI_AUTO_JUMP);
                    break;
                }
                // Stationary (stick neutral) — full SHADOW_BALL chain.
                // Phase 1: garo_collapse (visible, "Garo dissolves").
                // Phase 2: BANISH_SHADOW — invisible Garo, particle cluster
                //          travels Garo → enemy.
                // Phase 3: GARO_SHADOW_BALL — teleport behind enemy (if
                //          locked), Garo VISIBLE, plays appearDrawSwords
                //          @1.5x, damage 6 at elapsed frame 20.
                // If no enemy is locked, the chain still runs but the
                // "destination" defaults to Garo's own position (in-place
                // anim with no teleport).
                if (!stickActive) {
                    LinkAnimationHeader* collapse = GaroForm_LoadAnim(GARO_COLLAPSE_PATH);
                    if (collapse != NULL) {
                        GaroAttack_StartFormAnim(play, collapse, 0.0f, -1.0f, 1.0f);
                    }
                    sGaroAttack.state = GARO_BANISH_VANISH;
                    sGaroAttack.stateTimer = 0;
                    sGaroAttack.banishTarget = zTarget;  // may be NULL — handled
                    sGaroAttack.shadowBallSlashFired = 0;
                    Audio_PlayActorSound2(&player->actor, NA_SE_EV_FANTOM_WARP_S);
                    break;
                }
            }

            // A-press + Z-target enemy → banish (with cooldown). v9.1: STUN
            // the target on entry so the visual sequence (collapse → shadow
            // ball travel → reappear → slash) doesn't get interrupted by
            // the target moving / attacking back, and surround them with
            // dark particles to telegraph the lock. Identical mechanic
            // to equip_divine_shield.c's perfect-parry AOE, but a single
            // target + dark palette instead of AOE ice. v10: gated on
            // !stickActive — back/side stick routes to the new sidehop /
            // backflip handlers above, forward+speed routes to JUMP_ATTACK.
            if (aPress && zEnemy && sGaroAttack.banishCooldown == 0 &&
                onGround && !stickActive) {
                LinkAnimationHeader* collapse = GaroForm_LoadAnim(GARO_COLLAPSE_PATH);
                if (collapse != NULL) {
                    GaroAttack_StartFormAnim(play, collapse, 0.0f, -1.0f, 1.0f);
                }
                sGaroAttack.state = GARO_BANISH_VANISH;
                sGaroAttack.stateTimer = 0;
                sGaroAttack.banishTarget = zTarget;
                sGaroAttack.banishCooldown = GARO_BANISH_COOLDOWN;

                // Stun: freezeTimer halts the target's update loop for the
                // duration of the full banish sequence (vanish + shadow
                // travel + appear + slash ≈ 50-60 frames).
                zTarget->freezeTimer = GARO_BANISH_STUN_FRAMES;
                // Color filter: dark blue tint (kColorFilterColorFlagBlue)
                // signals "marked / locked-on" — palette matches divine
                // shield freeze but darker.
                Actor_SetColorFilter(zTarget, 0x0000, 0xF8, 0x0000, GARO_BANISH_STUN_FRAMES);

                // Dark sparkles ring around the stunned target.
                Vec3f sparkPos;
                Vec3f sparkVel   = { 0.0f, 1.5f, 0.0f };
                Vec3f sparkAccel = { 0.0f, -0.05f, 0.0f };
                Color_RGBA8 primColor = { 140,  80, 220, 255 };  // violet
                Color_RGBA8 envColor  = {  40,  10, 100, 0 };    // near-black violet
                for (s32 i = 0; i < 8; i++) {
                    f32 ang = (f32)i * ((f32)M_PI * 2.0f / 8.0f);
                    sparkPos.x = zTarget->world.pos.x + cosf(ang) * GARO_BANISH_STUN_RADIUS;
                    sparkPos.y = zTarget->world.pos.y + 30.0f;
                    sparkPos.z = zTarget->world.pos.z + sinf(ang) * GARO_BANISH_STUN_RADIUS;
                    sparkVel.x = cosf(ang) * 0.5f;
                    sparkVel.z = sinf(ang) * 0.5f;
                    EffectSsKiraKira_SpawnSmall(play, &sparkPos, &sparkVel, &sparkAccel,
                                                &primColor, &envColor);
                }

                Audio_PlayActorSound2(&player->actor, NA_SE_EV_FANTOM_WARP_S);
                Audio_PlayActorSound2(zTarget, NA_SE_IT_SHIELD_REFLECT_SW);
                break;
            }
            // v10: A-press + Z-target NPC handler removed. The v10 Z+A
            // dispatcher above routes "Z + stationary + no-enemy lock" to
            // SHADOW_BALL, which replaces the old look_around fluff with
            // a proper combat option. The GARO_LOOK_AROUND state handler
            // remains defined for compatibility with any external trigger
            // but is unreachable from the IDLE input flow now.
            // v10 A-PRESS no Z-target → Goron-roll-style dash. Stick
            // direction doesn't gate entry (the dash handler steers itself
            // via stick each frame, like Pegasus boots / Goron rolling),
            // so even stick-back A spins Garo's facing during the dash
            // rather than triggering a vanilla backflip (no Z-target = no
            // backflip in vanilla anyway). Do NOT StartFormAnim here;
            // ANIMMODE_ONCE would freeze; DASH_ATTACK handler re-inits
            // with ANIMMODE_LOOP.
            // v10.1: dash only when Z is NOT held. Holding Z without a
            // locked target previously fell through to dash; that's
            // confusing because the player visibly committed to "I want
            // to lock something / evade" by holding Z. Now Z-held + A is
            // a no-op when no target is acquired.
            if (aPress && !zHeld && onGround &&
                sGaroAttack.rodReleaseCD == 0) {
                GaroAttack_EnsureFormSkelAnime(play);
                sGaroAttack.state = GARO_DASH_ATTACK;
                sGaroAttack.stateTimer = 0;
                break;
            }
            // B-press on ground → instant SWING_A. Air-B is captured by
            // the v10 AIR_SLASH dispatcher above; this only runs when the
            // player is grounded. Gated on rodReleaseCD so a rod release →
            // instant B re-press doesn't loop into the slash combo.
            if (bPress && onGround && sGaroAttack.rodReleaseCD == 0) {
                GaroForm_StartSwing(play, player, GARO_SWING_1);
                sGaroAttack.bReleasedDuringSwing = 0;
                break;
            }

            // v9: post-kill laugh taunt — 20% chance per kill-event return to
            // idle. Fires only when no other input action took the frame
            // (this is the last check in IDLE). The laugh anim plays without
            // PAUSE_ACTION_FUNC so movement can interrupt it cleanly.
            if (sGaroAttack.laughPending) {
                sGaroAttack.laughPending = 0;
                if (Rand_ZeroOne() < GARO_LAUGH_CHANCE) {
                    LinkAnimationHeader* laugh = GaroForm_LoadAnim(GARO_LAUGH_PATH);
                    if (laugh != NULL) {
                        GaroAttack_StartFormAnim(play, laugh, 0.0f, -1.0f, 1.0f);
                        sGaroAttack.state = GARO_LAUGH_TAUNT;
                        sGaroAttack.stateTimer = 0;
                        // No dedicated MM voice ID for laugh — fallback to
                        // the Skull Kid laugh SFX (most thematically aligned
                        // with the Garo Master ninja vibe). When a Garo
                        // laugh sample is added to mm.o2r, swap this out
                        // for TransformMasks_PlayMmVoice(0x?? + 0x60).
                        Audio_PlayActorSound2(&player->actor, NA_SE_VO_SK_LAUGH);
                    }
                }
            }
            break;
        }

        // ────────────────────────────────────────────────────────────────────
        // SWING chain: A → B (B-repress) → C (B-repress) → IDLE.
        // Hold B continuously through SWING_A without re-pressing → ROD_AIM.
        // ────────────────────────────────────────────────────────────────────
        case GARO_SWING_1:
        case GARO_SWING_2:
        case GARO_SWING_3: {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->actor.velocity.x = 0;
            player->actor.velocity.z = 0;
            player->linearVelocity = 0;

            // Sticky B-press flag for chain.
            if (bPress && sGaroAttack.state != GARO_SWING_3) {
                sGaroAttack.comboBPressed = 1;
            }
            // Track if B was ever released during SWING_A (decides rod mode at end).
            if (!bHold && sGaroAttack.state == GARO_SWING_1) {
                sGaroAttack.bReleasedDuringSwing = 1;
            }

            // Per-step knife throw + hit window.
            s16 fireFrame = 0, hitBeg = 0, hitEnd = -1;
            switch (sGaroAttack.state) {
                case GARO_SWING_1: fireFrame = GARO_SWINGA_KNIFE_F; hitBeg = GARO_SWINGA_HIT_BEG; hitEnd = GARO_SWINGA_HIT_END; break;
                case GARO_SWING_2: fireFrame = GARO_SWINGB_KNIFE_F; hitBeg = GARO_SWINGB_HIT_BEG; hitEnd = GARO_SWINGB_HIT_END; break;
                case GARO_SWING_3: fireFrame = GARO_SWINGC_KNIFE_F; hitBeg = GARO_SWINGC_HIT_BEG; hitEnd = GARO_SWINGC_HIT_END; break;
                default: break;
            }
            // Finisher (SWING_C) throws 3 shurikens; SWING_A/B no projectile.
            if (sGaroAttack.state == GARO_SWING_3 && sGaroAttack.stateTimer == fireFrame &&
                !sGaroAttack.firedThisStep) {
                GaroAttack_SpawnSwordTriple(player);
                sGaroAttack.firedThisStep = 1;
                Audio_PlayActorSound2(&player->actor, NA_SE_IT_SWORD_SWING);
            }
            // Hit window — Garo swing quad (sword damage).
            bool inHit = (sGaroAttack.stateTimer >= hitBeg) && (sGaroAttack.stateTimer <= hitEnd);
            if (inHit) {
                GaroAttack_EnableSwingQuad(player, play);
            } else {
                player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
            }

            // Advance form anim. Done when curFrame >= endFrame.
            s32 done = GaroAttack_AdvanceFormAnim(play, player);
            sGaroAttack.stateTimer++;
            if (!done) break;

            // Anim end: post-input buffer for chain fluidity.
            bool isChainable = (sGaroAttack.state != GARO_SWING_3);
            if (isChainable && !sGaroAttack.comboBPressed &&
                sGaroAttack.comboBufferTimer < GARO_COMBO_BUFFER_FRAMES) {
                sGaroAttack.comboBufferTimer++;
                player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
                break;
            }
            // Decide next state.
            player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
            sGaroAttack.firedThisStep = 0;
            sGaroAttack.stateTimer = 0;
            sGaroAttack.comboBufferTimer = 0;

            if (sGaroAttack.state == GARO_SWING_1) {
                if (sGaroAttack.comboBPressed) {
                    GaroForm_StartSwing(play, player, GARO_SWING_2);
                } else if (!sGaroAttack.bReleasedDuringSwing && bHold) {
                    // Held B continuously → rod mode aim. Reset charge state
                    // so this aim starts from 0 (rodElement persists for UI
                    // continuity across aims).
                    GaroForm_LeaveRodState();
                    LinkAnimationHeader* tremble = GaroForm_LoadAnim(GARO_TREMBLE_PATH);
                    if (tremble != NULL) {
                        GaroAttack_StartFormAnim(play, tremble, 0.0f, -1.0f, 1.0f);
                    }
                    sGaroAttack.state = GARO_ROD_AIM;
                } else {
                    GaroForm_ResetToIdle(player);
                }
            } else if (sGaroAttack.state == GARO_SWING_2) {
                if (sGaroAttack.comboBPressed) {
                    GaroForm_StartSwing(play, player, GARO_SWING_3);
                } else {
                    GaroForm_ResetToIdle(player);
                }
            } else {
                // SWING_3 (finisher) done → idle.
                GaroForm_ResetToIdle(player);
            }
            break;
        }

        // ────────────────────────────────────────────────────────────────────
        // GARO_ROD_AIM: charge a magic orb. Held tremble anim drives the
        // visual; rodChargeTimer ramps 0..120 to pick a damage tier on
        // release. L cycles element backward, R forward (normal → fire →
        // ice → light → wrap). B release plays garo_takeOutBomb for a
        // beat and spawns the rod orb projectile via GaroAttack_SpawnRodOrb.
        // ────────────────────────────────────────────────────────────────────
        case GARO_ROD_AIM: {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->actor.velocity.x = 0;
            player->actor.velocity.z = 0;
            player->linearVelocity = 0;

            sGaroAttack.stateTimer++;
            if (sGaroAttack.rodChargeTimer < GARO_ROD_CHARGE_MAX) {
                sGaroAttack.rodChargeTimer++;
            }

            // Latched "fully charged" SFX so the player gets audio feedback
            // when the max tier unlocks. Plays exactly once per aim.
            if (!sGaroAttack.rodSfxPlayed &&
                sGaroAttack.rodChargeTimer >= GARO_ROD_CHARGE_TIER4) {
                Audio_PlayActorSound2(&player->actor, NA_SE_IT_MAGIC_ARROW_SHOT);
                sGaroAttack.rodSfxPlayed = 1;
            }

            // Element cycling — BTN_L (prev) / BTN_R (next). Pattern
            // mirrored from ArrowCycle.cpp:RegisterArrowCycle. BTN_L/BTN_R
            // are NOT stripped by TransformMasks_FilterB (that filter only
            // strips B for Garo), so we read them from the raw
            // play->state.input[0] copy. This is safe because the cycle
            // only triggers in GARO_ROD_AIM state, which only exists when
            // Garo is the active form and B is being held — vanilla
            // bow-aim L/R cycling can't be live at the same time.
            if (CHECK_BTN_ALL(input->press.button, BTN_L)) {
                sGaroAttack.rodElement =
                    (sGaroAttack.rodElement + GARO_ROD_ELEMENT_COUNT - 1) % GARO_ROD_ELEMENT_COUNT;
                Audio_PlayActorSound2(&player->actor, NA_SE_SY_DECIDE);
            }
            if (CHECK_BTN_ALL(input->press.button, BTN_R)) {
                sGaroAttack.rodElement =
                    (sGaroAttack.rodElement + 1) % GARO_ROD_ELEMENT_COUNT;
                Audio_PlayActorSound2(&player->actor, NA_SE_SY_DECIDE);
            }

            // B released → fire the orb.
            if (!bHold) {
                u8  dmg     = GaroAttack_GetRodDamage(sGaroAttack.rodChargeTimer);
                u32 dmgFlag = GaroAttack_GetRodDmgFlag(sGaroAttack.rodElement);

                // Play the "take out bomb" anim as a brief release pose so
                // the orb-spawn moment reads visually instead of snapping
                // back to idle. The anim runs once; we transition to IDLE
                // after spawning so subsequent input flows normally.
                LinkAnimationHeader* release = GaroForm_LoadAnim(GARO_TAKEOUTBOMB_PATH);
                if (release != NULL) {
                    GaroAttack_StartFormAnim(play, release, 0.0f, -1.0f, 1.5f);
                }

                GaroAttack_SpawnRodOrb(player, sGaroAttack.rodElement, dmg, dmgFlag);
                Audio_PlayActorSound2(&player->actor, NA_SE_IT_SWORD_SWING_HARD);

                GaroForm_LeaveRodState();
                // Block IDLE's B-press → SWING_1 re-entry for a few frames so
                // a release → rapid re-press doesn't loop into the slash combo.
                sGaroAttack.rodReleaseCD = GARO_ROD_RELEASE_CD;
                GaroForm_ResetToIdle(player);
            }
            break;
        }

        // ────────────────────────────────────────────────────────────────────
        // PARRY: garo_guard for 20 frames. If hit in window → riposte.
        // ────────────────────────────────────────────────────────────────────
        case GARO_PARRY_GUARD: {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->actor.velocity.x = 0;
            player->actor.velocity.z = 0;
            player->linearVelocity = 0;

            // v9.1: full damage immunity while shielding (mirrors Goron's
            // spike defense — while you're committed to guard, nothing
            // gets through). Sustain invincibilityTimer each frame so
            // every AC hit this frame is rejected at the chokepoint
            // (func_80837B18_modified short-circuits when invuln > 0).
            if (player->invincibilityTimer < 5) {
                player->invincibilityTimer = 5;
            }

            bool rStillHeld = CHECK_BTN_ALL(input->cur.button, BTN_R) != 0;
            bool inPerfectWindow = (sGaroAttack.parryWindowTimer > 0);

            // Hit detection. Persistent invincibility prevents
            // PLAYER_STATE1_DAMAGED from ever being set (the damage is
            // rejected before the action func switch), so we observe the
            // AC engine's bumper trigger directly: cylinder.base.ac is the
            // attacker that touched our cylinder this frame regardless of
            // damage outcome. We clear it after handling so consecutive
            // frames don't re-fire on the same touch.
            Actor* attacker = player->cylinder.base.ac;
            bool gotHit = (attacker != NULL && attacker->update != NULL);
            if (gotHit) {
                player->cylinder.base.ac = NULL;
                player->stateFlags1 &= ~PLAYER_STATE1_DAMAGED;  // defensive
                player->invincibilityTimer = 20;

                if (inPerfectWindow) {
                    // Perfect parry — clone divine_shield's AOE: freeze
                    // every enemy in radius + ice-violet sparkles + sound.
                    Actor* enemy = play->actorCtx.actorLists[ACTORCAT_ENEMY].head;
                    while (enemy != NULL) {
                        f32 dx = enemy->world.pos.x - player->actor.world.pos.x;
                        f32 dz = enemy->world.pos.z - player->actor.world.pos.z;
                        if ((dx * dx + dz * dz) <= (GARO_PARRY_AOE_RADIUS * GARO_PARRY_AOE_RADIUS)) {
                            enemy->freezeTimer = GARO_PARRY_FREEZE_FRAMES;
                            // Dark-blue color filter — mirrors divine
                            // shield's 0x0000 blue but stronger alpha.
                            Actor_SetColorFilter(enemy, 0x0000, 0xF8, 0x0000,
                                                 GARO_PARRY_FREEZE_FRAMES);

                            Vec3f spPos;
                            Vec3f spVel   = { 0.0f, 1.0f, 0.0f };
                            Vec3f spAccel = { 0.0f, 0.0f, 0.0f };
                            Color_RGBA8 primColor = { 200, 220, 255, 255 };
                            Color_RGBA8 envColor  = { 100, 150, 255, 0 };
                            for (s32 k = 0; k < 6; k++) {
                                spPos.x = enemy->world.pos.x + Rand_CenteredFloat(60.0f);
                                spPos.y = enemy->world.pos.y + 20.0f + Rand_ZeroFloat(40.0f);
                                spPos.z = enemy->world.pos.z + Rand_CenteredFloat(60.0f);
                                spVel.x = Rand_CenteredFloat(3.0f);
                                spVel.y = Rand_ZeroFloat(2.0f) + 1.0f;
                                spVel.z = Rand_CenteredFloat(3.0f);
                                EffectSsKiraKira_SpawnSmall(play, &spPos, &spVel, &spAccel,
                                                            &primColor, &envColor);
                            }
                        }
                        enemy = enemy->next;
                    }

                    // Riposte: teleport behind the attacker (captured at the
                    // top of this case before we cleared cylinder.base.ac).
                    // Fall back to focusActor if the attacker pointer turned
                    // stale between the AC trigger and now.
                    Actor* riposteTarget = (attacker != NULL && attacker->update != NULL)
                                               ? attacker
                                               : player->focusActor;
                    sGaroAttack.parryAttacker = riposteTarget;
                    if (riposteTarget != NULL) {
                        s16 aYaw = riposteTarget->shape.rot.y;
                        f32 sx = Math_SinS(aYaw), cz = Math_CosS(aYaw);
                        player->actor.world.pos.x = riposteTarget->world.pos.x - sx * GARO_RIPOSTE_OFFSET;
                        player->actor.world.pos.z = riposteTarget->world.pos.z - cz * GARO_RIPOSTE_OFFSET;
                        player->actor.world.pos.y = riposteTarget->world.pos.y;
                        player->actor.world.rot.y = aYaw;
                        player->actor.shape.rot.y = aYaw;
                    }
                    LinkAnimationHeader* slash = GaroForm_LoadAnim(GARO_SLASHSTART_PATH);
                    if (slash != NULL) {
                        GaroAttack_StartFormAnim(play, slash, 0.0f, -1.0f, 1.0f);
                    }
                    sGaroAttack.state = GARO_PARRY_RIPOSTE;
                    sGaroAttack.stateTimer = 0;
                    if (!sGaroAttack.trailActive) GaroAttack_SpawnTrail(play);
                    Audio_PlaySoundGeneral(NA_SE_IT_SHIELD_REFLECT_SW,
                                           &player->actor.world.pos, 4,
                                           &gSfxDefaultFreqAndVolScale,
                                           &gSfxDefaultFreqAndVolScale,
                                           &gSfxDefaultReverb);
                    break;
                }
                // Outside the perfect window — passive full-block. No AOE,
                // no riposte, just absorbed. The shield SFX still plays
                // so the player gets audio feedback that the hit landed.
                Audio_PlayActorSound2(&player->actor, NA_SE_IT_SHIELD_BOUND);
            }

            if (sGaroAttack.parryWindowTimer > 0) sGaroAttack.parryWindowTimer--;
            sGaroAttack.stateTimer++;
            // v9.1: window expiring no longer exits to idle — the guard is
            // passive-block from that point on. Only an R release ends it.
            if (!rStillHeld) {
                GaroForm_ResetToIdle(player);
            }
            break;
        }

        case GARO_PARRY_RIPOSTE: {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->actor.velocity.x = 0;
            player->actor.velocity.z = 0;
            player->linearVelocity = 0;

            if (sGaroAttack.stateTimer >= 4 && sGaroAttack.stateTimer <= 14) {
                GaroAttack_EnableSwingQuad(player, play);
                player->meleeWeaponQuads[0].info.toucher.damage = GARO_PARRY_DAMAGE;
            } else {
                player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
            }

            s32 done = GaroAttack_AdvanceFormAnim(play, player);
            sGaroAttack.stateTimer++;
            if (done) {
                player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
                GaroForm_ResetToIdle(player);
            }
            break;
        }

        // ────────────────────────────────────────────────────────────────────
        // DASH: A-hold forward at v=14. Stick lateral → spin variant.
        // ────────────────────────────────────────────────────────────────────
        case GARO_DASH_ATTACK: {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;

            f32 stickMag = GaroForm_StickMag(play);
            s16 stickAngle = GaroForm_StickAngle(play);

            // v10.1: smoother steering. Always use dashAttack anim (removed
            // the spinAttack variant switch — flipping anim mid-dash made
            // the visual feel janky as the loop reset every sharp turn).
            // Turn rate dropped 0x800 → 0x400 (≈5.6°/frame) so the rotation
            // is Pegasus-boots-heavy rather than instant, requiring the
            // player to commit to a direction (Goron-roll feel).
            LinkAnimationHeader* anim = GaroForm_LoadAnim(GARO_DASHATTACK_PATH);
            if (anim != NULL && sFormSkelAnime.animation != (void*)anim) {
                LinkAnimation_Change(play, &sFormSkelAnime, anim, 1.0f, 0.0f,
                                     Animation_GetLastFrame(anim), ANIMMODE_LOOP, -4.0f);
            }

            if (stickMag > 0.3f) {
                Math_ScaledStepToS(&player->actor.world.rot.y, stickAngle, 0x400);
            }
            player->actor.shape.rot.y = player->actor.world.rot.y;
            // Set linearVelocity only — the engine's Actor_MoveForward will
            // apply it along world.rot.y next physics tick. Skipping the
            // velocity.x/z direct write avoids the one-frame mismatch that
            // contributed to the jankiness.
            player->linearVelocity = GARO_DASH_SPEED;

            GaroAttack_EnableSwingQuad(player, play);
            player->meleeWeaponQuads[0].info.toucher.damage = GARO_DASH_DAMAGE;

            (void)GaroAttack_AdvanceFormAnim(play, player);

            if (player->actor.bgCheckFlags & 0x08) {
                // Wall hit → stop.
                player->linearVelocity = 0;
                GaroForm_ResetToIdle(player);
                break;
            }
            if (!aHold) {
                player->linearVelocity = 0;
                GaroForm_ResetToIdle(player);
            }
            sGaroAttack.stateTimer++;
            break;
        }

        // ────────────────────────────────────────────────────────────────────
        // BANISH: collapse → teleport → appear → slash.
        // ────────────────────────────────────────────────────────────────────
        case GARO_BANISH_VANISH: {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->actor.velocity.x = 0;
            player->actor.velocity.z = 0;
            player->linearVelocity = 0;

            if (sGaroAttack.stateTimer < 8) {
                player->actor.world.pos.y -= 1.0f;
            } else if (sGaroAttack.stateTimer == 8) {
                player->stateFlags2 |= PLAYER_STATE2_DISABLE_DRAW;
            }
            s32 done = GaroAttack_AdvanceFormAnim(play, player);
            sGaroAttack.stateTimer++;
            if (done || sGaroAttack.stateTimer >= GARO_BANISH_VANISH_END) {
                // Snapshot start (Garo collapse pos) + end (target pos) for
                // the shadow-ball lerp. Stay DISABLE_DRAW the whole time the
                // ball is in flight; Garo only re-appears in SHADOW_BALL
                // (the appearDrawSwords anim) after the travel completes.
                Actor* target = sGaroAttack.banishTarget;
                sGaroAttack.shadowBallStart = player->actor.world.pos;
                sGaroAttack.shadowBallStart.y += 30.0f;  // chest height
                if (target != NULL && target->update != NULL) {
                    sGaroAttack.shadowBallEnd   = target->world.pos;
                    sGaroAttack.shadowBallEnd.y += 30.0f;
                } else {
                    // v10.1: no target locked → the shadow ball "travels"
                    // to a point a short distance in front of Garo's
                    // facing. This lets the SHADOW chain still play
                    // visibly (the user wanted the full flow, not an
                    // abort). Garo doesn't teleport in this case — the
                    // SHADOW state arrival in-place plays the appearDrawSwords.
                    f32 sinY = Math_SinS(player->actor.shape.rot.y);
                    f32 cosY = Math_CosS(player->actor.shape.rot.y);
                    sGaroAttack.shadowBallEnd = player->actor.world.pos;
                    sGaroAttack.shadowBallEnd.x += sinY * 60.0f;
                    sGaroAttack.shadowBallEnd.z += cosY * 60.0f;
                    sGaroAttack.shadowBallEnd.y += 30.0f;
                }
                sGaroAttack.shadowBallTimer = 0;
                sGaroAttack.state = GARO_BANISH_SHADOW;
                Audio_PlayActorSound2(&player->actor, NA_SE_EV_FANTOM_WARP_S);
            }
            break;
        }
        case GARO_BANISH_SHADOW: {
            // Garo stays DISABLE_DRAW (invisible). A ball of dark sparkles
            // lerps from shadowBallStart to shadowBallEnd over
            // GARO_BANISH_SHADOW_LEN frames. On arrival, teleport Garo
            // behind the (still-stunned) target and transition to APPEAR.
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->actor.velocity.x = 0;
            player->actor.velocity.z = 0;
            player->linearVelocity = 0;
            player->stateFlags2 |= PLAYER_STATE2_DISABLE_DRAW;

            f32 t = (f32)sGaroAttack.shadowBallTimer / (f32)GARO_BANISH_SHADOW_LEN;
            if (t > 1.0f) t = 1.0f;
            Vec3f ballPos = {
                sGaroAttack.shadowBallStart.x + (sGaroAttack.shadowBallEnd.x - sGaroAttack.shadowBallStart.x) * t,
                sGaroAttack.shadowBallStart.y + (sGaroAttack.shadowBallEnd.y - sGaroAttack.shadowBallStart.y) * t,
                sGaroAttack.shadowBallStart.z + (sGaroAttack.shadowBallEnd.z - sGaroAttack.shadowBallStart.z) * t,
            };

            // Spawn a tight cluster of dark-violet sparkles centered on the
            // ball each frame — the cluster IS the visible "shadow ball".
            Vec3f sparkVel   = { 0.0f, 0.0f, 0.0f };
            Vec3f sparkAccel = { 0.0f, 0.0f, 0.0f };
            Color_RGBA8 primColor = { 120,  60, 200, 255 };
            Color_RGBA8 envColor  = {  40,  10, 100, 0 };
            for (s32 i = 0; i < 4; i++) {
                Vec3f sparkPos = {
                    ballPos.x + Rand_CenteredFloat(12.0f),
                    ballPos.y + Rand_CenteredFloat(12.0f),
                    ballPos.z + Rand_CenteredFloat(12.0f),
                };
                sparkVel.x = Rand_CenteredFloat(0.4f);
                sparkVel.y = Rand_CenteredFloat(0.4f);
                sparkVel.z = Rand_CenteredFloat(0.4f);
                EffectSsKiraKira_SpawnSmall(play, &sparkPos, &sparkVel, &sparkAccel,
                                            &primColor, &envColor);
            }

            sGaroAttack.shadowBallTimer++;
            if (sGaroAttack.shadowBallTimer >= GARO_BANISH_SHADOW_LEN) {
                // v10.1 arrival: teleport behind target (if locked), then
                // enter GARO_SHADOW_BALL which plays appearDrawSwords @1.5x
                // and stamps a master-sword quad with damage 6 at elapsed
                // frame 20. The OLD BANISH_APPEAR / BANISH_SLASH chain is
                // dead — appearDrawSwords combines the "appear" pose and
                // the strike pose into one anim, matching the user spec.
                Actor* target = sGaroAttack.banishTarget;
                if (target != NULL && target->update != NULL) {
                    s16 tYaw = target->shape.rot.y;
                    f32 sx = Math_SinS(tYaw), cz = Math_CosS(tYaw);
                    player->actor.world.pos.x = target->world.pos.x - sx * GARO_BANISH_OFFSET;
                    player->actor.world.pos.z = target->world.pos.z - cz * GARO_BANISH_OFFSET;
                    player->actor.world.pos.y = target->world.pos.y;
                    player->actor.world.rot.y = tYaw;
                    player->actor.shape.rot.y = tYaw;
                }
                LinkAnimationHeader* appear = GaroForm_LoadAnim(GARO_APPEARDRAWSWORDS_PATH);
                if (appear != NULL) {
                    GaroAttack_StartFormAnim(play, appear, 0.0f, -1.0f, 1.5f);
                }
                player->stateFlags2 &= ~PLAYER_STATE2_DISABLE_DRAW;
                Audio_PlayActorSound2(&player->actor, NA_SE_EV_FANTOM_WARP_L);
                sGaroAttack.state = GARO_SHADOW_BALL;
                sGaroAttack.stateTimer = 0;
                sGaroAttack.shadowBallSlashFired = 0;
                if (!sGaroAttack.trailActive) GaroAttack_SpawnTrail(play);
            }
            break;
        }
        case GARO_BANISH_APPEAR: {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->actor.velocity.x = 0;
            player->actor.velocity.z = 0;
            player->linearVelocity = 0;
            s32 done = GaroAttack_AdvanceFormAnim(play, player);
            sGaroAttack.stateTimer++;
            if (done) {
                LinkAnimationHeader* slash = GaroForm_LoadAnim(GARO_SLASHSTART_PATH);
                if (slash != NULL) {
                    GaroAttack_StartFormAnim(play, slash, 0.0f, -1.0f, 1.0f);
                }
                sGaroAttack.state = GARO_BANISH_SLASH;
                sGaroAttack.stateTimer = 0;
                if (!sGaroAttack.trailActive) GaroAttack_SpawnTrail(play);
            }
            break;
        }
        case GARO_BANISH_SLASH: {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->actor.velocity.x = 0;
            player->actor.velocity.z = 0;
            player->linearVelocity = 0;
            if (sGaroAttack.stateTimer >= 4 && sGaroAttack.stateTimer <= 14) {
                GaroAttack_EnableSwingQuad(player, play);
                player->meleeWeaponQuads[0].info.toucher.damage = GARO_BANISH_DAMAGE;
            } else {
                player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
            }
            s32 done = GaroAttack_AdvanceFormAnim(play, player);
            sGaroAttack.stateTimer++;
            if (done) {
                player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
                sGaroAttack.banishTarget = NULL;
                GaroForm_ResetToIdle(player);
            }
            break;
        }

        case GARO_LOOK_AROUND: {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->actor.velocity.x = 0;
            player->actor.velocity.z = 0;
            player->linearVelocity = 0;
            s32 done = GaroAttack_AdvanceFormAnim(play, player);
            sGaroAttack.stateTimer++;
            if (done) {
                GaroForm_ResetToIdle(player);
            }
            break;
        }

        // ────────────────────────────────────────────────────────────────────
        // v9 GARO_LAUGH_TAUNT — non-pausing post-kill taunt anim. Link's
        // actionFunc is intentionally NOT suppressed so the player can
        // immediately interrupt by walking / attacking. The form skel anime
        // drives the laugh pose blended over Link's lower-body motion via
        // memcpy in GaroAttack_AdvanceFormAnim.
        // ────────────────────────────────────────────────────────────────────
        case GARO_LAUGH_TAUNT: {
            s32 done = GaroAttack_AdvanceFormAnim(play, player);
            sGaroAttack.stateTimer++;
            if (done || bPress || aPress || rPress) {
                GaroForm_ResetToIdle(player);
            }
            break;
        }

        // ────────────────────────────────────────────────────────────────────
        // v10 GARO_SHADOW_BALL — Z+A stationary self-cast invuln slash.
        // Garo is invisible (DISABLE_DRAW) + invincible (sustained
        // invincibilityTimer) for the entire anim. At elapsed frame 20
        // (source frame 30 at 1.5x playSpeed), un-hide briefly and stamp
        // the master-sword damage quad with damage 6. The quad stays live
        // for 4 frames; after that, anim continues to its natural end and
        // we restore visibility + reset to idle.
        // ────────────────────────────────────────────────────────────────────
        // v10.1: SHADOW_BALL is the final phase of the chain (entered
        // from BANISH_SHADOW after particle travel + teleport). Garo is
        // VISIBLE here — appearDrawSwords IS the appear anim, so hiding
        // it defeats the point. Sustain invincibility for the duration
        // so the strike can't be interrupted, fire the master-sword
        // damage 6 quad at elapsed frame 20, exit on anim end.
        case GARO_SHADOW_BALL: {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->actor.velocity.x = player->actor.velocity.z = 0;
            player->linearVelocity = 0;
            if (player->invincibilityTimer < 5) player->invincibilityTimer = 5;
            // Make sure the skin is visible — the BANISH chain set
            // DISABLE_DRAW during travel and clears it on transition,
            // but defensively clear again here in case anything else
            // touched the flag.
            player->stateFlags2 &= ~PLAYER_STATE2_DISABLE_DRAW;

            if (sGaroAttack.stateTimer == GARO_SHADOW_BALL_HIT_F &&
                !sGaroAttack.shadowBallSlashFired) {
                GaroAttack_EnableSwingQuad(player, play);
                player->meleeWeaponQuads[0].info.toucher.damage = GARO_SHADOW_BALL_DAMAGE;
                sGaroAttack.shadowBallSlashFired = 1;
                Audio_PlayActorSound2(&player->actor, NA_SE_IT_SWORD_SWING_HARD);
            } else if (sGaroAttack.shadowBallSlashFired &&
                       sGaroAttack.stateTimer > GARO_SHADOW_BALL_HIT_F + 4) {
                player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
            }

            s32 done = GaroAttack_AdvanceFormAnim(play, player);
            sGaroAttack.stateTimer++;
            if (done) {
                player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
                sGaroAttack.shadowBallInvulnActive = 0;
                GaroForm_ResetToIdle(player);
            }
            break;
        }

        // ────────────────────────────────────────────────────────────────────
        // v10 GARO_SIDEHOP_L / _R — Z+A stick-side. Vanilla-distance hop
        // with garo_bounce anim. No damage. Gravity decays velocity.y; we
        // wait for ground contact + a minimum airtime before re-idling.
        // ────────────────────────────────────────────────────────────────────
        case GARO_SIDEHOP_L:
        case GARO_SIDEHOP_R: {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            // No damage quad during sidehop.
            player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
            (void)GaroAttack_AdvanceFormAnim(play, player);
            sGaroAttack.stateTimer++;
            sGaroAttack.hopAirTimer++;
            if ((player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) &&
                sGaroAttack.hopAirTimer >= 3) {
                GaroForm_ResetToIdle(player);
            }
            break;
        }

        // ────────────────────────────────────────────────────────────────────
        // v10 GARO_BACKFLIP — Z+A stick-back. 2x distance (linearVelocity
        // 12.0). Anim: garo_jumpBack. No damage.
        // ────────────────────────────────────────────────────────────────────
        case GARO_BACKFLIP: {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
            (void)GaroAttack_AdvanceFormAnim(play, player);
            sGaroAttack.stateTimer++;
            sGaroAttack.hopAirTimer++;
            if ((player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) &&
                sGaroAttack.hopAirTimer >= 3) {
                GaroForm_ResetToIdle(player);
            }
            break;
        }

        // ────────────────────────────────────────────────────────────────────
        // v10 GARO_JUMP_ATTACK — Z+A forward + speed>0. 2x distance leap,
        // anim garo_appear @ 1.5x, damage 4 from elapsed frame 4 to land.
        // Garo's facing is locked at entry so the leap is straight forward.
        // ────────────────────────────────────────────────────────────────────
        // v10.1: JUMP_ATTACK is now just the LAUNCH phase of a parabolic
        // forward leap — no damage during the rise. After ~6 frames of
        // airtime (roughly past apex of the 8-frame jump arc), we
        // auto-transition to AIR_SLASH which loops garo_slashLoop until
        // landing → LAND_STRIKE (where the big AOE quad finally fires).
        // So Z+A+forward chains: jump → mid-jump auto-slash → big strike.
        case GARO_JUMP_ATTACK: {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;  // no dmg on the leap
            (void)GaroAttack_AdvanceFormAnim(play, player);
            sGaroAttack.stateTimer++;
            sGaroAttack.hopAirTimer++;
            // Mid-jump auto-transition to AIR_SLASH — same flow as if the
            // user had manually pressed B in mid-air. The big landing
            // strike then fires automatically on touchdown.
            if (sGaroAttack.hopAirTimer >= 6) {
                LinkAnimationHeader* loop = GaroForm_LoadAnim(GARO_SLASHLOOP_PATH);
                if (loop != NULL) {
                    GaroAttack_StartFormAnim(play, loop, 0.0f, -1.0f, 1.0f);
                }
                sGaroAttack.state = GARO_AIR_SLASH;
                sGaroAttack.stateTimer = 0;
                sGaroAttack.airSlashActive = 1;
                sGaroAttack.landStrikeFired = 0;
                if (!sGaroAttack.trailActive) GaroAttack_SpawnTrail(play);
                Audio_PlayActorSound2(&player->actor, NA_SE_IT_SWORD_SWING);
                break;
            }
            // Safety: if for some reason we touch ground before mid-jump
            // (short-hop, terrain edge), skip straight to LAND_STRIKE.
            if ((player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) &&
                sGaroAttack.hopAirTimer >= 3) {
                LinkAnimationHeader* land = GaroForm_LoadAnim(GARO_DRAWSWORDS_PATH);
                if (land != NULL) {
                    GaroAttack_StartFormAnim(play, land, 0.0f, -1.0f, 1.0f);
                }
                sGaroAttack.state = GARO_LAND_STRIKE;
                sGaroAttack.stateTimer = 0;
                sGaroAttack.airSlashActive = 0;
                sGaroAttack.landStrikeFired = 0;
            }
            break;
        }

        // ────────────────────────────────────────────────────────────────────
        // v10 GARO_AIR_SLASH — B in mid-air. Plays garo_slashLoop in a loop
        // (no damage, purely cosmetic — the damage lives in LAND_STRIKE).
        // On ground contact, transitions to LAND_STRIKE for the heavy hit.
        // ────────────────────────────────────────────────────────────────────
        case GARO_AIR_SLASH: {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            // No damage during the air segment.
            player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;

            s32 done = GaroAttack_AdvanceFormAnim(play, player);
            if (done) {
                // Loop the slash anim while still airborne.
                LinkAnimationHeader* loop = GaroForm_LoadAnim(GARO_SLASHLOOP_PATH);
                if (loop != NULL) {
                    GaroAttack_StartFormAnim(play, loop, 0.0f, -1.0f, 1.0f);
                }
            }
            sGaroAttack.stateTimer++;

            // Land → LAND_STRIKE. Same airtime gate as JUMP_ATTACK to
            // avoid stale ground flag firing on entry.
            if ((player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) &&
                sGaroAttack.stateTimer >= 2) {
                LinkAnimationHeader* land = GaroForm_LoadAnim(GARO_DRAWSWORDS_PATH);
                if (land != NULL) {
                    GaroAttack_StartFormAnim(play, land, 0.0f, -1.0f, 1.0f);
                }
                sGaroAttack.state = GARO_LAND_STRIKE;
                sGaroAttack.stateTimer = 0;
                sGaroAttack.airSlashActive = 0;
                sGaroAttack.landStrikeFired = 0;
                Audio_PlayActorSound2(&player->actor, NA_SE_PL_ROLL_DUST);
            }
            break;
        }

        // ────────────────────────────────────────────────────────────────────
        // v10 GARO_LAND_STRIKE — Garo drives garo_drawSwords on landing,
        // with a damage-8 master-sword quad live from elapsed frame 12 to
        // anim end + 3 tail frames. Forward motion is killed so the strike
        // is a planted hit.
        // ────────────────────────────────────────────────────────────────────
        case GARO_LAND_STRIKE: {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->actor.velocity.x = player->actor.velocity.z = 0;
            player->linearVelocity = 0;

            if (sGaroAttack.stateTimer >= GARO_LAND_STRIKE_HIT_F) {
                // v10: dedicated big AOE quad (95u forward × 65u halfW)
                // instead of the swing quad — landing strike needs the
                // reach to clearly clip enemies in a planted-blow arc.
                GaroAttack_EnableLandStrikeQuad(player, play);
                if (!sGaroAttack.landStrikeFired) {
                    sGaroAttack.landStrikeFired = 1;
                    Audio_PlayActorSound2(&player->actor, NA_SE_IT_SWORD_SWING_HARD);
                }
            }
            s32 done = GaroAttack_AdvanceFormAnim(play, player);
            sGaroAttack.stateTimer++;
            // Tail-out: once the anim finishes, keep the quad live for
            // GARO_LAND_STRIKE_TAIL_F additional frames so a slightly-late
            // enemy still gets clipped by the planted-sword pose.
            if (done) {
                sGaroAttack.landStrikeTailFrames++;
                if (sGaroAttack.landStrikeTailFrames > GARO_LAND_STRIKE_TAIL_F) {
                    player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
                    sGaroAttack.landStrikeTailFrames = 0;
                    GaroForm_ResetToIdle(player);
                }
            }
            break;
        }

        default:
            // Unknown state → safety net.
            GaroForm_ResetToIdle(player);
            break;
    }
}

