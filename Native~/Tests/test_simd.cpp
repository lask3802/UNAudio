// SIMD kernels against their scalar references, over lengths that exercise
// every tail (0, odd, not a multiple of 4) and ramps that end mid-block.

#include "Core/Simd.h"
#include "Engine/MixKernels.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

namespace mix = una::mix;

namespace {

const int kLengths[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 63, 64, 65, 255, 256};

std::vector<float> Random(size_t n, std::mt19937& rng, float lo = -1.0f, float hi = 1.0f) {
    std::uniform_real_distribution<float> d(lo, hi);
    std::vector<float> v(n);
    for (auto& x : v) x = d(rng);
    return v;
}

// x86 has no fused multiply-add here, so SIMD and scalar agree exactly; ARM compilers
// may contract the scalar expressions into FMAs, which moves the last bit.
void ExpectClose(const std::vector<float>& a, const std::vector<float>& b, const char* what, int n) {
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i)
        ASSERT_NEAR(a[i], b[i], 2e-6f * (1.0f + std::fabs(b[i]))) << what << " n=" << n << " i=" << i;
}

} // namespace

TEST(Simd, MainstreamTargetsUseVectorKernels) {
#if !defined(UNA_NO_SIMD) && (defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64))
    EXPECT_STRNE(mix::IsaName(), "scalar");
#endif
    SUCCEED() << "kernels: " << mix::IsaName();
}

TEST(Simd, DirectKernelsMatchScalar) {
    std::mt19937 rng(1);
    for (int n : kLengths) {
        for (int ch = 1; ch <= 2; ++ch) {
            const auto src = Random(static_cast<size_t>(n) * ch + 1, rng);
            const auto base = Random(static_cast<size_t>(n) * 2, rng);
            auto a = base, b = base;
            const float l0 = 0.3f, r0 = 0.9f, dl = 0.0013f, dr = -0.0021f;
            if (ch == 1) {
                mix::Mono(src.data(), n, a.data(), l0, r0, dl, dr);
                mix::scalar::Mono(src.data(), n, b.data(), l0, r0, dl, dr);
            } else {
                mix::Stereo(src.data(), n, a.data(), l0, r0, dl, dr);
                mix::scalar::Stereo(src.data(), n, b.data(), l0, r0, dl, dr);
            }
            ExpectClose(a, b, ch == 1 ? "mono" : "stereo", n);
        }
    }
}

TEST(Simd, CubicKernelsMatchScalar) {
    std::mt19937 rng(2);
    const double rates[] = {0.25, 0.5, 0.999, 1.0, 1.37, 2.0, 3.3, 7.9};
    for (double rate : rates) {
        const uint64_t step = static_cast<uint64_t>(std::llround(rate * 4294967296.0));
        for (int n : kLengths) {
            for (int ch = 1; ch <= 2; ++ch) {
                const uint32_t frac = static_cast<uint32_t>(rng());
                const uint64_t last = n > 0 ? (frac + static_cast<uint64_t>(n - 1) * step) >> 32 : 0;
                const auto src = Random(static_cast<size_t>(last + 4) * ch, rng);
                const auto base = Random(static_cast<size_t>(n) * 2, rng);
                auto a = base, b = base;
                const float l0 = 0.7f, r0 = 0.2f, dl = -0.001f, dr = 0.003f;
                if (ch == 1) {
                    mix::MonoCubic(src.data(), frac, step, n, a.data(), l0, r0, dl, dr);
                    mix::scalar::MonoCubic(src.data(), frac, step, n, b.data(), l0, r0, dl, dr);
                } else {
                    mix::StereoCubic(src.data(), frac, step, n, a.data(), l0, r0, dl, dr);
                    mix::scalar::StereoCubic(src.data(), frac, step, n, b.data(), l0, r0, dl, dr);
                }
                ExpectClose(a, b, ch == 1 ? "mono cubic" : "stereo cubic", n);
            }
        }
    }
}

TEST(Simd, CubicPassesSamplesThroughAtIntegerPositions) {
    // t = 0 must return y1 exactly, so pitch 2 on an integer grid is lossless.
    std::vector<float> src(64);
    for (size_t i = 0; i < src.size(); ++i) src[i] = static_cast<float>(i) * 0.01f;
    std::vector<float> out(2 * 16, 0.0f);
    mix::MonoCubic(src.data(), 0, uint64_t(2) << 32, 16, out.data(), 1.0f, 1.0f, 0.0f, 0.0f);
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(out[2 * i], src[static_cast<size_t>(2 * i + 1)]) << i;
        EXPECT_EQ(out[2 * i + 1], src[static_cast<size_t>(2 * i + 1)]) << i;
    }
}

