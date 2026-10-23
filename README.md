# UNAudio — Unity 低延遲原生音訊引擎

UNAudio 繞過 Unity 內建音訊，直接用各平台最低延遲的輸出 API 播放聲音，並把「現在喇叭正在播哪一個樣本」精準地交給 C#。適合音樂遊戲、節奏判定、需要樣本級對拍的玩法，以及想把大量 OGG 放在 AssetBundle 裡、又不想付 AudioClip 匯入與記憶體成本的專案。

- **低延遲輸出**：WASAPI（IAudioClient3 / exclusive）、CoreAudio（AUHAL / RemoteIO）、AAudio / OpenSL ES（Oboe）、ALSA。
- **AssetBundle 直接讀 OGG**：`TextAsset.GetData<byte>()` 的原生記憶體指標直接交給 native，不產生任何 managed `byte[]`。
- **Native 非同步解碼**：stb_vorbis + polyphase sinc 重取樣在 worker thread 執行，第一段 8192 frame 解完就能播，其餘在背景繼續。長音樂可用串流模式邊播邊解。
- **樣本精準排程**：play / stop / pause / seek / volume 都能指定 dsp frame，mixer 在 buffer 內切段套用，結果與 buffer 大小無關。
- **精準時間**：每個 callback 的「播放時間戳」經 delay-locked loop 平滑，`voice.Time` 回報已扣除輸出延遲、在 callback 之間連續前進且單調的播放位置。
- **C# 完全可控**：clip / voice handle、16 條 bus、master、事件（開始、結束、loop、資料不足）都在主執行緒回呼。

> 狀態：2.0.0 是全面重寫。Windows、Linux 已編譯並通過全部測試；Android 已交叉編譯；macOS / iOS 的 CoreAudio backend 尚未在 Apple 工具鏈上編譯驗證，見 [平台相容性](Documentation/PlatformCompatibility.md)。

## 安裝

需求：Unity 6000.0 以上。

1. Package Manager → Add package from git URL：
   ```
   https://github.com/lask3802/UNAudio.git
   ```
