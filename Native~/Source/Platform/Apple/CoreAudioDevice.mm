// CoreAudio output for macOS (AUHAL on the default output device) and iOS /
// tvOS (RemoteIO). Objective-C++, compiled with ARC.
//
// Threads:
//   - the CoreAudio IO thread calls RenderProc: it renders straight into the
//     AudioUnit's buffer and never locks, allocates or sends ObjC messages;
//   - the caller's thread runs Open/Start/Stop/Close under the control mutex;
//   - a private serial dispatch queue handles every system notification
//     (default device / sample rate changes on macOS, AVAudioSession
//     interruptions, route changes and media resets on iOS) under the same
//     mutex. Listener and observer blocks only hop onto that queue, so they
//     never block the HAL or NSNotificationCenter.
// Info() takes only a small info mutex that the render path never touches.
// OnDeviceChanged is called with the control mutex held and the unit stopped
// (on that queue, or in Start() for a change that happened while the owner had
// not started the device), so the callback must not call Open/Start/Stop/Close.
// Failed rebuilds and starts are retried on the queue with a capped backoff.
//
// Timing (presentNs = host time at which the buffer's first frame is heard):
//   The render callback's inTimeStamp->mHostTime is the IO cycle's output
//   time: AudioHardware.h documents an IOProc's inOutputTime as "the time at
//   which the first frame in the data will be passed to the hardware", and
//   Apple DTS (developer.apple.com/forums/thread/25197) states that the render
//   proc timestamp already accounts for the IO buffer size and the safety
//   offset but not for the stream and device latencies. So:
//     macOS: presentNs = host(mHostTime) + AU latency + (device latency +
//            stream latency) frames. The safety offset is NOT added again.
//            Chromium's AUHAL output (core_audio_util_mac.cc GetHardwareLatency
//            and audio_auhal.cc GetPlayoutTime) uses this same sum; Chromium
//            briefly dropped the device term (2024-07) and restored it for
//            output after A/V sync regressions (commit 83b5c75a8f, 2024-12).
//     iOS:   presentNs = host(mHostTime) + AVAudioSession.outputLatency, as
//            The Amazing Audio Engine 2 does (AEIOAudioUnit.m).
//   Both latencies are cached off the render thread. When the host time is
//   missing or implausible the fallback is now + one buffer + latency.
//
// iOS session policy: Unity owns the AVAudioSession, so its category and mode
// are never changed here. The device only activates the session (a no-op when
// Unity already did) and sets the preferred IO buffer duration, which is
// session-wide and therefore also shortens Unity's own output buffer.
//
// Stop: AudioOutputUnitStop is synchronous when called off the IO thread
// (Doug Wyatt, coreaudio-api 2005, quoted in developer.apple.com/forums/
// thread/117962), but that is not in the headers, so Stop also gates the
// render path with an in-flight counter and waits for it to drain.

#if defined(__APPLE__)

#include "../AudioDevice.h"

#include "../../Core/Platform.h"
#include "unaudio.h"

#include <TargetConditionals.h>
#include <AudioToolbox/AudioToolbox.h>
#if TARGET_OS_IPHONE
#  import <AVFoundation/AVFoundation.h>
#else
#  include <CoreAudio/CoreAudio.h>
#endif
#import <Foundation/Foundation.h>
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include <os/log.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace una {

namespace {

constexpr int kDefaultChannels = 2;
constexpr UInt32 kMaxFramesPerSlice = 4096;               // QA1606: iOS renders 4096 with the screen locked
constexpr int64_t kMaxPlausibleLatencyNs = 1000000000LL;  // host time this far from now is bogus
constexpr int kMaxRetries = 8;
constexpr int64_t kRetryBaseMs = 250;
constexpr int64_t kRetryMaxMs = 4000;
#if TARGET_OS_IPHONE
constexpr int kDefaultIosBufferFrames = 128;              // ~2.7 ms at 48 kHz
#else
constexpr UInt32 kMinAutoBufferFrames = 64;
// kAudioObjectPropertyElementMain needs the macOS 12 SDK and ...Master is
// deprecated there; both are 0.
constexpr AudioObjectPropertyElement kElementMain = 0;
#endif

// frames * 1e9 / rate without overflowing for long sessions.
inline int64_t FramesToNs(int64_t frames, int rate) {
    return (frames / rate) * 1000000000LL + (frames % rate) * 1000000000LL / rate;
}

void LogStatus(const char* what, OSStatus status) {
    os_log_error(OS_LOG_DEFAULT, "UNAudio CoreAudio: %{public}s failed (%d)", what, static_cast<int>(status));
}

bool Check(OSStatus status, const char* what) {
    if (status == noErr) return true;
    LogStatus(what, status);
    return false;
}

#if !TARGET_OS_IPHONE
template <class T>
bool GetHalProperty(AudioObjectID object, AudioObjectPropertySelector selector,
                    AudioObjectPropertyScope scope, T& out) {
    const AudioObjectPropertyAddress address = {selector, scope, kElementMain};
    UInt32 size = sizeof(T);
    return AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, &out) == noErr && size == sizeof(T);
}

AudioObjectID DefaultOutputDevice() {
    AudioObjectID device = kAudioObjectUnknown;
    if (!GetHalProperty(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice,
                        kAudioObjectPropertyScopeGlobal, device))
        return kAudioObjectUnknown;
    return device;
}

Float64 NominalSampleRate(AudioObjectID device) {
    Float64 rate = 0.0;
    if (!GetHalProperty(device, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, rate))
        return 0.0;
    return rate;
}

// Latency of the device's first output stream (Apple's advice when streams differ).
UInt32 FirstOutputStreamLatency(AudioObjectID device) {
    const AudioObjectPropertyAddress address = {kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput,
                                                kElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) != noErr ||
        size < sizeof(AudioStreamID))
        return 0;
    std::vector<AudioStreamID> streams(size / sizeof(AudioStreamID));
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, streams.data()) != noErr ||
        size < sizeof(AudioStreamID))
        return 0;
    UInt32 latency = 0;
    if (!GetHalProperty(streams[0], kAudioStreamPropertyLatency, kAudioObjectPropertyScopeGlobal, latency) &&
        !GetHalProperty(streams[0], kAudioStreamPropertyLatency, kAudioObjectPropertyScopeOutput, latency))
        return 0;
    return latency;
}
#endif

