using System.Threading.Tasks;
using UnityEngine;

namespace UNAudio
{
    /// <summary>
    /// An imported audio file (Ogg Vorbis or WAV) kept compressed exactly as authored.
    /// The encoded bytes live in a <see cref="TextAsset"/> sub-asset, so they ship in
    /// AssetBundles / Addressables untouched and are handed to the native decoder
    /// without a managed copy. Created by the UNAudio importer (see the README).
    /// </summary>
    public sealed class UNAudioAsset : ScriptableObject
    {
        [SerializeField] TextAsset m_Data;
        [SerializeField] UNAudioLoadOptions m_LoadOptions;
        [SerializeField] int m_Channels;
        [SerializeField] int m_SampleRate;
        [SerializeField] long m_Frames;
        [SerializeField] double m_Length;
        [SerializeField] string m_Format;

        UNAudioClip m_Shared;

        /// <summary>The encoded file bytes.</summary>
        public TextAsset Data => m_Data;
        public UNAudioLoadOptions LoadOptions => m_LoadOptions;
        public int Channels => m_Channels;
        public int SampleRate => m_SampleRate;
        public long Frames => m_Frames;
        /// <summary>Length in seconds.</summary>
        public double Length => m_Length;
        /// <summary>"vorbis" or "wav".</summary>
        public string Format => m_Format;

        /// <summary>Starts a new, independent load with the importer's options (or <paramref name="options"/>).</summary>
        public UNAudioClip Load(UNAudioLoadOptions? options = null)
        {
            if (m_Data == null) throw new UNAudioException(UNAudioResult.InvalidArgument, $"'{name}' has no data");
            var clip = UNAudioClip.Load(m_Data, options ?? m_LoadOptions);
            clip.Name = name;
            return clip;
        }

        /// <summary>
        /// A clip shared by every caller and loaded on first use. It is released by
        /// <see cref="Unload"/> or when this asset is unloaded.
        /// </summary>
        public UNAudioClip Clip
        {
            get
            {
                if (m_Shared == null || m_Shared.IsDisposed || m_Shared.Session != UNAudioEngine.Session || !UNAudioEngine.IsInitialized)
                    m_Shared = Load();
                return m_Shared;
            }
        }

        /// <summary>Loads the shared clip and completes when it is playable.</summary>
        public Task<UNAudioClip> LoadAsync() => Clip.WhenReady;

        /// <summary>
        /// Plays the shared clip. The clip must be ready (await <see cref="LoadAsync"/> first);
        /// decoding is fast but asynchronous, so preload during a loading screen.
        /// </summary>
        public UNAudioVoice Play(in UNAudioPlayParams parameters) => Clip.Play(parameters);

        public UNAudioVoice Play() => Clip.Play();

        public void Unload()
        {
            m_Shared?.Dispose();
            m_Shared = null;
        }

        void OnDisable() => Unload();

#if UNITY_EDITOR
        internal void EditorSetup(TextAsset data, UNAudioLoadOptions options, UNAudioClipInfo info, string format)
        {
            m_Data = data;
            m_LoadOptions = options;
            m_Channels = info.SourceChannels;
            m_SampleRate = info.SourceSampleRate;
            m_Frames = info.Frames;
            m_Length = info.Length;
            m_Format = format;
        }
#endif
    }
}
