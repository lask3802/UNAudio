// Ogg Vorbis via stb_vorbis (pull API, in-memory). Sample-accurate seek.

#include "SourceDecoder.h"

#include <climits>
#include <cstring>
#include <mutex>

#define STB_VORBIS_NO_STDIO
#define UNA_STB_CRC_INIT_ONCE  // see Open()
#define STB_VORBIS_MAX_CHANNELS 8
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4244 4245 4456 4457 4701 4100 4189 4996)
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-value"
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#  pragma GCC diagnostic ignored "-Wsign-compare"
#  pragma GCC diagnostic ignored "-Wmisleading-indentation"
#  if !defined(__clang__)
#    pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#  endif
#endif
#include "../../ThirdParty/stb_vorbis.c"
#if defined(_MSC_VER)
#  pragma warning(pop)
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

namespace una {

class VorbisSource final : public SourceDecoder {
public:
    ~VorbisSource() override {
        if (v_) stb_vorbis_close(v_);
    }

    bool Open(const uint8_t* data, size_t size) override {
        if (!data || size < 4 || size > static_cast<size_t>(INT_MAX)) return false;
        // stb_vorbis' CRC table is global; build it once, before any decoder reads it.
        static std::once_flag crcOnce;
        std::call_once(crcOnce, crc32_init);
        int err = 0;
        v_ = stb_vorbis_open_memory(data, static_cast<int>(size), &err, nullptr);
        if (!v_) return false;
        stb_vorbis_info info = stb_vorbis_get_info(v_);
        if (info.channels < 1 || info.channels > kMaxSourceChannels || info.sample_rate == 0) return false;
        fmt_.sampleRate = static_cast<int>(info.sample_rate);
        fmt_.channels = info.channels;
        const unsigned int len = stb_vorbis_stream_length_in_samples(v_);
        if (len == 0 || len == 0xffffffffu) return false;
        fmt_.totalFrames = static_cast<int64_t>(len);
        return true;
    }

    SourceFormat Format() const override { return fmt_; }

    int Read(float* const* planar, int frames) override {
        if (!v_ || frames <= 0) return 0;
        // stb wants float** (non-const); our pointers are writable.
        return stb_vorbis_get_samples_float(v_, fmt_.channels, const_cast<float**>(planar), frames);
    }

    bool Seek(int64_t frame) override {
        if (!v_) return false;
        if (frame <= 0) return stb_vorbis_seek_start(v_) != 0;
        if (frame >= fmt_.totalFrames) frame = fmt_.totalFrames;
        return stb_vorbis_seek(v_, static_cast<unsigned int>(frame)) != 0;
    }

private:
    stb_vorbis* v_ = nullptr;
    SourceFormat fmt_{};
};

std::unique_ptr<SourceDecoder> CreateVorbisSource() { return std::make_unique<VorbisSource>(); }

} // namespace una
