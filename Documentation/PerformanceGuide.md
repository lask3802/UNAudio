# 效能與延遲

## 延遲從哪裡來

玩家按下按鍵到聽到聲音，經過這幾段：

1. **輸入到遊戲邏輯**：輸入取樣與幀率，UNAudio 管不到。
2. **命令到 mixer**：命令在下一個 callback 生效，最多一個 period。用 `atDspFrame` 排程時這段變成固定、可預測的值。
3. **Mixer 到喇叭**：裝置 buffer + OS mixer + 硬體。`UNAudioClock.OutputLatency` 就是這一段的實測值，時間 API 已經把它扣掉。

所以要降低延遲，主要是縮短 period 與裝置 buffer；要「準」，則是用排程與 audible time，而不是在 `Update` 裡立刻播放。

## 各平台怎麼拿到最低延遲

| 平台 | 預設 | 更低 |
|---|---|---|
| Windows | Shared 模式，IAudioClient3 的最小 engine period（裝置支援時常見 2.67–3 ms，不支援時 10 ms） | `exclusive = true`：繞過 Windows mixer，period 可到裝置最小值 |
| Android | AAudio LowLatency + Float + Usage::Game，buffer = 2 × burst | `exclusive = true` 要求 MMAP exclusive（裝置支援時） |
| macOS | AUHAL，裝置 buffer 設成支援範圍的最小值（下限 64） | `bufferFrames` 指定 |
| iOS | RemoteIO，preferred IO buffer 128 frames | `bufferFrames` 指定；不會改 AVAudioSession category |
| Linux | ALSA default，period 256、3 個 period | `bufferFrames` 指定 |

`bufferFrames = 0` 代表「裝置支援的最小值」。

## 實測

**WASAPI**（Logitech G535 USB 無線耳機，`una_device_probe`，3 秒）：

| 模式 | Period | present − now（平均） | Callback |
|---|---|---|---|
| Shared（裝置只支援 10 ms engine period） | 480 frames | 約 45 ms | 301 次，穩定 |
| Exclusive（裝置最小 period 3 ms） | 144 frames | 約 3.0 ms | 1004 次，每次 144 frames |

這支耳機在 exclusive 3 ms 時偶爾跟不上自己宣告的最小 period，獨立的原生 WASAPI 測試程式也能重現，屬於驅動限制。實際專案可把 `bufferFrames` 設成 2 倍最小 period 換取穩定性。

**CoreAudio**（Mac mini M4 內建輸出，`una_device_probe`）：64-frame buffer，輸出延遲 7.4 ms。

**Android**：只在模擬器上跑過。模擬器不給 FAST / MMAP，走 AudioTrack 路徑（44.1 kHz、burst 882、buffer 1764，延遲 40 ms 起跳，而且隨開機時間增長），不能代表實機。實機延遲尚未量測。

**Mixer 吞吐量**（48 kHz、256-frame buffer，`una_bench`，每個 buffer 的混音時間與佔預算的比例）：

| 情境 | Ryzen 7 5700X3D SSE2，64 voices | 同左，256 voices | 同左，純量，64 voices | Apple M4 NEON，64 voices |
|---|---|---|---|---|
| 立體聲 int16，1:1 fast path | 14.6 µs（0.27%） | 72.3 µs（1.36%） | 23.3 µs | 13.3 µs（0.25%） |
| 單聲道 int16，1:1 fast path | 10.5 µs（0.20%） | 45.6 µs（0.86%） | 20.5 µs | 7.4 µs（0.14%） |
| 變速 pitch 1.37（cubic 內插） | 50.2 µs（0.94%） | 207.8 µs（3.90%） | 103.7 µs | 22.8 µs（0.43%） |
| 44.1 kHz clip 即時重取樣 | 51.7 µs（0.97%） | 207.7 µs（3.89%） | 100.0 µs | 19.3 µs（0.36%） |

Decode 模式載入 60 秒、44.1 kHz 的 clip 並重取樣到 48 kHz：x64 SSE2 為 70–73 ms（純量 115–129 ms），M4 為 21.8 ms。

每個 voice 在 x64 fast path 約 0.9 ns / frame（立體聲）。fast path 的條件是「clip 取樣率 = 裝置取樣率、pitch = 1、位置沒有小數」，這就是預設在解碼時重取樣的原因。SIMD 在 64 voices 時讓 1:1 快 1.6–1.9 倍、cubic 與即時重取樣快約 2 倍；256 voices 的立體聲 1:1 只快 1.2 倍，那裡主要受記憶體頻寬限制。

Android 模擬器上的同一個 bench（64 voices）：x86_64 為 65 / 52 / 83 / 81 µs，arm64 經 ARM 轉譯為 146 / 165 / 419 / 331 µs。這兩組只反映 host CPU 與轉譯成本，不能拿來估手機的負載。

## 建議

- **音效用 Decode + Int16**。解碼後播放幾乎零成本，記憶體是 `frames × 聲道 × 2 bytes`（3 秒立體聲 48 kHz 約 576 KB）。
- **音樂用 Stream**。每個串流 voice 約 16 × 1024 frames 的 ring（立體聲 float 約 128 KB）加一個解碼器，記憶體只放壓縮檔。四種格式的串流 seek 都是樣本精準的。
- **在載入畫面 `await clip.WhenLoaded`**。READY 就能播，但解碼還在進行時遇到 seek 到後段，未解碼的部分會播靜音。
- **大量載入時設 priority**，讓馬上要用的先解。
- **不要每幀 Play 同一個音效幾十次**：voice 有上限（預設 128），滿了會回 `NoFreeVoice`。
- **監看 `UNAudioEngine.GetStats()`**：`StarvedFrames` 不為 0 代表解碼或串流跟不上；`CpuLoadPeak` 接近 1 代表 period 太短；`LateCommands` 增加代表排程時間給得太近（用 `EarliestScheduleFrame`）。
