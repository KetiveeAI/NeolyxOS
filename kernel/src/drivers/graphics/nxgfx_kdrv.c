/*
 * NXGraphics Kernel Driver (nxgfx.kdrv)
 * 
 * NeolyxOS Native GPU/Display Kernel Driver
 * 
 * Supports:
 *   - PCI GPU detection (Intel, AMD, NVIDIA, Bochs)
 *   - UEFI GOP framebuffer (TODO)
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "drivers/nxgfx_kdrv.h"
#include <stdint.h>
#include <stddef.h>

/* External kernel interfaces */
extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* PCI access */
extern uint32_t pci_read_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
extern void pci_write_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val);

/* ============ Constants ============ */

#define MAX_GPUS            4

/* PCI Display Class */
#define PCI_CLASS_DISPLAY   0x03

/* Vendor IDs */
#define PCI_VID_INTEL       0x8086
#define PCI_VID_AMD         0x1002
#define PCI_VID_NVIDIA      0x10DE
#define PCI_VID_BOCHS       0x1234

/* Bochs VGA Device ID */
#define BOCHS_VGA_DID       0x1111

/* ============ Driver State ============ */

static struct {
    int                 initialized;
    int                 device_count;
    nxgfx_device_info_t devices[MAX_GPUS];
} g_nxgfx = {0};

/* ============ Helpers ============ */

static void serial_hex32(uint32_t val) {
    const char hex[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

static void serial_dec(uint32_t val) {
    char buf[12];
    int i = 0;
    if (val == 0) {
        serial_putc('0');
        return;
    }
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0) serial_putc(buf[--i]);
}

static inline void nx_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t*)s;
    while (n--) *p++ = (uint8_t)c;
}

static inline void nx_strcpy(char *d, const char *s) {
    while ((*d++ = *s++));
}

/* ============ PCI GPU Detection ============ */

static int detect_pci_gpus(void) {
    int found = 0;
    
    for (int bus = 0; bus < 256 && found < MAX_GPUS; bus++) {
        for (int dev = 0; dev < 32 && found < MAX_GPUS; dev++) {
            for (int func = 0; func < 8 && found < MAX_GPUS; func++) {
                uint32_t id = pci_read_config32(bus, dev, func, 0x00);
                if (id == 0xFFFFFFFF) continue;
                
                uint32_t class_rev = pci_read_config32(bus, dev, func, 0x08);
                uint8_t class_code = (class_rev >> 24) & 0xFF;
                
                if (class_code != PCI_CLASS_DISPLAY) continue;
                
                uint16_t vendor = id & 0xFFFF;
                uint16_t device_id = (id >> 16) & 0xFFFF;
                
                nxgfx_device_info_t *d = &g_nxgfx.devices[found];
                d->id = found;
                d->pci_bus = bus;
                d->pci_dev = dev;
                d->pci_func = func;
                
                if (vendor == PCI_VID_INTEL) {
                    d->type = NXGFX_GPU_INTEL;
                    nx_strcpy(d->vendor, "Intel");
                    nx_strcpy(d->name, "Intel HD Graphics");
                } else if (vendor == PCI_VID_AMD) {
                    d->type = NXGFX_GPU_AMD;
                    nx_strcpy(d->vendor, "AMD");
                    nx_strcpy(d->name, "AMD Radeon");
                } else if (vendor == PCI_VID_NVIDIA) {
                    d->type = NXGFX_GPU_NVIDIA;
                    nx_strcpy(d->vendor, "NVIDIA");
                    nx_strcpy(d->name, "NVIDIA GeForce");
                } else if (vendor == PCI_VID_BOCHS && device_id == BOCHS_VGA_DID) {
                    d->type = NXGFX_GPU_BOCHS;
                    nx_strcpy(d->vendor, "QEMU");
                    nx_strcpy(d->name, "Bochs VGA");
                } else {
                    d->type = NXGFX_GPU_VBE;
                    nx_strcpy(d->vendor, "Generic");
                    nx_strcpy(d->name, "VGA Compatible");
                }
                
                if (found == 0) d->is_primary = 1;
                
                /* Enable bus master and memory */
                uint32_t cmd = pci_read_config32(bus, dev, func, 0x04);
                cmd |= 0x06;
                pci_write_config32(bus, dev, func, 0x04, cmd);
                
                serial_puts("[NXGfx] Found ");
                serial_puts(d->name);
                serial_puts(" @ PCI ");
                serial_dec(bus);
                serial_putc(':');
                serial_dec(dev);
                serial_putc('.');
                serial_dec(func);
                serial_puts("\n");
                
                found++;
            }
        }
    }
    
    return found;
}

/* ============ Public API ============ */

int nxgfx_kdrv_init(void) {
    if (g_nxgfx.initialized) {
        return 0;
    }
    
    serial_puts("[NXGfx] Initializing kernel driver v" NXGFX_KDRV_VERSION "\n");
    
    nx_memset(&g_nxgfx, 0, sizeof(g_nxgfx));
    
    /* Detect PCI GPUs */
    g_nxgfx.device_count = detect_pci_gpus();
    
    if (g_nxgfx.device_count == 0) {
        serial_puts("[NXGfx] No display devices found\n");
        return -1;
    }
    
    serial_puts("[NXGfx] Found ");
    serial_dec(g_nxgfx.device_count);
    serial_puts(" display device(s)\n");
    
    g_nxgfx.initialized = 1;
    return 0;
}

void nxgfx_kdrv_shutdown(void) {
    if (!g_nxgfx.initialized) return;
    g_nxgfx.initialized = 0;
    serial_puts("[NXGfx] Shutdown complete\n");
}

