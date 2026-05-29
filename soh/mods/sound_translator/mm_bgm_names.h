/**
 * @file mm_bgm_names.h
 * @brief Canonical MM BGM names — keyed by mm.o2r resource filename.
 *
 * Sources: mm_decomp/include/tables/sequence_table.h
 *
 * 2Ship/SOH extractions of mm.o2r store each MM sequence under
 * audio/sequences/Sequence_<N>, where N matches the numeric suffix on the
 * sequence_table entry (Sequence_83 → NA_BGM_BREMEN_MARCH, etc.).
 *
 * If your mm.o2r uses different filenames, call MmBgm_GetSeqId with the
 * actual filename — these constants are just convenience aliases for the
 * common cases the transformation_masks system depends on.
 */

#ifndef MM_BGM_NAMES_H
#define MM_BGM_NAMES_H

// 2Ship Audio.xml uses `<Name>_<HEX_ID>` naming (verified against
// `2ship2harkinian/mm/assets/xml/N64_US/audio/Audio.xml`). The "_HH" suffix
// is the sequence's HEX ID inside mm.o2r, NOT the decimal sequence number
// from sequence_table.h. So Sequence_83 (decimal) lives at HEX 0x52 in the
// ROM (which the OTRExporter writes as `BremenMarch_52`).
//
// Bremen Mask march — NA_BGM_BREMEN_MARCH (HEX 0x53 per `mm/include/sequence.h:92`).
//
// Confirmed by runtime diagnostic in user's mm.o2r:
//   `BremenMarch_52` BINARY seqNumber=0x52 → contains LEARNED_NEW_SONG (XML mislabeled)
//   `GetSong_53`     BINARY seqNumber=0x53 → contains BREMEN_MARCH    (XML mislabeled)
// Per 2Ship's exporter (ZAudio.cpp:388-400 + AudioExporter.cpp:354-376), the
// i-th file content = ROM seq at index i, regardless of XML label.
//
// We use `GetSong_53` because its binary holds the Bremen March sequence
// (seqNumber=0x53 = NA_BGM_BREMEN_MARCH).
#define MM_BGM_BREMEN_MARCH "GetSong_53"

// Kamaro's dance — NA_BGM_KAMARO_DANCE (HEX 0x71)
#define MM_BGM_KAMARO_DANCE "KamaroDance_71"

#endif // MM_BGM_NAMES_H
