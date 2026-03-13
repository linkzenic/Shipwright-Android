/**
 * pikachu_form.cpp — Pikachu Transformation Mask (Keaton Mask)
 *
 * Pikachu uses vanilla OOT movement (stick, ledge grab, swim, climb, etc.)
 * and just replaces Link's animations with Pikachu animations based on OOT's
 * movement state (speed, airborne, velY).  Sword / sword-charge / bow-charge
 * are blocked via the MmForm item restriction system (sSlotAllowedPikachu).
 *
 * Attacks are overlaid on top of vanilla movement: pressing A triggers an attack
 * animation and registers an AT ColliderCylinder for the active hitbox window.
 * OOT movement is frozen (PLAYER_STATE3_PAUSE_ACTION_FUNC) only during attacks.
 *
 * Draw: custom fast64 skeleton (Armature, 23 limbs) with segment-0x0D matrix
 * buffer approach. Hip-origin offset (+21) applied at draw to lift model to floor.
 */

#include <string.h>

#include "z64.h"
#include "macros.h"
#include "soh/frame_interpolation.h"
#include "functions.h"
#include "variables.h"
#include "mods/transformation_masks/transformation_masks.h"
#include "mods/extended_inventory.h"
#include "mods/items/custom_items.h"
#include "mods/actors/pikachu/pikachu_behavior.h"
#include "objects/gameplay_keep/gameplay_keep.h"

#include <libultraship/bridge.h>

// SkelAnime_GetFrameData is not declared in functions.h
extern "C" void SkelAnime_GetFrameData(AnimationHeader* animation, s32 frame, s32 limbCount, Vec3s* frameTable);

#ifndef GRAPH_ALLOC
#define GRAPH_ALLOC(gfxCtx, size) Graph_Alloc(gfxCtx, size)
#endif

// ── Pikachu local assets ──────────────────────────────────────────────────────
// clang-format off
#include "mods/actors/pikachu/pikachuDL.h"
#include "mods/actors/pikachu/pikachu_skel.h"
#include "mods/actors/pikachu/anims/pikachu_anims.h"

#include "mods/actors/pikachu/pikachuDL.c"
#include "mods/actors/pikachu/pikachu_skel.c"

#include "mods/actors/pikachu/anims/pikachu_idle.c"
#include "mods/actors/pikachu/anims/pikachu_landing.c"
#include "mods/actors/pikachu/anims/pikachu_knockback.c"
#include "mods/actors/pikachu/anims/pikachu_throw.c"
#include "mods/actors/pikachu/anims/pikachu_grab_pummel.c"
#include "mods/actors/pikachu/anims/pikachu_grab.c"
#include "mods/actors/pikachu/anims/pikachu_jab.c"
#include "mods/actors/pikachu/anims/pikachu_air_spin.c"
#include "mods/actors/pikachu/anims/pikachu_dash_attack.c"
#include "mods/actors/pikachu/anims/pikachu_run_loop.c"
#include "mods/actors/pikachu/anims/pikachu_run_start.c"
#include "mods/actors/pikachu/anims/pikachu_forward_smash.c"
#include "mods/actors/pikachu/anims/pikachu_up_tilt.c"
#include "mods/actors/pikachu/anims/pikachu_forward_tilt.c"
// clang-format on

// ── Macros ────────────────────────────────────────────────────────────────────

#define PIKA_CVAR "gMods.Pikachu.FormEnabled"
#define PIKACHU_FORM_SCALE 0.7f

// fast64 exports skeleton with origin at hip; OOT actor origin is at floor.
// Leg chain: thigh(7) + knee(10) + foot(4) = 21 units below hip.
// At 0.7 scale the effective leg length is ~14.7, so 15 sits feet on ground.
#define PIKA_HIP_HEIGHT 12.0f

// ── Attack types ──────────────────────────────────────────────────────────────

#define PIKA_ATTACK_NONE 0
#define PIKA_ATTACK_JAB 1        // A, grounded, no stick
#define PIKA_ATTACK_FTILT 2      // A, grounded, light stick
#define PIKA_ATTACK_FSMASH 3     // A, grounded, hard stick (tap)
#define PIKA_ATTACK_DASH 4       // A, grounded, running
#define PIKA_ATTACK_AIR 5        // A, airborne
#define PIKA_ATTACK_WHIP_GRAB 6  // Whip: wide forward grab → transitions to GRAB_HOLD
#define PIKA_ATTACK_THUNDER 7    // Din's/Demise: multi-phase float + AoE bomb
#define PIKA_ATTACK_QUICK_ATK 8  // Nayru's/Zonai: Quick Attack dash/teleport
#define PIKA_ATTACK_IRON_TAIL 9  // Farore's/Hylia's: metallic hammer (3 variants)
#define PIKA_ATTACK_SIDE_SPEC 10 // Roc's Cape on ground: forward burst
#define PIKA_ATTACK_GRAB_HOLD 11 // Active grab: pummel + throw (entered from WHIP_GRAB hit)
#define PIKA_ATTACK_THROW 12     // Post-grab throw animation (no hitbox)

// Per-attack damage flags (OOT dmgFlags bit masks)
#define PIKA_DMG_SWORD 0x00000700u  // DMG_SLASH_KOKIRI | MASTER | GIANT
#define PIKA_DMG_BOMB 0x00000008u   // DMG_EXPLOSIVE
#define PIKA_DMG_ARROW 0x00000024u  // DMG_ARROW_NORMAL | DMG_SLINGSHOT
#define PIKA_DMG_BOOM 0x00000010u   // DMG_BOOMERANG
#define PIKA_DMG_HAMMER 0x00000040u // DMG_HAMMER_SWING
#define PIKA_DMG_MAGIC 0x000A0000u  // DMG_MAGIC_FIRE | DMG_MAGIC_LIGHT

// Per-attack hitbox windows and parameters (start/end are inclusive frame numbers)
typedef struct {
    AnimationHeader* anim;
    s32 totalFrames;
    s32 hitStart; // first frame hitbox is active
    s32 hitEnd;   // last frame hitbox is active
    s32 cylRadius;
    s32 cylHeight;
    s32 cylYOff; // Y offset of cylinder bottom above actor origin
    s16 damage;
    f32 forwardOffset; // push collider this many units forward (0 = centered on player)
    u32 dmgFlags;      // OOT damage type bit mask
} PikaAttackDef;

static const PikaAttackDef sAttackDefs[] = {
    // 0: NONE
    { NULL, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0 },
    // 1: JAB — small hitbox, frames 2-4
    { &PikaJabAnim, PIKA_JAB_FRAMES, 2, 4, 20, 30, 30, 4, 0.0f, PIKA_DMG_SWORD },
    // 2: FTILT — forward tilt kick, frames 6-8 (Smash: startup F6)
    { &PikaForwardTiltAnim, PIKA_FORWARD_TILT_FRAMES, 6, 8, 25, 20, 10, 4, 0.0f, PIKA_DMG_BOOM },
    // 3: FSMASH — frames 15-20, big hitbox (Smash: startup F15)
    { &PikaForwardSmashAnim, PIKA_FORWARD_SMASH_FRAMES, 15, 20, 35, 40, 20, 8, 0.0f, PIKA_DMG_SWORD },
    // 4: DASH — frames 6-20 (Smash: startup F6, active window extended)
    { &PikaDashAttackAnim, PIKA_DASH_ATTACK_FRAMES, 6, 20, 30, 30, 20, 6, 0.0f, PIKA_DMG_SWORD },
    // 5: AIR (Nair) — 4-hit multi across 35 frames; hitbox managed manually (hitStart/hitEnd=999)
    { &PikaAirSpinAnim, 35, 999, 999, 25, 40, 30, 4, 0.0f, PIKA_DMG_SWORD },
    // 6: WHIP_GRAB — wide cylinder 60u forward, frames 3-15
    { &PikaGrabAnim, PIKA_GRAB_FRAMES, 3, 15, 55, 70, 10, 2, 60.0f, PIKA_DMG_SWORD },
    // 7: THUNDER — 38 frames, custom multi-phase anim (§E), AoE hitbox frames 8-22
    { &PikaRunStartAnim, 38, 8, 22, 90, 50, 5, 8, 0.0f, PIKA_DMG_BOMB | PIKA_DMG_HAMMER },
    // 8: QUICK_ATK — 20 frames, body AT every frame, custom dash logic
    { &PikaRunStartAnim, 20, 1, 18, 30, 60, 10, 4, 0.0f, PIKA_DMG_BOOM },
    // 9: IRON_TAIL — UpTilt anim, hammer, frames 8-16
    { &PikaUpTiltAnim, PIKA_UP_TILT_FRAMES, 8, 16, 35, 30, 10, 6, 30.0f, PIKA_DMG_HAMMER },
    // 10: SIDE_SPEC — RunStartAnim + forward burst, small hitbox
    { &PikaRunStartAnim, PIKA_RUN_START_FRAMES, 1, 3, 22, 30, 20, 4, 20.0f, PIKA_DMG_SWORD },
    // 11: GRAB_HOLD — hold pose (last GrabAnim frame), no AT; pummel handled separately
    { &PikaGrabAnim, PIKA_GRAB_FRAMES, 999, 999, 0, 0, 0, 0, 0.0f, 0 },
    // 12: THROW — post-grab throw, 12 frames, no hitbox
    { &PikaThrowAnim, PIKA_THROW_FRAMES, 999, 999, 0, 0, 0, 0, 0.0f, 0 },
};

