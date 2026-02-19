#ifndef UNAUDIO_AUDIO_MIXER_H
#define UNAUDIO_AUDIO_MIXER_H

#include "../Core/AudioTypes.h"
#include "../Core/FrameAllocator.h"
#include <vector>
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
/// Lock-free on the audio thread: no mutex, no heap allocation in Process().
///
/// The engine provides a maxHandle bound and a callback that returns source
/// info for each handle.  Process() iterates [0..maxHandle), asks the
/// callback, and mixes every source whose state is PLAYING.
class AudioMixer {
public:
    static constexpr int MAX_VOICES = 256;

    AudioMixer();
    ~AudioMixer();

    /// Set callback to query source info from engine during Process().
    void SetSourceCallback(MixSourceCallback callback);

    /// Mix sources [0..maxHandle) using callback to query each.
    /// Uses FrameAllocator for zero-alloc scratch buffers on audio thread.
    void Process(float* outputBuffer, int frameCount, int channels,
                 int maxHandle = 0, una::FrameAllocator* alloc = nullptr);

    void SetMasterVolume(float volume);
    float GetPeakLevel() const;

    /// Get voices that finished during the last Process() call.
    int GetFinishedVoiceCount() const;
    const UNAudioSourceHandle* GetFinishedVoices() const;

private:
    UNAudioSourceHandle finishedVoices_[MAX_VOICES];
    int finishedCount_ = 0;
    float masterVolume_ = 1.0f;
    float peakLevel_    = 0.0f;

    MixSourceCallback sourceCallback_;

    // Fallback heap buffer when no FrameAllocator is provided
    std::vector<float> heapMixBuffer_;
};

#endif // UNAUDIO_AUDIO_MIXER_H
