#include "TestUtil.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <random>
#include <thread>

using namespace test;

TEST(Stress, ConcurrentControlWithRealtimeMixer) {
    una_config cfg{};
    cfg.struct_size = sizeof(cfg);
    cfg.device_type = UNA_DEVICE_NULL;
    cfg.sample_rate = 48000;
    cfg.buffer_frames = 128;
    cfg.max_voices = 64;
    ASSERT_EQ(una_init(&cfg), UNA_OK);

    auto ogg = ReadFixture("tone_48k_stereo_3s.ogg");
    auto wav = MakeWavFloat(44100, 2, 4410, [](int i, int c) { return ((i + c) % 50) / 100.0f; });
    std::vector<una_clip> clips = {LoadClip(ogg), LoadClip(ogg, UNA_LOAD_STREAM), LoadClip(wav)};
    for (auto c : clips) ASSERT_EQ(WaitClip(c, UNA_CLIP_LOADED), UNA_CLIP_LOADED);

    std::atomic<bool> stop{false};
    std::atomic<int> plays{0};
    auto worker = [&](int seed) {
        std::mt19937 rng(static_cast<unsigned>(seed));
        std::vector<una_voice> mine;
        while (!stop.load()) {
            const int op = rng() % 10;
            if (op < 4 || mine.empty()) {
                una_play_params p = DefaultPlay();
                p.pitch = 0.5f + (rng() % 100) / 50.0f;
                p.loop = rng() % 4 == 0;
                p.bus = rng() % 4;
                una_clock clk{};
                una_get_clock(&clk);
                if (rng() % 2) p.start_dsp_frame = clk.mix_frame + rng() % 4800;
                una_voice v = 0;
                if (una_voice_play(clips[rng() % clips.size()], &p, &v) == UNA_OK) {
                    mine.push_back(v);
                    ++plays;
                }
            } else {
                una_voice v = mine[rng() % mine.size()];
                switch (op) {
                    case 4: una_voice_stop(v, 0, (rng() % 3) * 0.01f); break;
                    case 5: una_voice_set_volume(v, (rng() % 100) / 100.0f, 0.02f, 0); break;
                    case 6: una_voice_seek(v, rng() % 40000, 0); break;
                    case 7: una_voice_pause(v, 0); una_voice_resume(v, 0); break;
                    case 8: { una_voice_timing t{}; una_voice_get_timing(v, 0, &t); break; }
                    default: una_voice_set_pan(v, ((rng() % 200) - 100) / 100.0f, 0); break;
                }
                if (mine.size() > 32) mine.erase(mine.begin());
            }
            if (rng() % 64 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    std::vector<std::thread> ts;
    for (int i = 0; i < 4; ++i) ts.emplace_back(worker, 100 + i);
    std::thread housekeeping([&] {
        una_event buf[64];
        while (!stop.load()) {
            una_update();
            while (una_poll_events(buf, 64) > 0) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    std::this_thread::sleep_for(std::chrono::seconds(3));
    stop = true;
    for (auto& t : ts) t.join();
    housekeeping.join();

    una_stop_all(0.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    una_update();
    una_stats st{};
    ASSERT_EQ(una_get_stats(&st), UNA_OK);
    EXPECT_GT(plays.load(), 100);
    EXPECT_EQ(st.active_voices, 0);
    EXPECT_EQ(st.dropped_commands, 0);
    EXPECT_LT(st.cpu_load_peak, 1.0f);
    for (auto c : clips) una_clip_release(c);
    // A stream clip is freed only once the stream thread has dropped its last
    // reference to the stream, which can take one more update.
    for (int i = 0; i < 200; ++i) {
        una_update();
        una_get_stats(&st);
        if (st.live_clips == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_EQ(st.live_clips, 0);
    una_shutdown();
}

TEST(Stress, LoadReleaseChurn) {
    ManualEngine e;
    auto ogg = ReadFixture("sine_48k_stereo_100ms.ogg");
    std::vector<std::thread> ts;
    std::atomic<int> ok{0};
    for (int t = 0; t < 4; ++t)
        ts.emplace_back([&] {
            for (int i = 0; i < 50; ++i) {
                una_clip c = LoadClip(ogg, i % 2 ? UNA_LOAD_STREAM : UNA_LOAD_DECODE, UNA_SAMPLE_INT16);
                if (!c) continue;
                if (i % 3 == 0) WaitClip(c, UNA_CLIP_LOADED);
                if (una_clip_release(c) == UNA_OK) ++ok;
            }
        });
    for (auto& t : ts) t.join();
    EXPECT_EQ(ok.load(), 200);
    for (int i = 0; i < 200; ++i) {
        una_update();
        una_stats st{};
        una_get_stats(&st);
        if (st.live_clips == 0 && st.decode_jobs == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    una_stats st{};
    una_get_stats(&st);
    EXPECT_EQ(st.live_clips, 0);
    EXPECT_EQ(st.clip_memory_bytes, 0);
}
