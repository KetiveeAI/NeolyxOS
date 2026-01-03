/*
 * NXAudio Kernel Driver (nxaudio.kdrv)
 * 
 * NeolyxOS Native Audio Hardware Abstraction Layer
 * 
 * Supports:
 *   - Intel HD Audio (HDA) for real hardware
 *   - AC97 for QEMU testing
 * 
 * Architecture:
 *   This driver provides ring buffers shared with nxaudio-server
 *   Server handles mixing, spatial, DSP in user space
 *   Kernel handles DMA, IRQ, hardware registers
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "drivers/nxaudio_kdrv.h"
#include <stdint.h>
#include <stddef.h>

/* External kernel interfaces */
extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

/* Inline memset for kernel */
static inline void nx_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t*)s;
    while (n--) *p++ = (uint8_t)c;
}

/* PCI access from kernel */
extern uint32_t pci_read_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
extern void pci_write_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val);
extern void pci_write_config16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint16_t val);

/* IRQ handling */
extern void idt_register_handler(uint8_t vector, void (*handler)(void*));

/* ============ Constants ============ */

#define MAX_DEVICES         4
#define MAX_STREAMS         8
#define PAGE_SIZE           4096

/* Intel HDA PCI Class */
#define PCI_CLASS_AUDIO     0x04
#define PCI_SUBCLASS_HDA    0x03

/* AC97 PCI IDs (QEMU) */
#define AC97_VID            0x8086
#define AC97_DID            0x2415

/* HDA Register Offsets */
#define HDA_GCAP            0x00    /* Global Capabilities */
#define HDA_VMIN            0x02    /* Minor Version */
#define HDA_VMAJ            0x03    /* Major Version */
#define HDA_GCTL            0x08    /* Global Control */
#define HDA_WAKEEN          0x0C    /* Wake Enable */
#define HDA_STATESTS        0x0E    /* State Change Status */
#define HDA_INTCTL          0x20    /* Interrupt Control */
#define HDA_INTSTS          0x24    /* Interrupt Status */
#define HDA_CORBLBASE       0x40    /* CORB Lower Base */
#define HDA_CORBUBASE       0x44    /* CORB Upper Base */
#define HDA_CORBWP          0x48    /* CORB Write Pointer */
#define HDA_CORBRP          0x4A    /* CORB Read Pointer */
#define HDA_CORBCTL         0x4C    /* CORB Control */
#define HDA_RIRLBASE        0x50    /* RIRB Lower Base */
#define HDA_RIRBUBASE       0x54    /* RIRB Upper Base */
#define HDA_RIRBWP          0x58    /* RIRB Write Pointer */
#define HDA_RINTCNT         0x5A    /* Response Interrupt Count */
#define HDA_RIRBCTL         0x5C    /* RIRB Control */

/* Stream Descriptor Offsets (from stream base) */
#define HDA_SD_CTL          0x00    /* Stream Descriptor Control */
#define HDA_SD_STS          0x03    /* Stream Descriptor Status */
#define HDA_SD_LPIB         0x04    /* Link Position in Buffer */
#define HDA_SD_CBL          0x08    /* Cyclic Buffer Length */
#define HDA_SD_LVI          0x0C    /* Last Valid Index */
#define HDA_SD_FMT          0x12    /* Format */
#define HDA_SD_BDPL         0x18    /* BDL Pointer Lower */
#define HDA_SD_BDPU         0x1C    /* BDL Pointer Upper */

/* HDA GCTL bits */
#define HDA_GCTL_RESET      (1 << 0)

/* HDA INTCTL bits */
#define HDA_INTCTL_GIE      (1 << 31)   /* Global Interrupt Enable */
#define HDA_INTCTL_CIE      (1 << 30)   /* Controller Interrupt Enable */

/* Stream CTL bits */
#define HDA_SD_CTL_RUN      (1 << 1)
#define HDA_SD_CTL_IOCE     (1 << 2)    /* Interrupt on Completion Enable */
#define HDA_SD_CTL_STRIPE   (1 << 16)

/* ============ Structures ============ */

