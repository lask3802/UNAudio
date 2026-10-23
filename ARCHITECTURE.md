# UNAudio 架構（v2）

這份文件說明 2.0 重寫後的設計：每個機制先講它要防止什麼問題，再講怎麼做，最後講它沒有涵蓋的部分。

## 1. 分層

```
Unity C#  (Runtime/Scripts)
  UNAudioEngine / UNAudioClock / UNAudioClip / UNAudioVoice / UNAudioAsset
        │  P/Invoke（blittable struct，C ABI，Native~/Include/unaudio.h）
        ▼
Native engine (Native~/Source/Engine)
  控制端：Engine.cpp      ← 任何非 audio 執行緒，持 mutex
  音訊端：Mixer.cpp       ← 只在 device callback 上執行，不鎖、不配置、不解碼
  服務：Services.cpp      ← DecodeService（worker pool）、StreamService（串流補資料）
  時鐘：Clock.cpp         ← DLL 平滑 + seqlock 發佈
        │
Decode (Native~/Source/Decode)   stb_vorbis、WAV、polyphase 重取樣、downmix
Platform (Native~/Source/Platform) WASAPI / CoreAudio / Oboe / ALSA / Null / Manual
```

原始碼放在 `Native~/`，因為 Unity 會匯入套件裡所有資料夾，`.cpp` 會被當成 IL2CPP 外掛原始碼帶進 player build；結尾的 `~` 讓 Unity 完全忽略它。

## 2. 資料模型：Clip 與 Voice 分離

舊設計把「音檔」和「播放中的聲音」綁在一起，同一個音效疊播就得複製資料。v2 把它們拆開：

- **Clip**：解碼後的 PCM（或串流模式下的壓縮資料）。一個 clip 可以同時被任意數量的 voice 播放。
- **Voice**：一次播放，帶自己的位置、音量、pan、pitch、loop、bus。

兩者都用 32-bit handle `(slot + 1) | (generation << 16)`，0 永遠無效。slot 重複使用時 generation 會遞增，所以過期的 handle 會被拒絕（`UNA_E_INVALID_HANDLE`），而不是誤操作到別的聲音。

## 3. 執行緒模型

要防止的問題：audio callback 只有幾毫秒，任何鎖競爭、記憶體配置或解碼都可能讓它超時，造成爆音。

| 執行緒 | 做什麼 | 不做什麼 |
|---|---|---|
| Audio（device callback） | 取命令、混音、發事件、發佈時鐘 | 上鎖、malloc/free、解碼、系統呼叫（除了喚醒 semaphore） |
| 控制端（C# 主執行緒或任何執行緒） | 驗證參數、配置 voice/stream、推命令、回收 | 等 audio thread |
| DecodeService（1–2 個 worker） | 依 priority 解碼 clip，分段發佈進度 | 碰 voice 狀態 |
| StreamService（1 個） | 為每個串流 voice 預先解碼到 block ring | 碰 mixer 狀態 |

控制端到 audio 端走 `MpscQueue<Cmd>`（producer 端有 mutex，consumer 端 lock-free）；audio 端到控制端的事件走 `SpscQueue<una_event>`。佇列容量 8192，滿了就丟棄並計入 `dropped_commands`，不會阻塞。

Audio thread 每個 callback 都設定 flush-to-zero（SSE FTZ/DAZ、ARM FZ），避免 denormal 讓濾波運算變慢幾十倍。

## 4. 樣本精準排程

要防止的問題：如果命令只在 callback 邊界生效，時間精度就等於 buffer 長度（例如 10 ms），而且會隨裝置設定改變。

做法：每個命令帶一個 dsp frame。audio thread 把有時間的命令放進 min-heap（依 dsp、再依序號排序），處理一個 block 時，在下一個事件的位置把 block 切成子區段，事件剛好套用在指定的那個樣本上。`Play` 收到後立刻建立 voice（狀態 Scheduled），真正的開始是 heap 裡的一個內部 Start 事件。

- 已經過去的時間：命令在最早的可能位置套用，計入 `late_commands`。加上 `UNA_PLAY_CATCH_UP` 時會跳過錯過的 frame，讓 voice 仍然對齊原本的時間軸；loop 中的 voice 會在 loop 區段內取模，落在「準時開始的話現在該在的位置」。
- Heap 容量 8192。帶時間的命令在控制端推送時就先預約一個 heap 位置，audio thread 套用後才歸還；預約滿了，API 直接回 `UNA_E_QUEUE_FULL`。這樣 heap 不會在 audio thread 上溢出，也就不會出現「API 回 OK，命令卻被提早套用」的情況。目標 voice 先結束的話，它的未來命令仍佔著位置，到期時才被丟棄並歸還。
- 暫停中（voice 或所在 bus）的 voice 收到帶 fade 的 Stop 會立即結束：沒有在前進的樣本可以淡出，等待只會讓 voice 永遠停在 fading 狀態。
- Ramp（淡入淡出、音量、pan、bus）會在 ramp 結束點切 chunk，所以斜率在整個長度上是精確線性的；master ramp 逐樣本計算。

