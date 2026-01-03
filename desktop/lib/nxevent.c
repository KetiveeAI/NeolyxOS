/*
 * NeolyxOS Event Bus System Implementation
 * 
 * Publish/subscribe event system for live settings updates.
 * Components subscribe to events, Settings.app publishes changes.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/nxevent.h"
#include <stddef.h>

/* ============ Constants ============ */

#define NX_MAX_SUBSCRIPTIONS 64
#define NX_MAX_PENDING_EVENTS 32

/* ============ Internal Structures ============ */

typedef struct {
    nx_event_type_t type_min;
    nx_event_type_t type_max;
    nx_event_handler_t handler;
    void *userdata;
    int active;
} nx_subscription_t;

typedef struct {
    nx_event_t event;
    /* Event data storage (union of all event data types) */
    union {
        nx_theme_data_t theme;
        nx_dock_data_t dock;
        nx_window_data_t window;
        nx_app_data_t app;
    } data;
    int pending;
} nx_pending_event_t;

/* ============ Static State ============ */

static nx_subscription_t g_subscriptions[NX_MAX_SUBSCRIPTIONS];
static int g_subscription_count = 0;
static int g_next_subscription_id = 1;

static nx_pending_event_t g_pending[NX_MAX_PENDING_EVENTS];
static int g_pending_head = 0;
static int g_pending_tail = 0;

static int g_initialized = 0;
static uint32_t g_tick_counter = 0;

/* ============ String Helper ============ */

static void nxe_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* ============ Initialization ============ */

void nxevent_init(void) {
    if (g_initialized) return;
    
    for (int i = 0; i < NX_MAX_SUBSCRIPTIONS; i++) {
        g_subscriptions[i].active = 0;
    }
    g_subscription_count = 0;
    g_next_subscription_id = 1;
    
    for (int i = 0; i < NX_MAX_PENDING_EVENTS; i++) {
        g_pending[i].pending = 0;
    }
    g_pending_head = 0;
    g_pending_tail = 0;
    
    g_initialized = 1;
}

void nxevent_shutdown(void) {
    for (int i = 0; i < NX_MAX_SUBSCRIPTIONS; i++) {
        g_subscriptions[i].active = 0;
    }
    g_subscription_count = 0;
    g_pending_head = 0;
    g_pending_tail = 0;
    g_initialized = 0;
}

/* ============ Subscription ============ */

int nxevent_subscribe(nx_event_type_t type, nx_event_handler_t handler, void *userdata) {
    return nxevent_subscribe_range(type, type, handler, userdata);
}

int nxevent_subscribe_range(nx_event_type_t type_min, nx_event_type_t type_max,
                            nx_event_handler_t handler, void *userdata) {
    if (!g_initialized || !handler) return -1;
    if (g_subscription_count >= NX_MAX_SUBSCRIPTIONS) return -1;
    
    /* Find empty slot */
    for (int i = 0; i < NX_MAX_SUBSCRIPTIONS; i++) {
        if (!g_subscriptions[i].active) {
            g_subscriptions[i].type_min = type_min;
            g_subscriptions[i].type_max = type_max;
            g_subscriptions[i].handler = handler;
            g_subscriptions[i].userdata = userdata;
            g_subscriptions[i].active = g_next_subscription_id++;
            g_subscription_count++;
            return g_subscriptions[i].active;
        }
    }
    return -1;
}

void nxevent_unsubscribe(int subscription_id) {
    if (!g_initialized || subscription_id <= 0) return;
    
    for (int i = 0; i < NX_MAX_SUBSCRIPTIONS; i++) {
        if (g_subscriptions[i].active == subscription_id) {
            g_subscriptions[i].active = 0;
            g_subscription_count--;
            return;
        }
    }
}

/* ============ Publishing ============ */

static void dispatch_event(const nx_event_t *event) {
    for (int i = 0; i < NX_MAX_SUBSCRIPTIONS; i++) {
        if (g_subscriptions[i].active) {
            if (event->type >= g_subscriptions[i].type_min &&
                event->type <= g_subscriptions[i].type_max) {
                g_subscriptions[i].handler(event, g_subscriptions[i].userdata);
            }
        }
    }
}

void nxevent_publish(const nx_event_t *event) {
    if (!g_initialized || !event) return;
    
    /* Queue event for processing */
    int next_tail = (g_pending_tail + 1) % NX_MAX_PENDING_EVENTS;
    if (next_tail == g_pending_head) {
        /* Queue full - dispatch immediately */
        dispatch_event(event);
        return;
    }
    
    g_pending[g_pending_tail].event = *event;
    g_pending[g_pending_tail].event.timestamp = g_tick_counter++;
    g_pending[g_pending_tail].pending = 1;
    g_pending_tail = next_tail;
}

