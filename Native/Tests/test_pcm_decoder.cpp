#include "test_framework.h"
#include "../Source/Decoder/PCMDecoder.h"
#include <vector>

// Helper: build a minimal valid WAV file (16-bit stereo 44100 Hz)
static std::vector<uint8_t> make_wav_16bit(int numFrames) {
    const int channels = 2;
    const int bps = 16;
    const int sampleRate = 44100;
    const int blockAlign = channels * (bps / 8);
    const int dataSize = numFrames * blockAlign;

    std::vector<uint8_t> wav(44 + dataSize);

    std::memcpy(wav.data(), "RIFF", 4);
    uint32_t fileSize = 36 + dataSize;
    std::memcpy(wav.data() + 4, &fileSize, 4);
    std::memcpy(wav.data() + 8, "WAVE", 4);

    std::memcpy(wav.data() + 12, "fmt ", 4);
    uint32_t fmtSize = 16;
    std::memcpy(wav.data() + 16, &fmtSize, 4);
    uint16_t audioFormat = 1; // PCM
    std::memcpy(wav.data() + 20, &audioFormat, 2);
    uint16_t ch = channels;
    std::memcpy(wav.data() + 22, &ch, 2);
    uint32_t sr = sampleRate;
    std::memcpy(wav.data() + 24, &sr, 4);
    uint32_t byteRate = sampleRate * blockAlign;
    std::memcpy(wav.data() + 28, &byteRate, 4);
    uint16_t ba = blockAlign;
    std::memcpy(wav.data() + 32, &ba, 2);
    uint16_t bits = bps;
    std::memcpy(wav.data() + 34, &bits, 2);

    std::memcpy(wav.data() + 36, "data", 4);
    uint32_t ds = dataSize;
    std::memcpy(wav.data() + 40, &ds, 4);

    int16_t* samples = reinterpret_cast<int16_t*>(wav.data() + 44);
    for (int i = 0; i < numFrames * channels; ++i) {
        samples[i] = static_cast<int16_t>((i % 1000) * 32);
    }

    return wav;
}

// Helper: build a 32-bit float WAV
static std::vector<uint8_t> make_wav_float32(int numFrames) {
    const int channels = 2;
    const int bps = 32;
    const int sampleRate = 48000;
    const int blockAlign = channels * (bps / 8);
    const int dataSize = numFrames * blockAlign;

    std::vector<uint8_t> wav(44 + dataSize);

    std::memcpy(wav.data(), "RIFF", 4);
    uint32_t fileSize = 36 + dataSize;
    std::memcpy(wav.data() + 4, &fileSize, 4);
    std::memcpy(wav.data() + 8, "WAVE", 4);

    std::memcpy(wav.data() + 12, "fmt ", 4);
    uint32_t fmtSize = 16;
    std::memcpy(wav.data() + 16, &fmtSize, 4);
    uint16_t audioFormat = 3; // IEEE float
    std::memcpy(wav.data() + 20, &audioFormat, 2);
    uint16_t ch = channels;
    std::memcpy(wav.data() + 22, &ch, 2);
    uint32_t sr = sampleRate;
    std::memcpy(wav.data() + 24, &sr, 4);
    uint32_t byteRate = sampleRate * blockAlign;
    std::memcpy(wav.data() + 28, &byteRate, 4);
    uint16_t ba = blockAlign;
    std::memcpy(wav.data() + 32, &ba, 2);
    uint16_t bits = bps;
    std::memcpy(wav.data() + 34, &bits, 2);

    std::memcpy(wav.data() + 36, "data", 4);
    uint32_t ds = dataSize;
    std::memcpy(wav.data() + 40, &ds, 4);

    float* samples = reinterpret_cast<float*>(wav.data() + 44);
    for (int i = 0; i < numFrames * channels; ++i) {
        samples[i] = static_cast<float>(i) / (numFrames * channels);
    }

    return wav;
}

