#ifndef UNAUDIO_AUDIO_MIXER_H
#define UNAUDIO_AUDIO_MIXER_H

#include "../Core/AudioTypes.h"
#include <vector>
#include <mutex>
#include <cstdint>

/// Multi-track audio mixer with SIMD-ready mixing path.
class AudioMixer {
public:
    AudioMixer();
    ~AudioMixer();

    /// Add a source to the mix bus.
    void AddSource(UNAudioSourceHandle source);

    /// Remove a source from the mix bus.
    void RemoveSource(UNAudioSourceHandle source);

    /// Mix all active sources into outputBuffer (interleaved float).
    void Process(float* outputBuffer, int frameCount, int channels);

    /// Set the master volume applied after mixing.
    void SetMasterVolume(float volume);

    /// Get the current peak level (linear, 0..1+).
    float GetPeakLevel() const;

private:
    std::vector<UNAudioSourceHandle> activeSources_;
    std::mutex mutex_;
    float masterVolume_ = 1.0f;
    float peakLevel_    = 0.0f;

    // Temporary buffer used during mixing
    std::vector<float> mixBuffer_;
};

#endif // UNAUDIO_AUDIO_MIXER_H
