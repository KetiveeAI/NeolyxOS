/*
 * NXGraphics Kernel Driver (nxgfx.kdrv)
 * 
 * NeolyxOS Native GPU/Display Kernel Driver
 * 
 * Architecture:
 *   [ NXGfx Kernel Driver ] ← MMIO, DMA, IRQ
 *        ↕ Framebuffer
 *   [ nxrender-server ]      ← User-space compositor
 *        ↕ IPC
 *   [ libnxrender.so ]       ← App API
 * 
 * Supports: UEFI GOP (basic), Intel HD, AMD, NVIDIA (future)
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXGFX_KDRV_H
#define NXGFX_KDRV_H

#include <stdint.h>
#include <stddef.h>

/* ============ Driver Metadata ============ */

#define NXGFX_KDRV_VERSION    "1.0.0"
#define NXGFX_KDRV_ABI        1

/* ============ GPU Types ============ */

typedef enum {
    NXGFX_GPU_NONE = 0,
    NXGFX_GPU_GOP,          /* UEFI GOP framebuffer */
    NXGFX_GPU_VBE,          /* VBE BIOS framebuffer */
    NXGFX_GPU_INTEL,        /* Intel HD Graphics */
    NXGFX_GPU_AMD,          /* AMD/ATI */
    NXGFX_GPU_NVIDIA,       /* NVIDIA */
    NXGFX_GPU_VIRTIO,       /* VirtIO GPU (QEMU) */
    NXGFX_GPU_BOCHS,        /* Bochs VGA (QEMU) */
} nxgfx_gpu_type_t;

/* ============ Pixel Format ============ */

typedef enum {
    NXGFX_FMT_UNKNOWN = 0,
    NXGFX_FMT_RGB888,       /* 24-bit RGB */
    NXGFX_FMT_XRGB8888,     /* 32-bit XRGB, X ignored */
    NXGFX_FMT_ARGB8888,     /* 32-bit ARGB with alpha */
    NXGFX_FMT_BGR888,
    NXGFX_FMT_BGRX8888,
    NXGFX_FMT_BGRA8888,
} nxgfx_format_t;

/* ============ Display Mode ============ */

typedef struct {
    uint32_t        width;
    uint32_t        height;
    uint32_t        pitch;          /* Bytes per scanline */
    uint32_t        bpp;            /* Bits per pixel */
    nxgfx_format_t  format;
    uint32_t        refresh;        /* Refresh rate Hz (0 = unknown) */
} nxgfx_mode_t;

/* ============ Framebuffer Info ============ */

typedef struct {
    uint64_t        phys_addr;      /* Physical address */
    volatile void  *virt_addr;      /* Virtual mapping */
    size_t          size;           /* Total size in bytes */
    nxgfx_mode_t    mode;
} nxgfx_framebuffer_t;

/* ============ GPU Device Info ============ */

typedef struct {
    uint32_t            id;
    char                name[64];
    char                vendor[32];
    nxgfx_gpu_type_t    type;
    uint32_t            vram_mb;        /* VRAM in MB */
    uint8_t             pci_bus;
    uint8_t             pci_dev;
    uint8_t             pci_func;
    uint8_t             is_primary;
    nxgfx_framebuffer_t framebuffer;
} nxgfx_device_info_t;

/* ============ Kernel Driver API ============ */

/**
 * Initialize graphics hardware
 */
int nxgfx_kdrv_init(void);

/**
 * Shutdown graphics
 */
void nxgfx_kdrv_shutdown(void);

/**
 * Get number of GPU devices
 */
int nxgfx_kdrv_device_count(void);

/**
 * Get device info
 */
int nxgfx_kdrv_device_info(int index, nxgfx_device_info_t *info);

/**
 * Get framebuffer for device
 */
nxgfx_framebuffer_t* nxgfx_kdrv_get_framebuffer(int device);

/**
 * Set display mode
 */
int nxgfx_kdrv_set_mode(int device, const nxgfx_mode_t *mode);

/**
 * Get current mode
 */
int nxgfx_kdrv_get_mode(int device, nxgfx_mode_t *mode);

/**
 * Flush framebuffer to display (page flip)
 */
int nxgfx_kdrv_flip(int device);

/**
 * Wait for vertical blank
 */
int nxgfx_kdrv_wait_vblank(int device);

/**
 * Debug output
 */
void nxgfx_kdrv_debug(void);

#endif /* NXGFX_KDRV_H */
