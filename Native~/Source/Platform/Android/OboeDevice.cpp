// Android output through Oboe (AAudio on API 27+, OpenSL ES otherwise).
//
// Oboe owns the real-time thread and calls us back. Three threads touch the
// device:
//   - the Oboe data callback thread: renders; never locks or allocates;
//   - Oboe's error thread: reopens the stream after a disconnect (headphones
//     unplugged, route change) and announces the new format;
//   - our timestamp thread: polls getTimestamp() for AAudio streams.
//
// getTimestamp() is not called from the data callback because Oboe's AAudio
// implementation takes a shared_mutex there, and on Android 10 and earlier a
// call from inside an OUTPUT callback on a legacy (non-MMAP) AAudio or OpenSL ES
// stream can trip the "mUnreleased out of range" assert in AudioTrackShared
// when the route changes (Oboe wiki TechNote_ReleaseBuffer). The timestamp
// thread instead publishes one atomic anchor: the host time at which rendered
// frame 0 is heard. OpenSL ES has no timestamps in Oboe, so it always uses the
// now + buffer fallback.

#if defined(__ANDROID__)

#include "../AudioDevice.h"

#include "../../Core/Platform.h"
#include "unaudio.h"

// Oboe 1.9.3's AudioStream.h has an unused parameter that -Wextra flags.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include <oboe/Oboe.h>
#pragma clang diagnostic pop
#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <time.h>
#include <vector>

namespace una {

namespace {

constexpr const char* kLogTag = "UNAudio";
constexpr auto kReopenRetryFast = std::chrono::milliseconds(200); // first attempts
constexpr auto kReopenRetrySlow = std::chrono::milliseconds(1000);
constexpr int kReopenFastAttempts = 10;
constexpr int64_t kMaxPlausibleLatencyNs = 1000000000LL; // beyond this the anchor is stale
// Two polls agree on an anchor (plus one burst on resampled streams); well
// inside the clock's re-anchor threshold of max(8 ms, 3 periods).
constexpr int64_t kAnchorToleranceNs = 4000000;
// Before jittery anchors are accepted (see PollTimestamp_l): this many live polls
// without two agreeing, and this long since the position first moved, which
// outlasts the legacy path's extrapolated start-up positions.
constexpr int kMaxAnchorMisses = 50;
constexpr int64_t kMinUnconfirmedDelayNs = 2000000000LL;
constexpr auto kPollFast = std::chrono::milliseconds(5);  // until the first anchor
constexpr auto kPollSteady = std::chrono::milliseconds(100);

// frames * 1e9 / rate without overflowing for long sessions.
inline int64_t FramesToNs(int64_t frames, int rate) {
    return (frames / rate) * 1000000000LL + (frames % rate) * 1000000000LL / rate;
}

inline int64_t AbsDiff(int64_t a, int64_t b) { return a > b ? a - b : b - a; }

class OboeDevice;

// Oboe keeps shared_ptr copies of its callbacks and its error thread can outlive
// the device, so callbacks go through this bridge, which the device detaches
// from in its destructor.
class StreamBridge final : public oboe::AudioStreamDataCallback,
                           public oboe::AudioStreamErrorCallback,
                           public std::enable_shared_from_this<StreamBridge> {
public:
    explicit StreamBridge(OboeDevice* device) : device_(device) {}

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* data, int32_t frames) override;
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

    void Detach() {
        std::lock_guard<std::mutex> lock(errorMutex_);
        device_.store(nullptr, std::memory_order_release);
    }

    // Keeps a closed stream alive until Oboe's error thread is done with it.
    // When Oboe wraps the stream in a FilterAudioStream (resampling, format
    // quirks), the error thread holds only the child and calls the wrapper
    // through a raw pointer, so dropping our reference early would free it.
    void Park(std::shared_ptr<oboe::AudioStream> stream) {
        std::lock_guard<std::mutex> lock(parkMutex_);
        parked_.push_back(std::move(stream));
    }

private:
    std::shared_ptr<oboe::AudioStream> Unpark(oboe::AudioStream* stream) {
        std::lock_guard<std::mutex> lock(parkMutex_);
        for (auto it = parked_.begin(); it != parked_.end(); ++it) {
            if (it->get() == stream) {
                std::shared_ptr<oboe::AudioStream> found = std::move(*it);
                parked_.erase(it);
                return found;
            }
        }
        return nullptr;
    }

    std::atomic<OboeDevice*> device_;
    std::mutex errorMutex_; // error thread vs Detach; never taken by the data callback
    std::mutex parkMutex_;  // leaf lock
    std::vector<std::shared_ptr<oboe::AudioStream>> parked_;
};

class OboeDevice final : public AudioDevice {
public:
    OboeDevice() : bridge_(std::make_shared<StreamBridge>(this)) {}

