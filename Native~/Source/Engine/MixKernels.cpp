#include "MixKernels.h"

#include "../Core/Simd.h"

namespace una::mix {

namespace {

// Interpolation weight from the fractional part of a 32.32 position, kept to 24 bits
// so the conversion is exact in float and identical in the scalar and SIMD paths.
inline float FracToT(uint64_t x) {
    return static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(x) >> 8)) * (1.0f / 16777216.0f);
}

inline float CubicS(float y0, float y1, float y2, float y3, float t) {
    return y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + t * (3.0f * (y1 - y2) + y3 - y0)));
}

// Scalar loops starting at frame i (the SIMD kernels finish their tails with these).

void MonoFrom(int i, const float* src, int n, float* out, float l0, float r0, float dl, float dr) {
    for (; i < n; ++i) {
        const float gl = l0 + dl * static_cast<float>(i);
        const float gr = r0 + dr * static_cast<float>(i);
        const float v = src[i];
        out[2 * i] += v * gl;
        out[2 * i + 1] += v * gr;
    }
}

void StereoFrom(int i, const float* src, int n, float* out, float l0, float r0, float dl, float dr) {
    for (; i < n; ++i) {
        const float gl = l0 + dl * static_cast<float>(i);
        const float gr = r0 + dr * static_cast<float>(i);
        out[2 * i] += src[2 * i] * gl;
        out[2 * i + 1] += src[2 * i + 1] * gr;
    }
}

void MonoCubicFrom(int i, uint64_t x, const float* src, uint64_t step, int n, float* out, float l0, float r0,
                   float dl, float dr) {
    for (; i < n; ++i, x += step) {
        const float* p = src + static_cast<size_t>(x >> 32);
        const float v = CubicS(p[0], p[1], p[2], p[3], FracToT(x));
        const float gl = l0 + dl * static_cast<float>(i);
        const float gr = r0 + dr * static_cast<float>(i);
        out[2 * i] += v * gl;
        out[2 * i + 1] += v * gr;
    }
}

void StereoCubicFrom(int i, uint64_t x, const float* src, uint64_t step, int n, float* out, float l0, float r0,
                     float dl, float dr) {
    for (; i < n; ++i, x += step) {
        const float* p = src + static_cast<size_t>(x >> 32) * 2;
        const float t = FracToT(x);
        const float vl = CubicS(p[0], p[2], p[4], p[6], t);
        const float vr = CubicS(p[1], p[3], p[5], p[7], t);
        const float gl = l0 + dl * static_cast<float>(i);
        const float gr = r0 + dr * static_cast<float>(i);
        out[2 * i] += vl * gl;
        out[2 * i + 1] += vr * gr;
    }
}

void Int16From(size_t i, const int16_t* src, float* dst, size_t count) {
    constexpr float k = 1.0f / 32768.0f;
    for (; i < count; ++i) dst[i] = static_cast<float>(src[i]) * k;
}

void MasterFrom(int i, const float* mix, float* out, int n, float g0, float inc, int rampLeft, float target,
                float& peakL, float& peakR) {
    for (; i < n; ++i) {
        const float g = i < rampLeft ? g0 + inc * static_cast<float>(i) : target;
        float l = mix[2 * i] * g;
        float r = mix[2 * i + 1] * g;
        l = l > 1.0f ? 1.0f : (l < -1.0f ? -1.0f : l);
        r = r > 1.0f ? 1.0f : (r < -1.0f ? -1.0f : r);
        const float al = l < 0.0f ? -l : l, ar = r < 0.0f ? -r : r;
        if (al > peakL) peakL = al;
        if (ar > peakR) peakR = ar;
        out[2 * i] = l;
        out[2 * i + 1] = r;
    }
}

// ── 4-lane float vector ─────────────────────────────────────────

