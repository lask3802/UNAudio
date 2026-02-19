#ifndef UNAUDIO_SIMD_UTILS_H
#define UNAUDIO_SIMD_UTILS_H

/*
 * Cross-platform SIMD abstraction for audio hot paths.
 * Compile-time selects SSE2 / AVX2 / NEON / scalar fallback.
 *
 * Uses unaligned loads/stores so buffers from external callers
 * (e.g. platform audio callbacks) do not require special alignment.
 * FrameAllocator still provides aligned buffers for optimal throughput.
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>

// Detect SIMD availability
#if defined(__AVX2__) && !defined(UNA_FORCE_SCALAR)
    #include <immintrin.h>
    #define UNA_SIMD_AVX2 1
    #define UNA_SIMD_LANE_COUNT 8
#elif (defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)) && !defined(UNA_FORCE_SCALAR)
    #include <emmintrin.h>
    #define UNA_SIMD_SSE2 1
    #define UNA_SIMD_LANE_COUNT 4
#elif defined(__ARM_NEON) && !defined(UNA_FORCE_SCALAR)
    #include <arm_neon.h>
    #define UNA_SIMD_NEON 1
    #define UNA_SIMD_LANE_COUNT 4
#else
    #define UNA_SIMD_SCALAR 1
    #define UNA_SIMD_LANE_COUNT 1
#endif

namespace una {
namespace simd {

// --- Mix: dst[i] += src[i] * volume ---
inline void mix_add(float* dst, const float* src, float volume, int sample_count)
{
#if defined(UNA_SIMD_AVX2)
    __m256 vol = _mm256_set1_ps(volume);
    int i = 0;
    for (; i + 8 <= sample_count; i += 8) {
        __m256 s = _mm256_loadu_ps(src + i);
        __m256 d = _mm256_loadu_ps(dst + i);
        _mm256_storeu_ps(dst + i, _mm256_add_ps(d, _mm256_mul_ps(s, vol)));
    }
    for (; i < sample_count; ++i)
        dst[i] += src[i] * volume;

#elif defined(UNA_SIMD_SSE2)
    __m128 vol = _mm_set1_ps(volume);
    int i = 0;
    for (; i + 4 <= sample_count; i += 4) {
        __m128 s = _mm_loadu_ps(src + i);
        __m128 d = _mm_loadu_ps(dst + i);
        _mm_storeu_ps(dst + i, _mm_add_ps(d, _mm_mul_ps(s, vol)));
    }
    for (; i < sample_count; ++i)
        dst[i] += src[i] * volume;

#elif defined(UNA_SIMD_NEON)
    float32x4_t vol = vdupq_n_f32(volume);
    int i = 0;
    for (; i + 4 <= sample_count; i += 4) {
        float32x4_t s = vld1q_f32(src + i);
        float32x4_t d = vld1q_f32(dst + i);
        vst1q_f32(dst + i, vaddq_f32(d, vmulq_f32(s, vol)));
    }
    for (; i < sample_count; ++i)
        dst[i] += src[i] * volume;

#else // scalar
    for (int i = 0; i < sample_count; ++i)
        dst[i] += src[i] * volume;
#endif
}

// --- Apply volume in-place: buf[i] *= volume ---
inline void apply_gain(float* buf, float volume, int sample_count)
{
#if defined(UNA_SIMD_AVX2)
    __m256 vol = _mm256_set1_ps(volume);
    int i = 0;
    for (; i + 8 <= sample_count; i += 8) {
        __m256 d = _mm256_loadu_ps(buf + i);
        _mm256_storeu_ps(buf + i, _mm256_mul_ps(d, vol));
    }
    for (; i < sample_count; ++i)
        buf[i] *= volume;

#elif defined(UNA_SIMD_SSE2)
    __m128 vol = _mm_set1_ps(volume);
    int i = 0;
    for (; i + 4 <= sample_count; i += 4) {
        __m128 d = _mm_loadu_ps(buf + i);
        _mm_storeu_ps(buf + i, _mm_mul_ps(d, vol));
    }
    for (; i < sample_count; ++i)
        buf[i] *= volume;

#elif defined(UNA_SIMD_NEON)
    float32x4_t vol = vdupq_n_f32(volume);
    int i = 0;
    for (; i + 4 <= sample_count; i += 4) {
        float32x4_t d = vld1q_f32(buf + i);
        vst1q_f32(buf + i, vmulq_f32(d, vol));
    }
    for (; i < sample_count; ++i)
        buf[i] *= volume;

#else
    for (int i = 0; i < sample_count; ++i)
        buf[i] *= volume;
#endif
}

// --- Peak detector: returns max |buf[i]| ---
inline float peak_level(const float* buf, int sample_count)
{
    float peak = 0.0f;

#if defined(UNA_SIMD_SSE2) || defined(UNA_SIMD_AVX2)
    // Use SSE2 for both SSE2 and AVX2 (simpler, still fast for peak)
    __m128 sign_mask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
    __m128 vpeak = _mm_setzero_ps();
    int i = 0;
    for (; i + 4 <= sample_count; i += 4) {
        __m128 v = _mm_loadu_ps(buf + i);
        v = _mm_and_ps(v, sign_mask); // fabs
        vpeak = _mm_max_ps(vpeak, v);
    }
    // horizontal max
    alignas(16) float tmp[4];
    _mm_store_ps(tmp, vpeak);
    for (int j = 0; j < 4; ++j)
        if (tmp[j] > peak) peak = tmp[j];
    for (; i < sample_count; ++i) {
        float a = std::fabs(buf[i]);
        if (a > peak) peak = a;
    }

#elif defined(UNA_SIMD_NEON)
    float32x4_t vpeak = vdupq_n_f32(0.0f);
    int i = 0;
    for (; i + 4 <= sample_count; i += 4) {
        float32x4_t v = vld1q_f32(buf + i);
        v = vabsq_f32(v);
        vpeak = vmaxq_f32(vpeak, v);
    }
    // vmaxvq_f32 is aarch64-only; use pairwise max for 32-bit ARM compat
#if defined(__aarch64__)
    peak = vmaxvq_f32(vpeak);
#else
    float32x2_t half = vpmax_f32(vget_low_f32(vpeak), vget_high_f32(vpeak));
    half = vpmax_f32(half, half);
    peak = vget_lane_f32(half, 0);
#endif
    for (; i < sample_count; ++i) {
        float a = std::fabs(buf[i]);
        if (a > peak) peak = a;
    }

#else
    for (int i = 0; i < sample_count; ++i) {
        float a = std::fabs(buf[i]);
        if (a > peak) peak = a;
    }
#endif

    return peak;
}

// --- Clear buffer ---
inline void clear(float* buf, int sample_count)
{
    std::memset(buf, 0, static_cast<size_t>(sample_count) * sizeof(float));
}

// --- Stereo pan: constant-power panning using sqrt curve ---
inline void apply_stereo_pan(float* buf, float pan, int frame_count)
{
    // pan: -1.0 (full left) to 1.0 (full right)
    // Constant-power panning: maintains perceived loudness at center
    float left_gain  = std::sqrt((1.0f - pan) * 0.5f);
    float right_gain = std::sqrt((1.0f + pan) * 0.5f);

    for (int i = 0; i < frame_count; ++i) {
        buf[i * 2 + 0] *= left_gain;
        buf[i * 2 + 1] *= right_gain;
    }
}

// --- Convert int16 interleaved to float ---
inline void int16_to_float(float* dst, const int16_t* src, int sample_count)
{
    constexpr float scale = 1.0f / 32768.0f;
    for (int i = 0; i < sample_count; ++i)
        dst[i] = static_cast<float>(src[i]) * scale;
}

// --- Convert float to int16 interleaved (with clamp) ---
inline void float_to_int16(int16_t* dst, const float* src, int sample_count)
{
    for (int i = 0; i < sample_count; ++i) {
        float s = src[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        dst[i] = static_cast<int16_t>(s * 32767.0f);
    }
}

} // namespace simd
} // namespace una

#endif // UNAUDIO_SIMD_UTILS_H
