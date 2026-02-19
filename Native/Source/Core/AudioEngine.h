#ifndef UNAUDIO_AUDIO_ENGINE_H
#define UNAUDIO_AUDIO_ENGINE_H

#include "AudioTypes.h"
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>

// Forward declarations
class AudioDecoder;
class AudioMixer;
class AudioOutput;

/// Core audio engine - manages decoders, mixer, and platform output.
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
    void SetLoop(UNAudioSourceHandle handle, bool loop);
    UNAudioState GetState(UNAudioSourceHandle handle) const;
    UNAudioClipInfo GetClipInfo(UNAudioSourceHandle handle) const;

    // Engine-level
    void SetMasterVolume(float volume);
    float GetMasterVolume() const;
    void SetBufferSize(int32_t frames);
    float GetCurrentLatency() const;

private:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    struct AudioSource {
        std::unique_ptr<AudioDecoder> decoder;
        UNAudioState state = UNAUDIO_STATE_STOPPED;
        float volume = 1.0f;
        bool loop = false;
        UNAudioClipInfo clipInfo{};
    };

    std::vector<std::unique_ptr<AudioSource>> sources_;
    std::unique_ptr<AudioMixer> mixer_;
    std::unique_ptr<AudioOutput> output_;
    std::mutex mutex_;
    std::atomic<bool> initialized_{false};
    std::atomic<float> masterVolume_{1.0f};
    UNAudioOutputConfig config_{};
    int32_t nextHandle_ = 0;
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
UNAUDIO_EXPORT void     UNAudio_SetLoop(int32_t handle, int32_t loop);
UNAUDIO_EXPORT int32_t  UNAudio_GetState(int32_t handle);
UNAUDIO_EXPORT UNAudioClipInfo UNAudio_GetClipInfo(int32_t handle);

UNAUDIO_EXPORT void     UNAudio_SetMasterVolume(float volume);
UNAUDIO_EXPORT float    UNAudio_GetMasterVolume(void);
UNAUDIO_EXPORT void     UNAudio_SetBufferSize(int32_t frames);
UNAUDIO_EXPORT float    UNAudio_GetCurrentLatency(void);

#ifdef __cplusplus
}
#endif

#endif // UNAUDIO_AUDIO_ENGINE_H
