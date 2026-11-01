# nxcrypto

The NeolyxOS cryptography library, written from scratch for NeolyxOS
(stream S6 of the public beta plan). It is the base for TLS 1.3 (`lib/nxtls`),
HTTPS (`lib/nxhttp`), signed packages (S7) and the kernel's random number
generator.

## Rules the code follows

- **Freestanding C11.** It uses only `<stddef.h>` and `<stdint.h>`, has no C
  library calls, never allocates, has no global mutable state, and uses
  general-purpose registers only. The same sources link into the kernel and
  into user programs.
- **Published vectors before use.** Every primitive passes published test
  vectors in the host tests before any other code calls it.
- **Constant time for secrets.** No branch and no memory index depends on a
  key, seed, scalar or plaintext. `make ct-test` checks this mechanically
  under Valgrind (see below).
- **Explicit errors.** Every fallible function returns an `nxc_status_t`
  (`NXC_OK` is 0, failures are negative). Contexts carry a magic value, so
  update-after-final and use-before-init return `NXC_ERR_STATE` and do not
  silently compute garbage.
- **Wipe secrets.** Keys and intermediates are cleared with `nxc_memzero()`,
  which the compiler cannot optimise away.
- **No logging inside the library.** The caller decides what to log and
  where (kernel serial, an app's log). Use `nxc_status_str()` for the text.

## What is in it

| Header | Provides | Verified against |
| --- | --- | --- |
| `nxcrypto/sha2.h` | SHA-256, SHA-384, SHA-512 (streaming and one-shot), generic `nxc_hash_*` | NIST CAVP SHAVS ShortMsg, LongMsg and Monte Carlo for all three |
| `nxcrypto/hmac.h` | HMAC-SHA-256/384/512, HKDF extract and expand | RFC 4231 (all three hashes), RFC 5869 |
| `nxcrypto/curve25519.h` | X25519, Ed25519 key, sign and verify | RFC 7748 section 5.2 (including 1000 iterations) and section 6.1, the 1024-entry Ed25519 reference set (includes RFC 8032 section 7.1) |

Coming next, in this order: ChaCha20-Poly1305 (RFC 8439), AES-GCM with
constant-time software AES, ECDSA P-256/P-384 and RSA verification, then
X.509.

Include `nxcrypto.h` to get everything.

## Building

```sh
make -C lib/nxcrypto            # build/libnxcrypto.a for NeolyxOS (freestanding)
make -C lib/nxcrypto test       # host tests against the published vectors
make -C lib/nxcrypto ct-test    # constant-time check (needs valgrind)
```

`make test` runs `tests/host/crypto_fetch_vectors.sh` the first time. It
downloads the vector set (NIST CAVP files and RFC examples, packaged on PyPI
as `cryptography_vectors`), pinned by version and SHA-256, into
`build/crypto-vectors/`. The vectors are data and are not stored in the tree.
A missing vector file fails the test; it never counts as a pass.

`tests/userland/cryptotest` runs a known-answer subset inside NeolyxOS
(install it as `/apps/cryptotest` and launch it from the app drawer). Expect
`[CRYPTOTEST] RESULT: PASS` on the serial log.

### Constant-time check

`tests/host/crypto_ct_test.c` marks every secret input as "undefined" for
Valgrind memcheck, then hashes, MACs, derives keys, runs X25519 and signs with
Ed25519. Memcheck reports any conditional jump or memory address computed
from undefined data, and that is exactly a secret-dependent timing leak. The
check passes with 0 errors. It was confirmed to catch a leak by temporarily
replacing the masked select in the Ed25519 ladder with an `if`.

## Constants

The SHA-2 round constants and initial values, and the curve25519 constants
(d, 2d, sqrt(-1), the base point, the group order L), are derived from
their mathematical definitions by `tools/gen_constants.py`, not typed in by
hand. The Ed25519 host test checks the curve constants again at run time,
against independent properties (d * 121666 = -121665, sqrt(-1)^2 = -1, B on
the curve, [L]B = identity).

## Interface for other streams

S7 (signed packages) uses:

```c
int nxc_sha256(const void *data, size_t len, uint8_t out[32]);
/* or nxc_sha256_init / _update / _final for streaming file hashes */
int nxc_ed25519_public(uint8_t pub[32], const uint8_t seed[32]);
int nxc_ed25519_sign(uint8_t sig[64], const void *msg, size_t len, const uint8_t seed[32]);
int nxc_ed25519_verify(const uint8_t sig[64], const void *msg, size_t len, const uint8_t pub[32]);
```

`nxc_ed25519_verify` returns `NXC_OK` only for a valid signature. Treat any
other value as "not signed". It rejects S >= L (malleable signatures) and
non-canonical or invalid public keys. The host pack-and-sign tools can
compile the same sources with the host compiler, as the host tests do.
