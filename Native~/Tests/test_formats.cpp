// MP3 and FLAC sources. Fixtures come from ffmpeg 7.1 (libmp3lame 3.100, native FLAC encoder);
// expected lengths and the *.ref.f32 windows (first and last 512 frames, interleaved) are
// ffmpeg's own decode of the same files.

#include "Decode/DecodePipeline.h"
#include "Decode/SourceDecoder.h"
#include "TestUtil.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using una::DecodePipeline;
using una::SourceKind;
using namespace test;

namespace {

struct Fixture {
    const char* file;
    const char* ref;  // ffmpeg decode windows, or null
    SourceKind kind;
    int rate, ch;
    int64_t frames;   // samples per channel ffmpeg decodes (after gapless trimming)
    float tol;        // vs ffmpeg: MP3 decoders differ in float rounding, FLAC is lossless
};

const Fixture kFixtures[] = {
    {"mp3_44k_stereo_cbr.mp3", "mp3_44k_stereo_cbr.ref.f32", SourceKind::Mp3, 44100, 2, 88200, 1e-5f},
    {"mp3_44k_stereo_vbr.mp3", "mp3_44k_stereo_vbr.ref.f32", SourceKind::Mp3, 44100, 2, 88200, 1e-5f},
    {"mp3_22k_mono.mp3", "mp3_22k_mono.ref.f32", SourceKind::Mp3, 22050, 1, 44100, 1e-5f},
    {"mp3_48k_id3.mp3", "mp3_48k_id3.ref.f32", SourceKind::Mp3, 48000, 2, 48000, 1e-5f},
    {"mp3_44k_noxing.mp3", "mp3_44k_noxing.ref.f32", SourceKind::Mp3, 44100, 2, 46080, 1e-5f},
    {"flac_48k_stereo_16.flac", "flac_48k_stereo_16.ref.f32", SourceKind::Flac, 48000, 2, 48000, 0.0f},
    {"flac_44k_mono_24.flac", "flac_44k_mono_24.ref.f32", SourceKind::Flac, 44100, 1, 44100, 0.0f},
    {"flac_32k_mono_32.flac", "flac_32k_mono_32.ref.f32", SourceKind::Flac, 32000, 1, 16000, 0.0f},
    {"flac_48k_51.flac", nullptr, SourceKind::Flac, 48000, 6, 48000, 0.0f},
};

// Every frame the decoder produces, planar, at the source rate and channel count.
std::vector<std::vector<float>> DecodeSource(una::SourceDecoder& d, int64_t limit = INT64_MAX) {
    const int ch = d.Format().channels;
    std::vector<std::vector<float>> out(static_cast<size_t>(ch));
    std::vector<std::vector<float>> buf(static_cast<size_t>(ch), std::vector<float>(777));
    float* pl[una::kMaxSourceChannels];
    for (int c = 0; c < ch; ++c) pl[c] = buf[size_t(c)].data();
    int64_t got = 0;
    while (got < limit) {
        const int n = d.Read(pl, static_cast<int>(std::min<int64_t>(777, limit - got)));
        if (n <= 0) break;
        for (int c = 0; c < ch; ++c) out[size_t(c)].insert(out[size_t(c)].end(), pl[c], pl[c] + n);
        got += n;
    }
    return out;
}

std::vector<std::vector<float>> DecodePipe(DecodePipeline& p) {
    std::vector<std::vector<float>> out(static_cast<size_t>(p.StoredChannels()));
    std::vector<float> a(1000), b(1000);
    float* pl[2] = {a.data(), b.data()};
    for (;;) {
        const int n = p.Read(pl, 1000);
        if (n <= 0) break;
        for (int c = 0; c < p.StoredChannels(); ++c) out[size_t(c)].insert(out[size_t(c)].end(), pl[c], pl[c] + n);
    }
    return out;
}

std::unique_ptr<una::SourceDecoder> Open(const std::vector<uint8_t>& data) {
    return una::CreateSourceDecoder(data.data(), data.size());
}

int ZeroCrossings(const std::vector<float>& v, size_t from, size_t to) {
    int n = 0;
    for (size_t i = from + 1; i < to; ++i)
        if ((v[i - 1] < 0) != (v[i] < 0)) ++n;
    return n;
}

double Rms(const std::vector<float>& v) {
    double s = 0;
    for (float x : v) s += double(x) * x;
    return std::sqrt(s / double(v.size()));
}

std::vector<float> ReadRef(const char* name) {
    auto bytes = ReadFixture(name);
    std::vector<float> f(bytes.size() / 4);
    std::memcpy(f.data(), bytes.data(), f.size() * 4);
    return f;
}

// APEv2 tag (header + one binary item + footer), as taggers append it. The item holds
// `payload`, e.g. bytes that look like MP3 frames (cover art can), which must not be decoded.
std::vector<uint8_t> ApeTag(const std::vector<uint8_t>& payload) {
    auto put32 = [](std::vector<uint8_t>& v, uint32_t x) {
        for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
    };
    std::vector<uint8_t> item;
    put32(item, static_cast<uint32_t>(payload.size()));
    put32(item, 2);  // binary
    for (char c : std::string("Cover Art (Front)")) item.push_back(uint8_t(c));
    item.push_back(0);
    item.insert(item.end(), payload.begin(), payload.end());
    const uint32_t size = static_cast<uint32_t>(item.size() + 32);
    auto block = [&](uint32_t flags) {
        std::vector<uint8_t> b = {'A', 'P', 'E', 'T', 'A', 'G', 'E', 'X'};
        put32(b, 2000);
        put32(b, size);
        put32(b, 1);
        put32(b, flags);
        b.resize(b.size() + 8, 0);
        return b;
    };
    std::vector<uint8_t> tag = block(0xA0000000u);  // header: has header, is header
    tag.insert(tag.end(), item.begin(), item.end());
    auto footer = block(0x80000000u);
    tag.insert(tag.end(), footer.begin(), footer.end());
    return tag;
}

uint32_t Lcg(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s >> 8;
}

} // namespace

