/**
 * o2r_loader.h - Generalist .o2r player-model loader
 *
 * Forces a custom skeleton loaded from any .o2r archive to replace Link's
 * during Player_Draw. Mirrors pak_loader's force-model API but consumes
 * resources from the global ResourceManager (any .o2r already on the search
 * path — `nei/`, `mods/`, etc.) instead of parsing .pak files.
 *
 * The .o2r is expected to contain a FlexSkeletonHeader at the registered
 * OTR path (e.g. produced by tools/glb_to_o2r.py). The skeleton must use
 * the same 21-limb hierarchy as OOT Link so vanilla animations work.
 *
 * Usage:
 *   1. O2rLoader_Init() at startup
 *   2. O2rLoader_Register("garo", "__OTR__objects/garo/gGaroSkel")
 *   3. O2rLoader_ForceModel("garo") to activate, NULL to clear
 *   4. Engine's Player_Draw hook calls SwapSkeleton/RestoreSkeleton automatically
 */

#ifndef O2R_LOADER_H
#define O2R_LOADER_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

void O2rLoader_Init(void);

// Register an o2r-based model. Idempotent. Skeleton is lazy-loaded on first force.
void O2rLoader_Register(const char* name, const char* skelOtrPath);

// Activate the named model. Pass NULL or "" to clear.
void O2rLoader_ForceModel(const char* name);
void O2rLoader_ClearForcedModel(void);

// True when a model is forced AND its skeleton successfully resolved.
u8 O2rLoader_HasActiveModel(void);

// Name of the active forced model (NULL if none).
const char* O2rLoader_GetForcedName(void);

// Player_Draw hooks (mirror pak_loader pattern).
void O2rLoader_SwapSkeleton(Player* player);
void O2rLoader_RestoreSkeleton(Player* player);

#ifdef __cplusplus
}
#endif

#endif // O2R_LOADER_H
