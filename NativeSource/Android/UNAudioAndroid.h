#ifndef UNAUDIO_ANDROID_H
#define UNAUDIO_ANDROID_H

#ifdef __ANDROID__

#include "../Core/UNAudioCore.h"
#include <aaudio/AAudio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Android-specific implementation using AAudio API
typedef struct {
    AAudioStream* stream;
    UNAudioCallback callback;
    void* userData;
    float volume;
    int sampleRate;
    int channels;
    int bufferSize;
    bool isInitialized;
    bool isRunning;
} UNAudioAndroidContext;

// Android-specific functions
UNAudioResult UNAudio_Android_Initialize(int sampleRate, int channels, int bufferSize);
UNAudioResult UNAudio_Android_Shutdown();
UNAudioResult UNAudio_Android_Start();
UNAudioResult UNAudio_Android_Stop();

#ifdef __cplusplus
}
#endif

#endif // __ANDROID__

#endif // UNAUDIO_ANDROID_H
