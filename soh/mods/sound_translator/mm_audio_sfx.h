/**
 * @file mm_audio_sfx.h
 * @brief MM SFX engine — verbatim port of 2Ship's sfx.c logic into SoH.
 *
 * Sources (verbatim — only renamed for SoH coexistence):
 *   2ship/mm/include/sfx.h          (lines 2360-2486, structs + macros)
 *   2ship/mm/src/audio/sfx.c        (952 lines, engine)
 *   2ship/mm/src/audio/sfx_params.c (41 lines, bank table glue)
 *   2ship/mm/include/tables/sfx/*bank_table.h (7 tables)
 *
 * Renames applied:
 *   AudioSfx_*  -> AudioMmSfx_*
 *   gSfxBanks   -> gMmSfxBanks
 *   gActiveSfx  -> gMmActiveSfx
 *   sSfxRequests -> sMmSfxRequests
 *   gChannelsPerBank -> gMmChannelsPerBank
 *   gIsLargeSfxBank  -> gMmIsLargeSfxBank
 *   gSfxParams  -> gMmSfxParams
 *   etc.
 *
 * The structs and macros are NOT renamed because they live in our own namespace
 * here (we never include OOT's sfx-related headers — OOT doesn't have these).
 *
 * NOT ported (deliberately stubbed):
 *   - The seq-based dispatcher: AudioMmSfx_PlayActiveSfx no longer writes
 *     AUDIOCMD_CHANNEL_SET_IO(SEQ_PLAYER_SFX, ...). Instead it calls
 *     MmSfxInstr_LookupSample + MmDirectAudio_PlaySingle directly.
 *   - SfxBankLerp: MM-specific reverb/volume lerping per-bank. Not needed for SoH
 *     because MmDirectAudio mixer handles its own envelopes.
 *   - gSfxBankMuted: muting can be added later if needed.
 */

#ifndef MM_AUDIO_SFX_H
#define MM_AUDIO_SFX_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// SFX ID format — verbatim from mm/include/sfx.h
// =============================================================================
// (bank << 12) | flags(0xC00) | index(0x3FF)

#define MM_SFX_FLAG_MASK 0xC00
#define MM_SFX_FLAG 0x800

#define MM_SFX_BANK_SHIFT_OP(sfxId) (((sfxId) >> 12) & 0xFF)
#define MM_SFX_BANK_MASK_OP(sfxId)  ((sfxId) & 0xF000)
#define MM_SFX_INDEX_OP(sfxId)      ((sfxId) & 0x3FF)
#define MM_SFX_BANK_OP(sfxId)       MM_SFX_BANK_SHIFT_OP(MM_SFX_BANK_MASK_OP(sfxId))

// =============================================================================
// SfxParams bit-packing — verbatim from mm/include/sfx.h:2418-2480
// =============================================================================

#define MM_SFX_PARAM_DIST_RANGE_SHIFT 0
#define MM_SFX_PARAM_DIST_RANGE_MASK_UPPER (4 << MM_SFX_PARAM_DIST_RANGE_SHIFT)
#define MM_SFX_PARAM_DIST_RANGE_MASK (7 << MM_SFX_PARAM_DIST_RANGE_SHIFT)

#define MM_SFX_FLAG_LOWER_VOLUME_BGM (1 << 3)
#define MM_SFX_FLAG_PRIORITY_NO_DIST (1 << 4)
#define MM_SFX_FLAG_BLOCK_EQUAL_IMPORTANCE (1 << 5)

#define MM_SFX_PARAM_RAND_FREQ_RAISE_SHIFT 6
#define MM_SFX_PARAM_RAND_FREQ_RAISE_MASK (3 << MM_SFX_PARAM_RAND_FREQ_RAISE_SHIFT)

#define MM_SFX_FLAG_8 (1 << 8)
#define MM_SFX_FLAG_SURROUND_LOWPASS_FILTER (1 << 9)
#define MM_SFX_FLAG_BEHIND_SCREEN_Z_INDEX_SHIFT 10
#define MM_SFX_FLAG_BEHIND_SCREEN_Z_INDEX (1 << MM_SFX_FLAG_BEHIND_SCREEN_Z_INDEX_SHIFT)
#define MM_SFX_PARAM_RAND_FREQ_SCALE (1 << 11)
#define MM_SFX_FLAG_REVERB_NO_DIST (1 << 12)
#define MM_SFX_FLAG_VOLUME_NO_DIST (1 << 13)
#define MM_SFX_PARAM_RAND_FREQ_LOWER (1 << 14)
#define MM_SFX_FLAG_FREQ_NO_DIST (1 << 15)

#define MM_SFX_FLAG2_FORCE_RESET (1 << 0)
#define MM_SFX_FLAG2_UNUSED2 (1 << 2)
#define MM_SFX_FLAG2_UNUSED4 (1 << 4)
#define MM_SFX_FLAG2_SURROUND_NO_HIGHPASS_FILTER (1 << 5)
#define MM_SFX_FLAG2_UNUSED6 (1 << 6)
#define MM_SFX_FLAG2_APPLY_LOWPASS_FILTER (1 << 7)

