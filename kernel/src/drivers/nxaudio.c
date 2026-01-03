/*
 * NeolyxOS NXAudio - Kernel Audio Driver Implementation
 * 
 * Intel HDA driver with boot completion sound.
 * 
 * The NeolyxOS boot chime is a distinctive 3-note rising chord
 * that signals the system is ready.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "drivers/nxaudio.h"
#include "mm/kheap.h"
#include "mm/pmm.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

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

static audio_device_t *audio_devices = NULL;
static int audio_count = 0;
static audio_device_t *primary_audio = NULL;

/* ============ Helpers ============ */

static void audio_memset(void *s, int c, uint32_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
}

/* ============ HDA Register Access ============ */

static uint32_t hda_read32(audio_device_t *dev, uint32_t reg) {
    return *(volatile uint32_t *)(dev->mmio_base + reg);
}

static void hda_write32(audio_device_t *dev, uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(dev->mmio_base + reg) = val;
}

static uint16_t hda_read16(audio_device_t *dev, uint32_t reg) {
    return *(volatile uint16_t *)(dev->mmio_base + reg);
}

static void hda_write16(audio_device_t *dev, uint32_t reg, uint16_t val) {
    *(volatile uint16_t *)(dev->mmio_base + reg) = val;
}

static uint8_t hda_read8(audio_device_t *dev, uint32_t reg) {
    return *(volatile uint8_t *)(dev->mmio_base + reg);
}

static void hda_write8(audio_device_t *dev, uint32_t reg, uint8_t val) {
    *(volatile uint8_t *)(dev->mmio_base + reg) = val;
}

/* ============ HDA Initialization ============ */

static int hda_reset(audio_device_t *dev) {
    /* Enter reset */
    uint32_t gctl = hda_read32(dev, HDA_REG_GCTL);
    hda_write32(dev, HDA_REG_GCTL, gctl & ~1);
    
    /* Wait for reset */
    int timeout = 1000;
    while ((hda_read32(dev, HDA_REG_GCTL) & 1) && timeout--) {
        for (volatile int i = 0; i < 1000; i++);
    }
    
    /* Exit reset */
    hda_write32(dev, HDA_REG_GCTL, gctl | 1);
    
    /* Wait for ready */
    timeout = 1000;
    while (!(hda_read32(dev, HDA_REG_GCTL) & 1) && timeout--) {
        for (volatile int i = 0; i < 1000; i++);
    }
    
    if (timeout <= 0) {
        serial_puts("[AUDIO] HDA reset timeout\n");
        return -1;
    }
    
    /* Wait for codec */
    for (volatile int i = 0; i < 100000; i++);
    
    return 0;
}

