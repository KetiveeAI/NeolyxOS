/*
 * NeolyxOS Bluetooth Subsystem
 * 
 * Supports:
 *   - Bluetooth 4.0/4.2 (BLE)
 *   - Bluetooth 5.0/5.1/5.2
 *   - USB Bluetooth dongles
 *   - Integrated Bluetooth (Intel, Qualcomm)
 * 
 * Profiles:
 *   - HID (keyboards, mice, gamepads)
 *   - A2DP (audio streaming)
 *   - HFP (hands-free)
 *   - PAN (network)
 *   - File transfer (OBEX)
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#ifndef NEOLYX_BLUETOOTH_H
#define NEOLYX_BLUETOOTH_H

#include <stdint.h>

/* ============ Bluetooth Address ============ */

typedef struct {
    uint8_t bytes[6];   /* MAC-like address */
} bt_addr_t;

/* ============ Bluetooth Versions ============ */

typedef enum {
    BT_VERSION_4_0 = 0x06,
    BT_VERSION_4_1 = 0x07,
    BT_VERSION_4_2 = 0x08,
    BT_VERSION_5_0 = 0x09,
    BT_VERSION_5_1 = 0x0A,
    BT_VERSION_5_2 = 0x0B,
} bt_version_t;

/* ============ Device Classes ============ */

#define BT_CLASS_COMPUTER       0x01
#define BT_CLASS_PHONE          0x02
#define BT_CLASS_LAN            0x03
#define BT_CLASS_AUDIO          0x04
#define BT_CLASS_PERIPHERAL     0x05
#define BT_CLASS_IMAGING        0x06
#define BT_CLASS_WEARABLE       0x07
#define BT_CLASS_TOY            0x08
#define BT_CLASS_HEALTH         0x09

/* ============ Device Types ============ */

typedef enum {
    BT_TYPE_UNKNOWN = 0,
    BT_TYPE_KEYBOARD,
    BT_TYPE_MOUSE,
    BT_TYPE_GAMEPAD,
    BT_TYPE_HEADPHONES,
    BT_TYPE_SPEAKER,
    BT_TYPE_PHONE,
    BT_TYPE_COMPUTER,
} bt_device_type_t;

/* ============ Connection State ============ */

typedef enum {
    BT_STATE_DISCONNECTED = 0,
    BT_STATE_CONNECTING,
    BT_STATE_CONNECTED,
    BT_STATE_PAIRING,
    BT_STATE_PAIRED,
} bt_state_t;

/* ============ Bluetooth Device ============ */

typedef struct bt_device {
    bt_addr_t address;
    char name[64];
    bt_device_type_t type;
    bt_state_t state;
    
    uint32_t class_of_device;
    int8_t rssi;                /* Signal strength */
    
    /* Pairing */
    int paired;
    int trusted;
    uint8_t link_key[16];
    
    /* Profiles */
    int supports_hid;
    int supports_audio;
    int supports_file_transfer;
    
    /* Battery */
    int has_battery;
    uint8_t battery_level;      /* 0-100% */
    
    struct bt_device *next;
} bt_device_t;

/* ============ Bluetooth Controller ============ */

typedef struct bt_controller {
    char name[64];
    bt_addr_t address;
    bt_version_t version;
    
    /* State */
    int powered;
    int discoverable;
    int pairable;
    int scanning;
    
    /* Connected devices */
    bt_device_t *devices;
    int device_count;
    int max_connections;
    
    /* USB or PCI */
    void *driver_data;
    
    struct bt_controller *next;
} bt_controller_t;

/* ============ HCI Commands ============ */

#define HCI_OP_RESET                0x0C03
#define HCI_OP_SET_EVENT_MASK       0x0C01
#define HCI_OP_READ_BD_ADDR         0x1009
#define HCI_OP_READ_LOCAL_NAME      0x0C14
#define HCI_OP_WRITE_LOCAL_NAME     0x0C13
#define HCI_OP_INQUIRY              0x0401
#define HCI_OP_INQUIRY_CANCEL       0x0402
#define HCI_OP_CREATE_CONNECTION    0x0405
#define HCI_OP_DISCONNECT           0x0406
#define HCI_OP_ACCEPT_CONNECTION    0x0409
#define HCI_OP_WRITE_SCAN_ENABLE    0x0C1A
#define HCI_OP_AUTH_REQUESTED       0x0411
#define HCI_OP_READ_REMOTE_NAME     0x0419

/* ============ HCI Events ============ */

#define HCI_EV_INQUIRY_COMPLETE     0x01
#define HCI_EV_INQUIRY_RESULT       0x02
#define HCI_EV_CONNECTION_COMPLETE  0x03
#define HCI_EV_DISCONNECTION        0x05
#define HCI_EV_REMOTE_NAME          0x07
#define HCI_EV_COMMAND_COMPLETE     0x0E
#define HCI_EV_COMMAND_STATUS       0x0F

/* ============ Bluetooth API ============ */

/**
 * Initialize Bluetooth subsystem.
 */
int bluetooth_init(void);

/**
 * Get Bluetooth controller.
 */
bt_controller_t *bluetooth_get_controller(int index);

/**
 * Get controller count.
 */
int bluetooth_controller_count(void);

/* ============ Power ============ */

/**
 * Power on/off Bluetooth.
 */
int bluetooth_set_power(bt_controller_t *ctrl, int on);

/**
 * Check if powered.
 */
int bluetooth_is_powered(bt_controller_t *ctrl);

/* ============ Discovery ============ */

/**
 * Start scanning for devices.
 */
int bluetooth_start_scan(bt_controller_t *ctrl);

/**
 * Stop scanning.
 */
int bluetooth_stop_scan(bt_controller_t *ctrl);

/**
 * Get discovered devices.
 */
int bluetooth_get_devices(bt_controller_t *ctrl, bt_device_t *devices, int max);

/**
 * Set discoverable mode.
 */
int bluetooth_set_discoverable(bt_controller_t *ctrl, int discoverable);

/* ============ Pairing ============ */

/**
 * Pair with device.
 */
int bluetooth_pair(bt_controller_t *ctrl, bt_addr_t *addr, const char *pin);

/**
 * Remove pairing.
 */
int bluetooth_unpair(bt_controller_t *ctrl, bt_addr_t *addr);

/**
 * Get paired devices.
 */
int bluetooth_get_paired(bt_controller_t *ctrl, bt_device_t *devices, int max);

/* ============ Connection ============ */

/**
 * Connect to device.
 */
int bluetooth_connect(bt_controller_t *ctrl, bt_addr_t *addr);

/**
 * Disconnect from device.
 */
int bluetooth_disconnect(bt_controller_t *ctrl, bt_addr_t *addr);

/**
 * Get connection state.
 */
bt_state_t bluetooth_get_state(bt_controller_t *ctrl, bt_addr_t *addr);

/* ============ Audio (A2DP) ============ */

/**
 * Set audio output to Bluetooth device.
 */
int bluetooth_audio_connect(bt_controller_t *ctrl, bt_addr_t *addr);

/**
 * Disconnect audio.
 */
int bluetooth_audio_disconnect(bt_controller_t *ctrl);

/* ============ HID (Input devices) ============ */

typedef void (*bt_hid_callback_t)(bt_device_t *dev, uint8_t *report, int len);

/**
 * Register HID callback.
 */
int bluetooth_register_hid(bt_controller_t *ctrl, bt_hid_callback_t callback);

#endif /* NEOLYX_BLUETOOTH_H */
