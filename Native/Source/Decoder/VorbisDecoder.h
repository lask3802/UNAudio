#ifndef UNAUDIO_VORBIS_DECODER_H
#define UNAUDIO_VORBIS_DECODER_H

#include "AudioDecoder.h"
#include <atomic>
#include <vector>

/// Ogg Vorbis decoder backed by stb_vorbis.
/// Decodes into PCM float on open for deterministic, lock-free playback.
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
    int64_t GetCurrentFrame() const override;

private:
    UNAudioFormat format_{};
    std::vector<float> pcmData_;
    int64_t totalFrames_ = 0;
    std::atomic<int64_t> currentFrame_{0};
};

#endif // UNAUDIO_VORBIS_DECODER_H
