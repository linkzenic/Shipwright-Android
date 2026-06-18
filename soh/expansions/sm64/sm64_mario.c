/**
 * sm64_mario.c - libsm64 integration for Ship of Harkinian
 *
 * Loads sm64.dll DYNAMICALLY at runtime via LoadLibrary/GetProcAddress.
 * Pure C, #included into z_player.c via the expansions section.
 */

#define SM64_LIB_FN
#include "expansions/sm64/libsm64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#define SM64_LOAD_LIB(path) LoadLibraryA(path)
#define SM64_GET_PROC(h, name) (void*)GetProcAddress((HMODULE)(h), name)
#define SM64_FREE_LIB(h) FreeLibrary((HMODULE)(h))
#else
#include <dlfcn.h>
#define SM64_LOAD_LIB(path) dlopen(path, RTLD_LAZY)
#define SM64_GET_PROC(h, name) dlsym(h, name)
#define SM64_FREE_LIB(h) dlclose(h)
#endif

// =============================================================================
// Dynamic function pointers
// =============================================================================

typedef void (*pfn_sm64_global_init)(const uint8_t*, uint8_t*);
typedef void (*pfn_sm64_global_terminate)(void);
typedef void (*pfn_sm64_static_surfaces_load)(const struct SM64Surface*, uint32_t);
typedef int32_t (*pfn_sm64_mario_create)(float, float, float);
typedef void (*pfn_sm64_mario_tick)(int32_t, const struct SM64MarioInputs*, struct SM64MarioState*,
                                    struct SM64MarioGeometryBuffers*);
typedef void (*pfn_sm64_mario_delete)(int32_t);
typedef void (*pfn_sm64_set_mario_position)(int32_t, float, float, float);
typedef void (*pfn_sm64_set_mario_water_level)(int32_t, int);
typedef void (*pfn_sm64_set_mario_health)(int32_t, uint16_t);
typedef void (*pfn_sm64_mario_take_damage)(int32_t, uint32_t, uint32_t, float, float, float);
typedef void (*pfn_sm64_mario_heal)(int32_t, uint8_t);
typedef void (*pfn_sm64_audio_init)(const uint8_t*);
typedef uint32_t (*pfn_sm64_audio_tick)(uint32_t, uint32_t, int16_t*);
typedef void (*pfn_sm64_play_sound_global)(int32_t);
typedef void (*pfn_sm64_set_sound_volume)(float);
typedef void (*pfn_sm64_set_mario_action)(int32_t, uint32_t);
typedef void (*pfn_sm64_set_mario_action_arg)(int32_t, uint32_t, uint32_t);
typedef void (*pfn_sm64_set_mario_forward_velocity)(int32_t, float);
typedef void (*pfn_sm64_set_mario_velocity)(int32_t, float, float, float);
typedef void (*pfn_sm64_set_mario_animation)(int32_t, int32_t);
typedef void (*pfn_sm64_mario_grab_dummy)(int32_t);
typedef void (*pfn_sm64_mario_release_dummy)(int32_t);
typedef void (*pfn_sm64_mario_interact_cap)(int32_t, uint32_t, uint16_t, uint8_t);
typedef void (*pfn_sm64_set_mario_state)(int32_t, uint32_t);
typedef void (*pfn_sm64_stop_background_music)(uint16_t);
typedef uint16_t (*pfn_sm64_get_current_background_music)(void);
typedef void (*pfn_sm64_play_music)(uint8_t, uint16_t, uint16_t);
// Remote-Mario (Harpoon puppet) procs: create a floor-tolerant render instance,
// force a network-synced pose, then run the geometry-only puppet tick to skin it.
typedef int32_t (*pfn_sm64_mario_create_puppet)(float, float, float);
typedef void (*pfn_sm64_set_mario_anim_frame)(int32_t, int16_t);
typedef void (*pfn_sm64_set_mario_faceangle)(int32_t, float);
typedef void (*pfn_sm64_mario_tick_puppet)(int32_t, struct SM64MarioGeometryBuffers*);

static pfn_sm64_global_init p_sm64_global_init = NULL;
static pfn_sm64_global_terminate p_sm64_global_terminate = NULL;
static pfn_sm64_static_surfaces_load p_sm64_static_surfaces_load = NULL;
static pfn_sm64_mario_create p_sm64_mario_create = NULL;
static pfn_sm64_mario_tick p_sm64_mario_tick = NULL;
static pfn_sm64_mario_delete p_sm64_mario_delete = NULL;
static pfn_sm64_set_mario_position p_sm64_set_mario_position = NULL;
static pfn_sm64_set_mario_water_level p_sm64_set_mario_water_level = NULL;
static pfn_sm64_set_mario_health p_sm64_set_mario_health = NULL;
static pfn_sm64_mario_take_damage p_sm64_mario_take_damage = NULL;
static pfn_sm64_mario_heal p_sm64_mario_heal = NULL;
static pfn_sm64_audio_init p_sm64_audio_init = NULL;
static pfn_sm64_audio_tick p_sm64_audio_tick = NULL;
static pfn_sm64_play_sound_global p_sm64_play_sound_global = NULL;
static pfn_sm64_set_sound_volume p_sm64_set_sound_volume = NULL;
static pfn_sm64_set_mario_action p_sm64_set_mario_action = NULL;
static pfn_sm64_set_mario_action_arg p_sm64_set_mario_action_arg = NULL;
static pfn_sm64_set_mario_forward_velocity p_sm64_set_mario_forward_velocity = NULL;
static pfn_sm64_set_mario_velocity p_sm64_set_mario_velocity = NULL;
static pfn_sm64_set_mario_animation p_sm64_set_mario_animation = NULL;
static pfn_sm64_mario_grab_dummy p_sm64_mario_grab_dummy = NULL;
static pfn_sm64_mario_release_dummy p_sm64_mario_release_dummy = NULL;
static pfn_sm64_mario_interact_cap p_sm64_mario_interact_cap = NULL;
static pfn_sm64_set_mario_state p_sm64_set_mario_state = NULL;
static pfn_sm64_stop_background_music p_sm64_stop_background_music = NULL;
static pfn_sm64_get_current_background_music p_sm64_get_current_background_music = NULL;
static pfn_sm64_play_music p_sm64_play_music = NULL;
static pfn_sm64_mario_create_puppet p_sm64_mario_create_puppet = NULL;
static pfn_sm64_set_mario_anim_frame p_sm64_set_mario_anim_frame = NULL;
static pfn_sm64_set_mario_faceangle p_sm64_set_mario_faceangle = NULL;
static pfn_sm64_mario_tick_puppet p_sm64_mario_tick_puppet = NULL;

static void* sDllHandle = NULL;

static s32 Sm64_LoadDll(void) {
    if (sDllHandle)
        return 1;

    sDllHandle = SM64_LOAD_LIB("nei/sm64.dll");
    if (!sDllHandle) {
        lusprintf(__FILE__, __LINE__, 2, "[SM64] ERROR: Could not load nei/sm64.dll\n");
        return 0;
    }

    p_sm64_global_init = (pfn_sm64_global_init)SM64_GET_PROC(sDllHandle, "sm64_global_init");
    p_sm64_global_terminate = (pfn_sm64_global_terminate)SM64_GET_PROC(sDllHandle, "sm64_global_terminate");
    p_sm64_static_surfaces_load = (pfn_sm64_static_surfaces_load)SM64_GET_PROC(sDllHandle, "sm64_static_surfaces_load");
    p_sm64_mario_create = (pfn_sm64_mario_create)SM64_GET_PROC(sDllHandle, "sm64_mario_create");
    p_sm64_mario_tick = (pfn_sm64_mario_tick)SM64_GET_PROC(sDllHandle, "sm64_mario_tick");
    p_sm64_mario_delete = (pfn_sm64_mario_delete)SM64_GET_PROC(sDllHandle, "sm64_mario_delete");
    p_sm64_set_mario_position = (pfn_sm64_set_mario_position)SM64_GET_PROC(sDllHandle, "sm64_set_mario_position");
    p_sm64_set_mario_water_level =
        (pfn_sm64_set_mario_water_level)SM64_GET_PROC(sDllHandle, "sm64_set_mario_water_level");
    p_sm64_set_mario_health = (pfn_sm64_set_mario_health)SM64_GET_PROC(sDllHandle, "sm64_set_mario_health");
    p_sm64_mario_take_damage = (pfn_sm64_mario_take_damage)SM64_GET_PROC(sDllHandle, "sm64_mario_take_damage");
    p_sm64_mario_heal = (pfn_sm64_mario_heal)SM64_GET_PROC(sDllHandle, "sm64_mario_heal");
    p_sm64_audio_init = (pfn_sm64_audio_init)SM64_GET_PROC(sDllHandle, "sm64_audio_init");
    p_sm64_audio_tick = (pfn_sm64_audio_tick)SM64_GET_PROC(sDllHandle, "sm64_audio_tick");
    p_sm64_play_sound_global = (pfn_sm64_play_sound_global)SM64_GET_PROC(sDllHandle, "sm64_play_sound_global");
    p_sm64_set_sound_volume = (pfn_sm64_set_sound_volume)SM64_GET_PROC(sDllHandle, "sm64_set_sound_volume");
    p_sm64_set_mario_action = (pfn_sm64_set_mario_action)SM64_GET_PROC(sDllHandle, "sm64_set_mario_action");
    p_sm64_set_mario_action_arg =
        (pfn_sm64_set_mario_action_arg)SM64_GET_PROC(sDllHandle, "sm64_set_mario_action_arg");
    p_sm64_set_mario_forward_velocity =
        (pfn_sm64_set_mario_forward_velocity)SM64_GET_PROC(sDllHandle, "sm64_set_mario_forward_velocity");
    p_sm64_set_mario_velocity =
        (pfn_sm64_set_mario_velocity)SM64_GET_PROC(sDllHandle, "sm64_set_mario_velocity");
    p_sm64_set_mario_animation =
        (pfn_sm64_set_mario_animation)SM64_GET_PROC(sDllHandle, "sm64_set_mario_animation");
    p_sm64_mario_create_puppet =
        (pfn_sm64_mario_create_puppet)SM64_GET_PROC(sDllHandle, "sm64_mario_create_puppet");
    p_sm64_set_mario_anim_frame =
        (pfn_sm64_set_mario_anim_frame)SM64_GET_PROC(sDllHandle, "sm64_set_mario_anim_frame");
    p_sm64_set_mario_faceangle =
        (pfn_sm64_set_mario_faceangle)SM64_GET_PROC(sDllHandle, "sm64_set_mario_faceangle");
    p_sm64_mario_tick_puppet =
        (pfn_sm64_mario_tick_puppet)SM64_GET_PROC(sDllHandle, "sm64_mario_tick_puppet");
    p_sm64_mario_grab_dummy =
        (pfn_sm64_mario_grab_dummy)SM64_GET_PROC(sDllHandle, "sm64_mario_grab_dummy");
    p_sm64_mario_release_dummy =
        (pfn_sm64_mario_release_dummy)SM64_GET_PROC(sDllHandle, "sm64_mario_release_dummy");
    p_sm64_mario_interact_cap =
        (pfn_sm64_mario_interact_cap)SM64_GET_PROC(sDllHandle, "sm64_mario_interact_cap");
    p_sm64_set_mario_state =
        (pfn_sm64_set_mario_state)SM64_GET_PROC(sDllHandle, "sm64_set_mario_state");
    p_sm64_stop_background_music =
        (pfn_sm64_stop_background_music)SM64_GET_PROC(sDllHandle, "sm64_stop_background_music");
    p_sm64_get_current_background_music =
        (pfn_sm64_get_current_background_music)SM64_GET_PROC(sDllHandle, "sm64_get_current_background_music");
    p_sm64_play_music = (pfn_sm64_play_music)SM64_GET_PROC(sDllHandle, "sm64_play_music");

    if (!p_sm64_global_init || !p_sm64_mario_create || !p_sm64_mario_tick) {
        SM64_FREE_LIB(sDllHandle);
        sDllHandle = NULL;
        return 0;
    }

    return 1;
}

// =============================================================================
// State
// =============================================================================

static s32 sSm64Initialized = 0;
static int32_t sSm64MarioId = -1;

// Independent Mario health (8 segments = 8 hits), decoupled from Link's hearts.
// Mario is the source of truth; Link's gSaveContext.health is mirrored from it
// (for the OOT game-over) and OOT recovery is forwarded back to Mario.
//   sMarioLinkMirrorHP  — the Link HP we last wrote (heal-detect baseline; -1 = needs reinit)
//   sMarioHealthPersist — Mario's HP carried across scene-change recreates (-1 = start full)
static s16 sMarioLinkMirrorHP = -1;
static s16 sMarioHealthPersist = -1;
// Accumulated OOT heal (in quarter-hearts) fed by Health_ChangeBy via
// Sm64Mario_QueueOotHeal. Drained into a libsm64 heal each Mario update so heart
// pickups / heart CONTAINERS / fairies / potions reliably heal Mario, even when
// the heal landed during an item-get cutscene (Mario suspended → applies on resume).
static s32 sSm64PendingHealQuarters = 0;
// #3 Door animation: frame cursor for Mario's SM64 door-open anim while OOT walks
// Link through a door. Advances during the door (park path), reset to 0 on the
// normal walking tick so each door replays from the start. PrevX/Z track Link's
// position across door frames so Mario faces his TRAVEL direction (not Link's
// instantaneous yaw, which the door can flip — that made the anim look reversed).
static s16 sSm64DoorAnimFrame = 0;
static f32 sSm64DoorPrevX = 0.0f;
static f32 sSm64DoorPrevZ = 0.0f;
static uint8_t* sSm64RomData = NULL;
static uint8_t* sSm64TextureAtlas = NULL;
static s16 sSm64LastSceneNum = -1;
static u8 sSm64FrameToggle = 0;

// libsm64's Mario hurtbox is ~180 units tall; OOT's Link is ~60 units.
// Sending 1:1 world coords makes Mario physically 3–4× too big for OOT
// passages. Scale OOT → libsm64 by 4 on the way in (surfaces + create +
// position writes) and libsm64 → OOT by 1/4 on the way out (position
// readback). The render already scales mesh by 1/4 (SM64_SCALE in
// sm64_mario_render.c), so visual size stays right for OOT.
#define SM64_WORLD_SCALE 4.0f

#define SM64_MAX_TRIS SM64_GEO_MAX_TRIANGLES
static float sSm64PosBuffer[SM64_MAX_TRIS * 9];
static float sSm64NormBuffer[SM64_MAX_TRIS * 9];
static float sSm64ColorBuffer[SM64_MAX_TRIS * 9];
static float sSm64UvBuffer[SM64_MAX_TRIS * 6];
static struct SM64MarioState sSm64OutState;
static struct SM64MarioGeometryBuffers sSm64OutBuffers;

// =============================================================================
// libsm64 action / flag constants — mirror values from SM64 decomp's sm64.h
// (the values are stable across libsm64 builds since they match SM64's ABI).
// =============================================================================
#define SM64_MARIO_PUNCHING         0x00100000
#define SM64_MARIO_KICKING          0x00200000
#define SM64_ACT_PUNCHING           0x00800380
#define SM64_ACT_GROUND_POUND_LAND  0x0080023C
#define SM64_ACT_DIVE               0x0188088A
#define SM64_ACT_DIVE_SLIDE         0x00880456
#define SM64_ACT_SLIDE_KICK         0x018008AA
#define SM64_ACT_SLIDE_KICK_SLIDE   0x0080045A

// Water actions + flag (sm64.h:264, 303, and ACT_FLAG_SWIMMING = 0x00002000).
// Used by the surface-jump logic so pressing A while swimming near the
// water surface pops Mario out cleanly instead of getting stuck idling.
#define SM64_ACT_WATER_JUMP         0x01000889
#define SM64_ACT_WATER_IDLE         0x380022C0
#define SM64_ACT_FLAG_SWIMMING      0x00002000

// Hold / throw actions (sm64.h:181, 215, 410, 414). Safe to drive directly
// because we install a sentinel held-object via the patched libsm64 export
// `sm64_mario_grab_dummy` immediately after sm64_mario_create. With usedObj
// non-NULL, the pickup → hold → throw action handlers run their full
// animation flow (PICK_UP_LIGHT_OBJ → IDLE_WITH_LIGHT_OBJ →
// WALK_WITH_LIGHT_OBJ → THROW_LIGHT_OBJECT) without dereferencing NULL.
// Without the patch these would crash inside sm64.dll!sm64_mario_tick.
#define SM64_ACT_PICKING_UP         0x00000383
#define SM64_ACT_HOLD_IDLE          0x08000207
#define SM64_ACT_HOLD_WALKING       0x00000442
#define SM64_ACT_THROWING           0x80000588

