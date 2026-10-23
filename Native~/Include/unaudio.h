/*
 * UNAudio — low-latency native audio engine for Unity.
 *
 * Public C ABI. Every struct is plain-old-data with explicit sizes so the
 * C# side can declare a blittable mirror (see Runtime/Scripts/Native).
 *
 * Threading
 *   - Every function may be called from any thread except the audio thread.
 *   - Calls never block on the audio thread and never wait for decoding.
 *   - The audio thread never locks, allocates or decodes.
 *
 * Handles
 *   una_clip / una_voice are 32-bit (slot + 1) | (generation << 16).
 *   0 is never a valid handle. Stale handles are detected and rejected.
 *
 * Time
 *   "dsp frame"   : index of a frame on the mixer timeline (device rate).
 *                   Scheduling is expressed in dsp frames and is sample exact.
 *   "host ns"     : monotonic host clock (QPC / mach_absolute_time /
 *                   CLOCK_MONOTONIC) in nanoseconds — una_host_time_ns().
 *   "audible"     : the dsp frame that is reaching the speaker at a host
 *                   time, i.e. mixing position minus output latency, smoothed
 *                   with a delay-locked loop so it advances continuously.
 *   "source frame": position inside a clip at the clip's stored rate.
 */
#ifndef UNAUDIO_H
#define UNAUDIO_H

#include <stdint.h>

#if defined(_WIN32)
#  if defined(UNAUDIO_BUILDING)
#    define UNA_API __declspec(dllexport)
#  else
#    define UNA_API
#  endif
#else
#  define UNA_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define UNA_API_VERSION 2

typedef uint32_t una_clip;
typedef uint32_t una_voice;

/* ── Results ─────────────────────────────────────────────────────── */

typedef enum una_result {
    UNA_OK                    = 0,
    UNA_E_INVALID_ARG         = -1,
    UNA_E_NOT_INITIALIZED     = -2,
    UNA_E_ALREADY_INITIALIZED = -3,
    UNA_E_DEVICE              = -4,
    UNA_E_OUT_OF_MEMORY       = -5,
    UNA_E_FORMAT              = -6,
    UNA_E_INVALID_HANDLE      = -7,
    UNA_E_QUEUE_FULL          = -8,
    UNA_E_NO_VOICE            = -9,
    UNA_E_NOT_READY           = -10,
    UNA_E_IO                  = -11,
    UNA_E_TOO_MANY_CLIPS      = -12,
    UNA_E_ABI_MISMATCH        = -13,
    UNA_E_DECODE              = -14  /* data failed to decode or seek after a successful
                                        open (corrupt stream, borrowed bytes changed) */
} una_result;

/* ── Engine ──────────────────────────────────────────────────────── */

typedef enum una_device_type {
    UNA_DEVICE_DEFAULT = 0, /* platform backend (WASAPI/CoreAudio/Oboe/ALSA) */
    UNA_DEVICE_NULL    = 1, /* real-time paced thread, output discarded       */
    UNA_DEVICE_MANUAL  = 2  /* no thread; caller drives una_render()          */
} una_device_type;

typedef struct una_config {
    int32_t struct_size;      /* sizeof(una_config) — ABI guard               */
    int32_t device_type;      /* una_device_type                              */
    int32_t sample_rate;      /* 0 = device native rate (recommended)         */
    int32_t buffer_frames;    /* 0 = lowest the device supports               */
    int32_t exclusive;        /* 1 = request exclusive / unshared stream      */
    int32_t max_voices;       /* 0 = 128                                      */
    int32_t max_clips;        /* 0 = 1024                                     */
    int32_t decode_threads;   /* 0 = auto (1–2)                               */
    int32_t channels;         /* 0 = device channels (mixer is stereo)        */
    int32_t reserved[7];
} una_config;

typedef struct una_device_info {
    int32_t sample_rate;
    int32_t channels;
    int32_t buffer_frames;    /* frames per device period                     */
    int32_t latency_frames;   /* backend-reported output latency              */
    int32_t exclusive;
    int32_t backend;          /* una_backend                                  */
    int32_t reserved[2];
} una_device_info;

