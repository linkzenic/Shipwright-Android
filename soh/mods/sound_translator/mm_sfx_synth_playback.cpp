/*
 * mm_sfx_synth_playback.cpp
 *
 * Near-verbatim port of Majora's Mask (2ship2harkinian) audio note-playback
 * layer, mm/src/audio/lib/playback.c, isolated for the SoH SFX translator.
 * Everything lives in `namespace mmsfx`. Every `gAudioCtx.` was rewritten to
 * `gMmSfx.`. MM function names are kept VERBATIM (the namespace prevents any
 * collision with SoH's own audio symbols).
 *
 * The 2S2H custom-audio / opus-streaming / AudioApi hooks were STRIPPED back to
 * the vanilla MM MEDIUM_RAM ADPCM path. Specifically removed:
 *   - aOPUSFree() and the synthesisState.opusFile branch in AudioPlayback_NoteDisable.
 *   - The `ResourceMgr_LoadAudioSoundFontByName(gFontMap[...])` 2S2H archive
 *     lookup in Get{Instrument,Drum,SoundEffect}; replaced with the vanilla
 *     gMmSfx.soundFontList[fontId] access (caller guarantees fonts are loaded).
 *   - The CVar "gSettings.Audio.MasterVolume" scaling in AudioPlayback_InitSampleState
 *     (kept the vanilla math; master-volume is applied by the SoH side / glue).
 *
 * ===========================================================================
 * (a) EXTERNAL FUNCTIONS CALLED — defined in OTHER MM lib files (heap.c,
 *     load.c, effects.c, thread) or the glue. NOT defined here; provide them
 *     all inside namespace mmsfx:
 * ---------------------------------------------------------------------------
 *   // effects.c (already ported in mm_sfx_synth_effects.cpp)
 *   void  AudioEffects_InitAdsr(AdsrState* adsr, EnvelopePoint* envelope, s16* volOut);
 *   f32   AudioEffects_UpdateAdsr(AdsrState* adsr);
 *   void  AudioEffects_UpdatePortamentoAndVibrato(Note* note);
 *   void  AudioEffects_InitVibrato(Note* note);
 *   void  AudioEffects_InitPortamento(Note* note);
 *
 *   // seqplayer.cpp (already ported)
 *   void  AudioScript_SequenceChannelDisable(SequenceChannel* channel);
 *   void  AudioScript_AudioListPushBack(AudioListItem* list, AudioListItem* item);
 *   void* AudioScript_AudioListPopBack(AudioListItem* list);
 *
 *   // load.c (glue)
 *   s32   AudioLoad_IsFontLoadComplete(s32 fontId);
 *
 *   // heap.c (glue) — only AudioPlayback_NoteInitAll uses these; that fn is
 *   // optional (not on the SFX hot path), keep or drop with the glue.
 *   void* AudioHeap_AllocDmaMemory(void* pool, u32 size); // pool is AudioAllocPool* (&gMmSfx.miscPool)
 *
 * ===========================================================================
 * (b) CONTEXT FIELDS USED — gMmSfx.<field> with its MM type (from AudioContext
 *     in mm/include/z64audio.h). Build the isolated context to match:
 * ---------------------------------------------------------------------------
 *   s32                   numNotes;                 // # of active Note slots
 *   Note*                 notes;                    // Note notes[numNotes]
 *   NoteSampleState*      sampleStateList;          // synth out, indexed by sampleStateOffset+i
 *   s32                   sampleStateOffset;        // base offset into sampleStateList this update
 *   NotePool              noteFreeLists;            // global free-note pool
 *   u8/SoundMode          soundMode;                // SOUNDMODE_* (read in InitSampleState)
 *   AudioBufferParameters audioBufferParameters;    // .updatesPerFrameInv (f32) AND .resampleRate (f32)
 *                                                   //   NOTE: common.h's AudioBufferParameters has
 *                                                   //   updatesPerFrameInv but NOT resampleRate — the glue
 *                                                   //   must add an `f32 resampleRate;` field (MM default 1.0f).
 *   f32                   adsrDecayTable[256];      // indexed by AdsrSettings.decayIndex
 *   SoundFont*            soundFontList;            // soundFontList[fontId] (numInstruments/numDrums/numSfx,
 *                                                   //   instruments[], drums[], soundEffects[])
 *   u32                   audioErrorFlags;          // set on lookup failure
 *   AudioAllocPool        miscPool;                 // only AudioPlayback_NoteInitAll uses it (&gMmSfx.miscPool)
 *
 * ===========================================================================
 * (c) DATA TABLES referenced — live in MM's data.c (mm_sfx_synth_data.cpp or
 *     glue), all inside namespace mmsfx:
 * ---------------------------------------------------------------------------
 *   extern f32  gHeadsetPanVolume[128];   // SOUNDMODE_HEADSET pan curve   (data.cpp)
 *   extern f32  gStereoPanVolume[128];    // SOUNDMODE_STEREO pan curve     (data.cpp)
 *   extern f32  gDefaultPanVolume[128];   // default/surround pan curve     (data.cpp)
 *   extern s16* gWaveSamples[9];          // synthetic wave banks           (data.cpp)
 *   extern u8   gHaasEffectDelaySize[64]; // headset Haas-effect delay LUT  (GLUE — not in data.cpp)
 *   extern NoteSampleState gDefaultSampleState; // template copied in NoteInit (GLUE)
 *   // (gMmSfx.adsrDecayTable is the per-context f32[256] decay-rate table, built
 *   //  procedurally by MmSfx_InitAdsrDecayTable — see common.h.)
 *   // NOTE: gPitchFrequencies / gBendPitch* are NOT used by playback.c; the
 *   //  resampling rate comes from subAttrs->frequency, computed upstream.
 * ===========================================================================
 *
 * --- PER-NOTE SYNTH OUTPUT CONTRACT (what AudioPlayback_ProcessNotes writes) ---
 * For each active note i (playbackState->priority != 0), ProcessNotes fills
 * sampleState = &gMmSfx.sampleStateList[gMmSfx.sampleStateOffset + i] via
 * AudioPlayback_InitSampleState(). The synth backend consumes, per note:
 *   - sampleState->bitField0.enabled / .finished       (whether to render)
 *   - sampleState->bitField0.strong{Left,Right} / strongReverb{Left,Right}
 *   - sampleState->bitField1.{isSyntheticWave,hasTwoParts,useHaasEffect,
 *                             reverbIndex,bookOffset}
 *   - sampleState->frequencyFixedPoint  : UQ16.16 resampling ratio * 32768
 *                                         (resamplingRate folded to 0..2 range,
 *                                          hasTwoParts set when input >= 2.0;
 *                                          synthetic waves keep *0.25 above 4.0)
 *   - sampleState->targetVolLeft/Right  : UQ4.12 (0..0x1000) target volumes,
 *                                         = velocity*panVol*(0x1000-0.001)
 *   - sampleState->gain                 : UQ4.4 multiplicative gain
 *   - sampleState->targetReverbVol, ->combFilterSize/Gain, ->filter,
 *     ->surroundEffectIndex, ->haasEffect{Left,Right}DelaySize
 *   - sampleState->tunedSample (union w/ waveSampleAddr): Sample* + tuning to
 *     resample (or the synthetic-wave pointer for isSyntheticWave notes).
 */

