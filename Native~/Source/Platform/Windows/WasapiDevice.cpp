// WASAPI output backend. Event-driven render on a dedicated MMCSS thread that
// owns every COM object (MTA). Shared mode prefers IAudioClient3 small engine
// periods; exclusive mode negotiates an integer/float format and converts.
// Follows the default console render device and survives device loss.

#if defined(_WIN32)

#include "../AudioDevice.h"

#include "../../Core/Platform.h"
#include "unaudio.h"

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <ksmedia.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace una {

namespace {

constexpr DWORD kEventTimeoutMs = 200;
constexpr DWORD kRetryMinMs = 100;
constexpr DWORD kRetryMaxMs = 2000;
constexpr int64_t kHealthyNs = 1000000000LL; // a stream that ran this long is not "failing on open"
constexpr int64_t kMaxAheadNs = 1000000000LL;

template <class T> void SafeRelease(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

enum class SampleFormat { Unknown, Float32, Int32, Int24In32, Int24Packed, Int16 };

SampleFormat FormatOf(const WAVEFORMATEX* wf) {
    const WORD bits = wf->wBitsPerSample;
    bool isFloat = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    bool isPcm = wf->wFormatTag == WAVE_FORMAT_PCM;
    WORD valid = bits;
    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wf->cbSize >= 22) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
        isFloat = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != 0;
        isPcm = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) != 0;
        if (ext->Samples.wValidBitsPerSample) valid = ext->Samples.wValidBitsPerSample;
    }
    if (isFloat) return bits == 32 ? SampleFormat::Float32 : SampleFormat::Unknown;
    if (!isPcm) return SampleFormat::Unknown;
    if (bits == 16) return SampleFormat::Int16;
    if (bits == 24) return SampleFormat::Int24Packed;
    if (bits == 32) return valid == 24 ? SampleFormat::Int24In32 : SampleFormat::Int32;
    return SampleFormat::Unknown;
}

DWORD DefaultChannelMask(int channels) {
    switch (channels) {
    case 1: return KSAUDIO_SPEAKER_MONO;
    case 2: return KSAUDIO_SPEAKER_STEREO;
    case 4: return KSAUDIO_SPEAKER_QUAD;
    case 6: return KSAUDIO_SPEAKER_5POINT1;
    case 8: return KSAUDIO_SPEAKER_7POINT1_SURROUND;
    default: return 0;
    }
}

WAVEFORMATEXTENSIBLE MakeFormat(int rate, int channels, DWORD mask, WORD bits, WORD validBits, bool isFloat) {
    WAVEFORMATEXTENSIBLE f{};
    f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    f.Format.nChannels = static_cast<WORD>(channels);
    f.Format.nSamplesPerSec = static_cast<DWORD>(rate);
    f.Format.wBitsPerSample = bits;
    f.Format.nBlockAlign = static_cast<WORD>(channels * bits / 8);
    f.Format.nAvgBytesPerSec = f.Format.nSamplesPerSec * f.Format.nBlockAlign;
    f.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    f.Samples.wValidBitsPerSample = validBits;
    f.dwChannelMask = mask;
    f.SubFormat = isFloat ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
    return f;
}

REFERENCE_TIME FramesToHns(int64_t frames, int rate) {
    return static_cast<REFERENCE_TIME>(10000000.0 * static_cast<double>(frames) / rate + 0.5);
}

int HnsToFrames(REFERENCE_TIME hns, int rate) {
    return static_cast<int>(static_cast<double>(hns) * rate / 10000000.0 + 0.5);
}

inline float Clamp1(float x) { return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x); }

