#ifndef UNAUDIO_AUDIO_MIXER_H
#define UNAUDIO_AUDIO_MIXER_H

#include "../Core/AudioTypes.h"
#include "../Core/FrameAllocator.h"
#include <vector>
#include <mutex>
#include <cstdint>
#include <functional>

class AudioDecoder;

/// Callback to get a source's decoder, volume, and state from the engine.
struct MixSourceInfo {
    AudioDecoder* decoder;
    float volume;
    float pan;          // -1.0 (left) to 1.0 (right)
    bool  loop;
    UNAudioState state;
};

using MixSourceCallback = std::function<bool(UNAudioSourceHandle, MixSourceInfo&)>;

/// Multi-track audio mixer with SIMD mixing path.
class AudioMixer {
public:
    AudioMixer();
    ~AudioMixer();

    void AddSource(UNAudioSourceHandle source);
    void RemoveSource(UNAudioSourceHandle source);

    /// Set callback to query source info from engine during Process().
    void SetSourceCallback(MixSourceCallback callback);

    /// Mix all active sources into outputBuffer (interleaved float).
    /// Uses FrameAllocator for zero-alloc scratch buffers on audio thread.
    void Process(float* outputBuffer, int frameCount, int channels,
                 una::FrameAllocator* alloc = nullptr);

    void SetMasterVolume(float volume);
    float GetPeakLevel() const;

    /// Get list of voices that finished during the last Process() call.
    const std::vector<UNAudioSourceHandle>& GetFinishedVoices() const;

private:
    std::vector<UNAudioSourceHandle> activeSources_;
    std::vector<UNAudioSourceHandle> finishedVoices_;
    std::mutex mutex_;
    float masterVolume_ = 1.0f;
    float peakLevel_    = 0.0f;

    MixSourceCallback sourceCallback_;

    // Fallback heap buffer when no FrameAllocator is provided
    std::vector<float> heapMixBuffer_;
};

#endif // UNAUDIO_AUDIO_MIXER_H
