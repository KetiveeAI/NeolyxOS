/*
 * NXNetwork Kernel Driver (nxnet.kdrv)
 * 
 * NeolyxOS Native Network Kernel Driver
 * 
 * Supports:
 *   - Intel E1000/E1000E (real HW + QEMU)
 *   - VirtIO-Net (QEMU)
 *   - Realtek RTL8139/8169
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "drivers/nxnet_kdrv.h"
#include <stdint.h>
#include <stddef.h>

/* External kernel interfaces */
extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

/* PCI access */
extern uint32_t pci_read_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
extern void pci_write_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val);

/* ============ Constants ============ */

#define MAX_NICS            8

/* PCI Network Class */
#define PCI_CLASS_NETWORK   0x02
#define PCI_SUBCLASS_ETHER  0x00

/* Vendor/Device IDs */
#define E1000_VID           0x8086
#define E1000_DID           0x100E      /* 82540EM (QEMU) */
#define E1000E_DID          0x10D3      /* 82574L */
#define VIRTIO_VID          0x1AF4
#define VIRTIO_NET_DID      0x1000
#define RTL8139_VID         0x10EC
#define RTL8139_DID         0x8139
#define RTL8169_DID         0x8169

/* E1000 Registers */
#define E1000_CTRL          0x0000
#define E1000_STATUS        0x0008
#define E1000_EECD          0x0010
#define E1000_EERD          0x0014
#define E1000_ICR           0x00C0
#define E1000_IMS           0x00D0
#define E1000_IMC           0x00D8
#define E1000_RCTL          0x0100
#define E1000_TCTL          0x0400
#define E1000_RDBAL         0x2800
#define E1000_RDBAH         0x2804
#define E1000_RDLEN         0x2808
#define E1000_RDH           0x2810
#define E1000_RDT           0x2818
#define E1000_TDBAL         0x3800
#define E1000_TDBAH         0x3804
#define E1000_TDLEN         0x3808
#define E1000_TDH           0x3810
#define E1000_TDT           0x3818
#define E1000_RAL           0x5400
#define E1000_RAH           0x5404

/* E1000 CTRL bits */
#define E1000_CTRL_RST      (1 << 26)
#define E1000_CTRL_SLU      (1 << 6)

/* E1000 STATUS bits */
#define E1000_STATUS_LU     (1 << 1)    /* Link Up */

/* E1000 RCTL bits */
#define E1000_RCTL_EN       (1 << 1)
#define E1000_RCTL_BAM      (1 << 15)   /* Broadcast Accept */
#define E1000_RCTL_BSIZE    (3 << 16)

/* E1000 TCTL bits */
#define E1000_TCTL_EN       (1 << 1)
#define E1000_TCTL_PSP      (1 << 3)

/* ============ Driver State ============ */

typedef struct {
    int                     in_use;
    nxnet_device_info_t     info;
    volatile uint32_t      *regs;
    
    /* RX/TX descriptors */
    void                   *rx_desc;
    void                   *tx_desc;
    void                   *rx_buffers;
    void                   *tx_buffers;
} nxnet_device_t;

static struct {
    int                 initialized;
    int                 device_count;
    nxnet_device_t      devices[MAX_NICS];
} g_nxnet = {0};

/* ============ Helpers ============ */

static void serial_dec(uint32_t val) {
    char buf[12];
    int i = 0;
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) serial_putc(buf[--i]);
}

static void serial_hex8(uint8_t val) {
    const char hex[] = "0123456789ABCDEF";
    serial_putc(hex[(val >> 4) & 0xF]);
    serial_putc(hex[val & 0xF]);
}

static inline void nx_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t*)s;
    while (n--) *p++ = (uint8_t)c;
}

static inline void nx_strcpy(char *d, const char *s) {
    while ((*d++ = *s++));
}

static inline uint32_t mmio_read32(volatile uint32_t *base, uint32_t offset) {
    return *(volatile uint32_t*)((uint8_t*)base + offset);
}

static inline void mmio_write32(volatile uint32_t *base, uint32_t offset, uint32_t val) {
    *(volatile uint32_t*)((uint8_t*)base + offset) = val;
}

/* ============ E1000 Driver ============ */

static void e1000_read_mac(nxnet_device_t *dev) {
    /* Read from EEPROM or RAL/RAH */
    uint32_t ral = mmio_read32(dev->regs, E1000_RAL);
    uint32_t rah = mmio_read32(dev->regs, E1000_RAH);
    
    dev->info.mac.octets[0] = ral & 0xFF;
    dev->info.mac.octets[1] = (ral >> 8) & 0xFF;
    dev->info.mac.octets[2] = (ral >> 16) & 0xFF;
    dev->info.mac.octets[3] = (ral >> 24) & 0xFF;
    dev->info.mac.octets[4] = rah & 0xFF;
    dev->info.mac.octets[5] = (rah >> 8) & 0xFF;
}

