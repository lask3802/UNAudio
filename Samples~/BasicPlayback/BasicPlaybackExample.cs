using UnityEngine;

namespace UNAudio.Samples
{
    /// <summary>
    /// Demonstrates basic audio playback using UNAudioSource.
    /// Assign a UNAudioClip in the Inspector and press Play.
    /// </summary>
    public class BasicPlaybackExample : MonoBehaviour
    {
        [Header("Audio")]
        [Tooltip("The audio clip to play.")]
        public UNAudioClip clip;

        [Tooltip("Play automatically on Start.")]
        public bool playOnAwake = true;

        private UNAudioSource source;

        private void Start()
        {
            source = GetComponent<UNAudioSource>();
            if (source == null)
                source = gameObject.AddComponent<UNAudioSource>();

            source.clip = clip;

            if (playOnAwake && clip != null)
                source.Play();
        }

        private void OnGUI()
        {
            GUILayout.BeginArea(new Rect(10, 10, 300, 200));
            GUILayout.Label("UNAudio - Basic Playback", GUI.skin.box);

            if (source == null || clip == null)
            {
                GUILayout.Label("Assign a UNAudioClip in the Inspector.");
                GUILayout.EndArea();
                return;
            }

            GUILayout.Label($"Clip: {clip.name}");
            GUILayout.Label($"Sample Rate: {clip.sampleRate} Hz");
            GUILayout.Label($"Channels: {clip.channels}");
            GUILayout.Label($"Length: {clip.length:F2}s");
            GUILayout.Label($"Playing: {source.isPlaying}");
            GUILayout.Label($"Time: {source.playbackTime:F2}s");

            GUILayout.BeginHorizontal();
            if (GUILayout.Button("Play")) source.Play();
            if (GUILayout.Button("Pause")) source.Pause();
            if (GUILayout.Button("Stop")) source.Stop();
            GUILayout.EndHorizontal();

            source.volume = GUILayout.HorizontalSlider(source.volume, 0f, 1f);
            GUILayout.Label($"Volume: {source.volume:F2}");

            GUILayout.EndArea();
        }
    }
}
