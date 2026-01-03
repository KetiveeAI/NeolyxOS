/*
 * RoleCut Video Editor - Core Header
 * 
 * Professional video editing for NeolyxOS
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * PROPRIETARY AND CONFIDENTIAL
 */

#ifndef ROLECUT_H
#define ROLECUT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Version Info
 * ============================================================================ */

#define ROLECUT_VERSION_MAJOR 1
#define ROLECUT_VERSION_MINOR 0
#define ROLECUT_VERSION_PATCH 0
#define ROLECUT_VERSION_STRING "1.0.0"

/* ============================================================================
 * Time & Duration
 * ============================================================================ */

typedef int64_t rc_time_t;      /* Time in microseconds */
typedef int64_t rc_duration_t;  /* Duration in microseconds */

#define RC_TIME_SECOND      1000000LL
#define RC_TIME_MILLISECOND 1000LL
#define RC_TIME_MINUTE      (60LL * RC_TIME_SECOND)
#define RC_TIME_HOUR        (60LL * RC_TIME_MINUTE)

/* Convert frames to time at given FPS */
static inline rc_time_t rc_frames_to_time(int64_t frames, double fps) {
    return (rc_time_t)((double)frames / fps * RC_TIME_SECOND);
}

/* Convert time to frames at given FPS */
static inline int64_t rc_time_to_frames(rc_time_t time, double fps) {
    return (int64_t)((double)time / RC_TIME_SECOND * fps);
}

/* ============================================================================
 * Color
 * ============================================================================ */

typedef struct rc_color {
    uint8_t r, g, b, a;
} rc_color_t;

#define RC_COLOR_RGBA(r, g, b, a) ((rc_color_t){r, g, b, a})
#define RC_COLOR_RGB(r, g, b)     ((rc_color_t){r, g, b, 255})
#define RC_COLOR_WHITE  RC_COLOR_RGB(255, 255, 255)
#define RC_COLOR_BLACK  RC_COLOR_RGB(0, 0, 0)
#define RC_COLOR_TRANSPARENT RC_COLOR_RGBA(0, 0, 0, 0)

/* RoleCut brand colors */
#define RC_COLOR_PRIMARY  RC_COLOR_RGB(0xff, 0x6b, 0x6b)
#define RC_COLOR_ACCENT   RC_COLOR_RGB(0xff, 0xd9, 0x3d)

/* ============================================================================
 * Media Types
 * ============================================================================ */

typedef enum rc_media_type {
    RC_MEDIA_VIDEO = 0,
    RC_MEDIA_AUDIO,
    RC_MEDIA_IMAGE,
    RC_MEDIA_TEXT,
    RC_MEDIA_EFFECT,
} rc_media_type_t;

typedef enum rc_codec {
    RC_CODEC_H264 = 0,
    RC_CODEC_H265,
    RC_CODEC_VP9,
    RC_CODEC_AV1,
    RC_CODEC_PRORES,
    RC_CODEC_AAC,
    RC_CODEC_MP3,
    RC_CODEC_OPUS,
    RC_CODEC_FLAC,
} rc_codec_t;

typedef enum rc_container {
    RC_CONTAINER_MP4 = 0,
    RC_CONTAINER_MOV,
    RC_CONTAINER_AVI,
    RC_CONTAINER_MKV,
    RC_CONTAINER_WEBM,
} rc_container_t;

/* ============================================================================
 * Resolution & Frame Info
 * ============================================================================ */

typedef struct rc_resolution {
    int width;
    int height;
} rc_resolution_t;

/* Preset resolutions */
#define RC_RES_720P   ((rc_resolution_t){1280, 720})
#define RC_RES_1080P  ((rc_resolution_t){1920, 1080})
#define RC_RES_1440P  ((rc_resolution_t){2560, 1440})
#define RC_RES_4K     ((rc_resolution_t){3840, 2160})

typedef struct rc_frame_info {
    rc_resolution_t resolution;
    double fps;
    int sample_rate;    /* Audio sample rate */
    int channels;       /* Audio channels */
} rc_frame_info_t;

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct rc_project rc_project_t;
typedef struct rc_timeline rc_timeline_t;
typedef struct rc_track rc_track_t;
typedef struct rc_clip rc_clip_t;
typedef struct rc_media rc_media_t;
typedef struct rc_effect rc_effect_t;
typedef struct rc_transition rc_transition_t;
typedef struct rc_keyframe rc_keyframe_t;

/* ============================================================================
 * Project
 * ============================================================================ */

typedef struct rc_project_settings {
    char name[256];
    rc_resolution_t resolution;
    double fps;
    int sample_rate;
    char output_path[1024];
} rc_project_settings_t;

rc_project_t *rc_project_create(const rc_project_settings_t *settings);
void rc_project_destroy(rc_project_t *project);

bool rc_project_save(rc_project_t *project, const char *path);
rc_project_t *rc_project_load(const char *path);

