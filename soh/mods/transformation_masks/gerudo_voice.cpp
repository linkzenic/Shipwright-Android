/**
 * gerudo_voice.cpp — see header for design rationale.
 *
 * Architecture mirrors soh/mods/voice_pack/voice_pack.cpp (4 atomic-published
 * slots, 32 kHz mix rate, lazy decode-on-init). Differences:
 *   * Source archive is the standard SoH resource manager (so it pulls from
 *     gerudo.o2r without us touching the .pak path manually).
 *   * Single implicit "pack" — no menu, no random selection across packs.
 *   * Trigger is gated externally by the Player_PlayVoiceSfx caller checking
 *     GerudoForm_IsActive(), so PlayIfMatch can stay assumption-free.
 *   * Path prefix is `voice/` (vs voice_pack's `sounds/`) to avoid colliding
 *     if anyone ever ships an OoT-format pack that bundles a Gerudo voice.
 */

#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>

#include <spdlog/spdlog.h>

#include "gerudo_voice.h"
#include "z64.h"
#include <libultraship/bridge.h> // CVarGet*/CVarSet* — was transitive via OTRGlobals.h before upstream #6636
#include <libultraship/libultraship.h> // full Ship::ResourceManager (GetArchiveManager) — was transitive via OTRGlobals.h before #6636
#include "soh/OTRGlobals.h"
#include "soh/ResourceManagerHelpers.h"
#include <ship/Context.h>
#include <ship/resource/archive/ArchiveManager.h>

// ============================================================================
// OGG Vorbis decode — read raw OGG bytes, output mono s16 PCM
// (Copied verbatim from voice_pack.cpp:174-264. Identical algorithm — keeping
//  a local copy avoids a cross-module dependency on voice_pack internals.)
// ============================================================================

struct OggFileData {
    void* data;
    size_t pos;
    size_t size;
};

static size_t VorbisReadCallback(void* out, size_t size, size_t elems, void* src) {
    OggFileData* d = static_cast<OggFileData*>(src);
    size_t toRead = size * elems;
    if (toRead > d->size - d->pos) {
        toRead = d->size - d->pos;
    }
    memcpy(out, (uint8_t*)d->data + d->pos, toRead);
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
    if (newPos > d->size) return -1;
    d->pos = newPos;
    return 0;
}

static int VorbisCloseCallback(void* /*src*/) { return 0; }
static long VorbisTellCallback(void* src) {
    return (long)static_cast<OggFileData*>(src)->pos;
}

static const ov_callbacks vorbisCallbacks = {
    VorbisReadCallback, VorbisSeekCallback, VorbisCloseCallback, VorbisTellCallback,
};

static bool DecodeOggToMonoPcm(const uint8_t* oggData, size_t oggSize,
                               std::vector<int16_t>& outPcm, uint32_t& outRate) {
    OggFileData d = { (void*)oggData, 0, oggSize };
    OggVorbis_File vf;
    if (ov_open_callbacks(&d, &vf, nullptr, 0, vorbisCallbacks) < 0) return false;
    vorbis_info* vi = ov_info(&vf, -1);
    if (!vi) {
        ov_clear(&vf);
        return false;
    }
    int channels = vi->channels;
    outRate = (uint32_t)vi->rate;
    char buf[4096];
    int bs = 0;
    std::vector<int16_t> raw;
    for (;;) {
        long n = ov_read(&vf, buf, sizeof(buf), 0, 2, 1, &bs);
        if (n == 0) break;
        if (n < 0) {
            ov_clear(&vf);
            return false;
        }
        size_t numS16 = (size_t)n / 2;
        size_t base = raw.size();
        raw.resize(base + numS16);
        memcpy(raw.data() + base, buf, (size_t)n);
    }
    ov_clear(&vf);
    if (raw.empty()) return false;

    if (channels <= 1) {
        outPcm = std::move(raw);
    } else {
        size_t frames = raw.size() / channels;
        outPcm.resize(frames);
        for (size_t i = 0; i < frames; i++) {
            int32_t acc = 0;
            for (int c = 0; c < channels; c++) acc += (int32_t)raw[i * channels + c];
            outPcm[i] = (int16_t)(acc / channels);
        }
    }
    return true;
}

// ============================================================================
// State
// ============================================================================

struct GerudoSample {
    std::vector<int16_t> pcm;
    uint32_t rate;
};

// sfxId -> variants
static std::map<uint16_t, std::vector<GerudoSample>> sSamples;
static std::atomic<uint8_t> sInitialized{ 0 };

#define GV_SLOT_COUNT 4

struct GVoiceSlot {
    const int16_t* data;
    uint32_t len;
    float fracPos;
    float step;
    float vol;
    std::atomic<uint8_t> playing;
};

static GVoiceSlot sSlots[GV_SLOT_COUNT];
static std::mt19937 sRng{ 0x47657275 /* 'Geru' */ };

// ============================================================================
// Hex parsing — extract sfx id from "voice/<HEX>/<file>.ogg"
// ============================================================================

static bool ParseSfxIdFromPath(const std::string& path, uint16_t* outId) {
    // Expect: voice/<4-hex>/<file>.ogg
    // We're permissive: any "voice/<X>/..." where <X> parses as hex works.
    const std::string prefix = "voice/";
    if (path.size() < prefix.size() + 5) return false;
    if (path.compare(0, prefix.size(), prefix) != 0) return false;
    size_t hexStart = prefix.size();
    size_t hexEnd = path.find('/', hexStart);
    if (hexEnd == std::string::npos || hexEnd == hexStart) return false;
    std::string hex = path.substr(hexStart, hexEnd - hexStart);
    if (hex.size() > 4) return false;
    uint32_t v = 0;
    for (char c : hex) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
        else return false;
    }
    if (v > 0xFFFF) return false;
    *outId = (uint16_t)v;
    return true;
}

