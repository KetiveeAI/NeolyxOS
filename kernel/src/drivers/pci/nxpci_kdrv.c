/*
 * NXPCI Kernel Driver (nxpci.kdrv)
 * 
 * PCI Bus Kernel Driver Implementation
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "drivers/nxpci_kdrv.h"
#include "../src/drivers/pci/pci.h"
#include <stdint.h>

/* External kernel interfaces */
extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ Driver State ============ */

static struct {
    int  initialized;
    int  device_count;
} g_nxpci = {0};

/* ============ Helpers ============ */

static void serial_dec(int val) {
    char buf[12];
    int i = 0;
    if (val < 0) { serial_putc('-'); val = -val; }
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) serial_putc(buf[--i]);
}

static void serial_hex(uint32_t val, int digits) {
    const char hex[] = "0123456789ABCDEF";
    for (int i = (digits - 1) * 4; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

/* ============ API Implementation ============ */

int nxpci_kdrv_init(void) {
    if (g_nxpci.initialized) {
        return 0;
    }
    
    serial_puts("[NXPCI] Initializing kernel driver v" NXPCI_KDRV_VERSION "\n");
    
    /* Initialize underlying PCI subsystem */
    int result = pci_init();
    if (result < 0) {
        serial_puts("[NXPCI] PCI init failed\n");
        return -1;
    }
    
    g_nxpci.device_count = pci_get_device_count();
    g_nxpci.initialized = 1;
    
    serial_puts("[NXPCI] Found ");
    serial_dec(g_nxpci.device_count);
    serial_puts(" device(s)\n");
    
    return g_nxpci.device_count;
}

void nxpci_kdrv_shutdown(void) {
    if (!g_nxpci.initialized) return;
    
    serial_puts("[NXPCI] Shutting down...\n");
    g_nxpci.initialized = 0;
    g_nxpci.device_count = 0;
}

int nxpci_kdrv_device_count(void) {
    return g_nxpci.device_count;
}

int nxpci_kdrv_device_info(int index, nxpci_device_info_t *info) {
    if (!info) return -2;
    
    /* Get device list and iterate */
    pci_device_t *dev = pci_get_device_list();
    int i = 0;
    
    while (dev && i < index) {
        dev = dev->next;
        i++;
    }
    
    if (!dev) return -1;
    
    /* Copy info */
    info->bus = dev->bus;
    info->device = dev->device;
    info->function = dev->function;
    info->vendor_id = dev->vendor_id;
    info->device_id = dev->device_id;
    info->subsystem_vendor = dev->subsystem_vendor;
    info->subsystem_id = dev->subsystem_id;
    info->class_code = dev->class_code;
    info->subclass = dev->subclass;
    info->prog_if = dev->prog_if;
    info->revision = dev->revision;
    info->irq_line = dev->interrupt_line;
    info->irq_pin = dev->interrupt_pin;
    info->is_multifunction = (dev->header_type & 0x80) ? 1 : 0;
    info->is_bridge = (dev->class_code == 0x06) ? 1 : 0;
    
    /* Copy BARs */
    info->bar_count = dev->bar_count;
    for (int b = 0; b < dev->bar_count && b < NXPCI_MAX_BARS; b++) {
        info->bars[b].base = dev->bars[b].base;
        info->bars[b].size = dev->bars[b].size;
        info->bars[b].prefetchable = dev->bars[b].prefetchable;
        if (dev->bars[b].type == 0) {
            info->bars[b].type = NXPCI_BAR_IO;
        } else if (dev->bars[b].memory_type == 0x02) {
            info->bars[b].type = NXPCI_BAR_MEM64;
        } else {
            info->bars[b].type = NXPCI_BAR_MEM32;
        }
    }
    
    return 0;
}

int nxpci_kdrv_find_device(uint16_t vendor_id, uint16_t device_id) {
    pci_device_t *dev = pci_find_device(vendor_id, device_id);
    if (!dev) return -1;
    
    /* Find index */
    pci_device_t *list = pci_get_device_list();
    int index = 0;
    while (list && list != dev) {
        list = list->next;
        index++;
    }
    
    return index;
}

int nxpci_kdrv_find_class(uint8_t class_code, uint8_t subclass) {
    pci_device_t *dev = pci_find_class(class_code, subclass);
    if (!dev) return -1;
    
    /* Find index */
    pci_device_t *list = pci_get_device_list();
    int index = 0;
    while (list && list != dev) {
        list = list->next;
        index++;
    }
    
    return index;
}

int nxpci_kdrv_enable_busmaster(int device_index) {
    pci_device_t *dev = pci_get_device_list();
    int i = 0;
    while (dev && i < device_index) {
        dev = dev->next;
        i++;
    }
    
    if (!dev) return -1;
    return pci_enable_bus_master(dev);
}

int nxpci_kdrv_enable_memory(int device_index) {
    pci_device_t *dev = pci_get_device_list();
    int i = 0;
    while (dev && i < device_index) {
        dev = dev->next;
        i++;
    }
    
    if (!dev) return -1;
    return pci_enable_memory(dev);
}

const char *nxpci_kdrv_class_name(uint8_t class_code) {
    return pci_get_class_name(class_code);
}

uint32_t nxpci_kdrv_config_read32(int device_index, uint8_t offset) {
    pci_device_t *dev = pci_get_device_list();
    int i = 0;
    while (dev && i < device_index) { dev = dev->next; i++; }
    if (!dev) return 0xFFFFFFFF;
    
    return pci_read_config32(dev->bus, dev->device, dev->function, offset);
}

uint16_t nxpci_kdrv_config_read16(int device_index, uint8_t offset) {
    pci_device_t *dev = pci_get_device_list();
    int i = 0;
    while (dev && i < device_index) { dev = dev->next; i++; }
    if (!dev) return 0xFFFF;
    
    return pci_read_config16(dev->bus, dev->device, dev->function, offset);
}

uint8_t nxpci_kdrv_config_read8(int device_index, uint8_t offset) {
    pci_device_t *dev = pci_get_device_list();
    int i = 0;
    while (dev && i < device_index) { dev = dev->next; i++; }
    if (!dev) return 0xFF;
    
    return pci_read_config8(dev->bus, dev->device, dev->function, offset);
}

void nxpci_kdrv_config_write32(int device_index, uint8_t offset, uint32_t value) {
    pci_device_t *dev = pci_get_device_list();
    int i = 0;
    while (dev && i < device_index) { dev = dev->next; i++; }
    if (!dev) return;
    
    pci_write_config32(dev->bus, dev->device, dev->function, offset, value);
}

void nxpci_kdrv_config_write16(int device_index, uint8_t offset, uint16_t value) {
    pci_device_t *dev = pci_get_device_list();
    int i = 0;
    while (dev && i < device_index) { dev = dev->next; i++; }
    if (!dev) return;
    
    pci_write_config16(dev->bus, dev->device, dev->function, offset, value);
}

void nxpci_kdrv_config_write8(int device_index, uint8_t offset, uint8_t value) {
    pci_device_t *dev = pci_get_device_list();
    int i = 0;
    while (dev && i < device_index) { dev = dev->next; i++; }
    if (!dev) return;
    
    pci_write_config8(dev->bus, dev->device, dev->function, offset, value);
}

void nxpci_kdrv_debug(void) {
    serial_puts("\n=== NXPCI Debug Info ===\n");
    serial_puts("Version: " NXPCI_KDRV_VERSION "\n");
    serial_puts("Devices: ");
    serial_dec(g_nxpci.device_count);
    serial_puts("\n\n");
    
    pci_device_t *dev = pci_get_device_list();
    int idx = 0;
    
    while (dev) {
        serial_puts("[");
        serial_hex(dev->bus, 2);
        serial_puts(":");
        serial_hex(dev->device, 2);
        serial_puts(".");
        serial_putc('0' + dev->function);
        serial_puts("] ");
        
        serial_hex(dev->vendor_id, 4);
        serial_puts(":");
        serial_hex(dev->device_id, 4);
        serial_puts(" - ");
        serial_puts(pci_get_class_name(dev->class_code));
        serial_puts("\n");
        
        dev = dev->next;
        idx++;
    }
    
    serial_puts("========================\n\n");
}
