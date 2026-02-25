#ifndef UNAUDIO_MP3_DECODER_H
#define UNAUDIO_MP3_DECODER_H

#include "AudioDecoder.h"

/// MP3 decoder – wraps libmpg123 (to be integrated in a later phase).
class MP3Decoder : public AudioDecoder {
public:
    MP3Decoder();
    ~MP3Decoder() override;

    bool Open(const uint8_t* data, size_t size) override;
    int  Decode(float* buffer, int frameCount) override;
    bool Seek(int64_t frame) override;
    UNAudioFormat GetFormat() const override;
    bool SupportsStreaming() const override;
    int64_t GetTotalFrames() const override;
    int64_t GetCurrentFrame() const override;

private:
    UNAudioFormat format_{};
    const uint8_t* data_ = nullptr;
    size_t dataSize_ = 0;
    int64_t totalFrames_ = 0;
    int64_t currentFrame_ = 0;
    // TODO: mpg123_handle* handle_;
};

#endif // UNAUDIO_MP3_DECODER_H
