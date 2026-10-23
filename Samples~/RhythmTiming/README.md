# Rhythm Timing

`song` 用 Stream 模式匯入，`hitSound` 用 Decode。範例示範：

- 讓歌曲在確定的 dsp frame 開始；
- 依 BPM 把節拍器聲音排在每一拍的確切樣本上；
- 用 `GetTiming(UNAudioClock.FrameHostNs)` 取得本幀玩家聽到的歌曲時間；
- 按 Space 時以「當下被聽到的位置」判定誤差（ms）。

`inputOffsetSeconds` 是玩家校準值（藍牙耳機等系統未回報的延遲）。
