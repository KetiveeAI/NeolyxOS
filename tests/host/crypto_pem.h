/*
 * PEM reader for the nxtls host tests: finds "-----BEGIN <label>-----"
 * blocks and decodes their base64 body (RFC 7468). Host-only.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef CRYPTO_PEM_H
#define CRYPTO_PEM_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int b64val(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/*
 * Decode the next PEM block with the given label, starting at *pos.
 * Returns a malloc'd DER buffer (length in *len) and advances *pos past
 * the block, or returns NULL when there are no more blocks or the base64
 * is malformed (then *len is 0 for "no more", 1 for "malformed").
 */
static uint8_t *pem_next(const char **pos, const char *label, size_t *len) {
    char begin[64], end[64];
    snprintf(begin, sizeof(begin), "-----BEGIN %s-----", label);
    snprintf(end, sizeof(end), "-----END %s-----", label);
    *len = 0;
    const char *b = strstr(*pos, begin);
    if (!b) return NULL;
    b += strlen(begin);
    const char *e = strstr(b, end);
    if (!e) { *len = 1; return NULL; }
    uint8_t *out = malloc((size_t)(e - b) * 3 / 4 + 4);
    if (!out) { perror("malloc"); exit(2); }
    size_t n = 0;
    unsigned acc = 0;
    int bits = 0, pad = 0;
    for (const char *p = b; p < e; p++) {
        if (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') continue;
        if (*p == '=') { pad++; continue; }
        int v = b64val((unsigned char)*p);
        if (v < 0 || pad) { free(out); *len = 1; *pos = e; return NULL; }
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[n++] = (uint8_t)(acc >> bits);
        }
    }
    *pos = e + strlen(end);
    *len = n;
    return out;
}

#endif /* CRYPTO_PEM_H */
