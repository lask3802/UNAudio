using UnityEngine;

namespace UNAudio
{
    /// <summary>
    /// High-level singleton that manages the native audio engine lifetime
    /// and provides convenient access to engine-wide settings.
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
        [Tooltip("Output sample rate in Hz.")]
        public int sampleRate = 48000;

        [Tooltip("Number of output channels (1 = Mono, 2 = Stereo).")]
        public int outputChannels = 2;

        [Tooltip("Buffer size in frames. Smaller = lower latency, higher CPU.")]
        public int bufferSize = 256;

        [Tooltip("Number of buffers (double/triple buffering).")]
        public int bufferCount = 2;

        /// <summary>Whether the native engine is currently initialised.</summary>
        public bool IsInitialized => UNAudioBridge.IsInitialized() != 0;

        // ── Lifecycle ────────────────────────────────────────────

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
            if (instance == this)
            {
                UNAudioBridge.Shutdown();
                instance = null;
            }
        }

        /// <summary>Initialise the native engine with current settings.</summary>
        public void InitializeEngine()
        {
            if (IsInitialized) return;

            var config = new UNAudioOutputConfig
            {
                sampleRate    = sampleRate,
                channels      = outputChannels,
                bufferSize    = bufferSize,
                bufferCount   = bufferCount,
                exclusiveMode = 0
            };

            int result = UNAudioBridge.Initialize(config);
            if (result != 0)
                Debug.LogError($"[UNAudio] Engine initialisation failed (code {result}).");
            else
                Debug.Log("[UNAudio] Engine initialised successfully.");
        }

        // ── Engine-level controls ────────────────────────────────

        /// <summary>Set the master output volume (0 – 1).</summary>
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

        /// <summary>Get the current peak level for metering (0 – 1+).</summary>
        public float GetPeakLevel() => UNAudioBridge.GetPeakLevel();
    }
}
