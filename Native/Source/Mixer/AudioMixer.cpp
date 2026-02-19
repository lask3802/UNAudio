#include "AudioMixer.h"
#include "../Core/SimdUtils.h"
#include "../Decoder/AudioDecoder.h"
#include <algorithm>
#include <cstring>

AudioMixer::AudioMixer()  = default;
AudioMixer::~AudioMixer() = default;

void AudioMixer::AddSource(UNAudioSourceHandle source) {
    std::lock_guard<std::mutex> lock(mutex_);
    activeSources_.push_back(source);
}

void AudioMixer::RemoveSource(UNAudioSourceHandle source) {
    std::lock_guard<std::mutex> lock(mutex_);
    activeSources_.erase(
        std::remove(activeSources_.begin(), activeSources_.end(), source),
        activeSources_.end());
}

void AudioMixer::SetSourceCallback(MixSourceCallback callback) {
    sourceCallback_ = std::move(callback);
}

void AudioMixer::Process(float* outputBuffer, int frameCount, int channels,
                         una::FrameAllocator* alloc) {
    const int totalSamples = frameCount * channels;

    // Clear output buffer (SIMD)
    una::simd::clear(outputBuffer, totalSamples);
    finishedVoices_.clear();

    // Snapshot active sources under lock
    std::vector<UNAudioSourceHandle> sources;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sources = activeSources_;
    }

    if (!sourceCallback_ || sources.empty()) {
        peakLevel_ = 0.0f;
        return;
    }

    // Get scratch buffer for per-voice decode output
    float* mixBuf = nullptr;
    if (alloc) {
        mixBuf = alloc->alloc_array<float>(totalSamples, 32);
    } else {
        heapMixBuffer_.resize(totalSamples);
        mixBuf = heapMixBuffer_.data();
    }

    // Mix each active source
    for (auto handle : sources) {
        MixSourceInfo info{};
        if (!sourceCallback_(handle, info)) continue;
        if (info.state != UNAUDIO_STATE_PLAYING) continue;
        if (!info.decoder) continue;

        // Decode audio into scratch buffer
        una::simd::clear(mixBuf, totalSamples);
        int framesDecoded = info.decoder->Decode(mixBuf, frameCount);

        if (framesDecoded <= 0) {
            if (info.loop) {
                // Loop: seek to beginning and try again
                info.decoder->Seek(0);
                framesDecoded = info.decoder->Decode(mixBuf, frameCount);
            }
            if (framesDecoded <= 0) {
                finishedVoices_.push_back(handle);
                continue;
            }
        }

        const int decodedSamples = framesDecoded * channels;

        // Apply per-voice pan (stereo only)
        if (channels == 2 && info.pan != 0.0f) {
            una::simd::apply_stereo_pan(mixBuf, info.pan, framesDecoded);
        }

        // Mix into output with volume (SIMD accelerated)
        una::simd::mix_add(outputBuffer, mixBuf, info.volume, decodedSamples);
    }

    // Apply master volume (SIMD accelerated)
    if (masterVolume_ != 1.0f) {
        una::simd::apply_gain(outputBuffer, masterVolume_, totalSamples);
    }

    // Track peak level for metering (SIMD accelerated)
    peakLevel_ = una::simd::peak_level(outputBuffer, totalSamples);
}

void AudioMixer::SetMasterVolume(float volume) {
    masterVolume_ = volume;
}

float AudioMixer::GetPeakLevel() const {
    return peakLevel_;
}

const std::vector<UNAudioSourceHandle>& AudioMixer::GetFinishedVoices() const {
    return finishedVoices_;
}