// Mario's internal full health value (libsm64.h: 0x880 = 8 segments × 0x110).
// Used for the Link↔Mario HP sync — we scale Link's quarter-hearts into
// this range each frame so libsm64 never runs Mario's death check against
// a stale value that disagrees with OOT's.
#define SM64_MARIO_MAX_HP           0x0880

// Cap power-up flags (sm64.h:116-118) — passed to sm64_mario_interact_cap.
// Maps OOT spells to SM64 caps in the Mario item bridge:
//   Nayru's Love → Metal Cap (invincibility, sinks in water)
//   Farore's Wind → Wing Cap (flight via triple jump → flap)
#define SM64_MARIO_NORMAL_CAP       0x00000001
#define SM64_MARIO_VANISH_CAP       0x00000002
#define SM64_MARIO_METAL_CAP        0x00000004
#define SM64_MARIO_WING_CAP         0x00000008
#define SM64_MARIO_CAP_ON_HEAD      0x00000010  // restored after clearing a special cap

// How often (in frames) to re-upload OOT collision into libsm64 so the LIVE
// world stays in sync: broken blocks stop colliding, dynapoly doors / moving
// platforms track their current pose, vanish-cap phase-through uses the current
// wall set. The static set is otherwise frozen at scene-load.
//
// PERF (#4): re-extracting the whole scene + sm64_static_surfaces_load (which
// rebuilds libsm64's spatial partition for thousands of polys) every 4 frames was
// the main Mario lag. We now only rebuild when the DYNAPOLY actually changed (a
// cheap per-frame signature) — but no more often than _FRAMES while it keeps
// moving, and at least every _MAX frames as a safety net. Net: static scenes load
// surfaces ONCE; only moving platforms/doors trigger periodic rebuilds.
#define SM64_SURFACE_REFRESH_FRAMES 4  // min frames between rebuilds while dynapoly moves
#define SM64_SURFACE_REFRESH_MAX    30 // safety-net rebuild interval when nothing moves

// =============================================================================
// Damage / environment reaction actions (sm64.h). Forced via set_mario_action
// when the matching OOT state is detected (Sm64Mario_ApplyBehaviorAnims), so
// Mario plays SM64's native reaction animation + recovery instead of his normal
// moveset. ACT_FLAG_AIR/IDLE are used to branch ground/air and gate the idle
// shiver. Defined here (before sm64_mario_items.c is #included) so the cap
// handler there can reach SM64_ACT_PUTTING_ON_CAP / SM64_ACT_FLAG_AIR too.
// =============================================================================
#define SM64_ACT_FLAG_AIR           0x00000800
#define SM64_ACT_FLAG_IDLE          0x00400000
#define SM64_ACT_IDLE               0x0C400201  // grounded standing idle
#define SM64_ACT_SHIVERING          0x0C40020B  // cold idle (Ice Cavern)
#define SM64_ACT_SHOCKED            0x00020338  // electric (bodyShockTimer)
#define SM64_ACT_BURNING_GROUND     0x00020449  // on fire, grounded
#define SM64_ACT_BURNING_JUMP       0x010208B4  // on fire, airborne
#define SM64_ACT_PUTTING_ON_CAP     0x0000133D  // cap-on visual
#define SM64_ACT_TWIRLING           0x108008A4  // X (C-Left) spin — ACT_FLAG_ATTACKING
#define SM64_ACT_FORWARD_ROLLOUT    0x010008A6  // Y (C-Right) forward spin roll

// =============================================================================
// Master-Sword punch collider (AT) — positioned at Mario's fist per-frame
// =============================================================================
static ColliderCylinder sSm64AttackCollider;
static u8 sSm64AttackColliderInited = 0;

static ColliderCylinderInit sSm64AttackColliderInit = {
    { COLTYPE_NONE, AT_ON | AT_TYPE_PLAYER, AC_NONE,
      OC1_NONE, OC2_TYPE_PLAYER, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK0,
      { DMG_SLASH_MASTER | DMG_JUMP_MASTER | DMG_SPIN_MASTER, 0x00, 0x08 },
      { 0x00000000, 0x00, 0x00 },
      TOUCH_ON | TOUCH_NEAREST, BUMP_NONE, OCELEM_NONE },
    { 15, 40, -20, { 0, 0, 0 } }
};

// =============================================================================
// Metal Cap blast aura (AT) — a persistent damage cylinder centered on Mario,
// armed EVERY frame while the Metal Cap is worn. Contact kills enemies and
// breaks props (SM64 invincible "star" feel). Broad dmgFlags (0xFFFFFFFF) so it
// matches slash / hammer / explosive AC reactions alike — i.e. "blast + Master
// Sword" in one collider. Independent of the attack collider above (which only
// arms during punches/kicks/spins).
// =============================================================================
static ColliderCylinder sSm64MetalCollider;
static u8 sSm64MetalColliderInited = 0;

static ColliderCylinderInit sSm64MetalColliderInit = {
    { COLTYPE_NONE, AT_ON | AT_TYPE_PLAYER, AC_NONE,
      OC1_NONE, OC2_TYPE_PLAYER, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK0,
      { 0xFFFFFFFF, 0x00, 0x08 },   // dmgFlags = ALL damage types → kills/breaks everything
      { 0x00000000, 0x00, 0x00 },
      TOUCH_ON | TOUCH_SFX_NONE, BUMP_NONE, OCELEM_NONE },
    { 45, 70, -10, { 0, 0, 0 } }    // radius 45, height 70, yShift -10 (whole-body reach)
};

// =============================================================================
// ROM Loading
// =============================================================================

static uint8_t* Sm64_LoadRomFile(const char* path, size_t* outSize) {
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;

    fseek(f, 0, SEEK_END);
    *outSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* data = (uint8_t*)malloc(*outSize);
    if (!data) {
        fclose(f);
        return NULL;
    }

    fread(data, 1, *outSize, f);
    fclose(f);
    return data;
}

// =============================================================================
// Initialization
// =============================================================================

static s32 Sm64_InitLibrary(void) {
    const char* romPath;
    size_t romSize = 0;

    if (sSm64Initialized)
        return 1;

    if (!Sm64_LoadDll()) {
        lusprintf(__FILE__, __LINE__, 2, "[SM64] FAIL: DLL not loaded");
        return 0;
    }
    lusprintf(__FILE__, __LINE__, 2, "[SM64] DLL loaded OK");

    romPath = CVarGetString("gSm64RomPath", "");
    if (romPath == NULL || romPath[0] == '\0') {
        romPath = "nei/sm64.z64";
    }

    sSm64RomData = Sm64_LoadRomFile(romPath, &romSize);

    if (sSm64RomData == NULL) {
        lusprintf(__FILE__, __LINE__, 2, "[SM64] FAIL: ROM not found");
        return 0;
    }

    if (romSize != 8 * 1024 * 1024) {
        lusprintf(__FILE__, __LINE__, 2, "[SM64] FAIL: ROM wrong size %zu", romSize);
        free(sSm64RomData);
        sSm64RomData = NULL;
        return 0;
    }
    lusprintf(__FILE__, __LINE__, 2, "[SM64] ROM loaded OK (%zu bytes)", romSize);

    sSm64TextureAtlas = (uint8_t*)malloc(4 * SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT);
    if (!sSm64TextureAtlas) {
        free(sSm64RomData);
        sSm64RomData = NULL;
        return 0;
    }

    p_sm64_global_init(sSm64RomData, sSm64TextureAtlas);

    // Initialize libsm64's internal audio engine. After this, sm64_mario_tick
    // will auto-queue SM64's sound effects (punch, jump, coin, death, etc.)
    // and sm64_audio_tick fills PCM buffers we mix into SoH's audio output.
    if (p_sm64_audio_init) {
        p_sm64_audio_init(sSm64RomData);
        if (p_sm64_set_sound_volume) p_sm64_set_sound_volume(0.8f);
        lusprintf(__FILE__, __LINE__, 2, "[SM64] Audio engine initialized");
    }

    // Darken Mario's textures (RGB × 0.8) so he blends better with OOT's
    // moodier scene ambience. Keep alpha untouched — it's the mix weight in
    // our combiner, not opacity. One-time cost at init; libsm64 never
    // regenerates the atlas after sm64_global_init.
    {
        u32 atlasBytes = 4u * (u32)SM64_TEXTURE_WIDTH * (u32)SM64_TEXTURE_HEIGHT;
        u32 i;
        for (i = 0; i < atlasBytes; i += 4) {
            sSm64TextureAtlas[i + 0] = (u8)((u32)sSm64TextureAtlas[i + 0] * 4 / 5);
            sSm64TextureAtlas[i + 1] = (u8)((u32)sSm64TextureAtlas[i + 1] * 4 / 5);
            sSm64TextureAtlas[i + 2] = (u8)((u32)sSm64TextureAtlas[i + 2] * 4 / 5);
            // sSm64TextureAtlas[i + 3] (alpha) unchanged — mix weight.
        }
    }

    Sm64Render_SetTextureAtlas(sSm64TextureAtlas);

    sSm64OutBuffers.position = sSm64PosBuffer;
    sSm64OutBuffers.normal = sSm64NormBuffer;
    sSm64OutBuffers.color = sSm64ColorBuffer;
    sSm64OutBuffers.uv = sSm64UvBuffer;
    sSm64OutBuffers.numTrianglesUsed = 0;

    lusprintf(__FILE__, __LINE__, 2, "[SM64] Library fully initialized");

    sSm64Initialized = 1;
    return 1;
}

// =============================================================================
// Surface Loading
// =============================================================================

// Returns number of surfaces loaded. 0 means nothing was loaded (don't trust collision yet).
// floorOnly=1 strips wall + ceiling polys so Mario can phase through them
// (used while the SM64 vanish cap is active — user wanted Din's Fire to act
// like the cap and let Mario "atravesar todo menos el piso").
static u32 Sm64_LoadSceneSurfacesEx(PlayState* play, u8 floorOnly) {
    uint32_t numSurfaces = 0;
    struct SM64Surface* surfaces;

    if (!p_sm64_static_surfaces_load)
        return 0;

    surfaces = Sm64Surfaces_ExtractFiltered(play, &numSurfaces, floorOnly);
    if (surfaces == NULL || numSurfaces == 0) {
        // Don't call sm64_static_surfaces_load with 0 — it would erase existing surfaces.
        // Diagnostic: this silently returns 0, so log the reason (malloc fail vs. all
        // polys filtered out). Throttled so we don't spam.
        static u32 sZeroFrames = 0;
        if ((sZeroFrames % 60) == 0) {
            lusprintf(__FILE__, __LINE__, 2,
                "[SM64] Extract returned null=%d count=%u scene=%d srcNumPolys=%d floorOnly=%d",
                surfaces == NULL, numSurfaces, play->sceneNum,
                play->colCtx.colHeader ? play->colCtx.colHeader->numPolygons : -1,
                floorOnly);
        }
        sZeroFrames++;
        return 0;
    }

    {
        // Throttled: this now also runs on a periodic refresh (every few
        // frames), so logging every call would spam. Log ~once per second.
        static u32 sLoadLog = 0;
        if ((sLoadLog++ % 60) == 0) {
            lusprintf(__FILE__, __LINE__, 2, "[SM64] Loaded %u surfaces for scene %d (floorOnly=%d)",
                numSurfaces, play->sceneNum, floorOnly);
        }
    }
    p_sm64_static_surfaces_load(surfaces, numSurfaces);
    free(surfaces);
    return numSurfaces;
}

static u32 Sm64_LoadSceneSurfaces(PlayState* play) {
    return Sm64_LoadSceneSurfacesEx(play, 0);
}

// =============================================================================
// Public API
// =============================================================================

// Tracks the scene number the currently-loaded libsm64 surfaces belong to.
// -1 means "no surfaces loaded". If this doesn't match play->sceneNum, surfaces
// are stale and must be reloaded before mario_create can be trusted.
static s16 sSm64SurfacesForScene = -1;

s32 Sm64Mario_Init(PlayState* play, Player* player) {

    if (!Sm64_InitLibrary())
        return 0;

    if (sSm64MarioId >= 0)
        return 1;

    // Wait for collision to be ready
    if (play->colCtx.colHeader == NULL || play->colCtx.colHeader->numPolygons == 0)
        return 0;

    // Reload surfaces if they don't match the current scene. Without this, a
    // partially-successful Init during title/intro pins stale surfaces and all
    // later mario_create attempts fail against irrelevant collision.
    if (sSm64SurfacesForScene != play->sceneNum) {
        u32 count = Sm64_LoadSceneSurfaces(play);
        if (count == 0) return 0; // Surfaces not ready yet, retry next frame
        sSm64SurfacesForScene = play->sceneNum;
    }

    // Spawn Mario at Link's position (scaled into libsm64 world). find_floor
    // (surface_collision.c:140) accepts any surface whose height <= y + 78.
    if (p_sm64_mario_create) {
        sSm64MarioId = p_sm64_mario_create(
            player->actor.world.pos.x * SM64_WORLD_SCALE,
            player->actor.world.pos.y * SM64_WORLD_SCALE,
            player->actor.world.pos.z * SM64_WORLD_SCALE);
        lusprintf(__FILE__, __LINE__, 2, "[SM64] mario_create id=%d pos=(%.0f,%.0f,%.0f) scene=%d",
            sSm64MarioId, player->actor.world.pos.x, player->actor.world.pos.y,
            player->actor.world.pos.z, play->sceneNum);
    }

    if (sSm64MarioId >= 0) {
        // Independent health: restore Mario's carried-over HP (full the first time).
        if (p_sm64_set_mario_health) {
            p_sm64_set_mario_health(sSm64MarioId,
                (sMarioHealthPersist > 0) ? (u16)sMarioHealthPersist : (u16)SM64_MARIO_MAX_HP);
            sMarioLinkMirrorHP = -1;
        }
        sSm64LastSceneNum = play->sceneNum;
        // Install the sentinel held-object so ACT_PICKING_UP / ACT_HOLD_IDLE /
        // ACT_THROWING run their full anim flow without crashing on a NULL
        // heldObj deref. usedObj stays valid across throws — single call is
        // enough per Mario instance.
        if (p_sm64_mario_grab_dummy) {
            p_sm64_mario_grab_dummy(sSm64MarioId);
        }
        // Prime a tick so the draw hook has a valid mesh THIS frame.
        {
            struct SM64MarioInputs zeroInputs;
            memset(&zeroInputs, 0, sizeof(zeroInputs));
            zeroInputs.camLookZ = 1.0f;
            sSm64OutBuffers.numTrianglesUsed = 0;
            p_sm64_mario_tick(sSm64MarioId, &zeroInputs, &sSm64OutState, &sSm64OutBuffers);
        }
        return 1;
    }
    return 0;
}

// Forward-decl — definition lives at the end of the file. Called from
// Sm64Mario_Update to top up the audio ring buffer on the game thread.
static void Sm64Audio_RefillRing(void);

// =============================================================================
// B-proximity grab (Mario-style auto-grab on punch)
//
// A is reserved for Mario's jump, so OOT's A-press pickup via
// Player_ActionHandler_2 can't be repurposed. Instead, each frame the B-press
// probes nearby actors from a small whitelist (bombs, bomb flowers, liftable
// stones) and attaches the nearest one to Link's actor — same mechanism
// BallChain_CheckDestructibles uses for its proximity scan, but we SET
// actor->parent instead of destroying. While held, we pin the actor to
// Mario's position each frame; on the next B-press we detach with forward
// velocity (mirroring func_8084409C in z_player.c). ACT_PICKING_UP and
// ACT_THROWING are kicked on Mario so his mesh plays the matching animation.
// =============================================================================
static Actor* sSm64HeldActor = NULL;
// Debounce so the B-press that triggers a throw doesn't immediately re-grab
// whatever's still in range the same frame.
static u8 sSm64GrabLockoutFrames = 0;

static u8 Sm64Mario_IsGrabbableId(s16 id) {
    switch (id) {
        case ACTOR_EN_BOM:      // regular bomb
        case ACTOR_EN_BOMBF:    // bomb flower
        case ACTOR_EN_ISHI:     // liftable small / large stones (silver rocks)
        case ACTOR_EN_RU1:      // Ruto (carry her — Jabu-Jabu)
        case ACTOR_OBJ_TSUBO:   // pots
        case ACTOR_OBJ_KIBAKO:  // small crate
        case ACTOR_OBJ_KIBAKO2: // large crate
            return 1;
    }
    return 0;
}

