/**  * pikachu_form.cpp — Pikachu Transformation (Keaton Mask) — SSBB Rewrite
 *
 * Full SSBB moveset with 322 Brawl animations (SSBBAnim T+R+S format).
 * CPU weighted skinning via SSBBSkin_Draw.
 * State machine maps OOT input → SSBB actions.
 *
 * Controls:
 *   A          = Attack combo chain (jab → utilt → usmash)
 *   A + stick  = Forward tilt
 *   flick + A  = Forward smash
 *   L          = Crouch; L+A = down tilt; L+flick+A = down smash; L in air = dair
 *   B still    = Thunder Jolt
 *   B + stick  = Skull Bash
 *   C-buttons  = Items mapped to specials (Boomerang=QuickAtk, Din's=Thunder, etc.)
 *   R          = Bubble shield; R+dir = roll dodge
 *   Roc's Feather = Jump (required)
 */

#include <string.h>
#include <math.h>

#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "mods/transformation_masks/transformation_masks.h"
#include "objects/gameplay_keep/gameplay_keep.h"

#include <libultraship/bridge.h>

// ── SSBB System includes (compiled as C, need extern "C" wrapper) ────────────
extern "C" {
#include "expansions/ssbb/ssbb_anim.h"
#include "expansions/ssbb/ssbb_character.h"
#include "expansions/ssbb/ssbb_skin.h"
#include "expansions/ssbb/ssbb_action_defs.h"
#include "expansions/ssbb/characters/pikachu_ssbb_tex.h"
#include "objects/object_gi_hammer/object_gi_hammer.h"
#include "objects/object_gi_bow/object_gi_bow.h"
#include "objects/object_fhg/object_fhg.h"
}

// Forward declaration for C-linked SSBB register function
// (defined in ssbb_global.c as non-static wrapper around the static inline in register.h)
extern "C" s32 pikachu_ssbb_Register_Extern(void);
extern "C" u8 TransformMasks_HandleFormItemUse(PlayState* play, Player* player, s32 item);
extern "C" u8 PikaItem_Gigantamax(PlayState* play, Player* player, s32 item);

// ── Pikachu Voice Samples ───────────────────────────────────────────────────
#include "expansions/ssbb/characters/pikachu_ssbb_voice.h"

// Simple PCM mixer state (one voice at a time)
static struct {
    const s16* data;
    u32 len;
    u32 pos;       // Current playback position (in 22050Hz samples)
    f32 fracPos;   // Fractional position for resampling
    u8 playing;
} sPikaSfxState;

static void PikaSfx_Play(PikaSfxId id) {
    if (id >= PIKA_SFX_COUNT) return;
    sPikaSfxState.data = sPikaSfxTable[id].data;
    sPikaSfxState.len = sPikaSfxTable[id].len;
    sPikaSfxState.pos = 0;
    sPikaSfxState.fracPos = 0.0f;
    sPikaSfxState.playing = 1;
}

// Called from code_800E4FE0.c audio hook — mixes into output buffer
// Resamples 22050Hz → 32000Hz (OOT output rate)
static u8 sPikaSfxGiant = 0; // Set by Update, read by MixInto (avoids forward ref to sPika)

// Global flag: when set, bosses should accept damage regardless of state
extern "C" u8 gPikaGigantamaxActive = 0;

extern "C" void PikaSfx_MixInto(s16* outBuf, u32 numSamples) {
    if (!sPikaSfxState.playing || !sPikaSfxState.data) return;
    // Slower pitch when Gigantamax (deeper voice like a big Pikachu)
    f32 step = sPikaSfxGiant ? (22050.0f / 32000.0f) * 0.65f : 22050.0f / 32000.0f;
    // Half intensity, scaled by master volume (gSfxDefaultFreqAndVolScale = 0-1)
    f32 masterVol = gAudioContext.soundMode < 4 ? 1.0f : 0.5f; // Approximate master vol
    f32 vol = (sPikaSfxGiant ? 0.45f : 0.35f) * masterVol;
    for (u32 i = 0; i < numSamples; i++) {
        u32 idx = (u32)sPikaSfxState.fracPos;
        if (idx >= sPikaSfxState.len) {
            sPikaSfxState.playing = 0;
            return;
        }
        s16 sample = sPikaSfxState.data[idx];
        s32 mixed = (s32)outBuf[i * 2] + (s32)(sample * vol);     // Left
        s32 mixedR = (s32)outBuf[i * 2 + 1] + (s32)(sample * vol); // Right
        outBuf[i * 2] = (mixed > 32767) ? 32767 : (mixed < -32768) ? -32768 : (s16)mixed;
        outBuf[i * 2 + 1] = (mixedR > 32767) ? 32767 : (mixedR < -32768) ? -32768 : (s16)mixedR;
        sPikaSfxState.fracPos += step;
    }
}

// ── Macros ──────────────────────────────────────────────────────────────────
#define PIKA_CVAR "gMods.Pikachu.FormEnabled"
#define PIKACHU_SCALE 0.014f // 0.4 * 0.035 (small Pikachu)
#define PIKACHU_WALK_MULT 1.6f
#define PIKACHU_RUN_MULT 1.6f

#define PIKA_COMBO_WINDOW 20      // Frames after jab where 2nd A → utilt (generous for fast anims)
#define PIKA_SMASH_FLICK_WINDOW 3 // Frames for stick flick + A = smash
#define PIKA_IDLE_TAUNT_TIMER 600 // 10 seconds at 60fps → random taunt

// ── State ───────────────────────────────────────────────────────────────────

typedef struct {
    // SSBB character instance (skeleton, skin, animation)
    SSBBCharacterInstance charInst;

    // Current action
    SSBBActionId currentAction;
    u16 actionFrame;
    u8 comboCount; // A press chain: 0=jab, 1=utilt, 2=usmash

    // AT collider (attack hitbox)
    ColliderCylinder atCyl;
    u8 colliderReady;

    // Shield bubble
    u8 shieldActive;
    f32 shieldScale;
    s32 shieldTimer;

    // Knockback
    u8 inDamage;

    // Idle taunt timer
    s32 idleTimer;

    // Auto-blink
    s32 blinkTimer;
    s32 blinkFrame;

    // Quick Attack state
    u8 qatkPhase; // 0=inactive, 1=dash1, 2=dash2
    u8 airQuickAtkUsed; // 1 = already used in air (reset on ground)
    s32 qatkTimer;

    // Skull Bash charge
    s32 chargeTimer;
    Vec3f qatkDir; // Dash direction

    // Grab state
    s32 grabHoldTimer; // Frames remaining in grab hold

    // Stun state (shield break = 300 frames per Brawl)
    s32 stunTimer;

    // Gigantamax state (Giant's Mask)
    u8 gigantamax;       // 0=normal, 1=gigantamax active
    f32 giantScale;      // Current scale multiplier (lerps to target)
    s32 giantMpDrain;    // MP drain timer
    s32 giantTextTimer;  // Textbox display timer (>0 = showing text)
    u8 giantTextType;    // 0=transform, 1=revert
    s32 giantCooldown;   // Debounce timer (prevents double-call toggle)

    // Smash input detection
    s32 stickFlickTimer; // Frames since stick went from <50% to >80%
    u8 stickWasNeutral;

    // Input buffer (allows A/B press to be consumed within 4 frames)
    s32 aBufferTimer;
    u8 aBufferStickFlick; // Was stick flick active when A was pressed?
    s32 bBufferTimer;

    // Previous frame grounded state (for landing detection)
    u8 wasAirborne;

    // Carry state (for heavy get → hold transition)
    u8 wasCarrying;

    // Bomb summon-throw state
    u8 bombPending; // 1 = playing HeavyGet, will spawn+throw bomb on transition

    // Hammer state (JumpB → EscapeAir chain)
    u8 hammerPending; // 1 = playing JumpB (windup), 2 = playing EscapeAir (slam)

    // Run timer (frames since speed > 4.0f) for smash vs dash differentiation
    u16 runTimer;

// Thunder Jolt projectiles (5 bouncing light orbs)
#define PIKA_JOLT_COUNT 5
    struct {
        Vec3f pos;
        Vec3f vel;
        s16 timer;       // 0 = inactive, counts down
        f32 bouncePhase; // for parabolic bounce on ground
        f32 groundY;     // floor Y for bounce reference
        ColliderCylinder col;
        u8 colInited;
    } jolts[5];
    u8 joltsActive;
    u8 thunderActive;       // 1 = Thunder (L+B) is active
    u8 grabPullActive;      // 1 = grab is pulling enemy (hookshot-style)

    // Forward smash charge state
    u8 smashCharging; // 1 = in AttackS4Hold, charging
    s32 smashCharge;  // frames charged (0-60)

    // Jump limits (Roc's Feather = ground, Roc's Cape = air)
    u8 hasGroundJumped; // 1 = already used ground jump, reset on landing
    u8 hasAirJumped;    // 1 = already used air jump, reset on landing

    u8 initialized;
} PikachuSSBBState;

static PikachuSSBBState sPika;
static s32 sPikaDefIndex = -1;
static u8 sPikaRegistered = 0;

// AT ColliderCylinder init
static ColliderCylinderInit sAtCylInit = {
    { COLTYPE_HIT8, AT_ON | AT_TYPE_PLAYER, AC_NONE, OC1_ON | OC1_TYPE_ALL, OC2_TYPE_1, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK0,
      { 0xFFCFFFFF, 0x04, 0x08 }, // toucher: all flags, 4 damage, effect=ELECTRIC (0x08)
      { 0x00000000, 0x00, 0x00 }, // bumper: unused (AT only)
      TOUCH_ON | TOUCH_SFX_NONE,
      BUMP_NONE,
      OCELEM_ON },
    { 20, 30, 0, { 0, 0, 0 } },
};

// ── Helpers ─────────────────────────────────────────────────────────────────

static void Pika_SetAction(SSBBActionId action) {
    const SSBBActionDef* def = SSBBAction_Get(action);
    if (!def)
        return;

    const struct SSBBAnim* anim = SSBBAction_GetAnim(action);
    if (!anim)
        return;

    sPika.currentAction = action;
    sPika.actionFrame = 0;
    sPika.charInst.ssbbAnim = anim;
    sPika.charInst.curFrame = 0.0f;
    sPika.charInst.animLength = (f32)anim->numFrames;

    // Attacks play at 3x speed for snappy feel (but slower when Gigantamax)
    if (def->flags & (SSBB_ACT_FLAG_ATTACK | SSBB_ACT_FLAG_LOCKED)) {
        sPika.charInst.playSpeed = sPika.gigantamax ? 1.5f : 3.0f;
    } else {
        sPika.charInst.playSpeed = sPika.gigantamax ? 0.7f : 1.0f;
    }

    // Voice SFX per action type
    switch (action) {
        case SSBB_ACT_ATTACK_JAB: case SSBB_ACT_ATTACK_FTILT: case SSBB_ACT_ATTACK_FTILT_HI:
        case SSBB_ACT_ATTACK_FTILT_LW: case SSBB_ACT_ATTACK_DTILT: case SSBB_ACT_ATTACK_UTILT:
        case SSBB_ACT_ATTACK_DASH: case SSBB_ACT_ATTACK_NAIR: case SSBB_ACT_ATTACK_FAIR:
        case SSBB_ACT_ATTACK_BAIR: case SSBB_ACT_ATTACK_UAIR: case SSBB_ACT_ATTACK_DAIR:
            PikaSfx_Play(PIKA_SFX_ATTACK);
            break;
        case SSBB_ACT_ATTACK_FSMASH: case SSBB_ACT_ATTACK_USMASH: case SSBB_ACT_ATTACK_DSMASH:
            PikaSfx_Play(PIKA_SFX_SMASH);
            break;
        case SSBB_ACT_SPECIAL_N: case SSBB_ACT_SPECIAL_N_AIR:
            PikaSfx_Play(PIKA_SFX_SPECIAL);
            break;
        case SSBB_ACT_SPECIAL_HI_START: case SSBB_ACT_SPECIAL_HI_AIR_START:
            PikaSfx_Play(PIKA_SFX_QUICK_ATTACK);
            break;
        case SSBB_ACT_SPECIAL_LW_START: case SSBB_ACT_SPECIAL_LW_AIR_START:
            PikaSfx_Play(PIKA_SFX_THUNDER);
            break;
        case SSBB_ACT_SWING1: case SSBB_ACT_SWING4:
        case SSBB_ACT_JUMP_B: // Hammer windup uses JumpB anim
            PikaSfx_Play(PIKA_SFX_HAMMER);
            break;
        case SSBB_ACT_DAMAGE_N1: case SSBB_ACT_DAMAGE_N2: case SSBB_ACT_DAMAGE_N3:
        case SSBB_ACT_DAMAGE_HI1: case SSBB_ACT_DAMAGE_HI2: case SSBB_ACT_DAMAGE_HI3:
        case SSBB_ACT_DAMAGE_LW1: case SSBB_ACT_DAMAGE_LW2: case SSBB_ACT_DAMAGE_LW3:
        case SSBB_ACT_DAMAGE_AIR1: case SSBB_ACT_DAMAGE_AIR2: case SSBB_ACT_DAMAGE_AIR3:
        case SSBB_ACT_DAMAGE_FLY_N: case SSBB_ACT_DAMAGE_FLY_HI: case SSBB_ACT_DAMAGE_FLY_LW:
        case SSBB_ACT_DAMAGE_ELEC: case SSBB_ACT_DAMAGE_FALL:
            PikaSfx_Play(PIKA_SFX_DAMAGE);
            break;
        default:
            break;
    }
}

static u8 Pika_ActionFinished(void) {
    const SSBBActionDef* def = SSBBAction_Get(sPika.currentAction);
    if (!def)
        return 1;
    if (def->flags & SSBB_ACT_FLAG_LOOP)
        return 0;
    // Scale actionFrame threshold by playSpeed so faster anims finish sooner
    f32 spd = sPika.charInst.playSpeed;
    if (spd < 1.0f)
        spd = 1.0f;
    s32 threshold = (s32)((f32)sPika.charInst.ssbbAnim->numFrames / spd);
    return (sPika.actionFrame >= threshold);
}

static u8 Pika_CanCancel(void) {
    const SSBBActionDef* def = SSBBAction_Get(sPika.currentAction);
    if (!def)
        return 1;
    if (def->cancelFrame == 0)
        return Pika_ActionFinished();
    f32 spd = sPika.charInst.playSpeed;
    if (spd < 1.0f)
        spd = 1.0f;
    s32 threshold = (s32)((f32)def->cancelFrame / spd);
    return (sPika.actionFrame >= threshold);
}

static u8 Pika_IsAttacking(void) {
    const SSBBActionDef* def = SSBBAction_Get(sPika.currentAction);
    return def && (def->flags & SSBB_ACT_FLAG_ATTACK);
}

static f32 Pika_StickMag(PlayState* play) {
    s8 x = play->state.input[0].cur.stick_x;
    s8 y = play->state.input[0].cur.stick_y;
    f32 mag = sqrtf((f32)(x * x + y * y));
    return (mag > 80.0f) ? 1.0f : mag / 80.0f;
}

// ── Public API (extern "C" interface for transformation_masks.c) ────────────

extern "C" u8 PikachuForm_IsEnabled(void) {
    return CVarGetInteger(PIKA_CVAR, 0) != 0;
}

extern "C" u8 PikachuForm_LoadSkeleton(PlayState* play) {
    memset(&sPika, 0, sizeof(sPika));

    // Register SSBB character if not done
    if (!sPikaRegistered) {
        // The pikachu_ssbb_register.h is already included via z_player.c includes
        sPikaDefIndex = pikachu_ssbb_Register_Extern();
        sPikaRegistered = 1;
    }

    if (sPikaDefIndex < 0)
        return 0;

    // Init SSBB character instance
    SSBBChar_Init(&sPika.charInst, sPikaDefIndex, play);

    // Set body material DL (loads Pikachu_main texture + combiner)
    if (sPika.charInst.def && sPika.charInst.def->skinMesh) {
        sPika.charInst.def->skinMesh->materialDL = pikachu_ssbb_mat_main;
    }

    // Set initial animation to Wait1
    Pika_SetAction(SSBB_ACT_WAIT1);

    // Init AT collider — MUST pass player actor as owner or enemies ignore it
    {
        Player* player = GET_PLAYER(play);
        Collider_InitCylinder(play, &sPika.atCyl);
        Collider_SetCylinder(play, &sPika.atCyl, &player->actor, &sAtCylInit);
        sPika.colliderReady = 1;
    }

    sPika.blinkTimer = 240;
    sPika.idleTimer = 0;
    sPika.giantScale = 1.0f;
    sPika.shieldScale = 1.0f;
    sPika.initialized = 1;

    return 1;
}

extern "C" void PikachuForm_Cleanup(void) {
    if (sPika.charInst.def && sPika.charInst.def->skinMesh) {
        sPika.charInst.def->skinMesh->vtxBuf[0] = NULL;
        sPika.charInst.def->skinMesh->vtxBuf[1] = NULL;
    }
    sPika.initialized = 0;
    sPika.colliderReady = 0;
}

// ── Update ──────────────────────────────────────────────────────────────────

