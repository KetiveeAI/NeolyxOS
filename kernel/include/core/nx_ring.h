/*
 * NeolyxOS byte ring buffer (kernel-internal)
 *
 * A fixed-capacity FIFO of bytes over caller-provided storage. Used by
 * pipes and by the PTY line discipline (core/nx_ldisc.h). It allocates
 * nothing, takes no locks and has no kernel dependencies, so the same code
 * runs in host unit tests (tests/host/nx_ldisc_test.c). Callers serialise
 * access (interrupts off in the kernel).
 *
 * Not part of the user ABI.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_NX_RING_H
#define NEOLYX_NX_RING_H

#include <stdint.h>

typedef struct {
    uint8_t  *buf;      /* storage, cap bytes (owned by the caller) */
    uint32_t  cap;      /* capacity in bytes, > 0 */
    uint32_t  head;     /* index of the oldest byte */
    uint32_t  count;    /* bytes stored */
} nx_ring_t;

/**
 * Initialise a ring over storage. A NULL storage or cap 0 leaves a ring
 * that holds nothing (every write stores 0 bytes).
 */
void nx_ring_init(nx_ring_t *r, uint8_t *storage, uint32_t cap);

/** Bytes stored. 0 for a NULL ring. */
uint32_t nx_ring_used(const nx_ring_t *r);

/** Bytes that still fit. 0 for a NULL ring. */
uint32_t nx_ring_free(const nx_ring_t *r);

/**
 * Append up to n bytes from src. Never overwrites stored bytes.
 * @return bytes stored (less than n when the ring fills)
 */
uint32_t nx_ring_write(nx_ring_t *r, const uint8_t *src, uint32_t n);

/** Append one byte. @return 1 if stored, 0 if the ring is full. */
uint32_t nx_ring_put(nx_ring_t *r, uint8_t byte);

/**
 * Remove up to n of the oldest bytes into dst (dst may be NULL to drop).
 * @return bytes removed
 */
uint32_t nx_ring_read(nx_ring_t *r, uint8_t *dst, uint32_t n);

/** Drop every stored byte. */
void nx_ring_clear(nx_ring_t *r);

#endif /* NEOLYX_NX_RING_H */
