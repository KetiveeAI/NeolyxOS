/*
 * NXPen Kernel Driver Implementation
 * 
 * Stylus/Pen Digitizer Support
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "drivers/nxpen_kdrv.h"
#include <stdint.h>

/* External kernel interfaces */
extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pit_get_ticks(void);

/* ============ Helpers ============ */

static inline void nx_strcpy(char *d, const char *s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static inline void nx_memset(void *p, int c, size_t n) {
    uint8_t *ptr = (uint8_t*)p;
    while (n--) *ptr++ = (uint8_t)c;
}

static void serial_dec(int val) {
    char buf[12];
    int i = 0;
    if (val < 0) { serial_putc('-'); val = -val; }
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) serial_putc(buf[--i]);
}

/* ============ Constants ============ */

#define MAX_DIGITIZERS      4
#define MAX_PENS            8
#define EVENT_QUEUE_SIZE    64

/* ============ State ============ */

static struct {
    int                     initialized;
    
    /* Digitizers */
    int                     digitizer_count;
    nxpen_digitizer_t       digitizers[MAX_DIGITIZERS];
    
    /* Active pens */
    int                     pen_count;
    nxpen_device_t          pens[MAX_PENS];
    
    /* Event queue (ring buffer) */
    nxpen_event_t           event_queue[EVENT_QUEUE_SIZE];
    int                     event_head;
    int                     event_tail;
    
    /* Callback */
    nxpen_event_callback_t  callback;
    void                   *callback_data;
} g_pen = {0};

/* ============ Event Queue ============ */

static void queue_event(const nxpen_event_t *event) {
    int next = (g_pen.event_head + 1) % EVENT_QUEUE_SIZE;
    
    /* Overwrite oldest if full */
    if (next == g_pen.event_tail) {
        g_pen.event_tail = (g_pen.event_tail + 1) % EVENT_QUEUE_SIZE;
    }
    
    g_pen.event_queue[g_pen.event_head] = *event;
    g_pen.event_head = next;
    
    /* Call callback if registered */
    if (g_pen.callback) {
        g_pen.callback(event, g_pen.callback_data);
    }
}

/* ============ Digitizer Detection ============ */

static void detect_digitizers(void) {
    g_pen.digitizer_count = 0;
    
    /* Simulate Wacom tablet detection */
    /* In real implementation, would scan USB/I2C/HID */
    
    nxpen_digitizer_t *dig = &g_pen.digitizers[g_pen.digitizer_count];
    nx_memset(dig, 0, sizeof(*dig));
    
    dig->id = g_pen.digitizer_count;
    dig->protocol = NXPEN_PROTO_WACOM;
    nx_strcpy(dig->name, "Wacom Intuos Pro", 64);
    nx_strcpy(dig->manufacturer, "Wacom Co., Ltd.", 32);
    
    dig->max_x = 62200;
    dig->max_y = 46100;
    dig->max_pressure = 8192;
    dig->tilt_min = -600;  /* -60.0 degrees */
    dig->tilt_max = 600;   /* +60.0 degrees */
    dig->supports_tilt = 1;
    dig->supports_rotation = 1;
    dig->supports_eraser = 1;
    dig->multi_pen = 1;
    
    dig->width_mm = 311;
    dig->height_mm = 216;
    dig->display_id = -1;  /* Standalone tablet */
    
    g_pen.digitizer_count++;
    
    /* Simulate built-in pen display (like Surface) */
    nxpen_digitizer_t *dig2 = &g_pen.digitizers[g_pen.digitizer_count];
    nx_memset(dig2, 0, sizeof(*dig2));
    
    dig2->id = g_pen.digitizer_count;
    dig2->protocol = NXPEN_PROTO_MPP;
    nx_strcpy(dig2->name, "Built-in Digitizer", 64);
    nx_strcpy(dig2->manufacturer, "NeolyxOS", 32);
    
    dig2->max_x = 27648;
    dig2->max_y = 15552;
    dig2->max_pressure = 4096;
    dig2->tilt_min = -450;
    dig2->tilt_max = 450;
    dig2->supports_tilt = 1;
    dig2->supports_rotation = 0;
    dig2->supports_eraser = 1;
    dig2->multi_pen = 0;
    
    dig2->width_mm = 293;
    dig2->height_mm = 165;
    dig2->display_id = 0;  /* Mapped to primary display */
    
    g_pen.digitizer_count++;
}

