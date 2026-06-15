/**
 * voice_pack.cpp - Z64Online-style voice pack loader.
 *
 * Scans mods/ for ModLoader64 .pak archives containing sounds/<HEX>/*.ogg
 * directories. The hex directory name is the OOT NA_SE_VO_LI_* sfxId; each
 * directory may contain multiple OGGs as variants and one is picked at random
 * when Link triggers that voice id.
 *
 * Decoding is lazy on Select — at most one pack's PCM is resident at a time.
 *
 * Mixer follows the Pikachu single-voice pattern (pikachu_form.cpp:53-99)
 * generalized to 4 slots with atomic-publish so the audio thread can read
 * slot state without a lock.
 */

#include <libultraship/libultra.h>
#include <ship/Context.h> // Ship::Context was transitively via OTRGlobals.h before upstream #6636 cleanup

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <ctype.h>
#include <filesystem>
#include <map>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#include <zlib.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>

#include <spdlog/spdlog.h>

#include "voice_pack.h"
#include "z64.h"
#include "soh/OTRGlobals.h"

// ============================================================================
// Logging
// ============================================================================

#define VP_LOG(fmt, ...)                                                      \
    do {                                                                      \
        char _vpbuf[512];                                                     \
        snprintf(_vpbuf, sizeof(_vpbuf), "[VoicePack] " fmt, ##__VA_ARGS__);  \
        SPDLOG_INFO("{}", _vpbuf);                                            \
    } while (0)

// ============================================================================
// ModLoader64 PAK parser (slim copy of pak_loader.cpp:212)
// ============================================================================

static inline u32 BE_U32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

struct VPakEntry {
    std::string name;
    u32 dataStart;
    u32 dataEnd;
    bool compressed;
};

static bool VPak_Parse(const std::string& pakPath, std::vector<VPakEntry>& entries, std::vector<u8>& outFileData) {
    FILE* f = fopen(pakPath.c_str(), "rb");
    if (!f)
        return false;

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileSize < 16) {
        fclose(f);
        return false;
    }

    outFileData.resize(fileSize);
    if (fread(outFileData.data(), 1, fileSize, f) != (size_t)fileSize) {
        fclose(f);
        return false;
    }
    fclose(f);

    if (memcmp(outFileData.data(), "ModLoader64", 11) != 0) {
        return false;
    }

    u32 startPos = 12;
    while (startPos + 16 <= (u32)fileSize && memcmp(outFileData.data() + startPos, "UNCO", 4) != 0 &&
           memcmp(outFileData.data() + startPos, "DEFL", 4) != 0) {
        startPos++;
    }

    std::vector<std::tuple<u32, u32, u32, bool>> rawEntries;
    for (u32 pos = startPos; pos + 16 <= (u32)fileSize; pos += 16) {
        bool isUnco = memcmp(outFileData.data() + pos, "UNCO", 4) == 0;
        bool isDefl = memcmp(outFileData.data() + pos, "DEFL", 4) == 0;
        if (!isUnco && !isDefl)
            break;

        u32 nameOff = BE_U32(outFileData.data() + pos + 4);
        u32 dataStart = BE_U32(outFileData.data() + pos + 8);
        u32 dataEnd = BE_U32(outFileData.data() + pos + 12);
        if (nameOff < (u32)fileSize && dataStart < (u32)fileSize && dataEnd <= (u32)fileSize) {
            rawEntries.push_back({ nameOff, dataStart, dataEnd, isDefl });
        }
    }

    if (rawEntries.empty())
        return false;

    for (auto& [nameOff, dataStart, dataEnd, isCompressed] : rawEntries) {
        std::string name;
        for (u32 i = nameOff; i < (u32)fileSize; i++) {
            u8 c = outFileData[i];
            if (c == 0xFF || c == 0x00)
                break;
            name += (char)c;
        }
        VPakEntry e;
        e.name = name;
        e.dataStart = dataStart;
        e.dataEnd = dataEnd;
        e.compressed = isCompressed;
        entries.push_back(e);
    }
    return true;
}

