#ifndef UNAUDIO_EVENT_QUEUE_H
#define UNAUDIO_EVENT_QUEUE_H

/*
 * Lock-free event queue: Audio Thread → Main Thread.
 * Events are enqueued from the audio callback (no allocation)
 * and drained on the main thread in Update().
 */

#include "RingBuffer.h"
#include <cstdint>

namespace una {

struct AudioEvent {
    enum Type : uint8_t {
        Noop = 0,
        VoiceFinished,    // voice completed playback (non-loop)
        LoopPoint,        // voice looped back to start
        Marker,           // hit a user-defined marker
        DeviceLost,       // audio output device disconnected
        DeviceRestored,   // audio output device reconnected
        BufferUnderrun,   // decode couldn't keep up (debug)
    };

    Type     type       = Noop;
    int32_t  voice_id   = -1;
    uint32_t generation = 0;     // to detect stale handles
    int32_t  param      = 0;     // marker index or error code
};

// 512-slot queue — audio thread rarely produces more than a few events per callback
using EventQueue = RingBuffer<AudioEvent, 512>;

} // namespace una

#endif // UNAUDIO_EVENT_QUEUE_H
