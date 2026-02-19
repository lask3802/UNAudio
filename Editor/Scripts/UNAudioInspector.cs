#if UNITY_EDITOR
using UnityEditor;
using UnityEngine;

namespace UNAudio.Editor
{
    /// <summary>
    /// Custom Inspector for <see cref="UNAudioClip"/> assets.
    /// Shows metadata, memory usage, and playback controls.
    /// </summary>
    [CustomEditor(typeof(UNAudioClip))]
    public class UNAudioInspector : UnityEditor.Editor
    {
        public override void OnInspectorGUI()
        {
            var clip = (UNAudioClip)target;

            EditorGUILayout.LabelField("UNAudioClip", EditorStyles.boldLabel);
            EditorGUILayout.Space();

            // Metadata
            EditorGUILayout.LabelField("Sample Rate", $"{clip.sampleRate} Hz");
            EditorGUILayout.LabelField("Channels",    clip.channels.ToString());
            EditorGUILayout.LabelField("Bits/Sample",  clip.bitsPerSample.ToString());
            EditorGUILayout.LabelField("Length",       $"{clip.length:F2} s");
            EditorGUILayout.LabelField("Load Type",    clip.LoadType.ToString());
            EditorGUILayout.LabelField("Compressed",   clip.IsCompressed ? "Yes" : "No");

            long memKB = clip.GetMemorySize() / 1024;
            EditorGUILayout.LabelField("Memory",      $"{memKB} KB");

            EditorGUILayout.Space();

            // Preview controls
            EditorGUILayout.LabelField("Preview", EditorStyles.boldLabel);

            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button("Play"))
                {
                    // TODO: Preview playback in editor
                    Debug.Log($"[UNAudio] Preview play: {clip.name}");
                }
                if (GUILayout.Button("Stop"))
                {
                    Debug.Log($"[UNAudio] Preview stop: {clip.name}");
                }
            }
        }
    }
}
#endif
