/*
 * NeolyxOS byte ring buffer (kernel-internal). See core/nx_ring.h.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "core/nx_ring.h"

static int ring_ok(const nx_ring_t *r) {
    return r && r->buf && r->cap > 0 && r->count <= r->cap && r->head < r->cap;
}

void nx_ring_init(nx_ring_t *r, uint8_t *storage, uint32_t cap) {
    if (!r) return;
    r->buf = storage;
    r->cap = storage ? cap : 0;
    r->head = 0;
    r->count = 0;
}

uint32_t nx_ring_used(const nx_ring_t *r) {
    return ring_ok(r) ? r->count : 0;
}

uint32_t nx_ring_free(const nx_ring_t *r) {
    return ring_ok(r) ? r->cap - r->count : 0;
}

uint32_t nx_ring_write(nx_ring_t *r, const uint8_t *src, uint32_t n) {
    if (!ring_ok(r) || !src) return 0;
    uint32_t room = r->cap - r->count;
    if (n > room) n = room;
    uint32_t tail = r->head + r->count;
    if (tail >= r->cap) tail -= r->cap;
    for (uint32_t i = 0; i < n; i++) {
        r->buf[tail] = src[i];
        if (++tail == r->cap) tail = 0;
    }
    r->count += n;
    return n;
}

uint32_t nx_ring_put(nx_ring_t *r, uint8_t byte) {
    return nx_ring_write(r, &byte, 1);
}

uint32_t nx_ring_read(nx_ring_t *r, uint8_t *dst, uint32_t n) {
    if (!ring_ok(r)) return 0;
    if (n > r->count) n = r->count;
    for (uint32_t i = 0; i < n; i++) {
        if (dst) dst[i] = r->buf[r->head];
        if (++r->head == r->cap) r->head = 0;
    }
    r->count -= n;
    if (r->count == 0) r->head = 0;
    return n;
}

void nx_ring_clear(nx_ring_t *r) {
    if (!r) return;
    r->head = 0;
    r->count = 0;
}
