# AssetBundle Loading

1. 把 UNAudioAsset（或內容是 OGG/WAV 的 `.bytes` TextAsset）標記到 AssetBundle `audio`，建置到 `StreamingAssets/`。
2. 在 `assetNames` 填入資產名稱。
3. 進 Play Mode：每個 clip 從 bundle 的原生記憶體直接交給 native 解碼器，沒有 managed `byte[]`；載入後 bundle 立即卸載，clip 仍可播放。
