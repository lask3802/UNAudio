#pragma once
// Engine: control-thread API, audio-thread mixer, worker services.
//
// Thread ownership
//   control (any API caller)  : clip/voice tables under mutex_, pushes Cmds
//   audio (device callback)   : VoiceRT, buses, heap, mix buffers — lock-free
//   decode workers            : Clip decode jobs (DecodeService)
//   stream service            : Stream::Refill (StreamService)

#include "Clip.h"
#include "Clock.h"
#include "Stream.h"
#include "../Core/Platform.h"
#include "../Core/Queues.h"
#include "../Platform/AudioDevice.h"
#include "unaudio.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace una {

// ── Commands (control → audio) ──────────────────────────────────

enum class CmdType : uint8_t {
    Play, Start, Stop, Pause, Resume, Seek, Volume, Pan, Pitch, Loop,
    BusVolume, BusPause, MasterVolume, StopAll
};

struct Cmd {
    CmdType type;
    uint16_t slot;
    uint16_t gen;
    bool heapSlot; // holds a reservation in the timed-command heap (see Engine::heapReserved_)
    int64_t dsp;   // <= 0 immediate
    uint64_t seq;  // FIFO tiebreak for equal dsp
    union {
        struct {
            Clip* clip;
            Stream* stream;
            float volume, pan, pitch;
            int32_t fadeFrames;
            int32_t bus, loop, flags;
            int64_t startDsp, startFrame, loopStart, loopEnd;
        } play;
        struct { float value; int32_t rampFrames; int32_t bus; int32_t flag; } f;
        struct { int64_t frame; } seek;
        struct { int32_t loop; int64_t start, end; } loop;
    };
};

// ── Voice ───────────────────────────────────────────────────────

constexpr int kSegments = 16;

/// Timeline segments the audio thread publishes for lock-free timing reads.
/// source(dsp) = src + (dsp - dsp0) * rate on [dsp0, next segment).
struct VoiceTiming {
    std::atomic<uint32_t> seq{0};
    std::atomic<int32_t> count{0};
    std::atomic<int32_t> newest{0};
    std::atomic<int64_t> segDsp[kSegments];
    std::atomic<double> segSrc[kSegments];
    std::atomic<double> segRate[kSegments];
    std::atomic<int64_t> startDsp{0};

    void Reset(int64_t dsp, double src, double rate);
    void Append(int64_t dsp, double src, double rate);
    struct Copy {
        int count = 0;
        int newest = 0;
        int64_t dsp[kSegments];
        double src[kSegments];
        double rate[kSegments];
        int64_t startDsp = 0;
    };
    void Read(Copy& out) const;
};

struct Ramp {
    float value = 0.0f, target = 0.0f, inc = 0.0f;
    int32_t left = 0;
    void Set(float v) { value = target = v; inc = 0.0f; left = 0; }
    void To(float t, int frames) {
        if (frames <= 0) { Set(t); return; }
        target = t; left = frames; inc = (t - value) / static_cast<float>(frames);
    }
    float After(int frames) const { return frames >= left ? target : value + inc * static_cast<float>(frames); }
    void Advance(int frames) {
        if (left <= 0) return;
        if (frames >= left) { value = target; left = 0; inc = 0.0f; }
        else { value += inc * static_cast<float>(frames); left -= frames; }
    }
};

enum class VoicePhase : int32_t {
    Free = 0, Pending = 1, Scheduled = 2, Playing = 3, Paused = 4, Ended = 5
};

/// Audio-thread-owned voice runtime.
struct VoiceRT {
    bool active = false;
    uint16_t gen = 0;
    Clip* clip = nullptr;
    Stream* stream = nullptr;
    int channels = 0;
    int sampleFormat = 0;
    const void* pcm = nullptr;
    int64_t total = 0;
    int clipRate = 0;

    int64_t pos = 0;
    uint32_t frac = 0;
    uint64_t step = 0;           // 32.32 fixed source frames per output frame
    float pitch = 1.0f;

    bool loop = false;
    int64_t loopStart = 0, loopEnd = 0;
    int bus = 0;
    bool paused = false;         // user pause
    bool started = false;        // Start event fired
    bool waitPrime = false;      // immediate stream start waits for data
    bool stopAfterFade = false;
    bool catchUp = false;
    int64_t scheduledDsp = 0;

    float volume = 1.0f, pan = 0.0f;
    Ramp gain;                    // volume + fades (pan applied separately)
    Ramp panRamp;

    float prev[2] = {0.0f, 0.0f}; // frame at pos-1 (interpolation history)
    bool prevValid = false;
    bool starving = false;
};

