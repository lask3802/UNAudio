#include "VorbisDecoder.h"
#include <cstring>

// TODO: #include <vorbis/vorbisfile.h>  – integrate libvorbis in a later phase

VorbisDecoder::VorbisDecoder()  = default;
VorbisDecoder::~VorbisDecoder() = default;

bool VorbisDecoder::Open(const uint8_t* data, size_t size) {
    if (!data || size == 0) return false;
    data_     = data;
    dataSize_ = size;
    currentFrame_ = 0;

    // TODO: Initialise libvorbis, read format info
    format_.sampleRate    = 44100;
    format_.channels      = 2;
    format_.bitsPerSample = 32;
    format_.blockAlign    = format_.channels * (format_.bitsPerSample / 8);

    return true;
}

int VorbisDecoder::Decode(float* buffer, int frameCount) {
    if (!data_) return 0;
    // TODO: Decode via libvorbis
    std::memset(buffer, 0,
                static_cast<size_t>(frameCount) * format_.channels * sizeof(float));
    currentFrame_ += frameCount;
    return frameCount;
}

bool VorbisDecoder::Seek(int64_t frame) {
    currentFrame_ = frame;
    return true;
}

UNAudioFormat VorbisDecoder::GetFormat()   const { return format_; }
bool VorbisDecoder::SupportsStreaming()     const { return true; }
int64_t VorbisDecoder::GetTotalFrames()    const { return totalFrames_; }
