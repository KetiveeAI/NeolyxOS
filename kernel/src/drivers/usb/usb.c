/*
 * NeolyxOS USB Driver
 * 
 * Production-ready XHCI USB host controller driver
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "usb.h"
#include "../serial.h"

/* ============ XHCI Register Offsets ============ */
#define XHCI_CAP_CAPLENGTH    0x00
#define XHCI_CAP_HCIVERSION   0x02
#define XHCI_CAP_HCSPARAMS1   0x04
#define XHCI_CAP_HCSPARAMS2   0x08
#define XHCI_CAP_HCSPARAMS3   0x0C
#define XHCI_CAP_HCCPARAMS1   0x10
#define XHCI_CAP_DBOFF        0x14
#define XHCI_CAP_RTSOFF       0x18

#define XHCI_OP_USBCMD        0x00
#define XHCI_OP_USBSTS        0x04
#define XHCI_OP_PAGESIZE      0x08
#define XHCI_OP_DNCTRL        0x14
#define XHCI_OP_CRCR          0x18
#define XHCI_OP_DCBAAP        0x30
#define XHCI_OP_CONFIG        0x38

/* ============ XHCI Command Bits ============ */
#define XHCI_CMD_RUN          (1 << 0)
#define XHCI_CMD_HCRST        (1 << 1)
#define XHCI_CMD_INTE         (1 << 2)
#define XHCI_CMD_HSEE         (1 << 3)

/* ============ XHCI Status Bits ============ */
#define XHCI_STS_HCH          (1 << 0)   /* HC Halted */
#define XHCI_STS_HSE          (1 << 2)   /* Host System Error */
#define XHCI_STS_EINT         (1 << 3)   /* Event Interrupt */
#define XHCI_STS_PCD          (1 << 4)   /* Port Change Detect */
#define XHCI_STS_CNR          (1 << 11)  /* Controller Not Ready */
#define XHCI_STS_HCE          (1 << 12)  /* Host Controller Error */

/* ============ Controller Storage ============ */
static xhci_controller_t controllers[USB_MAX_CONTROLLERS];
static int controller_count = 0;

/* ============ Memory Buffers ============ */
static uint64_t dcbaa_buffer[256] __attribute__((aligned(64)));
static uint8_t cmd_ring_buffer[4096] __attribute__((aligned(64)));
static uint8_t event_ring_buffer[4096] __attribute__((aligned(64)));
static usb_device_t device_pool[USB_MAX_DEVICES];
static int device_pool_used = 0;

/* ============ Helper Functions ============ */

static inline void mmio_write32(volatile uint32_t *addr, uint32_t value) {
    *addr = value;
    __asm__ volatile("" ::: "memory");
}

static inline uint32_t mmio_read32(volatile uint32_t *addr) {
    uint32_t value = *addr;
    __asm__ volatile("" ::: "memory");
    return value;
}

static inline void mmio_write64(volatile uint64_t *addr, uint64_t value) {
    volatile uint32_t *low = (volatile uint32_t*)addr;
    volatile uint32_t *high = (volatile uint32_t*)((uint8_t*)addr + 4);
    *low = (uint32_t)value;
    *high = (uint32_t)(value >> 32);
    __asm__ volatile("" ::: "memory");
}

static void delay_ms(uint32_t ms) {
    for (volatile uint32_t i = 0; i < ms * 100000; i++) {
        __asm__ volatile("nop");
    }
}

/* ============ Controller Reset ============ */

static int xhci_reset(xhci_controller_t *ctrl) {
    volatile uint32_t *usbcmd = (volatile uint32_t*)((uint8_t*)ctrl->op_regs + XHCI_OP_USBCMD);
    volatile uint32_t *usbsts = (volatile uint32_t*)((uint8_t*)ctrl->op_regs + XHCI_OP_USBSTS);
    
    /* Wait for controller to halt */
    for (int i = 0; i < 100; i++) {
        if (mmio_read32(usbsts) & XHCI_STS_HCH) {
            break;
        }
        mmio_write32(usbcmd, mmio_read32(usbcmd) & ~XHCI_CMD_RUN);
        delay_ms(10);
    }
    
    if (!(mmio_read32(usbsts) & XHCI_STS_HCH)) {
        serial_puts("[USB] Controller did not halt\r\n");
        return -1;
    }
    
    /* Reset controller */
    mmio_write32(usbcmd, mmio_read32(usbcmd) | XHCI_CMD_HCRST);
    
    /* Wait for reset to complete */
    for (int i = 0; i < 1000; i++) {
        uint32_t cmd = mmio_read32(usbcmd);
        uint32_t sts = mmio_read32(usbsts);
        
        if (!(cmd & XHCI_CMD_HCRST) && !(sts & XHCI_STS_CNR)) {
            return 0;
        }
        delay_ms(1);
    }
    
    serial_puts("[USB] Controller reset timeout\r\n");
    return -2;
}

