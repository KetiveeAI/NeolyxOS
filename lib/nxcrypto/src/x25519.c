/*
 * nxcrypto - X25519 (RFC 7748 section 5), Montgomery ladder.
 *
 * The ladder runs a fixed 255 steps with conditional swaps driven by
 * masks, so its timing does not depend on the scalar.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "nxcrypto/curve25519.h"
#include "fe25519.h"
#include "internal.h"

/* (A - 2) / 4 for curve25519, A = 486662 (RFC 7748 section 5). */
#define X25519_A24 121665u

int nxc_x25519(uint8_t out[NXC_X25519_KEY_LEN],
               const uint8_t scalar[NXC_X25519_KEY_LEN],
               const uint8_t point[NXC_X25519_KEY_LEN]) {
    if (out == NULL || scalar == NULL || point == NULL) return NXC_ERR_ARG;

    uint8_t k[32];
    nxc_copy(k, scalar, 32);
    k[0] &= 248;            /* clear the cofactor bits */
    k[31] &= 127;
    k[31] |= 64;            /* fixed top bit: same ladder length for all keys */

    fe25519 x1, x2, z2, x3, z3, a, aa, b, bb, e, c, d, da, cb, t;
    nxc_fe_frombytes(&x1, point);          /* bit 255 of the u-coordinate is ignored */
    nxc_fe_1(&x2);
    nxc_fe_0(&z2);
    nxc_fe_copy(&x3, &x1);
    nxc_fe_1(&z3);

    uint32_t swap = 0;
    for (int pos = 254; pos >= 0; pos--) {
        uint32_t bit = (uint32_t)(k[pos >> 3] >> (pos & 7)) & 1u;
        swap ^= bit;
        nxc_fe_cswap(&x2, &x3, swap);
        nxc_fe_cswap(&z2, &z3, swap);
        swap = bit;

        nxc_fe_add(&a, &x2, &z2);          /* A  = x2 + z2 */
        nxc_fe_sq(&aa, &a);                /* AA = A^2 */
        nxc_fe_sub(&b, &x2, &z2);          /* B  = x2 - z2 */
        nxc_fe_sq(&bb, &b);                /* BB = B^2 */
        nxc_fe_sub(&e, &aa, &bb);          /* E  = AA - BB */
        nxc_fe_add(&c, &x3, &z3);          /* C  = x3 + z3 */
        nxc_fe_sub(&d, &x3, &z3);          /* D  = x3 - z3 */
        nxc_fe_mul(&da, &d, &a);           /* DA = D * A */
        nxc_fe_mul(&cb, &c, &b);           /* CB = C * B */
        nxc_fe_add(&t, &da, &cb);
        nxc_fe_sq(&x3, &t);                /* x3 = (DA + CB)^2 */
        nxc_fe_sub(&t, &da, &cb);
        nxc_fe_sq(&t, &t);
        nxc_fe_mul(&z3, &x1, &t);          /* z3 = x1 * (DA - CB)^2 */
        nxc_fe_mul(&x2, &aa, &bb);         /* x2 = AA * BB */
        nxc_fe_mul_small(&t, &e, X25519_A24);
        nxc_fe_add(&t, &aa, &t);
        nxc_fe_mul(&z2, &e, &t);           /* z2 = E * (AA + a24 * E) */
    }
    nxc_fe_cswap(&x2, &x3, swap);
    nxc_fe_cswap(&z2, &z3, swap);

    nxc_fe_invert(&z2, &z2);
    nxc_fe_mul(&x2, &x2, &z2);
    nxc_fe_tobytes(out, &x2);

    /* All-zero output means a low-order input point (RFC 7748 section 6.1).
     * Computed without a branch on the (secret) output bytes. */
    uint32_t acc = 0;
    for (int i = 0; i < 32; i++) acc |= out[i];
    uint32_t is_zero = ((acc - 1u) >> 8) & 1u;          /* 1 iff acc == 0 */
    int rc = (int)((uint32_t)0 - is_zero) & NXC_ERR_WEAK; /* 0 or NXC_ERR_WEAK */

    nxc_memzero(k, sizeof(k));
    nxc_memzero(&x2, sizeof(x2)); nxc_memzero(&z2, sizeof(z2));
    nxc_memzero(&x3, sizeof(x3)); nxc_memzero(&z3, sizeof(z3));
    nxc_memzero(&a, sizeof(a));   nxc_memzero(&aa, sizeof(aa));
    nxc_memzero(&b, sizeof(b));   nxc_memzero(&bb, sizeof(bb));
    nxc_memzero(&e, sizeof(e));   nxc_memzero(&c, sizeof(c));
    nxc_memzero(&d, sizeof(d));   nxc_memzero(&da, sizeof(da));
    nxc_memzero(&cb, sizeof(cb)); nxc_memzero(&t, sizeof(t));
    nxc_memzero(&x1, sizeof(x1));

    return rc;
}

int nxc_x25519_public(uint8_t out[NXC_X25519_KEY_LEN],
                      const uint8_t scalar[NXC_X25519_KEY_LEN]) {
    static const uint8_t base[32] = { 9 };
    return nxc_x25519(out, scalar, base);
}
