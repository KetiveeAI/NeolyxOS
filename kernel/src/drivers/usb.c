/*
 * NeolyxOS USB Host Controller Driver (xHCI)
 * 
 * USB 3.x support with fallback to 2.0.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "drivers/usb.h"
#include "mm/kheap.h"
#include "mm/pmm.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ PCI Access ============ */

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

static void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(0xCF8, addr);
    outl(0xCFC, val);
}

/* ============ State ============ */

static usb_controller_t *usb_controllers = NULL;
static int usb_ctrl_count = 0;

/* ============ Helpers ============ */

static void usb_memset(void *s, int c, uint32_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
}

static void usb_memcpy(void *d, const void *s, uint32_t n) {
    uint8_t *dp = (uint8_t *)d;
    const uint8_t *sp = (const uint8_t *)s;
    while (n--) *dp++ = *sp++;
}

/* ============ xHCI Register Access ============ */

static uint32_t xhci_read32(usb_controller_t *ctrl, uint32_t offset) {
    return *(volatile uint32_t *)(ctrl->mmio_base + offset);
}

static void xhci_write32(usb_controller_t *ctrl, uint32_t offset, uint32_t val) {
    *(volatile uint32_t *)(ctrl->mmio_base + offset) = val;
}

static uint32_t xhci_op_read32(usb_controller_t *ctrl, uint32_t offset) {
    return *(volatile uint32_t *)(ctrl->op_base + offset);
}

static void xhci_op_write32(usb_controller_t *ctrl, uint32_t offset, uint32_t val) {
    *(volatile uint32_t *)(ctrl->op_base + offset) = val;
}

/* ============ xHCI Initialization ============ */

static int xhci_reset(usb_controller_t *ctrl) {
    /* Set HCRST bit */
    uint32_t cmd = xhci_op_read32(ctrl, XHCI_OP_USBCMD);
    xhci_op_write32(ctrl, XHCI_OP_USBCMD, cmd | (1 << 1));
    
    /* Wait for reset complete */
    int timeout = 1000;
    while ((xhci_op_read32(ctrl, XHCI_OP_USBCMD) & (1 << 1)) && timeout--) {
        for (volatile int i = 0; i < 1000; i++);
    }
    
    /* Wait for CNR (Controller Not Ready) to clear */
    timeout = 1000;
    while ((xhci_op_read32(ctrl, XHCI_OP_USBSTS) & (1 << 11)) && timeout--) {
        for (volatile int i = 0; i < 1000; i++);
    }
    
    if (timeout <= 0) {
        serial_puts("[USB] xHCI reset timeout\n");
        return -1;
    }
    
    return 0;
}

/* ============ EHCI Registers (USB 2.0) ============ */

#define EHCI_CAP_CAPLENGTH      0x00
#define EHCI_CAP_HCIVERSION     0x02
#define EHCI_CAP_HCSPARAMS      0x04
#define EHCI_CAP_HCCPARAMS      0x08

#define EHCI_OP_USBCMD          0x00
#define EHCI_OP_USBSTS          0x04
#define EHCI_OP_USBINTR         0x08
#define EHCI_OP_FRINDEX         0x0C
#define EHCI_OP_PERIODICLIST    0x14
#define EHCI_OP_ASYNCLIST       0x18
#define EHCI_OP_CONFIGFLAG      0x40
#define EHCI_OP_PORTSC          0x44

/* EHCI USBCMD bits */
#define EHCI_CMD_RUN            (1 << 0)
#define EHCI_CMD_HCRESET        (1 << 1)
#define EHCI_CMD_ASYNC_EN       (1 << 5)
#define EHCI_CMD_PERIODIC_EN    (1 << 4)

/* EHCI USBSTS bits */
#define EHCI_STS_HALTED         (1 << 12)

