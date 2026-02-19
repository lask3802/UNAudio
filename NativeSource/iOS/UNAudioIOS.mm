#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE

#include "UNAudioIOS.h"
#include <string.h>
#include <os/log.h>

static UNAudioIOSContext g_context = {0};

// AudioUnit render callback
static OSStatus audioCallback(
    void *inRefCon,
    AudioUnitRenderActionFlags *ioActionFlags,
    const AudioTimeStamp *inTimeStamp,
    UInt32 inBusNumber,
    UInt32 inNumberFrames,
    AudioBufferList *ioData) {
    
    UNAudioIOSContext* ctx = (UNAudioIOSContext*)inRefCon;
    
    if (ctx->callback && ioData) {
        for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
            float* buffer = (float*)ioData->mBuffers[i].mData;
            UInt32 frames = inNumberFrames;
            
            // Clear buffer
            memset(buffer, 0, frames * ctx->channels * sizeof(float));
            
            // Call user callback
            ctx->callback(buffer, frames, ctx->channels, ctx->userData);
            
            // Apply volume
            if (ctx->volume != 1.0f) {
                int totalSamples = frames * ctx->channels;
                for (int j = 0; j < totalSamples; j++) {
                    buffer[j] *= ctx->volume;
                }
            }
        }
    }
    
    return noErr;
}

UNAudioResult UNAudio_iOS_Initialize(int sampleRate, int channels, int bufferSize) {
    if (g_context.isInitialized) {
        os_log_error(OS_LOG_DEFAULT, "UNAudio already initialized");
        return UNAUDIO_ERROR_ALREADY_INITIALIZED;
    }
    
    os_log_info(OS_LOG_DEFAULT, "Initializing UNAudio: SR=%d, CH=%d, BS=%d", 
                sampleRate, channels, bufferSize);
    
    OSStatus status;
    
    // Describe audio component
    AudioComponentDescription desc;
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;
    
    // Get component
    AudioComponent component = AudioComponentFindNext(NULL, &desc);
    if (!component) {
        os_log_error(OS_LOG_DEFAULT, "Failed to find audio component");
        return UNAUDIO_ERROR_PLATFORM_ERROR;
    }
    
    // Create audio unit
    status = AudioComponentInstanceNew(component, &g_context.audioUnit);
    if (status != noErr) {
        os_log_error(OS_LOG_DEFAULT, "Failed to create audio unit: %d", status);
        return UNAUDIO_ERROR_PLATFORM_ERROR;
    }
    
    // Configure audio format
    AudioStreamBasicDescription format;
    format.mSampleRate = sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    format.mChannelsPerFrame = channels;
    format.mBitsPerChannel = 32;
    format.mBytesPerPacket = sizeof(float);
    format.mBytesPerFrame = sizeof(float);
    format.mFramesPerPacket = 1;
    
    status = AudioUnitSetProperty(g_context.audioUnit,
                                  kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input,
                                  0,
                                  &format,
                                  sizeof(format));
    
    if (status != noErr) {
        os_log_error(OS_LOG_DEFAULT, "Failed to set stream format: %d", status);
        AudioComponentInstanceDispose(g_context.audioUnit);
        return UNAUDIO_ERROR_PLATFORM_ERROR;
    }
    
    // Set buffer size
    Float32 bufferDuration = (Float32)bufferSize / (Float32)sampleRate;
    AudioSessionSetProperty(kAudioSessionProperty_PreferredHardwareIOBufferDuration,
                           sizeof(bufferDuration),
                           &bufferDuration);
    
    // Set render callback
    AURenderCallbackStruct callbackStruct;
    callbackStruct.inputProc = audioCallback;
    callbackStruct.inputProcRefCon = &g_context;
    
    status = AudioUnitSetProperty(g_context.audioUnit,
                                  kAudioUnitProperty_SetRenderCallback,
                                  kAudioUnitScope_Input,
                                  0,
                                  &callbackStruct,
                                  sizeof(callbackStruct));
    
    if (status != noErr) {
        os_log_error(OS_LOG_DEFAULT, "Failed to set render callback: %d", status);
        AudioComponentInstanceDispose(g_context.audioUnit);
        return UNAUDIO_ERROR_PLATFORM_ERROR;
    }
    
    // Initialize audio unit
    status = AudioUnitInitialize(g_context.audioUnit);
    if (status != noErr) {
        os_log_error(OS_LOG_DEFAULT, "Failed to initialize audio unit: %d", status);
        AudioComponentInstanceDispose(g_context.audioUnit);
        return UNAUDIO_ERROR_PLATFORM_ERROR;
    }
    
    g_context.sampleRate = sampleRate;
    g_context.channels = channels;
    g_context.bufferSize = bufferSize;
    g_context.volume = 1.0f;
    g_context.isInitialized = true;
    g_context.isRunning = false;
    
    os_log_info(OS_LOG_DEFAULT, "UNAudio initialized successfully");
    
    return UNAUDIO_SUCCESS;
}