// =============================================================================
// Enums — verbatim from mm/include/sfx.h:2365-2382
// =============================================================================

typedef enum {
    /* 0 */ MM_BANK_PLAYER,
    /* 1 */ MM_BANK_ITEM,
    /* 2 */ MM_BANK_ENV,
    /* 3 */ MM_BANK_ENEMY,
    /* 4 */ MM_BANK_SYSTEM,
    /* 5 */ MM_BANK_OCARINA,
    /* 6 */ MM_BANK_VOICE
} MmSfxBankType;

typedef enum {
    /* 0 */ MM_SFX_STATE_EMPTY,
    /* 1 */ MM_SFX_STATE_QUEUED,
    /* 2 */ MM_SFX_STATE_READY,
    /* 3 */ MM_SFX_STATE_PLAYING_REFRESH,
    /* 4 */ MM_SFX_STATE_PLAYING,
    /* 5 */ MM_SFX_STATE_PLAYING_ONE_FRAME
} MmSfxState;

// =============================================================================
// Bank entry — verbatim from mm/include/sfx.h:2384-2404
// =============================================================================

typedef struct {
    /* 0x00 */ f32* posX;
    /* 0x04 */ f32* posY;
    /* 0x08 */ f32* posZ;
    /* 0x0C */ f32* freqScale;
    /* 0x10 */ f32* volume;
    /* 0x14 */ s8* reverbAdd;
    /* 0x18 */ f32 dist;
    /* 0x1C */ u32 priority; // lower is more prioritized
    /* 0x20 */ u16 sfxParams;
    /* 0x22 */ u16 sfxId;
    /* 0x24 */ u8 sfxImportance;
    /* 0x25 */ u8 sfxFlags;
    /* 0x26 */ u8 state;
    /* 0x27 */ u8 freshness;
    /* 0x28 */ u8 prev;
    /* 0x29 */ u8 next;
    /* 0x2A */ u8 channelIndex;
    /* 0x2B */ u8 randFreq;
    /* 0x2C */ u8 token;
} MmSfxBankEntry; // size = 0x30

typedef struct {
    /* 0x0 */ u32 priority;
    /* 0x4 */ u8 entryIndex;
} MmActiveSfx;

// =============================================================================
// SfxParams — verbatim from mm/include/sfx.h:2482-2486
// =============================================================================

typedef struct {
    /* 0x0 */ u8 importance;
    /* 0x1 */ u8 flags;
    /* 0x2 */ u16 params;
} MmSfxParams;

// =============================================================================
// SfxRequest (ring buffer entry) — verbatim from mm/src/audio/sfx.c:4-11
// =============================================================================

typedef struct {
    /* 0x00 */ u16 sfxId;
    /* 0x02 */ u8 token;
    /* 0x04 */ s8* reverbAdd;
    /* 0x08 */ Vec3f* pos;
    /* 0x0C */ f32* freqScale;
    /* 0x10 */ f32* volume;
} MmSfxRequest;

// =============================================================================
// Public API — names mirror 2Ship AudioSfx_* with MmSfx prefix
// =============================================================================

// Queue an SFX. Idempotent on (pos, sfxId) within the read..write window.
void AudioMmSfx_PlaySfx(u16 sfxId, Vec3f* pos, u8 token, f32* freqScale, f32* volume, s8* reverbAdd);

// Per-frame: drain request queue → bank slots.
void AudioMmSfx_ProcessRequests(void);

// Per-frame: pick channels, dispatch new/refresh entries to MmDirectAudio.
void AudioMmSfx_ProcessActiveSfx(void);

// Stop ops — match the 2Ship API surface.
void AudioMmSfx_StopByBank(u8 bankId);
void AudioMmSfx_StopByPosAndBank(u8 bankId, Vec3f* pos);
void AudioMmSfx_StopByPos(Vec3f* pos);
void AudioMmSfx_StopByPosAndId(Vec3f* pos, u16 sfxId);
void AudioMmSfx_StopByTokenAndId(u8 token, u16 sfxId);
void AudioMmSfx_StopById(u32 sfxId);

u8 AudioMmSfx_IsPlaying(u32 sfxId);

void AudioMmSfx_Reset(void);

// =============================================================================
// External globals (for diagnostics / dispatcher)
// =============================================================================

extern u8 gMmIsLargeSfxBank[7];
extern u8 gMmChannelsPerBank[4][7];
extern u8 gMmUsedChannelsPerBank[4][7];
extern MmSfxParams* gMmSfxParams[7];
extern size_t gMmSfxParamsCount[7]; // per-bank length of each gMmSfxParams[] table
extern MmSfxBankEntry* gMmSfxBanks[7];
extern u8 gMmSfxChannelLayout;
extern MmActiveSfx gMmActiveSfx[7][3];
extern Vec3f gMmSfxDefaultPos;
extern f32 gMmSfxDefaultFreqAndVolScale;
extern s8 gMmSfxDefaultReverb;

#ifdef __cplusplus
}
#endif

#endif // MM_AUDIO_SFX_H
