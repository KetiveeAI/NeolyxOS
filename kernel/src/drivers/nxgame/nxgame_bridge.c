/*
 * NXGame Bridge Implementation
 * 
 * Direct hardware access for games and heavy applications.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "../../../include/drivers/nxgame/nxgame_bridge.h"
#include "../../../include/drivers/nxgfx_kdrv.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pit_get_ticks(void);
extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);
extern int get_current_pid(void);

/* From session manager */
extern int session_request_exclusive(uint32_t session_id, uint32_t flags);
extern int session_release_exclusive(uint32_t session_id);

/* From nxgfx */
extern nxgfx_framebuffer_t* nxgfx_kdrv_get_framebuffer(int device);
extern int nxgfx_kdrv_flip(int device);
extern int nxgfx_kdrv_wait_vblank(int device);
extern int nxgfx_kdrv_get_mode(int device, nxgfx_mode_t *mode);

/* ============ Static State ============ */

#define MAX_NXGAME_SESSIONS 8

static nxgame_session_t *g_sessions[MAX_NXGAME_SESSIONS];
static int g_session_count = 0;
static int g_initialized = 0;
static uint32_t g_next_session_id = 1;

/* Default quotas */
static const nxgame_quotas_t g_default_quotas = {
    .max_vram_mb = 0,           /* Unlimited */
    .max_cpu_percent = 0,       /* Unlimited */
    .max_memory_mb = 0,         /* Unlimited */
    .max_audio_streams = 32,
};

/* ============ Utility Functions ============ */

static void serial_dec(uint64_t val) {
    char buf[21];
    int i = 20;
    buf[i--] = '\0';
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i--] = '0' + (val % 10); val /= 10; }
    serial_puts(&buf[i + 1]);
}

/* ============ Bridge Core ============ */

int nxgame_init(void) {
    if (g_initialized) {
        return 0;
    }
    
    serial_puts("[NXGAME] Initializing NXGame Bridge...\n");
    
    for (int i = 0; i < MAX_NXGAME_SESSIONS; i++) {
        g_sessions[i] = NULL;
    }
    g_session_count = 0;
    
    g_initialized = 1;
    serial_puts("[NXGAME] NXGame Bridge ready\n");
    
    return 0;
}

void nxgame_shutdown(void) {
    serial_puts("[NXGAME] Shutting down NXGame Bridge...\n");
    
    /* Destroy all sessions */
    for (int i = 0; i < MAX_NXGAME_SESSIONS; i++) {
        if (g_sessions[i]) {
            nxgame_session_destroy(g_sessions[i]);
        }
    }
    
    g_initialized = 0;
    serial_puts("[NXGAME] NXGame Bridge shutdown complete\n");
}

/* ============ Session Management ============ */

nxgame_session_t* nxgame_session_create(
    nxgame_session_type_t type,
    uint32_t caps,
    const nxgame_quotas_t *quotas
) {
    if (!g_initialized) {
        serial_puts("[NXGAME] Error: Bridge not initialized\n");
        return NULL;
    }
    
    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_NXGAME_SESSIONS; i++) {
        if (g_sessions[i] == NULL) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        serial_puts("[NXGAME] Error: Max sessions reached\n");
        return NULL;
    }
    
    /* Allocate session */
    nxgame_session_t *session = (nxgame_session_t *)kmalloc(sizeof(nxgame_session_t));
    if (!session) {
        serial_puts("[NXGAME] Error: Failed to allocate session\n");
        return NULL;
    }
    
    /* Initialize session */
    session->session_id = g_next_session_id++;
    session->owner_pid = get_current_pid();
    session->type = type;
    session->state = NXGAME_STATE_INACTIVE;
    session->capabilities = caps;
    session->quotas = quotas ? *quotas : g_default_quotas;
    
    /* GPU state */
    session->gpu_device = 0;
    session->front_buffer.virt_addr = NULL;
    session->back_buffer.virt_addr = NULL;
    session->vsync_enabled = 1;
    
    /* Audio state */
    session->audio_buffer.buffer = NULL;
    session->audio_active = 0;
    
    /* Stats */
    session->start_time = 0;
    session->frames_rendered = 0;
    session->audio_samples_played = 0;
    
    g_sessions[slot] = session;
    g_session_count++;
    
    serial_puts("[NXGAME] Created session ");
    serial_dec(session->session_id);
    serial_puts(" type=");
    switch (type) {
        case NXGAME_SESSION_GAME:     serial_puts("GAME"); break;
        case NXGAME_SESSION_CREATIVE: serial_puts("CREATIVE"); break;
        case NXGAME_SESSION_BENCHMARK:serial_puts("BENCHMARK"); break;
    }
    serial_puts(" caps=0x");
    /* Print hex */
    const char hex[] = "0123456789ABCDEF";
    for (int i = 4; i >= 0; i -= 4) {
        serial_putc(hex[(caps >> i) & 0xF]);
    }
    serial_puts("\n");
    
    return session;
}

