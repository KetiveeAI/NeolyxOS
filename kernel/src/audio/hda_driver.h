/*
 * NeolyxOS Intel HD Audio Kernel Driver
 * 
 * Low-level HDA controller driver:
 * - CORB/RIRB command interface
 * - Stream descriptor management
 * - Buffer Descriptor List (BDL)
 * - Codec communication
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef KERNEL_HDA_DRIVER_H
#define KERNEL_HDA_DRIVER_H

#include "audio.h"

/* HDA Register offsets */
#define HDA_REG_GCAP        0x00
#define HDA_REG_VMIN        0x02
#define HDA_REG_VMAJ        0x03
#define HDA_REG_GCTL        0x08
#define HDA_REG_WAKEEN      0x0C
#define HDA_REG_STATESTS    0x0E
#define HDA_REG_INTCTL      0x20
#define HDA_REG_INTSTS      0x24
#define HDA_REG_WALCLK      0x30
#define HDA_REG_CORBLBASE   0x40
#define HDA_REG_CORBUBASE   0x44
#define HDA_REG_CORBWP      0x48
#define HDA_REG_CORBRP      0x4A
#define HDA_REG_CORBCTL     0x4C
#define HDA_REG_CORBSTS     0x4D
#define HDA_REG_CORBSIZE    0x4E
#define HDA_REG_RIRBLBASE   0x50
#define HDA_REG_RIRBUBASE   0x54
#define HDA_REG_RIRBWP      0x58
#define HDA_REG_RINTCNT     0x5A
#define HDA_REG_RIRBCTL     0x5C
#define HDA_REG_RIRBSTS     0x5D
#define HDA_REG_RIRBSIZE    0x5E

/* Stream descriptor registers (base + stream * 0x20) */
#define HDA_SD_CTL          0x00
#define HDA_SD_STS          0x03
#define HDA_SD_LPIB         0x04
#define HDA_SD_CBL          0x08
#define HDA_SD_LVI          0x0C
#define HDA_SD_FIFOS        0x10
#define HDA_SD_FMT          0x12
#define HDA_SD_BDLPL        0x18
#define HDA_SD_BDLPU        0x1C

/* HDA Verbs */
#define HDA_VERB_GET_PARAM          0xF00
#define HDA_VERB_GET_CONN_SEL       0xF01
#define HDA_VERB_SET_CONN_SEL       0x701
#define HDA_VERB_GET_PIN_CTRL       0xF07
#define HDA_VERB_SET_PIN_CTRL       0x707
#define HDA_VERB_GET_AMP_GAIN       0xB00
#define HDA_VERB_SET_AMP_GAIN       0x300
#define HDA_VERB_GET_CONV_FMT       0xA00
#define HDA_VERB_SET_CONV_FMT       0x200
#define HDA_VERB_GET_CONV_CHAN      0xF06
#define HDA_VERB_SET_CONV_CHAN      0x706
#define HDA_VERB_GET_POWER          0xF05
#define HDA_VERB_SET_POWER          0x705

/* Parameters */
#define HDA_PARAM_VENDOR_ID         0x00
#define HDA_PARAM_REV_ID            0x02
#define HDA_PARAM_SUB_NODE_COUNT    0x04
#define HDA_PARAM_FN_GROUP_TYPE     0x05
#define HDA_PARAM_AUDIO_CAPS        0x09
#define HDA_PARAM_PIN_CAPS          0x0C
#define HDA_PARAM_CONN_LIST_LEN     0x0E
#define HDA_PARAM_POWER_STATE       0x0F
#define HDA_PARAM_VOL_CAPS          0x12

/* BDL entry */
typedef struct {
    uint64_t        address;
    uint32_t        length;
    uint32_t        flags;
} __attribute__((packed)) hda_bdl_entry_t;

/* HDA driver state */
typedef struct {
    void           *mmio;
    
    /* CORB/RIRB */
    uint32_t       *corb;
    uint64_t       *rirb;
    uint64_t        corb_phys;
    uint64_t        rirb_phys;
    uint32_t        corb_size;
    uint32_t        rirb_size;
    uint32_t        corb_wp;
    uint32_t        rirb_rp;
    
    /* Streams */
    int             num_output;
    int             num_input;
    int             active_stream;
    
    /* BDL */
    hda_bdl_entry_t *bdl;
    uint64_t        bdl_phys;
    
    /* DMA buffer */
    void           *dma_buffer;
    uint64_t        dma_phys;
    size_t          dma_size;
    
    /* Codec info */
    uint16_t        codec_vendor;
    uint16_t        codec_device;
    uint8_t         codec_addr;
    uint8_t         afg_node;
    uint8_t         dac_node;
    uint8_t         out_node;
} hda_state_t;

/* Driver functions */
int  hda_driver_probe(audio_device_t *dev);
int  hda_driver_init(audio_device_t *dev);
void hda_driver_remove(audio_device_t *dev);
int  hda_driver_open(audio_device_t *dev, audio_stream_config_t *config);
void hda_driver_close(audio_device_t *dev);
int  hda_driver_start(audio_device_t *dev);
int  hda_driver_stop(audio_device_t *dev);
int  hda_driver_write(audio_device_t *dev, const void *data, size_t frames);
int  hda_driver_read(audio_device_t *dev, void *data, size_t frames);
int  hda_driver_set_volume(audio_device_t *dev, int left, int right);
int  hda_driver_get_volume(audio_device_t *dev, int *left, int *right);

/* Register HDA driver with audio subsystem */
void hda_driver_register(void);

#endif /* KERNEL_HDA_DRIVER_H */
