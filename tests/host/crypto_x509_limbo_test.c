/*
 * Host test: nxtls path validation against x509-limbo
 * (https://github.com/C2SP/x509-limbo), a published suite of about 9,800
 * X.509 path-validation cases with expected results: RFC 5280 rules, web
 * PKI (CA/Browser Forum) rules, name constraints (the BetterTLS set),
 * path length, pathological chains and known CVEs.
 *
 * Scoring:
 *   - An expected FAILURE that nxtls accepts is a test failure, always.
 *   - An expected SUCCESS that nxtls refuses is a test failure unless the
 *     case is in KNOWN_STRICTER below with the reason nxtls refuses it
 *     (a deliberate policy that is stricter than the suite). Every such
 *     entry must still be refused, so the list cannot go stale silently.
 *   - Cases needing features nxtls does not implement and that only make
 *     sense with them (client validation, CRLs) are skipped and counted.
 *
 * Vectors: tests/host/crypto_fetch_vectors.sh downloads limbo.json pinned
 * to one commit with its SHA-256. Run: make -C lib/nxtls test
 * ("list" as argv[2] prints every case with nxtls' result.)
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "crypto_vec.h"
#include "crypto_json.h"
#include "crypto_pem.h"
#include "nxtls/x509_verify.h"

#include <time.h>

/*
 * Expected-SUCCESS cases nxtls refuses on purpose, with the reason.
 */
static const struct {
    const char *id;
    int err;              /* the refusal nxtls must give */
    const char *why;
} KNOWN_STRICTER[] = {
    { "pathlen::max-chain-depth-1-self-issued", NXT_X509_ERR_CHAIN_TOO_LONG,
      "the depth limit counts every certificate, self-issued ones included" },
    { "pathlen::validation-ignores-pathlen-in-leaf", NXT_X509_ERR_LEAF_IS_CA,
      "a TLS server certificate must not be a CA (BR 7.1.2.7.8)" },
    { "rfc5280::ca-as-leaf", NXT_X509_ERR_LEAF_IS_CA,
      "a TLS server certificate must not be a CA (BR 7.1.2.7.8)" },
    { "rfc5280::eku::ee-without-eku", NXT_X509_ERR_EKU,
      "web PKI requires extKeyUsage serverAuth (BR 7.1.2.7.10); conflicts with webpki::eku::ee-without-eku" },
    { "rfc9881::ml-dsa-44", NXT_X509_ERR_SIG_ALG, "ML-DSA is not supported yet" },
    { "rfc9881::ml-dsa-65", NXT_X509_ERR_SIG_ALG, "ML-DSA is not supported yet" },
    { "rfc9881::ml-dsa-87", NXT_X509_ERR_SIG_ALG, "ML-DSA is not supported yet" },
    { "webpki::nc::permitted-dns-match-noncritical", NXT_X509_ERR_UNKNOWN_ISSUER,
      "the root's non-critical name constraints break RFC 5280 4.2.1.10, so it does not load; "
      "conflicts with rfc5280::nc::permitted-dns-match-noncritical" },
};

/*
 * Expected-FAILURE cases nxtls accepts, with the reason. None may be a
 * signature, trust, date, host name or constraint failure.
 */
static const struct {
    const char *id;
    const char *why;
} KNOWN_LENIENT[] = {
#define CN_WHY "subject CN is never used as a host name, so a CN that differs from the SAN is not checked " \
               "(BR 7.1.4.3 binds the CA); the BetterTLS cases expect such certificates to be accepted"
    { "webpki::cn::case-mismatch", CN_WHY },
    { "webpki::cn::ipv4-hex-mismatch", CN_WHY },
    { "webpki::cn::ipv4-leading-zeros-mismatch", CN_WHY },
    { "webpki::cn::ipv6-non-rfc5952-mismatch", CN_WHY },
    { "webpki::cn::ipv6-uncompressed-mismatch", CN_WHY },
    { "webpki::cn::ipv6-uppercase-mismatch", CN_WHY },
    { "webpki::cn::not-in-san", CN_WHY },
    { "webpki::cn::punycode-not-in-san", CN_WHY },
    { "webpki::cn::utf8-vs-punycode-mismatch", CN_WHY },
    { "webpki::san::public-suffix-multi-label-wildcard-san",
      "needs the Public Suffix List, which NeolyxOS does not ship" },
    { "webpki::san::public-suffix-private-namespace-wildcard-san",
      "needs the Public Suffix List, which NeolyxOS does not ship" },
    { "rfc5280::aki::cross-signed-root-missing-aki",
      "a trust anchor's own issuer plays no part in validation; conflicts with cve::cve-2024-0567, "
      "whose trusted cross-signed root also has no AKI" },
};

