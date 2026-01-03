/*
 * NXAudio Core - Initialization
 * 
 * System initialization and global state management
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "nxaudio/nxaudio.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ============ Internal Types ============ */

#define MAX_CONTEXTS    16
#define MAX_BUFFERS     256
#define MAX_OBJECTS     512

typedef enum {
    OBJECT_STATE_STOPPED,
    OBJECT_STATE_PLAYING,
    OBJECT_STATE_PAUSED,
} object_state_t;

typedef struct {
    int32_t             id;
    void               *data;
    size_t              size;
    uint32_t            sample_rate;
    uint32_t            channels;
    nxaudio_format_t    format;
    size_t              frames;
    int                 in_use;
} audio_buffer_t;

typedef struct {
    int32_t             id;
    int32_t             ctx_id;
    int32_t             buffer_id;
    nxaudio_object_config_t config;
    object_state_t      state;
    size_t              position;       /* Current playback position */
    float               current_gain;   /* For smoothing */
    int                 in_use;
} audio_object_t;

typedef struct {
    int32_t             id;
    nxaudio_context_config_t config;
    nxaudio_listener_t  listener;
    float               master_gain;
    int                 hrtf_enabled;
    int                 reverb_enabled;
    float               reverb_room_size;
    float               reverb_damping;
    float               reverb_wet;
    float               reverb_dry;
    int                 suspended;
    int                 in_use;
} audio_context_t;

/* ============ Global State ============ */

static struct {
    int             initialized;
    audio_context_t contexts[MAX_CONTEXTS];
    audio_buffer_t  buffers[MAX_BUFFERS];
    audio_object_t  objects[MAX_OBJECTS];
    int32_t         next_ctx_id;
    int32_t         next_buf_id;
    int32_t         next_obj_id;
} g_nxaudio = {0};

/* ============ Helper Functions ============ */

static audio_context_t* find_context(nxaudio_context_t ctx) {
    for (int i = 0; i < MAX_CONTEXTS; i++) {
        if (g_nxaudio.contexts[i].in_use && g_nxaudio.contexts[i].id == ctx) {
            return &g_nxaudio.contexts[i];
        }
    }
    return NULL;
}

static audio_buffer_t* find_buffer(nxaudio_buffer_t buf) {
    for (int i = 0; i < MAX_BUFFERS; i++) {
        if (g_nxaudio.buffers[i].in_use && g_nxaudio.buffers[i].id == buf) {
            return &g_nxaudio.buffers[i];
        }
    }
    return NULL;
}

static audio_object_t* find_object(nxaudio_object_t obj) {
    for (int i = 0; i < MAX_OBJECTS; i++) {
        if (g_nxaudio.objects[i].in_use && g_nxaudio.objects[i].id == obj) {
            return &g_nxaudio.objects[i];
        }
    }
    return NULL;
}

static int find_free_context_slot(void) {
    for (int i = 0; i < MAX_CONTEXTS; i++) {
        if (!g_nxaudio.contexts[i].in_use) return i;
    }
    return -1;
}

static int find_free_buffer_slot(void) {
    for (int i = 0; i < MAX_BUFFERS; i++) {
        if (!g_nxaudio.buffers[i].in_use) return i;
    }
    return -1;
}

static int find_free_object_slot(void) {
    for (int i = 0; i < MAX_OBJECTS; i++) {
        if (!g_nxaudio.objects[i].in_use) return i;
    }
    return -1;
}

/* ============ System API ============ */

nxaudio_error_t nxaudio_init(void) {
    if (g_nxaudio.initialized) {
        return NXAUDIO_SUCCESS;
    }
    
    memset(&g_nxaudio, 0, sizeof(g_nxaudio));
    g_nxaudio.next_ctx_id = 1;
    g_nxaudio.next_buf_id = 1;
    g_nxaudio.next_obj_id = 1;
    g_nxaudio.initialized = 1;
    
    fprintf(stderr, "[NXAudio] Initialized v%s\n", NXAUDIO_VERSION_STRING);
    
    return NXAUDIO_SUCCESS;
}

void nxaudio_shutdown(void) {
    if (!g_nxaudio.initialized) return;
    
    /* Free all buffers */
    for (int i = 0; i < MAX_BUFFERS; i++) {
        if (g_nxaudio.buffers[i].in_use && g_nxaudio.buffers[i].data) {
            free(g_nxaudio.buffers[i].data);
        }
    }
    
    memset(&g_nxaudio, 0, sizeof(g_nxaudio));
    
    fprintf(stderr, "[NXAudio] Shutdown complete\n");
}

const char* nxaudio_version(void) {
    return NXAUDIO_VERSION_STRING;
}

