# UNAudio API Reference

Complete API reference for UNAudio Unity plugin.

## Table of Contents

1. [Core API](#core-api)
2. [Data Types](#data-types)
3. [Error Codes](#error-codes)
4. [Platform-Specific Notes](#platform-specific-notes)
5. [Code Examples](#code-examples)

---

## Core API

### UNAudioEngine Class

Main interface for the UNAudio engine. All methods are static.

#### Initialize

```csharp
public static Result Initialize(int sampleRate = 48000, int channels = 2, int bufferSize = 512)
```

Initialize the audio engine with specified parameters.

**Parameters:**
- `sampleRate` (int): Sample rate in Hz. Default: 48000
  - Recommended: 44100 or 48000
  - Range: 8000 - 192000 (platform dependent)
- `channels` (int): Number of audio channels. Default: 2
  - 1 = Mono
  - 2 = Stereo
- `bufferSize` (int): Buffer size in frames. Default: 512
  - Smaller values = lower latency, higher CPU usage
  - Larger values = higher latency, lower CPU usage
  - Recommended range: 256-1024

**Returns:**
- `Result.Success` - Initialization successful
- `Result.AlreadyInitialized` - Engine already initialized
- `Result.PlatformError` - Platform-specific error
- `Result.InvalidParameter` - Invalid parameters

**Example:**
```csharp
var result = UNAudioEngine.Initialize(48000, 2, 512);
if (result == UNAudioEngine.Result.Success)
{
    Debug.Log("Audio engine initialized");
}
```

---

#### Shutdown

```csharp
public static Result Shutdown()
```

Shutdown the audio engine and release resources.

**Returns:**
- `Result.Success` - Shutdown successful
- `Result.NotInitialized` - Engine not initialized

**Example:**
```csharp
UNAudioEngine.Shutdown();
```

**Note:** Always call `Shutdown()` when done to prevent resource leaks.

---

#### Start

```csharp
public static Result Start()
```

Start audio playback. Audio callback will begin receiving calls.

**Returns:**
- `Result.Success` - Playback started
- `Result.NotInitialized` - Engine not initialized
- `Result.PlatformError` - Failed to start playback

**Example:**
```csharp
UNAudioEngine.Start();
```

---

#### Stop

```csharp
public static Result Stop()
```

Stop audio playback. Audio callback will stop receiving calls.

**Returns:**
- `Result.Success` - Playback stopped
- `Result.NotInitialized` - Engine not initialized

**Example:**
```csharp
UNAudioEngine.Stop();
```

---

#### SetVolume

```csharp
public static Result SetVolume(float volume)
```

Set master volume level.

**Parameters:**
- `volume` (float): Volume level from 0.0 (silent) to 1.0 (full)
  - Values are automatically clamped to valid range

**Returns:**
- `Result.Success` - Volume set successfully
- `Result.NotInitialized` - Engine not initialized

**Example:**
```csharp
UNAudioEngine.SetVolume(0.5f); // 50% volume
```

---

#### GetVolume

```csharp
public static float GetVolume()
```

Get current master volume level.

**Returns:**
- Current volume (0.0 to 1.0)
- 0.0 if engine not initialized

**Example:**
```csharp
float volume = UNAudioEngine.GetVolume();
Debug.Log($"Current volume: {volume * 100}%");
```

---

#### GetLatency

```csharp
public static int GetLatency()
```

Get current audio latency in milliseconds.

**Returns:**
- Latency in milliseconds
- -1 if engine not initialized

**Example:**
```csharp
int latency = UNAudioEngine.GetLatency();
Debug.Log($"Audio latency: {latency}ms");
```

**Note:** Actual latency may vary based on device and buffer size.

---

#### GetDeviceInfo

```csharp
public static Result GetDeviceInfo(out DeviceInfo info)
```

Get information about the audio device.

**Parameters:**
- `info` (out DeviceInfo): Structure to receive device information

**Returns:**
- `Result.Success` - Info retrieved successfully
- `Result.NotInitialized` - Engine not initialized
- `Result.InvalidParameter` - Invalid parameter

**Example:**
```csharp
UNAudioEngine.DeviceInfo info;
if (UNAudioEngine.GetDeviceInfo(out info) == UNAudioEngine.Result.Success)
{
    Debug.Log($"Sample Rate: {info.sampleRate}Hz");
    Debug.Log($"Channels: {info.channels}");
    Debug.Log($"Buffer Size: {info.bufferSize} frames");
    Debug.Log($"Format: {info.format}");
}
```

---

#### SetCallback

```csharp
public static Result SetCallback(AudioCallbackDelegate callback, IntPtr userData = default)
```

Set audio processing callback function.

**Parameters:**
- `callback` (AudioCallbackDelegate): Function to call for audio processing
- `userData` (IntPtr): Optional user data pointer (default: IntPtr.Zero)

**Returns:**
- `Result.Success` - Callback set successfully
- `Result.NotInitialized` - Engine not initialized

**Example:**
```csharp
private UNAudioEngine.AudioCallbackDelegate audioCallback;

void Start()
{
    audioCallback = OnAudioCallback;
    UNAudioEngine.SetCallback(audioCallback);
}

private void OnAudioCallback(IntPtr buffer, int frames, int channels, IntPtr userData)
{
    unsafe
    {
        float* samples = (float*)buffer.ToPointer();
        // Process audio here
    }
}
```

**Important:** Keep a reference to the callback delegate to prevent garbage collection!

---

#### IsPlatformSupported

```csharp
public static bool IsPlatformSupported()
```

Check if current platform is supported.

**Returns:**
- `true` if platform is Android or iOS
- `false` otherwise

**Example:**
```csharp
if (UNAudioEngine.IsPlatformSupported())
{
    UNAudioEngine.Initialize();
}
else
{
    Debug.LogWarning("UNAudio not supported on this platform");
}
```

---

#### GetPlatformName

```csharp
public static string GetPlatformName()
```

Get name of current platform.

**Returns:**
- "Android" - Android platform
- "iOS" - iOS platform
- "Unsupported" - Other platforms

**Example:**
```csharp
string platform = UNAudioEngine.GetPlatformName();
Debug.Log($"Running on: {platform}");
```

---

## Data Types

### Result Enum

```csharp
public enum Result
{
    Success = 0,              // Operation successful
    InvalidParameter = -1,     // Invalid parameter provided
    NotInitialized = -2,       // Engine not initialized
    DeviceNotFound = -3,       // Audio device not found
    AlreadyInitialized = -4,   // Engine already initialized
    PlatformError = -5         // Platform-specific error
}
```

---

### AudioFormat Enum

```csharp
public enum AudioFormat
{
    PCM16 = 0,    // 16-bit PCM
    PCM32 = 1,    // 32-bit PCM
    Float = 2     // 32-bit Float (default on Android/iOS)
}
```

---

### DeviceInfo Struct

```csharp
public struct DeviceInfo
{
    public int sampleRate;         // Sample rate in Hz
    public int channels;           // Number of channels
    public int bufferSize;         // Buffer size in frames
    public AudioFormat format;     // Audio sample format
}
```

---

### AudioCallbackDelegate

```csharp
public delegate void AudioCallbackDelegate(
    IntPtr buffer,      // Pointer to audio buffer
    int frames,         // Number of frames to process
    int channels,       // Number of channels
    IntPtr userData     // User data pointer
);
```

**Callback Notes:**
- Called from audio thread (real-time priority)
- Must be fast and deterministic
- Avoid memory allocations
- Avoid locks or blocking operations
- Process `frames * channels` samples

---

## Error Codes

| Code | Name | Description | Solution |
|------|------|-------------|----------|
| 0 | Success | Operation completed successfully | - |
| -1 | InvalidParameter | Invalid parameter value | Check parameter ranges |
| -2 | NotInitialized | Engine not initialized | Call Initialize() first |
| -3 | DeviceNotFound | Audio device not available | Check device settings |
| -4 | AlreadyInitialized | Engine already initialized | Call Shutdown() first |
| -5 | PlatformError | Platform-specific error | Check platform logs |

---

## Platform-Specific Notes

### Android

- **Minimum API Level:** 26 (Android 8.0)
- **Audio API:** AAudio
- **Default Format:** Float32
- **Latency Target:** <15ms
- **Buffer Size:** 256-1024 frames recommended

**Platform Checks:**
```csharp
#if UNITY_ANDROID
    // Android-specific code
#endif
```

### iOS

- **Minimum Version:** iOS 11.0
- **Audio API:** CoreAudio (AudioUnit)
- **Default Format:** Float32
- **Latency Target:** <8ms
- **Buffer Size:** 256-512 frames recommended

**Platform Checks:**
```csharp
#if UNITY_IOS
    // iOS-specific code
#endif
```

---

## Code Examples

### Basic Initialization and Playback

```csharp
using UnityEngine;
using UNAudio;

public class BasicAudio : MonoBehaviour
{
    void Start()
    {
        // Initialize
        var result = UNAudioEngine.Initialize(48000, 2, 512);
        if (result != UNAudioEngine.Result.Success)
        {
            Debug.LogError($"Failed to initialize: {result}");
            return;
        }
        
        // Get device info
        UNAudioEngine.DeviceInfo info;
        UNAudioEngine.GetDeviceInfo(out info);
        Debug.Log($"Audio: {info.sampleRate}Hz, {info.channels}ch");
        
        // Start playback
        UNAudioEngine.Start();
    }
    
    void OnDestroy()
    {
        UNAudioEngine.Stop();
        UNAudioEngine.Shutdown();
    }
}
```

### Audio Processing with Callback

```csharp
using UnityEngine;
using UNAudio;

public class AudioProcessor : MonoBehaviour
{
    private UNAudioEngine.AudioCallbackDelegate callback;
    private double phase = 0;
    private float frequency = 440f;
    
    void Start()
    {
        UNAudioEngine.Initialize(48000, 2, 512);
        
        callback = OnAudioCallback;
        UNAudioEngine.SetCallback(callback);
        
        UNAudioEngine.Start();
    }
    
    void OnDestroy()
    {
        UNAudioEngine.Stop();
        UNAudioEngine.Shutdown();
    }
    
    private void OnAudioCallback(IntPtr buffer, int frames, int channels, IntPtr userData)
    {
        unsafe
        {
            float* samples = (float*)buffer.ToPointer();
            double phaseIncrement = 2.0 * Mathf.PI * frequency / 48000.0;
            
            for (int i = 0; i < frames; i++)
            {
                float sample = Mathf.Sin((float)phase) * 0.3f;
                
                for (int ch = 0; ch < channels; ch++)
                {
                    samples[i * channels + ch] = sample;
                }
                
                phase += phaseIncrement;
                if (phase >= 2.0 * Mathf.PI)
                    phase -= 2.0 * Mathf.PI;
            }
        }
    }
}
```

### Volume Control

```csharp
using UnityEngine;
using UNAudio;

public class VolumeControl : MonoBehaviour
{
    [Range(0f, 1f)]
    public float volume = 0.5f;
    
    void Start()
    {
        UNAudioEngine.Initialize();
        UNAudioEngine.SetVolume(volume);
        UNAudioEngine.Start();
    }
    
    void Update()
    {
        UNAudioEngine.SetVolume(volume);
    }
    
    void OnDestroy()
    {
        UNAudioEngine.Stop();
        UNAudioEngine.Shutdown();
    }
}
```

### Latency Monitoring

```csharp
using UnityEngine;
using UNAudio;

public class LatencyMonitor : MonoBehaviour
{
    void Start()
    {
        UNAudioEngine.Initialize(48000, 2, 256);
        UNAudioEngine.Start();
        
        InvokeRepeating("CheckLatency", 1f, 1f);
    }
    
    void CheckLatency()
    {
        int latency = UNAudioEngine.GetLatency();
        Debug.Log($"Current latency: {latency}ms");
        
        if (latency > 20)
        {
            Debug.LogWarning("High latency detected!");
        }
    }
    
    void OnDestroy()
    {
        CancelInvoke();
        UNAudioEngine.Stop();
        UNAudioEngine.Shutdown();
    }
}
```

---

## Best Practices

1. **Always check return codes** - Don't ignore error codes
2. **Keep callbacks fast** - Audio callback runs on real-time thread
3. **Avoid allocations** - No `new`, no LINQ in callbacks
4. **Test on real devices** - Emulators don't reflect real performance
5. **Monitor latency** - Check actual latency matches expectations
6. **Handle lifecycle** - Properly initialize and shutdown
7. **Platform checks** - Use `IsPlatformSupported()` before initialization

---

## Troubleshooting

### "NotInitialized" Error

```csharp
// ❌ Wrong
UNAudioEngine.Start();

// ✅ Correct
UNAudioEngine.Initialize();
UNAudioEngine.Start();
```

### High CPU Usage

```csharp
// ❌ Wrong - Allocations in callback
private void OnAudioCallback(IntPtr buffer, int frames, int channels, IntPtr userData)
{
    float[] tempBuffer = new float[frames]; // DON'T!
}

// ✅ Correct - Direct buffer access
private void OnAudioCallback(IntPtr buffer, int frames, int channels, IntPtr userData)
{
    unsafe
    {
        float* samples = (float*)buffer.ToPointer();
        // Process directly
    }
}
```

### Callback Not Being Called

```csharp
// ❌ Wrong - Callback garbage collected
void Start()
{
    UNAudioEngine.SetCallback(OnAudioCallback);
}

// ✅ Correct - Keep reference
private UNAudioEngine.AudioCallbackDelegate callback;

void Start()
{
    callback = OnAudioCallback;
    UNAudioEngine.SetCallback(callback);
}
```

---

For more information, see:
- [Android Setup Guide](AndroidSetup.md)
- [iOS Setup Guide](iOSSetup.md)
- [Main README](../README.md)
