/**
 * pak_loader.h - ModLoader64 .pak Player Model Loader
 *
 * Loads zzplayas .pak files containing custom player models (N64 .zobj format).
 * Models use the same 21-limb skeleton as OOT Link, so OOT animations work unmodified.
 *
 * Usage:
 *   1. Place .pak files in <soh_dir>/mods/ folder
 *   2. Call PakLoader_Init() on game startup
 *   3. Select model from Settings menu
 *   4. PakLoader_DrawPlayer() is called from Player_Draw() hook
 */

#ifndef PAK_LOADER_H
#define PAK_LOADER_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Public API
// ============================================================================

/**
 * Initialize the PAK loader system.
 * Scans mods/ directory for .pak files, parses manifests.
 * Call once during game initialization.
 */
void PakLoader_Init(void);

/**
 * Check if a custom model is active and ready to draw.
 * @return 1 if custom model should replace Link, 0 otherwise
 */
u8 PakLoader_HasActiveModel(void);

/**
 * Swap player's skeleton with custom .pak model before vanilla draw.
 * Replaces skelAnime.skeleton (limb table) and dListCount.
 * Called BEFORE vanilla Player_DrawGameplay runs.
 */
void PakLoader_SwapSkeleton(Player* player);

/**
 * Restore original skeleton after vanilla draw completes.
 * Called AFTER vanilla Player_DrawGameplay finishes.
 */
void PakLoader_RestoreSkeleton(Player* player);

/**
 * Check if a gSPDisplayList OTR path should be replaced with a pak custom DL.
 * Called from gSPDisplayList in GbiWrap.cpp.
 * @param otrPath The OTR path string being drawn
 * @return Custom Gfx* to use instead, or NULL to use vanilla
 */
Gfx* PakLoader_GetDLOverride(const char* otrPath);

/**
 * Get the custom equipment DL for the given limb based on current hand/sheath type.
 * Uses the Z64O alias table from the active .pak model.
 * @param player The player actor (for hand type info)
 * @param limbIndex The limb being drawn
 * @return Custom Gfx*, PAK_DL_STUB to hide, or NULL to use default
 */
#define PAK_DL_STUB ((Gfx*)(uintptr_t)1)
Gfx* PakLoader_GetEquipDL(Player* player, s32 limbIndex);

/**
 * Check if the pak model used a combined DL for the given hand (includes weapon geometry).
 * PostLimbDraw should skip drawing sword/shield separately when this returns true.
 * @param isLeftHand 1 for left hand, 0 for right hand
 * @return 1 if combined DL was used this frame
 */
u8 PakLoader_UsedCombinedDL(u8 isLeftHand);

/**
 * Get eye/mouth texture from the active pak model's zobj.
 * Returns pointer to CI8 texture data, or NULL if no custom texture.
 */
void* PakLoader_GetEyeTexture(s32 eyeIndex);
void* PakLoader_GetMouthTexture(s32 mouthIndex);

/**
 * Get the number of detected .pak models.
 * @return Number of available models
 */
s32 PakLoader_GetModelCount(void);

/**
 * Get the display name of a model by index.
 * @param index Model index (0 to count-1)
 * @return Display name string, or NULL if invalid index
 */
const char* PakLoader_GetModelName(s32 index);

/**
 * Check if a model has an adult or child zobj.
 * @param index Model index
 * @return 1 if the model has that age's zobj ready
 */
u8 PakLoader_ModelHasAdult(s32 index);
u8 PakLoader_ModelHasChild(s32 index);

/**
 * Select models per age. -1 to deselect (use default Link).
 * Allows different models for adult and child Link.
 */
void PakLoader_SelectAdultModel(s32 index);
void PakLoader_SelectChildModel(s32 index);

/**
 * Get currently selected model indices per age.
 * @return Selected index, or -1 if none
 */
s32 PakLoader_GetSelectedAdultIndex(void);
s32 PakLoader_GetSelectedChildIndex(void);

/**
 * Legacy: Select a model by index for both ages. -1 to deselect.
 */
void PakLoader_SelectModel(s32 index);

/**
 * Legacy: Get currently selected model index (adult).
 */
s32 PakLoader_GetSelectedIndex(void);

/**
 * Select equipment pak by index. -1 to deselect.
 * Equipment pak DLs override body pak equipment DLs.
 */
void PakLoader_SelectEquipment(s32 index);
s32 PakLoader_GetSelectedEquipIndex(void);

/**
 * Check if a model is an equipment-only pak (zzequipment).
 */
u8 PakLoader_ModelIsEquipmentOnly(s32 index);

/**
 * Force a specific .pak body model by file path (lazy-loaded).
 * Used by custom items (e.g., Kafei Mask, Champion's Tunic).
 * Has priority over user-selected models from the menu.
 * @param pakPath Path to the .pak file (relative to exe dir)
 */
void PakLoader_ForceModel(const char* pakPath);

/**
 * Clear the forced body model, returning to user-selected or vanilla Link.
 */
void PakLoader_ClearForcedModel(void);

/**
 * Check if a forced body model is currently active.
 * @return 1 if a forced model override is active
 */
u8 PakLoader_HasForcedModel(void);

/**
 * Force a specific equipment .pak by file path (lazy-loaded).
 * Used by custom items (e.g., Four Sword).
 * Has priority over user-selected equipment from the menu.
 * @param pakPath Path to the equipment .pak file (relative to exe dir)
 */
void PakLoader_ForceEquipment(const char* pakPath);

/**
 * Clear the forced equipment, returning to user-selected or vanilla.
 */
void PakLoader_ClearForcedEquipment(void);

/**
 * Called once per frame at the start of Player_Draw.
 * Frees GbiWrap combined DLs from the previous frame.
 */
void PakLoader_FrameBegin(void);

/**
 * Cleanup and free all loaded model data.
 */
void PakLoader_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // PAK_LOADER_H
