using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace UNAudio
{
    /// <summary>
    /// P/Invoke bridge to the UNAudio native library.
    /// All native calls are routed through this static class.
    /// </summary>
    public static class UNAudioBridge
    {
        private const string LibName = "UNAudio";

        // ── Engine lifecycle ─────────────────────────────────────

        [DllImport(LibName)] public static extern int Initialize(UNAudioOutputConfig config);
        [DllImport(LibName, EntryPoint = "UNAudio_Shutdown")]
        public static extern void Shutdown();
        [DllImport(LibName, EntryPoint = "UNAudio_IsInitialized")]
        public static extern int IsInitialized();

        // ── Audio loading ────────────────────────────────────────

        [DllImport(LibName, EntryPoint = "UNAudio_LoadAudio")]
        private static extern int UNAudio_LoadAudio(byte[] data, int size, int compressionMode);

        public static int LoadAudio(byte[] data, int compressionMode)
        {
            if (data == null || data.Length == 0) return -1;
            return UNAudio_LoadAudio(data, data.Length, compressionMode);
        }

        [DllImport(LibName, EntryPoint = "UNAudio_UnloadAudio")]
        public static extern void UnloadAudio(int handle);

        // ── Playback ─────────────────────────────────────────────

        [DllImport(LibName, EntryPoint = "UNAudio_Play")]
        public static extern int Play(int handle);
        [DllImport(LibName, EntryPoint = "UNAudio_Pause")]
        public static extern int Pause(int handle);
        [DllImport(LibName, EntryPoint = "UNAudio_Stop")]
        public static extern int Stop(int handle);

        // ── Properties ───────────────────────────────────────────

        [DllImport(LibName, EntryPoint = "UNAudio_SetVolume")]
        public static extern void SetVolume(int handle, float volume);
        [DllImport(LibName, EntryPoint = "UNAudio_GetVolume")]
        public static extern float GetVolume(int handle);
        [DllImport(LibName, EntryPoint = "UNAudio_SetLoop")]
        public static extern void SetLoop(int handle, bool loop);
        [DllImport(LibName, EntryPoint = "UNAudio_GetState")]
        public static extern int GetState(int handle);

        // ── Engine-level ─────────────────────────────────────────

        [DllImport(LibName, EntryPoint = "UNAudio_SetMasterVolume")]
        public static extern void SetMasterVolume(float volume);
        [DllImport(LibName, EntryPoint = "UNAudio_GetMasterVolume")]
        public static extern float GetMasterVolume();
        [DllImport(LibName, EntryPoint = "UNAudio_SetBufferSize")]
        public static extern void SetBufferSize(int frames);
        [DllImport(LibName, EntryPoint = "UNAudio_GetCurrentLatency")]
        public static extern float GetCurrentLatency();
    }

    /// <summary>
    /// Output configuration passed to the native engine.
    /// Must match the C struct UNAudioOutputConfig layout.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct UNAudioOutputConfig
    {
        public int sampleRate;
        public int channels;
        public int bufferSize;
        public int bufferCount;
        public int exclusiveMode;
    }
}