class CoreAudioDevice;

// Shared with the dispatched blocks: a block that runs after the device is
// destroyed finds a null owner instead of a dangling pointer.
struct DeviceLife {
    std::mutex mutex;                  // the device's control mutex
    CoreAudioDevice* owner = nullptr;  // cleared by the destructor
};

using DeviceWork = void (^)(CoreAudioDevice* device);

class CoreAudioDevice final : public AudioDevice {
public:
    CoreAudioDevice() : life_(std::make_shared<DeviceLife>()) {
        life_->owner = this;
        mach_timebase_info_data_t tb{};
        mach_timebase_info(&tb);
        if (tb.numer != 0 && tb.denom != 0) {
            tbNumer_ = tb.numer;
            tbDenom_ = tb.denom;
        }
        queue_ = dispatch_queue_create("com.unaudio.coreaudio", DISPATCH_QUEUE_SERIAL);
        (void)HostTimeNs(); // initialize its timebase static here, not on the IO thread
    }

    ~CoreAudioDevice() override {
        Close();
        std::lock_guard<std::mutex> lock(life_->mutex); // waits for a handler that is still running
        life_->owner = nullptr;
    }

    bool Open(const DeviceConfig& cfg, DeviceCallback* cb) override {
        Close();
        if (!cb) return false;
        std::lock_guard<std::mutex> lock(life_->mutex);
        cfg_ = cfg;
        cb_ = cb;
        if (!BuildUnit_l()) {
            DisposeUnit_l();
            return false;
        }
        AddListeners_l();
        opened_ = true;
        return true;
    }

    bool Start() override {
        std::lock_guard<std::mutex> lock(life_->mutex);
        if (!opened_) return false;
        if (wantRunning_ && (unitRunning_ || Interrupted_l())) return true;
        const bool wasWanted = wantRunning_;
        wantRunning_ = true;
        if (!unit_) {
            // A rebuild after a device change failed earlier: try again and announce the format.
            if (!BuildUnit_l()) {
                DisposeUnit_l();
                wantRunning_ = wasWanted;
                return false;
            }
            announcePending_ = true;
        }
        if (announcePending_) Announce_l(); // a change while we were not started
        if (Interrupted_l()) return true;   // resumes when the interruption ends
        if (!StartUnit_l()) {
#if TARGET_OS_IPHONE
            // Almost always the session cannot be activated right now (a call,
            // another app's non-mixable audio): keep the request and retry
            // instead of failing the engine.
            ScheduleRetry_l();
            return true;
#else
            wantRunning_ = wasWanted;
            return false;
#endif
        }
        return true;
    }

    void Stop() override {
        std::lock_guard<std::mutex> lock(life_->mutex);
        wantRunning_ = false;
        StopUnit_l();
    }

    void Close() override {
        std::lock_guard<std::mutex> lock(life_->mutex);
        wantRunning_ = false;
        RemoveListeners_l();
        DisposeUnit_l();
        opened_ = false;
        announcePending_ = false;
        retryCount_ = 0; // a retry still queued finds opened_ false
#if TARGET_OS_IPHONE
        interrupted_ = false;
#endif
    }

    DeviceInfo Info() const override { return CurrentInfo(); }

private:
    // ── Render (CoreAudio IO thread) ───────────────────────────────────