/* Buffer Descriptor List Entry */
typedef struct {
    uint64_t    address;
    uint32_t    length;
    uint32_t    flags;      /* IOC bit */
} __attribute__((packed)) hda_bdl_entry_t;

/* Audio device state */
typedef struct {
    int                     in_use;
    nxaudio_device_info_t   info;
    volatile uint32_t      *regs;       /* MMIO base */
    uint64_t                bar0;
} nxaudio_device_t;

/* Audio stream state */
typedef struct {
    int                     in_use;
    int                     device_id;
    nxaudio_stream_desc_t   desc;
    nxaudio_ring_t         *ring;
    hda_bdl_entry_t        *bdl;
    void                   *dma_buffer;
    size_t                  dma_size;
    int                     stream_index;
    volatile uint32_t      *sd_regs;    /* Stream descriptor regs */
} nxaudio_stream_t;

/* ============ Driver State ============ */

static struct {
    int                 initialized;
    int                 device_count;
    nxaudio_device_t    devices[MAX_DEVICES];
    nxaudio_stream_t    streams[MAX_STREAMS];
} g_nxaudio = {0};

/* ============ Helper Functions ============ */

static void serial_hex32(uint32_t val) {
    const char hex[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

static void delay_us(int us) {
    for (volatile int i = 0; i < us * 100; i++);
}

/* MMIO read/write */
static inline uint32_t hda_read32(volatile uint32_t *base, uint32_t offset) {
    return *(volatile uint32_t*)((uint8_t*)base + offset);
}

static inline void hda_write32(volatile uint32_t *base, uint32_t offset, uint32_t val) {
    *(volatile uint32_t*)((uint8_t*)base + offset) = val;
}

static inline uint16_t hda_read16(volatile uint32_t *base, uint32_t offset) {
    return *(volatile uint16_t*)((uint8_t*)base + offset);
}

static inline void hda_write16(volatile uint32_t *base, uint32_t offset, uint16_t val) {
    *(volatile uint16_t*)((uint8_t*)base + offset) = val;
}

static inline uint8_t hda_read8(volatile uint32_t *base, uint32_t offset) {
    return *(volatile uint8_t*)((uint8_t*)base + offset);
}

static inline void hda_write8(volatile uint32_t *base, uint32_t offset, uint8_t val) {
    *(volatile uint8_t*)((uint8_t*)base + offset) = val;
}

/* ============ HDA Controller Init ============ */

static int hda_reset_controller(nxaudio_device_t *dev) {
    volatile uint32_t *regs = dev->regs;
    
    /* Enter reset */
    hda_write32(regs, HDA_GCTL, 0);
    delay_us(100);
    
    /* Exit reset */
    hda_write32(regs, HDA_GCTL, HDA_GCTL_RESET);
    
    /* Wait for reset to complete */
    for (int i = 0; i < 1000; i++) {
        if (hda_read32(regs, HDA_GCTL) & HDA_GCTL_RESET) {
            return 0;
        }
        delay_us(100);
    }
    
    serial_puts("[NXAudio] HDA reset timeout\n");
    return -1;
}

static int hda_init_controller(nxaudio_device_t *dev) {
    volatile uint32_t *regs = dev->regs;
    
    /* Read capabilities */
    uint16_t gcap = hda_read16(regs, HDA_GCAP);
    uint8_t vmaj = hda_read8(regs, HDA_VMAJ);
    uint8_t vmin = hda_read8(regs, HDA_VMIN);
    
    serial_puts("[NXAudio] HDA v");
    serial_putc('0' + vmaj);
    serial_putc('.');
    serial_putc('0' + vmin);
    serial_puts(", GCAP=");
    serial_hex32(gcap);
    serial_puts("\n");
    
    /* Reset controller */
    if (hda_reset_controller(dev) != 0) {
        return -1;
    }
    
    /* Enable interrupts */
    hda_write32(regs, HDA_INTCTL, HDA_INTCTL_GIE | HDA_INTCTL_CIE);
    
    /* TODO: Setup CORB/RIRB for codec commands */
    
    return 0;
}

/* ============ Device Detection ============ */

static int detect_hda_devices(void) {
    int found = 0;
    
    /* Scan PCI for audio devices */
    for (int bus = 0; bus < 256 && found < MAX_DEVICES; bus++) {
        for (int dev = 0; dev < 32 && found < MAX_DEVICES; dev++) {
            for (int func = 0; func < 8 && found < MAX_DEVICES; func++) {
                uint32_t id = pci_read_config32(bus, dev, func, 0x00);
                if (id == 0xFFFFFFFF) continue;
                
                uint32_t class_rev = pci_read_config32(bus, dev, func, 0x08);
                uint8_t class_code = (class_rev >> 24) & 0xFF;
                uint8_t subclass = (class_rev >> 16) & 0xFF;
                
                if (class_code == PCI_CLASS_AUDIO && subclass == PCI_SUBCLASS_HDA) {
                    /* Found HDA controller */
                    uint32_t bar0 = pci_read_config32(bus, dev, func, 0x10);
                    uint64_t mmio_base = bar0 & 0xFFFFFFF0;
                    
                    nxaudio_device_t *d = &g_nxaudio.devices[found];
                    d->in_use = 1;
                    d->bar0 = mmio_base;
                    d->regs = (volatile uint32_t*)mmio_base;
                    d->info.id = found;
                    d->info.hw_type = NXAUDIO_HW_HDA;
                    d->info.max_channels = 8;
                    d->info.supports_playback = 1;
                    d->info.supports_capture = 1;
                    d->info.supported_rates = NXAUDIO_RATE_44100_BIT | 
                                               NXAUDIO_RATE_48000_BIT |
                                               NXAUDIO_RATE_96000_BIT;
                    
                    /* Get vendor info from PCI */
                    uint16_t vendor = id & 0xFFFF;
                    uint16_t device_id = (id >> 16) & 0xFFFF;
                    
                    if (vendor == 0x8086) {
                        __builtin_strcpy(d->info.vendor, "Intel");
                        __builtin_strcpy(d->info.name, "Intel HDA");
                    } else if (vendor == 0x1002) {
                        __builtin_strcpy(d->info.vendor, "AMD");
                        __builtin_strcpy(d->info.name, "AMD HDA");
                    } else if (vendor == 0x10DE) {
                        __builtin_strcpy(d->info.vendor, "NVIDIA");
                        __builtin_strcpy(d->info.name, "NVIDIA HDA");
                    } else {
                        __builtin_strcpy(d->info.vendor, "Generic");
                        __builtin_strcpy(d->info.name, "HD Audio");
                    }
                    
                    serial_puts("[NXAudio] Found HDA: ");
                    serial_puts(d->info.name);
                    serial_puts(" @ ");
                    serial_hex32((uint32_t)mmio_base);
                    serial_puts("\n");
                    
                    /* Enable bus master and memory */
                    uint32_t cmd = pci_read_config32(bus, dev, func, 0x04);
                    cmd |= 0x06;  /* Memory + Bus Master */
                    pci_write_config32(bus, dev, func, 0x04, cmd);
                    
                    /* Initialize controller */
                    if (hda_init_controller(d) == 0) {
                        if (found == 0) d->info.is_default = 1;
                        found++;
                    }
                }
                
                /* Check for AC97 (QEMU) */
                uint16_t vendor = id & 0xFFFF;
                uint16_t device_id = (id >> 16) & 0xFFFF;
                
                if (vendor == AC97_VID && device_id == AC97_DID) {
                    nxaudio_device_t *d = &g_nxaudio.devices[found];
                    d->in_use = 1;
                    d->info.id = found;
                    d->info.hw_type = NXAUDIO_HW_AC97;
                    d->info.max_channels = 2;
                    d->info.supports_playback = 1;
                    d->info.supports_capture = 1;
                    d->info.supported_rates = NXAUDIO_RATE_44100_BIT | NXAUDIO_RATE_48000_BIT;
                    __builtin_strcpy(d->info.vendor, "Intel");
                    __builtin_strcpy(d->info.name, "AC97 Audio");
                    
                    serial_puts("[NXAudio] Found AC97 (QEMU)\n");
                    
                    if (found == 0) d->info.is_default = 1;
                    found++;
                }
            }
        }
    }
    
    return found;
}

/* ============ Public API ============ */

int nxaudio_kdrv_init(void) {
    if (g_nxaudio.initialized) {
        return 0;
    }
    
    serial_puts("[NXAudio] Initializing kernel driver v" NXAUDIO_KDRV_VERSION "\n");
    
    /* Clear state */
    nx_memset(&g_nxaudio, 0, sizeof(g_nxaudio));
    
    /* Detect devices */
    g_nxaudio.device_count = detect_hda_devices();
    
    if (g_nxaudio.device_count == 0) {
        serial_puts("[NXAudio] No audio devices found\n");
        return -1;
    }
    
    serial_puts("[NXAudio] Found ");
    serial_putc('0' + g_nxaudio.device_count);
    serial_puts(" audio device(s)\n");
    
    g_nxaudio.initialized = 1;
    return 0;
}

void nxaudio_kdrv_shutdown(void) {
    if (!g_nxaudio.initialized) return;
    
    /* Stop all streams */
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (g_nxaudio.streams[i].in_use) {
            nxaudio_kdrv_stream_close(i);
        }
    }
    
    /* Reset controllers */
    for (int i = 0; i < g_nxaudio.device_count; i++) {
        if (g_nxaudio.devices[i].in_use && g_nxaudio.devices[i].regs) {
            hda_write32(g_nxaudio.devices[i].regs, HDA_GCTL, 0);
        }
    }
    
    g_nxaudio.initialized = 0;
    serial_puts("[NXAudio] Shutdown complete\n");
}

int nxaudio_kdrv_device_count(void) {
    return g_nxaudio.device_count;
}

int nxaudio_kdrv_device_info(int index, nxaudio_device_info_t *info) {
    if (index < 0 || index >= g_nxaudio.device_count || !info) {
        return -1;
    }
    
    *info = g_nxaudio.devices[index].info;
    return 0;
}

int nxaudio_kdrv_stream_open(int device, const nxaudio_stream_desc_t *desc) {
    if (device < 0 || device >= g_nxaudio.device_count || !desc) {
        return -1;
    }
    
    /* Find free stream slot */
    int slot = -1;
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (!g_nxaudio.streams[i].in_use) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        serial_puts("[NXAudio] No free stream slots\n");
        return -1;
    }
    
    nxaudio_stream_t *stream = &g_nxaudio.streams[slot];
    stream->in_use = 1;
    stream->device_id = device;
    stream->desc = *desc;
    
    /* Allocate ring buffer */
    stream->ring = (nxaudio_ring_t*)kmalloc(sizeof(nxaudio_ring_t));
    if (!stream->ring) {
        stream->in_use = 0;
        return -1;
    }
    nx_memset(stream->ring, 0, sizeof(nxaudio_ring_t));
    
    /* Allocate DMA buffer */
    stream->dma_size = NXAUDIO_RING_SIZE;
    stream->dma_buffer = kmalloc(stream->dma_size);
    if (!stream->dma_buffer) {
        kfree(stream->ring);
        stream->in_use = 0;
        return -1;
    }
    nx_memset(stream->dma_buffer, 0, stream->dma_size);
    
    /* Allocate BDL */
    stream->bdl = (hda_bdl_entry_t*)kmalloc(sizeof(hda_bdl_entry_t) * NXAUDIO_RING_PERIODS);
    if (!stream->bdl) {
        kfree(stream->dma_buffer);
        kfree(stream->ring);
        stream->in_use = 0;
        return -1;
    }
    
    /* Setup BDL entries */
    size_t period_size = stream->dma_size / NXAUDIO_RING_PERIODS;
    for (int i = 0; i < NXAUDIO_RING_PERIODS; i++) {
        stream->bdl[i].address = (uint64_t)(uintptr_t)stream->dma_buffer + i * period_size;
        stream->bdl[i].length = period_size;
        stream->bdl[i].flags = 1;  /* IOC = Interrupt on completion */
    }
    
    serial_puts("[NXAudio] Stream ");
    serial_putc('0' + slot);
    serial_puts(" opened\n");
    
    return slot;
}

