/*
 * mm_sfx_synth_common.h — shared macros, constants, enums and data-table
 * externs for the isolated MM SFX synth (namespace mmsfx). Included by every
 * ported MM lib TU (effects/seqplayer/playback) plus the loader/backend.
 *
 * See memory: mm_oot_audio_engine_divergence.md
 */
#ifndef MM_SFX_SYNTH_COMMON_H
#define MM_SFX_SYNTH_COMMON_H

// NULL / size_t: MSVC pulls these in transitively, but g++/clang (Linux/macOS CI)
// do not, so the MM SFX TUs that use NULL fail to build without these. Include
// here since this header is pulled in by every ported MM lib TU.
#include <cstddef>

#include "mm_sfx_synth_types.h"

namespace mmsfx {

// ---- generic macros used throughout the MM audio lib ----
#ifndef ARRAY_COUNT
#define ARRAY_COUNT(arr) (s32)(sizeof(arr) / sizeof((arr)[0]))
#endif
#define SQ(x) ((x) * (x))
#define ABS(x) ((x) >= 0 ? (x) : -(x))
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define AUDIO_LERPIMP(v0, v1, t) ((v0) + (((v1) - (v0)) * (t)))

// Big-endian swap for ROM/OTR audio data (envelopes, soundfont headers).
// 2S2H/MM keep audio data big-endian and swap at read time. SoH's OTR audio
// blobs are likewise big-endian, so we swap. (Verified per-field in the loader;
// flip MMSFX_AUDIO_BIG_ENDIAN to 0 if a given table proves native-endian.)
#define MMSFX_AUDIO_BIG_ENDIAN 1
#if MMSFX_AUDIO_BIG_ENDIAN
static inline u16 MmSfx_BE16(u16 v) { return (u16)((v << 8) | (v >> 8)); }
static inline u32 MmSfx_BE32(u32 v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
}
#else
static inline u16 MmSfx_BE16(u16 v) { return v; }
static inline u32 MmSfx_BE32(u32 v) { return v; }
#endif
// Guard against libultraship's endianness.h (which also defines BE16/32SWAP).
// Our TUs that use these (effects/playback) don't include libultraship, so they
// get ours; the loader includes both but doesn't use them.
#ifndef BE16SWAP
#define BE16SWAP(x) ((s16)MmSfx_BE16((u16)(x)))
#endif
#ifndef BE32SWAP
#define BE32SWAP(x) ((s32)MmSfx_BE32((u32)(x)))
#endif
// Compile-time 16-bit byte swap for static initializers (MM's BE16SWAP_CONST).
#define BE16SWAP_CONST(x) ((s16)((((u16)(x) & 0xFF) << 8) | (((u16)(x) >> 8) & 0xFF)))

// ---- constants ----
#define SEQ_NUM_CHANNELS 16
#define WAVE_SAMPLE_COUNT 64

#define MUTE_FLAGS_STOP_SAMPLES (1 << 3)
#define MUTE_FLAGS_STOP_LAYER (1 << 4)
#define MUTE_FLAGS_SOFTEN (1 << 5)
#define MUTE_FLAGS_STOP_NOTES (1 << 6)
#define MUTE_FLAGS_STOP_SCRIPT (1 << 7)

typedef enum SeqPlayerState {
    SEQPLAYER_STATE_0,
    SEQPLAYER_STATE_FADE_IN,
    SEQPLAYER_STATE_FADE_OUT
} SeqPlayerState;

typedef enum SoundMode {
    SOUNDMODE_STEREO,
    SOUNDMODE_HEADSET,
    SOUNDMODE_SURROUND_EXTERNAL,
    SOUNDMODE_MONO,
    SOUNDMODE_SURROUND
} SoundMode;

// AudioBufferParameters / AudioAllocPool / AudioCache / SynthesisReverb /
// AudioCustomSeqFunction now live in mm_sfx_synth_types.h (single source).

// ---- tatum / tempo constant ----
#ifndef TATUMS_PER_BEAT
#define TATUMS_PER_BEAT 48 // z64audio.h:20
#endif

// ---- data tables from MM data.c (defined in mm_sfx_synth_data.cpp) ----
extern f32 gBendPitchOneOctaveFrequencies[256];
extern f32 gBendPitchTwoSemitonesFrequencies[256];
extern s16* gWaveSamples[9];                          // 9 entries in MM; index 2 == gSineWaveSample
extern s16 gSineWaveSample[];                          // 256 samples (4 harmonics x 64); indexed mod WAVE_SAMPLE_COUNT
extern f32 gPitchFrequencies[128];                    // MM's per-semitone note->freqScale (== OOT gNoteFrequencies)
extern u8 gDefaultShortNoteVelocityTable[16];
extern u8 gDefaultShortNoteGateTimeTable[16];
extern f32 gHeadsetPanVolume[128];                    // SOUNDMODE_HEADSET pan curve
extern f32 gStereoPanVolume[128];                     // SOUNDMODE_STEREO pan curve
extern f32 gDefaultPanVolume[128];                    // default / mono pan curve
extern const s16 gAudioTatumInit[2];                  // [1] == gTatumsPerBeat == TATUMS_PER_BEAT

// adsrDecayTable: MM has NO constant source table. gAudioDecayRates is a dummy
// (the extern resolves at link time but is unused). The runtime f32[256] table
// is built procedurally — call MmSfx_InitAdsrDecayTable() at synth init with the
// known updatesPerFrameInvScaled. (See heap.c:26-55.)
extern u8 gAudioDecayRates[][16];
f32 MmSfx_CalculateAdsrDecay(f32 updatesPerFrameInvScaled, f32 scaleInv);
void MmSfx_InitAdsrDecayTable(f32* outTable, f32 updatesPerFrameInvScaled);

} // namespace mmsfx

#endif // MM_SFX_SYNTH_COMMON_H
