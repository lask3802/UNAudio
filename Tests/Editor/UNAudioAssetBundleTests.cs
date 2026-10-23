using System.Collections;
using System.IO;
using NUnit.Framework;
using UNAudio.Editor;
using UnityEditor;
using UnityEngine;
using UnityEngine.TestTools;
using static UNAudio.Tests.UNAudioTestUtil;

namespace UNAudio.Tests
{
    /// <summary>
    /// End to end: .ogg → UNAudio importer → AssetBundle on disk → source asset deleted →
    /// bundle loaded → clip decoded natively from the bundle's TextAsset → sample-exact
    /// playback and timing.
    /// </summary>
    public class UNAudioAssetBundleTests
    {
        const string AssetDir = "Assets/__UNAudioBundleTest";
        const string AssetPath = AssetDir + "/tone.ogg";
        const string BundleDir = "Temp/UNAudioBundleTest";
        const string BundleName = "unaudio_test";

        AssetBundle m_Bundle;

        [OneTimeSetUp]
        public void BuildBundle()
        {
            if (AssetDatabase.IsValidFolder(AssetDir)) AssetDatabase.DeleteAsset(AssetDir);
            AssetDatabase.CreateFolder("Assets", "__UNAudioBundleTest");
            File.WriteAllBytes(AssetPath, ToneBytes());
            AssetDatabase.ImportAsset(AssetPath, ImportAssetOptions.ForceSynchronousImport);
            UNAudioImporterUtility.UseUNAudio(AssetPath);
            AssetDatabase.ImportAsset(AssetPath, ImportAssetOptions.ForceUpdate | ImportAssetOptions.ForceSynchronousImport);

            var asset = AssetDatabase.LoadAssetAtPath<UNAudioAsset>(AssetPath);
            Assert.IsNotNull(asset, "importer override did not produce a UNAudioAsset");
            Assert.AreEqual("vorbis", asset.Format);
            Assert.AreEqual(2, asset.Channels);
            Assert.AreEqual(48000, asset.SampleRate);
            Assert.AreEqual(ToneFrames, asset.Frames);
            Assert.AreEqual(3.0, asset.Length, 1e-6);
            Assert.AreEqual(ToneBytes().Length, asset.Data.dataSize, "bytes must be stored untouched");

            Directory.CreateDirectory(BundleDir);
            var builds = new[] { new AssetBundleBuild { assetBundleName = BundleName, assetNames = new[] { AssetPath } } };
            var manifest = BuildPipeline.BuildAssetBundles(BundleDir, builds,
                BuildAssetBundleOptions.ForceRebuildAssetBundle | BuildAssetBundleOptions.ChunkBasedCompression,
                EditorUserBuildSettings.activeBuildTarget);
            Assert.IsNotNull(manifest, "BuildAssetBundles failed");

            // The source must be gone so everything below really comes from the bundle file.
            AssetDatabase.DeleteAsset(AssetDir);
            m_Bundle = AssetBundle.LoadFromFile(Path.Combine(BundleDir, BundleName));
            Assert.IsNotNull(m_Bundle);
        }

        [OneTimeTearDown]
        public void Cleanup()
        {
            UNAudioEngine.Shutdown();
            if (m_Bundle != null) m_Bundle.Unload(true);
            if (AssetDatabase.IsValidFolder(AssetDir)) AssetDatabase.DeleteAsset(AssetDir);
            if (Directory.Exists(BundleDir)) Directory.Delete(BundleDir, true);
        }

        [TearDown]
        public void TearDown() => UNAudioEngine.Shutdown();

