/*
 * NeolyxOS Event Bus System
 * 
 * Publish/subscribe event system for live settings updates.
 * Components subscribe to events, Settings.app publishes changes.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXEVENT_H
#define NXEVENT_H

#include <stdint.h>

/* ============ Event Types ============ */

typedef enum {
    /* Desktop events */
    NX_EVENT_THEME_CHANGED      = 0x0100,
    NX_EVENT_ACCENT_CHANGED     = 0x0101,
    NX_EVENT_WALLPAPER_CHANGED  = 0x0102,
    
    /* Dock events */
    NX_EVENT_DOCK_RESIZE        = 0x0200,
    NX_EVENT_DOCK_POSITION      = 0x0201,
    NX_EVENT_DOCK_AUTOHIDE      = 0x0202,
    NX_EVENT_DOCK_ICON_ADDED    = 0x0203,
    NX_EVENT_DOCK_ICON_REMOVED  = 0x0204,
    
    /* Window events */
    NX_EVENT_WINDOW_CREATED     = 0x0300,
    NX_EVENT_WINDOW_DESTROYED   = 0x0301,
    NX_EVENT_WINDOW_FOCUSED     = 0x0302,
    NX_EVENT_WINDOW_MINIMIZED   = 0x0303,
    NX_EVENT_WINDOW_MAXIMIZED   = 0x0304,
    NX_EVENT_WINDOW_MOVED       = 0x0305,
    NX_EVENT_WINDOW_RESIZED     = 0x0306,
    
    /* App events */
    NX_EVENT_APP_LAUNCHED       = 0x0400,
    NX_EVENT_APP_TERMINATED     = 0x0401,
    NX_EVENT_APP_FOCUSED        = 0x0402,
    
    /* Menu events */
    NX_EVENT_MENU_OPENED        = 0x0500,
    NX_EVENT_MENU_CLOSED        = 0x0501,
    NX_EVENT_MENU_ITEM_CLICKED  = 0x0502,
    
    /* Settings events */
    NX_EVENT_SETTINGS_CHANGED   = 0x0600,
    NX_EVENT_ANIMATION_TOGGLE   = 0x0601,
    NX_EVENT_TRANSPARENCY_TOGGLE = 0x0602,
    
    /* System events */
    NX_EVENT_DISPLAY_CHANGED    = 0x0700,
    NX_EVENT_AUDIO_CHANGED      = 0x0701,
    NX_EVENT_NETWORK_CHANGED    = 0x0702,
    
    NX_EVENT_MAX                = 0x0FFF
} nx_event_type_t;

/* ============ Event Data ============ */

/* Generic event data */
typedef struct {
    nx_event_type_t type;
    uint32_t timestamp;
    uint32_t source_id;     /* Component that published */
    uint32_t data_size;     /* Size of extra data */
    void *data;             /* Event-specific data (optional) */
} nx_event_t;

/* Theme change event data */
typedef struct {
    char theme_name[32];    /* "dark" or "light" */
    uint32_t accent_color;  /* ARGB */
} nx_theme_data_t;

/* Dock resize event data */
typedef struct {
    int icon_size;          /* New icon size */
    int dock_height;        /* New dock height */
    char position[16];      /* "bottom", "left", "right" */
} nx_dock_data_t;

/* Window event data */
typedef struct {
    uint32_t window_id;
    int x, y, width, height;
    char title[64];
} nx_window_data_t;

/* App event data */
typedef struct {
    uint32_t app_id;
    char app_name[64];
    char app_path[256];
} nx_app_data_t;

/* ============ Callback Types ============ */

/* Event handler callback */
typedef void (*nx_event_handler_t)(const nx_event_t *event, void *userdata);

/* ============ API Functions ============ */

/**
 * Initialize event bus
 * Call once at desktop startup
 */
void nxevent_init(void);

/**
 * Shutdown event bus
 */
void nxevent_shutdown(void);

/**
 * Subscribe to event type
 * @param type Event type to subscribe to
 * @param handler Callback function
 * @param userdata User data passed to handler
 * @return Subscription ID (for unsubscribe)
 */
int nxevent_subscribe(nx_event_type_t type, nx_event_handler_t handler, void *userdata);

/**
 * Subscribe to range of events (e.g., all dock events)
 * @param type_min Start of event range
 * @param type_max End of event range
 * @param handler Callback function
 * @param userdata User data passed to handler
 * @return Subscription ID
 */
int nxevent_subscribe_range(nx_event_type_t type_min, nx_event_type_t type_max,
                            nx_event_handler_t handler, void *userdata);

/**
 * Unsubscribe from events
 * @param subscription_id ID returned from subscribe
 */
void nxevent_unsubscribe(int subscription_id);

/**
 * Publish event to all subscribers
 * @param event Event to publish
 */
void nxevent_publish(const nx_event_t *event);

/**
 * Publish simple event (no extra data)
 * @param type Event type
 * @param source_id Source component ID
 */
void nxevent_publish_simple(nx_event_type_t type, uint32_t source_id);

/**
 * Publish theme change event
 */
void nxevent_publish_theme(const char *theme, uint32_t accent_color);

/**
 * Publish dock resize event
 */
void nxevent_publish_dock_resize(int icon_size, int dock_height, const char *position);

/**
 * Publish window event
 */
void nxevent_publish_window(nx_event_type_t type, uint32_t window_id,
                            int x, int y, int w, int h, const char *title);

/**
 * Publish app event
 */
void nxevent_publish_app(nx_event_type_t type, uint32_t app_id,
                         const char *name, const char *path);

/**
 * Process pending events (call in main loop)
 */
void nxevent_process(void);

/* ============ Component IDs ============ */

#define NX_COMPONENT_DESKTOP    0x0001
#define NX_COMPONENT_DOCK       0x0002
#define NX_COMPONENT_MENUBAR    0x0003
#define NX_COMPONENT_WINDOW_MGR 0x0004
#define NX_COMPONENT_SETTINGS   0x0005
#define NX_COMPONENT_COMPOSITOR 0x0006
#define NX_COMPONENT_APP_DRAWER 0x0007

#endif /* NXEVENT_H */
