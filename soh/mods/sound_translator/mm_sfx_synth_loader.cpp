/*
 * mm_sfx_synth_loader.cpp — boot/asset wiring for the isolated MM SFX engine.
 *
 * Loads Sequence_0 + Soundfont_0/1 from mm.o2r via SoH's ResourceManager (the
 * structs are binary-compatible with the mmsfx ones, so we reinterpret_cast),
 * starts the SFX sequence on the isolated player, installs the per-channel
 * sfxState + the 0xBE freq/stereo custom function (ported verbatim from MM
 * code_8019AF00.c), and exposes MmSfxSynth_Init().
 *
 * This TU is the ONLY one that includes BOTH SoH audio headers and the mmsfx
 * headers — they coexist because every mmsfx type is namespaced.
 *
 * See memory: mm_oot_audio_engine_divergence.md
 */
#include <libultraship/libultraship.h>
#include <libultraship/log/luslog.h>
#include "z64audio.h"               // SoH SoundFont / SequenceData (global scope)
#include "soh/ResourceManagerHelpers.h"
#include "mods/transformation_masks/assets/mm_asset_loader.h" // MmAssets_IsAvailable

#include "mm_sfx_synth_ctx.h"
#include "mm_sfx_synth_common.h"
#include "mm_sfx_synth.h"

#include <cstring>

#define MMSYN_LOG(fmt, ...) lusprintf(__FILE__, __LINE__, LUSLOG_LEVEL_INFO, "[MmSfxSynth] " fmt, ##__VA_ARGS__)

// Log hook so the audio-thread backend (which has no SoH headers) can report
// how far it gets — invaluable for locating an audio-thread crash.
extern "C" void MmSfxSynth_Log(const char* msg) {
    lusprintf(__FILE__, __LINE__, LUSLOG_LEVEL_INFO, "[MmSfxSynth] %s", msg);
}

// Patches a freshly (re)loaded MM SoundFont's sample pointers to mm.o2r's
// binary (ResourceMgr can hand back stale OOT sample pointers). Provided by
// mm_asset_loader.cpp.
extern "C" void MmSfxBridge_PatchFontSamples(::SoundFont* sf, const char* path);

namespace mmsfx {

void MmSfxSynth_InitEngine(void);     // glue.cpp
void MmSfxSynth_MarkReady(bool);      // backend.cpp

// ---- per-channel SFX state (read by the 0xBE custom function) -------------
SfxChannelState sSfxChannelState[SEQ_NUM_CHANNELS];

// Custom seq function (slot 0). VERBATIM from MM code_8019AF00.c:4118 — copies
// the per-channel freqScale + stereo bits from sSfxChannelState onto the live
// channel when the SFX sequence requests it (opcode 0xBE 0x00).
static u32 AudioSfx_SetFreqAndStereoBits(s8 seqScriptValIn, SequenceChannel* channel) {
    u8 idx = (u8)seqScriptValIn;
    channel->stereoData.asByte = sSfxChannelState[idx].stereoBits;
    channel->freqScale = sSfxChannelState[idx].freqScale;
    channel->changes.s.freqScale = true;
    return (u32)(u8)seqScriptValIn;
}

// VERBATIM intent from MM AudioSfx_ResetSfxChannelState (code_8019AF00.c:4126).
static void AudioSfx_ResetSfxChannelState(void) {
    for (s32 i = 0; i < SEQ_NUM_CHANNELS; i++) {
        sSfxChannelState[i].volume = 1.0f;
        sSfxChannelState[i].freqScale = 1.0f;
        sSfxChannelState[i].reverb = 0;
        sSfxChannelState[i].panSigned = 0x40;
        sSfxChannelState[i].stereoBits = 0;
        sSfxChannelState[i].filter = 0xFF;
        sSfxChannelState[i].combFilterGain = 0xFF;
        sSfxChannelState[i].zVolume = 0xFF;
    }
}

// Start the resident Sequence_0 on the isolated SFX player (player 0). Mirrors
// the essential state MM's INIT_SEQPLAYER sets; the seq's own script sets tempo
// and enables its channels on the first update.
void AudioScript_ResetSequencePlayer(SequencePlayer* seqPlayer); // seqplayer.cpp
static void StartSfxSequence(u8* seqData) {
    SequencePlayer* sp = &gMmSfx.seqPlayers[0];
    AudioScript_ResetSequencePlayer(sp);

    sp->seqData = seqData;
    sp->enabled = true;
    sp->finished = false;
    sp->scriptState.depth = 0;
    sp->scriptState.pc = seqData;
    sp->delay = 0;
    sp->fadeTimer = 0;
    sp->fadeVolume = 1.0f;
    sp->fadeVelocity = 0.0f;
    sp->fadeVolumeScale = 1.0f;
    sp->appliedFadeVolume = 1.0f;
    sp->portVolumeScale = 1.0f;
    sp->recalculateVolume = true;
    sp->state = SEQPLAYER_STATE_0;
    sp->scriptCounter = 0;
    sp->defaultFont = 0; // Soundfont_0 — avoid OOB soundFontList[] before the seq sets a font
    if (sp->tempo == 0) {
        sp->tempo = 120 * TATUMS_PER_BEAT; // fallback until the seq sets it
    }

    // point every SFX channel at its sfxState + install custom function slot 0
    for (s32 i = 0; i < SEQ_NUM_CHANNELS; i++) {
        SequenceChannel* ch = sp->channels[i];
        if (ch != &gMmSfx.sequenceChannelNone) {
            ch->sfxState = (u8*)&sSfxChannelState[i];
            ch->fontId = 0; // default to Soundfont_0 until the seq selects one
        }
    }
    gMmSfx.customSeqFunctions[0] = &AudioSfx_SetFreqAndStereoBits;
}

// Two-slot MM SoundFont table (font 0 + font 1), indexed by fontId.
static SoundFont sFontTable[2];
static bool sLoaderReady = false;

} // namespace mmsfx

