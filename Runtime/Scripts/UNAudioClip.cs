using System;
using System.Collections;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using UNAudio.Native;
using Unity.Collections;
using Unity.Collections.LowLevel.Unsafe;
using UnityEngine;

namespace UNAudio
{
    /// <summary>How a clip is loaded.</summary>
    [Serializable]
    public struct UNAudioLoadOptions
    {
        public UNAudioLoadMode mode;
        /// <summary>PCM storage for <see cref="UNAudioLoadMode.Decode"/>.</summary>
        public UNAudioSampleFormat sampleFormat;
        /// <summary>
        /// Zero-copy: the engine reads the caller's memory directly instead of copying it.
        /// The memory must stay valid until <see cref="UNAudioClip.IsSourceReleased"/>
        /// (for decoded clips: when decoding finishes; for streams: when the clip is freed).
        /// For a <see cref="TextAsset"/> the clip keeps the asset referenced for you, but
        /// unloading its AssetBundle with <c>unloadAllLoadedObjects</c> still destroys it.
        /// </summary>
        public bool borrow;
        /// <summary>Keep the source sample rate and let the mixer resample live (saves memory, costs CPU).</summary>
        public bool keepSourceRate;
        /// <summary>Higher decodes first.</summary>
        public int priority;

        /// <summary>Decode to 16-bit PCM, copy the source.</summary>
        public static UNAudioLoadOptions Default => default;

        public static UNAudioLoadOptions Streaming => new UNAudioLoadOptions { mode = UNAudioLoadMode.Stream };

        internal una_clip_params ToNative()
        {
            int flags = 0;
            if (borrow) flags |= UNAudioNative.CLIP_FLAG_BORROW;
            if (keepSourceRate) flags |= UNAudioNative.CLIP_FLAG_NO_RESAMPLE;
            return new una_clip_params
            {
                struct_size = Marshal.SizeOf<una_clip_params>(),
                mode = (int)mode,
                sample_format = (int)sampleFormat,
                flags = flags,
                priority = priority,
            };
        }
    }

    /// <summary>
    /// Audio data owned by the native engine (Ogg Vorbis or WAV). Loading returns at once
    /// and decodes on native worker threads; the clip becomes playable at
    /// <see cref="UNAudioClipState.Ready"/> (first slice decoded) while the rest decodes.
    /// Any number of voices can play one clip. Call <see cref="Dispose"/> when done; voices
    /// that are still playing keep the data alive until they end.
    /// </summary>
    public sealed unsafe class UNAudioClip : IDisposable
    {
        public uint Handle { get; private set; }
        public string Name { get; set; }
        /// <summary>Engine session the handle belongs to (see <see cref="UNAudioEngine.Session"/>).</summary>
        public int Session { get; }

        object m_Source;          // keeps borrowed memory alive (TextAsset)
        GCHandle m_Pin;           // pinned byte[] for borrowed managed memory
        bool m_Released;
        bool m_SourceReleased;
        bool m_Dead;
        UNAudioClipState m_LastState = UNAudioClipState.Loading;
        TaskCompletionSource<UNAudioClip> m_ReadyTcs, m_LoadedTcs;

        /// <summary>Raised on the main thread when the clip becomes playable.</summary>
        public event Action<UNAudioClip> Ready;
        /// <summary>Raised on the main thread when the clip is fully decoded.</summary>
        public event Action<UNAudioClip> Loaded;
        /// <summary>Raised on the main thread when loading fails.</summary>
        public event Action<UNAudioClip> Failed;

        UNAudioClip(uint handle, string name)
        {
            Handle = handle;
            Name = name;
            Session = UNAudioEngine.Session;
        }

        // ── Loading ──────────────────────────────────────────────────

        /// <summary>
        /// Loads from a TextAsset (e.g. one inside an AssetBundle) without any managed copy:
        /// the engine reads the asset's native memory through <see cref="TextAsset.GetData{T}"/>.
        /// </summary>
        public static UNAudioClip Load(TextAsset asset, UNAudioLoadOptions options = default)
        {
            if (asset == null) throw new ArgumentNullException(nameof(asset));
            var data = asset.GetData<byte>();
            var clip = LoadPointer(NativeArrayUnsafeUtility.GetUnsafeReadOnlyPtr(data), data.Length, options, asset.name);
            if (options.borrow) clip.m_Source = asset;
            return clip;
        }

