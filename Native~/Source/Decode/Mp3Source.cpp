// MPEG-1/2/2.5 Layer III via dr_mp3's frame decoder (minimp3), in memory.
//
// Open walks the frame headers (no synthesis) into an index of frame offsets, which gives
// the exact length and lets Seek jump straight to a frame. Gapless playback follows ffmpeg:
// a LAME/Lavf/Lavc Info tag removes encoder delay + 529 decoder samples at the start and
// the encoder padding - 529 at the end; without such a tag nothing is trimmed.

#include "SourceDecoder.h"

#include <algorithm>
#include <cstring>
#include <vector>

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DR_MP3_FLOAT_OUTPUT
#define DR_MP3_ONLY_MP3
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4244 4245 4456 4457 4701 4100 4189 4996)
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#  pragma GCC diagnostic ignored "-Wsign-compare"
#  pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include "../../ThirdParty/dr_mp3.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

namespace una {

namespace {

constexpr int kDecoderDelay = 529;   // 528 + 1: the decoder delay LAME and ffmpeg assume
constexpr int kMaxReservoir = 511;   // largest main_data_begin (MPEG-1; MPEG-2 uses <= 255)

struct FrameHeader {
    int version = 0;       // 3 = MPEG-1, 2 = MPEG-2, 0 = MPEG-2.5 (header bits)
    int rateIndex = 0;
    int sampleRate = 0;
    int samples = 0;       // per channel: 1152 (MPEG-1) or 576
    int bytes = 0;         // whole frame, padding included
    int channels = 0;
    int sideInfoEnd = 0;   // offset of the first byte after the side info
};

// Layer III only; free-format streams (bitrate index 0) are not supported.
bool ParseHeader(const uint8_t* h, FrameHeader& out) {
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return false;
    const int version = (h[1] >> 3) & 3;
    const int layer = (h[1] >> 1) & 3;
    const int bitrate = h[2] >> 4;
    const int rate = (h[2] >> 2) & 3;
    if (version == 1 || layer != 1 || bitrate == 0 || bitrate == 15 || rate == 3) return false;
    static const int kRates[3] = {44100, 48000, 32000};
    static const int kKbps[2][15] = {
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},       // MPEG-2 / 2.5
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320}};  // MPEG-1
    const bool mpeg1 = version == 3;
    out.version = version;
    out.rateIndex = rate;
    out.sampleRate = kRates[rate] >> (mpeg1 ? 0 : (version == 2 ? 1 : 2));
    out.samples = mpeg1 ? 1152 : 576;
    out.bytes = out.samples / 8 * kKbps[mpeg1 ? 1 : 0][bitrate] * 1000 / out.sampleRate + ((h[2] >> 1) & 1);
    out.channels = (h[3] >> 6) == 3 ? 1 : 2;
    const int sideInfo = mpeg1 ? (out.channels == 1 ? 17 : 32) : (out.channels == 1 ? 9 : 17);
    out.sideInfoEnd = 4 + ((h[1] & 1) ? 0 : 2) + sideInfo;
    return true;
}

bool SameStream(const FrameHeader& a, const FrameHeader& b) {
    return a.version == b.version && a.rateIndex == b.rateIndex;
}

uint32_t ReadBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

uint32_t ReadLE32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// End of the audio data: drops a trailing ID3v1 tag and an APEv2 tag (which may sit before it).
size_t AudioEnd(const uint8_t* data, size_t size, size_t start) {
    size_t end = size;
    if (end - start >= 128 && std::memcmp(data + end - 128, "TAG", 3) == 0) end -= 128;
    if (end - start >= 32 && std::memcmp(data + end - 32, "APETAGEX", 8) == 0) {
        const uint64_t tag = ReadLE32(data + end - 32 + 12);  // items + footer
        const uint64_t withHeader = tag + ((ReadLE32(data + end - 32 + 20) & 0x80000000u) ? 32 : 0);
        if (tag >= 32 && withHeader <= end - start) end -= static_cast<size_t>(withHeader);
    }
    return end;
}

