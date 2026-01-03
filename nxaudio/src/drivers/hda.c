/*
 * NXAudio Intel HDA Driver
 * 
 * High Definition Audio codec driver for NeolyxOS
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ============ HDA Registers ============ */
#define HDA_GCAP        0x00    /* Global Capabilities */
#define HDA_VMIN        0x02    /* Minor Version */
#define HDA_VMAJ        0x03    /* Major Version */
#define HDA_OUTPAY      0x04    /* Output Payload Capability */
#define HDA_INPAY       0x06    /* Input Payload Capability */
#define HDA_GCTL        0x08    /* Global Control */
#define HDA_WAKEEN      0x0C    /* Wake Enable */
#define HDA_STATESTS    0x0E    /* State Change Status */
#define HDA_GSTS        0x10    /* Global Status */
#define HDA_INTCTL      0x20    /* Interrupt Control */
#define HDA_INTSTS      0x24    /* Interrupt Status */
#define HDA_WALCLK      0x30    /* Wall Clock Counter */
#define HDA_SSYNC       0x38    /* Stream Synchronization */
#define HDA_CORBLBASE   0x40    /* CORB Lower Base Address */
#define HDA_CORBUBASE   0x44    /* CORB Upper Base Address */
#define HDA_CORBWP      0x48    /* CORB Write Pointer */
#define HDA_CORBRP      0x4A    /* CORB Read Pointer */
#define HDA_CORBCTL     0x4C    /* CORB Control */
#define HDA_CORBSTS     0x4D    /* CORB Status */
#define HDA_CORBSIZE    0x4E    /* CORB Size */
#define HDA_RIRBLBASE   0x50    /* RIRB Lower Base Address */
#define HDA_RIRBUBASE   0x54    /* RIRB Upper Base Address */
#define HDA_RIRBWP      0x58    /* RIRB Write Pointer */
#define HDA_RINTCNT     0x5A    /* Response Interrupt Count */
#define HDA_RIRBCTL     0x5C    /* RIRB Control */
#define HDA_RIRBSTS     0x5D    /* RIRB Status */
#define HDA_RIRBSIZE    0x5E    /* RIRB Size */

/* Stream descriptor registers (offset from stream base) */
#define HDA_SD_CTL      0x00    /* Stream Descriptor Control */
#define HDA_SD_STS      0x03    /* Stream Descriptor Status */
#define HDA_SD_LPIB     0x04    /* Link Position in Buffer */
#define HDA_SD_CBL      0x08    /* Cyclic Buffer Length */
#define HDA_SD_LVI      0x0C    /* Last Valid Index */
#define HDA_SD_FIFOS    0x10    /* FIFO Size */
#define HDA_SD_FMT      0x12    /* Format */
#define HDA_SD_BDLPL    0x18    /* BDL Pointer Lower */
#define HDA_SD_BDLPU    0x1C    /* BDL Pointer Upper */

/* ============ HDA Verbs ============ */
#define HDA_VERB_GET_PARAM          0xF00
#define HDA_VERB_GET_CONN_SELECT    0xF01
#define HDA_VERB_SET_CONN_SELECT    0x701
#define HDA_VERB_GET_CONN_LIST      0xF02
#define HDA_VERB_GET_STREAM_FMT     0xA00
#define HDA_VERB_SET_STREAM_FMT     0x200
#define HDA_VERB_GET_PIN_WIDGET     0xF07
#define HDA_VERB_SET_PIN_WIDGET     0x707
#define HDA_VERB_GET_POWER_STATE    0xF05
#define HDA_VERB_SET_POWER_STATE    0x705
#define HDA_VERB_GET_CONV_STREAM    0xF06
#define HDA_VERB_SET_CONV_STREAM    0x706
#define HDA_VERB_GET_VOLUME         0xB00
#define HDA_VERB_SET_VOLUME         0x300

