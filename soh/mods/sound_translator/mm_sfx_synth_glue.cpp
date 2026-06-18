/*
 * mm_sfx_synth_glue.cpp — the host/glue TU for the isolated MM SFX synth.
 *
 * Defines the isolated audio context instance `gMmSfx`, the simplified heap/
 * load/thread analogues the ported MM lib calls (everything is preloaded into
 * RAM, so no DMA / async loading is needed), the small glue-provided data
 * objects playback.c expects, and MmSfxSynth_InitEngine() which replicates the
 * relevant subset of MM's AudioHeap_Init (heap.c:1040-1073) using the init
 * routines that live inside the ported seqplayer/playback TUs.
 *
 * See memory: mm_oot_audio_engine_divergence.md
 */
#include "mm_sfx_synth_ctx.h"
#include "mm_sfx_synth_common.h"
#include <cstring>

namespace mmsfx {

// ===========================================================================
// Context instance + globals
// ===========================================================================
AudioContext gMmSfx;
AudioCustomSeqFunction gAudioCustomSeqFunction = nullptr;

// Default ADSR envelope (VERBATIM from MM data.c:854; big-endian like font
// envelopes since effects.c reads it via BE16SWAP). Used as the fallback
// envelope for channels/layers that don't specify one.
EnvelopePoint gDefaultEnvelope[] = {
    { BE16SWAP_CONST(1), BE16SWAP_CONST(32000) },
    { BE16SWAP_CONST(1000), BE16SWAP_CONST(32000) },
    { BE16SWAP_CONST(ADSR_HANG), BE16SWAP_CONST(0) },
    { BE16SWAP_CONST(ADSR_DISABLE), BE16SWAP_CONST(0) },
};

// glue-provided data consumed by playback.c
u8 gHaasEffectDelaySize[64];          // zeros -> no Haas stereo widening (acceptable stub)
NoteSampleState gDefaultSampleState;  // zero-init default (verify against MM if a SFX needs it)
NoteSampleState gZeroedSampleState;   // all zeros

// load-status sentinel (matches seqplayer.cpp's local LOAD_STATUS_COMPLETE == 2)
#define MMSFX_LOAD_STATUS_COMPLETE 2

// ===========================================================================
// Static backing storage (heap-allocated in MM; static here, bump-allocated)
// ===========================================================================
#define MMSFX_NUM_NOTES 32
#define MMSFX_UPDATES_PER_FRAME 3
#define MMSFX_MISC_POOL_SIZE (512 * 1024)

static u8 sMiscPool[MMSFX_MISC_POOL_SIZE];
static u8* sMiscCur = sMiscPool;
static u8 sFontLoadStatus[256];

// ===========================================================================
// Heap analogues — a trivial bump allocator over sMiscPool (one-shot at init)
// ===========================================================================
void* AudioHeap_AllocZeroed(AudioAllocPool* pool, u32 size) {
    (void)pool;
    size = (size + 0xF) & ~0xFu; // 16-byte align
    if (sMiscCur + size > sMiscPool + sizeof(sMiscPool)) {
        return nullptr; // pool exhausted — bump MMSFX_MISC_POOL_SIZE if this trips
    }
    void* p = sMiscCur;
    sMiscCur += size;
    memset(p, 0, size);
    return p;
}

void* AudioHeap_AllocDmaMemory(void* pool, u32 size) {
    return AudioHeap_AllocZeroed((AudioAllocPool*)pool, size);
}

// Fonts are always resident -> report "found/loaded" (truthy). The return is
// only used as a boolean by the seq script (can this font be selected?).
void* AudioHeap_SearchCaches(s32 tableType, s32 cache, s32 id) {
    (void)tableType; (void)cache;
    return (void*)(uintptr_t)(id + 1);
}

// SFX rarely uses the lowpass/highpass filter opcode; leave the filter buffer
// untouched (no filtering). TODO: port AudioHeap_LoadFilter if a filtered SFX
// proves audibly wrong.
void AudioHeap_LoadFilter(s16* filter, s32 lowPassCutoff, s32 highPassCutoff) {
    (void)filter; (void)lowPassCutoff; (void)highPassCutoff;
}

// ===========================================================================
// Load analogues — everything is preloaded, so loads are instantly "complete"
// ===========================================================================
s32 AudioLoad_IsSeqLoadComplete(s32 seqId) { (void)seqId; return 1; }
s32 AudioLoad_IsFontLoadComplete(s32 fontId) { (void)fontId; return 1; }
void AudioLoad_SetSeqLoadStatus(s32 seqId, s32 status) { (void)seqId; (void)status; }
void AudioLoad_SetFontLoadStatus(s32 fontId, s32 status) { (void)fontId; (void)status; }

s32 AudioLoad_SlowLoadSample(s32 fontId, s8 instId, s8* isDone) {
    (void)fontId; (void)instId;
    if (isDone) *isDone = MMSFX_LOAD_STATUS_COMPLETE;
    return 0;
}
void AudioLoad_SlowLoadSeq(s32 seqId, u8* ramAddr, s8* isDone) {
    (void)seqId; (void)ramAddr;
    if (isDone) *isDone = MMSFX_LOAD_STATUS_COMPLETE;
}
void AudioLoad_ScriptLoad(s32 tableType, s32 id, s8* isDone) {
    (void)tableType; (void)id;
    if (isDone) *isDone = MMSFX_LOAD_STATUS_COMPLETE;
}

// Re-init a seq player to run the already-resident sequence. seqData must have
// been set on the player by the loader before this is meaningful; the engine's
// own AudioScript_ResetSequencePlayer + the loader handle the real start.
void AudioLoad_SyncInitSeqPlayer(s32 playerIndex, s32 seqId, s32 arg2) {
    (void)seqId; (void)arg2;
    if (playerIndex >= 0 && playerIndex < 5) {
        // Left intentionally minimal — MmSfxSynth_StartSequence() (loader) does
        // the actual pc/enabled setup against the resident Sequence_0 data.
    }
}

// ===========================================================================
// Thread analogue — pseudo-random (MM's AudioThread_NextRandom mixes a counter
// and the OS timer; an xorshift is behaviorally adequate for SFX variance).
// ===========================================================================
static u32 sRandomState = 0x12345678u;
u32 AudioThread_NextRandom(void) {
    u32 x = sRandomState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    sRandomState = x;
    return x;
}

AudioBufferParameters* MmSfx_GetBufParams(void) {
    return &gMmSfx.audioBufferParameters;
}

// ===========================================================================
// External init routines that live inside the ported TUs
// ===========================================================================
void AudioPlayback_NoteInitAll(void);          // playback.cpp
void AudioPlayback_InitNoteFreeList(void);     // playback.cpp
void AudioScript_InitSequencePlayers(void);    // seqplayer.cpp
void AudioScript_InitSequencePlayerChannels(s32 seqPlayerIndex); // seqplayer.cpp
void AudioScript_ResetSequencePlayer(SequencePlayer* seqPlayer); // seqplayer.cpp

// ===========================================================================
// Engine init — mirrors AudioHeap_Init (heap.c:940-1073), minus DMA/RSP/cache.
// ===========================================================================
void MmSfxSynth_InitEngine(void) {
    memset(&gMmSfx, 0, sizeof(gMmSfx));
    sMiscCur = sMiscPool;

    // CRITICAL: MM's gDefaultSampleState has bitField0.enabled=TRUE, needsInit=TRUE
    // (data.c:863). AudioPlayback_NoteInit copies it into every note's sampleState,
    // and InitSampleState propagates enabled to the output slot — this is what makes
    // a playing note render. A zero-initialized default => every note silent.
    memset(&gDefaultSampleState, 0, sizeof(gDefaultSampleState));
    gDefaultSampleState.bitField0.enabled = 1;
    gDefaultSampleState.bitField0.needsInit = 1;
    memset(&gZeroedSampleState, 0, sizeof(gZeroedSampleState));

    AudioBufferParameters* abp = &gMmSfx.audioBufferParameters;
    abp->specUnk4 = 1;
    abp->samplingFreq = 32000;
    abp->aiSamplingFreq = 32000;
    abp->numSamplesPerFrameTarget = 544;       // ALIGN16(32000/60)
    abp->numSamplesPerFrameMax = 560;
    abp->numSamplesPerFrameMin = 528;
    abp->updatesPerFrame = MMSFX_UPDATES_PER_FRAME; // ((544+16)/0xD0)+1 = 3
    abp->numSamplesPerUpdate = 176;            // (544/3) & ~7
    abp->numSamplesPerUpdateMax = 184;
    abp->numSamplesPerUpdateMin = 168;
    abp->numSequencePlayers = 1;               // only our SFX player is active
    abp->resampleRate = 1.0f;                  // 32000/32000
    abp->updatesPerFrameInv = 1.0f / (f32)MMSFX_UPDATES_PER_FRAME;
    abp->updatesPerFrameInvScaled = (1.0f / 256.0f) / (f32)MMSFX_UPDATES_PER_FRAME;
    abp->updatesPerFrameScaled = (f32)MMSFX_UPDATES_PER_FRAME / 4.0f;

    gMmSfx.numNotes = MMSFX_NUM_NOTES;
    gMmSfx.maxTempo = 3000;                     // updatesPerFrame*2880000/48/60
    gMmSfx.unk_2870 = 60.0f * (f32)MMSFX_UPDATES_PER_FRAME / 32000.0f / 3000.0f;
    gMmSfx.soundMode = SOUNDMODE_STEREO;
    gMmSfx.audioErrorFlags = 0;

    gMmSfx.miscPool.start = sMiscPool;
    gMmSfx.miscPool.cur = sMiscPool;
    gMmSfx.miscPool.size = sizeof(sMiscPool);

    gMmSfx.fontLoadStatus = sFontLoadStatus;
    memset(sFontLoadStatus, MMSFX_LOAD_STATUS_COMPLETE, sizeof(sFontLoadStatus));

    // notes + per-note synthesis buffers + sample-state output list (heap.c:1040-1046)
    gMmSfx.notes = (Note*)AudioHeap_AllocZeroed(&gMmSfx.miscPool, gMmSfx.numNotes * sizeof(Note));
    AudioPlayback_NoteInitAll();
    AudioPlayback_InitNoteFreeList();
    gMmSfx.sampleStateList = (NoteSampleState*)AudioHeap_AllocZeroed(
        &gMmSfx.miscPool, (u32)abp->updatesPerFrame * gMmSfx.numNotes * sizeof(NoteSampleState));
    gMmSfx.sampleStateOffset = 0;

    // ADSR decay-rate table (heap.c:1054-1056)
    gMmSfx.adsrDecayTable = (f32*)AudioHeap_AllocZeroed(&gMmSfx.miscPool, 0x100 * sizeof(f32));
    MmSfx_InitAdsrDecayTable(gMmSfx.adsrDecayTable, abp->updatesPerFrameInvScaled);

    // reverbs: zeroed by the memset above (SFX uses none)

    // sequence players + channels (heap.c:1068-1073)
    AudioScript_InitSequencePlayers();
    for (s32 i = 0; i < abp->numSequencePlayers; i++) {
        AudioScript_InitSequencePlayerChannels(i);
        AudioScript_ResetSequencePlayer(&gMmSfx.seqPlayers[i]);
    }
}

} // namespace mmsfx
