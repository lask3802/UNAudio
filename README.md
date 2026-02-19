# UNAudio - Unity Native Audio Engine

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Unity](https://img.shields.io/badge/Unity-2020.3%2B-green.svg)](https://unity.com/)

## 概述 (Overview)

UNAudio 是一個專為 Unity 設計的高性能、低延遲原生音頻引擎。本項目致力於提供優於 Unity 內建音頻系統的性能，同時保持易用性和靈活性。

UNAudio is a high-performance, low-latency native audio engine designed specifically for Unity. This project aims to provide superior performance compared to Unity's built-in audio system while maintaining ease of use and flexibility.

### 核心特性 (Core Features)

- **超低延遲** (Ultra-Low Latency): 音頻延遲 < 10ms，適合音樂遊戲和實時互動應用
- **原生性能** (Native Performance): C/C++ 核心引擎，通過 P/Invoke 與 Unity 整合
- **壓縮格式支援** (Compressed Format Support): 支援在記憶體中直接播放壓縮音頻（MP3, Vorbis, FLAC）
- **靈活的資源管線** (Flexible Asset Pipeline): 自定義 Unity Asset Pipeline 整合
- **編輯器整合** (Editor Integration): 完整的編輯器工具和實時預覽功能
- **開源透明** (Open Source): 完整的代碼可見性和可自定義性

---

## 技術架構 (Technical Architecture)

### 1. 系統架構圖 (System Architecture)

```
┌─────────────────────────────────────────────────────────────┐
│                     Unity C# Layer                          │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │ AudioSource  │  │ AudioClip    │  │ AudioMixer   │     │
│  │   Manager    │  │   Manager    │  │   Manager    │     │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘     │
│         │                 │                  │              │
│         └─────────────────┴──────────────────┘              │
│                           │                                 │
│                  ┌────────▼────────┐                        │
│                  │  P/Invoke Bridge │                        │
│                  └────────┬────────┘                        │
└───────────────────────────┼─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│                  Native C/C++ Layer                         │
│  ┌──────────────────────────────────────────────────────┐  │
│  │              UNAudio Core Engine                      │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  │  │
│  │  │   Decoder   │  │   Mixer     │  │   Output    │  │  │
│  │  │   Module    │  │   Module    │  │   Module    │  │  │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  │  │
│  └──────────────────────────────────────────────────────┘  │
│                           │                                 │
│  ┌────────────────────────▼──────────────────────────────┐ │
│  │              Audio Hardware Layer                     │ │
│  │  (WASAPI/CoreAudio/ALSA/AAudio)                      │ │
│  └───────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

### 2. 核心模組設計 (Core Module Design)

#### 2.1 解碼器模組 (Decoder Module)

**責任**: 處理各種音頻格式的解碼，支援流式解碼和即時解壓

**支援格式**:
- **無壓縮格式**: WAV, PCM
- **有損壓縮**: MP3 (libmpg123), Vorbis (libvorbis), Opus
- **無損壓縮**: FLAC (libflac), ALAC

**關鍵特性**:
```cpp
class AudioDecoder {
public:
    virtual bool Open(const char* data, size_t size) = 0;
    virtual int Decode(float* buffer, int frameCount) = 0;
    virtual bool Seek(int64_t frame) = 0;
    virtual AudioFormat GetFormat() const = 0;
    
    // 支援串流解碼，減少記憶體占用
    virtual bool SupportsStreaming() const = 0;
};
```

#### 2.2 混音器模組 (Mixer Module)

**責任**: 高效能多軌混音，支援實時效果處理

**特性**:
- 多軌並行混音 (SIMD 優化)
- 3D 空間音效計算
- 動態音量控制和淡入淡出
- 效果鏈處理 (EQ, Reverb, Compression)

```cpp
class AudioMixer {
public:
    void AddSource(AudioSourceHandle source);
    void RemoveSource(AudioSourceHandle source);
    void Process(float* outputBuffer, int frameCount);
    
    // 效果處理
    void AddEffect(EffectType type, EffectParams params);
    void SetMasterVolume(float volume);
};
```

#### 2.3 輸出模組 (Output Module)

**責任**: 與平台音頻 API 交互，確保低延遲輸出

**平台支援**:
- **Windows**: WASAPI (低延遲模式)
- **macOS/iOS**: CoreAudio
- **Linux**: ALSA / PulseAudio
- **Android**: AAudio / OpenSL ES

**配置**:
```cpp
struct AudioOutputConfig {
    int sampleRate;        // 44100, 48000, 96000
    int channels;          // 1 (Mono), 2 (Stereo), 6 (5.1), 8 (7.1)
    int bufferSize;        // 64, 128, 256, 512 (frames)
    int bufferCount;       // 2, 3, 4 (double/triple buffering)
    bool exclusiveMode;    // WASAPI exclusive mode for minimum latency
};
```

---

## Asset Pipeline 整合 (Asset Pipeline Integration)

### 1. 自定義資源匯入器 (Custom Asset Importer)

```csharp
[ScriptedImporter(1, "mp3")]
public class UNAudioImporter : ScriptedImporter
{
    public enum CompressionMode
    {
        KeepOriginal,        // 保持原始壓縮格式
        DecompressOnLoad,    // 載入時解壓
        StreamFromDisk       // 串流播放
    }
    
    public CompressionMode compressionMode = CompressionMode.KeepOriginal;
    public bool preloadAudioData = false;
    public bool loadInBackground = true;
    
    public override void OnImportAsset(AssetImportContext ctx)
    {
        // 1. 讀取原始音頻檔案
        byte[] audioData = File.ReadAllBytes(ctx.assetPath);
        
        // 2. 提取音頻元數據
        AudioMetadata metadata = ExtractMetadata(audioData);
        
        // 3. 根據壓縮模式處理
        UNAudioClip clip = ProcessAudioData(audioData, metadata);
        
        // 4. 添加到資源
        ctx.AddObjectToAsset("main", clip);
        ctx.SetMainObject(clip);
    }
}
```

### 2. 資源格式設計 (Asset Format Design)

**UNAudioClip** 繼承自 Unity 的 ScriptableObject:

```csharp
public class UNAudioClip : ScriptableObject
{
    [SerializeField] private byte[] compressedData;
    [SerializeField] private AudioMetadata metadata;
    [SerializeField] private CompressionFormat format;
    
    public int sampleRate => metadata.sampleRate;
    public int channels => metadata.channels;
    public float length => metadata.lengthInSeconds;
    
    // 記憶體中壓縮資料支援
    public bool IsCompressed => format != CompressionFormat.PCM;
    
    // 延遲載入支援
    public void LoadAudioData() { }
    public void UnloadAudioData() { }
}
```

### 3. 建置流程整合 (Build Pipeline Integration)

```csharp
public class UNAudioBuildProcessor : IPreprocessBuildWithReport
{
    public int callbackOrder => 0;
    
    public void OnPreprocessBuild(BuildReport report)
    {
        // 1. 收集所有 UNAudioClip
        var clips = FindAllAudioClips();
        
        // 2. 根據平台優化音頻格式
        foreach (var clip in clips)
        {
            OptimizeForPlatform(clip, report.summary.platform);
        }
        
        // 3. 生成音頻資源索引
        GenerateAudioAssetIndex();
    }
}
```

---

## 編輯器測試工具 (Editor Testing Tools)

### 1. 音頻測試面板 (Audio Test Panel)

```csharp
public class UNAudioTestWindow : EditorWindow
{
    [MenuItem("Window/UNAudio/Test Panel")]
    public static void ShowWindow()
    {
        GetWindow<UNAudioTestWindow>("UNAudio Test");
    }
    
    private void OnGUI()
    {
        // 延遲測試
        GUILayout.Label("Latency Test", EditorStyles.boldLabel);
        if (GUILayout.Button("Measure Latency"))
        {
            float latency = MeasureAudioLatency();
            Debug.Log($"Audio Latency: {latency}ms");
        }
        
        // 效能測試
        GUILayout.Label("Performance Test", EditorStyles.boldLabel);
        if (GUILayout.Button("CPU Usage Test"))
        {
            RunCPUBenchmark();
        }
        
        // 格式測試
        GUILayout.Label("Format Test", EditorStyles.boldLabel);
        testClip = EditorGUILayout.ObjectField("Test Clip", testClip, 
                                               typeof(UNAudioClip), false) as UNAudioClip;
        if (GUILayout.Button("Test Decode"))
        {
            TestAudioDecode(testClip);
        }
    }
}
```

### 2. 實時波形顯示 (Real-time Waveform Display)

```csharp
public class AudioWaveformView : EditorWindow
{
    private Texture2D waveformTexture;
    private float[] audioSamples;
    
    void OnGUI()
    {
        // 繪製波形
        if (waveformTexture != null)
        {
            GUI.DrawTexture(new Rect(0, 0, position.width, position.height), 
                          waveformTexture);
        }
        
        // 顯示音頻統計資訊
        GUILayout.BeginArea(new Rect(10, 10, 200, 100));
        GUILayout.Label($"Peak: {GetPeakLevel():F2} dB");
        GUILayout.Label($"RMS: {GetRMSLevel():F2} dB");
        GUILayout.Label($"Latency: {GetCurrentLatency():F1} ms");
        GUILayout.EndArea();
    }
}
```

### 3. 自動化測試框架 (Automated Testing Framework)

```csharp
public class UNAudioTests
{
    [Test]
    public void TestMP3Decode()
    {
        // 載入測試 MP3 檔案
        var clip = LoadTestClip("test_audio.mp3");
        Assert.IsNotNull(clip);
        
        // 驗證解碼
        float[] samples = new float[1024];
        int decoded = clip.ReadSamples(samples, 0, 1024);
        Assert.AreEqual(1024, decoded);
    }
    
    [Test]
    public void TestLowLatency()
    {
        var latency = MeasureRoundTripLatency();
        Assert.Less(latency, 10.0f, "Latency should be less than 10ms");
    }
    
    [Test]
    public void TestMemoryCompression()
    {
        var clip = LoadTestClip("test_audio.mp3");
        long compressedSize = clip.GetMemorySize();
        
        clip.Decompress();
        long decompressedSize = clip.GetMemorySize();
        
        Assert.Less(compressedSize, decompressedSize * 0.2f);
    }
}
```

---

## 代碼可見性與架構 (Code Visibility & Architecture)

### 1. 專案結構 (Project Structure)

```
UNAudio/
├── Runtime/                      # 執行時程式碼
│   ├── Scripts/
│   │   ├── Core/
│   │   │   ├── UNAudioClip.cs
│   │   │   ├── UNAudioSource.cs
│   │   │   └── UNAudioListener.cs
│   │   ├── API/
│   │   │   └── UNAudioEngine.cs
│   │   └── Utilities/
│   │       └── AudioUtility.cs
│   └── Plugins/                  # 原生插件
│       ├── Windows/
│       │   └── UNAudio.dll
│       ├── macOS/
│       │   └── UNAudio.bundle
│       ├── Linux/
│       │   └── libUNAudio.so
│       ├── Android/
│       │   └── libUNAudio.so
│       └── iOS/
│           └── UNAudio.framework
├── Editor/                       # 編輯器程式碼
│   ├── Scripts/
│   │   ├── UNAudioImporter.cs
│   │   ├── UNAudioInspector.cs
│   │   └── UNAudioTestWindow.cs
│   └── Resources/
│       └── Icons/
├── Native/                       # 原生 C++ 程式碼
│   ├── Source/
│   │   ├── Core/
│   │   │   ├── AudioEngine.cpp
│   │   │   ├── AudioEngine.h
│   │   │   └── AudioTypes.h
│   │   ├── Decoder/
│   │   │   ├── MP3Decoder.cpp
│   │   │   ├── VorbisDecoder.cpp
│   │   │   └── FLACDecoder.cpp
│   │   ├── Mixer/
│   │   │   ├── AudioMixer.cpp
│   │   │   └── AudioMixer.h
│   │   └── Platform/
│   │       ├── Windows/
│   │       │   └── WASAPIOutput.cpp
│   │       ├── macOS/
│   │       │   └── CoreAudioOutput.cpp
│   │       └── Linux/
│   │           └── ALSAOutput.cpp
│   ├── ThirdParty/              # 第三方庫
│   │   ├── libmpg123/
│   │   ├── libvorbis/
│   │   └── libflac/
│   └── CMakeLists.txt
├── Tests/                       # 單元測試
│   ├── Runtime/
│   │   └── UNAudioRuntimeTests.cs
│   └── Editor/
│       └── UNAudioEditorTests.cs
├── Documentation/               # 文件
│   ├── API.md
│   ├── GettingStarted.md
│   └── PerformanceGuide.md
├── Samples~/                    # 範例專案
│   ├── BasicPlayback/
│   ├── 3DAudio/
│   └── MusicGame/
└── package.json                 # UPM 套件定義
```

### 2. API 設計原則 (API Design Principles)

**簡潔易用**:
```csharp
// 簡單播放
UNAudioSource.PlayOneShot(audioClip);

// 3D 音效
var source = gameObject.AddComponent<UNAudioSource>();
source.clip = audioClip;
source.spatialBlend = 1.0f;  // 完全 3D
source.Play();
```

**進階控制**:
```csharp
// 低階 API 存取
var engine = UNAudioEngine.Instance;
engine.SetBufferSize(128);  // 設定緩衝大小
engine.SetOutputDevice("ASIO Device");  // 選擇音頻設備

// 直接音頻串流
var stream = engine.CreateAudioStream(sampleRate: 48000, channels: 2);
stream.Write(audioData, 0, audioData.Length);
```

### 3. 除錯與分析工具 (Debugging & Profiling)

```csharp
public static class UNAudioDebug
{
    // 啟用詳細日誌
    public static bool EnableVerboseLogging { get; set; }
    
    // 效能分析
    public static AudioPerformanceStats GetPerformanceStats()
    {
        return new AudioPerformanceStats
        {
            cpuUsage = GetCPUUsage(),
            bufferUnderruns = GetUnderrunCount(),
            activeVoices = GetActiveVoiceCount(),
            memoryUsage = GetMemoryUsage()
        };
    }
    
    // 音頻路徑追蹤
    public static void TraceAudioPath(UNAudioSource source)
    {
        Debug.Log($"Source: {source.name}");
        Debug.Log($"Clip: {source.clip.name} ({source.clip.format})");
        Debug.Log($"Output Device: {GetOutputDevice()}");
        Debug.Log($"Latency: {GetLatency()}ms");
    }
}
```

---

## 壓縮音頻記憶體支援 (Compressed Audio In-Memory Support)

### 1. 記憶體管理策略 (Memory Management Strategy)

**三種載入模式**:

1. **完全壓縮** (Fully Compressed)
   - 音頻資料以壓縮格式儲存在記憶體
   - 播放時即時解碼
   - 最低記憶體占用，適合大量音效

2. **預先解壓** (Preloaded Decompressed)
   - 載入時解壓到 PCM 格式
   - 播放時直接讀取
   - 最低 CPU 占用，適合頻繁播放的音效

3. **串流播放** (Streaming)
   - 從磁碟串流讀取並即時解碼
   - 最低記憶體占用
   - 適合背景音樂和大型音檔

```csharp
public enum AudioLoadType
{
    CompressedInMemory,      // 壓縮在記憶體，播放時解碼
    DecompressOnLoad,        // 載入時解壓
    Streaming                // 串流播放
}

public class UNAudioClip : ScriptableObject
{
    [SerializeField] private AudioLoadType loadType;
    
    public void SetLoadType(AudioLoadType type)
    {
        loadType = type;
        ReloadAudioData();
    }
}
```

### 2. 即時解碼器 (Real-time Decoder)

```cpp
class StreamingDecoder {
private:
    CircularBuffer<float> decodedBuffer;  // 解碼緩衝區
    std::thread decoderThread;             // 解碼執行緒
    std::atomic<bool> isRunning;
    
public:
    StreamingDecoder(int bufferSize) 
        : decodedBuffer(bufferSize), isRunning(false) {}
    
    void Start() {
        isRunning = true;
        decoderThread = std::thread([this]() {
            while (isRunning) {
                // 持續解碼到緩衝區
                DecodeNextBlock();
            }
        });
    }
    
    int Read(float* output, int frameCount) {
        // 從緩衝區讀取已解碼的資料
        return decodedBuffer.Read(output, frameCount);
    }
    
    void Stop() {
        isRunning = false;
        if (decoderThread.joinable()) {
            decoderThread.join();
        }
    }
};
```

### 3. 記憶體池管理 (Memory Pool Management)

```cpp
class AudioMemoryPool {
private:
    struct MemoryBlock {
        void* data;
        size_t size;
        bool inUse;
    };
    
    std::vector<MemoryBlock> blocks;
    std::mutex mutex;
    
public:
    void* Allocate(size_t size) {
        std::lock_guard<std::mutex> lock(mutex);
        
        // 尋找可重用的區塊
        for (auto& block : blocks) {
            if (!block.inUse && block.size >= size) {
                block.inUse = true;
                return block.data;
            }
        }
        
        // 分配新區塊
        void* data = malloc(size);
        blocks.push_back({data, size, true});
        return data;
    }
    
    void Free(void* ptr) {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& block : blocks) {
            if (block.data == ptr) {
                block.inUse = false;
                return;
            }
        }
    }
};
```

---

## 實作路線圖 (Implementation Roadmap)

### Phase 1: 核心引擎開發 (Core Engine Development) - 2 個月

**Week 1-2: 基礎架構**
- [ ] 建立 CMake 建置系統
- [ ] 實作基本音頻輸出 (WASAPI/CoreAudio)
- [ ] 實作簡單的 PCM 播放器
- [ ] 建立 P/Invoke 橋接層

**Week 3-4: 解碼器實作**
- [ ] 整合 libmpg123 (MP3 解碼)
- [ ] 整合 libvorbis (Vorbis 解碼)
- [ ] 整合 libflac (FLAC 解碼)
- [ ] 實作解碼器工廠模式

**Week 5-6: 混音器開發**
- [ ] 實作多軌混音器
- [ ] 實作音量控制和淡入淡出
- [ ] 實作基本 3D 音效計算
- [ ] SIMD 優化 (SSE/NEON)

**Week 7-8: 測試與優化**
- [ ] 效能測試和優化
- [ ] 延遲測試和調整
- [ ] 記憶體洩漏檢測
- [ ] 跨平台測試

### Phase 2: Unity 整合 (Unity Integration) - 1.5 個月

**Week 9-10: C# API 層**
- [ ] 實作 UNAudioClip
- [ ] 實作 UNAudioSource
- [ ] 實作 UNAudioListener
- [ ] 實作 UNAudioEngine

**Week 11-12: Asset Pipeline**
- [ ] 實作自定義 Asset Importer
- [ ] 實作 Build Processor
- [ ] 支援多種音頻格式匯入
- [ ] 實作資源壓縮選項

**Week 13-14: Editor 工具**
- [ ] 實作 Audio Inspector
- [ ] 實作 Test Window
- [ ] 實作 Waveform Viewer
- [ ] 實作效能分析器

### Phase 3: 進階功能 (Advanced Features) - 1.5 個月

**Week 15-16: 壓縮音頻支援**
- [ ] 實作記憶體中壓縮播放
- [ ] 實作串流播放
- [ ] 實作記憶體池管理
- [ ] 實作智慧快取策略

**Week 17-18: 效果處理**
- [ ] 實作基本 EQ
- [ ] 實作 Reverb
- [ ] 實作 Compressor
- [ ] 實作效果鏈系統

**Week 19-20: 3D 音效增強**
- [ ] 實作 HRTF (頭部相關傳輸函數)
- [ ] 實作遮蔽和反射
- [ ] 實作多普勒效應
- [ ] 實作環境音效系統

### Phase 4: 測試與發布 (Testing & Release) - 1 個月

**Week 21-22: 完整測試**
- [ ] 單元測試覆蓋率 > 80%
- [ ] 整合測試
- [ ] 效能基準測試
- [ ] 跨平台兼容性測試

**Week 23-24: 文件與範例**
- [ ] API 文件
- [ ] 使用教學
- [ ] 範例專案
- [ ] 效能優化指南

**Week 25: 發布準備**
- [ ] 版本 1.0 發布
- [ ] 發布到 Unity Asset Store
- [ ] 發布到 GitHub
- [ ] 社群支援建立

---

## 效能目標 (Performance Targets)

### 延遲目標 (Latency Targets)

| 平台 | 目標延遲 | 緩衝設定 |
|------|----------|----------|
| Windows (WASAPI Exclusive) | < 5ms | 64 frames @ 48kHz |
| Windows (WASAPI Shared) | < 10ms | 128 frames @ 48kHz |
| macOS (CoreAudio) | < 8ms | 128 frames @ 48kHz |
| iOS (CoreAudio) | < 10ms | 256 frames @ 48kHz |
| Android (AAudio) | < 15ms | 256 frames @ 48kHz |
| Linux (ALSA) | < 12ms | 256 frames @ 48kHz |

### CPU 使用率目標 (CPU Usage Targets)

- **空閒狀態**: < 0.1% CPU
- **播放 10 個音源**: < 2% CPU (單核心)
- **播放 50 個音源**: < 8% CPU (單核心)
- **播放 100 個音源**: < 15% CPU (單核心)

### 記憶體使用目標 (Memory Usage Targets)

| 音頻長度 | 壓縮格式 (MP3) | 未壓縮 (PCM) | 節省比例 |
|----------|----------------|--------------|----------|
| 1 分鐘 | ~1 MB | ~10 MB | 90% |
| 5 分鐘 | ~5 MB | ~50 MB | 90% |
| 30 分鐘 | ~30 MB | ~300 MB | 90% |

---

## API 使用範例 (API Usage Examples)

### 基本播放 (Basic Playback)

```csharp
using UNAudio;