測試（`test_engine.cpp`）用「每個樣本編碼自己的索引」的 ramp 訊號，驗證開始、結束、loop、pause、seek 落在確切的 frame，而且在 64 到 2048 的各種 block 大小下結果相同。

### SIMD

要防止的問題：混音成本隨 voice 數線性成長，而編譯器不會自動向量化這幾個迴圈：交錯的立體聲寫入、逐 frame 不同位置的 cubic 內插，以及 FIR 的浮點累加（沒有 fast-math 時不允許重排加法順序）。

做法：`Core/Simd.h` 在 x86/x64 選 SSE2（x64 的基本指令集，不需要執行期偵測），在 ARM 選 NEON（AArch64 一定有，NDK 對 armeabi-v7a 預設開啟）。`Engine/MixKernels.cpp` 把五個熱迴圈寫成 4-lane 版本：

- 1:1 mono / stereo：一次 4 frame；gain ramp 用 `l0 + dl × i` 逐 lane 算，和純量版同一個式子。
- Cubic 內插：位置不規則，所以 4 個 tap 用純量讀進來，多項式用 SIMD 算；stereo 版一個向量放兩個 frame 的 L/R。內插權重取 32.32 位置的 24 位小數，轉成 float 是精確的，所以純量和 SIMD 用的是同一個權重。
- int16 → float、master（gain、clip、peak）。
- 解碼端 resampler 的內積（`simd::Dot`），兩組累加器。

每個 kernel 在 `mix::scalar` 裡都有純量參考版，`test_simd.cpp` 用各種長度（含 0、奇數、非 4 倍數）和中途結束的 ramp 比對兩者；`-DUNAUDIO_SIMD=OFF` 會整個退回純量版，同一套測試也要通過。x64 上 64 voices 的實測：1:1 快約 1.8 倍，cubic 與即時重取樣快約 2.2 倍，解碼時的重取樣快約 1.7 倍。

沒有涵蓋：沒有 AVX 路徑（要執行期偵測，而目前的瓶頸已經不在算術）；cubic 的 tap 讀取仍是純量。ARM 上編譯器可能把純量式子合併成 FMA，兩條路徑在最後一個 bit 可能不同，所以比對測試留了 2e-6 的容差。

## 5. 時鐘：「現在喇叭在播哪一個樣本」

要防止的問題：mixer 寫到哪裡（mix head）不等於玩家聽到哪裡，中間隔著整個輸出延遲；而 callback 的時間點有排程抖動，直接拿來用，時間會一格一格跳。

做法：

1. 每個 callback，backend 回報這個 buffer 第一個 frame 的 **presentNs**：它在 host 時鐘上被聽到的時刻。WASAPI 用 `IAudioClock::GetPosition` 的 QPC 位置加上 padding；CoreAudio 用 `mHostTime` 加上裝置與 stream latency；AAudio 用 `getTimestamp`（在獨立執行緒輪詢，因為它在 callback 內會上鎖）；ALSA 用 `snd_pcm_delay`。
2. `Clock` 用二階 delay-locked loop（Adriaensen）平滑這些 (frame, presentNs) 對：settling 期間頻寬 2 Hz，之後 0.4 Hz。誤差超過 `max(8 ms, 3 個 period)` 就重新錨定並計為 discontinuity（xrun、裝置重啟）。DLL 同時量出裝置時鐘相對 host 時鐘的實際速率，追得上 ±300 ppm 的漂移。
3. 結果（anchor frame、anchor ns、ns/frame）用 seqlock 發佈，任何執行緒都能無鎖讀取。`AudibleNow` 在同一個 epoch 內單調，而且不會超過 mix head。

每個 voice 另外記錄最近 16 段線性時間軸 `source = src0 + (dsp − dsp0) × rate`：開始、pause（rate 0）、pitch 改變、seek、loop 折返都會新增一段。查詢 voice 時間就是「把 audible dsp frame 代入對應的那一段」，所以排程中的 voice 回報負時間，暫停時停住，loop 時正確折返。

沒有涵蓋：backend 回報的延遲本身若不準（例如藍牙耳機的編碼延遲沒被系統計入），DLL 只能平滑、不能修正，這要靠遊戲內的使用者校準。

