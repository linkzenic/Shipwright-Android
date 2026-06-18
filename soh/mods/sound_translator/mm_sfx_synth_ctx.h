/*
 * mm_sfx_synth_ctx.h — the isolated MM audio context (`gMmSfx`) and the glue
 * function surface that the ported MM lib TUs (seqplayer/playback) call.
 *
 * Deliberately macro-free and enum-light so it can be included by the
 * self-contained seqplayer.cpp (which defines its own local macros/enums)
 * WITHOUT redefinition conflicts. Only structs/typedefs come from types.h.
 *
 * See memory: mm_oot_audio_engine_divergence.md
 */
#ifndef MM_SFX_SYNTH_CTX_H
#define MM_SFX_SYNTH_CTX_H

#include "mm_sfx_synth_types.h"

namespace mmsfx {

// ---------------------------------------------------------------------------
// Isolated audio context. A lean stand-in for MM's AudioContext holding ONLY
// the members the ported seqplayer.c / playback.c / effects.c actually touch.
// Field names & types match MM's AudioContext so the ported code reads
// `gMmSfx.<field>` verbatim. Pointer members (notes/adsrDecayTable/
// sampleStateList) are backed by static storage in the glue TU at init.
// ---------------------------------------------------------------------------
typedef struct AudioContext {
    /* note / layer / channel pools */
    Note* notes;                         // -> static note array (numNotes)
    s32 numNotes;
    NoteSampleState* sampleStateList;    // -> static array (numNotes * updatesPerFrame)
    s32 sampleStateOffset;
    NotePool noteFreeLists;
    AudioListItem layerFreeList;
    SequenceLayer sequenceLayers[80];
    SequenceChannel sequenceChannelNone;
    SequencePlayer seqPlayers[5];

    /* reverb (unused by SFX; zeroed) */
    SynthesisReverb synthesisReverbs[4];

    /* timing / misc */
    AudioBufferParameters audioBufferParameters;
    f32 unk_2870;
    s16 maxTempo;
    s8 soundMode;
    u32 audioRandom;
    s32 audioErrorFlags;

    /* custom seq function slots (0xBE). Real seqs use slot 0 only. */
    AudioCustomSeqFunction customSeqFunctions[4];

    /* heap / cache stand-ins */
    AudioAllocPool miscPool;
    AudioCache fontCache;

    /* fonts / adsr */
    SoundFont* soundFontList;            // indexed by fontId (we register 0/1)
    u8* fontLoadStatus;                  // all-loaded marker array
    f32* adsrDecayTable;                 // -> static f32[256] built at init
} AudioContext;

extern AudioContext gMmSfx;

// Global scratch written by channel opcode 0xBE (custom-function dispatch).
extern AudioCustomSeqFunction gAudioCustomSeqFunction;

// ---------------------------------------------------------------------------
// Data tables (defined in mm_sfx_synth_data.cpp). Declared unsized so this
// header and common.h can both declare them without size-conflict.
// ---------------------------------------------------------------------------
extern EnvelopePoint gDefaultEnvelope[];
extern f32 gBendPitchOneOctaveFrequencies[];
extern f32 gBendPitchTwoSemitonesFrequencies[];
extern f32 gPitchFrequencies[];
extern u8 gDefaultShortNoteVelocityTable[];
extern u8 gDefaultShortNoteGateTimeTable[];

// ---------------------------------------------------------------------------
// Glue surface — implemented in mm_sfx_synth_glue.cpp. The ported lib files
// call these (heap/load/thread analogues, simplified because we preload all
// assets into RAM and never DMA).
// ---------------------------------------------------------------------------
AudioBufferParameters* MmSfx_GetBufParams(void);

// heap.c analogues
void* AudioHeap_AllocZeroed(AudioAllocPool* pool, u32 size);
void* AudioHeap_SearchCaches(s32 tableType, s32 cache, s32 id);
void AudioHeap_LoadFilter(s16* filter, s32 lowPassCutoff, s32 highPassCutoff);

// load.c analogues (everything is preloaded → "complete" / no-op)
s32 AudioLoad_IsSeqLoadComplete(s32 seqId);
s32 AudioLoad_IsFontLoadComplete(s32 fontId);
void AudioLoad_SetSeqLoadStatus(s32 seqId, s32 status);
void AudioLoad_SetFontLoadStatus(s32 fontId, s32 status);
s32 AudioLoad_SlowLoadSample(s32 fontId, s8 instId, s8* isDone);
void AudioLoad_SlowLoadSeq(s32 seqId, u8* ramAddr, s8* isDone);
void AudioLoad_ScriptLoad(s32 tableType, s32 id, s8* isDone);
void AudioLoad_SyncInitSeqPlayer(s32 playerIndex, s32 seqId, s32 arg2);

// thread analogue
u32 AudioThread_NextRandom(void);

} // namespace mmsfx

#endif // MM_SFX_SYNTH_CTX_H
