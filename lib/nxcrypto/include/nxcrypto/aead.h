/*
 * nxcrypto - ChaCha20, Poly1305 and ChaCha20-Poly1305 (RFC 8439),
 * AES-128/256 (FIPS 197) and AES-GCM (NIST SP 800-38D).
 *
 * All of it runs in constant time: AES computes its S-box arithmetically
 * (no lookup tables indexed by secret bytes) and GHASH multiplies with
 * masks, not branches.
 *
 * AEAD rules for callers:
 *   - Never reuse a (key, nonce) pair. TLS derives nonces from a record
 *     sequence number; anything else must do the same or use random.get.
 *   - open() never releases plaintext from a record whose tag does not
 *     match: it returns NXC_ERR_VERIFY and the output buffer is all
 *     zeros. (It decrypts and then masks, so a forgery takes the same
 *     time as a valid record.)
 *   - out may equal in (in-place), but must not otherwise overlap it.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXCRYPTO_AEAD_H
#define NXCRYPTO_AEAD_H

#include "nxcrypto/common.h"

#define NXC_CHACHA20_KEY_LEN    32u
#define NXC_CHACHA20_NONCE_LEN  12u
#define NXC_POLY1305_KEY_LEN    32u
#define NXC_AEAD_TAG_LEN        16u
#define NXC_GCM_NONCE_LEN       12u

/* ------------------------------------------------------------------ */
/* ChaCha20 stream cipher (RFC 8439 section 2.4)                       */
/* ------------------------------------------------------------------ */

/*
 * out = in XOR keystream(key, nonce, starting at block `counter`).
 * Fails with NXC_ERR_LENGTH if the 32-bit block counter would wrap
 * (more than 256 GB from one nonce).
 */
int nxc_chacha20_xor(uint8_t *out, const uint8_t *in, size_t len,
                     const uint8_t key[NXC_CHACHA20_KEY_LEN],
                     const uint8_t nonce[NXC_CHACHA20_NONCE_LEN],
                     uint32_t counter);

/* ------------------------------------------------------------------ */
/* Poly1305 one-time authenticator (RFC 8439 section 2.5)              */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t r[5];
    uint32_t h[5];
    uint32_t pad[4];
    uint8_t  buf[16];
    uint32_t buf_len;
    uint32_t magic;
} nxc_poly1305_ctx;

/* The key must be used for one message only. */
int nxc_poly1305_init(nxc_poly1305_ctx *ctx, const uint8_t key[NXC_POLY1305_KEY_LEN]);
int nxc_poly1305_update(nxc_poly1305_ctx *ctx, const void *data, size_t len);
int nxc_poly1305_final(nxc_poly1305_ctx *ctx, uint8_t tag[NXC_AEAD_TAG_LEN]);

/* ------------------------------------------------------------------ */
/* ChaCha20-Poly1305 AEAD (RFC 8439 section 2.8)                       */
/* ------------------------------------------------------------------ */

/* Encrypt len bytes and compute the 16-byte tag over aad and ciphertext. */
int nxc_chacha20poly1305_seal(uint8_t *out, uint8_t tag[NXC_AEAD_TAG_LEN],
                              const uint8_t key[NXC_CHACHA20_KEY_LEN],
                              const uint8_t nonce[NXC_CHACHA20_NONCE_LEN],
                              const void *aad, size_t aad_len,
                              const uint8_t *in, size_t len);

/* Decrypt and verify. NXC_ERR_VERIFY, with out zeroed, on a bad tag. */
int nxc_chacha20poly1305_open(uint8_t *out,
                              const uint8_t key[NXC_CHACHA20_KEY_LEN],
                              const uint8_t nonce[NXC_CHACHA20_NONCE_LEN],
                              const void *aad, size_t aad_len,
                              const uint8_t *in, size_t len,
                              const uint8_t tag[NXC_AEAD_TAG_LEN]);

/* ------------------------------------------------------------------ */
/* AES block cipher (encryption direction only) and AES-GCM            */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t rk[60];        /* round keys, 4 words per round */
    uint32_t rounds;        /* 10 (AES-128) or 14 (AES-256) */
    uint32_t magic;
} nxc_aes_ctx;

/* key_len is 16 or 32. */
int nxc_aes_init(nxc_aes_ctx *ctx, const uint8_t *key, size_t key_len);
int nxc_aes_encrypt_block(const nxc_aes_ctx *ctx, uint8_t out[16], const uint8_t in[16]);
void nxc_aes_wipe(nxc_aes_ctx *ctx);

typedef struct {
    nxc_aes_ctx aes;
    uint64_t h_hi, h_lo;    /* GHASH key H = E(K, 0^128) */
} nxc_gcm_ctx;

/* Expand the key once per connection direction; key_len 16 or 32. */
int nxc_gcm_init(nxc_gcm_ctx *ctx, const uint8_t *key, size_t key_len);
void nxc_gcm_wipe(nxc_gcm_ctx *ctx);

/* 96-bit nonce only (the only size TLS uses; SP 800-38D recommends it). */
int nxc_gcm_seal(const nxc_gcm_ctx *ctx, uint8_t *out, uint8_t tag[NXC_AEAD_TAG_LEN],
                 const uint8_t nonce[NXC_GCM_NONCE_LEN],
                 const void *aad, size_t aad_len,
                 const uint8_t *in, size_t len);

int nxc_gcm_open(const nxc_gcm_ctx *ctx, uint8_t *out,
                 const uint8_t nonce[NXC_GCM_NONCE_LEN],
                 const void *aad, size_t aad_len,
                 const uint8_t *in, size_t len,
                 const uint8_t tag[NXC_AEAD_TAG_LEN]);

#endif /* NXCRYPTO_AEAD_H */
