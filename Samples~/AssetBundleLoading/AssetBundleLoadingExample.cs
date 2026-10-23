using System.IO;
using UnityEngine;

namespace UNAudio.Samples
{
    /// <summary>
    /// Loads compressed audio out of an AssetBundle. The bundle may contain UNAudioAssets
    /// (imported .ogg/.wav/.mp3/.flac) or plain TextAssets (.bytes files holding that data).
    /// The encoded bytes go from the bundle's native memory to the native decoder:
    /// no managed byte[] is ever created.
    /// </summary>
    public class AssetBundleLoadingExample : MonoBehaviour
    {
        [Tooltip("Path relative to StreamingAssets")]
        public string bundlePath = "audio";
        public string[] assetNames = { "explosion", "theme" };

        AssetBundle m_Bundle;
        UNAudioClip[] m_Clips;

        async void Start()
        {
            var request = AssetBundle.LoadFromFileAsync(Path.Combine(Application.streamingAssetsPath, bundlePath));
            while (!request.isDone) await System.Threading.Tasks.Task.Yield();
            m_Bundle = request.assetBundle;
            if (m_Bundle == null)
            {
                Debug.LogError($"Bundle '{bundlePath}' not found");
                return;
            }

            m_Clips = new UNAudioClip[assetNames.Length];
            for (int i = 0; i < assetNames.Length; i++)
            {
                // Copy mode (default): the clip keeps its own copy, so the bundle can be
                // unloaded as soon as loading has started.
                m_Clips[i] = await UNAudioAssetBundles.LoadClipAsync(m_Bundle, assetNames[i]);
                Debug.Log($"{m_Clips[i].Name}: {m_Clips[i].Length:F2} s, {m_Clips[i].Info.MemoryBytes / 1024} KiB PCM");
            }
            m_Bundle.Unload(true);
            m_Bundle = null;
        }

        void OnGUI()
        {
            if (m_Clips == null) return;
            foreach (var clip in m_Clips)
                if (clip != null && GUILayout.Button($"Play {clip.Name}") && clip.IsReady)
                    clip.Play();
        }

        void OnDestroy()
        {
            if (m_Clips != null)
                foreach (var c in m_Clips) c?.Dispose();
            if (m_Bundle != null) m_Bundle.Unload(true);
        }
    }
}