static int ehci_reset(usb_controller_t *ctrl) {
    /* Get capability length */
    uint8_t cap_len = *(volatile uint8_t *)(ctrl->mmio_base + EHCI_CAP_CAPLENGTH);
    ctrl->op_base = ctrl->mmio_base + cap_len;
    
    /* Stop controller if running */
    uint32_t cmd = *(volatile uint32_t *)(ctrl->op_base + EHCI_OP_USBCMD);
    if (cmd & EHCI_CMD_RUN) {
        *(volatile uint32_t *)(ctrl->op_base + EHCI_OP_USBCMD) = cmd & ~EHCI_CMD_RUN;
        /* Wait for halt */
        int timeout = 1000;
        while (!(*(volatile uint32_t *)(ctrl->op_base + EHCI_OP_USBSTS) & EHCI_STS_HALTED) && timeout--) {
            for (volatile int i = 0; i < 1000; i++);
        }
    }
    
    /* Reset controller */
    *(volatile uint32_t *)(ctrl->op_base + EHCI_OP_USBCMD) = EHCI_CMD_HCRESET;
    
    /* Wait for reset complete */
    int timeout = 1000;
    while ((*(volatile uint32_t *)(ctrl->op_base + EHCI_OP_USBCMD) & EHCI_CMD_HCRESET) && timeout--) {
        for (volatile int i = 0; i < 1000; i++);
    }
    
    if (timeout <= 0) {
        serial_puts("[USB] EHCI reset timeout\n");
        return -1;
    }
    
    return 0;
}

static int ehci_init(usb_controller_t *ctrl) {
    serial_puts("[USB] Initializing EHCI (USB 2.0) controller...\n");
    
    /* Enable bus mastering */
    uint32_t cmd = pci_read(ctrl->bus, ctrl->slot, ctrl->func, 0x04);
    cmd |= (1 << 2) | (1 << 1);  /* Bus master + Memory space */
    pci_write(ctrl->bus, ctrl->slot, ctrl->func, 0x04, cmd);
    
    /* Reset controller */
    if (ehci_reset(ctrl) != 0) return -1;
    
    /* Get number of ports from HCSPARAMS */
    uint32_t hcsparams = *(volatile uint32_t *)(ctrl->mmio_base + EHCI_CAP_HCSPARAMS);
    ctrl->num_ports = hcsparams & 0x0F;
    
    serial_puts("[USB] EHCI: ");
    serial_putc('0' + ctrl->num_ports / 10);
    serial_putc('0' + ctrl->num_ports % 10);
    serial_puts(" ports\n");
    
    /* Set CONFIG_FLAG to route ports to EHCI */
    *(volatile uint32_t *)(ctrl->op_base + EHCI_OP_CONFIGFLAG) = 1;
    
    /* Wait for port routing */
    for (volatile int i = 0; i < 100000; i++);
    
    /* Start controller */
    uint32_t usbcmd = *(volatile uint32_t *)(ctrl->op_base + EHCI_OP_USBCMD);
    *(volatile uint32_t *)(ctrl->op_base + EHCI_OP_USBCMD) = usbcmd | EHCI_CMD_RUN;
    
    /* Wait for controller to start */
    int timeout = 1000;
    while ((*(volatile uint32_t *)(ctrl->op_base + EHCI_OP_USBSTS) & EHCI_STS_HALTED) && timeout--) {
        for (volatile int i = 0; i < 100; i++);
    }
    
    serial_puts("[USB] EHCI ready\n");
    return 0;
}

/* ============ OHCI Registers (USB 1.1) ============ */

#define OHCI_REG_REVISION       0x00
#define OHCI_REG_CONTROL        0x04
#define OHCI_REG_CMDSTATUS      0x08
#define OHCI_REG_INTRSTATUS     0x0C
#define OHCI_REG_INTRENABLE     0x10
#define OHCI_REG_HCCA           0x18
#define OHCI_REG_FMINTERVAL     0x34
#define OHCI_REG_RHDESCRA       0x48
#define OHCI_REG_RHPORTSTATUS   0x54