int nxaudio_kdrv_stream_start(int stream_id) {
    if (stream_id < 0 || stream_id >= MAX_STREAMS) return -1;
    
    nxaudio_stream_t *stream = &g_nxaudio.streams[stream_id];
    if (!stream->in_use) return -1;
    
    stream->ring->running = 1;
    stream->ring->underrun = 0;
    stream->ring->overrun = 0;
    
    /* TODO: Start HDA stream descriptor */
    
    serial_puts("[NXAudio] Stream ");
    serial_putc('0' + stream_id);
    serial_puts(" started\n");
    
    return 0;
}

int nxaudio_kdrv_stream_stop(int stream_id) {
    if (stream_id < 0 || stream_id >= MAX_STREAMS) return -1;
    
    nxaudio_stream_t *stream = &g_nxaudio.streams[stream_id];
    if (!stream->in_use) return -1;
    
    stream->ring->running = 0;
    
    /* TODO: Stop HDA stream descriptor */
    
    return 0;
}

void nxaudio_kdrv_stream_close(int stream_id) {
    if (stream_id < 0 || stream_id >= MAX_STREAMS) return;
    
    nxaudio_stream_t *stream = &g_nxaudio.streams[stream_id];
    if (!stream->in_use) return;
    
    nxaudio_kdrv_stream_stop(stream_id);
    
    if (stream->bdl) kfree(stream->bdl);
    if (stream->dma_buffer) kfree(stream->dma_buffer);
    if (stream->ring) kfree(stream->ring);
    
    stream->in_use = 0;
}

