// RIFF/WAVE: PCM 8/16/24/32-bit integer, IEEE float 32/64, WAVE_FORMAT_EXTENSIBLE.

#include "SourceDecoder.h"

#include <algorithm>
#include <cstring>

namespace una {

namespace {

uint16_t ReadU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

enum class WavSample { U8, S16, S24, S32, F32, F64 };

} // namespace

class WavSource final : public SourceDecoder {
public:
    bool Open(const uint8_t* data, size_t size) override {
        if (!data || size < 12) return false;
        if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) return false;

        bool haveFmt = false;
        uint16_t tag = 0, channels = 0, bits = 0, blockAlign = 0;
        uint32_t rate = 0;

        size_t off = 12;
        while (off + 8 <= size) {
            const uint8_t* ck = data + off;
            const uint32_t ckSize = ReadU32(ck + 4);
            const size_t body = off + 8;
            if (ckSize > size - body) {
                // Truncated chunk: accept a truncated data chunk, reject others.
                if (std::memcmp(ck, "data", 4) != 0) return false;
            }
            const size_t avail = std::min<size_t>(ckSize, size - body);

            if (std::memcmp(ck, "fmt ", 4) == 0) {
                if (avail < 16) return false;
                tag = ReadU16(data + body);
                channels = ReadU16(data + body + 2);
                rate = ReadU32(data + body + 4);
                blockAlign = ReadU16(data + body + 12);
                bits = ReadU16(data + body + 14);
                if (tag == 0xFFFE && avail >= 26) tag = ReadU16(data + body + 24); // sub-format GUID head
                haveFmt = true;
            } else if (std::memcmp(ck, "data", 4) == 0) {
                if (!haveFmt) return false;
                pcm_ = data + body;
                pcmBytes_ = avail;
                break;
            }
            off = body + ckSize + (ckSize & 1u);
        }
        if (!haveFmt || !pcm_) return false;
        if (channels < 1 || channels > kMaxSourceChannels || rate == 0) return false;

        if (tag == 1) {
            switch (bits) {
                case 8:  kind_ = WavSample::U8; break;
                case 16: kind_ = WavSample::S16; break;
                case 24: kind_ = WavSample::S24; break;
                case 32: kind_ = WavSample::S32; break;
                default: return false;
            }
        } else if (tag == 3) {
            if (bits == 32) kind_ = WavSample::F32;
            else if (bits == 64) kind_ = WavSample::F64;
            else return false;
        } else {
            return false;
        }

        bytesPerSample_ = bits / 8;
        frameBytes_ = blockAlign ? blockAlign : static_cast<size_t>(bytesPerSample_) * channels;
        if (frameBytes_ < static_cast<size_t>(bytesPerSample_) * channels) return false;

        fmt_.sampleRate = static_cast<int>(rate);
        fmt_.channels = channels;
        fmt_.totalFrames = static_cast<int64_t>(pcmBytes_ / frameBytes_);
        return fmt_.totalFrames > 0;
    }

    SourceFormat Format() const override { return fmt_; }

    int Read(float* const* planar, int frames) override {
        const int64_t left = fmt_.totalFrames - pos_;
        const int n = static_cast<int>(std::min<int64_t>(frames, left));
        if (n <= 0) return 0;
        const int ch = fmt_.channels;
        const uint8_t* p = pcm_ + static_cast<size_t>(pos_) * frameBytes_;
        for (int i = 0; i < n; ++i, p += frameBytes_) {
            for (int c = 0; c < ch; ++c) planar[c][i] = Sample(p + static_cast<size_t>(c) * bytesPerSample_);
        }
        pos_ += n;
        return n;
    }

    bool Seek(int64_t frame) override {
        pos_ = std::max<int64_t>(0, std::min(frame, fmt_.totalFrames));
        return true;
    }

private:
    float Sample(const uint8_t* s) const {
        switch (kind_) {
            case WavSample::U8:  return (static_cast<float>(s[0]) - 128.0f) * (1.0f / 128.0f);
            case WavSample::S16: return static_cast<float>(static_cast<int16_t>(ReadU16(s))) * (1.0f / 32768.0f);
            case WavSample::S24: {
                int32_t v = static_cast<int32_t>(s[0] | (s[1] << 8) | (s[2] << 16));
                v = (v << 8) >> 8;
                return static_cast<float>(v) * (1.0f / 8388608.0f);
            }
            case WavSample::S32: return static_cast<float>(static_cast<int32_t>(ReadU32(s))) * (1.0f / 2147483648.0f);
            case WavSample::F32: { float f; std::memcpy(&f, s, 4); return f; }
            case WavSample::F64: { double d; std::memcpy(&d, s, 8); return static_cast<float>(d); }
        }
        return 0.0f;
    }

    SourceFormat fmt_{};
    const uint8_t* pcm_ = nullptr;
    size_t pcmBytes_ = 0;
    size_t frameBytes_ = 0;
    int bytesPerSample_ = 0;
    WavSample kind_ = WavSample::S16;
    int64_t pos_ = 0;
};

std::unique_ptr<SourceDecoder> CreateVorbisSource();

SourceKind DetectSourceKind(const uint8_t* data, size_t size) {
    if (!data || size < 12) return SourceKind::Unknown;
    if (std::memcmp(data, "OggS", 4) == 0) return SourceKind::Vorbis;
    if (std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WAVE", 4) == 0) return SourceKind::Wav;
    return SourceKind::Unknown;
}

std::unique_ptr<SourceDecoder> CreateSourceDecoder(const uint8_t* data, size_t size) {
    std::unique_ptr<SourceDecoder> dec;
    switch (DetectSourceKind(data, size)) {
        case SourceKind::Vorbis: dec = CreateVorbisSource(); break;
        case SourceKind::Wav:    dec = std::make_unique<WavSource>(); break;
        default: return nullptr;
    }
    if (!dec->Open(data, size)) return nullptr;
    return dec;
}

} // namespace una