/* ============ Pen Detection ============ */

static void detect_pens(void) {
    g_pen.pen_count = 0;
    
    /* Simulate connected pen */
    nxpen_device_t *pen = &g_pen.pens[g_pen.pen_count];
    nx_memset(pen, 0, sizeof(*pen));
    
    pen->id = g_pen.pen_count;
    pen->serial = 0x0123456789ABCDEF;
    pen->type = NXPEN_TYPE_STYLUS;
    nx_strcpy(pen->name, "Wacom Pro Pen 2", 32);
    
    pen->has_pressure = 1;
    pen->has_tilt = 1;
    pen->has_rotation = 1;
    pen->has_buttons = 1;
    pen->button_count = 2;
    
    pen->has_battery = 0;  /* EMR pens don't need battery */
    
    g_pen.pen_count++;
    
    /* Simulate MPP pen */
    nxpen_device_t *pen2 = &g_pen.pens[g_pen.pen_count];
    nx_memset(pen2, 0, sizeof(*pen2));
    
    pen2->id = g_pen.pen_count;
    pen2->serial = 0xFEDCBA9876543210;
    pen2->type = NXPEN_TYPE_STYLUS;
    nx_strcpy(pen2->name, "Surface Pen", 32);
    
    pen2->has_pressure = 1;
    pen2->has_tilt = 1;
    pen2->has_rotation = 0;
    pen2->has_buttons = 1;
    pen2->button_count = 1;
    
    pen2->has_battery = 1;
    pen2->battery_percent = 78;
    pen2->is_charging = 0;
    
    g_pen.pen_count++;
}

/* ============ Public API ============ */

int nxpen_kdrv_init(void) {
    if (g_pen.initialized) {
        return 0;
    }
    
    serial_puts("[NXPen] Initializing v" NXPEN_KDRV_VERSION "\n");
    
    nx_memset(&g_pen, 0, sizeof(g_pen));
    
    detect_digitizers();
    detect_pens();
    
    g_pen.initialized = 1;
    
    serial_puts("[NXPen] Found ");
    serial_dec(g_pen.digitizer_count);
    serial_puts(" digitizer(s), ");
    serial_dec(g_pen.pen_count);
    serial_puts(" pen(s)\n");
    
    for (int i = 0; i < g_pen.digitizer_count; i++) {
        nxpen_digitizer_t *d = &g_pen.digitizers[i];
        serial_puts("  [");
        serial_dec(i);
        serial_puts("] ");
        serial_puts(d->name);
        serial_puts(" (");
        serial_puts(nxpen_kdrv_protocol_name(d->protocol));
        serial_puts(")\n");
    }
    
    return 0;
}

void nxpen_kdrv_shutdown(void) {
    if (!g_pen.initialized) return;
    serial_puts("[NXPen] Shutting down\n");
    g_pen.initialized = 0;
}

int nxpen_kdrv_digitizer_count(void) {
    return g_pen.digitizer_count;
}

int nxpen_kdrv_digitizer_info(int index, nxpen_digitizer_t *info) {
    if (index < 0 || index >= g_pen.digitizer_count) return -1;
    if (!info) return -2;
    *info = g_pen.digitizers[index];
    return 0;
}

int nxpen_kdrv_pen_count(void) {
    return g_pen.pen_count;
}

int nxpen_kdrv_pen_info(int index, nxpen_device_t *info) {
    if (index < 0 || index >= g_pen.pen_count) return -1;
    if (!info) return -2;
    *info = g_pen.pens[index];
    return 0;
}

int nxpen_kdrv_poll(nxpen_event_t *event) {
    if (!event) return -1;
    
    if (g_pen.event_head == g_pen.event_tail) {
        return 0;  /* No events */
    }
    
    *event = g_pen.event_queue[g_pen.event_tail];
    g_pen.event_tail = (g_pen.event_tail + 1) % EVENT_QUEUE_SIZE;
    
    return 1;
}

int nxpen_kdrv_register_callback(nxpen_event_callback_t callback, void *user_data) {
    g_pen.callback = callback;
    g_pen.callback_data = user_data;
    return 0;
}

void nxpen_kdrv_unregister_callback(void) {
    g_pen.callback = 0;
    g_pen.callback_data = 0;
}

