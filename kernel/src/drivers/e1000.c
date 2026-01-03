/*
 * NeolyxOS Intel e1000 Ethernet Driver Implementation
 * 
 * Supports Intel 8254x Gigabit Ethernet (QEMU/VirtualBox/VMware).
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "drivers/e1000.h"
#include "net/network.h"
#include "mm/kheap.h"
#include "mm/pmm.h"

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
    uint32_t addr = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

/* ============ State ============ */

static e1000_device_t devices[4];
static int device_count = 0;

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

/* ============ Register Access ============ */

static uint32_t e1000_read(e1000_device_t *dev, uint16_t reg) {
    return *(volatile uint32_t *)(dev->mmio_base + reg);
}

static void e1000_write(e1000_device_t *dev, uint16_t reg, uint32_t val) {
    *(volatile uint32_t *)(dev->mmio_base + reg) = val;
}

/* ============ EEPROM Access ============ */

static uint16_t e1000_eeprom_read(e1000_device_t *dev, uint8_t addr) {
    uint32_t val;
    
    e1000_write(dev, E1000_EERD, (1) | ((uint32_t)addr << 8));
    
    /* Wait for read complete */
    int timeout = 10000;
    while (timeout--) {
        val = e1000_read(dev, E1000_EERD);
        if (val & 0x10) {  /* Done bit */
            return (uint16_t)(val >> 16);
        }
    }
    
    return 0;
}

/* ============ MAC Address ============ */

static void e1000_read_mac(e1000_device_t *dev) {
    /* Try EEPROM first */
    uint16_t word = e1000_eeprom_read(dev, 0);
    if (word != 0 && word != 0xFFFF) {
        dev->mac.bytes[0] = word & 0xFF;
        dev->mac.bytes[1] = (word >> 8) & 0xFF;
        
        word = e1000_eeprom_read(dev, 1);
        dev->mac.bytes[2] = word & 0xFF;
        dev->mac.bytes[3] = (word >> 8) & 0xFF;
        
        word = e1000_eeprom_read(dev, 2);
        dev->mac.bytes[4] = word & 0xFF;
        dev->mac.bytes[5] = (word >> 8) & 0xFF;
    } else {
        /* Read from RAL/RAH registers */
        uint32_t ral = e1000_read(dev, E1000_RAL);
        uint32_t rah = e1000_read(dev, E1000_RAH);
        
        dev->mac.bytes[0] = ral & 0xFF;
        dev->mac.bytes[1] = (ral >> 8) & 0xFF;
        dev->mac.bytes[2] = (ral >> 16) & 0xFF;
        dev->mac.bytes[3] = (ral >> 24) & 0xFF;
        dev->mac.bytes[4] = rah & 0xFF;
        dev->mac.bytes[5] = (rah >> 8) & 0xFF;
    }
    
    serial_puts("[E1000] MAC: ");
    for (int i = 0; i < 6; i++) {
        if (i > 0) serial_putc(':');
        serial_putc("0123456789ABCDEF"[(dev->mac.bytes[i] >> 4) & 0xF]);
        serial_putc("0123456789ABCDEF"[dev->mac.bytes[i] & 0xF]);
    }
    serial_puts("\n");
}

/* ============ RX/TX Setup ============ */

static void e1000_init_rx(e1000_device_t *dev) {
    /* Allocate RX descriptors */
    dev->rx_descs = (e1000_rx_desc_t *)kmalloc_aligned(
        sizeof(e1000_rx_desc_t) * E1000_NUM_RX_DESC, 16);
    if (!dev->rx_descs) {
        serial_puts("[E1000] ERROR: Failed to allocate RX descriptors\n");
        return;
    }
    e1000_memset(dev->rx_descs, 0, sizeof(e1000_rx_desc_t) * E1000_NUM_RX_DESC);
    
    /* Allocate RX buffers */
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        dev->rx_buffers[i] = (uint8_t *)kmalloc_aligned(E1000_RX_BUF_SIZE, 16);
        if (!dev->rx_buffers[i]) {
            serial_puts("[E1000] ERROR: Failed to allocate RX buffer\n");
            return;
        }
        dev->rx_descs[i].addr = (uint64_t)(uintptr_t)dev->rx_buffers[i];
        dev->rx_descs[i].status = 0;
    }
    
    /* Set up RX descriptor ring */
    uint64_t addr = (uint64_t)(uintptr_t)dev->rx_descs;
    e1000_write(dev, E1000_RDBAL, (uint32_t)(addr & 0xFFFFFFFF));
    e1000_write(dev, E1000_RDBAH, (uint32_t)(addr >> 32));
    e1000_write(dev, E1000_RDLEN, sizeof(e1000_rx_desc_t) * E1000_NUM_RX_DESC);
    e1000_write(dev, E1000_RDH, 0);
    e1000_write(dev, E1000_RDT, E1000_NUM_RX_DESC - 1);
    
    dev->rx_cur = 0;
    
    /* Enable receiver */
    e1000_write(dev, E1000_RCTL, 
        E1000_RCTL_EN | E1000_RCTL_SBP | E1000_RCTL_UPE | 
        E1000_RCTL_MPE | E1000_RCTL_BAM | E1000_RCTL_BSIZE_2K |
        E1000_RCTL_SECRC);
}

