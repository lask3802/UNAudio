# Music Game Sample

This sample demonstrates ultra-low-latency audio for rhythm / music games.

## Key Concepts

- Use a small buffer size (64–128 frames) for < 5 ms latency.
- Use `DecompressOnLoad` for frequently hit sound effects.
- Use `Streaming` for the background track.

```csharp
using UNAudio;

public class MusicGameExample : MonoBehaviour
{
    public UNAudioClip hitSound;
    public UNAudioClip bgm;

    void Start()
    {
        // Pre-decompress hit sounds
        hitSound.SetLoadType(AudioLoadType.DecompressOnLoad);
        hitSound.LoadAudioData();

        // Stream background music
        bgm.SetLoadType(AudioLoadType.Streaming);

        UNAudioEngine.Instance.SetBufferSize(128);
    }

    public void OnNoteHit()
    {
        UNAudioSource.PlayOneShot(hitSound);
    }
}
```
