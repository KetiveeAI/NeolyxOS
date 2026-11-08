/*
 * nxcrypto - arithmetic in GF(2^255 - 19), radix 2^51 (see fe25519.h).
 *
 * Products use the compiler's 128-bit integer type (two 64-bit registers
 * on x86_64; no floating point or vector registers). A product of two
 * limbs below 2^52, times 19, summed five times, stays below 2^115.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "fe25519.h"
#include "internal.h"

typedef unsigned __int128 u128;

#define MASK51 ((uint64_t)0x7ffffffffffffULL)

void nxc_fe_0(fe25519 *h) {
    for (int i = 0; i < 5; i++) h->v[i] = 0;
}

void nxc_fe_1(fe25519 *h) {
    nxc_fe_0(h);
    h->v[0] = 1;
}

void nxc_fe_copy(fe25519 *h, const fe25519 *f) {
    for (int i = 0; i < 5; i++) h->v[i] = f->v[i];
}

/* Bring every limb below 2^51 (limb 1 may end at 2^51 exactly). */
static void nxc_fe_carry(fe25519 *h) {
    uint64_t c;
    c = h->v[0] >> 51; h->v[0] &= MASK51; h->v[1] += c;
    c = h->v[1] >> 51; h->v[1] &= MASK51; h->v[2] += c;
    c = h->v[2] >> 51; h->v[2] &= MASK51; h->v[3] += c;
    c = h->v[3] >> 51; h->v[3] &= MASK51; h->v[4] += c;
    c = h->v[4] >> 51; h->v[4] &= MASK51; h->v[0] += c * 19;  /* 2^255 = 19 */
    c = h->v[0] >> 51; h->v[0] &= MASK51; h->v[1] += c;
}

void nxc_fe_frombytes(fe25519 *h, const uint8_t s[32]) {
    h->v[0] = nxc_load64_le(s) & MASK51;               /* bits   0..50  */
    h->v[1] = (nxc_load64_le(s + 6) >> 3) & MASK51;    /* bits  51..101 */
    h->v[2] = (nxc_load64_le(s + 12) >> 6) & MASK51;   /* bits 102..152 */
    h->v[3] = (nxc_load64_le(s + 19) >> 1) & MASK51;   /* bits 153..203 */
    h->v[4] = (nxc_load64_le(s + 24) >> 12) & MASK51;  /* bits 204..254 */
}

void nxc_fe_tobytes(uint8_t s[32], const fe25519 *f) {
    fe25519 h;
    nxc_fe_copy(&h, f);
    nxc_fe_carry(&h);
    nxc_fe_carry(&h);

    /* Now 0 <= h < 2^255 + 2^52 < 2p. q = 1 exactly when h >= p,
     * i.e. when h + 19 reaches 2^255. */
    uint64_t q = (h.v[0] + 19) >> 51;
    q = (h.v[1] + q) >> 51;
    q = (h.v[2] + q) >> 51;
    q = (h.v[3] + q) >> 51;
    q = (h.v[4] + q) >> 51;

    /* h - q*p = h + 19q - q*2^255: add 19q, carry, drop bit 255. */
    uint64_t c;
    h.v[0] += 19 * q;
    c = h.v[0] >> 51; h.v[0] &= MASK51; h.v[1] += c;
    c = h.v[1] >> 51; h.v[1] &= MASK51; h.v[2] += c;
    c = h.v[2] >> 51; h.v[2] &= MASK51; h.v[3] += c;
    c = h.v[3] >> 51; h.v[3] &= MASK51; h.v[4] += c;
    h.v[4] &= MASK51;

    nxc_store64_le(s,      h.v[0]         | (h.v[1] << 51));
    nxc_store64_le(s + 8,  (h.v[1] >> 13) | (h.v[2] << 38));
    nxc_store64_le(s + 16, (h.v[2] >> 26) | (h.v[3] << 25));
    nxc_store64_le(s + 24, (h.v[3] >> 39) | (h.v[4] << 12));
    nxc_memzero(&h, sizeof(h));
}

void nxc_fe_add(fe25519 *h, const fe25519 *f, const fe25519 *g) {
    for (int i = 0; i < 5; i++) h->v[i] = f->v[i] + g->v[i];
    nxc_fe_carry(h);
}