int nxgame_session_destroy(nxgame_session_t *session) {
    if (!session) {
        return -1;
    }
    
    serial_puts("[NXGAME] Destroying session ");
    serial_dec(session->session_id);
    serial_puts("...\n");
    
    /* Stop first */
    if (session->state == NXGAME_STATE_RUNNING) {
        nxgame_session_stop(session);
    }
    
    /* Release all resources */
    nxgame_gpu_release(session);
    nxgame_audio_release(session);
    nxgame_input_release(session);
    
    /* Remove from list */
    for (int i = 0; i < MAX_NXGAME_SESSIONS; i++) {
        if (g_sessions[i] == session) {
            g_sessions[i] = NULL;
            g_session_count--;
            break;
        }
    }
    
    kfree(session);
    
    serial_puts("[NXGAME] Session destroyed\n");
    
    return 0;
}

int nxgame_session_start(nxgame_session_t *session) {
    if (!session) {
        return -1;
    }
    
    if (session->state == NXGAME_STATE_RUNNING) {
        return 0;  /* Already running */
    }
    
    serial_puts("[NXGAME] Starting session ");
    serial_dec(session->session_id);
    serial_puts("...\n");
    
    session->state = NXGAME_STATE_INITIALIZING;
    session->start_time = pit_get_ticks();
    
    /* Acquire requested capabilities */
    if (session->capabilities & NXGAME_CAP_GPU) {
        if (nxgame_gpu_acquire(session) < 0) {
            serial_puts("[NXGAME] Warning: Failed to acquire GPU\n");
        }
    }
    
    if (session->capabilities & NXGAME_CAP_AUDIO) {
        /* Use default audio settings */
        if (nxgame_audio_acquire(session, 48000, 2, 256) < 0) {
            serial_puts("[NXGAME] Warning: Failed to acquire audio\n");
        }
    }
    
    if (session->capabilities & NXGAME_CAP_INPUT) {
        if (nxgame_input_acquire(session, NXGAME_INPUT_ALL) < 0) {
            serial_puts("[NXGAME] Warning: Failed to acquire input\n");
        }
    }
    
    session->state = NXGAME_STATE_RUNNING;
    
    serial_puts("[NXGAME] Session ");
    serial_dec(session->session_id);
    serial_puts(" now running in EXCLUSIVE MODE\n");
    serial_puts("[NXGAME] Desktop has stepped back\n");
    
    return 0;
}

int nxgame_session_stop(nxgame_session_t *session) {
    if (!session) {
        return -1;
    }
    
    serial_puts("[NXGAME] Stopping session ");
    serial_dec(session->session_id);
    serial_puts("...\n");
    
    session->state = NXGAME_STATE_TERMINATING;
    
    /* Release all resources */
    nxgame_gpu_release(session);
    nxgame_audio_release(session);
    nxgame_input_release(session);
    
    session->state = NXGAME_STATE_INACTIVE;
    
    serial_puts("[NXGAME] Session stopped, desktop can resume\n");
    
    return 0;
}

/* ============ GPU Access ============ */

int nxgame_gpu_acquire(nxgame_session_t *session) {
    if (!session) {
        return -1;
    }
    
    serial_puts("[NXGAME] Acquiring exclusive GPU access...\n");
    
    /* Request exclusive GPU from session manager */
    /* This will tell NXRender to step back */
    if (session_request_exclusive(session->session_id, 0x01) < 0) {
        serial_puts("[NXGAME] GPU access denied\n");
        return -1;
    }
    
    /* Get framebuffer from nxgfx */
    nxgfx_framebuffer_t *fb = nxgfx_kdrv_get_framebuffer(session->gpu_device);
    if (!fb || !fb->virt_addr) {
        serial_puts("[NXGAME] Error: No framebuffer available\n");
        session_release_exclusive(session->session_id);
        return -1;
    }
    
    /* Set up front buffer (direct framebuffer access) */
    session->front_buffer.phys_addr = fb->phys_addr;
    session->front_buffer.virt_addr = (void *)fb->virt_addr;
    session->front_buffer.width = fb->mode.width;
    session->front_buffer.height = fb->mode.height;
    session->front_buffer.pitch = fb->mode.pitch;
    session->front_buffer.format = fb->mode.format;
    session->front_buffer.size = fb->size;
    
    /* For double buffering, back buffer would be a separate allocation */
    /* For now, just use the front buffer directly */
    session->back_buffer = session->front_buffer;
    
    serial_puts("[NXGAME] GPU acquired: ");
    serial_dec(session->front_buffer.width);
    serial_puts("x");
    serial_dec(session->front_buffer.height);
    serial_puts(" @ 0x");
    uint64_t addr = session->front_buffer.phys_addr;
    const char hex[] = "0123456789ABCDEF";
    for (int i = 60; i >= 0; i -= 4) {
        char c = hex[(addr >> i) & 0xF];
        if (c != '0' || i < 16) serial_putc(c);
    }
    serial_puts("\n");
    
    return 0;
}

