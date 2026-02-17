/**
 * @file mm_sfx_ids.h
 * @brief MM Sound Effect IDs - verified against mm_decomp bank tables (2026-02-13)
 *
 * Sources: mm_decomp/include/tables/sfx/{playerbank,itembank,systembank,voicebank}_table.h
 * All hex IDs verified against the 0xXXXX comments in those files.
 */

#ifndef MM_SFX_IDS_H
#define MM_SFX_IDS_H

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// MM SFX ID Format (from z64audio.h)
// =============================================================================
// SFX IDs have format: (bank << 12) | SFX_FLAG(0x800) | index
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
// Common Player SFX (used by all forms) - playerbank_table.h
// =============================================================================

#define MM_NA_SE_PL_WALK_GROUND   0x0800 // Walking
#define MM_NA_SE_PL_WALK_SAND     0x0801 // Walking on sand
#define MM_NA_SE_PL_WALK_CONCRETE 0x0802 // Walking on stone
#define MM_NA_SE_PL_WALK_DIRT     0x0803 // Walking on dirt
#define MM_NA_SE_PL_WALK_WATER    0x0804 // Walking in water
#define MM_NA_SE_PL_JUMP          0x0811 // Jump
#define MM_NA_SE_PL_LAND          0x0812 // Landing
#define MM_NA_SE_PL_SLIPDOWN      0x0813 // Slipping
#define MM_NA_SE_PL_CLIMB_CLIFF   0x0814 // Climbing
#define MM_NA_SE_PL_SIT_ON_HORSE  0x0815 // Mount horse
#define MM_NA_SE_PL_GET_OFF_HORSE 0x0816 // Dismount
#define MM_NA_SE_PL_SLIP_LEVEL    0x08D0 // Sliding on floor

// =============================================================================
// Transformation SFX (playerbank_table.h) - shared by all mask transforms
// =============================================================================

#define MM_NA_SE_PL_TRANSFORM       0x08E4 // Transformation sound (3 layers)
#define MM_NA_SE_PL_TRANSFORM_DEMO  0x08E5 // Transformation cutscene demo sound
#define MM_NA_SE_PL_TRANSFORM_VOICE 0x09AA // Transformation scream/voice

// Giant/Normal mask transforms
#define MM_NA_SE_PL_TRANSFORM_GIANT  0x09C5 // Giant's Mask transform
#define MM_NA_SE_PL_TRANSFORM_NORAML 0x09C6 // Giant's Mask revert to normal (sic: typo from decomp)

// =============================================================================
// Deku SFX IDs (playerbank_table.h)
// =============================================================================

// Deku actions
#define MM_NA_SE_PL_DEKUNUTS_FIRE         0x08E0 // /* 0x8E0 */ Bubble fire/spit
#define MM_NA_SE_PL_DEKUNUTS_IN_GRD       0x08E2 // /* 0x8E2 */ Enter ground (flower)
#define MM_NA_SE_PL_DEKUNUTS_OUT_GRD      0x08E3 // /* 0x8E3 */ Exit ground (flower)
#define MM_NA_SE_PL_DEKUNUTS_BUD          0x09A0 // /* 0x9A0 */ Flower bud sound
#define MM_NA_SE_PL_DEKUNUTS_BUBLE_BREATH 0x09A1 // /* 0x9A1 */ Bubble charge/breath
#define MM_NA_SE_PL_DEKUNUTS_STRUGGLE     0x09A6 // /* 0x9A6 */ Struggle sound
#define MM_NA_SE_PL_DEKUNUTS_ATTACK       0x09A9 // /* 0x9A9 */ Spin attack
#define MM_NA_SE_PL_DEKUNUTS_DROP_BOMB    0x09AC // /* 0x9AC */ Drop bomb