// AT ColliderCylinder init (AT_ON, player-type)
static ColliderCylinderInit sAtCylInit = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_PLAYER,
        AC_NONE,
        OC1_NONE,
        OC2_NONE,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK0,
        { 0x00000000, 0x00, 0x00 },
        { 0x00000000, 0x00, 0x00 },
        TOUCH_ON | TOUCH_SFX_WOOD,
        BUMP_NONE,
        OCELEM_NONE,
    },
    { 20, 30, 0, { 0, 0, 0 } },
};

// ── State ─────────────────────────────────────────────────────────────────────

typedef struct {
    // Animation
    AnimationHeader* curAnim;
    s32 curAnimFrames;
    s32 animFrame;
    f32 animFrameF;

    // Joint table — filled by SkelAnime_GetFrameData every frame
    Vec3s jointTable[ARMATURE_NUM_LIMBS];

    // Attack state
    u8 attackType;   // PIKA_ATTACK_*
    s32 attackFrame; // current frame within attack animation

    // AT collider (active only during hitbox windows)
    ColliderCylinder atCyl;
    u8 colliderReady;

    // Roc's Cape 360° flip — active while airborne after Roc's use
    u8 rocFlipActive;
    s32 rocFlipFrame;

    // Shield bubble state
    u8 shieldActive;       // 1 = sphere is visible around Pikachu
    u8 shieldHP;           // 0..3; breaks when decremented to 0
    f32 shieldScale;       // 1.0 = full size, shrinks by 1/3 per press
    s32 shieldBubbleTimer; // used to throttle particle spawning (every 3 frames)

    // Knockback animation state
    u8 knockbackActive; // 1 = playing PikaKnockbackAnim
    s32 knockbackFrame;
    s32 postKnockbackTimer; // > 0 after recovery: idle runs at 25% speed

    // Attack Y-offset (used by Thunder float up/down)
    f32 attackYOff;

    // Quick Attack state
    u8 qatkPhase;       // 0=inactive, 1=dash1, 2=dash2, 3=returning(Z-target)
    s32 qatkTimer;      // dash duration countdown (frames)
    Vec3f qatkStartPos; // world pos before Z-target launch
    Actor* qatkTarget;  // Z-target actor pointer
    u8 qatkZTarget;     // 1 = was Z-targeting when activated

    // Iron Tail variant (determined at activation)
    u8 itailVariant; // 0=moving(forward), 1=neutral(downward), 2=Z-target

    // Grab hold (PIKA_ATTACK_GRAB_HOLD)
    Actor* grabbedActor; // enemy being held (NULL = not holding)
    s32 grabHoldTimer;
    s32 pummelFrame;
    u8 pummelActive; // 1 = pummel anim playing this frame

    // Jump backflip X-rotation accumulator
    s32 jumpFlipAngle;

    // Auto-blink
    s32 blinkTimer; // frames until next blink (counts down)
    s32 blinkFrame; // 0=not blinking, 1-6=blink in progress

    u8 initialized;
} PikachuFormState;

static PikachuFormState sPika;

// ── Helpers ───────────────────────────────────────────────────────────────────

// Read stick magnitude (0.0-1.0) from play->state.input[0]
static f32 Pika_StickMag(PlayState* play) {
    s8 x = play->state.input[0].cur.stick_x;
    s8 y = play->state.input[0].cur.stick_y;
    f32 mag = sqrtf((f32)(x * x + y * y));
    return (mag > 80.0f) ? 1.0f : mag / 80.0f;
}

// Impactrueno (Thunder): persistent storm darkening → growing electric field → bolt burst
// Electric effect runs the ENTIRE animation, starting small and growing outward.
// Scene stays dark (like spin charge / shadow medallion) through all phases.
static void Pika_SpawnThunderVfxPhased(PlayState* play, Player* player, s32 frame) {
    static Color_RGBA8 primYellow = { 255, 230, 50, 255 };
    static Color_RGBA8 envWhite = { 255, 255, 200, 255 };
    static Color_RGBA8 primBlue = { 100, 180, 255, 255 };
    static Color_RGBA8 envBlue = { 30, 80, 255, 200 };
    static Color_RGBA8 primWhite = { 255, 255, 255, 255 };
    static Color_RGBA8 envYellow = { 255, 220, 80, 255 };

    static const f32 cardinalAngles[4] = { 0.0f, 1.5708f, 3.14159f, 4.7124f };

    Vec3f base = player->actor.world.pos;
    Vec3f zero = { 0.0f, 0.0f, 0.0f };

    // Total attack length is 38 frames (from sAttackDefs[7].totalFrames)
    static const s32 totalFrames = 38;

    // ══ SCENE DARKENING — every frame, like spin attack charge ═══════════════
    // Frames 0-7: ramp up to full darkness
    // Frames 8-30: hold at moderate darkness (let the lightning illuminate)
    // Frames 31-37: fade out to normal
    {
        f32 intensity;
        if (frame < 8) {
            intensity = (f32)(frame + 1) / 8.0f * 0.85f; // 0.11 → 0.85
        } else if (frame <= 30) {
            intensity = 0.55f; // sustained darkness during discharge
        } else {
            // Fade out over frames 31-37
            intensity = 0.55f * (1.0f - (f32)(frame - 30) / 7.0f);
            if (intensity < 0.0f)
                intensity = 0.0f;
        }
        Environment_AdjustLights(play, intensity, 850.0f, 0.2f, 0.9f);
    }

    // Continuous electric buzz — plays every frame, auto-stops when we stop calling it
    func_8002F974(&player->actor, NA_SE_EN_BIRI_SPARK - SFX_FLAG);

    // ══ GROWING ELECTRIC AURA — every 2 frames, from frame 0 to end ═════════
    // Starts tiny (barely covering Pikachu), reaches full size by midpoint (~frame 19),
    // then HOLDS at full size for the entire second half.
    if ((frame % 2) == 0) {
        f32 t = (f32)frame / 19.0f; // reaches 1.0 at midpoint
        if (t > 1.0f)
            t = 1.0f;                  // clamp — stays at full size after midpoint
        f32 radius = 3.0f + t * 87.0f; // 3u (tiny) → 90u (full AoE)
        f32 rotOffset = (f32)frame * 0.4f;

        // Number of particles per ring grows: 3 at start → 12 at full
        s32 ringCount = 3 + (s32)(t * 9.0f);
        if (ringCount > 12)
            ringCount = 12;

        // Height layers: 1 at start → 3 at full size (second layer at t>=0.4, third at t>=0.7)
        s32 heightLayers = 1;
        if (t >= 0.4f)
            heightLayers = 2;
        if (t >= 0.7f)
            heightLayers = 3;
        f32 maxHeight = 5.0f + t * 55.0f; // vertical spread grows too

        for (s32 h = 0; h < heightLayers; h++) {
            f32 layerY = (heightLayers == 1) ? 15.0f : (5.0f + (f32)h * maxHeight / (f32)(heightLayers - 1));
            f32 layerRot = rotOffset * (h % 2 == 0 ? 1.0f : -1.0f);

            for (s32 i = 0; i < ringCount; i++) {
                f32 angle = (f32)i * (6.28318f / (f32)ringCount) + layerRot;
                f32 sx = Math_SinF(angle);
                f32 sz = Math_CosF(angle);

                Vec3f pos = { base.x + sx * radius, base.y + layerY, base.z + sz * radius };
                // Slow outward drift — stays near Pikachu like Nayru's Love aura
                Vec3f vel = { sx * 0.8f, 0.4f, sz * 0.8f };
                Vec3f accel = { sx * 0.1f, -0.08f, sz * 0.1f };

                Color_RGBA8* prim = (i % 3 == 0) ? &primYellow : &primBlue;
                Color_RGBA8* env = (i % 3 == 0) ? &envWhite : &envBlue;
                EffectSsKiraKira_SpawnFocused(play, &pos, &vel, &accel, prim, env, 520, 20);
            }
        }

        // Inner tight aura (Nayru's Love cylinder) — 8 sparkles close to Pikachu body
        if (frame >= 4) {
            f32 innerRadius = 3.0f + t * 39.0f; // 3u → 42u
            for (s32 i = 0; i < 8; i++) {
                f32 angle = (f32)i * (6.28318f / 8.0f) - rotOffset;
                f32 sx = Math_SinF(angle);
                f32 sz = Math_CosF(angle);
                Vec3f innerPos = { base.x + sx * innerRadius,
                                   base.y + 20.0f + Math_SinF((f32)frame * 0.3f + (f32)i) * 15.0f,
                                   base.z + sz * innerRadius };
                Vec3f vel = { sx * 0.4f, 1.5f, sz * 0.4f };
                Vec3f accel = { 0.0f, -0.1f, 0.0f };
                EffectSsKiraKira_SpawnFocused(play, &innerPos, &vel, &accel, &primBlue, &envWhite, 480, 18);
            }
        }

        // Lightning bolts radiating outward — grow with radius
        if (frame >= 6 && (frame % 4) == 0) {
            s32 boltCount = 2 + (s32)(t * 6.0f); // 2 → 8 bolts
            if (boltCount > 8)
                boltCount = 8;
            for (s32 i = 0; i < boltCount; i++) {
                f32 angle = (f32)i * (6.28318f / (f32)boltCount) + (f32)frame * 0.55f;
                Vec3f outPos = { base.x + Math_SinF(angle) * (radius * 0.6f), base.y + 10.0f + t * 20.0f,
                                 base.z + Math_CosF(angle) * (radius * 0.6f) };
                s32 boltScale = (s32)(80.0f + t * 60.0f);
                EffectSsLightning_Spawn(play, &outPos, &primBlue, &envBlue, boltScale, (s16)(i * 0x2000), 16, 3);
            }
        }
    }

    // ══ FRAME 8: BIG DISCHARGE BURST ═════════════════════════════════════════
    if (frame == 8) {
        // THICK CENTER BEAM: 6 large lightning bolts stacked vertically
        for (s32 h = 0; h < 6; h++) {
            Vec3f beamPos = { base.x, base.y + 20.0f + (f32)h * 40.0f, base.z };
            EffectSsLightning_Spawn(play, &beamPos, &primWhite, &envYellow, 400, (s16)(h * 0x1555), 12, 6);
        }

        // Sky ring: 10 bolts at high altitude
        for (s32 i = 0; i < 10; i++) {
            f32 angle = (f32)i * (6.28318f / 10.0f);
            Vec3f skyPos = { base.x + Math_SinF(angle) * 70.0f, base.y + 180.0f, base.z + Math_CosF(angle) * 70.0f };
            EffectSsLightning_Spawn(play, &skyPos, &primYellow, &envWhite, 180, (s16)(i * 0x1999), 8, 3);
        }

        // Cardinal bolts from all 4 sides at multiple heights
        for (s32 i = 0; i < 4; i++) {
            f32 sx = Math_SinF(cardinalAngles[i]);
            f32 sz = Math_CosF(cardinalAngles[i]);
            Vec3f midPos = { base.x + sx * 90.0f, base.y + 60.0f, base.z + sz * 90.0f };
            EffectSsLightning_Spawn(play, &midPos, &primYellow, &envWhite, 220, (s16)(i * 0x4000 + 0x2000), 14, 4);
            Vec3f lowPos = { base.x + sx * 60.0f, base.y + 15.0f, base.z + sz * 60.0f };
            EffectSsLightning_Spawn(play, &lowPos, &primBlue, &envBlue, 140, (s16)(i * 0x4000), 10, 2);
        }

        // Diagonal bolts at 45° gaps
        for (s32 i = 0; i < 4; i++) {
            f32 angle = (f32)i * (6.28318f / 4.0f) + 0.7854f;
            Vec3f diagPos = { base.x + Math_SinF(angle) * 80.0f, base.y + 100.0f, base.z + Math_CosF(angle) * 80.0f };
            EffectSsLightning_Spawn(play, &diagPos, &primYellow, &envYellow, 160, (s16)(i * 0x4000 + 0x4000), 10, 3);
        }

        // Ground shockwave + quake
        EffectSsBlast_SpawnWhiteShockwave(play, &base, &zero, &zero);
        s32 quakeIdx = Quake_Add(GET_ACTIVE_CAM(play), 3);
        Quake_SetSpeed(quakeIdx, 28000);
        Quake_SetQuakeValues(quakeIdx, 7, 0, 0, 0);
        Quake_SetCountdown(quakeIdx, 22);
    }

    // ══ SUSTAINED SIDE BOLTS (frames 10-30, every 4 frames) ═════════════════
    // Cardinal lightning to keep the field lit from all directions
    if (frame >= 10 && frame <= 30 && (frame % 4) == 0) {
        for (s32 i = 0; i < 4; i++) {
            f32 sx = Math_SinF(cardinalAngles[i]);
            f32 sz = Math_CosF(cardinalAngles[i]);
            Vec3f sidePos = { base.x + sx * 55.0f, base.y + 35.0f, base.z + sz * 55.0f };
            EffectSsLightning_Spawn(play, &sidePos, &primYellow, &envBlue, 100,
                                    (s16)(i * 0x4000 + (s16)(frame * 0x500)), 14, 2);
        }
    }
}

