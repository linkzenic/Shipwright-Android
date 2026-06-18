/**
 * @file mm_audio_sfx.cpp
 * @brief MM SFX engine — verbatim port of 2Ship's sfx.c.
 *
 * Source: 2ship/2ship2harkinian/mm/src/audio/sfx.c (952 lines)
 *
 * Renames only — no logic changes:
 *   AudioSfx_*  -> AudioMmSfx_*
 *   gSfxBanks   -> gMmSfxBanks
 *   gActiveSfx  -> gMmActiveSfx
 *   sSfxRequests -> sMmSfxRequests
 *   gChannelsPerBank/gUsedChannelsPerBank -> gMm*
 *   gSfxParams  -> gMmSfxParams
 *   gIsLargeSfxBank -> gMmIsLargeSfxBank
 *   SfxBankEntry / SfxParams / SfxRequest / ActiveSfx -> Mm* (in mm_audio_sfx.h)
 *   SFX_* / NA_SE_NONE -> MM_SFX_* (in mm_audio_sfx.h)
 *
 * Critical modifications (engine grafting, not logic):
 *   1. AUDIOCMD_CHANNEL_SET_IO(SEQ_PLAYER_SFX, ...) — REMOVED.
 *      The seq-based dispatcher (NA_BGM_GENERAL_SFX seq reading ioPort 0/4/5)
 *      doesn't exist in our setup. Sample dispatch happens in
 *      AudioMmSfx_PlayActiveSfx via MmSfxInstr_LookupSample + MmDirectAudio_PlaySingle.
 *   2. gAudioCtx.seqPlayers[SEQ_PLAYER_SFX].enabled gate — REPLACED with
 *      a static `sEnabled` flag that game code can toggle (default = true).
 *   3. AudioSfx_LowerBgmVolume/RestoreBgmVolume — calls SoH's Audio_SetVolScale
 *      instead of MM's AudioSeq_SetVolumeScale (same effect, different API name).
 *   4. AudioEditor_GetReplacementSeq — removed (2S2H custom seq replacement,
 *      not applicable to MM-isolated SFX path).
 */

#include "mm_audio_sfx.h"
#include "mods/transformation_masks/assets/mm_asset_loader.h"
#include <libultraship/libultraship.h>
#include <libultraship/log/luslog.h>
#include "z64audio.h"
#include "functions.h"

#include <cstdint>
#include <cstring>

#define MM_SFX_LOG(fmt, ...) lusprintf(__FILE__, __LINE__, LUSLOG_LEVEL_INFO, fmt, ##__VA_ARGS__)

// MM constants kept TU-local since we don't include OOT macros that collide
#define MAX_CHANNELS_PER_BANK 3
#define NA_SE_NONE 0
#define SFX_FLAG_MASK MM_SFX_FLAG_MASK
#define SFX_FLAG MM_SFX_FLAG
#define SFX_BANK_SHIFT(sfxId) MM_SFX_BANK_SHIFT_OP(sfxId)
#define SFX_BANK_MASK(sfxId) MM_SFX_BANK_MASK_OP(sfxId)
#define SFX_INDEX(sfxId) MM_SFX_INDEX_OP(sfxId)
#define SFX_BANK(sfxId) MM_SFX_BANK_OP(sfxId)
#define SFX_STATE_EMPTY MM_SFX_STATE_EMPTY
#define SFX_STATE_QUEUED MM_SFX_STATE_QUEUED
#define SFX_STATE_READY MM_SFX_STATE_READY
#define SFX_STATE_PLAYING_REFRESH MM_SFX_STATE_PLAYING_REFRESH
#define SFX_STATE_PLAYING MM_SFX_STATE_PLAYING
#define SFX_STATE_PLAYING_ONE_FRAME MM_SFX_STATE_PLAYING_ONE_FRAME
#define SFX_FLAG_LOWER_VOLUME_BGM MM_SFX_FLAG_LOWER_VOLUME_BGM
#define SFX_FLAG_PRIORITY_NO_DIST MM_SFX_FLAG_PRIORITY_NO_DIST
#define SFX_FLAG_BLOCK_EQUAL_IMPORTANCE MM_SFX_FLAG_BLOCK_EQUAL_IMPORTANCE
#define SFX_FLAG2_FORCE_RESET MM_SFX_FLAG2_FORCE_RESET
#define ARRAY_COUNT(x) ((s32)(sizeof(x) / sizeof((x)[0])))
#define SQ(x) ((x) * (x))

// Dispatcher hook — implemented in mm_audio_sfx_dispatch.cpp.
//   PlayEntry    : trigger a NEW playback (READY → PLAYING transition)
//   RefreshEntry : update pos/vol/freq on an existing playback, NO retrigger
//   StopEntry    : no-op (kept for ABI; release handled by MmDirectAudio ADSR)
//   IsEntryActive: returns 1 while the MmDirectAudio slot is still alive
extern "C" void MmSfxDispatch_PlayEntry(u8 bankId, MmSfxBankEntry* entry, u8 channelIndex);
extern "C" void MmSfxDispatch_RefreshEntry(u8 bankId, MmSfxBankEntry* entry, u8 channelIndex);
extern "C" void MmSfxDispatch_StopEntry(u8 bankId, MmSfxBankEntry* entry, u8 channelIndex);
extern "C" s32 MmSfxDispatch_IsEntryActive(u8 bankId, MmSfxBankEntry* entry);

// =============================================================================
// VERBATIM from sfx.c:13-18
// =============================================================================
typedef struct {
    /* 0x0 */ f32 value;
    /* 0x4 */ f32 target;
    /* 0x8 */ f32 step;
    /* 0xC */ u16 remainingFrames;
} MmSfxBankLerp;

