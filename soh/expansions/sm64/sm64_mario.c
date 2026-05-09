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
typedef void (*pfn_sm64_audio_init)(const uint8_t*);
typedef uint32_t (*pfn_sm64_audio_tick)(uint32_t, uint32_t, int16_t*);
typedef void (*pfn_sm64_play_sound_global)(int32_t);
typedef void (*pfn_sm64_set_sound_volume)(float);
typedef void (*pfn_sm64_set_mario_action)(int32_t, uint32_t);
typedef void (*pfn_sm64_set_mario_animation)(int32_t, int32_t);
typedef void (*pfn_sm64_mario_grab_dummy)(int32_t);
typedef void (*pfn_sm64_mario_release_dummy)(int32_t);
typedef void (*pfn_sm64_mario_interact_cap)(int32_t, uint32_t, uint16_t, uint8_t);

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
static pfn_sm64_audio_init p_sm64_audio_init = NULL;
static pfn_sm64_audio_tick p_sm64_audio_tick = NULL;
static pfn_sm64_play_sound_global p_sm64_play_sound_global = NULL;
static pfn_sm64_set_sound_volume p_sm64_set_sound_volume = NULL;
static pfn_sm64_set_mario_action p_sm64_set_mario_action = NULL;
static pfn_sm64_set_mario_animation p_sm64_set_mario_animation = NULL;
static pfn_sm64_mario_grab_dummy p_sm64_mario_grab_dummy = NULL;
static pfn_sm64_mario_release_dummy p_sm64_mario_release_dummy = NULL;
static pfn_sm64_mario_interact_cap p_sm64_mario_interact_cap = NULL;

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
    p_sm64_audio_init = (pfn_sm64_audio_init)SM64_GET_PROC(sDllHandle, "sm64_audio_init");
    p_sm64_audio_tick = (pfn_sm64_audio_tick)SM64_GET_PROC(sDllHandle, "sm64_audio_tick");
    p_sm64_play_sound_global = (pfn_sm64_play_sound_global)SM64_GET_PROC(sDllHandle, "sm64_play_sound_global");
    p_sm64_set_sound_volume = (pfn_sm64_set_sound_volume)SM64_GET_PROC(sDllHandle, "sm64_set_sound_volume");
    p_sm64_set_mario_action = (pfn_sm64_set_mario_action)SM64_GET_PROC(sDllHandle, "sm64_set_mario_action");
    p_sm64_set_mario_animation =
        (pfn_sm64_set_mario_animation)SM64_GET_PROC(sDllHandle, "sm64_set_mario_animation");
    p_sm64_mario_grab_dummy =
        (pfn_sm64_mario_grab_dummy)SM64_GET_PROC(sDllHandle, "sm64_mario_grab_dummy");
    p_sm64_mario_release_dummy =
        (pfn_sm64_mario_release_dummy)SM64_GET_PROC(sDllHandle, "sm64_mario_release_dummy");
    p_sm64_mario_interact_cap =
        (pfn_sm64_mario_interact_cap)SM64_GET_PROC(sDllHandle, "sm64_mario_interact_cap");

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
#define SM64_MARIO_VANISH_CAP       0x00000002
#define SM64_MARIO_METAL_CAP        0x00000004
#define SM64_MARIO_WING_CAP         0x00000008

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

    lusprintf(__FILE__, __LINE__, 2, "[SM64] Loaded %u surfaces for scene %d (floorOnly=%d)",
        numSurfaces, play->sceneNum, floorOnly);
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
        case ACTOR_EN_ISHI:     // liftable small / large stones
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
            sSm64HeldActor = NULL;

            // Mario throw animation. Safe with grab_dummy: act_throwing
            // derefs heldObj (the sentinel) without crashing.
            if (p_sm64_set_mario_action && sSm64MarioId >= 0) {
                p_sm64_set_mario_action(sSm64MarioId, SM64_ACT_THROWING);
            }
            sSm64GrabLockoutFrames = 10;
        }
        return;
    }

    // Nothing held — B-press while a grabbable is in range triggers the
    // attach. Use Mario's position (read via player->actor since we sync
    // them on every tick) so the search origin tracks Mario's current pos.
    if (bPress && sSm64GrabLockoutFrames == 0) {
        Actor* target = Sm64Mario_FindGrabbable(play, player);
        if (target != NULL) {
            target->parent = &player->actor;
            // Actor's action func (e.g. EnBom_Move checks Actor_HasParent)
            // will transition itself to the "held/wait-for-release" state
            // next update.
            sSm64HeldActor = target;

            // Mario pickup animation. With grab_dummy installed earlier
            // (at Init), act_picking_up's actionState=1 branch reads a
            // valid sentinel heldObj and plays MARIO_ANIM_PICK_UP_LIGHT_OBJ
            // → ACT_HOLD_IDLE → MARIO_ANIM_IDLE_WITH_LIGHT_OBJ. The action
            // machine handles all anim transitions natively.
            if (p_sm64_set_mario_action && sSm64MarioId >= 0) {
                p_sm64_set_mario_action(sSm64MarioId, SM64_ACT_PICKING_UP);
            }
            sSm64GrabLockoutFrames = 10;
            lusprintf(__FILE__, __LINE__, 2, "[SM64] Grabbed actor id=0x%02X", target->id);
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
                                PLAYER_STATE1_CARRYING_ACTOR |
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
    inputs.buttonA = (input->cur.button & BTN_A) ? 1 : 0;
    inputs.buttonB = (input->cur.button & BTN_B) ? 1 : 0;
    inputs.buttonZ = (input->cur.button & BTN_Z) ? 1 : 0;

    // Water level — Sm64Surfaces_GetWaterLevel returns OOT-scale Y; scale up for libsm64.
    if (p_sm64_set_mario_water_level) {
        waterY = Sm64Surfaces_GetWaterLevel(play, player->actor.world.pos.x, player->actor.world.pos.z);
        p_sm64_set_mario_water_level(sSm64MarioId, (int)(waterY * SM64_WORLD_SCALE));
    }

    // Tick SM64 at ~30 fps effective (SM64's native rate) regardless of OOT's
    // gameplay rate. OOT runs at 60 / R_UPDATE_RATE fps (R_UPDATE_RATE=3 → 20 fps).
    // Ticking once per OOT frame at 20 fps leaves Mario running at 67% of SM64
    // speed, which is why movement feels wrong. Time accumulator fixes it:
    //   20 fps OOT → ~1.5 ticks/frame average (pattern: 1,2,1,2,...)
    //   30 fps OOT → 1 tick/frame
    //   60 fps OOT → 1 tick every 2 frames
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

    // Shared-HP sync. Link is the source of truth: each frame we rescale
    // Link's current health (quarter-hearts, capacity-relative) into Mario's
    // 0..SM64_MARIO_MAX_HP range and overwrite libsm64's internal HP. This
    // keeps fall / burn / drown damage that libsm64 applies to Mario from
    // silently desyncing the two — InterceptDamage already routes enemy
    // hits through Health_ChangeBy in the opposite direction.
    if (p_sm64_set_mario_health && sSm64MarioId >= 0) {
        s16 linkHP = gSaveContext.health;
        s16 linkMax = gSaveContext.healthCapacity;
        if (linkMax > 0) {
            if (linkHP < 0) linkHP = 0;
            if (linkHP > linkMax) linkHP = linkMax;
            u16 marioHP = (u16)(((u32)linkHP * SM64_MARIO_MAX_HP) / linkMax);
            p_sm64_set_mario_health(sSm64MarioId, marioHP);
        }
    }

    // Vanish-cap collision swap. When Din's Fire (mapped to vanish cap)
    // activates, Mario should phase through walls + ceilings but still
    // detect floors. Implementation: re-upload only floor-like surfaces to
    // libsm64 while the cap flag is on, full surfaces when it clears.
    // Detected via sSm64OutState.flags & MARIO_VANISH_CAP — libsm64 also
    // auto-expires the cap after its internal timer.
    {
        static u8 sVanishCapPrev = 0;
        u8 vanishNow = (sSm64OutState.flags & SM64_MARIO_VANISH_CAP) != 0;
        if (vanishNow != sVanishCapPrev) {
            // State edge — swap surface set
            Sm64_LoadSceneSurfacesEx(play, vanishNow);
            sVanishCapPrev = vanishNow;
            lusprintf(__FILE__, __LINE__, 2,
                "[SM64] Vanish cap %s — surfaces reloaded (floorOnly=%d)",
                vanishNow ? "ACTIVATED" : "EXPIRED", vanishNow);
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
    player->actor.prevPos = player->actor.world.pos;

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
    // Metal cap → grayscale + silver bias on vertex colors (libsm64
    // strips the env-mapped metal material so we fake it on the OOT side).
    u8 translucent = (sSm64OutState.flags & SM64_MARIO_VANISH_CAP) != 0;
    u8 metalTint   = (sSm64OutState.flags & SM64_MARIO_METAL_CAP) != 0;
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
    Sm64Render_DrawMarioMesh(play, &sSm64OutBuffers, dx, dy, dz, translucent, metalTint);

    // If the player is holding a deku stick C-button, render the lit stick
    // model floating at Mario's hand. State + render impl live in
    // sm64_mario_items.c; the call is here so the stick appears in the
    // same draw pass as Mario's body (no z-fight, same render layer).
    Sm64Mario_DrawHeldStick(play);
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
    u8 nowSuspend = nowLoading || nowCutscene;

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

u8 Sm64Mario_HasMesh(void) {
    // Lens-of-truth held → hide Mario entirely (mirror of EnPartner.shouldDraw=0
    // in z_en_partner.c:617-622). Sm64Mario_LensActive is set by the Lens
    // item handler in sm64_mario_items.c.
    if (Sm64Mario_LensActive()) return 0;
    return Sm64Mario_IsReady() && sSm64OutBuffers.numTrianglesUsed > 0;
}

u8 Sm64Mario_ShouldHideLink(void) {
    // Bypass IsActive's suspend short-circuit on purpose — this is a
    // visibility-only flag. Detransform still happens internally; we just
    // don't want the draw hook to fall back to drawing Link during that
    // window because the user wants Mario mode to stay visually consistent.
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
    (void)player;
    if (play == NULL) return;
    if (!CVarGetInteger("gSm64MarioMaskForce", 0)) return;

    // Force ITEM_MARIO_MASK on C-Down each frame. If the user fiddles with
    // the kaleido subscreen and equips something else, our override puts
    // the mask back next frame — they can't actually unequip it. The icon
    // override in ExtInv_GetItemIcon (extended_inventory.c) handles the
    // rendering side.
    if (gSaveContext.equips.buttonItems[SM64_CDOWN_BUTTON_INDEX] != ITEM_MARIO_MASK) {
        gSaveContext.equips.buttonItems[SM64_CDOWN_BUTTON_INDEX] = ITEM_MARIO_MASK;
        // SLOT_NONE = 0xFF — no inventory grid slot binding. Prevents
        // ammo-display / decay / etc. from running on the mask slot.
        gSaveContext.equips.cButtonSlots[SM64_CDOWN_BUTTON_INDEX] = 0xFF;
    }

    // C-Down press → toggle gSm64Mario. Consume the press so the item-use
    // path doesn't try to do anything else with the mask.
    Input* in = &play->state.input[0];
    if (in->press.button & BTN_CDOWN) {
        s32 cur = CVarGetInteger("gSm64Mario", 0);
        s32 next = !cur;
        CVarSetInteger("gSm64Mario", next);
        CVarSave();
        in->press.button &= ~BTN_CDOWN;
        in->cur.button   &= ~BTN_CDOWN;  // also strip cur so HandleItems's "current" detection skips it
        lusprintf(__FILE__, __LINE__, 2,
            "[SM64] MarioMask C-Down toggle: gSm64Mario %d → %d", cur, next);
    }
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

    if ((hadAcHit || pendingDamage > 0) && p_sm64_mario_take_damage && sSm64MarioId >= 0) {
        // Source position drives the knockback direction inside libsm64.
        // Use the attacker's world.pos if the AC link is populated, else
        // fall back to Link's own position (knockback then defaults forward).
        // Note: Collider.ac is already `struct Actor*` (z64collision_check.h:15).
        Vec3f src = player->actor.world.pos;
        if (player->cylinder.base.ac != NULL) {
            src = player->cylinder.base.ac->world.pos;
        }
        // OOT damage is in quarter-hearts; libsm64's fake_interaction uses
        // 2 and 4 as soft/medium/hard knockback thresholds. Map 1/4-heart →
        // 1 (soft), full-heart+ → damage/4.
        u32 mDamage = (pendingDamage >= 4) ? (pendingDamage / 4) : 1;
        p_sm64_mario_take_damage(sSm64MarioId, mDamage, 0,
            src.x * SM64_WORLD_SCALE,
            src.y * SM64_WORLD_SCALE,
            src.z * SM64_WORLD_SCALE);

        // Shared-HP: scrubbing colChkInfo.damage below keeps OOT's
        // func_80837C0C from running, which means Link's HP never goes down
        // unless we apply the decrement ourselves. Health_ChangeBy also
        // honors double-defense, one-hit-KO, defense modifier — we want
        // those to still modulate Mario's damage.
        if (pendingDamage > 0 && play != NULL) {
            Health_ChangeBy(play, -(s16)pendingDamage);
        }

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
    lusprintf(__FILE__, __LINE__, 2, "[SM64] Attack collider bound to Player actor");
}

void Sm64Mario_UpdateAttackCollider(PlayState* play, Player* player) {
    if (!Sm64Mario_IsReady()) return;

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