// Spawn blue EffectSsBlast at position in front of player (forward smash VFX)
static void Pika_SpawnBlastVfx(PlayState* play, Player* player) {
    static Color_RGBA8 primColor = { 100, 160, 255, 200 };
    static Color_RGBA8 envColor = { 20, 80, 255, 150 };

    Vec3f pos;
    Vec3f vel = { 0.0f, 0.0f, 0.0f };
    Vec3f accel = { 0.0f, 0.0f, 0.0f };

    f32 yaw = (f32)player->actor.world.rot.y * (3.14159f / 32768.0f);
    pos.x = player->actor.world.pos.x + sinf(yaw) * 40.0f;
    pos.y = player->actor.world.pos.y + 30.0f;
    pos.z = player->actor.world.pos.z + cosf(yaw) * 40.0f;

    EffectSsBlast_Spawn(play, &pos, &vel, &accel, &primColor, &envColor, 100, -5, 3, 8);
}

// Draw shield bubble: 8 particles in an XZ ring + 2 vertical poles around Pikachu.
// Called every 3 frames to maintain a persistent translucent sphere look.
static void Pika_DrawShieldBubble(PlayState* play, Player* player) {
    static Color_RGBA8 primColor = { 160, 220, 255, 180 };
    static Color_RGBA8 envColor = { 60, 120, 255, 120 };
    Vec3f zero = { 0.0f, 0.0f, 0.0f };

    sPika.shieldBubbleTimer++;
    if (sPika.shieldBubbleTimer < 3)
        return;
    sPika.shieldBubbleTimer = 0;

    f32 r = sPika.shieldScale * 35.0f;
    Vec3f center = player->actor.world.pos;
    center.y += 30.0f; // center of Pikachu's body

    // 8 equally-spaced particles in XZ ring
    for (s32 i = 0; i < 8; i++) {
        f32 angle = (f32)i * (2.0f * 3.14159f / 8.0f);
        Vec3f p = { center.x + sinf(angle) * r, center.y, center.z + cosf(angle) * r };
        EffectSsBlast_Spawn(play, &p, &zero, &zero, &primColor, &envColor, 4, -2, 1, 4);
    }
    // Top and bottom poles
    Vec3f top = { center.x, center.y + r, center.z };
    Vec3f bot = { center.x, center.y - r, center.z };
    EffectSsBlast_Spawn(play, &top, &zero, &zero, &primColor, &envColor, 4, -2, 1, 4);
    EffectSsBlast_Spawn(play, &bot, &zero, &zero, &primColor, &envColor, 4, -2, 1, 4);
}

// Break the shield: icy burst VFX + 3 hearts damage + knockback impulse.
static void Pika_BreakShield(PlayState* play, Player* player) {
    static Color_RGBA8 primIce = { 200, 240, 255, 255 };
    static Color_RGBA8 envIce = { 100, 200, 255, 200 };
    Vec3f zero = { 0.0f, 0.0f, 0.0f };

    Vec3f center = player->actor.world.pos;
    center.y += 30.0f;

    // Ring of ice-shard blasts
    for (s32 i = 0; i < 12; i++) {
        f32 angle = (f32)i * (2.0f * 3.14159f / 12.0f);
        f32 r = 35.0f;
        Vec3f p = { center.x + sinf(angle) * r, center.y, center.z + cosf(angle) * r };
        Vec3f vel = { sinf(angle) * 3.0f, 1.0f, cosf(angle) * 3.0f };
        EffectSsBlast_Spawn(play, &p, &vel, &zero, &primIce, &envIce, 80, -5, 2, 8);
    }
    // Top burst
    Vec3f top = { center.x, center.y + 35.0f, center.z };
    EffectSsBlast_SpawnWhiteShockwave(play, &top, &zero, &zero);

    // Damage: 3 hearts
    Health_ChangeBy(play, -(3 * FULL_HEART_HEALTH));

    // Knockback impulse: fly backward + upward
    f32 yaw = (f32)player->actor.world.rot.y * (3.14159f / 32768.0f);
    player->actor.velocity.y = 10.0f;
    player->actor.velocity.x = -sinf(yaw) * 6.0f;
    player->actor.velocity.z = -cosf(yaw) * 6.0f;

    // Clear shield
    sPika.shieldActive = 0;
    sPika.shieldHP = 0;
    sPika.shieldScale = 0.0f;
}

// ── Per-form C-button item handlers ───────────────────────────────────────────
// Called by TransformMasks_HandleFormItemUse in mm_player_form.cpp.
// Return 1 → block Player_UseItem (we handle it ourselves).
// Return 0 → pass through to Player_UseItem (OOT handles it).