    static OSStatus RenderProc(void* refCon, AudioUnitRenderActionFlags* ioActionFlags,
                               const AudioTimeStamp* inTimeStamp, UInt32 /*inBusNumber*/,
                               UInt32 inNumberFrames, AudioBufferList* ioData) {
        return static_cast<CoreAudioDevice*>(refCon)->Render(ioActionFlags, inTimeStamp, inNumberFrames, ioData);
    }

    // Lock-free: atomics and fields that only change while rendering is gated off.
    OSStatus Render(AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts, UInt32 frames,
                    AudioBufferList* io) {
        inCallback_.fetch_add(1);
        if (!io) {
            inCallback_.fetch_sub(1);
            return noErr;
        }
        if (!renderEnabled_.load()) {
            Silence(io);
            if (flags) *flags |= kAudioUnitRenderAction_OutputIsSilence;
            inCallback_.fetch_sub(1);
            return noErr;
        }

        const int64_t now = HostTimeNs();
        const int64_t latencyNs = outputLatencyNs_.load(std::memory_order_relaxed);
        // Judge the timestamp itself, not timestamp + latency: AirPlay and some
        // USB/Bluetooth devices legitimately add more than a second.
        int64_t presentNs = 0;
        if (ts && (ts->mFlags & kAudioTimeStampHostTimeValid)) {
            const int64_t hostNs = TicksToNs(ts->mHostTime);
            if (hostNs > now - kMaxPlausibleLatencyNs && hostNs < now + kMaxPlausibleLatencyNs)
                presentNs = std::max(hostNs, now) + latencyNs;
        }
        if (presentNs == 0) presentNs = now + FramesToNs(rtBufferFrames_, rtRate_) + latencyNs;

        const int channels = rtChannels_;
        AudioBuffer& first = io->mBuffers[0];
        if (io->mNumberBuffers == 1 && static_cast<int>(first.mNumberChannels) == channels && first.mData) {
            // The interleaved float format we set: render in place.
            const int capacity = static_cast<int>(first.mDataByteSize / (sizeof(float) * channels));
            cb_->OnRender(static_cast<float*>(first.mData), std::min(static_cast<int>(frames), capacity),
                          channels, presentNs);
        } else {
            RenderViaScratch(io, static_cast<int>(frames), presentNs);
        }
        inCallback_.fetch_sub(1);
        return noErr;
    }

    // Fallback for a buffer layout other than the one we asked for (e.g.
    // non-interleaved): render interleaved into the preallocated scratch and
    // spread its channels over the AudioBuffers.
    void RenderViaScratch(AudioBufferList* io, int frames, int64_t presentNs) {
        const int channels = rtChannels_;
        float* scratch = scratch_.data();
        if (scratchFrames_ <= 0 || !scratch) {
            Silence(io);
            return;
        }
        for (int done = 0; done < frames;) {
            const int n = std::min(frames - done, scratchFrames_);
            cb_->OnRender(scratch, n, channels, presentNs + FramesToNs(done, rtRate_));
            int channelBase = 0;
            for (UInt32 b = 0; b < io->mNumberBuffers; ++b) {
                AudioBuffer& buf = io->mBuffers[b];
                const int bufChannels = static_cast<int>(buf.mNumberChannels);
                if (!buf.mData || bufChannels <= 0) continue;
                const int capacity = static_cast<int>(buf.mDataByteSize / (sizeof(float) * bufChannels));
                float* dst = static_cast<float*>(buf.mData);
                for (int i = 0; i < n && done + i < capacity; ++i) {
                    for (int c = 0; c < bufChannels; ++c) {
                        const int src = channelBase + c;
                        dst[(done + i) * bufChannels + c] = src < channels ? scratch[i * channels + src] : 0.0f;
                    }
                }
                channelBase += bufChannels;
            }
            done += n;
        }
    }

    static void Silence(AudioBufferList* io) {
        for (UInt32 b = 0; b < io->mNumberBuffers; ++b)
            if (io->mBuffers[b].mData) std::memset(io->mBuffers[b].mData, 0, io->mBuffers[b].mDataByteSize);
    }

    // Same conversion as HostTimeNs() in Core/Platform.cpp.
    int64_t TicksToNs(uint64_t ticks) const {
        if (tbNumer_ == tbDenom_) return static_cast<int64_t>(ticks);
        return static_cast<int64_t>((__uint128_t)ticks * tbNumer_ / tbDenom_);
    }

    // ── Unit lifecycle (caller holds life_->mutex) ─────────────────────

