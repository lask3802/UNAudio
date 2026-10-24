# 平台相容性

驗證程度分四級：**實機**（在真實硬體上播放並量測）、**模擬器 / 測試**（建置成功且 gtest 全過，模擬器上另外跑過實際輸出）、**僅編譯**、**未驗證**。下表是 2.1.0 的狀態。

| 平台 | Backend | 編譯 | 自動化測試 | 實機 | 備註 |
|---|---|---|---|---|---|
| Windows x64 | WASAPI（IAudioClient3、shared / exclusive、event-driven、MMCSS "Pro Audio"、預設裝置切換） | ✅ MSVC 19.5x，SSE2 與純量（`UNAUDIO_SIMD=OFF`）兩種 | ✅ 93 gtest + 32 Unity EditMode | ✅ shared 與 exclusive | 熱插拔只用模擬通知測過 |
| Linux x64 | ALSA（blocking writei、xrun 恢復、裝置遺失重開） | ✅ GCC 13（Docker） | ✅ ASan、TSan 都沒有任何報告；少數依賴即時速度的測試在 sanitizer 減速下不成立（見下方） | ❌ | 容器內沒有音效卡 |
| macOS arm64 / x86_64 | CoreAudio AUHAL | ✅ Apple Clang（Mac mini M4），NEON 與 SSE2（Rosetta），0 warning | ✅ 兩種架構 gtest 全過；Unity EditMode 32/32 | ✅ M4 內建輸出：64-frame buffer，輸出延遲 7.4 ms | universal dylib |
| iOS | CoreAudio RemoteIO + AVAudioSession | ✅ device（arm64）與 simulator | ✅ iOS 26.3 與 18.3 模擬器 gtest 全過 | ❌ | 靜態函式庫（`__Internal`）；不修改 AVAudioSession category |
| tvOS / visionOS | 同 iOS | ⚠️ 未編譯 | ❌ | ❌ | C# 端已用 `__Internal` |
| Android arm64-v8a / armeabi-v7a / x86_64 | Oboe 1.9.3（AAudio，舊裝置退回 OpenSL ES） | ✅ NDK r27，三個 ABI 0 diagnostics，只匯出 `una_` 符號 | ✅ 模擬器（Pixel XL，API 31）：x86_64 原生與 arm64（ARM 轉譯）gtest 全過；AAudio 與 OpenSL ES 實際輸出、suspend / resume、Unity IL2CPP APK 從 AssetBundle 載入播放 | ❌ | 模擬器不給 FAST / MMAP，低延遲路徑只在實機上才會走到 |
| WebGL | — | — | — | — | 不支援（asmdef 已排除） |

## 已知限制

- **取樣率**：Apple 平台一律用硬體取樣率；WASAPI exclusive 會優先嘗試指定的取樣率；Android 指定取樣率時會經過 Oboe 的重取樣器。
- **聲道**：mixer 輸出立體聲；裝置聲道較多時其餘聲道補零。來源超過 2 聲道時在解碼階段 downmix。
- **格式**：32-bit 立體聲 FLAC 在 Open 時拒絕（side 聲道需要 33 bit，dr_flac 解不了）。MP3 與 WAV 的中段損毀不會回報，Vorbis 只有損毀到 page 解不開時才回報；各格式能偵測到的損毀見 [API 參考](API.md)。
- **iOS 啟動時無法啟用 audio session**（例如通話中）：backend 回報成功並在背景重試。
- **App 進入背景**：Unity 層在 `OnApplicationPause(true)` 時呼叫 `una_set_suspended(1)` 停止輸出，回到前景再恢復（`UNAudioEngine.SuspendWhenPaused`）。Android 模擬器上驗證過：恢復後 clock 重新 anchor 一次（一次 discontinuity），之後穩定。iOS 只在模擬器上跑過 gtest，沒有驗證背景行為。
- **Android 的輸出延遲**：AAudio 回報 timestamp 後，backend 才把實測延遲帶進 `UNAudioEngine.Device.LatencyFrames` 與 audible time，啟動後約 0.1–0.2 秒內完成。OpenSL ES 沒有 timestamp，`LatencyFrames` 只是 buffer 大小，比實際延遲低，audible time 會偏早。timestamp 長時間對不上時（抖動很大的 HAL），backend 等 2 秒後改用未經確認的 anchor。
- **Android 模擬器**：輸出延遲會隨開機時間一路增長（實測 105 ms 長到 880 ms），問題在模擬器的 host 音訊路徑，不能拿來估實機延遲；mixer bench 的數字也只反映 host CPU 與 ARM 轉譯。
- **Android plugin 的 `.meta`**：Unity 6 只認 map 格式的 `platformData`（見 README 的安裝步驟）；list 格式會讓 `.so` 不被打進 APK。
- **Sanitizer 下的時序測試**：`Stream.SeekAndLoopFollowDecodedReference` 與 `Formats.StreamLoopAndSeekMatchDecoded` 在 seek 後只等固定的 50 ms 讓串流執行緒補資料，TSan 減速 5–15 倍時來不及，會出現 starvation 造成的不一致；一旦 starvation，voice 的位置會停住，之後整段都和參考差一段距離。`Timing.RealtimeDeviceClockIsSmoothAndMonotonic` 量的是真實時間下的時鐘平滑度，在 Docker 的 ASan 建置上 11 次有 1 次超過 1.5 ms（7.4 ms）。這些測試在沒有 sanitizer 的建置上全過，sanitizer 本身沒有回報任何問題。
- **藍牙裝置**：系統回報的延遲不一定包含編碼延遲，audible time 會偏早；請提供遊戲內校準。
- **Unity 內建音訊**：UNAudio 與 Unity 的 AudioSource 各自開輸出串流，可以並存；exclusive 模式下 Unity 的聲音可能無法輸出（Windows）。
