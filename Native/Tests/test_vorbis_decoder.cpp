#include "test_framework.h"
#include "../Source/Decoder/VorbisDecoder.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {
std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>());
}

std::filesystem::path fixture_path() {
    std::filesystem::path p = std::filesystem::path(__FILE__).parent_path() /
                              "fixtures" /
                              "sine_48k_stereo_100ms.ogg";
    return p;
}
} // namespace

TEST(UNAudio, VorbisDecoder_OpenDecode48kStereo) {
    auto bytes = read_file(fixture_path());
    ASSERT_FALSE(bytes.empty());

    VorbisDecoder dec;
    ASSERT_TRUE(dec.Open(bytes.data(), bytes.size()));

    UNAudioFormat fmt = dec.GetFormat();
    ASSERT_EQ(fmt.sampleRate, 48000);
    ASSERT_EQ(fmt.channels, 2);
    ASSERT_EQ(fmt.bitsPerSample, 16);

    // ~0.1s clip at 48kHz -> ~4800 frames (Vorbis can include small edge padding).
    ASSERT_GT(dec.GetTotalFrames(), 4700);
    ASSERT_LT(dec.GetTotalFrames(), 4900);

    std::vector<float> buf(512 * fmt.channels, 0.0f);
    int decoded = dec.Decode(buf.data(), 512);
    ASSERT_EQ(decoded, 512);

    float peak = 0.0f;
    for (float s : buf) peak = std::max(peak, std::fabs(s));
    ASSERT_GT(peak, 0.001f);
}

TEST(UNAudio, VorbisDecoder_SeekToStartReplaysSameData) {
    auto bytes = read_file(fixture_path());
    ASSERT_FALSE(bytes.empty());

    VorbisDecoder dec;
    ASSERT_TRUE(dec.Open(bytes.data(), bytes.size()));

    UNAudioFormat fmt = dec.GetFormat();
    std::vector<float> a(256 * fmt.channels, 0.0f);
    std::vector<float> b(256 * fmt.channels, 0.0f);

    ASSERT_EQ(dec.Decode(a.data(), 256), 256);
    ASSERT_TRUE(dec.Seek(0));
    ASSERT_EQ(dec.Decode(b.data(), 256), 256);

    for (size_t i = 0; i < a.size(); ++i) {
        ASSERT_NEAR(a[i], b[i], 1e-6f);
    }
}

TEST(UNAudio, VorbisDecoder_RejectsInvalidData) {
    uint8_t bad[32] = {};
    VorbisDecoder dec;
    ASSERT_FALSE(dec.Open(bad, sizeof(bad)));
}