/* OHCI Control states */
#define OHCI_CTRL_RESET         0
#define OHCI_CTRL_RESUME        1
#define OHCI_CTRL_OPERATIONAL   2
#define OHCI_CTRL_SUSPEND       3

static int ohci_reset(usb_controller_t *ctrl) {
    /* Software reset */
    *(volatile uint32_t *)(ctrl->mmio_base + OHCI_REG_CMDSTATUS) = (1 << 0);
    
    /* Wait for reset complete */
    int timeout = 1000;
    while ((*(volatile uint32_t *)(ctrl->mmio_base + OHCI_REG_CMDSTATUS) & (1 << 0)) && timeout--) {
        for (volatile int i = 0; i < 1000; i++);
    }
    
    if (timeout <= 0) {
        serial_puts("[USB] OHCI reset timeout\n");
        return -1;
    }
    
    return 0;
}

static int ohci_init(usb_controller_t *ctrl) {
    serial_puts("[USB] Initializing OHCI (USB 1.1) controller...\n");
    
    /* Enable bus mastering */
    uint32_t cmd = pci_read(ctrl->bus, ctrl->slot, ctrl->func, 0x04);
    cmd |= (1 << 2) | (1 << 1);
    pci_write(ctrl->bus, ctrl->slot, ctrl->func, 0x04, cmd);
    
    ctrl->op_base = ctrl->mmio_base;  /* OHCI has no cap regs */
    
    /* Check revision */
    uint32_t rev = *(volatile uint32_t *)(ctrl->mmio_base + OHCI_REG_REVISION);
    serial_puts("[USB] OHCI revision: ");
    serial_putc('0' + ((rev >> 4) & 0xF));
    serial_putc('.');
    serial_putc('0' + (rev & 0xF));
    serial_puts("\n");
    
    /* Reset controller */
    if (ohci_reset(ctrl) != 0) return -1;
    
    /* Get number of ports */
    uint32_t rhdescra = *(volatile uint32_t *)(ctrl->mmio_base + OHCI_REG_RHDESCRA);
    ctrl->num_ports = rhdescra & 0xFF;
    
    serial_puts("[USB] OHCI: ");
    serial_putc('0' + ctrl->num_ports);
    serial_puts(" ports\n");
    
    /* Set frame interval */
    uint32_t fminterval = 0x2EDF;  /* 11999 (default frame interval) */
    fminterval |= (0x2778 << 16);  /* FS largest data packet */
    *(volatile uint32_t *)(ctrl->mmio_base + OHCI_REG_FMINTERVAL) = fminterval;
    
    /* Transition to operational state */
    uint32_t control = *(volatile uint32_t *)(ctrl->mmio_base + OHCI_REG_CONTROL);
    control = (control & ~(3 << 6)) | (OHCI_CTRL_OPERATIONAL << 6);
    *(volatile uint32_t *)(ctrl->mmio_base + OHCI_REG_CONTROL) = control;
    
    serial_puts("[USB] OHCI ready\n");
    return 0;
}

/* ============ UHCI Registers (USB 1.0 - Intel) ============ */

#define UHCI_REG_USBCMD         0x00
#define UHCI_REG_USBSTS         0x02
#define UHCI_REG_USBINTR        0x04
#define UHCI_REG_FRNUM          0x06
#define UHCI_REG_FRBASEADD      0x08
#define UHCI_REG_SOFMOD         0x0C
#define UHCI_REG_PORTSC1        0x10
#define UHCI_REG_PORTSC2        0x12