// A header at `pos` that the next frame confirms (or that ends exactly at `end`).
bool ConfirmedFrame(const uint8_t* data, size_t pos, size_t end, FrameHeader& h) {
    if (end - pos < 4 || !ParseHeader(data + pos, h)) return false;
    if (static_cast<size_t>(h.bytes) > end - pos) return false;
    const size_t next = pos + static_cast<size_t>(h.bytes);
    if (next == end) return true;
    FrameHeader n;
    return end - next >= 4 && ParseHeader(data + next, n) && SameStream(h, n);
}

// Where ffmpeg starts decoding: the first frame (within 64 KiB) whose successor has the same
// sync, version, layer, rate, channel mode and copyright/original/emphasis bits. Anything
// before it is skipped as junk, such as a metadata frame not taken as a tag whose header
// differs from the audio frames'. Without such a pair, decoding starts at `pos`.
size_t FirstAudioFrame(const uint8_t* data, size_t pos, size_t end) {
    constexpr uint32_t kSameHeader = 0xFFFE0CCFu;
    const size_t limit = std::min(end, pos + 64 * 1024);
    for (size_t i = pos; i < limit && end - i >= 4; ++i) {
        FrameHeader h, n;
        if (!ParseHeader(data + i, h) || static_cast<size_t>(h.bytes) > end - i) continue;
        const size_t next = i + static_cast<size_t>(h.bytes);
        if (end - next >= 4 && ParseHeader(data + next, n) &&
            (ReadBE32(data + i) & kSameHeader) == (ReadBE32(data + next) & kSameHeader))
            return i;
    }
    return pos;
}

} // namespace

bool IsMp3Stream(const uint8_t* data, size_t size) {
    if (!data) return false;
    const size_t start = SkipId3v2(data, size);
    if (start >= size) return false;
    FrameHeader h;
    return ConfirmedFrame(data, start, AudioEnd(data, size, start), h);
}

class Mp3Source final : public SourceDecoder {
public:
    bool Open(const uint8_t* data, size_t size) override {
        if (!data || size > 0xFFFFFFFFu) return false;  // frame offsets are 32-bit
        data_ = data;
        const size_t start = SkipId3v2(data, size);
        if (start >= size) return false;
        const size_t end = AudioEnd(data, size, start);
        FrameHeader first;
        if (!ConfirmedFrame(data, start, end, first)) return false;

        const InfoTag tag = ParseInfoTag(data + start, first, size - (start + 4));
        const size_t audio = FirstAudioFrame(data, tag.isTagFrame ? start + static_cast<size_t>(first.bytes) : start, end);
        FrameHeader lead;
        if (end - audio < 4 || !ParseHeader(data + audio, lead)) return false;
        BuildIndex(lead, audio, end);
        if (frames_.empty()) return false;

        spf_ = lead.samples;
        const int64_t raw = static_cast<int64_t>(frames_.size()) * spf_;
        delay_ = tag.lame ? tag.startPad + kDecoderDelay : 0;
        int64_t rawEnd = raw;
        if (tag.lame && tag.frames > 0) {
            // ffmpeg drops [frames * spf - padding + 529, frames * spf); a stream longer than
            // its tag says is left untrimmed rather than cut in the middle.
            const int64_t tagEnd = static_cast<int64_t>(tag.frames) * spf_;
            const int64_t firstDiscard = tagEnd - tag.endPad + kDecoderDelay;
            if (raw <= tagEnd && firstDiscard < raw) rawEnd = std::max<int64_t>(firstDiscard, 0);
        }
        if (rawEnd <= delay_) return false;

        fmt_.sampleRate = lead.sampleRate;
        fmt_.channels = lead.channels;
        fmt_.totalFrames = rawEnd - delay_;
        return Seek(0);
    }

    SourceFormat Format() const override { return fmt_; }

