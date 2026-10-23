# Getting Started

## 1. 安裝與原生外掛

照 [README](../README.md#安裝) 用 git URL 安裝，並把對應平台的原生外掛放進 `Runtime/Plugins/`。確認方式：進入 Play Mode 後執行

```csharp
Debug.Log(UNAudioEngine.Device);   // 例：Wasapi 48000 Hz 2 ch, period 480 (10.00 ms), latency 960 (20.00 ms)
```

找不到外掛時會丟 `DllNotFoundException`；輸出裝置開不起來時引擎會退回 Null device（時間照走、沒有聲音）並印出警告。

## 2. 匯入音檔

選取 `.ogg`、`.wav`、`.mp3` 或 `.flac` → **Assets ▸ UNAudio ▸ Import as UNAudio Asset**。這是 importer override，只影響你選的檔案，其他音檔仍是 AudioClip（Unity 6 預設會把這四種都匯入成 AudioClip）。要改回來用 **Import as Unity AudioClip**。程式化切換：`UNAudioImporterUtility.UseUNAudio(path)`。格式依檔案內容判斷，MP3 有 LAME/Lavf Info tag 時會去掉 encoder delay 與 padding（gapless，和 ffmpeg 逐樣本一致）。

Importer 設定：

| 欄位 | 建議 |
|---|---|
| Load Mode | 音效用 `Decode`（整段解成 PCM，播放零成本）；長音樂用 `Stream`（記憶體只放壓縮資料） |
| Sample Format | `Int16` 記憶體減半，對遊戲音效足夠；需要後製級動態範圍才用 `Float32` |
| Keep Source Rate | 關閉（預設）時在解碼階段重取樣到裝置取樣率，mixer 走最快路徑；開啟可省記憶體但每次播放都要即時重取樣 |
| Priority | 同時載入很多檔案時，數字大的先解碼 |
| Preload Audio Data | 和 AudioClip 的同名選項一樣：資產一載入（場景參照、Resources、AssetBundle），共用的 `asset.Clip` 就在下一幀開始解碼，不等第一次使用。它不會自己初始化引擎，所以從引擎初始化後的第一幀開始 |

## 3. 初始化（可選）

第一次用到 clip 或 voice 時會自動以 `UNAudioEngine.DefaultConfig` 初始化。勾了 Preload Audio Data 的資產會等到引擎初始化後才開始解碼，所以要讓它們在載入畫面就開始，就在啟動流程明確初始化。要自訂設定就在第一次使用之前呼叫：

```csharp
UNAudioEngine.Initialize(new UNAudioConfig
{
    bufferFrames = 0,      // 0 = 裝置支援的最小 period
    exclusive = true,      // WASAPI exclusive / AAudio exclusive：延遲最低，但獨佔裝置
    maxVoices = 128,
});
```

## 4. 載入與播放

```csharp
UNAudioClip clip = asset.Load();           // 立刻回傳，native 背景解碼
await clip.WhenReady;                      // 第一段解完就可以播
UNAudioVoice v = clip.Play();

var p = UNAudioPlayParams.Default;
p.volume = 0.8f; p.pan = -0.3f; p.pitch = 1.2f; p.loop = true; p.bus = 2;
p.fadeInSeconds = 0.25f;
v = clip.Play(p);

v.SetVolume(0.3f, rampSeconds: 0.5f);
v.Pause(); v.Resume();
v.Seek(12.5);
v.Stop(fadeSeconds: 1f);

clip.Dispose();                            // 播放中的 voice 會播完才真正釋放
```

`UNAudioAsset.Clip` 是共用的 clip（第一次存取時載入），適合「這個資產就是一個音效」的用法；`asset.Load()` 每次建立獨立的 clip。

Coroutine 版本：`yield return clip.WaitUntilReady();`

## 5. 從 AssetBundle / bytes / 檔案載入

```csharp
await UNAudioAssetBundles.LoadClipAsync(bundle, "explosion");   // UNAudioAsset 或 .bytes TextAsset
await UNAudioAssetBundles.PreloadAsync(bundle);                  // bundle 裡所有 UNAudioAsset 的共用 clip
UNAudioClip.Load(textAsset);                                     // 任何含 OGG/WAV/MP3/FLAC 的 TextAsset
UNAudioClip.Load(nativeArray);                                   // 例如 UnityWebRequest 下載的資料
UNAudioClip.Load(bytes);                                         // managed byte[]
UNAudioClip.LoadFile(path);                                      // native worker 讀檔
```

`LoadFile` 只接受一般檔案系統路徑；Android 的 StreamingAssets 在 APK 裡，請改用 TextAsset 或先讀成 bytes。

**Borrow（零複製）**：`new UNAudioLoadOptions { borrow = true }` 讓 native 直接讀你的記憶體。TextAsset 的參照由 clip 保管，但 `AssetBundle.Unload(true)` 仍會銷毀它，所以要等 `clip.IsSourceReleased` 才能卸載。不確定時用預設的複製模式：只有 native 端一次 memcpy，沒有 managed 配置。

## 6. 時間

| 你想知道 | 用 |
|---|---|
| 玩家此刻聽到 voice 的哪裡（秒） | `voice.Time` |
| 同一幀內一致的值 | `voice.GetTiming(UNAudioClock.FrameHostNs).AudibleSeconds` |
| 某個輸入事件發生時聽到哪裡 | `voice.GetTiming(UNAudioClock.RealtimeToHostNs(eventRealtime))` |
| 現在可以排程的最早 frame | `UNAudioClock.EarliestScheduleFrame()` |
| 讓聲音在 x 秒後被聽到 | `clip.PlayScheduled(UNAudioClock.DspFrameHeardIn(x))` |
| 輸出延遲 | `UNAudioClock.OutputLatency` |

所有控制（Stop / Pause / Resume / Seek / SetVolume / SetPan / SetPitch / bus pause）都可以帶 `atDspFrame`，在那個樣本上生效。

## 7. Bus 與 master

16 條 bus，voice 預設在 bus 0。

```csharp
UNAudioEngine.SetBusVolume(1, 0.5f, rampSeconds: 0.3f);   // 例如音樂
UNAudioEngine.SetBusPaused(2, true);                      // 例如暫停選單時的遊戲音效
UNAudioEngine.SetMasterVolume(0.8f);
```

## 8. 事件

```csharp
UNAudioEngine.VoiceStarted += (voice, dspFrame) => { };
UNAudioEngine.VoiceEnded   += (voice, dspFrame) => { };
UNAudioEngine.VoiceLooped  += (voice, dspFrame) => { };
UNAudioEngine.VoiceStarved += (voice, frames) => { };     // 解碼或串流跟不上
UNAudioEngine.VoiceError   += (voice, error) => { };      // 串流解碼失敗，之後接著 VoiceEnded
UNAudioEngine.DeviceChanged += sampleRate => { };
clip.Ready += c => { }; clip.Loaded += c => { }; clip.Failed += c => { };
```

都在主執行緒、`EarlyUpdate` 開頭觸發。

串流 voice 失敗時（借用的記憶體被改掉、seek 失敗、資料中段損毀），已經解出的部分會先播完，然後觸發 `VoiceError`，帶 `UNAudioResult`（`UnsupportedFormat`、`DecodeError`、`OutOfMemory`），同一個 dsp frame 接著 `VoiceEnded`。事後也能從 voice 查：

```csharp
UNAudioEngine.VoiceEnded += (voice, dsp) =>
{
    if (voice.Error != UNAudioResult.Ok) Debug.LogWarning($"stream stopped: {voice.Error}");
};
```

`UNAudioPlayer` 元件把同樣的資訊放在 `LastError` 與 `Error` 事件。載入前就截斷的檔案不會觸發 `VoiceError`：clip 載入失敗，或長度變短、照常播完；能偵測到哪些損毀依格式而定，見 [API 參考](API.md)。

## 9. 測試與離線渲染

`UNAudioDeviceType.Manual` 不開音訊執行緒，由你呼叫 `UNAudioEngine.Render(buffer, frames)` 推進時間，適合單元測試與離線輸出；`Null` 以真實速度跑但丟掉輸出，適合 CI 與 server。