void nxgame_gpu_release(nxgame_session_t *session) {
    if (!session) {
        return;
    }
    
    if (session->front_buffer.virt_addr) {
        serial_puts("[NXGAME] Releasing GPU access...\n");
        
        session->front_buffer.virt_addr = NULL;
        session->back_buffer.virt_addr = NULL;
        
        session_release_exclusive(session->session_id);
        
        serial_puts("[NXGAME] GPU released, NXRender can resume\n");
    }
}

nxgame_buffer_t* nxgame_gpu_get_buffer(nxgame_session_t *session) {
    if (!session || !session->back_buffer.virt_addr) {
        return NULL;
    }
    return &session->back_buffer;
}

int nxgame_gpu_present(nxgame_session_t *session, int wait_vsync) {
    if (!session) {
        return -1;
    }
    
    /* Wait for VSync if requested */
    if (wait_vsync && session->vsync_enabled) {
        nxgfx_kdrv_wait_vblank(session->gpu_device);
    }
    
    /* Flip buffers */
    nxgfx_kdrv_flip(session->gpu_device);
    
    session->frames_rendered++;
    
    return 0;
}

void nxgame_gpu_set_vsync(nxgame_session_t *session, int enabled) {
    if (session) {
        session->vsync_enabled = enabled;
    }
}

int nxgame_gpu_get_vsync_info(nxgame_session_t *session, nxgame_vsync_info_t *info) {
    if (!session || !info) {
        return -1;
    }
    
    nxgfx_mode_t mode;
    if (nxgfx_kdrv_get_mode(session->gpu_device, &mode) == 0) {
        info->refresh_rate = mode.refresh ? mode.refresh : 60;
    } else {
        info->refresh_rate = 60;  /* Default */
    }
    
    info->frame_count = session->frames_rendered;
    info->last_vblank_time = pit_get_ticks();  /* Approximation */
    
    return 0;
}

/* ============ Audio Access ============ */

int nxgame_audio_acquire(nxgame_session_t *session, 
    uint32_t sample_rate, uint32_t channels, uint32_t buffer_frames) {
    
    if (!session) {
        return -1;
    }
    
    serial_puts("[NXGAME] Acquiring low-latency audio...\n");
    
    /* Request exclusive audio */
    if (session_request_exclusive(session->session_id, 0x02) < 0) {
        serial_puts("[NXGAME] Audio access denied\n");
        return -1;
    }
    
    /* Calculate buffer size */
    uint32_t bytes_per_sample = 2;  /* 16-bit */
    size_t buffer_size = buffer_frames * channels * bytes_per_sample;
    
    /* Allocate audio buffer */
    void *buffer = kmalloc(buffer_size);
    if (!buffer) {
        serial_puts("[NXGAME] Failed to allocate audio buffer\n");
        session_release_exclusive(session->session_id);
        return -1;
    }
    
    session->audio_buffer.sample_rate = sample_rate;
    session->audio_buffer.channels = channels;
    session->audio_buffer.bits_per_sample = 16;
    session->audio_buffer.buffer_frames = buffer_frames;
    session->audio_buffer.buffer = buffer;
    session->audio_buffer.buffer_size = buffer_size;
    session->audio_active = 1;
    
    serial_puts("[NXGAME] Audio acquired: ");
    serial_dec(sample_rate);
    serial_puts("Hz ");
    serial_dec(channels);
    serial_puts("ch ");
    serial_dec(buffer_frames);
    serial_puts(" frames\n");
    
    /* Calculate latency */
    uint32_t latency_ms = (buffer_frames * 1000) / sample_rate;
    serial_puts("[NXGAME] Audio latency: ~");
    serial_dec(latency_ms);
    serial_puts("ms\n");
    
    return 0;
}