int nxgfx_kdrv_device_count(void) {
    return g_nxgfx.device_count;
}

int nxgfx_kdrv_device_info(int index, nxgfx_device_info_t *info) {
    if (index < 0 || index >= g_nxgfx.device_count || !info) {
        return -1;
    }
    *info = g_nxgfx.devices[index];
    return 0;
}

nxgfx_framebuffer_t* nxgfx_kdrv_get_framebuffer(int device) {
    if (device < 0 || device >= g_nxgfx.device_count) return NULL;
    return &g_nxgfx.devices[device].framebuffer;
}

int nxgfx_kdrv_set_mode(int device, const nxgfx_mode_t *mode) {
    (void)device; (void)mode;
    return -1; /* Not implemented yet */
}

int nxgfx_kdrv_get_mode(int device, nxgfx_mode_t *mode) {
    if (device < 0 || device >= g_nxgfx.device_count || !mode) return -1;
    *mode = g_nxgfx.devices[device].framebuffer.mode;
    return 0;
}

int nxgfx_kdrv_flip(int device) {
    (void)device;
    return 0;
}

int nxgfx_kdrv_wait_vblank(int device) {
    (void)device;
    return 0;
}

void nxgfx_kdrv_debug(void) {
    serial_puts("\n=== NXGfx Debug ===\n");
    serial_puts("Devices: ");
    serial_dec(g_nxgfx.device_count);
    serial_puts("\n");
    
    for (int i = 0; i < g_nxgfx.device_count; i++) {
        nxgfx_device_info_t *d = &g_nxgfx.devices[i];
        serial_puts("  [");
        serial_dec(i);
        serial_puts("] ");
        serial_puts(d->name);
        if (d->is_primary) serial_puts(" (primary)");
        serial_puts("\n");
    }
    serial_puts("===================\n\n");
}

/* ============ GPU Memory Management ============ */

#define VRAM_POOL_SIZE      (4 * 1024 * 1024)   /* 4 MB VRAM pool (aligned with kernel memory map) */
#define MAX_GPU_BUFFERS     64

/* GPU buffer flags */
#define NXGFX_BUF_VERTEX    0x0001
#define NXGFX_BUF_INDEX     0x0002
#define NXGFX_BUF_TEXTURE   0x0004
#define NXGFX_BUF_RENDER    0x0008

typedef struct {
    uint32_t id;
    uint64_t offset;        /* Offset in VRAM pool */
    uint64_t size;
    uint32_t flags;
    int in_use;
} nxgfx_buffer_t;

static struct {
    uint64_t vram_base;     /* Simulated VRAM base (would be BAR in real hw) */
    uint64_t vram_offset;   /* Next free offset */
    nxgfx_buffer_t buffers[MAX_GPU_BUFFERS];
    int buffer_count;
} g_vram = {0};

/* Simulated VRAM (in system memory for now) */
static uint8_t g_vram_pool[VRAM_POOL_SIZE] __attribute__((aligned(4096)));

int nxgfx_vram_init(void) {
    g_vram.vram_base = (uint64_t)g_vram_pool;
    g_vram.vram_offset = 0;
    g_vram.buffer_count = 0;
    
    for (int i = 0; i < MAX_GPU_BUFFERS; i++) {
        g_vram.buffers[i].in_use = 0;
    }
    
    serial_puts("[NXGfx] VRAM initialized: ");
    serial_dec(VRAM_POOL_SIZE / (1024 * 1024));
    serial_puts(" MB\n");
    return 0;
}

nxgfx_buffer_t* nxgfx_alloc_buffer(uint64_t size, uint32_t flags) {
    if (size == 0 || size > VRAM_POOL_SIZE) return NULL;
    
    /* Align size to 64 bytes */
    size = (size + 63) & ~63ULL;
    
    /* Check if we have space */
    if (g_vram.vram_offset + size > VRAM_POOL_SIZE) {
        serial_puts("[NXGfx] VRAM full\n");
        return NULL;
    }
    
    /* Find free buffer slot */
    nxgfx_buffer_t *buf = NULL;
    for (int i = 0; i < MAX_GPU_BUFFERS; i++) {
        if (!g_vram.buffers[i].in_use) {
            buf = &g_vram.buffers[i];
            buf->id = i;
            break;
        }
    }
    
    if (!buf) {
        serial_puts("[NXGfx] No free buffer slots\n");
        return NULL;
    }
    
    buf->offset = g_vram.vram_offset;
    buf->size = size;
    buf->flags = flags;
    buf->in_use = 1;
    
    g_vram.vram_offset += size;
    g_vram.buffer_count++;
    
    return buf;
}

void nxgfx_free_buffer(nxgfx_buffer_t *buf) {
    if (!buf || !buf->in_use) return;
    
    /* Simple free - doesn't compact (would need garbage collection) */
    buf->in_use = 0;
    g_vram.buffer_count--;
}

void* nxgfx_buffer_ptr(nxgfx_buffer_t *buf) {
    if (!buf || !buf->in_use) return NULL;
    return (void*)(g_vram.vram_base + buf->offset);
}

int nxgfx_upload(nxgfx_buffer_t *dst, const void *src, uint64_t size) {
    if (!dst || !src || !dst->in_use) return -1;
    if (size > dst->size) size = dst->size;
    
    uint8_t *d = (uint8_t*)(g_vram.vram_base + dst->offset);
    const uint8_t *s = (const uint8_t*)src;
    
    for (uint64_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
    
    return 0;
}

uint64_t nxgfx_vram_used(void) {
    return g_vram.vram_offset;
}

uint64_t nxgfx_vram_free(void) {
    return VRAM_POOL_SIZE - g_vram.vram_offset;
}

