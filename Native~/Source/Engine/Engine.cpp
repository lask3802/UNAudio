// Engine — control side (API entry points; never runs on the audio thread).

#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace una {

namespace {

constexpr int kDefaultVoices = 128;
constexpr int kMaxVoicesCap = 4096;
constexpr int kDefaultClips = 1024;
constexpr int kMaxClipsCap = 65000;
constexpr size_t kCommandCapacity = 8192;
constexpr size_t kHeapCapacity = 8192;
constexpr size_t kEventCapacity = 8192;

uint16_t NextGen(uint16_t g) {
    ++g;
    return g == 0 ? 1 : g;
}

double EvalSegments(const VoiceTiming::Copy& t, double frame, int64_t* segDsp, double* segSrc, double* segRate) {
    if (t.count <= 0) {
        if (segDsp) *segDsp = 0;
        if (segSrc) *segSrc = 0.0;
        if (segRate) *segRate = 0.0;
        return 0.0;
    }
    int pick = (t.newest - (t.count - 1) + kSegments) % kSegments; // oldest: extrapolate backwards
    for (int k = 0; k < t.count; ++k) {
        const int idx = (t.newest - k + kSegments) % kSegments;
        if (static_cast<double>(t.dsp[idx]) <= frame) {
            pick = idx;
            break;
        }
    }
    if (segDsp) *segDsp = t.dsp[pick];
    if (segSrc) *segSrc = t.src[pick];
    if (segRate) *segRate = t.rate[pick];
    return t.src[pick] + (frame - static_cast<double>(t.dsp[pick])) * t.rate[pick];
}

} // namespace

// ── VoiceTiming ────────────────────────────────────────────────

