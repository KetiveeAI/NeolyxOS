/*
 * Shared helpers for the nxcrypto host tests (tests/host/crypto_*_test.c).
 *
 * Published test vectors are data, not code, and are not stored in the
 * tree: tests/host/crypto_fetch_vectors.sh downloads a pinned, checksummed
 * copy of the vector set (NIST CAVP files and RFC examples) and each test
 * takes its directory as argv[1]. This header reads those files: a
 * "Name = value" line parser, a hex decoder and a small check counter.
 *
 * Host-only (uses the C library). Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef CRYPTO_VEC_H
#define CRYPTO_VEC_H

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks, g_failed;

#define CHECK(cond, ...) do {                                   \
        g_checks++;                                             \
        if (!(cond)) {                                          \
            g_failed++;                                         \
            if (g_failed <= 20) {                               \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);     \
                printf(__VA_ARGS__);                            \
                printf("\n");                                   \
            }                                                   \
        }                                                       \
    } while (0)

static inline int vec_report(const char *name) {
    printf("%s: %d checks, %d failed -> %s\n", name, g_checks, g_failed,
           g_failed ? "FAIL" : "PASS");
    return g_failed ? 1 : 0;
}

/* strdup is POSIX, not C11; tests build with -std=c11. */
static inline char *vec_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (!d) { perror("malloc"); exit(2); }
    memcpy(d, s, n);
    return d;
}

static inline int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decode hex into a new malloc'd buffer; *out_len gets the byte count.
 * Returns NULL on odd length or a non-hex character. */
static inline uint8_t *hex_decode(const char *hex, size_t *out_len) {
    size_t n = strlen(hex);
    if (n % 2) return NULL;
    uint8_t *b = malloc(n / 2 + 1);
    if (!b) { perror("malloc"); exit(2); }
    for (size_t i = 0; i < n / 2; i++) {
        int hi = hexval(hex[2 * i]), lo = hexval(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) { free(b); return NULL; }
        b[i] = (uint8_t)(hi << 4 | lo);
    }
    *out_len = n / 2;
    return b;
}

/* Decode into a fixed buffer; returns 0 on success. */
static inline int hex_fixed(const char *hex, uint8_t *out, size_t want) {
    size_t n = 0;
    uint8_t *b = hex_decode(hex, &n);
    if (!b || n != want) { free(b); return -1; }
    memcpy(out, b, n);
    free(b);
    return 0;
}

static inline char *vec_path(const char *dir, const char *rel) {
    size_t n = strlen(dir) + strlen(rel) + 2;
    char *p = malloc(n);
    if (!p) { perror("malloc"); exit(2); }
    snprintf(p, n, "%s/%s", dir, rel);
    return p;
}

/* Read a whole file; exits the test with status 2 if it is missing,
 * because a missing vector file must never count as a pass. */
static inline char *vec_read_file(const char *dir, const char *rel) {
    char *path = vec_path(dir, rel);
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("ERROR: cannot open vector file %s (run tests/host/crypto_fetch_vectors.sh)\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        printf("ERROR: cannot read %s\n", path);
        exit(2);
    }
    buf[sz] = 0;
    fclose(f);
    free(path);
    return buf;
}

/*
 * Iterate "Name = value" lines. For each one calls cb(ctx, name, value),
 * with both trimmed. "[...]" section lines call cb(ctx, "[", inner).
 * Comment (#) and blank lines call cb(ctx, "", "") so a test can close a
 * record at a blank line. The buffer is modified in place.
 */
typedef void (*vec_cb)(void *ctx, const char *name, const char *value);

static inline char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static inline void vec_parse(char *text, vec_cb cb, void *ctx) {
    char *line = text;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *t = trim(line);
        if (*t == 0 || *t == '#') {
            cb(ctx, "", "");
        } else if (*t == '[') {
            char *end = strchr(t, ']');
            if (end) *end = 0;
            cb(ctx, "[", trim(t + 1));
        } else {
            char *eq = strchr(t, '=');
            if (eq) {
                *eq = 0;
                cb(ctx, trim(t), trim(eq + 1));
            }
        }
        line = nl ? nl + 1 : NULL;
    }
    cb(ctx, "", "");   /* close the last record */
}

#endif /* CRYPTO_VEC_H */
