/*
 * NXAudio Server - User-space Audio Daemon
 * 
 * NeolyxOS Native Audio Mixing Server
 * 
 * Architecture:
 *   [ Kernel Driver ] ← Ring buffer + IRQ
 *        ↕ mmap
 *   [ nxaudio-server ] ← Mixing, spatial DSP (THIS FILE)
 *        ↕ Unix sockets / shared memory
 *   [ libnxaudio.so ] ← App API
 * 
 * Responsibilities:
 *   - Connect to kernel ring buffer
 *   - Mix multiple audio streams
 *   - Apply spatial audio (HRTF)
 *   - Handle client connections
 *   - Low-latency buffer management
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXAUDIO_SERVER_H
#define NXAUDIO_SERVER_H

#include <stdint.h>
#include <stddef.h>

/* ============ Server Configuration ============ */

#define NXAUDIO_SERVER_VERSION      "1.0.0"
#define NXAUDIO_SERVER_NAME         "nxaudio-server"

#define NXAUDIO_MAX_CLIENTS         64
#define NXAUDIO_MAX_SOURCES         512
#define NXAUDIO_BLOCK_FRAMES        256
#define NXAUDIO_SAMPLE_RATE         48000

/* IPC Socket Path */
#define NXAUDIO_SOCKET_PATH         "/tmp/nxaudio.sock"

/* Shared memory paths */
#define NXAUDIO_SHM_RING            "/nxaudio-ring"
#define NXAUDIO_SHM_SOURCES         "/nxaudio-sources"

/* ============ IPC Message Types ============ */

typedef enum {
    /* Client → Server */
    NXMSG_CONNECT       = 0x01,
    NXMSG_DISCONNECT    = 0x02,
    NXMSG_SOURCE_CREATE = 0x10,
    NXMSG_SOURCE_DESTROY= 0x11,
    NXMSG_SOURCE_PLAY   = 0x12,
    NXMSG_SOURCE_STOP   = 0x13,
    NXMSG_SOURCE_UPDATE = 0x14,
    NXMSG_LISTENER_SET  = 0x20,
    NXMSG_MASTER_GAIN   = 0x30,
    
    /* Server → Client */
    NXMSG_OK            = 0x80,
    NXMSG_ERROR         = 0x81,
    NXMSG_SOURCE_ID     = 0x82,
    NXMSG_BUFFER_READY  = 0x83,
} nxaudio_msg_type_t;

/* ============ 3D Position ============ */

typedef struct {
    float x, y, z;
} nxaudio_pos3_t;

/* ============ Audio Source (from client) ============ */

typedef struct {
    uint32_t        id;
    uint32_t        client_id;
    nxaudio_pos3_t  position;
    nxaudio_pos3_t  velocity;
    float           gain;
    float           pitch;
    float           min_distance;
    float           max_distance;
    float           rolloff;
    uint8_t         playing;
    uint8_t         looping;
    uint8_t         spatial;
    uint8_t         _pad;
    
    /* Playback state */
    uint32_t        buffer_offset;
    uint32_t        buffer_size;
    void           *buffer_data;
} nxaudio_source_t;

/* ============ Listener ============ */

typedef struct {
    nxaudio_pos3_t  position;
    nxaudio_pos3_t  forward;
    nxaudio_pos3_t  up;
    nxaudio_pos3_t  velocity;
    float           gain;
} nxaudio_listener_t;

/* ============ Client Connection ============ */

typedef struct {
    uint32_t        id;
    int             socket_fd;
    char            name[32];
    uint8_t         connected;
    uint32_t        source_count;
} nxaudio_client_t;

/* ============ Server State ============ */

typedef struct {
    /* Kernel interface */
    int             kernel_fd;
    void           *ring_buffer;
    size_t          ring_size;
    
    /* Audio processing */
    float          *mix_buffer;
    float          *temp_buffer;
    uint32_t        sample_rate;
    uint32_t        channels;
    uint32_t        block_frames;
    
    /* Spatial engine */
    void           *hrtf;
    int             hrtf_enabled;
    
    /* Listener */
    nxaudio_listener_t listener;
    
    /* Clients */
    nxaudio_client_t clients[NXAUDIO_MAX_CLIENTS];
    int             client_count;
    
    /* Sources */
    nxaudio_source_t sources[NXAUDIO_MAX_SOURCES];
    int             source_count;
    
    /* Control */
    float           master_gain;
    int             running;
    int             suspended;
} nxaudio_server_t;

/* ============ Server API ============ */

/**
 * Create server instance
 */
nxaudio_server_t* nxaudio_server_create(void);

/**
 * Destroy server instance
 */
void nxaudio_server_destroy(nxaudio_server_t *server);

/**
 * Initialize and connect to kernel driver
 */
int nxaudio_server_init(nxaudio_server_t *server);

/**
 * Main processing loop (blocking)
 */
int nxaudio_server_run(nxaudio_server_t *server);

/**
 * Stop server
 */
void nxaudio_server_stop(nxaudio_server_t *server);

/**
 * Process one audio block (non-blocking)
 */
int nxaudio_server_process(nxaudio_server_t *server);

/**
 * Handle IPC message
 */
int nxaudio_server_handle_message(nxaudio_server_t *server, 
                                   int client_fd,
                                   const void *data, size_t len);

/* ============ Internal Functions ============ */

/**
 * Mix all active sources to output buffer
 */
void nxaudio_server_mix(nxaudio_server_t *server, 
                         float *output, size_t frames);

/**
 * Apply HRTF to a source
 */
void nxaudio_server_spatialize(nxaudio_server_t *server,
                                nxaudio_source_t *source,
                                const float *mono_in,
                                float *stereo_out,
                                size_t frames);

/**
 * Calculate distance attenuation
 */
float nxaudio_calc_distance_gain(const nxaudio_listener_t *listener,
                                  const nxaudio_source_t *source);

#endif /* NXAUDIO_SERVER_H */
