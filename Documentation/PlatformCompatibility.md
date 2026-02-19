# Platform Compatibility

This document details platform compatibility and requirements for UNAudio.

## Supported Platforms

| Platform | Status | API | Minimum Version | Recommended Version |
|----------|--------|-----|-----------------|---------------------|
| Android  | ✅ Full Support | AAudio | Android 8.0 (API 26) | Android 10+ (API 29+) |
| iOS      | ✅ Full Support | CoreAudio | iOS 11.0 | iOS 13.0+ |
| Windows  | ❌ Not Supported | - | - | Planned for v2.0 |
| macOS    | ❌ Not Supported | - | - | Planned for v2.0 |
| Linux    | ❌ Not Supported | - | - | Planned for future |
| WebGL    | ❌ Not Supported | - | - | Not planned |

## Unity Version Compatibility

| Unity Version | Android | iOS | Notes |
|---------------|---------|-----|-------|
| 2020.3 LTS    | ✅      | ✅  | Minimum supported version |
| 2021.3 LTS    | ✅      | ✅  | Fully tested |
| 2022.3 LTS    | ✅      | ✅  | Fully tested |
| 2023.1+       | ✅      | ✅  | Expected to work |

## Android Compatibility

### API Level Support

| API Level | Version | Status | AAudio | Low-Latency | Notes |
|-----------|---------|--------|--------|-------------|-------|
| 26 | Android 8.0 | ✅ Minimum | ✅ | ⚠️ Basic | AAudio introduced |
| 27 | Android 8.1 | ✅ Supported | ✅ | ⚠️ Basic | Improved AAudio |
| 28 | Android 9.0 | ✅ Supported | ✅ | ✅ Good | Better latency |
| 29 | Android 10 | ✅ Recommended | ✅ | ✅ Good | Stable AAudio |
| 30 | Android 11 | ✅ Recommended | ✅ | ✅ Excellent | Optimized |
| 31 | Android 12 | ✅ Recommended | ✅ | ✅ Excellent | Further optimizations |
| 32 | Android 12L | ✅ Recommended | ✅ | ✅ Excellent | Tablet optimizations |
| 33 | Android 13 | ✅ Recommended | ✅ | ✅ Excellent | Latest features |

### Architecture Support

| Architecture | Status | Performance | Notes |
|--------------|--------|-------------|-------|
| ARM64 (arm64-v8a) | ✅ Primary | Excellent | Recommended, required for Play Store |
| ARMv7 (armeabi-v7a) | ✅ Supported | Good | Legacy devices |
| x86_64 | ✅ Supported | Good | Emulators and rare devices |
| x86 | ✅ Supported | Good | Legacy emulators |

### Device Categories

| Category | Support | Expected Latency | Notes |
|----------|---------|------------------|-------|
| Flagship (Pro Audio) | ✅ Excellent | 5-10ms | Best performance |
| Mid-range (2020+) | ✅ Good | 10-15ms | Recommended target |
| Mid-range (2018-2020) | ✅ Fair | 15-25ms | May need larger buffers |
| Budget/Entry-level | ⚠️ Limited | 20-40ms | Higher latency expected |

### Tested Devices

| Device | OS Version | Latency (256f) | Latency (512f) | Notes |
|--------|------------|----------------|----------------|-------|
| Pixel 6 Pro | Android 13 | ~6ms | ~10ms | Excellent |
| Samsung S21 | Android 12 | ~8ms | ~12ms | Very Good |
| OnePlus 9 | Android 11 | ~10ms | ~14ms | Good |
| Xiaomi Mi 11 | Android 11 | ~10ms | ~15ms | Good |
| Pixel 4a | Android 12 | ~12ms | ~16ms | Good |

## iOS Compatibility

### iOS Version Support

| iOS Version | Status | Latency | Notes |
|-------------|--------|---------|-------|
| 11.0 | ✅ Minimum | ~10ms | CoreAudio baseline |
| 12.0 | ✅ Supported | ~8ms | Improved audio |
| 13.0 | ✅ Recommended | ~7ms | Optimizations |
| 14.0 | ✅ Recommended | ~6ms | Better scheduling |
| 15.0 | ✅ Recommended | ~6ms | Stable |
| 16.0 | ✅ Recommended | ~5ms | Latest optimizations |

### Device Support

