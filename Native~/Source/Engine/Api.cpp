// Exported C ABI — thin forwarding to the process-wide engine.

#include "Engine.h"
#include "../Decode/DecodePipeline.h"

#include <cstring>

using una::GlobalEngine;

extern "C" {

UNA_API int32_t una_get_api_version(void) { return UNA_API_VERSION; }

UNA_API int32_t una_init(const una_config* config) {
    if (!config) return UNA_E_INVALID_ARG;
    return GlobalEngine()->Init(*config);
}

UNA_API void una_shutdown(void) { GlobalEngine()->Shutdown(); }

UNA_API int32_t una_is_initialized(void) {
    una_device_info info;
    return GlobalEngine()->GetDeviceInfo(info) == UNA_OK ? 1 : 0;
}

UNA_API int32_t una_get_device_info(una_device_info* out) {
    if (!out) return UNA_E_INVALID_ARG;
    return GlobalEngine()->GetDeviceInfo(*out);
}

UNA_API void una_update(void) { GlobalEngine()->Update(); }

UNA_API int32_t una_set_suspended(int32_t suspended) { return GlobalEngine()->SetSuspended(suspended != 0); }

UNA_API int32_t una_is_suspended(void) { return GlobalEngine()->IsSuspended() ? 1 : 0; }

UNA_API int32_t una_render(float* out, int32_t frames, int32_t channels) {
    return GlobalEngine()->Render(out, frames, channels);
}

UNA_API int64_t una_host_time_ns(void) { return GlobalEngine()->NowNs(); }

UNA_API int32_t una_get_clock(una_clock* out) {
    if (!out) return UNA_E_INVALID_ARG;
    return GlobalEngine()->GetClock(*out);
}

UNA_API double una_audible_frame_at(int64_t host_ns) { return GlobalEngine()->AudibleFrameAt(host_ns); }

UNA_API int32_t una_clip_load_memory(const void* data, int64_t size, const una_clip_params* params,
                                     una_clip* out_clip) {
    return GlobalEngine()->ClipLoad(data, size, nullptr, params, out_clip);
}

UNA_API int32_t una_clip_load_file(const char* utf8_path, const una_clip_params* params, una_clip* out_clip) {
    if (!utf8_path || !*utf8_path) return UNA_E_INVALID_ARG;
    return GlobalEngine()->ClipLoad(nullptr, 0, utf8_path, params, out_clip);
}

UNA_API int32_t una_clip_release(una_clip clip) { return GlobalEngine()->ClipRelease(clip); }

UNA_API int32_t una_clip_get_info(una_clip clip, una_clip_info* out) {
    if (!out) return UNA_E_INVALID_ARG;
    return GlobalEngine()->ClipInfo(clip, *out);
}

UNA_API int32_t una_clip_set_priority(una_clip clip, int32_t priority) {
    return GlobalEngine()->ClipSetPriority(clip, priority);
}

UNA_API int32_t una_probe(const void* data, int64_t size, una_clip_info* out) {
    if (!data || size <= 0 || !out) return UNA_E_INVALID_ARG;
    std::memset(out, 0, sizeof(*out));
    una::DecodePipeline pipe;
    const int rc = pipe.Open(static_cast<const uint8_t*>(data), static_cast<size_t>(size), 0);
    if (rc != UNA_OK) {
        out->state = UNA_CLIP_FAILED;
        out->error = rc;
        return rc;
    }
    out->state = UNA_CLIP_LOADED;
    out->channels = pipe.StoredChannels();
    out->source_channels = pipe.SourceChannels();
    out->source_rate = pipe.SourceRate();
    out->stored_rate = pipe.StoredRate();
    out->total_frames = pipe.TotalFrames();
    out->decoded_frames = 0;
    out->length_seconds = pipe.StoredRate() > 0 ? static_cast<double>(pipe.TotalFrames()) / pipe.StoredRate() : 0.0;
    return UNA_OK;
}

UNA_API int32_t una_voice_play(una_clip clip, const una_play_params* params, una_voice* out_voice) {
    return GlobalEngine()->VoicePlay(clip, params, out_voice);
}

UNA_API int32_t una_voice_stop(una_voice v, int64_t at_dsp_frame, float fade_seconds) {
    return GlobalEngine()->VoiceStop(v, at_dsp_frame, fade_seconds);
}

UNA_API int32_t una_voice_pause(una_voice v, int64_t at_dsp_frame) {
    return GlobalEngine()->VoicePause(v, at_dsp_frame, true);
}

UNA_API int32_t una_voice_resume(una_voice v, int64_t at_dsp_frame) {
    return GlobalEngine()->VoicePause(v, at_dsp_frame, false);
}

UNA_API int32_t una_voice_seek(una_voice v, int64_t source_frame, int64_t at_dsp_frame) {
    return GlobalEngine()->VoiceSeek(v, source_frame, at_dsp_frame);
}

UNA_API int32_t una_voice_set_volume(una_voice v, float volume, float ramp_seconds, int64_t at_dsp_frame) {
    return GlobalEngine()->VoiceVolume(v, volume, ramp_seconds, at_dsp_frame);
}

UNA_API int32_t una_voice_set_pan(una_voice v, float pan, int64_t at_dsp_frame) {
    return GlobalEngine()->VoicePan(v, pan, at_dsp_frame);
}

UNA_API int32_t una_voice_set_pitch(una_voice v, float pitch, int64_t at_dsp_frame) {
    return GlobalEngine()->VoicePitch(v, pitch, at_dsp_frame);
}

UNA_API int32_t una_voice_set_loop(una_voice v, int32_t loop, int64_t loop_start, int64_t loop_end) {
    return GlobalEngine()->VoiceLoop(v, loop, loop_start, loop_end);
}

UNA_API int32_t una_voice_get_state(una_voice v) { return GlobalEngine()->VoiceState(v); }

UNA_API int32_t una_voice_get_timing(una_voice v, int64_t host_ns, una_voice_timing* out) {
    if (!out) return UNA_E_INVALID_ARG;
    return GlobalEngine()->VoiceTimingInfo(v, host_ns, *out);
}

UNA_API int32_t una_stop_all(float fade_seconds) { return GlobalEngine()->StopAll(fade_seconds); }

UNA_API int32_t una_set_master_volume(float volume, float ramp_seconds) {
    return GlobalEngine()->SetMasterVolume(volume, ramp_seconds);
}

UNA_API float una_get_master_volume(void) { return GlobalEngine()->MasterVolume(); }

UNA_API int32_t una_bus_set_volume(int32_t bus, float volume, float ramp_seconds) {
    return GlobalEngine()->BusVolume(bus, volume, ramp_seconds);
}

UNA_API int32_t una_bus_set_paused(int32_t bus, int32_t paused, int64_t at_dsp_frame) {
    return GlobalEngine()->BusPause(bus, paused != 0, at_dsp_frame);
}

UNA_API int32_t una_poll_events(una_event* out, int32_t max) { return GlobalEngine()->PollEvents(out, max); }

UNA_API int32_t una_get_stats(una_stats* out) {
    if (!out) return UNA_E_INVALID_ARG;
    return GlobalEngine()->Stats(*out);
}

} // extern "C"
