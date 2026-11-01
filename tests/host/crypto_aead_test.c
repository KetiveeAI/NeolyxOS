/*
 * Host test for nxcrypto ChaCha20, Poly1305, ChaCha20-Poly1305, AES and
 * AES-GCM.
 *
 * Published vectors:
 *   RFC 8439 (published earlier as RFC 7539) appendix A: ChaCha20 and
 *     Poly1305
 *   ChaCha20-Poly1305: RFC 8439 section 2.8.2 and the BoringSSL and
 *     OpenSSL AEAD test files, including vectors that must fail
 *   NIST CAVP AESAVS ECB KAT and MMT files for AES-128 and AES-256
 *   NIST CAVP GCMVS gcmEncryptExtIV and gcmDecrypt (96-bit IVs) for
 *     AES-128 and AES-256, including the FAIL cases
 * plus negative checks: every single-bit flip of tag, ciphertext and AAD
 * is refused, a refused open leaves no plaintext behind, and in-place
 * operation matches.
 *
 * Build and run (from the repo root): make -C lib/nxcrypto test
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "crypto_vec.h"
#include "nxcrypto.h"

/* ------------------------------------------------------------------ */
/* ChaCha20 and Poly1305 (RFC 8439 appendix A)                         */
/* ------------------------------------------------------------------ */

static void chacha_rec(rec *r) {
    size_t kl, nl, pl, cl;
    uint8_t *k = field(r, "KEY", &kl), *n = field(r, "NONCE", &nl);
    uint8_t *p = field(r, "PLAINTEXT", &pl), *c = field(r, "CIPHERTEXT", &cl);
    const char *ctr = rget(r, "INITIAL_BLOCK_COUNTER");
    if (k && n && p && c && ctr) {
        uint8_t *out = malloc(pl + 1);
        int rc = nxc_chacha20_xor(out, p, pl, k, n, (uint32_t)atol(ctr));
        CHECK(kl == 32 && nl == 12 && pl == cl && rc == NXC_OK && memcmp(out, c, cl) == 0,
              "%s vector %d", r->file, r->count);
        free(out);
        r->count++;
    }
    free(k); free(n); free(p); free(c);
}

static void poly_rec(rec *r) {
    size_t kl, ml, tl;
    uint8_t *k = field(r, "KEY", &kl), *m = field(r, "MSG", &ml), *t = field(r, "TAG", &tl);
    if (k && m && t) {
        uint8_t tag[16];
        nxc_poly1305_ctx ctx;
        int rc = nxc_poly1305_init(&ctx, k);
        if (rc == NXC_OK) rc = nxc_poly1305_update(&ctx, m, ml);
        if (rc == NXC_OK) rc = nxc_poly1305_final(&ctx, tag);
        CHECK(kl == 32 && tl == 16 && rc == NXC_OK && memcmp(tag, t, 16) == 0,
              "%s vector %d", r->file, r->count);
        /* Byte-at-a-time gives the same tag. */
        nxc_poly1305_init(&ctx, k);
        for (size_t i = 0; i < ml; i++) nxc_poly1305_update(&ctx, m + i, 1);
        nxc_poly1305_final(&ctx, tag);
        CHECK(memcmp(tag, t, 16) == 0, "%s vector %d bytewise", r->file, r->count);
        r->count++;
    }
    free(k); free(m); free(t);
}

/* ------------------------------------------------------------------ */
/* AEAD round trip with tamper checks, shared by both AEADs.           */
/* ------------------------------------------------------------------ */

typedef int (*seal_fn)(const void *key, uint8_t *out, uint8_t tag[16], const uint8_t *nonce,
                       const uint8_t *aad, size_t aad_len, const uint8_t *in, size_t len);
typedef int (*open_fn)(const void *key, uint8_t *out, const uint8_t *nonce,
                       const uint8_t *aad, size_t aad_len, const uint8_t *in, size_t len,
                       const uint8_t tag[16]);

