# UNAudio — Unity Native Low-Latency Audio Engine

## 1. 專案目標

打造一個繞過 Unity 內建 AudioSource/AudioClip 系統的**原生低延遲音訊引擎**，解決 Unity 預設音訊系統在以下方面的不足：

| 痛點 | Unity 預設 | UNAudio 目標 |
|------|-----------|-------------|
| 輸出延遲 | 約 46–92 ms（DSP buffer 1024–2048 samples） | < 10 ms（buffer 128–256 samples） |
| 解碼延遲 | `AudioClip.LoadType` 對 streaming 友善但 latency 不可控 | Compressed-in-memory + lock-free ring buffer decode |
| 混音靈活度 | 單一 AudioMixer，不可從 native 端直接存取 | 自建 Mixer graph，可程式化控制 |
| Asset Pipeline | 與 Unity importer 緊耦合 | 獨立 import pipeline，支援 Editor 即時預覽 |
| 除錯可見性 | Profiler 資訊有限 | 自訂 Editor window + real-time waveform/debug overlay |

---

## 2. 目標平台與 Native Audio Backend

| 平台 | Native API | 備註 |
|------|-----------|------|
| **Windows** | WASAPI (Exclusive/Shared) | 優先 Exclusive mode 取得最低延遲 |
| **Android** | AAudio (API 26+) / OpenSL ES (fallback) | AAudio performance mode = LowLatency |
| **iOS** | Core Audio (Audio Unit) | `kAudioUnitSubType_RemoteIO` |
| **macOS** | Core Audio (Audio Unit) | 支援 Editor 開發測試 |
| **Linux** | PulseAudio / ALSA | 僅供 Editor 開發用 |

> **Phase 1** 先完成 Windows + Android；Phase 2 加入 iOS/macOS；Linux 為開發便利提供基本支援。

---

## 3. 架構總覽

```
┌──────────────────────────────────────────────────────────┐
│                    Unity C# Layer                         │
│                                                          │
│  ┌─────────────┐  ┌──────────────┐  ┌────────────────┐  │
│  │ UNAudioMgr  │  │ UNAudioSource│  │ UNAudioMixer   │  │
│  │ (Singleton)  │  │ (Component)  │  │ (ScriptableObj)│  │
│  └──────┬───────┘  └──────┬───────┘  └───────┬────────┘  │
│         │                 │                   │           │
│         ▼                 ▼                   ▼           │
│  ┌──────────────────────────────────────────────────┐    │
│  │           C# Interop Layer (P/Invoke)             │    │
│  │     Marshal / NativeArray<T> / unsafe pinning     │    │
│  └──────────────────────┬───────────────────────────┘    │
└─────────────────────────┼────────────────────────────────┘
                          │ C ABI (cdecl)
┌─────────────────────────┼────────────────────────────────┐
│                  Native C++ Layer                         │
│                                                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌─────────┐  │
│  │  Engine   │  │  Codec   │  │  Mixer   │  │  DSP    │  │
│  │  Core     │  │  Layer   │  │  Graph   │  │  Chain  │  │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬────┘  │
│       │              │             │              │       │
│       ▼              ▼             ▼              ▼       │
│  ┌──────────────────────────────────────────────────┐    │
│  │         Platform Audio Backend (PAL)              │    │
│  │   WASAPI │ AAudio │ CoreAudio │ PulseAudio       │    │
│  └──────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────┘
```

### 3.1 分層職責

| 層級 | 職責 | 語言 |
|------|------|------|
| **Unity C# Layer** | MonoBehaviour 整合、Inspector UI、Asset 管理、Editor 工具 | C# |
| **Interop Layer** | P/Invoke binding、memory marshalling、callback delegate | C# (unsafe) |
| **Engine Core** | 生命週期管理、voice allocation、command queue | C++ |
| **Codec Layer** | 壓縮音訊解碼（Vorbis / Opus / ADPCM） | C++ |
| **Mixer Graph** | 多聲道混音、bus routing、master output | C++ |
| **DSP Chain** | 可擴展效果處理節點（Volume、Pan、Filter...） | C++ |
| **Platform Audio Backend (PAL)** | 各平台原生音訊輸出 | C++ / platform API |

