using UnityEngine;

namespace UNAudio
{
    /// <summary>
    /// Component that plays back a <see cref="UNAudioClip"/> via the native engine.
    /// Attach to any GameObject – analogous to Unity's AudioSource.
    /// </summary>
    public class UNAudioSource : MonoBehaviour
    {
        [Tooltip("The audio clip to play.")]
        public UNAudioClip clip;

        [Range(0f, 1f)]
        [Tooltip("Playback volume (0 = silent, 1 = full).")]
        public float volume = 1.0f;

        [Tooltip("Loop playback.")]
        public bool loop;

        [Range(0f, 1f)]
        [Tooltip("Spatial blend (0 = 2D, 1 = full 3D).")]
        public float spatialBlend;

        [Tooltip("Minimum distance for 3D attenuation.")]
        public float minDistance = 1f;

        [Tooltip("Maximum distance for 3D attenuation.")]
        public float maxDistance = 50f;

        [Range(0f, 5f)]
        [Tooltip("Doppler effect level.")]
        public float dopplerLevel = 1f;

        /// <summary>Whether the source is currently playing.</summary>
        public bool isPlaying => clip != null && clip.NativeHandle >= 0
                                 && UNAudioBridge.GetState(clip.NativeHandle) == 1;

        // ── Playback controls ────────────────────────────────────

        /// <summary>Start or resume playback.</summary>
        public void Play()
        {
            if (clip == null) return;
            EnsureLoaded();
            UNAudioBridge.SetVolume(clip.NativeHandle, volume);
            UNAudioBridge.SetLoop(clip.NativeHandle, loop);
            UNAudioBridge.Play(clip.NativeHandle);
        }

        /// <summary>Pause playback.</summary>
        public void Pause()
        {
            if (clip == null || clip.NativeHandle < 0) return;
            UNAudioBridge.Pause(clip.NativeHandle);
        }

        /// <summary>Stop playback and reset position.</summary>
        public void Stop()
        {
            if (clip == null || clip.NativeHandle < 0) return;
            UNAudioBridge.Stop(clip.NativeHandle);
        }

        // ── Convenience statics ──────────────────────────────────

        /// <summary>Play a clip once without needing a persistent component.</summary>
        public static void PlayOneShot(UNAudioClip clip)
        {
            if (clip == null) return;
            clip.LoadAudioData();
            UNAudioBridge.Play(clip.NativeHandle);
        }

        /// <summary>Play a clip at a world position (3D).</summary>
        public static void PlayClipAtPoint(UNAudioClip clip, Vector3 position)
        {
            if (clip == null) return;

            var go = new GameObject("UNAudio_OneShot");
            go.transform.position = position;

            var source = go.AddComponent<UNAudioSource>();
            source.clip = clip;
            source.spatialBlend = 1f;
            source.Play();

            Destroy(go, clip.length + 0.1f);
        }

        // ── Internal ─────────────────────────────────────────────

        private void EnsureLoaded()
        {
            if (clip != null && !clip.IsLoaded)
                clip.LoadAudioData();
        }

        private void Update()
        {
            if (clip == null || clip.NativeHandle < 0) return;

            // Sync volume to native side
            UNAudioBridge.SetVolume(clip.NativeHandle, volume);
            UNAudioBridge.SetLoop(clip.NativeHandle, loop);
        }

        private void OnDestroy()
        {
            Stop();
        }
    }
}
