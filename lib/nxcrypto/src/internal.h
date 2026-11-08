/*
 * nxcrypto - private helpers shared by the sources (not installed).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXCRYPTO_INTERNAL_H
#define NXCRYPTO_INTERNAL_H

#include "nxcrypto/common.h"

/* Context magic: set by init, checked by update/final, cleared on wipe. */
#define NXC_CTX_MAGIC 0x4e584331u   /* "NXC1" */

static inline uint32_t nxc_load32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline void nxc_store32_be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static inline uint64_t nxc_load64_be(const uint8_t *p) {
    return ((uint64_t)nxc_load32_be(p) << 32) | nxc_load32_be(p + 4);
}

static inline void nxc_store64_be(uint8_t *p, uint64_t v) {
    nxc_store32_be(p, (uint32_t)(v >> 32));
    nxc_store32_be(p + 4, (uint32_t)v);
}

static inline uint32_t nxc_load32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void nxc_store32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;         p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static inline uint64_t nxc_load64_le(const uint8_t *p) {
    return (uint64_t)nxc_load32_le(p) | ((uint64_t)nxc_load32_le(p + 4) << 32);
}

static inline void nxc_store64_le(uint8_t *p, uint64_t v) {
    nxc_store32_le(p, (uint32_t)v);
    nxc_store32_le(p + 4, (uint32_t)(v >> 32));
}

/* Byte copy that does not depend on the C library. */
static inline void nxc_copy(void *dst, const void *src, size_t len) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < len; i++) d[i] = s[i];
}

#endif /* NXCRYPTO_INTERNAL_H */