void VoiceTiming::Reset(int64_t dsp, double src, double rate) {
    const uint32_t s = seq.load(std::memory_order_relaxed);
    seq.store(s + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    segDsp[0].store(dsp, std::memory_order_relaxed);
    segSrc[0].store(src, std::memory_order_relaxed);
    segRate[0].store(rate, std::memory_order_relaxed);
    newest.store(0, std::memory_order_relaxed);
    count.store(1, std::memory_order_relaxed);
    startDsp.store(dsp, std::memory_order_relaxed);
    seq.store(s + 2, std::memory_order_release);
}

void VoiceTiming::Append(int64_t dsp, double src, double rate) {
    const uint32_t s = seq.load(std::memory_order_relaxed);
    seq.store(s + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    int n = newest.load(std::memory_order_relaxed);
    int c = count.load(std::memory_order_relaxed);
    if (c > 0 && segDsp[n].load(std::memory_order_relaxed) == dsp) {
        // Same instant: replace instead of growing history.
    } else {
        n = (n + 1) % kSegments;
        if (c < kSegments) ++c;
    }
    segDsp[n].store(dsp, std::memory_order_relaxed);
    segSrc[n].store(src, std::memory_order_relaxed);
    segRate[n].store(rate, std::memory_order_relaxed);
    newest.store(n, std::memory_order_relaxed);
    count.store(c, std::memory_order_relaxed);
    seq.store(s + 2, std::memory_order_release);
}

void VoiceTiming::Read(Copy& out) const {
    for (;;) {
        const uint32_t s0 = seq.load(std::memory_order_acquire);
        if (s0 & 1u) { CpuRelax(); continue; }
        out.count = count.load(std::memory_order_relaxed);
        out.newest = newest.load(std::memory_order_relaxed);
        for (int i = 0; i < kSegments; ++i) {
            out.dsp[i] = segDsp[i].load(std::memory_order_relaxed);
            out.src[i] = segSrc[i].load(std::memory_order_relaxed);
            out.rate[i] = segRate[i].load(std::memory_order_relaxed);
        }
        out.startDsp = startDsp.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq.load(std::memory_order_relaxed) == s0) return;
    }
}

// ── Lifecycle ──────────────────────────────────────────────────

Engine::Engine() {
    for (auto& b : busVolume_) b.store(1.0f, std::memory_order_relaxed);
}

Engine::~Engine() { Shutdown(); }

int Engine::Init(const una_config& cfgIn) {
    std::lock_guard<std::mutex> life(lifeMutex_);
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_.load()) return UNA_E_ALREADY_INITIALIZED;
    if (cfgIn.struct_size != static_cast<int32_t>(sizeof(una_config))) return UNA_E_ABI_MISMATCH;

    cfg_ = cfgIn;
    const int maxVoices = std::min(kMaxVoicesCap, cfg_.max_voices > 0 ? cfg_.max_voices : kDefaultVoices);
    const int maxClips = std::min(kMaxClipsCap, cfg_.max_clips > 0 ? cfg_.max_clips : kDefaultClips);
    int threads = cfg_.decode_threads;
    if (threads <= 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        threads = hw >= 6 ? 2 : 1;
    }
    threads = std::min(threads, 8);

    manual_ = cfg_.device_type == UNA_DEVICE_MANUAL;
    if (manual_) {
        devInfo_ = DeviceInfo{};
        devInfo_.sampleRate = cfg_.sample_rate > 0 ? cfg_.sample_rate : 48000;
        devInfo_.channels = cfg_.channels > 0 ? cfg_.channels : 2;
        devInfo_.bufferFrames = cfg_.buffer_frames > 0 ? cfg_.buffer_frames : 256;
        devInfo_.latencyFrames = 0;
        devInfo_.backend = UNA_BACKEND_MANUAL;
    } else {
        device_ = cfg_.device_type == UNA_DEVICE_NULL ? CreateNullDevice() : CreatePlatformDevice();
        if (!device_) return UNA_E_DEVICE;
        DeviceConfig dc;
        dc.sampleRate = cfg_.sample_rate;
        dc.bufferFrames = cfg_.buffer_frames;
        dc.channels = cfg_.channels;
        dc.exclusive = cfg_.exclusive != 0;
        if (!device_->Open(dc, this)) {
            device_.reset();
            return UNA_E_DEVICE;
        }
        devInfo_ = device_->Info();
    }
    sampleRate_.store(devInfo_.sampleRate, std::memory_order_relaxed);
    channels_ = devInfo_.channels;
    fallbackLatencyNs_ = devInfo_.sampleRate > 0
                             ? static_cast<int64_t>(devInfo_.latencyFrames) * 1000000000LL / devInfo_.sampleRate
                             : 0;

    // A handle kept from a previous session must not alias a new voice or clip in the same
    // slot, so each session starts its generations at a different base.
    sessionGenBase_ = static_cast<uint16_t>(sessionGenBase_ + 0x1000);
    std::vector<VoiceSlot>(static_cast<size_t>(maxVoices)).swap(voices_);
    for (auto& s : voices_) s.gen = sessionGenBase_;
    freeVoiceSlots_.clear();
    for (int i = 0; i < maxVoices; ++i) freeVoiceSlots_.push_back(i);
    clips_.assign(static_cast<size_t>(maxClips), ClipSlot{nullptr, sessionGenBase_});
    freeClipSlots_.clear();
    for (int i = 0; i < maxClips; ++i) freeClipSlots_.push_back(i);

    cmdQueue_ = std::make_unique<MpscQueue<Cmd>>(kCommandCapacity);
    {
        // A PollEvents from another thread may still be draining the old queues.
        std::lock_guard<std::mutex> pl(pollMutex_);
        std::lock_guard<std::mutex> wl(workerEventMutex_);
        audioEvents_ = std::make_unique<SpscQueue<una_event>>(kEventCapacity);
        workerEvents_.clear();
    }
    heap_.assign(kHeapCapacity, Cmd{});
    heapSize_ = 0;
    heapReserved_.store(0, std::memory_order_relaxed);
    activeVoices_.clear();
    activeVoices_.reserve(static_cast<size_t>(maxVoices));
    endedScratch_.clear();
    endedScratch_.reserve(static_cast<size_t>(maxVoices));
    mix_.assign(static_cast<size_t>(kMaxBlock) * 2, 0.0f);
    staging_.assign(static_cast<size_t>(kStagingFrames) * 2, 0.0f);
    // Master and bus volumes are mix settings, not session state: they survive a
    // restart (device switch) and may be set before the first init.
    for (int i = 0; i < UNA_MAX_BUSES; ++i) {
        buses_[i].gain.Set(busVolume_[i].load(std::memory_order_relaxed));
        buses_[i].paused = false;
    }
    master_.Set(masterVolume_.load(std::memory_order_relaxed));
    // Every session starts with fresh statistics.
    starvedFrames_.store(0, std::memory_order_relaxed);
    droppedCommands_.store(0, std::memory_order_relaxed);
    lateCommands_.store(0, std::memory_order_relaxed);
    droppedEvents_.store(0, std::memory_order_relaxed);
    activeCount_.store(0, std::memory_order_relaxed);
    cpuLoad_.store(0.0f, std::memory_order_relaxed);
    cpuPeak_.store(0.0f, std::memory_order_relaxed);
    peakL_.store(0.0f, std::memory_order_relaxed);
    peakR_.store(0.0f, std::memory_order_relaxed);
    audioDsp_ = 0;
    dspFrame_.store(0, std::memory_order_relaxed);
    manualNowFrames_.store(0, std::memory_order_relaxed);
    framesRendered_.store(0, std::memory_order_relaxed);
    clock_.Reset(devInfo_.sampleRate, devInfo_.bufferFrames);

    decoder_.Start(this, threads);
    streamer_.Start();

    if (device_ && !device_->Start()) {
        decoder_.Stop();
        streamer_.Stop();
        device_->Close();
        device_.reset();
        return UNA_E_DEVICE;
    }
    suspended_.store(false, std::memory_order_relaxed);
    initialized_.store(true, std::memory_order_release);
    return UNA_OK;
}