extern "C" void PikachuForm_Update(Player* player, PlayState* play) {
    if (!sPika.initialized || !sPika.charInst.ssbbAnim)
        return;

    // ── Body parts positions (for collider size + Navi/camera) ──
    // Pikachu is small: ~25 units tall. Feet at ground, head at +25.
    for (s32 i = 0; i < PLAYER_BODYPART_MAX; i++) {
        player->bodyPartsPos[i].x = player->actor.world.pos.x;
        player->bodyPartsPos[i].y = player->actor.world.pos.y + 10.0f;
        player->bodyPartsPos[i].z = player->actor.world.pos.z;
    }
    player->bodyPartsPos[PLAYER_BODYPART_L_FOOT].y = player->actor.world.pos.y;
    player->bodyPartsPos[PLAYER_BODYPART_R_FOOT].y = player->actor.world.pos.y;
    player->bodyPartsPos[PLAYER_BODYPART_HEAD].y = player->actor.world.pos.y + 25.0f;
    player->actor.shape.feetPos[0] = player->actor.shape.feetPos[1] = player->actor.world.pos;
    // Pikachu's cylinder — smaller and lower to match his small body
    player->cylinder.dim.radius = 10;
    player->cylinder.dim.yShift = -15;

    // ── Read input ──
    u8 onGround = (player->actor.bgCheckFlags & 1) != 0;
    f32 speed = player->linearVelocity;
    u8 aPress = CHECK_BTN_ALL(play->state.input[0].press.button, BTN_A) != 0;
    u8 bPress = CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B) != 0;
    u8 rHold = CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_R) != 0;
    u8 rPress = CHECK_BTN_ALL(play->state.input[0].press.button, BTN_R) != 0;
    u8 lHold = CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_L) != 0;
    f32 stickMag = Pika_StickMag(play);

    // Block input during OOT blocking states
    u32 blockMask = PLAYER_STATE1_LOADING | PLAYER_STATE1_TALKING | PLAYER_STATE1_DEAD | PLAYER_STATE1_GETTING_ITEM |
                    PLAYER_STATE1_CARRYING_ACTOR | PLAYER_STATE1_CLIMBING_LEDGE | PLAYER_STATE1_HANGING_OFF_LEDGE |
                    PLAYER_STATE1_FIRST_PERSON | PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_IN_ITEM_CS |
                    PLAYER_STATE1_IN_CUTSCENE;
    if (player->stateFlags1 & blockMask) {
        aPress = bPress = rPress = 0;
    }

    // Reset air limits on ground (before dispatch so handlers see updated state)
    if (onGround) {
        sPika.airQuickAtkUsed = 0;
    }

    // ── Gigantamax cooldown tick ──
    if (sPika.giantCooldown > 0) sPika.giantCooldown--;
    sPikaSfxGiant = sPika.gigantamax;
    {
        const SSBBActionDef* gDef = SSBBAction_Get(sPika.currentAction);
        u8 isInAction = gDef && (gDef->flags & (SSBB_ACT_FLAG_ATTACK | SSBB_ACT_FLAG_LOCKED));
        gPikaGigantamaxActive = sPika.gigantamax && isInAction;
    }

    // Gigantamax: fully invincible, no damage, no knockback
    if (sPika.gigantamax) {
        player->invincibilityTimer = -2; // Negative = no visual blink
        player->actor.colChkInfo.damage = 0;
        player->stateFlags1 &= ~PLAYER_STATE1_DAMAGED;
    }
    // NEVER blink Pikachu — force unk_6AD=0 so draw always executes
    // (OOT sets unk_6AD=4 during invincibility frames for visual flicker)
    player->unk_6AD = 0;
    if (sPika.gigantamax && (play->gameplayFrames % 30 == 0))
        lusprintf(__FILE__, __LINE__, 2, "GIGA: active=%d isAtk=%d action=%d\n", (int)gPikaGigantamaxActive, (int)Pika_IsAttacking(), (int)sPika.currentAction);

    // ── Gigantamax state ──
    if (sPika.gigantamax) {
        Math_SmoothStepToF(&sPika.giantScale, 6.0f, 0.3f, 0.5f, 0.01f);
        // MP drain: 1 MP every 30 frames
        if (++sPika.giantMpDrain >= 30) {
            sPika.giantMpDrain = 0;
            if (gSaveContext.magic > 0) {
                gSaveContext.magic--;
            } else {
                sPika.gigantamax = 0;
                sPika.giantTextTimer = 90;
                sPika.giantTextType = 1;
            }
        }
    } else if (sPika.giantScale > 1.01f) {
        Math_SmoothStepToF(&sPika.giantScale, 1.0f, 0.3f, 0.5f, 0.01f);
    } else {
        sPika.giantScale = 1.0f;
    }
    if (sPika.giantTextTimer > 0) sPika.giantTextTimer--;

    // ── C-button item dispatch (bypass OOT's blocked Player_ProcessItemButtons) ──
    if (!Pika_IsAttacking()) {
        static const u16 cBtns[] = { BTN_CLEFT, BTN_CDOWN, BTN_CRIGHT };
        u16 pressed = play->state.input[0].press.button;
        for (s32 ci = 0; ci < 3; ci++) {
            if (CHECK_BTN_ALL(pressed, cBtns[ci])) {
                s32 cItem = gSaveContext.equips.buttonItems[ci + 1]; // [0]=B, [1]=CL, [2]=CD, [3]=CR
                if (cItem != ITEM_NONE && cItem < ITEM_NONE_FE) {
                    if (TransformMasks_HandleFormItemUse(play, player, cItem)) {
                        goto advance_anim;
                    }
                }
            }
        }
    }

    // ── Smash flick detection (works even while running) ──
    // Detect rapid stick change: if stick went from <50% to >80% in 3 frames = flick
    // OR if A+B are pressed simultaneously with stick held = smash/special intent
    if (stickMag < 0.5f) {
        sPika.stickWasNeutral = 1;
        sPika.stickFlickTimer = 0;
    } else if (sPika.stickWasNeutral && stickMag > 0.8f) {
        sPika.stickFlickTimer = PIKA_SMASH_FLICK_WINDOW;
        sPika.stickWasNeutral = 0;
    }
    if (sPika.stickFlickTimer > 0)
        sPika.stickFlickTimer--;

    // ── Auto-blink ──
    if (sPika.blinkFrame > 0) {
        sPika.blinkFrame++;
        if (sPika.blinkFrame > 6)
            sPika.blinkFrame = 0;
    } else {
        sPika.blinkTimer--;
        if (sPika.blinkTimer <= 0) {
            sPika.blinkFrame = 1;
            sPika.blinkTimer = 180 + (s32)(play->gameplayFrames % 300);
        }
    }

    // ── Run timer (for smash vs dash attack differentiation) ──
    if (speed > 4.0f && onGround) {
        if (sPika.runTimer < 0xFFFF)
            sPika.runTimer++;
    } else if (speed < 2.0f) {
        sPika.runTimer = 0;
    }

    // ── Slow fall (Pikachu is floaty) — NOT in water ──
    if (!onGround && !(player->stateFlags1 & PLAYER_STATE1_IN_WATER)) {
        player->actor.velocity.y += 0.5f;
        if (player->actor.velocity.y < -12.0f)
            player->actor.velocity.y = -12.0f;
    }

    // Pikachu speed boost: handled in z_player.c alongside Bunny Hood (1.5x speed target + maxSpeed).

    // ── Speed cap + wall collision check ──
    // Cap regular speed to prevent momentum accumulation
    if (fabsf(player->linearVelocity) > 12.0f && sPika.currentAction != SSBB_ACT_SPECIAL_S &&
        sPika.currentAction != SSBB_ACT_SPECIAL_HI_START && sPika.currentAction != SSBB_ACT_SPECIAL_HI_AIR_START) {
        player->linearVelocity = (player->linearVelocity > 0) ? 12.0f : -12.0f;
    }
    // Wall check: if moving fast and hit a wall, stop (prevents clipping through walls)
    if ((player->actor.bgCheckFlags & 0x08) && fabsf(player->linearVelocity) > 6.0f) {
        player->linearVelocity = 0;
        player->actor.velocity.x = 0;
        player->actor.velocity.z = 0;
        // If in Skull Bash or Quick Attack, end the dash
        if (sPika.currentAction == SSBB_ACT_SPECIAL_S) {
            Pika_SetAction(SSBB_ACT_SPECIAL_S_END);
        }
        if (sPika.currentAction == SSBB_ACT_SPECIAL_HI_START || sPika.currentAction == SSBB_ACT_SPECIAL_HI_AIR_START) {
            Pika_SetAction(onGround ? SSBB_ACT_LANDING_LIGHT : SSBB_ACT_FALL);
        }
    }

    // ── Shield blocks damage (absorbs hit, shrinks shield) ──
    if (sPika.shieldActive && (player->stateFlags1 & PLAYER_STATE1_DAMAGED)) {
        // Block the damage: clear damage flag, heal the quarter-heart OOT took
        player->stateFlags1 &= ~PLAYER_STATE1_DAMAGED;
        Health_ChangeBy(play, 4); // Restore the damage OOT already applied
        // Shrink shield based on hit (extra shrink on top of passive drain)
        sPika.shieldScale -= 0.08f;
        Pika_SetAction(SSBB_ACT_GUARD_DAMAGE);
        Audio_PlayActorSound2(&player->actor, NA_SE_IT_SHIELD_BOUND);
        goto advance_anim;
    }

    // ── Damage reaction (highest priority) ──
    if (player->stateFlags1 & PLAYER_STATE1_DAMAGED) {
        if (!sPika.inDamage) {
            sPika.inDamage = 1;
            // Pick damage anim based on whether airborne
            if (!onGround) {
                s32 variant = (s32)(play->gameplayFrames % 3);
                SSBBActionId airDmg[] = { SSBB_ACT_DAMAGE_AIR1, SSBB_ACT_DAMAGE_AIR2, SSBB_ACT_DAMAGE_AIR3 };
                Pika_SetAction(airDmg[variant]);
            } else {
                // Strong knockback → DamageFly, normal → DamageN/Hi/Lw
                s32 variant = (s32)(play->gameplayFrames % 3);
                SSBBActionId dmgActions[] = { SSBB_ACT_DAMAGE_N1, SSBB_ACT_DAMAGE_N2, SSBB_ACT_DAMAGE_N3 };
                Pika_SetAction(dmgActions[variant]);
            }
        }
        goto advance_anim;
    }
    sPika.inDamage = 0;

    // ── Swimming — HIGHEST PRIORITY after damage ──
    // Pikachu is small: lower his position 5 units so water detection keeps him submerged
    if (player->stateFlags1 & PLAYER_STATE1_IN_WATER) {
        player->actor.world.pos.y -= 2.2f;
        player->stateFlags3 &= ~PLAYER_STATE3_PAUSE_ACTION_FUNC;
        if (speed > 1.0f) {
            if (sPika.currentAction != SSBB_ACT_SWIM_F)
                Pika_SetAction(SSBB_ACT_SWIM_F);
        } else {
            if (sPika.currentAction != SSBB_ACT_SWIM)
                Pika_SetAction(SSBB_ACT_SWIM);
        }
        sPika.hasGroundJumped = 0;
        sPika.hasAirJumped = 0;
        sPika.bombPending = 0;
        sPika.hammerPending = 0;
        sPika.smashCharging = 0;
        goto advance_anim;
    }

    // ── FuraFura stun (shield break or Deku Nut) — ~5 seconds (300 frames) ──
    // Mashing buttons/stick reduces stun (like Smash Bros)
    if (sPika.currentAction == SSBB_ACT_FURA_FURA) {
        player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
        player->actor.velocity.x = 0;
        player->actor.velocity.z = 0;
        player->linearVelocity = 0;
        sPika.stunTimer--;
        // Mashing: any button press or stick movement reduces stun by 8 frames
        u16 buttons = play->state.input[0].press.button;
        if (buttons || stickMag > 0.5f) {
            sPika.stunTimer -= 8;
        }
        // Loop the dizzy anim
        if (Pika_ActionFinished() && sPika.stunTimer > 0) {
            Pika_SetAction(SSBB_ACT_FURA_FURA);
        }
        if (sPika.stunTimer <= 0) {
            Pika_SetAction(SSBB_ACT_FURA_FURA_END);
        }
        goto advance_anim;
    }
    if (sPika.currentAction == SSBB_ACT_FURA_FURA_END && Pika_ActionFinished()) {
        sPika.shieldScale = 1.0f;
        Pika_SetAction(SSBB_ACT_WAIT1);
    }

    // ── Shield (R button) — don't activate during grab/throw ──
    {
    u8 inGrab = (sPika.currentAction == SSBB_ACT_CATCH || sPika.currentAction == SSBB_ACT_CATCH_DASH ||
                 sPika.currentAction == SSBB_ACT_CATCH_WAIT || sPika.currentAction == SSBB_ACT_CATCH_ATTACK ||
                 sPika.currentAction == SSBB_ACT_THROW_B || sPika.currentAction == SSBB_ACT_THROW_F ||
                 sPika.currentAction == SSBB_ACT_THROW_HI || sPika.currentAction == SSBB_ACT_THROW_LW);
    if (rHold && onGround && !Pika_IsAttacking() && !inGrab) {
        if (sPika.currentAction != SSBB_ACT_GUARD && sPika.currentAction != SSBB_ACT_GUARD_ON) {
            Pika_SetAction(SSBB_ACT_GUARD_ON);
            sPika.shieldActive = 1;
        } else if (sPika.currentAction == SSBB_ACT_GUARD_ON && Pika_ActionFinished()) {
            Pika_SetAction(SSBB_ACT_GUARD);
        }
        // While shield active: act as Mirror Shield for Twinrova beam absorption
        if (sPika.shieldActive) {
            player->stateFlags1 |= PLAYER_STATE1_SHIELDING;
            player->currentShield = PLAYER_SHIELD_MIRROR;
        }
        // Roll dodge: R + stick
        if (rPress && stickMag > 0.5f) {
            s16 stickAngle = play->state.input[0].cur.stick_x > 0 ? 0x4000 : -0x4000;
            s16 facingDiff = stickAngle - player->actor.shape.rot.y;
            if (facingDiff > 0) {
                Pika_SetAction(SSBB_ACT_ESCAPE_F);
            } else {
                Pika_SetAction(SSBB_ACT_ESCAPE_B);
            }
        }
        player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
        goto advance_anim;
    }
    } // close inGrab scope
    if (sPika.shieldActive && !rHold) {
        sPika.shieldActive = 0;
        player->stateFlags1 &= ~PLAYER_STATE1_SHIELDING;
        player->currentShield = PLAYER_SHIELD_NONE;
        Pika_SetAction(SSBB_ACT_GUARD_OFF);
    }
    // Always clear shielding flag when shield not active
    if (!sPika.shieldActive) {
        player->stateFlags1 &= ~PLAYER_STATE1_SHIELDING;
    }

    // ── THUNDER: L+B normally, ANY B when Gigantamax ──
    if (bPress && (lHold || sPika.gigantamax) && (!Pika_IsAttacking() || Pika_CanCancel())) {
        Pika_SetAction(onGround ? SSBB_ACT_SPECIAL_LW_START : SSBB_ACT_SPECIAL_LW_AIR_START);
        sPika.thunderActive = 1;
        player->actor.shape.rot.y = player->actor.world.rot.y;
        player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
        goto advance_anim;
    }

    // ── B specials (with 4-frame input buffer) — blocked in Gigantamax (except Thunder L+B above) ──
    if (bPress && !lHold && !sPika.gigantamax) {
        sPika.bBufferTimer = 4;
    }
    if (sPika.bBufferTimer > 0 && !sPika.gigantamax) {
        sPika.bBufferTimer--;
        if (!Pika_IsAttacking() || Pika_CanCancel()) {
            sPika.bBufferTimer = 0;
            if (stickMag > 0.5f) {
                // B + stick moving = Skull Bash
                Pika_SetAction(onGround ? SSBB_ACT_SPECIAL_S_START : SSBB_ACT_SPECIAL_S_AIR_START);
            } else {
                // B still = Thunder Jolt — spawns at 1/3 of anim
                Pika_SetAction(onGround ? SSBB_ACT_SPECIAL_N : SSBB_ACT_SPECIAL_N_AIR);
                sPika.joltsActive = 2; // 2 = pending spawn (spawns at 1/3 of anim)
            }
            player->actor.shape.rot.y = player->actor.world.rot.y;
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            goto advance_anim;
        }
    }

    // ── Grab system (R+A = standing, R+B = dash) — hookshot pull + back throw ──
    if (sPika.currentAction == SSBB_ACT_CATCH || sPika.currentAction == SSBB_ACT_CATCH_DASH) {
        player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
        player->linearVelocity = 0;
        // CatchDash advances Pikachu forward during anim
        if (sPika.currentAction == SSBB_ACT_CATCH_DASH && sPika.actionFrame < 8) {
            f32 pyaw = (f32)player->actor.world.rot.y * (M_PI / 0x8000);
            player->actor.velocity.x = sinf(pyaw) * 6.0f;
            player->actor.velocity.z = cosf(pyaw) * 6.0f;
        }
        // On hit: pull enemy toward Pikachu, then auto back-throw
        if (sPika.atCyl.base.atFlags & AT_HIT) {
            sPika.atCyl.base.atFlags &= ~AT_HIT;
            Pika_SetAction(SSBB_ACT_THROW_B);
            sPika.grabPullActive = 0;
            goto advance_anim;
        }
        // If grab anim finishes without hitting, return to idle
        if (Pika_ActionFinished()) {
            Pika_SetAction(SSBB_ACT_WAIT1);
            sPika.grabPullActive = 0;
        }
        goto advance_anim;
    }
    // Back throw: play anim then return to idle
    if (sPika.currentAction == SSBB_ACT_THROW_B || sPika.currentAction == SSBB_ACT_THROW_F ||
        sPika.currentAction == SSBB_ACT_THROW_HI || sPika.currentAction == SSBB_ACT_THROW_LW) {
        player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
        player->linearVelocity = 0;
        if (Pika_ActionFinished()) {
            Pika_SetAction(SSBB_ACT_WAIT1);
        }
        goto advance_anim;
    }

    // ── Thunder (L+B) chain transitions — MUST be before attack/movement checks ──
    if (sPika.currentAction == SSBB_ACT_SPECIAL_LW_START && Pika_ActionFinished()) {
        Pika_SetAction(SSBB_ACT_SPECIAL_LW_LOOP);
        player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
        goto advance_anim;
    }
    if (sPika.currentAction == SSBB_ACT_SPECIAL_LW_AIR_START && Pika_ActionFinished()) {
        Pika_SetAction(SSBB_ACT_SPECIAL_LW_AIR_LOOP);
        player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
        goto advance_anim;
    }
    if ((sPika.currentAction == SSBB_ACT_SPECIAL_LW_LOOP && sPika.actionFrame > 60) ||
        (sPika.currentAction == SSBB_ACT_SPECIAL_LW_AIR_LOOP && sPika.actionFrame > 60)) {
        SSBBActionId endAct = (sPika.currentAction == SSBB_ACT_SPECIAL_LW_LOOP)
            ? SSBB_ACT_SPECIAL_LW_CHARGE_END : SSBB_ACT_SPECIAL_LW_AIR_CHARGE_END;
        Pika_SetAction(endAct);
        player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
        goto advance_anim;
    }
    if ((sPika.currentAction == SSBB_ACT_SPECIAL_LW_LOOP ||
         sPika.currentAction == SSBB_ACT_SPECIAL_LW_AIR_LOOP) && !Pika_ActionFinished()) {
        player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
        goto advance_anim;
    }
    if ((sPika.currentAction == SSBB_ACT_SPECIAL_LW_CHARGE_END ||
         sPika.currentAction == SSBB_ACT_SPECIAL_LW_AIR_CHARGE_END) && !Pika_ActionFinished()) {
        player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
        goto advance_anim;
    }
    if (sPika.currentAction == SSBB_ACT_SPECIAL_LW_CHARGE_END && Pika_ActionFinished()) {
        Pika_SetAction(SSBB_ACT_WAIT1);
        sPika.thunderActive = 0;
    }
    if (sPika.currentAction == SSBB_ACT_SPECIAL_LW_AIR_CHARGE_END && Pika_ActionFinished()) {
        Pika_SetAction(SSBB_ACT_FALL);
        sPika.thunderActive = 0;
    }

    // ── A attacks (with 4-frame input buffer) ──
    if (aPress) {
        sPika.aBufferTimer = 4;
        sPika.aBufferStickFlick = (sPika.stickFlickTimer > 0); // Remember if flick was active at press time
    }
    if (sPika.aBufferTimer > 0)
        sPika.aBufferTimer--;

    {
    u8 jabSelfCancel = ((sPika.currentAction == SSBB_ACT_ATTACK_JAB ||
                         sPika.currentAction == SSBB_ACT_ATTACK_UTILT ||
                         sPika.currentAction == SSBB_ACT_ATTACK_USMASH) && sPika.actionFrame >= 3);
    if (sPika.aBufferTimer > 0 && (Pika_CanCancel() || !Pika_IsAttacking() || jabSelfCancel)) {
        sPika.aBufferTimer = 0;
        u8 wasFlick = sPika.aBufferStickFlick;
        SSBBActionId newAction = SSBB_ACT_WAIT1;

        // Gigantamax: only jab combo allowed (no tilts, smashes, aerials)
        if (sPika.gigantamax) {
            newAction = SSBB_ACT_ATTACK_JAB;
            Pika_SetAction(newAction);
            goto advance_anim;
        }

        if (!onGround && sPika.comboCount == 0) {
            // Air attacks (only if NOT in jab combo chain)
            if (lHold) {
                newAction = SSBB_ACT_ATTACK_DAIR;
            } else if (stickMag > 0.3f) {
                s16 stickYaw = Math_Atan2S(play->state.input[0].cur.stick_x, play->state.input[0].cur.stick_y);
                s16 facingDiff = stickYaw - player->actor.shape.rot.y;
                if (abs(facingDiff) > 0x4000) {
                    newAction = SSBB_ACT_ATTACK_BAIR;
                } else {
                    newAction = SSBB_ACT_ATTACK_FAIR;
                }
            } else {
                newAction = SSBB_ACT_ATTACK_NAIR;
            }
        } else if ((wasFlick || sPika.stickFlickTimer > 0) && stickMag > 0.5f) {
            // Stick flick + A = Forward/Down smash
            if (lHold) {
                newAction = SSBB_ACT_ATTACK_DSMASH;
            } else {
                // Start smash charge (AttackS4Hold loops while A held)
                newAction = SSBB_ACT_ATTACK_FSMASH_HOLD;
                sPika.smashCharging = 1;
                sPika.smashCharge = 0;
            }
        } else if (speed > 4.0f && sPika.runTimer < 60) {
            // Running < 1 second + A = Forward Smash charge
            newAction = SSBB_ACT_ATTACK_FSMASH_HOLD;
            sPika.smashCharging = 1;
            sPika.smashCharge = 0;
        } else if (speed > 4.0f) {
            // Running 1+ second + A = Dash Attack
            newAction = SSBB_ACT_ATTACK_DASH;
        } else if (lHold) {
            newAction = SSBB_ACT_ATTACK_DTILT;
        } else if (stickMag > 0.3f) {
            // A + stick held = Forward tilt
            newAction = SSBB_ACT_ATTACK_FTILT;
        } else {
            // A combo chain: jab → utilt → usmash (3 different anims)
            if (sPika.comboCount == 0) {
                newAction = SSBB_ACT_ATTACK_JAB;
                sPika.comboCount = 1;
            } else if (sPika.comboCount == 1) {
                newAction = SSBB_ACT_ATTACK_UTILT;
                sPika.comboCount = 2;
            } else {
                newAction = SSBB_ACT_ATTACK_USMASH;
                sPika.comboCount = 0;
            }
        }

        Pika_SetAction(newAction);
        player->actor.shape.rot.y = player->actor.world.rot.y;
        player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
        goto advance_anim;
    }
    } // close jabSelfCancel scope

    // Reset combo if not in attack
    if (!Pika_IsAttacking() && sPika.comboCount > 0) {
        if (sPika.actionFrame > PIKA_COMBO_WINDOW)
            sPika.comboCount = 0;
    }

    // ── Bomb/Nut summon-throw chain (highest priority — runs before attack/locked checks) ──
    if (sPika.bombPending) {
        player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
        player->actor.velocity.x = 0;
        player->actor.velocity.z = 0;
        player->linearVelocity = 0;
        player->actor.speedXZ = 0;

        u8 isNut = (sPika.bombPending == 3);

        // Deku Nut: uses LIGHT_THROW_F (fast toss), spawns at 1/3 of animation
        if (isNut) {
            if (sPika.currentAction != SSBB_ACT_LIGHT_THROW_F) {
                Pika_SetAction(SSBB_ACT_LIGHT_THROW_F);
            }
            // Spawn nut at 1/3 of animation
            if (sPika.actionFrame == 3) {
                f32 pyaw = (f32)player->actor.world.rot.y * (M_PI / 0x8000);
                Vec3f spawnPos = { player->actor.world.pos.x + sinf(pyaw) * 25.0f, player->actor.world.pos.y + 30.0f,
                                   player->actor.world.pos.z + cosf(pyaw) * 25.0f };
                Actor* nut = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOM, spawnPos.x, spawnPos.y, spawnPos.z,
                                         0x4000, player->actor.shape.rot.y, 0, 0x0002); // params=2 for nut behavior
                if (nut) {
                    nut->world.rot.y = player->actor.shape.rot.y;
                    nut->speedXZ = 15.0f;
                    nut->velocity.y = 5.0f;
                    nut->gravity = -2.0f;
                }
                AMMO(ITEM_NUT) -= 1;
                if (AMMO(ITEM_NUT) < 0) AMMO(ITEM_NUT) = 0;
                sPika.bombPending = 0;
            }
            if (Pika_ActionFinished()) {
                Pika_SetAction(onGround ? SSBB_ACT_WAIT1 : SSBB_ACT_FALL);
            }
        } else {
            // Bomb/Bombchu: HEAVY_GET → spawn → HEAVY_THROW_HI
            if (sPika.currentAction == SSBB_ACT_HEAVY_GET && sPika.actionFrame >= 1) {
                f32 pyaw = (f32)player->actor.world.rot.y * (M_PI / 0x8000);
                Vec3f spawnPos = { player->actor.world.pos.x + sinf(pyaw) * 20.0f, player->actor.world.pos.y + 40.0f,
                                   player->actor.world.pos.z + cosf(pyaw) * 20.0f };

                s32 actorId = (sPika.bombPending == 2) ? ACTOR_EN_BOM_CHU : ACTOR_EN_BOM;
                s32 ammoItem = (sPika.bombPending == 2) ? ITEM_BOMBCHU : ITEM_BOMB;

                Actor* projectile = Actor_Spawn(&play->actorCtx, play, actorId, spawnPos.x, spawnPos.y, spawnPos.z, 0,
                                                player->actor.shape.rot.y, 0, 0);
                if (projectile) {
                    projectile->world.rot.y = player->actor.shape.rot.y;
                    projectile->speedXZ = 12.0f;
                    projectile->velocity.y = 8.0f;
                    projectile->gravity = -1.5f;
                }

                AMMO(ammoItem) -= 1;
                if (AMMO(ammoItem) < 0) AMMO(ammoItem) = 0;

                Pika_SetAction(SSBB_ACT_HEAVY_THROW_HI);
                sPika.bombPending = 0;
            }
            if (sPika.currentAction == SSBB_ACT_HEAVY_THROW_HI && Pika_ActionFinished()) {
                Pika_SetAction(onGround ? SSBB_ACT_WAIT1 : SSBB_ACT_FALL);
            }
        }
        goto advance_anim;
    }

    // ── Hammer chain: JumpB (windup) → EscapeAir (slam) with hammer collider ──
    if (sPika.hammerPending) {
        // Cancel hammer if action was interrupted (damage, item use, etc.)
        if (sPika.hammerPending == 1 && sPika.currentAction != SSBB_ACT_JUMP_B) {
            sPika.hammerPending = 0;
        } else if (sPika.hammerPending == 2 && sPika.currentAction != SSBB_ACT_ESCAPE_AIR) {
            sPika.hammerPending = 0;
        }
    }
    if (sPika.hammerPending) {
        player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
        player->actor.velocity.x = 0;
        player->actor.velocity.z = 0;
        player->linearVelocity = 0;
        player->actor.speedXZ = 0;
        sPika.charInst.playSpeed = 3.0f;

        if (sPika.hammerPending == 1 && sPika.currentAction == SSBB_ACT_JUMP_B && Pika_ActionFinished()) {
            // Windup done → slam down
            Pika_SetAction(SSBB_ACT_ESCAPE_AIR);
            sPika.hammerPending = 2;
        }

        // During JumpB (windup): sphere collider around Pikachu with hammer damage
        if (sPika.hammerPending == 1 && sPika.colliderReady) {
            sPika.atCyl.dim.radius = 35;
            sPika.atCyl.dim.height = 35;
            sPika.atCyl.info.toucher.dmgFlags = DMG_HAMMER_SWING | DMG_SLASH | DMG_EXPLOSIVE;
            sPika.atCyl.info.toucher.damage = 8;
            sPika.atCyl.info.toucher.effect = 0x00;
            sPika.atCyl.info.toucherFlags = TOUCH_ON | TOUCH_SFX_HARD;
            sPika.atCyl.base.atFlags = AT_ON | AT_TYPE_PLAYER;
            sPika.atCyl.base.actor = &player->actor;
            Collider_UpdateCylinder(&player->actor, &sPika.atCyl);
            sPika.atCyl.dim.pos.y += 10;
            CollisionCheck_SetAT(play, &play->colChkCtx, &sPika.atCyl.base);
        }

        // During EscapeAir (slam): ground hammer impact — activates rusty switches + pillars
        if (sPika.hammerPending == 2 && sPika.colliderReady) {
            f32 pyaw = (f32)player->actor.shape.rot.y * (M_PI / 0x8000);
            sPika.atCyl.dim.radius = 40;
            sPika.atCyl.dim.height = 20;
            sPika.atCyl.info.toucher.dmgFlags = DMG_HAMMER_SWING | DMG_EXPLOSIVE | DMG_SLASH;
            sPika.atCyl.info.toucher.damage = 12;
            sPika.atCyl.info.toucher.effect = 0x00;
            sPika.atCyl.info.toucherFlags = TOUCH_ON | TOUCH_SFX_HARD;
            sPika.atCyl.base.atFlags = AT_ON | AT_TYPE_PLAYER;
            sPika.atCyl.base.actor = &player->actor;
            Collider_UpdateCylinder(&player->actor, &sPika.atCyl);
            sPika.atCyl.dim.pos.x += (s16)(sinf(pyaw) * 5.0f);
            sPika.atCyl.dim.pos.z += (s16)(cosf(pyaw) * 5.0f);
            CollisionCheck_SetAT(play, &play->colChkCtx, &sPika.atCyl.base);

            // Trick OOT into thinking Link is doing a hammer swing animation
            // Required by Bg_Hidan_Dalm (pillars) and other actors that check this
            player->meleeWeaponAnimation = 22; // PLAYER_MWA_HAMMER_FORWARD

            // Quake on first frame of slam
            if (sPika.actionFrame == 1) {
                s32 quakeIdx = Quake_Add(GET_ACTIVE_CAM(play), 3);
                Quake_SetSpeed(quakeIdx, 28000);
                Quake_SetQuakeValues(quakeIdx, 5, 0, 0, 0);
                Quake_SetCountdown(quakeIdx, 12);
                Audio_PlayActorSound2(&player->actor, NA_SE_IT_HAMMER_HIT);
            }
        }

        if (sPika.hammerPending == 2 && sPika.currentAction == SSBB_ACT_ESCAPE_AIR && Pika_ActionFinished()) {
            sPika.hammerPending = 0;
            Pika_SetAction(onGround ? SSBB_ACT_WAIT1 : SSBB_ACT_FALL);
        }
        goto advance_anim;
    }

    // ── Forward Smash charge chain: AttackS4Hold (loop while A held) → AttackS4S (release) ──
    if (sPika.smashCharging) {
        player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
        player->actor.velocity.x = 0;
        player->actor.velocity.z = 0;
        player->linearVelocity = 0;
        player->actor.speedXZ = 0;
        sPika.charInst.playSpeed = 3.0f; // 3x speed for charge anim

        u8 aHeld = CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_A) != 0;

        if (aHeld && sPika.smashCharge < 60) {
            // Still charging — stay in hold anim (loops)
            sPika.smashCharge++;
        } else {
            // Released A or max charge → execute smash
            Pika_SetAction(SSBB_ACT_ATTACK_FSMASH);
            sPika.smashCharging = 0;
        }

        if (sPika.currentAction == SSBB_ACT_ATTACK_FSMASH && Pika_ActionFinished()) {
            Pika_SetAction(onGround ? SSBB_ACT_WAIT1 : SSBB_ACT_FALL);
        }
        goto advance_anim;
    }

    // ── Currently in attack or LOCKED action — wait for finish ──
    if (Pika_IsAttacking() && !Pika_ActionFinished()) {
        player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
        const SSBBActionDef* curDef = SSBBAction_Get(sPika.currentAction);

        if (sPika.currentAction == SSBB_ACT_ATTACK_DASH) {
            // Dash attack: keep momentum for 16 frames, then hard stop
            if (sPika.actionFrame > 16) {
                player->actor.velocity.x = 0;
                player->actor.velocity.z = 0;
                player->linearVelocity = 0;
                player->actor.speedXZ = 0;
            }
        } else if (curDef && !(curDef->flags & SSBB_ACT_FLAG_MOVEMENT)) {
            // Non-movement attacks: hard stop (zero every frame)
            player->actor.velocity.x = 0;
            player->actor.velocity.z = 0;
            player->linearVelocity = 0;
            player->actor.speedXZ = 0;
        }
        goto advance_anim;
    }
    {
        const SSBBActionDef* curDef = SSBBAction_Get(sPika.currentAction);
        u8 isQuickAtk = (sPika.currentAction == SSBB_ACT_SPECIAL_HI_START ||
                         sPika.currentAction == SSBB_ACT_SPECIAL_HI_AIR_START);
        u8 isSkullBash = (sPika.currentAction == SSBB_ACT_SPECIAL_S ||
                          sPika.currentAction == SSBB_ACT_SPECIAL_S_AIR_START);
        if (curDef && (curDef->flags & SSBB_ACT_FLAG_LOCKED) && !Pika_ActionFinished() &&
            !isQuickAtk && !isSkullBash) {
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            if (!(curDef->flags & SSBB_ACT_FLAG_MOVEMENT)) {
                player->actor.velocity.x = 0;
                player->actor.velocity.z = 0;
                player->linearVelocity = 0;
                player->actor.speedXZ = 0;
            }
            goto advance_anim;
        }
    }

    // Reset air quick attack when on ground
    if (onGround) sPika.airQuickAtkUsed = 0;

    // ── Quick Attack — forward dash or homing launch (like Spinner) ──
    if (sPika.currentAction == SSBB_ACT_SPECIAL_HI_START || sPika.currentAction == SSBB_ACT_SPECIAL_HI_AIR_START) {
        player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
        Actor* target = (player->focusActor && (player->stateFlags1 & PLAYER_STATE1_Z_TARGETING))
                        ? player->focusActor : NULL;
        // Block air usage if already used once (like Roc's Cape)
        if (!onGround && sPika.qatkPhase == 0 && sPika.airQuickAtkUsed) {
            Pika_SetAction(SSBB_ACT_FALL);
            goto advance_anim;
        }
        // Initialize on first frame
        if (sPika.qatkPhase == 0) {
            sPika.qatkPhase = 1;
            sPika.qatkTimer = target ? 30 : 12;
            if (!onGround) sPika.airQuickAtkUsed = 1;
            Audio_PlayActorSound2(&player->actor, NA_SE_IT_BOOMERANG_THROW);
        }
        if (sPika.qatkPhase >= 1 && sPika.qatkTimer > 0) {
            player->invincibilityTimer = 2;
            player->actor.velocity.y = 0.0f; // No vertical movement
            sPika.qatkTimer--;
            if (target && target->update) {
                // HOMING: move world.pos directly toward target (like Spinner)
                s16 angle = Math_Vec3f_Yaw(&player->actor.world.pos, &target->world.pos);
                f32 hSpeed = 40.0f;
                player->actor.world.pos.x += Math_SinS(angle) * hSpeed;
                player->actor.world.pos.z += Math_CosS(angle) * hSpeed;
                player->linearVelocity = 0.0f;
                player->actor.world.rot.y = player->actor.shape.rot.y = angle;
                f32 dist = Math_Vec3f_DistXYZ(&player->actor.world.pos, &target->world.pos);
                if (dist < 60.0f) sPika.qatkTimer = 0;
            } else {
                // NO TARGET: dash forward in facing direction
                s16 yaw = player->actor.world.rot.y;
                player->actor.world.pos.x += Math_SinS(yaw) * 40.0f;
                player->actor.world.pos.z += Math_CosS(yaw) * 40.0f;
                player->linearVelocity = 0.0f;
            }
            if (player->actor.bgCheckFlags & 0x08) sPika.qatkTimer = 0;

            // Boomerang / G-Max Volt Crash collider during dash
            s16 qRadius = sPika.gigantamax ? (s16)(40 * sPika.giantScale) : 40;
            s16 qHeight = sPika.gigantamax ? (s16)(40 * sPika.giantScale) : 40;
            sPika.atCyl.dim.radius = qRadius;
            sPika.atCyl.dim.height = qHeight;
            sPika.atCyl.info.toucher.dmgFlags = sPika.gigantamax
                ? (DMG_UNBLOCKABLE | DMG_SLASH_MASTER | DMG_BOOMERANG | DMG_ARROW_LIGHT)
                : (DMG_BOOMERANG | DMG_SLASH_MASTER | DMG_ARROW_LIGHT);
            sPika.atCyl.info.toucher.damage = sPika.gigantamax ? 16 : 8;
            sPika.atCyl.info.toucher.effect = 0x08;
            sPika.atCyl.info.toucherFlags = TOUCH_ON | TOUCH_SFX_HARD;
            sPika.atCyl.base.atFlags = AT_ON | AT_TYPE_PLAYER;
            sPika.atCyl.base.atFlags &= ~AT_HIT;
            sPika.atCyl.base.actor = &player->actor;
            Collider_UpdateCylinder(&player->actor, &sPika.atCyl);
            CollisionCheck_SetAT(play, &play->colChkCtx, &sPika.atCyl.base);
        }
        // End dash — grant extended invulnerability (×2 dash duration)
        if (sPika.qatkTimer <= 0 && sPika.qatkPhase >= 1) {
            sPika.qatkPhase = 0;
            player->linearVelocity = 0.0f;
            player->invincibilityTimer = 24; // ~0.4s post-dash immunity
            Pika_SetAction(onGround ? SSBB_ACT_WAIT1 : SSBB_ACT_FALL);
        }
        goto advance_anim;
    }

    // ── World Interaction Overrides (OOT state flags → Brawl anims) ──

    // Throwing item (bombs, nuts, etc.)
    // When holding an actor: A button = throw with Brawl animation + boosted velocity
    // Smash-style: items thrown forward with force
    if (player->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) {
        // A press while carrying = throw
        if (aPress && player->heldActor != NULL) {
            Actor* thrown = player->heldActor;

            // Determine throw type and direction
            u8 isSmashThrow = (sPika.stickFlickTimer > 0);
            f32 throwSpeed = isSmashThrow ? 16.0f : 10.0f;
            f32 throwUpward = isSmashThrow ? 6.0f : 4.0f;

            // Smash-style: throw in facing direction with force
            f32 yaw = (f32)player->actor.world.rot.y * (3.14159265f / 32768.0f);

            // Detach from player
            player->actor.child = NULL;
            player->heldActor = NULL;
            player->interactRangeActor = NULL;
            thrown->parent = NULL;
            player->stateFlags1 &= ~PLAYER_STATE1_CARRYING_ACTOR;

            // Apply throw velocity (forward + up)
            thrown->velocity.x = sinf(yaw) * throwSpeed;
            thrown->velocity.y = throwUpward;
            thrown->velocity.z = cosf(yaw) * throwSpeed;

            // Throw direction based on stick — always use HeavyThrow
            if (stickMag > 0.5f) {
                s16 stickYaw = Math_Atan2S(play->state.input[0].cur.stick_x, play->state.input[0].cur.stick_y);
                s16 facingDiff = stickYaw - player->actor.shape.rot.y;
                if (abs(facingDiff) > 0x6000) {
                    // Back throw
                    thrown->velocity.x = -sinf(yaw) * throwSpeed;
                    thrown->velocity.z = -cosf(yaw) * throwSpeed;
                    thrown->velocity.y = throwUpward + 2.0f;
                    Pika_SetAction(SSBB_ACT_HEAVY_THROW_B);
                } else if (play->state.input[0].cur.stick_y > 40) {
                    // Up throw
                    thrown->velocity.y = throwUpward + 6.0f;
                    thrown->velocity.x *= 0.3f;
                    thrown->velocity.z *= 0.3f;
                    Pika_SetAction(SSBB_ACT_HEAVY_THROW_HI);
                } else if (play->state.input[0].cur.stick_y < -40) {
                    // Down throw (slam)
                    thrown->velocity.y = -2.0f;
                    Pika_SetAction(SSBB_ACT_HEAVY_THROW_LW);
                } else {
                    // Forward throw
                    Pika_SetAction(SSBB_ACT_HEAVY_THROW_F);
                }
            } else {
                // Neutral = default to HeavyThrowHi (overhead toss)
                Pika_SetAction(SSBB_ACT_HEAVY_THROW_HI);
            }
            goto advance_anim;
        }

        // Use ItemSmall/HeavyWalk anims while carrying (already handled below)
    }

    // Also detect when OOT drops the held actor (fallback throw detection)
    // Detect via carrying flag going away while we were in a carry anim
    if (!(player->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) && player->heldActor == NULL &&
        (sPika.currentAction == SSBB_ACT_HEAVY_WALK1 || sPika.currentAction == SSBB_ACT_HEAVY_WALK2 ||
         (sPika.currentAction == SSBB_ACT_HEAVY_GET && sPika.charInst.playSpeed == 0.0f))) {
        Pika_SetAction(SSBB_ACT_HEAVY_THROW_HI);
        goto advance_anim;
    }

    // Carrying actor (lifting rocks, pots, bombs, etc.)
    if (player->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) {
        if (!sPika.wasCarrying) {
            // Just picked up — play HeavyGet lift animation
            Pika_SetAction(SSBB_ACT_HEAVY_GET);
            sPika.wasCarrying = 1;
        } else if (sPika.currentAction == SSBB_ACT_HEAVY_GET && Pika_ActionFinished()) {
            // Lift done → freeze on last frame (idle hold = last frame of HeavyGet)
            sPika.charInst.playSpeed = 0.0f; // Freeze animation
        } else if (sPika.currentAction == SSBB_ACT_HEAVY_GET && speed > 0.5f && sPika.charInst.playSpeed == 0.0f) {
            // Start walking while holding
            Pika_SetAction(SSBB_ACT_HEAVY_WALK1);
        } else if ((sPika.currentAction == SSBB_ACT_HEAVY_WALK1 || sPika.currentAction == SSBB_ACT_HEAVY_WALK2) &&
                   speed < 0.3f) {
            // Stop walking → freeze on last frame of HeavyGet again
            Pika_SetAction(SSBB_ACT_HEAVY_GET);
            sPika.charInst.curFrame = sPika.charInst.animLength - 1.0f;
            sPika.charInst.playSpeed = 0.0f;
        } else if (sPika.currentAction == SSBB_ACT_HEAVY_WALK1 && Pika_ActionFinished()) {
            Pika_SetAction(SSBB_ACT_HEAVY_WALK2);
        } else if (sPika.currentAction == SSBB_ACT_HEAVY_WALK2 && Pika_ActionFinished()) {
            Pika_SetAction(SSBB_ACT_HEAVY_WALK1);
        }
        goto advance_anim;
    } else {
        sPika.wasCarrying = 0;
    }

    // Getting item (chest open, pickup)
    if (player->stateFlags1 & PLAYER_STATE1_GETTING_ITEM) {
        if (sPika.currentAction != SSBB_ACT_LIGHT_GET)
            Pika_SetAction(SSBB_ACT_LIGHT_GET);
        goto advance_anim;
    }

    // Pushing block (heavy push)
    if (player->stateFlags2 & PLAYER_STATE2_MOVING_DYNAPOLY) {
        if (sPika.currentAction != SSBB_ACT_HEAVY_WALK1 && sPika.currentAction != SSBB_ACT_HEAVY_WALK2)
            Pika_SetAction(SSBB_ACT_HEAVY_WALK1);
        goto advance_anim;
    }

    // Talking to NPC
    if (player->stateFlags1 & PLAYER_STATE1_TALKING) {
        if (sPika.currentAction != SSBB_ACT_WAIT1)
            Pika_SetAction(SSBB_ACT_WAIT1);
        goto advance_anim;
    }

    // In cutscene (don't override OOT)
    if (player->stateFlags1 & (PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_IN_ITEM_CS)) {
        goto advance_anim;
    }

    // Loading/transition
    if (player->stateFlags1 & PLAYER_STATE1_LOADING) {
        goto advance_anim;
    }

    // First person (scope for elemental rods)
    if (player->stateFlags1 & PLAYER_STATE1_FIRST_PERSON) {
        goto advance_anim;
    }

    // (Swimming handled above — before all action checks)

    // Climbing ladder — OOT handles everything, we only set SSBB visual anims
    if (player->stateFlags1 & PLAYER_STATE1_CLIMBING_LADDER) {
        s8 stickY = play->state.input[0].cur.stick_y;
        if (stickY > 10) {
            if (sPika.currentAction != SSBB_ACT_LADDER_UP)
                Pika_SetAction(SSBB_ACT_LADDER_UP);
        } else if (stickY < -10) {
            if (sPika.currentAction != SSBB_ACT_LADDER_DOWN)
                Pika_SetAction(SSBB_ACT_LADDER_DOWN);
        } else {
            if (sPika.currentAction != SSBB_ACT_LADDER_WAIT)
                Pika_SetAction(SSBB_ACT_LADDER_WAIT);
        }
        goto advance_anim; // Skip movement selection so ladder anims aren't overwritten
    }

    // Hanging from ledge (Brawl ledge grab system)
    if (player->stateFlags1 & PLAYER_STATE1_HANGING_OFF_LEDGE) {
        if (sPika.currentAction != SSBB_ACT_CLIFF_WAIT && sPika.currentAction != SSBB_ACT_CLIFF_CATCH) {
            Pika_SetAction(SSBB_ACT_CLIFF_CATCH);
        }
        if (sPika.currentAction == SSBB_ACT_CLIFF_CATCH && Pika_ActionFinished()) {
            Pika_SetAction(SSBB_ACT_CLIFF_WAIT);
        }
        // Climb up: stick up or A
        if (sPika.currentAction == SSBB_ACT_CLIFF_WAIT) {
            if (stickMag > 0.5f)
                Pika_SetAction(SSBB_ACT_CLIFF_CLIMB_QUICK);
            if (aPress)
                Pika_SetAction(SSBB_ACT_CLIFF_ATTACK_QUICK);
            if (rPress)
                Pika_SetAction(SSBB_ACT_CLIFF_ESCAPE_QUICK);
        }
        goto advance_anim;
    }

    // Climbing ledge (pulling up)
    if (player->stateFlags1 & PLAYER_STATE1_CLIMBING_LEDGE) {
        if (sPika.currentAction != SSBB_ACT_CLIFF_CLIMB_QUICK)
            Pika_SetAction(SSBB_ACT_CLIFF_CLIMB_QUICK);
        goto advance_anim;
    }

    // Edge teeter (standing at edge)
    if (player->stateFlags2 & PLAYER_STATE2_NEAR_OCARINA_ACTOR) {
        // OOT uses this flag for various states; detect edge by checking floor
        // Simplified: use Ottotto when near edge
    }

    // Wall jump (Pikachu can wall jump in Brawl)
    if (!onGround && (player->actor.bgCheckFlags & 0x08) && sPika.currentAction == SSBB_ACT_FALL && aPress) {
        Pika_SetAction(SSBB_ACT_PASSIVE_WALL_JUMP);
        player->actor.velocity.y = 4.0f;
        // Reverse horizontal direction
        f32 yaw = (f32)player->actor.world.rot.y * (3.14159265f / 32768.0f);
        player->actor.velocity.x = -sinf(yaw) * 6.0f;
        player->actor.velocity.z = -cosf(yaw) * 6.0f;
        player->actor.world.rot.y += 0x8000; // Turn around
        goto advance_anim;
    }

    // Being grabbed/captured by enemy (Like-Like, etc.)
    if (player->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY) {
        if (sPika.currentAction != SSBB_ACT_SWALLOWED)
            Pika_SetAction(SSBB_ACT_SWALLOWED);
        goto advance_anim;
    }

    // Frozen/stunned by enemy
    if (player->stateFlags2 & PLAYER_STATE2_FROZEN) {
        if (sPika.currentAction != SSBB_ACT_DAMAGE_ELEC)
            Pika_SetAction(SSBB_ACT_DAMAGE_ELEC);
        goto advance_anim;
    }

    // ── Landing detection (was airborne, now grounded) ──
    if (onGround) {
        sPika.hasGroundJumped = 0;
        sPika.hasAirJumped = 0;
    }
    if (onGround && (sPika.currentAction == SSBB_ACT_FALL || sPika.currentAction == SSBB_ACT_FALL_F ||
                     sPika.currentAction == SSBB_ACT_FALL_B || sPika.currentAction == SSBB_ACT_FALL_AERIAL)) {
        Pika_SetAction(SSBB_ACT_LANDING_LIGHT);
    }
    if (onGround && sPika.currentAction == SSBB_ACT_FALL_SPECIAL) {
        Pika_SetAction(SSBB_ACT_LANDING_FALL_SPECIAL);
    }
    // Landing after aerial attacks
    if (onGround && (sPika.currentAction == SSBB_ACT_ATTACK_NAIR || sPika.currentAction == SSBB_ACT_ATTACK_FAIR ||
                     sPika.currentAction == SSBB_ACT_ATTACK_BAIR || sPika.currentAction == SSBB_ACT_ATTACK_UAIR ||
                     sPika.currentAction == SSBB_ACT_ATTACK_DAIR)) {
        SSBBActionId landAnims[] = { SSBB_ACT_LANDING_AIR_N, SSBB_ACT_LANDING_AIR_F, SSBB_ACT_LANDING_AIR_B,
                                     SSBB_ACT_LANDING_AIR_HI, SSBB_ACT_LANDING_AIR_LW };
        s32 idx = sPika.currentAction - SSBB_ACT_ATTACK_NAIR;
        if (idx >= 0 && idx < 5)
            Pika_SetAction(landAnims[idx]);
    }

    // Landing anim → idle transition
    if (onGround &&
        (sPika.currentAction >= SSBB_ACT_LANDING_LIGHT && sPika.currentAction <= SSBB_ACT_LANDING_FALL_SPECIAL) &&
        Pika_ActionFinished()) {
        Pika_SetAction(SSBB_ACT_WAIT1);
    }

    // ── Movement animation selection (no attack active) ──
    if (Pika_IsAttacking() && Pika_ActionFinished()) {
        // Attack ended, return to idle
        sPika.comboCount = 0;
    }

    // ── JumpSquat → JumpF/JumpB transition (not in water) ──
    if (!(player->stateFlags1 & PLAYER_STATE1_IN_WATER) && sPika.currentAction == SSBB_ACT_JUMP_SQUAT &&
        Pika_ActionFinished()) {
        if (stickMag > 0.3f) {
            s16 stickYaw = Math_Atan2S(play->state.input[0].cur.stick_x, play->state.input[0].cur.stick_y);
            s16 facingDiff = stickYaw - player->actor.shape.rot.y;
            Pika_SetAction(abs(facingDiff) > 0x4000 ? SSBB_ACT_JUMP_B : SSBB_ACT_JUMP_F);
        } else {
            Pika_SetAction(SSBB_ACT_JUMP_F);
        }
    }
    // ── JumpF/JumpB/JumpAerial → Fall when descending (not in water) ──
    if (!(player->stateFlags1 & PLAYER_STATE1_IN_WATER)) {
        if ((sPika.currentAction == SSBB_ACT_JUMP_F || sPika.currentAction == SSBB_ACT_JUMP_B) &&
            player->actor.velocity.y < 0.0f) {
            Pika_SetAction(SSBB_ACT_FALL);
        }
        if ((sPika.currentAction == SSBB_ACT_JUMP_AERIAL_F || sPika.currentAction == SSBB_ACT_JUMP_AERIAL_B) &&
            player->actor.velocity.y < 0.0f) {
            Pika_SetAction(SSBB_ACT_FALL_AERIAL);
        }
    }

    // In water: don't override swim anims with jump/fall
    if (player->stateFlags1 & PLAYER_STATE1_IN_WATER)
        goto advance_anim;

    if (!onGround) {
        // Airborne — only set fall if not already in a jump/fall/attack anim
        if (player->actor.velocity.y > 2.0f) {
            if (sPika.currentAction != SSBB_ACT_JUMP_F && sPika.currentAction != SSBB_ACT_JUMP_B &&
                sPika.currentAction != SSBB_ACT_JUMP_SQUAT && sPika.currentAction != SSBB_ACT_JUMP_AERIAL_F &&
                sPika.currentAction != SSBB_ACT_JUMP_AERIAL_B)
                Pika_SetAction(SSBB_ACT_JUMP_F);
        } else {
            if (sPika.currentAction != SSBB_ACT_FALL && sPika.currentAction != SSBB_ACT_FALL_AERIAL &&
                sPika.currentAction != SSBB_ACT_JUMP_AERIAL_F && sPika.currentAction != SSBB_ACT_JUMP_AERIAL_B)
                Pika_SetAction(SSBB_ACT_FALL);
        }
    } else if (lHold) {
        // Crouching — with crawl walk when stick is held
        if (speed > 0.3f) {
            // Crawl forward/backward (Pikachu can crawl in Brawl!)
            // Determine direction relative to facing
            s16 stickYaw = Math_Atan2S(play->state.input[0].cur.stick_x, play->state.input[0].cur.stick_y);
            s16 facingDiff = stickYaw - player->actor.shape.rot.y;
            if (abs(facingDiff) > 0x4000) {
                if (sPika.currentAction != SSBB_ACT_SQUAT_B)
                    Pika_SetAction(SSBB_ACT_SQUAT_B);
            } else {
                if (sPika.currentAction != SSBB_ACT_SQUAT_F)
                    Pika_SetAction(SSBB_ACT_SQUAT_F);
            }
        } else {
            // Crouch idle
            if (sPika.currentAction != SSBB_ACT_SQUAT_WAIT && sPika.currentAction != SSBB_ACT_SQUAT) {
                Pika_SetAction(SSBB_ACT_SQUAT);
            }
            if (sPika.currentAction == SSBB_ACT_SQUAT && Pika_ActionFinished())
                Pika_SetAction(SSBB_ACT_SQUAT_WAIT);
        }
    } else if (speed > 4.0f) {
        // Gigantamax: always WalkSlow (heavy stomping)
        if (sPika.gigantamax) {
            if (sPika.currentAction != SSBB_ACT_WALK_SLOW)
                Pika_SetAction(SSBB_ACT_WALK_SLOW);
        } else {
            if (sPika.currentAction != SSBB_ACT_RUN)
                Pika_SetAction(SSBB_ACT_RUN);
        }
    } else if (speed > 2.0f) {
        if (sPika.currentAction != SSBB_ACT_WALK_FAST)
            Pika_SetAction(SSBB_ACT_WALK_FAST);
    } else if (speed > 0.5f) {
        if (sPika.currentAction != SSBB_ACT_WALK_MIDDLE)
            Pika_SetAction(SSBB_ACT_WALK_MIDDLE);
    } else {
        // Idle:
        //   Z-targeting → Wait1 (combat stance, loops)
        //   Normal rest → cycle Wait2 ↔ Wait3 (stretch, look around)
        u8 isIdle = (sPika.currentAction == SSBB_ACT_WAIT1 || sPika.currentAction == SSBB_ACT_WAIT2 ||
                     sPika.currentAction == SSBB_ACT_WAIT3);
        u8 isTaunt = (sPika.currentAction >= SSBB_ACT_APPEAL_HI && sPika.currentAction <= SSBB_ACT_APPEAL_SR);
        u8 zTargeting = (player->stateFlags1 & PLAYER_STATE1_Z_TARGETING) != 0;

        if (!isIdle && !isTaunt) {
            // Just became idle
            Pika_SetAction(zTargeting ? SSBB_ACT_WAIT1 : SSBB_ACT_WAIT2);
            sPika.idleTimer = 0;
        }

        // Z-target: always Wait1 (combat stance)
        if (zTargeting) {
            if (sPika.currentAction != SSBB_ACT_WAIT1)
                Pika_SetAction(SSBB_ACT_WAIT1);
        } else {
            // Rest idle: cycle Wait2 ↔ Wait3
            if (sPika.currentAction == SSBB_ACT_WAIT1)
                Pika_SetAction(SSBB_ACT_WAIT2);
            if (isIdle && Pika_ActionFinished()) {
                if (sPika.currentAction == SSBB_ACT_WAIT2)
                    Pika_SetAction(SSBB_ACT_WAIT3);
                else
                    Pika_SetAction(SSBB_ACT_WAIT2);
            }
        }

        // Auto-taunt after 10s idle (only in rest, not Z-target)
        if (!zTargeting) {
            sPika.idleTimer++;
            if (sPika.idleTimer >= PIKA_IDLE_TAUNT_TIMER) {
                SSBBActionId taunts[] = { SSBB_ACT_APPEAL_HI, SSBB_ACT_APPEAL_LW, SSBB_ACT_APPEAL_SL,
                                          SSBB_ACT_APPEAL_SR };
                Pika_SetAction(taunts[play->gameplayFrames % 4]);
                sPika.idleTimer = 0;
            }
        }
        if (isTaunt && Pika_ActionFinished()) {
            Pika_SetAction(SSBB_ACT_WAIT2);
        }
    }

    // (Thunder Jolt spawn+update moved after advance_anim label)
    if (sPika.joltsActive == 1) {
        u8 anyActive = 0;
        Actor* target = player->actor.child; // Z-target lock-on actor
        u8 targetInAir = (target != NULL && !(target->bgCheckFlags & 1));

        for (s32 j = 0; j < PIKA_JOLT_COUNT; j++) {
            if (sPika.jolts[j].timer <= 0)
                continue;
            anyActive = 1;
            sPika.jolts[j].timer--;

            // Init collider on first use
            if (!sPika.jolts[j].colInited) {
                Collider_InitCylinder(play, &sPika.jolts[j].col);
                static ColliderCylinderInit sJoltColInit = {
                    { COLTYPE_NONE, AT_ON | AT_TYPE_PLAYER, AC_NONE, OC1_NONE, OC2_NONE, COLSHAPE_CYLINDER },
                    { ELEMTYPE_UNK2,
                      { DMG_ARROW_LIGHT | DMG_MAGIC_LIGHT | DMG_SLINGSHOT | DMG_SLASH_KOKIRI | DMG_SLASH_MASTER, 0x01,
                        4 },
                      { 0, 0, 0 },
                      TOUCH_ON | TOUCH_SFX_NORMAL,
                      BUMP_NONE,
                      OCELEM_NONE },
                    { 10, 20, 0, { 0, 0, 0 } }
                };
                Collider_SetCylinder(play, &sPika.jolts[j].col, &player->actor, &sJoltColInit);
                sPika.jolts[j].colInited = 1;
            }

            // Movement
            if (target && targetInAir) {
                // Air: homing toward target
                Vec3f diff = { target->world.pos.x - sPika.jolts[j].pos.x,
                               target->world.pos.y + 20.0f - sPika.jolts[j].pos.y,
                               target->world.pos.z - sPika.jolts[j].pos.z };
                f32 dist = sqrtf(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
                if (dist > 1.0f) {
                    f32 spd = 14.0f;
                    sPika.jolts[j].vel.x = diff.x / dist * spd;
                    sPika.jolts[j].vel.y = diff.y / dist * spd;
                    sPika.jolts[j].vel.z = diff.z / dist * spd;
                }
            } else {
                // Ground: parabolic bounce toward target (or forward)
                // Speed scales with distance — always reaches target
                f32 dx = 0, dz = 0, hDist = 0;
                if (target) {
                    dx = target->world.pos.x - sPika.jolts[j].pos.x;
                    dz = target->world.pos.z - sPika.jolts[j].pos.z;
                    hDist = sqrtf(dx * dx + dz * dz);
                }

                // Bounce phase: faster when close, so it always does ~3 bounces to reach target
                f32 phaseSpeed = 0.25f; // fast bounces
                sPika.jolts[j].bouncePhase += phaseSpeed;
                f32 bounceH = 30.0f;
                sPika.jolts[j].pos.y = sPika.jolts[j].groundY + fabsf(Math_SinF(sPika.jolts[j].bouncePhase)) * bounceH;

                // Horizontal: always steer toward target, speed = distance/frames_remaining
                if (target && hDist > 5.0f) {
                    // Move a fraction of remaining distance each frame (arrives in ~15 frames)
                    f32 hSpd = hDist * 0.08f;
                    if (hSpd < 4.0f)
                        hSpd = 4.0f;
                    if (hSpd > 18.0f)
                        hSpd = 18.0f;
                    sPika.jolts[j].vel.x = dx / hDist * hSpd;
                    sPika.jolts[j].vel.z = dz / hDist * hSpd;
                } else if (hDist <= 5.0f && target) {
                    // Close enough — slow down
                    sPika.jolts[j].vel.x *= 0.5f;
                    sPika.jolts[j].vel.z *= 0.5f;
                }
                // No target: keep initial velocity (forward)

                sPika.jolts[j].pos.x += sPika.jolts[j].vel.x;
                sPika.jolts[j].pos.z += sPika.jolts[j].vel.z;

                // Update ground reference
                CollisionPoly* jFloorPoly;
                s32 jFloorBgId;
                f32 floorY = BgCheck_EntityRaycastFloor4(&play->colCtx, &jFloorPoly, &jFloorBgId, &player->actor,
                                                         &sPika.jolts[j].pos);
                if (floorY > -30000.0f)
                    sPika.jolts[j].groundY = floorY;
                goto jolt_collider;
            }

            // Air homing: apply velocity directly
            sPika.jolts[j].pos.x += sPika.jolts[j].vel.x;
            sPika.jolts[j].pos.y += sPika.jolts[j].vel.y;
            sPika.jolts[j].pos.z += sPika.jolts[j].vel.z;

        jolt_collider:
            // Collider
            sPika.jolts[j].col.dim.pos.x = (s16)sPika.jolts[j].pos.x;
            sPika.jolts[j].col.dim.pos.y = (s16)sPika.jolts[j].pos.y;
            sPika.jolts[j].col.dim.pos.z = (s16)sPika.jolts[j].pos.z;
            CollisionCheck_SetAT(play, &play->colChkCtx, &sPika.jolts[j].col.base);

            // VFX: KiraKira sparkle trail
            if ((play->gameplayFrames % 3) == 0) {
                static Color_RGBA8 jPrim = { 255, 255, 200, 255 };
                static Color_RGBA8 jEnv = { 255, 255, 50, 255 };
                Vec3f jVel = { 0, 1.0f, 0 };
                Vec3f jAccel = { 0, -0.05f, 0 };
                EffectSsKiraKira_SpawnSmall(play, &sPika.jolts[j].pos, &jVel, &jAccel, &jPrim, &jEnv);
            }

            // Expire
            if (sPika.jolts[j].timer <= 0) {
                // Explosion VFX on expire
                static Color_RGBA8 ePrim = { 255, 255, 255, 255 };
                static Color_RGBA8 eEnv = { 255, 255, 100, 200 };
                Vec3f eZero = { 0, 0, 0 };
                EffectSsBlast_Spawn(play, &sPika.jolts[j].pos, &eZero, &eZero, &ePrim, &eEnv, 200, -12, 2, 8);
            }
        }
        if (!anyActive)
            sPika.joltsActive = 0;
    }

advance_anim:
    // ── Thunder Jolt: spawn at 1/3 of anim ──
    if (sPika.joltsActive == 2 &&
        (sPika.currentAction == SSBB_ACT_SPECIAL_N || sPika.currentAction == SSBB_ACT_SPECIAL_N_AIR)) {
        s32 spawnFrame = (s32)(sPika.charInst.ssbbAnim->numFrames / (3.0f * sPika.charInst.playSpeed));
        if (spawnFrame < 1)
            spawnFrame = 1;
        if (sPika.actionFrame >= spawnFrame) {
            f32 pyaw = (f32)player->actor.world.rot.y * (M_PI / 0x8000);
            f32 gY = player->actor.world.pos.y;
            u8 inAir = !(player->actor.bgCheckFlags & 1);
            s32 count = inAir ? 1 : PIKA_JOLT_COUNT;

            for (s32 j = 0; j < PIKA_JOLT_COUNT; j++) {
                if (j >= count) {
                    sPika.jolts[j].timer = 0;
                    continue;
                }
                f32 spread = (count > 1) ? ((f32)j / (f32)(count - 1) - 0.5f) * (60.0f * M_PI / 180.0f) : 0.0f;
                f32 yaw = pyaw + spread;
                sPika.jolts[j].pos.x = player->actor.world.pos.x + sinf(pyaw) * 15.0f;
                sPika.jolts[j].pos.y = gY + 20.0f;
                sPika.jolts[j].pos.z = player->actor.world.pos.z + cosf(pyaw) * 15.0f;
                sPika.jolts[j].vel.x = sinf(yaw) * 10.0f;
                sPika.jolts[j].vel.y = 0;
                sPika.jolts[j].vel.z = cosf(yaw) * 10.0f;
                sPika.jolts[j].timer = 90;
                sPika.jolts[j].bouncePhase = (f32)j * 0.8f;
                sPika.jolts[j].groundY = gY;
                sPika.jolts[j].colInited = 0;
            }
            sPika.joltsActive = 1;
        }
    }

    // ── Skull Bash: START → HOLD (charge) → READY → S (launch) → END ──
    // Charge: B held = charge grows. Release B or auto-release after ~60 frames.
    // Launch speed and damage scale with charge time.
    {
        SSBBActionId sbAct = sPika.currentAction;

        // START → HOLD (begin charging)
        if (sbAct == SSBB_ACT_SPECIAL_S_START && Pika_ActionFinished()) {
            Pika_SetAction(SSBB_ACT_SPECIAL_S_HOLD);
            sPika.chargeTimer = 0;
        }

        // HOLD: charge while B held. Scene darkens. Auto-release after 60 frames.
        if (sbAct == SSBB_ACT_SPECIAL_S_HOLD) {
            sPika.chargeTimer++;
            u8 bHold = CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_B) != 0;

            // Darken scene proportional to charge (like dark medallion)
            f32 chargePct = (f32)sPika.chargeTimer / 60.0f;
            if (chargePct > 1.0f)
                chargePct = 1.0f;
            Environment_AdjustLights(play, chargePct * 0.6f, 850.0f, 0.2f, 0.9f);

            // Electric charge buzz SFX
            func_8002F974(&player->actor, NA_SE_EN_BIRI_SPARK - SFX_FLAG);

            // Freeze position during charge
            player->linearVelocity = 0;
            player->actor.velocity.x = 0;
            player->actor.velocity.z = 0;

            // Release: B released or auto after 60 frames
            if (!bHold || sPika.chargeTimer >= 60) {
                Pika_SetAction(SSBB_ACT_SPECIAL_S_READY);
            }
        }

        // READY → S (launch!)
        if (sbAct == SSBB_ACT_SPECIAL_S_READY && Pika_ActionFinished()) {
            Pika_SetAction(SSBB_ACT_SPECIAL_S);
            // Restore lighting
            Environment_AdjustLights(play, 0.0f, 850.0f, 0.2f, 0.0f);
        }

        // S: LAUNCH — speed based on charge
        if (sbAct == SSBB_ACT_SPECIAL_S) {
            f32 yaw = (f32)player->actor.world.rot.y * (M_PI / 0x8000);
            f32 chargePct = (f32)sPika.chargeTimer / 60.0f;
            if (chargePct > 1.0f)
                chargePct = 1.0f;
            f32 dashSpeed = 8.0f + chargePct * 12.0f; // 8-20 speed based on charge
            player->actor.velocity.x = sinf(yaw) * dashSpeed;
            player->actor.velocity.z = cosf(yaw) * dashSpeed;
            player->linearVelocity = dashSpeed;
        }

        // S → END
        if (sbAct == SSBB_ACT_SPECIAL_S && Pika_ActionFinished())
            Pika_SetAction(SSBB_ACT_SPECIAL_S_END);

        // END → return to normal (NOT helpless, per Brawl)
        if (sbAct == SSBB_ACT_SPECIAL_S_END && Pika_ActionFinished())
            Pika_SetAction(onGround ? SSBB_ACT_WAIT1 : SSBB_ACT_FALL);
    }

    // ── Quick Attack physics — fast dash, Z-target homing ──
    if (sPika.currentAction == SSBB_ACT_SPECIAL_HI_START || sPika.currentAction == SSBB_ACT_SPECIAL_HI_AIR_START) {
        if (sPika.qatkPhase == 0) {
            sPika.qatkPhase = 1;
            sPika.qatkTimer = 10;
            // If Z-targeting, dash TOWARD the target
            Actor* target = player->focusActor;
            if (target != NULL) {
                f32 dx = target->world.pos.x - player->actor.world.pos.x;
                f32 dy = target->world.pos.y - player->actor.world.pos.y;
                f32 dz = target->world.pos.z - player->actor.world.pos.z;
                f32 dist = sqrtf(dx * dx + dy * dy + dz * dz);
                if (dist > 1.0f) {
                    sPika.qatkDir.x = dx / dist;
                    sPika.qatkDir.y = dy / dist;
                    sPika.qatkDir.z = dz / dist;
                } else {
                    f32 y = (f32)player->actor.world.rot.y * (3.14159265f / 32768.0f);
                    sPika.qatkDir.x = sinf(y);
                    sPika.qatkDir.z = cosf(y);
                    sPika.qatkDir.y = 0.0f;
                }
            } else {
                // No target: dash forward (slight upward for ground→air transition)
                f32 y = (f32)player->actor.world.rot.y * (3.14159265f / 32768.0f);
                sPika.qatkDir.x = sinf(y);
                sPika.qatkDir.z = cosf(y);
                sPika.qatkDir.y = 0.2f;
            }
            // Flash + SFX at launch
            Audio_PlayActorSound2(&player->actor, NA_SE_IT_BOOMERANG_THROW);
        }
        if (sPika.qatkPhase >= 1 && sPika.qatkTimer > 0) {
            f32 qSpeed = 22.0f;
            player->actor.velocity.x = sPika.qatkDir.x * qSpeed;
            player->actor.velocity.y = sPika.qatkDir.y * qSpeed;
            player->actor.velocity.z = sPika.qatkDir.z * qSpeed;
            player->linearVelocity = 0.0f; // Don't let OOT add more speed
            sPika.qatkTimer--;

            // Wall check — stop on collision
            if (player->actor.bgCheckFlags & 0x08) {
                sPika.qatkTimer = 0;
            }
        }
        if (sPika.qatkTimer <= 0 && sPika.qatkPhase >= 1) {
            sPika.qatkPhase = 0;
            player->actor.velocity.x = 0.0f;
            player->actor.velocity.y = 0.0f;
            player->actor.velocity.z = 0.0f;
            player->linearVelocity = 0.0f;
            Pika_SetAction(onGround ? SSBB_ACT_LANDING_LIGHT : SSBB_ACT_FALL);
        }

        // Quick Attack invincibility (frames 1-18 and 27-31 per Brawl)
        if (sPika.qatkPhase >= 1 && sPika.qatkTimer > 0) {
            player->invincibilityTimer = 2; // Re-apply each frame during dash
        }

        // Electric ring particles during dash
        if (sPika.qatkPhase >= 1) {
            static Color_RGBA8 qYellow = { 255, 220, 50, 255 };
            static Color_RGBA8 qWhite = { 255, 255, 200, 200 };
            Vec3f zero = { 0, 0, 0 };
            for (s32 qi = 0; qi < 4; qi++) {
                u16 qangle = (u16)(qi * 0x4000 + play->gameplayFrames * 0x1000);
                Vec3f ringPos;
                ringPos.x = player->actor.world.pos.x + Math_SinS((s16)qangle) * 15.0f;
                ringPos.y = player->actor.world.pos.y + 20.0f;
                ringPos.z = player->actor.world.pos.z + Math_CosS((s16)qangle) * 15.0f;
                EffectSsBlast_Spawn(play, &ringPos, &zero, &zero, &qYellow, &qWhite, 160, -9, 1, 6);
            }
        }
    }

    // Thunder invincibility + quake during LOOP phase (damage is active during Loop)
    // (Chain transitions moved to early section above A attacks)
    if (sPika.currentAction == SSBB_ACT_SPECIAL_LW_LOOP || sPika.currentAction == SSBB_ACT_SPECIAL_LW_AIR_LOOP) {
        // Invincibility frames 1-8
        if (sPika.actionFrame <= 8)
            player->invincibilityTimer = 2;
        // Big quake on frame 1
        if (sPika.actionFrame == 1) {
            s32 quakeIdx = Quake_Add(GET_ACTIVE_CAM(play), 3);
            Quake_SetSpeed(quakeIdx, 28000);
            Quake_SetQuakeValues(quakeIdx, 12, 0, 0, 0);
            Quake_SetCountdown(quakeIdx, 25);
            Audio_PlayActorSound2(&player->actor, NA_SE_EV_LIGHTNING);
        }
    }

    // ── Freeze movement during grounded attacks (not movement attacks) ──
    if (Pika_IsAttacking()) {
        const SSBBActionDef* curDef = SSBBAction_Get(sPika.currentAction);
        if (curDef && !(curDef->flags & SSBB_ACT_FLAG_MOVEMENT)) {
            player->actor.velocity.x = 0.0f;
            player->actor.velocity.z = 0.0f;
            player->linearVelocity = 0.0f;
            player->actor.speedXZ = 0.0f;
        }
    }

    // ── Thunder VFX (multi-phase discharge from last commit) ──
    if (sPika.currentAction == SSBB_ACT_SPECIAL_LW_LOOP || sPika.currentAction == SSBB_ACT_SPECIAL_LW_AIR_LOOP ||
        sPika.currentAction == SSBB_ACT_SPECIAL_LW_LOOP || sPika.currentAction == SSBB_ACT_SPECIAL_LW_AIR_LOOP) {
        static Color_RGBA8 tPrimYellow = { 255, 230, 50, 255 };
        static Color_RGBA8 tEnvWhite = { 255, 255, 200, 255 };
        static Color_RGBA8 tPrimBlue = { 100, 180, 255, 255 };
        static Color_RGBA8 tEnvBlue = { 30, 80, 255, 200 };
        static Color_RGBA8 tPrimWhite = { 255, 255, 255, 255 };
        static Color_RGBA8 tEnvYellow = { 255, 220, 80, 255 };
        Vec3f tBase = player->actor.world.pos;
        Vec3f tZero = { 0, 0, 0 };
        s32 fr = sPika.actionFrame;

        // Scene darkening
        f32 intensity = (fr < 8)     ? (f32)(fr + 1) / 8.0f * 0.85f
                        : (fr <= 30) ? 0.55f
                                     : 0.55f * (1.0f - (f32)(fr - 30) / 7.0f);
        if (intensity < 0.0f)
            intensity = 0.0f;
        Environment_AdjustLights(play, intensity, 850.0f, 0.2f, 0.9f);

        // Electric buzz SFX
        func_8002F974(&player->actor, NA_SE_EN_BIRI_SPARK - SFX_FLAG);

        // Growing electric aura (KiraKira sparkles + Lightning bolts)
        if ((fr % 2) == 0) {
            f32 t = (f32)fr / 19.0f;
            if (t > 1.0f)
                t = 1.0f;
            f32 radius = 3.0f + t * 87.0f;
            f32 rotOfs = (f32)fr * 0.4f;
            s32 ringCount = 3 + (s32)(t * 9.0f);
            if (ringCount > 12)
                ringCount = 12;

            for (s32 i = 0; i < ringCount; i++) {
                f32 angle = (f32)i * (6.28318f / (f32)ringCount) + rotOfs;
                Vec3f pos = { tBase.x + Math_SinF(angle) * radius, tBase.y + 15.0f,
                              tBase.z + Math_CosF(angle) * radius };
                Vec3f vel = { Math_SinF(angle) * 0.8f, 0.4f, Math_CosF(angle) * 0.8f };
                Vec3f accel = { Math_SinF(angle) * 0.1f, -0.08f, Math_CosF(angle) * 0.1f };
                Color_RGBA8* prim = (i % 3 == 0) ? &tPrimYellow : &tPrimBlue;
                Color_RGBA8* env = (i % 3 == 0) ? &tEnvWhite : &tEnvBlue;
                EffectSsKiraKira_SpawnFocused(play, &pos, &vel, &accel, prim, env, 520, 20);
            }

            // Inner aura sparkles
            if (fr >= 4) {
                f32 innerR = 3.0f + t * 39.0f;
                for (s32 i = 0; i < 8; i++) {
                    f32 angle = (f32)i * (6.28318f / 8.0f) - rotOfs;
                    Vec3f iPos = { tBase.x + Math_SinF(angle) * innerR,
                                   tBase.y + 20.0f + Math_SinF((f32)fr * 0.3f + (f32)i) * 15.0f,
                                   tBase.z + Math_CosF(angle) * innerR };
                    Vec3f vel = { Math_SinF(angle) * 0.4f, 1.5f, Math_CosF(angle) * 0.4f };
                    Vec3f accel = { 0, -0.1f, 0 };
                    EffectSsKiraKira_SpawnFocused(play, &iPos, &vel, &accel, &tPrimBlue, &tEnvWhite, 480, 18);
                }
            }

            // Lightning bolts radiating outward
            if (fr >= 6 && (fr % 4) == 0) {
                s32 boltCount = 2 + (s32)(t * 6.0f);
                if (boltCount > 8)
                    boltCount = 8;
                for (s32 i = 0; i < boltCount; i++) {
                    f32 angle = (f32)i * (6.28318f / (f32)boltCount) + (f32)fr * 0.55f;
                    Vec3f oPos = { tBase.x + Math_SinF(angle) * (radius * 0.6f), tBase.y + 10.0f + t * 20.0f,
                                   tBase.z + Math_CosF(angle) * (radius * 0.6f) };
                    s32 boltScale = (s32)(80.0f + t * 60.0f);
                    EffectSsLightning_Spawn(play, &oPos, &tPrimBlue, &tEnvBlue, boltScale, (s16)(i * 0x2000), 16, 3);
                }
            }
        }

        // Frame 8: BIG DISCHARGE BURST
        if (fr == 8) {
            for (s32 h = 0; h < 6; h++) {
                Vec3f beamPos = { tBase.x, tBase.y + 20.0f + (f32)h * 40.0f, tBase.z };
                EffectSsLightning_Spawn(play, &beamPos, &tPrimWhite, &tEnvYellow, 400, (s16)(h * 0x1555), 12, 6);
            }
            for (s32 i = 0; i < 10; i++) {
                f32 angle = (f32)i * (6.28318f / 10.0f);
                Vec3f skyPos = { tBase.x + Math_SinF(angle) * 70.0f, tBase.y + 180.0f,
                                 tBase.z + Math_CosF(angle) * 70.0f };
                EffectSsLightning_Spawn(play, &skyPos, &tPrimYellow, &tEnvWhite, 180, (s16)(i * 0x1999), 8, 3);
            }
            EffectSsBlast_SpawnWhiteShockwave(play, &tBase, &tZero, &tZero);
            s32 quakeIdx = Quake_Add(GET_ACTIVE_CAM(play), 3);
            Quake_SetSpeed(quakeIdx, 28000);
            Quake_SetQuakeValues(quakeIdx, 7, 0, 0, 0);
            Quake_SetCountdown(quakeIdx, 22);
        }

        // Sustained side bolts (frames 10-30)
        if (fr >= 10 && fr <= 30 && (fr % 4) == 0) {
            static const f32 cardAngles[4] = { 0.0f, 1.5708f, 3.14159f, 4.7124f };
            for (s32 i = 0; i < 4; i++) {
                Vec3f sidePos = { tBase.x + Math_SinF(cardAngles[i]) * 55.0f, tBase.y + 35.0f,
                                  tBase.z + Math_CosF(cardAngles[i]) * 55.0f };
                EffectSsLightning_Spawn(play, &sidePos, &tPrimYellow, &tEnvBlue, 100,
                                        (s16)(i * 0x4000 + (s16)(fr * 0x500)), 14, 2);
            }
        }

        // Restore lighting when thunder ends
        if (sPika.currentAction == SSBB_ACT_SPECIAL_LW_LOOP || sPika.currentAction == SSBB_ACT_SPECIAL_LW_AIR_LOOP) {
            if (Pika_ActionFinished()) {
                Environment_AdjustLights(play, 0.0f, 850.0f, 0.2f, 0.0f);
            }
        }
    }

    // ── Attack VFX (Brawl-accurate per-attack visual effects) ──
    // Check for ANY active action (not just A flag — specials have K flag)
    if (sPika.currentAction != SSBB_ACT_WAIT1 && sPika.currentAction != SSBB_ACT_WAIT2 &&
        sPika.currentAction != SSBB_ACT_WAIT3 && sPika.currentAction != SSBB_ACT_WALK_SLOW &&
        sPika.currentAction != SSBB_ACT_WALK_MIDDLE && sPika.currentAction != SSBB_ACT_WALK_FAST &&
        sPika.currentAction != SSBB_ACT_RUN && sPika.currentAction != SSBB_ACT_FALL &&
        sPika.currentAction != SSBB_ACT_SQUAT_WAIT) {
        SSBBActionId vfxAct = sPika.currentAction;
        Vec3f pPos = player->actor.world.pos;
        f32 pyaw = (f32)player->actor.world.rot.y * (3.14159265f / 32768.0f);
        Vec3f zero = { 0, 0, 0 };

        // Electric attack colors (yellow sparks, white-warm env)
        static Color_RGBA8 elecPrim = { 255, 230, 50, 255 };
        static Color_RGBA8 elecEnv = { 120, 180, 255, 200 };
        // Normal hit colors (white impact)
        static Color_RGBA8 hitPrim = { 255, 255, 255, 255 };
        static Color_RGBA8 hitEnv = { 200, 200, 200, 200 };
        // Thunder colors (bright white-blue)
        static Color_RGBA8 thdrPrim = { 200, 220, 255, 255 };
        static Color_RGBA8 thdrEnv = { 100, 150, 255, 255 };

        const SSBBActionDef* vfxDef = SSBBAction_Get(vfxAct);
        u8 inHitbox = vfxDef && vfxDef->hitboxStartFrame > 0 && sPika.actionFrame >= vfxDef->hitboxStartFrame &&
                      sPika.actionFrame <= vfxDef->hitboxEndFrame;

        // ── Forward Smash: light orbs surrounding the collider (3-6 based on charge) ──
        if (vfxAct == SSBB_ACT_ATTACK_FSMASH && inHitbox && (play->gameplayFrames % 2) == 0) {
            f32 chPct = (f32)sPika.smashCharge / 60.0f;
            if (chPct > 1.0f)
                chPct = 1.0f;
            s32 orbCount = 3 + (s32)(chPct * 3.0f); // 3 uncharged → 6 full
            f32 orbRadius = 15.0f + chPct * 10.0f;  // matches collider radius
            f32 rotOfs = (f32)play->gameplayFrames * 0.6f;
            Vec3f center = { pPos.x + sinf(pyaw) * 5.0f, pPos.y + 10.0f, pPos.z + cosf(pyaw) * 5.0f };
            for (s32 oi = 0; oi < orbCount; oi++) {
                f32 angle = (f32)oi * (6.28318f / (f32)orbCount) + rotOfs;
                Vec3f orbPos = { center.x + Math_SinF(angle) * orbRadius, center.y + Math_CosF(angle) * 5.0f,
                                 center.z + Math_CosF(angle) * orbRadius };
                EffectSsGSpk_SpawnNoAccel(play, &player->actor, &orbPos, &zero, &zero, &hitPrim, &hitEnv, 150, 6);
            }
        }

        // ── Down Smash: spinning electric discharges on ground ──
        if (vfxAct == SSBB_ACT_ATTACK_DSMASH && inHitbox && (play->gameplayFrames % 2) == 0) {
            for (s32 si = 0; si < 4; si++) {
                u16 sAngle = (u16)(si * 0x4000 + play->gameplayFrames * 0x1800);
                Vec3f sparkPos = { pPos.x + Math_SinS((s16)sAngle) * 25.0f, pPos.y + 5.0f,
                                   pPos.z + Math_CosS((s16)sAngle) * 25.0f };
                Vec3f sparkVel = { Math_SinS((s16)sAngle) * 2.0f, 2.0f, Math_CosS((s16)sAngle) * 2.0f };
                EffectSsBlast_Spawn(play, &sparkPos, &sparkVel, &zero, &elecPrim, &elecEnv, 160, -9, 1, 6);
            }
        }

        // ── Nair: electric ring around body ──
        if (vfxAct == SSBB_ACT_ATTACK_NAIR && inHitbox && (play->gameplayFrames % 3) == 0) {
            for (s32 ni = 0; ni < 6; ni++) {
                u16 nAngle = (u16)(ni * 0x2AAB + play->gameplayFrames * 0x1000);
                Vec3f ringPos = { pPos.x + Math_SinS((s16)nAngle) * 18.0f,
                                  pPos.y + 20.0f + Math_CosS((s16)nAngle) * 10.0f,
                                  pPos.z + Math_CosS((s16)nAngle) * 18.0f };
                EffectSsBlast_Spawn(play, &ringPos, &zero, &zero, &elecPrim, &elecEnv, 120, -6, 1, 5);
            }
        }

        // ── Fair/Dair: electric drill sparkles ──
        if ((vfxAct == SSBB_ACT_ATTACK_FAIR || vfxAct == SSBB_ACT_ATTACK_DAIR) && inHitbox &&
            (play->gameplayFrames % 2) == 0) {
            Vec3f drillDir = { sinf(pyaw) * 2.0f, (vfxAct == SSBB_ACT_ATTACK_DAIR) ? -3.0f : 0.0f, cosf(pyaw) * 2.0f };
            Vec3f drillPos = { pPos.x + sinf(pyaw) * 15.0f, pPos.y + 15.0f, pPos.z + cosf(pyaw) * 15.0f };
            EffectSsBlast_Spawn(play, &drillPos, &drillDir, &zero, &elecPrim, &elecEnv, 160, -9, 1, 6);
        }

        // ── Bair: "Pikacopter" horizontal disc sparks ──
        if (vfxAct == SSBB_ACT_ATTACK_BAIR && inHitbox && (play->gameplayFrames % 2) == 0) {
            Vec3f bairPos = { pPos.x - sinf(pyaw) * 15.0f, pPos.y + 18.0f, pPos.z - cosf(pyaw) * 15.0f };
            Vec3f bairVel = { -sinf(pyaw) * 2.0f, 0.5f, -cosf(pyaw) * 2.0f };
            EffectSsBlast_Spawn(play, &bairPos, &bairVel, &zero, &elecPrim, &elecEnv, 144, -9, 1, 6);
        }

        // ── Quick Attack: afterimage trail + lightning bolts + boomerang whoosh ──
        if ((vfxAct == SSBB_ACT_SPECIAL_HI_START || vfxAct == SSBB_ACT_SPECIAL_HI_AIR_START) && sPika.qatkPhase >= 1) {
            // White star afterimage trail behind Pikachu (6 particles per frame)
            for (s32 qi = 0; qi < 6; qi++) {
                f32 spreadX = ((play->gameplayFrames + qi * 3) % 9 - 4) * 2.5f;
                f32 spreadY = ((play->gameplayFrames + qi * 5) % 7 - 3) * 2.5f;
                f32 spreadZ = ((play->gameplayFrames + qi * 7) % 9 - 4) * 2.5f;
                Vec3f starPos = { pPos.x + spreadX - sPika.qatkDir.x * (qi * 4.0f),
                                  pPos.y + 15.0f + spreadY - sPika.qatkDir.y * (qi * 4.0f),
                                  pPos.z + spreadZ - sPika.qatkDir.z * (qi * 4.0f) };
                Vec3f fadeVel = { -sPika.qatkDir.x * 1.5f, 0.3f, -sPika.qatkDir.z * 1.5f };
                EffectSsBlast_Spawn(play, &starPos, &fadeVel, &zero, &hitPrim, &elecEnv, 160, -10, 1, 6);
            }
            // Yellow KiraKira sparkles at Pikachu's position
            if ((play->gameplayFrames % 2) == 0) {
                for (s32 k = 0; k < 3; k++) {
                    f32 angle = (f32)k * 2.094f + (f32)play->gameplayFrames * 0.8f;
                    Vec3f kPos = { pPos.x + Math_SinF(angle) * 12.0f, pPos.y + 15.0f, pPos.z + Math_CosF(angle) * 12.0f };
                    Vec3f kVel = { Math_SinF(angle) * 1.5f, 1.0f, Math_CosF(angle) * 1.5f };
                    EffectSsKiraKira_SpawnFocused(play, &kPos, &kVel, &zero, &elecPrim, &elecEnv, 400, 12);
                }
            }
            // Lightning bolt every 3 frames along trail
            if ((play->gameplayFrames % 3) == 0) {
                Vec3f boltPos = { pPos.x - sPika.qatkDir.x * 20.0f, pPos.y + 15.0f, pPos.z - sPika.qatkDir.z * 20.0f };
                EffectSsLightning_Spawn(play, &boltPos, &elecPrim, &elecEnv, 120, (s16)(play->gameplayFrames * 0x2000), 10, 3);
            }
            // Continuous whoosh SFX
            func_8002F974(&player->actor, NA_SE_IT_BOOMERANG_FLY - SFX_FLAG);
        }

        // ── Skull Bash CHARGE: electric sparks while charging ──
        if (vfxAct == SSBB_ACT_SPECIAL_S_HOLD && (play->gameplayFrames % 3) == 0) {
            f32 chPct = (f32)sPika.chargeTimer / 60.0f;
            if (chPct > 1.0f)
                chPct = 1.0f;
            s32 sparkCount = 2 + (s32)(chPct * 6.0f);
            for (s32 i = 0; i < sparkCount; i++) {
                f32 angle = (f32)i * (6.28318f / (f32)sparkCount) + (f32)play->gameplayFrames * 0.3f;
                f32 r = 8.0f + chPct * 20.0f;
                Vec3f sPos = { pPos.x + Math_SinF(angle) * r, pPos.y + 15.0f, pPos.z + Math_CosF(angle) * r };
                Vec3f sVel = { Math_SinF(angle) * 1.0f, 2.0f, Math_CosF(angle) * 1.0f };
                Vec3f sAccel = { 0, -0.1f, 0 };
                EffectSsKiraKira_SpawnFocused(play, &sPos, &sVel, &sAccel, &elecPrim, &elecEnv, 400, 16);
            }
            // Lightning bolts at higher charge
            if (chPct > 0.5f && (play->gameplayFrames % 6) == 0) {
                static Color_RGBA8 sBoltPrim = { 100, 180, 255, 255 };
                static Color_RGBA8 sBoltEnv = { 30, 80, 255, 200 };
                Vec3f bPos = { pPos.x, pPos.y + 20.0f, pPos.z };
                EffectSsLightning_Spawn(play, &bPos, &sBoltPrim, &sBoltEnv, (s16)(60 + chPct * 80),
                                        (s16)(play->gameplayFrames * 0x1000), 10, 3);
            }
        }

        // ── Skull Bash LAUNCH: trail of lightning + KiraKira behind Pikachu ──
        if (vfxAct == SSBB_ACT_SPECIAL_S && (play->gameplayFrames % 2) == 0) {
            // Trail behind
            Vec3f trailPos = { pPos.x - sinf(pyaw) * 15.0f, pPos.y + 12.0f, pPos.z - cosf(pyaw) * 15.0f };
            Vec3f trailVel = { -sinf(pyaw) * 3.0f, 1.5f, -cosf(pyaw) * 3.0f };
            Vec3f trailAccel = { 0, -0.1f, 0 };
            EffectSsKiraKira_SpawnFocused(play, &trailPos, &trailVel, &trailAccel, &elecPrim, &elecEnv, 500, 18);
            // Side bolts
            static Color_RGBA8 sLBPrim = { 255, 230, 50, 255 };
            static Color_RGBA8 sLBEnv = { 100, 180, 255, 200 };
            EffectSsLightning_Spawn(play, &pPos, &sLBPrim, &sLBEnv, 120, (s16)(play->gameplayFrames * 0x2000), 8, 2);
        }

        // ── Skull Bash: propulsion blast behind Pikachu on frame 1 ──
        if (vfxAct == SSBB_ACT_SPECIAL_S && sPika.actionFrame == 1) {
            Vec3f propPos = { pPos.x - sinf(pyaw) * 20.0f, pPos.y + 15.0f, pPos.z - cosf(pyaw) * 20.0f };
            Vec3f propVel = { -sinf(pyaw) * 5.0f, 3.0f, -cosf(pyaw) * 5.0f };
            EffectSsBlast_Spawn(play, &propPos, &propVel, &zero, &hitPrim, &hitEnv, 400, -15, 2, 15);
            EffectSsBlast_Spawn(play, &propPos, &propVel, &zero, &elecPrim, &hitEnv, 280, -12, 2, 10);
        }

        // ── Thunder Jolt: spawned by PikaItem_ThunderJolt as EffectSsBlast ──
        // (VFX handled in the Thunder Jolt actor)

        // ── Thunder (Down-B) ongoing: lightning column particles ──
        if ((vfxAct == SSBB_ACT_SPECIAL_LW_LOOP || vfxAct == SSBB_ACT_SPECIAL_LW_AIR_LOOP) &&
            (play->gameplayFrames % 2) == 0) {
            // Vertical bolt particles
            for (s32 ti = 0; ti < 3; ti++) {
                Vec3f tPos = { pPos.x + (ti - 1) * 5.0f, pPos.y + 30.0f + ti * 40.0f, pPos.z + (ti - 1) * 5.0f };
                Vec3f tVel = { 0, 8.0f, 0 };
                EffectSsBlast_Spawn(play, &tPos, &tVel, &zero, &thdrPrim, &thdrEnv, 240, -9, 2, 9);
            }
            // Ground shockwave ring
            for (s32 ri = 0; ri < 6; ri++) {
                u16 rAngle = (u16)(ri * 0x2AAB);
                f32 ringDist = 20.0f + sPika.actionFrame * 3.0f;
                Vec3f ringPos = { pPos.x + Math_SinS((s16)rAngle) * ringDist, pPos.y + 5.0f,
                                  pPos.z + Math_CosS((s16)rAngle) * ringDist };
                Vec3f ringVel = { Math_SinS((s16)rAngle) * 4.0f, 1.0f, Math_CosS((s16)rAngle) * 4.0f };
                EffectSsBlast_Spawn(play, &ringPos, &ringVel, &zero, &elecPrim, &thdrEnv, 200, -9, 1, 7);
            }
            // Screen darken during thunder
            Environment_AdjustLights(play, 0.0f, 300.0f, 0.05f, 0.0f);
        }

        // ═══ JAB COMBO VFX ═══
        // Hit 1 (JAB): white impact puff at head
        if (vfxAct == SSBB_ACT_ATTACK_JAB && inHitbox) {
            Vec3f jabPos = { pPos.x + sinf(pyaw) * 18.0f, pPos.y + 20.0f, pPos.z + cosf(pyaw) * 18.0f };
            EffectSsGSpk_SpawnNoAccel(play, &player->actor, &jabPos, &zero, &zero, &hitPrim, &hitEnv, 150, 6);
        }
        // Hit 2 (UTILT in combo): circular sword swing trail around body
        if (vfxAct == SSBB_ACT_ATTACK_UTILT && inHitbox && (play->gameplayFrames % 2) == 0) {
            for (s32 i = 0; i < 4; i++) {
                f32 angle = (f32)i * 1.5708f + (f32)play->gameplayFrames * 0.5f;
                Vec3f swingPos = { pPos.x + Math_SinF(angle) * 20.0f, pPos.y + 15.0f,
                                   pPos.z + Math_CosF(angle) * 20.0f };
                EffectSsGSpk_SpawnNoAccel(play, &player->actor, &swingPos, &zero, &zero, &hitPrim, &hitEnv, 120, 4);
            }
        }
        // Hit 3 (USMASH in combo): electric discharge in front (paralysis zone)
        if (vfxAct == SSBB_ACT_ATTACK_USMASH && inHitbox && (play->gameplayFrames % 2) == 0) {
            Vec3f smashPos = { pPos.x + sinf(pyaw) * 25.0f, pPos.y + 15.0f, pPos.z + cosf(pyaw) * 25.0f };
            EffectSsLightning_Spawn(play, &smashPos, &elecPrim, &elecEnv, 150, (s16)(play->gameplayFrames * 0x1000), 12,
                                    3);
            EffectSsKiraKira_SpawnSmallYellow(play, &smashPos, &zero, &zero);
        }

        // ── Tilts: white swing trail (physical attacks, no electricity) ──
        if ((vfxAct == SSBB_ACT_ATTACK_FTILT || vfxAct == SSBB_ACT_ATTACK_FTILT_HI ||
             vfxAct == SSBB_ACT_ATTACK_FTILT_LW || vfxAct == SSBB_ACT_ATTACK_UTILT ||
             vfxAct == SSBB_ACT_ATTACK_DTILT) &&
            inHitbox && (play->gameplayFrames % 2) == 0) {
            f32 ofsY = (vfxAct == SSBB_ACT_ATTACK_UTILT) ? 30.0f : 10.0f;
            f32 ofsF = (vfxAct == SSBB_ACT_ATTACK_DTILT) ? 20.0f : 15.0f;
            Vec3f tiltPos = { pPos.x + sinf(pyaw) * ofsF, pPos.y + ofsY, pPos.z + cosf(pyaw) * ofsF };
            EffectSsBlast_Spawn(play, &tiltPos, &zero, &zero, &hitPrim, &hitEnv, 120, -10, 2, 4);
        }

        // ── Up Smash: Lightning bolts shooting upward from tail ──
        if (vfxAct == SSBB_ACT_ATTACK_USMASH && inHitbox && (play->gameplayFrames % 2) == 0) {
            static Color_RGBA8 sPrimB = { 100, 180, 255, 255 };
            static Color_RGBA8 sEnvB = { 30, 80, 255, 200 };
            for (s32 ui = 0; ui < 3; ui++) {
                Vec3f uPos = { pPos.x + (ui - 1) * 8.0f, pPos.y + 15.0f + ui * 15.0f, pPos.z };
                EffectSsLightning_Spawn(play, &uPos, &elecPrim, &sEnvB, 120,
                                        (s16)(ui * 0x2000 + play->gameplayFrames * 0x800), 10, 3);
            }
        }

        // ── Uair: electric tail arc upward ──
        if (vfxAct == SSBB_ACT_ATTACK_UAIR && inHitbox && (play->gameplayFrames % 2) == 0) {
            static Color_RGBA8 sPrimB2 = { 100, 180, 255, 255 };
            Vec3f uairPos = { pPos.x, pPos.y + 30.0f, pPos.z };
            EffectSsLightning_Spawn(play, &uairPos, &elecPrim, &sPrimB2, 100, (s16)(play->gameplayFrames * 0x1000), 8,
                                    3);
        }

        // ── Dash Attack: white speed trail behind Pikachu (physical) ──
        if (vfxAct == SSBB_ACT_ATTACK_DASH && inHitbox && (play->gameplayFrames % 2) == 0) {
            Vec3f dashPos = { pPos.x - sinf(pyaw) * 10.0f, pPos.y + 12.0f, pPos.z - cosf(pyaw) * 10.0f };
            Vec3f dashVel = { -sinf(pyaw) * 2.0f, 1.0f, -cosf(pyaw) * 2.0f };
            EffectSsBlast_Spawn(play, &dashPos, &dashVel, &zero, &hitPrim, &hitEnv, 140, -10, 2, 5);
        }

        // ── Forward Throw: electrocute effect on release frame ──
        if (vfxAct == SSBB_ACT_THROW_F && sPika.actionFrame == 5) {
            Vec3f throwPos = { pPos.x + sinf(pyaw) * 30.0f, pPos.y + 15.0f, pPos.z + cosf(pyaw) * 30.0f };
            for (s32 ei = 0; ei < 4; ei++) {
                Vec3f eVel = { (ei - 2) * 3.0f, 4.0f, (ei % 2) * 3.0f };
                EffectSsBlast_Spawn(play, &throwPos, &eVel, &zero, &elecPrim, &elecEnv, 200, -9, 1, 7);
            }
        }

        // ── Restore lighting after Thunder ends ──
        if ((vfxAct == SSBB_ACT_SPECIAL_LW_LOOP || vfxAct == SSBB_ACT_SPECIAL_LW_AIR_LOOP) && Pika_ActionFinished()) {
            Environment_AdjustLights(play, 0.0f, 850.0f, 0.2f, 0.0f);
        }
    }

    // ── Hitbox activation ──
    // Specials have K flag (not A), so Pika_IsAttacking() misses them.
    // Explicitly enable hitbox for ALL actions that deal damage.
    u8 hitboxActive = 0;
    SSBBActionId act = sPika.currentAction;

    // Regular attacks (flag A): use ATKD hitbox frames
    if (Pika_IsAttacking() && sPika.colliderReady) {
        const SSBBActionDef* def = SSBBAction_Get(act);
        if (def && def->hitboxStartFrame > 0 && sPika.actionFrame >= def->hitboxStartFrame &&
            sPika.actionFrame <= def->hitboxEndFrame)
            hitboxActive = 1;
    }

    // Specials & other non-A-flag attacks: always active during their action
    if (act == SSBB_ACT_SPECIAL_LW_LOOP || act == SSBB_ACT_SPECIAL_LW_AIR_LOOP ||  // Thunder
        act == SSBB_ACT_SPECIAL_S || act == SSBB_ACT_SPECIAL_S_AIR_START ||          // Skull Bash dash
        act == SSBB_ACT_SPECIAL_N || act == SSBB_ACT_SPECIAL_N_AIR ||                // Thunder Jolt
        act == SSBB_ACT_SPECIAL_HI_START || act == SSBB_ACT_SPECIAL_HI_AIR_START ||  // Quick Attack
        act == SSBB_ACT_ITEM_HAMMER_WAIT || act == SSBB_ACT_ITEM_HAMMER_MOVE ||      // Hammer
        act == SSBB_ACT_ITEM_HAMMER_AIR ||
        act == SSBB_ACT_CATCH || act == SSBB_ACT_CATCH_DASH ||                       // Grab
        act == SSBB_ACT_CATCH_ATTACK ||                                               // Pummel
        act == SSBB_ACT_THROW_F || act == SSBB_ACT_THROW_B ||                        // Throws
        act == SSBB_ACT_THROW_HI || act == SSBB_ACT_THROW_LW ||
        act == SSBB_ACT_SWING1 || act == SSBB_ACT_SWING3 ||                          // Item swings
        act == SSBB_ACT_SWING4 || act == SSBB_ACT_SWING4_BAT ||
        act == SSBB_ACT_SWING_DASH)
        hitboxActive = 1;

    if (hitboxActive && sPika.colliderReady) {

            // ── Per-attack hitbox parameters ──
            // dmgFlags must cover what each boss/puzzle needs:
            //   Sword:     0x00000700 (KOKIRI|MASTER|GIANT) — most enemies
            //   Hammer:    0x00000040 — rusted switches, Volvagia, Ganon
            //   Arrow:     0x00000020 — Gohma eye, Bongo Bongo hands
            //   Hookshot:  0x00000080 — Morpha, various pulls
            //   Boomerang: 0x00000010 — Barinade tentacles, parasites
            //   Explosive: 0x00000008 — bombable walls, Dodongo, Ganon
            //   MagicFire: 0x00020000 — ice blocks, torches
            //   MagicLight:0x00080000 — Ganondorf, dark enemies
            //   ArrowLight:0x00002000 — Ganondorf stun, Bongo Bongo
            //   DekuNut:   0x00000001 — stun
            //   Spin:      0x01C00000 — spin attack damage

            // ── ATKD-verified hitbox data from FitPikachuMotionEtc.pac ──
            // Sizes scaled: Brawl range × 1.5 for OOT collider units
            // dmgFlags: real macros from z64collision_check.h
            s32 radius = 20;
            s32 height = 25;
            s32 damage = 4;
            // Default: hits EVERYTHING (all damage bits except shield/mirror)
            u32 dmgFlags = DMG_DEFAULT;
            u8 sfxType = TOUCH_SFX_WOOD;
            u32 atTypeFlags = AT_ON | AT_TYPE_PLAYER;

            SSBBActionId act = sPika.currentAction;

            // ═══ JAB COMBO (3 hits) ═══
            // Hit 1 (JAB): Headbutt — sphere at head position, sword damage
            if (act == SSBB_ACT_ATTACK_JAB) {
                damage = 4; // Master sword damage
                radius = 15;
                height = 15;
                dmgFlags = DMG_SLASH_MASTER | DMG_SPIN_MASTER;
            }
            // ── Forward Tilt: BGS damage ──
            if (act == SSBB_ACT_ATTACK_FTILT || act == SSBB_ACT_ATTACK_FTILT_HI || act == SSBB_ACT_ATTACK_FTILT_LW) {
                damage = 8;
                radius = 13;
                height = 12;
                dmgFlags = DMG_SLASH_GIANT | DMG_SPIN_GIANT | DMG_JUMP_GIANT;
            }
            // Hit 2 (UTILT in combo): BGS damage
            if (act == SSBB_ACT_ATTACK_UTILT) {
                damage = 8;
                radius = 30;
                height = 30;
                dmgFlags = DMG_SLASH_GIANT | DMG_SPIN_GIANT | DMG_JUMP_GIANT;
            }
            // ── Down Tilt: BGS damage ──
            if (act == SSBB_ACT_ATTACK_DTILT) {
                damage = 8;
                radius = 13;
                height = 15;
                dmgFlags = DMG_SLASH_GIANT | DMG_SPIN_GIANT | DMG_JUMP_GIANT;
            }
            // ── Dash Attack: ATKD frames 4-16, X=[3,32] Y=[0,12] ──
            if (act == SSBB_ACT_ATTACK_DASH) {
                damage = 4;
                radius = 22;
                height = 18;
                dmgFlags = DMG_SLASH_MASTER | DMG_HAMMER_SWING | DMG_MAGIC_LIGHT;
            }
            // ── Aerials: boosted radius and damage for OOT gameplay ──
            if (act == SSBB_ACT_ATTACK_NAIR) {
                damage = 8;
                radius = 30;
                height = 30;
            }
            if (act == SSBB_ACT_ATTACK_FAIR) {
                damage = 6;
                radius = 30;
                height = 25;
            }
            if (act == SSBB_ACT_ATTACK_BAIR) {
                damage = 6;
                radius = 30;
                height = 25;
            }
            if (act == SSBB_ACT_ATTACK_UAIR) {
                damage = 6;
                radius = 35;
                height = 50;
            }
            if (act == SSBB_ACT_ATTACK_DAIR) {
                damage = 8;
                radius = 11;
                height = 30;
            } // X=[-7,7] Y=[-10,10]
            if (act >= SSBB_ACT_ATTACK_NAIR && act <= SSBB_ACT_ATTACK_DAIR) {
                dmgFlags = DMG_SLASH_KOKIRI | DMG_SLASH_MASTER | DMG_BOOMERANG | DMG_MAGIC_LIGHT;
            }
            // ── Forward Smash: damage scales with charge (4 uncharged → 8 full) ──
            // ── Forward Smash: BGS damage, charge 8→16 (double BGS jump slash at max) ──
            if (act == SSBB_ACT_ATTACK_FSMASH) {
                f32 chPct = (f32)sPika.smashCharge / 60.0f;
                if (chPct > 1.0f) chPct = 1.0f;
                damage = 8 + (s32)(chPct * 8.0f);   // 8-16
                radius = 15 + (s32)(chPct * 10.0f);
                height = 15;
                sfxType = TOUCH_SFX_HARD;
                dmgFlags = DMG_SLASH_GIANT | DMG_SPIN_GIANT | DMG_JUMP_GIANT;
            }
            // ── Up Smash (combo hit 3): BGS damage + paralyze ──
            if (act == SSBB_ACT_ATTACK_USMASH) {
                damage = 8;
                radius = 25;
                height = 25;
                sfxType = TOUCH_SFX_HARD;
                dmgFlags = DMG_SLASH_GIANT | DMG_SPIN_GIANT | DMG_JUMP_GIANT | DMG_DEKU_NUT;
            }
            // ── Down Smash: BGS damage ──
            if (act == SSBB_ACT_ATTACK_DSMASH) {
                damage = 8;
                radius = 21;
                height = 27;
                sfxType = TOUCH_SFX_HARD;
                dmgFlags = DMG_SLASH_GIANT | DMG_SPIN_GIANT | DMG_JUMP_GIANT;
            }
            // ── Final Smash: ALL damage types ──
            if (act == SSBB_ACT_FINAL || act == SSBB_ACT_FINAL2 || act == SSBB_ACT_FINAL_AIR ||
                act == SSBB_ACT_FINAL_AIR2) {
                radius = 80;
                height = 80;
                damage = 20;
                sfxType = TOUCH_SFX_HARD;
                dmgFlags = DMG_DEFAULT;
                atTypeFlags = AT_ON | AT_TYPE_ALL;
            }
            // ── Thunder Jolt: projectile-type ──
            if (act == SSBB_ACT_SPECIAL_N || act == SSBB_ACT_SPECIAL_N_AIR) {
                radius = 15;
                height = 15;
                damage = 6;
                dmgFlags = DMG_SLINGSHOT | DMG_SLASH_KOKIRI | DMG_ARROW_NORMAL | DMG_HOOKSHOT;
            }
            // ── Skull Bash: damage scales with charge (7% uncharged → 25% full) ──
            if (act == SSBB_ACT_SPECIAL_S) {
                f32 chPct = (f32)sPika.chargeTimer / 60.0f;
                if (chPct > 1.0f)
                    chPct = 1.0f;
                damage = 4 + (s32)(chPct * 12.0f);  // 4-16 damage based on charge
                radius = 25 + (s32)(chPct * 15.0f); // 25-40 radius
                height = 30 + (s32)(chPct * 10.0f); // 30-40 height
                sfxType = TOUCH_SFX_HARD;
                // Hammer flag to break things (rusted switches, rocks) + slash + electric
                dmgFlags = DMG_SLASH | DMG_HAMMER_SWING | DMG_HOOKSHOT | DMG_EXPLOSIVE | DMG_MAGIC_LIGHT;
            }

            // ── Quick Attack: large boomerang collider, stuns Barinade ──
            if (act == SSBB_ACT_SPECIAL_HI_START || act == SSBB_ACT_SPECIAL_HI_AIR_START) {
                radius = 40;
                height = 50;
                damage = 8;
                dmgFlags = DMG_BOOMERANG | DMG_DEKU_NUT | DMG_SLASH_MASTER | DMG_SLINGSHOT | DMG_HOOKSHOT | DMG_MAGIC_LIGHT;
            }
            // ── THUNDER (L+B): MASSIVE AoE — 2× Biggoron jump slash ──
            // Active during LOOP phase (60 frames of continuous damage).
            // Radius 120 covers entire arena. Damage 16 = 4 full hearts.
            if (act == SSBB_ACT_SPECIAL_LW_LOOP || act == SSBB_ACT_SPECIAL_LW_AIR_LOOP) {
                radius = 120;
                height = 150;
                damage = 16;
                sfxType = TOUCH_SFX_HARD;
                dmgFlags = DMG_SLASH_MASTER | DMG_SLASH_GIANT | DMG_HAMMER_SWING | DMG_EXPLOSIVE | DMG_ARROW_LIGHT |
                           DMG_MAGIC_FIRE | DMG_MAGIC_ICE | DMG_MAGIC_LIGHT | DMG_HOOKSHOT | DMG_BOOMERANG |
                           DMG_SLINGSHOT | DMG_UNBLOCKABLE;
                atTypeFlags = AT_ON | AT_TYPE_ALL;
            }
            // ── Grab: hookshot pull — must hit Morpha ──
            if (act == SSBB_ACT_CATCH) {
                radius = 30;
                height = 30;
                damage = 2;
                dmgFlags = DMG_HOOKSHOT | DMG_BOOMERANG | DMG_MAGIC_LIGHT;
            }
            if (act == SSBB_ACT_CATCH_DASH) {
                radius = 35;
                height = 30;
                damage = 2;
                dmgFlags = DMG_HOOKSHOT | DMG_BOOMERANG | DMG_MAGIC_LIGHT;
            }
            // ── Back Throw: strong knockback damage ──
            if (act == SSBB_ACT_THROW_B || act == SSBB_ACT_THROW_F ||
                act == SSBB_ACT_THROW_HI || act == SSBB_ACT_THROW_LW) {
                radius = 30;
                height = 30;
                damage = 8;
                dmgFlags = DMG_DEFAULT;
                sfxType = TOUCH_SFX_HARD;
            }
            // ── Pummel (CatchAttack): 2% Electric per hit ──
            if (act == SSBB_ACT_CATCH_ATTACK) {
                radius = 15;
                height = 20;
                damage = 2;
                dmgFlags = DMG_SLASH_KOKIRI | DMG_MAGIC_LIGHT;
            }

            // ── Hammer: ATKD range 36×23 ──
            if (act == SSBB_ACT_ITEM_HAMMER_WAIT || act == SSBB_ACT_ITEM_HAMMER_MOVE ||
                act == SSBB_ACT_ITEM_HAMMER_AIR) {
                radius = 72;
                height = 46;
                damage = 12;
                sfxType = TOUCH_SFX_HARD;
                dmgFlags = 0x40 | 0x08 | 0x400 | 0x80000 | 0x400000 | 0x800000 | 0x1000000;
            }

            // ── Melee items (Deku Stick): reflect energy balls ──
            if (act == SSBB_ACT_SWING1 || act == SSBB_ACT_SWING3 || act == SSBB_ACT_SWING4 ||
                act == SSBB_ACT_SWING4_BAT || act == SSBB_ACT_SWING_DASH) {
                damage = 4;
                dmgFlags = 0x02 | 0x100 | 0x200 | 0x400 | 0x100000;
                // DEKU_STICK | SLASH_ALL | SHIELD (reflect)
            }

            // ── Elemental Rod: NO hitbox here — rods spawn arrow projectiles ──
            // (handled in PikaItem_ElementalRod which spawns En_Arrow)
            if (act == SSBB_ACT_ITEM_SHOOT || act == SSBB_ACT_ITEM_SHOOT_AIR) {
                // Don't activate AT collider — the arrow actor handles damage
                radius = 0;
                height = 0;
                damage = 0;
            }

            sPika.atCyl.dim.radius = radius;
            sPika.atCyl.dim.height = height;
            // Thunder extends both up AND down; everything else starts at feet
            sPika.atCyl.dim.yShift = (act == SSBB_ACT_SPECIAL_LW_LOOP || act == SSBB_ACT_SPECIAL_LW_AIR_LOOP) ? -75 : 0;
            // Gigantamax: add UNBLOCKABLE to ALL attacks so bosses take damage
            if (sPika.gigantamax) dmgFlags |= DMG_UNBLOCKABLE;
            sPika.atCyl.info.toucher.dmgFlags = dmgFlags;
            sPika.atCyl.info.toucher.damage = (u8)damage;
            // Effect: 0x08=electric (Biri), 0x01=stun (Deku Nut), 0x00=normal
            if (dmgFlags & DMG_DEKU_NUT) {
                sPika.atCyl.info.toucher.effect = 0x01; // Stun/paralyze
            } else {
                sPika.atCyl.info.toucher.effect = 0x00; // Normal sword hit
            }
            sPika.atCyl.info.toucherFlags = TOUCH_ON | TOUCH_SFX_WOOD;
            sPika.atCyl.base.actor = &player->actor;
            sPika.atCyl.base.atFlags = atTypeFlags;

            // Position collider IN FRONT of Pikachu (not at feet)
            // Up attacks (utilt, usmash, uair) go above; down attacks below; rest forward
            Collider_UpdateCylinder(&player->actor, &sPika.atCyl);
            {
                f32 pyaw = (f32)player->actor.shape.rot.y * (M_PI / 0x8000);
                // All colliders centered on Pikachu, max 5 units forward offset
                f32 fwd = 0.0f; // Default: centered
                f32 up = 10.0f; // Default: body center

                // Thunder: centered on Pikachu, elevated
                if (act == SSBB_ACT_SPECIAL_LW_LOOP || act == SSBB_ACT_SPECIAL_LW_AIR_LOOP) {
                    fwd = 0.0f;
                    up = 30.0f;
                }
                // Jab combo
                if (act == SSBB_ACT_ATTACK_JAB) {
                    fwd = 5.0f;
                    up = 15.0f;
                }
                if (act == SSBB_ACT_ATTACK_UTILT) {
                    fwd = 0.0f;
                    up = 10.0f;
                }
                if (act == SSBB_ACT_ATTACK_USMASH) {
                    fwd = 5.0f;
                    up = 10.0f;
                }
                // Tilts
                if (act == SSBB_ACT_ATTACK_FTILT || act == SSBB_ACT_ATTACK_FTILT_HI ||
                    act == SSBB_ACT_ATTACK_FTILT_LW) {
                    fwd = 5.0f;
                    up = 10.0f;
                }
                if (act == SSBB_ACT_ATTACK_DTILT) {
                    fwd = 5.0f;
                    up = 0.0f;
                }
                // Smashes
                if (act == SSBB_ACT_ATTACK_FSMASH) {
                    fwd = 5.0f;
                    up = 10.0f;
                }
                if (act == SSBB_ACT_ATTACK_DSMASH) {
                    fwd = 0.0f;
                    up = 0.0f;
                }
                // Aerials
                if (act == SSBB_ACT_ATTACK_UAIR) {
                    fwd = 0.0f;
                    up = 20.0f;
                }
                if (act == SSBB_ACT_ATTACK_DAIR) {
                    fwd = 0.0f;
                    up = -5.0f;
                }
                if (act == SSBB_ACT_ATTACK_BAIR) {
                    fwd = -5.0f;
                    up = 10.0f;
                }
                if (act == SSBB_ACT_ATTACK_NAIR) {
                    fwd = 0.0f;
                    up = 10.0f;
                }
                if (act == SSBB_ACT_ATTACK_FAIR) {
                    fwd = 5.0f;
                    up = 10.0f;
                }
                // Dash/Skull Bash — these need more forward since Pikachu is moving
                if (act == SSBB_ACT_ATTACK_DASH) {
                    fwd = 5.0f;
                    up = 5.0f;
                }
                if (act == SSBB_ACT_SPECIAL_S) {
                    fwd = 5.0f;
                    up = 5.0f;
                }

                sPika.atCyl.dim.pos.x += (s16)(sinf(pyaw) * fwd);
                sPika.atCyl.dim.pos.z += (s16)(cosf(pyaw) * fwd);
                sPika.atCyl.dim.pos.y += (s16)up;
            }
            // Clear hit flags every frame so collider can hit repeatedly
            sPika.atCyl.base.atFlags &= ~(AT_HIT | AT_BOUNCED);
            // For multi-hit attacks (Thunder), reset hit tracking every 10 frames
            // so the collider can damage the same enemy again
            if ((act == SSBB_ACT_SPECIAL_LW_LOOP || act == SSBB_ACT_SPECIAL_LW_AIR_LOOP) &&
                (sPika.actionFrame % 10) == 0) {
                sPika.atCyl.info.atHit = NULL;
                sPika.atCyl.info.atHitInfo = NULL;
                sPika.atCyl.info.toucherFlags = TOUCH_ON | TOUCH_SFX_NONE;
            }
            CollisionCheck_SetAT(play, &play->colChkCtx, &sPika.atCyl.base);
        }

    // ── Advance animation frame ──
    sPika.actionFrame++;
    SSBBChar_Update(&sPika.charInst);

    // Sync shape yaw — only when moving or attacking (prevents idle jitter)
    if (player->linearVelocity > 0.5f || Pika_IsAttacking()) {
        player->actor.shape.rot.y = player->actor.world.rot.y;
    }

    // ── Gigantamax: persistent AT collider while ANY attack or recently attacked ──
    // Keep a large UNBLOCKABLE collider active during AND after attacks (lingers 15 frames)
    {
        static s32 sGiantAtkLinger = 0;
        if (sPika.gigantamax) {
            if (Pika_IsAttacking()) sGiantAtkLinger = 20;
            if (sGiantAtkLinger > 0) {
                sGiantAtkLinger--;
                // Scale the collider that the normal attack already set up
                // (the per-attack section already set radius/height/dmgFlags)
                // Just multiply by giantScale and ensure UNBLOCKABLE is present
                sPika.atCyl.dim.radius = (s16)(sPika.atCyl.dim.radius * sPika.giantScale);
                sPika.atCyl.dim.height = (s16)(sPika.atCyl.dim.height * sPika.giantScale);
                sPika.atCyl.info.toucher.dmgFlags |= DMG_UNBLOCKABLE;
                sPika.atCyl.base.atFlags &= ~AT_HIT;
                Collider_UpdateCylinder(&player->actor, &sPika.atCyl);
                CollisionCheck_SetAT(play, &play->colChkCtx, &sPika.atCyl.base);

                // Direct damage: only when attacking (linger > 0 = recently attacked)
                static s32 sDirectCD = 0;
                if (sDirectCD <= 0 && gPikaGigantamaxActive) {
                    f32 dmgR = 60.0f * sPika.giantScale;
                    for (s32 cat = ACTORCAT_ENEMY; cat <= ACTORCAT_BOSS; cat += (ACTORCAT_BOSS - ACTORCAT_ENEMY)) {
                        Actor* a = play->actorCtx.actorLists[cat].head;
                        while (a != NULL) {
                            f32 dx = player->actor.world.pos.x - a->world.pos.x;
                            f32 dz = player->actor.world.pos.z - a->world.pos.z;
                            if (sqrtf(dx*dx + dz*dz) < dmgR && a->update != NULL) {
                                // Increment colChkInfo.health for witches (they merge at sum >= 4)
                                // Decrement for normal bosses
                                if (a->id == ACTOR_BOSS_TW && a->params < 2) {
                                    // Witches (params 0,1): increment health (merge at sum >= 4)
                                    a->colChkInfo.health++;
                                    // No VFX for witches — they just absorb hits
                                } else if (a->id == ACTOR_BOSS_TW && a->params == 2 && a->world.pos.y < -500.0f) {
                                    // Combined Twinrova hidden below map (phase 1) — skip
                                } else {
                                    // Combined Twinrova (phase 2, visible) and all other bosses
                                    a->colChkInfo.health -= 4;
                                    if ((s8)a->colChkInfo.health <= 0) a->colChkInfo.health = 0;
                                    a->colChkInfo.damage += 4;
                                    // Electric spark VFX (blue flash + thunder SFX)
                                    Actor_SetColorFilter(a, 0x8000, 255, 0, 12);
                                    Audio_PlayActorSound2(a, NA_SE_EN_LIGHT_ARROW_HIT);
                                }
                            }
                            a = a->next;
                        }
                    }
                    sDirectCD = 15;
                }
                sDirectCD--;
            }
        } else {
            sGiantAtkLinger = 0;
        }
    }

} // end PikachuForm_Update

