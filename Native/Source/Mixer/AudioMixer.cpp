#include "AudioMixer.h"
#include <algorithm>
#include <cstring>
#include <cmath>

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

void AudioMixer::Process(float* outputBuffer, int frameCount, int channels) {
    const size_t totalSamples = static_cast<size_t>(frameCount) * channels;

    // Clear output
    std::memset(outputBuffer, 0, totalSamples * sizeof(float));

    // TODO: For each active source, decode into mixBuffer_ and sum into outputBuffer.
    // This will be implemented once decoders are fully integrated.

    // Apply master volume and track peak
    float peak = 0.0f;
    for (size_t i = 0; i < totalSamples; ++i) {
        outputBuffer[i] *= masterVolume_;
        float abs = std::fabs(outputBuffer[i]);
        if (abs > peak) peak = abs;
    }
    peakLevel_ = peak;
}

void AudioMixer::SetMasterVolume(float volume) {
    masterVolume_ = volume;
}

float AudioMixer::GetPeakLevel() const {
    return peakLevel_;
}