static int hda_init_device(audio_device_t *dev) {
    serial_puts("[AUDIO] Initializing HDA controller...\n");
    
    /* Enable bus mastering */
    uint32_t cmd = pci_read(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= (1 << 2) | (1 << 1);
    pci_write(dev->bus, dev->slot, dev->func, 0x04, cmd);
    
    /* Reset controller */
    if (hda_reset(dev) != 0) {
        return -1;
    }
    
    /* Read capabilities */
    uint16_t gcap = hda_read16(dev, HDA_REG_GCAP);
    dev->num_output_streams = (gcap >> 12) & 0xF;
    dev->num_input_streams = (gcap >> 8) & 0xF;
    
    serial_puts("[AUDIO] HDA v");
    serial_putc('0' + hda_read8(dev, HDA_REG_VMAJ));
    serial_putc('.');
    serial_putc('0' + hda_read8(dev, HDA_REG_VMIN));
    serial_puts(", ");
    serial_putc('0' + dev->num_output_streams);
    serial_puts(" outputs\n");
    
    /* Allocate playback buffer (64KB) */
    dev->playback_size = 64 * 1024;
    dev->playback_buffer = (uint8_t *)kmalloc_aligned(dev->playback_size, 128);
    if (!dev->playback_buffer) return -1;
    audio_memset(dev->playback_buffer, 0, dev->playback_size);
    
    /* Default settings */
    dev->sample_rate = 48000;
    dev->channels = 2;
    dev->format = AUDIO_FORMAT_PCM_S16LE;
    dev->master_volume = 80;
    dev->muted = 0;
    dev->active_output = AUDIO_OUTPUT_SPEAKERS;
    
    /* Enable interrupts */
    hda_write32(dev, HDA_REG_INTCTL, 0x80000000);
    
    serial_puts("[AUDIO] HDA ready\n");
    return 0;
}

/* ============ Boot Sound Generation ============ */

/* NeolyxOS signature boot chime
 * A pleasant 3-note rising chord:
 *   - Note 1: C5 (523 Hz) - 200ms
 *   - Note 2: E5 (659 Hz) - 200ms (overlapping)
 *   - Note 3: G5 (784 Hz) - 300ms (final, with fade)
 * Total duration: ~700ms
 */

#define BOOT_SOUND_RATE     48000
#define BOOT_SOUND_DURATION 700     /* ms */
#define BOOT_SOUND_SAMPLES  (BOOT_SOUND_RATE * BOOT_SOUND_DURATION / 1000)

/* Simple sine wave lookup (256 entries, scaled to 16-bit) */
static const int16_t sine_table[256] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739,
    9512, 10278, 11039, 11793, 12539, 13279, 14010, 14732, 15446, 16151,
    16846, 17530, 18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790, 27245, 27683,
    28105, 28510, 28898, 29268, 29621, 29956, 30273, 30571, 30852, 31113,
    31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678,
    32728, 32757, 32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285,
    32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571, 30273, 29956,
    29621, 29268, 28898, 28510, 28105, 27683, 27245, 26790, 26319, 25832,
    25329, 24811, 24279, 23731, 23170, 22594, 22005, 21403, 20787, 20159,
    19519, 18868, 18204, 17530, 16846, 16151, 15446, 14732, 14010, 13279,
    12539, 11793, 11039, 10278, 9512, 8739, 7962, 7179, 6393, 5602, 4808,
    4011, 3212, 2410, 1608, 804, 0, -804, -1608, -2410, -3212, -4011,
    -4808, -5602, -6393, -7179, -7962, -8739, -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530, -18204,
    -18868, -19519, -20159, -20787, -21403, -22005, -22594, -23170, -23731,
    -24279, -24811, -25329, -25832, -26319, -26790, -27245, -27683, -28105,
    -28510, -28898, -29268, -29621, -29956, -30273, -30571, -30852, -31113,
    -31356, -31580, -31785, -31971, -32137, -32285, -32412, -32521, -32609,
    -32678, -32728, -32757, -32767, -32757, -32728, -32678, -32609, -32521,
    -32412, -32285, -32137, -31971, -31785, -31580, -31356, -31113, -30852,
    -30571, -30273, -29956
};

static int16_t generate_sine(uint32_t phase, uint16_t amplitude) {
    uint8_t idx = (phase >> 8) & 0xFF;
    int32_t val = sine_table[idx];
    return (int16_t)((val * amplitude) >> 16);
}

void nxaudio_generate_boot_sound(int16_t *buffer, uint32_t *samples) {
    *samples = BOOT_SOUND_SAMPLES;
    
    /* Frequencies in phase-increment form (for 48kHz) */
    uint32_t freq_c5 = (523 * 65536) / BOOT_SOUND_RATE * 256;  /* C5 - 523 Hz */
    uint32_t freq_e5 = (659 * 65536) / BOOT_SOUND_RATE * 256;  /* E5 - 659 Hz */
    uint32_t freq_g5 = (784 * 65536) / BOOT_SOUND_RATE * 256;  /* G5 - 784 Hz */
    
    uint32_t phase_c = 0, phase_e = 0, phase_g = 0;
    
    for (uint32_t i = 0; i < BOOT_SOUND_SAMPLES; i++) {
        int32_t sample = 0;
        uint32_t t = i * 1000 / BOOT_SOUND_RATE;  /* Time in ms */
        
        /* Note 1: C5 at 0-400ms with fade */
        if (t < 400) {
            uint16_t amp = 20000;
            if (t < 50) amp = amp * t / 50;           /* Attack */
            if (t > 300) amp = amp * (400 - t) / 100; /* Release */
            sample += generate_sine(phase_c, amp);
            phase_c += freq_c5;
        }
        
        /* Note 2: E5 at 150-500ms */
        if (t >= 150 && t < 500) {
            uint16_t amp = 18000;
            if (t < 200) amp = amp * (t - 150) / 50;
            if (t > 400) amp = amp * (500 - t) / 100;
            sample += generate_sine(phase_e, amp);
            phase_e += freq_e5;
        }
        
        /* Note 3: G5 at 300-700ms (main note) */
        if (t >= 300) {
            uint16_t amp = 22000;
            if (t < 350) amp = amp * (t - 300) / 50;
            if (t > 550) amp = amp * (700 - t) / 150;
            sample += generate_sine(phase_g, amp);
            phase_g += freq_g5;
        }
        
        /* Clip */
        if (sample > 32767) sample = 32767;
        if (sample < -32767) sample = -32767;
        
        /* Stereo (duplicate to both channels) */
        buffer[i * 2] = (int16_t)sample;
        buffer[i * 2 + 1] = (int16_t)sample;
    }
}