        [UnityTest]
        public IEnumerator DecodeFromBundlePlaysSampleExact()
        {
            StartManual();
            var task = UNAudioAssetBundles.LoadClipAsync(m_Bundle, "tone",
                new UNAudioLoadOptions { sampleFormat = UNAudioSampleFormat.Float32 });
            yield return Wait(() => task.IsCompleted);
            Assert.IsFalse(task.IsFaulted, task.Exception?.ToString());
            var clip = task.Result;
            Assert.IsTrue(clip.IsReady);
            yield return Wait(() => clip.IsLoaded);

            const int start = 4800;
            var voice = clip.PlayScheduled(start);
            float[] o = Render(start + 96000);
            for (int i = 0; i < start * 2; i++) Assert.AreEqual(0f, o[i]);
            Assert.AreNotEqual(0f, o[(start + 1) * 2]);
            Assert.AreEqual(880, ZeroCrossings(o, 0, start + 48000, start + 96000), 4);
            Assert.AreEqual(1320, ZeroCrossings(o, 1, start + 48000, start + 96000), 4);
            Assert.AreEqual(0.0891, Rms(o, 0, start + 48000, start + 96000), 0.002);

            var t = voice.GetTiming();
            Assert.AreEqual(96000.0, t.AudibleFrame, 1e-3);
            Assert.AreEqual(2.0, t.AudibleSeconds, 1e-7);
            clip.Dispose();
        }

        [UnityTest]
        public IEnumerator PreloadAsyncStartsEveryAssetInTheBundle()
        {
            StartManual();
            var task = UNAudioAssetBundles.PreloadAsync(m_Bundle);
            yield return Wait(() => task.IsCompleted);
            Assert.IsFalse(task.IsFaulted, task.Exception?.ToString());
            var assets = task.Result;
            Assert.AreEqual(1, assets.Length);
            var asset = assets[0];
            Assert.IsTrue(asset.IsClipLoaded);
            Assert.IsTrue(asset.Clip.IsReady);

            // Play() uses the preloaded shared clip; no new load starts.
            var shared = asset.Clip;
            var voice = asset.Play(UNAudioPlayParams.At(256));
            Assert.AreSame(shared, asset.Clip);
            float[] o = Render(256 + 4800);
            Assert.Greater(Rms(o, 0, 256, 256 + 4800), 0.05);
            voice.Stop();
            asset.Unload();
            Assert.IsFalse(asset.IsClipLoaded);
        }

        [UnityTest]
        public IEnumerator CopiedClipSurvivesBundleUnload()
        {
            StartManual();
            var clip = UNAudioAssetBundles.LoadClip(m_Bundle, "tone");
            Assert.IsTrue(clip.IsSourceReleased, "default load copies the bytes natively");
            // Destroys the TextAsset immediately; the clip must not care.
            m_Bundle.Unload(true);
            m_Bundle = AssetBundle.LoadFromFile(Path.Combine(BundleDir, BundleName));
            yield return Wait(() => clip.IsLoaded);
            var voice = clip.PlayScheduled(256);
            float[] o = Render(256 + 4800);
            Assert.Greater(Rms(o, 0, 256, 256 + 4800), 0.05);
            voice.Stop();
            clip.Dispose();
        }

        [UnityTest]
        public IEnumerator StreamFromBundleWithBorrowedMemory()
        {
            StartManual();
            var asset = m_Bundle.LoadAsset<UNAudioAsset>("tone");
            Assert.IsNotNull(asset);
            var clip = asset.Load(new UNAudioLoadOptions { mode = UNAudioLoadMode.Stream, borrow = true });
            yield return Wait(() => clip.IsLoaded);
            Assert.IsFalse(clip.IsSourceReleased, "a stream reads the bundle memory for its whole life");

            var voice = clip.PlayScheduled(1024);
            double until = EditorApplication.timeSinceStartup + 0.1;
            while (EditorApplication.timeSinceStartup < until) yield return null;
            var o = new float[(1024 + 24000) * 2];
            var block = new float[512];
            for (int f = 0; f < 1024 + 24000; f += 256)
            {
                UNAudioEngine.Render(block, 256);
                System.Array.Copy(block, 0, o, f * 2, System.Math.Min(512, o.Length - f * 2));
                System.Threading.Thread.Sleep(1);
            }
            Assert.AreEqual(0f, o[1023 * 2]);
            Assert.Greater(Rms(o, 0, 1024, 1024 + 24000), 0.05);

            voice.Stop();
            clip.Dispose();
            UNAudioEngine.Render(block, 256);  // manual device: the mixer applies the stop here
            yield return Wait(() => clip.IsSourceReleased);
        }
    }
}