/* UHCI uses I/O port access */
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static int uhci_reset(usb_controller_t *ctrl) {
    uint16_t io_base = (uint16_t)(uintptr_t)ctrl->mmio_base;
    
    /* Global reset */
    outw(io_base + UHCI_REG_USBCMD, 0x0004);  /* GRESET */
    for (volatile int i = 0; i < 100000; i++);  /* 10ms delay */
    outw(io_base + UHCI_REG_USBCMD, 0x0000);
    
    /* Host controller reset */
    outw(io_base + UHCI_REG_USBCMD, 0x0002);  /* HCRESET */
    
    int timeout = 1000;
    while ((inw(io_base + UHCI_REG_USBCMD) & 0x0002) && timeout--) {
        for (volatile int i = 0; i < 1000; i++);
    }
    
    if (timeout <= 0) {
        serial_puts("[USB] UHCI reset timeout\n");
        return -1;
    }
    
    return 0;
}

static int uhci_init(usb_controller_t *ctrl) {
    serial_puts("[USB] Initializing UHCI (USB 1.0) controller...\n");
    
    /* Get I/O base from BAR4 (UHCI uses I/O, not MMIO) */
    uint32_t bar4 = pci_read(ctrl->bus, ctrl->slot, ctrl->func, 0x20);
    uint16_t io_base = bar4 & 0xFFFC;  /* I/O port address */
    ctrl->mmio_base = (volatile uint8_t *)(uintptr_t)io_base;  /* Store I/O base */
    ctrl->op_base = ctrl->mmio_base;
    
    serial_puts("[USB] UHCI I/O base: 0x");
    serial_putc("0123456789ABCDEF"[(io_base >> 12) & 0xF]);
    serial_putc("0123456789ABCDEF"[(io_base >> 8) & 0xF]);
    serial_putc("0123456789ABCDEF"[(io_base >> 4) & 0xF]);
    serial_putc("0123456789ABCDEF"[io_base & 0xF]);
    serial_puts("\n");
    
    /* Enable bus mastering and I/O space */
    uint32_t cmd = pci_read(ctrl->bus, ctrl->slot, ctrl->func, 0x04);
    cmd |= (1 << 2) | (1 << 0);  /* Bus master + I/O space */
    pci_write(ctrl->bus, ctrl->slot, ctrl->func, 0x04, cmd);
    
    /* Reset controller */
    if (uhci_reset(ctrl) != 0) return -1;
    
    /* UHCI has 2 root ports */
    ctrl->num_ports = 2;
    
    /* Clear status */
    outw(io_base + UHCI_REG_USBSTS, 0xFFFF);
    
    /* Set frame number to 0 */
    outw(io_base + UHCI_REG_FRNUM, 0);
    
    /* Set SOF timing */
    outw(io_base + UHCI_REG_SOFMOD, 64);  /* SOF timing value */
    
    /* Start controller */
    outw(io_base + UHCI_REG_USBCMD, 0x0001);  /* RS (Run/Stop) */
    
    serial_puts("[USB] UHCI ready (2 ports)\n");
    return 0;
}