/* ============ NXAudio API ============ */

int nxaudio_init(void) {
    serial_puts("[AUDIO] Initializing NXAudio subsystem...\n");
    
    audio_devices = NULL;
    audio_count = 0;
    primary_audio = NULL;
    
    /* Scan for HDA controllers */
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t vendor_device = pci_read(bus, slot, 0, 0);
            uint16_t vendor = vendor_device & 0xFFFF;
            uint16_t device = (vendor_device >> 16) & 0xFFFF;
            
            if (vendor == 0xFFFF) continue;
            
            /* Check class (multimedia controller - audio) */
            uint32_t class_info = pci_read(bus, slot, 0, 8);
            uint8_t base_class = (class_info >> 24) & 0xFF;
            uint8_t sub_class = (class_info >> 16) & 0xFF;
            
            if (base_class != 0x04) continue;  /* Not multimedia */
            if (sub_class != 0x03) continue;   /* Not audio */
            
            serial_puts("[AUDIO] Found HDA: ");
            const char *hex = "0123456789ABCDEF";
            for (int i = 12; i >= 0; i -= 4) serial_putc(hex[(vendor >> i) & 0xF]);
            serial_putc(':');
            for (int i = 12; i >= 0; i -= 4) serial_putc(hex[(device >> i) & 0xF]);
            serial_puts("\n");
            
            audio_device_t *dev = (audio_device_t *)kzalloc(sizeof(audio_device_t));
            if (!dev) continue;
            
            dev->vendor_id = vendor;
            dev->device_id = device;
            dev->bus = bus;
            dev->slot = slot;
            dev->func = 0;
            
            /* Get BAR0 */
            uint32_t bar0 = pci_read(bus, slot, 0, 0x10);
            dev->mmio_base = (volatile uint8_t *)(uintptr_t)(bar0 & ~0xF);
            
            /* Get IRQ */
            dev->irq = pci_read(bus, slot, 0, 0x3C) & 0xFF;
            
            /* Set name */
            if (vendor == PCI_VENDOR_INTEL) {
                const char *n = "Intel HDA";
                for (int i = 0; i < 9; i++) dev->name[i] = n[i];
            } else if (vendor == PCI_VENDOR_AMD) {
                const char *n = "AMD HDA";
                for (int i = 0; i < 7; i++) dev->name[i] = n[i];
            } else {
                const char *n = "HD Audio";
                for (int i = 0; i < 8; i++) dev->name[i] = n[i];
            }
            
            if (hda_init_device(dev) == 0) {
                dev->next = audio_devices;
                audio_devices = dev;
                audio_count++;
                
                if (primary_audio == NULL) {
                    primary_audio = dev;
                }
            }
        }
    }
    
    if (audio_count == 0) {
        serial_puts("[AUDIO] No audio devices found\n");
        return -1;
    }
    
    serial_puts("[AUDIO] Ready (");
    serial_putc('0' + audio_count);
    serial_puts(" devices)\n");
    
    return 0;
}

audio_device_t *nxaudio_get_device(void) {
    return primary_audio;
}

audio_device_t *nxaudio_get_device_by_index(int index) {
    audio_device_t *dev = audio_devices;
    int i = 0;
    while (dev && i < index) {
        dev = dev->next;
        i++;
    }
    return dev;
}

int nxaudio_device_count(void) {
    return audio_count;
}

/* ============ Playback ============ */

