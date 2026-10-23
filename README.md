# UNAudio — Unity 低延遲原生音訊引擎

UNAudio 繞過 Unity 內建音訊，直接用各平台最低延遲的輸出 API 播放聲音，並把「現在喇叭正在播哪一個樣本」精準地交給 C#。適合音樂遊戲、節奏判定、需要樣本級對拍的玩法，以及想把大量音檔放在 AssetBundle 裡、又不想付 AudioClip 匯入與記憶體成本的專案。

- **低延遲輸出**：WASAPI（IAudioClient3 / exclusive）、CoreAudio（AUHAL / RemoteIO）、AAudio / OpenSL ES（Oboe）、ALSA。
- **AssetBundle 直接讀音檔**：OGG Vorbis、MP3、FLAC、WAV，依內容判斷格式。`TextAsset.GetData<byte>()` 的原生記憶體指標直接交給 native，不產生任何 managed `byte[]`。
- **Native 非同步解碼**：stb_vorbis / dr_mp3 / dr_flac + polyphase sinc 重取樣在 worker thread 執行，第一段 8192 frame 解完就能播，其餘在背景繼續。長音樂可用串流模式邊播邊解。seek 一律樣本精準，MP3 依 LAME 標頭去掉前後的 padding（gapless）。
- **SIMD mixer**：1:1 與 cubic 路徑、int16 轉換、master 段與重取樣器都有 SSE2（x64）與 NEON（ARM）版本，各自對照純量版本測試。
- **樣本精準排程**：play / stop / pause / seek / volume 都能指定 dsp frame，mixer 在 buffer 內切段套用，結果與 buffer 大小無關。
- **精準時間**：每個 callback 的「播放時間戳」經 delay-locked loop 平滑，`voice.Time` 回報已扣除輸出延遲、在 callback 之間連續前進且單調的播放位置。
- **C# 完全可控**：clip / voice handle、16 條 bus、master、事件（開始、結束、loop、資料不足、串流錯誤）都在主執行緒回呼；資產可以設成 preload，引擎啟動後自動載入。

> 狀態：2.1.0。Windows、Linux、macOS（M4，arm64 與 Rosetta x86_64）上編譯並通過全部 native 測試；iOS 在模擬器、Android 在模擬器（x86_64 與 ARM 轉譯的 arm64）上通過；Unity EditMode 在 Windows 與 macOS 通過。還沒有在 iPhone 或 Android 實機上量過延遲，見 [平台相容性](Documentation/PlatformCompatibility.md)。

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
   | macOS | `Runtime/Plugins/macOS/libUNAudio.dylib`（arm64 + x86_64 universal） |
   | Linux x64 | `Runtime/Plugins/x86_64/libUNAudio.so` |
   | Android | `Runtime/Plugins/Android/<abi>/libUNAudio.so` |
   | iOS | `Runtime/Plugins/iOS/libUNAudio.a`（`__Internal` 靜態連結） |

   Android 的 `.so` 要在 Plugin Inspector 勾選 Android 並設定對應的 CPU。自己產生 `.meta` 時，Unity 6 只認 map 格式的 `platformData`（`platformData:` 底下直接寫 `Android:`，裡面是 `enabled: 1` 與 `settings: {CPU: ARM64}`）；舊的 `- first: / second:` list 格式會被判成不相容，`.so` 不會打進 APK。

## 快速開始

在 Project 視窗選取 `.ogg` / `.wav` / `.mp3` / `.flac`，執行 **Assets ▸ UNAudio ▸ Import as UNAudio Asset**。檔案會變成 `UNAudioAsset`，原始位元組原封不動存在子資產 TextAsset 裡（不重新編碼、不產生 AudioClip）。

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

Bundle 裡可以是 `UNAudioAsset`，也可以是單純的 `.bytes` TextAsset（內容是 OGG / MP3 / FLAC / WAV）。資料路徑是 bundle 的原生記憶體 → `una_clip_load_memory`，中間沒有 managed 複製。`UNAudioLoadOptions.borrow = true` 連 native 那一次 memcpy 都省掉，但 bundle 要保留到 `clip.IsSourceReleased`（解碼模式在解碼完成時，串流模式在 clip 釋放時）。

## Preload 與串流錯誤

匯入設定勾 **Preload Audio Data** 的資產，一載入（場景參照、Resources、AssetBundle）就在引擎初始化後的下一幀開始解碼共用的 `asset.Clip`；它不會自己初始化引擎，要在載入畫面就開始，請在啟動流程先呼叫 `UNAudioEngine.Initialize(config)`（或用預設設定的 `EnsureInitialized()`）。整個 bundle 可以用 `await UNAudioAssetBundles.PreloadAsync(bundle)` 一次等完。

