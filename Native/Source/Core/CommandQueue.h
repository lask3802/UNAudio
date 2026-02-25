#ifndef UNAUDIO_COMMAND_QUEUE_H
#define UNAUDIO_COMMAND_QUEUE_H

/*
 * Lock-free command queue: Main Thread → Audio Thread.
 * Supports sample-accurate scheduling and batch submission.
 */

#include "RingBuffer.h"
#include "AudioTypes.h"
#include <cstdint>

namespace una {

struct AudioCommand {
    enum Type : uint8_t {
        Noop = 0,
        Play,
        Stop,
        Pause,
        Resume,
        SetVolume,
        SetPitch,
        SetPan,
        SetLoop,
        FadeVolume,
        Seek,
        StopAll,
    };

    Type     type          = Noop;
    int32_t  voice_id      = -1;
    float    param0        = 0.0f;  // primary scalar: volume / pitch / pan
    float    param1        = 0.0f;  // fade target
    float    duration      = 0.0f;  // fade duration in seconds
    int64_t  seek_frame    = 0;     // Seek: target frame index (avoids float precision loss)
    uint64_t schedule_sample = 0;   // 0 = immediate, >0 = absolute sample position
};

// 1024-slot queue ≈ handles bursts of ~1000 commands per audio callback
using CommandQueue = RingBuffer<AudioCommand, 1024>;

// Batch helper: accumulate commands, submit in one go
class CommandBatch {
public:
    static constexpr size_t MAX_BATCH = 64;

    void add(const AudioCommand& cmd) {
        if (count_ < MAX_BATCH)
            commands_[count_++] = cmd;
    }

    size_t submit(CommandQueue& queue) {
        size_t pushed = queue.try_push_batch(commands_, count_);
        count_ = 0;
        return pushed;
    }

    size_t count() const { return count_; }
    void clear() { count_ = 0; }

private:
    AudioCommand commands_[MAX_BATCH];
    size_t count_ = 0;
};

} // namespace una

#endif // UNAUDIO_COMMAND_QUEUE_H
