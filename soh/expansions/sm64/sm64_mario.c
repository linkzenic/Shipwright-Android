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

static void* sDllHandle = NULL;

static s32 Sm64_LoadDll(void) {
    if (sDllHandle)
        return 1;

    sDllHandle = SM64_LOAD_LIB("sm64.dll");
    if (!sDllHandle)
        sDllHandle = SM64_LOAD_LIB("./sm64.dll");
    if (!sDllHandle)
        sDllHandle = SM64_LOAD_LIB(".\\sm64.dll");
    if (!sDllHandle)
        sDllHandle = SM64_LOAD_LIB("x64/Release/sm64.dll");
    if (!sDllHandle) {
        osSyncPrintf("[SM64] ERROR: Could not load sm64.dll\n");
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

#define SM64_MAX_TRIS SM64_GEO_MAX_TRIANGLES
static float sSm64PosBuffer[SM64_MAX_TRIS * 9];
static float sSm64NormBuffer[SM64_MAX_TRIS * 9];
static float sSm64ColorBuffer[SM64_MAX_TRIS * 9];
static float sSm64UvBuffer[SM64_MAX_TRIS * 6];
static struct SM64MarioState sSm64OutState;
static struct SM64MarioGeometryBuffers sSm64OutBuffers;

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
    if (!Sm64_LoadDll())
        return 0;

    romPath = CVarGetString("gSm64RomPath", "");
    if (romPath == NULL || romPath[0] == '\0') {
        romPath = "sm64.z64";
    }

    sSm64RomData = Sm64_LoadRomFile(romPath, &romSize);
    if (sSm64RomData == NULL)
        return 0;

    if (romSize != 8 * 1024 * 1024) {
        free(sSm64RomData);
        sSm64RomData = NULL;
        return 0;
    }

    sSm64TextureAtlas = (uint8_t*)malloc(4 * SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT);
    if (!sSm64TextureAtlas) {
        free(sSm64RomData);
        sSm64RomData = NULL;
        return 0;
    }

    p_sm64_global_init(sSm64RomData, sSm64TextureAtlas);

    Sm64Render_SetTextureAtlas(sSm64TextureAtlas);

    sSm64OutBuffers.position = sSm64PosBuffer;
    sSm64OutBuffers.normal = sSm64NormBuffer;
    sSm64OutBuffers.color = sSm64ColorBuffer;
    sSm64OutBuffers.uv = sSm64UvBuffer;
    sSm64OutBuffers.numTrianglesUsed = 0;

    sSm64Initialized = 1;
    return 1;
}

// =============================================================================
// Surface Loading
// =============================================================================

static void Sm64_LoadSceneSurfaces(PlayState* play) {
    uint32_t numSurfaces = 0;
    struct SM64Surface* surfaces;

    if (!p_sm64_static_surfaces_load)
        return;

    surfaces = Sm64Surfaces_ExtractStatic(play, &numSurfaces);
    if (surfaces != NULL && numSurfaces > 0) {
        p_sm64_static_surfaces_load(surfaces, numSurfaces);
        free(surfaces);
    }

    sSm64LastSceneNum = play->sceneNum;
}

// =============================================================================
// Public API
// =============================================================================

// Returns 1 if Mario was successfully created, 0 otherwise.
s32 Sm64Mario_Init(PlayState* play, Player* player) {
    if (!Sm64_InitLibrary())
        return 0;

    Sm64_LoadSceneSurfaces(play);

    if (p_sm64_mario_create) {
        sSm64MarioId =
            p_sm64_mario_create(player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z);
    }

    return (sSm64MarioId >= 0) ? 1 : 0;
}

void Sm64Mario_Update(PlayState* play, Player* player) {
    Camera* cam;
    float lookX, lookZ, lookMag, waterY;
    Input* input;
    struct SM64MarioInputs inputs;
    u32 deferMask;

    if (!sSm64Initialized || sSm64MarioId < 0 || !p_sm64_mario_tick)
        return;

    // Block OOT's actionFunc (like Pikachu does)
    player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;

    // Detect scene change → reload collision
    if (play->sceneNum != sSm64LastSceneNum) {
        Sm64_LoadSceneSurfaces(play);
        if (p_sm64_set_mario_position) {
            p_sm64_set_mario_position(sSm64MarioId, player->actor.world.pos.x, player->actor.world.pos.y,
                                      player->actor.world.pos.z);
        }
    }

    // DEFER to OOT during transitions (Pikachu pattern lines 442-449, 1308-1316)
    // When these flags are set, OOT is handling something important.
    // Sync Mario to Link's position and let OOT drive.
    deferMask = PLAYER_STATE1_LOADING | PLAYER_STATE1_DEAD | PLAYER_STATE1_TALKING | PLAYER_STATE1_GETTING_ITEM |
                PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_CARRYING_ACTOR | PLAYER_STATE1_CLIMBING_LADDER |
                PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_FIRST_PERSON | PLAYER_STATE1_DAMAGED;
    if (player->stateFlags1 & deferMask) {
        // OOT is handling this — sync Mario to wherever Link is
        if (p_sm64_set_mario_position) {
            p_sm64_set_mario_position(sSm64MarioId, player->actor.world.pos.x, player->actor.world.pos.y,
                                      player->actor.world.pos.z);
        }
        return;
    }

    // Normal frame: tick libsm64 with OOT inputs

    cam = GET_ACTIVE_CAM(play);
    lookX = cam->at.x - cam->eye.x;
    lookZ = cam->at.z - cam->eye.z;
    lookMag = sqrtf(lookX * lookX + lookZ * lookZ);
    if (lookMag < 0.001f)
        lookMag = 0.001f;

    input = &play->state.input[0];

    memset(&inputs, 0, sizeof(inputs));
    inputs.camLookX = lookX / lookMag;
    inputs.camLookZ = lookZ / lookMag;
    inputs.stickX = (float)input->rel.stick_x / 64.0f;
    inputs.stickY = -(float)input->rel.stick_y / 64.0f;
    inputs.buttonA = (input->cur.button & BTN_A) ? 1 : 0;
    inputs.buttonB = (input->cur.button & BTN_B) ? 1 : 0;
    inputs.buttonZ = (input->cur.button & BTN_Z) ? 1 : 0;

    // Water level from OOT water boxes
    if (p_sm64_set_mario_water_level) {
        waterY = Sm64Surfaces_GetWaterLevel(play, player->actor.world.pos.x, player->actor.world.pos.z);
        p_sm64_set_mario_water_level(sSm64MarioId, (int)waterY);
    }

    // Tick SM64 engine
    sSm64OutBuffers.numTrianglesUsed = 0;
    p_sm64_mario_tick(sSm64MarioId, &inputs, &sSm64OutState, &sSm64OutBuffers);

    // Write Mario's position to Link (SM64 physics drive movement)
    player->actor.world.pos.x = sSm64OutState.position[0];
    player->actor.world.pos.y = sSm64OutState.position[1];
    player->actor.world.pos.z = sSm64OutState.position[2];
    player->actor.shape.rot.y = (s16)(sSm64OutState.faceAngle / 3.14159f * 32768.0f);
    player->actor.world.rot.y = player->actor.shape.rot.y;
    player->linearVelocity = sSm64OutState.forwardVelocity;
    player->actor.velocity.y = sSm64OutState.velocity[1];
    player->actor.prevPos = player->actor.world.pos;
}

void Sm64Mario_Draw(PlayState* play, Player* player) {
    if (!sSm64Initialized || sSm64MarioId < 0)
        return;
    Sm64Render_DrawMarioMesh(play, &sSm64OutBuffers, sSm64OutState.position[0], sSm64OutState.position[1],
                             sSm64OutState.position[2]);
}

u8 Sm64Mario_IsActive(void) {
    return CVarGetInteger("gSm64Mario", 0) != 0;
}

void Sm64Mario_Reset(void) {
    if (sSm64MarioId >= 0 && p_sm64_mario_delete) {
        p_sm64_mario_delete(sSm64MarioId);
        sSm64MarioId = -1;
    }
}

void Sm64Mario_OnSceneChange(PlayState* play) {
    if (sSm64Initialized && sSm64MarioId >= 0) {
        Sm64_LoadSceneSurfaces(play);
    }
}

// SyncPositionToPlayer removed — position override now happens inside Sm64Mario_Update