static int e1000_init(nxnet_device_t *dev) {
    volatile uint32_t *regs = dev->regs;
    
    /* Reset */
    mmio_write32(regs, E1000_CTRL, E1000_CTRL_RST);
    for (volatile int i = 0; i < 100000; i++);
    
    /* Disable interrupts */
    mmio_write32(regs, E1000_IMC, 0xFFFFFFFF);
    
    /* Set link up */
    uint32_t ctrl = mmio_read32(regs, E1000_CTRL);
    ctrl |= E1000_CTRL_SLU;
    mmio_write32(regs, E1000_CTRL, ctrl);
    
    /* Read MAC address */
    e1000_read_mac(dev);
    
    /* Check link status */
    uint32_t status = mmio_read32(regs, E1000_STATUS);
    dev->info.link = (status & E1000_STATUS_LU) ? NXNET_LINK_UP : NXNET_LINK_DOWN;
    dev->info.speed = NXNET_SPEED_1000;  /* E1000 is Gigabit */
    
    serial_puts("[NXNet] E1000 MAC: ");
    for (int i = 0; i < 6; i++) {
        if (i > 0) serial_putc(':');
        serial_hex8(dev->info.mac.octets[i]);
    }
    serial_puts(dev->info.link == NXNET_LINK_UP ? " (Link UP)\n" : " (Link DOWN)\n");
    
    return 0;
}

/* ============ Device Detection ============ */

static int detect_network_devices(void) {
    int found = 0;
    
    for (int bus = 0; bus < 256 && found < MAX_NICS; bus++) {
        for (int dev = 0; dev < 32 && found < MAX_NICS; dev++) {
            for (int func = 0; func < 8 && found < MAX_NICS; func++) {
                uint32_t id = pci_read_config32(bus, dev, func, 0x00);
                if (id == 0xFFFFFFFF) continue;
                
                uint32_t class_rev = pci_read_config32(bus, dev, func, 0x08);
                uint8_t class_code = (class_rev >> 24) & 0xFF;
                uint8_t subclass = (class_rev >> 16) & 0xFF;
                
                if (class_code != PCI_CLASS_NETWORK || subclass != PCI_SUBCLASS_ETHER) continue;
                
                uint16_t vendor = id & 0xFFFF;
                uint16_t device_id = (id >> 16) & 0xFFFF;
                
                nxnet_device_t *d = &g_nxnet.devices[found];
                d->in_use = 1;
                d->info.id = found;
                d->info.pci_bus = bus;
                d->info.pci_dev = dev;
                d->info.pci_func = func;
                
                /* Get BAR0 */
                uint32_t bar0 = pci_read_config32(bus, dev, func, 0x10);
                d->info.mmio_base = bar0 & 0xFFFFFFF0;
                d->regs = (volatile uint32_t*)d->info.mmio_base;
                
                /* Get IRQ */
                uint32_t irq_pin = pci_read_config32(bus, dev, func, 0x3C);
                d->info.irq = irq_pin & 0xFF;
                
                /* Identify device */
                if (vendor == E1000_VID && (device_id == E1000_DID || device_id == E1000E_DID)) {
                    d->info.type = NXNET_NIC_E1000;
                    nx_strcpy(d->info.vendor, "Intel");
                    nx_strcpy(d->info.name, "Intel E1000");
                } else if (vendor == VIRTIO_VID && device_id == VIRTIO_NET_DID) {
                    d->info.type = NXNET_NIC_VIRTIO;
                    nx_strcpy(d->info.vendor, "QEMU");
                    nx_strcpy(d->info.name, "VirtIO-Net");
                } else if (vendor == RTL8139_VID) {
                    if (device_id == RTL8139_DID) {
                        d->info.type = NXNET_NIC_RTL8139;
                        nx_strcpy(d->info.name, "Realtek RTL8139");
                    } else if (device_id == RTL8169_DID) {
                        d->info.type = NXNET_NIC_RTL8169;
                        nx_strcpy(d->info.name, "Realtek RTL8169");
                    }
                    nx_strcpy(d->info.vendor, "Realtek");
                } else {
                    d->info.type = NXNET_NIC_E1000;  /* Fallback */
                    nx_strcpy(d->info.vendor, "Unknown");
                    nx_strcpy(d->info.name, "Ethernet");
                }
                
                /* Enable bus master and memory */
                uint32_t cmd = pci_read_config32(bus, dev, func, 0x04);
                cmd |= 0x07;  /* IO + Memory + Bus Master */
                pci_write_config32(bus, dev, func, 0x04, cmd);
                
                serial_puts("[NXNet] Found ");
                serial_puts(d->info.name);
                serial_puts(" @ PCI ");
                serial_dec(bus);
                serial_putc(':');
                serial_dec(dev);
                serial_putc('.');
                serial_dec(func);
                serial_puts(", IRQ ");
                serial_dec(d->info.irq);
                serial_puts("\n");
                
                /* Initialize device */
                if (d->info.type == NXNET_NIC_E1000) {
                    e1000_init(d);
                }
                
                found++;
            }
        }
    }
    
    return found;
}