#if defined(UNA_SSE2)
using V = __m128;
inline V Load(const float* p) { return _mm_loadu_ps(p); }
inline void Store(float* p, V v) { _mm_storeu_ps(p, v); }
inline V Set1(float x) { return _mm_set1_ps(x); }
inline V Set(float a, float b, float c, float d) { return _mm_setr_ps(a, b, c, d); }
inline V Add(V a, V b) { return _mm_add_ps(a, b); }
inline V Sub(V a, V b) { return _mm_sub_ps(a, b); }
inline V Mul(V a, V b) { return _mm_mul_ps(a, b); }
inline V Min(V a, V b) { return _mm_min_ps(a, b); }
inline V Max(V a, V b) { return _mm_max_ps(a, b); }
inline V Abs(V a) { return _mm_and_ps(a, _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF))); }
/// lo = a0 b0 a1 b1, hi = a2 b2 a3 b3
inline void Zip(V a, V b, V& lo, V& hi) { lo = _mm_unpacklo_ps(a, b); hi = _mm_unpackhi_ps(a, b); }
/// p[0] p[1] q[0] q[1]
inline V LoadPairs(const float* p, const float* q) {
    return _mm_loadh_pi(_mm_loadl_pi(_mm_setzero_ps(), reinterpret_cast<const __m64*>(p)),
                        reinterpret_cast<const __m64*>(q));
}
#elif defined(UNA_NEON)
using V = float32x4_t;
inline V Load(const float* p) { return vld1q_f32(p); }
inline void Store(float* p, V v) { vst1q_f32(p, v); }
inline V Set1(float x) { return vdupq_n_f32(x); }
inline V Set(float a, float b, float c, float d) {
    alignas(16) const float t[4] = {a, b, c, d};
    return vld1q_f32(t);
}
inline V Add(V a, V b) { return vaddq_f32(a, b); }
inline V Sub(V a, V b) { return vsubq_f32(a, b); }
inline V Mul(V a, V b) { return vmulq_f32(a, b); }
inline V Min(V a, V b) { return vminq_f32(a, b); }
inline V Max(V a, V b) { return vmaxq_f32(a, b); }
inline V Abs(V a) { return vabsq_f32(a); }
inline void Zip(V a, V b, V& lo, V& hi) {
    const float32x4x2_t z = vzipq_f32(a, b);
    lo = z.val[0];
    hi = z.val[1];
}
inline V LoadPairs(const float* p, const float* q) { return vcombine_f32(vld1_f32(p), vld1_f32(q)); }
#endif

#if defined(UNA_SSE2) || defined(UNA_NEON)
#  define UNA_MIX_SIMD 1

// Same expression tree as CubicS.
inline V CubicV(V y0, V y1, V y2, V y3, V t) {
    const V c3 = Sub(Add(Mul(Set1(3.0f), Sub(y1, y2)), y3), y0);
    const V c2 = Add(Sub(Add(Sub(Mul(Set1(2.0f), y0), Mul(Set1(5.0f), y1)), Mul(Set1(4.0f), y2)), y3), Mul(t, c3));
    const V c1 = Add(Sub(y2, y0), Mul(t, c2));
    return Add(y1, Mul(Mul(Set1(0.5f), t), c1));
}

/// Accumulates four mono samples (already gained per side) into four stereo frames.
inline void AccumulateMono4(float* o, V left, V right) {
    V lo, hi;
    Zip(left, right, lo, hi);
    Store(o, Add(Load(o), lo));
    Store(o + 4, Add(Load(o + 4), hi));
}
#endif

} // namespace

// ── Scalar reference ────────────────────────────────────────────

namespace scalar {
void Mono(const float* src, int n, float* out, float l0, float r0, float dl, float dr) {
    MonoFrom(0, src, n, out, l0, r0, dl, dr);
}
void Stereo(const float* src, int n, float* out, float l0, float r0, float dl, float dr) {
    StereoFrom(0, src, n, out, l0, r0, dl, dr);
}
void MonoCubic(const float* src, uint32_t frac, uint64_t step, int n, float* out, float l0, float r0, float dl,
               float dr) {
    MonoCubicFrom(0, frac, src, step, n, out, l0, r0, dl, dr);
}
void StereoCubic(const float* src, uint32_t frac, uint64_t step, int n, float* out, float l0, float r0, float dl,
                 float dr) {
    StereoCubicFrom(0, frac, src, step, n, out, l0, r0, dl, dr);
}
void Int16ToFloat(const int16_t* src, float* dst, size_t count) { Int16From(0, src, dst, count); }
void Master(const float* mix, float* out, int n, float g0, float inc, int rampLeft, float target, float& peakL,
            float& peakR) {
    MasterFrom(0, mix, out, n, g0, inc, rampLeft, target, peakL, peakR);
}
} // namespace scalar

