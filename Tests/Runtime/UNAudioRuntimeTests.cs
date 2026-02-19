using NUnit.Framework;
using UnityEngine;

namespace UNAudio.Tests.Runtime
{
    /// <summary>
    /// Runtime unit tests for UNAudio.
    /// These tests run inside the Unity Test Runner.
    /// </summary>
    public class UNAudioRuntimeTests
    {
        [Test]
        public void AudioUtility_LinearToDecibel_ZeroReturnsSilence()
        {
            float dB = AudioUtility.LinearToDecibel(0f);
            Assert.AreEqual(-144f, dB);
        }

        [Test]
        public void AudioUtility_LinearToDecibel_OneReturnsZero()
        {
            float dB = AudioUtility.LinearToDecibel(1f);
            Assert.AreEqual(0f, dB, 0.001f);
        }

        [Test]
        public void AudioUtility_DecibelToLinear_ZeroReturnsOne()
        {
            float linear = AudioUtility.DecibelToLinear(0f);
            Assert.AreEqual(1f, linear, 0.001f);
        }

        [Test]
        public void AudioUtility_BufferLatencyMs_CalculatesCorrectly()
        {
            float latency = AudioUtility.BufferLatencyMs(256, 48000);
            // 256 / 48000 * 1000 ≈ 5.333 ms
            Assert.AreEqual(5.333f, latency, 0.01f);
        }

        [Test]
        public void AudioUtility_DurationToSamples_CalculatesCorrectly()
        {
            int samples = AudioUtility.DurationToSamples(1f, 44100, 2);
            Assert.AreEqual(88200, samples);
        }

        [Test]
        public void UNAudioClip_DefaultState_IsNotLoaded()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            Assert.IsFalse(clip.IsLoaded);
            Assert.AreEqual(0, clip.sampleRate);
            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_SetMetadata_StoresValues()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            clip.SetMetadata(48000, 2, 16, 3.5f, new byte[] { 0, 1, 2 });

            Assert.AreEqual(48000, clip.sampleRate);
            Assert.AreEqual(2, clip.channels);
            Assert.AreEqual(16, clip.bitsPerSample);
            Assert.AreEqual(3.5f, clip.length, 0.001f);
            Assert.AreEqual(3, clip.GetMemorySize());

            Object.DestroyImmediate(clip);
        }
    }
}
