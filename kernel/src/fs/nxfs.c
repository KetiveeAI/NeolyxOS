/*
 * NeolyxOS NXFS Implementation
 * 
 * Native filesystem with optional AES-256 encryption.
 * Phase 1: Read-only support.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "fs/nxfs.h"
#include "mm/kheap.h"
#include "core/panic.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ Block Device Operations ============ */

static nxfs_block_ops_t *block_ops = NULL;

void nxfs_set_block_ops(nxfs_block_ops_t *ops) {
    block_ops = ops;
}

/* ============ String Helpers ============ */

static void *nxfs_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static void *nxfs_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

static int nxfs_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void nxfs_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int nxfs_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

/* ============ Block I/O ============ */

static int nxfs_read_block(void *device, uint64_t block, void *buffer) {
    if (!block_ops || !block_ops->read_block) {
        serial_puts("[NXFS] ERROR: No block device\n");
        return -1;
    }
    return block_ops->read_block(device, block, buffer);
}

static int nxfs_write_block(void *device, uint64_t block, const void *buffer) {
    if (!block_ops || !block_ops->write_block) {
        return -1;
    }
    return block_ops->write_block(device, block, buffer);
}

/* ============ Checksum ============ */

static uint32_t nxfs_checksum(const void *data, size_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0;
    for (size_t i = 0; i < size; i++) {
        crc = crc ^ p[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

/* ============ AES-256 Encryption ============ */

/* AES S-box lookup table */
static const uint8_t aes_sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

/* AES inverse S-box for decryption */
static const uint8_t aes_inv_sbox[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

/* AES round constants */
static const uint8_t aes_rcon[11] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};

/* GF(2^8) multiplication */
static uint8_t aes_gmul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    for (int i = 0; i < 8; i++) {
        if (b & 1) p ^= a;
        uint8_t hi = a & 0x80;
        a <<= 1;
        if (hi) a ^= 0x1b;
        b >>= 1;
    }
    return p;
}

/* Expand 256-bit key to 240 bytes (15 round keys) */
static void aes256_key_expand(const uint8_t *key, uint8_t *round_keys) {
    nxfs_memcpy(round_keys, key, 32);
    
    uint8_t temp[4];
    int i = 8;  /* 8 words in 256-bit key */
    
    while (i < 60) {
        nxfs_memcpy(temp, &round_keys[(i-1)*4], 4);
        
        if (i % 8 == 0) {
            uint8_t t = temp[0];
            temp[0] = aes_sbox[temp[1]] ^ aes_rcon[i/8];
            temp[1] = aes_sbox[temp[2]];
            temp[2] = aes_sbox[temp[3]];
            temp[3] = aes_sbox[t];
        } else if (i % 8 == 4) {
            for (int j = 0; j < 4; j++) temp[j] = aes_sbox[temp[j]];
        }
        
        for (int j = 0; j < 4; j++) {
            round_keys[i*4 + j] = round_keys[(i-8)*4 + j] ^ temp[j];
        }
        i++;
    }
}

/* AES-256 encrypt single 16-byte block */
static void aes256_encrypt_block(const uint8_t *round_keys, uint8_t *block) {
    uint8_t state[16];
    nxfs_memcpy(state, block, 16);
    
    /* Initial round key addition */
    for (int i = 0; i < 16; i++) state[i] ^= round_keys[i];
    
    for (int round = 1; round <= 14; round++) {
        /* SubBytes */
        for (int i = 0; i < 16; i++) state[i] = aes_sbox[state[i]];
        
        /* ShiftRows */
        uint8_t t;
        t = state[1]; state[1] = state[5]; state[5] = state[9]; state[9] = state[13]; state[13] = t;
        t = state[2]; state[2] = state[10]; state[10] = t; t = state[6]; state[6] = state[14]; state[14] = t;
        t = state[15]; state[15] = state[11]; state[11] = state[7]; state[7] = state[3]; state[3] = t;
        
        /* MixColumns (skip on final round) */
        if (round < 14) {
            for (int c = 0; c < 4; c++) {
                uint8_t a[4];
                for (int i = 0; i < 4; i++) a[i] = state[c*4 + i];
                state[c*4 + 0] = aes_gmul(a[0],2) ^ aes_gmul(a[1],3) ^ a[2] ^ a[3];
                state[c*4 + 1] = a[0] ^ aes_gmul(a[1],2) ^ aes_gmul(a[2],3) ^ a[3];
                state[c*4 + 2] = a[0] ^ a[1] ^ aes_gmul(a[2],2) ^ aes_gmul(a[3],3);
                state[c*4 + 3] = aes_gmul(a[0],3) ^ a[1] ^ a[2] ^ aes_gmul(a[3],2);
            }
        }
        
        /* AddRoundKey */
        for (int i = 0; i < 16; i++) state[i] ^= round_keys[round*16 + i];
    }
    
    nxfs_memcpy(block, state, 16);
}

/* AES-256 decrypt single 16-byte block */
static void aes256_decrypt_block(const uint8_t *round_keys, uint8_t *block) {
    uint8_t state[16];
    nxfs_memcpy(state, block, 16);
    
    /* Initial round key addition (round 14) */
    for (int i = 0; i < 16; i++) state[i] ^= round_keys[14*16 + i];
    
    for (int round = 13; round >= 0; round--) {
        /* InvShiftRows */
        uint8_t t;
        t = state[13]; state[13] = state[9]; state[9] = state[5]; state[5] = state[1]; state[1] = t;
        t = state[2]; state[2] = state[10]; state[10] = t; t = state[6]; state[6] = state[14]; state[14] = t;
        t = state[3]; state[3] = state[7]; state[7] = state[11]; state[11] = state[15]; state[15] = t;
        
        /* InvSubBytes */
        for (int i = 0; i < 16; i++) state[i] = aes_inv_sbox[state[i]];
        
        /* AddRoundKey */
        for (int i = 0; i < 16; i++) state[i] ^= round_keys[round*16 + i];
        
        /* InvMixColumns (skip on final round) */
        if (round > 0) {
            for (int c = 0; c < 4; c++) {
                uint8_t a[4];
                for (int i = 0; i < 4; i++) a[i] = state[c*4 + i];
                state[c*4 + 0] = aes_gmul(a[0],14) ^ aes_gmul(a[1],11) ^ aes_gmul(a[2],13) ^ aes_gmul(a[3],9);
                state[c*4 + 1] = aes_gmul(a[0],9) ^ aes_gmul(a[1],14) ^ aes_gmul(a[2],11) ^ aes_gmul(a[3],13);
                state[c*4 + 2] = aes_gmul(a[0],13) ^ aes_gmul(a[1],9) ^ aes_gmul(a[2],14) ^ aes_gmul(a[3],11);
                state[c*4 + 3] = aes_gmul(a[0],11) ^ aes_gmul(a[1],13) ^ aes_gmul(a[2],9) ^ aes_gmul(a[3],14);
            }
        }
    }
    
    nxfs_memcpy(block, state, 16);
}

/**
 * nxfs_encrypt_block - AES-256-CBC encrypt a data block
 */
static void nxfs_encrypt_block(const uint8_t *key, const uint8_t *iv, 
                               void *data, size_t size) {
    if (!key || !iv || !data || size == 0) return;
    
    uint8_t round_keys[240];
    aes256_key_expand(key, round_keys);
    
    uint8_t prev[16];
    nxfs_memcpy(prev, iv, 16);
    
    uint8_t *ptr = (uint8_t *)data;
    size_t blocks = size / 16;
    
    for (size_t b = 0; b < blocks; b++) {
        /* XOR with previous ciphertext (CBC) */
        for (int i = 0; i < 16; i++) ptr[i] ^= prev[i];
        
        aes256_encrypt_block(round_keys, ptr);
        
        nxfs_memcpy(prev, ptr, 16);
        ptr += 16;
    }
}

/**
 * nxfs_decrypt_block - AES-256-CBC decrypt a data block
 */
static void nxfs_decrypt_block(const uint8_t *key, const uint8_t *iv,
                               void *data, size_t size) {
    if (!key || !iv || !data || size == 0) return;
    
    uint8_t round_keys[240];
    aes256_key_expand(key, round_keys);
    
    uint8_t prev[16], curr[16];
    nxfs_memcpy(prev, iv, 16);
    
    uint8_t *ptr = (uint8_t *)data;
    size_t blocks = size / 16;
    
    for (size_t b = 0; b < blocks; b++) {
        nxfs_memcpy(curr, ptr, 16);
        
        aes256_decrypt_block(round_keys, ptr);
        
        /* XOR with previous ciphertext (CBC) */
        for (int i = 0; i < 16; i++) ptr[i] ^= prev[i];
        
        nxfs_memcpy(prev, curr, 16);
        ptr += 16;
    }
}

/* ============ Block Allocation ============ */

/**
 * nxfs_alloc_block - Allocate a free block from the bitmap
 */
static int64_t nxfs_alloc_block(nxfs_mount_t *nxfs) {
    uint8_t bitmap_block[NXFS_BLOCK_SIZE];
    uint64_t total_blocks = nxfs->superblock.total_blocks;
    uint64_t bitmap_start = nxfs->superblock.block_bitmap_block;
    
    /* Scan bitmap for free block */
    for (uint64_t block = 0; block < total_blocks; block++) {
        uint64_t bitmap_block_num = bitmap_start + (block / (NXFS_BLOCK_SIZE * 8));
        uint32_t byte_offset = (block % (NXFS_BLOCK_SIZE * 8)) / 8;
        uint8_t bit_offset = block % 8;
        
        /* Read bitmap block if needed */
        if (block % (NXFS_BLOCK_SIZE * 8) == 0) {
            if (nxfs_read_block(nxfs->device, bitmap_block_num, bitmap_block) != 0) {
                return -1;
            }
        }
        
        /* Check if block is free */
        if ((bitmap_block[byte_offset] & (1 << bit_offset)) == 0) {
            /* Mark as used */
            bitmap_block[byte_offset] |= (1 << bit_offset);
            
            /* Write bitmap back */
            if (nxfs_write_block(nxfs->device, bitmap_block_num, bitmap_block) != 0) {
                return -1;
            }
            
            /* Update superblock */
            nxfs->superblock.free_blocks--;
            
            return (int64_t)block;
        }
    }
    
    return -1;  /* No free blocks */
}

/**
 * nxfs_free_block - Free a block back to the bitmap
 */
static int nxfs_free_block(nxfs_mount_t *nxfs, uint64_t block) {
    uint8_t bitmap_block[NXFS_BLOCK_SIZE];
    uint64_t bitmap_start = nxfs->superblock.block_bitmap_block;
    uint64_t bitmap_block_num = bitmap_start + (block / (NXFS_BLOCK_SIZE * 8));
    uint32_t byte_offset = (block % (NXFS_BLOCK_SIZE * 8)) / 8;
    uint8_t bit_offset = block % 8;
    
    if (nxfs_read_block(nxfs->device, bitmap_block_num, bitmap_block) != 0) {
        return -1;
    }
    
    /* Clear bit */
    bitmap_block[byte_offset] &= ~(1 << bit_offset);
    
    if (nxfs_write_block(nxfs->device, bitmap_block_num, bitmap_block) != 0) {
        return -1;
    }
    
    nxfs->superblock.free_blocks++;
    return 0;
}

/* ============ Inode Allocation ============ */

/**
 * nxfs_alloc_inode - Allocate a free inode
 */
static int64_t nxfs_alloc_inode(nxfs_mount_t *nxfs) {
    uint8_t bitmap_block[NXFS_BLOCK_SIZE];
    uint64_t total_inodes = nxfs->superblock.total_inodes;
    uint64_t bitmap_start = nxfs->superblock.inode_bitmap_block;
    
    for (uint64_t inode = 1; inode < total_inodes; inode++) {  /* Skip inode 0 */
        uint64_t bitmap_block_num = bitmap_start + (inode / (NXFS_BLOCK_SIZE * 8));
        uint32_t byte_offset = (inode % (NXFS_BLOCK_SIZE * 8)) / 8;
        uint8_t bit_offset = inode % 8;
        
        if (inode % (NXFS_BLOCK_SIZE * 8) == 0 || inode == 1) {
            if (nxfs_read_block(nxfs->device, bitmap_block_num, bitmap_block) != 0) {
                return -1;
            }
        }
        
        if ((bitmap_block[byte_offset] & (1 << bit_offset)) == 0) {
            bitmap_block[byte_offset] |= (1 << bit_offset);
            
            if (nxfs_write_block(nxfs->device, bitmap_block_num, bitmap_block) != 0) {
                return -1;
            }
            
            nxfs->superblock.free_inodes--;
            return (int64_t)inode;
        }
    }
    
    return -1;
}

/**
 * nxfs_free_inode - Free an inode
 */
static int nxfs_free_inode(nxfs_mount_t *nxfs, uint64_t inode_num) {
    uint8_t bitmap_block[NXFS_BLOCK_SIZE];
    uint64_t bitmap_start = nxfs->superblock.inode_bitmap_block;
    uint64_t bitmap_block_num = bitmap_start + (inode_num / (NXFS_BLOCK_SIZE * 8));
    uint32_t byte_offset = (inode_num % (NXFS_BLOCK_SIZE * 8)) / 8;
    uint8_t bit_offset = inode_num % 8;
    
    if (nxfs_read_block(nxfs->device, bitmap_block_num, bitmap_block) != 0) {
        return -1;
    }
    
    bitmap_block[byte_offset] &= ~(1 << bit_offset);
    
    if (nxfs_write_block(nxfs->device, bitmap_block_num, bitmap_block) != 0) {
        return -1;
    }
    
    nxfs->superblock.free_inodes++;
    return 0;
}

/**
 * nxfs_write_inode - Write inode to disk
 */
static int nxfs_write_inode(nxfs_mount_t *nxfs, uint64_t inode_num, nxfs_inode_t *inode) {
    uint64_t inodes_per_block = NXFS_BLOCK_SIZE / sizeof(nxfs_inode_t);
    uint64_t block = nxfs->superblock.inode_table_block + (inode_num / inodes_per_block);
    uint64_t offset = (inode_num % inodes_per_block) * sizeof(nxfs_inode_t);
    
    uint8_t buffer[NXFS_BLOCK_SIZE];
    if (nxfs_read_block(nxfs->device, block, buffer) != 0) {
        return -1;
    }
    
    nxfs_memcpy(buffer + offset, inode, sizeof(nxfs_inode_t));
    
    if (nxfs->encrypted) {
        nxfs_encrypt_block(nxfs->encryption_key, nxfs->superblock.encryption_iv,
                          buffer, NXFS_BLOCK_SIZE);
    }
    
    return nxfs_write_block(nxfs->device, block, buffer);
}

/**
 * nxfs_update_superblock - Sync superblock to disk
 */
static int nxfs_update_superblock(nxfs_mount_t *nxfs) {
    nxfs->superblock.checksum = nxfs_checksum(&nxfs->superblock, 
        sizeof(nxfs_superblock_t) - sizeof(nxfs->superblock.checksum) - sizeof(nxfs->superblock.reserved));
    return nxfs_write_block(nxfs->device, 0, &nxfs->superblock);
}

/* ============ Inode Operations ============ */

static nxfs_inode_t *nxfs_read_inode(nxfs_mount_t *nxfs, uint64_t inode_num) {
    /* Calculate block containing inode */
    uint64_t inodes_per_block = NXFS_BLOCK_SIZE / sizeof(nxfs_inode_t);
    uint64_t block = nxfs->superblock.inode_table_block + (inode_num / inodes_per_block);
    uint64_t offset = (inode_num % inodes_per_block) * sizeof(nxfs_inode_t);
    
    /* Read block */
    uint8_t buffer[NXFS_BLOCK_SIZE];
    if (nxfs_read_block(nxfs->device, block, buffer) != 0) {
        return NULL;
    }
    
    /* Decrypt if encrypted */
    if (nxfs->encrypted) {
        nxfs_decrypt_block(nxfs->encryption_key, nxfs->superblock.encryption_iv,
                          buffer, NXFS_BLOCK_SIZE);
    }
    
    /* Allocate and copy inode */
    nxfs_inode_t *inode = (nxfs_inode_t *)kmalloc(sizeof(nxfs_inode_t));
    if (!inode) return NULL;
    
    nxfs_memcpy(inode, buffer + offset, sizeof(nxfs_inode_t));
    return inode;
}

/* ============ VFS Operations for NXFS ============ */

static int nxfs_vfs_open(vfs_node_t *node, uint32_t mode) {
    (void)mode;
    return VFS_OK;
}

static int nxfs_vfs_close(vfs_node_t *node) {
    return VFS_OK;
}

static int64_t nxfs_vfs_read(vfs_node_t *node, void *buffer, uint64_t offset, uint64_t size) {
    if (!node || !node->private_data) return VFS_ERR_INVALID;
    
    nxfs_inode_t *inode = (nxfs_inode_t *)node->private_data;
    nxfs_mount_t *nxfs = (nxfs_mount_t *)node->mount->private_data;
    
    /* Clamp read to file size */
    if (offset >= inode->size) return 0;
    if (offset + size > inode->size) {
        size = inode->size - offset;
    }
    
    uint8_t block_buffer[NXFS_BLOCK_SIZE];
    uint64_t bytes_read = 0;
    uint8_t *dst = (uint8_t *)buffer;
    
    while (bytes_read < size) {
        /* Calculate logical block */
        uint64_t logical_block = (offset + bytes_read) / NXFS_BLOCK_SIZE;
        uint32_t block_offset = (offset + bytes_read) % NXFS_BLOCK_SIZE;
        
        /* Find physical block from extents */
        uint64_t phys_block = 0;
        for (int i = 0; i < 12; i++) {
            if (inode->extents[i].block_count == 0) break;
            uint64_t ext_start = inode->extents[i].logical_start;
            uint64_t ext_count = inode->extents[i].block_count;
            if (logical_block >= ext_start && logical_block < ext_start + ext_count) {
                phys_block = inode->extents[i].start_block + (logical_block - ext_start);
                break;
            }
        }
        
        if (phys_block == 0) return bytes_read;
        
        /* Read block */
        if (nxfs_read_block(nxfs->device, phys_block, block_buffer) != 0) {
            return bytes_read;
        }
        
        /* Decrypt if encrypted */
        if (nxfs->encrypted) {
            nxfs_decrypt_block(nxfs->encryption_key, nxfs->superblock.encryption_iv,
                              block_buffer, NXFS_BLOCK_SIZE);
        }
        
        /* Copy to user buffer */
        uint32_t to_copy = NXFS_BLOCK_SIZE - block_offset;
        if (to_copy > size - bytes_read) {
            to_copy = size - bytes_read;
        }
        
        nxfs_memcpy(dst + bytes_read, block_buffer + block_offset, to_copy);
        bytes_read += to_copy;
    }
    
    return bytes_read;
}

static int64_t nxfs_vfs_write(vfs_node_t *node, const void *buffer, uint64_t offset, uint64_t size) {
    if (!node || !node->private_data) return VFS_ERR_INVALID;
    if (node->type == VFS_DIRECTORY) return VFS_ERR_IS_DIR;
    
    nxfs_inode_t *inode = (nxfs_inode_t *)node->private_data;
    nxfs_mount_t *nxfs = (nxfs_mount_t *)node->mount->private_data;
    
    uint8_t block_buffer[NXFS_BLOCK_SIZE];
    uint64_t bytes_written = 0;
    const uint8_t *src = (const uint8_t *)buffer;
    
    while (bytes_written < size) {
        uint64_t logical_block = (offset + bytes_written) / NXFS_BLOCK_SIZE;
        uint32_t block_offset = (offset + bytes_written) % NXFS_BLOCK_SIZE;
        
        /* Find or allocate physical block */
        uint64_t phys_block = 0;
        int extent_idx = -1;
        
        /* Search existing extents */
        for (int i = 0; i < 12; i++) {
            if (inode->extents[i].block_count == 0) {
                extent_idx = i;  /* First free extent slot */
                break;
            }
            uint64_t ext_start = inode->extents[i].logical_start;
            uint64_t ext_count = inode->extents[i].block_count;
            if (logical_block >= ext_start && logical_block < ext_start + ext_count) {
                phys_block = inode->extents[i].start_block + (logical_block - ext_start);
                break;
            }
        }
        
        /* Allocate new block if needed */
        if (phys_block == 0) {
            int64_t new_block = nxfs_alloc_block(nxfs);
            if (new_block < 0) return bytes_written > 0 ? (int64_t)bytes_written : VFS_ERR_NO_SPACE;
            
            phys_block = (uint64_t)new_block;
            
            /* Add to extent - simple append */
            if (extent_idx >= 0 && extent_idx < 12) {
                inode->extents[extent_idx].start_block = phys_block;
                inode->extents[extent_idx].block_count = 1;
                inode->extents[extent_idx].logical_start = logical_block;
            }
            inode->blocks++;
            
            /* Zero the new block */
            nxfs_memset(block_buffer, 0, NXFS_BLOCK_SIZE);
        } else {
            /* Read existing block for partial write */
            if (block_offset != 0 || (size - bytes_written) < NXFS_BLOCK_SIZE) {
                if (nxfs_read_block(nxfs->device, phys_block, block_buffer) != 0) {
                    return bytes_written > 0 ? (int64_t)bytes_written : VFS_ERR_IO;
                }
                if (nxfs->encrypted) {
                    nxfs_decrypt_block(nxfs->encryption_key, nxfs->superblock.encryption_iv,
                                      block_buffer, NXFS_BLOCK_SIZE);
                }
            }
        }
        
        /* Copy data to block buffer */
        uint32_t to_copy = NXFS_BLOCK_SIZE - block_offset;
        if (to_copy > size - bytes_written) to_copy = size - bytes_written;
        nxfs_memcpy(block_buffer + block_offset, src + bytes_written, to_copy);
        
        /* Encrypt if needed */
        if (nxfs->encrypted) {
            nxfs_encrypt_block(nxfs->encryption_key, nxfs->superblock.encryption_iv,
                              block_buffer, NXFS_BLOCK_SIZE);
        }
        
        /* Write block */
        if (nxfs_write_block(nxfs->device, phys_block, block_buffer) != 0) {
            return bytes_written > 0 ? (int64_t)bytes_written : VFS_ERR_IO;
        }
        
        bytes_written += to_copy;
    }
    
    /* Update inode size if we extended the file */
    if (offset + bytes_written > inode->size) {
        inode->size = offset + bytes_written;
        node->size = inode->size;
    }
    
    /* Write inode back */
    nxfs_write_inode(nxfs, node->inode, inode);
    
    return bytes_written;
}

/* Forward declarations */
static vfs_node_t *nxfs_vfs_finddir(vfs_node_t *dir, const char *name);
static int nxfs_vfs_readdir(vfs_node_t *dir, vfs_dirent_t *entries, uint32_t count, uint32_t *read);
static int nxfs_vfs_create(vfs_node_t *parent, const char *name, vfs_type_t type, uint32_t perms);
static int nxfs_vfs_unlink(vfs_node_t *parent, const char *name);
static int nxfs_vfs_stat(vfs_node_t *node);
static int nxfs_vfs_truncate(vfs_node_t *node, uint64_t size);
static int nxfs_vfs_rename(vfs_node_t *old_parent, const char *old_name,
                           vfs_node_t *new_parent, const char *new_name);
static int nxfs_vfs_chmod(vfs_node_t *node, uint32_t mode);
static int nxfs_vfs_chown(vfs_node_t *node, uint32_t uid, uint32_t gid);

static vfs_operations_t nxfs_ops = {
    .open = nxfs_vfs_open,
    .close = nxfs_vfs_close,
    .read = nxfs_vfs_read,
    .write = nxfs_vfs_write,
    .readdir = nxfs_vfs_readdir,
    .finddir = nxfs_vfs_finddir,
    .create = nxfs_vfs_create,
    .unlink = nxfs_vfs_unlink,
    .rename = nxfs_vfs_rename,
    .truncate = nxfs_vfs_truncate,
    .stat = nxfs_vfs_stat,
    .chmod = nxfs_vfs_chmod,
    .chown = nxfs_vfs_chown,
};

/* ============ Directory Operations ============ */

static int nxfs_vfs_readdir(vfs_node_t *dir, vfs_dirent_t *entries, 
                            uint32_t count, uint32_t *read) {
    if (!dir || dir->type != VFS_DIRECTORY) return VFS_ERR_NOT_DIR;
    
    nxfs_inode_t *inode = (nxfs_inode_t *)dir->private_data;
    nxfs_mount_t *nxfs = (nxfs_mount_t *)dir->mount->private_data;
    
    uint8_t block_buffer[NXFS_BLOCK_SIZE];
    *read = 0;
    
    /* Read directory blocks */
    uint64_t offset = 0;
    while (offset < inode->size && *read < count) {
        uint64_t logical_block = offset / NXFS_BLOCK_SIZE;
        
        /* Find physical block */
        uint64_t phys_block = 0;
        for (int i = 0; i < 12; i++) {
            if (inode->extents[i].block_count == 0) break;
            uint64_t ext_start = inode->extents[i].logical_start;
            uint64_t ext_count = inode->extents[i].block_count;
            if (logical_block >= ext_start && logical_block < ext_start + ext_count) {
                phys_block = inode->extents[i].start_block + (logical_block - ext_start);
                break;
            }
        }
        
        if (phys_block == 0) break;
        
        if (nxfs_read_block(nxfs->device, phys_block, block_buffer) != 0) {
            break;
        }
        
        if (nxfs->encrypted) {
            nxfs_decrypt_block(nxfs->encryption_key, nxfs->superblock.encryption_iv,
                              block_buffer, NXFS_BLOCK_SIZE);
        }
        
        /* Parse directory entries */
        uint32_t pos = offset % NXFS_BLOCK_SIZE;
        while (pos < NXFS_BLOCK_SIZE && *read < count) {
            nxfs_dirent_t *de = (nxfs_dirent_t *)(block_buffer + pos);
            if (de->inode == 0 || de->record_len == 0) break;
            
            entries[*read].inode = de->inode;
            entries[*read].type = de->file_type == 2 ? VFS_DIRECTORY : VFS_FILE;
            nxfs_memcpy(entries[*read].name, de->name, de->name_len);
            entries[*read].name[de->name_len] = '\0';
            
            (*read)++;
            pos += de->record_len;
            offset += de->record_len;
        }
        
        if (pos >= NXFS_BLOCK_SIZE) {
            offset = (logical_block + 1) * NXFS_BLOCK_SIZE;
        }
    }
    
    return VFS_OK;
}

static vfs_node_t *nxfs_vfs_finddir(vfs_node_t *dir, const char *name) {
    vfs_dirent_t entries[32];
    uint32_t read = 0;
    
    if (nxfs_vfs_readdir(dir, entries, 32, &read) != VFS_OK) {
        return NULL;
    }
    
    for (uint32_t i = 0; i < read; i++) {
        if (nxfs_strcmp(entries[i].name, name) == 0) {
            /* Found - load inode and create vnode */
            nxfs_mount_t *nxfs = (nxfs_mount_t *)dir->mount->private_data;
            nxfs_inode_t *inode = nxfs_read_inode(nxfs, entries[i].inode);
            if (!inode) return NULL;
            
            vfs_node_t *node = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
            if (!node) { kfree(inode); return NULL; }
            
            nxfs_strcpy(node->name, name, VFS_NAME_MAX);
            node->type = entries[i].type;
            node->size = inode->size;
            node->inode = entries[i].inode;
            node->permissions = inode->permissions;
            node->uid = inode->uid;
            node->gid = inode->gid;
            node->mount = dir->mount;
            node->parent = dir;
            node->ops = &nxfs_ops;
            node->private_data = inode;
            
            return node;
        }
    }
    
    return NULL;
}

/* ============ File Creation/Deletion ============ */

/**
 * nxfs_vfs_create - Create a file or directory
 */
static int nxfs_vfs_create(vfs_node_t *parent, const char *name, vfs_type_t type, uint32_t perms) {
    if (!parent || !name) return VFS_ERR_INVALID;
    if (parent->type != VFS_DIRECTORY) return VFS_ERR_NOT_DIR;
    if (nxfs_strlen(name) > NXFS_NAME_MAX) return VFS_ERR_INVALID;
    
    nxfs_mount_t *nxfs = (nxfs_mount_t *)parent->mount->private_data;
    nxfs_inode_t *parent_inode = (nxfs_inode_t *)parent->private_data;
    
    /* Check if already exists */
    if (nxfs_vfs_finddir(parent, name) != NULL) {
        return VFS_ERR_EXIST;
    }
    
    /* Allocate a new inode */
    int64_t new_inode_num = nxfs_alloc_inode(nxfs);
    if (new_inode_num < 0) return VFS_ERR_NO_SPACE;
    
    /* Initialize the new inode */
    nxfs_inode_t new_inode;
    nxfs_memset(&new_inode, 0, sizeof(nxfs_inode_t));
    
    new_inode.flags = (type == VFS_DIRECTORY) ? NXFS_INODE_DIR : NXFS_INODE_FILE;
    new_inode.permissions = perms;
    new_inode.uid = 0;
    new_inode.gid = 0;
    new_inode.link_count = 1;
    
    if (type == VFS_DIRECTORY) {
        /* Allocate block for directory entries */
        int64_t dir_block = nxfs_alloc_block(nxfs);
        if (dir_block < 0) {
            nxfs_free_inode(nxfs, new_inode_num);
            return VFS_ERR_NO_SPACE;
        }
        
        new_inode.size = NXFS_BLOCK_SIZE;
        new_inode.blocks = 1;
        new_inode.extents[0].start_block = dir_block;
        new_inode.extents[0].block_count = 1;
        new_inode.extents[0].logical_start = 0;
        new_inode.link_count = 2;  /* . and .. */
        
        /* Write . and .. entries */
        uint8_t dir_buffer[NXFS_BLOCK_SIZE];
        nxfs_memset(dir_buffer, 0, NXFS_BLOCK_SIZE);
        
        nxfs_dirent_t *dot = (nxfs_dirent_t *)dir_buffer;
        dot->inode = new_inode_num;
        dot->record_len = 32;
        dot->name_len = 1;
        dot->file_type = 2;
        dot->name[0] = '.';
        
        nxfs_dirent_t *dotdot = (nxfs_dirent_t *)(dir_buffer + 32);
        dotdot->inode = parent->inode;
        dotdot->record_len = NXFS_BLOCK_SIZE - 32;
        dotdot->name_len = 2;
        dotdot->file_type = 2;
        dotdot->name[0] = '.';
        dotdot->name[1] = '.';
        
        nxfs_write_block(nxfs->device, dir_block, dir_buffer);
    } else {
        new_inode.size = 0;
        new_inode.blocks = 0;
    }
    
    /* Write new inode */
    nxfs_write_inode(nxfs, new_inode_num, &new_inode);
    
    /* Add entry to parent directory */
    uint8_t dir_buffer[NXFS_BLOCK_SIZE];
    uint64_t parent_block = parent_inode->extents[0].start_block;
    nxfs_read_block(nxfs->device, parent_block, dir_buffer);
    
    /* Find space for new entry */
    uint32_t pos = 0;
    while (pos < NXFS_BLOCK_SIZE) {
        nxfs_dirent_t *de = (nxfs_dirent_t *)(dir_buffer + pos);
        if (de->inode == 0 || de->record_len == 0) break;
        
        /* Check for unused space in this record */
        uint32_t actual_len = 8 + ((de->name_len + 3) & ~3); /* Align to 4 */
        if (de->record_len > actual_len + 32) {
            /* Split this entry */
            uint32_t new_len = de->record_len - actual_len;
            de->record_len = actual_len;
            
            nxfs_dirent_t *new_de = (nxfs_dirent_t *)(dir_buffer + pos + actual_len);
            new_de->inode = new_inode_num;
            new_de->record_len = new_len;
            new_de->name_len = nxfs_strlen(name);
            new_de->file_type = (type == VFS_DIRECTORY) ? 2 : 1;
            nxfs_strcpy(new_de->name, name, 20);
            
            nxfs_write_block(nxfs->device, parent_block, dir_buffer);
            nxfs_update_superblock(nxfs);
            return VFS_OK;
        }
        
        pos += de->record_len;
    }
    
    /* Add at end if space */
    if (pos < NXFS_BLOCK_SIZE - 32) {
        nxfs_dirent_t *new_de = (nxfs_dirent_t *)(dir_buffer + pos);
        new_de->inode = new_inode_num;
        new_de->record_len = NXFS_BLOCK_SIZE - pos;
        new_de->name_len = nxfs_strlen(name);
        new_de->file_type = (type == VFS_DIRECTORY) ? 2 : 1;
        nxfs_strcpy(new_de->name, name, 20);
        
        nxfs_write_block(nxfs->device, parent_block, dir_buffer);
        nxfs_update_superblock(nxfs);
        return VFS_OK;
    }
    
    return VFS_ERR_NO_SPACE;
}

/**
 * nxfs_vfs_unlink - Delete a file
 */
static int nxfs_vfs_unlink(vfs_node_t *parent, const char *name) {
    if (!parent || !name) return VFS_ERR_INVALID;
    
    nxfs_mount_t *nxfs = (nxfs_mount_t *)parent->mount->private_data;
    nxfs_inode_t *parent_inode = (nxfs_inode_t *)parent->private_data;
    
    /* Find the entry */
    vfs_node_t *target = nxfs_vfs_finddir(parent, name);
    if (!target) return VFS_ERR_NOT_FOUND;
    
    /* Don't delete non-empty directories */
    if (target->type == VFS_DIRECTORY) {
        vfs_dirent_t entries[3];
        uint32_t count = 0;
        nxfs_vfs_readdir(target, entries, 3, &count);
        if (count > 2) {  /* More than . and .. */
            kfree(target->private_data);
            kfree(target);
            return VFS_ERR_NOT_EMPTY;
        }
    }
    
    nxfs_inode_t *inode = (nxfs_inode_t *)target->private_data;
    uint64_t inode_num = target->inode;
    
    /* Free all blocks */
    for (int i = 0; i < 12; i++) {
        if (inode->extents[i].block_count == 0) break;
        for (uint32_t j = 0; j < inode->extents[i].block_count; j++) {
            nxfs_free_block(nxfs, inode->extents[i].start_block + j);
        }
    }
    
    /* Free inode */
    nxfs_free_inode(nxfs, inode_num);
    
    /* Remove directory entry */
    uint8_t dir_buffer[NXFS_BLOCK_SIZE];
    uint64_t parent_block = parent_inode->extents[0].start_block;
    nxfs_read_block(nxfs->device, parent_block, dir_buffer);
    
    uint32_t pos = 0;
    while (pos < NXFS_BLOCK_SIZE) {
        nxfs_dirent_t *de = (nxfs_dirent_t *)(dir_buffer + pos);
        if (de->record_len == 0) break;
        
        if (de->inode == inode_num) {
            de->inode = 0;  /* Mark as deleted */
            nxfs_write_block(nxfs->device, parent_block, dir_buffer);
            break;
        }
        pos += de->record_len;
    }
    
    kfree(target->private_data);
    kfree(target);
    
    nxfs_update_superblock(nxfs);
    return VFS_OK;
}

/**
 * nxfs_vfs_stat - Get file information
 */
static int nxfs_vfs_stat(vfs_node_t *node) {
    if (!node || !node->private_data) return VFS_ERR_INVALID;
    
    nxfs_inode_t *inode = (nxfs_inode_t *)node->private_data;
    
    /* Update node from inode */
    node->size = inode->size;
    node->permissions = inode->permissions;
    node->uid = inode->uid;
    node->gid = inode->gid;
    node->created = inode->created;
    node->modified = inode->modified;
    node->accessed = inode->accessed;
    
    return VFS_OK;
}

/**
 * nxfs_vfs_truncate - Resize a file
 */
static int nxfs_vfs_truncate(vfs_node_t *node, uint64_t new_size) {
    if (!node || !node->private_data) return VFS_ERR_INVALID;
    if (node->type == VFS_DIRECTORY) return VFS_ERR_IS_DIR;
    
    nxfs_inode_t *inode = (nxfs_inode_t *)node->private_data;
    nxfs_mount_t *nxfs = (nxfs_mount_t *)node->mount->private_data;
    
    uint64_t old_blocks = (inode->size + NXFS_BLOCK_SIZE - 1) / NXFS_BLOCK_SIZE;
    uint64_t new_blocks = (new_size + NXFS_BLOCK_SIZE - 1) / NXFS_BLOCK_SIZE;
    
    if (new_size < inode->size) {
        /* Shrinking - free excess blocks */
        for (int i = 0; i < 12; i++) {
            if (inode->extents[i].block_count == 0) break;
            
            uint64_t ext_start = inode->extents[i].logical_start;
            uint64_t ext_end = ext_start + inode->extents[i].block_count;
            
            if (ext_start >= new_blocks) {
                /* Free entire extent */
                for (uint32_t j = 0; j < inode->extents[i].block_count; j++) {
                    nxfs_free_block(nxfs, inode->extents[i].start_block + j);
                }
                inode->extents[i].block_count = 0;
            } else if (ext_end > new_blocks) {
                /* Partial free */
                uint32_t keep = new_blocks - ext_start;
                for (uint32_t j = keep; j < inode->extents[i].block_count; j++) {
                    nxfs_free_block(nxfs, inode->extents[i].start_block + j);
                }
                inode->extents[i].block_count = keep;
            }
        }
    }
    /* Growing is handled by write() when data is written */
    
    inode->size = new_size;
    inode->blocks = new_blocks;
    node->size = new_size;
    
    nxfs_write_inode(nxfs, node->inode, inode);
    (void)old_blocks;
    
    return VFS_OK;
}

/**
 * nxfs_vfs_rename - Move or rename a file/directory
 */
static int nxfs_vfs_rename(vfs_node_t *old_parent, const char *old_name,
                           vfs_node_t *new_parent, const char *new_name) {
    if (!old_parent || !old_name || !new_parent || !new_name) return VFS_ERR_INVALID;
    
    nxfs_mount_t *nxfs = (nxfs_mount_t *)old_parent->mount->private_data;
    
    /* Find source entry */
    vfs_node_t *src = nxfs_vfs_finddir(old_parent, old_name);
    if (!src) return VFS_ERR_NOT_FOUND;
    
    /* Check destination doesn't exist */
    vfs_node_t *dst = nxfs_vfs_finddir(new_parent, new_name);
    if (dst) {
        kfree(dst->private_data);
        kfree(dst);
        kfree(src->private_data);
        kfree(src);
        return VFS_ERR_EXIST;
    }
    
    uint64_t inode_num = src->inode;
    vfs_type_t type = src->type;
    
    /* Add new directory entry in new_parent */
    nxfs_inode_t *new_parent_inode = (nxfs_inode_t *)new_parent->private_data;
    uint8_t dir_buffer[NXFS_BLOCK_SIZE];
    uint64_t parent_block = new_parent_inode->extents[0].start_block;
    nxfs_read_block(nxfs->device, parent_block, dir_buffer);
    
    uint32_t pos = 0;
    int added = 0;
    while (pos < NXFS_BLOCK_SIZE && !added) {
        nxfs_dirent_t *de = (nxfs_dirent_t *)(dir_buffer + pos);
        if (de->inode == 0 || de->record_len == 0) {
            /* Use this slot */
            de->inode = inode_num;
            de->record_len = NXFS_BLOCK_SIZE - pos;
            de->name_len = nxfs_strlen(new_name);
            de->file_type = (type == VFS_DIRECTORY) ? 2 : 1;
            nxfs_strcpy(de->name, new_name, 20);
            added = 1;
        } else {
            uint32_t actual_len = 8 + ((de->name_len + 3) & ~3);
            if (de->record_len > actual_len + 32) {
                uint32_t new_len = de->record_len - actual_len;
                de->record_len = actual_len;
                
                nxfs_dirent_t *new_de = (nxfs_dirent_t *)(dir_buffer + pos + actual_len);
                new_de->inode = inode_num;
                new_de->record_len = new_len;
                new_de->name_len = nxfs_strlen(new_name);
                new_de->file_type = (type == VFS_DIRECTORY) ? 2 : 1;
                nxfs_strcpy(new_de->name, new_name, 20);
                added = 1;
            }
        }
        pos += de->record_len;
    }
    
    if (!added) {
        kfree(src->private_data);
        kfree(src);
        return VFS_ERR_NO_SPACE;
    }
    
    nxfs_write_block(nxfs->device, parent_block, dir_buffer);
    
    /* Remove old directory entry */
    nxfs_inode_t *old_parent_inode = (nxfs_inode_t *)old_parent->private_data;
    uint64_t old_block = old_parent_inode->extents[0].start_block;
    nxfs_read_block(nxfs->device, old_block, dir_buffer);
    
    pos = 0;
    while (pos < NXFS_BLOCK_SIZE) {
        nxfs_dirent_t *de = (nxfs_dirent_t *)(dir_buffer + pos);
        if (de->record_len == 0) break;
        if (de->inode == inode_num) {
            de->inode = 0;
            nxfs_write_block(nxfs->device, old_block, dir_buffer);
            break;
        }
        pos += de->record_len;
    }
    
    kfree(src->private_data);
    kfree(src);
    return VFS_OK;
}

/**
 * nxfs_vfs_chmod - Change file permissions
 */
static int nxfs_vfs_chmod(vfs_node_t *node, uint32_t mode) {
    if (!node || !node->private_data) return VFS_ERR_INVALID;
    
    nxfs_inode_t *inode = (nxfs_inode_t *)node->private_data;
    nxfs_mount_t *nxfs = (nxfs_mount_t *)node->mount->private_data;
    
    inode->permissions = mode & 0777;  /* Only permission bits */
    node->permissions = inode->permissions;
    
    return nxfs_write_inode(nxfs, node->inode, inode);
}

/**
 * nxfs_vfs_chown - Change file owner
 */
static int nxfs_vfs_chown(vfs_node_t *node, uint32_t uid, uint32_t gid) {
    if (!node || !node->private_data) return VFS_ERR_INVALID;
    
    nxfs_inode_t *inode = (nxfs_inode_t *)node->private_data;
    nxfs_mount_t *nxfs = (nxfs_mount_t *)node->mount->private_data;
    
    inode->uid = uid;
    inode->gid = gid;
    node->uid = uid;
    node->gid = gid;
    
    return nxfs_write_inode(nxfs, node->inode, inode);
}

/* ============ NXFS Mount/Unmount ============ */

int nxfs_mount(vfs_mount_t *mount) {
    serial_puts("[NXFS] Mounting filesystem...\n");
    
    if (!block_ops) {
        serial_puts("[NXFS] ERROR: No block device operations\n");
        return VFS_ERR_IO;
    }
    
    /* Allocate mount info */
    nxfs_mount_t *nxfs = (nxfs_mount_t *)kzalloc(sizeof(nxfs_mount_t));
    if (!nxfs) return VFS_ERR_NO_MEM;
    
    nxfs->device = mount->device;
    
    /* Read superblock */
    if (nxfs_read_block(mount->device, 0, &nxfs->superblock) != 0) {
        kfree(nxfs);
        return VFS_ERR_IO;
    }
    
    /* Validate magic */
    if (nxfs->superblock.magic != NXFS_MAGIC) {
        serial_puts("[NXFS] ERROR: Invalid magic number\n");
        kfree(nxfs);
        return VFS_ERR_INVALID;
    }
    
    serial_puts("[NXFS] Volume: ");
    serial_puts(nxfs->superblock.volume_name);
    serial_puts("\n");
    
    /* Handle encryption */
    nxfs->encrypted = (nxfs->superblock.encryption_mode != NXFS_ENCRYPT_NONE);
    if (nxfs->encrypted) {
        serial_puts("[NXFS] Volume is encrypted (AES-256)\n");
        /* TODO: Request key from user */
    }
    
    /* Read root inode */
    nxfs_inode_t *root_inode = nxfs_read_inode(nxfs, nxfs->superblock.root_inode);
    if (!root_inode) {
        kfree(nxfs);
        return VFS_ERR_IO;
    }
    
    /* Create root vnode */
    vfs_node_t *root = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    if (!root) {
        kfree(root_inode);
        kfree(nxfs);
        return VFS_ERR_NO_MEM;
    }
    
    nxfs_strcpy(root->name, "/", VFS_NAME_MAX);
    root->type = VFS_DIRECTORY;
    root->size = root_inode->size;
    root->inode = nxfs->superblock.root_inode;
    root->permissions = root_inode->permissions;
    root->mount = mount;
    root->ops = &nxfs_ops;
    root->private_data = root_inode;
    
    nxfs->root = root;
    mount->root = root;
    mount->private_data = nxfs;
    
    serial_puts("[NXFS] Mounted successfully\n");
    return VFS_OK;
}

int nxfs_unmount(vfs_mount_t *mount) {
    nxfs_mount_t *nxfs = (nxfs_mount_t *)mount->private_data;
    if (nxfs) {
        /* TODO: Sync and free all cached inodes/vnodes */
        kfree(nxfs);
    }
    mount->private_data = NULL;
    mount->root = NULL;
    return VFS_OK;
}

int nxfs_sync(vfs_mount_t *mount) {
    /* TODO: Flush all dirty buffers to disk */
    return VFS_OK;
}

/* ============ NXFS Filesystem Type ============ */

static vfs_fs_type_t nxfs_fs_type = {
    .name = "nxfs",
    .mount = nxfs_mount,
    .unmount = nxfs_unmount,
    .sync = nxfs_sync,
};

static vfs_fs_type_t nxfs_enc_fs_type = {
    .name = "nxfs_enc",
    .mount = nxfs_mount,  /* Same mount, but encrypted flag set */
    .unmount = nxfs_unmount,
    .sync = nxfs_sync,
};

void nxfs_init(void) {
    serial_puts("[NXFS] Initializing NXFS filesystem driver...\n");
    vfs_register_fs(&nxfs_fs_type);
    vfs_register_fs(&nxfs_enc_fs_type);
    serial_puts("[NXFS] Ready (standard + encrypted modes)\n");
}

/* ============ NXFS Format ============ */

/**
 * nxfs_format - Format a partition as NXFS
 * @device: Pointer to block device
 * @total_blocks: Total 4KB blocks in the partition
 * @volume_name: Volume label (max 63 chars)
 * @encrypted: 1 to enable AES-256 encryption
 * @key: 32-byte encryption key (NULL if not encrypted)
 *
 * Returns 0 on success, negative on error.
 * 
 * NXFS Layout:
 *   Block 0: Superblock
 *   Block 1+: Block bitmap
 *   Block N: Inode bitmap  
 *   Block M: Inode table
 *   Block X+: Data area (root dir is first data block)
 */
int nxfs_format(void *device, uint64_t total_blocks, const char *volume_name,
                int encrypted, const uint8_t *key) {
    serial_puts("[NXFS] Formatting device...\n");
    
    if (!block_ops || !device) {
        serial_puts("[NXFS] ERROR: No block device\n");
        return -1;
    }
    
    if (total_blocks < 128) {
        serial_puts("[NXFS] ERROR: Partition too small (min 512KB)\n");
        return -1;
    }
    
    /* Allocate working buffer */
    uint8_t buffer[NXFS_BLOCK_SIZE];
    nxfs_memset(buffer, 0, NXFS_BLOCK_SIZE);
    
    /* === Calculate filesystem geometry === */
    uint64_t inode_count = total_blocks / 16;  /* 1 inode per 16 blocks */
    if (inode_count < 16) inode_count = 16;    /* Minimum 16 inodes */
    if (inode_count > 65536) inode_count = 65536; /* Max 64K inodes */
    
    uint64_t inode_table_blocks = (inode_count * sizeof(nxfs_inode_t) + NXFS_BLOCK_SIZE - 1) / NXFS_BLOCK_SIZE;
    uint64_t block_bitmap_blocks = (total_blocks / 8 + NXFS_BLOCK_SIZE - 1) / NXFS_BLOCK_SIZE;
    uint64_t inode_bitmap_blocks = (inode_count / 8 + NXFS_BLOCK_SIZE - 1) / NXFS_BLOCK_SIZE;
    
    /* Layout: superblock (1) + block_bitmap + inode_bitmap + inode_table */
    uint64_t block_bitmap_start = 1;
    uint64_t inode_bitmap_start = block_bitmap_start + block_bitmap_blocks;
    uint64_t inode_table_start = inode_bitmap_start + inode_bitmap_blocks;
    uint64_t data_start = inode_table_start + inode_table_blocks;
    
    uint64_t reserved_blocks = data_start;
    uint64_t free_data_blocks = total_blocks - reserved_blocks - 1; /* -1 for root dir */
    
    serial_puts("[NXFS] Geometry: ");
    serial_putc('0' + (total_blocks / 10000) % 10);
    serial_putc('0' + (total_blocks / 1000) % 10);
    serial_putc('0' + (total_blocks / 100) % 10);
    serial_putc('0' + (total_blocks / 10) % 10);
    serial_putc('0' + total_blocks % 10);
    serial_puts(" blocks, ");
    serial_putc('0' + (inode_count / 1000) % 10);
    serial_putc('0' + (inode_count / 100) % 10);
    serial_putc('0' + (inode_count / 10) % 10);
    serial_putc('0' + inode_count % 10);
    serial_puts(" inodes\n");
    
    /* === Step 1: Write Superblock === */
    serial_puts("[NXFS] Step 1: Writing superblock...\n");
    
    nxfs_superblock_t *sb = (nxfs_superblock_t *)buffer;
    nxfs_memset(sb, 0, sizeof(nxfs_superblock_t));
    
    sb->magic = NXFS_MAGIC;
    sb->version = NXFS_VERSION;
    sb->block_size = NXFS_BLOCK_SIZE;
    sb->flags = 0;
    sb->total_blocks = total_blocks;
    sb->free_blocks = free_data_blocks;
    sb->total_inodes = inode_count;
    sb->free_inodes = inode_count - 2;  /* -2 for root inode and reserved inode 0 */
    
    sb->block_bitmap_block = block_bitmap_start;
    sb->inode_bitmap_block = inode_bitmap_start;
    sb->inode_table_block = inode_table_start;
    sb->root_inode = 1;  /* Inode 0 is reserved, root is inode 1 */
    
    /* Handle encryption */
    if (encrypted && key) {
        sb->encryption_mode = NXFS_ENCRYPT_AES256;
        sb->flags |= NXFS_MOUNT_ENCRYPTED;
        
        /* Store key hash (simple XOR hash for now - TODO: proper SHA-256) */
        for (int i = 0; i < 32; i++) {
            sb->encryption_key_hash[i] = key[i] ^ 0xAA;
        }
        
        /* Generate IV from key */
        for (int i = 0; i < 16; i++) {
            sb->encryption_iv[i] = key[i] ^ key[16 + i];
        }
        
        serial_puts("[NXFS] Encryption enabled (AES-256)\n");
    } else {
        sb->encryption_mode = NXFS_ENCRYPT_NONE;
    }
    
    /* Volume name */
    if (volume_name && volume_name[0]) {
        nxfs_strcpy(sb->volume_name, volume_name, 64);
    } else {
        nxfs_strcpy(sb->volume_name, "NeolyxOS", 64);
    }
    
    /* Timestamps (TODO: get real time) */
    sb->created = 0;
    sb->mounted = 0;
    sb->mount_count = 0;
    sb->max_mount_count = 50;
    
    /* Generate UUID */
    for (int i = 0; i < 16; i++) {
        sb->uuid[i] = (char)(i * 17 + 0x42);  /* Simple pseudo-UUID */
    }
    
    /* Checksum (exclude checksum and reserved fields) */
    sb->checksum = nxfs_checksum(sb, sizeof(nxfs_superblock_t) - sizeof(sb->checksum) - sizeof(sb->reserved));
    
    if (nxfs_write_block(device, 0, buffer) != 0) {
        serial_puts("[NXFS] ERROR: Failed to write superblock\n");
        return -2;
    }
    
    /* === Step 2: Initialize Block Bitmap === */
    serial_puts("[NXFS] Step 2: Initializing block bitmap...\n");
    
    /* Zero all bitmap blocks first */
    nxfs_memset(buffer, 0, NXFS_BLOCK_SIZE);
    for (uint64_t b = 0; b < block_bitmap_blocks; b++) {
        if (nxfs_write_block(device, block_bitmap_start + b, buffer) != 0) {
            serial_puts("[NXFS] ERROR: Failed to write block bitmap\n");
            return -3;
        }
    }
    
    /* Mark reserved blocks as used (superblock, bitmaps, inode table, root dir) */
    nxfs_memset(buffer, 0, NXFS_BLOCK_SIZE);
    uint64_t blocks_to_mark = data_start + 1;  /* +1 for root directory block */
    
    /* Set bits for reserved blocks */
    for (uint64_t i = 0; i < blocks_to_mark && i < NXFS_BLOCK_SIZE * 8; i++) {
        buffer[i / 8] |= (1 << (i % 8));
    }
    
    if (nxfs_write_block(device, block_bitmap_start, buffer) != 0) {
        serial_puts("[NXFS] ERROR: Failed to write block bitmap\n");
        return -3;
    }
    
    /* === Step 3: Initialize Inode Bitmap === */
    serial_puts("[NXFS] Step 3: Initializing inode bitmap...\n");
    
    nxfs_memset(buffer, 0, NXFS_BLOCK_SIZE);
    for (uint64_t b = 0; b < inode_bitmap_blocks; b++) {
        if (nxfs_write_block(device, inode_bitmap_start + b, buffer) != 0) {
            serial_puts("[NXFS] ERROR: Failed to write inode bitmap\n");
            return -4;
        }
    }
    
    /* Mark inode 0 (reserved) and inode 1 (root) as used */
    nxfs_memset(buffer, 0, NXFS_BLOCK_SIZE);
    buffer[0] = 0x03;  /* Bits 0 and 1 set */
    
    if (nxfs_write_block(device, inode_bitmap_start, buffer) != 0) {
        serial_puts("[NXFS] ERROR: Failed to write inode bitmap\n");
        return -4;
    }
    
    /* === Step 4: Initialize Inode Table === */
    serial_puts("[NXFS] Step 4: Initializing inode table...\n");
    
    /* Zero first block of inode table (contains inodes 0 and 1) */
    nxfs_memset(buffer, 0, NXFS_BLOCK_SIZE);
    
    uint64_t inodes_per_block = NXFS_BLOCK_SIZE / sizeof(nxfs_inode_t);
    
    /* Inode 0: Reserved (unused) */
    nxfs_inode_t *inode0 = (nxfs_inode_t *)buffer;
    inode0->flags = 0;
    
    /* Inode 1: Root directory */
    nxfs_inode_t *inode1 = (nxfs_inode_t *)(buffer + sizeof(nxfs_inode_t));
    inode1->flags = NXFS_INODE_DIR;
    inode1->permissions = 0755;  /* rwxr-xr-x */
    inode1->uid = 0;
    inode1->gid = 0;
    inode1->size = NXFS_BLOCK_SIZE;  /* One block for empty directory */
    inode1->blocks = 1;
    inode1->created = 0;
    inode1->modified = 0;
    inode1->accessed = 0;
    inode1->link_count = 2;  /* . and .. */
    inode1->checksum = 0;
    
    /* Root directory data is at first data block */
    inode1->extents[0].start_block = data_start;
    inode1->extents[0].block_count = 1;
    inode1->extents[0].logical_start = 0;
    
    /* Encrypt inode block if encrypted */
    if (encrypted && key) {
        nxfs_encrypt_block(key, sb->encryption_iv, buffer, NXFS_BLOCK_SIZE);
    }
    
    if (nxfs_write_block(device, inode_table_start, buffer) != 0) {
        serial_puts("[NXFS] ERROR: Failed to write inode table\n");
        return -5;
    }
    
    /* Zero remaining inode table blocks */
    nxfs_memset(buffer, 0, NXFS_BLOCK_SIZE);
    for (uint64_t b = 1; b < inode_table_blocks; b++) {
        nxfs_write_block(device, inode_table_start + b, buffer);
    }
    
    /* === Step 5: Initialize Root Directory === */
    serial_puts("[NXFS] Step 5: Initializing root directory...\n");
    
    nxfs_memset(buffer, 0, NXFS_BLOCK_SIZE);
    
    /* Create . entry */
    nxfs_dirent_t *dot = (nxfs_dirent_t *)buffer;
    dot->inode = 1;
    dot->record_len = 32;
    dot->name_len = 1;
    dot->file_type = 2;  /* Directory */
    dot->name[0] = '.';
    dot->name[1] = '\0';
    
    /* Create .. entry */
    nxfs_dirent_t *dotdot = (nxfs_dirent_t *)(buffer + 32);
    dotdot->inode = 1;  /* Parent of root is root */
    dotdot->record_len = NXFS_BLOCK_SIZE - 32;  /* Rest of block */
    dotdot->name_len = 2;
    dotdot->file_type = 2;  /* Directory */
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    dotdot->name[2] = '\0';
    
    /* Encrypt if needed */
    if (encrypted && key) {
        nxfs_encrypt_block(key, sb->encryption_iv, buffer, NXFS_BLOCK_SIZE);
    }
    
    if (nxfs_write_block(device, data_start, buffer) != 0) {
        serial_puts("[NXFS] ERROR: Failed to write root directory\n");
        return -6;
    }
    
    serial_puts("[NXFS] Format complete: ");
    serial_puts(volume_name ? volume_name : "NeolyxOS");
    if (encrypted) serial_puts(" (encrypted)");
    serial_puts("\n");
    
    return 0;
}

