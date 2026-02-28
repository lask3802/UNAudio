#if UNAUDIO_ADDRESSABLES
using System;
using UnityEngine;
using UnityEngine.AddressableAssets;
using UnityEngine.ResourceManagement.AsyncOperations;

namespace UNAudio
{
    public partial class UNAudioClip
    {
        [SerializeField] private AssetReference addressableDataReference;

        /// <summary>Addressable asset reference for loading audio data at runtime.</summary>
        public AssetReference AddressableDataReference => addressableDataReference;

        internal void SetAddressableDataReference(AssetReference reference)
        {
            addressableDataReference = reference;
        }

        partial void LoadFromAddressablePartial(Action<bool> callback, ref bool handled)
        {
            handled = true;

            if (addressableDataReference == null || !addressableDataReference.RuntimeKeyIsValid())
            {
                Debug.LogError("[UNAudio] Addressable reference is not set or invalid.");
                callback?.Invoke(false);
                return;
            }

            var op = addressableDataReference.LoadAssetAsync<TextAsset>();
            op.Completed += handle =>
            {
                if (handle.Status != AsyncOperationStatus.Succeeded || handle.Result == null)
                {
                    Debug.LogError("[UNAudio] Failed to load audio data from Addressables.");
                    callback?.Invoke(false);
                    return;
                }

                byte[] data = handle.Result.bytes;
                Addressables.Release(handle);

                nativeHandle = UNAudioBridge.LoadAudio(data, (int)loadType);
                isLoaded = nativeHandle >= 0;

                if (isLoaded)
                    UpdateMetadataFromNative();

                callback?.Invoke(isLoaded);
            };
        }
    }
}
#endif
