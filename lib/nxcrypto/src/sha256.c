/*
 * nxcrypto - SHA-256 (FIPS 180-4 sections 4.1.2, 5.1.1, 6.2)
 *
 * Constants come from tools/gen_constants.py. Timing depends only on the
 * message length, never on its contents.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "nxcrypto/sha2.h"
#include "internal.h"

/* Maximum message length: the bit count must fit in 64 bits. */
#define SHA2_MAX_BYTES ((uint64_t)1 << 61)

static const uint32_t K256[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static const uint32_t H256[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
};

static inline uint32_t rotr32(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32u - n));
}

/* Process one 64-byte block (FIPS 180-4 section 6.2.2). */
static void sha256_block(uint32_t st[8], const uint8_t *block) {
    uint32_t w[64];
    for (unsigned t = 0; t < 16; t++) w[t] = nxc_load32_be(block + 4 * t);
    for (unsigned t = 16; t < 64; t++) {
        uint32_t s0 = rotr32(w[t - 15], 7) ^ rotr32(w[t - 15], 18) ^ (w[t - 15] >> 3);
        uint32_t s1 = rotr32(w[t - 2], 17) ^ rotr32(w[t - 2], 19) ^ (w[t - 2] >> 10);
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }

    uint32_t a = st[0], b = st[1], c = st[2], d = st[3];
    uint32_t e = st[4], f = st[5], g = st[6], h = st[7];
    for (unsigned t = 0; t < 64; t++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + K256[t] + w[t];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
    st[4] += e; st[5] += f; st[6] += g; st[7] += h;
    nxc_memzero(w, sizeof(w));
}

int nxc_sha256_init(nxc_sha256_ctx *ctx) {
    if (ctx == NULL) return NXC_ERR_ARG;
    for (unsigned i = 0; i < 8; i++) ctx->state[i] = H256[i];
    ctx->total = 0;
    ctx->buf_len = 0;
    ctx->magic = NXC_CTX_MAGIC;
    return NXC_OK;
}

int nxc_sha256_update(nxc_sha256_ctx *ctx, const void *data, size_t len) {
    if (ctx == NULL || (data == NULL && len != 0)) return NXC_ERR_ARG;
    if (ctx->magic != NXC_CTX_MAGIC || ctx->buf_len >= NXC_SHA256_BLOCK_LEN)
        return NXC_ERR_STATE;
    if ((uint64_t)len >= SHA2_MAX_BYTES - ctx->total) return NXC_ERR_LENGTH;

    const uint8_t *p = (const uint8_t *)data;
    ctx->total += len;

    if (ctx->buf_len > 0) {
        size_t take = NXC_SHA256_BLOCK_LEN - ctx->buf_len;
        if (take > len) take = len;
        nxc_copy(ctx->buf + ctx->buf_len, p, take);
        ctx->buf_len += (uint32_t)take;
        p += take;
        len -= take;
        if (ctx->buf_len < NXC_SHA256_BLOCK_LEN) return NXC_OK;
        sha256_block(ctx->state, ctx->buf);
        ctx->buf_len = 0;
    }
    while (len >= NXC_SHA256_BLOCK_LEN) {
        sha256_block(ctx->state, p);
        p += NXC_SHA256_BLOCK_LEN;
        len -= NXC_SHA256_BLOCK_LEN;
    }
    if (len > 0) {
        nxc_copy(ctx->buf, p, len);
        ctx->buf_len = (uint32_t)len;
    }
    return NXC_OK;
}

int nxc_sha256_final(nxc_sha256_ctx *ctx, uint8_t out[NXC_SHA256_DIGEST_LEN]) {
    if (ctx == NULL || out == NULL) return NXC_ERR_ARG;
    if (ctx->magic != NXC_CTX_MAGIC || ctx->buf_len >= NXC_SHA256_BLOCK_LEN)
        return NXC_ERR_STATE;

    /* Padding: 0x80, zeros, then the 64-bit big-endian bit length. */
    uint64_t bits = ctx->total << 3;
    uint32_t n = ctx->buf_len;
    ctx->buf[n++] = 0x80;
    if (n > NXC_SHA256_BLOCK_LEN - 8) {
        while (n < NXC_SHA256_BLOCK_LEN) ctx->buf[n++] = 0;
        sha256_block(ctx->state, ctx->buf);
        n = 0;
    }
    while (n < NXC_SHA256_BLOCK_LEN - 8) ctx->buf[n++] = 0;
    nxc_store64_be(ctx->buf + NXC_SHA256_BLOCK_LEN - 8, bits);
    sha256_block(ctx->state, ctx->buf);

    for (unsigned i = 0; i < 8; i++) nxc_store32_be(out + 4 * i, ctx->state[i]);
    nxc_memzero(ctx, sizeof(*ctx));
    return NXC_OK;
}

int nxc_sha256(const void *data, size_t len, uint8_t out[NXC_SHA256_DIGEST_LEN]) {
    nxc_sha256_ctx ctx;
    int rc = nxc_sha256_init(&ctx);
    if (rc == NXC_OK) rc = nxc_sha256_update(&ctx, data, len);
    if (rc == NXC_OK) rc = nxc_sha256_final(&ctx, out);
    nxc_memzero(&ctx, sizeof(ctx));
    return rc;
}
