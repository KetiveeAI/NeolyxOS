/*
 * nxcrypto - SHA-384 and SHA-512 (FIPS 180-4 sections 4.1.3, 5.3.4,
 * 5.3.5, 6.4, 6.5)
 *
 * Constants come from tools/gen_constants.py. Timing depends only on the
 * message length. Lengths are tracked in bytes up to 2^61 - 1, so the
 * upper 64 bits of the 128-bit length field are always zero.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "nxcrypto/sha2.h"
#include "internal.h"

#define SHA2_MAX_BYTES ((uint64_t)1 << 61)

static const uint64_t K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

static const uint64_t H512[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};

static const uint64_t H384[8] = {
    0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
    0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL,
};

static inline uint64_t rotr64(uint64_t x, unsigned n) {
    return (x >> n) | (x << (64u - n));
}

/* Process one 128-byte block (FIPS 180-4 section 6.4.2). */
static void sha512_block(uint64_t st[8], const uint8_t *block) {
    uint64_t w[80];
    for (unsigned t = 0; t < 16; t++) w[t] = nxc_load64_be(block + 8 * t);
    for (unsigned t = 16; t < 80; t++) {
        uint64_t s0 = rotr64(w[t - 15], 1) ^ rotr64(w[t - 15], 8) ^ (w[t - 15] >> 7);
        uint64_t s1 = rotr64(w[t - 2], 19) ^ rotr64(w[t - 2], 61) ^ (w[t - 2] >> 6);
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }

    uint64_t a = st[0], b = st[1], c = st[2], d = st[3];
    uint64_t e = st[4], f = st[5], g = st[6], h = st[7];
    for (unsigned t = 0; t < 80; t++) {
        uint64_t S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
        uint64_t ch = (e & f) ^ (~e & g);
        uint64_t t1 = h + S1 + ch + K512[t] + w[t];
        uint64_t S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
    st[4] += e; st[5] += f; st[6] += g; st[7] += h;
    nxc_memzero(w, sizeof(w));
}

static int sha512_init_iv(nxc_sha512_ctx *ctx, const uint64_t iv[8], uint32_t dlen) {
    if (ctx == NULL) return NXC_ERR_ARG;
    for (unsigned i = 0; i < 8; i++) ctx->state[i] = iv[i];
    ctx->total = 0;
    ctx->buf_len = 0;
    ctx->digest_len = dlen;
    ctx->magic = NXC_CTX_MAGIC;
    return NXC_OK;
}

int nxc_sha384_init(nxc_sha512_ctx *ctx) {
    return sha512_init_iv(ctx, H384, NXC_SHA384_DIGEST_LEN);
}

int nxc_sha512_init(nxc_sha512_ctx *ctx) {
    return sha512_init_iv(ctx, H512, NXC_SHA512_DIGEST_LEN);
}

static int sha512_ctx_ok(const nxc_sha512_ctx *ctx) {
    return ctx->magic == NXC_CTX_MAGIC &&
           ctx->buf_len < NXC_SHA512_BLOCK_LEN &&
           (ctx->digest_len == NXC_SHA384_DIGEST_LEN ||
            ctx->digest_len == NXC_SHA512_DIGEST_LEN);
}

int nxc_sha512_update(nxc_sha512_ctx *ctx, const void *data, size_t len) {
    if (ctx == NULL || (data == NULL && len != 0)) return NXC_ERR_ARG;
    if (!sha512_ctx_ok(ctx)) return NXC_ERR_STATE;
    if ((uint64_t)len >= SHA2_MAX_BYTES - ctx->total) return NXC_ERR_LENGTH;

    const uint8_t *p = (const uint8_t *)data;
    ctx->total += len;

    if (ctx->buf_len > 0) {
        size_t take = NXC_SHA512_BLOCK_LEN - ctx->buf_len;
        if (take > len) take = len;
        nxc_copy(ctx->buf + ctx->buf_len, p, take);
        ctx->buf_len += (uint32_t)take;
        p += take;
        len -= take;
        if (ctx->buf_len < NXC_SHA512_BLOCK_LEN) return NXC_OK;
        sha512_block(ctx->state, ctx->buf);
        ctx->buf_len = 0;
    }
    while (len >= NXC_SHA512_BLOCK_LEN) {
        sha512_block(ctx->state, p);
        p += NXC_SHA512_BLOCK_LEN;
        len -= NXC_SHA512_BLOCK_LEN;
    }
    if (len > 0) {
        nxc_copy(ctx->buf, p, len);
        ctx->buf_len = (uint32_t)len;
    }
    return NXC_OK;
}

int nxc_sha512_final(nxc_sha512_ctx *ctx, uint8_t *out) {
    if (ctx == NULL || out == NULL) return NXC_ERR_ARG;
    if (!sha512_ctx_ok(ctx)) return NXC_ERR_STATE;

    /* Padding: 0x80, zeros, then the 128-bit big-endian bit length. */
    uint64_t bits = ctx->total << 3;
    uint32_t n = ctx->buf_len;
    ctx->buf[n++] = 0x80;
    if (n > NXC_SHA512_BLOCK_LEN - 16) {
        while (n < NXC_SHA512_BLOCK_LEN) ctx->buf[n++] = 0;
        sha512_block(ctx->state, ctx->buf);
        n = 0;
    }
    while (n < NXC_SHA512_BLOCK_LEN - 8) ctx->buf[n++] = 0;  /* includes high 64 bits */
    nxc_store64_be(ctx->buf + NXC_SHA512_BLOCK_LEN - 8, bits);
    sha512_block(ctx->state, ctx->buf);

    uint8_t full[NXC_SHA512_DIGEST_LEN];
    for (unsigned i = 0; i < 8; i++) nxc_store64_be(full + 8 * i, ctx->state[i]);
    nxc_copy(out, full, ctx->digest_len);
    nxc_memzero(full, sizeof(full));
    nxc_memzero(ctx, sizeof(*ctx));
    return NXC_OK;
}

int nxc_sha384(const void *data, size_t len, uint8_t out[NXC_SHA384_DIGEST_LEN]) {
    nxc_sha512_ctx ctx;
    int rc = nxc_sha384_init(&ctx);
    if (rc == NXC_OK) rc = nxc_sha512_update(&ctx, data, len);
    if (rc == NXC_OK) rc = nxc_sha512_final(&ctx, out);
    nxc_memzero(&ctx, sizeof(ctx));
    return rc;
}

int nxc_sha512(const void *data, size_t len, uint8_t out[NXC_SHA512_DIGEST_LEN]) {
    nxc_sha512_ctx ctx;
    int rc = nxc_sha512_init(&ctx);
    if (rc == NXC_OK) rc = nxc_sha512_update(&ctx, data, len);
    if (rc == NXC_OK) rc = nxc_sha512_final(&ctx, out);
    nxc_memzero(&ctx, sizeof(ctx));
    return rc;
}
