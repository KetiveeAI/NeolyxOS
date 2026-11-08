/*
 * nxcrypto - Poly1305 (RFC 8439 section 2.5), radix 2^26.
 *
 * The accumulator h and key r are five 26-bit limbs; products fit in
 * 64 bits (limb < 2^27, r*5 < 2^29, five terms < 2^59). The final
 * reduction mod 2^130 - 5 selects with a mask. Constant time.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "nxcrypto/aead.h"
#include "internal.h"

#define M26 0x3ffffffu

/* Split 16 little-endian bytes into five 26-bit limbs. */
static void load_limbs(uint32_t l[5], const uint8_t b[16]) {
    uint32_t t0 = nxc_load32_le(b), t1 = nxc_load32_le(b + 4);
    uint32_t t2 = nxc_load32_le(b + 8), t3 = nxc_load32_le(b + 12);
    l[0] = t0 & M26;
    l[1] = ((t0 >> 26) | (t1 << 6)) & M26;
    l[2] = ((t1 >> 20) | (t2 << 12)) & M26;
    l[3] = ((t2 >> 14) | (t3 << 18)) & M26;
    l[4] = t3 >> 8;
}

int nxc_poly1305_init(nxc_poly1305_ctx *ctx, const uint8_t key[NXC_POLY1305_KEY_LEN]) {
    if (ctx == NULL || key == NULL) return NXC_ERR_ARG;
    uint8_t r[16];
    nxc_copy(r, key, 16);
    /* Clamp (RFC 8439 section 2.5.1). */
    r[3] &= 15; r[7] &= 15; r[11] &= 15; r[15] &= 15;
    r[4] &= 252; r[8] &= 252; r[12] &= 252;
    load_limbs(ctx->r, r);
    for (int i = 0; i < 5; i++) ctx->h[i] = 0;
    for (int i = 0; i < 4; i++) ctx->pad[i] = nxc_load32_le(key + 16 + 4 * i);
    ctx->buf_len = 0;
    ctx->magic = NXC_CTX_MAGIC;
    nxc_memzero(r, sizeof(r));
    return NXC_OK;
}

/* h = (h + block + hibit * 2^128) * r mod 2^130 - 5 */
static void poly_block(nxc_poly1305_ctx *ctx, const uint8_t b[16], uint32_t hibit) {
    uint32_t m[5];
    load_limbs(m, b);
    m[4] |= hibit << 24;

    uint64_t h0 = ctx->h[0] + m[0], h1 = ctx->h[1] + m[1], h2 = ctx->h[2] + m[2];
    uint64_t h3 = ctx->h[3] + m[3], h4 = ctx->h[4] + m[4];
    uint64_t r0 = ctx->r[0], r1 = ctx->r[1], r2 = ctx->r[2], r3 = ctx->r[3], r4 = ctx->r[4];
    uint64_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;  /* 2^130 = 5 */

    uint64_t d0 = h0 * r0 + h1 * s4 + h2 * s3 + h3 * s2 + h4 * s1;
    uint64_t d1 = h0 * r1 + h1 * r0 + h2 * s4 + h3 * s3 + h4 * s2;
    uint64_t d2 = h0 * r2 + h1 * r1 + h2 * r0 + h3 * s4 + h4 * s3;
    uint64_t d3 = h0 * r3 + h1 * r2 + h2 * r1 + h3 * r0 + h4 * s4;
    uint64_t d4 = h0 * r4 + h1 * r3 + h2 * r2 + h3 * r1 + h4 * r0;

    uint64_t c;
    c = d0 >> 26; d0 &= M26; d1 += c;
    c = d1 >> 26; d1 &= M26; d2 += c;
    c = d2 >> 26; d2 &= M26; d3 += c;
    c = d3 >> 26; d3 &= M26; d4 += c;
    c = d4 >> 26; d4 &= M26; d0 += c * 5;
    c = d0 >> 26; d0 &= M26; d1 += c;

    ctx->h[0] = (uint32_t)d0; ctx->h[1] = (uint32_t)d1; ctx->h[2] = (uint32_t)d2;
    ctx->h[3] = (uint32_t)d3; ctx->h[4] = (uint32_t)d4;
    nxc_memzero(m, sizeof(m));
}

