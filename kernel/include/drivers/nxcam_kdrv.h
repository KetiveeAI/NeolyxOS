/*
 * NXCamera Kernel Driver (nxcam_kdrv)
 * 
 * Camera/webcam driver for NeolyxOS
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXCAM_KDRV_H
#define NXCAM_KDRV_H

#include <stdint.h>

#define NXCAM_KDRV_VERSION "1.0.0"
#define NXCAM_MAX_DEVICES  4

typedef enum {
    NXCAM_FMT_RGB24 = 0,
    NXCAM_FMT_YUYV,
    NXCAM_FMT_MJPEG,
    NXCAM_FMT_NV12
} nxcam_format_t;

typedef struct {
    uint32_t id;
    char     name[64];
    uint16_t width;
    uint16_t height;
    uint8_t  fps;
    uint8_t  connected;
    uint8_t  streaming;
    nxcam_format_t format;
} nxcam_info_t;

typedef struct {
    void    *data;
    uint32_t size;
    uint32_t timestamp;
    uint16_t width;
    uint16_t height;
    nxcam_format_t format;
} nxcam_frame_t;

int  nxcam_kdrv_init(void);
void nxcam_kdrv_shutdown(void);
int  nxcam_kdrv_count(void);
int  nxcam_kdrv_get_info(int id, nxcam_info_t *info);
int  nxcam_kdrv_start_stream(int id, uint16_t width, uint16_t height);
int  nxcam_kdrv_stop_stream(int id);
int  nxcam_kdrv_get_frame(int id, nxcam_frame_t *frame);
void nxcam_kdrv_debug(void);

#endif /* NXCAM_KDRV_H */
