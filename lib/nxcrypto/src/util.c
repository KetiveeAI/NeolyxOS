/*
 * nxcrypto - status strings and constant-time helpers.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "nxcrypto/common.h"

const char *nxc_status_str(int status) {
    switch (status) {
    case NXC_OK:           return "ok";
    case NXC_ERR_ARG:      return "invalid argument";
    case NXC_ERR_STATE:    return "context used out of order";
    case NXC_ERR_LENGTH:   return "length out of range";
    case NXC_ERR_VERIFY:   return "verification failed";
    case NXC_ERR_ENCODING: return "malformed encoding";
    case NXC_ERR_WEAK:     return "weak or low-order value";
    default:               return "unknown status";
    }
}

/*
 * Writing through a volatile pointer is an observable side effect, so the
 * compiler may not drop the stores even when the buffer is dead afterwards.
 * The empty asm with a memory clobber also stops reordering past it.
 */
void nxc_memzero(void *p, size_t len) {
    if (p == NULL) return;
    volatile uint8_t *v = (volatile uint8_t *)p;
    for (size_t i = 0; i < len; i++) v[i] = 0;
    __asm__ __volatile__("" : : "r"(p) : "memory");
}

int nxc_ct_equal(const void *a, const void *b, size_t len) {
    if (a == NULL || b == NULL) return 0;
    const volatile uint8_t *x = (const volatile uint8_t *)a;
    const volatile uint8_t *y = (const volatile uint8_t *)b;
    uint32_t diff = 0;
    for (size_t i = 0; i < len; i++) diff |= (uint32_t)(x[i] ^ y[i]);
    /* (diff - 1) >> 8 has bit 0 set only when diff == 0 (diff <= 0xff). */
    return (int)(((diff - 1u) >> 8) & 1u);
}