void Engine::Shutdown() {
    // Held throughout so an Init cannot slip in between the two locked sections and a
    // manual-device Render cannot be mixing while clips are deleted.
    std::lock_guard<std::mutex> life(lifeMutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_.exchange(false)) return;
    }
    // No lock while stopping the device: its thread may call OnDeviceChanged.
    if (device_) {
        device_->Stop();
        device_->Close();
    }
    decoder_.Stop();
    streamer_.Stop();

    std::lock_guard<std::mutex> lock(mutex_);
    device_.reset();
    for (auto& s : voices_) {
        s.stream.reset();
        s.clip = nullptr;
        s.allocated = false;
        s.rt = VoiceRT{};
        s.phase.store(static_cast<int32_t>(VoicePhase::Free), std::memory_order_relaxed);
    }
    for (auto& c : clips_) {
        delete c.clip;
        c.clip = nullptr;
    }
    for (Clip* c : releasedClips_) delete c;
    releasedClips_.clear();
    activeVoices_.clear();
    heapSize_ = 0;
    if (cmdQueue_) { Cmd c; while (cmdQueue_->TryPop(c)) {} }
    {
        std::lock_guard<std::mutex> pl(pollMutex_);   // single consumer of audioEvents_
        std::lock_guard<std::mutex> wl(workerEventMutex_);
        if (audioEvents_) { una_event e; while (audioEvents_->TryPop(e)) {} }
        workerEvents_.clear();
    }
    clipMemory_.store(0, std::memory_order_relaxed);
    liveClips_.store(0, std::memory_order_relaxed);
    activeCount_.store(0, std::memory_order_relaxed);
    suspended_.store(false, std::memory_order_relaxed);
}

int Engine::GetDeviceInfo(una_device_info& out) {
    // mutex_ keeps device_ alive: Shutdown clears initialized_ and resets device_ under it.
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_.load(std::memory_order_acquire)) return UNA_E_NOT_INITIALIZED;
    // Backends refine their latency once the stream reports timestamps (AAudio, CoreAudio), so
    // the copy from Init or the last device change goes stale. Read it before taking devMutex_:
    // a device thread may hold its own info lock while it reports a change.
    DeviceInfo live;
    if (device_) live = device_->Info();
    std::lock_guard<std::mutex> dl(devMutex_);
    std::memset(&out, 0, sizeof(out));
    out.sample_rate = devInfo_.sampleRate;
    out.channels = devInfo_.channels;
    out.buffer_frames = devInfo_.bufferFrames;
    // Between a stream reopen and its OnDeviceChanged the live figure counts frames of the new rate.
    out.latency_frames = device_ && live.sampleRate == devInfo_.sampleRate ? live.latencyFrames : devInfo_.latencyFrames;
    out.exclusive = devInfo_.exclusive ? 1 : 0;
    out.backend = devInfo_.backend;
    return UNA_OK;
}

void Engine::Update() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_.load(std::memory_order_relaxed)) return;
    ReclaimVoices();
    CollectClips();
}

int64_t Engine::NowNs() const {
    if (manual_) {
        const int sr = sampleRate_.load(std::memory_order_relaxed);
        const int64_t f = manualNowFrames_.load(std::memory_order_acquire);
        return sr > 0 ? (f / sr) * 1000000000LL + (f % sr) * 1000000000LL / sr : 0;
    }
    return HostTimeNs();
}

int Engine::GetClock(una_clock& out) {
    if (!initialized_.load(std::memory_order_acquire)) return UNA_E_NOT_INITIALIZED;
    const int64_t now = NowNs();
    const ClockSnapshot s = clock_.Read();
    std::memset(&out, 0, sizeof(out));
    out.host_ns = now;
    out.mix_frame = s.mixFrame;
    out.audible_frame = clock_.AudibleNow(now);
    out.frames_per_second = s.nsPerFrame > 0.0 ? 1e9 / s.nsPerFrame : 0.0;
    out.sample_rate = s.sampleRate;
    out.buffer_frames = s.bufferFrames;
    out.anchor_frame = s.anchorFrame;
    out.anchor_host_ns = s.anchorNs;
    out.callbacks = s.callbacks;
    out.discontinuities = s.discontinuities;
    return UNA_OK;
}

double Engine::AudibleFrameAt(int64_t hostNs) {
    const ClockSnapshot s = clock_.Read();
    return Clock::AudibleAt(s, hostNs);
}

int Engine::FramesFromSeconds(float s) const {
    if (!(s > 0.0f)) return 0;
    const double f = static_cast<double>(s) * sampleRate_.load(std::memory_order_relaxed);
    // Round: float seconds such as 0.02f are slightly below the exact frame count.
    return static_cast<int>(std::min(std::floor(f + 0.5), 1e9));
}

// ── Clips ──────────────────────────────────────────────────────

Clip* Engine::LookupClip(una_clip h) {
    const uint32_t idx = (h & 0xFFFFu);
    const uint16_t gen = static_cast<uint16_t>(h >> 16);
    if (idx == 0 || idx > clips_.size()) return nullptr;
    ClipSlot& s = clips_[idx - 1];
    if (!s.clip || s.gen != gen) return nullptr;
    return s.clip;
}

