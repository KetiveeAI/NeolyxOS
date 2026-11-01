/*
 * nxcrypto - ECDSA verification over NIST P-256 and P-384.
 *
 * Field arithmetic is Montgomery (bn.c); points use Jacobian coordinates
 * (X, Y, Z) for x = X/Z^2, y = Y/Z^3, with Z = 0 for the point at
 * infinity. Formulas: doubling "dbl-2001-b" (a = -3) and addition
 * "add-2007-bl" from the Explicit-Formulas Database (Bernstein, Lange).
 * Verification only: all inputs are public, so this code may branch on
 * them (see bn.h).
 *
 * Domain parameters (FIPS 186-4 appendix D.1.2) were transcribed by
 * tools/gen_ec_params.py and are checked by the host test.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "nxcrypto/pubkey.h"
#include "bn.h"
#include "internal.h"

#define EC_MAX_LIMBS 6      /* P-384 */
#define EC_MAX_BYTES 48

static const uint8_t P256_P[32] = {
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};
static const uint8_t P256_B[32] = {
    0x5a, 0xc6, 0x35, 0xd8, 0xaa, 0x3a, 0x93, 0xe7, 0xb3, 0xeb, 0xbd, 0x55,
    0x76, 0x98, 0x86, 0xbc, 0x65, 0x1d, 0x06, 0xb0, 0xcc, 0x53, 0xb0, 0xf6,
    0x3b, 0xce, 0x3c, 0x3e, 0x27, 0xd2, 0x60, 0x4b
};
static const uint8_t P256_N[32] = {
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17, 0x9e, 0x84,
    0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51
};
static const uint8_t P256_GX[32] = {
    0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42, 0x47, 0xf8, 0xbc, 0xe6, 0xe5,
    0x63, 0xa4, 0x40, 0xf2, 0x77, 0x03, 0x7d, 0x81, 0x2d, 0xeb, 0x33, 0xa0,
    0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2, 0x96
};
static const uint8_t P256_GY[32] = {
    0x4f, 0xe3, 0x42, 0xe2, 0xfe, 0x1a, 0x7f, 0x9b, 0x8e, 0xe7, 0xeb, 0x4a,
    0x7c, 0x0f, 0x9e, 0x16, 0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31, 0x5e, 0xce,
    0xcb, 0xb6, 0x40, 0x68, 0x37, 0xbf, 0x51, 0xf5
};
static const uint8_t P384_P[48] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff
};
static const uint8_t P384_B[48] = {
    0xb3, 0x31, 0x2f, 0xa7, 0xe2, 0x3e, 0xe7, 0xe4, 0x98, 0x8e, 0x05, 0x6b,
    0xe3, 0xf8, 0x2d, 0x19, 0x18, 0x1d, 0x9c, 0x6e, 0xfe, 0x81, 0x41, 0x12,
    0x03, 0x14, 0x08, 0x8f, 0x50, 0x13, 0x87, 0x5a, 0xc6, 0x56, 0x39, 0x8d,
    0x8a, 0x2e, 0xd1, 0x9d, 0x2a, 0x85, 0xc8, 0xed, 0xd3, 0xec, 0x2a, 0xef
};
static const uint8_t P384_N[48] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xc7, 0x63, 0x4d, 0x81, 0xf4, 0x37, 0x2d, 0xdf, 0x58, 0x1a, 0x0d, 0xb2,
    0x48, 0xb0, 0xa7, 0x7a, 0xec, 0xec, 0x19, 0x6a, 0xcc, 0xc5, 0x29, 0x73
};
static const uint8_t P384_GX[48] = {
    0xaa, 0x87, 0xca, 0x22, 0xbe, 0x8b, 0x05, 0x37, 0x8e, 0xb1, 0xc7, 0x1e,
    0xf3, 0x20, 0xad, 0x74, 0x6e, 0x1d, 0x3b, 0x62, 0x8b, 0xa7, 0x9b, 0x98,
    0x59, 0xf7, 0x41, 0xe0, 0x82, 0x54, 0x2a, 0x38, 0x55, 0x02, 0xf2, 0x5d,
    0xbf, 0x55, 0x29, 0x6c, 0x3a, 0x54, 0x5e, 0x38, 0x72, 0x76, 0x0a, 0xb7
};
static const uint8_t P384_GY[48] = {
    0x36, 0x17, 0xde, 0x4a, 0x96, 0x26, 0x2c, 0x6f, 0x5d, 0x9e, 0x98, 0xbf,
    0x92, 0x92, 0xdc, 0x29, 0xf8, 0xf4, 0x1d, 0xbd, 0x28, 0x9a, 0x14, 0x7c,
    0xe9, 0xda, 0x31, 0x13, 0xb5, 0xf0, 0xb8, 0xc0, 0x0a, 0x60, 0xb1, 0xce,
    0x1d, 0x7e, 0x81, 0x9d, 0x7a, 0x43, 0x1d, 0x7c, 0x90, 0xea, 0x0e, 0x5f
};

