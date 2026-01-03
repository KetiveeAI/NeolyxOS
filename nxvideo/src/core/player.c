/*
 * NXVideo Player Implementation
 * 
 * High-level video playback with A/V sync and callbacks.
 */

#define _DEFAULT_SOURCE  /* For usleep */
#define _POSIX_C_SOURCE 200809L

#include "nxvideo/nxvideo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

/* ============ Player State ============ */
typedef struct {
    int                     id;
    nxvideo_state_t         state;
    nxvideo_player_config_t config;
    nxvideo_media_info_t    media_info;
    
    /* Source */
    char                    url[1024];
    int                     is_file;
    
    /* Playback */
    double                  position;
    double                  duration;
    double                  start_time;
    double                  pause_time;
    
    /* Decoder */
    nxvideo_decoder_t       video_decoder;
    nxvideo_hw_type_t       hw_type;
    
    /* Current frame */
    nxvideo_frame_info_t    current_frame;
    uint32_t                current_texture;
    int                     has_frame;
    
    /* A/V Sync */
    double                  audio_clock;
    double                  av_offset;
    
    /* Callbacks */
    nxvideo_frame_cb        on_frame;
    void                   *on_frame_data;
    nxvideo_state_cb        on_state;
    void                   *on_state_data;
    nxvideo_error_cb        on_error;
    void                   *on_error_data;
    nxvideo_progress_cb     on_progress;
    void                   *on_progress_data;
    
    /* Threading */
    pthread_mutex_t         mutex;
    pthread_t               decode_thread;
    int                     decode_running;
    
} player_state_t;

/* ============ Player Pool ============ */
#define MAX_PLAYERS 16
static player_state_t g_players[MAX_PLAYERS];
static int g_next_player_id = 1;

/* ============ Internal Helpers ============ */
static double get_time_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static player_state_t* get_player(nxvideo_player_t id) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (g_players[i].id == id) {
            return &g_players[i];
        }
    }
    return NULL;
}

static void set_state(player_state_t *p, nxvideo_state_t state) {
    if (p->state != state) {
        p->state = state;
        if (p->on_state) {
            p->on_state(p->on_state_data, state);
        }
    }
}

/* ============ Decode Thread ============ */
static void* decode_thread_func(void *arg) {
    player_state_t *p = (player_state_t*)arg;
    
    printf("[NXVideo] Decode thread started for player %d\n", p->id);
    
    while (p->decode_running) {
        if (p->state != NXVIDEO_STATE_PLAYING) {
            usleep(10000);  /* 10ms */
            continue;
        }
        
        /* In real implementation:
         * 1. Read next packet from container
         * 2. Send to decoder
         * 3. Receive decoded frame
         * 4. Store in current_frame
         */
        
        /* Simulate decode time */
        usleep(16666);  /* ~60 FPS */
        
        pthread_mutex_lock(&p->mutex);
        
        /* Update position based on time elapsed */
        if (p->state == NXVIDEO_STATE_PLAYING) {
            double now = get_time_seconds();
            double elapsed = now - p->start_time;
            p->position = elapsed * p->config.playback_rate;
            
            /* Check for end */
            if (p->position >= p->duration) {
                if (p->config.loop) {
                    p->position = 0;
                    p->start_time = now;
                } else {
                    set_state(p, NXVIDEO_STATE_ENDED);
                }
            }
            
            /* Callback */
            if (p->on_progress) {
                p->on_progress(p->on_progress_data, p->position, p->duration);
            }
        }
        
        pthread_mutex_unlock(&p->mutex);
    }
    
    printf("[NXVideo] Decode thread stopped for player %d\n", p->id);
    return NULL;
}

/* ===========================================================================
 * Player API
 * =========================================================================*/

