#ifndef UNAUDIO_AUDIO_OUTPUT_H
#define UNAUDIO_AUDIO_OUTPUT_H

#include "../Core/AudioTypes.h"
#include <memory>

// Platform output pull callback.
// The output backend requests `frameCount * channels` float samples.
using AudioRenderCallback = void(*)(float* outputBuffer, int frameCount, int channels, void* userData);

/// Abstract base class for platform-specific audio output.
class AudioOutput {
public:
    virtual ~AudioOutput() = default;

    /// Initialise the audio device with the given configuration.
    virtual bool Initialize(const UNAudioOutputConfig& config,
                            AudioRenderCallback callback,
                            void* userData) = 0;

    /// Start audio playback (output callback will begin firing).
    virtual bool Start() = 0;

    /// Stop audio playback.
    virtual void Stop() = 0;

    /// Get the actual sample rate negotiated with the hardware.
    virtual int32_t GetActualSampleRate() const = 0;

    /// Get the actual buffer size in frames.
    virtual int32_t GetActualBufferSize() const = 0;

    /// Get the estimated output latency in milliseconds.
    virtual float GetLatencyMs() const = 0;
};

/// Factory implemented in each platform output .cpp.
std::unique_ptr<AudioOutput> CreatePlatformAudioOutput();

#endif // UNAUDIO_AUDIO_OUTPUT_H
