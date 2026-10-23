#include "Decode/DecodePipeline.h"
#include "Decode/Resampler.h"
#include "TestUtil.h"

#include <gtest/gtest.h>

#include <cmath>

using una::DecodePipeline;
using una::Resampler;

namespace {
constexpr double kPi = 3.14159265358979323846;

std::vector<float> RunResampler(Resampler& rs, const std::vector<float>& in, int64_t outFrames) {
    std::vector<float> out(static_cast<size_t>(outFrames));
    size_t fed = 0;
    int64_t made = 0;
    while (made < outFrames) {
        float* o[1] = {out.data() + made};
        made += rs.Produce(o, static_cast<int>(outFrames - made));
        if (made >= outFrames) break;
        if (fed < in.size()) {
            const int n = static_cast<int>(std::min<size_t>(512, in.size() - fed));
            const float* i[1] = {in.data() + fed};
            rs.Push(i, n);
            fed += static_cast<size_t>(n);
        } else {
            rs.PushSilence(rs.Taps());
        }
    }
    return out;
}

double ToneAmplitude(const std::vector<float>& v, double freq, double rate, size_t from, size_t to) {
    double re = 0, im = 0;
    for (size_t i = from; i < to; ++i) {
        re += v[i] * std::cos(2 * kPi * freq * double(i) / rate);
        im += v[i] * std::sin(2 * kPi * freq * double(i) / rate);
    }
    return 2.0 * std::sqrt(re * re + im * im) / double(to - from);
}
} // namespace

TEST(Resampler, OutputLengthIsExactRatio) {
    Resampler rs;
    ASSERT_TRUE(rs.Init(44100, 48000, 1));
    EXPECT_EQ(rs.OutputLength(88200), 96000);
    EXPECT_EQ(rs.OutputLength(1), 2);
    Resampler down;
    ASSERT_TRUE(down.Init(96000, 48000, 1));
    EXPECT_EQ(down.OutputLength(96000), 48000);
}

TEST(Resampler, DcGainIsUnity) {
    Resampler rs;
    ASSERT_TRUE(rs.Init(44100, 48000, 1));
    std::vector<float> in(20000, 0.5f);
    auto out = RunResampler(rs, in, 20000);
    for (size_t i = 100; i < 20000; ++i) ASSERT_NEAR(out[i], 0.5f, 2e-4f) << i;
}

TEST(Resampler, PreservesToneAndAmplitude) {
    for (double f : {440.0, 5000.0, 15000.0}) {
        Resampler rs;
        ASSERT_TRUE(rs.Init(44100, 48000, 1));
        std::vector<float> in(44100);
        for (size_t i = 0; i < in.size(); ++i) in[i] = 0.5f * float(std::sin(2 * kPi * f * double(i) / 44100.0));
        auto out = RunResampler(rs, in, 48000);
        const double a = ToneAmplitude(out, f, 48000.0, 1000, 47000);
        EXPECT_NEAR(a, 0.5, 0.01) << f;
    }
}

TEST(Resampler, DownsamplingRejectsAliases) {
    Resampler rs;
    ASSERT_TRUE(rs.Init(96000, 48000, 1));
    std::vector<float> in(96000);
    // 30 kHz is above the 24 kHz output Nyquist: it must not fold to 18 kHz.
    for (size_t i = 0; i < in.size(); ++i) in[i] = 0.5f * float(std::sin(2 * kPi * 30000.0 * double(i) / 96000.0));
    auto out = RunResampler(rs, in, 48000);
    EXPECT_LT(ToneAmplitude(out, 18000.0, 48000.0, 1000, 47000), 0.005);
}

TEST(Resampler, SeekReproducesContinuousOutput) {
    auto data = test::ReadFixture("tone_44k_mono_2s.ogg");
    DecodePipeline full;
    ASSERT_EQ(full.Open(data.data(), data.size(), 48000), UNA_OK);
    ASSERT_EQ(full.StoredRate(), 48000);
    ASSERT_EQ(full.TotalFrames(), 96000);
    std::vector<float> ref;
    std::vector<float> buf(777);
    float* pl[1] = {buf.data()};
    for (;;) {
        const int n = full.Read(pl, 777);
        if (n <= 0) break;
        ref.insert(ref.end(), buf.begin(), buf.begin() + n);
    }
    ASSERT_EQ(ref.size(), 96000u);

    for (int64_t target : {0LL, 5LL, 160LL, 12345LL, 95000LL}) {
        DecodePipeline p;
        ASSERT_EQ(p.Open(data.data(), data.size(), 48000), UNA_OK);
        ASSERT_TRUE(p.Seek(target));
        const int n = p.Read(pl, 700);
        ASSERT_GT(n, 0);
        for (int i = 0; i < n; ++i) ASSERT_NEAR(buf[size_t(i)], ref[size_t(target + i)], 1e-5) << target << ":" << i;
    }
}