nxvideo_player_t nxvideo_player_create(const nxvideo_player_config_t *config) {
    /* Find free slot */
    player_state_t *p = NULL;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (g_players[i].id == 0) {
            p = &g_players[i];
            break;
        }
    }
    
    if (!p) {
        fprintf(stderr, "[NXVideo] Max players reached\n");
        return NXVIDEO_INVALID_HANDLE;
    }
    
    memset(p, 0, sizeof(player_state_t));
    p->id = g_next_player_id++;
    p->state = NXVIDEO_STATE_STOPPED;
    
    /* Default config */
    p->config.hw_decode = 1;
    p->config.loop = 0;
    p->config.volume = 1.0f;
    p->config.playback_rate = 1.0f;
    p->config.muted = 0;
    
    if (config) {
        p->config = *config;
    }
    
    pthread_mutex_init(&p->mutex, NULL);
    
    printf("[NXVideo] Created player %d\n", p->id);
    return p->id;
}

void nxvideo_player_destroy(nxvideo_player_t player) {
    player_state_t *p = get_player(player);
    if (!p) return;
    
    /* Stop decode thread */
    if (p->decode_running) {
        p->decode_running = 0;
        pthread_join(p->decode_thread, NULL);
    }
    
    pthread_mutex_destroy(&p->mutex);
    
    printf("[NXVideo] Destroyed player %d\n", p->id);
    p->id = 0;
}

nxvideo_error_t nxvideo_player_open(nxvideo_player_t player, const char *url) {
    player_state_t *p = get_player(player);
    if (!p) return NXVIDEO_ERROR_INVALID;
    if (!url) return NXVIDEO_ERROR_INVALID;
    
    strncpy(p->url, url, sizeof(p->url) - 1);
    p->is_file = (strncmp(url, "http", 4) != 0);
    
    printf("[NXVideo] Opening: %s\n", url);
    
    /* In real implementation:
     * 1. Open container (MP4, MKV, WebM)
     * 2. Parse streams
     * 3. Select best video/audio tracks
     * 4. Create decoder
     */
    
    /* Simulate loading */
    p->media_info.has_video = 1;
    p->media_info.has_audio = 1;
    p->media_info.duration = 120.0;  /* 2 minutes demo */
    p->media_info.video.codec = NXVIDEO_CODEC_H264;
    p->media_info.video.width = 1920;
    p->media_info.video.height = 1080;
    p->media_info.video.fps = 30.0;
    p->media_info.video.bitrate = 8000000;
    strncpy(p->media_info.video.codec_name, "H.264", 31);
    strncpy(p->media_info.video.profile, "High", 31);
    strncpy(p->media_info.container, "MP4", 31);
    
    p->duration = p->media_info.duration;
    p->position = 0;
    
    /* Select hardware decoder */
    if (p->config.hw_decode) {
        p->hw_type = nxvideo_hw_best(p->media_info.video.codec);
        if (p->hw_type != NXVIDEO_HW_NONE) {
            printf("[NXVideo] Using hardware decoder: %d\n", p->hw_type);
        }
    }
    
    /* Start decode thread */
    p->decode_running = 1;
    pthread_create(&p->decode_thread, NULL, decode_thread_func, p);
    
    set_state(p, NXVIDEO_STATE_PAUSED);
    return NXVIDEO_SUCCESS;
}

nxvideo_error_t nxvideo_player_close(nxvideo_player_t player) {
    player_state_t *p = get_player(player);
    if (!p) return NXVIDEO_ERROR_INVALID;
    
    /* Stop decode thread */
    if (p->decode_running) {
        p->decode_running = 0;
        pthread_join(p->decode_thread, NULL);
    }
    
    p->url[0] = '\0';
    set_state(p, NXVIDEO_STATE_STOPPED);
    
    return NXVIDEO_SUCCESS;
}

nxvideo_error_t nxvideo_player_info(nxvideo_player_t player,
                                     nxvideo_media_info_t *info) {
    player_state_t *p = get_player(player);
    if (!p || !info) return NXVIDEO_ERROR_INVALID;
    
    *info = p->media_info;
    return NXVIDEO_SUCCESS;
}

