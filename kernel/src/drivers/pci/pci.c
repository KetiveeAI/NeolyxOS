/*
 * NeolyxOS PCI Bus Driver
 * 
 * Production-ready PCI enumeration and management
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "pci.h"
#include "../../drivers/serial.h"

/* Kernel identity map for MMIO above 4 GB (src/arch/x86_64/paging.c) */
extern int paging_mmio_register(uint64_t phys, uint64_t size);
extern int paging_mmio_is_mapped(uint64_t phys, uint64_t size);

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

/* ============ Logging Helpers ============ */

static void pci_put_hex(uint64_t v, int digits) {
    static const char hex[] = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; i--) {
        serial_putc(hex[(v >> (i * 4)) & 0xF]);
    }
}

static void pci_put_dec(uint64_t v) {
    char buf[21];
    int i = 0;
    do { buf[i++] = (char)('0' + (v % 10)); v /= 10; } while (v && i < 20);
    while (i > 0) serial_putc(buf[--i]);
}

static void pci_put_bdf(const pci_device_t *dev) {
    pci_put_hex(dev->bus, 2);
    serial_putc(':');
    pci_put_hex(dev->device, 2);
    serial_putc('.');
    pci_put_hex(dev->function, 1);
}

/* Interrupts off around BAR sizing: while decoding is disabled, an
 * interrupt handler touching this device would read all-ones. */
static inline uint64_t pci_irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void pci_irq_restore(uint64_t flags) {
    if (flags & (1ULL << 9)) __asm__ volatile("sti" : : : "memory");
}

/* ============ BAR Parsing ============ */

/*
 * Fill dev->bars[] indexed by BAR number. Sizes are read with the
 * write-all-ones method, with I/O and memory decoding disabled for the
 * whole device while its BARs are probed, then restored.
 */