static int g_list;

static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? (unsigned)-3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

/* "YYYY-MM-DDTHH:MM:SS[.fff]+00:00"; fractions are dropped (X.509 times have whole seconds). */
static int64_t parse_iso(const char *s) {
    int y, mo, d, h, mi, se;
    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se) != 6 ||
        (!strstr(s, "+00:00") && !strchr(s, 'Z'))) {
        printf("ERROR: unexpected time format %s\n", s);
        exit(2);
    }
    return days_from_civil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + se;
}

typedef struct {
    uint8_t *der[NXT_X509_MAX_CHAIN + 8];
    nxt_x509_cert cert[NXT_X509_MAX_CHAIN + 8];
    size_t n;
} cert_set;

/* Parse PEM strings into certs; ones that do not parse are left out
 * (a root store loader or TLS client skips them the same way). Returns
 * the parse result of the first certificate. */
static int load_set(cert_set *s, const json *arr, size_t limit) {
    int first_rc = NXT_X509_ERR_ARG;
    s->n = 0;
    for (size_t i = 0; arr && i < arr->len; i++) {
        const char *pem = arr->items[i]->str;
        size_t len;
        const char *pos = pem;
        uint8_t *der = pem ? pem_next(&pos, "CERTIFICATE", &len) : NULL;
        if (!der) continue;
        if (s->n == limit) { free(der); continue; }
        int rc = nxt_x509_parse(&s->cert[s->n], der, len);
        if (i == 0) first_rc = rc;
        if (rc != NXT_X509_OK) { free(der); continue; }
        s->der[s->n++] = der;
    }
    return first_rc;
}

static void free_set(cert_set *s) {
    for (size_t i = 0; i < s->n; i++) free(s->der[i]);
    s->n = 0;
}

