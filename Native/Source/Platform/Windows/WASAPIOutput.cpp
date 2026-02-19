#include "../AudioOutput.h"

#ifdef _WIN32

// TODO: #include <audioclient.h>
// TODO: #include <mmdeviceapi.h>

/// WASAPI-based audio output for Windows (exclusive + shared modes).
class WASAPIOutput : public AudioOutput {
public:
    WASAPIOutput();
    ~WASAPIOutput() override;

    bool Initialize(const UNAudioOutputConfig& config) override;
    bool Start() override;
    void Stop() override;
    int32_t GetActualSampleRate() const override;
    int32_t GetActualBufferSize() const override;
    float   GetLatencyMs() const override;

private:
    UNAudioOutputConfig config_{};
    bool running_ = false;
    // TODO: IAudioClient*, IAudioRenderClient*, etc.
};

// ── Implementation stubs ─────────────────────────────────────────

WASAPIOutput::WASAPIOutput()  = default;
WASAPIOutput::~WASAPIOutput() { Stop(); }

bool WASAPIOutput::Initialize(const UNAudioOutputConfig& config) {
    config_ = config;
    // TODO: CoInitialize, enumerate devices, create audio client
    return true;
}

bool WASAPIOutput::Start() {
    // TODO: IAudioClient::Start()
    running_ = true;
    return true;
}

void WASAPIOutput::Stop() {
    // TODO: IAudioClient::Stop()
    running_ = false;
}

int32_t WASAPIOutput::GetActualSampleRate() const { return config_.sampleRate; }
int32_t WASAPIOutput::GetActualBufferSize() const { return config_.bufferSize; }
float   WASAPIOutput::GetLatencyMs() const {
    if (config_.sampleRate > 0)
        return static_cast<float>(config_.bufferSize) / config_.sampleRate * 1000.0f;
    return 0.0f;
}

#endif // _WIN32
