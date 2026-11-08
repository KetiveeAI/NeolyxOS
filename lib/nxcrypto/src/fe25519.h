/*
 * nxcrypto - arithmetic in GF(2^255 - 19) (private).
 *
 * An element is five unsigned 51-bit limbs: v = sum v[i] * 2^(51*i).
 * Limbs may exceed 51 bits slightly between operations ("loosely
 * reduced", every limb < 2^52); nxc_fe_tobytes() gives the unique canonical
 * encoding. All functions run in constant time.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXCRYPTO_FE25519_H
#define NXCRYPTO_FE25519_H

#include <stdint.h>

typedef struct { uint64_t v[5]; } fe25519;

void nxc_fe_0(fe25519 *h);
void nxc_fe_1(fe25519 *h);
void nxc_fe_copy(fe25519 *h, const fe25519 *f);
/* Reads 255 bits (bit 255 is ignored); values >= p are accepted and reduced. */
void nxc_fe_frombytes(fe25519 *h, const uint8_t s[32]);
void nxc_fe_tobytes(uint8_t s[32], const fe25519 *h);
void nxc_fe_add(fe25519 *h, const fe25519 *f, const fe25519 *g);
void nxc_fe_sub(fe25519 *h, const fe25519 *f, const fe25519 *g);
void nxc_fe_neg(fe25519 *h, const fe25519 *f);
void nxc_fe_mul(fe25519 *h, const fe25519 *f, const fe25519 *g);
void nxc_fe_sq(fe25519 *h, const fe25519 *f);
void nxc_fe_mul_small(fe25519 *h, const fe25519 *f, uint32_t k);
void nxc_fe_invert(fe25519 *out, const fe25519 *z);        /* z^(p-2); 0 -> 0 */
void nxc_fe_pow22523(fe25519 *out, const fe25519 *z);      /* z^((p-5)/8) */
/* Swap / move when b == 1, do nothing when b == 0 (b must be 0 or 1). */
void nxc_fe_cswap(fe25519 *f, fe25519 *g, uint32_t b);
void nxc_fe_cmov(fe25519 *f, const fe25519 *g, uint32_t b);
int  nxc_fe_isnonzero(const fe25519 *f);   /* 1 if f != 0 mod p */
int  nxc_fe_isnegative(const fe25519 *f);  /* low bit of the canonical form */

#endif /* NXCRYPTO_FE25519_H */
