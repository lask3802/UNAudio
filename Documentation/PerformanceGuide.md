# UNAudio Performance Guide

## 延遲目標 (Latency Targets)

| Platform | API | Target Latency | Recommended Buffer |
|----------|-----|----------------|-------------------|
| Windows (WASAPI Exclusive) | WASAPI | < 5 ms | 64 frames @ 48 kHz |
| Windows (WASAPI Shared) | WASAPI | < 10 ms | 128 frames @ 48 kHz |
| macOS | CoreAudio | < 8 ms | 128 frames @ 48 kHz |
| iOS | CoreAudio | < 10 ms | 256 frames @ 48 kHz |
| Android (API 26+) | AAudio via Oboe | < 15 ms | 192–256 frames @ 48 kHz |
| Android (API 16–25) | OpenSL ES via Oboe | < 25 ms | 256 frames @ 48 kHz |
| Linux | ALSA | < 12 ms | 256 frames @ 48 kHz |

---

## 緩衝大小調整 (Buffer Size Tuning)

Smaller buffers reduce latency but increase CPU load and the risk of
audio glitches (underruns).

```csharp
// Trade-off: latency vs. stability
UNAudioEngine.Instance.SetBufferSize(128); // ~2.7 ms @ 48 kHz
UNAudioEngine.Instance.SetBufferSize(256); // ~5.3 ms @ 48 kHz
UNAudioEngine.Instance.SetBufferSize(512); // ~10.7 ms @ 48 kHz
```

### Recommended starting points

| Use Case | Buffer Size |
|----------|-------------|
| Music / rhythm games | 64–128 frames |
| General SFX | 256 frames |
| Background music | 512–1024 frames |

---

## CPU 使用率目標 (CPU Usage Targets)

| Active Voices | Target CPU (single core) |
|---------------|-------------------------|
| Idle (0) | < 0.1 % |
| 10 | < 2 % |
| 50 | < 8 % |
| 100 | < 15 % |

---

## 記憶體管理 (Memory Management)

| Load Type | Memory | CPU | Best For |
|-----------|--------|-----|----------|
| `CompressedInMemory` | Low | Medium | Large numbers of SFX |
| `DecompressOnLoad` | High | Low | Frequently played clips |
| `Streaming` | Very Low | Low–Med | Background music, long clips |

### 壓縮比例 (Compression Ratios)

| Duration | MP3 (~128 kbps) | PCM (16-bit stereo) | Savings |
|----------|-----------------|---------------------|---------|
| 1 min | ~1 MB | ~10 MB | 90 % |
| 5 min | ~5 MB | ~50 MB | 90 % |
| 30 min | ~30 MB | ~300 MB | 90 % |

---

## Android 特定最佳化 (Android Optimisation)

- UNAudio uses **Oboe** (Google official), which automatically selects
  AAudio on API 26+ and OpenSL ES on older devices.
- Use `Exclusive` sharing mode for the lowest latency.
- Set the buffer size to `2 × framesPerBurst` for a good latency/stability
  balance.

---

## iOS 特定最佳化 (iOS Optimisation)

- Set `AVAudioSession` IO buffer duration to 5 ms.
- Use the `Measurement` audio mode for lowest latency.
- Add `audio` to `UIBackgroundModes` in Info.plist if background playback
  is needed.

---

## 診斷工具 (Diagnostics)

```csharp
// Log the full audio signal path
UNAudioDebug.TraceAudioPath(mySource);

// Get live performance stats
var stats = UNAudioDebug.GetPerformanceStats();
Debug.Log($"Latency: {stats.latencyMs} ms");
```

Use the **UNAudio Test Panel** (`Window → UNAudio → Test Panel`) to
measure latency, run decode tests, and view live stats.
