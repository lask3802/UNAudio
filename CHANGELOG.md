# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-02-19

### Added

#### Core Features
- Initial release of UNAudio low-latency audio engine for Unity
- Support for Android platform (API 26+) using AAudio
- Support for iOS platform (iOS 11.0+) using CoreAudio
- Unified C# API for both platforms
- Real-time audio processing callbacks
- Master volume control
- Audio latency reporting
- Device information queries

#### Android Implementation
- Native AAudio backend for low-latency audio
- Support for exclusive sharing mode
- ARM64, ARMv7, x86, and x86_64 architectures
- Gradle-based build system
- CMake integration for native builds
- Optimized for <15ms latency

#### iOS Implementation
- Native CoreAudio (AudioUnit) backend
- RemoteIO audio unit configuration
- ARM64 architecture support
- Xcode project integration
- Optimized for <8ms latency

#### Documentation
- Comprehensive README with bilingual support (English/Chinese)
- Detailed Android setup guide
- Detailed iOS setup guide
- Complete API reference documentation
- Contributing guidelines
- Code examples and samples

#### CI/CD
- GitHub Actions workflow for Android builds
- GitHub Actions workflow for iOS builds
- Combined CI pipeline for multi-platform testing
- Automated artifact generation

#### Examples
- Basic sine wave generator example
- Audio callback demonstration
- Volume control sample

### Technical Details

**Android:**
- Minimum SDK: API 26 (Android 8.0)
- NDK: r21 or later
- Build System: Gradle 8.0 + CMake 3.10+
- Audio API: AAudio
- Supported Architectures: armeabi-v7a, arm64-v8a, x86, x86_64

**iOS:**
- Minimum Version: iOS 11.0
- Xcode: 13.0+
- Build System: Xcode Build System
- Audio API: CoreAudio (AudioUnit/RemoteIO)
- Supported Architecture: ARM64

**Performance:**
- Android Latency: <15ms (typical 10-12ms on modern devices)
- iOS Latency: <8ms (typical 5-7ms on modern devices)
- CPU Usage: <5% on mid-range devices
- Buffer Sizes: 256-1024 frames supported
- Sample Rates: 44100Hz, 48000Hz recommended

### Security
- No known vulnerabilities at release
- Uses platform-standard audio APIs
- No network access required
- No user data collection

### Dependencies
- Unity 2020.3 LTS or later
- Android: Gradle, NDK, AAudio library
- iOS: Xcode, AudioToolbox framework, AVFoundation framework

### License
- MIT License

---

## [Unreleased]

### Planned Features
- Support for additional audio formats (MP3, OGG, FLAC)
- Built-in mixer with multiple audio tracks
- Spatial audio support
- Audio effects (reverb, delay, EQ)
- Recording functionality
- File playback integration
- Performance profiling tools
- Unity Editor support for testing

---

[1.0.0]: https://github.com/lask3802/UNAudio/releases/tag/v1.0.0
