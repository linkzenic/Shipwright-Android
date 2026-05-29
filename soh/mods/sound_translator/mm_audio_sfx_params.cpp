/**
 * @file mm_audio_sfx_params.cpp
 * @brief MM SFX parameter tables — verbatim port of 2Ship's sfx_params.c.
 *
 * Source: c:/Users/LENOVO/Documents/GitHub/2ship/2ship2harkinian/mm/src/audio/sfx_params.c
 *
 * Renames applied (only renames, no logic changes):
 *   - SfxParams      -> MmSfxParams
 *   - gSfxParams     -> gMmSfxParams
 *   - s{X}BankParams -> sMm{X}BankParams (avoids any collision with future OOT)
 *
 * The bank table headers under mm/include/tables/sfx/*bank_table.h are copied
 * verbatim into ../mm_sources/audio/sfx/. They use unprefixed macros like
 * SFX_FLAG_BEHIND_SCREEN_Z_INDEX / SFX_PARAM_DIST_RANGE_SHIFT etc. To compile
 * them unchanged, we provide TU-local aliases below from the prefixed
 * MM_SFX_* macros in mm_audio_sfx.h.
 *
 * The first DEFINE_SFX argument is the SFX enum NAME (e.g. NA_SE_PL_WALK_GROUND).
 * Those names are never defined in SoH — the macro discards the parameter, so
 * they remain valid tokens. No code change needed in the tables.
 */

#include "mm_audio_sfx.h"

// =============================================================================
// TU-local SFX_FLAG_* aliases so the verbatim bank tables compile
// =============================================================================

#define SFX_PARAM_DIST_RANGE_SHIFT          MM_SFX_PARAM_DIST_RANGE_SHIFT
#define SFX_PARAM_DIST_RANGE_MASK           MM_SFX_PARAM_DIST_RANGE_MASK
#define SFX_FLAG_LOWER_VOLUME_BGM           MM_SFX_FLAG_LOWER_VOLUME_BGM
#define SFX_FLAG_PRIORITY_NO_DIST           MM_SFX_FLAG_PRIORITY_NO_DIST
#define SFX_FLAG_BLOCK_EQUAL_IMPORTANCE     MM_SFX_FLAG_BLOCK_EQUAL_IMPORTANCE
#define SFX_PARAM_RAND_FREQ_RAISE_SHIFT     MM_SFX_PARAM_RAND_FREQ_RAISE_SHIFT
#define SFX_PARAM_RAND_FREQ_RAISE_MASK      MM_SFX_PARAM_RAND_FREQ_RAISE_MASK
#define SFX_FLAG_8                          MM_SFX_FLAG_8
#define SFX_FLAG_SURROUND_LOWPASS_FILTER    MM_SFX_FLAG_SURROUND_LOWPASS_FILTER
#define SFX_FLAG_BEHIND_SCREEN_Z_INDEX      MM_SFX_FLAG_BEHIND_SCREEN_Z_INDEX
#define SFX_PARAM_RAND_FREQ_SCALE           MM_SFX_PARAM_RAND_FREQ_SCALE
#define SFX_FLAG_REVERB_NO_DIST             MM_SFX_FLAG_REVERB_NO_DIST
#define SFX_FLAG_VOLUME_NO_DIST             MM_SFX_FLAG_VOLUME_NO_DIST
#define SFX_PARAM_RAND_FREQ_LOWER           MM_SFX_PARAM_RAND_FREQ_LOWER
#define SFX_FLAG_FREQ_NO_DIST               MM_SFX_FLAG_FREQ_NO_DIST
#define SFX_FLAG2_FORCE_RESET               MM_SFX_FLAG2_FORCE_RESET
#define SFX_FLAG2_UNUSED2                   MM_SFX_FLAG2_UNUSED2
#define SFX_FLAG2_UNUSED4                   MM_SFX_FLAG2_UNUSED4
#define SFX_FLAG2_SURROUND_NO_HIGHPASS_FILTER MM_SFX_FLAG2_SURROUND_NO_HIGHPASS_FILTER
#define SFX_FLAG2_UNUSED6                   MM_SFX_FLAG2_UNUSED6
#define SFX_FLAG2_APPLY_LOWPASS_FILTER      MM_SFX_FLAG2_APPLY_LOWPASS_FILTER

// =============================================================================
// DEFINE_SFX macro — verbatim from sfx_params.c:3-6
// =============================================================================

#define DEFINE_SFX(_0, importance, distParam, randParam, flags2, flags1)           \
    { (u8)(importance), (u8)(flags2),                                              \
      (u16)((((distParam) << SFX_PARAM_DIST_RANGE_SHIFT) & SFX_PARAM_DIST_RANGE_MASK) | \
            (((randParam) << SFX_PARAM_RAND_FREQ_RAISE_SHIFT) & SFX_PARAM_RAND_FREQ_RAISE_MASK) | (flags1)) },

// =============================================================================
// Bank tables — VERBATIM from 2Ship include/tables/sfx/*bank_table.h
// =============================================================================

static MmSfxParams sMmEnemyBankParams[] = {
#include "../mm_sources/audio/sfx/enemybank_table.h"
};

static MmSfxParams sMmPlayerBankParams[] = {
#include "../mm_sources/audio/sfx/playerbank_table.h"
};

static MmSfxParams sMmItemBankParams[] = {
#include "../mm_sources/audio/sfx/itembank_table.h"
};

static MmSfxParams sMmEnvBankParams[] = {
#include "../mm_sources/audio/sfx/environmentbank_table.h"
};

static MmSfxParams sMmSystemBankParams[] = {
#include "../mm_sources/audio/sfx/systembank_table.h"
};

static MmSfxParams sMmOcarinaBankParams[] = {
#include "../mm_sources/audio/sfx/ocarinabank_table.h"
};

static MmSfxParams sMmVoiceBankParams[] = {
#include "../mm_sources/audio/sfx/voicebank_table.h"
};

#undef DEFINE_SFX

// =============================================================================
// Public array — verbatim layout from sfx_params.c:38-41
// =============================================================================

MmSfxParams* gMmSfxParams[7] = {
    sMmPlayerBankParams, sMmItemBankParams,    sMmEnvBankParams,    sMmEnemyBankParams,
    sMmSystemBankParams, sMmOcarinaBankParams, sMmVoiceBankParams,
};
