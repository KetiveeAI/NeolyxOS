/*
 * NXAudio Kernel Driver (nxaudio.kdrv)
 * 
 * NeolyxOS Native Audio Hardware Abstraction
 * 
 * Architecture:
 *   [ NXAudio Kernel Driver ] ← Ring buffer + IRQ
 *        ↕ Shared Memory
 *   [ nxaudio-server ]        ← User-space mixing
 *        ↕ IPC
 *   [ libnxaudio.so ]         ← App API
 * 
 * Supports: HD Audio (Intel HDA), AC97 (QEMU/legacy)
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXAUDIO_KDRV_H
#define NXAUDIO_KDRV_H

#include <stdint.h>
#include <stddef.h>

/* ============ Driver Metadata ============ */

#define NXAUDIO_KDRV_VERSION    "1.0.0"
#define NXAUDIO_KDRV_ABI        1
#define NXAUDIO_KDRV_ARCH       "x86_64"

/* ============ Hardware Types ============ */

typedef enum {
    NXAUDIO_HW_NONE = 0,
    NXAUDIO_HW_HDA,         /* Intel High Definition Audio */
    NXAUDIO_HW_AC97,        /* AC97 (QEMU, legacy) */
    NXAUDIO_HW_USB,         /* USB Audio Class */
} nxaudio_hw_type_t;

/* ============ Audio Format ============ */

typedef enum {
    NXAUDIO_FMT_S16LE = 0,  /* Signed 16-bit little-endian */
    NXAUDIO_FMT_S24LE,      /* Signed 24-bit (in 32-bit container) */
    NXAUDIO_FMT_S32LE,      /* Signed 32-bit */
    NXAUDIO_FMT_F32LE,      /* Float 32-bit (processing format) */
} nxaudio_format_t;

/* ============ Stream Direction ============ */

typedef enum {
    NXAUDIO_DIR_PLAYBACK = 0,
    NXAUDIO_DIR_CAPTURE,
} nxaudio_direction_t;

/* ============ Stream Descriptor ============ */

typedef struct {
    uint32_t            sample_rate;    /* 44100, 48000, 96000, 192000 */
    uint8_t             channels;       /* 2 = stereo, 6 = 5.1, 8 = 7.1 */
    nxaudio_format_t    format;
    nxaudio_direction_t direction;
    uint32_t            buffer_frames;  /* Frames per DMA buffer */
    uint32_t            period_frames;  /* Frames per IRQ period */
} nxaudio_stream_desc_t;

/* ============ Ring Buffer (Kernel ↔ Server) ============ */

#define NXAUDIO_RING_SIZE       (64 * 1024)  /* 64KB ring buffer */
#define NXAUDIO_RING_PERIODS    4            /* 4 periods for low latency */

typedef struct {
    uint8_t             data[NXAUDIO_RING_SIZE];
    volatile uint32_t   write_pos;      /* Producer position */
    volatile uint32_t   read_pos;       /* Consumer position */
    volatile uint32_t   period_count;   /* Periods completed (IRQ count) */
    volatile uint8_t    running;        /* Stream active */
    volatile uint8_t    underrun;       /* Buffer underrun flag */
    volatile uint8_t    overrun;        /* Buffer overrun flag */
    uint8_t             _pad[1];
} nxaudio_ring_t;

/* ============ Device Info ============ */

typedef struct {
    uint32_t            id;
    char                name[64];
    char                vendor[32];
    nxaudio_hw_type_t   hw_type;
    uint32_t            supported_rates;    /* Bitmask */
    uint8_t             max_channels;
    uint8_t             supports_capture;
    uint8_t             supports_playback;
    uint8_t             is_default;
} nxaudio_device_info_t;

/* Rate bitmask */
#define NXAUDIO_RATE_44100_BIT  (1 << 0)
#define NXAUDIO_RATE_48000_BIT  (1 << 1)
#define NXAUDIO_RATE_96000_BIT  (1 << 2)
#define NXAUDIO_RATE_192000_BIT (1 << 3)

/* ============ Kernel Driver API ============ */

/**
 * Initialize audio hardware
 */
int nxaudio_kdrv_init(void);

/**
 * Shutdown audio hardware
 */
void nxaudio_kdrv_shutdown(void);

/**
 * Get number of audio devices
 */
int nxaudio_kdrv_device_count(void);

/**
 * Get device info
 */
int nxaudio_kdrv_device_info(int index, nxaudio_device_info_t *info);

/**
 * Open stream to device
 * Returns: stream handle, or -1 on error
 */
int nxaudio_kdrv_stream_open(int device, const nxaudio_stream_desc_t *desc);

/**
 * Start stream playback/capture
 */
int nxaudio_kdrv_stream_start(int stream);

/**
 * Stop stream
 */
int nxaudio_kdrv_stream_stop(int stream);

/**
 * Close stream
 */
void nxaudio_kdrv_stream_close(int stream);

/**
 * Get ring buffer pointer (for mmap from userspace)
 */
nxaudio_ring_t* nxaudio_kdrv_get_ring(int stream);

/**
 * Get current hardware position
 */
uint32_t nxaudio_kdrv_get_hw_pos(int stream);

/**
 * Wait for period completion
 */
int nxaudio_kdrv_wait_period(int stream, uint32_t timeout_ms);

/* ============ IRQ Handler ============ */

/**
 * Audio interrupt handler (internal)
 */
void nxaudio_irq_handler(void *ctx);

#endif /* NXAUDIO_KDRV_H */
