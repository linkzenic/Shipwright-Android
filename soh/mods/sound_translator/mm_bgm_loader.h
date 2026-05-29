/**
 * @file mm_bgm_loader.h
 * @brief MM BGM Sequence Loader - Public C API
 *
 * Scans mm.o2r for audio/sequences/* entries at boot time, registers each
 * with SOH's audio engine (AudioCollection + sequenceMap), and exposes a
 * name-keyed lookup so any mod can play any MM BGM by filename.
 *
 * Names are derived from the mm.o2r resource path (directory + extension
 * stripped). For example "audio/sequences/Sequence_83" becomes "Sequence_83".
 * Canonical constants for the BGMs we care about live in mm_bgm_names.h.
 *
 * If mm.o2r is not loaded, every call here is a silent no-op. There is NO
 * OOT BGM fallback — game code that wants MM-specific BGM must accept silence
 * when the MM extraction is missing.
 */

#ifndef MM_BGM_LOADER_H
#define MM_BGM_LOADER_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register all MM sequence files from mm.o2r into SOH's audio engine.
 * Idempotent: subsequent calls after the first successful registration are
 * no-ops. Safe to call before mm.o2r is mounted (returns without doing work).
 *
 * Typical call site: shortly after MmAssets_Init() succeeds.
 */
void MmBgm_RegisterSequences(void);

/**
 * @return 1 if the MM BGM registry is populated, 0 if mm.o2r is missing or
 *         the registration pass has not run yet.
 */
s32 MmBgm_IsAvailable(void);

/**
 * Resolve an MM BGM name to its assigned SOH seq ID.
 * @param mmBgmName Filename-derived name (e.g. "Sequence_83").
 * @return SOH seq ID, or 0xFFFF (NA_BGM_DISABLED) if not found.
 */
u16 MmBgm_GetSeqId(const char* mmBgmName);

/**
 * Play an MM BGM on the fanfare channel (transient — for short cues like
 * Bremen March or Kamaro Dance). No-op if mm.o2r is unavailable or the
 * name is not registered.
 */
void MmBgm_PlayFanfare(const char* mmBgmName);

/**
 * Play an MM BGM on the main BGM channel (looping field/dungeon music).
 */
void MmBgm_PlayMain(const char* mmBgmName);

/**
 * Stop fanfare-channel BGM. Convenience wrapper around NA_BGM_STOP that
 * is safe to call regardless of MM availability.
 */
void MmBgm_StopFanfare(void);

#ifdef __cplusplus
}
#endif

#endif // MM_BGM_LOADER_H