const char *nxpen_kdrv_protocol_name(nxpen_protocol_t proto) {
    switch (proto) {
        case NXPEN_PROTO_WACOM:        return "Wacom";
        case NXPEN_PROTO_MPP:          return "Microsoft Pen Protocol";
        case NXPEN_PROTO_USI:          return "USI";
        case NXPEN_PROTO_APPLE_PENCIL: return "Apple Pencil";
        case NXPEN_PROTO_NTRIG:        return "N-Trig";
        case NXPEN_PROTO_ELAN:         return "Elan";
        default:                       return "Unknown";
    }
}

const char *nxpen_kdrv_type_name(nxpen_type_t type) {
    switch (type) {
        case NXPEN_TYPE_STYLUS:   return "Stylus";
        case NXPEN_TYPE_ERASER:   return "Eraser";
        case NXPEN_TYPE_FINGER:   return "Finger";
        case NXPEN_TYPE_AIRBRUSH: return "Airbrush";
        case NXPEN_TYPE_MOUSE:    return "Mouse";
        default:                  return "Unknown";
    }
}

void nxpen_kdrv_debug(void) {
    serial_puts("\n=== NXPen Debug ===\n");
    serial_puts("Digitizers: ");
    serial_dec(g_pen.digitizer_count);
    serial_puts("\n");
    
    for (int i = 0; i < g_pen.digitizer_count; i++) {
        nxpen_digitizer_t *d = &g_pen.digitizers[i];
        
        serial_puts("\nDigitizer ");
        serial_dec(i);
        serial_puts(": ");
        serial_puts(d->name);
        serial_puts("\n");
        
        serial_puts("  Protocol: ");
        serial_puts(nxpen_kdrv_protocol_name(d->protocol));
        serial_puts("\n");
        
        serial_puts("  Area: ");
        serial_dec(d->max_x);
        serial_puts("x");
        serial_dec(d->max_y);
        serial_puts(" (");
        serial_dec(d->width_mm);
        serial_puts("x");
        serial_dec(d->height_mm);
        serial_puts("mm)\n");
        
        serial_puts("  Pressure: ");
        serial_dec(d->max_pressure);
        serial_puts(" levels\n");
        
        serial_puts("  Features: ");
        if (d->supports_tilt) serial_puts("Tilt ");
        if (d->supports_rotation) serial_puts("Rotation ");
        if (d->supports_eraser) serial_puts("Eraser ");
        if (d->multi_pen) serial_puts("Multi-pen ");
        serial_puts("\n");
        
        if (d->display_id >= 0) {
            serial_puts("  Mapped to display: ");
            serial_dec(d->display_id);
            serial_puts("\n");
        }
    }
    
    serial_puts("\nPens: ");
    serial_dec(g_pen.pen_count);
    serial_puts("\n");
    
    for (int i = 0; i < g_pen.pen_count; i++) {
        nxpen_device_t *p = &g_pen.pens[i];
        
        serial_puts("  [");
        serial_dec(i);
        serial_puts("] ");
        serial_puts(p->name);
        serial_puts(" (");
        serial_puts(nxpen_kdrv_type_name(p->type));
        serial_puts(")");
        
        if (p->has_battery) {
            serial_puts(" Battery: ");
            serial_dec(p->battery_percent);
            serial_puts("%");
        }
        serial_puts("\n");
    }
    
    serial_puts("===================\n\n");
}

/* ============ IRQ Handler (called from HID/I2C driver) ============ */

void nxpen_irq_handler(uint32_t digitizer_id, uint32_t pen_id,
                       uint32_t x, uint32_t y, uint32_t pressure,
                       int32_t tilt_x, int32_t tilt_y,
                       uint32_t buttons, uint8_t touching) {
    if (!g_pen.initialized) return;
    
    nxpen_event_t event;
    nx_memset(&event, 0, sizeof(event));
    
    event.timestamp_us = pit_get_ticks() * 1000;  /* Approximate */
    event.digitizer_id = digitizer_id;
    event.pen_id = pen_id;
    event.x = x;
    event.y = y;
    event.pressure = pressure;
    event.tilt_x = tilt_x;
    event.tilt_y = tilt_y;
    event.buttons = buttons;
    event.in_proximity = 1;
    event.touching = touching;
    
    queue_event(&event);
}