rc_timeline_t *rc_project_get_timeline(rc_project_t *project);
const rc_project_settings_t *rc_project_get_settings(rc_project_t *project);

/* ============================================================================
 * Media Import
 * ============================================================================ */

typedef struct rc_media_info {
    rc_media_type_t type;
    rc_duration_t duration;
    rc_resolution_t resolution;
    double fps;
    rc_codec_t video_codec;
    rc_codec_t audio_codec;
    int audio_channels;
    int audio_sample_rate;
    size_t file_size;
    char path[1024];
} rc_media_info_t;

rc_media_t *rc_media_import(rc_project_t *project, const char *path);
void rc_media_release(rc_media_t *media);
bool rc_media_get_info(rc_media_t *media, rc_media_info_t *info);

/* ============================================================================
 * Timeline
 * ============================================================================ */

rc_track_t *rc_timeline_add_video_track(rc_timeline_t *timeline);
rc_track_t *rc_timeline_add_audio_track(rc_timeline_t *timeline);
void rc_timeline_remove_track(rc_timeline_t *timeline, rc_track_t *track);

int rc_timeline_get_track_count(rc_timeline_t *timeline);
rc_track_t *rc_timeline_get_track(rc_timeline_t *timeline, int index);

rc_duration_t rc_timeline_get_duration(rc_timeline_t *timeline);
void rc_timeline_set_playhead(rc_timeline_t *timeline, rc_time_t time);
rc_time_t rc_timeline_get_playhead(rc_timeline_t *timeline);

/* ============================================================================
 * Track
 * ============================================================================ */

typedef enum rc_track_type {
    RC_TRACK_VIDEO = 0,
    RC_TRACK_AUDIO,
} rc_track_type_t;

rc_track_type_t rc_track_get_type(rc_track_t *track);
void rc_track_set_name(rc_track_t *track, const char *name);
const char *rc_track_get_name(rc_track_t *track);

void rc_track_set_muted(rc_track_t *track, bool muted);
bool rc_track_is_muted(rc_track_t *track);

void rc_track_set_locked(rc_track_t *track, bool locked);
bool rc_track_is_locked(rc_track_t *track);

rc_clip_t *rc_track_add_clip(rc_track_t *track, rc_media_t *media, 
                              rc_time_t position);
void rc_track_remove_clip(rc_track_t *track, rc_clip_t *clip);

int rc_track_get_clip_count(rc_track_t *track);
rc_clip_t *rc_track_get_clip(rc_track_t *track, int index);
rc_clip_t *rc_track_get_clip_at(rc_track_t *track, rc_time_t time);

/* ============================================================================
 * Clip
 * ============================================================================ */

void rc_clip_set_position(rc_clip_t *clip, rc_time_t position);
rc_time_t rc_clip_get_position(rc_clip_t *clip);

void rc_clip_set_duration(rc_clip_t *clip, rc_duration_t duration);
rc_duration_t rc_clip_get_duration(rc_clip_t *clip);

void rc_clip_set_in_point(rc_clip_t *clip, rc_time_t in_point);
rc_time_t rc_clip_get_in_point(rc_clip_t *clip);

void rc_clip_set_out_point(rc_clip_t *clip, rc_time_t out_point);
rc_time_t rc_clip_get_out_point(rc_clip_t *clip);

/* Trim operations */
void rc_clip_trim_start(rc_clip_t *clip, rc_time_t delta);
void rc_clip_trim_end(rc_clip_t *clip, rc_time_t delta);

/* Split clip at time, returns new clip */
rc_clip_t *rc_clip_split(rc_clip_t *clip, rc_time_t time);

/* Effects */
void rc_clip_add_effect(rc_clip_t *clip, rc_effect_t *effect);
void rc_clip_remove_effect(rc_clip_t *clip, rc_effect_t *effect);

/* Volume (audio clips) */
void rc_clip_set_volume(rc_clip_t *clip, float volume);
float rc_clip_get_volume(rc_clip_t *clip);

/* Opacity (video clips) */
void rc_clip_set_opacity(rc_clip_t *clip, float opacity);
float rc_clip_get_opacity(rc_clip_t *clip);

/* ============================================================================
 * Effects
 * ============================================================================ */

typedef enum rc_effect_type {
    /* Video Effects */
    RC_EFFECT_BRIGHTNESS = 0,
    RC_EFFECT_CONTRAST,
    RC_EFFECT_SATURATION,
    RC_EFFECT_HUE,
    RC_EFFECT_BLUR,
    RC_EFFECT_SHARPEN,
    RC_EFFECT_VIGNETTE,
    RC_EFFECT_COLOR_GRADE,
    RC_EFFECT_CHROMA_KEY,
    
    /* Audio Effects */
    RC_EFFECT_GAIN,
    RC_EFFECT_EQ,
    RC_EFFECT_COMPRESSOR,
    RC_EFFECT_REVERB,
    RC_EFFECT_NOISE_REDUCTION,
    
    /* Motion */
    RC_EFFECT_TRANSFORM,
    RC_EFFECT_CROP,
    RC_EFFECT_PAN_ZOOM,
    
    RC_EFFECT_COUNT
} rc_effect_type_t;