static int has_feature(const json *t, const char *f) {
    const json *fs = json_get(t, "features");
    for (size_t i = 0; fs && i < fs->len; i++)
        if (fs->items[i]->str && !strcmp(fs->items[i]->str, f)) return 1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: %s <vector dir> [list]\n", argv[0]);
        return 2;
    }
    g_list = argc > 2 && !strcmp(argv[2], "list");
    char *text = vec_read_file(argv[1], "x509-limbo/limbo.json");
    json *doc = json_parse(text, "limbo.json");
    const json *cases = json_get(doc, "testcases");
    if (!cases || cases->type != JSON_ARR) {
        printf("ERROR: limbo.json has no testcases\n");
        return 2;
    }
    printf("nxtls x509-limbo path validation\n");

    static cert_set anchors, chain;
    int run = 0, skip_client = 0, skip_crl = 0, skip_noname = 0;
    int ok_success = 0, ok_failure = 0, stricter = 0, unexpected_refusals = 0;
    int stricter_seen[sizeof(KNOWN_STRICTER) / sizeof(KNOWN_STRICTER[0])] = { 0 };
    int lenient_seen[sizeof(KNOWN_LENIENT) / sizeof(KNOWN_LENIENT[0])] = { 0 };
    int lenient = 0;
    int64_t now_wall = (int64_t)time(NULL);

    for (size_t ci = 0; ci < cases->len; ci++) {
        const json *t = cases->items[ci];
        const char *id = json_str(t, "id");
        const char *kind = json_str(t, "validation_kind");
        const char *expected = json_str(t, "expected_result");
        if (!id || !kind || !expected) {
            printf("ERROR: malformed test case %zu\n", ci);
            return 2;
        }
        if (strcmp(kind, "SERVER")) { skip_client++; continue; }
        if (has_feature(t, "has-crl")) { skip_crl++; continue; }
        const json *peer = json_get(t, "expected_peer_name");
        const char *host = peer ? json_str(peer, "value") : NULL;
        if (!host) { skip_noname++; continue; }
        run++;

        const char *vt = json_str(t, "validation_time");
        nxt_verify_opts opts = { vt ? parse_iso(vt) : now_wall, host, 0 };
        long depth = json_long(t, "max_chain_depth", -1);
        if (depth >= 0) opts.max_depth = depth + 1 > 255 ? 255 : (unsigned)depth + 1;

        load_set(&anchors, json_get(t, "trusted_certs"), NXT_X509_MAX_CHAIN + 8);
        /* chain = peer certificate followed by the intermediates */
        int rc;
        const char *pem_pos = json_str(t, "peer_certificate");
        size_t leaf_len = 0;
        uint8_t *leaf_der = pem_pos ? pem_next(&pem_pos, "CERTIFICATE", &leaf_len) : NULL;
        chain.n = 0;
        if (!leaf_der) {
            rc = NXT_X509_ERR_ENCODING;
        } else {
            rc = nxt_x509_parse(&chain.cert[0], leaf_der, leaf_len);
            chain.der[0] = leaf_der;
            chain.n = 1;
        }
        if (rc == NXT_X509_OK) {
            cert_set inter;
            const json *ia = json_get(t, "untrusted_intermediates");
            if (ia && ia->len + 1 > NXT_X509_MAX_CHAIN) {
                rc = NXT_X509_ERR_CHAIN_TOO_LONG;        /* what the TLS client does */
            } else {
                load_set(&inter, ia, NXT_X509_MAX_CHAIN - 1);
                for (size_t i = 0; i < inter.n; i++) {
                    chain.cert[chain.n] = inter.cert[i];
                    chain.der[chain.n++] = inter.der[i];
                }
                nxt_trust_store ts = { anchors.cert, anchors.n };
                rc = nxt_x509_verify_server(chain.cert, chain.n, &ts, &opts, NULL);
            }
        }

        int want_ok = !strcmp(expected, "SUCCESS");
        if (g_list) printf("    %-80s want %-7s got %3d %s\n", id, expected, rc, nxt_x509_err_str(rc));
        if (!want_ok) {
            if (rc != NXT_X509_OK) {
                ok_failure++;
                CHECK(1, "counted");
            } else {
                int known = 0;
                for (size_t k = 0; k < sizeof(KNOWN_LENIENT) / sizeof(KNOWN_LENIENT[0]); k++)
                    if (!strcmp(KNOWN_LENIENT[k].id, id)) known = 1, lenient_seen[k] = 1;
                lenient += known;
                CHECK(known, "limbo %s: expected FAILURE, nxtls accepted", id);
            }
        } else if (rc == NXT_X509_OK) {
            ok_success++;
            CHECK(1, "counted");
        } else {
            int known = 0;
            for (size_t k = 0; k < sizeof(KNOWN_STRICTER) / sizeof(KNOWN_STRICTER[0]); k++) {
                if (strcmp(KNOWN_STRICTER[k].id, id)) continue;
                known = 1;
                stricter_seen[k] = 1;
                CHECK(rc == KNOWN_STRICTER[k].err, "limbo %s: refused with %d (%s), list says %d",
                      id, rc, nxt_x509_err_str(rc), KNOWN_STRICTER[k].err);
            }
            if (known) {
                stricter++;
            } else {
                unexpected_refusals++;
                CHECK(0, "limbo %s: expected SUCCESS, nxtls refused: %s", id, nxt_x509_err_str(rc));
            }
        }
        free_set(&anchors);
        free_set(&chain);
    }
    for (size_t k = 0; k < sizeof(KNOWN_STRICTER) / sizeof(KNOWN_STRICTER[0]); k++)
        CHECK(stricter_seen[k], "KNOWN_STRICTER entry %s was not refused (remove it)", KNOWN_STRICTER[k].id);
    for (size_t k = 0; k < sizeof(KNOWN_LENIENT) / sizeof(KNOWN_LENIENT[0]); k++)
        CHECK(lenient_seen[k], "KNOWN_LENIENT entry %s was not accepted (remove it)", KNOWN_LENIENT[k].id);

    CHECK(run > 9000, "only %d limbo cases ran", run);
    printf("  %d cases run: %d valid accepted, %d invalid refused\n", run, ok_success, ok_failure);
    printf("  documented differences: %d valid refused (stricter policy), %d invalid accepted (CN / PSL / anchor AKI)\n",
           stricter, lenient);
    printf("  skipped: %d client-certificate cases, %d CRL cases, %d without a server name\n",
           skip_client, skip_crl, skip_noname);
    if (unexpected_refusals) printf("  %d valid cases refused unexpectedly\n", unexpected_refusals);
    json_free(doc);
    free(text);
    return vec_report("crypto_x509_limbo_test");
}