// Generic pass-through — OOT handles the item normally.
extern "C" u8 PikaItem_PassThrough(PlayState* play, Player* player, s32 item) {
    (void)play;
    (void)player;
    (void)item;
    return 0;
}

// Sword: always blocked — OOT sword swing animations look wrong on Pikachu skeleton.
extern "C" u8 PikaItem_BlockSword(PlayState* play, Player* player, s32 item) {
    (void)play;
    (void)player;
    (void)item;
    return 1;
}

// Shield: activate a semi-transparent bubble sphere. Each press shrinks it.
// On third press (HP=0) the shield shatters: icy VFX + 3 hearts damage + knockback.
extern "C" u8 PikaItem_Shield(PlayState* play, Player* player, s32 item) {
    (void)item;
    if (!sPika.initialized)
        return 1;
    if (!sPika.shieldActive) {
        // First press: activate at full size
        sPika.shieldActive = 1;
        sPika.shieldHP = 3;
        sPika.shieldScale = 1.0f;
        sPika.shieldBubbleTimer = 0;
    } else {
        // Subsequent press: shrink
        if (sPika.shieldHP > 0)
            sPika.shieldHP--;
        sPika.shieldScale = (sPika.shieldHP > 0) ? ((f32)sPika.shieldHP / 3.0f) : 0.0f;
        if (sPika.shieldHP == 0) {
            Pika_BreakShield(play, player);
        }
    }
    return 1;
}

// Quick Attack (Nayru's Love / Zonai Permafrost):
//   No Z-target: 2-dash system — press again during phase 1 for second dash in new direction.
//   Z-target (focusActor set): Pikachu dashes invisibly toward target, then teleports back.
extern "C" u8 PikaItem_QuickAtk(PlayState* play, Player* player, s32 item) {
    (void)item;
    if (!sPika.initialized)
        return 1;

    // If already in phase 1 (first dash, no Z-target), activate second dash in new direction
    if (sPika.attackType == PIKA_ATTACK_QUICK_ATK && sPika.qatkPhase == 1 && !sPika.qatkZTarget) {
        sPika.qatkPhase = 2;
        sPika.qatkTimer = 20;
        s8 sx = play->state.input[0].cur.stick_x;
        s8 sy = play->state.input[0].cur.stick_y;
        if ((s32)(sx * sx + sy * sy) > 100) {
            f32 yaw2 = (f32)player->actor.world.rot.y * (3.14159f / 32768.0f);
            player->actor.velocity.x = sinf(yaw2) * 20.0f;
            player->actor.velocity.z = cosf(yaw2) * 20.0f;
        } else {
            player->actor.velocity.y = 20.0f;
        }
        return 1;
    }

    if (sPika.attackType != PIKA_ATTACK_NONE)
        return 1;

    if (player->focusActor != NULL) {
        // Z-target mode: dash invisibly toward locked enemy, then teleport back
        sPika.qatkZTarget = 1;
        sPika.qatkStartPos = player->actor.world.pos;
        sPika.qatkTarget = player->focusActor;
        sPika.qatkPhase = 1;
        sPika.qatkTimer = 15;
    } else {
        // Normal mode: dash in facing direction (or up if neutral stick)
        sPika.qatkZTarget = 0;
        sPika.qatkPhase = 1;
        sPika.qatkTimer = 20;
        s8 sx = play->state.input[0].cur.stick_x;
        s8 sy = play->state.input[0].cur.stick_y;
        if ((s32)(sx * sx + sy * sy) > 100) {
            f32 yaw2 = (f32)player->actor.world.rot.y * (3.14159f / 32768.0f);
            player->actor.velocity.x = sinf(yaw2) * 20.0f;
            player->actor.velocity.z = cosf(yaw2) * 20.0f;
        } else {
            player->actor.velocity.y = 20.0f;
        }
    }
    sPika.attackType = PIKA_ATTACK_QUICK_ATK;
    sPika.attackFrame = 0;
    player->actor.shape.rot.y = player->actor.world.rot.y;
    return 1;
}

// Iron Tail (Farore's Wind / Hylia's Grace): metallic hammer strike, 3 variants.
//   Z-target (focusActor set): hammer toward target (variant 2).
//   Neutral stick (speed < 1): downward strike — UpTilt played backwards (variant 1).
//   Moving: upward swing — UpTilt played forward (variant 0).
extern "C" u8 PikaItem_IronTail(PlayState* play, Player* player, s32 item) {
    (void)play;
    (void)item;
    if (!sPika.initialized)
        return 1;
    if (sPika.attackType != PIKA_ATTACK_NONE)
        return 1;

    if (player->focusActor != NULL) {
        sPika.itailVariant = 2;
    } else if (player->linearVelocity < 1.0f) {
        sPika.itailVariant = 1;
    } else {
        sPika.itailVariant = 0;
    }
    sPika.attackType = PIKA_ATTACK_IRON_TAIL;
    sPika.attackFrame = 0;
    player->actor.shape.rot.y = player->actor.world.rot.y;
    return 1;
}

// Forward Tilt via Boomerang:
//   Z-targeting an enemy within 40 game units → dash toward enemy + FTILT attack.
//   Otherwise: pass through to OOT (normal boomerang throw).
extern "C" u8 PikaItem_ForwardTilt(PlayState* play, Player* player, s32 item) {
    (void)play;
    (void)item;
    if (!sPika.initialized)
        return 0;
    if (sPika.attackType != PIKA_ATTACK_NONE)
        return 1;

    if (player->focusActor == NULL)
        return 0;

    f32 dx = player->focusActor->world.pos.x - player->actor.world.pos.x;
    f32 dz = player->focusActor->world.pos.z - player->actor.world.pos.z;
    f32 distSq = dx * dx + dz * dz;
    if (distSq > 40.0f * 40.0f)
        return 0;

    player->actor.world.rot.y = Math_Atan2S(dx, dz);
    player->actor.shape.rot.y = player->actor.world.rot.y;

    f32 yaw = (f32)player->actor.world.rot.y * (3.14159f / 32768.0f);
    f32 dashSpeed = 12.0f;
    player->actor.velocity.x = sinf(yaw) * dashSpeed;
    player->actor.velocity.z = cosf(yaw) * dashSpeed;
    player->linearVelocity = dashSpeed;

    sPika.attackType = PIKA_ATTACK_FTILT;
    sPika.attackFrame = 0;
    return 1;
}

// Roc's Cape / Roc's Feather:
//   Grounded → Side special: RunStartAnim + forward velocity burst (Skull Bash style).
//   Airborne → 360° air-spin flip (existing behavior).
// The jump velocity for the air case is applied by Handle_RocsCape() independently.
extern "C" u8 PikaItem_RocsCape(PlayState* play, Player* player, s32 item) {
    (void)play;
    (void)item;
    if (!sPika.initialized)
        return 1;
    u8 onGround = (player->actor.bgCheckFlags & 1) != 0;
    if (onGround && sPika.attackType == PIKA_ATTACK_NONE) {
        // Ground: side special — RunStartAnim + forward burst
        sPika.attackType = PIKA_ATTACK_SIDE_SPEC;
        sPika.attackFrame = 0;
        player->actor.shape.rot.y = player->actor.world.rot.y;
    } else {
        // Air: double-jump boost — backflip is handled by jumpFlipAngle system
        player->actor.velocity.y = 12.0f; // boosted second jump (OOT default ≈ 8)
    }
    return 1; // Block Player_UseItem — prevents OOT's held-item (cape-over-head) animation
}

// Spawn two blue Thunder Jolt blasts forward from player (used on ground by attack SM and in air directly).
static void Pika_FireThunderJolt(PlayState* play, Player* player) {
    static Color_RGBA8 primColor = { 120, 180, 255, 255 };
    static Color_RGBA8 envColor = { 30, 80, 255, 200 };

    Vec3f pos;
    Vec3f vel = { 0.0f, 0.0f, 0.0f };
    Vec3f accel = { 0.0f, 0.0f, 0.0f };

    f32 yaw = (f32)player->actor.world.rot.y * (3.14159f / 32768.0f);
    pos.x = player->actor.world.pos.x + sinf(yaw) * 30.0f;
    pos.y = player->actor.world.pos.y + 30.0f;
    pos.z = player->actor.world.pos.z + cosf(yaw) * 30.0f;

    vel.x = sinf(yaw) * 8.0f;
    vel.z = cosf(yaw) * 8.0f;

    EffectSsBlast_Spawn(play, &pos, &vel, &accel, &primColor, &envColor, 80, -4, 2, 10);
    pos.y += 10.0f;
    EffectSsBlast_Spawn(play, &pos, &vel, &accel, &primColor, &envColor, 60, -4, 2, 8);
}

// Thunder Jolt (Bow): fire two blue blasts immediately in facing direction.
// Blocks Player_UseItem so no OOT arrow is spawned.
extern "C" u8 PikaItem_ThunderJolt(PlayState* play, Player* player, s32 item) {
    (void)item;
    if (!sPika.initialized)
        return 1;
    Pika_FireThunderJolt(play, player);
    return 1;
}

