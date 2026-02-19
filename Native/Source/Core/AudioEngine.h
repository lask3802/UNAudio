#ifndef UNAUDIO_AUDIO_ENGINE_H
#define UNAUDIO_AUDIO_ENGINE_H

#include "AudioTypes.h"
#include "CommandQueue.h"
#include "EventQueue.h"
#include "FrameAllocator.h"
#include "MemoryBudget.h"
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <array>

// Forward declarations
class AudioDecoder;
class AudioMixer;
class AudioOutput;

/// Core audio engine - manages decoders, mixer, and platform output.
///
/// Thread safety model:
/// - Main thread holds mutex_ for all source management (Load, Unload, Play, Stop, etc.)
/// - Audio thread reads source pointers via lock-free published snapshot
///   (RCU-style: main thread writes new snapshot, audio thread reads atomically)
/// - Individual source properties (volume, pan, loop, state) are atomic
/// - Decoder access is confined to the audio thread after setup; main thread
///   defers all decoder mutations (Seek) through the CommandQueue
class AudioEngine {
public:
    static AudioEngine& Instance();

    UNAudioResult Initialize(const UNAudioOutputConfig& config);
    void Shutdown();
    bool IsInitialized() const;

    // Source management
    UNAudioSourceHandle LoadAudio(const uint8_t* data, size_t size,
                                  UNAudioCompressionMode mode);
    void UnloadAudio(UNAudioSourceHandle handle);

    // Playback control
    UNAudioResult Play(UNAudioSourceHandle handle);
    UNAudioResult Pause(UNAudioSourceHandle handle);
    UNAudioResult Stop(UNAudioSourceHandle handle);

    // Properties
    void SetVolume(UNAudioSourceHandle handle, float volume);
    float GetVolume(UNAudioSourceHandle handle) const;
    void SetPan(UNAudioSourceHandle handle, float pan);
    float GetPan(UNAudioSourceHandle handle) const;
    void SetLoop(UNAudioSourceHandle handle, bool loop);
    UNAudioState GetState(UNAudioSourceHandle handle) const;
    UNAudioClipInfo GetClipInfo(UNAudioSourceHandle handle) const;
    bool Seek(UNAudioSourceHandle handle, int64_t frame);

    // Engine-level
    void SetMasterVolume(float volume);
    float GetMasterVolume() const;
    void SetBufferSize(int32_t frames);
    float GetCurrentLatency() const;
    float GetPeakLevel() const;

    // Event polling (called from main thread)
    bool PollEvent(una::AudioEvent& outEvent);

    // Audio callback (called from audio thread)
    void AudioCallback(float* outputBuffer, int frameCount, int channels);

    // Memory budget
    una::MemoryUsage GetMemoryUsage() const;

    static constexpr int MAX_SOURCES = 256;

private:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    struct AudioSource {
        std::unique_ptr<AudioDecoder> decoder;
        std::atomic<UNAudioState> state{UNAUDIO_STATE_STOPPED};
        std::atomic<float> volume{1.0f};
        std::atomic<float> pan{0.0f};
        std::atomic<bool>  loop{false};
        UNAudioClipInfo clipInfo{};

        // Owned data buffer (keeps data alive for decoder)
        std::vector<uint8_t> audioData;
    };

    // Lock-free source snapshot for audio thread (RCU-style).
    // Main thread publishes a new snapshot; audio thread reads atomically.
    struct SourceSnapshot {
        AudioSource* sources[MAX_SOURCES] = {};
        int count = 0;
    };

    // Helper: check if handle is valid (caller must hold mutex_)
    bool isValidHandle(UNAudioSourceHandle handle) const;

    // Publish a new snapshot for the audio thread (lock-free on reader side)
    void publishSnapshot();

    // Process commands from the command queue (audio thread)
    void processCommands();

    // Process finished voices (audio thread)
    void processFinishedVoices();

    std::vector<std::unique_ptr<AudioSource>> sources_;
    std::unique_ptr<AudioMixer> mixer_;
    std::unique_ptr<AudioOutput> output_;
    std::unique_ptr<una::FrameAllocator> frameAllocator_;
    std::mutex mutex_;
    std::atomic<bool> initialized_{false};
    std::atomic<float> masterVolume_{1.0f};
    std::atomic<float> peakLevel_{0.0f};
    UNAudioOutputConfig config_{};
    int32_t nextHandle_ = 0;

    // Double-buffered snapshots: main thread writes inactive, then flips.
    // Audio thread reads active snapshot via atomic index — zero locks.
    SourceSnapshot snapshots_[2];
    std::atomic<int> activeSnapshot_{0};

    una::CommandQueue commandQueue_;
    una::EventQueue eventQueue_;
    una::MemoryBudget memoryBudget_;
};

// ── P/Invoke C API (exported to Unity) ──────────────────────────
#ifdef __cplusplus
extern "C" {
#endif

UNAUDIO_EXPORT int32_t  UNAudio_Initialize(UNAudioOutputConfig config);
UNAUDIO_EXPORT void     UNAudio_Shutdown(void);
UNAUDIO_EXPORT int32_t  UNAudio_IsInitialized(void);

UNAUDIO_EXPORT int32_t  UNAudio_LoadAudio(const uint8_t* data, int32_t size,
                                           int32_t compressionMode);
UNAUDIO_EXPORT void     UNAudio_UnloadAudio(int32_t handle);

UNAUDIO_EXPORT int32_t  UNAudio_Play(int32_t handle);
UNAUDIO_EXPORT int32_t  UNAudio_Pause(int32_t handle);
UNAUDIO_EXPORT int32_t  UNAudio_Stop(int32_t handle);

UNAUDIO_EXPORT void     UNAudio_SetVolume(int32_t handle, float volume);
UNAUDIO_EXPORT float    UNAudio_GetVolume(int32_t handle);
UNAUDIO_EXPORT void     UNAudio_SetPan(int32_t handle, float pan);
UNAUDIO_EXPORT float    UNAudio_GetPan(int32_t handle);
UNAUDIO_EXPORT void     UNAudio_SetLoop(int32_t handle, int32_t loop);
UNAUDIO_EXPORT int32_t  UNAudio_GetState(int32_t handle);
UNAUDIO_EXPORT UNAudioClipInfo UNAudio_GetClipInfo(int32_t handle);
UNAUDIO_EXPORT int32_t  UNAudio_Seek(int32_t handle, int64_t frame);

UNAUDIO_EXPORT void     UNAudio_SetMasterVolume(float volume);
UNAUDIO_EXPORT float    UNAudio_GetMasterVolume(void);
UNAUDIO_EXPORT void     UNAudio_SetBufferSize(int32_t frames);
UNAUDIO_EXPORT float    UNAudio_GetCurrentLatency(void);
UNAUDIO_EXPORT float    UNAudio_GetPeakLevel(void);

#ifdef __cplusplus
}
#endif

#endif // UNAUDIO_AUDIO_ENGINE_H
