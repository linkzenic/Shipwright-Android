/**
 * mm_asset_loader.h - MM Asset Detection and Loading
 *
 * C API for detecting and loading assets from mm.o2r
 *
 * MOD OVERRIDE SYSTEM:
 * Place a mod .o2r file alongside mm.o2r to override specific assets:
 *   - mm-mod.o2r     (primary)
 *   - mm-custom.o2r  (alternative)
 *   - mm-override.o2r (alternative)
 *
 * Assets in the mod file take priority over mm.o2r.
 * Use the same OTR paths as mm.o2r to replace specific DLs, icons, or textures.
 */

#ifndef MM_ASSET_LOADER_H
#define MM_ASSET_LOADER_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize MM asset detection
 * Checks for mm.o2r and loads it if found
 */
void MmAssets_Init(void);

/**
 * Check if mm.o2r is available (detected)
 * @return 1 if available, 0 if not
 */
u8 MmAssets_IsAvailable(void);

/**
 * Check if mm.o2r is loaded into archive manager
 * @return 1 if loaded, 0 if not
 */
u8 MmAssets_IsLoaded(void);

/**
 * Check if a mod .o2r override is loaded
 * Mod archives (mm-mod.o2r, mm-custom.o2r) override assets from mm.o2r
 * @return 1 if mod loaded, 0 if not
 */
u8 MmAssets_IsModLoaded(void);

/**
 * Get path to loaded mod .o2r file
 * @return Path string, or empty if no mod loaded
 */
const char* MmAssets_GetModPath(void);

/**
 * Get required 2Ship version string
 * @return Version string (e.g., "2Ship2Harkinian Keiichi Alfa 4.0.0")
 */
const char* MmAssets_GetRequiredVersion(void);

/**
 * Get download URL for 2Ship
 * @return URL string
 */
const char* MmAssets_GetDownloadUrl(void);

/**
 * Get path to mm.o2r file
 * @return Path string, or empty if not found
 */
const char* MmAssets_GetPath(void);

/**
 * Load a resource from mm.o2r
 * @param path Resource path (e.g., "objects/object_link_goron/gLinkGoronSkel")
 * @return Pointer to loaded resource, or NULL if not found
 */
void* MmAssets_LoadResource(const char* path);

/**
 * Load a resource from mm.o2r and get its size
 * @param path Resource path (e.g., "objects/gameplay_keep/gPlayerAnim_...")
 * @param outSize Output: size in bytes of the resource data
 * @return Pointer to loaded resource, or NULL if not found
 */
void* MmAssets_LoadResourceWithSize(const char* path, size_t* outSize);

/**
 * Check if a specific resource exists in mm.o2r
 * @param path Resource path
 * @return 1 if exists, 0 if not
 */
u8 MmAssets_ResourceExists(const char* path);

/**
 * List files matching a pattern from mm.o2r
 * @param searchMask Pattern (e.g., "audio/fonts*")
 * @param resultSize Output: number of matching files
 * @return Array of file paths (caller must free), or NULL
 */
char** MmAssets_ListFiles(const char* searchMask, int* resultSize);

// =============================================================================
// Asset Replacement System (OOT → MM replacements)
// =============================================================================

/**
 * Asset replacement types
 */
typedef enum {
    MM_REPLACE_ICON = 0,  // Item icon (32x32 texture)
    MM_REPLACE_TEXT = 1,  // Item name text texture
    MM_REPLACE_MODEL = 2, // 3D model/display list
} MmReplaceType;

/**
 * Check if a specific replacement is active
 * @param cvarName CVar name (e.g., "gMods.TransformMasks.DekuReplacesSkull")
 * @return 1 if mm.o2r available AND CVar enabled, 0 otherwise
 */
u8 MmAssets_IsReplacementActive(const char* cvarName);

/**
 * Get MM replacement path for an OOT asset (if replacement is active)
 * Strips __OTR__ prefix from input and output for consistency
 * @param ootPath OOT asset path (with or without __OTR__ prefix)
 * @return MM path (with __OTR__ prefix) if replacement active, or NULL
 */
const char* MmAssets_GetReplacement(const char* ootPath);

// =============================================================================
// MM Masks Inventory (3rd Page) Icon/Name Loaders
// =============================================================================

/**
 * Load icon texture for any MM mask item
 * @param itemId Item ID (ITEM_MM_MASK_POSTMAN through ITEM_MM_MASK_FIERCE_DEITY)
 * @return Pointer to 32x32 RGBA icon texture, or NULL if not found
 */
void* MmMasks_LoadIcon(uint16_t itemId);

/**
 * Load name texture for any MM mask item
 * @param itemId Item ID (ITEM_MM_MASK_POSTMAN through ITEM_MM_MASK_FIERCE_DEITY)
 * @return Pointer to name texture, or NULL if not found
 */
void* MmMasks_LoadNameTex(uint16_t itemId);

/**
 * Load FD sword icon for B-button HUD override
 * @return Pointer to 32x32 RGBA icon texture, or NULL if not found
 */
void* MmAssets_LoadFDSwordIcon(void);