// ── Draw ────────────────────────────────────────────────────────────────────

extern "C" void PikachuForm_Draw(PlayState* play, Player* player) {
    if (!sPika.initialized || !sPika.charInst.ssbbAnim)
        return;

    // Damage flicker
    if (player->invincibilityTimer > 0 && (play->gameplayFrames % 4) < 2)
        return;

    Vec3f pos = player->actor.world.pos;
    Vec3s rot = player->actor.shape.rot;

    // Compensate axis mapping (+x,+z,-y): model is rotated 90° around X axis
    // Apply -90° X rotation to stand upright
    rot.x = 0x4000;

    // Scale override (Gigantamax multiplier)
    f32 origScale = sPika.charInst.def->scale;
    sPika.charInst.def->scale = PIKACHU_SCALE * sPika.giantScale;

    // ── Set eye material on segment 0x09 (DL references it for eye triangles) ──
    {
        OPEN_DISPS(play->state.gfxCtx);
        // Blinking: cycle through eye frames
        // Open=0 (frames 0-40), Half=1 (41-43), Closed=2 (44-46), Half=3 (47-49), loop
        static const Gfx* sEyeMatTable[] = {
            pikachu_ssbb_mat_eyes_00, pikachu_ssbb_mat_eyes_01, pikachu_ssbb_mat_eyes_02,
            pikachu_ssbb_mat_eyes_03, pikachu_ssbb_mat_eyes_04, pikachu_ssbb_mat_eyes_05,
        };
        s32 blinkCycle = play->gameplayFrames % 50;
        s32 eyeIdx = 0; // open
        if (blinkCycle >= 41 && blinkCycle <= 43)
            eyeIdx = 1; // half close
        else if (blinkCycle >= 44 && blinkCycle <= 46)
            eyeIdx = 2; // closed
        else if (blinkCycle >= 47 && blinkCycle <= 49)
            eyeIdx = 3; // half open

        gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)sEyeMatTable[eyeIdx]);
        CLOSE_DISPS(play->state.gfxCtx);
    }

    // Normal model draw
    SSBBSkin_Draw(&sPika.charInst, play, &pos, &rot);

    // Gigantamax outline: shadow DL at bigger scale, cull front = purple edges
    if (sPika.gigantamax && sPika.giantScale > 1.5f) {
        extern Gfx pikachu_ssbb_shadow_dl[];
        SSBBSkinMesh* skin = sPika.charInst.def->skinMesh;
        if (skin && skin->displayList) {
            OPEN_DISPS(play->state.gfxCtx);
            f32 outlineScale = PIKACHU_SCALE * sPika.giantScale * 1.05f;
            Matrix_SetTranslateRotateYXZ(pos.x, pos.y, pos.z, &rot);
            Matrix_Scale(outlineScale, outlineScale, outlineScale, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, pikachu_ssbb_shadow_dl);
            gDPPipeSync(POLY_OPA_DISP++);
            CLOSE_DISPS(play->state.gfxCtx);
        }
    }

    sPika.charInst.def->scale = origScale;

    // ── Held item rendering (bow, rod, hammer in Pikachu's hand) ──
    {
        SSBBActionId drawAct = sPika.currentAction;
        f32 pyaw = (f32)player->actor.shape.rot.y * (M_PI / 0x8000);
        f32 handX = pos.x + sinf(pyaw) * 12.0f;
        f32 handZ = pos.z + cosf(pyaw) * 12.0f;
        f32 handY = pos.y + 18.0f;
        Gfx* itemDL = NULL;
        f32 itemScale = 0.5f;

        // Hammer: draw during hammer anims
        if (drawAct == SSBB_ACT_ITEM_HAMMER_WAIT || drawAct == SSBB_ACT_ITEM_HAMMER_MOVE ||
            drawAct == SSBB_ACT_ITEM_HAMMER_AIR) {
            itemDL = (Gfx*)gGiHammerDL; // OTR path resolved by SoH resource manager
            itemScale = 0.4f;
        }
        // Bow/Rod: draw during shoot anims
        if (drawAct == SSBB_ACT_ITEM_SHOOT || drawAct == SSBB_ACT_ITEM_SHOOT_AIR) {
            itemDL = (Gfx*)gGiBowDL; // OTR path resolved by SoH resource manager
            itemScale = 0.3f;
        }
        // Hookshot/Whip: draw during catch anims
        if (drawAct == SSBB_ACT_CATCH || drawAct == SSBB_ACT_CATCH_DASH || drawAct == SSBB_ACT_CATCH_WAIT ||
            drawAct == SSBB_ACT_CATCH_ATTACK) {
            // No DL for hookshot in hand — the grab is implicit (Pikachu grabs with hands)
        }

        if (itemDL) {
            OPEN_DISPS(play->state.gfxCtx);
            Gfx_SetupDL_25Opa(play->state.gfxCtx);
            Matrix_Translate(handX, handY, handZ, MTXMODE_NEW);
            Matrix_RotateY(pyaw, MTXMODE_APPLY);
            Matrix_Scale(itemScale, itemScale, itemScale, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, itemDL);
            CLOSE_DISPS(play->state.gfxCtx);
        }
    }

    // ── Thunder Jolt: draw 5 light orbs (same as Light Rod balls) ──
    if (sPika.joltsActive) {
        OPEN_DISPS(play->state.gfxCtx);
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, 200);
        gDPSetEnvColor(POLY_XLU_DISP++, 255, 255, 50, 0);
        gDPPipeSync(POLY_XLU_DISP++);

        s16 rotZ = (play->gameplayFrames * 0x1000) + (s16)(Rand_ZeroOne() * 0x4000);
        for (s32 j = 0; j < PIKA_JOLT_COUNT; j++) {
            if (sPika.jolts[j].timer <= 0)
                continue;
            Matrix_Translate(sPika.jolts[j].pos.x, sPika.jolts[j].pos.y, sPika.jolts[j].pos.z, MTXMODE_NEW);
            Matrix_ReplaceRotation(&play->billboardMtxF);
            Matrix_Scale(5.5f, 5.5f, 5.5f, MTXMODE_APPLY);
            Matrix_RotateZ(((rotZ + (j * 0x3333)) / (f32)0x8000) * M_PI, MTXMODE_APPLY);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gPhantomEnergyBallDL);
        }
        CLOSE_DISPS(play->state.gfxCtx);
    }

    // ── Gigantamax aura (red/pink glow + yellow sparkles) ──
    if (sPika.gigantamax && sPika.giantScale > 1.5f) {
        Vec3f base = player->actor.world.pos;
        Vec3f zero = { 0.0f, 0.0f, 0.0f };
        f32 radius = 30.0f * sPika.giantScale;
        f32 rotOffset = (f32)play->gameplayFrames * 0.15f;

        // Red/pink aura sparkles ring (like the image)
        if ((play->gameplayFrames % 2) == 0) {
            static Color_RGBA8 primPink = { 255, 80, 120, 255 };
            static Color_RGBA8 envPink = { 200, 30, 80, 200 };
            static Color_RGBA8 primYellow = { 255, 255, 100, 255 };
            static Color_RGBA8 envYellow = { 255, 200, 50, 200 };
            for (s32 i = 0; i < 8; i++) {
                f32 angle = (f32)i * (6.28318f / 8.0f) + rotOffset;
                f32 sx = Math_SinF(angle);
                f32 sz = Math_CosF(angle);
                f32 height = 15.0f + Math_SinF((f32)play->gameplayFrames * 0.1f + (f32)i) * 20.0f;
                Vec3f pos = { base.x + sx * radius, base.y + height * sPika.giantScale, base.z + sz * radius };
                Vec3f vel = { sx * 1.5f, 2.0f, sz * 1.5f };
                Vec3f accel = { 0.0f, -0.1f, 0.0f };
                EffectSsKiraKira_SpawnFocused(play, &pos, &vel, &accel, &primPink, &envPink, 400, 15);
            }
            // Yellow sparkles near body (tail glow effect)
            for (s32 i = 0; i < 4; i++) {
                f32 angle = (f32)i * (6.28318f / 4.0f) - rotOffset * 1.5f;
                Vec3f pos = { base.x + Math_SinF(angle) * (radius * 0.4f),
                              base.y + 25.0f * sPika.giantScale + (f32)(i * 5),
                              base.z + Math_CosF(angle) * (radius * 0.4f) };
                Vec3f vel = { 0.0f, 3.0f, 0.0f };
                Vec3f accel = { 0.0f, -0.05f, 0.0f };
                EffectSsKiraKira_SpawnFocused(play, &pos, &vel, &accel, &primYellow, &envYellow, 500, 12);
            }
        }
        // (lightning bolts removed — just sparkle aura)
    }

    // ── Bubble Shield visual (Smash-style red translucent sphere) ──
    // Custom 30-vert UV sphere DL (6 lon × 4 lat, fits in 1 SPVertex load)
    static Vtx sPikaBubbleVtx[30] = {
        VTX(0, 100, 0, 0, 0, 0, 127, 0, 255),         VTX(0, 100, 0, 0, 0, 0, 127, 0, 255),
        VTX(0, 100, 0, 0, 0, 0, 127, 0, 255),         VTX(0, 100, 0, 0, 0, 0, 127, 0, 255),
        VTX(0, 100, 0, 0, 0, 0, 127, 0, 255),         VTX(0, 100, 0, 0, 0, 0, 127, 0, 255),
        VTX(71, 71, 0, 0, 0, 90, 90, 0, 255),         VTX(35, 71, 61, 0, 0, 45, 90, 78, 255),
        VTX(-35, 71, 61, 0, 0, -45, 90, 78, 255),     VTX(-71, 71, 0, 0, 0, -90, 90, 0, 255),
        VTX(-35, 71, -61, 0, 0, -45, 90, -78, 255),   VTX(35, 71, -61, 0, 0, 45, 90, -78, 255),
        VTX(100, 0, 0, 0, 0, 127, 0, 0, 255),         VTX(50, 0, 87, 0, 0, 64, 0, 110, 255),
        VTX(-50, 0, 87, 0, 0, -63, 0, 110, 255),      VTX(-100, 0, 0, 0, 0, -127, 0, 0, 255),
        VTX(-50, 0, -87, 0, 0, -64, 0, -110, 255),    VTX(50, 0, -87, 0, 0, 64, 0, -110, 255),
        VTX(71, -71, 0, 0, 0, 90, -90, 0, 255),       VTX(35, -71, 61, 0, 0, 45, -90, 78, 255),
        VTX(-35, -71, 61, 0, 0, -45, -90, 78, 255),   VTX(-71, -71, 0, 0, 0, -90, -90, 0, 255),
        VTX(-35, -71, -61, 0, 0, -45, -90, -78, 255), VTX(35, -71, -61, 0, 0, 45, -90, -78, 255),
        VTX(0, -100, 0, 0, 0, 0, -127, 0, 255),       VTX(0, -100, 0, 0, 0, 0, -127, 0, 255),
        VTX(0, -100, 0, 0, 0, 0, -127, 0, 255),       VTX(0, -100, 0, 0, 0, 0, -127, 0, 255),
        VTX(0, -100, 0, 0, 0, 0, -127, 0, 255),       VTX(0, -100, 0, 0, 0, 0, -127, 0, 255),
    };
    static Gfx sPikaBubbleDL[] = {
        gsSPVertex(sPikaBubbleVtx, 30, 0),
        gsSP2Triangles(0, 6, 1, 0, 1, 6, 7, 0),
        gsSP2Triangles(1, 7, 2, 0, 2, 7, 8, 0),
        gsSP2Triangles(2, 8, 3, 0, 3, 8, 9, 0),
        gsSP2Triangles(3, 9, 4, 0, 4, 9, 10, 0),
        gsSP2Triangles(4, 10, 5, 0, 5, 10, 11, 0),
        gsSP2Triangles(5, 11, 0, 0, 0, 11, 6, 0),
        gsSP2Triangles(6, 12, 7, 0, 7, 12, 13, 0),
        gsSP2Triangles(7, 13, 8, 0, 8, 13, 14, 0),
        gsSP2Triangles(8, 14, 9, 0, 9, 14, 15, 0),
        gsSP2Triangles(9, 15, 10, 0, 10, 15, 16, 0),
        gsSP2Triangles(10, 16, 11, 0, 11, 16, 17, 0),
        gsSP2Triangles(11, 17, 6, 0, 6, 17, 12, 0),
        gsSP2Triangles(12, 18, 13, 0, 13, 18, 19, 0),
        gsSP2Triangles(13, 19, 14, 0, 14, 19, 20, 0),
        gsSP2Triangles(14, 20, 15, 0, 15, 20, 21, 0),
        gsSP2Triangles(15, 21, 16, 0, 16, 21, 22, 0),
        gsSP2Triangles(16, 22, 17, 0, 17, 22, 23, 0),
        gsSP2Triangles(17, 23, 12, 0, 12, 23, 18, 0),
        gsSP2Triangles(18, 24, 19, 0, 19, 24, 25, 0),
        gsSP2Triangles(19, 25, 20, 0, 20, 25, 26, 0),
        gsSP2Triangles(20, 26, 21, 0, 21, 26, 27, 0),
        gsSP2Triangles(21, 27, 22, 0, 22, 27, 28, 0),
        gsSP2Triangles(22, 28, 23, 0, 23, 28, 29, 0),
        gsSP2Triangles(23, 29, 18, 0, 18, 29, 24, 0),
        gsSPEndDisplayList(),
    };

    if (sPika.shieldActive) {
        OPEN_DISPS(play->state.gfxCtx);
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);

        // Pulsing size
        f32 pulse = 1.0f + 0.03f * sinf(play->gameplayFrames * 0.3f);
        f32 shieldSize = 0.3f * sPika.shieldScale * pulse;

        // Shield color: ALWAYS red (like Smash Bros), alpha decreases as it shrinks
        u8 alpha = (u8)(40 + 15 * sPika.shieldScale); // ~50 alpha (0.2 opacity), fades as it shrinks
        gDPPipeSync(POLY_XLU_DISP++);
        gSPLoadGeometryMode(POLY_XLU_DISP++, G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK);
        gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0,
                          PRIMITIVE);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 220, 40, 40, alpha);

        // Center on Pikachu body
        Matrix_Translate(pos.x, pos.y + 15.0f, pos.z, MTXMODE_NEW);
        Matrix_Scale(shieldSize, shieldSize, shieldSize, MTXMODE_APPLY);

        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, sPikaBubbleDL);

        CLOSE_DISPS(play->state.gfxCtx);

        // Shrink shield while held
        sPika.shieldScale -= 0.002f;
        if (sPika.shieldScale <= 0.0f) {
            // Shield break! → FuraFura stun (~5 seconds = 300 frames per Brawl)
            sPika.shieldActive = 0;
            sPika.shieldScale = 1.0f;
            sPika.stunTimer = 300;
            Pika_SetAction(SSBB_ACT_FURA_FURA);
        }
    } else {
        // Regenerate shield when not active
        if (sPika.shieldScale < 1.0f)
            sPika.shieldScale += 0.001f;
    }

    // ── Pokemon-style textbox (full-width dark bar, white text) ──
    if (sPika.giantTextTimer > 0) {
        OPEN_DISPS(play->state.gfxCtx);

        s32 alpha = 200;
        if (sPika.giantTextTimer > 75) alpha = (90 - sPika.giantTextTimer) * 13;
        if (sPika.giantTextTimer < 15) alpha = sPika.giantTextTimer * 13;

        s32 barY = 170, barH = 32, skew = 8;

        gDPPipeSync(OVERLAY_DISP++);
        gDPSetCycleType(OVERLAY_DISP++, G_CYC_1CYCLE);
        gDPSetRenderMode(OVERLAY_DISP++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
        gDPSetCombineLERP(OVERLAY_DISP++, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE,
                                          0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE);
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 30, 30, 40, (u8)alpha);
        gDPFillRectangle(OVERLAY_DISP++, skew, barY, 320 - skew, barY + barH);
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 80, 180, 220, (u8)(alpha * 3 / 4));
        gDPFillRectangle(OVERLAY_DISP++, skew + 2, barY, 320 - skew - 2, barY + 2);
        gDPFillRectangle(OVERLAY_DISP++, skew + 2, barY + barH - 2, 320 - skew - 2, barY + barH);
        gDPPipeSync(OVERLAY_DISP++);

        GfxPrint printer;
        GfxPrint_Init(&printer);
        GfxPrint_Open(&printer, OVERLAY_DISP);
        GfxPrint_SetColor(&printer, 255, 255, 255, 255);
        if (sPika.giantTextType == 0) {
            GfxPrint_SetPos(&printer, 7, 23);
            GfxPrint_Printf(&printer, "Pikachu is Gigantamaxing!");
        } else {
            GfxPrint_SetPos(&printer, 6, 23);
            GfxPrint_Printf(&printer, "Pikachu returned to normal!");
        }
        OVERLAY_DISP = GfxPrint_Close(&printer);
        GfxPrint_Destroy(&printer);
        CLOSE_DISPS(play->state.gfxCtx);
    }
}

