using System;
using System.IO;
using UnityEditor;
using UnityEditor.AssetImporters;
using UnityEngine;

namespace UNAudio.Editor
{
    /// <summary>
    /// Imports .ogg / .wav / .mp3 / .flac as a <see cref="UNAudioAsset"/> whose data is the
    /// original file, untouched (no re-encode, no AudioClip). It is an importer <i>override</i>:
    /// Unity keeps importing other audio files as AudioClips. Switch a file with
    /// Assets ▸ UNAudio ▸ Import as UNAudio Asset, or in code with
    /// <see cref="UNAudioImporterUtility.UseUNAudio(string)"/>.
    /// </summary>
    [ScriptedImporter(3, null, new[] { "ogg", "wav", "mp3", "flac" })]
    public sealed class UNAudioImporter : ScriptedImporter
    {
        [Tooltip("Decode: whole clip to PCM in the background (sound effects). Stream: decode while playing (music).")]
        public UNAudioLoadMode loadMode = UNAudioLoadMode.Decode;
        [Tooltip("PCM storage for decoded clips. Int16 halves memory.")]
        public UNAudioSampleFormat sampleFormat = UNAudioSampleFormat.Int16;
        [Tooltip("Keep the file's sample rate and resample while mixing instead of at load time.")]
        public bool keepSourceRate;
        [Tooltip("Higher loads first when many clips decode at once.")]
        public int priority;
        [Tooltip("Start loading when the asset is loaded (scene, Resources, AssetBundle) instead of on first use, " +
                 "like AudioClip's Preload Audio Data.")]
        public bool preloadAudioData;

        public override void OnImportAsset(AssetImportContext ctx)
        {
            byte[] bytes = File.ReadAllBytes(ctx.assetPath);
            string baseName = Path.GetFileNameWithoutExtension(ctx.assetPath);

            UNAudioClipInfo info = default;
            string format = Path.GetExtension(ctx.assetPath).TrimStart('.').ToLowerInvariant() switch
            {
                "wav" => "wav",
                "mp3" => "mp3",
                "flac" => "flac",
                _ => "vorbis",
            };
            try
            {
                if (!UNAudioClip.TryProbe(bytes, out info))
                    ctx.LogImportError($"UNAudio: '{ctx.assetPath}' is not a supported Ogg Vorbis / WAV / MP3 / FLAC file.");
            }
            catch (DllNotFoundException)
            {
                ctx.LogImportWarning("UNAudio: native plugin not found; metadata left empty. Build Native/ and copy the plugin into Runtime/Plugins.");
            }

            var data = new TextAsset(new ReadOnlySpan<byte>(bytes)) { name = baseName + ".data" };
            var asset = ScriptableObject.CreateInstance<UNAudioAsset>();
            asset.name = baseName;
            var options = new UNAudioLoadOptions
            {
                mode = loadMode,
                sampleFormat = sampleFormat,
                keepSourceRate = keepSourceRate,
                priority = priority,
            };
            asset.EditorSetup(data, options, preloadAudioData, info, format);

            ctx.AddObjectToAsset("data", data);
            ctx.AddObjectToAsset("main", asset, EditorGUIUtility.IconContent("AudioClip Icon").image as Texture2D);
            ctx.SetMainObject(asset);
        }
    }

    public static class UNAudioImporterUtility
    {
        /// <summary>Imports an .ogg/.wav/.mp3/.flac as a UNAudioAsset instead of an AudioClip.</summary>
        public static void UseUNAudio(string assetPath)
        {
            AssetDatabase.SetImporterOverride<UNAudioImporter>(assetPath);
        }

        /// <summary>Restores Unity's AudioClip import.</summary>
        public static void UseAudioClip(string assetPath)
        {
            AssetDatabase.ClearImporterOverride(assetPath);
        }

        static bool IsAudio(string path)
        {
            var ext = Path.GetExtension(path).ToLowerInvariant();
            return ext == ".ogg" || ext == ".wav" || ext == ".mp3" || ext == ".flac";
        }

        [MenuItem("Assets/UNAudio/Import as UNAudio Asset", false, 1100)]
        static void UseSelected()
        {
            foreach (var guid in Selection.assetGUIDs)
            {
                var path = AssetDatabase.GUIDToAssetPath(guid);
                if (IsAudio(path)) UseUNAudio(path);
            }
        }

        [MenuItem("Assets/UNAudio/Import as Unity AudioClip", false, 1101)]
        static void RevertSelected()
        {
            foreach (var guid in Selection.assetGUIDs)
            {
                var path = AssetDatabase.GUIDToAssetPath(guid);
                if (IsAudio(path)) UseAudioClip(path);
            }
        }

        [MenuItem("Assets/UNAudio/Import as UNAudio Asset", true)]
        [MenuItem("Assets/UNAudio/Import as Unity AudioClip", true)]
        static bool ValidateSelection()
        {
            foreach (var guid in Selection.assetGUIDs)
                if (IsAudio(AssetDatabase.GUIDToAssetPath(guid))) return true;
            return false;
        }
    }
}
