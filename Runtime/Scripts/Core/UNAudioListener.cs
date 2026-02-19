using UnityEngine;

namespace UNAudio
{
    /// <summary>
    /// Marks the GameObject as the audio listener position for 3D spatialization.
    /// Typically attached to the main camera – analogous to Unity's AudioListener.
    /// </summary>
    [DisallowMultipleComponent]
    public class UNAudioListener : MonoBehaviour
    {
        private static UNAudioListener current;

        /// <summary>The currently active listener (singleton).</summary>
        public static UNAudioListener Current => current;

        /// <summary>World-space position of the listener.</summary>
        public Vector3 Position => transform.position;

        /// <summary>Forward direction of the listener.</summary>
        public Vector3 Forward => transform.forward;

        /// <summary>Up direction of the listener.</summary>
        public Vector3 Up => transform.up;

        private void OnEnable()
        {
            if (current != null && current != this)
            {
                Debug.LogWarning("[UNAudio] Multiple UNAudioListeners detected. " +
                                 "Only the most recently enabled one will be used.");
            }
            current = this;
        }

        private void OnDisable()
        {
            if (current == this)
                current = null;
        }
    }
}
