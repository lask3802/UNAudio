#include "TestUtil.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <thread>

using namespace test;

namespace {
una_clip LongClip(int frames = 48000 * 4) {
    una_clip c = LoadClip(MakeWavFloat(48000, 1, frames, [](int, int) { return 0.1f; }));
    EXPECT_EQ(WaitClip(c, UNA_CLIP_LOADED), UNA_CLIP_LOADED);
    return c;
}

una_voice_timing Timing(una_voice v, int64_t hostNs = 0) {
    una_voice_timing t{};
    EXPECT_EQ(una_voice_get_timing(v, hostNs, &t), UNA_OK);
    return t;
}
} // namespace

TEST(Timing, ManualModeAudibleEqualsMixHead) {
    ManualEngine e;
    una_clip c = LongClip();
    una_play_params p = DefaultPlay();
    p.start_dsp_frame = 1000;
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);

    // Before the mixer has seen the command: predicted negative time.
    auto t = Timing(v);
    EXPECT_EQ(t.state, UNA_VOICE_PENDING);
    EXPECT_NEAR(t.audible_frame, -1000.0, 1e-3);

    e.Render(512);
    t = Timing(v);
    EXPECT_EQ(t.state, UNA_VOICE_SCHEDULED);
    EXPECT_NEAR(t.audible_frame, 512.0 - 1000.0, 1e-3);
    EXPECT_EQ(t.start_dsp_frame, 1000);

    e.Render(1536);
    t = Timing(v);
    EXPECT_EQ(t.state, UNA_VOICE_PLAYING);
    EXPECT_NEAR(t.audible_frame, 2048.0 - 1000.0, 1e-3);
    EXPECT_NEAR(t.audible_seconds, 1048.0 / 48000.0, 1e-7);
    EXPECT_DOUBLE_EQ(t.mix_frame, 1048.0);
    EXPECT_DOUBLE_EQ(t.seg_rate, 1.0);
}

TEST(Timing, PauseFreezesAndPitchChangesSlope) {
    ManualEngine e;
    una_clip c = LongClip();
    una_play_params p = DefaultPlay();
    p.start_dsp_frame = 256;
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    una_voice_pause(v, 1256);
    e.Render(2048);
    auto t = Timing(v);
    EXPECT_EQ(t.state, UNA_VOICE_PAUSED);
    EXPECT_NEAR(t.audible_frame, 1000.0, 1e-3);
    una_voice_resume(v, 0);
    una_voice_set_pitch(v, 1.5f, 0);
    e.Render(1024);  // resumes at 2048
    t = Timing(v);
    EXPECT_NEAR(t.audible_frame, 1000.0 + 1024 * 1.5, 1e-3);
    EXPECT_NEAR(t.seg_rate, 1.5, 1e-9);
}

TEST(Timing, LoopSegmentsWrapTime) {
    ManualEngine e;
    una_clip c = LongClip(1000);
    una_play_params p = DefaultPlay();
    p.loop = 1;
    p.start_dsp_frame = 100;
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    e.Render(100 + 2500);
    EXPECT_NEAR(Timing(v).audible_frame, 500.0, 1e-3);
}

TEST(Timing, CatchUpKeepsAlignmentWhenLate) {
    ManualEngine e;
    auto wav = MakeWavFloat(48000, 1, 48000, [](int i, int) { return i * 1e-5f; });
    una_clip c = LoadClip(wav);
    ASSERT_EQ(WaitClip(c, UNA_CLIP_LOADED), UNA_CLIP_LOADED);
    e.Render(512);
    una_play_params p = DefaultPlay();
    p.start_dsp_frame = 100;  // already in the past
    p.flags = UNA_PLAY_CATCH_UP;
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    std::vector<float> out;
    e.Render(256, &out);
    EXPECT_FLOAT_EQ(out[0], 412 * 1e-5f);  // frame 512 plays source frame 412
    auto t = Timing(v);
    EXPECT_EQ(t.start_dsp_frame, 100);
    EXPECT_NEAR(t.audible_frame, 768.0 - 100.0, 1e-3);
    una_stats st{};
    una_get_stats(&st);
    EXPECT_GE(st.late_commands, 1);
}

TEST(Timing, TimingAtExplicitHostTime) {
    ManualEngine e;
    una_clip c = LongClip();
    una_play_params p = DefaultPlay();
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    e.Render(4800);
    // In manual mode host time is virtual: frame f is heard at f / rate.
    const int64_t at = 2400LL * 1000000000LL / 48000;
    auto t = Timing(v, at);
    EXPECT_NEAR(t.audible_frame, 2400.0, 1e-3);
    EXPECT_NEAR(una_audible_frame_at(at), 2400.0, 1e-3);
}

TEST(Timing, RealtimeDeviceClockIsSmoothAndMonotonic) {
    una_config cfg{};
    cfg.struct_size = sizeof(cfg);
    cfg.device_type = UNA_DEVICE_NULL;
    cfg.sample_rate = 48000;
    cfg.buffer_frames = 480;  // 10 ms periods: coarse on purpose
    ASSERT_EQ(una_init(&cfg), UNA_OK);
    una_clip c = LongClip(48000 * 10);
    una_play_params p = DefaultPlay();
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));  // let the DLL settle

    double prevSec = -1e9;
    int64_t prevNs = 0;
    double worst = 0;
    for (int i = 0; i < 300; ++i) {
        una_voice_timing t{};
        ASSERT_EQ(una_voice_get_timing(v, 0, &t), UNA_OK);
        ASSERT_GE(t.audible_seconds, prevSec) << i;
        if (prevNs) {
            // Audio time must advance with host time even between 10 ms callbacks.
            const double dHost = (t.host_ns - prevNs) * 1e-9;
            const double dAudio = t.audible_seconds - prevSec;
            worst = std::max(worst, std::fabs(dAudio - dHost));
        }
        prevSec = t.audible_seconds;
        prevNs = t.host_ns;
        std::this_thread::sleep_for(std::chrono::microseconds(3000));
    }
    una_shutdown();
    EXPECT_LT(worst, 0.0015) << "seconds";  // < 1.5 ms deviation per step
}