// ============================================================================
// Init — scan + decode
// ============================================================================

static void DoInit() {
    auto ctx = Ship::Context::GetRawInstance();
    if (!ctx) return;
    auto rm = ctx->GetResourceManager();
    if (!rm) return;
    auto am = rm->GetArchiveManager();
    if (!am) return;

    int count = 0;
    char** list = ResourceMgr_ListFiles("voice/*", &count);
    if (list == nullptr || count == 0) {
        SPDLOG_INFO("[GerudoVoice] no voice/* entries in any archive");
        return;
    }

    int decoded = 0;
    for (int i = 0; i < count; i++) {
        std::string path = list[i];
        uint16_t sfxId = 0;
        if (!ParseSfxIdFromPath(path, &sfxId)) continue;
        auto file = am->LoadFile(path);
        if (!file || !file->Buffer || file->Buffer->empty()) continue;
        GerudoSample s{};
        if (!DecodeOggToMonoPcm((const uint8_t*)file->Buffer->data(), file->Buffer->size(),
                                s.pcm, s.rate)) {
            continue;
        }
        sSamples[sfxId].push_back(std::move(s));
        decoded++;
    }
    // Free the list (allocated by ResourceMgr_ListFiles)
    for (int i = 0; i < count; i++) free(list[i]);
    free(list);

    SPDLOG_INFO("[GerudoVoice] decoded {} samples across {} sfx slots", decoded, (int)sSamples.size());
}

extern "C" void GerudoVoice_Init(void) {
    if (sInitialized.load(std::memory_order_acquire)) return;
    for (int i = 0; i < GV_SLOT_COUNT; i++) {
        sSlots[i].playing.store(0);
        sSlots[i].data = nullptr;
        sSlots[i].len = 0;
    }
    DoInit();
    sInitialized.store(1, std::memory_order_release);
}

extern "C" void GerudoVoice_Shutdown(void) {
    for (int i = 0; i < GV_SLOT_COUNT; i++) {
        sSlots[i].playing.store(0, std::memory_order_release);
    }
    sSamples.clear();
    sInitialized.store(0, std::memory_order_release);
}

// ============================================================================
// PlayIfMatch — game thread
// ============================================================================

extern "C" u8 GerudoVoice_PlayIfMatch(u16 sfxId, Vec3f* /*pos*/) {
    if (!sInitialized.load(std::memory_order_acquire)) {
        GerudoVoice_Init();
    }
    auto it = sSamples.find(sfxId);
    if (it == sSamples.end() || it->second.empty()) return 0;

    // Pick a random variant
    auto& variants = it->second;
    std::uniform_int_distribution<size_t> dist(0, variants.size() - 1);
    const GerudoSample& chosen = variants[dist(sRng)];

    // Find a free slot; if none, skip this voice (do NOT steal slot 0).
    int freeSlot = -1;
    for (int i = 0; i < GV_SLOT_COUNT; i++) {
        if (sSlots[i].playing.load(std::memory_order_acquire) == 0) {
            freeSlot = i;
            break;
        }
    }
    if (freeSlot < 0) {
        // All slots busy — skip instead of stealing. Stealing slot 0 (store
        // playing=0 then overwrite data/len/step) raced the mixer mid-read on the
        // audio thread → use-after-free / OOB of the previous sample's pcm.
        // Dropping the new voice removes the race with no locking.
        return 0;
    }

    GVoiceSlot& s = sSlots[freeSlot];
    s.data = chosen.pcm.data();
    s.len = (uint32_t)chosen.pcm.size();
    s.fracPos = 0.0f;
    // Mix rate is 32 kHz; step = source_rate / 32000 keeps playback at real speed.
    s.step = (float)chosen.rate / 32000.0f;
    s.vol = 1.0f;
    s.playing.store(1, std::memory_order_release);  // publish last
    return 1;
}

// ============================================================================
// MixInto — audio thread
// ============================================================================

extern "C" void GerudoVoice_MixInto(s16* outBuf, u32 numSamples) {
    if (!sInitialized.load(std::memory_order_acquire) || !outBuf) return;

    float masterVol = (float)CVarGetInteger("gSettings.Volume.Master", 40) / 100.0f;
    float voiceVol = CVarGetFloat("gSettings.Volume.SFX", 1.0f);
    float globalGain = masterVol * voiceVol;

    for (int sl = 0; sl < GV_SLOT_COUNT; sl++) {
        GVoiceSlot& slot = sSlots[sl];
        if (slot.playing.load(std::memory_order_acquire) == 0) continue;
        if (!slot.data || slot.len == 0) {
            slot.playing.store(0, std::memory_order_release);
            continue;
        }
        float gain = slot.vol * globalGain;
        for (u32 i = 0; i < numSamples; i++) {
            uint32_t idx = (uint32_t)slot.fracPos;
            if (idx >= slot.len) {
                slot.playing.store(0, std::memory_order_release);
                break;
            }
            int32_t sample = (int32_t)((float)slot.data[idx] * gain);
            int32_t mL = (int32_t)outBuf[i * 2]     + sample;
            int32_t mR = (int32_t)outBuf[i * 2 + 1] + sample;
            outBuf[i * 2]     = (mL > 32767) ? 32767 : (mL < -32768) ? -32768 : (s16)mL;
            outBuf[i * 2 + 1] = (mR > 32767) ? 32767 : (mR < -32768) ? -32768 : (s16)mR;
            slot.fracPos += slot.step;
        }
    }
}