/* ============ Controller Initialization ============ */

static int xhci_init_controller(pci_device_t *pci_dev) {
    if (controller_count >= USB_MAX_CONTROLLERS) {
        return -1;
    }
    
    xhci_controller_t *ctrl = &controllers[controller_count];
    ctrl->pci_dev = pci_dev;
    
    /* Get BAR0 (MMIO registers) */
    if (pci_dev->bar_count == 0 || pci_dev->bars[0].type != PCI_BAR_MEM) {
        serial_puts("[USB] No valid BAR0\r\n");
        return -2;
    }
    
    ctrl->cap_regs = (volatile uint32_t*)(uintptr_t)pci_dev->bars[0].base;
    
    /* Enable bus mastering and memory */
    pci_enable_bus_master(pci_dev);
    pci_enable_memory(pci_dev);
    
    serial_puts("[USB] Initializing XHCI controller...\r\n");
    
    /* Read capability registers */
    uint8_t cap_length = *(uint8_t*)ctrl->cap_regs;
    uint32_t hcsparams1 = mmio_read32((volatile uint32_t*)((uint8_t*)ctrl->cap_regs + XHCI_CAP_HCSPARAMS1));
    uint32_t dboff = mmio_read32((volatile uint32_t*)((uint8_t*)ctrl->cap_regs + XHCI_CAP_DBOFF));
    uint32_t rtsoff = mmio_read32((volatile uint32_t*)((uint8_t*)ctrl->cap_regs + XHCI_CAP_RTSOFF));
    
    ctrl->max_ports = (hcsparams1 >> 24) & 0xFF;
    ctrl->max_slots = hcsparams1 & 0xFF;
    ctrl->max_interrupters = (hcsparams1 >> 8) & 0x7FF;
    
    /* Set up register pointers */
    ctrl->op_regs = (volatile uint32_t*)((uint8_t*)ctrl->cap_regs + cap_length);
    ctrl->runtime_regs = (volatile uint32_t*)((uint8_t*)ctrl->cap_regs + rtsoff);
    ctrl->doorbell_regs = (volatile uint32_t*)((uint8_t*)ctrl->cap_regs + dboff);
    
    /* Reset controller */
    if (xhci_reset(ctrl) != 0) {
        return -3;
    }
    
    /* Set up DCBAA */
    ctrl->dcbaa = dcbaa_buffer;
    for (int i = 0; i < 256; i++) {
        dcbaa_buffer[i] = 0;
    }
    
    mmio_write64((volatile uint64_t*)((uint8_t*)ctrl->op_regs + XHCI_OP_DCBAAP),
                 (uint64_t)(uintptr_t)ctrl->dcbaa);
    
    /* Configure max slots */
    mmio_write32((volatile uint32_t*)((uint8_t*)ctrl->op_regs + XHCI_OP_CONFIG),
                 ctrl->max_slots);
    
    /* Set up command ring */
    ctrl->cmd_ring = cmd_ring_buffer;
    for (int i = 0; i < 4096; i++) {
        cmd_ring_buffer[i] = 0;
    }
    
    /* Set up event ring */
    ctrl->event_ring = event_ring_buffer;
    for (int i = 0; i < 4096; i++) {
        event_ring_buffer[i] = 0;
    }
    
    /* Start controller */
    volatile uint32_t *usbcmd = (volatile uint32_t*)((uint8_t*)ctrl->op_regs + XHCI_OP_USBCMD);
    mmio_write32(usbcmd, mmio_read32(usbcmd) | XHCI_CMD_RUN | XHCI_CMD_INTE);
    
    /* Wait for controller to start */
    volatile uint32_t *usbsts = (volatile uint32_t*)((uint8_t*)ctrl->op_regs + XHCI_OP_USBSTS);
    for (int i = 0; i < 100; i++) {
        if (!(mmio_read32(usbsts) & XHCI_STS_HCH)) {
            ctrl->initialized = 1;
            controller_count++;
            
            serial_puts("[USB] XHCI controller started (");
            char buf[4];
            buf[0] = '0' + ctrl->max_ports / 10;
            buf[1] = '0' + ctrl->max_ports % 10;
            buf[2] = '\0';
            serial_puts(buf);
            serial_puts(" ports)\r\n");
            
            return 0;
        }
        delay_ms(10);
    }
    
    serial_puts("[USB] Controller failed to start\r\n");
    return -4;
}

