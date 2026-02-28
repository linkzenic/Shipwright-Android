/**
 * item_minish_cap.h - The Minish Cap (Fast Travel via Pod Soils)
 *
 * Pod Soil warp point table and utility functions.
 * Each pod soil (ObjMakekinsuta) whose Gold Skulltula has been killed
 * becomes an unlocked fast travel destination.
 */

#ifndef ITEM_MINISH_CAP_H
#define ITEM_MINISH_CAP_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pod Soil warp point definition
typedef struct {
    s16 sceneId;       // SCENE_* enum
    s16 entranceIndex; // ENTR_* for scene transition
    Vec3f pos;         // Position near the pod soil
    s16 rotY;          // Player facing direction after warp
    s16 gsGroup;       // GS flag group index (bits 8-12 of ObjMakekinsuta params)
    u8 gsMask;         // GS flag bitmask (bits 0-7 of ObjMakekinsuta params)
    u8 roomIndex;      // Room number within the scene (from scene actor list)
    u8 areaIdx;        // World map area index (0-21) for area box position/texture
    const char* name;  // Display name
} PodSoilWarpPoint;

// 9 confirmed bean spots in OOT (all overworld)
#define POD_SOIL_COUNT 9

extern const PodSoilWarpPoint sPodSoilTable[POD_SOIL_COUNT];

// Check if a pod soil's skulltula has been killed
s32 MinishCap_IsPodSoilUnlocked(s32 idx);

// Count total unlocked pod soils
s32 MinishCap_GetUnlockedCount(void);

#ifdef __cplusplus
}
#endif

#endif // ITEM_MINISH_CAP_H
