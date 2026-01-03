/*
 * NXUSB Kernel Driver (nxusb.kdrv)
 * 
 * NeolyxOS USB Host Controller Kernel Driver
 * 
 * Architecture:
 *   [ NXUSB Kernel Driver ] ← XHCI/EHCI/UHCI
 *        ↕ Device Enumeration
 *   [ USB Device Drivers ] ← Class drivers
 *        ↕ Kernel API
 *   [ User Applications ]
 * 
 * Supports: XHCI (USB 3.x), EHCI (USB 2.0), UHCI/OHCI (USB 1.x)
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXUSB_KDRV_H
#define NXUSB_KDRV_H

#include <stdint.h>
#include <stddef.h>

/* ============ Driver Metadata ============ */

#define NXUSB_KDRV_VERSION    "1.0.0"
#define NXUSB_KDRV_ABI        1

/* ============ USB Controller Types ============ */

typedef enum {
    NXUSB_CTRL_NONE = 0,
    NXUSB_CTRL_UHCI,        /* USB 1.x */
    NXUSB_CTRL_OHCI,        /* USB 1.x */
    NXUSB_CTRL_EHCI,        /* USB 2.0 */
    NXUSB_CTRL_XHCI,        /* USB 3.x */
} nxusb_ctrl_type_t;

/* ============ USB Speed ============ */

typedef enum {
    NXUSB_SPEED_LOW = 0,    /* 1.5 Mbps */
    NXUSB_SPEED_FULL,       /* 12 Mbps */
    NXUSB_SPEED_HIGH,       /* 480 Mbps */
    NXUSB_SPEED_SUPER,      /* 5 Gbps */
    NXUSB_SPEED_SUPER_PLUS, /* 10 Gbps */
} nxusb_speed_t;

/* ============ USB Device Class ============ */

typedef enum {
    NXUSB_CLASS_NONE = 0x00,
    NXUSB_CLASS_AUDIO = 0x01,
    NXUSB_CLASS_CDC = 0x02,
    NXUSB_CLASS_HID = 0x03,
    NXUSB_CLASS_PHYSICAL = 0x05,
    NXUSB_CLASS_IMAGE = 0x06,
    NXUSB_CLASS_PRINTER = 0x07,
    NXUSB_CLASS_MASS_STORAGE = 0x08,
    NXUSB_CLASS_HUB = 0x09,
    NXUSB_CLASS_CDC_DATA = 0x0A,
    NXUSB_CLASS_VIDEO = 0x0E,
    NXUSB_CLASS_WIRELESS = 0xE0,
    NXUSB_CLASS_MISC = 0xEF,
    NXUSB_CLASS_VENDOR = 0xFF,
} nxusb_class_t;

/* ============ Controller Info ============ */

typedef struct {
    uint32_t            id;
    nxusb_ctrl_type_t   type;
    char                name[32];
    uint8_t             pci_bus;
    uint8_t             pci_dev;
    uint8_t             pci_func;
    uint32_t            ports;
    uint32_t            max_speed;
    uint8_t             initialized;
} nxusb_controller_info_t;

/* ============ Device Info ============ */

typedef struct {
    uint32_t            id;
    uint8_t             address;
    nxusb_speed_t       speed;
    uint16_t            vendor_id;
    uint16_t            product_id;
    nxusb_class_t       device_class;
    uint8_t             subclass;
    uint8_t             protocol;
    char                manufacturer[64];
    char                product[64];
    char                serial[32];
    uint8_t             num_configurations;
    uint8_t             controller_id;
    uint8_t             port;
    uint8_t             connected;
} nxusb_device_info_t;

/* ============ Kernel Driver API ============ */

/**
 * Initialize USB subsystem
 */
int nxusb_kdrv_init(void);

/**
 * Shutdown USB subsystem
 */
void nxusb_kdrv_shutdown(void);

/**
 * Get number of USB controllers
 */
int nxusb_kdrv_controller_count(void);

/**
 * Get controller info
 */
int nxusb_kdrv_controller_info(int index, nxusb_controller_info_t *info);

/**
 * Get number of connected USB devices
 */
int nxusb_kdrv_device_count(void);

/**
 * Get device info by index
 */
int nxusb_kdrv_device_info(int index, nxusb_device_info_t *info);

/**
 * Trigger USB bus rescan
 */
int nxusb_kdrv_rescan(void);

/**
 * Reset USB device
 */
int nxusb_kdrv_reset_device(int device_id);

/**
 * Get device class name
 */
const char *nxusb_kdrv_class_name(nxusb_class_t class_code);

/**
 * Get speed name
 */
const char *nxusb_kdrv_speed_name(nxusb_speed_t speed);

/**
 * Debug output
 */
void nxusb_kdrv_debug(void);

#endif /* NXUSB_KDRV_H */
