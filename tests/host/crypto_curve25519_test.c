/*
 * Host test for nxcrypto X25519 (RFC 7748) and Ed25519 (RFC 8032).
 *
 * Published vectors:
 *   RFC 7748 section 5.2 (scalar multiplication), from the vector set
 *   RFC 7748 section 5.2 iterated test (1 and 1000 iterations) and the
 *     section 6.1 Diffie-Hellman example (embedded below, as printed in
 *     the RFC)
 *   Ed25519 "sign.input": 1024 key / message / signature triples from
 *     the Ed25519 reference test set (includes the RFC 8032 section 7.1
 *     TEST 1-3 keys)
 * plus negative checks: tampered messages and signatures, S >= L,
 * non-canonical public keys, low-order X25519 points.
 *
 * Build and run (from the repo root): make -C lib/nxcrypto test
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "crypto_vec.h"
#include "nxcrypto.h"

int nxc__ed25519_selfcheck(void);

static void unhex(const char *h, uint8_t *out, size_t n) {
    if (hex_fixed(h, out, n) != 0) {
        printf("ERROR: bad embedded hex\n");
        exit(2);
    }
}

/* ------------------------------------------------------------------ */
/* X25519                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t k[32], u[32];
    int have_k, have_u, count;
} x_state;

static void x_cb(void *vctx, const char *name, const char *value) {
    x_state *s = vctx;
    if (!strcmp(name, "INPUT_SCALAR")) s->have_k = hex_fixed(value, s->k, 32) == 0;
    else if (!strcmp(name, "INPUT_U")) s->have_u = hex_fixed(value, s->u, 32) == 0;
    else if (!strcmp(name, "OUTPUT_U") && s->have_k && s->have_u) {
        uint8_t want[32], got[32];
        CHECK(hex_fixed(value, want, 32) == 0, "bad OUTPUT_U");
        int rc = nxc_x25519(got, s->k, s->u);
        CHECK(rc == NXC_OK && memcmp(got, want, 32) == 0, "X25519 vector %d", s->count + 1);
        s->have_k = s->have_u = 0;
        s->count++;
    }
}

static void test_x25519(const char *dir) {
    const char *file = "asymmetric/X25519/rfc7748.txt";
    x_state s;
    memset(&s, 0, sizeof(s));
    char *text = vec_read_file(dir, file);
    vec_parse(text, x_cb, &s);
    free(text);
    CHECK(s.count >= 3, "%s: %d vectors", file, s.count);
    printf("  %-34s %d vectors\n", file, s.count);

    /* RFC 7748 section 5.2: k = u = 9; repeat k, u = X25519(k, u), k. */
    uint8_t k[32] = { 9 }, u[32] = { 9 }, out[32], want1[32], want1000[32];
    unhex("422c8e7a6227d7bca1350b3e2bb7279f7897b87bb6854b783c60e80311ae3079", want1, 32);
    unhex("684cf59ba83309552800ef566f2f4d3c1c3887c49360e3875f2eb94d99532c51", want1000, 32);
    for (int i = 1; i <= 1000; i++) {
        nxc_x25519(out, k, u);
        memcpy(u, k, 32);
        memcpy(k, out, 32);
        if (i == 1) CHECK(memcmp(k, want1, 32) == 0, "X25519 iterated, 1");
    }
    CHECK(memcmp(k, want1000, 32) == 0, "X25519 iterated, 1000");
    printf("  %-34s 1 and 1000 iterations\n", "RFC 7748 5.2 iterated");

    /* RFC 7748 section 6.1 */
    uint8_t a[32], b[32], apub[32], bpub[32], want[32], s1[32], s2[32];
    unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a, 32);
    unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", b, 32);
    nxc_x25519_public(apub, a);
    nxc_x25519_public(bpub, b);
    unhex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", want, 32);
    CHECK(memcmp(apub, want, 32) == 0, "RFC 7748 6.1 Alice public");
    unhex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", want, 32);
    CHECK(memcmp(bpub, want, 32) == 0, "RFC 7748 6.1 Bob public");
    unhex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", want, 32);
    CHECK(nxc_x25519(s1, a, bpub) == NXC_OK && memcmp(s1, want, 32) == 0, "RFC 7748 6.1 shared (A)");
    CHECK(nxc_x25519(s2, b, apub) == NXC_OK && memcmp(s2, want, 32) == 0, "RFC 7748 6.1 shared (B)");
    printf("  %-34s Diffie-Hellman\n", "RFC 7748 6.1");

    /* Low-order points give an all-zero secret, which must be refused:
     * u = 0 and u = 1 (order 4 / identity-like on the twist). */
    uint8_t zero[32] = { 0 }, onep[32] = { 1 };
    CHECK(nxc_x25519(out, a, zero) == NXC_ERR_WEAK, "u = 0 refused");
    CHECK(nxc_x25519(out, a, onep) == NXC_ERR_WEAK, "u = 1 refused");
    CHECK(nxc_x25519(NULL, a, onep) == NXC_ERR_ARG, "NULL out");

    /* Bit 255 of u is ignored (RFC 7748 section 5). */
    uint8_t up[32];
    memcpy(up, bpub, 32);
    up[31] |= 0x80;
    CHECK(nxc_x25519(s2, a, up) == NXC_OK && memcmp(s1, s2, 32) == 0, "top bit of u ignored");
}

