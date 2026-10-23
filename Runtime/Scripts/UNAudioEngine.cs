using System;
using System.Collections.Generic;
using UNAudio.Native;
using UnityEngine;
using UnityEngine.LowLevel;
using UnityEngine.PlayerLoop;

namespace UNAudio
{
    /// <summary>
    /// Entry point of the native engine. Initialises lazily on first use with
    /// <see cref="DefaultConfig"/>, or explicitly with <see cref="Initialize"/>.
    /// Housekeeping and event dispatch run once per frame from the PlayerLoop
    /// (EarlyUpdate), so callbacks always arrive on the main thread.
    /// </summary>
    public static unsafe class UNAudioEngine
    {
        /// <summary>Config used by lazy initialisation. Set before the first clip or voice call.</summary>
        public static UNAudioConfig DefaultConfig = UNAudioConfig.Default;

        /// <summary>Raised on the main thread for every voice start (exact dsp frame).</summary>
        public static event Action<UNAudioVoice, long> VoiceStarted;
        /// <summary>Raised on the main thread when a voice ends (exact dsp frame).</summary>
        public static event Action<UNAudioVoice, long> VoiceEnded;
        /// <summary>Raised on the main thread each time a looping voice wraps.</summary>
        public static event Action<UNAudioVoice, long> VoiceLooped;
        /// <summary>A voice ran out of decoded data and played silence (value = frames).</summary>
        public static event Action<UNAudioVoice, long> VoiceStarved;
        /// <summary>
        /// A streaming voice could not decode its data and stopped (<see cref="VoiceEnded"/> follows).
        /// The error is also kept on the voice: <see cref="UNAudioVoice.Error"/>.
        /// </summary>
        public static event Action<UNAudioVoice, UNAudioResult> VoiceError;
        /// <summary>The output device changed; the argument is the new sample rate.</summary>
        public static event Action<int> DeviceChanged;
        /// <summary>Raised after <see cref="Shutdown"/>; every clip and voice handle is now invalid.</summary>
        public static event Action ShutdownCompleted;

        /// <summary>
        /// Suspend the output stream while the application is paused (mobile background,
        /// <c>OnApplicationPause</c>). The dsp timeline freezes with it, so scheduled frames
        /// stay valid. Default true.
        /// </summary>
        public static bool SuspendWhenPaused = true;

        static bool s_Initialized;
        static int s_Session;
        static int s_MainThreadId;
        static UNAudioResult s_LazyInitFailure;  // remembered so lazy init does not retry every call
        static UNAudioDeviceInfo s_Device;
        static readonly una_event[] s_Events = new una_event[256];
        internal static readonly Dictionary<uint, UNAudioClip> LiveClips = new Dictionary<uint, UNAudioClip>();
        // Errors of recently failed voices (this session), oldest dropped first.
        const int k_MaxVoiceErrors = 256;
        static readonly Dictionary<uint, UNAudioResult> s_VoiceErrors = new Dictionary<uint, UNAudioResult>();
        static readonly Queue<uint> s_VoiceErrorOrder = new Queue<uint>();

        public static bool IsInitialized => s_Initialized;

        /// <summary>Changes every time the engine is (re)initialised. Cached handles from an older session are dead.</summary>
        public static int Session => s_Session;

        /// <summary>
        /// Output format and latency, refreshed every frame: some backends refine their latency while
        /// running (Android once AAudio reports real timestamps, iOS when the audio route changes).
        /// </summary>
        public static UNAudioDeviceInfo Device
        {
            get
            {
                EnsureInitialized();
                return s_Device;
            }
        }