typedef struct {
    nxc_mont fp;                    /* arithmetic mod p */
    nxc_mont fn;                    /* arithmetic mod n */
    uint64_t b[EC_MAX_LIMBS];       /* curve b, Montgomery form */
    uint64_t gx[EC_MAX_LIMBS];      /* generator, Montgomery form */
    uint64_t gy[EC_MAX_LIMBS];
    const uint8_t *p_bytes, *n_bytes;
    uint32_t size;                  /* field and order size in bytes */
    uint32_t limbs;
} ec_curve;

typedef struct {
    uint64_t x[EC_MAX_LIMBS], y[EC_MAX_LIMBS], z[EC_MAX_LIMBS];
} ec_point;

static int curve_init(ec_curve *c, nxc_ec_curve_t id) {
    const uint8_t *p, *b, *n, *gx, *gy;
    if (id == NXC_EC_P256) {
        p = P256_P; b = P256_B; n = P256_N; gx = P256_GX; gy = P256_GY; c->size = 32;
    } else if (id == NXC_EC_P384) {
        p = P384_P; b = P384_B; n = P384_N; gx = P384_GX; gy = P384_GY; c->size = 48;
    } else {
        return NXC_ERR_ARG;
    }
    if (nxc_mont_init(&c->fp, p, c->size) != 0 || nxc_mont_init(&c->fn, n, c->size) != 0)
        return NXC_ERR_ARG;
    c->limbs = c->fp.n;
    c->p_bytes = p;
    c->n_bytes = n;
    uint64_t t[EC_MAX_LIMBS];
    nxc_bn_from_be(t, c->limbs, b, c->size);  nxc_mont_to(&c->fp, c->b, t);
    nxc_bn_from_be(t, c->limbs, gx, c->size); nxc_mont_to(&c->fp, c->gx, t);
    nxc_bn_from_be(t, c->limbs, gy, c->size); nxc_mont_to(&c->fp, c->gy, t);
    return NXC_OK;
}

#define FMUL(r, a, b) nxc_mont_mul(&c->fp, (r), (a), (b))
#define FADD(r, a, b) nxc_mod_add(&c->fp, (r), (a), (b))
#define FSUB(r, a, b) nxc_mod_sub(&c->fp, (r), (a), (b))

static void fcopy(const ec_curve *c, uint64_t *r, const uint64_t *a) {
    nxc_copy(r, a, (size_t)c->limbs * 8);
}

static int pt_is_inf(const ec_curve *c, const ec_point *p) {
    return nxc_bn_is_zero(p->z, c->limbs);
}

static void pt_set_inf(const ec_curve *c, ec_point *p) {
    for (uint32_t i = 0; i < c->limbs; i++) p->x[i] = p->y[i] = p->z[i] = 0;
    fcopy(c, p->x, c->fp.one);
    fcopy(c, p->y, c->fp.one);
}