/* ------------------------------------------------------------------ */
/* Ed25519                                                             */
/* ------------------------------------------------------------------ */

static void test_ed25519_sign_input(const char *dir) {
    const char *file = "asymmetric/Ed25519/sign.input";
    char *text = vec_read_file(dir, file);
    int count = 0;
    char *line = text;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        /* sk||pk : pk : msg : sig||msg : */
        char *f[4];
        char *p = line;
        int nf = 0;
        while (nf < 4) {
            char *c = strchr(p, ':');
            if (!c) break;
            *c = 0;
            f[nf++] = p;
            p = c + 1;
        }
        if (nf == 4) {
            size_t skl, pkl, ml, sgl;
            uint8_t *sk = hex_decode(f[0], &skl), *pk = hex_decode(f[1], &pkl);
            uint8_t *m = hex_decode(f[2], &ml), *sg = hex_decode(f[3], &sgl);
            CHECK(sk && pk && m && sg && skl == 64 && pkl == 32 && sgl == 64 + ml,
                  "sign.input line %d malformed", count + 1);
            if (sk && pk && m && sg && skl == 64 && pkl == 32 && sgl == 64 + ml) {
                uint8_t pub[32], sig[64];
                CHECK(nxc_ed25519_public(pub, sk) == NXC_OK && memcmp(pub, pk, 32) == 0,
                      "Ed25519 public key, line %d", count + 1);
                CHECK(nxc_ed25519_sign(sig, ml ? m : NULL, ml, sk) == NXC_OK &&
                      memcmp(sig, sg, 64) == 0, "Ed25519 sign, line %d", count + 1);
                CHECK(nxc_ed25519_verify(sg, ml ? m : NULL, ml, pk) == NXC_OK,
                      "Ed25519 verify, line %d", count + 1);

                /* Flip one bit of the message, R, and S: all must fail. */
                if (ml > 0) {
                    m[count % ml] ^= 0x01;
                    CHECK(nxc_ed25519_verify(sg, m, ml, pk) != NXC_OK,
                          "tampered message accepted, line %d", count + 1);
                    m[count % ml] ^= 0x01;
                }
                sig[0] ^= 0x10;
                CHECK(nxc_ed25519_verify(sig, ml ? m : NULL, ml, pk) != NXC_OK,
                      "tampered R accepted, line %d", count + 1);
                sig[0] ^= 0x10;
                sig[40] ^= 0x01;
                CHECK(nxc_ed25519_verify(sig, ml ? m : NULL, ml, pk) != NXC_OK,
                      "tampered S accepted, line %d", count + 1);
            }
            free(sk); free(pk); free(m); free(sg);
            count++;
        }
        line = nl ? nl + 1 : NULL;
    }
    free(text);
    CHECK(count == 1024, "%s: %d vectors, want 1024", file, count);
    printf("  %-34s %d vectors\n", file, count);
}

