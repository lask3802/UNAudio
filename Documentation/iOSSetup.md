# iOS Setup Guide for UNAudio

## Prerequisites

- Unity 2020.3 LTS or later
- macOS with Xcode 13.0 or later
- iOS deployment target: iOS 11.0 or higher
- Valid Apple Developer account (for device testing)

## Project Configuration

### 1. Unity Player Settings

Open **Edit → Project Settings → Player → iOS**:

1. **Other Settings**:
   - Target minimum iOS Version: **11.0**
   - Architecture: **ARM64** (required for iOS 11+)
   - Target SDK: **Device SDK**
   - Scripting Backend: **IL2CPP** (required for iOS)

2. **Optimization**:
   - Script Call Optimization: **Fast but no exceptions**
   - Enable Bitcode: **No** (for faster builds)

### 2. Required Frameworks

UNAudio automatically links the required frameworks. If building manually:

- AudioToolbox.framework
- AVFoundation.framework
- Foundation.framework

### 3. Build from Unity

1. **File → Build Settings**
2. Switch Platform to **iOS**
3. Click **Build** or **Build and Run**
4. Choose output folder (e.g., `Build/iOS`)
5. Unity generates Xcode project

### 4. Xcode Configuration

Open the generated Xcode project:

```bash
cd Build/iOS
open Unity-iPhone.xcodeproj
```

#### Project Settings

1. **General Tab**:
   - Deployment Target: **11.0**
   - Signing: Select your development team
   - Bundle Identifier: Match Unity settings

2. **Build Settings**:
   - Enable Bitcode: **No**
   - Optimization Level: **-O3** (for Release)
   - C++ Language Dialect: **C++11**
   - Dead Code Stripping: **Yes**

3. **Build Phases**:
   - Verify `libUNAudio.a` is in **Link Binary With Libraries**
   - Verify frameworks are linked:
     - AudioToolbox.framework
     - AVFoundation.framework

### 5. Info.plist Configuration

Add audio background capability if needed:

```xml
<key>UIBackgroundModes</key>
<array>
    <string>audio</string>
</array>

<key>NSMicrophoneUsageDescription</key>
<string>This app requires microphone access for audio features</string>
```

### 6. Audio Session Configuration (Optional)

For advanced audio session control, you may configure the audio session category in Objective-C:

```objective-c
// In your app initialization code
#import <AVFoundation/AVFoundation.h>

AVAudioSession *session = [AVAudioSession sharedInstance];
NSError *error = nil;

[session setCategory:AVAudioSessionCategoryPlayback
         withOptions:AVAudioSessionCategoryOptionMixWithOthers
               error:&error];

[session setPreferredSampleRate:48000.0 error:&error];
[session setPreferredIOBufferDuration:0.005 error:&error]; // 5ms

[session setActive:YES error:&error];
```

However, UNAudio handles this automatically in most cases.

### 7. Testing on Device

#### Development Build

1. Connect iOS device via USB
2. In Xcode: Select your device from target dropdown
3. Click **Run** (▶️) or press **Cmd+R**
4. App installs and launches on device

#### Check Console Logs

In Xcode, open **Debug Area** (Cmd+Shift+Y) to see logs:

Expected output:
```
UNAudio: Initializing UNAudio: SR=48000, CH=2, BS=512
UNAudio: UNAudio initialized successfully
UNAudio: Starting UNAudio
UNAudio: UNAudio started successfully
```

#### Monitor Performance

Use **Instruments** for detailed profiling:

1. Xcode → **Product → Profile** (Cmd+I)
2. Choose **Time Profiler** or **Core Audio** template
3. Analyze CPU usage and audio performance

## Troubleshooting

### Issue: "Symbol not found: _UNAudio_Initialize"

**Solution**:
1. Clean build folder: **Product → Clean Build Folder** (Cmd+Shift+K)
2. Verify `libUNAudio.a` is in **Link Binary With Libraries**
3. Check architecture matches (ARM64)
4. Rebuild native library if needed

### Issue: "Failed to initialize audio unit"

**Solution**:
1. Check iOS version (must be 11.0+)
2. Verify audio session is not being used by another component
3. Check for audio interruptions (phone calls, etc.)
4. Review console logs for specific error codes

### Issue: High Latency (>15ms)

**Possible causes**:
1. Buffer size too large
2. Device limitations
3. Audio session not optimized
4. Background apps using audio

**Solutions**:
```csharp
// Try smaller buffer size
UNAudioEngine.Initialize(48000, 2, 256);

// Check actual latency
int latency = UNAudioEngine.GetLatency();
Debug.Log($"Latency: {latency}ms");
```

### Issue: Audio Glitches/Clicks

**Solutions**:
1. Increase buffer size (512 frames)
2. Reduce CPU usage in audio callback
3. Avoid memory allocations in callback
4. Profile with Instruments
5. Check for thread priority issues

### Issue: Build Fails in Xcode

**Common errors**:

