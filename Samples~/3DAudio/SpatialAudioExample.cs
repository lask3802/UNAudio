using UnityEngine;

namespace UNAudio.Samples
{
    /// <summary>
    /// Demonstrates 3D spatial audio with UNAudioSource + UNAudioListener.
    /// Attach to a GameObject with UNAudioSource. Put UNAudioListener on the camera.
    /// The audio source orbits around the listener to demonstrate spatialization.
    /// </summary>
    public class SpatialAudioExample : MonoBehaviour
    {
        [Header("Audio")]
        [Tooltip("The audio clip to play in 3D space.")]
        public UNAudioClip clip;

        [Header("Orbit Settings")]
        [Tooltip("Radius of the orbit path.")]
        public float orbitRadius = 5f;

        [Tooltip("Orbit speed in degrees per second.")]
        public float orbitSpeed = 30f;

        [Tooltip("Enable automatic orbiting.")]
        public bool autoOrbit = true;

        private UNAudioSource source;
        private float angle;

        private void Start()
        {
            source = GetComponent<UNAudioSource>();
            if (source == null)
                source = gameObject.AddComponent<UNAudioSource>();

            source.clip = clip;
            source.spatialBlend = 1.0f;
            source.minDistance = 1f;
            source.maxDistance = 50f;
            source.loop = true;

            if (clip != null)
                source.Play();
        }

        private void Update()
        {
            if (!autoOrbit) return;

            angle += orbitSpeed * Time.deltaTime;
            var rad = angle * Mathf.Deg2Rad;
            transform.position = new Vector3(
                Mathf.Cos(rad) * orbitRadius,
                0f,
                Mathf.Sin(rad) * orbitRadius
            );
        }

        private void OnGUI()
        {
            GUILayout.BeginArea(new Rect(10, 10, 300, 220));
            GUILayout.Label("UNAudio - 3D Audio", GUI.skin.box);

            if (source == null || clip == null)
            {
                GUILayout.Label("Assign a UNAudioClip in the Inspector.");
                GUILayout.EndArea();
                return;
            }

            var listener = UNAudioListener.Current;
            GUILayout.Label($"Listener: {(listener != null ? listener.Position.ToString("F1") : "None")}");
            GUILayout.Label($"Source: {transform.position:F1}");

            if (listener != null)
            {
                float dist = Vector3.Distance(listener.Position, transform.position);
                GUILayout.Label($"Distance: {dist:F2}m");
            }

            GUILayout.Label($"Playing: {source.isPlaying}");

            GUILayout.BeginHorizontal();
            if (GUILayout.Button("Play")) source.Play();
            if (GUILayout.Button("Stop")) source.Stop();
            GUILayout.EndHorizontal();

            autoOrbit = GUILayout.Toggle(autoOrbit, "Auto Orbit");

            GUILayout.Label($"Orbit Radius: {orbitRadius:F1}");
            orbitRadius = GUILayout.HorizontalSlider(orbitRadius, 1f, 20f);

            GUILayout.Label($"Orbit Speed: {orbitSpeed:F0} deg/s");
            orbitSpeed = GUILayout.HorizontalSlider(orbitSpeed, 0f, 180f);

            GUILayout.EndArea();
        }

        private void OnDrawGizmos()
        {
            Gizmos.color = Color.cyan;
            // Draw orbit circle
            const int segments = 64;
            for (int i = 0; i < segments; i++)
            {
                float a1 = (i / (float)segments) * Mathf.PI * 2;
                float a2 = ((i + 1) / (float)segments) * Mathf.PI * 2;
                var p1 = new Vector3(Mathf.Cos(a1) * orbitRadius, 0, Mathf.Sin(a1) * orbitRadius);
                var p2 = new Vector3(Mathf.Cos(a2) * orbitRadius, 0, Mathf.Sin(a2) * orbitRadius);
                Gizmos.DrawLine(p1, p2);
            }

            // Draw line from listener to source
            Gizmos.color = Color.yellow;
            if (UNAudioListener.Current != null)
                Gizmos.DrawLine(UNAudioListener.Current.Position, transform.position);
        }
    }
}
