// Engine — audio thread. Nothing here locks, allocates or decodes.
//
// Each device callback is split into blocks of at most kMaxBlock frames.
// Commands drained at a block start either apply immediately or go into a
// min-heap keyed by dsp frame; the block is then mixed in sub-ranges cut at
// every due event, so a scheduled play/stop/seek/volume lands on its exact
// frame regardless of buffer size.

#include "Engine.h"
#include "MixKernels.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace una {

namespace {

constexpr uint64_t kOne = 1ull << 32;
constexpr double kInvOne = 1.0 / 4294967296.0;

inline float PanL(float p) { return p > 0.0f ? 1.0f - p : 1.0f; }
inline float PanR(float p) { return p < 0.0f ? 1.0f + p : 1.0f; }

inline bool CmdLess(const Cmd& a, const Cmd& b) {
    return a.dsp < b.dsp || (a.dsp == b.dsp && a.seq < b.seq);
}

} // namespace

// ── Callback ───────────────────────────────────────────────────

void Engine::OnRender(float* out, int frames, int channels, int64_t presentNs) {
    // Every callback: backends may switch threads, and thread_local can allocate
    // on first touch (Darwin TLV). Setting MXCSR/FPCR costs a few cycles.
    EnableFlushToZero();
    const int64_t t0 = HostTimeNs();
    const int sr = sampleRate_.load(std::memory_order_relaxed);
    clock_.OnCallback(audioDsp_, frames, presentNs, manual_ ? presentNs : t0, fallbackLatencyNs_);

    int done = 0;
    while (done < frames) {
        const int n = std::min(kMaxBlock, frames - done);
        ProcessBlock(out + static_cast<size_t>(done) * channels, n, channels);
        done += n;
    }

    clock_.PublishMixFrame(audioDsp_);
    dspFrame_.store(audioDsp_, std::memory_order_release);
    framesRendered_.fetch_add(frames, std::memory_order_relaxed);
    activeCount_.store(static_cast<int32_t>(activeVoices_.size()), std::memory_order_relaxed);

    if (sr > 0 && frames > 0) {
        const double budget = static_cast<double>(frames) * 1e9 / sr;
        const float load = static_cast<float>(static_cast<double>(HostTimeNs() - t0) / budget);
        cpuLoad_.store(load, std::memory_order_relaxed);
        if (load > cpuPeak_.load(std::memory_order_relaxed)) cpuPeak_.store(load, std::memory_order_relaxed);
    }
}

void Engine::ProcessBlock(float* out, int n, int channels) {
    const int64_t start = audioDsp_;
    const int64_t end = start + n;
    DrainCommands(start, end);
    std::memset(mix_.data(), 0, static_cast<size_t>(n) * 2 * sizeof(float));

    int offset = 0;
    for (;;) {
        while (heapSize_ > 0 && heap_[0].dsp <= start + offset) {
            const Cmd c = heap_[0];
            HeapPop();
            ReleaseHeapSlot(c);
            ApplyCmd(c, start + offset);
        }
        FlushEnded();
        int stop = n;
        if (heapSize_ > 0 && heap_[0].dsp < end) stop = static_cast<int>(heap_[0].dsp - start);
        if (stop > offset) {
            MixRange(offset, stop - offset, start + offset);
            offset = stop;
        }
        if (offset >= n) break;
    }

    // Master gain, clip and metering; stereo devices are written directly, other
    // layouts are processed in place and mapped afterwards.
    float* m = mix_.data();
    float pkL = 0.0f, pkR = 0.0f;
    mix::Master(m, channels == 2 ? out : m, n, master_.value, master_.inc, master_.left, master_.target, pkL, pkR);
    if (channels != 2) {
        for (int i = 0; i < n; ++i) {
            const float l = m[2 * i], r = m[2 * i + 1];
            float* o = out + static_cast<size_t>(i) * channels;
            if (channels == 1) {
                o[0] = 0.5f * (l + r);
            } else {
                o[0] = l;
                o[1] = r;
                for (int c = 2; c < channels; ++c) o[c] = 0.0f;
            }
        }
    }
    master_.Advance(n);
    if (pkL > peakL_.load(std::memory_order_relaxed)) peakL_.store(pkL, std::memory_order_relaxed);
    if (pkR > peakR_.load(std::memory_order_relaxed)) peakR_.store(pkR, std::memory_order_relaxed);
    audioDsp_ = end;
}

