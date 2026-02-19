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
│  │  (WASAPI/CoreAudio/ALSA/Oboe)                        │ │
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
- **Android**: Oboe (自動選擇 AAudio 或 OpenSL ES)

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

#### 2.4 行動平台專屬實作 (Mobile Platform Implementations)

##### Android 平台支援 (Android Platform Support)

**推薦使用 Oboe 函式庫**:
- **Oboe** (Google 官方): 簡化 API，自動處理 AAudio/OpenSL ES 切換
- 自動選擇最佳音頻 API (AAudio 或 OpenSL ES)
- 支援 API 16+ (Android 4.1+)，在 API 26+ 自動使用 AAudio
- 處理音頻路徑優化和延遲管理

**Oboe 實作**:
```cpp
#include <oboe/Oboe.h>

class OboeAudioOutput : public oboe::AudioStreamCallback {
private:
    std::shared_ptr<oboe::AudioStream> stream;
    
public:
    bool Initialize(int32_t sampleRate, int32_t channels) {
        oboe::AudioStreamBuilder builder;
        
        // 設定音頻參數
        builder.setDirection(oboe::Direction::Output)
               ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
               ->setSharingMode(oboe::SharingMode::Exclusive)
               ->setSampleRate(sampleRate)
               ->setChannelCount(channels)
               ->setFormat(oboe::AudioFormat::Float)
               ->setCallback(this);
        
        // 開啟音頻流
        oboe::Result result = builder.openStream(stream);
        if (result != oboe::Result::OK) {
            return false;
        }
        
        // 啟動音頻流
        result = stream->requestStart();
        return result == oboe::Result::OK;
    }
    
    // Oboe 回調函數
    oboe::DataCallbackResult onAudioReady(
        oboe::AudioStream* audioStream,
        void* audioData,
        int32_t numFrames) override {
        
        float* outputBuffer = static_cast<float*>(audioData);
        FillAudioBuffer(outputBuffer, numFrames);
        
        return oboe::DataCallbackResult::Continue;
    }
    
    void Stop() {
        if (stream) {
            stream->requestStop();
            stream->close();
        }
    }
    
    // 取得實際音頻參數
    int32_t GetSampleRate() const {
        return stream ? stream->getSampleRate() : 0;
    }
    
    int32_t GetBufferSizeInFrames() const {
        return stream ? stream->getBufferSizeInFrames() : 0;
    }
    
    // 動態調整緩衝大小以優化延遲
    void OptimizeLatency() {
        if (!stream) return;
        
        auto result = stream->setBufferSizeInFrames(
            stream->getFramesPerBurst() * 2
        );
        
        if (result) {
            // 緩衝大小已優化
        }
    }
};
```

**Oboe 優勢**:
- **自動 API 選擇**: 在 API 26+ 使用 AAudio，舊版使用 OpenSL ES
- **自動重新連接**: 處理音頻設備變更（插拔耳機）
- **延遲調優**: 自動偵測並使用最佳緩衝大小
- **穩定性**: Google 維護，已在眾多應用中驗證
- **簡化代碼**: 減少約 60% 的平台相關代碼

**Android 特性**:
- 自動裝置選擇（耳機/揚聲器）
- 低延遲音頻路徑偵測
- 動態緩衝大小調整
- 音頻焦點管理 (AudioFocus)
- 自動處理音頻中斷和恢復

**Gradle 整合**:
```gradle
android {
    defaultConfig {
        minSdkVersion 16  // Oboe 支援 API 16+，自動切換 AAudio/OpenSL ES
        ndk {
            abiFilters 'armeabi-v7a', 'arm64-v8a', 'x86', 'x86_64'
        }
    }
    
    externalNativeBuild {
        cmake {
            path "CMakeLists.txt"
            version "3.22.1"
        }
    }
}

dependencies {
    // Oboe - Google 官方低延遲音頻庫
    implementation 'com.google.oboe:oboe:1.8.1'
}
```

**CMakeLists.txt 配置**:
```cmake
cmake_minimum_required(VERSION 3.22.1)
project(UNAudio)

# 尋找 Oboe 套件
find_package(oboe REQUIRED CONFIG)

# 添加原生庫
add_library(UNAudio SHARED
    Native/Source/Android/OboeAudioOutput.cpp
    Native/Source/Core/AudioEngine.cpp
    # ... 其他源文件
)

# 連結 Oboe
target_link_libraries(UNAudio
    oboe::oboe
    log
    android
)
```

