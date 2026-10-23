using UnityEngine;

namespace UNAudio
{
    /// <summary>
    /// Hidden object that forwards <c>OnApplicationPause</c> to the engine: Unity has no static
    /// pause event, and focus changes alone would also stop audio on desktop alt-tab.
    /// </summary>
    [AddComponentMenu("")]
    sealed class UNAudioPauseHandler : MonoBehaviour
    {
        static UNAudioPauseHandler s_Instance;
        bool m_SuspendedByPause;
        float m_RetryAt;

        internal static void Ensure()
        {
            if (s_Instance != null) return;
            // Not DontSave: the object must be destroyed with the play session, not leak into edit mode.
            var go = new GameObject("UNAudio") { hideFlags = HideFlags.HideInHierarchy | HideFlags.HideInInspector | HideFlags.NotEditable };
            DontDestroyOnLoad(go);
            s_Instance = go.AddComponent<UNAudioPauseHandler>();
        }

        void OnApplicationPause(bool paused)
        {
            if (paused)
            {
                if (!UNAudioEngine.SuspendWhenPaused || UNAudioEngine.IsSuspended) return;
                UNAudioEngine.SetSuspended(true);
                m_SuspendedByPause = UNAudioEngine.IsSuspended;
            }
            else if (m_SuspendedByPause)
            {
                // Only undo our own suspension; one requested by the game stays.
                TryResume();
            }
        }

        void Update()
        {
            // The device may refuse to restart right after resume (route change, busy); retry.
            if (m_SuspendedByPause && Time.unscaledTime >= m_RetryAt) TryResume();
        }

        void TryResume()
        {
            if (!UNAudioEngine.IsInitialized)
            {
                m_SuspendedByPause = false;
                return;
            }
            UNAudioEngine.SetSuspended(false);
            m_SuspendedByPause = UNAudioEngine.IsSuspended;
            m_RetryAt = Time.unscaledTime + 1f;
        }

        void OnDestroy()
        {
            if (s_Instance == this) s_Instance = null;
        }
    }
}
