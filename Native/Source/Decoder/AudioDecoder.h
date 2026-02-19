#ifndef UNAUDIO_AUDIO_DECODER_H
#define UNAUDIO_AUDIO_DECODER_H

#include "../Core/AudioTypes.h"
#include <cstdint>
#include <cstddef>

/// Abstract base class for all audio decoders.
class AudioDecoder {
public:
    virtual ~AudioDecoder() = default;

    /// Open compressed data for decoding.
    virtual bool Open(const uint8_t* data, size_t size) = 0;

    /// Decode up to frameCount frames into the float buffer.
    /// Returns the number of frames actually decoded.
    virtual int Decode(float* buffer, int frameCount) = 0;

    /// Seek to a specific frame position.
    virtual bool Seek(int64_t frame) = 0;

    /// Get the audio format of the decoded stream.
    virtual UNAudioFormat GetFormat() const = 0;

    /// Whether this decoder supports streaming (partial reads).
    virtual bool SupportsStreaming() const = 0;

    /// Total number of frames in the audio clip (0 if unknown).
    virtual int64_t GetTotalFrames() const = 0;
};

#endif // UNAUDIO_AUDIO_DECODER_H