**延遲優化** (使用 Oboe):
| 裝置類型 | API 版本 | 使用 API | 典型延遲 | 緩衝設定 |
|---------|---------|---------|---------|---------|
| 高階裝置 (Pixel, Galaxy S) | API 26+ | AAudio | 10-15ms | 192 frames @ 48kHz |
| 高階裝置 (Pixel, Galaxy S) | API 16-25 | OpenSL ES | 15-20ms | 256 frames @ 48kHz |
| 中階裝置 | API 26+ | AAudio | 15-25ms | 256 frames @ 48kHz |
| 中階裝置 | API 16-25 | OpenSL ES | 25-35ms | 384 frames @ 48kHz |
| 低階裝置 | 所有版本 | 自動選擇 | 30-50ms | 512 frames @ 48kHz |

**Oboe 最佳實踐**:
```cpp
// 1. 使用 Exclusive 模式以獲得最低延遲
builder.setSharingMode(oboe::SharingMode::Exclusive);

// 2. 設定 FramesPerBurst 的倍數作為緩衝大小
int32_t framesPerBurst = stream->getFramesPerBurst();
stream->setBufferSizeInFrames(framesPerBurst * 2);

// 3. 處理音頻設備變更
void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override {
    // 自動重建音頻流
    if (error == oboe::Result::ErrorDisconnected) {
        Initialize(sampleRate, channels);
    }
}

// 4. 監控實際延遲
int64_t framesWritten = stream->getFramesWritten();
int64_t framesRead = stream->getFramesRead();
int32_t latencyFrames = framesWritten - framesRead;
double latencyMs = (latencyFrames * 1000.0) / sampleRate;
```

**參考資源**:
- Oboe 官方文件: https://github.com/google/oboe
- Oboe 最佳實踐: https://developer.android.com/ndk/guides/audio/oboe/getting-started
- Oboe 延遲調優指南: https://github.com/google/oboe/blob/master/docs/FullGuide.md

##### iOS 平台支援 (iOS Platform Support)

**CoreAudio 實作**:
```objc
class CoreAudioOutput {
private:
    AudioUnit outputUnit;
    AudioStreamBasicDescription audioFormat;
    
public:
    bool Initialize(int sampleRate, int channels) {
        // 設定音訊會話
        AVAudioSession* session = [AVAudioSession sharedInstance];
        NSError* error = nil;
        
        // 設定為低延遲播放模式
        [session setCategory:AVAudioSessionCategoryPlayback
                        mode:AVAudioSessionModeMeasurement
                     options:AVAudioSessionCategoryOptionMixWithOthers
                       error:&error];
        
        // 設定較小的緩衝區以降低延遲
        [session setPreferredIOBufferDuration:0.005 error:&error];  // 5ms
        [session setPreferredSampleRate:sampleRate error:&error];
        [session setActive:YES error:&error];
        
        // 建立 Audio Unit
        AudioComponentDescription desc;
        desc.componentType = kAudioUnitType_Output;
        desc.componentSubType = kAudioUnitSubType_RemoteIO;
        desc.componentManufacturer = kAudioUnitManufacturer_Apple;
        desc.componentFlags = 0;
        desc.componentFlagsMask = 0;
        
        AudioComponent component = AudioComponentFindNext(NULL, &desc);
        AudioComponentInstanceNew(component, &outputUnit);
        
        // 設定音訊格式
        audioFormat.mSampleRate = sampleRate;
        audioFormat.mFormatID = kAudioFormatLinearPCM;
        audioFormat.mFormatFlags = kAudioFormatFlagIsFloat | 
                                   kAudioFormatFlagIsPacked;
        audioFormat.mChannelsPerFrame = channels;
        audioFormat.mFramesPerPacket = 1;
        audioFormat.mBitsPerChannel = 32;
        audioFormat.mBytesPerFrame = channels * sizeof(float);
        audioFormat.mBytesPerPacket = audioFormat.mBytesPerFrame;
        
        AudioUnitSetProperty(outputUnit,
                           kAudioUnitProperty_StreamFormat,
                           kAudioUnitScope_Input,
                           0,
                           &audioFormat,
                           sizeof(audioFormat));
        
        // 設定回調
        AURenderCallbackStruct callback;
        callback.inputProc = RenderCallback;
        callback.inputProcRefCon = this;
        
        AudioUnitSetProperty(outputUnit,
                           kAudioUnitProperty_SetRenderCallback,
                           kAudioUnitScope_Input,
                           0,
                           &callback,
                           sizeof(callback));
        
        AudioUnitInitialize(outputUnit);
        AudioOutputUnitStart(outputUnit);
        
        return true;
    }
    
    static OSStatus RenderCallback(void* inRefCon,
                                   AudioUnitRenderActionFlags* ioActionFlags,
                                   const AudioTimeStamp* inTimeStamp,
                                   UInt32 inBusNumber,
                                   UInt32 inNumberFrames,
                                   AudioBufferList* ioData) {
        
        CoreAudioOutput* output = static_cast<CoreAudioOutput*>(inRefCon);
        float* buffer = (float*)ioData->mBuffers[0].mData;
        output->FillAudioBuffer(buffer, inNumberFrames);
        return noErr;
    }
};
```