// ── Dispatch ────────────────────────────────────────────────────

const char* IsaName() { return simd::kIsa; }

#if defined(UNA_MIX_SIMD)

void Mono(const float* src, int n, float* out, float l0, float r0, float dl, float dr) {
    const V L0 = Set1(l0), R0 = Set1(r0), DL = Set1(dl), DR = Set1(dr), K = Set(0.0f, 1.0f, 2.0f, 3.0f);
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        const V f = Add(Set1(static_cast<float>(i)), K);
        const V s = Load(src + i);
        AccumulateMono4(out + 2 * i, Mul(s, Add(L0, Mul(DL, f))), Mul(s, Add(R0, Mul(DR, f))));
    }
    MonoFrom(i, src, n, out, l0, r0, dl, dr);
}

void Stereo(const float* src, int n, float* out, float l0, float r0, float dl, float dr) {
    const V G0 = Set(l0, r0, l0, r0), D = Set(dl, dr, dl, dr), K = Set(0.0f, 0.0f, 1.0f, 1.0f);
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        const V f0 = Add(Set1(static_cast<float>(i)), K);
        const V f1 = Add(Set1(static_cast<float>(i + 2)), K);
        float* o = out + 2 * i;
        const float* s = src + 2 * i;
        Store(o, Add(Load(o), Mul(Load(s), Add(G0, Mul(D, f0)))));
        Store(o + 4, Add(Load(o + 4), Mul(Load(s + 4), Add(G0, Mul(D, f1)))));
    }
    StereoFrom(i, src, n, out, l0, r0, dl, dr);
}

void MonoCubic(const float* src, uint32_t frac, uint64_t step, int n, float* out, float l0, float r0, float dl,
               float dr) {
    const V L0 = Set1(l0), R0 = Set1(r0), DL = Set1(dl), DR = Set1(dr), K = Set(0.0f, 1.0f, 2.0f, 3.0f);
    alignas(16) float y0[4], y1[4], y2[4], y3[4], t[4];
    uint64_t x = frac;
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        // Positions are irregular, so gather the taps with scalar loads; the polynomial is SIMD.
        for (int j = 0; j < 4; ++j, x += step) {
            const float* p = src + static_cast<size_t>(x >> 32);
            y0[j] = p[0];
            y1[j] = p[1];
            y2[j] = p[2];
            y3[j] = p[3];
            t[j] = FracToT(x);
        }
        const V v = CubicV(Load(y0), Load(y1), Load(y2), Load(y3), Load(t));
        const V f = Add(Set1(static_cast<float>(i)), K);
        AccumulateMono4(out + 2 * i, Mul(v, Add(L0, Mul(DL, f))), Mul(v, Add(R0, Mul(DR, f))));
    }
    MonoCubicFrom(i, x, src, step, n, out, l0, r0, dl, dr);
}

void StereoCubic(const float* src, uint32_t frac, uint64_t step, int n, float* out, float l0, float r0, float dl,
                 float dr) {
    // Two frames per vector: lanes are L(a) R(a) L(b) R(b).
    const V G0 = Set(l0, r0, l0, r0), D = Set(dl, dr, dl, dr), K = Set(0.0f, 0.0f, 1.0f, 1.0f);
    uint64_t x = frac;
    int i = 0;
    for (; i + 2 <= n; i += 2) {
        const float* pa = src + static_cast<size_t>(x >> 32) * 2;
        const float ta = FracToT(x);
        x += step;
        const float* pb = src + static_cast<size_t>(x >> 32) * 2;
        const float tb = FracToT(x);
        x += step;
        const V v = CubicV(LoadPairs(pa, pb), LoadPairs(pa + 2, pb + 2), LoadPairs(pa + 4, pb + 4),
                           LoadPairs(pa + 6, pb + 6), Set(ta, ta, tb, tb));
        const V f = Add(Set1(static_cast<float>(i)), K);
        float* o = out + 2 * i;
        Store(o, Add(Load(o), Mul(v, Add(G0, Mul(D, f)))));
    }
    StereoCubicFrom(i, x, src, step, n, out, l0, r0, dl, dr);
}

