/**
 * @file mm_audio_sfx_dispatch.cpp
 * @brief Bridge between the MM SFX engine (mm_audio_sfx.cpp) and the actual
 *        sample renderer (MmDirectAudio_Play in mm_asset_loader.cpp).
 *
 * In vanilla MM, AudioSfx_PlayActiveSfx writes IO ports on SEQ_PLAYER_SFX:
 *   - ioPort 0 = 1 (enable this channel)
 *   - ioPort 4 = sfxId & 0xFF (low byte of index)
 *   - ioPort 5 = upper bits / flags
 * The NA_BGM_GENERAL_SFX sequence then reads those ports and triggers a
 * note-on with the correct soundfont sample, frequency, volume, and reverb.
 *
 * Here we short-circuit that: instead of writing IO ports, we directly call
 * MmDirectAudio_Play with the already-resolved spatial parameters from the
 * bank entry. The bank tables (importance, distRange, randFreq, flags) still
 * govern WHICH SFX gets a channel and WHEN; we just bypass the seq player as
 * the dispatch mechanism. This gives us vanilla MM bank semantics on the
 * existing MmDirectAudio mixer (which already handles ADPCM, ADSR, spatial
 * vol/pan, and multi-layer playback).
 */

#include "mm_audio_sfx.h"
#include "mods/transformation_masks/assets/mm_asset_loader.h"
#include <libultraship/libultraship.h>
#include <libultraship/log/luslog.h>
#include "z64audio.h"

#include <cmath>

#define MMSFX_LOG(fmt, ...) lusprintf(__FILE__, __LINE__, LUSLOG_LEVEL_INFO, fmt, ##__VA_ARGS__)

// MmDirectAudio_Play / Stop live in mm_asset_loader.cpp. They are TU-static
// there, so we expose extern "C" stubs that wrap them. To keep that file
// minimally invasive, mm_asset_loader.cpp exposes a public wrapper
// MmSfxBridge_Play(sfxId, freqScale, pos) and MmSfxBridge_StopByMmSfxId(sfxId).
extern "C" {
s32 MmSfxBridge_Play(u16 mmSfxId, f32 freqScale, Vec3f* pos);
void MmSfxBridge_StopByMmSfxId(u16 mmSfxId);
s32 MmSfxBridge_IsActive(u16 mmSfxId);
void MmSfxBridge_RefreshProperties(u16 mmSfxId, Vec3f* pos, f32 freqScale);
}

// ---- Ruta B: isolated MM SFX synth (real Sequence_0 dispatch) -------------
// When CVar gMods.MmSfxNewEngine is set, dispatch goes to the new engine via
// channel IO (mirroring MM AUDIOCMD_CHANNEL_SET_IO) instead of MmDirectAudio.
extern "C" {
void MmSfxSynth_WriteChannelIO(int channelIndex, int ioPort, signed char value);
int MmSfxSynth_ReadChannelIO(int channelIndex, int ioPort);
void MmSfxSynth_SetChannelState(int channelIndex, float volume, float freqScale,
                                signed char panSigned, signed char stereoBits);
int MmSfxSynth_IsReady(void);
}

// The isolated mmsfx engine (real MM Sequence_0 synthesis) is now THE SFX path.
// Use it whenever it's booted; the old MmDirectAudio hand-map path below is dead.
static inline bool MmSfx_UseNewEngine(void) {
    return MmSfxSynth_IsReady();
}

// Mirror MM sfx.c:645-668 — write the SFX-sequence channel IO ports so seq_0
// triggers a note-on with the right sample/freq. `firstTrigger` issues the
// enable + sfxId ports; refresh-only updates the freq/stereo state.
static void MmSfx_DriveNewEngine(MmSfxBankEntry* entry, u8 channelIndex, f32 freqScale, bool firstTrigger) {
    static int sDriveLog = 0;
    if (sDriveLog < 12) {
        sDriveLog++;
        MMSFX_LOG("[MmSfxSynth] DriveNewEngine sfx=0x%X ch=%d first=%d freq=%.3f", entry->sfxId, channelIndex,
                  (int)firstTrigger, freqScale);
    }
    // Per-channel freq/stereo state that the seq's 0xBE custom function reads.
    // Volume left at unity here; spatial attenuation is TODO (needs listener pos).
    MmSfxSynth_SetChannelState(channelIndex, 1.0f, freqScale, 0x40, 0);
    if (firstTrigger) {
        u16 sfxId = entry->sfxId;
        MmSfxSynth_WriteChannelIO(channelIndex, 0, 1);                 // enable
        MmSfxSynth_WriteChannelIO(channelIndex, 2, 0x7F);              // volume (full; TODO spatial)
        MmSfxSynth_WriteChannelIO(channelIndex, 4, (signed char)(sfxId & 0xFF)); // sfxId low
        u8 hi = (u8)(((sfxId & 0x300) >> 7) + ((sfxId & 0xFF) >> 7));  // sfx.c:660 (large-bank bits)
        MmSfxSynth_WriteChannelIO(channelIndex, 5, (signed char)hi);   // sfxId high bits
    }
}

