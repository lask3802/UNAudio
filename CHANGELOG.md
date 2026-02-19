# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2026-02-19

### Added

- Project scaffold with three-layer architecture (Unity C# → P/Invoke → Native C/C++)
- Native C/C++ core engine with 17 exported P/Invoke functions
- Audio decoder stubs for MP3, Vorbis, and FLAC formats
- Multi-track audio mixer framework with master volume and peak tracking
- Platform output stubs for WASAPI (Windows), CoreAudio (macOS), ALSA (Linux), Oboe (Android), CoreAudio (iOS)
- CMake cross-platform build system
- Unity C# runtime: `UNAudioClip`, `UNAudioSource`, `UNAudioListener`, `UNAudioEngine`
- P/Invoke bridge (`UNAudioBridge`) with all native function declarations
- `AudioUtility` and `UNAudioDebug` helper classes
- Editor tools: `UNAudioImporter` (ScriptedImporter), `UNAudioInspector`, `UNAudioTestWindow`
- Assembly definitions for Runtime, Editor, and Tests
- NUnit test scaffold (Runtime and Editor tests)
- Documentation: API reference, Getting Started, Performance Guide, Platform Compatibility
- Sample stubs: BasicPlayback, 3DAudio, MusicGame
- UPM package.json for Unity Package Manager git URL installation
- GitHub Actions CI for native builds (Linux, macOS, Windows) and UPM package validation
- MIT License