typedef enum {
    /* 0 */ SFX_RM_REQ_BY_BANK,
    /* 1 */ SFX_RM_REQ_BY_POS_AND_BANK,
    /* 2 */ SFX_RM_REQ_BY_POS,
    /* 3 */ SFX_RM_REQ_BY_POS_AND_ID,
    /* 4 */ SFX_RM_REQ_BY_TOKEN_AND_ID,
    /* 5 */ SFX_RM_REQ_BY_ID
} MmSfxRemoveRequest;

// =============================================================================
// VERBATIM from sfx.c:29-83 (bank storage + Mm renames)
// =============================================================================
static MmSfxBankEntry sMmSfxPlayerBank[9];
static MmSfxBankEntry sMmSfxItemBank[12];
static MmSfxBankEntry sMmSfxEnvironmentBank[32];
static MmSfxBankEntry sMmSfxEnemyBank[20];
static MmSfxBankEntry sMmSfxSystemBank[8];
static MmSfxBankEntry sMmSfxOcarinaBank[3];
static MmSfxBankEntry sMmSfxVoiceBank[5];
static MmSfxRequest sMmSfxRequests[0x100];
static u8 sMmSfxBankListEnd[7];
static u8 sMmSfxBankFreeListStart[7];
static u8 sMmSfxBankUnused[7];
MmActiveSfx gMmActiveSfx[7][3];
static u8 sMmCurSfxPlayerChannelIndex;
static u8 sMmSfxBankMuted[7];
static MmSfxBankLerp sMmSfxBankLerp[7];

static u8 sMmSfxRequestWriteIndex = 0;
static u8 sMmSfxRequestReadIndex = 0;

// Cross-thread stop guard. The game thread queues PlaySfx requests AND calls
// StopById, while the audio thread drains the request ring asynchronously. A
// PlaySfx request queued on the same frame as a StopById can survive the ring
// purge and be processed AFTER the stop, re-creating the bank entry — which
// then refreshes lastRefreshFrame forever and defeats the continuous auto-stop.
// (Manifested as the Goron BALL_CHARGE hum never stopping after spike activation.)
// When StopById runs, we record the sfxId here; ProcessRequest skips any queued
// request for a guarded id. The guard is cleared at the end of each drain cycle.
static u16 sMmStopGuard[16];
static u8 sMmStopGuardCount = 0;

static void MmStopGuard_Add(u16 sfxId) {
    for (u8 i = 0; i < sMmStopGuardCount; i++) {
        if (sMmStopGuard[i] == sfxId) {
            return; // already guarded
        }
    }
    if (sMmStopGuardCount < (u8)(sizeof(sMmStopGuard) / sizeof(sMmStopGuard[0]))) {
        sMmStopGuard[sMmStopGuardCount++] = sfxId;
    }
}

static u8 MmStopGuard_Contains(u16 sfxId) {
    for (u8 i = 0; i < sMmStopGuardCount; i++) {
        if (sMmStopGuard[i] == sfxId) {
            return 1;
        }
    }
    return 0;
}

// VERBATIM from sfx.c:60-62
MmSfxBankEntry* gMmSfxBanks[7] = {
    sMmSfxPlayerBank, sMmSfxItemBank, sMmSfxEnvironmentBank, sMmSfxEnemyBank,
    sMmSfxSystemBank, sMmSfxOcarinaBank, sMmSfxVoiceBank,
};

// VERBATIM from sfx.c:64-68
static u8 sMmSfxBankSizes[ARRAY_COUNT(gMmSfxBanks)] = {
    ARRAY_COUNT(sMmSfxPlayerBank), ARRAY_COUNT(sMmSfxItemBank),   ARRAY_COUNT(sMmSfxEnvironmentBank),
    ARRAY_COUNT(sMmSfxEnemyBank),  ARRAY_COUNT(sMmSfxSystemBank), ARRAY_COUNT(sMmSfxOcarinaBank),
    ARRAY_COUNT(sMmSfxVoiceBank),
};

// VERBATIM from code_8019AF00.c:187-204 (MM channel layouts)
u8 gMmIsLargeSfxBank[7] = { 1, 0, 1, 1, 0, 0, 1 };
u8 gMmChannelsPerBank[4][7] = {
    { 3, 2, 3, 3, 2, 1, 2 },
    { 3, 2, 2, 2, 2, 2, 2 },
    { 3, 2, 2, 2, 2, 2, 2 },
    { 4, 1, 0, 0, 2, 2, 2 },
};
u8 gMmUsedChannelsPerBank[4][7] = {
    { 3, 2, 3, 2, 2, 1, 1 },
    { 3, 1, 1, 1, 2, 1, 1 },
    { 3, 1, 1, 1, 2, 1, 1 },
    { 2, 1, 0, 0, 1, 1, 1 },
};

u8 gMmSfxChannelLayout = 0;
static u16 sMmSfxChannelLowVolumeFlag = 0;

Vec3f gMmSfxDefaultPos = { 0.0f, 0.0f, 0.0f };
f32 gMmSfxDefaultFreqAndVolScale = 1.0f;
s8 gMmSfxDefaultReverb = 0;

// Replacement for `gAudioCtx.seqPlayers[SEQ_PLAYER_SFX].enabled`.
// Engine self-enables once AudioMmSfx_Reset has been called.
static u8 sMmSfxEngineReady = 0;

// Forward declarations
static void AudioMmSfx_RemoveBankEntry(u8 bankId, u8 entryIndex);
static void AudioMmSfx_PlayActiveSfx(u8 bankId);

// VERBATIM from sfx.c:85-96
void AudioMmSfx_MuteBanks(u16 muteMask) {
    u8 bankId;
    for (bankId = 0; bankId < ARRAY_COUNT(gMmSfxBanks); bankId++) {
        sMmSfxBankMuted[bankId] = (muteMask & 1) ? 1 : 0;
        muteMask = muteMask >> 1;
    }
}

