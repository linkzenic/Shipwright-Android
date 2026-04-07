/**
 * pak_loader.cpp - ModLoader64 .pak Player Model Loader
 *
 * Parses ModLoader64 .pak archives, extracts zzplayas .zobj N64 binaries,
 * byte-swaps from big-endian to native, patches segment addresses,
 * and builds native skeleton structures for SkelAnime_DrawFlexOpa.
 */

#include "pak_loader.h"
#include "mods/transformation_masks/transformation_masks.h"

#include <libultraship/libultra.h>
#include "global.h"
#include "z64.h"
#include "soh/OTRGlobals.h"

#include <vector>
#include <string>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <map>
#include <zlib.h>

extern "C" {
#include "objects/gameplay_keep/gameplay_keep.h"
}

// ============================================================================
// Logging
// ============================================================================

#include <spdlog/spdlog.h>
// Use spdlog for file logging but keep printf-style format
#define PAK_LOG(fmt, ...)                                                      \
    do {                                                                       \
        char _pakbuf[512];                                                     \
        snprintf(_pakbuf, sizeof(_pakbuf), "[PakLoader] " fmt, ##__VA_ARGS__); \
        SPDLOG_INFO("{}", _pakbuf);                                            \
    } while (0)

// ============================================================================
// Big-Endian Read Helpers
// ============================================================================

static inline u32 BE_U32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static inline s16 BE_S16(const u8* p) {
    return (s16)(((u16)p[0] << 8) | p[1]);
}

static inline u16 BE_U16(const u8* p) {
    return ((u16)p[0] << 8) | p[1];
}

// Write native u32 to memory
static inline void WRITE_NATIVE_U32(u8* p, u32 val) {
    memcpy(p, &val, 4);
}

// Write native u16 to memory
static inline void WRITE_NATIVE_U16(u8* p, u16 val) {
    memcpy(p, &val, 2);
}

// Swap a 16-bit value in-place (BE to native LE)
static inline void SWAP16_INPLACE(u8* p) {
    u16 val = BE_U16(p);
    WRITE_NATIVE_U16(p, val);
}

// Swap a 32-bit value in-place (BE to native LE)
static inline void SWAP32_INPLACE(u8* p) {
    u32 val = BE_U32(p);
    WRITE_NATIVE_U32(p, val);
}

// ============================================================================
// F3DEX2 Opcodes (values for F3DEX2 microcode)
// ============================================================================

#define F3DEX2_G_VTX 0x01
#define F3DEX2_G_MODIFYVTX 0x02
#define F3DEX2_G_DL 0xDE
#define F3DEX2_G_ENDDL 0xDF
#define F3DEX2_G_MTX 0xDA
#define F3DEX2_G_MOVEMEM 0xDC
#define F3DEX2_G_SETTIMG 0xFD
#define F3DEX2_G_SETTILE 0xF5
#define F3DEX2_G_MOVEWORD 0xDB
#define F3DEX2_G_LOADBLOCK 0xF3
#define F3DEX2_G_LOADTLUT 0xF0

// ============================================================================
// Texture Format Constants (from G_SETTILE)
// ============================================================================

#define G_IM_FMT_RGBA 0
#define G_IM_FMT_YUV 1
#define G_IM_FMT_CI 2
#define G_IM_FMT_IA 3
#define G_IM_FMT_I 4

#define G_IM_SIZ_4b 0
#define G_IM_SIZ_8b 1
#define G_IM_SIZ_16b 2
#define G_IM_SIZ_32b 3

// ============================================================================
// PAK Model Structure
// ============================================================================

#define PAK_MAX_LIMBS 22

struct PakModel {
    char displayName[128];
    std::string pakPath;

    // Loaded .zobj data (byte-swapped to native endian)
    u8* adultZobj;
    u32 adultZobjSize;
    u8* childZobj;
    u32 childZobjSize;

    // Native skeleton structures (LodLimb because Player_DrawImpl uses SkelAnime_DrawFlexLod)
    LodLimb adultLimbs[PAK_MAX_LIMBS];
    void* adultLimbTable[PAK_MAX_LIMBS];
    FlexSkeletonHeader adultFlexHeader;

    LodLimb childLimbs[PAK_MAX_LIMBS];
    void* childLimbTable[PAK_MAX_LIMBS];
    FlexSkeletonHeader childFlexHeader;

    // Translated native DLs (owned, must be freed)
    std::map<u32, Gfx*> adultTranslatedDLs;
    std::map<u32, Gfx*> childTranslatedDLs;

    // Equipment DLs from alias table (keyed by Z64O alias offset)
    std::map<u32, Gfx*> adultEquipDLs;
    std::map<u32, Gfx*> childEquipDLs;

    u8 hasAdult;
    u8 hasChild;
    u8 adultReady;
    u8 childReady;
    u8 isEquipmentOnly; // 1 = zzequipment pak (no body, only equipment items)
};

// ============================================================================
// Module State
// ============================================================================

static std::vector<PakModel> sModels;
static s32 sSelectedAdultIndex = -1;
static s32 sSelectedChildIndex = -1;
static s32 sSelectedEquipIndex = -1;
static u8 sInitialized = 0;

// Forced model (from custom items like Kafei Mask)
static s32 sForcedModelIndex = -1;
static std::string sForcedModelPath;

// Helper: get active model index for current Link age
// Forced model takes priority over user selection
static inline s32 sGetActiveIndex(void) {
    if (sForcedModelIndex >= 0 && sForcedModelIndex < (s32)sModels.size()) {
        return sForcedModelIndex;
    }
    return (LINK_AGE_IN_YEARS == YEARS_ADULT) ? sSelectedAdultIndex : sSelectedChildIndex;
}

// ============================================================================
// PAK File Entry
// ============================================================================

struct PakEntry {
    std::string name;
    u32 dataStart;
    u32 dataEnd;
    bool compressed; // true = DEFL (zlib), false = UNCO (raw)
};

// ============================================================================
// PAK Parser
// ============================================================================

static bool PakParser_Parse(const std::string& pakPath, std::vector<PakEntry>& entries) {
    FILE* f = fopen(pakPath.c_str(), "rb");
    if (!f)
        return false;

    // Get file size
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileSize < 16) {
        fclose(f);
        return false;
    }

    // Read entire file
    std::vector<u8> data(fileSize);
    if (fread(data.data(), 1, fileSize, f) != (size_t)fileSize) {
        fclose(f);
        return false;
    }
    fclose(f);

    // Check magic: "ModLoader64\0"
    if (memcmp(data.data(), "ModLoader64", 11) != 0) {
        return false;
    }

    // Parse UNCO entries
    // Format: each 16 bytes starting at offset after header
    // { 'UNCO'(4), name_offset(4 BE), data_start(4 BE), data_end(4 BE) }
    // Name table: filenames separated by 0xFF

    // First, find the name table by looking at the first UNCO entry's name_offset
    // UNCO entries start at various offsets after the magic

    // Scan for UNCO/DEFL markers
    std::vector<std::tuple<u32, u32, u32, bool>> rawEntries; // name_off, data_start, data_end, compressed

    // Skip past the magic "ModLoader64\0" and any header bytes to find first UNCO
    u32 startPos = 12;
    // Scan forward to find the first UNCO or DEFL marker (header size varies between .pak files)
    while (startPos + 16 <= (u32)fileSize && memcmp(data.data() + startPos, "UNCO", 4) != 0 &&
           memcmp(data.data() + startPos, "DEFL", 4) != 0) {
        startPos++;
    }

    for (u32 pos = startPos; pos + 16 <= (u32)fileSize; pos += 16) {
        bool isUnco = memcmp(data.data() + pos, "UNCO", 4) == 0;
        bool isDefl = memcmp(data.data() + pos, "DEFL", 4) == 0;
        if (isUnco || isDefl) {
            u32 nameOff = BE_U32(data.data() + pos + 4);
            u32 dataStart = BE_U32(data.data() + pos + 8);
            u32 dataEnd = BE_U32(data.data() + pos + 12);

            if (nameOff < (u32)fileSize && dataStart < (u32)fileSize && dataEnd <= (u32)fileSize) {
                rawEntries.push_back({ nameOff, dataStart, dataEnd, isDefl });
            }
        } else {
            // End of entries
            break;
        }
    }

    if (rawEntries.empty()) {
        return false;
    }

    // Extract filenames from the name table
    // Each UNCO's name_offset points into a name table where names are separated by 0xFF
    for (auto& [nameOff, dataStart, dataEnd, isCompressed] : rawEntries) {
        // Read name until 0xFF or 0x00
        std::string name;
        for (u32 i = nameOff; i < (u32)fileSize; i++) {
            u8 c = data[i];
            if (c == 0xFF || c == 0x00)
                break;
            name += (char)c;
        }

        PakEntry entry;
        entry.name = name;
        entry.dataStart = dataStart;
        entry.dataEnd = dataEnd;
        entry.compressed = isCompressed;
        entries.push_back(entry);
    }

    return true;
}

// ============================================================================
// Simple JSON-ish String Parser (for package.json fields)
// ============================================================================

static std::string JsonFindString(const std::string& json, const std::string& key) {
    // Find "key": "value" pattern
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos)
        return "";

    pos = json.find("\"", pos + search.length());
    if (pos == std::string::npos)
        return "";
    pos++; // skip opening quote

    size_t end = json.find("\"", pos);
    if (end == std::string::npos)
        return "";

    return json.substr(pos, end - pos);
}

// Find the "file" value inside a model array entry like: [{"file": "xxx.zobj", "name": "yyy"}]
static std::string JsonFindModelFile(const std::string& json, const std::string& modelKey) {
    // Find the model key (e.g., "adult_model")
    size_t pos = json.find("\"" + modelKey + "\"");
    if (pos == std::string::npos)
        return "";

    // Find the array start
    pos = json.find("[", pos);
    if (pos == std::string::npos)
        return "";

    // Find "file" within this array section
    size_t arrayEnd = json.find("]", pos);
    if (arrayEnd == std::string::npos)
        return "";

    std::string arraySection = json.substr(pos, arrayEnd - pos);

    return JsonFindString(arraySection, "file");
}

// ============================================================================
// ZOBJ Byte-Swap Engine
// ============================================================================

// Context for tracking what's been byte-swapped
struct SwapContext {
    u8* data;
    u32 size;
    std::set<u32> swappedDLs; // DL offsets already processed
    std::set<u32> swappedVtx; // Vtx buffer offsets already processed
    std::set<u32> swappedTex; // Texture offsets already processed

    // Current texture state (from SETTILE/SETTIMG)
    u8 lastTexFmt;
    u8 lastTexSiz;
    u32 lastTexAddr;   // segment offset of last SETTIMG
    u32 lastTexWidth;  // width from SETTILE
    u32 lastTexHeight; // height from SETTILESIZE
};

/**
 * Byte-swap a vertex buffer from BE to native LE.
 * Vtx layout (16 bytes): ob[3](s16) + flag(u16) + tc[2](s16) + cn[4](u8)
 */
static void SwapVertexBuffer(SwapContext& ctx, u32 offset, u32 count) {
    if (ctx.swappedVtx.count(offset))
        return;
    ctx.swappedVtx.insert(offset);

    for (u32 i = 0; i < count; i++) {
        u32 voff = offset + i * 16;
        if (voff + 16 > ctx.size)
            break;

        u8* v = ctx.data + voff;
        // ob[0..2]: 3 x s16 (bytes 0-5)
        SWAP16_INPLACE(v + 0);
        SWAP16_INPLACE(v + 2);
        SWAP16_INPLACE(v + 4);
        // flag: u16 (bytes 6-7)
        SWAP16_INPLACE(v + 6);
        // tc[0..1]: 2 x s16 (bytes 8-11)
        SWAP16_INPLACE(v + 8);
        SWAP16_INPLACE(v + 10);
        // cn[0..3]: 4 x u8 (bytes 12-15) - no swap needed
    }
}

/**
 * Byte-swap a texture buffer for RGBA16 format.
 * Each pixel is a 16-bit value that needs swapping.
 */
static void SwapTextureRGBA16(SwapContext& ctx, u32 offset, u32 numPixels) {
    if (ctx.swappedTex.count(offset))
        return;
    ctx.swappedTex.insert(offset);

    for (u32 i = 0; i < numPixels; i++) {
        u32 toff = offset + i * 2;
        if (toff + 2 > ctx.size)
            break;
        SWAP16_INPLACE(ctx.data + toff);
    }
}

/**
 * Byte-swap a texture buffer for RGBA32 format.
 * Each pixel is a 32-bit value that needs swapping.
 */
static void SwapTextureRGBA32(SwapContext& ctx, u32 offset, u32 numPixels) {
    if (ctx.swappedTex.count(offset))
        return;
    ctx.swappedTex.insert(offset);

    for (u32 i = 0; i < numPixels; i++) {
        u32 toff = offset + i * 4;
        if (toff + 4 > ctx.size)
            break;
        SWAP32_INPLACE(ctx.data + toff);
    }
}

/**
 * Translate a N64 display list (8 bytes/cmd, big-endian) into a native SOH
 * display list (sizeof(Gfx) per cmd, uintptr_t w0/w1).
 * Converts segment 06 addresses to direct pointers into zobjData.
 * Other segment addresses are kept with LSB=1 for runtime SegAddr() resolution.
 * Returns a malloc'd Gfx array. Caller must free.
 */
#define Z64O_MANIFEST_START 0x5000