/* ============ Parameters ============ */
#define HDA_PARAM_VENDOR_ID         0x00
#define HDA_PARAM_REVISION_ID       0x02
#define HDA_PARAM_NODE_COUNT        0x04
#define HDA_PARAM_FN_GROUP_TYPE     0x05
#define HDA_PARAM_AUDIO_CAPS        0x09
#define HDA_PARAM_STREAM_FORMATS    0x0B
#define HDA_PARAM_PIN_CAPS          0x0C
#define HDA_PARAM_VOLUME_CAPS       0x12
#define HDA_PARAM_CONN_LIST_LEN     0x0E

/* ============ Types ============ */
typedef struct {
    void            *base;          /* MMIO base address */
    uint32_t        num_streams;
    uint32_t        num_outputs;
    uint32_t        num_inputs;
    
    /* CORB/RIRB */
    uint32_t       *corb;
    uint64_t       *rirb;
    uint32_t        corb_size;
    uint32_t        rirb_size;
    uint32_t        corb_wp;
    uint32_t        rirb_rp;
    
    /* Codec info */
    uint16_t        vendor_id;
    uint16_t        device_id;
    uint8_t         revision;
    
    /* State */
    int             initialized;
} hda_controller_t;

typedef struct {
    uint64_t        address;
    uint32_t        length;
    uint32_t        flags;
} __attribute__((packed)) hda_bdl_entry_t;

/* ============ MMIO Access ============ */

static inline uint32_t hda_read32(hda_controller_t *hda, uint32_t offset) {
    return *(volatile uint32_t*)((uint8_t*)hda->base + offset);
}

static inline void hda_write32(hda_controller_t *hda, uint32_t offset, uint32_t value) {
    *(volatile uint32_t*)((uint8_t*)hda->base + offset) = value;
}

static inline uint16_t hda_read16(hda_controller_t *hda, uint32_t offset) {
    return *(volatile uint16_t*)((uint8_t*)hda->base + offset);
}

static inline void hda_write16(hda_controller_t *hda, uint32_t offset, uint16_t value) {
    *(volatile uint16_t*)((uint8_t*)hda->base + offset) = value;
}

static inline uint8_t hda_read8(hda_controller_t *hda, uint32_t offset) {
    return *(volatile uint8_t*)((uint8_t*)hda->base + offset);
}

static inline void hda_write8(hda_controller_t *hda, uint32_t offset, uint8_t value) {
    *(volatile uint8_t*)((uint8_t*)hda->base + offset) = value;
}

/* ============ Controller Functions ============ */

/**
 * Reset HDA controller
 */
int hda_reset(hda_controller_t *hda) {
    /* Enter reset */
    hda_write32(hda, HDA_GCTL, 0);
    
    /* Wait for reset */
    for (int i = 0; i < 1000; i++) {
        if (!(hda_read32(hda, HDA_GCTL) & 1)) {
            break;
        }
        /* delay */
        for (volatile int j = 0; j < 10000; j++);
    }
    
    /* Exit reset */
    hda_write32(hda, HDA_GCTL, 1);
    
    /* Wait for controller ready */
    for (int i = 0; i < 1000; i++) {
        if (hda_read32(hda, HDA_GCTL) & 1) {
            return 0;
        }
        for (volatile int j = 0; j < 10000; j++);
    }
    
    return -1;
}

/**
 * Initialize HDA controller
 */
int hda_init(hda_controller_t *hda, void *mmio_base) {
    if (!hda || !mmio_base) return -1;
    
    hda->base = mmio_base;
    
    /* Reset controller */
    if (hda_reset(hda) < 0) {
        return -1;
    }
    
    /* Read capabilities */
    uint16_t gcap = hda_read16(hda, HDA_GCAP);
    hda->num_outputs = (gcap >> 12) & 0xF;
    hda->num_inputs = (gcap >> 8) & 0xF;
    hda->num_streams = hda->num_outputs + hda->num_inputs;
    
    /* Enable interrupts */
    hda_write32(hda, HDA_INTCTL, 0x80000000 | ((1 << hda->num_streams) - 1));
    
    hda->initialized = 1;
    
    return 0;
}

/**
 * Send verb to codec
 */
