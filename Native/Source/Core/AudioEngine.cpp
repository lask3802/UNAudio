#include "AudioEngine.h"
#include "../Decoder/AudioDecoder.h"
#include "../Decoder/PCMDecoder.h"
#include "../Decoder/VorbisDecoder.h"
#include "../Decoder/MP3Decoder.h"
#include "../Decoder/FLACDecoder.h"
#include "../Mixer/AudioMixer.h"
#include "../Platform/AudioOutput.h"
#include <cstring>
#include <algorithm>

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

    // Create mixer
    mixer_ = std::make_unique<AudioMixer>();
    mixer_->SetMasterVolume(masterVolume_.load());

    // Set up source callback for mixer to query source state
    mixer_->SetSourceCallback([this](UNAudioSourceHandle handle, MixSourceInfo& info) -> bool {
        if (!isValidHandle(handle)) return false;
        auto& src = sources_[handle];
        if (!src || !src->decoder) return false;

        info.decoder = src->decoder.get();
        info.volume  = src->volume.load(std::memory_order_relaxed);
        info.pan     = src->pan.load(std::memory_order_relaxed);
        info.loop    = src->loop.load(std::memory_order_relaxed);
        info.state   = src->state.load(std::memory_order_relaxed);
        return true;
    });

    // Create frame allocator for audio thread (128 KB arena)
    frameAllocator_ = std::make_unique<una::FrameAllocator>(128 * 1024);

    // TODO: Create platform-specific AudioOutput based on current platform

    initialized_ = true;
    return UNAUDIO_OK;
}

void AudioEngine::Shutdown() {
    if (!initialized_) return;

    std::lock_guard<std::mutex> lock(mutex_);

    // Stop all active sources
    for (auto& src : sources_) {
        if (src) src->state = UNAUDIO_STATE_STOPPED;
    }

    output_.reset();
    mixer_.reset();
    frameAllocator_.reset();
    sources_.clear();
    nextHandle_ = 0;
    initialized_ = false;
}

bool AudioEngine::IsInitialized() const { return initialized_; }

// ── Helper ───────────────────────────────────────────────────────

bool AudioEngine::isValidHandle(UNAudioSourceHandle handle) const {
    return handle >= 0 &&
           static_cast<size_t>(handle) < sources_.size() &&
           sources_[handle] != nullptr;
}

// ── Source management ────────────────────────────────────────────

UNAudioSourceHandle AudioEngine::LoadAudio(const uint8_t* data, size_t size,
                                           UNAudioCompressionMode mode) {
    if (!initialized_) return -1;
    if (!data || size == 0) return -1;

    std::lock_guard<std::mutex> lock(mutex_);

    // Check memory budget
    if (!memoryBudget_.try_alloc_compressed(size)) {
        return -1; // Over budget
    }

    auto source = std::make_unique<AudioSource>();

    // Own a copy of the audio data (decoder points into this)
    source->audioData.assign(data, data + size);

    // Create appropriate decoder based on compression mode and format
    std::unique_ptr<AudioDecoder> decoder;

    if (mode == UNAUDIO_DECOMPRESS_ON_LOAD || mode == UNAUDIO_COMPRESS_IN_MEMORY) {
        // Try PCM first (WAV format)
        auto pcm = std::make_unique<PCMDecoder>();
        if (pcm->Open(source->audioData.data(), source->audioData.size())) {
            decoder = std::move(pcm);
        }
    }

    if (!decoder) {
        // Try Vorbis
        auto vorbis = std::make_unique<VorbisDecoder>();
        if (vorbis->Open(source->audioData.data(), source->audioData.size())) {
            decoder = std::move(vorbis);
        }
    }

    if (!decoder) {
        // Try MP3
        auto mp3 = std::make_unique<MP3Decoder>();
        if (mp3->Open(source->audioData.data(), source->audioData.size())) {
            decoder = std::move(mp3);
        }
    }

    if (!decoder) {
        // Try FLAC
        auto flac = std::make_unique<FLACDecoder>();
        if (flac->Open(source->audioData.data(), source->audioData.size())) {
            decoder = std::move(flac);
        }
    }

    if (!decoder) {
        memoryBudget_.free_compressed(size);
        return -1;
    }

    // Fill clip info from decoder
    UNAudioFormat fmt = decoder->GetFormat();
    source->clipInfo.sampleRate     = fmt.sampleRate;
    source->clipInfo.channels       = fmt.channels;
    source->clipInfo.bitsPerSample  = fmt.bitsPerSample;
    source->clipInfo.totalFrames    = decoder->GetTotalFrames();
    source->clipInfo.compressionMode = mode;
    if (fmt.sampleRate > 0 && source->clipInfo.totalFrames > 0) {
        source->clipInfo.lengthInSeconds =
            static_cast<float>(source->clipInfo.totalFrames) / fmt.sampleRate;
    }

    source->decoder = std::move(decoder);

    UNAudioSourceHandle handle = nextHandle_++;
    if (static_cast<size_t>(handle) >= sources_.size())
        sources_.resize(handle + 1);
    sources_[handle] = std::move(source);
    return handle;
}