---

## 4. Asset Pipeline

### 4.1 自訂 Audio Asset：`UNAudioClip`

```
UNAudioClip (.unac)
├── Header (magic, version, format metadata)
│   ├── sample_rate     : uint32
│   ├── channels        : uint8
│   ├── codec           : enum { PCM16, ADPCM, Vorbis, Opus }
│   ├── total_samples   : uint64
│   ├── compressed_size : uint64
│   └── loop_points     : { start, end } (samples)
├── Compressed Data Block
│   └── codec-specific compressed payload
└── Optional Seek Table (for Vorbis/Opus random access)
```

### 4.2 Import Pipeline

```
原始音檔 (.wav/.ogg/.mp3/.flac)
        │
        ▼
┌─────────────────────────┐
│  UNAudioImporter        │  ← Unity AssetPostprocessor / ScriptedImporter
│  (Editor only)          │
│                         │
│  1. 讀取原始音訊        │
│  2. Resample (if needed)│
│  3. 編碼為目標 codec    │
│  4. 生成 .unac asset    │
│  5. 建立 seek table     │
└────────┬────────────────┘
         ▼
  Assets/Audio/*.unac      ← 可被 Unity 版控、Addressables 管理
```

**關鍵設計決策：**

- 使用 `ScriptedImporter` 註冊 `.unac` 副檔名，讓 Unity Editor 原生識別
- Import settings 可在 Inspector 中調整（目標 codec、品質、sample rate）
- 支援 **Import-time encode**：原始檔留在專案中，`.unac` 為 import 產物
- 也支援 **Pre-encoded import**：直接匯入已編碼的 `.unac` 檔案

### 4.3 Codec 選擇策略

| 用途 | 建議 Codec | 理由 |
|------|-----------|------|
| **SFX（短音效）** | ADPCM 或 PCM16 | 解碼極快、零延遲，檔案較小 |
| **音樂 / BGM** | Vorbis (OGG) | 壓縮率高、品質好、開源免費 |
| **語音 / 對話** | Opus | 最佳低位元率品質、低延遲設計 |
| **大量重複音效** | ADPCM | 記憶體效率與解碼速度最佳平衡 |

### 4.4 記憶體載入策略：Compressed In Memory

```
UNAudioClip 載入流程：

1. Load Phase (main thread or async)
   ├── 從 disk 讀取 .unac 完整檔案
   ├── 壓縮資料保留在 native heap（不解壓）
   └── 建立 codec decoder context

2. Play Phase (audio thread)
   ├── 從 compressed buffer 逐 frame 解碼
   ├── 解碼輸出至 per-voice PCM ring buffer
   └── Mixer 從 ring buffer 拉取混音

記憶體佈局：
┌──────────────────────────────────────┐
│          Native Heap                  │
│                                      │
│  ┌─────────────────────┐             │
│  │ Compressed Data     │  ← 常駐記憶體 │
│  │ (Vorbis/Opus/ADPCM) │             │
│  └─────────────────────┘             │
│                                      │
│  ┌─────────────────────┐             │
│  │ PCM Decode Buffer   │  ← 每個 voice │
│  │ (Ring Buffer, ~4KB) │    獨立 buffer │
│  └─────────────────────┘             │
└──────────────────────────────────────┘
```

---

## 5. Engine Core 設計

### 5.1 Threading Model

```
┌─────────────┐     Command Queue      ┌──────────────┐
│ Main Thread  │ ──────────────────────▶│ Audio Thread  │
│ (C# / Unity) │  (lock-free SPSC)     │ (Native)      │
└─────────────┘                        └──────┬───────┘
                                              │
       ┌──────────────────────────────────────┤
       │                                      │
       ▼                                      ▼
┌──────────────┐                     ┌──────────────┐
│ Decode Thread │                     │ Platform     │
│ (per codec   │                     │ Audio Output │
│  or pooled)  │                     │ Callback     │
└──────────────┘                     └──────────────┘
```

