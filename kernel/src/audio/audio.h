/*
 * NeolyxOS Kernel Audio Subsystem
 * 
 * Low-level audio hardware abstraction:
 * - Device detection and enumeration
 * - Hardware codec control
 * - DMA buffer management
 * - Interrupt handling
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef KERNEL_AUDIO_H
#define KERNEL_AUDIO_H

#include <stdint.h>
#include <stddef.h>

/* Audio device types */
#define AUDIO_DEV_TYPE_UNKNOWN      0
#define AUDIO_DEV_TYPE_HDA          1
#define AUDIO_DEV_TYPE_AC97         2
#define AUDIO_DEV_TYPE_USB          3
#define AUDIO_DEV_TYPE_HDMI         4
#define AUDIO_DEV_TYPE_SPDIF        5

/* Audio device capabilities */
#define AUDIO_CAP_PLAYBACK          (1 << 0)
#define AUDIO_CAP_CAPTURE           (1 << 1)
#define AUDIO_CAP_DUPLEX            (1 << 2)
#define AUDIO_CAP_SURROUND          (1 << 3)
#define AUDIO_CAP_SPDIF             (1 << 4)

/* Sample formats */
#define AUDIO_FMT_S8                0
#define AUDIO_FMT_S16_LE            1
#define AUDIO_FMT_S24_LE            2
#define AUDIO_FMT_S32_LE            3
#define AUDIO_FMT_FLOAT32           4

/* Error codes */
#define AUDIO_OK                    0
#define AUDIO_ERR_NO_DEVICE         (-1)
#define AUDIO_ERR_BUSY              (-2)
#define AUDIO_ERR_IO                (-3)
#define AUDIO_ERR_INVALID           (-4)
#define AUDIO_ERR_NO_MEMORY         (-5)

/* Maximum devices */
#define AUDIO_MAX_DEVICES           16
#define AUDIO_MAX_STREAMS           32

/* Forward declarations */
typedef struct audio_device audio_device_t;
typedef struct audio_stream audio_stream_t;
typedef struct audio_driver audio_driver_t;

/* Audio device info */
typedef struct {
    char            name[64];
    char            vendor[32];
    uint32_t        type;
    uint32_t        caps;
    uint32_t        min_rate;
    uint32_t        max_rate;
    uint32_t        min_channels;
    uint32_t        max_channels;
    uint32_t        formats;            /* Bitmask of supported formats */
} audio_device_info_t;

/* Stream configuration */
typedef struct {
    uint32_t        sample_rate;
    uint32_t        channels;
    uint32_t        format;
    uint32_t        buffer_frames;
    uint32_t        period_frames;
} audio_stream_config_t;

/* DMA buffer descriptor */
typedef struct {
    uint64_t        phys_addr;
    void           *virt_addr;
    size_t          size;
    size_t          frames;
} audio_buffer_t;

/* Driver operations */
typedef struct {
    int  (*probe)(audio_device_t *dev);
    int  (*init)(audio_device_t *dev);
    void (*remove)(audio_device_t *dev);
    int  (*open)(audio_device_t *dev, audio_stream_config_t *config);
    void (*close)(audio_device_t *dev);
    int  (*start)(audio_device_t *dev);
    int  (*stop)(audio_device_t *dev);
    int  (*write)(audio_device_t *dev, const void *data, size_t frames);
    int  (*read)(audio_device_t *dev, void *data, size_t frames);
    int  (*set_volume)(audio_device_t *dev, int left, int right);
    int  (*get_volume)(audio_device_t *dev, int *left, int *right);
} audio_driver_ops_t;

/* Audio driver */
struct audio_driver {
    const char             *name;
    uint32_t                type;
    const audio_driver_ops_t *ops;
};

/* Audio device */
struct audio_device {
    int                     id;
    audio_device_info_t     info;
    audio_driver_t         *driver;
    void                   *private_data;
    
    /* Hardware state */
    void                   *mmio_base;
    uint32_t                irq;
    int                     opened;
    int                     running;
    
    /* Current configuration */
    audio_stream_config_t   config;
    
    /* DMA buffers */
    audio_buffer_t          play_buffer;
    audio_buffer_t          capture_buffer;
    size_t                  write_pos;
    size_t                  read_pos;
};

/* Audio stream */
struct audio_stream {
    int                     id;
    audio_device_t         *device;
    int                     direction;      /* 0=playback, 1=capture */
    audio_stream_config_t   config;
    int                     active;
};

/* Public API */

/* Subsystem init/cleanup */
int  audio_init(void);
void audio_cleanup(void);

/* Device enumeration */
int  audio_get_device_count(void);
int  audio_get_device_info(int index, audio_device_info_t *info);
audio_device_t* audio_get_device(int index);
audio_device_t* audio_get_default_device(void);

/* Device control */
int  audio_device_open(audio_device_t *dev, audio_stream_config_t *config);
void audio_device_close(audio_device_t *dev);
int  audio_device_start(audio_device_t *dev);
int  audio_device_stop(audio_device_t *dev);

/* Audio I/O */
int  audio_write(audio_device_t *dev, const void *data, size_t frames);
int  audio_read(audio_device_t *dev, void *data, size_t frames);

/* Volume control */
int  audio_set_volume(audio_device_t *dev, int left, int right);
int  audio_get_volume(audio_device_t *dev, int *left, int *right);

/* Driver registration */
int  audio_register_driver(audio_driver_t *driver);
void audio_unregister_driver(audio_driver_t *driver);

/* Device registration (called by drivers) */
int  audio_register_device(audio_device_t *dev);
void audio_unregister_device(audio_device_t *dev);

#endif /* KERNEL_AUDIO_H */