// ── Commands ───────────────────────────────────────────────────

void Engine::DrainCommands(int64_t blockStart, int64_t /*blockEnd*/) {
    Cmd c;
    while (cmdQueue_->TryPop(c)) {
        if (c.type == CmdType::Play) {
            ApplyCmd(c, blockStart);  // schedules its own Start event (and keeps or releases the slot)
        } else if (c.dsp > blockStart) {
            HeapPush(c);
        } else {
            if (c.dsp > 0 && c.dsp < blockStart) lateCommands_.fetch_add(1, std::memory_order_relaxed);
            ReleaseHeapSlot(c);
            ApplyCmd(c, blockStart);
        }
    }
    FlushEnded();
}

void Engine::HeapPush(const Cmd& c) {
    if (heapSize_ >= heap_.size()) {
        // Unreachable while PushCmd reserves entries; never drop, apply now (early).
        lateCommands_.fetch_add(1, std::memory_order_relaxed);
        ReleaseHeapSlot(c);
        ApplyCmd(c, audioDsp_);
        return;
    }
    size_t i = heapSize_++;
    heap_[i] = c;
    while (i > 0) {
        const size_t parent = (i - 1) / 2;
        if (!CmdLess(heap_[i], heap_[parent])) break;
        std::swap(heap_[i], heap_[parent]);
        i = parent;
    }
}

void Engine::HeapPop() {
    if (heapSize_ == 0) return;
    heap_[0] = heap_[--heapSize_];
    size_t i = 0;
    for (;;) {
        const size_t l = 2 * i + 1, r = l + 1;
        size_t best = i;
        if (l < heapSize_ && CmdLess(heap_[l], heap_[best])) best = l;
        if (r < heapSize_ && CmdLess(heap_[r], heap_[best])) best = r;
        if (best == i) break;
        std::swap(heap_[i], heap_[best]);
        i = best;
    }
}