// MODIFIED: routes to SoH's Audio_SetVolScale instead of MM's AudioSeq_SetVolumeScale.
// 2Ship sfx.c:103-108
void AudioMmSfx_LowerBgmVolume(u8 channelIndex) {
    sMmSfxChannelLowVolumeFlag |= (1 << channelIndex);
    // SoH equivalent: Audio_SetVolScale(player, scaleIndex, volume, fadeFrames)
    Audio_SetVolScale(SEQ_PLAYER_BGM_MAIN, /*VOL_SCALE_INDEX_SFX*/ 1, 0x40, 0xF);
}

// 2Ship sfx.c:114-121
void AudioMmSfx_RestoreBgmVolume(u8 channelIndex) {
    sMmSfxChannelLowVolumeFlag &= ((1 << channelIndex) ^ 0xFFFF);
    if (sMmSfxChannelLowVolumeFlag == 0) {
        Audio_SetVolScale(SEQ_PLAYER_BGM_MAIN, /*VOL_SCALE_INDEX_SFX*/ 1, 0x7F, 0xF);
    }
}

// VERBATIM from sfx.c:126-149
void AudioMmSfx_PlaySfx(u16 sfxId, Vec3f* pos, u8 token, f32* freqScale, f32* volume, s8* reverbAdd) {
    u8 i;
    MmSfxRequest* reqWrite;
    MmSfxRequest* reqRead;

    // Bounds guard: the bank nibble (SFX_BANK_SHIFT = (sfxId >> 12) & 0xFF) indexes
    // every [7]-sized bank array below. A malformed sfxId (bits 12+ >= 7) would
    // read OOB starting with sMmSfxBankMuted[] on the very next line. Reject it.
    if ((s32)SFX_BANK_SHIFT(sfxId) >= ARRAY_COUNT(gMmSfxBanks)) {
        return;
    }
    // Per-bank index guard: the index field (0x3FF) can exceed this bank's param
    // table length; gMmSfxParams[bank][SFX_INDEX] is read in ProcessRequest.
    if ((size_t)SFX_INDEX(sfxId) >= gMmSfxParamsCount[SFX_BANK_SHIFT(sfxId)]) {
        return;
    }

    if (!sMmSfxBankMuted[SFX_BANK_SHIFT(sfxId)]) {
        reqWrite = &sMmSfxRequests[sMmSfxRequestWriteIndex];

        for (i = sMmSfxRequestReadIndex; sMmSfxRequestWriteIndex != i; i++) {
            reqRead = &sMmSfxRequests[i];
            if ((reqRead->pos == pos) && (reqRead->sfxId == sfxId)) {
                return;
            }
        }

        reqWrite->sfxId = sfxId;
        reqWrite->pos = pos;
        reqWrite->token = token;
        reqWrite->freqScale = freqScale;
        reqWrite->volume = volume;
        reqWrite->reverbAdd = reverbAdd;
        sMmSfxRequestWriteIndex++;
    }
}

// VERBATIM from sfx.c:151-205
static void AudioMmSfx_RemoveMatchingRequests(u8 aspect, MmSfxBankEntry* entry) {
    MmSfxRequest* req;
    s32 remove;
    u8 i = sMmSfxRequestReadIndex;

    for (; i != sMmSfxRequestWriteIndex; i++) {
        remove = 0;
        req = &sMmSfxRequests[i];

        switch (aspect) {
            case SFX_RM_REQ_BY_BANK:
                if (SFX_BANK_MASK(req->sfxId) == SFX_BANK_MASK(entry->sfxId)) {
                    remove = 1;
                }
                break;

            case SFX_RM_REQ_BY_POS_AND_BANK:
                if ((SFX_BANK_MASK(req->sfxId) == SFX_BANK_MASK(entry->sfxId)) && (&req->pos->x == entry->posX)) {
                    remove = 1;
                }
                break;

            case SFX_RM_REQ_BY_POS:
                if (&req->pos->x == entry->posX) {
                    remove = 1;
                }
                break;

            case SFX_RM_REQ_BY_POS_AND_ID:
                if ((&req->pos->x == entry->posX) && (req->sfxId == entry->sfxId)) {
                    remove = 1;
                }
                break;

            case SFX_RM_REQ_BY_TOKEN_AND_ID:
                if ((req->token == entry->token) && (req->sfxId == entry->sfxId)) {
                    remove = 1;
                }
                break;

            case SFX_RM_REQ_BY_ID:
                if (req->sfxId == entry->sfxId) {
                    remove = 1;
                }
                break;

            default:
                break;
        }

        if (remove) {
            req->sfxId = NA_SE_NONE;
        }
    }
}