        /// <summary>Loads from a NativeArray. With <c>borrow</c>, keep it alive until <see cref="IsSourceReleased"/>.</summary>
        public static UNAudioClip Load(NativeArray<byte> data, UNAudioLoadOptions options = default, string name = null)
        {
            if (!data.IsCreated) throw new ArgumentException("NativeArray is not created", nameof(data));
            return LoadPointer(NativeArrayUnsafeUtility.GetUnsafeReadOnlyPtr(data), data.Length, options, name);
        }

        /// <summary>Loads from managed bytes. With <c>borrow</c> the array is pinned until the engine is done with it.</summary>
        public static UNAudioClip Load(byte[] data, UNAudioLoadOptions options = default, string name = null)
        {
            if (data == null || data.Length == 0) throw new ArgumentException("No data", nameof(data));
            if (!options.borrow)
            {
                fixed (byte* p = data) return LoadPointer(p, data.Length, options, name);
            }
            var pin = GCHandle.Alloc(data, GCHandleType.Pinned);
            try
            {
                var clip = LoadPointer((void*)pin.AddrOfPinnedObject(), data.Length, options, name);
                clip.m_Pin = pin;
                return clip;
            }
            catch
            {
                pin.Free();
                throw;
            }
        }

        /// <summary>Loads from a span. The bytes are always copied (borrow is ignored).</summary>
        public static UNAudioClip Load(ReadOnlySpan<byte> data, UNAudioLoadOptions options = default, string name = null)
        {
            options.borrow = false;
            fixed (byte* p = data) return LoadPointer(p, data.Length, options, name);
        }

        /// <summary>
        /// Loads a file; it is read on a native worker thread. Plain file-system paths only
        /// (not Android StreamingAssets inside the APK, use a TextAsset or bytes there).
        /// </summary>
        public static UNAudioClip LoadFile(string path, UNAudioLoadOptions options = default)
        {
            if (string.IsNullOrEmpty(path)) throw new ArgumentException("No path", nameof(path));
            UNAudioEngine.ThrowIfNotMainThread("UNAudioClip.LoadFile");
            UNAudioEngine.EnsureInitialized();
            options.borrow = false;
            var p = options.ToNative();
            var utf8 = Encoding.UTF8.GetBytes(path + "\0");
            uint handle;
            fixed (byte* s = utf8)
                UNAudioException.ThrowIfFailed(UNAudioNative.una_clip_load_file(s, &p, &handle), $"Load '{path}'");
            var clip = Register(new UNAudioClip(handle, System.IO.Path.GetFileNameWithoutExtension(path)));
            clip.m_SourceReleased = true;  // the engine reads the file into its own buffer
            return clip;
        }

        static UNAudioClip LoadPointer(void* data, long size, UNAudioLoadOptions options, string name)
        {
            if (size <= 0) throw new ArgumentException("No data");
            UNAudioEngine.ThrowIfNotMainThread("UNAudioClip.Load");
            UNAudioEngine.EnsureInitialized();
            var p = options.ToNative();
            uint handle;
            UNAudioException.ThrowIfFailed(UNAudioNative.una_clip_load_memory(data, size, &p, &handle), $"Load '{name}'");
            var clip = Register(new UNAudioClip(handle, name));
            if (!options.borrow) clip.m_SourceReleased = true;
            return clip;
        }

        static UNAudioClip Register(UNAudioClip clip)
        {
            // Native events for the clip are routed through this table on the main thread.
            UNAudioEngine.LiveClips[clip.Handle] = clip;
            return clip;
        }

        /// <summary>Reads format information without an engine (editor import, validation).</summary>
        public static bool TryProbe(ReadOnlySpan<byte> data, out UNAudioClipInfo info)
        {
            una_clip_info i;
            int r;
            fixed (byte* p = data) r = UNAudioNative.una_probe(p, data.Length, &i);
            info = r < 0 ? default : new UNAudioClipInfo(i);
            return r >= 0;
        }