static Gfx* TranslateDL(SwapContext& ctx, u32 dlOffset, std::map<u32, Gfx*>& translatedDLs) {
    // Already translated?
    auto it = translatedDLs.find(dlOffset);
    if (it != translatedDLs.end())
        return it->second;

    if (dlOffset + 8 > ctx.size) {
        PAK_LOG("TranslateDL: offset 0x%X out of range (size=0x%X)", dlOffset, ctx.size);
        return NULL;
    }

    // Debug: show first 2 words at this offset
    u32 dbgW0 = BE_U32(ctx.data + dlOffset);
    u32 dbgW1 = BE_U32(ctx.data + dlOffset + 4);
    static u32 sTranslateLogCount = 0;
    if (sTranslateLogCount < 30) {
        sTranslateLogCount++;
        PAK_LOG("TranslateDL: offset=0x%X, first cmd: w0=0x%08X w1=0x%08X (opcode=0x%02X)", dlOffset, dbgW0, dbgW1,
                (dbgW0 >> 24) & 0xFF);
    }

    // First pass: count valid commands
    u32 cmdCount = 0;
    u32 pos = dlOffset;
    while (pos + 8 <= ctx.size) {
        u32 w0 = BE_U32(ctx.data + pos);
        u8 opcode = (w0 >> 24) & 0xFF;

        // Validate opcode - stop if we hit non-DL data
        switch (opcode) {
            case 0x00:
            case 0x01:
            case 0x03:
            case 0x05:
            case 0x06:
            case 0x07:
            case 0xD7:
            case 0xD9:
            case 0xDA:
            case 0xDB:
            case 0xDC:
            case 0xDE:
            case 0xDF:
            case 0xE1:
            case 0xE2:
            case 0xE3:
            case 0xE4:
            case 0xE6:
            case 0xE7:
            case 0xE8:
            case 0xE9:
            case 0xF0:
            case 0xF2:
            case 0xF3:
            case 0xF4:
            case 0xF5:
            case 0xFA:
            case 0xFB:
            case 0xFC:
            case 0xFD:
                break;
            default:
                goto countDone; // Unknown opcode = not a DL command
        }

        cmdCount++;

        if (opcode == 0xDF)
            break; // G_ENDDL
        if (opcode == 0xDE && ((w0 >> 16) & 1) == 1)
            break; // G_DL branch

        pos += 8;
    }
countDone:

    if (cmdCount == 0)
        return NULL;

    // Allocate native Gfx array (persistent)
    Gfx* nativeDL = (Gfx*)calloc(cmdCount + 1, sizeof(Gfx)); // +1 for safety ENDDL
    translatedDLs[dlOffset] = nativeDL;

    // Second pass: translate each command
    pos = dlOffset;
    for (u32 i = 0; i < cmdCount; i++) {
        u32 w0 = BE_U32(ctx.data + pos);
        u32 w1 = BE_U32(ctx.data + pos + 4);
        u8 opcode = (w0 >> 24) & 0xFF;
        u8 seg = (w1 >> 24) & 0xFF;
        u32 segOff = w1 & 0x00FFFFFF;

        // Default: copy w0/w1 directly (works for commands without addresses)
        nativeDL[i].words.w0 = (uintptr_t)w0;
        nativeDL[i].words.w1 = (uintptr_t)w1;

        switch (opcode) {
            case F3DEX2_G_VTX: {
                u32 numVtx = (w0 >> 12) & 0xFF;
                if (seg == 0x06 && segOff < ctx.size) {
                    SwapVertexBuffer(ctx, segOff, numVtx);
                    nativeDL[i].words.w1 = (uintptr_t)(ctx.data + segOff);
                } else if (seg != 0x00) {
                    nativeDL[i].words.w1 = (uintptr_t)(w1 | 1); // segmented, LSB=1
                }
                break;
            }

            case F3DEX2_G_DL: {
                u8 pushFlag = (w0 >> 16) & 0x01;

                if (seg == 0x06 && segOff < ctx.size) {
                    Gfx* subDL = TranslateDL(ctx, segOff, translatedDLs);
                    if (subDL) {
                        nativeDL[i].words.w1 = (uintptr_t)subDL;
                    } else {
                        // Failed to translate sub-DL, NOP it
                        nativeDL[i].words.w0 = 0;
                        nativeDL[i].words.w1 = 0;
                    }
                } else if (seg != 0x00 && w1 != 0 && w1 != 0xFFFFFFFF) {
                    nativeDL[i].words.w1 = (uintptr_t)(w1 | 1); // segmented, LSB=1
                } else {
                    // Invalid target - NOP
                    nativeDL[i].words.w0 = 0;
                    nativeDL[i].words.w1 = 0;
                }
                break;
            }

            case F3DEX2_G_SETTIMG: {
                u8 fmt = (w0 >> 21) & 0x07;
                u8 siz = (w0 >> 19) & 0x03;

                // Remember last SETTIMG info for LOADBLOCK/LOADTLUT size calculation
                ctx.lastTexFmt = fmt;
                ctx.lastTexSiz = siz;
                ctx.lastTexAddr = segOff;

                if (seg == 0x06 && segOff < ctx.size) {
                    // Don't swap yet - wait for LOADBLOCK/LOADTLUT to know exact size
                    nativeDL[i].words.w1 = (uintptr_t)(ctx.data + segOff);
                } else if (seg != 0x00) {
                    nativeDL[i].words.w1 = (uintptr_t)(w1 | 1);
                }
                break;
            }

            // LOADTLUT and LOADBLOCK: NO byte-swap needed for texture/palette data.
            // SOH's Fast3D interpreter reads texture data as big-endian manually:
            //   col16 = (addr[2*i] << 8) | addr[2*i+1]
            // So the raw N64 big-endian bytes are correct as-is.
            case F3DEX2_G_LOADTLUT:
            case F3DEX2_G_LOADBLOCK:
                break;

            case F3DEX2_G_MTX: {
                if (seg == 0x06 && segOff < ctx.size) {
                    // Byte-swap the 64-byte matrix data (16 x s32 BE → LE)
                    if (!ctx.swappedVtx.count(segOff)) { // Reuse vtx tracking to prevent double-swap
                        ctx.swappedVtx.insert(segOff);
                        for (u32 mi = 0; mi < 16 && segOff + mi * 4 + 4 <= ctx.size; mi++) {
                            SWAP32_INPLACE(ctx.data + segOff + mi * 4);
                        }
                    }
                    nativeDL[i].words.w1 = (uintptr_t)(ctx.data + segOff);
                } else if (seg != 0x00 && w1 != 0xFFFFFFFF) {
                    nativeDL[i].words.w1 = (uintptr_t)(w1 | 1);
                }
                break;
            }

            case F3DEX2_G_MOVEMEM: {
                if (seg == 0x06 && segOff < ctx.size) {
                    nativeDL[i].words.w1 = (uintptr_t)(ctx.data + segOff);
                } else if (seg != 0x00 && w1 != 0xFFFFFFFF) {
                    nativeDL[i].words.w1 = (uintptr_t)(w1 | 1);
                }
                break;
            }

            case F3DEX2_G_ENDDL:
                break;

            default:
                // All other commands: w0/w1 already copied as-is (no addresses)
                break;
        }

        pos += 8;
    }

    // Ensure last command is ENDDL
    if ((nativeDL[cmdCount - 1].words.w0 >> 24) != F3DEX2_G_ENDDL) {
        nativeDL[cmdCount].words.w0 = (uintptr_t)((u32)F3DEX2_G_ENDDL << 24);
        nativeDL[cmdCount].words.w1 = 0;
    }

    return nativeDL;
}

// ============================================================================
// ZOBJ Skeleton Parser
// ============================================================================

/**
 * Parse a zzplayas .zobj file:
 * 1. Find skeleton at manifest offset 0x500C
 * 2. Byte-swap all referenced DLs/vertices/textures
 * 3. Build native StandardLimb[] and FlexSkeletonHeader
 */
static bool ZobjBuildSkeleton(u8* zobjData, u32 zobjSize, LodLimb* outLimbs, void** outLimbTable,
                              FlexSkeletonHeader* outFlexHeader, std::map<u32, Gfx*>& translatedDLs,
                              SwapContext* outSwapCtx) {
    // zzplayas manifest: skeleton pointer at 0x500C
    if (zobjSize < 0x5010) {
        PAK_LOG("ZOBJ too small for zzplayas manifest");
        return false;
    }

    // Verify MODLOADER64 marker at 0x5000
    // (Some zobj files may have it at 0x4FFC instead)
    bool hasMarker = false;
    if (zobjSize >= 0x500C) {
        // Check around 0x5000 for "MODLOADER64" or "MODL"
        for (u32 checkOff = 0x4FF8; checkOff <= 0x5008 && checkOff + 12 <= zobjSize; checkOff += 4) {
            if (memcmp(zobjData + checkOff, "MODL", 4) == 0) {
                hasMarker = true;
                break;
            }
        }
    }

    if (!hasMarker) {
        PAK_LOG("ZOBJ missing MODLOADER64 marker");
        return false;
    }

    // Read skeleton pointer (segment 06 address, big-endian)
    u32 skelSegAddr = BE_U32(zobjData + 0x500C);
    u8 skelSeg = (skelSegAddr >> 24) & 0xFF;
    u32 skelOffset = skelSegAddr & 0x00FFFFFF;

    if (skelSeg != 0x06 || skelOffset + 12 > zobjSize) {
        // Manifest pointer is invalid (e.g. 0xFFFFFFFF) - search by pattern
        // Look for FlexSkeletonHeader: 06XXXXXX followed by limbCount=21
        PAK_LOG("Manifest skeleton ptr 0x%08X invalid, searching by pattern...", skelSegAddr);
        bool found = false;
        for (u32 i = 0x5010; i + 12 <= zobjSize; i += 4) {
            u32 ptr = BE_U32(zobjData + i);
            if ((ptr >> 24) == 0x06 && (ptr & 0x00FFFFFF) < zobjSize) {
                u8 count = zobjData[i + 4];
                if (count == 21) { // OOT Link limb count
                    u32 ltOff = ptr & 0x00FFFFFF;
                    if (ltOff + 21 * 4 <= zobjSize) {
                        u32 firstLimb = BE_U32(zobjData + ltOff);
                        if ((firstLimb >> 24) == 0x06) {
                            skelOffset = i;
                            found = true;
                            PAK_LOG("Found skeleton by pattern at 0x%X", skelOffset);
                            break;
                        }
                    }
                }
            }
        }
        if (!found) {
            PAK_LOG("Could not find skeleton in zobj");
            return false;
        }
    }

    // Read FlexSkeletonHeader (big-endian)
    // Offset+0: limb table pointer (u32 seg06)
    // Offset+4: limbCount (u8)
    // Offset+8: dListCount (u8) -- Actually at offset+5 for SkeletonHeader, then +8 for Flex
    u32 limbTableSegAddr = BE_U32(zobjData + skelOffset);
    u8 limbCount = zobjData[skelOffset + 4];
    // FlexSkeletonHeader has dListCount after the SkeletonHeader
    // SkeletonHeader = 8 bytes (ptr + count + pad), FlexSkeletonHeader adds dListCount
    u8 dListCount = zobjData[skelOffset + 8];

    u32 limbTableOffset = limbTableSegAddr & 0x00FFFFFF;

    PAK_LOG("Skeleton at 0x%X: limbTable=0x%08X, limbCount=%d, dListCount=%d", skelOffset, limbTableSegAddr, limbCount,
            dListCount);

    if (limbCount == 0 || limbCount > PAK_MAX_LIMBS) {
        PAK_LOG("Invalid limb count: %d", limbCount);
        return false;
    }

    if (limbTableOffset + limbCount * 4 > zobjSize) {
        PAK_LOG("Limb table out of bounds");
        return false;
    }

    // Set up byte-swap context
    SwapContext swapCtx;
    swapCtx.data = zobjData;
    swapCtx.size = zobjSize;
    swapCtx.lastTexFmt = 0;
    swapCtx.lastTexSiz = 0;
    swapCtx.lastTexAddr = 0;
    swapCtx.lastTexWidth = 0;
    swapCtx.lastTexHeight = 0;

    // Parse each limb and build native skeleton
    for (s32 i = 0; i < limbCount; i++) {
        // Read limb pointer from limb table (BE u32, segment 06)
        u32 limbPtrSeg = BE_U32(zobjData + limbTableOffset + i * 4);
        u32 limbOffset = limbPtrSeg & 0x00FFFFFF;

        if (limbOffset + 12 > zobjSize) {
            PAK_LOG("Limb %d out of bounds at 0x%X", i, limbOffset);
            return false;
        }

        // Read StandardLimb fields (big-endian)
        // Layout: x(s16), y(s16), z(s16), child(u8), sibling(u8), dList(u32)
        s16 x = BE_S16(zobjData + limbOffset + 0);
        s16 y = BE_S16(zobjData + limbOffset + 2);
        s16 z = BE_S16(zobjData + limbOffset + 4);
        u8 child = zobjData[limbOffset + 6];
        u8 sibling = zobjData[limbOffset + 7];
        u32 dListSeg = BE_U32(zobjData + limbOffset + 8);

        // Build native LodLimb (Player uses SkelAnime_DrawFlexLod which expects LodLimb)
        outLimbs[i].jointPos.x = x;
        outLimbs[i].jointPos.y = y;
        outLimbs[i].jointPos.z = z;
        outLimbs[i].child = child;
        outLimbs[i].sibling = sibling;

        if (dListSeg != 0) {
            u32 dListOffset = dListSeg & 0x00FFFFFF;
            if (dListOffset < zobjSize) {
                // Translate N64 DL (8 bytes/cmd) to native SOH DL (sizeof(Gfx)/cmd)
                Gfx* translated = TranslateDL(swapCtx, dListOffset, translatedDLs);
                outLimbs[i].dLists[0] = translated; // near LOD
                outLimbs[i].dLists[1] = translated; // far LOD (same, zzplayas has no LOD)
            } else {
                outLimbs[i].dLists[0] = NULL;
                outLimbs[i].dLists[1] = NULL;
            }
        } else {
            outLimbs[i].dLists[0] = NULL;
            outLimbs[i].dLists[1] = NULL;
        }

        // Set up limb table entry (direct pointer - SEGMENTED_TO_VIRTUAL is no-op in SOH)
        outLimbTable[i] = &outLimbs[i];
    }

    // Build FlexSkeletonHeader
    outFlexHeader->sh.segment = outLimbTable;
    outFlexHeader->sh.limbCount = limbCount;
    outFlexHeader->dListCount = dListCount;

    // Pass swap context out so alias table parser shares vertex/texture tracking
    if (outSwapCtx)
        *outSwapCtx = swapCtx;

    PAK_LOG("Skeleton built: %d limbs, %d dLists", limbCount, dListCount);
    return true;
}