/* ============ Public API ============ */

int nxnet_kdrv_init(void) {
    if (g_nxnet.initialized) return 0;
    
    serial_puts("[NXNet] Initializing kernel driver v" NXNET_KDRV_VERSION "\n");
    
    nx_memset(&g_nxnet, 0, sizeof(g_nxnet));
    
    g_nxnet.device_count = detect_network_devices();
    
    if (g_nxnet.device_count == 0) {
        serial_puts("[NXNet] No network devices found\n");
        return -1;
    }
    
    serial_puts("[NXNet] Found ");
    serial_dec(g_nxnet.device_count);
    serial_puts(" network device(s)\n");
    
    g_nxnet.initialized = 1;
    return 0;
}

void nxnet_kdrv_shutdown(void) {
    if (!g_nxnet.initialized) return;
    g_nxnet.initialized = 0;
    serial_puts("[NXNet] Shutdown complete\n");
}

int nxnet_kdrv_device_count(void) {
    return g_nxnet.device_count;
}

int nxnet_kdrv_device_info(int index, nxnet_device_info_t *info) {
    if (index < 0 || index >= g_nxnet.device_count || !info) return -1;
    *info = g_nxnet.devices[index].info;
    return 0;
}

nxnet_link_status_t nxnet_kdrv_link_status(int device) {
    if (device < 0 || device >= g_nxnet.device_count) return NXNET_LINK_DOWN;
    return g_nxnet.devices[device].info.link;
}

int nxnet_kdrv_up(int device) {
    if (device < 0 || device >= g_nxnet.device_count) return -1;
    nxnet_device_t *dev = &g_nxnet.devices[device];
    
    if (dev->info.type == NXNET_NIC_E1000) {
        /* Enable RX and TX */
        mmio_write32(dev->regs, E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM);
        mmio_write32(dev->regs, E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP);
    }
    
    return 0;
}

int nxnet_kdrv_down(int device) {
    if (device < 0 || device >= g_nxnet.device_count) return -1;
    nxnet_device_t *dev = &g_nxnet.devices[device];
    
    if (dev->info.type == NXNET_NIC_E1000) {
        mmio_write32(dev->regs, E1000_RCTL, 0);
        mmio_write32(dev->regs, E1000_TCTL, 0);
    }
    
    return 0;
}

int nxnet_kdrv_tx(int device, const void *data, size_t len) {
    (void)device; (void)data; (void)len;
    /* TODO: Implement TX */
    return -1;
}

int nxnet_kdrv_rx(int device, void *data, size_t max_len) {
    (void)device; (void)data; (void)max_len;
    /* TODO: Implement RX */
    return -1;
}

void nxnet_kdrv_debug(void) {
    serial_puts("\n=== NXNet Debug ===\n");
    serial_puts("Devices: ");
    serial_dec(g_nxnet.device_count);
    serial_puts("\n");
    
    for (int i = 0; i < g_nxnet.device_count; i++) {
        nxnet_device_info_t *d = &g_nxnet.devices[i].info;
        serial_puts("  [");
        serial_dec(i);
        serial_puts("] ");
        serial_puts(d->name);
        serial_puts("\n      MAC: ");
        for (int j = 0; j < 6; j++) {
            if (j > 0) serial_putc(':');
            serial_hex8(d->mac.octets[j]);
        }
        serial_puts(d->link == NXNET_LINK_UP ? " UP" : " DOWN");
        serial_puts("\n      TX: ");
        serial_dec((uint32_t)d->tx_packets);
        serial_puts(" RX: ");
        serial_dec((uint32_t)d->rx_packets);
        serial_puts("\n");
    }
    serial_puts("===================\n\n");
}