static int cp_seal(const void *key, uint8_t *out, uint8_t tag[16], const uint8_t *nonce,
                   const uint8_t *aad, size_t aad_len, const uint8_t *in, size_t len) {
    return nxc_chacha20poly1305_seal(out, tag, key, nonce, aad, aad_len, in, len);
}
static int cp_open(const void *key, uint8_t *out, const uint8_t *nonce,
                   const uint8_t *aad, size_t aad_len, const uint8_t *in, size_t len,
                   const uint8_t tag[16]) {
    return nxc_chacha20poly1305_open(out, key, nonce, aad, aad_len, in, len, tag);
}
static int g_seal(const void *key, uint8_t *out, uint8_t tag[16], const uint8_t *nonce,
                  const uint8_t *aad, size_t aad_len, const uint8_t *in, size_t len) {
    return nxc_gcm_seal(key, out, tag, nonce, aad, aad_len, in, len);
}
static int g_open(const void *key, uint8_t *out, const uint8_t *nonce,
                  const uint8_t *aad, size_t aad_len, const uint8_t *in, size_t len,
                  const uint8_t tag[16]) {
    return nxc_gcm_open(key, out, nonce, aad, aad_len, in, len, tag);
}

/* Checks that need no vectors: flips, in-place, wiped output on failure. */
static void aead_tamper(const char *what, const void *key, seal_fn seal, open_fn open) {
    uint8_t nonce[12], aad[21], pt[77], ct[77], out[77], tag[16];
    for (int i = 0; i < 12; i++) nonce[i] = (uint8_t)(i + 1);
    for (int i = 0; i < 21; i++) aad[i] = (uint8_t)(i * 3);
    for (int i = 0; i < 77; i++) pt[i] = (uint8_t)(i * 7 + 1);

    CHECK(seal(key, ct, tag, nonce, aad, 21, pt, 77) == NXC_OK, "%s seal", what);
    CHECK(open(key, out, nonce, aad, 21, ct, 77, tag) == NXC_OK && !memcmp(out, pt, 77),
          "%s open", what);

    int bad = 0;
    for (int i = 0; i < 16 * 8; i++) {
        tag[i / 8] ^= (uint8_t)(1 << (i % 8));
        memset(out, 0xaa, sizeof(out));
        int rc = open(key, out, nonce, aad, 21, ct, 77, tag);
        uint8_t acc = 0;
        for (int j = 0; j < 77; j++) acc |= out[j];
        if (rc != NXC_ERR_VERIFY || acc != 0) bad++;
        tag[i / 8] ^= (uint8_t)(1 << (i % 8));
    }
    for (int i = 0; i < 77 * 8; i++) {
        ct[i / 8] ^= (uint8_t)(1 << (i % 8));
        if (open(key, out, nonce, aad, 21, ct, 77, tag) != NXC_ERR_VERIFY) bad++;
        ct[i / 8] ^= (uint8_t)(1 << (i % 8));
    }
    for (int i = 0; i < 21 * 8; i++) {
        aad[i / 8] ^= (uint8_t)(1 << (i % 8));
        if (open(key, out, nonce, aad, 21, ct, 77, tag) != NXC_ERR_VERIFY) bad++;
        aad[i / 8] ^= (uint8_t)(1 << (i % 8));
    }
    nonce[11] ^= 1;
    if (open(key, out, nonce, aad, 21, ct, 77, tag) != NXC_ERR_VERIFY) bad++;
    nonce[11] ^= 1;
    CHECK(bad == 0, "%s: %d single-bit changes were accepted or left plaintext", what, bad);

    /* In place. */
    uint8_t buf[77], tag2[16];
    memcpy(buf, pt, 77);
    CHECK(seal(key, buf, tag2, nonce, aad, 21, buf, 77) == NXC_OK &&
          !memcmp(buf, ct, 77) && !memcmp(tag2, tag, 16), "%s in-place seal", what);
    CHECK(open(key, buf, nonce, aad, 21, buf, 77, tag2) == NXC_OK && !memcmp(buf, pt, 77),
          "%s in-place open", what);

    /* Empty message and AAD. */
    CHECK(seal(key, NULL, tag2, nonce, NULL, 0, NULL, 0) == NXC_OK &&
          open(key, NULL, nonce, NULL, 0, NULL, 0, tag2) == NXC_OK, "%s empty", what);
    CHECK(open(key, out, nonce, NULL, 5, ct, 77, tag) == NXC_ERR_ARG, "%s NULL aad with length", what);
}

