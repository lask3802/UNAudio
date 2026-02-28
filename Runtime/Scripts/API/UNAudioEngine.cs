using UnityEngine;

namespace UNAudio
{
    /// <summary>
    /// High-level singleton that manages the native audio engine lifetime
    /// and provides convenient access to engine-wide settings.
    /// Auto-detects system audio format on initialization.
    /// </summary>
    public class UNAudioEngine : MonoBehaviour
    {
        private static UNAudioEngine instance;

        /// <summary>Global engine instance (created on first access).</summary>
        public static UNAudioEngine Instance
        {
            get
            {
                if (instance == null)
                {
                    var go = new GameObject("[UNAudioEngine]");
                    instance = go.AddComponent<UNAudioEngine>();
                    DontDestroyOnLoad(go);
                }
                return instance;
            }
        }

        [Header("Output Settings")]
        [Tooltip("Auto-detect system sample rate and channel count on initialization.")]
        public bool autoDetectSystemFormat = true;

        [Tooltip("Output sample rate in Hz. Ignored when auto-detect is enabled.")]
        public int sampleRate = 48000;

        [Tooltip("Number of output channels (1 = Mono, 2 = Stereo). Ignored when auto-detect is enabled.")]
        public int outputChannels = 2;

        [Tooltip("Buffer size in frames. Smaller = lower latency, higher CPU.")]
        public int bufferSize = 512;

        [Tooltip("Number of buffers (double/triple/quad buffering). Higher = more stable, more latency.")]
        public int bufferCount = 4;

        [Header("System Info (Read Only)")]
        [SerializeField] private int systemSampleRate;
        [SerializeField] private int systemChannels;
        [SerializeField] private int systemBitsPerSample;

        /// <summary>Whether the native engine is currently initialised.</summary>
        public bool IsInitialized => UNAudioBridge.IsInitialized() != 0;

        /// <summary>The detected system sample rate (0 if detection failed).</summary>
        public int SystemSampleRate => systemSampleRate;

        /// <summary>The detected system channel count (0 if detection failed).</summary>
        public int SystemChannels => systemChannels;

        /// <summary>The detected system bit depth (0 if detection failed).</summary>
        public int SystemBitsPerSample => systemBitsPerSample;

        // ── Lifecycle ────────────────────────────────────────────

        /// <summary>
        /// Ensure clean state when entering Play mode.
        /// Called before any Awake, so the native engine is shut down if stale.
        /// </summary>
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
        private static void OnSubsystemRegistration()
        {
            // Domain reload may have reset the static C# instance field while
            // the native DLL singleton is still alive. Force a clean shutdown
            // so the next Initialize() starts fresh.
            if (UNAudioBridge.IsInitialized() != 0)
            {
                Debug.Log("[UNAudio] Cleaning up stale native engine from previous session.");
                UNAudioBridge.Shutdown();
            }
            instance = null;
        }

        private void Awake()
        {
            if (instance != null && instance != this)
            {
                Destroy(gameObject);
                return;
            }
            instance = this;
            DontDestroyOnLoad(gameObject);
            InitializeEngine();
        }

        private void OnDestroy()
        {
            ShutdownEngine();
        }

        private void OnApplicationQuit()
        {
            ShutdownEngine();
        }

        private void ShutdownEngine()
        {
            if (instance == this)
            {
                if (UNAudioBridge.IsInitialized() != 0)
                {
                    UNAudioBridge.Shutdown();
                    Debug.Log("[UNAudio] Engine shut down.");
                }
                instance = null;
            }
        }

        /// <summary>Initialise the native engine with current settings.</summary>
        public void InitializeEngine()
        {
            if (IsInitialized) return;

            // Query system audio format for auto-detection
            DetectSystemFormat();

            int initSampleRate = sampleRate;
            int initChannels = outputChannels;

            if (autoDetectSystemFormat && systemSampleRate > 0)
            {
                initSampleRate = systemSampleRate;
                sampleRate = systemSampleRate;
                Debug.Log($"[UNAudio] Auto-detected system sample rate: {systemSampleRate} Hz, " +
                          $"channels: {systemChannels}, bit depth: {systemBitsPerSample}");
            }

            if (autoDetectSystemFormat && systemChannels > 0)
            {
                // Clamp to stereo for now (mixer only supports mono/stereo)
                initChannels = Mathf.Min(systemChannels, 2);
                outputChannels = initChannels;
            }

            var config = new UNAudioOutputConfig
            {
                sampleRate    = initSampleRate,
                channels      = initChannels,
                bufferSize    = bufferSize,
                bufferCount   = bufferCount,
                exclusiveMode = 0
            };

            int result = UNAudioBridge.Initialize(config);
            if (result != 0)
                Debug.LogError($"[UNAudio] Engine initialisation failed (code {result}).");
            else
                Debug.Log($"[UNAudio] Engine initialised: {initSampleRate} Hz, " +
                          $"{initChannels} ch, buffer {bufferSize}x{bufferCount}");
        }

        /// <summary>Query the system's default audio endpoint format.</summary>
        public void DetectSystemFormat()
        {
            try
            {
                var fmt = UNAudioBridge.GetSystemAudioFormat();
                if (fmt.isValid != 0)
                {
                    systemSampleRate = fmt.sampleRate;
                    systemChannels = fmt.channels;
                    systemBitsPerSample = fmt.bitsPerSample;
                }
                else
                {
                    Debug.LogWarning("[UNAudio] Failed to detect system audio format, using defaults.");
                }
            }
            catch (System.Exception ex)
            {
                Debug.LogWarning($"[UNAudio] System format detection unavailable: {ex.Message}");
            }
        }

        // ── Engine-level controls ────────────────────────────────

        /// <summary>Set the master output volume (0 - 1).</summary>
        public void SetMasterVolume(float volume) => UNAudioBridge.SetMasterVolume(volume);

        /// <summary>Get the master output volume.</summary>
        public float GetMasterVolume() => UNAudioBridge.GetMasterVolume();

        /// <summary>Change the audio buffer size at runtime.</summary>
        public void SetBufferSize(int frames)
        {
            bufferSize = frames;
            UNAudioBridge.SetBufferSize(frames);
        }

        /// <summary>Get the estimated output latency in milliseconds.</summary>
        public float GetCurrentLatency() => UNAudioBridge.GetCurrentLatency();

        /// <summary>Get the current peak level for metering (0 - 1+).</summary>
        public float GetPeakLevel() => UNAudioBridge.GetPeakLevel();
    }
}
