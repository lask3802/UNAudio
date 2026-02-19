#include "../AudioOutput.h"

#ifdef __APPLE__

// TODO: #include <AudioToolbox/AudioToolbox.h>
// TODO: #include <AVFoundation/AVFoundation.h>

/// CoreAudio output for macOS and iOS.
class CoreAudioOutput : public AudioOutput {
public:
    CoreAudioOutput();
    ~CoreAudioOutput() override;

    bool Initialize(const UNAudioOutputConfig& config) override;
    bool Start() override;
    void Stop() override;
    int32_t GetActualSampleRate() const override;
    int32_t GetActualBufferSize() const override;
    float   GetLatencyMs() const override;

private:
    UNAudioOutputConfig config_{};
    bool running_ = false;
    // TODO: AudioUnit outputUnit;
};

// ── Implementation stubs ─────────────────────────────────────────

CoreAudioOutput::CoreAudioOutput()  = default;
CoreAudioOutput::~CoreAudioOutput() { Stop(); }

bool CoreAudioOutput::Initialize(const UNAudioOutputConfig& config) {
    config_ = config;
    // TODO: Create AudioUnit, set stream format, set render callback
    return true;
}

bool CoreAudioOutput::Start() {
    // TODO: AudioOutputUnitStart()
    running_ = true;
    return true;
}

void CoreAudioOutput::Stop() {
    // TODO: AudioOutputUnitStop()
    running_ = false;
}

int32_t CoreAudioOutput::GetActualSampleRate() const { return config_.sampleRate; }
int32_t CoreAudioOutput::GetActualBufferSize() const { return config_.bufferSize; }
float   CoreAudioOutput::GetLatencyMs() const {
    if (config_.sampleRate > 0)
        return static_cast<float>(config_.bufferSize) / config_.sampleRate * 1000.0f;
    return 0.0f;
}

#endif // __APPLE__
