# API 參考

命名空間 `UNAudio`。執行緒規則如下，事件與 `Task` 完成一律在主執行緒發生：

- **只能在主執行緒呼叫：** 初始化、`Shutdown`、所有 `UNAudioClip.Load*`。違反時丟 `InvalidOperationException`。
- **任何執行緒都可以呼叫：** voice 控制、時間查詢、混音控制。

## UNAudioEngine（static）

| 成員 | 說明 |
|---|---|
| `UNAudioConfig DefaultConfig` | 懶初始化使用的設定 |
| `UNAudioResult Initialize(UNAudioConfig)` | 明確初始化；預設裝置失敗時退回 Null device（`Device.Backend == Null`，不會出聲） |
| `void EnsureInitialized()` | 需要時以 `DefaultConfig` 初始化，失敗丟 `UNAudioException`；失敗後不再自動重試，直到明確呼叫 `Initialize` |
| `void Shutdown()` | 停止所有聲音、釋放所有 clip、關閉裝置；master / bus 音量保留到下一次初始化 |
| `bool SuspendWhenPaused` | 預設 true：`OnApplicationPause` 時暫停輸出串流 |
| `SetSuspended(bool)`、`bool IsSuspended` | 手動暫停 / 恢復輸出串流；暫停期間 dsp 時間軸凍結，已排程的 frame 仍然有效 |
| `bool IsInitialized` / `int Session` | `Session` 每次初始化遞增，舊 session 的 handle 全部失效 |
| `UNAudioDeviceInfo Device` | 取樣率、聲道、period、延遲、backend |
| `void Update()` | 每幀自動執行（PlayerLoop `EarlyUpdate`）；PlayerLoop 不跑時才需要自己呼叫 |
| `float MasterVolume`、`SetMasterVolume(v, ramp)` | |
| `SetBusVolume(bus, v, ramp)`、`SetBusPaused(bus, paused, atDsp)` | 16 條 bus |
| `StopAll(fade)` | |
| `UNAudioStats GetStats()` | callbacks、starved / dropped / late、active voices、CPU load、peak、clip 記憶體 |
| `Render(float[] buffer, int frames)` | 只用於 `Manual` 裝置 |
| 事件 `VoiceStarted / VoiceEnded / VoiceLooped(voice, dspFrame)`、`VoiceStarved(voice, frames)`、`DeviceChanged(sampleRate)`、`ShutdownCompleted` | |
| 事件 `VoiceError(voice, UNAudioResult)` | 串流 voice 解碼失敗（開檔失敗 `UnsupportedFormat`；seek 失敗或資料截斷、損毀 `DecodeError`；記憶體不足 `OutOfMemory`）；已解出的資料播完後發出，之後一定接著同一個 dsp frame 的 `VoiceEnded` |

## UNAudioConfig

`deviceType`（Default / Null / Manual）、`sampleRate`、`bufferFrames`、`exclusive`、`maxVoices`、`maxClips`、`decodeThreads`。0 代表交給引擎決定。

## UNAudioClip（class, IDisposable）

| 成員 | 說明 |
|---|---|
| `static Load(TextAsset, UNAudioLoadOptions)` | 用 `GetData<byte>()` 的原生指標，無 managed 複製 |
| `static Load(NativeArray<byte>, options, name)` | |
| `static Load(byte[], options, name)` | borrow 時會 pin 住陣列直到 native 用完 |
| `static Load(ReadOnlySpan<byte>, options, name)` | 一律複製 |
| `static LoadFile(string path, options)` | native worker 讀檔（UTF-8 路徑） |
| `static bool TryProbe(ReadOnlySpan<byte>, out UNAudioClipInfo)` | 不需要引擎，同步讀取格式資訊 |
| `State`、`IsReady`、`IsLoaded`、`IsFailed`、`IsSourceReleased`、`IsDisposed` | `Dispose` 之後 `State` 就是 `Invalid`（即使還有 voice 在播它） |
| `Info`、`Channels`、`SampleRate`、`Frames`、`Length` | `SampleRate` 是儲存的取樣率（預設等於裝置取樣率） |
| `Task<UNAudioClip> WhenReady / WhenLoaded` | 失敗時 fault 成 `UNAudioException` |
| `IEnumerator WaitUntilReady() / WaitUntilLoaded()` | coroutine 用 |
| `int Priority { set; }` | |
| `UNAudioVoice Play()`、`Play(in UNAudioPlayParams)`、`PlayScheduled(long dspFrame)` | clip 未 READY 時丟 `UNAudioException(NotReady)` |
| `bool TryPlay(in UNAudioPlayParams, out UNAudioVoice)` | 不丟例外的版本 |
| 事件 `Ready / Loaded / Failed` | |
| `Dispose()` | 播放中的 voice 播完才真正釋放 |

