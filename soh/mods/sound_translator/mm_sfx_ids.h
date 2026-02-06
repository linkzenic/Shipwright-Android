/**
 * @file mm_sfx_ids.h
 * @brief MM Sound Effect IDs
 *
 * Copied from mm_decomp/include/tables/sfx/playerbank_table.h
 * These are the SFX IDs used by MM Player code (Goron, Zora, Deku, etc.)
 */

#ifndef MM_SFX_IDS_H
#define MM_SFX_IDS_H

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// MM SFX ID Format (from z64audio.h)
// =============================================================================
// SFX IDs have format: (bank << 12) | index
// Banks: 0=Player, 1=Item, 2=Environment, 3=Enemy, 4=System, 5=Ocarina, 6=Voice

#define MM_SFX_BANK_PLAYER 0
#define MM_SFX_BANK_ITEM 1
#define MM_SFX_BANK_ENV 2
#define MM_SFX_BANK_ENEMY 3
#define MM_SFX_BANK_SYSTEM 4
#define MM_SFX_BANK_OCARINA 5
#define MM_SFX_BANK_VOICE 6

#define MM_SFX_BANK_SHIFT 12
#define MM_SFX_INDEX_MASK 0x01FF

// Extract bank and index from SFX ID
#define MM_SFX_GET_BANK(sfxId) (((sfxId) >> MM_SFX_BANK_SHIFT) & 0xF)
#define MM_SFX_GET_INDEX(sfxId) ((sfxId)&MM_SFX_INDEX_MASK)

// =============================================================================
// Goron SFX IDs (from playerbank_table.h)
// =============================================================================

// Goron transformation and actions
#define MM_NA_SE_PL_GORON_BALLJUMP 0x08E1    // Jump in ball form
#define MM_NA_SE_PL_GORON_TO_BALL 0x08E6     // Transform to ball
#define MM_NA_SE_PL_GORON_PUNCH 0x08E8       // Goron punch attack
#define MM_NA_SE_PL_GORON_BALL_CHARGE 0x08EB // Charging roll
#define MM_NA_SE_PL_GORON_SQUAT 0x08EF       // Squat down

// Goron roll sounds (with flags)
#define MM_NA_SE_PL_GORON_CHG_ROLL 0x0980     // Charged roll start
#define MM_NA_SE_PL_GORON_CHG_ROLL_ICE 0x098F // Charged roll on ice
#define MM_NA_SE_PL_GORON_ROLL 0x0990         // Rolling sound
#define MM_NA_SE_PL_GORON_ROLL_ICE 0x099F     // Rolling on ice

// Goron misc
#define MM_NA_SE_PL_GORON_BALL_CHARGE_FAILED 0x09A2 // Failed charge
#define MM_NA_SE_PL_GORON_BALL_CHARGE_DASH 0x09A3   // Charge dash
#define MM_NA_SE_PL_GORON_SLIP 0x09AD               // Slipping
#define MM_NA_SE_PL_GORON_STOMACH_EXPLOSION 0x09B8  // Bomb swallow explosion
#define MM_NA_SE_PL_GORON_DRINK_BOMB 0x09B9         // Drinking bomb

// =============================================================================
// Zora SFX IDs (from playerbank_table.h)
// =============================================================================

#define MM_NA_SE_PL_ZORA_SWIM 0x08F0             // Swimming
#define MM_NA_SE_PL_ZORA_KICK 0x08F1             // Kick attack
#define MM_NA_SE_PL_ZORA_DIVE 0x08F2             // Diving
#define MM_NA_SE_PL_ZORA_ELECTRIC_BARRIER 0x08F3 // Electric barrier
#define MM_NA_SE_PL_ZORA_BOOMERANG_THROW 0x08F4  // Fin boomerang throw
#define MM_NA_SE_PL_ZORA_BOOMERANG_CATCH 0x08F5  // Fin boomerang catch

// =============================================================================
// Deku SFX IDs (from playerbank_table.h)
// =============================================================================

