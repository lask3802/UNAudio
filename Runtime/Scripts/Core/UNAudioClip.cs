using System;
using System.IO;
using System.Threading;
using UnityEngine;

namespace UNAudio
{
    /// <summary>
    /// Audio load / compression modes.
    /// </summary>
    public enum AudioLoadType
    {
        /// <summary>Keep data compressed in memory; decode in real-time during playback.</summary>
        CompressedInMemory = 0,
        /// <summary>Decompress the full clip into PCM when it is loaded.</summary>
        DecompressOnLoad = 1,
        /// <summary>Stream audio data from disk during playback.</summary>
        Streaming = 2
    }

    /// <summary>
    /// How the audio data is sourced at runtime.
    /// </summary>
    public enum AudioSourceType
    {
        /// <summary>Audio bytes are embedded directly in the ScriptableObject asset.</summary>
        Embedded = 0,
        /// <summary>Audio is loaded from a file path at runtime (StreamingAssets-relative or absolute).</summary>
        Path = 1,
        /// <summary>Audio is loaded via the Addressables system (requires com.unity.addressables).</summary>
        Addressable = 2
    }

    /// <summary>
    /// Represents an audio clip managed by the UNAudio native engine.
    /// Stores compressed or decompressed audio data and metadata.
    /// </summary>
    [Serializable]
    [CreateAssetMenu(menuName = "UNAudio/Audio Clip", fileName = "NewUNAudioClip", order = 200)]
    public partial class UNAudioClip : ScriptableObject
    {
        [SerializeField] private AudioSourceType sourceType = AudioSourceType.Embedded;
        [SerializeField] private byte[] compressedData;
        [SerializeField] private string audioFilePath;
        [SerializeField] private int sampleRateValue;
        [SerializeField] private int channelsValue;
        [SerializeField] private int bitsPerSampleValue;
        [SerializeField] private float lengthValue;
        [SerializeField] private long totalFramesValue;
        [SerializeField] private AudioLoadType loadType = AudioLoadType.CompressedInMemory;

        private int nativeHandle = -1;
        private bool isLoaded;

        // ── Public properties ────────────────────────────────────

        /// <summary>How the audio data is sourced at runtime.</summary>
        public AudioSourceType SourceType => sourceType;

        /// <summary>Sample rate in Hz (e.g. 44100, 48000).</summary>
        public int sampleRate => sampleRateValue;

        /// <summary>Number of channels (1 = Mono, 2 = Stereo).</summary>
        public int channels => channelsValue;

        /// <summary>Bits per sample.</summary>
        public int bitsPerSample => bitsPerSampleValue;

        /// <summary>Length of the clip in seconds.</summary>
        public float length => lengthValue;

        /// <summary>Total number of audio frames.</summary>
        public long totalFrames => totalFramesValue;

        /// <summary>Current load / compression type.</summary>
        public AudioLoadType LoadType => loadType;

        /// <summary>Whether audio data is currently loaded in the native engine.</summary>
        public bool IsLoaded => isLoaded;

        /// <summary>Whether the data is stored in a compressed format.</summary>
        public bool IsCompressed => loadType != AudioLoadType.DecompressOnLoad;

        /// <summary>File path for Path source type (relative to StreamingAssets or absolute).</summary>
        public string AudioFilePath => audioFilePath;

        // ── Data management ──────────────────────────────────────

        /// <summary>
        /// Change the load type.  Reloads audio data with the new mode.
        /// </summary>
        public void SetLoadType(AudioLoadType type)
        {
            if (loadType == type) return;
            UnloadAudioData();
            loadType = type;
            LoadAudioData();
        }

        /// <summary>Load audio data into the native engine (synchronous).</summary>
        public void LoadAudioData()
        {
            ValidateRuntimeState();
            if (isLoaded) return;

            // Ensure the native engine is alive (auto-creates UNAudioEngine if needed).
            if (UNAudioBridge.IsInitialized() == 0)
            {
                var _ = UNAudioEngine.Instance; // triggers Awake → InitializeEngine
                if (UNAudioBridge.IsInitialized() == 0)
                {
                    Debug.LogError("[UNAudio] Cannot load audio: native engine failed to initialize.");
                    return;
                }
            }

            byte[] data = ResolveAudioData();
            if (data == null || data.Length == 0) return;

            nativeHandle = UNAudioBridge.LoadAudio(data, (int)loadType);
            isLoaded = nativeHandle >= 0;

            if (isLoaded)
                UpdateMetadataFromNative();
        }

        /// <summary>
        /// Load audio data asynchronously. The callback receives true on success, false on failure.
        /// Always invoked on the main thread.
        /// </summary>
        public void LoadAudioDataAsync(Action<bool> callback)
        {
            ValidateRuntimeState();
            if (isLoaded)
            {
                callback?.Invoke(true);
                return;
            }

            switch (sourceType)
            {
                case AudioSourceType.Embedded:
                    // Embedded data is already in memory — just load synchronously.
                    LoadAudioData();
                    callback?.Invoke(isLoaded);
                    break;

                case AudioSourceType.Path:
                    LoadFromPathAsync(callback);
                    break;

                case AudioSourceType.Addressable:
                    // Handled in partial class (UNAudioClip.Addressables.cs).
                    // If Addressables package is not installed, this falls through.
                    LoadFromAddressableFallback(callback);
                    break;
            }
        }

        /// <summary>Unload audio data from the native engine.</summary>
        public void UnloadAudioData()
        {
            if (!isLoaded && nativeHandle < 0) return;

            if (nativeHandle >= 0 && UNAudioBridge.IsInitialized() != 0)
                UNAudioBridge.UnloadAudio(nativeHandle);

            ResetRuntimeState();
        }