void nxevent_publish_simple(nx_event_type_t type, uint32_t source_id) {
    nx_event_t event = {
        .type = type,
        .timestamp = 0,
        .source_id = source_id,
        .data_size = 0,
        .data = NULL
    };
    nxevent_publish(&event);
}

void nxevent_publish_theme(const char *theme, uint32_t accent_color) {
    if (!g_initialized) return;
    
    int next_tail = (g_pending_tail + 1) % NX_MAX_PENDING_EVENTS;
    if (next_tail == g_pending_head) return;  /* Queue full */
    
    nx_pending_event_t *pe = &g_pending[g_pending_tail];
    nxe_strcpy(pe->data.theme.theme_name, theme, 32);
    pe->data.theme.accent_color = accent_color;
    
    pe->event.type = NX_EVENT_THEME_CHANGED;
    pe->event.timestamp = g_tick_counter++;
    pe->event.source_id = NX_COMPONENT_SETTINGS;
    pe->event.data_size = sizeof(nx_theme_data_t);
    pe->event.data = &pe->data.theme;
    pe->pending = 1;
    
    g_pending_tail = next_tail;
}

void nxevent_publish_dock_resize(int icon_size, int dock_height, const char *position) {
    if (!g_initialized) return;
    
    int next_tail = (g_pending_tail + 1) % NX_MAX_PENDING_EVENTS;
    if (next_tail == g_pending_head) return;
    
    nx_pending_event_t *pe = &g_pending[g_pending_tail];
    pe->data.dock.icon_size = icon_size;
    pe->data.dock.dock_height = dock_height;
    nxe_strcpy(pe->data.dock.position, position, 16);
    
    pe->event.type = NX_EVENT_DOCK_RESIZE;
    pe->event.timestamp = g_tick_counter++;
    pe->event.source_id = NX_COMPONENT_DOCK;
    pe->event.data_size = sizeof(nx_dock_data_t);
    pe->event.data = &pe->data.dock;
    pe->pending = 1;
    
    g_pending_tail = next_tail;
}

void nxevent_publish_window(nx_event_type_t type, uint32_t window_id,
                            int x, int y, int w, int h, const char *title) {
    if (!g_initialized) return;
    
    int next_tail = (g_pending_tail + 1) % NX_MAX_PENDING_EVENTS;
    if (next_tail == g_pending_head) return;
    
    nx_pending_event_t *pe = &g_pending[g_pending_tail];
    pe->data.window.window_id = window_id;
    pe->data.window.x = x;
    pe->data.window.y = y;
    pe->data.window.width = w;
    pe->data.window.height = h;
    nxe_strcpy(pe->data.window.title, title, 64);
    
    pe->event.type = type;
    pe->event.timestamp = g_tick_counter++;
    pe->event.source_id = NX_COMPONENT_WINDOW_MGR;
    pe->event.data_size = sizeof(nx_window_data_t);
    pe->event.data = &pe->data.window;
    pe->pending = 1;
    
    g_pending_tail = next_tail;
}

void nxevent_publish_app(nx_event_type_t type, uint32_t app_id,
                         const char *name, const char *path) {
    if (!g_initialized) return;
    
    int next_tail = (g_pending_tail + 1) % NX_MAX_PENDING_EVENTS;
    if (next_tail == g_pending_head) return;
    
    nx_pending_event_t *pe = &g_pending[g_pending_tail];
    pe->data.app.app_id = app_id;
    nxe_strcpy(pe->data.app.app_name, name, 64);
    nxe_strcpy(pe->data.app.app_path, path, 256);
    
    pe->event.type = type;
    pe->event.timestamp = g_tick_counter++;
    pe->event.source_id = NX_COMPONENT_APP_DRAWER;
    pe->event.data_size = sizeof(nx_app_data_t);
    pe->event.data = &pe->data.app;
    pe->pending = 1;
    
    g_pending_tail = next_tail;
}

/* ============ Processing ============ */

void nxevent_process(void) {
    if (!g_initialized) return;
    
    /* Process all pending events */
    while (g_pending_head != g_pending_tail) {
        nx_pending_event_t *pe = &g_pending[g_pending_head];
        if (pe->pending) {
            dispatch_event(&pe->event);
            pe->pending = 0;
        }
        g_pending_head = (g_pending_head + 1) % NX_MAX_PENDING_EVENTS;
    }
}