**核心原則：Audio Thread 零阻塞（No Allocation, No Lock, No System Call）**

| 執行緒 | 職責 | 阻塞限制 |
|--------|------|---------|
| **Main Thread** | 發送 command（Play/Stop/SetParam）、asset 載入 | 可阻塞 |
| **Audio Thread** | 混音、DSP 處理、輸出 buffer 填充 | **禁止阻塞** |
| **Decode Thread(s)** | 解壓縮音訊資料，填充 per-voice ring buffer | 可阻塞（I/O） |

### 5.2 Command Queue（Main → Audio Thread）

```cpp
// Lock-free SPSC ring buffer
struct AudioCommand {
    enum Type : uint8_t {
        Play, Stop, Pause, Resume,
        SetVolume, SetPitch, SetPan,
        SetBus, FadeVolume,
        StopAll
    };
    Type     type;
    uint32_t voice_id;
    float    param0;
    float    param1;
    float    duration;  // for fades
};
```

### 5.3 Voice Pool

```
Voice Pool (固定大小，如 64 voices)
┌─────┬─────┬─────┬─────┬─────┬─────┐
│ V0  │ V1  │ V2  │ ... │ V62 │ V63 │
└─────┴─────┴─────┴─────┴─────┴─────┘
  │
  ▼
Voice State:
  ├── clip_ref        → 指向 compressed data
  ├── decoder_state   → codec-specific 解碼狀態
  ├── ring_buffer     → PCM output (decode → audio thread)
  ├── playback_pos    → 目前播放位置 (samples)
  ├── volume / pitch / pan
  ├── bus_id          → 輸出至哪個 mixer bus
  ├── state           → { Free, Playing, Paused, Stopping }
  └── priority        → voice stealing 優先序
```

**Voice Stealing 策略：**
1. 優先回收 `Free` 狀態的 voice
2. 其次回收最低 priority 的 voice
3. 同 priority 則回收 volume 最小的 voice
4. 提供 `VoiceStealCallback` 讓 C# 層可自訂邏輯

### 5.4 Mixer Graph

```
                ┌─────────┐
                │ Master  │ → Platform Output
                │  Bus    │
                └────┬────┘
                     │
         ┌───────────┼───────────┐
         │           │           │
    ┌────┴────┐ ┌────┴────┐ ┌───┴─────┐
    │  Music  │ │   SFX   │ │  Voice  │
    │  Bus    │ │   Bus   │ │  Bus    │
    └────┬────┘ └────┬────┘ └────┬────┘
         │           │           │
     ┌───┴───┐   ┌───┴───┐   ┌──┴──┐
     │ V0,V1 │   │V2..V10│   │V11  │
     └───────┘   └───────┘   └─────┘

每個 Bus：
  ├── volume (float, 0.0–1.0)
  ├── mute (bool)
  ├── DSP chain (linked list of effects)
  └── child buses / voices
```

---

## 6. C# Public API 設計

### 6.1 核心 API

