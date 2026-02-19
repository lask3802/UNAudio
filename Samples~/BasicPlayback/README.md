# BasicPlayback Sample

This sample demonstrates basic audio playback using UNAudio.

## Usage

1. Add a `UNAudioSource` component to a GameObject.
2. Assign a `UNAudioClip` asset.
3. Call `Play()` at runtime.

```csharp
using UNAudio;

public class BasicPlaybackExample : MonoBehaviour
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
