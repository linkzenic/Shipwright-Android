/*
 * mm_sfx_synth_backend.cpp — real-time driver for the isolated MM SFX engine.
 *
 *  - MmSfxSynth_WriteChannelIO / SetChannelState : game thread enqueues IO; the
 *    audio thread drains it (the seqplayer state is touched ONLY on the audio
 *    thread, mirroring MM's command queue, so there are no data races).
 *  - MmSfxSynth_RenderInto : audio thread. Every numSamplesPerUpdate samples it
 *    ticks the sequence once (AudioScript_ProcessSequences -> ProcessNotes fills
 *    sampleStateList), then resamples + mixes every active note's
 *    NoteSampleState into the output, scaled by Volume.Master * Volume.SFX.
 *
 * Sample decoding reuses MmDirectAudio's proven VADPCM decoder (whole-sample
 * decode, cached per Sample*); the backend then resamples from that PCM using
 * each note's frequencyFixedPoint + target volumes from playback.c.
 *
 * See memory: mm_oot_audio_engine_divergence.md
 */
#include "mm_sfx_synth_ctx.h"
#include "mm_sfx_synth_common.h"
#include "mm_sfx_synth.h"

#include <mutex>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <cstdio>

// Real N64 audio-microcode DSP (soh/soh/mixer.c). We drive these EXACTLY as
// 2ship's synthesis.c drives the Acmd list, giving us bit-faithful VADPCM
// decode + the 4-tap pitch-accumulator resampler + the per-sample env-mixer
// volume ramp. Declared here (extern "C") to avoid pulling SoH's audio headers
// (which would clash with the isolated mmsfx types). ADPCM_STATE / RESAMPLE_STATE
// are `short[16]`; flags match libultra/abi.h.
extern "C" {
void aClearBufferImpl(uint16_t addr, int nbytes);
void aLoadBufferImpl(const void* source_addr, uint16_t dest_addr, uint16_t nbytes);
void aSaveBufferImpl(uint16_t source_addr, int16_t* dest_addr, uint16_t nbytes);
void aLoadADPCMImpl(int num_entries_times_16, const int16_t* book_source_addr);
void aSetBufferImpl(uint8_t flags, uint16_t in, uint16_t out, uint16_t nbytes);
void aInterleaveImpl(uint16_t dest, uint16_t left, uint16_t right, uint16_t c);
void aDMEMMoveImpl(uint16_t in_addr, uint16_t out_addr, int nbytes);
void aSetLoopImpl(int16_t* adpcm_loop_state);
void aADPCMdecImpl(uint8_t flags, int16_t* state);
void aResampleImpl(uint8_t flags, uint16_t pitch, int16_t* state);
void aEnvSetup1Impl(uint8_t initial_vol_wet, uint16_t rate_wet, uint16_t rate_left, uint16_t rate_right);
void aEnvSetup2Impl(uint16_t initial_vol_left, uint16_t initial_vol_right);
void aEnvMixerImpl(uint16_t in_addr, uint16_t n_samples, bool swap_reverb, bool neg_3, bool neg_2, bool neg_left,
                   bool neg_right, int32_t wet_dry_addr, uint32_t unk);
void aS8DecImpl(uint8_t flags, int16_t* state);
void aHiLoGainImpl(uint8_t g, uint16_t count, uint16_t addr);
}

// SoH CVar accessor (Volume sliders) — declared to avoid pulling SoH headers in.
extern "C" int CVarGetInteger(const char* name, int defaultValue);
// Log hook (implemented in loader.cpp, which has SoH logging) so we can trace
// how far the audio thread gets before a crash.
extern "C" void MmSfxSynth_Log(const char* msg);

// Whole-sample VADPCM decode, exposed by mm_asset_loader.cpp (reuses the same
// MmDirectAudio_DecodeADPCM that already plays Zora SFX correctly). Returns a
// malloc'd s16 PCM buffer (caller does NOT free; we cache it) and its length.
extern "C" short* MmSfxDecode_Sample(void* soundFontSample, unsigned int* outLen);

namespace mmsfx {

// ProcessSequences lives in seqplayer.cpp (ticks all players + ProcessNotes).
void AudioScript_ProcessSequences(s32 arg0);

extern AudioContext gMmSfx;

// ===========================================================================
// IO command queue (game thread -> audio thread)
// ===========================================================================
struct IoCmd {
    s16 channelIndex;
    s16 ioPort;
    s8 value;
    u8 kind; // 0 = channel IO write, 1 = channel-state write
    f32 volume, freqScale;
    s8 panSigned, stereoBits;
};
static std::mutex sIoMutex;
static std::vector<IoCmd> sIoQueue;

static bool sReady = false;
static const s32 kPlayerIdx = 0; // our SFX sequence runs on isolated player 0

void MmSfxSynth_MarkReady(bool ready) { sReady = ready; } // called by loader

} // namespace mmsfx

extern "C" void MmSfxSynth_WriteChannelIO(int channelIndex, int ioPort, int8_t value) {
    using namespace mmsfx;
    std::lock_guard<std::mutex> lk(sIoMutex);
    IoCmd c{};
    c.kind = 0;
    c.channelIndex = (s16)channelIndex;
    c.ioPort = (s16)ioPort;
    c.value = (s8)value;
    sIoQueue.push_back(c);
}