static Actor* Sm64Mario_FindGrabbable(PlayState* play, Player* player) {
    // Reach: slightly more than Mario's arm length in OOT units. Matches
    // the "feels right" range from BallChain's proximity constants.
    const f32 reach = 55.0f;
    Actor* best = NULL;
    f32 bestDist = reach;
    s32 cat;
    for (cat = 0; cat < ACTORCAT_MAX; cat++) {
        Actor* a;
        for (a = play->actorCtx.actorLists[cat].head; a != NULL; a = a->next) {
            if (a == NULL || a->update == NULL) continue;
            if (!Sm64Mario_IsGrabbableId(a->id)) continue;
            // Skip actors already parented to someone (already held).
            if (a->parent != NULL) continue;
            f32 d = Math_Vec3f_DistXYZ(&player->actor.world.pos, &a->world.pos);
            if (d < bestDist) {
                bestDist = d;
                best = a;
            }
        }
    }
    return best;
}

// Called each frame from Sm64Mario_Update — must run AFTER the tick so
// `sSm64OutState.faceAngle` (used for throw yaw) reflects this frame's value.
static void Sm64Mario_TryGrabOrThrow(PlayState* play, Player* player) {
    if (sSm64GrabLockoutFrames > 0) {
        sSm64GrabLockoutFrames--;
    }

    // Held actor died / was untouched by external forces? Clear pointer.
    // We keep our grip while parent == &player->actor; if something else
    // nulled parent (e.g. explosion from a lit bomb), drop our ref too.
    if (sSm64HeldActor != NULL) {
        if (sSm64HeldActor->update == NULL || sSm64HeldActor->parent != &player->actor) {
            sSm64HeldActor = NULL;
        }
    }

    Input* in = &play->state.input[0];
    u8 bPress = (in->press.button & BTN_B) != 0;

    if (sSm64HeldActor != NULL) {
        // Carry — pin actor at Mario's hands during the SM64 hold-light-obj
        // pose. Mario in MARIO_ANIM_IDLE_WITH_LIGHT_OBJ holds the object
        // at chest level forward of the body (NOT overhead — that's the
        // pickup transition). Mario's effective render height is ~37 OOT
        // units (libsm64 mesh × 0.25 scale); chest is ~18 from feet,
        // hands forward by ~10. Previous +35 was Mario's head — bomb
        // looked like it was floating over him instead of in his grip.
        f32 fistFwd = 10.0f;
        f32 sinY = Math_SinS(player->actor.shape.rot.y);
        f32 cosY = Math_CosS(player->actor.shape.rot.y);
        sSm64HeldActor->world.pos.x = player->actor.world.pos.x + sinY * fistFwd;
        sSm64HeldActor->world.pos.y = player->actor.world.pos.y + 18.0f;
        sSm64HeldActor->world.pos.z = player->actor.world.pos.z + cosY * fistFwd;
        sSm64HeldActor->prevPos = sSm64HeldActor->world.pos;
        // Zero velocity each frame — otherwise a bomb's internal physics
        // from the last pre-grab update would keep adding gravity.
        sSm64HeldActor->velocity.x = 0.0f;
        sSm64HeldActor->velocity.y = 0.0f;
        sSm64HeldActor->velocity.z = 0.0f;
        sSm64HeldActor->speedXZ = 0.0f;

        if (bPress && sSm64GrabLockoutFrames == 0) {
            // Throw — set OOT velocity for the actor's own physics, then
            // kick Mario into ACT_THROWING. With grab_dummy installed,
            // act_throwing's mario_throw_held_object call hits a valid
            // sentinel object (no NULL deref) and plays the throw anim.
            s16 throwYaw = player->actor.shape.rot.y;
            sSm64HeldActor->world.rot.y = throwYaw;
            sSm64HeldActor->shape.rot.y = throwYaw;
            f32 launchDX = Math_SinS(throwYaw);
            f32 launchDZ = Math_CosS(throwYaw);
            sSm64HeldActor->speedXZ = 12.0f;
            sSm64HeldActor->velocity.x = launchDX * 12.0f;
            sSm64HeldActor->velocity.z = launchDZ * 12.0f;
            sSm64HeldActor->velocity.y = 10.0f;
            // Detach OOT-side — the actor's own action func reads
            // parent==NULL as "thrown" and applies gravity itself.
            sSm64HeldActor->parent = NULL;
            // Release OOT's grip too (the offer-grab set heldActor + CARRYING),
            // so it doesn't keep the item or re-throw it from Link's position.
            if (player->heldActor == sSm64HeldActor) {
                player->heldActor = NULL;
            }
            player->stateFlags1 &= ~PLAYER_STATE1_CARRYING_ACTOR;
            sSm64HeldActor = NULL;

            // Mario throw animation. Safe with grab_dummy: act_throwing
            // derefs heldObj (the sentinel) without crashing.
            if (p_sm64_set_mario_action && sSm64MarioId >= 0) {
                p_sm64_set_mario_action(sSm64MarioId, SM64_ACT_THROWING);
            }
            // Consume B so throwing the held actor doesn't also fire a fireball.
            in->press.button &= ~BTN_B;
            sSm64GrabLockoutFrames = 10;
        }
        return;
    }

    // Nothing held — adopt whatever OOT's offer-grab handed us. The vanilla
    // A-action (Player_ActionHandler_2) fires on real-B via the A<->B swap and
    // sets player->heldActor; we take it over so the held actor runs through the
    // pin + throw-from-Mario logic above. Without this, OOT carries it on Link's
    // (undrawn) hand, so it stays put and throws from the wrong spot.
    if (player->heldActor != NULL && player->heldActor->parent == &player->actor) {
        // OOT's get-item/offer path already set a held actor — adopt it. (Checked
        // FIRST so it can't double-grab with the proximity path below.)
        sSm64HeldActor = player->heldActor;
        if (p_sm64_set_mario_action && sSm64MarioId >= 0) {
            p_sm64_set_mario_action(sSm64MarioId, SM64_ACT_PICKING_UP);
        }
        sSm64GrabLockoutFrames = 10;
        lusprintf(__FILE__, __LINE__, 2, "[SM64] Adopted OOT heldActor id=0x%02X", player->heldActor->id);
    } else if (bPress && sSm64GrabLockoutFrames == 0) {
        // Proximity grab on B. Bombs / bomb flowers / liftable rocks are CARRIED
        // through a multi-frame OOT lift action func, which PAUSE_ACTION_FUNC
        // freezes for Mario — so the vanilla path never sets heldActor and the
        // adopt branch above never fires (that's why "B can't grab bombs"). Grab
        // directly here instead. Because OOT's lift is frozen it can't ALSO carry
        // the actor, so the old double-carry / wrong-spot-throw bug can't recur.
        Actor* grab = Sm64Mario_FindGrabbable(play, player);
        if (grab != NULL) {
            grab->parent = &player->actor; // mark held (FindGrabbable skips parented)
            grab->velocity.x = grab->velocity.y = grab->velocity.z = 0.0f;
            grab->speedXZ = 0.0f;
            sSm64HeldActor = grab;
            if (p_sm64_set_mario_action && sSm64MarioId >= 0) {
                p_sm64_set_mario_action(sSm64MarioId, SM64_ACT_PICKING_UP);
            }
            // Consume B so the Fire cap doesn't also throw a fireball on this press.
            in->press.button &= ~BTN_B;
            sSm64GrabLockoutFrames = 10;
            lusprintf(__FILE__, __LINE__, 2, "[SM64] Proximity-grabbed id=0x%02X", grab->id);
        }
    }
}

// Scene change / Mario reset must drop the held ref — actor may already be
// freed, and we shouldn't leave a parent pointer to a now-stale Player.
static void Sm64Mario_DropHeldActor(void) {
    if (sSm64HeldActor != NULL) {
        if (sSm64HeldActor->update != NULL && sSm64HeldActor->parent != NULL) {
            sSm64HeldActor->parent = NULL;
        }
        sSm64HeldActor = NULL;
    }
    sSm64GrabLockoutFrames = 0;
}

// =============================================================================
// Environment behavior anims — per-frame ambient reactions driven by scene/
// hazard state (not by a hit). Forcing the native ACTION (not a raw anim — the
// tick would overwrite a forced anim) lets libsm64 play + recover it.
//
// Hit-driven reactions (fire / ice / electric / knockback) are NOT here: they
// come from OOT's AC pipeline (colChkInfo.acHitEffect), which
// Sm64Mario_InterceptDamage short-circuits before bodyShockTimer / bodyIsBurning
// are ever set — so they're applied there instead, where the effect is live.
// =============================================================================
static void Sm64Mario_ApplyBehaviorAnims(PlayState* play, Player* player) {
    (void)player; // reserved for future state-driven env reactions
    if (sSm64MarioId < 0 || !p_sm64_set_mario_action)
        return;

    u32 act = sSm64OutState.action;

    // Ice Cavern ambiance — shiver while standing idle. Only nudge out of a
    // plain idle/sleep action with no input; ACT_SHIVERING itself transitions
    // back to walking when the stick moves, so movement is unaffected.
    if (play->sceneNum == SCENE_ICE_CAVERN) {
        Input* in = &play->state.input[0];
        u8 idleNoInput = (in->rel.stick_x == 0) && (in->rel.stick_y == 0) &&
                         ((in->cur.button & (BTN_A | BTN_B | BTN_Z)) == 0);
        if (idleNoInput && (act & SM64_ACT_FLAG_IDLE) && (act != SM64_ACT_SHIVERING)) {
            p_sm64_set_mario_action(sSm64MarioId, SM64_ACT_SHIVERING);
        }
    }
}

// =============================================================================
// Mario-mode Odyssey moves on the freed C-buttons (camera forced free-cam):
//   C-Left  → Cappy throw. The VARIANT is read from the stick at the press:
//             airborne → DIVE, stick spun → SPIN, stick up → UP, else FORWARD.
//             Grounded variants force ACT_CAP_THROW (real throw anim); the thrown
//             cap (Sm64Cappy_Throw) homes/orbits, stuns, and can be cap-bounced.
//   C-Right → roll: ACT_ROLL, a forward-spinning momentum roll.
// Both consume their press so the Ivan C-button item handler doesn't also fire.
// Setting the action before the tick lets libsm64 play it this frame.
// =============================================================================

// Stick-rotation detector for the spin throw: accumulate the signed angle the
// analog stick sweeps while deflected; a full-ish rotation arms SPIN for a few
// frames. Uses the cross/dot of consecutive stick vectors so there's no angle
// wrap-around to track.
static s16 Sm64Mario_StickSpinReady(Input* in) {
    static f32 sAccum = 0.0f;
    static f32 sPrevX = 0.0f, sPrevZ = 0.0f;
    static s16 sReady = 0;

    f32 sx = (f32)in->rel.stick_x;
    f32 sz = (f32)in->rel.stick_y;
    if (sx * sx + sz * sz > (50.0f * 50.0f)) { // only a near-fully-deflected stick
        if (sPrevX != 0.0f || sPrevZ != 0.0f) {
            f32 cross = sPrevX * sz - sPrevZ * sx;
            f32 dot = sPrevX * sx + sPrevZ * sz;
            f32 d = atan2f(cross, dot);
            if (fabsf(d) > 0.22f) {  // only count FAST rotation (a real spin)
                sAccum += d;
            } else {
                sAccum *= 0.5f;      // slow aim/turn -> decay, don't accumulate
            }
        }
        sPrevX = sx;
        sPrevZ = sz;
        if (fabsf(sAccum) > 11.0f) { // ~1.75 fast rotations — a deliberate spin
            sReady = 12;
            sAccum = 0.0f;
        }
    } else {
        sAccum = 0.0f;               // stick released -> full reset
        sPrevX = sPrevZ = 0.0f;
    }
    if (sReady > 0) sReady--;
    return sReady;
}

static void Sm64Mario_HandleMoves(PlayState* play) {
    if (sSm64MarioId < 0 || !p_sm64_set_mario_action) return;
    // No Cappy / roll while a transform cap is active (Wing / Metal / Vanish /
    // Fire Flower) — those power-ups own Mario's moveset.
    if (Sm64MarioCaps_GetActiveIndex() >= 0) return;
    Input* in = &play->state.input[0];
    u8 grounded = !(sSm64OutState.action & SM64_ACT_FLAG_AIR);

    s16 spinReady = Sm64Mario_StickSpinReady(in);

    if (CHECK_BTN_ALL(in->press.button, BTN_CLEFT)) {
        in->press.button &= ~BTN_CLEFT;
        in->cur.button &= ~BTN_CLEFT;

        s32 mode;
        if (!grounded) {
            mode = SM64_CAPPY_DIVE;             // air throw
        } else if (spinReady > 0) {
            mode = SM64_CAPPY_SPIN;             // spun the stick
        } else {
            mode = SM64_CAPPY_FWD;
        }
        // Grounded variants play the real throw anim; the air dive keeps Mario's
        // air state (no ground action) so the cap just flies out mid-air.
        if (grounded) {
            p_sm64_set_mario_action(sSm64MarioId, SM64_ACT_CAP_THROW);
        }
        Sm64Cappy_Throw(play, mode, mode != SM64_CAPPY_SPIN);
        return;
    }

    if (CHECK_BTN_ALL(in->press.button, BTN_CRIGHT)) {
        in->press.button &= ~BTN_CRIGHT;
        in->cur.button &= ~BTN_CRIGHT;
        if (grounded) {
            p_sm64_set_mario_action(sSm64MarioId, SM64_ACT_ROLL);
        }
        return;
    }
}

// True while Mario is performing his boss-room "super attack" — currently the
// spin (ACT_TWIRLING). Read by boss_super_damage so the reworked bosses take
// FD-style paralyze-or-damage from the spin. Implicitly gated to boss rooms:
// only those bosses query boss_super_damage, so regular enemies still take the
// twirl's normal contact damage. (Fireball/Fire Flower will OR in here later.)
u8 Sm64Mario_IsSuperAttacking(void) {
    return (CVarGetInteger("gSm64Mario", 0) != 0) && (sSm64MarioId >= 0) &&
           (sSm64OutState.action == SM64_ACT_TWIRLING || sSm64OutState.action == SM64_ACT_ROLL);
}

// Mario's independent health as a 0..8 wedge count (the SM64 power-meter
// segments: full 0x880 → 8). Read by the HUD's HP dial. Returns 8 when Mario
// isn't active yet so the dial doesn't flash empty during creation.
s32 Sm64Mario_GetHealthWedges(void) {
    if (sSm64MarioId < 0) {
        return 8;
    }
    s32 w = (s32)sSm64OutState.health >> 8; // each segment = 0x100
    if (w < 0) {
        w = 0;
    }
    if (w > 8) {
        w = 8;
    }
    return w;
}

// Called from OOT's Health_ChangeBy (z_parameter.c) on every positive health
// change. While Mario mode is enabled we accumulate the healed quarter-hearts;
// Sm64Mario_Update drains them into a libsm64 heal (see sSm64PendingHealQuarters).
// Routing all heals through here — instead of diffing gSaveContext.health — is
// what makes heart CONTAINERS / fairies / potions actually heal Mario, including
// heals that land mid-cutscene while Mario is suspended. No-op when Mario is off.
void Sm64Mario_QueueOotHeal(s16 healthChangeQuarters) {
    if (healthChangeQuarters <= 0) {
        return;
    }
    if (CVarGetInteger("gSm64Mario", 0) == 0) {
        return; // not in Mario mode — let OOT heal Link normally
    }
    sSm64PendingHealQuarters += healthChangeQuarters;
    if (sSm64PendingHealQuarters > SM64_MARIO_MAX_HP) {
        sSm64PendingHealQuarters = SM64_MARIO_MAX_HP; // clamp (full bar is plenty)
    }
}

// OOT door/exit "walk-through" action funcs. Non-static in z_player.c (which
// compares this->actionFunc against these by identity itself, e.g. z_player.c
// ~12072), just absent from functions.h — so a forward extern lets us detect
// them by pointer. doorType is only a 1-frame latch that Player_UpdateCommon
// nulls (z_player.c:13089) BEFORE Sm64Mario_Update runs, so the action-func
// identity is the ONLY signal that persists across the multi-frame door walk.
extern void Player_Action_80845EF8(Player* this, PlayState* play); // knob door: open + walk-through
extern void Player_Action_80845CA4(Player* this, PlayState* play); // sliding door + entrance/exit walk