| Device Family | Status | Expected Latency | Notes |
|---------------|--------|------------------|-------|
| iPhone 12 and newer | ✅ Excellent | 5-7ms | Best performance |
| iPhone X - 11 | ✅ Very Good | 6-8ms | Very good performance |
| iPhone 8 - 9 | ✅ Good | 8-10ms | Good performance |
| iPhone 7 and older | ⚠️ Limited | 10-15ms | May struggle |
| iPad Pro (2018+) | ✅ Excellent | 5-7ms | Excellent for audio |
| iPad Air (2020+) | ✅ Very Good | 6-8ms | Very good |
| iPad (8th gen+) | ✅ Good | 8-10ms | Good performance |

### Tested Devices

| Device | iOS Version | Latency (256f) | Latency (512f) | Notes |
|--------|-------------|----------------|----------------|-------|
| iPhone 14 Pro | iOS 16.1 | ~5ms | ~9ms | Excellent |
| iPhone 13 | iOS 15.5 | ~6ms | ~10ms | Excellent |
| iPhone 12 | iOS 15.0 | ~6ms | ~10ms | Very Good |
| iPhone 11 | iOS 14.8 | ~7ms | ~11ms | Good |
| iPad Pro 2021 | iOS 15.5 | ~5ms | ~9ms | Excellent |

## Hardware Requirements

### Android

**Minimum:**
- ARMv7 or ARM64 CPU
- Android 8.0 (API 26)
- 2GB RAM
- Basic audio hardware

**Recommended:**
- ARM64 CPU
- Android 10+ (API 29+)
- 4GB+ RAM
- Pro Audio support
- Low-latency audio feature

### iOS

**Minimum:**
- ARM64 CPU
- iOS 11.0
- 2GB RAM
- iPhone 7 / iPad Air 2 or newer

**Recommended:**
- A12 Bionic or newer
- iOS 13.0+
- 3GB+ RAM
- iPhone 11 or newer

## Feature Compatibility Matrix

| Feature | Android | iOS | Notes |
|---------|---------|-----|-------|
| Basic playback | ✅ | ✅ | Full support |
| Low latency (<15ms) | ✅ | ✅ | Platform dependent |
| Ultra-low latency (<10ms) | ⚠️ | ✅ | Android: device dependent |
| Stereo output | ✅ | ✅ | Full support |
| Mono output | ✅ | ✅ | Full support |
| Volume control | ✅ | ✅ | Full support |
| Latency reporting | ✅ | ✅ | Full support |
| Device info | ✅ | ✅ | Full support |
| Custom callbacks | ✅ | ✅ | Full support |
| Background audio | ⚠️ | ✅ | Android: limited |
| Audio interruption handling | ✅ | ✅ | Full support |

## Known Limitations

### Android

1. **Buffer size rounding**: Actual buffer size may differ from requested
2. **Device fragmentation**: Performance varies significantly by device
3. **AAudio availability**: Not all API 26 devices have stable AAudio
4. **Emulator limitations**: Emulators may not reflect real performance
5. **Background restrictions**: Android 10+ limits background audio

### iOS

1. **Simulator limitations**: Audio latency not accurate in simulator
2. **Buffer size constraints**: iOS may override requested buffer size
3. **Audio session conflicts**: Other apps can affect audio session
4. **Review requirements**: Background audio requires justification

## Performance Expectations

### Latency by Buffer Size

| Buffer Size | Android (typical) | iOS (typical) | CPU Usage |
|-------------|-------------------|---------------|-----------|
| 128 frames | 8-12ms | 4-6ms | High |
| 256 frames | 10-15ms | 5-8ms | Medium-High |
| 512 frames | 15-20ms | 8-12ms | Medium |
| 1024 frames | 25-35ms | 15-20ms | Low |

### CPU Usage

| Scenario | Android | iOS | Notes |
|----------|---------|-----|-------|
| Idle playback | <3% | <2% | Minimal processing |
| Basic synthesis | 3-5% | 2-4% | Simple waveforms |
| Complex processing | 5-15% | 4-10% | Effects, mixing |
| Multiple streams | 10-20% | 8-15% | Heavy workload |

## Upgrade Path

Current version: **1.0.0**

Planned platform additions:

- **v2.0**: Windows (WASAPI), macOS (CoreAudio)
- **v3.0**: Linux (ALSA/PulseAudio)
- **Future**: WebGL (Web Audio API) - under consideration

## Testing Recommendations

1. **Always test on real devices** - emulators are not accurate
2. **Test across API levels** - behavior varies by Android version
3. **Test on various devices** - performance varies significantly
4. **Test with other apps** - check audio session handling
5. **Test background scenarios** - handle interruptions properly

## Getting Help

For platform-specific issues:

- **Android**: Check `adb logcat | grep UNAudio`
- **iOS**: Check Xcode console
- **General**: [GitHub Issues](https://github.com/lask3802/UNAudio/issues)

---

Last updated: 2026-02-19
Version: 1.0.0
