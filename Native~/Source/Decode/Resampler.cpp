#include "Resampler.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace una {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kCutoff = 0.92;   // fraction of the lower Nyquist kept
constexpr double kKaiserBeta = 8.0;

double BesselI0(double x) {
    double sum = 1.0, term = 1.0;
    const double q = x * x * 0.25;
    for (int k = 1; k < 64; ++k) {
        term *= q / (static_cast<double>(k) * k);
        sum += term;
        if (term < sum * 1e-12) break;
    }
    return sum;
}

void BuildPhase(float* dst, int taps, int half, double frac, double fc, double i0beta) {
    double sum = 0.0;
    for (int j = 0; j < taps; ++j) {
        const double d = static_cast<double>(j - half + 1) - frac;
        const double x = d / static_cast<double>(half);
        double w = 0.0;
        if (std::fabs(x) < 1.0) w = BesselI0(kKaiserBeta * std::sqrt(1.0 - x * x)) / i0beta;
        const double a = kPi * fc * d;
        const double s = std::fabs(a) < 1e-12 ? 1.0 : std::sin(a) / a;
        const double h = fc * s * w;
        dst[j] = static_cast<float>(h);
        sum += h;
    }
    if (sum != 0.0) {
        const float inv = static_cast<float>(1.0 / sum);
        for (int j = 0; j < taps; ++j) dst[j] *= inv;
    }
}

} // namespace

bool Resampler::Init(int srcRate, int dstRate, int channels) {
    if (srcRate <= 0 || dstRate <= 0 || channels < 1 || channels > kMaxChannels) return false;
    channels_ = channels;
    const int64_t g = std::gcd(static_cast<int64_t>(srcRate), static_cast<int64_t>(dstRate));
    L_ = dstRate / g;
    M_ = srcRate / g;

    const double ratio = static_cast<double>(dstRate) / srcRate;
    const double fc = std::min(1.0, ratio) * kCutoff;
    int taps = 32;
    if (ratio < 1.0) taps = static_cast<int>(std::ceil(32.0 / ratio));
    taps = std::min(160, (taps + 1) & ~1);
    taps_ = taps;
    half_ = taps / 2;

    exact_ = L_ <= kMaxExactPhases;
    const int phases = exact_ ? static_cast<int>(L_) : kInterpPhases + 1;
    table_.assign(static_cast<size_t>(phases) * taps_, 0.0f);
    const double i0b = BesselI0(kKaiserBeta);
    for (int p = 0; p < phases; ++p) {
        const double frac = exact_ ? static_cast<double>(p) / static_cast<double>(L_)
                                   : static_cast<double>(p) / kInterpPhases;
        BuildPhase(&table_[static_cast<size_t>(p) * taps_], taps_, half_, frac, fc, i0b);
    }
    lerpScratch_.assign(taps_, 0.0f);

    for (int c = 0; c < channels_; ++c) fifo_[c].assign(8192 + 2 * taps_, 0.0f);
    Reset(0);
    return true;
}

int64_t Resampler::OutputLength(int64_t inFrames) const {
    // ceil(inFrames * L / M)
    return (inFrames * L_ + M_ - 1) / M_;
}

int64_t Resampler::Reset(int64_t outFrame) {
    outPos_ = std::max<int64_t>(0, outFrame);
    const int64_t ipos = outPos_ * M_ / L_;
    const int64_t first = ipos - half_ + 1;
    fifoStart_ = first;
    fifoCount_ = 0;
    if (first < 0) {
        // Input before the start of the stream is silence.
        PushSilence(static_cast<int>(-first));
        return 0;
    }
    return first;
}

void Resampler::EnsureCapacity(int extra) {
    const size_t need = static_cast<size_t>(fifoCount_) + static_cast<size_t>(extra);
    if (need <= fifo_[0].size()) return;
    Compact();
    if (need <= fifo_[0].size()) return;
    for (int c = 0; c < channels_; ++c) fifo_[c].resize(need * 2);
}

void Resampler::Compact() {
    // Drop input no longer reachable by the next output frame.
    const int64_t ipos = outPos_ * M_ / L_;
    const int64_t first = ipos - half_ + 1;
    const int64_t drop = std::min<int64_t>(std::max<int64_t>(0, first - fifoStart_), fifoCount_);
    if (drop <= 0) return;
    const int keep = fifoCount_ - static_cast<int>(drop);
    for (int c = 0; c < channels_; ++c) {
        float* f = fifo_[c].data();
        std::memmove(f, f + drop, static_cast<size_t>(keep) * sizeof(float));
    }
    fifoStart_ += drop;
    fifoCount_ = keep;
}

void Resampler::Push(const float* const* in, int frames) {
    if (frames <= 0) return;
    EnsureCapacity(frames);
    for (int c = 0; c < channels_; ++c)
        std::memcpy(fifo_[c].data() + fifoCount_, in[c], static_cast<size_t>(frames) * sizeof(float));
    fifoCount_ += frames;
}

void Resampler::PushSilence(int frames) {
    if (frames <= 0) return;
    EnsureCapacity(frames);
    for (int c = 0; c < channels_; ++c)
        std::memset(fifo_[c].data() + fifoCount_, 0, static_cast<size_t>(frames) * sizeof(float));
    fifoCount_ += frames;
}

int Resampler::InputDeficit() const {
    const int64_t ipos = outPos_ * M_ / L_;
    const int64_t last = ipos + half_;
    const int64_t have = fifoStart_ + fifoCount_;
    return last < have ? 0 : static_cast<int>(last - have + 1);
}

const float* Resampler::PhaseCoefs(int64_t phase, float* scratch) const {
    if (exact_) return &table_[static_cast<size_t>(phase) * taps_];
    const double p = static_cast<double>(phase) / static_cast<double>(L_) * kInterpPhases;
    int i0 = static_cast<int>(p);
    if (i0 >= kInterpPhases) i0 = kInterpPhases - 1;
    const float t = static_cast<float>(p - i0);
    const float* a = &table_[static_cast<size_t>(i0) * taps_];
    const float* b = a + taps_;
    for (int j = 0; j < taps_; ++j) scratch[j] = a[j] + (b[j] - a[j]) * t;
    return scratch;
}

int Resampler::Produce(float* const* out, int maxFrames) {
    int n = 0;
    const int64_t have = fifoStart_ + fifoCount_;
    while (n < maxFrames) {
        const int64_t num = outPos_ * M_;
        const int64_t ipos = num / L_;
        const int64_t phase = num % L_;
        const int64_t first = ipos - half_ + 1;
        if (ipos + half_ >= have) break;
        const float* h = PhaseCoefs(phase, lerpScratch_.data());
        const size_t idx = static_cast<size_t>(first - fifoStart_);
        for (int c = 0; c < channels_; ++c) {
            const float* x = fifo_[c].data() + idx;
            float acc = 0.0f;
            for (int j = 0; j < taps_; ++j) acc += h[j] * x[j];
            out[c][n] = acc;
        }
        ++outPos_;
        ++n;
    }
    if (fifoCount_ > static_cast<int>(fifo_[0].size() / 2)) Compact();
    return n;
}

} // namespace una