// TRUE when OOT must own the player this frame (a scripted sequence is running).
// Used at BOTH enforcement points so they can never diverge:
//   (A) the z_player hook leaves PLAYER_STATE3_PAUSE_ACTION_FUNC CLEAR when this
//       is true, so OOT's action func runs the door/void/cutscene/exit to
//       completion. (The door START frame still installs its action via the
//       handler block at z_player.c:13023 — at that pre-UpdateCommon point
//       neither the action func nor IN_CUTSCENE is set yet, so this is false and
//       PAUSE is left set, letting Player_ActionHandler_1 install + run it.)
//   (B) Sm64Mario_Update PARKS libsm64 (pin to Link, zero velocity, idle, skip
//       tick, no write-back) so it never fights the scripted move.
// Player_InBlockingCsMode covers cutscene/csAction/transition-start/loading/
// magic/hookshot-fly; the explicit flags cover talk/item-get/ledge; the action-
// func identity covers the multi-frame door + entrance/exit walk (incl. FAKE
// doors, which after frame 0 set neither doorType nor IN_CUTSCENE).
s32 Sm64Mario_OotIsScriptingPlayer(PlayState* play, Player* p) {
    if (Player_InBlockingCsMode(play, p)) {
        return 1;
    }
    if (p->stateFlags1 & (PLAYER_STATE1_TALKING | PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_IN_ITEM_CS |
                          PLAYER_STATE1_CLIMBING_LEDGE | PLAYER_STATE1_HANGING_OFF_LEDGE)) {
        return 1;
    }
    // #6 First-person look: while Mario is in OOT's first-person mode, let OOT own
    // the player (run its look action + camera) and park libsm64 — Mario stands
    // still and isn't visible from his own eyes, so the frozen idle is invisible.
    if (p->stateFlags1 & PLAYER_STATE1_FIRST_PERSON) {
        return 1;
    }
    if (p->actionFunc == Player_Action_80845EF8 || p->actionFunc == Player_Action_80845CA4) {
        return 1;
    }
    return 0;
}

