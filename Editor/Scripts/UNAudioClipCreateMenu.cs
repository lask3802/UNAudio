#if UNITY_EDITOR
using System.IO;
using UnityEditor;
using UnityEngine;

namespace UNAudio.Editor
{
    /// <summary>
    /// Editor context menus for creating UNAudioClip assets from audio files.
    /// </summary>
    public static class UNAudioClipCreateMenu
    {
        private static readonly string[] SupportedExtensions = { ".wav", ".mp3", ".ogg", ".flac" };

        /// <summary>
        /// Create an embedded UNAudioClip from a file picker dialog.
        /// </summary>
        [MenuItem("Assets/Create/UNAudio/Audio Clip from File", priority = 201)]
        private static void CreateClipFromFile()
        {
            string filter = "Audio files;*.wav;*.mp3;*.ogg;*.flac";
            string path = EditorUtility.OpenFilePanel("Select Audio File", "", filter);

            if (string.IsNullOrEmpty(path)) return;

            CreateEmbeddedClipFromPath(path);
        }

        /// <summary>
        /// Create UNAudioClip assets from currently selected audio files in the Project window.
        /// </summary>
        [MenuItem("Assets/UNAudio/Create Clip from Selected", priority = 100)]
        private static void CreateClipFromSelected()
        {
            var selected = Selection.objects;
            int created = 0;

            foreach (var obj in selected)
            {
                string assetPath = AssetDatabase.GetAssetPath(obj);
                if (string.IsNullOrEmpty(assetPath)) continue;

                string ext = Path.GetExtension(assetPath).ToLowerInvariant();
                if (!IsSupportedExtension(ext)) continue;

                string fullPath = Path.GetFullPath(assetPath);
                CreateEmbeddedClipFromPath(fullPath, Path.GetDirectoryName(assetPath));
                created++;
            }

            if (created == 0)
            {
                EditorUtility.DisplayDialog(
                    "UNAudio",
                    "No supported audio files selected.\nSupported formats: WAV, MP3, OGG, FLAC",
                    "OK");
            }
        }

        [MenuItem("Assets/UNAudio/Create Clip from Selected", validate = true)]
        private static bool CreateClipFromSelectedValidate()
        {
            foreach (var obj in Selection.objects)
            {
                string assetPath = AssetDatabase.GetAssetPath(obj);
                if (string.IsNullOrEmpty(assetPath)) continue;

                string ext = Path.GetExtension(assetPath).ToLowerInvariant();
                if (IsSupportedExtension(ext)) return true;
            }
            return false;
        }

        private static void CreateEmbeddedClipFromPath(string fullPath, string targetFolder = null)
        {
            if (!File.Exists(fullPath))
            {
                Debug.LogError($"[UNAudio] File not found: {fullPath}");
                return;
            }

            byte[] audioData = File.ReadAllBytes(fullPath);
            string clipName = Path.GetFileNameWithoutExtension(fullPath);

            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            clip.name = clipName;

            // Try to extract metadata from audio headers (WAV, OGG, etc.)
            if (AudioHeaderParser.TryParse(audioData, out AudioMetadata meta))
            {
                clip.SetMetadata(
                    sr:     meta.sampleRate,
                    ch:     meta.channels,
                    bps:    meta.bitsPerSample,
                    len:    meta.lengthInSeconds,
                    frames: meta.totalFrames,
                    data:   audioData
                );
            }
            else
            {
                clip.SetMetadata(
                    sr:     0,
                    ch:     0,
                    bps:    0,
                    len:    0f,
                    frames: 0,
                    data:   audioData
                );
            }

            // Determine save path
            if (string.IsNullOrEmpty(targetFolder))
                targetFolder = GetActiveFolder();

            string assetPath = AssetDatabase.GenerateUniqueAssetPath(
                $"{targetFolder}/{clipName}.asset");

            AssetDatabase.CreateAsset(clip, assetPath);
            AssetDatabase.SaveAssets();
            AssetDatabase.Refresh();

            EditorUtility.FocusProjectWindow();
            Selection.activeObject = clip;

            Debug.Log($"[UNAudio] Created clip: {assetPath}");
        }

        private static bool IsSupportedExtension(string ext)
        {
            foreach (var supported in SupportedExtensions)
            {
                if (ext == supported) return true;
            }
            return false;
        }

        private static string GetActiveFolder()
        {
            // Try to get the folder currently active in the Project window
            foreach (var obj in Selection.GetFiltered<Object>(SelectionMode.Assets))
            {
                string path = AssetDatabase.GetAssetPath(obj);
                if (!string.IsNullOrEmpty(path))
                {
                    if (Directory.Exists(path))
                        return path;
                    return Path.GetDirectoryName(path);
                }
            }
            return "Assets";
        }
    }
}
#endif
