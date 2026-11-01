/*
 * nxtls - strict DER reader. See der.h.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "der.h"

int der_next(der_reader *r, uint8_t *tag, der_reader *content, der_reader *whole) {
    if (r == NULL || r->len < 2) return -1;
    const uint8_t *p = r->p;
    size_t avail = r->len;
    uint8_t t = p[0];
    if ((t & 0x1f) == 0x1f) return -1;          /* high tag numbers: not used by X.509 */

    size_t hdr = 2, len = p[1];
    if (len & 0x80) {
        size_t n = len & 0x7f;
        /* 0x80 is the BER indefinite form; 0xff is reserved. Lengths up
         * to 4 bytes are plenty for certificates. */
        if (n == 0 || n > 4 || avail < 2 + n) return -1;
        len = 0;
        for (size_t i = 0; i < n; i++) len = (len << 8) | p[2 + i];
        if (p[2] == 0) return -1;               /* leading zero: not minimal */
        if (len < 0x80) return -1;              /* short form was required */
        hdr = 2 + n;
    }
    if (len > avail - hdr) return -1;

    if (tag) *tag = t;
    if (content) der_init(content, p + hdr, len);
    if (whole) der_init(whole, p, hdr + len);
    r->p += hdr + len;
    r->len -= hdr + len;
    return 0;
}

int der_expect(der_reader *r, uint8_t tag, der_reader *content, der_reader *whole) {
    if (der_peek(r) != tag) return -1;
    return der_next(r, NULL, content, whole);
}

int der_optional(der_reader *r, uint8_t tag, der_reader *content, int *present) {
    *present = 0;
    if (der_peek(r) != tag) return 0;
    if (der_next(r, NULL, content, NULL) != 0) return -1;
    *present = 1;
    return 0;
}

int der_integer(const der_reader *c, int allow_negative, der_reader *mag) {
    if (c->len == 0) return -1;
    if (c->len > 1) {
        /* X.690 8.3.2: the first nine bits must not be all 0 or all 1. */
        if (c->p[0] == 0x00 && !(c->p[1] & 0x80)) return -1;
        if (c->p[0] == 0xff && (c->p[1] & 0x80)) return -1;
    }
    if ((c->p[0] & 0x80) && !allow_negative) return -1;
    if (mag) {
        if (c->len > 1 && c->p[0] == 0x00) der_init(mag, c->p + 1, c->len - 1);
        else der_init(mag, c->p, c->len);
    }
    return 0;
}

int der_small_uint(der_reader *r, uint32_t max, uint32_t *out) {
    der_reader saved = *r, c, mag;
    if (der_expect(r, DER_INTEGER, &c, NULL) != 0 || der_integer(&c, 0, &mag) != 0 ||
        mag.len > 4) {
        *r = saved;
        return -1;
    }
    uint32_t v = 0;
    for (size_t i = 0; i < mag.len; i++) v = (v << 8) | mag.p[i];
    if (v > max) {
        *r = saved;
        return -1;
    }
    *out = v;
    return 0;
}

int der_boolean(der_reader *r, int *out) {
    der_reader saved = *r, c;
    if (der_expect(r, DER_BOOLEAN, &c, NULL) != 0 || c.len != 1 ||
        (c.p[0] != 0x00 && c.p[0] != 0xff)) {
        *r = saved;
        return -1;
    }
    *out = c.p[0] == 0xff;
    return 0;
}

int der_bit_string(der_reader *r, der_reader *bits, unsigned *unused) {
    der_reader saved = *r, c;
    if (der_expect(r, DER_BIT_STRING, &c, NULL) != 0 || c.len == 0 || c.p[0] > 7 ||
        (c.len == 1 && c.p[0] != 0) ||
        (c.len > 1 && (c.p[c.len - 1] & ((1u << c.p[0]) - 1u)) != 0)) {
        *r = saved;
        return -1;
    }
    *unused = c.p[0];
    der_init(bits, c.p + 1, c.len - 1);
    return 0;
}

int der_equal(const der_reader *r, const uint8_t *p, size_t len) {
    if (r->len != len) return 0;
    for (size_t i = 0; i < len; i++)
        if (r->p[i] != p[i]) return 0;
    return 1;
}
