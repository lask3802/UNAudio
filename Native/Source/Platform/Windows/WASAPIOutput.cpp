#include "../AudioOutput.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#endif

// ── System format query (WASAPI Core Audio) ──────────────────────

static bool QuerySystemAudioFormat(int32_t& outSampleRate, int32_t& outChannels,
                                   int32_t& outBitsPerSample)
{
    // Use COM to query the Windows default audio endpoint's mix format.
    // This is the format that the Windows audio engine actually runs at.
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comOwned = SUCCEEDED(hrInit); // S_OK or S_FALSE (already initialized)

    bool result = false;

    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));

    if (SUCCEEDED(hr) && enumerator) {
        IMMDevice* device = nullptr;
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);

        if (SUCCEEDED(hr) && device) {
            IAudioClient* audioClient = nullptr;
            hr = device->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(&audioClient));

            if (SUCCEEDED(hr) && audioClient) {
                WAVEFORMATEX* mixFormat = nullptr;
                hr = audioClient->GetMixFormat(&mixFormat);

                if (SUCCEEDED(hr) && mixFormat) {
                    outSampleRate = static_cast<int32_t>(mixFormat->nSamplesPerSec);
                    outChannels = static_cast<int32_t>(mixFormat->nChannels);
                    outBitsPerSample = static_cast<int32_t>(mixFormat->wBitsPerSample);

                    // For WAVEFORMATEXTENSIBLE, check the actual sub-format
                    if (mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                        mixFormat->cbSize >= 22) {
                        auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mixFormat);
                        outBitsPerSample = static_cast<int32_t>(ext->Samples.wValidBitsPerSample);
                    }

                    result = true;
                    CoTaskMemFree(mixFormat);
                }
                audioClient->Release();
            }
            device->Release();
        }
        enumerator->Release();
    }

    if (comOwned && hrInit == S_OK) {
        CoUninitialize();
    }

    return result;
}

// ── Exported C API: query system audio format ────────────────────

extern "C" {
UNAUDIO_EXPORT UNAudioSystemFormat UNAudio_GetSystemAudioFormat(void) {
    UNAudioSystemFormat fmt{};
    if (QuerySystemAudioFormat(fmt.sampleRate, fmt.channels, fmt.bitsPerSample)) {
        fmt.isValid = 1;
    }
    return fmt;
}
} // extern "C"

// ── WinMM waveOut output backend ─────────────────────────────────

/// Windows standalone output backend.
///
/// Note:
/// - This is a functional WinMM `waveOut` implementation that immediately
///   drives the engine callback for Windows standalone builds.
/// - The class name is kept as `WASAPIOutput` to avoid broader refactors.
/// - A true WASAPI path can replace this implementation later.
class WASAPIOutput : public AudioOutput {
public:
    WASAPIOutput();
    ~WASAPIOutput() override;

    bool Initialize(const UNAudioOutputConfig& config,
                    AudioRenderCallback callback,
                    void* userData) override;
    bool Start() override;
    void Stop() override;
    int32_t GetActualSampleRate() const override;
    int32_t GetActualBufferSize() const override;
    float   GetLatencyMs() const override;

private:
    enum class SampleFormat {
        Float32,
        Int16
    };

    static constexpr int32_t kMinBufferCount = 2;
    static constexpr int32_t kDefaultBufferCount = 4;

    bool openDevice();
    void closeDevice();
    bool allocateBuffers();
    void releaseBuffers();
    void renderAndQueueBuffer(size_t index);
    void renderLoop();

    UNAudioOutputConfig config_{};
    AudioRenderCallback callback_ = nullptr;
    void* userData_ = nullptr;

    HWAVEOUT waveOut_ = nullptr;
    std::atomic<bool> running_{false};
    std::thread renderThread_;
    bool prepared_ = false;
    bool timerPeriodSet_ = false;

    int32_t actualSampleRate_ = 0;
    int32_t actualBufferSize_ = 0;
    int32_t actualChannels_ = 0;
    int32_t bufferCount_ = 0;

    SampleFormat sampleFormat_ = SampleFormat::Float32;
    int32_t bytesPerSample_ = 4;

