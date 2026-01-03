/*
 * NXGame Bridge - Direct Hardware Access for Games
 * 
 * Allows games and heavy applications (like Unreal Engine,
 * DaVinci Resolve, After Effects) to bypass the window
 * compositor and access hardware directly.
 * 
 * Architecture:
 *   nxgfx_kdrv (OWNER of GPU)
 *       ├── NXRender (Desktop Client) - Can step back
 *       └── NXGame Bridge (Game Client) - Exclusive mode
 * 
 * Key Features:
 * - Direct framebuffer rendering (no compositor overhead)
 * - Low-latency audio path (~5ms)
 * - Raw input polling (no event queue delay)
 * - Crash isolation (game crash ≠ system crash)
 * - Resource quotas (prevent hogging)
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXGAME_BRIDGE_H
#define NXGAME_BRIDGE_H

#include <stdint.h>
#include "../nxgfx_kdrv.h"

/* ============ Version ============ */

#define NXGAME_VERSION_MAJOR    1
#define NXGAME_VERSION_MINOR    0
#define NXGAME_VERSION_PATCH    0

/* ============ Session Types ============ */

typedef enum {
    NXGAME_SESSION_GAME       = 0,   /* Standard game */
    NXGAME_SESSION_CREATIVE   = 1,   /* Creative app (After Effects, etc.) */
    NXGAME_SESSION_BENCHMARK  = 2,   /* Benchmark/stress test */
} nxgame_session_type_t;

/* ============ Capability Flags ============ */

typedef enum {
    NXGAME_CAP_GPU            = 0x01,  /* Exclusive GPU access */
    NXGAME_CAP_AUDIO          = 0x02,  /* Low-latency audio */
    NXGAME_CAP_INPUT          = 0x04,  /* Raw input polling */
    NXGAME_CAP_VSYNC          = 0x08,  /* VSync control */
    NXGAME_CAP_FULLSCREEN     = 0x10,  /* Fullscreen exclusive */
    NXGAME_CAP_GPU_COMPUTE    = 0x20,  /* GPU compute access */
    
    NXGAME_CAP_ALL            = 0x3F,  /* All capabilities */
} nxgame_capabilities_t;

/* ============ Session State ============ */

typedef enum {
    NXGAME_STATE_INACTIVE     = 0,
    NXGAME_STATE_INITIALIZING = 1,
    NXGAME_STATE_RUNNING      = 2,
    NXGAME_STATE_PAUSED       = 3,
    NXGAME_STATE_TERMINATING  = 4,
    NXGAME_STATE_CRASHED      = 5,
} nxgame_state_t;

/* ============ Resource Quotas ============ */

typedef struct {
    uint32_t max_vram_mb;          /* Max VRAM usage in MB (0 = unlimited) */
    uint32_t max_cpu_percent;      /* Max CPU usage (0-100, 0 = unlimited) */
    uint32_t max_memory_mb;        /* Max system RAM in MB (0 = unlimited) */
    uint32_t max_audio_streams;    /* Max simultaneous audio streams */
} nxgame_quotas_t;

/* ============ GPU Access ============ */

/* Direct framebuffer handle */
typedef struct {
    uint64_t phys_addr;             /* Physical address of buffer */
    void *virt_addr;                /* Virtual address (mapped to userspace) */
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t format;                /* Pixel format */
    size_t size;                    /* Total buffer size */
} nxgame_buffer_t;

/* VSync info */
typedef struct {
    uint32_t refresh_rate;          /* Current refresh rate Hz */
    uint64_t frame_count;           /* Total frames presented */
    uint64_t last_vblank_time;      /* Timestamp of last VBlank */
} nxgame_vsync_info_t;

/* ============ Audio Access ============ */

typedef struct {
    uint32_t sample_rate;           /* Sample rate (44100, 48000, etc.) */
    uint32_t channels;              /* Number of channels */
    uint32_t bits_per_sample;       /* 16, 24, or 32 */
    uint32_t buffer_frames;         /* Frames per buffer */
    void *buffer;                   /* Audio buffer */
    size_t buffer_size;             /* Buffer size in bytes */
} nxgame_audio_buffer_t;