static bool VPak_Extract(const VPakEntry& e, const std::vector<u8>& fileData, std::vector<u8>& out) {
    u32 size = e.dataEnd - e.dataStart;
    if (size == 0 || e.dataStart + size > fileData.size())
        return false;
    if (e.compressed) {
        uLongf decompSize = (uLongf)size * 8 + 64;
        out.resize(decompSize);
        int ret;
        // Retry with growing buffer if zlib reports buffer too small.
        for (int attempt = 0; attempt < 6; attempt++) {
            decompSize = (uLongf)out.size();
            ret = uncompress(out.data(), &decompSize, fileData.data() + e.dataStart, size);
            if (ret == Z_OK) {
                out.resize(decompSize);
                return true;
            }
            if (ret != Z_BUF_ERROR)
                return false;
            out.resize(out.size() * 2);
        }
        return false;
    } else {
        out.assign(fileData.data() + e.dataStart, fileData.data() + e.dataStart + size);
        return true;
    }
}

// ============================================================================
// Vorbis-from-memory callbacks (copied from AudioSampleFactory.cpp:24-88)
// ============================================================================

struct OggFileData {
    void* data;
    size_t pos;
    size_t size;
};

static size_t VorbisReadCallback(void* out, size_t size, size_t elems, void* src) {
    OggFileData* d = static_cast<OggFileData*>(src);
    size_t toRead = size * elems;
    if (toRead > d->size - d->pos)
        toRead = d->size - d->pos;
    memcpy(out, static_cast<uint8_t*>(d->data) + d->pos, toRead);
    d->pos += toRead;
    return toRead / size;
}

static int VorbisSeekCallback(void* src, ogg_int64_t pos, int whence) {
    OggFileData* d = static_cast<OggFileData*>(src);
    size_t newPos;
    switch (whence) {
        case SEEK_SET: newPos = (size_t)pos; break;
        case SEEK_CUR: newPos = d->pos + (size_t)pos; break;
        case SEEK_END: newPos = d->size + (size_t)pos; break;
        default: return -1;
    }
    if (newPos > d->size)
        return -1;
    d->pos = newPos;
    return 0;
}

static int VorbisCloseCallback(void* /*src*/) {
    return 0;
}

static long VorbisTellCallback(void* src) {
    OggFileData* d = static_cast<OggFileData*>(src);
    return (long)d->pos;
}

static const ov_callbacks vorbisCallbacks = {
    VorbisReadCallback, VorbisSeekCallback, VorbisCloseCallback, VorbisTellCallback,
};

// Decode an OGG Vorbis byte buffer into mono s16 PCM at its source sample rate.
// Stereo input is mixed to mono by averaging L+R per sample.
static bool DecodeOggToMonoPcm(const u8* oggData, size_t oggSize, std::vector<s16>& outPcm, u32& outRate) {
    OggFileData fileData = { (void*)oggData, 0, oggSize };
    OggVorbis_File vf;
    if (ov_open_callbacks(&fileData, &vf, nullptr, 0, vorbisCallbacks) < 0)
        return false;

    vorbis_info* vi = ov_info(&vf, -1);
    if (!vi) {
        ov_clear(&vf);
        return false;
    }
    int channels = vi->channels;
    outRate = (u32)vi->rate;

    // Read in 4 KB chunks, signed 16, native LE.
    char buffer[4096];
    int bitstream = 0;
    std::vector<s16> raw;
    for (;;) {
        long n = ov_read(&vf, buffer, sizeof(buffer), 0, 2, 1, &bitstream);
        if (n == 0)
            break;
        if (n < 0) {
            ov_clear(&vf);
            return false;
        }
        size_t numS16 = (size_t)n / 2;
        size_t base = raw.size();
        raw.resize(base + numS16);
        memcpy(raw.data() + base, buffer, (size_t)n);
    }
    ov_clear(&vf);

    if (raw.empty())
        return false;

    if (channels <= 1) {
        outPcm = std::move(raw);
    } else {
        // Down-mix to mono (average across channels)
        size_t frames = raw.size() / channels;
        outPcm.resize(frames);
        for (size_t i = 0; i < frames; i++) {
            s32 acc = 0;
            for (int c = 0; c < channels; c++) {
                acc += (s32)raw[i * channels + c];
            }
            outPcm[i] = (s16)(acc / channels);
        }
    }
    return true;
}

