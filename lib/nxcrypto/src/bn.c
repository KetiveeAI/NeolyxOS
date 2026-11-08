/*
 * nxcrypto - big integers and Montgomery multiplication (see bn.h).
 *
 * Montgomery multiplication follows the coarsely integrated
 * operand-scanning method (Koc, Acar, Kaliski 1996): for each limb of b,
 * add a * b_i, then add the multiple of m that clears the lowest limb and
 * shift one limb down. The result is < 2m and one subtraction finishes.
 *
 * Variable time: public inputs only (see bn.h).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "bn.h"
#include "internal.h"

typedef unsigned __int128 u128;

int nxc_bn_from_be(uint64_t *x, uint32_t n, const uint8_t *in, size_t len) {
    for (uint32_t i = 0; i < n; i++) x[i] = 0;
    for (size_t i = 0; i < len; i++) {
        size_t bit = (len - 1 - i) * 8;          /* position of this byte */
        if (bit / 64 >= n) {
            if (in[i] != 0) return -1;
            continue;
        }
        x[bit / 64] |= (uint64_t)in[i] << (bit % 64);
    }
    return 0;
}

int nxc_bn_to_be(uint8_t *out, size_t len, const uint64_t *x, uint32_t n) {
    for (size_t i = 0; i < len; i++) {
        size_t bit = (len - 1 - i) * 8;
        out[i] = bit / 64 < n ? (uint8_t)(x[bit / 64] >> (bit % 64)) : 0;
    }
    /* Anything above len bytes must be zero. */
    for (size_t bit = len * 8; bit < (size_t)n * 64; bit += 8)
        if ((uint8_t)(x[bit / 64] >> (bit % 64)) != 0) return -1;
    return 0;
}

