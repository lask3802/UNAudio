// Regression tests for issues found in review: each test names the failure it guards.

#include "TestUtil.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace test;

namespace {

float Ramp(int i, int c) { return (c == 0 ? 1.0f : -1.0f) * float(i) * 1e-5f; }

float L(const std::vector<float>& out, int64_t frame) { return out[size_t(frame) * 2]; }

una_clip Ready(const std::vector<uint8_t>& wav, int mode = UNA_LOAD_DECODE) {
    una_clip c = LoadClip(wav, mode);
    EXPECT_NE(c, 0u);
    EXPECT_EQ(WaitClip(c, UNA_CLIP_LOADED), UNA_CLIP_LOADED);
    return c;
}

bool Has(const std::vector<una_event>& ev, int type, uint32_t handle) {
    for (const auto& e : ev)
        if (e.type == type && e.handle == handle) return true;
    return false;
}

int64_t StartOf(const std::vector<una_event>& ev, una_voice v) {
    for (const auto& e : ev)
        if (e.type == UNA_EVENT_VOICE_STARTED && e.handle == v) return e.dsp_frame;
    return -1;
}

std::vector<float> RenderPaced(ManualEngine& e, int frames) {
    std::vector<float> out;
    for (int done = 0; done < frames; done += 256) {
        e.Render(256, &out);
        std::this_thread::sleep_for(std::chrono::microseconds(300));
    }
    return out;
}

} // namespace

// A handle kept from an earlier session used to address the new session's voice in the
// same slot (generations restarted at zero), so stopping a stale voice stopped another.
TEST(Regression, HandlesDoNotAliasAcrossSessions) {
    una_clip c1;
    una_voice v1 = 0;
    {
        ManualEngine e;
        c1 = Ready(MakeWavFloat(48000, 2, 48000, Ramp));
        una_play_params p = DefaultPlay();
        ASSERT_EQ(una_voice_play(c1, &p, &v1), UNA_OK);
        e.Render(256);
    }
    ManualEngine e;
    una_clip c2 = Ready(MakeWavFloat(48000, 2, 48000, Ramp));
    una_play_params p = DefaultPlay();
    una_voice v2 = 0;
    ASSERT_EQ(una_voice_play(c2, &p, &v2), UNA_OK);
    e.Render(256);
    EXPECT_NE(c1, c2);
    EXPECT_NE(v1, v2);
    EXPECT_EQ(una_voice_stop(v1, 0, 0.0f), UNA_E_INVALID_HANDLE);
    EXPECT_EQ(una_clip_release(c1), UNA_E_INVALID_HANDLE);
    e.Render(256);
    EXPECT_EQ(una_voice_get_state(v2), UNA_VOICE_PLAYING);
}

// Master volume set before init (a saved "muted" setting) used to be reset to 1 by init.
TEST(Regression, MixVolumesSurviveInitAndRestart) {
    struct Restore {  // volumes are process-wide: never leak them into later tests
        ~Restore() {
            una_shutdown();
            una_set_master_volume(1.0f, 0.0f);
            una_bus_set_volume(3, 1.0f, 0.0f);
        }
    } restore;
    ASSERT_EQ(una_set_master_volume(0.5f, 0.0f), UNA_OK);
    ASSERT_EQ(una_bus_set_volume(3, 0.25f, 0.0f), UNA_OK);
    una_config cfg{};
    cfg.struct_size = sizeof(cfg);
    cfg.device_type = UNA_DEVICE_MANUAL;
    cfg.sample_rate = 48000;
    cfg.buffer_frames = 256;
    cfg.channels = 2;
    ASSERT_EQ(una_init(&cfg), UNA_OK);
    EXPECT_FLOAT_EQ(una_get_master_volume(), 0.5f);
    una_clip c = Ready(MakeWavFloat(48000, 2, 4800, Ramp));
    una_play_params p = DefaultPlay();
    p.bus = 3;
    p.start_dsp_frame = 1;
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    std::vector<float> out(512 * 2);
    ASSERT_EQ(una_render(out.data(), 512, 2), UNA_OK);
    EXPECT_FLOAT_EQ(L(out, 101), Ramp(100, 0) * 0.5f * 0.25f);
}

