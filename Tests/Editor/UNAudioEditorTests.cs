using NUnit.Framework;
using UnityEditor;
using UnityEngine;

namespace UNAudio.Tests.Editor
{
    /// <summary>
    /// Editor-only unit tests for UNAudio (AudioHeaderParser, SerializedProperty fields, etc.).
    /// </summary>
    public class UNAudioEditorTests
    {
        // ═══════════════════════════════════════════════════════════
        //  Helper: build a minimal WAV byte array
        // ═══════════════════════════════════════════════════════════

        /// <summary>
        /// Builds a valid WAV file in memory with the given parameters.
        /// </summary>
        private static byte[] BuildWav(
            int sampleRate, short channels, short bitsPerSample,
            int numFrames, short audioFormat = 1)
        {
            int blockAlign = channels * (bitsPerSample / 8);
            int byteRate = sampleRate * blockAlign;
            int dataSize = numFrames * blockAlign;

            var wav = new System.Collections.Generic.List<byte>();

            // RIFF header
            wav.AddRange(System.Text.Encoding.ASCII.GetBytes("RIFF"));
            wav.AddRange(System.BitConverter.GetBytes(0)); // placeholder
            wav.AddRange(System.Text.Encoding.ASCII.GetBytes("WAVE"));

            // fmt chunk
            wav.AddRange(System.Text.Encoding.ASCII.GetBytes("fmt "));
            wav.AddRange(System.BitConverter.GetBytes(16));           // chunk size
            wav.AddRange(System.BitConverter.GetBytes(audioFormat));  // format
            wav.AddRange(System.BitConverter.GetBytes(channels));
            wav.AddRange(System.BitConverter.GetBytes(sampleRate));
            wav.AddRange(System.BitConverter.GetBytes(byteRate));
            wav.AddRange(System.BitConverter.GetBytes((short)blockAlign));
            wav.AddRange(System.BitConverter.GetBytes(bitsPerSample));

            // data chunk
            wav.AddRange(System.Text.Encoding.ASCII.GetBytes("data"));
            wav.AddRange(System.BitConverter.GetBytes(dataSize));
            wav.AddRange(new byte[dataSize]);

            // Fix RIFF size
            int riffSize = wav.Count - 8;
            var sizeBytes = System.BitConverter.GetBytes(riffSize);
            wav[4] = sizeBytes[0];
            wav[5] = sizeBytes[1];
            wav[6] = sizeBytes[2];
            wav[7] = sizeBytes[3];

            return wav.ToArray();
        }

        /// <summary>
        /// Builds a WAV with a JUNK chunk before fmt (tests chunk scanning).
        /// </summary>
        private static byte[] BuildWavWithJunkChunk(
            int sampleRate, short channels, short bitsPerSample, int numFrames)
        {
            int blockAlign = channels * (bitsPerSample / 8);
            int byteRate = sampleRate * blockAlign;
            int dataSize = numFrames * blockAlign;

            var wav = new System.Collections.Generic.List<byte>();

            // RIFF + WAVE
            wav.AddRange(System.Text.Encoding.ASCII.GetBytes("RIFF"));
            wav.AddRange(System.BitConverter.GetBytes(0));
            wav.AddRange(System.Text.Encoding.ASCII.GetBytes("WAVE"));

            // JUNK chunk (16 bytes of padding)
            wav.AddRange(System.Text.Encoding.ASCII.GetBytes("JUNK"));
            wav.AddRange(System.BitConverter.GetBytes(16));
            wav.AddRange(new byte[16]);

            // data chunk BEFORE fmt (reversed order)
            wav.AddRange(System.Text.Encoding.ASCII.GetBytes("data"));
            wav.AddRange(System.BitConverter.GetBytes(dataSize));
            wav.AddRange(new byte[dataSize]);

            // fmt chunk
            wav.AddRange(System.Text.Encoding.ASCII.GetBytes("fmt "));
            wav.AddRange(System.BitConverter.GetBytes(16));
            wav.AddRange(System.BitConverter.GetBytes((short)1));
            wav.AddRange(System.BitConverter.GetBytes(channels));
            wav.AddRange(System.BitConverter.GetBytes(sampleRate));
            wav.AddRange(System.BitConverter.GetBytes(byteRate));
            wav.AddRange(System.BitConverter.GetBytes((short)blockAlign));
            wav.AddRange(System.BitConverter.GetBytes(bitsPerSample));

            // Fix RIFF size
            int riffSize = wav.Count - 8;
            var sizeBytes = System.BitConverter.GetBytes(riffSize);
            wav[4] = sizeBytes[0];
            wav[5] = sizeBytes[1];
            wav[6] = sizeBytes[2];
            wav[7] = sizeBytes[3];

            return wav.ToArray();
        }