extern "C" void MmSfxSynth_SetChannelState(int channelIndex, float volume, float freqScale,
                                           int8_t panSigned, int8_t stereoBits) {
    using namespace mmsfx;
    std::lock_guard<std::mutex> lk(sIoMutex);
    IoCmd c{};
    c.kind = 1;
    c.channelIndex = (s16)channelIndex;
    c.volume = volume;
    c.freqScale = freqScale;
    c.panSigned = panSigned;
    c.stereoBits = stereoBits;
    sIoQueue.push_back(c);
}

extern "C" int MmSfxSynth_ReadChannelIO(int channelIndex, int ioPort) {
    using namespace mmsfx;
    if (!sReady || channelIndex < 0 || channelIndex >= SEQ_NUM_CHANNELS || ioPort < 0 || ioPort >= 8) {
        return 0;
    }
    SequenceChannel* ch = gMmSfx.seqPlayers[kPlayerIdx].channels[channelIndex];
    if (ch == &gMmSfx.sequenceChannelNone) return 0;
    return ch->seqScriptIO[ioPort];
}

namespace mmsfx {

// sSfxChannelState lives in the loader TU (custom function reads it).
extern SfxChannelState sSfxChannelState[SEQ_NUM_CHANNELS];

// Drain queued IO/state writes onto the live seq state (audio thread only).
static void DrainIoQueue(void) {
    std::vector<IoCmd> pending;
    {
        std::lock_guard<std::mutex> lk(sIoMutex);
        pending.swap(sIoQueue);
    }
    SequencePlayer* sp = &gMmSfx.seqPlayers[kPlayerIdx];
    for (const IoCmd& c : pending) {
        if (c.channelIndex < 0 || c.channelIndex >= SEQ_NUM_CHANNELS) continue;
        if (c.kind == 1) {
            SfxChannelState* st = &sSfxChannelState[c.channelIndex];
            st->volume = c.volume;
            st->freqScale = c.freqScale;
            st->panSigned = c.panSigned;
            st->stereoBits = c.stereoBits;
        } else {
            if (c.ioPort < 0 || c.ioPort >= 8) continue;
            SequenceChannel* ch = sp->channels[c.channelIndex];
            if (ch != &gMmSfx.sequenceChannelNone) {
                ch->seqScriptIO[c.ioPort] = c.value;
            }
        }
    }
}

// Accumulate `n` stereo samples for one update window into `acc` (interleaved
// s32 L/R, pre-zeroed by the caller). Mixes every active note additively.
// VERBATIM from 2ship synthesis.c:199 (AudioSynth_SyncSampleStates). Called
// after each AudioScript_ProcessSequences, it clears the `enabled` flag on the
// per-update sampleStateList slot of any note that is no longer enabled — so
// stopped notes stop rendering. Skipping this was making dead notes linger
// (stale enabled=1 slots) -> pileup / "everything sounds wrong".
static void AudioSynth_SyncSampleStates(s32 updateIndex) {
    s32 baseIdx = gMmSfx.numNotes * updateIndex;
    for (s32 i = 0; i < gMmSfx.numNotes; i++) {
        NoteSampleState* noteSampleState = &gMmSfx.notes[i].sampleState;
        NoteSampleState* sampleState = &gMmSfx.sampleStateList[baseIdx + i];
        if (noteSampleState->bitField0.enabled) {
            noteSampleState->bitField0.needsInit = false;
        } else {
            sampleState->bitField0.enabled = false;
        }
        noteSampleState->harmonicIndexCurAndPrev = 0;
    }
}

// ===========================================================================
// FAITHFUL per-note synthesis — a direct port of 2ship synthesis.c driving the
// real N64 microcode DSP (soh/soh/mixer.c). See the summary comment at the end
// of this file for the exact mapping of which functions were replicated.
//
// The DSP impls (aADPCMdecImpl/aResampleImpl/aEnvMixerImpl/...) operate on the
// file-static DMEM scratch inside mixer.c keyed by fixed addresses; we drive
// them with the SAME address layout and the SAME call sequence the Acmd list
// would, so the math (VADPCM decode, 4-tap pitch-accumulator resampler, and the
// per-sample volume ramp of the env-mixer) is bit-identical to MM. RenderInto
// runs as a post-process after SoH finished its own synthesis for this buffer,
// so the shared mixer.c `rspa` DMEM is free for our exclusive use.
// ===========================================================================

// --- MM audio constants (from z64audio.h / abi.h) ---
static const s32 MM_SAMPLES_PER_FRAME = 16;   // ADPCMFSIZE
static const s32 MM_SAMPLE_SIZE = 2;          // sizeof(s16)
static const s32 MM_DMEM_1CH_SIZE = 13 * 16 * 2; // 0x1A0
static const s32 MM_DMEM_2CH_SIZE = 2 * MM_DMEM_1CH_SIZE; // 0x340

// DMEM addresses — identical to synthesis.c so the math lands in the same slots.
static const s32 MM_DMEM_TEMP = 0x3B0;
static const s32 MM_DMEM_UNCOMPRESSED_NOTE = 0x570;
static const s32 MM_DMEM_COMPRESSED_ADPCM_DATA = 0x930;
static const s32 MM_DMEM_LEFT_CH = 0x930;
static const s32 MM_DMEM_RIGHT_CH = 0xAD0;
// Wet (reverb) channels — SFX uses no reverb, but the env-mixer always writes
// to them, so we give it valid scratch to keep it from clobbering anything.
static const s32 MM_DMEM_WET_LEFT_CH = 0xC70;
static const s32 MM_DMEM_WET_RIGHT_CH = 0xE10;

// abi flags
static const u8 MM_A_INIT = 0x01;
static const u8 MM_A_CONTINUE = 0x00;
static const u8 MM_A_LOOP = 0x02;
static const u8 MM_A_ADPCM_SHORT = 0x04;

// codecs (mmsfx::SampleCodec)
static const u32 MM_CODEC_ADPCM = 0;
static const u32 MM_CODEC_S8 = 1;
static const u32 MM_CODEC_S16_INMEMORY = 2;
static const u32 MM_CODEC_SMALL_ADPCM = 3;
static const u32 MM_CODEC_S16 = 5;

#define MM_ALIGN16(s) (((s) + 0xF) & ~0xF)

// Pack four (dmem>>4) addresses into the env-mixer's wet_dry word, matching
// AUDIO_MK_CMD(DMEM_LEFT_CH>>4, DMEM_RIGHT_CH>>4, DMEM_WET_LEFT_CH>>4, DMEM_WET_RIGHT_CH>>4).
static inline s32 EnvMixerDefaultDests(void) {
    return (((MM_DMEM_LEFT_CH >> 4) & 0xFF) << 24) | (((MM_DMEM_RIGHT_CH >> 4) & 0xFF) << 16) |
           (((MM_DMEM_WET_LEFT_CH >> 4) & 0xFF) << 8) | ((MM_DMEM_WET_RIGHT_CH >> 4) & 0xFF);
}

// Track the last-loaded ADPCM codebook so we only re-upload it on change
// (mirrors gAudioCtx.adpcmCodeBook). Reset per SynthUpdate so a fresh decode
// always re-loads on the first note that needs it.
static const s16* sLoadedCodeBook = nullptr;

// -------- AudioSynth_ProcessEnvelope (faithful) --------
// Splits the mono signal in DMEM_TEMP into L/R, ramping curVol -> targetVol
// across the update (the aEnvMixer per-sample ramp). SFX has no reverb/haas, so
// the reverb-ramp branch is exercised with curReverbVol=0 (a no-op for dry).
static void ProcessEnvelope(NoteSampleState* sampleState, NoteSynthesisState* synthState, s32 numSamplesPerUpdate) {
    u16 curVolLeft = (u16)synthState->curVolLeft;
    u16 curVolRight = (u16)synthState->curVolRight;

    u16 targetVolLeft = sampleState->targetVolLeft << 4;
    u16 targetVolRight = sampleState->targetVolRight << 4;

    s16 rampLeft = (targetVolLeft != curVolLeft) ? (s16)((targetVolLeft - curVolLeft) / (numSamplesPerUpdate >> 3)) : 0;
    s16 rampRight =
        (targetVolRight != curVolRight) ? (s16)((targetVolRight - curVolRight) / (numSamplesPerUpdate >> 3)) : 0;

    // Reverb ramp (SFX: targetReverbVol is 0, curReverbVol initialised to it).
    s16 curReverbVolAndFlags = (s16)synthState->curReverbVol;
    s32 curReverbVol = curReverbVolAndFlags & 0x7F;
    s16 targetReverbVol = sampleState->targetReverbVol;
    s16 rampReverb;
    if (curReverbVolAndFlags != targetReverbVol) {
        rampReverb = (s16)((((targetReverbVol & 0x7F) - curReverbVol) << 9) / (numSamplesPerUpdate >> 3));
        synthState->curReverbVol = targetReverbVol;
    } else {
        rampReverb = 0;
    }

    // Advance the stored curVol by exactly what the env-mixer will ramp through.
    synthState->curVolLeft = (s16)(curVolLeft + (rampLeft * (numSamplesPerUpdate >> 3)));
    synthState->curVolRight = (s16)(curVolRight + (rampRight * (numSamplesPerUpdate >> 3)));

    aEnvSetup1Impl((u8)(curReverbVol * 2), (u16)rampReverb, (u16)rampLeft, (u16)rampRight);
    aEnvSetup2Impl(curVolLeft, curVolRight);

    aEnvMixerImpl((u16)MM_DMEM_TEMP, (u16)numSamplesPerUpdate, (curReverbVolAndFlags & 0x80) >> 7,
                  sampleState->bitField0.strongReverbRight, sampleState->bitField0.strongReverbLeft,
                  sampleState->bitField0.strongRight, sampleState->bitField0.strongLeft, EnvMixerDefaultDests(), 0);
}

// -------- AudioSynth_ProcessSample (faithful, real-ADPCM/S16/S8 path) --------
// Decodes/loads numSamplesPerUpdate worth of samples into DMEM_UNCOMPRESSED_NOTE
// (honoring the AdpcmLoop), 4-tap-resamples to pitch into DMEM_TEMP, applies
// gain, then env-mixes into the shared DMEM_LEFT_CH / DMEM_RIGHT_CH.
// Returns false (and disables the note) when a non-looping sample ends.
static void ProcessSample(s32 noteIndex, NoteSampleState* sampleState, NoteSynthesisState* synthState,
                          s32 numSamplesPerUpdate) {
    Sample* sample = sampleState->tunedSample ? sampleState->tunedSample->sample : nullptr;
    if (sample == nullptr || sample->sampleAddr == nullptr || sample->loop == nullptr) {
        sampleState->bitField0.enabled = 0;
        return;
    }
    AdpcmLoop* loopInfo = sample->loop;
    Note* note = &gMmSfx.notes[noteIndex];

    u8 flags = MM_A_CONTINUE;

    if (sampleState->bitField0.needsInit) {
        flags = MM_A_INIT;
        synthState->atLoopPoint = false;
        synthState->stopLoop = false;
        synthState->samplePosInt = note->playbackState.startSamplePos;
        synthState->samplePosFrac = 0;
        synthState->curVolLeft = 0;
        synthState->curVolRight = 0;
        synthState->curReverbVol = sampleState->targetReverbVol;
        synthState->numParts = 0;
        synthState->combFilterNeedsInit = true;
        note->sampleState.bitField0.finished = false;
    }

    s32 finished = sampleState->bitField0.finished;

    // numSamplesToLoad from frequency (UQ16.16 accumulator).
    u16 frequencyFixedPoint = sampleState->frequencyFixedPoint;
    u32 numSamplesToLoadFixedPoint =
        (frequencyFixedPoint * numSamplesPerUpdate * 2) + synthState->samplePosFrac;
    s32 numSamplesToLoad = numSamplesToLoadFixedPoint >> 16;
    synthState->samplePosFrac = numSamplesToLoadFixedPoint & 0xFFFF;
    synthState->numParts = 1; // we do not split into two parts; see summary.

    s32 skipBytes = 0;
    s16 sampleDmemBeforeResampling = MM_DMEM_UNCOMPRESSED_NOTE;

    if (note->playbackState.status != 0 /* PLAYBACK_STATUS_0 */) {
        synthState->stopLoop = true;
    }

    s32 sampleEndPos;
    if ((loopInfo->count == 2) && synthState->stopLoop) {
        sampleEndPos = loopInfo->sampleEnd;
    } else {
        sampleEndPos = loopInfo->loopEnd;
    }

    u8* sampleAddr = sample->sampleAddr;
    s32 numSamplesToLoadAdj = numSamplesToLoad;
    s32 numSamplesProcessed = 0;
    s32 dmemUncompressedAddrOffset1 = 0;

    // Upload the ADPCM codebook on change (bookOffset handling omitted: SFX
    // uses bookOffset 0/2/3 which all resolve to sample->book->codeBook).
    if ((sample->codec == MM_CODEC_ADPCM) || (sample->codec == MM_CODEC_SMALL_ADPCM)) {
        if (sLoadedCodeBook != sample->book->codeBook) {
            sLoadedCodeBook = sample->book->codeBook;
            u32 numEntries = MM_SAMPLES_PER_FRAME * sample->book->order * sample->book->numPredictors;
            aLoadADPCMImpl(numEntries, sample->book->codeBook);
        }
    }

    while (numSamplesProcessed != numSamplesToLoadAdj) {
        s32 sampleFinished = false;
        s32 loopToPoint = false;
        s32 dmemUncompressedAddrOffset2 = 0;

        s32 numFirstFrameSamplesToIgnore = synthState->samplePosInt & 0xF;
        s32 numSamplesUntilEnd = sampleEndPos - synthState->samplePosInt;
        s32 numSamplesToProcess = numSamplesToLoadAdj - numSamplesProcessed;

        if ((numFirstFrameSamplesToIgnore == 0) && !synthState->atLoopPoint) {
            numFirstFrameSamplesToIgnore = MM_SAMPLES_PER_FRAME;
        }
        s32 numSamplesInFirstFrame = MM_SAMPLES_PER_FRAME - numFirstFrameSamplesToIgnore;

        s32 numSamplesToDecode;
        s32 numTrailingSamplesToIgnore;
        s32 numFramesToDecode;
        if (numSamplesToProcess < numSamplesUntilEnd) {
            numFramesToDecode = (s32)(numSamplesToProcess - numSamplesInFirstFrame + MM_SAMPLES_PER_FRAME - 1) /
                                MM_SAMPLES_PER_FRAME;
            numSamplesToDecode = numFramesToDecode * MM_SAMPLES_PER_FRAME;
            numTrailingSamplesToIgnore = numSamplesInFirstFrame + numSamplesToDecode - numSamplesToProcess;
        } else {
            numSamplesToDecode = numSamplesUntilEnd - numSamplesInFirstFrame;
            numTrailingSamplesToIgnore = 0;
            if (numSamplesToDecode <= 0) {
                numSamplesToDecode = 0;
                numSamplesInFirstFrame = numSamplesUntilEnd;
            }
            numFramesToDecode = (numSamplesToDecode + MM_SAMPLES_PER_FRAME - 1) / MM_SAMPLES_PER_FRAME;
            if (loopInfo->count != 0) {
                if ((loopInfo->count == 2) && synthState->stopLoop) {
                    sampleFinished = true;
                } else {
                    loopToPoint = true;
                }
            } else {
                sampleFinished = true;
            }
        }

        s32 frameSize = 0;
        s32 skipInitialSamples = MM_SAMPLES_PER_FRAME;
        s32 zeroOffset = 0;
        bool isUncompressed = false; // S16 / S16_INMEMORY: raw load, no decode

        switch (sample->codec) {
            case MM_CODEC_ADPCM:
                frameSize = 9;
                break;
            case MM_CODEC_SMALL_ADPCM:
                frameSize = 5;
                break;
            case MM_CODEC_S8:
                frameSize = 16;
                break;
            case MM_CODEC_S16_INMEMORY:
            case MM_CODEC_S16:
                isUncompressed = true;
                break;
            default:
                // Unsupported codec — stop the note.
                sampleState->bitField0.enabled = 0;
                return;
        }

        if (isUncompressed) {
            // Clear then raw-load the s16 PCM directly into DMEM.
            aClearBufferImpl((u16)MM_DMEM_UNCOMPRESSED_NOTE,
                             (numSamplesToLoadAdj + MM_SAMPLES_PER_FRAME) * MM_SAMPLE_SIZE);
            flags = MM_A_CONTINUE;
            skipBytes = 0;
            numSamplesProcessed += numSamplesToLoadAdj;
            dmemUncompressedAddrOffset1 = numSamplesToLoadAdj;

            size_t bytesToRead;
            if (((synthState->samplePosInt * 2) + (numSamplesToLoadAdj * MM_SAMPLE_SIZE)) < (s32)sample->size) {
                bytesToRead = numSamplesToLoadAdj * MM_SAMPLE_SIZE;
            } else {
                bytesToRead = sample->size - (synthState->samplePosInt * 2);
            }
            aLoadBufferImpl(sampleAddr + (synthState->samplePosInt * 2), (u16)MM_DMEM_UNCOMPRESSED_NOTE,
                            (u16)bytesToRead);
            // fall through to the post-decode bookkeeping below (skip label)
        } else {
            // Move the compressed raw sample chunk from RAM into DMEM.
            s32 sampleDataChunkAlignPad = 0;
            if (numFramesToDecode != 0) {
                s32 frameIndex =
                    (synthState->samplePosInt + skipInitialSamples - numFirstFrameSamplesToIgnore) / MM_SAMPLES_PER_FRAME;
                s32 sampleAddrOffset = frameIndex * frameSize;
                u8* samplesToLoadAddr = sampleAddr + (zeroOffset + sampleAddrOffset);

                sampleDataChunkAlignPad = (uintptr_t)samplesToLoadAddr & 0xF;
                s32 sampleDataChunkSize = MM_ALIGN16((numFramesToDecode * frameSize) + MM_SAMPLES_PER_FRAME);
                s16 sampleDataDmemAddr = MM_DMEM_COMPRESSED_ADPCM_DATA - sampleDataChunkSize;
                aLoadBufferImpl(samplesToLoadAddr - sampleDataChunkAlignPad, (u16)sampleDataDmemAddr,
                                (u16)sampleDataChunkSize);
            } else {
                numSamplesToDecode = 0;
                sampleDataChunkAlignPad = 0;
            }

            if (synthState->atLoopPoint) {
                aSetLoopImpl(sample->loop->predictorState);
                flags = MM_A_LOOP;
                synthState->atLoopPoint = false;
            }

            s32 numSamplesInThisIteration =
                numSamplesToDecode + numSamplesInFirstFrame - numTrailingSamplesToIgnore;

            if (numSamplesProcessed == 0) {
                skipBytes = numFirstFrameSamplesToIgnore * MM_SAMPLE_SIZE;
            } else {
                dmemUncompressedAddrOffset2 = MM_ALIGN16(dmemUncompressedAddrOffset1 + 8 * MM_SAMPLE_SIZE);
            }

            // Decode into DMEM_UNCOMPRESSED_NOTE.
            s32 sampleDataChunkSize = MM_ALIGN16((numFramesToDecode * frameSize) + MM_SAMPLES_PER_FRAME);
            s16 sampleDataDmemAddr = MM_DMEM_COMPRESSED_ADPCM_DATA - sampleDataChunkSize;
            switch (sample->codec) {
                case MM_CODEC_ADPCM:
                    aSetBufferImpl(0, (u16)(sampleDataDmemAddr + sampleDataChunkAlignPad),
                                   (u16)(MM_DMEM_UNCOMPRESSED_NOTE + dmemUncompressedAddrOffset2),
                                   (u16)(numSamplesToDecode * MM_SAMPLE_SIZE));
                    aADPCMdecImpl(flags, synthState->synthesisBuffers->adpcmState);
                    break;
                case MM_CODEC_SMALL_ADPCM:
                    aSetBufferImpl(0, (u16)(sampleDataDmemAddr + sampleDataChunkAlignPad),
                                   (u16)(MM_DMEM_UNCOMPRESSED_NOTE + dmemUncompressedAddrOffset2),
                                   (u16)(numSamplesToDecode * MM_SAMPLE_SIZE));
                    aADPCMdecImpl(flags | MM_A_ADPCM_SHORT, synthState->synthesisBuffers->adpcmState);
                    break;
                case MM_CODEC_S8:
                    aSetBufferImpl(0, (u16)(sampleDataDmemAddr + sampleDataChunkAlignPad),
                                   (u16)(MM_DMEM_UNCOMPRESSED_NOTE + dmemUncompressedAddrOffset2),
                                   (u16)(numSamplesToDecode * MM_SAMPLE_SIZE));
                    aS8DecImpl(flags, synthState->synthesisBuffers->adpcmState);
                    break;
                default:
                    break;
            }

            if (numSamplesProcessed != 0) {
                aDMEMMoveImpl(
                    (u16)(MM_DMEM_UNCOMPRESSED_NOTE + dmemUncompressedAddrOffset2 +
                          (numFirstFrameSamplesToIgnore * MM_SAMPLE_SIZE)),
                    (u16)(MM_DMEM_UNCOMPRESSED_NOTE + dmemUncompressedAddrOffset1),
                    numSamplesInThisIteration * MM_SAMPLE_SIZE);
            }

            numSamplesProcessed += numSamplesInThisIteration;

            switch (flags) {
                case MM_A_INIT:
                    skipBytes = MM_SAMPLES_PER_FRAME * MM_SAMPLE_SIZE;
                    dmemUncompressedAddrOffset1 = (numSamplesToDecode + MM_SAMPLES_PER_FRAME) * MM_SAMPLE_SIZE;
                    break;
                case MM_A_LOOP:
                    dmemUncompressedAddrOffset1 =
                        numSamplesInThisIteration * MM_SAMPLE_SIZE + dmemUncompressedAddrOffset1;
                    break;
                default:
                    if (dmemUncompressedAddrOffset1 != 0) {
                        dmemUncompressedAddrOffset1 =
                            numSamplesInThisIteration * MM_SAMPLE_SIZE + dmemUncompressedAddrOffset1;
                    } else {
                        dmemUncompressedAddrOffset1 =
                            (numFirstFrameSamplesToIgnore + numSamplesInThisIteration) * MM_SAMPLE_SIZE;
                    }
                    break;
            }
            flags = MM_A_CONTINUE;
        }

        // skip: post-decode advance
        if (sampleFinished) {
            if ((numSamplesToLoadAdj - numSamplesProcessed) != 0) {
                aClearBufferImpl((u16)(MM_DMEM_UNCOMPRESSED_NOTE + dmemUncompressedAddrOffset1),
                                 (numSamplesToLoadAdj - numSamplesProcessed) * MM_SAMPLE_SIZE);
            }
            finished = true;
            note->sampleState.bitField0.finished = true;
            break;
        } else if (loopToPoint) {
            synthState->atLoopPoint = true;
            synthState->samplePosInt = loopInfo->start;
        } else {
            synthState->samplePosInt += numSamplesToProcess;
        }

        if (isUncompressed) {
            // raw-load path processed everything in one shot
            break;
        }
    }

    sampleDmemBeforeResampling = MM_DMEM_UNCOMPRESSED_NOTE + skipBytes;

    // Resample flags: A_INIT only on the very first update for this note.
    u8 resampleFlags = MM_A_CONTINUE;
    if (sampleState->bitField0.needsInit) {
        sampleState->bitField0.needsInit = false;
        resampleFlags = MM_A_INIT;
    }

    // Final resample (4-tap pitch-accumulator) into DMEM_TEMP.
    if (frequencyFixedPoint == 0) {
        aClearBufferImpl((u16)MM_DMEM_TEMP, numSamplesPerUpdate * MM_SAMPLE_SIZE);
    } else {
        aSetBufferImpl(0, (u16)sampleDmemBeforeResampling, (u16)MM_DMEM_TEMP, (u16)(numSamplesPerUpdate * MM_SAMPLE_SIZE));
        aResampleImpl(resampleFlags, frequencyFixedPoint, synthState->synthesisBuffers->finalResampleState);
    }

    // Apply gain (UQ4.4; 0x10 == 1.0). 0 means "leave unchanged".
    s32 gain = sampleState->gain;
    if (gain != 0) {
        if (gain < 0x10) {
            gain = 0x10;
        }
        aHiLoGainImpl((u8)gain, (u16)((numSamplesPerUpdate + MM_SAMPLES_PER_FRAME) * MM_SAMPLE_SIZE),
                      (u16)MM_DMEM_TEMP);
    }

    // Envelope mix (volume ramp + pan) into DMEM_LEFT_CH / DMEM_RIGHT_CH.
    ProcessEnvelope(sampleState, synthState, numSamplesPerUpdate);

    // If a non-looping sample reached its end, retire the note this update.
    if (finished) {
        sampleState->bitField0.enabled = 0;
        note->sampleState.bitField0.enabled = 0;
    }
}

static void SynthUpdate(s32* acc, s32 n) {
    const s32 base = gMmSfx.sampleStateOffset;
    const s32 numNotes = gMmSfx.numNotes;

    // The env-mixer accumulates ALL notes into the shared L/R DMEM channels; the
    // DSP requires frame (8-sample) alignment, so quantise n down for the DSP.
    s32 numSamplesPerUpdate = n & ~7;
    if (numSamplesPerUpdate <= 0) {
        return;
    }

    // Mirror AudioSynth_ProcessSamples: clear the dry+wet channels once, then
    // every note env-mixes into them. (gAudioCtx.adpcmCodeBook = NULL.)
    sLoadedCodeBook = nullptr;
    aClearBufferImpl((u16)MM_DMEM_LEFT_CH, MM_DMEM_2CH_SIZE);
    aClearBufferImpl((u16)MM_DMEM_WET_LEFT_CH, MM_DMEM_2CH_SIZE);

    for (s32 i = 0; i < numNotes && i < 64; i++) {
        NoteSampleState* ss = &gMmSfx.sampleStateList[base + i];
        if (ss->bitField0.enabled) {
            static int sLogActive = 0;
            if (sLogActive < 14) {
                sLogActive++;
                char b[176];
                double tun = (ss->tunedSample && ss->tunedSample->sample) ? (double)ss->tunedSample->tuning : -1.0;
                snprintf(b, sizeof(b),
                         "ENABLED note i=%d twoParts=%d tuning=%.4f freq=%u(ratio=%.3f) volL=%u volR=%u gain=%u",
                         i, ss->bitField1.hasTwoParts, tun, ss->frequencyFixedPoint,
                         (double)ss->frequencyFixedPoint / 32768.0 * (ss->bitField1.hasTwoParts ? 2.0 : 1.0),
                         ss->targetVolLeft, ss->targetVolRight, ss->gain);
                MmSfxSynth_Log(b);
            }
        }
        if (!ss->bitField0.enabled || ss->bitField1.isSyntheticWave) {
            continue; // synthetic waves intentionally skipped (see summary)
        }
        ProcessSample(i, ss, &gMmSfx.notes[i].synthesisState, numSamplesPerUpdate);
    }

    // Interleave L/R -> DMEM_TEMP, then accumulate into the caller's s32 buffer.
    aInterleaveImpl((u16)MM_DMEM_TEMP, (u16)MM_DMEM_LEFT_CH, (u16)MM_DMEM_RIGHT_CH,
                    (u16)(numSamplesPerUpdate * MM_SAMPLE_SIZE));

    // Read the interleaved s16 stereo result straight out of DMEM_TEMP.
    s16 stereo[2 * 184 + 16];
    aSaveBufferImpl((u16)MM_DMEM_TEMP, stereo, (u16)(numSamplesPerUpdate * 2 * MM_SAMPLE_SIZE));
    for (s32 s = 0; s < numSamplesPerUpdate; s++) {
        acc[s * 2 + 0] += (s32)stereo[s * 2 + 0];
        acc[s * 2 + 1] += (s32)stereo[s * 2 + 1];
    }
}

/*
 * =====================================================================
 * FAITHFUL-PORT SUMMARY — what was replicated and what was simplified.
 * =====================================================================
 * Ported VERBATIM (logic copied from 2ship mm/src/audio/lib/synthesis.c,
 * driving the real microcode DSP in soh/soh/mixer.c):
 *   - AudioSynth_ProcessSamples (caller)  -> SynthUpdate: clear dry+wet DMEM
 *       channels once, env-mix every enabled note into them, then
 *       aInterleave -> read out -> accumulate. (aClearBuffer/aInterleave)
 *   - AudioSynth_ProcessSample           -> ProcessSample: the full
 *       per-note decode loop (numSamplesToLoad from frequencyFixedPoint +
 *       samplePosFrac, frame-accurate numFirstFrameSamplesToIgnore /
 *       numSamplesUntilEnd / numFramesToDecode bookkeeping, AdpcmLoop
 *       handling via loopInfo->count/start/loopEnd/sampleEnd, atLoopPoint
 *       + aSetLoop(predictorState), A_INIT/A_LOOP/A_CONTINUE flag flow,
 *       codebook upload on change). Drives aLoadADPCM / aSetBuffer /
 *       aADPCMdec (+ A_ADPCM_SHORT) / aS8Dec / aDMEMMove / aClearBuffer /
 *       aLoadBuffer exactly as the Acmd list would.
 *   - AudioSynth_FinalResample           -> the aSetBuffer + aResample pair
 *       (4-tap resampler with the 16.16 pitch accumulator + per-note
 *       finalResampleState history). pitch == frequencyFixedPoint.
 *   - AudioSynth_ProcessEnvelope         -> ProcessEnvelope: curVol ->
 *       targetVol<<4 ramp (rampLeft/Right = delta / (n>>3)), reverb ramp,
 *       aEnvSetup1/aEnvSetup2/aEnvMixer (the per-sample volume ramp). curVol
 *       persists in synthState->curVolLeft/Right — this is the volume ramp
 *       that fixes the distorted "instant target volume" sound.
 *   - HiLoGain (sampleState->gain, UQ4.4, clamped to >=0x10) via aHiLoGain.
 *
 * Codecs handled: CODEC_ADPCM, CODEC_SMALL_ADPCM, CODEC_S8, CODEC_S16 /
 *   CODEC_S16_INMEMORY (raw load). CODEC_REVERB/OPUS/UNK are stopped.
 *
 * INTENTIONALLY OMITTED (SFX engine does not use these; left as no-ops):
 *   - Reverb (SynthesisReverb ring buffers, wet save/load, decay, leak,
 *     filter-reverb, MixOtherReverbIndex). The env-mixer still writes the wet
 *     channels into scratch DMEM (cleared each update) but nothing reads them.
 *   - Haas effect (useHaasEffect / AudioSynth_ApplyHaasEffect) and the
 *     surround-sound effect (AudioSynth_ApplySurroundEffect / gDefaultPanVolume).
 *   - Comb filter (combFilterSize/Gain) and the per-note convolution filter
 *     (sampleState->filter / aFilter).
 *   - Synthetic-wave notes (bitField1.isSyntheticWave) — skipped via continue.
 *   - The two-part split (hasTwoParts): we force numParts = 1. SFX frequencies
 *     stay within the resampler's range, so the split is not required.
 *   - bookOffset 1 (gInvalidAdpcmCodeBook) / bookOffset 3 (UnkCmd19 no-op).
 * =====================================================================
 */

} // namespace mmsfx

