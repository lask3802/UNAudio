# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