#include "mm_sfx_synth_common.h"
#include "mm_sfx_synth_ctx.h" // full AudioContext + gMmSfx
#include <cstdio>

namespace mmsfx {

// Diagnostic log hook (implemented in loader.cpp).
extern "C" void MmSfxSynth_Log(const char* msg);

// ---- Local constants / macros (verbatim from MM z64audio.h) ---------------
#define NO_LAYER ((SequenceLayer*)(-1))
#define FILTER_SIZE (8 * 8) // 8 taps * 8 (lowpass+highpass)

#ifndef false
#define false 0
#define true 1
#endif
#ifndef NULL
#define NULL 0
#endif

#define CLAMP_MAX(x, max) ((x) > (max) ? (max) : (x))

// AUDIO_ERROR packing (z64audio.h). Only used to set gMmSfx.audioErrorFlags;
// value is informational. (arg1 << 16) | (arg2 << 8) | code.
#define AUDIO_ERROR_FONT_NOT_LOADED 1
#define AUDIO_ERROR_NO_INST 2
#define AUDIO_ERROR_NO_DRUM_SFX 3
#define AUDIO_ERROR(arg1, arg2, code) (((arg1) << 16) | ((arg2) << 8) | (code))

// NoteSubAttributes — VERBATIM from MM z64audio.h:666. Small per-note scratch
// passed from ProcessNotes into InitSampleState; lives only in this TU.
typedef struct {
    /* 0x00 */ u8 targetReverbVol;
    /* 0x01 */ u8 gain; // UQ4.4 multiplicative gain
    /* 0x02 */ u8 pan;
    /* 0x03 */ u8 surroundEffectIndex;
    /* 0x04 */ StereoData stereoData;
    /* 0x08 */ f32 frequency;
    /* 0x0C */ f32 velocity;
    /* 0x10 */ char unk_0C[0x4];
    /* 0x14 */ s16* filter;
    /* 0x18 */ u8 combFilterSize;
    /* 0x1A */ u16 combFilterGain;
} NoteSubAttributes; // size = 0x1A

// ---- External data tables (data.cpp / glue) -------------------------------
extern f32 gHeadsetPanVolume[];
extern f32 gStereoPanVolume[];
extern f32 gDefaultPanVolume[];
extern s16* gWaveSamples[];
extern u8 gHaasEffectDelaySize[];           // GLUE provides
extern NoteSampleState gDefaultSampleState; // GLUE provides (template copied in NoteInit)
extern NoteSampleState gZeroedSampleState;  // GLUE provides (all-zero; used by NoteInitAll)

// ---- External functions (other MM lib TUs / glue) -------------------------
void AudioEffects_InitAdsr(AdsrState* adsr, EnvelopePoint* envelope, s16* volOut);
f32 AudioEffects_UpdateAdsr(AdsrState* adsr);
void AudioEffects_UpdatePortamentoAndVibrato(Note* note);
void AudioEffects_InitVibrato(Note* note);
void AudioEffects_InitPortamento(Note* note);

void AudioScript_SequenceChannelDisable(SequenceChannel* channel);
void AudioScript_AudioListPushBack(AudioListItem* list, AudioListItem* item);
void* AudioScript_AudioListPopBack(AudioListItem* list);

s32 AudioLoad_IsFontLoadComplete(s32 fontId);

void* AudioHeap_AllocDmaMemory(void* pool, u32 size);

// ---- Forward declarations (this TU) ---------------------------------------
void AudioPlayback_SeqLayerNoteDecay(SequenceLayer* layer);
void AudioPlayback_SeqLayerNoteRelease(SequenceLayer* layer);
void AudioPlayback_NoteSetResamplingRate(NoteSampleState* sampleState, f32 resamplingRateInput);
void AudioPlayback_AudioListPushFront(AudioListItem* list, AudioListItem* item);
void AudioPlayback_AudioListRemove(AudioListItem* item);
void AudioPlayback_NoteInitForLayer(Note* note, SequenceLayer* layer);
void AudioPlayback_NoteInit(Note* note);
void AudioPlayback_NoteDisable(Note* note);
s32 AudioPlayback_BuildSyntheticWave(Note* note, SequenceLayer* layer, s32 waveId);
Note* AudioPlayback_FindNodeWithPrioLessThan(AudioListItem* list, s32 limit);
void AudioPlayback_NoteReleaseAndTakeOwnership(Note* note, SequenceLayer* layer);
Note* AudioPlayback_AllocNoteFromDisabled(NotePool* pool, SequenceLayer* layer);
Note* AudioPlayback_AllocNoteFromDecaying(NotePool* pool, SequenceLayer* layer);
Note* AudioPlayback_AllocNoteFromActive(NotePool* pool, SequenceLayer* layer);
void func_801963E8(Note* note, SequenceLayer* layer);

// ===========================================================================

void AudioPlayback_InitSampleState(Note* note, NoteSampleState* sampleState, NoteSubAttributes* subAttrs) {
    f32 volLeft;
    f32 volRight;
    s32 halfPanIndex;
    u8 strongLeft;
    u8 strongRight;
    f32 velocity;
    u8 pan;
    u8 targetReverbVol;
    StereoData stereoData;
    s32 stereoHeadsetEffects = note->playbackState.stereoHeadsetEffects;

    velocity = subAttrs->velocity;
    pan = subAttrs->pan;
    targetReverbVol = subAttrs->targetReverbVol;
    stereoData = subAttrs->stereoData;

    sampleState->bitField0 = note->sampleState.bitField0;
    sampleState->bitField1 = note->sampleState.bitField1;
    sampleState->waveSampleAddr = note->sampleState.waveSampleAddr;
    sampleState->harmonicIndexCurAndPrev = note->sampleState.harmonicIndexCurAndPrev;

    AudioPlayback_NoteSetResamplingRate(sampleState, subAttrs->frequency);

    pan &= 0x7F;

    sampleState->bitField0.strongRight = false;
    sampleState->bitField0.strongLeft = false;
    sampleState->bitField0.strongReverbRight = stereoData.strongReverbRight;
    sampleState->bitField0.strongReverbLeft = stereoData.strongReverbLeft;
    if (stereoHeadsetEffects && (gMmSfx.soundMode == SOUNDMODE_HEADSET)) {
        halfPanIndex = pan >> 1;
        if (halfPanIndex > 0x3F) {
            halfPanIndex = 0x3F;
        }

        sampleState->haasEffectRightDelaySize = gHaasEffectDelaySize[halfPanIndex];
        sampleState->haasEffectLeftDelaySize = gHaasEffectDelaySize[0x3F - halfPanIndex];
        sampleState->bitField1.useHaasEffect = true;

        volLeft = gHeadsetPanVolume[pan];
        volRight = gHeadsetPanVolume[0x7F - pan];
    } else if (stereoHeadsetEffects && (gMmSfx.soundMode == SOUNDMODE_STEREO)) {
        strongLeft = strongRight = false;
        sampleState->haasEffectLeftDelaySize = 0;
        sampleState->haasEffectRightDelaySize = 0;
        sampleState->bitField1.useHaasEffect = false;

        volLeft = gStereoPanVolume[pan];
        volRight = gStereoPanVolume[0x7F - pan];
        if (pan < 0x20) {
            strongLeft = true;
        } else if (pan > 0x60) {
            strongRight = true;
        }

        // case 0:
        sampleState->bitField0.strongRight = strongRight;
        sampleState->bitField0.strongLeft = strongLeft;

        switch (stereoData.type) {
            case 0:
                break;

            case 1:
                sampleState->bitField0.strongRight = stereoData.strongRight;
                sampleState->bitField0.strongLeft = stereoData.strongLeft;
                break;

            case 2:
                sampleState->bitField0.strongRight = stereoData.strongRight | strongRight;
                sampleState->bitField0.strongLeft = stereoData.strongLeft | strongLeft;
                break;

            case 3:
                sampleState->bitField0.strongRight = stereoData.strongRight ^ strongRight;
                sampleState->bitField0.strongLeft = stereoData.strongLeft ^ strongLeft;
                break;

            default:
                break;
        }

    } else if (gMmSfx.soundMode == SOUNDMODE_MONO) {
        sampleState->bitField0.strongReverbRight = false;
        sampleState->bitField0.strongReverbLeft = false;
        volLeft = 0.707f; // approx 1/sqrt(2)
        volRight = 0.707f;
    } else {
        sampleState->bitField0.strongRight = stereoData.strongRight;
        sampleState->bitField0.strongLeft = stereoData.strongLeft;
        volLeft = gDefaultPanVolume[pan];
        volRight = gDefaultPanVolume[0x7F - pan];
    }

    velocity = 0.0f > velocity ? 0.0f : velocity;
    velocity = 1.0f < velocity ? 1.0f : velocity;

    sampleState->targetVolLeft = (s32)((velocity * volLeft) * (0x1000 - 0.001f));
    sampleState->targetVolRight = (s32)((velocity * volRight) * (0x1000 - 0.001f));

    sampleState->gain = subAttrs->gain;
    sampleState->filter = subAttrs->filter;
    sampleState->combFilterSize = subAttrs->combFilterSize;
    sampleState->combFilterGain = subAttrs->combFilterGain;
    sampleState->targetReverbVol = targetReverbVol;
    sampleState->surroundEffectIndex = subAttrs->surroundEffectIndex;
}

void AudioPlayback_NoteSetResamplingRate(NoteSampleState* sampleState, f32 resamplingRateInput) {
    f32 resamplingRate = 0.0f;

    if (resamplingRateInput < 2.0f) {
        sampleState->bitField1.hasTwoParts = false;
        resamplingRate = CLAMP_MAX(resamplingRateInput, 1.99998f);

    } else {
        sampleState->bitField1.hasTwoParts = true;
        if (resamplingRateInput > 3.99996f) {
            if (sampleState->bitField1.isSyntheticWave) {
                resamplingRate = resamplingRateInput * 0.25;
            } else {
                resamplingRate = 1.99998f;
            }
        } else {
            resamplingRate = resamplingRateInput * 0.5f;
        }
    }
    sampleState->frequencyFixedPoint = (s32)(resamplingRate * 32768.0f);
}

void AudioPlayback_NoteInit(Note* note) {
    if (note->playbackState.parentLayer->adsr.decayIndex == 0) {
        AudioEffects_InitAdsr(&note->playbackState.adsr, note->playbackState.parentLayer->channel->adsr.envelope,
                              &note->playbackState.adsrVolScaleUnused);
    } else {
        AudioEffects_InitAdsr(&note->playbackState.adsr, note->playbackState.parentLayer->adsr.envelope,
                              &note->playbackState.adsrVolScaleUnused);
    }

    note->playbackState.status = PLAYBACK_STATUS_0;
    note->playbackState.adsr.action.s.status = ADSR_STATUS_INITIAL;
    note->sampleState = gDefaultSampleState;
}

void AudioPlayback_NoteDisable(Note* note) {
    if (note->sampleState.bitField0.needsInit == true) {
        note->sampleState.bitField0.needsInit = false;
    }
    note->playbackState.priority = 0;
    note->sampleState.bitField0.enabled = false;
    note->playbackState.status = PLAYBACK_STATUS_0;
    note->sampleState.bitField0.finished = false;
    note->playbackState.parentLayer = NO_LAYER;
    note->playbackState.prevParentLayer = NO_LAYER;
    note->playbackState.adsr.action.s.status = ADSR_STATUS_DISABLED;
    note->playbackState.adsr.current = 0;
}

void AudioPlayback_ProcessNotes(void) {
    s32 playbackStatus;
    NoteAttributes* attrs;
    NoteSampleState* sampleState;
    NoteSampleState* noteSampleState;
    Note* note;
    NotePlaybackState* playbackState;
    NoteSubAttributes subAttrs;
    u8 bookOffset;
    f32 adsrVolumeScale;
    s32 i;

    for (i = 0; i < gMmSfx.numNotes; i++) {
        note = &gMmSfx.notes[i];
        sampleState = &gMmSfx.sampleStateList[gMmSfx.sampleStateOffset + i];
        playbackState = &note->playbackState;
        if (playbackState->wantedParentLayer != NO_LAYER || playbackState->priority != 0 ||
            playbackState->parentLayer != NO_LAYER) {
            static int sPnLog = 0;
            if (sPnLog < 16) {
                sPnLog++;
                char b[208];
                snprintf(b, sizeof(b),
                         "PN note[%d]: prio=%d status=%d adsrStatus=%d parent=%p wanted=%p lt0x7FFF=%d",
                         i, playbackState->priority, playbackState->status, playbackState->adsr.action.s.status,
                         (void*)playbackState->parentLayer, (void*)playbackState->wantedParentLayer,
                         (int)((uintptr_t)playbackState->parentLayer < 0x7FFFFFFF));
                MmSfxSynth_Log(b);
            }
        }
        if (playbackState->parentLayer != NO_LAYER) {

            // OTRTODO: This skips playback if the pointer is below where memory on the N64 normally would be.
            // This does not translate well to modern platforms and how they map memory.
            // Considering that this check is not present in OoT/SoH, we may be able to remove this altogether.
            if ((uintptr_t)playbackState->parentLayer < 0x7FFFFFFF) {
                continue;
            }

            if ((note != playbackState->parentLayer->note) && (playbackState->status == PLAYBACK_STATUS_0)) {
                playbackState->adsr.action.s.release = true;
                playbackState->adsr.fadeOutVel = gMmSfx.audioBufferParameters.updatesPerFrameInv;
                playbackState->priority = 1;
                playbackState->status = PLAYBACK_STATUS_2;
                goto out;
            } else if (!playbackState->parentLayer->enabled && (playbackState->status == PLAYBACK_STATUS_0) &&
                       (playbackState->priority >= 1)) {
                // do nothing
            } else if (playbackState->parentLayer->channel->seqPlayer == NULL) {
                AudioScript_SequenceChannelDisable(playbackState->parentLayer->channel);
                playbackState->priority = 1;
                playbackState->status = PLAYBACK_STATUS_1;
                continue;
            } else if (playbackState->parentLayer->channel->seqPlayer->muted &&
                       (playbackState->parentLayer->channel->muteFlags & MUTE_FLAGS_STOP_NOTES)) {
                // do nothing
            } else {
                goto out;
            }

            AudioPlayback_SeqLayerNoteRelease(playbackState->parentLayer);
            AudioPlayback_AudioListRemove(&note->listItem);
            AudioPlayback_AudioListPushFront(&note->listItem.pool->decaying, &note->listItem);
            playbackState->priority = 1;
            playbackState->status = PLAYBACK_STATUS_2;
        } else if ((playbackState->status == PLAYBACK_STATUS_0) && (playbackState->priority >= 1)) {
            continue;
        }

    out:
        if (playbackState->priority != 0) {
            //! FAKE:
            if (1) {}
            noteSampleState = &note->sampleState;
            if ((playbackState->status >= 1) || noteSampleState->bitField0.finished) {
                if ((playbackState->adsr.action.s.status == ADSR_STATUS_DISABLED) ||
                    noteSampleState->bitField0.finished) {
                    if (playbackState->wantedParentLayer != NO_LAYER) {
                        AudioPlayback_NoteDisable(note);
                        if (playbackState->wantedParentLayer->channel != NULL) {
                            AudioPlayback_NoteInitForLayer(note, playbackState->wantedParentLayer);
                            AudioEffects_InitVibrato(note);
                            AudioEffects_InitPortamento(note);
                            AudioPlayback_AudioListRemove(&note->listItem);
                            AudioScript_AudioListPushBack(&note->listItem.pool->active, &note->listItem);
                            playbackState->wantedParentLayer = NO_LAYER;
                            // don't skip
                        } else {
                            AudioPlayback_NoteDisable(note);
                            AudioPlayback_AudioListRemove(&note->listItem);
                            AudioScript_AudioListPushBack(&note->listItem.pool->disabled, &note->listItem);
                            playbackState->wantedParentLayer = NO_LAYER;
                            goto skip;
                        }
                    } else {
                        if (playbackState->parentLayer != NO_LAYER) {
                            playbackState->parentLayer->bit1 = true;
                        }
                        AudioPlayback_NoteDisable(note);
                        AudioPlayback_AudioListRemove(&note->listItem);
                        AudioScript_AudioListPushBack(&note->listItem.pool->disabled, &note->listItem);
                        continue;
                    }
                }
            } else if (playbackState->adsr.action.s.status == ADSR_STATUS_DISABLED) {
                if (playbackState->parentLayer != NO_LAYER) {
                    playbackState->parentLayer->bit1 = true;
                }
                AudioPlayback_NoteDisable(note);
                AudioPlayback_AudioListRemove(&note->listItem);
                AudioScript_AudioListPushBack(&note->listItem.pool->disabled, &note->listItem);
                continue;
            }

            adsrVolumeScale = AudioEffects_UpdateAdsr(&playbackState->adsr);
            AudioEffects_UpdatePortamentoAndVibrato(note);
            playbackStatus = playbackState->status;
            attrs = &playbackState->attributes;
            if ((playbackStatus == PLAYBACK_STATUS_1) || (playbackStatus == PLAYBACK_STATUS_2)) {
                subAttrs.frequency = attrs->freqScale;
                subAttrs.velocity = attrs->velocity;
                subAttrs.pan = attrs->pan;
                subAttrs.targetReverbVol = attrs->targetReverbVol;
                subAttrs.stereoData = attrs->stereoData;
                subAttrs.gain = attrs->gain;
                subAttrs.filter = attrs->filter;
                subAttrs.combFilterSize = attrs->combFilterSize;
                subAttrs.combFilterGain = attrs->combFilterGain;
                subAttrs.surroundEffectIndex = attrs->surroundEffectIndex;
                bookOffset = noteSampleState->bitField1.bookOffset;
            } else {
                SequenceLayer* layer = playbackState->parentLayer;
                SequenceChannel* channel = playbackState->parentLayer->channel;

                subAttrs.frequency = layer->noteFreqScale;
                subAttrs.velocity = layer->noteVelocity;
                subAttrs.pan = layer->notePan;

                if (layer->surroundEffectIndex == 0x80) {
                    subAttrs.surroundEffectIndex = channel->surroundEffectIndex;
                } else {
                    subAttrs.surroundEffectIndex = layer->surroundEffectIndex;
                }

                if (layer->stereoData.type == 0) {
                    subAttrs.stereoData = channel->stereoData;
                } else {
                    subAttrs.stereoData = layer->stereoData;
                }

                if (layer->unk_0A.s.bit_2 == 1) {
                    subAttrs.targetReverbVol = channel->targetReverbVol;
                } else {
                    subAttrs.targetReverbVol = layer->targetReverbVol;
                }

                if (layer->unk_0A.s.bit_9 == 1) {
                    subAttrs.gain = channel->gain;
                } else {
                    subAttrs.gain = 0;
                    //! FAKE:
                    if (1) {}
                }

                subAttrs.filter = channel->filter;
                subAttrs.combFilterSize = channel->combFilterSize;
                subAttrs.combFilterGain = channel->combFilterGain;
                bookOffset = channel->bookOffset & 0x7;

                if (channel->seqPlayer->muted && (channel->muteFlags & MUTE_FLAGS_STOP_SAMPLES)) {
                    subAttrs.frequency = 0.0f;
                    subAttrs.velocity = 0.0f;
                }
            }

            subAttrs.frequency *= playbackState->vibratoFreqScale * playbackState->portamentoFreqScale;
            subAttrs.frequency *= gMmSfx.audioBufferParameters.resampleRate;
            subAttrs.velocity *= adsrVolumeScale;
            AudioPlayback_InitSampleState(note, sampleState, &subAttrs);
            noteSampleState->bitField1.bookOffset = bookOffset;
        skip:;
        }
    }
}

TunedSample* AudioPlayback_GetInstrumentTunedSample(Instrument* instrument, s32 semitone) {
    TunedSample* tunedSample;

    if (semitone < instrument->normalRangeLo) {
        tunedSample = &instrument->lowPitchTunedSample;
    } else if (semitone <= instrument->normalRangeHi) {
        tunedSample = &instrument->normalPitchTunedSample;
    } else {
        tunedSample = &instrument->highPitchTunedSample;
    }

    return tunedSample;
}

Instrument* AudioPlayback_GetInstrumentInner(s32 fontId, s32 instId) {
    Instrument* inst;

    if (fontId == 0xFF) {
        return NULL;
    }

    if (!AudioLoad_IsFontLoadComplete(fontId)) {
        gMmSfx.audioErrorFlags = AUDIO_ERROR(0, fontId, AUDIO_ERROR_FONT_NOT_LOADED);
        return NULL;
    }

    SoundFont* sf = &gMmSfx.soundFontList[fontId];

    if (instId >= sf->numInstruments) {
        return NULL;
    }

    inst = sf->instruments[instId];

    if (inst == NULL) {
        gMmSfx.audioErrorFlags = AUDIO_ERROR(fontId, instId, AUDIO_ERROR_NO_INST);
        return inst;
    }

    return inst;
}

Drum* AudioPlayback_GetDrum(s32 fontId, s32 drumId) {
    Drum* drum = NULL;

    if (fontId == 0xFF) {
        return NULL;
    }

    if (!AudioLoad_IsFontLoadComplete(fontId)) {
        gMmSfx.audioErrorFlags = AUDIO_ERROR(0, fontId, AUDIO_ERROR_FONT_NOT_LOADED);
        return NULL;
    }

    SoundFont* sf = &gMmSfx.soundFontList[fontId];
    if (drumId < sf->numDrums) {
        drum = sf->drums[drumId];
    }

    if (drum == NULL) {
        gMmSfx.audioErrorFlags = AUDIO_ERROR(fontId, drumId, AUDIO_ERROR_NO_DRUM_SFX);
    }

    return drum;
}

SoundEffect* AudioPlayback_GetSoundEffect(s32 fontId, s32 sfxId) {
    SoundEffect* soundEffect = NULL;

    if (fontId == 0xFF) {
        return NULL;
    }

    if (!AudioLoad_IsFontLoadComplete(fontId)) {
        gMmSfx.audioErrorFlags = AUDIO_ERROR(0, fontId, AUDIO_ERROR_FONT_NOT_LOADED);
        return NULL;
    }

    SoundFont* sf = &gMmSfx.soundFontList[fontId];
    if (sfxId < sf->numSfx) {
        soundEffect = &sf->soundEffects[sfxId];
    }

    if (soundEffect == NULL) {
        gMmSfx.audioErrorFlags = AUDIO_ERROR(fontId, sfxId, AUDIO_ERROR_NO_DRUM_SFX);
    }

    if (soundEffect != NULL && soundEffect->tunedSample.sample == NULL) {
        return NULL;
    }

    return soundEffect;
}

void AudioPlayback_SeqLayerDecayRelease(SequenceLayer* layer, s32 target) {
    Note* note;
    NoteAttributes* attrs;
    SequenceChannel* channel;
    s32 i;

    if (layer == NO_LAYER) {
        return;
    }

    layer->bit3 = false;

    if (layer->note == NULL) {
        return;
    }

    note = layer->note;
    attrs = &note->playbackState.attributes;

    if (note->playbackState.wantedParentLayer == layer) {
        note->playbackState.wantedParentLayer = NO_LAYER;
    }

    if (note->playbackState.parentLayer != layer) {
        if (note->playbackState.parentLayer == NO_LAYER && note->playbackState.wantedParentLayer == NO_LAYER &&
            note->playbackState.prevParentLayer == layer && target != ADSR_STATUS_DECAY) {
            note->playbackState.adsr.fadeOutVel = gMmSfx.audioBufferParameters.updatesPerFrameInv;
            note->playbackState.adsr.action.s.release = true;
        }
        return;
    }

    if (note->playbackState.adsr.action.s.status != ADSR_STATUS_DECAY) {
        attrs->freqScale = layer->noteFreqScale;
        attrs->velocity = layer->noteVelocity;
        attrs->pan = layer->notePan;

        if (layer->channel != NULL) {
            channel = layer->channel;

            if (layer->unk_0A.s.bit_2 == 1) {
                attrs->targetReverbVol = channel->targetReverbVol;
            } else {
                attrs->targetReverbVol = layer->targetReverbVol;
            }

            if (layer->surroundEffectIndex == 0x80) {
                attrs->surroundEffectIndex = channel->surroundEffectIndex;
            } else {
                attrs->surroundEffectIndex = layer->surroundEffectIndex;
            }

            if (layer->unk_0A.s.bit_9 == 1) {
                attrs->gain = channel->gain;
            } else {
                attrs->gain = 0;
            }

            attrs->filter = channel->filter;

            if (attrs->filter != NULL) {
                for (i = 0; i < 8; i++) {
                    attrs->filterBuf[i] = attrs->filter[i];
                }
                attrs->filter = attrs->filterBuf;
            }

            attrs->combFilterGain = channel->combFilterGain;
            attrs->combFilterSize = channel->combFilterSize;
            if (channel->seqPlayer->muted && (channel->muteFlags & MUTE_FLAGS_STOP_SAMPLES)) {
                note->sampleState.bitField0.finished = true;
            }

            if (layer->stereoData.asByte == 0) {
                attrs->stereoData = channel->stereoData;
            } else {
                attrs->stereoData = layer->stereoData;
            }
            note->playbackState.priority = channel->someOtherPriority;
        } else {
            attrs->stereoData = layer->stereoData;
            note->playbackState.priority = 1;
        }

        note->playbackState.prevParentLayer = note->playbackState.parentLayer;
        note->playbackState.parentLayer = NO_LAYER;
        if (target == ADSR_STATUS_RELEASE) {
            note->playbackState.adsr.fadeOutVel = gMmSfx.audioBufferParameters.updatesPerFrameInv;
            note->playbackState.adsr.action.s.release = true;
            note->playbackState.status = PLAYBACK_STATUS_2;
        } else {
            note->playbackState.status = PLAYBACK_STATUS_1;
            note->playbackState.adsr.action.s.decay = true;
            if (layer->adsr.decayIndex == 0) {
                note->playbackState.adsr.fadeOutVel = gMmSfx.adsrDecayTable[layer->channel->adsr.decayIndex];
            } else {
                note->playbackState.adsr.fadeOutVel = gMmSfx.adsrDecayTable[layer->adsr.decayIndex];
            }
            note->playbackState.adsr.sustain =
                ((f32)(s32)(layer->channel->adsr.sustain) * note->playbackState.adsr.current) / 256.0f;
        }
    }

    if (target == ADSR_STATUS_DECAY) {
        AudioPlayback_AudioListRemove(&note->listItem);
        AudioPlayback_AudioListPushFront(&note->listItem.pool->decaying, &note->listItem);
    }
}

void AudioPlayback_SeqLayerNoteDecay(SequenceLayer* layer) {
    AudioPlayback_SeqLayerDecayRelease(layer, ADSR_STATUS_DECAY);
}

void AudioPlayback_SeqLayerNoteRelease(SequenceLayer* layer) {
    AudioPlayback_SeqLayerDecayRelease(layer, ADSR_STATUS_RELEASE);
}

/**
 * Extract the synthetic wave to use from gWaveSamples and update corresponding frequencies
 *
 * @param note
 * @param layer
 * @param waveId the index of the type of synthetic wave to use, offset by 128
 * @return harmonicIndex, the index of the harmonic for the synthetic wave contained in gWaveSamples
 */
s32 AudioPlayback_BuildSyntheticWave(Note* note, SequenceLayer* layer, s32 waveId) {
    f32 freqScale;
    f32 freqRatio;
    u8 harmonicIndex;

    if (waveId < 128) {
        waveId = 128;
    }

    freqScale = layer->freqScale;
    if ((layer->portamento.mode != PORTAMENTO_MODE_OFF) && (layer->portamento.extent > 0.0f)) {
        freqScale *= (layer->portamento.extent + 1.0f);
    }

    // Map frequency to the harmonic to use from gWaveSamples
    if (freqScale < 0.99999f) {
        harmonicIndex = 0;
        freqRatio = 1.0465f;
    } else if (freqScale < 1.99999f) {
        harmonicIndex = 1;
        freqRatio = 1.0465f / 2;
    } else if (freqScale < 3.99999f) {
        harmonicIndex = 2;
        freqRatio = 1.0465f / 4 + 1.005E-3;
    } else {
        harmonicIndex = 3;
        freqRatio = 1.0465f / 8 - 2.5E-6;
    }

    // Update results
    layer->freqScale *= freqRatio;
    note->playbackState.waveId = waveId;
    note->playbackState.harmonicIndex = harmonicIndex;

    // Save the pointer to the synthethic wave
    // waveId index starts at 128, there are WAVE_SAMPLE_COUNT samples to read from
    note->sampleState.waveSampleAddr = &gWaveSamples[waveId - 128][harmonicIndex * WAVE_SAMPLE_COUNT];

    return harmonicIndex;
}

void AudioPlayback_InitSyntheticWave(Note* note, SequenceLayer* layer) {
    s32 prevHarmonicIndex;
    s32 curHarmonicIndex;
    s32 waveId = layer->instOrWave;

    if (waveId == 0xFF) {
        waveId = layer->channel->instOrWave;
    }

    prevHarmonicIndex = note->playbackState.harmonicIndex;
    curHarmonicIndex = AudioPlayback_BuildSyntheticWave(note, layer, waveId);

    if (curHarmonicIndex != prevHarmonicIndex) {
        note->sampleState.harmonicIndexCurAndPrev = (curHarmonicIndex << 2) + prevHarmonicIndex;
    }
}

void AudioPlayback_InitNoteList(AudioListItem* list) {
    list->prev = list;
    list->next = list;
    list->u.count = 0;
}

void AudioPlayback_InitNoteLists(NotePool* pool) {
    AudioPlayback_InitNoteList(&pool->disabled);
    AudioPlayback_InitNoteList(&pool->decaying);
    AudioPlayback_InitNoteList(&pool->releasing);
    AudioPlayback_InitNoteList(&pool->active);
    pool->disabled.pool = pool;
    pool->decaying.pool = pool;
    pool->releasing.pool = pool;
    pool->active.pool = pool;
}

void AudioPlayback_InitNoteFreeList(void) {
    s32 i;

    AudioPlayback_InitNoteLists(&gMmSfx.noteFreeLists);
    for (i = 0; i < gMmSfx.numNotes; i++) {
        gMmSfx.notes[i].listItem.u.value = &gMmSfx.notes[i];
        gMmSfx.notes[i].listItem.prev = NULL;
        AudioScript_AudioListPushBack(&gMmSfx.noteFreeLists.disabled, &gMmSfx.notes[i].listItem);
    }
}

void AudioPlayback_NotePoolClear(NotePool* pool) {
    s32 i;
    AudioListItem* source;
    AudioListItem* cur;
    AudioListItem* dest;

    for (i = 0; i < 4; i++) {
        switch (i) {
            case 0:
                source = &pool->disabled;
                dest = &gMmSfx.noteFreeLists.disabled;
                break;

            case 1:
                source = &pool->decaying;
                dest = &gMmSfx.noteFreeLists.decaying;
                break;

            case 2:
                source = &pool->releasing;
                dest = &gMmSfx.noteFreeLists.releasing;
                break;

            case 3:
                source = &pool->active;
                dest = &gMmSfx.noteFreeLists.active;
                break;

            default:
                break;
        }

        while (true) {
            cur = source->next;
            if ((cur == source) || (cur == NULL)) {
                break;
            }
            AudioPlayback_AudioListRemove(cur);
            AudioScript_AudioListPushBack(dest, cur);
        }
    }
}

void AudioPlayback_NotePoolFill(NotePool* pool, s32 count) {
    s32 i;
    s32 j;
    Note* note;
    AudioListItem* source;
    AudioListItem* dest;

    AudioPlayback_NotePoolClear(pool);

    for (i = 0, j = 0; j < count; i++) {
        if (i == 4) {
            return;
        }

        switch (i) {
            case 0:
                source = &gMmSfx.noteFreeLists.disabled;
                dest = &pool->disabled;
                break;

            case 1:
                source = &gMmSfx.noteFreeLists.decaying;
                dest = &pool->decaying;
                break;

            case 2:
                source = &gMmSfx.noteFreeLists.releasing;
                dest = &pool->releasing;
                break;

            case 3:
                source = &gMmSfx.noteFreeLists.active;
                dest = &pool->active;
                break;
        }

        while (j < count) {
            note = (Note*)AudioScript_AudioListPopBack(source);
            if (note == NULL) {
                break;
            }
            AudioScript_AudioListPushBack(dest, &note->listItem);
            j++;
        }
    }
}

void AudioPlayback_AudioListPushFront(AudioListItem* list, AudioListItem* item) {
    // add 'item' to the front of the list given by 'list', if it's not in any list
    if (item->prev == NULL) {
        item->prev = list;
        item->next = list->next;
        list->next->prev = item;
        list->next = item;
        list->u.count++;
        item->pool = list->pool;
    }
}

void AudioPlayback_AudioListRemove(AudioListItem* item) {
    // remove 'item' from the list it's in, if any
    if (item->prev != NULL) {
        item->prev->next = item->next;
        item->next->prev = item->prev;
        item->prev = NULL;
    }
}

Note* AudioPlayback_FindNodeWithPrioLessThan(AudioListItem* list, s32 limit) {
    AudioListItem* cur = list->next;
    AudioListItem* best;

    if (cur == list) {
        return NULL;
    }

    for (best = cur; cur != list; cur = cur->next) {
        if (((Note*)best->u.value)->playbackState.priority >= ((Note*)cur->u.value)->playbackState.priority) {
            best = cur;
        }
    }

    if (best == NULL) {
        return NULL;
    }

    if (limit <= ((Note*)best->u.value)->playbackState.priority) {
        return NULL;
    }

    return (Note*)best->u.value;
}

void AudioPlayback_NoteInitForLayer(Note* note, SequenceLayer* layer) {
    s16 instId;
    SequenceChannel* channel = layer->channel;
    NotePlaybackState* playbackState = &note->playbackState;
    NoteSampleState* noteSampleState = &note->sampleState;

    playbackState->prevParentLayer = NO_LAYER;
    playbackState->parentLayer = layer;
    playbackState->priority = channel->notePriority;
    layer->notePropertiesNeedInit = true;
    layer->bit3 = true;
    layer->note = note;
    channel->noteUnused = note;
    channel->layerUnused = layer;
    layer->noteVelocity = 0.0f;
    AudioPlayback_NoteInit(note);
    instId = layer->instOrWave;

    if (instId == 0xFF) {
        instId = channel->instOrWave;
    }
    noteSampleState->tunedSample = layer->tunedSample;

    if (instId >= 0x80 && instId < 0xC0) {
        noteSampleState->bitField1.isSyntheticWave = true;
    } else {
        noteSampleState->bitField1.isSyntheticWave = false;
    }

    if (noteSampleState->bitField1.isSyntheticWave) {
        AudioPlayback_BuildSyntheticWave(note, layer, instId);
    } else if (channel->startSamplePos == 1) {
        playbackState->startSamplePos = noteSampleState->tunedSample->sample->loop->start;
    } else {
        playbackState->startSamplePos = channel->startSamplePos;
        if (playbackState->startSamplePos >= noteSampleState->tunedSample->sample->loop->loopEnd) {
            playbackState->startSamplePos = 0;
        }
    }

    playbackState->fontId = channel->fontId;
    playbackState->stereoHeadsetEffects = channel->stereoHeadsetEffects;
    noteSampleState->bitField1.reverbIndex = channel->reverbIndex & 3;
}

void func_801963E8(Note* note, SequenceLayer* layer) {
    // similar to Audio_NoteReleaseAndTakeOwnership, hard to say what the difference is
    AudioPlayback_SeqLayerNoteRelease(note->playbackState.parentLayer);
    note->playbackState.wantedParentLayer = layer;
}

void AudioPlayback_NoteReleaseAndTakeOwnership(Note* note, SequenceLayer* layer) {
    note->playbackState.wantedParentLayer = layer;
    note->playbackState.priority = layer->channel->notePriority;

    note->playbackState.adsr.fadeOutVel = gMmSfx.audioBufferParameters.updatesPerFrameInv;
    note->playbackState.adsr.action.s.release = true;
}

Note* AudioPlayback_AllocNoteFromDisabled(NotePool* pool, SequenceLayer* layer) {
    Note* note = (Note*)AudioScript_AudioListPopBack(&pool->disabled);

    if (note != NULL) {
        AudioPlayback_NoteInitForLayer(note, layer);
        AudioPlayback_AudioListPushFront(&pool->active, &note->listItem);
    }
    return note;
}

Note* AudioPlayback_AllocNoteFromDecaying(NotePool* pool, SequenceLayer* layer) {
    Note* note = AudioPlayback_FindNodeWithPrioLessThan(&pool->decaying, layer->channel->notePriority);

    if (note != NULL) {
        AudioPlayback_NoteReleaseAndTakeOwnership(note, layer);
        AudioPlayback_AudioListRemove(&note->listItem);
        AudioScript_AudioListPushBack(&pool->releasing, &note->listItem);
    }
    return note;
}

Note* AudioPlayback_AllocNoteFromActive(NotePool* pool, SequenceLayer* layer) {
    Note* rNote;
    Note* aNote;
    s32 rPriority;
    s32 aPriority;

    rPriority = aPriority = 0x10;
    rNote = AudioPlayback_FindNodeWithPrioLessThan(&pool->releasing, layer->channel->notePriority);

    if (rNote != NULL) {
        rPriority = rNote->playbackState.priority;
    }

    aNote = AudioPlayback_FindNodeWithPrioLessThan(&pool->active, layer->channel->notePriority);

    if (aNote != NULL) {
        aPriority = aNote->playbackState.priority;
    }

    if ((rNote == NULL) && (aNote == NULL)) {
        return NULL;
    }

    if (aPriority < rPriority) {
        AudioPlayback_AudioListRemove(&aNote->listItem);
        func_801963E8(aNote, layer);
        AudioScript_AudioListPushBack(&pool->releasing, &aNote->listItem);
        aNote->playbackState.priority = layer->channel->notePriority;
        return aNote;
    }
    rNote->playbackState.wantedParentLayer = layer;
    rNote->playbackState.priority = layer->channel->notePriority;
    return rNote;
}

Note* AudioPlayback_AllocNote(SequenceLayer* layer) {
    Note* note;
    u32 policy = layer->channel->noteAllocPolicy;

    {
        static int sAllocLog = 0;
        if (sAllocLog < 8) {
            sAllocLog++;
            char b[128];
            snprintf(b, sizeof(b), "AllocNote called: ch=%d policy=%u instOrWave=%d semitone=%d",
                     layer->channel ? layer->channel->channelIndex : -1, policy, layer->instOrWave, layer->semitone);
            MmSfxSynth_Log(b);
        }
    }

    if (policy & 1) {
        note = layer->note;
        if ((note != NULL) && (note->playbackState.prevParentLayer == layer) &&
            (note->playbackState.wantedParentLayer == NO_LAYER)) {
            AudioPlayback_NoteReleaseAndTakeOwnership(note, layer);
            AudioPlayback_AudioListRemove(&note->listItem);
            AudioScript_AudioListPushBack(&note->listItem.pool->releasing, &note->listItem);
            return note;
        }
    }

    if (policy & 2) {
        if (!(note = AudioPlayback_AllocNoteFromDisabled(&layer->channel->notePool, layer)) &&
            !(note = AudioPlayback_AllocNoteFromDecaying(&layer->channel->notePool, layer)) &&
            !(note = AudioPlayback_AllocNoteFromActive(&layer->channel->notePool, layer))) {
            goto null_return;
        }
        return note;
    }

    if (policy & 4) {
        if (!(note = AudioPlayback_AllocNoteFromDisabled(&layer->channel->notePool, layer)) &&
            !(note = AudioPlayback_AllocNoteFromDisabled(&layer->channel->seqPlayer->notePool, layer)) &&
            !(note = AudioPlayback_AllocNoteFromDecaying(&layer->channel->notePool, layer)) &&
            !(note = AudioPlayback_AllocNoteFromDecaying(&layer->channel->seqPlayer->notePool, layer)) &&
            !(note = AudioPlayback_AllocNoteFromActive(&layer->channel->notePool, layer)) &&
            !(note = AudioPlayback_AllocNoteFromActive(&layer->channel->seqPlayer->notePool, layer))) {
            goto null_return;
        }
        return note;
    }

    if (policy & 8) {
        if (!(note = AudioPlayback_AllocNoteFromDisabled(&gMmSfx.noteFreeLists, layer)) &&
            !(note = AudioPlayback_AllocNoteFromDecaying(&gMmSfx.noteFreeLists, layer)) &&
            !(note = AudioPlayback_AllocNoteFromActive(&gMmSfx.noteFreeLists, layer))) {
            goto null_return;
        }
        return note;
    }

    if (!(note = AudioPlayback_AllocNoteFromDisabled(&layer->channel->notePool, layer)) &&
        !(note = AudioPlayback_AllocNoteFromDisabled(&layer->channel->seqPlayer->notePool, layer)) &&
        !(note = AudioPlayback_AllocNoteFromDisabled(&gMmSfx.noteFreeLists, layer)) &&
        !(note = AudioPlayback_AllocNoteFromDecaying(&layer->channel->notePool, layer)) &&
        !(note = AudioPlayback_AllocNoteFromDecaying(&layer->channel->seqPlayer->notePool, layer)) &&
        !(note = AudioPlayback_AllocNoteFromDecaying(&gMmSfx.noteFreeLists, layer)) &&
        !(note = AudioPlayback_AllocNoteFromActive(&layer->channel->notePool, layer)) &&
        !(note = AudioPlayback_AllocNoteFromActive(&layer->channel->seqPlayer->notePool, layer)) &&
        !(note = AudioPlayback_AllocNoteFromActive(&gMmSfx.noteFreeLists, layer))) {
        goto null_return;
    }
    return note;

null_return:
    {
        static int sNullLog = 0;
        if (sNullLog < 8) { sNullLog++; MmSfxSynth_Log("AllocNote -> NULL (no free note in any pool)"); }
    }
    layer->bit3 = true;
    return NULL;
}

void AudioPlayback_NoteInitAll(void) {
    Note* note;
    s32 i;

    for (i = 0; i < gMmSfx.numNotes; i++) {
        note = &gMmSfx.notes[i];
        note->sampleState = gZeroedSampleState;
        note->playbackState.priority = 0;
        note->playbackState.status = PLAYBACK_STATUS_0;
        note->playbackState.parentLayer = NO_LAYER;
        note->playbackState.wantedParentLayer = NO_LAYER;
        note->playbackState.prevParentLayer = NO_LAYER;
        note->playbackState.waveId = 0;
        note->playbackState.attributes.velocity = 0.0f;
        note->playbackState.adsrVolScaleUnused = 0;
        note->playbackState.adsr.action.asByte = 0;
        note->playbackState.vibratoState.active = false;
        note->playbackState.portamento.cur = 0;
        note->playbackState.portamento.speed = 0;
        note->playbackState.stereoHeadsetEffects = false;
        note->playbackState.startSamplePos = 0;
        note->synthesisState.synthesisBuffers =
            (NoteSynthesisBuffers*)AudioHeap_AllocDmaMemory(&gMmSfx.miscPool, sizeof(NoteSynthesisBuffers));
        note->playbackState.attributes.filterBuf = (s16*)AudioHeap_AllocDmaMemory(&gMmSfx.miscPool, FILTER_SIZE);
    }
}

} // namespace mmsfx