    int Read(float* const* planar, int frames) override {
        if (!primed_) Prime();
        const int ch = fmt_.channels;
        int done = 0;
        while (done < frames && pos_ < fmt_.totalFrames) {
            if (bufPos_ >= spf_) {
                if (next_ >= frames_.size()) break;
                DecodeFrame(next_++, pcm_);
                bufPos_ = skip_;
                skip_ = 0;
                continue;
            }
            const int n = static_cast<int>(std::min<int64_t>(
                std::min(spf_ - bufPos_, frames - done), fmt_.totalFrames - pos_));
            const float* s = pcm_ + static_cast<size_t>(bufPos_) * ch;
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < ch; ++c) planar[c][done + i] = *s++;
            bufPos_ += n;
            done += n;
            pos_ += n;
        }
        return done;
    }

    bool Seek(int64_t frame) override {
        if (frames_.empty()) return false;
        pos_ = std::max<int64_t>(0, std::min(frame, fmt_.totalFrames));
        const int64_t internal = pos_ + delay_;
        const size_t k = static_cast<size_t>(internal / spf_);
        bufPos_ = spf_;  // nothing buffered
        skip_ = static_cast<int>(internal % spf_);
        next_ = k;
        primed_ = false;  // the decoder state is rebuilt on the next Read
        return true;
    }