void Engine::ApplyCmd(const Cmd& c, int64_t dsp) {
    VoiceSlot* s = c.slot < voices_.size() ? &voices_[c.slot] : nullptr;
    const bool voiceCmd = c.type <= CmdType::Loop;
    if (voiceCmd && !s) {
        // Only a Play still holds its reservation here (others released it before
        // ApplyCmd). Unreachable: control code never sends an out-of-range slot.
        if (c.type == CmdType::Play) ReleaseHeapSlot(c);
        return;
    }
    const bool live = voiceCmd && s->rt.active && s->rt.gen == c.gen;

    switch (c.type) {
        case CmdType::Play: {
            VoiceRT& v = s->rt;
            if (v.active) EndVoice(*s, c.slot, dsp);  // defensive: slot reuse race
            FlushEnded();
            const Clip* clip = c.play.clip;
            v = VoiceRT{};
            v.active = true;
            v.gen = c.gen;
            v.clip = c.play.clip;
            v.stream = c.play.stream;
            v.channels = clip->channels;
            v.sampleFormat = clip->sampleFormat;
            v.pcm = clip->pcm;
            v.total = clip->totalFrames;
            v.clipRate = clip->storedRate;
            v.pos = c.play.startFrame;
            v.pitch = c.play.pitch;
            UpdateStep(v);
            v.loop = c.play.loop != 0;
            v.loopStart = c.play.loopStart;
            v.loopEnd = c.play.loopEnd;
            v.bus = c.play.bus;
            v.paused = (c.play.flags & UNA_PLAY_PAUSED) != 0;
            v.catchUp = (c.play.flags & UNA_PLAY_CATCH_UP) != 0;
            v.volume = c.play.volume;
            v.pan = c.play.pan;
            v.panRamp.Set(v.pan);
            if (c.play.fadeFrames > 0) {
                v.gain.Set(0.0f);
                v.gain.To(v.volume, c.play.fadeFrames);
            } else {
                v.gain.Set(v.volume);
            }
            v.scheduledDsp = c.play.startDsp;
            v.waitPrime = v.stream != nullptr && c.play.startDsp <= 0;
            activeVoices_.push_back(c.slot);
            s->phase.store(static_cast<int32_t>(VoicePhase::Scheduled), std::memory_order_release);

            const double nominal = v.paused ? 0.0 : static_cast<double>(v.step) * kInvOne;
            const int64_t start = c.play.startDsp;
            if (start > dsp) {
                s->timing.Reset(start, static_cast<double>(v.pos), nominal);
                Cmd st{};
                st.type = CmdType::Start;
                st.slot = c.slot;
                st.gen = c.gen;
                st.heapSlot = c.heapSlot;  // the Play's reservation moves to its Start
                st.dsp = start;
                st.seq = c.seq;
                HeapPush(st);
            } else if (v.waitPrime && (!v.stream->Primed() || v.stream->Failed())) {
                // Still waiting, or failed before producing anything: MixRange reports it.
                ReleaseHeapSlot(c);
                s->timing.Reset(dsp, static_cast<double>(v.pos), nominal);
            } else {
                ReleaseHeapSlot(c);
                if (start > 0 && start < dsp) lateCommands_.fetch_add(1, std::memory_order_relaxed);
                StartVoice(*s, c.slot, dsp, start > 0 ? dsp - start : 0);
            }
            break;
        }
        case CmdType::Start:
            if (live && !s->rt.started) StartVoice(*s, c.slot, dsp, dsp - c.dsp);
            break;
        case CmdType::Stop:
            if (!live) break;
            if (CanFade(s->rt) && c.f.rampFrames > 0) {
                s->rt.gain.To(0.0f, c.f.rampFrames);
                s->rt.stopAfterFade = true;
            } else {
                EndVoice(*s, c.slot, dsp);
            }
            break;
        case CmdType::Pause:
            if (!live || s->rt.paused) break;
            if (s->rt.stopAfterFade) {  // a frozen fade-out would hold the slot until resume
                EndVoice(*s, c.slot, dsp);
                break;
            }
            s->rt.paused = true;
            if (s->rt.started) {
                s->phase.store(static_cast<int32_t>(VoicePhase::Paused), std::memory_order_release);
                s->timing.Append(dsp, SourcePos(s->rt), 0.0);
            }
            break;
        case CmdType::Resume:
            if (!live || !s->rt.paused) break;
            s->rt.paused = false;
            if (s->rt.started) {
                s->phase.store(static_cast<int32_t>(VoicePhase::Playing), std::memory_order_release);
                s->timing.Append(dsp, SourcePos(s->rt), EffectiveRate(s->rt));
            } else {
                s->timing.Reset(s->timing.startDsp.load(std::memory_order_relaxed),
                                SourcePos(s->rt), static_cast<double>(s->rt.step) * kInvOne);
            }
            break;
        case CmdType::Seek: {
            if (!live) break;
            VoiceRT& v = s->rt;
            v.pos = std::max<int64_t>(0, std::min(c.seek.frame, v.total));
            v.frac = 0;
            v.prevValid = false;
            if (v.stream) {
                v.stream->RequestSeek(v.pos);
                streamer_.Wake();
            }
            if (v.started) {
                s->timing.Append(dsp, static_cast<double>(v.pos), EffectiveRate(v));
            } else {
                s->timing.Reset(s->timing.startDsp.load(std::memory_order_relaxed), static_cast<double>(v.pos),
                                v.paused ? 0.0 : static_cast<double>(v.step) * kInvOne);
            }
            break;
        }
        case CmdType::Volume:
            if (!live || s->rt.stopAfterFade) break;
            s->rt.volume = c.f.value;
            s->rt.gain.To(c.f.value, c.f.rampFrames);
            break;
        case CmdType::Pan:
            if (!live) break;
            s->rt.pan = c.f.value;
            s->rt.panRamp.To(c.f.value, 64);  // de-zipper
            break;
        case CmdType::Pitch:
            if (!live) break;
            s->rt.pitch = c.f.value;
            UpdateStep(s->rt);
            if (s->rt.started) s->timing.Append(dsp, SourcePos(s->rt), EffectiveRate(s->rt));
            break;
        case CmdType::Loop: {
            if (!live) break;
            VoiceRT& v = s->rt;
            const bool loop = c.loop.loop != 0;
            const bool changed = v.loop != loop || v.loopStart != c.loop.start || v.loopEnd != c.loop.end;
            v.loop = loop;
            v.loopStart = c.loop.start;
            v.loopEnd = c.loop.end;
            // Frames already in the ring may follow the old loop; RenderVoice resyncs the
            // stream only if the voice actually reaches such a frame.
            if (v.stream && changed) v.stream->SetLoop(loop, c.loop.start, c.loop.end);
            break;
        }
        case CmdType::BusVolume:
            buses_[c.f.bus].gain.To(c.f.value, c.f.rampFrames);
            break;
        case CmdType::BusPause: {
            BusRT& b = buses_[c.f.bus];
            const bool paused = c.f.flag != 0;
            if (b.paused == paused) break;
            b.paused = paused;
            for (int idx : activeVoices_) {
                VoiceSlot& vs = voices_[static_cast<size_t>(idx)];
                if (!vs.rt.active || vs.rt.bus != c.f.bus) continue;
                if (paused && vs.rt.stopAfterFade) {
                    EndVoice(vs, idx, dsp);  // see Pause
                    continue;
                }
                if (vs.rt.started && !vs.rt.paused)
                    vs.timing.Append(dsp, SourcePos(vs.rt), EffectiveRate(vs.rt));
            }
            break;
        }
        case CmdType::MasterVolume:
            master_.To(c.f.value, c.f.rampFrames);
            break;
        case CmdType::StopAll:
            for (int idx : activeVoices_) {
                VoiceSlot& vs = voices_[static_cast<size_t>(idx)];
                if (!vs.rt.active) continue;
                if (CanFade(vs.rt) && c.f.rampFrames > 0) {
                    vs.rt.gain.To(0.0f, c.f.rampFrames);
                    vs.rt.stopAfterFade = true;
                } else {
                    EndVoice(vs, idx, dsp);
                }
            }
            break;
    }
}