nxaudio_ring_t* nxaudio_kdrv_get_ring(int stream_id) {
    if (stream_id < 0 || stream_id >= MAX_STREAMS) return NULL;
    if (!g_nxaudio.streams[stream_id].in_use) return NULL;
    return g_nxaudio.streams[stream_id].ring;
}

uint32_t nxaudio_kdrv_get_hw_pos(int stream_id) {
    if (stream_id < 0 || stream_id >= MAX_STREAMS) return 0;
    nxaudio_stream_t *stream = &g_nxaudio.streams[stream_id];
    if (!stream->in_use) return 0;
    
    /* For now return ring read position */
    return stream->ring->read_pos;
}

int nxaudio_kdrv_wait_period(int stream_id, uint32_t timeout_ms) {
    if (stream_id < 0 || stream_id >= MAX_STREAMS) return -1;
    nxaudio_stream_t *stream = &g_nxaudio.streams[stream_id];
    if (!stream->in_use) return -1;
    
    uint32_t start_period = stream->ring->period_count;
    uint32_t elapsed = 0;
    
    while (elapsed < timeout_ms) {
        if (stream->ring->period_count != start_period) {
            return 0;
        }
        delay_us(1000);
        elapsed++;
    }
    
    return -1;  /* Timeout */
}

/* ============ IRQ Handler ============ */

