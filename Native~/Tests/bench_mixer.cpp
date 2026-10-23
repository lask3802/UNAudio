// Mixer throughput benchmark on the MANUAL device (no audio hardware, no pacing).
// Usage: una_bench [voices=64] [seconds=10] [buffer=256]
// Prints render time per buffer and the real-time factor for several voice mixes.

#include "unaudio.h"
#include "Engine/MixKernels.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace {

std::vector<uint8_t> MakeWav(int rate, int channels, int frames) {
    std::vector<uint8_t> w(44 + size_t(frames) * channels * 2);
    auto put32 = [&](size_t o, uint32_t v) { std::memcpy(&w[o], &v, 4); };
    auto put16 = [&](size_t o, uint16_t v) { std::memcpy(&w[o], &v, 2); };
    std::memcpy(&w[0], "RIFF", 4);
    put32(4, uint32_t(w.size() - 8));
    std::memcpy(&w[8], "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, uint16_t(channels));
    put32(24, uint32_t(rate));
    put32(28, uint32_t(rate * channels * 2));
    put16(32, uint16_t(channels * 2));
    put16(34, 16);
    std::memcpy(&w[36], "data", 4);
    put32(40, uint32_t(frames * channels * 2));
    for (int i = 0; i < frames * channels; ++i) {
        const int16_t s = int16_t(8000.0 * std::sin(i * 0.031));
        std::memcpy(&w[44 + size_t(i) * 2], &s, 2);
    }
    return w;
}

una_clip Load(const std::vector<uint8_t>& data) {
    una_clip_params p{};
    p.struct_size = sizeof(p);
    p.mode = UNA_LOAD_DECODE;
    p.sample_format = UNA_SAMPLE_INT16;
    una_clip c = 0;
    una_clip_load_memory(data.data(), int64_t(data.size()), &p, &c);
    una_clip_info info{};
    for (int i = 0; i < 2000 && (una_clip_get_info(c, &info), info.state != UNA_CLIP_LOADED); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return c;
}

double Run(const char* label, int voices, int seconds, int buffer, float pitch, una_clip clip) {
    una_stop_all(0.0f);
    std::vector<float> out(size_t(buffer) * 2);
    una_render(out.data(), buffer, 2);  // apply the stop
    una_update();
    for (int i = 0; i < voices; ++i) {
        una_play_params p{};
        p.struct_size = sizeof(p);
        p.volume = 1.0f / voices;
        p.pitch = pitch;
        p.loop = 1;
        p.pan = (i % 7 - 3) / 3.0f;
        una_voice v;
        una_voice_play(clip, &p, &v);
    }
    const int blocks = seconds * 48000 / buffer;
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b) una_render(out.data(), buffer, 2);
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double perBufferUs = sec * 1e6 / blocks;
    const double budgetUs = buffer * 1e6 / 48000.0;
    std::printf("%-28s voices=%3d  %7.2f us/buffer (budget %.0f us)  load %5.2f%%  %6.0fx real time\n", label,
                voices, perBufferUs, budgetUs, 100.0 * perBufferUs / budgetUs, budgetUs / perBufferUs);
    return perBufferUs;
}

} // namespace

int main(int argc, char** argv) {
    const int voices = argc > 1 ? std::atoi(argv[1]) : 64;
    const int seconds = argc > 2 ? std::atoi(argv[2]) : 10;
    const int buffer = argc > 3 ? std::atoi(argv[3]) : 256;

    una_config cfg{};
    cfg.struct_size = sizeof(cfg);
    cfg.device_type = UNA_DEVICE_MANUAL;
    cfg.sample_rate = 48000;
    cfg.buffer_frames = buffer;
    cfg.max_voices = voices > 128 ? voices : 128;
    if (una_init(&cfg) != UNA_OK) {
        std::printf("init failed\n");
        return 1;
    }
    auto stereo = MakeWav(48000, 2, 48000 * 4);
    auto mono = MakeWav(48000, 1, 48000 * 4);
    auto other = MakeWav(44100, 2, 44100 * 4);
    una_clip cs = Load(stereo), cm = Load(mono);
    una_clip_params np{};
    np.struct_size = sizeof(np);
    np.flags = UNA_CLIP_FLAG_NO_RESAMPLE;
    una_clip co = 0;
    una_clip_load_memory(other.data(), int64_t(other.size()), &np, &co);
    una_clip_info info{};
    while (una_clip_get_info(co, &info), info.state != UNA_CLIP_LOADED)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    std::printf("kernels: %s\n", una::mix::IsaName());
    Run("stereo int16, 1:1 fast path", voices, seconds, buffer, 1.0f, cs);
    Run("mono int16, 1:1 fast path", voices, seconds, buffer, 1.0f, cm);
    Run("stereo, pitch 1.37 (cubic)", voices, seconds, buffer, 1.37f, cs);
    Run("44.1k stereo live resample", voices, seconds, buffer, 1.0f, co);

    // Worker-side cost: decode 60 s of 44.1 kHz stereo with resampling to 48 kHz.
    auto long441 = MakeWav(44100, 2, 44100 * 60);
    una_clip_params dp{};
    dp.struct_size = sizeof(dp);
    const auto d0 = std::chrono::steady_clock::now();
    una_clip cd = 0;
    una_clip_load_memory(long441.data(), int64_t(long441.size()), &dp, &cd);
    while (una_clip_get_info(cd, &info), info.state != UNA_CLIP_LOADED && info.state != UNA_CLIP_FAILED)
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    const double ds = std::chrono::duration<double>(std::chrono::steady_clock::now() - d0).count();
    std::printf("%-28s 60 s of audio in %.1f ms (%.0fx real time)\n", "decode 44.1k->48k resample", ds * 1e3,
                60.0 / ds);
    una_shutdown();
    return 0;
}
