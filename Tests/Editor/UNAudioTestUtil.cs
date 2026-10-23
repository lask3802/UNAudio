using System;
using System.Collections;
using System.IO;
using NUnit.Framework;
using UnityEditor;
using UnityEngine;

namespace UNAudio.Tests
{
    static class UNAudioTestUtil
    {
        public const string FixtureDir = "Packages/com.lask3802.unaudio/Tests/Editor/Fixtures";
        /// <summary>3 s, 48 kHz stereo Ogg Vorbis: 440 Hz left, 660 Hz right, -21 dBFS.</summary>
        public const string ToneFixture = FixtureDir + "/tone_48k_stereo_3s.bytes";
        public const int ToneFrames = 144000;

        public static TextAsset ToneAsset()
        {
            var t = AssetDatabase.LoadAssetAtPath<TextAsset>(ToneFixture);
            Assert.IsNotNull(t, "fixture TextAsset missing");
            return t;
        }

        public static byte[] ToneBytes() => File.ReadAllBytes(Path.GetFullPath(ToneFixture));

        public static void StartManual(int sampleRate = 48000)
        {
            UNAudioEngine.Shutdown();
            var r = UNAudioEngine.Initialize(new UNAudioConfig
            {
                deviceType = UNAudioDeviceType.Manual,
                sampleRate = sampleRate,
                bufferFrames = 256,
            });
            Assert.AreEqual(UNAudioResult.Ok, r);
        }

        public static void StartNull()
        {
            UNAudioEngine.Shutdown();
            var r = UNAudioEngine.Initialize(new UNAudioConfig { deviceType = UNAudioDeviceType.Null, bufferFrames = 256 });
            Assert.AreEqual(UNAudioResult.Ok, r);
        }

        /// <summary>Pumps the engine until <paramref name="done"/> or the timeout (edit-mode frames are sparse).</summary>
        public static IEnumerator Wait(Func<bool> done, double timeoutSeconds = 10)
        {
            double end = EditorApplication.timeSinceStartup + timeoutSeconds;
            while (!done())
            {
                if (EditorApplication.timeSinceStartup > end) Assert.Fail("Timed out");
                UNAudioEngine.Update();
                yield return null;
            }
            UNAudioEngine.Update();
        }

        public static float[] Render(int frames)
        {
            var buf = new float[frames * 2];
            for (int done = 0; done < frames; done += 256)
            {
                int n = Math.Min(256, frames - done);
                var block = new float[n * 2];
                UNAudioEngine.Render(block, n);
                Array.Copy(block, 0, buf, done * 2, n * 2);
            }
            return buf;
        }

        public static int ZeroCrossings(float[] interleaved, int channel, int from, int to)
        {
            int n = 0;
            for (int i = from + 1; i < to; i++)
            {
                float a = interleaved[(i - 1) * 2 + channel], b = interleaved[i * 2 + channel];
                if ((a < 0f) != (b < 0f)) n++;
            }
            return n;
        }

        public static double Rms(float[] interleaved, int channel, int from, int to)
        {
            double s = 0;
            for (int i = from; i < to; i++) s += interleaved[i * 2 + channel] * (double)interleaved[i * 2 + channel];
            return Math.Sqrt(s / Math.Max(1, to - from));
        }
    }
}
