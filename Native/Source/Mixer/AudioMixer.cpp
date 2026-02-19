#include "AudioMixer.h"
#include "../Core/SimdUtils.h"
#include "../Decoder/AudioDecoder.h"
#include <algorithm>
#include <cstring>

AudioMixer::AudioMixer()  = default;
AudioMixer::~AudioMixer() = default;

void AudioMixer::SetSourceCallback(MixSourceCallback callback) {
    sourceCallback_ = std::move(callback);
}

void AudioMixer::Process(float* outputBuffer, int frameCount, int channels,
                         int maxHandle, una::FrameAllocator* alloc) {
    const int totalSamples = frameCount * channels;

    // Clear output buffer (SIMD)
    una::simd::clear(outputBuffer, totalSamples);
    finishedCount_ = 0;

    if (!sourceCallback_ || maxHandle <= 0) {
        peakLevel_ = 0.0f;
        return;
    }

    // Get scratch buffer for per-voice decode output
    float* mixBuf = nullptr;
    if (alloc) {
        mixBuf = alloc->alloc_array<float>(totalSamples, 32);
    }
    if (!mixBuf) {
        // Fallback to heap buffer (only if alloc failed or not provided)
        heapMixBuffer_.resize(totalSamples);
        mixBuf = heapMixBuffer_.data();
    }

    // Mix each active source
    for (int handle = 0; handle < maxHandle; ++handle) {
        MixSourceInfo info{};
        if (!sourceCallback_(handle, info)) continue;
        if (info.state != UNAUDIO_STATE_PLAYING) continue;
        if (!info.decoder) continue;

        // Determine source channel count for potential mono→stereo conversion
        UNAudioFormat fmt = info.decoder->GetFormat();
        int srcChannels = fmt.channels;

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
                if (finishedCount_ < MAX_VOICES)
                    finishedVoices_[finishedCount_++] = handle;
                continue;
            }
        }

        int decodedSamples = framesDecoded * channels;

        // Mono→stereo upmix: duplicate each sample to L and R (backward for in-place safety)
        if (srcChannels == 1 && channels == 2) {
            for (int i = framesDecoded - 1; i >= 0; --i) {
                mixBuf[i * 2 + 0] = mixBuf[i];
                mixBuf[i * 2 + 1] = mixBuf[i];
            }
        }

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

int AudioMixer::GetFinishedVoiceCount() const {
    return finishedCount_;
}

const UNAudioSourceHandle* AudioMixer::GetFinishedVoices() const {
    return finishedVoices_;
}