static void pci_parse_bars(pci_device_t *dev) {
    dev->bar_count = 0;
    for (int i = 0; i < PCI_MAX_BARS; i++) {
        dev->bars[i].base = 0;
        dev->bars[i].size = 0;
        dev->bars[i].type = PCI_BAR_NONE;
        dev->bars[i].memory_type = 0;
        dev->bars[i].prefetchable = 0;
        dev->bars[i].upper_half = 0;
    }

    /* Type 0 headers have six BARs, PCI-to-PCI bridges two, CardBus none */
    int nbars;
    switch (dev->header_type & 0x7F) {
        case PCI_HEADER_TYPE_NORMAL: nbars = 6; break;
        case PCI_HEADER_TYPE_BRIDGE: nbars = 2; break;
        default:                     return;
    }

    uint8_t b = dev->bus, d = dev->device, f = dev->function;
    uint64_t irq = pci_irq_save();
    uint16_t cmd = pci_read_config16(b, d, f, PCI_COMMAND);
    pci_write_config16(b, d, f, PCI_COMMAND,
                       cmd & (uint16_t)~(PCI_CMD_IO_SPACE | PCI_CMD_MEM_SPACE));

    for (int i = 0; i < nbars; i++) {
        uint8_t off = (uint8_t)(PCI_BAR0 + i * 4);
        uint32_t lo = pci_read_config32(b, d, f, off);
        pci_bar_t *bar = &dev->bars[i];

        if (lo & PCI_BAR_IO) {
            pci_write_config32(b, d, f, off, 0xFFFFFFFF);
            uint32_t mask = pci_read_config32(b, d, f, off);
            pci_write_config32(b, d, f, off, lo);
            mask &= 0xFFFFFFFC;
            if (mask == 0) continue;                    /* not implemented */
            if ((mask & 0xFFFF0000) == 0) mask |= 0xFFFF0000;  /* 16-bit decoder */
            uint64_t base = lo & 0xFFFFFFFC;
            if (base == 0) continue;                    /* not assigned */
            bar->type = PCI_BAR_IO;
            bar->base = base;
            bar->size = (uint64_t)(uint32_t)(~mask + 1);
            dev->bar_count++;
            continue;
        }

        uint8_t memtype = (uint8_t)((lo >> 1) & 0x03);
        int is64 = (memtype == PCI_BAR_MEMTYPE_64);
        if (is64 && i + 1 >= nbars) {
            /* A 64-bit BAR in the last register has no upper half */
            continue;
        }

        uint32_t hi = 0, mask_hi = 0xFFFFFFFF;
        pci_write_config32(b, d, f, off, 0xFFFFFFFF);
        uint32_t mask_lo = pci_read_config32(b, d, f, off);
        pci_write_config32(b, d, f, off, lo);
        if (is64) {
            uint8_t off_hi = (uint8_t)(off + 4);
            hi = pci_read_config32(b, d, f, off_hi);
            pci_write_config32(b, d, f, off_hi, 0xFFFFFFFF);
            mask_hi = pci_read_config32(b, d, f, off_hi);
            pci_write_config32(b, d, f, off_hi, hi);
            dev->bars[i + 1].upper_half = 1;
        }

        uint64_t mask = ((uint64_t)mask_hi << 32) | (mask_lo & 0xFFFFFFF0u);
        uint64_t base = ((uint64_t)hi << 32) | (lo & 0xFFFFFFF0u);
        if (is64) i++;                                  /* skip the upper half */
        if ((mask & 0xFFFFFFFFFFFFFFF0ULL) == 0xFFFFFFFF00000000ULL || mask == 0) {
            continue;                                   /* not implemented */
        }
        if (base == 0) continue;                        /* not assigned */

        bar->type = PCI_BAR_MEM;
        bar->memory_type = memtype;
        bar->prefetchable = (lo & PCI_BAR_PREFETCH) ? 1 : 0;
        bar->base = base;
        bar->size = ~mask + 1;
        dev->bar_count++;
    }

    pci_write_config16(b, d, f, PCI_COMMAND, cmd);
    pci_irq_restore(irq);
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

/* Buses already scanned: a bridge with a bad secondary-bus number cannot
 * send the scan round in a loop. */
static uint8_t pci_bus_seen[PCI_MAX_BUSES / 8];

/* Probe one function; if it is a PCI-to-PCI bridge, scan the bus behind it. */
static int pci_probe_and_descend(uint8_t bus, uint8_t dev, uint8_t func) {
    if (pci_probe_device(bus, dev, func) < 0) return -1;
    const pci_device_t *device = pci_device_head;      /* just added */
    if (device->class_code == PCI_CLASS_BRIDGE && device->subclass == 0x04) {
        uint8_t secondary_bus = pci_read_config8(bus, dev, func, 0x19);
        if (secondary_bus != 0 && secondary_bus != bus) {
            pci_scan_bus(secondary_bus);
        }
    }
    return 0;
}

static void pci_scan_device(uint8_t bus, uint8_t dev) {
    if (pci_probe_and_descend(bus, dev, 0) < 0) {
        return;
    }
    
    /* Check for multi-function device */
    uint8_t header = pci_read_config8(bus, dev, 0, PCI_HEADER_TYPE);
    if (header & PCI_HEADER_TYPE_MULTI) {
        for (uint8_t func = 1; func < PCI_MAX_FUNCTIONS; func++) {
            pci_probe_and_descend(bus, dev, func);
        }
    }
}

static void pci_scan_bus(uint8_t bus) {
    if (pci_bus_seen[bus / 8] & (1u << (bus % 8))) return;
    pci_bus_seen[bus / 8] |= (uint8_t)(1u << (bus % 8));
    for (uint8_t dev = 0; dev < PCI_MAX_DEVICES; dev++) {
        pci_scan_device(bus, dev);
    }
}

/* ============ Public API ============ */

int pci_init(void) {
    serial_puts("[PCI] Initializing PCI subsystem...\r\n");
    
    pci_device_count = 0;
    pci_device_head = 0;
    for (int i = 0; i < (int)sizeof(pci_bus_seen); i++) pci_bus_seen[i] = 0;
    
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
    
    /* Log devices and their BARs; register memory BARs above 4 GB so the
     * kernel page tables map them (uncached) when they are built. */
    for (pci_device_t *dev = pci_device_head; dev; dev = dev->next) {
        serial_puts("  [");
        pci_put_bdf(dev);
        serial_puts("] ");
        pci_put_hex(dev->vendor_id, 4);
        serial_putc(':');
        pci_put_hex(dev->device_id, 4);
        serial_putc(' ');
        serial_puts(pci_get_class_name(dev->class_code));
        serial_puts("\r\n");

        for (int i = 0; i < PCI_MAX_BARS; i++) {
            const pci_bar_t *bar = pci_get_bar(dev, i);
            if (!bar) continue;
            serial_puts("      BAR");
            pci_put_dec((uint64_t)i);
            if (bar->type == PCI_BAR_IO) {
                serial_puts(" IO    0x");
            } else {
                serial_puts(bar->memory_type == PCI_BAR_MEMTYPE_64 ? " MEM64 0x" : " MEM32 0x");
            }
            pci_put_hex(bar->base, bar->base > 0xFFFFFFFFULL ? 16 : 8);
            serial_puts(" size 0x");
            pci_put_hex(bar->size, bar->size > 0xFFFFFFFFULL ? 16 : 8);
            if (bar->prefetchable) serial_puts(" prefetchable");
            serial_puts("\r\n");

            if (bar->type == PCI_BAR_MEM && bar->base + bar->size > 0x100000000ULL) {
                int r = paging_mmio_register(bar->base, bar->size);
                serial_puts(r == 0 ? "[PCI] BAR above 4 GB registered for mapping: "
                                   : "[PCI] WARNING: BAR above 4 GB cannot be mapped: ");
                pci_put_bdf(dev);
                serial_puts(" BAR");
                pci_put_dec((uint64_t)i);
                serial_puts("\r\n");
            }
        }
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

const pci_bar_t *pci_get_bar(const pci_device_t *dev, int index) {
    if (!dev || index < 0 || index >= PCI_MAX_BARS) return 0;
    const pci_bar_t *bar = &dev->bars[index];
    if (bar->type != PCI_BAR_MEM && bar->type != PCI_BAR_IO) return 0;
    return bar;
}

/* Capability lists live in the first 256 bytes: at most 48 entries of 4
 * bytes fit after the 64-byte header, so more steps means a loop. */
#define PCI_CAP_MAX_STEPS 48

uint8_t pci_find_next_cap(const pci_device_t *dev, uint8_t after, uint8_t cap_id) {
    if (!dev) return 0;
    uint8_t b = dev->bus, d = dev->device, f = dev->function;

    uint16_t status = pci_read_config16(b, d, f, PCI_STATUS);
    if (status == 0xFFFF || !(status & PCI_STATUS_CAP_LIST)) return 0;

    uint8_t ptr;
    if (after) {
        if (after < 0x40 || (after & 0x03)) return 0;   /* not a capability offset */
        ptr = pci_read_config8(b, d, f, (uint8_t)(after + 1));
    } else {
        /* CardBus bridges keep the list head at 0x14 */
        uint8_t head = ((dev->header_type & 0x7F) == PCI_HEADER_TYPE_CARDBUS) ? 0x14 : PCI_CAPABILITIES;
        ptr = pci_read_config8(b, d, f, head);
    }

    for (int step = 0; step < PCI_CAP_MAX_STEPS; step++) {
        ptr &= 0xFC;
        if (ptr < 0x40) return 0;                       /* end of list or bad pointer */
        uint8_t id = pci_read_config8(b, d, f, ptr);
        if (id == 0xFF) return 0;                       /* device gone */
        if (id == cap_id) return ptr;
        ptr = pci_read_config8(b, d, f, (uint8_t)(ptr + 1));
    }
    serial_puts("[PCI] WARNING: capability list of ");
    pci_put_bdf(dev);
    serial_puts(" does not end; ignoring it\r\n");
    return 0;
}

uint8_t pci_find_cap(const pci_device_t *dev, uint8_t cap_id) {
    return pci_find_next_cap(dev, 0, cap_id);
}

int pci_bar_map(const pci_device_t *dev, int index, volatile void **out) {
    if (!out) return -1;
    *out = 0;
    const pci_bar_t *bar = pci_get_bar(dev, index);
    if (!bar || bar->type != PCI_BAR_MEM || bar->size == 0) return -1;
    if (bar->base + bar->size < bar->base) return -1;  /* wraps */

    if (!paging_mmio_is_mapped(bar->base, bar->size)) {
        serial_puts("[PCI] ERROR: ");
        pci_put_bdf(dev);
        serial_puts(" BAR");
        pci_put_dec((uint64_t)index);
        serial_puts(" at 0x");
        pci_put_hex(bar->base, 16);
        serial_puts(" is not mapped in the active page tables\r\n");
        return -2;
    }
    *out = (volatile void *)(uintptr_t)bar->base;
    return 0;
}
