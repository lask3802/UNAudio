#include "Decode/DecodePipeline.h"
#include "TestUtil.h"

#include <gtest/gtest.h>

#include <cmath>

using una::DecodePipeline;

namespace {

std::vector<std::vector<float>> DecodeAll(DecodePipeline& p) {
    std::vector<std::vector<float>> out(static_cast<size_t>(p.StoredChannels()));
    std::vector<float> a(1024), b(1024);
    float* pl[2] = {a.data(), b.data()};
    for (;;) {
        const int n = p.Read(pl, 1024);
        if (n <= 0) break;
        for (int c = 0; c < p.StoredChannels(); ++c) out[static_cast<size_t>(c)].insert(out[static_cast<size_t>(c)].end(), pl[c], pl[c] + n);
    }
    return out;
}

double Rms(const std::vector<float>& v, size_t from = 0) {
    double s = 0;
    for (size_t i = from; i < v.size(); ++i) s += double(v[i]) * v[i];
    return std::sqrt(s / double(v.size() - from));
}

int ZeroCrossings(const std::vector<float>& v, size_t from, size_t to) {
    int n = 0;
    for (size_t i = from + 1; i < to; ++i)
        if ((v[i - 1] < 0) != (v[i] < 0)) ++n;
    return n;
}

} // namespace

TEST(Decode, ProbeFixturesMatchFfprobe) {
    struct Case { const char* file; int rate, ch; int64_t frames; } cases[] = {
        {"sine_48k_stereo_100ms.ogg", 48000, 2, 4800},
        {"tone_44k_mono_2s.ogg", 44100, 1, 88200},
        {"tone_48k_stereo_3s.ogg", 48000, 2, 144000},
        {"tone_48k_51_1s.ogg", 48000, 6, 48000},
    };
    for (const auto& c : cases) {
        auto data = test::ReadFixture(c.file);
        ASSERT_FALSE(data.empty()) << c.file;
        una_clip_info info{};
        ASSERT_EQ(una_probe(data.data(), (int64_t)data.size(), &info), UNA_OK) << c.file;
        EXPECT_EQ(info.source_rate, c.rate) << c.file;
        EXPECT_EQ(info.source_channels, c.ch) << c.file;
        EXPECT_EQ(info.channels, c.ch >= 2 ? 2 : 1) << c.file;
        EXPECT_EQ(info.total_frames, c.frames) << c.file;
    }
}

TEST(Decode, VorbisContentIsTheTone) {
    auto data = test::ReadFixture("tone_48k_stereo_3s.ogg");
    DecodePipeline p;
    ASSERT_EQ(p.Open(data.data(), data.size(), 0), UNA_OK);
    auto pcm = DecodeAll(p);
    ASSERT_EQ(pcm.size(), 2u);
    ASSERT_EQ(pcm[0].size(), 144000u);
    // 1 s window: 440 Hz → 880 crossings, 660 Hz → 1320.
    EXPECT_NEAR(ZeroCrossings(pcm[0], 48000, 96000), 880, 4);
    EXPECT_NEAR(ZeroCrossings(pcm[1], 48000, 96000), 1320, 4);
    EXPECT_GT(Rms(pcm[0]), 0.05);
}

TEST(Decode, VorbisSeekIsSampleExact) {
    auto data = test::ReadFixture("tone_48k_stereo_3s.ogg");
    DecodePipeline full;
    ASSERT_EQ(full.Open(data.data(), data.size(), 0), UNA_OK);
    auto ref = DecodeAll(full);
    for (int64_t target : {0LL, 1LL, 1023LL, 4097LL, 77777LL, 143000LL}) {
        DecodePipeline p;
        ASSERT_EQ(p.Open(data.data(), data.size(), 0), UNA_OK);
        ASSERT_TRUE(p.Seek(target));
        std::vector<float> a(500), b(500);
        float* pl[2] = {a.data(), b.data()};
        const int n = p.Read(pl, 500);
        ASSERT_GT(n, 0);
        for (int i = 0; i < n; ++i) {
            ASSERT_NEAR(a[i], ref[0][size_t(target + i)], 1e-6) << "target " << target << " i " << i;
            ASSERT_NEAR(b[i], ref[1][size_t(target + i)], 1e-6);
        }
    }
}

TEST(Decode, SurroundDownmixesToStereo) {
    auto data = test::ReadFixture("tone_48k_51_1s.ogg");
    DecodePipeline p;
    ASSERT_EQ(p.Open(data.data(), data.size(), 0), UNA_OK);
    EXPECT_EQ(p.SourceChannels(), 6);
    EXPECT_EQ(p.StoredChannels(), 2);
    auto pcm = DecodeAll(p);
    // The fixture carries a -21 dBFS tone on the centre channel only (RMS 0.0888 per libvorbis).
    // Centre feeds both sides at 0.7071, normalised by 1 + 0.7071 + 0.7071 per side.
    // Guards the patched stb_vorbis residue-type-2 path, which used to truncate >2 channels.
    const double expect = 0.0888 * 0.70710678 / (1.0 + 2 * 0.70710678);
    EXPECT_NEAR(Rms(pcm[0]), expect, 0.001);
    EXPECT_NEAR(Rms(pcm[1]), expect, 0.001);
    for (float s : pcm[0]) ASSERT_LE(std::fabs(s), 1.01f);
}

TEST(Decode, WavIntegerAndFloatFormatsAreExact) {
    auto gen = [](int i, int c) { return float(((i * 7 + c * 13) % 200) - 100) / 128.0f; };
    for (int bits : {8, 16, 24, 32}) {
        auto wav = test::MakeWavPcm(22050, 2, 1000, bits, gen);
        DecodePipeline p;
        ASSERT_EQ(p.Open(wav.data(), wav.size(), 0), UNA_OK) << bits;
        auto pcm = DecodeAll(p);
        ASSERT_EQ(pcm[0].size(), 1000u);
        const float tol = bits == 8 ? 1.0f / 64 : 1.0f / 16384;
        for (int i = 0; i < 1000; ++i) ASSERT_NEAR(pcm[1][size_t(i)], gen(i, 1), tol) << bits;
    }
    auto wf = test::MakeWavFloat(48000, 1, 300, [](int i, int) { return i / 300.0f; });
    DecodePipeline p;
    ASSERT_EQ(p.Open(wf.data(), wf.size(), 0), UNA_OK);
    auto pcm = DecodeAll(p);
    for (int i = 0; i < 300; ++i) ASSERT_FLOAT_EQ(pcm[0][size_t(i)], i / 300.0f);
}

TEST(Decode, RejectsGarbage) {
    std::vector<uint8_t> junk(4096, 0x5A);
    una_clip_info info{};
    EXPECT_EQ(una_probe(junk.data(), (int64_t)junk.size(), &info), UNA_E_FORMAT);
    auto ogg = test::ReadFixture("tone_48k_stereo_3s.ogg");
    ogg.resize(200); // truncated inside the setup header
    EXPECT_NE(una_probe(ogg.data(), (int64_t)ogg.size(), &info), UNA_OK);
}
