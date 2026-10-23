#include "Engine/Clock.h"

#include <gtest/gtest.h>

#include <cmath>
#include <random>

using una::Clock;

namespace {
// Simulates a device whose true presentation time of frame f is
// base + f * truePeriod, reported with callback scheduling jitter.
struct Sim {
    double base = 1e12;
    double nsPerFrame;
    int buffer;
    std::mt19937 rng{1234};
    std::uniform_real_distribution<double> jitter;
    Sim(double rate, int buf, double jitterNs) : nsPerFrame(1e9 / rate), buffer(buf), jitter(-jitterNs, jitterNs) {}
    double TruePresent(double frame) const { return base + frame * nsPerFrame; }
};
} // namespace

TEST(Clock, DllSmoothsJitterAndTracksDrift) {
    // Device runs 300 ppm fast relative to the host clock.
    Sim sim(48000.0 * 1.0003, 256, 1.5e6);
    Clock clock;
    clock.Reset(48000, 256);
    double worst = 0;
    double lastAudible = -1;
    for (int cb = 0; cb < 4000; ++cb) {
        const int64_t first = int64_t(cb) * sim.buffer;
        const double present = sim.TruePresent(double(first));
        const int64_t reported = int64_t(present + sim.jitter(sim.rng));
        clock.OnCallback(first, sim.buffer, reported, reported - 5000000, 0);
        clock.PublishMixFrame(first + sim.buffer);
        if (cb > 1500) {
            // Query in the middle of the period, as a game frame would.
            const double queryNs = present - 2.0e6;
            const auto s = clock.Read();
            const double est = Clock::AudibleAt(s, int64_t(queryNs));
            const double truth = (queryNs - sim.base) / sim.nsPerFrame;
            worst = std::max(worst, std::fabs(est - truth));
            EXPECT_GE(est, lastAudible - 1.0);
            lastAudible = est;
        }
    }
    // ±1.5 ms uniform raw jitter (±72 frames, sigma ~41) must stay under 0.5 ms worst case.
    // A 0.4 Hz loop at 187 updates/s leaves sigma ~6 frames; 2500 samples reach ~3.5 sigma.
    EXPECT_LT(worst, 24.0) << "frames";
    const auto s = clock.Read();
    EXPECT_NEAR(s.nsPerFrame, sim.nsPerFrame, sim.nsPerFrame * 5e-5);
}

TEST(Clock, DiscontinuityReanchors) {
    Clock clock;
    clock.Reset(48000, 480);
    const double p = 1e9 / 48000.0;
    int64_t t = 1000000000;
    for (int cb = 0; cb < 100; ++cb) clock.OnCallback(int64_t(cb) * 480, 480, t + int64_t(cb * 480 * p), t, 0);
    const auto before = clock.Read();
    // Device restarted: same frames now presented 80 ms later.
    clock.OnCallback(100 * 480, 480, t + int64_t(100 * 480 * p) + 80000000, t, 0);
    const auto after = clock.Read();
    EXPECT_EQ(after.discontinuities, before.discontinuities + 1);
    EXPECT_EQ(after.anchorFrame, 100 * 480);
}

TEST(Clock, TinyCallbacksKeepANominalPeriodOfTolerance) {
    Clock clock;
    clock.Reset(44100, 1764);  // legacy AAudio: 40 ms period
    const double p = 1e9 / 44100.0;
    const int64_t t = 1000000000;
    int64_t frame = 0;
    for (int cb = 0; cb < 50; ++cb, frame += 1764) clock.OnCallback(frame, 1764, t + int64_t(frame * p), t, 0);
    const auto before = clock.Read();
    // After a restart the stream delivers a few 2-frame callbacks whose timestamps are 12 ms
    // off: jitter well inside a 40 ms period, not a discontinuity.
    for (int cb = 0; cb < 4; ++cb, frame += 2) clock.OnCallback(frame, 2, t + int64_t(frame * p) + 12000000, t, 0);
    EXPECT_EQ(clock.Read().discontinuities, before.discontinuities);
    // A real jump still re-anchors.
    clock.OnCallback(frame, 1764, t + int64_t(frame * p) + 200000000, t, 0);
    EXPECT_EQ(clock.Read().discontinuities, before.discontinuities + 1);
}

TEST(Clock, AudibleNowIsMonotonicAndBoundedByMix) {
    Clock clock;
    clock.Reset(48000, 256);
    const double p = 1e9 / 48000.0;
    const int64_t base = 5000000000LL;
    double prev = -1e18;  // before frame 0 is heard the audible frame is legitimately negative
    for (int cb = 0; cb < 200; ++cb) {
        const int64_t present = base + int64_t(cb * 256 * p) + ((cb % 3) - 1) * 400000;
        clock.OnCallback(int64_t(cb) * 256, 256, present, present - 10000000, 0);
        clock.PublishMixFrame(int64_t(cb + 1) * 256);
        for (int q = 0; q < 4; ++q) {
            const int64_t now = present - 8000000 + q * 1300000;
            const double a = clock.AudibleNow(now);
            EXPECT_GE(a, prev);
            EXPECT_LE(a, double((cb + 1) * 256));
            prev = a;
        }
    }
}