void Int16ToFloat(const int16_t* src, float* dst, size_t count) {
    size_t i = 0;
#  if defined(UNA_SSE2)
    const __m128 k = _mm_set1_ps(1.0f / 32768.0f);
    for (; i + 8 <= count; i += 8) {
        const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
        // Duplicate each 16-bit sample into a 32-bit lane, then shift down to sign-extend.
        const __m128i lo = _mm_srai_epi32(_mm_unpacklo_epi16(v, v), 16);
        const __m128i hi = _mm_srai_epi32(_mm_unpackhi_epi16(v, v), 16);
        _mm_storeu_ps(dst + i, _mm_mul_ps(_mm_cvtepi32_ps(lo), k));
        _mm_storeu_ps(dst + i + 4, _mm_mul_ps(_mm_cvtepi32_ps(hi), k));
    }
#  else
    const float32x4_t k = vdupq_n_f32(1.0f / 32768.0f);
    for (; i + 8 <= count; i += 8) {
        const int16x8_t v = vld1q_s16(src + i);
        vst1q_f32(dst + i, vmulq_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(v))), k));
        vst1q_f32(dst + i + 4, vmulq_f32(vcvtq_f32_s32(vmovl_s16(vget_high_s16(v))), k));
    }
#  endif
    Int16From(i, src, dst, count);
}

void Master(const float* mix, float* out, int n, float g0, float inc, int rampLeft, float target, float& peakL,
            float& peakR) {
    const V one = Set1(1.0f), minusOne = Set1(-1.0f), G0 = Set1(g0), INC = Set1(inc), T = Set1(target);
    const V K = Set(0.0f, 0.0f, 1.0f, 1.0f);
    V pk = Set(peakL, peakR, peakL, peakR);
    int i = 0;
    for (; i + 2 <= n; i += 2) {
        V g;
        if (i + 1 < rampLeft) {
            g = Add(G0, Mul(INC, Add(Set1(static_cast<float>(i)), K)));
        } else if (i >= rampLeft) {
            g = T;
        } else {  // the ramp ends between these two frames
            const float a = g0 + inc * static_cast<float>(i);
            g = Set(a, a, target, target);
        }
        const V x = Min(Max(Mul(Load(mix + 2 * i), g), minusOne), one);
        pk = Max(pk, Abs(x));
        Store(out + 2 * i, x);
    }
    alignas(16) float lanes[4];
    Store(lanes, pk);
    peakL = lanes[0] > lanes[2] ? lanes[0] : lanes[2];
    peakR = lanes[1] > lanes[3] ? lanes[1] : lanes[3];
    MasterFrom(i, mix, out, n, g0, inc, rampLeft, target, peakL, peakR);
}

#else  // scalar build

void Mono(const float* src, int n, float* out, float l0, float r0, float dl, float dr) {
    scalar::Mono(src, n, out, l0, r0, dl, dr);
}
void Stereo(const float* src, int n, float* out, float l0, float r0, float dl, float dr) {
    scalar::Stereo(src, n, out, l0, r0, dl, dr);
}
void MonoCubic(const float* src, uint32_t frac, uint64_t step, int n, float* out, float l0, float r0, float dl,
               float dr) {
    scalar::MonoCubic(src, frac, step, n, out, l0, r0, dl, dr);
}
void StereoCubic(const float* src, uint32_t frac, uint64_t step, int n, float* out, float l0, float r0, float dl,
                 float dr) {
    scalar::StereoCubic(src, frac, step, n, out, l0, r0, dl, dr);
}
void Int16ToFloat(const int16_t* src, float* dst, size_t count) { scalar::Int16ToFloat(src, dst, count); }
void Master(const float* mix, float* out, int n, float g0, float inc, int rampLeft, float target, float& peakL,
            float& peakR) {
    scalar::Master(mix, out, n, g0, inc, rampLeft, target, peakL, peakR);
}

#endif

} // namespace una::mix
