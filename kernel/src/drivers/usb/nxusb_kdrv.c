/*
 * NXUSB Kernel Driver (nxusb.kdrv)
 * 
 * USB Host Controller Kernel Driver Implementation
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "drivers/nxusb_kdrv.h"
#include "../src/drivers/usb/usb.h"
#include <stdint.h>

/* External kernel interfaces */
extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ Constants ============ */

#define MAX_USB_DEVICES     32

/* ============ Driver State ============ */

static struct {
    int                     initialized;
    int                     controller_count;
    nxusb_controller_info_t controllers[4];
    int                     device_count;
    nxusb_device_info_t     devices[MAX_USB_DEVICES];
} g_nxusb = {0};

/* ============ Helpers ============ */

static inline void nx_strcpy(char *d, const char *s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static void serial_dec(int val) {
    char buf[12];
    int i = 0;
    if (val < 0) { serial_putc('-'); val = -val; }
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) serial_putc(buf[--i]);
}

static void serial_hex16(uint16_t val) {
    const char hex[] = "0123456789ABCDEF";
    serial_putc(hex[(val >> 12) & 0xF]);
    serial_putc(hex[(val >> 8) & 0xF]);
    serial_putc(hex[(val >> 4) & 0xF]);
    serial_putc(hex[val & 0xF]);
}

/* ============ API Implementation ============ */

int nxusb_kdrv_init(void) {
    if (g_nxusb.initialized) {
        return 0;
    }
    
    serial_puts("[NXUSB] Initializing kernel driver v" NXUSB_KDRV_VERSION "\n");
    
    /* Initialize underlying USB subsystem */
    int result = usb_init();
    if (result < 0) {
        serial_puts("[NXUSB] USB init failed\n");
        return -1;
    }
    
    /* Get controller info */
    g_nxusb.controller_count = usb_get_controller_count();
    
    for (int i = 0; i < g_nxusb.controller_count && i < 4; i++) {
        xhci_controller_t *ctrl = usb_get_controller(i);
        if (ctrl) {
            nxusb_controller_info_t *info = &g_nxusb.controllers[i];
            info->id = i;
            info->type = NXUSB_CTRL_XHCI;
            nx_strcpy(info->name, "XHCI Controller", 32);
            info->ports = ctrl->max_ports;
            info->max_speed = NXUSB_SPEED_SUPER;
            info->initialized = 1;  /* Assume running if we got it */
        }
    }
    
    /* Enumerate devices */
    nxusb_kdrv_rescan();
    
    g_nxusb.initialized = 1;
    
    serial_puts("[NXUSB] Found ");
    serial_dec(g_nxusb.controller_count);
    serial_puts(" controller(s), ");
    serial_dec(g_nxusb.device_count);
    serial_puts(" device(s)\n");
    
    return g_nxusb.controller_count;
}

void nxusb_kdrv_shutdown(void) {
    if (!g_nxusb.initialized) return;
    
    serial_puts("[NXUSB] Shutting down...\n");
    
    g_nxusb.initialized = 0;
    g_nxusb.controller_count = 0;
    g_nxusb.device_count = 0;
}

int nxusb_kdrv_controller_count(void) {
    return g_nxusb.controller_count;
}

int nxusb_kdrv_controller_info(int index, nxusb_controller_info_t *info) {
    if (index < 0 || index >= g_nxusb.controller_count) return -1;
    if (!info) return -2;
    
    *info = g_nxusb.controllers[index];
    return 0;
}

int nxusb_kdrv_device_count(void) {
    return g_nxusb.device_count;
}

int nxusb_kdrv_device_info(int index, nxusb_device_info_t *info) {
    if (index < 0 || index >= g_nxusb.device_count) return -1;
    if (!info) return -2;
    
    *info = g_nxusb.devices[index];
    return 0;
}