extern "C" {

// Compute the spatial frequency scale for an entry, including random-freq raise.
static f32 ComputeEntryFreqScale(MmSfxBankEntry* entry) {
    f32 freqScale = (entry->freqScale != nullptr) ? *entry->freqScale : 1.0f;
    if (entry->randFreq != 0) {
        // MM: 2^(randFreq/96) ≈ 1 + randFreq*(1/96). Cheap approx, randFreq <= 63.
        freqScale *= 1.0f + (entry->randFreq * (1.0f / 96.0f));
    }
    return freqScale;
}

// Called from PlayActiveSfx on state READY → PLAYING (initial trigger).
// Equivalent to MM's AUDIOCMD_CHANNEL_SET_IO(SEQ_PLAYER_SFX, ch, 4, sfxId) which
// makes the seq script issue a NEW note-on. Mirrors that: triggers ONE playback.
void MmSfxDispatch_PlayEntry(u8 bankId, MmSfxBankEntry* entry, u8 channelIndex) {
    if (entry == nullptr || entry->posX == nullptr) {
        return;
    }
    Vec3f* pos = (Vec3f*)entry->posX;
    f32 freqScale = ComputeEntryFreqScale(entry);
    if (MmSfx_UseNewEngine()) {
        MmSfx_DriveNewEngine(entry, channelIndex, freqScale, /*firstTrigger=*/true);
        return;
    }
    MmSfxBridge_Play(entry->sfxId, freqScale, pos);
}

// Called from PlayActiveSfx on state PLAYING_REFRESH (continuing note, refresh
// pos/vol/freq only). Equivalent to MM's AudioSfx_SetProperties which writes
// channel ioPort vol/pan/freq WITHOUT re-triggering the note.
//
// CRITICAL: must NOT call MmSfxBridge_Play here. The previous version did, and
// every audio callback re-triggered MmDirectAudio for every PLAYING_REFRESH
// entry → all FLAG_MASK SFX (= every voice/system/etc. ID with bit 0x800) ran
// in infinite loop until MM_DIRECT_MAX_SOUNDS slots filled.
void MmSfxDispatch_RefreshEntry(u8 bankId, MmSfxBankEntry* entry, u8 channelIndex) {
    if (entry == nullptr || entry->posX == nullptr) {
        return;
    }
    Vec3f* pos = (Vec3f*)entry->posX;
    f32 freqScale = ComputeEntryFreqScale(entry);
    if (MmSfx_UseNewEngine()) {
        MmSfx_DriveNewEngine(entry, channelIndex, freqScale, /*firstTrigger=*/false);
        return;
    }
    MmSfxBridge_RefreshProperties(entry->sfxId, pos, freqScale);
}

// Returns 1 if the entry's MmDirectAudio playback slot is still alive.
// Used by the bank engine to clean up entries whose sample has finished.
s32 MmSfxDispatch_IsEntryActive(u8 bankId, MmSfxBankEntry* entry) {
    if (entry == nullptr) return 0;
    if (MmSfx_UseNewEngine()) {
        // MM (sfx.c:678): the SFX is finished when seq_0 writes SEQ_IO_VAL_NONE
        // (-1 / 0xFF) to the channel's ioPort 1. Until then it's still active.
        // Reading the OLD MmDirectAudio state here made every continuous SFX look
        // "inactive" -> the bank engine removed+re-added it every frame, which the
        // seq saw as a fresh note-on -> piled-up, never-stopping notes.
        int v = MmSfxSynth_ReadChannelIO(entry->channelIndex, 1);
        return ((u8)v == 0xFFu) ? 0 : 1;
    }
    return MmSfxBridge_IsActive(entry->sfxId);
}

void MmSfxDispatch_StopEntry(u8 bankId, MmSfxBankEntry* entry, u8 channelIndex) {
    // INTENTIONAL NO-OP for one-frame SFX.
    //
    // In vanilla MM, AUDIOCMD_CHANNEL_SET_IO(SEQ_PLAYER_SFX, ch, 0, 0) writes
    // 0 to ioPort 0 — it tells the seq script to STOP triggering new note-ons
    // on that channel. The note that was already triggered keeps playing until
    // its envelope completes naturally (via the seq player's release stage).
    //
    // Our MmDirectAudio mixer ALREADY handles natural envelope decay for
    // one-shots (ADSR), and continuous loops auto-stop when the game stops
    // calling PlaySfx (sMmAudioFrame > lastRefreshFrame + 3 triggers release).
    //
    // The previous implementation called MmDirectAudio_StopById which actively
    // killed the playing sample. Since the bank engine ticks at audio-callback
    // rate (~50Hz), one-frame SFX transitioned QUEUED→READY→PLAYING_ONE_FRAME
    // (audible) → "needs stop" → KILLED within ~20ms. The pig grunt and other
    // short voice/system SFX never got past their attack phase. Bug.
    //
    // For very-long continuous loops where the bank entry is forcibly evicted
    // BEFORE the game stops calling PlaySfx (e.g. importance eviction), the
    // sound will briefly hang in the mixer until its lastRefreshFrame timeout
    // (~60ms). Acceptable trade-off.
    if (MmSfx_UseNewEngine()) {
        // MM sfx.c: write 0 to ioPort 0 — seq stops re-triggering on this
        // channel; the already-started note finishes via its envelope release.
        MmSfxSynth_WriteChannelIO(channelIndex, 0, 0);
    }
    (void)bankId;
    (void)entry;
    (void)channelIndex;
}

} // extern "C"
