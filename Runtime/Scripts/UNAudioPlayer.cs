using UnityEngine;

namespace UNAudio
{
    /// <summary>
    /// Minimal component for scene-authored sounds. For gameplay-critical timing use
    /// <see cref="UNAudioClip"/> / <see cref="UNAudioVoice"/> directly.
    /// </summary>
    [AddComponentMenu("Audio/UNAudio Player")]
    public sealed class UNAudioPlayer : MonoBehaviour
    {
        [SerializeField] UNAudioAsset m_Asset;
        [SerializeField] bool m_PlayOnEnable = true;
        [SerializeField] bool m_Loop;
        [SerializeField, Range(0f, 2f)] float m_Volume = 1f;
        [SerializeField, Range(-1f, 1f)] float m_Pan;
        [SerializeField, Range(0.25f, 4f)] float m_Pitch = 1f;
        [SerializeField, Range(0, 15)] int m_Bus;
        [SerializeField, Min(0f)] float m_FadeOutOnDisable = 0.05f;

        UNAudioVoice m_Voice;
        bool m_PendingPlay;

        public UNAudioAsset Asset { get => m_Asset; set => m_Asset = value; }
        public UNAudioVoice Voice => m_Voice;
        public bool IsPlaying => m_Voice.IsAlive;

        void OnEnable()
        {
            if (m_Asset == null) return;
            m_Asset.LoadAsync();  // start decoding early
            if (m_PlayOnEnable) Play();
        }

        void OnDisable()
        {
            m_PendingPlay = false;
            Stop(m_FadeOutOnDisable);
        }

        void Update()
        {
            if (!m_PendingPlay || m_Asset == null) return;
            var clip = m_Asset.Clip;
            if (clip.IsReady)
            {
                m_PendingPlay = false;
                StartVoice();
            }
            else if (clip.IsFailed)
            {
                m_PendingPlay = false;
                Debug.LogError($"[UNAudio] '{m_Asset.name}' failed to load: {clip.Info.Error}", this);
            }
        }

        /// <summary>Plays now, or as soon as the clip finishes its first decode slice.</summary>
        public void Play()
        {
            if (m_Asset == null) return;
            Stop();
            if (m_Asset.Clip.IsReady) StartVoice();
            else m_PendingPlay = true;
        }

        public void Stop(float fadeSeconds = 0f)
        {
            if (m_Voice.IsAlive) m_Voice.Stop(fadeSeconds);
            m_Voice = default;
        }

        void StartVoice()
        {
            var p = UNAudioPlayParams.Default;
            p.volume = m_Volume;
            p.pan = m_Pan;
            p.pitch = m_Pitch;
            p.loop = m_Loop;
            p.bus = m_Bus;
            m_Voice = m_Asset.Play(p);
        }
    }
}