        // ── State ────────────────────────────────────────────────────

        bool Alive => !m_Dead && Handle != 0 && UNAudioEngine.IsInitialized && Session == UNAudioEngine.Session;

        public UNAudioClipInfo Info
        {
            get
            {
                una_clip_info i;
                if (!Alive || UNAudioNative.una_clip_get_info(Handle, &i) < 0) return default;
                return new UNAudioClipInfo(i);
            }
        }

        /// <summary>
        /// Loading state. <see cref="UNAudioClipState.Invalid"/> once the clip is disposed (even while
        /// voices still play it), freed, or its engine session has ended.
        /// </summary>
        public UNAudioClipState State
        {
            get
            {
                if (m_Released || !Alive) return UNAudioClipState.Invalid;
                una_clip_info i;
                return UNAudioNative.una_clip_get_info(Handle, &i) < 0 ? UNAudioClipState.Invalid : (UNAudioClipState)i.state;
            }
        }
        /// <summary>Playable (possibly still decoding).</summary>
        public bool IsReady { get { var s = State; return s == UNAudioClipState.Ready || s == UNAudioClipState.Loaded; } }
        public bool IsLoaded => State == UNAudioClipState.Loaded;
        public bool IsFailed => State == UNAudioClipState.Failed;
        /// <summary>The engine no longer reads the memory the clip was loaded from.</summary>
        public bool IsSourceReleased => m_SourceReleased;
        public bool IsDisposed => m_Released;

        public int Channels => Info.Channels;
        /// <summary>Sample rate of the stored frames (the device rate unless <c>keepSourceRate</c>).</summary>
        public int SampleRate => Info.SampleRate;
        public long Frames => Info.Frames;
        public double Length => Info.Length;

        public int Priority
        {
            set { if (Alive) UNAudioNative.una_clip_set_priority(Handle, value); }
        }

        /// <summary>Completes when the clip is playable. Faults with <see cref="UNAudioException"/> if loading fails.</summary>
        public Task<UNAudioClip> WhenReady => GetTask(ref m_ReadyTcs, IsReady);

        /// <summary>Completes when the clip is fully decoded. Faults with <see cref="UNAudioException"/> if loading fails.</summary>
        public Task<UNAudioClip> WhenLoaded => GetTask(ref m_LoadedTcs, IsLoaded);

        /// <summary>Coroutine helper: <c>yield return clip.WaitUntilReady();</c></summary>
        public IEnumerator WaitUntilReady()
        {
            while (State == UNAudioClipState.Loading) yield return null;
        }

        /// <summary>Coroutine helper: <c>yield return clip.WaitUntilLoaded();</c></summary>
        public IEnumerator WaitUntilLoaded()
        {
            for (var s = State; s == UNAudioClipState.Loading || s == UNAudioClipState.Ready; s = State) yield return null;
        }

        Task<UNAudioClip> GetTask(ref TaskCompletionSource<UNAudioClip> tcs, bool done)
        {
            if (tcs == null)
            {
                // Continuations must not run inside event dispatch or Shutdown.
                tcs = new TaskCompletionSource<UNAudioClip>(TaskCreationOptions.RunContinuationsAsynchronously);
                if (done) tcs.TrySetResult(this);
                else if (IsFailed) tcs.TrySetException(new UNAudioException(Info.Error, $"Load '{Name}'"));
                else if (m_Dead || m_Released) tcs.TrySetCanceled();
            }
            return tcs.Task;
        }

        // ── Playback ─────────────────────────────────────────────────

        public UNAudioVoice Play() => Play(UNAudioPlayParams.Default);

        /// <summary>Plays so the first sample is mixed at an exact dsp frame (see <see cref="UNAudioClock"/>).</summary>
        public UNAudioVoice PlayScheduled(long dspFrame) => Play(UNAudioPlayParams.At(dspFrame));

