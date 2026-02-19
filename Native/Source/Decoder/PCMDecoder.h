#ifndef UNAUDIO_PCM_DECODER_H
#define UNAUDIO_PCM_DECODER_H

#include "AudioDecoder.h"

/// Raw PCM decoder — handles uncompressed 16-bit and float PCM data.
/// This is the simplest decoder, used for DecompressOnLoad clips and testing.
class PCMDecoder : public AudioDecoder {
public:
    PCMDecoder();
    ~PCMDecoder() override;

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
    bool isFloat_ = false;

    // Simple WAV header parser
    bool parseWavHeader(const uint8_t* data, size_t size);
    const uint8_t* pcmData_ = nullptr;
    size_t pcmDataSize_ = 0;
};

#endif // UNAUDIO_PCM_DECODER_H
