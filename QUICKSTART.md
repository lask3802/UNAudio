# Quick Start Guide

Get started with UNAudio in 5 minutes!

## Installation

### Option 1: Unity Package Manager (Recommended)

1. Open Unity (2020.3 LTS or later)
2. Go to **Window → Package Manager**
3. Click **+** → **Add package from git URL**
4. Enter: `https://github.com/lask3802/UNAudio.git`
5. Click **Add**

### Option 2: Download Release

1. Download the latest `.unitypackage` from [Releases](https://github.com/lask3802/UNAudio/releases)
2. In Unity: **Assets → Import Package → Custom Package**
3. Select the downloaded file
4. Click **Import**

## Platform Setup

### Android

1. **Switch to Android platform**: 
   - **File → Build Settings → Android → Switch Platform**

2. **Configure Player Settings**:
   - **Edit → Project Settings → Player → Android**
   - Minimum API Level: **Android 8.0 (API 26)** or higher
   - Scripting Backend: **IL2CPP** (recommended)
   - Target Architectures: Enable **ARM64**

3. **Done!** You're ready to build.

### iOS

1. **Switch to iOS platform**: 
   - **File → Build Settings → iOS → Switch Platform**

2. **Configure Player Settings**:
   - **Edit → Project Settings → Player → iOS**
   - Target minimum iOS Version: **11.0**
   - Architecture: **ARM64**

3. **Done!** You're ready to build.

## Your First Audio App

### Step 1: Create a new C# script

Create a new script called `MyAudioApp.cs`:

```csharp
using UnityEngine;
using UNAudio;

public class MyAudioApp : MonoBehaviour
{
    private UNAudioEngine.AudioCallbackDelegate audioCallback;
    private double phase = 0;

    void Start()
    {
        // Check if platform is supported
        if (!UNAudioEngine.IsPlatformSupported())
        {
            Debug.LogWarning("UNAudio not supported on this platform");
            return;
        }

        // Initialize audio engine
        var result = UNAudioEngine.Initialize(
            sampleRate: 48000,
            channels: 2,
            bufferSize: 512
        );

        if (result != UNAudioEngine.Result.Success)
        {
            Debug.LogError($"Failed to initialize UNAudio: {result}");
            return;
        }

        Debug.Log("UNAudio initialized successfully!");

        // Get device info
        UNAudioEngine.DeviceInfo info;
        UNAudioEngine.GetDeviceInfo(out info);
        Debug.Log($"Audio: {info.sampleRate}Hz, {info.channels}ch, {info.bufferSize} frames");
        Debug.Log($"Latency: {UNAudioEngine.GetLatency()}ms");

        // Set callback
        audioCallback = OnAudioCallback;
        UNAudioEngine.SetCallback(audioCallback);

        // Start playback
        UNAudioEngine.Start();
        Debug.Log("Audio playback started!");
    }

    void OnDestroy()
    {
        UNAudioEngine.Stop();
        UNAudioEngine.Shutdown();
        Debug.Log("UNAudio shutdown");
    }

    // Audio callback - generates a 440Hz sine wave
    private void OnAudioCallback(System.IntPtr buffer, int frames, int channels, System.IntPtr userData)
    {
        unsafe
        {
            float* samples = (float*)buffer.ToPointer();
            double frequency = 440.0; // A4 note
            double phaseIncrement = 2.0 * Mathf.PI * frequency / 48000.0;

            for (int i = 0; i < frames; i++)
            {
                float sample = Mathf.Sin((float)phase) * 0.3f; // 30% volume

                // Write to all channels
                for (int ch = 0; ch < channels; ch++)
                {
                    samples[i * channels + ch] = sample;
                }

                phase += phaseIncrement;
                
                // Keep phase in valid range
                if (phase >= 2.0 * Mathf.PI)
                {
                    phase -= 2.0 * Mathf.PI;
                }
            }
        }
    }
}
```

### Step 2: Add script to a GameObject

1. Create a new empty GameObject in your scene
2. Add the `MyAudioApp` component to it
3. Save the scene

### Step 3: Build and run!

#### For Android:
1. Connect your Android device (API 26+)
2. **File → Build Settings → Build and Run**
3. You should hear a 440Hz tone!

#### For iOS:
1. **File → Build Settings → Build**
2. Open the generated Xcode project
3. Connect your iOS device
4. Run from Xcode
5. You should hear a 440Hz tone!

## What's Next?

### Learn More

- 📖 [API Reference](Documentation/APIReference.md) - Complete API documentation
- 🤖 [Android Setup Guide](Documentation/AndroidSetup.md) - Android-specific details
- 🍎 [iOS Setup Guide](Documentation/iOSSetup.md) - iOS-specific details
- 📝 [Full README](README.md) - Architecture and features

### Examples

Check out the `Examples/` folder for more:
- Sine wave generator
- Volume control
- Latency monitoring

### Common Issues

**No sound on Android?**
- Check device Android version (must be 8.0+)
- Check volume is not muted
- View logs: `adb logcat | grep UNAudio`

**No sound on iOS?**
- Check iOS version (must be 11.0+)
- Check silent switch is off
- View Xcode console for errors

**High latency?**
- Try reducing buffer size to 256
- Close other audio apps
- Test on newer device

### Get Help

- 🐛 [Report Issues](https://github.com/lask3802/UNAudio/issues)
- 💬 [Discussions](https://github.com/lask3802/UNAudio/discussions)
- 📧 Email: lask380@hotmail.com

## Tips

✅ **DO:**
- Always check `IsPlatformSupported()` before initializing
- Keep audio callbacks fast and simple
- Test on real devices, not emulators
- Call `Shutdown()` when done

❌ **DON'T:**
- Allocate memory in audio callbacks
- Use heavy computations in callbacks
- Forget to keep callback delegate reference
- Ignore error return codes

---

Happy coding! 🎵
