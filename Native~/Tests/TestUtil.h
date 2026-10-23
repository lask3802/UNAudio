#pragma once

#include "unaudio.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace test {

std::vector<uint8_t> ReadFixture(const std::string& name);

/// Float32 WAV in memory; gen(frame, channel) -> sample.
std::vector<uint8_t> MakeWavFloat(int rate, int channels, int frames,
                                  const std::function<float(int, int)>& gen);
/// Integer PCM WAV (bits = 8/16/24/32).
std::vector<uint8_t> MakeWavPcm(int rate, int channels, int frames, int bits,
                                const std::function<float(int, int)>& gen);

/// Engine in MANUAL mode for deterministic rendering.
class ManualEngine {
public:
    explicit ManualEngine(int rate = 48000, int buffer = 256, int maxVoices = 64);
    ~ManualEngine();
    bool ok() const { return rc_ == UNA_OK; }
    int rc() const { return rc_; }
    /// Renders `frames` stereo frames, appending to `out` if given.
    void Render(int frames, std::vector<float>* out = nullptr, int block = 256);
    int64_t MixFrame() const;

private:
    int rc_;
};

una_clip LoadClip(const std::vector<uint8_t>& data, int mode = UNA_LOAD_DECODE,
                  int format = UNA_SAMPLE_FLOAT32, int flags = 0);
/// Polls until the clip reaches `state` (or FAILED). Returns final state.
int WaitClip(una_clip clip, int state, int timeoutMs = 5000);
una_play_params DefaultPlay();
std::vector<una_event> DrainEvents();

} // namespace test