uint32_t hda_send_verb(hda_controller_t *hda, uint8_t codec, uint8_t node, 
                        uint32_t verb, uint32_t param) {
    if (!hda || !hda->initialized) return 0;
    
    /* Build command */
    uint32_t cmd = ((uint32_t)codec << 28) | 
                   ((uint32_t)node << 20) | 
                   (verb << 8) | 
                   (param & 0xFF);
    
    /* Write to CORB */
    if (hda->corb) {
        hda->corb[hda->corb_wp] = cmd;
        hda->corb_wp = (hda->corb_wp + 1) % hda->corb_size;
        hda_write16(hda, HDA_CORBWP, hda->corb_wp);
    }
    
    /* Wait for response */
    for (int i = 0; i < 1000; i++) {
        uint16_t wp = hda_read16(hda, HDA_RIRBWP);
        if (wp != hda->rirb_rp && hda->rirb) {
            hda->rirb_rp = wp;
            return (uint32_t)(hda->rirb[wp] & 0xFFFFFFFF);
        }
        for (volatile int j = 0; j < 1000; j++);
    }
    
    return 0;
}

/**
 * Set output volume
 */
int hda_set_volume(hda_controller_t *hda, uint8_t codec, uint8_t node, 
                   uint8_t left, uint8_t right) {
    /* Set left channel */
    hda_send_verb(hda, codec, node, HDA_VERB_SET_VOLUME, 
                  0x9000 | (left & 0x7F));
    
    /* Set right channel */
    hda_send_verb(hda, codec, node, HDA_VERB_SET_VOLUME, 
                  0xA000 | (right & 0x7F));
    
    return 0;
}

/**
 * Configure output stream
 */
int hda_configure_stream(hda_controller_t *hda, int stream_id,
                          uint32_t sample_rate, uint8_t channels, uint8_t bits) {
    if (!hda || stream_id < 0 || stream_id >= 8) return -1;
    
    uint32_t stream_base = 0x80 + stream_id * 0x20;
    
    /* Stop stream */
    hda_write8(hda, stream_base + HDA_SD_CTL, 0);
    
    /* Format: bits[15:14]=type, bits[13:11]=base, bits[10:8]=mult, 
       bits[7:4]=div, bits[3:0]=chan */
    uint16_t fmt = 0;
    
    /* Sample rate */
    switch (sample_rate) {
        case 48000: fmt |= 0x0000; break;
        case 44100: fmt |= 0x4000; break;
        case 96000: fmt |= 0x0800; break;
        case 192000: fmt |= 0x1800; break;
        default: fmt |= 0x0000; break;
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
    
    hda_write16(hda, stream_base + HDA_SD_FMT, fmt);
    
    return 0;
}

/**
 * Start playback
 */
int hda_start_stream(hda_controller_t *hda, int stream_id) {
    if (!hda || stream_id < 0 || stream_id >= 8) return -1;
    
    uint32_t stream_base = 0x80 + stream_id * 0x20;
    
    /* Run + interrupt enable */
    hda_write8(hda, stream_base + HDA_SD_CTL, 0x02);
    hda_write8(hda, stream_base + HDA_SD_CTL + 2, 0x1C);
    
    return 0;
}

/**
 * Stop playback
 */
int hda_stop_stream(hda_controller_t *hda, int stream_id) {
    if (!hda || stream_id < 0 || stream_id >= 8) return -1;
    
    uint32_t stream_base = 0x80 + stream_id * 0x20;
    
    hda_write8(hda, stream_base + HDA_SD_CTL, 0x00);
    
    return 0;
}

/**
 * Cleanup HDA controller
 */
void hda_cleanup(hda_controller_t *hda) {
    if (!hda) return;
    
    /* Stop all streams */
    for (int i = 0; i < 8; i++) {
        hda_stop_stream(hda, i);
    }
    
    /* Disable interrupts */
    hda_write32(hda, HDA_INTCTL, 0);
    
    /* Reset controller */
    hda_write32(hda, HDA_GCTL, 0);
    
    hda->initialized = 0;
}