// A fading stop on a voice whose bus (or the voice itself) is paused never advanced the
// fade: the voice stayed PLAYING forever, leaked its slot and faded out on resume. Covers
// stop-then-pause and pause-then-stop, for una_voice_stop and una_stop_all.
TEST(Regression, FadeStopOnPausedVoiceOrBusEndsVoice) {
    ManualEngine e;
    una_clip c = Ready(MakeWavFloat(48000, 2, 48000, Ramp));
    auto play = [&](int bus) {
        una_play_params p = DefaultPlay();
        p.bus = bus;
        una_voice v = 0;
        EXPECT_EQ(una_voice_play(c, &p, &v), UNA_OK);
        return v;
    };
    // Pause the bus, then stop with a fade: one voice by handle, one through stop_all.
    una_voice a = play(2);
    una_voice b = play(2);
    e.Render(512);
    ASSERT_EQ(una_bus_set_paused(2, 1, 0), UNA_OK);
    e.Render(256);
    ASSERT_EQ(una_voice_stop(a, 0, 0.5f), UNA_OK);
    e.Render(256);
    ASSERT_EQ(una_stop_all(0.5f), UNA_OK);
    e.Render(256);
    auto ev = DrainEvents();
    EXPECT_TRUE(Has(ev, UNA_EVENT_VOICE_ENDED, a));
    EXPECT_TRUE(Has(ev, UNA_EVENT_VOICE_ENDED, b));
    ASSERT_EQ(una_bus_set_paused(2, 0, 0), UNA_OK);
    std::vector<float> out;
    e.Render(512, &out);
    for (float s : out) ASSERT_EQ(s, 0.0f);

    // Start a long fade, then pause the bus / the voice mid-fade.
    una_voice d = play(4);
    una_voice f = play(5);
    e.Render(512);
    ASSERT_EQ(una_voice_stop(d, 0, 2.0f), UNA_OK);
    ASSERT_EQ(una_voice_stop(f, 0, 2.0f), UNA_OK);
    e.Render(256);
    ASSERT_EQ(una_bus_set_paused(4, 1, 0), UNA_OK);
    ASSERT_EQ(una_voice_pause(f, 0), UNA_OK);
    e.Render(256);
    ev = DrainEvents();
    EXPECT_TRUE(Has(ev, UNA_EVENT_VOICE_ENDED, d));
    EXPECT_TRUE(Has(ev, UNA_EVENT_VOICE_ENDED, f));
    una_bus_set_paused(4, 0, 0);
}

// CATCH_UP clamped at the clip end instead of continuing around the loop, so a layer
// joining a long-running loop came in out of phase.
TEST(Regression, CatchUpWrapsIntoLoopRegion) {
    ManualEngine e;
    una_clip c = Ready(MakeWavFloat(48000, 2, 1000, Ramp));
    e.Render(5120);
    una_play_params p = DefaultPlay();
    p.start_dsp_frame = 100;  // 5020 frames late
    p.flags = UNA_PLAY_CATCH_UP;
    p.loop = 1;
    p.loop_start = 200;
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    std::vector<float> out;
    e.Render(256, &out);
    // 0..1000 once, then (5020 - 1000) % 800 = 20 frames into [200, 1000).
    EXPECT_FLOAT_EQ(L(out, 0), Ramp(220, 0));
    EXPECT_FLOAT_EQ(L(out, 100), Ramp(320, 0));
}

// Turning looping on after the stream producer had already reached the clip end left
// the voice starving forever: the producer never produced again.
TEST(Regression, StreamLoopEnabledAfterProducerReachedEnd) {
    ManualEngine e;
    una_clip c = Ready(MakeWavFloat(48000, 2, 8000, Ramp), UNA_LOAD_STREAM);
    DrainEvents();
    una_play_params p = DefaultPlay();
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // producer reads all 8000 frames
    std::vector<float> out = RenderPaced(e, 2048);
    const auto ev = DrainEvents();
    const int64_t start = StartOf(ev, v);
    ASSERT_GE(start, 0);
    ASSERT_EQ(una_voice_set_loop(v, 1, 0, 0), UNA_OK);
    std::vector<float> more = RenderPaced(e, 16384);
    out.insert(out.end(), more.begin(), more.end());
    const auto ev2 = DrainEvents();
    EXPECT_TRUE(Has(ev2, UNA_EVENT_VOICE_LOOPED, v));
    EXPECT_FALSE(Has(ev2, UNA_EVENT_VOICE_ENDED, v));
    EXPECT_EQ(una_voice_get_state(v), UNA_VOICE_PLAYING);
    // Second pass through the clip plays the right frames on the right dsp frames.
    EXPECT_FLOAT_EQ(L(out, start + 8000 + 3000), Ramp(3000, 0));
    EXPECT_FLOAT_EQ(L(out, start + 8000 + 4000), Ramp(4000, 0));
}