```csharp
// === Singleton Manager ===
public class UNAudioManager : MonoBehaviour
{
    public static UNAudioManager Instance { get; }

    // 初始化（自動在 Awake 呼叫，或手動）
    public void Initialize(UNAudioConfig config);
    public void Shutdown();

    // 播放
    public UNVoiceHandle Play(UNAudioClip clip, UNPlayParams param = default);
    public void Stop(UNVoiceHandle handle, float fadeOut = 0f);
    public void Pause(UNVoiceHandle handle);
    public void Resume(UNVoiceHandle handle);
    public void StopAll(float fadeOut = 0f);

    // 參數控制
    public void SetVolume(UNVoiceHandle handle, float volume);
    public void SetPitch(UNVoiceHandle handle, float pitch);
    public void SetPan(UNVoiceHandle handle, float pan);  // -1.0 ~ 1.0
    public void FadeTo(UNVoiceHandle handle, float targetVol, float duration);

    // Bus 控制
    public void SetBusVolume(UNBusId bus, float volume);
    public void SetBusMute(UNBusId bus, bool mute);

    // 查詢
    public bool IsPlaying(UNVoiceHandle handle);
    public float GetPlaybackPosition(UNVoiceHandle handle); // seconds
    public int ActiveVoiceCount { get; }
}

// === Voice Handle（值型別，避免 GC）===
public readonly struct UNVoiceHandle : IEquatable<UNVoiceHandle>
{
    internal readonly uint Id;        // voice slot index
    internal readonly uint Generation; // 防止 dangling reference
    public bool IsValid { get; }
    public static readonly UNVoiceHandle Invalid;
}

// === Play 參數 ===
public struct UNPlayParams
{
    public float  Volume;    // 0.0–1.0, default 1.0
    public float  Pitch;     // 0.5–2.0, default 1.0
    public float  Pan;       // -1.0–1.0, default 0.0
    public bool   Loop;      // default false
    public int    Priority;  // 0 = lowest, default 128
    public UNBusId Bus;      // default = SFX
    public float  FadeIn;    // seconds, default 0
    public float  StartTime; // seconds, default 0
}
```

### 6.2 UNAudioSource Component

```csharp
/// MonoBehaviour wrapper，可掛在 GameObject 上，提供 Inspector 操作介面
[AddComponentMenu("UNAudio/UNAudio Source")]
public class UNAudioSource : MonoBehaviour
{
    [SerializeField] private UNAudioClip clip;
    [SerializeField] private UNPlayParams defaultParams;
    [SerializeField] private bool playOnAwake;

    private UNVoiceHandle currentHandle;

    public void Play();
    public void Stop(float fadeOut = 0f);
    public void Pause();
    public void Resume();

    public float Volume { get; set; }
    public float Pitch { get; set; }
    public bool IsPlaying { get; }
}
```

### 6.3 UNAudioClip（ScriptableObject）

```csharp
/// 對應 .unac 檔案的 Unity Asset 表示
public class UNAudioClip : ScriptableObject
{
    // Import metadata (read-only in Inspector)
    public int    SampleRate  { get; }
    public int    Channels    { get; }
    public float  Duration    { get; }  // seconds
    public UNCodec Codec      { get; }
    public long   MemorySize  { get; }  // compressed size in bytes

    // Native handle (internal)
    internal IntPtr NativeHandle { get; }

    // 載入控制
    public bool   IsLoaded    { get; }
    public void   Load();               // 同步載入
    public Task   LoadAsync();           // 非同步載入
    public void   Unload();
}
```

---

## 7. Editor 工具與測試便利性

### 7.1 Editor Window：UNAudio Debug Panel

```
┌─ UNAudio Debug ──────────────────────────────────────┐
│                                                       │
│  Engine Status: ● Running   Output: 48kHz / 256 smp  │
│  Active Voices: 12 / 64     Decode Threads: 2        │
│  CPU Load: 2.3%             Latency: ~5.3 ms         │
│                                                       │
│  ┌─ Active Voices ─────────────────────────────────┐  │
│  │ ID  │ Clip        │ Bus   │ Vol │ Pos   │ State │  │
│  │ 03  │ hit_01      │ SFX   │ 0.8 │ 0.12s │ ▶    │  │
│  │ 07  │ bgm_battle  │ Music │ 1.0 │ 42.5s │ ▶    │  │
│  │ 11  │ footstep_02 │ SFX   │ 0.5 │ 0.03s │ ▶    │  │
│  └─────────────────────────────────────────────────┘  │
│                                                       │
│  ┌─ Mixer Buses ───────────────────────────────────┐  │
│  │ Master ████████████████░░░░ 0.0 dB              │  │
│  │ ├─ Music ██████████░░░░░░░░ -6.0 dB             │  │
│  │ ├─ SFX   ████████████████░░ -2.0 dB             │  │
│  │ └─ Voice ████░░░░░░░░░░░░░░ -12.0 dB            │  │
│  └─────────────────────────────────────────────────┘  │
│                                                       │
│  ┌─ Waveform Preview ──────────────────────────────┐  │
│  │  ∿∿∿∿∿∿∿∿∿∿∿∿∿▎∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿  │  │
│  └─────────────────────────────────────────────────┘  │
│                                                       │
│  [▶ Play Test] [⏹ Stop All] [🔄 Reload Engine]       │
└───────────────────────────────────────────────────────┘
```

