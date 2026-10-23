// Plays a 440 Hz tone through the platform device and prints what the
// backend negotiated plus per-callback timing statistics.
//
//   una_device_probe [seconds] [buffer_frames] [exclusive] [null]

#include "Core/Platform.h"
#include "Platform/AudioDevice.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

using namespace una;

namespace {

class Tone final : public DeviceCallback {
public:
    void OnRender(float* out, int frames, int channels, int64_t presentNs) override {
        const int64_t now = HostTimeNs();
        if (rate_ <= 0) rate_ = 48000;
        const double inc = 2.0 * 3.14159265358979 * 440.0 / rate_;
        for (int i = 0; i < frames; ++i) {
            const float s = 0.15f * static_cast<float>(std::sin(phase_));
            phase_ += inc;
            if (phase_ > 6.283185307179586) phase_ -= 6.283185307179586;
            for (int c = 0; c < channels; ++c) out[i * channels + c] = s;
        }
        callbacks.fetch_add(1);
        frameCount.fetch_add(frames);
        if (minFrames == 0 || frames < minFrames) minFrames = frames;
        if (frames > maxFrames) maxFrames = frames;
        if (presentNs > 0) {
            const double lat = (presentNs - now) * 1e-6;
            latSum += lat;
            ++latCount;
            if (lat < latMin) latMin = lat;
            if (lat > latMax) latMax = lat;
        }
        if (lastNs) {
            const double gap = (now - lastNs) * 1e-6;
            if (gap > gapMax) gapMax = gap;
        }
        lastNs = now;
    }
    void OnDeviceChanged(const DeviceInfo& info) override {
        rate_ = info.sampleRate;
        changes.fetch_add(1);
        std::printf("device changed: %d Hz, %d ch, %d frames\n", info.sampleRate, info.channels, info.bufferFrames);
    }

    int rate_ = 0;
    double phase_ = 0.0;
    std::atomic<int64_t> callbacks{0}, frameCount{0};
    std::atomic<int> changes{0};
    int minFrames = 0, maxFrames = 0;
    double latSum = 0, latMin = 1e9, latMax = -1e9, gapMax = 0;
    int64_t latCount = 0;
    int64_t lastNs = 0;
};

} // namespace

int main(int argc, char** argv) {
    const double seconds = argc > 1 ? std::atof(argv[1]) : 2.0;
    DeviceConfig cfg;
    cfg.bufferFrames = argc > 2 ? std::atoi(argv[2]) : 0;
    cfg.exclusive = argc > 3 && std::atoi(argv[3]) != 0;
    const bool useNull = argc > 4 && std::strcmp(argv[4], "null") == 0;

    auto dev = useNull ? CreateNullDevice() : CreatePlatformDevice();
    if (!dev) { std::printf("no device backend\n"); return 2; }
    Tone tone;
    if (!dev->Open(cfg, &tone)) { std::printf("Open failed\n"); return 3; }
    DeviceInfo info = dev->Info();
    tone.rate_ = info.sampleRate;
    std::printf("backend=%d rate=%d ch=%d period=%d latency=%d frames (%.2f ms) exclusive=%d\n",
                info.backend, info.sampleRate, info.channels, info.bufferFrames, info.latencyFrames,
                info.sampleRate ? info.latencyFrames * 1000.0 / info.sampleRate : 0.0, info.exclusive ? 1 : 0);
    if (!dev->Start()) { std::printf("Start failed\n"); return 4; }
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(seconds * 1000)));
    dev->Stop();
    dev->Close();

    const double expected = seconds * info.sampleRate;
    std::printf("callbacks=%lld frames=%lld (expected ~%.0f) per-callback=[%d..%d] max-gap=%.2f ms\n",
                (long long)tone.callbacks.load(), (long long)tone.frameCount.load(), expected,
                tone.minFrames, tone.maxFrames, tone.gapMax);
    if (tone.latCount)
        std::printf("present-now latency ms: avg=%.2f min=%.2f max=%.2f\n", tone.latSum / tone.latCount,
                    tone.latMin, tone.latMax);
    else
        std::printf("present-now latency: not reported\n");
    return tone.callbacks.load() > 0 ? 0 : 5;
}