TEST(Formats, DetectedByContent) {
    for (const auto& f : kFixtures) {
        auto data = ReadFixture(f.file);
        ASSERT_FALSE(data.empty()) << f.file;
        EXPECT_EQ(una::DetectSourceKind(data.data(), data.size()), f.kind) << f.file;
    }
    auto ogg = ReadFixture("tone_48k_stereo_3s.ogg");
    EXPECT_EQ(una::DetectSourceKind(ogg.data(), ogg.size()), SourceKind::Vorbis);
    auto wav = MakeWavPcm(48000, 2, 100, 16, [](int, int) { return 0.0f; });
    EXPECT_EQ(una::DetectSourceKind(wav.data(), wav.size()), SourceKind::Wav);
}

TEST(Formats, ProbeMatchesFfmpeg) {
    for (const auto& f : kFixtures) {
        auto data = ReadFixture(f.file);
        una_clip_info info{};
        ASSERT_EQ(una_probe(data.data(), (int64_t)data.size(), &info), UNA_OK) << f.file;
        EXPECT_EQ(info.source_rate, f.rate) << f.file;
        EXPECT_EQ(info.source_channels, f.ch) << f.file;
        EXPECT_EQ(info.channels, f.ch >= 2 ? 2 : 1) << f.file;
        EXPECT_EQ(info.total_frames, f.frames) << f.file;
    }
}

// The pipeline pads a source that ends early with silence; the decoder itself must produce
// exactly the length it announced.
TEST(Formats, DecoderProducesExactlyTotalFrames) {
    for (const auto& f : kFixtures) {
        auto data = ReadFixture(f.file);
        auto d = Open(data);
        ASSERT_TRUE(d) << f.file;
        auto pcm = DecodeSource(*d);
        EXPECT_EQ(int64_t(pcm[0].size()), d->Format().totalFrames) << f.file;
    }
}

TEST(Formats, MatchesFfmpegDecode) {
    for (const auto& f : kFixtures) {
        if (!f.ref) continue;
        auto data = ReadFixture(f.file);
        auto d = Open(data);
        ASSERT_TRUE(d) << f.file;
        auto pcm = DecodeSource(*d);
        auto ref = ReadRef(f.ref);
        const size_t n = 512, ch = size_t(f.ch);
        ASSERT_EQ(ref.size(), 2 * n * ch) << f.ref;
        ASSERT_GE(pcm[0].size(), n);
        // First and last 512 frames: gapless trimming must line up at both ends.
        float worst = 0;
        for (size_t w = 0; w < 2; ++w) {
            const size_t base = w == 0 ? 0 : pcm[0].size() - n;
            for (size_t i = 0; i < n; ++i)
                for (size_t c = 0; c < ch; ++c)
                    worst = std::max(worst, std::fabs(pcm[c][base + i] - ref[(w * n + i) * ch + c]));
        }
        EXPECT_LE(worst, f.tol) << f.file;
        std::cout << "  " << f.file << ": max |ours - ffmpeg| = " << worst << "\n";
    }
}

