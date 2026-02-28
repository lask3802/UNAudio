#include "ResamplingDecoder.h"
#include <cstring>
#include <algorithm>

ResamplingDecoder::ResamplingDecoder() = default;
ResamplingDecoder::~ResamplingDecoder() = default;

bool ResamplingDecoder::Init(std::unique_ptr<AudioDecoder> inner, int32_t targetSampleRate) {
    if (!inner || targetSampleRate <= 0) return false;

    UNAudioFormat fmt = inner->GetFormat();
    if (fmt.sampleRate <= 0 || fmt.channels <= 0) return false;

    inner_    = std::move(inner);
    sourceSR_ = fmt.sampleRate;
    targetSR_ = targetSampleRate;
    channels_ = fmt.channels;
    ratio_    = static_cast<double>(sourceSR_) / static_cast<double>(targetSR_);

    // Allocate source buffer: SRC_BUF_FRAMES + margin for interpolation lookback/ahead
    srcBufCapacity_ = SRC_BUF_FRAMES + INTERP_MARGIN * 2;
    srcBuf_.resize(static_cast<size_t>(srcBufCapacity_) * channels_, 0.0f);
    srcBufFrames_ = 0;
    phase_ = 0.0;
    innerDone_ = false;

    // Prefill the buffer
    refillBuffer();

    return true;
}

bool ResamplingDecoder::Open(const uint8_t* /*data*/, size_t /*size*/) {
    // Not used — Init() replaces Open() for this decorator.
    return false;
}

int ResamplingDecoder::Decode(float* buffer, int frameCount) {
    if (!inner_ || frameCount <= 0 || !buffer) return 0;

    int produced = 0;

    while (produced < frameCount) {
        int idx = static_cast<int>(std::floor(phase_));

        // We need at least idx + 2 frames for cubic interpolation (s0..s3)
        // s0 = idx-1, s1 = idx, s2 = idx+1, s3 = idx+2
        int needed = idx + 3; // need frame at index idx+2, so srcBufFrames_ >= idx+3
        while (srcBufFrames_ < needed && !innerDone_) {
            if (refillBuffer() == 0) {
                innerDone_ = true;
            }
        }

        // Can we produce this sample? Need s1 at idx to exist.
        // s0/s2/s3 are clamped so only idx must be in range.
        if (idx >= srcBufFrames_) {
            break; // no more source data
        }

        float frac = static_cast<float>(phase_ - idx);

        for (int ch = 0; ch < channels_; ++ch) {
            // Clamp indices to valid range
            int i0 = std::max(0, idx - 1);
            int i1 = idx;
            int i2 = std::min(srcBufFrames_ - 1, idx + 1);
            int i3 = std::min(srcBufFrames_ - 1, idx + 2);

            float s0 = srcBuf_[i0 * channels_ + ch];
            float s1 = srcBuf_[i1 * channels_ + ch];
            float s2 = srcBuf_[i2 * channels_ + ch];
            float s3 = srcBuf_[i3 * channels_ + ch];

            buffer[produced * channels_ + ch] = hermite(s0, s1, s2, s3, frac);
        }

        ++produced;
        phase_ += ratio_;

        // Periodically shift out consumed frames to prevent unbounded growth.
        // Keep a margin of 1 frame behind the read position for s0 lookback.
        int consumed = static_cast<int>(std::floor(phase_)) - 1;
        if (consumed > SRC_BUF_FRAMES / 2) {
            shiftBuffer(consumed);
            phase_ -= consumed;
            if (phase_ < 0.0) phase_ = 0.0; // guard against FP drift
        }
    }

    // Final shift of consumed frames
    int consumed = static_cast<int>(std::floor(phase_)) - 1;
    if (consumed > 0 && consumed < srcBufFrames_) {
        shiftBuffer(consumed);
        phase_ -= consumed;
        if (phase_ < 0.0) phase_ = 0.0; // guard against FP drift
    }

    return produced;
}

bool ResamplingDecoder::Seek(int64_t frame) {
    if (!inner_) return false;

    // frame is in source space
    bool ok = inner_->Seek(frame);

    // Reset resampler state
    srcBufFrames_ = 0;
    phase_ = 0.0;
    innerDone_ = false;
    std::fill(srcBuf_.begin(), srcBuf_.end(), 0.0f);

    // Prefill buffer from new position
    refillBuffer();

    return ok;
}

UNAudioFormat ResamplingDecoder::GetFormat() const {
    if (!inner_) return {};

    UNAudioFormat fmt = inner_->GetFormat();
    fmt.sampleRate = targetSR_; // report target rate to mixer
    return fmt;
}

bool ResamplingDecoder::SupportsStreaming() const {
    return inner_ ? inner_->SupportsStreaming() : false;
}

int64_t ResamplingDecoder::GetTotalFrames() const {
    // Return source-space total frames so playback time calculation
    // with clipInfo.sampleRate (source rate) stays correct.
    return inner_ ? inner_->GetTotalFrames() : 0;
}

int64_t ResamplingDecoder::GetCurrentFrame() const {
    // Return source-space current frame for accurate playback time.
    return inner_ ? inner_->GetCurrentFrame() : 0;
}

int ResamplingDecoder::refillBuffer() {
    if (!inner_ || innerDone_) return 0;

    int spaceFrames = srcBufCapacity_ - srcBufFrames_;
    if (spaceFrames <= 0) return 0;

    float* dest = srcBuf_.data() + static_cast<size_t>(srcBufFrames_) * channels_;
    int decoded = inner_->Decode(dest, spaceFrames);
    srcBufFrames_ += decoded;
    return decoded;
}

void ResamplingDecoder::shiftBuffer(int consumed) {
    if (consumed <= 0 || consumed >= srcBufFrames_) {
        if (consumed >= srcBufFrames_) {
            srcBufFrames_ = 0;
        }
        return;
    }

    int remaining = srcBufFrames_ - consumed;
    std::memmove(srcBuf_.data(),
                 srcBuf_.data() + static_cast<size_t>(consumed) * channels_,
                 static_cast<size_t>(remaining) * channels_ * sizeof(float));
    srcBufFrames_ = remaining;
}