// ── Voice lifecycle ────────────────────────────────────────────

void Engine::UpdateStep(VoiceRT& v) {
    const int sr = sampleRate_.load(std::memory_order_relaxed);
    double rate = static_cast<double>(v.pitch) * v.clipRate / (sr > 0 ? sr : v.clipRate);
    rate = std::min(static_cast<double>(kMaxStepInt) - 0.001, std::max(1.0 / 256.0, rate));
    uint64_t step = static_cast<uint64_t>(std::llround(rate * static_cast<double>(kOne)));
    v.step = step == 0 ? 1 : step;
}

double Engine::SourcePos(const VoiceRT& v) const {
    return static_cast<double>(v.pos) + static_cast<double>(v.frac) * kInvOne;
}

double Engine::EffectiveRate(const VoiceRT& v) const {
    if (!v.started || v.paused || buses_[v.bus].paused) return 0.0;
    return static_cast<double>(v.step) * kInvOne;
}

void Engine::StartVoice(VoiceSlot& s, int slotIndex, int64_t dsp, int64_t lateFrames) {
    VoiceRT& v = s.rt;
    v.started = true;
    v.waitPrime = false;
    int64_t timelineStart = dsp;
    if (lateFrames > 0 && v.catchUp) {
        // Advance as if the voice had started on time. step < 8 << 32, so the
        // fixed-point product only fits below 2^28 frames; beyond that use doubles.
        int64_t skip;
        if (lateFrames < (int64_t(1) << 28)) {
            const uint64_t adv = static_cast<uint64_t>(v.frac) + static_cast<uint64_t>(lateFrames) * v.step;
            skip = static_cast<int64_t>(adv >> 32);
            v.frac = static_cast<uint32_t>(adv);
        } else {
            const double adv = static_cast<double>(v.frac) * kInvOne +
                               static_cast<double>(lateFrames) * static_cast<double>(v.step) * kInvOne;
            skip = static_cast<int64_t>(std::min(adv, 9.0e18));
            v.frac = static_cast<uint32_t>((adv - std::floor(adv)) * 4294967296.0);
        }
        const bool loopValid = v.loop && v.loopStart < v.loopEnd;
        int64_t pos;
        bool wrapped = false;
        if (loopValid && v.pos <= v.loopEnd && skip >= v.loopEnd - v.pos) {
            // Land where a voice looping since the scheduled frame would be now.
            pos = v.loopStart + (skip - (v.loopEnd - v.pos)) % (v.loopEnd - v.loopStart);
            wrapped = true;
        } else {
            pos = skip >= v.total - v.pos ? v.total : v.pos + skip;
        }
        v.pos = pos;
        v.prevValid = false;
        if (v.stream) {
            // Small skips come straight out of the ring; anything the ring cannot cover
            // (or that wrapped) restarts the producer at the new position.
            if (!wrapped && skip < static_cast<int64_t>(Stream::kBlockFrames) * (Stream::kBlocks / 2)) {
                v.stream->Consume(skip);
            } else {
                v.stream->RequestSeek(v.pos);
                streamer_.Wake();
            }
        }
        timelineStart = dsp - lateFrames;
    }
    s.phase.store(static_cast<int32_t>(v.paused ? VoicePhase::Paused : VoicePhase::Playing), std::memory_order_release);
    s.timing.Reset(dsp, SourcePos(v), EffectiveRate(v));
    s.timing.startDsp.store(timelineStart, std::memory_order_relaxed);
    PushAudioEvent(UNA_EVENT_VOICE_STARTED, VoiceHandle(slotIndex, v.gen), dsp, lateFrames);
}