private:
    // Readies the decoder to produce frame next_ as a linear decode would.
    void Prime() {
        primed_ = true;
        drmp3dec_init(&dec_);
        const size_t k = next_;
        if (k == 0 || k >= frames_.size()) return;

        // Frame k's output depends on the overlap-add and synthesis state left by the frames
        // before it, and its main data may start up to 511 bytes back (bit reservoir). Decode
        // k-2 and k-1 fully (an MPEG-2 frame is a single granule, so k-1 needs k-2), and feed
        // earlier frames through the reservoir only, until it holds 511 bytes of main data.
        const size_t full = std::min<size_t>(k, 2);
        size_t s = k - full;
        int need = kMaxReservoir;
        while (s > 0 && need > 0) {
            --s;
            FrameHeader h;
            ParseHeader(data_ + frames_[s], h);
            need -= h.bytes - h.sideInfoEnd;
        }
        for (size_t j = s; j < k; ++j) DecodeFrame(j, j + full >= k ? pcm_ : nullptr);
    }

    struct InfoTag {
        bool isTagFrame = false;  // the first frame is metadata, not audio
        bool lame = false;        // LAME extension with delay/padding
        uint32_t frames = 0;      // frame count from the tag (0 = none)
        int startPad = 0;
        int endPad = 0;
    };

    // Xing/Info (LAME or ffmpeg) or VBRI tag in the first frame, read the way ffmpeg reads it:
    // the frame counts as a tag (not audio) only when the tag carries a frame count or size,
    // and only the LAME, Lavf and Lavc encoder strings enable gapless trimming.
    static InfoTag ParseInfoTag(const uint8_t* f, const FrameHeader& h, size_t bytesAfterHeader) {
        InfoTag tag;
        const size_t fb = static_cast<size_t>(h.bytes);
        // ffmpeg looks right after the side info as if there were no CRC.
        size_t x = static_cast<size_t>(h.sideInfoEnd) - ((f[1] & 1) ? 0 : 2);
        if (x + 8 <= fb && (std::memcmp(f + x, "Xing", 4) == 0 || std::memcmp(f + x, "Info", 4) == 0)) {
            const uint32_t flags = ReadBE32(f + x + 4);
            x += 8;
            uint32_t bytes = 0;
            if (flags & 1) {
                if (x + 4 > fb) return tag;
                tag.frames = ReadBE32(f + x);
                x += 4;
            }
            if (flags & 2) {
                if (x + 4 > fb) return tag;
                bytes = ReadBE32(f + x);
                x += 4;
            }
            if (flags & 4) x += 100;  // TOC
            if (flags & 8) x += 4;    // quality
            tag.isTagFrame = tag.frames != 0 || bytes != 0;
            // A file much longer than the tag's byte size is a concatenation: ignore the count.
            if (bytes != 0 && bytesAfterHeader > bytes && bytesAfterHeader - bytes > (bytes >> 4)) tag.frames = 0;
            // LAME extension: 9-byte encoder string, 12 bytes, then 12-bit delay and padding.
            if (x + 24 <= fb && (std::memcmp(f + x, "LAME", 4) == 0 || std::memcmp(f + x, "Lavf", 4) == 0 ||
                                 std::memcmp(f + x, "Lavc", 4) == 0)) {
                const uint32_t v = (static_cast<uint32_t>(f[x + 21]) << 16) | (static_cast<uint32_t>(f[x + 22]) << 8) |
                                   f[x + 23];
                tag.lame = true;
                tag.startPad = static_cast<int>(v >> 12);
                tag.endPad = static_cast<int>(v & 0xFFF);
            }
        } else if (4 + 32 + 18 <= fb && std::memcmp(f + 4 + 32, "VBRI", 4) == 0 && f[4 + 32 + 4] == 0 &&
                   f[4 + 32 + 5] == 1) {
            // Fraunhofer VBRI version 1: byte size and frame count, no gapless information.
            tag.isTagFrame = ReadBE32(f + 4 + 32 + 10) != 0 || ReadBE32(f + 4 + 32 + 14) != 0;
        }
        return tag;
    }

    void BuildIndex(const FrameHeader& first, size_t pos, size_t end) {
        frames_.clear();
        frames_.reserve((end - pos) / static_cast<size_t>(first.bytes) + 1);
        while (end - pos >= 4) {
            FrameHeader h;
            if (ParseHeader(data_ + pos, h) && SameStream(first, h)) {
                if (static_cast<size_t>(h.bytes) > end - pos) break;  // truncated last frame
                frames_.push_back(static_cast<uint32_t>(pos));
                pos += static_cast<size_t>(h.bytes);
                continue;
            }
            // Lost sync (junk between frames): continue at the next header a second one confirms.
            size_t q = pos + 1;
            for (; end - q >= 4; ++q) {
                const void* ff = std::memchr(data_ + q, 0xFF, end - q - 3);
                if (!ff) {
                    q = end;
                    break;
                }
                q = static_cast<size_t>(static_cast<const uint8_t*>(ff) - data_);
                if (ConfirmedFrame(data_, q, end, h) && SameStream(first, h)) break;
            }
            if (end - q < 4) break;
            pos = q;
        }
    }

    // Decodes indexed frame `j`; `out` may be null to only advance the bit reservoir.
    // A frame that cannot be decoded (its reservoir is missing) plays as silence so the
    // timeline stays aligned with the index.
    void DecodeFrame(size_t j, float* out) {
        const uint8_t* f = data_ + frames_[j];
        FrameHeader h;
        ParseHeader(f, h);
        drmp3dec_frame_info info{};
        // Exactly one frame: dr_mp3 then never resyncs (which would reset its state).
        const int n = drmp3dec_decode_frame(&dec_, f, h.bytes, out, &info);
        if (!out) return;
        const int ch = fmt_.channels;
        if (n <= 0) {
            std::fill(out, out + static_cast<size_t>(spf_) * ch, 0.0f);
        } else if (info.channels != ch) {
            // Mid-stream mono/stereo switch: convert in place.
            if (ch == 2) {
                for (int i = spf_ - 1; i >= 0; --i) out[2 * i] = out[2 * i + 1] = out[i];
            } else {
                for (int i = 0; i < spf_; ++i) out[i] = 0.5f * (out[2 * i] + out[2 * i + 1]);
            }
        }
    }

    const uint8_t* data_ = nullptr;
    std::vector<uint32_t> frames_;  // byte offset of every audio frame
    SourceFormat fmt_{};
    int spf_ = 1152;
    int64_t delay_ = 0;             // samples dropped from the start (gapless)

    drmp3dec dec_{};
    float pcm_[DRMP3_MAX_SAMPLES_PER_FRAME] = {};
    size_t next_ = 0;               // next frame to decode
    bool primed_ = false;           // dec_ is ready to decode next_
    int bufPos_ = 0;                // next sample in pcm_ (spf_ = empty)
    int skip_ = 0;                  // samples to drop from the next decoded frame
    int64_t pos_ = 0;
};

std::unique_ptr<SourceDecoder> CreateMp3Source() { return std::make_unique<Mp3Source>(); }

} // namespace una
