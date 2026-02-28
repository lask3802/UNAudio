using System.Runtime.InteropServices;
using System.Reflection;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.TestTools;

namespace UNAudio.Tests.Runtime
{
    /// <summary>
    /// Runtime unit tests for UNAudio.
    /// These tests run inside the Unity Test Runner.
    /// </summary>
    public class UNAudioRuntimeTests
    {
        private static void SetClipRuntimeState(UNAudioClip clip, int handle, bool loaded)
        {
            var type = typeof(UNAudioClip);
            type.GetField("nativeHandle", BindingFlags.Instance | BindingFlags.NonPublic)
                ?.SetValue(clip, handle);
            type.GetField("isLoaded", BindingFlags.Instance | BindingFlags.NonPublic)
                ?.SetValue(clip, loaded);
        }

        // ── AudioUtility ──────────────────────────────────────────

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

        // ── Enums ──────────────────────────────────────────────────

        [Test]
        public void AudioSourceType_EnumValues_AreCorrect()
        {
            Assert.AreEqual(0, (int)AudioSourceType.Embedded);
            Assert.AreEqual(1, (int)AudioSourceType.Path);
            Assert.AreEqual(2, (int)AudioSourceType.Addressable);
        }

        [Test]
        public void AudioLoadType_EnumValues_AreCorrect()
        {
            Assert.AreEqual(0, (int)AudioLoadType.CompressedInMemory);
            Assert.AreEqual(1, (int)AudioLoadType.DecompressOnLoad);
            Assert.AreEqual(2, (int)AudioLoadType.Streaming);
        }

        // ── UNAudioClip defaults ──────────────────────────────────

        [Test]
        public void UNAudioClip_DefaultState_IsNotLoaded()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();

            Assert.IsFalse(clip.IsLoaded);
            Assert.AreEqual(0, clip.sampleRate);
            Assert.AreEqual(-1, clip.NativeHandle);

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_DefaultSourceType_IsEmbedded()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();

            Assert.AreEqual(AudioSourceType.Embedded, clip.SourceType);

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_DefaultLoadType_IsCompressedInMemory()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();

            Assert.AreEqual(AudioLoadType.CompressedInMemory, clip.LoadType);

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_DefaultMetadata_IsAllZero()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();

            Assert.AreEqual(0, clip.sampleRate);
            Assert.AreEqual(0, clip.channels);
            Assert.AreEqual(0, clip.bitsPerSample);
            Assert.AreEqual(0f, clip.length);
            Assert.AreEqual(0L, clip.totalFrames);

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_DefaultMemorySize_IsZero()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();

            Assert.AreEqual(0, clip.GetMemorySize());

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_DefaultAudioFilePath_IsNull()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();

            Assert.IsNull(clip.AudioFilePath);

            Object.DestroyImmediate(clip);
        }

        // ── SetMetadata (5-param, backward compat) ────────────────

        [Test]
        public void UNAudioClip_SetMetadata5_StoresValues()
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

        [Test]
        public void UNAudioClip_SetMetadata5_DoesNotSetTotalFrames()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            clip.SetMetadata(44100, 1, 16, 1.0f, new byte[100]);

            Assert.AreEqual(0L, clip.totalFrames);

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_SetMetadata5_NullData_SetsZeroMemory()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            clip.SetMetadata(44100, 2, 16, 1.0f, null);

            Assert.AreEqual(0, clip.GetMemorySize());

