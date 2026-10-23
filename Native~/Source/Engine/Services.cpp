// Worker services: clip decode pool and the stream refill thread.

#include "Engine.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace una {

namespace {

constexpr int64_t kSliceFrames = 48000;   // decode quantum before yielding to other jobs
constexpr int64_t kFirstSliceFrames = 8192; // decoded before READY so playback can start

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
#if defined(_WIN32)
    // UTF-8 → UTF-16 for non-ASCII paths.
    std::wstring wpath;
    {
        const char* s = path.c_str();
        size_t i = 0;
        while (s[i]) {
            uint32_t cp;
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x80) { cp = c; i += 1; }
            else if ((c >> 5) == 0x6 && s[i + 1]) { cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F); i += 2; }
            else if ((c >> 4) == 0xE && s[i + 1] && s[i + 2]) {
                cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); i += 3;
            } else if ((c >> 3) == 0x1E && s[i + 1] && s[i + 2] && s[i + 3]) {
                cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
                i += 4;
            } else { return false; }
            if (cp >= 0x10000) {
                cp -= 0x10000;
                wpath.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
                wpath.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
            } else {
                wpath.push_back(static_cast<wchar_t>(cp));
            }
        }
    }
    FILE* f = _wfopen(wpath.c_str(), L"rb");
#else
    FILE* f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return false;
    bool ok = false;
    if (std::fseek(f, 0, SEEK_END) == 0) {
        const long len = std::ftell(f);
        if (len > 0 && std::fseek(f, 0, SEEK_SET) == 0) {
            try {
                out.resize(static_cast<size_t>(len));
                ok = std::fread(out.data(), 1, out.size(), f) == out.size();
            } catch (...) {
                ok = false;
            }
        }
    }
    std::fclose(f);
    return ok;
}

} // namespace

// ── DecodeService ──────────────────────────────────────────────

void DecodeService::Start(Engine* engine, int threads) {
    engine_ = engine;
    running_ = true;
    for (int i = 0; i < threads; ++i) threads_.emplace_back([this] { Run(); });
}

void DecodeService::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    cv_.notify_all();
    for (auto& t : threads_) t.join();
    threads_.clear();
    for (auto& job : queue_) {
        job.second->refs.fetch_sub(1, std::memory_order_acq_rel);
        pending_.fetch_sub(1, std::memory_order_relaxed);
    }
    queue_.clear();
}

void DecodeService::Submit(Clip* clip) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.emplace_back(seq_++, clip);
        pending_.fetch_add(1, std::memory_order_relaxed);
    }
    cv_.notify_one();
}

void DecodeService::Reprioritize() { cv_.notify_all(); }

void DecodeService::Run() {
    SetCurrentThreadName("UNAudio Decode");
    SetCurrentThreadRole(ThreadRole::Decoder);
    for (;;) {
        Clip* clip = nullptr;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !running_ || !queue_.empty(); });
            if (!running_) return;
            // Highest priority first, FIFO within a priority (round-robin by re-queue).
            auto best = queue_.begin();
            for (auto it = queue_.begin(); it != queue_.end(); ++it) {
                const int pa = it->second->priority.load(std::memory_order_relaxed);
                const int pb = best->second->priority.load(std::memory_order_relaxed);
                if (pa > pb || (pa == pb && it->first < best->first)) best = it;
            }
            clip = best->second;
            queue_.erase(best);
        }

        const bool finished = Step(clip);
        if (finished) {
            pending_.fetch_sub(1, std::memory_order_relaxed);
            clip->refs.fetch_sub(1, std::memory_order_acq_rel);
        } else {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!running_) {
                    pending_.fetch_sub(1, std::memory_order_relaxed);
                    clip->refs.fetch_sub(1, std::memory_order_acq_rel);
                    return;
                }
                queue_.emplace_back(seq_++, clip);
            }
            cv_.notify_one();
        }
    }
}