void Sm64Mario_Update(PlayState* play, Player* player) {
    Camera* cam;
    float lookX, lookZ, lookMag, waterY;
    Input* input;
    struct SM64MarioInputs inputs;

    // Allow sSm64MarioId < 0 through so the scene-change branch can retry
    // mario_create after a failed attempt. Without this, a single failed create
    // post-transition would pin the state forever and leave an invisible Link.
    if (!sSm64Initialized || !p_sm64_mario_tick)
        return;

    // Z-targeting now WORKS for Mario, driven by R: z_player.c swaps Z<->R in the
    // OOT input copy (sp44) so OOT's lock-on (which reads BTN_Z) fires on physical
    // R. We no longer clear PLAYER_STATE1_Z_TARGETING here — that suppression was
    // what stopped targeting. (Mario's jump/punch still come from the REAL buttons
    // read below; physical Z is mapped to jump.)

    // Scene change: nuke Mario immediately so IsReady() becomes false and
    // Link falls back to normal rendering. Otherwise the old Mario (with
    // stale position from the previous scene) keeps "existing" invisibly
    // far off-camera while the old scene's surfaces are still loaded in
    // libsm64, and the draw hook hides Link. Retry create every frame
    // until the new scene's collision is ready.
    if (play->sceneNum != sSm64LastSceneNum) {
        // Throttled diagnostic: log which gate is keeping us out, once per
        // distinct reason plus every 60 frames. Key for diagnosing "Mario
        // never re-creates in scene X" silent failures.
        static u32 sScBlockReasonPrev = 0xFFFF;
        static u32 sScBlockFrames = 0;
        u32 blockReason = 0;
        // Do NOT block on PLAYER_STATE1_LOADING — Init path doesn't, and Init
        // works for the same scene via CVAR toggle. colHeader + numPolygons > 0
        // are the only real safety gates.
        if (play->colCtx.colHeader == NULL) blockReason = 2;
        else if (play->colCtx.colHeader->numPolygons == 0) blockReason = 3;

        // Step 1 — always: drop old Mario + state. Safe to call every frame
        // while retrying; these are idempotent.
        if (sSm64MarioId >= 0 && p_sm64_mario_delete) {
            p_sm64_mario_delete(sSm64MarioId);
            sSm64MarioId = -1;
        }
        sSm64OutBuffers.numTrianglesUsed = 0; // stop rendering stale mesh
        sSm64SurfacesForScene = -1;

        // Step 2 — gate creation on collision availability only.
        if (blockReason != 0) {
            if (blockReason != sScBlockReasonPrev || (sScBlockFrames % 60) == 0) {
                lusprintf(__FILE__, __LINE__, 2,
                    "[SM64] Scene-change blocked reason=%u scene=%d flags1=0x%08x colHeader=%p numPolys=%d",
                    blockReason, play->sceneNum, player->stateFlags1,
                    (void*)play->colCtx.colHeader,
                    play->colCtx.colHeader ? play->colCtx.colHeader->numPolygons : -1);
                sScBlockReasonPrev = blockReason;
            }
            sScBlockFrames++;
            return;
        }
        sScBlockReasonPrev = 0xFFFF;
        sScBlockFrames = 0;

        // Step 3 — load surfaces for the new scene. A 0 return means
        // extraction found nothing valid yet (rare but possible right
        // after scene init); try again next frame.
        {
            u32 loaded = Sm64_LoadSceneSurfaces(play);
            if (loaded == 0) {
                static u32 sScZeroFrames = 0;
                if ((sScZeroFrames % 60) == 0) {
                    lusprintf(__FILE__, __LINE__, 2,
                        "[SM64] Scene-change: LoadSceneSurfaces returned 0 scene=%d numPolys=%d",
                        play->sceneNum, play->colCtx.colHeader->numPolygons);
                }
                sScZeroFrames++;
                return;
            }
            sSm64SurfacesForScene = play->sceneNum;
        }

        // Step 4 — create Mario (OOT pos scaled into libsm64 world).
        if (p_sm64_mario_create) {
            sSm64MarioId = p_sm64_mario_create(
                player->actor.world.pos.x * SM64_WORLD_SCALE,
                player->actor.world.pos.y * SM64_WORLD_SCALE,
                player->actor.world.pos.z * SM64_WORLD_SCALE);
            lusprintf(__FILE__, __LINE__, 2, "[SM64] Scene change: create id=%d pos=(%.0f,%.0f,%.0f) scene=%d",
                sSm64MarioId, player->actor.world.pos.x, player->actor.world.pos.y,
                player->actor.world.pos.z, play->sceneNum);
        }

        if (sSm64MarioId >= 0) {
            // Independent health: restore Mario's carried-over HP across the scene change.
            if (p_sm64_set_mario_health) {
                p_sm64_set_mario_health(sSm64MarioId,
                    (sMarioHealthPersist > 0) ? (u16)sMarioHealthPersist : (u16)SM64_MARIO_MAX_HP);
                sMarioLinkMirrorHP = -1;
            }
            sSm64LastSceneNum = play->sceneNum;
            // Re-install the sentinel held-object on the new Mario instance —
            // mario_create resets gMarioState including usedObj, so the dummy
            // pointer needs to be reattached after every recreate.
            if (p_sm64_mario_grab_dummy) {
                p_sm64_mario_grab_dummy(sSm64MarioId);
            }
            // Prime a zero-input tick so Draw has a valid mesh THIS frame.
            // Otherwise numTrianglesUsed stays 0 and the draw hook (which hides
            // Link once sSm64MarioId >= 0) renders nothing → both invisible.
            {
                struct SM64MarioInputs zeroInputs;
                memset(&zeroInputs, 0, sizeof(zeroInputs));
                zeroInputs.camLookZ = 1.0f; // non-zero to avoid internal div-by-0
                sSm64OutBuffers.numTrianglesUsed = 0;
                p_sm64_mario_tick(sSm64MarioId, &zeroInputs, &sSm64OutState, &sSm64OutBuffers);
            }
        }
        return;
    }

    // Same-scene frame after a failed create: nothing to tick. The scene-change
    // branch above keeps retrying because sSm64LastSceneNum stays pinned.
    if (sSm64MarioId < 0)
        return;

    // (Carry animation bridge removed — set_mario_action(ACT_HOLD_IDLE /
    // ACT_THROWING) crashes inside libsm64 because both actions deref
    // gMarioState->usedObj which we never populate. Mario keeps his normal
    // anim during carries; the actor still gets visually pinned via OOT's
    // heldActor system or our own B-grab in TryGrabOrThrow.)

    // =========================================================================
    // YIELD to OOT scripted-player sequences: door/exit walk-through, void-fall,
    // cutscenes, scene/room transitions, talk, item-get, ledge climb. ONE
    // predicate (Sm64Mario_OotIsScriptingPlayer) decides; the z_player hook uses
    // the SAME predicate to leave PLAYER_STATE3_PAUSE_ACTION_FUNC clear so OOT's
    // action func actually RUNS and completes the sequence. Here we PARK libsm64
    // so it never fights that scripted move:
    //   - pin libsm64's internal position to Link's (OOT-scripted) world.pos;
    //   - ZERO both velocity reps (set_mario_velocity + forward) so there is no
    //     leftover momentum to fling Mario when the tick resumes — this is the
    //     fix for the door slide/softlock (the old no-tick defer FROZE velocity
    //     instead of zeroing it, so it re-applied on resume; worse when airborne,
    //     where there is no ground friction to ever stop the slide);
    //   - force a grounded idle action;
    //   - SKIP the tick (a scripted position can NULL-deref sm64_mario_tick — see
    //     the no-tick note below) and RETURN before the write-back, so OOT's
    //     scripted world.pos survives and the mesh follows Link via the draw delta.
    //
    // WATCHDOG: a lingering IN_CUTSCENE (cutscene already ended, csCtx idle, no
    // transition/door/void) once stranded Mario "until you toggle the CVAR".
    // If we park many frames with nothing actually scripting, break out and tick.
    // =========================================================================
    if (Sm64Mario_OotIsScriptingPlayer(play, player)) {
        static u32 sParkFrames = 0;
        u8 reallyScripted =
            (play->transitionTrigger != TRANS_TRIGGER_OFF) || (play->csCtx.state != CS_STATE_IDLE) ||
            (player->csAction != 0) || (player->actionFunc == Player_Action_80845EF8) ||
            (player->actionFunc == Player_Action_80845CA4) ||
            (player->stateFlags1 & (PLAYER_STATE1_LOADING | PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_IN_ITEM_CS |
                                    PLAYER_STATE1_TALKING | PLAYER_STATE1_CLIMBING_LEDGE |
                                    PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_FIRST_PERSON));
        if (reallyScripted) {
            sParkFrames = 0;
        } else {
            sParkFrames++;
        }
        if (sParkFrames < 120) { // ~2s grace before breaking a stuck park
            f32 px = player->actor.world.pos.x * SM64_WORLD_SCALE;
            f32 py = player->actor.world.pos.y * SM64_WORLD_SCALE;
            f32 pz = player->actor.world.pos.z * SM64_WORLD_SCALE;
            if (p_sm64_set_mario_position) {
                p_sm64_set_mario_position(sSm64MarioId, px, py, pz);
            }
            if (p_sm64_set_mario_velocity) {
                p_sm64_set_mario_velocity(sSm64MarioId, 0.0f, 0.0f, 0.0f);
            }
            if (p_sm64_set_mario_forward_velocity) {
                p_sm64_set_mario_forward_velocity(sSm64MarioId, 0.0f);
            }

            // #3 Mario's own door animation. While OOT walks Link through a door
            // (knob = Player_Action_80845EF8, sliding = ...CA4), play Mario's SM64
            // door-open anim instead of freezing his last pose. We can't run the
            // physics tick at a scripted position (it can NULL-deref), so force the
            // anim + advance its frame and regenerate the mesh with the GEOMETRY-
            // ONLY puppet tick (same safe path as the remote-Mario renderer). Pin
            // the readback position so Sm64Mario_Draw's (link − mario) delta stays
            // ~0 and the door-pose mesh draws right at Link.
            u8 inDoor = (player->actionFunc == Player_Action_80845EF8) ||
                        (player->actionFunc == Player_Action_80845CA4);
            if (inDoor && p_sm64_set_mario_animation && p_sm64_set_mario_anim_frame &&
                p_sm64_mario_tick_puppet && p_sm64_set_mario_faceangle) {
                // On the first door frame, snap the travel reference to Link so the
                // first-frame delta is 0 (face Link while the door opens in place).
                if (sSm64DoorAnimFrame == 0) {
                    sSm64DoorPrevX = player->actor.world.pos.x;
                    sSm64DoorPrevZ = player->actor.world.pos.z;
                }
                // Face Mario along his ACTUAL movement (Link is being walked through
                // the door). Following the travel vector instead of Link's
                // shape.rot.y — which the door briefly flips — keeps Mario walking
                // INTO the door, fixing the "exits backwards" look. Falls back to
                // Link's facing while stationary (door still opening).
                f32 ddx = player->actor.world.pos.x - sSm64DoorPrevX;
                f32 ddz = player->actor.world.pos.z - sSm64DoorPrevZ;
                s16 faceYaw = player->actor.shape.rot.y;
                if ((ddx * ddx + ddz * ddz) > 0.25f) {
                    faceYaw = Math_Atan2S(ddx, ddz); // OOT yaw of the (dx,dz) heading
                }
                sSm64DoorPrevX = player->actor.world.pos.x;
                sSm64DoorPrevZ = player->actor.world.pos.z;
                p_sm64_set_mario_faceangle(sSm64MarioId, (f32)faceYaw * 3.14159f / 32768.0f);
                p_sm64_set_mario_animation(sSm64MarioId, 0x60); // MARIO_ANIM_PUSH_DOOR_WALK_IN
                p_sm64_set_mario_anim_frame(sSm64MarioId, sSm64DoorAnimFrame);
                if (sSm64DoorAnimFrame < 28) {
                    sSm64DoorAnimFrame++;
                }
                p_sm64_mario_tick_puppet(sSm64MarioId, &sSm64OutBuffers);
                sSm64OutState.position[0] = px;
                sSm64OutState.position[1] = py;
                sSm64OutState.position[2] = pz;
            } else if (p_sm64_set_mario_action) {
                p_sm64_set_mario_action(sSm64MarioId, SM64_ACT_IDLE);
            }
            Sm64Audio_RefillRing();
            return;
        }
        // watchdog tripped — fall through and tick normally (anti-stuck escape)
    }

    // No-tick defer — during item-get cutscenes, demo cutscenes, door
    // open/walk-through anims, and any PlayerCs (Player_InCsMode) sequence,
    // OOT script-moves Link to precise scripted positions. Some of those
    // positions (e.g. Kakariko heart-piece pickup) make libsm64's internal
    // surface lookup deref NULL inside sm64_mario_tick (observed crash:
    // RAX=0 at sm64.dll!sm64_mario_tick). Bail out of the tick entirely
    // for these states.
    //
    // We DO call sm64_set_mario_position to keep libsm64's internal Mario
    // position in sync with where the cutscene moves Link — that call is
    // safe (no surface lookup) and ensures that when ticking resumes after
    // the cutscene, Mario picks up at Link's new position instead of the
    // pre-cutscene one (otherwise Link gets snapped back across a door).
    //
    // Sm64Mario_Draw computes (linkPos − marioStalePos) and shifts the
    // mesh visually too, so Mario stays visible throughout the cutscene.
    // Audio refill keeps SM64 SFX from starving.
    if ((player->stateFlags1 & (PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_IN_ITEM_CS)) ||
        Player_InCsMode(play)) {
        // Update libsm64's INTERNAL Mario position each frame so when the
        // tick resumes after the cutscene, Mario starts at Link's current
        // (post-cutscene) position. Without this, walking through a door
        // would tick Mario at the pre-door position on cutscene end and
        // immediately snap Link back across the door.
        // We deliberately do NOT touch sSm64OutState.position here — that
        // value is what Sm64Mario_Draw reads to compute the render-time
        // delta. Keeping it stale means the delta tracks Link's cutscene
        // movement and the mesh visually slides along with him.
        if (p_sm64_set_mario_position && sSm64MarioId >= 0) {
            p_sm64_set_mario_position(sSm64MarioId,
                player->actor.world.pos.x * SM64_WORLD_SCALE,
                player->actor.world.pos.y * SM64_WORLD_SCALE,
                player->actor.world.pos.z * SM64_WORLD_SCALE);
        }
        Sm64Audio_RefillRing();
        return;
    }

    // Hard-defer states — vanilla interaction in progress (textbox, grabbing
    // /carrying, ledge climb, loading zone). Safe to zero-input tick — these
    // don't crash libsm64, they just need Mario's physics paused so his
    // mesh mirrors Link while the vanilla action func drives the anim.
    if (player->stateFlags1 & (PLAYER_STATE1_LOADING |
                                PLAYER_STATE1_TALKING |
                                // CARRYING_ACTOR NOT deferred: Mario keeps ticking
                                // (walks with the held item, which TryGrabOrThrow
                                // pins to his hands) instead of freezing.
                                PLAYER_STATE1_CLIMBING_LEDGE |
                                PLAYER_STATE1_HANGING_OFF_LEDGE)) {
        // Only zero velocity for pure transitions — for TALKING/CARRYING the
        // vanilla action func manages velocity itself, so don't stomp it.
        if (player->stateFlags1 & PLAYER_STATE1_LOADING) {
            player->linearVelocity = 0.0f;
            player->actor.velocity.x = 0.0f;
            player->actor.velocity.y = 0.0f;
            player->actor.velocity.z = 0.0f;
        }
        if (p_sm64_set_mario_position) {
            p_sm64_set_mario_position(sSm64MarioId,
                player->actor.world.pos.x * SM64_WORLD_SCALE,
                player->actor.world.pos.y * SM64_WORLD_SCALE,
                player->actor.world.pos.z * SM64_WORLD_SCALE);
        }
        if (p_sm64_mario_tick) {
            struct SM64MarioInputs zi;
            memset(&zi, 0, sizeof(zi));
            zi.camLookZ = 1.0f;
            sSm64OutBuffers.numTrianglesUsed = 0;
            p_sm64_mario_tick(sSm64MarioId, &zi, &sSm64OutState, &sSm64OutBuffers);
        }
        return;
    }

    // Soft defer — yield to OOT during scripted cutscenes / first-person aim
    // / item-get sequences BUT let the user break out by pressing stick or a
    // button. Without the escape hatch, scenes where IN_CUTSCENE lingers
    // post-scene-change leave Mario frozen; the user reported "after scene
    // change, actors don't interact — have to toggle CVAR".
    {
        s32 scriptedCs = (play->csCtx.state != CS_STATE_IDLE);
        Input* userInputProbe = &play->state.input[0];
        s32 userWantsControl =
            (userInputProbe->rel.stick_x != 0) ||
            (userInputProbe->rel.stick_y != 0) ||
            (userInputProbe->cur.button & (BTN_A | BTN_B | BTN_Z | BTN_R)) != 0;
        u32 softDefer = !userWantsControl && (
            (player->stateFlags1 & PLAYER_STATE1_FIRST_PERSON) ||
            (player->stateFlags1 & PLAYER_STATE1_IN_ITEM_CS) ||
            ((player->stateFlags1 & PLAYER_STATE1_IN_CUTSCENE) && scriptedCs));
        if (softDefer) {
            player->linearVelocity = 0.0f;
            player->actor.velocity.x = 0.0f;
            player->actor.velocity.y = 0.0f;
            player->actor.velocity.z = 0.0f;
            if (p_sm64_set_mario_position) {
                p_sm64_set_mario_position(sSm64MarioId,
                    player->actor.world.pos.x * SM64_WORLD_SCALE,
                    player->actor.world.pos.y * SM64_WORLD_SCALE,
                    player->actor.world.pos.z * SM64_WORLD_SCALE);
            }
            if (p_sm64_mario_tick) {
                struct SM64MarioInputs zi;
                memset(&zi, 0, sizeof(zi));
                zi.camLookZ = 1.0f;
                sSm64OutBuffers.numTrianglesUsed = 0;
                p_sm64_mario_tick(sSm64MarioId, &zi, &sSm64OutState, &sSm64OutBuffers);
            }
            return;
        }
    }

    // Damage/talking/item/dead: sync position but keep ticking so Mario keeps
    // animating in place. These states don't invalidate the camera.
    if (player->stateFlags1 & (PLAYER_STATE1_DAMAGED | PLAYER_STATE1_TALKING |
                                PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_DEAD)) {
        if (p_sm64_set_mario_position) {
            p_sm64_set_mario_position(sSm64MarioId,
                player->actor.world.pos.x * SM64_WORLD_SCALE,
                player->actor.world.pos.y * SM64_WORLD_SCALE,
                player->actor.world.pos.z * SM64_WORLD_SCALE);
        }
        // Fall through to tick so the animation state machine advances.
    }

    // Build inputs
    cam = GET_ACTIVE_CAM(play);
    if (cam == NULL) {
        // Camera not set up yet (rare transient during cam swaps). Skip one frame.
        return;
    }
    lookX = cam->at.x - cam->eye.x;
    lookZ = cam->at.z - cam->eye.z;
    lookMag = sqrtf(lookX * lookX + lookZ * lookZ);
    if (lookMag < 0.001f) lookMag = 0.001f;

    input = &play->state.input[0];

    memset(&inputs, 0, sizeof(inputs));
    inputs.camLookX = lookX / lookMag;
    inputs.camLookZ = lookZ / lookMag;
    inputs.stickX = (float)input->rel.stick_x / 64.0f;
    inputs.stickY = -(float)input->rel.stick_y / 64.0f;
    // Mario's real buttons: A = jump, B = punch, Z = crouch / ground-pound (SM64
    // default). R is freed for OOT Z-targeting via the Z<->R swap in z_player.c,
    // which only touches OOT's input copy — these REAL buttons are untouched.
    inputs.buttonA = (input->cur.button & BTN_A) ? 1 : 0;
    inputs.buttonB = (input->cur.button & BTN_B) ? 1 : 0;
    inputs.buttonZ = (input->cur.button & BTN_Z) ? 1 : 0;

    // Fire Flower (Fire cap active): B still PUNCHES (libsm64 keeps buttonB via
    // cur), and additionally invokes a fireball. The helper reads the B *press*
    // edge and consumes it (so the proximity grab doesn't also fire) — the punch
    // is driven from cur, so it plays normally.
    if (Sm64MarioCaps_IsFireActive()) {
        Sm64Mario_FireballOnBPress(play, player);
    }
    // Always advance in-flight fireballs (unconditional) so balls already thrown
    // keep arcing/bouncing/burning even if the Fire cap times out mid-flight.
    Sm64Mario_UpdateFireballs(play);
    // Advance the thrown cap (Cappy): out/hover/return + cap-bounce detection.
    Sm64Cappy_Update(play);

    // Water level — Sm64Surfaces_GetWaterLevel returns OOT-scale Y; scale up for libsm64.
    if (p_sm64_set_mario_water_level) {
        waterY = Sm64Surfaces_GetWaterLevel(play, player->actor.world.pos.x, player->actor.world.pos.z);
        p_sm64_set_mario_water_level(sSm64MarioId, (int)(waterY * SM64_WORLD_SCALE));
    }

    // Damage / environment behavior anims — map OOT shock/burn/cold onto
    // Mario's native reaction action so the tick below plays it. Runs only in
    // normal play (the cutscene/defer branches above already returned).
    Sm64Mario_ApplyBehaviorAnims(play, player);

    // Mario-mode special moves (X/Y on the freed C-buttons). After the env
    // anims so a deliberate spin overrides the idle shiver; before the tick so
    // libsm64 plays the forced action this frame.
    Sm64Mario_HandleMoves(play);

    // Tick SM64 at ~30 fps effective (SM64's native rate) regardless of OOT's
    // gameplay rate. OOT runs at 60 / R_UPDATE_RATE fps (R_UPDATE_RATE=3 → 20 fps).
    // Ticking once per OOT frame at 20 fps leaves Mario running at 67% of SM64
    // speed, which is why movement feels wrong. Time accumulator fixes it:
    //   20 fps OOT → ~1.5 ticks/frame average (pattern: 1,2,1,2,...)
    //   30 fps OOT → 1 tick/frame
    //   60 fps OOT → 1 tick every 2 frames
    // Reached the normal physics tick → Mario isn't in a door; reset the door-anim
    // cursor so the next door replays Mario's open animation from frame 0 (#3).
    sSm64DoorAnimFrame = 0;
    {
        static float sTickAccum = 0.0f;
        const float SM64_TICK_DT = 1.0f / 30.0f;
        int rate = R_UPDATE_RATE;
        if (rate < 1) rate = 3;            // guard against weird values
        float ootFrameDt = (float)rate / 60.0f;
        sTickAccum += ootFrameDt;
        int ticksThisFrame = 0;
        while (sTickAccum >= SM64_TICK_DT) {
            sSm64OutBuffers.numTrianglesUsed = 0;
            p_sm64_mario_tick(sSm64MarioId, &inputs, &sSm64OutState, &sSm64OutBuffers);
            sTickAccum -= SM64_TICK_DT;
            if (++ticksThisFrame >= 3) {   // cap "spiral of death" if we fall behind
                sTickAccum = 0.0f;
                break;
            }
        }
        // If accum < SM64_TICK_DT this frame, no tick: previous mesh is reused.
        // That's intentional — keeps Mario at 30 fps physics even on 60 fps OOT.
    }

    // Independent Mario health: MARIO is the source of truth (his own 8-segment
    // bar = 8 hits; see InterceptDamage's take_damage(1) per hit). We mirror
    // Mario → Link's gSaveContext.health so the OOT game-over still fires when
    // Mario dies, and we detect OOT recovery (a Link-health increase from a
    // heart/fairy) and forward it to Mario so healing works "the same".
    if (sSm64MarioId >= 0) {
        s16 linkMax = gSaveContext.healthCapacity;
        if (linkMax > 0) {
            // OOT heal → Mario heal, driven by the explicit queue fed from
            // Health_ChangeBy (Sm64Mario_QueueOotHeal). This catches EVERY heal
            // event — recovery hearts, heart CONTAINERS, fairies, potions — and
            // those applied while Mario was suspended during an item-get cutscene
            // (they accumulate and drain here on resume). The old delta-vs-mirror
            // detection missed them because the Mario→Link mirror below overwrote
            // the very gSaveContext.health it diffed against.
            // healCounter units: 4 per segment, 32 = full bar.
            if (sSm64PendingHealQuarters > 0 && p_sm64_mario_heal) {
                s32 healCounter = (sSm64PendingHealQuarters * 32 + linkMax - 1) / linkMax;
                if (healCounter > 255) {
                    healCounter = 255;
                }
                if (healCounter > 0) {
                    p_sm64_mario_heal(sSm64MarioId, (u8)healCounter);
                }
                sSm64PendingHealQuarters = 0;
            }
            // Mirror Mario → Link.
            s32 marioHP = sSm64OutState.health;
            if (marioHP < 0) {
                marioHP = 0;
            }
            if (marioHP < 0x100) {
                // Mario dead → set Link's HP to 0 so OOT's death/game-over runs
                // (IsActive yields). Forget the saved health so the respawn
                // recreate starts Mario at a full 8-segment bar.
                gSaveContext.health = 0;
                sMarioLinkMirrorHP = 0;
                sMarioHealthPersist = -1;
            } else {
                s16 linkHP = (s16)(((s32)marioHP * linkMax) / SM64_MARIO_MAX_HP);
                if (linkHP < 1) {
                    linkHP = 1;
                }
                if (linkHP > linkMax) {
                    linkHP = linkMax;
                }
                gSaveContext.health = linkHP;
                sMarioLinkMirrorHP = linkHP;
                sMarioHealthPersist = (s16)marioHP;
            }
        }
    }

    // Vanish-cap collision swap. When Din's Fire (mapped to vanish cap)
    // activates, Mario should phase through walls + ceilings but still
    // detect floors. Implementation: re-upload only floor-like surfaces to
    // libsm64 while the cap flag is on, full surfaces when it clears.
    // Detected via sSm64OutState.flags & MARIO_VANISH_CAP — libsm64 also
    // auto-expires the cap after its internal timer.
    {
        static u8  sVanishCapPrev = 0;
        static u32 sSurfRefresh = 0;
        static u32 sDynaSig = 0xFFFFFFFFu; // signature of the last-uploaded dynapoly state
        static u8  sDidInitialLoad = 0;
        u8 vanishNow = (sSm64OutState.flags & SM64_MARIO_VANISH_CAP) != 0;
        u8 vanishEdge = (vanishNow != sVanishCapPrev);

        // Cheap signature of the LIVE dynapoly (active BgActors + their transforms).
        // Static scene collision never changes, so an unchanged signature means a
        // re-upload would produce identical surfaces — skip it (#4 lag fix).
        u32 dynaSig = 0u;
        {
            DynaCollisionContext* dyna = &play->colCtx.dyna;
            s32 bgId;
            for (bgId = 0; bgId < BG_ACTOR_MAX; bgId++) {
                if (!(dyna->bgActorFlags[bgId] & 1)) {
                    continue;
                }
                Vec3f* p = &dyna->bgActors[bgId].curTransform.pos;
                Vec3s* r = &dyna->bgActors[bgId].curTransform.rot;
                dynaSig = dynaSig * 31u + (u32)bgId;
                dynaSig = dynaSig * 31u + (u32)(s32)p->x;
                dynaSig = dynaSig * 31u + (u32)(s32)p->y;
                dynaSig = dynaSig * 31u + (u32)(s32)p->z;
                dynaSig = dynaSig * 31u + (u32)(u16)r->y;
            }
        }

        sSurfRefresh++;
        u8 sigChanged = (dynaSig != sDynaSig);
        // Rebuild on: vanish edge (swaps the floorOnly set), the very first load,
        // a dynapoly change that's had at least _FRAMES since the last rebuild, or
        // the _MAX safety net. A continuously-moving platform thus rebuilds at most
        // every _FRAMES (not every frame); a fully static scene loads ONCE then only
        // hits the rare safety net.
        u8 due = vanishEdge || !sDidInitialLoad ||
                 (sigChanged && sSurfRefresh >= SM64_SURFACE_REFRESH_FRAMES) ||
                 (sSurfRefresh >= SM64_SURFACE_REFRESH_MAX);
        if (due) {
            Sm64_LoadSceneSurfacesEx(play, vanishNow);
            sSurfRefresh = 0;
            sDynaSig = dynaSig;
            sDidInitialLoad = 1;
            if (vanishEdge) {
                sVanishCapPrev = vanishNow;
                lusprintf(__FILE__, __LINE__, 2, "[SM64] Vanish cap %s — surfaces swapped (floorOnly=%d)",
                    vanishNow ? "ACTIVATED" : "EXPIRED", vanishNow);
            }
        }
    }

    // SM64 drives position — write back to Link
    // libsm64 coords are SM64_WORLD_SCALE× OOT coords — unscale on readback.
    player->actor.world.pos.x = sSm64OutState.position[0] / SM64_WORLD_SCALE;
    player->actor.world.pos.y = sSm64OutState.position[1] / SM64_WORLD_SCALE;
    player->actor.world.pos.z = sSm64OutState.position[2] / SM64_WORLD_SCALE;
    player->actor.shape.rot.y = (s16)(sSm64OutState.faceAngle / 3.14159f * 32768.0f);
    player->actor.world.rot.y = player->actor.shape.rot.y;
    player->linearVelocity = sSm64OutState.forwardVelocity / SM64_WORLD_SCALE;
    player->actor.velocity.y = sSm64OutState.velocity[1] / SM64_WORLD_SCALE;

    // Keep prevPos ONE FRAME behind world.pos (clamped) so OOT's wall/exit
    // bgcheck sees Mario's real per-frame displacement. This used to be
    // `prevPos = world.pos` — i.e. ZERO displacement — which makes
    // BgCheck_CheckWallImpl skip its line-sweep, so actor.wallPoly never
    // populates and wall-based loading zones / scene-exit polys never fired for
    // Mario (couldn't walk into dungeon entrances). The distance clamp drops the
    // sweep on teleports (scene change, warp, knockback) so a cross-scene jump
    // can't trigger a spurious far-wall exit or snag a wall across the map.
    {
        static Vec3f sPrevMarioPos;
        static u8 sHavePrevMarioPos = 0;
        f32 dpx = player->actor.world.pos.x - sPrevMarioPos.x;
        f32 dpy = player->actor.world.pos.y - sPrevMarioPos.y;
        f32 dpz = player->actor.world.pos.z - sPrevMarioPos.z;
        if (sHavePrevMarioPos && (dpx * dpx + dpy * dpy + dpz * dpz) < (200.0f * 200.0f)) {
            player->actor.prevPos = sPrevMarioPos;
        } else {
            player->actor.prevPos = player->actor.world.pos; // first frame / teleport: no sweep
        }
        sPrevMarioPos = player->actor.world.pos;
        sHavePrevMarioPos = 1;
    }

    // Void-out. OOT's Player_HandleExitsAndVoids reacts to a void plane by setting
    // a falling-into-void player ACTION — but with Mario active that action is
    // paused (or never armed), AND libsm64 OWNS the collision and keeps the void
    // plane SOLID (the surface extractor emits every floor as type=0), so Mario
    // just STANDS on the void plane and OOT never even sees it. We must detect it
    // Mario-side. player->floorProperty can't be trusted (it's computed against
    // Link's pre-override body and is force-zeroed on the anim/airborne branch),
    // so RAYCAST OOT collision under Mario's REAL world.pos and read the floor's
    // void property directly (func_80041EA4): 12 = void (Play_TriggerVoidOut),
    // 5 = void-with-respawn (Play_TriggerRespawn). world.pos.y < -4000 covers a
    // true bottomless pit (no floor poly at all). Gated on transitionTrigger==OFF
    // so it fires once and Mario then parks through the fade via the predicate.
    if (play->transitionTrigger == TRANS_TRIGGER_OFF &&
        !(player->stateFlags1 & (PLAYER_STATE1_LOADING | PLAYER_STATE1_DEAD))) {
        u8 doVoid = 0;
        u8 doRespawn = 0;
        if (player->actor.world.pos.y < -4000.0f) {
            doVoid = 1; // bottomless pit
        } else {
            CollisionPoly* voidPoly = NULL;
            s32 voidBgId = 0;
            Vec3f probe = player->actor.world.pos;
            probe.y += 20.0f; // start just above the feet so the floor under him is found
            f32 fy = BgCheck_EntityRaycastFloor5(play, &play->colCtx, &voidPoly, &voidBgId, &player->actor, &probe);
            if (voidPoly != NULL && fy > BGCHECK_Y_MIN && (player->actor.world.pos.y - fy) < 80.0f) {
                u32 fprop = func_80041EA4(&play->colCtx, voidPoly, voidBgId);
                if (fprop == 12) {
                    doVoid = 1;
                } else if (fprop == 5) {
                    doRespawn = 1;
                }
            }
        }
        if (doRespawn) {
            Play_TriggerRespawn(play);
            play->transitionType = TRANS_TYPE_FADE_BLACK_FAST;
            Sfx_PlaySfxCentered(NA_SE_OC_ABYSS);
        } else if (doVoid) {
            Play_TriggerVoidOut(play);
            play->transitionType = TRANS_TYPE_FADE_BLACK_FAST;
            Sfx_PlaySfxCentered(NA_SE_OC_ABYSS);
        }
    }

    // ----- bodyPartsPos / focus.pos fallback -----
    // Link's skeleton draw is skipped while Mario is showing, so OOT's
    // Player_PostLimbDraw never fires and bodyPartsPos[] / actor.focus.pos
    // stay frozen at wherever Link was when Mario took over. Anything that
    // reads those (shadow under feet, lock-on origin, boomerang return
    // target, sword/projectile attach points, mirror-shield matrix) ends
    // up acting on stale coordinates — symptom: boomerang flies back to
    // the original spawn point instead of following Mario, foot shadow
    // stays at the entry point, lock-on cone is in the wrong place.
    //
    // Mirror of mm_player_form.cpp:12909-12937 fallback. Mario height
    // ≈ 60 OOT units (matches OOT Link bumper); use it for top/center.
    {
        const f32 marioHeight = 60.0f;
        f32 midY = player->actor.world.pos.y + marioHeight * 0.5f;
        for (s32 i = 0; i < PLAYER_BODYPART_MAX; i++) {
            player->bodyPartsPos[i].x = player->actor.world.pos.x;
            player->bodyPartsPos[i].y = midY;
            player->bodyPartsPos[i].z = player->actor.world.pos.z;
        }
        player->bodyPartsPos[PLAYER_BODYPART_L_FOOT].y = player->actor.world.pos.y;
        player->bodyPartsPos[PLAYER_BODYPART_R_FOOT].y = player->actor.world.pos.y;
        player->bodyPartsPos[PLAYER_BODYPART_HEAD].y =
            player->actor.world.pos.y + marioHeight - 10.0f;

        // Foot shadow: ActorShadow_DrawFeet (the player's shadowDraw) reads
        // actor.shape.feetPos[], which Link's skeleton PostLimbDraw normally
        // writes — but that draw is skipped in Mario mode, so the foot shadow
        // stays frozen at Link's entry point. Pin both feet to Mario; the
        // shadow still raycasts each foot's own floor Y, so it sits correctly
        // on slopes/stairs and simply follows him now.
        player->actor.shape.feetPos[FOOT_LEFT].x = player->actor.world.pos.x;
        player->actor.shape.feetPos[FOOT_LEFT].y = player->actor.world.pos.y;
        player->actor.shape.feetPos[FOOT_LEFT].z = player->actor.world.pos.z;
        player->actor.shape.feetPos[FOOT_RIGHT] = player->actor.shape.feetPos[FOOT_LEFT];

        // focus.pos = lock-on origin + boomerang return target. Center on
        // Mario's torso so anything tracking him (camera C-up, boomerang)
        // converges to where his mesh actually is.
        player->actor.focus.pos.x = player->actor.world.pos.x;
        player->actor.focus.pos.y = midY;
        player->actor.focus.pos.z = player->actor.world.pos.z;
    }

    // Water surface-jump helper: real SM64 pops Mario out of water when A is
    // pressed while he's at the surface. libsm64's internal transition can
    // miss this because OOT's waterbox Y values are OOT-scale and the depth
    // heuristic inside libsm64 may not recognize "I'm right at the top" in
    // all scenes. Force-transition to ACT_WATER_JUMP when the player clearly
    // wants out and Mario is swimming near the surface.
    if (p_sm64_set_mario_action &&
        (input->press.button & BTN_A) &&
        (sSm64OutState.action & SM64_ACT_FLAG_SWIMMING) != 0) {
        f32 waterYOoT = Sm64Surfaces_GetWaterLevel(play,
            player->actor.world.pos.x, player->actor.world.pos.z);
        if (waterYOoT > -10000.0f) {
            f32 marioYOoT = sSm64OutState.position[1] / SM64_WORLD_SCALE;
            // Within ~1 Mario-head-height (20 OOT ≈ 80 SM64 units) of the
            // water surface counts as "at surface" — avoids triggering from
            // deep underwater (where Mario should swim up first).
            if (marioYOoT >= waterYOoT - 20.0f) {
                p_sm64_set_mario_action(sSm64MarioId, SM64_ACT_WATER_JUMP);
            }
        }
    }

    // B-proximity grab — runs after Mario's position is written back to
    // Link so the grabbable search uses the post-tick Mario position (via
    // player->actor.world.pos). Also pins / detaches the held actor for
    // this frame. See comment on Sm64Mario_TryGrabOrThrow above.
    Sm64Mario_TryGrabOrThrow(play, player);

    // Ivan-style item handling — C-button (and optionally D-pad) presses
    // spawn items at Mario's current position with Mario's facing yaw.
    // Mario's mesh stays visible throughout; vanilla Link FP aim is never
    // entered. Implementation lives in sm64_mario_items.c.
    Sm64Mario_HandleItems(play, player);

    // Pipe libsm64's audio output into our ring buffer. Runs on game thread
    // so the audio callback (Sm64Audio_MixInto on audio thread) only reads.
    // sm64_mario_tick above may have queued new sounds (punches, jumps,
    // splashes); sm64_audio_tick drains that queue into PCM.
    Sm64Audio_RefillRing();
}

