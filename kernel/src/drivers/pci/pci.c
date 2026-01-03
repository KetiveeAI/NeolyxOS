/*
 * NeolyxOS PCI Bus Driver
 * 
 * Production-ready PCI enumeration and management
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "pci.h"
#include "../../drivers/serial.h"

/* ============ PCI I/O Ports ============ */
#define PCI_CONFIG_ADDR    0xCF8
#define PCI_CONFIG_DATA    0xCFC

/* ============ Device List ============ */
#define MAX_PCI_DEVICES    128

static pci_device_t pci_devices[MAX_PCI_DEVICES];
static int pci_device_count = 0;
static pci_device_t *pci_device_head = 0;

/* ============ I/O Functions ============ */

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* ============ Configuration Space Access ============ */

static uint32_t pci_config_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    return (uint32_t)(
        (1 << 31) |                  /* Enable bit */
        ((uint32_t)bus << 16) |
        ((uint32_t)(dev & 0x1F) << 11) |
        ((uint32_t)(func & 0x07) << 8) |
        (offset & 0xFC)
    );
}

uint32_t pci_read_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDR, pci_config_addr(bus, dev, func, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read_config16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read_config32(bus, dev, func, offset & 0xFC);
    return (val >> ((offset & 2) * 8)) & 0xFFFF;
}

uint8_t pci_read_config8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read_config32(bus, dev, func, offset & 0xFC);
    return (val >> ((offset & 3) * 8)) & 0xFF;
}

void pci_write_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDR, pci_config_addr(bus, dev, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

void pci_write_config16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t val = pci_read_config32(bus, dev, func, offset & 0xFC);
    int shift = (offset & 2) * 8;
    val = (val & ~(0xFFFF << shift)) | ((uint32_t)value << shift);
    pci_write_config32(bus, dev, func, offset & 0xFC, val);
}

void pci_write_config8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint8_t value) {
    uint32_t val = pci_read_config32(bus, dev, func, offset & 0xFC);
    int shift = (offset & 3) * 8;
    val = (val & ~(0xFF << shift)) | ((uint32_t)value << shift);
    pci_write_config32(bus, dev, func, offset & 0xFC, val);
}

/* ============ BAR Parsing ============ */

static void pci_parse_bars(pci_device_t *dev) {
    dev->bar_count = 0;
    
    for (int i = 0; i < PCI_MAX_BARS && dev->bar_count < PCI_MAX_BARS; i++) {
        uint8_t offset = PCI_BAR0 + (i * 4);
        
        /* Read current BAR value */
        uint32_t bar = pci_read_config32(dev->bus, dev->device, dev->function, offset);
        
        if (bar == 0) continue;
        
        /* Disable decoding */
        uint16_t cmd = pci_read_config16(dev->bus, dev->device, dev->function, PCI_COMMAND);
        pci_write_config16(dev->bus, dev->device, dev->function, PCI_COMMAND, 
                          cmd & ~(PCI_CMD_IO_SPACE | PCI_CMD_MEM_SPACE));
        
        /* Write all 1s to determine size */
        pci_write_config32(dev->bus, dev->device, dev->function, offset, 0xFFFFFFFF);
        uint32_t size_mask = pci_read_config32(dev->bus, dev->device, dev->function, offset);
        
        /* Restore original value */
        pci_write_config32(dev->bus, dev->device, dev->function, offset, bar);
        pci_write_config16(dev->bus, dev->device, dev->function, PCI_COMMAND, cmd);
        
        if (size_mask == 0 || size_mask == 0xFFFFFFFF) continue;
        
        pci_bar_t *b = &dev->bars[dev->bar_count];
        
        if (bar & PCI_BAR_IO) {
            /* I/O BAR */
            b->type = PCI_BAR_IO;
            b->base = bar & 0xFFFFFFFC;
            b->size = ~(size_mask & 0xFFFFFFFC) + 1;
            b->prefetchable = 0;
        } else {
            /* Memory BAR */
            b->type = PCI_BAR_MEM;
            b->memory_type = (bar >> 1) & 0x03;
            b->prefetchable = (bar & PCI_BAR_PREFETCH) ? 1 : 0;
            b->base = bar & 0xFFFFFFF0;
            b->size = ~(size_mask & 0xFFFFFFF0) + 1;
            
            /* Skip next BAR if 64-bit */
            if (b->memory_type == 0x02) {
                i++;
            }
        }
        
        dev->bar_count++;
    }
}

