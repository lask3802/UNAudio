#include "DecodePipeline.h"

#include "unaudio.h"

#include <algorithm>
#include <cstring>

namespace una {

namespace {

enum Role { FL, FR, FC, LFE, SL, SR, BL, BR, BC };

// Channel orders: Vorbis I spec §4.3.9 and the WAVE default channel mask (FLAC uses the
// WAVE order; MP3 has at most two channels).
const Role* ChannelRoles(SourceKind kind, int channels) {
    static const Role vorbis[9][8] = {
        {}, {FC}, {FL, FR}, {FL, FC, FR}, {FL, FR, BL, BR}, {FL, FC, FR, BL, BR},
        {FL, FC, FR, BL, BR, LFE}, {FL, FC, FR, SL, SR, BC, LFE}, {FL, FC, FR, SL, SR, BL, BR, LFE}};
    static const Role wav[9][8] = {
        {}, {FC}, {FL, FR}, {FL, FR, FC}, {FL, FR, BL, BR}, {FL, FR, FC, BL, BR},
        {FL, FR, FC, LFE, BL, BR}, {FL, FR, FC, LFE, BC, SL, SR}, {FL, FR, FC, LFE, BL, BR, SL, SR}};
    if (channels < 1 || channels > 8) return nullptr;
    return kind == SourceKind::Vorbis ? vorbis[channels] : wav[channels];
}

} // namespace

int DecodePipeline::Open(const uint8_t* data, size_t size, int targetRate, int64_t sourceFrames) {
    kind_ = DetectSourceKind(data, size);
    if (kind_ == SourceKind::Unknown) return UNA_E_FORMAT;
    dec_ = CreateSourceDecoder(data, size, sourceFrames);
    if (!dec_) return UNA_E_FORMAT;
    srcFmt_ = dec_->Format();

    storedChannels_ = srcFmt_.channels >= 2 ? 2 : 1;
    if (srcFmt_.channels > 2) {
        const Role* roles = ChannelRoles(kind_, srcFmt_.channels);
        if (!roles) return UNA_E_FORMAT;
        constexpr float k = 0.70710678f;
        float sumL = 0.0f, sumR = 0.0f;
        for (int c = 0; c < srcFmt_.channels; ++c) {
            float l = 0.0f, r = 0.0f;
            switch (roles[c]) {
                case FL: l = 1.0f; break;
                case FR: r = 1.0f; break;
                case FC: l = r = k; break;
                case SL: case BL: l = k; break;
                case SR: case BR: r = k; break;
                case BC: l = r = 0.5f; break;
                case LFE: break;
            }
            coefL_[c] = l; coefR_[c] = r;
            sumL += l; sumR += r;
        }
        const float norm = 1.0f / std::max(1.0f, std::max(sumL, sumR));
        for (int c = 0; c < srcFmt_.channels; ++c) { coefL_[c] *= norm; coefR_[c] *= norm; }
    }

    for (int c = 0; c < srcFmt_.channels; ++c) raw_[c].assign(kChunk, 0.0f);
    for (int c = 0; c < storedChannels_; ++c) mixed_[c].assign(kChunk, 0.0f);

    resample_ = targetRate > 0 && targetRate != srcFmt_.sampleRate;
    if (resample_) {
        if (!rs_.Init(srcFmt_.sampleRate, targetRate, storedChannels_)) return UNA_E_FORMAT;
        storedRate_ = targetRate;
        total_ = rs_.OutputLength(srcFmt_.totalFrames);
    } else {
        storedRate_ = srcFmt_.sampleRate;
        total_ = srcFmt_.totalFrames;
    }
    pos_ = 0;
    srcEnd_ = false;
    srcPos_ = 0;
    srcShort_ = false;
    return total_ > 0 ? UNA_OK : UNA_E_FORMAT;
}

void DecodePipeline::Downmix(int frames, float* const* out) {
    const int ch = srcFmt_.channels;
    if (ch <= 2) {
        for (int c = 0; c < ch; ++c)
            std::memcpy(out[c], raw_[c].data(), static_cast<size_t>(frames) * sizeof(float));
        return;
    }
    float* l = out[0];
    float* r = out[1];
    std::memset(l, 0, static_cast<size_t>(frames) * sizeof(float));
    std::memset(r, 0, static_cast<size_t>(frames) * sizeof(float));
    for (int c = 0; c < ch; ++c) {
        const float* s = raw_[c].data();
        const float cl = coefL_[c], cr = coefR_[c];
        if (cl != 0.0f) for (int i = 0; i < frames; ++i) l[i] += s[i] * cl;
        if (cr != 0.0f) for (int i = 0; i < frames; ++i) r[i] += s[i] * cr;
    }
}

int DecodePipeline::ReadSource(float* const* out, int frames) {
    frames = std::min(frames, kChunk);
    float* raw[kMaxSourceChannels];
    for (int c = 0; c < srcFmt_.channels; ++c) raw[c] = raw_[c].data();
    const int n = dec_->Read(raw, frames);
    if (n > 0) {
        Downmix(n, out);
        srcPos_ += n;
    } else if (srcPos_ < srcFmt_.totalFrames) {
        srcShort_ = true;
    }
    return n;
}

int DecodePipeline::Read(float* const* out, int frames) {
    const int64_t left = total_ - pos_;
    if (left <= 0 || frames <= 0) return 0;
    frames = static_cast<int>(std::min<int64_t>(frames, left));

    int done = 0;
    if (!resample_) {
        while (done < frames) {
            float* dst[2] = {out[0] + done, storedChannels_ > 1 ? out[1] + done : nullptr};
            int n = srcEnd_ ? 0 : ReadSource(dst, frames - done);
            if (n <= 0) {
                // Source shorter than its header claimed: pad with silence so
                // the frame count the mixer was promised stays exact.
                srcEnd_ = true;
                for (int c = 0; c < storedChannels_; ++c)
                    std::memset(out[c] + done, 0, static_cast<size_t>(frames - done) * sizeof(float));
                done = frames;
                break;
            }
            done += n;
        }
        pos_ += done;
        return done;
    }

    while (done < frames) {
        float* dst[2] = {out[0] + done, storedChannels_ > 1 ? out[1] + done : nullptr};
        const int n = rs_.Produce(dst, frames - done);
        done += n;
        if (done >= frames) break;
        if (!srcEnd_) {
            float* mixed[2] = {mixed_[0].data(), storedChannels_ > 1 ? mixed_[1].data() : nullptr};
            const int want = std::max(rs_.InputDeficit(), 256);
            const int got = ReadSource(mixed, std::min(want, kChunk));
            if (got > 0) {
                rs_.Push(mixed, got);
                continue;
            }
            srcEnd_ = true;
        }
        rs_.PushSilence(std::max(rs_.InputDeficit(), rs_.Taps()));
    }
    pos_ += done;
    return done;
}

bool DecodePipeline::Seek(int64_t storedFrame) {
    storedFrame = std::max<int64_t>(0, std::min(storedFrame, total_));
    srcEnd_ = false;
    srcShort_ = false;
    if (!resample_) {
        if (!dec_->Seek(storedFrame)) return false;
        pos_ = srcPos_ = storedFrame;
        return true;
    }
    const int64_t firstInput = rs_.Reset(storedFrame);
    if (firstInput >= srcFmt_.totalFrames) {
        srcEnd_ = true;
        srcPos_ = srcFmt_.totalFrames;
    } else if (!dec_->Seek(firstInput)) {
        return false;
    } else {
        srcPos_ = firstInput;
    }
    pos_ = storedFrame;
    return true;
}

} // namespace una