// ── Item hook functions (extern "C", called from transformation_masks.c) ────

// Thunder Jolt (Bow/Arrows) — spawn electric blast forward
extern "C" u8 PikaItem_ThunderJolt(PlayState* play, Player* player, s32 item) {
    (void)item;
    if (!sPika.initialized)
        return 1;
    u8 onGround = (player->actor.bgCheckFlags & 1) != 0;
    Pika_SetAction(onGround ? SSBB_ACT_SPECIAL_N : SSBB_ACT_SPECIAL_N_AIR);
    sPika.joltsActive = 2; // Pending — spawns at 1/3 of anim
    return 1;
}

// Thunder (Din's Fire / Demise Destruction) — lightning column
extern "C" u8 PikaItem_Thunder(PlayState* play, Player* player, s32 item) {
    (void)play;
    (void)player;
    (void)item;
    if (!sPika.initialized)
        return 0;
    Pika_SetAction(SSBB_ACT_SPECIAL_LW_START);
    return 0; // Let OOT process Din's Fire (spawns fire projectile)
}

// Quick Attack (Boomerang / Beetle) — 2-phase directional dash
extern "C" u8 PikaItem_QuickAtk(PlayState* play, Player* player, s32 item) {
    (void)play;
    (void)item;
    if (!sPika.initialized)
        return 0;
    u8 onGround = (player->actor.bgCheckFlags & 1) != 0;
    // Only 1 use in air (like Roc's Cape hasAirJumped)
    if (!onGround && sPika.airQuickAtkUsed) return 1;
    sPika.airQuickAtkUsed = onGround ? 0 : 1;
    Pika_SetAction(SSBB_ACT_SPECIAL_HI_START);
    sPika.qatkPhase = 0;
    return 1;
}