public class AudioPlayer : MonoBehaviour
{
    public UNAudioClip audioClip;
    private UNAudioSource audioSource;
    
    void Start()
    {
        // 方法 1: 使用組件
        audioSource = gameObject.AddComponent<UNAudioSource>();
        audioSource.clip = audioClip;
        audioSource.Play();
        
        // 方法 2: 一次性播放
        UNAudioSource.PlayClipAtPoint(audioClip, transform.position);
        
        // 方法 3: 使用靜態方法
        UNAudioSource.PlayOneShot(audioClip);
    }
}
```

### 3D 音效 (3D Audio)

```csharp
public class SpatialAudioExample : MonoBehaviour
{
    void Start()
    {
        var source = gameObject.AddComponent<UNAudioSource>();
        source.clip = audioClip;
        
        // 啟用 3D 音效
        source.spatialBlend = 1.0f;  // 0 = 2D, 1 = 3D
        
        // 設定 3D 參數
        source.minDistance = 1.0f;
        source.maxDistance = 50.0f;
        source.rolloffMode = AudioRolloffMode.Logarithmic;
        
        // 啟用多普勒效應
        source.dopplerLevel = 1.0f;
        
        source.Play();
    }
}
```

### 壓縮音頻管理 (Compressed Audio Management)

```csharp
public class AudioManager : MonoBehaviour
{
    public UNAudioClip backgroundMusic;
    public UNAudioClip[] soundEffects;
    