int Engine::ClipLoad(const void* data, int64_t size, const char* path, const una_clip_params* p, una_clip* out) {
    if (!out) return UNA_E_INVALID_ARG;
    *out = 0;
    una_clip_params params{};
    params.struct_size = sizeof(una_clip_params);
    if (p) {
        if (p->struct_size != static_cast<int32_t>(sizeof(una_clip_params))) return UNA_E_ABI_MISMATCH;
        params = *p;
    }
    if (params.mode != UNA_LOAD_DECODE && params.mode != UNA_LOAD_STREAM) return UNA_E_INVALID_ARG;
    if (params.sample_format != UNA_SAMPLE_INT16 && params.sample_format != UNA_SAMPLE_FLOAT32)
        return UNA_E_INVALID_ARG;
    if (!path && (!data || size <= 0)) return UNA_E_INVALID_ARG;
    if (!initialized_.load(std::memory_order_acquire)) return UNA_E_NOT_INITIALIZED;

    auto clip = std::make_unique<Clip>();
    clip->mode = params.mode;
    clip->sampleFormat = params.sample_format;
    clip->flags = params.flags;
    clip->priority.store(params.priority, std::memory_order_relaxed);
    clip->targetRate = (params.flags & UNA_CLIP_FLAG_NO_RESAMPLE) ? 0 : sampleRate_.load(std::memory_order_relaxed);
    if (path) {
        clip->path = path;
    } else if (params.flags & UNA_CLIP_FLAG_BORROW) {
        clip->borrowed = true;
        clip->src = static_cast<const uint8_t*>(data);
        clip->srcSize = static_cast<size_t>(size);
    } else {
        // Copy outside the lock: a multi-megabyte memcpy must not stall other API calls.
        try {
            clip->owned.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
        } catch (...) {
            return UNA_E_OUT_OF_MEMORY;
        }
        clip->src = clip->owned.data();
        clip->srcSize = clip->owned.size();
        clip->memoryBytes.store(size, std::memory_order_relaxed);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_.load(std::memory_order_relaxed)) return UNA_E_NOT_INITIALIZED;
    if (freeClipSlots_.empty()) CollectClips();
    if (freeClipSlots_.empty()) return UNA_E_TOO_MANY_CLIPS;
    const int slot = freeClipSlots_.front();
    freeClipSlots_.pop_front();
    ClipSlot& cs = clips_[static_cast<size_t>(slot)];
    cs.gen = NextGen(cs.gen);
    clip->handle = static_cast<uint32_t>(slot + 1) | (static_cast<uint32_t>(cs.gen) << 16);
    clip->refs.store(2, std::memory_order_relaxed); // owner + decode job
    if (!clip->owned.empty()) AddClipMemory(static_cast<int64_t>(clip->owned.size()));
    cs.clip = clip.release();
    liveClips_.fetch_add(1, std::memory_order_relaxed);
    *out = cs.clip->handle;
    decoder_.Submit(cs.clip);
    return UNA_OK;
}

int Engine::ClipRelease(una_clip h) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_.load(std::memory_order_relaxed)) return UNA_E_NOT_INITIALIZED;
    Clip* c = LookupClip(h);
    if (!c) return UNA_E_INVALID_HANDLE;
    clips_[(h & 0xFFFFu) - 1].clip = nullptr;
    freeClipSlots_.push_back(static_cast<int>((h & 0xFFFFu) - 1));
    c->released.store(true, std::memory_order_release);
    ReleaseClipRef(c);
    releasedClips_.push_back(c);
    CollectClips();
    return UNA_OK;
}

void Engine::ReleaseClipRef(Clip* c) { c->refs.fetch_sub(1, std::memory_order_acq_rel); }

void Engine::CollectClips() {
    for (size_t i = 0; i < releasedClips_.size();) {
        Clip* c = releasedClips_[i];
        if (c->refs.load(std::memory_order_acquire) == 0) {
            AddClipMemory(-c->memoryBytes.load(std::memory_order_relaxed));
            PostWorkerEvent(UNA_EVENT_CLIP_FREED, c->handle, 0);
            delete c;
            liveClips_.fetch_sub(1, std::memory_order_relaxed);
            releasedClips_[i] = releasedClips_.back();
            releasedClips_.pop_back();
        } else {
            ++i;
        }
    }
}

int Engine::ClipInfo(una_clip h, una_clip_info& out) {
    std::memset(&out, 0, sizeof(out));
    out.state = UNA_CLIP_INVALID;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_.load(std::memory_order_relaxed)) return UNA_E_NOT_INITIALIZED;
    Clip* c = LookupClip(h);
    if (!c) return UNA_E_INVALID_HANDLE;
    const int st = c->state.load(std::memory_order_acquire);
    out.state = st;
    out.mode = c->mode;
    out.sample_format = c->sampleFormat;
    out.memory_bytes = c->memoryBytes.load(std::memory_order_relaxed);
    if (st == UNA_CLIP_FAILED) {
        out.error = c->error.load(std::memory_order_relaxed);
        return UNA_OK;
    }
    if (st >= UNA_CLIP_READY) {
        out.channels = c->channels;
        out.source_channels = c->sourceChannels;
        out.source_rate = c->sourceRate;
        out.stored_rate = c->storedRate;
        out.total_frames = c->totalFrames;
        out.decoded_frames = c->mode == UNA_LOAD_STREAM ? c->totalFrames
                                                        : c->decodedFrames.load(std::memory_order_acquire);
        out.length_seconds = c->storedRate > 0 ? static_cast<double>(c->totalFrames) / c->storedRate : 0.0;
    }
    return UNA_OK;
}