struct VoiceSlot {
    // control thread (under Engine::mutex_)
    uint16_t gen = 0;
    bool allocated = false;
    Clip* clip = nullptr;
    std::shared_ptr<Stream> stream;
    // shared
    std::atomic<int32_t> phase{0};
    VoiceTiming timing;
    // audio thread
    VoiceRT rt;
};

struct BusRT {
    Ramp gain;
    bool paused = false;
};

// ── Services ────────────────────────────────────────────────────

class Engine;

class DecodeService {
public:
    void Start(Engine* engine, int threads);
    void Stop();
    void Submit(Clip* clip);   // caller added a ref for the job
    void Reprioritize();
    int Pending() const { return pending_.load(std::memory_order_relaxed); }

private:
    void Run();
    bool Step(Clip* clip);     // returns true when the job is finished

    Engine* engine_ = nullptr;
    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::pair<uint64_t, Clip*>> queue_;
    uint64_t seq_ = 0;
    bool running_ = false;
    std::atomic<int> pending_{0};
};

class StreamService {
public:
    void Start();
    void Stop();
    void Add(const std::shared_ptr<Stream>& s);
    void Remove(Stream* s);
    void Wake() { sem_.Signal(); }
    int Count();

private:
    void Run();

    std::thread thread_;
    std::mutex mutex_;
    std::vector<std::shared_ptr<Stream>> streams_;
    std::vector<std::shared_ptr<Stream>> scratch_;
    Semaphore sem_;
    std::atomic<bool> running_{false};
};

// ── Engine ──────────────────────────────────────────────────────

class Engine final : public DeviceCallback {
public:
    Engine();
    ~Engine() override;

    int Init(const una_config& cfg);
    void Shutdown();

    // control API (any non-audio thread)
    int GetDeviceInfo(una_device_info& out);
    void Update();
    int Render(float* out, int frames, int channels);   // MANUAL device
    int SetSuspended(bool suspended);
    bool IsSuspended() const { return suspended_.load(std::memory_order_relaxed); }
    int64_t NowNs() const;
    int GetClock(una_clock& out);
    double AudibleFrameAt(int64_t hostNs);

    int ClipLoad(const void* data, int64_t size, const char* path, const una_clip_params* p, una_clip* out);
    int ClipRelease(una_clip h);
    int ClipInfo(una_clip h, una_clip_info& out);
    int ClipSetPriority(una_clip h, int priority);

    int VoicePlay(una_clip h, const una_play_params* p, una_voice* out);
    int VoiceStop(una_voice v, int64_t at, float fade);
    int VoicePause(una_voice v, int64_t at, bool pause);
    int VoiceSeek(una_voice v, int64_t frame, int64_t at);
    int VoiceVolume(una_voice v, float vol, float ramp, int64_t at);
    int VoicePan(una_voice v, float pan, int64_t at);
    int VoicePitch(una_voice v, float pitch, int64_t at);
    int VoiceLoop(una_voice v, int loop, int64_t ls, int64_t le);
    int VoiceState(una_voice v);
    int VoiceTimingInfo(una_voice v, int64_t hostNs, una_voice_timing& out);
    int StopAll(float fade);

    int SetMasterVolume(float v, float ramp);
    float MasterVolume() const { return masterVolume_.load(std::memory_order_relaxed); }
    int BusVolume(int bus, float v, float ramp);
    int BusPause(int bus, bool paused, int64_t at);

    int PollEvents(una_event* out, int max);
    int Stats(una_stats& out);

    // worker → engine
    void PostWorkerEvent(int type, uint32_t handle, int64_t value);
    void AddClipMemory(int64_t bytes) { clipMemory_.fetch_add(bytes, std::memory_order_relaxed); }
    int SampleRate() const { return sampleRate_.load(std::memory_order_relaxed); }

    // DeviceCallback (audio thread)
    void OnRender(float* out, int frames, int channels, int64_t presentNs) override;
    void OnDeviceChanged(const DeviceInfo& info) override;

private:
    // control helpers (mutex_ held)
    Clip* LookupClip(una_clip h);
    VoiceSlot* LookupVoice(una_voice v);
    int PushCmd(Cmd& c);
    void ReclaimVoices();
    void CollectClips();
    void ReleaseClipRef(Clip* c);
    int FramesFromSeconds(float s) const;

    // audio helpers
    static constexpr int kMaxBlock = 2048;      // frames mixed per inner block
    static constexpr int kChunk = 256;          // voice kernel chunk
    static constexpr int kMaxStepInt = 8;       // max source frames per output frame
    static constexpr int kStagingFrames = kChunk * kMaxStepInt + 8;

