/*
 * nxcrypto - Ed25519 signatures (RFC 8032 section 5.1).
 *
 * Points on edwards25519 (-x^2 + y^2 = 1 + d x^2 y^2) use extended
 * coordinates (X:Y:Z:T), x = X/Z, y = Y/Z, xy = T/Z, with the complete
 * addition law for a = -1 (Hisil, Wong, Carter, Dawson 2008, section 3.1)
 * and its doubling formula. Scalars mod L use plain 64-bit limbs.
 *
 * Constant time: scalar multiplication is a fixed double-and-add over all
 * 256 bits with masked selection; scalar reduction is a fixed bit-serial
 * loop with masked subtraction. Only public data (signature S, public
 * key) is checked with ordinary branches.
 *
 * Constants come from tools/gen_constants.py; the host test checks them
 * again (d * 121666 = -121665, sqrtm1^2 = -1, B on the curve).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "nxcrypto/curve25519.h"
#include "nxcrypto/sha2.h"
#include "fe25519.h"
#include "internal.h"

typedef unsigned __int128 u128;

typedef struct { fe25519 X, Y, Z, T; } ge25519;

/* d = -121665/121666, 2d, sqrt(-1) mod p, and the base point B. */
static const fe25519 ED_D = {{ 0x34dca135978a3ULL, 0x1a8283b156ebdULL, 0x5e7a26001c029ULL,
                               0x739c663a03cbbULL, 0x52036cee2b6ffULL }};
static const fe25519 ED_2D = {{ 0x69b9426b2f159ULL, 0x35050762add7aULL, 0x3cf44c0038052ULL,
                                0x6738cc7407977ULL, 0x2406d9dc56dffULL }};
static const fe25519 ED_SQRTM1 = {{ 0x61b274a0ea0b0ULL, 0x0d5a5fc8f189dULL, 0x7ef5e9cbd0c60ULL,
                                    0x78595a6804c9eULL, 0x2b8324804fc1dULL }};
static const ge25519 ED_B = {
    {{ 0x62d608f25d51aULL, 0x412a4b4f6592aULL, 0x75b7171a4b31dULL, 0x1ff60527118feULL, 0x216936d3cd6e5ULL }},
    {{ 0x6666666666658ULL, 0x4ccccccccccccULL, 0x1999999999999ULL, 0x3333333333333ULL, 0x6666666666666ULL }},
    {{ 1, 0, 0, 0, 0 }},
    {{ 0x68ab3a5b7dda3ULL, 0x00eea2a5eadbbULL, 0x2af8df483c27eULL, 0x332b375274732ULL, 0x67875f0fd78b7ULL }},
};

/* Group order L = 2^252 + 27742317777372353535851937790883648493, 64-bit limbs. */
static const uint64_t ED_L[4] = {
    0x5812631a5cf5d3edULL, 0x14def9dea2f79cd6ULL, 0x0000000000000000ULL, 0x1000000000000000ULL
};

/* ------------------------------------------------------------------ */
/* Group operations                                                    */
/* ------------------------------------------------------------------ */

static void ge_identity(ge25519 *p) {
    nxc_fe_0(&p->X);
    nxc_fe_1(&p->Y);
    nxc_fe_1(&p->Z);
    nxc_fe_0(&p->T);
}

static void ge_wipe(ge25519 *p) {
    nxc_memzero(p, sizeof(*p));
}

