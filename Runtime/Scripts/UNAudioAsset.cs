using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using UnityEngine;

namespace UNAudio
{
    /// <summary>
    /// An imported audio file (Ogg Vorbis, WAV, MP3 or FLAC) kept compressed exactly as authored.
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
        /// Like <see cref="AudioClip.preloadAudioData"/>: while this asset is loaded in a running game
        /// (scene reference, Resources, AssetBundle), its shared <see cref="Clip"/> starts loading on
        /// the next frame instead of on first use. Preloading never initialises the engine: it starts
        /// on the first frame after <see cref="UNAudioEngine.Initialize"/> (or the first lazy
        /// initialisation), and again after every re-initialisation.
        /// </summary>
        public bool PreloadAudioData => m_PreloadAudioData;
        public int Channels => m_Channels;
        public int SampleRate => m_SampleRate;
        public long Frames => m_Frames;
        /// <summary>Length in seconds.</summary>
        public double Length => m_Length;
        /// <summary>"vorbis", "wav", "mp3" or "flac".</summary>
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
    /// Starts the shared clips of loaded <see cref="UNAudioAsset.PreloadAudioData"/> assets from the
    /// engine tick (main thread, play mode): once per engine session and again whenever an asset
    /// is added. It never initialises the engine itself, which would race the game's own
    /// <see cref="UNAudioEngine.Initialize"/> call and silently replace its config.
    /// <c>OnEnable</c> runs while the asset is still being integrated (e.g. an async AssetBundle
    /// load), so it only registers.
    /// </summary>
    static class UNAudioPreloader
    {
        static readonly object s_Lock = new object();
        // Every loaded preload asset (kept across sessions: without a domain reload the
        // assets stay loaded and are not enabled again).
        static readonly HashSet<UNAudioAsset> s_Assets = new HashSet<UNAudioAsset>();
        static volatile bool s_Added;  // registered since the last flush
        static int s_FlushedSession;

        internal static void Add(UNAudioAsset asset)
        {
            lock (s_Lock)
            {
                s_Assets.Add(asset);
                s_Added = true;
            }
        }

        internal static void Remove(UNAudioAsset asset)
        {
            lock (s_Lock) s_Assets.Remove(asset);
        }

        internal static bool IsRegistered(UNAudioAsset asset)
        {
            lock (s_Lock) return s_Assets.Contains(asset);
        }

        /// <summary>Called by <see cref="UNAudioEngine.Update"/> (tests may skip the play-mode check).</summary>
        internal static void Tick(bool requirePlayMode = true)
        {
            if (!UNAudioEngine.IsInitialized) return;
            if (!s_Added && s_FlushedSession == UNAudioEngine.Session) return;
            if (requirePlayMode && !Application.isPlaying) return;
            UNAudioAsset[] assets;
            lock (s_Lock)
            {
                assets = new UNAudioAsset[s_Assets.Count];
                s_Assets.CopyTo(assets);
                s_Added = false;
            }
            s_FlushedSession = UNAudioEngine.Session;
            foreach (var asset in assets)
            {
                if (asset == null) continue;  // destroyed without OnDisable
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
    }
}