// Deku jump sounds (water hop sequence)
#define MM_NA_SE_PL_DEKUNUTS_JUMP  0x09B0 // /* 0x9B0 */ Jump/hop 1
#define MM_NA_SE_PL_DEKUNUTS_JUMP2 0x09B1 // /* 0x9B1 */ Jump/hop 2
#define MM_NA_SE_PL_DEKUNUTS_JUMP3 0x09B2 // /* 0x9B2 */ Jump/hop 3
#define MM_NA_SE_PL_DEKUNUTS_JUMP4 0x09B3 // /* 0x9B3 */ Jump/hop 4
#define MM_NA_SE_PL_DEKUNUTS_JUMP5 0x09B4 // /* 0x9B4 */ Jump/hop 5
#define MM_NA_SE_PL_DEKUNUTS_JUMP6 0x09B5 // /* 0x9B5 */ Jump/hop 6 (FIX: was 0x09B6)
#define MM_NA_SE_PL_DEKUNUTS_JUMP7 0x09B6 // /* 0x9B6 */ Jump/hop 7
#define MM_NA_SE_PL_DEKUNUTS_JUMP8 0x09B7 // /* 0x9B7 */ Jump/hop 8

// Deku misc
#define MM_NA_SE_PL_DEKUNUTS_MISS_FIRE 0x09BF // /* 0x9BF */ Missed shot
#define MM_NA_SE_PL_WALK_WALL_DEKU     0x09CF // /* 0x9CF */ Wall climbing as Deku

// =============================================================================
// Goron SFX IDs (playerbank_table.h)
// =============================================================================

// Goron transformation and actions
#define MM_NA_SE_PL_GORON_BALLJUMP    0x08E1 // /* 0x8E1 */ Jump in ball form
#define MM_NA_SE_PL_GORON_TO_BALL     0x08E6 // /* 0x8E6 */ Transform to ball
#define MM_NA_SE_PL_BALL_TO_GORON     0x08E7 // /* 0x8E7 */ Ball to goron
#define MM_NA_SE_PL_GORON_PUNCH       0x08E8 // /* 0x8E8 */ Goron punch attack
#define MM_NA_SE_PL_GORON_BALL_CHARGE 0x08EB // /* 0x8EB */ Charging roll
#define MM_NA_SE_PL_GORON_SQUAT       0x08EF // /* 0x8EF */ Squat down

// Goron roll sounds
#define MM_NA_SE_PL_GORON_CHG_ROLL     0x0980 // /* 0x980 */ Charged roll start
#define MM_NA_SE_PL_GORON_CHG_ROLL_ICE 0x098F // /* 0x98F */ Charged roll on ice
#define MM_NA_SE_PL_GORON_ROLL         0x0990 // /* 0x990 */ Rolling sound
#define MM_NA_SE_PL_GORON_ROLL_ICE     0x099F // /* 0x99F */ Rolling on ice

// Goron misc
#define MM_NA_SE_PL_GORON_BALL_CHARGE_FAILED 0x09A2 // /* 0x9A2 */ Failed charge
#define MM_NA_SE_PL_GORON_BALL_CHARGE_DASH   0x09A3 // /* 0x9A3 */ Charge dash
#define MM_NA_SE_PL_GORON_SLIP               0x09AD // /* 0x9AD */ Slipping
#define MM_NA_SE_PL_GORON_STOMACH_EXPLOSION  0x09B8 // /* 0x9B8 */ Bomb swallow explosion
#define MM_NA_SE_PL_GORON_DRINK_BOMB         0x09B9 // /* 0x9B9 */ Drinking bomb
#define MM_NA_SE_PL_LI_FUTTOBI              0x09C8 // /* 0x9C8 */ Goron impact/blown away

// Compat alias (old name used in some code)
#define MM_NA_SE_PL_GORON_BALL_TO_GORON MM_NA_SE_PL_BALL_TO_GORON

// =============================================================================
// Zora SFX IDs (playerbank_table.h)
// =============================================================================

#define MM_NA_SE_PL_ZORA_SWIM_DASH       0x08EC // /* 0x8EC */ Dash swimming
#define MM_NA_SE_PL_ZORA_SWIM_LV         0x08ED // /* 0x8ED */ Level swimming
#define MM_NA_SE_PL_ZORA_SWIM_ROLL       0x08EE // /* 0x8EE */ Swim roll
#define MM_NA_SE_PL_ZORA_SWIM            0x08F0 // /* 0x8F0 */ Swimming
#define MM_NA_SE_PL_ZORA_KICK            0x08F1 // /* 0x8F1 */ Kick attack
#define MM_NA_SE_PL_ZORA_DIVE            0x08F2 // /* 0x8F2 */ Diving
#define MM_NA_SE_PL_ZORA_ELECTRIC_BARRIER 0x08F3 // /* 0x8F3 */ Electric barrier
#define MM_NA_SE_PL_ZORA_BOOMERANG_THROW 0x08F4 // /* 0x8F4 */ Fin boomerang throw
#define MM_NA_SE_PL_ZORA_BOOMERANG_CATCH 0x08F5 // /* 0x8F5 */ Fin boomerang catch
#define MM_NA_SE_PL_ZORA_SPARK_BARRIER   0x09AF // /* 0x9AF */ Spark barrier sound