/* r = p + q (complete for all inputs, including p == q and identity). */
static void ge_add(ge25519 *r, const ge25519 *p, const ge25519 *q) {
    fe25519 a, b, c, d, e, f, g, h, t;
    nxc_fe_sub(&a, &p->Y, &p->X);
    nxc_fe_sub(&t, &q->Y, &q->X);
    nxc_fe_mul(&a, &a, &t);                /* A = (Y1 - X1)(Y2 - X2) */
    nxc_fe_add(&b, &p->Y, &p->X);
    nxc_fe_add(&t, &q->Y, &q->X);
    nxc_fe_mul(&b, &b, &t);                /* B = (Y1 + X1)(Y2 + X2) */
    nxc_fe_mul(&c, &p->T, &q->T);
    nxc_fe_mul(&c, &c, &ED_2D);            /* C = 2d T1 T2 */
    nxc_fe_mul(&d, &p->Z, &q->Z);
    nxc_fe_add(&d, &d, &d);                /* D = 2 Z1 Z2 */
    nxc_fe_sub(&e, &b, &a);                /* E = B - A */
    nxc_fe_sub(&f, &d, &c);                /* F = D - C */
    nxc_fe_add(&g, &d, &c);                /* G = D + C */
    nxc_fe_add(&h, &b, &a);                /* H = B + A */
    nxc_fe_mul(&r->X, &e, &f);
    nxc_fe_mul(&r->Y, &g, &h);
    nxc_fe_mul(&r->T, &e, &h);
    nxc_fe_mul(&r->Z, &f, &g);
    nxc_memzero(&a, sizeof(a)); nxc_memzero(&b, sizeof(b)); nxc_memzero(&c, sizeof(c));
    nxc_memzero(&d, sizeof(d)); nxc_memzero(&e, sizeof(e)); nxc_memzero(&f, sizeof(f));
    nxc_memzero(&g, sizeof(g)); nxc_memzero(&h, sizeof(h)); nxc_memzero(&t, sizeof(t));
}

/* r = 2p (doubling for a = -1). */
static void ge_dbl(ge25519 *r, const ge25519 *p) {
    fe25519 a, b, c, e, f, g, h;
    nxc_fe_sq(&a, &p->X);                  /* A = X1^2 */
    nxc_fe_sq(&b, &p->Y);                  /* B = Y1^2 */
    nxc_fe_sq(&c, &p->Z);
    nxc_fe_add(&c, &c, &c);                /* C = 2 Z1^2 */
    nxc_fe_add(&e, &p->X, &p->Y);
    nxc_fe_sq(&e, &e);
    nxc_fe_sub(&e, &e, &a);
    nxc_fe_sub(&e, &e, &b);                /* E = (X1 + Y1)^2 - A - B */
    nxc_fe_sub(&g, &b, &a);                /* G = D + B with D = aA = -A */
    nxc_fe_sub(&f, &g, &c);                /* F = G - C */
    nxc_fe_add(&h, &a, &b);
    nxc_fe_neg(&h, &h);                    /* H = D - B = -A - B */
    nxc_fe_mul(&r->X, &e, &f);
    nxc_fe_mul(&r->Y, &g, &h);
    nxc_fe_mul(&r->T, &e, &h);
    nxc_fe_mul(&r->Z, &f, &g);
    nxc_memzero(&a, sizeof(a)); nxc_memzero(&b, sizeof(b)); nxc_memzero(&c, sizeof(c));
    nxc_memzero(&e, sizeof(e)); nxc_memzero(&f, sizeof(f)); nxc_memzero(&g, sizeof(g));
    nxc_memzero(&h, sizeof(h));
}

static void ge_cmov(ge25519 *p, const ge25519 *q, uint32_t b) {
    nxc_fe_cmov(&p->X, &q->X, b);
    nxc_fe_cmov(&p->Y, &q->Y, b);
    nxc_fe_cmov(&p->Z, &q->Z, b);
    nxc_fe_cmov(&p->T, &q->T, b);
}

/*
 * r = [s]p for a 256-bit little-endian scalar. Every bit costs one
 * doubling and one addition; the sum is kept or dropped with a masked
 * move, so time and memory access do not depend on s.
 */
static void ge_scalarmult(ge25519 *r, const ge25519 *p, const uint8_t s[32]) {
    ge25519 q, sum;
    ge_identity(&q);
    for (int i = 255; i >= 0; i--) {
        uint32_t bit = (uint32_t)(s[i >> 3] >> (i & 7)) & 1u;
        ge_dbl(&q, &q);
        ge_add(&sum, &q, p);
        ge_cmov(&q, &sum, bit);
    }
    *r = q;
    ge_wipe(&q);
    ge_wipe(&sum);
}

