/*
 * NeolyxOS AC97 Audio Codec Driver
 * 
 * Legacy audio codec driver:
 * - AC97 register access
 * - PCM out/in
 * - Volume control
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "audio.h"
#include "../serial.h"
#include <string.h>

/* AC97 Native Audio Mixer registers */
#define AC97_RESET          0x00
#define AC97_MASTER         0x02
#define AC97_HEADPHONE      0x04
#define AC97_MASTER_MONO    0x06
#define AC97_PC_BEEP        0x0A
#define AC97_PHONE          0x0C
#define AC97_MIC            0x0E
#define AC97_LINE_IN        0x10
#define AC97_CD             0x12
#define AC97_VIDEO          0x14
#define AC97_AUX            0x16
#define AC97_PCM_OUT        0x18
#define AC97_RECORD_SELECT  0x1A
#define AC97_RECORD_GAIN    0x1C
#define AC97_GENERAL        0x20
#define AC97_3D_CONTROL     0x22
#define AC97_POWERDOWN      0x26
#define AC97_EXTENDED_ID    0x28
#define AC97_EXTENDED_STATUS 0x2A
#define AC97_PCM_FRONT_RATE 0x2C
#define AC97_PCM_SURR_RATE  0x2E
#define AC97_PCM_LFE_RATE   0x30
#define AC97_PCM_LR_RATE    0x32
#define AC97_SPDIF_CTRL     0x3A
#define AC97_VENDOR_ID1     0x7C
#define AC97_VENDOR_ID2     0x7E

/* Bus Master registers */
#define AC97_BM_PI_BDBAR    0x00    /* PCM In buffer descriptor */
#define AC97_BM_PI_CIV      0x04    /* Current index value */
#define AC97_BM_PI_LVI      0x05    /* Last valid index */
#define AC97_BM_PI_SR       0x06    /* Status register */
#define AC97_BM_PI_CR       0x0B    /* Control register */
#define AC97_BM_PO_BDBAR    0x10    /* PCM Out buffer descriptor */
#define AC97_BM_PO_CIV      0x14
#define AC97_BM_PO_LVI      0x15
#define AC97_BM_PO_SR       0x16
#define AC97_BM_PO_CR       0x1B
#define AC97_BM_GLOB_CNT    0x2C    /* Global control */
#define AC97_BM_GLOB_STA    0x30    /* Global status */

/* Buffer descriptor entry */
typedef struct {
    uint32_t    address;
    uint16_t    length;         /* In samples */
    uint16_t    flags;
} __attribute__((packed)) ac97_bdl_entry_t;

/* AC97 driver state */
typedef struct {
    uint16_t    mixer_base;
    uint16_t    bus_base;
    
    /* Buffer descriptor list */
    ac97_bdl_entry_t *bdl;
    uint32_t    bdl_phys;
    
    /* DMA buffer */
    void       *dma_buffer;
    uint32_t    dma_phys;
    size_t      dma_size;
    
    /* Volume */
    int         volume_left;
    int         volume_right;
} ac97_state_t;

/* Logging */
static void ac97_log(const char *msg) {
    serial_puts("[AC97] ");
    serial_puts(msg);
    serial_puts("\n");
}

/* I/O access */
static inline uint16_t ac97_mixer_read(ac97_state_t *ac, uint8_t reg) {
    return inw(ac->mixer_base + reg);
}

static inline void ac97_mixer_write(ac97_state_t *ac, uint8_t reg, uint16_t val) {
    outw(ac->mixer_base + reg, val);
}

static inline uint8_t ac97_bus_read8(ac97_state_t *ac, uint8_t reg) {
    return inb(ac->bus_base + reg);
}

static inline void ac97_bus_write8(ac97_state_t *ac, uint8_t reg, uint8_t val) {
    outb(ac->bus_base + reg, val);
}

static inline uint32_t ac97_bus_read32(ac97_state_t *ac, uint8_t reg) {
    return inl(ac->bus_base + reg);
}

static inline void ac97_bus_write32(ac97_state_t *ac, uint8_t reg, uint32_t val) {
    outl(ac->bus_base + reg, val);
}