int Engine::ClipSetPriority(una_clip h, int priority) {
    std::lock_guard<std::mutex> lock(mutex_);
    Clip* c = LookupClip(h);
    if (!c) return UNA_E_INVALID_HANDLE;
    c->priority.store(priority, std::memory_order_relaxed);
    decoder_.Reprioritize();
    return UNA_OK;
}

// ── Voices ─────────────────────────────────────────────────────

VoiceSlot* Engine::LookupVoice(una_voice v) {
    const uint32_t idx = v & 0xFFFFu;
    const uint16_t gen = static_cast<uint16_t>(v >> 16);
    if (idx == 0 || idx > voices_.size()) return nullptr;
    VoiceSlot& s = voices_[idx - 1];
    if (!s.allocated || s.gen != gen) return nullptr;
    return &s;
}

uint32_t Engine::VoiceHandle(int slot, uint16_t gen) const {
    return static_cast<uint32_t>(slot + 1) | (static_cast<uint32_t>(gen) << 16);
}

int Engine::PushCmd(Cmd& c) {
    c.seq = ++cmdSeq_;
    // Anything with a target frame may sit in the heap (a Play schedules its Start).
    c.heapSlot = c.type == CmdType::Play ? c.play.startDsp > 0 : c.dsp > 0;
    if (c.heapSlot && heapReserved_.fetch_add(1, std::memory_order_relaxed) >= static_cast<int32_t>(kHeapCapacity)) {
        heapReserved_.fetch_sub(1, std::memory_order_relaxed);
        droppedCommands_.fetch_add(1, std::memory_order_relaxed);
        return UNA_E_QUEUE_FULL;
    }
    if (!cmdQueue_->TryPush(c)) {
        if (c.heapSlot) heapReserved_.fetch_sub(1, std::memory_order_relaxed);
        droppedCommands_.fetch_add(1, std::memory_order_relaxed);
        return UNA_E_QUEUE_FULL;
    }
    return UNA_OK;
}

void Engine::ReclaimVoices() {
    for (size_t i = 0; i < voices_.size(); ++i) {
        VoiceSlot& s = voices_[i];
        if (!s.allocated) continue;
        if (s.phase.load(std::memory_order_acquire) != static_cast<int32_t>(VoicePhase::Ended)) continue;
        if (s.stream) {
            s.stream->dead.store(true, std::memory_order_release);
            streamer_.Remove(s.stream.get());
            s.stream.reset();
        }
        if (s.clip) ReleaseClipRef(s.clip);
        s.clip = nullptr;
        s.allocated = false;
        s.phase.store(static_cast<int32_t>(VoicePhase::Free), std::memory_order_release);
        freeVoiceSlots_.push_back(static_cast<int>(i));
    }
}

