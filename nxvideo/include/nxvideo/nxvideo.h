/*
 * NXVideo - NeolyxOS Native Video Rendering System
 * 
 * Production-grade video API with:
 * - Hardware accelerated decode (VAAPI, NVDEC, VideoToolbox)
 * - Software decode fallback
 * - GPU texture output
 * - A/V synchronization
 * - Color space conversion
 * - HDR tone mapping
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXVIDEO_H
#define NXVIDEO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* ============ Version ============ */
#define NXVIDEO_VERSION_MAJOR   1
#define NXVIDEO_VERSION_MINOR   0
#define NXVIDEO_VERSION_PATCH   0
#define NXVIDEO_VERSION_STRING  "1.0.0"

/* ============ Handle Types ============ */
typedef int32_t nxvideo_context_t;
typedef int32_t nxvideo_decoder_t;
typedef int32_t nxvideo_player_t;
typedef int32_t nxvideo_frame_t;
typedef int32_t nxvideo_texture_t;

#define NXVIDEO_INVALID_HANDLE (-1)

/* ============ Error Codes ============ */
typedef enum {
    NXVIDEO_SUCCESS             = 0,
    NXVIDEO_ERROR_INIT          = -1,
    NXVIDEO_ERROR_INVALID       = -2,
    NXVIDEO_ERROR_NO_MEMORY     = -3,
    NXVIDEO_ERROR_NO_DEVICE     = -4,
    NXVIDEO_ERROR_IO            = -5,
    NXVIDEO_ERROR_FORMAT        = -6,
    NXVIDEO_ERROR_DECODE        = -7,
    NXVIDEO_ERROR_EOF           = -8,
    NXVIDEO_ERROR_UNSUPPORTED   = -9,
} nxvideo_error_t;

/* ============ Video Codecs ============ */
typedef enum {
    NXVIDEO_CODEC_UNKNOWN       = 0,
    NXVIDEO_CODEC_H264          = 1,
    NXVIDEO_CODEC_H265          = 2,
    NXVIDEO_CODEC_VP8           = 3,
    NXVIDEO_CODEC_VP9           = 4,
    NXVIDEO_CODEC_AV1           = 5,
    NXVIDEO_CODEC_MPEG4         = 6,
    NXVIDEO_CODEC_MPEG2         = 7,
} nxvideo_codec_t;

/* ============ Pixel Formats ============ */
typedef enum {
    NXVIDEO_PIXEL_RGB24         = 0,
    NXVIDEO_PIXEL_RGBA32        = 1,
    NXVIDEO_PIXEL_BGR24         = 2,
    NXVIDEO_PIXEL_BGRA32        = 3,
    NXVIDEO_PIXEL_YUV420P       = 4,
    NXVIDEO_PIXEL_NV12          = 5,
    NXVIDEO_PIXEL_P010          = 6,  /* 10-bit YUV */
} nxvideo_pixel_format_t;

/* ============ Hardware Decoder Type ============ */
typedef enum {
    NXVIDEO_HW_NONE             = 0,
    NXVIDEO_HW_VAAPI            = 1,  /* Linux Intel/AMD */
    NXVIDEO_HW_NVDEC            = 2,  /* NVIDIA */
    NXVIDEO_HW_VDPAU            = 3,  /* NVIDIA legacy */
    NXVIDEO_HW_VIDEOTOOLBOX     = 4,  /* macOS */
    NXVIDEO_HW_D3D11VA          = 5,  /* Windows */
    NXVIDEO_HW_VULKAN           = 6,  /* Cross-platform */
} nxvideo_hw_type_t;

/* ============ Playback State ============ */
typedef enum {
    NXVIDEO_STATE_STOPPED       = 0,
    NXVIDEO_STATE_PLAYING       = 1,
    NXVIDEO_STATE_PAUSED        = 2,
    NXVIDEO_STATE_BUFFERING     = 3,
    NXVIDEO_STATE_SEEKING       = 4,
    NXVIDEO_STATE_ENDED         = 5,
    NXVIDEO_STATE_ERROR         = 6,
} nxvideo_state_t;

/* ============ Video Frame ============ */
typedef struct {
    uint8_t        *data[4];        /* Plane pointers (Y, U, V, A) */
    int32_t         linesize[4];    /* Bytes per line per plane */
    int32_t         width;
    int32_t         height;
    nxvideo_pixel_format_t format;
    double          pts;            /* Presentation time (seconds) */
    double          duration;       /* Frame duration */
    uint8_t         keyframe;
    uint32_t        hw_texture;     /* GPU texture ID if hardware */
} nxvideo_frame_info_t;

