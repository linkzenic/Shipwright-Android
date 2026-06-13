/**
 * sm64_mario_surfaces.c - Extract OOT collision geometry for libsm64
 *
 * Converts OOT CollisionPoly into SM64Surface[]. Pulls both static scene
 * collision (play->colCtx.colHeader) AND dynamic BgActor collision
 * (play->colCtx.dyna). Some scenes (e.g. Forest Meadow) spawn Link on
 * a dynamic BgActor platform — without the dyna pass, libsm64's find_floor
 * returns NULL and mario_create fails.
 */

#include "z64.h"
#include "functions.h"
#define SM64_LIB_FN
#include "expansions/sm64/libsm64.h"

// Must match SM64_WORLD_SCALE in sm64_mario.c. OOT world → libsm64 world
// scale factor: libsm64's Mario is SM64-sized (~180u tall) vs OOT Link
// (~60u), so we upsize OOT geometry by 4 to give Mario room to fit.
#define SM64_WORLD_SCALE 4

// Write one SM64 surface from an OOT poly+vtxList. Vertices are passed in
// OOT's original order (A, B, C). libsm64 computes normals as
// (v2-v1)×(v3-v2), which is mathematically equivalent to (v2-v1)×(v3-v1),
// matching OOT's own Math3D_SurfaceNorm (sys_math3d.c:504 — (vB-vA)×(vC-vA)).
// Same direction → floors keep normal.y > 0 → accepted by find_floor.
// Reversing to (C, B, A) inverts the normal, turning every floor into a
// ceiling in libsm64's view and failing the filter at surface_collision.c:104.
static void writeSurface(struct SM64Surface* out, CollisionPoly* poly, Vec3s* vtxList) {
    u16 idxA = COLPOLY_VTX_INDEX(poly->flags_vIA);
    u16 idxB = COLPOLY_VTX_INDEX(poly->flags_vIB);
    u16 idxC = poly->vIC;

    out->type = 0;
    out->force = 0;
    out->terrain = 0;

    out->vertices[0][0] = vtxList[idxA].x * SM64_WORLD_SCALE;
    out->vertices[0][1] = vtxList[idxA].y * SM64_WORLD_SCALE;
    out->vertices[0][2] = vtxList[idxA].z * SM64_WORLD_SCALE;

    out->vertices[1][0] = vtxList[idxB].x * SM64_WORLD_SCALE;
    out->vertices[1][1] = vtxList[idxB].y * SM64_WORLD_SCALE;
    out->vertices[1][2] = vtxList[idxB].z * SM64_WORLD_SCALE;

    out->vertices[2][0] = vtxList[idxC].x * SM64_WORLD_SCALE;
    out->vertices[2][1] = vtxList[idxC].y * SM64_WORLD_SCALE;
    out->vertices[2][2] = vtxList[idxC].z * SM64_WORLD_SCALE;
}

// =============================================================================
// Actor OC-collider surfaces (signs, torches, gates, pushable props...).
// libsm64 only knows static scene + dynapoly collision, so any actor that
// blocks the player via its OWN OC ColliderCylinder was invisible to Mario —
// he phased right through it. We snapshot those cylinders each frame (AFTER
// Actor_UpdateAll, when the OC list is fully populated — see
// Sm64Surfaces_RefreshActorColliders) as axis-aligned box walls and append
// them to the surface set. Enemies/bosses are excluded so Mario can still run
// into them to attack. Vanish-cap (floorOnly) skips these like any other wall.
// =============================================================================
#define SM64_ACTOR_SURF_MAX 1024 // ~ COLLISION_CHECK_OC_MAX(50) * 16 walls + headroom
static struct SM64Surface sActorSurfaces[SM64_ACTOR_SURF_MAX];
static u32 sActorSurfaceCount = 0;

