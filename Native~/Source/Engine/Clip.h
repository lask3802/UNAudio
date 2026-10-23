#pragma once
// Clip = loaded audio data shared by any number of voices.
//
// Lifetime: `refs` counts the API owner, every voice slot that plays it, every
// in-flight worker job and every Stream reading it (a Stream may outlive its voice
// slot on the stream thread). Only the control thread deletes a clip (in
// Engine::CollectClips) once the owner released it and refs reached zero; the
// audio thread never touches the count.

#include "../Decode/DecodePipeline.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace una {

struct Clip {
    uint32_t handle = 0;

    std::atomic<int> refs{1};
    std::atomic<bool> released{false};
    std::atomic<int> state{0};          // una_clip_state
    std::atomic<int> error{0};
    std::atomic<int> priority{0};
    std::atomic<bool> cancel{false};

    // Load request (immutable after creation)
    int mode = 0;                       // una_load_mode
    int sampleFormat = 0;               // una_sample_format
    int flags = 0;
    int targetRate = 0;
    std::string path;                   // file load
    std::vector<uint8_t> owned;         // copied / file bytes
    const uint8_t* src = nullptr;       // == owned.data() or borrowed memory
    size_t srcSize = 0;
    bool borrowed = false;

    // Format — written by the worker before state becomes READY (release),
    // immutable afterwards.
    int channels = 0;
    int sourceChannels = 0;
    int sourceRate = 0;
    int storedRate = 0;
    int64_t totalFrames = 0;

    // PCM (DECODE mode): interleaved, allocated once, never moved.
    void* pcm = nullptr;
    size_t pcmBytes = 0;
    std::atomic<int64_t> decodedFrames{0};
    std::atomic<int64_t> memoryBytes{0};

    // Worker-only decode state
    std::unique_ptr<DecodePipeline> pipeline;

    ~Clip();
    void FreeSource();
};

} // namespace una
