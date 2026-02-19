# UNAudio - Unity Low-Latency Native Audio Engine
# UNAudio - Unity 低延遲原生音訊引擎

[![Platform](https://img.shields.io/badge/platform-Android%20%7C%20iOS-blue.svg)](https://github.com/lask3802/UNAudio)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

[English](#english) | [中文](#中文)

---

## English

### Overview

UNAudio is a high-performance, low-latency audio engine for Unity that provides direct access to native audio APIs on Android and iOS platforms. It bypasses Unity's built-in audio system to achieve minimal latency and maximum control over audio processing.

### Key Features

- **Ultra-Low Latency**: 
  - Android: <15ms using AAudio API
  - iOS: <8ms using CoreAudio (AudioUnit)
- **Native Performance**: Direct access to platform audio APIs
- **Cross-Platform**: Unified C# API for both Android and iOS
- **Real-time Processing**: Support for custom audio callbacks
- **Production Ready**: Optimized for mobile devices
- **Easy Integration**: Simple Unity plugin architecture

### System Architecture

```
┌─────────────────────────────────────────────┐
│          Unity C# Layer                      │
│  ┌────────────────────────────────────────┐ │
│  │    UNAudioEngine.cs                    │ │
│  │  - Initialize/Shutdown                 │ │
│  │  - Start/Stop                          │ │
│  │  - Volume Control                      │ │
│  │  - Callback Management                 │ │
│  └────────────────────────────────────────┘ │
└─────────────────┬───────────────────────────┘
                  │ P/Invoke
┌─────────────────▼───────────────────────────┐
│       Native C/C++ Layer                     │
│  ┌──────────────┐      ┌─────────────────┐ │
│  │   Android    │      │      iOS        │ │
│  │  (AAudio)    │      │  (CoreAudio)    │ │
│  │              │      │                 │ │
│  │ - Low-level  │      │ - AudioUnit     │ │
│  │   audio I/O  │      │ - RemoteIO      │ │
│  │ - Callbacks  │      │ - Callbacks     │ │
│  └──────────────┘      └─────────────────┘ │
└─────────────────────────────────────────────┘
```

### Platform Support

| Platform | API | Min Version | Latency Target |
|----------|-----|-------------|----------------|
| Android  | AAudio | Android 8.0 (API 26) | <15ms |
| iOS      | CoreAudio | iOS 11.0+ | <8ms |

### Installation

#### Method 1: Unity Package Manager (Recommended)

1. Open Unity Package Manager
2. Click "+" → "Add package from git URL"
3. Enter: `https://github.com/lask3802/UNAudio.git`

#### Method 2: Manual Installation

1. Download the latest release
2. Extract to your Unity project's `Assets/Plugins` folder
3. Ensure native libraries are in correct platform folders:
   - Android: `Assets/Plugins/Android/`
   - iOS: `Assets/Plugins/iOS/`

### Quick Start

```csharp
using UnityEngine;
using UNAudio;

public class AudioDemo : MonoBehaviour
{
    private UNAudioEngine.AudioCallbackDelegate audioCallback;

    void Start()
    {
        // Initialize audio engine
        var result = UNAudioEngine.Initialize(
            sampleRate: 48000,
            channels: 2,
            bufferSize: 512
        );

        if (result == UNAudioEngine.Result.Success)
        {
            // Set audio callback
            audioCallback = OnAudioCallback;
            UNAudioEngine.SetCallback(audioCallback);

            // Start playback
            UNAudioEngine.Start();
        }
    }

    void OnDestroy()
    {
        UNAudioEngine.Stop();
        UNAudioEngine.Shutdown();
    }

    private void OnAudioCallback(System.IntPtr buffer, int frames, int channels, System.IntPtr userData)
    {
        unsafe
        {
            float* samples = (float*)buffer.ToPointer();
            // Fill buffer with audio samples
            for (int i = 0; i < frames * channels; i++)
            {
                samples[i] = 0.0f; // Your audio processing here
            }
        }
    }
}
```

### API Reference

#### Initialization

```csharp
// Initialize the audio engine
UNAudioEngine.Result Initialize(int sampleRate, int channels, int bufferSize)

// Shutdown the audio engine
UNAudioEngine.Result Shutdown()
```

#### Playback Control

```csharp
// Start audio playback
UNAudioEngine.Result Start()

// Stop audio playback
UNAudioEngine.Result Stop()
```

#### Audio Settings

```csharp
// Set master volume (0.0 to 1.0)
UNAudioEngine.Result SetVolume(float volume)

// Get current master volume
float GetVolume()

// Get current latency in milliseconds
int GetLatency()
```

#### Device Information

```csharp
// Get audio device information
UNAudioEngine.Result GetDeviceInfo(out DeviceInfo info)

public struct DeviceInfo
{
    public int sampleRate;
    public int channels;
    public int bufferSize;
    public AudioFormat format;
}
```

#### Audio Processing

```csharp
// Set audio callback for real-time processing
UNAudioEngine.Result SetCallback(AudioCallbackDelegate callback, IntPtr userData)

// Callback delegate signature
public delegate void AudioCallbackDelegate(IntPtr buffer, int frames, int channels, IntPtr userData)
```

### Building for Platforms

#### Android Build

1. **Prerequisites**:
   - Android NDK r21 or later
   - Gradle 7.0+
   - CMake 3.10+

2. **Build Native Library**:
   ```bash
   cd Plugins/Android
   ./gradlew assembleRelease
   ```

3. **Unity Configuration**:
   - Set minimum API level to 26 (Android 8.0)
   - Enable ARM64 architecture
   - Add permissions in AndroidManifest.xml:
     ```xml
     <uses-permission android:name="android.permission.MODIFY_AUDIO_SETTINGS" />
     ```

#### iOS Build

1. **Prerequisites**:
   - Xcode 13.0+
   - iOS Deployment Target: 11.0+

2. **Unity Configuration**:
   - Set target minimum iOS version to 11.0
   - Enable Bitcode: No (for faster builds)
   - Add frameworks in build settings:
     - AudioToolbox.framework
     - AVFoundation.framework

3. **Build from Unity**:
   - Build Settings → iOS
   - Build and open in Xcode
   - Native plugin will be automatically included

### Performance Targets

| Metric | Android | iOS | Notes |
|--------|---------|-----|-------|
| Latency | <15ms | <8ms | Measured round-trip |
| CPU Usage | <5% | <3% | On mid-range devices |
| Buffer Size | 256-1024 | 256-512 | Frames |
| Sample Rate | 48000 Hz | 48000 Hz | Standard |

### Examples

See the `Examples/` folder for complete examples:

- **UNAudioExample.cs**: Basic sine wave generator
- **More examples coming soon**

### Troubleshooting

#### Android Issues

**Problem**: Audio not playing
- Check Android version (must be 8.0+)
- Verify AAudio permissions in manifest
- Check logcat for error messages: `adb logcat | grep UNAudio`

**Problem**: High latency
- Reduce buffer size (minimum 256 frames)
- Use exclusive sharing mode
- Test on device with low-latency audio support

#### iOS Issues

**Problem**: Build fails
- Ensure Xcode version is 13.0+
- Check that AudioToolbox framework is linked
- Verify deployment target is iOS 11.0+

**Problem**: Audio glitches
- Increase buffer size
- Check for CPU-intensive operations in callback
- Profile with Instruments

### Performance Tips

1. **Keep callbacks lightweight**: Audio callbacks run on real-time threads
2. **Avoid allocations**: Never allocate memory in audio callbacks
3. **Use fixed-point math**: Faster than floating-point on some devices
4. **Profile regularly**: Use platform profiling tools
5. **Test on real devices**: Emulators may not reflect actual performance

### Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request

### License

MIT License - see LICENSE file for details

### Credits

Developed by LASK
- GitHub: [@lask3802](https://github.com/lask3802)
- Email: lask380@hotmail.com

### Support

- **Issues**: [GitHub Issues](https://github.com/lask3802/UNAudio/issues)
- **Discussions**: [GitHub Discussions](https://github.com/lask3802/UNAudio/discussions)

---

## 中文

### 概述

UNAudio 是一個為 Unity 設計的高性能、低延遲音訊引擎，在 Android 和 iOS 平台上提供對原生音訊 API 的直接訪問。它繞過 Unity 內建的音訊系統，以實現最小延遲和對音訊處理的最大控制。

### 主要特性

- **超低延遲**：
  - Android：使用 AAudio API <15ms
  - iOS：使用 CoreAudio (AudioUnit) <8ms
- **原生性能**：直接訪問平台音訊 API
- **跨平台**：Android 和 iOS 統一的 C# API
- **即時處理**：支援自訂音訊回調
- **生產就緒**：針對行動裝置優化
- **易於整合**：簡單的 Unity 插件架構

### 系統架構

參見英文版架構圖

### 平台支援

| 平台 | API | 最低版本 | 延遲目標 |
|------|-----|---------|---------|
| Android | AAudio | Android 8.0 (API 26) | <15ms |
| iOS | CoreAudio | iOS 11.0+ | <8ms |

### 安裝

#### 方法 1：Unity Package Manager（推薦）

1. 打開 Unity Package Manager
2. 點擊 "+" → "Add package from git URL"
3. 輸入：`https://github.com/lask3802/UNAudio.git`

#### 方法 2：手動安裝

1. 下載最新版本
2. 解壓縮到您的 Unity 專案的 `Assets/Plugins` 資料夾
3. 確保原生庫在正確的平台資料夾中：
   - Android：`Assets/Plugins/Android/`
   - iOS：`Assets/Plugins/iOS/`

### 快速開始

參見英文版程式碼範例

### API 參考

參見英文版 API 文件

### 平台建置

#### Android 建置

1. **前置要求**：
   - Android NDK r21 或更新版本
   - Gradle 7.0+
   - CMake 3.10+

2. **建置原生庫**：
   ```bash
   cd Plugins/Android
   ./gradlew assembleRelease
   ```

3. **Unity 配置**：
   - 設定最低 API 級別為 26 (Android 8.0)
   - 啟用 ARM64 架構
   - 在 AndroidManifest.xml 中添加權限

#### iOS 建置

1. **前置要求**：
   - Xcode 13.0+
   - iOS 部署目標：11.0+

2. **Unity 配置**：
   - 設定目標最低 iOS 版本為 11.0
   - 添加必要的框架

3. **從 Unity 建置**：
   - Build Settings → iOS
   - 建置並在 Xcode 中打開

### 性能目標

參見英文版性能表格

### 範例

查看 `Examples/` 資料夾以獲取完整範例

### 疑難排解

參見英文版疑難排解指南

### 貢獻

歡迎貢獻！請參見英文版貢獻指南

### 授權

MIT 授權 - 詳見 LICENSE 檔案

### 致謝

由 LASK 開發
- GitHub：[@lask3802](https://github.com/lask3802)
- Email：lask380@hotmail.com

### 支援

- **問題回報**：[GitHub Issues](https://github.com/lask3802/UNAudio/issues)
- **討論**：[GitHub Discussions](https://github.com/lask3802/UNAudio/discussions)
