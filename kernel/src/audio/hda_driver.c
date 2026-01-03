/*
 * NeolyxOS Intel HD Audio Kernel Driver Implementation
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "hda_driver.h"
#include "../serial.h"
#include <string.h>

/* Logging */
static void hda_log(const char *msg) {
    serial_puts("[HDA] ");
    serial_puts(msg);
    serial_puts("\n");
}

/* MMIO access */
static inline uint32_t hda_read32(hda_state_t *hda, uint32_t offset) {
    return *(volatile uint32_t*)((uint8_t*)hda->mmio + offset);
}

static inline void hda_write32(hda_state_t *hda, uint32_t offset, uint32_t val) {
    *(volatile uint32_t*)((uint8_t*)hda->mmio + offset) = val;
}

static inline uint16_t hda_read16(hda_state_t *hda, uint32_t offset) {
    return *(volatile uint16_t*)((uint8_t*)hda->mmio + offset);
}

static inline void hda_write16(hda_state_t *hda, uint32_t offset, uint16_t val) {
    *(volatile uint16_t*)((uint8_t*)hda->mmio + offset) = val;
}

static inline uint8_t hda_read8(hda_state_t *hda, uint32_t offset) {
    return *(volatile uint8_t*)((uint8_t*)hda->mmio + offset);
}

static inline void hda_write8(hda_state_t *hda, uint32_t offset, uint8_t val) {
    *(volatile uint8_t*)((uint8_t*)hda->mmio + offset) = val;
}

/* Delay helper */
static void hda_delay(int us) {
    for (volatile int i = 0; i < us * 100; i++);
}

/* Reset controller */
static int hda_reset(hda_state_t *hda) {
    hda_log("Resetting controller");
    
    /* Enter reset */
    hda_write32(hda, HDA_REG_GCTL, 0);
    hda_delay(100);
    
    /* Wait for reset */
    for (int i = 0; i < 1000; i++) {
        if (!(hda_read32(hda, HDA_REG_GCTL) & 1)) break;
        hda_delay(10);
    }
    
    /* Exit reset */
    hda_write32(hda, HDA_REG_GCTL, 1);
    hda_delay(100);
    
    /* Wait for ready */
    for (int i = 0; i < 1000; i++) {
        if (hda_read32(hda, HDA_REG_GCTL) & 1) {
            hda_log("Controller reset complete");
            return 0;
        }
        hda_delay(10);
    }
    
    hda_log("Controller reset failed");
    return -1;
}

/* Setup CORB */
static int hda_setup_corb(hda_state_t *hda) {
    /* Stop CORB */
    hda_write8(hda, HDA_REG_CORBCTL, 0);
    hda_delay(10);
    
    /* CORB size */
    uint8_t corbsize = hda_read8(hda, HDA_REG_CORBSIZE);
    if (corbsize & 0x40) {
        hda_write8(hda, HDA_REG_CORBSIZE, 0x02);  /* 256 entries */
        hda->corb_size = 256;
    } else if (corbsize & 0x20) {
        hda_write8(hda, HDA_REG_CORBSIZE, 0x01);  /* 16 entries */
        hda->corb_size = 16;
    } else {
        hda_write8(hda, HDA_REG_CORBSIZE, 0x00);  /* 2 entries */
        hda->corb_size = 2;
    }
    
    /* Set CORB base address */
    hda_write32(hda, HDA_REG_CORBLBASE, (uint32_t)hda->corb_phys);
    hda_write32(hda, HDA_REG_CORBUBASE, (uint32_t)(hda->corb_phys >> 32));
    
    /* Reset read pointer */
    hda_write16(hda, HDA_REG_CORBRP, 0x8000);
    for (int i = 0; i < 1000; i++) {
        if (hda_read16(hda, HDA_REG_CORBRP) & 0x8000) break;
        hda_delay(1);
    }
    hda_write16(hda, HDA_REG_CORBRP, 0);
    hda->corb_wp = 0;
    
    /* Start CORB */
    hda_write8(hda, HDA_REG_CORBCTL, 0x02);
    
    return 0;
}