TEST(Formats, ContentIsTheTone) {
    struct Case { const char* file; int left, right; } cases[] = {
        // Expected zero crossings in a one-second window: 2 per cycle.
        {"mp3_44k_stereo_cbr.mp3", 880, 1320},
        {"mp3_22k_mono.mp3", 880, 0},
        {"mp3_48k_id3.mp3", 880, 1320},
        {"flac_48k_stereo_16.flac", 880, 1320},
        {"flac_44k_mono_24.flac", 880, 0},
    };
    for (const auto& c : cases) {
        auto data = ReadFixture(c.file);
        DecodePipeline p;
        ASSERT_EQ(p.Open(data.data(), data.size(), 0), UNA_OK) << c.file;
        auto pcm = DecodePipe(p);
        const size_t rate = size_t(p.StoredRate());
        const size_t from = pcm[0].size() > 2 * rate ? rate / 2 : 0;
        ASSERT_GE(pcm[0].size(), from + rate) << c.file;
        EXPECT_NEAR(ZeroCrossings(pcm[0], from, from + rate), c.left, 4) << c.file;
        if (c.right) EXPECT_NEAR(ZeroCrossings(pcm[1], from, from + rate), c.right, 4) << c.file;
        EXPECT_NEAR(Rms(pcm[0]), 0.125 / std::sqrt(2.0), 0.005) << c.file;  // ffmpeg sine: amplitude 1/8
    }
    // 32-bit FLAC: 440 Hz at 32 kHz, 0.5 s.
    auto data = ReadFixture("flac_32k_mono_32.flac");
    auto d = Open(data);
    ASSERT_TRUE(d);
    auto pcm = DecodeSource(*d);
    EXPECT_NEAR(ZeroCrossings(pcm[0], 0, pcm[0].size()), 440, 4);
}

TEST(Formats, FlacSurroundDownmixesToStereo) {
    auto data = ReadFixture("flac_48k_51.flac");
    DecodePipeline p;
    ASSERT_EQ(p.Open(data.data(), data.size(), 0), UNA_OK);
    EXPECT_EQ(p.SourceChannels(), 6);
    EXPECT_EQ(p.StoredChannels(), 2);
    auto pcm = DecodePipe(p);
    // A 1/8-amplitude tone on the centre only (WAVE order: FL FR FC LFE BL BR); the centre
    // feeds both sides at 0.7071, normalised by 1 + 0.7071 + 0.7071.
    const double expect = 0.125 / std::sqrt(2.0) * 0.70710678 / (1.0 + 2 * 0.70710678);
    EXPECT_NEAR(Rms(pcm[0]), expect, 0.001);
    EXPECT_NEAR(Rms(pcm[1]), expect, 0.001);
}

TEST(Formats, SeekIsSampleExact) {
    for (const auto& f : kFixtures) {
        auto data = ReadFixture(f.file);
        auto full = Open(data);
        ASSERT_TRUE(full) << f.file;
        const auto ref = DecodeSource(*full);
        const int64_t total = int64_t(ref[0].size());
        // Frame edges of both MPEG frame sizes, the gapless delay region, random points and the end.
        std::vector<int64_t> targets = {0, 1, 575, 576, 577, 1151, 1152, 1153, 1105, 2303, 4097, total / 2,
                                        total - 700, total - 1};
        uint32_t seed = 12345;
        for (int i = 0; i < 12; ++i) targets.push_back(int64_t(Lcg(seed) % uint32_t(total)));
        auto d = Open(data);  // one decoder, seeking back and forth from wherever it is
        for (int64_t t : targets) {
            ASSERT_TRUE(d->Seek(t)) << f.file << " target " << t;
            const auto got = DecodeSource(*d, 700);
            const int64_t expectN = std::min<int64_t>(700, total - t);
            ASSERT_EQ(int64_t(got[0].size()), expectN) << f.file << " target " << t;
            for (size_t c = 0; c < got.size(); ++c)
                for (int64_t i = 0; i < expectN; ++i)
                    ASSERT_EQ(got[c][size_t(i)], ref[c][size_t(t + i)])
                        << f.file << " target " << t << " ch " << c << " i " << i;
        }
        ASSERT_TRUE(d->Seek(total));
        float a = 0, b = 0;
        float* pl[una::kMaxSourceChannels] = {&a, &b, &a, &b, &a, &b, &a, &b};
        EXPECT_EQ(d->Read(pl, 1), 0) << f.file;
    }
}

