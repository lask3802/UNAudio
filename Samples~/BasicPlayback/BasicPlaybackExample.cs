using UnityEngine;

namespace UNAudio.Samples
{
    /// <summary>
    /// Play / stop / fade an imported UNAudioAsset. Assign an .ogg, .wav, .mp3 or .flac switched to
    /// the UNAudio importer (Assets ▸ UNAudio ▸ Import as UNAudio Asset).
    /// Uses IMGUI buttons so it runs with either input backend.
    /// </summary>
    public class BasicPlaybackExample : MonoBehaviour
    {
        public UNAudioAsset sound;
        public UNAudioAsset music;

        UNAudioVoice m_Music;

        async void Start()
        {
            // Decoding runs on native worker threads; await it during loading.
            if (sound != null) await sound.LoadAsync();
            if (music != null) await music.LoadAsync();
            Debug.Log($"UNAudio ready: {UNAudioEngine.Device}");
        }

        void OnGUI()
        {
            if (GUILayout.Button("Play sound") && sound != null && sound.Clip.IsReady)
            {
                var p = UNAudioPlayParams.Default;
                p.pitch = Random.Range(0.9f, 1.1f);
                p.pan = Random.Range(-0.5f, 0.5f);
                sound.Play(p);
            }
            if (GUILayout.Button(m_Music.IsAlive ? "Stop music" : "Loop music") && music != null && music.Clip.IsReady)
            {
                if (m_Music.IsAlive)
                {
                    m_Music.Stop();
                }
                else
                {
                    var p = UNAudioPlayParams.Default;
                    p.loop = true;
                    p.fadeInSeconds = 0.5f;
                    p.bus = 1;  // music bus
                    m_Music = music.Play(p);
                }
            }
            if (GUILayout.Button("Fade out music") && m_Music.IsAlive) m_Music.Stop(fadeSeconds: 1.5f);

            var stats = UNAudioEngine.IsInitialized ? UNAudioEngine.GetStats() : default;
            GUILayout.Label($"Voices {stats.ActiveVoices}  CPU {stats.CpuLoad * 100f:F1}%  " +
                            $"Latency {UNAudioClock.OutputLatency * 1000.0:F1} ms");
            if (m_Music.IsAlive) GUILayout.Label($"Music at {m_Music.Time:F3} s");
        }
    }
}
