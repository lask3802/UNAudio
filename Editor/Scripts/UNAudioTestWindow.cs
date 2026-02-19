#if UNITY_EDITOR
using UnityEditor;
using UnityEngine;

namespace UNAudio.Editor
{
    /// <summary>
    /// Editor window for testing and profiling the UNAudio engine.
    /// Access via Window → UNAudio → Test Panel.
    /// </summary>
    public class UNAudioTestWindow : EditorWindow
    {
        private UNAudioClip testClip;

        [MenuItem("Window/UNAudio/Test Panel")]
        public static void ShowWindow()
        {
            GetWindow<UNAudioTestWindow>("UNAudio Test");
        }

        private void OnGUI()
        {
            GUILayout.Label("UNAudio Test Panel", EditorStyles.boldLabel);
            EditorGUILayout.Space();

            // ── Engine status ────────────────────────────────────
            GUILayout.Label("Engine Status", EditorStyles.boldLabel);
            EditorGUILayout.LabelField("Initialized",
                Application.isPlaying ? UNAudioEngine.Instance.IsInitialized.ToString() : "N/A (Play mode only)");

            EditorGUILayout.Space();

            // ── Latency test ─────────────────────────────────────
            GUILayout.Label("Latency Test", EditorStyles.boldLabel);
            if (GUILayout.Button("Measure Latency"))
            {
                if (Application.isPlaying)
                {
                    float latency = UNAudioBridge.GetCurrentLatency();
                    Debug.Log($"[UNAudio] Current latency: {latency:F1} ms");
                }
                else
                {
                    Debug.LogWarning("[UNAudio] Enter Play mode to measure latency.");
                }
            }

            EditorGUILayout.Space();

            // ── Clip test ────────────────────────────────────────
            GUILayout.Label("Format Test", EditorStyles.boldLabel);
            testClip = (UNAudioClip)EditorGUILayout.ObjectField(
                "Test Clip", testClip, typeof(UNAudioClip), false);

            if (GUILayout.Button("Test Decode") && testClip != null)
            {
                Debug.Log($"[UNAudio] Test decode: {testClip.name} " +
                          $"({testClip.sampleRate} Hz, {testClip.channels} ch)");
                // TODO: Run native decode test
            }

            EditorGUILayout.Space();

            // ── Performance stats ────────────────────────────────
            GUILayout.Label("Performance", EditorStyles.boldLabel);
            if (Application.isPlaying)
            {
                var stats = UNAudioDebug.GetPerformanceStats();
                EditorGUILayout.LabelField("Latency",       $"{stats.latencyMs:F1} ms");
                EditorGUILayout.LabelField("Master Volume",  $"{stats.masterVolume:F2}");
            }
            else
            {
                EditorGUILayout.HelpBox("Enter Play mode to view live stats.", MessageType.Info);
            }
        }
    }
}
#endif