// Seeks through the resampler (44.1 kHz -> 48 kHz) as the engine does for stream voices.
TEST(Formats, ResampledSeekIsSampleExact) {
    for (const char* file : {"mp3_44k_stereo_vbr.mp3", "mp3_22k_mono.mp3", "flac_44k_mono_24.flac"}) {
        auto data = ReadFixture(file);
        DecodePipeline full;
        ASSERT_EQ(full.Open(data.data(), data.size(), 48000), UNA_OK) << file;
        auto ref = DecodePipe(full);
        ASSERT_EQ(int64_t(ref[0].size()), full.TotalFrames()) << file;
        for (int64_t target : std::vector<int64_t>{1, 1000, 4801, 33333, full.TotalFrames() - 300}) {
            DecodePipeline p;
            ASSERT_EQ(p.Open(data.data(), data.size(), 48000), UNA_OK);
            ASSERT_TRUE(p.Seek(target));
            std::vector<float> a(256), b(256);
            float* pl[2] = {a.data(), b.data()};
            const int n = p.Read(pl, 256);
            ASSERT_GT(n, 0);
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < p.StoredChannels(); ++c)
                    ASSERT_EQ(pl[c][i], ref[size_t(c)][size_t(target + i)]) << file << " target " << target;
        }
    }
}

TEST(Formats, Mp3TagsAndTrailingJunkAreSkipped) {
    const auto plain = ReadFixture("mp3_44k_noxing.mp3");  // starts directly with a frame
    auto d0 = Open(plain);
    ASSERT_TRUE(d0);
    const auto ref = DecodeSource(*d0);

    std::vector<uint8_t> id3 = {'I', 'D', '3', 3, 0, 0, 0, 0, 0x17, 0x70};  // 3056-byte body
    id3.resize(10 + 3056, 0);
    std::memcpy(id3.data() + 10, "TIT2", 4);
    // Trailing tags whose bytes look like frames of the same stream: a 32 kbps header pair in
    // the ID3v1 tag, two real frames inside the APE item.
    std::vector<uint8_t> id3v1(128, ' ');
    std::memcpy(id3v1.data(), "TAG", 3);
    const uint8_t fake[4] = {plain[0], plain[1], uint8_t(0x10 | (plain[2] & 0x0D)), plain[3]};
    std::memcpy(id3v1.data() + 3, fake, 4);
    std::memcpy(id3v1.data() + 3 + 104, fake, 4);
    const auto ape = ApeTag(std::vector<uint8_t>(plain.begin(), plain.begin() + 600));

    // Zero padding after the last frame: that frame must still decode with its bit reservoir.
    const std::vector<uint8_t> junk(300, 0);

    struct Variant { const char* name; bool id3, ape, v1, junk; } variants[] = {
        {"id3v2", true, false, false, false}, {"ape", false, true, false, false},
        {"id3v1", false, false, true, false}, {"ape+id3v1", false, true, true, false},
        {"junk", false, false, false, true}, {"all", true, true, true, false},
    };
    for (const auto& v : variants) {
        std::vector<uint8_t> data;
        if (v.id3) data.insert(data.end(), id3.begin(), id3.end());
        data.insert(data.end(), plain.begin(), plain.end());
        if (v.junk) data.insert(data.end(), junk.begin(), junk.end());
        if (v.ape) data.insert(data.end(), ape.begin(), ape.end());
        if (v.v1) data.insert(data.end(), id3v1.begin(), id3v1.end());
        EXPECT_EQ(una::DetectSourceKind(data.data(), data.size()), SourceKind::Mp3) << v.name;
        auto d = Open(data);
        ASSERT_TRUE(d) << v.name;
        EXPECT_EQ(d->Format().totalFrames, int64_t(ref[0].size())) << v.name;
        EXPECT_EQ(DecodeSource(*d), ref) << v.name;
    }
}