        public static UNAudioResult Initialize(UNAudioConfig config)
        {
            ThrowIfNotMainThread("UNAudioEngine.Initialize");
            if (s_Initialized) return UNAudioResult.AlreadyInitialized;
            s_LazyInitFailure = UNAudioResult.Ok;
            int version = UNAudioNative.una_get_api_version();
            if (version != UNAudioNative.ApiVersion)
            {
                Debug.LogError($"[UNAudio] Native library API v{version}, managed layer expects v{UNAudioNative.ApiVersion}.");
                return UNAudioResult.AbiMismatch;
            }
            // A previous domain (script reload with the engine running) may have left it up.
            if (UNAudioNative.una_is_initialized() != 0) UNAudioNative.una_shutdown();

            var c = new una_config
            {
                struct_size = sizeof(una_config),
                device_type = (int)config.deviceType,
                sample_rate = config.sampleRate,
                buffer_frames = config.bufferFrames,
                exclusive = config.exclusive ? 1 : 0,
                max_voices = config.maxVoices,
                max_clips = config.maxClips,
                decode_threads = config.decodeThreads,
            };
            int r = UNAudioNative.una_init(&c);
            if (r < 0 && config.deviceType == UNAudioDeviceType.Default)
            {
                // No usable output (headless machine, device busy): keep the timeline running.
                Debug.LogWarning($"[UNAudio] Output device failed ({(UNAudioResult)r}); falling back to the Null device.");
                c.device_type = (int)UNAudioDeviceType.Null;
                r = UNAudioNative.una_init(&c);
            }
            if (r < 0) return (UNAudioResult)r;

            una_device_info info;
            UNAudioNative.una_get_device_info(&info);
            s_Device = new UNAudioDeviceInfo(info);
            s_Initialized = true;
            s_Session++;
            UNAudioPlayerLoop.Install();
            UNAudioClock.OnSessionStart();
            return UNAudioResult.Ok;
        }

        /// <summary>
        /// Initialises with <see cref="DefaultConfig"/> if needed. Throws if the native engine cannot
        /// start; after such a failure lazy initialisation is not retried until <see cref="Initialize"/>
        /// is called explicitly.
        /// </summary>
        public static void EnsureInitialized()
        {
            if (s_Initialized) return;
            if (s_LazyInitFailure != UNAudioResult.Ok)
                throw new UNAudioException(s_LazyInitFailure, "UNAudio initialisation failed earlier");
            var r = Initialize(DefaultConfig);
            if (r != UNAudioResult.Ok && r != UNAudioResult.AlreadyInitialized)
            {
                s_LazyInitFailure = r;
                throw new UNAudioException(r, "UNAudio initialisation failed");
            }
        }

        /// <summary>Stops every voice, frees every clip and closes the device.</summary>
        public static void Shutdown()
        {
            if (!s_Initialized) return;
            s_Initialized = false;
            UNAudioNative.una_shutdown();
            // Snapshot first: a handler reacting to a cancelled clip may load a new one.
            var clips = new UNAudioClip[LiveClips.Count];
            LiveClips.Values.CopyTo(clips, 0);
            LiveClips.Clear();
            s_VoiceErrors.Clear();
            s_VoiceErrorOrder.Clear();
            foreach (var clip in clips) clip.OnEngineShutdown();
            ShutdownCompleted?.Invoke();
        }

        /// <summary>Whether the output stream is suspended (see <see cref="SetSuspended"/>).</summary>
        public static bool IsSuspended => s_Initialized && UNAudioNative.una_is_suspended() != 0;

        /// <summary>
        /// Stops or restarts the output stream without releasing clips or voices. The dsp
        /// timeline freezes while suspended. Called automatically on application pause when
        /// <see cref="SuspendWhenPaused"/> is set.
        /// </summary>
        public static void SetSuspended(bool suspended)
        {
            if (!s_Initialized) return;
            var r = UNAudioNative.una_set_suspended(suspended ? 1 : 0);
            if (r < 0) Debug.LogWarning($"[UNAudio] {(suspended ? "Suspend" : "Resume")} failed: {(UNAudioResult)r}");
        }

        internal static void ThrowIfNotMainThread(string api)
        {
            if (s_MainThreadId != 0 && System.Threading.Thread.CurrentThread.ManagedThreadId != s_MainThreadId)
                throw new InvalidOperationException($"{api} must be called from the Unity main thread.");
        }

        internal static void CaptureMainThread() => s_MainThreadId = System.Threading.Thread.CurrentThread.ManagedThreadId;

        /// <summary>
        /// Drops state left from a previous play session (enter play mode without a domain
        /// reload keeps statics, including handlers on destroyed objects and a remembered
        /// initialisation failure). Edit-mode subscribers are dropped too.
        /// </summary>
        internal static void ResetForPlaySession()
        {
            s_LazyInitFailure = UNAudioResult.Ok;
            VoiceStarted = null;
            VoiceEnded = null;
            VoiceLooped = null;
            VoiceStarved = null;
            VoiceError = null;
            DeviceChanged = null;
            ShutdownCompleted = null;
        }