int Engine::VoicePlay(una_clip h, const una_play_params* p, una_voice* out) {
    if (!out) return UNA_E_INVALID_ARG;
    *out = 0;
    una_play_params params{};
    params.struct_size = sizeof(una_play_params);
    params.volume = 1.0f;
    params.pitch = 1.0f;
    if (p) {
        if (p->struct_size != static_cast<int32_t>(sizeof(una_play_params))) return UNA_E_ABI_MISMATCH;
        params = *p;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_.load(std::memory_order_relaxed)) return UNA_E_NOT_INITIALIZED;
    Clip* clip = LookupClip(h);
    if (!clip) return UNA_E_INVALID_HANDLE;
    const int st = clip->state.load(std::memory_order_acquire);
    if (st == UNA_CLIP_FAILED) return clip->error.load(std::memory_order_relaxed);
    if (st < UNA_CLIP_READY) return UNA_E_NOT_READY;

    if (freeVoiceSlots_.empty()) ReclaimVoices();
    if (freeVoiceSlots_.empty()) return UNA_E_NO_VOICE;
    const int slotIndex = freeVoiceSlots_.front();
    VoiceSlot& slot = voices_[static_cast<size_t>(slotIndex)];

    const int64_t total = clip->totalFrames;
    int64_t startFrame = std::max<int64_t>(0, std::min(params.start_frame, total));
    int64_t ls = params.loop_start, le = params.loop_end;
    if (le <= 0 || le > total) le = total;
    if (ls < 0 || ls >= le) ls = 0;
    const float pitch = std::isfinite(params.pitch) && params.pitch > 0.0f ? params.pitch : 1.0f;
    const float volume = std::isfinite(params.volume) ? std::max(0.0f, params.volume) : 1.0f;
    const float pan = std::isfinite(params.pan) ? std::max(-1.0f, std::min(1.0f, params.pan)) : 0.0f;
    const int bus = std::max(0, std::min(UNA_MAX_BUSES - 1, params.bus));

    std::shared_ptr<Stream> stream;
    if (clip->mode == UNA_LOAD_STREAM) {
        stream = std::make_shared<Stream>(clip, clip->storedRate, startFrame, params.loop != 0, ls, le);
    }

    Cmd c{};
    c.type = CmdType::Play;
    c.slot = static_cast<uint16_t>(slotIndex);
    c.gen = NextGen(slot.gen);
    c.dsp = 0;
    c.play.clip = clip;
    c.play.stream = stream.get();
    c.play.volume = volume;
    c.play.pan = pan;
    c.play.pitch = pitch;
    c.play.fadeFrames = FramesFromSeconds(params.fade_in_seconds);
    c.play.bus = bus;
    c.play.loop = params.loop != 0;
    c.play.flags = params.flags;
    c.play.startDsp = params.start_dsp_frame;
    c.play.startFrame = startFrame;
    c.play.loopStart = ls;
    c.play.loopEnd = le;

    // Predicted timeline so timing queries work before the mixer sees the command.
    const int sr = sampleRate_.load(std::memory_order_relaxed);
    // Same clamp as UpdateStep on the audio thread.
    const double rate = (params.flags & UNA_PLAY_PAUSED)
                            ? 0.0
                            : std::min(static_cast<double>(kMaxStepInt) - 0.001,
                                       std::max(1.0 / 256.0, static_cast<double>(pitch) * clip->storedRate /
                                                                 (sr > 0 ? sr : clip->storedRate)));
    const int64_t head = dspFrame_.load(std::memory_order_acquire);
    slot.timing.Reset(std::max(params.start_dsp_frame, head), static_cast<double>(startFrame), rate);

    slot.gen = c.gen;
    slot.allocated = true;
    slot.clip = clip;
    slot.phase.store(static_cast<int32_t>(VoicePhase::Pending), std::memory_order_release);
    clip->refs.fetch_add(1, std::memory_order_acq_rel);
    slot.stream = stream;
    if (stream) streamer_.Add(stream);

    const int rc = PushCmd(c);
    if (rc != UNA_OK) {
        if (stream) {
            stream->dead.store(true);
            streamer_.Remove(stream.get());
        }
        slot.stream.reset();
        ReleaseClipRef(clip);
        slot.clip = nullptr;
        slot.allocated = false;
        slot.phase.store(static_cast<int32_t>(VoicePhase::Free), std::memory_order_release);
        return rc;
    }
    freeVoiceSlots_.pop_front();
    *out = VoiceHandle(slotIndex, c.gen);
    return UNA_OK;
}

#define UNA_VOICE_CMD_PROLOGUE(v)                                                    \
    std::lock_guard<std::mutex> lock(mutex_);                                        \
    if (!initialized_.load(std::memory_order_relaxed)) return UNA_E_NOT_INITIALIZED; \
    VoiceSlot* slot = LookupVoice(v);                                                \
    if (!slot) return UNA_E_INVALID_HANDLE;                                          \
    if (slot->phase.load(std::memory_order_acquire) == static_cast<int32_t>(VoicePhase::Ended)) return UNA_OK; \
    Cmd c{};                                                                         \
    c.slot = static_cast<uint16_t>((v & 0xFFFFu) - 1);                               \
    c.gen = slot->gen;

int Engine::VoiceStop(una_voice v, int64_t at, float fade) {
    UNA_VOICE_CMD_PROLOGUE(v)
    c.type = CmdType::Stop;
    c.dsp = at;
    c.f.rampFrames = FramesFromSeconds(fade);
    return PushCmd(c);
}

int Engine::VoicePause(una_voice v, int64_t at, bool pause) {
    UNA_VOICE_CMD_PROLOGUE(v)
    c.type = pause ? CmdType::Pause : CmdType::Resume;
    c.dsp = at;
    return PushCmd(c);
}

int Engine::VoiceSeek(una_voice v, int64_t frame, int64_t at) {
    UNA_VOICE_CMD_PROLOGUE(v)
    c.type = CmdType::Seek;
    c.dsp = at;
    c.seek.frame = std::max<int64_t>(0, frame);
    return PushCmd(c);
}

int Engine::VoiceVolume(una_voice v, float vol, float ramp, int64_t at) {
    if (!std::isfinite(vol)) return UNA_E_INVALID_ARG;
    UNA_VOICE_CMD_PROLOGUE(v)
    c.type = CmdType::Volume;
    c.dsp = at;
    c.f.value = std::max(0.0f, vol);
    c.f.rampFrames = FramesFromSeconds(ramp);
    return PushCmd(c);
}

