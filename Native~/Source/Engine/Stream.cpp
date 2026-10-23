#include "Stream.h"

#include "Clip.h"
#include "unaudio.h"

#include <algorithm>
#include <cstring>

namespace una {

Stream::ClipRef::ClipRef(Clip* c) : clip(c) { clip->refs.fetch_add(1, std::memory_order_relaxed); }

// Dropping to zero here is fine on any thread: only the control thread deletes, in CollectClips.
Stream::ClipRef::~ClipRef() { clip->refs.fetch_sub(1, std::memory_order_acq_rel); }

Stream::Stream(Clip* clip, int targetRate, int64_t startFrame, bool loop, int64_t loopStart,
               int64_t loopEnd)
    : clip_(clip), clipRef_(clip), targetRate_(targetRate), blocks_(new StreamBlock[kBlocks]) {
    reqFrame_.store(startFrame, std::memory_order_relaxed);
    reqEpoch_.store(1, std::memory_order_relaxed);
    consEpoch_ = 1;
    SetLoop(loop, loopStart, loopEnd);
}

bool Stream::Refill() {
    if (failed_.load(std::memory_order_relaxed)) return false;
    if (!opened_) {
        // The clip was probed when it loaded, so this fails only if the bytes changed since
        // (borrowed memory) or memory ran out; this runs on the stream thread, never throw.
        int rc;
        try {
            rc = pipe_.Open(clip_->src, clip_->srcSize, targetRate_, clip_->sourceFrames);
        } catch (...) {
            rc = UNA_E_OUT_OF_MEMORY;
        }
        if (rc != UNA_OK) {
            Fail(rc);
            return false;
        }
        opened_ = true;
    }

    const uint32_t e = reqEpoch_.load(std::memory_order_acquire);
    if (e != prodEpoch_) {
        prodEpoch_ = e;
        prodStreamPos_ = 0;
        prodEnd_ = false;
        if (!SeekTo(reqFrame_.load(std::memory_order_relaxed))) return false;
    }

    bool produced = false;
    const int ch = pipe_.StoredChannels();
    const int64_t total = pipe_.TotalFrames();
    while (!prodEnd_) {
        const uint64_t head = head_.load(std::memory_order_relaxed);
        if (head - tail_.load(std::memory_order_acquire) >= static_cast<uint64_t>(kBlocks)) break;
        if (reqEpoch_.load(std::memory_order_acquire) != prodEpoch_) break;

        const bool loop = loop_.load(std::memory_order_relaxed) != 0;
        const int64_t ls = loopStart_.load(std::memory_order_relaxed);
        int64_t le = loopEnd_.load(std::memory_order_relaxed);
        if (le <= 0 || le > total) le = total;
        const bool loopValid = loop && ls >= 0 && ls < le;
        // At exactly le the loop wraps; only a position past le (seek beyond the region) plays on.
        const int64_t end = (loopValid && prodPos_ <= le) ? le : total;

        if (prodPos_ >= end) {
            if (loopValid) {
                if (!SeekTo(ls)) return produced;
                continue;
            }
            prodEnd_ = true;
            break;
        }

        const int n = static_cast<int>(std::min<int64_t>(kBlockFrames, end - prodPos_));
        float* pl[2] = {planar_[0], planar_[1]};
        int got = pipe_.Read(pl, n);
        bool truncated = false;
        if (pipe_.SourceEndedEarly() && prodPos_ + got >= pipe_.SourceEndFrame()) {
            // Truncated or corrupt data: keep what decoded, drop the silence padding, then fail.
            got = static_cast<int>(std::max<int64_t>(0, pipe_.SourceEndFrame() - prodPos_));
            truncated = true;
        }
        if (got <= 0) {
            prodEnd_ = true;
            if (truncated) Fail(UNA_E_DECODE);
            break;
        }

        StreamBlock& b = blocks_[head % kBlocks];
        b.epoch = prodEpoch_;
        b.frames = got;
        b.streamPos = prodStreamPos_;
        b.clipPos = prodPos_;
        if (ch == 1) {
            std::memcpy(b.data, planar_[0], static_cast<size_t>(got) * sizeof(float));
        } else {
            for (int i = 0; i < got; ++i) {
                b.data[2 * i] = planar_[0][i];
                b.data[2 * i + 1] = planar_[1][i];
            }
        }
        prodPos_ += got;
        prodStreamPos_ += got;
        head_.store(head + 1, std::memory_order_release);
        produced = true;
        primedEpoch_.store(prodEpoch_, std::memory_order_release);
        if (truncated) {
            prodEnd_ = true;
            Fail(UNA_E_DECODE);  // after the last head_ store: Failed() then sees every frame
            break;
        }
    }
    if (prodEnd_) {
        endEpoch_.store(prodEpoch_, std::memory_order_release);
        primedEpoch_.store(prodEpoch_, std::memory_order_release);
    }
    return produced;
}

bool Stream::SeekTo(int64_t frame) {
    // A failed seek leaves the decoder somewhere else (stb_vorbis rewinds to the start);
    // playing on would put audio and the voice position out of step, so stop instead.
    if (!pipe_.Seek(frame)) {
        prodEnd_ = true;
        Fail(UNA_E_DECODE);
        return false;
    }
    prodPos_ = pipe_.Position();
    return true;
}

void Stream::Fail(int error) {
    error_.store(error, std::memory_order_relaxed);
    failed_.store(true, std::memory_order_release);
    // A voice waiting for data must wake up to see the failure.
    primedEpoch_.store(prodEpoch_, std::memory_order_release);
}

void Stream::SkipStale() {
    for (;;) {
        const uint64_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return;
        const StreamBlock& b = blocks_[tail % kBlocks];
        if (b.epoch == consEpoch_ && b.streamPos + b.frames > consStreamPos_) return;
        tail_.store(tail + 1, std::memory_order_release);
    }
}

bool Stream::Failed() {
    if (!failed_.load(std::memory_order_acquire)) return false;
    // Let the frames produced before the failure play out first.
    SkipStale();
    return tail_.load(std::memory_order_relaxed) == head_.load(std::memory_order_acquire);
}

int64_t Stream::CursorClipPos() {
    SkipStale();
    const uint64_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return -1;
    const StreamBlock& b = blocks_[tail % kBlocks];
    if (b.epoch != consEpoch_ || b.streamPos > consStreamPos_) return -1;
    return b.clipPos + (consStreamPos_ - b.streamPos);
}

int Stream::Peek(float* dst, int frames, int channels) {
    SkipStale();
    int copied = 0;
    int available = 0;
    uint64_t idx = tail_.load(std::memory_order_relaxed);
    const uint64_t head = head_.load(std::memory_order_acquire);
    while (copied < frames && idx != head) {
        const StreamBlock& b = blocks_[idx % kBlocks];
        if (b.epoch != consEpoch_) break;
        const int64_t want = consStreamPos_ + copied;
        if (b.streamPos > want) {
            // Gap (should not happen in order, but never read garbage).
            const int gap = static_cast<int>(std::min<int64_t>(b.streamPos - want, frames - copied));
            std::memset(dst + static_cast<size_t>(copied) * channels, 0,
                        static_cast<size_t>(gap) * channels * sizeof(float));
            copied += gap;
            continue;
        }
        const int off = static_cast<int>(want - b.streamPos);
        const int n = std::min(b.frames - off, frames - copied);
        if (n > 0) {
            std::memcpy(dst + static_cast<size_t>(copied) * channels, b.data + static_cast<size_t>(off) * channels,
                        static_cast<size_t>(n) * channels * sizeof(float));
            copied += n;
            available += n;
        }
        ++idx;
    }
    if (copied < frames)
        std::memset(dst + static_cast<size_t>(copied) * channels, 0,
                    static_cast<size_t>(frames - copied) * channels * sizeof(float));
    return available;
}

bool Stream::Consume(int64_t frames) {
    const uint64_t before = tail_.load(std::memory_order_relaxed);
    consStreamPos_ += frames;
    SkipStale();
    return tail_.load(std::memory_order_relaxed) != before;
}

void Stream::RequestSeek(int64_t frame) {
    ++consEpoch_;
    if (consEpoch_ == 0) consEpoch_ = 1;
    consStreamPos_ = 0;
    reqFrame_.store(frame, std::memory_order_relaxed);
    reqEpoch_.store(consEpoch_, std::memory_order_release);
}

void Stream::SetLoop(bool loop, int64_t loopStart, int64_t loopEnd) {
    loopStart_.store(loopStart, std::memory_order_relaxed);
    loopEnd_.store(loopEnd, std::memory_order_relaxed);
    loop_.store(loop ? 1 : 0, std::memory_order_release);
}

} // namespace una
