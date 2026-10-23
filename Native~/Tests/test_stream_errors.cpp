// Stream voices that cannot decode report UNA_EVENT_VOICE_ERROR (then end).
// Failures are provoked by changing borrowed bytes after the clip was probed.

#include "TestUtil.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <thread>

using namespace test;

namespace {

struct Outcome {
    bool error = false, ended = false, started = false, looped = false, starved = false;
    int64_t errorValue = 0, errorDsp = -1, endDsp = -1;
};

void Collect(Outcome& o, una_voice v) {
    for (const auto& e : DrainEvents()) {
        if (e.handle != v) continue;
        if (e.type == UNA_EVENT_VOICE_ERROR) {
            EXPECT_FALSE(o.ended) << "VOICE_ERROR must precede VOICE_ENDED";
            o.error = true;
            o.errorValue = e.value;
            o.errorDsp = e.dsp_frame;
        } else if (e.type == UNA_EVENT_VOICE_ENDED) {
            o.ended = true;
            o.endDsp = e.dsp_frame;
        } else if (e.type == UNA_EVENT_VOICE_STARTED) {
            o.started = true;
        } else if (e.type == UNA_EVENT_VOICE_LOOPED) {
            o.looped = true;
        } else if (e.type == UNA_EVENT_VOICE_STARVED) {
            o.starved = true;
        }
    }
}

Outcome RenderUntilEnded(ManualEngine& e, una_voice v, Outcome o = Outcome()) {
    for (int i = 0; i < 400 && !o.ended; ++i) {
        e.Render(256);
        Collect(o, v);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return o;
}

} // namespace

TEST(StreamErrors, OpenFailureReportsFormatError) {
    ManualEngine e;
    ASSERT_TRUE(e.ok());
    std::vector<uint8_t> bytes = ReadFixture("tone_48k_stereo_3s.ogg");
    ASSERT_FALSE(bytes.empty());
    una_clip c = LoadClip(bytes, UNA_LOAD_STREAM, UNA_SAMPLE_FLOAT32, UNA_CLIP_FLAG_BORROW);
    ASSERT_EQ(WaitClip(c, UNA_CLIP_LOADED), UNA_CLIP_LOADED);
    // The clip was probed; the voice's own decoder opens later on the stream thread.
    std::fill(bytes.begin(), bytes.end(), uint8_t(0));
    DrainEvents();

    una_play_params p = DefaultPlay();
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    const Outcome o = RenderUntilEnded(e, v);

    EXPECT_TRUE(o.error);
    EXPECT_EQ(o.errorValue, UNA_E_FORMAT);
    EXPECT_TRUE(o.ended);
    EXPECT_EQ(o.errorDsp, o.endDsp);
    EXPECT_FALSE(o.started) << "an immediate stream voice must not start without data";
    una_clip_release(c);
    una_update();
}

TEST(StreamErrors, SeekFailureReportsDecodeError) {
    ManualEngine e;
    ASSERT_TRUE(e.ok());
    std::vector<uint8_t> bytes = ReadFixture("tone_48k_stereo_3s.ogg");
    una_clip c = LoadClip(bytes, UNA_LOAD_STREAM, UNA_SAMPLE_FLOAT32, UNA_CLIP_FLAG_BORROW);
    ASSERT_EQ(WaitClip(c, UNA_CLIP_LOADED), UNA_CLIP_LOADED);

    // Scheduled far ahead: the producer opens the decoder, fills the ring and then idles.
    una_play_params p = DefaultPlay();
    p.start_dsp_frame = 48000 * 60;
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    e.Render(256);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // Stats takes the stream service lock, ordering the producer's last reads before our writes.
    una_stats st{};
    una_get_stats(&st);
    std::fill(bytes.begin(), bytes.end(), uint8_t(0));
    DrainEvents();

    ASSERT_EQ(una_voice_seek(v, 48000, 0), UNA_OK);
    const Outcome o = RenderUntilEnded(e, v);

    EXPECT_TRUE(o.error);
    EXPECT_EQ(o.errorValue, UNA_E_DECODE);
    EXPECT_TRUE(o.ended);
    EXPECT_EQ(o.errorDsp, o.endDsp);
    una_clip_release(c);
    una_update();
}

TEST(StreamErrors, FailureAfterDataEndsRightAfterTheLastDecodedFrame) {
    ManualEngine e;
    ASSERT_TRUE(e.ok());
    std::vector<uint8_t> bytes = ReadFixture("tone_48k_stereo_3s.ogg");
    una_clip c = LoadClip(bytes, UNA_LOAD_STREAM, UNA_SAMPLE_FLOAT32, UNA_CLIP_FLAG_BORROW);
    ASSERT_EQ(WaitClip(c, UNA_CLIP_LOADED), UNA_CLIP_LOADED);

    // The ring (16 blocks of 1024 frames) fills exactly up to loop_end, so the producer
    // stops just before the wrap; the wrap's seek runs after the bytes are gone and fails.
    constexpr int64_t kLoopEnd = 72000, kRing = 16 * 1024;
    constexpr int64_t kStart = 4196;  // not on a 256-frame render boundary
    una_play_params p = DefaultPlay();
    p.start_dsp_frame = kStart;
    p.start_frame = kLoopEnd - kRing;
    p.loop = 1;
    p.loop_start = 60000;
    p.loop_end = kLoopEnd;
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    una_stats st{};
    una_get_stats(&st);  // orders the producer's last reads before the writes below
    std::fill(bytes.begin(), bytes.end(), uint8_t(0));

    // Free a few blocks so the producer attempts the wrap (and fails) long before the ring runs dry.
    Outcome o;
    e.Render(8192);
    Collect(o, v);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    o = RenderUntilEnded(e, v, o);

    EXPECT_TRUE(o.started);
    EXPECT_TRUE(o.error);
    EXPECT_EQ(o.errorValue, UNA_E_DECODE);
    EXPECT_TRUE(o.ended);
    EXPECT_EQ(o.errorDsp, kStart + kRing) << "ends right after the last decoded frame";
    EXPECT_EQ(o.endDsp, kStart + kRing);
    EXPECT_FALSE(o.looped) << "the wrap never happened";
    EXPECT_FALSE(o.starved) << "running out of data after a failure is the end, not starvation";
    una_clip_release(c);
    una_update();
}

TEST(StreamErrors, HealthyStreamReportsNoError) {
    ManualEngine e;
    auto bytes = ReadFixture("sine_48k_stereo_100ms.ogg");
    una_clip c = LoadClip(bytes, UNA_LOAD_STREAM);
    ASSERT_EQ(WaitClip(c, UNA_CLIP_LOADED), UNA_CLIP_LOADED);
    DrainEvents();
    una_play_params p = DefaultPlay();
    una_voice v = 0;
    ASSERT_EQ(una_voice_play(c, &p, &v), UNA_OK);
    const Outcome o = RenderUntilEnded(e, v);
    EXPECT_TRUE(o.started);
    EXPECT_TRUE(o.ended);
    EXPECT_FALSE(o.error);
}