        /// <summary>Get the size of the audio data currently in memory (bytes).</summary>
        public long GetMemorySize()
        {
            if (compressedData != null)
                return compressedData.Length;
            return 0;
        }

        internal int NativeHandle => nativeHandle;

        internal void ResetRuntimeState()
        {
            nativeHandle = -1;
            isLoaded = false;
        }

        // ── Internal resolution ──────────────────────────────────

        private byte[] ResolveAudioData()
        {
            switch (sourceType)
            {
                case AudioSourceType.Embedded:
                    return compressedData;

                case AudioSourceType.Path:
                    string resolved = ResolvePath(audioFilePath);
                    if (string.IsNullOrEmpty(resolved) || !File.Exists(resolved))
                    {
                        Debug.LogError($"[UNAudio] Audio file not found: {resolved}");
                        return null;
                    }
                    return File.ReadAllBytes(resolved);

                case AudioSourceType.Addressable:
                    // For sync load, Addressable data must have been loaded via async first.
                    // Fall back to embedded data if available.
                    return compressedData;

                default:
                    return null;
            }
        }

        private void LoadFromPathAsync(Action<bool> callback)
        {
            string resolved = ResolvePath(audioFilePath);
            if (string.IsNullOrEmpty(resolved))
            {
                Debug.LogError("[UNAudio] Audio file path is empty.");
                callback?.Invoke(false);
                return;
            }

            ThreadPool.QueueUserWorkItem(_ =>
            {
                byte[] data = null;
                try
                {
                    if (File.Exists(resolved))
                        data = File.ReadAllBytes(resolved);
                }
                catch (Exception ex)
                {
                    Debug.LogError($"[UNAudio] Failed to read audio file: {ex.Message}");
                }

                UNAudioMainThreadDispatcher.Enqueue(() =>
                {
                    if (data == null || data.Length == 0)
                    {
                        Debug.LogError($"[UNAudio] Audio file not found or empty: {resolved}");
                        callback?.Invoke(false);
                        return;
                    }

                    nativeHandle = UNAudioBridge.LoadAudio(data, (int)loadType);
                    isLoaded = nativeHandle >= 0;

                    if (isLoaded)
                        UpdateMetadataFromNative();

                    callback?.Invoke(isLoaded);
                });
            });
        }

        /// <summary>
        /// Fallback when Addressables package is not installed.
        /// Overridden in the partial class when UNAUDIO_ADDRESSABLES is defined.
        /// </summary>
        partial void LoadFromAddressablePartial(Action<bool> callback, ref bool handled);

        private void LoadFromAddressableFallback(Action<bool> callback)
        {
            bool handled = false;
            LoadFromAddressablePartial(callback, ref handled);
            if (!handled)
            {
                Debug.LogWarning("[UNAudio] Addressable source type requires the com.unity.addressables package.");
                callback?.Invoke(false);
            }
        }

        private static string ResolvePath(string path)
        {
            if (string.IsNullOrEmpty(path)) return null;
            if (System.IO.Path.IsPathRooted(path)) return path;
            return System.IO.Path.Combine(Application.streamingAssetsPath, path);
        }

        private void UpdateMetadataFromNative()
        {
            if (nativeHandle < 0) return;
            var info = UNAudioBridge.GetClipInfo(nativeHandle);

            // Only fill in metadata that wasn't already set at import time.
            // Import-time metadata (from AudioHeaderParser) is authoritative
            // for the source file's format and must not be overwritten by
            // the native engine, which may report the output device rate
            // instead of the source rate.
            if (sampleRateValue <= 0)    sampleRateValue = info.sampleRate;
            if (channelsValue <= 0)      channelsValue = info.channels;
            if (bitsPerSampleValue <= 0) bitsPerSampleValue = info.bitsPerSample;
            if (lengthValue <= 0)        lengthValue = info.lengthInSeconds;
            if (totalFramesValue <= 0)   totalFramesValue = info.totalFrames;
        }

        private void ValidateRuntimeState()
        {
            if (!isLoaded) return;

            if (nativeHandle < 0)
            {
                ResetRuntimeState();
                return;
            }

            if (UNAudioBridge.IsInitialized() == 0)
            {
                ResetRuntimeState();
                return;
            }

            var info = UNAudioBridge.GetClipInfo(nativeHandle);
            if (info.sampleRate <= 0 || info.channels <= 0)
            {
                Debug.LogWarning($"[UNAudio] Stale native handle detected for clip '{name}', reloading.");
                ResetRuntimeState();
            }
        }

        // ── Editor helpers ───────────────────────────────────────

        /// <summary>Initialise metadata (called by the importer).</summary>
        internal void SetMetadata(int sr, int ch, int bps, float len, byte[] data)
        {
            sampleRateValue    = sr;
            channelsValue      = ch;
            bitsPerSampleValue = bps;
            lengthValue        = len;
            compressedData     = data;
        }

        /// <summary>Set metadata with total frames (called by the importer).</summary>
        internal void SetMetadata(int sr, int ch, int bps, float len, long frames, byte[] data)
        {
            sampleRateValue    = sr;
            channelsValue      = ch;
            bitsPerSampleValue = bps;
            lengthValue        = len;
            totalFramesValue   = frames;
            compressedData     = data;
        }

        internal void SetSourceType(AudioSourceType type) => sourceType = type;
        internal void SetAudioFilePath(string path) => audioFilePath = path;

        private void OnDestroy()
        {
            UnloadAudioData();
        }
    }
}
