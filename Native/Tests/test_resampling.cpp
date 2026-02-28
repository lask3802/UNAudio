#include "test_framework.h"
#include "../Source/Decoder/ResamplingDecoder.h"
#include <memory>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── MockDecoder ──────────────────────────────────────────────────
// Generates known signals at a specified sample rate for testing.

class MockDecoder : public AudioDecoder {
public:
    enum class Signal { DC, Sine, Ramp };

    MockDecoder(int32_t sampleRate, int channels, int64_t totalFrames,
                Signal signal = Signal::DC, float amplitude = 1.0f)
        : sampleRate_(sampleRate), channels_(channels),
          totalFrames_(totalFrames), signal_(signal), amplitude_(amplitude) {}

    bool Open(const uint8_t*, size_t) override { return true; }

    int Decode(float* buffer, int frameCount) override {
        int available = static_cast<int>(totalFrames_ - currentFrame_);
        int toDecode = std::min(frameCount, available);

        for (int i = 0; i < toDecode; ++i) {
            float value = 0.0f;
            int64_t frame = currentFrame_ + i;

            switch (signal_) {
                case Signal::DC:
                    value = amplitude_;
                    break;
                case Signal::Sine:
                    // 440 Hz sine at the source sample rate
                    value = amplitude_ * std::sin(
                        2.0 * M_PI * 440.0 * frame / sampleRate_);
                    break;
                case Signal::Ramp:
                    value = amplitude_ * static_cast<float>(frame) / totalFrames_;
                    break;
            }

            for (int ch = 0; ch < channels_; ++ch) {
                buffer[i * channels_ + ch] = value;
            }
        }

        currentFrame_ += toDecode;
        return toDecode;
    }

    bool Seek(int64_t frame) override {
        if (frame < 0) frame = 0;
        if (frame > totalFrames_) frame = totalFrames_;
        currentFrame_ = frame;
        return true;
    }

    UNAudioFormat GetFormat() const override {
        UNAudioFormat fmt{};
        fmt.sampleRate = sampleRate_;
        fmt.channels = channels_;
        fmt.bitsPerSample = 32;
        fmt.blockAlign = channels_ * 4;
        return fmt;
    }

    bool SupportsStreaming() const override { return false; }
    int64_t GetTotalFrames() const override { return totalFrames_; }
    int64_t GetCurrentFrame() const override { return currentFrame_; }

private:
    int32_t sampleRate_;
    int channels_;
    int64_t totalFrames_;
    Signal signal_;
    float amplitude_;
    int64_t currentFrame_ = 0;
};

// ── Helper ────────────────────────────────────────────────────────

static std::unique_ptr<ResamplingDecoder> makeResampler(
    int32_t sourceSR, int32_t targetSR, int channels, int64_t totalFrames,
    MockDecoder::Signal signal = MockDecoder::Signal::DC, float amplitude = 1.0f)
{
    auto inner = std::make_unique<MockDecoder>(
        sourceSR, channels, totalFrames, signal, amplitude);
    auto resampler = std::make_unique<ResamplingDecoder>();
    if (!resampler->Init(std::move(inner), targetSR)) {
        return nullptr;
    }
    return resampler;
}

// ── Tests ─────────────────────────────────────────────────────────

TEST(UNAudio, Resampling_Passthrough_1to1) {
    // When source and target rate match, output should equal input exactly.
    const int frames = 512;
    const int channels = 2;
    auto resampler = makeResampler(48000, 48000, channels, frames,
                                   MockDecoder::Signal::Ramp, 1.0f);
    ASSERT_NE(resampler, nullptr);

    std::vector<float> output(frames * channels);
    int decoded = resampler->Decode(output.data(), frames);
    ASSERT_EQ(decoded, frames);

    // Compare against expected ramp values
    for (int i = 0; i < frames; ++i) {
        float expected = static_cast<float>(i) / frames;
        for (int ch = 0; ch < channels; ++ch) {
            ASSERT_NEAR(output[i * channels + ch], expected, 0.001f)
                << "Frame " << i << " ch " << ch;
        }
    }
}