串流 voice 失敗時（seek 失敗、資料中段損毀、借用的記憶體在載入後被改動），已解出的部分會先播完，然後觸發 `UNAudioEngine.VoiceError(voice, UNAudioResult)`，同一個 dsp frame 接著 `VoiceEnded`；事後也能查 `voice.Error`。載入前就截斷的檔案不會走到這裡（載入失敗或長度變短），各格式能偵測到哪些損毀見 [API 參考](Documentation/API.md)。

```csharp
UNAudioEngine.VoiceError += (voice, error) => Debug.LogWarning($"stream failed: {error}");
```

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

48 kHz、256-frame buffer（預算 5333 µs），`una_bench`（`Native~/Tests/bench_mixer.cpp`）每個 buffer 的混音時間：

| 情境 | Ryzen 7 5700X3D（SSE2）64 voices | 同左 256 voices | Apple M4（NEON）64 voices |
|---|---|---|---|
| 立體聲 int16，1:1 fast path | 14.6 µs（0.27%） | 72.3 µs（1.36%） | 13.3 µs（0.25%） |
| 單聲道 int16，1:1 fast path | 10.5 µs（0.20%） | 45.6 µs（0.86%） | 7.4 µs（0.14%） |
| 變速 pitch 1.37（cubic 內插） | 50.2 µs（0.94%） | 207.8 µs（3.90%） | 22.8 µs（0.43%） |
| 44.1 kHz clip 即時重取樣 | 51.7 µs（0.97%） | 207.7 µs（3.89%） | 19.3 µs（0.36%） |

x64 上 SIMD 版本比純量版本（`UNAUDIO_SIMD=OFF`）快：64 voices 時 1:1 約 1.6–1.9 倍、cubic 與即時重取樣約 2 倍；256 voices 的立體聲 1:1 只快 1.2 倍，那裡主要受記憶體頻寬限制。

Audio thread 不上鎖、不配置記憶體、不解碼；所有控制經由 lock-free 佇列傳到 mixer。延遲實測與調校見 [效能與延遲](Documentation/PerformanceGuide.md)。

## 編譯原生外掛

原始碼在 `Native~/`（結尾的 `~` 讓 Unity 不匯入 C++ 檔）。需要 CMake 3.22+。

```bash
# Windows（VS 2022/2026 開發者命令列）、Linux、macOS
cmake -S Native~ -B build -DCMAKE_BUILD_TYPE=Release -DUNAUDIO_BUILD_TESTS=ON
cmake --build build --config Release
build/UNAudioTests              # 93 個 gtest，不需要音效卡
build/una_bench                 # mixer 吞吐量（64 / 256 voices）
build/una_device_probe 3 0 1    # 實際裝置：播 3 秒、exclusive，印出延遲

# Android（每個 ABI 各一次）
cmake -S Native~ -B build-android -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-23 -DCMAKE_BUILD_TYPE=Release
# macOS universal
cmake -S Native~ -B build-mac -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
# iOS
cmake -S Native~ -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64
```

`-DUNAUDIO_SIMD=OFF` 建出純量版本（用來對照或除錯）。CI（`.github/workflows/native-build.yml`）會建出所有平台並上傳 artifact。

## 測試

- Native：`Native~/Tests`，93 個 gtest，涵蓋解碼（Vorbis 對照 libvorbis，MP3 / FLAC 對照 ffmpeg）、樣本精準 seek、SIMD 與純量結果一致、重取樣、時鐘 DLL、樣本精準排程、串流與串流錯誤、壓力測試與 review 找到的問題的回歸測試。Windows、macOS（arm64 / x86_64）、iOS 模擬器、Android 模擬器全過；Linux 上另外用 ASan 與 TSan 跑過，沒有任何 sanitizer 報告（少數依賴即時速度的測試在 sanitizer 減速下不成立，見[平台相容性](Documentation/PlatformCompatibility.md)）。
- Unity：`Tests/Editor`，28 個 EditMode 測試，包含「匯入 → 建 AssetBundle → 刪除原始資產 → 從 bundle 載入 → 解碼 → 樣本精準播放」的完整流程、MP3 / FLAC 匯入與播放、preload 與串流錯誤事件。在專案的 `Packages/manifest.json` 加上 `"testables": ["com.lask3802.unaudio"]` 後用 Test Runner 執行。

## 文件

- [Getting Started](Documentation/GettingStarted.md)
- [API 參考](Documentation/API.md)
- [效能與延遲](Documentation/PerformanceGuide.md)
- [平台相容性](Documentation/PlatformCompatibility.md)
- [架構](ARCHITECTURE.md)

## 授權

MIT。內含 [stb_vorbis](https://github.com/nothings/stb)（public domain，已修正 >2 聲道 residue type 2 的解碼錯誤）、[dr_mp3 與 dr_flac](https://github.com/mackron/dr_libs)（public domain 或 MIT-0 擇一，固定在 commit b4264c5，dr_flac 已修正 32-bit sample size code）與 [Oboe](https://github.com/google/oboe)（Apache 2.0，Android 編譯時下載）。