    // Queries the hardware, creates and initializes the output unit and
    // publishes the format. The unit is left stopped.
    bool BuildUnit_l() {
        const int channels = cfg_.channels > 0 ? cfg_.channels : kDefaultChannels;
        int rate = 0;
        int bufferFrames = 0;

#if TARGET_OS_IPHONE
        @autoreleasepool {
            AVAudioSession* session = [AVAudioSession sharedInstance];
            // Activating during an interruption would interrupt the app that took over.
            if (!interrupted_) ActivateSession_l();
            const double hwRate = session.sampleRate;
            if (!(hwRate > 0.0)) return false;
            const int frames = cfg_.bufferFrames > 0 ? cfg_.bufferFrames : kDefaultIosBufferFrames;
            NSError* error = nil;
            if (![session setPreferredIOBufferDuration:frames / hwRate error:&error])
                os_log_error(OS_LOG_DEFAULT, "UNAudio CoreAudio: setPreferredIOBufferDuration failed (%ld)",
                             static_cast<long>(error.code));
            rate = static_cast<int>(std::lround(session.sampleRate));
            if (rate <= 0) return false;
            bufferFrames = static_cast<int>(std::lround(session.IOBufferDuration * session.sampleRate));
        }
        const OSType subType = kAudioUnitSubType_RemoteIO;
#else
        const AudioObjectID device = DefaultOutputDevice();
        if (device == kAudioObjectUnknown) return false;
        rate = static_cast<int>(std::lround(NominalSampleRate(device)));
        if (rate <= 0) return false;
        {
            // The IO buffer size is a device property: it also applies to the
            // other clients of this device in our process (Unity's own output).
            AudioValueRange range{};
            UInt32 frames = cfg_.bufferFrames > 0 ? static_cast<UInt32>(cfg_.bufferFrames) : kMinAutoBufferFrames;
            if (GetHalProperty(device, kAudioDevicePropertyBufferFrameSizeRange, kAudioObjectPropertyScopeGlobal,
                               range)) {
                const UInt32 lo = static_cast<UInt32>(range.mMinimum);
                const UInt32 hi = static_cast<UInt32>(range.mMaximum);
                if (cfg_.bufferFrames <= 0) frames = std::max(lo, kMinAutoBufferFrames);
                if (hi >= lo) frames = std::min(std::max(frames, lo), hi);
            }
            const AudioObjectPropertyAddress address = {kAudioDevicePropertyBufferFrameSize,
                                                        kAudioObjectPropertyScopeGlobal, kElementMain};
            Check(AudioObjectSetPropertyData(device, &address, 0, nullptr, sizeof(frames), &frames),
                  "set kAudioDevicePropertyBufferFrameSize");
        }
        device_ = device;
        AddDeviceListener_l(device);
        const OSType subType = kAudioUnitSubType_HALOutput;
#endif

        AudioComponentDescription desc{};
        desc.componentType = kAudioUnitType_Output;
        desc.componentSubType = subType;
        desc.componentManufacturer = kAudioUnitManufacturer_Apple;
        AudioComponent component = AudioComponentFindNext(nullptr, &desc);
        if (!component) return false;
        if (!Check(AudioComponentInstanceNew(component, &unit_), "AudioComponentInstanceNew")) {
            unit_ = nullptr;
            return false;
        }

        // Output is enabled and input disabled by default on both units; make
        // output explicit and leave input (and with it the microphone) alone.
        const UInt32 enable = 1;
        if (!Check(AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0,
                                        &enable, sizeof(enable)), "EnableIO"))
            return false;
#if !TARGET_OS_IPHONE
        if (!Check(AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
                                        &device, sizeof(device)), "CurrentDevice"))
            return false;
#endif

        AudioStreamBasicDescription format{};
        format.mSampleRate = rate;
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked; // interleaved
        format.mFramesPerPacket = 1;
        format.mChannelsPerFrame = static_cast<UInt32>(channels);
        format.mBitsPerChannel = 32;
        format.mBytesPerFrame = static_cast<UInt32>(sizeof(float) * channels);
        format.mBytesPerPacket = format.mBytesPerFrame;
        if (!Check(AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0,
                                        &format, sizeof(format)), "StreamFormat"))
            return false;

        const UInt32 maxSlice = std::max(kMaxFramesPerSlice, static_cast<UInt32>(std::max(bufferFrames, 0)));
        Check(AudioUnitSetProperty(unit_, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0,
                                   &maxSlice, sizeof(maxSlice)), "MaximumFramesPerSlice");

        AURenderCallbackStruct callback{};
        callback.inputProc = &CoreAudioDevice::RenderProc;
        callback.inputProcRefCon = this;
        if (!Check(AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0,
                                        &callback, sizeof(callback)), "SetRenderCallback"))
            return false;

        if (!Check(AudioUnitInitialize(unit_), "AudioUnitInitialize")) return false;

        int latencyFrames = 0;
        int64_t latencyNs = 0;
#if TARGET_OS_IPHONE
        @autoreleasepool {
            const double outputLatency = [AVAudioSession sharedInstance].outputLatency;
            latencyNs = static_cast<int64_t>(std::llround(std::max(outputLatency, 0.0) * 1e9));
            latencyFrames = bufferFrames + static_cast<int>(std::lround(std::max(outputLatency, 0.0) * rate));
        }