static int xhci_init(usb_controller_t *ctrl) {
    serial_puts("[USB] Initializing xHCI controller...\n");
    
    /* Enable bus mastering */
    uint32_t cmd = pci_read(ctrl->bus, ctrl->slot, ctrl->func, 0x04);
    cmd |= (1 << 2) | (1 << 1);
    pci_write(ctrl->bus, ctrl->slot, ctrl->func, 0x04, cmd);
    
    /* Get capability register length */
    uint8_t cap_len = *(volatile uint8_t *)(ctrl->mmio_base + XHCI_CAP_CAPLENGTH);
    ctrl->op_base = ctrl->mmio_base + cap_len;
    
    /* Get runtime and doorbell offsets */
    uint32_t rtsoff = xhci_read32(ctrl, XHCI_CAP_RTSOFF) & ~0x1F;
    uint32_t dboff = xhci_read32(ctrl, XHCI_CAP_DBOFF) & ~0x3;
    ctrl->rt_base = ctrl->mmio_base + rtsoff;
    ctrl->db_base = ctrl->mmio_base + dboff;
    
    /* Read capabilities */
    uint32_t hcsparams1 = xhci_read32(ctrl, XHCI_CAP_HCSPARAMS1);
    ctrl->num_ports = (hcsparams1 >> 24) & 0xFF;
    
    uint16_t hci_ver = *(volatile uint16_t *)(ctrl->mmio_base + XHCI_CAP_HCIVERSION);
    
    serial_puts("[USB] xHCI v");
    serial_putc('0' + (hci_ver >> 8));
    serial_putc('.');
    serial_putc('0' + ((hci_ver >> 4) & 0xF));
    serial_putc('0' + (hci_ver & 0xF));
    serial_puts(", ");
    serial_putc('0' + ctrl->num_ports / 10);
    serial_putc('0' + ctrl->num_ports % 10);
    serial_puts(" ports\n");
    
    /* Stop controller if running */
    uint32_t usbcmd = xhci_op_read32(ctrl, XHCI_OP_USBCMD);
    if (usbcmd & 1) {
        xhci_op_write32(ctrl, XHCI_OP_USBCMD, usbcmd & ~1);
        while (!(xhci_op_read32(ctrl, XHCI_OP_USBSTS) & 1));
    }
    
    /* Reset controller */
    if (xhci_reset(ctrl) != 0) return -1;
    
    /* Allocate DCBAA (Device Context Base Address Array) */
    ctrl->dcbaa = kmalloc_aligned(256 * sizeof(uint64_t), 64);
    if (!ctrl->dcbaa) return -1;
    usb_memset(ctrl->dcbaa, 0, 256 * sizeof(uint64_t));
    
    /* Set DCBAAP */
    uint64_t dcbaa_phys = (uint64_t)(uintptr_t)ctrl->dcbaa;
    xhci_op_write32(ctrl, XHCI_OP_DCBAAP, (uint32_t)dcbaa_phys);
    xhci_op_write32(ctrl, XHCI_OP_DCBAAP + 4, (uint32_t)(dcbaa_phys >> 32));
    
    /* Set max device slots */
    uint32_t config = xhci_op_read32(ctrl, XHCI_OP_CONFIG);
    config = (config & ~0xFF) | 32;  /* 32 device slots */
    xhci_op_write32(ctrl, XHCI_OP_CONFIG, config);
    
    /* Start controller */
    usbcmd = xhci_op_read32(ctrl, XHCI_OP_USBCMD);
    xhci_op_write32(ctrl, XHCI_OP_USBCMD, usbcmd | 1);
    
    /* Wait for running */
    int timeout = 1000;
    while ((xhci_op_read32(ctrl, XHCI_OP_USBSTS) & 1) && timeout--) {
        for (volatile int i = 0; i < 100; i++);
    }
    
    serial_puts("[USB] xHCI ready\n");
    return 0;
}

/* ============ USB API ============ */

