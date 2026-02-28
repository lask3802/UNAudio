#if UNAUDIO_ADDRESSABLES
using System;
using UnityEngine.AddressableAssets;

namespace UNAudio
{
    /// <summary>
    /// Typed Addressable reference for UNAudioClip ScriptableObjects.
    /// Use this in fields where you want to reference a UNAudioClip via Addressables.
    /// </summary>
    [Serializable]
    public class UNAudioClipReference : AssetReferenceT<UNAudioClip>
    {
        public UNAudioClipReference(string guid) : base(guid) { }
    }
}
#endif