        // ═══════════════════════════════════════════════════════════
        //  AudioHeaderParser — valid WAV files
        // ═══════════════════════════════════════════════════════════

        [Test]
        public void AudioHeaderParser_PCM_Stereo_44100_16bit()
        {
            byte[] wav = BuildWav(44100, 2, 16, 100);
            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseWav(wav, out var meta);

            Assert.IsTrue(ok);
            Assert.AreEqual(44100, meta.sampleRate);
            Assert.AreEqual(2, meta.channels);
            Assert.AreEqual(16, meta.bitsPerSample);
            Assert.AreEqual(100L, meta.totalFrames);
            Assert.AreEqual(100f / 44100f, meta.lengthInSeconds, 0.0001f);
        }

        [Test]
        public void AudioHeaderParser_PCM_Mono_48000_16bit()
        {
            byte[] wav = BuildWav(48000, 1, 16, 48000);
            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseWav(wav, out var meta);

            Assert.IsTrue(ok);
            Assert.AreEqual(48000, meta.sampleRate);
            Assert.AreEqual(1, meta.channels);
            Assert.AreEqual(16, meta.bitsPerSample);
            Assert.AreEqual(48000L, meta.totalFrames);
            Assert.AreEqual(1.0f, meta.lengthInSeconds, 0.0001f);
        }

        [Test]
        public void AudioHeaderParser_PCM_Stereo_96000_24bit()
        {
            byte[] wav = BuildWav(96000, 2, 24, 960);
            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseWav(wav, out var meta);

            Assert.IsTrue(ok);
            Assert.AreEqual(96000, meta.sampleRate);
            Assert.AreEqual(2, meta.channels);
            Assert.AreEqual(24, meta.bitsPerSample);
            Assert.AreEqual(960L, meta.totalFrames);
            Assert.AreEqual(960f / 96000f, meta.lengthInSeconds, 0.0001f);
        }

        [Test]
        public void AudioHeaderParser_PCM_Mono_22050_8bit()
        {
            // 8-bit audio: blockAlign = 1*1 = 1
            byte[] wav = BuildWav(22050, 1, 8, 22050);
            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseWav(wav, out var meta);

            Assert.IsTrue(ok);
            Assert.AreEqual(22050, meta.sampleRate);
            Assert.AreEqual(1, meta.channels);
            Assert.AreEqual(8, meta.bitsPerSample);
            Assert.AreEqual(22050L, meta.totalFrames);
            Assert.AreEqual(1.0f, meta.lengthInSeconds, 0.0001f);
        }

        [Test]
        public void AudioHeaderParser_Float_Stereo_44100_32bit()
        {
            // IEEE Float format = 3
            byte[] wav = BuildWav(44100, 2, 32, 44100, audioFormat: 3);
            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseWav(wav, out var meta);

            Assert.IsTrue(ok);
            Assert.AreEqual(44100, meta.sampleRate);
            Assert.AreEqual(2, meta.channels);
            Assert.AreEqual(32, meta.bitsPerSample);
            Assert.AreEqual(44100L, meta.totalFrames);
            Assert.AreEqual(1.0f, meta.lengthInSeconds, 0.0001f);
        }

        [Test]
        public void AudioHeaderParser_CalculatesLengthCorrectly_LongFile()
        {
            // 5 minutes of 44100 Hz stereo 16-bit
            int frames = 44100 * 300; // 300 seconds
            byte[] wav = BuildWav(44100, 2, 16, frames);
            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseWav(wav, out var meta);

            Assert.IsTrue(ok);
            Assert.AreEqual((long)frames, meta.totalFrames);
            Assert.AreEqual(300f, meta.lengthInSeconds, 0.01f);
        }