#else
        {
            UInt32 actual = 0;
            if (GetHalProperty(device, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, actual) &&
                actual > 0)
                bufferFrames = static_cast<int>(actual);
            UInt32 deviceLatency = 0, safetyOffset = 0;
            GetHalProperty(device, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput, deviceLatency);
            GetHalProperty(device, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeOutput, safetyOffset);
            const UInt32 streamLatency = FirstOutputStreamLatency(device);
            Float64 unitLatency = 0.0;
            UInt32 size = sizeof(unitLatency);
            if (AudioUnitGetProperty(unit_, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &unitLatency,
                                     &size) != noErr || !(unitLatency > 0.0))
                unitLatency = 0.0;
            // The timestamp already covers buffer + safety offset (see top of file).
            latencyNs = FramesToNs(static_cast<int64_t>(deviceLatency) + streamLatency, rate) +
                        static_cast<int64_t>(std::llround(unitLatency * 1e9));
            latencyFrames = bufferFrames + static_cast<int>(safetyOffset + deviceLatency + streamLatency) +
                            static_cast<int>(std::lround(unitLatency * rate));
        }
#endif
        if (bufferFrames <= 0) bufferFrames = static_cast<int>(maxSlice);

        // Rendering is gated off (no unit yet / StopUnit_l ran), so these are safe to write.
        rtChannels_ = channels;
        rtRate_ = rate;
        rtBufferFrames_ = bufferFrames;
        scratchFrames_ = static_cast<int>(maxSlice);
        scratch_.assign(static_cast<size_t>(scratchFrames_) * channels, 0.0f);
        outputLatencyNs_.store(latencyNs, std::memory_order_relaxed);
        PublishInfo(rate, channels, bufferFrames, latencyFrames);
        return true;
    }

    void DisposeUnit_l() {
        StopUnit_l();
#if !TARGET_OS_IPHONE
        RemoveDeviceListener_l();
        device_ = kAudioObjectUnknown;
#endif
        if (unit_) {
            AudioUnitUninitialize(unit_);
            AudioComponentInstanceDispose(unit_);
            unit_ = nullptr;
        }
    }

    bool StartUnit_l() {
        if (!unit_) return false;
        if (unitRunning_) return true;
        renderEnabled_.store(true);
        OSStatus status = AudioOutputUnitStart(unit_);
#if TARGET_OS_IPHONE
        if (status != noErr) {
            // The session may have been deactivated behind our back (another app, Unity): retry once.
            ActivateSession_l();
            status = AudioOutputUnitStart(unit_);
        }
#endif
        if (!Check(status, "AudioOutputUnitStart")) {
            GateRender_l();
            return false;
        }
        unitRunning_ = true;
        retryCount_ = 0;
        return true;
    }

    // Returns once no Render() is executing and none will reach OnRender.
    void StopUnit_l() {
        renderEnabled_.store(false);
        if (unit_ && unitRunning_) Check(AudioOutputUnitStop(unit_), "AudioOutputUnitStop");
        unitRunning_ = false;
        GateRender_l();
    }

    void GateRender_l() {
        renderEnabled_.store(false);
        while (inCallback_.load() != 0) std::this_thread::yield();
    }

    // Tears the unit down and builds it again for the current hardware, then
    // announces the format before callbacks resume.
    void Rebuild_l() {
        if (!opened_) return;
        DisposeUnit_l();
        if (!BuildUnit_l()) {
            DisposeUnit_l();
            os_log_error(OS_LOG_DEFAULT, "UNAudio CoreAudio: rebuild failed");
            if (wantRunning_) ScheduleRetry_l();
            return;
        }
        Announce_l();
        // On failure wantRunning_ stays set: retried with backoff, on the next
        // change, on app activation (iOS) or by Start().
        if (wantRunning_ && !Interrupted_l() && !StartUnit_l()) ScheduleRetry_l();
    }

    // Delivers OnDeviceChanged; the unit must be stopped. Until the owner has
    // started the device (between Open and Start, or after Stop) the owner is
    // still setting up or tearing down its own state, so the announcement is
    // deferred to the next Start() instead of racing it.
    void Announce_l() {
        if (!wantRunning_) {
            announcePending_ = true;
            return;
        }
        announcePending_ = false;
        cb_->OnDeviceChanged(CurrentInfo());
    }

    // Retries a failed rebuild or start on the queue with a capped backoff.
    void ScheduleRetry_l() {
        if (retryPending_ || retryCount_ >= kMaxRetries) return;
        retryPending_ = true;
        const int64_t delayMs = std::min(kRetryBaseMs << retryCount_, kRetryMaxMs);
        ++retryCount_;
        PostWork(queue_, life_, ^(CoreAudioDevice* device) { device->OnRetry_l(); }, delayMs);
    }

    void OnRetry_l() {
        retryPending_ = false;
        if (!opened_ || !wantRunning_ || unitRunning_ || Interrupted_l()) return;
        if (!unit_ || HardwareChanged_l()) {
            Rebuild_l();
            return;
        }
#if TARGET_OS_IPHONE
        ActivateSession_l();
#endif
        if (!StartUnit_l()) ScheduleRetry_l();
    }

    bool Interrupted_l() const {
#if TARGET_OS_IPHONE
        return interrupted_;
#else
        return false;
#endif
    }

    // ── Notifications ──────────────────────────────────────────────────

    // Runs `work` on the private queue under the control mutex (after
    // `delayMs`), unless the device has been destroyed by then. Never blocks
    // the caller.
    static void PostWork(dispatch_queue_t queue, std::weak_ptr<DeviceLife> weak, DeviceWork work,
                         int64_t delayMs = 0) {
        dispatch_block_t block = ^{
            std::shared_ptr<DeviceLife> life = weak.lock();
            if (!life) return;
            std::lock_guard<std::mutex> lock(life->mutex);
            if (life->owner) work(life->owner);
        };
        if (delayMs > 0)
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delayMs * 1000000LL), queue, block);
        else
            dispatch_async(queue, block);
    }

