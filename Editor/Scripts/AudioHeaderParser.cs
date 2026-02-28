#if UNITY_EDITOR
using System;

namespace UNAudio.Editor
{
    /// <summary>
    /// Metadata extracted from an audio file header.
    /// </summary>
    public struct AudioMetadata
    {
        public int sampleRate;
        public int channels;
        public int bitsPerSample;
        public float lengthInSeconds;
        public long totalFrames;
    }

    /// <summary>
    /// Pure C# audio header parser for extracting metadata at import time
    /// without requiring the native engine.
    /// </summary>
    public static class AudioHeaderParser
    {
        /// <summary>
        /// Try all supported formats and return metadata for the first match.
        /// </summary>
        public static bool TryParse(byte[] data, out AudioMetadata meta)
        {
            if (TryParseWav(data, out meta)) return true;
            if (TryParseOggVorbis(data, out meta)) return true;
            return false;
        }

        /// <summary>
        /// Attempt to parse a WAV/RIFF header from raw file bytes.
        /// </summary>
        public static bool TryParseWav(byte[] data, out AudioMetadata meta)
        {
            meta = default;

            if (data == null || data.Length < 44)
                return false;

            // Verify RIFF header
            if (data[0] != 'R' || data[1] != 'I' || data[2] != 'F' || data[3] != 'F')
                return false;

            // Verify WAVE format
            if (data[8] != 'W' || data[9] != 'A' || data[10] != 'V' || data[11] != 'E')
                return false;

            // Scan for chunks — fmt and data can appear in any order
            int fmtOffset = -1;
            int dataOffset = -1;
            int dataSize = 0;

            int pos = 12;
            while (pos + 8 <= data.Length)
            {
                string chunkId = "" + (char)data[pos] + (char)data[pos + 1]
                                    + (char)data[pos + 2] + (char)data[pos + 3];
                int chunkSize = BitConverter.ToInt32(data, pos + 4);

                if (chunkId == "fmt ")
                {
                    fmtOffset = pos + 8;
                }
                else if (chunkId == "data")
                {
                    dataOffset = pos + 8;
                    dataSize = chunkSize;
                }

                if (fmtOffset >= 0 && dataOffset >= 0)
                    break;

                // Advance to next chunk (chunk header is 8 bytes + chunkSize, word-aligned)
                pos += 8 + chunkSize;
                if (chunkSize % 2 != 0) pos++; // WAV chunks are word-aligned
            }

            if (fmtOffset < 0 || dataOffset < 0)
                return false;

            // Parse fmt chunk (minimum 16 bytes)
            if (fmtOffset + 16 > data.Length)
                return false;

            int audioFormat = BitConverter.ToInt16(data, fmtOffset);
            // Accept PCM (1) and IEEE Float (3); extensible (0xFFFE) also OK for basic metadata
            if (audioFormat != 1 && audioFormat != 3 && audioFormat != -2) // -2 is 0xFFFE as short
                return false;

            meta.channels = BitConverter.ToInt16(data, fmtOffset + 2);
            meta.sampleRate = BitConverter.ToInt32(data, fmtOffset + 4);
            // fmtOffset + 8 = byteRate (4 bytes), fmtOffset + 12 = blockAlign (2 bytes)
            meta.bitsPerSample = BitConverter.ToInt16(data, fmtOffset + 14);

            if (meta.sampleRate <= 0 || meta.channels <= 0 || meta.bitsPerSample <= 0)
                return false;

            int blockAlign = meta.channels * (meta.bitsPerSample / 8);
            if (blockAlign > 0)
            {
                meta.totalFrames = dataSize / blockAlign;
                meta.lengthInSeconds = (float)meta.totalFrames / meta.sampleRate;
            }

            return true;
        }

        /// <summary>
        /// Attempt to parse an OGG Vorbis identification header from raw file bytes.
        /// Extracts sample rate, channels and total duration (if available).
        /// </summary>
        public static bool TryParseOggVorbis(byte[] data, out AudioMetadata meta)
        {
            meta = default;

            if (data == null || data.Length < 30)
                return false;

            // Verify OGG capture pattern
            if (data[0] != 'O' || data[1] != 'g' || data[2] != 'g' || data[3] != 'S')
                return false;

            // Find the Vorbis identification header (packet type 1 + "vorbis")
            int vorbisOffset = -1;
            for (int i = 0; i <= data.Length - 7; i++)
            {
                if (data[i] == 0x01 &&
                    data[i + 1] == 'v' && data[i + 2] == 'o' && data[i + 3] == 'r' &&
                    data[i + 4] == 'b' && data[i + 5] == 'i' && data[i + 6] == 's')
                {
                    vorbisOffset = i + 7; // skip packet type + "vorbis"
                    break;
                }
            }

            if (vorbisOffset < 0 || vorbisOffset + 11 > data.Length)
                return false;

            // Vorbis identification header layout (after "\x01vorbis"):
            //   0..3  vorbis_version  (uint32, must be 0)
            //   4     audio_channels  (uint8)
            //   5..8  audio_sample_rate (uint32 LE)
            //   9..12 bitrate_maximum
            //  13..16 bitrate_nominal
            //  17..20 bitrate_minimum

            int version = BitConverter.ToInt32(data, vorbisOffset);
            if (version != 0)
                return false;

            meta.channels = data[vorbisOffset + 4];
            meta.sampleRate = BitConverter.ToInt32(data, vorbisOffset + 5);

            if (meta.sampleRate <= 0 || meta.channels <= 0)
                return false;

            // Vorbis is always decoded to float, report as 16-bit for user-facing display
            meta.bitsPerSample = 16;

            // Try to determine total duration from the last OGG page's granule position.
            // Scan backwards from end of file to find the last page with "OggS" capture.
            long lastGranule = -1;
            for (int i = data.Length - 14; i >= 0; i--)
            {
                if (data[i] == 'O' && data[i + 1] == 'g' && data[i + 2] == 'g' && data[i + 3] == 'S')
                {
                    // Granule position is at offset 6 within the OGG page header (8 bytes, int64 LE)
                    if (i + 14 <= data.Length)
                    {
                        lastGranule = BitConverter.ToInt64(data, i + 6);
                        if (lastGranule >= 0)
                            break;
                    }
                }
            }

            if (lastGranule > 0)
            {
                meta.totalFrames = lastGranule;
                meta.lengthInSeconds = (float)lastGranule / meta.sampleRate;
            }

            return true;
        }
    }
}
#endif