/* Encoding (RFC 8032 section 5.1.2): y, with the low bit of x in bit 255. */
static void ge_tobytes(uint8_t s[32], const ge25519 *p) {
    fe25519 zinv, x, y;
    nxc_fe_invert(&zinv, &p->Z);
    nxc_fe_mul(&x, &p->X, &zinv);
    nxc_fe_mul(&y, &p->Y, &zinv);
    nxc_fe_tobytes(s, &y);
    s[31] ^= (uint8_t)(nxc_fe_isnegative(&x) << 7);
    nxc_memzero(&zinv, sizeof(zinv)); nxc_memzero(&x, sizeof(x)); nxc_memzero(&y, sizeof(y));
}

/*
 * Decoding (RFC 8032 section 5.1.3). Only public values (keys) are
 * decoded, so the early returns leak nothing secret. Rejects a
 * non-canonical y (y >= p), a y with no matching x, and "-0".
 */
static int ge_frombytes(ge25519 *p, const uint8_t s[32]) {
    uint8_t ys[32], check[32];
    nxc_copy(ys, s, 32);
    uint32_t sign = ys[31] >> 7;
    ys[31] &= 0x7f;

    fe25519 y, u, v, v3, x, vxx, t;
    nxc_fe_frombytes(&y, ys);
    nxc_fe_tobytes(check, &y);
    for (int i = 0; i < 32; i++)
        if (check[i] != ys[i]) return NXC_ERR_ENCODING;   /* y >= p */

    nxc_fe_sq(&u, &y);
    nxc_fe_mul(&v, &u, &ED_D);
    fe25519 one;
    nxc_fe_1(&one);
    nxc_fe_sub(&u, &u, &one);              /* u = y^2 - 1 */
    nxc_fe_add(&v, &v, &one);              /* v = d y^2 + 1 */

    /* x = u v^3 (u v^7)^((p-5)/8) */
    nxc_fe_sq(&v3, &v);
    nxc_fe_mul(&v3, &v3, &v);              /* v^3 */
    nxc_fe_sq(&t, &v3);
    nxc_fe_mul(&t, &t, &v);                /* v^7 */
    nxc_fe_mul(&t, &t, &u);                /* u v^7 */
    nxc_fe_pow22523(&t, &t);
    nxc_fe_mul(&t, &t, &v3);
    nxc_fe_mul(&x, &t, &u);

    nxc_fe_sq(&vxx, &x);
    nxc_fe_mul(&vxx, &vxx, &v);            /* v x^2 */
    nxc_fe_sub(&t, &vxx, &u);
    if (nxc_fe_isnonzero(&t)) {            /* v x^2 != u */
        nxc_fe_add(&t, &vxx, &u);
        if (nxc_fe_isnonzero(&t)) return NXC_ERR_ENCODING;   /* no square root */
        nxc_fe_mul(&x, &x, &ED_SQRTM1);    /* v x^2 == -u */
    }
    if (!nxc_fe_isnonzero(&x) && sign) return NXC_ERR_ENCODING;
    if ((uint32_t)nxc_fe_isnegative(&x) != sign) nxc_fe_neg(&x, &x);

    nxc_fe_copy(&p->X, &x);
    nxc_fe_copy(&p->Y, &y);
    nxc_fe_1(&p->Z);
    nxc_fe_mul(&p->T, &x, &y);
    return NXC_OK;
}

/* ------------------------------------------------------------------ */
/* Scalars mod L                                                       */
/* ------------------------------------------------------------------ */

