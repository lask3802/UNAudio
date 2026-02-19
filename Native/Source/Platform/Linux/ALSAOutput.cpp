#include "../AudioOutput.h"

#ifdef __linux__

// TODO: #include <alsa/asoundlib.h>

/// ALSA-based audio output for Linux.
class ALSAOutput : public AudioOutput {
public:
    ALSAOutput();
    ~ALSAOutput() override;

    bool Initialize(const UNAudioOutputConfig& config) override;
    bool Start() override;
    void Stop() override;
    int32_t GetActualSampleRate() const override;
    int32_t GetActualBufferSize() const override;
    float   GetLatencyMs() const override;

private:
    UNAudioOutputConfig config_{};
    bool running_ = false;
    // TODO: snd_pcm_t* pcmHandle;
};

// ── Implementation stubs ─────────────────────────────────────────

ALSAOutput::ALSAOutput()  = default;
ALSAOutput::~ALSAOutput() { Stop(); }

bool ALSAOutput::Initialize(const UNAudioOutputConfig& config) {
    config_ = config;
    // TODO: snd_pcm_open, snd_pcm_hw_params, etc.
    return true;
}

bool ALSAOutput::Start() {
    // TODO: snd_pcm_start
    running_ = true;
    return true;
}

void ALSAOutput::Stop() {
    // TODO: snd_pcm_drop / snd_pcm_close
    running_ = false;
}

int32_t ALSAOutput::GetActualSampleRate() const { return config_.sampleRate; }
int32_t ALSAOutput::GetActualBufferSize() const { return config_.bufferSize; }
float   ALSAOutput::GetLatencyMs() const {
    if (config_.sampleRate > 0)
        return static_cast<float>(config_.bufferSize) / config_.sampleRate * 1000.0f;
    return 0.0f;
}

#endif // __linux__
