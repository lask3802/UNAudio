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

        /// <summary>The clip failed to load or the voice failed to decode (streaming).</summary>
        public event System.Action<UNAudioPlayer, UNAudioResult> Error;

        /// <summary>Last load or decode error since <see cref="Play"/>, or <see cref="UNAudioResult.Ok"/>.</summary>
        public UNAudioResult LastError { get; private set; }

        void OnEnable()
        {
            UNAudioEngine.VoiceError += OnVoiceError;
            if (m_Asset == null) return;
            m_Asset.LoadAsync();  // start decoding early
            if (m_PlayOnEnable) Play();
        }

        void OnDisable()
        {
            UNAudioEngine.VoiceError -= OnVoiceError;
            m_PendingPlay = false;
            Stop(m_FadeOutOnDisable);
        }

        void OnVoiceError(UNAudioVoice voice, UNAudioResult error)
        {
            if (voice != m_Voice) return;
            ReportError(error, "failed while playing");
        }

        void ReportError(UNAudioResult error, string what)
        {
            LastError = error;
            Debug.LogError($"[UNAudio] '{(m_Asset != null ? m_Asset.name : name)}' {what}: {error}", this);
            Error?.Invoke(this, error);
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
                ReportError(clip.Info.Error, "failed to load");
            }
        }

        /// <summary>Plays now, or as soon as the clip finishes its first decode slice.</summary>
        public void Play()
        {
            if (m_Asset == null) return;
            Stop();
            LastError = UNAudioResult.Ok;
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