        [Test]
        public void AudioHeaderParser_SingleFrame()
        {
            byte[] wav = BuildWav(44100, 2, 16, 1);
            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseWav(wav, out var meta);

            Assert.IsTrue(ok);
            Assert.AreEqual(1L, meta.totalFrames);
        }

        // ═══════════════════════════════════════════════════════════
        //  AudioHeaderParser — chunk ordering / extra chunks
        // ═══════════════════════════════════════════════════════════

        [Test]
        public void AudioHeaderParser_HandlesReversedChunks()
        {
            // data chunk before fmt chunk, with JUNK chunk
            byte[] wav = BuildWavWithJunkChunk(44100, 2, 16, 100);
            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseWav(wav, out var meta);

            Assert.IsTrue(ok);
            Assert.AreEqual(44100, meta.sampleRate);
            Assert.AreEqual(2, meta.channels);
            Assert.AreEqual(100L, meta.totalFrames);
        }

        // ═══════════════════════════════════════════════════════════
        //  AudioHeaderParser — invalid/edge-case inputs
        // ═══════════════════════════════════════════════════════════

        [Test]
        public void AudioHeaderParser_NullData_ReturnsFalse()
        {
            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseWav(null, out _);
            Assert.IsFalse(ok);
        }

        [Test]
        public void AudioHeaderParser_EmptyData_ReturnsFalse()
        {
            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseWav(new byte[0], out _);
            Assert.IsFalse(ok);
        }

        [Test]
        public void AudioHeaderParser_TooShort_ReturnsFalse()
        {
            // Less than 44 bytes
            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseWav(new byte[20], out _);
            Assert.IsFalse(ok);
        }

        [Test]
        public void AudioHeaderParser_NotRIFF_ReturnsFalse()
        {
            byte[] data = new byte[100];
            System.Text.Encoding.ASCII.GetBytes("XXXX").CopyTo(data, 0);
            System.Text.Encoding.ASCII.GetBytes("WAVE").CopyTo(data, 8);

            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseWav(data, out _);
            Assert.IsFalse(ok);
        }

        [Test]
        public void AudioHeaderParser_RIFFButNotWAVE_ReturnsFalse()
        {
            byte[] data = new byte[100];
            System.Text.Encoding.ASCII.GetBytes("RIFF").CopyTo(data, 0);
            System.Text.Encoding.ASCII.GetBytes("AVI ").CopyTo(data, 8);

            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseWav(data, out _);
            Assert.IsFalse(ok);
        }

        [Test]
        public void AudioHeaderParser_MissingDataChunk_ReturnsFalse()
        {
            // Build a WAV but don't include "data" chunk
            var wav = new System.Collections.Generic.List<byte>();
            wav.AddRange(System.Text.Encoding.ASCII.GetBytes("RIFF"));
            wav.AddRange(System.BitConverter.GetBytes(0));
            wav.AddRange(System.Text.Encoding.ASCII.GetBytes("WAVE"));

            // fmt chunk only
            wav.AddRange(System.Text.Encoding.ASCII.GetBytes("fmt "));
            wav.AddRange(System.BitConverter.GetBytes(16));
            wav.AddRange(System.BitConverter.GetBytes((short)1));
            wav.AddRange(System.BitConverter.GetBytes((short)2));
            wav.AddRange(System.BitConverter.GetBytes(44100));
            wav.AddRange(System.BitConverter.GetBytes(176400));
            wav.AddRange(System.BitConverter.GetBytes((short)4));
            wav.AddRange(System.BitConverter.GetBytes((short)16));

            // Fix RIFF size
            int riffSize = wav.Count - 8;
            var sizeBytes = System.BitConverter.GetBytes(riffSize);
            wav[4] = sizeBytes[0]; wav[5] = sizeBytes[1];
            wav[6] = sizeBytes[2]; wav[7] = sizeBytes[3];

            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseWav(wav.ToArray(), out _);
            Assert.IsFalse(ok);
        }

