#ifndef UNAUDIO_RESAMPLING_DECODER_H
#define UNAUDIO_RESAMPLING_DECODER_H

#include "AudioDecoder.h"
#include <memory>
#include <vector>
#include <cmath>

/// Decorator that wraps any AudioDecoder and transparently converts its
/// sample rate to a target rate using cubic Hermite (Catmull-Rom)
/// interpolation.  The mixer and engine see a decoder that produces
/// frames at the output rate.
///
/// Data flow:  InnerDecoder (sourceRate) → ResamplingDecoder → (targetRate)
///
/// Thread safety: same as the inner decoder — Decode()/Seek() are called
/// from the audio thread; GetCurrentFrame() is atomic-safe for reads
/// from any thread.
class ResamplingDecoder : public AudioDecoder {
public:
    ResamplingDecoder();
    ~ResamplingDecoder() override;

    /// Initialize with an already-opened inner decoder and target sample rate.
    /// Takes ownership of inner via unique_ptr.
    bool Init(std::unique_ptr<AudioDecoder> inner, int32_t targetSampleRate);

    // AudioDecoder interface
    bool Open(const uint8_t* data, size_t size) override;
    int  Decode(float* buffer, int frameCount) override;
    bool Seek(int64_t frame) override;
    UNAudioFormat GetFormat() const override;
    bool SupportsStreaming() const override;
    int64_t GetTotalFrames() const override;
    int64_t GetCurrentFrame() const override;

private:
    std::unique_ptr<AudioDecoder> inner_;
    int32_t targetSR_ = 0;
    int32_t sourceSR_ = 0;
    int channels_ = 0;
    double ratio_ = 1.0;           // sourceSR / targetSR

    // Source-rate sample buffer (interleaved: [frame0_ch0, frame0_ch1, ...])
    std::vector<float> srcBuf_;
    int srcBufFrames_ = 0;         // valid frames currently in buffer
    int srcBufCapacity_ = 0;       // max frames the buffer can hold

    double phase_ = 0.0;           // fractional read position in srcBuf_ (frames)
    bool innerDone_ = false;       // inner decoder reached end

    static constexpr int SRC_BUF_FRAMES = 2048;
    static constexpr int INTERP_MARGIN = 2;   // frames beyond read pos for cubic

    int  refillBuffer();            // decode from inner, returns frames added
    void shiftBuffer(int consumed); // shift consumed frames out

    /// Cubic Hermite (Catmull-Rom) interpolation between 4 samples.
    static inline float hermite(float s0, float s1, float s2, float s3, float t) {
        float a = -0.5f * s0 + 1.5f * s1 - 1.5f * s2 + 0.5f * s3;
        float b =         s0 - 2.5f * s1 + 2.0f * s2 - 0.5f * s3;
        float c = -0.5f * s0              + 0.5f * s2;
        float d =                     s1;
        return ((a * t + b) * t + c) * t + d;
    }
};

#endif // UNAUDIO_RESAMPLING_DECODER_H
