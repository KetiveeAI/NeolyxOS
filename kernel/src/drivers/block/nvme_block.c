/*
 * NeolyxOS NVMe Block Backend
 * 
 * Registers NVMe namespaces as block devices.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "block.h"
#include "../storage/nvme.h"
#include "../serial.h"

/* ============ NVMe Backend Private Data ============ */

typedef struct {
    nvme_controller_t *ctrl;
    uint32_t nsid;
} nvme_block_private_t;

/* Pre-allocated private data for NVMe devices */
static nvme_block_private_t nvme_private[16];
static int nvme_private_count = 0;

/* Pre-allocated block device structures */
static block_device_t nvme_block_devices[16];
static int nvme_block_count = 0;

/* ============ String Helper ============ */

static void nvme_block_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void nvme_block_set_name(char *name, int ctrl_idx, int ns_idx) {
    /* Generate name like "nvme0n1" */
    name[0] = 'n';
    name[1] = 'v';
    name[2] = 'm';
    name[3] = 'e';
    name[4] = '0' + ctrl_idx;
    name[5] = 'n';
    name[6] = '0' + ns_idx;
    name[7] = '\0';
}

/* ============ Block Operations ============ */

static int nvme_block_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buffer) {
    if (!dev || !dev->private_data) return -1;
    
    nvme_block_private_t *priv = (nvme_block_private_t*)dev->private_data;
    return nvme_read(priv->ctrl, priv->nsid, lba, count, buffer);
}

static int nvme_block_write(block_device_t *dev, uint64_t lba, uint32_t count, const void *buffer) {
    if (!dev || !dev->private_data) return -1;
    
    nvme_block_private_t *priv = (nvme_block_private_t*)dev->private_data;
    return nvme_write(priv->ctrl, priv->nsid, lba, count, buffer);
}

static int nvme_block_flush(block_device_t *dev) {
    /* NVMe flush command - not yet implemented */
    (void)dev;
    return 0;
}

static uint64_t nvme_block_capacity(block_device_t *dev) {
    if (!dev || !dev->private_data) return 0;
    
    nvme_block_private_t *priv = (nvme_block_private_t*)dev->private_data;
    return nvme_get_capacity(priv->ctrl, priv->nsid);
}

/* Block operations table */
static block_ops_t nvme_block_ops = {
    .read = nvme_block_read,
    .write = nvme_block_write,
    .flush = nvme_block_flush,
    .get_capacity = nvme_block_capacity,
};

/* ============ NVMe Block Registration ============ */

extern void serial_puts(const char *s);

/**
 * Register all NVMe namespaces as block devices.
 * Call this after nvme_init() and block_init().
 */
int nvme_block_init(void) {
    serial_puts("[NVME-BLOCK] Registering NVMe devices...\r\n");
    
    int registered = 0;
    int num_controllers = nvme_get_controller_count();
    
    for (int c = 0; c < num_controllers && nvme_block_count < 16; c++) {
        nvme_controller_t *ctrl = nvme_get_controller(c);
        if (!ctrl) continue;
        
        /* Register each namespace as a block device */
        for (uint32_t ns = 1; ns <= ctrl->namespace_count && nvme_block_count < 16; ns++) {
            nvme_namespace_t *namespace = &ctrl->namespaces[ns - 1];
            if (!namespace->active) continue;
            
            /* Set up private data */
            nvme_block_private_t *priv = &nvme_private[nvme_private_count++];
            priv->ctrl = ctrl;
            priv->nsid = ns;
            
            /* Set up block device */
            block_device_t *dev = &nvme_block_devices[nvme_block_count++];
            nvme_block_set_name(dev->name, c, ns);
            dev->type = BLOCK_TYPE_NVME;
            dev->flags = 0;
            dev->total_blocks = namespace->blocks;
            dev->block_size = namespace->block_size;
            dev->ops = &nvme_block_ops;
            dev->private_data = priv;
            
            /* Register with block subsystem */
            if (block_register(dev) >= 0) {
                registered++;
            }
        }
    }
    
    serial_puts("[NVME-BLOCK] Registered ");
    char buf[4];
    buf[0] = '0' + registered;
    buf[1] = '\0';
    serial_puts(buf);
    serial_puts(" device(s)\r\n");
    
    return registered;
}
