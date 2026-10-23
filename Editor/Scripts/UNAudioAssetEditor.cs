using UnityEditor;
using UnityEngine;

namespace UNAudio.Editor
{
    [CustomEditor(typeof(UNAudioAsset))]
    sealed class UNAudioAssetEditor : UnityEditor.Editor
    {
        static UNAudioVoice s_Preview;
        static UNAudioClip s_PreviewClip;

        public override void OnInspectorGUI()
        {
            var a = (UNAudioAsset)target;
            using (new EditorGUI.DisabledScope(true))
            {
                EditorGUILayout.TextField("Format", a.Format);
                EditorGUILayout.IntField("Channels", a.Channels);
                EditorGUILayout.IntField("Sample Rate", a.SampleRate);
                EditorGUILayout.LongField("Frames", a.Frames);
                EditorGUILayout.TextField("Length", $"{a.Length:F3} s");
                EditorGUILayout.TextField("File Size", a.Data != null ? EditorUtility.FormatBytes(a.Data.dataSize) : "-");
                EditorGUILayout.EnumPopup("Load Mode", a.LoadOptions.mode);
                EditorGUILayout.Toggle("Preload Audio Data", a.PreloadAudioData);
                if (a.LoadOptions.mode == UNAudioLoadMode.Decode)
                {
                    EditorGUILayout.EnumPopup("Sample Format", a.LoadOptions.sampleFormat);
                    int bytesPerSample = a.LoadOptions.sampleFormat == UNAudioSampleFormat.Int16 ? 2 : 4;
                    long pcm = a.Frames * System.Math.Min(2, System.Math.Max(1, a.Channels)) * bytesPerSample;
                    EditorGUILayout.TextField("Decoded Size (approx.)", EditorUtility.FormatBytes(pcm));
                }
            }
            EditorGUILayout.HelpBox("Import settings live on the importer (select the source file).", MessageType.None);

            EditorGUILayout.Space();
            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button("Play")) Play(a);
                if (GUILayout.Button("Stop")) StopPreview();
            }
            if (s_Preview.IsAlive)
            {
                var t = s_Preview.GetTiming();
                EditorGUILayout.LabelField("Position", $"{t.AudibleSeconds:F3} s");
                Repaint();
            }
        }

        static void Play(UNAudioAsset a)
        {
            StopPreview();
            try
            {
                s_PreviewClip = a.Load();
                s_PreviewClip.Ready += c =>
                {
                    if (c == s_PreviewClip) s_Preview = c.Play();
                };
            }
            catch (System.Exception e)
            {
                Debug.LogException(e);
            }
        }

        static void StopPreview()
        {
            if (s_Preview.IsAlive) s_Preview.Stop(0.02f);
            s_Preview = default;
            s_PreviewClip?.Dispose();
            s_PreviewClip = null;
        }

        void OnDisable() => StopPreview();
    }
}