// VERBATIM from sfx.c:207-357 (with AudioEditor_GetReplacementSeq stripped)
static void AudioMmSfx_ProcessRequest(void) {
    u16 sfxId;
    u8 channelCount;
    u8 index;
    MmSfxRequest* req = &sMmSfxRequests[sMmSfxRequestReadIndex];
    MmSfxBankEntry* entry;
    MmSfxParams* sfxParams;
    s32 bankId;
    u8 evictImportance = 0;
    u8 evictIndex = 0x80;

    if (req->sfxId == NA_SE_NONE) {
        return;
    }
    if (req->sfxId == 0) {
        return;
    }
    // Skip a request whose sfxId was stopped this drain cycle — prevents a
    // stale same-frame request from resurrecting a just-stopped continuous SFX
    // (Goron BALL_CHARGE leak after spike activation).
    if (MmStopGuard_Contains(req->sfxId)) {
        return;
    }
    // Bounds guard (defense in depth — PlaySfx already rejects bad ids, but a
    // request could in principle be queued by another path). SFX_BANK indexes
    // the [7]-sized bank arrays; SFX_INDEX indexes the per-bank param table.
    if ((s32)SFX_BANK(req->sfxId) >= ARRAY_COUNT(gMmSfxBanks)) {
        return;
    }
    if ((size_t)SFX_INDEX(req->sfxId) >= gMmSfxParamsCount[SFX_BANK(req->sfxId)]) {
        return;
    }
    bankId = SFX_BANK(req->sfxId);
    channelCount = 0;
    index = gMmSfxBanks[bankId][0].next;

    while ((index != 0xFF) && (index != 0)) {
        if (gMmSfxBanks[bankId][index].posX == &req->pos->x) {
            if ((gMmSfxParams[SFX_BANK_SHIFT(req->sfxId)][SFX_INDEX(req->sfxId)].params &
                 SFX_FLAG_BLOCK_EQUAL_IMPORTANCE) &&
                (gMmSfxParams[SFX_BANK_SHIFT(req->sfxId)][SFX_INDEX(req->sfxId)].importance ==
                 gMmSfxBanks[bankId][index].sfxImportance)) {
                return;
            }

            if (gMmSfxBanks[bankId][index].sfxId == req->sfxId) {
                channelCount = gMmUsedChannelsPerBank[gMmSfxChannelLayout][bankId];
            } else {
                if (channelCount == 0) {
                    evictIndex = index;
                    sfxId = gMmSfxBanks[bankId][index].sfxId & 0xFFFF;
                    evictImportance = gMmSfxParams[SFX_BANK_SHIFT(sfxId)][SFX_INDEX(sfxId)].importance;
                } else if (gMmSfxBanks[bankId][index].sfxImportance < evictImportance) {
                    evictIndex = index;
                    sfxId = gMmSfxBanks[bankId][index].sfxId & 0xFFFF;
                    evictImportance = gMmSfxParams[SFX_BANK_SHIFT(sfxId)][SFX_INDEX(sfxId)].importance;
                }

                channelCount++;

                if (channelCount == gMmUsedChannelsPerBank[gMmSfxChannelLayout][bankId]) {
                    if (gMmSfxParams[SFX_BANK_SHIFT(req->sfxId)][SFX_INDEX(req->sfxId)].importance >= evictImportance) {
                        index = evictIndex;
                    } else {
                        index = 0;
                    }
                }
            }

            if (channelCount == gMmUsedChannelsPerBank[gMmSfxChannelLayout][bankId]) {
                sfxParams = &gMmSfxParams[SFX_BANK_SHIFT(req->sfxId)][SFX_INDEX(req->sfxId)];

                if ((req->sfxId & SFX_FLAG_MASK) || (sfxParams->flags & SFX_FLAG2_FORCE_RESET) ||
                    (index == evictIndex)) {

                    if ((gMmSfxBanks[bankId][index].sfxParams & SFX_FLAG_LOWER_VOLUME_BGM) &&
                        (gMmSfxBanks[bankId][index].state != SFX_STATE_QUEUED)) {
                        AudioMmSfx_RestoreBgmVolume(gMmSfxBanks[bankId][index].channelIndex);
                    }

                    gMmSfxBanks[bankId][index].token = req->token;
                    gMmSfxBanks[bankId][index].sfxId = req->sfxId;
                    gMmSfxBanks[bankId][index].state = SFX_STATE_QUEUED;
                    gMmSfxBanks[bankId][index].freshness = 2;
                    gMmSfxBanks[bankId][index].freqScale = req->freqScale;
                    gMmSfxBanks[bankId][index].volume = req->volume;
                    gMmSfxBanks[bankId][index].reverbAdd = req->reverbAdd;
                    gMmSfxBanks[bankId][index].sfxParams = sfxParams->params;
                    gMmSfxBanks[bankId][index].sfxFlags = sfxParams->flags;
                    gMmSfxBanks[bankId][index].sfxImportance = sfxParams->importance;
                    // CRITICAL: zero randFreq when reassigning a bank slot. The
                    // randFreq update (line ~614) ONLY runs when sfxParams->randParam
                    // is nonzero; otherwise the stale value from the previous SFX
                    // (could be a damage voice with randParam=2 → randFreq=14)
                    // bleeds into this new SFX. Result: voices with randParam=0
                    // (e.g. Deku SWORD_N) played at random pitch they shouldn't.
                    gMmSfxBanks[bankId][index].randFreq = 0;
                } else if (gMmSfxBanks[bankId][index].state == SFX_STATE_PLAYING_ONE_FRAME) {
                    gMmSfxBanks[bankId][index].state = SFX_STATE_PLAYING;
                }
                index = 0;
            }
        }

        if (index != 0) {
            index = gMmSfxBanks[bankId][index].next;
        }
    }

    if ((gMmSfxBanks[bankId][sMmSfxBankFreeListStart[bankId]].next != 0xFF) && (index != 0)) {
        index = sMmSfxBankFreeListStart[bankId];

        entry = &gMmSfxBanks[bankId][index];
        entry->posX = &req->pos->x;
        entry->posY = &req->pos->y;
        entry->posZ = &req->pos->z;
        entry->token = req->token;
        entry->freqScale = req->freqScale;
        entry->volume = req->volume;
        entry->reverbAdd = req->reverbAdd;

        sfxParams = &gMmSfxParams[SFX_BANK_SHIFT(req->sfxId)][SFX_INDEX(req->sfxId)];

        entry->sfxParams = sfxParams->params;
        entry->sfxFlags = sfxParams->flags;
        entry->sfxImportance = sfxParams->importance;
        entry->sfxId = req->sfxId;
        entry->state = SFX_STATE_QUEUED;
        entry->freshness = 2;
        // CRITICAL: zero randFreq when allocating a fresh entry. See comment in
        // the reassignment path above — without this, the slot can have a stale
        // randFreq value left over from a previous SFX in the same bank slot,
        // causing pitch variation on voices that should be deterministic.
        entry->randFreq = 0;
        entry->prev = sMmSfxBankListEnd[bankId];

        gMmSfxBanks[bankId][sMmSfxBankListEnd[bankId]].next = sMmSfxBankFreeListStart[bankId];
        sMmSfxBankListEnd[bankId] = sMmSfxBankFreeListStart[bankId];
        sMmSfxBankFreeListStart[bankId] = gMmSfxBanks[bankId][sMmSfxBankFreeListStart[bankId]].next;
        gMmSfxBanks[bankId][sMmSfxBankFreeListStart[bankId]].prev = 0xFF;

        entry->next = 0xFF;
    }
}