#if TARGET_OS_IPHONE
    void ActivateSession_l() {
        @autoreleasepool {
            NSError* error = nil;
            if (![[AVAudioSession sharedInstance] setActive:YES error:&error])
                os_log_error(OS_LOG_DEFAULT, "UNAudio CoreAudio: AVAudioSession setActive failed (%ld)",
                             static_cast<long>(error.code));
        }
    }

    void AddListeners_l() {
        @autoreleasepool {
            std::weak_ptr<DeviceLife> weak = life_;
            dispatch_queue_t queue = queue_;
            NSNotificationCenter* center = [NSNotificationCenter defaultCenter];

            // object:nil — not every AVAudioSession notification names the
            // shared instance as its object.
            interruptionObserver_ = [center addObserverForName:AVAudioSessionInterruptionNotification
                                                        object:nil
                                                         queue:nil
                                                    usingBlock:^(NSNotification* note) {
                NSNumber* type = note.userInfo[AVAudioSessionInterruptionTypeKey];
                NSNumber* options = note.userInfo[AVAudioSessionInterruptionOptionKey];
                const bool began = type && type.unsignedIntegerValue == AVAudioSessionInterruptionTypeBegan;
                const bool shouldResume =
                    options && (options.unsignedIntegerValue & AVAudioSessionInterruptionOptionShouldResume);
                // The system deactivates the session of a suspended app and
                // reports it with a late "began" once the app runs again.
                bool wasSuspended = false;
#if !TARGET_OS_TV
                if (@available(iOS 14.5, *)) {
                    NSNumber* reason = note.userInfo[AVAudioSessionInterruptionReasonKey];
                    wasSuspended =
                        reason && reason.unsignedIntegerValue == AVAudioSessionInterruptionReasonAppWasSuspended;
                } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                    NSNumber* suspended = note.userInfo[AVAudioSessionInterruptionWasSuspendedKey];
#pragma clang diagnostic pop
                    wasSuspended = suspended && suspended.boolValue;
                }
#endif
                PostWork(queue, weak, ^(CoreAudioDevice* device) {
                    device->OnInterruption_l(began, shouldResume, wasSuspended);
                });
            }];

            routeObserver_ = [center addObserverForName:AVAudioSessionRouteChangeNotification
                                                 object:nil
                                                  queue:nil
                                             usingBlock:^(NSNotification*) {
                PostWork(queue, weak, ^(CoreAudioDevice* device) { device->OnRouteChange_l(); });
            }];

            resetObserver_ = [center addObserverForName:AVAudioSessionMediaServicesWereResetNotification
                                                 object:nil
                                                  queue:nil
                                             usingBlock:^(NSNotification*) {
                PostWork(queue, weak, ^(CoreAudioDevice* device) { device->OnMediaServicesReset_l(); });
            }];

            // An interruption does not always deliver its "ended" notification
            // (and one without ShouldResume waits for the app to come back), so
            // also resume when the app becomes active. Named by string so the
            // plugin does not link UIKit (Unity's player does).
            activeObserver_ = [center addObserverForName:@"UIApplicationDidBecomeActiveNotification"
                                                  object:nil
                                                   queue:nil
                                              usingBlock:^(NSNotification*) {
                PostWork(queue, weak, ^(CoreAudioDevice* device) { device->OnBecameActive_l(); });
            }];
        }
    }

    void RemoveListeners_l() {
        @autoreleasepool {
            NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
            if (interruptionObserver_) [center removeObserver:interruptionObserver_];
            if (routeObserver_) [center removeObserver:routeObserver_];
            if (resetObserver_) [center removeObserver:resetObserver_];
            if (activeObserver_) [center removeObserver:activeObserver_];
            interruptionObserver_ = nil;
            routeObserver_ = nil;
            resetObserver_ = nil;
            activeObserver_ = nil;
        }
    }

    void OnInterruption_l(bool began, bool shouldResume, bool wasSuspended) {
        if (!opened_) return;
        if (began && wasSuspended) {
            // Not a live interruption: we are running again, so recover now.
            SyncRunning_l();
            Resume_l();
            return;
        }
        if (began) {
            interrupted_ = true;
            StopUnit_l(); // keeps wantRunning_
            return;
        }
        // Without ShouldResume, OnBecameActive_l resumes when the app returns.
        if (shouldResume) Resume_l();
    }

    void OnBecameActive_l() {
        if (!opened_) return;
        SyncRunning_l();
        retryCount_ = 0; // back in the foreground: a fresh retry budget
        if (interrupted_ || (wantRunning_ && !unitRunning_)) Resume_l();
    }

    // The unit can be stopped without a notification reaching us (e.g. the
    // session deactivated by someone else): trust the unit, not our flag.
    void SyncRunning_l() {
        if (!unit_ || !unitRunning_) return;
        UInt32 running = 1;
        UInt32 size = sizeof(running);
        if (AudioUnitGetProperty(unit_, kAudioOutputUnitProperty_IsRunning, kAudioUnitScope_Global, 0, &running,
                                 &size) == noErr &&
            running == 0)
            StopUnit_l();
    }

    void Resume_l() {
        interrupted_ = false;
        if (!wantRunning_) return;
        if (!unit_ || HardwareChanged_l()) {
            Rebuild_l(); // starts the new unit
            return;
        }
        ActivateSession_l();
        if (!StartUnit_l()) ScheduleRetry_l();
    }

    bool HardwareChanged_l() {
        @autoreleasepool {
            const int rate = static_cast<int>(std::lround([AVAudioSession sharedInstance].sampleRate));
            return rate > 0 && rate != rtRate_;
        }
    }

    void OnRouteChange_l() {
        if (!opened_) return;
        if (!unit_ || HardwareChanged_l()) {
            Rebuild_l();
            return;
        }
        // Same rate: refresh the cached latency and buffer size in place.
        bool changed = false;
        @autoreleasepool {
            AVAudioSession* session = [AVAudioSession sharedInstance];
            const double outputLatency = std::max(session.outputLatency, 0.0);
            int bufferFrames = static_cast<int>(std::lround(session.IOBufferDuration * rtRate_));
            if (bufferFrames <= 0) bufferFrames = rtBufferFrames_;
            const int latencyFrames = bufferFrames + static_cast<int>(std::lround(outputLatency * rtRate_));
            const DeviceInfo old = CurrentInfo();
            changed = old.bufferFrames != bufferFrames || old.latencyFrames != latencyFrames;
            outputLatencyNs_.store(static_cast<int64_t>(std::llround(outputLatency * 1e9)),
                                   std::memory_order_relaxed);
            PublishInfo(rtRate_, rtChannels_, bufferFrames, latencyFrames);
        }
        if (!changed) return;
        // Let the owner pick up the new latency (e.g. speaker -> AirPods). The
        // route change has already broken continuity, so a stop/start is cheap.
        const bool wasRunning = unitRunning_;
        StopUnit_l();
        Announce_l();
        if (wasRunning && !StartUnit_l()) ScheduleRetry_l();
    }

    void OnMediaServicesReset_l() {
        if (!opened_) return;
        // Every audio object is invalid after a reset (QA1749): rebuild.
        interrupted_ = false;
        Rebuild_l();
    }
