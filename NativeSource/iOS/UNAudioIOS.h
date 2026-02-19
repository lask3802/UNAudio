#ifndef UNAUDIO_IOS_H
#define UNAUDIO_IOS_H

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE

#include "../Core/UNAudioCore.h"
#include <AudioToolbox/AudioToolbox.h>

#ifdef __cplusplus
extern "C" {
#endif

// iOS-specific implementation using AudioUnit
typedef struct {
    AudioComponentInstance audioUnit;
    UNAudioCallback callback;
    void* userData;
    float volume;
    int sampleRate;
    int channels;
    int bufferSize;
    bool isInitialized;
    bool isRunning;
} UNAudioIOSContext;

// iOS-specific functions
UNAudioResult UNAudio_iOS_Initialize(int sampleRate, int channels, int bufferSize);
UNAudioResult UNAudio_iOS_Shutdown();
UNAudioResult UNAudio_iOS_Start();
UNAudioResult UNAudio_iOS_Stop();

#ifdef __cplusplus
}
#endif

#endif // TARGET_OS_IPHONE
#endif // __APPLE__

#endif // UNAUDIO_IOS_H
