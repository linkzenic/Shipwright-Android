/**
 * @file mm_anim_loader.c
 * @brief MM Animation loader implementation
 *
 * Based on working anim_translator_inline_test.h
 *
 * Key implementation details:
 * 1. Load raw animation data from mm.o2r (or user mod o2r via ResourceMgr)
 * 2. Apply baseTransl fix (X=-57, Z=0) to each frame's root position
 * 3. Create LinkAnimationHeader pointing to the corrected data
 * 4. Cache loaded animations for performance
 */

#include "mm_anim_loader.h"
#include "mods/transformation_masks/assets/mm_asset_loader.h"
#include <stdlib.h>
#include <string.h>
#include <libultraship/log/luslog.h>

#define MMANIM_LOG(fmt, ...) lusprintf(__FILE__, __LINE__, LUSLOG_LEVEL_INFO, fmt, ##__VA_ARGS__)

// ============================================================================
// Cache Implementation
// ============================================================================

typedef struct {
    MmAnimId id;               // Animation ID (-1 if entry is free)
    const char* path;          // Path (for path-based lookups)
    LinkAnimationHeader* anim; // Loaded animation
    s16* rawData;              // Raw data copy with baseTransl fix
    u32 sizeBytes;             // Size in bytes for stats
} MmAnimCacheEntry;

static MmAnimCacheEntry sCache[MM_ANIM_CACHE_SIZE];
static s32 sCacheCount = 0;
static u32 sCacheTotalBytes = 0;
static s32 sInitialized = 0;

/**
 * Initialize cache
 */
static void MmAnimCache_Init(void) {
    if (sInitialized) {
        return;
    }

    for (s32 i = 0; i < MM_ANIM_CACHE_SIZE; i++) {
        sCache[i].id = -1;
        sCache[i].path = NULL;
        sCache[i].anim = NULL;
        sCache[i].rawData = NULL;
        sCache[i].sizeBytes = 0;
    }

    sCacheCount = 0;
    sCacheTotalBytes = 0;
    sInitialized = 1;
}

/**
 * Find cache entry by animation ID
 */
static MmAnimCacheEntry* MmAnimCache_FindById(MmAnimId animId) {
    for (s32 i = 0; i < sCacheCount; i++) {
        if (sCache[i].id == animId) {
            return &sCache[i];
        }
    }
    return NULL;
}

/**
 * Find cache entry by path
 */
static MmAnimCacheEntry* MmAnimCache_FindByPath(const char* path) {
    for (s32 i = 0; i < sCacheCount; i++) {
        if (sCache[i].path != NULL && strcmp(sCache[i].path, path) == 0) {
            return &sCache[i];
        }
    }
    return NULL;
}

/**
 * Find or create a free cache entry
 */
static MmAnimCacheEntry* MmAnimCache_GetFreeEntry(void) {
    // Find existing free entry
    for (s32 i = 0; i < MM_ANIM_CACHE_SIZE; i++) {
        if (sCache[i].anim == NULL) {
            if (i >= sCacheCount) {
                sCacheCount = i + 1;
            }
            return &sCache[i];
        }
    }

    // Cache is full - evict oldest entry (simple FIFO for now)
    if (sCacheCount > 0) {
        MmAnimCacheEntry* entry = &sCache[0];

        // Free old entry
        if (entry->rawData != NULL) {
            free(entry->rawData);
        }
        if (entry->anim != NULL) {
            free(entry->anim);
        }
        if (entry->path != NULL) {
            free((void*)entry->path);
        }
        sCacheTotalBytes -= entry->sizeBytes;

        // Shift entries
        for (s32 i = 0; i < sCacheCount - 1; i++) {
            sCache[i] = sCache[i + 1];
        }
        sCacheCount--;

        // Return last slot
        return &sCache[sCacheCount++];
    }

    return NULL;
}

// ============================================================================
// Core Loading Function
// ============================================================================

/**
 * Load and process MM animation data
 *
 * @param path       OTR path to raw animation data
 * @param frameCount Number of frames
 * @param limbCount  Number of limbs (for validation)
 * @return Allocated LinkAnimationHeader or NULL
 */
