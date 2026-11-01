# nxtls

The NeolyxOS TLS 1.3 client library, written from scratch for NeolyxOS
(stream S6 of the public beta plan). It is built on `lib/nxcrypto` and will
carry HTTPS for `lib/nxhttp` and the `fetch` command.

It contains the X.509 certificate code, the TLS 1.3 key schedule and
record protection, and a blocking TLS 1.3 client (`nxt_tls_connect`,
`nxt_tls_read`, `nxt_tls_write`, `nxt_tls_close`).

## Rules the code follows

The same rules as `lib/nxcrypto`:

- **Freestanding C11.** No C library, no allocation, no global mutable
  state, general-purpose registers only.
- **Published test data before use.** The host tests run published suites
  and must pass before other code relies on this library.
- **Explicit errors.** Every failure has its own `nxt_x509_err_t` code, and
  `nxt_x509_err_str()` gives text for the caller's log line, so an
  expired, self-signed or wrong-host certificate is logged with its reason.
- **Fail closed.** Anything the code does not understand that could change
  a certificate's meaning is refused, never skipped. Examples are an
  unknown critical extension, a name constraint of a type it cannot check,
  or an unsupported algorithm.

## X.509

| Header | Provides |
| --- | --- |
| `nxtls/x509.h` | `nxt_x509_parse()` (strict DER, zero-copy), `nxt_x509_check_signature()`, `nxt_x509_name_equal()` |
| `nxtls/x509_verify.h` | `nxt_x509_verify_server()` (path building and validation for a TLS server), `nxt_x509_check_host()` |

**Supported:** RSA keys of 2048 to 4096 bits, ECDSA P-256 and P-384, and
Ed25519. Signatures can be RSA PKCS#1 v1.5 or RSA-PSS with SHA-256/384/512,
ECDSA with SHA-256/384/512, or Ed25519. SHA-1, MD5, DSA and RSA keys under
2048 bits are refused.

### Validation policy (web PKI)

`x509_verify.h` describes the rules in full. In summary:

**Server certificate**
- Valid at the given time, and not a CA.
- Has extKeyUsage with serverAuth, and no anyExtendedKeyUsage.
- If it has keyUsage, it includes digitalSignature.
- Has an authorityKeyIdentifier.
- Names the host in subjectAltName. The common name is never used as a
  host name.

**Intermediates**
- Valid at the given time.
- CA certificates with a critical basicConstraints, and with SKI and AKI.
- If they have keyUsage, it includes keyCertSign.
- If they have extKeyUsage, it includes serverAuth (or any).
- pathLenConstraint is enforced.

**Roots**
- Contribute their name, key and name constraints.
- Must still be unexpired, well-formed CA certificates without
  extKeyUsage.

**Name constraints**
- Enforced for DNS names and IP addresses, from roots and intermediates.
- A constraint of any other type that applies fails the path.

**Host names**
- Matching is case-insensitive.
- A wildcard covers exactly one leftmost label, and `*.com` is refused.
- IPv4 and IPv6 hosts match iPAddress entries.

**Path building**
- Tries every candidate issuer, so cross-signed chains and chains sent out
  of order work.
- Work is bounded:
  - 8 certificates per path
  - 64 certificates from the server
  - 64 signature checks
  - a cap on name-constraint comparisons

**Not covered:** revocation (CRL, OCSP) and certificate policies. A
critical policy extension makes a certificate fail to load.

### Tests