**iOS 特性**:
- AVAudioSession 整合
- 自動音訊中斷處理（來電、鬧鐘）
- 藍牙裝置延遲補償
- 空間音訊支援 (iOS 14+)
- 背景音訊播放支援

**Info.plist 配置**:
```xml
<key>UIBackgroundModes</key>
<array>
    <string>audio</string>
</array>

<key>AVAudioSessionCategory</key>
<string>AVAudioSessionCategoryPlayback</string>
```

**延遲優化**:
| 裝置 | 典型延遲 | 緩衝設定 |
|------|---------|---------|
| iPhone 13+ | 6-8ms | 128 frames @ 48kHz |
| iPhone X-12 | 8-10ms | 256 frames @ 48kHz |
| iPad Pro | 6-8ms | 128 frames @ 48kHz |
| 舊款裝置 | 10-15ms | 256-512 frames @ 48kHz |

**Metal 加速音訊處理 (選用)**:
```objc
// 使用 Metal Performance Shaders 進行音訊 DSP
id<MTLDevice> device = MTLCreateSystemDefaultDevice();
id<MTLCommandQueue> commandQueue = [device newCommandQueue];

// 使用 Metal 進行快速卷積、FFT 等運算
MPSMatrixMultiplication* matrixMult = 
    [[MPSMatrixMultiplication alloc] initWithDevice:device
                                       transposeLeft:NO
                                      transposeRight:NO
                                          resultRows:rows
                                       resultColumns:cols
                                    interiorColumns:inner
                                              alpha:1.0
                                               beta:0.0];
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
        CompressedInMemory,  // 壓縮在記憶體中
        DecompressOnLoad,    // 載入時解壓
        Streaming            // 串流播放
    }
    
    public CompressionMode compressionMode = CompressionMode.CompressedInMemory;
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

### 4. 原生端資源讀取 (Native Asset Loading)

**從原生代碼讀取 Unity 資源**，支援 AssetBundle 和檔案系統兩種方式。

#### 4.1 從檔案系統讀取 (File System Loading)

**C# 層準備資源路徑**:
```csharp
public class NativeAssetLoader
{
    [DllImport("UNAudio")]
    private static extern bool LoadAudioFromFile(string path);
    
