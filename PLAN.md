# UNAudio Roadmap

> 2.0.0 已完成的內容見 [CHANGELOG](CHANGELOG.md)，設計見 [ARCHITECTURE](ARCHITECTURE.md)。

## 待驗證

- [ ] macOS / iOS：在 Apple SDK 上第一次編譯 CoreAudio backend（CI 的 macOS / iOS job），並在實機量測 present latency。
- [ ] Android 實機：AAudio timestamp 精度、MMAP exclusive、耳機插拔重連、OpenSL ES 退回路徑。
- [ ] Linux 實機：ALSA period / latency、xrun 恢復。
- [ ] WASAPI：實際拔插裝置與切換預設裝置（目前只用模擬通知測過）。

## 待決定

- 原生 binary 的發佈方式（commit 進套件、GitHub Release、或由使用者自行編譯）。
- WASAPI shared 模式在只有 10 ms period 的裝置上，佇列餘裕只有約 2 ms；是否在 underrun 時自動加大。
- 遇到 exclusive 最小 period 不穩定的驅動時，是否自動退到預設 period。

## 之後可做

- 串流 voice 的 sample-exact seek 預讀（目前 seek 後需要一次補資料）。
- Voice 群組與 ducking（side-chain）。
- 遊戲內延遲校準工具（敲擊對拍）。
- Addressables 的範例。
