/**
 * @file mm_bgm_loader.cpp
 * @brief MM BGM Sequence Loader — scans mm.o2r and registers MM seqs into SOH audio.
 *
 * Mirrors the custom-seq pipeline in soh/src/code/audio_load.c (lines ~1402-1442).
 * One-shot: first call walks the MM archive for fonts + sequences, registers each
 * with the SOH audio engine, and builds a name→seqId map. Subsequent calls no-op.
 *
 * Fail-quiet by design: if mm.o2r is not mounted, the module disables itself and
 * every public call becomes silent. There is NO OOT BGM fallback — the user
 * explicitly forbade that for the transformation_masks audio system.
 */

#include "mm_bgm_loader.h"
#include "mm_bgm_names.h"

#include "mods/transformation_masks/assets/mm_asset_loader.h"
#include <libultraship/libultraship.h>
#include <libultraship/log/luslog.h>
#include "z64audio.h"
#include "functions.h"
#include "variables.h" // for gAudioContext (seqToPlay/seqReplaced side-channel)
#include "soh/Enhancements/audio/AudioCollection.h"
#include "soh/ResourceManagerHelpers.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

#define MMBGM_LOG(fmt, ...) lusprintf(__FILE__, __LINE__, LUSLOG_LEVEL_INFO, fmt, ##__VA_ARGS__)

// Bridge from mm_asset_loader.cpp — patches every sample pointer in `sf` to a
// SoundFontSample loaded from sMmArchive. Required after re-loading an MM
// SoundFont via the global ResourceManager (which can hand back stale OOT
// sample pointers despite the path resolving to mm.o2r's binary).
extern "C" void MmSfxBridge_PatchFontSamples(SoundFont* sf, const char* path);