    ~OboeDevice() override {
        Close();
        bridge_->Detach(); // waits for an error handler that is still running
    }

    bool Open(const DeviceConfig& cfg, DeviceCallback* cb) override {
        Close();
        if (!cb) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        cfg_ = cfg;
        cb_ = cb;
        if (!OpenStream_l()) return false;
        opened_ = true;
        quitPoller_ = false;
        poller_ = std::thread([this] { PollLoop(); });
        return true;
    }

    bool Start() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!opened_) return false;
        if (started_) return true;
        if (!stream_) {
            // A reconnect gave up while stopped: try again and announce the format.
            if (!OpenStream_l()) return false;
            cb_->OnDeviceChanged(CurrentInfo());
        }
        if (!StartStream_l()) return false;
        started_ = true;
        return true;
    }

    void Stop() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!started_) return;
            started_ = false;
            StopStream_l();
        }
        cv_.notify_all(); // ends a reconnect loop
    }

    void Close() override {
        std::thread poller;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            started_ = false;
            opened_ = false;
            StopStream_l();
            if (stream_) Discard_l(std::move(stream_));
            quitPoller_ = true;
            poller = std::move(poller_);
        }
        cv_.notify_all();
        if (poller.joinable()) poller.join();
    }

    DeviceInfo Info() const override { return CurrentInfo(); }

    // ── Oboe callbacks (via StreamBridge) ──────────────────────────────

    // Data callback thread. Lock-free: atomics only.
    void Render(float* out, int frames, int streamChannels) {
        inCallback_.fetch_add(1);
        if (!renderEnabled_.load()) {
            std::memset(out, 0, static_cast<size_t>(frames) * streamChannels * sizeof(float));
            inCallback_.fetch_sub(1);
            return;
        }
        if (!threadNamed_.exchange(true, std::memory_order_relaxed)) SetCurrentThreadName("UNAudio Oboe");

        const int64_t now = HostTimeNs();
        const int64_t rendered = framesRendered_.load(std::memory_order_relaxed);
        const int64_t anchor = anchorNs_.load(std::memory_order_acquire);
        int64_t presentNs = anchor != 0 ? anchor + FramesToNs(rendered, rtRate_) : 0;
        if (anchor != 0 && presentNs >= now && presentNs <= now + kMaxPlausibleLatencyNs) {
            leadNs_.store(presentNs - now, std::memory_order_relaxed);
        } else {
            // No usable timestamp (OpenSL ES, or AAudio before the first one after
            // a start): the lead last measured on this stream beats the bare
            // buffer size, which leaves out the hardware latency.
            presentNs = now + leadNs_.load(std::memory_order_relaxed);
        }

        cb_->OnRender(out, frames, rtChannels_, presentNs);
        framesRendered_.store(rendered + frames, std::memory_order_release);
        inCallback_.fetch_sub(1);
    }

    // Oboe error thread, after Oboe stopped and closed `stream`. Returns our
    // reference to it, which the bridge drops once Oboe is done with it.
    std::shared_ptr<oboe::AudioStream> OnStreamError(oboe::AudioStream* stream, oboe::Result error) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!opened_ || stream != stream_.get()) return nullptr; // closed by us, or a stale stream
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "Oboe stream error %s, reopening",
                            oboe::convertToText(error));
        std::shared_ptr<oboe::AudioStream> old = std::move(stream_);
        renderEnabled_.store(false);

        // Retry while the owner still wants audio, like a device that comes back
        // after a slow route switch. Stopped: one attempt, Start() does the rest.
        for (int attempt = 0; !OpenStream_l(); ++attempt) {
            if (!started_) return old;
            cv_.wait_for(lock, attempt < kReopenFastAttempts ? kReopenRetryFast : kReopenRetrySlow);
            if (!opened_ || !started_ || stream_) return old; // closed, stopped or reopened meanwhile
        }

        // The new stream is not started yet, so no OnRender can be in flight.
        cb_->OnDeviceChanged(CurrentInfo());
        if (started_ && !StartStream_l()) started_ = false;
        return old;
    }

