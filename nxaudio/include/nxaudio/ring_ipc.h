/*
 * NXAudio Ring Buffer IPC
 * 
 * Shared memory ring buffer for kernel ↔ server communication
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXAUDIO_RING_IPC_H
#define NXAUDIO_RING_IPC_H

#include <stdint.h>
#include <stddef.h>

/* Ring buffer size and layout */
#define NXAUDIO_RING_SIZE       (64 * 1024)  /* 64KB */
#define NXAUDIO_RING_PERIODS    4

/* Ring buffer shared between kernel and server */
typedef struct {
    /* Header (cache-line aligned) */
    volatile uint32_t   magic;          /* 0x4E584155 'NXAU' */
    volatile uint32_t   version;        /* Protocol version */
    volatile uint32_t   sample_rate;
    volatile uint32_t   channels;
    volatile uint32_t   format;         /* Sample format */
    volatile uint32_t   period_frames;
    uint8_t             _pad_header[40];
    
    /* Producer (server writes, kernel reads) - 64-byte aligned */
    volatile uint64_t   write_pos;      /* Write position in bytes */
    volatile uint64_t   write_frames;   /* Total frames written */
    uint8_t             _pad_prod[48];
    
    /* Consumer (kernel reads, updates) - 64-byte aligned */
    volatile uint64_t   read_pos;       /* Read position in bytes */
    volatile uint64_t   read_frames;    /* Total frames consumed */
    volatile uint64_t   hw_pos;         /* Hardware position */
    uint8_t             _pad_cons[40];
    
    /* Status flags - 64-byte aligned */
    volatile uint8_t    running;
    volatile uint8_t    underrun;
    volatile uint8_t    overrun;
    volatile uint8_t    muted;
    volatile uint32_t   period_count;   /* Periods completed */
    volatile uint32_t   error_count;
    uint8_t             _pad_status[48];
    
    /* Audio data buffer */
    uint8_t             data[NXAUDIO_RING_SIZE];
} __attribute__((aligned(64))) nxaudio_ring_ipc_t;

#define NXAUDIO_RING_MAGIC      0x4E584155  /* 'NXAU' */
#define NXAUDIO_RING_VERSION    1

/* ============ Ring Buffer Operations ============ */

/**
 * Initialize ring buffer
 */
static inline void nxaudio_ring_init(nxaudio_ring_ipc_t *ring,
                                      uint32_t sample_rate,
                                      uint32_t channels,
                                      uint32_t format) {
    ring->magic = NXAUDIO_RING_MAGIC;
    ring->version = NXAUDIO_RING_VERSION;
    ring->sample_rate = sample_rate;
    ring->channels = channels;
    ring->format = format;
    ring->period_frames = NXAUDIO_RING_SIZE / (channels * 4 * NXAUDIO_RING_PERIODS);
    ring->write_pos = 0;
    ring->read_pos = 0;
    ring->write_frames = 0;
    ring->read_frames = 0;
    ring->hw_pos = 0;
    ring->running = 0;
    ring->underrun = 0;
    ring->overrun = 0;
    ring->muted = 0;
    ring->period_count = 0;
    ring->error_count = 0;
}

/**
 * Get available space for writing (server)
 */
static inline size_t nxaudio_ring_write_avail(const nxaudio_ring_ipc_t *ring) {
    uint64_t write = ring->write_pos;
    uint64_t read = ring->read_pos;
    
    if (write >= read) {
        return NXAUDIO_RING_SIZE - (write - read) - 1;
    } else {
        return read - write - 1;
    }
}

/**
 * Get available data for reading (kernel)
 */
static inline size_t nxaudio_ring_read_avail(const nxaudio_ring_ipc_t *ring) {
    uint64_t write = ring->write_pos;
    uint64_t read = ring->read_pos;
    
    if (write >= read) {
        return write - read;
    } else {
        return NXAUDIO_RING_SIZE - read + write;
    }
}

/**
 * Write samples to ring buffer (server side)
 */
static inline size_t nxaudio_ring_write(nxaudio_ring_ipc_t *ring,
                                         const void *data, size_t bytes) {
    size_t avail = nxaudio_ring_write_avail(ring);
    if (bytes > avail) bytes = avail;
    if (bytes == 0) return 0;
    
    uint64_t pos = ring->write_pos % NXAUDIO_RING_SIZE;
    size_t first = NXAUDIO_RING_SIZE - pos;
    
    const uint8_t *src = (const uint8_t*)data;
    
    if (first >= bytes) {
        for (size_t i = 0; i < bytes; i++) {
            ring->data[pos + i] = src[i];
        }
    } else {
        for (size_t i = 0; i < first; i++) {
            ring->data[pos + i] = src[i];
        }
        for (size_t i = 0; i < bytes - first; i++) {
            ring->data[i] = src[first + i];
        }
    }
    
    __sync_synchronize();  /* Memory barrier */
    ring->write_pos += bytes;
    
    return bytes;
}

/**
 * Read samples from ring buffer (kernel side)
 */
static inline size_t nxaudio_ring_read(nxaudio_ring_ipc_t *ring,
                                        void *data, size_t bytes) {
    size_t avail = nxaudio_ring_read_avail(ring);
    if (bytes > avail) bytes = avail;
    if (bytes == 0) {
        ring->underrun = 1;
        return 0;
    }
    
    uint64_t pos = ring->read_pos % NXAUDIO_RING_SIZE;
    size_t first = NXAUDIO_RING_SIZE - pos;
    
    uint8_t *dst = (uint8_t*)data;
    
    if (first >= bytes) {
        for (size_t i = 0; i < bytes; i++) {
            dst[i] = ring->data[pos + i];
        }
    } else {
        for (size_t i = 0; i < first; i++) {
            dst[i] = ring->data[pos + i];
        }
        for (size_t i = 0; i < bytes - first; i++) {
            dst[first + i] = ring->data[i];
        }
    }
    
    __sync_synchronize();  /* Memory barrier */
    ring->read_pos += bytes;
    
    return bytes;
}

/**
 * Check if a period is complete
 */
static inline int nxaudio_ring_period_ready(const nxaudio_ring_ipc_t *ring) {
    size_t period_bytes = ring->period_frames * ring->channels * 4;  /* 32-bit float */
    return nxaudio_ring_read_avail(ring) >= period_bytes;
}

#endif /* NXAUDIO_RING_IPC_H */
