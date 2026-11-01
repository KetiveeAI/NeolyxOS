/*
 * NeolyxOS Intel e1000 Ethernet Driver Implementation
 *
 * Supports Intel 8254x Gigabit Ethernet (QEMU/VirtualBox/VMware).
 *
 * Model:
 *   - Polled: the network service thread (net/network.c) drains the RX
 *     ring and refreshes the link state. Device interrupts are masked, so
 *     the shared PIC line (IRQ 11 on QEMU) never fires for this device.
 *   - DMA memory comes from the kernel heap, which is identity mapped
 *     (virtual == physical) below 4 GB, so heap addresses are bus addresses.
 *   - Every RX and TX buffer is allocated once at init; send and receive
 *     never allocate.
 *
 * Copyright (c) 2025 KetiveeAI.
 */

#include "drivers/e1000.h"
#include "net/network.h"
#include "mm/kheap.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ PCI Access ============ */

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                    ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                    ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

/* ============ State ============ */

#define E1000_MAX_DEVICES 4

static e1000_device_t devices[E1000_MAX_DEVICES];
static int device_count = 0;

/* The kernel maps physical memory 0-4 GB only (see paging_init). */
#define E1000_MMIO_LIMIT  0x100000000ULL
#define E1000_MMIO_SIZE   0x20000ULL     /* 128 KB register window */

/* ============ Helpers ============ */