/* Setup RIRB */
static int hda_setup_rirb(hda_state_t *hda) {
    /* Stop RIRB */
    hda_write8(hda, HDA_REG_RIRBCTL, 0);
    hda_delay(10);
    
    /* RIRB size */
    uint8_t rirbsize = hda_read8(hda, HDA_REG_RIRBSIZE);
    if (rirbsize & 0x40) {
        hda_write8(hda, HDA_REG_RIRBSIZE, 0x02);
        hda->rirb_size = 256;
    } else if (rirbsize & 0x20) {
        hda_write8(hda, HDA_REG_RIRBSIZE, 0x01);
        hda->rirb_size = 16;
    } else {
        hda_write8(hda, HDA_REG_RIRBSIZE, 0x00);
        hda->rirb_size = 2;
    }
    
    /* Set RIRB base address */
    hda_write32(hda, HDA_REG_RIRBLBASE, (uint32_t)hda->rirb_phys);
    hda_write32(hda, HDA_REG_RIRBUBASE, (uint32_t)(hda->rirb_phys >> 32));
    
    /* Reset write pointer */
    hda_write16(hda, HDA_REG_RIRBWP, 0x8000);
    hda->rirb_rp = 0;
    
    /* Start RIRB */
    hda_write8(hda, HDA_REG_RIRBCTL, 0x02);
    
    return 0;
}

/* Send verb via CORB */
static uint32_t hda_send_verb(hda_state_t *hda, uint8_t codec, uint8_t node,
                               uint32_t verb, uint32_t param) {
    uint32_t cmd = ((uint32_t)codec << 28) | ((uint32_t)node << 20) |
                   (verb << 8) | (param & 0xFF);
    
    /* Write to CORB */
    hda->corb_wp = (hda->corb_wp + 1) % hda->corb_size;
    hda->corb[hda->corb_wp] = cmd;
    hda_write16(hda, HDA_REG_CORBWP, hda->corb_wp);
    
    /* Wait for response */
    for (int i = 0; i < 1000; i++) {
        uint16_t wp = hda_read16(hda, HDA_REG_RIRBWP);
        if (wp != hda->rirb_rp) {
            hda->rirb_rp = wp;
            return (uint32_t)(hda->rirb[wp] & 0xFFFFFFFF);
        }
        hda_delay(10);
    }
    
    return 0;
}

/* Get parameter */
static uint32_t hda_get_param(hda_state_t *hda, uint8_t node, uint32_t param) {
    return hda_send_verb(hda, hda->codec_addr, node, HDA_VERB_GET_PARAM, param);
}

/* Detect codec */
static int hda_detect_codec(hda_state_t *hda) {
    hda_log("Detecting codecs");
    
    uint16_t statests = hda_read16(hda, HDA_REG_STATESTS);
    
    for (int addr = 0; addr < 15; addr++) {
        if (statests & (1 << addr)) {
            hda->codec_addr = addr;
            
            uint32_t vendor_id = hda_get_param(hda, 0, HDA_PARAM_VENDOR_ID);
            hda->codec_vendor = (vendor_id >> 16) & 0xFFFF;
            hda->codec_device = vendor_id & 0xFFFF;
            
            hda_log("Found codec");
            
            /* Find audio function group */
            uint32_t node_count = hda_get_param(hda, 0, HDA_PARAM_SUB_NODE_COUNT);
            uint8_t start_node = (node_count >> 16) & 0xFF;
            uint8_t num_nodes = node_count & 0xFF;
            
            for (int n = 0; n < num_nodes; n++) {
                uint8_t nid = start_node + n;
                uint32_t fn_type = hda_get_param(hda, nid, HDA_PARAM_FN_GROUP_TYPE);
                
                if ((fn_type & 0xFF) == 1) {  /* Audio function group */
                    hda->afg_node = nid;
                    hda_log("Found AFG");
                    break;
                }
            }
            
            return 0;
        }
    }
    
    hda_log("No codec found");
    return -1;
}

/* Configure stream format */
static uint16_t hda_format(uint32_t rate, uint8_t channels, uint8_t bits) {
    uint16_t fmt = 0;
    
    /* Sample rate */
    switch (rate) {
        case 48000:  fmt |= 0x0000; break;
        case 44100:  fmt |= 0x4000; break;
        case 96000:  fmt |= 0x0800; break;
        case 192000: fmt |= 0x1800; break;
        default:     fmt |= 0x0000; break;
    }
    
    /* Bits per sample */
    switch (bits) {
        case 16: fmt |= 0x10; break;
        case 24: fmt |= 0x30; break;
        case 32: fmt |= 0x40; break;
        default: fmt |= 0x10; break;
    }
    
    /* Channels */
    fmt |= (channels - 1) & 0xF;
    
    return fmt;
}