    void Start()
    {
        // 背景音樂使用串流播放（節省記憶體）
        backgroundMusic.SetLoadType(AudioLoadType.Streaming);
        
        // 音效預先解壓（降低 CPU 占用）
        foreach (var sfx in soundEffects)
        {
            sfx.SetLoadType(AudioLoadType.DecompressOnLoad);
            sfx.LoadAudioData();
        }
    }
    
    void OnDestroy()
    {
        // 釋放記憶體
        foreach (var sfx in soundEffects)
        {
            sfx.UnloadAudioData();
        }
    }
}
```

### 動態混音 (Dynamic Mixing)

```csharp
public class MusicMixer : MonoBehaviour
{
    private UNAudioSource[] musicTracks;
    
    void Start()
    {
        // 創建多軌音樂
        musicTracks = new UNAudioSource[4];
        for (int i = 0; i < 4; i++)
        {
            musicTracks[i] = gameObject.AddComponent<UNAudioSource>();
            musicTracks[i].clip = musicClips[i];
            musicTracks[i].loop = true;
            musicTracks[i].volume = 0f;
            musicTracks[i].Play();
        }
    }
    
    public void CrossfadeTrack(int trackIndex, float duration)
    {
        StartCoroutine(CrossfadeCoroutine(trackIndex, duration));
    }
    