/* r -= L if r >= L, without branching on r. Needs r < 2L. */
static void sc_cond_sub_l(uint64_t r[4]) {
    uint64_t d[4];
    uint64_t borrow = 0;
    for (int i = 0; i < 4; i++) {
        u128 t = (u128)r[i] - ED_L[i] - borrow;
        d[i] = (uint64_t)t;
        borrow = (uint64_t)(t >> 64) & 1u;
    }
    uint64_t take = (uint64_t)0 - (borrow ^ 1u);   /* all ones when r >= L */
    for (int i = 0; i < 4; i++) r[i] = (d[i] & take) | (r[i] & ~take);
    nxc_memzero(d, sizeof(d));
}

/* out = in mod L for a 512-bit little-endian value (bit-serial). */
static void sc_reduce512(uint8_t out[32], const uint8_t in[64]) {
    uint64_t r[4] = { 0, 0, 0, 0 };
    for (int i = 511; i >= 0; i--) {
        uint64_t bit = (uint64_t)(in[i >> 3] >> (i & 7)) & 1u;
        /* r < L < 2^253, so 2r + 1 < 2L fits in 256 bits. */
        r[3] = (r[3] << 1) | (r[2] >> 63);
        r[2] = (r[2] << 1) | (r[1] >> 63);
        r[1] = (r[1] << 1) | (r[0] >> 63);
        r[0] = (r[0] << 1) | bit;
        sc_cond_sub_l(r);
    }
    for (int i = 0; i < 4; i++) nxc_store64_le(out + 8 * i, r[i]);
    nxc_memzero(r, sizeof(r));
}

/* s = (a * b + c) mod L, all 32-byte little-endian. */
static void sc_muladd(uint8_t s[32], const uint8_t a[32], const uint8_t b[32],
                      const uint8_t c[32]) {
    uint64_t al[4], bl[4], p[8] = { 0 };
    for (int i = 0; i < 4; i++) {
        al[i] = nxc_load64_le(a + 8 * i);
        bl[i] = nxc_load64_le(b + 8 * i);
    }
    for (int i = 0; i < 4; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < 4; j++) {
            u128 t = (u128)al[i] * bl[j] + p[i + j] + carry;
            p[i + j] = (uint64_t)t;
            carry = (uint64_t)(t >> 64);
        }
        p[i + 4] = carry;
    }
    /* a*b <= (2^256 - 1)^2, so adding c < 2^256 cannot overflow 512 bits. */
    uint64_t carry = 0;
    for (int i = 0; i < 8; i++) {
        u128 t = (u128)p[i] + (i < 4 ? nxc_load64_le(c + 8 * i) : 0) + carry;
        p[i] = (uint64_t)t;
        carry = (uint64_t)(t >> 64);
    }
    uint8_t wide[64];
    for (int i = 0; i < 8; i++) nxc_store64_le(wide + 8 * i, p[i]);
    sc_reduce512(s, wide);
    nxc_memzero(al, sizeof(al)); nxc_memzero(bl, sizeof(bl));
    nxc_memzero(p, sizeof(p));   nxc_memzero(wide, sizeof(wide));
}