rc_effect_t *rc_effect_create(rc_effect_type_t type);
void rc_effect_destroy(rc_effect_t *effect);

void rc_effect_set_enabled(rc_effect_t *effect, bool enabled);
bool rc_effect_is_enabled(rc_effect_t *effect);

void rc_effect_set_param_float(rc_effect_t *effect, const char *name, float value);
float rc_effect_get_param_float(rc_effect_t *effect, const char *name);

void rc_effect_set_param_color(rc_effect_t *effect, const char *name, rc_color_t color);
rc_color_t rc_effect_get_param_color(rc_effect_t *effect, const char *name);

/* ============================================================================
 * Transitions
 * ============================================================================ */

typedef enum rc_transition_type {
    RC_TRANSITION_CUT = 0,
    RC_TRANSITION_DISSOLVE,
    RC_TRANSITION_FADE,
    RC_TRANSITION_WIPE_LEFT,
    RC_TRANSITION_WIPE_RIGHT,
    RC_TRANSITION_WIPE_UP,
    RC_TRANSITION_WIPE_DOWN,
    RC_TRANSITION_SLIDE,
    RC_TRANSITION_ZOOM,
    RC_TRANSITION_COUNT
} rc_transition_type_t;

rc_transition_t *rc_transition_create(rc_transition_type_t type, 
                                       rc_duration_t duration);
void rc_transition_destroy(rc_transition_t *transition);

bool rc_clip_set_transition_in(rc_clip_t *clip, rc_transition_t *transition);
bool rc_clip_set_transition_out(rc_clip_t *clip, rc_transition_t *transition);

/* ============================================================================
 * Keyframes
 * ============================================================================ */

typedef enum rc_keyframe_interp {
    RC_INTERP_LINEAR = 0,
    RC_INTERP_EASE_IN,
    RC_INTERP_EASE_OUT,
    RC_INTERP_EASE_IN_OUT,
    RC_INTERP_BEZIER,
    RC_INTERP_HOLD,
} rc_keyframe_interp_t;

rc_keyframe_t *rc_keyframe_add(rc_effect_t *effect, const char *param,
                                rc_time_t time, float value);
void rc_keyframe_remove(rc_keyframe_t *keyframe);

void rc_keyframe_set_interpolation(rc_keyframe_t *keyframe, 
                                    rc_keyframe_interp_t interp);

/* ============================================================================
 * Export
 * ============================================================================ */

typedef struct rc_export_settings {
    rc_container_t container;
    rc_codec_t video_codec;
    rc_codec_t audio_codec;
    rc_resolution_t resolution;
    double fps;
    int bitrate_video;      /* kbps */
    int bitrate_audio;      /* kbps */
    int quality;            /* 0-100 */
    char output_path[1024];
} rc_export_settings_t;

/* Export presets */
typedef enum rc_export_preset {
    RC_PRESET_YOUTUBE_1080P = 0,
    RC_PRESET_YOUTUBE_4K,
    RC_PRESET_INSTAGRAM_FEED,
    RC_PRESET_INSTAGRAM_STORY,
    RC_PRESET_TIKTOK,
    RC_PRESET_TWITTER,
    RC_PRESET_PRORES_MASTER,
    RC_PRESET_WEB_OPTIMIZED,
    RC_PRESET_CUSTOM,
} rc_export_preset_t;

void rc_export_get_preset(rc_export_preset_t preset, rc_export_settings_t *settings);

typedef void (*rc_export_progress_fn)(float progress, const char *status, void *user);

bool rc_export_start(rc_project_t *project, const rc_export_settings_t *settings,
                     rc_export_progress_fn callback, void *user_data);
void rc_export_cancel(rc_project_t *project);
bool rc_export_is_running(rc_project_t *project);

/* ============================================================================
 * Playback
 * ============================================================================ */

typedef enum rc_playback_state {
    RC_PLAYBACK_STOPPED = 0,
    RC_PLAYBACK_PLAYING,
    RC_PLAYBACK_PAUSED,
} rc_playback_state_t;

void rc_playback_play(rc_timeline_t *timeline);
void rc_playback_pause(rc_timeline_t *timeline);
void rc_playback_stop(rc_timeline_t *timeline);
void rc_playback_seek(rc_timeline_t *timeline, rc_time_t time);

rc_playback_state_t rc_playback_get_state(rc_timeline_t *timeline);

void rc_playback_set_loop(rc_timeline_t *timeline, bool loop);
void rc_playback_set_loop_region(rc_timeline_t *timeline, 
                                  rc_time_t start, rc_time_t end);

#ifdef __cplusplus
}
#endif

#endif /* ROLECUT_H */
