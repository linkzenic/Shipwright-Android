/**
 * Somaria Cubes System (Elegy Statues)
 * Form-dependent statues spawned by Cane of Somaria (max 3)
 * - Uses MM Elegy of Emptiness shell models from mm.o2r
 * - Form based on current player transformation
 * - Hookshotable
 * - Switchhookable (future)
 * - Can press switches (all forms except Deku; Goron = heavy switches)
 * - Dominion Rod orb swap: hitting a statue swaps player/statue positions
 *
 * Uses actor hijacking on En_Lightbox
 */

#ifndef SOMARIA_CUBES_H
#define SOMARIA_CUBES_H

#include "z64.h"
#include "elegy_shell_assets.h"

// ============================================================================
// STATUE PROPERTIES
// ============================================================================

#define SOMARIA_MAX_CUBES 3
#define SOMARIA_MAX_COLLIDERS 12 // 3 local + up to 9 remote (3 per remote player)
#define SOMARIA_CUBE_SCALE 0.01f // Elegy shells are large models, scale down
#define SOMARIA_CUBE_SIZE 30
#define SOMARIA_SPAWN_FRAMES 20

// Physics
#define SOMARIA_GRAVITY -2.0f
#define SOMARIA_THROW_VEL_Y 8.0f
#define SOMARIA_THROW_SPEED_XZ 6.0f
#define SOMARIA_MIN_VEL_Y -20.0f

// Collision cylinder (like MM's EnTorch2)
#define SOMARIA_CYL_RADIUS 20
#define SOMARIA_CYL_HEIGHT 60
#define SOMARIA_MASS 255 // MASS_IMMOVABLE

// Knockback (AT damage when thrown)
#define SOMARIA_KNOCKBACK_DAMAGE 4

// ============================================================================
// STATE MACROS (using hijacked actor fields)
// ============================================================================

#define SOMARIA_GET_STATE(actor) ((actor)->home.rot.x)
#define SOMARIA_SET_STATE(actor, s) ((actor)->home.rot.x = (s))
#define SOMARIA_GET_TIMER(actor) ((actor)->home.rot.z)
#define SOMARIA_SET_TIMER(actor, t) ((actor)->home.rot.z = (t))
#define SOMARIA_GET_FORM(actor) ((actor)->home.rot.y)
#define SOMARIA_SET_FORM(actor, f) ((actor)->home.rot.y = (f))

typedef enum {
    SOMARIA_STATE_SPAWN = 0,
    SOMARIA_STATE_IDLE = 1,
    SOMARIA_STATE_HELD = 2,
    SOMARIA_STATE_THROWN = 3,
} SomariaCubeState;

// ============================================================================
// CUSTOM FLAGS
// ============================================================================

// Flag for switchhook compatibility (future use)
#define ACTOR_FLAG_SWITCHHOOKABLE (1 << 28)

// ============================================================================
// BGCHECK FLAGS
// ============================================================================

#ifndef BGCHECKFLAG_GROUND
#define BGCHECKFLAG_GROUND 0x0001
#define BGCHECKFLAG_WALL 0x0008
#endif

// ============================================================================
// FUNCTIONS
// ============================================================================

Actor* SomariaCube_Spawn(PlayState* play, Vec3f* pos, s16 yaw);
u8 SomariaCube_IsSomariaCube(Actor* actor);
u8 SomariaCube_IsSwitchable(Actor* actor);
void SomariaCube_PlaySound(Actor* actor, u16 sfxId);
u8 SomariaCube_GetForm(Actor* actor);

// Remote cube functions (for Harpoon multiplayer sync)
Actor* SomariaCube_SpawnRemote(PlayState* play, Vec3f* pos, s16 yaw, u8 form);
void SomariaCube_UpdateRemotePos(Actor* cube, Vec3f* pos, f32 scale, s16 rotY);
u8 SomariaCube_IsRemoteCube(Actor* actor);

#endif // SOMARIA_CUBES_H
