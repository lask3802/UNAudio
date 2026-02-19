#include "test_framework.h"
#include "../Source/Mixer/AudioMixer.h"
#include "../Source/Decoder/PCMDecoder.h"
#include "../Source/Core/FrameAllocator.h"
#include "../Source/Core/SimdUtils.h"
#include <vector>

// Helper: create a simple WAV with a DC signal at a given amplitude
static std::vector<uint8_t> make_dc_wav(int numFrames, float amplitude) {
    const int channels = 2;
    const int bps = 32;
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
    uint16_t audioFormat = 3;
    std::memcpy(wav.data() + 20, &audioFormat, 2);
    uint16_t ch = channels;
    std::memcpy(wav.data() + 22, &ch, 2);
    uint32_t sr = 44100;
    std::memcpy(wav.data() + 24, &sr, 4);
    uint32_t byteRate = 44100 * blockAlign;
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
        samples[i] = amplitude;
    }

    return wav;
}

TEST(Mixer_EmptyOutput) {
    AudioMixer mixer;

    float output[256] = {};
    for (auto& v : output) v = 99.0f;

    mixer.Process(output, 128, 2);

    for (int i = 0; i < 256; ++i)
        ASSERT_NEAR(output[i], 0.0f, 0.0001f);
}

TEST(Mixer_MasterVolume) {
    AudioMixer mixer;
    mixer.SetMasterVolume(0.5f);

    auto wav = make_dc_wav(128, 0.8f);
    PCMDecoder decoder;
    ASSERT_TRUE(decoder.Open(wav.data(), wav.size()));

    UNAudioSourceHandle handle = 1;
    mixer.AddSource(handle);

    mixer.SetSourceCallback([&](UNAudioSourceHandle h, MixSourceInfo& info) -> bool {
        if (h != handle) return false;
        info.decoder = &decoder;
        info.volume = 1.0f;
        info.pan = 0.0f;
        info.loop = false;
        info.state = UNAUDIO_STATE_PLAYING;
        return true;
    });

    float output[256] = {};
    mixer.Process(output, 128, 2);

    // 0.8 * 1.0 * 0.5 = 0.4
    ASSERT_NEAR(output[0], 0.4f, 0.001f);
    ASSERT_NEAR(output[1], 0.4f, 0.001f);
}

TEST(Mixer_SourceVolume) {
    AudioMixer mixer;
    mixer.SetMasterVolume(1.0f);

    auto wav = make_dc_wav(128, 1.0f);
    PCMDecoder decoder;
    ASSERT_TRUE(decoder.Open(wav.data(), wav.size()));

    UNAudioSourceHandle handle = 0;
    mixer.AddSource(handle);

    mixer.SetSourceCallback([&](UNAudioSourceHandle h, MixSourceInfo& info) -> bool {
        if (h != handle) return false;
        info.decoder = &decoder;
        info.volume = 0.25f;
        info.pan = 0.0f;
        info.loop = false;
        info.state = UNAUDIO_STATE_PLAYING;
        return true;
    });

    float output[256] = {};
    mixer.Process(output, 128, 2);

    ASSERT_NEAR(output[0], 0.25f, 0.001f);
}

TEST(Mixer_PeakLevel) {
    AudioMixer mixer;
    mixer.SetMasterVolume(1.0f);

    auto wav = make_dc_wav(128, 0.75f);
    PCMDecoder decoder;
    ASSERT_TRUE(decoder.Open(wav.data(), wav.size()));

    UNAudioSourceHandle handle = 0;
    mixer.AddSource(handle);

    mixer.SetSourceCallback([&](UNAudioSourceHandle h, MixSourceInfo& info) -> bool {
        if (h != handle) return false;
        info.decoder = &decoder;
        info.volume = 1.0f;
        info.pan = 0.0f;
        info.loop = false;
        info.state = UNAUDIO_STATE_PLAYING;
        return true;
    });

    float output[256] = {};
    mixer.Process(output, 128, 2);

    ASSERT_NEAR(mixer.GetPeakLevel(), 0.75f, 0.001f);
}

TEST(Mixer_FinishedVoice) {
    AudioMixer mixer;

    auto wav = make_dc_wav(64, 0.5f);
    PCMDecoder decoder;
    ASSERT_TRUE(decoder.Open(wav.data(), wav.size()));

    UNAudioSourceHandle handle = 42;
    mixer.AddSource(handle);

    mixer.SetSourceCallback([&](UNAudioSourceHandle h, MixSourceInfo& info) -> bool {
        if (h != handle) return false;
        info.decoder = &decoder;
        info.volume = 1.0f;
        info.pan = 0.0f;
        info.loop = false;
        info.state = UNAUDIO_STATE_PLAYING;
        return true;
    });

    float output[128] = {};
    mixer.Process(output, 64, 2);
    ASSERT_TRUE(mixer.GetFinishedVoices().empty());

    mixer.Process(output, 64, 2);
    ASSERT_EQ(mixer.GetFinishedVoices().size(), 1u);
    ASSERT_EQ(mixer.GetFinishedVoices()[0], 42);
}

TEST(Mixer_WithFrameAllocator) {
    AudioMixer mixer;
    una::FrameAllocator alloc(64 * 1024);

    auto wav = make_dc_wav(128, 0.5f);
    PCMDecoder decoder;
    ASSERT_TRUE(decoder.Open(wav.data(), wav.size()));

    UNAudioSourceHandle handle = 0;
    mixer.AddSource(handle);

    mixer.SetSourceCallback([&](UNAudioSourceHandle h, MixSourceInfo& info) -> bool {
        if (h != handle) return false;
        info.decoder = &decoder;
        info.volume = 1.0f;
        info.pan = 0.0f;
        info.loop = false;
        info.state = UNAUDIO_STATE_PLAYING;
        return true;
    });

    float output[256] = {};
    alloc.reset();
    mixer.Process(output, 128, 2, &alloc);

    ASSERT_TRUE(alloc.used() > 0u);
    ASSERT_NEAR(output[0], 0.5f, 0.001f);
}

TEST(Mixer_RemoveSource) {
    AudioMixer mixer;

    UNAudioSourceHandle handle = 7;
    mixer.AddSource(handle);
    mixer.RemoveSource(handle);

    float output[64] = {};
    mixer.Process(output, 32, 2);

    for (int i = 0; i < 64; ++i)
        ASSERT_NEAR(output[i], 0.0f, 0.0001f);
}