// With the timed-command heap full, further scheduled commands were applied
// immediately while the API had returned OK.
TEST(Regression, FullScheduleHeapIsReportedNotAppliedEarly) {
    ManualEngine e;
    una_clip c = Ready(MakeWavFloat(48000, 2, 48000, Ramp));
    una_play_params p = DefaultPlay();
    p.loop = 1;
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    e.Render(256);
    int rc = UNA_OK;
    int accepted = 0;
    for (int i = 0; i < 9000 && rc == UNA_OK; ++i) {
        rc = una_voice_set_volume(v, 0.0f, 0.0f, 1000000000LL + i);
        if (rc == UNA_OK) ++accepted;
        if (i % 1000 == 999) e.Render(256);  // drain the command queue into the heap
    }
    EXPECT_EQ(rc, UNA_E_QUEUE_FULL);
    EXPECT_EQ(accepted, 8192);
    std::vector<float> out;
    e.Render(256, &out);
    EXPECT_NE(L(out, 128), 0.0f);  // none of the far-future mutes took effect
    una_stats st{};
    una_get_stats(&st);
    EXPECT_EQ(st.late_commands, 0);
}

// Suspending must stop the device and freeze the dsp timeline; resuming restarts it.
TEST(Regression, SuspendFreezesTimelineAndResumes) {
    una_config cfg{};
    cfg.struct_size = sizeof(cfg);
    cfg.device_type = UNA_DEVICE_NULL;
    cfg.sample_rate = 48000;
    cfg.buffer_frames = 256;
    ASSERT_EQ(una_init(&cfg), UNA_OK);
    auto mix = [] {
        una_clock c{};
        una_get_clock(&c);
        return c.mix_frame;
    };
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    EXPECT_GT(mix(), 0);
    ASSERT_EQ(una_set_suspended(1), UNA_OK);
    EXPECT_EQ(una_is_suspended(), 1);
    const int64_t frozen = mix();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    EXPECT_EQ(mix(), frozen);
    ASSERT_EQ(una_set_suspended(0), UNA_OK);
    EXPECT_EQ(una_is_suspended(), 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    EXPECT_GT(mix(), frozen + 1024);
    una_shutdown();
    EXPECT_EQ(una_set_suspended(1), UNA_E_NOT_INITIALIZED);
}

// A stream voice reclaimed while the stream thread was mid-Refill let the clip (and its
// copied bytes) be deleted under the decoder. Long Ogg clips keep the producer decoding
// when the voice is stopped; under ASan a missing clip reference shows up as a
// heap-use-after-free in stb_vorbis.
TEST(Regression, ReleasedStreamClipOutlivesItsStream) {
    ManualEngine e;
    const auto ogg = ReadFixture("tone_48k_stereo_3s.ogg");
    for (int round = 0; round < 40; ++round) {
        una_clip c = LoadClip(ogg, UNA_LOAD_STREAM);
        ASSERT_EQ(WaitClip(c, UNA_CLIP_LOADED), UNA_CLIP_LOADED);
        una_play_params p = DefaultPlay();
        una_voice v = 0;
        ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        for (int i = 0; i < 24; ++i) e.Render(1024);  // drain the ring so the producer keeps decoding
        ASSERT_EQ(una_clip_release(c), UNA_OK);
        ASSERT_EQ(una_voice_stop(v, 0, 0.0f), UNA_OK);
        e.Render(256);
        bool freed = false;
        for (int i = 0; i < 2000 && !freed; ++i) {
            una_update();  // reclaim + collect while the stream thread may still be refilling
            freed = Has(DrainEvents(), UNA_EVENT_CLIP_FREED, c);
            if (!freed) std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        ASSERT_TRUE(freed) << "round " << round;
    }
}

// Changing loop settings on a stream voice used to throw away the whole ring (an audible
// gap every time), and right after Play it even skipped the start of the clip. Now the
// stream only resyncs when the voice reaches a frame laid out for other settings.
TEST(Regression, StreamLoopChangeOnlyResyncsWhenNeeded) {
    ManualEngine e;
    una_clip c = Ready(MakeWavFloat(48000, 2, 96000, Ramp), UNA_LOAD_STREAM);
    DrainEvents();
    una_play_params p = DefaultPlay();
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    ASSERT_EQ(una_voice_set_loop(v, 1, 0, 0), UNA_OK);  // same frame as Play
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    std::vector<float> out = RenderPaced(e, 8192);
    const int64_t start = StartOf(DrainEvents(), v);
    ASSERT_GE(start, 0);
    EXPECT_FLOAT_EQ(L(out, start), Ramp(0, 0));
    EXPECT_FLOAT_EQ(L(out, start + 100), Ramp(100, 0));
    // Turning looping off far from the loop point must not interrupt playback.
    ASSERT_EQ(una_voice_set_loop(v, 0, 0, 0), UNA_OK);
    std::vector<float> more = RenderPaced(e, 8192);
    out.insert(out.end(), more.begin(), more.end());
    una_stats st{};
    una_get_stats(&st);
    EXPECT_EQ(st.starved_frames, 0);
    for (int64_t f = start; f < static_cast<int64_t>(out.size() / 2); f += 997)
        ASSERT_FLOAT_EQ(L(out, f), Ramp(static_cast<int>(f - start), 0)) << "frame " << f;
}

// Looping turned off after the producer already wrapped: the ring holds frames from the
// loop start, but the voice must continue past the old loop end with the right frames.
TEST(Regression, StreamLoopDisabledAfterWrapContinuesPastLoopEnd) {
    ManualEngine e;
    una_clip c = Ready(MakeWavFloat(48000, 2, 96000, Ramp), UNA_LOAD_STREAM);
    DrainEvents();
    una_play_params p = DefaultPlay();
    p.loop = 1;
    p.loop_end = 20000;
    p.start_frame = 12000;  // the producer wraps at 20000 within its 16k-frame ring
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::vector<float> out = RenderPaced(e, 4096);
    const int64_t start = StartOf(DrainEvents(), v);
    ASSERT_GE(start, 0);
    ASSERT_EQ(una_voice_set_loop(v, 0, 0, 0), UNA_OK);
    std::vector<float> more = RenderPaced(e, 16384);
    out.insert(out.end(), more.begin(), more.end());
    EXPECT_FALSE(Has(DrainEvents(), UNA_EVENT_VOICE_LOOPED, v));
    // Clip frame 20000 + k plays at dsp start + 8000 + k; allow one refill before checking.
    for (int k = 4096; k < 8000; k += 331)
        ASSERT_FLOAT_EQ(L(out, start + 8000 + k), Ramp(20000 + k, 0)) << "k " << k;
}

// A manual-device engine reported "suspended" but kept advancing its timeline.
TEST(Regression, SuspendedManualDeviceDoesNotAdvance) {
    ManualEngine e;
    e.Render(512);
    const int64_t before = e.MixFrame();
    ASSERT_EQ(una_set_suspended(1), UNA_OK);
    std::vector<float> out;
    e.Render(512, &out);
    EXPECT_EQ(e.MixFrame(), before);
    for (float s : out) ASSERT_EQ(s, 0.0f);
    ASSERT_EQ(una_set_suspended(0), UNA_OK);
    e.Render(512);
    EXPECT_EQ(e.MixFrame(), before + 512);
}

// Manual rendering on one thread while another restarts the engine used to touch
// deleted clips and voices.
TEST(Regression, ManualRenderRacingRestartIsSafe) {
    std::atomic<bool> stop{false};
    una_config cfg{};
    cfg.struct_size = sizeof(cfg);
    cfg.device_type = UNA_DEVICE_MANUAL;
    cfg.sample_rate = 48000;
    cfg.buffer_frames = 256;
    cfg.channels = 2;
    const auto wav = MakeWavFloat(48000, 2, 4800, Ramp);
    std::thread renderer([&] {
        std::vector<float> buf(256 * 2);
        while (!stop.load()) una_render(buf.data(), 256, 2);
    });
    for (int i = 0; i < 30; ++i) {
        ASSERT_EQ(una_init(&cfg), UNA_OK);
        una_clip c = LoadClip(wav);
        WaitClip(c, UNA_CLIP_READY);
        una_play_params p = DefaultPlay();
        una_voice v = 0;
        una_voice_play(c, &p, &v);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        una_shutdown();
    }
    stop.store(true);
    renderer.join();
}