        [Test]
        public void AudioHeaderParser_MissingFmtChunk_ReturnsFalse()
        {
            // Build a WAV but don't include "fmt " chunk
            var wav = new System.Collections.Generic.List<byte>();
            wav.AddRange(System.Text.Encoding.ASCII.GetBytes("RIFF"));
            wav.AddRange(System.BitConverter.GetBytes(0));
            wav.AddRange(System.Text.Encoding.ASCII.GetBytes("WAVE"));

            // data chunk only
            wav.AddRange(System.Text.Encoding.ASCII.GetBytes("data"));
            wav.AddRange(System.BitConverter.GetBytes(100));
            wav.AddRange(new byte[100]);

            int riffSize = wav.Count - 8;
            var sizeBytes = System.BitConverter.GetBytes(riffSize);
            wav[4] = sizeBytes[0]; wav[5] = sizeBytes[1];
            wav[6] = sizeBytes[2]; wav[7] = sizeBytes[3];

            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseWav(wav.ToArray(), out _);
            Assert.IsFalse(ok);
        }

        [Test]
        public void AudioHeaderParser_UnsupportedFormat_ReturnsFalse()
        {
            // audioFormat = 7 (mu-law) is unsupported
            byte[] wav = BuildWav(44100, 2, 16, 100, audioFormat: 7);
            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseWav(wav, out _);
            Assert.IsFalse(ok);
        }

        [Test]
        public void AudioHeaderParser_MP3Data_ReturnsFalse()
        {
            // MP3 files start with 0xFF 0xFB or "ID3"
            byte[] mp3 = new byte[100];
            mp3[0] = 0x49; mp3[1] = 0x44; mp3[2] = 0x33; // "ID3"
            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseWav(mp3, out _);
            Assert.IsFalse(ok);
        }

        [Test]
        public void AudioHeaderParser_RandomGarbage_ReturnsFalse()
        {
            byte[] garbage = new byte[200];
            var rng = new System.Random(42);
            rng.NextBytes(garbage);

            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseWav(garbage, out _);
            Assert.IsFalse(ok);
        }

        // ═══════════════════════════════════════════════════════════
        //  Helper: build a minimal OGG Vorbis byte array
        // ═══════════════════════════════════════════════════════════

        /// <summary>
        /// Builds a minimal OGG Vorbis file with the given parameters.
        /// Creates two OGG pages: the first contains the Vorbis identification header,
        /// the last has a granule position for total duration.
        /// </summary>
        private static byte[] BuildOggVorbis(int sampleRate, byte channels, long totalFrames)
        {
            var ogg = new System.Collections.Generic.List<byte>();

            // ── Page 1: Vorbis identification header ──
            // OGG page header
            ogg.AddRange(System.Text.Encoding.ASCII.GetBytes("OggS")); // capture pattern
            ogg.Add(0); // stream_structure_version
            ogg.Add(0x02); // header_type: beginning of stream
            ogg.AddRange(System.BitConverter.GetBytes(0L)); // granule position
            ogg.AddRange(System.BitConverter.GetBytes(0x12345678)); // serial number
            ogg.AddRange(System.BitConverter.GetBytes(0)); // page sequence number
            ogg.AddRange(System.BitConverter.GetBytes(0)); // CRC (placeholder)
            ogg.Add(1); // number of segments
            ogg.Add(30); // segment table: 30 bytes in this segment

            // Vorbis identification header (30 bytes)
            ogg.Add(0x01); // packet type = identification
            ogg.AddRange(System.Text.Encoding.ASCII.GetBytes("vorbis")); // 6 bytes
            ogg.AddRange(System.BitConverter.GetBytes(0)); // vorbis_version = 0
            ogg.Add(channels); // audio_channels
            ogg.AddRange(System.BitConverter.GetBytes(sampleRate)); // audio_sample_rate
            ogg.AddRange(System.BitConverter.GetBytes(0)); // bitrate_maximum
            ogg.AddRange(System.BitConverter.GetBytes(128000)); // bitrate_nominal
            ogg.AddRange(System.BitConverter.GetBytes(0)); // bitrate_minimum

            // ── Page 2: last page with granule position (for duration) ──
            ogg.AddRange(System.Text.Encoding.ASCII.GetBytes("OggS"));
            ogg.Add(0); // stream_structure_version
            ogg.Add(0x04); // header_type: end of stream
            ogg.AddRange(System.BitConverter.GetBytes(totalFrames)); // granule position
            ogg.AddRange(System.BitConverter.GetBytes(0x12345678)); // serial number
            ogg.AddRange(System.BitConverter.GetBytes(1)); // page sequence number
            ogg.AddRange(System.BitConverter.GetBytes(0)); // CRC (placeholder)
            ogg.Add(1); // number of segments
            ogg.Add(0); // segment table: 0 bytes (empty page)

            return ogg.ToArray();
        }

