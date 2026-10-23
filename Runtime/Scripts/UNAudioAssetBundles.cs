using System;
using System.Threading.Tasks;
using UnityEngine;

namespace UNAudio
{
    /// <summary>
    /// Loading audio straight out of AssetBundles. A bundle can carry either
    /// <see cref="UNAudioAsset"/>s (imported .ogg/.wav) or plain <see cref="TextAsset"/>s
    /// (.bytes files containing Ogg/WAV data); both reach the native decoder without a
    /// managed copy.
    /// </summary>
    public static class UNAudioAssetBundles
    {
        /// <summary>
        /// Loads <paramref name="assetName"/> from the bundle asynchronously and starts decoding it.
        /// Completes when the clip is playable.
        /// </summary>
        public static async Task<UNAudioClip> LoadClipAsync(AssetBundle bundle, string assetName,
                                                            UNAudioLoadOptions? options = null)
        {
            if (bundle == null) throw new ArgumentNullException(nameof(bundle));
            var obj = await LoadAssetAsync(bundle, assetName);
            UNAudioClip clip = obj switch
            {
                UNAudioAsset a => a.Load(options),
                TextAsset t => UNAudioClip.Load(t, options ?? UNAudioLoadOptions.Default),
                null => throw new UNAudioException(UNAudioResult.InvalidArgument, $"'{assetName}' not found in bundle '{bundle.name}'"),
                _ => throw new UNAudioException(UNAudioResult.InvalidArgument, $"'{assetName}' is a {obj.GetType().Name}, not audio"),
            };
            try
            {
                return await clip.WhenReady;
            }
            catch
            {
                // The caller never receives this clip, so it cannot release it.
                clip.Dispose();
                throw;
            }
        }

        /// <summary>
        /// Loads every <see cref="UNAudioAsset"/> in the bundle and starts their shared clips
        /// (<see cref="UNAudioAsset.Clip"/>), e.g. during a loading screen. Completes when all of
        /// them are playable; a clip that fails to load faults the task (the others stay loaded).
        /// The clips are released when the assets unload, or with <see cref="UNAudioAsset.Unload"/>.
        /// </summary>
        public static async Task<UNAudioAsset[]> PreloadAsync(AssetBundle bundle)
        {
            if (bundle == null) throw new ArgumentNullException(nameof(bundle));
            var assets = await LoadAllAsync(bundle);
            var ready = new Task<UNAudioClip>[assets.Length];
            for (int i = 0; i < assets.Length; i++) ready[i] = assets[i].LoadAsync();
            await Task.WhenAll(ready);
            return assets;
        }

        /// <summary>Synchronous variant: loads the asset now; decoding still runs in the background.</summary>
        public static UNAudioClip LoadClip(AssetBundle bundle, string assetName, UNAudioLoadOptions? options = null)
        {
            if (bundle == null) throw new ArgumentNullException(nameof(bundle));
            var a = bundle.LoadAsset<UNAudioAsset>(assetName);
            if (a != null) return a.Load(options);
            var t = bundle.LoadAsset<TextAsset>(assetName);
            if (t != null) return UNAudioClip.Load(t, options ?? UNAudioLoadOptions.Default);
            throw new UNAudioException(UNAudioResult.InvalidArgument, $"'{assetName}' not found in bundle '{bundle.name}'");
        }

        static Task<UNAudioAsset[]> LoadAllAsync(AssetBundle bundle)
        {
            var tcs = new TaskCompletionSource<UNAudioAsset[]>(TaskCreationOptions.RunContinuationsAsynchronously);
            var request = bundle.LoadAllAssetsAsync<UNAudioAsset>();
            request.completed += _ =>
            {
                try
                {
                    var all = request.allAssets;
                    var assets = new UNAudioAsset[all.Length];
                    for (int i = 0; i < all.Length; i++) assets[i] = (UNAudioAsset)all[i];
                    tcs.TrySetResult(assets);
                }
                catch (Exception e)
                {
                    tcs.TrySetException(e);
                }
            };
            return tcs.Task;
        }

        static Task<UnityEngine.Object> LoadAssetAsync(AssetBundle bundle, string assetName)
        {
            var tcs = new TaskCompletionSource<UnityEngine.Object>(TaskCreationOptions.RunContinuationsAsynchronously);
            var request = bundle.LoadAssetAsync<UnityEngine.Object>(assetName);
            request.completed += _ =>
            {
                try
                {
                    // A UNAudioAsset's TextAsset sub-object can win the name lookup; prefer the main asset.
                    var main = request.asset;
                    if (main is TextAsset)
                    {
                        var asset = bundle.LoadAsset<UNAudioAsset>(assetName);
                        if (asset != null) main = asset;
                    }
                    tcs.TrySetResult(main);
                }
                catch (Exception e)
                {
                    // e.g. the bundle was unloaded before the request completed
                    tcs.TrySetException(e);
                }
            };
            return tcs.Task;
        }
    }
}