static void e1000_init_tx(e1000_device_t *dev) {
    /* Allocate TX descriptors */
    dev->tx_descs = (e1000_tx_desc_t *)kmalloc_aligned(
        sizeof(e1000_tx_desc_t) * E1000_NUM_TX_DESC, 16);
    if (!dev->tx_descs) {
        serial_puts("[E1000] ERROR: Failed to allocate TX descriptors\n");
        return;
    }
    e1000_memset(dev->tx_descs, 0, sizeof(e1000_tx_desc_t) * E1000_NUM_TX_DESC);
    
    /* Set up TX descriptor ring */
    uint64_t addr = (uint64_t)(uintptr_t)dev->tx_descs;
    e1000_write(dev, E1000_TDBAL, (uint32_t)(addr & 0xFFFFFFFF));
    e1000_write(dev, E1000_TDBAH, (uint32_t)(addr >> 32));
    e1000_write(dev, E1000_TDLEN, sizeof(e1000_tx_desc_t) * E1000_NUM_TX_DESC);
    e1000_write(dev, E1000_TDH, 0);
    e1000_write(dev, E1000_TDT, 0);
    
    dev->tx_cur = 0;
    
    /* Enable transmitter */
    e1000_write(dev, E1000_TCTL, 
        E1000_TCTL_EN | E1000_TCTL_PSP |
        (15 << 4) |     /* Collision Threshold */
        (64 << 12));    /* Collision Distance */
}

/* ============ Device Initialization ============ */

static int e1000_init_device(e1000_device_t *dev) {
    serial_puts("[E1000] Initializing device...\n");
    
    /* Enable bus mastering */
    uint32_t cmd = pci_read(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= (1 << 2) | (1 << 1);  /* Bus Master + Memory Space */
    pci_write(dev->bus, dev->slot, dev->func, 0x04, cmd);
    
    /* Reset device */
    e1000_write(dev, E1000_CTRL, E1000_CTRL_RST);
    for (volatile int i = 0; i < 100000; i++);
    
    /* Wait for reset complete */
    while (e1000_read(dev, E1000_CTRL) & E1000_CTRL_RST);
    
    /* Set link up */
    uint32_t ctrl = e1000_read(dev, E1000_CTRL);
    ctrl |= E1000_CTRL_SLU | E1000_CTRL_ASDE;
    ctrl &= ~E1000_CTRL_LRST;
    e1000_write(dev, E1000_CTRL, ctrl);
    
    /* Read MAC address */
    e1000_read_mac(dev);
    
    /* Clear multicast table */
    for (int i = 0; i < 128; i++) {
        e1000_write(dev, E1000_MTA + i * 4, 0);
    }
    
    /* Initialize RX and TX */
    e1000_init_rx(dev);
    e1000_init_tx(dev);
    
    /* Enable interrupts */
    e1000_write(dev, E1000_IMS, E1000_ICR_RXT0 | E1000_ICR_LSC);
    
    /* Check link status */
    uint32_t status = e1000_read(dev, E1000_STATUS);
    if (status & 0x02) {
        serial_puts("[E1000] Link UP\n");
    } else {
        serial_puts("[E1000] Link DOWN\n");
    }
    
    return 0;
}

/* ============ API Implementation ============ */

int e1000_init(void) {
    serial_puts("[E1000] Scanning for Intel Ethernet...\n");
    
    device_count = 0;
    
    for (int bus = 0; bus < 256 && device_count < 4; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t vendor = pci_read(bus, slot, 0, 0);
            uint16_t vendor_id = vendor & 0xFFFF;
            uint16_t device_id = (vendor >> 16) & 0xFFFF;
            
            if (vendor_id != E1000_VENDOR_ID) continue;
            
            /* Check for supported device */
            if (device_id == E1000_DEV_82540EM ||
                device_id == E1000_DEV_82545EM ||
                device_id == E1000_DEV_82574L ||
                device_id == E1000_DEV_82579LM) {
                
                serial_puts("[E1000] Found device: ");
                serial_hex(device_id);
                serial_puts("\n");
                
                e1000_device_t *dev = &devices[device_count];
                dev->bus = bus;
                dev->slot = slot;
                dev->func = 0;
                
                /* Get BAR0 (MMIO) */
                uint32_t bar0 = pci_read(bus, slot, 0, 0x10);
                dev->mmio_base = (volatile uint8_t *)(uintptr_t)(bar0 & ~0xF);
                
                /* Get IRQ */
                dev->irq = pci_read(bus, slot, 0, 0x3C) & 0xFF;
                
                if (e1000_init_device(dev) == 0) {
                    device_count++;
                }
            }
        }
    }
    
    if (device_count == 0) {
        serial_puts("[E1000] No devices found\n");
        return -1;
    }
    
    serial_puts("[E1000] Ready (");
    serial_putc('0' + device_count);
    serial_puts(" devices)\n");
    
    return 0;
}