/* Configure stream */
static int hda_configure_stream(hda_state_t *hda, int stream_id,
                                 audio_stream_config_t *config) {
    uint32_t sd_base = 0x80 + stream_id * 0x20;
    
    /* Stop stream */
    hda_write8(hda, sd_base + HDA_SD_CTL, 0);
    hda_delay(10);
    
    /* Clear status */
    hda_write8(hda, sd_base + HDA_SD_STS, 0x1C);
    
    /* Set format */
    uint8_t bits = 16;
    if (config->format == AUDIO_FMT_S24_LE) bits = 24;
    else if (config->format == AUDIO_FMT_S32_LE) bits = 32;
    
    uint16_t fmt = hda_format(config->sample_rate, config->channels, bits);
    hda_write16(hda, sd_base + HDA_SD_FMT, fmt);
    
    /* Set cyclic buffer length */
    hda_write32(hda, sd_base + HDA_SD_CBL, hda->dma_size);
    
    /* Set last valid index */
    hda_write16(hda, sd_base + HDA_SD_LVI, 1);  /* 2 BDL entries */
    
    /* Set BDL address */
    hda_write32(hda, sd_base + HDA_SD_BDLPL, (uint32_t)hda->bdl_phys);
    hda_write32(hda, sd_base + HDA_SD_BDLPU, (uint32_t)(hda->bdl_phys >> 32));
    
    /* Setup BDL */
    hda->bdl[0].address = hda->dma_phys;
    hda->bdl[0].length = hda->dma_size / 2;
    hda->bdl[0].flags = 1;  /* IOC */
    
    hda->bdl[1].address = hda->dma_phys + hda->dma_size / 2;
    hda->bdl[1].length = hda->dma_size / 2;
    hda->bdl[1].flags = 1;
    
    hda->active_stream = stream_id;
    
    return 0;
}

/* Driver operations */

int hda_driver_probe(audio_device_t *dev) {
    if (!dev || dev->info.type != AUDIO_DEV_TYPE_HDA) {
        return -1;
    }
    return 0;
}

int hda_driver_init(audio_device_t *dev) {
    if (!dev || !dev->mmio_base) return -1;
    
    hda_log("Initializing HDA driver");
    
    /* Allocate state */
    hda_state_t *hda = (hda_state_t*)kmalloc(sizeof(hda_state_t));
    if (!hda) return -1;
    
    memset(hda, 0, sizeof(*hda));
    hda->mmio = dev->mmio_base;
    dev->private_data = hda;
    
    /* Allocate CORB/RIRB */
    hda->corb = (uint32_t*)kmalloc(4096);
    hda->rirb = (uint64_t*)kmalloc(4096);
    hda->corb_phys = (uint64_t)(uintptr_t)hda->corb;
    hda->rirb_phys = (uint64_t)(uintptr_t)hda->rirb;
    
    /* Allocate BDL */
    hda->bdl = (hda_bdl_entry_t*)kmalloc(4096);
    hda->bdl_phys = (uint64_t)(uintptr_t)hda->bdl;
    
    /* Reset controller */
    if (hda_reset(hda) != 0) {
        kfree(hda);
        return -1;
    }
    
    /* Get capabilities */
    uint16_t gcap = hda_read16(hda, HDA_REG_GCAP);
    hda->num_output = (gcap >> 12) & 0xF;
    hda->num_input = (gcap >> 8) & 0xF;
    
    /* Setup CORB/RIRB */
    hda_setup_corb(hda);
    hda_setup_rirb(hda);
    
    /* Detect codec */
    if (hda_detect_codec(hda) != 0) {
        hda_log("No codec detected");
    }
    
    /* Enable interrupts */
    hda_write32(hda, HDA_REG_INTCTL, 0x80000000 | 0xFF);
    
    hda_log("HDA driver initialized");
    return 0;
}

void hda_driver_remove(audio_device_t *dev) {
    if (!dev || !dev->private_data) return;
    
    hda_state_t *hda = (hda_state_t*)dev->private_data;
    
    /* Disable interrupts */
    hda_write32(hda, HDA_REG_INTCTL, 0);
    
    /* Reset */
    hda_write32(hda, HDA_REG_GCTL, 0);
    
    /* Free resources */
    if (hda->corb) kfree(hda->corb);
    if (hda->rirb) kfree(hda->rirb);
    if (hda->bdl) kfree(hda->bdl);
    if (hda->dma_buffer) kfree(hda->dma_buffer);
    
    kfree(hda);
    dev->private_data = NULL;
}

