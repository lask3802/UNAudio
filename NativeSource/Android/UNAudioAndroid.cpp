#ifdef __ANDROID__

#include "UNAudioAndroid.h"
#include <android/log.h>
#include <string.h>

#define LOG_TAG "UNAudio"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static UNAudioAndroidContext g_context = {0};

// AAudio callback
static aaudio_data_callback_result_t audioCallback(
    AAudioStream *stream,
    void *userData,
    void *audioData,
    int32_t numFrames) {
    
    UNAudioAndroidContext* ctx = (UNAudioAndroidContext*)userData;
    
    if (ctx->callback && audioData) {
        float* buffer = (float*)audioData;
        
        // Clear buffer
        memset(buffer, 0, numFrames * ctx->channels * sizeof(float));
        
        // Call user callback
        ctx->callback(buffer, numFrames, ctx->channels, ctx->userData);
        
        // Apply volume
        if (ctx->volume != 1.0f) {
            int totalSamples = numFrames * ctx->channels;
            for (int i = 0; i < totalSamples; i++) {
                buffer[i] *= ctx->volume;
            }
        }
    }
    
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

UNAudioResult UNAudio_Android_Initialize(int sampleRate, int channels, int bufferSize) {
    if (g_context.isInitialized) {
        LOGE("UNAudio already initialized");
        return UNAUDIO_ERROR_ALREADY_INITIALIZED;
    }
    
    LOGD("Initializing UNAudio: SR=%d, CH=%d, BS=%d", sampleRate, channels, bufferSize);
    
    AAudioStreamBuilder* builder;
    aaudio_result_t result = AAudio_createStreamBuilder(&builder);
    
    if (result != AAUDIO_OK) {
        LOGE("Failed to create stream builder: %s", AAudio_convertResultToText(result));
        return UNAUDIO_ERROR_PLATFORM_ERROR;
    }
    
    // Configure stream
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(builder, channels);
    AAudioStreamBuilder_setSampleRate(builder, sampleRate);
    AAudioStreamBuilder_setBufferCapacityInFrames(builder, bufferSize * 2);
    AAudioStreamBuilder_setDataCallback(builder, audioCallback, &g_context);
    
    // Create stream
    result = AAudioStreamBuilder_openStream(builder, &g_context.stream);
    AAudioStreamBuilder_delete(builder);
    
    if (result != AAUDIO_OK) {
        LOGE("Failed to open stream: %s", AAudio_convertResultToText(result));
        return UNAUDIO_ERROR_PLATFORM_ERROR;
    }
    
    // Get actual configuration
    g_context.sampleRate = AAudioStream_getSampleRate(g_context.stream);
    g_context.channels = AAudioStream_getChannelCount(g_context.stream);
    g_context.bufferSize = AAudioStream_getBufferCapacityInFrames(g_context.stream);
    g_context.volume = 1.0f;
    g_context.isInitialized = true;
    g_context.isRunning = false;
    
    LOGD("UNAudio initialized successfully");
    LOGD("Actual config: SR=%d, CH=%d, BS=%d", 
         g_context.sampleRate, g_context.channels, g_context.bufferSize);
    
    return UNAUDIO_SUCCESS;
}

UNAudioResult UNAudio_Android_Shutdown() {
    if (!g_context.isInitialized) {
        return UNAUDIO_ERROR_NOT_INITIALIZED;
    }
    
    LOGD("Shutting down UNAudio");
    
    if (g_context.isRunning) {
        UNAudio_Android_Stop();
    }
    
    if (g_context.stream) {
        AAudioStream_close(g_context.stream);
        g_context.stream = NULL;
    }
    
    memset(&g_context, 0, sizeof(UNAudioAndroidContext));
    
    LOGD("UNAudio shutdown complete");
    return UNAUDIO_SUCCESS;
}

UNAudioResult UNAudio_Android_Start() {
    if (!g_context.isInitialized) {
        LOGE("UNAudio not initialized");
        return UNAUDIO_ERROR_NOT_INITIALIZED;
    }
    
    if (g_context.isRunning) {
        LOGD("UNAudio already running");
        return UNAUDIO_SUCCESS;
    }
    
    LOGD("Starting UNAudio");
    
    aaudio_result_t result = AAudioStream_requestStart(g_context.stream);
    if (result != AAUDIO_OK) {
        LOGE("Failed to start stream: %s", AAudio_convertResultToText(result));
        return UNAUDIO_ERROR_PLATFORM_ERROR;
    }
    
    g_context.isRunning = true;
    LOGD("UNAudio started successfully");
    
    return UNAUDIO_SUCCESS;
}

UNAudioResult UNAudio_Android_Stop() {
    if (!g_context.isInitialized) {
        return UNAUDIO_ERROR_NOT_INITIALIZED;
    }
    
    if (!g_context.isRunning) {
        return UNAUDIO_SUCCESS;
    }
    
    LOGD("Stopping UNAudio");
    
    aaudio_result_t result = AAudioStream_requestStop(g_context.stream);
    if (result != AAUDIO_OK) {
        LOGE("Failed to stop stream: %s", AAudio_convertResultToText(result));
        return UNAUDIO_ERROR_PLATFORM_ERROR;
    }
    
    g_context.isRunning = false;
    LOGD("UNAudio stopped successfully");
    
    return UNAUDIO_SUCCESS;
}

// Implementation of core API for Android
UNAudioResult UNAudio_Initialize(int sampleRate, int channels, int bufferSize) {
    return UNAudio_Android_Initialize(sampleRate, channels, bufferSize);
}

UNAudioResult UNAudio_Shutdown() {
    return UNAudio_Android_Shutdown();
}

UNAudioResult UNAudio_Start() {
    return UNAudio_Android_Start();
}

UNAudioResult UNAudio_Stop() {
    return UNAudio_Android_Stop();
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
    if (!g_context.isInitialized || !g_context.stream) {
        return -1;
    }
    
    int32_t framesPerBurst = AAudioStream_getFramesPerBurst(g_context.stream);
    int32_t sampleRate = g_context.sampleRate;
    
    // Calculate latency in milliseconds
    int latencyMs = (framesPerBurst * 1000) / sampleRate;
    
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

#endif // __ANDROID__