/* ------------------------------------------------------------------ */
/* ChaCha20-Poly1305 vector files                                      */
/* ------------------------------------------------------------------ */

static void cp_check(rec *r, const uint8_t *k, size_t kl, const uint8_t *n, size_t nl,
                     const uint8_t *a, size_t al, const uint8_t *p, size_t pl,
                     const uint8_t *c, size_t cl, const uint8_t *t, size_t tl, int must_fail) {
    if (kl != 32 || nl != 12 || tl != 16 || pl != cl) {
        CHECK(0, "%s vector %d has unexpected sizes", r->file, r->count);
        return;
    }
    uint8_t *out = malloc(pl + 1), tag[16];
    if (!must_fail) {
        int rc = nxc_chacha20poly1305_seal(out, tag, k, n, a, al, p, pl);
        CHECK(rc == NXC_OK && !memcmp(out, c, cl) && !memcmp(tag, t, 16),
              "%s vector %d seal", r->file, r->count);
    }
    int rc = nxc_chacha20poly1305_open(out, k, n, a, al, c, cl, t);
    if (must_fail)
        CHECK(rc == NXC_ERR_VERIFY, "%s vector %d must fail, got %d", r->file, r->count, rc);
    else
        CHECK(rc == NXC_OK && !memcmp(out, p, pl), "%s vector %d open", r->file, r->count);
    free(out);
    r->count++;
}

static void cp_openssl_rec(rec *r) {
    size_t kl, nl, al, pl, cl, tl;
    uint8_t *k = field(r, "Key", &kl), *n = field(r, "IV", &nl), *a = field(r, "AAD", &al);
    uint8_t *p = field(r, "Plaintext", &pl), *c = field(r, "Ciphertext", &cl);
    uint8_t *t = field(r, "Tag", &tl);
    if (k && n && p && c && t)
        cp_check(r, k, kl, n, nl, a, al, p, pl, c, cl, t, tl, rget(r, "Result") != NULL);
    free(k); free(n); free(a); free(p); free(c); free(t);
}

static void cp_boring_rec(rec *r) {
    size_t kl, nl, al, pl, cl, tl;
    uint8_t *k = field(r, "KEY", &kl), *n = field(r, "NONCE", &nl), *a = field(r, "AD", &al);
    uint8_t *p = field(r, "IN", &pl), *c = field(r, "CT", &cl), *t = field(r, "TAG", &tl);
    if (k && n && p && c && t) cp_check(r, k, kl, n, nl, a, al, p, pl, c, cl, t, tl, 0);
    free(k); free(n); free(a); free(p); free(c); free(t);
}

/* ------------------------------------------------------------------ */
/* AES ECB KAT / MMT (block function)                                  */
/* ------------------------------------------------------------------ */

static void ecb_rec(rec *r) {
    size_t kl, pl, cl;
    uint8_t *k = field(r, "KEY", &kl), *p = field(r, "PLAINTEXT", &pl);
    uint8_t *c = field(r, "CIPHERTEXT", &cl);
    /* Decrypt-section records list CIPHERTEXT first; the pair is the same. */
    if (k && p && c && pl == cl && pl % 16 == 0) {
        nxc_aes_ctx ctx;
        int ok = nxc_aes_init(&ctx, k, kl) == NXC_OK;
        uint8_t blk[16];
        for (size_t i = 0; ok && i < pl; i += 16) {
            ok = nxc_aes_encrypt_block(&ctx, blk, p + i) == NXC_OK && !memcmp(blk, c + i, 16);
        }
        CHECK(ok, "%s vector %d", r->file, r->count);
        r->count++;
    }
    free(k); free(p); free(c);
}

/* ------------------------------------------------------------------ */
/* AES-GCM CAVP                                                        */
/* ------------------------------------------------------------------ */