// float -> device format. Runs on the render thread: no allocation.
void ConvertSamples(const float* in, BYTE* out, size_t count, SampleFormat fmt) {
    switch (fmt) {
    case SampleFormat::Int16: {
        auto* o = reinterpret_cast<int16_t*>(out);
        for (size_t i = 0; i < count; ++i) o[i] = static_cast<int16_t>(std::lrint(Clamp1(in[i]) * 32767.0f));
        break;
    }
    case SampleFormat::Int32: {
        auto* o = reinterpret_cast<int32_t*>(out);
        for (size_t i = 0; i < count; ++i)
            o[i] = static_cast<int32_t>(std::llrint(static_cast<double>(Clamp1(in[i])) * 2147483647.0));
        break;
    }
    case SampleFormat::Int24In32: {
        // 24 valid bits, MSB-aligned in a 32-bit container.
        auto* o = reinterpret_cast<int32_t*>(out);
        for (size_t i = 0; i < count; ++i)
            o[i] = static_cast<int32_t>(static_cast<uint32_t>(std::lrint(Clamp1(in[i]) * 8388607.0f)) << 8);
        break;
    }
    case SampleFormat::Int24Packed: {
        for (size_t i = 0; i < count; ++i) {
            const int32_t v = static_cast<int32_t>(std::lrint(Clamp1(in[i]) * 8388607.0f));
            out[i * 3 + 0] = static_cast<BYTE>(v & 0xff);
            out[i * 3 + 1] = static_cast<BYTE>((v >> 8) & 0xff);
            out[i * 3 + 2] = static_cast<BYTE>((v >> 16) & 0xff);
        }
        break;
    }
    case SampleFormat::Float32:
        std::memcpy(out, in, count * sizeof(float));
        break;
    default:
        break;
    }
}

// Sets a flag and wakes the render thread when the default console render
// device changes. Called on an OS thread: must not touch WASAPI.
class NotificationClient final : public IMMNotificationClient {
public:
    NotificationClient(std::atomic<bool>& flag, const HANDLE& wake) : flag_(flag), wake_(wake) {}

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs_; } // owned by the device, never deleted here
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(IMMNotificationClient))) {
            *out = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole) {
            flag_.store(true, std::memory_order_release);
            SetEvent(wake_);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    std::atomic<ULONG> refs_{1};
    std::atomic<bool>& flag_;
    const HANDLE& wake_; // the device's wake event; valid while registered
};

class WasapiDevice final : public AudioDevice {
public:
    WasapiDevice() : notifier_(deviceChanged_, wakeEvent_) {}
    ~WasapiDevice() override { Close(); }

    bool Open(const DeviceConfig& cfg, DeviceCallback* cb) override {
        std::lock_guard<std::mutex> api(apiMutex_);
        CloseLocked();
        if (!cb) return false;
        cfg_ = cfg;
        cb_ = cb;
        wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        renderEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!wakeEvent_ || !renderEvent_) { CloseLocked(); return false; }
        quit_.store(false);
        wantRun_.store(false);
        deviceChanged_.store(false);
        {
            std::lock_guard<std::mutex> lk(ctlMutex_);
            openResult_ = 0;
            threadExited_ = false;
            ackSeq_ = cmdSeq_.load();
        }
        thread_ = std::thread([this] { Run(); });
        bool ok;
        {
            std::unique_lock<std::mutex> lk(ctlMutex_);
            ctlCv_.wait(lk, [this] { return openResult_ != 0; });
            ok = openResult_ > 0;
        }
        if (!ok) CloseLocked();
        return ok;
    }

    bool Start() override {
        std::lock_guard<std::mutex> api(apiMutex_);
        if (!thread_.joinable()) return false;
        wantRun_.store(true, std::memory_order_release);
        SendCommand();
        return true; // with no device present the stream starts once one appears
    }

    void Stop() override {
        std::lock_guard<std::mutex> api(apiMutex_);
        if (!thread_.joinable()) return;
        wantRun_.store(false, std::memory_order_release);
        SendCommand();
    }

    void Close() override {
        std::lock_guard<std::mutex> api(apiMutex_);
        CloseLocked();
    }

    DeviceInfo Info() const override {
        std::lock_guard<std::mutex> lk(infoMutex_);
        return info_;
    }