// Write one SM64 surface from three OOT-space points (scaled into libsm64 world).
static void emitActorTri(struct SM64Surface* out, f32 ax, f32 ay, f32 az, f32 bx, f32 by, f32 bz, f32 cx, f32 cy,
                         f32 cz) {
    out->type = 0;
    out->force = 0;
    out->terrain = 0;
    out->vertices[0][0] = (s32)(ax * SM64_WORLD_SCALE);
    out->vertices[0][1] = (s32)(ay * SM64_WORLD_SCALE);
    out->vertices[0][2] = (s32)(az * SM64_WORLD_SCALE);
    out->vertices[1][0] = (s32)(bx * SM64_WORLD_SCALE);
    out->vertices[1][1] = (s32)(by * SM64_WORLD_SCALE);
    out->vertices[1][2] = (s32)(bz * SM64_WORLD_SCALE);
    out->vertices[2][0] = (s32)(cx * SM64_WORLD_SCALE);
    out->vertices[2][1] = (s32)(cy * SM64_WORLD_SCALE);
    out->vertices[2][2] = (s32)(cz * SM64_WORLD_SCALE);
}

// Unit-circle directions for an 8-sided prism (octagon). Vertices sit ON the
// cylinder; the edges fall only ~0.08*r inside it — a far closer fit than an
// axis-aligned box, whose corners poked ~0.41*r past the cylinder and made
// Mario collide with "invented" phantom walls near signs/torches/props.
static const f32 kOctDir[8][2] = {
    { 1.000000f, 0.000000f },   { 0.707107f, 0.707107f },   { 0.000000f, 1.000000f },  { -0.707107f, 0.707107f },
    { -1.000000f, 0.000000f },  { -0.707107f, -0.707107f }, { 0.000000f, -1.000000f }, { 0.707107f, -0.707107f },
};

// Emit an 8-wall prism (16 triangles) around a cylinder footprint of radius r,
// from y=by (bottom) to y=ty (top). Vertex orders give OUTWARD-facing normals
// so libsm64 treats each face as a solid wall (one-sided from outside).
static void emitActorPrism(f32 cx, f32 cz, f32 by, f32 ty, f32 r) {
    s32 k;
    if (sActorSurfaceCount + 16 > SM64_ACTOR_SURF_MAX)
        return;
    for (k = 0; k < 8; k++) {
        const f32* d0 = kOctDir[k];
        const f32* d1 = kOctDir[(k + 1) & 7];
        f32 b0x = cx + d0[0] * r, b0z = cz + d0[1] * r;
        f32 b1x = cx + d1[0] * r, b1z = cz + d1[1] * r;
        struct SM64Surface* s = &sActorSurfaces[sActorSurfaceCount];
        // Outward-facing winding: tri(B1,B0,T0) + tri(B1,T0,T1).
        emitActorTri(s++, b1x, by, b1z, b0x, by, b0z, b0x, ty, b0z);
        emitActorTri(s++, b1x, by, b1z, b0x, ty, b0z, b1x, ty, b1z);
        sActorSurfaceCount += 2;
    }
}

// Forward decl — Sm64Surfaces_ExtractStatic delegates to Filtered, which is
// defined below. Without this forward decl C falls back to implicit-int
// return type for the call site, conflicting with the actual SM64Surface*
// return at the definition (C2040 differing levels of indirection).
struct SM64Surface* Sm64Surfaces_ExtractFiltered(PlayState* play, u32* outCount, u8 floorOnly);

// Returns 1 if the poly is a "floor-like" surface — normal.y > threshold.
// Vanish-cap mode strips everything that's not floor-like so Mario can phase
// through walls and ceilings, but still has ground to stand on. Threshold
// 0.5 ≈ slope of 60° from vertical, matching OOT's standard floor cutoff.
static u8 isFloorPoly(CollisionPoly* poly) {
    f32 ny = COLPOLY_GET_NORMAL(poly->normal.y);
    return ny > 0.5f;
}