const char* nxaudio_error_string(nxaudio_error_t error) {
    switch (error) {
        case NXAUDIO_SUCCESS:       return "Success";
        case NXAUDIO_ERROR_INIT:    return "Initialization error";
        case NXAUDIO_ERROR_INVALID: return "Invalid parameter";
        case NXAUDIO_ERROR_NO_MEMORY: return "Out of memory";
        case NXAUDIO_ERROR_NO_DEVICE: return "No audio device";
        case NXAUDIO_ERROR_IO:      return "I/O error";
        case NXAUDIO_ERROR_FORMAT:  return "Invalid format";
        case NXAUDIO_ERROR_TIMEOUT: return "Timeout";
        case NXAUDIO_ERROR_BUSY:    return "Resource busy";
        default:                    return "Unknown error";
    }
}

/* ============ Context API ============ */

nxaudio_context_t nxaudio_context_create(void) {
    nxaudio_context_config_t config = {
        .sample_rate = 48000,
        .buffer_size = 1024,
        .max_objects = 128,
        .distance_model = NXAUDIO_DISTANCE_INVERSE,
        .doppler_factor = 1.0f,
        .speed_of_sound = 343.0f,
    };
    return nxaudio_context_create_ex(&config);
}

nxaudio_context_t nxaudio_context_create_ex(const nxaudio_context_config_t *config) {
    if (!g_nxaudio.initialized) {
        return NXAUDIO_INVALID_HANDLE;
    }
    
    int slot = find_free_context_slot();
    if (slot < 0) {
        return NXAUDIO_INVALID_HANDLE;
    }
    
    audio_context_t *ctx = &g_nxaudio.contexts[slot];
    memset(ctx, 0, sizeof(*ctx));
    
    ctx->id = g_nxaudio.next_ctx_id++;
    ctx->config = *config;
    ctx->master_gain = 1.0f;
    ctx->hrtf_enabled = 1;
    ctx->reverb_enabled = 0;
    ctx->reverb_room_size = 0.5f;
    ctx->reverb_damping = 0.5f;
    ctx->reverb_wet = 0.3f;
    ctx->reverb_dry = 1.0f;
    ctx->suspended = 0;
    ctx->in_use = 1;
    
    /* Default listener */
    ctx->listener.position = (nxaudio_vec3_t){0, 0, 0};
    ctx->listener.velocity = (nxaudio_vec3_t){0, 0, 0};
    ctx->listener.orientation = (nxaudio_quat_t){0, 0, 0, 1};
    ctx->listener.gain = 1.0f;
    
    return ctx->id;
}

void nxaudio_context_destroy(nxaudio_context_t ctx) {
    audio_context_t *c = find_context(ctx);
    if (!c) return;
    
    /* Stop all objects in this context */
    for (int i = 0; i < MAX_OBJECTS; i++) {
        if (g_nxaudio.objects[i].in_use && g_nxaudio.objects[i].ctx_id == ctx) {
            g_nxaudio.objects[i].in_use = 0;
        }
    }
    
    c->in_use = 0;
}

nxaudio_error_t nxaudio_context_set_listener(nxaudio_context_t ctx,
                                              const nxaudio_listener_t *listener) {
    audio_context_t *c = find_context(ctx);
    if (!c) return NXAUDIO_ERROR_INVALID;
    if (!listener) return NXAUDIO_ERROR_INVALID;
    
    c->listener = *listener;
    return NXAUDIO_SUCCESS;
}

nxaudio_error_t nxaudio_context_get_listener(nxaudio_context_t ctx,
                                              nxaudio_listener_t *listener) {
    audio_context_t *c = find_context(ctx);
    if (!c) return NXAUDIO_ERROR_INVALID;
    if (!listener) return NXAUDIO_ERROR_INVALID;
    
    *listener = c->listener;
    return NXAUDIO_SUCCESS;
}

nxaudio_error_t nxaudio_context_set_gain(nxaudio_context_t ctx, float gain) {
    audio_context_t *c = find_context(ctx);
    if (!c) return NXAUDIO_ERROR_INVALID;
    
    c->master_gain = gain < 0.0f ? 0.0f : gain;
    return NXAUDIO_SUCCESS;
}

nxaudio_error_t nxaudio_context_suspend(nxaudio_context_t ctx) {
    audio_context_t *c = find_context(ctx);
    if (!c) return NXAUDIO_ERROR_INVALID;
    
    c->suspended = 1;
    return NXAUDIO_SUCCESS;
}

nxaudio_error_t nxaudio_context_resume(nxaudio_context_t ctx) {
    audio_context_t *c = find_context(ctx);
    if (!c) return NXAUDIO_ERROR_INVALID;
    
    c->suspended = 0;
    return NXAUDIO_SUCCESS;
}