void AudioEngine::UnloadAudio(UNAudioSourceHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (isValidHandle(handle)) {
        auto& src = sources_[handle];
        if (src) {
            // Remove from mixer
            if (mixer_) mixer_->RemoveSource(handle);

            // Free memory budget
            memoryBudget_.free_compressed(src->audioData.size());
        }
        sources_[handle].reset();
    }
}

// ── Playback ─────────────────────────────────────────────────────

UNAudioResult AudioEngine::Play(UNAudioSourceHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isValidHandle(handle)) return UNAUDIO_ERROR_INVALID_PARAM;

    auto& src = sources_[handle];
    if (!src->decoder) return UNAUDIO_ERROR_DECODE_FAILED;

    UNAudioState prev = src->state.load(std::memory_order_relaxed);

    // If stopped, seek to beginning
    if (prev == UNAUDIO_STATE_STOPPED) {
        src->decoder->Seek(0);
    }

    src->state = UNAUDIO_STATE_PLAYING;

    // Register with mixer
    if (mixer_) {
        mixer_->AddSource(handle);
    }

    return UNAUDIO_OK;
}

UNAudioResult AudioEngine::Pause(UNAudioSourceHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isValidHandle(handle)) return UNAUDIO_ERROR_INVALID_PARAM;
    sources_[handle]->state = UNAUDIO_STATE_PAUSED;
    return UNAUDIO_OK;
}

UNAudioResult AudioEngine::Stop(UNAudioSourceHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isValidHandle(handle)) return UNAUDIO_ERROR_INVALID_PARAM;

    sources_[handle]->state = UNAUDIO_STATE_STOPPED;
    if (sources_[handle]->decoder) {
        sources_[handle]->decoder->Seek(0);
    }
    if (mixer_) mixer_->RemoveSource(handle);

    return UNAUDIO_OK;
}

// ── Properties ───────────────────────────────────────────────────

void AudioEngine::SetVolume(UNAudioSourceHandle handle, float volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (isValidHandle(handle))
        sources_[handle]->volume = volume;
}

float AudioEngine::GetVolume(UNAudioSourceHandle handle) const {
    if (isValidHandle(handle))
        return sources_[handle]->volume.load(std::memory_order_relaxed);
    return 0.0f;
}

void AudioEngine::SetPan(UNAudioSourceHandle handle, float pan) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (isValidHandle(handle))
        sources_[handle]->pan = std::max(-1.0f, std::min(1.0f, pan));
}

float AudioEngine::GetPan(UNAudioSourceHandle handle) const {
    if (isValidHandle(handle))
        return sources_[handle]->pan.load(std::memory_order_relaxed);
    return 0.0f;
}

void AudioEngine::SetLoop(UNAudioSourceHandle handle, bool loop) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (isValidHandle(handle))
        sources_[handle]->loop = loop;
}

UNAudioState AudioEngine::GetState(UNAudioSourceHandle handle) const {
    if (isValidHandle(handle))
        return sources_[handle]->state.load(std::memory_order_relaxed);
    return UNAUDIO_STATE_STOPPED;
}

