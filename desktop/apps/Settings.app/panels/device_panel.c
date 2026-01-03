/*
 * Settings - Device Manager Panel
 * Manages connected hardware devices
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "panel_common.h"
#include <string.h>

/* ============ Device Types ============ */

typedef enum {
    DEVICE_TYPE_DISPLAY,
    DEVICE_TYPE_AUDIO,
    DEVICE_TYPE_NETWORK,
    DEVICE_TYPE_STORAGE,
    DEVICE_TYPE_USB,
    DEVICE_TYPE_BLUETOOTH,
    DEVICE_TYPE_INPUT,
    DEVICE_TYPE_GRAPHICS,
    DEVICE_TYPE_OTHER
} device_type_t;

typedef enum {
    DEVICE_STATUS_CONNECTED,
    DEVICE_STATUS_DISCONNECTED,
    DEVICE_STATUS_ERROR,
    DEVICE_STATUS_DISABLED
} device_status_t;

typedef struct {
    char name[64];
    char vendor[32];
    char model[64];
    char driver[32];
    char driver_version[16];
    device_type_t type;
    device_status_t status;
    uint64_t id;
} device_info_t;

#define MAX_DEVICES 64
static device_info_t devices[MAX_DEVICES];
static int device_count = 0;
static int selected_device = -1;

/* ============ Forward Declarations ============ */

extern int nxdevice_enumerate(device_info_t *out, int max);
extern int nxdevice_enable(uint64_t device_id);
extern int nxdevice_disable(uint64_t device_id);
extern int nxdevice_update_driver(uint64_t device_id);
extern int nxdevice_get_properties(uint64_t device_id, char *out, int max_len);

/* ============ Helper Functions ============ */

static const char *device_type_name(device_type_t type) {
    switch (type) {
        case DEVICE_TYPE_DISPLAY: return "Display";
        case DEVICE_TYPE_AUDIO: return "Audio";
        case DEVICE_TYPE_NETWORK: return "Network";
        case DEVICE_TYPE_STORAGE: return "Storage";
        case DEVICE_TYPE_USB: return "USB";
        case DEVICE_TYPE_BLUETOOTH: return "Bluetooth";
        case DEVICE_TYPE_INPUT: return "Input";
        case DEVICE_TYPE_GRAPHICS: return "Graphics";
        default: return "Other";
    }
}

static const char *device_type_icon(device_type_t type) {
    switch (type) {
        case DEVICE_TYPE_DISPLAY: return "display";
        case DEVICE_TYPE_AUDIO: return "speaker";
        case DEVICE_TYPE_NETWORK: return "network";
        case DEVICE_TYPE_STORAGE: return "storage";
        case DEVICE_TYPE_USB: return "usb";
        case DEVICE_TYPE_BLUETOOTH: return "bluetooth";
        case DEVICE_TYPE_INPUT: return "keyboard";
        case DEVICE_TYPE_GRAPHICS: return "gpu";
        default: return "device";
    }
}

static uint32_t device_status_color(device_status_t status) {
    switch (status) {
        case DEVICE_STATUS_CONNECTED: return 0x34C759;
        case DEVICE_STATUS_DISCONNECTED: return 0x8E8E93;
        case DEVICE_STATUS_ERROR: return 0xFF3B30;
        case DEVICE_STATUS_DISABLED: return 0xFF9500;
        default: return 0x8E8E93;
    }
}

/* ============ Panel Callbacks ============ */

void device_panel_init(settings_panel_t *panel) {
    panel->title = "Devices";
    panel->icon = "devices";
    
    device_count = nxdevice_enumerate(devices, MAX_DEVICES);
    selected_device = device_count > 0 ? 0 : -1;
}