## UNAudioLoadOptions

| 欄位 | 預設 | 說明 |
|---|---|---|
| `mode` | `Decode` | `Decode` 整段解碼；`Stream` 邊播邊解 |
| `sampleFormat` | `Int16` | Decode 模式的 PCM 格式 |
| `borrow` | false | 零複製，記憶體要活到 `IsSourceReleased` |
| `keepSourceRate` | false | 保留原始取樣率，mixer 即時重取樣 |
| `priority` | 0 | 數字大的先解碼 |

`UNAudioLoadOptions.Default`、`UNAudioLoadOptions.Streaming`。

## UNAudioPlayParams（struct）

`volume`、`pan`（−1..1）、`pitch`（速率倍數）、`loop`、`bus`（0..15）、`startPaused`、`catchUp`、`fadeInSeconds`、`startDspFrame`（0 = 立刻）、`startFrame`、`loopStart`、`loopEnd`（0 = clip 結尾）。

從 `UNAudioPlayParams.Default` 或 `UNAudioPlayParams.At(dspFrame)` 開始改。**`default` / `new UNAudioPlayParams()` 的 volume 是 0**，會播出無聲的 voice。

## UNAudioVoice（readonly struct）

輕量 handle，可以隨意複製。voice 結束後，或屬於舊的引擎 session（`Session` 欄位）時，所有控制都是無害的 no-op。

| 成員 | 說明 |
|---|---|
| `State`、`IsValid`、`IsAlive`、`IsPlaying` | voice 被回收後狀態是 `Invalid` |
| `UNAudioResult Error` | voice 因錯誤提前結束時的原因，否則 `Ok`。在 `VoiceError` 事件分派時記錄（主執行緒），保留到引擎 `Shutdown` |
| `double Time`、`double Frame` | 此刻被聽到的 clip 時間 / frame |
| `UNAudioVoiceTiming GetTiming(long hostNs = 0)` | 指定 host 時間的完整時間資訊 |
| `Stop(fade, atDsp)`、`Pause(atDsp)`、`Resume(atDsp)` | |
| `Seek(seconds, atDsp)`、`SeekFrame(frame, atDsp)` | |
| `SetVolume(v, ramp, atDsp)`、`SetPan(p, atDsp)`、`SetPitch(p, atDsp)`、`SetLoop(loop, start, end)` | |

`UNAudioVoiceTiming`：`State`、`AudibleFrame`、`AudibleSeconds`（排程前為負）、`MixFrame`、`StartDspFrame`、`HostNs`、`Rate`（暫停時 0）、`StoredRate`。

## UNAudioClock（static）

| 成員 | 說明 |
|---|---|
| `long HostTimeNs` | 單調 host 時鐘（QPC / mach_absolute_time / CLOCK_MONOTONIC） |
| `int SampleRate` | |
| `long MixFrame` | mixer 下一個要產生的 dsp frame |
| `double AudibleFrame`、`AudibleTime` | 此刻被聽到的 dsp frame / 秒 |
| `double OutputLatency` | mix head − audible，秒 |
| `double MeasuredSampleRate` | DLL 量到的實際裝置速率 |
| `FrameHostNs`、`FrameAudibleFrame`、`FrameAudibleTime`、`FrameMixFrame` | 本幀開始時的快照 |
| `double AudibleFrameAt(long hostNs)`、`long HostNsOfFrame(double dspFrame)` | 兩個時間軸互換 |
| `long EarliestScheduleFrame(double safety = 0.005)` | 不會遲到的最早排程 frame |
| `long DspFrameHeardIn(double seconds)` | 要在 x 秒後被聽到該排在哪個 frame |
| `SecondsToFrames`、`FramesToSeconds` | |
| `RealtimeToHostNs(double)`、`HostNsToRealtime(long)` | 與 `Time.realtimeSinceStartupAsDouble` 互換 |