typedef enum una_backend {
    UNA_BACKEND_NONE = 0, UNA_BACKEND_MANUAL = 1, UNA_BACKEND_NULL = 2,
    UNA_BACKEND_WASAPI = 3, UNA_BACKEND_COREAUDIO = 4, UNA_BACKEND_AAUDIO = 5,
    UNA_BACKEND_OPENSLES = 6, UNA_BACKEND_ALSA = 7
} una_backend;

UNA_API int32_t  una_get_api_version(void);
UNA_API int32_t  una_init(const una_config* config);
UNA_API void     una_shutdown(void);
UNA_API int32_t  una_is_initialized(void);
UNA_API int32_t  una_get_device_info(una_device_info* out);

/* Housekeeping: reclaims finished voices / released clips. Call once per
 * frame from the main thread (the C# layer does this from the PlayerLoop). */
UNA_API void     una_update(void);

/* Stops (1) or restarts (0) the output stream without tearing the engine down,
 * e.g. while the app is in the background. The dsp timeline freezes while
 * suspended, so scheduled frames keep their meaning; the clock re-anchors on
 * resume. For the MANUAL device una_render outputs silence without advancing
 * the timeline while suspended. */
UNA_API int32_t  una_set_suspended(int32_t suspended);
UNA_API int32_t  una_is_suspended(void);

/* MANUAL device only: render `frames` frames of interleaved float output.
 * `channels` must equal the configured channel count. */
UNA_API int32_t  una_render(float* out, int32_t frames, int32_t channels);

/* ── Clock ───────────────────────────────────────────────────────── */

typedef struct una_clock {
    int64_t host_ns;          /* host time this snapshot was taken at          */
    int64_t mix_frame;        /* next dsp frame the mixer will produce         */
    double  audible_frame;    /* dsp frame reaching the speaker at host_ns     */
    double  frames_per_second;/* measured device rate in host time (DLL)      */
    int32_t sample_rate;      /* nominal device rate                           */
    int32_t buffer_frames;
    int64_t anchor_frame;     /* DLL anchor: dsp frame ...                     */
    int64_t anchor_host_ns;   /* ... presented at this host time               */
    int64_t callbacks;
    int64_t discontinuities;  /* clock resets (xrun / device restart)          */
} una_clock;

UNA_API int64_t  una_host_time_ns(void);
UNA_API int32_t  una_get_clock(una_clock* out);
/* Audible dsp frame at an arbitrary host time (past or near future). */
UNA_API double   una_audible_frame_at(int64_t host_ns);

/* ── Clips ───────────────────────────────────────────────────────── */

typedef enum una_load_mode {
    UNA_LOAD_DECODE = 0,     /* async decode whole clip to PCM (playable early) */
    UNA_LOAD_STREAM = 1      /* keep compressed, decode per voice on a worker   */
} una_load_mode;

typedef enum una_sample_format {
    UNA_SAMPLE_INT16 = 0,    /* half the memory, ~96 dB                         */
    UNA_SAMPLE_FLOAT32 = 1
} una_sample_format;

typedef enum una_clip_state {
    UNA_CLIP_INVALID = -1,
    UNA_CLIP_LOADING = 0,    /* header not parsed yet                           */
    UNA_CLIP_READY   = 1,    /* playable; decode still running                  */
    UNA_CLIP_LOADED  = 2,    /* fully decoded (or stream prepared)              */
    UNA_CLIP_FAILED  = 3
} una_clip_state;

#define UNA_CLIP_FLAG_BORROW       0x1 /* do not copy input memory; it must stay
                                          valid until UNA_EVENT_CLIP_SOURCE_RELEASED
                                          (DECODE) or UNA_EVENT_CLIP_FREED (STREAM,
                                          which reads it for as long as it plays) */
#define UNA_CLIP_FLAG_NO_RESAMPLE  0x2 /* keep source rate; mixer resamples live */

typedef struct una_clip_params {
    int32_t struct_size;
    int32_t mode;            /* una_load_mode                                   */
    int32_t sample_format;   /* una_sample_format (DECODE only)                 */
    int32_t flags;           /* UNA_CLIP_FLAG_*                                 */
    int32_t priority;        /* higher decodes first                            */
    int32_t reserved[3];
} una_clip_params;