#define MM_NA_SE_PL_DEKU_SHOOT 0x08D0 // Bubble shot
#define MM_NA_SE_PL_DEKU_JUMP 0x08D1  // Deku hop
#define MM_NA_SE_PL_DEKU_SPIN 0x08D2  // Spin attack
#define MM_NA_SE_PL_DEKU_FLY 0x08D3   // Flower flight
#define MM_NA_SE_PL_DEKU_WALK 0x08D4  // Walking sound

// =============================================================================
// Transformation SFX IDs (from systembank_table.h)
// =============================================================================

#define MM_NA_SE_SY_TRANSFORM_MASK_FLASH 0x4835 // Mask flash during transform

// =============================================================================
// Common Player SFX (used by all forms)
// =============================================================================

#define MM_NA_SE_PL_WALK_GROUND 0x0800   // Walking
#define MM_NA_SE_PL_WALK_SAND 0x0801     // Walking on sand
#define MM_NA_SE_PL_WALK_CONCRETE 0x0802 // Walking on stone
#define MM_NA_SE_PL_WALK_DIRT 0x0803     // Walking on dirt
#define MM_NA_SE_PL_WALK_WATER 0x0804    // Walking in water
#define MM_NA_SE_PL_JUMP 0x0811          // Jump
#define MM_NA_SE_PL_LAND 0x0812          // Landing
#define MM_NA_SE_PL_SLIPDOWN 0x0813      // Slipping
#define MM_NA_SE_PL_CLIMB_CLIFF 0x0814   // Climbing
#define MM_NA_SE_PL_SIT_ON_HORSE 0x0815  // Mount horse
#define MM_NA_SE_PL_GET_OFF_HORSE 0x0816 // Dismount

// =============================================================================
// Voice SFX (from voicebank_table.h)
// Voice bank = 0x6XXX, forms use offsets: Goron=0xC0, Zora=0xA0, Deku=0xE0
// Base: NA_SE_VO_LI_* starts at 0x6800, add form offset for transformed voices
// =============================================================================

// Goron Link voice (base 0x6800 + offset 0xC0 = 0x68C0)
#define MM_NA_SE_VO_GORON_SWORD_N 0x68C0     // Normal sword attack voice
#define MM_NA_SE_VO_GORON_SWORD_L 0x68C1     // Strong sword attack voice
#define MM_NA_SE_VO_GORON_LASH 0x68C2        // Lash/whip attack voice
#define MM_NA_SE_VO_GORON_HANG 0x68C3        // Hanging voice
#define MM_NA_SE_VO_GORON_CLIMB_END 0x68C4   // Finish climbing voice
#define MM_NA_SE_VO_GORON_DAMAGE_S 0x68C5    // Small damage "ouch!"
#define MM_NA_SE_VO_GORON_FREEZE 0x68C6      // Freeze voice
#define MM_NA_SE_VO_GORON_FALL_S 0x68C7      // Short fall voice
#define MM_NA_SE_VO_GORON_FALL_L 0x68C8      // Long fall voice
#define MM_NA_SE_VO_GORON_BREATH_REST 0x68C9 // Rest breath
#define MM_NA_SE_VO_GORON_DOWN 0x68CB        // Knocked down voice

// Zora Link voice (base 0x6800 + offset 0xA0 = 0x68A0)
#define MM_NA_SE_VO_ZORA_SWORD_N 0x68A0  // Normal attack voice
#define MM_NA_SE_VO_ZORA_DAMAGE_S 0x68A5 // Small damage
#define MM_NA_SE_VO_ZORA_FALL_L 0x68A8   // Long fall voice

// Deku Link voice (base 0x6800 + offset 0xE0 = 0x68E0)
#define MM_NA_SE_VO_DEKU_SWORD_N 0x68E0  // Attack voice
#define MM_NA_SE_VO_DEKU_DAMAGE_S 0x68E5 // Small damage
#define MM_NA_SE_VO_DEKU_FALL_L 0x68E8   // Long fall voice

#ifdef __cplusplus
}
#endif

#endif // MM_SFX_IDS_H