TEST(UNAudio, Resampling_Upsample_44100_to_48000) {
    // 44100 → 48000: output should have more frames than source.
    const int64_t srcFrames = 4410;  // 100ms at 44100 Hz
    const int channels = 2;
    auto resampler = makeResampler(44100, 48000, channels, srcFrames,
                                   MockDecoder::Signal::Sine, 0.5f);
    ASSERT_NE(resampler, nullptr);

    // Expected output frames ≈ srcFrames * (48000/44100) ≈ 4800
    int expectedOut = static_cast<int>(
        std::ceil(static_cast<double>(srcFrames) * 48000.0 / 44100.0));

    std::vector<float> output(expectedOut * channels + channels * 64);
    int decoded = resampler->Decode(output.data(), expectedOut + 32);

    // Should produce roughly expectedOut frames (within a few frames tolerance)
    EXPECT_GE(decoded, expectedOut - 4);
    EXPECT_LE(decoded, expectedOut + 4);

    // Output should not contain NaN or Inf
    for (int i = 0; i < decoded * channels; ++i) {
        ASSERT_FALSE(std::isnan(output[i])) << "NaN at sample " << i;
        ASSERT_FALSE(std::isinf(output[i])) << "Inf at sample " << i;
    }

    // Amplitude should be bounded by the source amplitude
    for (int i = 0; i < decoded * channels; ++i) {
        EXPECT_LE(std::abs(output[i]), 0.6f)
            << "Amplitude exceeded at sample " << i;
    }
}

TEST(UNAudio, Resampling_Downsample_48000_to_44100) {
    // 48000 → 44100: output should have fewer frames than source.
    const int64_t srcFrames = 4800;  // 100ms at 48000 Hz
    const int channels = 2;
    auto resampler = makeResampler(48000, 44100, channels, srcFrames,
                                   MockDecoder::Signal::Sine, 0.5f);
    ASSERT_NE(resampler, nullptr);

    int expectedOut = static_cast<int>(
        std::ceil(static_cast<double>(srcFrames) * 44100.0 / 48000.0));

    std::vector<float> output(expectedOut * channels + channels * 64);
    int decoded = resampler->Decode(output.data(), expectedOut + 32);

    EXPECT_GE(decoded, expectedOut - 4);
    EXPECT_LE(decoded, expectedOut + 4);
}

TEST(UNAudio, Resampling_Upsample_22050_to_48000) {
    // Large ratio (>2x): 22050 → 48000
    const int64_t srcFrames = 2205;  // 100ms at 22050 Hz
    const int channels = 1;
    auto resampler = makeResampler(22050, 48000, channels, srcFrames,
                                   MockDecoder::Signal::DC, 0.75f);
    ASSERT_NE(resampler, nullptr);

    int expectedOut = static_cast<int>(
        std::ceil(static_cast<double>(srcFrames) * 48000.0 / 22050.0));

    std::vector<float> output(expectedOut * channels + channels * 64);
    int decoded = resampler->Decode(output.data(), expectedOut + 32);

    EXPECT_GE(decoded, expectedOut - 4);

    // DC signal should come through as constant (within interpolation tolerance)
    for (int i = 1; i < decoded; ++i) {
        EXPECT_NEAR(output[i], 0.75f, 0.02f)
            << "DC deviation at frame " << i;
    }
}

TEST(UNAudio, Resampling_Seek_Resets_Phase) {
    const int64_t srcFrames = 8820;  // 200ms at 44100
    const int channels = 1;

    // Create two resamplers from the same signal
    auto r1 = makeResampler(44100, 48000, channels, srcFrames,
                            MockDecoder::Signal::Sine, 1.0f);
    auto r2 = makeResampler(44100, 48000, channels, srcFrames,
                            MockDecoder::Signal::Sine, 1.0f);
    ASSERT_NE(r1, nullptr);
    ASSERT_NE(r2, nullptr);

    // r1: decode 1000 frames, then seek to frame 2000 (source space)
    std::vector<float> buf1(2000);
    r1->Decode(buf1.data(), 1000);
    r1->Seek(2000);

    // r2: seek directly to frame 2000
    r2->Seek(2000);

    // Both should now produce the same output
    std::vector<float> out1(1000), out2(1000);
    int d1 = r1->Decode(out1.data(), 1000);
    int d2 = r2->Decode(out2.data(), 1000);

    ASSERT_EQ(d1, d2);
    for (int i = 0; i < d1; ++i) {
        EXPECT_NEAR(out1[i], out2[i], 0.001f)
            << "Mismatch after seek at frame " << i;
    }
}

TEST(UNAudio, Resampling_GetFormat_Reports_TargetRate) {
    auto resampler = makeResampler(44100, 48000, 2, 1000);
    ASSERT_NE(resampler, nullptr);

    UNAudioFormat fmt = resampler->GetFormat();
    EXPECT_EQ(fmt.sampleRate, 48000);
    EXPECT_EQ(fmt.channels, 2);
}