typedef struct una_clip_info {
    int32_t state;           /* una_clip_state                                  */
    int32_t error;           /* una_result when FAILED                          */
    int32_t channels;        /* stored channels (1 or 2)                        */
    int32_t source_channels;
    int32_t source_rate;
    int32_t stored_rate;     /* rate of the frames the mixer reads              */
    int32_t mode;
    int32_t sample_format;
    int64_t total_frames;    /* at stored rate                                  */
    int64_t decoded_frames;  /* at stored rate                                  */
    int64_t memory_bytes;
    double  length_seconds;
} una_clip_info;

/* Starts an async load. The clip handle is valid immediately; poll
 * una_clip_get_info or wait for UNA_EVENT_CLIP_* events. Unless BORROW is
 * set the bytes are copied before returning. Supported: Ogg Vorbis, WAV,
 * MP3 (Layer III), FLAC; detected from the content. */
UNA_API int32_t  una_clip_load_memory(const void* data, int64_t size,
                                      const una_clip_params* params,
                                      una_clip* out_clip);
/* File is read on a worker thread (UTF-8 path). */
UNA_API int32_t  una_clip_load_file(const char* utf8_path,
                                    const una_clip_params* params,
                                    una_clip* out_clip);
/* Drops the caller's reference. Playing voices keep the data alive. */
UNA_API int32_t  una_clip_release(una_clip clip);
UNA_API int32_t  una_clip_get_info(una_clip clip, una_clip_info* out);
UNA_API int32_t  una_clip_set_priority(una_clip clip, int32_t priority);

/* Synchronous header probe without an engine (editor import, validation). */
UNA_API int32_t  una_probe(const void* data, int64_t size, una_clip_info* out);

/* ── Voices ──────────────────────────────────────────────────────── */

#define UNA_PLAY_PAUSED     0x1 /* create paused (position at start_frame)       */
#define UNA_PLAY_CATCH_UP   0x2 /* if the start time already passed, skip the
                                   missed frames so the voice stays aligned     */

typedef struct una_play_params {
    int32_t struct_size;
    float   volume;          /* linear gain, default 1                          */
    float   pan;             /* -1 .. 1                                         */
    float   pitch;           /* playback rate multiplier, default 1             */
    int32_t loop;
    int32_t bus;             /* 0 .. UNA_MAX_BUSES-1                            */
    int32_t flags;           /* UNA_PLAY_*                                      */
    float   fade_in_seconds;
    int64_t start_dsp_frame; /* <= 0: as soon as possible                       */
    int64_t start_frame;     /* source frame to start from                      */
    int64_t loop_start;      /* source frames; loop_end <= 0 means clip end     */
    int64_t loop_end;
} una_play_params;

#define UNA_MAX_BUSES 16

typedef enum una_voice_state {
    UNA_VOICE_INVALID   = 0, /* stale / reclaimed handle                         */
    UNA_VOICE_PENDING   = 1, /* command not yet seen by the mixer                */
    UNA_VOICE_SCHEDULED = 2, /* waiting for start_dsp_frame or stream data       */
    UNA_VOICE_PLAYING   = 3,
    UNA_VOICE_PAUSED    = 4,
    UNA_VOICE_ENDED     = 5
} una_voice_state;

typedef struct una_voice_timing {
    int32_t state;           /* una_voice_state                                 */
    int32_t stored_rate;     /* clip stored rate (frames → seconds)             */
    double  audible_frame;   /* source frame reaching the speaker now; negative
                                before a scheduled start                        */
    double  audible_seconds;
    double  mix_frame;       /* source frame at the mixer's write head          */
    int64_t start_dsp_frame; /* dsp frame the voice started / will start        */
    int64_t host_ns;         /* host time the snapshot refers to                */
    /* Current linear segment: source = seg_source + (dsp - seg_dsp) * seg_rate */
    int64_t seg_dsp_frame;
    double  seg_source_frame;
    double  seg_rate;        /* source frames per dsp frame (0 = paused)        */
} una_voice_timing;

UNA_API int32_t  una_voice_play(una_clip clip, const una_play_params* params,
                                una_voice* out_voice);
/* at_dsp_frame <= 0 means immediately. fade_seconds > 0 ramps to silence
 * before the voice ends (the ramp starts at at_dsp_frame). */
