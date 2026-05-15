/**
 * voice_pack.h - Z64Online-style voice pack loader.
 *
 * Scans mods/ for ModLoader64 .pak archives that contain sounds/<HEX_SFX_ID>/*.ogg
 * directories, decodes OGG Vorbis to 32 kHz mono s16 PCM, and intercepts Link
 * voice SFX (NA_SE_VO_LI_SWORD_N..NA_SE_VO_LI_ELECTRIC_SHOCK_LV_KID, range
 * 0x6800-0x6832) to play the custom samples instead of vanilla grunts.
 *
 * Mixer runs on the audio thread via VoicePack_MixInto, which is invoked from
 * the same hook in soh/src/code/code_800E4FE0.c that drives MmDirectAudio_MixInto
 * and PikaSfx_MixInto.
 */

#ifndef VOICE_PACK_H
#define VOICE_PACK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "z64.h"

void  VoicePack_Init(void);
void  VoicePack_Shutdown(void);

s32         VoicePack_GetCount(void);
const char* VoicePack_GetName(s32 index);
void        VoicePack_Select(s32 index);
s32         VoicePack_GetSelectedIndex(void);

u8   VoicePack_PlayIfMatch(u16 sfxId, Vec3f* pos);
void VoicePack_MixInto(s16* outBuf, u32 numSamples);

int  VoicePack_OwnsPath(const char* path);

#ifdef __cplusplus
}
#endif

#endif