int usb_init(void) {
    serial_puts("[USB] Initializing USB subsystem...\n");
    
    usb_controllers = NULL;
    usb_ctrl_count = 0;
    
    /* Scan for USB controllers */
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                uint32_t vendor_device = pci_read(bus, slot, func, 0);
                if ((vendor_device & 0xFFFF) == 0xFFFF) continue;
                
                /* Check class (serial bus controller, USB) */
                uint32_t class_info = pci_read(bus, slot, func, 8);
                uint8_t base_class = (class_info >> 24) & 0xFF;
                uint8_t sub_class = (class_info >> 16) & 0xFF;
                uint8_t prog_if = (class_info >> 8) & 0xFF;
                
                if (base_class != 0x0C || sub_class != 0x03) continue;
                
                usb_controller_type_t type;
                const char *type_name;
                
                switch (prog_if) {
                    case 0x00: type = USB_CTRL_UHCI; type_name = "UHCI"; break;
                    case 0x10: type = USB_CTRL_OHCI; type_name = "OHCI"; break;
                    case 0x20: type = USB_CTRL_EHCI; type_name = "EHCI"; break;
                    case 0x30: type = USB_CTRL_XHCI; type_name = "xHCI"; break;
                    default: continue;
                }
                
                serial_puts("[USB] Found ");
                serial_puts(type_name);
                serial_puts(" controller\n");
                
                usb_controller_t *ctrl = (usb_controller_t *)kzalloc(sizeof(usb_controller_t));
                if (!ctrl) continue;
                
                ctrl->type = type;
                ctrl->bus = bus;
                ctrl->slot = slot;
                ctrl->func = func;
                
                /* Get BAR0 (except UHCI which uses BAR4) */
                if (type != USB_CTRL_UHCI) {
                    uint32_t bar0 = pci_read(bus, slot, func, 0x10);
                    uint64_t mmio = bar0 & ~0xF;
                    
                    if ((bar0 & 0x6) == 0x4) {
                        /* 64-bit BAR */
                        uint32_t bar1 = pci_read(bus, slot, func, 0x14);
                        mmio |= ((uint64_t)bar1 << 32);
                    }
                    
                    ctrl->mmio_base = (volatile uint8_t *)(uintptr_t)mmio;
                }
                
                /* Initialize controller based on type */
                int init_result = -1;
                switch (type) {
                    case USB_CTRL_XHCI:
                        init_result = xhci_init(ctrl);
                        break;
                    case USB_CTRL_EHCI:
                        init_result = ehci_init(ctrl);
                        break;
                    case USB_CTRL_OHCI:
                        init_result = ohci_init(ctrl);
                        break;
                    case USB_CTRL_UHCI:
                        init_result = uhci_init(ctrl);
                        break;
                }
                
                if (init_result == 0) {
                    ctrl->next = usb_controllers;
                    usb_controllers = ctrl;
                    usb_ctrl_count++;
                } else {
                    /* Free failed controller */
                    kfree(ctrl);
                }
            }
        }
    }
    
    if (usb_ctrl_count == 0) {
        serial_puts("[USB] No controllers found\n");
        return -1;
    }
    
    serial_puts("[USB] Ready (");
    serial_putc('0' + usb_ctrl_count);
    serial_puts(" controllers)\n");
    
    return 0;
}

int usb_scan(void) {
    serial_puts("[USB] Scanning for devices...\n");
    
    usb_controller_t *ctrl = usb_controllers;
    int total_devices = 0;
    
    while (ctrl) {
        /* Scan each port on this controller */
        for (int port = 0; port < ctrl->num_ports; port++) {
            /* Read Port Status and Control Register (PORTSC) */
            /* PORTSC offset = 0x400 + (0x10 * port_number) from operational base */
            uint32_t portsc_offset = 0x400 + (0x10 * port);
            uint32_t portsc = xhci_op_read32(ctrl, portsc_offset);
            
            /* Check Current Connect Status (bit 0) */
            if (!(portsc & 0x01)) continue;  /* No device connected */
            
            /* Get port speed (bits 10-13) */
            uint8_t speed = (portsc >> 10) & 0x0F;
            const char *speed_str;
            switch (speed) {
                case 1: speed_str = "Full-Speed (12Mbps)"; break;
                case 2: speed_str = "Low-Speed (1.5Mbps)"; break;
                case 3: speed_str = "High-Speed (480Mbps)"; break;
                case 4: speed_str = "SuperSpeed (5Gbps)"; break;
                case 5: speed_str = "SuperSpeed+ (10Gbps)"; break;
                default: speed_str = "Unknown"; break;
            }
            
            serial_puts("[USB] Port ");
            serial_putc('0' + port);
            serial_puts(": Device connected - ");
            serial_puts(speed_str);
            serial_puts("\n");
            
            /* Check if port is enabled (bit 1) */
            if (!(portsc & 0x02)) {
                /* Enable the port by triggering port reset (bit 4) */
                xhci_op_write32(ctrl, portsc_offset, portsc | (1 << 4));
                
                /* Wait for reset to complete */
                int timeout = 1000;
                while (timeout-- > 0) {
                    portsc = xhci_op_read32(ctrl, portsc_offset);
                    if (!(portsc & (1 << 4))) break;  /* Reset complete */
                    for (volatile int i = 0; i < 1000; i++) {}
                }
            }
            
            /* Allocate device structure */
            if (ctrl->device_count < 128) {
                usb_device_t *dev = (usb_device_t *)kzalloc(sizeof(usb_device_t));
                if (dev) {
                    dev->controller = ctrl;
                    dev->port = port;
                    dev->speed = speed;
                    dev->address = ctrl->device_count + 1;
                    
                    ctrl->devices[dev->address] = dev;
                    ctrl->device_count++;
                    total_devices++;
                }
            }
        }
        
        ctrl = ctrl->next;
    }
    
    serial_puts("[USB] Scan complete: ");
    serial_putc('0' + total_devices);
    serial_puts(" devices found\n");
    
    return total_devices;
}

