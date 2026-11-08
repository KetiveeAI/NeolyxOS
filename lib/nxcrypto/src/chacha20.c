/*
 * nxcrypto - ChaCha20 (RFC 8439 section 2.3, 2.4).
 *
 * Only additions, XORs and fixed rotations: constant time by
 * construction.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "nxcrypto/aead.h"
#include "internal.h"

static inline uint32_t rotl32(uint32_t x, unsigned n) {
    return (x << n) | (x >> (32u - n));
}

#define QR(a, b, c, d) do {                      \
        a += b; d ^= a; d = rotl32(d, 16);       \
        c += d; b ^= c; b = rotl32(b, 12);       \
        a += b; d ^= a; d = rotl32(d, 8);        \
        c += d; b ^= c; b = rotl32(b, 7);        \
    } while (0)

/* One 64-byte keystream block (RFC 8439 section 2.3). */
static void chacha20_block(uint8_t out[64], const uint8_t key[32],
                           uint32_t counter, const uint8_t nonce[12]) {
    uint32_t in[16], x[16];
    in[0] = 0x61707865u;    /* "expa" */
    in[1] = 0x3320646eu;    /* "nd 3" */
    in[2] = 0x79622d32u;    /* "2-by" */
    in[3] = 0x6b206574u;    /* "te k" */
    for (int i = 0; i < 8; i++) in[4 + i] = nxc_load32_le(key + 4 * i);
    in[12] = counter;
    for (int i = 0; i < 3; i++) in[13 + i] = nxc_load32_le(nonce + 4 * i);

    for (int i = 0; i < 16; i++) x[i] = in[i];
    for (int i = 0; i < 10; i++) {          /* 20 rounds = 10 double rounds */
        QR(x[0], x[4], x[8],  x[12]);       /* columns */
        QR(x[1], x[5], x[9],  x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]);       /* diagonals */
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[8],  x[13]);
        QR(x[3], x[4], x[9],  x[14]);
    }
    for (int i = 0; i < 16; i++) nxc_store32_le(out + 4 * i, x[i] + in[i]);
    nxc_memzero(in, sizeof(in));
    nxc_memzero(x, sizeof(x));
}

int nxc_chacha20_xor(uint8_t *out, const uint8_t *in, size_t len,
                     const uint8_t key[NXC_CHACHA20_KEY_LEN],
                     const uint8_t nonce[NXC_CHACHA20_NONCE_LEN],
                     uint32_t counter) {
    if (key == NULL || nonce == NULL) return NXC_ERR_ARG;
    if (len != 0 && (out == NULL || in == NULL)) return NXC_ERR_ARG;
    /* Blocks needed must fit before the 32-bit counter wraps. */
    uint64_t blocks = ((uint64_t)len + 63) / 64;
    if (blocks > (uint64_t)0x100000000ULL - counter) return NXC_ERR_LENGTH;

    uint8_t ks[64];
    while (len > 0) {
        chacha20_block(ks, key, counter, nonce);
        size_t n = len < 64 ? len : 64;
        for (size_t i = 0; i < n; i++) out[i] = (uint8_t)(in[i] ^ ks[i]);
        out += n;
        in += n;
        len -= n;
        counter++;
    }
    nxc_memzero(ks, sizeof(ks));
    return NXC_OK;
}

/* Poly1305 one-time key for an AEAD message: first 32 bytes of block 0. */
void nxc__chacha20_poly_key(uint8_t otk[32], const uint8_t key[32], const uint8_t nonce[12]);
void nxc__chacha20_poly_key(uint8_t otk[32], const uint8_t key[32], const uint8_t nonce[12]) {
    uint8_t block[64];
    chacha20_block(block, key, 0, nonce);
    nxc_copy(otk, block, 32);
    nxc_memzero(block, sizeof(block));
}
