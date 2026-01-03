/*
 * NeolyxOS GPU Driver Framework
 * 
 * GPU support for:
 *   - Intel (integrated)
 *   - AMD (Radeon)
 *   - NVIDIA (GeForce)
 * 
 * Features:
 *   - Framebuffer acceleration
 *   - 2D blitting
 *   - Optional 3D (OpenGL-like)
 *   - Multi-monitor
 *   - AI/Compute (always enabled)
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#ifndef NEOLYX_GPU_H
#define NEOLYX_GPU_H

#include <stdint.h>

/* ============ GPU Vendors ============ */

#define PCI_VENDOR_INTEL    0x8086
#define PCI_VENDOR_AMD      0x1002
#define PCI_VENDOR_NVIDIA   0x10DE

/* ============ GPU Types ============ */

typedef enum {
    GPU_TYPE_UNKNOWN = 0,
    GPU_TYPE_INTEL_INTEGRATED,
    GPU_TYPE_INTEL_ARC,
    GPU_TYPE_AMD_RDNA,
    GPU_TYPE_AMD_RDNA2,
    GPU_TYPE_AMD_RDNA3,
    GPU_TYPE_AMD_GCN,
    GPU_TYPE_NVIDIA_TURING,
    GPU_TYPE_NVIDIA_AMPERE,
    GPU_TYPE_NVIDIA_ADA,
    GPU_TYPE_VIRTIO_GPU,
    GPU_TYPE_VMWARE_SVGA,
} gpu_type_t;

/* ============ Display Modes ============ */

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t refresh_rate;      /* Hz */
    uint32_t bpp;               /* Bits per pixel */
} gpu_mode_t;

/* ============ Framebuffer ============ */

typedef struct {
    uint64_t phys_addr;
    volatile uint32_t *virt_addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;             /* Bytes per row */
    uint32_t bpp;
    uint32_t format;            /* Pixel format */
} gpu_framebuffer_t;

/* Pixel formats */
#define GPU_FORMAT_XRGB8888     0x01
#define GPU_FORMAT_ARGB8888     0x02
#define GPU_FORMAT_RGB565       0x03
#define GPU_FORMAT_XBGR8888     0x04

/* ============ GPU Device ============ */

typedef struct gpu_device {
    char name[64];
    gpu_type_t type;
    uint16_t vendor_id;
    uint16_t device_id;
    
    /* PCI */
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    
    /* MMIO */
    volatile uint8_t *mmio_base;
    uint64_t mmio_size;
    
    /* VRAM */
    uint64_t vram_base;
    uint64_t vram_size;
    
    /* Current mode */
    gpu_mode_t current_mode;
    gpu_framebuffer_t framebuffer;
    
    /* Capabilities */
    int supports_3d;
    int supports_compute;
    int supports_vulkan;
    int max_texture_size;
    int max_render_targets;
    
    /* Multi-monitor */
    int num_outputs;
    int active_outputs;
    
    /* Power state */
    int powered_on;
    int suspended;
    
    struct gpu_device *next;
} gpu_device_t;

/* ============ GPU Operations ============ */

typedef struct {
    int (*init)(gpu_device_t *dev);
    void (*shutdown)(gpu_device_t *dev);
    
    int (*set_mode)(gpu_device_t *dev, gpu_mode_t *mode);
    int (*get_modes)(gpu_device_t *dev, gpu_mode_t *modes, int max);
    
    void (*clear)(gpu_device_t *dev, uint32_t color);
    void (*fill_rect)(gpu_device_t *dev, int x, int y, int w, int h, uint32_t color);
    void (*blit)(gpu_device_t *dev, int dx, int dy, int dw, int dh,
                 const void *src, int sw, int sh, int spitch);
    
    void (*present)(gpu_device_t *dev);
    void (*vsync)(gpu_device_t *dev);
} gpu_ops_t;

/* ============ Intel GPU Devices ============ */

/* Intel Integrated */
#define INTEL_GPU_HD_630        0x5912
#define INTEL_GPU_UHD_630       0x3E92
#define INTEL_GPU_UHD_770       0x4680
#define INTEL_GPU_IRIS_XE       0x9A49
#define INTEL_GPU_IRIS_PLUS     0x8A52

