using System;
using System.Collections;
using System.Collections.Generic;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.TestTools;
using static UNAudio.Tests.UNAudioTestUtil;

namespace UNAudio.Tests
{
    public class UNAudioEngineTests
    {
        [TearDown]
        public void TearDown() => UNAudioEngine.Shutdown();

        [Test]
        public void ManualDeviceReportsConfiguration()
        {
            StartManual(44100);
            var d = UNAudioEngine.Device;
            Assert.AreEqual(UNAudioBackend.Manual, d.Backend);
            Assert.AreEqual(44100, d.SampleRate);
            Assert.AreEqual(2, d.Channels);
            Assert.AreEqual(UNAudioResult.AlreadyInitialized, UNAudioEngine.Initialize(default));
        }

        [UnityTest]
        public IEnumerator ShutdownInvalidatesClipsAndVoices()
        {
            StartManual();
            var clip = UNAudioClip.Load(ToneAsset());
            yield return Wait(() => clip.IsReady);
            var oldVoice = clip.Play();
            Render(256);
            int session = UNAudioEngine.Session;
            UNAudioEngine.Shutdown();
            Assert.AreEqual(UNAudioClipState.Invalid, clip.State);
            Assert.AreEqual(UNAudioVoiceState.Invalid, oldVoice.State);
            Assert.Throws<UNAudioException>(() => clip.Play());

            // The first voice of the next session occupies the same native slot.
            StartManual();
            Assert.AreNotEqual(session, UNAudioEngine.Session);
            var clip2 = UNAudioClip.Load(ToneAsset());
            yield return Wait(() => clip2.IsReady);
            var newVoice = clip2.Play();
            Render(256);
            Assert.AreNotEqual(oldVoice, newVoice);
            Assert.AreEqual(UNAudioVoiceState.Invalid, oldVoice.State);
            oldVoice.Stop();
            Render(256);
            Assert.AreEqual(UNAudioVoiceState.Playing, newVoice.State, "a stale voice must not stop the new one");
            clip2.Dispose();
        }

        [UnityTest]
        public IEnumerator TextAssetLoadMakesNoManagedCopy()
        {
            StartManual();
            var asset = ToneAsset();
            Assert.Greater(asset.dataSize, 10000);
            UNAudioClip.Load(asset).Dispose();  // warm up JIT and the clip registry

            long before = GC.GetAllocatedBytesForCurrentThread();
            var clip = UNAudioClip.Load(asset);
            long allocated = GC.GetAllocatedBytesForCurrentThread() - before;
            Assert.Less(allocated, 2048, "managed bytes allocated by Load(TextAsset)");

            yield return Wait(() => clip.IsLoaded);
            var info = clip.Info;
            Assert.AreEqual(2, info.Channels);
            Assert.AreEqual(48000, info.SampleRate);
            Assert.AreEqual(ToneFrames, info.Frames);
            Assert.AreEqual(UNAudioSampleFormat.Int16, info.SampleFormat);
            Assert.AreEqual(ToneFrames * 2 * 2, info.MemoryBytes, ToneFrames * 0.01);
            clip.Dispose();
        }

        [UnityTest]
        public IEnumerator DecodedPlaybackIsSampleExactAndCorrect()
        {
            StartManual();
            var clip = UNAudioClip.Load(ToneAsset(), new UNAudioLoadOptions { sampleFormat = UNAudioSampleFormat.Float32 });
            yield return Wait(() => clip.IsLoaded);

            var started = new List<long>();
            Action<UNAudioVoice, long> onStart = (v, f) => started.Add(f);
            UNAudioEngine.VoiceStarted += onStart;
            try
            {
                var voice = clip.PlayScheduled(1000);
                var timing = voice.GetTiming();
                Assert.AreEqual(UNAudioVoiceState.Pending, timing.State);
                Assert.AreEqual(-1000.0, timing.AudibleFrame, 1e-3, "predicted negative time before start");

                float[] out0 = Render(1000 + 48000);
                for (int i = 0; i < 1000 * 2; i++) Assert.AreEqual(0f, out0[i], "silence before the scheduled frame");
                Assert.AreNotEqual(0f, out0[1001 * 2]);
                // 1 s window: 440 Hz left → 880 zero crossings, 660 Hz right → 1320.
                Assert.AreEqual(880, ZeroCrossings(out0, 0, 1000, 49000), 4);
                Assert.AreEqual(1320, ZeroCrossings(out0, 1, 1000, 49000), 4);

                UNAudioEngine.Update();
                CollectionAssert.AreEqual(new[] { 1000L }, started);

                timing = voice.GetTiming();
                Assert.AreEqual(UNAudioVoiceState.Playing, timing.State);
                Assert.AreEqual(48000.0, timing.AudibleFrame, 1e-3);
                Assert.AreEqual(1.0, timing.AudibleSeconds, 1e-7);
                Assert.AreEqual(1000, timing.StartDspFrame);
            }
            finally
            {
                UNAudioEngine.VoiceStarted -= onStart;
            }
        }