UNA_API int32_t  una_voice_stop(una_voice v, int64_t at_dsp_frame, float fade_seconds);
UNA_API int32_t  una_voice_pause(una_voice v, int64_t at_dsp_frame);
UNA_API int32_t  una_voice_resume(una_voice v, int64_t at_dsp_frame);
UNA_API int32_t  una_voice_seek(una_voice v, int64_t source_frame, int64_t at_dsp_frame);
UNA_API int32_t  una_voice_set_volume(una_voice v, float volume, float ramp_seconds,
                                      int64_t at_dsp_frame);
UNA_API int32_t  una_voice_set_pan(una_voice v, float pan, int64_t at_dsp_frame);
UNA_API int32_t  una_voice_set_pitch(una_voice v, float pitch, int64_t at_dsp_frame);
UNA_API int32_t  una_voice_set_loop(una_voice v, int32_t loop, int64_t loop_start,
                                    int64_t loop_end);
UNA_API int32_t  una_voice_get_state(una_voice v);
/* host_ns <= 0 means "now". */
UNA_API int32_t  una_voice_get_timing(una_voice v, int64_t host_ns,
                                      una_voice_timing* out);
UNA_API int32_t  una_stop_all(float fade_seconds);

/* ── Mix ─────────────────────────────────────────────────────────── */

/* Master and bus volumes are kept across una_shutdown / una_init and may be
   set before the first una_init. Bus pause state is per session. */
UNA_API int32_t  una_set_master_volume(float volume, float ramp_seconds);
UNA_API float    una_get_master_volume(void);
UNA_API int32_t  una_bus_set_volume(int32_t bus, float volume, float ramp_seconds);
UNA_API int32_t  una_bus_set_paused(int32_t bus, int32_t paused, int64_t at_dsp_frame);

/* ── Events ──────────────────────────────────────────────────────── */

typedef enum una_event_type {
    UNA_EVENT_NONE                 = 0,
    UNA_EVENT_CLIP_READY           = 1,
    UNA_EVENT_CLIP_LOADED          = 2,
    UNA_EVENT_CLIP_FAILED          = 3,  /* value = una_result               */
    UNA_EVENT_CLIP_SOURCE_RELEASED = 4,  /* borrowed memory no longer used   */
    UNA_EVENT_CLIP_FREED           = 5,
    UNA_EVENT_VOICE_STARTED        = 10, /* dsp_frame = exact start frame     */
    UNA_EVENT_VOICE_ENDED          = 11, /* dsp_frame = exact end frame       */
    UNA_EVENT_VOICE_LOOPED         = 12,
    UNA_EVENT_VOICE_STARVED        = 13, /* value = frames of silence inserted */
    UNA_EVENT_VOICE_ERROR          = 14, /* value = una_result; a stream voice could
                                            not decode (open or seek failed, or the
                                            decoder ran out of data before the
                                            declared length) after playing all it
                                            decoded. VOICE_ENDED follows at the
                                            same dsp_frame. What counts as damage
                                            depends on the format: see API.md.
                                            Decode-mode clips pad with silence.   */
    UNA_EVENT_DEVICE_CHANGED       = 20, /* value = new sample rate           */
    UNA_EVENT_DEVICE_LOST          = 21
} una_event_type;

typedef struct una_event {
    int32_t  type;
    uint32_t handle;
    int64_t  dsp_frame;
    int64_t  value;
} una_event;

/* Copies up to `max` pending events, returns the count. */
UNA_API int32_t  una_poll_events(una_event* out, int32_t max);

/* ── Stats ───────────────────────────────────────────────────────── */

typedef struct una_stats {
    int64_t callbacks;
    int64_t frames_rendered;
    int64_t starved_frames;
    int64_t dropped_commands;
    int64_t late_commands;     /* applied after their requested dsp frame     */
    int32_t active_voices;
    int32_t live_clips;
    int32_t decode_jobs;
    int32_t streams;
    float   cpu_load;          /* render time / buffer time, last callback    */
    float   cpu_load_peak;     /* since last una_get_stats                    */
    float   peak_left;
    float   peak_right;
    int64_t clip_memory_bytes;
} una_stats;

UNA_API int32_t  una_get_stats(una_stats* out);

#ifdef __cplusplus
}
#endif

#endif /* UNAUDIO_H */