        public UNAudioVoice Play(in UNAudioPlayParams parameters)
        {
            if (!Alive) throw new UNAudioException(UNAudioResult.InvalidHandle, $"Clip '{Name}' is not loaded in this engine session");
            var p = parameters.ToNative();
            uint v;
            UNAudioException.ThrowIfFailed(UNAudioNative.una_voice_play(Handle, &p, &v), $"Play '{Name}'");
            return new UNAudioVoice(v);
        }

        /// <summary>Like <see cref="Play(in UNAudioPlayParams)"/> but returns false instead of throwing.</summary>
        public bool TryPlay(in UNAudioPlayParams parameters, out UNAudioVoice voice)
        {
            voice = default;
            if (!Alive) return false;
            var p = parameters.ToNative();
            uint v;
            if (UNAudioNative.una_voice_play(Handle, &p, &v) < 0) return false;
            voice = new UNAudioVoice(v);
            return true;
        }

        // ── Lifetime ─────────────────────────────────────────────────

        /// <summary>Releases the clip. Playing voices finish normally; the data is freed after them.</summary>
        public void Dispose()
        {
            if (m_Released) return;
            m_Released = true;
            if (Alive) UNAudioNative.una_clip_release(Handle);
            m_LoadedTcs?.TrySetCanceled();
            m_ReadyTcs?.TrySetCanceled();
        }

        internal void OnNativeEvent(int type, long value)
        {
            switch (type)
            {
                case 1: // READY
                    m_LastState = UNAudioClipState.Ready;
                    m_ReadyTcs?.TrySetResult(this);
                    Ready?.Invoke(this);
                    break;
                case 2: // LOADED
                    bool wasReady = m_LastState == UNAudioClipState.Ready;
                    m_LastState = UNAudioClipState.Loaded;
                    m_ReadyTcs?.TrySetResult(this);
                    m_LoadedTcs?.TrySetResult(this);
                    if (!wasReady) Ready?.Invoke(this);
                    Loaded?.Invoke(this);
                    break;
                case 3: // FAILED
                    m_LastState = UNAudioClipState.Failed;
                    var e = new UNAudioException((UNAudioResult)value, $"Load '{Name}'");
                    m_ReadyTcs?.TrySetException(e);
                    m_LoadedTcs?.TrySetException(e);
                    Failed?.Invoke(this);
                    break;
                case 4: // SOURCE_RELEASED
                    DropSource();
                    break;
                case 5: // FREED
                    DropSource();
                    m_Dead = true;
                    break;
            }
        }

        internal void OnEngineShutdown()
        {
            m_Dead = true;
            DropSource();
            m_ReadyTcs?.TrySetCanceled();
            m_LoadedTcs?.TrySetCanceled();
        }

        void DropSource()
        {
            m_SourceReleased = true;
            m_Source = null;
            if (m_Pin.IsAllocated) m_Pin.Free();
        }

        public override string ToString() => $"UNAudioClip '{Name}' ({State})";
    }

    public readonly struct UNAudioClipInfo
    {
        public readonly UNAudioClipState State;
        public readonly UNAudioResult Error;
        /// <summary>Stored channels (1 or 2; surround is downmixed).</summary>
        public readonly int Channels;
        public readonly int SourceChannels;
        public readonly int SourceSampleRate;
        /// <summary>Rate of the frames the mixer reads.</summary>
        public readonly int SampleRate;
        public readonly UNAudioLoadMode Mode;
        public readonly UNAudioSampleFormat SampleFormat;
        public readonly long Frames;
        public readonly long DecodedFrames;
        public readonly long MemoryBytes;
        public readonly double Length;

        internal UNAudioClipInfo(in una_clip_info i)
        {
            State = (UNAudioClipState)i.state;
            Error = (UNAudioResult)i.error;
            Channels = i.channels;
            SourceChannels = i.source_channels;
            SourceSampleRate = i.source_rate;
            SampleRate = i.stored_rate;
            Mode = (UNAudioLoadMode)i.mode;
            SampleFormat = (UNAudioSampleFormat)i.sample_format;
            Frames = i.total_frames;
            DecodedFrames = i.decoded_frames;
            MemoryBytes = i.memory_bytes;
            Length = i.length_seconds;
        }
    }
}