void Engine::EndVoice(VoiceSlot& s, int slotIndex, int64_t dsp) {
    VoiceRT& v = s.rt;
    if (!v.active) return;
    s.timing.Append(dsp, SourcePos(v), 0.0);
    v.active = false;
    anyEnded_ = true;
    PushAudioEvent(UNA_EVENT_VOICE_ENDED, VoiceHandle(slotIndex, v.gen), dsp, 0);
}

void Engine::FailVoice(VoiceSlot& s, int slotIndex, int64_t dsp) {
    PushAudioEvent(UNA_EVENT_VOICE_ERROR, VoiceHandle(slotIndex, s.rt.gen), dsp, s.rt.stream->Error());
    EndVoice(s, slotIndex, dsp);
}

void Engine::FlushEnded() {
    if (!anyEnded_) return;
    anyEnded_ = false;
    size_t w = 0;
    for (size_t r = 0; r < activeVoices_.size(); ++r) {
        const int idx = activeVoices_[r];
        VoiceSlot& s = voices_[static_cast<size_t>(idx)];
        if (s.rt.active) {
            activeVoices_[w++] = idx;
        } else {
            s.rt.clip = nullptr;
            s.rt.stream = nullptr;
            s.rt.pcm = nullptr;
            // After this store the control thread may reclaim and reuse the slot.
            s.phase.store(static_cast<int32_t>(VoicePhase::Ended), std::memory_order_release);
        }
    }
    activeVoices_.resize(w);
}

