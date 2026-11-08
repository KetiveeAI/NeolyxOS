/*
 * Host test for nxcrypto signature verification: ECDSA P-256/P-384 and
 * RSA PKCS#1 v1.5 / PSS.
 *
 * Published vectors:
 *   NIST CAVP ECDSA SigVer (FIPS 186-3), P-256 and P-384 with SHA-256,
 *     SHA-384 and SHA-512: valid and invalid signatures
 *   NIST CAVP RSA SigVer15_186-3 and SigVerPSS_186-3, 1024 to 4096-bit
 *     moduli with SHA-256/384/512: valid and invalid signatures
 * plus the domain-parameter self-check (G on the curve, n G = infinity,
 * (n-1) G = -G), DER strictness, and range checks for r, s and the key.
 *
 * Build and run (from the repo root): make -C lib/nxcrypto test
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "crypto_vec.h"
#include "nxcrypto.h"

int nxc__ecdsa_selfcheck(nxc_ec_curve_t curve);

static nxc_hash_alg_t alg_from(const char *s) {
    if (strstr(s, "SHA-256") || !strcmp(s, "SHA256")) return NXC_HASH_SHA256;
    if (strstr(s, "SHA-384") || !strcmp(s, "SHA384")) return NXC_HASH_SHA384;
    if (strstr(s, "SHA-512") || !strcmp(s, "SHA512")) return NXC_HASH_SHA512;
    return NXC_HASH_NONE;
}

/* ------------------------------------------------------------------ */
/* ECDSA SigVer                                                        */
/* ------------------------------------------------------------------ */

static int g_ecdsa_pass, g_ecdsa_fail;

static void ecdsa_rec(rec *r) {
    const char *res = rget(r, "Result");
    if (!res || !rget(r, "Msg")) return;
    nxc_ec_curve_t curve = !strncmp(r->sect, "P-256,", 6) ? NXC_EC_P256 :
                           !strncmp(r->sect, "P-384,", 6) ? NXC_EC_P384 : NXC_EC_NONE;
    nxc_hash_alg_t alg = alg_from(r->sect);
    if (curve == NXC_EC_NONE || alg == NXC_HASH_NONE) { r->skipped++; return; }

    size_t ml, xl, yl, rl, sl;
    uint8_t *m = field(r, "Msg", &ml), *qx = field(r, "Qx", &xl), *qy = field(r, "Qy", &yl);
    uint8_t *rv = field(r, "R", &rl), *sv = field(r, "S", &sl);
    size_t size = curve == NXC_EC_P256 ? 32 : 48;
    uint8_t pub[97], h[64];
    CHECK(xl == size && yl == size, "%s vector %d: key size", r->file, r->count);
    pub[0] = 0x04;
    memcpy(pub + 1, qx, size);
    memcpy(pub + 1 + size, qy, size);
    nxc_hash(alg, m, ml, h, sizeof(h));
    int rc = nxc_ecdsa_verify(curve, pub, 1 + 2 * size, h, nxc_hash_digest_len(alg), rv, rl, sv, sl);
    int want_ok = res[0] == 'P';
    /* An invalid key is reported as ENCODING; anything non-OK is a reject. */
    CHECK((rc == NXC_OK) == want_ok, "%s [%s] vector %d: rc %d, expected %s",
          r->file, r->sect, r->count, rc, res);
    if (want_ok) g_ecdsa_pass++; else g_ecdsa_fail++;
    free(m); free(qx); free(qy); free(rv); free(sv);
    r->count++;
}

/* ------------------------------------------------------------------ */
/* RSA SigVer (PKCS#1 v1.5 and PSS)                                    */
/* ------------------------------------------------------------------ */

static uint8_t *g_n;
static size_t g_n_len;
static int g_pss, g_rsa_valid, g_rsa_invalid;