// ===========================================================================
// Audio-thread entry: tick the sequence + mix into the output buffer.
// ===========================================================================
extern "C" void MmSfxSynth_RenderInto(int16_t* outBuf, uint32_t numSamples) {
    using namespace mmsfx;
    if (!sReady) return;

    DrainIoQueue();

    const s32 perUpdate = gMmSfx.audioBufferParameters.numSamplesPerUpdate;
    const s32 updatesPerFrame = gMmSfx.audioBufferParameters.updatesPerFrame;

    // master/sfx volume (0..1)
    f32 masterVol = (f32)CVarGetInteger("gSettings.Volume.Master", 40) / 100.0f;
    f32 sfxVol = (f32)CVarGetInteger("gSettings.Volume.SFX", 100) / 100.0f;
    f32 outScale = masterVol * sfxVol;

    static s32 sUpdateIndex = 0;       // cycles 0..updatesPerFrame-1
    static s32 sSamplesUntilTick = 0;  // samples remaining before next seq tick

    // Private scratch accumulator so the master*sfx volume is applied to OUR
    // contribution only, then additively mixed into the shared output buffer.
    static const s32 kScratchMax = 2048; // stereo frames
    static s32 sScratch[kScratchMax * 2];

    // One-shot stage markers: pinpoint an audio-thread crash on first run.
    static bool sLogEntry = false, sLogTick = false, sLogSynth = false, sLogDone = false;
    if (!sLogEntry) { sLogEntry = true; MmSfxSynth_Log("RenderInto: first call entered"); }

    s32 produced = 0;
    while (produced < (s32)numSamples) {
        if (sSamplesUntilTick <= 0) {
            if (!sLogTick) { sLogTick = true; MmSfxSynth_Log("RenderInto: calling ProcessSequences (first tick)"); }
            s32 arg0 = updatesPerFrame - 1 - sUpdateIndex;
            AudioScript_ProcessSequences(arg0);
            // 2ship synthesis.c:230 — sync the per-update sampleStateList right
            // after processing sequences (clears stale enabled slots).
            mmsfx::AudioSynth_SyncSampleStates(gMmSfx.numNotes > 0 ? gMmSfx.sampleStateOffset / gMmSfx.numNotes : 0);
            if (sLogTick && !sLogSynth) { MmSfxSynth_Log("RenderInto: ProcessSequences returned OK"); }
            sUpdateIndex = (sUpdateIndex + 1) % (updatesPerFrame > 0 ? updatesPerFrame : 1);
            sSamplesUntilTick = perUpdate > 0 ? perUpdate : (s32)numSamples;
        }
        s32 chunk = sSamplesUntilTick;
        if (chunk > (s32)numSamples - produced) chunk = (s32)numSamples - produced;
        if (chunk > kScratchMax) chunk = kScratchMax;

        memset(sScratch, 0, sizeof(s32) * chunk * 2);
        if (!sLogSynth) { sLogSynth = true; MmSfxSynth_Log("RenderInto: calling SynthUpdate (first synth)"); }
        mmsfx::SynthUpdate(sScratch, chunk);
        if (!sLogDone) { sLogDone = true; MmSfxSynth_Log("RenderInto: SynthUpdate returned OK — first frame mixed"); }

        for (s32 k = 0; k < chunk * 2; k++) {
            s32 mixed = (s32)outBuf[produced * 2 + k] + (s32)(sScratch[k] * outScale);
            outBuf[produced * 2 + k] = (s16)CLAMP(mixed, -32768, 32767);
        }

        produced += chunk;
        sSamplesUntilTick -= chunk;
    }

    // Periodic state diagnostic (~every 256 callbacks): is the sequence
    // advancing, are channels enabled, are IO ports set, any active notes?
    static int sDiagN = 0;
    if (((++sDiagN) & 0xFF) == 0) {
        SequencePlayer* sp = &gMmSfx.seqPlayers[0];
        int enCh = 0, ioSet = 0, actNotes = 0;
        for (int c = 0; c < SEQ_NUM_CHANNELS; c++) {
            SequenceChannel* ch = sp->channels[c];
            if (ch == &gMmSfx.sequenceChannelNone) continue;
            if (ch->enabled) enCh++;
            for (int p = 0; p < 8; p++) {
                if (ch->seqScriptIO[p] != 0 && ch->seqScriptIO[p] != -1) { ioSet++; break; }
            }
        }
        for (int i = 0; i < gMmSfx.numNotes; i++) {
            if (gMmSfx.sampleStateList[i].bitField0.enabled) actNotes++;
        }
        char b[176];
        snprintf(b, sizeof(b),
                 "diag: seqEnabled=%d scriptCtr=%u tempoAcc=%u enabledCh=%d chWithIO=%d activeNotes=%d",
                 (int)sp->enabled, sp->scriptCounter, sp->tempoAcc, enCh, ioSet, actNotes);
        MmSfxSynth_Log(b);
    }
}
