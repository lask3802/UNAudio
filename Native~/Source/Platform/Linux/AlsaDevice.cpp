// ALSA output ("default" PCM, blocking, interleaved).
//
// One dedicated render thread paces itself on the PCM: it waits until a full
// period of space is free, asks the engine for one period, converts it to the
// device format and writes it. Because the write only starts once a whole
// period fits, snd_pcm_writei never blocks, so Stop() can interrupt the thread
// with nothing but an atomic flag — no ALSA call is ever made from two threads
// on the same handle.

#if defined(__linux__) && !defined(__ANDROID__)

#include "../AudioDevice.h"

#include "../../Core/Platform.h"
#include "unaudio.h"

#include <alsa/asoundlib.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace una {

namespace {

enum class SampleFormat { Float32, Int32, Int16 };

int32_t FloatToS32(float v) {
    if (v != v) return 0;
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    const double d = static_cast<double>(v) * 2147483648.0;
    return d >= 2147483647.0 ? INT32_MAX : static_cast<int32_t>(d);
}

int16_t FloatToS16(float v) {
    if (v != v) return 0;
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return static_cast<int16_t>(std::lrintf(v * 32767.0f));
}

class AlsaDevice final : public AudioDevice {
public:
    ~AlsaDevice() override { Close(); }

    bool Open(const DeviceConfig& cfg, DeviceCallback* cb) override {
        Close();
        if (!cb) return false;
        cb_ = cb;
        cfg_ = cfg;
        if (!OpenPcm()) return false;
        std::lock_guard<std::mutex> lock(infoMutex_);
        info_ = MakeInfo();
        return true;
    }

    bool Start() override {
        if (running_.exchange(true)) return true;
        if (!pcm_) {                      // an earlier Stop() may have interrupted a reconnect
            if (!OpenPcm()) {
                running_.store(false);
                return false;
            }
            // The reopened device may run another format; announce it before rendering resumes
            // (no render thread exists yet, as OnDeviceChanged requires).
            const DeviceInfo info = MakeInfo();
            {
                std::lock_guard<std::mutex> lock(infoMutex_);
                info_ = info;
            }
            cb_->OnDeviceChanged(info);
        }
        thread_ = std::thread([this] { Run(); });
        return true;
    }

    // Race-free shutdown: the flag is only observed between ALSA calls, each of
    // which is bounded by roughly one period (wait timeout, non-blocking write).
    void Stop() override {
        if (!running_.exchange(false)) return;
        if (thread_.joinable()) thread_.join();
        if (pcm_) {                       // thread is gone: handle is ours alone
            snd_pcm_drop(pcm_);
            snd_pcm_prepare(pcm_);        // ready for a later Start()
        }
    }

    void Close() override {
        Stop();
        ClosePcm();
    }

    DeviceInfo Info() const override {
        std::lock_guard<std::mutex> lock(infoMutex_);
        return info_;
    }

    /// Xruns recovered since Open (diagnostics).
    uint64_t XRunCount() const { return xruns_.load(std::memory_order_relaxed); }

private:
    enum class Step { Ok, Lost };

    // Negotiates the PCM using cfg_. Fills pcm_, rate_, channels_, period_,
    // buffer_, format_ and the scratch buffers. Leaves pcm_ null on failure.
    bool OpenPcm() {
        snd_pcm_t* pcm = nullptr;
        if (snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) return false;

        snd_pcm_hw_params_t* hw;
        snd_pcm_hw_params_alloca(&hw);
        snd_pcm_hw_params_any(pcm, hw);
        bool ok = snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED) >= 0;