e1000_device_t *e1000_get_device(int index) {
    if (index >= device_count) return NULL;
    return &devices[index];
}

void e1000_get_mac(e1000_device_t *dev, mac_addr_t *mac) {
    for (int i = 0; i < 6; i++) {
        mac->bytes[i] = dev->mac.bytes[i];
    }
}

int e1000_send(e1000_device_t *dev, const void *data, uint32_t len) {
    if (!dev || !data) return -1;  /* Security: null check */
    
    if (len == 0 || len > ETH_MTU + ETH_HEADER_LEN) {
        return -1;  /* Security: bounds check */
    }
    
    uint32_t tx_idx = dev->tx_cur;
    e1000_tx_desc_t *desc = &dev->tx_descs[tx_idx];
    
    /* Wait for descriptor to be free */
    int timeout = 100000;
    while (!(desc->status & E1000_TXD_STAT_DD) && desc->addr != 0) {
        if (--timeout == 0) return -1;
    }
    
    /* Free previous buffer if any (prevent memory leak) */
    if (desc->addr != 0) {
        kfree((void *)(uintptr_t)desc->addr);
        desc->addr = 0;
    }
    
    /* Copy data to buffer */
    uint8_t *buf = (uint8_t *)kmalloc(len);
    if (!buf) return -1;  /* Security: null check */
    
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = ((const uint8_t *)data)[i];
    }
    
    desc->addr = (uint64_t)(uintptr_t)buf;
    desc->length = len;
    desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    desc->status = 0;
    
    dev->tx_cur = (tx_idx + 1) % E1000_NUM_TX_DESC;
    e1000_write(dev, E1000_TDT, dev->tx_cur);
    
    return len;
}

int e1000_receive(e1000_device_t *dev, void *buf, uint32_t max_len) {
    uint32_t rx_idx = dev->rx_cur;
    e1000_rx_desc_t *desc = &dev->rx_descs[rx_idx];
    
    if (!(desc->status & E1000_RXD_STAT_DD)) {
        return 0;  /* No packet */
    }
    
    uint32_t len = desc->length;
    if (len > max_len) len = max_len;
    
    /* Copy data */
    uint8_t *src = dev->rx_buffers[rx_idx];
    for (uint32_t i = 0; i < len; i++) {
        ((uint8_t *)buf)[i] = src[i];
    }
    
    /* Clear descriptor and return to ring */
    desc->status = 0;
    e1000_write(dev, E1000_RDT, rx_idx);
    
    dev->rx_cur = (rx_idx + 1) % E1000_NUM_RX_DESC;
    
    return len;
}

void e1000_irq_handler(void) {
    for (int i = 0; i < device_count; i++) {
        e1000_device_t *dev = &devices[i];
        uint32_t icr = e1000_read(dev, E1000_ICR);
        
        if (icr & E1000_ICR_RXT0) {
            /* Packet received - handled by polling for now */
        }
        
        if (icr & E1000_ICR_LSC) {
            /* Link status change */
            uint32_t status = e1000_read(dev, E1000_STATUS);
            if (status & 0x02) {
                serial_puts("[E1000] Link UP\n");
            } else {
                serial_puts("[E1000] Link DOWN\n");
            }
        }
    }
}