// Returns 1 if the poly is a scene-exit / loading-zone wall (exit index != 0).
// Even in vanish-cap pass-through mode we KEEP these solid in libsm64 so Mario
// still collides with the loading-zone wall — OOT's Player_HandleExitsAndVoids
// (z_player.c:5560) reads that wallPoly to trigger the scene transition, so if
// we let Mario phase through it he sails into the void and the loading zone
// (and the enemies in the room he left) never engage. Voids are floor-type and
// are already preserved by isFloorPoly, so only exit *walls* need this guard.
static u8 isExitPoly(PlayState* play, CollisionPoly* poly, s32 bgId) {
    return SurfaceType_GetSceneExitIndex(&play->colCtx, poly, bgId) != 0;
}

// Extract static + dynamic collision into SM64Surface[]. When floorOnly = 1,
// only floor-like polys are emitted (vanish-cap pass-through mode).
struct SM64Surface* Sm64Surfaces_ExtractStatic(PlayState* play, u32* outCount) {
    return Sm64Surfaces_ExtractFiltered(play, outCount, 0);
}

struct SM64Surface* Sm64Surfaces_ExtractFiltered(PlayState* play, u32* outCount, u8 floorOnly) {
    CollisionHeader* colHeader;
    CollisionPoly* polyList;
    Vec3s* vtxList;
    u32 staticCount;
    u32 dynaCapacity;
    u32 total;
    u32 outIdx = 0;
    u32 i;
    s32 bgId;
    struct SM64Surface* surfaces;

    if (play == NULL || play->colCtx.colHeader == NULL || play->colCtx.colHeader->numPolygons == 0) {
        *outCount = 0;
        return NULL;
    }

    colHeader = play->colCtx.colHeader;
    staticCount = colHeader->numPolygons;
    polyList = colHeader->polyList;
    vtxList = colHeader->vtxList;

    // Upper bound for total surfaces: static + whatever dyna capacity allows.
    // dyna.polyListMax is the allocation size; we'll only copy active ones.
    dynaCapacity = play->colCtx.dyna.polyListMax;
    total = staticCount + dynaCapacity + (floorOnly ? 0 : sActorSurfaceCount);

    surfaces = malloc(total * sizeof(struct SM64Surface));
    if (surfaces == NULL) {
        *outCount = 0;
        return NULL;
    }

    // === Static scene collision ===
    // NOTE: bgId must be BGCHECK_SCENE, not 0. When bgId=0 (a dyna actor slot)
    // and that slot is inactive, BgCheck_GetCollisionHeader returns NULL, and
    // SurfaceType_IsIgnoredByEntities (z_bgcheck.c:4132-4133) conservatively
    // returns true — skipping every poly. This bug silently filtered out all
    // 2210 polygons in Lost Woods (slot 0 happened to be inactive there).
    for (i = 0; i < staticCount; i++) {
        CollisionPoly* poly = &polyList[i];
        if (SurfaceType_IsIgnoredByEntities(&play->colCtx, poly, BGCHECK_SCENE))
            continue;
        // Vanish cap: keep floors AND loading-zone/exit walls solid; phase
        // through everything else.
        if (floorOnly && !isFloorPoly(poly) && !isExitPoly(play, poly, BGCHECK_SCENE))
            continue;
        writeSurface(&surfaces[outIdx++], poly, vtxList);
    }

