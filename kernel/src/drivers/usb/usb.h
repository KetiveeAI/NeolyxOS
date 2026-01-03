/*
 * NeolyxOS USB Driver
 * 
 * Production-ready USB host controller driver with:
 * - XHCI (USB 3.0) controller support
 * - Device enumeration
 * - Hub support
 * - Mass storage class
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef USB_H
#define USB_H

#include <stdint.h>
#include "../pci/pci.h"

/* ============ USB Constants ============ */
#define USB_MAX_DEVICES     127
#define USB_MAX_ENDPOINTS   32
#define USB_MAX_CONTROLLERS 4

/* ============ USB Speeds ============ */
#define USB_SPEED_LOW       1    /* 1.5 Mbps */
#define USB_SPEED_FULL      2    /* 12 Mbps */
#define USB_SPEED_HIGH      3    /* 480 Mbps */
#define USB_SPEED_SUPER     4    /* 5 Gbps */
#define USB_SPEED_SUPER_PLUS 5   /* 10 Gbps */

/* ============ USB Request Types ============ */
#define USB_REQ_GET_STATUS      0x00
#define USB_REQ_CLEAR_FEATURE   0x01
#define USB_REQ_SET_FEATURE     0x03
#define USB_REQ_SET_ADDRESS     0x05
#define USB_REQ_GET_DESCRIPTOR  0x06
#define USB_REQ_SET_DESCRIPTOR  0x07
#define USB_REQ_GET_CONFIG      0x08
#define USB_REQ_SET_CONFIG      0x09

/* ============ USB Descriptor Types ============ */
#define USB_DESC_DEVICE         0x01
#define USB_DESC_CONFIG         0x02
#define USB_DESC_STRING         0x03
#define USB_DESC_INTERFACE      0x04
#define USB_DESC_ENDPOINT       0x05
#define USB_DESC_HID            0x21
#define USB_DESC_HUB            0x29

/* ============ USB Classes ============ */
#define USB_CLASS_INTERFACE     0x00
#define USB_CLASS_AUDIO         0x01
#define USB_CLASS_CDC           0x02
#define USB_CLASS_HID           0x03
#define USB_CLASS_PHYSICAL      0x05
#define USB_CLASS_IMAGE         0x06
#define USB_CLASS_PRINTER       0x07
#define USB_CLASS_MASS_STORAGE  0x08
#define USB_CLASS_HUB           0x09
#define USB_CLASS_CDC_DATA      0x0A
#define USB_CLASS_SMART_CARD    0x0B
#define USB_CLASS_VIDEO         0x0E
#define USB_CLASS_WIRELESS      0xE0
#define USB_CLASS_VENDOR        0xFF

/* ============ USB Device Descriptor ============ */
typedef struct {
    uint8_t  length;
    uint8_t  descriptor_type;
    uint16_t bcd_usb;
    uint8_t  device_class;
    uint8_t  device_subclass;
    uint8_t  device_protocol;
    uint8_t  max_packet_size;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t bcd_device;
    uint8_t  manufacturer_idx;
    uint8_t  product_idx;
    uint8_t  serial_idx;
    uint8_t  num_configs;
} __attribute__((packed)) usb_device_descriptor_t;

/* ============ USB Setup Packet ============ */
typedef struct {
    uint8_t  request_type;
    uint8_t  request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} __attribute__((packed)) usb_setup_packet_t;

/* ============ USB Endpoint ============ */
typedef struct {
    uint8_t  address;
    uint8_t  type;       /* Control, Bulk, Interrupt, Isochronous */
    uint16_t max_packet;
    uint8_t  interval;
} usb_endpoint_t;

/* ============ USB Device ============ */
typedef struct usb_device {
    uint8_t  address;
    uint8_t  speed;
    uint8_t  port;
    uint8_t  slot_id;     /* XHCI slot */
    
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t  device_class;
    uint8_t  device_subclass;
    uint8_t  device_protocol;
    
    char     manufacturer[64];
    char     product[64];
    char     serial[32];
    
    usb_endpoint_t endpoints[USB_MAX_ENDPOINTS];
    int      endpoint_count;
    
    struct usb_device *next;
} usb_device_t;

/* ============ XHCI Controller ============ */
typedef struct {
    pci_device_t *pci_dev;
    volatile uint32_t *cap_regs;      /* Capability registers */
    volatile uint32_t *op_regs;       /* Operational registers */
    volatile uint32_t *runtime_regs;  /* Runtime registers */
    volatile uint32_t *doorbell_regs; /* Doorbell registers */
    
    uint8_t  max_ports;
    uint8_t  max_slots;
    uint16_t max_interrupters;
    
    void    *dcbaa;          /* Device Context Base Address Array */
    void    *cmd_ring;       /* Command Ring */
    void    *event_ring;     /* Event Ring */
    
    usb_device_t *devices;
    int      device_count;
    
    uint8_t  initialized;
} xhci_controller_t;

/* ============ Public API ============ */

/**
 * usb_init - Initialize USB subsystem
 * 
 * Scans PCI for USB controllers and initializes them.
 * 
 * Returns: Number of controllers found, or negative on error
 */
int usb_init(void);

/**
 * usb_get_controller - Get controller by index
 */
xhci_controller_t *usb_get_controller(int index);

/**
 * usb_enumerate_devices - Enumerate devices on controller
 */
int usb_enumerate_devices(xhci_controller_t *ctrl);

/**
 * usb_get_device - Get device by address
 */
usb_device_t *usb_get_device(xhci_controller_t *ctrl, uint8_t address);

/**
 * usb_control_transfer - Perform control transfer
 */
int usb_control_transfer(usb_device_t *dev, usb_setup_packet_t *setup,
                        void *data, uint16_t length);

/**
 * usb_bulk_transfer - Perform bulk transfer
 */
int usb_bulk_transfer(usb_device_t *dev, uint8_t endpoint,
                     void *data, uint32_t length, int direction);

/**
 * usb_get_controller_count - Get number of detected controllers
 */
int usb_get_controller_count(void);

/**
 * usb_get_class_name - Get human-readable class name
 */
const char *usb_get_class_name(uint8_t class_code);

#endif /* USB_H */