int nxc_poly1305_update(nxc_poly1305_ctx *ctx, const void *data, size_t len) {
    if (ctx == NULL || (data == NULL && len != 0)) return NXC_ERR_ARG;
    if (ctx->magic != NXC_CTX_MAGIC || ctx->buf_len >= 16) return NXC_ERR_STATE;
    const uint8_t *p = (const uint8_t *)data;

    if (ctx->buf_len > 0) {
        size_t take = 16 - ctx->buf_len;
        if (take > len) take = len;
        nxc_copy(ctx->buf + ctx->buf_len, p, take);
        ctx->buf_len += (uint32_t)take;
        p += take;
        len -= take;
        if (ctx->buf_len < 16) return NXC_OK;
        poly_block(ctx, ctx->buf, 1);
        ctx->buf_len = 0;
    }
    while (len >= 16) {
        poly_block(ctx, p, 1);
        p += 16;
        len -= 16;
    }
    if (len > 0) {
        nxc_copy(ctx->buf, p, len);
        ctx->buf_len = (uint32_t)len;
    }
    return NXC_OK;
}

int nxc_poly1305_final(nxc_poly1305_ctx *ctx, uint8_t tag[NXC_AEAD_TAG_LEN]) {
    if (ctx == NULL || tag == NULL) return NXC_ERR_ARG;
    if (ctx->magic != NXC_CTX_MAGIC || ctx->buf_len >= 16) return NXC_ERR_STATE;

    if (ctx->buf_len > 0) {         /* last partial block: append 0x01, no 2^128 bit */
        uint32_t n = ctx->buf_len;
        ctx->buf[n++] = 1;
        while (n < 16) ctx->buf[n++] = 0;
        poly_block(ctx, ctx->buf, 0);
    }

    /* Fully carry h. */
    uint32_t h0 = ctx->h[0], h1 = ctx->h[1], h2 = ctx->h[2], h3 = ctx->h[3], h4 = ctx->h[4];
    uint32_t c;
    c = h1 >> 26; h1 &= M26; h2 += c;
    c = h2 >> 26; h2 &= M26; h3 += c;
    c = h3 >> 26; h3 &= M26; h4 += c;
    c = h4 >> 26; h4 &= M26; h0 += c * 5;
    c = h0 >> 26; h0 &= M26; h1 += c;

    /* g = h + 5 - 2^130; use g when it did not go negative (h >= p). */
    uint32_t g0 = h0 + 5;  c = g0 >> 26; g0 &= M26;
    uint32_t g1 = h1 + c;  c = g1 >> 26; g1 &= M26;
    uint32_t g2 = h2 + c;  c = g2 >> 26; g2 &= M26;
    uint32_t g3 = h3 + c;  c = g3 >> 26; g3 &= M26;
    uint32_t g4 = h4 + c - (1u << 26);
    uint32_t use_g = (g4 >> 31) - 1u;       /* all ones if g4 >= 0 */
    h0 = (h0 & ~use_g) | (g0 & use_g);
    h1 = (h1 & ~use_g) | (g1 & use_g);
    h2 = (h2 & ~use_g) | (g2 & use_g);
    h3 = (h3 & ~use_g) | (g3 & use_g);
    h4 = (h4 & ~use_g) | (g4 & use_g & M26);

    /* tag = (h + pad) mod 2^128 */
    uint32_t w0 = h0 | (h1 << 26);
    uint32_t w1 = (h1 >> 6) | (h2 << 20);
    uint32_t w2 = (h2 >> 12) | (h3 << 14);
    uint32_t w3 = (h3 >> 18) | (h4 << 8);
    uint64_t f;
    f = (uint64_t)w0 + ctx->pad[0];             nxc_store32_le(tag, (uint32_t)f);
    f = (uint64_t)w1 + ctx->pad[1] + (f >> 32); nxc_store32_le(tag + 4, (uint32_t)f);
    f = (uint64_t)w2 + ctx->pad[2] + (f >> 32); nxc_store32_le(tag + 8, (uint32_t)f);
    f = (uint64_t)w3 + ctx->pad[3] + (f >> 32); nxc_store32_le(tag + 12, (uint32_t)f);

    nxc_memzero(ctx, sizeof(*ctx));
    return NXC_OK;
}