// Whip/Hookshot/Switch Hook → Grab (context-aware: standing/moving/air)
extern "C" u8 PikaItem_WhipGrab(PlayState* play, Player* player, s32 item) {
    (void)play;
    (void)item;
    if (!sPika.initialized)
        return 0;
    u8 onGround = (player->actor.bgCheckFlags & 1) != 0;
    if (!onGround) {
        Pika_SetAction(SSBB_ACT_CATCH);
    } else if (player->linearVelocity > 3.0f) {
        Pika_SetAction(SSBB_ACT_CATCH_DASH);
    } else {
        Pika_SetAction(SSBB_ACT_CATCH);
    }
    player->actor.shape.rot.y = player->actor.world.rot.y;
    return 0; // Let OOT process hookshot (spawns hookshot actor, pulls enemies)
}

// Iron Tail (Farore's/Hylia's) — up tilt variant
extern "C" u8 PikaItem_IronTail(PlayState* play, Player* player, s32 item) {
    (void)play;
    (void)player;
    (void)item;
    if (!sPika.initialized)
        return 0;
    Pika_SetAction(SSBB_ACT_ATTACK_UTILT);
    return 0; // Let OOT process Farore's Wind / Hylia's Grace
}

// Roc's Feather = ground jump (1 per landing), Roc's Cape = air jump (1 per landing)
extern "C" u8 PikaItem_RocsCape(PlayState* play, Player* player, s32 item) {
    (void)play;
    if (!sPika.initialized)
        return 1;
    u8 onGround = (player->actor.bgCheckFlags & 1) != 0;
    f32 stickMag = Pika_StickMag(play);

    if (onGround) {
        // Ground jump — Roc's Feather (limit 1)
        if (sPika.hasGroundJumped)
            return 1; // Already jumped, block
        sPika.hasGroundJumped = 1;
        // JumpSquat → JumpF or JumpB based on stick
        Pika_SetAction(SSBB_ACT_JUMP_SQUAT);
        player->actor.velocity.y = 5.0f;
    } else {
        // Air jump — Roc's Cape (limit 1)
        if (sPika.hasAirJumped)
            return 1; // Already double-jumped, block
        sPika.hasAirJumped = 1;
        // JumpAerialF or JumpAerialB based on stick vs facing
        if (stickMag > 0.3f) {
            s16 stickYaw = Math_Atan2S(play->state.input[0].cur.stick_x, play->state.input[0].cur.stick_y);
            s16 facingDiff = stickYaw - player->actor.shape.rot.y;
            Pika_SetAction(abs(facingDiff) > 0x4000 ? SSBB_ACT_JUMP_AERIAL_B : SSBB_ACT_JUMP_AERIAL_F);
        } else {
            Pika_SetAction(SSBB_ACT_JUMP_AERIAL_F);
        }
        player->actor.velocity.y = 4.0f;
    }
    return 1;
}