/* ============ Buffer API ============ */

nxaudio_buffer_t nxaudio_buffer_create(const char *filepath) {
    if (!g_nxaudio.initialized || !filepath) {
        return NXAUDIO_INVALID_HANDLE;
    }
    
    /* TODO: Implement file loading (WAV, FLAC, OGG) */
    /* For now, return placeholder */
    
    int slot = find_free_buffer_slot();
    if (slot < 0) return NXAUDIO_INVALID_HANDLE;
    
    audio_buffer_t *buf = &g_nxaudio.buffers[slot];
    buf->id = g_nxaudio.next_buf_id++;
    buf->data = NULL;
    buf->size = 0;
    buf->sample_rate = 48000;
    buf->channels = 2;
    buf->format = NXAUDIO_FORMAT_F32;
    buf->frames = 0;
    buf->in_use = 1;
    
    return buf->id;
}

nxaudio_buffer_t nxaudio_buffer_create_mem(const void *data, size_t size,
                                            nxaudio_format_t format,
                                            uint32_t sample_rate,
                                            uint32_t channels) {
    if (!g_nxaudio.initialized || !data || size == 0) {
        return NXAUDIO_INVALID_HANDLE;
    }
    
    int slot = find_free_buffer_slot();
    if (slot < 0) return NXAUDIO_INVALID_HANDLE;
    
    void *copy = malloc(size);
    if (!copy) return NXAUDIO_INVALID_HANDLE;
    memcpy(copy, data, size);
    
    audio_buffer_t *buf = &g_nxaudio.buffers[slot];
    buf->id = g_nxaudio.next_buf_id++;
    buf->data = copy;
    buf->size = size;
    buf->sample_rate = sample_rate;
    buf->channels = channels;
    buf->format = format;
    
    /* Calculate frames */
    size_t sample_size = 4; /* F32 */
    if (format == NXAUDIO_FORMAT_S16) sample_size = 2;
    else if (format == NXAUDIO_FORMAT_S24) sample_size = 3;
    else if (format == NXAUDIO_FORMAT_S32) sample_size = 4;
    
    buf->frames = size / (sample_size * channels);
    buf->in_use = 1;
    
    return buf->id;
}

void nxaudio_buffer_destroy(nxaudio_buffer_t buffer) {
    audio_buffer_t *buf = find_buffer(buffer);
    if (!buf) return;
    
    if (buf->data) {
        free(buf->data);
    }
    buf->in_use = 0;
}

float nxaudio_buffer_duration(nxaudio_buffer_t buffer) {
    audio_buffer_t *buf = find_buffer(buffer);
    if (!buf || buf->sample_rate == 0) return 0.0f;
    
    return (float)buf->frames / (float)buf->sample_rate;
}

/* ============ Object API ============ */

nxaudio_object_t nxaudio_object_create(nxaudio_context_t ctx,
                                        nxaudio_buffer_t buffer,
                                        const nxaudio_object_config_t *config) {
    if (!g_nxaudio.initialized) return NXAUDIO_INVALID_HANDLE;
    
    audio_context_t *c = find_context(ctx);
    if (!c) return NXAUDIO_INVALID_HANDLE;
    
    audio_buffer_t *buf = find_buffer(buffer);
    if (!buf) return NXAUDIO_INVALID_HANDLE;
    
    int slot = find_free_object_slot();
    if (slot < 0) return NXAUDIO_INVALID_HANDLE;
    
    audio_object_t *obj = &g_nxaudio.objects[slot];
    obj->id = g_nxaudio.next_obj_id++;
    obj->ctx_id = ctx;
    obj->buffer_id = buffer;
    obj->state = OBJECT_STATE_STOPPED;
    obj->position = 0;
    obj->current_gain = config ? config->gain : 1.0f;
    obj->in_use = 1;
    
    if (config) {
        obj->config = *config;
    } else {
        obj->config.position = (nxaudio_vec3_t){0, 0, 0};
        obj->config.velocity = (nxaudio_vec3_t){0, 0, 0};
        obj->config.gain = 1.0f;
        obj->config.pitch = 1.0f;
        obj->config.min_distance = 1.0f;
        obj->config.max_distance = 100.0f;
        obj->config.rolloff = 1.0f;
        obj->config.looping = 0;
        obj->config.spatial = 1;
        obj->config.reverb = 1;
        obj->config.occlusion = 0;
    }
    
    return obj->id;
}

void nxaudio_object_destroy(nxaudio_object_t obj) {
    audio_object_t *o = find_object(obj);
    if (!o) return;
    o->in_use = 0;
}

