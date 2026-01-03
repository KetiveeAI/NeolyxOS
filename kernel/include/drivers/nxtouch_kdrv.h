/*
 * NXTouch Kernel Driver (nxtouch_kdrv)
 * 
 * Touch screen driver for NeolyxOS
 * Supports: Single/multi-touch, gestures, pressure
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXTOUCH_KDRV_H
#define NXTOUCH_KDRV_H

#include <stdint.h>

#define NXTOUCH_KDRV_VERSION "1.0.0"
#define NXTOUCH_MAX_POINTS   10

/* Touch event types */
typedef enum {
    NXTOUCH_DOWN = 0,
    NXTOUCH_UP,
    NXTOUCH_MOVE,
    NXTOUCH_CANCEL
} nxtouch_event_type_t;

/* Touch point */
typedef struct {
    int32_t  id;
    int32_t  x;
    int32_t  y;
    uint16_t pressure;
    uint16_t width;
    uint16_t height;
    nxtouch_event_type_t type;
} nxtouch_point_t;

/* Touch event (multi-touch) */
typedef struct {
    uint32_t timestamp;
    uint8_t  point_count;
    nxtouch_point_t points[NXTOUCH_MAX_POINTS];
} nxtouch_event_t;

/* Touch device info */
typedef struct {
    char     name[64];
    uint16_t max_x;
    uint16_t max_y;
    uint8_t  max_points;
    uint8_t  pressure_support;
    uint8_t  connected;
} nxtouch_info_t;

/* Gesture types */
typedef enum {
    NXGESTURE_NONE = 0,
    NXGESTURE_TAP,
    NXGESTURE_DOUBLE_TAP,
    NXGESTURE_LONG_PRESS,
    NXGESTURE_SWIPE_LEFT,
    NXGESTURE_SWIPE_RIGHT,
    NXGESTURE_SWIPE_UP,
    NXGESTURE_SWIPE_DOWN,
    NXGESTURE_PINCH_IN,
    NXGESTURE_PINCH_OUT,
    NXGESTURE_ROTATE
} nxtouch_gesture_t;

/* API */
int  nxtouch_kdrv_init(void);
void nxtouch_kdrv_shutdown(void);
int  nxtouch_kdrv_poll(nxtouch_event_t *event);
int  nxtouch_kdrv_get_info(nxtouch_info_t *info);
nxtouch_gesture_t nxtouch_kdrv_detect_gesture(void);
void nxtouch_kdrv_debug(void);

#endif /* NXTOUCH_KDRV_H */
