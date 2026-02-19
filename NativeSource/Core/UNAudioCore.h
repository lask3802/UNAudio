#ifndef UNAUDIO_CORE_H
#define UNAUDIO_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

// Platform detection
#if defined(__ANDROID__)
    #define UNAUDIO_ANDROID 1
    #define UNAUDIO_EXPORT __attribute__((visibility("default")))
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IPHONE
        #define UNAUDIO_IOS 1
        #define UNAUDIO_EXPORT __attribute__((visibility("default")))
    #endif
#elif defined(_WIN32) || defined(_WIN64)
    #define UNAUDIO_WINDOWS 1
    #define UNAUDIO_EXPORT __declspec(dllexport)
#else
    #define UNAUDIO_EXPORT
#endif

// Audio configuration
#define UNAUDIO_MAX_CHANNELS 2
#define UNAUDIO_DEFAULT_SAMPLE_RATE 48000
#define UNAUDIO_DEFAULT_BUFFER_SIZE 512

// Return codes
typedef enum {
    UNAUDIO_SUCCESS = 0,
    UNAUDIO_ERROR_INVALID_PARAMETER = -1,
    UNAUDIO_ERROR_NOT_INITIALIZED = -2,
    UNAUDIO_ERROR_DEVICE_NOT_FOUND = -3,
    UNAUDIO_ERROR_ALREADY_INITIALIZED = -4,
    UNAUDIO_ERROR_PLATFORM_ERROR = -5
} UNAudioResult;

// Audio format
typedef enum {
    UNAUDIO_FORMAT_PCM_16 = 0,
    UNAUDIO_FORMAT_PCM_32 = 1,
    UNAUDIO_FORMAT_FLOAT = 2
} UNAudioFormat;

// Audio device info
typedef struct {
    int sampleRate;
    int channels;
    int bufferSize;
    UNAudioFormat format;
} UNAudioDeviceInfo;

// Core API
UNAUDIO_EXPORT UNAudioResult UNAudio_Initialize(int sampleRate, int channels, int bufferSize);
UNAUDIO_EXPORT UNAudioResult UNAudio_Shutdown();
UNAUDIO_EXPORT UNAudioResult UNAudio_Start();
UNAUDIO_EXPORT UNAudioResult UNAudio_Stop();
UNAUDIO_EXPORT UNAudioResult UNAudio_GetDeviceInfo(UNAudioDeviceInfo* info);
UNAUDIO_EXPORT UNAudioResult UNAudio_SetVolume(float volume);
UNAUDIO_EXPORT float UNAudio_GetVolume();
UNAUDIO_EXPORT int UNAudio_GetLatency(); // Returns latency in milliseconds

// Audio buffer callback
typedef void (*UNAudioCallback)(float* buffer, int frames, int channels, void* userData);
UNAUDIO_EXPORT UNAudioResult UNAudio_SetCallback(UNAudioCallback callback, void* userData);

#ifdef __cplusplus
}
#endif

#endif // UNAUDIO_CORE_H