/* Wait for codec ready */
static int ac97_wait_ready(ac97_state_t *ac) {
    for (int i = 0; i < 10000; i++) {
        if (ac97_bus_read32(ac, AC97_BM_GLOB_STA) & 0x100) {
            return 0;
        }
        for (volatile int j = 0; j < 100; j++);
    }
    return -1;
}

/* Driver operations */

int ac97_driver_probe(audio_device_t *dev) {
    if (!dev || dev->info.type != AUDIO_DEV_TYPE_AC97) {
        return -1;
    }
    return 0;
}

int ac97_driver_init(audio_device_t *dev) {
    if (!dev) return -1;
    
    ac97_log("Initializing AC97 driver");
    
    ac97_state_t *ac = (ac97_state_t*)kmalloc(sizeof(ac97_state_t));
    if (!ac) return -1;
    
    memset(ac, 0, sizeof(*ac));
    
    /* Get I/O ports from PCI BARs */
    /* BAR0 = mixer, BAR1 = bus master */
    ac->mixer_base = (uint16_t)(uintptr_t)dev->mmio_base;
    ac->bus_base = ac->mixer_base + 0x100;  /* Simplified */
    
    dev->private_data = ac;
    
    /* Enable bus master */
    ac97_bus_write32(ac, AC97_BM_GLOB_CNT, 0x02);  /* Cold reset */
    
    if (ac97_wait_ready(ac) != 0) {
        ac97_log("Codec not ready");
        kfree(ac);
        return -1;
    }
    
    /* Read vendor ID */
    uint16_t vid1 = ac97_mixer_read(ac, AC97_VENDOR_ID1);
    uint16_t vid2 = ac97_mixer_read(ac, AC97_VENDOR_ID2);
    (void)vid1; (void)vid2;
    
    /* Set default volume */
    ac97_mixer_write(ac, AC97_MASTER, 0x0000);      /* Max volume */
    ac97_mixer_write(ac, AC97_PCM_OUT, 0x0808);     /* PCM out volume */
    
    /* Enable variable rate */
    uint16_t ext = ac97_mixer_read(ac, AC97_EXTENDED_ID);
    if (ext & 0x01) {
        ac97_mixer_write(ac, AC97_EXTENDED_STATUS, 0x01);
    }
    
    ac->volume_left = 100;
    ac->volume_right = 100;
    
    ac97_log("AC97 driver initialized");
    return 0;
}

void ac97_driver_remove(audio_device_t *dev) {
    if (!dev || !dev->private_data) return;
    
    ac97_state_t *ac = (ac97_state_t*)dev->private_data;
    
    /* Mute */
    ac97_mixer_write(ac, AC97_MASTER, 0x8000);
    
    if (ac->bdl) kfree(ac->bdl);
    if (ac->dma_buffer) kfree(ac->dma_buffer);
    
    kfree(ac);
    dev->private_data = NULL;
}

int ac97_driver_open(audio_device_t *dev, audio_stream_config_t *config) {
    if (!dev || !config || !dev->private_data) return -1;
    
    ac97_state_t *ac = (ac97_state_t*)dev->private_data;
    
    /* Set sample rate */
    ac97_mixer_write(ac, AC97_PCM_FRONT_RATE, config->sample_rate);
    
    /* Allocate BDL */
    ac->bdl = (ac97_bdl_entry_t*)kmalloc(sizeof(ac97_bdl_entry_t) * 32);
    ac->bdl_phys = (uint32_t)(uintptr_t)ac->bdl;
    
    /* Allocate DMA buffer */
    size_t frame_size = config->channels * 2;  /* 16-bit */
    ac->dma_size = config->buffer_frames * frame_size;
    ac->dma_buffer = kmalloc(ac->dma_size);
    ac->dma_phys = (uint32_t)(uintptr_t)ac->dma_buffer;
    
    if (!ac->bdl || !ac->dma_buffer) return -1;
    
    memset(ac->dma_buffer, 0, ac->dma_size);
    
    /* Setup BDL */
    ac->bdl[0].address = ac->dma_phys;
    ac->bdl[0].length = ac->dma_size / 2;
    ac->bdl[0].flags = 0x8000;  /* IOC */
    
    ac->bdl[1].address = ac->dma_phys + ac->dma_size / 2;
    ac->bdl[1].length = ac->dma_size / 2;
    ac->bdl[1].flags = 0x8000;
    
    /* Set BDL address */
    ac97_bus_write32(ac, AC97_BM_PO_BDBAR, ac->bdl_phys);
    ac97_bus_write8(ac, AC97_BM_PO_LVI, 1);
    
    return 0;
}

