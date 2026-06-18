/*
 * mm_sfx_synth_types.h — Isolated MM audio-engine struct definitions.
 *
 * RUTA B (motor MM aislado). These are MM's (2ship2harkinian) audio structs,
 * copied VERBATIM from mm/include/z64audio.h, mm/include/audio/effects.h and
 * mm/include/audio/soundfont.h, wrapped in `namespace mmsfx` so they DO NOT
 * collide with SoH's older, differently-laid-out homonyms (SequenceChannel,
 * Note, SequencePlayer, ...). The ported MM seqplayer/playback/effects .cpp
 * files live entirely inside `namespace mmsfx` and therefore resolve every
 * unqualified type name to these MM versions.
 *
 * Do NOT include any SoH audio header (z64audio.h) from a TU that includes
 * this — that is the whole point of the isolation. Bridge code that must talk
 * to SoH lives in its own TU and uses the extern "C" API in mm_sfx_synth.h.
 *
 * See memory: mm_oot_audio_engine_divergence.md
 */
#ifndef MM_SFX_SYNTH_TYPES_H
#define MM_SFX_SYNTH_TYPES_H

#include <cstdint>

namespace mmsfx {

// libultraship/libultra/gbi.h #define's u8/s8/s16 etc. as MACROS (e.g.
// `#define s8 int8_t`). In the one TU that includes both libultraship and this
// header (the loader), those macros turn our typedefs into `typedef int8_t
// int8_t;` redefinitions. Drop the macros so our real typedefs stand. Harmless
// in the clean (non-SoH) TUs where the macros were never defined.
#ifdef u8
#undef u8
#endif
#ifdef u16
#undef u16
#endif
#ifdef u32
#undef u32
#endif
#ifdef s8
#undef s8
#endif
#ifdef s16
#undef s16
#endif
#ifdef s32
#undef s32
#endif
#ifdef f32
#undef f32
#endif
#ifdef f64
#undef f64
#endif
#ifdef UNK_TYPE1
#undef UNK_TYPE1
#endif

// ---- Base scalar types (MM uses these unqualified; keep ported code verbatim) ----
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef float    f32;
typedef double   f64;
typedef u8       UNK_TYPE1;

#define MM_SFX_ADPCMFSIZE 16
#define MM_SFX_SAMPLES_PER_FRAME MM_SFX_ADPCMFSIZE
#define MM_SFX_WAVE_SAMPLE_COUNT 64
#define MM_SFX_NUM_CHANNELS 16

// ==========================================================================
// effects.h
// ==========================================================================

typedef enum AdsrStatus {
    ADSR_STATUS_DISABLED,
    ADSR_STATUS_INITIAL,
    ADSR_STATUS_START_LOOP,
    ADSR_STATUS_LOOP,
    ADSR_STATUS_FADE,
    ADSR_STATUS_HANG,
    ADSR_STATUS_DECAY,
    ADSR_STATUS_RELEASE,
    ADSR_STATUS_SUSTAIN
} AdsrStatus;

#define ADSR_DISABLE 0
#define ADSR_HANG -1
#define ADSR_GOTO -2
#define ADSR_RESTART -3

typedef struct EnvelopePoint {
    /* 0x0 */ s16 delay;
    /* 0x2 */ s16 arg;
} EnvelopePoint; // size = 0x4

typedef struct AdsrSettings {
    /* 0x0 */ u8 decayIndex;
    /* 0x1 */ u8 sustain;
    /* 0x4 */ EnvelopePoint* envelope;
} AdsrSettings; // size = 0x8

typedef struct AdsrState {
    union {
        struct {
            /* 0x00 */ u8 unused : 1;
            /* 0x00 */ u8 hang : 1;
            /* 0x00 */ u8 decay : 1;
            /* 0x00 */ u8 release : 1;
            /* 0x00 */ u8 status : 4;
        } s;
        /* 0x00 */ u8 asByte;
    } action;
    /* 0x01 */ u8 envelopeIndex;
    /* 0x02 */ s16 delay;
    /* 0x04 */ f32 sustain;
    /* 0x08 */ f32 velocity;
    /* 0x0C */ f32 fadeOutVel;
    /* 0x10 */ f32 current;
    /* 0x14 */ f32 target;
    /* 0x18 */ UNK_TYPE1 pad18[4];
    /* 0x1C */ EnvelopePoint* envelope;
} AdsrState; // size = 0x20

typedef struct VibratoSubStruct {
    /* 0x0 */ u16 vibratoRateStart;
    /* 0x2 */ u16 vibratoDepthStart;
    /* 0x4 */ u16 vibratoRateTarget;
    /* 0x6 */ u16 vibratoDepthTarget;
    /* 0x8 */ u16 vibratoRateChangeDelay;
    /* 0xA */ u16 vibratoDepthChangeDelay;
    /* 0xC */ u16 vibratoDelay;
} VibratoSubStruct; // size = 0xE

typedef struct VibratoState {
    /* 0x00 */ VibratoSubStruct* vibSubStruct;
    /* 0x04 */ u32 time;
    /* 0x08 */ s16* curve;
    /* 0x0C */ f32 depth;
    /* 0x10 */ f32 rate;
    /* 0x14 */ u8 active;
    /* 0x16 */ u16 rateChangeTimer;
    /* 0x18 */ u16 depthChangeTimer;
    /* 0x1A */ u16 delay;
} VibratoState; // size = 0x1C

typedef enum PortamentoMode {
    PORTAMENTO_MODE_OFF,
    PORTAMENTO_MODE_1,
    PORTAMENTO_MODE_2,
    PORTAMENTO_MODE_3,
    PORTAMENTO_MODE_4,
    PORTAMENTO_MODE_5
} PortamentoMode;

#define PORTAMENTO_IS_SPECIAL(x) ((x).mode & 0x80)
#define PORTAMENTO_MODE(x) ((x).mode & ~0x80)

typedef struct Portamento {
    /* 0x0 */ u8 mode;
    /* 0x2 */ u16 cur;
    /* 0x4 */ u16 speed;
    /* 0x8 */ f32 extent;
} Portamento; // size = 0xC

// ==========================================================================
// soundfont.h
// ==========================================================================

typedef struct AdpcmLoop {
    /* 0x00 */ u32 start;
    /* 0x04 */ u32 loopEnd;
    /* 0x08 */ u32 count;
    /* 0x0C */ u32 sampleEnd;
    /* 0x10 */ s16 predictorState[16];
} AdpcmLoop;

typedef struct AdpcmBook {
    /* 0x0 */ s32 order;
    /* 0x4 */ s32 numPredictors;
    /* 0x8 */ s16* codeBook;
} AdpcmBook;

typedef enum SampleCodec {
    CODEC_ADPCM,
    CODEC_S8,
    CODEC_S16_INMEMORY,
    CODEC_SMALL_ADPCM,
    CODEC_REVERB,
    CODEC_S16,
    CODEC_UNK6,
    CODEC_UNK7,
    CODEC_OPUS,
} SampleCodec;

typedef enum SampleMedium {
    MEDIUM_RAM,
    MEDIUM_UNK,
    MEDIUM_CART,
    MEDIUM_DISK_DRIVE,
    MEDIUM_RAM_UNLOADED = 5
} SampleMedium;

typedef struct Sample {
    union {
        struct {
            /* 0x0 */ u32 codec : 4;
            /* 0x0 */ u32 medium : 2;
            /* 0x0 */ u32 unk_bit26 : 1;
            /* 0x0 */ u32 isRelocated : 1;
        };
        u32 asU32;
    };
    /* 0x1 */ u32 size;
    u32 fileSize;
    /* 0x4 */ u8* sampleAddr;
    /* 0x8 */ AdpcmLoop* loop;
    /* 0xC */ AdpcmBook* book;
} Sample;

typedef struct TunedSample {
    /* 0x0 */ Sample* sample;
    /* 0x4 */ f32 tuning;
} TunedSample;

typedef struct Instrument {
    /* 0x00 */ u8 isRelocated;
    /* 0x01 */ u8 normalRangeLo;
    /* 0x02 */ u8 normalRangeHi;
    /* 0x03 */ u8 adsrDecayIndex;
    /* 0x04 */ EnvelopePoint* envelope;
    /* 0x08 */ TunedSample lowPitchTunedSample;
    /* 0x10 */ TunedSample normalPitchTunedSample;
    /* 0x18 */ TunedSample highPitchTunedSample;
} Instrument;

typedef struct Drum {
    /* 0x0 */ u8 adsrDecayIndex;
    /* 0x1 */ u8 pan;
    /* 0x2 */ u8 isRelocated;
    /* 0x4 */ TunedSample tunedSample;
    /* 0xC */ EnvelopePoint* envelope;
} Drum;

typedef struct SoundEffect {
    /* 0x0 */ TunedSample tunedSample;
} SoundEffect;

typedef struct SoundFont {
    /* 0x00 */ u8 numInstruments;
    /* 0x01 */ u8 numDrums;
    /* 0x02 */ u8 sampleBankId1;
    /* 0x03 */ u8 sampleBankId2;
    /* 0x04 */ u16 numSfx;
    /* 0x08 */ Instrument** instruments;
    /* 0x0C */ Drum** drums;
    /* 0x10 */ SoundEffect* soundEffects;
    s32 fntIndex;
} SoundFont;

// ==========================================================================
// z64audio.h (sequence / note structures)
// ==========================================================================

struct Note;
struct NotePool;
struct SequenceChannel;
struct SequenceLayer;
struct SequencePlayer;

typedef struct AudioListItem {
    /* 0x00 */ struct AudioListItem* prev;
    /* 0x04 */ struct AudioListItem* next;
    union {
        /* 0x08 */ void* value; // Note* or SequenceLayer*
        /* 0x08 */ s32 count;
    } u;
    /* 0x0C */ struct NotePool* pool;
} AudioListItem;

typedef struct NotePool {
    /* 0x00 */ AudioListItem disabled;
    /* 0x10 */ AudioListItem decaying;
    /* 0x20 */ AudioListItem releasing;
    /* 0x30 */ AudioListItem active;
} NotePool;

typedef struct SeqScriptState {
    /* 0x00 */ u8* pc;
    /* 0x04 */ u8* stack[4];
    /* 0x14 */ u8 remLoopIters[4];
    /* 0x18 */ u8 depth;
    /* 0x19 */ s8 value;
} SeqScriptState;

typedef union StereoData {
    struct {
        /* 0x0 */ u8 unused : 2;
        /* 0x0 */ u8 type : 2;
        /* 0x0 */ u8 strongRight : 1;
        /* 0x0 */ u8 strongLeft : 1;
        /* 0x0 */ u8 strongReverbRight : 1;
        /* 0x0 */ u8 strongReverbLeft : 1;
    };
    /* 0x0 */ u8 asByte;
} StereoData;

typedef struct SequencePlayer {
    /* 0x000 */ u8 enabled : 1;
    /* 0x000 */ u8 finished : 1;
    /* 0x000 */ u8 muted : 1;
    /* 0x000 */ u8 seqDmaInProgress : 1;
    /* 0x000 */ u8 fontDmaInProgress : 1;
    /* 0x000 */ u8 recalculateVolume : 1;
    /* 0x000 */ u8 stopScript : 1;
    /* 0x000 */ u8 applyBend : 1;
    /* 0x001 */ u8 state;
    /* 0x002 */ u8 noteAllocPolicy;
    /* 0x003 */ u8 muteFlags;
    /* 0x004 */ u16 seqId;
    /* 0x005 */ u8 defaultFont;
    /* 0x006 */ u8 unk_06[1];
    /* 0x007 */ s8 playerIndex;
    /* 0x008 */ u16 tempo;
    /* 0x00A */ u16 tempoAcc;
    /* 0x00C */ s16 tempoChange;
    /* 0x00E */ s16 transposition;
    /* 0x010 */ u16 delay;
    /* 0x012 */ u16 fadeTimer;
    /* 0x014 */ u16 storedFadeTimer;
    /* 0x016 */ u16 unk_16;
    /* 0x018 */ u8* seqData;
    /* 0x01C */ f32 fadeVolume;
    /* 0x020 */ f32 fadeVelocity;
    /* 0x024 */ f32 volume;
    /* 0x028 */ f32 muteVolumeScale;
    /* 0x02C */ f32 fadeVolumeScale;
    /* 0x030 */ f32 appliedFadeVolume;
    /* 0x034 */ f32 bend;
    /* 0x038 */ struct SequenceChannel* channels[16];
    /* 0x078 */ SeqScriptState scriptState;
    /* 0x094 */ u8* shortNoteVelocityTable;
    /* 0x098 */ u8* shortNoteGateTimeTable;
    /* 0x09C */ NotePool notePool;
    /* 0x0DC */ s32 skipTicks;
    /* 0x0E0 */ u32 scriptCounter;
    /* 0x0E4 */ UNK_TYPE1 unk_E4[0x74];
    /* 0x158 */ s8 seqScriptIO[8];
    /*       */ f32 portVolumeScale;
} SequencePlayer;

typedef struct NoteAttributes {
    /* 0x00 */ u8 targetReverbVol;
    /* 0x01 */ u8 gain;
    /* 0x02 */ u8 pan;
    /* 0x03 */ u8 surroundEffectIndex;
    /* 0x04 */ StereoData stereoData;
    /* 0x05 */ u8 combFilterSize;
    /* 0x06 */ u16 combFilterGain;
    /* 0x08 */ f32 freqScale;
    /* 0x0C */ f32 velocity;
    /* 0x10 */ s16* filter;
    /* 0x14 */ s16* filterBuf;
} NoteAttributes;

typedef struct SequenceChannel {
    /* 0x00 */ u8 enabled : 1;
    /* 0x00 */ u8 finished : 1;
    /* 0x00 */ u8 stopScript : 1;
    /* 0x00 */ u8 muted : 1;
    /* 0x00 */ u8 hasInstrument : 1;
    /* 0x00 */ u8 stereoHeadsetEffects : 1;
    /* 0x00 */ u8 largeNotes : 1;
    /* 0x00 */ u8 unused : 1;
    union {
        struct {
            /* 0x01 */ u8 freqScale : 1;
            /* 0x01 */ u8 volume : 1;
            /* 0x01 */ u8 pan : 1;
        } s;
        /* 0x01 */ u8 asByte;
    } changes;
    /* 0x02 */ u8 noteAllocPolicy;
    /* 0x03 */ u8 muteFlags;
    /* 0x04 */ u8 targetReverbVol;
    /* 0x05 */ u8 notePriority;
    /* 0x06 */ u8 someOtherPriority;
    /* 0x07 */ u8 fontId;
    /* 0x08 */ u8 reverbIndex;
    /* 0x09 */ u8 bookOffset;
    /* 0x0A */ u8 newPan;
    /* 0x0B */ u8 panChannelWeight;
    /* 0x0C */ u8 gain;
    /* 0x0D */ u8 velocityRandomVariance;
    /* 0x0E */ u8 gateTimeRandomVariance;
    /* 0x0F */ u8 combFilterSize;
    /* 0x10 */ u8 surroundEffectIndex;
    /* 0x11 */ u8 channelIndex;
    /* 0x12 */ VibratoSubStruct vibrato;
    /* 0x20 */ u16 delay;
    /* 0x22 */ u16 combFilterGain;
    /* 0x24 */ u16 unk_22;
    /* 0x26 */ s16 instOrWave;
    /* 0x28 */ s16 transposition;
    /* 0x2C */ f32 volumeScale;
    /* 0x30 */ f32 volume;
    /* 0x34 */ s32 pan;
    /* 0x38 */ f32 appliedVolume;
    /* 0x3C */ f32 freqScale;
    /* 0x40 */ u8 (*dynTable)[][2];
    /* 0x44 */ struct Note* noteUnused;
    /* 0x48 */ struct SequenceLayer* layerUnused;
    /* 0x4C */ Instrument* instrument;
    /* 0x50 */ SequencePlayer* seqPlayer;
    /* 0x54 */ struct SequenceLayer* layers[4];
    /* 0x64 */ SeqScriptState scriptState;
    /* 0x80 */ AdsrSettings adsr;
    /* 0x88 */ NotePool notePool;
    /* 0xC8 */ s8 seqScriptIO[8];
    /* 0xD0 */ u8* sfxState; // SfxChannelState*
    /* 0xD4 */ s16* filter;
    /* 0xD8 */ StereoData stereoData;
    /* 0xDC */ s32 startSamplePos;
    /* 0xE0 */ s32 unk_E0;
} SequenceChannel;

typedef struct SequenceLayer {
    /* 0x00 */ u8 enabled : 1;
    /* 0x00 */ u8 finished : 1;
    /* 0x00 */ u8 muted : 1;
    /* 0x00 */ u8 continuousNotes : 1;
    /* 0x00 */ u8 bit3 : 1;
    /* 0x00 */ u8 ignoreDrumPan : 1;
    /* 0x00 */ u8 bit1 : 1;
    /* 0x00 */ u8 notePropertiesNeedInit : 1;
    /* 0x01 */ StereoData stereoData;
    /* 0x02 */ u8 instOrWave;
    /* 0x03 */ u8 gateTime;
    /* 0x04 */ u8 semitone;
    /* 0x05 */ u8 portamentoTargetNote;
    /* 0x06 */ u8 pan;
    /* 0x07 */ u8 notePan;
    /* 0x08 */ u8 surroundEffectIndex;
    /* 0x09 */ u8 targetReverbVol;
    union {
        struct {
            /* 0x0A */ u16 bit_0 : 1;
            /* 0x0A */ u16 bit_1 : 1;
            /* 0x0A */ u16 bit_2 : 1;
            /* 0x0A */ u16 useVibrato : 1;
            /* 0x0A */ u16 bit_4 : 1;
            /* 0x0A */ u16 bit_5 : 1;
            /* 0x0A */ u16 bit_6 : 1;
            /* 0x0A */ u16 bit_7 : 1;
            /* 0x0A */ u16 bit_8 : 1;
            /* 0x0A */ u16 bit_9 : 1;
            /* 0x0A */ u16 bit_A : 1;
            /* 0x0A */ u16 bit_B : 1;
            /* 0x0A */ u16 bit_C : 1;
            /* 0x0A */ u16 bit_D : 1;
            /* 0x0A */ u16 bit_E : 1;
            /* 0x0A */ u16 bit_F : 1;
        } s;
        /* 0x0A */ u16 asByte;
    } unk_0A;
    /* 0x0C */ VibratoSubStruct vibrato;
    /* 0x1A */ s16 delay;
    /* 0x1C */ s16 gateDelay;
    /* 0x1E */ s16 delay2;
    /* 0x20 */ u16 portamentoTime;
    /* 0x22 */ s16 transposition;
    /* 0x24 */ s16 shortNoteDefaultDelay;
    /* 0x26 */ s16 lastDelay;
    /* 0x28 */ AdsrSettings adsr;
    /* 0x30 */ Portamento portamento;
    /* 0x3C */ struct Note* note;
    /* 0x40 */ f32 freqScale;
    /* 0x44 */ f32 bend;
    /* 0x48 */ f32 velocitySquare2;
    /* 0x4C */ f32 velocitySquare;
    /* 0x50 */ f32 noteVelocity;
    /* 0x54 */ f32 noteFreqScale;
    /* 0x58 */ Instrument* instrument;
    /* 0x5C */ TunedSample* tunedSample;
    /* 0x60 */ SequenceChannel* channel;
    /* 0x64 */ SeqScriptState scriptState;
    /* 0x80 */ AudioListItem listItem;
} SequenceLayer;

typedef struct NoteSynthesisBuffers {
    /* 0x000 */ s16 adpcmState[16];
    /* 0x020 */ s16 finalResampleState[16];
    /* 0x040 */ s16 filterState[32];
    /* 0x080 */ s16 unusedState[16];
    /* 0x0A0 */ s16 haasEffectDelayState[32];
    /* 0x0E0 */ s16 combFilterState[128];
    /* 0x1E0 */ s16 surroundEffectState[128];
} NoteSynthesisBuffers;

struct OggOpusFile;

typedef struct NoteSynthesisState {
    /* 0x00 */ u8 atLoopPoint : 1;
    /* 0x00 */ u8 stopLoop : 1;
    /* 0x01 */ u8 sampleDmaIndex;
    /* 0x02 */ u8 prevHaasEffectLeftDelaySize;
    /* 0x03 */ u8 prevHaasEffectRightDelaySize;
    /* 0x04 */ u8 curReverbVol;
    /* 0x05 */ u8 numParts;
    /* 0x06 */ u16 samplePosFrac;
    /* 0x08 */ u16 surroundEffectGain;
    /* 0x0C */ s32 samplePosInt;
    /* 0x10 */ NoteSynthesisBuffers* synthesisBuffers;
    /* 0x14 */ s16 curVolLeft;
    /* 0x16 */ s16 curVolRight;
    /* 0x18 */ UNK_TYPE1 unk_14[0x6];
    /* 0x1E */ u8 combFilterNeedsInit;
    /* 0x1F */ u8 unk_1F;
    struct OggOpusFile* opusFile;
} NoteSynthesisState;

typedef enum NotePlaybackStatus {
    PLAYBACK_STATUS_0,
    PLAYBACK_STATUS_1,
    PLAYBACK_STATUS_2
} NotePlaybackStatus;

typedef struct NotePlaybackState {
    /* 0x00 */ u8 priority;
    /* 0x01 */ u8 waveId;
    /* 0x02 */ u8 harmonicIndex;
    /* 0x03 */ u8 fontId;
    /* 0x04 */ u8 status;
    /* 0x05 */ u8 stereoHeadsetEffects;
    /* 0x06 */ s16 adsrVolScaleUnused;
    /* 0x08 */ f32 portamentoFreqScale;
    /* 0x0C */ f32 vibratoFreqScale;
    /* 0x18 */ SequenceLayer* wantedParentLayer;
    /* 0x14 */ SequenceLayer* parentLayer;
    /* 0x10 */ SequenceLayer* prevParentLayer;
    /* 0x1C */ NoteAttributes attributes;
    /* 0x34 */ AdsrState adsr;
    /* 0x54 */ Portamento portamento;
    /* 0x60 */ VibratoState vibratoState;
    /* 0x7C */ UNK_TYPE1 pad7C[0x4];
    /* 0x80 */ u8 unk_80;
    /* 0x84 */ u32 startSamplePos;
    /* 0x88 */ UNK_TYPE1 unk_BC[0x1C];
} NotePlaybackState;

typedef struct NoteSampleState {
    struct {
        /* 0x00 */ volatile u8 enabled : 1;
        /* 0x00 */ u8 needsInit : 1;
        /* 0x00 */ u8 finished : 1;
        /* 0x00 */ u8 unused : 1;
        /* 0x00 */ u8 strongRight : 1;
        /* 0x00 */ u8 strongLeft : 1;
        /* 0x00 */ u8 strongReverbRight : 1;
        /* 0x00 */ u8 strongReverbLeft : 1;
    } bitField0;
    struct {
        /* 0x01 */ u8 reverbIndex : 3;
        /* 0x01 */ u8 bookOffset : 2;
        /* 0x01 */ u8 isSyntheticWave : 1;
        /* 0x01 */ u8 hasTwoParts : 1;
        /* 0x01 */ u8 useHaasEffect : 1;
    } bitField1;
    /* 0x02 */ u8 gain;
    /* 0x03 */ u8 haasEffectLeftDelaySize;
    /* 0x04 */ u8 haasEffectRightDelaySize;
    /* 0x05 */ u8 targetReverbVol;
    /* 0x06 */ u8 harmonicIndexCurAndPrev;
    /* 0x07 */ u8 combFilterSize;
    /* 0x08 */ u16 targetVolLeft;
    /* 0x0A */ u16 targetVolRight;
    /* 0x0C */ u16 frequencyFixedPoint;
    /* 0x0E */ u16 combFilterGain;
    union {
        /* 0x10 */ TunedSample* tunedSample;
        /* 0x10 */ s16* waveSampleAddr;
    };
    /* 0x14 */ s16* filter;
    /* 0x18 */ UNK_TYPE1 unk_18;
    /* 0x19 */ u8 surroundEffectIndex;
    /* 0x1A */ UNK_TYPE1 unk_1A[0x6];
} NoteSampleState;

typedef struct Note {
    /* 0x00 */ AudioListItem listItem;
    /* 0x10 */ NoteSynthesisState synthesisState;
    /* 0x34 */ NotePlaybackState playbackState;
    /* 0xD8 */ NoteSampleState sampleState;
} Note;

// ---- audio buffer / timing parameters (EXACT MM AudioBufferParameters layout) ----
// Single source of truth (included by ctx.h and common.h).
typedef struct AudioBufferParameters {
    /* 0x00 */ s16 specUnk4;
    /* 0x02 */ u16 samplingFreq;
    /* 0x04 */ u16 aiSamplingFreq;
    /* 0x06 */ s16 numSamplesPerFrameTarget;
    /* 0x08 */ s16 numSamplesPerFrameMax;
    /* 0x0A */ s16 numSamplesPerFrameMin;
    /* 0x0C */ s16 updatesPerFrame;          // updates per audio frame
    /* 0x0E */ s16 numSamplesPerUpdate;
    /* 0x10 */ s16 numSamplesPerUpdateMax;
    /* 0x12 */ s16 numSamplesPerUpdateMin;
    /* 0x14 */ s16 numSequencePlayers;
    /* 0x18 */ f32 resampleRate;
    /* 0x1C */ f32 updatesPerFrameInv;        // 1 / updatesPerFrame
    /* 0x20 */ f32 updatesPerFrameInvScaled;  // updatesPerFrameInv / 256 (ADSR decay table)
    /* 0x24 */ f32 updatesPerFrameScaled;     // updatesPerFrame / 4 (ADSR delay scaling)
} AudioBufferParameters;

// Minimal stand-ins for the few aggregate context members the ported MM lib
// touches (we don't load from ROM, so only the read/written fields matter).
typedef struct AudioAllocPool {
    /*       */ u8* start;
    /*       */ u8* cur;
    /*       */ s32 size;
    /*       */ s32 count;
} AudioAllocPool;

typedef struct AudioCacheEntryMin {
    /*       */ s32 id;
} AudioCacheEntryMin;

typedef struct AudioTemporaryCacheMin {
    /*       */ AudioCacheEntryMin entries[2];
    /*       */ s32 nextSide;
} AudioTemporaryCacheMin;

typedef struct AudioCache {
    /*       */ AudioTemporaryCacheMin temporary;
} AudioCache;

// seqplayer reads synthesisReverbs[i].tunedSample only; SFX uses no reverb so a
// zeroed tunedSample (sample==NULL) is a no-op.
typedef struct SynthesisReverb {
    /*       */ TunedSample tunedSample;
} SynthesisReverb;

typedef u32 (*AudioCustomSeqFunction)(s8 value, SequenceChannel* channel);

// MM's SfxChannelState (code_8019AF00.c). channel->sfxState points at one of
// these per SFX channel; the 0xA0-0xA3 opcodes index it as a raw byte array and
// AudioSfx_SetFreqAndStereoBits (custom function slot 0) reads freqScale/stereoBits.
typedef struct SfxChannelState {
    /* 0x0 */ f32 volume;
    /* 0x4 */ f32 freqScale;
    /* 0x8 */ s8 reverb;
    /* 0x9 */ s8 panSigned;
    /* 0xA */ s8 stereoBits;
    /* 0xB */ u8 filter;
    /* 0xC */ u8 combFilterGain;
    /* 0xD */ u8 zVolume;
} SfxChannelState; // size = 0x10

} // namespace mmsfx

#endif // MM_SFX_SYNTH_TYPES_H
