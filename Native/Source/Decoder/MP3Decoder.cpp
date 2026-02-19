#include "MP3Decoder.h"
#include <cstring>

// TODO: #include <mpg123.h>  – integrate libmpg123 in a later phase

MP3Decoder::MP3Decoder()  = default;
MP3Decoder::~MP3Decoder() = default;

bool MP3Decoder::Open(const uint8_t* data, size_t size) {
    if (!data || size == 0) return false;
    data_     = data;
    dataSize_ = size;
    currentFrame_ = 0;

    // TODO: Initialise mpg123, feed data, read format info
    format_.sampleRate    = 44100;
    format_.channels      = 2;
    format_.bitsPerSample = 32;    // float output
    format_.blockAlign    = format_.channels * (format_.bitsPerSample / 8);

    return true;
}

int MP3Decoder::Decode(float* buffer, int frameCount) {
    if (!data_) return 0;
    // TODO: Decode via mpg123
    // Stub: fill silence
    std::memset(buffer, 0,
                static_cast<size_t>(frameCount) * format_.channels * sizeof(float));
    currentFrame_ += frameCount;
    return frameCount;
}

bool MP3Decoder::Seek(int64_t frame) {
    // TODO: mpg123_seek
    currentFrame_ = frame;
    return true;
}

UNAudioFormat MP3Decoder::GetFormat()   const { return format_; }
bool MP3Decoder::SupportsStreaming()     const { return true; }
int64_t MP3Decoder::GetTotalFrames()    const { return totalFrames_; }