// Forward Tilt via Boomerang (Z-target close range)
extern "C" u8 PikaItem_ForwardTilt(PlayState* play, Player* player, s32 item) {
    (void)play;
    (void)item;
    if (!sPika.initialized)
        return 0;
    if (player->focusActor == NULL)
        return 0;

    f32 dx = player->focusActor->world.pos.x - player->actor.world.pos.x;
    f32 dz = player->focusActor->world.pos.z - player->actor.world.pos.z;
    f32 distSq = dx * dx + dz * dz;
    if (distSq > 40.0f * 40.0f)
        return 0;

    player->actor.world.rot.y = Math_Atan2S(dx, dz);
    player->actor.shape.rot.y = player->actor.world.rot.y;
    Pika_SetAction(SSBB_ACT_ATTACK_FTILT);
    return 1;
}

// Elemental Rods — spawn elemental arrow projectile matching the rod type
extern "C" u8 PikaItem_ElementalRod(PlayState* play, Player* player, s32 item) {
    if (!sPika.initialized)
        return 0;
    Pika_SetAction(SSBB_ACT_ITEM_SHOOT);
    return 0; // Let OOT process the rod/bow (OOT spawns the arrow/magic projectile natively)
}

// Hammer: JumpB (windup spin) → EscapeAir (ground slam) with hammer damage collider
extern "C" u8 PikaItem_Hammer(PlayState* play, Player* player, s32 item) {
    (void)play;
    (void)item;
    if (!sPika.initialized)
        return 1;
    Pika_SetAction(SSBB_ACT_JUMP_B);
    sPika.hammerPending = 1;
    return 1; // Block OOT — we handle hammer damage ourselves
}

