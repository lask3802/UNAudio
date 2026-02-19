# UNAudio Tests

This directory contains tests for UNAudio.

## Test Structure

```
Tests/
├── Runtime/           # Runtime tests (Unity Test Framework)
├── Editor/            # Editor tests
└── README.md         # This file
```

## Running Tests

### Unity Test Runner

1. Open Unity Test Runner: **Window → General → Test Runner**
2. Switch to **PlayMode** or **EditMode** tabs
3. Click **Run All** to run all tests

### Command Line

```bash
# Run all tests
unity -runTests -projectPath . -testResults results.xml

# Run specific test category
unity -runTests -projectPath . -testCategory "Android" -testResults results.xml
```

## Writing Tests

### Example Runtime Test

```csharp
using NUnit.Framework;
using UNAudio;
using UnityEngine;
using UnityEngine.TestTools;

public class UNAudioTests
{
    [Test]
    public void TestPlatformDetection()
    {
        #if UNITY_ANDROID || UNITY_IOS
        Assert.IsTrue(UNAudioEngine.IsPlatformSupported());
        #else
        Assert.IsFalse(UNAudioEngine.IsPlatformSupported());
        #endif
    }

    [Test]
    public void TestInitialization()
    {
        if (!UNAudioEngine.IsPlatformSupported())
        {
            Assert.Pass("Platform not supported, skipping test");
            return;
        }

        var result = UNAudioEngine.Initialize(48000, 2, 512);
        Assert.AreEqual(UNAudioEngine.Result.Success, result);

        UNAudioEngine.Shutdown();
    }
}
```

## Test Coverage

Current test coverage areas:

- [ ] Platform detection
- [ ] Initialization/shutdown
- [ ] Start/stop functionality
- [ ] Volume control
- [ ] Device info retrieval
- [ ] Latency measurement
- [ ] Callback functionality
- [ ] Error handling
- [ ] Edge cases

## Platform-Specific Tests

### Android Tests
- Test on different API levels (26, 28, 30, 33)
- Test on different architectures (ARM64, ARMv7)
- Test with different buffer sizes
- Test AAudio availability

### iOS Tests
- Test on different iOS versions (11, 13, 15, 16)
- Test on different devices (iPhone, iPad)
- Test with different buffer sizes
- Test audio session handling

## Performance Tests

Performance benchmarks to track:

- Latency measurement
- CPU usage during playback
- Memory allocation
- Callback timing consistency

## Integration Tests

Test integration with:

- Unity audio system
- Other Unity subsystems
- Platform lifecycle (pause/resume)
- Audio interruptions

## Future Test Plans

- Automated device testing
- Continuous integration tests
- Performance regression tests
- Stress testing
- Memory leak detection

---

For more information, see [Contributing Guidelines](../CONTRIBUTING.md).