static void rsa_rec(rec *r) {
    size_t l;
    if (rget(r, "n")) {
        free(g_n);
        g_n = field(r, "n", &g_n_len);
    }
    const char *res = rget(r, "Result");
    const char *sha = rget(r, "SHAAlg");
    if (!res || !sha || !g_n) return;
    nxc_hash_alg_t alg = alg_from(sha);
    if (alg == NXC_HASH_NONE) { r->skipped++; return; }   /* SHA-1 / SHA-224 */

    size_t el, ml, sl, saltl = 0;
    uint8_t *e = field(r, "e", &el), *m = field(r, "Msg", &ml), *s = field(r, "S", &sl);
    uint8_t *salt = g_pss ? field(r, "SaltVal", &saltl) : NULL;
    uint8_t h[64];
    nxc_hash(alg, m, ml, h, sizeof(h));
    size_t hl = nxc_hash_digest_len(alg);
    int rc = g_pss ? nxc_rsa_verify_pss(g_n, g_n_len, e, el, alg, h, hl, saltl, s, sl)
                   : nxc_rsa_verify_pkcs1v15(g_n, g_n_len, e, el, alg, h, hl, s, sl);
    int want_ok = res[0] == 'P';
    CHECK((rc == NXC_OK) == want_ok, "%s [%s] %s vector %d: rc %d, expected %s",
          r->file, r->sect, sha, r->count, rc, res);
    if (want_ok) g_rsa_valid++; else g_rsa_invalid++;
    (void)l;
    free(e); free(m); free(s); free(salt);
    r->count++;
}

/* ------------------------------------------------------------------ */
/* Checks without vector files                                         */
/* ------------------------------------------------------------------ */

