/**
 * bremen_follower_actor.c - Bremen Mask chick / adult cucco follower.
 *
 * - Chick = ACTOR_EN_NWC (cucco cluster — Unfinished in OOT, no hostile path).
 * - Adult = ACTOR_EN_NIW (full cucco).
 *
 * Both actors get their `update` function pointer replaced with a custom
 * follower update; the vanilla update is NEVER invoked, which keeps EnNiw
 * out of its swarm/aggro state machine and makes the follower pacific by
 * construction.
 *
 * Follower physics: a Vec3f trail buffer is filled from Link's world.pos
 * every frame; the follower targets a sample ~30 frames in the past, so it
 * lags about half a second behind. XZ approach via Math_ApproachF; Y is
 * snapped to ground via Actor_UpdateBgCheckInfo.
 *
 * Scene transitions: actors are destroyed on scene-load, so we detect frame-
 * counter rewind (mailbox pattern) and re-spawn the appropriate follower
 * at Link's current position from persistent state.
 */

#include "bremen_follower_actor.h"

#include <string.h>
#include "macros.h"

extern PlayState* gPlayState;

// ────────── Persistent state (cleared only on death) ──────────
static u8 sFollowerSpawnedThisRun = 0;   // 1 = chick OR adult has been seen this run
static u8 sFollowerIsAdult = 0;          // 0 = chick mode, 1 = adult mode
static Actor* sFollowerActor = NULL;     // Currently-tracked follower in this scene

// Link trail ring buffer (60 frames = ~3 s history).
#define TRAIL_LEN 60
#define TRAIL_LAG 30  // 0.5 s lag
static Vec3f sLinkTrail[TRAIL_LEN];
static s32 sTrailHead = 0;
static u8 sTrailPrimed = 0;

// Scene-transition detection.
static s16 sLastSceneNum = -1;
static u32 sLastFrames = 0;

// Forward decls.
static void BremenFollower_Update(Actor* thisx, PlayState* play);

// ────────── Trail buffer ──────────
static void TrailReset(const Vec3f* p) {
    for (s32 i = 0; i < TRAIL_LEN; i++) {
        sLinkTrail[i] = *p;
    }
    sTrailHead = 0;
    sTrailPrimed = 1;
}

static void TrailPush(const Vec3f* p) {
    if (!sTrailPrimed) {
        TrailReset(p);
        return;
    }
    sTrailHead = (sTrailHead + 1) % TRAIL_LEN;
    sLinkTrail[sTrailHead] = *p;
}

static Vec3f TrailSampleLagged(void) {
    s32 idx = (sTrailHead - TRAIL_LAG + TRAIL_LEN * 2) % TRAIL_LEN;
    return sLinkTrail[idx];
}

// ────────── Pacific configuration shared by chick + adult ──────────
static void ConfigureAsPacificFollower(Actor* actor) {
    actor->update = BremenFollower_Update;
    actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
    actor->flags |= ACTOR_FLAG_DRAW_CULLING_DISABLED;
    // OOT equivalent of MM's "untargetable" — disable Z-target attention.
    actor->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
    actor->gravity = -1.0f;
}

// ────────── Custom update: trail-follow ──────────
static void BremenFollower_Update(Actor* thisx, PlayState* play) {
    Player* player = GET_PLAYER(play);
    if (player == NULL)
        return;

    Vec3f target = TrailSampleLagged();

    // XZ smooth approach.
    Math_ApproachF(&thisx->world.pos.x, target.x, 0.3f, 8.0f);
    Math_ApproachF(&thisx->world.pos.z, target.z, 0.3f, 8.0f);

    // Y: pull toward target Y but apply gravity + bg check for ground snap.
    thisx->world.pos.y += thisx->velocity.y;
    thisx->velocity.y += thisx->gravity;
    if (thisx->velocity.y < -8.0f) thisx->velocity.y = -8.0f;
    if (thisx->world.pos.y < target.y - 40.0f) thisx->world.pos.y = target.y - 40.0f;

    // Face the direction of travel (Link's position).
    f32 dx = player->actor.world.pos.x - thisx->world.pos.x;
    f32 dz = player->actor.world.pos.z - thisx->world.pos.z;
    if ((dx * dx + dz * dz) > 1.0f) {
        thisx->shape.rot.y = Math_Atan2S(dz, dx);
    }

    // Ground snap. Flag value 5 (= 0x1 ground + 0x4 floor-snap) matches
    // typical actor patterns in z_actor.c.
    Actor_UpdateBgCheckInfo(play, thisx, 8.0f, 12.0f, 30.0f, 5);
    if (thisx->bgCheckFlags & 1 /* on-ground */) {
        thisx->velocity.y = 0.0f;
    }
}

// ────────── Spawn helpers ──────────
Actor* BremenFollower_SpawnChick(PlayState* play, Player* player) {
    Vec3f pos = player->actor.world.pos;
    Actor* a = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_NWC, pos.x, pos.y, pos.z, 0,
                           player->actor.shape.rot.y, 0, 0);
    if (a != NULL) {
        ConfigureAsPacificFollower(a);
        sFollowerActor = a;
        sFollowerIsAdult = 0;
        sFollowerSpawnedThisRun = 1;
        TrailReset(&pos);
    }
    return a;
}

Actor* BremenFollower_SpawnAdult(PlayState* play, const Vec3f* pos, s16 yaw) {
    Actor* a = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_NIW, pos->x, pos->y, pos->z, 0, yaw, 0, 0);
    if (a != NULL) {
        ConfigureAsPacificFollower(a);
        sFollowerActor = a;
        sFollowerIsAdult = 1;
        sFollowerSpawnedThisRun = 1;
    }
    return a;
}

// ────────── Public API ──────────
void BremenFollower_UpgradeToAdult(PlayState* play, Player* player) {
    Vec3f spawnPos = (sFollowerActor != NULL) ? sFollowerActor->world.pos
                                              : player->actor.world.pos;
    if (sFollowerActor != NULL) {
        Actor_Kill(sFollowerActor);
        sFollowerActor = NULL;
    }
    BremenFollower_SpawnAdult(play, &spawnPos, player->actor.shape.rot.y);
}

u8 BremenFollower_IsAdult(void) {
    return sFollowerIsAdult;
}

void BremenFollower_OnDeath(void) {
    // Caller is responsible for clearing sBremenWornTotalFrames /
    // sBremenAdultCuccoSpawned over in mm_mask_wear.cpp.
    sFollowerSpawnedThisRun = 0;
    sFollowerIsAdult = 0;
    sFollowerActor = NULL;
    sTrailPrimed = 0;
    sLastSceneNum = -1;
}

void BremenFollower_Tick(PlayState* play, Player* player) {
    if (player == NULL)
        return;

    // Push Link's pos onto the trail every frame so the follower has fresh history.
    TrailPush(&player->actor.world.pos);

    // Scene-transition detection: re-spawn the appropriate follower if we
    // were already tracking one this run.
    s32 sceneChanged = (play->sceneNum != sLastSceneNum) || (play->state.frames < sLastFrames);
    if (sceneChanged) {
        sLastSceneNum = play->sceneNum;
        sFollowerActor = NULL; // Actor pointer is dangling after scene reload.
        if (sFollowerSpawnedThisRun) {
            if (sFollowerIsAdult) {
                BremenFollower_SpawnAdult(play, &player->actor.world.pos, player->actor.shape.rot.y);
            } else {
                BremenFollower_SpawnChick(play, player);
            }
        }
    }
    sLastFrames = play->state.frames;
}
