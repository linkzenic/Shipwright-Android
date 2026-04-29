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
    total = staticCount + dynaCapacity;

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
        if (floorOnly && !isFloorPoly(poly))
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
                    if (floorOnly && !isFloorPoly(&dPolyList[p]))
                        continue;
                    writeSurface(&surfaces[outIdx++], &dPolyList[p], dVtxList);
                }
            }
        }
    }

    *outCount = outIdx;
    return surfaces;
}

f32 Sm64Surfaces_GetWaterLevel(PlayState* play, f32 x, f32 z) {
    f32 ySurface;
    WaterBox* waterBox;
    if (WaterBox_GetSurface1(play, &play->colCtx, x, z, &ySurface, &waterBox)) {
        return ySurface;
    }
    return -11000.0f;
}