    std::vector<std::vector<uint8_t>> buffers_;
    std::vector<WAVEHDR> headers_;
    std::vector<float> mixScratch_;
};

WASAPIOutput::WASAPIOutput() = default;
WASAPIOutput::~WASAPIOutput() { Stop(); }

bool WASAPIOutput::Initialize(const UNAudioOutputConfig& config,
                              AudioRenderCallback callback,
                              void* userData) {
    Stop();

    config_ = config;
    callback_ = callback;
    userData_ = userData;

    if (!callback_) return false;

    if (config_.sampleRate <= 0) config_.sampleRate = 48000;
    if (config_.channels <= 0) config_.channels = 2;
    if (config_.bufferSize <= 0) config_.bufferSize = 512;
    if (config_.bufferCount < kMinBufferCount) config_.bufferCount = kDefaultBufferCount;

    actualSampleRate_ = config_.sampleRate;
    actualChannels_ = config_.channels;
    actualBufferSize_ = config_.bufferSize;
    bufferCount_ = std::max(config_.bufferCount, kMinBufferCount);

    if (!openDevice()) {
        closeDevice();
        return false;
    }

    if (!allocateBuffers()) {
        closeDevice();
        return false;
    }

    return true;
}

bool WASAPIOutput::openDevice() {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = static_cast<WORD>(actualChannels_);
    format.nSamplesPerSec = static_cast<DWORD>(actualSampleRate_);
    format.wBitsPerSample = 32;
    format.nBlockAlign = static_cast<WORD>(format.nChannels * (format.wBitsPerSample / 8));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    MMRESULT mm = waveOutOpen(&waveOut_, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (mm == MMSYSERR_NOERROR) {
        sampleFormat_ = SampleFormat::Float32;
        bytesPerSample_ = 4;
        return true;
    }

    // Fallback for older drivers/devices that reject float32.
    format = {};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(actualChannels_);
    format.nSamplesPerSec = static_cast<DWORD>(actualSampleRate_);
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(format.nChannels * (format.wBitsPerSample / 8));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    mm = waveOutOpen(&waveOut_, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (mm != MMSYSERR_NOERROR) {
        waveOut_ = nullptr;
        return false;
    }

    sampleFormat_ = SampleFormat::Int16;
    bytesPerSample_ = 2;
    return true;
}

void WASAPIOutput::closeDevice() {
    if (waveOut_) {
        waveOutClose(waveOut_);
        waveOut_ = nullptr;
    }
}

bool WASAPIOutput::allocateBuffers() {
    if (!waveOut_) return false;

    const int64_t samplesPerBuffer = static_cast<int64_t>(actualBufferSize_) * actualChannels_;
    const int64_t bytesPerBuffer = samplesPerBuffer * bytesPerSample_;
    if (samplesPerBuffer <= 0 || bytesPerBuffer <= 0) return false;

    buffers_.assign(bufferCount_, std::vector<uint8_t>(static_cast<size_t>(bytesPerBuffer), 0));
    headers_.assign(bufferCount_, WAVEHDR{});

    for (int32_t i = 0; i < bufferCount_; ++i) {
        WAVEHDR& hdr = headers_[i];
        hdr.lpData = reinterpret_cast<LPSTR>(buffers_[i].data());
        hdr.dwBufferLength = static_cast<DWORD>(buffers_[i].size());
        hdr.dwFlags = 0;
        hdr.dwLoops = 0;

        MMRESULT mm = waveOutPrepareHeader(waveOut_, &hdr, sizeof(WAVEHDR));
        if (mm != MMSYSERR_NOERROR) {
            // Unprepare those that succeeded before failing.
            for (int32_t j = 0; j < i; ++j) {
                waveOutUnprepareHeader(waveOut_, &headers_[j], sizeof(WAVEHDR));
            }
            headers_.clear();
            buffers_.clear();
            return false;
        }
    }

    prepared_ = true;
    return true;
}

void WASAPIOutput::releaseBuffers() {
    if (!prepared_) {
        headers_.clear();
        buffers_.clear();
        mixScratch_.clear();
        return;
    }

    if (waveOut_) {
        for (WAVEHDR& hdr : headers_) {
            waveOutUnprepareHeader(waveOut_, &hdr, sizeof(WAVEHDR));
        }
    }

    prepared_ = false;
    headers_.clear();
    buffers_.clear();
    mixScratch_.clear();
}

void WASAPIOutput::renderAndQueueBuffer(size_t index) {
    if (!waveOut_ || !callback_ || index >= buffers_.size() || index >= headers_.size()) return;

    const int32_t sampleCount = actualBufferSize_ * actualChannels_;
    if (sampleCount <= 0) return;

    if (sampleFormat_ == SampleFormat::Float32) {
        float* out = reinterpret_cast<float*>(buffers_[index].data());
        callback_(out, actualBufferSize_, actualChannels_, userData_);
    } else {
        if (mixScratch_.size() != static_cast<size_t>(sampleCount)) {
            mixScratch_.assign(static_cast<size_t>(sampleCount), 0.0f);
        }
        callback_(mixScratch_.data(), actualBufferSize_, actualChannels_, userData_);

        int16_t* dst = reinterpret_cast<int16_t*>(buffers_[index].data());
        for (int32_t i = 0; i < sampleCount; ++i) {
            float s = std::max(-1.0f, std::min(1.0f, mixScratch_[i]));
            dst[i] = static_cast<int16_t>(s * 32767.0f);
        }
    }

    WAVEHDR& hdr = headers_[index];
    MMRESULT mm = waveOutWrite(waveOut_, &hdr, sizeof(WAVEHDR));
    if (mm != MMSYSERR_NOERROR) {
        // Keep the header state untouched on failure so the render loop can retry.
        return;
    }
}

void WASAPIOutput::renderLoop() {
    // Prefer higher scheduling priority to reduce render underruns.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    while (running_.load(std::memory_order_acquire)) {
        bool submitted = false;
        for (size_t i = 0; i < headers_.size(); ++i) {
            // Re-queue any header that is currently not owned by waveOut.
            // Using !WHDR_INQUEUE is more robust than WHDR_DONE-only checks:
            // if a write call fails/races, we still retry this slot.
            if ((headers_[i].dwFlags & WHDR_INQUEUE) == 0) {
                renderAndQueueBuffer(i);
                submitted = true;
            }
        }

        if (!submitted) {
            // Yield to avoid busy-wait; timeBeginPeriod(1) ensures ~1ms resolution.
            Sleep(1);
        }
    }
}

bool WASAPIOutput::Start() {
    if (!waveOut_ || !prepared_ || !callback_) return false;
    if (running_.load(std::memory_order_acquire)) return true;

    // Request 1ms timer resolution for reliable Sleep(1) in the render loop.
    if (timeBeginPeriod(1) == TIMERR_NOERROR) {
        timerPeriodSet_ = true;
    }

    running_.store(true, std::memory_order_release);

    // Prime output queue.
    for (size_t i = 0; i < headers_.size(); ++i) {
        renderAndQueueBuffer(i);
    }

    renderThread_ = std::thread([this]() { renderLoop(); });
    return true;
}

void WASAPIOutput::Stop() {
    running_.store(false, std::memory_order_release);
    if (renderThread_.joinable()) {
        renderThread_.join();
    }

    if (waveOut_) {
        waveOutReset(waveOut_);
    }

    releaseBuffers();
    closeDevice();

    // Restore default timer resolution.
    if (timerPeriodSet_) {
        timeEndPeriod(1);
        timerPeriodSet_ = false;
    }
}

int32_t WASAPIOutput::GetActualSampleRate() const {
    return actualSampleRate_ > 0 ? actualSampleRate_ : config_.sampleRate;
}

int32_t WASAPIOutput::GetActualBufferSize() const {
    return actualBufferSize_ > 0 ? actualBufferSize_ : config_.bufferSize;
}

float WASAPIOutput::GetLatencyMs() const {
    const int32_t sr = GetActualSampleRate();
    if (sr <= 0) return 0.0f;
    const int32_t frames = std::max(GetActualBufferSize(), 0);
    const int32_t buffers = std::max(bufferCount_, 1);
    return static_cast<float>(frames * buffers) / sr * 1000.0f;
}

std::unique_ptr<AudioOutput> CreatePlatformAudioOutput() {
    return std::make_unique<WASAPIOutput>();
}

#endif // _WIN32