/* Intel Arc */
#define INTEL_GPU_ARC_A770      0x56A0
#define INTEL_GPU_ARC_A750      0x56A1
#define INTEL_GPU_ARC_A380      0x56A5

/* ============ AMD GPU Devices ============ */

/* AMD RDNA3 */
#define AMD_GPU_RX7900XTX       0x744C
#define AMD_GPU_RX7900XT        0x7448
#define AMD_GPU_RX7800XT        0x7480
#define AMD_GPU_RX7600          0x7481  /* Fixed: was same as RX7800XT */

/* AMD RDNA2 */
#define AMD_GPU_RX6900XT        0x73BF
#define AMD_GPU_RX6800XT        0x73AF
#define AMD_GPU_RX6700XT        0x73DF

/* AMD RDNA */
#define AMD_GPU_RX5700XT        0x731F
#define AMD_GPU_RX5600XT        0x7340

/* ============ NVIDIA GPU Devices ============ */

/* NVIDIA Ada Lovelace */
#define NVIDIA_GPU_RTX4090      0x2684
#define NVIDIA_GPU_RTX4080      0x2704
#define NVIDIA_GPU_RTX4070TI    0x2782

/* NVIDIA Ampere */
#define NVIDIA_GPU_RTX3090      0x2204
#define NVIDIA_GPU_RTX3080      0x2206
#define NVIDIA_GPU_RTX3070      0x2484

/* NVIDIA Turing */
#define NVIDIA_GPU_RTX2080TI    0x1E04
#define NVIDIA_GPU_RTX2070      0x1F02

/* ============ GPU API ============ */

/**
 * Initialize GPU subsystem.
 */
int gpu_init(void);

/**
 * Probe for GPUs.
 */
int gpu_probe(void);

/**
 * Get primary GPU.
 */
gpu_device_t *gpu_get_primary(void);

/**
 * Get GPU by index.
 */
gpu_device_t *gpu_get_device(int index);

/**
 * Get number of GPUs.
 */
int gpu_device_count(void);

/**
 * Set display mode.
 */
int gpu_set_mode(gpu_device_t *dev, uint32_t width, uint32_t height, uint32_t hz);

/**
 * Get available modes.
 */
int gpu_get_modes(gpu_device_t *dev, gpu_mode_t *modes, int max);

/**
 * Get framebuffer.
 */
gpu_framebuffer_t *gpu_get_framebuffer(gpu_device_t *dev);

/* ============ 2D Operations ============ */

/**
 * Clear screen.
 */
void gpu_clear(gpu_device_t *dev, uint32_t color);

/**
 * Fill rectangle (hardware accelerated).
 */
void gpu_fill_rect(gpu_device_t *dev, int x, int y, int w, int h, uint32_t color);

/**
 * Blit (copy) image.
 */
void gpu_blit(gpu_device_t *dev, int dx, int dy, int dw, int dh,
              const void *src, int sw, int sh);

/**
 * Present framebuffer to screen.
 */
void gpu_present(gpu_device_t *dev);

/**
 * Wait for vertical sync.
 */
void gpu_vsync(gpu_device_t *dev);

/* ============ Power Management ============ */

/**
 * Suspend GPU (low power).
 */
int gpu_suspend(gpu_device_t *dev);

/**
 * Resume GPU.
 */
int gpu_resume(gpu_device_t *dev);

/* ============ Compute/AI ============ */

/**
 * Check if compute support is available.
 */
int gpu_has_compute(gpu_device_t *dev);

/**
 * Allocate GPU memory.
 */
void *gpu_alloc(gpu_device_t *dev, uint64_t size);

/**
 * Free GPU memory.
 */
void gpu_free(gpu_device_t *dev, void *ptr);

/**
 * Copy to GPU memory.
 */
int gpu_copy_to(gpu_device_t *dev, void *gpu_ptr, const void *cpu_ptr, uint64_t size);

/**
 * Copy from GPU memory.
 */
int gpu_copy_from(gpu_device_t *dev, void *cpu_ptr, const void *gpu_ptr, uint64_t size);

#endif /* NEOLYX_GPU_H */