| Test | What it runs | Result |
| --- | --- | --- |
| `tests/host/crypto_x509_test.c` | Parses every certificate in the `cryptography_vectors` x509 set and in NIST PKITS, with pinned results and field values for selected files. Checks host names. Checks every PKITS certificate's signature against its issuer. Builds a self-signed server certificate and checks that it is refused. | 895 checks pass |
| `tests/host/crypto_x509_limbo_test.c` | [x509-limbo](https://github.com/C2SP/x509-limbo): 9,774 path-validation cases (RFC 5280, CA/Browser Forum, BetterTLS name constraints and path building, pathological chains, CVEs) | 914 valid accepted and 8,840 invalid refused; 20 documented differences (below) |
| `cryptotest` in QEMU | A real server chain (fastly.com, from x509-limbo) verified inside NeolyxOS, plus expired, wrong-host, untrusted-root and missing-intermediate refusals | `RESULT: PASS`; chain verify took 6 to 51 ms across runs under TCG |

The limbo test fails on any invalid chain that is accepted, unless the
case is in `KNOWN_LENIENT`. It also fails on any valid chain that is
refused, unless the case is in `KNOWN_STRICTER`. Each entry gives its
reason, and the test checks that every entry still applies.

- **Stricter (8 valid chains refused):** a CA certificate used as a server
  certificate, a missing extKeyUsage, ML-DSA, a root with non-critical name
  constraints, and a depth limit that counts self-issued certificates.
- **Lenient (12 invalid chains accepted):** the common name is not compared
  with the subjectAltName (it is never used as a host name), wildcards on
  public suffixes (this needs the Public Suffix List), and one case that
  conflicts with another case in the suite.

Seven deliberately injected bugs (no name constraints, no expiry check, no
pathLen, no intermediate signature check, a wildcard spanning labels, no
CA check, CA as leaf) each make the limbo test fail. Both tests are clean
under ASan and UBSan.

## TLS 1.3 key schedule and records

| File | Provides |
| --- | --- |
| `nxtls/tls.h` | Result codes (`nxt_tls_err_t`, `nxt_tls_err_str()`), cipher suite ids, record size limits |
| `src/tls13.h` (internal) | HKDF-Expand-Label, the key schedule for a full handshake (RFC 8446 7.1), the Finished MAC (4.4.4), KeyUpdate (7.2), and record protection (5.2, 5.3) |

**Cipher suites:**
- TLS_AES_128_GCM_SHA256
- TLS_AES_256_GCM_SHA384
- TLS_CHACHA20_POLY1305_SHA256

Only TLS 1.3 is supported, with no fallback to TLS 1.2. There is no
pre-shared key or session resumption, so every connection does a full
handshake.

**Record rules:**
- Plaintext is at most 2^14 bytes and ciphertext at most 2^14 + 256 bytes.
  Longer records are refused.
- A record of padding only, with no content type, is refused.
- A record that fails authentication is wiped before the error returns.
- Sequence numbers never wrap. At 2^64 - 1 the record layer refuses to
  continue.

### Tests

| Test | What it runs | Result |
| --- | --- | --- |
| `tests/host/crypto_tls13_trace_test.c` | Replays the RFC 8448 example handshakes from the client's side (section 3 simple 1-RTT, section 7 middlebox compatibility mode) and checks the server's flight in section 6 (client authentication). The server's encrypted handshake must decrypt to the recorded messages. The server Finished must verify. The client Finished, application data and close_notify records built here must equal the recorded ones byte for byte. Derived secrets, keys and IVs are also looked up in a second, independent transcription of RFC 8448. Also covers every record-layer failure path for all three suites: each single-bit change, replay, wrong sequence number, length limits, padding-only records, the nonce at large sequence numbers, sequence exhaustion, KeyUpdate and wiping. | 282 checks pass |
| `cryptotest` in QEMU | Replays RFC 8448 section 3 inside NeolyxOS, the same way, plus a tampered record | `RESULT: PASS`; key schedule with two records took 32 ms under TCG |

The IETF site is not reachable from every build machine, so the RFC 8448
trace comes from two transcriptions. Botan's machine-readable copy of
every record is the one the test replays. Mbed TLS's test data quotes
RFC 8448 secrets and records, and the test checks that the two agree.
Both are pinned by commit and SHA-256 in `crypto_fetch_vectors.sh`.

Nine deliberately injected bugs were tried, such as a wrong label, a wrong
"derived" context, a wrong nonce, a wrong record length, no padding
removal, a missing sequence increment and no wipe on failure. Each one
makes the test fail. The test is clean under ASan and UBSan.

## TLS 1.3 client

`nxtls/tls.h` declares the client. The caller supplies the connection
memory (`nxt_tls_conn`, 96 KiB, static or heap), the transport as two
callbacks (send and recv on a connected socket), a random source (the
`random.get` syscall on NeolyxOS), the host name, the trust store and the
current time. The library does no I/O, allocation or logging of its own.

```c
static nxt_tls_conn conn;
nxt_tls_config cfg = { "example.org", &roots, now, { &sock, tcp_send, tcp_recv },
                       os_random, NULL, "http/1.1" };
int rc = nxt_tls_connect(&conn, &cfg);
if (rc != NXT_TLS_OK) {
    const nxt_tls_info *in = nxt_tls_get_info(&conn);
    log("tls: %s (certificate: %s, alert sent: %s)", nxt_tls_err_str(rc),
        nxt_x509_err_str(in->cert_error), nxt_tls_alert_str(in->alert_sent));
}
```

**What it does:**
- TLS 1.3 only, X25519 only. Suites in order of preference:
  ChaCha20-Poly1305 (fast without AES instructions), then AES-128-GCM,
  then AES-256-GCM.
- CertificateVerify signatures accepted: ECDSA P-256 and P-384, Ed25519,
  RSA-PSS. PKCS#1 v1.5 is accepted only inside certificates, as RFC 8446
  requires.
- The chain is checked with `nxt_x509_verify_server` (the web PKI policy
  above). An expired, self-signed, untrusted or wrong-host certificate
  ends the handshake with `NXT_TLS_ERR_CERTIFICATE` and the matching alert
  (`certificate_expired`, `unknown_ca`, `certificate_unknown`,
  `bad_certificate`); `nxt_tls_get_info` gives the X.509 reason and the
  index of the refused certificate.
- SNI for DNS names (not for IP literals), optional ALPN, the
  HelloRetryRequest cookie, middlebox compatibility mode, KeyUpdate both
  ways, and record_size_limit (RFC 8449).
- A CertificateRequest is answered with an empty Certificate. Session
  tickets are read and dropped; there is no resumption or 0-RTT.
- Every protocol violation ends the handshake with a fatal alert and its
  own result code, and every secret is wiped on failure and on close.
- `nxt_tls_read` returns 0 only after the server's close_notify. A
  connection that ends without it returns `NXT_TLS_ERR_CLOSED`, so a cut
  short download is never mistaken for a complete one.

Timeouts belong to the transport: a recv callback that gives up returns a
negative value and the call fails with `NXT_TLS_ERR_IO`.

### Tests

| Test | What it runs | Result |
| --- | --- | --- |
| `tests/host/crypto_tls13_client_test.c` | Part 1 runs the RFC 8448 handshakes (simple, middlebox compatibility, client authentication) through the real `nxt_tls_connect` with a mock transport, whole records and one byte per read; every record the client sends must equal the trace. It also checks that the trace's RSA-1024 certificate is refused as too weak. Part 2 runs a scripted TLS 1.3 server built from the key schedule above and a test PKI of Ed25519 certificates (`crypto_certgen.h`): good handshakes with all three suites, ALPN, a 40 KB write, HRR with cookie, CertificateRequest, KeyUpdate both ways, close_notify, truncation, record_size_limit, 12 certificate refusals, 43 handshake violations and 8 after the handshake, each with the exact result code and alert on the wire. | 698 checks pass |
| `cryptotest` in QEMU | `nxt_tls_connect` inside NeolyxOS against canned server answers: the ClientHello it sends (SNI, ALPN), refusal of a TLS 1.2 server and of a ServerHello that does not echo the session id, with the right alert | `RESULT: PASS` |

The test hooks the RFC 8448 replay needs (fixed ClientHello and key) are
compiled only with `-DNXT_TESTING`; `make test` fails if they ever end up
in `libnxtls.a`. A full handshake inside NeolyxOS is tested by the `fetch`
step against a test server on the host.

22 injected bugs were tried in `tls_client.c`, such as skipping the
Finished or signature check, ignoring the chain result, accepting a second
HelloRetryRequest, ignoring the server's record size limit or accepting
application data in the middle of a handshake message. 21 make the test
fail; the 22nd was in code that could never run, which was removed. The
test is clean under ASan and UBSan.

## Building

```sh
make -C lib/nxtls             # build/libnxtls.a (link with lib/nxcrypto/build/libnxcrypto.a)
make -C lib/nxtls test        # host tests (downloads the pinned vectors once)
```

The vectors are data and are not stored in the tree. They are the
`cryptography_vectors` package (which includes PKITS), x509-limbo
`limbo.json`, and the two RFC 8448 transcriptions. `tests/host/crypto_fetch_vectors.sh` downloads them, pinned
by version or commit and checked by SHA-256.

## Root certificates

The trust store is passed in by the caller as parsed certificates. Which
root list NeolyxOS ships (for example the Mozilla/CCADB list) is owner
decision D5. Loading that file comes with the `fetch` step.