namespace {

bool sRegistered = false;
bool sRegistrationStarted = false;
std::unordered_map<std::string, uint16_t> sNameToId;

// Remap: MM ROM's original SoundFont index (as baked into mm.o2r's .seq binary
// `fonts[]` field by OTRExporter:AudioExporter.cpp:370) -> SoH-side fontMap
// slot we assigned when registering the MM font.
//
// 2Ship's exporter writes each MM seq's `numFonts` + `fonts[k]` verbatim from
// the ROM (audio->fontIndices[i][k]). Those indices reference MM's font table
// — not SoH's. Without this remap, an MM seq that says "fonts[0] = 25" would
// look up fontMap[25] in SoH and get OOT's Soundfont_25 (or NULL if absent).
std::unordered_map<uint8_t, uint8_t> sMmFontIndexMap;

// Strip "audio/sequences/" prefix and any extension so the name is just the
// raw sequence basename (e.g. "Sequence_83").
std::string ExtractBgmName(const std::string& path) {
    size_t slash = path.find_last_of('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) {
        name = name.substr(0, dot);
    }
    return name;
}

void RegisterMmFonts() {
    int count = 0;
    char** files = MmAssets_ListMmArchiveFiles("audio/fonts*", &count);
    if (files == nullptr || count == 0) {
        if (files) free(files);
        MMBGM_LOG("[MmBgm] No audio/fonts* found in mm.o2r — skipping font registration");
        return;
    }

    int registered = 0;
    int aliased = 0;
    for (int i = 0; i < count; i++) {
        const char* path = files[i];
        if (path == nullptr) continue;

        // CRITICAL: the global ResourceManager cache holds OOT's SoundFont
        // content for paths like `audio/fonts/Soundfont_6` (cached during
        // AudioLoad_Init at boot, before mm.o2r was mounted). Without
        // explicit eviction, every subsequent ResourceMgr_LoadAudioSoundFontByName
        // returns OOT's stale content for that path. Drop the cached entry
        // so the next load resolves through the archive manager (last-wins
        // = mm.o2r) and rebuilds the SoundFont from MM's binary.
        //
        // Verified crash 0xc0000005 in audio_synthesis.c:905 / mixer.c:103
        // (aLoadBufferImpl memcpy) when an MM seq referenced a font whose
        // global-cached SoundFont still pointed at OOT instruments with
        // garbage MM sample pointers.
        ResourceUnloadByName(path);

        // Now re-load — global cache miss → AddArchive last-wins resolves to
        // mm.o2r → MM's SoundFont binary parsed → cached as the active version.
        SoundFont* sf = ResourceMgr_LoadAudioSoundFontByName(path);
        if (sf == nullptr) {
            MMBGM_LOG("[MmBgm] Failed to load MM SoundFont '%s' after unload", path);
            continue;
        }
        u8 mmOriginalIdx = sf->fntIndex;

        // Even after re-loading from MM, the SoundFont's per-instrument sample
        // pointers may still point at OOT samples — when AudioSoundFontFactory
        // parsed MM's binary, every sample path was loaded via the global
        // ResourceManager which returns the OOT-cached version for shared
        // sample names. Patch each sample pointer in-place to a sample loaded
        // directly from sMmArchive. Mirrors what MmSfx_LoadFont does
        // internally for the SFX path (mm_asset_loader.cpp:1371).
        MmSfxBridge_PatchFontSamples(sf, path);

        // Is this path already mapped to a SoH fontMap slot? (OOT extractions
        // commonly use the same `audio/fonts/Soundfont_N` naming.) If so, reuse
        // that slot — last-wins resource lookup will still hand back MM content
        // when the engine resolves fontMap[slot] -> path -> SoundFont resource.
        s32 sohIdx = -1;
        for (size_t j = 0; j < fontMapSize; j++) {
            if (fontMap[j] != nullptr && strcmp(fontMap[j], path) == 0) {
                sohIdx = (s32)j;
                break;
            }
        }

        if (sohIdx < 0) {
            // MM-only font (no OOT slot). Allocate a fresh SoH index.
            sohIdx = AudioLoad_FindNextFreeFontIndex();
            if (AudioLoad_RegisterMmFont(path, sohIdx) < 0) {
                MMBGM_LOG("[MmBgm] Failed to grow fontMap for '%s'", path);
                continue;
            }
            sf->fntIndex = (u8)sohIdx;
            // CRITICAL: populate gAudioContext.soundFonts[sohIdx] so the audio
            // synth thread can resolve instruments[]/drums[]/soundEffects[]
            // without going OOB. Without this, mixer.c:103 aLoadBufferImpl
            // memcpy's from a garbage sampleAddr → access violation crash.
            AudioLoad_PopulateMmFontMeta(sohIdx, sf);
            registered++;
        } else {
            aliased++;
        }

        sMmFontIndexMap[mmOriginalIdx] = (u8)sohIdx;
        MMBGM_LOG("[MmBgm] DIAG: font '%s' MM_orig=%u -> SoH=%d", path, mmOriginalIdx, sohIdx);
    }

    for (int i = 0; i < count; i++) {
        if (files[i]) free(files[i]);
    }
    free(files);

    MMBGM_LOG("[MmBgm] Registered %d new MM soundfonts + %d aliased to existing OOT slots (scanned %d)",
              registered, aliased, count);
}

void RegisterMmSequencesInternal() {
    int count = 0;
    char** files = MmAssets_ListMmArchiveFiles("audio/sequences*", &count);
    if (files == nullptr || count == 0) {
        if (files) free(files);
        MMBGM_LOG("[MmBgm] DIAG: 'audio/sequences*' returned 0 entries. Trying broader patterns...");

        // Diagnostic fallbacks — log alt patterns so the user can see what's
        // actually in mm.o2r. Common alternatives: trailing slash, no glob, etc.
        const char* probes[] = { "audio/sequences/", "audio/sequence*", "*sequence*", "audio*" };
        for (size_t p = 0; p < sizeof(probes) / sizeof(probes[0]); p++) {
            int n = 0;
            char** files2 = MmAssets_ListMmArchiveFiles(probes[p], &n);
            MMBGM_LOG("[MmBgm] DIAG: pattern '%s' -> %d hits", probes[p], n);
            if (files2 != nullptr) {
                for (int i = 0; i < n && i < 5; i++) {
                    if (files2[i]) MMBGM_LOG("[MmBgm] DIAG:   [%d] '%s'", i, files2[i]);
                }
                for (int i = 0; i < n; i++) {
                    if (files2[i]) free(files2[i]);
                }
                free(files2);
            }
        }
        return;
    }

    // Diagnostic: dump the first few sequence paths so we can confirm the
    // expected filenames (Sequence_83 for Bremen March, Sequence_113 for Kamaro).
    MMBGM_LOG("[MmBgm] DIAG: Found %d audio/sequences* entries:", count);
    for (int i = 0; i < count && i < 10; i++) {
        if (files[i]) MMBGM_LOG("[MmBgm] DIAG:   [%d] '%s'", i, files[i]);
    }
    if (count > 10) MMBGM_LOG("[MmBgm] DIAG:   ... and %d more", count - 10);

    int registered = 0;
    for (int i = 0; i < count; i++) {
        const char* path = files[i];
        if (path == nullptr) continue;

        SequenceData* sDat = ResourceMgr_LoadSeqPtrByName(path);
        if (sDat == nullptr) {
            MMBGM_LOG("[MmBgm] Failed to load SequenceData for '%s'", path);
            continue;
        }

        // Diagnostic — capture the BINARY's original seqNumber BEFORE we
        // overwrite it. This tells us the MM ROM seq index that file holds.
        // For files of interest (Bremen/GetSong/etc), this confirms whether
        // the file content matches the file label, or if 2Ship's XML naming
        // is swapped vs the actual ROM data.
        u8 originalSeqNumber = sDat->seqNumber;
        if (strstr(path, "BremenMarch") != nullptr || strstr(path, "GetSong") != nullptr ||
            strstr(path, "LearnedNewSong") != nullptr || strstr(path, "_52") != nullptr ||
            strstr(path, "_53") != nullptr || strstr(path, "Kamaro") != nullptr ||
            strstr(path, "_71") != nullptr) {
            MMBGM_LOG("[MmBgm] DIAG: '%s' BINARY seqNumber=0x%02X (ROM index this file's binary identifies as)",
                      path, originalSeqNumber);
        }

        // Two cases produced by OTRExporter::WriteSequenceBinary:
        //   numFonts == -1  -> font referenced by CRC (custom seqs)
        //   numFonts >= 0   -> font[] holds MM ROM's original font indices
        //                       (verbatim from ZAudio::fontIndices, see
        //                        AudioExporter.cpp:370-371)
        if (sDat->numFonts == -1) {
            uint64_t crc;
            memcpy(&crc, sDat->fonts, sizeof(uint64_t));
            const char* res = ResourceGetNameByCrc(crc);
            if (res == nullptr) {
                MMBGM_LOG("[MmBgm] Could not find soundfont (CRC 0x%llx) for sequence '%s'",
                          (unsigned long long)crc, path);
                continue;
            }
            SoundFont* sf = ResourceMgr_LoadAudioSoundFontByName(res);
            if (sf == nullptr) {
                MMBGM_LOG("[MmBgm] Resolved font name '%s' but load failed for sequence '%s'", res, path);
                continue;
            }
            memset(&sDat->fonts[0], 0, sizeof(sDat->fonts));
            sDat->fonts[0] = sf->fntIndex;
            sDat->numFonts = 1;
        } else if (sDat->numFonts > 0) {
            // Remap each MM ROM font index to SoH's fontMap slot.
            // Without this, AudioLoad_GetFontsForSequence would hand SoH the
            // wrong fontMap row (or NULL) at playback time.
            for (s32 k = 0; k < sDat->numFonts; k++) {
                auto it = sMmFontIndexMap.find(sDat->fonts[k]);
                if (it != sMmFontIndexMap.end()) {
                    u8 oldIdx = sDat->fonts[k];
                    sDat->fonts[k] = it->second;
                    MMBGM_LOG("[MmBgm] DIAG: '%s' fonts[%d] remap MM_orig=%u -> SoH=%u",
                              path, k, oldIdx, it->second);
                } else {
                    MMBGM_LOG("[MmBgm] WARN: '%s' fonts[%d]=%u — no MM->SoH font remap available; "
                              "playback will use existing fontMap slot which may be wrong",
                              path, k, sDat->fonts[k]);
                }
            }

            // Diagnostic: for sequences of interest (Bremen/Kamaro), log which
            // fontMap slot the seq will ACTUALLY resolve to at playback time
            // — i.e. the path AudioLoad will hand to ResourceMgr when it
            // builds the synth voice. If this is wrong (points at an OOT
            // soundfont instead of MM's expected one), the BGM will play but
            // with the wrong instruments → "wrong song" symptom.
            if (strstr(path, "BremenMarch") != nullptr || strstr(path, "GetSong") != nullptr ||
                strstr(path, "LearnedNewSong") != nullptr || strstr(path, "_52") != nullptr ||
                strstr(path, "_53") != nullptr || strstr(path, "Kamaro") != nullptr ||
                strstr(path, "_71") != nullptr) {
                u8 finalFont = sDat->fonts[0];
                const char* fontPath = (finalFont < fontMapSize && fontMap[finalFont] != nullptr)
                                           ? fontMap[finalFont]
                                           : "(invalid)";
                MMBGM_LOG("[MmBgm] DIAG: '%s' will play with fontMap[%u]='%s'",
                          path, finalFont, fontPath);
            }
        }

        u16 seqNum = (u16)AudioLoad_FindNextFreeSeqId();
        if (!AudioLoad_RegisterMmSequence(path, seqNum)) {
            MMBGM_LOG("[MmBgm] Failed to register sequence '%s'", path);
            continue;
        }
        sDat->seqNumber = seqNum;

        sNameToId[ExtractBgmName(path)] = seqNum;
        registered++;
    }

    for (int i = 0; i < count; i++) {
        if (files[i]) free(files[i]);
    }
    free(files);

    MMBGM_LOG("[MmBgm] Registered %d MM sequences (scanned %d)", registered, count);
}

void EnsureRegistered() {
    if (sRegistered || sRegistrationStarted) return;

    // Triggers MmAssets_Init() lazily if it hasn't run yet.
    if (!MmAssets_IsAvailable()) {
        return; // silent — mm.o2r genuinely missing
    }
    if (!MmAssets_IsLoaded()) {
        MMBGM_LOG("[MmBgm] DIAG: MmAssets_IsAvailable=1 but IsLoaded=0 — mm.o2r detected but not mounted yet");
        return;
    }
    if (sequenceMap == nullptr || fontMap == nullptr) {
        MMBGM_LOG("[MmBgm] DIAG: sequenceMap=%p fontMap=%p — audio engine not initialized yet",
                  (void*)sequenceMap, (void*)fontMap);
        return;
    }

    MMBGM_LOG("[MmBgm] DIAG: Starting MM seq registration (sequenceMapSize=%zu, fontMapSize=%zu)",
              sequenceMapSize, fontMapSize);
    sRegistrationStarted = true;
    RegisterMmFonts();
    RegisterMmSequencesInternal();
    sRegistered = !sNameToId.empty();
    MMBGM_LOG("[MmBgm] DIAG: Registration done. sRegistered=%d, names=%zu", sRegistered, sNameToId.size());
}

} // namespace

