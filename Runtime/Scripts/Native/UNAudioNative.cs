using System;
using System.Runtime.InteropServices;

namespace UNAudio.Native
{
    // Blittable mirrors of Native/Include/unaudio.h. Field order and types must
    // match exactly; every struct carries struct_size where the C side checks it.

    [StructLayout(LayoutKind.Sequential)]
    internal struct una_config
    {
        public int struct_size;
        public int device_type;
        public int sample_rate;
        public int buffer_frames;
        public int exclusive;
        public int max_voices;
        public int max_clips;
        public int decode_threads;
        public int channels;
        public int reserved0, reserved1, reserved2, reserved3, reserved4, reserved5, reserved6;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct una_device_info
    {
        public int sample_rate;
        public int channels;
        public int buffer_frames;
        public int latency_frames;
        public int exclusive;
        public int backend;
        public int reserved0, reserved1;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct una_clock
    {
        public long host_ns;
        public long mix_frame;
        public double audible_frame;
        public double frames_per_second;
        public int sample_rate;
        public int buffer_frames;
        public long anchor_frame;
        public long anchor_host_ns;
        public long callbacks;
        public long discontinuities;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct una_clip_params
    {
        public int struct_size;
        public int mode;
        public int sample_format;
        public int flags;
        public int priority;
        public int reserved0, reserved1, reserved2;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct una_clip_info
    {
        public int state;
        public int error;
        public int channels;
        public int source_channels;
        public int source_rate;
        public int stored_rate;
        public int mode;
        public int sample_format;
        public long total_frames;
        public long decoded_frames;
        public long memory_bytes;
        public double length_seconds;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct una_play_params
    {
        public int struct_size;
        public float volume;
        public float pan;
        public float pitch;
        public int loop;
        public int bus;
        public int flags;
        public float fade_in_seconds;
        public long start_dsp_frame;
        public long start_frame;
        public long loop_start;
        public long loop_end;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct una_voice_timing
    {
        public int state;
        public int stored_rate;
        public double audible_frame;
        public double audible_seconds;
        public double mix_frame;
        public long start_dsp_frame;
        public long host_ns;
        public long seg_dsp_frame;
        public double seg_source_frame;
        public double seg_rate;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct una_event
    {
        public int type;
        public uint handle;
        public long dsp_frame;
        public long value;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct una_stats
    {
        public long callbacks;
        public long frames_rendered;
        public long starved_frames;
        public long dropped_commands;
        public long late_commands;
        public int active_voices;
        public int live_clips;
        public int decode_jobs;
        public int streams;
        public float cpu_load;
        public float cpu_load_peak;
        public float peak_left;
        public float peak_right;
        public long clip_memory_bytes;
    }

    internal static unsafe class UNAudioNative
    {
#if (UNITY_IOS || UNITY_TVOS || UNITY_VISIONOS) && !UNITY_EDITOR
        // Statically linked into the player (see Native~/CMakeLists.txt).
        private const string Lib = "__Internal";
#else
        private const string Lib = "UNAudio";
#endif
        public const int ApiVersion = 2;

        public const int CLIP_FLAG_BORROW = 0x1;
        public const int CLIP_FLAG_NO_RESAMPLE = 0x2;
        public const int PLAY_PAUSED = 0x1;
        public const int PLAY_CATCH_UP = 0x2;
        public const int MAX_BUSES = 16;

        [DllImport(Lib)] public static extern int una_get_api_version();
        [DllImport(Lib)] public static extern int una_init(una_config* config);
        [DllImport(Lib)] public static extern void una_shutdown();
        [DllImport(Lib)] public static extern int una_is_initialized();
        [DllImport(Lib)] public static extern int una_get_device_info(una_device_info* info);
        [DllImport(Lib)] public static extern void una_update();
        [DllImport(Lib)] public static extern int una_set_suspended(int suspended);
        [DllImport(Lib)] public static extern int una_is_suspended();
        [DllImport(Lib)] public static extern int una_render(float* output, int frames, int channels);

        [DllImport(Lib)] public static extern long una_host_time_ns();
        [DllImport(Lib)] public static extern int una_get_clock(una_clock* clock);
        [DllImport(Lib)] public static extern double una_audible_frame_at(long hostNs);

        [DllImport(Lib)] public static extern int una_clip_load_memory(void* data, long size, una_clip_params* p, uint* clip);
        [DllImport(Lib)] public static extern int una_clip_load_file(byte* utf8Path, una_clip_params* p, uint* clip);
        [DllImport(Lib)] public static extern int una_clip_release(uint clip);
        [DllImport(Lib)] public static extern int una_clip_get_info(uint clip, una_clip_info* info);
        [DllImport(Lib)] public static extern int una_clip_set_priority(uint clip, int priority);
        [DllImport(Lib)] public static extern int una_probe(void* data, long size, una_clip_info* info);

        [DllImport(Lib)] public static extern int una_voice_play(uint clip, una_play_params* p, uint* voice);
        [DllImport(Lib)] public static extern int una_voice_stop(uint voice, long atDspFrame, float fadeSeconds);
        [DllImport(Lib)] public static extern int una_voice_pause(uint voice, long atDspFrame);
        [DllImport(Lib)] public static extern int una_voice_resume(uint voice, long atDspFrame);
        [DllImport(Lib)] public static extern int una_voice_seek(uint voice, long sourceFrame, long atDspFrame);
        [DllImport(Lib)] public static extern int una_voice_set_volume(uint voice, float volume, float rampSeconds, long atDspFrame);
        [DllImport(Lib)] public static extern int una_voice_set_pan(uint voice, float pan, long atDspFrame);
        [DllImport(Lib)] public static extern int una_voice_set_pitch(uint voice, float pitch, long atDspFrame);
        [DllImport(Lib)] public static extern int una_voice_set_loop(uint voice, int loop, long loopStart, long loopEnd);
        [DllImport(Lib)] public static extern int una_voice_get_state(uint voice);
        [DllImport(Lib)] public static extern int una_voice_get_timing(uint voice, long hostNs, una_voice_timing* timing);
        [DllImport(Lib)] public static extern int una_stop_all(float fadeSeconds);

        [DllImport(Lib)] public static extern int una_set_master_volume(float volume, float rampSeconds);
        [DllImport(Lib)] public static extern float una_get_master_volume();
        [DllImport(Lib)] public static extern int una_bus_set_volume(int bus, float volume, float rampSeconds);
        [DllImport(Lib)] public static extern int una_bus_set_paused(int bus, int paused, long atDspFrame);

        [DllImport(Lib)] public static extern int una_poll_events(una_event* events, int max);
        [DllImport(Lib)] public static extern int una_get_stats(una_stats* stats);
    }
}
