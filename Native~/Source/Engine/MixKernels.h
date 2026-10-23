#pragma once
// Mixer inner loops (audio thread): SSE2 / NEON with a scalar fallback.
// Every kernel also exists in namespace `scalar`, the reference the SIMD paths
// are tested against (and what a UNA_NO_SIMD build uses).
//
// Output is always interleaved stereo and accumulated (+=). Gains ramp
// linearly: frame i is mixed with left gain l0 + dl * i and right gain r0 + dr * i.

#include <cstddef>
#include <cstdint>

namespace una::mix {

/// 1:1 playback. `src` is the first frame to play (mono / interleaved stereo).
void Mono(const float* src, int n, float* out, float l0, float r0, float dl, float dr);
void Stereo(const float* src, int n, float* out, float l0, float r0, float dl, float dr);

/// Catmull-Rom interpolation at 32.32 fixed-point source positions frac + i * step.
/// `src` starts one frame before the play position; the kernel reads frames up to
/// ((frac + (n - 1) * step) >> 32) + 3.
void MonoCubic(const float* src, uint32_t frac, uint64_t step, int n, float* out, float l0, float r0,
               float dl, float dr);
void StereoCubic(const float* src, uint32_t frac, uint64_t step, int n, float* out, float l0, float r0,
                 float dl, float dr);

/// dst[i] = src[i] / 32768.
void Int16ToFloat(const int16_t* src, float* dst, size_t count);

/// Master stage on interleaved stereo: gain (frame i < rampLeft uses g0 + inc * i,
/// later frames `target`), clip to [-1, 1], raise peakL / peakR to the largest
/// |sample|. `out` may equal `mix`.
void Master(const float* mix, float* out, int n, float g0, float inc, int rampLeft, float target,
            float& peakL, float& peakR);

/// Instruction set the kernels were built with: "sse2", "neon" or "scalar".
const char* IsaName();

namespace scalar {
void Mono(const float* src, int n, float* out, float l0, float r0, float dl, float dr);
void Stereo(const float* src, int n, float* out, float l0, float r0, float dl, float dr);
void MonoCubic(const float* src, uint32_t frac, uint64_t step, int n, float* out, float l0, float r0,
               float dl, float dr);
void StereoCubic(const float* src, uint32_t frac, uint64_t step, int n, float* out, float l0, float r0,
                 float dl, float dr);
void Int16ToFloat(const int16_t* src, float* dst, size_t count);
void Master(const float* mix, float* out, int n, float g0, float inc, int rampLeft, float target,
            float& peakL, float& peakR);
} // namespace scalar

} // namespace una::mix