/* L as little-endian bytes, for building S >= L. */
static const uint8_t L_BYTES[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
};

static void test_ed25519_edges(void) {
    /* RFC 8032 section 7.1 TEST 1 (empty message). */
    uint8_t seed[32], pub[32], sig[64], want_pub[32], want_sig[64];
    unhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", seed, 32);
    unhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", want_pub, 32);
    unhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
          "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b", want_sig, 64);
    CHECK(nxc_ed25519_public(pub, seed) == NXC_OK && memcmp(pub, want_pub, 32) == 0, "TEST 1 pub");
    CHECK(nxc_ed25519_sign(sig, NULL, 0, seed) == NXC_OK && memcmp(sig, want_sig, 64) == 0,
          "TEST 1 sig");
    CHECK(nxc_ed25519_verify(sig, NULL, 0, pub) == NXC_OK, "TEST 1 verify");

    /* S + L is the same point equation but must be refused (malleability). */
    uint8_t mal[64];
    memcpy(mal, sig, 64);
    unsigned carry = 0;
    for (int i = 0; i < 32; i++) {
        unsigned t = mal[32 + i] + L_BYTES[i] + carry;
        mal[32 + i] = (uint8_t)t;
        carry = t >> 8;
    }
    CHECK(carry == 0 && nxc_ed25519_verify(mal, NULL, 0, pub) == NXC_ERR_ENCODING, "S + L refused");
    memcpy(mal, sig, 64);
    memcpy(mal + 32, L_BYTES, 32);
    CHECK(nxc_ed25519_verify(mal, NULL, 0, pub) == NXC_ERR_ENCODING, "S = L refused");

    /* Non-canonical public key: y = p (encodes 0 non-canonically). */
    uint8_t bad[32];
    memset(bad, 0xff, 32);
    bad[0] = 0xed;
    bad[31] = 0x7f;
    CHECK(nxc_ed25519_verify(sig, NULL, 0, bad) == NXC_ERR_ENCODING, "y = p refused");
    /* y = 2 has no x on the curve. */
    memset(bad, 0, 32);
    bad[0] = 2;
    CHECK(nxc_ed25519_verify(sig, NULL, 0, bad) == NXC_ERR_ENCODING, "y with no x refused");
    /* x = 0 with the sign bit set ("-0"): y = 1 is the identity. */
    memset(bad, 0, 32);
    bad[0] = 1;
    bad[31] = 0x80;
    CHECK(nxc_ed25519_verify(sig, NULL, 0, bad) == NXC_ERR_ENCODING, "-0 refused");

    /* A signature from one key must not verify under another. */
    uint8_t seed2[32], pub2[32];
    memcpy(seed2, seed, 32);
    seed2[0] ^= 1;
    nxc_ed25519_public(pub2, seed2);
    CHECK(nxc_ed25519_verify(sig, NULL, 0, pub2) == NXC_ERR_VERIFY, "wrong key refused");

    CHECK(nxc_ed25519_sign(sig, NULL, 5, seed) == NXC_ERR_ARG, "sign NULL msg with length");
    CHECK(nxc_ed25519_verify(NULL, NULL, 0, pub) == NXC_ERR_ARG, "verify NULL sig");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("usage: %s <vector dir from crypto_fetch_vectors.sh>\n", argv[0]);
        return 2;
    }
    int sc = nxc__ed25519_selfcheck();
    CHECK(sc == 0, "Ed25519 constants self-check, bits 0x%x", sc);
    test_x25519(argv[1]);
    test_ed25519_sign_input(argv[1]);
    test_ed25519_edges();
    return vec_report("crypto_curve25519_test");
}