/* ============ Video Stream Info ============ */
typedef struct {
    nxvideo_codec_t     codec;
    int32_t             width;
    int32_t             height;
    double              fps;
    double              duration;       /* Total duration in seconds */
    int64_t             bitrate;
    int32_t             bit_depth;      /* 8, 10, or 12 */
    nxvideo_pixel_format_t pixel_format;
    char                codec_name[32];
    char                profile[32];
} nxvideo_stream_info_t;

/* ============ Audio Stream Info (for A/V sync) ============ */
typedef struct {
    int32_t             sample_rate;
    int32_t             channels;
    int64_t             bitrate;
    double              duration;
    char                codec_name[32];
} nxvideo_audio_info_t;

/* ============ Media Info ============ */
typedef struct {
    int                     has_video;
    int                     has_audio;
    nxvideo_stream_info_t   video;
    nxvideo_audio_info_t    audio;
    double                  duration;
    int64_t                 size_bytes;
    char                    container[32];
    char                    title[256];
} nxvideo_media_info_t;

/* ============ Hardware Capabilities ============ */
typedef struct {
    nxvideo_hw_type_t   type;
    char                name[64];
    int                 supports_h264;
    int                 supports_h265;
    int                 supports_vp9;
    int                 supports_av1;
    int                 supports_10bit;
    int                 max_width;
    int                 max_height;
} nxvideo_hw_caps_t;

/* ============ Decoder Config ============ */
typedef struct {
    nxvideo_hw_type_t   preferred_hw;       /* NXVIDEO_HW_NONE for software */
    int                 threads;            /* 0 = auto */
    int                 low_latency;        /* For live streams */
    nxvideo_pixel_format_t output_format;   /* Desired output format */
} nxvideo_decoder_config_t;

/* ============ Player Config ============ */
typedef struct {
    int                 hw_decode;          /* Enable hardware decode */
    int                 loop;               /* Loop playback */
    float               volume;             /* 0.0 - 1.0 */
    float               playback_rate;      /* 0.5 - 4.0 */
    int                 muted;
} nxvideo_player_config_t;

/* ============ Callbacks ============ */
typedef void (*nxvideo_frame_cb)(void *userdata, const nxvideo_frame_info_t *frame);
typedef void (*nxvideo_state_cb)(void *userdata, nxvideo_state_t state);
typedef void (*nxvideo_error_cb)(void *userdata, nxvideo_error_t error, const char *msg);
typedef void (*nxvideo_progress_cb)(void *userdata, double current, double total);

/* ===========================================================================
 * System API
 * =========================================================================*/

/**
 * Initialize NXVideo system
 */
nxvideo_error_t nxvideo_init(void);

/**
 * Shutdown NXVideo system
 */
void nxvideo_shutdown(void);

/**
 * Get version string
 */
const char* nxvideo_version(void);

/**
 * Get error description
 */
const char* nxvideo_error_string(nxvideo_error_t error);

/* ===========================================================================
 * Hardware Detection API
 * =========================================================================*/

/**
 * Get number of hardware decoders available
 */
int nxvideo_hw_count(void);

/**
 * Get hardware decoder capabilities
 */
nxvideo_error_t nxvideo_hw_caps(int index, nxvideo_hw_caps_t *caps);

/**
 * Get best hardware decoder for codec
 */
nxvideo_hw_type_t nxvideo_hw_best(nxvideo_codec_t codec);

/**
 * Check if hardware decode available for codec
 */
int nxvideo_hw_available(nxvideo_codec_t codec);

/* ===========================================================================
 * Decoder API (Low-level decode)
 * =========================================================================*/

/**
 * Create decoder for codec
 */
nxvideo_decoder_t nxvideo_decoder_create(nxvideo_codec_t codec,
                                          const nxvideo_decoder_config_t *config);

/**
 * Destroy decoder
 */
void nxvideo_decoder_destroy(nxvideo_decoder_t decoder);

/**
 * Feed data to decoder
 */
nxvideo_error_t nxvideo_decoder_send(nxvideo_decoder_t decoder,
                                      const uint8_t *data,
                                      size_t size,
                                      double pts);

/**
 * Receive decoded frame
 */
nxvideo_error_t nxvideo_decoder_receive(nxvideo_decoder_t decoder,
                                         nxvideo_frame_info_t *frame);

