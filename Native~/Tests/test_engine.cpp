#include "TestUtil.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace test;

namespace {

// Each frame encodes its own index so output can be checked frame-exactly.
float Ramp(int i, int c) { return (c == 0 ? 1.0f : -1.0f) * float(i) * 1e-5f; }

std::vector<uint8_t> RampWav(int frames = 40000, int rate = 48000) { return MakeWavFloat(rate, 2, frames, Ramp); }

una_clip ReadyRamp(int frames = 40000) {
    una_clip c = LoadClip(RampWav(frames));
    EXPECT_NE(c, 0u);
    EXPECT_EQ(WaitClip(c, UNA_CLIP_LOADED), UNA_CLIP_LOADED);
    return c;
}

float L(const std::vector<float>& out, int frame) { return out[size_t(frame) * 2]; }
float R(const std::vector<float>& out, int frame) { return out[size_t(frame) * 2 + 1]; }

const una_event* Find(const std::vector<una_event>& ev, int type, uint32_t handle = 0) {
    for (const auto& e : ev)
        if (e.type == type && (handle == 0 || e.handle == handle)) return &e;
    return nullptr;
}

} // namespace

TEST(Engine, InitShutdownAndAbiGuard) {
    una_config bad{};
    bad.struct_size = 12;
    EXPECT_EQ(una_init(&bad), UNA_E_ABI_MISMATCH);
    {
        ManualEngine e;
        ASSERT_TRUE(e.ok());
        EXPECT_EQ(una_is_initialized(), 1);
        una_config cfg{};
        cfg.struct_size = sizeof(cfg);
        cfg.device_type = UNA_DEVICE_MANUAL;
        EXPECT_EQ(una_init(&cfg), UNA_E_ALREADY_INITIALIZED);
        una_device_info info{};
        ASSERT_EQ(una_get_device_info(&info), UNA_OK);
        EXPECT_EQ(info.sample_rate, 48000);
        EXPECT_EQ(info.backend, UNA_BACKEND_MANUAL);
    }
    EXPECT_EQ(una_is_initialized(), 0);
    ManualEngine again;  // re-init after shutdown
    EXPECT_TRUE(again.ok());
}

TEST(Engine, ClipLifecycleAndEvents) {
    ManualEngine e;
    auto ogg = ReadFixture("tone_48k_stereo_3s.ogg");
    una_clip c = LoadClip(ogg, UNA_LOAD_DECODE, UNA_SAMPLE_INT16);
    ASSERT_NE(c, 0u);
    ASSERT_EQ(WaitClip(c, UNA_CLIP_LOADED), UNA_CLIP_LOADED);
    una_clip_info info{};
    ASSERT_EQ(una_clip_get_info(c, &info), UNA_OK);
    EXPECT_EQ(info.channels, 2);
    EXPECT_EQ(info.total_frames, 144000);
    EXPECT_EQ(info.decoded_frames, 144000);
    EXPECT_NEAR(info.length_seconds, 3.0, 1e-9);
    EXPECT_EQ(info.memory_bytes, 144000 * 2 * 2);  // compressed copy freed after decode

    auto ev = DrainEvents();
    EXPECT_NE(Find(ev, UNA_EVENT_CLIP_READY, c), nullptr);
    EXPECT_NE(Find(ev, UNA_EVENT_CLIP_LOADED, c), nullptr);

    EXPECT_EQ(una_clip_release(c), UNA_OK);
    EXPECT_EQ(una_clip_release(c), UNA_E_INVALID_HANDLE);
    una_update();
    ev = DrainEvents();
    EXPECT_NE(Find(ev, UNA_EVENT_CLIP_FREED, c), nullptr);
    una_stats st{};
    una_get_stats(&st);
    EXPECT_EQ(st.live_clips, 0);
    EXPECT_EQ(st.clip_memory_bytes, 0);
}

TEST(Engine, ResamplesToDeviceRateOnDecode) {
    ManualEngine e(48000);
    una_clip c = LoadClip(ReadFixture("tone_44k_mono_2s.ogg"));
    ASSERT_EQ(WaitClip(c, UNA_CLIP_LOADED), UNA_CLIP_LOADED);
    una_clip_info info{};
    una_clip_get_info(c, &info);
    EXPECT_EQ(info.source_rate, 44100);
    EXPECT_EQ(info.stored_rate, 48000);
    EXPECT_EQ(info.total_frames, 96000);
    EXPECT_NEAR(info.length_seconds, 2.0, 1e-9);

    una_clip raw = LoadClip(ReadFixture("tone_44k_mono_2s.ogg"), UNA_LOAD_DECODE, UNA_SAMPLE_FLOAT32,
                            UNA_CLIP_FLAG_NO_RESAMPLE);
    ASSERT_EQ(WaitClip(raw, UNA_CLIP_LOADED), UNA_CLIP_LOADED);
    una_clip_get_info(raw, &info);
    EXPECT_EQ(info.stored_rate, 44100);
    EXPECT_EQ(info.total_frames, 88200);
}

