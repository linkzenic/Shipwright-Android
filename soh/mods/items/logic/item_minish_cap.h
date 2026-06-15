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

// 10 bean spots in OOT (all overworld, 9 confirmed + Zora's River always-unlocked)
#define POD_SOIL_COUNT 10

extern const PodSoilWarpPoint sPodSoilTable[POD_SOIL_COUNT];

// Check if a pod soil's skulltula has been killed
s32 MinishCap_IsPodSoilUnlocked(s32 idx);

// Count total unlocked pod soils
s32 MinishCap_GetUnlockedCount(void);

// ── Tiny mode (Minish-size toggle, used away from pod soils) ─────────────────
// True while the toggle is on (including the shrink/grow transition frames)
s32 MinishTiny_IsActive(void);
// Movement speed multiplier for the player (0.2f while tiny, 1.0f otherwise)
f32 MinishTiny_GetSpeedFactor(void);
// Per-frame upkeep: scene-load auto-reset, scale guard, transition animation.
// Called unconditionally from CustomItems_Update (runs even when cap unequipped).
void MinishTiny_Update(Player* p, PlayState* play);
// True when the player should pass through a crawlspace hole wall poly this
// frame (caller skips the wall bgcheck flag). Called from Player_ProcessSceneCollision.
s32 MinishTiny_CrawlspacePassthrough(Player* p, PlayState* play);
// Arm the crawlspace pass-through. Called from z_player.c whenever tiny Link's
// interact-wall flags (sTouchedWallFlags) report a crawlspace — the same signal
// vanilla uses for the "Enter on A" prompt.
void MinishTiny_ArmCrawlspace(void);
// Pulls the rendered main-camera eye/at toward tiny Link proportionally to his
// scale. Called from Camera_Update just before the view is applied.
void MinishTiny_AdjustCameraView(Camera* camera, Vec3f* eye, Vec3f* at);

#ifdef __cplusplus
}
#endif

#endif // ITEM_MINISH_CAP_H