// =============================================================================
// Item Bank SFX (itembank_table.h) - form-specific item sounds
// =============================================================================

// Goron items
#define MM_NA_SE_IT_GORON_BALLFANG           0x184F // /* 0x184F */ Ball fang/bite
#define MM_NA_SE_IT_GORON_PUNCH_SWING        0x1857 // /* 0x1857 */ Punch swing whoosh
#define MM_NA_SE_IT_GORON_ROLLING_REFLECTION 0x185E // /* 0x185E */ Wall bounce (FIX: was 0x1847)

// Deku items
#define MM_NA_SE_IT_DEKUNUTS_FLOWER_OPEN     0x1850 // /* 0x1850 */ Flower open
#define MM_NA_SE_IT_DEKUNUTS_FLOWER_ROLL     0x1851 // /* 0x1851 */ Flower roll
#define MM_NA_SE_IT_DEKUNUTS_FLOWER_CLOSE    0x1852 // /* 0x1852 */ Flower close
#define MM_NA_SE_IT_DEKUNUTS_BUBLE_BROKEN    0x1853 // /* 0x1853 */ Bubble broken
#define MM_NA_SE_IT_DEKUNUTS_BUBLE_VANISH    0x1854 // /* 0x1854 */ Bubble vanish
#define MM_NA_SE_IT_DEKUNUTS_DROP_BOMB       0x1855 // /* 0x1855 */ Drop bomb
#define MM_NA_SE_IT_DEKUNUTS_BUBLE_SHOT_LEVEL 0x185A // /* 0x185A */ Bubble shot level

// Zora items
#define MM_NA_SE_IT_ZORA_KICK_SWING       0x1859 // /* 0x1859 */ Kick swing whoosh
#define MM_NA_SE_IT_SHIELD_SWING_ZORA     0x1868 // /* 0x1868 */ Zora shield swing
#define MM_NA_SE_IT_SHIELD_REMOVE_ZORA    0x1869 // /* 0x1869 */ Zora shield remove

// =============================================================================
// System Bank SFX (systembank_table.h)
// =============================================================================

#define MM_NA_SE_SY_TRANSFORM_MASK_FLASH 0x484F // /* 0x484F */ Mask flash during transform

// =============================================================================
// Voice SFX (voicebank_table.h)
// =============================================================================
// Voice bank = 0x6800 + form offset
// Form offsets: FierceDeity=0x00, Deku=0x80, Zora=0xA0, Goron=0xC0
//
// Within each form, voice indices map to the same roles:
//   +0x00=SWORD_N  +0x01=SWORD_L    +0x02=LASH       +0x03=HANG
//   +0x04=CLIMB_END +0x05=DAMAGE_S  +0x06=FREEZE     +0x07=FALL_S
//   +0x08=FALL_L    +0x09=BREATH_REST +0x0A=BREATH_DRINK +0x0B=DOWN
//   +0x0C=TAKEN_AWAY +0x0D=HELD     +0x0E=SNEEZE     +0x0F=SWEAT
//   +0x10=DRINK     +0x11=RELAX     +0x12=SWORD_PUTAWAY +0x13=GROAN
//   +0x14=AUTO_JUMP  +0x15=MAGIC_NALE +0x16=SURPRISE  +0x17=MAGIC_FROL
//   +0x18=PUSH      +0x19=HOOKSHOT_HANG +0x1A=LAND_DAMAGE_S +0x1B=MAGIC_START
//   +0x1C=MAGIC_ATTACK +0x1D=BL_DOWN  +0x1E=DEMO_DAMAGE +0x1F=(last)

