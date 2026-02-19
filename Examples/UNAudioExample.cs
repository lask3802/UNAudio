using UnityEngine;
using UNAudio;

/// <summary>
/// Example usage of UNAudio engine
/// Generates a simple sine wave tone
/// </summary>
public class UNAudioExample : MonoBehaviour
{
    [Header("Audio Settings")]
    [SerializeField] private int sampleRate = 48000;
    [SerializeField] private int channels = 2;
    [SerializeField] private int bufferSize = 512;
    
    [Header("Tone Generator")]
    [SerializeField] private float frequency = 440f; // A4 note
    [SerializeField] [Range(0f, 1f)] private float volume = 0.5f;
    
    private double phase = 0;
    private UNAudioEngine.AudioCallbackDelegate audioCallback;

    void Start()
    {
        if (!UNAudioEngine.IsPlatformSupported())
        {
            Debug.LogWarning("UNAudio is not supported on this platform: " + Application.platform);
            return;
        }

        Debug.Log($"Initializing UNAudio on {UNAudioEngine.GetPlatformName()}");

        // Initialize audio engine
        var result = UNAudioEngine.Initialize(sampleRate, channels, bufferSize);
        
        if (result != UNAudioEngine.Result.Success)
        {
            Debug.LogError($"Failed to initialize UNAudio: {result}");
            return;
        }

        // Get device info
        UNAudioEngine.DeviceInfo info;
        result = UNAudioEngine.GetDeviceInfo(out info);
        
        if (result == UNAudioEngine.Result.Success)
        {
            Debug.Log($"Device Info: {info.sampleRate}Hz, {info.channels}ch, {info.bufferSize} frames, Format: {info.format}");
            Debug.Log($"Estimated Latency: {UNAudioEngine.GetLatency()}ms");
        }

        // Set volume
        UNAudioEngine.SetVolume(volume);

        // Set callback (keep reference to prevent garbage collection)
        audioCallback = OnAudioCallback;
        UNAudioEngine.SetCallback(audioCallback);

        // Start playback
        result = UNAudioEngine.Start();
        
        if (result == UNAudioEngine.Result.Success)
        {
            Debug.Log("UNAudio playback started");
        }
        else
        {
            Debug.LogError($"Failed to start UNAudio: {result}");
        }
    }

    void OnDestroy()
    {
        if (UNAudioEngine.IsPlatformSupported())
        {
            Debug.Log("Shutting down UNAudio");
            UNAudioEngine.Stop();
            UNAudioEngine.Shutdown();
        }
    }

    void Update()
    {
        // Update volume in real-time
        if (UNAudioEngine.IsPlatformSupported())
        {
            UNAudioEngine.SetVolume(volume);
        }
    }

    // Audio callback - generates sine wave
    private void OnAudioCallback(System.IntPtr buffer, int frames, int channels, System.IntPtr userData)
    {
        unsafe
        {
            float* samples = (float*)buffer.ToPointer();
            double phaseIncrement = 2.0 * Mathf.PI * frequency / sampleRate;

            for (int i = 0; i < frames; i++)
            {
                float sample = Mathf.Sin((float)phase);
                
                // Write to all channels
                for (int ch = 0; ch < channels; ch++)
                {
                    samples[i * channels + ch] = sample;
                }

                phase += phaseIncrement;
                
                // Keep phase in range
                if (phase >= 2.0 * Mathf.PI)
                {
                    phase -= 2.0 * Mathf.PI;
                }
            }
        }
    }
}