/* ============ Device Scanning ============ */

static int pci_probe_device(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t vendor = pci_read_config16(bus, dev, func, PCI_VENDOR_ID);
    
    if (vendor == 0xFFFF || vendor == 0x0000) {
        return -1; /* No device */
    }
    
    if (pci_device_count >= MAX_PCI_DEVICES) {
        return -2; /* Device list full */
    }
    
    pci_device_t *device = &pci_devices[pci_device_count];
    
    device->bus = bus;
    device->device = dev;
    device->function = func;
    device->vendor_id = vendor;
    device->device_id = pci_read_config16(bus, dev, func, PCI_DEVICE_ID);
    device->class_code = pci_read_config8(bus, dev, func, PCI_CLASS);
    device->subclass = pci_read_config8(bus, dev, func, PCI_SUBCLASS);
    device->prog_if = pci_read_config8(bus, dev, func, PCI_PROG_IF);
    device->revision = pci_read_config8(bus, dev, func, PCI_REVISION_ID);
    device->header_type = pci_read_config8(bus, dev, func, PCI_HEADER_TYPE);
    device->interrupt_line = pci_read_config8(bus, dev, func, PCI_INTERRUPT_LINE);
    device->interrupt_pin = pci_read_config8(bus, dev, func, PCI_INTERRUPT_PIN);
    device->subsystem_vendor = pci_read_config16(bus, dev, func, PCI_SUBSYSTEM_VENDOR);
    device->subsystem_id = pci_read_config16(bus, dev, func, PCI_SUBSYSTEM_ID);
    
    /* Parse BARs */
    pci_parse_bars(device);
    
    /* Add to linked list */
    device->next = pci_device_head;
    pci_device_head = device;
    
    pci_device_count++;
    
    return 0;
}

static void pci_scan_bus(uint8_t bus);

static void pci_scan_device(uint8_t bus, uint8_t dev) {
    if (pci_probe_device(bus, dev, 0) < 0) {
        return;
    }
    
    /* Check for multi-function device */
    uint8_t header = pci_read_config8(bus, dev, 0, PCI_HEADER_TYPE);
    if (header & PCI_HEADER_TYPE_MULTI) {
        for (uint8_t func = 1; func < PCI_MAX_FUNCTIONS; func++) {
            pci_probe_device(bus, dev, func);
        }
    }
    
    /* Check for PCI-to-PCI bridge */
    pci_device_t *device = &pci_devices[pci_device_count - 1];
    if (device->class_code == PCI_CLASS_BRIDGE && device->subclass == 0x04) {
        uint8_t secondary_bus = pci_read_config8(bus, dev, 0, 0x19);
        if (secondary_bus != 0) {
            pci_scan_bus(secondary_bus);
        }
    }
}

static void pci_scan_bus(uint8_t bus) {
    for (uint8_t dev = 0; dev < PCI_MAX_DEVICES; dev++) {
        pci_scan_device(bus, dev);
    }
}

/* ============ Public API ============ */

