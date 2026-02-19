#ifndef UNAUDIO_AUDIO_CLOCK_H
#define UNAUDIO_AUDIO_CLOCK_H

/*
 * Lock-free DSP clock for precise audio time reporting.
 *
 * The audio thread advances the clock at the END of each callback.
 * The main thread reads the (dspFrames, wallClockTimestamp) pair and
 * interpolates using steady_clock to achieve sub-callback precision.
 *
 * Typical jitter: < 0.5 ms on desktop, < 1–2 ms on mobile.
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <algorithm>

namespace una {

struct AudioClock {
    /// Total output frames processed (audio thread writes, main thread reads).
    std::atomic<int64_t> dspFrames{0};

    /// steady_clock timestamp (nanoseconds) at last callback end.
    std::atomic<int64_t> timestampNs{0};

    /// Output sample rate — set once at Initialize().
    int32_t sampleRate = 0;

    /// Buffer size in frames — used to clamp interpolation.
    int32_t bufferSize = 0;

    // ── Audio thread ─────────────────────────────────────────────

    /// Call at the END of each AudioCallback to advance the clock.
    /// @param framesProcessed  Number of frames written to the output buffer.
    void advance(int framesProcessed) {
        dspFrames.fetch_add(framesProcessed, std::memory_order_relaxed);
        auto ns = std::chrono::steady_clock::now().time_since_epoch();
        timestampNs.store(
            std::chrono::duration_cast<std::chrono::nanoseconds>(ns).count(),
            std::memory_order_release);
    }

    // ── Main thread ──────────────────────────────────────────────

    /// Returns the interpolated DSP time in seconds.
    /// Between callbacks, the time smoothly advances based on wall-clock
    /// elapsed time, clamped to avoid runaway extrapolation.
    double getTimeSeconds() const {
        if (sampleRate <= 0) return 0.0;

        int64_t frames = dspFrames.load(std::memory_order_relaxed);
        int64_t ts = timestampNs.load(std::memory_order_acquire);
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
        return dspFrames.load(std::memory_order_relaxed);
    }

    void reset() {
        dspFrames.store(0, std::memory_order_relaxed);
        timestampNs.store(0, std::memory_order_relaxed);
    }
};

} // namespace una

#endif // UNAUDIO_AUDIO_CLOCK_H
