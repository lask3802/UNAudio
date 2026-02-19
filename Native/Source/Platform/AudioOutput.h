#ifndef UNAUDIO_AUDIO_OUTPUT_H
#define UNAUDIO_AUDIO_OUTPUT_H

#include "../Core/AudioTypes.h"

/// Abstract base class for platform-specific audio output.
class AudioOutput {
public:
    virtual ~AudioOutput() = default;

    /// Initialise the audio device with the given configuration.
    virtual bool Initialize(const UNAudioOutputConfig& config) = 0;

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

#endif // UNAUDIO_AUDIO_OUTPUT_H