static LinkAnimationHeader* MmAnim_LoadInternal(const char* path, s16 hintFrameCount, u8 limbCount) {
    MMANIM_LOG("[MmAnim] LoadInternal: path=%s, hintFrames=%d, limbs=%d", path, hintFrameCount, limbCount);

    // Validate
    if (path == NULL) {
        MMANIM_LOG("[MmAnim] LoadInternal FAIL: Invalid params");
        return NULL;
    }

    // Load from mm.o2r (or mod o2r via ResourceMgr priority)
    size_t resourceSize = 0;
    void* resource = MmAssets_LoadResourceWithSize(path, &resourceSize);
    if (resource == NULL) {
        MMANIM_LOG("[MmAnim] LoadInternal FAIL: Resource not found");
        return NULL;
    }

    // Calculate ACTUAL frame count from file size (don't trust hardcoded values)
    // MM format: (limbCount * 3 + 1) s16 values per frame
    // Human=67, Goron=52, Zora=70, Deku=37
    s32 s16PerFrame = (limbCount * 3) + 1;
    s32 bytesPerFrame = s16PerFrame * (s32)sizeof(s16);
    s32 actualFrameCount = (s32)(resourceSize / bytesPerFrame);

    MMANIM_LOG("[MmAnim] Resource: %zu bytes = %d frames (hint was %d)", resourceSize, actualFrameCount,
               hintFrameCount);

    if (actualFrameCount <= 0) {
        MMANIM_LOG("[MmAnim] LoadInternal FAIL: No frames in resource");
        return NULL;
    }

    // Use actual size for allocation
    s32 dataSize = actualFrameCount * bytesPerFrame;

    // Create copy with baseTransl fix
    s16* rawCopy = (s16*)malloc(dataSize);
    if (rawCopy == NULL) {
        MMANIM_LOG("[MmAnim] LoadInternal FAIL: malloc failed");
        return NULL;
    }

    memcpy(rawCopy, resource, dataSize);

    // Apply baseTransl fix to each frame's root position (indices 0 and 2)
    // This is REQUIRED - without it, Link's root position is wrong and animation looks broken
    // Index 0 = root X, Index 1 = root Y (keep for jumping), Index 2 = root Z
    for (s32 frame = 0; frame < actualFrameCount; frame++) {
        s16* frameStart = rawCopy + (frame * s16PerFrame);
        frameStart[0] = MM_ANIM_BASE_TRANSL_X; // Force X to -57
        // frameStart[1] = keep Y from animation (for jumping height)
        frameStart[2] = MM_ANIM_BASE_TRANSL_Z; // Force Z to 0
    }

    // Create LinkAnimationHeader
    LinkAnimationHeader* anim = (LinkAnimationHeader*)malloc(sizeof(LinkAnimationHeader));
    if (anim == NULL) {
        free(rawCopy);
        MMANIM_LOG("[MmAnim] LoadInternal FAIL: malloc anim failed");
        return NULL;
    }

    anim->common.frameCount = (s16)actualFrameCount;
    anim->segment = rawCopy;

    MMANIM_LOG("[MmAnim] LoadInternal SUCCESS: anim=%p, frames=%d", anim, actualFrameCount);
    return anim;
}

// ============================================================================
// Public API
// ============================================================================

s32 MmAnim_IsAvailable(void) {
    return MmAssets_IsAvailable();
}

