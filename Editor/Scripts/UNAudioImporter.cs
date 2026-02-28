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

            // Try to extract real metadata from audio headers (WAV, OGG, etc.)
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
                // Non-WAV format (mp3, ogg, flac) — metadata will be populated
                // at runtime when the native engine decodes the file.
                clip.SetMetadata(
                    sr:     0,
                    ch:     0,
                    bps:    0,
                    len:    0f,
                    frames: 0,
                    data:   audioData
                );

                string ext = Path.GetExtension(ctx.assetPath).ToLowerInvariant();
                ctx.LogImportWarning(
                    $"Could not parse audio header for '{ext}' format. " +
                    "Metadata will be populated at runtime after native decode.");
            }

            ctx.AddObjectToAsset("main", clip);
            ctx.SetMainObject(clip);
        }
    }
}
#endif
