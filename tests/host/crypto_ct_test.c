/*
 * Constant-time check for nxcrypto, run under Valgrind memcheck.
 *
 * Secret inputs (keys, seeds, scalars, plaintext) are marked
 * "undefined" with VALGRIND_MAKE_MEM_UNDEFINED. Memcheck then reports
 * any conditional branch or memory index that depends on them
 * ("Conditional jump or move depends on uninitialised value(s)" /
 * "Use of uninitialised value of size 8"), which is exactly a timing
 * leak. Results are marked defined again before the test looks at them.
 *
 * Build and run (from the repo root): make -C lib/nxcrypto ct-test
 * The test passes when Valgrind reports 0 errors.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <valgrind/memcheck.h>

#include "nxcrypto.h"

#define SECRET(p, n) VALGRIND_MAKE_MEM_UNDEFINED((p), (n))
#define PUBLIC(p, n) VALGRIND_MAKE_MEM_DEFINED((p), (n))

int main(void) {
    uint8_t key[32], seed[32], msg[200], out[64], pub[32], sig[64], peer[32];
    for (int i = 0; i < 32; i++) {
        key[i] = (uint8_t)(i * 13 + 1);
        seed[i] = (uint8_t)(i * 29 + 7);
    }
    for (int i = 0; i < (int)sizeof(msg); i++) msg[i] = (uint8_t)i;
    int rc;

    /* Hashes over secret data. */
    SECRET(msg, sizeof(msg));
    rc = nxc_sha256(msg, sizeof(msg), out);
    PUBLIC(&rc, sizeof(rc));
    rc |= nxc_sha512(msg, sizeof(msg), out);
    PUBLIC(&rc, sizeof(rc));
    rc |= nxc_sha384(msg, sizeof(msg), out);
    PUBLIC(&rc, sizeof(rc));

    /* HMAC and HKDF with a secret key. */
    SECRET(key, sizeof(key));
    rc |= nxc_hmac(NXC_HASH_SHA256, key, sizeof(key), msg, sizeof(msg), out, sizeof(out));
    PUBLIC(&rc, sizeof(rc));
    uint8_t prk[48], okm[100];
    rc |= nxc_hkdf_extract(NXC_HASH_SHA384, NULL, 0, key, sizeof(key), prk, sizeof(prk));
    PUBLIC(&rc, sizeof(rc));
    rc |= nxc_hkdf_expand(NXC_HASH_SHA384, prk, sizeof(prk), "info", 4, okm, sizeof(okm));
    PUBLIC(&rc, sizeof(rc));

    /* X25519 with a secret scalar against a public point. */
    PUBLIC(key, sizeof(key));
    rc |= nxc_x25519_public(peer, key);         /* peer = public value */
    PUBLIC(&rc, sizeof(rc));
    PUBLIC(peer, sizeof(peer));
    SECRET(key, sizeof(key));
    rc |= nxc_x25519(out, key, peer);
    PUBLIC(&rc, sizeof(rc));

    /* Ed25519 key derivation and signing with a secret seed. */
    SECRET(seed, sizeof(seed));
    rc |= nxc_ed25519_public(pub, seed);
    PUBLIC(&rc, sizeof(rc));
    rc |= nxc_ed25519_sign(sig, msg, sizeof(msg), seed);
    PUBLIC(&rc, sizeof(rc));

    /* Constant-time comparison of a secret tag. */
    SECRET(out, 32);
    int eq = nxc_ct_equal(out, okm, 32);
    PUBLIC(&eq, sizeof(eq));

    PUBLIC(sig, sizeof(sig));
    PUBLIC(pub, sizeof(pub));
    PUBLIC(msg, sizeof(msg));
    int v = nxc_ed25519_verify(sig, msg, sizeof(msg), pub);

    printf("crypto_ct_test: rc=%d verify=%d (pass = 0 Valgrind errors)\n", rc, v);
    return (rc == 0 && v == 0) ? 0 : 1;
}
