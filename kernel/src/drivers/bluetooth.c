/*
 * NeolyxOS Bluetooth HCI Driver
 * 
 * Supports USB Bluetooth dongles and integrated Bluetooth.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "drivers/bluetooth.h"
#include "drivers/usb.h"
#include "mm/kheap.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ State ============ */

static bt_controller_t *bt_controllers = NULL;
static int bt_ctrl_count = 0;
static bt_device_t *paired_devices = NULL;

/* ============ Helpers ============ */

static void bt_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void bt_memset(void *s, int c, uint32_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
}

static void serial_mac(bt_addr_t *addr) {
    const char *hex = "0123456789ABCDEF";
    for (int i = 5; i >= 0; i--) {
        if (i < 5) serial_putc(':');
        serial_putc(hex[(addr->bytes[i] >> 4) & 0xF]);
        serial_putc(hex[addr->bytes[i] & 0xF]);
    }
}

/* ============ HCI Command ============ */

typedef struct {
    uint8_t type;           /* 0x01 = command */
    uint16_t opcode;
    uint8_t length;
    uint8_t params[];
} __attribute__((packed)) hci_command_t;

static int bt_send_hci_command(bt_controller_t *ctrl, uint16_t opcode,
                               const uint8_t *params, uint8_t param_len) {
    /* TODO: Send via USB bulk endpoint */
    serial_puts("[BT] HCI cmd: ");
    const char *hex = "0123456789ABCDEF";
    serial_putc(hex[(opcode >> 12) & 0xF]);
    serial_putc(hex[(opcode >> 8) & 0xF]);
    serial_putc(hex[(opcode >> 4) & 0xF]);
    serial_putc(hex[opcode & 0xF]);
    serial_puts("\n");
    
    (void)ctrl;
    (void)params;
    (void)param_len;
    
    return 0;
}

/* ============ Bluetooth Init ============ */

static int bt_init_controller(bt_controller_t *ctrl) {
    serial_puts("[BT] Initializing controller...\n");
    
    /* HCI Reset */
    bt_send_hci_command(ctrl, HCI_OP_RESET, NULL, 0);
    
    /* Wait for reset complete */
    for (volatile int i = 0; i < 100000; i++);
    
    /* Read local BD address */
    bt_send_hci_command(ctrl, HCI_OP_READ_BD_ADDR, NULL, 0);
    
    /* For now, use a simulated address */
    ctrl->address.bytes[5] = 0x00;
    ctrl->address.bytes[4] = 0x1A;
    ctrl->address.bytes[3] = 0x7D;
    ctrl->address.bytes[2] = 0xDA;
    ctrl->address.bytes[1] = 0x71;
    ctrl->address.bytes[0] = 0x00;
    
    serial_puts("[BT] Local address: ");
    serial_mac(&ctrl->address);
    serial_puts("\n");
    
    ctrl->powered = 1;
    ctrl->max_connections = 7;
    
    serial_puts("[BT] Controller ready\n");
    return 0;
}

/* ============ Bluetooth API ============ */

int bluetooth_init(void) {
    serial_puts("[BT] Initializing Bluetooth subsystem...\n");
    
    bt_controllers = NULL;
    bt_ctrl_count = 0;
    
    /* Look for USB Bluetooth devices */
    /* USB Class 0xE0 (Wireless), Subclass 0x01 (RF), Protocol 0x01 (Bluetooth) */
    
    /* For now, create a simulated controller if USB is present */
    usb_controller_t *usb = usb_get_controller(0);
    if (usb) {
        bt_controller_t *ctrl = (bt_controller_t *)kzalloc(sizeof(bt_controller_t));
        if (ctrl) {
            bt_strcpy(ctrl->name, "NeolyxOS Bluetooth", 64);
            ctrl->version = BT_VERSION_5_0;
            ctrl->powered = 0;
            ctrl->devices = NULL;
            ctrl->device_count = 0;
            
            if (bt_init_controller(ctrl) == 0) {
                ctrl->next = bt_controllers;
                bt_controllers = ctrl;
                bt_ctrl_count++;
            }
        }
    }
    
    if (bt_ctrl_count == 0) {
        serial_puts("[BT] No Bluetooth controllers found\n");
        return -1;
    }
    
    serial_puts("[BT] Ready (");
    serial_putc('0' + bt_ctrl_count);
    serial_puts(" controllers)\n");
    
    return 0;
}

