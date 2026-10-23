# 平台相容性

驗證程度分三級：**實機**（在真實硬體上播放並量測）、**編譯 + 測試**（建置成功且 gtest 全過，但不經過音效卡）、**僅編譯**、**未驗證**。

| 平台 | Backend | 編譯 | 自動化測試 | 實機 | 備註 |
|---|---|---|---|---|---|
| Windows x64 | WASAPI（IAudioClient3、shared / exclusive、event-driven、MMCSS "Pro Audio"、預設裝置切換） | ✅ MSVC 19.5x | ✅ 58 gtest + 20 Unity EditMode | ✅ shared 與 exclusive | 熱插拔只用模擬通知測過 |
| Linux x64 | ALSA（blocking writei、xrun 恢復、裝置遺失重開） | ✅ GCC 13（Docker） | ✅ 58 gtest；ASan 全過；TSan 無 race 報告（2 個依賴即時速度的測試在 TSan 減速下不成立） | ❌ | 容器內沒有音效卡 |
| Android arm64-v8a / armeabi-v7a / x86_64 | Oboe 1.9.3（AAudio，舊裝置退回 OpenSL ES） | ✅ NDK r27 | ❌ | ❌ | 沒有實機；`getTimestamp` 在獨立執行緒輪詢 |
| macOS | CoreAudio AUHAL | ⚠️ 未在 Apple SDK 編譯 | ❌ | ❌ | 只用依 SDK 簽名寫的 stub header 做過語法 / 型別檢查；CI 的 macOS job 會是第一次真正編譯 |
| iOS / tvOS | CoreAudio RemoteIO + AVAudioSession | ⚠️ 同上 | ❌ | ❌ | 靜態函式庫（`__Internal`）；不修改 AVAudioSession category |
| WebGL | — | — | — | — | 不支援（asmdef 已排除） |

## 已知限制

- **取樣率**：Apple 平台一律用硬體取樣率；WASAPI exclusive 會優先嘗試指定的取樣率；Android 指定取樣率時會經過 Oboe 的重取樣器。
- **聲道**：mixer 輸出立體聲；裝置聲道較多時其餘聲道補零。來源超過 2 聲道時在解碼階段 downmix。
- **iOS 啟動時無法啟用 audio session**（例如通話中）：backend 回報成功並在背景重試。
- **App 進入背景**：Unity 層在 `OnApplicationPause(true)` 時呼叫 `una_set_suspended(1)` 停止輸出，回到前景再恢復（`UNAudioEngine.SuspendWhenPaused`）。Android / iOS 上的實際行為尚未在實機驗證。
- **藍牙裝置**：系統回報的延遲不一定包含編碼延遲，audible time 會偏早；請提供遊戲內校準。
- **Unity 內建音訊**：UNAudio 與 Unity 的 AudioSource 各自開輸出串流，可以並存；exclusive 模式下 Unity 的聲音可能無法輸出（Windows）。
