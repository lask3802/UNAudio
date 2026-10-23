// Exercises the platform output device and reports what it negotiated.
//
//   una_device_probe [seconds] [buffer_frames] [exclusive] [null] [restart_ms]
//       Plays a 440 Hz tone through the raw backend and prints per-callback
//       timing: callback sizes and gaps, presentNs - now, and how often the
//       backend's presentation anchor (presentNs - frame / rate) moved. With
//       restart_ms > 0 the device is stopped for that long and played for
//       another 2 s.
//
//   una_device_probe engine [seconds] [clip_path] [suspend_ms]
//       Runs the whole engine on the default device: loads clip_path (or a
//       generated tone), loops it, samples una_get_clock and the voice timing
//       every 5 ms, then suspends and resumes the output. Prints the audible
//       clock's rate and jitter, latency, discontinuities and engine stats, and
//       PASS/FAIL lines; exits non-zero if a check fails.

#include "Core/Platform.h"
#include "Platform/AudioDevice.h"
#include "unaudio.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

using namespace una;

namespace {

const char* BackendName(int b) {
    switch (b) {
    case UNA_BACKEND_MANUAL: return "manual";
    case UNA_BACKEND_NULL: return "null";
    case UNA_BACKEND_WASAPI: return "WASAPI";
    case UNA_BACKEND_COREAUDIO: return "CoreAudio";
    case UNA_BACKEND_AAUDIO: return "AAudio";
    case UNA_BACKEND_OPENSLES: return "OpenSL ES";
    case UNA_BACKEND_ALSA: return "ALSA";
    default: return "none";
    }
}

void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

double Percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(p * (v.size() - 1))];
}

// ── Raw device mode ────────────────────────────────────────────────

struct CallbackRec {
    int64_t nowNs;
    int64_t presentNs;
    int64_t frame; // frames rendered before this callback
    int frames;
};

class Tone final : public DeviceCallback {
public:
    explicit Tone(size_t capacity) { recs.resize(capacity); }

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
        const int64_t n = callbacks.load(std::memory_order_relaxed);
        if (n < static_cast<int64_t>(recs.size())) recs[static_cast<size_t>(n)] = {now, presentNs, frame_, frames};
        frame_ += frames;
        callbacks.store(n + 1, std::memory_order_release);
    }
    void OnDeviceChanged(const DeviceInfo& info) override {
        rate_ = info.sampleRate;
        changes.fetch_add(1);
        std::printf("device changed: %d Hz, %d ch, %d frames\n", info.sampleRate, info.channels, info.bufferFrames);
    }

    int rate_ = 0;
    double phase_ = 0.0;
    int64_t frame_ = 0;
    std::atomic<int64_t> callbacks{0};
    std::atomic<int> changes{0};
    std::vector<CallbackRec> recs;
};

