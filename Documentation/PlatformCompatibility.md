# UNAudio Platform Compatibility

## 支援平台 (Supported Platforms)

| Platform | Audio API | Min Version | Status |
|----------|-----------|-------------|--------|
| Windows | WASAPI | Windows 7+ | 🚧 Stub |
| macOS | CoreAudio | 10.13+ | 🚧 Stub |
| Linux | ALSA | Kernel 2.6+ | 🚧 Stub |
| Android | Oboe (AAudio / OpenSL ES) | API 16+ | 🚧 Stub |
| iOS | CoreAudio + AVAudioSession | iOS 11.0+ | 🚧 Stub |

---

## Android 延遲參考 (Android Latency Reference)

| Device Tier | API Level | Audio API | Typical Latency | Buffer |
|-------------|-----------|-----------|-----------------|--------|
| High-end (Pixel, Galaxy S) | 26+ | AAudio | 10–15 ms | 192 frames |
| High-end | 16–25 | OpenSL ES | 15–20 ms | 256 frames |
| Mid-range | 26+ | AAudio | 15–25 ms | 256 frames |
| Mid-range | 16–25 | OpenSL ES | 25–35 ms | 384 frames |
| Low-end | All | Auto | 30–50 ms | 512 frames |

---

## iOS 延遲參考 (iOS Latency Reference)

| Device | Typical Latency | Buffer |
|--------|-----------------|--------|
| iPhone 13+ | 6–8 ms | 128 frames @ 48 kHz |
| iPhone X–12 | 8–10 ms | 256 frames @ 48 kHz |
| iPad Pro | 6–8 ms | 128 frames @ 48 kHz |
| Older devices | 10–15 ms | 256–512 frames @ 48 kHz |
