#include "VorbisDecoder.h"
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <limits>

#define STB_VORBIS_NO_STDIO
#include "../../ThirdParty/stb_vorbis.c"

VorbisDecoder::VorbisDecoder()  = default;
VorbisDecoder::~VorbisDecoder() = default;

bool VorbisDecoder::Open(const uint8_t* data, size_t size) {
    pcmData_.clear();
    totalFrames_ = 0;
    currentFrame_.store(0, std::memory_order_relaxed);
    format_ = {};

    if (!data || size < 4) return false;
    if (std::memcmp(data, "OggS", 4) != 0) return false;
    if (size > static_cast<size_t>(std::numeric_limits<int>::max())) return false;

    int channels = 0;
    int sampleRate = 0;
    short* decoded = nullptr;
    int frames = stb_vorbis_decode_memory(
        data, static_cast<int>(size), &channels, &sampleRate, &decoded);

    if (frames <= 0 || !decoded) {
        std::free(decoded);
        return false;
    }

    // Mixer supports mono/stereo only.
    if (channels < 1 || channels > 2 || sampleRate <= 0) {
        std::free(decoded);
        return false;
    }

    const int64_t totalSamples = static_cast<int64_t>(frames) * channels;
    if (totalSamples <= 0) {
        std::free(decoded);
        return false;
    }

    try {
        pcmData_.resize(static_cast<size_t>(totalSamples));
    } catch (...) {
        std::free(decoded);
        return false;
    }

    constexpr float scale = 1.0f / 32768.0f;
    for (int64_t i = 0; i < totalSamples; ++i) {
        pcmData_[static_cast<size_t>(i)] =
            static_cast<float>(decoded[i]) * scale;
    }
    std::free(decoded);

    format_.sampleRate = sampleRate;
    format_.channels = channels;
    format_.bitsPerSample = 16;
    format_.blockAlign = channels * static_cast<int32_t>(sizeof(int16_t));
    totalFrames_ = frames;

    return true;
}

int VorbisDecoder::Decode(float* buffer, int frameCount) {
    if (!buffer || frameCount <= 0 || pcmData_.empty()) return 0;

    const int64_t curFrame = currentFrame_.load(std::memory_order_relaxed);
    if (curFrame >= totalFrames_) return 0;

    const int64_t framesAvailable = totalFrames_ - curFrame;
    const int framesToDecode = static_cast<int>(
        std::min(static_cast<int64_t>(frameCount), framesAvailable));
    if (framesToDecode <= 0) return 0;

    const size_t sampleOffset =
        static_cast<size_t>(curFrame) * static_cast<size_t>(format_.channels);
    const size_t sampleCount =
        static_cast<size_t>(framesToDecode) * static_cast<size_t>(format_.channels);
    std::memcpy(buffer, pcmData_.data() + sampleOffset, sampleCount * sizeof(float));

    currentFrame_.store(curFrame + framesToDecode, std::memory_order_relaxed);
    return framesToDecode;
}

bool VorbisDecoder::Seek(int64_t frame) {
    if (frame < 0) frame = 0;
    if (frame > totalFrames_) frame = totalFrames_;
    currentFrame_.store(frame, std::memory_order_relaxed);
    return true;
}

UNAudioFormat VorbisDecoder::GetFormat() const {
    return format_;
}

bool VorbisDecoder::SupportsStreaming() const {
    return false;
}

int64_t VorbisDecoder::GetTotalFrames() const {
    return totalFrames_;
}

int64_t VorbisDecoder::GetCurrentFrame() const {
    return currentFrame_.load(std::memory_order_relaxed);
}
