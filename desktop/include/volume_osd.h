/*
 * NeolyxOS Volume OSD Header
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#ifndef VOLUME_OSD_H
#define VOLUME_OSD_H

#include <stdint.h>

/* OSD state structure */
typedef struct {
    int visible;
    int volume;         /* 0-100 */
    int muted;
    uint64_t show_time;
    uint8_t opacity;    /* 0-255 */
    int32_t x, y;
    uint32_t width, height;
} volume_osd_state_t;

/* Initialize volume OSD */
void volume_osd_init(uint32_t screen_w, uint32_t screen_h);

/* Show volume OSD with current volume level */
void volume_osd_show(int volume, int muted);

/* Hide volume OSD immediately */
void volume_osd_hide(void);

/* Check if OSD is visible */
int volume_osd_is_visible(void);

/* Update OSD state (call each frame) */
void volume_osd_tick(void);

/* Render volume OSD */
void volume_osd_render(uint32_t *fb, uint32_t pitch);

/* Get current state */
volume_osd_state_t volume_osd_get_state(void);

#endif /* VOLUME_OSD_H */
