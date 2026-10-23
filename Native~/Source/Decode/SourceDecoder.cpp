// Container detection and decoder factory.

#include "SourceDecoder.h"

#include <cstring>

namespace una {

std::unique_ptr<SourceDecoder> CreateVorbisSource();
std::unique_ptr<SourceDecoder> CreateWavSource();
std::unique_ptr<SourceDecoder> CreateFlacSource();
std::unique_ptr<SourceDecoder> CreateMp3Source();

size_t SkipId3v2(const uint8_t* data, size_t size) {
    size_t p = 0;
    // Tags may be chained; each header is "ID3", version, flags, then a 28-bit syncsafe size.
    while (size - p >= 10 && std::memcmp(data + p, "ID3", 3) == 0 && data[p + 3] != 0xFF &&
           data[p + 4] != 0xFF && ((data[p + 6] | data[p + 7] | data[p + 8] | data[p + 9]) & 0x80) == 0) {
        const size_t body = (static_cast<size_t>(data[p + 6]) << 21) | (static_cast<size_t>(data[p + 7]) << 14) |
                            (static_cast<size_t>(data[p + 8]) << 7) | data[p + 9];
        const size_t len = 10 + body + ((data[p + 5] & 0x10) ? 10 : 0);  // footer flag
        if (len > size - p) return size;
        p += len;
    }
    return p;
}

SourceKind DetectSourceKind(const uint8_t* data, size_t size) {
    if (!data || size < 12) return SourceKind::Unknown;
    if (std::memcmp(data, "OggS", 4) == 0) return SourceKind::Vorbis;
    if (std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WAVE", 4) == 0) return SourceKind::Wav;
    const size_t tagEnd = SkipId3v2(data, size);
    if (size - tagEnd >= 4 && std::memcmp(data + tagEnd, "fLaC", 4) == 0) return SourceKind::Flac;
    if (IsMp3Stream(data, size)) return SourceKind::Mp3;
    return SourceKind::Unknown;
}

std::unique_ptr<SourceDecoder> CreateSourceDecoder(const uint8_t* data, size_t size) {
    std::unique_ptr<SourceDecoder> dec;
    switch (DetectSourceKind(data, size)) {
        case SourceKind::Vorbis: dec = CreateVorbisSource(); break;
        case SourceKind::Wav:    dec = CreateWavSource(); break;
        case SourceKind::Flac:   dec = CreateFlacSource(); break;
        case SourceKind::Mp3:    dec = CreateMp3Source(); break;
        default: return nullptr;
    }
    if (!dec->Open(data, size)) return nullptr;
    return dec;
}

} // namespace una