int RunDevice(int argc, char** argv) {
    const double seconds = argc > 1 ? std::atof(argv[1]) : 2.0;
    DeviceConfig cfg;
    cfg.bufferFrames = argc > 2 ? std::atoi(argv[2]) : 0;
    cfg.exclusive = argc > 3 && std::atoi(argv[3]) != 0;
    const bool useNull = argc > 4 && std::strcmp(argv[4], "null") == 0;
    const int restartMs = argc > 5 ? std::atoi(argv[5]) : 0;
    int64_t restartAt = -1;

    auto dev = useNull ? CreateNullDevice() : CreatePlatformDevice();
    if (!dev) { std::printf("no device backend\n"); return 2; }
    Tone tone(static_cast<size_t>(seconds * 4000) + 1024); // up to 4 kHz callbacks
    if (!dev->Open(cfg, &tone)) { std::printf("Open failed\n"); return 3; }
    const DeviceInfo info = dev->Info();
    tone.rate_ = info.sampleRate;
    std::printf("backend=%d (%s) rate=%d ch=%d period=%d latency=%d frames (%.2f ms) exclusive=%d\n", info.backend,
                BackendName(info.backend), info.sampleRate, info.channels, info.bufferFrames, info.latencyFrames,
                info.sampleRate ? info.latencyFrames * 1000.0 / info.sampleRate : 0.0, info.exclusive ? 1 : 0);
    if (!dev->Start()) { std::printf("Start failed\n"); return 4; }
    SleepMs(static_cast<int>(seconds * 1000));
    dev->Stop();
    if (restartMs > 0) {
        // Like una_set_suspended: the frame count continues, presentation does not.
        restartAt = tone.callbacks.load(std::memory_order_acquire);
        SleepMs(restartMs);
        if (!dev->Start()) { std::printf("restart failed\n"); return 4; }
        SleepMs(2000);
        dev->Stop();
    }
    const DeviceInfo after = dev->Info(); // AAudio refines the latency from its timestamps
    dev->Close();

    const int64_t count = std::min<int64_t>(tone.callbacks.load(std::memory_order_acquire),
                                            static_cast<int64_t>(tone.recs.size()));
    const int rate = after.sampleRate > 0 ? after.sampleRate : 48000;
    int minFrames = 0, maxFrames = 0, unknown = 0, anchorMoves = 0;
    double maxMoveMs = 0.0;
    int64_t lastMoveIndex = -1;
    std::vector<double> gaps;
    for (int64_t i = 0; i < count; ++i) {
        const CallbackRec& r = tone.recs[static_cast<size_t>(i)];
        if (minFrames == 0 || r.frames < minFrames) minFrames = r.frames;
        if (r.frames > maxFrames) maxFrames = r.frames;
        if (i > 0) gaps.push_back((r.nowNs - tone.recs[static_cast<size_t>(i - 1)].nowNs) * 1e-6);
        if (r.presentNs <= 0) { ++unknown; continue; }
        if (i == 0 || tone.recs[static_cast<size_t>(i - 1)].presentNs <= 0) continue;
        // Host time of frame 0 implied by this callback vs the previous one.
        const CallbackRec& p = tone.recs[static_cast<size_t>(i - 1)];
        const double anchor = r.presentNs - r.frame * 1e9 / rate;
        const double prevAnchor = p.presentNs - p.frame * 1e9 / rate;
        const double moveMs = std::fabs(anchor - prevAnchor) * 1e-6;
        if (i == restartAt) {
            std::printf("  restart: anchor moved %+.2f ms at callback %lld (t=%.3f s)\n", (anchor - prevAnchor) * 1e-6,
                        (long long)i, (r.nowNs - tone.recs[0].nowNs) * 1e-9);
            lastMoveIndex = i;
            continue;
        }
        if (moveMs > 0.5) {
            ++anchorMoves;
            lastMoveIndex = i;
            if (anchorMoves <= 8)
                std::printf("  anchor moved %+.2f ms at callback %lld (t=%.3f s)\n", (anchor - prevAnchor) * 1e-6,
                            (long long)i, (r.nowNs - tone.recs[0].nowNs) * 1e-9);
        }
        if (moveMs > maxMoveMs) maxMoveMs = moveMs;
    }
    // presentNs - now once the anchor stopped moving.
    std::vector<double> lat;
    for (int64_t i = lastMoveIndex < 0 ? 0 : lastMoveIndex; i < count; ++i) {
        const CallbackRec& r = tone.recs[static_cast<size_t>(i)];
        if (r.presentNs > 0) lat.push_back((r.presentNs - r.nowNs) * 1e-6);
    }

    const int64_t frames = tone.frame_;
    std::printf("callbacks=%lld frames=%lld (expected ~%.0f) per-callback=[%d..%d] gap ms p50=%.2f p99=%.2f max=%.2f\n",
                (long long)tone.callbacks.load(), (long long)frames,
                (seconds + (restartMs > 0 ? 2.0 : 0.0)) * info.sampleRate, minFrames, maxFrames,
                Percentile(gaps, 0.5), Percentile(gaps, 0.99), Percentile(gaps, 1.0));
    std::printf("latency after first timestamp: %d frames (%.2f ms)\n", after.latencyFrames,
                after.latencyFrames * 1000.0 / rate);
    std::printf("presentNs unknown in %d callbacks; anchor moved %d times (max %.2f ms)\n", unknown, anchorMoves,
                maxMoveMs);
    if (!lat.empty())
        std::printf("present-now latency ms (steady): min=%.2f p50=%.2f max=%.2f over %zu callbacks\n",
                    Percentile(lat, 0.0), Percentile(lat, 0.5), Percentile(lat, 1.0), lat.size());
    else
        std::printf("present-now latency: not reported\n");
    return tone.callbacks.load() > 0 ? 0 : 5;
}