// VERBATIM from sfx.c:359-386
static void AudioMmSfx_RemoveBankEntry(u8 bankId, u8 entryIndex) {
    MmSfxBankEntry* entry = &gMmSfxBanks[bankId][entryIndex];
    u8 i;

    if (entry->sfxParams & SFX_FLAG_LOWER_VOLUME_BGM) {
        AudioMmSfx_RestoreBgmVolume(entry->channelIndex);
    }

    if (entryIndex == sMmSfxBankListEnd[bankId]) {
        sMmSfxBankListEnd[bankId] = entry->prev;
    } else {
        gMmSfxBanks[bankId][entry->next].prev = entry->prev;
    }

    gMmSfxBanks[bankId][entry->prev].next = entry->next;
    entry->next = sMmSfxBankFreeListStart[bankId];
    entry->prev = 0xFF;
    gMmSfxBanks[bankId][sMmSfxBankFreeListStart[bankId]].prev = entryIndex;
    sMmSfxBankFreeListStart[bankId] = entryIndex;
    entry->state = SFX_STATE_EMPTY;

    for (i = 0; i < gMmChannelsPerBank[gMmSfxChannelLayout][bankId]; i++) {
        if (gMmActiveSfx[bankId][i].entryIndex == entryIndex) {
            gMmActiveSfx[bankId][i].entryIndex = 0xFF;
            i = gMmChannelsPerBank[gMmSfxChannelLayout][bankId];
        }
    }
}

// VERBATIM from sfx.c:388-598 (with AUDIOCMD_CHANNEL_SET_IO replaced by MmSfxDispatch_StopEntry)
static void AudioMmSfx_ChooseActiveSfx(u8 bankId) {
    u8 numChosenSfx = 0;
    u8 numChannels;
    u8 entryIndex;
    u8 i;
    u8 j;
    u8 k;
    u8 sfxImportance;
    u8 needNewSfx;
    u8 chosenEntryIndex;
    MmSfxBankEntry* entry;
    MmActiveSfx chosenSfx[MAX_CHANNELS_PER_BANK];
    MmActiveSfx* activeSfx;
    f32 entryPosY;
    f32 entryPosX;

    for (i = 0; i < MAX_CHANNELS_PER_BANK; i++) {
        chosenSfx[i].priority = 0x7FFFFFFF;
        chosenSfx[i].entryIndex = 0xFF;
    }

    entryIndex = gMmSfxBanks[bankId][0].next;
    k = 0;

    while (entryIndex != 0xFF) {
        if ((gMmSfxBanks[bankId][entryIndex].state == SFX_STATE_QUEUED) &&
            (gMmSfxBanks[bankId][entryIndex].sfxId & SFX_FLAG_MASK)) {
            gMmSfxBanks[bankId][entryIndex].freshness--;
        } else if (!(gMmSfxBanks[bankId][entryIndex].sfxId & SFX_FLAG_MASK) &&
                   (gMmSfxBanks[bankId][entryIndex].state == SFX_STATE_PLAYING_ONE_FRAME)) {
            // Was: AUDIOCMD_CHANNEL_SET_IO(SEQ_PLAYER_SFX, channelIndex, 0, 0)
            MmSfxDispatch_StopEntry(bankId, &gMmSfxBanks[bankId][entryIndex],
                                    gMmSfxBanks[bankId][entryIndex].channelIndex);
            AudioMmSfx_RemoveBankEntry(bankId, entryIndex);
        } else if ((gMmSfxBanks[bankId][entryIndex].sfxId & SFX_FLAG_MASK) &&
                   (gMmSfxBanks[bankId][entryIndex].state >= SFX_STATE_PLAYING_REFRESH) &&
                   !MmSfxDispatch_IsEntryActive(bankId, &gMmSfxBanks[bankId][entryIndex])) {
            // All-frames (FLAG_MASK set) entry whose MmDirectAudio slot has
            // finished playing (sample exhausted + envelope released, slot
            // returned to inactive). Mirrors MM's seqScriptIO[1] == SEQ_IO_VAL_NONE
            // cleanup: the bank entry has outlived its audible playback, remove it.
            // Without this, FLAG_MASK entries would persist forever (no game-side
            // PlaySfx → no state reset → no eviction).
            AudioMmSfx_RemoveBankEntry(bankId, entryIndex);
        }

        if (gMmSfxBanks[bankId][entryIndex].freshness == 0) {
            AudioMmSfx_RemoveBankEntry(bankId, entryIndex);
        } else if (gMmSfxBanks[bankId][entryIndex].state != SFX_STATE_EMPTY) {
            entry = &gMmSfxBanks[bankId][entryIndex];

            if (&gMmSfxDefaultPos.x == entry[0].posX) {
                entry->dist = 0.0f;
            } else {
                entryPosY = *entry->posY * 1;
                entryPosX = *entry->posX * 0.5f;
                entry->dist = (SQ(entryPosX) + SQ(entryPosY) + SQ(*entry->posZ)) / 10.0f;
            }

            sfxImportance = entry->sfxImportance;

            if (entry->sfxParams & SFX_FLAG_PRIORITY_NO_DIST) {
                entry->priority = SQ(0xFF - sfxImportance) * SQ(76);
            } else {
                if (entry->dist > 0x7FFFFFD0) {
                    entry->dist = (f32)0x70000008;
                }

                entry->priority = (u32)entry->dist + (SQ(0xFF - sfxImportance) * SQ(76));
                if (*entry->posZ < 0.0f) {
                    entry->priority += (s32)(-*entry->posZ * 6.0f);
                }
            }

            if (entry->dist > SQ(1e5f)) {
                if (entry->state == SFX_STATE_PLAYING) {
                    MmSfxDispatch_StopEntry(bankId, entry, entry->channelIndex);
                    if (entry->sfxId & SFX_FLAG_MASK) {
                        AudioMmSfx_RemoveBankEntry(bankId, entryIndex);
                        entryIndex = k;
                    }
                }
            } else {
                numChannels = gMmChannelsPerBank[gMmSfxChannelLayout][bankId];

                for (i = 0; i < numChannels; i++) {
                    if (chosenSfx[i].priority >= entry->priority) {
                        if (numChosenSfx < gMmChannelsPerBank[gMmSfxChannelLayout][bankId]) {
                            numChosenSfx++;
                        }

                        for (j = numChannels - 1; j > i; j--) {
                            chosenSfx[j].priority = chosenSfx[j - 1].priority;
                            chosenSfx[j].entryIndex = chosenSfx[j - 1].entryIndex;
                        }

                        chosenSfx[i].priority = entry->priority;
                        chosenSfx[i].entryIndex = entryIndex;
                        i = numChannels;
                    }
                }
            }

            k = entryIndex;
        }

        entryIndex = gMmSfxBanks[bankId][k].next;
    }

    for (i = 0; i < numChosenSfx; i++) {
        entry = &gMmSfxBanks[bankId][chosenSfx[i].entryIndex];

        if (entry->state == SFX_STATE_QUEUED) {
            entry->state = SFX_STATE_READY;
        } else if (entry->state == SFX_STATE_PLAYING) {
            entry->state = SFX_STATE_PLAYING_REFRESH;
        }
    }

    numChannels = gMmChannelsPerBank[gMmSfxChannelLayout][bankId];
    for (i = 0; i < numChannels; i++) {
        needNewSfx = 0;
        activeSfx = &gMmActiveSfx[bankId][i];

        if (activeSfx->entryIndex == 0xFF) {
            needNewSfx = 1;
        } else {
            entry = &gMmSfxBanks[bankId][activeSfx[0].entryIndex];

            if (entry->state == SFX_STATE_PLAYING) {
                if (entry->sfxId & SFX_FLAG_MASK) {
                    AudioMmSfx_RemoveBankEntry(bankId, activeSfx->entryIndex);
                } else {
                    entry->state = SFX_STATE_QUEUED;
                    entry->freshness = 0x80;
                }
                needNewSfx = 1;
            } else if (entry->state == SFX_STATE_EMPTY) {
                activeSfx->entryIndex = 0xFF;
                needNewSfx = 1;
            } else {
                for (j = 0; j < numChannels; j++) {
                    if (activeSfx->entryIndex == chosenSfx[j].entryIndex) {
                        chosenSfx[j].entryIndex = 0xFF;
                        j = numChannels;
                    }
                }
                numChosenSfx--;
            }
        }

        if (needNewSfx == 1) {
            for (j = 0; j < numChannels; j++) {
                chosenEntryIndex = chosenSfx[j].entryIndex;
                if ((chosenEntryIndex != 0xFF) &&
                    (gMmSfxBanks[bankId][chosenEntryIndex].state != SFX_STATE_PLAYING_REFRESH)) {
                    for (k = 0; k < numChannels; k++) {
                        if (chosenEntryIndex == gMmActiveSfx[bankId][k].entryIndex) {
                            needNewSfx = 0;
                            k = numChannels;
                        }
                    }

                    if (needNewSfx == 1) {
                        activeSfx->entryIndex = chosenEntryIndex;
                        chosenSfx[j].entryIndex = 0xFF;
                        j = numChannels + 1;
                        numChosenSfx--;
                    }
                }
            }
            if (j == numChannels) {
                activeSfx->entryIndex = 0xFF;
            }
        }
    }
}