int pci_init(void) {
    serial_puts("[PCI] Initializing PCI subsystem...\r\n");
    
    pci_device_count = 0;
    pci_device_head = 0;
    
    /* Scan all buses starting from bus 0 */
    /* Check if host bridge is multi-function */
    uint8_t header = pci_read_config8(0, 0, 0, PCI_HEADER_TYPE);
    
    if (header & PCI_HEADER_TYPE_MULTI) {
        /* Multiple host bridges */
        for (uint8_t func = 0; func < PCI_MAX_FUNCTIONS; func++) {
            if (pci_read_config16(0, 0, func, PCI_VENDOR_ID) != 0xFFFF) {
                pci_scan_bus(func);
            }
        }
    } else {
        /* Single host bridge */
        pci_scan_bus(0);
    }
    
    serial_puts("[PCI] Found ");
    /* Print count */
    char buf[8];
    int cnt = pci_device_count;
    int i = 0;
    if (cnt == 0) {
        buf[i++] = '0';
    } else {
        char tmp[8];
        int j = 0;
        while (cnt > 0) { tmp[j++] = '0' + (cnt % 10); cnt /= 10; }
        while (j > 0) buf[i++] = tmp[--j];
    }
    buf[i] = '\0';
    serial_puts(buf);
    serial_puts(" devices\r\n");
    
    /* Log devices */
    pci_device_t *dev = pci_device_head;
    while (dev) {
        serial_puts("  [");
        /* Bus */
        char hex[3];
        hex[0] = "0123456789ABCDEF"[(dev->bus >> 4) & 0xF];
        hex[1] = "0123456789ABCDEF"[dev->bus & 0xF];
        hex[2] = '\0';
        serial_puts(hex);
        serial_puts(":");
        hex[0] = "0123456789ABCDEF"[(dev->device >> 4) & 0xF];
        hex[1] = "0123456789ABCDEF"[dev->device & 0xF];
        serial_puts(hex);
        serial_puts(".");
        hex[0] = '0' + dev->function;
        hex[1] = '\0';
        serial_puts(hex);
        serial_puts("] ");
        serial_puts(pci_get_class_name(dev->class_code));
        serial_puts("\r\n");
        
        dev = dev->next;
    }
    
    serial_puts("[PCI] Initialization complete\r\n");
    return 0;
}

pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    pci_device_t *dev = pci_device_head;
    while (dev) {
        if (dev->vendor_id == vendor_id && dev->device_id == device_id) {
            return dev;
        }
        dev = dev->next;
    }
    return 0;
}

pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass) {
    pci_device_t *dev = pci_device_head;
    while (dev) {
        if (dev->class_code == class_code && dev->subclass == subclass) {
            return dev;
        }
        dev = dev->next;
    }
    return 0;
}

int pci_enable_bus_master(pci_device_t *dev) {
    if (!dev) return -1;
    
    uint16_t cmd = pci_read_config16(dev->bus, dev->device, dev->function, PCI_COMMAND);
    cmd |= PCI_CMD_BUS_MASTER;
    pci_write_config16(dev->bus, dev->device, dev->function, PCI_COMMAND, cmd);
    
    return 0;
}

int pci_enable_memory(pci_device_t *dev) {
    if (!dev) return -1;
    
    uint16_t cmd = pci_read_config16(dev->bus, dev->device, dev->function, PCI_COMMAND);
    cmd |= PCI_CMD_MEM_SPACE;
    pci_write_config16(dev->bus, dev->device, dev->function, PCI_COMMAND, cmd);
    
    return 0;
}

int pci_get_device_count(void) {
    return pci_device_count;
}

pci_device_t *pci_get_device_list(void) {
    return pci_device_head;
}

const char *pci_get_class_name(uint8_t class_code) {
    switch (class_code) {
        case PCI_CLASS_UNCLASSIFIED:  return "Unclassified";
        case PCI_CLASS_STORAGE:       return "Storage";
        case PCI_CLASS_NETWORK:       return "Network";
        case PCI_CLASS_DISPLAY:       return "Display";
        case PCI_CLASS_MULTIMEDIA:    return "Multimedia";
        case PCI_CLASS_MEMORY:        return "Memory";
        case PCI_CLASS_BRIDGE:        return "Bridge";
        case PCI_CLASS_COMMUNICATION: return "Communication";
        case PCI_CLASS_SYSTEM:        return "System";
        case PCI_CLASS_INPUT:         return "Input";
        case PCI_CLASS_DOCKING:       return "Docking";
        case PCI_CLASS_PROCESSOR:     return "Processor";
        case PCI_CLASS_SERIAL:        return "Serial Bus";
        case PCI_CLASS_WIRELESS:      return "Wireless";
        case PCI_CLASS_INTELLIGENT:   return "I/O Controller";
        case PCI_CLASS_SATELLITE:     return "Satellite";
        case PCI_CLASS_ENCRYPTION:    return "Encryption";
        case PCI_CLASS_SIGNAL:        return "Signal Processing";
        default:                      return "Unknown";
    }
}