nxaudio_error_t nxaudio_object_play(nxaudio_object_t obj) {
    audio_object_t *o = find_object(obj);
    if (!o) return NXAUDIO_ERROR_INVALID;
    
    o->state = OBJECT_STATE_PLAYING;
    return NXAUDIO_SUCCESS;
}

nxaudio_error_t nxaudio_object_pause(nxaudio_object_t obj) {
    audio_object_t *o = find_object(obj);
    if (!o) return NXAUDIO_ERROR_INVALID;
    
    o->state = OBJECT_STATE_PAUSED;
    return NXAUDIO_SUCCESS;
}

nxaudio_error_t nxaudio_object_stop(nxaudio_object_t obj) {
    audio_object_t *o = find_object(obj);
    if (!o) return NXAUDIO_ERROR_INVALID;
    
    o->state = OBJECT_STATE_STOPPED;
    o->position = 0;
    return NXAUDIO_SUCCESS;
}

nxaudio_error_t nxaudio_object_update(nxaudio_object_t obj,
                                       const nxaudio_object_config_t *config) {
    audio_object_t *o = find_object(obj);
    if (!o || !config) return NXAUDIO_ERROR_INVALID;
    
    o->config = *config;
    return NXAUDIO_SUCCESS;
}

nxaudio_error_t nxaudio_object_set_position(nxaudio_object_t obj,
                                             float x, float y, float z) {
    audio_object_t *o = find_object(obj);
    if (!o) return NXAUDIO_ERROR_INVALID;
    
    o->config.position.x = x;
    o->config.position.y = y;
    o->config.position.z = z;
    return NXAUDIO_SUCCESS;
}

nxaudio_error_t nxaudio_object_set_gain(nxaudio_object_t obj, float gain) {
    audio_object_t *o = find_object(obj);
    if (!o) return NXAUDIO_ERROR_INVALID;
    
    o->config.gain = gain < 0.0f ? 0.0f : gain;
    return NXAUDIO_SUCCESS;
}

int nxaudio_object_is_playing(nxaudio_object_t obj) {
    audio_object_t *o = find_object(obj);
    if (!o) return 0;
    return o->state == OBJECT_STATE_PLAYING;
}

/* ============ HRTF API ============ */

nxaudio_error_t nxaudio_hrtf_load(nxaudio_context_t ctx, const char *filepath) {
    audio_context_t *c = find_context(ctx);
    if (!c || !filepath) return NXAUDIO_ERROR_INVALID;
    
    /* TODO: Load SOFA HRTF file */
    c->hrtf_enabled = 1;
    return NXAUDIO_SUCCESS;
}

nxaudio_error_t nxaudio_hrtf_load_default(nxaudio_context_t ctx) {
    audio_context_t *c = find_context(ctx);
    if (!c) return NXAUDIO_ERROR_INVALID;
    
    /* Use built-in default HRTF */
    c->hrtf_enabled = 1;
    return NXAUDIO_SUCCESS;
}

nxaudio_error_t nxaudio_hrtf_info(nxaudio_context_t ctx, nxaudio_hrtf_info_t *info) {
    audio_context_t *c = find_context(ctx);
    if (!c || !info) return NXAUDIO_ERROR_INVALID;
    
    strncpy(info->name, "Default HRTF", sizeof(info->name) - 1);
    info->sample_rate = 48000;
    info->ir_length = 256;
    info->num_positions = 1250;
    
    return NXAUDIO_SUCCESS;
}

nxaudio_error_t nxaudio_hrtf_enable(nxaudio_context_t ctx, int enabled) {
    audio_context_t *c = find_context(ctx);
    if (!c) return NXAUDIO_ERROR_INVALID;
    
    c->hrtf_enabled = enabled ? 1 : 0;
    return NXAUDIO_SUCCESS;
}

/* ============ Reverb API ============ */

nxaudio_error_t nxaudio_reverb_set(nxaudio_context_t ctx,
                                    float room_size,
                                    float damping,
                                    float wet,
                                    float dry) {
    audio_context_t *c = find_context(ctx);
    if (!c) return NXAUDIO_ERROR_INVALID;
    
    c->reverb_room_size = room_size;
    c->reverb_damping = damping;
    c->reverb_wet = wet;
    c->reverb_dry = dry;
    
    return NXAUDIO_SUCCESS;
}

nxaudio_error_t nxaudio_reverb_enable(nxaudio_context_t ctx, int enabled) {
    audio_context_t *c = find_context(ctx);
    if (!c) return NXAUDIO_ERROR_INVALID;
    
    c->reverb_enabled = enabled ? 1 : 0;
    return NXAUDIO_SUCCESS;
}
