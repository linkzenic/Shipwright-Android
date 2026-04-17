/**
 * sm64_mario_surfaces.c - Extract OOT collision geometry for libsm64
 *
 * Converts OOT's CollisionPoly + vtxList into SM64Surface[] arrays.
 * Filters out loading zone polygons so Mario can walk through them
 * (the invisible Link handles the actual scene transition).
 */

#include "z64.h"
#include "functions.h"
#define SM64_LIB_FN
#include "expansions/sm64/libsm64.h"

// Extract static collision, skipping loading zones and nearby geometry.
struct SM64Surface* Sm64Surfaces_ExtractStatic(PlayState* play, u32* outCount) {
    CollisionHeader* colHeader = play->colCtx.colHeader;
    CollisionPoly* polyList;
    Vec3s* vtxList;
    u32 numPolys, numVerts;
    u32 outIdx = 0;
    u32 i;
    struct SM64Surface* surfaces;
    u8* exitVerts; // Marks vertices that belong to loading zone polys

    if (colHeader == NULL || colHeader->numPolygons == 0) {
        *outCount = 0;
        return NULL;
    }

    numPolys = colHeader->numPolygons;
    numVerts = colHeader->numVertices;
    polyList = colHeader->polyList;
    vtxList = colHeader->vtxList;

    // Pass 1: Mark vertices that belong to loading zone polygons
    exitVerts = calloc(numVerts, 1);
    if (exitVerts == NULL) {
        *outCount = 0;
        return NULL;
    }

    for (i = 0; i < numPolys; i++) {
        CollisionPoly* poly = &polyList[i];
        if (SurfaceType_GetSceneExitIndex(&play->colCtx, poly, 0) > 0) {
            exitVerts[COLPOLY_VTX_INDEX(poly->flags_vIA)] = 1;
            exitVerts[COLPOLY_VTX_INDEX(poly->flags_vIB)] = 1;
            exitVerts[poly->vIC] = 1;
        }
    }

    // Pass 2: Build surfaces, skipping polys that share vertices with loading zones
    surfaces = malloc(numPolys * sizeof(struct SM64Surface));
    if (surfaces == NULL) {
        free(exitVerts);
        *outCount = 0;
        return NULL;
    }

    for (i = 0; i < numPolys; i++) {
        CollisionPoly* poly = &polyList[i];
        u16 idxA = COLPOLY_VTX_INDEX(poly->flags_vIA);
        u16 idxB = COLPOLY_VTX_INDEX(poly->flags_vIB);
        u16 idxC = poly->vIC;

        // Skip loading zone polygons themselves
        if (SurfaceType_GetSceneExitIndex(&play->colCtx, poly, 0) > 0)
            continue;

        // Skip polys that share vertices with loading zones (walls around exits)
        if (exitVerts[idxA] || exitVerts[idxB] || exitVerts[idxC])
            continue;

        // Skip polys that entities should ignore
        if (SurfaceType_IsIgnoredByEntities(&play->colCtx, poly, 0))
            continue;

        surfaces[outIdx].type = 0;
        surfaces[outIdx].force = 0;
        surfaces[outIdx].terrain = 0;

        surfaces[outIdx].vertices[0][0] = vtxList[idxA].x;
        surfaces[outIdx].vertices[0][1] = vtxList[idxA].y;
        surfaces[outIdx].vertices[0][2] = vtxList[idxA].z;

        surfaces[outIdx].vertices[1][0] = vtxList[idxB].x;
        surfaces[outIdx].vertices[1][1] = vtxList[idxB].y;
        surfaces[outIdx].vertices[1][2] = vtxList[idxB].z;

        surfaces[outIdx].vertices[2][0] = vtxList[idxC].x;
        surfaces[outIdx].vertices[2][1] = vtxList[idxC].y;
        surfaces[outIdx].vertices[2][2] = vtxList[idxC].z;

        outIdx++;
    }

    free(exitVerts);
    *outCount = outIdx;
    return surfaces;
}

// Get water level at Mario's XZ position from OOT water boxes
f32 Sm64Surfaces_GetWaterLevel(PlayState* play, f32 x, f32 z) {
    f32 ySurface;
    WaterBox* waterBox;
    if (WaterBox_GetSurface1(play, &play->colCtx, x, z, &ySurface, &waterBox)) {
        return ySurface;
    }
    return -11000.0f;
}
