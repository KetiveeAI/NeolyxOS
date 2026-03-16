/*
 * NXZIP - NeolyxOS Compression Library
 * CRC32 Implementation
 * 
 * Standard CRC32 with polynomial 0xEDB88320 (ZIP compatible)
 * 
 * Copyright (c) 2025-2026 KetiveeAI. All Rights Reserved.
 */

#include "../include/nxzip.h"

/* CRC32 lookup table */
static uint32_t crc32_table[256];
static bool crc32_table_init = false;

/* Initialize CRC32 table */
static void crc32_init_table(void) {
    if (crc32_table_init) return;
    
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_table_init = true;
}

/* Calculate CRC32 of data */
uint32_t nxzip_crc32(const void* data, size_t size) {
    return nxzip_crc32_update(0xFFFFFFFF, data, size) ^ 0xFFFFFFFF;
}

/* Update CRC32 incrementally */
uint32_t nxzip_crc32_update(uint32_t crc, const void* data, size_t size) {
    crc32_init_table();
    
    const uint8_t* bytes = (const uint8_t*)data;
    for (size_t i = 0; i < size; i++) {
        uint8_t index = (crc ^ bytes[i]) & 0xFF;
        crc = crc32_table[index] ^ (crc >> 8);
    }
    
    return crc;
}
