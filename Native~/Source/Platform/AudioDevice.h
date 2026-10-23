#pragma once
// Platform output backend interface.
//
// A backend owns the real-time thread (or receives the OS callback) and asks
// the engine for float frames. It reports, per callback, its best estimate
// of the host time (una::HostTimeNs clock) at which the first frame of the
// buffer will be heard — this is what makes the engine's audible clock
// accurate across backends with very different latencies.

#include <cstdint>
#include <memory>

namespace una {

struct DeviceConfig {
    int sampleRate = 0;      // 0 = native
    int bufferFrames = 0;    // 0 = lowest
    int channels = 0;        // 0 = native
    bool exclusive = false;
};

struct DeviceInfo {
    int sampleRate = 0;
    int channels = 0;
    int bufferFrames = 0;    // frames per period
    int latencyFrames = 0;   // estimated output latency (buffer + hardware)
    bool exclusive = false;
    int backend = 0;         // una_backend
};

class DeviceCallback {
public:
    virtual ~DeviceCallback() = default;
    /// Fill `frames` interleaved float frames. presentNs = host time the
    /// first frame reaches the output (0 if unknown).
    virtual void OnRender(float* out, int frames, int channels, int64_t presentNs) = 0;
    /// Format changed (device switch / restart). Called on the device thread
    /// while no OnRender is in flight.
    virtual void OnDeviceChanged(const DeviceInfo& info) = 0;
};

class AudioDevice {
public:
    virtual ~AudioDevice() = default;
    /// Opens the device and negotiates the format. Does not start callbacks.
    virtual bool Open(const DeviceConfig& cfg, DeviceCallback* cb) = 0;
    virtual bool Start() = 0;
    /// Stops callbacks; returns only once no callback is running.
    virtual void Stop() = 0;
    virtual void Close() = 0;
    virtual DeviceInfo Info() const = 0;
};

std::unique_ptr<AudioDevice> CreatePlatformDevice();
std::unique_ptr<AudioDevice> CreateNullDevice();

} // namespace una