// --- Fierce Deity / Human Link voice (0x6800-0x681E) ---
#define MM_NA_SE_VO_LI_SWORD_N       0x6800 // Normal sword attack
#define MM_NA_SE_VO_LI_SWORD_L       0x6801 // Strong sword attack
#define MM_NA_SE_VO_LI_LASH          0x6802 // Lash/whip
#define MM_NA_SE_VO_LI_HANG          0x6803 // Hanging
#define MM_NA_SE_VO_LI_CLIMB_END     0x6804 // Finish climbing
#define MM_NA_SE_VO_LI_DAMAGE_S      0x6805 // Small damage
#define MM_NA_SE_VO_LI_FREEZE        0x6806 // Freeze
#define MM_NA_SE_VO_LI_FALL_S        0x6807 // Short fall
#define MM_NA_SE_VO_LI_FALL_L        0x6808 // Long fall
#define MM_NA_SE_VO_LI_BREATH_REST   0x6809 // Rest breath
#define MM_NA_SE_VO_LI_BREATH_DRINK  0x680A // Drinking breath
#define MM_NA_SE_VO_LI_DOWN          0x680B // Knocked down
#define MM_NA_SE_VO_LI_TAKEN_AWAY    0x680C // Taken away
#define MM_NA_SE_VO_LI_HELD          0x680D // Held/grabbed
#define MM_NA_SE_VO_LI_SNEEZE        0x680E // Sneeze
#define MM_NA_SE_VO_LI_SWEAT         0x680F // Sweat/exhaustion
#define MM_NA_SE_VO_LI_DRINK         0x6810 // Drinking
#define MM_NA_SE_VO_LI_RELAX         0x6811 // Relaxing
#define MM_NA_SE_VO_LI_SWORD_PUTAWAY 0x6812 // Sword put away
#define MM_NA_SE_VO_LI_GROAN         0x6813 // Groan
#define MM_NA_SE_VO_LI_AUTO_JUMP     0x6814 // Auto jump
#define MM_NA_SE_VO_LI_MAGIC_NALE    0x6815 // Nayru's Love
#define MM_NA_SE_VO_LI_SURPRISE      0x6816 // Surprise
#define MM_NA_SE_VO_LI_MAGIC_FROL    0x6817 // Farore's Wind
#define MM_NA_SE_VO_LI_PUSH          0x6818 // Pushing
#define MM_NA_SE_VO_LI_HOOKSHOT_HANG 0x6819 // Hookshot hang
#define MM_NA_SE_VO_LI_LAND_DAMAGE_S 0x681A // Landing damage
#define MM_NA_SE_VO_LI_MAGIC_START   0x681B // Magic start
#define MM_NA_SE_VO_LI_MAGIC_ATTACK  0x681C // Magic attack
#define MM_NA_SE_VO_BL_DOWN          0x681D // Knocked out
#define MM_NA_SE_VO_LI_DEMO_DAMAGE   0x681E // Demo damage