// The first frame of mp3_44k_stereo_cbr.mp3 is an Info tag (78 frames, LAME delay 576 and
// padding) whose header differs from the audio frames' (stereo, not original). Expected
// lengths and alignment are ffmpeg 7.1's decode of each variant.
TEST(Formats, Mp3FirstAudioFrameFollowsFfmpeg) {
    const auto base = ReadFixture("mp3_44k_stereo_cbr.mp3");
    auto d0 = Open(base);
    ASSERT_TRUE(d0);
    const auto ref = DecodeSource(*d0);
    ASSERT_EQ(ref[0].size(), 88200u);
    const auto it = std::search(base.begin(), base.end(), std::begin("Info"), std::end("Info") - 1);
    ASSERT_NE(it, base.end());
    const size_t x = size_t(it - base.begin());
    const size_t f0 = x - 36;  // MPEG-1 stereo: 4-byte header + 32 bytes of side info
    ASSERT_EQ(base[f0], 0xFF);
    const size_t f1 = f0 + 417;  // 128 kbps, no padding
    ASSERT_EQ(base[f1], 0xFF);

    enum : unsigned { kCountsZero = 1, kFlagsZero = 2, kNoMagic = 4, kSameHeader = 8, kCrc = 16 };
    struct Variant { const char* name; unsigned edits; int64_t frames, shift; } variants[] = {
        // A tag (frame count or byte size set) is skipped even with a CRC: 88200, as the original.
        {"same header", kSameHeader, 88200, 0},
        {"crc", kCrc, 88200, 0},
        // Counts zero: not a tag, gapless start only. With its own header the frame is skipped
        // as junk; with the audio frames' header it plays as one frame of audio.
        {"counts zero", kCountsZero, 88751, 0},
        {"counts zero, same header", kCountsZero | kSameHeader, 89903, 1152},
        {"counts zero, same header, crc", kCountsZero | kSameHeader | kCrc, 89903, 1152},
        // No fields, so no LAME data at the expected place: no trimming at all.
        {"flags zero", kFlagsZero, 89856, 1105},
        {"no magic", kNoMagic, 89856, 1105},
    };
    for (const auto& v : variants) {
        auto data = base;
        if (v.edits & kCountsZero) std::fill(data.begin() + std::ptrdiff_t(x + 8), data.begin() + std::ptrdiff_t(x + 16), uint8_t(0));
        if (v.edits & kFlagsZero) std::fill(data.begin() + std::ptrdiff_t(x + 4), data.begin() + std::ptrdiff_t(x + 8), uint8_t(0));
        if (v.edits & kNoMagic) std::memcpy(data.data() + x, "Junk", 4);
        if (v.edits & kSameHeader) data[f0 + 3] = base[f1 + 3];
        if (v.edits & kCrc) data[f0 + 1] &= 0xFE;  // protection bit 0: a CRC follows the header
        auto d = Open(data);
        ASSERT_TRUE(d) << v.name;
        EXPECT_EQ(d->Format().totalFrames, v.frames) << v.name;
        const auto pcm = DecodeSource(*d);
        ASSERT_EQ(int64_t(pcm[0].size()), v.frames) << v.name;
        // The original's audio, shifted by the extra frame or the start trim the variant lost.
        const int64_t shift = v.shift;
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = 0; i < ref[c].size(); i += 97)
                ASSERT_EQ(pcm[c][size_t(shift) + i], ref[c][i]) << v.name << " ch " << c << " i " << i;
    }
}

