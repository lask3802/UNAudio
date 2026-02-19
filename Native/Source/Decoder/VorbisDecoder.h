#ifndef UNAUDIO_VORBIS_DECODER_H
#define UNAUDIO_VORBIS_DECODER_H

#include "AudioDecoder.h"

/// Ogg Vorbis decoder – wraps libvorbis (to be integrated in a later phase).
class VorbisDecoder : public AudioDecoder {
public:
    VorbisDecoder();
    ~VorbisDecoder() override;

    bool Open(const uint8_t* data, size_t size) override;
    int  Decode(float* buffer, int frameCount) override;
    bool Seek(int64_t frame) override;
    UNAudioFormat GetFormat() const override;
    bool SupportsStreaming() const override;
    int64_t GetTotalFrames() const override;

private:
    UNAudioFormat format_{};
    const uint8_t* data_ = nullptr;
    size_t dataSize_ = 0;
    int64_t totalFrames_ = 0;
    int64_t currentFrame_ = 0;
};

#endif // UNAUDIO_VORBIS_DECODER_H
