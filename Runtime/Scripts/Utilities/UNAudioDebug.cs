using UnityEngine;

namespace UNAudio
{
    /// <summary>
    /// Debug and profiling helpers for UNAudio.
    /// </summary>
    public static class UNAudioDebug
    {
        /// <summary>Enable verbose native-side logging.</summary>
        public static bool EnableVerboseLogging { get; set; }

        /// <summary>Log the audio path for a given source.</summary>
        public static void TraceAudioPath(UNAudioSource source)
        {
            if (source == null || source.clip == null)
            {
                Debug.Log("[UNAudio] TraceAudioPath: source or clip is null.");
                return;
            }

            Debug.Log($"[UNAudio] Source: {source.name}");
            Debug.Log($"[UNAudio] Clip: {source.clip.name} " +
                      $"({source.clip.sampleRate} Hz, {source.clip.channels} ch, " +
                      $"{source.clip.LoadType})");
            Debug.Log($"[UNAudio] Latency: {UNAudioBridge.GetCurrentLatency():F1} ms");
        }

        /// <summary>Get a snapshot of engine performance stats.</summary>
        public static AudioPerformanceStats GetPerformanceStats()
        {
            return new AudioPerformanceStats
            {
                latencyMs    = UNAudioBridge.GetCurrentLatency(),
                masterVolume = UNAudioBridge.GetMasterVolume()
            };
        }
    }

    /// <summary>
    /// A snapshot of engine performance metrics.
    /// </summary>
    public struct AudioPerformanceStats
    {
        public float latencyMs;
        public float masterVolume;
        // TODO: cpuUsage, bufferUnderruns, activeVoices, memoryUsage
    }
}
