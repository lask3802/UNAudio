# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.1.0] - 2026-10-24

### Added

- MP3 (MPEG-1/2/2.5 Layer III, dr_mp3) and FLAC (4–32-bit, up to 8 channels, dr_flac) decoding in decode and stream mode, detected from the content. MP3 is gapless (LAME / Lavf / Lavc Info tags; matches ffmpeg sample for sample); seeking is sample-exact in both. The importer override now also covers `.mp3` and `.flac`.
- SIMD mixer: SSE2 and NEON kernels for the 1:1 and cubic paths, int16 conversion, the master stage and the resampler's dot product, each tested against a scalar reference (`UNAUDIO_SIMD=OFF` builds the scalar path). On x64 with 64 voices, 1.6–1.9× faster for 1:1 voices and about 2× for cubic and live resampling; 256 stereo 1:1 voices gain 1.2× (memory bound).
- Stream failures reach C#: `UNA_E_DECODE`, `UNA_EVENT_VOICE_ERROR` (value = `una_result`) followed by `VOICE_ENDED` at the frame after the last decoded one; `UNAudioEngine.VoiceError`, `UNAudioVoice.Error`, `UNAudioPlayer.Error` / `LastError`. Covers open and seek failures and data that stops decoding before the length counted at load (corruption the format can detect, borrowed bytes changed after load); files truncated before loading fail to load or load shorter instead. See the per-format table in `Documentation/API.md`.
- Preload: `UNAudioAsset.PreloadAudioData` (importer option) starts the shared clip once the asset is loaded and the engine is running; `UNAudioAssetBundles.PreloadAsync(bundle)`.

### Fixed

- Android (Oboe): `SL_IID_*` constants exported from executables crashed ARM translation at load; `isMMapUsed` on OpenSL ES streams crashed; legacy AAudio's frozen early timestamps were taken as anchors (audible clock 90–130 ms off); anchors on resampled streams jittered by ~7 ms.
- `una_get_device_info` returned the latency from Init although the Android backend refines it later; the clock counted ordinary jitter on tiny (2-frame) callbacks as discontinuities.
- Apple: the iOS static plugin was built without warnings, `-O3` or hidden visibility; the null device thread ran at default QoS.
- NaN in the mix becomes silence instead of reaching the device.
- tvOS / visionOS use `__Internal` like iOS.

### Verified

- macOS (M4, arm64 NEON and x86_64 under Rosetta), iOS 26.3 / 18.3 simulators and the Android emulator (x86_64 native, arm64 under ARM translation), in addition to Windows and Linux (ASan, TSan); Unity EditMode on Windows and macOS; a Unity Android player on the emulator. 93 native tests, 28 Unity EditMode tests.

## [2.0.0] - 2026-10-24

Complete rewrite. Nothing from 0.1.0 is API compatible.

### Added

- Native engine with a C ABI (`una_*`, `Native~/Include/unaudio.h`): separate clips and voices, generation-checked handles, lock-free command/event queues, an audio thread that never locks, allocates or decodes.
- Sample-accurate scheduling of play / stop / pause / resume / seek / volume / pan / pitch / bus pause at any dsp frame, independent of buffer size; optional catch-up for late starts.
- Output clock: per-callback presentation timestamps smoothed by a delay-locked loop; per-voice timeline segments give latency-compensated, monotonic "audible" positions (negative before a scheduled start).
- Async decoding on a native worker pool: Ogg Vorbis (stb_vorbis) and WAV (PCM 8/16/24/32, float 32/64, EXTENSIBLE); playable after the first 8192 frames; int16 or float32 storage; polyphase windowed-sinc resampling to the device rate with sample-exact seeking; surround downmix.
- Streaming mode for long music: per-voice block ring filled by a stream thread, seek and loop without timeline drift.
- Mixer: 1:1 fast path, cubic interpolation for pitch / live resampling, exact linear ramps, 16 buses, master, peak and CPU statistics.
- Backends: WASAPI (IAudioClient3 shared / exclusive, MMCSS, default-device follow), CoreAudio (AUHAL / RemoteIO), Oboe (AAudio / OpenSL ES), ALSA, plus Null (real-time paced) and Manual (caller-driven) devices.
- Unity layer: `UNAudioEngine` (PlayerLoop driven, editor reload/play-mode safe), `UNAudioClip` (TextAsset / NativeArray / byte[] / span / file; zero managed copy from AssetBundles; optional borrow), `UNAudioVoice`, `UNAudioClock`, `UNAudioAsset`, `UNAudioAssetBundles`, `UNAudioPlayer`.
- `.ogg` / `.wav` importer override that stores the original bytes untouched.
- Tests: 58 native gtest cases (also run under ASan and TSan on Linux); 20 Unity EditMode tests including an import → AssetBundle → load → decode → sample-exact playback round trip.
- Tools: `una_device_probe` (real device latency), `una_bench` (mixer throughput).

- `una_set_suspended` / `UNAudioEngine.SetSuspended`: stop the output stream without tearing the engine down; the Unity layer suspends on `OnApplicationPause` (`SuspendWhenPaused`).

### Fixed

- stb_vorbis 1.22 residue type 2 decoding of streams with more than two channels (5.1 files decoded as noise).
- stb_vorbis rebuilt its global CRC table on every open, racing with decoders on other threads (found with TSan).

### Changed

- Native sources moved to `Native~/` so Unity no longer imports C++ files into the project.
- Minimum Unity version is 6000.0.

### Removed

- 0.1.0 scaffolding: MP3 / FLAC decoder stubs, `UNAudioSource`, `UNAudioListener`, `UNAudioBridge`, old samples and tests.

## [0.1.0] - 2026-02-19

- Initial scaffold (stub decoders and outputs).