// VERBATIM from sfx.c:600-698 (with AUDIOCMD_CHANNEL_SET_IO replaced by MmSfxDispatch_PlayEntry)
static void AudioMmSfx_PlayActiveSfx(u8 bankId) {
    u8 entryIndex;
    MmSfxBankEntry* entry;
    u8 i;

    for (i = 0; i < gMmChannelsPerBank[gMmSfxChannelLayout][bankId]; i++) {
        entryIndex = gMmActiveSfx[bankId][i].entryIndex;
        if (entryIndex != 0xFF) {
            entry = &gMmSfxBanks[bankId][entryIndex];

            if (entry->state == SFX_STATE_READY) {
                entry->channelIndex = sMmCurSfxPlayerChannelIndex;
                if (entry->sfxParams & SFX_FLAG_LOWER_VOLUME_BGM) {
                    AudioMmSfx_LowerBgmVolume(sMmCurSfxPlayerChannelIndex);
                }

                // Random freq raise — 2Ship sfx.c:622-639 verbatim
                if ((entry->sfxParams & MM_SFX_PARAM_RAND_FREQ_RAISE_MASK) !=
                    (0 << MM_SFX_PARAM_RAND_FREQ_RAISE_SHIFT)) {
                    switch (entry->sfxParams & MM_SFX_PARAM_RAND_FREQ_RAISE_MASK) {
                        case (1 << MM_SFX_PARAM_RAND_FREQ_RAISE_SHIFT):
                            entry->randFreq = (u8)(rand() & 0xF);
                            break;
                        case (2 << MM_SFX_PARAM_RAND_FREQ_RAISE_SHIFT):
                            entry->randFreq = (u8)(rand() & 0x1F);
                            break;
                        case (3 << MM_SFX_PARAM_RAND_FREQ_RAISE_SHIFT):
                            entry->randFreq = (u8)(rand() & 0x3F);
                            break;
                        default:
                            entry->randFreq = 0;
                            break;
                    }
                }

                // Trigger fresh playback — equivalent to MM's
                // AUDIOCMD_CHANNEL_SET_IO(SEQ_PLAYER_SFX, ch, 4, sfxId) note-on.
                MmSfxDispatch_PlayEntry(bankId, entry, sMmCurSfxPlayerChannelIndex);

                if (entry->sfxId & SFX_FLAG_MASK) {
                    entry->state = SFX_STATE_PLAYING;
                } else {
                    entry->state = SFX_STATE_PLAYING_ONE_FRAME;
                }
            } else if (entry->state == SFX_STATE_PLAYING_REFRESH) {
                // Refresh ONLY pos/vol/freq on the existing playback slot.
                // Equivalent to MM's AudioSfx_SetProperties — NO new note-on.
                // Calling PlayEntry here would re-trigger the sample every
                // audio callback and cause infinite loop for FLAG_MASK SFX.
                MmSfxDispatch_RefreshEntry(bankId, entry, entry->channelIndex);
                if (entry->sfxId & SFX_FLAG_MASK) {
                    entry->state = SFX_STATE_PLAYING;
                } else {
                    entry->state = SFX_STATE_PLAYING_ONE_FRAME;
                }
            }
        }

        sMmCurSfxPlayerChannelIndex++;
    }
}

