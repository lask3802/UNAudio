using UNAudio.Native;
using UnityEngine;

namespace UNAudio
{
    /// <summary>
    /// The output timeline. A "dsp frame" is a frame index on the mixer timeline at
    /// <see cref="SampleRate"/>; scheduling is expressed in dsp frames and is sample exact.
    /// "Audible" values describe what is reaching the speaker (mix head minus output
    /// latency), smoothed by a delay-locked loop so they advance continuously between
    /// audio callbacks instead of jumping once per buffer.
    ///
    /// Live properties query the native clock on every call. The <c>Frame*</c> values are
    /// a snapshot taken at the start of the current frame (EarlyUpdate), so all gameplay
    /// code in one frame can agree on a single time.
    /// </summary>
    public static unsafe class UNAudioClock
    {
        static una_clock s_Frame;
        static double s_RealtimeOffsetNs;
        static bool s_HasOffset;

        /// <summary>Monotonic host clock in nanoseconds (QPC / mach_absolute_time / CLOCK_MONOTONIC).</summary>
        public static long HostTimeNs => UNAudioNative.una_host_time_ns();

        public static int SampleRate => UNAudioEngine.Device.SampleRate;

        /// <summary>Next dsp frame the mixer will produce.</summary>
        public static long MixFrame
        {
            get
            {
                UNAudioEngine.EnsureInitialized();
                una_clock c;
                UNAudioNative.una_get_clock(&c);
                return c.mix_frame;
            }
        }

        /// <summary>The dsp frame reaching the speaker right now (fractional).</summary>
        public static double AudibleFrame
        {
            get
            {
                UNAudioEngine.EnsureInitialized();
                una_clock c;
                UNAudioNative.una_get_clock(&c);
                return c.audible_frame;
            }
        }

        /// <summary><see cref="AudibleFrame"/> in seconds.</summary>
        public static double AudibleTime => AudibleFrame / SampleRate;

        /// <summary>Measured output latency: mix head minus audible frame, in seconds.</summary>
        public static double OutputLatency
        {
            get
            {
                UNAudioEngine.EnsureInitialized();
                una_clock c;
                UNAudioNative.una_get_clock(&c);
                return c.sample_rate > 0 ? (c.mix_frame - c.audible_frame) / c.sample_rate : 0.0;
            }
        }

        /// <summary>Device frames per second of host time as measured by the clock DLL (drift-aware).</summary>
        public static double MeasuredSampleRate => FrameSnapshot.frames_per_second;

        /// <summary>Host time captured at the start of this frame.</summary>
        public static long FrameHostNs => FrameSnapshot.host_ns;

        /// <summary>Audible dsp frame at the start of this frame.</summary>
        public static double FrameAudibleFrame => FrameSnapshot.audible_frame;

        /// <summary>Audible time (seconds) at the start of this frame.</summary>
        public static double FrameAudibleTime
        {
            get
            {
                ref var f = ref FrameSnapshot;
                return f.sample_rate > 0 ? f.audible_frame / f.sample_rate : 0.0;
            }
        }

        /// <summary>Mix head at the start of this frame.</summary>
        public static long FrameMixFrame => FrameSnapshot.mix_frame;

        static ref una_clock FrameSnapshot
        {
            get
            {
                UNAudioEngine.EnsureInitialized();
                return ref s_Frame;
            }
        }

        /// <summary>Audible dsp frame at any host time (past, or shortly ahead).</summary>
        public static double AudibleFrameAt(long hostNs)
        {
            UNAudioEngine.EnsureInitialized();
            return UNAudioNative.una_audible_frame_at(hostNs);
        }

        /// <summary>Host time at which a dsp frame is (or was) heard.</summary>
        public static long HostNsOfFrame(double dspFrame)
        {
            UNAudioEngine.EnsureInitialized();
            una_clock c;
            UNAudioNative.una_get_clock(&c);
            if (c.frames_per_second <= 0.0) return c.host_ns;
            return c.anchor_host_ns + (long)((dspFrame - c.anchor_frame) * 1e9 / c.frames_per_second);
        }

        /// <summary>
        /// Earliest dsp frame that can be scheduled without being late: the mix head plus
        /// two device periods (command latency) and a small safety margin.
        /// </summary>
        public static long EarliestScheduleFrame(double safetySeconds = 0.005)
        {
            var dev = UNAudioEngine.Device;
            una_clock c;
            UNAudioNative.una_get_clock(&c);
            return c.mix_frame + 2L * dev.BufferFrames + SecondsToFrames(safetySeconds);
        }

        /// <summary>
        /// The dsp frame that will be heard <paramref name="secondsFromNow"/> after "now",
        /// i.e. what to pass as a start frame to make a sound reach the speaker then.
        /// </summary>
        public static long DspFrameHeardIn(double secondsFromNow) =>
            (long)System.Math.Round(AudibleFrameAt(HostTimeNs + (long)(secondsFromNow * 1e9)));

        public static long SecondsToFrames(double seconds) => (long)System.Math.Round(seconds * SampleRate);
        public static double FramesToSeconds(double frames) => frames / SampleRate;

        /// <summary>Converts <see cref="Time.realtimeSinceStartupAsDouble"/> to host ns.</summary>
        public static long RealtimeToHostNs(double realtimeSinceStartup) =>
            (long)(realtimeSinceStartup * 1e9 + s_RealtimeOffsetNs);

        /// <summary>Converts host ns to the <see cref="Time.realtimeSinceStartupAsDouble"/> timeline.</summary>
        public static double HostNsToRealtime(long hostNs) => (hostNs - s_RealtimeOffsetNs) * 1e-9;

        internal static void Refresh()
        {
            if (!UNAudioEngine.IsInitialized) return;
            una_clock c;
            UNAudioNative.una_get_clock(&c);
            s_Frame = c;

            // Both clocks are monotonic; their offset only wanders by OS timer drift.
            // Bracket the realtime read with host reads, drop pairings stretched by a
            // preemption, and smooth the rest.
            long h0 = UNAudioNative.una_host_time_ns();
            double rt = Time.realtimeSinceStartupAsDouble;
            long h1 = UNAudioNative.una_host_time_ns();
            if (s_HasOffset && h1 - h0 > 200_000) return;
            double offset = (h0 + h1) * 0.5 - rt * 1e9;
            s_RealtimeOffsetNs = s_HasOffset ? s_RealtimeOffsetNs + (offset - s_RealtimeOffsetNs) * 0.05 : offset;
            s_HasOffset = true;
        }

        /// <summary>
        /// A new session may use another host clock (the Manual device synthesises it from
        /// rendered frames), so the realtime offset starts over.
        /// </summary>
        internal static void OnSessionStart()
        {
            s_HasOffset = false;
            Refresh();
        }
    }
}