1. **"Framework not found"**:
   - Add missing frameworks in **Build Phases → Link Binary With Libraries**

2. **"Architecture arm64 not supported"**:
   - Check deployment target is 11.0+
   - Verify Architecture is set to ARM64

3. **"Undefined symbols"**:
   - Clean build folder
   - Check C++ language dialect is C++11
   - Verify all source files are included

## Performance Optimization

### Recommended Settings

```csharp
// For low latency (gaming, music apps)
UNAudioEngine.Initialize(
    sampleRate: 48000,
    channels: 2,
    bufferSize: 256  // ~5ms latency
);

// For stability (streaming, playback)
UNAudioEngine.Initialize(
    sampleRate: 48000,
    channels: 2,
    bufferSize: 512  // ~10ms latency
);
```

### Audio Callback Best Practices

```csharp
// ✅ Good: Fast, no allocations
private void OnAudioCallback(IntPtr buffer, int frames, int channels, IntPtr userData)
{
    unsafe
    {
        float* samples = (float*)buffer.ToPointer();
        // Direct buffer manipulation
        for (int i = 0; i < frames * channels; i++)
        {
            samples[i] = GenerateSample();
        }
    }
}

// ❌ Bad: Allocations in callback
private void OnAudioCallback(IntPtr buffer, int frames, int channels, IntPtr userData)
{
    float[] tempBuffer = new float[frames * channels]; // DON'T DO THIS
    // Process...
    Marshal.Copy(tempBuffer, 0, buffer, tempBuffer.Length);
}
```

### Testing Latency

```csharp
void Start()
{
    var result = UNAudioEngine.Initialize(48000, 2, 256);
    
    if (result == UNAudioEngine.Result.Success)
    {
        UNAudioEngine.DeviceInfo info;
        UNAudioEngine.GetDeviceInfo(out info);
        
        int latency = UNAudioEngine.GetLatency();
        
        Debug.Log($"Sample Rate: {info.sampleRate}Hz");
        Debug.Log($"Buffer Size: {info.bufferSize} frames");
        Debug.Log($"Latency: {latency}ms");
    }
}
```

### Device-Specific Optimization

Different iOS devices have different audio capabilities:

| Device | Recommended Buffer | Expected Latency |
|--------|-------------------|------------------|
| iPhone 12+ | 256 frames | ~5ms |
| iPhone X-11 | 256-512 frames | ~5-10ms |
| iPhone 7-8 | 512 frames | ~10ms |
| iPad Pro | 256 frames | ~5ms |
| iPad (2020+) | 512 frames | ~10ms |

## App Store Submission

### Requirements

1. **Privacy**: Add microphone usage description if recording
2. **Background Audio**: Declare if app plays audio in background
3. **Testing**: Test on multiple devices
4. **Optimization**: Profile and optimize performance

### Build for Distribution

1. In Xcode:
   - Select **Generic iOS Device** or **Any iOS Device (arm64)**
   - Product → Archive
   - Distribute to App Store

2. Upload to App Store Connect

### App Store Review Notes

If using background audio, explain in review notes:
```
This app uses low-latency audio for [your use case].
Audio continues in background to maintain real-time processing.
```

## Advanced Topics

### Custom Audio Session

For more control, configure audio session manually:

```objective-c
// Add to Unity-iPhone/Classes/UnityAppController.mm

#import <AVFoundation/AVFoundation.h>

- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions
{
    // Configure audio session
    AVAudioSession *session = [AVAudioSession sharedInstance];
    NSError *error = nil;
    
    // Set category for playback
    [session setCategory:AVAudioSessionCategoryPlayback
             withOptions:AVAudioSessionCategoryOptionMixWithOthers
                   error:&error];
    
    // Optimize for low latency
    [session setPreferredSampleRate:48000.0 error:&error];
    [session setPreferredIOBufferDuration:0.005 error:&error];
    
    [session setActive:YES error:&error];
    
    return [super application:application didFinishLaunchingWithOptions:launchOptions];
}
```

### Handling Audio Interruptions

```csharp
void OnApplicationPause(bool pauseStatus)
{
    if (pauseStatus)
    {
        // App going to background
        UNAudioEngine.Stop();
    }
    else
    {
        // App returning to foreground
        UNAudioEngine.Start();
    }
}
```

## Additional Resources

- [Core Audio Documentation](https://developer.apple.com/documentation/coreaudio)
- [Audio Session Programming Guide](https://developer.apple.com/library/archive/documentation/Audio/Conceptual/AudioSessionProgrammingGuide/)
- [Unity iOS Build](https://docs.unity3d.com/Manual/iphone-GettingStarted.html)
- [Instruments User Guide](https://help.apple.com/instruments/mac/)

## Support

For iOS-specific issues:
- Check Xcode console logs
- Use Instruments for profiling
- Review crash reports in Xcode Organizer

Report issues at: https://github.com/lask3802/UNAudio/issues