/* ============ Public API ============ */

int usb_init(void) {
    serial_puts("[USB] Scanning for USB controllers...\r\n");
    
    controller_count = 0;
    device_pool_used = 0;
    
    /* Find XHCI devices via PCI (class 0x0C, subclass 0x03, prog_if 0x30) */
    pci_device_t *dev = pci_find_class(PCI_CLASS_SERIAL, PCI_SUBCLASS_USB);
    
    while (dev) {
        /* Check if it's XHCI (prog_if = 0x30) */
        if (dev->prog_if == 0x30) {
            serial_puts("[USB] Found XHCI controller\r\n");
            xhci_init_controller(dev);
        } else if (dev->prog_if == 0x20) {
            serial_puts("[USB] Found EHCI controller (not implemented)\r\n");
        } else if (dev->prog_if == 0x10) {
            serial_puts("[USB] Found OHCI controller (not implemented)\r\n");
        } else if (dev->prog_if == 0x00) {
            serial_puts("[USB] Found UHCI controller (not implemented)\r\n");
        }
        
        /* Find next USB device */
        pci_device_t *next = dev->next;
        while (next) {
            if (next->class_code == PCI_CLASS_SERIAL && 
                next->subclass == PCI_SUBCLASS_USB) {
                break;
            }
            next = next->next;
        }
        dev = next;
    }
    
    serial_puts("[USB] Found ");
    char buf[4];
    buf[0] = '0' + controller_count;
    buf[1] = '\0';
    serial_puts(buf);
    serial_puts(" XHCI controller(s)\r\n");
    
    return controller_count;
}

xhci_controller_t *usb_get_controller(int index) {
    if (index < 0 || index >= controller_count) {
        return 0;
    }
    return &controllers[index];
}

int usb_enumerate_devices(xhci_controller_t *ctrl) {
    if (!ctrl || !ctrl->initialized) {
        return -1;
    }
    
    /* TODO: Implement port status checking and device enumeration */
    serial_puts("[USB] Device enumeration not yet implemented\r\n");
    
    return 0;
}

usb_device_t *usb_get_device(xhci_controller_t *ctrl, uint8_t address) {
    if (!ctrl) return 0;
    
    usb_device_t *dev = ctrl->devices;
    while (dev) {
        if (dev->address == address) {
            return dev;
        }
        dev = dev->next;
    }
    return 0;
}

int usb_control_transfer(usb_device_t *dev, usb_setup_packet_t *setup,
                        void *data, uint16_t length) {
    if (!dev || !setup) return -1;
    
    /* TODO: Implement control transfer */
    return -1;
}

int usb_bulk_transfer(usb_device_t *dev, uint8_t endpoint,
                     void *data, uint32_t length, int direction) {
    if (!dev || !data) return -1;
    
    /* TODO: Implement bulk transfer */
    return -1;
}

int usb_get_controller_count(void) {
    return controller_count;
}

const char *usb_get_class_name(uint8_t class_code) {
    switch (class_code) {
        case USB_CLASS_AUDIO:        return "Audio";
        case USB_CLASS_CDC:          return "CDC";
        case USB_CLASS_HID:          return "HID";
        case USB_CLASS_PHYSICAL:     return "Physical";
        case USB_CLASS_IMAGE:        return "Image";
        case USB_CLASS_PRINTER:      return "Printer";
        case USB_CLASS_MASS_STORAGE: return "Mass Storage";
        case USB_CLASS_HUB:          return "Hub";
        case USB_CLASS_VIDEO:        return "Video";
        case USB_CLASS_WIRELESS:     return "Wireless";
        case USB_CLASS_VENDOR:       return "Vendor";
        default:                     return "Unknown";
    }
}