## UNAudioAsset（ScriptableObject）

由 importer 產生。`Data`（原始檔案的 TextAsset）、`LoadOptions`、`Channels`、`SampleRate`、`Frames`、`Length`、`Format`；`Load(options)` 建立獨立 clip；`Clip` 共用 clip；`IsClipLoaded`；`LoadAsync()`；`Play()`；`Unload()`。

`PreloadAudioData`（importer 的 **Preload Audio Data** 選項，預設關閉）對應 AudioClip 的同名設定：資產在執行中的遊戲裡被載入（場景參照、Resources、AssetBundle）後的下一幀，共用的 `Clip` 就開始 native 解碼，而不是等到第一次使用。preload **不會自己初始化引擎**（否則會搶在你的 `Initialize(config)` 之前用 `DefaultConfig` 初始化），所以它從引擎初始化後的第一幀開始，每次重新初始化後也會重跑。想在載入畫面就開始解碼，就在啟動流程呼叫 `UNAudioEngine.Initialize(config)` 或 `EnsureInitialized()`。資產卸載時 clip 一起釋放。

## UNAudioAssetBundles（static）

| 成員 | 說明 |
|---|---|
| `Task<UNAudioClip> LoadClipAsync(AssetBundle, name, options)` | 非同步載入資產（UNAudioAsset 或 TextAsset）並等到 clip 可以播放 |
| `UNAudioClip LoadClip(...)` | 同步載入資產、背景解碼 |
| `Task<UNAudioAsset[]> PreloadAsync(AssetBundle)` | 載入 bundle 裡所有 UNAudioAsset 並啟動它們的共用 clip，全部可以播放時完成；之後 `asset.Play()` 直接使用。某個 clip 失敗時 task fault，其他的仍保持載入 |

## UNAudioPlayer（MonoBehaviour）

場景裡擺放用的簡單元件：asset、play on enable、loop、volume、pan、pitch、bus、停用時淡出。clip 載入失敗或串流解碼失敗時記錄錯誤、設定 `LastError` 並觸發 `Error(player, UNAudioResult)` 事件。

## Editor

- `UNAudioImporter`：`.ogg` / `.wav` 的 importer override。
- `UNAudioImporterUtility.UseUNAudio(path)` / `UseAudioClip(path)`，以及 **Assets ▸ UNAudio** 選單。
- UNAudioAsset inspector：格式資訊、解碼後大小估計、試聽。

## 原生 C API

`Native~/Include/unaudio.h`，前綴 `una_`，`UNA_API_VERSION 2`。所有 struct 都有 `struct_size` 欄位做 ABI 檢查。C# 層是它的薄包裝，其他引擎或工具也可以直接使用。

帶時間的命令超過 8192 筆待執行時，API 回 `UNA_E_QUEUE_FULL`，不會被提早套用。串流 clip 搭配 `UNA_CLIP_FLAG_BORROW` 時，借用的記憶體要保留到 `UNA_EVENT_CLIP_FREED`。

串流 voice 的 decoder 打不開、seek 失敗，或資料在檔頭宣告的長度之前就斷掉（截斷、損毀）時，先發 `UNA_EVENT_VOICE_ERROR`（`value` 是 `una_result`：`UNA_E_FORMAT`、`UNA_E_DECODE`、`UNA_E_OUT_OF_MEMORY`），再在同一個 dsp frame 發 `UNA_EVENT_VOICE_ENDED`。失敗前已經解出的資料會先播完，結束點就在最後一個解出的 frame 之後，不會出現 `STARVED`，也不會有沒發生的 `LOOPED`。decoder 偵測損毀的粒度是它自己的封包（Vorbis 是一個 page），所以損毀處之前可能先播出一小段 decoder 解出的靜音。Decode 模式的 clip 遇到同樣的資料會補靜音，clip 仍然是 `Loaded`。
