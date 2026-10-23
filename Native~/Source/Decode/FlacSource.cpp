// Native FLAC via dr_flac (in-memory). 4–32-bit (32-bit stereo excepted), 1–8 channels
// (WAVE/SMPTE order).
// Length from STREAMINFO; sample-accurate seek (SEEKTABLE or dr_flac's binary search).

#include "SourceDecoder.h"

#include <algorithm>
#include <mutex>
#include <vector>

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4244 4245 4267 4456 4457 4701 4100 4189 4996)
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#  pragma GCC diagnostic ignored "-Wsign-compare"
#  pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include "../../ThirdParty/dr_flac.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

namespace una {

class FlacSource final : public SourceDecoder {
public:
    ~FlacSource() override {
        if (f_) drflac_close(f_);
    }

    bool Open(const uint8_t* data, size_t size) override {
        if (!data || size < 4) return false;
        // dr_flac detects CPU features into globals on open; detect once up front so they hold
        // their final values before decode workers open concurrently (on ARM every open
        // rewrites the same value, which dr_flac marks as a benign race).
        static std::once_flag capsOnce;
        std::call_once(capsOnce, drflac__init_cpu_caps);
        f_ = drflac_open_memory(data, size, nullptr);
        if (!f_) return false;
        if (f_->channels < 1 || f_->channels > kMaxSourceChannels || f_->sampleRate == 0 ||
            f_->bitsPerSample < 4 || f_->bitsPerSample > 32)
            return false;
        // 32-bit stereo may code any frame as left/right/mid + side, and the side channel then
        // needs 33 bits, which dr_flac cannot decode. Refuse the file up front rather than fail
        // at the first such frame mid-playback.
        if (f_->bitsPerSample == 32 && f_->channels == 2) return false;
        fmt_.sampleRate = static_cast<int>(f_->sampleRate);
        fmt_.channels = f_->channels;
        scratch_.resize(static_cast<size_t>(kScratchFrames) * f_->channels);

        drflac_uint64 total = f_->totalPCMFrameCount;
        if (total == 0) {
            // STREAMINFO leaves the length unknown (written by a non-seekable encoder): count it.
            drflac_uint64 n;
            while ((n = drflac_read_pcm_frames_f32(f_, kScratchFrames, scratch_.data())) > 0) total += n;
            if (total == 0) return false;
            // dr_flac clamps seek targets to its own count, so give it the real one.
            f_->totalPCMFrameCount = total;
            if (!drflac_seek_to_pcm_frame(f_, 0)) return false;
        } else {
            // The clip timeline trusts this length, so a truncated file must fail here
            // rather than play silence where its end is missing.
            if (!drflac_seek_to_pcm_frame(f_, total - 1) ||
                drflac_read_pcm_frames_f32(f_, 1, scratch_.data()) != 1 || !drflac_seek_to_pcm_frame(f_, 0))
                return false;
        }
        if (total > static_cast<drflac_uint64>(INT64_MAX)) return false;
        fmt_.totalFrames = static_cast<int64_t>(total);
        pos_ = 0;
        return true;
    }

    SourceFormat Format() const override { return fmt_; }

    int Read(float* const* planar, int frames) override {
        if (!f_) return 0;
        const int want = static_cast<int>(std::min<int64_t>(frames, fmt_.totalFrames - pos_));
        const int ch = fmt_.channels;
        int done = 0;
        while (done < want) {
            const int n = static_cast<int>(drflac_read_pcm_frames_f32(
                f_, static_cast<drflac_uint64>(std::min(want - done, kScratchFrames)), scratch_.data()));
            if (n <= 0) break;
            const float* s = scratch_.data();
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < ch; ++c) planar[c][done + i] = *s++;
            done += n;
        }
        pos_ += done;
        return done;
    }

    bool Seek(int64_t frame) override {
        if (!f_) return false;
        frame = std::max<int64_t>(0, std::min(frame, fmt_.totalFrames));
        if (frame < fmt_.totalFrames && !drflac_seek_to_pcm_frame(f_, static_cast<drflac_uint64>(frame))) return false;
        pos_ = frame;
        return true;
    }

private:
    static constexpr int kScratchFrames = 1024;

    drflac* f_ = nullptr;
    SourceFormat fmt_{};
    std::vector<float> scratch_;
    int64_t pos_ = 0;
};

std::unique_ptr<SourceDecoder> CreateFlacSource() { return std::make_unique<FlacSource>(); }

} // namespace una