    IEnumerator CrossfadeCoroutine(int trackIndex, float duration)
    {
        float elapsed = 0f;
        while (elapsed < duration)
        {
            elapsed += Time.deltaTime;
            float t = elapsed / duration;
            
            for (int i = 0; i < musicTracks.Length; i++)
            {
                musicTracks[i].volume = (i == trackIndex) ? t : (1 - t);
            }
            
            yield return null;
        }
    }
}
```

### 低階音頻存取 (Low-level Audio Access)

```csharp
public class AudioStreamExample : MonoBehaviour
{
    private UNAudioStream stream;
    
    void Start()
    {
        // 創建音頻串流
        stream = UNAudioEngine.Instance.CreateStream(
            sampleRate: 48000,
            channels: 2,
            bufferSize: 1024
        );
        
        // 生成正弦波
        float frequency = 440f; // A4 note
        float[] buffer = new float[1024];
        
        for (int i = 0; i < buffer.Length; i++)
        {
            float t = i / 48000f;
            buffer[i] = Mathf.Sin(2 * Mathf.PI * frequency * t);
        }
        
        // 寫入音頻資料
        stream.Write(buffer, 0, buffer.Length);
        stream.Play();
    }
    
    void OnDestroy()
    {
        stream?.Dispose();
    }
}
```

---

## 建置說明 (Build Instructions)

### 原生庫建置 (Native Library Build)

#### Windows

```bash
cd Native
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

