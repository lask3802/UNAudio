using System;
using System.Collections;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using NUnit.Framework;
using UnityEditor;
using UnityEngine;
using UnityEngine.LowLevel;
using UnityEngine.PlayerLoop;
using UnityEngine.TestTools;
using static UNAudio.Tests.UNAudioTestUtil;

namespace UNAudio.Tests
{
    /// <summary>Engine lifecycle: ticking, ownership of borrowed memory, suspend, threading.</summary>
    public class UNAudioLifecycleTests
    {
        [TearDown]
        public void TearDown() => UNAudioEngine.Shutdown();

        [Test]
        public void InitialiseInstallsEarlyUpdateHook()
        {
            StartManual();
            var root = PlayerLoop.GetCurrentPlayerLoop();
            bool found = false;
            foreach (var sys in root.subSystemList)
            {
                if (sys.type != typeof(EarlyUpdate)) continue;
                foreach (var sub in sys.subSystemList)
                    found |= sub.type != null && sub.type.Name == "UNAudioUpdate" && sub.updateDelegate != null;
            }
            Assert.IsTrue(found, "UNAudioUpdate missing from EarlyUpdate: a player build would never dispatch events");
        }

        [UnityTest]
        public IEnumerator EditorTickDispatchesEventsWithoutManualUpdate()
        {
            StartManual();
            var clip = UNAudioClip.Load(ToneAsset());
            bool loaded = false;
            clip.Loaded += _ => loaded = true;
            double end = EditorApplication.timeSinceStartup + 10;
            while (!loaded && EditorApplication.timeSinceStartup < end) yield return null;  // no UNAudioEngine.Update()
            Assert.IsTrue(loaded, "Loaded event was not dispatched by the engine's own tick");
            clip.Dispose();
        }

        [UnityTest]
        public IEnumerator BorrowedByteArrayStaysPinnedUntilDecodeFinishes()
        {
            StartManual();
            var bytes = ToneBytes();
            var clip = UNAudioClip.Load(bytes, new UNAudioLoadOptions { borrow = true, sampleFormat = UNAudioSampleFormat.Float32 });
            var pin = typeof(UNAudioClip).GetField("m_Pin", BindingFlags.NonPublic | BindingFlags.Instance);
            Assert.IsNotNull(pin);
            Assert.IsTrue(((GCHandle)pin.GetValue(clip)).IsAllocated, "borrowed array must be pinned while the engine reads it");
            Assert.IsFalse(clip.IsSourceReleased);

            yield return Wait(() => clip.IsSourceReleased);
            Assert.IsTrue(clip.IsLoaded);
            Assert.IsFalse(((GCHandle)pin.GetValue(clip)).IsAllocated, "pin must be released once decoding is done");

            // Decoded from the borrowed bytes: the content is intact.
            clip.PlayScheduled(256);
            float[] o = Render(256 + 48000);
            Assert.AreEqual(880, ZeroCrossings(o, 0, 256, 256 + 48000), 4);
            clip.Dispose();
        }

        [UnityTest]
        public IEnumerator BorrowedTextAssetIsReferencedUntilReleased()
        {
            StartManual();
            var asset = ToneAsset();
            var clip = UNAudioClip.Load(asset, new UNAudioLoadOptions { borrow = true });
            var source = typeof(UNAudioClip).GetField("m_Source", BindingFlags.NonPublic | BindingFlags.Instance);
            Assert.AreSame(asset, source.GetValue(clip));
            yield return Wait(() => clip.IsSourceReleased);
            Assert.IsNull(source.GetValue(clip));
            clip.Dispose();
        }

        [UnityTest]
        public IEnumerator DisposedClipReportsInvalidWhileVoiceStillPlays()
        {
            StartManual();
            var clip = UNAudioClip.Load(ToneAsset());
            yield return Wait(() => clip.IsLoaded);
            var voice = clip.Play(new UNAudioPlayParams { volume = 1f, pitch = 1f, loop = true });
            Render(256);
            clip.Dispose();
            Assert.AreEqual(UNAudioClipState.Invalid, clip.State);
            var wait = clip.WaitUntilLoaded();
            Assert.IsFalse(wait.MoveNext(), "coroutine must not spin on a disposed clip");
            Render(256);
            Assert.AreEqual(UNAudioVoiceState.Playing, voice.State);
            voice.Stop();
        }

        [UnityTest]
        public IEnumerator SuspendFreezesTheTimeline()
        {
            StartNull();
            double t0 = EditorApplication.timeSinceStartup;
            while (EditorApplication.timeSinceStartup - t0 < 0.1) yield return null;
            UNAudioEngine.SetSuspended(true);
            Assert.IsTrue(UNAudioEngine.IsSuspended);
            long frozen = UNAudioClock.MixFrame;
            t0 = EditorApplication.timeSinceStartup;
            while (EditorApplication.timeSinceStartup - t0 < 0.1) yield return null;
            Assert.AreEqual(frozen, UNAudioClock.MixFrame);
            UNAudioEngine.SetSuspended(false);
            Assert.IsFalse(UNAudioEngine.IsSuspended);
            t0 = EditorApplication.timeSinceStartup;
            while (EditorApplication.timeSinceStartup - t0 < 0.1) yield return null;
            Assert.Greater(UNAudioClock.MixFrame, frozen);
        }

        [Test]
        public void LoadingOffTheMainThreadIsRejected()
        {
            StartManual();
            var bytes = ToneBytes();
            var task = Task.Run(() => UNAudioClip.Load(bytes));
            var e = Assert.Throws<AggregateException>(() => task.Wait());
            Assert.IsInstanceOf<InvalidOperationException>(e.InnerException);
        }

        [UnityTest]
        public IEnumerator ShutdownCancelsPendingTasks()
        {
            StartManual();
            var clip = UNAudioClip.Load(ToneAsset(), UNAudioLoadOptions.Default);
            var loaded = clip.WhenLoaded;
            if (loaded.IsCompleted) Assert.Inconclusive("decode finished before shutdown");
            int mainThread = System.Threading.Thread.CurrentThread.ManagedThreadId;
            int ranOn = 0;
            _ = loaded.ContinueWith(_ => ranOn = System.Threading.Thread.CurrentThread.ManagedThreadId,
                                    TaskContinuationOptions.ExecuteSynchronously);
            UNAudioEngine.Shutdown();
            Assert.IsTrue(loaded.IsCanceled);
            double end = EditorApplication.timeSinceStartup + 5;
            while (ranOn == 0 && EditorApplication.timeSinceStartup < end) yield return null;
            Assert.AreNotEqual(0, ranOn);
            Assert.AreNotEqual(mainThread, ranOn, "continuations must not run inline inside Shutdown");
        }
    }
}
