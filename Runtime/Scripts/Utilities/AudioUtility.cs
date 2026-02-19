using UnityEngine;

namespace UNAudio
{
    /// <summary>
    /// General-purpose audio utility helpers.
    /// </summary>
    public static class AudioUtility
    {
        /// <summary>Convert a linear amplitude (0..1+) to decibels.</summary>
        public static float LinearToDecibel(float linear)
        {
            if (linear <= 0f) return -144f; // silence floor
            return 20f * Mathf.Log10(linear);
        }

        /// <summary>Convert a decibel value to linear amplitude.</summary>
        public static float DecibelToLinear(float dB)
        {
            return Mathf.Pow(10f, dB / 20f);
        }

        /// <summary>Clamp a volume value to a safe range.</summary>
        public static float ClampVolume(float volume) => Mathf.Clamp01(volume);

        /// <summary>Calculate buffer latency in milliseconds.</summary>
        public static float BufferLatencyMs(int bufferFrames, int sampleRate)
        {
            if (sampleRate <= 0) return 0f;
            return (float)bufferFrames / sampleRate * 1000f;
        }

        /// <summary>Calculate the number of samples for a given duration.</summary>
        public static int DurationToSamples(float seconds, int sampleRate, int channels)
        {
            return Mathf.RoundToInt(seconds * sampleRate * channels);
        }
    }
}
