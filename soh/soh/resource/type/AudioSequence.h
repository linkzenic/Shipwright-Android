#pragma once

#include <stdint.h>
#include <ship/resource/Resource.h>

namespace SOH {

typedef struct {
    char* seqData;
    uint32_t seqDataSize;
    uint16_t seqNumber;
    uint8_t medium;
    uint8_t cachePolicy;
    uint32_t numFonts;
    uint8_t fonts[16];
    // Full-width resolved soundfont index for streamed custom songs. fonts[] is
    // u8 (the N64 256-soundfont cap), but a streamed music pack assigns one
    // unique font per song, so past 256 songs the index truncates and the song
    // plays the wrong/corrupt font. This carries the un-truncated index instead;
    // -1 means "not a resolved-streamed seq, use fonts[]". MUST stay layout-
    // identical to SequenceData in z64audio.h (reinterpret_cast in ResourceMgr_LoadSeqByName).
    int32_t resolvedFont;
} Sequence;

class AudioSequence : public Ship::Resource<Sequence> {
  public:
    using Resource::Resource;

    AudioSequence() : Resource(std::shared_ptr<Ship::ResourceInitData>()) {
    }
    ~AudioSequence();

    Sequence* GetPointer();
    size_t GetPointerSize();

    Sequence sequence;
};
}; // namespace SOH