TEST(UNAudio, Resampling_GetTotalFrames_SourceSpace) {
    const int64_t srcFrames = 44100;
    auto resampler = makeResampler(44100, 48000, 1, srcFrames);
    ASSERT_NE(resampler, nullptr);

    // TotalFrames should report source-space count, not target-space
    EXPECT_EQ(resampler->GetTotalFrames(), srcFrames);
}

TEST(UNAudio, Resampling_GetCurrentFrame_SourceSpace) {
    const int64_t srcFrames = 44100;
    auto resampler = makeResampler(44100, 48000, 1, srcFrames,
                                   MockDecoder::Signal::DC, 1.0f);
    ASSERT_NE(resampler, nullptr);

    // Before any decode, current frame should be 0 (or close, after prefill)
    // After decoding some output frames, inner should have advanced
    std::vector<float> buf(4800);
    int decoded = resampler->Decode(buf.data(), 4800);
    ASSERT_GT(decoded, 0);

    int64_t curFrame = resampler->GetCurrentFrame();
    // Inner decoder should have advanced roughly 4800 * (44100/48000) ≈ 4410 frames
    // But it reads ahead in chunks, so just verify it's in a reasonable range
    EXPECT_GT(curFrame, 0);
    EXPECT_LE(curFrame, srcFrames);
}

TEST(UNAudio, Resampling_Hermite_DC_Signal) {
    // Constant input should produce constant output with no artifacts.
    const int64_t srcFrames = 1024;
    const int channels = 2;
    auto resampler = makeResampler(44100, 48000, channels, srcFrames,
                                   MockDecoder::Signal::DC, 0.5f);
    ASSERT_NE(resampler, nullptr);

    std::vector<float> output(2048 * channels);
    int decoded = resampler->Decode(output.data(), 2048);
    ASSERT_GT(decoded, 0);

    for (int i = 0; i < decoded * channels; ++i) {
        EXPECT_NEAR(output[i], 0.5f, 0.001f)
            << "DC artifact at sample " << i;
    }
}

TEST(UNAudio, Resampling_Empty_Inner) {
    // Inner decoder with 0 total frames should produce 0 output.
    auto resampler = makeResampler(44100, 48000, 1, 0);
    ASSERT_NE(resampler, nullptr);

    float buf[64];
    int decoded = resampler->Decode(buf, 32);
    EXPECT_EQ(decoded, 0);
}

TEST(UNAudio, Resampling_SmallChunks) {
    // Decode in very small chunks to stress the buffer shift logic.
    const int64_t srcFrames = 4410;
    const int channels = 1;
    auto resampler = makeResampler(44100, 48000, channels, srcFrames,
                                   MockDecoder::Signal::DC, 0.3f);
    ASSERT_NE(resampler, nullptr);

    int totalDecoded = 0;
    float buf[16];
    for (int i = 0; i < 500; ++i) {
        int d = resampler->Decode(buf, 8);
        if (d == 0) break;
        totalDecoded += d;

        // All samples should be near 0.3 (DC)
        for (int j = 0; j < d; ++j) {
            EXPECT_NEAR(buf[j], 0.3f, 0.01f);
        }
    }

    EXPECT_GT(totalDecoded, 0);
}

TEST(UNAudio, Resampling_Downsample_NoDropoutWindows) {
    // Long 48k -> 44.1k downsample path should not emit mid-stream silent chunks.
    const int64_t srcFrames = 48000 * 5; // 5 seconds
    const int channels = 2;
    auto resampler = makeResampler(48000, 44100, channels, srcFrames,
                                   MockDecoder::Signal::Sine, 0.8f);
    ASSERT_NE(resampler, nullptr);

    std::vector<float> buf(512 * channels, 0.0f);
    int chunkIndex = 0;
    while (true) {
        int d = resampler->Decode(buf.data(), 512);
        if (d <= 0) break;

        // Compute per-chunk RMS and ensure no silent dropout appears mid-stream.
        double sumSq = 0.0;
        for (int i = 0; i < d * channels; ++i) {
            const double v = static_cast<double>(buf[i]);
            sumSq += v * v;
        }
        const double rms = std::sqrt(sumSq / static_cast<double>(d * channels));

        // Skip first chunk (startup transient) and final tail chunk (may be short).
        if (chunkIndex > 0 && d == 512) {
            EXPECT_GT(rms, 0.05) << "Detected potential dropout at chunk " << chunkIndex;
        }

        ++chunkIndex;
    }

    EXPECT_GT(chunkIndex, 10);
}