// ── Engine mode ────────────────────────────────────────────────────

std::vector<uint8_t> MakeToneWav(int rate, int seconds) {
    const int frames = rate * seconds;
    std::vector<uint8_t> w(44 + static_cast<size_t>(frames) * 4);
    auto put32 = [&](size_t o, uint32_t v) { std::memcpy(&w[o], &v, 4); };
    auto put16 = [&](size_t o, uint16_t v) { std::memcpy(&w[o], &v, 2); };
    std::memcpy(&w[0], "RIFF", 4);
    put32(4, static_cast<uint32_t>(w.size() - 8));
    std::memcpy(&w[8], "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, 2);
    put32(24, static_cast<uint32_t>(rate));
    put32(28, static_cast<uint32_t>(rate * 4));
    put16(32, 4);
    put16(34, 16);
    std::memcpy(&w[36], "data", 4);
    put32(40, static_cast<uint32_t>(frames * 4));
    for (int i = 0; i < frames; ++i) {
        const int16_t s = static_cast<int16_t>(5000.0 * std::sin(2.0 * 3.14159265358979 * 440.0 * i / rate));
        std::memcpy(&w[44 + static_cast<size_t>(i) * 4], &s, 2);
        std::memcpy(&w[46 + static_cast<size_t>(i) * 4], &s, 2);
    }
    return w;
}

struct ClockSample {
    una_clock clock;
    double voiceAudible; // source frame
    int voiceState;
};

int failures = 0;
void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// Samples the clock every 5 ms and reports how evenly the audible frame advances.
std::vector<ClockSample> SampleClock(una_voice voice, double seconds) {
    std::vector<ClockSample> out;
    out.reserve(static_cast<size_t>(seconds * 200) + 16);
    const int64_t until = una_host_time_ns() + static_cast<int64_t>(seconds * 1e9);
    while (una_host_time_ns() < until) {
        ClockSample s{};
        una_get_clock(&s.clock);
        una_voice_timing t{};
        una_voice_get_timing(voice, s.clock.host_ns, &t);
        s.voiceAudible = t.audible_frame;
        s.voiceState = t.state;
        out.push_back(s);
        una_update();
        SleepMs(5);
    }
    return out;
}

struct ClockReport {
    double ppm = 0, jitterMaxMs = 0, jitterP99Ms = 0, lagMinMs = 0, lagMaxMs = 0, lagP50Ms = 0;
    int backwards = 0;
    int64_t discontinuities = 0;
};

// Steady state = samples after the last discontinuity before `skipSeconds`.
ClockReport Analyze(const std::vector<ClockSample>& v, double skipSeconds) {
    ClockReport r;
    if (v.size() < 3) return r;
    const int rate = v.front().clock.sample_rate;
    const int64_t t0 = v.front().clock.host_ns;
    std::vector<double> jitter, lag;
    size_t first = 0;
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i].clock.host_ns - t0 < static_cast<int64_t>(skipSeconds * 1e9)) first = i;
    r.discontinuities = v.back().clock.discontinuities - v[first].clock.discontinuities;
    for (size_t i = first + 1; i < v.size(); ++i) {
        const una_clock& a = v[i - 1].clock;
        const una_clock& b = v[i].clock;
        if (b.discontinuities != a.discontinuities) continue;
        const double expected = (b.host_ns - a.host_ns) * 1e-9 * rate;
        const double got = b.audible_frame - a.audible_frame;
        if (got < 0) ++r.backwards;
        jitter.push_back(std::fabs(got - expected) * 1000.0 / rate);
        lag.push_back((b.mix_frame - b.audible_frame) * 1000.0 / rate);
    }
    const una_clock& a = v[first].clock;
    const una_clock& b = v.back().clock;
    const double expected = (b.host_ns - a.host_ns) * 1e-9 * rate;
    if (expected > 0) r.ppm = ((b.audible_frame - a.audible_frame) / expected - 1.0) * 1e6;
    r.jitterMaxMs = Percentile(jitter, 1.0);
    r.jitterP99Ms = Percentile(jitter, 0.99);
    r.lagMinMs = Percentile(lag, 0.0);
    r.lagP50Ms = Percentile(lag, 0.5);
    r.lagMaxMs = Percentile(lag, 1.0);
    return r;
}