int nxc_bn_cmp(const uint64_t *a, const uint64_t *b, uint32_t n) {
    for (uint32_t i = n; i-- > 0;) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

int nxc_bn_is_zero(const uint64_t *a, uint32_t n) {
    uint64_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= a[i];
    return acc == 0;
}

uint32_t nxc_bn_bits(const uint64_t *a, uint32_t n) {
    for (uint32_t i = n; i-- > 0;) {
        if (a[i] != 0) {
            uint32_t b = 64;
            while (!(a[i] >> (b - 1))) b--;
            return i * 64 + b;
        }
    }
    return 0;
}

uint64_t nxc_bn_sub(uint64_t *r, const uint64_t *a, const uint64_t *b, uint32_t n) {
    uint64_t borrow = 0;
    for (uint32_t i = 0; i < n; i++) {
        u128 t = (u128)a[i] - b[i] - borrow;
        r[i] = (uint64_t)t;
        borrow = (uint64_t)(t >> 64) & 1u;
    }
    return borrow;
}

static uint64_t bn_add(uint64_t *r, const uint64_t *a, const uint64_t *b, uint32_t n) {
    uint64_t carry = 0;
    for (uint32_t i = 0; i < n; i++) {
        u128 t = (u128)a[i] + b[i] + carry;
        r[i] = (uint64_t)t;
        carry = (uint64_t)(t >> 64);
    }
    return carry;
}

void nxc_mod_add(const nxc_mont *ctx, uint64_t *r, const uint64_t *a, const uint64_t *b) {
    uint64_t t[NXC_BN_MAX_LIMBS];
    uint64_t carry = bn_add(t, a, b, ctx->n);
    if (carry || nxc_bn_cmp(t, ctx->m, ctx->n) >= 0) nxc_bn_sub(t, t, ctx->m, ctx->n);
    nxc_copy(r, t, (size_t)ctx->n * 8);
}

void nxc_mod_sub(const nxc_mont *ctx, uint64_t *r, const uint64_t *a, const uint64_t *b) {
    uint64_t t[NXC_BN_MAX_LIMBS];
    if (nxc_bn_sub(t, a, b, ctx->n)) bn_add(t, t, ctx->m, ctx->n);
    nxc_copy(r, t, (size_t)ctx->n * 8);
}

void nxc_mont_mul(const nxc_mont *ctx, uint64_t *r, const uint64_t *a, const uint64_t *b) {
    const uint32_t n = ctx->n;
    uint64_t t[NXC_BN_MAX_LIMBS + 2];
    for (uint32_t i = 0; i < n + 2; i++) t[i] = 0;

    for (uint32_t i = 0; i < n; i++) {
        /* t += a * b[i] */
        uint64_t c = 0;
        for (uint32_t j = 0; j < n; j++) {
            u128 s = (u128)a[j] * b[i] + t[j] + c;
            t[j] = (uint64_t)s;
            c = (uint64_t)(s >> 64);
        }
        u128 s = (u128)t[n] + c;
        t[n] = (uint64_t)s;
        t[n + 1] = (uint64_t)(s >> 64);

        /* t = (t + u*m) / 2^64 with u chosen so the low limb becomes 0 */
        uint64_t u = t[0] * ctx->m0inv;
        s = (u128)u * ctx->m[0] + t[0];
        c = (uint64_t)(s >> 64);
        for (uint32_t j = 1; j < n; j++) {
            s = (u128)u * ctx->m[j] + t[j] + c;
            t[j - 1] = (uint64_t)s;
            c = (uint64_t)(s >> 64);
        }
        s = (u128)t[n] + c;
        t[n - 1] = (uint64_t)s;
        t[n] = t[n + 1] + (uint64_t)(s >> 64);
    }
    /* t < 2m: subtract once if needed. */
    if (t[n] != 0 || nxc_bn_cmp(t, ctx->m, n) >= 0) nxc_bn_sub(t, t, ctx->m, n);
    nxc_copy(r, t, (size_t)n * 8);
}

void nxc_mont_to(const nxc_mont *ctx, uint64_t *r, const uint64_t *a) {
    nxc_mont_mul(ctx, r, a, ctx->rr);
}

void nxc_mont_from(const nxc_mont *ctx, uint64_t *r, const uint64_t *a) {
    uint64_t one[NXC_BN_MAX_LIMBS];
    for (uint32_t i = 0; i < ctx->n; i++) one[i] = 0;
    one[0] = 1;
    nxc_mont_mul(ctx, r, a, one);
}

int nxc_mont_init(nxc_mont *ctx, const uint8_t *mod, size_t len) {
    if (ctx == NULL || mod == NULL || len == 0) return -1;
    /* Limbs from the significant length. */
    size_t first = 0;
    while (first < len && mod[first] == 0) first++;
    size_t sig = len - first;
    if (sig == 0 || sig > (size_t)NXC_BN_MAX_LIMBS * 8) return -1;
    uint32_t n = (uint32_t)((sig + 7) / 8);
    ctx->n = n;
    if (nxc_bn_from_be(ctx->m, n, mod + first, sig) != 0) return -1;
    if ((ctx->m[0] & 1u) == 0) return -1;                 /* must be odd */
    if (n == 1 && ctx->m[0] < 3) return -1;

    /* m0inv = -m[0]^-1 mod 2^64 by Newton iteration (each step doubles
     * the number of correct low bits: 1, 2, 4, ..., 64). */
    uint64_t x = 1;
    for (int i = 0; i < 6; i++) x *= 2 - ctx->m[0] * x;
    ctx->m0inv = (uint64_t)0 - x;

    /* R mod m and R^2 mod m by repeated doubling of 1 (setup only). */
    uint64_t t[NXC_BN_MAX_LIMBS];
    for (uint32_t i = 0; i < n; i++) t[i] = 0;
    t[0] = 1;
    for (uint32_t i = 0; i < 2 * 64 * n; i++) {
        uint64_t top = t[n - 1] >> 63;
        for (uint32_t j = n - 1; j > 0; j--) t[j] = (t[j] << 1) | (t[j - 1] >> 63);
        t[0] <<= 1;
        if (top || nxc_bn_cmp(t, ctx->m, n) >= 0) nxc_bn_sub(t, t, ctx->m, n);
        if (i + 1 == 64 * n) nxc_copy(ctx->one, t, (size_t)n * 8);
    }
    nxc_copy(ctx->rr, t, (size_t)n * 8);
    return 0;
}

void nxc_mont_exp(const nxc_mont *ctx, uint64_t *r, const uint64_t *a,
                  const uint8_t *e, size_t e_len) {
    uint64_t acc[NXC_BN_MAX_LIMBS], base[NXC_BN_MAX_LIMBS];
    nxc_copy(acc, ctx->one, (size_t)ctx->n * 8);
    nxc_copy(base, a, (size_t)ctx->n * 8);
    for (size_t i = 0; i < e_len; i++) {
        for (int b = 7; b >= 0; b--) {
            nxc_mont_mul(ctx, acc, acc, acc);
            if ((e[i] >> b) & 1) nxc_mont_mul(ctx, acc, acc, base);
        }
    }
    nxc_copy(r, acc, (size_t)ctx->n * 8);
}