/* r = 2p (a = -3). r may alias p. */
static void pt_dbl(const ec_curve *c, ec_point *r, const ec_point *p) {
    if (pt_is_inf(c, p) || nxc_bn_is_zero(p->y, c->limbs)) { pt_set_inf(c, r); return; }
    uint64_t delta[EC_MAX_LIMBS], gamma[EC_MAX_LIMBS], beta[EC_MAX_LIMBS];
    uint64_t alpha[EC_MAX_LIMBS], t1[EC_MAX_LIMBS], t2[EC_MAX_LIMBS];
    uint64_t x3[EC_MAX_LIMBS], y3[EC_MAX_LIMBS], z3[EC_MAX_LIMBS];

    FMUL(delta, p->z, p->z);               /* delta = Z^2 */
    FMUL(gamma, p->y, p->y);               /* gamma = Y^2 */
    FMUL(beta, p->x, gamma);               /* beta = X gamma */
    FSUB(t1, p->x, delta);
    FADD(t2, p->x, delta);
    FMUL(alpha, t1, t2);
    FADD(t1, alpha, alpha);
    FADD(alpha, t1, alpha);                /* alpha = 3 (X - delta)(X + delta) */

    FMUL(x3, alpha, alpha);
    FADD(t1, beta, beta);                  /* 2 beta */
    FADD(t1, t1, t1);                      /* 4 beta */
    FADD(t2, t1, t1);                      /* 8 beta */
    FSUB(x3, x3, t2);                      /* X3 = alpha^2 - 8 beta */

    FADD(z3, p->y, p->z);
    FMUL(z3, z3, z3);
    FSUB(z3, z3, gamma);
    FSUB(z3, z3, delta);                   /* Z3 = (Y + Z)^2 - gamma - delta */

    FSUB(t1, t1, x3);                      /* 4 beta - X3 */
    FMUL(y3, alpha, t1);
    FMUL(t2, gamma, gamma);
    FADD(t2, t2, t2);
    FADD(t2, t2, t2);
    FADD(t2, t2, t2);                      /* 8 gamma^2 */
    FSUB(y3, y3, t2);                      /* Y3 = alpha (4 beta - X3) - 8 gamma^2 */

    fcopy(c, r->x, x3);
    fcopy(c, r->y, y3);
    fcopy(c, r->z, z3);
}

/* r = p + q. r may alias p or q. */
static void pt_add(const ec_curve *c, ec_point *r, const ec_point *p, const ec_point *q) {
    if (pt_is_inf(c, p)) { *r = *q; return; }
    if (pt_is_inf(c, q)) { *r = *p; return; }
    uint64_t z1z1[EC_MAX_LIMBS], z2z2[EC_MAX_LIMBS], u1[EC_MAX_LIMBS], u2[EC_MAX_LIMBS];
    uint64_t s1[EC_MAX_LIMBS], s2[EC_MAX_LIMBS], h[EC_MAX_LIMBS], i[EC_MAX_LIMBS];
    uint64_t j[EC_MAX_LIMBS], rr[EC_MAX_LIMBS], v[EC_MAX_LIMBS], t[EC_MAX_LIMBS];
    uint64_t x3[EC_MAX_LIMBS], y3[EC_MAX_LIMBS], z3[EC_MAX_LIMBS];

    FMUL(z1z1, p->z, p->z);
    FMUL(z2z2, q->z, q->z);
    FMUL(u1, p->x, z2z2);
    FMUL(u2, q->x, z1z1);
    FMUL(s1, p->y, q->z);
    FMUL(s1, s1, z2z2);                    /* S1 = Y1 Z2^3 */
    FMUL(s2, q->y, p->z);
    FMUL(s2, s2, z1z1);                    /* S2 = Y2 Z1^3 */
    FSUB(h, u2, u1);
    FSUB(rr, s2, s1);
    if (nxc_bn_is_zero(h, c->limbs)) {
        if (nxc_bn_is_zero(rr, c->limbs)) { pt_dbl(c, r, p); return; }  /* p == q */
        pt_set_inf(c, r);                                               /* p == -q */
        return;
    }
    FADD(rr, rr, rr);                      /* r = 2 (S2 - S1) */
    FADD(i, h, h);
    FMUL(i, i, i);                         /* I = (2H)^2 */
    FMUL(j, h, i);                         /* J = H I */
    FMUL(v, u1, i);                        /* V = U1 I */

    FMUL(x3, rr, rr);
    FSUB(x3, x3, j);
    FSUB(x3, x3, v);
    FSUB(x3, x3, v);                       /* X3 = r^2 - J - 2V */

    FSUB(t, v, x3);
    FMUL(y3, rr, t);
    FMUL(t, s1, j);
    FADD(t, t, t);
    FSUB(y3, y3, t);                       /* Y3 = r (V - X3) - 2 S1 J */

    FADD(z3, p->z, q->z);
    FMUL(z3, z3, z3);
    FSUB(z3, z3, z1z1);
    FSUB(z3, z3, z2z2);
    FMUL(z3, z3, h);                       /* Z3 = ((Z1 + Z2)^2 - Z1Z1 - Z2Z2) H */

    fcopy(c, r->x, x3);
    fcopy(c, r->y, y3);
    fcopy(c, r->z, z3);
}

