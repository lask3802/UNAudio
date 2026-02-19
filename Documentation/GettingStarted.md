# Getting Started with UNAudio

## 前置需求 (Prerequisites)

- Unity 2020.3 or later
- .NET Standard 2.1
- C++ compiler (MSVC / Clang / GCC) for native library builds
- CMake 3.22+

---

## 安裝 (Installation)

### Unity Package Manager（推薦）

1. Open the Unity Package Manager.
2. Click **+** → **Add package from git URL**.
3. Enter: `https://github.com/lask3802/UNAudio.git`

### 手動安裝

1. Clone or download this repository.
2. Copy the folder into `Packages/com.lask3802.unaudio/`.

---

## 建置原生庫 (Building the Native Library)

### Windows

```bash
cd Native
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

### macOS

```bash
cd Native
mkdir build && cd build
cmake .. -G Xcode
cmake --build . --config Release
```

### Linux

```bash
cd Native
mkdir build && cd build
cmake .. -G "Unix Makefiles"
cmake --build . --config Release
```

Copy the compiled library to `Runtime/Plugins/<platform>/`.

---

## 快速開始 (Quick Start)

### 1. 初始化引擎

引擎會在場景中自動初始化。你也可以手動存取：

```csharp
var engine = UNAudioEngine.Instance;
engine.SetBufferSize(128);   // lower latency
engine.SetMasterVolume(0.8f);
```

### 2. 播放音效

```csharp
using UNAudio;

public class MyPlayer : MonoBehaviour
{
    public UNAudioClip clip;

    void Start()
    {
        var source = gameObject.AddComponent<UNAudioSource>();
        source.clip = clip;
        source.Play();
    }
}
```

### 3. 一次性播放

```csharp
UNAudioSource.PlayOneShot(clip);
```

### 4. 3D 空間音效

```csharp
UNAudioSource.PlayClipAtPoint(clip, transform.position);
```

---

## 下一步 (Next Steps)

- Read the [API Reference](API.md) for the full API surface.
- See the [Performance Guide](PerformanceGuide.md) for latency tuning.
- Check the `Samples~/` folder for complete example projects.