    void ProcessBlock(float* out, int frames, int channels);
    void FlushEnded();
    void DrainCommands(int64_t blockStart, int64_t blockEnd);
    void ApplyCmd(const Cmd& c, int64_t dsp);
    void HeapPush(const Cmd& c);
    void ReleaseHeapSlot(const Cmd& c) {
        if (c.heapSlot) heapReserved_.fetch_sub(1, std::memory_order_relaxed);
    }
    void HeapPop();
    void MixRange(int offset, int frames, int64_t dsp);
    void RenderVoice(VoiceSlot& slot, int slotIndex, float* out, int frames, int64_t dsp);
    void FetchDecoded(const VoiceRT& v, int64_t start, int count, float* dst, int64_t end);
    double EffectiveRate(const VoiceRT& v) const;
    /// A fade-out only advances while the voice is rendered; otherwise stop at once.
    bool CanFade(const VoiceRT& v) const { return v.started && !v.paused && !buses_[v.bus].paused; }
    void StartVoice(VoiceSlot& slot, int slotIndex, int64_t dsp, int64_t lateFrames);
    void EndVoice(VoiceSlot& slot, int slotIndex, int64_t dsp);
    void UpdateStep(VoiceRT& v);
    double SourcePos(const VoiceRT& v) const;
    void PushAudioEvent(int type, uint32_t handle, int64_t dsp, int64_t value);
    uint32_t VoiceHandle(int slot, uint16_t gen) const;

    // config / device
    una_config cfg_{};
    std::unique_ptr<AudioDevice> device_;
    DeviceInfo devInfo_{};
    std::atomic<int> sampleRate_{48000};
    int channels_ = 2;
    bool manual_ = false;
    std::atomic<bool> initialized_{false};

    // control state
    std::mutex lifeMutex_;    // serialises Init / Shutdown / suspend / manual Render; taken before mutex_
    std::atomic<bool> suspended_{false};
    std::mutex mutex_;
    std::mutex devMutex_;     // devInfo_ (device thread may update it)
    std::mutex pollMutex_;    // single consumer for audioEvents_
    struct ClipSlot { Clip* clip = nullptr; uint16_t gen = 0; };
    std::vector<ClipSlot> clips_;
    // FIFO: reuse the least recently freed slot so 16-bit generations wrap as late as possible.
    std::deque<int> freeClipSlots_;
    std::vector<Clip*> releasedClips_;
    std::vector<VoiceSlot> voices_;   // fixed size, never reallocated after Init
    std::deque<int> freeVoiceSlots_;
    uint16_t sessionGenBase_ = 0; // slot generations start here so handles differ across sessions
    uint64_t cmdSeq_ = 0;

    std::unique_ptr<MpscQueue<Cmd>> cmdQueue_;

    // audio state
    std::vector<int> activeVoices_;
    std::vector<int> endedScratch_;
    bool anyEnded_ = false;
    std::vector<Cmd> heap_;
    size_t heapSize_ = 0;
    std::vector<float> mix_;          // stereo interleaved
    std::vector<float> staging_;      // interleaved source frames
    BusRT buses_[UNA_MAX_BUSES];
    Ramp master_;
    std::atomic<int64_t> dspFrame_{0};
    int64_t audioDsp_ = 0;            // audio-thread copy
    int64_t fallbackLatencyNs_ = 0;

    std::atomic<float> masterVolume_{1.0f};
    std::atomic<float> busVolume_[UNA_MAX_BUSES];

    // events
    std::unique_ptr<SpscQueue<una_event>> audioEvents_;
    std::mutex workerEventMutex_;
    std::vector<una_event> workerEvents_;

    // timing
    Clock clock_;
    std::atomic<int64_t> manualNowFrames_{0};

    // services
    DecodeService decoder_;
    StreamService streamer_;

    // stats
    std::atomic<int64_t> starvedFrames_{0};
    std::atomic<int64_t> droppedCommands_{0};
    // Timed commands reserve a heap entry when queued (control thread) and give it back
    // when applied (audio thread), so a full heap is reported as UNA_E_QUEUE_FULL
    // instead of silently applying scheduled commands early.
    std::atomic<int32_t> heapReserved_{0};
    std::atomic<int64_t> lateCommands_{0};
    std::atomic<int64_t> droppedEvents_{0};
    std::atomic<int32_t> activeCount_{0};
    std::atomic<float> cpuLoad_{0.0f};
    std::atomic<float> cpuPeak_{0.0f};
    std::atomic<float> peakL_{0.0f}, peakR_{0.0f};
    std::atomic<int64_t> clipMemory_{0};
    std::atomic<int32_t> liveClips_{0};
    std::atomic<int64_t> framesRendered_{0};

    friend class DecodeService;
};

Engine* GlobalEngine();

} // namespace una