int Engine::VoicePan(una_voice v, float pan, int64_t at) {
    if (!std::isfinite(pan)) return UNA_E_INVALID_ARG;
    UNA_VOICE_CMD_PROLOGUE(v)
    c.type = CmdType::Pan;
    c.dsp = at;
    c.f.value = std::max(-1.0f, std::min(1.0f, pan));
    return PushCmd(c);
}

int Engine::VoicePitch(una_voice v, float pitch, int64_t at) {
    if (!std::isfinite(pitch) || pitch <= 0.0f) return UNA_E_INVALID_ARG;
    UNA_VOICE_CMD_PROLOGUE(v)
    c.type = CmdType::Pitch;
    c.dsp = at;
    c.f.value = pitch;
    return PushCmd(c);
}

int Engine::VoiceLoop(una_voice v, int loop, int64_t ls, int64_t le) {
    UNA_VOICE_CMD_PROLOGUE(v)
    const int64_t total = slot->clip ? slot->clip->totalFrames : 0;
    if (le <= 0 || le > total) le = total;
    if (ls < 0 || ls >= le) ls = 0;
    c.type = CmdType::Loop;
    c.dsp = 0;
    c.loop.loop = loop != 0;
    c.loop.start = ls;
    c.loop.end = le;
    return PushCmd(c);
}

#undef UNA_VOICE_CMD_PROLOGUE

int Engine::VoiceState(una_voice v) {
    std::lock_guard<std::mutex> lock(mutex_);
    VoiceSlot* slot = LookupVoice(v);
    if (!slot) return UNA_VOICE_INVALID;
    return slot->phase.load(std::memory_order_acquire);
}

int Engine::VoiceTimingInfo(una_voice v, int64_t hostNs, una_voice_timing& out) {
    std::memset(&out, 0, sizeof(out));
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_.load(std::memory_order_relaxed)) return UNA_E_NOT_INITIALIZED;
    VoiceSlot* slot = LookupVoice(v);
    if (!slot) return UNA_E_INVALID_HANDLE;

    VoiceTiming::Copy t;
    slot->timing.Read(t);
    const int64_t now = hostNs > 0 ? hostNs : NowNs();
    const ClockSnapshot snap = clock_.Read();
    double audible;
    if (hostNs > 0) {
        audible = Clock::AudibleAt(snap, hostNs);
        if (audible > static_cast<double>(snap.mixFrame)) audible = static_cast<double>(snap.mixFrame);
    } else {
        audible = clock_.AudibleNow(now);
    }

    out.state = slot->phase.load(std::memory_order_acquire);
    out.stored_rate = slot->clip ? slot->clip->storedRate : 0;
    out.host_ns = now;
    out.start_dsp_frame = t.startDsp;
    out.audible_frame = EvalSegments(t, audible, nullptr, nullptr, nullptr);
    out.mix_frame = EvalSegments(t, static_cast<double>(snap.mixFrame), &out.seg_dsp_frame,
                                 &out.seg_source_frame, &out.seg_rate);
    out.audible_seconds = out.stored_rate > 0 ? out.audible_frame / out.stored_rate : 0.0;
    return UNA_OK;
}

int Engine::StopAll(float fade) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_.load(std::memory_order_relaxed)) return UNA_E_NOT_INITIALIZED;
    Cmd c{};
    c.type = CmdType::StopAll;
    c.f.rampFrames = FramesFromSeconds(fade);
    return PushCmd(c);
}

// ── Mix controls ───────────────────────────────────────────────

int Engine::SetMasterVolume(float v, float ramp) {
    if (!std::isfinite(v)) return UNA_E_INVALID_ARG;
    v = std::max(0.0f, v);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_.load(std::memory_order_relaxed)) {
        masterVolume_.store(v, std::memory_order_relaxed);  // applied at init
        return UNA_OK;
    }
    Cmd c{};
    c.type = CmdType::MasterVolume;
    c.f.value = v;
    c.f.rampFrames = FramesFromSeconds(ramp);
    const int rc = PushCmd(c);
    if (rc == UNA_OK) masterVolume_.store(v, std::memory_order_relaxed);  // only what the mixer will apply
    return rc;
}

int Engine::BusVolume(int bus, float v, float ramp) {
    if (bus < 0 || bus >= UNA_MAX_BUSES || !std::isfinite(v)) return UNA_E_INVALID_ARG;
    v = std::max(0.0f, v);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_.load(std::memory_order_relaxed)) {
        busVolume_[bus].store(v, std::memory_order_relaxed);  // applied at init
        return UNA_OK;
    }
    Cmd c{};
    c.type = CmdType::BusVolume;
    c.f.bus = bus;
    c.f.value = v;
    c.f.rampFrames = FramesFromSeconds(ramp);
    const int rc = PushCmd(c);
    if (rc == UNA_OK) busVolume_[bus].store(v, std::memory_order_relaxed);
    return rc;
}