// ============================================================================
// State
// ============================================================================

struct VoiceSample {
    std::vector<s16> pcm;
    u32 rate;
};

struct VoicePack {
    std::string path;
    std::string displayName;
    bool decoded;
    // sfxId -> list of variant samples
    std::map<u16, std::vector<VoiceSample>> samples;
    // Pre-scan list of OGG entry indices (entry index in `entries`) keyed by sfxId,
    // so lazy decode on Select doesn't have to re-parse the pak header.
    std::map<u16, std::vector<size_t>> oggEntryByHex;
};

static std::vector<VoicePack> sPacks;
static std::set<std::string> sClaimedPaths;
static s32 sActiveIdx = -1;
static u8 sInitialized = 0;

#define VOICE_SLOT_COUNT 4

struct VoiceSlot {
    const s16* data;
    u32 len;
    f32 fracPos;
    f32 step;
    f32 vol;
    std::atomic<uint8_t> playing;
};

static VoiceSlot sSlots[VOICE_SLOT_COUNT];

static std::mt19937 sRng{ 0x53767069 };

// ============================================================================
// Hex parsing
// ============================================================================

// Parse a string as hex (no 0x prefix). Returns true on success and stores the
// value in *out. Allows any case; rejects empty/non-hex strings.
static bool ParseHex16(const std::string& s, u16* out) {
    if (s.empty() || s.size() > 4)
        return false;
    u32 v = 0;
    for (char c : s) {
        v <<= 4;
        if (c >= '0' && c <= '9')      v |= (u32)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (u32)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (u32)(c - 'A' + 10);
        else return false;
    }
    if (v > 0xFFFF)
        return false;
    *out = (u16)v;
    return true;
}

// Match an entry name that contains ".../sounds/<HEX>/<file>.ogg".
// The "sounds" segment can appear anywhere in the path (real ModLoader64 paks
// often nest the entries under the mod name or under "assets/", e.g.
// "MyMod/sounds/6800/voice01.ogg"). Both '/' and '\' are accepted as
// separators, since paks built on Windows occasionally use backslashes.
//
// Returns true and fills *outHexId on match.
static bool MatchSoundsEntry(const std::string& name, u16* outHexId) {
    if (name.size() < 12) // minimum: "sounds/0/a.ogg"
        return false;

    auto isSep = [](char c) { return c == '/' || c == '\\'; };
    auto eqi = [](char a, char b) {
        return tolower((unsigned char)a) == tolower((unsigned char)b);
    };

    // Find "sounds" as a path component (preceded by start-of-string or a
    // separator, and immediately followed by a separator).
    const char* kw = "sounds";
    const size_t kwLen = 6;
    size_t soundsEnd = std::string::npos; // index of separator AFTER "sounds"
    for (size_t i = 0; i + kwLen < name.size(); i++) {
        if (i > 0 && !isSep(name[i - 1]))
            continue;
        bool match = true;
        for (size_t j = 0; j < kwLen; j++) {
            if (!eqi(name[i + j], kw[j])) {
                match = false;
                break;
            }
        }
        if (!match)
            continue;
        if (!isSep(name[i + kwLen]))
            continue;
        soundsEnd = i + kwLen; // points at the separator
        break;
    }
    if (soundsEnd == std::string::npos)
        return false;

    // After "sounds<sep>", expect <HEX><sep><file>.ogg
    size_t hexStart = soundsEnd + 1;
    size_t hexEnd = std::string::npos;
    for (size_t i = hexStart; i < name.size(); i++) {
        if (isSep(name[i])) {
            hexEnd = i;
            break;
        }
    }
    if (hexEnd == std::string::npos || hexEnd == hexStart)
        return false;

    std::string hexPart = name.substr(hexStart, hexEnd - hexStart);
    if (!ParseHex16(hexPart, outHexId))
        return false;

    // Must end in .ogg (case-insensitive)
    if (name.size() < 4)
        return false;
    std::string tail = name.substr(name.size() - 4);
    for (char& c : tail) c = (char)tolower((unsigned char)c);
    return tail == ".ogg";
}