bool DecodeService::Step(Clip* clip) {
    auto fail = [&](int err) {
        clip->pipeline.reset();
        clip->error.store(err, std::memory_order_relaxed);
        clip->state.store(UNA_CLIP_FAILED, std::memory_order_release);
        engine_->PostWorkerEvent(UNA_EVENT_CLIP_FAILED, clip->handle, err);
        return true;
    };

    // Owner released and no voice holds it: nobody can observe the result.
    if (clip->released.load(std::memory_order_acquire) && clip->refs.load(std::memory_order_acquire) <= 1) {
        clip->pipeline.reset();
        return true;
    }

    const int st = clip->state.load(std::memory_order_acquire);
    if (st == UNA_CLIP_LOADED || st == UNA_CLIP_FAILED) return true;

    if (!clip->pipeline) {
        if (!clip->path.empty()) {
            if (!ReadWholeFile(clip->path, clip->owned)) return fail(UNA_E_IO);
            clip->src = clip->owned.data();
            clip->srcSize = clip->owned.size();
            engine_->AddClipMemory(static_cast<int64_t>(clip->srcSize));
            clip->memoryBytes.fetch_add(static_cast<int64_t>(clip->srcSize), std::memory_order_relaxed);
        }
        auto pipe = std::make_unique<DecodePipeline>();
        const int rc = pipe->Open(clip->src, clip->srcSize, clip->targetRate);
        if (rc != UNA_OK) return fail(rc);

        clip->channels = pipe->StoredChannels();
        clip->sourceChannels = pipe->SourceChannels();
        clip->sourceRate = pipe->SourceRate();
        clip->storedRate = pipe->StoredRate();
        clip->totalFrames = pipe->TotalFrames();

        if (clip->mode == UNA_LOAD_STREAM) {
            // Each stream voice opens its own pipeline on the shared bytes.
            clip->state.store(UNA_CLIP_READY, std::memory_order_release);
            engine_->PostWorkerEvent(UNA_EVENT_CLIP_READY, clip->handle, 0);
            clip->state.store(UNA_CLIP_LOADED, std::memory_order_release);
            engine_->PostWorkerEvent(UNA_EVENT_CLIP_LOADED, clip->handle, 0);
            return true;
        }

        const uint64_t bps = clip->sampleFormat == UNA_SAMPLE_FLOAT32 ? 4 : 2;
        const uint64_t frameBytes = static_cast<uint64_t>(clip->channels) * bps;
        // totalFrames comes from the file header; on 32-bit targets a large (or forged)
        // value must not wrap into a small allocation the decoder then overruns.
        if (clip->totalFrames < 0 ||
            static_cast<uint64_t>(clip->totalFrames) > static_cast<uint64_t>(SIZE_MAX) / frameBytes)
            return fail(UNA_E_OUT_OF_MEMORY);
        const size_t bytes = static_cast<size_t>(static_cast<uint64_t>(clip->totalFrames) * frameBytes);
        clip->pcm = std::malloc(bytes);
        if (!clip->pcm) return fail(UNA_E_OUT_OF_MEMORY);
        clip->pcmBytes = bytes;
        engine_->AddClipMemory(static_cast<int64_t>(bytes));
        clip->memoryBytes.fetch_add(static_cast<int64_t>(bytes), std::memory_order_relaxed);
        clip->pipeline = std::move(pipe);
    }

    DecodePipeline& pipe = *clip->pipeline;
    const int ch = clip->channels;
    const bool isFloat = clip->sampleFormat == UNA_SAMPLE_FLOAT32;
    float planar[2][DecodePipeline::kChunk];
    float* pl[2] = {planar[0], planar[1]};

    int64_t done = clip->decodedFrames.load(std::memory_order_relaxed);
    const int64_t slice = st == UNA_CLIP_LOADING ? kFirstSliceFrames : kSliceFrames;
    const int64_t stop = std::min(clip->totalFrames, done + slice);
    while (done < stop) {
        const int want = static_cast<int>(std::min<int64_t>(DecodePipeline::kChunk, stop - done));
        const int n = pipe.Read(pl, want);
        if (n <= 0) break;
        if (isFloat) {
            float* dst = static_cast<float*>(clip->pcm) + static_cast<size_t>(done) * ch;
            if (ch == 1) {
                std::memcpy(dst, planar[0], static_cast<size_t>(n) * sizeof(float));
            } else {
                for (int i = 0; i < n; ++i) { dst[2 * i] = planar[0][i]; dst[2 * i + 1] = planar[1][i]; }
            }
        } else {
            int16_t* dst = static_cast<int16_t*>(clip->pcm) + static_cast<size_t>(done) * ch;
            for (int c = 0; c < ch; ++c) {
                const float* s = planar[c];
                for (int i = 0; i < n; ++i) {
                    float v = s[i] * 32767.0f;
                    v = v > 32767.0f ? 32767.0f : (v < -32768.0f ? -32768.0f : v);
                    dst[static_cast<size_t>(i) * ch + c] = static_cast<int16_t>(v >= 0.0f ? v + 0.5f : v - 0.5f);
                }
            }
        }
        done += n;
        // Publish progress per chunk so a voice running ahead of the decoder waits less.
        clip->decodedFrames.store(done, std::memory_order_release);
    }
    if (done < stop) {
        // The pipeline guarantees TotalFrames(); a short read means corruption.
        return fail(UNA_E_FORMAT);
    }

    if (st == UNA_CLIP_LOADING) {
        clip->state.store(UNA_CLIP_READY, std::memory_order_release);
        engine_->PostWorkerEvent(UNA_EVENT_CLIP_READY, clip->handle, 0);
    }

    if (done >= clip->totalFrames) {
        clip->pipeline.reset();
        if (clip->borrowed) {
            clip->src = nullptr;
            clip->srcSize = 0;
            engine_->PostWorkerEvent(UNA_EVENT_CLIP_SOURCE_RELEASED, clip->handle, 0);
        } else if (!clip->owned.empty()) {
            const int64_t freed = static_cast<int64_t>(clip->owned.size());
            clip->FreeSource();
            engine_->AddClipMemory(-freed);
            clip->memoryBytes.fetch_sub(freed, std::memory_order_relaxed);
        }
        clip->state.store(UNA_CLIP_LOADED, std::memory_order_release);
        engine_->PostWorkerEvent(UNA_EVENT_CLIP_LOADED, clip->handle, 0);
        return true;
    }
    return false;
}

// ── StreamService ──────────────────────────────────────────────

void StreamService::Start() {
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { Run(); });
}

void StreamService::Stop() {
    if (!running_.exchange(false)) return;
    sem_.Signal();
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    streams_.clear();
    scratch_.clear();
}

void StreamService::Add(const std::shared_ptr<Stream>& s) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        streams_.push_back(s);
    }
    sem_.Signal();
}

void StreamService::Remove(Stream* s) {
    std::lock_guard<std::mutex> lock(mutex_);
    streams_.erase(std::remove_if(streams_.begin(), streams_.end(),
                                  [s](const std::shared_ptr<Stream>& p) { return p.get() == s; }),
                   streams_.end());
}

int StreamService::Count() {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(streams_.size());
}

void StreamService::Run() {
    SetCurrentThreadName("UNAudio Stream");
    SetCurrentThreadRole(ThreadRole::StreamService);
    while (running_.load(std::memory_order_acquire)) {
        sem_.Wait(4);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            scratch_.assign(streams_.begin(), streams_.end());
        }
        for (auto& s : scratch_) {
            if (!s->dead.load(std::memory_order_acquire)) s->Refill();
        }
        // Releasing here may destroy streams the control thread already dropped.
        scratch_.clear();
    }
}

} // namespace una
