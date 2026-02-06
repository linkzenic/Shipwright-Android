/**
 * @file mm_soundfont.h
 * @brief MM SoundFont structures
 *
 * These match the OOT structures in z64audio.h.
 * MM and OOT use the same audio format, so we just typedef to OOT types.
 */

#ifndef MM_SOUNDFONT_H
#define MM_SOUNDFONT_H

#include "z64audio.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Type Aliases (MM names -> OOT types)
// =============================================================================

// MM uses "Sample", OOT uses "SoundFontSample" - same struct
typedef SoundFontSample MmSample;

// MM uses "TunedSample" / "SoundFontSound", OOT uses "SoundFontSound" - same struct
typedef SoundFontSound MmTunedSample;

// MM uses "SoundEffect" which wraps TunedSample
typedef struct MmSoundEffect {
    MmTunedSample tunedSample;
} MmSoundEffect;

// MM SoundFont matches OOT SoundFont
typedef SoundFont MmSoundFont;

// =============================================================================
// MM Audio Bank IDs
// =============================================================================

// MM uses same bank structure as OOT
// Bank 0 = Player sounds
// Bank 1 = Item sounds
// Bank 2 = Environment sounds
// Bank 3 = Enemy sounds
// Bank 4 = System sounds
// Bank 5 = Ocarina sounds
// Bank 6 = Voice sounds

#define MM_AUDIO_BANK_PLAYER 0
#define MM_AUDIO_BANK_ITEM 1
#define MM_AUDIO_BANK_ENV 2
#define MM_AUDIO_BANK_ENEMY 3
#define MM_AUDIO_BANK_SYSTEM 4
#define MM_AUDIO_BANK_OCARINA 5
#define MM_AUDIO_BANK_VOICE 6

// =============================================================================
// MM SFX ID Macros (matching MM's sfx.h)
// =============================================================================

// SFX ID format in MM:
// Bits 15-12: Bank ID (0-6)
// Bits 11-9:  Unknown/flags
// Bits 8-0:   SFX index within bank

#define MM_SOUNDINDEX_SHIFT 0
#define MM_SOUNDINDEX_MASK 0x01FF

#define MM_SOUNDPARAMS_SHIFT 9
#define MM_SOUNDPARAMS_MASK 0x7

#define MM_SOUNDBANK_SHIFT 12
#define MM_SOUNDBANK_MASK 0xF

// Extract parts from SFX ID
#define MM_SFX_BANK_INDEX(sfxId) (((sfxId) >> MM_SOUNDBANK_SHIFT) & MM_SOUNDBANK_MASK)
#define MM_SFX_SOUND_INDEX(sfxId) (((sfxId) >> MM_SOUNDINDEX_SHIFT) & MM_SOUNDINDEX_MASK)
#define MM_SFX_PARAMS(sfxId) (((sfxId) >> MM_SOUNDPARAMS_SHIFT) & MM_SOUNDPARAMS_MASK)

// =============================================================================
// SFX Flags (from MM's sfx.h)
// =============================================================================

#define MM_SFX_FLAG_NONE 0
#define MM_SFX_FLAG_BEHIND_SCREEN_Z_INDEX (1 << 0) // 0x0001

#ifdef __cplusplus
}
#endif

#endif // MM_SOUNDFONT_H