TEST(Engine, BorrowedSourceIsReleasedAfterDecode) {
    ManualEngine e;
    auto wav = RampWav(5000);
    una_clip c = LoadClip(wav, UNA_LOAD_DECODE, UNA_SAMPLE_FLOAT32, UNA_CLIP_FLAG_BORROW);
    ASSERT_EQ(WaitClip(c, UNA_CLIP_LOADED), UNA_CLIP_LOADED);
    auto ev = DrainEvents();
    EXPECT_NE(Find(ev, UNA_EVENT_CLIP_SOURCE_RELEASED, c), nullptr);
}

TEST(Engine, ImmediatePlayStartsAtNextBlockExactly) {
    ManualEngine e;
    una_clip c = ReadyRamp();
    std::vector<float> out;
    e.Render(512, &out);
    una_play_params p = DefaultPlay();
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    e.Render(1024, &out);
    for (int k = 0; k < 1024; ++k) {
        ASSERT_FLOAT_EQ(L(out, 512 + k), Ramp(k, 0)) << k;
        ASSERT_FLOAT_EQ(R(out, 512 + k), Ramp(k, 1)) << k;
    }
    auto ev = DrainEvents();
    const una_event* s = Find(ev, UNA_EVENT_VOICE_STARTED, v);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->dsp_frame, 512);
}

TEST(Engine, ScheduledPlayAndStopAreSampleAccurate) {
    ManualEngine e;
    una_clip c = ReadyRamp();
    una_play_params p = DefaultPlay();
    p.start_dsp_frame = 1000;  // middle of the 4th 256-frame block
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    ASSERT_EQ(una_voice_stop(v, 1777, 0.0f), UNA_OK);
    std::vector<float> out;
    e.Render(4096, &out);
    for (int f = 0; f < 1000; ++f) ASSERT_EQ(L(out, f), 0.0f) << f;
    for (int f = 1000; f < 1777; ++f) ASSERT_FLOAT_EQ(L(out, f), Ramp(f - 1000, 0)) << f;
    for (int f = 1777; f < 4096; ++f) ASSERT_EQ(L(out, f), 0.0f) << f;
    auto ev = DrainEvents();
    ASSERT_NE(Find(ev, UNA_EVENT_VOICE_STARTED, v), nullptr);
    EXPECT_EQ(Find(ev, UNA_EVENT_VOICE_STARTED, v)->dsp_frame, 1000);
    ASSERT_NE(Find(ev, UNA_EVENT_VOICE_ENDED, v), nullptr);
    EXPECT_EQ(Find(ev, UNA_EVENT_VOICE_ENDED, v)->dsp_frame, 1777);
    EXPECT_EQ(una_voice_get_state(v), UNA_VOICE_ENDED);
    una_update();
    EXPECT_EQ(una_voice_get_state(v), UNA_VOICE_INVALID);
}

TEST(Engine, ScheduledEventsIndependentOfBlockSize) {
    for (int block : {64, 100, 256, 1024, 4000}) {
        ManualEngine e(48000, block);
        una_clip c = ReadyRamp();
        una_play_params p = DefaultPlay();
        p.start_dsp_frame = 3333;
        una_voice v = 0;
        ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
        una_voice_set_volume(v, 0.5f, 0.0f, 4444);
        std::vector<float> out;
        e.Render(6000, &out, block);
        ASSERT_EQ(L(out, 3332), 0.0f) << block;
        ASSERT_FLOAT_EQ(L(out, 3333), Ramp(0, 0)) << block;
        ASSERT_FLOAT_EQ(L(out, 4443), Ramp(1110, 0)) << block;
        ASSERT_FLOAT_EQ(L(out, 4444), 0.5f * Ramp(1111, 0)) << block;
    }
}