/* r = u1 G + u2 Q (Shamir's trick: one shared doubling chain). */
static void pt_mul2(const ec_curve *c, ec_point *r, const uint64_t *u1,
                    const ec_point *q, const uint64_t *u2) {
    ec_point g, gq;
    fcopy(c, g.x, c->gx);
    fcopy(c, g.y, c->gy);
    fcopy(c, g.z, c->fp.one);
    pt_add(c, &gq, &g, q);
    pt_set_inf(c, r);
    uint32_t bits = c->limbs * 64;
    for (uint32_t k = bits; k-- > 0;) {
        pt_dbl(c, r, r);
        uint32_t b1 = (uint32_t)(u1[k / 64] >> (k % 64)) & 1u;
        uint32_t b2 = (uint32_t)(u2[k / 64] >> (k % 64)) & 1u;
        if (b1 && b2) pt_add(c, r, r, &gq);
        else if (b1) pt_add(c, r, r, &g);
        else if (b2) pt_add(c, r, r, q);
    }
}

/* Affine x of p as an ordinary (non-Montgomery) number. */
static void pt_affine_x(const ec_curve *c, uint64_t *x, const ec_point *p) {
    uint8_t pm2[EC_MAX_BYTES];
    nxc_copy(pm2, c->p_bytes, c->size);
    /* p - 2 (p is odd and large, so only the last byte can borrow). */
    int i = (int)c->size - 1;
    unsigned borrow = 2;
    while (borrow && i >= 0) {
        unsigned v = pm2[i];
        pm2[i] = (uint8_t)(v - borrow);
        borrow = v < borrow ? 1 : 0;
        i--;
    }
    uint64_t zinv[EC_MAX_LIMBS], t[EC_MAX_LIMBS];
    nxc_mont_exp(&c->fp, zinv, p->z, pm2, c->size);   /* Z^(p-2) = 1/Z */
    FMUL(t, zinv, zinv);
    FMUL(t, t, p->x);                                   /* X / Z^2 */
    nxc_mont_from(&c->fp, x, t);
}

/* Is (x, y), in Montgomery form, on y^2 = x^3 - 3x + b? */
static int on_curve(const ec_curve *c, const uint64_t *x, const uint64_t *y) {
    uint64_t lhs[EC_MAX_LIMBS], rhs[EC_MAX_LIMBS], t[EC_MAX_LIMBS];
    FMUL(lhs, y, y);
    FMUL(rhs, x, x);
    FMUL(rhs, rhs, x);
    FADD(t, x, x);
    FADD(t, t, x);
    FSUB(rhs, rhs, t);
    FADD(rhs, rhs, c->b);
    return nxc_bn_cmp(lhs, rhs, c->limbs) == 0;
}

