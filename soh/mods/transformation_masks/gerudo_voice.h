/**
 * gerudo_voice.h - Auto-loaded Gerudo voice samples bundled inside gerudo.o2r.
 *
 * On first use, scans the resource archive for `voice/<HEX>/*.ogg` entries
 * (placed there by tools/add_gerudo_voice_to_o2r.py), decodes each OGG to
 * mono s16 PCM, and indexes them by NA_SE_VO_LI_* sfxId.
 *
 * When the player triggers a Link voice SFX while GerudoForm_IsActive() is
 * true, Player_PlayVoiceSfx routes through GerudoVoice_PlayIfMatch which
 * publishes the chosen sample into a free mixer slot. The mixer (called from
 * code_800E4FE0.c alongside VoicePack_MixInto / MmDirectAudio_MixInto) sums
 * the slot's PCM into the 32 kHz audio output buffer.
 *
 * Slot count, lock-free atomic publish, and 32 kHz mix rate match
 * VoicePack_MixInto / PikaSfx_MixInto so latency is identical.
 */

#ifndef GERUDO_VOICE_H
#define GERUDO_VOICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "z64.h"

void GerudoVoice_Init(void);
void GerudoVoice_Shutdown(void);

// Game thread: call from Player_PlayVoiceSfx when GerudoForm is active.
// Returns 1 if a sample was published (caller should SKIP vanilla voice),
// 0 if no sample exists for that sfxId (caller should play vanilla).
u8 GerudoVoice_PlayIfMatch(u16 sfxId, Vec3f* pos);

// Audio thread: mix any currently-playing slots into the output buffer.
void GerudoVoice_MixInto(s16* outBuf, u32 numSamples);

#ifdef __cplusplus
}
#endif

#endif // GERUDO_VOICE_H