bt_controller_t *bluetooth_get_controller(int index) {
    bt_controller_t *ctrl = bt_controllers;
    int i = 0;
    while (ctrl && i < index) {
        ctrl = ctrl->next;
        i++;
    }
    return ctrl;
}

int bluetooth_controller_count(void) {
    return bt_ctrl_count;
}

/* ============ Power ============ */

int bluetooth_set_power(bt_controller_t *ctrl, int on) {
    if (!ctrl) return -1;
    
    ctrl->powered = on ? 1 : 0;
    serial_puts("[BT] Power ");
    serial_puts(on ? "ON\n" : "OFF\n");
    
    return 0;
}

int bluetooth_is_powered(bt_controller_t *ctrl) {
    return ctrl ? ctrl->powered : 0;
}

/* ============ Discovery ============ */

int bluetooth_start_scan(bt_controller_t *ctrl) {
    if (!ctrl || !ctrl->powered) return -1;
    
    serial_puts("[BT] Starting scan...\n");
    ctrl->scanning = 1;
    
    /* HCI Inquiry */
    uint8_t params[5] = {0x33, 0x8B, 0x9E, 0x08, 0x00};  /* LAP, duration, responses */
    bt_send_hci_command(ctrl, HCI_OP_INQUIRY, params, 5);
    
    /* Simulate some discovered devices */
    bt_device_t *dev1 = (bt_device_t *)kzalloc(sizeof(bt_device_t));
    if (dev1) {
        dev1->address.bytes[5] = 0xAA;
        dev1->address.bytes[4] = 0xBB;
        dev1->address.bytes[3] = 0xCC;
        dev1->address.bytes[2] = 0xDD;
        dev1->address.bytes[1] = 0xEE;
        dev1->address.bytes[0] = 0x01;
        bt_strcpy(dev1->name, "Wireless Mouse", 64);
        dev1->type = BT_TYPE_MOUSE;
        dev1->rssi = -55;
        dev1->supports_hid = 1;
        
        dev1->next = ctrl->devices;
        ctrl->devices = dev1;
        ctrl->device_count++;
    }
    
    bt_device_t *dev2 = (bt_device_t *)kzalloc(sizeof(bt_device_t));
    if (dev2) {
        dev2->address.bytes[5] = 0x11;
        dev2->address.bytes[4] = 0x22;
        dev2->address.bytes[3] = 0x33;
        dev2->address.bytes[2] = 0x44;
        dev2->address.bytes[1] = 0x55;
        dev2->address.bytes[0] = 0x66;
        bt_strcpy(dev2->name, "BT Headphones", 64);
        dev2->type = BT_TYPE_HEADPHONES;
        dev2->rssi = -45;
        dev2->supports_audio = 1;
        dev2->has_battery = 1;
        dev2->battery_level = 85;
        
        dev2->next = ctrl->devices;
        ctrl->devices = dev2;
        ctrl->device_count++;
    }
    
    serial_puts("[BT] Found ");
    serial_putc('0' + ctrl->device_count);
    serial_puts(" devices\n");
    
    ctrl->scanning = 0;
    return 0;
}

int bluetooth_stop_scan(bt_controller_t *ctrl) {
    if (!ctrl) return -1;
    
    ctrl->scanning = 0;
    bt_send_hci_command(ctrl, HCI_OP_INQUIRY_CANCEL, NULL, 0);
    
    return 0;
}

int bluetooth_get_devices(bt_controller_t *ctrl, bt_device_t *devices, int max) {
    if (!ctrl || !devices) return 0;
    
    int count = 0;
    bt_device_t *dev = ctrl->devices;
    while (dev && count < max) {
        devices[count] = *dev;
        dev = dev->next;
        count++;
    }
    
    return count;
}

int bluetooth_set_discoverable(bt_controller_t *ctrl, int discoverable) {
    if (!ctrl) return -1;
    
    ctrl->discoverable = discoverable ? 1 : 0;
    uint8_t scan = discoverable ? 0x03 : 0x02;  /* Inquiry + Page / Page only */
    bt_send_hci_command(ctrl, HCI_OP_WRITE_SCAN_ENABLE, &scan, 1);
    
    return 0;
}