/**
 * Unwrap a chain of DL jumps in the alias/LUT area.
 * Old zzplayas entries can point to other entries within 0x5000-0x5800.
 * Follow the chain until we reach a real DL offset (< 0x5000).
 */
static u32 UnwrapDLChain(u8* zobjData, u32 zobjSize, u32 startOff) {
    u32 cur = startOff;
    for (s32 i = 0; i < 10; i++) { // max 10 hops to prevent infinite loop
        if (cur < 0x5000 || cur >= 0x5800)
            return cur; // Real DL offset
        if (cur + 8 > zobjSize)
            return cur;
        u32 ptr = BE_U32(zobjData + cur + 4);
        if ((ptr >> 24) != 0x06)
            return cur;
        cur = ptr & 0x00FFFFFF;
    }
    return cur;
}

/**
 * Read a DL entry from the zobj, unwrap chains, translate, and store.
 * Old format entries are simple 8-byte {0xDE010000, 0x06XXXXXX} at the LUT offset.
 */
static void ParseOneEquipEntry(u8* zobjData, u32 zobjSize, u32 lutOff, u32 z64oAlias, std::map<u32, Gfx*>& equipDLs,
                               std::map<u32, Gfx*>& translatedDLs, SwapContext& ctx, s32& customCount) {
    if (lutOff + 8 > zobjSize)
        return;

    u32 de = BE_U32(zobjData + lutOff);
    u32 ptr = BE_U32(zobjData + lutOff + 4);

    if (de == 0xDF000000) {
        equipDLs[z64oAlias] = PAK_DL_STUB;
        return;
    }

    // Must be a 0xDE01xxxx command (gsSPDisplayList branch)
    if ((de >> 24) != 0xDE)
        return;
    if ((ptr >> 24) != 0x06)
        return;

    u32 dlOff = UnwrapDLChain(zobjData, zobjSize, ptr & 0x00FFFFFF);
    if (dlOff >= zobjSize)
        return;

    Gfx* translated = TranslateDL(ctx, dlOff, translatedDLs);
    if (translated) {
        equipDLs[z64oAlias] = translated;
        customCount++;
    }
}

// Old zzplayas LUT offset → Z64O alias offset mapping (adult)
struct OldToZ64O {
    u32 oldLut;
    u32 z64oAlias;
};
static const OldToZ64O sOldAdultMap[] = {
    // Hands
    { 0x5108, 0x5098 }, // LHAND
    { 0x5110, 0x50A0 }, // LFIST
    { 0x5118, 0x50A8 }, // LHAND_BOTTLE
    { 0x5120, 0x50B0 }, // RHAND
    { 0x5128, 0x50B8 }, // RFIST
    // Sheath/Hilt/Blade
    { 0x5130, 0x50C0 }, // SWORD_SHEATH → DL_SWORD_SHEATH_1
    { 0x5138, 0x50E0 }, // SWORD_HILT → DL_SWORD_HILT_2
    { 0x5140, 0x50F8 }, // SWORD_BLADE → DL_SWORD_BLADE_2
    { 0x5148, 0x50E8 }, // LONGSWORD_HILT → DL_SWORD_HILT_3
    { 0x5150, 0x5100 }, // LONGSWORD_BLADE → DL_SWORD_BLADE_3
    { 0x5158, 0x51E0 }, // LONGSWORD_BROKEN → DL_SWORD_BLADE_3_BROKEN
    // Shields
    { 0x5160, 0x5110 }, // SHIELD_HYLIAN → DL_SHIELD_2
    { 0x5168, 0x5118 }, // SHIELD_MIRROR → DL_SHIELD_3
    // Items
    { 0x5170, 0x51F0 }, // HAMMER
    { 0x5178, 0x5120 }, // BOTTLE
    { 0x5180, 0x5138 }, // BOW
    { 0x5188, 0x5128 }, // OCARINA_TIME → DL_OCARINA_2
    { 0x5190, 0x5148 }, // HOOKSHOT
    // Gauntlets
    { 0x5198, 0x51F8 }, // UPGRADE_LFOREARM
    { 0x51A0, 0x5200 }, // UPGRADE_LHAND
    { 0x51A8, 0x5208 }, // UPGRADE_LFIST
    { 0x51B0, 0x5210 }, // UPGRADE_RFOREARM
    { 0x51B8, 0x5218 }, // UPGRADE_RHAND
    { 0x51C0, 0x5220 }, // UPGRADE_RFIST
    // Boots
    { 0x51C8, 0x5228 }, // BOOT_LIRON
    { 0x51D0, 0x5230 }, // BOOT_RIRON
    { 0x51D8, 0x5238 }, // BOOT_LHOVER
    { 0x51E0, 0x5240 }, // BOOT_RHOVER
    // Hookshot parts
    { 0x5210, 0x5150 }, // HOOKSHOT_CHAIN
    { 0x5218, 0x5158 }, // HOOKSHOT_HOOK
    { 0x5220, 0x5160 }, // HOOKSHOT_AIM
    // Bow string
    { 0x5228, 0x5140 }, // BOW_STRING
    // Combined DLs
    { 0x5238, 0x53D0 }, // SWORD_SHEATHED → DL_SWORD1_SHEATHED
    { 0x5258, 0x53F0 }, // SHIELD_HYLIAN_BACK
    { 0x5268, 0x53F8 }, // SHIELD_MIRROR_BACK
    { 0x5278, 0x5420 }, // SWORD_SHIELD_HYLIAN
    { 0x5288, 0x5428 }, // SWORD_SHIELD_MIRROR
    { 0x5298, 0x55C0 }, // SHEATH0_HYLIAN → SWORD1_SHIELD1_SHEATHED (approx)
    { 0x52A8, 0x55D0 }, // SHEATH0_MIRROR → SWORD1_SHIELD3_SHEATHED (approx)
    { 0x52B8, 0x5448 }, // LFIST_SWORD
    { 0x52D0, 0x5458 }, // LFIST_LONGSWORD → LFIST_SWORD3
    { 0x52E8, 0x54F0 }, // LFIST_LONGSWORD_BROKEN
    { 0x5300, 0x5460 }, // LFIST_HAMMER
    { 0x5310, 0x5470 }, // RFIST_SHIELD_HYLIAN
    { 0x5320, 0x5478 }, // RFIST_SHIELD_MIRROR
    { 0x5330, 0x5480 }, // RFIST_BOW
    { 0x5340, 0x5488 }, // RFIST_HOOKSHOT
    { 0x5350, 0x5490 }, // RHAND_OCARINA_TIME
    { 0x5360, 0x5498 }, // FPS_RHAND_BOW
    { 0x5370, 0x54A0 }, // FPS_LHAND_HOOKSHOT
    // Sentinel
    { 0, 0 }
};

// Old zzplayas LUT offset → Z64O alias offset mapping (child)
static const OldToZ64O sOldChildMap[] = {
    // Hands
    { 0x5150, 0x5098 }, // LHAND
    { 0x5158, 0x50A0 }, // LFIST
    { 0x5160, 0x50A8 }, // LHAND_BOTTLE
    { 0x5168, 0x50B0 }, // RHAND
    { 0x5170, 0x50B8 }, // RFIST
    // Equipment
    { 0x5178, 0x50C0 }, // SWORD_SHEATH
    { 0x5180, 0x50D8 }, // SWORD_HILT → DL_SWORD_HILT_1
    { 0x5188, 0x50F0 }, // SWORD_BLADE → DL_SWORD_BLADE_1
    { 0x50D0, 0x5108 }, // SHIELD_DEKU → DL_SHIELD_1
    { 0x5190, 0x5180 }, // SLINGSHOT
    { 0x5198, 0x5190 }, // OCARINA_FAIRY
    { 0x51A0, 0x5128 }, // OCARINA_TIME → DL_OCARINA_2
    { 0x51A8, 0x5130 }, // DEKU_STICK
    { 0x51B0, 0x5178 }, // BOOMERANG
    { 0x51B8, 0x53F0 }, // SHIELD_HYLIAN_BACK
    { 0x51C0, 0x5120 }, // BOTTLE
    { 0x51C8, 0x50F8 }, // MASTER_SWORD → DL_SWORD_BLADE_2
    { 0x51D0, 0x5198 }, // GORON_BRACELET
    { 0x51E0, 0x5188 }, // SLINGSHOT_STRING
    // Masks
    { 0x51E8, 0x51D8 }, // MASK_BUNNY
    { 0x51F0, 0x51D0 }, // MASK_GERUDO
    { 0x51F8, 0x51C0 }, // MASK_GORON
    { 0x5200, 0x51B0 }, // MASK_KEATON
    { 0x5208, 0x51A8 }, // MASK_SPOOKY
    { 0x5210, 0x51B8 }, // MASK_TRUTH
    { 0x5218, 0x51C8 }, // MASK_ZORA
    { 0x5220, 0x51A0 }, // MASK_SKULL
    // Combined DLs
    { 0x52E0, 0x5448 }, // LFIST_SWORD → DL_LFIST_SWORD1
    { 0x5318, 0x5500 }, // LFIST_BOOMERANG
    { 0x5330, 0x5468 }, // RFIST_SHIELD_DEKU → DL_RFIST_SHIELD_1
    { 0x5348, 0x5508 }, // RFIST_SLINGSHOT
    { 0x5360, 0x5510 }, // RHAND_OCARINA_FAIRY
    { 0x5378, 0x5490 }, // RHAND_OCARINA_TIME
    // Sheath combos
    { 0x5248, 0x53D0 }, // SWORD_SHEATHED
    { 0x5268, 0x53E8 }, // SHIELD_DEKU_BACK
    { 0x5280, 0x5400 }, // SWORD_SHIELD_HYLIAN
    { 0x5298, 0x5408 }, // SWORD_SHIELD_DEKU
    { 0x52B0, 0x55C8 }, // SHEATH0_HYLIAN → SWORD1_SHIELD2_SHEATHED
    { 0x52C8, 0x55C0 }, // SHEATH0_DEKU → SWORD1_SHIELD1_SHEATHED
    // Sentinel
    { 0, 0 }
};

/**
 * Parse equipment DLs from the zobj, supporting both old zzplayas and Z64O formats.
 * Stores results keyed by Z64O alias offsets so PakLoader_GetEquipDL works for both.
 */
static void ZobjParseAliasTable(u8* zobjData, u32 zobjSize, std::map<u32, Gfx*>& equipDLs,
                                std::map<u32, Gfx*>& translatedDLs, SwapContext& ctx) {
    s32 customCount = 0;

    // Detect format: old zzplayas has "MODLOADER64" but NOT "UNIVERSAL_ALIAS_TABLE"
    bool isOldFormat = false;
    if (zobjSize > 0x500C) {
        bool hasML64 = (memcmp(zobjData + 0x5000, "MODL", 4) == 0);
        // Check for UNIVERSAL_ALIAS_TABLE string anywhere in the zobj
        bool hasUAT = false;
        for (u32 i = 0; i + 21 <= zobjSize && !hasUAT; i++) {
            if (memcmp(zobjData + i, "UNIVERSAL_ALIAS_TABLE", 21) == 0)
                hasUAT = true;
        }
        isOldFormat = hasML64 && !hasUAT;
    }

    if (isOldFormat) {
        // Old zzplayas format: read from old LUT offsets, store as Z64O alias keys
        // Age byte at 0x500B: 0=adult, 1=child
        u8 age = zobjData[0x500B];
        const OldToZ64O* map = (age == 1) ? sOldChildMap : sOldAdultMap;
        PAK_LOG("Detected OLD zzplayas format (age=%d), using old LUT offsets", age);
        for (const OldToZ64O* m = map; m->oldLut != 0; m++) {
            if (m->oldLut + 8 > zobjSize)
                continue;
            u32 de = BE_U32(zobjData + m->oldLut);
            if ((de >> 24) != 0xDE)
                continue; // Not a DL entry

            // Translate the entire mini-DL at this LUT offset.
            // Combined entries (like LFIST_SWORD) are multi-command DLs
            // that call sub-DLs for each component (hilt, blade, fist).
            // TranslateDL handles these correctly by following all G_DL calls.
            Gfx* translated = TranslateDL(ctx, m->oldLut, translatedDLs);
            if (translated) {
                equipDLs[m->z64oAlias] = translated;
                customCount++;
            }
        }
    } else {
        // Z64O universal format: read from standard alias table at 0x5020+
        for (u32 off = 0x5020; off < 0x5808 && off + 8 <= zobjSize; off += 8) {
            u32 de = BE_U32(zobjData + off);
            u32 ptr = BE_U32(zobjData + off + 4);

            if (de == 0xDE010000 && (ptr >> 24) == 0x06) {
                u32 dlOff = ptr & 0x00FFFFFF;
                if (dlOff < zobjSize) {
                    Gfx* translated = TranslateDL(ctx, dlOff, translatedDLs);
                    if (translated) {
                        equipDLs[off] = translated;
                        customCount++;
                    }
                }
            } else if (de == 0xDF000000) {
                equipDLs[off] = PAK_DL_STUB;
            }
        }
    }

    if (customCount > 0) {
        PAK_LOG("Parsed alias table: %d custom equipment DLs (old=%d)", customCount, isOldFormat);
    }
}

