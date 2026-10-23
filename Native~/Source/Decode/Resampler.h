#pragma once
// Polyphase windowed-sinc sample-rate converter (worker threads only).
//
// Output frame k corresponds exactly to input position k * src / dst, computed
// with integer arithmetic, so any output frame can be reached by Reset(k)
// without drift — this is what makes resampled clips seekable sample-exactly.
//
// Ratios whose reduced numerator L = dst/gcd fits kMaxExactPhases get an
// exact per-phase filter table (44.1k→48k needs 160 phases); other ratios
// interpolate between kInterpPhases precomputed phases.

#include <cstdint>
#include <vector>

namespace una {

class Resampler {
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kMaxExactPhases = 2048;
    static constexpr int kInterpPhases = 512;

    bool Init(int srcRate, int dstRate, int channels);

    /// Positions the converter so the next produced frame is `outFrame`.
    /// Returns the first input frame the caller must Push from (>= 0).
    int64_t Reset(int64_t outFrame);

    /// Appends input frames (planar).
    void Push(const float* const* in, int frames);
    void PushSilence(int frames);

    /// Writes up to maxFrames output frames; stops when more input is needed.
    int Produce(float* const* out, int maxFrames);

    /// Number of output frames a source of `inFrames` frames maps to.
    int64_t OutputLength(int64_t inFrames) const;

    int Taps() const { return taps_; }
    int64_t OutputPosition() const { return outPos_; }
    /// Input frames the next Produce needs beyond what is buffered.
    int InputDeficit() const;

private:
    void EnsureCapacity(int extra);
    void Compact();
    const float* PhaseCoefs(int64_t phase, float* scratch) const;

    int channels_ = 0;
    int64_t L_ = 1, M_ = 1;       // out/in = L/M (reduced)
    int taps_ = 0, half_ = 0;
    bool exact_ = true;
    std::vector<float> table_;    // [phases][taps]

    std::vector<float> fifo_[kMaxChannels];
    int64_t fifoStart_ = 0;       // absolute input index of fifo[0]
    int fifoCount_ = 0;
    int64_t outPos_ = 0;
    mutable std::vector<float> lerpScratch_;
};

} // namespace una