/* ============ Pairing ============ */

int bluetooth_pair(bt_controller_t *ctrl, bt_addr_t *addr, const char *pin) {
    if (!ctrl || !addr) return -1;
    
    serial_puts("[BT] Pairing with ");
    serial_mac(addr);
    serial_puts("...\n");
    
    /* Find device */
    bt_device_t *dev = ctrl->devices;
    while (dev) {
        int match = 1;
        for (int i = 0; i < 6; i++) {
            if (dev->address.bytes[i] != addr->bytes[i]) {
                match = 0;
                break;
            }
        }
        
        if (match) {
            dev->paired = 1;
            dev->trusted = 1;
            serial_puts("[BT] Paired with ");
            serial_puts(dev->name);
            serial_puts("\n");
            return 0;
        }
        
        dev = dev->next;
    }
    
    (void)pin;
    return -1;
}

int bluetooth_unpair(bt_controller_t *ctrl, bt_addr_t *addr) {
    if (!ctrl || !addr) return -1;
    
    bt_device_t *dev = ctrl->devices;
    while (dev) {
        int match = 1;
        for (int i = 0; i < 6; i++) {
            if (dev->address.bytes[i] != addr->bytes[i]) {
                match = 0;
                break;
            }
        }
        
        if (match) {
            dev->paired = 0;
            dev->trusted = 0;
            bt_memset(dev->link_key, 0, 16);
            return 0;
        }
        
        dev = dev->next;
    }
    
    return -1;
}

/* ============ Connection ============ */

int bluetooth_connect(bt_controller_t *ctrl, bt_addr_t *addr) {
    if (!ctrl || !addr) return -1;
    
    serial_puts("[BT] Connecting to ");
    serial_mac(addr);
    serial_puts("...\n");
    
    /* Find device */
    bt_device_t *dev = ctrl->devices;
    while (dev) {
        int match = 1;
        for (int i = 0; i < 6; i++) {
            if (dev->address.bytes[i] != addr->bytes[i]) {
                match = 0;
                break;
            }
        }
        
        if (match) {
            dev->state = BT_STATE_CONNECTED;
            serial_puts("[BT] Connected to ");
            serial_puts(dev->name);
            serial_puts("\n");
            return 0;
        }
        
        dev = dev->next;
    }
    
    return -1;
}

int bluetooth_disconnect(bt_controller_t *ctrl, bt_addr_t *addr) {
    if (!ctrl || !addr) return -1;
    
    bt_device_t *dev = ctrl->devices;
    while (dev) {
        int match = 1;
        for (int i = 0; i < 6; i++) {
            if (dev->address.bytes[i] != addr->bytes[i]) {
                match = 0;
                break;
            }
        }
        
        if (match) {
            dev->state = BT_STATE_DISCONNECTED;
            serial_puts("[BT] Disconnected\n");
            return 0;
        }
        
        dev = dev->next;
    }
    
    return -1;
}

bt_state_t bluetooth_get_state(bt_controller_t *ctrl, bt_addr_t *addr) {
    if (!ctrl || !addr) return BT_STATE_DISCONNECTED;
    
    bt_device_t *dev = ctrl->devices;
    while (dev) {
        int match = 1;
        for (int i = 0; i < 6; i++) {
            if (dev->address.bytes[i] != addr->bytes[i]) {
                match = 0;
                break;
            }
        }
        
        if (match) return dev->state;
        dev = dev->next;
    }
    
    return BT_STATE_DISCONNECTED;
}

/* ============ Audio ============ */

int bluetooth_audio_connect(bt_controller_t *ctrl, bt_addr_t *addr) {
    if (!ctrl || !addr) return -1;
    
    serial_puts("[BT] A2DP connecting...\n");
    /* TODO: A2DP connection */
    return 0;
}

int bluetooth_audio_disconnect(bt_controller_t *ctrl) {
    if (!ctrl) return -1;
    serial_puts("[BT] A2DP disconnected\n");
    return 0;
}

/* ============ HID ============ */

static bt_hid_callback_t hid_callback = NULL;

int bluetooth_register_hid(bt_controller_t *ctrl, bt_hid_callback_t callback) {
    (void)ctrl;
    hid_callback = callback;
    return 0;
}
