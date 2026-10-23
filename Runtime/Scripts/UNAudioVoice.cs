using System;
using UNAudio.Native;

namespace UNAudio
{
    /// <summary>
    /// How to start a voice. Start from <see cref="Default"/> (or <see cref="At"/>) and set what
    /// you need: <c>default</c> / <c>new UNAudioPlayParams()</c> has volume 0 and plays silently.
    /// </summary>
    [Serializable]
    public struct UNAudioPlayParams
    {
        /// <summary>Linear gain; 0 is silent (useful with a later fade). <see cref="Default"/> sets 1.</summary>
        public float volume;
        /// <summary>-1 (left) .. 1 (right).</summary>
        public float pan;
        /// <summary>Playback rate multiplier (changes pitch and speed together).</summary>
        public float pitch;
        public bool loop;
        /// <summary>0 .. 15.</summary>
        public int bus;
        /// <summary>Create the voice paused at its start position.</summary>
        public bool startPaused;
        /// <summary>
        /// If <see cref="startDspFrame"/> is already in the past when the mixer sees the
        /// command, skip the missed frames so the voice stays aligned to the timeline.
        /// </summary>
        public bool catchUp;
        public float fadeInSeconds;
        /// <summary>Dsp frame the first sample is mixed at (sample exact). 0 = as soon as possible.</summary>
        public long startDspFrame;
        /// <summary>Source frame to start from (clip's stored rate).</summary>
        public long startFrame;
        /// <summary>Loop region in source frames; <see cref="loopEnd"/> 0 means the clip end.</summary>
        public long loopStart;
        public long loopEnd;

        public static UNAudioPlayParams Default => new UNAudioPlayParams { volume = 1f, pitch = 1f };

        /// <summary>Default params scheduled at an exact dsp frame.</summary>
        public static UNAudioPlayParams At(long dspFrame)
        {
            var p = Default;
            p.startDspFrame = dspFrame;
            return p;
        }

        internal una_play_params ToNative()
        {
            int flags = 0;
            if (startPaused) flags |= UNAudioNative.PLAY_PAUSED;
            if (catchUp) flags |= UNAudioNative.PLAY_CATCH_UP;
            return new una_play_params
            {
                struct_size = System.Runtime.InteropServices.Marshal.SizeOf<una_play_params>(),
                volume = volume,
                pan = pan,
                pitch = pitch,
                loop = loop ? 1 : 0,
                bus = bus,
                flags = flags,
                fade_in_seconds = fadeInSeconds,
                start_dsp_frame = startDspFrame,
                start_frame = startFrame,
                loop_start = loopStart,
                loop_end = loopEnd,
            };
        }
    }

    /// <summary>Snapshot of where a voice is on its timeline.</summary>
    public readonly struct UNAudioVoiceTiming
    {
        public readonly UNAudioVoiceState State;
        /// <summary>Source frame reaching the speaker at <see cref="HostNs"/>. Negative before a scheduled start.</summary>
        public readonly double AudibleFrame;
        /// <summary><see cref="AudibleFrame"/> in seconds of clip time.</summary>
        public readonly double AudibleSeconds;
        /// <summary>Source frame at the mixer's write head (ahead of what is heard by the output latency).</summary>
        public readonly double MixFrame;
        /// <summary>Dsp frame the voice started (or will start) at.</summary>
        public readonly long StartDspFrame;
        public readonly long HostNs;
        /// <summary>Source frames advanced per dsp frame right now (0 while paused).</summary>
        public readonly double Rate;
        public readonly int StoredRate;

        internal UNAudioVoiceTiming(in una_voice_timing t)
        {
            State = (UNAudioVoiceState)t.state;
            AudibleFrame = t.audible_frame;
            AudibleSeconds = t.audible_seconds;
            MixFrame = t.mix_frame;
            StartDspFrame = t.start_dsp_frame;
            HostNs = t.host_ns;
            Rate = t.seg_rate;
            StoredRate = t.stored_rate;
        }
    }

