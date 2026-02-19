using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace UNAudio
{
    /// <summary>
    /// Low-latency native audio engine for Unity
    /// Supports Android (AAudio) and iOS (CoreAudio)
    /// </summary>
    public static class UNAudioEngine
    {
        #region Native Imports

#if UNITY_IOS && !UNITY_EDITOR
        private const string PLUGIN_NAME = "__Internal";
#elif UNITY_ANDROID && !UNITY_EDITOR
        private const string PLUGIN_NAME = "UNAudio";
#else
        private const string PLUGIN_NAME = "UNAudio";
#endif

        [DllImport(PLUGIN_NAME)]
        private static extern int UNAudio_Initialize(int sampleRate, int channels, int bufferSize);

        [DllImport(PLUGIN_NAME)]
        private static extern int UNAudio_Shutdown();

        [DllImport(PLUGIN_NAME)]
        private static extern int UNAudio_Start();

        [DllImport(PLUGIN_NAME)]
        private static extern int UNAudio_Stop();

        [DllImport(PLUGIN_NAME)]
        private static extern int UNAudio_SetVolume(float volume);

        [DllImport(PLUGIN_NAME)]
        private static extern float UNAudio_GetVolume();

        [DllImport(PLUGIN_NAME)]
        private static extern int UNAudio_GetLatency();

        [DllImport(PLUGIN_NAME)]
        private static extern int UNAudio_GetDeviceInfo(ref DeviceInfo info);

        [DllImport(PLUGIN_NAME)]
        private static extern int UNAudio_SetCallback(AudioCallbackDelegate callback, IntPtr userData);

        #endregion

        #region Types

        public enum Result
        {
            Success = 0,
            InvalidParameter = -1,
            NotInitialized = -2,
            DeviceNotFound = -3,
            AlreadyInitialized = -4,
            PlatformError = -5
        }

        public enum AudioFormat
        {
            PCM16 = 0,
            PCM32 = 1,
            Float = 2
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct DeviceInfo
        {
            public int sampleRate;
            public int channels;
            public int bufferSize;
            public AudioFormat format;
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void AudioCallbackDelegate(IntPtr buffer, int frames, int channels, IntPtr userData);

        #endregion

        #region Public API

        /// <summary>
        /// Initialize the audio engine
        /// </summary>
        /// <param name="sampleRate">Sample rate in Hz (e.g., 48000)</param>
        /// <param name="channels">Number of channels (1 = mono, 2 = stereo)</param>
        /// <param name="bufferSize">Buffer size in frames</param>
        /// <returns>Result code</returns>
        public static Result Initialize(int sampleRate = 48000, int channels = 2, int bufferSize = 512)
        {
            if (!IsPlatformSupported())
            {
                Debug.LogWarning("UNAudio: Platform not supported");
                return Result.PlatformError;
            }

            int result = UNAudio_Initialize(sampleRate, channels, bufferSize);
            
            if (result == 0)
            {
                Debug.Log($"UNAudio initialized: {sampleRate}Hz, {channels}ch, {bufferSize} frames");
            }
            else
            {
                Debug.LogError($"UNAudio initialization failed: {(Result)result}");
            }

            return (Result)result;
        }

        /// <summary>
        /// Shutdown the audio engine
        /// </summary>
        public static Result Shutdown()
        {
            if (!IsPlatformSupported())
            {
                return Result.PlatformError;
            }

            int result = UNAudio_Shutdown();
            
            if (result == 0)
            {
                Debug.Log("UNAudio shutdown complete");
            }

            return (Result)result;
        }

        /// <summary>
        /// Start audio playback
        /// </summary>
        public static Result Start()
        {
            if (!IsPlatformSupported())
            {
                return Result.PlatformError;
            }

            return (Result)UNAudio_Start();
        }

        /// <summary>
        /// Stop audio playback
        /// </summary>
        public static Result Stop()
        {
            if (!IsPlatformSupported())
            {
                return Result.PlatformError;
            }

            return (Result)UNAudio_Stop();
        }

        /// <summary>
        /// Set master volume
        /// </summary>
        /// <param name="volume">Volume level (0.0 to 1.0)</param>
        public static Result SetVolume(float volume)
        {
            if (!IsPlatformSupported())
            {
                return Result.PlatformError;
            }

            return (Result)UNAudio_SetVolume(Mathf.Clamp01(volume));
        }

        /// <summary>
        /// Get current master volume
        /// </summary>
        public static float GetVolume()
        {
            if (!IsPlatformSupported())
            {
                return 0f;
            }

            return UNAudio_GetVolume();
        }

        /// <summary>
        /// Get current audio latency in milliseconds
        /// </summary>
        public static int GetLatency()
        {
            if (!IsPlatformSupported())
            {
                return -1;
            }

            return UNAudio_GetLatency();
        }

        /// <summary>
        /// Get device information
        /// </summary>
        public static Result GetDeviceInfo(out DeviceInfo info)
        {
            info = new DeviceInfo();
            
            if (!IsPlatformSupported())
            {
                return Result.PlatformError;
            }

            return (Result)UNAudio_GetDeviceInfo(ref info);
        }

        /// <summary>
        /// Set audio callback for processing
        /// </summary>
        public static Result SetCallback(AudioCallbackDelegate callback, IntPtr userData = default)
        {
            if (!IsPlatformSupported())
            {
                return Result.PlatformError;
            }

            return (Result)UNAudio_SetCallback(callback, userData);
        }

        /// <summary>
        /// Check if current platform is supported
        /// </summary>
        public static bool IsPlatformSupported()
        {
#if UNITY_ANDROID || UNITY_IOS
            return true;
#else
            return false;
#endif
        }

        /// <summary>
        /// Get platform name
        /// </summary>
        public static string GetPlatformName()
        {
#if UNITY_ANDROID
            return "Android";
#elif UNITY_IOS
            return "iOS";
#else
            return "Unsupported";
#endif
        }

        #endregion
    }
}