// ============================================================================
// Equipment Manifest Slot Name → Z64O Alias Offset
// ============================================================================

struct EquipSlotMapping {
    const char* slotName;
    u32 z64oAlias;
};

static const EquipSlotMapping sEquipSlotMap[] = {
    // Swords
    {"sword0_blade", 0x50F0},  // DL_SWORD_BLADE_1 (Kokiri)
    {"sword0_hilt", 0x50D8},   // DL_SWORD_HILT_1
    {"sword0_sheath", 0x50C0}, // DL_SWORD_SHEATH_1 (Kokiri sheath)
    {"sword1_blade", 0x50F8},  // DL_SWORD_BLADE_2 (Master)
    {"sword1_hilt", 0x50E0},   // DL_SWORD_HILT_2
    {"sword1_sheath", 0x50C0}, // DL_SWORD_SHEATH_1
    {"sword2_blade", 0x5100},  // DL_SWORD_BLADE_3 (Biggoron)
    {"sword2_hilt", 0x50E8},   // DL_SWORD_HILT_3
    // Shields
    {"shield0_held", 0x5108},  // DL_SHIELD_1 (Deku)
    {"shield1_held", 0x5110},  // DL_SHIELD_2 (Hylian)
    {"shield2_held", 0x5118},  // DL_SHIELD_3 (Mirror)
    // Ranged
    {"bow", 0x5138},           // DL_BOW
    {"hookshot", 0x5148},      // DL_HOOKSHOT
    {"boomerang", 0x5178},     // DL_BOOMERANG
    {"slingshot", 0x5180},     // DL_SLINGSHOT
    // Items
    {"deku_stick", 0x5130},    // DL_DEKU_STICK
    {"bottle", 0x5120},        // DL_BOTTLE
    {"ocarina_0", 0x5190},     // DL_OCARINA_FAIRY
    {"ocarina_1_a", 0x5128},   // DL_OCARINA_2 (adult OoT)
    {"ocarina_1", 0x5128},     // DL_OCARINA_2 (alternate name)
    {"hammer", 0x51F0},        // DL_HAMMER
    // Sentinel
    {NULL, 0}
};

static u32 EquipSlotNameToAlias(const char* slotName) {
    for (const EquipSlotMapping* m = sEquipSlotMap; m->slotName != NULL; m++) {
        if (strcmp(slotName, m->slotName) == 0) return m->z64oAlias;
    }
    return 0;
}

/**
 * Load a single equipment zobj from a zzequipment pak.
 * Reads the EQUIPMANIFEST JSON, translates DLs, stores in equipDLs maps.
 */
static void LoadEquipmentZobj(u8* zobjData, u32 zobjSize, PakModel& model) {
    // Find EQUIPMANIFEST marker
    const char* marker = "EQUIPMANIFEST";
    u8* found = NULL;
    for (u32 i = 0; i + 13 <= zobjSize; i++) {
        if (memcmp(zobjData + i, marker, 13) == 0) {
            found = zobjData + i;
            break;
        }
    }
    if (!found) {
        PAK_LOG("Equipment zobj has no EQUIPMANIFEST");
        return;
    }

    // Find JSON start (skip null bytes after marker)
    u8* jsonStart = found + 13;
    while (jsonStart < zobjData + zobjSize && *jsonStart == 0) jsonStart++;
    if (jsonStart >= zobjData + zobjSize || *jsonStart != '{') return;

    // Extract JSON string
    s32 depth = 0;
    u8* jsonEnd = jsonStart;
    for (u8* p = jsonStart; p < zobjData + zobjSize; p++) {
        if (*p == '{') depth++;
        else if (*p == '}') { depth--; if (depth == 0) { jsonEnd = p + 1; break; } }
    }
    std::string json((char*)jsonStart, jsonEnd - jsonStart);

    // Find MODLOADER64 header to get DL pointers
    u8* ml64 = NULL;
    for (u32 i = 0; i + 11 <= zobjSize; i++) {
        if (memcmp(zobjData + i, "MODLOADER64", 11) == 0) {
            ml64 = zobjData + i;
            break;
        }
    }
    if (!ml64) return;

    // DL count is at ml64+12 as u32 BE (after "MODLOADER64" + version byte)
    u32 headerOff = (u32)(ml64 - zobjData);
    // DL entries start after: magic(11) + version(1) + count(4) = offset +16
    // But the actual format has the count at +12 and DLs at +16... let me check
    // From analysis: after "R64i" at +12, count at +12+4=+16? No.
    // Let's just scan for DE entries after the magic
    u32 dlEntryStart = headerOff + 16; // After MODLOADER64(11) + version(1) + padding(4)

    // Collect DL offsets (each is {0xDE010000, 0x06XXXXXX})
    std::vector<u32> dlOffsets;
    for (u32 off = dlEntryStart; off + 8 <= zobjSize; off += 8) {
        u32 de = BE_U32(zobjData + off);
        u32 ptr = BE_U32(zobjData + off + 4);
        if ((de >> 24) == 0xDE && (ptr >> 24) == 0x06) {
            dlOffsets.push_back(ptr & 0x00FFFFFF);
        } else {
            break; // End of DL entries
        }
    }

    if (dlOffsets.empty()) {
        PAK_LOG("Equipment zobj has no DL entries");
        return;
    }

    // Set up swap context for translating DLs
    SwapContext ctx;
    ctx.data = zobjData;
    ctx.size = zobjSize;
    ctx.lastTexFmt = 0;
    ctx.lastTexSiz = 0;
    ctx.lastTexAddr = 0;
    ctx.lastTexWidth = 0;
    ctx.lastTexHeight = 0;

    // Parse the JSON to find slot assignments
    // Format: {"OOT":{"adult":{"0":"sword1_blade","1":"sword1_hilt"},"child":{...}}}
    // Simple parser: find "adult":{...} and "child":{...} sections
    auto parseAge = [&](const char* ageName, std::map<u32, Gfx*>& equipDLs,
                         std::map<u32, Gfx*>& translatedDLs) {
        std::string ageKey = std::string("\"") + ageName + "\":{";
        size_t agePos = json.find(ageKey);
        if (agePos == std::string::npos) return;
        agePos += ageKey.length();

        // Find matching close brace
        s32 d = 1;
        size_t ageEnd = agePos;
        for (size_t i = agePos; i < json.length() && d > 0; i++) {
            if (json[i] == '{') d++;
            else if (json[i] == '}') { d--; if (d == 0) ageEnd = i; }
        }

        std::string ageSection = json.substr(agePos, ageEnd - agePos);
        if (ageSection.empty() || ageSection == "}") return;

        // Parse "index":"slotname" pairs
        size_t pos = 0;
        while (pos < ageSection.length()) {
            // Find "N":"name"
            size_t q1 = ageSection.find('"', pos);
            if (q1 == std::string::npos) break;
            size_t q2 = ageSection.find('"', q1 + 1);
            if (q2 == std::string::npos) break;
            std::string indexStr = ageSection.substr(q1 + 1, q2 - q1 - 1);

            size_t q3 = ageSection.find('"', q2 + 1);
            if (q3 == std::string::npos) break;
            size_t q4 = ageSection.find('"', q3 + 1);
            if (q4 == std::string::npos) break;
            std::string slotName = ageSection.substr(q3 + 1, q4 - q3 - 1);

            pos = q4 + 1;

            // Convert index to DL offset
            s32 dlIdx = atoi(indexStr.c_str());
            if (dlIdx < 0 || dlIdx >= (s32)dlOffsets.size()) continue;

            // Map slot name to Z64O alias
            u32 alias = EquipSlotNameToAlias(slotName.c_str());
            if (alias == 0) {
                PAK_LOG("Unknown equipment slot: '%s'", slotName.c_str());
                continue;
            }

            // Translate the DL
            Gfx* translated = TranslateDL(ctx, dlOffsets[dlIdx], translatedDLs);
            if (translated) {
                equipDLs[alias] = translated;
                PAK_LOG("Equipment: '%s' (DL %d @ 0x%X) -> alias 0x%04X",
                        slotName.c_str(), dlIdx, dlOffsets[dlIdx], alias);
            }
        }
    };

    parseAge("adult", model.adultEquipDLs, model.adultTranslatedDLs);
    parseAge("child", model.childEquipDLs, model.childTranslatedDLs);

    // Generate combined DLs from individual pieces.
    // Z64O generates these at load time: DL_LFIST_SWORD1 = hilt + blade + lfist, etc.
    // For back-mounted items (sheath, shield), Z64O uses positioning matrices.
    // We don't have those matrices for equipment-only paks, so those items may not
    // be positioned perfectly. The vanilla limb positioning handles most of it.
    auto generateCombined = [](std::map<u32, Gfx*>& eq) {
        auto makeCombinedDL = [](std::vector<Gfx*> subDLs) -> Gfx* {
            if (subDLs.empty()) return NULL;
            Gfx* dl = (Gfx*)calloc(subDLs.size() + 1, sizeof(Gfx));
            for (size_t i = 0; i < subDLs.size(); i++) {
                dl[i].words.w0 = (uintptr_t)(0xDE000000); // G_DL push
                dl[i].words.w1 = (uintptr_t)subDLs[i];
            }
            dl[subDLs.size()].words.w0 = (uintptr_t)(0xDF000000); // G_ENDDL
            dl[subDLs.size()].words.w1 = 0;
            return dl;
        };

        // Standard shield-back matrix: rotates 180° around Y + translates to back position
        // Extracted from vanilla object_link_boy (same in all zzplayas models)
        // Matrix = {-1,0,0,0, 0,-1,0,0, 0,0,1,0, 935,94,29,1} in fixed-point
        static u8 sShieldBackMtxBE[64] = {
            0xFF,0xFF,0x00,0x00, 0x00,0x00,0x00,0x00,
            0x00,0x00,0xFF,0xFF, 0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00, 0x00,0x01,0x00,0x00,
            0x03,0xA7,0x00,0x5E, 0x00,0x1D,0x00,0x01,
            0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
        };
        static u8 sShieldBackMtxSwapped = 0;
        static Mtx* sShieldBackMtx = NULL;
        if (!sShieldBackMtxSwapped) {
            sShieldBackMtx = (Mtx*)malloc(64);
            memcpy(sShieldBackMtx, sShieldBackMtxBE, 64);
            // Byte-swap from BE to native (16 x s32)
            for (int mi = 0; mi < 16; mi++) {
                u8* p = (u8*)sShieldBackMtx + mi * 4;
                u8 t;
                t = p[0]; p[0] = p[3]; p[3] = t;
                t = p[1]; p[1] = p[2]; p[2] = t;
            }
            sShieldBackMtxSwapped = 1;
        }

        // Create a DL that: push matrix → draw DL → pop matrix
        auto makeMtxWrappedDL = [](Mtx* mtx, Gfx* innerDL) -> Gfx* {
            if (!innerDL) return NULL;
            // G_MTX push + G_DL push + G_POPMTX + G_ENDDL = 4 commands
            Gfx* dl = (Gfx*)calloc(4, sizeof(Gfx));
            dl[0].words.w0 = (uintptr_t)(0xDA380000); // gSPMatrix(mtx, PUSH|LOAD|MODELVIEW)
            dl[0].words.w1 = (uintptr_t)mtx;
            dl[1].words.w0 = (uintptr_t)(0xDE000000); // G_DL push
            dl[1].words.w1 = (uintptr_t)innerDL;
            dl[2].words.w0 = (uintptr_t)(0xD8380002); // gSPPopMatrix(MODELVIEW)
            dl[2].words.w1 = (uintptr_t)(0x00000040);
            dl[3].words.w0 = (uintptr_t)(0xDF000000); // G_ENDDL
            dl[3].words.w1 = 0;
            return dl;
        };

        struct CombinedDef { u32 result; u32 pieces[4]; };

        CombinedDef combos[] = {
            // Held weapons (weapon pieces only — fist comes from body pak via merge)
            { 0x5448, { 0x50D8, 0x50F0, 0, 0 } }, // DL_LFIST_SWORD1 = hilt1 + blade1
            { 0x5450, { 0x50E0, 0x50F8, 0, 0 } }, // DL_LFIST_SWORD2 = hilt2 + blade2
            { 0x5458, { 0x50E8, 0x5100, 0, 0 } }, // DL_LFIST_SWORD3 = hilt3 + blade3
            { 0x5460, { 0x51F0, 0, 0, 0 } },       // DL_LFIST_HAMMER = hammer
            { 0x5500, { 0x5178, 0, 0, 0 } },       // DL_LFIST_BOOMERANG = boomerang
            // Held shields/items (item only — fist comes from body pak)
            { 0x5468, { 0x5108, 0, 0, 0 } },       // DL_RFIST_SHIELD_1 = shield1
            { 0x5470, { 0x5110, 0, 0, 0 } },       // DL_RFIST_SHIELD_2 = shield2
            { 0x5478, { 0x5118, 0, 0, 0 } },       // DL_RFIST_SHIELD_3 = shield3
            { 0x5480, { 0x5138, 0, 0, 0 } },       // DL_RFIST_BOW = bow
            { 0x5488, { 0x5148, 0, 0, 0 } },       // DL_RFIST_HOOKSHOT = hookshot
            { 0x5508, { 0x5180, 0, 0, 0 } },       // DL_RFIST_SLINGSHOT = slingshot
            { 0x5510, { 0x5190, 0, 0, 0 } },       // DL_RHAND_OCARINA_FAIRY
            { 0x5490, { 0x5128, 0, 0, 0 } },       // DL_RHAND_OCARINA_TIME
            // Sheathed swords on back (hilt + sheath)
            { 0x53D0, { 0x50D8, 0x50C0, 0, 0 } },  // DL_SWORD1_SHEATHED
            { 0x53D8, { 0x50E0, 0x50C0, 0, 0 } },  // DL_SWORD2_SHEATHED
            { 0x53E0, { 0x50E8, 0x50C0, 0, 0 } },  // DL_SWORD3_SHEATHED
            // Shields on back
            { 0x53E8, { 0x5108, 0, 0, 0 } },        // DL_SHIELD1_BACK
            { 0x53F0, { 0x5110, 0, 0, 0 } },        // DL_SHIELD2_BACK
            { 0x53F8, { 0x5118, 0, 0, 0 } },        // DL_SHIELD3_BACK
            // Sword+Shield on back
            { 0x5400, { 0x53D0, 0x53E8, 0, 0 } },   // DL_SWORD1_SHIELD1
            { 0x5408, { 0x53D0, 0x53F0, 0, 0 } },   // DL_SWORD1_SHIELD2
            { 0x5410, { 0x53D0, 0x53F8, 0, 0 } },   // DL_SWORD1_SHIELD3
            { 0x5418, { 0x53D8, 0x53E8, 0, 0 } },   // DL_SWORD2_SHIELD1
            { 0x5420, { 0x53D8, 0x53F0, 0, 0 } },   // DL_SWORD2_SHIELD2
            { 0x5428, { 0x53D8, 0x53F8, 0, 0 } },   // DL_SWORD2_SHIELD3
            { 0x5430, { 0x53E0, 0x53E8, 0, 0 } },   // DL_SWORD3_SHIELD1
            { 0x5438, { 0x53E0, 0x53F0, 0, 0 } },   // DL_SWORD3_SHIELD2
            { 0x5440, { 0x53E0, 0x53F8, 0, 0 } },   // DL_SWORD3_SHIELD3
            // Sword+Shield sheathed
            { 0x55C0, { 0x53E8, 0x53D0, 0, 0 } },   // DL_SWORD1_SHIELD1_SHEATHED
            { 0x55C8, { 0x53F0, 0x53D0, 0, 0 } },   // DL_SWORD1_SHIELD2_SHEATHED
            { 0x55D0, { 0x53F8, 0x53D0, 0, 0 } },   // DL_SWORD1_SHIELD3_SHEATHED
            { 0x55D8, { 0x53E8, 0x53D8, 0, 0 } },   // DL_SWORD2_SHIELD1_SHEATHED
            { 0x55E0, { 0x53F0, 0x53D8, 0, 0 } },   // DL_SWORD2_SHIELD2_SHEATHED
            { 0x55E8, { 0x53F8, 0x53D8, 0, 0 } },   // DL_SWORD2_SHIELD3_SHEATHED
            { 0x55F0, { 0x53E8, 0x53E0, 0, 0 } },   // DL_SWORD3_SHIELD1_SHEATHED
            { 0x55F8, { 0x53F0, 0x53E0, 0, 0 } },   // DL_SWORD3_SHIELD2_SHEATHED
            { 0x5600, { 0x53F8, 0x53E0, 0, 0 } },   // DL_SWORD3_SHIELD3_SHEATHED
            { 0, { 0, 0, 0, 0 } }
        };

        // Multiple passes for combos that reference other generated combos
        for (s32 pass = 0; pass < 3; pass++) {
            for (CombinedDef* c = combos; c->result != 0; c++) {
                if (eq.count(c->result)) continue;
                if (!eq.count(c->pieces[0])) continue;

                std::vector<Gfx*> subDLs;
                for (int p = 0; c->pieces[p] != 0; p++) {
                    auto it = eq.find(c->pieces[p]);
                    if (it != eq.end() && it->second != NULL && it->second != PAK_DL_STUB) {
                        subDLs.push_back(it->second);
                    }
                }
                if (!subDLs.empty()) {
                    // Shield-back entries need matrix wrapping for correct orientation
                    bool isShieldBack = (c->result == 0x53E8 || c->result == 0x53F0 || c->result == 0x53F8);
                    Gfx* combined;
                    if (isShieldBack && subDLs.size() == 1 && sShieldBackMtx) {
                        combined = makeMtxWrappedDL(sShieldBackMtx, subDLs[0]);
                    } else {
                        combined = makeCombinedDL(subDLs);
                    }
                    if (combined) {
                        eq[c->result] = combined;
                    }
                }
            }
        }
    };

    generateCombined(model.adultEquipDLs);
    generateCombined(model.childEquipDLs);

    PAK_LOG("Equipment after generation: adult=%d DLs, child=%d DLs",
            (int)model.adultEquipDLs.size(), (int)model.childEquipDLs.size());
}