void PrintReport(const char* label, const ClockReport& r) {
    std::printf("%s: audible rate %+.0f ppm vs nominal, step error max=%.3f ms p99=%.3f ms, backwards=%d, "
                "mix-audible lag ms min=%.2f p50=%.2f max=%.2f, discontinuities=%lld\n",
                label, r.ppm, r.jitterMaxMs, r.jitterP99Ms, r.backwards, r.lagMinMs, r.lagP50Ms, r.lagMaxMs,
                (long long)r.discontinuities);
}

int RunEngine(int argc, char** argv) {
    const double seconds = argc > 2 ? std::atof(argv[2]) : 5.0;
    const char* path = argc > 3 && std::strcmp(argv[3], "-") != 0 ? argv[3] : nullptr;
    const int suspendMs = argc > 4 ? std::atoi(argv[4]) : 1000;

    una_config cfg{};
    cfg.struct_size = sizeof(cfg);
    cfg.device_type = UNA_DEVICE_DEFAULT;
    const int64_t initStart = una_host_time_ns();
    const int rc = una_init(&cfg);
    if (rc != UNA_OK) { std::printf("una_init failed: %d\n", rc); return 3; }
    una_device_info di{};
    una_get_device_info(&di);
    std::printf("engine: backend=%d (%s) rate=%d ch=%d period=%d latency=%d frames (%.2f ms) exclusive=%d "
                "init=%.1f ms\n",
                di.backend, BackendName(di.backend), di.sample_rate, di.channels, di.buffer_frames, di.latency_frames,
                di.sample_rate ? di.latency_frames * 1000.0 / di.sample_rate : 0.0, di.exclusive,
                (una_host_time_ns() - initStart) * 1e-6);

    una_clip_params cp{};
    cp.struct_size = sizeof(cp);
    cp.mode = UNA_LOAD_DECODE;
    una_clip clip = 0;
    std::vector<uint8_t> wav;
    if (path) {
        if (una_clip_load_file(path, &cp, &clip) != UNA_OK) { std::printf("load %s failed\n", path); return 6; }
    } else {
        wav = MakeToneWav(48000, 2);
        una_clip_load_memory(wav.data(), static_cast<int64_t>(wav.size()), &cp, &clip);
    }
    una_clip_info ci{};
    const int64_t loadStart = una_host_time_ns();
    while (una_clip_get_info(clip, &ci), ci.state != UNA_CLIP_LOADED && ci.state != UNA_CLIP_FAILED &&
                                             una_host_time_ns() - loadStart < 5000000000LL)
        SleepMs(1);
    std::printf("clip %s: state=%d source=%d Hz %d ch, stored=%d Hz %d ch, %lld frames (%.2f s), load %.1f ms\n",
                path ? path : "<generated 48 kHz tone>", ci.state, ci.source_rate, ci.source_channels,
                ci.stored_rate, ci.channels, (long long)ci.total_frames, ci.length_seconds,
                (una_host_time_ns() - loadStart) * 1e-6);
    Check(ci.state == UNA_CLIP_LOADED, "clip decoded");
    Check(ci.stored_rate == di.sample_rate, "clip resampled to the device rate");

    una_play_params pp{};
    pp.struct_size = sizeof(pp);
    pp.volume = 0.5f;
    pp.pitch = 1.0f;
    pp.loop = 1;
    una_voice voice = 0;
    Check(una_voice_play(clip, &pp, &voice) == UNA_OK, "voice_play");

    const std::vector<ClockSample> run = SampleClock(voice, seconds);
    const ClockReport rep = Analyze(run, 1.0);
    PrintReport("clock (after 1 s)", rep);
    std::printf("clock: first anchor discontinuities=%lld callbacks=%lld fps=%.2f\n",
                (long long)run.front().clock.discontinuities, (long long)run.back().clock.callbacks,
                run.back().clock.frames_per_second);
    Check(run.back().voiceState == UNA_VOICE_PLAYING, "voice playing");
    Check(rep.backwards == 0, "audible frame never moves backwards");
    Check(std::fabs(rep.ppm) < 2000.0, "audible clock rate within 0.2% of nominal");
    Check(rep.discontinuities == 0, "no clock discontinuity in steady state");
    Check(rep.lagMinMs > 0.0, "audible frame stays behind the mix head");

    // Suspend: the timeline freezes; resume: it moves again after one re-anchor.
    una_clock before{}, frozenA{}, frozenB{}, resumed{};
    una_get_clock(&before);
    Check(una_set_suspended(1) == UNA_OK && una_is_suspended() == 1, "suspend");
    SleepMs(100);
    una_get_clock(&frozenA);
    una_voice_timing vtA{}, vtB{};
    una_voice_get_timing(voice, 0, &vtA);
    SleepMs(suspendMs);
    una_get_clock(&frozenB);
    una_voice_get_timing(voice, 0, &vtB);
    std::printf("suspended: mix_frame %lld -> %lld, audible %.1f -> %.1f, voice %.1f -> %.1f\n",
                (long long)frozenA.mix_frame, (long long)frozenB.mix_frame, frozenA.audible_frame,
                frozenB.audible_frame, vtA.audible_frame, vtB.audible_frame);
    Check(frozenA.mix_frame == frozenB.mix_frame, "mix head frozen while suspended");
    Check(frozenB.audible_frame <= frozenB.mix_frame, "audible frame bounded by the frozen mix head");
    const int64_t resumeAt = una_host_time_ns();
    Check(una_set_suspended(0) == UNA_OK && una_is_suspended() == 0, "resume");
    std::printf("resume took %.1f ms\n", (una_host_time_ns() - resumeAt) * 1e-6);
    const std::vector<ClockSample> after = SampleClock(voice, 2.0);
    una_get_clock(&resumed);
    const ClockReport rep2 = Analyze(after, 0.5);
    PrintReport("clock after resume (after 0.5 s)", rep2);
    std::printf("resume: mix_frame advanced %lld frames in %.2f s, discontinuities %lld -> %lld\n",
                (long long)(resumed.mix_frame - frozenB.mix_frame), (resumed.host_ns - resumeAt) * 1e-9,
                (long long)before.discontinuities, (long long)resumed.discontinuities);
    Check(resumed.mix_frame > frozenB.mix_frame + di.sample_rate, "mix head advances after resume");
    Check(after.back().voiceState == UNA_VOICE_PLAYING, "voice still playing after resume");
    Check(rep2.backwards == 0 && rep2.discontinuities == 0, "clock steady after resume");

    una_stats st{};
    una_get_stats(&st);
    std::printf("stats: callbacks=%lld frames=%lld starved=%lld dropped=%lld late=%lld voices=%d cpu=%.3f "
                "peak=%.3f out-peak L=%.3f R=%.3f\n",
                (long long)st.callbacks, (long long)st.frames_rendered, (long long)st.starved_frames,
                (long long)st.dropped_commands, (long long)st.late_commands, st.active_voices, st.cpu_load,
                st.cpu_load_peak, st.peak_left, st.peak_right);
    Check(st.starved_frames == 0, "no starved frames");

    una_voice_stop(voice, 0, 0.0f);
    una_clip_release(clip);
    una_shutdown();
    std::printf("%s (%d failed)\n", failures ? "ENGINE PROBE FAILED" : "ENGINE PROBE OK", failures);
    return failures ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "engine") == 0) return RunEngine(argc, argv);
    return RunDevice(argc, argv);
}
