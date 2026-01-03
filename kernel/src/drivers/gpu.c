/*
 * NeolyxOS GPU Driver Implementation
 * 
 * GPU detection and basic 2D acceleration.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "drivers/gpu.h"
#include "mm/kheap.h"
#include "mm/pmm.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint32_t fb_width, fb_height, fb_pitch;
extern volatile uint32_t *framebuffer;

/* ============ PCI Access ============ */

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

static void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(0xCF8, addr);
    outl(0xCFC, val);
}

/* ============ State ============ */

static gpu_device_t *gpu_devices = NULL;
static int gpu_count = 0;
static gpu_device_t *primary_gpu = NULL;

/* ============ Helpers ============ */

static void *gpu_memset(void *s, int c, uint64_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static void serial_hex32(uint32_t val) {
    const char *hex = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

/* ============ GPU Type Detection ============ */

static gpu_type_t detect_gpu_type(uint16_t vendor, uint16_t device) {
    if (vendor == PCI_VENDOR_INTEL) {
        switch (device) {
            case INTEL_GPU_ARC_A770:
            case INTEL_GPU_ARC_A750:
            case INTEL_GPU_ARC_A380:
                return GPU_TYPE_INTEL_ARC;
            default:
                return GPU_TYPE_INTEL_INTEGRATED;
        }
    } else if (vendor == PCI_VENDOR_AMD) {
        switch (device) {
            case AMD_GPU_RX7900XTX:
            case AMD_GPU_RX7900XT:
            case AMD_GPU_RX7800XT:
            case AMD_GPU_RX7600:
                return GPU_TYPE_AMD_RDNA3;
            case AMD_GPU_RX6900XT:
            case AMD_GPU_RX6800XT:
            case AMD_GPU_RX6700XT:
                return GPU_TYPE_AMD_RDNA2;
            case AMD_GPU_RX5700XT:
            case AMD_GPU_RX5600XT:
                return GPU_TYPE_AMD_RDNA;
            default:
                return GPU_TYPE_AMD_GCN;
        }
    } else if (vendor == PCI_VENDOR_NVIDIA) {
        switch (device) {
            case NVIDIA_GPU_RTX4090:
            case NVIDIA_GPU_RTX4080:
            case NVIDIA_GPU_RTX4070TI:
                return GPU_TYPE_NVIDIA_ADA;
            case NVIDIA_GPU_RTX3090:
            case NVIDIA_GPU_RTX3080:
            case NVIDIA_GPU_RTX3070:
                return GPU_TYPE_NVIDIA_AMPERE;
            case NVIDIA_GPU_RTX2080TI:
            case NVIDIA_GPU_RTX2070:
                return GPU_TYPE_NVIDIA_TURING;
            default:
                return GPU_TYPE_NVIDIA_TURING;
        }
    }
    
    return GPU_TYPE_UNKNOWN;
}

static const char *gpu_type_name(gpu_type_t type) {
    switch (type) {
        case GPU_TYPE_INTEL_INTEGRATED: return "Intel Integrated";
        case GPU_TYPE_INTEL_ARC: return "Intel Arc";
        case GPU_TYPE_AMD_RDNA: return "AMD RDNA";
        case GPU_TYPE_AMD_RDNA2: return "AMD RDNA2";
        case GPU_TYPE_AMD_RDNA3: return "AMD RDNA3";
        case GPU_TYPE_AMD_GCN: return "AMD GCN";
        case GPU_TYPE_NVIDIA_TURING: return "NVIDIA Turing";
        case GPU_TYPE_NVIDIA_AMPERE: return "NVIDIA Ampere";
        case GPU_TYPE_NVIDIA_ADA: return "NVIDIA Ada";
        case GPU_TYPE_VIRTIO_GPU: return "VirtIO GPU";
        case GPU_TYPE_VMWARE_SVGA: return "VMware SVGA";
        default: return "Unknown";
    }
}

/* ============ Device Initialization ============ */

static int gpu_init_device(gpu_device_t *dev) {
    serial_puts("[GPU] Initializing ");
    serial_puts(gpu_type_name(dev->type));
    serial_puts("...\n");
    
    /* Enable bus mastering and memory space */
    uint32_t cmd = pci_read(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= (1 << 2) | (1 << 1);
    pci_write(dev->bus, dev->slot, dev->func, 0x04, cmd);
    
    /* Get BARs */
    dev->mmio_base = NULL;
    dev->vram_base = 0;
    
    for (int bar = 0; bar < 6; bar++) {
        uint32_t bar_val = pci_read(dev->bus, dev->slot, dev->func, 0x10 + bar * 4);
        if (bar_val == 0) continue;
        
        if (bar_val & 1) {
            /* I/O BAR - skip */
            continue;
        }
        
        uint64_t addr = bar_val & ~0xF;
        
        /* Check if 64-bit BAR */
        if (((bar_val >> 1) & 0x3) == 2) {
            bar++;
            uint32_t bar_high = pci_read(dev->bus, dev->slot, dev->func, 0x10 + bar * 4);
            addr |= ((uint64_t)bar_high << 32);
        }
        
        /* First large BAR is usually VRAM */
        if (dev->vram_base == 0 && addr > 0x100000) {
            dev->vram_base = addr;
        } else if (dev->mmio_base == NULL) {
            dev->mmio_base = (volatile uint8_t *)addr;
        }
    }
    
    /* Set capabilities based on type */
    switch (dev->type) {
        case GPU_TYPE_INTEL_ARC:
        case GPU_TYPE_AMD_RDNA2:
        case GPU_TYPE_AMD_RDNA3:
        case GPU_TYPE_NVIDIA_AMPERE:
        case GPU_TYPE_NVIDIA_ADA:
            dev->supports_3d = 1;
            dev->supports_compute = 1;
            dev->supports_vulkan = 1;
            break;
        default:
            dev->supports_3d = 1;
            dev->supports_compute = 1;
            dev->supports_vulkan = 0;
            break;
    }
    
    dev->max_texture_size = 16384;
    dev->max_render_targets = 8;
    dev->num_outputs = 4;
    dev->active_outputs = 1;
    dev->powered_on = 1;
    
    /* Use existing framebuffer from UEFI */
    dev->framebuffer.virt_addr = framebuffer;
    dev->framebuffer.width = fb_width;
    dev->framebuffer.height = fb_height;
    dev->framebuffer.pitch = fb_pitch;
    dev->framebuffer.bpp = 32;
    dev->framebuffer.format = GPU_FORMAT_XRGB8888;
    
    dev->current_mode.width = fb_width;
    dev->current_mode.height = fb_height;
    dev->current_mode.refresh_rate = 60;
    dev->current_mode.bpp = 32;
    
    serial_puts("[GPU] VRAM: ");
    serial_hex32((uint32_t)(dev->vram_base >> 32));
    serial_hex32((uint32_t)dev->vram_base);
    serial_puts("\n");
    
    return 0;
}

/* ============ GPU API ============ */

int gpu_init(void) {
    serial_puts("[GPU] Initializing GPU subsystem...\n");
    
    gpu_devices = NULL;
    gpu_count = 0;
    primary_gpu = NULL;
    
    return gpu_probe();
}

int gpu_probe(void) {
    serial_puts("[GPU] Scanning for GPUs...\n");
    
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t vendor_device = pci_read(bus, slot, 0, 0);
            uint16_t vendor = vendor_device & 0xFFFF;
            uint16_t device = (vendor_device >> 16) & 0xFFFF;
            
            if (vendor == 0xFFFF) continue;
            
            /* Check class (display controller) */
            uint32_t class_info = pci_read(bus, slot, 0, 8);
            uint8_t base_class = (class_info >> 24) & 0xFF;
            
            if (base_class != 0x03) continue;  /* Not display */
            
            /* Check for supported vendors */
            if (vendor != PCI_VENDOR_INTEL && 
                vendor != PCI_VENDOR_AMD && 
                vendor != PCI_VENDOR_NVIDIA) {
                continue;
            }
            
            serial_puts("[GPU] Found: ");
            serial_hex32((vendor << 16) | device);
            serial_puts("\n");
            
            gpu_device_t *dev = (gpu_device_t *)kzalloc(sizeof(gpu_device_t));
            if (!dev) continue;
            
            dev->vendor_id = vendor;
            dev->device_id = device;
            dev->bus = bus;
            dev->slot = slot;
            dev->func = 0;
            dev->type = detect_gpu_type(vendor, device);
            
            /* Set name */
            const char *type_name = gpu_type_name(dev->type);
            int i = 0;
            while (type_name[i] && i < 63) {
                dev->name[i] = type_name[i];
                i++;
            }
            dev->name[i] = '\0';
            
            if (gpu_init_device(dev) == 0) {
                dev->next = gpu_devices;
                gpu_devices = dev;
                gpu_count++;
                
                if (primary_gpu == NULL) {
                    primary_gpu = dev;
                }
            }
        }
    }
    
    if (gpu_count == 0) {
        serial_puts("[GPU] No dedicated GPU found, using UEFI framebuffer\n");
        
        /* Create virtual GPU device for UEFI framebuffer */
        gpu_device_t *dev = (gpu_device_t *)kzalloc(sizeof(gpu_device_t));
        if (dev) {
            dev->type = GPU_TYPE_UNKNOWN;
            const char *uefi_name = "UEFI Framebuffer";
            int i = 0;
            while (uefi_name[i] && i < 63) {
                dev->name[i] = uefi_name[i];
                i++;
            }
            dev->name[i] = '\0';
            
            dev->framebuffer.virt_addr = framebuffer;
            dev->framebuffer.width = fb_width;
            dev->framebuffer.height = fb_height;
            dev->framebuffer.pitch = fb_pitch;
            dev->framebuffer.bpp = 32;
            
            dev->current_mode.width = fb_width;
            dev->current_mode.height = fb_height;
            dev->current_mode.refresh_rate = 60;
            dev->current_mode.bpp = 32;
            
            dev->supports_compute = 0;
            dev->powered_on = 1;
            
            gpu_devices = dev;
            gpu_count = 1;
            primary_gpu = dev;
        }
    }
    
    serial_puts("[GPU] Ready (");
    serial_putc('0' + gpu_count);
    serial_puts(" devices)\n");
    
    return 0;
}

gpu_device_t *gpu_get_primary(void) {
    return primary_gpu;
}

gpu_device_t *gpu_get_device(int index) {
    gpu_device_t *dev = gpu_devices;
    int i = 0;
    while (dev && i < index) {
        dev = dev->next;
        i++;
    }
    return dev;
}

int gpu_device_count(void) {
    return gpu_count;
}

gpu_framebuffer_t *gpu_get_framebuffer(gpu_device_t *dev) {
    return dev ? &dev->framebuffer : NULL;
}

/* ============ 2D Operations ============ */

void gpu_clear(gpu_device_t *dev, uint32_t color) {
    if (!dev || !dev->framebuffer.virt_addr) return;
    
    volatile uint32_t *fb = dev->framebuffer.virt_addr;
    uint32_t pixels = dev->framebuffer.width * dev->framebuffer.height;
    
    for (uint32_t i = 0; i < pixels; i++) {
        fb[i] = color;
    }
}

void gpu_fill_rect(gpu_device_t *dev, int x, int y, int w, int h, uint32_t color) {
    if (!dev || !dev->framebuffer.virt_addr) return;
    
    volatile uint32_t *fb = dev->framebuffer.virt_addr;
    uint32_t pitch = dev->framebuffer.pitch / 4;
    
    for (int j = y; j < y + h && j < (int)dev->framebuffer.height; j++) {
        if (j < 0) continue;
        for (int i = x; i < x + w && i < (int)dev->framebuffer.width; i++) {
            if (i >= 0) {
                fb[j * pitch + i] = color;
            }
        }
    }
}

void gpu_blit(gpu_device_t *dev, int dx, int dy, int dw, int dh,
              const void *src, int sw, int sh) {
    if (!dev || !dev->framebuffer.virt_addr || !src) return;
    
    volatile uint32_t *fb = dev->framebuffer.virt_addr;
    const uint32_t *s = (const uint32_t *)src;
    uint32_t pitch = dev->framebuffer.pitch / 4;
    
    /* Simple nearest-neighbor scaling */
    for (int j = 0; j < dh && (dy + j) < (int)dev->framebuffer.height; j++) {
        if (dy + j < 0) continue;
        int sy = j * sh / dh;
        
        for (int i = 0; i < dw && (dx + i) < (int)dev->framebuffer.width; i++) {
            if (dx + i < 0) continue;
            int sx = i * sw / dw;
            
            fb[(dy + j) * pitch + (dx + i)] = s[sy * sw + sx];
        }
    }
}

void gpu_present(gpu_device_t *dev) {
    /* With direct framebuffer, no explicit present needed */
    /* For double-buffered modes, would flip buffers here */
    (void)dev;
}

void gpu_vsync(gpu_device_t *dev) {
    /* TODO: Wait for vertical blank interrupt */
    (void)dev;
    for (volatile int i = 0; i < 100000; i++);
}

/* ============ Power Management ============ */

int gpu_suspend(gpu_device_t *dev) {
    if (!dev) return -1;
    dev->suspended = 1;
    serial_puts("[GPU] Suspended\n");
    return 0;
}

int gpu_resume(gpu_device_t *dev) {
    if (!dev) return -1;
    dev->suspended = 0;
    serial_puts("[GPU] Resumed\n");
    return 0;
}

/* ============ Compute/AI ============ */

int gpu_has_compute(gpu_device_t *dev) {
    return dev ? dev->supports_compute : 0;
}

void *gpu_alloc(gpu_device_t *dev, uint64_t size) {
    if (!dev || !dev->supports_compute) return NULL;
    /* TODO: Proper GPU memory allocation */
    return kmalloc(size);
}

void gpu_free(gpu_device_t *dev, void *ptr) {
    if (!dev) return;
    kfree(ptr);
}

int gpu_copy_to(gpu_device_t *dev, void *gpu_ptr, const void *cpu_ptr, uint64_t size) {
    if (!dev || !gpu_ptr || !cpu_ptr) return -1;
    
    uint8_t *d = (uint8_t *)gpu_ptr;
    const uint8_t *s = (const uint8_t *)cpu_ptr;
    while (size--) *d++ = *s++;
    
    return 0;
}

int gpu_copy_from(gpu_device_t *dev, void *cpu_ptr, const void *gpu_ptr, uint64_t size) {
    if (!dev || !cpu_ptr || !gpu_ptr) return -1;
    
    uint8_t *d = (uint8_t *)cpu_ptr;
    const uint8_t *s = (const uint8_t *)gpu_ptr;
    while (size--) *d++ = *s++;
    
    return 0;
}
