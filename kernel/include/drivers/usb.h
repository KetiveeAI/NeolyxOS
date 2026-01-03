/*
 * NeolyxOS USB Subsystem
 * 
 * USB Host Controller Drivers:
 *   - xHCI (USB 3.x) - Primary
 *   - EHCI (USB 2.0) - Legacy fallback
 * 
 * Device Classes:
 *   - Mass Storage (flash drives, external HDD)
 *   - HID (keyboard, mouse)
 *   - Hub
 *   - Audio
 *   - Network (USB Ethernet, WiFi dongles)
 *   - Bluetooth
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#ifndef NEOLYX_USB_H
#define NEOLYX_USB_H

#include <stdint.h>

/* ============ USB Speeds ============ */

typedef enum {
    USB_SPEED_LOW = 0,      /* 1.5 Mbps */
    USB_SPEED_FULL,         /* 12 Mbps */
    USB_SPEED_HIGH,         /* 480 Mbps (USB 2.0) */
    USB_SPEED_SUPER,        /* 5 Gbps (USB 3.0) */
    USB_SPEED_SUPER_PLUS,   /* 10 Gbps (USB 3.1) */
    USB_SPEED_SUPER_PLUS_X2,/* 20 Gbps (USB 3.2) */
} usb_speed_t;

/* ============ USB Device Classes ============ */

#define USB_CLASS_AUDIO         0x01
#define USB_CLASS_COMM          0x02
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
#define USB_CLASS_MISC          0xEF
#define USB_CLASS_VENDOR        0xFF

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

/* ============ USB Device Descriptor ============ */

typedef struct {
    uint8_t length;
    uint8_t type;
    uint16_t usb_version;
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
    uint8_t max_packet_size;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_version;
    uint8_t manufacturer_index;
    uint8_t product_index;
    uint8_t serial_index;
    uint8_t num_configs;
} __attribute__((packed)) usb_device_desc_t;

/* ============ USB Endpoint ============ */

typedef struct {
    uint8_t address;        /* Endpoint address */
    uint8_t type;           /* Control, Bulk, Interrupt, Isochronous */
    uint16_t max_packet;
    uint8_t interval;
} usb_endpoint_t;

#define USB_EP_TYPE_CONTROL     0
#define USB_EP_TYPE_ISOC        1
#define USB_EP_TYPE_BULK        2
#define USB_EP_TYPE_INTERRUPT   3

/* ============ USB Device ============ */

typedef struct usb_device {
    uint8_t address;            /* Device address (1-127) */
    usb_speed_t speed;
    
    /* Controller that owns this device */
    struct usb_controller *controller;
    
    /* Descriptors */
    usb_device_desc_t desc;
    char manufacturer[64];
    char product[64];
    char serial[64];
    
    /* Endpoints */
    usb_endpoint_t endpoints[16];
    int num_endpoints;
    
    /* State */
    int configured;
    int suspended;
    
    /* Parent hub (NULL for root) */
    struct usb_device *parent;
    uint8_t port;
    
    /* Driver */
    void *driver_data;
    
    struct usb_device *next;
} usb_device_t;

/* ============ USB Controller Types ============ */

typedef enum {
    USB_CTRL_UHCI,      /* USB 1.x */
    USB_CTRL_OHCI,      /* USB 1.x */
    USB_CTRL_EHCI,      /* USB 2.0 */
    USB_CTRL_XHCI,      /* USB 3.x */
} usb_controller_type_t;

/* ============ xHCI Registers ============ */

#define XHCI_CAP_CAPLENGTH      0x00
#define XHCI_CAP_HCIVERSION     0x02
#define XHCI_CAP_HCSPARAMS1     0x04
#define XHCI_CAP_HCSPARAMS2     0x08
#define XHCI_CAP_HCSPARAMS3     0x0C
#define XHCI_CAP_HCCPARAMS1     0x10
#define XHCI_CAP_DBOFF          0x14
#define XHCI_CAP_RTSOFF         0x18

#define XHCI_OP_USBCMD          0x00
#define XHCI_OP_USBSTS          0x04
#define XHCI_OP_DNCTRL          0x14
#define XHCI_OP_CRCR            0x18
#define XHCI_OP_DCBAAP          0x30
#define XHCI_OP_CONFIG          0x38

/* ============ USB Controller ============ */

typedef struct usb_controller {
    usb_controller_type_t type;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    
    volatile uint8_t *mmio_base;
    volatile uint8_t *op_base;      /* Operational registers */
    volatile uint8_t *rt_base;      /* Runtime registers */
    volatile uint8_t *db_base;      /* Doorbell registers */
    
    /* Ports */
    int num_ports;
    
    /* Devices */
    usb_device_t *devices[128];     /* By address */
    int device_count;
    
    /* xHCI specific */
    void *dcbaa;                    /* Device Context Base Address Array */
    void *cmd_ring;                 /* Command Ring */
    void *event_ring;               /* Event Ring */
    
    struct usb_controller *next;
} usb_controller_t;

/* ============ USB Transfer ============ */

typedef struct {
    usb_device_t *device;
    usb_endpoint_t *endpoint;
    
    uint8_t *buffer;
    uint32_t length;
    
    int direction;      /* 0 = OUT, 1 = IN */
    int complete;
    int status;
} usb_transfer_t;

/* ============ USB API ============ */

/**
 * Initialize USB subsystem.
 */
int usb_init(void);

/**
 * Scan for USB devices.
 */
int usb_scan(void);

/**
 * Get USB controller.
 */
usb_controller_t *usb_get_controller(int index);

/**
 * Get number of controllers.
 */
int usb_controller_count(void);

/**
 * Get device by address.
 */
usb_device_t *usb_get_device(usb_controller_t *ctrl, uint8_t address);

/**
 * Get device count.
 */
int usb_device_count(usb_controller_t *ctrl);

/* ============ Transfers ============ */

/**
 * Control transfer.
 */
int usb_control_transfer(usb_device_t *dev, uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index, void *data, uint16_t length);

/**
 * Bulk transfer.
 */
int usb_bulk_transfer(usb_device_t *dev, usb_endpoint_t *ep,
                      void *data, uint32_t length, int direction);

/**
 * Interrupt transfer.
 */
int usb_interrupt_transfer(usb_device_t *dev, usb_endpoint_t *ep,
                           void *data, uint32_t length);

/* ============ Device Management ============ */

/**
 * Get device descriptor.
 */
int usb_get_device_descriptor(usb_device_t *dev);

/**
 * Get string descriptor.
 */
int usb_get_string(usb_device_t *dev, uint8_t index, char *buf, int max);

/**
 * Set device configuration.
 */
int usb_set_config(usb_device_t *dev, uint8_t config);

/**
 * Reset device.
 */
int usb_reset_device(usb_device_t *dev);

#endif /* NEOLYX_USB_H */