int nxusb_kdrv_rescan(void) {
    g_nxusb.device_count = 0;
    
    /* Iterate controllers and enumerate */
    for (int c = 0; c < g_nxusb.controller_count && c < 4; c++) {
        xhci_controller_t *ctrl = usb_get_controller(c);
        if (!ctrl) continue;
        
        usb_enumerate_devices(ctrl);
        
        /* Get devices from this controller */
        for (int addr = 1; addr < 128 && g_nxusb.device_count < MAX_USB_DEVICES; addr++) {
            usb_device_t *dev = usb_get_device(ctrl, addr);
            if (dev && dev->address > 0) {
                nxusb_device_info_t *info = &g_nxusb.devices[g_nxusb.device_count];
                info->id = g_nxusb.device_count;
                info->address = dev->address;
                info->vendor_id = dev->vendor_id;
                info->product_id = dev->product_id;
                info->device_class = 0;  /* Class not available directly */
                info->subclass = 0;  /* Not available */
                info->protocol = 0;  /* Not available */
                info->controller_id = c;
                info->port = dev->port;
                info->connected = 1;
                
                /* Speed mapping */
                switch (dev->speed) {
                    case 0: info->speed = NXUSB_SPEED_LOW; break;
                    case 1: info->speed = NXUSB_SPEED_FULL; break;
                    case 2: info->speed = NXUSB_SPEED_HIGH; break;
                    case 3: info->speed = NXUSB_SPEED_SUPER; break;
                    default: info->speed = NXUSB_SPEED_FULL; break;
                }
                
                g_nxusb.device_count++;
            }
        }
    }
    
    return g_nxusb.device_count;
}

int nxusb_kdrv_reset_device(int device_id) {
    if (device_id < 0 || device_id >= g_nxusb.device_count) return -1;
    
    /* TODO: Implement device reset */
    serial_puts("[NXUSB] Reset device ");
    serial_dec(device_id);
    serial_puts("\n");
    
    return 0;
}

const char *nxusb_kdrv_class_name(nxusb_class_t class_code) {
    switch (class_code) {
        case NXUSB_CLASS_AUDIO:        return "Audio";
        case NXUSB_CLASS_CDC:          return "CDC";
        case NXUSB_CLASS_HID:          return "HID";
        case NXUSB_CLASS_PHYSICAL:     return "Physical";
        case NXUSB_CLASS_IMAGE:        return "Image";
        case NXUSB_CLASS_PRINTER:      return "Printer";
        case NXUSB_CLASS_MASS_STORAGE: return "Mass Storage";
        case NXUSB_CLASS_HUB:          return "Hub";
        case NXUSB_CLASS_CDC_DATA:     return "CDC-Data";
        case NXUSB_CLASS_VIDEO:        return "Video";
        case NXUSB_CLASS_WIRELESS:     return "Wireless";
        case NXUSB_CLASS_MISC:         return "Misc";
        case NXUSB_CLASS_VENDOR:       return "Vendor";
        default:                       return "Unknown";
    }
}

const char *nxusb_kdrv_speed_name(nxusb_speed_t speed) {
    switch (speed) {
        case NXUSB_SPEED_LOW:        return "Low (1.5 Mbps)";
        case NXUSB_SPEED_FULL:       return "Full (12 Mbps)";
        case NXUSB_SPEED_HIGH:       return "High (480 Mbps)";
        case NXUSB_SPEED_SUPER:      return "Super (5 Gbps)";
        case NXUSB_SPEED_SUPER_PLUS: return "Super+ (10 Gbps)";
        default:                     return "Unknown";
    }
}

void nxusb_kdrv_debug(void) {
    serial_puts("\n=== NXUSB Debug Info ===\n");
    serial_puts("Version: " NXUSB_KDRV_VERSION "\n");
    serial_puts("Controllers: ");
    serial_dec(g_nxusb.controller_count);
    serial_puts("\n");
    
    for (int i = 0; i < g_nxusb.controller_count; i++) {
        nxusb_controller_info_t *c = &g_nxusb.controllers[i];
        serial_puts("  [");
        serial_dec(i);
        serial_puts("] ");
        serial_puts(c->name);
        serial_puts(", ");
        serial_dec(c->ports);
        serial_puts(" ports\n");
    }
    
    serial_puts("Devices: ");
    serial_dec(g_nxusb.device_count);
    serial_puts("\n");
    
    for (int i = 0; i < g_nxusb.device_count; i++) {
        nxusb_device_info_t *d = &g_nxusb.devices[i];
        serial_puts("  [");
        serial_dec(i);
        serial_puts("] ");
        serial_hex16(d->vendor_id);
        serial_puts(":");
        serial_hex16(d->product_id);
        serial_puts(" - ");
        serial_puts(nxusb_kdrv_class_name(d->device_class));
        serial_puts(" (");
        serial_puts(nxusb_kdrv_speed_name(d->speed));
        serial_puts(")\n");
    }
    
    serial_puts("========================\n\n");
}