// A FLAC whose STREAMINFO leaves the length unknown (0): counted at open, then seekable.
TEST(Formats, FlacWithoutLengthIsCountedAndSeekable) {
    auto data = ReadFixture("flac_48k_stereo_16.flac");
    auto d0 = Open(data);
    ASSERT_TRUE(d0);
    const auto ref = DecodeSource(*d0);
    ASSERT_EQ(std::memcmp(data.data(), "fLaC", 4), 0);
    data[21] &= 0xF0;  // the 36-bit total: low nibble of byte 21, bytes 22-25
    std::fill(data.begin() + 22, data.begin() + 26, uint8_t(0));
    auto d = Open(data);
    ASSERT_TRUE(d);
    EXPECT_EQ(d->Format().totalFrames, 48000);
    EXPECT_EQ(DecodeSource(*d), ref);
    for (int64_t t : {int64_t(1000), int64_t(40000), int64_t(7), int64_t(47999)}) {
        ASSERT_TRUE(d->Seek(t));
        const auto got = DecodeSource(*d, 300);
        ASSERT_EQ(int64_t(got[0].size()), std::min<int64_t>(300, 48000 - t)) << t;
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = 0; i < got[c].size(); ++i) ASSERT_EQ(got[c][i], ref[c][size_t(t) + i]) << t;
    }
}

TEST(Formats, GarbageAndLookalikesAreRejected) {
    una_clip_info info{};
    uint32_t seed = 7;
    for (size_t size : {12u, 100u, 4096u, 65536u}) {
        for (int trial = 0; trial < 20; ++trial) {
            std::vector<uint8_t> junk(size);
            for (auto& b : junk) b = uint8_t(Lcg(seed));
            EXPECT_EQ(una_probe(junk.data(), (int64_t)junk.size(), &info), UNA_E_FORMAT) << size;
        }
    }
    // A valid MP3 frame header followed by noise: no confirming second header.
    const auto mp3 = ReadFixture("mp3_44k_noxing.mp3");
    std::vector<uint8_t> lone(mp3.begin(), mp3.begin() + 4);
    for (int i = 0; i < 2000; ++i) lone.push_back(uint8_t(Lcg(seed)));
    EXPECT_FALSE(una::IsMp3Stream(lone.data(), lone.size()));
    EXPECT_EQ(una_probe(lone.data(), (int64_t)lone.size(), &info), UNA_E_FORMAT);
    // ID3v2 tag followed by noise, and an ID3v2 tag claiming more bytes than exist.
    std::vector<uint8_t> tagged = {'I', 'D', '3', 3, 0, 0, 0, 0, 0, 20};
    tagged.resize(30, 0);
    for (int i = 0; i < 500; ++i) tagged.push_back(uint8_t(Lcg(seed)));
    EXPECT_EQ(una_probe(tagged.data(), (int64_t)tagged.size(), &info), UNA_E_FORMAT);
    std::vector<uint8_t> huge = {'I', 'D', '3', 3, 0, 0, 0x7F, 0x7F, 0x7F, 0x7F};
    huge.resize(64, 0);
    EXPECT_EQ(una_probe(huge.data(), (int64_t)huge.size(), &info), UNA_E_FORMAT);
    // FLAC magic followed by noise.
    std::vector<uint8_t> flac = {'f', 'L', 'a', 'C'};
    for (int i = 0; i < 500; ++i) flac.push_back(uint8_t(Lcg(seed)));
    EXPECT_EQ(una_probe(flac.data(), (int64_t)flac.size(), &info), UNA_E_FORMAT);
}

// dr_flac stores samples as 32-bit integers, so the 33-bit side channel of a 32-bit stereo
// stream coded as left/side, right/side or mid/side cannot be decoded. Any frame may use it,
// so 32-bit stereo is refused at open.
TEST(Formats, Flac32BitStereoIsRejected) {
    // Every frame mid/side; and side-coded frames only in the middle (first and last independent).
    for (const char* file : {"flac_32k_stereo_32_side.flac", "flac_8k_stereo_32_midside.flac"}) {
        auto data = ReadFixture(file);
        ASSERT_FALSE(data.empty()) << file;
        EXPECT_EQ(una::DetectSourceKind(data.data(), data.size()), SourceKind::Flac) << file;
        una_clip_info info{};
        EXPECT_EQ(una_probe(data.data(), (int64_t)data.size(), &info), UNA_E_FORMAT) << file;
    }
}