void Sm64Mario_Draw(PlayState* play, Player* player) {
    if (!Sm64Mario_HasMesh()) return;
    // Pass (linkPos - marioPosAtLastTick) as a render-time translation.
    // In normal play the tick just ran so marioPos ≈ linkPos (delta ≈ 0).
    // During the no-tick defer (item-get, cutscenes) the tick was skipped
    // to avoid crashing libsm64; the mesh vertices are stale at Mario's
    // last-ticked position. Adding the delta in the renderer shifts the
    // whole mesh over to wherever Link's been scripted, so Mario stays
    // visible and appears to track Link through the cutscene even while
    // frozen in his last animation pose.
    float dx = player->actor.world.pos.x - sSm64OutState.position[0] / SM64_WORLD_SCALE;
    float dy = player->actor.world.pos.y - sSm64OutState.position[1] / SM64_WORLD_SCALE;
    float dz = player->actor.world.pos.z - sSm64OutState.position[2] / SM64_WORLD_SCALE;
    // Vanish cap → render translucent (ghost look) into the XLU pass.
    // Metal cap → sphere-map the real SM64 metal envmap (atlas tile 0) into
    // the vertex colors (libsm64 strips the env-mapped metal material when it
    // emits the geometry buffer, so we reconstruct it on the OOT side from
    // per-vertex normals — see emitTrisSingle in sm64_mario_render.c).
    // Wing cap → drop alpha on the wing-texture tiles so the alpha-cutout
    // wing edges don't render an opaque halo.
    u8 translucent = (sSm64OutState.flags & SM64_MARIO_VANISH_CAP) != 0;
    u8 metalTint   = (sSm64OutState.flags & SM64_MARIO_METAL_CAP) != 0;
    u8 wingCap     = (sSm64OutState.flags & SM64_MARIO_WING_CAP) != 0;
    // Fire cap has no libsm64 flag (it's a pure OOT-side cap) — query the cap module.
    u8 fireActive  = Sm64MarioCaps_IsFireActive();
    // Cap-state heartbeat — log on any cap edge change so we can verify
    // the libsm64 patched interact_cap is actually applying the new cap
    // flag. If the user casts Vanish then Metal and the log doesn't show
    // METAL_CAP after the second cast, the dll patch didn't take.
    {
        static u32 sLastCapState = 0;
        u32 nowState = sSm64OutState.flags & (SM64_MARIO_VANISH_CAP | SM64_MARIO_METAL_CAP | SM64_MARIO_WING_CAP);
        if (nowState != sLastCapState) {
            lusprintf(__FILE__, __LINE__, 2,
                "[SM64] Cap state change: 0x%X → 0x%X (V=%d M=%d W=%d) translucent=%d metalTint=%d",
                sLastCapState, nowState,
                (nowState & SM64_MARIO_VANISH_CAP) ? 1 : 0,
                (nowState & SM64_MARIO_METAL_CAP) ? 1 : 0,
                (nowState & SM64_MARIO_WING_CAP) ? 1 : 0,
                translucent, metalTint);
            sLastCapState = nowState;
        }
    }
    Sm64Render_DrawMarioMesh(play, &sSm64OutBuffers, dx, dy, dz, translucent, metalTint, wingCap,
                             fireActive, /*recolor*/ 0, 0, 0, 0);

    // If the player is holding a deku stick C-button, render the lit stick
    // model floating at Mario's hand. State + render impl live in
    // sm64_mario_items.c; the call is here so the stick appears in the
    // same draw pass as Mario's body (no z-fight, same render layer).
    Sm64Mario_DrawHeldStick(play);

    // Fire Flower fireballs — drawn here so each flame billboard shares Mario's
    // draw pass (same XLU layer). Positions are absolute/world, so the fire
    // follows each ball's own bouncing trajectory, not Mario.
    Sm64Mario_DrawFireballs(play);
    // Thrown cap (Cappy) billboard.
    Sm64Cappy_Draw(play);
}

// Suspend cascade. While sSm64SuspendActive is true, Sm64Mario_IsActive()
// returns false — that propagates through the z_player hook to drop into
// the Reset path: Sm64Mario_Reset() deletes Mario, gSm64MarioInitialized
// goes to 0, and Player_Draw stops hiding Link (Sm64Mario_IsReady is
// false). Net effect: Mario "detransforms", Link is visible, vanilla
// action funcs run unimpeded.
//
// Triggers (rising edge of any of these → 30-frame suspend):
//   - PLAYER_STATE1_LOADING: scene-change fade. Mario must be gone before
//     scene swap so the new scene sees no stale collision references.
//   - PLAYER_STATE1_IN_CUTSCENE / Player_InCsMode: door walk-through,
//     demo cutscenes, csAction-driven scripted moves. Door anim sets
//     IN_CUTSCENE in Player_ActionHandler_1 (z_player.c:5828) — Mario
//     would otherwise stay on the wrong side of the door because libsm64
//     can't follow Link's scripted move through it.
//   - PLAYER_STATE1_GETTING_ITEM / IN_ITEM_CS: item-get cutscene that
//     was crashing sm64_mario_tick (RAX=0 NULL deref).
//
// On falling edge of every trigger, the 30-frame countdown drains and
// Mario re-transforms — Sm64Mario_Init runs again from the player update
// hook, re-creates Mario at Link's then-current (post-cutscene) position.
static u8 sSm64SuspendActive = 0;
static u32 sSm64ResumeCountdown = 0;
static u8 sSm64PrevSuspendTrigger = 0;

u8 Sm64Mario_IsActive(void) {
    if (sSm64SuspendActive) return 0;
    // Mario dead (the health manager set Link's HP to 0): yield so OOT runs its
    // own death / game-over / respawn on Link without Mario overriding it —
    // otherwise the player is stuck mid-death (softlock). On respawn the scene
    // reloads, OnPlayerInit recreates Mario at full, and this returns true again.
    if (gSaveContext.health <= 0) return 0;
    return CVarGetInteger("gSm64Mario", 0) != 0;
}

void Sm64Mario_TickTransitionSuspend(PlayState* play, Player* player) {
    if (play == NULL || player == NULL) {
        sSm64PrevSuspendTrigger = 0;
        return;
    }

    u8 nowLoading = (player->stateFlags1 & PLAYER_STATE1_LOADING) != 0;
    u8 nowCutscene = Player_InCsMode(play) ||
        (player->stateFlags1 & (PLAYER_STATE1_GETTING_ITEM |
                                PLAYER_STATE1_IN_ITEM_CS |
                                PLAYER_STATE1_IN_CUTSCENE)) != 0;
    // Persistence: ONLY the actual scene-load fade detransforms Mario. Cutscenes
    // / talk / doors / item-get are handled live by the defer branches in
    // Sm64Mario_Update (sync position, skip the crash-prone tick, the draw shifts
    // the mesh), so Mario stays VISIBLE through them instead of vanishing. The old
    // behavior also suspended on cutscenes — that's why Mario disappeared on every
    // NPC / door / item-get. nowCutscene is kept only for the diagnostic log.
    u8 nowSuspend = nowLoading;

    // Rising edge of any trigger → start a fresh suspend window.
    //   LOADING (scene fade): 30 frames so Mario stays gone through the
    //     fade-out + scene swap + fade-in.
    //   Cutscene only (door, item-get, demo): 5 frames — no fade to wait
    //     for, just enough grace for camera to settle on the other side
    //     before we recreate Mario at Link's new position.
    if (nowSuspend && !sSm64PrevSuspendTrigger) {
        sSm64SuspendActive = 1;
        sSm64ResumeCountdown = nowLoading ? 30 : 5;
        lusprintf(__FILE__, __LINE__, 2, "[SM64] Suspend trigger: loading=%d cs=%d", nowLoading, nowCutscene);
    }

    // Hold suspend while any trigger is still active. Floor at 3 frames so
    // a single-frame flicker on the trigger flag doesn't immediately
    // re-transform Mario mid-cutscene.
    if (nowSuspend) {
        sSm64SuspendActive = 1;
        if (sSm64ResumeCountdown < 3) sSm64ResumeCountdown = 3;
    } else if (sSm64ResumeCountdown > 0) {
        sSm64ResumeCountdown--;
        if (sSm64ResumeCountdown == 0) {
            sSm64SuspendActive = 0;
            lusprintf(__FILE__, __LINE__, 2, "[SM64] Suspend ended — Mario re-transforming");
        }
    }

    sSm64PrevSuspendTrigger = nowSuspend;
}

u8 Sm64Mario_IsReady(void) {
    return Sm64Mario_IsActive() && sSm64MarioId >= 0;
}

