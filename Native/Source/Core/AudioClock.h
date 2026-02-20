#ifndef UNAUDIO_AUDIO_CLOCK_H
#define UNAUDIO_AUDIO_CLOCK_H

/*
 * Lock-free DSP clock for precise audio time reporting.
 *
 * The audio thread advances the clock at the END of each callback.
 * The main thread reads the (dspFrames, wallClockTimestamp) pair and
 * interpolates using steady_clock to achieve sub-callback precision.
 *
 * Thread safety: uses a SeqLock so the reader always sees a consistent
 * (frames, timestamp) pair — no torn reads, no mutex, no allocation.
 *
 * Typical jitter: < 0.5 ms on desktop, < 1–2 ms on mobile.
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <algorithm>

namespace una {

struct AudioClock {
    /// Output sample rate — set once at Initialize().
    int32_t sampleRate = 0;

    /// Buffer size in frames — used to clamp interpolation.
    int32_t bufferSize = 0;

    // ── Audio thread ─────────────────────────────────────────────

    /// Call at the END of each AudioCallback to advance the clock.
    /// @param framesProcessed  Number of frames written to the output buffer.
    void advance(int framesProcessed) {
        // SeqLock write: odd sequence = write in progress
        uint32_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_release);

        int64_t newFrames = dspFrames_.load(std::memory_order_relaxed) + framesProcessed;
        auto ns = std::chrono::steady_clock::now().time_since_epoch();
        int64_t newTs = std::chrono::duration_cast<std::chrono::nanoseconds>(ns).count();

        dspFrames_.store(newFrames, std::memory_order_relaxed);
        timestampNs_.store(newTs, std::memory_order_relaxed);

        // Even sequence = write complete, reader may proceed
        seq_.store(s + 2, std::memory_order_release);
    }

    // ── Main thread ──────────────────────────────────────────────

    /// Returns the interpolated DSP time in seconds.
    /// Between callbacks, the time smoothly advances based on wall-clock
    /// elapsed time, clamped to avoid runaway extrapolation.
    /// Uses SeqLock to guarantee a consistent (frames, timestamp) pair.
    double getTimeSeconds() const {
        if (sampleRate <= 0) return 0.0;

        int64_t frames;
        int64_t ts;

        // SeqLock read: spin until we get a consistent snapshot
        uint32_t s0;
        do {
            s0 = seq_.load(std::memory_order_acquire);
            frames = dspFrames_.load(std::memory_order_relaxed);
            ts = timestampNs_.load(std::memory_order_relaxed);
        } while ((s0 & 1) || seq_.load(std::memory_order_acquire) != s0);

        double baseTime = static_cast<double>(frames) / sampleRate;

        if (ts == 0) return baseTime;

        // Interpolate using wall-clock elapsed since last callback
        auto nowNs = std::chrono::steady_clock::now().time_since_epoch();
        int64_t nowNsCount =
            std::chrono::duration_cast<std::chrono::nanoseconds>(nowNs).count();
        double elapsed = static_cast<double>(nowNsCount - ts) * 1e-9;

        // Clamp to at most 2 buffer periods to prevent drift on stalls
        double maxElapsed = bufferSize > 0
            ? 2.0 * bufferSize / sampleRate
            : 0.05;
        elapsed = std::min(std::max(elapsed, 0.0), maxElapsed);

        return baseTime + elapsed;
    }

    /// Returns the raw DSP frame count (no interpolation).
    int64_t getFrames() const {
        return dspFrames_.load(std::memory_order_relaxed);
    }

    void reset() {
        seq_.store(0, std::memory_order_relaxed);
        dspFrames_.store(0, std::memory_order_relaxed);
        timestampNs_.store(0, std::memory_order_relaxed);
    }

private:
    std::atomic<uint32_t> seq_{0};          // SeqLock sequence counter
    std::atomic<int64_t>  dspFrames_{0};    // total output frames processed
    std::atomic<int64_t>  timestampNs_{0};  // steady_clock ns at last advance()
};

} // namespace una

#endif // UNAUDIO_AUDIO_CLOCK_H