// =============================================================================
// Transformation Mask Asset Loaders
// =============================================================================

/**
 * Deku Mask assets (replaces Skull Mask)
 * Get Item: TWO DLs drawn with GetItem_DrawOpa0Xlu1 (Empty=Opa, Mask=Xlu)
 */
void* MmAssets_LoadDekuMaskIcon(void);
void* MmAssets_LoadDekuMaskNameText(void);
void* MmAssets_LoadDekuMaskEmptyDL(void); // First DL - empty (Opa)
void* MmAssets_LoadDekuMaskDL(void);      // Second DL - mask (Xlu)

/**
 * Stone Mask assets (replaces Spooky Mask)
 * Get Item: TWO DLs drawn with GetItem_DrawOpa0Xlu1 (Empty=Opa, Mask=Xlu)
 */
void* MmAssets_LoadStoneMaskIcon(void);
void* MmAssets_LoadStoneMaskNameText(void);
void* MmAssets_LoadStoneMaskEmptyDL(void); // First DL - empty (Opa)
void* MmAssets_LoadStoneMaskDL(void);      // Second DL - mask (Xlu)

/**
 * Fierce Deity Mask assets (replaces Gerudo Mask)
 * Get Item: TWO DLs drawn with GetItem_DrawOpa01 (both Opa)
 */
void* MmAssets_LoadFierceMaskIcon(void);
void* MmAssets_LoadFierceMaskNameText(void);
void* MmAssets_LoadFierceMaskFaceDL(void); // First DL - face (Opa)
void* MmAssets_LoadFierceMaskHairDL(void); // Second DL - hair/hat (Opa)

/**
 * Worn Mask DLs (attached to Link's face when wearing)
 * DIFFERENT from Get Item DLs! These come from gameplay_keep or object_mask_*.
 * From 2Ship z_player_lib.c D_801C0B20[] array.
 */
void* MmAssets_LoadDekuMaskWornDL(void);   // gDekuMaskDL (gameplay_keep)
void* MmAssets_LoadStoneMaskWornDL(void);  // object_mask_stone_DL_000820
void* MmAssets_LoadFierceMaskWornDL(void); // gFierceDeityMaskDL (gameplay_keep)

// =============================================================================
// MM SFX Loader (Audio from mm.o2r)
// =============================================================================

/**
 * Check if MM audio is available
 * @return 1 if available, 0 if not
 */
s32 MmSfx_IsAvailable(void);

/**
 * Initialize MM SFX system
 */
void MmSfx_Init(void);

/**
 * Shutdown MM SFX system
 */
void MmSfx_Shutdown(void);

/**
 * Load a SoundFont from mm.o2r
 * @param fontId Font index (0-6)
 * @return Pointer to SoundFont, or NULL
 */
SoundFont* MmSfx_LoadFont(s32 fontId);

/**
 * Get SoundFont for a specific SFX ID
 * @param sfxId MM SFX ID
 * @return Pointer to SoundFont, or NULL
 */
SoundFont* MmSfx_GetFontForSfx(u16 sfxId);

/**
 * Play MM sound effect at position
 * @param sfxId MM SFX ID
 * @param pos World position (NULL for 2D)
 * @return 1 if played successfully, 0 if no valid sample (caller should use OOT fallback)
 */
s32 MmSfx_PlayAtPos(u16 sfxId, Vec3f* pos);

/**
 * Play MM sound effect with full control
 * @return 1 if played successfully, 0 if no valid sample
 */
s32 MmSfx_PlayEx(u16 sfxId, Vec3f* pos, u8 token, f32* freqScale, f32* vol, s8* reverbAdd);

/**
 * Stop a MM sound effect
 */
void MmSfx_Stop(u16 sfxId);

/**
 * Play Goron roll sound with speed-based pitch
 */
void MmSfx_PlayGoronRoll(Vec3f* pos, f32 speed);

/**
 * Play Goron charge sound with charge level pitch
 */
void MmSfx_PlayGoronCharge(Vec3f* pos, f32 chargeLevel);

/**
 * Play transformation mask flash sound
 */
void MmSfx_PlayTransformFlash(void);

/**
 * Get cache statistics
 */
void MmSfx_GetCacheStats(s32* outLoadedFonts, s32* outTotalBytes);

/**
 * Flush SFX cache
 */
void MmSfx_FlushCache(void);

// =============================================================================
// MM Direct Audio (bypass OOT SFX pipeline - decode ADPCM, mix into output)
// =============================================================================

/**
 * Mix MM direct audio sounds into the audio output buffer
 * Called from AudioMgr_CreateNextAudioBuffer after OOT synthesis
 * @param outBuf Interleaved stereo s16 buffer [L,R,L,R,...]
 * @param numSamples Number of stereo sample pairs
 */
void MmDirectAudio_MixInto(s16* outBuf, u32 numSamples);

/**
 * Stop all playing MM direct audio sounds
 */
void MmDirectAudio_StopAll(void);

#ifdef __cplusplus
}
#endif

#endif // MM_ASSET_LOADER_H