static void *e1000_memset(void *s, int c, uint32_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static void serial_hex(uint32_t val) {
    const char *hex = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

static void serial_dec(uint32_t v) {
    char buf[12];
    int i = 0;
    if (v == 0) { serial_putc('0'); return; }
    while (v && i < 11) { buf[i++] = (char)('0' + v % 10); v /= 10; }
    while (i--) serial_putc(buf[i]);
}

/*
 * Allocate DMA memory from the kernel heap, aligned to `align` (a power of
 * two). Never freed: rings and buffers live as long as the device. The
 * address is checked to be below 4 GB because the device is given its
 * 64-bit bus address and the kernel only maps that range.
 */
static void *e1000_dma_alloc(uint32_t size, uint32_t align) {
    void *raw = kmalloc((size_t)size + align);
    if (!raw) return NULL;
    uint64_t addr = ((uint64_t)(uintptr_t)raw + align - 1) & ~((uint64_t)align - 1);
    if (addr + size > E1000_MMIO_LIMIT) {
        kfree(raw);
        return NULL;
    }
    return (void *)(uintptr_t)addr;
}

/* ============ Register Access ============ */

static uint32_t e1000_read(e1000_device_t *dev, uint16_t reg) {
    return *(volatile uint32_t *)(dev->mmio_base + reg);
}

static void e1000_write(e1000_device_t *dev, uint16_t reg, uint32_t val) {
    *(volatile uint32_t *)(dev->mmio_base + reg) = val;
}

/* ============ EEPROM Access ============ */

/* Returns 0 and the word in *out, or -1 if the EEPROM did not answer. */
static int e1000_eeprom_read(e1000_device_t *dev, uint8_t addr, uint16_t *out) {
    e1000_write(dev, E1000_EERD, 1u | ((uint32_t)addr << 8));

    for (int timeout = 0; timeout < 100000; timeout++) {
        uint32_t val = e1000_read(dev, E1000_EERD);
        if (val & 0x10) {           /* Done bit (8254x layout) */
            *out = (uint16_t)(val >> 16);
            return 0;
        }
        __asm__ volatile("pause");
    }
    return -1;
}

/* ============ MAC Address ============ */

static int mac_is_valid(const mac_addr_t *mac) {
    int all_zero = 1, all_ff = 1;
    for (int i = 0; i < 6; i++) {
        if (mac->bytes[i] != 0x00) all_zero = 0;
        if (mac->bytes[i] != 0xFF) all_ff = 0;
    }
    /* Bit 0 of the first byte marks a multicast address */
    return !all_zero && !all_ff && !(mac->bytes[0] & 0x01);
}

static int e1000_read_mac(e1000_device_t *dev) {
    uint16_t w0, w1, w2;

    if (e1000_eeprom_read(dev, 0, &w0) == 0 &&
        e1000_eeprom_read(dev, 1, &w1) == 0 &&
        e1000_eeprom_read(dev, 2, &w2) == 0) {
        dev->mac.bytes[0] = w0 & 0xFF;
        dev->mac.bytes[1] = (w0 >> 8) & 0xFF;
        dev->mac.bytes[2] = w1 & 0xFF;
        dev->mac.bytes[3] = (w1 >> 8) & 0xFF;
        dev->mac.bytes[4] = w2 & 0xFF;
        dev->mac.bytes[5] = (w2 >> 8) & 0xFF;
    }

    if (!mac_is_valid(&dev->mac)) {
        /* Fall back to the receive address the firmware programmed */
        uint32_t ral = e1000_read(dev, E1000_RAL);
        uint32_t rah = e1000_read(dev, E1000_RAH);
        dev->mac.bytes[0] = ral & 0xFF;
        dev->mac.bytes[1] = (ral >> 8) & 0xFF;
        dev->mac.bytes[2] = (ral >> 16) & 0xFF;
        dev->mac.bytes[3] = (ral >> 24) & 0xFF;
        dev->mac.bytes[4] = rah & 0xFF;
        dev->mac.bytes[5] = (rah >> 8) & 0xFF;
    }

    if (!mac_is_valid(&dev->mac)) {
        serial_puts("[E1000] ERROR: no valid MAC address in EEPROM or RAL/RAH\n");
        return -1;
    }

    serial_puts("[E1000] MAC: ");
    for (int i = 0; i < 6; i++) {
        if (i > 0) serial_putc(':');
        serial_putc("0123456789ABCDEF"[(dev->mac.bytes[i] >> 4) & 0xF]);
        serial_putc("0123456789ABCDEF"[dev->mac.bytes[i] & 0xF]);
    }
    serial_puts("\n");

    /* Program receive address 0 so unicast frames to us pass the filter */
    uint32_t ral = (uint32_t)dev->mac.bytes[0] | ((uint32_t)dev->mac.bytes[1] << 8) |
                   ((uint32_t)dev->mac.bytes[2] << 16) | ((uint32_t)dev->mac.bytes[3] << 24);
    uint32_t rah = (uint32_t)dev->mac.bytes[4] | ((uint32_t)dev->mac.bytes[5] << 8) |
                   E1000_RAH_AV;
    e1000_write(dev, E1000_RAL, ral);
    e1000_write(dev, E1000_RAH, rah);
    return 0;
}

/* ============ RX/TX Setup ============ */

static int e1000_init_rx(e1000_device_t *dev) {
    uint32_t ring_bytes = (uint32_t)sizeof(e1000_rx_desc_t) * E1000_NUM_RX_DESC;

    /* RDLEN must be a multiple of 128 and the ring 16-byte aligned */
    dev->rx_descs = (e1000_rx_desc_t *)e1000_dma_alloc(ring_bytes, 128);
    if (!dev->rx_descs) {
        serial_puts("[E1000] ERROR: Failed to allocate RX descriptors\n");
        return -1;
    }
    e1000_memset(dev->rx_descs, 0, ring_bytes);

    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        dev->rx_buffers[i] = (uint8_t *)e1000_dma_alloc(E1000_RX_BUF_SIZE, 16);
        if (!dev->rx_buffers[i]) {
            serial_puts("[E1000] ERROR: Failed to allocate RX buffer\n");
            return -1;
        }
        dev->rx_descs[i].addr = (uint64_t)(uintptr_t)dev->rx_buffers[i];
        dev->rx_descs[i].status = 0;
    }

    uint64_t addr = (uint64_t)(uintptr_t)dev->rx_descs;
    e1000_write(dev, E1000_RDBAL, (uint32_t)(addr & 0xFFFFFFFF));
    e1000_write(dev, E1000_RDBAH, (uint32_t)(addr >> 32));
    e1000_write(dev, E1000_RDLEN, ring_bytes);
    e1000_write(dev, E1000_RDH, 0);
    e1000_write(dev, E1000_RDT, E1000_NUM_RX_DESC - 1);
    dev->rx_cur = 0;

    /* Our unicast address (RAL/RAH) and broadcast; no promiscuous modes */
    e1000_write(dev, E1000_RCTL,
        E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_BSIZE_2K | E1000_RCTL_SECRC);
    return 0;
}

static int e1000_init_tx(e1000_device_t *dev) {
    uint32_t ring_bytes = (uint32_t)sizeof(e1000_tx_desc_t) * E1000_NUM_TX_DESC;

    dev->tx_descs = (e1000_tx_desc_t *)e1000_dma_alloc(ring_bytes, 128);
    if (!dev->tx_descs) {
        serial_puts("[E1000] ERROR: Failed to allocate TX descriptors\n");
        return -1;
    }
    e1000_memset(dev->tx_descs, 0, ring_bytes);

    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        dev->tx_buffers[i] = (uint8_t *)e1000_dma_alloc(E1000_TX_BUF_SIZE, 16);
        if (!dev->tx_buffers[i]) {
            serial_puts("[E1000] ERROR: Failed to allocate TX buffer\n");
            return -1;
        }
        dev->tx_descs[i].addr = (uint64_t)(uintptr_t)dev->tx_buffers[i];
        /* Mark every descriptor as done so the first pass finds it free */
        dev->tx_descs[i].status = E1000_TXD_STAT_DD;
    }

    uint64_t addr = (uint64_t)(uintptr_t)dev->tx_descs;
    e1000_write(dev, E1000_TDBAL, (uint32_t)(addr & 0xFFFFFFFF));
    e1000_write(dev, E1000_TDBAH, (uint32_t)(addr >> 32));
    e1000_write(dev, E1000_TDLEN, ring_bytes);
    e1000_write(dev, E1000_TDH, 0);
    e1000_write(dev, E1000_TDT, 0);
    dev->tx_cur = 0;

    /* Inter-packet gap recommended for 8254x copper: IPGT 10, IPGR1 8, IPGR2 6 */
    e1000_write(dev, E1000_TIPG, 10u | (8u << 10) | (6u << 20));

    e1000_write(dev, E1000_TCTL,
        E1000_TCTL_EN | E1000_TCTL_PSP |
        (15u << 4) |     /* Collision Threshold */
        (64u << 12));    /* Collision Distance (full duplex) */
    return 0;
}

