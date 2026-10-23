using System;
using System.Collections.Generic;
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
        [SerializeField] bool m_PreloadAudioData;
        [SerializeField] int m_Channels;
        [SerializeField] int m_SampleRate;
        [SerializeField] long m_Frames;
        [SerializeField] double m_Length;
        [SerializeField] string m_Format;

        UNAudioClip m_Shared;

        /// <summary>The encoded file bytes.</summary>
        public TextAsset Data => m_Data;
        public UNAudioLoadOptions LoadOptions => m_LoadOptions;
        /// <summary>
        /// Like <see cref="AudioClip.preloadAudioData"/>: the shared <see cref="Clip"/> starts loading
        /// as soon as this asset is loaded in a running game (scene reference, Resources, AssetBundle),
        /// instead of on first use. In the editor, assets already in memory start when play mode begins.
        /// </summary>
        public bool PreloadAudioData => m_PreloadAudioData;
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
        /// A clip shared by every caller and loaded on first use (or on asset load with
        /// <see cref="PreloadAudioData"/>). It is released by <see cref="Unload"/> or when this
        /// asset is unloaded.
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

        /// <summary>Whether the shared clip has been started in the current engine session.</summary>
        public bool IsClipLoaded =>
            m_Shared != null && !m_Shared.IsDisposed && UNAudioEngine.IsInitialized && m_Shared.Session == UNAudioEngine.Session;

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

        void OnEnable()
        {
            if (m_PreloadAudioData) UNAudioPreloader.Add(this);
        }

        void OnDisable()
        {
            UNAudioPreloader.Remove(this);
            Unload();
        }

        internal void StartPreload()
        {
            if (m_Data == null || IsClipLoaded) return;
            _ = Clip;  // starts the native load; decoding continues on worker threads
        }

#if UNITY_EDITOR
        internal void EditorSetup(TextAsset data, UNAudioLoadOptions options, bool preload, UNAudioClipInfo info, string format)
        {
            m_Data = data;
            m_LoadOptions = options;
            m_PreloadAudioData = preload;
            m_Channels = info.SourceChannels;
            m_SampleRate = info.SourceSampleRate;
            m_Frames = info.Frames;
            m_Length = info.Length;
            m_Format = format;
        }
#endif
    }

    /// <summary>
    /// Starts the shared clips of <see cref="UNAudioAsset.PreloadAudioData"/> assets. Loading must
    /// happen on the main thread in play mode, so assets enabled elsewhere (edit mode, the domain
    /// reload before play mode) wait here until play mode starts.
    /// </summary>
    static class UNAudioPreloader
    {
        static readonly object s_Lock = new object();
        static readonly HashSet<UNAudioAsset> s_Waiting = new HashSet<UNAudioAsset>();
        static volatile bool s_HasWaiting;

        internal static bool HasWaiting => s_HasWaiting;

        internal static void Add(UNAudioAsset asset)
        {
            lock (s_Lock)
            {
                s_Waiting.Add(asset);
                s_HasWaiting = true;
            }
            if (UNAudioEngine.IsMainThread && Application.isPlaying) Flush();
        }

        internal static void Remove(UNAudioAsset asset)
        {
            lock (s_Lock)
            {
                s_Waiting.Remove(asset);
                s_HasWaiting = s_Waiting.Count > 0;
            }
        }

        internal static bool IsWaiting(UNAudioAsset asset)
        {
            lock (s_Lock) return s_Waiting.Contains(asset);
        }

        /// <summary>Main thread, play mode: starts every waiting asset (tests may skip the play-mode check).</summary>
        internal static void Flush(bool requirePlayMode = true)
        {
            if (!s_HasWaiting || (requirePlayMode && !Application.isPlaying)) return;
            UNAudioAsset[] assets;
            lock (s_Lock)
            {
                assets = new UNAudioAsset[s_Waiting.Count];
                s_Waiting.CopyTo(assets);
                s_Waiting.Clear();
                s_HasWaiting = false;
            }
            foreach (var asset in assets)
            {
                if (asset == null) continue;  // destroyed while waiting
                try
                {
                    asset.StartPreload();
                }
                catch (Exception e)
                {
                    Debug.LogException(e, asset);
                }
            }
        }

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
        static void OnPlayModeStart() => Flush();
    }
}