/**
 * Flush decoder
 */
nxvideo_error_t nxvideo_decoder_flush(nxvideo_decoder_t decoder);

/* ===========================================================================
 * Player API (High-level playback)
 * =========================================================================*/

/**
 * Create video player
 */
nxvideo_player_t nxvideo_player_create(const nxvideo_player_config_t *config);

/**
 * Destroy player
 */
void nxvideo_player_destroy(nxvideo_player_t player);

/**
 * Open media file or URL
 */
nxvideo_error_t nxvideo_player_open(nxvideo_player_t player, const char *url);

/**
 * Close current media
 */
nxvideo_error_t nxvideo_player_close(nxvideo_player_t player);

/**
 * Get media info
 */
nxvideo_error_t nxvideo_player_info(nxvideo_player_t player,
                                     nxvideo_media_info_t *info);

/**
 * Start playback
 */
nxvideo_error_t nxvideo_player_play(nxvideo_player_t player);

/**
 * Pause playback
 */
nxvideo_error_t nxvideo_player_pause(nxvideo_player_t player);

/**
 * Stop playback
 */
nxvideo_error_t nxvideo_player_stop(nxvideo_player_t player);

/**
 * Seek to position (seconds)
 */
nxvideo_error_t nxvideo_player_seek(nxvideo_player_t player, double seconds);

/**
 * Get current playback position
 */
double nxvideo_player_position(nxvideo_player_t player);

/**
 * Get total duration
 */
double nxvideo_player_duration(nxvideo_player_t player);

/**
 * Get current state
 */
nxvideo_state_t nxvideo_player_state(nxvideo_player_t player);

/**
 * Set volume (0.0 - 1.0)
 */
nxvideo_error_t nxvideo_player_set_volume(nxvideo_player_t player, float volume);

/**
 * Set muted
 */
nxvideo_error_t nxvideo_player_set_muted(nxvideo_player_t player, int muted);

/**
 * Set playback rate (0.5 - 4.0)
 */
nxvideo_error_t nxvideo_player_set_rate(nxvideo_player_t player, float rate);

/**
 * Set loop
 */
nxvideo_error_t nxvideo_player_set_loop(nxvideo_player_t player, int loop);

/* ===========================================================================
 * Frame API
 * =========================================================================*/

/**
 * Get current video frame
 * Returns frame info and data for rendering
 */
nxvideo_error_t nxvideo_player_current_frame(nxvideo_player_t player,
                                              nxvideo_frame_info_t *frame);

/**
 * Upload frame to GPU texture
 * Returns OpenGL texture ID
 */
uint32_t nxvideo_frame_to_texture(const nxvideo_frame_info_t *frame);

/**
 * Free frame data
 */
void nxvideo_frame_free(nxvideo_frame_info_t *frame);

/* ===========================================================================
 * Callback API
 * =========================================================================*/

/**
 * Set frame callback (called for each decoded frame)
 */
void nxvideo_player_on_frame(nxvideo_player_t player,
                              nxvideo_frame_cb callback,
                              void *userdata);

/**
 * Set state change callback
 */
void nxvideo_player_on_state(nxvideo_player_t player,
                              nxvideo_state_cb callback,
                              void *userdata);

/**
 * Set error callback
 */
void nxvideo_player_on_error(nxvideo_player_t player,
                              nxvideo_error_cb callback,
                              void *userdata);

/**
 * Set progress callback
 */
void nxvideo_player_on_progress(nxvideo_player_t player,
                                 nxvideo_progress_cb callback,
                                 void *userdata);

/* ===========================================================================
 * Update API (call from render loop)
 * =========================================================================*/

/**
 * Update player (decode, sync, etc.)
 * Call this every frame from render loop
 */
nxvideo_error_t nxvideo_player_update(nxvideo_player_t player);

/* ===========================================================================
 * A/V Sync API
 * =========================================================================*/

/**
 * Set audio clock (for A/V sync with external audio)
 */
nxvideo_error_t nxvideo_player_set_audio_clock(nxvideo_player_t player,
                                                double audio_time);

/**
 * Get video-audio offset
 */
double nxvideo_player_av_offset(nxvideo_player_t player);

/**
 * Set A/V offset adjustment
 */
nxvideo_error_t nxvideo_player_set_av_offset(nxvideo_player_t player,
                                              double offset_ms);

#ifdef __cplusplus
}
#endif

#endif /* NXVIDEO_H */