// Whip: play grab animation with wide forward AT cylinder; hit enemies are pulled toward Pikachu.
// Blocks Player_UseItem to prevent OOT's whip swing animation on Link's skeleton.
extern "C" u8 PikaItem_WhipGrab(PlayState* play, Player* player, s32 item) {
    (void)play;
    (void)player;
    (void)item;
    if (!sPika.initialized)
        return 1;
    if (sPika.attackType == PIKA_ATTACK_NONE) {
        sPika.attackType = PIKA_ATTACK_WHIP_GRAB;
        sPika.attackFrame = 0;
        player->actor.shape.rot.y = player->actor.world.rot.y;
    }
    return 1;
}

// Din's Fire / Demise's Destruction: Impactrueno (Thunder).
// Pikachu raises arm, lightning column crashes down + ground shockwave.
// Blocks Player_UseItem so no fire orb / Demise DL is spawned.
extern "C" u8 PikaItem_Thunder(PlayState* play, Player* player, s32 item) {
    (void)play;
    (void)player;
    (void)item;
    if (!sPika.initialized)
        return 1;
    if (sPika.attackType == PIKA_ATTACK_NONE) {
        sPika.attackType = PIKA_ATTACK_THUNDER;
        sPika.attackFrame = 0;
        player->actor.shape.rot.y = player->actor.world.rot.y;
    }
    return 1;
}

// ── Public API ────────────────────────────────────────────────────────────────

extern "C" u8 PikachuForm_IsEnabled(void) {
    return CVarGetInteger(PIKA_CVAR, 0) != 0;
}

extern "C" u8 PikachuForm_LoadSkeleton(PlayState* play) {
    memset(&sPika, 0, sizeof(sPika));
    sPika.initialized = 1;
    sPika.curAnim = &PikaIdleAnim;
    sPika.curAnimFrames = PIKA_IDLE_FRAMES;

    // Fill joint table with idle frame 0 so first draw has valid data
    SkelAnime_GetFrameData(&PikaIdleAnim, 0, 23, sPika.jointTable);
    sPika.blinkTimer = 240; // first blink after ~4 seconds

    // Init AT collider
    Collider_InitCylinder(play, &sPika.atCyl);
    Collider_SetCylinder(play, &sPika.atCyl, NULL, &sAtCylInit);
    sPika.colliderReady = 1;

    return 1; // Always succeeds — no mm.o2r needed
}

extern "C" void PikachuForm_Cleanup(void) {
    sPika.initialized = 0;
    sPika.colliderReady = 0;
    // Note: we don't have a PlayState here, so we skip Collider_DestroyCylinder.
    // The collider is fully static (no heap allocs), so it's safe to just zero the state.
}

// ─────────────────────────────────────────────────────────────────────────────
// Update — vanilla OOT movement, Pikachu animation selected by movement state,
//          attacks overlaid on top when A is pressed.
// ─────────────────────────────────────────────────────────────────────────────