        /// <summary>
        /// Reclaims finished voices and released clips, then dispatches native events.
        /// Runs automatically every frame; call it yourself only when the PlayerLoop is not
        /// running (edit-mode tools, tests).
        /// </summary>
        public static void Update()
        {
            if (!s_Initialized) return;
            UNAudioClock.Refresh();
            UNAudioNative.una_update();
            RefreshDeviceLatency();
            DispatchEvents();
            UNAudioPreloader.Tick();
        }

        // Once per frame rather than per Device read: UNAudioClock's conversions read Device on any
        // thread, and the native call takes the engine's control lock. The format itself only
        // changes with DeviceChanged.
        static void RefreshDeviceLatency()
        {
            una_device_info info;
            if (UNAudioNative.una_get_device_info(&info) == 0 && info.sample_rate == s_Device.SampleRate &&
                info.latency_frames != s_Device.LatencyFrames)
                s_Device = new UNAudioDeviceInfo(info);
        }

        static void DispatchEvents()
        {
            // A handler may shut the engine down (or restart it): the rest of the batch then
            // belongs to a dead session and must not be stamped with the new one.
            int session = s_Session;
            fixed (una_event* buf = s_Events)
            {
                int n;
                while ((n = UNAudioNative.una_poll_events(buf, s_Events.Length)) > 0)
                {
                    for (int i = 0; i < n; i++)
                    {
                        if (!s_Initialized || s_Session != session) return;
                        try { Dispatch(in buf[i]); }
                        catch (Exception e) { Debug.LogException(e); }
                    }
                    if (n < s_Events.Length) break;
                }
            }
        }

        static void Dispatch(in una_event e)
        {
            switch (e.type)
            {
                case 1: // CLIP_READY
                case 2: // CLIP_LOADED
                case 3: // CLIP_FAILED
                case 4: // CLIP_SOURCE_RELEASED
                    if (LiveClips.TryGetValue(e.handle, out var clip)) clip.OnNativeEvent(e.type, e.value);
                    break;
                case 5: // CLIP_FREED
                    if (LiveClips.TryGetValue(e.handle, out var freed))
                    {
                        LiveClips.Remove(e.handle);
                        freed.OnNativeEvent(e.type, e.value);
                    }
                    break;
                case 10: VoiceStarted?.Invoke(new UNAudioVoice(e.handle), e.dsp_frame); break;
                case 11: VoiceEnded?.Invoke(new UNAudioVoice(e.handle), e.dsp_frame); break;
                case 12: VoiceLooped?.Invoke(new UNAudioVoice(e.handle), e.dsp_frame); break;
                case 13: VoiceStarved?.Invoke(new UNAudioVoice(e.handle), e.value); break;
                case 14: // VOICE_ERROR
                    var error = (UNAudioResult)(int)e.value;
                    RecordVoiceError(e.handle, error);
                    VoiceError?.Invoke(new UNAudioVoice(e.handle), error);
                    break;
                case 20:
                    una_device_info info;
                    UNAudioNative.una_get_device_info(&info);
                    s_Device = new UNAudioDeviceInfo(info);
                    DeviceChanged?.Invoke((int)e.value);
                    break;
            }
        }

        static void RecordVoiceError(uint handle, UNAudioResult error)
        {
            if (!s_VoiceErrors.ContainsKey(handle))
            {
                if (s_VoiceErrorOrder.Count >= k_MaxVoiceErrors) s_VoiceErrors.Remove(s_VoiceErrorOrder.Dequeue());
                s_VoiceErrorOrder.Enqueue(handle);
            }
            s_VoiceErrors[handle] = error;
        }

        internal static UNAudioResult VoiceErrorOf(uint handle) =>
            s_VoiceErrors.TryGetValue(handle, out var r) ? r : UNAudioResult.Ok;

        // ── Mix ──────────────────────────────────────────────────────

        public static float MasterVolume
        {
            get => s_Initialized ? UNAudioNative.una_get_master_volume() : 1f;
            set => SetMasterVolume(value);
        }

