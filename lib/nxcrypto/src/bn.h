/*
 * nxcrypto - fixed-size big integers and Montgomery arithmetic (private).
 *
 * Used only for *verification* (RSA, ECDSA), where every input is
 * public: signatures, public keys and message hashes. These routines are
 * therefore allowed to take time that depends on their inputs. They are
 * never used on secret values.
 *
 * Numbers are little-endian arrays of 64-bit limbs, with an explicit
 * limb count. Moduli are odd, 1..NXC_BN_MAX_LIMBS limbs (up to 4096 bits).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXCRYPTO_BN_H
#define NXCRYPTO_BN_H

#include <stddef.h>
#include <stdint.h>

#define NXC_BN_MAX_LIMBS 64     /* 4096 bits */

typedef struct {
    uint64_t m[NXC_BN_MAX_LIMBS];     /* modulus */
    uint64_t rr[NXC_BN_MAX_LIMBS];    /* R^2 mod m, R = 2^(64n) */
    uint64_t one[NXC_BN_MAX_LIMBS];   /* R mod m (1 in Montgomery form) */
    uint64_t m0inv;                   /* -m^-1 mod 2^64 */
    uint32_t n;                       /* limbs */
} nxc_mont;

/* Big-endian bytes -> n limbs. Returns 0, or -1 if the value needs more
 * than n limbs (leading zero bytes are allowed). */
int  nxc_bn_from_be(uint64_t *x, uint32_t n, const uint8_t *in, size_t len);
/* n limbs -> exactly len big-endian bytes; -1 if the value does not fit. */
int  nxc_bn_to_be(uint8_t *out, size_t len, const uint64_t *x, uint32_t n);
int  nxc_bn_cmp(const uint64_t *a, const uint64_t *b, uint32_t n);   /* -1, 0, 1 */
int  nxc_bn_is_zero(const uint64_t *a, uint32_t n);
uint32_t nxc_bn_bits(const uint64_t *a, uint32_t n);
/* r = a - b, returns the borrow (0 or 1). r may alias a or b. */
uint64_t nxc_bn_sub(uint64_t *r, const uint64_t *a, const uint64_t *b, uint32_t n);

/* Set up arithmetic mod an odd modulus given as big-endian bytes.
 * Returns 0, or -1 if the modulus is even, below 3, or too large. */
int  nxc_mont_init(nxc_mont *ctx, const uint8_t *mod, size_t len);

/* All values below are < m and n = ctx->n limbs long. */
void nxc_mont_mul(const nxc_mont *ctx, uint64_t *r, const uint64_t *a, const uint64_t *b);
void nxc_mont_to(const nxc_mont *ctx, uint64_t *r, const uint64_t *a);    /* a*R mod m */
void nxc_mont_from(const nxc_mont *ctx, uint64_t *r, const uint64_t *a);  /* a/R mod m */
void nxc_mod_add(const nxc_mont *ctx, uint64_t *r, const uint64_t *a, const uint64_t *b);
void nxc_mod_sub(const nxc_mont *ctx, uint64_t *r, const uint64_t *a, const uint64_t *b);
/* r = a^e (a and r in Montgomery form, e as big-endian bytes). */
void nxc_mont_exp(const nxc_mont *ctx, uint64_t *r, const uint64_t *a,
                  const uint8_t *e, size_t e_len);

#endif /* NXCRYPTO_BN_H */