2. 放入原生外掛。套件不含預先編好的 binary，請從 CI artifact 取得或自行編譯（見 [編譯原生外掛](#編譯原生外掛)），放到：

   | 平台 | 路徑 |
   |---|---|
   | Windows x64 | `Runtime/Plugins/x86_64/UNAudio.dll` |
   | macOS | `Runtime/Plugins/macOS/libUNAudio.dylib` |
   | Linux x64 | `Runtime/Plugins/x86_64/libUNAudio.so` |
   | Android | `Runtime/Plugins/Android/<abi>/libUNAudio.so` |
   | iOS | `Runtime/Plugins/iOS/libUNAudio.a`（`__Internal` 靜態連結） |

## 快速開始

在 Project 視窗選取 `.ogg` / `.wav`，執行 **Assets ▸ UNAudio ▸ Import as UNAudio Asset**。檔案會變成 `UNAudioAsset`，原始位元組原封不動存在子資產 TextAsset 裡（不重新編碼、不產生 AudioClip）。

```csharp
using UNAudio;

public class Example : MonoBehaviour
{
    public UNAudioAsset shot;

    async void Start()
    {
        await shot.LoadAsync();          // native 背景解碼，載入畫面期間做
    }

    void Fire()
    {
        var p = UNAudioPlayParams.Default;
        p.pitch = Random.Range(0.95f, 1.05f);
        shot.Play(p);
    }
}
```

## 從 AssetBundle 載入

```csharp
var bundle = AssetBundle.LoadFromFile(path);
UNAudioClip clip = await UNAudioAssetBundles.LoadClipAsync(bundle, "theme");
bundle.Unload(true);    // 預設模式在 native 端複製一次，bundle 可以馬上卸載
clip.Play();
```

Bundle 裡可以是 `UNAudioAsset`，也可以是單純的 `.bytes` TextAsset（內容是 OGG / WAV）。資料路徑是 bundle 的原生記憶體 → `una_clip_load_memory`，中間沒有 managed 複製。`UNAudioLoadOptions.borrow = true` 連 native 那一次 memcpy 都省掉，但 bundle 要保留到 `clip.IsSourceReleased`（解碼模式在解碼完成時，串流模式在 clip 釋放時）。

## 精準時間與排程

```csharp
// 讓歌曲在確定的 dsp frame 開始
long start = UNAudioClock.EarliestScheduleFrame() + UNAudioClock.SecondsToFrames(0.2);
UNAudioVoice song = songClip.PlayScheduled(start);

// 每一幀：玩家此刻聽到的歌曲時間（已扣輸出延遲，平滑、單調）
double t = song.GetTiming(UNAudioClock.FrameHostNs).AudibleSeconds;

// 打擊音落在第 n 拍的那一個樣本上
long beatDsp = start + UNAudioClock.SecondsToFrames(firstBeat + n * 60.0 / bpm);
hitClip.PlayScheduled(beatDsp);
```

排程前的 voice 會回報負的時間（距離開始還有多久）；暫停時時間停住；loop 時會折返。輸入事件的時間戳可以用 `UNAudioClock.RealtimeToHostNs` 換算後丟給 `GetTiming(hostNs)`，取得按鍵那一刻的歌曲位置。詳見 [Getting Started](Documentation/GettingStarted.md)。

## 效能

Ryzen 7 5700X3D、Windows、48 kHz、256-frame buffer（預算 5333 µs），`Native~/Tests/bench_mixer.cpp` 實測：

| 情境 | 64 voices | 256 voices |
|---|---|---|
| 立體聲 int16，1:1 fast path | 22.9 µs（0.43%） | 90.2 µs（1.69%） |
| 單聲道 int16，1:1 fast path | 21.6 µs（0.41%） | 83.1 µs（1.56%） |
| 變速 pitch 1.37（cubic 內插） | 103.8 µs（1.95%） | 437.8 µs（8.21%） |
| 44.1 kHz clip 即時重取樣 | 103.8 µs（1.95%） | 417.1 µs（7.82%） |

Audio thread 不上鎖、不配置記憶體、不解碼；所有控制經由 lock-free 佇列傳到 mixer。延遲實測與調校見 [效能與延遲](Documentation/PerformanceGuide.md)。

## 編譯原生外掛

原始碼在 `Native~/`（結尾的 `~` 讓 Unity 不匯入 C++ 檔）。需要 CMake 3.22+。

```bash
# Windows（VS 2022/2026 開發者命令列）、Linux、macOS
cmake -S Native~ -B build -DCMAKE_BUILD_TYPE=Release -DUNAUDIO_BUILD_TESTS=ON
cmake --build build --config Release
build/UNAudioTests              # 58 個 gtest，不需要音效卡
build/una_device_probe 3 0 1    # 實際裝置：播 3 秒、exclusive，印出延遲

# Android（每個 ABI 各一次）
cmake -S Native~ -B build-android -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-23 -DCMAKE_BUILD_TYPE=Release
# iOS
cmake -S Native~ -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64
```

CI（`.github/workflows/native-build.yml`）會建出所有平台並上傳 artifact。

## 測試

- Native：`Native~/Tests`，58 個 gtest，涵蓋解碼（對照 ffmpeg/libvorbis）、重取樣、時鐘 DLL、樣本精準排程、串流、壓力測試與 review 找到的問題的回歸測試；Linux 上另外用 ASan（全過）與 TSan（無 race 報告）跑過。
- Unity：`Tests/Editor`，20 個 EditMode 測試，包含「匯入 → 建 AssetBundle → 刪除原始資產 → 從 bundle 載入 → 解碼 → 樣本精準播放」的完整流程。在專案的 `Packages/manifest.json` 加上 `"testables": ["com.lask3802.unaudio"]` 後用 Test Runner 執行。

## 文件

- [Getting Started](Documentation/GettingStarted.md)
- [API 參考](Documentation/API.md)
- [效能與延遲](Documentation/PerformanceGuide.md)
- [平台相容性](Documentation/PlatformCompatibility.md)
- [架構](ARCHITECTURE.md)

## 授權

MIT。內含 [stb_vorbis](https://github.com/nothings/stb)（public domain，已修正 >2 聲道 residue type 2 的解碼錯誤）與 [Oboe](https://github.com/google/oboe)（Apache 2.0，Android 編譯時下載）。
