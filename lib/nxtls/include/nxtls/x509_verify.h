/*
 * nxtls - certificate path validation and host name check for TLS
 * servers (RFC 5280 section 6, RFC 6125 / RFC 9525).
 *
 * Policy (the "web PKI" profile, close to what browsers enforce):
 *  - The server's certificate must be valid now and must not be a CA. If
 *    it has keyUsage, that must include digitalSignature and not
 *    keyCertSign. It must have a non-critical extKeyUsage that includes
 *    serverAuth and not anyExtendedKeyUsage, an authorityKeyIdentifier, a
 *    non-zero serial number and no name constraints. It must name the
 *    host in subjectAltName (dNSName or iPAddress). The subject common
 *    name is never used as a host name.
 *  - Intermediates must be valid now and be CAs with a critical
 *    basicConstraints, a subjectKeyIdentifier, an authorityKeyIdentifier
 *    and a non-zero serial number. If they have keyUsage it must include
 *    keyCertSign; if they have extKeyUsage it must include serverAuth (or
 *    any). pathLenConstraint is enforced.
 *  - A trust anchor contributes its subject, its key and its name
 *    constraints. It must also be valid now and be a well-formed CA
 *    certificate (as for intermediates, without the serial and AKI rules)
 *    with no extKeyUsage.
 *  - Name constraints on dNSName and iPAddress, from anchors and
 *    intermediates, are enforced. A constraint of any other type that
 *    would apply to a certificate on the path makes the path fail (it is
 *    never ignored).
 *  - Revocation (CRL, OCSP) and certificate policies are not checked.
 *
 * Path building tries every candidate issuer (anchors first, then the
 * intermediates the server sent, in any order), so cross-signed chains
 * work. The work is bounded: at most NXT_X509_MAX_SIG_CHECKS signature
 * checks per call, at most NXT_X509_MAX_DEPTH certificates on a path.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXTLS_X509_VERIFY_H
#define NXTLS_X509_VERIFY_H

#include "nxtls/x509.h"

#define NXT_X509_MAX_DEPTH      8u    /* certificates on a path, not counting the anchor */
#define NXT_X509_MAX_CHAIN      64u   /* certificates the server may send */
#define NXT_X509_MAX_SIG_CHECKS 64u   /* signature checks per verification */

/* Trusted root certificates (parsed by the caller, e.g. from the root CA file). */
typedef struct {
    const nxt_x509_cert *anchors;
    size_t count;
} nxt_trust_store;

typedef struct {
    int64_t now;          /* current time, seconds since 1970 UTC */
    const char *host;     /* DNS name or IP address literal (no brackets); required */
    unsigned max_depth;   /* 0 means NXT_X509_MAX_DEPTH; larger values are capped */
} nxt_verify_opts;

typedef struct {
    int err;              /* the return value */
    int failed_index;     /* chain[] index of the certificate that failed, or -1 */
    size_t path_length;   /* certificates on the accepted path, leaf included */
    size_t anchor_index;  /* trust store index of the anchor that was used */
} nxt_verify_result;

/*
 * Verify a server certificate chain: chain[0] is the server's
 * certificate, chain[1..] the intermediates it sent (any order, unused or
 * duplicate ones are allowed). Returns NXT_X509_OK if a valid path to an
 * anchor exists and chain[0] is valid for opts->host; otherwise the most
 * specific reason found. result may be NULL.
 */
int nxt_x509_verify_server(const nxt_x509_cert *chain, size_t chain_len,
                           const nxt_trust_store *roots, const nxt_verify_opts *opts,
                           nxt_verify_result *result);

/*
 * Host name check alone: NXT_X509_OK if the certificate's subjectAltName
 * covers host, NXT_X509_ERR_HOST_MISMATCH if not, NXT_X509_ERR_BAD_HOST
 * if host is not a valid DNS name or IP address.
 */
int nxt_x509_check_host(const nxt_x509_cert *cert, const char *host);

#endif /* NXTLS_X509_VERIFY_H */