static void test_edges(const char *dir) {
    CHECK(nxc__ecdsa_selfcheck(NXC_EC_P256) == 0, "P-256 parameter self-check");
    CHECK(nxc__ecdsa_selfcheck(NXC_EC_P384) == 0, "P-384 parameter self-check");

    /* Take the first valid P-256/SHA-256 vector and mutate it. */
    char *text = vec_read_file(dir, "asymmetric/ECDSA/FIPS_186-3/SigVer.rsp");
    char *sec = strstr(text, "[P-256,SHA-256]");
    char *pass = sec ? strstr(sec, "Result = P") : NULL;
    CHECK(pass != NULL, "no valid P-256 vector found");
    if (!pass) { free(text); return; }
    char *start = pass;
    while (start > sec && !(start[-1] == '\n' && (start[0] == '\n' || start[0] == '\r'))) start--;
    char *keys[5] = { "Msg = ", "Qx = ", "Qy = ", "R = ", "S = " };
    uint8_t *v[5];
    size_t vl[5];
    for (int i = 0; i < 5; i++) {
        char *p = strstr(start, keys[i]);
        p += strlen(keys[i]);
        char buf[512];
        size_t n = strcspn(p, "\r\n");
        memcpy(buf, p, n);
        buf[n] = 0;
        v[i] = hex_decode(buf, &vl[i]);
    }
    free(text);

    uint8_t pub[65], h[32];
    pub[0] = 4;
    memcpy(pub + 1, v[1], 32);
    memcpy(pub + 33, v[2], 32);
    nxc_sha256(v[0], vl[0], h);
    CHECK(nxc_ecdsa_verify(NXC_EC_P256, pub, 65, h, 32, v[3], vl[3], v[4], vl[4]) == NXC_OK,
          "base vector verifies");

    /* DER form: build SEQUENCE { INTEGER r, INTEGER s } with minimal ints. */
    uint8_t der[80];
    size_t dl = 2;
    for (int i = 3; i <= 4; i++) {
        const uint8_t *x = v[i];
        size_t xl = vl[i];
        while (xl > 1 && x[0] == 0) { x++; xl--; }
        int pad = x[0] & 0x80 ? 1 : 0;
        der[dl++] = 0x02;
        der[dl++] = (uint8_t)(xl + (size_t)pad);
        if (pad) der[dl++] = 0;
        memcpy(der + dl, x, xl);
        dl += xl;
    }
    der[0] = 0x30;
    der[1] = (uint8_t)(dl - 2);
    CHECK(nxc_ecdsa_verify_der(NXC_EC_P256, pub, 65, h, 32, der, dl) == NXC_OK, "DER verifies");
    der[dl] = 0;
    CHECK(nxc_ecdsa_verify_der(NXC_EC_P256, pub, 65, h, 32, der, dl + 1) == NXC_ERR_ENCODING,
          "DER trailing byte refused");
    uint8_t der2[82];
    memcpy(der2, der, 4);
    der2[3] = (uint8_t)(der[3] + 1);           /* r length + 1, extra 0x00 */
    der2[4] = 0;
    memcpy(der2 + 5, der + 4, dl - 4);
    der2[1] = (uint8_t)(dl - 1);
    CHECK(nxc_ecdsa_verify_der(NXC_EC_P256, pub, 65, h, 32, der2, dl + 1) == NXC_ERR_ENCODING,
          "DER non-minimal integer refused");

    /* r = 0, s = n, s = n + small: all refused. */
    static const uint8_t zero[1] = { 0 };
    CHECK(nxc_ecdsa_verify(NXC_EC_P256, pub, 65, h, 32, zero, 1, v[4], vl[4]) == NXC_ERR_VERIFY,
          "r = 0 refused");
    static const uint8_t n256[32] = {
        0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17, 0x9e, 0x84, 0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51 };
    CHECK(nxc_ecdsa_verify(NXC_EC_P256, pub, 65, h, 32, v[3], vl[3], n256, 32) == NXC_ERR_VERIFY,
          "s = n refused");

    /* Off-curve key, compressed key, wrong length. */
    pub[64] ^= 1;
    CHECK(nxc_ecdsa_verify(NXC_EC_P256, pub, 65, h, 32, v[3], vl[3], v[4], vl[4]) == NXC_ERR_ENCODING,
          "point off the curve refused");
    pub[64] ^= 1;
    pub[0] = 0x02;
    CHECK(nxc_ecdsa_verify(NXC_EC_P256, pub, 33, h, 32, v[3], vl[3], v[4], vl[4]) == NXC_ERR_ENCODING,
          "compressed key refused");
    pub[0] = 0x04;
    CHECK(nxc_ecdsa_verify(NXC_EC_P384, pub, 65, h, 32, v[3], vl[3], v[4], vl[4]) == NXC_ERR_ENCODING,
          "P-256 key on P-384 refused");
    h[0] ^= 1;
    CHECK(nxc_ecdsa_verify(NXC_EC_P256, pub, 65, h, 32, v[3], vl[3], v[4], vl[4]) == NXC_ERR_VERIFY,
          "changed hash refused");
    for (int i = 0; i < 5; i++) free(v[i]);

    /* RSA argument checks. */
    uint8_t n_even[256], e3[1] = { 3 }, e2[1] = { 2 }, sig[256], hh[32] = { 0 };
    memset(n_even, 0xff, sizeof(n_even));
    memset(sig, 0x01, sizeof(sig));
    n_even[255] = 0xfe;
    CHECK(nxc_rsa_verify_pkcs1v15(n_even, 256, e3, 1, NXC_HASH_SHA256, hh, 32, sig, 256) ==
          NXC_ERR_ENCODING, "even RSA modulus refused");
    n_even[255] = 0xff;
    CHECK(nxc_rsa_verify_pkcs1v15(n_even, 256, e2, 1, NXC_HASH_SHA256, hh, 32, sig, 256) ==
          NXC_ERR_ENCODING, "even exponent refused");
    CHECK(nxc_rsa_verify_pkcs1v15(n_even, 64, e3, 1, NXC_HASH_SHA256, hh, 32, sig, 64) ==
          NXC_ERR_LENGTH, "512-bit modulus refused");
    CHECK(nxc_rsa_verify_pkcs1v15(n_even, 256, e3, 1, NXC_HASH_SHA256, hh, 32, sig, 255) ==
          NXC_ERR_VERIFY, "short signature refused");
    CHECK(nxc_rsa_verify_pkcs1v15(n_even, 256, e3, 1, NXC_HASH_SHA256, hh, 31, sig, 256) ==
          NXC_ERR_ARG, "wrong hash length refused");
    uint8_t big[600];
    memset(big, 0xff, sizeof(big));
    CHECK(nxc_rsa_verify_pkcs1v15(big, 600, e3, 1, NXC_HASH_SHA256, hh, 32, big, 600) ==
          NXC_ERR_ENCODING, "4800-bit modulus refused");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("usage: %s <vector dir from crypto_fetch_vectors.sh>\n", argv[0]);
        return 2;
    }
    const char *d = argv[1];
    run_file(d, "asymmetric/ECDSA/FIPS_186-3/SigVer.rsp", ecdsa_rec, 90);
    printf("    ECDSA P-256/P-384: %d valid accepted, %d invalid refused (expected)\n",
           g_ecdsa_pass, g_ecdsa_fail);
    g_pss = 0;
    run_file(d, "asymmetric/RSA/FIPS_186-2/SigVer15_186-3.rsp", rsa_rec, 100);
    g_pss = 1;
    run_file(d, "asymmetric/RSA/FIPS_186-2/SigVerPSS_186-3.rsp", rsa_rec, 100);
    printf("    RSA: %d valid accepted, %d invalid refused (expected)\n", g_rsa_valid, g_rsa_invalid);
    free(g_n);
    test_edges(d);
    return vec_report("crypto_pubkey_test");
}
