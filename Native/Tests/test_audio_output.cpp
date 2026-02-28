#include "test_framework.h"
#include "../Source/Platform/AudioOutput.h"

namespace {
void noopCallback(float* outputBuffer, int frameCount, int channels, void* userData) {
    (void)outputBuffer;
    (void)frameCount;
    (void)channels;
    (void)userData;
}
} // namespace

TEST(UNAudio, AudioOutput_FactoryCreatesInstance) {
    auto output = CreatePlatformAudioOutput();
    ASSERT_TRUE(output != nullptr);
}

TEST(UNAudio, AudioOutput_InitializeRejectsNullCallback) {
    auto output = CreatePlatformAudioOutput();
    ASSERT_TRUE(output != nullptr);

    UNAudioOutputConfig config{};
    config.sampleRate = 48000;
    config.channels = 2;
    config.bufferSize = 128;
    config.bufferCount = 2;
    config.exclusiveMode = 0;

    bool ok = output->Initialize(config, nullptr, nullptr);
    ASSERT_FALSE(ok);
}

TEST(UNAudio, AudioOutput_InitializeAcceptsValidCallback) {
    auto output = CreatePlatformAudioOutput();
    ASSERT_TRUE(output != nullptr);

    UNAudioOutputConfig config{};
    config.sampleRate = 48000;
    config.channels = 2;
    config.bufferSize = 128;
    config.bufferCount = 2;
    config.exclusiveMode = 0;

    bool ok = output->Initialize(config, &noopCallback, nullptr);
    ASSERT_TRUE(ok);
}

