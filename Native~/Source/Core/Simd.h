#pragma once
// Instruction-set selection for the mixer kernels and the resampler.
//
// x86 / x64 : SSE2 — part of the x64 baseline, so no runtime dispatch is needed.
// ARM       : NEON — always present on AArch64; the NDK enables it for armeabi-v7a.
// Anything else, or UNA_NO_SIMD defined: scalar code (also used as the reference
// the tests compare the SIMD paths against).

#if !defined(UNA_NO_SIMD) && \
    (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#  define UNA_SSE2 1
#  include <emmintrin.h>
#elif !defined(UNA_NO_SIMD) && (defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64))
#  define UNA_NEON 1
#  include <arm_neon.h>
#endif

namespace una::simd {

#if defined(UNA_SSE2)
constexpr const char* kIsa = "sse2";
#elif defined(UNA_NEON)
constexpr const char* kIsa = "neon";
#else
constexpr const char* kIsa = "scalar";
#endif

/// Dot product of two float arrays (any alignment). The summation order differs
/// from a plain loop, but it is fixed for a given n, so results are reproducible.
inline float Dot(const float* a, const float* b, int n) {
    int j = 0;
    float r = 0.0f;
#if defined(UNA_SSE2)
    __m128 acc0 = _mm_setzero_ps(), acc1 = _mm_setzero_ps();
    for (; j + 8 <= n; j += 8) {
        acc0 = _mm_add_ps(acc0, _mm_mul_ps(_mm_loadu_ps(a + j), _mm_loadu_ps(b + j)));
        acc1 = _mm_add_ps(acc1, _mm_mul_ps(_mm_loadu_ps(a + j + 4), _mm_loadu_ps(b + j + 4)));
    }
    if (j + 4 <= n) {
        acc0 = _mm_add_ps(acc0, _mm_mul_ps(_mm_loadu_ps(a + j), _mm_loadu_ps(b + j)));
        j += 4;
    }
    acc0 = _mm_add_ps(acc0, acc1);
    __m128 sh = _mm_shuffle_ps(acc0, acc0, _MM_SHUFFLE(2, 3, 0, 1));
    __m128 s = _mm_add_ps(acc0, sh);
    sh = _mm_movehl_ps(sh, s);
    r = _mm_cvtss_f32(_mm_add_ss(s, sh));
#elif defined(UNA_NEON)
    float32x4_t acc0 = vdupq_n_f32(0.0f), acc1 = vdupq_n_f32(0.0f);
    for (; j + 8 <= n; j += 8) {
        acc0 = vaddq_f32(acc0, vmulq_f32(vld1q_f32(a + j), vld1q_f32(b + j)));
        acc1 = vaddq_f32(acc1, vmulq_f32(vld1q_f32(a + j + 4), vld1q_f32(b + j + 4)));
    }
    if (j + 4 <= n) {
        acc0 = vaddq_f32(acc0, vmulq_f32(vld1q_f32(a + j), vld1q_f32(b + j)));
        j += 4;
    }
    acc0 = vaddq_f32(acc0, acc1);
#  if defined(__aarch64__) || defined(_M_ARM64)
    r = vaddvq_f32(acc0);
#  else
    const float32x2_t h = vadd_f32(vget_low_f32(acc0), vget_high_f32(acc0));
    r = vget_lane_f32(vpadd_f32(h, h), 0);
#  endif
#endif
    for (; j < n; ++j) r += a[j] * b[j];
    return r;
}

} // namespace una::simd