/* Parse a scalar in [1, n-1]; returns 0 on success. */
static int parse_scalar(const ec_curve *c, uint64_t *v, const uint8_t *in, size_t len) {
    if (in == NULL || len == 0) return -1;
    if (nxc_bn_from_be(v, c->limbs, in, len) != 0) return -1;
    if (nxc_bn_is_zero(v, c->limbs)) return -1;
    if (nxc_bn_cmp(v, c->fn.m, c->limbs) >= 0) return -1;
    return 0;
}

int nxc_ecdsa_verify(nxc_ec_curve_t curve,
                     const uint8_t *pub, size_t pub_len,
                     const uint8_t *hash, size_t hash_len,
                     const uint8_t *r, size_t r_len,
                     const uint8_t *s, size_t s_len) {
    if (pub == NULL || hash == NULL || hash_len == 0) return NXC_ERR_ARG;
    ec_curve c;
    int rc = curve_init(&c, curve);
    if (rc != NXC_OK) return rc;
    const uint32_t L = c.limbs;

    /* Public key: uncompressed, coordinates < p, on the curve. */
    if (pub_len != 1 + 2 * (size_t)c.size || pub[0] != 0x04) return NXC_ERR_ENCODING;
    uint64_t qx[EC_MAX_LIMBS], qy[EC_MAX_LIMBS];
    nxc_bn_from_be(qx, L, pub + 1, c.size);
    nxc_bn_from_be(qy, L, pub + 1 + c.size, c.size);
    if (nxc_bn_cmp(qx, c.fp.m, L) >= 0 || nxc_bn_cmp(qy, c.fp.m, L) >= 0) return NXC_ERR_ENCODING;
    ec_point q;
    nxc_mont_to(&c.fp, q.x, qx);
    nxc_mont_to(&c.fp, q.y, qy);
    fcopy(&c, q.z, c.fp.one);
    if (!on_curve(&c, q.x, q.y)) return NXC_ERR_ENCODING;

    /* r, s in [1, n-1] (SEC 1 section 4.1.4 step 1). */
    uint64_t rv[EC_MAX_LIMBS], sv[EC_MAX_LIMBS];
    if (parse_scalar(&c, rv, r, r_len) != 0 || parse_scalar(&c, sv, s, s_len) != 0)
        return NXC_ERR_VERIFY;

    /* e = leftmost bits of the hash (both curve orders are whole bytes). */
    uint64_t e[EC_MAX_LIMBS];
    nxc_bn_from_be(e, L, hash, hash_len < c.size ? hash_len : c.size);
    if (nxc_bn_cmp(e, c.fn.m, L) >= 0) nxc_bn_sub(e, e, c.fn.m, L);   /* e < 2n */

    /* w = s^-1 = s^(n-2) mod n; u1 = e w, u2 = r w. */
    uint8_t nm2[EC_MAX_BYTES];
    nxc_copy(nm2, c.n_bytes, c.size);
    {
        int i = (int)c.size - 1;
        unsigned borrow = 2;
        while (borrow && i >= 0) {
            unsigned v = nm2[i];
            nm2[i] = (uint8_t)(v - borrow);
            borrow = v < borrow ? 1 : 0;
            i--;
        }
    }
    uint64_t sm[EC_MAX_LIMBS], wm[EC_MAX_LIMBS], t[EC_MAX_LIMBS];
    uint64_t u1[EC_MAX_LIMBS], u2[EC_MAX_LIMBS];
    nxc_mont_to(&c.fn, sm, sv);
    nxc_mont_exp(&c.fn, wm, sm, nm2, c.size);
    nxc_mont_to(&c.fn, t, e);
    nxc_mont_mul(&c.fn, t, t, wm);
    nxc_mont_from(&c.fn, u1, t);
    nxc_mont_to(&c.fn, t, rv);
    nxc_mont_mul(&c.fn, t, t, wm);
    nxc_mont_from(&c.fn, u2, t);

    ec_point R;
    pt_mul2(&c, &R, u1, &q, u2);
    if (pt_is_inf(&c, &R)) return NXC_ERR_VERIFY;

    uint64_t x[EC_MAX_LIMBS];
    pt_affine_x(&c, x, &R);
    if (nxc_bn_cmp(x, c.fn.m, L) >= 0) nxc_bn_sub(x, x, c.fn.m, L);  /* x < p < 2n */
    return nxc_bn_cmp(x, rv, L) == 0 ? NXC_OK : NXC_ERR_VERIFY;
}