// --- Deku Link voice (0x6880-0x689F) --- (FIX: was 0x68E0, correct is 0x6880)
#define MM_NA_SE_VO_DEKU_SWORD_N       0x6880 // Normal attack
#define MM_NA_SE_VO_DEKU_SWORD_L       0x6881 // Strong attack
#define MM_NA_SE_VO_DEKU_LASH          0x6882 // Lash
#define MM_NA_SE_VO_DEKU_HANG          0x6883 // Hanging
#define MM_NA_SE_VO_DEKU_CLIMB_END     0x6884 // Finish climbing
#define MM_NA_SE_VO_DEKU_DAMAGE_S      0x6885 // Small damage
#define MM_NA_SE_VO_DEKU_FREEZE        0x6886 // Freeze
#define MM_NA_SE_VO_DEKU_FALL_S        0x6887 // Short fall
#define MM_NA_SE_VO_DEKU_FALL_L        0x6888 // Long fall
#define MM_NA_SE_VO_DEKU_BREATH_REST   0x6889 // Rest breath
#define MM_NA_SE_VO_DEKU_BREATH_DRINK  0x688A // Drinking breath
#define MM_NA_SE_VO_DEKU_DOWN          0x688B // Knocked down
#define MM_NA_SE_VO_DEKU_TAKEN_AWAY    0x688C // Taken away
#define MM_NA_SE_VO_DEKU_HELD          0x688D // Held/grabbed
#define MM_NA_SE_VO_DEKU_SNEEZE        0x688E // Sneeze
#define MM_NA_SE_VO_DEKU_SWEAT         0x688F // Sweat/exhaustion
#define MM_NA_SE_VO_DEKU_DRINK         0x6890 // Drinking
#define MM_NA_SE_VO_DEKU_RELAX         0x6891 // Relaxing
#define MM_NA_SE_VO_DEKU_SWORD_PUTAWAY 0x6892 // Sword put away
#define MM_NA_SE_VO_DEKU_GROAN         0x6893 // Groan
#define MM_NA_SE_VO_DEKU_AUTO_JUMP     0x6894 // Auto jump
#define MM_NA_SE_VO_DEKU_MAGIC_NALE    0x6895 // Magic
#define MM_NA_SE_VO_DEKU_SURPRISE      0x6896 // Surprise
#define MM_NA_SE_VO_DEKU_MAGIC_FROL    0x6897 // Magic
#define MM_NA_SE_VO_DEKU_PUSH          0x6898 // Pushing
#define MM_NA_SE_VO_DEKU_HOOKSHOT_HANG 0x6899 // Hookshot hang
#define MM_NA_SE_VO_DEKU_LAND_DAMAGE_S 0x689A // Landing damage
#define MM_NA_SE_VO_DEKU_MAGIC_START   0x689B // Magic start
#define MM_NA_SE_VO_DEKU_MAGIC_ATTACK  0x689C // Magic attack
#define MM_NA_SE_VO_DEKU_BL_DOWN       0x689D // Knocked out
#define MM_NA_SE_VO_DEKU_DEMO_DAMAGE   0x689E // Demo damage
#define MM_NA_SE_VO_DEKU_LAST          0x689F // Last Deku voice slot

// --- Zora Link voice (0x68A0-0x68BF) ---
#define MM_NA_SE_VO_ZORA_SWORD_N       0x68A0 // Normal attack
#define MM_NA_SE_VO_ZORA_SWORD_L       0x68A1 // Strong attack
#define MM_NA_SE_VO_ZORA_LASH          0x68A2 // Lash
#define MM_NA_SE_VO_ZORA_HANG          0x68A3 // Hanging
#define MM_NA_SE_VO_ZORA_CLIMB_END     0x68A4 // Finish climbing
#define MM_NA_SE_VO_ZORA_DAMAGE_S      0x68A5 // Small damage
#define MM_NA_SE_VO_ZORA_FREEZE        0x68A6 // Freeze
#define MM_NA_SE_VO_ZORA_FALL_S        0x68A7 // Short fall
#define MM_NA_SE_VO_ZORA_FALL_L        0x68A8 // Long fall
#define MM_NA_SE_VO_ZORA_BREATH_REST   0x68A9 // Rest breath
#define MM_NA_SE_VO_ZORA_BREATH_DRINK  0x68AA // Drinking breath
#define MM_NA_SE_VO_ZORA_DOWN          0x68AB // Knocked down
#define MM_NA_SE_VO_ZORA_TAKEN_AWAY    0x68AC // Taken away
#define MM_NA_SE_VO_ZORA_HELD          0x68AD // Held/grabbed
#define MM_NA_SE_VO_ZORA_SNEEZE        0x68AE // Sneeze
#define MM_NA_SE_VO_ZORA_SWEAT         0x68AF // Sweat/exhaustion
#define MM_NA_SE_VO_ZORA_DRINK         0x68B0 // Drinking
#define MM_NA_SE_VO_ZORA_RELAX         0x68B1 // Relaxing
#define MM_NA_SE_VO_ZORA_SWORD_PUTAWAY 0x68B2 // Sword put away
#define MM_NA_SE_VO_ZORA_GROAN         0x68B3 // Groan
#define MM_NA_SE_VO_ZORA_AUTO_JUMP     0x68B4 // Auto jump
#define MM_NA_SE_VO_ZORA_MAGIC_NALE    0x68B5 // Magic
#define MM_NA_SE_VO_ZORA_SURPRISE      0x68B6 // Surprise
#define MM_NA_SE_VO_ZORA_MAGIC_FROL    0x68B7 // Magic
#define MM_NA_SE_VO_ZORA_PUSH          0x68B8 // Pushing
#define MM_NA_SE_VO_ZORA_HOOKSHOT_HANG 0x68B9 // Hookshot hang
#define MM_NA_SE_VO_ZORA_LAND_DAMAGE_S 0x68BA // Landing damage
#define MM_NA_SE_VO_ZORA_MAGIC_START   0x68BB // Magic start
#define MM_NA_SE_VO_ZORA_MAGIC_ATTACK  0x68BC // Magic attack
#define MM_NA_SE_VO_ZORA_BL_DOWN       0x68BD // Knocked out
#define MM_NA_SE_VO_ZORA_DEMO_DAMAGE   0x68BE // Demo damage
#define MM_NA_SE_VO_ZORA_LAST          0x68BF // Last Zora voice slot