void Engine::PushAudioEvent(int type, uint32_t handle, int64_t dsp, int64_t value) {
    una_event e{};
    e.type = type;
    e.handle = handle;
    e.dsp_frame = dsp;
    e.value = value;
    if (!audioEvents_->TryPush(e)) droppedEvents_.fetch_add(1, std::memory_order_relaxed);
}

// ── Mixing ─────────────────────────────────────────────────────

void Engine::MixRange(int offset, int frames, int64_t dsp) {
    float* mix = mix_.data() + static_cast<size_t>(offset) * 2;
    for (size_t i = 0; i < activeVoices_.size(); ++i) {
        const int idx = activeVoices_[i];
        VoiceSlot& s = voices_[static_cast<size_t>(idx)];
        VoiceRT& v = s.rt;
        if (!v.active) continue;
        if (v.stream && v.stream->Failed()) {
            FailVoice(s, idx, dsp);
            continue;
        }
        if (!v.started) {
            if (!v.waitPrime || !v.stream->Primed()) continue;
            // Primed() also turns true when the stream fails; checked in this order, a
            // failure that made it true is visible to Failed() too.
            if (v.stream->Failed()) {
                FailVoice(s, idx, dsp);
                continue;
            }
            StartVoice(s, idx, dsp, 0);
        }
        if (v.paused || buses_[v.bus].paused) continue;
        RenderVoice(s, idx, mix, frames, dsp);
    }
    for (auto& b : buses_) b.gain.Advance(frames);
    FlushEnded();
}

void Engine::FetchDecoded(const VoiceRT& v, int64_t start, int count, float* dst, int64_t end) {
    const int ch = v.channels;
    const int64_t decoded = v.clip->decodedFrames.load(std::memory_order_acquire);
    const bool loopValid = v.loop && v.loopStart < v.loopEnd;
    const int64_t loopLen = v.loopEnd - v.loopStart;
    int i = 0;
    while (i < count) {
        int64_t p = start + i;
        int64_t limit = end;
        if (p < 0) {
            const int run = static_cast<int>(std::min<int64_t>(count - i, -p));
            std::memset(dst + static_cast<size_t>(i) * ch, 0, static_cast<size_t>(run) * ch * sizeof(float));
            i += run;
            continue;
        }
        if (p >= end) {
            if (!loopValid) {
                std::memset(dst + static_cast<size_t>(i) * ch, 0, static_cast<size_t>(count - i) * ch * sizeof(float));
                return;
            }
            p = v.loopStart + (p - end) % loopLen;
            limit = v.loopEnd;
        }
        const int run = static_cast<int>(std::min<int64_t>(count - i, limit - p));
        const int have = static_cast<int>(std::max<int64_t>(0, std::min<int64_t>(run, decoded - p)));
        float* d = dst + static_cast<size_t>(i) * ch;
        if (have > 0) {
            const size_t samples = static_cast<size_t>(have) * ch;
            if (v.sampleFormat == UNA_SAMPLE_FLOAT32) {
                std::memcpy(d, static_cast<const float*>(v.pcm) + static_cast<size_t>(p) * ch, samples * sizeof(float));
            } else {
                mix::Int16ToFloat(static_cast<const int16_t*>(v.pcm) + static_cast<size_t>(p) * ch, d, samples);
            }
        }
        if (have < run)
            std::memset(d + static_cast<size_t>(have) * ch, 0, static_cast<size_t>(run - have) * ch * sizeof(float));
        i += run;
    }
}