// True when Mario is ready AND standing on the ground (not airborne). Used to gate
// the C-Up first-person entry (#6) so it can't trigger mid-jump.
u8 Sm64Mario_IsGrounded(void) {
    if (sSm64MarioId < 0) {
        return 0;
    }
    return (sSm64OutState.action & SM64_ACT_FLAG_AIR) ? 0 : 1;
}

// Remote sync (Harpoon): report the LOCAL Mario's libsm64 anim pose so peers can
// drive their own Mario instance to the same animation. Returns 1 when the local
// player is an active Mario (caller then broadcasts transformation = MARIO). Pos +
// yaw already ride the normal posRot sync; this adds the libsm64 animID/frame.
u8 Sm64Mario_GetSyncState(s32* outAnimId, s16* outAnimFrame) {
    if (!Sm64Mario_IsReady()) {
        return 0;
    }
    if (outAnimId != NULL) {
        *outAnimId = sSm64OutState.animID;
    }
    if (outAnimFrame != NULL) {
        *outAnimFrame = sSm64OutState.animFrame;
    }
    return 1;
}

// =============================================================================
// Remote-Mario puppet renderer (Harpoon)
//
// When a remote player is in Mario mode, the local client renders their dummy as
// a real libsm64 Mario mesh — recolored from the default red to the remote's
// chosen Harpoon color — posed to the network-synced libsm64 animation. We keep
// ONE shared libsm64 "renderer" instance and re-pose it per dummy draw (immediate
// mode: pose → puppet-tick → draw within the same call), so N remote Marios need
// only one instance. The instance NEVER runs physics (geometry-only puppet tick),
// so it is safe even though no static surfaces are loaded into its globalState,
// and it survives scene changes (the DLL instance pool persists for the session).
// =============================================================================
static int32_t sSm64PuppetId = -1;
static float sSm64PuppetPos[SM64_MAX_TRIS * 9];
static float sSm64PuppetNorm[SM64_MAX_TRIS * 9];
static float sSm64PuppetColor[SM64_MAX_TRIS * 9];
static float sSm64PuppetUv[SM64_MAX_TRIS * 6];
static struct SM64MarioGeometryBuffers sSm64PuppetBuffers;
static u8 sSm64PuppetBuffersInited = 0;

// True only when the local client can actually render a remote Mario: the DLL is
// loaded + libsm64 globally initialized (ROM present), and the puppet procs all
// resolved (an old sm64.dll without sm64_mario_tick_puppet returns false).
// HarpoonDummyPlayer_Draw gates on this — if false, the remote's dummy stays Link.
u8 Sm64Remote_CanRender(void) {
    // The local player may be Link (never toggled Mario on), in which case the
    // library was never globally initialized. Try ONCE to init on demand so we can
    // still render a remote's Mario — this loads the DLL + sm64.z64 ROM. One-shot:
    // if the ROM/DLL aren't present it fails and we stay Link for the session
    // (matches "render the remote as Mario only if I have sm64.dll + sm64.n64").
    // If the local later enables Mario, that path re-runs Sm64_InitLibrary anyway.
    if (!sSm64Initialized) {
        static u8 sTriedRemoteInit = 0;
        if (!sTriedRemoteInit) {
            sTriedRemoteInit = 1;
            Sm64_InitLibrary();
        }
    }
    return sSm64Initialized && p_sm64_mario_create_puppet != NULL && p_sm64_mario_tick_puppet != NULL &&
           p_sm64_set_mario_position != NULL && p_sm64_set_mario_faceangle != NULL &&
           p_sm64_set_mario_animation != NULL && p_sm64_set_mario_anim_frame != NULL;
}

// Render a remote player's Mario at OOT world (x,y,z), facing faceYaw (OOT binary
// angle), posed to the synced libsm64 animID/animFrame, with Mario's red (cap +
// shirt) recolored to (tintR,tintG,tintB). Returns 1 on success; 0 if we can't
// render (caller then draws the normal Link dummy). Immediate-mode: it poses,
// puppet-ticks, and draws the shared renderer instance all within this call.
u8 Sm64Remote_DrawPuppet(PlayState* play, f32 x, f32 y, f32 z, s16 faceYaw, s32 animId,
                         s16 animFrame, u8 tintR, u8 tintG, u8 tintB) {
    if (play == NULL || !Sm64Remote_CanRender()) {
        return 0;
    }

    if (!sSm64PuppetBuffersInited) {
        sSm64PuppetBuffers.position = sSm64PuppetPos;
        sSm64PuppetBuffers.normal = sSm64PuppetNorm;
        sSm64PuppetBuffers.color = sSm64PuppetColor;
        sSm64PuppetBuffers.uv = sSm64PuppetUv;
        sSm64PuppetBuffers.numTrianglesUsed = 0;
        sSm64PuppetBuffersInited = 1;
    }

    // Lazy-create the shared renderer instance the first time it's needed. Use the
    // floor-tolerant puppet create: this instance has no static surfaces loaded
    // (they're per-globalState and only the local Mario loads them), so the normal
    // create would fail its floor check. The puppet never runs physics anyway.
    if (sSm64PuppetId < 0) {
        sSm64PuppetId = p_sm64_mario_create_puppet(x * SM64_WORLD_SCALE, y * SM64_WORLD_SCALE,
                                                   z * SM64_WORLD_SCALE);
        if (sSm64PuppetId < 0) {
            return 0;
        }
    }

    // Force the synced pose (position + facing) — set_mario_position also writes
    // gfx.pos and set_mario_faceangle writes gfx.angle, so the geometry-only tick
    // skins the mesh at exactly this transform. OOT binary yaw → libsm64 radians
    // is the inverse of the local readback (shape.rot.y = faceAngle/PI*32768).
    p_sm64_set_mario_position(sSm64PuppetId, x * SM64_WORLD_SCALE, y * SM64_WORLD_SCALE,
                              z * SM64_WORLD_SCALE);
    p_sm64_set_mario_faceangle(sSm64PuppetId, (f32)faceYaw * 3.14159f / 32768.0f);

    // Drive the exact animation the remote is playing.
    if (animId >= 0) {
        p_sm64_set_mario_animation(sSm64PuppetId, animId);
        p_sm64_set_mario_anim_frame(sSm64PuppetId, animFrame);
    }

    p_sm64_mario_tick_puppet(sSm64PuppetId, &sSm64PuppetBuffers);

    if (sSm64PuppetBuffers.numTrianglesUsed == 0) {
        return 0;
    }

    // Mesh verts come out at libsm64 world coords; ×SM64_SCALE (in the renderer)
    // lands them back at the OOT world pos we set, so no extra offset is needed.
    Sm64Render_DrawMarioMesh(play, &sSm64PuppetBuffers, 0.0f, 0.0f, 0.0f,
                             /*translucent*/ 0, /*metalTint*/ 0, /*wingCap*/ 0, /*fireActive*/ 0,
                             /*recolor*/ 1, tintR, tintG, tintB);
    return 1;
}

// True only while the Vanish Cap is worn. Used to FORCE the NoClip wall-bypass
// (z_bgcheck.c) so Mario phases through walls AND floor-based loading zones
// still fire — independent of the gCheats.NoClip CVar. Preferred over the
// surface-strip approach, which left actor.wallPoly stale so exits didn't trigger.
u8 Sm64Mario_IsVanishActive(void) {
    return Sm64Mario_IsReady() && (sSm64OutState.flags & SM64_MARIO_VANISH_CAP) != 0;
}

u8 Sm64Mario_HasMesh(void) {
    // Lens-of-truth held → hide Mario entirely (mirror of EnPartner.shouldDraw=0
    // in z_en_partner.c:617-622). Sm64Mario_LensActive is set by the Lens
    // item handler in sm64_mario_items.c.
    if (Sm64Mario_LensActive()) return 0;
    // First-person (#6 C-Up free-look): the camera sits inside Mario's head, so
    // drawing his mesh just shows the model inside-out. Hide it while looking —
    // ShouldHideLink keeps Link hidden too, so nothing draws (correct first-person).
    if (gPlayState != NULL) {
        Player* fpPlayer = GET_PLAYER(gPlayState);
        if (fpPlayer != NULL && (fpPlayer->stateFlags1 & PLAYER_STATE1_FIRST_PERSON)) {
            return 0;
        }
    }
    return Sm64Mario_IsReady() && sSm64OutBuffers.numTrianglesUsed > 0;
}

u8 Sm64Mario_ShouldHideLink(void) {
    // Bypass IsActive's suspend short-circuit on purpose — this is a
    // visibility-only flag. Detransform still happens internally; we just
    // don't want the draw hook to fall back to drawing Link during that
    // window because the user wants Mario mode to stay visually consistent.
    // EXCEPTION: when Mario is dead (HP 0) we must SHOW Link so his death /
    // game-over animation is visible instead of an invisible, frozen-looking
    // player (matches the IsActive yield above).
    if (gSaveContext.health <= 0) return 0;
    return CVarGetInteger("gSm64Mario", 0) != 0;
}

void Sm64Mario_Reset(void) {
    lusprintf(__FILE__, __LINE__, 2, "[SM64] Reset: MarioId=%d surfacesForScene=%d",
        sSm64MarioId, sSm64SurfacesForScene);
    if (sSm64MarioId >= 0 && p_sm64_mario_delete) {
        p_sm64_mario_delete(sSm64MarioId);
        sSm64MarioId = -1;
    }
    // Stop rendering stale mesh — without this, a frame could still draw the
    // last Mario pose via Sm64Mario_Draw if something called it after Reset.
    sSm64OutBuffers.numTrianglesUsed = 0;
    // Deactivate the punch collider so it doesn't fire in a limbo state where
    // CVAR is off but the cylinder is still registered for AT processing.
    sSm64AttackCollider.base.atFlags &= ~AT_ON;
    sSm64AttackCollider.base.atFlags &= ~AT_HIT;
    sSm64SurfacesForScene = -1;
    sSm64LastSceneNum = -1;
    // Let go of anything Mario was carrying — otherwise a scene-reset
    // leaves a stale Actor* that may reference freed memory.
    Sm64Mario_DropHeldActor();
    // Clear Ivan-style item state too (cooldowns, hookshot target,
    // lens-active flag, in-flight spell). The plain ItemsReset variant
    // can't restore Player struct fields without a Player*; the caller
    // chain into Sm64Mario_Reset doesn't carry one through. The
    // ItemsResetWithPlayer variant is invoked from the z_player.c hook
    // path where a Player* is available.
    Sm64Mario_ItemsReset();
    // Mario mode is ending (CVAR off / detransform): drop the active cap to
    // its proportional cooldown. The per-cap timer state itself persists,
    // frozen, until Mario mode resumes — it is NOT cleared here.
    Sm64MarioCaps_OnSuspend();

    // Forget Mario's independent health so re-enabling Mario mode starts at a
    // full 8-segment bar. (Scene changes keep it via sMarioHealthPersist; only
    // a full mode-off resets it.)
    sMarioHealthPersist = -1;
    sMarioLinkMirrorHP = -1;
}

// =============================================================================
// Mario Mask hidden item — C-Down lock + toggle. While
// gSm64MarioMaskForce CVar is set, ITEM_MARIO_MASK is forced into the
// C-Down slot every frame (overrides whatever the user equipped). Pressing
// C-Down in that mode flips gSm64Mario on/off — the press is consumed so
// Player_ItemAction never tries to "use" the mask as a real item.
// =============================================================================

#define SM64_CDOWN_BUTTON_INDEX 2  // buttonItems[2] is the C-Down slot

void Sm64MarioMask_ForceAndToggle(PlayState* play, Player* player) {
    (void)play;
    (void)player;
    // C-Down NO LONGER toggles Mario Mode — it's freed for the Cappy / cap-throw.
    // Mario Mode is toggled via the menu CVar gSm64Mario (or the Broken Items
    // pause page). This handler is kept as a no-op so the z_player hook call
    // site stays valid; no mask is stamped onto C-Down anymore.
}

void Sm64Mario_OnSceneChange(PlayState* play) {
    // Scene change logic lives in Sm64Mario_Update now
    (void)play;
}

// Reliable scene-spawn signal (Player_Init fires every loading zone, warp,
// respawn). Polling play->sceneNum inside Update misses some warp cases
// because Player_Update can be suspended during the fade. This is the same
// pattern transformation_masks uses — see TransformMasks_Init hook at
// z_player.c:11510.
void Sm64Mario_OnPlayerInit(PlayState* play, Player* player) {
    (void)player;
    if (!sSm64Initialized) return;

    // Drop old Mario + mesh buffer. sSm64LastSceneNum = -1 guarantees the
    // scene-change branch in Update re-enters on the first frame it runs,
    // even if the new play->sceneNum happens to match the old one.
    if (sSm64MarioId >= 0 && p_sm64_mario_delete) {
        p_sm64_mario_delete(sSm64MarioId);
        sSm64MarioId = -1;
    }
    sSm64OutBuffers.numTrianglesUsed = 0;
    sSm64SurfacesForScene = -1;
    sSm64LastSceneNum = -1;
    // Held actor belongs to the old scene — never carry it across a
    // loading zone, that path crashes once the old actor pool is freed.
    Sm64Mario_DropHeldActor();
    // Scene change: drop the active cap to its proportional cooldown (the
    // recreated Mario starts cap-less). Cooldown timers freeze across the
    // transition and resume once Mario mode is active again.
    Sm64MarioCaps_OnSuspend();
    // Trigger the post-scene-transition suspend — forces a Reset→Init cycle
    // in the z_player hook (Mario hidden for ~30 frames, Link runs normally,
    // then Mario re-created fresh). Empirically this is the only way to get
    // actors interacting after scene change; recreation via scene-change
    // branch or direct Init-same-frame both leave some state stale.
    sSm64SuspendActive = 1;
    sSm64ResumeCountdown = 30;
    lusprintf(__FILE__, __LINE__, 2, "[SM64] OnPlayerInit: nuked Mario + suspended for scene %d",
        play ? play->sceneNum : -1);
}

// SyncPositionToPlayer removed — position override now happens inside Sm64Mario_Update

// =============================================================================
// Combat bridge: OOT damage → Mario knockback animation, and Mario's fist/foot
// → Master-Sword AT collider so breakables/enemies react to his attacks.
// =============================================================================