/* ============ Input Access ============ */

typedef enum {
    NXGAME_INPUT_KEYBOARD    = 0x01,
    NXGAME_INPUT_MOUSE       = 0x02,
    NXGAME_INPUT_GAMEPAD     = 0x04,
    NXGAME_INPUT_PEN         = 0x08,
    NXGAME_INPUT_ALL         = 0x0F,
} nxgame_input_type_t;

typedef struct {
    uint8_t keys[256];              /* Key states (1 = pressed) */
    int32_t mouse_x;
    int32_t mouse_y;
    int32_t mouse_delta_x;
    int32_t mouse_delta_y;
    uint8_t mouse_buttons;          /* Bit flags for buttons */
    int16_t wheel_delta;
    
    /* Gamepad (up to 4) */
    struct {
        uint8_t connected;
        uint32_t buttons;           /* Button state bits */
        int16_t left_stick_x;
        int16_t left_stick_y;
        int16_t right_stick_x;
        int16_t right_stick_y;
        uint8_t left_trigger;
        uint8_t right_trigger;
    } gamepad[4];
} nxgame_input_state_t;

/* ============ Session Handle ============ */

typedef struct {
    uint32_t session_id;
    int32_t owner_pid;
    nxgame_session_type_t type;
    nxgame_state_t state;
    uint32_t capabilities;
    nxgame_quotas_t quotas;
    
    /* GPU state */
    int gpu_device;                 /* nxgfx device index */
    nxgame_buffer_t front_buffer;
    nxgame_buffer_t back_buffer;
    int vsync_enabled;
    
    /* Audio state */
    nxgame_audio_buffer_t audio_buffer;
    int audio_active;
    
    /* Stats */
    uint64_t start_time;
    uint64_t frames_rendered;
    uint64_t audio_samples_played;
} nxgame_session_t;

/* ============ Bridge API ============ */

/**
 * nxgame_init - Initialize the NXGame Bridge
 * 
 * Must be called before any other nxgame functions.
 * 
 * Returns: 0 on success, -1 on error
 */
int nxgame_init(void);

/**
 * nxgame_shutdown - Shutdown the NXGame Bridge
 * 
 * Releases all sessions and resources.
 */
void nxgame_shutdown(void);

/* ============ Session Management ============ */

/**
 * nxgame_session_create - Create a new game session
 * 
 * @type: Type of session (game, creative, benchmark)
 * @caps: Requested capabilities
 * @quotas: Resource quotas (NULL for defaults)
 * 
 * Returns: Session pointer on success, NULL on error
 */
nxgame_session_t* nxgame_session_create(
    nxgame_session_type_t type,
    uint32_t caps,
    const nxgame_quotas_t *quotas
);

/**
 * nxgame_session_destroy - Destroy a session
 * 
 * Releases all resources, restores desktop.
 * 
 * @session: Session to destroy
 * 
 * Returns: 0 on success, -1 on error
 */
int nxgame_session_destroy(nxgame_session_t *session);

/**
 * nxgame_session_start - Start exclusive mode
 * 
 * Tells desktop to step back, takes exclusive control.
 * 
 * @session: Session to start
 * 
 * Returns: 0 on success, -1 on error
 */
int nxgame_session_start(nxgame_session_t *session);

/**
 * nxgame_session_stop - Stop exclusive mode
 * 
 * Returns control to desktop.
 * 
 * @session: Session to stop
 * 
 * Returns: 0 on success, -1 on error
 */
int nxgame_session_stop(nxgame_session_t *session);

/* ============ GPU Access ============ */

/**
 * nxgame_gpu_acquire - Acquire exclusive GPU access
 * 
 * @session: Game session
 * 
 * Returns: 0 on success, -1 if denied
 */
int nxgame_gpu_acquire(nxgame_session_t *session);

/**
 * nxgame_gpu_release - Release GPU access
 * 
 * @session: Game session
 */
void nxgame_gpu_release(nxgame_session_t *session);

