// Real-time paced device that discards output. Used headless (CI, servers,
// batchmode) so timing behaves like a real device without audio hardware.

#include "AudioDevice.h"

#include "../Core/Platform.h"
#include "unaudio.h"

#include <atomic>
#include <thread>
#include <vector>

namespace una {

namespace {

class NullDevice final : public AudioDevice {
public:
    ~NullDevice() override { Close(); }

    bool Open(const DeviceConfig& cfg, DeviceCallback* cb) override {
        cb_ = cb;
        info_.sampleRate = cfg.sampleRate > 0 ? cfg.sampleRate : 48000;
        info_.channels = cfg.channels > 0 ? cfg.channels : 2;
        info_.bufferFrames = cfg.bufferFrames > 0 ? cfg.bufferFrames : 256;
        // Modelled as a two-period queue: a buffer rendered at deadline D is heard
        // at D + 2 periods, so one period of scheduling slack never underruns.
        info_.latencyFrames = info_.bufferFrames * 2;
        info_.exclusive = false;
        info_.backend = UNA_BACKEND_NULL;
        buffer_.assign(static_cast<size_t>(info_.bufferFrames) * info_.channels, 0.0f);
        return cb_ != nullptr;
    }

    bool Start() override {
        if (running_.exchange(true)) return true;
        thread_ = std::thread([this] { Run(); });
        return true;
    }

    void Stop() override {
        if (!running_.exchange(false)) return;
        if (thread_.joinable()) thread_.join();
    }

    void Close() override { Stop(); }
    DeviceInfo Info() const override { return info_; }

private:
    void Run() {
        SetCurrentThreadName("UNAudio Null");
        SetCurrentThreadRole(ThreadRole::AudioRealtime);
        const int64_t periodNs = static_cast<int64_t>(info_.bufferFrames) * 1000000000LL / info_.sampleRate;
        int64_t deadline = HostTimeNs();
        while (running_.load(std::memory_order_acquire)) {
            cb_->OnRender(buffer_.data(), info_.bufferFrames, info_.channels, deadline + 2 * periodNs);
            deadline += periodNs;
            int64_t now = HostTimeNs();
            if (deadline < now - 4 * periodNs) deadline = now; // fell far behind: resync
            SleepUntilNs(deadline);
        }
    }

    DeviceCallback* cb_ = nullptr;
    DeviceInfo info_{};
    std::vector<float> buffer_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

} // namespace

std::unique_ptr<AudioDevice> CreateNullDevice() { return std::make_unique<NullDevice>(); }

} // namespace una