void Sm64Mario_InterceptDamage(PlayState* play, Player* player) {
    (void)play;
    if (!Sm64Mario_IsReady()) return;

    u8 pendingDamage = player->actor.colChkInfo.damage;
    s32 hadAcHit = (player->cylinder.base.acFlags & AC_HIT) != 0;

    // Metal Cap → total invulnerability (SM64 Metal Mario shrugs off every hit).
    // The damage scrub at the bottom still runs (so OOT applies nothing), but we
    // skip Mario's own take_damage below: no health segments lost, no flinch.
    u32 metalActive = (sSm64OutState.flags & SM64_MARIO_METAL_CAP) != 0;

    // Per-hit invincibility window. Mario loses exactly ONE of his 8 segments
    // per hit; without this an enemy/boss that keeps overlapping Link's bumper
    // would drain several segments in a few frames (instant death). 40 frames
    // ~= 0.66s, similar to SM64's post-hit i-frames.
    static s16 sMarioHurtCooldown = 0;
    if (sMarioHurtCooldown > 0) {
        sMarioHurtCooldown--;
    }

    // Periodic snapshot of Link's damage-reception state so we can diagnose
    // "Mario doesn't get hurt" reports. Logs once every ~3 seconds (60 frames
    // × ~3). Reveals if acFlags is stuck, invincibilityTimer is nonzero, or
    // PLAYER_STATE1_DAMAGED is set — any of which would stop SetAC from
    // registering Link's cylinder and silently prevent enemy contact.
    {
        static u32 sHeartbeat = 0;
        if ((sHeartbeat % 180) == 0) {
            lusprintf(__FILE__, __LINE__, 2,
                "[SM64] Damage-state: acFlags=0x%02x colInfo.damage=%u invincT=%d flags1=0x%08x cylR=%d cylH=%d csState=%d",
                player->cylinder.base.acFlags, pendingDamage, player->invincibilityTimer,
                player->stateFlags1, player->cylinder.dim.radius, player->cylinder.dim.height,
                play ? (int)play->csCtx.state : -1);
        }
        sHeartbeat++;
    }

    if ((hadAcHit || pendingDamage > 0) && sMarioHurtCooldown == 0 && !metalActive && p_sm64_mario_take_damage &&
        sSm64MarioId >= 0) {
        sMarioHurtCooldown = 40; // start i-frames; one segment lost this hit
        // Source position drives the knockback direction inside libsm64.
        // Use the attacker's world.pos if the AC link is populated, else
        // fall back to Link's own position (knockback then defaults forward).
        // Note: Collider.ac is already `struct Actor*` (z64collision_check.h:15).
        Vec3f src = player->actor.world.pos;
        if (player->cylinder.base.ac != NULL) {
            src = player->cylinder.base.ac->world.pos;
        }
        // Independent Mario health: every enemy/hazard hit removes exactly ONE
        // of Mario's 8 segments (with his cap on head, take_damage(1) drains
        // 0x100 = one wedge), so Mario always dies in 8 hits regardless of how
        // much OOT damage the source would have dealt. libsm64's own i-frames
        // keep contiguous contact from chewing through multiple segments.
        u32 mDamage = 1;
        p_sm64_mario_take_damage(sSm64MarioId, mDamage, 0,
            src.x * SM64_WORLD_SCALE,
            src.y * SM64_WORLD_SCALE,
            src.z * SM64_WORLD_SCALE);

        // Element-specific reaction. OOT's AC hit effect (colChkInfo.acHitEffect:
        // 1=fire, 2=ice, 3=electric) survives the scrub below, so map it onto
        // Mario's native reaction action — overriding take_damage's plain
        // knockback. Effect 4 / others keep that default knockback.
        if (p_sm64_set_mario_action) {
            u32 react = 0;
            switch (player->actor.colChkInfo.acHitEffect) {
                case 1: // fire
                    react = (sSm64OutState.action & SM64_ACT_FLAG_AIR)
                                ? SM64_ACT_BURNING_JUMP : SM64_ACT_BURNING_GROUND;
                    break;
                case 2: react = SM64_ACT_SHIVERING; break; // ice (closest SM64 has to "frozen")
                case 3: react = SM64_ACT_SHOCKED;   break; // electric
            }
            if (react != 0) {
                p_sm64_set_mario_action(sSm64MarioId, react);
            }
        }

        // NOTE: we deliberately do NOT call Health_ChangeBy on Link here.
        // Mario's health is now independent (the take_damage above is the real
        // hit), and the health manager in Sm64Mario_Update mirrors Mario → Link
        // each frame. Decrementing Link here too would double-count.

        // Drop whatever Mario was carrying — real SM64 calls
        // drop_and_set_mario_action() in damage transitions, but our held
        // actor is tracked OOT-side via actor.parent which libsm64 doesn't
        // see. Without this, the bomb/rock would keep floating in front of
        // Mario even after he gets knocked back. Apply small drop velocity
        // so the actor falls naturally instead of just teleporting down.
        if (sSm64HeldActor != NULL) {
            sSm64HeldActor->parent = NULL;
            sSm64HeldActor->speedXZ = 0.0f;
            sSm64HeldActor->velocity.x = 0.0f;
            sSm64HeldActor->velocity.y = 3.0f;
            sSm64HeldActor->velocity.z = 0.0f;
            sSm64HeldActor = NULL;
        }

        lusprintf(__FILE__, __LINE__, 2,
            "[SM64] Damage intercepted: hadAcHit=%d oot_dmg=%u → mario_dmg=%u linkHP=%d src=(%.0f,%.0f,%.0f)",
            hadAcHit, pendingDamage, mDamage, gSaveContext.health, src.x, src.y, src.z);
    }

    // Scrub every damage input regardless — blocks enemy bumpers, floor
    // hazards (lava / spikes), and any scripted knockback before OOT's
    // func_80837C0C gets a chance to apply health / state / velocity.
    player->cylinder.base.acFlags &= ~AC_HIT;
    player->actor.colChkInfo.damage = 0;
    player->actor.colChkInfo.damageEffect = 0;
}

void Sm64Mario_ScrubDamageState(PlayState* play, Player* player) {
    (void)play;
    if (!Sm64Mario_IsReady()) return;
    // Defense in depth for non-AC_HIT paths (void-out, script damage).
    player->stateFlags1 &= ~PLAYER_STATE1_DAMAGED;

    // Force Link's bumper cylinder to Link-sized dimensions. During Mario
    // mode we skip Link's skeleton draw, so the Player_PostLimbDraw callbacks
    // that normally update bodyPartsPos[] never run. Player_UpdateCommon
    // (lines 12921-12926 of z_player.c) computes cylinder.dim.height from
    // those stale bodyParts → ends up tiny (10 OOT units observed). Enemies
    // then walk over Link's cylinder without triggering AC_HIT and Mario
    // never takes damage. Override with vanilla Link standing dimensions.
    player->cylinder.dim.radius = 12;
    player->cylinder.dim.height = 40;
    player->cylinder.dim.yShift = 0;
}

void Sm64Mario_InitAttackCollider(PlayState* play, Player* player) {
    // Gate only on library-ready + valid args. We deliberately do NOT check
    // IsActive() — the suspend window sets IsActive=false even when Mario
    // is "on" from the user's perspective, and during that window we still
    // need to re-bind the collider to the new Player actor so it's ready
    // when suspend lifts.
    if (play == NULL || player == NULL || !sSm64Initialized) return;
    Collider_InitCylinder(play, &sSm64AttackCollider);
    Collider_SetCylinder(play, &sSm64AttackCollider, &player->actor, &sSm64AttackColliderInit);
    sSm64AttackColliderInited = 1;
    // Bind the Metal Cap blast aura to the same (possibly new) Player actor.
    Collider_InitCylinder(play, &sSm64MetalCollider);
    Collider_SetCylinder(play, &sSm64MetalCollider, &player->actor, &sSm64MetalColliderInit);
    sSm64MetalColliderInited = 1;
    lusprintf(__FILE__, __LINE__, 2, "[SM64] Attack + Metal-aura colliders bound to Player actor");
}

// Metal Cap blast aura — see sSm64MetalColliderInit. While the Metal Cap is on,
// arm a damage cylinder centered on Mario each frame so contact kills enemies
// and breaks pots/grass/props. No one-hit gate: it's a CONTINUOUS aura (enemy
// i-frames pace repeat damage; breakables shatter on first contact).
static void Sm64Mario_UpdateMetalBlast(PlayState* play, Player* player) {
    if (!Sm64Mario_IsReady()) return;

    // Lazy bind (mode toggled on mid-scene, before any Player_Init re-bind).
    if (!sSm64MetalColliderInited) {
        Collider_InitCylinder(play, &sSm64MetalCollider);
        Collider_SetCylinder(play, &sSm64MetalCollider, &player->actor, &sSm64MetalColliderInit);
        sSm64MetalColliderInited = 1;
    }

    sSm64MetalCollider.base.atFlags &= ~(AT_ON | AT_HIT);

    // Only while the Metal Cap is actually worn.
    if (!(sSm64OutState.flags & SM64_MARIO_METAL_CAP)) return;

    f32 mx = sSm64OutState.position[0] / SM64_WORLD_SCALE;
    f32 my = sSm64OutState.position[1] / SM64_WORLD_SCALE;
    f32 mz = sSm64OutState.position[2] / SM64_WORLD_SCALE;
    sSm64MetalCollider.dim.pos.x = (s16)mx;
    sSm64MetalCollider.dim.pos.y = (s16)my;
    sSm64MetalCollider.dim.pos.z = (s16)mz;
    sSm64MetalCollider.base.atFlags |= AT_ON;
    CollisionCheck_SetAT(play, &play->colChkCtx, &sSm64MetalCollider.base);
}

void Sm64Mario_UpdateAttackCollider(PlayState* play, Player* player) {
    if (!Sm64Mario_IsReady()) return;

    // Metal Cap blast aura — independent of the attack-state logic below, which
    // has several early returns. Runs every frame the Metal Cap is worn.
    Sm64Mario_UpdateMetalBlast(play, player);

    // Lazy init: if Player_Init's InitAttackCollider bailed (CVAR was off at
    // scene-load, user toggled on later), bind now.
    if (!sSm64AttackColliderInited) {
        Collider_InitCylinder(play, &sSm64AttackCollider);
        Collider_SetCylinder(play, &sSm64AttackCollider, &player->actor, &sSm64AttackColliderInit);
        sSm64AttackColliderInited = 1;
        lusprintf(__FILE__, __LINE__, 2, "[SM64] Attack collider lazy-bound to Player actor");
    }

    u32 action = sSm64OutState.action;
    u32 flags  = sSm64OutState.flags;
    f32 fwd = 0.0f, up = 0.0f;
    u8 attacking = 0;
    u8 isGroundPound = 0;

    if (flags & SM64_MARIO_PUNCHING) {
        attacking = 1; fwd = 20.0f; up = 25.0f;
    } else if (flags & SM64_MARIO_KICKING) {
        attacking = 1; fwd = 22.0f; up = 15.0f;
    } else if (action == SM64_ACT_GROUND_POUND_LAND) {
        attacking = 1; fwd = 0.0f;  up = 5.0f;
        isGroundPound = 1;
    } else if (action == SM64_ACT_DIVE || action == SM64_ACT_DIVE_SLIDE ||
               action == SM64_ACT_SLIDE_KICK || action == SM64_ACT_SLIDE_KICK_SLIDE) {
        attacking = 1; fwd = 25.0f; up = 10.0f;
    } else if (action == SM64_ACT_TWIRLING) {
        // Spin attack (X) — 360° rotation, so the collider sits centered on Mario.
        attacking = 1; fwd = 0.0f; up = 15.0f;
    }

    // ONE-HIT-PER-ATTACK gate (fix for "sometimes kills in one hit"): the
    // collider was re-activated every frame during a punch (~8-12 frames)
    // and could register multiple hits per single punch. Track the attack
    // action value — on transition to a new action, clear the hit flag;
    // during an active attack, if AT_HIT fired last frame, stop re-SetAT'ing
    // until Mario leaves this attack.
    static u32 sPrevAttackAction = 0;
    static u8  sHitThisAttack = 0;
    if (attacking && action != sPrevAttackAction) {
        sHitThisAttack = 0;  // fresh attack window
    }
    if (sSm64AttackCollider.base.atFlags & AT_HIT) {
        sHitThisAttack = 1;  // the previous frame's SetAT connected
    }
    sPrevAttackAction = attacking ? action : 0;

    // Clear both AT flags for this frame. Re-armed below only if we're
    // attacking AND haven't already scored a hit this attack window.
    sSm64AttackCollider.base.atFlags &= ~AT_ON;
    sSm64AttackCollider.base.atFlags &= ~AT_HIT;

    // Log transitions both directions (start of attack, end of attack).
    {
        static u8 sWasAttacking = 0;
        if (attacking && !sWasAttacking) {
            lusprintf(__FILE__, __LINE__, 2, "[SM64] Attack START: action=0x%08x flags=0x%08x",
                action, flags);
        } else if (!attacking && sWasAttacking) {
            lusprintf(__FILE__, __LINE__, 2, "[SM64] Attack END");
        }
        sWasAttacking = attacking;
    }

    if (!attacking || sHitThisAttack) return;

    // Swap damage flags per attack type so ground pound breaks hammer-
    // specific props (cracked floor tiles, Dead Hand drop, ReDead stun, etc.)
    // while punches/kicks still behave as Master-Sword slashes.
    sSm64AttackCollider.info.toucher.dmgFlags =
        isGroundPound ? DMG_HAMMER
                      : (DMG_SLASH_MASTER | DMG_JUMP_MASTER | DMG_SPIN_MASTER);

    // Mario's libsm64 position is SM64-scale; convert to OOT.
    f32 mx = sSm64OutState.position[0] / SM64_WORLD_SCALE;
    f32 my = sSm64OutState.position[1] / SM64_WORLD_SCALE;
    f32 mz = sSm64OutState.position[2] / SM64_WORLD_SCALE;

    // faceAngle is radians in libsm64's SM64MarioState.
    f32 yaw = sSm64OutState.faceAngle;
    f32 fx = sinf(yaw), fz = cosf(yaw);

    sSm64AttackCollider.dim.pos.x = (s16)(mx + fx * fwd);
    sSm64AttackCollider.dim.pos.y = (s16)(my + up);
    sSm64AttackCollider.dim.pos.z = (s16)(mz + fz * fwd);
    sSm64AttackCollider.base.atFlags |= AT_ON;
    CollisionCheck_SetAT(play, &play->colChkCtx, &sSm64AttackCollider.base);
}

// =============================================================================
// Audio bridge: pipe libsm64's PCM output into SoH's audio mixer
// =============================================================================
//
// Pattern mirrors Pikachu/MM direct-audio (code_800E4FE0.c:78-80). Producer
// is the game thread (Sm64Mario_Update calls sm64_audio_tick to refill the
// ring); consumer is the audio thread (Sm64Audio_MixInto drains the ring
// into the PCM buffer OOT's synth just filled). Single-producer / single-
// consumer keeps libsm64's internal audio state touched only by game thread
// — avoids races in sm64_audio_tick's update_game_sound() with the Mario
// tick that's happening concurrently with rendering in other transformations.

// 8192 stereo pairs @ 32000 Hz = 256 ms of buffered audio. Plenty of
// headroom for any blocking the audio thread might hit.
#define SM64_AUDIO_RING_PAIRS 8192
#define SM64_AUDIO_RING_MASK  (SM64_AUDIO_RING_PAIRS - 1)
_Static_assert((SM64_AUDIO_RING_PAIRS & SM64_AUDIO_RING_MASK) == 0,
               "Audio ring size must be a power of two for mask indexing");

static int16_t sSm64AudioRing[SM64_AUDIO_RING_PAIRS * 2]; // interleaved L,R
static volatile uint32_t sSm64AudioHead = 0; // write cursor (stereo pairs)
static volatile uint32_t sSm64AudioTail = 0; // read cursor (stereo pairs)

static inline uint32_t Sm64Audio_RingFill(void) {
    return (sSm64AudioHead - sSm64AudioTail) & 0xFFFFFFFFu;
}

// Called from the game thread (Sm64Mario_Update) to keep the ring topped up.
// Target: ~128 ms of buffered audio (4096 pairs) so the audio thread never
// starves even if a game frame stalls briefly.
static void Sm64Audio_RefillRing(void) {
    if (!p_sm64_audio_tick) return;

    // Scratch buffer for one audio_tick call. libsm64 writes
    //   2 chunks × SAMPLES_HIGH(544) pairs × 2 s16 per pair = 2176 shorts.
    // Size = 544*2 pairs, 2 s16 per pair = 2176 ints, pad safety.
    static int16_t tmp[544 * 2 * 2 + 64];

    const uint32_t desired = 4096; // target fill (pairs)
    uint32_t safety = 8;           // avoid infinite loop if tick returns 0
    while (Sm64Audio_RingFill() < desired && safety-- > 0) {
        uint32_t queued = Sm64Audio_RingFill();
        uint32_t got = p_sm64_audio_tick(queued, desired, tmp);
        if (got == 0) break;
        // libsm64 writes 2 chunks, each `got` stereo pairs → 2*got pairs total.
        uint32_t totalPairs = 2u * got;
        // Don't overrun the ring: cap against free space.
        uint32_t freePairs = SM64_AUDIO_RING_PAIRS - Sm64Audio_RingFill();
        if (totalPairs > freePairs) totalPairs = freePairs;
        for (uint32_t i = 0; i < totalPairs; i++) {
            uint32_t idx = (sSm64AudioHead & SM64_AUDIO_RING_MASK) * 2;
            sSm64AudioRing[idx + 0] = tmp[i * 2 + 0];
            sSm64AudioRing[idx + 1] = tmp[i * 2 + 1];
            sSm64AudioHead++;
        }
    }
}

// Public hook for code_800E4FE0.c — consumes ring samples and mixes into
// the output buffer OOT's synth already wrote. numSamples is stereo pairs.
void Sm64Audio_MixInto(int16_t* outBuf, uint32_t numSamples) {
    if (!sSm64Initialized || outBuf == NULL || numSamples == 0) return;

    uint32_t available = Sm64Audio_RingFill();
    uint32_t toMix = numSamples < available ? numSamples : available;

    for (uint32_t i = 0; i < toMix; i++) {
        uint32_t idx = (sSm64AudioTail & SM64_AUDIO_RING_MASK) * 2;
        int32_t l = (int32_t)outBuf[i * 2 + 0] + (int32_t)sSm64AudioRing[idx + 0];
        int32_t r = (int32_t)outBuf[i * 2 + 1] + (int32_t)sSm64AudioRing[idx + 1];
        if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
        if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
        outBuf[i * 2 + 0] = (int16_t)l;
        outBuf[i * 2 + 1] = (int16_t)r;
        sSm64AudioTail++;
    }
}
