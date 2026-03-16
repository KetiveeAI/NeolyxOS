/*
 * NeolyxOS Notification Center
 * 
 * Slide-in notification panel from top-right.
 * Displays system and app notifications with dismiss.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NOTIFICATION_CENTER_H
#define NOTIFICATION_CENTER_H

#include <stdint.h>

/* ============ Notification Entry ============ */

typedef struct {
    uint32_t id;
    char app_name[32];
    char title[64];
    char message[128];
    uint32_t icon_color;
    uint64_t timestamp;
    int read;
    int dismissed;
} notification_t;

/* ============ Notification Center State ============ */

#define NC_MAX_NOTIFICATIONS 16

typedef struct {
    int visible;
    notification_t notifications[NC_MAX_NOTIFICATIONS];
    int count;
    int hover_idx;              /* Hovered notification (-1 if none) */
    int scroll_offset;          /* Scroll position */
    
    /* Geometry */
    int x, y, width, height;
    uint32_t screen_width, screen_height;
    
    /* Animation */
    int slide_progress;         /* 0-100 for slide-in */
} notification_center_state_t;

/* ============ Colors ============ */

#define NC_BG_PRIMARY       0xC01E1E2E
#define NC_BG_NOTIFICATION  0x40303040
#define NC_BG_HOVER         0x50404050
#define NC_ACCENT           0xFF007AFF
#define NC_TEXT_PRIMARY     0xFFFFFFFF
#define NC_TEXT_SECONDARY   0x99FFFFFF
#define NC_TEXT_DIM         0x60FFFFFF

/* ============ Dimensions ============ */

#define NC_WIDTH            320
#define NC_HEIGHT           400
#define NC_MARGIN           20
#define NC_CORNER_R         12
#define NC_NOTIF_HEIGHT     72
#define NC_NOTIF_GAP        8

/* ============ API Functions ============ */

void notification_center_init(uint32_t screen_w, uint32_t screen_h);
void notification_center_toggle(void);
void notification_center_show(void);
void notification_center_hide(void);
int notification_center_is_visible(void);

void notification_center_render(uint32_t *fb, uint32_t pitch);
int notification_center_handle_input(int mouse_x, int mouse_y, uint8_t buttons, uint8_t prev_buttons);
void notification_center_tick(void);

/* Notification management */
uint32_t notification_push(const char *app_name, const char *title, const char *message, uint32_t icon_color);
void notification_dismiss(uint32_t id);
void notification_clear_all(void);
int notification_get_count(void);

notification_center_state_t* notification_center_get_state(void);

#endif /* NOTIFICATION_CENTER_H */