void ac97_driver_close(audio_device_t *dev) {
    if (!dev || !dev->private_data) return;
    
    ac97_driver_stop(dev);
}

int ac97_driver_start(audio_device_t *dev) {
    if (!dev || !dev->private_data) return -1;
    
    ac97_state_t *ac = (ac97_state_t*)dev->private_data;
    
    /* Start playback */
    ac97_bus_write8(ac, AC97_BM_PO_CR, 0x01);
    
    ac97_log("Playback started");
    return 0;
}

int ac97_driver_stop(audio_device_t *dev) {
    if (!dev || !dev->private_data) return -1;
    
    ac97_state_t *ac = (ac97_state_t*)dev->private_data;
    
    /* Stop playback */
    ac97_bus_write8(ac, AC97_BM_PO_CR, 0x00);
    
    ac97_log("Playback stopped");
    return 0;
}

int ac97_driver_write(audio_device_t *dev, const void *data, size_t frames) {
    if (!dev || !data || !dev->private_data) return -1;
    
    ac97_state_t *ac = (ac97_state_t*)dev->private_data;
    
    size_t bytes = frames * dev->config.channels * 2;
    if (bytes > ac->dma_size / 2) {
        bytes = ac->dma_size / 2;
        frames = bytes / (dev->config.channels * 2);
    }
    
    size_t offset = dev->write_pos % ac->dma_size;
    memcpy((uint8_t*)ac->dma_buffer + offset, data, bytes);
    
    dev->write_pos += bytes;
    
    return frames;
}

int ac97_driver_read(audio_device_t *dev, void *data, size_t frames) {
    (void)dev; (void)data; (void)frames;
    return 0;
}

int ac97_driver_set_volume(audio_device_t *dev, int left, int right) {
    if (!dev || !dev->private_data) return -1;
    
    ac97_state_t *ac = (ac97_state_t*)dev->private_data;
    
    /* Convert 0-100 to 0-63 attenuation (inverted) */
    uint8_t att_l = 63 - ((left * 63) / 100);
    uint8_t att_r = 63 - ((right * 63) / 100);
    
    uint16_t vol = (att_l << 8) | att_r;
    ac97_mixer_write(ac, AC97_MASTER, vol);
    
    ac->volume_left = left;
    ac->volume_right = right;
    
    return 0;
}

int ac97_driver_get_volume(audio_device_t *dev, int *left, int *right) {
    if (!dev || !dev->private_data) return -1;
    
    ac97_state_t *ac = (ac97_state_t*)dev->private_data;
    
    *left = ac->volume_left;
    *right = ac->volume_right;
    
    return 0;
}

/* Driver registration */

static const audio_driver_ops_t ac97_ops = {
    .probe = ac97_driver_probe,
    .init = ac97_driver_init,
    .remove = ac97_driver_remove,
    .open = ac97_driver_open,
    .close = ac97_driver_close,
    .start = ac97_driver_start,
    .stop = ac97_driver_stop,
    .write = ac97_driver_write,
    .read = ac97_driver_read,
    .set_volume = ac97_driver_set_volume,
    .get_volume = ac97_driver_get_volume,
};

static audio_driver_t ac97_driver = {
    .name = "AC97 Audio",
    .type = AUDIO_DEV_TYPE_AC97,
    .ops = &ac97_ops,
};

void ac97_driver_register(void) {
    audio_register_driver(&ac97_driver);
}
