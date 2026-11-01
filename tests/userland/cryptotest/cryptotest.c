/*
 * NeolyxOS cryptotest - runs lib/nxcrypto inside NeolyxOS.
 *
 * The full published vector suites run on the build host
 * (make -C lib/nxcrypto test). This program checks that the same
 * freestanding library behaves identically on NeolyxOS itself, with
 * known answers taken from those published sets:
 *
 *   SHA-256, SHA-384, SHA-512 of the empty message (NIST CAVP ShortMsg)
 *   HMAC-SHA-256 RFC 4231 test case 2, HKDF-SHA-256 RFC 5869 A.1
 *   X25519 RFC 7748 section 6.1 (both sides of the exchange)
 *   Ed25519 RFC 8032 section 7.1 TEST 2 (key, signature, verify) and
 *   rejection of a tampered message
 *   ChaCha20-Poly1305 RFC 8439 section 2.8.2, AES-128-GCM (NIST CAVP
 *   gcmEncryptExtIV128, PTlen 408 / AADlen 160 / Taglen 128, Count 0),
 *   and rejection of a flipped tag bit for both
 *
 * It also times Ed25519, X25519 and the two ciphers in the VM.
 * Results go to the serial log as "[CRYPTOTEST] PASS/FAIL ..." ending
 * with "[CRYPTOTEST] RESULT: PASS" or "[CRYPTOTEST] RESULT: FAIL".
 * Exit code 0 on PASS, 1 on FAIL.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "nxcrypto.h"

#define SYS_EXIT        1
#define SYS_TIME        3
#define SYS_DEBUG_PRINT 127

static inline int64_t sc1(int64_t num, int64_t a1) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static char g_line[200];
static int g_len;
static int g_fail;

static void out_str(const char *s) {
    while (*s && g_len < (int)sizeof(g_line) - 2) g_line[g_len++] = *s++;
}

static void out_dec(uint64_t u) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (u == 0) buf[--i] = '0';
    while (u > 0 && i > 0) { buf[--i] = (char)('0' + u % 10); u /= 10; }
    out_str(&buf[i]);
}

static void flush(void) {
    g_line[g_len++] = '\n';
    g_line[g_len] = '\0';
    sc1(SYS_DEBUG_PRINT, (int64_t)g_line);
    g_len = 0;
}

static uint64_t now_ms(void) {
    return (uint64_t)sc1(SYS_TIME, 0);
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Decode exactly n bytes of lowercase hex; returns 0 on success. */
static int unhex(const char *h, uint8_t *out, int n) {
    for (int i = 0; i < n; i++) {
        int hi = hexval(h[2 * i]), lo = hexval(h[2 * i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return h[2 * n] == '\0' ? 0 : -1;
}

static int same(const uint8_t *a, const uint8_t *b, int n) {
    return nxc_ct_equal(a, b, (size_t)n);
}

static void report(const char *name, int ok) {
    out_str("[CRYPTOTEST] ");
    out_str(ok ? "PASS " : "FAIL ");
    out_str(name);
    flush();
    if (!ok) g_fail = 1;
}

/* Compare a computed value with the expected hex string. */
static void expect(const char *name, int rc, const uint8_t *got, const char *hex, int n) {
    uint8_t want[64];
    int ok = rc == NXC_OK && n <= 64 && unhex(hex, want, n) == 0 && same(got, want, n);
    report(name, ok);
}

static void test_hashes(void) {
    uint8_t d[64];
    expect("SHA-256 empty", nxc_sha256(NULL, 0, d),
           d, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", 32);
    expect("SHA-384 empty", nxc_sha384(NULL, 0, d), d,
           "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da"
           "274edebfe76f65fbd51ad2f14898b95b", 48);
    expect("SHA-512 empty", nxc_sha512(NULL, 0, d), d,
           "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
           "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e", 64);

    static const char key[] = "Jefe";
    static const char msg[] = "what do ya want for nothing?";
    expect("HMAC-SHA-256 RFC 4231 case 2",
           nxc_hmac(NXC_HASH_SHA256, key, 4, msg, sizeof(msg) - 1, d, sizeof(d)), d,
           "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", 32);

    uint8_t ikm[22], salt[13], info[10], prk[32], okm[42];
    for (int i = 0; i < 22; i++) ikm[i] = 0x0b;
    for (int i = 0; i < 13; i++) salt[i] = (uint8_t)i;
    for (int i = 0; i < 10; i++) info[i] = (uint8_t)(0xf0 + i);
    int rc = nxc_hkdf_extract(NXC_HASH_SHA256, salt, 13, ikm, 22, prk, 32);
    expect("HKDF-SHA-256 RFC 5869 A.1 PRK", rc, prk,
           "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5", 32);
    rc = nxc_hkdf_expand(NXC_HASH_SHA256, prk, 32, info, 10, okm, 42);
    uint8_t want[42];
    int ok = rc == NXC_OK &&
             unhex("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
                   "34007208d5b887185865", want, 42) == 0 && same(okm, want, 42);
    report("HKDF-SHA-256 RFC 5869 A.1 OKM", ok);
}

static void test_x25519(void) {
    uint8_t a[32], b[32], apub[32], bpub[32], s1[32], s2[32];
    unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a, 32);
    unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", b, 32);
    expect("X25519 RFC 7748 6.1 Alice public", nxc_x25519_public(apub, a), apub,
           "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", 32);
    expect("X25519 RFC 7748 6.1 Bob public", nxc_x25519_public(bpub, b), bpub,
           "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", 32);

    uint64_t t0 = now_ms();
    int rc = nxc_x25519(s1, a, bpub);
    uint64_t t1 = now_ms();
    expect("X25519 RFC 7748 6.1 shared (Alice)", rc, s1,
           "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", 32);
    expect("X25519 RFC 7748 6.1 shared (Bob)", nxc_x25519(s2, b, apub), s2,
           "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", 32);

    uint8_t zero[32] = { 0 };
    report("X25519 low-order point refused", nxc_x25519(s2, a, zero) == NXC_ERR_WEAK);

    out_str("[CRYPTOTEST] time X25519 one exchange: ");
    out_dec(t1 - t0);
    out_str(" ms");
    flush();
}

static void test_ed25519(void) {
    /* RFC 8032 section 7.1 TEST 2: one-byte message 0x72. */
    uint8_t seed[32], pub[32], sig[64];
    uint8_t msg[1] = { 0x72 };
    unhex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", seed, 32);
    expect("Ed25519 RFC 8032 TEST 2 public key", nxc_ed25519_public(pub, seed), pub,
           "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", 32);

    uint64_t t0 = now_ms();
    int rc = nxc_ed25519_sign(sig, msg, 1, seed);
    uint64_t t1 = now_ms();
    expect("Ed25519 RFC 8032 TEST 2 signature", rc, sig,
           "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
           "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00", 64);

    uint64_t t2 = now_ms();
    rc = nxc_ed25519_verify(sig, msg, 1, pub);
    uint64_t t3 = now_ms();
    report("Ed25519 RFC 8032 TEST 2 verify", rc == NXC_OK);

    msg[0] ^= 1;
    report("Ed25519 tampered message refused", nxc_ed25519_verify(sig, msg, 1, pub) == NXC_ERR_VERIFY);

    out_str("[CRYPTOTEST] time Ed25519 sign: ");
    out_dec(t1 - t0);
    out_str(" ms, verify: ");
    out_dec(t3 - t2);
    out_str(" ms");
    flush();
}

/* Bulk-throughput sample: seal 64 KiB `rounds` times, report KiB/s. */
static uint8_t g_bulk[65536];

static void time_cipher(const char *name, int which, const nxc_gcm_ctx *g, const uint8_t *key) {
    uint8_t nonce[12] = { 0 }, tag[16];
    int rounds = which == 0 ? 32 : 2;
    uint64_t t0 = now_ms();
    for (int i = 0; i < rounds; i++) {
        nonce[0] = (uint8_t)i;
        if (which == 0)
            nxc_chacha20poly1305_seal(g_bulk, tag, key, nonce, NULL, 0, g_bulk, sizeof(g_bulk));
        else
            nxc_gcm_seal(g, g_bulk, tag, nonce, NULL, 0, g_bulk, sizeof(g_bulk));
    }
    uint64_t ms = now_ms() - t0;
    out_str("[CRYPTOTEST] time ");
    out_str(name);
    out_str(": ");
    out_dec(ms ? (uint64_t)rounds * 64 * 1000 / ms : 0);
    out_str(" KiB/s");
    flush();
}

static void test_aead(void) {
    static const char pt_text[] = "Ladies and Gentlemen of the class of '99: If I could offer you "
                                  "only one tip for the future, sunscreen would be it.";
    uint8_t key[32], nonce[12], aad[20], ct[114], out[114], tag[16];
    unhex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f", key, 32);
    unhex("070000004041424344454647", nonce, 12);
    unhex("50515253c0c1c2c3c4c5c6c7", aad, 12);
    int rc = nxc_chacha20poly1305_seal(ct, tag, key, nonce, aad, 12,
                                       (const uint8_t *)pt_text, 114);
    uint8_t want[114];
    int ok = rc == NXC_OK &&
             unhex("d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d6"
                   "3dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b36"
                   "92ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc"
                   "3ff4def08e4b7a9de576d26586cec64b6116", want, 114) == 0 &&
             same(ct, want, 114);
    report("ChaCha20-Poly1305 RFC 8439 2.8.2 ciphertext", ok);
    expect("ChaCha20-Poly1305 RFC 8439 2.8.2 tag", rc, tag, "1ae10b594f09e26a7e902ecbd0600691", 16);
    rc = nxc_chacha20poly1305_open(out, key, nonce, aad, 12, ct, 114, tag);
    report("ChaCha20-Poly1305 open", rc == NXC_OK && same(out, (const uint8_t *)pt_text, 114));
    tag[5] ^= 0x20;
    report("ChaCha20-Poly1305 flipped tag refused",
           nxc_chacha20poly1305_open(out, key, nonce, aad, 12, ct, 114, tag) == NXC_ERR_VERIFY);

    uint8_t gk[16], giv[12], gpt[51], gct[51];
    nxc_gcm_ctx g;
    unhex("fe47fcce5fc32665d2ae399e4eec72ba", gk, 16);
    unhex("5adb9609dbaeb58cbd6e7275", giv, 12);
    unhex("7c0e88c88899a779228465074797cd4c2e1498d259b54390b85e3eef1c02df60"
          "e743f1b840382c4bccaf3bafb4ca8429bea063", gpt, 51);
    unhex("88319d6e1d3ffa5f987199166c8a9b56c2aeba5a", aad, 20);
    rc = nxc_gcm_init(&g, gk, 16);
    if (rc == NXC_OK) rc = nxc_gcm_seal(&g, gct, tag, giv, aad, 20, gpt, 51);
    uint8_t gwant[51];
    ok = rc == NXC_OK &&
         unhex("98f4826f05a265e6dd2be82db241c0fbbbf9ffb1c173aa83964b7cf539304373"
               "6365253ddbc5db8778371495da76d269e5db3e", gwant, 51) == 0 && same(gct, gwant, 51);
    report("AES-128-GCM CAVP ciphertext", ok);
    expect("AES-128-GCM CAVP tag", rc, tag, "291ef1982e4defedaa2249f898556b47", 16);
    tag[0] ^= 1;
    report("AES-128-GCM flipped tag refused",
           nxc_gcm_open(&g, out, giv, aad, 20, gct, 51, tag) == NXC_ERR_VERIFY);

    time_cipher("ChaCha20-Poly1305", 0, &g, key);
    time_cipher("AES-128-GCM", 1, &g, key);
    nxc_gcm_wipe(&g);
}

void _start(void) {
    out_str("[CRYPTOTEST] start (lib/nxcrypto known-answer tests)");
    flush();
    test_hashes();
    test_x25519();
    test_ed25519();
    test_aead();
    out_str(g_fail ? "[CRYPTOTEST] RESULT: FAIL" : "[CRYPTOTEST] RESULT: PASS");
    flush();
    sc1(SYS_EXIT, g_fail ? 1 : 0);
    for (;;) { __asm__ volatile("pause"); }
}
