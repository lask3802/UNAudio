#pragma once
// Per-voice streaming decode for UNA_LOAD_STREAM clips.
//
// The StreamService thread (producer) decodes into a ring of fixed blocks; the
// audio thread (consumer) reads frames in playback order. Loop wrapping is
// done by the producer, so the ring always holds frames in the order they
// will be heard. Every block carries the stream epoch (bumped on seek) and
// its position in "stream frames" — frames since the epoch began, wraps
// included — which lets the consumer skip data that arrived too late instead
// of drifting out of sync.

#include "../Decode/DecodePipeline.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace una {

struct Clip;

struct StreamBlock {
    uint32_t epoch;
    int32_t frames;
    int64_t streamPos;
    int64_t clipPos;   // clip frame of data[0]; a block never spans a loop wrap
    float data[1024 * 2];
};

class Stream {
public:
    static constexpr int kBlockFrames = 1024;
    static constexpr int kBlocks = 16;

    Stream(Clip* clip, int targetRate, int64_t startFrame, bool loop, int64_t loopStart,
           int64_t loopEnd);

    // ── Producer (stream service thread) ─────────────────────────
    /// Returns true if any block was produced.
    bool Refill();

    // ── Consumer (audio thread) ──────────────────────────────────
    /// Data (or the end) exists for the current epoch, so a voice waiting for it may start.
    bool Primed() const {
        return failed_.load(std::memory_order_acquire) ||
               primedEpoch_.load(std::memory_order_acquire) == consEpoch_;
    }
    /// Decoding failed and every frame produced before the failure has been played.
    bool Failed();
    /// una_result describing the failure (valid once Failed()).
    int Error() const { return error_.load(std::memory_order_acquire); }
    /// Clip frame at the consumer cursor, or -1 when the ring has no data there yet.
    /// The mixer compares it with the voice position to detect a ring laid out for
    /// other loop settings (see Mixer.cpp).
    int64_t CursorClipPos();
    /// The producer reached the end of its data for the current epoch.
    bool EndedInEpoch() const { return endEpoch_.load(std::memory_order_acquire) == consEpoch_; }
    /// Copies `frames` frames starting at the consumer cursor (without
    /// consuming) into interleaved dst; missing frames are zero.
    /// Returns frames that were actually available.
    int Peek(float* dst, int frames, int channels);
    /// Advances the consumer cursor; returns true if a block was released.
    bool Consume(int64_t frames);
    /// Restart at `frame` (seek). Data already queued is discarded lazily.
    void RequestSeek(int64_t frame);
    void SetLoop(bool loop, int64_t loopStart, int64_t loopEnd);

    std::atomic<bool> dead{false};
    Clip* clip() const { return clip_; }

private:
    void SkipStale();
    bool SeekTo(int64_t frame);
    void Fail(int error);

    // Holds a clip reference for the stream's whole lifetime. The last owner of the
    // stream may be the stream thread mid-Refill, so the voice slot's own reference
    // is not enough to keep clip->src alive. Declared before pipe_ so the decoder is
    // destroyed first.
    struct ClipRef {
        Clip* clip;
        explicit ClipRef(Clip* c);
        ~ClipRef();
        ClipRef(const ClipRef&) = delete;
        ClipRef& operator=(const ClipRef&) = delete;
    };

    Clip* clip_;
    ClipRef clipRef_;
    int targetRate_;
    DecodePipeline pipe_;
    bool opened_ = false;

    std::unique_ptr<StreamBlock[]> blocks_;
    alignas(64) std::atomic<uint64_t> head_{0};   // producer
    alignas(64) std::atomic<uint64_t> tail_{0};   // consumer

    // consumer → producer
    std::atomic<uint32_t> reqEpoch_{1};
    std::atomic<int64_t> reqFrame_{0};
    std::atomic<int> loop_{0};
    std::atomic<int64_t> loopStart_{0};
    std::atomic<int64_t> loopEnd_{0};

    // producer-local
    uint32_t prodEpoch_ = 0;
    int64_t prodPos_ = 0;        // clip frame
    int64_t prodStreamPos_ = 0;  // stream frame
    bool prodEnd_ = false;
    float planar_[2][kBlockFrames];

    // consumer-local
    uint32_t consEpoch_ = 1;
    int64_t consStreamPos_ = 0;

    std::atomic<uint32_t> primedEpoch_{0};  // last epoch with data or an end
    std::atomic<uint32_t> endEpoch_{0};     // last epoch the producer finished
    std::atomic<bool> failed_{false};
    std::atomic<int32_t> error_{0};         // written before failed_ is set
};

} // namespace una