## 6. 解碼與記憶體

- **格式**：Ogg Vorbis（stb_vorbis v1.22，已修正 residue type 2 在超過 2 聲道時被截斷的錯誤）、WAV（PCM 8/16/24/32、float 32/64、EXTENSIBLE）。
- **Pipeline**：來源解碼（planar float）→ downmix 到 ≤ 2 聲道（依 Vorbis / WAV 聲道順序表，係數正規化避免削波）→ 重取樣到裝置取樣率。
- **重取樣**：polyphase windowed-sinc（Kaiser β = 8，cutoff 0.92），升取樣 32 taps，降取樣按比例加長（上限 160）。輸出 frame k 對應輸入 k·M/L 以整數計算，所以 seek 是樣本精準的。放在解碼階段做，mixer 在速率相同時就能走 1:1 fast path。
- **Clip 儲存**：int16 或 float32 interleaved，一次配置完成。`decodedFrames` 是 atomic，第一段（8192 frame）解完就進入 READY，可以邊解邊播；mixer 遇到還沒解到的位置會播靜音並計入 starved。
- **來源記憶體**：預設在呼叫 `una_clip_load_memory` 時複製一次；`BORROW` 模式不複製，解碼完成時（串流模式則是 clip 釋放時）發 `CLIP_SOURCE_RELEASED`。

## 7. 串流模式

給長音樂用：記憶體只放壓縮資料，每個 voice 有自己的解碼器與 16 個 1024-frame block 的環。StreamService 在背景補滿；mixer 只讀。

- **Loop**：由 producer 端處理，在 loop end 時 seek 回 loop start 繼續填，mixer 看到的是連續資料。播放中改變 loop 設定只更新 producer 的參數，不立刻 seek：多數情況下 ring 裡的資料仍然正確（例如 loop end 還在前方）。每個 block 記錄它的 `clipPos`，mixer 讀之前比對 ring 開頭的位置與 voice 自己的位置，只有兩者不符（ring 裡是舊 loop 順序的資料），或 loop 已啟用但 producer 已經讀到結尾時，才從目前位置 seek 一次；代價是一次補資料的時間。
- **Clip 參考**：每個 Stream 自己持有一個 clip 參考，最後一個 `shared_ptr` 消失時才放掉。stream thread 在 `Refill` 時可能握著最後一個參考，光靠 voice slot 的參考不足以保證 `clip->src` 還活著。所以串流 clip 的 `CLIP_FREED` 可能比 voice 回收晚一次 `una_update`，借用的記憶體要等到 `CLIP_FREED` 才能釋放。
- **Seek**：控制端提高 epoch；producer 看到新 epoch 就丟棄舊 block、從新位置開始填。每個 block 帶 `streamPos`，mixer 用它對齊，遲到的資料直接跳過，所以 seek 後時間軸不會偏移。
- **開始**：立即播放的 voice 會等 ring 有資料（primed）才開始；排程播放則準時開始，資料不足的部分播靜音並發 `VOICE_STARVED`。
- **失敗**：clip 載入時已經 probe 過，所以 voice 自己的 decoder 開不起來，只會是借用的位元組被改掉，或記憶體不足；seek 失敗則是資料損毀（stb_vorbis seek 失敗時會回到開頭，繼續播會讓聲音和時間軸錯開）。兩種情況下 producer 都停止，並記錄 `una_result`。mixer 先把失敗前已經解出的資料播完，再發 `VOICE_ERROR`（帶錯誤碼），然後在同一個 frame 發 `VOICE_ENDED`。等待資料的立即播放 voice 在失敗時不會先發 `VOICE_STARTED`：mixer 先問 `Primed()` 再問 `Failed()`，因為失敗也會讓 `Primed()` 成立，而 `failed_` 只會從 false 變成 true。

## 8. 生命週期