// ============================================================================
// PAK Model Loading
// ============================================================================

static bool LoadPakModel(PakModel& model) {
    // Parse the .pak file
    std::vector<PakEntry> entries;
    if (!PakParser_Parse(model.pakPath, entries)) {
        PAK_LOG("Failed to parse PAK: %s", model.pakPath.c_str());
        return false;
    }

    // Read entire .pak into memory for extraction
    FILE* f = fopen(model.pakPath.c_str(), "rb");
    if (!f)
        return false;

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<u8> pakData(fileSize);
    if (fread(pakData.data(), 1, fileSize, f) != (size_t)fileSize) {
        fclose(f);
        return false;
    }
    fclose(f);

    // Find package.json
    std::string packageJson;
    std::string adultZobjName;
    std::string childZobjName;

    for (auto& entry : entries) {
        if (entry.name.find("package.json") != std::string::npos) {
            u32 size = entry.dataEnd - entry.dataStart;
            if (entry.compressed) {
                uLongf decompSize = size * 8;
                std::vector<u8> decompBuf(decompSize);
                if (uncompress(decompBuf.data(), &decompSize, pakData.data() + entry.dataStart, size) == Z_OK) {
                    packageJson.assign((char*)decompBuf.data(), decompSize);
                }
            } else {
                packageJson.assign((char*)pakData.data() + entry.dataStart, size);
            }
            break;
        }
    }

    if (packageJson.empty()) {
        PAK_LOG("No package.json found in PAK");
        return false;
    }

    // Parse model name
    std::string name = JsonFindString(packageJson, "name");
    if (!name.empty()) {
        snprintf(model.displayName, sizeof(model.displayName), "%s", name.c_str());
    }

    // Check if this is a zzequipment pak (equipment-only, no body model)
    if (packageJson.find("\"zzequipment\"") != std::string::npos) {
        model.isEquipmentOnly = 1;
        PAK_LOG("Loading equipment pak: '%s'", model.displayName);

        // Find the equipment array in JSON: "equipment": ["file1.zobj", "file2.zobj"]
        size_t eqPos = packageJson.find("\"equipment\"");
        if (eqPos == std::string::npos) {
            PAK_LOG("No equipment array found");
            return false;
        }
        size_t arrStart = packageJson.find('[', eqPos);
        size_t arrEnd = packageJson.find(']', arrStart);
        if (arrStart == std::string::npos || arrEnd == std::string::npos) return false;

        std::string arr = packageJson.substr(arrStart + 1, arrEnd - arrStart - 1);

        // Parse filenames from array
        std::vector<std::string> equipFiles;
        size_t pos = 0;
        while (pos < arr.length()) {
            size_t q1 = arr.find('"', pos);
            if (q1 == std::string::npos) break;
            size_t q2 = arr.find('"', q1 + 1);
            if (q2 == std::string::npos) break;
            std::string fname = arr.substr(q1 + 1, q2 - q1 - 1);
            if (!fname.empty()) equipFiles.push_back(fname);
            pos = q2 + 1;
        }

        // Load each equipment zobj
        s32 loadedCount = 0;
        for (auto& equipFile : equipFiles) {
            // Extract the zobj from the pak
            for (auto& entry : entries) {
                if (entry.name.find(equipFile) == std::string::npos) continue;

                u32 compSize = entry.dataEnd - entry.dataStart;
                if (compSize == 0 || entry.dataStart + compSize > (u32)fileSize) continue;

                u8* zobjData = NULL;
                u32 zobjSize = 0;

                if (entry.compressed) {
                    uLongf decompSize = compSize * 8;
                    zobjData = (u8*)malloc(decompSize);
                    if (!zobjData) continue;
                    if (uncompress(zobjData, &decompSize, pakData.data() + entry.dataStart, compSize) != Z_OK) {
                        free(zobjData);
                        continue;
                    }
                    zobjSize = (u32)decompSize;
                } else {
                    zobjData = (u8*)malloc(compSize);
                    if (!zobjData) continue;
                    memcpy(zobjData, pakData.data() + entry.dataStart, compSize);
                    zobjSize = compSize;
                }

                PAK_LOG("Loading equipment item: '%s' (%u bytes)", equipFile.c_str(), zobjSize);
                LoadEquipmentZobj(zobjData, zobjSize, model);
                // Do NOT free zobjData — translated DLs have direct pointers into it
                // (vertex data, texture data). The buffer lives for the model's lifetime.
                loadedCount++;
                break;
            }
        }

        PAK_LOG("Equipment pak loaded: %d items, %d adult DLs, %d child DLs",
                loadedCount, (int)model.adultEquipDLs.size(), (int)model.childEquipDLs.size());

        return !model.adultEquipDLs.empty() || !model.childEquipDLs.empty();
    }

    // Parse model file references from zzplayas manifest
    adultZobjName = JsonFindModelFile(packageJson, "adult_model");
    childZobjName = JsonFindModelFile(packageJson, "child_model");

    PAK_LOG("Model '%s': adult='%s', child='%s'", model.displayName, adultZobjName.c_str(), childZobjName.c_str());

    // Extract and process .zobj files
    auto extractZobj = [&](const std::string& zobjName, u8** outData, u32* outSize) -> bool {
        if (zobjName.empty())
            return false;

        for (auto& entry : entries) {
            // Match by filename (entries may have path prefix like "N64_Kafei/xxx.zobj")
            if (entry.name.find(zobjName) != std::string::npos) {
                u32 compSize = entry.dataEnd - entry.dataStart;
                if (compSize == 0 || entry.dataStart + compSize > (u32)fileSize)
                    return false;

                if (entry.compressed) {
                    // DEFL: decompress with zlib
                    // Estimate max decompressed size (zobj rarely > 4x compressed)
                    uLongf decompSize = compSize * 8;
                    u8* decompBuf = (u8*)malloc(decompSize);
                    if (!decompBuf)
                        return false;

                    int ret = uncompress(decompBuf, &decompSize, pakData.data() + entry.dataStart, compSize);
                    if (ret != Z_OK) {
                        PAK_LOG("zlib decompress failed (err=%d) for '%s'", ret, zobjName.c_str());
                        free(decompBuf);
                        return false;
                    }

                    // Realloc to exact size
                    *outData = (u8*)realloc(decompBuf, decompSize);
                    if (!*outData)
                        *outData = decompBuf; // realloc failed, keep original
                    *outSize = (u32)decompSize;

                    PAK_LOG("Extracted '%s': %u bytes (decompressed from %u)", zobjName.c_str(), (u32)decompSize,
                            compSize);
                } else {
                    // UNCO: raw copy
                    *outData = (u8*)malloc(compSize);
                    if (!*outData)
                        return false;

                    memcpy(*outData, pakData.data() + entry.dataStart, compSize);
                    *outSize = compSize;

                    PAK_LOG("Extracted '%s': %u bytes", zobjName.c_str(), compSize);
                }
                return true;
            }
        }

        PAK_LOG("ZOBJ '%s' not found in PAK", zobjName.c_str());
        return false;
    };

    // Extract adult model
    if (!adultZobjName.empty()) {
        if (extractZobj(adultZobjName, &model.adultZobj, &model.adultZobjSize)) {
            model.hasAdult = 1;

            SwapContext adultSwapCtx = {};
            if (ZobjBuildSkeleton(model.adultZobj, model.adultZobjSize, model.adultLimbs, model.adultLimbTable,
                                  &model.adultFlexHeader, model.adultTranslatedDLs, &adultSwapCtx)) {
                model.adultReady = 1;
                ZobjParseAliasTable(model.adultZobj, model.adultZobjSize, model.adultEquipDLs, model.adultTranslatedDLs,
                                    adultSwapCtx);
                PAK_LOG("Adult model ready!");
            } else {
                PAK_LOG("Failed to build adult skeleton");
            }
        }
    }

    // Extract child model
    if (!childZobjName.empty()) {
        if (extractZobj(childZobjName, &model.childZobj, &model.childZobjSize)) {
            model.hasChild = 1;

            SwapContext childSwapCtx = {};
            if (ZobjBuildSkeleton(model.childZobj, model.childZobjSize, model.childLimbs, model.childLimbTable,
                                  &model.childFlexHeader, model.childTranslatedDLs, &childSwapCtx)) {
                model.childReady = 1;
                ZobjParseAliasTable(model.childZobj, model.childZobjSize, model.childEquipDLs, model.childTranslatedDLs,
                                    childSwapCtx);
                PAK_LOG("Child model ready!");
            } else {
                PAK_LOG("Failed to build child skeleton");
            }
        }
    }

    return model.adultReady || model.childReady;
}