void device_panel_render(settings_panel_t *panel) {
    int y = panel->y + 20;
    int x = panel->x + 20;
    
    /* Header */
    panel_draw_header(x, y, "Device Manager");
    y += 40;
    
    /* Device count */
    char count_str[32];
    snprintf(count_str, sizeof(count_str), "%d devices detected", device_count);
    panel_draw_text(x, y, count_str, 0x888888);
    y += 30;
    
    /* Device list on left, details on right */
    int list_width = 280;
    int detail_x = x + list_width + 20;
    
    /* Device list */
    for (int i = 0; i < device_count && i < 12; i++) {
        device_info_t *dev = &devices[i];
        bool selected = (i == selected_device);
        
        /* Card background */
        uint32_t bg = selected ? 0x0A84FF : 0x2A2A2A;
        panel_fill_rect(x, y, list_width, 50, bg);
        
        /* Icon */
        panel_draw_icon(x + 10, y + 10, device_type_icon(dev->type), 30);
        
        /* Name */
        panel_draw_text(x + 50, y + 8, dev->name, 0xFFFFFF);
        
        /* Type badge */
        panel_draw_text(x + 50, y + 28, device_type_name(dev->type), 0x888888);
        
        /* Status indicator */
        panel_fill_circle(x + list_width - 20, y + 25, 5, device_status_color(dev->status));
        
        y += 54;
    }
    
    /* Device details */
    if (selected_device >= 0 && selected_device < device_count) {
        device_info_t *dev = &devices[selected_device];
        int dy = panel->y + 100;
        
        panel_draw_subheader(detail_x, dy, "Device Details");
        dy += 35;
        
        /* Device name */
        panel_draw_label(detail_x, dy, "Name:");
        panel_draw_text(detail_x + 100, dy, dev->name, 0xFFFFFF);
        dy += 28;
        
        /* Vendor */
        panel_draw_label(detail_x, dy, "Vendor:");
        panel_draw_text(detail_x + 100, dy, dev->vendor, 0xCCCCCC);
        dy += 28;
        
        /* Model */
        panel_draw_label(detail_x, dy, "Model:");
        panel_draw_text(detail_x + 100, dy, dev->model, 0xCCCCCC);
        dy += 28;
        
        /* Driver */
        panel_draw_label(detail_x, dy, "Driver:");
        char driver_str[64];
        snprintf(driver_str, sizeof(driver_str), "%s v%s", dev->driver, dev->driver_version);
        panel_draw_text(detail_x + 100, dy, driver_str, 0xCCCCCC);
        dy += 28;
        
        /* Status */
        panel_draw_label(detail_x, dy, "Status:");
        const char *status_text = "Connected";
        switch (dev->status) {
            case DEVICE_STATUS_DISCONNECTED: status_text = "Disconnected"; break;
            case DEVICE_STATUS_ERROR: status_text = "Error"; break;
            case DEVICE_STATUS_DISABLED: status_text = "Disabled"; break;
            default: break;
        }
        panel_draw_text(detail_x + 100, dy, status_text, device_status_color(dev->status));
        dy += 40;
        
        /* Buttons */
        if (dev->status == DEVICE_STATUS_DISABLED) {
            panel_draw_button(detail_x, dy, "Enable Device", 0x34C759);
        } else if (dev->status == DEVICE_STATUS_CONNECTED) {
            panel_draw_button(detail_x, dy, "Disable Device", 0xFF9500);
        }
        
        panel_draw_button(detail_x + 140, dy, "Update Driver", 0x007AFF);
        panel_draw_button(detail_x + 280, dy, "Properties", 0x48484A);
    }
}

void device_panel_handle_event(settings_panel_t *panel, settings_event_t *event) {
    if (event->type == EVT_CLICK) {
        /* Check device list clicks */
        int list_y = panel->y + 90;
        int x = panel->x + 20;
        
        for (int i = 0; i < device_count && i < 12; i++) {
            if (event->x >= x && event->x <= x + 280 &&
                event->y >= list_y && event->y <= list_y + 50) {
                selected_device = i;
                panel->needs_redraw = true;
                break;
            }
            list_y += 54;
        }
    }
}

void device_panel_cleanup(settings_panel_t *panel) {
    (void)panel;
}