void nxaudio_irq_handler(void *ctx) {
    (void)ctx;
    
    /* Check each stream for completion */
    for (int i = 0; i < MAX_STREAMS; i++) {
        nxaudio_stream_t *stream = &g_nxaudio.streams[i];
        if (!stream->in_use || !stream->ring->running) continue;
        
        /* Increment period count */
        stream->ring->period_count++;
        
        /* Check for underrun */
        if (stream->ring->read_pos == stream->ring->write_pos) {
            stream->ring->underrun = 1;
        }
    }
    
    /* TODO: Clear HDA interrupt status */
}

/* ============ Debug ============ */

void nxaudio_kdrv_debug(void) {
    serial_puts("\n=== NXAudio Debug ===\n");
    serial_puts("Devices: ");
    serial_putc('0' + g_nxaudio.device_count);
    serial_puts("\n");
    
    for (int i = 0; i < g_nxaudio.device_count; i++) {
        serial_puts("  [");
        serial_putc('0' + i);
        serial_puts("] ");
        serial_puts(g_nxaudio.devices[i].info.name);
        if (g_nxaudio.devices[i].info.is_default) {
            serial_puts(" (default)");
        }
        serial_puts("\n");
    }
    
    serial_puts("Active streams: ");
    int active = 0;
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (g_nxaudio.streams[i].in_use) active++;
    }
    serial_putc('0' + active);
    serial_puts("\n");
    serial_puts("=====================\n\n");
}