extern "C" int MmSfxSynth_IsReady(void) {
    return mmsfx::sLoaderReady ? 1 : 0;
}

extern "C" int MmSfxSynth_Init(void) {
    using namespace mmsfx;
    if (sLoaderReady) return 1;
    if (!MmAssets_IsAvailable()) return 0;

    MMSYN_LOG("Init: booting isolated engine...");

    // 1) engine pools / players / notes
    MmSfxSynth_InitEngine();
    MMSYN_LOG("Init: engine pools OK (numNotes=%d)", gMmSfx.numNotes);

    // 2) soundfonts 0 and 1 (binary-compatible -> reinterpret into mmsfx)
    static const char* kFontPaths[2] = {
        "audio/fonts/Soundfont_0",
        "audio/fonts/Soundfont_1",
    };
    for (s32 f = 0; f < 2; f++) {
        ::SoundFont* sf = ResourceMgr_LoadAudioSoundFontByName(kFontPaths[f]);
        if (sf == nullptr) {
            MMSYN_LOG("Init: font %d (%s) not ready — retry next call", f, kFontPaths[f]);
            return 0; // assets not ready yet; retry next call
        }
        MmSfxBridge_PatchFontSamples(sf, kFontPaths[f]);
        sFontTable[f] = *reinterpret_cast<mmsfx::SoundFont*>(sf);
        MMSYN_LOG("Init: font %d loaded (inst=%d drums=%d sfx=%d)", f, sFontTable[f].numInstruments,
                  sFontTable[f].numDrums, sFontTable[f].numSfx);
    }
    gMmSfx.soundFontList = sFontTable;

    // 3) the SFX sequence program (Sequence_0)
    SequenceData* sd = ResourceMgr_LoadSeqPtrByName("audio/sequences/Sequence_0");
    if (sd == nullptr || sd->seqData == nullptr) {
        MMSYN_LOG("Init: Sequence_0 not ready — retry next call");
        return 0;
    }
    MMSYN_LOG("Init: Sequence_0 loaded (seqData=%p size=%d numFonts=%d)", (void*)sd->seqData, sd->seqDataSize,
              sd->numFonts);

    // 4) install sfx state + start the sequence
    AudioSfx_ResetSfxChannelState();
    StartSfxSequence((u8*)sd->seqData);

    sLoaderReady = true;
    MmSfxSynth_MarkReady(true);
    MMSYN_LOG("Init: COMPLETE — engine ready, sequence started on player 0");
    return 1;
}