// VERBATIM from sfx.c:700-720 (with AUDIOCMD_CHANNEL_SET_IO removed)
void AudioMmSfx_StopByBank(u8 bankId) {
    MmSfxBankEntry* entry;
    MmSfxBankEntry entryToRemove;
    u8 entryIndex = gMmSfxBanks[bankId][0].next;

    while (entryIndex != 0xFF) {
        entry = &gMmSfxBanks[bankId][entryIndex];
        if (entry->state >= SFX_STATE_PLAYING_REFRESH) {
            MmSfxDispatch_StopEntry(bankId, entry, entry->channelIndex);
        }

        if (entry->state != SFX_STATE_EMPTY) {
            AudioMmSfx_RemoveBankEntry(bankId, entryIndex);
        }
        entryIndex = gMmSfxBanks[bankId][0].next;
    }

    entryToRemove.sfxId = bankId << 12;
    AudioMmSfx_RemoveMatchingRequests(SFX_RM_REQ_BY_BANK, &entryToRemove);
}

// VERBATIM from sfx.c:722-743
static void AudioMmSfx_StopByPosAndBankImpl(u8 bankId, Vec3f* pos) {
    MmSfxBankEntry* entry;
    u8 entryIndex = gMmSfxBanks[bankId][0].next;
    u8 prevEntryIndex = 0;

    while (entryIndex != 0xFF) {
        entry = &gMmSfxBanks[bankId][entryIndex];
        if (entry->posX == &pos->x) {
            if (entry->state >= SFX_STATE_PLAYING_REFRESH) {
                MmSfxDispatch_StopEntry(bankId, entry, entry->channelIndex);
            }

            if (entry->state != SFX_STATE_EMPTY) {
                AudioMmSfx_RemoveBankEntry(bankId, entryIndex);
            }
        } else {
            prevEntryIndex = entryIndex;
        }

        entryIndex = gMmSfxBanks[bankId][prevEntryIndex].next;
    }
}

void AudioMmSfx_StopByPosAndBank(u8 bankId, Vec3f* pos) {
    MmSfxBankEntry entryToRemove;
    AudioMmSfx_StopByPosAndBankImpl(bankId, pos);
    entryToRemove.sfxId = bankId << 12;
    entryToRemove.posX = &pos->x;
    AudioMmSfx_RemoveMatchingRequests(SFX_RM_REQ_BY_POS_AND_BANK, &entryToRemove);
}

void AudioMmSfx_StopByPos(Vec3f* pos) {
    u8 bankId;
    MmSfxBankEntry entryToRemove;

    for (bankId = 0; bankId < ARRAY_COUNT(gMmSfxBanks); bankId++) {
        AudioMmSfx_StopByPosAndBankImpl(bankId, pos);
    }

    entryToRemove.posX = &pos->x;
    AudioMmSfx_RemoveMatchingRequests(SFX_RM_REQ_BY_POS, &entryToRemove);
}

void AudioMmSfx_StopByPosAndId(Vec3f* pos, u16 sfxId) {
    MmSfxBankEntry* entry;
    u8 entryIndex = gMmSfxBanks[SFX_BANK(sfxId)][0].next;
    u8 prevEntryIndex = 0;
    MmSfxBankEntry entryToRemove;

    while (entryIndex != 0xFF) {
        entry = &gMmSfxBanks[SFX_BANK(sfxId)][entryIndex];
        if ((entry->posX == &pos->x) && (entry->sfxId == sfxId)) {
            if (entry->state >= SFX_STATE_PLAYING_REFRESH) {
                MmSfxDispatch_StopEntry(SFX_BANK(sfxId), entry, entry->channelIndex);
            }
            if (entry->state != SFX_STATE_EMPTY) {
                AudioMmSfx_RemoveBankEntry(SFX_BANK(sfxId), entryIndex);
            }
            entryIndex = 0xFF;
        } else {
            prevEntryIndex = entryIndex;
        }

        if (entryIndex != 0xFF) {
            entryIndex = gMmSfxBanks[SFX_BANK(sfxId)][prevEntryIndex].next;
        }
    }

    entryToRemove.posX = &pos->x;
    entryToRemove.sfxId = sfxId;
    AudioMmSfx_RemoveMatchingRequests(SFX_RM_REQ_BY_POS_AND_ID, &entryToRemove);
}