/**
 * nxgame_gpu_get_buffer - Get the current back buffer for rendering
 * 
 * @session: Game session
 * 
 * Returns: Pointer to back buffer, NULL on error
 */
nxgame_buffer_t* nxgame_gpu_get_buffer(nxgame_session_t *session);

/**
 * nxgame_gpu_present - Present the back buffer (swap buffers)
 * 
 * @session: Game session
 * @wait_vsync: Wait for VSync if true
 * 
 * Returns: 0 on success, -1 on error
 */
int nxgame_gpu_present(nxgame_session_t *session, int wait_vsync);

/**
 * nxgame_gpu_set_vsync - Enable/disable VSync
 * 
 * @session: Game session
 * @enabled: 1 to enable, 0 to disable
 */
void nxgame_gpu_set_vsync(nxgame_session_t *session, int enabled);

/**
 * nxgame_gpu_get_vsync_info - Get VSync timing info
 * 
 * @session: Game session
 * @info: Output structure
 * 
 * Returns: 0 on success, -1 on error
 */
int nxgame_gpu_get_vsync_info(nxgame_session_t *session, nxgame_vsync_info_t *info);

/* ============ Audio Access ============ */

/**
 * nxgame_audio_acquire - Acquire low-latency audio access
 * 
 * @session: Game session
 * @sample_rate: Desired sample rate
 * @channels: Number of channels (1 or 2)
 * @buffer_frames: Frames per buffer (lower = lower latency)
 * 
 * Returns: 0 on success, -1 on error
 */
int nxgame_audio_acquire(nxgame_session_t *session, 
    uint32_t sample_rate, uint32_t channels, uint32_t buffer_frames);

/**
 * nxgame_audio_release - Release audio access
 * 
 * @session: Game session
 */
void nxgame_audio_release(nxgame_session_t *session);

/**
 * nxgame_audio_get_buffer - Get audio buffer for writing
 * 
 * @session: Game session
 * 
 * Returns: Pointer to audio buffer
 */
nxgame_audio_buffer_t* nxgame_audio_get_buffer(nxgame_session_t *session);

/**
 * nxgame_audio_submit - Submit audio buffer for playback
 * 
 * @session: Game session
 * @frames: Number of frames written
 * 
 * Returns: 0 on success, -1 on error
 */
int nxgame_audio_submit(nxgame_session_t *session, uint32_t frames);

/* ============ Input Access ============ */

/**
 * nxgame_input_acquire - Acquire raw input access
 * 
 * @session: Game session
 * @types: Input types to capture (OR of nxgame_input_type_t)
 * 
 * Returns: 0 on success, -1 on error
 */
int nxgame_input_acquire(nxgame_session_t *session, uint32_t types);

/**
 * nxgame_input_release - Release input access
 * 
 * @session: Game session
 */
void nxgame_input_release(nxgame_session_t *session);

/**
 * nxgame_input_poll - Poll current input state
 * 
 * @session: Game session
 * @state: Output structure
 * 
 * Returns: 0 on success, -1 on error
 */
int nxgame_input_poll(nxgame_session_t *session, nxgame_input_state_t *state);

/**
 * nxgame_input_set_mouse_capture - Capture/release mouse
 * 
 * When captured, mouse is hidden and confined to window.
 * 
 * @session: Game session
 * @capture: 1 to capture, 0 to release
 */
void nxgame_input_set_mouse_capture(nxgame_session_t *session, int capture);

/* ============ Stats & Debug ============ */

/**
 * nxgame_get_stats - Get session statistics
 * 
 * @session: Game session
 * @frames: Output: frames rendered (may be NULL)
 * @fps: Output: current FPS (may be NULL)
 * @vram_used: Output: VRAM used in MB (may be NULL)
 */
void nxgame_get_stats(nxgame_session_t *session, 
    uint64_t *frames, float *fps, uint32_t *vram_used);

/**
 * nxgame_debug_print - Print session debug info
 * 
 * @session: Game session
 */
void nxgame_debug_print(nxgame_session_t *session);

#endif /* NXGAME_BRIDGE_H */