TEST(PCMDecoder_OpenWav16) {
    auto wav = make_wav_16bit(1024);
    PCMDecoder dec;
    ASSERT_TRUE(dec.Open(wav.data(), wav.size()));

    UNAudioFormat fmt = dec.GetFormat();
    ASSERT_EQ(fmt.sampleRate, 44100);
    ASSERT_EQ(fmt.channels, 2);
    ASSERT_EQ(fmt.bitsPerSample, 16);
    ASSERT_EQ(dec.GetTotalFrames(), 1024);
}

TEST(PCMDecoder_OpenWavFloat32) {
    auto wav = make_wav_float32(512);
    PCMDecoder dec;
    ASSERT_TRUE(dec.Open(wav.data(), wav.size()));

    UNAudioFormat fmt = dec.GetFormat();
    ASSERT_EQ(fmt.sampleRate, 48000);
    ASSERT_EQ(fmt.channels, 2);
    ASSERT_EQ(fmt.bitsPerSample, 32);
    ASSERT_EQ(dec.GetTotalFrames(), 512);
}

TEST(PCMDecoder_Decode16) {
    auto wav = make_wav_16bit(256);
    PCMDecoder dec;
    ASSERT_TRUE(dec.Open(wav.data(), wav.size()));

    float buf[512]; // 256 frames * 2 channels
    int decoded = dec.Decode(buf, 256);
    ASSERT_EQ(decoded, 256);
    ASSERT_NEAR(buf[0], 0.0f, 0.001f);
}

TEST(PCMDecoder_DecodeFloat32) {
    auto wav = make_wav_float32(128);
    PCMDecoder dec;
    ASSERT_TRUE(dec.Open(wav.data(), wav.size()));

    float buf[256]; // 128 frames * 2 channels
    int decoded = dec.Decode(buf, 128);
    ASSERT_EQ(decoded, 128);
    ASSERT_NEAR(buf[0], 0.0f, 0.001f);
}

TEST(PCMDecoder_Seek) {
    auto wav = make_wav_16bit(1000);
    PCMDecoder dec;
    ASSERT_TRUE(dec.Open(wav.data(), wav.size()));

    float buf[200];
    int decoded = dec.Decode(buf, 100);
    ASSERT_EQ(decoded, 100);

    ASSERT_TRUE(dec.Seek(0));

    float buf2[200];
    decoded = dec.Decode(buf2, 100);
    ASSERT_EQ(decoded, 100);
    ASSERT_NEAR(buf[0], buf2[0], 0.0001f);
    ASSERT_NEAR(buf[50], buf2[50], 0.0001f);
}

TEST(PCMDecoder_PartialDecode) {
    auto wav = make_wav_16bit(100);
    PCMDecoder dec;
    ASSERT_TRUE(dec.Open(wav.data(), wav.size()));

    float buf[400];
    int decoded = dec.Decode(buf, 200);
    ASSERT_EQ(decoded, 100);

    decoded = dec.Decode(buf, 200);
    ASSERT_EQ(decoded, 0);
}

TEST(PCMDecoder_SeekBeyondEnd) {
    auto wav = make_wav_16bit(100);
    PCMDecoder dec;
    ASSERT_TRUE(dec.Open(wav.data(), wav.size()));

    ASSERT_TRUE(dec.Seek(9999));

    float buf[200];
    int decoded = dec.Decode(buf, 100);
    ASSERT_EQ(decoded, 0);
}

TEST(PCMDecoder_RawFallback) {
    uint8_t raw[100] = {};
    PCMDecoder dec;
    ASSERT_TRUE(dec.Open(raw, sizeof(raw)));

    UNAudioFormat fmt = dec.GetFormat();
    ASSERT_EQ(fmt.sampleRate, 44100);
    ASSERT_EQ(fmt.channels, 2);
    ASSERT_EQ(fmt.bitsPerSample, 16);
}
