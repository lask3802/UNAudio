#pragma once
// Output clock: maps dsp frames to the host time they reach the speaker.
//
// Each callback reports (first dsp frame of the buffer, host ns at which that
// frame is presented). Raw presentation estimates jitter with scheduling, so
// they feed a second-order delay-locked loop (Adriaensen, "Using a DLL to
// filter time") that yields a smooth anchor + measured ns-per-frame. Readers
// extrapolate from the anchor, which gives sub-sample interpolation between
// callbacks and tracks device clock drift against the host clock.

#include <atomic>
#include <cstdint>
#include <mutex>

namespace una {

struct ClockSnapshot {
    int64_t anchorFrame = 0;
    int64_t anchorNs = 0;
    double nsPerFrame = 0.0;
    int64_t mixFrame = 0;      // frames rendered so far (next frame to mix)
    int32_t sampleRate = 0;
    int32_t bufferFrames = 0;
    int64_t callbacks = 0;
    int64_t discontinuities = 0;
    int64_t generation = 0;    // changes on engine restart or device change
    bool valid = false;
};

class Clock {
public:
    /// newSession: engine (re)initialised, so counters and the mix head restart at 0.
    /// Otherwise (device change) dsp frames continue and only the DLL re-anchors.
    void Reset(int sampleRate, int bufferFrames, bool newSession = true);

    /// Audio thread, once per callback, before mixing.
    /// presentNs: host time the buffer's first frame is heard (0 = unknown,
    /// then nowNs + fallbackLatencyNs is used).
    void OnCallback(int64_t firstFrame, int frames, int64_t presentNs, int64_t nowNs,
                    int64_t fallbackLatencyNs);
    /// Audio thread, after mixing.
    void PublishMixFrame(int64_t mixFrame);

    ClockSnapshot Read() const;

    /// Audible dsp frame at hostNs (not clamped).
    static double AudibleAt(const ClockSnapshot& s, int64_t hostNs);
    /// Host ns at which dsp frame `frame` is heard.
    static double HostNsOfFrame(const ClockSnapshot& s, double frame);

    /// Audible frame "now", monotonic across calls and never past mixFrame.
    double AudibleNow(int64_t nowNs);

private:
    void Publish();

    // DLL state (audio thread only)
    bool init_ = false;
    int64_t dllFrame_ = 0;
    double dllNs_ = 0.0;
    double dllNsPerFrame_ = 0.0;
    double nominalNsPerFrame_ = 0.0;
    int64_t callbacks_ = 0;
    int64_t settle_ = 0;           // callbacks since the last (re)anchor
    int64_t discontinuities_ = 0;
    int64_t generation_ = 0;       // bumped by every Reset; never rewinds
    int32_t sampleRate_ = 0;
    int32_t bufferFrames_ = 0;     // last callback's size
    int32_t periodFrames_ = 0;     // nominal device period from Reset

    // Published (seqlock)
    std::atomic<uint32_t> seq_{0};
    std::atomic<int64_t> pAnchorFrame_{0};
    std::atomic<int64_t> pAnchorNs_{0};
    std::atomic<double> pNsPerFrame_{0.0};
    std::atomic<int64_t> pCallbacks_{0};
    std::atomic<int64_t> pDiscontinuities_{0};
    std::atomic<int32_t> pSampleRate_{0};
    std::atomic<int32_t> pBufferFrames_{0};
    std::atomic<int64_t> pGeneration_{0};
    std::atomic<int64_t> pMixFrame_{0};

    // AudibleNow is control-side only; the lock keeps (epoch, value) consistent.
    std::mutex lastMutex_;
    double lastAudible_ = 0.0;
    int64_t lastAudibleEpoch_ = 0;
};

} // namespace una