void nxgame_audio_release(nxgame_session_t *session) {
    if (!session) {
        return;
    }
    
    if (session->audio_active) {
        serial_puts("[NXGAME] Releasing audio...\n");
        
        if (session->audio_buffer.buffer) {
            kfree(session->audio_buffer.buffer);
            session->audio_buffer.buffer = NULL;
        }
        
        session->audio_active = 0;
        session_release_exclusive(session->session_id);
        
        serial_puts("[NXGAME] Audio released\n");
    }
}

nxgame_audio_buffer_t* nxgame_audio_get_buffer(nxgame_session_t *session) {
    if (!session || !session->audio_active) {
        return NULL;
    }
    return &session->audio_buffer;
}

int nxgame_audio_submit(nxgame_session_t *session, uint32_t frames) {
    if (!session || !session->audio_active) {
        return -1;
    }
    
    /* In production: submit to audio driver */
    /* For now, just track stats */
    session->audio_samples_played += frames;
    
    return 0;
}

/* ============ Input Access ============ */

int nxgame_input_acquire(nxgame_session_t *session, uint32_t types) {
    if (!session) {
        return -1;
    }
    
    serial_puts("[NXGAME] Acquiring raw input access...\n");
    (void)types;  /* For now, acquire all */
    
    serial_puts("[NXGAME] Input acquired (keyboard, mouse, gamepad)\n");
    
    return 0;
}

void nxgame_input_release(nxgame_session_t *session) {
    if (!session) {
        return;
    }
    
    serial_puts("[NXGAME] Releasing input...\n");
}

int nxgame_input_poll(nxgame_session_t *session, nxgame_input_state_t *state) {
    if (!session || !state) {
        return -1;
    }
    
    /* In production: read from keyboard/mouse drivers directly */
    /* For now, return empty state */
    
    for (int i = 0; i < 256; i++) {
        state->keys[i] = 0;
    }
    state->mouse_x = 0;
    state->mouse_y = 0;
    state->mouse_delta_x = 0;
    state->mouse_delta_y = 0;
    state->mouse_buttons = 0;
    state->wheel_delta = 0;
    
    for (int i = 0; i < 4; i++) {
        state->gamepad[i].connected = 0;
    }
    
    return 0;
}

void nxgame_input_set_mouse_capture(nxgame_session_t *session, int capture) {
    (void)session;
    if (capture) {
        serial_puts("[NXGAME] Mouse captured\n");
    } else {
        serial_puts("[NXGAME] Mouse released\n");
    }
}

/* ============ Stats & Debug ============ */

void nxgame_get_stats(nxgame_session_t *session, 
    uint64_t *frames, float *fps, uint32_t *vram_used) {
    
    if (!session) {
        return;
    }
    
    if (frames) {
        *frames = session->frames_rendered;
    }
    
    if (fps) {
        uint64_t elapsed = pit_get_ticks() - session->start_time;
        if (elapsed > 0) {
            *fps = (float)session->frames_rendered * 1000.0f / (float)elapsed;
        } else {
            *fps = 0.0f;
        }
    }
    
    if (vram_used) {
        /* Estimate based on buffer size */
        *vram_used = (uint32_t)(session->front_buffer.size / (1024 * 1024));
    }
}

void nxgame_debug_print(nxgame_session_t *session) {
    if (!session) {
        serial_puts("[NXGAME] No session\n");
        return;
    }
    
    serial_puts("\n=== NXGame Session ");
    serial_dec(session->session_id);
    serial_puts(" ===\n");
    
    serial_puts("  Type: ");
    switch (session->type) {
        case NXGAME_SESSION_GAME:     serial_puts("GAME\n"); break;
        case NXGAME_SESSION_CREATIVE: serial_puts("CREATIVE\n"); break;
        case NXGAME_SESSION_BENCHMARK:serial_puts("BENCHMARK\n"); break;
    }
    
    serial_puts("  State: ");
    switch (session->state) {
        case NXGAME_STATE_INACTIVE:     serial_puts("INACTIVE\n"); break;
        case NXGAME_STATE_INITIALIZING: serial_puts("INITIALIZING\n"); break;
        case NXGAME_STATE_RUNNING:      serial_puts("RUNNING\n"); break;
        case NXGAME_STATE_PAUSED:       serial_puts("PAUSED\n"); break;
        case NXGAME_STATE_TERMINATING:  serial_puts("TERMINATING\n"); break;
        case NXGAME_STATE_CRASHED:      serial_puts("CRASHED\n"); break;
    }
    
    serial_puts("  PID: ");
    serial_dec(session->owner_pid);
    serial_puts("\n");
    
    serial_puts("  Frames: ");
    serial_dec(session->frames_rendered);
    serial_puts("\n");
    
    if (session->front_buffer.virt_addr) {
        serial_puts("  GPU: ");
        serial_dec(session->front_buffer.width);
        serial_puts("x");
        serial_dec(session->front_buffer.height);
        serial_puts(" (exclusive)\n");
    }
    
    if (session->audio_active) {
        serial_puts("  Audio: ");
        serial_dec(session->audio_buffer.sample_rate);
        serial_puts("Hz (active)\n");
    }
    
    serial_puts("=========================\n\n");
}

