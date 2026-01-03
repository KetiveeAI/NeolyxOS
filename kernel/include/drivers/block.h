// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.
#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>

struct block_device {
    const char *name;
    uint32_t block_size;
    uint64_t num_blocks;
    int (*read)(struct block_device *dev, uint64_t block, uint32_t count, void *buf);
    int (*write)(struct block_device *dev, uint64_t block, uint32_t count, const void *buf);
    void *private_data;
};

int block_read(struct block_device *dev, uint64_t block, uint32_t count, void *buf);
int block_write(struct block_device *dev, uint64_t block, uint32_t count, const void *buf);

#endif // BLOCK_H 