/* ============ Gamepad Support ============ */

/* Gamepad state - up to 4 gamepads */
#define MAX_GAMEPADS 4

typedef struct {
    int connected;
    int device_id;
    uint16_t vendor_id;
    uint16_t product_id;
    
    /* Axes (normalized -32768 to 32767) */
    int16_t axis_left_x;
    int16_t axis_left_y;
    int16_t axis_right_x;
    int16_t axis_right_y;
    int16_t trigger_left;
    int16_t trigger_right;
    
    /* Buttons (bitfield) */
    uint32_t buttons;
} nxusb_gamepad_state_t;

#define GAMEPAD_BTN_A       0x0001
#define GAMEPAD_BTN_B       0x0002
#define GAMEPAD_BTN_X       0x0004
#define GAMEPAD_BTN_Y       0x0008
#define GAMEPAD_BTN_LB      0x0010
#define GAMEPAD_BTN_RB      0x0020
#define GAMEPAD_BTN_START   0x0040
#define GAMEPAD_BTN_SELECT  0x0080
#define GAMEPAD_BTN_L3      0x0100
#define GAMEPAD_BTN_R3      0x0200
#define GAMEPAD_BTN_DPAD_UP    0x0400
#define GAMEPAD_BTN_DPAD_DOWN  0x0800
#define GAMEPAD_BTN_DPAD_LEFT  0x1000
#define GAMEPAD_BTN_DPAD_RIGHT 0x2000
#define GAMEPAD_BTN_HOME    0x4000

static nxusb_gamepad_state_t g_gamepads[MAX_GAMEPADS] = {0};
static int g_gamepad_count = 0;

int nxusb_gamepad_detect(void) {
    g_gamepad_count = 0;
    
    /* Scan USB devices for HID gamepads */
    for (int i = 0; i < g_nxusb.device_count && g_gamepad_count < MAX_GAMEPADS; i++) {
        nxusb_device_info_t *dev = &g_nxusb.devices[i];
        
        /* Check for HID class (gamepads are HID devices) */
        if (dev->device_class == NXUSB_CLASS_HID) {
            /* Known gamepad vendor/product IDs */
            int is_gamepad = 0;
            
            /* Xbox controllers */
            if (dev->vendor_id == 0x045E) is_gamepad = 1;
            /* Sony PlayStation */
            if (dev->vendor_id == 0x054C) is_gamepad = 1;
            /* Nintendo */
            if (dev->vendor_id == 0x057E) is_gamepad = 1;
            /* Generic gamepads (Logitech, etc) */
            if (dev->vendor_id == 0x046D) is_gamepad = 1;
            
            if (is_gamepad) {
                nxusb_gamepad_state_t *pad = &g_gamepads[g_gamepad_count];
                pad->connected = 1;
                pad->device_id = dev->id;
                pad->vendor_id = dev->vendor_id;
                pad->product_id = dev->product_id;
                pad->buttons = 0;
                pad->axis_left_x = 0;
                pad->axis_left_y = 0;
                pad->axis_right_x = 0;
                pad->axis_right_y = 0;
                pad->trigger_left = 0;
                pad->trigger_right = 0;
                
                g_gamepad_count++;
                
                serial_puts("[NXUSB] Gamepad detected: ");
                serial_hex16(dev->vendor_id);
                serial_puts(":");
                serial_hex16(dev->product_id);
                serial_puts("\n");
            }
        }
    }
    
    return g_gamepad_count;
}

int nxusb_gamepad_count(void) {
    return g_gamepad_count;
}

int nxusb_gamepad_poll(int index, nxusb_gamepad_state_t *state) {
    if (index < 0 || index >= g_gamepad_count) return -1;
    if (!state) return -2;
    
    /* TODO: Actually poll HID device for current state */
    /* For now, return cached state */
    *state = g_gamepads[index];
    
    return 0;
}

int nxusb_gamepad_rumble(int index, int left_motor, int right_motor) {
    if (index < 0 || index >= g_gamepad_count) return -1;
    
    (void)left_motor;
    (void)right_motor;
    
    /* TODO: Send HID output report for rumble */
    serial_puts("[NXUSB] Rumble: ");
    serial_dec(index);
    serial_puts("\n");
    
    return 0;
}