nxvideo_error_t nxvideo_player_play(nxvideo_player_t player) {
    player_state_t *p = get_player(player);
    if (!p) return NXVIDEO_ERROR_INVALID;
    
    pthread_mutex_lock(&p->mutex);
    
    if (p->state == NXVIDEO_STATE_PAUSED) {
        /* Resume from pause */
        double now = get_time_seconds();
        p->start_time = now - p->position / p->config.playback_rate;
    } else if (p->state == NXVIDEO_STATE_STOPPED ||
               p->state == NXVIDEO_STATE_ENDED) {
        p->position = 0;
        p->start_time = get_time_seconds();
    }
    
    set_state(p, NXVIDEO_STATE_PLAYING);
    
    pthread_mutex_unlock(&p->mutex);
    
    printf("[NXVideo] Play\n");
    return NXVIDEO_SUCCESS;
}

nxvideo_error_t nxvideo_player_pause(nxvideo_player_t player) {
    player_state_t *p = get_player(player);
    if (!p) return NXVIDEO_ERROR_INVALID;
    
    pthread_mutex_lock(&p->mutex);
    set_state(p, NXVIDEO_STATE_PAUSED);
    p->pause_time = get_time_seconds();
    pthread_mutex_unlock(&p->mutex);
    
    printf("[NXVideo] Pause\n");
    return NXVIDEO_SUCCESS;
}

nxvideo_error_t nxvideo_player_stop(nxvideo_player_t player) {
    player_state_t *p = get_player(player);
    if (!p) return NXVIDEO_ERROR_INVALID;
    
    pthread_mutex_lock(&p->mutex);
    set_state(p, NXVIDEO_STATE_STOPPED);
    p->position = 0;
    pthread_mutex_unlock(&p->mutex);
    
    printf("[NXVideo] Stop\n");
    return NXVIDEO_SUCCESS;
}

nxvideo_error_t nxvideo_player_seek(nxvideo_player_t player, double seconds) {
    player_state_t *p = get_player(player);
    if (!p) return NXVIDEO_ERROR_INVALID;
    
    pthread_mutex_lock(&p->mutex);
    
    if (seconds < 0) seconds = 0;
    if (seconds > p->duration) seconds = p->duration;
    
    p->position = seconds;
    
    if (p->state == NXVIDEO_STATE_PLAYING) {
        p->start_time = get_time_seconds() - seconds / p->config.playback_rate;
    }
    
    pthread_mutex_unlock(&p->mutex);
    
    printf("[NXVideo] Seek to %.2f\n", seconds);
    return NXVIDEO_SUCCESS;
}

double nxvideo_player_position(nxvideo_player_t player) {
    player_state_t *p = get_player(player);
    if (!p) return 0;
    return p->position;
}

double nxvideo_player_duration(nxvideo_player_t player) {
    player_state_t *p = get_player(player);
    if (!p) return 0;
    return p->duration;
}

nxvideo_state_t nxvideo_player_state(nxvideo_player_t player) {
    player_state_t *p = get_player(player);
    if (!p) return NXVIDEO_STATE_ERROR;
    return p->state;
}

nxvideo_error_t nxvideo_player_set_volume(nxvideo_player_t player, float volume) {
    player_state_t *p = get_player(player);
    if (!p) return NXVIDEO_ERROR_INVALID;
    
    if (volume < 0) volume = 0;
    if (volume > 1) volume = 1;
    p->config.volume = volume;
    
    return NXVIDEO_SUCCESS;
}

nxvideo_error_t nxvideo_player_set_muted(nxvideo_player_t player, int muted) {
    player_state_t *p = get_player(player);
    if (!p) return NXVIDEO_ERROR_INVALID;
    
    p->config.muted = muted ? 1 : 0;
    return NXVIDEO_SUCCESS;
}

