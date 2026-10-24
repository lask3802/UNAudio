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
    /// <summary>MP3 and FLAC through the importer override, in both load modes.</summary>
    public class UNAudioFormatTests
    {
        const string AssetDir = "Assets/__UNAudioFormatTest";

        [TearDown]
        public void TearDown()
        {
            UNAudioEngine.Shutdown();
            if (AssetDatabase.IsValidFolder(AssetDir)) AssetDatabase.DeleteAsset(AssetDir);
        }

        static UNAudioAsset Import(string fixture, string file)
        {
            if (!AssetDatabase.IsValidFolder(AssetDir)) AssetDatabase.CreateFolder("Assets", "__UNAudioFormatTest");
            string path = AssetDir + "/" + file;
            File.WriteAllBytes(path, File.ReadAllBytes(Path.GetFullPath(FixtureDir + "/" + fixture)));
            AssetDatabase.ImportAsset(path, ImportAssetOptions.ForceSynchronousImport);
            // Unity imports both extensions as AudioClips by default; switch to the override.
            Assert.IsNotInstanceOf<UNAudioImporter>(AssetImporter.GetAtPath(path));
            UNAudioImporterUtility.UseUNAudio(path);
            AssetDatabase.ImportAsset(path, ImportAssetOptions.ForceUpdate | ImportAssetOptions.ForceSynchronousImport);
            Assert.IsInstanceOf<UNAudioImporter>(AssetImporter.GetAtPath(path));
            var asset = AssetDatabase.LoadAssetAtPath<UNAudioAsset>(path);
            Assert.IsNotNull(asset, path);
            return asset;
        }

        static IEnumerator ImportAndPlay(string fixture, string file, string format, int rate, long frames,
                                         UNAudioLoadMode mode)
        {
            var asset = Import(fixture, file);
            Assert.AreEqual(format, asset.Format);
            Assert.AreEqual(rate, asset.SampleRate);
            Assert.AreEqual(2, asset.Channels);
            Assert.AreEqual(frames, asset.Frames);

            StartManual();
            var clip = asset.Load(new UNAudioLoadOptions { mode = mode });
            try
            {
                yield return Wait(() => clip.IsReady || clip.IsFailed);
                Assert.IsTrue(clip.IsReady, $"{file} failed to load");
                var voice = clip.Play(UNAudioPlayParams.At(4800));
                System.Threading.Thread.Sleep(200);  // a stream voice primes on the stream thread
                float[] o = Render(4800 + 9600);
                Assert.Greater(Rms(o, 0, 4800, 4800 + 9600), 0.01, "left");
                Assert.Greater(Rms(o, 1, 4800, 4800 + 9600), 0.01, "right");
                Assert.AreEqual(UNAudioResult.Ok, voice.Error);
                voice.Stop();
            }
            finally
            {
                clip.Dispose();
            }
        }

        [UnityTest]
        public IEnumerator Mp3Decode() =>
            ImportAndPlay("mp3_44k_stereo_cbr.bytes", "cbr.mp3", "mp3", 44100, 88200, UNAudioLoadMode.Decode);

        [UnityTest]
        public IEnumerator Mp3Stream() =>
            ImportAndPlay("mp3_44k_stereo_cbr.bytes", "cbr_stream.mp3", "mp3", 44100, 88200, UNAudioLoadMode.Stream);

        [UnityTest]
        public IEnumerator FlacDecode() =>
            ImportAndPlay("flac_48k_stereo_16.bytes", "tone.flac", "flac", 48000, 48000, UNAudioLoadMode.Decode);

        [UnityTest]
        public IEnumerator FlacStream() =>
            ImportAndPlay("flac_48k_stereo_16.bytes", "tone_stream.flac", "flac", 48000, 48000, UNAudioLoadMode.Stream);

        // A standalone file outside the project (downloaded, user content): no import at all.
        static IEnumerator LoadFileAndPlay(string fixture, string file, double seconds, UNAudioLoadMode mode)
        {
            string dir = Path.GetFullPath("Temp/UNAudioFileTest");
            Directory.CreateDirectory(dir);
            string path = Path.Combine(dir, file);
            File.WriteAllBytes(path, File.ReadAllBytes(Path.GetFullPath(FixtureDir + "/" + fixture)));

            StartManual();
            var clip = UNAudioClip.LoadFile(path, new UNAudioLoadOptions { mode = mode });
            try
            {
                Assert.IsTrue(clip.IsSourceReleased, "the engine reads the file into its own buffer");
                yield return Wait(() => clip.IsReady || clip.IsFailed);
                Assert.IsTrue(clip.IsReady, $"{file} failed to load");
                Assert.AreEqual(Path.GetFileNameWithoutExtension(file), clip.Name);
                Assert.AreEqual(seconds, clip.Length, 1e-3);
                var voice = clip.Play(UNAudioPlayParams.At(4800));
                System.Threading.Thread.Sleep(200);
                float[] o = Render(4800 + 9600);
                Assert.Greater(Rms(o, 0, 4800, 4800 + 9600), 0.01);
                Assert.AreEqual(UNAudioResult.Ok, voice.Error);
                voice.Stop();
            }
            finally
            {
                clip.Dispose();
                File.Delete(path);
            }
        }

        [UnityTest]
        public IEnumerator FlacFileDecode() => LoadFileAndPlay("flac_48k_stereo_16.bytes", "tone.flac", 1.0, UNAudioLoadMode.Decode);

        [UnityTest]
        public IEnumerator Mp3FileStream() => LoadFileAndPlay("mp3_44k_stereo_cbr.bytes", "cbr.mp3", 2.0, UNAudioLoadMode.Stream);

        // The file is opened on a worker thread, so a missing one fails the clip, not the call.
        [UnityTest]
        public IEnumerator MissingFileFailsTheClip()
        {
            StartManual();
            var clip = UNAudioClip.LoadFile(Path.GetFullPath("Temp/UNAudioFileTest/missing.ogg"));
            var ready = clip.WhenReady;
            yield return Wait(() => ready.IsCompleted);
            Assert.IsTrue(clip.IsFailed);
            Assert.AreEqual(UNAudioResult.IOError, clip.Info.Error);
            Assert.IsTrue(ready.IsFaulted);
            Assert.IsInstanceOf<UNAudioException>(ready.Exception.InnerException);
            clip.Dispose();
        }
    }
}