void Engine::RenderVoice(VoiceSlot& s, int slotIndex, float* out, int n, int64_t dsp) {
    VoiceRT& v = s.rt;
    const int ch = v.channels;
    const BusRT& bus = buses_[v.bus];
    int done = 0;
    while (done < n) {
        // A failed stream ends right after its last decoded frame, before a loop wrap it never reached.
        if (v.stream && v.stream->Failed()) {
            FailVoice(s, slotIndex, dsp + done);
            return;
        }
        const bool loopValid = v.loop && v.loopStart < v.loopEnd;
        // At exactly loopEnd the loop wraps; only a position past it (seek beyond the region) plays on.
        const int64_t end = (loopValid && v.pos <= v.loopEnd) ? v.loopEnd : v.total;
        if (v.pos >= end) {
            if (loopValid) {
                v.pos = v.loopStart + (v.pos - end) % (v.loopEnd - v.loopStart);
                s.timing.Append(dsp + done, SourcePos(v), EffectiveRate(v));
                PushAudioEvent(UNA_EVENT_VOICE_LOOPED, VoiceHandle(slotIndex, v.gen), dsp + done, 0);
                continue;
            }
            EndVoice(s, slotIndex, dsp + done);
            return;
        }
        if (v.stopAfterFade && v.gain.left <= 0) {
            EndVoice(s, slotIndex, dsp + done);
            return;
        }

        int chunk = std::min(n - done, kChunk);
        const int64_t span = std::min<int64_t>(end - v.pos, int64_t(1) << 30);
        const uint64_t remaining = (static_cast<uint64_t>(span) << 32) - v.frac;
        const uint64_t untilEnd = (remaining + v.step - 1) / v.step;
        if (untilEnd < static_cast<uint64_t>(chunk)) chunk = static_cast<int>(untilEnd);
        // Cut chunks at ramp ends so every ramp is exactly linear over its length.
        if (v.gain.left > 0) chunk = std::min(chunk, v.gain.left);
        if (v.panRamp.left > 0) chunk = std::min(chunk, v.panRamp.left);
        if (bus.gain.left > done) chunk = std::min(chunk, bus.gain.left - done);

        uint64_t adv = static_cast<uint64_t>(v.frac) + static_cast<uint64_t>(chunk) * v.step;
        int consumed = static_cast<int>(adv >> 32);
        const uint64_t lastX = static_cast<uint64_t>(v.frac) + static_cast<uint64_t>(chunk - 1) * v.step;
        const int need = std::max(static_cast<int>(lastX >> 32) + 3, consumed + 1);

        float* st = staging_.data();
        if (v.prevValid) {
            st[0] = v.prev[0];
            if (ch == 2) st[1] = v.prev[1];
        } else if (!v.stream) {
            FetchDecoded(v, v.pos - 1, 1, st, end);
        } else {
            st[0] = 0.0f;
            if (ch == 2) st[1] = 0.0f;
        }

        int64_t starved = 0;
        if (v.stream) {
            // The ring follows the loop settings the producer saw. If the voice has diverged
            // (loop changed after those frames were produced, or looping was enabled after the
            // producer reached the end), restart the producer here; this costs one refill.
            const int64_t at = v.stream->CursorClipPos();
            if (at >= 0 ? at != v.pos : (loopValid && v.stream->EndedInEpoch())) {
                v.stream->RequestSeek(v.pos);
                streamer_.Wake();
            }
            // Read before Peek: once decoding has failed, the ring holds everything it will get.
            const bool complete = v.stream->DecodeFailed();
            const int avail = v.stream->Peek(st + ch, need, ch);
            if (avail < consumed) {
                if (!complete) {
                    starved = consumed - avail;
                } else {
                    // Play the output frames that start before the data runs out; the next pass ends the voice.
                    const uint64_t limit = static_cast<uint64_t>(avail) << 32;
                    const int keep = limit > v.frac ? static_cast<int>((limit - v.frac + v.step - 1) / v.step) : 0;
                    if (keep == 0) {
                        FailVoice(s, slotIndex, dsp + done);
                        return;
                    }
                    chunk = std::min(chunk, keep);
                    adv = static_cast<uint64_t>(v.frac) + static_cast<uint64_t>(chunk) * v.step;
                    consumed = static_cast<int>(adv >> 32);
                }
            }
        } else {
            FetchDecoded(v, v.pos, need, st + ch, end);
            const int64_t decoded = v.clip->decodedFrames.load(std::memory_order_relaxed);
            if (decoded < v.total && v.pos + consumed > decoded)
                starved = v.pos + consumed - std::max(v.pos, decoded);
        }

        const float g0 = v.gain.value, g1 = v.gain.After(chunk);
        const float b0 = bus.gain.After(done), b1 = bus.gain.After(done + chunk);
        const float p0 = v.panRamp.value, p1 = v.panRamp.After(chunk);
        const float l0 = g0 * b0 * PanL(p0), r0 = g0 * b0 * PanR(p0);
        const float l1 = g1 * b1 * PanL(p1), r1 = g1 * b1 * PanR(p1);
        const float inv = 1.0f / static_cast<float>(chunk);
        const float dl = (l1 - l0) * inv, dr = (r1 - r0) * inv;
        float* o = out + static_cast<size_t>(done) * 2;

        // st[0..ch) is the frame before the play position (cubic history); 1:1 starts after it.
        const bool exact = v.step == kOne && v.frac == 0;
        if (ch == 1) {
            if (exact) mix::Mono(st + 1, chunk, o, l0, r0, dl, dr);
            else mix::MonoCubic(st, v.frac, v.step, chunk, o, l0, r0, dl, dr);
        } else {
            if (exact) mix::Stereo(st + 2, chunk, o, l0, r0, dl, dr);
            else mix::StereoCubic(st, v.frac, v.step, chunk, o, l0, r0, dl, dr);
        }

        // Staging frame `consumed` is the frame just before the new position.
        v.prev[0] = st[static_cast<size_t>(consumed) * ch];
        if (ch == 2) v.prev[1] = st[static_cast<size_t>(consumed) * ch + 1];
        v.prevValid = true;
        v.pos += consumed;
        v.frac = static_cast<uint32_t>(adv);
        if (v.stream && consumed > 0 && v.stream->Consume(consumed)) streamer_.Wake();
        v.gain.Advance(chunk);
        v.panRamp.Advance(chunk);

        if (starved > 0) {
            starvedFrames_.fetch_add(starved, std::memory_order_relaxed);
            if (!v.starving) {
                v.starving = true;
                PushAudioEvent(UNA_EVENT_VOICE_STARVED, VoiceHandle(slotIndex, v.gen), dsp + done, starved);
            }
        } else {
            v.starving = false;
        }
        done += chunk;
    }
}

// ── Device changes ─────────────────────────────────────────────

void Engine::OnDeviceChanged(const DeviceInfo& info) {
    {
        std::lock_guard<std::mutex> lock(devMutex_);
        devInfo_ = info;
    }
    sampleRate_.store(info.sampleRate, std::memory_order_relaxed);
    channels_ = info.channels;
    fallbackLatencyNs_ = info.sampleRate > 0
                             ? static_cast<int64_t>(info.latencyFrames) * 1000000000LL / info.sampleRate
                             : 0;
    clock_.Reset(info.sampleRate, info.bufferFrames, /*newSession=*/false);
    for (int idx : activeVoices_) {
        VoiceSlot& s = voices_[static_cast<size_t>(idx)];
        if (!s.rt.active) continue;
        UpdateStep(s.rt);
        if (s.rt.started) s.timing.Append(audioDsp_, SourcePos(s.rt), EffectiveRate(s.rt));
    }
    PushAudioEvent(UNA_EVENT_DEVICE_CHANGED, 0, audioDsp_, info.sampleRate);
}

} // namespace una
