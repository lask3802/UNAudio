#include "test_framework.h"
#include "../Source/Core/SimdUtils.h"

TEST(Simd_Clear) {
    float buf[16];
    for (auto& v : buf) v = 99.0f;

    una::simd::clear(buf, 16);
    for (int i = 0; i < 16; ++i)
        ASSERT_NEAR(buf[i], 0.0f, 0.0001f);
}

TEST(Simd_ApplyGain) {
    float buf[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    una::simd::apply_gain(buf, 0.5f, 8);

    ASSERT_NEAR(buf[0], 0.5f, 0.0001f);
    ASSERT_NEAR(buf[3], 2.0f, 0.0001f);
    ASSERT_NEAR(buf[7], 4.0f, 0.0001f);
}

TEST(Simd_MixAdd) {
    float dst[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float src[4] = {2.0f, 4.0f, 6.0f, 8.0f};

    una::simd::mix_add(dst, src, 0.5f, 4);

    ASSERT_NEAR(dst[0], 2.0f, 0.0001f);  // 1 + 2*0.5
    ASSERT_NEAR(dst[1], 3.0f, 0.0001f);  // 1 + 4*0.5
    ASSERT_NEAR(dst[2], 4.0f, 0.0001f);  // 1 + 6*0.5
    ASSERT_NEAR(dst[3], 5.0f, 0.0001f);  // 1 + 8*0.5
}

TEST(Simd_PeakLevel) {
    float buf[8] = {0.1f, -0.9f, 0.5f, -0.3f, 0.8f, -0.2f, 0.4f, -0.7f};
    float peak = una::simd::peak_level(buf, 8);
    ASSERT_NEAR(peak, 0.9f, 0.0001f);
}

TEST(Simd_PeakLevel_AllZero) {
    float buf[8] = {};
    float peak = una::simd::peak_level(buf, 8);
    ASSERT_NEAR(peak, 0.0f, 0.0001f);
}

TEST(Simd_StereoPan_Left) {
    float buf[8] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    una::simd::apply_stereo_pan(buf, -1.0f, 4);

    ASSERT_NEAR(buf[0], 1.0f, 0.0001f); // L
    ASSERT_NEAR(buf[1], 0.0f, 0.0001f); // R
    ASSERT_NEAR(buf[2], 1.0f, 0.0001f); // L
    ASSERT_NEAR(buf[3], 0.0f, 0.0001f); // R
}

TEST(Simd_StereoPan_Right) {
    float buf[8] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    una::simd::apply_stereo_pan(buf, 1.0f, 4);

    ASSERT_NEAR(buf[0], 0.0f, 0.0001f); // L zeroed
    ASSERT_NEAR(buf[1], 1.0f, 0.0001f); // R unchanged
}

TEST(Simd_StereoPan_Center) {
    float buf[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    una::simd::apply_stereo_pan(buf, 0.0f, 2);

    ASSERT_NEAR(buf[0], 1.0f, 0.0001f);
    ASSERT_NEAR(buf[1], 1.0f, 0.0001f);
}

TEST(Simd_Int16ToFloat) {
    int16_t src[4] = {0, 32767, -32768, 16384};
    float dst[4];
    una::simd::int16_to_float(dst, src, 4);

    ASSERT_NEAR(dst[0], 0.0f, 0.001f);
    ASSERT_NEAR(dst[1], 1.0f, 0.001f);
    ASSERT_NEAR(dst[2], -1.0f, 0.001f);
    ASSERT_NEAR(dst[3], 0.5f, 0.001f);
}

TEST(Simd_FloatToInt16) {
    float src[4] = {0.0f, 1.0f, -1.0f, 0.5f};
    int16_t dst[4];
    una::simd::float_to_int16(dst, src, 4);

    ASSERT_EQ(dst[0], 0);
    ASSERT_EQ(dst[1], 32767);
    ASSERT_EQ(dst[2], -32767);
    ASSERT_TRUE(std::abs(dst[3] - 16383) <= 1);
}

TEST(Simd_FloatToInt16_Clamp) {
    float src[2] = {2.0f, -3.0f};
    int16_t dst[2];
    una::simd::float_to_int16(dst, src, 2);

    ASSERT_EQ(dst[0], 32767);
    ASSERT_EQ(dst[1], -32767);
}