        // ═══════════════════════════════════════════════════════════
        //  AudioHeaderParser — OGG Vorbis files
        // ═══════════════════════════════════════════════════════════

        [Test]
        public void AudioHeaderParser_OggVorbis_Stereo_48000()
        {
            byte[] ogg = BuildOggVorbis(48000, 2, 48000 * 5);
            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseOggVorbis(ogg, out var meta);

            Assert.IsTrue(ok);
            Assert.AreEqual(48000, meta.sampleRate);
            Assert.AreEqual(2, meta.channels);
            Assert.AreEqual(48000L * 5, meta.totalFrames);
            Assert.AreEqual(5.0f, meta.lengthInSeconds, 0.01f);
        }

        [Test]
        public void AudioHeaderParser_OggVorbis_Mono_44100()
        {
            byte[] ogg = BuildOggVorbis(44100, 1, 44100);
            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseOggVorbis(ogg, out var meta);

            Assert.IsTrue(ok);
            Assert.AreEqual(44100, meta.sampleRate);
            Assert.AreEqual(1, meta.channels);
            Assert.AreEqual(44100L, meta.totalFrames);
            Assert.AreEqual(1.0f, meta.lengthInSeconds, 0.01f);
        }

        [Test]
        public void AudioHeaderParser_OggVorbis_RejectsWavData()
        {
            byte[] wav = BuildWav(44100, 2, 16, 100);
            bool ok = UNAudio.Editor.AudioHeaderParser.TryParseOggVorbis(wav, out _);
            Assert.IsFalse(ok);
        }

        [Test]
        public void AudioHeaderParser_TryParse_DetectsOgg()
        {
            byte[] ogg = BuildOggVorbis(48000, 2, 48000);
            bool ok = UNAudio.Editor.AudioHeaderParser.TryParse(ogg, out var meta);

            Assert.IsTrue(ok);
            Assert.AreEqual(48000, meta.sampleRate);
            Assert.AreEqual(2, meta.channels);
        }

        [Test]
        public void AudioHeaderParser_TryParse_DetectsWav()
        {
            byte[] wav = BuildWav(44100, 2, 16, 100);
            bool ok = UNAudio.Editor.AudioHeaderParser.TryParse(wav, out var meta);

            Assert.IsTrue(ok);
            Assert.AreEqual(44100, meta.sampleRate);
        }

        [Test]
        public void AudioHeaderParser_TryParse_RejectsGarbage()
        {
            byte[] garbage = new byte[200];
            var rng = new System.Random(42);
            rng.NextBytes(garbage);

            bool ok = UNAudio.Editor.AudioHeaderParser.TryParse(garbage, out _);
            Assert.IsFalse(ok);
        }

        // ═══════════════════════════════════════════════════════════
        //  UNAudioClip — SerializedProperty field existence
        // ═══════════════════════════════════════════════════════════

        private static readonly string[] ExpectedSerializedProperties = {
            "sourceType",
            "compressedData",
            "audioFilePath",
            "sampleRateValue",
            "channelsValue",
            "bitsPerSampleValue",
            "lengthValue",
            "totalFramesValue",
            "loadType"
        };

        [Test]
        public void UNAudioClip_AllSerializedProperties_Exist()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            var so = new SerializedObject(clip);