    // === Dynamic BgActor collision (moving platforms, doors, Forest Meadow
    //     pedestals, etc.). Vertices in dyna.vtxList are already world-space
    //     (see z_bgcheck.c:2843-2857 where the actor's SRT is applied). ===
    {
        DynaCollisionContext* dyna = &play->colCtx.dyna;
        CollisionPoly* dPolyList = dyna->polyList;
        Vec3s* dVtxList = dyna->vtxList;
        if (dPolyList != NULL && dVtxList != NULL) {
            for (bgId = 0; bgId < BG_ACTOR_MAX; bgId++) {
                BgActor* bg;
                u32 polyStart, polyEnd, p;
                if (!(dyna->bgActorFlags[bgId] & 1)) continue;
                bg = &dyna->bgActors[bgId];
                if (bg->colHeader == NULL) continue;
                polyStart = bg->dynaLookup.polyStartIndex;
                polyEnd = polyStart + bg->colHeader->numPolygons;
                if (polyEnd > dynaCapacity) polyEnd = dynaCapacity;
                for (p = polyStart; p < polyEnd; p++) {
                    // Honor the player-ignore flag (flags_vIA & 0x4000) on dynapoly
                    // too — same as the static pass. This is what makes Lens-of-Truth
                    // "fake walls" (and other entity-ignored bgActor polys) passable;
                    // without it Mario collided with illusory walls and phantom
                    // geometry the engine never blocks Link with.
                    if (SurfaceType_IsIgnoredByEntities(&play->colCtx, &dPolyList[p], bgId))
                        continue;
                    // Vanish cap: keep floors + exit walls on dynamic bgActors
                    // too (e.g. door-mounted loading zones).
                    if (floorOnly && !isFloorPoly(&dPolyList[p]) &&
                        !isExitPoly(play, &dPolyList[p], bgId))
                        continue;
                    writeSurface(&surfaces[outIdx++], &dPolyList[p], dVtxList);
                }
            }
        }
    }

    // === Actor OC colliders (signs, torches, props) snapshotted last frame ===
    // Solid mode only; vanish-cap (floorOnly) phases through them like walls.
    if (!floorOnly) {
        for (i = 0; i < sActorSurfaceCount; i++) {
            surfaces[outIdx++] = sActorSurfaces[i];
        }
    }

    *outCount = outIdx;
    return surfaces;
}

// Snapshot every player-blocking actor OC ColliderCylinder into sActorSurfaces
// as box walls. MUST be called AFTER Actor_UpdateAll (z_play.c) — that's when
// colChkCtx.colOC[] holds the full frame's OC list (it is cleared right before
// Actor_UpdateAll, and props/doors register AFTER the player updates, so reading
// it mid-player-update would miss them). The next surface refresh in
// Sm64Mario_Update (1-frame lag) uploads them; fine for static props.
void Sm64Surfaces_RefreshActorColliders(PlayState* play) {
    CollisionCheckContext* cc;
    s32 i;

    sActorSurfaceCount = 0;
    if (play == NULL)
        return;
    cc = &play->colChkCtx;

    for (i = 0; i < cc->colOCCount; i++) {
        Collider* col = cc->colOC[i];
        Actor* actor;
        ColliderCylinder* cyl;
        f32 r, cx, cz, by, ty;

        if (col == NULL)
            continue;
        actor = col->actor;
        if (actor == NULL)
            continue;
        // Scope: obstacles only. Skip the player (that's Mario himself) and
        // enemies/bosses so Mario can still walk into them to attack.
        if (actor->category == ACTORCAT_PLAYER || actor->category == ACTORCAT_ENEMY ||
            actor->category == ACTORCAT_BOSS)
            continue;
        // Must be an active OC collider that blocks the player.
        if (!(col->ocFlags1 & OC1_ON) || !(col->ocFlags1 & OC1_TYPE_PLAYER))
            continue;
        // v1: cylinders only — covers signs, torch stands, gates and most props.
        if (col->shape != COLSHAPE_CYLINDER)
            continue;

        cyl = (ColliderCylinder*)col;
        r = (f32)cyl->dim.radius;
        if (r <= 0.0f)
            continue;
        cx = (f32)cyl->dim.pos.x;
        cz = (f32)cyl->dim.pos.z;
        by = (f32)cyl->dim.pos.y + (f32)cyl->dim.yShift;
        ty = by + (f32)cyl->dim.height;
        emitActorPrism(cx, cz, by, ty, r);
    }
}

f32 Sm64Surfaces_GetWaterLevel(PlayState* play, f32 x, f32 z) {
    f32 ySurface;
    WaterBox* waterBox;
    if (WaterBox_GetSurface1(play, &play->colCtx, x, z, &ySurface, &waterBox)) {
        return ySurface;
    }
    return -11000.0f;
}