usb_controller_t *usb_get_controller(int index) {
    usb_controller_t *ctrl = usb_controllers;
    int i = 0;
    while (ctrl && i < index) {
        ctrl = ctrl->next;
        i++;
    }
    return ctrl;
}

int usb_controller_count(void) {
    return usb_ctrl_count;
}

usb_device_t *usb_get_device(usb_controller_t *ctrl, uint8_t address) {
    if (!ctrl || address >= 128) return NULL;
    return ctrl->devices[address];
}

int usb_device_count(usb_controller_t *ctrl) {
    return ctrl ? ctrl->device_count : 0;
}

/* ============ xHCI TRB (Transfer Request Block) ============ */

/* TRB structure - 16 bytes */
typedef struct {
    uint64_t param;     /* Data buffer or parameter */
    uint32_t status;    /* Transfer length / status */
    uint32_t control;   /* Flags and TRB type */
} xhci_trb_t;

/* TRB Types */
#define TRB_TYPE_NORMAL         1
#define TRB_TYPE_SETUP          2
#define TRB_TYPE_DATA           3
#define TRB_TYPE_STATUS         4
#define TRB_TYPE_LINK           6
#define TRB_TYPE_CMD_ENABLE     9
#define TRB_TYPE_CMD_ADDRESS    11

/* ============ Transfers ============ */

int usb_control_transfer(usb_device_t *dev, uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index, void *data, uint16_t length) {
    if (!dev || !dev->controller) return -1;
    
    usb_controller_t *ctrl = dev->controller;
    
    /* Build Setup TRB (8-byte setup packet in param field) */
    xhci_trb_t setup_trb;
    setup_trb.param = ((uint64_t)length << 48) |
                      ((uint64_t)index << 32) |
                      ((uint64_t)value << 16) |
                      ((uint64_t)request << 8) |
                      request_type;
    setup_trb.status = 8;  /* Setup packet is 8 bytes */
    setup_trb.control = (TRB_TYPE_SETUP << 10) | (1 << 6);  /* IDT, Imm Data */
    
    /* Write to device's transfer ring (simplified - uses endpoint 0) */
    volatile xhci_trb_t *ring = (volatile xhci_trb_t *)(ctrl->op_base + 0x1000 + dev->address * 0x100);
    ring[0] = setup_trb;
    
    /* Data TRB if there's data to transfer */
    if (data && length > 0) {
        xhci_trb_t data_trb;
        data_trb.param = (uint64_t)(uintptr_t)data;
        data_trb.status = length;
        data_trb.control = (TRB_TYPE_DATA << 10) | 
                          ((request_type & 0x80) ? (1 << 16) : 0);  /* DIR bit */
        ring[1] = data_trb;
    }
    
    /* Status TRB */
    xhci_trb_t status_trb;
    status_trb.param = 0;
    status_trb.status = 0;
    status_trb.control = (TRB_TYPE_STATUS << 10) | (1 << 5);  /* IOC */
    ring[data && length ? 2 : 1] = status_trb;
    
    /* Ring doorbell to start transfer */
    *(volatile uint32_t *)(ctrl->db_base + dev->address * 4) = 1;
    
    /* Wait for completion (poll status) */
    int timeout = 5000;
    while (timeout-- > 0) {
        uint32_t event_status = *(volatile uint32_t *)(ctrl->rt_base + 0x28);
        if (event_status & 0x01) {  /* Event pending */
            /* Clear event */
            *(volatile uint32_t *)(ctrl->rt_base + 0x28) = event_status;
            return 0;
        }
        for (volatile int i = 0; i < 100; i++) {}
    }
    
    return -1;  /* Timeout */
}