extern "C" void PikachuForm_Update(Player* player, PlayState* play) {
    if (!sPika.initialized)
        return;

    // Update body part positions EVERY frame (before Navi/camera reads them).
    // Must be in Update, not Draw, because Draw has early-return paths (damage flicker,
    // Quick Attack invisibility) that would leave stale positions.
    for (s32 i = 0; i < PLAYER_BODYPART_MAX; i++) {
        player->bodyPartsPos[i].x = player->actor.world.pos.x;
        player->bodyPartsPos[i].y = player->actor.world.pos.y + 25.0f;
        player->bodyPartsPos[i].z = player->actor.world.pos.z;
    }
    player->bodyPartsPos[PLAYER_BODYPART_L_FOOT].y = player->actor.world.pos.y;
    player->bodyPartsPos[PLAYER_BODYPART_R_FOOT].y = player->actor.world.pos.y;
    player->bodyPartsPos[PLAYER_BODYPART_HEAD].y = player->actor.world.pos.y + 50.0f;
    player->bodyPartsPos[PLAYER_BODYPART_HAT].y = player->actor.world.pos.y + 50.0f;

    // Foot shadow positions (ActorShadow_DrawFeet reads shape.feetPos[])
    player->actor.shape.feetPos[0].x = player->actor.world.pos.x;
    player->actor.shape.feetPos[0].y = player->actor.world.pos.y;
    player->actor.shape.feetPos[0].z = player->actor.world.pos.z;
    player->actor.shape.feetPos[1].x = player->actor.world.pos.x;
    player->actor.shape.feetPos[1].y = player->actor.world.pos.y;
    player->actor.shape.feetPos[1].z = player->actor.world.pos.z;

    // OOT handles gravity, movement, rotation, collision — nothing overridden here
    // (unless we're in an attack, in which case we freeze the action function).

    u8 onGround = (player->actor.bgCheckFlags & 1) != 0; // bit 0 = BGCHECKFLAG_GROUND
    f32 speed = player->linearVelocity;
    f32 velY = player->actor.velocity.y;
    u8 aPressed = CHECK_BTN_ALL(play->state.input[0].press.button, BTN_A) != 0;

    // Slow-fall: add upward counter-impulse to offset half of OOT's -1.0/frame gravity.
    // Net fall rate ≈ -0.5/frame, cap terminal velocity at -12 (Pikachu = floaty slow faller).
    if (!onGround) {
        player->actor.velocity.y += 0.5f;
        if (player->actor.velocity.y < -12.0f)
            player->actor.velocity.y = -12.0f;
    }

    // Auto-blink
    if (sPika.blinkFrame > 0) {
        sPika.blinkFrame++;
        if (sPika.blinkFrame > 6)
            sPika.blinkFrame = 0;
    } else {
        sPika.blinkTimer--;
        if (sPika.blinkTimer <= 0) {
            sPika.blinkFrame = 1;
            sPika.blinkTimer = 180 + (s32)(play->gameplayFrames % 300); // 3-8 sec
        }
    }

    // ── Shield bubble visual (drawn every frame when active) ──────────────────
    if (sPika.shieldActive) {
        Pika_DrawShieldBubble(play, player);
    }

    // ── Knockback override — highest priority, overrides attack/flip/movement ──
    // Detect when OOT puts Pikachu in a damage/knockback state.
    if (player->stateFlags1 & PLAYER_STATE1_DAMAGED) {
        if (!sPika.knockbackActive) {
            sPika.knockbackActive = 1;
            sPika.knockbackFrame = 0;
            sPika.postKnockbackTimer = 90; // ~3 seconds of slowed idle after recovery
            // Clear any ongoing attack so we don't resume it after damage
            sPika.attackType = PIKA_ATTACK_NONE;
            sPika.attackFrame = 0;
            Environment_AdjustLights(play, 0.0f, 850.0f, 0.2f, 0.0f); // restore lighting if Thunder interrupted
        }
    }

    if (sPika.knockbackActive) {
        sPika.knockbackFrame++;
        s32 kf = sPika.knockbackFrame;
        if (kf >= PIKA_KNOCKBACK_FRAMES)
            kf = PIKA_KNOCKBACK_FRAMES - 1; // hold last frame
        SkelAnime_GetFrameData(&PikaKnockbackAnim, kf, 23, sPika.jointTable);
        player->actor.shape.rot.y = player->actor.world.rot.y;
        if (sPika.knockbackFrame >= PIKA_KNOCKBACK_FRAMES) {
            sPika.knockbackActive = 0;
        }
        return; // Skip attack / flip / movement anim this frame
    }

    // ── Attack state machine ──────────────────────────────────────────────────

    if (sPika.attackType != PIKA_ATTACK_NONE) {
        // Currently in an attack — advance frame, check hitbox window, end when done
        const PikaAttackDef& def = sAttackDefs[sPika.attackType];

        sPika.attackFrame++;

        // Freeze OOT's action function so player doesn't interrupt the attack
        player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;

        // JAB: don't freeze movement; allow chain into new jab at frame 7 (Smash mash system)
        if (sPika.attackType == PIKA_ATTACK_JAB) {
            player->stateFlags3 &= ~PLAYER_STATE3_PAUSE_ACTION_FUNC;
            if (aPressed && sPika.attackFrame >= 7) {
                sPika.attackFrame = 0; // restart jab immediately
            }
        }
        // DASH: commit to direction on frame 1, apply momentum burst; decelerate after frame 10
        if (sPika.attackType == PIKA_ATTACK_DASH && sPika.attackFrame == 1) {
            f32 dashYaw = (f32)player->actor.world.rot.y * (3.14159f / 32768.0f);
            player->actor.velocity.x = sinf(dashYaw) * 10.0f;
            player->actor.velocity.z = cosf(dashYaw) * 10.0f;
            player->actor.velocity.y = 0.0f;
        }
        if (sPika.attackType == PIKA_ATTACK_DASH && sPika.attackFrame > 10) {
            player->actor.velocity.x *= 0.82f;
            player->actor.velocity.z *= 0.82f;
            player->linearVelocity *= 0.82f;
        }

        // Freeze XZ movement during spell/grounded attacks so Pikachu stays in place
        if (sPika.attackType == PIKA_ATTACK_FSMASH || sPika.attackType == PIKA_ATTACK_THUNDER ||
            sPika.attackType == PIKA_ATTACK_IRON_TAIL || sPika.attackType == PIKA_ATTACK_WHIP_GRAB ||
            sPika.attackType == PIKA_ATTACK_THROW) {
            player->actor.velocity.x = 0.0f;
            player->actor.velocity.z = 0.0f;
            player->linearVelocity = 0.0f;
        }

        // Sample attack animation — with per-type overrides
        s32 clampedFrame = sPika.attackFrame;
        if (clampedFrame >= def.totalFrames)
            clampedFrame = def.totalFrames - 1;

        if (sPika.attackType == PIKA_ATTACK_THUNDER) {
            // Multi-phase: RunStart fwd (0-3) → RunStart bk (4-7) → Idle+float (8+)
            s32 f = sPika.attackFrame;
            if (f <= 3) {
                SkelAnime_GetFrameData(&PikaRunStartAnim, f, 23, sPika.jointTable);
            } else if (f <= 7) {
                s32 backF = 3 - (f - 4);
                if (backF < 0)
                    backF = 0;
                SkelAnime_GetFrameData(&PikaRunStartAnim, backF, 23, sPika.jointTable);
            } else {
                s32 idleF = (f - 8) % PIKA_IDLE_FRAMES;
                SkelAnime_GetFrameData(&PikaIdleAnim, idleF, 23, sPika.jointTable);
                // Float up (frames 8-22) then descend (frames 23-37)
                if (f <= 22) {
                    sPika.attackYOff = ((f - 8) / 14.0f) * 5.0f;
                } else {
                    sPika.attackYOff = (1.0f - (f - 23) / 14.0f) * 5.0f;
                    if (sPika.attackYOff < 0.0f)
                        sPika.attackYOff = 0.0f;
                }
            }
        } else if (sPika.attackType == PIKA_ATTACK_IRON_TAIL && sPika.itailVariant == 1) {
            // Variant 1 (neutral/downward): play UpTilt BACKWARDS
            s32 backF = (PIKA_UP_TILT_FRAMES - 1) - clampedFrame;
            if (backF < 0)
                backF = 0;
            SkelAnime_GetFrameData(&PikaUpTiltAnim, backF, 23, sPika.jointTable);
        } else if (sPika.attackType == PIKA_ATTACK_GRAB_HOLD) {
            // Hold pose: last frame of GrabAnim; pummel overrides when active
            if (sPika.pummelActive) {
                s32 pf = sPika.pummelFrame;
                if (pf >= PIKA_GRAB_PUMMEL_FRAMES)
                    pf = PIKA_GRAB_PUMMEL_FRAMES - 1;
                SkelAnime_GetFrameData(&PikaGrabPummelAnim, pf, 23, sPika.jointTable);
            } else {
                SkelAnime_GetFrameData(&PikaGrabAnim, PIKA_GRAB_FRAMES - 1, 23, sPika.jointTable);
            }
        } else if (sPika.attackType == PIKA_ATTACK_AIR) {
            // Nair: 4-hit multi, loop the 7-frame anim across 35 total frames
            s32 nairLoopF = sPika.attackFrame % PIKA_AIR_SPIN_FRAMES;
            SkelAnime_GetFrameData(&PikaAirSpinAnim, nairLoopF, 23, sPika.jointTable);
        } else {
            SkelAnime_GetFrameData(def.anim, clampedFrame, 23, sPika.jointTable);
        }

        // Thunder VFX: runs EVERY frame of the attack (not just hitbox window)
        if (sPika.attackType == PIKA_ATTACK_THUNDER) {
            Pika_SpawnThunderVfxPhased(play, player, sPika.attackFrame);

            // King Dodongo special: spawn bomb at his feet (once, on frame 8)
            if (sPika.attackFrame == 8) {
                Actor* a = play->actorCtx.actorLists[ACTORCAT_BOSS].head;
                while (a != NULL) {
                    if (a->id == ACTOR_BOSS_DODONGO) {
                        f32 dist = Actor_WorldDistXYZToActor(&player->actor, a);
                        if (dist < 250.0f) {
                            Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOM, a->world.pos.x, a->world.pos.y,
                                        a->world.pos.z, 0, 0, 0, 0, 0);
                        }
                    }
                    a = a->next;
                }
            }
        }

        // Hitbox window
        bool hitboxOn = (sPika.attackFrame >= def.hitStart && sPika.attackFrame <= def.hitEnd);
        // Nair: 4 manual hit windows at F3-6, F9-12, F15-18, F21-22
        if (sPika.attackType == PIKA_ATTACK_AIR) {
            s32 af = sPika.attackFrame;
            hitboxOn =
                (af >= 3 && af <= 6) || (af >= 9 && af <= 12) || (af >= 15 && af <= 18) || (af >= 21 && af <= 22);
        }

        if (hitboxOn && sPika.colliderReady) {
            // Update collider dimensions from attack def
            sPika.atCyl.dim.radius = def.cylRadius;
            sPika.atCyl.dim.height = def.cylHeight;
            sPika.atCyl.dim.yShift = def.cylYOff;

            // Damage flags from per-attack def
            sPika.atCyl.info.toucher.dmgFlags = def.dmgFlags;
            sPika.atCyl.info.toucher.damage = (u8)def.damage;
            sPika.atCyl.base.actor = &player->actor;
            // Thunder: AT_TYPE_ALL so bomb-breakable walls/crates respond (they check AT_TYPE_OTHER)
            // All other attacks: AT_TYPE_PLAYER
            sPika.atCyl.base.atFlags =
                (sPika.attackType == PIKA_ATTACK_THUNDER) ? (AT_ON | AT_TYPE_ALL) : (AT_ON | AT_TYPE_PLAYER);
            // Iron Tail: hard (metallic) hit sound; all others use wood
            sPika.atCyl.info.toucherFlags =
                (sPika.attackType == PIKA_ATTACK_IRON_TAIL) ? (TOUCH_ON | TOUCH_SFX_HARD) : (TOUCH_ON | TOUCH_SFX_WOOD);

            // Position: center on player, then shift forward if forwardOffset > 0
            Collider_UpdateCylinder(&player->actor, &sPika.atCyl);
            if (def.forwardOffset > 0.0f) {
                f32 yaw = (f32)player->actor.world.rot.y * (3.14159f / 32768.0f);
                Vec3s fwdPos = { (s16)(player->actor.world.pos.x + sinf(yaw) * def.forwardOffset),
                                 (s16)player->actor.world.pos.y,
                                 (s16)(player->actor.world.pos.z + cosf(yaw) * def.forwardOffset) };
                Collider_SetCylinderPosition(&sPika.atCyl, &fwdPos);
            }

            CollisionCheck_SetAT(play, &play->colChkCtx, &sPika.atCyl.base);

            // ── Per-attack VFX / special effects at hitbox open ────────────────
            if (sPika.attackFrame == def.hitStart) {
                if (sPika.attackType == PIKA_ATTACK_FSMASH) {
                    Pika_SpawnBlastVfx(play, player);
                    Pika_SpawnBlastVfx(play, player);
                }
            }

            // ── Side special: apply forward velocity burst on frame 1 ─────────
            if (sPika.attackType == PIKA_ATTACK_SIDE_SPEC && sPika.attackFrame == 1) {
                f32 yaw = (f32)player->actor.world.rot.y * (3.14159f / 32768.0f);
                player->actor.velocity.x += sinf(yaw) * 7.0f;
                player->actor.velocity.z += cosf(yaw) * 7.0f;
            }

            // ── Whip grab: on first AT hit, transition to GRAB_HOLD ──────────
            if (sPika.attackType == PIKA_ATTACK_WHIP_GRAB && (sPika.atCyl.base.atFlags & AT_HIT) &&
                sPika.atCyl.base.at != NULL) {
                Actor* hitActor = sPika.atCyl.base.at;
                if (hitActor != NULL) {
                    sPika.grabbedActor = hitActor;
                    sPika.grabHoldTimer = 300;
                    sPika.pummelActive = 0;
                    sPika.pummelFrame = 0;
                    sPika.attackType = PIKA_ATTACK_GRAB_HOLD;
                    sPika.attackFrame = 0;
                    sPika.atCyl.base.atFlags &= ~AT_HIT;
                }
            }
        }

        // ── GRAB_HOLD: lock enemy, pummel on A, auto-throw on timer ──────────
        if (sPika.attackType == PIKA_ATTACK_GRAB_HOLD) {
            if (sPika.grabbedActor != NULL) {
                f32 ghYaw = (f32)player->actor.world.rot.y * (3.14159f / 32768.0f);
                sPika.grabbedActor->world.pos.x = player->actor.world.pos.x + sinf(ghYaw) * 40.0f;
                sPika.grabbedActor->world.pos.y = player->actor.world.pos.y + 20.0f;
                sPika.grabbedActor->world.pos.z = player->actor.world.pos.z + cosf(ghYaw) * 40.0f;
                sPika.grabbedActor->velocity.x = 0.0f;
                sPika.grabbedActor->velocity.y = 0.0f;
                sPika.grabbedActor->velocity.z = 0.0f;
            }
            // A press → start pummel
            if (aPressed && !sPika.pummelActive) {
                sPika.pummelActive = 1;
                sPika.pummelFrame = 0;
            }
            // Advance pummel anim
            if (sPika.pummelActive) {
                sPika.pummelFrame++;
                // Midpoint: deal 1 hit to grabbed actor
                if (sPika.pummelFrame == 5 && sPika.grabbedActor != NULL && sPika.colliderReady) {
                    sPika.atCyl.dim.radius = 15;
                    sPika.atCyl.dim.height = 30;
                    sPika.atCyl.dim.yShift = 10;
                    sPika.atCyl.info.toucher.dmgFlags = PIKA_DMG_SWORD;
                    sPika.atCyl.info.toucher.damage = 2;
                    sPika.atCyl.info.toucherFlags = TOUCH_ON | TOUCH_SFX_WOOD;
                    sPika.atCyl.base.actor = &player->actor;
                    Vec3s pummelPos = { (s16)sPika.grabbedActor->world.pos.x, (s16)sPika.grabbedActor->world.pos.y,
                                        (s16)sPika.grabbedActor->world.pos.z };
                    Collider_SetCylinderPosition(&sPika.atCyl, &pummelPos);
                    CollisionCheck_SetAT(play, &play->colChkCtx, &sPika.atCyl.base);
                }
                if (sPika.pummelFrame >= PIKA_GRAB_PUMMEL_FRAMES) {
                    sPika.pummelActive = 0;
                }
            }
            // Timer → throw
            sPika.grabHoldTimer--;
            if (sPika.grabHoldTimer <= 0) {
                if (sPika.grabbedActor != NULL) {
                    f32 ghYaw = (f32)player->actor.world.rot.y * (3.14159f / 32768.0f);
                    sPika.grabbedActor->velocity.x = sinf(ghYaw) * 12.0f;
                    sPika.grabbedActor->velocity.y = 8.0f;
                    sPika.grabbedActor->velocity.z = cosf(ghYaw) * 12.0f;
                    sPika.grabbedActor = NULL;
                }
                sPika.attackType = PIKA_ATTACK_THROW;
                sPika.attackFrame = 0;
            }
            player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
            player->actor.shape.rot.y = player->actor.world.rot.y;
            return;
        }

        // ── QUICK_ATK: apply dash velocity each frame + electric particles ────
        if (sPika.attackType == PIKA_ATTACK_QUICK_ATK) {
            if (sPika.qatkZTarget && sPika.qatkTarget != NULL) {
                // Z-target: dash toward target invisibly
                Vec3f* tp = &sPika.qatkTarget->world.pos;
                Vec3f* pp = &player->actor.world.pos;
                f32 qdx = tp->x - pp->x;
                f32 qdy = tp->y - pp->y;
                f32 qdz = tp->z - pp->z;
                f32 qdist = sqrtf(qdx * qdx + qdy * qdy + qdz * qdz);
                if (qdist > 0.01f) {
                    player->actor.velocity.x = (qdx / qdist) * 20.0f;
                    player->actor.velocity.y = (qdy / qdist) * 20.0f;
                    player->actor.velocity.z = (qdz / qdist) * 20.0f;
                }
                sPika.qatkTimer--;
                if (sPika.qatkTimer <= 0 || qdist < 30.0f) {
                    // Teleport back; trigger brief knockback anim as "return flash"
                    player->actor.world.pos = sPika.qatkStartPos;
                    sPika.knockbackActive = 1;
                    sPika.knockbackFrame = 0;
                    sPika.postKnockbackTimer = 0;
                    sPika.attackType = PIKA_ATTACK_NONE;
                    sPika.attackFrame = 0;
                    sPika.qatkPhase = 0;
                    sPika.qatkTarget = NULL;
                    sPika.qatkZTarget = 0;
                }
            } else {
                sPika.qatkTimer--;
                if (sPika.qatkTimer <= 0) {
                    sPika.qatkPhase = 0;
                }
            }
            // Electric ring particles
            static Color_RGBA8 qatkYellow = { 255, 220, 50, 255 };
            static Color_RGBA8 qatkWhite = { 255, 255, 200, 200 };
            Vec3f qatkZero = { 0.0f, 0.0f, 0.0f };
            for (s32 qi = 0; qi < 6; qi++) {
                u16 qangle = (u16)(qi * 0x2AAB);
                Vec3f ringPos = { player->actor.world.pos.x + Math_SinS((s16)qangle) * 15.0f,
                                  player->actor.world.pos.y + 30.0f,
                                  player->actor.world.pos.z + Math_CosS((s16)qangle) * 15.0f };
                EffectSsBlast_Spawn(play, &ringPos, &qatkZero, &qatkZero, &qatkYellow, &qatkWhite, 20, -3, 1, 4);
            }
        }

        // End attack when animation completes
        if (sPika.attackFrame >= def.totalFrames) {
            sPika.attackType = PIKA_ATTACK_NONE;
            sPika.attackFrame = 0;
            sPika.attackYOff = 0.0f;
            Environment_AdjustLights(play, 0.0f, 850.0f, 0.2f, 0.0f); // restore lighting after attack ends
            sPika.qatkPhase = 0;
            // Reset to idle immediately so next frame picks the right anim
            sPika.curAnim = &PikaIdleAnim;
            sPika.curAnimFrames = PIKA_IDLE_FRAMES;
            sPika.animFrameF = 0.0f;
        }

        // Also sync shape yaw while attacking
        player->actor.shape.rot.y = player->actor.world.rot.y;
        return; // Skip movement animation selection this frame
    }

    // ── Attack start — check A press ──────────────────────────────────────────

    // Suppress A-button attacks during blocking actions (grab, chest, door, talk, etc.)
    {
        u32 blockMask = PLAYER_STATE1_LOADING | PLAYER_STATE1_TALKING | PLAYER_STATE1_DEAD |
                        PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_CARRYING_ACTOR | PLAYER_STATE1_CLIMBING_LEDGE |
                        PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_FIRST_PERSON | PLAYER_STATE1_CLIMBING_LADDER |
                        PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_IN_CUTSCENE;
        if (player->stateFlags1 & blockMask) {
            aPressed = 0;
        }
    }

    if (aPressed) {
        u8 newAttack = PIKA_ATTACK_NONE;

        if (!onGround) {
            newAttack = PIKA_ATTACK_AIR;
        } else if (speed > 4.0f) {
            // Running → dash attack
            newAttack = PIKA_ATTACK_DASH;
        } else {
            // Grounded, check stick magnitude for smash vs tilt vs jab
            f32 mag = Pika_StickMag(play);
            if (mag > 0.75f) {
                newAttack = PIKA_ATTACK_FSMASH;
            } else if (mag > 0.25f) {
                newAttack = PIKA_ATTACK_FTILT;
            } else {
                newAttack = PIKA_ATTACK_JAB;
            }
        }

        if (newAttack != PIKA_ATTACK_NONE) {
            sPika.attackType = newAttack;
            sPika.attackFrame = 0;
            player->actor.shape.rot.y = player->actor.world.rot.y;
            return;
        }
    }

    // ── Movement animation selection (no attack active) ───────────────────────

    AnimationHeader* anim;
    s32 frames;

    if (!onGround) {
        if (velY > 0.0f) {
            // Rising: Pikachu backflip — RunStart ONCE, spin 360°
            sPika.jumpFlipAngle += 0x1200;
            player->actor.shape.rot.x = (s16)(-sPika.jumpFlipAngle);
            if (sPika.curAnim != &PikaRunStartAnim) {
                sPika.curAnim = &PikaRunStartAnim;
                sPika.animFrameF = 0.0f;
            }
            sPika.animFrameF += 1.0f;
            if (sPika.animFrameF >= (f32)PIKA_RUN_START_FRAMES)
                sPika.animFrameF = 0.0f;
            SkelAnime_GetFrameData(&PikaRunStartAnim, (s32)sPika.animFrameF, 23, sPika.jointTable);
            player->actor.shape.rot.y = player->actor.world.rot.y;
            return;
        } else {
            // Falling / neutral air — Pikachu idle pose while airborne
            sPika.jumpFlipAngle = 0;
            player->actor.shape.rot.x = 0;

            anim = &PikaIdleAnim;
            frames = PIKA_IDLE_FRAMES;
            player->actor.shape.rot.y = player->actor.world.rot.y;
        }
    } else if (speed > 1.5f) {
        // Fast ground movement — Pikachu run
        sPika.jumpFlipAngle = 0;
        player->actor.shape.rot.x = 0;

        anim = &PikaRunLoopAnim;
        frames = PIKA_RUN_LOOP_FRAMES;
    } else if (speed > 0.3f) {
        // Slow ground movement — Pikachu run loop (same as fast, just slower player speed)
        sPika.jumpFlipAngle = 0;
        player->actor.shape.rot.x = 0;

        anim = &PikaRunLoopAnim;
        frames = PIKA_RUN_LOOP_FRAMES;
    } else {
        // Standing still — Pikachu idle
        sPika.jumpFlipAngle = 0;
        player->actor.shape.rot.x = 0;

        anim = &PikaIdleAnim;
        frames = PIKA_IDLE_FRAMES;
    }

    // Advance animation (loop), reset on anim change
    if (anim != sPika.curAnim) {
        sPika.curAnim = anim;
        sPika.curAnimFrames = frames;
        sPika.animFrameF = 0.0f;
    }
    f32 animStep = (anim == &PikaIdleAnim) ? 0.3f : 1.0f;
    if (anim == &PikaIdleAnim && sPika.postKnockbackTimer > 0) {
        animStep = 0.15f;
        sPika.postKnockbackTimer--;
    }
    sPika.animFrameF += animStep;
    if (sPika.animFrameF >= (f32)frames)
        sPika.animFrameF = 0.0f;
    sPika.animFrame = (s32)sPika.animFrameF;

    SkelAnime_GetFrameData(anim, sPika.animFrame, 23, sPika.jointTable);

    // Sync shape yaw to world yaw (OOT already set world.rot.y)
    player->actor.shape.rot.y = player->actor.world.rot.y;
}

