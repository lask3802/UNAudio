using System.IO;
using UnityEngine;
#if UNITY_EDITOR
using UnityEditor;
using UnityEditor.AssetImporters;

namespace UNAudio.Editor
{
    /// <summary>
    /// Custom ScriptedImporter that intercepts audio files and creates
    /// <see cref="UNAudioClip"/> assets with the chosen compression mode.
    /// </summary>
    [ScriptedImporter(1, new[] { "una" })]
    public class UNAudioImporter : ScriptedImporter
    {
        [Tooltip("How the audio data is stored in memory.")]
        public AudioLoadType compressionMode = AudioLoadType.CompressedInMemory;

        [Tooltip("Preload audio data when the scene loads.")]
        public bool preloadAudioData;

        [Tooltip("Load audio data on a background thread.")]
        public bool loadInBackground = true;

        public override void OnImportAsset(AssetImportContext ctx)
        {
            // Read raw file bytes
            byte[] audioData = File.ReadAllBytes(ctx.assetPath);

            // Create the clip ScriptableObject
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            clip.name = Path.GetFileNameWithoutExtension(ctx.assetPath);

            // TODO: Extract real metadata (sample rate, channels, etc.) from the
            //       audio data using the native decoder.  For now, use placeholders.
            clip.SetMetadata(
                sr:   44100,
                ch:   2,
                bps:  16,
                len:  0f,
                data: audioData
            );

            ctx.AddObjectToAsset("main", clip);
            ctx.SetMainObject(clip);
        }
    }
}
#endif
