#if UNITY_EDITOR
using UnityEditor;
using UnityEngine;

namespace UNAudio.Editor
{
    /// <summary>
    /// Custom Inspector for <see cref="UNAudioClip"/> assets.
    /// Shows metadata, source type configuration, memory usage, and playback controls.
    /// </summary>
    [CustomEditor(typeof(UNAudioClip))]
    public class UNAudioInspector : UnityEditor.Editor
    {
        private SerializedProperty sourceTypeProp;
        private SerializedProperty audioFilePathProp;
        private SerializedProperty loadTypeProp;
        private SerializedProperty compressedDataProp;
        private SerializedProperty sampleRateProp;
        private SerializedProperty channelsProp;
        private SerializedProperty bitsPerSampleProp;
        private SerializedProperty lengthProp;
        private SerializedProperty totalFramesProp;
#if UNAUDIO_ADDRESSABLES
        private SerializedProperty addressableDataReferenceProp;
#endif

        private void OnEnable()
        {
            sourceTypeProp     = serializedObject.FindProperty("sourceType");
            audioFilePathProp  = serializedObject.FindProperty("audioFilePath");
            loadTypeProp       = serializedObject.FindProperty("loadType");
            compressedDataProp = serializedObject.FindProperty("compressedData");
            sampleRateProp     = serializedObject.FindProperty("sampleRateValue");
            channelsProp       = serializedObject.FindProperty("channelsValue");
            bitsPerSampleProp  = serializedObject.FindProperty("bitsPerSampleValue");
            lengthProp         = serializedObject.FindProperty("lengthValue");
            totalFramesProp    = serializedObject.FindProperty("totalFramesValue");
#if UNAUDIO_ADDRESSABLES
            addressableDataReferenceProp = serializedObject.FindProperty("addressableDataReference");
#endif
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            EditorGUILayout.LabelField("UNAudioClip", EditorStyles.boldLabel);
            EditorGUILayout.Space();

            // ── Source Type ──────────────────────────────────────
            EditorGUILayout.PropertyField(sourceTypeProp, new GUIContent("Source Type"));
            var srcType = (AudioSourceType)sourceTypeProp.enumValueIndex;

            switch (srcType)
            {
                case AudioSourceType.Embedded:
                    DrawEmbeddedUI();
                    break;
                case AudioSourceType.Path:
                    DrawPathUI();
                    break;
                case AudioSourceType.Addressable:
                    DrawAddressableUI();
                    break;
            }

            EditorGUILayout.Space();

            // ── Load Type ────────────────────────────────────────
            EditorGUILayout.PropertyField(loadTypeProp, new GUIContent("Load Type"));

            EditorGUILayout.Space();

            // ── Metadata (read-only) ─────────────────────────────
            EditorGUILayout.LabelField("Metadata", EditorStyles.boldLabel);

            using (new EditorGUI.DisabledGroupScope(true))
            {
                int sr  = sampleRateProp.intValue;
                int ch  = channelsProp.intValue;
                int bps = bitsPerSampleProp.intValue;
                float len = lengthProp.floatValue;
                long frames = totalFramesProp.longValue;

                EditorGUILayout.LabelField("Sample Rate", sr > 0 ? $"{sr} Hz" : "Unknown");
                EditorGUILayout.LabelField("Channels", ch > 0 ? ch.ToString() : "Unknown");
                EditorGUILayout.LabelField("Bits/Sample", bps > 0 ? bps.ToString() : "Unknown");
                EditorGUILayout.LabelField("Length", len > 0 ? $"{len:F2} s" : "Unknown");
                EditorGUILayout.LabelField("Total Frames", frames > 0 ? frames.ToString("N0") : "Unknown");

                long memBytes = compressedDataProp.arraySize;
                string memDisplay = memBytes > 0 ? FormatBytes(memBytes) : "No embedded data";
                EditorGUILayout.LabelField("Embedded Data", memDisplay);
            }

            EditorGUILayout.Space();

            // ── Preview controls ─────────────────────────────────
            EditorGUILayout.LabelField("Preview", EditorStyles.boldLabel);
            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button("Play"))
                {
                    // TODO: Preview playback in editor
                    Debug.Log($"[UNAudio] Preview play: {target.name}");
                }
                if (GUILayout.Button("Stop"))
                {
                    Debug.Log($"[UNAudio] Preview stop: {target.name}");
                }
            }

            serializedObject.ApplyModifiedProperties();
        }

        private void DrawEmbeddedUI()
        {
            long bytes = compressedDataProp.arraySize;
            if (bytes > 0)
            {
                EditorGUILayout.HelpBox(
                    $"Embedded audio data: {FormatBytes(bytes)}",
                    MessageType.Info);
            }
            else
            {
                EditorGUILayout.HelpBox(
                    "No embedded data. Import a .una file or use the Create menu to embed audio.",
                    MessageType.Warning);
            }
        }

        private void DrawPathUI()
        {
            EditorGUILayout.PropertyField(audioFilePathProp, new GUIContent("File Path"));
            EditorGUILayout.HelpBox(
                "Relative paths are resolved from StreamingAssets.\n" +
                "Absolute paths are used as-is.",
                MessageType.Info);
        }

        private void DrawAddressableUI()
        {
#if UNAUDIO_ADDRESSABLES
            if (addressableDataReferenceProp != null)
            {
                EditorGUILayout.PropertyField(
                    addressableDataReferenceProp,
                    new GUIContent("Addressable Data"));
            }
            else
            {
                EditorGUILayout.HelpBox(
                    "Addressable reference field not found.",
                    MessageType.Error);
            }
#else
            EditorGUILayout.HelpBox(
                "The com.unity.addressables package is not installed.\n" +
                "Install it via Package Manager to enable Addressable source type.",
                MessageType.Warning);
#endif
        }

        private static string FormatBytes(long bytes)
        {
            if (bytes < 1024) return $"{bytes} B";
            if (bytes < 1024 * 1024) return $"{bytes / 1024f:F1} KB";
            return $"{bytes / (1024f * 1024f):F1} MB";
        }
    }
}
#endif
