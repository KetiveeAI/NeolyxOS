/*
 * nxtls - strict DER reader (ITU-T X.690 section 10), internal.
 *
 * A reader is a view of the bytes still to be read. Every function either
 * consumes one complete element and returns 0, or returns -1 and leaves
 * the reader unchanged. Only the DER forms are accepted: definite lengths
 * in the shortest form, single-byte tags (tag numbers below 31, which is
 * all X.509 needs), minimal INTEGERs, BOOLEAN as 0x00 or 0xFF.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXTLS_DER_H
#define NXTLS_DER_H

#include <stddef.h>
#include <stdint.h>

#define DER_BOOLEAN       0x01
#define DER_INTEGER       0x02
#define DER_BIT_STRING    0x03
#define DER_OCTET_STRING  0x04
#define DER_NULL          0x05
#define DER_OID           0x06
#define DER_UTF8_STRING   0x0c
#define DER_PRINTABLE     0x13
#define DER_IA5_STRING    0x16
#define DER_UTC_TIME      0x17
#define DER_GEN_TIME      0x18
#define DER_SEQUENCE      0x30
#define DER_SET           0x31
#define DER_CTX(n)        (0x80 | (n))      /* [n] IMPLICIT, primitive */
#define DER_CTX_CONS(n)   (0xa0 | (n))      /* [n], constructed */

typedef struct {
    const uint8_t *p;
    size_t len;
} der_reader;

static inline void der_init(der_reader *r, const uint8_t *p, size_t len) {
    r->p = p;
    r->len = len;
}

static inline int der_empty(const der_reader *r) { return r->len == 0; }

/* Tag of the next element, or -1 at the end. */
static inline int der_peek(const der_reader *r) { return r->len ? r->p[0] : -1; }

/*
 * Read one element. *tag gets its tag, *content its contents and, if
 * whole is not NULL, *whole the full encoding (tag, length and contents).
 */
int der_next(der_reader *r, uint8_t *tag, der_reader *content, der_reader *whole);

/* Read one element that must have tag `tag`. */
int der_expect(der_reader *r, uint8_t tag, der_reader *content, der_reader *whole);

/*
 * If the next element has tag `tag`, read it and set *present to 1;
 * otherwise read nothing and set *present to 0.
 */
int der_optional(der_reader *r, uint8_t tag, der_reader *content, int *present);

/*
 * Check that an INTEGER's contents are minimal. *mag gets the magnitude
 * without a leading 0x00 sign byte. Fails for negative values when
 * allow_negative is 0.
 */
int der_integer(const der_reader *content, int allow_negative, der_reader *mag);

/* INTEGER that fits in 0..max; result in *out. */
int der_small_uint(der_reader *r, uint32_t max, uint32_t *out);

/* BOOLEAN element (must be 0x00 or 0xFF). */
int der_boolean(der_reader *r, int *out);

/*
 * BIT STRING element: *bits gets the bytes after the unused-bits count,
 * *unused the count (0..7, unused bits must be zero, and an empty string
 * must have 0 unused bits).
 */
int der_bit_string(der_reader *r, der_reader *bits, unsigned *unused);

/* 1 if the reader's bytes equal the len bytes at p. */
int der_equal(const der_reader *r, const uint8_t *p, size_t len);

#endif /* NXTLS_DER_H */
