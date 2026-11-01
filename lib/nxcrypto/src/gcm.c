/*
 * nxcrypto - AES-GCM with 96-bit nonces (NIST SP 800-38D).
 *
 * GHASH multiplies in GF(2^128) bit by bit, selecting with masks
 * (section 6.3, Algorithm 1), so time does not depend on H or the data.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "nxcrypto/aead.h"
#include "internal.h"

/* Plaintext limit for one nonce: 2^39 - 256 bits (section 5.2.1.1). */
#define GCM_MAX_BYTES ((((uint64_t)1 << 39) - 256) / 8)

/* (y_hi, y_lo) = (y_hi, y_lo) * H, blocks as big-endian 64-bit halves. */
static void gf128_mul(uint64_t *y_hi, uint64_t *y_lo, uint64_t h_hi, uint64_t h_lo) {
    uint64_t x_hi = *y_hi, x_lo = *y_lo;
    uint64_t z_hi = 0, z_lo = 0, v_hi = h_hi, v_lo = h_lo;
    for (int i = 0; i < 128; i++) {
        /* bit i of X, counting from the most significant bit of byte 0 */
        uint64_t bit = i < 64 ? (x_hi >> (63 - i)) & 1u : (x_lo >> (127 - i)) & 1u;
        uint64_t m = (uint64_t)0 - bit;
        z_hi ^= v_hi & m;
        z_lo ^= v_lo & m;
        uint64_t lsb = (uint64_t)0 - (v_lo & 1u);
        v_lo = (v_lo >> 1) | (v_hi << 63);
        v_hi = (v_hi >> 1) ^ (0xe100000000000000ULL & lsb);
    }
    *y_hi = z_hi;
    *y_lo = z_lo;
}

/* Absorb data (zero padded to 16 bytes) into the GHASH state. */
static void ghash_update(const nxc_gcm_ctx *ctx, uint64_t *y_hi, uint64_t *y_lo,
                         const uint8_t *p, size_t len) {
    uint8_t block[16];
    while (len > 0) {
        size_t n = len < 16 ? len : 16;
        for (size_t i = 0; i < 16; i++) block[i] = i < n ? p[i] : 0;
        *y_hi ^= nxc_load64_be(block);
        *y_lo ^= nxc_load64_be(block + 8);
        gf128_mul(y_hi, y_lo, ctx->h_hi, ctx->h_lo);
        p += n;
        len -= n;
    }
    nxc_memzero(block, sizeof(block));
}

int nxc_gcm_init(nxc_gcm_ctx *ctx, const uint8_t *key, size_t key_len) {
    if (ctx == NULL) return NXC_ERR_ARG;
    int rc = nxc_aes_init(&ctx->aes, key, key_len);
    if (rc != NXC_OK) return rc;
    uint8_t zero[16] = { 0 }, h[16];
    rc = nxc_aes_encrypt_block(&ctx->aes, h, zero);
    ctx->h_hi = nxc_load64_be(h);
    ctx->h_lo = nxc_load64_be(h + 8);
    nxc_memzero(h, sizeof(h));
    if (rc != NXC_OK) nxc_gcm_wipe(ctx);
    return rc;
}

void nxc_gcm_wipe(nxc_gcm_ctx *ctx) {
    if (ctx != NULL) nxc_memzero(ctx, sizeof(*ctx));
}

/* CTR keystream from counter block inc32(J0) onward; out = in ^ keystream. */
static int gcm_ctr(const nxc_gcm_ctx *ctx, const uint8_t j0[16],
                   uint8_t *out, const uint8_t *in, size_t len) {
    uint8_t cb[16], ks[16];
    nxc_copy(cb, j0, 16);
    uint32_t ctr = nxc_load32_be(cb + 12);
    int rc = NXC_OK;
    while (len > 0 && rc == NXC_OK) {
        ctr++;
        nxc_store32_be(cb + 12, ctr);
        rc = nxc_aes_encrypt_block(&ctx->aes, ks, cb);
        size_t n = len < 16 ? len : 16;
        for (size_t i = 0; i < n; i++) out[i] = (uint8_t)(in[i] ^ ks[i]);
        out += n;
        in += n;
        len -= n;
    }
    nxc_memzero(ks, sizeof(ks));
    return rc;
}

