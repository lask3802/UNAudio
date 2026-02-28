using UnityEngine;
using UnityEngine.Events;

namespace UNAudio
{
    /// <summary>
    /// Fires a <see cref="UnityEvent"/> whenever the left mouse button is pressed down.
    /// Attach to any GameObject and wire up the event in the Inspector.
    /// </summary>
    [AddComponentMenu("UNAudio/Click To Play")]
    public class UNAudioClickToPlay : MonoBehaviour
    {
        [Tooltip("Invoked on every left-mouse-button down frame.")]
        public UnityEvent onMouseDown;

        private void Update()
        {
            if (Input.GetMouseButtonDown(0))
            {
                onMouseDown?.Invoke();
            }
        }
    }
}