TEST(Simd, Int16ConversionIsExact) {
    std::mt19937 rng(3);
    std::uniform_int_distribution<int> d(-32768, 32767);
    for (size_t n : {size_t(0), size_t(1), size_t(7), size_t(8), size_t(9), size_t(31), size_t(1000)}) {
        std::vector<int16_t> s(n);
        for (auto& x : s) x = static_cast<int16_t>(d(rng));
        if (n >= 2) {
            s[0] = -32768;
            s[1] = 32767;
        }
        std::vector<float> a(n, 9.0f), b(n, 9.0f);
        mix::Int16ToFloat(s.data(), a.data(), n);
        mix::scalar::Int16ToFloat(s.data(), b.data(), n);
        for (size_t i = 0; i < n; ++i) ASSERT_EQ(a[i], b[i]) << "n=" << n << " i=" << i;
        if (n >= 2) {
            EXPECT_EQ(a[0], -1.0f);
            EXPECT_EQ(a[1], 32767.0f / 32768.0f);
        }
    }
}

TEST(Simd, MasterMatchesScalar) {
    std::mt19937 rng(4);
    for (int n : kLengths) {
        for (int rampLeft : {0, 1, 2, 3, n / 2, n - 1, n, n + 5}) {
            const auto mixBuf = Random(static_cast<size_t>(n) * 2, rng, -2.0f, 2.0f);  // exercises clipping
            std::vector<float> a(mixBuf.size(), 7.0f), b(mixBuf.size(), 7.0f);
            float pa[2] = {0.05f, 0.0f}, pb[2] = {0.05f, 0.0f};
            mix::Master(mixBuf.data(), a.data(), n, 0.2f, 0.01f, rampLeft, 0.8f, pa[0], pa[1]);
            mix::scalar::Master(mixBuf.data(), b.data(), n, 0.2f, 0.01f, rampLeft, 0.8f, pb[0], pb[1]);
            ExpectClose(a, b, "master", n);
            EXPECT_NEAR(pa[0], pb[0], 2e-6f) << "n=" << n << " ramp=" << rampLeft;
            EXPECT_NEAR(pa[1], pb[1], 2e-6f) << "n=" << n << " ramp=" << rampLeft;
        }
    }
}

TEST(Simd, MasterInPlace) {
    std::mt19937 rng(5);
    auto buf = Random(2 * 33, rng, -1.5f, 1.5f);
    std::vector<float> ref(buf.size());
    float a[2] = {0, 0}, b[2] = {0, 0};
    mix::scalar::Master(buf.data(), ref.data(), 33, 1.0f, 0.0f, 0, 0.5f, b[0], b[1]);
    mix::Master(buf.data(), buf.data(), 33, 1.0f, 0.0f, 0, 0.5f, a[0], a[1]);
    ExpectClose(buf, ref, "master in place", 33);
}

TEST(Simd, MasterTurnsNaNIntoSilence) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (int n : {1, 2, 3, 8, 9}) {
        std::vector<float> in(static_cast<size_t>(n) * 2, 0.25f);
        in[0] = nan;
        in[in.size() - 1] = nan;
        if (n > 2) in[3] = inf;
        for (int rampLeft : {0, n}) {
            std::vector<float> a(in.size()), b(in.size());
            float pa[2] = {0, 0}, pb[2] = {0, 0};
            mix::Master(in.data(), a.data(), n, 1.0f, 0.0f, rampLeft, 1.0f, pa[0], pa[1]);
            mix::scalar::Master(in.data(), b.data(), n, 1.0f, 0.0f, rampLeft, 1.0f, pb[0], pb[1]);
            for (size_t i = 0; i < a.size(); ++i) {
                ASSERT_FALSE(std::isnan(a[i])) << "n=" << n << " i=" << i;
                ASSERT_EQ(a[i], b[i]) << "n=" << n << " i=" << i;
            }
            EXPECT_EQ(a[0], 0.0f);
            EXPECT_EQ(pa[0], pb[0]);
            EXPECT_EQ(pa[1], pb[1]);
            EXPECT_FALSE(std::isnan(pa[0]) || std::isnan(pa[1]));
        }
    }
}

TEST(Simd, DotMatchesDoubleReference) {
    std::mt19937 rng(6);
    for (int n = 0; n <= 170; ++n) {
        const auto a = Random(static_cast<size_t>(n), rng);
        const auto b = Random(static_cast<size_t>(n), rng);
        double ref = 0.0, mag = 0.0;
        for (int j = 0; j < n; ++j) {
            ref += double(a[j]) * b[j];
            mag += std::fabs(double(a[j]) * b[j]);
        }
        EXPECT_NEAR(una::simd::Dot(a.data(), b.data(), n), ref, 1e-6 * (1.0 + mag)) << n;
    }
}
