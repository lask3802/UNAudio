# 3D Audio Sample

This sample demonstrates 3D spatial audio with UNAudio.

## Usage

```csharp
using UNAudio;

public class SpatialAudioExample : MonoBehaviour
{
    public UNAudioClip clip;

    void Start()
    {
        var source = gameObject.AddComponent<UNAudioSource>();
        source.clip = clip;
        source.spatialBlend = 1.0f;  // fully 3D
        source.minDistance = 1f;
        source.maxDistance = 50f;
        source.Play();
    }
}
```

Attach a `UNAudioListener` to your camera for correct 3D positioning.
