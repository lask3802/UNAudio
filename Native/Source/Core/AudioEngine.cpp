#include "AudioEngine.h"
#include "../Decoder/AudioDecoder.h"
#include "../Mixer/AudioMixer.h"
#include "../Platform/AudioOutput.h"
#include <cstring>

// ── Singleton ────────────────────────────────────────────────────

AudioEngine& AudioEngine::Instance() {
    static AudioEngine instance;
    return instance;
}

AudioEngine::AudioEngine() = default;
AudioEngine::~AudioEngine() { Shutdown(); }

// ── Lifecycle ────────────────────────────────────────────────────

UNAudioResult AudioEngine::Initialize(const UNAudioOutputConfig& config) {
    if (initialized_) return UNAUDIO_ERROR_ALREADY_INITIALIZED;

    config_ = config;

    // TODO: Create platform-specific AudioOutput
    // TODO: Create AudioMixer

    initialized_ = true;
    return UNAUDIO_OK;
}

void AudioEngine::Shutdown() {
    if (!initialized_) return;

    std::lock_guard<std::mutex> lock(mutex_);
    sources_.clear();
    output_.reset();
    mixer_.reset();
    initialized_ = false;
}

bool AudioEngine::IsInitialized() const { return initialized_; }

// ── Source management ────────────────────────────────────────────

UNAudioSourceHandle AudioEngine::LoadAudio(const uint8_t* data, size_t size,
                                           UNAudioCompressionMode mode) {
    if (!initialized_) return -1;

    std::lock_guard<std::mutex> lock(mutex_);

    auto source = std::make_unique<AudioSource>();
    // TODO: Create appropriate decoder based on format detection
    source->clipInfo.compressionMode = mode;

    UNAudioSourceHandle handle = nextHandle_++;
    if (static_cast<size_t>(handle) >= sources_.size())
        sources_.resize(handle + 1);
    sources_[handle] = std::move(source);
    return handle;
}

void AudioEngine::UnloadAudio(UNAudioSourceHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (handle >= 0 && static_cast<size_t>(handle) < sources_.size())
        sources_[handle].reset();
}

// ── Playback ─────────────────────────────────────────────────────

UNAudioResult AudioEngine::Play(UNAudioSourceHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (handle < 0 || static_cast<size_t>(handle) >= sources_.size() || !sources_[handle])
        return UNAUDIO_ERROR_INVALID_PARAM;
    sources_[handle]->state = UNAUDIO_STATE_PLAYING;
    return UNAUDIO_OK;
}

UNAudioResult AudioEngine::Pause(UNAudioSourceHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (handle < 0 || static_cast<size_t>(handle) >= sources_.size() || !sources_[handle])
        return UNAUDIO_ERROR_INVALID_PARAM;
    sources_[handle]->state = UNAUDIO_STATE_PAUSED;
    return UNAUDIO_OK;
}

UNAudioResult AudioEngine::Stop(UNAudioSourceHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (handle < 0 || static_cast<size_t>(handle) >= sources_.size() || !sources_[handle])
        return UNAUDIO_ERROR_INVALID_PARAM;
    sources_[handle]->state = UNAUDIO_STATE_STOPPED;
    return UNAUDIO_OK;
}

// ── Properties ───────────────────────────────────────────────────

void AudioEngine::SetVolume(UNAudioSourceHandle handle, float volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (handle >= 0 && static_cast<size_t>(handle) < sources_.size() && sources_[handle])
        sources_[handle]->volume = volume;
}

float AudioEngine::GetVolume(UNAudioSourceHandle handle) const {
    if (handle >= 0 && static_cast<size_t>(handle) < sources_.size() && sources_[handle])
        return sources_[handle]->volume;
    return 0.0f;
}

void AudioEngine::SetLoop(UNAudioSourceHandle handle, bool loop) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (handle >= 0 && static_cast<size_t>(handle) < sources_.size() && sources_[handle])
        sources_[handle]->loop = loop;
}