        [UnityTest]
        public IEnumerator PauseResumeStopLandOnExactFrames()
        {
            StartManual();
            var clip = UNAudioClip.Load(ToneAsset(), new UNAudioLoadOptions { sampleFormat = UNAudioSampleFormat.Float32 });
            yield return Wait(() => clip.IsLoaded);

            var ended = new List<long>();
            Action<UNAudioVoice, long> onEnd = (v, f) => ended.Add(f);
            UNAudioEngine.VoiceEnded += onEnd;
            try
            {
                var voice = clip.PlayScheduled(256);
                voice.Pause(1256);
                voice.Resume(2256);
                voice.Stop(0f, 4000);
                float[] o = Render(5000);
                Assert.AreNotEqual(0f, o[1255 * 2]);
                for (int f = 1256; f < 2256; f++) Assert.AreEqual(0f, o[f * 2], $"paused frame {f}");
                Assert.AreNotEqual(0f, o[2300 * 2]);
                for (int f = 4000; f < 5000; f++) Assert.AreEqual(0f, o[f * 2], $"stopped frame {f}");
                UNAudioEngine.Update();
                CollectionAssert.AreEqual(new[] { 4000L }, ended);
                // Update() reclaimed the finished voice, so its handle is now stale.
                Assert.IsFalse(voice.IsAlive);
                Assert.AreEqual(UNAudioVoiceState.Invalid, voice.State);
            }
            finally
            {
                UNAudioEngine.VoiceEnded -= onEnd;
            }
        }

        [UnityTest]
        public IEnumerator BorrowedTextAssetIsReleasedAfterDecode()
        {
            StartManual();
            var clip = UNAudioClip.Load(ToneAsset(), new UNAudioLoadOptions { borrow = true });
            Assert.IsFalse(clip.IsSourceReleased);
            yield return Wait(() => clip.IsLoaded);
            yield return Wait(() => clip.IsSourceReleased);
            clip.Dispose();
        }

        [UnityTest]
        public IEnumerator StreamingClipStartsOnTimeWithCorrectContent()
        {
            StartManual();
            var clip = UNAudioClip.Load(ToneAsset(), UNAudioLoadOptions.Streaming);
            yield return Wait(() => clip.IsLoaded);
            var voice = clip.PlayScheduled(2048);
            // Let the stream thread prime its ring before rendering starts.
            double until = UnityEditor.EditorApplication.timeSinceStartup + 0.1;
            while (UnityEditor.EditorApplication.timeSinceStartup < until) yield return null;

            const int frames = 2048 + 48128;  // whole 256-frame blocks
            float[] o = new float[frames * 2];
            var block = new float[256 * 2];
            for (int f = 0; f < frames; f += 256)
            {
                UNAudioEngine.Render(block, 256);
                Array.Copy(block, 0, o, f * 2, 512);
                System.Threading.Thread.Sleep(1);  // real-time-ish pacing for the stream thread
            }
            for (int i = 0; i < 2048 * 2; i++) Assert.AreEqual(0f, o[i]);
            Assert.AreNotEqual(0f, o[2049 * 2]);
            Assert.AreEqual(880, ZeroCrossings(o, 0, 2048, 2048 + 48000), 4);
            Assert.AreEqual(1320, ZeroCrossings(o, 1, 2048, 2048 + 48000), 4);
            Assert.AreEqual(0, UNAudioEngine.GetStats().StarvedFrames);
            Assert.AreEqual(48128.0, voice.GetTiming().AudibleFrame, 1e-3);
        }

        [UnityTest]
        public IEnumerator InvalidDataFailsAsynchronously()
        {
            StartManual();
            var garbage = new byte[4096];
            for (int i = 0; i < garbage.Length; i++) garbage[i] = (byte)(i * 31);
            UNAudioClip clip;
            try
            {
                clip = UNAudioClip.Load(garbage);
            }
            catch (UNAudioException e)
            {
                // Rejected synchronously by the header sniff: also acceptable.
                Assert.AreEqual(UNAudioResult.UnsupportedFormat, e.Result);
                yield break;
            }
            var task = clip.WhenReady;
            yield return Wait(() => task.IsCompleted);
            Assert.IsTrue(task.IsFaulted);
            Assert.IsTrue(clip.IsFailed);
        }

        [UnityTest]
        public IEnumerator ClockAdvancesSmoothlyWithRealTime()
        {
            StartNull();
            double t0 = UnityEditor.EditorApplication.timeSinceStartup;
            while (UnityEditor.EditorApplication.timeSinceStartup - t0 < 0.3) yield return null;

            double firstAudible = UNAudioClock.AudibleTime;
            long firstHost = UNAudioClock.HostTimeNs;
            double prev = firstAudible;
            while (UnityEditor.EditorApplication.timeSinceStartup - t0 < 1.3)
            {
                double a = UNAudioClock.AudibleTime;
                Assert.GreaterOrEqual(a, prev, "audible time went backwards");
                prev = a;
                yield return null;
            }
            double dAudio = UNAudioClock.AudibleTime - firstAudible;
            double dHost = (UNAudioClock.HostTimeNs - firstHost) * 1e-9;
            Assert.AreEqual(dHost, dAudio, 0.005, "audio clock vs host clock over ~1 s");
            Assert.Greater(UNAudioClock.OutputLatency, 0.0);

            long now = UNAudioClock.HostTimeNs;
            double rt = UNAudioClock.HostNsToRealtime(now);
            Assert.AreEqual(Time.realtimeSinceStartupAsDouble, rt, 0.05);
            Assert.AreEqual(now, UNAudioClock.RealtimeToHostNs(rt), 1000);
        }
    }
}