UNAudioResult UNAudio_iOS_Shutdown() {
    if (!g_context.isInitialized) {
        return UNAUDIO_ERROR_NOT_INITIALIZED;
    }
    
    os_log_info(OS_LOG_DEFAULT, "Shutting down UNAudio");
    
    if (g_context.isRunning) {
        UNAudio_iOS_Stop();
    }
    
    if (g_context.audioUnit) {
        AudioUnitUninitialize(g_context.audioUnit);
        AudioComponentInstanceDispose(g_context.audioUnit);
        g_context.audioUnit = NULL;
    }
    
    memset(&g_context, 0, sizeof(UNAudioIOSContext));
    
    os_log_info(OS_LOG_DEFAULT, "UNAudio shutdown complete");
    
    return UNAUDIO_SUCCESS;
}

UNAudioResult UNAudio_iOS_Start() {
    if (!g_context.isInitialized) {
        os_log_error(OS_LOG_DEFAULT, "UNAudio not initialized");
        return UNAUDIO_ERROR_NOT_INITIALIZED;
    }
    
    if (g_context.isRunning) {
        os_log_info(OS_LOG_DEFAULT, "UNAudio already running");
        return UNAUDIO_SUCCESS;
    }
    
    os_log_info(OS_LOG_DEFAULT, "Starting UNAudio");
    
    OSStatus status = AudioOutputUnitStart(g_context.audioUnit);
    if (status != noErr) {
        os_log_error(OS_LOG_DEFAULT, "Failed to start audio unit: %d", status);
        return UNAUDIO_ERROR_PLATFORM_ERROR;
    }
    
    g_context.isRunning = true;
    os_log_info(OS_LOG_DEFAULT, "UNAudio started successfully");
    
    return UNAUDIO_SUCCESS;
}

UNAudioResult UNAudio_iOS_Stop() {
    if (!g_context.isInitialized) {
        return UNAUDIO_ERROR_NOT_INITIALIZED;
    }
    
    if (!g_context.isRunning) {
        return UNAUDIO_SUCCESS;
    }
    
    os_log_info(OS_LOG_DEFAULT, "Stopping UNAudio");
    
    OSStatus status = AudioOutputUnitStop(g_context.audioUnit);
    if (status != noErr) {
        os_log_error(OS_LOG_DEFAULT, "Failed to stop audio unit: %d", status);
        return UNAUDIO_ERROR_PLATFORM_ERROR;
    }
    
    g_context.isRunning = false;
    os_log_info(OS_LOG_DEFAULT, "UNAudio stopped successfully");
    
    return UNAUDIO_SUCCESS;
}

// Implementation of core API for iOS
UNAudioResult UNAudio_Initialize(int sampleRate, int channels, int bufferSize) {
    return UNAudio_iOS_Initialize(sampleRate, channels, bufferSize);
}

UNAudioResult UNAudio_Shutdown() {
    return UNAudio_iOS_Shutdown();
}

UNAudioResult UNAudio_Start() {
    return UNAudio_iOS_Start();
}

UNAudioResult UNAudio_Stop() {
    return UNAudio_iOS_Stop();
}

UNAudioResult UNAudio_GetDeviceInfo(UNAudioDeviceInfo* info) {
    if (!g_context.isInitialized) {
        return UNAUDIO_ERROR_NOT_INITIALIZED;
    }
    
    if (!info) {
        return UNAUDIO_ERROR_INVALID_PARAMETER;
    }
    
    info->sampleRate = g_context.sampleRate;
    info->channels = g_context.channels;
    info->bufferSize = g_context.bufferSize;
    info->format = UNAUDIO_FORMAT_FLOAT;
    
    return UNAUDIO_SUCCESS;
}

UNAudioResult UNAudio_SetVolume(float volume) {
    if (!g_context.isInitialized) {
        return UNAUDIO_ERROR_NOT_INITIALIZED;
    }
    
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    
    g_context.volume = volume;
    return UNAUDIO_SUCCESS;
}

float UNAudio_GetVolume() {
    return g_context.volume;
}

int UNAudio_GetLatency() {
    if (!g_context.isInitialized) {
        return -1;
    }
    
    // Calculate latency based on buffer size
    int latencyMs = (g_context.bufferSize * 1000) / g_context.sampleRate;
    
    return latencyMs;
}

UNAudioResult UNAudio_SetCallback(UNAudioCallback callback, void* userData) {
    if (!g_context.isInitialized) {
        return UNAUDIO_ERROR_NOT_INITIALIZED;
    }
    
    g_context.callback = callback;
    g_context.userData = userData;
    
    return UNAUDIO_SUCCESS;
}

#endif // TARGET_OS_IPHONE
#endif // __APPLE__
