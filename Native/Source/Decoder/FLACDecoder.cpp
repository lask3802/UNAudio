#include "FLACDecoder.h"
#include <cstring>

// TODO: #include <FLAC/stream_decoder.h>  – integrate libflac in a later phase

FLACDecoder::FLACDecoder()  = default;
FLACDecoder::~FLACDecoder() = default;

bool FLACDecoder::Open(const uint8_t* /*data*/, size_t /*size*/) {
    // Stub: libflac not yet integrated. Return false so the decoder
    // fallback chain correctly skips this and tries the next format.
    return false;
}

int FLACDecoder::Decode(float* buffer, int frameCount) {
    if (!data_) return 0;
    // TODO: Decode via libflac
    std::memset(buffer, 0,
                static_cast<size_t>(frameCount) * format_.channels * sizeof(float));
    currentFrame_ += frameCount;
    return frameCount;
}

bool FLACDecoder::Seek(int64_t frame) {
    currentFrame_ = frame;
    return true;
}

UNAudioFormat FLACDecoder::GetFormat()   const { return format_; }
bool FLACDecoder::SupportsStreaming()     const { return true; }
int64_t FLACDecoder::GetTotalFrames()       const { return totalFrames_; }
int64_t FLACDecoder::GetCurrentFrame()      const { return currentFrame_; }