// ─────────────────────────────────────────────────────────────────────────────
// Skeleton draw — fast64 segment-0x0D matrix buffer approach
//
// fast64 exports skeletons where each limb DL references matrices from seg 0x0D:
//   gsSPMatrix(0x0d + slot*0x40, G_MTX_LOAD)
// where slot = DFS order index of limbs WITH dLists (18 total = dListCount).
//
// Two-pass approach:
//  Pass 1: walk skeleton DFS, accumulate world matrices, store in matBuf[slot]
//  Pass 2: walk same DFS order, load matBuf[slot] to RSP, call limb->dList
//   (mid-DL gsSPMatrix calls load other bones' matrices from 0x0D for skinning)
// ─────────────────────────────────────────────────────────────────────────────
#define PIKA_DL_COUNT 18 // limbs with dLists in Armature

static void Pika_FillMatBuf(Mtx* buf, u8 limbIdx, s32* slot, Vec3s* jt) {
    if (limbIdx == 0xFF || limbIdx >= (u8)ARMATURE_NUM_LIMBS)
        return;
    StandardLimb* limb = (StandardLimb*)Armature.sh.segment[limbIdx];
    Vec3f pos;
    Vec3s rot = jt[limbIdx + 1];
    if (limbIdx == 0) {
        pos.x = jt[0].x;
        pos.y = jt[0].y;
        pos.z = jt[0].z;
    } else {
        pos.x = (f32)limb->jointPos.x;
        pos.y = (f32)limb->jointPos.y;
        pos.z = (f32)limb->jointPos.z;
    }
    Matrix_Push();
    Matrix_TranslateRotateZYX(&pos, &rot);
    if (limb->dList != NULL) {
        Matrix_ToMtx(&buf[(*slot)++], const_cast<char*>(__FILE__), __LINE__);
    }
    Pika_FillMatBuf(buf, limb->child, slot, jt);
    Matrix_Pop();
    Pika_FillMatBuf(buf, limb->sibling, slot, jt);
}