            foreach (var propName in ExpectedSerializedProperties)
            {
                var prop = so.FindProperty(propName);
                Assert.IsNotNull(prop, $"Missing SerializedProperty: {propName}");
            }

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_SourceType_SerializedProperty_ReflectsValue()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            var so = new SerializedObject(clip);

            // Default should be Embedded (0)
            Assert.AreEqual(
                (int)AudioSourceType.Embedded,
                so.FindProperty("sourceType").enumValueIndex);

            // Change to Path via SerializedProperty
            so.FindProperty("sourceType").enumValueIndex = (int)AudioSourceType.Path;
            so.ApplyModifiedPropertiesWithoutUndo();
            Assert.AreEqual(AudioSourceType.Path, clip.SourceType);

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_LoadType_SerializedProperty_ReflectsValue()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            var so = new SerializedObject(clip);

            Assert.AreEqual(
                (int)AudioLoadType.CompressedInMemory,
                so.FindProperty("loadType").enumValueIndex);

            so.FindProperty("loadType").enumValueIndex = (int)AudioLoadType.Streaming;
            so.ApplyModifiedPropertiesWithoutUndo();
            Assert.AreEqual(AudioLoadType.Streaming, clip.LoadType);

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_Metadata_SerializedProperty_Roundtrip()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            var so = new SerializedObject(clip);

            so.FindProperty("sampleRateValue").intValue = 96000;
            so.FindProperty("channelsValue").intValue = 6;
            so.FindProperty("bitsPerSampleValue").intValue = 32;
            so.FindProperty("lengthValue").floatValue = 120.5f;
            so.FindProperty("totalFramesValue").longValue = 11568000L;
            so.FindProperty("audioFilePath").stringValue = "sfx/explosion.wav";
            so.ApplyModifiedPropertiesWithoutUndo();

            Assert.AreEqual(96000, clip.sampleRate);
            Assert.AreEqual(6, clip.channels);
            Assert.AreEqual(32, clip.bitsPerSample);
            Assert.AreEqual(120.5f, clip.length, 0.001f);
            Assert.AreEqual(11568000L, clip.totalFrames);
            Assert.AreEqual("sfx/explosion.wav", clip.AudioFilePath);

            Object.DestroyImmediate(clip);
        }

        [Test]
        public void UNAudioClip_CompressedData_SerializedProperty_SetArraySize()
        {
            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            var so = new SerializedObject(clip);

            var dataProp = so.FindProperty("compressedData");
            dataProp.arraySize = 256;
            so.ApplyModifiedPropertiesWithoutUndo();

            Assert.AreEqual(256, clip.GetMemorySize());

            Object.DestroyImmediate(clip);
        }

        // ═══════════════════════════════════════════════════════════
        //  UNAudioClip — asset persistence roundtrip
        // ═══════════════════════════════════════════════════════════

        [Test]
        public void UNAudioClip_AssetPersistence_SaveAndLoad()
        {
            // Ensure folder
            if (!AssetDatabase.IsValidFolder("Assets/TestClips"))
                AssetDatabase.CreateFolder("Assets", "TestClips");

            var clip = ScriptableObject.CreateInstance<UNAudioClip>();
            clip.SetMetadata(44100, 2, 16, 2.0f, 88200L, new byte[50]);
            clip.SetSourceType(AudioSourceType.Path);
            clip.SetAudioFilePath("test/audio.wav");

            string path = "Assets/TestClips/_TestPersistence.asset";
            AssetDatabase.CreateAsset(clip, path);
            AssetDatabase.SaveAssets();

            var loaded = AssetDatabase.LoadAssetAtPath<UNAudioClip>(path);
            Assert.IsNotNull(loaded);
            Assert.AreEqual(AudioSourceType.Path, loaded.SourceType);
            Assert.AreEqual("test/audio.wav", loaded.AudioFilePath);
            Assert.AreEqual(44100, loaded.sampleRate);
            Assert.AreEqual(2, loaded.channels);
            Assert.AreEqual(88200L, loaded.totalFrames);
            Assert.AreEqual(50, loaded.GetMemorySize());

            // Cleanup
            AssetDatabase.DeleteAsset(path);
        }
    }
}