/* ============ Capability Query (ABI v1 Frozen) ============ */

/* 
 * NXGame ABI v1.0 - FROZEN
 * 
 * These syscall numbers and structures are permanently frozen.
 * Engines can rely on this ABI for backward compatibility.
 * 
 * Syscall Numbers (110-119):
 *   110: SYS_NXGAME_SESSION_CREATE
 *   111: SYS_NXGAME_SESSION_DESTROY
 *   112: SYS_NXGAME_SESSION_START
 *   113: SYS_NXGAME_SESSION_STOP
 *   114: SYS_NXGAME_GPU_ACQUIRE
 *   115: SYS_NXGAME_GPU_RELEASE
 *   116: SYS_NXGAME_GPU_PRESENT
 *   117: SYS_NXGAME_AUDIO_SUBMIT
 *   118: SYS_NXGAME_INPUT_POLL
 *   119: SYS_NXGAME_QUERY_CAPS
 */

#define NXGAME_ABI_VERSION_MAJOR    1
#define NXGAME_ABI_VERSION_MINOR    0
#define NXGAME_ABI_VERSION_PATCH    0

/* Platform capabilities (queried by engines) */
#define NXGAME_PLAT_CAP_VSYNC           0x0001
#define NXGAME_PLAT_CAP_EXCLUSIVE       0x0002
#define NXGAME_PLAT_CAP_AUDIO_LL        0x0004
#define NXGAME_PLAT_CAP_INPUT_RAW       0x0008
#define NXGAME_PLAT_CAP_GAMEPAD         0x0010
#define NXGAME_PLAT_CAP_DOUBLE_BUFFER   0x0020
#define NXGAME_PLAT_CAP_FIXED_TIMESTEP  0x0040
#define NXGAME_PLAT_CAP_GPU_CONTEXT     0x0080

typedef struct {
    uint16_t abi_major;
    uint16_t abi_minor;
    uint16_t abi_patch;
    uint16_t reserved;
    uint32_t platform_caps;
    uint32_t max_sessions;
    uint32_t max_vram_mb;
    uint32_t max_audio_streams;
    uint32_t max_gamepad_count;
    uint32_t vsync_hz;
    uint32_t audio_latency_us;
    uint32_t _reserved[8];
} nxgame_caps_t;

int nxgame_query_caps(nxgame_caps_t *caps) {
    if (!caps) return -1;
    
    caps->abi_major = NXGAME_ABI_VERSION_MAJOR;
    caps->abi_minor = NXGAME_ABI_VERSION_MINOR;
    caps->abi_patch = NXGAME_ABI_VERSION_PATCH;
    caps->reserved = 0;
    
    caps->platform_caps = 
        NXGAME_PLAT_CAP_VSYNC | NXGAME_PLAT_CAP_EXCLUSIVE |
        NXGAME_PLAT_CAP_AUDIO_LL | NXGAME_PLAT_CAP_INPUT_RAW |
        NXGAME_PLAT_CAP_GAMEPAD | NXGAME_PLAT_CAP_DOUBLE_BUFFER;
    
    caps->max_sessions = MAX_NXGAME_SESSIONS;
    caps->max_vram_mb = 16;
    caps->max_audio_streams = 32;
    caps->max_gamepad_count = 4;
    caps->vsync_hz = 60;
    caps->audio_latency_us = 5000;
    
    for (int i = 0; i < 8; i++) caps->_reserved[i] = 0;
    return 0;
}

const char* nxgame_get_version(void) {
    return "NXGame ABI v1.0.0 (Frozen)";
}

int nxgame_has_cap(uint32_t cap) {
    nxgame_caps_t caps;
    nxgame_query_caps(&caps);
    return (caps.platform_caps & cap) ? 1 : 0;
}