    /// <summary>
    /// A playing (or scheduled) instance of a clip. A lightweight handle: copying it is
    /// free, and calls on a voice that already ended are harmless no-ops.
    /// Every control takes an optional dsp frame so changes can land sample-exactly.
    /// </summary>
    public readonly unsafe struct UNAudioVoice : IEquatable<UNAudioVoice>
    {
        public readonly uint Handle;
        /// <summary>Engine session the voice was started in; a voice from an older session is dead.</summary>
        public readonly int Session;

        internal UNAudioVoice(uint handle)
        {
            Handle = handle;
            Session = UNAudioEngine.Session;
        }

        public static UNAudioVoice None => default;

        // A handle from an earlier session can collide with a live voice of the current one.
        bool Live => Handle != 0 && UNAudioEngine.IsInitialized && Session == UNAudioEngine.Session;

        public bool IsValid => State != UNAudioVoiceState.Invalid;

        public UNAudioVoiceState State =>
            Live ? (UNAudioVoiceState)UNAudioNative.una_voice_get_state(Handle) : UNAudioVoiceState.Invalid;

        /// <summary>Scheduled, playing or paused (not yet ended).</summary>
        public bool IsAlive
        {
            get
            {
                var s = State;
                return s == UNAudioVoiceState.Pending || s == UNAudioVoiceState.Scheduled ||
                       s == UNAudioVoiceState.Playing || s == UNAudioVoiceState.Paused;
            }
        }

        public bool IsPlaying => State == UNAudioVoiceState.Playing;

        /// <summary>
        /// Why the voice stopped early, or <see cref="UNAudioResult.Ok"/>. Set for streaming voices
        /// that could not decode, from the event dispatch after the failure (same frame as
        /// <see cref="UNAudioEngine.VoiceError"/>). Main thread only.
        /// </summary>
        public UNAudioResult Error =>
            Handle != 0 && Session == UNAudioEngine.Session ? UNAudioEngine.VoiceErrorOf(Handle) : UNAudioResult.Ok;

        /// <summary>Clip time (seconds) reaching the speaker right now. Smooth and monotonic while playing.</summary>
        public double Time => GetTiming().AudibleSeconds;

        /// <summary>Clip frame reaching the speaker right now.</summary>
        public double Frame => GetTiming().AudibleFrame;

        /// <summary>Timing at a host time (0 = now). Use <see cref="UNAudioClock.FrameHostNs"/> for a frame-stable value.</summary>
        public UNAudioVoiceTiming GetTiming(long hostNs = 0)
        {
            una_voice_timing t;
            if (!Live || UNAudioNative.una_voice_get_timing(Handle, hostNs, &t) < 0)
                return default;
            return new UNAudioVoiceTiming(t);
        }

        /// <summary>Stops at a dsp frame (0 = now), optionally fading out over <paramref name="fadeSeconds"/>.</summary>
        public void Stop(float fadeSeconds = 0f, long atDspFrame = 0)
        {
            if (Live) Call(UNAudioNative.una_voice_stop(Handle, atDspFrame, fadeSeconds));
        }

        public void Pause(long atDspFrame = 0)
        {
            if (Live) Call(UNAudioNative.una_voice_pause(Handle, atDspFrame));
        }

        public void Resume(long atDspFrame = 0)
        {
            if (Live) Call(UNAudioNative.una_voice_resume(Handle, atDspFrame));
        }

        /// <summary>Jumps to a source frame (sample exact for decoded clips).</summary>
        public void SeekFrame(long sourceFrame, long atDspFrame = 0)
        {
            if (Live) Call(UNAudioNative.una_voice_seek(Handle, sourceFrame, atDspFrame));
        }

        public void Seek(double seconds, long atDspFrame = 0)
        {
            int rate = GetTiming().StoredRate;
            if (rate > 0) SeekFrame((long)Math.Round(seconds * rate), atDspFrame);
        }

        public void SetVolume(float volume, float rampSeconds = 0f, long atDspFrame = 0)
        {
            if (Live) Call(UNAudioNative.una_voice_set_volume(Handle, volume, rampSeconds, atDspFrame));
        }

        public void SetPan(float pan, long atDspFrame = 0)
        {
            if (Live) Call(UNAudioNative.una_voice_set_pan(Handle, pan, atDspFrame));
        }

        public void SetPitch(float pitch, long atDspFrame = 0)
        {
            if (Live) Call(UNAudioNative.una_voice_set_pitch(Handle, pitch, atDspFrame));
        }

        /// <summary>Changes looping; <paramref name="loopEnd"/> 0 means the clip end.</summary>
        public void SetLoop(bool loop, long loopStart = 0, long loopEnd = 0)
        {
            if (Live) Call(UNAudioNative.una_voice_set_loop(Handle, loop ? 1 : 0, loopStart, loopEnd));
        }

        void Call(int result)
        {
            // Stale handles are expected (voice ended); anything else is a programming error.
            if (result < 0 && result != (int)UNAudioResult.InvalidHandle && result != (int)UNAudioResult.NotInitialized)
                throw new UNAudioException((UNAudioResult)result, "Voice command failed");
        }

        public bool Equals(UNAudioVoice other) => Handle == other.Handle && Session == other.Session;
        public override bool Equals(object obj) => obj is UNAudioVoice v && Equals(v);
        public override int GetHashCode() => (int)Handle ^ (Session << 24);
        public static bool operator ==(UNAudioVoice a, UNAudioVoice b) => a.Equals(b);
        public static bool operator !=(UNAudioVoice a, UNAudioVoice b) => !a.Equals(b);
        public override string ToString() => $"UNAudioVoice(0x{Handle:X8})";
    }
}