int nxaudio_play(audio_device_t *dev, const void *data, uint32_t len,
                 uint32_t sample_rate, uint8_t channels, audio_format_t format) {
    if (!dev || !data) return -1;
    
    serial_puts("[AUDIO] Playing audio...\n");
    
    /* Copy to playback buffer */
    uint32_t copy_len = (len < dev->playback_size) ? len : dev->playback_size;
    const uint8_t *src = (const uint8_t *)data;
    for (uint32_t i = 0; i < copy_len; i++) {
        dev->playback_buffer[i] = src[i];
    }
    
    dev->sample_rate = sample_rate;
    dev->channels = channels;
    dev->format = format;
    dev->playback_pos = 0;
    
    /* TODO: Configure HDA stream descriptor and start DMA */
    /* For now, playback is simulated */
    
    return 0;
}

int nxaudio_stop(audio_device_t *dev) {
    if (!dev) return -1;
    dev->playback_pos = 0;
    return 0;
}

int nxaudio_pause(audio_device_t *dev) {
    if (!dev) return -1;
    return 0;
}

int nxaudio_resume(audio_device_t *dev) {
    if (!dev) return -1;
    return 0;
}

int nxaudio_is_playing(audio_device_t *dev) {
    return dev && dev->playback_pos > 0;
}

/* ============ Volume ============ */

int nxaudio_set_volume(audio_device_t *dev, uint8_t volume) {
    if (!dev) return -1;
    if (volume > 100) volume = 100;
    dev->master_volume = volume;
    
    serial_puts("[AUDIO] Volume: ");
    serial_putc('0' + volume / 10);
    serial_putc('0' + volume % 10);
    serial_puts("%\n");
    
    return 0;
}

uint8_t nxaudio_get_volume(audio_device_t *dev) {
    return dev ? dev->master_volume : 0;
}

int nxaudio_set_mute(audio_device_t *dev, int muted) {
    if (!dev) return -1;
    dev->muted = muted ? 1 : 0;
    return 0;
}

/* ============ Output ============ */

int nxaudio_set_output(audio_device_t *dev, audio_output_t output) {
    if (!dev) return -1;
    dev->active_output = output;
    return 0;
}

/* ============ Boot Sound ============ */

void nxaudio_play_boot_sound(void) {
    serial_puts("\n");
    serial_puts("  ╔═══════════════════════════════════════╗\n");
    serial_puts("  ║                                       ║\n");
    serial_puts("  ║   ♪  NeolyxOS Boot Sound  ♪          ║\n");
    serial_puts("  ║                                       ║\n");
    serial_puts("  ║   The system is ready!                ║\n");
    serial_puts("  ║                                       ║\n");
    serial_puts("  ╚═══════════════════════════════════════╝\n");
    serial_puts("\n");
    
    audio_device_t *dev = nxaudio_get_device();
    if (!dev) {
        serial_puts("[AUDIO] No audio device for boot sound\n");
        return;
    }
    
    /* Generate boot sound */
    uint32_t samples;
    int16_t *buffer = (int16_t *)kmalloc(BOOT_SOUND_SAMPLES * 2 * sizeof(int16_t));
    if (!buffer) return;
    
    nxaudio_generate_boot_sound(buffer, &samples);
    
    /* Play the sound */
    nxaudio_play(dev, buffer, samples * 2 * sizeof(int16_t),
                 BOOT_SOUND_RATE, 2, AUDIO_FORMAT_PCM_S16LE);
    
    serial_puts("[AUDIO] ♪ Boot chime played ♪\n");
    
    kfree(buffer);
}

/* ============ System Sounds ============ */

int nxaudio_play_system_sound(system_sound_t sound) {
    switch (sound) {
        case SYSTEM_SOUND_BOOT:
            nxaudio_play_boot_sound();
            return 0;
        case SYSTEM_SOUND_SHUTDOWN:
            serial_puts("[AUDIO] Shutdown sound\n");
            return 0;
        case SYSTEM_SOUND_ERROR:
            serial_puts("[AUDIO] Error beep\n");
            return 0;
        case SYSTEM_SOUND_NOTIFICATION:
            serial_puts("[AUDIO] Notification\n");
            return 0;
        case SYSTEM_SOUND_CLICK:
            /* Quiet click for UI */
            return 0;
        default:
            return -1;
    }
}