// ============================================================================
// Pack scanning (no decode)
// ============================================================================

// Returns substring value of a JSON `"name"` field (very permissive, like
// pak_loader's JsonFindString). Empty if not present.
static std::string FindJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find("\"", pos + search.size());
    if (pos == std::string::npos) return "";
    pos++;
    size_t end = json.find("\"", pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

static bool ScanOnePak(const std::string& pakPath, VoicePack& outPack) {
    std::vector<VPakEntry> entries;
    std::vector<u8> fileData;
    if (!VPak_Parse(pakPath, entries, fileData))
        return false;

    // Find any sounds/<hex>/*.ogg entries
    bool anyVoice = false;
    for (size_t i = 0; i < entries.size(); i++) {
        u16 hexId;
        if (MatchSoundsEntry(entries[i].name, &hexId)) {
            outPack.oggEntryByHex[hexId].push_back(i);
            anyVoice = true;
        }
    }
    if (!anyVoice) {
        // Diagnostic: dump the first few entry names so we can see what format
        // the pak actually uses if our matcher rejected everything. This makes
        // it possible to spot voice paks built with non-standard layouts
        // without re-running with a debugger.
        const size_t kSample = 6;
        size_t shown = entries.size() < kSample ? entries.size() : kSample;
        for (size_t i = 0; i < shown; i++) {
            VP_LOG("  no-voice in '%s': entry[%zu]='%s'",
                   std::filesystem::path(pakPath).filename().string().c_str(),
                   i, entries[i].name.c_str());
        }
        return false;
    }

    // Try to read display name from package.json
    std::string displayName;
    for (auto& e : entries) {
        if (e.name.find("package.json") != std::string::npos) {
            std::vector<u8> jsonBuf;
            if (VPak_Extract(e, fileData, jsonBuf)) {
                std::string json((char*)jsonBuf.data(), jsonBuf.size());
                displayName = FindJsonString(json, "name");
            }
            break;
        }
    }
    if (displayName.empty()) {
        displayName = std::filesystem::path(pakPath).stem().string();
    }

    outPack.path = pakPath;
    outPack.displayName = displayName;
    outPack.decoded = false;
    return true;
}

// ============================================================================
// Lazy decode on Select
// ============================================================================

static bool DecodePack(VoicePack& pack) {
    std::vector<VPakEntry> entries;
    std::vector<u8> fileData;
    if (!VPak_Parse(pack.path, entries, fileData)) {
        VP_LOG("Decode failed: cannot reparse '%s'", pack.path.c_str());
        return false;
    }

    s32 totalSamples = 0;
    s32 totalBytes = 0;
    for (auto& [hexId, entryIndices] : pack.oggEntryByHex) {
        std::vector<VoiceSample>& bucket = pack.samples[hexId];
        for (size_t idx : entryIndices) {
            if (idx >= entries.size())
                continue;
            std::vector<u8> oggBytes;
            if (!VPak_Extract(entries[idx], fileData, oggBytes))
                continue;
            VoiceSample s{};
            if (!DecodeOggToMonoPcm(oggBytes.data(), oggBytes.size(), s.pcm, s.rate)) {
                VP_LOG("OGG decode failed for sfxId=0x%04X entry='%s'", hexId, entries[idx].name.c_str());
                continue;
            }
            totalBytes += (s32)(s.pcm.size() * sizeof(s16));
            totalSamples++;
            bucket.push_back(std::move(s));
        }
    }

    pack.decoded = true;
    VP_LOG("Decoded '%s': %d samples (%d sfxIds), %d KB PCM",
           pack.displayName.c_str(), totalSamples, (int)pack.samples.size(), totalBytes / 1024);
    return totalSamples > 0;
}

// ============================================================================
// Mixer (audio thread side)
// ============================================================================

extern "C" void VoicePack_MixInto(s16* outBuf, u32 numSamples) {
    if (!sInitialized || !outBuf)
        return;

    // SoH master volume (gSettings.Volume.Master, 0-100, default 40)
    f32 masterVol = (f32)CVarGetInteger("gSettings.Volume.Master", 40) / 100.0f;
    // Voice pack mix gain (defaults to 1.0)
    f32 voiceVol = CVarGetFloat("gMods.VoicePack.Volume", 1.0f);
    f32 globalGain = masterVol * voiceVol;

    for (s32 s = 0; s < VOICE_SLOT_COUNT; s++) {
        VoiceSlot& slot = sSlots[s];
        if (slot.playing.load(std::memory_order_acquire) == 0)
            continue;
        if (!slot.data || slot.len == 0) {
            slot.playing.store(0, std::memory_order_release);
            continue;
        }

        f32 gain = slot.vol * globalGain;
        for (u32 i = 0; i < numSamples; i++) {
            u32 idx = (u32)slot.fracPos;
            if (idx >= slot.len) {
                slot.playing.store(0, std::memory_order_release);
                break;
            }
            s32 sample = (s32)((f32)slot.data[idx] * gain);
            s32 mL = (s32)outBuf[i * 2] + sample;
            s32 mR = (s32)outBuf[i * 2 + 1] + sample;
            outBuf[i * 2]     = (mL > 32767) ? 32767 : (mL < -32768) ? -32768 : (s16)mL;
            outBuf[i * 2 + 1] = (mR > 32767) ? 32767 : (mR < -32768) ? -32768 : (s16)mR;
            slot.fracPos += slot.step;
        }
    }
}

// ============================================================================
// Trigger (game thread side) — atomic-publish slot
// ============================================================================

extern "C" u8 VoicePack_PlayIfMatch(u16 sfxId, Vec3f* /*pos*/) {
    if (!sInitialized) return 0;
    if (!CVarGetInteger("gMods.VoicePack.Enabled", 0)) return 0;
    if (sActiveIdx < 0 || sActiveIdx >= (s32)sPacks.size()) return 0;

    VoicePack& pack = sPacks[sActiveIdx];
    if (!pack.decoded) return 0;

    auto it = pack.samples.find(sfxId);
    if (it == pack.samples.end() || it->second.empty())
        return 0;

    // Pick a random variant
    const std::vector<VoiceSample>& variants = it->second;
    std::uniform_int_distribution<size_t> dist(0, variants.size() - 1);
    const VoiceSample& chosen = variants[dist(sRng)];

    // Find a free slot
    s32 freeSlot = -1;
    for (s32 i = 0; i < VOICE_SLOT_COUNT; i++) {
        if (sSlots[i].playing.load(std::memory_order_acquire) == 0) {
            freeSlot = i;
            break;
        }
    }
    if (freeSlot < 0) {
        // All slots busy — steal slot 0 (oldest assumption)
        freeSlot = 0;
        sSlots[0].playing.store(0, std::memory_order_release);
    }

    VoiceSlot& slot = sSlots[freeSlot];
    slot.data    = chosen.pcm.data();
    slot.len     = (u32)chosen.pcm.size();
    slot.fracPos = 0.0f;
    slot.step    = (f32)chosen.rate / 32000.0f;
    slot.vol     = 1.0f;
    // Publish last so the audio thread either sees the new playback fully set up
    // or still sees playing=0 from a previous frame.
    slot.playing.store(1, std::memory_order_release);

    return 1;
}

// ============================================================================
// Public API
// ============================================================================

extern "C" s32 VoicePack_GetCount(void) {
    if (!sInitialized) VoicePack_Init();
    return (s32)sPacks.size();
}

extern "C" const char* VoicePack_GetName(s32 index) {
    if (index < 0 || index >= (s32)sPacks.size())
        return nullptr;
    return sPacks[index].displayName.c_str();
}

extern "C" s32 VoicePack_GetSelectedIndex(void) {
    return sActiveIdx;
}

extern "C" int VoicePack_OwnsPath(const char* path) {
    if (!path) return 0;
    return sClaimedPaths.count(path) ? 1 : 0;
}

// Stop all currently playing voice slots (so changing pack mid-clip cleans up).
static void StopAllSlots(void) {
    for (s32 i = 0; i < VOICE_SLOT_COUNT; i++) {
        sSlots[i].playing.store(0, std::memory_order_release);
        sSlots[i].data = nullptr;
        sSlots[i].len  = 0;
    }
}

extern "C" void VoicePack_Select(s32 index) {
    if (index < -1 || index >= (s32)sPacks.size())
        index = -1;
    if (index == sActiveIdx)
        return;

    StopAllSlots();
    sActiveIdx = index;

    if (index < 0) {
        VP_LOG("Deselected voice pack");
        return;
    }
    VoicePack& p = sPacks[index];
    if (!p.decoded) {
        VP_LOG("Selecting '%s' — decoding now...", p.displayName.c_str());
        DecodePack(p);
    } else {
        VP_LOG("Selected '%s' (already decoded)", p.displayName.c_str());
    }
}

extern "C" void VoicePack_Init(void) {
    if (sInitialized)
        return;
    if (!Ship::Context::GetInstance())
        return;

    sInitialized = 1;
    VP_LOG("Initializing...");

    // Reset slot state
    for (s32 i = 0; i < VOICE_SLOT_COUNT; i++) {
        sSlots[i].playing.store(0);
        sSlots[i].data = nullptr;
        sSlots[i].len = 0;
    }

    std::string modsPath = Ship::Context::LocateFileAcrossAppDirs("mods", appShortName);
    if (modsPath.empty() || !std::filesystem::exists(modsPath) || !std::filesystem::is_directory(modsPath)) {
        VP_LOG("No mods/ directory found at '%s'", modsPath.c_str());
        return;
    }

    s32 candidates = 0;
    for (auto& entry : std::filesystem::directory_iterator(modsPath)) {
        if (entry.is_directory()) continue;
        if (entry.path().extension() != ".pak") continue;

        candidates++;
        VoicePack pack{};
        if (ScanOnePak(entry.path().string(), pack)) {
            sClaimedPaths.insert(pack.path);
            VP_LOG("Found voice pack: '%s' (%d sfxIds, %s)",
                   pack.displayName.c_str(), (int)pack.oggEntryByHex.size(), pack.path.c_str());
            sPacks.push_back(std::move(pack));
        }
    }

    VP_LOG("Init complete: scanned %d .pak files, found %d voice packs", candidates, (int)sPacks.size());

    // Sanitize saved selection
    s32 saved = CVarGetInteger("gMods.VoicePack.Selection", -1);
    if (saved >= (s32)sPacks.size()) {
        CVarSetInteger("gMods.VoicePack.Selection", -1);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    // Lazy-select if enabled at startup
    if (CVarGetInteger("gMods.VoicePack.Enabled", 0)) {
        VoicePack_Select(CVarGetInteger("gMods.VoicePack.Selection", -1));
    }
}

extern "C" void VoicePack_Shutdown(void) {
    StopAllSlots();
    sPacks.clear();
    sClaimedPaths.clear();
    sActiveIdx = -1;
    sInitialized = 0;
}