/* f - g computed as f + 2p - g so no limb goes negative (g limbs <= 2^51). */
void nxc_fe_sub(fe25519 *h, const fe25519 *f, const fe25519 *g) {
    fe25519 t;
    nxc_fe_copy(&t, g);
    nxc_fe_carry(&t);
    h->v[0] = f->v[0] + 0xfffffffffffdaULL - t.v[0];
    h->v[1] = f->v[1] + 0xffffffffffffeULL - t.v[1];
    h->v[2] = f->v[2] + 0xffffffffffffeULL - t.v[2];
    h->v[3] = f->v[3] + 0xffffffffffffeULL - t.v[3];
    h->v[4] = f->v[4] + 0xffffffffffffeULL - t.v[4];
    nxc_fe_carry(h);
}

void nxc_fe_neg(fe25519 *h, const fe25519 *f) {
    fe25519 z;
    nxc_fe_0(&z);
    nxc_fe_sub(h, &z, f);
}

/* Reduce five 128-bit column sums into h. */
static void nxc_fe_reduce128(fe25519 *h, u128 t0, u128 t1, u128 t2, u128 t3, u128 t4) {
    u128 c;
    c = t0 >> 51; t0 &= MASK51; t1 += c;
    c = t1 >> 51; t1 &= MASK51; t2 += c;
    c = t2 >> 51; t2 &= MASK51; t3 += c;
    c = t3 >> 51; t3 &= MASK51; t4 += c;
    c = t4 >> 51; t4 &= MASK51; t0 += c * 19;
    c = t0 >> 51; t0 &= MASK51; t1 += c;
    h->v[0] = (uint64_t)t0;
    h->v[1] = (uint64_t)t1;
    h->v[2] = (uint64_t)t2;
    h->v[3] = (uint64_t)t3;
    h->v[4] = (uint64_t)t4;
}

void nxc_fe_mul(fe25519 *h, const fe25519 *f, const fe25519 *g) {
    uint64_t f0 = f->v[0], f1 = f->v[1], f2 = f->v[2], f3 = f->v[3], f4 = f->v[4];
    uint64_t g0 = g->v[0], g1 = g->v[1], g2 = g->v[2], g3 = g->v[3], g4 = g->v[4];
    uint64_t g1_19 = 19 * g1, g2_19 = 19 * g2, g3_19 = 19 * g3, g4_19 = 19 * g4;

    /* Limb i*j lands in column i+j; columns >= 5 wrap with a factor 19. */
    u128 t0 = (u128)f0 * g0 + (u128)f1 * g4_19 + (u128)f2 * g3_19 + (u128)f3 * g2_19 + (u128)f4 * g1_19;
    u128 t1 = (u128)f0 * g1 + (u128)f1 * g0    + (u128)f2 * g4_19 + (u128)f3 * g3_19 + (u128)f4 * g2_19;
    u128 t2 = (u128)f0 * g2 + (u128)f1 * g1    + (u128)f2 * g0    + (u128)f3 * g4_19 + (u128)f4 * g3_19;
    u128 t3 = (u128)f0 * g3 + (u128)f1 * g2    + (u128)f2 * g1    + (u128)f3 * g0    + (u128)f4 * g4_19;
    u128 t4 = (u128)f0 * g4 + (u128)f1 * g3    + (u128)f2 * g2    + (u128)f3 * g1    + (u128)f4 * g0;
    nxc_fe_reduce128(h, t0, t1, t2, t3, t4);
}

void nxc_fe_sq(fe25519 *h, const fe25519 *f) {
    nxc_fe_mul(h, f, f);
}

void nxc_fe_mul_small(fe25519 *h, const fe25519 *f, uint32_t k) {
    nxc_fe_reduce128(h, (u128)f->v[0] * k, (u128)f->v[1] * k, (u128)f->v[2] * k,
                 (u128)f->v[3] * k, (u128)f->v[4] * k);
}

/* h = f^(2^n) */
static void nxc_fe_sqn(fe25519 *h, const fe25519 *f, int n) {
    nxc_fe_sq(h, f);
    for (int i = 1; i < n; i++) nxc_fe_sq(h, h);
}

/*
 * Shared start of both exponentiations: returns z^(2^250 - 1) in z250
 * and z^11 in z11. Fixed sequence of squarings and multiplications.
 */
