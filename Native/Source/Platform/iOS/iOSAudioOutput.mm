#include "../AudioOutput.h"

#if defined(__APPLE__) && defined(TARGET_OS_IPHONE)

// TODO: #include <AudioToolbox/AudioToolbox.h>
// TODO: #include <AVFoundation/AVAudioSession.h>

/// CoreAudio-based output for iOS with AVAudioSession integration.
class iOSAudioOutput : public AudioOutput {
public:
    iOSAudioOutput();
    ~iOSAudioOutput() override;

    bool Initialize(const UNAudioOutputConfig& config) override;
    bool Start() override;
    void Stop() override;
    int32_t GetActualSampleRate() const override;
    int32_t GetActualBufferSize() const override;
    float   GetLatencyMs() const override;

private:
    UNAudioOutputConfig config_{};
    bool running_ = false;
};

// ── Implementation stubs ─────────────────────────────────────────

iOSAudioOutput::iOSAudioOutput()  = default;
iOSAudioOutput::~iOSAudioOutput() { Stop(); }

bool iOSAudioOutput::Initialize(const UNAudioOutputConfig& config) {
    config_ = config;
    // TODO: AVAudioSession setup, AudioUnit creation
    return true;
}

bool iOSAudioOutput::Start() {
    running_ = true;
    return true;
}

void iOSAudioOutput::Stop() {
    running_ = false;
}

int32_t iOSAudioOutput::GetActualSampleRate() const { return config_.sampleRate; }
int32_t iOSAudioOutput::GetActualBufferSize() const { return config_.bufferSize; }
float   iOSAudioOutput::GetLatencyMs() const {
    if (config_.sampleRate > 0)
        return static_cast<float>(config_.bufferSize) / config_.sampleRate * 1000.0f;
    return 0.0f;
}

#endif // __APPLE__ && TARGET_OS_IPHONE