// ============================================================================
// Skeleton Swap (before/after vanilla draw)
// ============================================================================

// Saved original skeleton data for restore after draw
static void** sSavedSkeleton = NULL;
static s32 sSavedDListCount = 0;

extern "C" void PakLoader_SwapSkeleton(Player* player) {
    if (sGetActiveIndex() < 0 || sGetActiveIndex() >= (s32)sModels.size())
        return;

    PakModel& model = sModels[sGetActiveIndex()];
    u8 isAdult = (LINK_AGE_IN_YEARS == YEARS_ADULT);

    void** pakSkeleton = NULL;
    s32 pakDListCount = 0;

    if (isAdult && model.adultReady) {
        pakSkeleton = model.adultLimbTable;
        pakDListCount = model.adultFlexHeader.dListCount;
    } else if (!isAdult && model.childReady) {
        pakSkeleton = model.childLimbTable;
        pakDListCount = model.childFlexHeader.dListCount;
    }

    if (!pakSkeleton)
        return;

    // Save originals
    sSavedSkeleton = (void**)player->skelAnime.skeleton;
    sSavedDListCount = player->skelAnime.dListCount;

    // Swap in custom
    player->skelAnime.skeleton = pakSkeleton;
    player->skelAnime.dListCount = pakDListCount;
}

extern "C" void PakLoader_RestoreSkeleton(Player* player) {
    if (!sSavedSkeleton)
        return;

    player->skelAnime.skeleton = sSavedSkeleton;
    player->skelAnime.dListCount = sSavedDListCount;

    sSavedSkeleton = NULL;
    sSavedDListCount = 0;
}

// ============================================================================
// Equipment DL Override (Smart alias table lookup)
// ============================================================================

// Per-frame flags: whether combined DLs were used (include weapon geometry)
static u8 sPakLeftHandCombined = 0;
static u8 sPakRightHandCombined = 0;

// Try alias with fallbacks. Returns PAK_DL_STUB for 0xDF entries, NULL if not found.
static Gfx* FindEquip(std::map<u32, Gfx*>& equipDLs, u32 primary, u32 fb1 = 0, u32 fb2 = 0) {
    auto it = equipDLs.find(primary);
    if (it != equipDLs.end())
        return it->second; // Could be real DL or PAK_DL_STUB
    if (fb1) {
        it = equipDLs.find(fb1);
        if (it != equipDLs.end())
            return it->second;
    }
    if (fb2) {
        it = equipDLs.find(fb2);
        if (it != equipDLs.end())
            return it->second;
    }
    return NULL;
}

// Helper: get the merged equipment DLs map (equipment pak overrides body pak)
static std::map<u32, Gfx*>* sGetEquipDLs(void) {
    static std::map<u32, Gfx*> sMerged;
    sMerged.clear();
    u8 isAdult = (LINK_AGE_IN_YEARS == YEARS_ADULT);

    // Start with body model's equipment DLs
    s32 bodyIdx = sGetActiveIndex();
    if (bodyIdx >= 0 && bodyIdx < (s32)sModels.size()) {
        auto& bodyEq = isAdult ? sModels[bodyIdx].adultEquipDLs : sModels[bodyIdx].childEquipDLs;
        sMerged.insert(bodyEq.begin(), bodyEq.end());
    }

    // Equipment pak overrides body pak
    if (sSelectedEquipIndex >= 0 && sSelectedEquipIndex < (s32)sModels.size()) {
        auto& equipEq = isAdult ? sModels[sSelectedEquipIndex].adultEquipDLs
                                : sModels[sSelectedEquipIndex].childEquipDLs;
        for (auto& [k, v] : equipEq) {
            sMerged[k] = v; // Override
        }
    }

    return sMerged.empty() ? NULL : &sMerged;
}

extern "C" Gfx* PakLoader_GetEquipDL(Player* player, s32 limbIndex) {
    if (!CVarGetInteger("gMods.PakLoader.Enabled", 0))
        return NULL;

    std::map<u32, Gfx*>* eqPtr = sGetEquipDLs();
    if (!eqPtr) return NULL;
    std::map<u32, Gfx*>& eq = *eqPtr;

    Gfx* result = NULL;

    static u32 sEquipLog = 0;
    u8 doLog = (sEquipLog < 20);

    if (limbIndex == PLAYER_LIMB_L_HAND) {
        sPakLeftHandCombined = 0;
        switch (player->leftHandType) {
            case PLAYER_MODELTYPE_LH_OPEN:
                result = FindEquip(eq, 0x5098);
                break;
            case PLAYER_MODELTYPE_LH_CLOSED:
                result = FindEquip(eq, 0x50A0);
                break;
            case PLAYER_MODELTYPE_LH_SWORD:
                result = FindEquip(eq, 0x5448, 0x50A0);
                sPakLeftHandCombined = 1;
                break;
            case PLAYER_MODELTYPE_LH_SWORD_2:
                result = FindEquip(eq, 0x5450, 0x5448, 0x50A0);
                sPakLeftHandCombined = 1;
                break;
            case PLAYER_MODELTYPE_LH_BGS:
                result = FindEquip(eq, 0x5458, 0x5448, 0x50A0);
                sPakLeftHandCombined = 1;
                break;
            case PLAYER_MODELTYPE_LH_HAMMER:
                result = FindEquip(eq, 0x5460, 0x50A0);
                sPakLeftHandCombined = 1;
                break;
            case PLAYER_MODELTYPE_LH_BOOMERANG:
                result = FindEquip(eq, 0x5500, 0x50A0);
                sPakLeftHandCombined = 1;
                break;
            case PLAYER_MODELTYPE_LH_BOTTLE:
                result = FindEquip(eq, 0x50A8, 0x5098);
                break;
        }
    } else if (limbIndex == PLAYER_LIMB_R_HAND) {
        sPakRightHandCombined = 0;
        switch (player->rightHandType) {
            case PLAYER_MODELTYPE_RH_OPEN:
                result = FindEquip(eq, 0x50B0);
                break;
            case PLAYER_MODELTYPE_RH_CLOSED:
                result = FindEquip(eq, 0x50B8);
                break;
            case PLAYER_MODELTYPE_RH_SHIELD: {
                u32 sa[] = { 0x5468, 0x5470, 0x5478 };
                s32 idx = player->currentShield - 1;
                if (idx >= 0 && idx < 3)
                    result = FindEquip(eq, sa[idx], 0x5468, 0x50B8);
                else
                    result = FindEquip(eq, 0x5468, 0x50B8);
                sPakRightHandCombined = 1;
                break;
            }
            case PLAYER_MODELTYPE_RH_BOW_SLINGSHOT:
                result = FindEquip(eq, 0x5480, 0x50B8);
                sPakRightHandCombined = 1;
                break;
            case PLAYER_MODELTYPE_RH_BOW_SLINGSHOT_2:
                result = FindEquip(eq, 0x5480, 0x50B8);
                sPakRightHandCombined = 1;
                break;
            case PLAYER_MODELTYPE_RH_OCARINA:
                result = FindEquip(eq, 0x5510, 0x50B0);
                break;
            case PLAYER_MODELTYPE_RH_OOT:
                result = FindEquip(eq, 0x5490, 0x50B0);
                break;
            case PLAYER_MODELTYPE_RH_HOOKSHOT:
                result = FindEquip(eq, 0x5488, 0x50B8);
                sPakRightHandCombined = 1;
                break;
        }
    } else if (limbIndex == PLAYER_LIMB_SHEATH) {
        // Vanilla sheath logic:
        // SHEATH_16 = sword+sheath on back (sSwordAndSheathDLs) — NO shield
        // SHEATH_17 = sheath only (sSheathDLs) — NO shield
        // SHEATH_18 = sword sheathed + shield on back (sSheathWithSwordDLs + shield offset)
        // SHEATH_19 = sheath + shield on back, no sword (sSheathWithoutSwordDLs + shield offset)
        s32 sheathType = player->sheathType;
        s32 shield = player->currentShield;
        s32 hasShieldOnBack = (sheathType == PLAYER_MODELTYPE_SHEATH_18 ||
                               sheathType == PLAYER_MODELTYPE_SHEATH_19);
        s32 hasSword = (sheathType == PLAYER_MODELTYPE_SHEATH_16 ||
                        sheathType == PLAYER_MODELTYPE_SHEATH_18);

        if (hasSword && hasShieldOnBack && shield > 0) {
            // Sword sheathed + shield on back
            u32 ca[] = { 0x55C0, 0x55C8, 0x55D0 };
            result = FindEquip(eq, ca[shield - 1], 0x53D0, 0x50C0);
        } else if (hasSword) {
            // Sword sheathed, no shield on back
            result = FindEquip(eq, 0x53D0, 0x50C0);
        } else if (hasShieldOnBack && shield > 0) {
            // Shield on back, no sword
            u32 sba[] = { 0x53E8, 0x53F0, 0x53F8 };
            result = FindEquip(eq, sba[shield - 1], 0x53E8);
        } else {
            // Just sheath or nothing
            result = FindEquip(eq, 0x50C8, 0x50C0);
        }
    } else if (limbIndex == PLAYER_LIMB_WAIST) {
        result = FindEquip(eq, 0x5020);
    }

    if (doLog && result != NULL) {
        sEquipLog++;
        PAK_LOG("GetEquipDL: limb=%d LH=%d RH=%d result=%p stub=%d", limbIndex, player->leftHandType,
                player->rightHandType, (void*)result, (result == PAK_DL_STUB) ? 1 : 0);
    }

    return result;
}

extern "C" u8 PakLoader_UsedCombinedDL(u8 isLeftHand) {
    return isLeftHand ? sPakLeftHandCombined : sPakRightHandCombined;
}

// ============================================================================
// gSPDisplayList Override (standalone equipment items from object_link_boy)
// ============================================================================

#include "objects/object_link_boy/object_link_boy.h"

// Map OTR path pointers → Z64O alias offsets for standalone equipment DLs.
// These are items drawn by PostLimbDraw and other systems that reference
// object_link_boy DLs directly via gSPDisplayList.
struct OtrAliasEntry {
    const char* otr;
    u32 alias;
};

