/*
 * mm_sfx_synth.h — public C API for the isolated MM SFX synth (Ruta B).
 *
 * The rest of the mod talks to the MM SFX engine ONLY through this header:
 *   - boot/init + asset load (Sequence_0 + Soundfont_0/1 from mm.o2r)
 *   - per-SFX channel-IO dispatch (mirrors MM AUDIOCMD_CHANNEL_SET_IO)
 *   - the audio-thread render/mix entry (called from MmDirectAudio_MixInto)
 *
 * Implementation spans (all namespace mmsfx internally):
 *   mm_sfx_synth_{seqplayer,playback,effects,data,glue,loader,backend}.cpp
 *
 * See memory: mm_oot_audio_engine_divergence.md
 */
#ifndef MM_SFX_SYNTH_H
#define MM_SFX_SYNTH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the engine, load Sequence_0 + Soundfont_0/1, start the SFX
// sequence, and register the freq/stereo custom function + per-channel sfxState.
// Idempotent: safe to call every frame; real work happens once mm.o2r is
// available. Returns 1 when the engine is ready to play SFX.
int MmSfxSynth_Init(void);
int MmSfxSynth_IsReady(void);

// Per-SFX dispatch. Writes one of the running SFX sequence's channel IO ports
// (port 0 = enable, 2 = volume, 4 = sfxId low byte, 5 = sfxId high bits;
// port 1 is the seq's read-back "done" flag). channelIndex is 0..15.
void MmSfxSynth_WriteChannelIO(int channelIndex, int ioPort, int8_t value);
int MmSfxSynth_ReadChannelIO(int channelIndex, int ioPort);

// Set the per-channel SFX state (freq/volume/stereo) that the seq's 0xBE custom
// function reads back. Mirrors MM's sSfxChannelState[channelIndex].
void MmSfxSynth_SetChannelState(int channelIndex, float volume, float freqScale,
                                int8_t panSigned, int8_t stereoBits);

// Audio thread: advance the SFX sequence and mix its output into a 32 kHz
// stereo-interleaved s16 buffer (same format/sample-rate as MmDirectAudio).
// Applies Volume.Master * Volume.SFX. No-op until ready.
void MmSfxSynth_RenderInto(int16_t* outBuf, uint32_t numSamples);

#ifdef __cplusplus
}
#endif

#endif // MM_SFX_SYNTH_H