### 7.2 Inspector 整合

- **UNAudioClip Inspector**：顯示波形預覽、metadata、壓縮率、可直接試聽
- **UNAudioSource Inspector**：播放/停止按鈕、參數即時調整、Play on Awake 設定
- **UNAudioMixer Inspector**：視覺化 bus 拓撲、即時 VU meter

### 7.3 Editor 測試設計原則

| 需求 | 做法 |
|------|------|
| **不需 Build 即可測試** | Native plugin 在 Editor 中直接載入（`Assets/Plugins/x86_64/`） |
| **Play Mode 即時音訊** | Engine 在 `EditorApplication.playModeStateChanged` 自動初始化/關閉 |
| **Edit Mode 預覽** | 提供 `[ExecuteInEditMode]` 的預覽播放功能，不依賴 Play Mode |
| **Hot Reload 安全** | Domain Reload 時安全 shutdown native engine，reload 後自動重啟 |
| **Mock/Stub 支援** | 提供 `IUNAudioBackend` 介面，Editor 可注入 null audio backend 做無聲測試 |
| **Unit Test 友善** | 核心邏輯 (mixer math, command queue) 可在 Edit Mode Test Runner 測試 |

### 7.4 Domain Reload / Assembly Reload 處理

```csharp
// 確保 native engine 在 C# domain reload 時安全關閉
#if UNITY_EDITOR
[InitializeOnLoad]
static class UNAudioEditorLifecycle
{
    static UNAudioEditorLifecycle()
    {
        AssemblyReloadEvents.beforeAssemblyReload += OnBeforeReload;
        AssemblyReloadEvents.afterAssemblyReload  += OnAfterReload;
        EditorApplication.playModeStateChanged    += OnPlayModeChanged;
    }

    static void OnBeforeReload()  => UNAudioNative.ForceShutdown();
    static void OnAfterReload()   => UNAudioNative.TryRestore();
    static void OnPlayModeChanged(PlayModeStateChange state) { /* ... */ }
}
#endif
```

---

## 8. Code Visibility（可見性與除錯性）

### 8.1 原則

- **C# 層完全開源**放在 Unity 專案內，開發者可閱讀、修改、擴展
- **Native 層以源碼形式提供**，搭配 CMake build system
- **清晰的 C ABI boundary**：所有 native 函式透過單一 header `unaudio.h` 匯出

### 8.2 Native Debug 支援

```cpp
// Callback 機制讓 native log 顯示在 Unity Console
typedef void (*UNA_LogCallback)(int level, const char* message);
UNA_API void una_set_log_callback(UNA_LogCallback callback);

// Debug stats 可從 C# 查詢
struct UNAudioStats {
    uint32_t active_voices;
    uint32_t peak_voices;
    float    cpu_load_percent;
    float    output_latency_ms;
    uint64_t total_samples_processed;
    uint32_t buffer_underruns;      // 重要！追蹤效能問題
    uint32_t commands_pending;
};
UNA_API void una_get_stats(UNAudioStats* out_stats);
```

### 8.3 Profiler Integration

- 在 `Unity.Profiling.ProfilerMarker` 標記關鍵 C# 路徑
- Native 層提供 `una_profiler_begin_sample` / `una_profiler_end_sample` 與 Unity Profiler 對接
- Buffer underrun counter 可在 Editor Debug Panel 即時顯示

---

## 9. 專案目錄結構

