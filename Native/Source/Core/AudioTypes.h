#ifndef UNAUDIO_AUDIO_TYPES_H
#define UNAUDIO_AUDIO_TYPES_H

#include <cstdint>
#include <cstddef>

#ifdef _WIN32
    #define UNAUDIO_EXPORT __declspec(dllexport)
#else
    #define UNAUDIO_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Audio format description
typedef struct {
    int32_t sampleRate;    // 44100, 48000, 96000
    int32_t channels;      // 1 (Mono), 2 (Stereo), 6 (5.1), 8 (7.1)
    int32_t bitsPerSample; // 16, 24, 32
    int32_t blockAlign;    // channels * (bitsPerSample / 8)
} UNAudioFormat;

// Audio output configuration
typedef struct {
    int32_t sampleRate;
    int32_t channels;
    int32_t bufferSize;      // frames per buffer: 64, 128, 256, 512
    int32_t bufferCount;     // double/triple buffering: 2, 3, 4
    int32_t exclusiveMode;   // 1 = exclusive (WASAPI), 0 = shared
} UNAudioOutputConfig;

// Audio source handle
typedef int32_t UNAudioSourceHandle;

// Compression mode
typedef enum {
    UNAUDIO_COMPRESS_IN_MEMORY = 0,  // Compressed in memory, decode on play
    UNAUDIO_DECOMPRESS_ON_LOAD = 1,  // Decompress when loaded
    UNAUDIO_STREAMING = 2            // Stream from disk
} UNAudioCompressionMode;

// Audio state
typedef enum {
    UNAUDIO_STATE_STOPPED = 0,
    UNAUDIO_STATE_PLAYING = 1,
    UNAUDIO_STATE_PAUSED = 2
} UNAudioState;

// Result codes
typedef enum {
    UNAUDIO_OK = 0,
    UNAUDIO_ERROR_INVALID_PARAM = -1,
    UNAUDIO_ERROR_NOT_INITIALIZED = -2,
    UNAUDIO_ERROR_DECODE_FAILED = -3,
    UNAUDIO_ERROR_OUTPUT_FAILED = -4,
    UNAUDIO_ERROR_OUT_OF_MEMORY = -5,
    UNAUDIO_ERROR_FILE_NOT_FOUND = -6,
    UNAUDIO_ERROR_FORMAT_NOT_SUPPORTED = -7,
    UNAUDIO_ERROR_ALREADY_INITIALIZED = -8
} UNAudioResult;

// Audio clip info (returned to C# side)
typedef struct {
    int32_t sampleRate;
    int32_t channels;
    int32_t bitsPerSample;
    float lengthInSeconds;
    int64_t totalFrames;
    UNAudioCompressionMode compressionMode;
} UNAudioClipInfo;

#ifdef __cplusplus
}
#endif

#endif // UNAUDIO_AUDIO_TYPES_H
