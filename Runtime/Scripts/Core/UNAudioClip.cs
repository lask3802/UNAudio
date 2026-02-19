using System;
using System.Runtime.InteropServices;
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
    /// Represents an audio clip managed by the UNAudio native engine.
    /// Stores compressed or decompressed audio data and metadata.
    /// </summary>
    [Serializable]
    public class UNAudioClip : ScriptableObject
    {
        [SerializeField] private byte[] compressedData;
        [SerializeField] private int sampleRateValue;
        [SerializeField] private int channelsValue;
        [SerializeField] private int bitsPerSampleValue;
        [SerializeField] private float lengthValue;
        [SerializeField] private AudioLoadType loadType = AudioLoadType.CompressedInMemory;

        private int nativeHandle = -1;
        private bool isLoaded;

        // ── Public properties ────────────────────────────────────

        /// <summary>Sample rate in Hz (e.g. 44100, 48000).</summary>
        public int sampleRate => sampleRateValue;

        /// <summary>Number of channels (1 = Mono, 2 = Stereo).</summary>
        public int channels => channelsValue;

        /// <summary>Bits per sample.</summary>
        public int bitsPerSample => bitsPerSampleValue;

        /// <summary>Length of the clip in seconds.</summary>
        public float length => lengthValue;

        /// <summary>Current load / compression type.</summary>
        public AudioLoadType LoadType => loadType;

        /// <summary>Whether audio data is currently loaded in the native engine.</summary>
        public bool IsLoaded => isLoaded;

        /// <summary>Whether the data is stored in a compressed format.</summary>
        public bool IsCompressed => loadType != AudioLoadType.DecompressOnLoad;

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

        /// <summary>Load audio data into the native engine.</summary>
        public void LoadAudioData()
        {
            if (isLoaded || compressedData == null || compressedData.Length == 0) return;

            nativeHandle = UNAudioBridge.LoadAudio(compressedData, (int)loadType);
            isLoaded = nativeHandle >= 0;
        }

        /// <summary>Unload audio data from the native engine.</summary>
        public void UnloadAudioData()
        {
            if (!isLoaded) return;
            UNAudioBridge.UnloadAudio(nativeHandle);
            nativeHandle = -1;
            isLoaded = false;
        }

        /// <summary>Get the size of the audio data currently in memory (bytes).</summary>
        public long GetMemorySize()
        {
            if (compressedData != null)
                return compressedData.Length;
            return 0;
        }

        internal int NativeHandle => nativeHandle;

        // ── Editor helpers ───────────────────────────────────────

        /// <summary>Initialise metadata (called by the importer).</summary>
        internal void SetMetadata(int sr, int ch, int bps, float len, byte[] data)
        {
            sampleRateValue   = sr;
            channelsValue     = ch;
            bitsPerSampleValue = bps;
            lengthValue       = len;
            compressedData    = data;
        }

        private void OnDestroy()
        {
            UnloadAudioData();
        }
    }
}