// Bomb/Nut summon-throw: Pikachu does HeavyGet → spawns projectile → HeavyThrowHi
extern "C" u8 PikaItem_BombThrow(PlayState* play, Player* player, s32 item) {
    (void)play;
    if (!sPika.initialized)
        return 0;
    // Deku Nut: let OOT handle it natively (passthrough), just play throw anim
    if (item == ITEM_NUT) {
        Pika_SetAction(SSBB_ACT_LIGHT_THROW_F);
        return 0; // OOT handles the nut spawn/throw
    }
    // Bomb/Bombchu: custom spawn + throw
    s32 ammoItem = (item == ITEM_BOMBCHU) ? ITEM_BOMBCHU : ITEM_BOMB;
    if (AMMO(ammoItem) <= 0)
        return 1; // No ammo left, block
    Pika_SetAction(SSBB_ACT_HEAVY_GET);
    sPika.bombPending = (item == ITEM_BOMBCHU) ? 2 : 1;
    return 1;
}

// PassThrough — let OOT handle the item normally (nuts, etc.)
extern "C" u8 PikaItem_PassThrough(PlayState* play, Player* player, s32 item) {
    (void)play;
    (void)player;
    (void)item;
    return 0; // 0 = don't block, let OOT handle it
}

// Bottle use — play SSBB LightEat anim (grab/drink), then let OOT handle the bottle
extern "C" u8 PikaItem_Bottle(PlayState* play, Player* player, s32 item) {
    (void)play;
    (void)item;
    if (!sPika.initialized)
        return 0;
    Pika_SetAction(SSBB_ACT_LIGHT_EAT);
    return 0; // 0 = still let OOT process the bottle (drink potion, catch fairy, etc.)
}

