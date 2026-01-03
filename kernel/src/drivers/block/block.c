/*
 * NeolyxOS Block Device Subsystem
 * 
 * Manages block devices and provides unified I/O interface.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "block.h"
#include "../serial.h"

/* ============ Device Registry ============ */

static block_device_t *devices[BLOCK_MAX_DEVICES];
static int device_count = 0;

/* ============ String Helpers ============ */

static int block_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void block_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* ============ Serial Output ============ */

extern void serial_puts(const char *s);

static void block_print_num(uint64_t n) {
    char buf[21];
    int i = 20;
    buf[i--] = '\0';
    if (n == 0) { serial_puts("0"); return; }
    while (n > 0 && i >= 0) {
        buf[i--] = '0' + (n % 10);
        n /= 10;
    }
    serial_puts(&buf[i + 1]);
}

/* ============ Block Subsystem API ============ */

void block_init(void) {
    serial_puts("[BLOCK] Initializing block subsystem...\r\n");
    
    for (int i = 0; i < BLOCK_MAX_DEVICES; i++) {
        devices[i] = 0;
    }
    device_count = 0;
    
    serial_puts("[BLOCK] Ready\r\n");
}

int block_register(block_device_t *dev) {
    if (!dev) return -1;
    
    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < BLOCK_MAX_DEVICES; i++) {
        if (!devices[i]) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        serial_puts("[BLOCK] Error: No free slots\r\n");
        return -2;
    }
    
    dev->index = slot;
    dev->flags |= BLOCK_FLAG_PRESENT;
    devices[slot] = dev;
    device_count++;
    
    serial_puts("[BLOCK] Registered: ");
    serial_puts(dev->name);
    serial_puts(" (");
    block_print_num((dev->total_blocks * dev->block_size) / (1024 * 1024));
    serial_puts(" MB)\r\n");
    
    return slot;
}

int block_unregister(int index) {
    if (index < 0 || index >= BLOCK_MAX_DEVICES) return -1;
    if (!devices[index]) return -2;
    
    serial_puts("[BLOCK] Unregistered: ");
    serial_puts(devices[index]->name);
    serial_puts("\r\n");
    
    devices[index]->flags &= ~BLOCK_FLAG_PRESENT;
    devices[index] = 0;
    device_count--;
    
    return 0;
}

block_device_t *block_get(int index) {
    if (index < 0 || index >= BLOCK_MAX_DEVICES) return 0;
    return devices[index];
}

block_device_t *block_find(const char *name) {
    if (!name) return 0;
    
    for (int i = 0; i < BLOCK_MAX_DEVICES; i++) {
        if (devices[i] && block_strcmp(devices[i]->name, name)) {
            return devices[i];
        }
    }
    return 0;
}

int block_count(void) {
    return device_count;
}

/* ============ High-Level Block I/O ============ */

int block_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buffer) {
    if (!dev || !dev->ops || !dev->ops->read || !buffer) {
        return -1;
    }
    
    if (!(dev->flags & BLOCK_FLAG_PRESENT)) {
        return -2;
    }
    
    /* Bounds check */
    if (lba + count > dev->total_blocks) {
        return -3;
    }
    
    return dev->ops->read(dev, lba, count, buffer);
}

int block_write(block_device_t *dev, uint64_t lba, uint32_t count, const void *buffer) {
    if (!dev || !dev->ops || !dev->ops->write || !buffer) {
        return -1;
    }
    
    if (!(dev->flags & BLOCK_FLAG_PRESENT)) {
        return -2;
    }
    
    if (dev->flags & BLOCK_FLAG_READONLY) {
        return -4;
    }
    
    /* Bounds check */
    if (lba + count > dev->total_blocks) {
        return -3;
    }
    
    return dev->ops->write(dev, lba, count, buffer);
}

int block_flush(block_device_t *dev) {
    if (!dev || !dev->ops) {
        return -1;
    }
    
    if (dev->ops->flush) {
        return dev->ops->flush(dev);
    }
    
    return 0;  /* No flush op = success */
}

uint64_t block_capacity(block_device_t *dev) {
    if (!dev) return 0;
    
    if (dev->ops && dev->ops->get_capacity) {
        return dev->ops->get_capacity(dev);
    }
    
    return dev->total_blocks * dev->block_size;
}
