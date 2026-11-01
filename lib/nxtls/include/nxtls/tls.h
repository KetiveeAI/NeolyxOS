/*
 * nxtls - TLS 1.3 (RFC 8446) client: result codes and cipher suites.
 *
 * This header holds the parts of the TLS client that are fixed now: the
 * result codes every TLS function returns and the cipher suites the
 * client supports. The connection API (handshake, read, write, close)
 * is added on top of this by the handshake step.
 *
 * Only TLS 1.3 is supported. The cipher suites are the three that every
 * TLS 1.3 server offers; there is no TLS 1.2 fallback, so a server that
 * cannot speak TLS 1.3 is refused instead of being talked to with a
 * weaker protocol.
 *
 * Freestanding C11, like lib/nxcrypto: no C library, no allocation, no
 * logging. Callers log the returned code with nxt_tls_err_str().
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXTLS_TLS_H
#define NXTLS_TLS_H

#include <stddef.h>
#include <stdint.h>

/* Result codes. 0 is success, failures are negative and each names one
 * reason, so a log line can say exactly why a connection failed. */
typedef enum {
    NXT_TLS_OK                     =   0,
    NXT_TLS_ERR_ARG                =  -1,  /* NULL pointer, bad length or wrong state */
    NXT_TLS_ERR_BUFFER             =  -2,  /* output buffer too small */
    NXT_TLS_ERR_RECORD_OVERFLOW    =  -3,  /* record longer than RFC 8446 5.2 allows */
    NXT_TLS_ERR_BAD_RECORD_MAC     =  -4,  /* record failed authentication (tampered or wrong key) */
    NXT_TLS_ERR_SEQ_EXHAUSTED      =  -5,  /* 2^64 - 1 records sent or received with one key */
    NXT_TLS_ERR_UNEXPECTED_MESSAGE =  -6,  /* message or record not allowed here */
    NXT_TLS_ERR_CRYPTO             =  -7   /* a crypto primitive failed (should not happen) */
} nxt_tls_err_t;

/* Short, constant text for a result code (for the caller's log line). */
const char *nxt_tls_err_str(int err);

/* TLS 1.3 cipher suites (RFC 8446 appendix B.4) supported by nxtls. */
#define NXT_TLS_AES_128_GCM_SHA256       0x1301u
#define NXT_TLS_AES_256_GCM_SHA384       0x1302u
#define NXT_TLS_CHACHA20_POLY1305_SHA256 0x1303u

/* Record sizes (RFC 8446 section 5). */
#define NXT_TLS_MAX_PLAINTEXT   16384u                          /* 2^14 */
#define NXT_TLS_MAX_CIPHERTEXT  (NXT_TLS_MAX_PLAINTEXT + 256u)  /* 2^14 + 256 */
#define NXT_TLS_RECORD_HEADER   5u
#define NXT_TLS_MAX_RECORD      (NXT_TLS_RECORD_HEADER + NXT_TLS_MAX_CIPHERTEXT)

/* Record content types (RFC 8446 section 5.1). */
#define NXT_TLS_CT_CHANGE_CIPHER_SPEC 20u
#define NXT_TLS_CT_ALERT              21u
#define NXT_TLS_CT_HANDSHAKE          22u
#define NXT_TLS_CT_APPLICATION_DATA   23u

#endif /* NXTLS_TLS_H */
