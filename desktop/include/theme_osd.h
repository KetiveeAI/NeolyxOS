/*
 * NeolyxOS Theme OSD (On-Screen Display)
 * 
 * Shows floating indicator when theme/appearance changes.
 * Currently supports: Dark Mode toggle
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#ifndef THEME_OSD_H
#define THEME_OSD_H

#include <stdint.h>

/* OSD State */
typedef struct {
    int visible;
    int dark_mode_on;       /* 1 = dark, 0 = light */
    uint64_t show_time;
    uint8_t opacity;
    
    int32_t x, y;
    uint32_t width, height;
} theme_osd_state_t;

/* API */
void theme_osd_init(uint32_t screen_w, uint32_t screen_h);
void theme_osd_show_dark_mode(int enabled);
void theme_osd_hide(void);
int theme_osd_is_visible(void);
void theme_osd_tick(void);
void theme_osd_render(uint32_t *fb, uint32_t pitch);

#endif /* THEME_OSD_H */