        public static void SetMasterVolume(float volume, float rampSeconds = 0f)
        {
            EnsureInitialized();
            UNAudioNative.una_set_master_volume(volume, rampSeconds);
        }

        /// <summary>Sets a bus gain (16 buses; every voice plays through one, default 0).</summary>
        public static void SetBusVolume(int bus, float volume, float rampSeconds = 0f)
        {
            EnsureInitialized();
            UNAudioException.ThrowIfFailed(UNAudioNative.una_bus_set_volume(bus, volume, rampSeconds), "SetBusVolume");
        }

        /// <summary>Pauses or resumes every voice on a bus, optionally at an exact dsp frame.</summary>
        public static void SetBusPaused(int bus, bool paused, long atDspFrame = 0)
        {
            EnsureInitialized();
            UNAudioException.ThrowIfFailed(UNAudioNative.una_bus_set_paused(bus, paused ? 1 : 0, atDspFrame), "SetBusPaused");
        }

        public static void StopAll(float fadeSeconds = 0f)
        {
            if (s_Initialized) UNAudioNative.una_stop_all(fadeSeconds);
        }

        public static UNAudioStats GetStats()
        {
            EnsureInitialized();
            una_stats s;
            UNAudioNative.una_get_stats(&s);
            return new UNAudioStats(s);
        }

        /// <summary>Manual device only: renders interleaved stereo float frames into <paramref name="output"/>.</summary>
        public static void Render(float[] output, int frames)
        {
            if (output == null || output.Length < frames * 2) throw new ArgumentException("output too small");
            EnsureInitialized();
            fixed (float* p = output)
                UNAudioException.ThrowIfFailed(UNAudioNative.una_render(p, frames, 2), "Render");
        }
    }

    /// <summary>Installs the per-frame hook and the shutdown hooks.</summary>
    static class UNAudioPlayerLoop
    {
        struct UNAudioUpdate { }

        static bool s_HooksInstalled;

        public static void Install()
        {
            InsertIntoPlayerLoop();
            if (Application.isPlaying) UNAudioPauseHandler.Ensure();
            if (s_HooksInstalled) return;
            s_HooksInstalled = true;
            Application.quitting += UNAudioEngine.Shutdown;
#if UNITY_EDITOR
            UnityEditor.AssemblyReloadEvents.beforeAssemblyReload += UNAudioEngine.Shutdown;
            UnityEditor.EditorApplication.playModeStateChanged += state =>
            {
                // Play-mode clips and voices must not leak into edit mode (or survive
                // "enter play mode without domain reload").
                if (state == UnityEditor.PlayModeStateChange.ExitingPlayMode ||
                    state == UnityEditor.PlayModeStateChange.ExitingEditMode)
                    UNAudioEngine.Shutdown();
            };
            // The PlayerLoop does not tick in edit mode.
            UnityEditor.EditorApplication.update += () =>
            {
                if (!Application.isPlaying) UNAudioEngine.Update();
            };
#endif
        }

        static void InsertIntoPlayerLoop()
        {
            var root = PlayerLoop.GetCurrentPlayerLoop();
            for (int i = 0; i < root.subSystemList.Length; i++)
            {
                if (root.subSystemList[i].type != typeof(EarlyUpdate)) continue;
                var early = root.subSystemList[i];
                var list = new List<PlayerLoopSystem>(early.subSystemList ?? Array.Empty<PlayerLoopSystem>());
                if (list.Exists(s => s.type == typeof(UNAudioUpdate))) return;
                list.Insert(0, new PlayerLoopSystem { type = typeof(UNAudioUpdate), updateDelegate = UNAudioEngine.Update });
                early.subSystemList = list.ToArray();
                root.subSystemList[i] = early;
                PlayerLoop.SetPlayerLoop(root);
                return;
            }
        }

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
        static void OnPlayModeStart()
        {
            UNAudioEngine.CaptureMainThread();
            // Without a domain reload, statics survive into the next play session.
            UNAudioEngine.ResetForPlaySession();
        }

#if UNITY_EDITOR
        [UnityEditor.InitializeOnLoadMethod]
        static void OnEditorLoad() => UNAudioEngine.CaptureMainThread();
#endif
    }
}
