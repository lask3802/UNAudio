#pragma once
// Source decoder → channel downmix (≤ 2) → optional resampler.
// Produces exactly TotalFrames() frames at StoredRate(), seekable to any
// stored frame. Used by both full-decode jobs and stream voices.

#include "Resampler.h"
#include "SourceDecoder.h"

#include <memory>
#include <vector>

namespace una {

class DecodePipeline {
public:
    static constexpr int kChunk = 2048;

    /// `targetRate` <= 0 keeps the source rate. Returns a una_result code.
    int Open(const uint8_t* data, size_t size, int targetRate);

    int SourceRate() const { return srcFmt_.sampleRate; }
    int SourceChannels() const { return srcFmt_.channels; }
    int StoredRate() const { return storedRate_; }
    int StoredChannels() const { return storedChannels_; }
    int64_t TotalFrames() const { return total_; }
    int64_t Position() const { return pos_; }

    /// Writes up to `frames` planar frames into out[0..StoredChannels()).
    /// Returns 0 only at the end.
    int Read(float* const* out, int frames);

    bool Seek(int64_t storedFrame);

private:
    int ReadSource(float* const* out, int frames);   // decode + downmix
    void Downmix(int frames, float* const* out);

    std::unique_ptr<SourceDecoder> dec_;
    SourceKind kind_ = SourceKind::Unknown;
    SourceFormat srcFmt_{};
    int storedRate_ = 0;
    int storedChannels_ = 0;
    int64_t total_ = 0;
    int64_t pos_ = 0;
    bool srcEnd_ = false;

    bool resample_ = false;
    Resampler rs_;

    std::vector<float> raw_[kMaxSourceChannels];
    std::vector<float> mixed_[2];
    float coefL_[kMaxSourceChannels] = {};
    float coefR_[kMaxSourceChannels] = {};
};

} // namespace una