static const OtrAliasEntry sStandaloneOtrTable[] = {
    // Hookshot parts
    { gLinkAdultHookshotChainDL, 0x5150 }, // DL_HOOKSHOT_CHAIN
    { gLinkAdultHookshotTipDL, 0x5158 },   // DL_HOOKSHOT_HOOK
    // Bow string
    { gLinkAdultBowStringDL, 0x5140 }, // DL_BOW_STRING
    // Bottle
    { gLinkAdultBottleDL, 0x5120 }, // DL_BOTTLE
    // Boots
    { gLinkAdultLeftIronBootDL, 0x5228 },   // DL_BOOT_LIRON
    { gLinkAdultRightIronBootDL, 0x5230 },  // DL_BOOT_RIRON
    { gLinkAdultLeftHoverBootDL, 0x5238 },  // DL_BOOT_LHOVER
    { gLinkAdultRightHoverBootDL, 0x5240 }, // DL_BOOT_RHOVER
    // Waist
    { gLinkAdultWaistNearDL, 0x5020 }, // DL_WAIST
    { gLinkAdultWaistFarDL, 0x5020 },
    // Gauntlet upgrade forearms/hands
    { gLinkAdultRightArmOutNearDL, 0x5210 },  // DL_UPGRADE_RFOREARM
    { gLinkAdultRightHandOutNearDL, 0x5218 }, // DL_UPGRADE_RHAND
    { gLinkAdultLeftArmOutNearDL, 0x51F8 },   // DL_UPGRADE_LFOREARM
    // Gauntlet plates (no direct Z64O alias — keep vanilla)
    // Hookshot reticle
    { gLinkAdultHookshotReticleDL, 0x5160 }, // DL_HOOKSHOT_AIM
    // Sheath combos on back (drawn by OverrideLimbDraw but also referenced standalone)
    { gLinkAdultSheathNearDL, 0x50C8 }, // DL_SWORD_SHEATH_2
    { gLinkAdultSheathFarDL, 0x50C8 },
    { gLinkAdultMasterSwordAndSheathNearDL, 0x50C0 }, // DL_SWORD_SHEATH_1
    { gLinkAdultMasterSwordAndSheathFarDL, 0x50C0 },
    { gLinkAdultHylianShieldSwordAndSheathNearDL, 0x5420 }, // DL_SWORD2_SHIELD2
    { gLinkAdultHylianShieldSwordAndSheathFarDL, 0x5420 },
    { gLinkAdultMirrorShieldSwordAndSheathNearDL, 0x5428 }, // DL_SWORD2_SHIELD3
    { gLinkAdultMirrorShieldSwordAndSheathFarDL, 0x5428 },
    { gLinkAdultHylianShieldAndSheathNearDL, 0x53F0 }, // DL_SHIELD2_BACK
    { gLinkAdultHylianShieldAndSheathFarDL, 0x53F0 },
    { gLinkAdultMirrorShieldAndSheathNearDL, 0x53F8 }, // DL_SHIELD3_BACK
    { gLinkAdultMirrorShieldAndSheathFarDL, 0x53F8 },
    // Hand combos (also caught by OverrideLimbDraw but backup for other code paths)
    { gLinkAdultLeftHandHoldingMasterSwordNearDL, 0x5448 }, // DL_LFIST_SWORD1
    { gLinkAdultLeftHandHoldingMasterSwordFarDL, 0x5448 },
    { gLinkAdultLeftHandHoldingBgsNearDL, 0x5458 }, // DL_LFIST_SWORD3
    { gLinkAdultLeftHandHoldingBgsFarDL, 0x5458 },
    { gLinkAdultLeftHandHoldingHammerNearDL, 0x5460 }, // DL_LFIST_HAMMER
    { gLinkAdultLeftHandHoldingHammerFarDL, 0x5460 },
    { gLinkAdultRightHandHoldingBowNearDL, 0x5480 }, // DL_RFIST_BOW
    { gLinkAdultRightHandHoldingBowFarDL, 0x5480 },
    { gLinkAdultRightHandHoldingHookshotNearDL, 0x5488 }, // DL_RFIST_HOOKSHOT
    { gLinkAdultRightHandHoldingOotNearDL, 0x5490 },      // DL_RHAND_OCARINA_TIME
    { gLinkAdultRightHandHoldingOotFarDL, 0x5490 },
    { gLinkAdultRightHandHoldingHylianShieldNearDL, 0x5470 }, // DL_RFIST_SHIELD_2
    { gLinkAdultRightHandHoldingHylianShieldFarDL, 0x5470 },
    { gLinkAdultRightHandHoldingMirrorShieldNearDL, 0x5478 }, // DL_RFIST_SHIELD_3
    { gLinkAdultRightHandHoldingMirrorShieldFarDL, 0x5478 },
    // Plain hands (backup for code paths that bypass OverrideLimbDraw)
    { gLinkAdultLeftHandNearDL, 0x5098 }, // DL_LHAND
    { gLinkAdultLeftHandFarDL, 0x5098 },
    { gLinkAdultLeftHandClosedNearDL, 0x50A0 }, // DL_LFIST
    { gLinkAdultLeftHandClosedFarDL, 0x50A0 },
    { gLinkAdultLeftHandOutNearDL, 0x50A8 }, // DL_LHAND_BOTTLE
    { gLinkAdultRightHandNearDL, 0x50B0 },   // DL_RHAND
    { gLinkAdultRightHandFarDL, 0x50B0 },
    { gLinkAdultRightHandClosedNearDL, 0x50B8 }, // DL_RFIST
    { gLinkAdultRightHandClosedFarDL, 0x50B8 },
    // Broken Giant's Knife
    { gLinkAdultHandHoldingBrokenGiantsKnifeDL, 0x54F0 },
    { gLinkAdultHandHoldingBrokenGiantsKnifeFarDL, 0x54F0 },
    // Sentinel
    { NULL, 0 }
};

extern "C" Gfx* PakLoader_GetDLOverride(const char* otrPath) {
    if (!CVarGetInteger("gMods.PakLoader.Enabled", 0))
        return NULL;

    std::map<u32, Gfx*>* eqPtr = sGetEquipDLs();
    if (!eqPtr) return NULL;
    std::map<u32, Gfx*>& eq = *eqPtr;

    // Debug: log calls matching object_link_boy
    static u32 sHookCallCount = 0;
    if (sHookCallCount < 20 && strstr(otrPath, "object_link_boy") != NULL) {
        sHookCallCount++;
        PAK_LOG("GetDLOverride: %s", otrPath);
    }

    // Compare by string content since OTR paths may come from different arrays
    for (const OtrAliasEntry* e = sStandaloneOtrTable; e->otr != NULL; e++) {
        if (strcmp(otrPath, e->otr) == 0) {
            auto it = eq.find(e->alias);
            if (it != eq.end() && it->second != NULL && it->second != PAK_DL_STUB) {
                static u32 sOverrideLogCount = 0;
                if (sOverrideLogCount < 10) {
                    sOverrideLogCount++;
                    PAK_LOG("DL Override: %s → alias 0x%04X", otrPath, e->alias);
                }
                return it->second;
            }
            return NULL;
        }
    }
    return NULL;
}

// ============================================================================
// Eye/Mouth Texture Getters
// ============================================================================

// Eye/mouth texture offsets within zzplayas zobj (same as vanilla object_link_boy)
static const u32 sEyeTextureOffsets[] = { 0x0000, 0x0800, 0x1000, 0x1800, 0x2000, 0x2800, 0x3000, 0x3800 };
static const u32 sMouthTextureOffsets[] = { 0x4000, 0x4400, 0x4800, 0x4C00 };

static void* PakLoader_GetFaceTexture(const u32* offsets, s32 index, s32 maxIndex, u32 texSize) {
    if (sGetActiveIndex() < 0 || sGetActiveIndex() >= (s32)sModels.size())
        return NULL;
    if (!CVarGetInteger("gMods.PakLoader.Enabled", 0))
        return NULL;
    if (index < 0 || index > maxIndex)
        index = 0;

    PakModel& model = sModels[sGetActiveIndex()];
    u8 isAdult = (LINK_AGE_IN_YEARS == YEARS_ADULT);
    u8* zobjData = isAdult ? model.adultZobj : model.childZobj;
    u32 zobjSize = isAdult ? model.adultZobjSize : model.childZobjSize;
    u8 ready = isAdult ? model.adultReady : model.childReady;

    if (!ready || !zobjData)
        return NULL;

    u32 offset = offsets[index];
    if (offset + texSize > zobjSize)
        return NULL;

    // Check if face texture area has data (not all zeros)
    // Sample multiple bytes across the texture to detect non-zero content
    u8 hasData = 0;
    for (u32 j = offset; j < offset + texSize && j < zobjSize; j++) {
        if (zobjData[j] != 0) {
            hasData = 1;
            break;
        }
    }
    if (!hasData)
        return NULL; // All zeros = no custom texture, use vanilla
    return (void*)(zobjData + offset);
}

extern "C" void* PakLoader_GetEyeTexture(s32 eyeIndex) {
    return PakLoader_GetFaceTexture(sEyeTextureOffsets, eyeIndex, 7, 0x800);
}

extern "C" void* PakLoader_GetMouthTexture(s32 mouthIndex) {
    return PakLoader_GetFaceTexture(sMouthTextureOffsets, mouthIndex, 3, 0x400);
}

// ============================================================================
// Legacy draw callbacks (disabled - skeleton swap approach used instead)
// ============================================================================
#if 0
// Limb index → body part index mapping (same as OOT z_player_lib.c)
static const s8 sPakLimbToBodyPart[PAK_MAX_LIMBS] = {
    -1,                         // 0x00 PLAYER_LIMB_NONE
    -1,                         // 0x01 PLAYER_LIMB_ROOT
    PLAYER_BODYPART_WAIST,      // 0x02 PLAYER_LIMB_WAIST
    -1,                         // 0x03 PLAYER_LIMB_LOWER
    PLAYER_BODYPART_R_THIGH,    // 0x04 PLAYER_LIMB_R_THIGH
    PLAYER_BODYPART_R_SHIN,     // 0x05 PLAYER_LIMB_R_SHIN
    PLAYER_BODYPART_R_FOOT,     // 0x06 PLAYER_LIMB_R_FOOT
    PLAYER_BODYPART_L_THIGH,    // 0x07 PLAYER_LIMB_L_THIGH
    PLAYER_BODYPART_L_SHIN,     // 0x08 PLAYER_LIMB_L_SHIN
    PLAYER_BODYPART_L_FOOT,     // 0x09 PLAYER_LIMB_L_FOOT
    -1,                         // 0x0A PLAYER_LIMB_UPPER
    PLAYER_BODYPART_HEAD,       // 0x0B PLAYER_LIMB_HEAD
    PLAYER_BODYPART_HAT,        // 0x0C PLAYER_LIMB_HAT
    PLAYER_BODYPART_COLLAR,     // 0x0D PLAYER_LIMB_COLLAR
    PLAYER_BODYPART_L_SHOULDER, // 0x0E PLAYER_LIMB_L_SHOULDER
    PLAYER_BODYPART_L_FOREARM,  // 0x0F PLAYER_LIMB_L_FOREARM
    PLAYER_BODYPART_L_HAND,     // 0x10 PLAYER_LIMB_L_HAND
    PLAYER_BODYPART_R_SHOULDER, // 0x11 PLAYER_LIMB_R_SHOULDER
    PLAYER_BODYPART_R_FOREARM,  // 0x12 PLAYER_LIMB_R_FOREARM
    PLAYER_BODYPART_R_HAND,     // 0x13 PLAYER_LIMB_R_HAND
    PLAYER_BODYPART_SHEATH,     // 0x14 PLAYER_LIMB_SHEATH
    PLAYER_BODYPART_TORSO,      // 0x15 PLAYER_LIMB_TORSO
};

static s32 PakLoader_OverrideLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot,
                                       void* thisx) {
    // Don't override anything - let all limbs draw normally
    return false;
}

static void PakLoader_PostLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* thisx) {
    Player* player = (Player*)thisx;
    Vec3f zeroVec = { 0.0f, 0.0f, 0.0f };

    // Store limb world positions in bodyPartsPos
    if (limbIndex > 0 && limbIndex < PAK_MAX_LIMBS) {
        s8 bodyPart = sPakLimbToBodyPart[limbIndex];
        if (bodyPart >= 0) {
            Matrix_MultVec3f(&zeroVec, &player->bodyPartsPos[bodyPart]);
        }
    }

    // Update leftHandPos + carried actor support
    if (limbIndex == PLAYER_LIMB_L_HAND) {
        Matrix_MultVec3f(&zeroVec, &player->leftHandPos);

        if (player->actor.scale.y >= 0.0f) {
            Actor* heldActor = player->heldActor;

            if (!Player_HoldsHookshot(player) && (heldActor != NULL)) {
                if (player->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) {
                    MtxF carryMtx;
                    Vec3s carryRot;

                    Matrix_Get(&carryMtx);
                    Matrix_MtxFToYXZRotS(&carryMtx, &carryRot, 0);

                    if (heldActor->flags & ACTOR_FLAG_CARRY_X_ROT_INFLUENCE) {
                        heldActor->world.rot.x = heldActor->shape.rot.x = carryRot.x - player->unk_3BC.x;
                    } else {
                        heldActor->world.rot.y = heldActor->shape.rot.y =
                            player->actor.shape.rot.y + player->unk_3BC.y;
                    }
                }
            } else {
                Matrix_Get(&player->mf_9E0);
                Matrix_MtxFToYXZRotS(&player->mf_9E0, &player->unk_3BC, 0);
            }
        }
    }

    // Update focus.pos at HEAD (Navi tracking, Z-targeting)
    if (limbIndex == PLAYER_LIMB_HEAD) {
        Vec3f headOffset = { 1100.0f, -700.0f, 0.0f };
        Matrix_MultVec3f(&headOffset, &player->actor.focus.pos);
    }

    // Update feet positions (ground dust effects)
    if (limbIndex == PLAYER_LIMB_L_FOOT || limbIndex == PLAYER_LIMB_R_FOOT) {
        Actor_SetFeetPos(&player->actor, limbIndex, PLAYER_LIMB_L_FOOT, &zeroVec, PLAYER_LIMB_R_FOOT, &zeroVec);
    }
}

#endif // Legacy draw disabled

// ============================================================================
// Public API Implementation
// ============================================================================

