/**
 * Grappling Helper Implementation
 *
 * Surface shape analysis for grapple-type items.
 * Uses neighbor polygon walking + bounding box to detect beam/bar geometry.
 */

#include "grappling_helper.h"
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include <math.h>

// Forward declarations from z_bgcheck.c
extern CollisionHeader* BgCheck_GetCollisionHeader(CollisionContext* colCtx, s32 bgId);
extern u32 SurfaceType_IsHookshotSurface(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
extern void CollisionPoly_GetNormalF(CollisionPoly* poly, f32* nx, f32* ny, f32* nz);

// =============================================================================
// Internal: sort 3 floats ascending
// =============================================================================
static void SortDims3(f32* a, f32* b, f32* c) {
    f32 tmp;
    if (*a > *b) {
        tmp = *a;
        *a = *b;
        *b = tmp;
    }
    if (*b > *c) {
        tmp = *b;
        *b = *c;
        *c = tmp;
    }
    if (*a > *b) {
        tmp = *a;
        *a = *b;
        *b = tmp;
    }
}

// =============================================================================
// Internal: expand bounding box with a Vec3s vertex
// =============================================================================
static void ExpandBBox(Vec3f* bMin, Vec3f* bMax, Vec3s* vtx) {
    if (vtx->x < bMin->x)
        bMin->x = vtx->x;
    if (vtx->y < bMin->y)
        bMin->y = vtx->y;
    if (vtx->z < bMin->z)
        bMin->z = vtx->z;
    if (vtx->x > bMax->x)
        bMax->x = vtx->x;
    if (vtx->y > bMax->y)
        bMax->y = vtx->y;
    if (vtx->z > bMax->z)
        bMax->z = vtx->z;
}

// =============================================================================
// Internal: get center of a polygon
// =============================================================================
static void GetPolyCenterF(Vec3s* vtxList, u16 idxA, u16 idxB, u16 idxC, Vec3f* out) {
    out->x = (vtxList[idxA].x + vtxList[idxB].x + vtxList[idxC].x) / 3.0f;
    out->y = (vtxList[idxA].y + vtxList[idxB].y + vtxList[idxC].y) / 3.0f;
    out->z = (vtxList[idxA].z + vtxList[idxB].z + vtxList[idxC].z) / 3.0f;
}

// =============================================================================
// Internal: check if two polygons have similar normals (within ~30 degrees)
// =============================================================================
static s32 NormalsAreSimilar(CollisionPoly* p1, CollisionPoly* p2) {
    f32 n1x, n1y, n1z, n2x, n2y, n2z, dot;
    CollisionPoly_GetNormalF(p1, &n1x, &n1y, &n1z);
    CollisionPoly_GetNormalF(p2, &n2x, &n2y, &n2z);
    dot = n1x * n2x + n1y * n2y + n1z * n2z;
    return (dot > 0.85f); // cos(30 deg) ≈ 0.866
}

// =============================================================================
// Grapple_AnalyzeSurface
// =============================================================================
s32 Grapple_AnalyzeSurface(PlayState* play, CollisionPoly* poly, s32 bgId, Vec3f* hitPos, GrappleTarget* outTarget) {
    CollisionHeader* colHeader;
    Vec3s* vtxList;
    u16 idxA, idxB, idxC;
    Vec3f bMin, bMax, hitCenter;
    f32 dx, dy, dz, aspectRatio;

    if (outTarget == NULL || poly == NULL)
        return 0;

    // Initialize output
    outTarget->poly = poly;
    outTarget->bgId = bgId;
    outTarget->isGraspable = 0;
    outTarget->isHookshottable = 0;

    if (hitPos != NULL) {
        outTarget->attachPoint = *hitPos;
    }

    // Get surface normal
    CollisionPoly_GetNormalF(poly, &outTarget->surfaceNormal.x, &outTarget->surfaceNormal.y,
                             &outTarget->surfaceNormal.z);

    // Check hookshot flag (info only, not automatic graspable)
    outTarget->isHookshottable = SurfaceType_IsHookshotSurface(&play->colCtx, poly, bgId);

    // Get collision header
    colHeader = BgCheck_GetCollisionHeader(&play->colCtx, bgId);
    if (colHeader == NULL)
        return 0;

    // Get vertex list
    if (bgId == BGCHECK_SCENE) {
        vtxList = colHeader->vtxList;
    } else {
        vtxList = play->colCtx.dyna.vtxList;
    }
    if (vtxList == NULL)
        return 0;

    // Get hit poly vertex indices
    idxA = COLPOLY_VTX_INDEX(poly->flags_vIA);
    idxB = COLPOLY_VTX_INDEX(poly->flags_vIB);
    idxC = poly->vIC;

    // Get hit poly center for proximity checks
    GetPolyCenterF(vtxList, idxA, idxB, idxC, &hitCenter);

    // Initialize bounding box from hit poly vertices
    bMin.x = bMin.y = bMin.z = 99999.0f;
    bMax.x = bMax.y = bMax.z = -99999.0f;

    ExpandBBox(&bMin, &bMax, &vtxList[idxA]);
    ExpandBBox(&bMin, &bMax, &vtxList[idxB]);
    ExpandBBox(&bMin, &bMax, &vtxList[idxC]);

    // Determine if hit surface is a ceiling (wider neighbor search for cylinders)
    {
        f32 nx, ny, nz;
        f32 neighborDist;
        CollisionPoly_GetNormalF(poly, &nx, &ny, &nz);
        neighborDist = (ny < -0.5f) ? (GRAPPLE_NEIGHBOR_DIST * 2.0f) : GRAPPLE_NEIGHBOR_DIST;

        // Walk neighbor polygons: similar normal AND (shared vertex OR within distance)
        for (u16 i = 0; i < colHeader->numPolygons; i++) {
            CollisionPoly* other = &colHeader->polyList[i];
            u16 oA, oB, oC;
            s32 shared;
            Vec3f otherCenter;
            f32 distSq;

            if (other == poly)
                continue;

            // Must have similar surface normal (facing same direction)
            if (!NormalsAreSimilar(poly, other))
                continue;

            oA = COLPOLY_VTX_INDEX(other->flags_vIA);
            oB = COLPOLY_VTX_INDEX(other->flags_vIB);
            oC = other->vIC;

            // Check if shares at least 1 vertex with hit poly
            shared = (oA == idxA || oA == idxB || oA == idxC || oB == idxA || oB == idxB || oB == idxC || oC == idxA ||
                      oC == idxB || oC == idxC);

            if (!shared) {
                // Check proximity: is the other poly center close to hit poly center?
                GetPolyCenterF(vtxList, oA, oB, oC, &otherCenter);
                dx = otherCenter.x - hitCenter.x;
                dy = otherCenter.y - hitCenter.y;
                dz = otherCenter.z - hitCenter.z;
                distSq = dx * dx + dy * dy + dz * dz;
                if (distSq > neighborDist * neighborDist)
                    continue;
            }

            ExpandBBox(&bMin, &bMax, &vtxList[oA]);
            ExpandBBox(&bMin, &bMax, &vtxList[oB]);
            ExpandBBox(&bMin, &bMax, &vtxList[oC]);
        }
    }

    // Calculate dimensions
    dx = bMax.x - bMin.x;
    dy = bMax.y - bMin.y;
    dz = bMax.z - bMin.z;

    // Sort dimensions: smallest, middle, largest
    SortDims3(&dx, &dy, &dz);
    outTarget->dims[0] = dx; // smallest
    outTarget->dims[1] = dy; // middle
    outTarget->dims[2] = dz; // largest

    // Calculate aspect ratio (how elongated the shape is)
    aspectRatio = (dy > 0.1f) ? (dz / dy) : 10.0f;

    // Check if this is a ceiling surface (normal pointing mostly downward)
    {
        s32 isCeiling = (outTarget->surfaceNormal.y < -0.5f);

        if (isCeiling) {
            // Ceiling surfaces: more lenient detection for beams/bars/cylinders
            // Hookshottable ceiling surfaces are always graspable
            // Otherwise, any elongated shape or reasonable cross-section counts
            outTarget->isGraspable =
                outTarget->isHookshottable ||
                ((dz >= GRAPPLE_MIN_LENGTH * 0.5f) && (dx <= GRAPPLE_MAX_CROSS_SECTION * 1.5f)) ||
                ((dz >= GRAPPLE_MIN_LENGTH * 0.5f) && (aspectRatio >= GRAPPLE_ASPECT_RATIO * 0.75f));
        } else {
            // Standard wall/floor: elongated shape (beam/bar/ledge)
            outTarget->isGraspable =
                // Traditional beam/bar check (relaxed thresholds)
                ((dz >= GRAPPLE_MIN_LENGTH) && (dx >= GRAPPLE_MIN_THICKNESS) && (dx <= GRAPPLE_MAX_CROSS_SECTION) &&
                 (dy <= GRAPPLE_MAX_CROSS_SECTION) && (dx + dy <= GRAPPLE_MAX_CROSS_SUM)) ||
                // Elongated shape check (high aspect ratio)
                ((dz >= GRAPPLE_MIN_LENGTH) && (aspectRatio >= GRAPPLE_ASPECT_RATIO) &&
                 (dx <= GRAPPLE_MAX_CROSS_SECTION));
        }
    }

    return outTarget->isGraspable;
}

// =============================================================================
// Grapple_FindTarget
// =============================================================================
s32 Grapple_FindTarget(PlayState* play, Player* player, f32 maxRange, GrappleTarget* outTarget) {
    Vec3f rayStart, rayEnd;
    Vec3f hitPos;
    CollisionPoly* hitPoly = NULL;
    s32 bgId = BGCHECK_SCENE;
    s16 aimYaw, aimPitch;
    f32 cosP, sinP, cosY, sinY;

    if (outTarget == NULL || player == NULL)
        return 0;

    // Determine aim direction
    if (Player_IsZTargeting(player) && player->focusActor != NULL) {
        // Z-target: aim at focus actor
        Vec3f targetPos = player->focusActor->focus.pos;
        f32 dx = targetPos.x - player->actor.world.pos.x;
        f32 dy = targetPos.y - (player->actor.world.pos.y + 50.0f);
        f32 dz = targetPos.z - player->actor.world.pos.z;
        f32 hDist = sqrtf(dx * dx + dz * dz);
        aimYaw = Math_Atan2S(dx, dz);
        aimPitch = Math_Atan2S(-dy, hDist);
    } else {
        // Free aim: use player facing direction
        aimYaw = player->actor.shape.rot.y;
        aimPitch = 0;
    }

    // Calculate ray start (player eye position)
    rayStart.x = player->actor.world.pos.x;
    rayStart.y = player->actor.world.pos.y + 50.0f; // eye height
    rayStart.z = player->actor.world.pos.z;

    // Calculate ray end
    cosP = Math_CosS(aimPitch);
    sinP = Math_SinS(aimPitch);
    cosY = Math_CosS(aimYaw);
    sinY = Math_SinS(aimYaw);

    rayEnd.x = rayStart.x + sinY * cosP * maxRange;
    rayEnd.y = rayStart.y - sinP * maxRange;
    rayEnd.z = rayStart.z + cosY * cosP * maxRange;

    // Cast line test
    if (!BgCheck_EntityLineTest1(&play->colCtx, &rayStart, &rayEnd, &hitPos, &hitPoly, true, true, true, true, &bgId)) {
        return 0; // Nothing hit
    }

    // Analyze the surface
    Grapple_AnalyzeSurface(play, hitPoly, bgId, &hitPos, outTarget);

    return 1;
}