static void Pika_DrawSkelR(PlayState* play, u8 limbIdx, Mtx* buf, s32* slot) {
    if (limbIdx == 0xFF || limbIdx >= (u8)ARMATURE_NUM_LIMBS)
        return;
    StandardLimb* limb = (StandardLimb*)Armature.sh.segment[limbIdx];
    if (limb->dList != NULL) {
        OPEN_DISPS(play->state.gfxCtx);
        gSPMatrix(POLY_OPA_DISP++, &buf[(*slot)++], G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, limb->dList);
        CLOSE_DISPS(play->state.gfxCtx);
    }
    Pika_DrawSkelR(play, limb->child, buf, slot);
    Pika_DrawSkelR(play, limb->sibling, buf, slot);
}

static void Pika_DrawSkel(PlayState* play) {
    Mtx* matBuf = (Mtx*)GRAPH_ALLOC(play->state.gfxCtx, PIKA_DL_COUNT * sizeof(Mtx));
    s32 fillSlot = 0;
    Pika_FillMatBuf(matBuf, 0, &fillSlot, sPika.jointTable);

    OPEN_DISPS(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x0D, (uintptr_t)matBuf);
    CLOSE_DISPS(play->state.gfxCtx);

    s32 drawSlot = 0;
    Pika_DrawSkelR(play, 0, matBuf, &drawSlot);
}

extern "C" void PikachuForm_Draw(PlayState* play, Player* player) {
    if (!sPika.initialized)
        return;

    // Damage flicker
    if (player->invincibilityTimer > 0 && (play->gameplayFrames % 4) < 2)
        return;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    // Automatic face expression — based on current game state
    s32 eyesIdx = PIKA_EYES_HAPPY;
    s32 mouthIdx = PIKA_MOUTH_HAPPY;
    s32 tailIdx = CVarGetInteger(CVAR_PIKACHU_TAIL, PIKA_TAIL_NORMAL);
    if (tailIdx < 0 || tailIdx >= PIKA_TAIL_MAX)
        tailIdx = PIKA_TAIL_NORMAL;

    if (sPika.knockbackActive) {
        // Taking damage: pained/surprised look
        eyesIdx = PIKA_EYES_CLOSED;
        mouthIdx = PIKA_MOUTH_SURPRISED;
    } else {
        switch (sPika.attackType) {
            case PIKA_ATTACK_JAB:
            case PIKA_ATTACK_FTILT:
            case PIKA_ATTACK_FSMASH:
            case PIKA_ATTACK_DASH:
            case PIKA_ATTACK_IRON_TAIL:
                eyesIdx = PIKA_EYES_ANGRY;
                mouthIdx = PIKA_MOUTH_ATTACK;
                break;
            case PIKA_ATTACK_AIR:
                eyesIdx = PIKA_EYES_HAPPY;
                mouthIdx = PIKA_MOUTH_SMILE;
                break;
            case PIKA_ATTACK_QUICK_ATK:
            case PIKA_ATTACK_SIDE_SPEC:
                eyesIdx = PIKA_EYES_HAPPY;
                mouthIdx = PIKA_MOUTH_SMILE;
                break;
            case PIKA_ATTACK_THUNDER: {
                // Charge phase (before hitbox): neutral/surprised; discharge: angry
                bool thunderDischarge = (sPika.attackFrame >= 8 && sPika.attackFrame <= 22);
                eyesIdx = thunderDischarge ? PIKA_EYES_ANGRY : PIKA_EYES_NEUTRAL;
                mouthIdx = thunderDischarge ? PIKA_MOUTH_ATTACK : PIKA_MOUTH_SURPRISED;
                break;
            }
            case PIKA_ATTACK_WHIP_GRAB:
            case PIKA_ATTACK_GRAB_HOLD:
                eyesIdx = PIKA_EYES_ANGRY;
                mouthIdx = PIKA_MOUTH_CHARGE;
                break;
            case PIKA_ATTACK_THROW:
                eyesIdx = PIKA_EYES_NEUTRAL;
                mouthIdx = PIKA_MOUTH_DISCHARGE;
                break;
            default:
                // Idle/running/airborne: happy
                eyesIdx = PIKA_EYES_HAPPY;
                mouthIdx = PIKA_MOUTH_HAPPY;
                break;
        }
    }

    // Blink overrides eyes (frames 1-3: eyes shut)
    if (sPika.blinkFrame >= 1 && sPika.blinkFrame <= 3)
        eyesIdx = PIKA_EYES_CLOSED;

    gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)pika_eyes_mats[eyesIdx]);
    gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)pika_mouth_mats[mouthIdx]);
    gSPSegment(POLY_OPA_DISP++, 0x0A, (uintptr_t)pika_tail_mats[tailIdx]);

    CLOSE_DISPS(play->state.gfxCtx);

    // World matrix: actor position (+ PIKA_HIP_HEIGHT to lift from hip to floor) + facing yaw + scale
    // Quick Attack Z-target mode: suppress draw while dashing invisibly toward target
    if (sPika.qatkZTarget && sPika.qatkPhase == 1)
        return;

    Matrix_SetTranslateRotateYXZ(player->actor.world.pos.x,
                                 player->actor.world.pos.y + PIKA_HIP_HEIGHT + sPika.attackYOff,
                                 player->actor.world.pos.z, &player->actor.world.rot);
    // Quick Attack: squish Z by 0.1 to give Smash-style stretch/dash effect
    f32 pikaZScale = (sPika.attackType == PIKA_ATTACK_QUICK_ATK) ? 0.1f : 1.0f;
    Matrix_Scale(PIKACHU_FORM_SCALE, PIKACHU_FORM_SCALE, PIKACHU_FORM_SCALE * pikaZScale, MTXMODE_APPLY);

    Pika_DrawSkel(play);

    // bodyPartsPos + feetPos are updated in PikachuForm_Update (runs every frame).
}
