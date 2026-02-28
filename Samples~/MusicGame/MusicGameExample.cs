using UnityEngine;

namespace UNAudio.Samples
{
    /// <summary>
    /// Demonstrates ultra-low-latency audio for rhythm / music games.
    /// Uses small buffer sizes (64-128 frames) for &lt; 5ms latency.
    /// Press Space or click the button to trigger hit sounds.
    /// </summary>
    public class MusicGameExample : MonoBehaviour
    {
        [Header("Audio Clips")]
        [Tooltip("Sound effect for note hits (use DecompressOnLoad).")]
        public UNAudioClip hitSound;

        [Tooltip("Background music track (use Streaming).")]
        public UNAudioClip bgm;

        [Header("Engine Settings")]
        [Tooltip("Buffer size in frames. Lower = less latency, more CPU.")]
        [Range(64, 512)]
        public int bufferSize = 128;

        private int hitCount;
        private float lastHitTime;
        private UNAudioSource bgmSource;

        private void Start()
        {
            // Configure for low latency
            UNAudioEngine.Instance.SetBufferSize(bufferSize);

            // Pre-decompress hit sounds for instant playback
            if (hitSound != null)
            {
                hitSound.SetLoadType(AudioLoadType.DecompressOnLoad);
                hitSound.LoadAudioData();
            }

            // Stream background music
            if (bgm != null)
            {
                bgm.SetLoadType(AudioLoadType.Streaming);

                bgmSource = gameObject.AddComponent<UNAudioSource>();
                bgmSource.clip = bgm;
                bgmSource.loop = true;
                bgmSource.volume = 0.6f;
                bgmSource.Play();
            }
        }

        private void Update()
        {
            if (Input.GetKeyDown(KeyCode.Space))
                OnNoteHit();
        }

        public void OnNoteHit()
        {
            if (hitSound == null) return;
            UNAudioSource.PlayOneShot(hitSound);
            hitCount++;
            lastHitTime = Time.time;
        }

        private void OnGUI()
        {
            GUILayout.BeginArea(new Rect(10, 10, 320, 280));
            GUILayout.Label("UNAudio - Music Game (Low Latency)", GUI.skin.box);

            float latency = UNAudioEngine.Instance.GetCurrentLatency();
            GUILayout.Label($"Buffer Size: {bufferSize} frames");
            GUILayout.Label($"Current Latency: {latency:F2} ms");
            GUILayout.Label($"Hit Count: {hitCount}");

            if (hitSound == null)
            {
                GUILayout.Label("Assign hitSound in the Inspector.");
            }
            else
            {
                GUILayout.Label($"Hit Sound: {hitSound.name} ({hitSound.LoadType})");
            }

            if (bgm == null)
            {
                GUILayout.Label("Assign bgm in the Inspector.");
            }
            else
            {
                GUILayout.Label($"BGM: {bgm.name} ({bgm.LoadType})");
                if (bgmSource != null)
                    GUILayout.Label($"BGM Time: {bgmSource.playbackTime:F2}s");
            }

            GUILayout.Space(10);

            if (GUILayout.Button("HIT! (or press Space)", GUILayout.Height(40)))
                OnNoteHit();

            // Flash indicator on hit
            if (Time.time - lastHitTime < 0.1f)
            {
                var style = new GUIStyle(GUI.skin.box);
                style.normal.background = Texture2D.whiteTexture;
                GUILayout.Box("", style, GUILayout.Height(10));
            }

            GUILayout.Space(5);
            GUILayout.Label("Buffer Size:");
            int newSize = (int)GUILayout.HorizontalSlider(bufferSize, 64, 512);
            if (newSize != bufferSize)
            {
                bufferSize = newSize;
                UNAudioEngine.Instance.SetBufferSize(bufferSize);
            }

            GUILayout.EndArea();
        }
    }
}