void AudioMmSfx_StopByTokenAndId(u8 token, u16 sfxId) {
    MmSfxBankEntry* entry;
    u8 entryIndex = gMmSfxBanks[SFX_BANK(sfxId)][0].next;
    u8 prevEntryIndex = 0;
    MmSfxBankEntry entryToRemove;

    while (entryIndex != 0xFF) {
        entry = &gMmSfxBanks[SFX_BANK(sfxId)][entryIndex];
        if ((entry->token == token) && (entry->sfxId == sfxId)) {
            if (entry->state >= SFX_STATE_PLAYING_REFRESH) {
                MmSfxDispatch_StopEntry(SFX_BANK(sfxId), entry, entry->channelIndex);
            }
            if (entry->state != SFX_STATE_EMPTY) {
                AudioMmSfx_RemoveBankEntry(SFX_BANK(sfxId), entryIndex);
            }
        } else {
            prevEntryIndex = entryIndex;
        }

        if (entryIndex != 0xFF) {
            entryIndex = gMmSfxBanks[SFX_BANK(sfxId)][prevEntryIndex].next;
        }
    }

    entryToRemove.token = token;
    entryToRemove.sfxId = sfxId;
    AudioMmSfx_RemoveMatchingRequests(SFX_RM_REQ_BY_TOKEN_AND_ID, &entryToRemove);
}

void AudioMmSfx_StopById(u32 sfxId) {
    MmSfxBankEntry* entry;
    u8 entryIndex = gMmSfxBanks[SFX_BANK(sfxId)][0].next;
    u8 prevEntryIndex = 0;
    MmSfxBankEntry entryToRemove;

    while (entryIndex != 0xFF) {
        entry = &gMmSfxBanks[SFX_BANK(sfxId)][entryIndex];
        if (entry->sfxId == sfxId) {
            if (entry->state >= SFX_STATE_PLAYING_REFRESH) {
                MmSfxDispatch_StopEntry(SFX_BANK(sfxId), entry, entry->channelIndex);
            }
            if (entry->state != SFX_STATE_EMPTY) {
                AudioMmSfx_RemoveBankEntry(SFX_BANK(sfxId), entryIndex);
            }
        } else {
            prevEntryIndex = entryIndex;
        }

        entryIndex = gMmSfxBanks[SFX_BANK(sfxId)][prevEntryIndex].next;
    }

    entryToRemove.sfxId = sfxId;
    AudioMmSfx_RemoveMatchingRequests(SFX_RM_REQ_BY_ID, &entryToRemove);

    // Guard against a stale same-frame PlaySfx request resurrecting this id
    // before the next drain cycle (see sMmStopGuard comment).
    MmStopGuard_Add((u16)sfxId);
}

// VERBATIM from sfx.c:853-860 (replaced enabled-gate with our static)
void AudioMmSfx_ProcessRequests(void) {
    if (!sMmSfxEngineReady) return;
    while (sMmSfxRequestWriteIndex != sMmSfxRequestReadIndex) {
        AudioMmSfx_ProcessRequest();
        sMmSfxRequestReadIndex++;
    }
    // Clear the stop-guard after the drain — guarded ids only need to survive
    // one cycle to catch the stale request queued alongside the StopById.
    sMmStopGuardCount = 0;
}

// VERBATIM from sfx.c:875-885
static void AudioMmSfx_StepBankLerp(u8 bankId) {
    if (sMmSfxBankLerp[bankId].remainingFrames != 0) {
        sMmSfxBankLerp[bankId].remainingFrames--;

        if (sMmSfxBankLerp[bankId].remainingFrames != 0) {
            sMmSfxBankLerp[bankId].value -= sMmSfxBankLerp[bankId].step;
        } else {
            sMmSfxBankLerp[bankId].value = sMmSfxBankLerp[bankId].target;
        }
    }
}

// VERBATIM from sfx.c:887-899
void AudioMmSfx_ProcessActiveSfx(void) {
    u8 bankId;
    if (!sMmSfxEngineReady) return;

    sMmCurSfxPlayerChannelIndex = 0;

    for (bankId = 0; bankId < ARRAY_COUNT(gMmSfxBanks); bankId++) {
        AudioMmSfx_ChooseActiveSfx(bankId);
        AudioMmSfx_PlayActiveSfx(bankId);
        AudioMmSfx_StepBankLerp(bankId);
    }
}

// VERBATIM from sfx.c:901-914
u8 AudioMmSfx_IsPlaying(u32 sfxId) {
    MmSfxBankEntry* entry;
    u8 entryIndex = gMmSfxBanks[SFX_BANK(sfxId)][0].next;

    while (entryIndex != 0xFF) {
        entry = &gMmSfxBanks[SFX_BANK(sfxId)][entryIndex];
        if (entry->sfxId == sfxId) {
            return 1;
        }
        entryIndex = entry->next;
    }
    return 0;
}

// VERBATIM from sfx.c:916-952
void AudioMmSfx_Reset(void) {
    u8 bankId;
    u8 i;

    sMmSfxRequestWriteIndex = 0;
    sMmSfxRequestReadIndex = 0;
    sMmSfxChannelLowVolumeFlag = 0;

    for (bankId = 0; bankId < ARRAY_COUNT(gMmSfxBanks); bankId++) {
        sMmSfxBankListEnd[bankId] = 0;
        sMmSfxBankFreeListStart[bankId] = 1;
        sMmSfxBankUnused[bankId] = 0;
        sMmSfxBankMuted[bankId] = 0;
        sMmSfxBankLerp[bankId].value = 1.0f;
        sMmSfxBankLerp[bankId].remainingFrames = 0;
    }

    for (bankId = 0; bankId < ARRAY_COUNT(gMmSfxBanks); bankId++) {
        for (i = 0; i < MAX_CHANNELS_PER_BANK; i++) {
            gMmActiveSfx[bankId][i].entryIndex = 0xFF;
        }
    }

    for (bankId = 0; bankId < ARRAY_COUNT(gMmSfxBanks); bankId++) {
        gMmSfxBanks[bankId][0].prev = 0xFF;
        gMmSfxBanks[bankId][0].next = 0xFF;

        for (i = 1; i < sMmSfxBankSizes[bankId] - 1; i++) {
            gMmSfxBanks[bankId][i].prev = i - 1;
            gMmSfxBanks[bankId][i].next = i + 1;
        }

        gMmSfxBanks[bankId][i].prev = i - 1;
        gMmSfxBanks[bankId][i].next = 0xFF;
    }

    sMmSfxEngineReady = 1;
}