LinkAnimationHeader* MmAnim_Load(MmAnimId animId) {
    MMANIM_LOG("[MmAnim] Load called with animId=%d", animId);

    // Initialize cache on first use
    if (!sInitialized) {
        MmAnimCache_Init();
    }

    // Validate ID
    if (animId < 0 || animId >= MM_ANIM_MAX) {
        MMANIM_LOG("[MmAnim] FAIL: Invalid animId (max=%d)", MM_ANIM_MAX);
        return NULL;
    }

    // Check if mm.o2r is available
    if (!MmAnim_IsAvailable()) {
        MMANIM_LOG("[MmAnim] FAIL: mm.o2r not available");
        return NULL;
    }

    // Check cache
    MmAnimCacheEntry* cached = MmAnimCache_FindById(animId);
    if (cached != NULL && cached->anim != NULL) {
        MMANIM_LOG("[MmAnim] HIT cache for animId=%d, anim=%p", animId, cached->anim);
        return cached->anim;
    }

    // Get animation definition
    const MmAnimDef* def = &gMmAnims[animId];
    MMANIM_LOG("[MmAnim] def->path=%s, frameCount=%d, limbCount=%d", def->path ? def->path : "NULL", def->frameCount,
               def->limbCount);

    if (def->path == NULL || def->frameCount <= 0) {
        MMANIM_LOG("[MmAnim] FAIL: Invalid def (path=%p, frames=%d)", def->path, def->frameCount);
        return NULL;
    }

    // Load animation
    LinkAnimationHeader* anim = MmAnim_LoadInternal(def->path, def->frameCount, def->limbCount);
    if (anim == NULL) {
        MMANIM_LOG("[MmAnim] FAIL: MmAnim_LoadInternal returned NULL");
        return NULL;
    }

    MMANIM_LOG("[MmAnim] SUCCESS: Loaded anim=%p, frameCount=%d", anim, anim->common.frameCount);

    // Add to cache
    MmAnimCacheEntry* entry = MmAnimCache_GetFreeEntry();
    if (entry != NULL) {
        entry->id = animId;
        entry->path = strdup(def->path);
        entry->anim = anim;
        entry->rawData = (s16*)anim->segment;
        entry->sizeBytes =
            sizeof(LinkAnimationHeader) + (anim->common.frameCount * ((def->limbCount * 3 + 1)) * sizeof(s16));
        sCacheTotalBytes += entry->sizeBytes;
    }

    return anim;
}

LinkAnimationHeader* MmAnim_LoadByPath(const char* path, s16 frameCount, u8 limbCount) {
    // Initialize cache on first use
    if (!sInitialized) {
        MmAnimCache_Init();
    }

    // Validate
    if (path == NULL || frameCount <= 0) {
        return NULL;
    }

    // Check if mm.o2r is available
    if (!MmAnim_IsAvailable()) {
        return NULL;
    }

    // Check cache
    MmAnimCacheEntry* cached = MmAnimCache_FindByPath(path);
    if (cached != NULL && cached->anim != NULL) {
        return cached->anim;
    }

    // Load animation
    LinkAnimationHeader* anim = MmAnim_LoadInternal(path, frameCount, limbCount);
    if (anim == NULL) {
        return NULL;
    }

    // Add to cache
    MmAnimCacheEntry* entry = MmAnimCache_GetFreeEntry();
    if (entry != NULL) {
        entry->id = -1; // No ID for path-based loads
        entry->path = strdup(path);
        entry->anim = anim;
        entry->rawData = (s16*)anim->segment;
        entry->sizeBytes =
            sizeof(LinkAnimationHeader) + (anim->common.frameCount * ((limbCount * 3 + 1)) * sizeof(s16));
        sCacheTotalBytes += entry->sizeBytes;
    }

    return anim;
}

void MmAnim_FlushCache(void) {
    for (s32 i = 0; i < sCacheCount; i++) {
        if (sCache[i].rawData != NULL) {
            free(sCache[i].rawData);
        }
        if (sCache[i].anim != NULL) {
            free(sCache[i].anim);
        }
        if (sCache[i].path != NULL) {
            free((void*)sCache[i].path);
        }

        sCache[i].id = -1;
        sCache[i].path = NULL;
        sCache[i].anim = NULL;
        sCache[i].rawData = NULL;
        sCache[i].sizeBytes = 0;
    }

    sCacheCount = 0;
    sCacheTotalBytes = 0;
}

void MmAnim_GetCacheStats(s32* outEntryCount, s32* outTotalBytes) {
    if (outEntryCount != NULL) {
        *outEntryCount = sCacheCount;
    }
    if (outTotalBytes != NULL) {
        *outTotalBytes = (s32)sCacheTotalBytes;
    }
}

// Include the animation data table
#include "../mm_sources/mm_anims_data.c"