TEST(Formats, TruncatedFilesFailCleanly) {
    una_clip_info info{};
    for (const auto& f : kFixtures) {
        const auto full = ReadFixture(f.file);
        for (size_t keep : {size_t(4), size_t(11), size_t(100), size_t(417), size_t(1200), full.size() / 2,
                            full.size() - 1}) {
            std::vector<uint8_t> data(full.begin(), full.begin() + std::min(keep, full.size()));
            const int rc = una_probe(data.data(), (int64_t)data.size(), &info);
            if (f.kind == SourceKind::Flac) {
                // STREAMINFO promises a length the data cannot reach.
                EXPECT_EQ(rc, UNA_E_FORMAT) << f.file << " keep " << keep;
                continue;
            }
            // MP3 has no global length: a cut stream plays its complete frames, exactly.
            if (rc != UNA_OK) {
                EXPECT_EQ(rc, UNA_E_FORMAT) << f.file << " keep " << keep;
                continue;
            }
            auto d = Open(data);
            ASSERT_TRUE(d) << f.file << " keep " << keep;
            const int64_t total = d->Format().totalFrames;
            EXPECT_LE(total, f.frames) << f.file << " keep " << keep;
            EXPECT_EQ(int64_t(DecodeSource(*d)[0].size()), total) << f.file << " keep " << keep;
            EXPECT_TRUE(d->Seek(total / 3));
        }
    }
}

TEST(Formats, CorruptedDataDecodesWithoutCrashing) {
    uint32_t seed = 99;
    for (const auto& f : kFixtures) {
        auto data = ReadFixture(f.file);
        // Overwrite a few spans past the headers with noise.
        for (int k = 0; k < 6; ++k) {
            const size_t at = data.size() / 4 + size_t(Lcg(seed)) % (data.size() / 2);
            for (size_t i = at; i < std::min(data.size(), at + 64); ++i) data[i] = uint8_t(Lcg(seed));
        }
        auto d = Open(data);
        if (!d) continue;
        auto pcm = DecodeSource(*d);
        EXPECT_LE(int64_t(pcm[0].size()), d->Format().totalFrames) << f.file;
        EXPECT_TRUE(d->Seek(d->Format().totalFrames / 2) || f.kind == SourceKind::Flac) << f.file;
    }
}

namespace {

// Renders slowly enough that the stream service keeps the ring filled.
std::vector<float> RenderPaced(ManualEngine& e, int frames) {
    std::vector<float> out;
    for (int done = 0; done < frames; done += 512) {
        e.Render(512, &out);
        std::this_thread::sleep_for(std::chrono::microseconds(300));
    }
    return out;
}

} // namespace

