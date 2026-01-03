/*
 * NXPen Kernel Driver (nxpen.kdrv)
 * 
 * NeolyxOS Stylus/Pen Digitizer Driver
 * 
 * Architecture:
 *   [ NXPen Kernel Driver ]
 *        ↕ Digitizer Protocols
 *   [ Wacom | Microsoft Pen | USI | MPP ]
 *        ↕ Kernel Events
 *   [ Drawing Apps / UI ]
 * 
 * Features:
 *   - Pressure sensitivity (4096 levels)
 *   - Tilt detection (X/Y)
 *   - Button events (tip, barrel, eraser)
 *   - Multi-pen support
 *   - Proximity detection
 * 
 * Supported: Wacom, Microsoft Pen Protocol, USI (Universal Stylus Initiative)
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXPEN_KDRV_H
#define NXPEN_KDRV_H

#include <stdint.h>
#include <stddef.h>

/* ============ Driver Metadata ============ */

#define NXPEN_KDRV_VERSION    "1.0.0"
#define NXPEN_KDRV_ABI        1

/* ============ Pen Protocol ============ */

typedef enum {
    NXPEN_PROTO_UNKNOWN = 0,
    NXPEN_PROTO_WACOM,          /* Wacom EMR/AES */
    NXPEN_PROTO_MPP,            /* Microsoft Pen Protocol */
    NXPEN_PROTO_USI,            /* Universal Stylus Initiative */
    NXPEN_PROTO_APPLE_PENCIL,   /* Apple Pencil */
    NXPEN_PROTO_NTRIG,          /* N-Trig (legacy) */
    NXPEN_PROTO_ELAN,           /* Elan digitizer */
} nxpen_protocol_t;

/* ============ Pen Type ============ */

typedef enum {
    NXPEN_TYPE_STYLUS = 0,
    NXPEN_TYPE_ERASER,
    NXPEN_TYPE_FINGER,
    NXPEN_TYPE_AIRBRUSH,
    NXPEN_TYPE_MOUSE,
} nxpen_type_t;

/* ============ Button State ============ */

typedef enum {
    NXPEN_BTN_TIP       = (1 << 0),  /* Pen tip touching */
    NXPEN_BTN_BARREL1   = (1 << 1),  /* First barrel button */
    NXPEN_BTN_BARREL2   = (1 << 2),  /* Second barrel button */
    NXPEN_BTN_ERASER    = (1 << 3),  /* Eraser end touching */
    NXPEN_BTN_INVERT    = (1 << 4),  /* Pen inverted */
} nxpen_button_t;

/* ============ Digitizer Info ============ */

typedef struct {
    uint32_t            id;
    nxpen_protocol_t    protocol;
    char                name[64];
    char                manufacturer[32];
    
    /* Capabilities */
    uint32_t            max_x;
    uint32_t            max_y;
    uint32_t            max_pressure;   /* e.g., 4096, 8192 */
    int32_t             tilt_min;       /* -60 to +60 degrees */
    int32_t             tilt_max;
    uint8_t             supports_tilt;
    uint8_t             supports_rotation;
    uint8_t             supports_eraser;
    uint8_t             multi_pen;      /* Multiple pens supported */
    
    /* Physical area (mm) */
    uint32_t            width_mm;
    uint32_t            height_mm;
    
    /* Associated display */
    int32_t             display_id;     /* -1 if standalone tablet */
} nxpen_digitizer_t;

/* ============ Pen Device ============ */

typedef struct {
    uint32_t            id;
    uint64_t            serial;         /* Unique pen ID */
    nxpen_type_t        type;
    char                name[32];
    
    /* Capabilities */
    uint8_t             has_pressure;
    uint8_t             has_tilt;
    uint8_t             has_rotation;
    uint8_t             has_buttons;
    uint8_t             button_count;
    
    /* Battery (if applicable) */
    uint8_t             has_battery;
    uint8_t             battery_percent;
    uint8_t             is_charging;
} nxpen_device_t;

/* ============ Pen Event ============ */

typedef struct {
    uint64_t            timestamp_us;
    uint32_t            digitizer_id;
    uint32_t            pen_id;
    
    /* Position (raw coordinates) */
    uint32_t            x;
    uint32_t            y;
    
    /* Pressure (0 - max_pressure) */
    uint32_t            pressure;
    
    /* Tilt (degrees * 10 for 0.1 precision) */
    int32_t             tilt_x;
    int32_t             tilt_y;
    
    /* Rotation (degrees * 10) */
    int32_t             rotation;
    
    /* Distance from surface (mm * 10) */
    uint32_t            distance;
    
    /* Button state */
    uint32_t            buttons;        /* Bitmask of nxpen_button_t */
    
    /* State */
    uint8_t             in_proximity;
    uint8_t             touching;
} nxpen_event_t;

/* ============ Event Callback ============ */

typedef void (*nxpen_event_callback_t)(const nxpen_event_t *event, void *user_data);

/* ============ Kernel Driver API ============ */

/**
 * Initialize pen subsystem
 */
int nxpen_kdrv_init(void);

/**
 * Shutdown
 */
void nxpen_kdrv_shutdown(void);

/**
 * Get number of digitizers
 */
int nxpen_kdrv_digitizer_count(void);

/**
 * Get digitizer info
 */
int nxpen_kdrv_digitizer_info(int index, nxpen_digitizer_t *info);

/**
 * Get number of active pens
 */
int nxpen_kdrv_pen_count(void);

/**
 * Get pen device info
 */
int nxpen_kdrv_pen_info(int index, nxpen_device_t *info);

/**
 * Poll for pen event (non-blocking)
 * Returns 1 if event available, 0 if none, -1 on error
 */
int nxpen_kdrv_poll(nxpen_event_t *event);

/**
 * Register event callback
 */
int nxpen_kdrv_register_callback(nxpen_event_callback_t callback, void *user_data);

/**
 * Unregister callback
 */
void nxpen_kdrv_unregister_callback(void);

/**
 * Get protocol name
 */
const char *nxpen_kdrv_protocol_name(nxpen_protocol_t proto);

/**
 * Get pen type name
 */
const char *nxpen_kdrv_type_name(nxpen_type_t type);

/**
 * Debug output
 */
void nxpen_kdrv_debug(void);

#endif /* NXPEN_KDRV_H */