```
UNAudio/
├── plan.md                           ← 本企劃文件
│
├── unity/
│   └── UNAudio/                      ← Unity Package (可作為 local package 引用)
│       ├── package.json
│       ├── Runtime/
│       │   ├── UNAudioManager.cs
│       │   ├── UNAudioSource.cs
│       │   ├── UNAudioClip.cs
│       │   ├── UNVoiceHandle.cs
│       │   ├── UNPlayParams.cs
│       │   ├── UNBusId.cs
│       │   ├── UNAudioConfig.cs      ← ScriptableObject 設定檔
│       │   ├── Internal/
│       │   │   ├── UNAudioNative.cs  ← P/Invoke declarations
│       │   │   ├── NativeArray.cs    ← 記憶體管理輔助
│       │   │   └── CommandBuffer.cs  ← C# 側 command 封裝
│       │   └── Plugins/
│       │       ├── x86_64/           ← Windows .dll
│       │       ├── Android/          ← .so (arm64-v8a, armeabi-v7a)
│       │       └── iOS/              ← .a (static lib)
│       ├── Editor/
│       │   ├── UNAudioClipImporter.cs
│       │   ├── UNAudioClipEditor.cs
│       │   ├── UNAudioSourceEditor.cs
│       │   ├── UNAudioDebugWindow.cs
│       │   ├── UNAudioMixerEditor.cs
│       │   └── UNAudioEditorLifecycle.cs
│       └── Tests/
│           ├── EditMode/
│           │   ├── CommandQueueTests.cs
│           │   ├── VoiceHandleTests.cs
│           │   └── MixerMathTests.cs
│           └── PlayMode/
│               ├── PlaybackTests.cs
│               └── LatencyTests.cs
│
├── native/
│   ├── CMakeLists.txt                ← 跨平台建置
│   ├── include/
│   │   └── unaudio.h                ← 公開 C ABI header
│   ├── src/
│   │   ├── engine_core.cpp           ← 生命週期、voice pool
│   │   ├── command_queue.cpp         ← lock-free SPSC
│   │   ├── mixer.cpp                 ← mixer graph
│   │   ├── decoder_vorbis.cpp
│   │   ├── decoder_opus.cpp
│   │   ├── decoder_adpcm.cpp
│   │   ├── decoder_pcm.cpp
│   │   ├── dsp_chain.cpp
│   │   ├── ring_buffer.h             ← lock-free ring buffer (header-only)
│   │   └── platform/
│   │       ├── backend_wasapi.cpp
│   │       ├── backend_aaudio.cpp
│   │       ├── backend_coreaudio.cpp
│   │       └── backend_pulseaudio.cpp
│   ├── third_party/
│   │   ├── stb_vorbis/              ← single-file Vorbis decoder
│   │   ├── opus/                    ← Opus codec (or opusfile)
│   │   └── (無外部 dependency 為目標)
│   └── tests/
│       ├── test_ring_buffer.cpp
│       ├── test_command_queue.cpp
│       ├── test_mixer.cpp
│       └── test_decoder.cpp
│
├── tools/
│   ├── encode_unac.py               ← CLI 工具：將 .wav 轉為 .unac
│   └── batch_import.py              ← 批次匯入腳本
│
└── docs/
    ├── architecture.md
    ├── getting-started.md
    └── api-reference.md
```

---

## 10. 開發階段規劃

### Phase 1：Core Foundation（4–6 週）

- [ ] Native engine 骨架（初始化 / 關閉 / audio callback）
- [ ] WASAPI backend（Windows, Shared mode 先行）
- [ ] Lock-free command queue (SPSC)
- [ ] Voice pool + PCM 播放（先不壓縮）
- [ ] 基本 mixer（master + 2 bus）
- [ ] C# P/Invoke binding
- [ ] `UNAudioManager` + `Play()`/`Stop()` 基本功能
- [ ] Editor 中可播放 PCM audio

### Phase 2：Codec & Asset Pipeline（3–4 週）