- Clip 有參考計數（擁有者 + 每個 voice + 解碼工作）。`una_clip_release` 只放掉擁有者的參考；真正刪除只在控制端的 `CollectClips` 發生，發 `CLIP_FREED`。
- Voice 結束時，audio thread 先把它從 active list 移除，之後才把 phase 設成 Ended；控制端在 `una_update`（或沒有空 slot 時）回收。這個順序保證 slot 被重用時 audio thread 已經不會再碰它。
- `Shutdown` 停止 device 時不持有控制端 mutex，因為 device 執行緒可能正在呼叫 `OnDeviceChanged`；device 相關資訊用獨立的 `devMutex_` 保護。`Init`、`Shutdown`、`una_set_suspended` 與 Manual device 的 `una_render` 由 `lifeMutex_` 串行化，所以 Shutdown 的兩段加鎖之間不會插入 Init，Manual render 也不會在 clip 被刪除時還在混音。
- Handle 的 generation 每個 session 從不同的 base 起算，free list 採 FIFO。前者讓舊 session 留下的 handle 不會對上新 session 同一個 slot 的 voice；後者讓 16-bit generation 盡量晚才繞回來。
- Master 與 bus 音量屬於混音設定，跨 `una_shutdown` / `una_init` 保留，也可以在第一次 init 前設定；bus 的暫停狀態則屬於 session。
- `una_set_suspended(1)` 停止輸出串流但不拆掉 engine。dsp 時間軸跟著凍結，所以已排程的 frame 仍然有效；恢復時 clock 會偵測到時間空檔並重新 anchor，算一次 discontinuity；`AudibleNow` 只保證同一個 epoch 內單調，所以恢復後讀到的時間可能跳一次。suspend 期間 dsp 不前進，一般沒有往回跳的問題。
- 全域 engine 物件刻意不在 DLL unload 時解構：Windows 在 loader lock 下 join 執行緒會 deadlock。

## 9. C# 層

- `UNAudioEngine` 懶初始化；預設裝置失敗時退回 Null device，時間軸照樣前進。
- PlayerLoop 在 `EarlyUpdate` 最前面插入一個系統：拍下時鐘快照（`UNAudioClock.Frame*`）、呼叫 `una_update`、分派事件。所以事件與 `Task` 完成都在主執行緒，同一幀的所有腳本看到同一個時間。Edit mode 用 `EditorApplication.update` 補上。
- Domain reload、離開 Play Mode、`Application.quitting` 時關閉 native engine；`UNAudioEngine.Session` 讓快取的 clip 與 voice 知道自己屬於舊 session，舊 voice 的所有控制都變成 no-op。
- 載入 clip 與初始化只能在主執行緒呼叫（clip 登記表與 PlayerLoop 都不是 thread-safe，違反時直接丟例外）；voice 控制與時間查詢可以在任何執行緒呼叫。
- `Task` 一律以 `RunContinuationsAsynchronously` 建立，continuation 不會在事件分派或 `Shutdown` 的迴圈中途執行。
- `UNAudioAsset.PreloadAudioData`：資產的 `OnEnable` 只把自己登記到 `UNAudioPreloader`，因為 `OnEnable` 發生在資產還在整合的時候（例如非同步載入 AssetBundle），這時去讀同一個資產裡的 TextAsset 會觸發同步載入。真正開始載入由 `UNAudioEngine.Update` 的 tick 負責（Play Mode 的主執行緒）：每個引擎 session 第一次 tick 時啟動所有已登記的資產，之後有新資產登記就再跑一次。preload 從不自己初始化引擎。如果它用 `DefaultConfig` 初始化，遊戲自己在 `Awake`／`Start` 呼叫的 `Initialize(config)` 會回 `AlreadyInitialized`，設定就被悄悄丟掉；而且 Editor（`BeforeSceneLoad`）和 Player（第一個 `EarlyUpdate`）的時序不同，結果還會因環境而異。登記清單跨 session 保留，因為關閉 domain reload 時資產留在記憶體裡、不會再收到 `OnEnable`。`OnDisable`（資產卸載）會取消登記並釋放 clip。
- 隱藏的 `UNAudioPauseHandler` 接收 `OnApplicationPause`，在 `SuspendWhenPaused` 為 true（預設）時暫停輸出。不用 `Application.focusChanged`，因為桌面版切換視窗也會觸發，而設定了 `runInBackground` 的遊戲不該因此停止聲音。
- `UNAudioClip.Load(TextAsset)` 用 `GetData<byte>()` 取得原生記憶體指標，整條路徑沒有 managed 配置（EditMode 測試量測 `GC.GetAllocatedBytesForCurrentThread` 驗證）。

## 10. 平台 backend 介面

`AudioDevice`（`Native~/Source/Platform/AudioDevice.h`）只有 Open / Start / Stop / Close / Info。約定：

- `Stop()` 返回後保證沒有 `OnRender` 正在執行。
- `OnDeviceChanged` 只在 device 停止渲染的狀態下呼叫（Start 之前的變更延後到 Start 時通知），engine 藉此重算速率與時鐘。
- 在 `OnRender` / `OnDeviceChanged` 裡呼叫 `Stop` / `Close` 會 deadlock，backend 不檢查。

各 backend 的驗證程度見 [平台相容性](Documentation/PlatformCompatibility.md)。
