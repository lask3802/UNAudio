# Contributing to UNAudio

Thank you for your interest in contributing to UNAudio! This document provides guidelines for contributing to the project.

## Code of Conduct

Be respectful and constructive in all interactions with the community.

## How to Contribute

### Reporting Bugs

When reporting bugs, please include:
- Device model and OS version (Android/iOS)
- UNAudio version
- Unity version
- Steps to reproduce
- Expected vs actual behavior
- Relevant logs or error messages

### Suggesting Features

Feature requests are welcome! Please:
- Check existing issues first
- Clearly describe the use case
- Explain why this would benefit UNAudio users
- Consider platform-specific implications

### Pull Requests

1. **Fork** the repository
2. **Create a branch** for your feature: `git checkout -b feature/amazing-feature`
3. **Make your changes**:
   - Follow the existing code style
   - Add comments for complex logic
   - Update documentation as needed
4. **Test thoroughly**:
   - Test on both Android and iOS if applicable
   - Verify no performance regressions
   - Check for memory leaks
5. **Commit** with clear messages: `git commit -m 'Add amazing feature'`
6. **Push** to your fork: `git push origin feature/amazing-feature`
7. **Open a Pull Request**

## Development Setup

### Requirements

- Unity 2020.3 LTS or later
- For Android: Android Studio, NDK r21+
- For iOS: macOS with Xcode 13+

### Building Native Libraries

#### Android
```bash
cd Plugins/Android
./gradlew assembleRelease
```

#### iOS
```bash
cd Build/iOS
# Use Xcode or command line tools
```

## Code Style

### C++ Code

- Use 4 spaces for indentation (no tabs)
- Place opening braces on same line
- Use descriptive variable names
- Add comments for non-obvious logic
- Keep functions focused and small

Example:
```cpp
UNAudioResult UNAudio_Initialize(int sampleRate, int channels, int bufferSize) {
    if (g_context.isInitialized) {
        return UNAUDIO_ERROR_ALREADY_INITIALIZED;
    }
    
    // Initialize platform-specific context
    // ...
    
    return UNAUDIO_SUCCESS;
}
```

### C# Code

- Follow Unity C# coding conventions
- Use PascalCase for public members
- Use camelCase for private members
- Add XML documentation for public APIs

Example:
```csharp
/// <summary>
/// Initialize the audio engine
/// </summary>
/// <param name="sampleRate">Sample rate in Hz</param>
/// <returns>Result code</returns>
public static Result Initialize(int sampleRate)
{
    // Implementation
}
```

## Testing Guidelines

### Before Submitting PR

1. **Build Tests**: Ensure code compiles for both platforms
2. **Runtime Tests**: Test on real devices (not just emulators)
3. **Performance Tests**: Measure latency and CPU usage
4. **Memory Tests**: Check for leaks using profilers

### Platform-Specific Testing

#### Android
- Test on API 26+ devices
- Use `adb logcat` to verify no errors
- Test different buffer sizes
- Verify on multiple device manufacturers

#### iOS
- Test on iOS 11+ devices
- Use Xcode Instruments for profiling
- Test interruption scenarios (calls, notifications)
- Verify on different iPhone/iPad models

## Documentation

When making changes, update relevant documentation:

- `README.md` - For API changes or new features
- `Documentation/AndroidSetup.md` - For Android-specific changes
- `Documentation/iOSSetup.md` - For iOS-specific changes
- Code comments - For implementation details

Documentation should be:
- Clear and concise
- Bilingual (English/Chinese) for README
- Include code examples
- Explain "why" not just "what"

## Performance Considerations

UNAudio is a low-latency audio library, so performance is critical:

1. **Avoid allocations** in audio callbacks
2. **Minimize CPU usage** in real-time threads
3. **Profile changes** before submitting
4. **Document performance impact** of new features

## Platform-Specific Notes

### Android (AAudio)

- Minimum API 26 (Android 8.0)
- Use exclusive sharing mode for lowest latency
- Test on devices with/without Pro Audio support
- Handle audio focus changes properly

### iOS (CoreAudio)

- Minimum iOS 11.0
- Use RemoteIO audio unit
- Handle audio session interruptions
- Consider background audio scenarios

## Git Commit Messages

Write clear commit messages:

```
Add support for custom buffer sizes

- Allow users to specify buffer size during initialization
- Add validation for minimum/maximum buffer sizes
- Update documentation with buffer size recommendations
- Test on Android API 26-33 and iOS 11-16

Closes #123
```

## Review Process

1. Automated CI checks must pass
2. Code review by maintainers
3. Testing on multiple devices
4. Documentation review
5. Performance validation

## Questions?

Feel free to:
- Open an issue for questions
- Start a discussion in GitHub Discussions
- Contact maintainers directly

## License

By contributing, you agree that your contributions will be licensed under the MIT License.

---

Thank you for contributing to UNAudio! 🎵
