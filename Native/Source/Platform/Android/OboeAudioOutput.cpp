#include "../AudioOutput.h"

#ifdef __ANDROID__

// TODO: #include <oboe/Oboe.h>

/// Oboe-based audio output for Android.
/// Oboe automatically selects AAudio (API 26+) or OpenSL ES (API 16-25).
class OboeAudioOutput : public AudioOutput {
public:
    OboeAudioOutput();
    ~OboeAudioOutput() override;

    bool Initialize(const UNAudioOutputConfig& config) override;
    bool Start() override;
    void Stop() override;
    int32_t GetActualSampleRate() const override;
    int32_t GetActualBufferSize() const override;
    float   GetLatencyMs() const override;

private:
    UNAudioOutputConfig config_{};
    bool running_ = false;
    // TODO: std::shared_ptr<oboe::AudioStream> stream;
};

// ── Implementation stubs ─────────────────────────────────────────

OboeAudioOutput::OboeAudioOutput()  = default;
OboeAudioOutput::~OboeAudioOutput() { Stop(); }

bool OboeAudioOutput::Initialize(const UNAudioOutputConfig& config) {
    config_ = config;
    // TODO: Build Oboe AudioStream with LowLatency + Exclusive mode
    return true;
}

bool OboeAudioOutput::Start() {
    // TODO: stream->requestStart()
    running_ = true;
    return true;
}

void OboeAudioOutput::Stop() {
    // TODO: stream->requestStop(); stream->close();
    running_ = false;
}

int32_t OboeAudioOutput::GetActualSampleRate() const { return config_.sampleRate; }
int32_t OboeAudioOutput::GetActualBufferSize() const { return config_.bufferSize; }
float   OboeAudioOutput::GetLatencyMs() const {
    if (config_.sampleRate > 0)
        return static_cast<float>(config_.bufferSize) / config_.sampleRate * 1000.0f;
    return 0.0f;
}

#endif // __ANDROID__