// --- Goron Link voice (0x68C0-0x68DF) ---
#define MM_NA_SE_VO_GORON_SWORD_N       0x68C0 // Normal attack
#define MM_NA_SE_VO_GORON_SWORD_L       0x68C1 // Strong attack
#define MM_NA_SE_VO_GORON_LASH          0x68C2 // Lash
#define MM_NA_SE_VO_GORON_HANG          0x68C3 // Hanging
#define MM_NA_SE_VO_GORON_CLIMB_END     0x68C4 // Finish climbing
#define MM_NA_SE_VO_GORON_DAMAGE_S      0x68C5 // Small damage
#define MM_NA_SE_VO_GORON_FREEZE        0x68C6 // Freeze
#define MM_NA_SE_VO_GORON_FALL_S        0x68C7 // Short fall
#define MM_NA_SE_VO_GORON_FALL_L        0x68C8 // Long fall
#define MM_NA_SE_VO_GORON_BREATH_REST   0x68C9 // Rest breath
#define MM_NA_SE_VO_GORON_BREATH_DRINK  0x68CA // Drinking breath
#define MM_NA_SE_VO_GORON_DOWN          0x68CB // Knocked down
#define MM_NA_SE_VO_GORON_TAKEN_AWAY    0x68CC // Taken away
#define MM_NA_SE_VO_GORON_HELD          0x68CD // Held/grabbed
#define MM_NA_SE_VO_GORON_SNEEZE        0x68CE // Sneeze
#define MM_NA_SE_VO_GORON_SWEAT         0x68CF // Sweat/exhaustion
#define MM_NA_SE_VO_GORON_DRINK         0x68D0 // Drinking
#define MM_NA_SE_VO_GORON_RELAX         0x68D1 // Relaxing
#define MM_NA_SE_VO_GORON_SWORD_PUTAWAY 0x68D2 // Sword put away
#define MM_NA_SE_VO_GORON_GROAN         0x68D3 // Groan
#define MM_NA_SE_VO_GORON_AUTO_JUMP     0x68D4 // Auto jump
#define MM_NA_SE_VO_GORON_MAGIC_NALE    0x68D5 // Magic
#define MM_NA_SE_VO_GORON_SURPRISE      0x68D6 // Surprise
#define MM_NA_SE_VO_GORON_MAGIC_FROL    0x68D7 // Magic
#define MM_NA_SE_VO_GORON_PUSH          0x68D8 // Pushing
#define MM_NA_SE_VO_GORON_HOOKSHOT_HANG 0x68D9 // Hookshot hang
#define MM_NA_SE_VO_GORON_LAND_DAMAGE_S 0x68DA // Landing damage
#define MM_NA_SE_VO_GORON_MAGIC_START   0x68DB // Magic start
#define MM_NA_SE_VO_GORON_MAGIC_ATTACK  0x68DC // Magic attack
#define MM_NA_SE_VO_GORON_BL_DOWN       0x68DD // Knocked out
#define MM_NA_SE_VO_GORON_DEMO_DAMAGE   0x68DE // Demo damage
#define MM_NA_SE_VO_GORON_LAST          0x68DF // Last Goron voice slot

#ifdef __cplusplus
}
#endif

#endif // MM_SFX_IDS_H