// BlockSword — prevent sword use while Pikachu
extern "C" u8 PikaItem_BlockSword(PlayState* play, Player* player, s32 item) {
    (void)play;
    (void)player;
    (void)item;
    return 1; // 1 = block the item use
}

// Shield — activate bubble shield (handled in Update via R button)
extern "C" u8 PikaItem_Shield(PlayState* play, Player* player, s32 item) {
    (void)play;
    (void)player;
    (void)item;
    // Shield is handled by R button in PikachuForm_Update, not by item use
    return 1;
}

// Gigantamax (Giant's Mask) — toggle giant mode, costs MP
extern "C" u8 PikaItem_Gigantamax(PlayState* play, Player* player, s32 item) {
    (void)item;
    if (!sPika.initialized) return 0;

    // Debounce: ignore if already processing (prevents double-call toggle)
    if (sPika.giantCooldown > 0) return 1;
    sPika.giantCooldown = 10; // 10 frame cooldown

    if (sPika.gigantamax) {
        // Revert to normal
        sPika.gigantamax = 0;
        sPika.giantTextTimer = 90;
        sPika.giantTextType = 1;
    } else {
        if (gSaveContext.magic < 8) return 1;
        gSaveContext.magic -= 8;
        sPika.gigantamax = 1;
        sPika.giantScale = 1.0f;
        sPika.giantMpDrain = 0;
        sPika.giantTextTimer = 90;
        sPika.giantTextType = 0;
        Audio_PlayActorSound2(&player->actor, NA_SE_SY_CORRECT_CHIME);
    }
    return 1;
}
