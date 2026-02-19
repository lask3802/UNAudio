# Android Setup Guide for UNAudio

## Prerequisites

- Unity 2020.3 LTS or later
- Android SDK with API Level 26 (Android 8.0) or higher
- Android NDK r21 or later
- Gradle 7.0 or later
- CMake 3.10 or later

## Project Configuration

### 1. Unity Player Settings

Open **Edit → Project Settings → Player → Android**:

1. **Other Settings**:
   - Minimum API Level: **Android 8.0 'Oreo' (API level 26)**
   - Target API Level: **Android 12 or higher (API level 31+)**
   - Scripting Backend: **IL2CPP** (recommended for better performance)
   - Target Architectures:
     - ✅ ARMv7
     - ✅ ARM64 (required for Play Store)
     - ✅ x86 (optional, for emulators)
     - ✅ x86_64 (optional, for emulators)

2. **Publishing Settings**:
   - Create custom AndroidManifest.xml if not exists
   - Add required permissions (see below)

### 2. Android Manifest Configuration

Add the following permissions to your `AndroidManifest.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
    
    <!-- Required permissions for audio -->
    <uses-permission android:name="android.permission.MODIFY_AUDIO_SETTINGS" />
    
    <!-- Optional: For recording audio -->
    <uses-permission android:name="android.permission.RECORD_AUDIO" />
    
    <!-- Recommended: Declare audio features -->
    <uses-feature 
        android:name="android.hardware.audio.low_latency" 
        android:required="false" />
    <uses-feature 
        android:name="android.hardware.audio.pro" 
        android:required="false" />
    
    <application>
        <!-- Your application settings -->
    </application>
</manifest>
```

### 3. Build the Native Plugin

The native Android plugin uses CMake and Gradle for building.

#### Option A: Build with Gradle (Recommended)

```bash
cd Plugins/Android
./gradlew assembleRelease
```

The compiled library will be in:
```
Plugins/Android/unaudio/build/intermediates/cmake/release/obj/
```

#### Option B: Manual CMake Build

```bash
cd NativeSource/Android
mkdir build && cd build

# For ARM64
cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a \
      -DANDROID_PLATFORM=android-26 \
      ..
cmake --build .

# For ARMv7
cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=armeabi-v7a \
      -DANDROID_PLATFORM=android-26 \
      ..
cmake --build .
```

### 4. Copy Native Libraries to Unity

Copy the compiled libraries to your Unity project:

```bash
# Copy to Unity Plugins folder
cp -r Plugins/Android/unaudio/build/intermediates/cmake/release/obj/* \
      YourUnityProject/Assets/Plugins/Android/libs/
```

Or use the pre-compiled libraries from the release package.

### 5. Plugin Import Settings

In Unity, select the native library files and configure:

1. Select `libUNAudio.so` in Project window
2. In Inspector:
   - Platform: **Android**
   - CPU: **ARMv7**, **ARM64**, etc. (match your build)
   - Load on startup: ✅ Enabled

### 6. Testing on Device

#### Enable Developer Options

1. Go to **Settings → About Phone**
2. Tap **Build Number** 7 times
3. Go back to **Settings → Developer Options**
4. Enable **USB Debugging**

#### Build and Run

1. Connect Android device via USB
2. In Unity: **File → Build Settings → Build and Run**
3. App will install and launch on device

#### Check Logs

Use ADB to monitor logs:

```bash
# Filter UNAudio logs
adb logcat | grep UNAudio

# Full logcat
adb logcat -v time
```

Expected output:
```
D/UNAudio: Initializing UNAudio: SR=48000, CH=2, BS=512
D/UNAudio: UNAudio initialized successfully
D/UNAudio: Actual config: SR=48000, CH=2, BS=1024
D/UNAudio: Starting UNAudio
D/UNAudio: UNAudio started successfully
```

## Troubleshooting

### Issue: "UnsatisfiedLinkError: dlopen failed: library not found"

**Solution**:
1. Check that `libUNAudio.so` is in the correct folder
2. Verify architecture matches device (ARM64 for most modern devices)
3. Ensure library is included in build (check apk contents)

### Issue: "Failed to initialize UNAudio: PlatformError"

**Solution**:
1. Check Android version (must be 8.0 or higher)
2. Verify AAudio is available: `adb shell dumpsys media.audio_policy`
3. Check for permission issues
4. Review logcat for detailed error messages

### Issue: High Latency (>30ms)

**Possible causes**:
1. Device doesn't support low-latency audio
2. Buffer size too large - try reducing to 256-512 frames
3. Other apps using audio in background
4. USB audio debugging enabled

**Solutions**:
- Reduce buffer size in code
- Close other audio apps
- Test on device with Pro Audio support
- Check device specs: `adb shell pm list features | grep audio`

### Issue: Audio Glitches/Dropouts

**Solutions**:
1. Increase buffer size (512-1024 frames)
2. Reduce CPU usage in audio callback
3. Use IL2CPP backend for better performance
4. Check for garbage collection during playback
5. Profile with Android Profiler

## Performance Optimization

### Recommended Settings

```csharp
// For low latency
UNAudioEngine.Initialize(
    sampleRate: 48000,
    channels: 2,
    bufferSize: 256  // Adjust based on device
);

// For stability
UNAudioEngine.Initialize(
    sampleRate: 48000,
    channels: 2,
    bufferSize: 512  // More stable
);
```

### Testing Latency

```csharp
void Start()
{
    UNAudioEngine.Initialize(48000, 2, 256);
    int latency = UNAudioEngine.GetLatency();
    Debug.Log($"Audio Latency: {latency}ms");
}
```

### Device Compatibility

To check if device supports low-latency audio:

```bash
adb shell pm list features | grep audio
```

Look for:
- `android.hardware.audio.low_latency`
- `android.hardware.audio.pro`

## Building for Google Play

### Requirements

1. **Target API Level**: Must target API 31+ for new apps
2. **64-bit Support**: ARM64 library required
3. **App Bundle**: Use Android App Bundle (AAB) format

### Build Steps

1. Set target API level to 31+
2. Enable ARM64 architecture
3. Build → **Build App Bundle**
4. Test bundle with bundletool:

```bash
bundletool build-apks --bundle=app.aab --output=app.apks
bundletool install-apks --apks=app.apks
```

## Additional Resources

- [AAudio Documentation](https://developer.android.com/ndk/guides/audio/aaudio/aaudio)
- [Android Audio Latency](https://source.android.com/devices/audio/latency)
- [Unity Android Build](https://docs.unity3d.com/Manual/android-BuildProcess.html)

## Support

For Android-specific issues, please check:
- Device logs with `adb logcat`
- Unity build logs
- Native crash dumps in `/data/tombstones/`

Report issues at: https://github.com/lask3802/UNAudio/issues