TEST(Engine, ClipEndEndsVoiceOnExactFrame) {
    ManualEngine e;
    una_clip c = ReadyRamp(700);
    una_play_params p = DefaultPlay();
    p.start_dsp_frame = 300;
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    std::vector<float> out;
    e.Render(2048, &out);
    EXPECT_FLOAT_EQ(L(out, 999), Ramp(699, 0));
    EXPECT_EQ(L(out, 1000), 0.0f);
    auto ev = DrainEvents();
    ASSERT_NE(Find(ev, UNA_EVENT_VOICE_ENDED, v), nullptr);
    EXPECT_EQ(Find(ev, UNA_EVENT_VOICE_ENDED, v)->dsp_frame, 1000);
}

TEST(Engine, LoopWrapsSeamlessly) {
    ManualEngine e;
    una_clip c = ReadyRamp(300);
    una_play_params p = DefaultPlay();
    p.loop = 1;
    p.start_dsp_frame = 256;
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    std::vector<float> out;
    e.Render(2048, &out);
    for (int f = 256; f < 2048; ++f) ASSERT_FLOAT_EQ(L(out, f), Ramp((f - 256) % 300, 0)) << f;
    auto ev = DrainEvents();
    int loops = 0;
    for (const auto& x : ev)
        if (x.type == UNA_EVENT_VOICE_LOOPED && x.handle == v) {
            EXPECT_EQ((x.dsp_frame - 256) % 300, 0);
            ++loops;
        }
    EXPECT_EQ(loops, (2048 - 256) / 300);
}

TEST(Engine, LoopRegion) {
    ManualEngine e;
    una_clip c = ReadyRamp(1000);
    una_play_params p = DefaultPlay();
    p.loop = 1;
    p.loop_start = 100;
    p.loop_end = 200;
    p.start_dsp_frame = 1;
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    std::vector<float> out;
    e.Render(1024, &out);
    // intro 0..199, then 100..199 repeating
    for (int f = 1; f < 1024; ++f) {
        const int k = f - 1;
        const int expect = k < 200 ? k : 100 + (k - 200) % 100;
        ASSERT_FLOAT_EQ(L(out, f), Ramp(expect, 0)) << f;
    }
}

TEST(Engine, PauseResumeSeekVolumePan) {
    ManualEngine e;
    una_clip c = ReadyRamp();
    una_play_params p = DefaultPlay();
    p.start_dsp_frame = 1000;
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    una_voice_pause(v, 1200);
    una_voice_resume(v, 1400);
    una_voice_seek(v, 5000, 1600);
    una_voice_set_pan(v, -1.0f, 1800);
    std::vector<float> out;
    e.Render(2400, &out);
    EXPECT_FLOAT_EQ(L(out, 1199), Ramp(199, 0));
    for (int f = 1200; f < 1400; ++f) ASSERT_EQ(L(out, f), 0.0f) << f;
    EXPECT_FLOAT_EQ(L(out, 1400), Ramp(200, 0));
    EXPECT_FLOAT_EQ(L(out, 1599), Ramp(399, 0));
    EXPECT_FLOAT_EQ(L(out, 1600), Ramp(5000, 0));
    // Hard-left pan: right channel ramps to zero within the 64-frame de-zipper.
    EXPECT_FLOAT_EQ(L(out, 1900), Ramp(5300, 0));
    EXPECT_EQ(R(out, 1900), 0.0f);
}

TEST(Engine, PitchTwoSkipsEveryOtherFrame) {
    ManualEngine e;
    una_clip c = ReadyRamp();
    una_play_params p = DefaultPlay();
    p.pitch = 2.0f;
    p.start_dsp_frame = 10;
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    std::vector<float> out;
    e.Render(1024, &out);
    for (int k = 0; k < 1000; ++k) ASSERT_NEAR(L(out, 10 + k), Ramp(2 * k, 0), 1e-7) << k;
}

TEST(Engine, FadeInAndFadeOutStop) {
    ManualEngine e;
    auto dc = MakeWavFloat(48000, 1, 48000, [](int, int) { return 0.5f; });
    una_clip c = LoadClip(dc);
    ASSERT_EQ(WaitClip(c, UNA_CLIP_LOADED), UNA_CLIP_LOADED);
    una_play_params p = DefaultPlay();
    p.start_dsp_frame = 100;
    p.fade_in_seconds = 480.0f / 48000.0f;
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    una_voice_stop(v, 2000, 960.0f / 48000.0f);
    std::vector<float> out;
    e.Render(4096, &out);
    EXPECT_NEAR(L(out, 100), 0.0f, 1e-6);
    EXPECT_NEAR(L(out, 340), 0.25f, 0.01f);
    EXPECT_NEAR(L(out, 1000), 0.5f, 1e-6);
    EXPECT_NEAR(L(out, 2480), 0.25f, 0.01f);
    EXPECT_EQ(L(out, 2960), 0.0f);
    auto ev = DrainEvents();
    ASSERT_NE(Find(ev, UNA_EVENT_VOICE_ENDED, v), nullptr);
    EXPECT_EQ(Find(ev, UNA_EVENT_VOICE_ENDED, v)->dsp_frame, 2960);
}