UNAudioState AudioEngine::GetState(UNAudioSourceHandle handle) const {
    if (handle >= 0 && static_cast<size_t>(handle) < sources_.size() && sources_[handle])
        return sources_[handle]->state;
    return UNAUDIO_STATE_STOPPED;
}

UNAudioClipInfo AudioEngine::GetClipInfo(UNAudioSourceHandle handle) const {
    if (handle >= 0 && static_cast<size_t>(handle) < sources_.size() && sources_[handle])
        return sources_[handle]->clipInfo;
    return {};
}

// ── Engine-level ─────────────────────────────────────────────────

void  AudioEngine::SetMasterVolume(float volume) { masterVolume_ = volume; }
float AudioEngine::GetMasterVolume() const       { return masterVolume_; }

void AudioEngine::SetBufferSize(int32_t frames) {
    config_.bufferSize = frames;
    // TODO: Reconfigure output
}

float AudioEngine::GetCurrentLatency() const {
    if (config_.sampleRate > 0)
        return static_cast<float>(config_.bufferSize) / config_.sampleRate * 1000.0f;
    return 0.0f;
}

// ── P/Invoke C API ───────────────────────────────────────────────

extern "C" {

UNAUDIO_EXPORT int32_t UNAudio_Initialize(UNAudioOutputConfig config) {
    return static_cast<int32_t>(AudioEngine::Instance().Initialize(config));
}

UNAUDIO_EXPORT void UNAudio_Shutdown(void) {
    AudioEngine::Instance().Shutdown();
}

UNAUDIO_EXPORT int32_t UNAudio_IsInitialized(void) {
    return AudioEngine::Instance().IsInitialized() ? 1 : 0;
}

UNAUDIO_EXPORT int32_t UNAudio_LoadAudio(const uint8_t* data, int32_t size,
                                          int32_t compressionMode) {
    return AudioEngine::Instance().LoadAudio(
        data, static_cast<size_t>(size),
        static_cast<UNAudioCompressionMode>(compressionMode));
}

UNAUDIO_EXPORT void UNAudio_UnloadAudio(int32_t handle) {
    AudioEngine::Instance().UnloadAudio(handle);
}

UNAUDIO_EXPORT int32_t UNAudio_Play(int32_t handle) {
    return static_cast<int32_t>(AudioEngine::Instance().Play(handle));
}

UNAUDIO_EXPORT int32_t UNAudio_Pause(int32_t handle) {
    return static_cast<int32_t>(AudioEngine::Instance().Pause(handle));
}

UNAUDIO_EXPORT int32_t UNAudio_Stop(int32_t handle) {
    return static_cast<int32_t>(AudioEngine::Instance().Stop(handle));
}

UNAUDIO_EXPORT void UNAudio_SetVolume(int32_t handle, float volume) {
    AudioEngine::Instance().SetVolume(handle, volume);
}

UNAUDIO_EXPORT float UNAudio_GetVolume(int32_t handle) {
    return AudioEngine::Instance().GetVolume(handle);
}

UNAUDIO_EXPORT void UNAudio_SetLoop(int32_t handle, int32_t loop) {
    AudioEngine::Instance().SetLoop(handle, loop != 0);
}

UNAUDIO_EXPORT int32_t UNAudio_GetState(int32_t handle) {
    return static_cast<int32_t>(AudioEngine::Instance().GetState(handle));
}

UNAUDIO_EXPORT UNAudioClipInfo UNAudio_GetClipInfo(int32_t handle) {
    return AudioEngine::Instance().GetClipInfo(handle);
}

UNAUDIO_EXPORT void UNAudio_SetMasterVolume(float volume) {
    AudioEngine::Instance().SetMasterVolume(volume);
}

UNAUDIO_EXPORT float UNAudio_GetMasterVolume(void) {
    return AudioEngine::Instance().GetMasterVolume();
}

UNAUDIO_EXPORT void UNAudio_SetBufferSize(int32_t frames) {
    AudioEngine::Instance().SetBufferSize(frames);
}

UNAUDIO_EXPORT float UNAudio_GetCurrentLatency(void) {
    return AudioEngine::Instance().GetCurrentLatency();
}

} // extern "C"
