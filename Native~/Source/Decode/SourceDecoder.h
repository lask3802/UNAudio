#pragma once
// Compressed / container decoders. Output is planar float at the source
// rate and source channel count. Decoders run on worker threads only.

#include <cstddef>
#include <cstdint>
#include <memory>

namespace una {

constexpr int kMaxSourceChannels = 8;

struct SourceFormat {
    int sampleRate = 0;
    int channels = 0;
    int64_t totalFrames = 0;
};

class SourceDecoder {
public:
    virtual ~SourceDecoder() = default;

    /// Parses headers. `data` must outlive the decoder.
    virtual bool Open(const uint8_t* data, size_t size) = 0;
    virtual SourceFormat Format() const = 0;

    /// Decodes up to `frames` frames into `planar[ch][0..frames)`.
    /// Returns frames written; 0 means end of stream.
    virtual int Read(float* const* planar, int frames) = 0;

    /// Sample-accurate seek. The next Read starts at `frame`.
    virtual bool Seek(int64_t frame) = 0;
};

enum class SourceKind { Unknown, Vorbis, Wav, Flac, Mp3 };

/// Identifies the container from its content (never the file name).
SourceKind DetectSourceKind(const uint8_t* data, size_t size);
std::unique_ptr<SourceDecoder> CreateSourceDecoder(const uint8_t* data, size_t size);

/// Offset of the first byte after any leading ID3v2 tags (FLAC and MP3 may carry them);
/// `size` when a tag runs past the end.
size_t SkipId3v2(const uint8_t* data, size_t size);
/// MPEG audio Layer III: a valid frame header right after the ID3v2 tags, followed by
/// a consistent second header (or the end of the data).
bool IsMp3Stream(const uint8_t* data, size_t size);

} // namespace una