nxvideo_error_t nxvideo_player_set_rate(nxvideo_player_t player, float rate) {
    player_state_t *p = get_player(player);
    if (!p) return NXVIDEO_ERROR_INVALID;
    
    if (rate < 0.25f) rate = 0.25f;
    if (rate > 4.0f) rate = 4.0f;
    
    pthread_mutex_lock(&p->mutex);
    
    /* Adjust start time to maintain position */
    if (p->state == NXVIDEO_STATE_PLAYING) {
        double now = get_time_seconds();
        p->start_time = now - p->position / rate;
    }
    
    p->config.playback_rate = rate;
    pthread_mutex_unlock(&p->mutex);
    
    return NXVIDEO_SUCCESS;
}

nxvideo_error_t nxvideo_player_set_loop(nxvideo_player_t player, int loop) {
    player_state_t *p = get_player(player);
    if (!p) return NXVIDEO_ERROR_INVALID;
    
    p->config.loop = loop ? 1 : 0;
    return NXVIDEO_SUCCESS;
}

/* ===========================================================================
 * Frame API
 * =========================================================================*/

nxvideo_error_t nxvideo_player_current_frame(nxvideo_player_t player,
                                              nxvideo_frame_info_t *frame) {
    player_state_t *p = get_player(player);
    if (!p || !frame) return NXVIDEO_ERROR_INVALID;
    
    pthread_mutex_lock(&p->mutex);
    *frame = p->current_frame;
    frame->pts = p->position;
    pthread_mutex_unlock(&p->mutex);
    
    return NXVIDEO_SUCCESS;
}

nxvideo_error_t nxvideo_player_update(nxvideo_player_t player) {
    player_state_t *p = get_player(player);
    if (!p) return NXVIDEO_ERROR_INVALID;
    
    /* In real implementation:
     * 1. Check for new decoded frame
     * 2. A/V sync
     * 3. Upload to texture if needed
     * 4. Fire callbacks
     */
    
    return NXVIDEO_SUCCESS;
}

/* ===========================================================================
 * Callback API
 * =========================================================================*/

void nxvideo_player_on_frame(nxvideo_player_t player,
                              nxvideo_frame_cb callback,
                              void *userdata) {
    player_state_t *p = get_player(player);
    if (!p) return;
    p->on_frame = callback;
    p->on_frame_data = userdata;
}

void nxvideo_player_on_state(nxvideo_player_t player,
                              nxvideo_state_cb callback,
                              void *userdata) {
    player_state_t *p = get_player(player);
    if (!p) return;
    p->on_state = callback;
    p->on_state_data = userdata;
}

void nxvideo_player_on_error(nxvideo_player_t player,
                              nxvideo_error_cb callback,
                              void *userdata) {
    player_state_t *p = get_player(player);
    if (!p) return;
    p->on_error = callback;
    p->on_error_data = userdata;
}

void nxvideo_player_on_progress(nxvideo_player_t player,
                                 nxvideo_progress_cb callback,
                                 void *userdata) {
    player_state_t *p = get_player(player);
    if (!p) return;
    p->on_progress = callback;
    p->on_progress_data = userdata;
}

/* ===========================================================================
 * A/V Sync API
 * =========================================================================*/

nxvideo_error_t nxvideo_player_set_audio_clock(nxvideo_player_t player,
                                                double audio_time) {
    player_state_t *p = get_player(player);
    if (!p) return NXVIDEO_ERROR_INVALID;
    
    p->audio_clock = audio_time;
    return NXVIDEO_SUCCESS;
}

double nxvideo_player_av_offset(nxvideo_player_t player) {
    player_state_t *p = get_player(player);
    if (!p) return 0;
    
    return p->position - p->audio_clock + p->av_offset;
}

nxvideo_error_t nxvideo_player_set_av_offset(nxvideo_player_t player,
                                              double offset_ms) {
    player_state_t *p = get_player(player);
    if (!p) return NXVIDEO_ERROR_INVALID;
    
    p->av_offset = offset_ms / 1000.0;
    return NXVIDEO_SUCCESS;
}