/* tag = E(K, J0) ^ GHASH(H, aad, ct) */
static int gcm_tag(const nxc_gcm_ctx *ctx, const uint8_t j0[16], uint8_t tag[16],
                   const void *aad, size_t aad_len, const uint8_t *ct, size_t len) {
    uint64_t y_hi = 0, y_lo = 0;
    ghash_update(ctx, &y_hi, &y_lo, (const uint8_t *)aad, aad_len);
    ghash_update(ctx, &y_hi, &y_lo, ct, len);
    y_hi ^= (uint64_t)aad_len * 8;     /* bit lengths */
    y_lo ^= (uint64_t)len * 8;
    gf128_mul(&y_hi, &y_lo, ctx->h_hi, ctx->h_lo);

    uint8_t ek[16];
    int rc = nxc_aes_encrypt_block(&ctx->aes, ek, j0);
    nxc_store64_be(tag, y_hi ^ nxc_load64_be(ek));
    nxc_store64_be(tag + 8, y_lo ^ nxc_load64_be(ek + 8));
    nxc_memzero(ek, sizeof(ek));
    return rc;
}

static int gcm_args_ok(const nxc_gcm_ctx *ctx, const void *out, const void *nonce,
                       const void *aad, size_t aad_len, const void *in, size_t len) {
    if (ctx == NULL || nonce == NULL) return 0;
    if (aad == NULL && aad_len != 0) return 0;
    if (len != 0 && (in == NULL || out == NULL)) return 0;
    if ((uint64_t)len > GCM_MAX_BYTES) return 0;
    if ((uint64_t)aad_len > ((uint64_t)1 << 61) - 1) return 0;
    return 1;
}

static void make_j0(uint8_t j0[16], const uint8_t nonce[12]) {
    nxc_copy(j0, nonce, 12);
    j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
}

int nxc_gcm_seal(const nxc_gcm_ctx *ctx, uint8_t *out, uint8_t tag[NXC_AEAD_TAG_LEN],
                 const uint8_t nonce[NXC_GCM_NONCE_LEN],
                 const void *aad, size_t aad_len,
                 const uint8_t *in, size_t len) {
    if (tag == NULL || !gcm_args_ok(ctx, out, nonce, aad, aad_len, in, len)) return NXC_ERR_ARG;
    uint8_t j0[16];
    make_j0(j0, nonce);
    int rc = gcm_ctr(ctx, j0, out, in, len);
    if (rc == NXC_OK) rc = gcm_tag(ctx, j0, tag, aad, aad_len, out, len);
    if (rc != NXC_OK) {
        if (out != NULL) nxc_memzero(out, len);
        nxc_memzero(tag, NXC_AEAD_TAG_LEN);
    }
    return rc;
}

int nxc_gcm_open(const nxc_gcm_ctx *ctx, uint8_t *out,
                 const uint8_t nonce[NXC_GCM_NONCE_LEN],
                 const void *aad, size_t aad_len,
                 const uint8_t *in, size_t len,
                 const uint8_t tag[NXC_AEAD_TAG_LEN]) {
    if (tag == NULL || !gcm_args_ok(ctx, out, nonce, aad, aad_len, in, len)) return NXC_ERR_ARG;
    uint8_t j0[16], want[16];
    make_j0(j0, nonce);
    int rc = gcm_tag(ctx, j0, want, aad, aad_len, in, len);
    int vrc = NXC_OK;           /* verification result, kept out of rc's branches */
    if (rc == NXC_OK) {
        /* As in ChaCha20-Poly1305: no branch on the tag comparison. */
        uint32_t ok = (uint32_t)nxc_ct_equal(want, tag, 16);
        rc = gcm_ctr(ctx, j0, out, in, len);
        if (rc == NXC_OK) vrc = nxc__aead_keep_if(out, len, ok);
    }
    if (rc != NXC_OK && out != NULL) nxc_memzero(out, len);
    nxc_memzero(want, sizeof(want));
    return rc != NXC_OK ? rc : vrc;
}
