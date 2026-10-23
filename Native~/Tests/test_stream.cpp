#include "TestUtil.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace test;

namespace {

// Renders slowly enough that the stream service keeps the ring filled.
std::vector<float> RenderPaced(ManualEngine& e, int frames) {
    std::vector<float> out;
    for (int done = 0; done < frames; done += 256) {
        e.Render(256, &out);
        std::this_thread::sleep_for(std::chrono::microseconds(300));
    }
    return out;
}

int StartFrame(una_voice v) {
    for (const auto& ev : DrainEvents())
        if (ev.type == UNA_EVENT_VOICE_STARTED && ev.handle == v) return static_cast<int>(ev.dsp_frame);
    return -1;
}

} // namespace

TEST(Stream, MatchesDecodedPlaybackExactly) {
    ManualEngine e;
    auto ogg = ReadFixture("tone_48k_stereo_3s.ogg");
    una_clip dec = LoadClip(ogg, UNA_LOAD_DECODE, UNA_SAMPLE_FLOAT32);
    una_clip str = LoadClip(ogg, UNA_LOAD_STREAM);
    ASSERT_EQ(WaitClip(dec, UNA_CLIP_LOADED), UNA_CLIP_LOADED);
    ASSERT_EQ(WaitClip(str, UNA_CLIP_LOADED), UNA_CLIP_LOADED);
    DrainEvents();

    una_play_params p = DefaultPlay();
    p.start_dsp_frame = 4096;  // scheduled: both start on the same frame
    una_voice vd = 0, vs = 0;
    ASSERT_EQ(una_voice_play(dec, &p, &vd), UNA_OK);
    p.bus = 1;
    ASSERT_EQ(una_voice_play(str, &p, &vs), UNA_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // prime
    una_bus_set_volume(1, 0.0f, 0.0f);  // render decoded only first
    std::vector<float> a = RenderPaced(e, 4096 + 48000);
    una_shutdown();

    ManualEngine e2;
    dec = LoadClip(ogg, UNA_LOAD_DECODE, UNA_SAMPLE_FLOAT32);
    str = LoadClip(ogg, UNA_LOAD_STREAM);
    ASSERT_EQ(WaitClip(str, UNA_CLIP_LOADED), UNA_CLIP_LOADED);
    p = DefaultPlay();
    p.start_dsp_frame = 4096;
    ASSERT_EQ(una_voice_play(str, &p, &vs), UNA_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::vector<float> b = RenderPaced(e2, 4096 + 48000);

    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) ASSERT_FLOAT_EQ(a[i], b[i]) << "sample " << i;
    una_stats st{};
    una_get_stats(&st);
    EXPECT_EQ(st.starved_frames, 0);
}

TEST(Stream, ImmediatePlayWaitsForDataThenStarts) {
    ManualEngine e;
    una_clip str = LoadClip(ReadFixture("tone_48k_stereo_3s.ogg"), UNA_LOAD_STREAM);
    ASSERT_EQ(WaitClip(str, UNA_CLIP_LOADED), UNA_CLIP_LOADED);
    DrainEvents();
    una_play_params p = DefaultPlay();
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(str, &p, &v), UNA_OK);
    auto out = RenderPaced(e, 48000);
    const int start = StartFrame(v);
    ASSERT_GE(start, 0);
    double energy = 0;
    for (size_t i = size_t(start) * 2; i < out.size(); ++i) energy += out[i] * out[i];
    EXPECT_GT(energy, 1.0);
    una_stats st{};
    una_get_stats(&st);
    EXPECT_EQ(st.starved_frames, 0);
}

TEST(Stream, SeekAndLoopFollowDecodedReference) {
    auto ogg = ReadFixture("tone_48k_stereo_3s.ogg");
    auto run = [&](int mode) {
        ManualEngine e;
        una_clip c = LoadClip(ogg, mode, UNA_SAMPLE_FLOAT32);
        EXPECT_EQ(WaitClip(c, UNA_CLIP_LOADED), UNA_CLIP_LOADED);
        una_play_params p = DefaultPlay();
        p.start_dsp_frame = 2048;
        p.loop = 1;
        p.loop_start = 1000;
        p.loop_end = 9000;
        una_voice v = 0;
        EXPECT_EQ(una_voice_play(c, &p, &v), UNA_OK);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto out = RenderPaced(e, 30000);
        una_voice_seek(v, 5000, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));  // stream refills after seek
        auto more = RenderPaced(e, 30000);
        out.insert(out.end(), more.begin(), more.end());
        return out;
    };
    auto a = run(UNA_LOAD_DECODE);
    auto b = run(UNA_LOAD_STREAM);
    ASSERT_EQ(a.size(), b.size());
    // Before the seek the outputs are identical, loop wraps included.
    for (size_t i = 0; i < 2 * size_t(30000); ++i) ASSERT_FLOAT_EQ(a[i], b[i]) << i;
    // After the seek the stream may need one refill; it must then be sample-aligned again.
    size_t mismatches = 0;
    for (size_t i = 2 * size_t(30000) + 2 * 4096; i < a.size(); ++i) mismatches += a[i] != b[i];
    EXPECT_EQ(mismatches, 0u);
}