private:
    DeviceInfo CurrentInfo() const {
        std::lock_guard<std::mutex> lock(infoMutex_);
        return info_;
    }

    std::shared_ptr<oboe::AudioStream> OpenOboe_l(int sampleRate) {
        oboe::AudioStreamBuilder builder;
        builder.setDirection(oboe::Direction::Output)
            ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
            ->setSharingMode(cfg_.exclusive ? oboe::SharingMode::Exclusive : oboe::SharingMode::Shared)
            ->setFormat(oboe::AudioFormat::Float)
            ->setFormatConversionAllowed(true)
            ->setChannelCount(cfg_.channels > 0 ? cfg_.channels : oboe::ChannelCount::Stereo)
            ->setChannelConversionAllowed(true)
            ->setUsage(oboe::Usage::Game)
            ->setDataCallback(bridge_)
            ->setErrorCallback(bridge_);
        // Test hook for shell tools such as una_device_probe (apps cannot set it):
        // UNAUDIO_OBOE_API=opensles runs the OpenSL ES path that API 23-26 devices take.
        if (const char* api = std::getenv("UNAUDIO_OBOE_API")) {
            if (std::strcmp(api, "opensles") == 0) builder.setAudioApi(oboe::AudioApi::OpenSLES);
            else if (std::strcmp(api, "aaudio") == 0) builder.setAudioApi(oboe::AudioApi::AAudio);
        }
        if (sampleRate > 0) {
            builder.setSampleRate(sampleRate)
                ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium);
        }
        std::shared_ptr<oboe::AudioStream> stream;
        const oboe::Result r = builder.openStream(stream);
        if (r != oboe::Result::OK || !stream) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Oboe openStream failed: %s", oboe::convertToText(r));
            return nullptr;
        }
        if (stream->getFormat() != oboe::AudioFormat::Float || stream->getFramesPerBurst() <= 0 ||
            stream->getSampleRate() <= 0 || stream->getChannelCount() <= 0) {
            Discard_l(std::move(stream));
            return nullptr;
        }
        return stream;
    }

    // Closes a stream we no longer use; parks it if an error thread may still run.
    void Discard_l(std::shared_ptr<oboe::AudioStream> stream) {
        stream->close();
        if (stream->getLastErrorCallbackResult() != oboe::Result::OK) bridge_->Park(std::move(stream));
    }

    // Opens a stream for cfg_ and publishes its format. Callbacks are idle.
    bool OpenStream_l() {
        // Prefer the native rate: it is the only one that gets the fast path.
        std::shared_ptr<oboe::AudioStream> stream = OpenOboe_l(0);
        if (!stream) return false;
        const int nativeRate = stream->getSampleRate();
        if (cfg_.sampleRate > 0 && cfg_.sampleRate != nativeRate) {
            // Oboe resamples in a FilterAudioStream whose burst and buffer sizes
            // stay in native-rate frames.
            Discard_l(std::move(stream));
            stream = OpenOboe_l(cfg_.sampleRate);
            if (!stream) return false;
        }
        const int rate = stream->getSampleRate();
        const auto toAppFrames = [&](int64_t native) {
            return static_cast<int>((native * rate + nativeRate - 1) / nativeRate);
        };

        // Two bursts is the smallest size that survives scheduling jitter.
        const int burst = stream->getFramesPerBurst();
        int target = 2 * burst;
        if (cfg_.bufferFrames > 0) {
            const int64_t wanted = (static_cast<int64_t>(cfg_.bufferFrames) * nativeRate + rate - 1) / rate;
            if (wanted > target) target = static_cast<int>((wanted + burst - 1) / burst * burst);
        }
        const auto sized = stream->setBufferSizeInFrames(target); // AAudio only; OpenSL ES keeps its own
        const int bufferFrames = sized ? sized.value() : stream->getBufferSizeInFrames();

        stream_ = std::move(stream);
        rtRate_ = rate;
        rtChannels_ = stream_->getChannelCount();
        rtBufferFrames_ = toAppFrames(bufferFrames > 0 ? bufferFrames : target);
        // Resampled streams advance their counters in uneven steps: our callbacks
        // come in blocks of `burst` app frames, the written count in one native
        // burst converted to app frames. Either can be the larger.
        deltaTolerance_ = rate != nativeRate ? std::max(burst, toAppFrames(burst)) : 0;
        framesRendered_.store(0, std::memory_order_relaxed);
        threadNamed_.store(false, std::memory_order_relaxed);
        ResetAnchor_l();
        leadNs_.store(FramesToNs(rtBufferFrames_, rtRate_), std::memory_order_relaxed);

        DeviceInfo info;
        info.sampleRate = rtRate_;
        info.channels = rtChannels_;
        info.bufferFrames = toAppFrames(burst);
        info.latencyFrames = rtBufferFrames_; // refined by the timestamps (AAudio)
        info.exclusive = stream_->getSharingMode() == oboe::SharingMode::Exclusive;
        const bool aaudio = stream_->getAudioApi() == oboe::AudioApi::AAudio;
        info.backend = aaudio ? UNA_BACKEND_AAUDIO : UNA_BACKEND_OPENSLES;
        // isMMapUsed dereferences the AAudio stream, which an OpenSL ES stream lacks.
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "Oboe stream: %s%s %s/%s, %d Hz (native %d), %d ch, burst %d, buffer %d of %d",
                            oboe::convertToText(stream_->getAudioApi()),
                            aaudio && oboe::OboeExtensions::isMMapUsed(stream_.get()) ? " MMAP" : "",
                            oboe::convertToText(stream_->getSharingMode()),
                            oboe::convertToText(stream_->getPerformanceMode()), rate, nativeRate, rtChannels_, burst,
                            bufferFrames, stream_->getBufferCapacityInFrames());
        std::lock_guard<std::mutex> lock(infoMutex_);
        info_ = info;
        return true;
    }

    bool StartStream_l() {
        ResetAnchor_l();
        renderEnabled_.store(true);
        if (stream_->requestStart() != oboe::Result::OK) {
            renderEnabled_.store(false);
            return false;
        }
        cv_.notify_all();
        return true;
    }

    // Returns once Oboe stopped and no OnRender is running. The flag catches a
    // callback that is still in flight after an OpenSL ES stop.
    void StopStream_l() {
        const bool wasRunning = renderEnabled_.exchange(false);
        if (stream_) stream_->stop();
        while (inCallback_.load() != 0) std::this_thread::yield();
        if (wasRunning && stream_) {
            const auto xruns = stream_->getXRunCount();
            __android_log_print(ANDROID_LOG_INFO, kLogTag, "Oboe stream stopped: %lld frames rendered, xruns %d",
                                static_cast<long long>(framesRendered_.load()), xruns ? xruns.value() : -1);
        }
        ResetAnchor_l();
    }

    void ResetAnchor_l() {
        anchorNs_.store(0, std::memory_order_relaxed);
        anchorConfirmed_ = false;
        havePending_ = false;
        anchorMisses_ = 0;
        firstLiveNs_ = 0;
        unconfirmed_ = false;
    }

    void PollLoop() {
        SetCurrentThreadName("UNAudio OboeTS");
        std::unique_lock<std::mutex> lock(mutex_);
        while (!quitPoller_) {
            const bool active = started_ && stream_ && stream_->getAudioApi() == oboe::AudioApi::AAudio;
            if (!active) {
                cv_.wait(lock);
                continue;
            }
            cv_.wait_for(lock, anchorConfirmed_ ? kPollSteady : kPollFast);
            if (!quitPoller_ && started_ && stream_) PollTimestamp_l();
        }
    }

    // Maps the stream's presentation timestamp onto our rendered-frame count.
    // AAudio's frame counters can run ahead of the frames we rendered (MMAP
    // catch-up on start, skipped frames after an underrun), so the offset is
    // re-measured on every poll. Until the hardware reports presentation, the
    // legacy (AudioTrack) path repeats a stale position or extrapolates one from
    // the mixer, which is off by the hardware latency (90-130 ms on the API 31
    // emulator). So an anchor is only published once the position has moved
    // and two consecutive polls agree on it. A device whose live timestamps
    // never agree that closely (coarse HAL updates) falls back, after
    // kMaxAnchorMisses and kMinUnconfirmedDelayNs, to the offset check alone
    // until two agree again: a jittery anchor still carries the hardware
    // latency that now + buffer leaves out.
    void PollTimestamp_l() {
        const auto ts = stream_->getTimestamp(CLOCK_MONOTONIC);
        if (!ts) return;
        const int64_t before = framesRendered_.load(std::memory_order_acquire);
        const int64_t written = stream_->getFramesWritten();
        const int64_t after = framesRendered_.load(std::memory_order_acquire);
        if (before != after) return; // a callback finished in between: retry next poll
        int64_t delta = written - before;
        // A resampling stream counts written frames in its own blocks, so the
        // offset dips by up to a block while it holds frames we rendered but it
        // has not consumed. The largest offset within that wobble is the real one.
        if (deltaTolerance_ > 0 && havePending_ && AbsDiff(delta, pendingDelta_) <= deltaTolerance_)
            delta = std::max(delta, pendingDelta_);
        const int64_t position = ts.value().position;
        const int64_t anchor = ts.value().timestamp - FramesToNs(position - delta, rtRate_);
        const bool live = havePending_ && position > pendingPosition_ &&
                          AbsDiff(delta, pendingDelta_) <= deltaTolerance_;
        const int64_t toleranceNs = kAnchorToleranceNs + FramesToNs(deltaTolerance_, rtRate_);
        const bool close = AbsDiff(anchor, pendingAnchor_) <= toleranceNs;
        const int64_t now = HostTimeNs();
        if (live && firstLiveNs_ == 0) firstLiveNs_ = now;
        if (live && close) {
            anchorMisses_ = 0;
            unconfirmed_ = false;
        } else if (live && anchorMisses_ < kMaxAnchorMisses) {
            ++anchorMisses_;
        }
        if (live && !close && !unconfirmed_ && anchorMisses_ >= kMaxAnchorMisses &&
            now - firstLiveNs_ >= kMinUnconfirmedDelayNs) {
            unconfirmed_ = true;
            __android_log_print(ANDROID_LOG_INFO, kLogTag,
                                "Oboe timestamps: %d live polls disagree by more than %.1f ms (%.1f s after the "
                                "position first moved); accepting them unconfirmed",
                                kMaxAnchorMisses, toleranceNs * 1e-6, (now - firstLiveNs_) * 1e-9);
        }
        const bool agreed = live && (close || unconfirmed_);
        pendingDelta_ = delta;
        pendingAnchor_ = anchor;
        pendingPosition_ = position;
        havePending_ = true;
        if (!agreed) return;

        anchorNs_.store(anchor != 0 ? anchor : 1, std::memory_order_release);
        anchorConfirmed_ = true;

        // Next frame we write vs now = buffer fill + hardware latency.
        const int64_t aheadNs = anchor + FramesToNs(after, rtRate_) - HostTimeNs();
        const int64_t measured = aheadNs * rtRate_ / 1000000000LL;
        if (measured > 0 && measured < rtRate_) {
            std::lock_guard<std::mutex> infoLock(infoMutex_);
            info_.latencyFrames = std::max(rtBufferFrames_, static_cast<int>(measured));
        }
    }

    const std::shared_ptr<StreamBridge> bridge_;

    // Lifecycle state, guarded by mutex_ (never taken by the data callback).
    std::mutex mutex_;
    std::condition_variable cv_;
    DeviceConfig cfg_{};
    DeviceCallback* cb_ = nullptr;
    std::shared_ptr<oboe::AudioStream> stream_;
    std::thread poller_;
    bool opened_ = false;
    bool started_ = false;
    bool quitPoller_ = false;
    bool anchorConfirmed_ = false;
    bool havePending_ = false;
    int anchorMisses_ = 0;      // live polls since two last agreed (frozen polls do not count)
    int64_t firstLiveNs_ = 0;   // host time of the first poll whose position moved
    bool unconfirmed_ = false;  // publishing anchors that no second poll confirmed
    int64_t pendingDelta_ = 0;
    int64_t pendingAnchor_ = 0;
    int64_t pendingPosition_ = 0;
    int64_t deltaTolerance_ = 0;

    // Read by the data callback; written only while no callback runs (open).
    int rtRate_ = 0;
    int rtChannels_ = 0;
    int rtBufferFrames_ = 0;

    std::atomic<bool> renderEnabled_{false};
    std::atomic<int> inCallback_{0};
    std::atomic<bool> threadNamed_{false};
    std::atomic<int64_t> framesRendered_{0};
    std::atomic<int64_t> anchorNs_{0}; // host time of rendered frame 0; 0 = unknown
    std::atomic<int64_t> leadNs_{0};   // presentNs - now of the last timestamped callback

    mutable std::mutex infoMutex_;
    DeviceInfo info_{};
};

oboe::DataCallbackResult StreamBridge::onAudioReady(oboe::AudioStream* stream, void* data, int32_t frames) {
    OboeDevice* device = device_.load(std::memory_order_acquire);
    if (!device) {
        std::memset(data, 0, static_cast<size_t>(frames) * stream->getBytesPerFrame());
        return oboe::DataCallbackResult::Stop;
    }
    device->Render(static_cast<float*>(data), frames, stream->getChannelCount());
    return oboe::DataCallbackResult::Continue;
}

void StreamBridge::onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) {
    const auto self = shared_from_this(); // the device may drop its reference meanwhile
    std::shared_ptr<oboe::AudioStream> current;
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        if (OboeDevice* device = device_.load(std::memory_order_acquire))
            current = device->OnStreamError(stream, error);
    }
    const std::shared_ptr<oboe::AudioStream> parked = Unpark(stream);
    // Both references die here; Oboe does not touch the stream after we return.
}

} // namespace

std::unique_ptr<AudioDevice> CreatePlatformDevice() { return std::make_unique<OboeDevice>(); }

} // namespace una

#endif // defined(__ANDROID__)