private:
    // -- Control (caller threads) --

    // Publishes the new wantRun_ and blocks until the render thread has
    // applied it, i.e. after Stop() returns no OnRender is in flight.
    void SendCommand() {
        const uint64_t seq = cmdSeq_.fetch_add(1, std::memory_order_acq_rel) + 1;
        SetEvent(wakeEvent_);
        std::unique_lock<std::mutex> lk(ctlMutex_);
        ctlCv_.wait(lk, [&] { return ackSeq_ >= seq || threadExited_; });
    }

    void CloseLocked() {
        if (thread_.joinable()) {
            quit_.store(true, std::memory_order_release);
            SetEvent(wakeEvent_);
            thread_.join();
        }
        if (wakeEvent_) { CloseHandle(wakeEvent_); wakeEvent_ = nullptr; }
        if (renderEvent_) { CloseHandle(renderEvent_); renderEvent_ = nullptr; }
        scratch_ = std::vector<float>();
        std::lock_guard<std::mutex> lk(infoMutex_);
        info_ = DeviceInfo{};
    }

    void Ack(uint64_t seq) {
        std::lock_guard<std::mutex> lk(ctlMutex_);
        if (seq > ackSeq_) ackSeq_ = seq;
        ctlCv_.notify_all();
    }

    // -- Render thread --

    void Run() {
        SetCurrentThreadName("UNAudio WASAPI");
        SetCurrentThreadRole(ThreadRole::AudioRealtime);
        const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        bool ok = SUCCEEDED(coHr) &&
                  SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                             __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator_)));
        if (ok) registered_ = SUCCEEDED(enumerator_->RegisterEndpointNotificationCallback(&notifier_));
        ok = ok && OpenStream();
        // Read before Open() returns: no command can have been sent yet, so
        // every later command is seen as new and acknowledged.
        const uint64_t acked = cmdSeq_.load(std::memory_order_acquire);
        {
            std::lock_guard<std::mutex> lk(ctlMutex_);
            openResult_ = ok ? 1 : -1;
            ctlCv_.notify_all();
        }

        if (ok) Loop(acked);

        ReleaseStream();
        if (registered_) enumerator_->UnregisterEndpointNotificationCallback(&notifier_);
        registered_ = false;
        SafeRelease(enumerator_);
        if (SUCCEEDED(coHr)) CoUninitialize();
        std::lock_guard<std::mutex> lk(ctlMutex_);
        threadExited_ = true;
        ctlCv_.notify_all();
    }

    void Loop(uint64_t acked) {
        bool streaming = false;
        bool needReopen = false;
        bool notifyPending = false; // reopened; OnDeviceChanged owed before the next OnRender
        DWORD retryMs = kRetryMinMs;
        int64_t nextTryNs = 0;
        int64_t openedNs = HostTimeNs();

        // The stream failed. Retry at once if it had been healthy for a while;
        // otherwise back off, so a device that opens but keeps failing cannot
        // spin reopen cycles at real-time priority.
        auto lost = [&] {
            needReopen = true;
            const int64_t now = HostTimeNs();
            if (now - openedNs > kHealthyNs) {
                retryMs = kRetryMinMs;
                nextTryNs = 0;
            } else {
                nextTryNs = now + static_cast<int64_t>(retryMs) * 1000000LL;
                retryMs = std::min<DWORD>(retryMs * 2, kRetryMaxMs);
            }
        };

        for (;;) {
            const uint64_t seq = cmdSeq_.load(std::memory_order_acquire);
            if (quit_.load(std::memory_order_acquire)) break;
            const bool want = wantRun_.load(std::memory_order_acquire);

            if (deviceChanged_.exchange(false, std::memory_order_acq_rel)) {
                needReopen = true;
                nextTryNs = 0; // a new default device: try it now
            }
            if (needReopen && streaming) { StopStream(); streaming = false; }
            if (needReopen && HostTimeNs() >= nextTryNs) {
                ReleaseStream();
                if (OpenStream()) {
                    needReopen = false;
                    notifyPending = true;
                    openedNs = HostTimeNs();
                } else {
                    nextTryNs = HostTimeNs() + static_cast<int64_t>(retryMs) * 1000000LL;
                    retryMs = std::min<DWORD>(retryMs * 2, kRetryMaxMs);
                }
            }
            if (!needReopen) {
                if (want && !streaming) {
                    // Only while started: callbacks never run between Open and
                    // Start or after Stop, so a reopen while stopped is
                    // reported when the stream next starts.
                    if (notifyPending) {
                        notifyPending = false;
                        cb_->OnDeviceChanged(Info());
                    }
                    streaming = StartStream();
                    if (!streaming) lost();
                } else if (!want && streaming) {
                    StopStream();
                    streaming = false;
                }
            }
            if (seq != acked) { Ack(seq); acked = seq; }

            if (needReopen) {
                // No usable device: output nothing and wait for the next
                // attempt while staying responsive to commands.
                const int64_t waitNs = nextTryNs - HostTimeNs();
                WaitForSingleObject(wakeEvent_, waitNs > 0 ? static_cast<DWORD>(waitNs / 1000000 + 1) : 0);
                continue;
            }
            if (!streaming) {
                WaitForSingleObject(wakeEvent_, INFINITE);
                continue;
            }

            HANDLE handles[2] = {wakeEvent_, renderEvent_};
            const DWORD r = WaitForMultipleObjects(2, handles, FALSE, kEventTimeoutMs);
            if (r == WAIT_OBJECT_0 + 1) {
                if (!RenderOnce()) lost();
            } else if (r == WAIT_TIMEOUT) {
                // Events stopped arriving: check whether the device is gone.
                UINT32 padding = 0;
                if (FAILED(client_->GetCurrentPadding(&padding))) lost();
            }
        }
        if (streaming) StopStream();
    }

    bool OpenStream() {
        IMMDevice* device = nullptr;
        if (FAILED(enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device))) return false;
        device_ = device;

        WAVEFORMATEX* mix = nullptr;
        {
            IAudioClient* probe = nullptr;
            if (FAILED(device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&probe)))) {
                ReleaseStream();
                return false;
            }
            const HRESULT hr = probe->GetMixFormat(&mix);
            probe->Release();
            if (FAILED(hr) || !mix) { ReleaseStream(); return false; }
        }

        bool ok = false;
        if (cfg_.exclusive) ok = InitExclusive(mix);
        if (!ok) ok = InitShared(mix);
        CoTaskMemFree(mix);
        if (!ok || !FinishInit()) { ReleaseStream(); return false; }
        return true;
    }

    bool Activate() {
        SafeRelease(client_);
        return SUCCEEDED(device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client_)));
    }

    bool InitShared(const WAVEFORMATEX* mix) {
        exclusive_ = false;
        rate_ = static_cast<int>(mix->nSamplesPerSec);
        channels_ = mix->nChannels;
        format_ = FormatOf(mix);
        if (format_ == SampleFormat::Unknown) return false;

        // Preferred: IAudioClient3 with the smallest engine period that
        // satisfies the request.
        IAudioClient3* client3 = nullptr;
        if (SUCCEEDED(device_->Activate(__uuidof(IAudioClient3), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client3)))) {
            UINT32 defP = 0, fundP = 0, minP = 0, maxP = 0;
            HRESULT hr = client3->GetSharedModeEnginePeriod(mix, &defP, &fundP, &minP, &maxP);
            if (SUCCEEDED(hr) && fundP > 0 && minP > 0) {
                // Periods must be multiples of the fundamental within [min, max].
                UINT32 period = minP;
                const UINT32 req = cfg_.bufferFrames > 0 ? static_cast<UINT32>(cfg_.bufferFrames) : 0;
                if (req > minP) period = std::min(maxP, (req + fundP - 1) / fundP * fundP);
                hr = client3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, mix, nullptr);
                if (SUCCEEDED(hr)) {
                    WAVEFORMATEX* cur = nullptr;
                    UINT32 curPeriod = 0;
                    if (SUCCEEDED(client3->GetCurrentSharedModeEnginePeriod(&cur, &curPeriod)) && curPeriod > 0)
                        period = curPeriod;
                    if (cur) CoTaskMemFree(cur);
                    SafeRelease(client_);
                    client_ = client3;
                    periodFrames_ = static_cast<int>(period);
                    return true;
                }
            }
            client3->Release();
        }

        // Fallback: legacy event-driven shared stream.
        if (!Activate()) return false;
        const REFERENCE_TIME bufferHns = cfg_.bufferFrames > 0 ? FramesToHns(cfg_.bufferFrames, rate_) : 0;
        if (FAILED(client_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, bufferHns, 0, mix, nullptr)))
            return false;
        REFERENCE_TIME defHns = 0, minHns = 0;
        if (FAILED(client_->GetDevicePeriod(&defHns, &minHns))) return false;
        periodFrames_ = std::max(1, HnsToFrames(defHns, rate_));
        return true;
    }

    bool InitExclusive(const WAVEFORMATEX* mix) {
        exclusive_ = true;
        channels_ = mix->nChannels;
        DWORD mask = DefaultChannelMask(channels_);
        if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mix->cbSize >= 22)
            mask = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix)->dwChannelMask;

        struct Candidate { WORD bits, valid; bool isFloat; };
        const Candidate candidates[] = {{32, 32, true}, {32, 32, false}, {32, 24, false}, {24, 24, false}, {16, 16, false}};
        const int mixRate = static_cast<int>(mix->nSamplesPerSec);
        const int rates[2] = {cfg_.sampleRate > 0 ? cfg_.sampleRate : mixRate, mixRate};

        if (!Activate()) return false;
        WAVEFORMATEXTENSIBLE fmt{};
        bool found = false;
        for (int ri = 0; ri < 2 && !found; ++ri) {
            if (ri == 1 && rates[1] == rates[0]) break;
            for (const Candidate& c : candidates) {
                fmt = MakeFormat(rates[ri], channels_, mask, c.bits, c.valid, c.isFloat);
                if (client_->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &fmt.Format, nullptr) == S_OK) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) return false;
        rate_ = static_cast<int>(fmt.Format.nSamplesPerSec);
        format_ = FormatOf(&fmt.Format);

        REFERENCE_TIME defHns = 0, minHns = 0;
        if (FAILED(client_->GetDevicePeriod(&defHns, &minHns))) return false;
        REFERENCE_TIME period = minHns;
        if (cfg_.bufferFrames > 0) period = std::max(minHns, FramesToHns(cfg_.bufferFrames, rate_));

        HRESULT hr = client_->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period,
                                         &fmt.Format, nullptr);
        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
            UINT32 aligned = 0;
            if (FAILED(client_->GetBufferSize(&aligned)) || aligned == 0) return false;
            if (!Activate()) return false;
            period = FramesToHns(aligned, rate_);
            hr = client_->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period,
                                     &fmt.Format, nullptr);
        }
        if (FAILED(hr)) { SafeRelease(client_); return false; }
        periodFrames_ = HnsToFrames(period, rate_);
        return true;
    }

    // Common tail of both init paths: services, scratch and DeviceInfo.
    bool FinishInit() {
        UINT32 bufferFrames = 0;
        if (FAILED(client_->GetBufferSize(&bufferFrames)) || bufferFrames == 0) return false;
        if (FAILED(client_->SetEventHandle(renderEvent_))) return false;
        if (FAILED(client_->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&render_)))) return false;
        if (FAILED(client_->GetService(__uuidof(IAudioClock), reinterpret_cast<void**>(&clock_)))) return false;
        UINT64 freq = 0;
        if (FAILED(clock_->GetFrequency(&freq)) || freq == 0) return false;
        clockFreq_ = freq;
        bufferFrames_ = static_cast<int>(bufferFrames);

        // Exclusive event mode hands over one whole period per event. In
        // shared mode filling all free space would queue the full buffer;
        // capping the queue at two engine periods keeps one period of slack
        // against wake-up jitter without adding latency.
        if (exclusive_) {
            periodFrames_ = bufferFrames_;
            queueTarget_ = bufferFrames_;
        } else {
            queueTarget_ = std::min(bufferFrames_, 2 * periodFrames_);
        }

        direct_ = format_ == SampleFormat::Float32;
        scratch_.assign(direct_ ? 0 : static_cast<size_t>(bufferFrames_) * channels_, 0.0f);

        REFERENCE_TIME streamLatency = 0;
        if (FAILED(client_->GetStreamLatency(&streamLatency))) streamLatency = 0;

        DeviceInfo info;
        info.sampleRate = rate_;
        info.channels = channels_;
        info.bufferFrames = periodFrames_;
        info.latencyFrames = queueTarget_ + HnsToFrames(streamLatency, rate_);
        info.exclusive = exclusive_;
        info.backend = UNA_BACKEND_WASAPI;
        std::lock_guard<std::mutex> lk(infoMutex_);
        info_ = info;
        return true;
    }

    void ReleaseStream() {
        SafeRelease(clock_);
        SafeRelease(render_);
        SafeRelease(client_);
        SafeRelease(device_);
    }

    bool StartStream() {
        framesWritten_ = 0;
        // Prefill silence so the first engine pass does not underrun.
        BYTE* data = nullptr;
        const UINT32 prefill = static_cast<UINT32>(queueTarget_);
        if (FAILED(render_->GetBuffer(prefill, &data))) return false;
        if (FAILED(render_->ReleaseBuffer(prefill, AUDCLNT_BUFFERFLAGS_SILENT))) return false;
        framesWritten_ = prefill;
        return SUCCEEDED(client_->Start());
    }

    void StopStream() {
        client_->Stop();
        client_->Reset();
        framesWritten_ = 0;
    }

    // Host time at which frame `framesWritten_` (the first frame of the
    // buffer about to be filled) will be heard. `queued` = frames ahead of it.
    int64_t EstimatePresentNs(UINT32 queued) {
        const int64_t now = HostTimeNs();
        const int64_t fallback = now + static_cast<int64_t>(queued) * 1000000000LL / rate_;
        UINT64 pos = 0, qpcPos = 0;
        if (FAILED(clock_->GetPosition(&pos, &qpcPos)) || qpcPos == 0) return fallback;
        const double played = static_cast<double>(pos) * rate_ / static_cast<double>(clockFreq_);
        const double aheadNs = (static_cast<double>(framesWritten_) - played) * 1e9 / rate_;
        const int64_t present = static_cast<int64_t>(qpcPos) * 100 + static_cast<int64_t>(aheadNs);
        if (present < now || present > now + kMaxAheadNs) return fallback;
        return present;
    }

    // One event's worth of work. Returns false when the device is lost.
    bool RenderOnce() {
        UINT32 padding = 0;
        UINT32 frames;
        if (exclusive_) {
            frames = static_cast<UINT32>(bufferFrames_);
            padding = frames; // the other half of the double buffer is playing
        } else {
            if (FAILED(client_->GetCurrentPadding(&padding))) return false;
            const UINT32 target = static_cast<UINT32>(queueTarget_);
            frames = padding >= target ? 0 : std::min(static_cast<UINT32>(bufferFrames_) - padding, target - padding);
        }
        if (frames == 0) return true;

        const int64_t presentNs = EstimatePresentNs(padding);
        BYTE* data = nullptr;
        const HRESULT hr = render_->GetBuffer(frames, &data);
        if (hr == AUDCLNT_E_BUFFER_TOO_LARGE) return true;
        if (FAILED(hr)) return false;

        float* out = direct_ ? reinterpret_cast<float*>(data) : scratch_.data();
        cb_->OnRender(out, static_cast<int>(frames), channels_, presentNs);
        if (!direct_) ConvertSamples(out, data, static_cast<size_t>(frames) * channels_, format_);

        if (FAILED(render_->ReleaseBuffer(frames, 0))) return false;
        framesWritten_ += frames;
        return true;
    }

    // Caller side
    std::mutex apiMutex_;
    DeviceConfig cfg_{};
    DeviceCallback* cb_ = nullptr;
    std::thread thread_;

    // Control handshake
    std::mutex ctlMutex_;
    std::condition_variable ctlCv_;
    std::atomic<uint64_t> cmdSeq_{0};
    uint64_t ackSeq_ = 0;     // guarded by ctlMutex_
    int openResult_ = 0;      // guarded by ctlMutex_: 0 pending, 1 ok, -1 failed
    bool threadExited_ = false;
    std::atomic<bool> wantRun_{false};
    std::atomic<bool> quit_{false};
    std::atomic<bool> deviceChanged_{false};
    HANDLE wakeEvent_ = nullptr;
    HANDLE renderEvent_ = nullptr;

    mutable std::mutex infoMutex_;
    DeviceInfo info_{};

    // Render thread only
    NotificationClient notifier_;
    bool registered_ = false;
    IMMDeviceEnumerator* enumerator_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioClient* client_ = nullptr;
    IAudioRenderClient* render_ = nullptr;
    IAudioClock* clock_ = nullptr;
    UINT64 clockFreq_ = 1;
    SampleFormat format_ = SampleFormat::Unknown;
    bool exclusive_ = false;
    bool direct_ = true;
    int rate_ = 48000;
    int channels_ = 2;
    int bufferFrames_ = 0;
    int periodFrames_ = 0;
    int queueTarget_ = 0;
    uint64_t framesWritten_ = 0;
    std::vector<float> scratch_;
};

} // namespace

std::unique_ptr<AudioDevice> CreatePlatformDevice() { return std::make_unique<WasapiDevice>(); }

} // namespace una

#endif // _WIN32