    public static bool LoadAudio(string assetPath)
    {
        // 轉換為絕對路徑
        string fullPath = Path.Combine(Application.streamingAssetsPath, assetPath);
        return LoadAudioFromFile(fullPath);
    }
}
```

**原生層實作**:
```cpp
// Native/Source/AssetLoader/FileLoader.h
class FileAssetLoader {
public:
    bool LoadFromFile(const char* path) {
        FILE* file = fopen(path, "rb");
        if (!file) return false;
        
        fseek(file, 0, SEEK_END);
        size_t size = ftell(file);
        fseek(file, 0, SEEK_SET);
        
        std::vector<uint8_t> buffer(size);
        fread(buffer.data(), 1, size, file);
        fclose(file);
        
        // 解析音頻資料
        return ParseAudioData(buffer.data(), size);
    }
};
```

#### 4.2 從 AssetBundle 讀取 (AssetBundle Loading)

Unity 6 的 AssetBundle 使用 LZ4 壓縮格式，需要解析 SerializedFile 格式。

**參考 AssetStudio 開源專案**: https://github.com/Perfare/AssetStudio

**AssetBundle 結構** (Unity 6):
```
AssetBundle File Format:
┌─────────────────────────────────────┐
│ Header                              │
│  - Signature: "UnityFS"             │
│  - Format Version: 6 or 7           │
│  - Unity Version: "2023.x.x"        │
│  - Bundle Size                      │
├─────────────────────────────────────┤
│ Blocks Info                         │
│  - Uncompressed Size                │
│  - Compressed Size                  │
│  - Compression Type (LZ4/LZMA/None) │
├─────────────────────────────────────┤
│ Directory Info                      │
│  - Asset Count                      │
│  - Asset Entries[]                  │
│    - Name                           │
│    - Offset                         │
│    - Size                           │
├─────────────────────────────────────┤
│ Asset Data (Compressed)             │
│  - SerializedFile Data              │
│  - Audio Clip Data                  │
└─────────────────────────────────────┘
```

**AssetBundle 讀取器實作**:
```cpp
// Native/Source/AssetLoader/AssetBundleReader.h
#include "lz4.h"  // LZ4 解壓縮庫

class AssetBundleReader {
private:
    struct BundleHeader {
        char signature[8];      // "UnityFS\0"
        uint32_t formatVersion; // 6 or 7
        char unityVersion[32];  // "2023.1.0f1"
        char bundleVersion[32]; // "6.0.0"
        uint64_t bundleSize;
        uint32_t compressedBlocksInfoSize;
        uint32_t uncompressedBlocksInfoSize;
        uint32_t flags;
    };
    
    struct BlockInfo {
        uint32_t uncompressedSize;
        uint32_t compressedSize;
        uint16_t flags;  // 0=None, 1=LZMA, 2=LZ4, 3=LZ4HC
    };
    
    struct AssetEntry {
        uint64_t offset;
        uint64_t size;
        uint32_t typeID;  // 83 = AudioClip
        char name[256];
    };
    
public:
    bool LoadBundle(const char* bundlePath) {
        FILE* file = fopen(bundlePath, "rb");
        if (!file) return false;
        
        // 1. 讀取 Header
        BundleHeader header;
        fread(&header, sizeof(BundleHeader), 1, file);
        
        if (strncmp(header.signature, "UnityFS", 7) != 0) {
            fclose(file);
            return false;
        }
        
        // 2. 讀取並解壓 BlocksInfo
        std::vector<uint8_t> compressedBlocksInfo(header.compressedBlocksInfoSize);
        fread(compressedBlocksInfo.data(), 1, header.compressedBlocksInfoSize, file);
        
        std::vector<uint8_t> blocksInfo(header.uncompressedBlocksInfoSize);
        LZ4_decompress_safe(
            (char*)compressedBlocksInfo.data(),
            (char*)blocksInfo.data(),
            header.compressedBlocksInfoSize,
            header.uncompressedBlocksInfoSize
        );
        
        // 3. 解析 Blocks
        std::vector<BlockInfo> blocks = ParseBlocksInfo(blocksInfo);
        
        // 4. 讀取並解壓所有 Blocks
        std::vector<uint8_t> assetData;
        for (const auto& block : blocks) {
            std::vector<uint8_t> compressedBlock(block.compressedSize);
            fread(compressedBlock.data(), 1, block.compressedSize, file);
            
            std::vector<uint8_t> uncompressedBlock(block.uncompressedSize);
            
            if (block.flags == 2 || block.flags == 3) {  // LZ4 or LZ4HC
                LZ4_decompress_safe(
                    (char*)compressedBlock.data(),
                    (char*)uncompressedBlock.data(),
                    block.compressedSize,
                    block.uncompressedSize
                );
            } else {
                // No compression
                memcpy(uncompressedBlock.data(), compressedBlock.data(), 
                       block.compressedSize);
            }
            
            assetData.insert(assetData.end(), 
                           uncompressedBlock.begin(), 
                           uncompressedBlock.end());
        }
        
        fclose(file);
        
        // 5. 解析 SerializedFile 並提取 AudioClip
        return ParseSerializedFile(assetData);
    }
    
private:
    bool ParseSerializedFile(const std::vector<uint8_t>& data) {
        // Unity SerializedFile 格式解析
        // 參考: https://github.com/Perfare/AssetStudio/blob/master/AssetStudio/SerializedFile.cs
        
        size_t offset = 0;
        
        // SerializedFile Header
        uint32_t metadataSize = ReadUInt32(data, offset);
        uint32_t fileSize = ReadUInt32(data, offset + 4);
        uint32_t version = ReadUInt32(data, offset + 8);
        uint32_t dataOffset = ReadUInt32(data, offset + 12);
        
        // 跳到 Type Tree
        offset += 20;
        
        // 讀取 Objects
        uint32_t objectCount = ReadUInt32(data, offset);
        offset += 4;
        
        for (uint32_t i = 0; i < objectCount; i++) {
            uint64_t pathID = ReadUInt64(data, offset);
            uint32_t byteStart = ReadUInt32(data, offset + 8);
            uint32_t byteSize = ReadUInt32(data, offset + 12);
            uint32_t typeID = ReadUInt32(data, offset + 16);
            
            // TypeID 83 = AudioClip
            if (typeID == 83) {
                ExtractAudioClip(data, dataOffset + byteStart, byteSize);
            }
            
            offset += 20;
        }
        
        return true;
    }
    