TEST(Engine, BusAndMasterGain) {
    ManualEngine e;
    auto dc = MakeWavFloat(48000, 1, 48000, [](int, int) { return 0.5f; });
    una_clip c = LoadClip(dc);
    ASSERT_EQ(WaitClip(c, UNA_CLIP_LOADED), UNA_CLIP_LOADED);
    una_bus_set_volume(3, 0.5f, 0.0f);
    una_set_master_volume(0.5f, 0.0f);
    una_play_params p = DefaultPlay();
    p.bus = 3;
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    std::vector<float> out;
    e.Render(512, &out);
    EXPECT_FLOAT_EQ(L(out, 300), 0.125f);
    una_set_master_volume(1.0f, 0.0f);
    una_bus_set_paused(3, 1, 0);
    out.clear();
    e.Render(512, &out);
    EXPECT_EQ(L(out, 10), 0.0f);
}

TEST(Engine, MonoSourceCentersOnBothChannels) {
    ManualEngine e;
    auto wav = MakeWavFloat(48000, 1, 1000, [](int i, int) { return i * 1e-4f; });
    una_clip c = LoadClip(wav);
    ASSERT_EQ(WaitClip(c, UNA_CLIP_LOADED), UNA_CLIP_LOADED);
    una_play_params p = DefaultPlay();
    p.start_dsp_frame = 1;
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    std::vector<float> out;
    e.Render(512, &out);
    EXPECT_FLOAT_EQ(L(out, 101), 100 * 1e-4f);
    EXPECT_FLOAT_EQ(R(out, 101), 100 * 1e-4f);
}

TEST(Engine, VoiceLimitAndReclaim) {
    ManualEngine e(48000, 256, 8);
    una_clip c = ReadyRamp(100);
    std::vector<una_voice> vs;
    for (int i = 0; i < 8; ++i) {
        una_play_params p = DefaultPlay();
        una_voice v = 0;
        ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
        vs.push_back(v);
    }
    una_play_params p = DefaultPlay();
    una_voice extra = 0;
    EXPECT_EQ(una_voice_play(c, &p, &extra), UNA_E_NO_VOICE);
    e.Render(512);
    // All ended (100-frame clip); slots come back without an explicit update.
    EXPECT_EQ(una_voice_play(c, &p, &extra), UNA_OK);
    EXPECT_EQ(una_voice_get_state(vs[0]), UNA_VOICE_INVALID);  // reclaimed: handle is stale
    EXPECT_EQ(una_voice_stop(vs[0], 0, 0), UNA_E_INVALID_HANDLE);
}

TEST(Engine, ReleasedClipKeepsPlayingUntilVoiceEnds) {
    ManualEngine e;
    una_clip c = ReadyRamp(2000);
    una_play_params p = DefaultPlay();
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    ASSERT_EQ(una_clip_release(c), UNA_OK);
    std::vector<float> out;
    e.Render(512, &out);
    EXPECT_FLOAT_EQ(L(out, 300), Ramp(300, 0));
    una_update();
    auto ev = DrainEvents();
    EXPECT_EQ(Find(ev, UNA_EVENT_CLIP_FREED), nullptr);
    e.Render(2048);
    una_update();
    ev = DrainEvents();
    EXPECT_NE(Find(ev, UNA_EVENT_CLIP_FREED), nullptr);
}

TEST(Engine, StopAllWithFade) {
    ManualEngine e;
    una_clip c = ReadyRamp();
    for (int i = 0; i < 4; ++i) {
        una_play_params p = DefaultPlay();
        una_voice v = 0;
        ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    }
    e.Render(512);
    ASSERT_EQ(una_stop_all(0.01f), UNA_OK);
    e.Render(1024);
    una_stats st{};
    una_get_stats(&st);
    EXPECT_EQ(st.active_voices, 0);
}