int Engine::BusPause(int bus, bool paused, int64_t at) {
    if (bus < 0 || bus >= UNA_MAX_BUSES) return UNA_E_INVALID_ARG;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_.load(std::memory_order_relaxed)) return UNA_E_NOT_INITIALIZED;
    Cmd c{};
    c.type = CmdType::BusPause;
    c.dsp = at;
    c.f.bus = bus;
    c.f.flag = paused ? 1 : 0;
    return PushCmd(c);
}

// ── Events / stats ─────────────────────────────────────────────

void Engine::PostWorkerEvent(int type, uint32_t handle, int64_t value) {
    std::lock_guard<std::mutex> lock(workerEventMutex_);
    if (workerEvents_.size() >= kEventCapacity) return;
    una_event e{};
    e.type = type;
    e.handle = handle;
    e.value = value;
    workerEvents_.push_back(e);
}

int Engine::PollEvents(una_event* out, int max) {
    if (!out || max <= 0) return 0;
    std::lock_guard<std::mutex> lock(pollMutex_);
    if (!initialized_.load(std::memory_order_acquire)) return 0;
    int n = 0;
    {
        // Worker events first: CLIP_READY should precede voices started on that clip.
        std::lock_guard<std::mutex> wl(workerEventMutex_);
        const int take = std::min<int>(max, static_cast<int>(workerEvents_.size()));
        for (int i = 0; i < take; ++i) out[n++] = workerEvents_[static_cast<size_t>(i)];
        workerEvents_.erase(workerEvents_.begin(), workerEvents_.begin() + take);
    }
    while (n < max && audioEvents_ && audioEvents_->TryPop(out[n])) ++n;
    return n;
}

int Engine::Stats(una_stats& out) {
    std::memset(&out, 0, sizeof(out));
    if (!initialized_.load(std::memory_order_acquire)) return UNA_E_NOT_INITIALIZED;
    const ClockSnapshot s = clock_.Read();
    out.callbacks = s.callbacks;
    out.frames_rendered = framesRendered_.load(std::memory_order_relaxed);
    out.starved_frames = starvedFrames_.load(std::memory_order_relaxed);
    out.dropped_commands = droppedCommands_.load(std::memory_order_relaxed);
    out.late_commands = lateCommands_.load(std::memory_order_relaxed);
    out.active_voices = activeCount_.load(std::memory_order_relaxed);
    out.live_clips = liveClips_.load(std::memory_order_relaxed);
    out.decode_jobs = decoder_.Pending();
    out.streams = streamer_.Count();
    out.cpu_load = cpuLoad_.load(std::memory_order_relaxed);
    out.cpu_load_peak = cpuPeak_.exchange(0.0f, std::memory_order_relaxed);
    out.peak_left = peakL_.exchange(0.0f, std::memory_order_relaxed);
    out.peak_right = peakR_.exchange(0.0f, std::memory_order_relaxed);
    out.clip_memory_bytes = clipMemory_.load(std::memory_order_relaxed);
    return UNA_OK;
}

// ── Manual rendering ───────────────────────────────────────────

int Engine::SetSuspended(bool suspended) {
    std::lock_guard<std::mutex> life(lifeMutex_);
    if (!initialized_.load(std::memory_order_acquire)) return UNA_E_NOT_INITIALIZED;
    if (suspended_.load(std::memory_order_relaxed) == suspended) return UNA_OK;
    // No mutex_ here, as in Shutdown: stopping waits for the device thread.
    if (device_) {
        if (suspended) device_->Stop();
        else if (!device_->Start()) return UNA_E_DEVICE;
    }
    suspended_.store(suspended, std::memory_order_relaxed);
    return UNA_OK;
}

int Engine::Render(float* out, int frames, int channels) {
    // Manual rendering is not real-time, so it may lock; this keeps Shutdown/Init out.
    std::lock_guard<std::mutex> life(lifeMutex_);
    if (!initialized_.load(std::memory_order_acquire)) return UNA_E_NOT_INITIALIZED;
    if (!manual_) return UNA_E_DEVICE;
    if (!out || frames <= 0 || channels != channels_) return UNA_E_INVALID_ARG;
    if (suspended_.load(std::memory_order_relaxed)) {
        // Like a stopped device: silence, and the timeline does not move.
        std::memset(out, 0, static_cast<size_t>(frames) * channels * sizeof(float));
        return UNA_OK;
    }
    const int sr = sampleRate_.load(std::memory_order_relaxed);
    const int64_t first = manualNowFrames_.load(std::memory_order_relaxed);
    const int64_t presentNs = (first / sr) * 1000000000LL + (first % sr) * 1000000000LL / sr;
    // presentNs of 0 means "unknown" to the clock; nudge the very first buffer.
    OnRender(out, frames, channels, presentNs > 0 ? presentNs : 1);
    manualNowFrames_.store(first + frames, std::memory_order_release);
    return UNA_OK;
}

// ── Global instance ────────────────────────────────────────────

Engine* GlobalEngine() {
    // Intentionally leaked: destroying it during DLL unload would join audio
    // threads under the loader lock. Hosts call una_shutdown() explicitly.
    static Engine* engine = new Engine();
    return engine;
}

} // namespace una
