using System.Collections.Generic;
using UnityEngine;

namespace UNAudio.Samples
{
    /// <summary>
    /// Music-game timing: start the song on an exact dsp frame, schedule hit sounds
    /// sample-exactly on the beat, and judge input against the audible song position.
    /// Press Space (IMGUI key event, works with either input backend) on the beat.
    /// </summary>
    public class RhythmTimingExample : MonoBehaviour
    {
        public UNAudioAsset song;          // import as Stream for long music
        public UNAudioAsset hitSound;      // import as Decode (Int16)
        public double bpm = 120.0;
        [Tooltip("Song time of beat 0, in seconds")]
        public double firstBeat = 0.5;
        [Tooltip("Player calibration: positive if the player hears audio late")]
        public double inputOffsetSeconds;

        UNAudioVoice m_Song;
        long m_SongStartDsp;
        int m_NextScheduledBeat;
        double m_SongTime;
        readonly List<string> m_Log = new List<string>();

        double BeatLength => 60.0 / bpm;

        async void Start()
        {
            await song.LoadAsync();
            await hitSound.LoadAsync();

            // Start 0.2 s from now, on a known dsp frame. Everything else is relative to it.
            m_SongStartDsp = UNAudioClock.EarliestScheduleFrame() + UNAudioClock.SecondsToFrames(0.2);
            m_Song = song.Clip.PlayScheduled(m_SongStartDsp);
        }

        void Update()
        {
            if (!m_Song.IsAlive) return;

            // 1) Song position the player hears at the start of this frame: smooth, monotonic,
            //    output-latency compensated, and identical for every script in the frame.
            m_SongTime = m_Song.GetTiming(UNAudioClock.FrameHostNs).AudibleSeconds;

            // 2) Schedule metronome hits a little ahead, each on its exact sample.
            //    Song time t is mixed at start + t * rate (pitch 1; clips are stored at the device rate).
            int rate = UNAudioClock.SampleRate;
            long earliest = UNAudioClock.EarliestScheduleFrame();
            long horizon = earliest + UNAudioClock.SecondsToFrames(0.25);
            while (true)
            {
                double beatSongTime = firstBeat + m_NextScheduledBeat * BeatLength;
                long dsp = m_SongStartDsp + (long)System.Math.Round(beatSongTime * rate);
                if (dsp > horizon) break;
                if (dsp >= earliest) hitSound.Clip.PlayScheduled(dsp);  // too late ones are skipped
                m_NextScheduledBeat++;
            }
        }

        // 3) Judge a key press against what is audible at that moment. With the Input System
        //    package, use the event's timestamp instead: UNAudioClock.RealtimeToHostNs(ctx.time)
        //    and m_Song.GetTiming(hostNs) give the song time at the physical key press.
        void Judge()
        {
            double t = m_Song.GetTiming().AudibleSeconds - inputOffsetSeconds;
            double beat = (t - firstBeat) / BeatLength;
            double error = (beat - System.Math.Round(beat)) * BeatLength;
            string grade = System.Math.Abs(error) < 0.025 ? "PERFECT" : System.Math.Abs(error) < 0.06 ? "GOOD" : "MISS";
            m_Log.Insert(0, $"{grade} {error * 1000.0:+0.0;-0.0} ms");
            if (m_Log.Count > 10) m_Log.RemoveAt(10);
        }

        void OnGUI()
        {
            if (!m_Song.IsAlive) return;
            if (Event.current.type == EventType.KeyDown && Event.current.keyCode == KeyCode.Space) Judge();
            GUILayout.Label($"Song {m_SongTime:F4} s   output latency {UNAudioClock.OutputLatency * 1000.0:F1} ms");
            foreach (var line in m_Log) GUILayout.Label(line);
        }
    }
}
