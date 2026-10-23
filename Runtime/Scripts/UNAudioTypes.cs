using System;

namespace UNAudio
{
    /// <summary>Result codes returned by the native engine.</summary>
    public enum UNAudioResult
    {
        Ok = 0,
        InvalidArgument = -1,
        NotInitialized = -2,
        AlreadyInitialized = -3,
        DeviceError = -4,
        OutOfMemory = -5,
        UnsupportedFormat = -6,
        InvalidHandle = -7,
        QueueFull = -8,
        NoFreeVoice = -9,
        NotReady = -10,
        IOError = -11,
        TooManyClips = -12,
        AbiMismatch = -13,
    }

    public enum UNAudioDeviceType
    {
        /// <summary>Platform backend: WASAPI, CoreAudio, AAudio/OpenSL ES (Oboe), ALSA.</summary>
        Default = 0,
        /// <summary>Real-time paced thread that discards output (CI, servers, batchmode).</summary>
        Null = 1,
        /// <summary>No audio thread; call <see cref="UNAudioEngine.Render"/> yourself (offline, tests).</summary>
        Manual = 2,
    }

    public enum UNAudioBackend
    {
        None = 0, Manual = 1, Null = 2, Wasapi = 3, CoreAudio = 4, AAudio = 5, OpenSLES = 6, Alsa = 7,
    }

    public enum UNAudioLoadMode
    {
        /// <summary>Decode the whole clip to PCM on a worker. Playable as soon as the first slice is decoded.</summary>
        Decode = 0,
        /// <summary>Keep the compressed data; each voice decodes ahead on the stream thread. For music.</summary>
        Stream = 1,
    }

    public enum UNAudioSampleFormat
    {
        /// <summary>16-bit PCM: half the memory of Float32, ~96 dB dynamic range.</summary>
        Int16 = 0,
        Float32 = 1,
    }

    public enum UNAudioClipState
    {
        Invalid = -1,
        /// <summary>Header not parsed yet.</summary>
        Loading = 0,
        /// <summary>Playable; decoding continues in the background.</summary>
        Ready = 1,
        /// <summary>Fully decoded (or stream prepared).</summary>
        Loaded = 2,
        Failed = 3,
    }

    public enum UNAudioVoiceState
    {
        /// <summary>Handle is stale or was never valid.</summary>
        Invalid = 0,
        /// <summary>Play was issued; the mixer has not seen it yet.</summary>
        Pending = 1,
        /// <summary>Waiting for its start frame (or for stream data).</summary>
        Scheduled = 2,
        Playing = 3,
        Paused = 4,
        Ended = 5,
    }

    /// <summary>Engine configuration. Zero means "let the engine choose".</summary>
    [Serializable]
    public struct UNAudioConfig
    {
        public UNAudioDeviceType deviceType;
        /// <summary>0 = device native rate (recommended: avoids a resampler in the OS mixer).</summary>
        public int sampleRate;
        /// <summary>0 = lowest period the device supports.</summary>
        public int bufferFrames;
        /// <summary>Request an exclusive / unshared stream (WASAPI exclusive, AAudio exclusive).</summary>
        public bool exclusive;
        /// <summary>0 = 128.</summary>
        public int maxVoices;
        /// <summary>0 = 1024.</summary>
        public int maxClips;
        /// <summary>0 = auto (1 or 2 decoder threads).</summary>
        public int decodeThreads;

        public static UNAudioConfig Default => default;
    }

    public readonly struct UNAudioDeviceInfo
    {
        public readonly int SampleRate;
        public readonly int Channels;
        /// <summary>Frames per device period.</summary>
        public readonly int BufferFrames;
        /// <summary>Output latency reported by the backend, in frames.</summary>
        public readonly int LatencyFrames;
        public readonly bool Exclusive;
        public readonly UNAudioBackend Backend;

        internal UNAudioDeviceInfo(in Native.una_device_info i)
        {
            SampleRate = i.sample_rate;
            Channels = i.channels;
            BufferFrames = i.buffer_frames;
            LatencyFrames = i.latency_frames;
            Exclusive = i.exclusive != 0;
            Backend = (UNAudioBackend)i.backend;
        }

        public double LatencySeconds => SampleRate > 0 ? (double)LatencyFrames / SampleRate : 0.0;
        public double BufferSeconds => SampleRate > 0 ? (double)BufferFrames / SampleRate : 0.0;

        public override string ToString() =>
            $"{Backend} {SampleRate} Hz {Channels} ch, period {BufferFrames} ({BufferSeconds * 1000.0:F2} ms), " +
            $"latency {LatencyFrames} ({LatencySeconds * 1000.0:F2} ms){(Exclusive ? ", exclusive" : "")}";
    }

    public readonly struct UNAudioStats
    {
        public readonly long Callbacks;
        public readonly long FramesRendered;
        /// <summary>Frames of silence inserted because decoding or streaming fell behind.</summary>
        public readonly long StarvedFrames;
        /// <summary>Commands dropped because the queue was full.</summary>
        public readonly long DroppedCommands;
        /// <summary>Commands applied after the dsp frame they asked for.</summary>
        public readonly long LateCommands;
        public readonly int ActiveVoices;
        public readonly int LiveClips;
        public readonly int DecodeJobs;
        public readonly int Streams;
        /// <summary>Render time / period time of the last callback (1 = no headroom).</summary>
        public readonly float CpuLoad;
        public readonly float CpuLoadPeak;
        public readonly float PeakLeft;
        public readonly float PeakRight;
        public readonly long ClipMemoryBytes;

        internal UNAudioStats(in Native.una_stats s)
        {
            Callbacks = s.callbacks;
            FramesRendered = s.frames_rendered;
            StarvedFrames = s.starved_frames;
            DroppedCommands = s.dropped_commands;
            LateCommands = s.late_commands;
            ActiveVoices = s.active_voices;
            LiveClips = s.live_clips;
            DecodeJobs = s.decode_jobs;
            Streams = s.streams;
            CpuLoad = s.cpu_load;
            CpuLoadPeak = s.cpu_load_peak;
            PeakLeft = s.peak_left;
            PeakRight = s.peak_right;
            ClipMemoryBytes = s.clip_memory_bytes;
        }
    }

    public sealed class UNAudioException : Exception
    {
        public UNAudioResult Result { get; }

        public UNAudioException(UNAudioResult result, string message) : base($"{message}: {result}")
        {
            Result = result;
        }

        internal static void ThrowIfFailed(int result, string what)
        {
            if (result < 0) throw new UNAudioException((UNAudioResult)result, what);
        }
    }
}