extern "C" {

void MmBgm_RegisterSequences(void) {
    EnsureRegistered();
}

s32 MmBgm_IsAvailable(void) {
    EnsureRegistered();
    return sRegistered ? 1 : 0;
}

u16 MmBgm_GetSeqId(const char* mmBgmName) {
    if (mmBgmName == nullptr) return 0xFFFF;
    EnsureRegistered();
    if (!sRegistered) return 0xFFFF;
    auto it = sNameToId.find(std::string(mmBgmName));
    return (it != sNameToId.end()) ? it->second : 0xFFFF;
}

// Pre-set the 16-bit seqToPlay side-channel so Audio_QueueSeqCmd's patched
// bypass (see code_800F9280.c) carries our full MM seq ID through to
// AudioLoad_SyncInitSeqPlayerInternal — which honors seqReplaced[playerIdx]
// and pulls the 16-bit ID from seqToPlay[]. Without this, `cmd & 0xFF`
// truncates IDs >0xFF and the audio engine plays whatever vanilla OOT seq
// happens to occupy that 8-bit slot (e.g. 0x16C → NA_BGM_TIMED_MINI_GAME).
//
// Uses Audio_PrimeMmSideChannel (one-shot flag) so the bypass is consumed
// by the next QueueSeqCmd and never shadows the custom/music/* randomizer's
// own writes to seqReplaced/seqToPlay.
static void MmBgm_PrimeSideChannel(u8 playerIdx, u16 fullSeqId) {
    Audio_PrimeMmSideChannel(playerIdx, fullSeqId);
}

void MmBgm_PlayFanfare(const char* mmBgmName) {
    u16 id = MmBgm_GetSeqId(mmBgmName);
    if (id == 0xFFFF) return;
    MmBgm_PrimeSideChannel(/*SEQ_PLAYER_FANFARE=*/ 1, id);
    Audio_PlayFanfare(id);
}

void MmBgm_PlayMain(const char* mmBgmName) {
    u16 id = MmBgm_GetSeqId(mmBgmName);
    if (id == 0xFFFF) return;
    MmBgm_PrimeSideChannel(/*SEQ_PLAYER_BGM_MAIN=*/ 0, id);
    // SEQ_PLAYER_BGM_MAIN = 0. Format: (op << 28) | (seqPlayer << 24) | (seq & 0xFFFF).
    // The low 8 bits of `id` may be garbage when truncated, but the side-channel
    // primed above carries the full 16-bit ID through.
    Audio_QueueSeqCmd((u32)id & 0xFFFFu);
}

void MmBgm_StopFanfare(void) {
    // SEQ_PLAYER_FANFARE = 1. NA_BGM_STOP = 0x100000FF (op=stop, seq=0xFF).
    Audio_QueueSeqCmd(NA_BGM_STOP | (1u << 24));
}

} // extern "C"