/* ============ Device Initialization ============ */

static int e1000_init_device(e1000_device_t *dev) {
    serial_puts("[E1000] Initializing device...\n");

    /* Enable memory space and bus mastering (DMA) */
    uint32_t cmd = pci_read(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= (1u << 2) | (1u << 1);
    pci_write(dev->bus, dev->slot, dev->func, 0x04, cmd);

    /* Mask every interrupt before reset: the driver is polled */
    e1000_write(dev, E1000_IMC, 0xFFFFFFFFu);

    e1000_write(dev, E1000_CTRL, e1000_read(dev, E1000_CTRL) | E1000_CTRL_RST);
    for (volatile int i = 0; i < 100000; i++) { }

    int timeout = 1000000;
    while ((e1000_read(dev, E1000_CTRL) & E1000_CTRL_RST) && --timeout > 0) {
        __asm__ volatile("pause");
    }
    if (timeout == 0) {
        serial_puts("[E1000] ERROR: device reset did not complete\n");
        return -1;
    }

    /* Reset re-enables nothing, but clear the mask and pending causes again */
    e1000_write(dev, E1000_IMC, 0xFFFFFFFFu);
    (void)e1000_read(dev, E1000_ICR);

    uint32_t ctrl = e1000_read(dev, E1000_CTRL);
    ctrl |= E1000_CTRL_SLU | E1000_CTRL_ASDE;
    ctrl &= ~E1000_CTRL_LRST;
    e1000_write(dev, E1000_CTRL, ctrl);

    if (e1000_read_mac(dev) != 0) return -1;

    for (int i = 0; i < 128; i++) {
        e1000_write(dev, (uint16_t)(E1000_MTA + i * 4), 0);
    }

    if (e1000_init_rx(dev) != 0) return -1;
    if (e1000_init_tx(dev) != 0) return -1;

    dev->link_up = 0xFF;    /* unknown: the first poll always logs */
    e1000_link_poll(dev);
    return 0;
}

/* ============ API Implementation ============ */

int e1000_init(void) {
    if (device_count > 0) {
        /* Idempotent: a second caller (e.g. a boot table) must not reset
         * a device the network stack is already using. */
        return 0;
    }

    serial_puts("[E1000] Scanning for Intel Ethernet...\n");

    for (int bus = 0; bus < 256 && device_count < E1000_MAX_DEVICES; bus++) {
        for (int slot = 0; slot < 32 && device_count < E1000_MAX_DEVICES; slot++) {
            uint32_t vendor = pci_read((uint8_t)bus, (uint8_t)slot, 0, 0);
            uint16_t vendor_id = vendor & 0xFFFF;
            uint16_t device_id = (vendor >> 16) & 0xFFFF;

            if (vendor_id != E1000_VENDOR_ID) continue;
            if (device_id != E1000_DEV_82540EM && device_id != E1000_DEV_82545EM &&
                device_id != E1000_DEV_82574L && device_id != E1000_DEV_82579LM) {
                continue;
            }

            serial_puts("[E1000] Found device: ");
            serial_hex(device_id);
            serial_puts("\n");

            e1000_device_t *dev = &devices[device_count];
            e1000_memset(dev, 0, sizeof(*dev));
            dev->bus = (uint8_t)bus;
            dev->slot = (uint8_t)slot;
            dev->func = 0;

            /* BAR0: memory BAR, 32- or 64-bit */
            uint32_t bar0 = pci_read((uint8_t)bus, (uint8_t)slot, 0, 0x10);
            if (bar0 & 0x1) {
                serial_puts("[E1000] ERROR: BAR0 is an I/O BAR, skipping device\n");
                continue;
            }
            uint64_t mmio = bar0 & ~0xFULL;
            if (((bar0 >> 1) & 0x3) == 0x2) {
                uint32_t bar1 = pci_read((uint8_t)bus, (uint8_t)slot, 0, 0x14);
                mmio |= (uint64_t)bar1 << 32;
            }
            if (mmio == 0 || mmio + E1000_MMIO_SIZE > E1000_MMIO_LIMIT) {
                serial_puts("[E1000] ERROR: BAR0 unassigned or above 4 GB (not mapped), skipping\n");
                continue;
            }
            dev->mmio_base = (volatile uint8_t *)(uintptr_t)mmio;
            dev->irq = pci_read((uint8_t)bus, (uint8_t)slot, 0, 0x3C) & 0xFF;

            if (e1000_init_device(dev) == 0) {
                device_count++;
            } else {
                serial_puts("[E1000] ERROR: device initialization failed, skipping\n");
            }
        }
    }

    if (device_count == 0) {
        serial_puts("[E1000] No devices found\n");
        return -1;
    }

    serial_puts("[E1000] Ready (");
    serial_dec((uint32_t)device_count);
    serial_puts(" devices)\n");

    return 0;
}

e1000_device_t *e1000_get_device(int index) {
    if (index < 0 || index >= device_count) return NULL;
    return &devices[index];
}

void e1000_get_mac(e1000_device_t *dev, mac_addr_t *mac) {
    if (!dev || !mac) return;
    for (int i = 0; i < 6; i++) {
        mac->bytes[i] = dev->mac.bytes[i];
    }
}

int e1000_link_poll(e1000_device_t *dev) {
    if (!dev || !dev->mmio_base) return 0;

    uint32_t status = e1000_read(dev, E1000_STATUS);
    uint8_t up = (status & E1000_STATUS_LU) ? 1 : 0;
    uint32_t sp = (status & E1000_STATUS_SPEED_MASK) >> 6;
    dev->speed_mbps = up ? (sp == 0 ? 10 : (sp == 1 ? 100 : 1000)) : 0;
    dev->link_changed = 0;

    if (up != dev->link_up) {
        dev->link_up = up;
        if (up) {
            serial_puts("[E1000] Link UP (");
            serial_dec(dev->speed_mbps);
            serial_puts(" Mb/s)\n");
        } else {
            serial_puts("[E1000] Link DOWN\n");
        }
    }
    return up;
}

int e1000_send(e1000_device_t *dev, const void *data, uint32_t len) {
    if (!dev || !data || !dev->tx_descs) return -1;

    if (len == 0 || len > ETH_MTU + ETH_HEADER_LEN || len > E1000_TX_BUF_SIZE) {
        return -1;
    }

    uint32_t tx_idx = dev->tx_cur;
    e1000_tx_desc_t *desc = &dev->tx_descs[tx_idx];

    /* The device writes DD back once it has fetched the frame */
    int timeout = 100000;
    while (!(*(volatile uint8_t *)&desc->status & E1000_TXD_STAT_DD)) {
        if (--timeout == 0) {
            dev->tx_ring_full++;
            return -1;
        }
        __asm__ volatile("pause");
    }

    uint8_t *buf = dev->tx_buffers[tx_idx];
    const uint8_t *src = (const uint8_t *)data;
    for (uint32_t i = 0; i < len; i++) buf[i] = src[i];

    desc->addr = (uint64_t)(uintptr_t)buf;
    desc->length = (uint16_t)len;
    desc->cso = 0;
    desc->css = 0;
    desc->special = 0;
    desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    desc->status = 0;

    __asm__ volatile("sfence" ::: "memory");
    dev->tx_cur = (tx_idx + 1) % E1000_NUM_TX_DESC;
    e1000_write(dev, E1000_TDT, dev->tx_cur);

    return (int)len;
}

int e1000_receive(e1000_device_t *dev, void *buf, uint32_t max_len) {
    if (!dev || !buf || !dev->rx_descs) return -1;

    for (;;) {
        uint32_t rx_idx = dev->rx_cur;
        e1000_rx_desc_t *desc = &dev->rx_descs[rx_idx];
        uint8_t status = *(volatile uint8_t *)&desc->status;

        if (!(status & E1000_RXD_STAT_DD)) {
            return 0;  /* No packet */
        }
        __asm__ volatile("lfence" ::: "memory");

        uint32_t len = desc->length;
        int ok = (status & E1000_RXD_STAT_EOP) && !(desc->errors & E1000_RXD_ERR_MASK) &&
                 len > 0 && len <= max_len && len <= E1000_RX_BUF_SIZE;

        if (ok) {
            uint8_t *src = dev->rx_buffers[rx_idx];
            for (uint32_t i = 0; i < len; i++) {
                ((uint8_t *)buf)[i] = src[i];
            }
        } else {
            /* Bad, truncated or multi-descriptor frame: drop it whole
             * rather than hand a partial frame to the stack. */
            dev->rx_dropped++;
        }

        /* Give the descriptor back: the tail trails the next one we read */
        desc->status = 0;
        desc->errors = 0;
        e1000_write(dev, E1000_RDT, rx_idx);
        dev->rx_cur = (rx_idx + 1) % E1000_NUM_RX_DESC;

        if (ok) return (int)len;
    }
}

void e1000_irq_handler(void) {
    /* Interrupts are masked (polled driver). If the shared line fires for
     * another reason, acknowledge any cause and let the poller log it. */
    for (int i = 0; i < device_count; i++) {
        e1000_device_t *dev = &devices[i];
        uint32_t icr = e1000_read(dev, E1000_ICR);
        if (icr & E1000_ICR_LSC) {
            dev->link_changed = 1;
        }
    }
}