extern "C" void PakLoader_Init(void) {
    if (sInitialized)
        return;

    // Don't try to init until Context is ready
    if (!Ship::Context::GetInstance())
        return;

    sInitialized = 1;
    PAK_LOG("Initializing...");

    std::vector<std::string> pakFiles;

    // Use the same method as SOH's mod_menu.cpp (line 134) to find the mods/ folder
    std::string modsPath = Ship::Context::LocateFileAcrossAppDirs("mods", appShortName);
    PAK_LOG("Mods path: %s", modsPath.c_str());

    if (!modsPath.empty() && std::filesystem::exists(modsPath) && std::filesystem::is_directory(modsPath)) {
        for (auto& entry : std::filesystem::recursive_directory_iterator(modsPath)) {
            if (entry.is_directory())
                continue;
            if (entry.path().extension() == ".pak") {
                pakFiles.push_back(entry.path().string());
                PAK_LOG("Found: %s", entry.path().string().c_str());
            }
        }
    }

    PAK_LOG("Found %d .pak files", (int)pakFiles.size());

    // Reserve space so push_back doesn't reallocate and invalidate internal pointers
    sModels.reserve(pakFiles.size());

    // Load each .pak model
    for (auto& pakPath : pakFiles) {
        PakModel model = {};
        model.pakPath = pakPath;
        snprintf(model.displayName, sizeof(model.displayName), "Unknown");

        if (LoadPakModel(model)) {
            sModels.push_back(std::move(model));
            // Fix up limbTable pointers after move (they pointed to the old struct)
            PakModel& m = sModels.back();
            for (s32 j = 0; j < PAK_MAX_LIMBS; j++) {
                if (m.adultLimbTable[j])
                    m.adultLimbTable[j] = &m.adultLimbs[j];
                if (m.childLimbTable[j])
                    m.childLimbTable[j] = &m.childLimbs[j];
            }
            PAK_LOG("Loaded: '%s' (adult=%d, child=%d)", m.displayName, m.adultReady, m.childReady);
        } else {
            // Free any allocated data
            if (model.adultZobj)
                free(model.adultZobj);
            if (model.childZobj)
                free(model.childZobj);
            PAK_LOG("Failed to load: %s", pakPath.c_str());
        }
    }

    PAK_LOG("Initialization complete: %d models available", (int)sModels.size());

    // Sanitize saved CVars to prevent out-of-range crashes
    s32 savedAdult = CVarGetInteger("gMods.PakLoader.AdultModel", -1);
    s32 savedChild = CVarGetInteger("gMods.PakLoader.ChildModel", -1);
    s32 savedEquip = CVarGetInteger("gMods.PakLoader.Equipment", -1);
    s32 count = (s32)sModels.size();

    if (savedAdult >= count || (savedAdult >= 0 && sModels[savedAdult].isEquipmentOnly)) {
        CVarSetInteger("gMods.PakLoader.AdultModel", -1);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    if (savedChild >= count || (savedChild >= 0 && sModels[savedChild].isEquipmentOnly)) {
        CVarSetInteger("gMods.PakLoader.ChildModel", -1);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    if (savedEquip >= count || (savedEquip >= 0 && !sModels[savedEquip].isEquipmentOnly)) {
        CVarSetInteger("gMods.PakLoader.Equipment", -1);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
}

// Auto-force model based on worn MM mask (called per-frame)
static void PakLoader_CheckMaskForce(void) {
    // Kafei Mask Transform
    if (CVarGetInteger("gMods.KafeiMaskTransform", 0)) {
        s32 wornMask = TransformMasks_WearGetCurrent();

        if (wornMask == ITEM_MM_MASK_KAFEI) {
            if (sForcedModelIndex < 0 || sForcedModelPath != "custom_items_resources/N64_Kafei.pak") {
                PakLoader_ForceModel("custom_items_resources/N64_Kafei.pak");
            }
        } else {
            if (sForcedModelIndex >= 0 && sForcedModelPath == "custom_items_resources/N64_Kafei.pak") {
                PakLoader_ClearForcedModel();
            }
        }
    } else {
        // CVar disabled — clear kafei forced model if active
        if (sForcedModelIndex >= 0 && sForcedModelPath == "custom_items_resources/N64_Kafei.pak") {
            PakLoader_ClearForcedModel();
        }
    }
}

extern "C" u8 PakLoader_HasActiveModel(void) {
    // Check if worn mask should auto-force a model
    PakLoader_CheckMaskForce();

    // Forced model bypasses the Enabled CVar
    if (sForcedModelIndex >= 0 && sForcedModelIndex < (s32)sModels.size()) {
        PakModel& fm = sModels[sForcedModelIndex];
        if (LINK_AGE_IN_YEARS == YEARS_ADULT && fm.adultReady) return 1;
        if (LINK_AGE_IN_YEARS != YEARS_ADULT && fm.childReady) return 1;
    }

    if (!CVarGetInteger("gMods.PakLoader.Enabled", 0)) {
        return 0;
    }

    // Check body model
    s32 bodyIdx = sGetActiveIndex();
    if (bodyIdx >= 0 && bodyIdx < (s32)sModels.size()) {
        PakModel& model = sModels[bodyIdx];
        if (LINK_AGE_IN_YEARS == YEARS_ADULT && model.adultReady) return 1;
        if (LINK_AGE_IN_YEARS != YEARS_ADULT && model.childReady) return 1;
    }

    // Check equipment pak (active even without body model)
    if (sSelectedEquipIndex >= 0 && sSelectedEquipIndex < (s32)sModels.size()) {
        return 1;
    }

    return 0;
}

#if 0  // Legacy DrawPlayer - disabled, using skeleton swap instead
extern "C" void PakLoader_DrawPlayer(PlayState* play, Player* player) {
    if (sGetActiveIndex() < 0 || sGetActiveIndex() >= (s32)sModels.size()) return;

    PakModel& model = sModels[sGetActiveIndex()];

    u8 isAdult = (LINK_AGE_IN_YEARS == YEARS_ADULT);
    void** skeleton;
    s32 dListCount;
    u8* zobjData;

    if (isAdult && model.adultReady) {
        skeleton = model.adultLimbTable;
        dListCount = model.adultFlexHeader.dListCount;
        zobjData = model.adultZobj;
    } else if (!isAdult && model.childReady) {
        skeleton = model.childLimbTable;
        dListCount = model.childFlexHeader.dListCount;
        zobjData = model.childZobj;
    } else {
        return; // No model for this age
    }

    OPEN_DISPS(play->state.gfxCtx);

    // Set segment 0x0C for backface culling (same as Player_DrawGameplay)
    gSPSegment(POLY_OPA_DISP++, 0x0C, (uintptr_t)gCullBackDList);
    gSPSegment(POLY_XLU_DISP++, 0x0C, (uintptr_t)gCullBackDList);

    // Override eye/mouth texture segments to point to zobj's embedded textures.
    // The zobj is a drop-in replacement for object_link_boy, so the same offsets apply.
    // Eye textures (CI8 64x32 = 0x800 bytes each):
    //   0x0000=open, 0x0800=half, 0x1000=closed, 0x1800=rollL, 0x2000=rollR,
    //   0x2800=shock, 0x3000=unk1, 0x3800=unk2
    // Mouth textures (CI8 32x32 = 0x400 bytes each):
    //   0x4000=mouth1, 0x4400=mouth2, 0x4800=mouth3, 0x4C00=mouth4
    {
        // Eye blink cycle (same as Player_DrawGameplay)
        static const u32 sEyeOffsets[] = {
            0x0000, 0x0800, 0x1000, 0x1800, 0x2000, 0x2800, 0x3000, 0x3800
        };
        static const u32 sMouthOffsets[] = {
            0x4000, 0x4400, 0x4800, 0x4C00
        };

        // Eye/mouth indices are packed in jointTable[22].x (same as Player_DrawImpl)
        s32 eyeIdx = (player->skelAnime.jointTable[22].x & 0xF) - 1;
        s32 mouthIdx = (player->skelAnime.jointTable[22].x >> 4) - 1;
        if (eyeIdx < 0 || eyeIdx > 7) eyeIdx = 0;
        if (mouthIdx < 0 || mouthIdx > 3) mouthIdx = 0;

        gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)(zobjData + sEyeOffsets[eyeIdx]));
        gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)(zobjData + sMouthOffsets[mouthIdx]));
    }

    // Draw using OOT's animation jointTable - compatible because same 21-limb hierarchy
    static u32 sDrawLogCount = 0;
    if (sDrawLogCount < 3) {
        sDrawLogCount++;
        s32 dlNonNull = 0;
        for (s32 li = 0; li < 21 && skeleton[li]; li++) {
            StandardLimb* limb = (StandardLimb*)skeleton[li];
            if (limb->dList) dlNonNull++;
        }
        // Show first limb that has a DL
        Gfx* firstDL = NULL;
        for (s32 li = 0; li < 21 && skeleton[li]; li++) {
            StandardLimb* limb = (StandardLimb*)skeleton[li];
            if (limb->dList) { firstDL = limb->dList; break; }
        }
        PAK_LOG("DrawPlayer: skeleton=%p, dListCount=%d, limbsWithDL=%d, firstDL=%p, jointTable=%p",
                (void*)skeleton, dListCount, dlNonNull, (void*)firstDL, (void*)player->skelAnime.jointTable);
    }
    SkelAnime_DrawFlexOpa(play, skeleton,
                          player->skelAnime.jointTable,
                          dListCount,
                          PakLoader_OverrideLimbDraw,
                          PakLoader_PostLimbDraw,
                          &player->actor);

    CLOSE_DISPS(play->state.gfxCtx);
}
#endif // Legacy DrawPlayer

extern "C" s32 PakLoader_GetModelCount(void) {
    if (!sInitialized) {
        PakLoader_Init();
    }
    return (s32)sModels.size();
}

extern "C" const char* PakLoader_GetModelName(s32 index) {
    if (index < 0 || index >= (s32)sModels.size())
        return NULL;
    return sModels[index].displayName;
}

extern "C" void PakLoader_SelectAdultModel(s32 index) {
    if (index < -1 || index >= (s32)sModels.size()) index = -1;
    if (index >= 0 && sModels[index].isEquipmentOnly) index = -1; // Can't use equipment pak as body
    if (index == sSelectedAdultIndex) return;
    sSelectedAdultIndex = index;
    if (index >= 0) {
        PAK_LOG("Selected adult model: '%s'", sModels[index].displayName);
    } else {
        PAK_LOG("Deselected adult model");
    }
}

extern "C" void PakLoader_SelectChildModel(s32 index) {
    if (index < -1 || index >= (s32)sModels.size()) index = -1;
    if (index >= 0 && sModels[index].isEquipmentOnly) index = -1;
    if (index == sSelectedChildIndex) return;
    sSelectedChildIndex = index;
    if (index >= 0) {
        PAK_LOG("Selected child model: '%s'", sModels[index].displayName);
    } else {
        PAK_LOG("Deselected child model");
    }
}

extern "C" s32 PakLoader_GetSelectedAdultIndex(void) { return sSelectedAdultIndex; }
extern "C" s32 PakLoader_GetSelectedChildIndex(void) { return sSelectedChildIndex; }

extern "C" u8 PakLoader_ModelHasAdult(s32 index) {
    if (index < 0 || index >= (s32)sModels.size()) return 0;
    if (sModels[index].isEquipmentOnly) return 0; // Equipment paks don't have body models
    return sModels[index].adultReady;
}

extern "C" u8 PakLoader_ModelHasChild(s32 index) {
    if (index < 0 || index >= (s32)sModels.size()) return 0;
    if (sModels[index].isEquipmentOnly) return 0;
    return sModels[index].childReady;
}

// Legacy: select for both ages
extern "C" void PakLoader_SelectModel(s32 index) {
    PakLoader_SelectAdultModel(index);
    PakLoader_SelectChildModel(index);
}

extern "C" s32 PakLoader_GetSelectedIndex(void) {
    return sGetActiveIndex();
}

extern "C" void PakLoader_SelectEquipment(s32 index) {
    if (index < -1 || index >= (s32)sModels.size()) index = -1;
    if (index == sSelectedEquipIndex) return;
    sSelectedEquipIndex = index;
    if (index >= 0) {
        PAK_LOG("Selected equipment: '%s'", sModels[index].displayName);
    } else {
        PAK_LOG("Deselected equipment");
    }
}

extern "C" s32 PakLoader_GetSelectedEquipIndex(void) { return sSelectedEquipIndex; }

extern "C" u8 PakLoader_ModelIsEquipmentOnly(s32 index) {
    if (index < 0 || index >= (s32)sModels.size()) return 0;
    return sModels[index].isEquipmentOnly;
}

// ============================================================================
// Forced Model API (for custom items like Kafei Mask)
// ============================================================================

// Forward declare LoadPakModel
static bool LoadPakModel(PakModel& model);

extern "C" void PakLoader_ForceModel(const char* pakPath) {
    if (!pakPath || !pakPath[0]) return;

    // Already forcing this same model?
    if (sForcedModelIndex >= 0 && sForcedModelPath == pakPath) return;

    // Check if this pak is already loaded in sModels
    for (s32 i = 0; i < (s32)sModels.size(); i++) {
        if (sModels[i].pakPath == pakPath) {
            sForcedModelIndex = i;
            sForcedModelPath = pakPath;
            PAK_LOG("Forced model (cached): '%s' (index %d)", sModels[i].displayName, i);
            return;
        }
    }

    // Lazy-load: parse and load the pak file
    if (!std::filesystem::exists(pakPath)) {
        PAK_LOG("Forced model file not found: %s", pakPath);
        return;
    }

    PakModel model = {};
    model.pakPath = pakPath;
    snprintf(model.displayName, sizeof(model.displayName), "Forced");

    if (LoadPakModel(model)) {
        sModels.push_back(std::move(model));
        sForcedModelIndex = (s32)sModels.size() - 1;
        sForcedModelPath = pakPath;

        // Fix up limb table pointers after move
        PakModel& m = sModels.back();
        for (s32 j = 0; j < PAK_MAX_LIMBS; j++) {
            if (m.adultLimbTable[j]) m.adultLimbTable[j] = &m.adultLimbs[j];
            if (m.childLimbTable[j]) m.childLimbTable[j] = &m.childLimbs[j];
        }

        PAK_LOG("Forced model loaded: '%s' (adult=%d, child=%d)",
                m.displayName, m.adultReady, m.childReady);
    } else {
        if (model.adultZobj) free(model.adultZobj);
        if (model.childZobj) free(model.childZobj);
        PAK_LOG("Failed to load forced model: %s", pakPath);
    }
}

extern "C" void PakLoader_ClearForcedModel(void) {
    if (sForcedModelIndex < 0) return;
    PAK_LOG("Cleared forced model");
    sForcedModelIndex = -1;
    sForcedModelPath.clear();
}

extern "C" u8 PakLoader_HasForcedModel(void) {
    return (sForcedModelIndex >= 0 && sForcedModelIndex < (s32)sModels.size()) ? 1 : 0;
}

extern "C" void PakLoader_Shutdown(void) {
    for (auto& model : sModels) {
        if (model.adultZobj) free(model.adultZobj);
        if (model.childZobj) free(model.childZobj);
    }
    sModels.clear();
    sSelectedAdultIndex = -1;
    sSelectedChildIndex = -1;
    sSelectedEquipIndex = -1;
    sForcedModelIndex = -1;
    sForcedModelPath.clear();
    sInitialized = 0;
}