/* Strict DER INTEGER (positive, minimal). Returns bytes consumed or -1. */
static int der_uint(const uint8_t *p, size_t len, const uint8_t **val, size_t *val_len) {
    if (len < 2 || p[0] != 0x02) return -1;
    size_t l = p[1];
    if (l == 0 || l >= 0x80 || l > len - 2) return -1;
    const uint8_t *v = p + 2;
    if (v[0] & 0x80) return -1;                              /* negative */
    if (l > 1 && v[0] == 0x00 && !(v[1] & 0x80)) return -1;  /* not minimal */
    *val = v;
    *val_len = l;
    return (int)(l + 2);
}

int nxc_ecdsa_verify_der(nxc_ec_curve_t curve,
                         const uint8_t *pub, size_t pub_len,
                         const uint8_t *hash, size_t hash_len,
                         const uint8_t *sig, size_t sig_len) {
    if (sig == NULL) return NXC_ERR_ARG;
    /* SEQUENCE, short-form length (at most 2 * (2 + 49) bytes inside). */
    if (sig_len < 2 || sig[0] != 0x30 || sig[1] >= 0x80 || (size_t)sig[1] != sig_len - 2)
        return NXC_ERR_ENCODING;
    const uint8_t *r, *s;
    size_t rl, sl;
    int a = der_uint(sig + 2, sig_len - 2, &r, &rl);
    if (a < 0) return NXC_ERR_ENCODING;
    int b = der_uint(sig + 2 + a, sig_len - 2 - (size_t)a, &s, &sl);
    if (b < 0 || (size_t)(2 + a + b) != sig_len) return NXC_ERR_ENCODING;
    return nxc_ecdsa_verify(curve, pub, pub_len, hash, hash_len, r, rl, s, sl);
}

/* Self-check for the host test: G on the curve and n G = infinity. */
int nxc__ecdsa_selfcheck(nxc_ec_curve_t curve);
int nxc__ecdsa_selfcheck(nxc_ec_curve_t curve) {
    ec_curve c;
    if (curve_init(&c, curve) != NXC_OK) return 1;
    if (!on_curve(&c, c.gx, c.gy)) return 2;
    ec_point g, inf, r;
    fcopy(&c, g.x, c.gx);
    fcopy(&c, g.y, c.gy);
    fcopy(&c, g.z, c.fp.one);
    pt_set_inf(&c, &inf);
    uint64_t zero[EC_MAX_LIMBS] = { 0 };
    pt_mul2(&c, &r, c.fn.m, &inf, zero);          /* n G */
    if (!pt_is_inf(&c, &r)) return 3;
    uint64_t nm1[EC_MAX_LIMBS], one[EC_MAX_LIMBS] = { 1 };
    nxc_bn_sub(nm1, c.fn.m, one, c.limbs);
    pt_mul2(&c, &r, nm1, &inf, zero);             /* (n-1) G = -G */
    uint64_t x[EC_MAX_LIMBS], gx[EC_MAX_LIMBS];
    pt_affine_x(&c, x, &r);
    nxc_mont_from(&c.fp, gx, c.gx);
    if (nxc_bn_cmp(x, gx, c.limbs) != 0) return 4;
    return 0;
}