#### macOS

```bash
cd Native
mkdir build && cd build
cmake .. -G Xcode
cmake --build . --config Release
```

#### Linux

```bash
cd Native
mkdir build && cd build
cmake .. -G "Unix Makefiles"
cmake --build . --config Release
```

### Unity 套件安裝 (Unity Package Installation)

**方法 1: Unity Package Manager (推薦)**

1. 開啟 Unity Package Manager
2. 點擊 "+" -> "Add package from git URL"
3. 輸入: `https://github.com/lask3802/UNAudio.git`

**方法 2: 手動安裝**

1. 下載最新版本
2. 解壓到 `Packages/com.unaudio.core/`
3. Unity 會自動偵測並載入

---

## 依賴項目 (Dependencies)

### 原生依賴 (Native Dependencies)

- **libmpg123**: MP3 解碼 (LGPL License)
- **libvorbis**: Vorbis 解碼 (BSD License)
- **libflac**: FLAC 解碼 (BSD License)
- **miniaudio**: 跨平台音頻抽象層 (MIT License)

### Unity 依賴 (Unity Dependencies)

- Unity 2020.3 或更高版本
- .NET Standard 2.1

---

## 授權條款 (License)

本專案採用 MIT 授權條款。詳見 [LICENSE](LICENSE) 檔案。

---

## 貢獻指南 (Contributing)

我們歡迎社群貢獻！請參閱 [CONTRIBUTING.md](CONTRIBUTING.md) 了解如何參與開發。

### 開發流程

1. Fork 本專案
2. 創建功能分支 (`git checkout -b feature/AmazingFeature`)
3. 提交變更 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 開啟 Pull Request

---

## 支援與社群 (Support & Community)

- **問題回報**: [GitHub Issues](https://github.com/lask3802/UNAudio/issues)
- **討論區**: [GitHub Discussions](https://github.com/lask3802/UNAudio/discussions)
- **文件**: [Wiki](https://github.com/lask3802/UNAudio/wiki)

---

## 致謝 (Acknowledgments)

感謝以下開源專案的貢獻:
- libmpg123
- libvorbis
- libflac
- miniaudio

---

## 版本歷史 (Version History)

### v1.0.0 (計劃中)
- 初始版本發布
- 支援基本音頻播放
- 支援 MP3, Vorbis, FLAC 格式
- 跨平台支援 (Windows, macOS, Linux, iOS, Android)
- Unity 編輯器整合

---

**最後更新**: 2026-02-19

**維護者**: UNAudio Team

**專案狀態**: 🚧 開發中 (In Development)