#else
    void AddListeners_l() {
        std::weak_ptr<DeviceLife> weak = life_;
        dispatch_queue_t queue = queue_;
        defaultDeviceListener_ = ^(UInt32, const AudioObjectPropertyAddress*) {
            PostWork(queue, weak, ^(CoreAudioDevice* device) { device->OnDefaultDeviceChanged_l(); });
        };
        const AudioObjectPropertyAddress address = {kAudioHardwarePropertyDefaultOutputDevice,
                                                    kAudioObjectPropertyScopeGlobal, kElementMain};
        // A null queue invokes the block directly on a HAL thread; it only enqueues.
        if (!Check(AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &address, nullptr,
                                                       defaultDeviceListener_),
                   "listen kAudioHardwarePropertyDefaultOutputDevice"))
            defaultDeviceListener_ = nil;
    }

    void RemoveListeners_l() {
        if (!defaultDeviceListener_) return;
        const AudioObjectPropertyAddress address = {kAudioHardwarePropertyDefaultOutputDevice,
                                                    kAudioObjectPropertyScopeGlobal, kElementMain};
        AudioObjectRemovePropertyListenerBlock(kAudioObjectSystemObject, &address, nullptr, defaultDeviceListener_);
        defaultDeviceListener_ = nil;
    }

    // Nominal rate changes and device death on the device in use.
    static constexpr AudioObjectPropertySelector kDeviceSelectors[2] = {kAudioDevicePropertyNominalSampleRate,
                                                                        kAudioDevicePropertyDeviceIsAlive};

    void AddDeviceListener_l(AudioObjectID device) {
        RemoveDeviceListener_l();
        std::weak_ptr<DeviceLife> weak = life_;
        dispatch_queue_t queue = queue_;
        AudioObjectPropertyListenerBlock block = ^(UInt32, const AudioObjectPropertyAddress*) {
            PostWork(queue, weak, ^(CoreAudioDevice* dev) { dev->OnDeviceFormatChanged_l(); });
        };
        bool any = false;
        for (AudioObjectPropertySelector selector : kDeviceSelectors) {
            const AudioObjectPropertyAddress address = {selector, kAudioObjectPropertyScopeGlobal, kElementMain};
            any |= Check(AudioObjectAddPropertyListenerBlock(device, &address, nullptr, block),
                         "listen output device property");
        }
        if (any) {
            deviceListener_ = block;
            listenedDevice_ = device;
        }
    }

    void RemoveDeviceListener_l() {
        if (!deviceListener_) return;
        for (AudioObjectPropertySelector selector : kDeviceSelectors) {
            const AudioObjectPropertyAddress address = {selector, kAudioObjectPropertyScopeGlobal, kElementMain};
            AudioObjectRemovePropertyListenerBlock(listenedDevice_, &address, nullptr, deviceListener_);
        }
        deviceListener_ = nil;
        listenedDevice_ = kAudioObjectUnknown;
    }

    void OnDefaultDeviceChanged_l() {
        if (!opened_) return;
        if (unit_ && DefaultOutputDevice() == device_) return; // coalesced or already handled
        retryCount_ = 0; // new hardware: a fresh retry budget
        Rebuild_l();
    }

    void OnDeviceFormatChanged_l() {
        if (!opened_) return;
        if (unit_ && !HardwareChanged_l()) return;
        Rebuild_l();
    }

    // The unit no longer matches the hardware: another default device, the
    // device gone, or a new nominal rate.
    bool HardwareChanged_l() {
        if (device_ == kAudioObjectUnknown || DefaultOutputDevice() != device_) return true;
        UInt32 alive = 0;
        if (!GetHalProperty(device_, kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal, alive) ||
            alive == 0)
            return true;
        const int rate = static_cast<int>(std::lround(NominalSampleRate(device_)));
        return rate > 0 && rate != rtRate_;
    }