        SampleFormat fmt = SampleFormat::Float32;
        if (ok) {
            if (snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_FLOAT_LE) >= 0) fmt = SampleFormat::Float32;
            else if (snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S32_LE) >= 0) fmt = SampleFormat::Int32;
            else if (snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE) >= 0) fmt = SampleFormat::Int16;
            else ok = false;
        }

        if (ok) {
            if (cfg_.channels > 0) {
                ok = snd_pcm_hw_params_set_channels(pcm, hw, static_cast<unsigned>(cfg_.channels)) >= 0;
            } else {
                unsigned ch = 2;
                ok = snd_pcm_hw_params_set_channels_near(pcm, hw, &ch) >= 0;
            }
        }

        if (ok) {
            snd_pcm_hw_params_set_rate_resample(pcm, hw, 1); // let plug/dmix convert
            unsigned rate = static_cast<unsigned>(cfg_.sampleRate > 0 ? cfg_.sampleRate : 48000);
            int dir = 0;
            ok = snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, &dir) >= 0;
        }

        if (ok) {
            snd_pcm_uframes_t period = static_cast<snd_pcm_uframes_t>(cfg_.bufferFrames > 0 ? cfg_.bufferFrames : 256);
            int dir = 0;
            ok = snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, &dir) >= 0;
            snd_pcm_uframes_t buffer = period * 3;
            if (ok) ok = snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buffer) >= 0;
        }

        if (ok) ok = snd_pcm_hw_params(pcm, hw) >= 0;

        // Read back what the driver actually granted.
        unsigned rate = 0, channels = 0;
        snd_pcm_uframes_t period = 0, buffer = 0;
        if (ok) ok = snd_pcm_get_params(pcm, &buffer, &period) >= 0;
        if (ok) {
            int dir = 0;
            ok = snd_pcm_hw_params_get_rate(hw, &rate, &dir) >= 0 &&
                 snd_pcm_hw_params_get_channels(hw, &channels) >= 0;
        }

        if (ok) {
            // Start as soon as the first period is queued; wake once a period is free.
            snd_pcm_sw_params_t* sw;
            snd_pcm_sw_params_alloca(&sw);
            ok = snd_pcm_sw_params_current(pcm, sw) >= 0 &&
                 snd_pcm_sw_params_set_start_threshold(pcm, sw, period) >= 0 &&
                 snd_pcm_sw_params_set_avail_min(pcm, sw, period) >= 0 &&
                 snd_pcm_sw_params(pcm, sw) >= 0;
        }

        if (!ok || period == 0 || channels == 0 || rate == 0 || snd_pcm_prepare(pcm) < 0) {
            snd_pcm_close(pcm);
            return false;
        }

        pcm_ = pcm;
        format_ = fmt;
        rate_ = static_cast<int>(rate);
        channels_ = static_cast<int>(channels);
        period_ = static_cast<int>(period);
        buffer_ = static_cast<int>(buffer);

        const size_t samples = static_cast<size_t>(period_) * channels_;
        render_.assign(samples, 0.0f);
        scratch32_.clear();
        scratch16_.clear();
        if (format_ == SampleFormat::Int32) scratch32_.assign(samples, 0);
        else if (format_ == SampleFormat::Int16) scratch16_.assign(samples, 0);
        return true;
    }

    void ClosePcm() {
        if (pcm_) {
            snd_pcm_close(pcm_);
            pcm_ = nullptr;
        }
    }

    DeviceInfo MakeInfo() const {
        DeviceInfo info;
        info.sampleRate = rate_;
        info.channels = channels_;
        info.bufferFrames = period_;
        info.latencyFrames = buffer_;
        info.exclusive = false;
        info.backend = UNA_BACKEND_ALSA;
        return info;
    }

    // Sleeps in short slices so Stop() stays responsive. False if stopped.
    bool InterruptibleSleepMs(int ms) {
        for (int left = ms; left > 0 && running_.load(std::memory_order_acquire); left -= 25) {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(left, 25)));
        }
        return running_.load(std::memory_order_acquire);
    }

    // Device vanished: reopen periodically, announce the (possibly different)
    // format, then let rendering resume. False only if stopped meanwhile.
    bool Reconnect() {
        ClosePcm();
        while (running_.load(std::memory_order_acquire)) {
            if (!InterruptibleSleepMs(500)) return false;
            if (!OpenPcm()) continue;
            const DeviceInfo info = MakeInfo();
            {
                std::lock_guard<std::mutex> lock(infoMutex_);
                info_ = info;
            }
            cb_->OnDeviceChanged(info);
            return true;
        }
        return false;
    }

    static bool IsDeviceGone(int err) { return err == -ENODEV || err == -EBADFD; }

    // Blocks (bounded) until one full period of space is free.
    Step WaitWritable() {
        const int periodMs = std::max(1, period_ * 1000 / rate_);
        const int timeoutMs = std::clamp(periodMs * 2, 10, 100);
        int stalledMs = 0;
        while (running_.load(std::memory_order_acquire)) {
            const int r = snd_pcm_wait(pcm_, timeoutMs);
            if (r > 0) return Step::Ok;
            if (r == 0) {
                // A live device drains one period per period; 2 s of silence = wedged.
                stalledMs += timeoutMs;
                if (stalledMs >= 2000) return Step::Lost;
                continue;
            }
            if (IsDeviceGone(r)) return Step::Lost;
            if (r == -EINTR) continue;
            xruns_.fetch_add(1, std::memory_order_relaxed);
            if (snd_pcm_recover(pcm_, r, 1) < 0) return Step::Lost;
            return Step::Ok;
        }
        return Step::Ok; // stopping: caller re-checks the flag
    }

    // Host time at which the first frame of the next period is heard.
    int64_t PresentTimeNs() {
        snd_pcm_sframes_t delay = 0;
        if (snd_pcm_delay(pcm_, &delay) < 0 || delay < 0) {
            // Fallback: whatever is not free in the buffer is still queued.
            const snd_pcm_sframes_t avail = snd_pcm_avail_update(pcm_);
            delay = avail >= 0 ? std::max<snd_pcm_sframes_t>(0, buffer_ - avail) : buffer_;
        }
        return HostTimeNs() + static_cast<int64_t>(delay) * 1000000000LL / rate_;
    }

    // Converts render_ to the device format; returns the bytes to write.
    const uint8_t* ConvertPeriod() {
        const size_t n = render_.size();
        switch (format_) {
        case SampleFormat::Int32:
            for (size_t i = 0; i < n; ++i) scratch32_[i] = FloatToS32(render_[i]);
            return reinterpret_cast<const uint8_t*>(scratch32_.data());
        case SampleFormat::Int16:
            for (size_t i = 0; i < n; ++i) scratch16_[i] = FloatToS16(render_[i]);
            return reinterpret_cast<const uint8_t*>(scratch16_.data());
        case SampleFormat::Float32:
        default:
            return reinterpret_cast<const uint8_t*>(render_.data());
        }
    }

    Step WritePeriod(const uint8_t* data) {
        const size_t bytesPerFrame = static_cast<size_t>(channels_) *
            (format_ == SampleFormat::Int16 ? 2 : 4);
        int done = 0;
        while (done < period_ && running_.load(std::memory_order_acquire)) {
            const snd_pcm_sframes_t n = snd_pcm_writei(pcm_, data + done * bytesPerFrame,
                                                       static_cast<snd_pcm_uframes_t>(period_ - done));
            if (n > 0) { done += static_cast<int>(n); continue; }
            const int err = n < 0 ? static_cast<int>(n) : -EAGAIN;
            if (IsDeviceGone(err)) return Step::Lost;
            if (err == -EAGAIN) { snd_pcm_wait(pcm_, 5); continue; }
            if (err == -EPIPE || err == -ESTRPIPE) xruns_.fetch_add(1, std::memory_order_relaxed);
            // Recovered: retry the same period so no rendered audio is dropped
            // (its presentNs is then slightly early, once).
            if (snd_pcm_recover(pcm_, err, 1) < 0) return Step::Lost;
        }
        return Step::Ok;
    }

    void Run() {
        SetCurrentThreadName("UNAudio ALSA");
        SetCurrentThreadRole(ThreadRole::AudioRealtime);
        while (running_.load(std::memory_order_acquire)) {
            Step step = WaitWritable();
            if (step == Step::Ok && running_.load(std::memory_order_acquire)) {
                const int64_t presentNs = PresentTimeNs();
                cb_->OnRender(render_.data(), period_, channels_, presentNs);
                step = WritePeriod(ConvertPeriod());
            }
            if (step == Step::Lost && !Reconnect()) break;
        }
    }

    DeviceCallback* cb_ = nullptr;
    DeviceConfig cfg_{};

    // Negotiated format: written by Open/Reconnect, read by the render thread.
    snd_pcm_t* pcm_ = nullptr;
    SampleFormat format_ = SampleFormat::Float32;
    int rate_ = 0;
    int channels_ = 0;
    int period_ = 0;
    int buffer_ = 0;
    std::vector<float> render_;
    std::vector<int32_t> scratch32_;
    std::vector<int16_t> scratch16_;

    mutable std::mutex infoMutex_;
    DeviceInfo info_{};
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> xruns_{0};
};

} // namespace

std::unique_ptr<AudioDevice> CreatePlatformDevice() { return std::make_unique<AlsaDevice>(); }

} // namespace una

#endif // defined(__linux__) && !defined(__ANDROID__)