int usb_bulk_transfer(usb_device_t *dev, usb_endpoint_t *ep,
                      void *data, uint32_t length, int direction) {
    if (!dev || !ep || !data || !dev->controller) return -1;
    
    usb_controller_t *ctrl = dev->controller;
    
    /* Build Normal TRB for bulk transfer */
    xhci_trb_t bulk_trb;
    bulk_trb.param = (uint64_t)(uintptr_t)data;
    bulk_trb.status = length;
    bulk_trb.control = (TRB_TYPE_NORMAL << 10) | (1 << 5);  /* IOC */
    
    /* Write to endpoint's transfer ring */
    uint8_t ep_idx = (ep->address & 0x0F) * 2 + (direction ? 1 : 0);
    volatile xhci_trb_t *ring = (volatile xhci_trb_t *)(ctrl->op_base + 0x2000 + 
                                 dev->address * 0x200 + ep_idx * 0x20);
    ring[0] = bulk_trb;
    
    /* Ring doorbell for this endpoint */
    *(volatile uint32_t *)(ctrl->db_base + dev->address * 4) = ep_idx + 1;
    
    /* Wait for completion */
    int timeout = 10000;
    while (timeout-- > 0) {
        uint32_t event_status = *(volatile uint32_t *)(ctrl->rt_base + 0x28);
        if (event_status & 0x01) {
            *(volatile uint32_t *)(ctrl->rt_base + 0x28) = event_status;
            return length;
        }
        for (volatile int i = 0; i < 100; i++) {}
    }
    
    return -1;
}

int usb_interrupt_transfer(usb_device_t *dev, usb_endpoint_t *ep,
                           void *data, uint32_t length) {
    if (!dev || !ep || !data || !dev->controller) return -1;
    
    /* Interrupt transfers use same mechanism as bulk */
    return usb_bulk_transfer(dev, ep, data, length, ep->address & 0x80 ? 1 : 0);
}

int usb_get_device_descriptor(usb_device_t *dev) {
    return usb_control_transfer(dev, 0x80, USB_REQ_GET_DESCRIPTOR,
                                USB_DESC_DEVICE << 8, 0, &dev->desc, 18);
}

int usb_set_config(usb_device_t *dev, uint8_t config) {
    return usb_control_transfer(dev, 0x00, USB_REQ_SET_CONFIG, config, 0, NULL, 0);
}

int usb_reset_device(usb_device_t *dev) {
    if (!dev || !dev->controller) return -1;
    
    usb_controller_t *ctrl = dev->controller;
    
    /* Read PORTSC for this device's port */
    uint32_t portsc_offset = 0x400 + (0x10 * dev->port);
    uint32_t portsc = xhci_op_read32(ctrl, portsc_offset);
    
    /* Set Port Reset bit (bit 4) */
    xhci_op_write32(ctrl, portsc_offset, portsc | (1 << 4));
    
    /* Wait for reset to complete */
    int timeout = 1000;
    while (timeout-- > 0) {
        portsc = xhci_op_read32(ctrl, portsc_offset);
        if (!(portsc & (1 << 4))) {
            serial_puts("[USB] Device reset complete\n");
            return 0;
        }
        for (volatile int i = 0; i < 1000; i++) {}
    }
    
    serial_puts("[USB] Device reset timeout\n");
    return -1;
}