- [ ] `.unac` 檔案格式定義與序列化
- [ ] `ScriptedImporter` for `.unac`
- [ ] stb_vorbis 整合（Vorbis decode）
- [ ] ADPCM decoder
- [ ] Compressed-in-memory 載入 + streaming decode
- [ ] `UNAudioClip` ScriptableObject
- [ ] Import settings Inspector UI

### Phase 3：Editor Tooling（2–3 週）

- [ ] UNAudio Debug Window
- [ ] UNAudioClip Inspector（波形預覽 + 試聽）
- [ ] UNAudioSource Inspector
- [ ] Domain Reload 安全處理
- [ ] Edit Mode 預覽播放
- [ ] Profiler marker 整合

### Phase 4：Advanced Features（3–4 週）

- [ ] DSP chain 框架（volume → pan → output）
- [ ] Pitch shifting（resampler）
- [ ] Fade in/out
- [ ] Voice stealing + priority system
- [ ] 3D spatial 基礎（distance attenuation + stereo panning）
- [ ] Opus decoder 整合

### Phase 5：Android & Mobile（3–4 週）

- [ ] AAudio backend
- [ ] OpenSL ES fallback
- [ ] Android .so 建置 (CMake + NDK)
- [ ] iOS Core Audio backend
- [ ] iOS static lib 建置
- [ ] 行動平台延遲測試與調校

### Phase 6：Polish & Production（2–3 週）

- [ ] WASAPI Exclusive mode（可選）
- [ ] Async asset loading
- [ ] Memory profiling + leak detection
- [ ] Stress test（64 voices 同時播放）
- [ ] 文件撰寫
- [ ] Sample project

---

## 11. 技術風險與對策

| 風險 | 衝擊 | 對策 |
|------|------|------|
| Audio thread stall（GC / lock） | 爆音、斷音 | 嚴格零分配政策；command queue lock-free |
| Domain Reload crash | Editor 崩潰 | `ForceShutdown` + 全 native 資源追蹤清理 |
| Android 碎片化延遲差異 | 部分裝置高延遲 | AAudio performance mode + fallback buffer 大小調整 |
| Codec 解碼跟不上播放速度 | 斷音 | Decode thread 預填充 ring buffer；buffer underrun 監控 |
| Native plugin 載入失敗 | 功能不可用 | Graceful fallback + 明確錯誤訊息 |
| stb_vorbis seek 精度 | Loop 接縫 | 自建 seek table + crossfade loop |

---

## 12. 第三方依賴

| Library | 用途 | 授權 |
|---------|------|------|
| **stb_vorbis** | OGG Vorbis 解碼 | Public Domain |
| **Opus / opusfile** | Opus 解碼 | BSD |
| **Google Test** | Native 單元測試 | BSD |

> 目標：最小化外部依賴，核心 mixer/DSP/ring buffer 完全自寫。

---

## 13. 效能目標

| 指標 | 目標值 |
|------|--------|
| Output latency (Windows) | < 10 ms |
| Output latency (Android, good devices) | < 15 ms |
| Voice count (simultaneous) | 64 |
| Mixer CPU (64 voices, 48kHz) | < 5% single core |
| Memory per compressed clip (1 min music) | < 1 MB (Vorbis q5) |
| Memory per voice decode buffer | ~4 KB (256 samples × 2ch × 8 bytes) |
| Command queue throughput | > 10,000 commands/sec |

---

## 14. 命名規範

| 範疇 | 規範 | 範例 |
|------|------|------|
| C# public class | PascalCase, `UN` prefix | `UNAudioManager` |
| C# internal | PascalCase | `CommandBuffer` |
| C ABI function | snake_case, `una_` prefix | `una_engine_init()` |
| C struct | PascalCase, `UNA` prefix | `UNAudioStats` |
| C enum | `UNA_` prefix + UPPER_SNAKE | `UNA_CODEC_VORBIS` |
| Native C++ internal | snake_case | `voice_pool::allocate()` |
| Files | snake_case | `engine_core.cpp` |
| Unity asset | PascalCase | `UNAudioConfig.asset` |