int hda_driver_open(audio_device_t *dev, audio_stream_config_t *config) {
    if (!dev || !config || !dev->private_data) return -1;
    
    hda_state_t *hda = (hda_state_t*)dev->private_data;
    
    /* Allocate DMA buffer */
    size_t frame_size = config->channels * 2;  /* 16-bit */
    if (config->format == AUDIO_FMT_S24_LE || config->format == AUDIO_FMT_S32_LE) {
        frame_size = config->channels * 4;
    }
    
    hda->dma_size = config->buffer_frames * frame_size;
    hda->dma_buffer = kmalloc(hda->dma_size);
    hda->dma_phys = (uint64_t)(uintptr_t)hda->dma_buffer;
    
    if (!hda->dma_buffer) return -1;
    
    memset(hda->dma_buffer, 0, hda->dma_size);
    
    /* Configure stream */
    hda_configure_stream(hda, 4, config);  /* Use stream 4 for output */
    
    return 0;
}

void hda_driver_close(audio_device_t *dev) {
    if (!dev || !dev->private_data) return;
    
    hda_state_t *hda = (hda_state_t*)dev->private_data;
    
    hda_driver_stop(dev);
    
    if (hda->dma_buffer) {
        kfree(hda->dma_buffer);
        hda->dma_buffer = NULL;
    }
}

int hda_driver_start(audio_device_t *dev) {
    if (!dev || !dev->private_data) return -1;
    
    hda_state_t *hda = (hda_state_t*)dev->private_data;
    uint32_t sd_base = 0x80 + hda->active_stream * 0x20;
    
    /* Set stream ID and start */
    hda_write8(hda, sd_base + HDA_SD_CTL + 2, 0x10);  /* Stream ID 1 */
    hda_write8(hda, sd_base + HDA_SD_CTL, 0x02);  /* Run */
    
    hda_log("Stream started");
    return 0;
}

int hda_driver_stop(audio_device_t *dev) {
    if (!dev || !dev->private_data) return -1;
    
    hda_state_t *hda = (hda_state_t*)dev->private_data;
    uint32_t sd_base = 0x80 + hda->active_stream * 0x20;
    
    hda_write8(hda, sd_base + HDA_SD_CTL, 0);
    
    hda_log("Stream stopped");
    return 0;
}

int hda_driver_write(audio_device_t *dev, const void *data, size_t frames) {
    if (!dev || !data || !dev->private_data) return -1;
    
    hda_state_t *hda = (hda_state_t*)dev->private_data;
    
    size_t frame_size = dev->config.channels * 2;
    size_t bytes = frames * frame_size;
    
    if (bytes > hda->dma_size / 2) {
        bytes = hda->dma_size / 2;
        frames = bytes / frame_size;
    }
    
    /* Copy to DMA buffer */
    size_t offset = dev->write_pos % hda->dma_size;
    memcpy((uint8_t*)hda->dma_buffer + offset, data, bytes);
    
    dev->write_pos += bytes;
    
    return frames;
}

int hda_driver_read(audio_device_t *dev, void *data, size_t frames) {
    (void)dev; (void)data; (void)frames;
    return 0;  /* Capture not implemented */
}

int hda_driver_set_volume(audio_device_t *dev, int left, int right) {
    if (!dev || !dev->private_data) return -1;
    
    hda_state_t *hda = (hda_state_t*)dev->private_data;
    
    /* Set output amp gain */
    if (hda->afg_node) {
        uint8_t gain_l = (left * 127) / 100;
        uint8_t gain_r = (right * 127) / 100;
        
        hda_send_verb(hda, hda->codec_addr, hda->afg_node,
                      HDA_VERB_SET_AMP_GAIN, 0x9000 | gain_l);
        hda_send_verb(hda, hda->codec_addr, hda->afg_node,
                      HDA_VERB_SET_AMP_GAIN, 0xA000 | gain_r);
    }
    
    return 0;
}

int hda_driver_get_volume(audio_device_t *dev, int *left, int *right) {
    (void)dev;
    *left = 100;
    *right = 100;
    return 0;
}

/* Driver registration */

static const audio_driver_ops_t hda_ops = {
    .probe = hda_driver_probe,
    .init = hda_driver_init,
    .remove = hda_driver_remove,
    .open = hda_driver_open,
    .close = hda_driver_close,
    .start = hda_driver_start,
    .stop = hda_driver_stop,
    .write = hda_driver_write,
    .read = hda_driver_read,
    .set_volume = hda_driver_set_volume,
    .get_volume = hda_driver_get_volume,
};

static audio_driver_t hda_driver = {
    .name = "Intel HD Audio",
    .type = AUDIO_DEV_TYPE_HDA,
    .ops = &hda_ops,
};

void hda_driver_register(void) {
    audio_register_driver(&hda_driver);
}