// Stream voices decode on the stream thread and seek on every loop wrap and user seek; the
// result must match the fully decoded clip sample for sample.
TEST(Formats, StreamLoopAndSeekMatchDecoded) {
    // Resampled MPEG-1 and MPEG-2 MP3, and FLAC through the surround downmix.
    for (const char* file : {"mp3_44k_stereo_cbr.mp3", "mp3_22k_mono.mp3", "flac_48k_51.flac"}) {
        auto bytes = ReadFixture(file);
        auto run = [&](int mode) {
            ManualEngine e;
            una_clip c = LoadClip(bytes, mode, UNA_SAMPLE_FLOAT32);
            EXPECT_EQ(WaitClip(c, UNA_CLIP_LOADED), UNA_CLIP_LOADED) << file;
            una_play_params p = DefaultPlay();
            p.start_dsp_frame = 2048;
            p.loop = 1;
            p.loop_start = 1000;
            p.loop_end = 9000;
            una_voice v = 0;
            EXPECT_EQ(una_voice_play(c, &p, &v), UNA_OK) << file;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            auto out = RenderPaced(e, 30000);
            una_stats st{};
            una_get_stats(&st);
            EXPECT_EQ(st.starved_frames, 0) << file;  // loop wraps never starve
            una_voice_seek(v, 20000, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            auto more = RenderPaced(e, 30000);
            out.insert(out.end(), more.begin(), more.end());
            return out;
        };
        auto a = run(UNA_LOAD_DECODE);
        auto b = run(UNA_LOAD_STREAM);
        ASSERT_EQ(a.size(), b.size()) << file;
        double energy = 0;
        for (size_t i = 0; i < 2 * size_t(30000); ++i) {
            ASSERT_FLOAT_EQ(a[i], b[i]) << file << " sample " << i;
            energy += double(a[i]) * a[i];
        }
        EXPECT_GT(energy, 1.0) << file;
        size_t mismatches = 0;
        for (size_t i = 2 * size_t(30000) + 2 * 4096; i < a.size(); ++i) mismatches += a[i] != b[i];
        EXPECT_EQ(mismatches, 0u) << file;
    }
}

/// Manual benchmark for a long MP3 or FLAC file (not in the repo):
//   UNA_BENCH_FILE=<file> [UNA_BENCH_REF=<ffmpeg -f f32le decode>] UNAudioTests
//   --gtest_also_run_disabled_tests --gtest_filter=Formats.DISABLED_OpenAndSeekTime
TEST(Formats, DISABLED_OpenAndSeekTime) {
    auto load = [](const char* path) {
        std::vector<uint8_t> data;
        if (FILE* fp = path ? std::fopen(path, "rb") : nullptr) {
            std::fseek(fp, 0, SEEK_END);
            data.resize(size_t(std::ftell(fp)));
            std::fseek(fp, 0, SEEK_SET);
            if (std::fread(data.data(), 1, data.size(), fp) != data.size()) data.clear();
            std::fclose(fp);
        }
        return data;
    };
    const char* path = std::getenv("UNA_BENCH_FILE");
    if (!path) GTEST_SKIP() << "set UNA_BENCH_FILE";
    const auto data = load(path);
    ASSERT_FALSE(data.empty());
    using Clock = std::chrono::steady_clock;
    auto us = [](Clock::duration d) { return std::chrono::duration<double, std::micro>(d).count(); };
    std::vector<double> opens;
    std::unique_ptr<una::SourceDecoder> d;
    for (int i = 0; i < 21; ++i) {
        const auto t0 = Clock::now();
        d = Open(data);
        opens.push_back(us(Clock::now() - t0));
        ASSERT_TRUE(d);
    }
    std::sort(opens.begin(), opens.end());
    const int64_t total = d->Format().totalFrames;
    const int ch = d->Format().channels;
    std::cout << "  " << data.size() << " bytes, " << total << " frames, " << ch << " ch @ " << d->Format().sampleRate
              << " Hz\n  Open: min " << opens.front() << " us, median " << opens[opens.size() / 2] << " us\n";

    ASSERT_TRUE(d->Seek(0));
    auto t0 = Clock::now();
    const auto ref = DecodeSource(*d);
    std::cout << "  full decode " << us(Clock::now() - t0) / 1000 << " ms\n";
    ASSERT_EQ(int64_t(ref[0].size()), total);

    uint32_t seed = 1;
    std::vector<std::vector<float>> buf(size_t(ch), std::vector<float>(1024));
    float* pl[una::kMaxSourceChannels];
    for (int c = 0; c < ch; ++c) pl[c] = buf[size_t(c)].data();
    std::vector<double> seeks;
    for (int i = 0; i < 300; ++i) {
        const int64_t t = int64_t(Lcg(seed) % uint32_t(total - 1024));
        t0 = Clock::now();
        ASSERT_TRUE(d->Seek(t));
        ASSERT_EQ(d->Read(pl, 1024), 1024);
        seeks.push_back(us(Clock::now() - t0));
        for (int c = 0; c < ch; ++c)
            for (int k = 0; k < 1024; ++k) ASSERT_EQ(pl[c][k], ref[size_t(c)][size_t(t + k)]) << "seek " << t;
    }
    std::sort(seeks.begin(), seeks.end());
    std::cout << "  Seek + read 1024 frames (300 random, all sample-exact): median " << seeks[seeks.size() / 2]
              << " us, max " << seeks.back() << " us\n";

    const auto refBytes = load(std::getenv("UNA_BENCH_REF"));
    if (!refBytes.empty()) {
        std::vector<float> ff(refBytes.size() / 4);
        std::memcpy(ff.data(), refBytes.data(), ff.size() * 4);
        EXPECT_EQ(int64_t(ff.size() / size_t(ch)), total) << "ffmpeg frame count";
        float worst = 0;
        for (size_t i = 0; i < std::min(ref[0].size(), ff.size() / size_t(ch)); ++i)
            for (size_t c = 0; c < size_t(ch); ++c) worst = std::max(worst, std::fabs(ref[c][i] - ff[i * size_t(ch) + c]));
        std::cout << "  ffmpeg: " << ff.size() / size_t(ch) << " frames, max |ours - ffmpeg| = " << worst << "\n";
    }
}
