#include "PCMDecoder.h"
#include <cstring>
#include <algorithm>

PCMDecoder::PCMDecoder()  = default;
PCMDecoder::~PCMDecoder() = default;

// Minimal WAV header parser (RIFF/WAVE, PCM format)
bool PCMDecoder::parseWavHeader(const uint8_t* data, size_t size) {
    if (size < 44) return false;

    // Check RIFF header
    if (std::memcmp(data, "RIFF", 4) != 0) return false;
    if (std::memcmp(data + 8, "WAVE", 4) != 0) return false;

    // Find "fmt " chunk
    size_t pos = 12;
    while (pos + 8 <= size) {
        uint32_t chunkSize = 0;
        std::memcpy(&chunkSize, data + pos + 4, 4);

        if (std::memcmp(data + pos, "fmt ", 4) == 0) {
            if (pos + 8 + chunkSize > size) return false;
            const uint8_t* fmt = data + pos + 8;

            uint16_t audioFormat = 0;
            std::memcpy(&audioFormat, fmt, 2);

            // 1 = PCM int, 3 = IEEE float
            if (audioFormat != 1 && audioFormat != 3) return false;
            isFloat_ = (audioFormat == 3);

            std::memcpy(&format_.channels, fmt + 2, 2);
            std::memcpy(&format_.sampleRate, fmt + 4, 4);
            std::memcpy(&format_.bitsPerSample, fmt + 14, 2);
            format_.blockAlign = format_.channels * (format_.bitsPerSample / 8);

        } else if (std::memcmp(data + pos, "data", 4) == 0) {
            pcmData_ = data + pos + 8;
            pcmDataSize_ = chunkSize;
            if (pcmData_ + pcmDataSize_ > data + size)
                pcmDataSize_ = size - (pcmData_ - data);

            if (format_.blockAlign > 0)
                totalFrames_ = static_cast<int64_t>(pcmDataSize_) / format_.blockAlign;
            return true;
        }

        pos += 8 + chunkSize;
        if (chunkSize & 1) pos++; // RIFF chunks are 2-byte aligned
    }

    return false;
}

bool PCMDecoder::Open(const uint8_t* data, size_t size) {
    if (!data || size == 0) return false;
    data_ = data;
    dataSize_ = size;
    currentFrame_ = 0;

    // Try WAV format first
    if (parseWavHeader(data, size)) {
        return true;
    }

    // Fallback: treat as raw 16-bit stereo 44.1kHz PCM
    format_.sampleRate    = 44100;
    format_.channels      = 2;
    format_.bitsPerSample = 16;
    format_.blockAlign    = format_.channels * (format_.bitsPerSample / 8);
    isFloat_ = false;
    pcmData_ = data;
    pcmDataSize_ = size;
    totalFrames_ = static_cast<int64_t>(pcmDataSize_) / format_.blockAlign;

    return true;
}

int PCMDecoder::Decode(float* buffer, int frameCount) {
    if (!pcmData_ || currentFrame_ >= totalFrames_) return 0;

    int64_t framesAvailable = totalFrames_ - currentFrame_;
    int framesToDecode = static_cast<int>(
        std::min(static_cast<int64_t>(frameCount), framesAvailable));

    const int channels = format_.channels;
    const int totalSamples = framesToDecode * channels;

    if (isFloat_ && format_.bitsPerSample == 32) {
        // 32-bit float PCM — direct copy
        size_t byteOffset = static_cast<size_t>(currentFrame_) * format_.blockAlign;
        const float* src = reinterpret_cast<const float*>(pcmData_ + byteOffset);
        std::memcpy(buffer, src, totalSamples * sizeof(float));

    } else if (!isFloat_ && format_.bitsPerSample == 16) {
        // 16-bit int PCM — convert to float
        size_t byteOffset = static_cast<size_t>(currentFrame_) * format_.blockAlign;
        const int16_t* src = reinterpret_cast<const int16_t*>(pcmData_ + byteOffset);
        constexpr float scale = 1.0f / 32768.0f;
        for (int i = 0; i < totalSamples; ++i)
            buffer[i] = static_cast<float>(src[i]) * scale;

    } else if (!isFloat_ && format_.bitsPerSample == 24) {
        // 24-bit int PCM — convert to float
        size_t byteOffset = static_cast<size_t>(currentFrame_) * format_.blockAlign;
        const uint8_t* src = pcmData_ + byteOffset;
        constexpr float scale = 1.0f / 8388608.0f; // 2^23
        for (int i = 0; i < totalSamples; ++i) {
            int32_t sample = (static_cast<int32_t>(src[i * 3 + 2]) << 24) |
                             (static_cast<int32_t>(src[i * 3 + 1]) << 16) |
                             (static_cast<int32_t>(src[i * 3 + 0]) << 8);
            sample >>= 8; // sign extend
            buffer[i] = static_cast<float>(sample) * scale;
        }

    } else {
        // Unsupported format — output silence
        std::memset(buffer, 0, totalSamples * sizeof(float));
    }

    currentFrame_ += framesToDecode;
    return framesToDecode;
}

bool PCMDecoder::Seek(int64_t frame) {
    if (frame < 0) frame = 0;
    if (frame > totalFrames_) frame = totalFrames_;
    currentFrame_ = frame;
    return true;
}

UNAudioFormat PCMDecoder::GetFormat()   const { return format_; }
bool PCMDecoder::SupportsStreaming()     const { return false; }
int64_t PCMDecoder::GetTotalFrames()    const { return totalFrames_; }
