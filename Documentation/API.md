# UNAudio API Reference

## Namespace: `UNAudio`

---

### `UNAudioEngine`

Singleton MonoBehaviour managing the native audio engine lifetime.

| Member | Type | Description |
|--------|------|-------------|
| `Instance` | `static UNAudioEngine` | Global engine instance (auto-created). |
| `sampleRate` | `int` | Output sample rate (default 48000). |
| `outputChannels` | `int` | Number of output channels (default 2). |
| `bufferSize` | `int` | Buffer size in frames (default 256). |
| `IsInitialized` | `bool` | Whether the native engine is running. |
| `SetMasterVolume(float)` | `void` | Set master volume (0–1). |
| `GetMasterVolume()` | `float` | Get current master volume. |
| `SetBufferSize(int)` | `void` | Change buffer size at runtime. |
| `GetCurrentLatency()` | `float` | Estimated output latency in ms. |

---

### `UNAudioClip` (ScriptableObject)

Represents an audio clip with compressed or decompressed data.

| Member | Type | Description |
|--------|------|-------------|
| `sampleRate` | `int` | Sample rate in Hz. |
| `channels` | `int` | Channel count. |
| `length` | `float` | Duration in seconds. |
| `LoadType` | `AudioLoadType` | Current compression mode. |
| `IsLoaded` | `bool` | Whether data is loaded natively. |
| `IsCompressed` | `bool` | Whether data is compressed. |
| `SetLoadType(AudioLoadType)` | `void` | Change compression mode. |
| `LoadAudioData()` | `void` | Load into native engine. |
| `UnloadAudioData()` | `void` | Unload from native engine. |
| `GetMemorySize()` | `long` | Memory usage in bytes. |

---

### `UNAudioSource` (MonoBehaviour)

Plays back a `UNAudioClip`. Attach to any GameObject.

| Member | Type | Description |
|--------|------|-------------|
| `clip` | `UNAudioClip` | The clip to play. |
| `volume` | `float` | Volume (0–1). |
| `loop` | `bool` | Loop playback. |
| `spatialBlend` | `float` | 2D/3D blend (0–1). |
| `isPlaying` | `bool` | Whether currently playing. |
| `Play()` | `void` | Start playback. |
| `Pause()` | `void` | Pause playback. |
| `Stop()` | `void` | Stop and reset. |
| `PlayOneShot(clip)` | `static void` | One-shot playback. |
| `PlayClipAtPoint(clip, pos)` | `static void` | 3D one-shot. |

---

### `UNAudioListener` (MonoBehaviour)

Marks a GameObject as the 3D audio listener. Singleton.

---

### `AudioLoadType` (enum)

| Value | Description |
|-------|-------------|
| `CompressedInMemory` | Keep compressed; decode during playback. |
| `DecompressOnLoad` | Decompress fully on load. |
| `Streaming` | Stream from disk. |

---

### `AudioUtility` (static class)

| Method | Description |
|--------|-------------|
| `LinearToDecibel(float)` | Linear amplitude → dB. |
| `DecibelToLinear(float)` | dB → linear amplitude. |
| `ClampVolume(float)` | Clamp to 0–1. |
| `BufferLatencyMs(int, int)` | Calculate buffer latency. |
| `DurationToSamples(float, int, int)` | Duration → sample count. |

---

### `UNAudioDebug` (static class)

| Method | Description |
|--------|-------------|
| `TraceAudioPath(UNAudioSource)` | Log the audio signal path. |
| `GetPerformanceStats()` | Get an `AudioPerformanceStats` snapshot. |

---

## Native C API (P/Invoke)

All native functions are prefixed with `UNAudio_` and exported from the shared library.

| Function | Description |
|----------|-------------|
| `UNAudio_Initialize(config)` | Initialise the engine. |
| `UNAudio_Shutdown()` | Shut down the engine. |
| `UNAudio_LoadAudio(data, size, mode)` | Load audio data, returns handle. |
| `UNAudio_UnloadAudio(handle)` | Unload audio data. |
| `UNAudio_Play(handle)` | Start playback. |
| `UNAudio_Pause(handle)` | Pause playback. |
| `UNAudio_Stop(handle)` | Stop playback. |
| `UNAudio_SetVolume(handle, vol)` | Set source volume. |
| `UNAudio_SetMasterVolume(vol)` | Set master volume. |
| `UNAudio_GetCurrentLatency()` | Get estimated latency (ms). |