UNAudioClipInfo AudioEngine::GetClipInfo(UNAudioSourceHandle handle) const {
    if (isValidHandle(handle))
        return sources_[handle]->clipInfo;
    return {};
}

bool AudioEngine::Seek(UNAudioSourceHandle handle, int64_t frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isValidHandle(handle) || !sources_[handle]->decoder)
        return false;
    return sources_[handle]->decoder->Seek(frame);
}

// ── Engine-level ─────────────────────────────────────────────────

void AudioEngine::SetMasterVolume(float volume) {
    masterVolume_ = volume;
    if (mixer_) mixer_->SetMasterVolume(volume);
}

float AudioEngine::GetMasterVolume() const { return masterVolume_; }

void AudioEngine::SetBufferSize(int32_t frames) {
    config_.bufferSize = frames;
    // TODO: Reconfigure output
}

float AudioEngine::GetCurrentLatency() const {
    if (output_) return output_->GetLatencyMs();
    if (config_.sampleRate > 0)
        return static_cast<float>(config_.bufferSize) / config_.sampleRate * 1000.0f;
    return 0.0f;
}

float AudioEngine::GetPeakLevel() const {
    return peakLevel_.load(std::memory_order_relaxed);
}

// ── Event system ─────────────────────────────────────────────────

bool AudioEngine::PollEvent(una::AudioEvent& outEvent) {
    return eventQueue_.try_pop(outEvent);
}

// ── Audio callback (called from platform audio thread) ───────────

void AudioEngine::AudioCallback(float* outputBuffer, int frameCount, int channels) {
    if (!mixer_) return;

    // Reset frame allocator at top of each callback
    if (frameAllocator_) {
        frameAllocator_->reset();
    }

    // Process pending commands from main thread
    processCommands();

    // Mix all active sources
    mixer_->Process(outputBuffer, frameCount, channels, frameAllocator_.get());

    // Update peak level for metering
    peakLevel_.store(mixer_->GetPeakLevel(), std::memory_order_relaxed);

    // Handle finished voices
    processFinishedVoices();
}

void AudioEngine::processCommands() {
    una::AudioCommand cmd;
    while (commandQueue_.try_pop(cmd)) {
        if (!isValidHandle(cmd.voice_id)) continue;

        auto& src = sources_[cmd.voice_id];
        if (!src) continue;

        switch (cmd.type) {
            case una::AudioCommand::Type::SetVolume:
                src->volume.store(cmd.param0, std::memory_order_relaxed);
                break;
            case una::AudioCommand::Type::SetPan:
                src->pan.store(cmd.param0, std::memory_order_relaxed);
                break;
            case una::AudioCommand::Type::SetLoop:
                src->loop.store(cmd.param0 > 0.0f, std::memory_order_relaxed);
                break;
            default:
                break;
        }
    }
}

void AudioEngine::processFinishedVoices() {
    if (!mixer_) return;

    for (auto handle : mixer_->GetFinishedVoices()) {
        if (isValidHandle(handle)) {
            sources_[handle]->state.store(UNAUDIO_STATE_STOPPED,
                                          std::memory_order_relaxed);
        }
        // Notify main thread
        una::AudioEvent evt;
        evt.type = una::AudioEvent::Type::VoiceFinished;
        evt.voice_id = handle;
        eventQueue_.try_push(evt);
    }
}

// ── Memory budget ────────────────────────────────────────────────

una::MemoryUsage AudioEngine::GetMemoryUsage() const {
    return memoryBudget_.get_usage();
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

UNAUDIO_EXPORT void UNAudio_SetPan(int32_t handle, float pan) {
    AudioEngine::Instance().SetPan(handle, pan);
}

UNAUDIO_EXPORT float UNAudio_GetPan(int32_t handle) {
    return AudioEngine::Instance().GetPan(handle);
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

UNAUDIO_EXPORT int32_t UNAudio_Seek(int32_t handle, int64_t frame) {
    return AudioEngine::Instance().Seek(handle, frame) ? 1 : 0;
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

UNAUDIO_EXPORT float UNAudio_GetPeakLevel(void) {
    return AudioEngine::Instance().GetPeakLevel();
}

} // extern "C"