static void gcm_rec(rec *r) {
    size_t kl, il, pl, al, cl, tl;
    if (!rget(r, "Key") || !rget(r, "Tag")) return;
    if (r->sect_ivlen != 96) { r->skipped++; return; }
    uint8_t *k = field(r, "Key", &kl), *iv = field(r, "IV", &il), *a = field(r, "AAD", &al);
    uint8_t *c = field(r, "CT", &cl), *t = field(r, "Tag", &tl), *p = field(r, "PT", &pl);
    int must_fail = rget(r, "FAIL") != NULL;
    nxc_gcm_ctx g;
    CHECK(nxc_gcm_init(&g, k, kl) == NXC_OK && il == 12, "%s vector %d init", r->file, r->count);
    uint8_t *out = malloc(cl + 1), tag[16];

    if (!must_fail && p) {                  /* seal: CT and the tag prefix must match */
        int rc = nxc_gcm_seal(&g, out, tag, iv, a, al, p, pl);
        CHECK(rc == NXC_OK && pl == cl && !memcmp(out, c, cl) && !memcmp(tag, t, tl),
              "%s vector %d seal (Taglen %ld)", r->file, r->count, r->sect_taglen);
    }
    if (tl == 16) {                         /* open accepts full-length tags only */
        int rc = nxc_gcm_open(&g, out, iv, a, al, c, cl, t);
        if (must_fail)
            CHECK(rc == NXC_ERR_VERIFY, "%s vector %d must fail", r->file, r->count);
        else
            CHECK(rc == NXC_OK && !memcmp(out, p, pl), "%s vector %d open", r->file, r->count);
    }
    free(out); free(k); free(iv); free(a); free(c); free(t); free(p);
    nxc_gcm_wipe(&g);
    r->count++;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("usage: %s <vector dir from crypto_fetch_vectors.sh>\n", argv[0]);
        return 2;
    }
    const char *d = argv[1];
    run_file(d, "ciphers/ChaCha20/rfc7539.txt", chacha_rec, 3);
    run_file(d, "poly1305/rfc7539.txt", poly_rec, 11);
    run_file(d, "ciphers/ChaCha20Poly1305/openssl.txt", cp_openssl_rec, 5);
    run_file(d, "ciphers/ChaCha20Poly1305/boringssl.txt", cp_boring_rec, 60);

    static const char *ecb[] = {
        "ECBGFSbox128.rsp", "ECBKeySbox128.rsp", "ECBVarKey128.rsp", "ECBVarTxt128.rsp", "ECBMMT128.rsp",
        "ECBGFSbox256.rsp", "ECBKeySbox256.rsp", "ECBVarKey256.rsp", "ECBVarTxt256.rsp", "ECBMMT256.rsp",
    };
    for (size_t i = 0; i < sizeof(ecb) / sizeof(ecb[0]); i++) {
        char path[96];
        snprintf(path, sizeof(path), "ciphers/AES/ECB/%s", ecb[i]);
        run_file(d, path, ecb_rec, 10);
    }
    run_file(d, "ciphers/AES/GCM/gcmEncryptExtIV128.rsp", gcm_rec, 1000);
    run_file(d, "ciphers/AES/GCM/gcmEncryptExtIV256.rsp", gcm_rec, 1000);
    run_file(d, "ciphers/AES/GCM/gcmDecrypt128.rsp", gcm_rec, 1000);
    run_file(d, "ciphers/AES/GCM/gcmDecrypt256.rsp", gcm_rec, 1000);

    uint8_t key[32];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)(0x40 + i);
    aead_tamper("ChaCha20-Poly1305", key, cp_seal, cp_open);
    nxc_gcm_ctx g128, g256;
    nxc_gcm_init(&g128, key, 16);
    nxc_gcm_init(&g256, key, 32);
    aead_tamper("AES-128-GCM", &g128, g_seal, g_open);
    aead_tamper("AES-256-GCM", &g256, g_seal, g_open);

    CHECK(nxc_aes_init(&g128.aes, key, 24) == NXC_ERR_ARG, "AES-192 key length is refused");
    uint8_t zero[64] = { 0 };
    CHECK(nxc_chacha20_xor(zero, zero, 64, key, key, 0xffffffffu) == NXC_OK &&
          nxc_chacha20_xor(zero, zero, 65, key, key, 0xffffffffu) == NXC_ERR_LENGTH,
          "ChaCha20 counter wrap is refused");
    return vec_report("crypto_aead_test");
}
