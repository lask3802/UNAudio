using System;
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
    public class UNAudioErrorAndPreloadTests
    {
        const string AssetDir = "Assets/__UNAudioPreloadTest";

        [TearDown]
        public void TearDown()
        {
            UNAudioEngine.Shutdown();
            if (AssetDatabase.IsValidFolder(AssetDir)) AssetDatabase.DeleteAsset(AssetDir);
        }

        [UnityTest]
        public IEnumerator StreamDecodeFailureReachesCSharpWithErrorCode()
        {
            StartManual();
            byte[] bytes = ToneBytes();
            var clip = UNAudioClip.Load(bytes, new UNAudioLoadOptions { mode = UNAudioLoadMode.Stream, borrow = true });
            yield return Wait(() => clip.IsLoaded);

            // The clip was probed at load; the voice's decoder opens on the stream thread later.
            Array.Clear(bytes, 0, bytes.Length);
            UNAudioVoice failed = default;
            var error = UNAudioResult.Ok;
            int ended = 0;
            Action<UNAudioVoice, UNAudioResult> onError = (v, e) => { failed = v; error = e; };
            Action<UNAudioVoice, long> onEnded = (v, f) => ended++;
            UNAudioEngine.VoiceError += onError;
            UNAudioEngine.VoiceEnded += onEnded;
            try
            {
                var voice = clip.Play();
                var block = new float[512];
                double until = EditorApplication.timeSinceStartup + 5;
                while (error == UNAudioResult.Ok && EditorApplication.timeSinceStartup < until)
                {
                    UNAudioEngine.Render(block, 256);
                    UNAudioEngine.Update();
                    System.Threading.Thread.Sleep(1);
                }
                Assert.AreEqual(UNAudioResult.UnsupportedFormat, error);
                Assert.AreEqual(voice, failed);
                Assert.AreEqual(UNAudioResult.UnsupportedFormat, voice.Error);
                Assert.AreEqual(1, ended, "VOICE_ENDED follows VOICE_ERROR");
                Assert.IsFalse(voice.IsAlive);
                Assert.AreEqual(UNAudioResult.Ok, default(UNAudioVoice).Error);
                UNAudioEngine.Shutdown();
                Assert.AreEqual(UNAudioResult.Ok, voice.Error, "errors belong to their engine session");
            }
            finally
            {
                UNAudioEngine.VoiceError -= onError;
                UNAudioEngine.VoiceEnded -= onEnded;
                clip.Dispose();
            }
        }

        static UNAudioAsset ImportTone(string file, bool preload)
        {
            if (!AssetDatabase.IsValidFolder(AssetDir)) AssetDatabase.CreateFolder("Assets", "__UNAudioPreloadTest");
            string path = AssetDir + "/" + file;
            File.WriteAllBytes(path, ToneBytes());
            AssetDatabase.ImportAsset(path, ImportAssetOptions.ForceSynchronousImport);
            UNAudioImporterUtility.UseUNAudio(path);
            AssetDatabase.ImportAsset(path, ImportAssetOptions.ForceUpdate | ImportAssetOptions.ForceSynchronousImport);
            var importer = (UNAudioImporter)AssetImporter.GetAtPath(path);
            importer.preloadAudioData = preload;
            EditorUtility.SetDirty(importer);  // SaveAndReimport only writes a dirty importer
            importer.SaveAndReimport();
            var asset = AssetDatabase.LoadAssetAtPath<UNAudioAsset>(path);
            Assert.IsNotNull(asset);
            Assert.AreEqual(preload, asset.PreloadAudioData);
            return asset;
        }

        [UnityTest]
        public IEnumerator PreloadAudioDataStartsTheSharedClipOncePerEngineSession()
        {
            UNAudioEngine.Shutdown();
            var preloaded = ImportTone("preload.ogg", true);
            var lazy = ImportTone("lazy.ogg", false);
            Assert.IsTrue(UNAudioPreloader.IsRegistered(preloaded));
            Assert.IsFalse(UNAudioPreloader.IsRegistered(lazy));

            // Never initialises the engine itself: that would take the game's own Initialize(config).
            UNAudioPreloader.Tick(requirePlayMode: false);  // what UNAudioEngine.Update does in play mode
            Assert.IsFalse(UNAudioEngine.IsInitialized);
            Assert.IsFalse(preloaded.IsClipLoaded);

            StartManual();
            UNAudioPreloader.Tick(requirePlayMode: false);
            Assert.IsTrue(preloaded.IsClipLoaded, "preload asset started its shared clip");
            Assert.IsFalse(lazy.IsClipLoaded, "assets without preload load on first use");
            yield return Wait(() => preloaded.Clip.IsReady);

            var voice = preloaded.Play(UNAudioPlayParams.At(256));
            float[] o = Render(256 + 4800);
            Assert.Greater(Rms(o, 0, 256, 256 + 4800), 0.05);
            voice.Stop();

            // A new session (or play mode without a domain reload) starts it again.
            UNAudioEngine.Shutdown();
            StartManual();
            Assert.IsFalse(preloaded.IsClipLoaded);
            UNAudioPreloader.Tick(requirePlayMode: false);
            Assert.IsTrue(preloaded.IsClipLoaded, "restarted in the new session");
            Assert.IsTrue(UNAudioPreloader.IsRegistered(preloaded));
        }

        [Test]
        public void PreloadTickOutsidePlayModeDoesNothing()
        {
            var preloaded = ImportTone("preload_edit.ogg", true);
            StartManual();
            UNAudioPreloader.Tick();  // edit mode (inspector preview): no preloading
            Assert.IsFalse(preloaded.IsClipLoaded);
            Assert.IsTrue(UNAudioPreloader.IsRegistered(preloaded));
        }
    }
}