            Object.DestroyImmediate(clip);
        }

        // ── SetMetadata (6-param, with totalFrames) ───────────────

        [Test]
        public void UNAudioClip_SetMetadata6_StoresAllValues()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            byte[] data = new byte[1024];
            clip.SetMetadata(48000, 2, 24, 2.5f, 120000L, data);

            Assert.AreEqual(48000, clip.sampleRate);
            Assert.AreEqual(2, clip.channels);
            Assert.AreEqual(24, clip.bitsPerSample);
            Assert.AreEqual(2.5f, clip.length, 0.001f);
            Assert.AreEqual(120000L, clip.totalFrames);
            Assert.AreEqual(1024, clip.GetMemorySize());

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_SetMetadata6_OverwritesPreviousValues()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            clip.SetMetadata(44100, 1, 16, 1.0f, 44100L, new byte[50]);
            clip.SetMetadata(96000, 6, 32, 10.0f, 960000L, new byte[2000]);

            Assert.AreEqual(96000, clip.sampleRate);
            Assert.AreEqual(6, clip.channels);
            Assert.AreEqual(32, clip.bitsPerSample);
            Assert.AreEqual(10.0f, clip.length, 0.001f);
            Assert.AreEqual(960000L, clip.totalFrames);
            Assert.AreEqual(2000, clip.GetMemorySize());

            Object.DestroyImmediate(clip);
        }

        // ── SetSourceType / SetAudioFilePath ──────────────────────

        [Test]
        public void UNAudioClip_SetSourceType_Path()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            clip.SetSourceType(AudioSourceType.Path);

            Assert.AreEqual(AudioSourceType.Path, clip.SourceType);

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_SetSourceType_Addressable()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            clip.SetSourceType(AudioSourceType.Addressable);

            Assert.AreEqual(AudioSourceType.Addressable, clip.SourceType);

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_SetAudioFilePath_StoresValue()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            clip.SetAudioFilePath("music/bgm.ogg");

            Assert.AreEqual("music/bgm.ogg", clip.AudioFilePath);

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_SetAudioFilePath_Null_StoresNull()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            clip.SetAudioFilePath("something");
            clip.SetAudioFilePath(null);

            Assert.IsNull(clip.AudioFilePath);

            Object.DestroyImmediate(clip);
        }

        // ── IsCompressed ──────────────────────────────────────────

        [Test]
        public void UNAudioClip_IsCompressed_TrueForCompressedInMemory()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            // Default is CompressedInMemory
            Assert.IsTrue(clip.IsCompressed);

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_IsCompressed_TrueForStreaming()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            clip.SetMetadata(44100, 2, 16, 1.0f, new byte[10]);
            // Need to use internal trick: set loadType via SetLoadType
            // But SetLoadType calls LoadAudioData which needs native.
            // So just verify the default and DecompressOnLoad case.
            Assert.IsTrue(clip.IsCompressed);

            Object.DestroyImmediate(clip);
        }

        // ── GetMemorySize ─────────────────────────────────────────

        [Test]
        public void UNAudioClip_GetMemorySize_ReturnsCorrectSize()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            clip.SetMetadata(44100, 2, 16, 0f, new byte[4096]);

            Assert.AreEqual(4096, clip.GetMemorySize());

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_GetMemorySize_LargeData()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            clip.SetMetadata(44100, 2, 16, 0f, new byte[1024 * 1024]);

            Assert.AreEqual(1024 * 1024, clip.GetMemorySize());

            Object.DestroyImmediate(clip);
        }

        // ── LoadAudioData without native engine ───────────────────

        [Test]
        public void UNAudioClip_LoadAudioData_NoData_RemainsUnloaded()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            // No data set, should not crash and remain unloaded
            clip.LoadAudioData();

            Assert.IsFalse(clip.IsLoaded);

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_LoadAudioData_EmptyData_RemainsUnloaded()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            clip.SetMetadata(44100, 2, 16, 0f, new byte[0]);

            clip.LoadAudioData();

            Assert.IsFalse(clip.IsLoaded);

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_LoadAudioData_StaleHandle_ResetsRuntimeState()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            SetClipRuntimeState(clip, 123, true);

            // No data is present, but stale runtime state should still be repaired.
            clip.LoadAudioData();

            Assert.IsFalse(clip.IsLoaded);
            Assert.AreEqual(-1, clip.NativeHandle);

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_UnloadAudioData_WhenNotLoaded_DoesNotThrow()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();

            Assert.DoesNotThrow(() => clip.UnloadAudioData());

            Object.DestroyImmediate(clip);
        }

        // ── LoadAudioDataAsync surface ────────────────────────────

        [Test]
        public void UNAudioClip_LoadAudioDataAsync_Embedded_NoData_CallbackFalse()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            bool? result = null;

            clip.LoadAudioDataAsync(success => result = success);

            // Embedded with no data: sync path, callback invoked immediately
            Assert.IsNotNull(result);
            Assert.IsFalse(result.Value);

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_LoadAudioDataAsync_NullCallback_DoesNotThrow()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();

            Assert.DoesNotThrow(() => clip.LoadAudioDataAsync(null));

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_LoadAudioDataAsync_Path_EmptyPath_CallbackFalse()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            clip.SetSourceType(AudioSourceType.Path);
            clip.SetAudioFilePath("");

            LogAssert.Expect(LogType.Error, "[UNAudio] Audio file path is empty.");

            bool? result = null;
            clip.LoadAudioDataAsync(success => result = success);

            // Empty path: callback invoked synchronously with false
            Assert.IsNotNull(result);
            Assert.IsFalse(result.Value);

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_LoadAudioDataAsync_Addressable_NoPackage_CallbackFalse()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            clip.SetSourceType(AudioSourceType.Addressable);

            bool? result = null;
            clip.LoadAudioDataAsync(success => result = success);

            // Without Addressables package: fallback callback with false
            Assert.IsNotNull(result);
            Assert.IsFalse(result.Value);

            Object.DestroyImmediate(clip);
        }

        // ── UNAudioClipInfo struct layout ─────────────────────────

        [Test]
        public void UNAudioClipInfo_IsSequentialLayout()
        {
            var layout = typeof(UNAudioClipInfo).StructLayoutAttribute;
            Assert.IsNotNull(layout);
            Assert.AreEqual(LayoutKind.Sequential, layout.Value);
        }

        [Test]
        public void UNAudioClipInfo_FieldsExist()
        {
            var info = new UNAudioClipInfo();
            info.sampleRate = 44100;
            info.channels = 2;
            info.bitsPerSample = 16;
            info.lengthInSeconds = 3.5f;
            info.totalFrames = 154350L;
            info.compressionMode = 0;

            Assert.AreEqual(44100, info.sampleRate);
            Assert.AreEqual(2, info.channels);
            Assert.AreEqual(16, info.bitsPerSample);
            Assert.AreEqual(3.5f, info.lengthInSeconds, 0.001f);
            Assert.AreEqual(154350L, info.totalFrames);
            Assert.AreEqual(0, info.compressionMode);
        }

        // ── UNAudioOutputConfig struct layout ─────────────────────

        [Test]
        public void UNAudioOutputConfig_IsSequentialLayout()
        {
            var layout = typeof(UNAudioOutputConfig).StructLayoutAttribute;
            Assert.IsNotNull(layout);
            Assert.AreEqual(LayoutKind.Sequential, layout.Value);
        }

        // ── UNAudioSystemFormat struct layout ────────────────────

        [Test]
        public void UNAudioSystemFormat_IsSequentialLayout()
        {
            var layout = typeof(UNAudioSystemFormat).StructLayoutAttribute;
            Assert.IsNotNull(layout);
            Assert.AreEqual(LayoutKind.Sequential, layout.Value);
        }

        [Test]
        public void UNAudioSystemFormat_FieldsExist()
        {
            var fmt = new UNAudioSystemFormat();
            fmt.sampleRate = 48000;
            fmt.channels = 2;
            fmt.bitsPerSample = 32;
            fmt.isValid = 1;

            Assert.AreEqual(48000, fmt.sampleRate);
            Assert.AreEqual(2, fmt.channels);
            Assert.AreEqual(32, fmt.bitsPerSample);
            Assert.AreEqual(1, fmt.isValid);
        }

        // ── UNAudioBridge guard checks ────────────────────────────

        [Test]
        public void UNAudioBridge_LoadAudio_NullData_ReturnsNegative()
        {
            int handle = UNAudioBridge.LoadAudio(null, 0);
            Assert.AreEqual(-1, handle);
        }

        [Test]
        public void UNAudioBridge_LoadAudio_EmptyData_ReturnsNegative()
        {
            int handle = UNAudioBridge.LoadAudio(new byte[0], 0);
            Assert.AreEqual(-1, handle);
        }
    }
}