static void nxc_fe_pow_2_250_1(fe25519 *z250, fe25519 *z11, const fe25519 *z) {
    fe25519 t0, t1, z5, z10, z20, z50, z100;

    nxc_fe_sq(&t0, z);                 /* z^2 */
    nxc_fe_sqn(&t1, &t0, 2);           /* z^8 */
    nxc_fe_mul(&t1, &t1, z);           /* z^9 */
    nxc_fe_mul(z11, &t0, &t1);         /* z^11 */
    nxc_fe_sq(&t0, z11);               /* z^22 */
    nxc_fe_mul(&z5, &t0, &t1);         /* z^31 = z^(2^5 - 1) */
    nxc_fe_sqn(&t0, &z5, 5);
    nxc_fe_mul(&z10, &t0, &z5);        /* 2^10 - 1 */
    nxc_fe_sqn(&t0, &z10, 10);
    nxc_fe_mul(&z20, &t0, &z10);       /* 2^20 - 1 */
    nxc_fe_sqn(&t0, &z20, 20);
    nxc_fe_mul(&t0, &t0, &z20);        /* 2^40 - 1 */
    nxc_fe_sqn(&t0, &t0, 10);
    nxc_fe_mul(&z50, &t0, &z10);       /* 2^50 - 1 */
    nxc_fe_sqn(&t0, &z50, 50);
    nxc_fe_mul(&z100, &t0, &z50);      /* 2^100 - 1 */
    nxc_fe_sqn(&t0, &z100, 100);
    nxc_fe_mul(&t0, &t0, &z100);       /* 2^200 - 1 */
    nxc_fe_sqn(&t0, &t0, 50);
    nxc_fe_mul(z250, &t0, &z50);       /* 2^250 - 1 */

    nxc_memzero(&t0, sizeof(t0)); nxc_memzero(&t1, sizeof(t1));
    nxc_memzero(&z5, sizeof(z5)); nxc_memzero(&z10, sizeof(z10));
    nxc_memzero(&z20, sizeof(z20)); nxc_memzero(&z50, sizeof(z50));
    nxc_memzero(&z100, sizeof(z100));
}

/* z^(p-2) = z^(2^255 - 21) = (z^(2^250 - 1))^(2^5) * z^11 */
void nxc_fe_invert(fe25519 *out, const fe25519 *z) {
    fe25519 z250, z11, t;
    nxc_fe_pow_2_250_1(&z250, &z11, z);
    nxc_fe_sqn(&t, &z250, 5);
    nxc_fe_mul(out, &t, &z11);
    nxc_memzero(&z250, sizeof(z250)); nxc_memzero(&z11, sizeof(z11));
    nxc_memzero(&t, sizeof(t));
}

/* z^((p-5)/8) = z^(2^252 - 3) = (z^(2^250 - 1))^4 * z */
void nxc_fe_pow22523(fe25519 *out, const fe25519 *z) {
    fe25519 z250, z11, t;
    nxc_fe_pow_2_250_1(&z250, &z11, z);
    nxc_fe_sqn(&t, &z250, 2);
    nxc_fe_mul(out, &t, z);
    nxc_memzero(&z250, sizeof(z250)); nxc_memzero(&z11, sizeof(z11));
    nxc_memzero(&t, sizeof(t));
}

void nxc_fe_cswap(fe25519 *f, fe25519 *g, uint32_t b) {
    uint64_t mask = (uint64_t)0 - (uint64_t)(b & 1);
    for (int i = 0; i < 5; i++) {
        uint64_t x = mask & (f->v[i] ^ g->v[i]);
        f->v[i] ^= x;
        g->v[i] ^= x;
    }
}

void nxc_fe_cmov(fe25519 *f, const fe25519 *g, uint32_t b) {
    uint64_t mask = (uint64_t)0 - (uint64_t)(b & 1);
    for (int i = 0; i < 5; i++) f->v[i] ^= mask & (f->v[i] ^ g->v[i]);
}

int nxc_fe_isnonzero(const fe25519 *f) {
    uint8_t s[32];
    uint8_t acc = 0;
    nxc_fe_tobytes(s, f);
    for (int i = 0; i < 32; i++) acc |= s[i];
    nxc_memzero(s, sizeof(s));
    return (int)((((uint32_t)acc) + 0xffu) >> 8);   /* 1 if acc != 0 */
}

int nxc_fe_isnegative(const fe25519 *f) {
    uint8_t s[32];
    nxc_fe_tobytes(s, f);
    int neg = s[0] & 1;
    nxc_memzero(s, sizeof(s));
    return neg;
}