/* 1 if the 32-byte little-endian scalar is < L (public input only). */
static int sc_is_canonical(const uint8_t s[32]) {
    for (int i = 3; i >= 0; i--) {
        uint64_t w = nxc_load64_le(s + 8 * i);
        if (w < ED_L[i]) return 1;
        if (w > ED_L[i]) return 0;
    }
    return 0;   /* equal to L */
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/* h = SHA-512(seed); a = clamped h[0..31]; prefix = h[32..63]. */
static int ed25519_expand(const uint8_t seed[32], uint8_t a[32], uint8_t prefix[32]) {
    uint8_t h[64];
    int rc = nxc_sha512(seed, 32, h);
    if (rc == NXC_OK) {
        nxc_copy(a, h, 32);
        a[0] &= 248;
        a[31] &= 127;
        a[31] |= 64;
        nxc_copy(prefix, h + 32, 32);
    }
    nxc_memzero(h, sizeof(h));
    return rc;
}

int nxc_ed25519_public(uint8_t pub[NXC_ED25519_PUBLIC_LEN],
                       const uint8_t seed[NXC_ED25519_SEED_LEN]) {
    if (pub == NULL || seed == NULL) return NXC_ERR_ARG;
    uint8_t a[32], prefix[32];
    int rc = ed25519_expand(seed, a, prefix);
    if (rc == NXC_OK) {
        ge25519 A;
        ge_scalarmult(&A, &ED_B, a);
        ge_tobytes(pub, &A);
        ge_wipe(&A);
    }
    nxc_memzero(a, sizeof(a));
    nxc_memzero(prefix, sizeof(prefix));
    return rc;
}

int nxc_ed25519_sign(uint8_t sig[NXC_ED25519_SIGNATURE_LEN],
                     const void *msg, size_t msg_len,
                     const uint8_t seed[NXC_ED25519_SEED_LEN]) {
    if (sig == NULL || seed == NULL || (msg == NULL && msg_len != 0)) return NXC_ERR_ARG;

    uint8_t a[32], prefix[32], pub[32], hash[64], r[32], k[32];
    ge25519 P;
    nxc_sha512_ctx hc;
    int rc = ed25519_expand(seed, a, prefix);

    if (rc == NXC_OK) {                       /* A = [a]B */
        ge_scalarmult(&P, &ED_B, a);
        ge_tobytes(pub, &P);
    }
    if (rc == NXC_OK) {                       /* r = SHA-512(prefix || M) mod L */
        rc = nxc_sha512_init(&hc);
        if (rc == NXC_OK) rc = nxc_sha512_update(&hc, prefix, 32);
        if (rc == NXC_OK) rc = nxc_sha512_update(&hc, msg, msg_len);
        if (rc == NXC_OK) rc = nxc_sha512_final(&hc, hash);
        if (rc == NXC_OK) sc_reduce512(r, hash);
    }
    if (rc == NXC_OK) {                       /* R = [r]B */
        ge_scalarmult(&P, &ED_B, r);
        ge_tobytes(sig, &P);
    }
    if (rc == NXC_OK) {                       /* k = SHA-512(R || A || M) mod L */
        rc = nxc_sha512_init(&hc);
        if (rc == NXC_OK) rc = nxc_sha512_update(&hc, sig, 32);
        if (rc == NXC_OK) rc = nxc_sha512_update(&hc, pub, 32);
        if (rc == NXC_OK) rc = nxc_sha512_update(&hc, msg, msg_len);
        if (rc == NXC_OK) rc = nxc_sha512_final(&hc, hash);
        if (rc == NXC_OK) sc_reduce512(k, hash);
    }
    if (rc == NXC_OK) {
        sc_muladd(sig + 32, k, a, r);         /* S = (r + k a) mod L */
    } else {
        nxc_memzero(sig, NXC_ED25519_SIGNATURE_LEN);
    }

    nxc_memzero(a, sizeof(a)); nxc_memzero(prefix, sizeof(prefix));
    nxc_memzero(hash, sizeof(hash)); nxc_memzero(r, sizeof(r));
    nxc_memzero(k, sizeof(k)); nxc_memzero(&hc, sizeof(hc));
    ge_wipe(&P);
    return rc;
}

int nxc_ed25519_verify(const uint8_t sig[NXC_ED25519_SIGNATURE_LEN],
                       const void *msg, size_t msg_len,
                       const uint8_t pub[NXC_ED25519_PUBLIC_LEN]) {
    if (sig == NULL || pub == NULL || (msg == NULL && msg_len != 0)) return NXC_ERR_ARG;

    /* S must be fully reduced (RFC 8032 5.1.7 step 1, prevents malleability). */
    if (!sc_is_canonical(sig + 32)) return NXC_ERR_ENCODING;

    ge25519 A;
    int rc = ge_frombytes(&A, pub);
    if (rc != NXC_OK) return rc;

    uint8_t hash[64], k[32], check[32];
    nxc_sha512_ctx hc;
    rc = nxc_sha512_init(&hc);
    if (rc == NXC_OK) rc = nxc_sha512_update(&hc, sig, 32);
    if (rc == NXC_OK) rc = nxc_sha512_update(&hc, pub, 32);
    if (rc == NXC_OK) rc = nxc_sha512_update(&hc, msg, msg_len);
    if (rc == NXC_OK) rc = nxc_sha512_final(&hc, hash);
    if (rc != NXC_OK) return rc;
    sc_reduce512(k, hash);

    /* R' = [S]B - [k]A; valid when its encoding equals R. */
    ge25519 sb, ka, rcheck;
    ge_scalarmult(&sb, &ED_B, sig + 32);
    ge_scalarmult(&ka, &A, k);
    nxc_fe_neg(&ka.X, &ka.X);
    nxc_fe_neg(&ka.T, &ka.T);
    ge_add(&rcheck, &sb, &ka);
    ge_tobytes(check, &rcheck);

    return nxc_ct_equal(check, sig, 32) ? NXC_OK : NXC_ERR_VERIFY;
}

/* ------------------------------------------------------------------ */
/* Self-check hooks for the host test (not in the public header).      */
/* ------------------------------------------------------------------ */

int nxc__ed25519_selfcheck(void);

/*
 * Checks the typed-in constants against their definitions:
 * d * 121666 == -121665, (2d) == d + d, sqrtm1^2 == -1, B on the curve
 * (-x^2 + y^2 == 1 + d x^2 y^2), T == x y, and B encodes to 0x58 66..66.
 * Returns 0 when all hold.
 */
int nxc__ed25519_selfcheck(void) {
    fe25519 t, u, one, x2, y2;
    int bad = 0;
    nxc_fe_1(&one);

    nxc_fe_mul_small(&t, &ED_D, 121666);
    nxc_fe_mul_small(&u, &one, 121665);
    nxc_fe_add(&t, &t, &u);
    bad |= nxc_fe_isnonzero(&t) << 0;

    nxc_fe_add(&t, &ED_D, &ED_D);
    nxc_fe_sub(&t, &t, &ED_2D);
    bad |= nxc_fe_isnonzero(&t) << 1;

    nxc_fe_sq(&t, &ED_SQRTM1);
    nxc_fe_add(&t, &t, &one);
    bad |= nxc_fe_isnonzero(&t) << 2;

    nxc_fe_sq(&x2, &ED_B.X);
    nxc_fe_sq(&y2, &ED_B.Y);
    nxc_fe_sub(&t, &y2, &x2);              /* -x^2 + y^2 */
    nxc_fe_mul(&u, &x2, &y2);
    nxc_fe_mul(&u, &u, &ED_D);
    nxc_fe_add(&u, &u, &one);              /* 1 + d x^2 y^2 */
    nxc_fe_sub(&t, &t, &u);
    bad |= nxc_fe_isnonzero(&t) << 3;

    nxc_fe_mul(&t, &ED_B.X, &ED_B.Y);
    nxc_fe_sub(&t, &t, &ED_B.T);
    bad |= nxc_fe_isnonzero(&t) << 4;

    uint8_t enc[32];
    ge_tobytes(enc, &ED_B);
    if (enc[0] != 0x58) bad |= 1 << 5;
    for (int i = 1; i < 32; i++) if (enc[i] != 0x66) bad |= 1 << 5;

    /* [L]B must be the identity: encoding of (0, 1). */
    uint8_t lbytes[32];
    for (int i = 0; i < 4; i++) nxc_store64_le(lbytes + 8 * i, ED_L[i]);
    ge25519 lb;
    ge_scalarmult(&lb, &ED_B, lbytes);
    ge_tobytes(enc, &lb);
    if (enc[0] != 1) bad |= 1 << 6;
    for (int i = 1; i < 32; i++) if (enc[i] != 0) bad |= 1 << 6;
    return bad;
}