    void ExtractAudioClip(const std::vector<uint8_t>& data, 
                         size_t offset, 
                         size_t size) {
        // AudioClip 結構（簡化版）
        // - m_Name: string
        // - m_LoadType: int (0=Decompress, 1=Compressed, 2=Streaming)
        // - m_Channels: int
        // - m_Frequency: int
        // - m_BitsPerSample: int
        // - m_Length: float
        // - m_AudioData: byte[]
        
        // 解析並載入音頻資料
        // ...
    }
    
    uint32_t ReadUInt32(const std::vector<uint8_t>& data, size_t offset) {
        if (offset + sizeof(uint32_t) > data.size()) return 0;
        return *reinterpret_cast<const uint32_t*>(&data[offset]);
    }
    
    uint64_t ReadUInt64(const std::vector<uint8_t>& data, size_t offset) {
        if (offset + sizeof(uint64_t) > data.size()) return 0;
        return *reinterpret_cast<const uint64_t*>(&data[offset]);
    }
};
```

**C# 整合**:
```csharp
public class UNAudioAssetBundleLoader
{
    [DllImport("UNAudio")]
    private static extern bool LoadAudioFromBundle(string bundlePath, string assetName);
    
    [DllImport("UNAudio")]
    private static extern void UnloadBundle();
    
    public static UNAudioClip LoadFromBundle(string bundlePath, string clipName)
    {
        if (LoadAudioFromBundle(bundlePath, clipName))
        {
            // 從原生層取得音頻資料並建立 UNAudioClip
            return CreateClipFromNative();
        }
        return null;
    }
    
    [DllImport("UNAudio")]
    private static extern IntPtr CreateClipFromNative();  // 返回 IntPtr，需手動 Marshal
}
```

**CMakeLists.txt 增加 LZ4 依賴**:
```cmake
# 添加 LZ4 庫
add_subdirectory(ThirdParty/lz4)

target_link_libraries(UNAudio
    PRIVATE
        lz4
        # ... other libraries
)
```

**使用範例**:
```csharp
// 從檔案系統載入
NativeAssetLoader.LoadAudio("audio/background.mp3");

// 從 AssetBundle 載入
var clip = UNAudioAssetBundleLoader.LoadFromBundle(
    "bundles/audio.bundle",
    "background_music"
);
```

**參考資源**:
- AssetStudio: https://github.com/Perfare/AssetStudio
- Unity AssetBundle 文件: https://docs.unity3d.com/Manual/AssetBundlesIntro.html
- LZ4 壓縮庫: https://github.com/lz4/lz4

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
| Android (Oboe/AAudio) | < 15ms | 192-256 frames @ 48kHz |
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
    public UNAudioClip audioClip;
    
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
    public UNAudioClip[] musicClips;
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
