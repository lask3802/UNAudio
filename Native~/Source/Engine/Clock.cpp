#include "Clock.h"

#include <algorithm>
#include <cmath>

namespace una {

namespace {
constexpr double kTwoPi = 6.283185307179586;
}

void Clock::Reset(int sampleRate, int bufferFrames, bool newSession) {
    if (newSession) {
        callbacks_ = 0;
        discontinuities_ = 0;
        dllFrame_ = 0;
        dllNs_ = 0.0;
        pMixFrame_.store(0, std::memory_order_relaxed);
    }
    ++generation_;
    settle_ = 0;
    sampleRate_ = sampleRate;
    bufferFrames_ = bufferFrames;
    periodFrames_ = bufferFrames;
    nominalNsPerFrame_ = sampleRate > 0 ? 1e9 / sampleRate : 0.0;
    init_ = false;
    dllNsPerFrame_ = nominalNsPerFrame_;
    Publish();
}

void Clock::OnCallback(int64_t firstFrame, int frames, int64_t presentNs, int64_t nowNs,
                       int64_t fallbackLatencyNs) {
    ++callbacks_;
    ++settle_;
    if (frames > 0 && frames != bufferFrames_) bufferFrames_ = frames;
    const double t = static_cast<double>(presentNs > 0 ? presentNs : nowNs + fallbackLatencyNs);

    if (!init_) {
        init_ = true;
        dllFrame_ = firstFrame;
        dllNs_ = t;
        dllNsPerFrame_ = nominalNsPerFrame_;
        Publish();
        return;
    }

    const int64_t df = firstFrame - dllFrame_;
    if (df <= 0) {
        Publish();
        return;
    }
    const double pred = dllNs_ + static_cast<double>(df) * dllNsPerFrame_;
    const double e = t - pred;
    const double periodNs = static_cast<double>(df) * nominalNsPerFrame_;
    // A few tiny callbacks (legacy AAudio sends 2-frame ones after a start) carry as much
    // timestamp jitter as full ones, so the tolerance never drops below a nominal period's.
    const double tolerance =
        std::max(8e6, 3.0 * static_cast<double>(std::max<int64_t>(df, periodFrames_)) * nominalNsPerFrame_);

    if (std::fabs(e) > tolerance) {
        // Device restart, xrun or a jump in reported latency: re-anchor.
        ++discontinuities_;
        settle_ = 0;
        dllFrame_ = firstFrame;
        dllNs_ = t;
        dllNsPerFrame_ = nominalNsPerFrame_;
        Publish();
        return;
    }

    // Wider loop bandwidth while settling, then narrow for smoothness.
    const double bandwidthHz = settle_ < 200 ? 2.0 : 0.4;
    const double w = kTwoPi * bandwidthHz * periodNs * 1e-9;
    const double b = std::sqrt(2.0) * w;
    const double c = w * w;

    dllNs_ = pred + b * e;
    dllNsPerFrame_ += c * e / static_cast<double>(df);
    const double lo = nominalNsPerFrame_ * 0.98, hi = nominalNsPerFrame_ * 1.02;
    dllNsPerFrame_ = std::min(hi, std::max(lo, dllNsPerFrame_));
    dllFrame_ = firstFrame;
    Publish();
}

void Clock::PublishMixFrame(int64_t mixFrame) { pMixFrame_.store(mixFrame, std::memory_order_release); }

void Clock::Publish() {
    const uint32_t s = seq_.load(std::memory_order_relaxed);
    seq_.store(s + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    pAnchorFrame_.store(dllFrame_, std::memory_order_relaxed);
    pAnchorNs_.store(static_cast<int64_t>(std::llround(dllNs_)), std::memory_order_relaxed);
    pNsPerFrame_.store(dllNsPerFrame_, std::memory_order_relaxed);
    pCallbacks_.store(callbacks_, std::memory_order_relaxed);
    pDiscontinuities_.store(discontinuities_, std::memory_order_relaxed);
    pSampleRate_.store(sampleRate_, std::memory_order_relaxed);
    pBufferFrames_.store(bufferFrames_, std::memory_order_relaxed);
    pGeneration_.store(generation_, std::memory_order_relaxed);
    seq_.store(s + 2, std::memory_order_release);
}

ClockSnapshot Clock::Read() const {
    ClockSnapshot s;
    for (;;) {
        const uint32_t s0 = seq_.load(std::memory_order_acquire);
        if (s0 & 1u) continue;
        s.anchorFrame = pAnchorFrame_.load(std::memory_order_relaxed);
        s.anchorNs = pAnchorNs_.load(std::memory_order_relaxed);
        s.nsPerFrame = pNsPerFrame_.load(std::memory_order_relaxed);
        s.callbacks = pCallbacks_.load(std::memory_order_relaxed);
        s.discontinuities = pDiscontinuities_.load(std::memory_order_relaxed);
        s.sampleRate = pSampleRate_.load(std::memory_order_relaxed);
        s.bufferFrames = pBufferFrames_.load(std::memory_order_relaxed);
        s.generation = pGeneration_.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq_.load(std::memory_order_relaxed) == s0) break;
    }
    s.mixFrame = pMixFrame_.load(std::memory_order_acquire);
    s.valid = s.callbacks > 0 && s.nsPerFrame > 0.0;
    return s;
}

double Clock::AudibleAt(const ClockSnapshot& s, int64_t hostNs) {
    if (!s.valid) return 0.0;
    return static_cast<double>(s.anchorFrame) +
           static_cast<double>(hostNs - s.anchorNs) / s.nsPerFrame;
}

double Clock::HostNsOfFrame(const ClockSnapshot& s, double frame) {
    if (!s.valid) return 0.0;
    return static_cast<double>(s.anchorNs) + (frame - static_cast<double>(s.anchorFrame)) * s.nsPerFrame;
}

double Clock::AudibleNow(int64_t nowNs) {
    const ClockSnapshot s = Read();
    if (!s.valid) return 0.0;
    double f = AudibleAt(s, nowNs);
    if (f > static_cast<double>(s.mixFrame)) f = static_cast<double>(s.mixFrame);

    // Monotonic within a clock epoch; a discontinuity or reset may legitimately jump back.
    // Ordered: generation never rewinds and discontinuities only grow within one.
    const int64_t epoch = (s.generation << 32) + (s.discontinuities & 0xFFFFFFFFLL);
    std::lock_guard<std::mutex> lock(lastMutex_);
    if (epoch < lastAudibleEpoch_) return f;  // snapshot read before a newer epoch was published
    if (epoch > lastAudibleEpoch_ || f > lastAudible_) {
        lastAudibleEpoch_ = epoch;
        lastAudible_ = f;
    }
    return lastAudible_;
}

} // namespace una