#endif

    // ── Info ───────────────────────────────────────────────────────────

    void PublishInfo(int rate, int channels, int bufferFrames, int latencyFrames) {
        std::lock_guard<std::mutex> lock(infoMutex_);
        info_.sampleRate = rate;
        info_.channels = channels;
        info_.bufferFrames = bufferFrames;
        info_.latencyFrames = latencyFrames;
        info_.exclusive = false;
        info_.backend = UNA_BACKEND_COREAUDIO;
    }

    DeviceInfo CurrentInfo() const {
        std::lock_guard<std::mutex> lock(infoMutex_);
        return info_;
    }

    // ── State ──────────────────────────────────────────────────────────

    std::shared_ptr<DeviceLife> life_;
    dispatch_queue_t queue_ = nil;    // serial; all notification work runs here

    // Guarded by life_->mutex.
    DeviceConfig cfg_{};
    DeviceCallback* cb_ = nullptr;
    AudioUnit unit_ = nullptr;
    bool opened_ = false;
    bool wantRunning_ = false;        // Start() was called and Stop() was not
    bool unitRunning_ = false;
    bool announcePending_ = false;    // a format change not yet delivered (see Announce_l)
    bool retryPending_ = false;
    int retryCount_ = 0;
#if TARGET_OS_IPHONE
    bool interrupted_ = false;
    id interruptionObserver_ = nil;
    id routeObserver_ = nil;
    id resetObserver_ = nil;
    id activeObserver_ = nil;
#else
    AudioObjectID device_ = kAudioObjectUnknown;
    AudioObjectID listenedDevice_ = kAudioObjectUnknown;
    AudioObjectPropertyListenerBlock defaultDeviceListener_ = nil;
    AudioObjectPropertyListenerBlock deviceListener_ = nil;
#endif

    // Render-thread state: written only while renderEnabled_ is false and no
    // Render() is in flight; the seq_cst enable store publishes it.
    int rtChannels_ = kDefaultChannels;
    int rtRate_ = 48000;
    int rtBufferFrames_ = 0;
    int scratchFrames_ = 0;
    std::vector<float> scratch_;
    uint32_t tbNumer_ = 1;
    uint32_t tbDenom_ = 1;
    std::atomic<int64_t> outputLatencyNs_{0};
    std::atomic<int> inCallback_{0};
    std::atomic<bool> renderEnabled_{false};

    mutable std::mutex infoMutex_;
    DeviceInfo info_{};
};

} // namespace

std::unique_ptr<AudioDevice> CreatePlatformDevice() { return std::make_unique<CoreAudioDevice>(); }

} // namespace una

#endif // __APPLE__
