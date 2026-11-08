/*
 * nxtls - certificate path validation and host name check. See
 * nxtls/x509_verify.h for the policy.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "nxtls/x509_verify.h"
#include "der.h"
#include "names.h"

#define GN_EMAIL   DER_CTX(1)
#define GN_DNS     DER_CTX(2)
#define GN_DIRNAME DER_CTX_CONS(4)
#define GN_URI     DER_CTX(6)
#define GN_IP      DER_CTX(7)

/* ------------------------------------------------------------------ */
/* Names: DNS and IP                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    int ip_len;            /* 0 for a DNS name, else 4 or 16 */
    uint8_t ip[16];
    const uint8_t *dns;
    size_t dns_len;
} host_id;

static int parse_host(const char *host, host_id *h) {
    const uint8_t *s = (const uint8_t *)host;
    size_t n = nxt__strlen(host);
    h->ip_len = 0;
    h->dns = NULL;
    h->dns_len = 0;
    if (n == 0 || n > 253 + 1) return NXT_X509_ERR_BAD_HOST;
    for (size_t i = 0; i < n; i++)
        if (s[i] == ':') return nxt__parse_ipv6(s, n, h->ip) ? (h->ip_len = 16, NXT_X509_OK)
                                                         : NXT_X509_ERR_BAD_HOST;
    if (nxt__parse_ipv4(s, n, h->ip)) {
        h->ip_len = 4;
        return NXT_X509_OK;
    }
    if (s[n - 1] == '.') n--;                   /* absolute form "example.com." */
    if (!nxt__valid_dns(s, n, 0)) return NXT_X509_ERR_BAD_HOST;
    /* A name whose last label is all digits would be read as an address
     * by other software; refuse it rather than guess. */
    size_t k = n;
    int digits_only = 1;
    while (k > 0 && s[k - 1] != '.') {
        if (s[k - 1] < '0' || s[k - 1] > '9') digits_only = 0;
        k--;
    }
    if (digits_only) return NXT_X509_ERR_BAD_HOST;
    h->dns = s;
    h->dns_len = n;
    return NXT_X509_OK;
}

/* RFC 6125 6.4: exact match, or "*.rest" matching one label then rest. */
static int dns_matches(const uint8_t *pattern, size_t plen, const uint8_t *host, size_t hlen) {
    if (!nxt__valid_dns(pattern, plen, 1)) return 0;
    if (plen >= 2 && pattern[0] == '*') {
        size_t dot = 0;
        while (dot < hlen && host[dot] != '.') dot++;
        if (dot == 0 || dot == hlen) return 0;
        return hlen - dot == plen - 1 && nxt__ieq(pattern + 1, host + dot, plen - 1);
    }
    return plen == hlen && nxt__ieq(pattern, host, hlen);
}

/* Visit each GeneralName in a GeneralNames body; stops early when fn returns nonzero. */
typedef int (*gn_fn)(void *ctx, uint8_t tag, der_reader value);

static int each_name(nxt_slice names, gn_fn fn, void *ctx) {
    der_reader r;
    der_init(&r, names.p, names.len);
    while (!der_empty(&r)) {
        uint8_t tag;
        der_reader v;
        if (der_next(&r, &tag, &v, NULL) != 0) return -1;    /* checked at parse; defensive */
        int rc = fn(ctx, tag, v);
        if (rc != 0) return rc;
    }
    return 0;
}

static int host_cb(void *vctx, uint8_t tag, der_reader v) {
    const host_id *h = vctx;
    if (h->ip_len == 0 && tag == GN_DNS)
        return dns_matches(v.p, v.len, h->dns, h->dns_len);
    if (h->ip_len != 0 && tag == GN_IP && v.len == (size_t)h->ip_len) {
        for (int i = 0; i < h->ip_len; i++)
            if (v.p[i] != h->ip[i]) return 0;
        return 1;
    }
    return 0;
}

int nxt_x509_check_host(const nxt_x509_cert *cert, const char *host) {
    if (cert == NULL || host == NULL) return NXT_X509_ERR_ARG;
    host_id h;
    int rc = parse_host(host, &h);
    if (rc != NXT_X509_OK) return rc;
    if (!(cert->ext_present & NXT_EXT_SUBJECT_ALT_NAME)) return NXT_X509_ERR_HOST_MISMATCH;
    return each_name(cert->san, host_cb, &h) == 1 ? NXT_X509_OK : NXT_X509_ERR_HOST_MISMATCH;
}

/* ------------------------------------------------------------------ */
/* Name constraints (RFC 5280 4.2.1.10)                                */
/* ------------------------------------------------------------------ */

/*
 * Is DNS name `name` inside subtree `base`? "example.com" covers itself
 * and every name below it; ".example.com" covers only names below it;
 * an empty base covers everything.
 */
static int dns_in_subtree(const uint8_t *name, size_t nlen, const uint8_t *base, size_t blen) {
    if (blen == 0) return 1;
    if (base[0] == '.') {
        return nlen > blen && nxt__ieq(name + nlen - blen, base, blen);
    }
    if (nlen == blen) return nxt__ieq(name, base, nlen);
    return nlen > blen && name[nlen - blen - 1] == '.' && nxt__ieq(name + nlen - blen, base, blen);
}

/*
 * Could a wildcard name "*.rest" match some host inside `base`? True if
 * rest itself is inside base (every match is), or base is one label
 * followed by rest (that one host matches).
 */
static int wildcard_meets_subtree(const uint8_t *rest, size_t rlen, const uint8_t *base, size_t blen) {
    if (blen == 0) return 1;
    const uint8_t *b = base[0] == '.' ? base + 1 : base;
    size_t bl = base[0] == '.' ? blen - 1 : blen;
    if (dns_in_subtree(rest, rlen, base, blen) || (base[0] == '.' && bl == rlen && nxt__ieq(b, rest, rlen)))
        return 1;
    size_t dot = 0;
    while (dot < bl && b[dot] != '.') dot++;
    return dot < bl && bl - dot - 1 == rlen && nxt__ieq(b + dot + 1, rest, rlen);
}

static int ip_in_subtree(der_reader ip, const uint8_t *base, size_t blen) {
    if (ip.len * 2 != blen) return 0;
    const uint8_t *mask = base + ip.len;
    for (size_t i = 0; i < ip.len; i++)
        if ((ip.p[i] & mask[i]) != (base[i] & mask[i])) return 0;
    return 1;
}

typedef struct {
    nxt_slice subtrees;    /* GeneralSubtrees body */
    int excluded;
    int status;            /* NXT_X509_OK or NXT_X509_ERR_NAME_CONSTRAINTS */
} nc_ctx;

/* Does one name (tag, value) violate the subtrees in ctx? */
static int nc_name_cb(void *vctx, uint8_t tag, der_reader name) {
    nc_ctx *c = vctx;
    der_reader trees;
    der_init(&trees, c->subtrees.p, c->subtrees.len);
    int have_type = 0, matched = 0;
    while (!der_empty(&trees)) {
        der_reader st;
        uint8_t btag;
        der_reader base;
        if (der_expect(&trees, DER_SEQUENCE, &st, NULL) != 0 || der_next(&st, &btag, &base, NULL) != 0) {
            c->status = NXT_X509_ERR_NAME_CONSTRAINTS;
            return 1;
        }
        if (btag != tag) continue;
        have_type = 1;
        int hit;
        if (tag == GN_DNS) {
            if (name.len >= 2 && name.p[0] == '*' && name.p[1] == '.')
                hit = c->excluded ? wildcard_meets_subtree(name.p + 2, name.len - 2, base.p, base.len)
                                  : dns_in_subtree(name.p, name.len, base.p, base.len);
            else
                hit = dns_in_subtree(name.p, name.len, base.p, base.len);
        } else if (tag == GN_IP) {
            hit = ip_in_subtree(name, base.p, base.len);
        } else {
            /* A constraint of a type this code cannot evaluate applies to
             * this name: fail rather than ignore it. */
            c->status = NXT_X509_ERR_NAME_CONSTRAINTS;
            return 1;
        }
        if (hit) matched = 1;
    }
    if (c->excluded ? matched : (have_type && !matched)) {
        c->status = NXT_X509_ERR_NAME_CONSTRAINTS;
        return 1;
    }
    return 0;
}

/* 1 if the subtrees contain an entry of the given GeneralName tag. */
static int subtrees_have(nxt_slice subtrees, uint8_t tag) {
    der_reader trees;
    der_init(&trees, subtrees.p, subtrees.len);
    while (!der_empty(&trees)) {
        der_reader st;
        uint8_t btag;
        if (der_expect(&trees, DER_SEQUENCE, &st, NULL) != 0 || der_next(&st, &btag, NULL, NULL) != 0)
            return 1;
        if (btag == tag) return 1;
    }
    return 0;
}

static size_t count_entries(nxt_slice body) {
    der_reader r;
    size_t n = 0;
    der_init(&r, body.p, body.len);
    while (!der_empty(&r) && der_next(&r, NULL, NULL, NULL) == 0) n++;
    return n;
}

/* Name-constraint comparisons allowed per certificate pair: real CAs
 * stay far below this; the cap stops quadratic blow-up on hostile input. */
#define NC_MAX_WORK 65536u

/* Check every name of `cert` against the constraints of `ca`. */
static int check_name_constraints(const nxt_x509_cert *ca, const nxt_x509_cert *cert) {
    if (!(ca->ext_present & NXT_EXT_NAME_CONSTRAINTS)) return NXT_X509_OK;
    size_t names = count_entries(cert->san);
    size_t trees = count_entries(ca->nc_permitted) + count_entries(ca->nc_excluded);
    if (names > NC_MAX_WORK || trees > NC_MAX_WORK || names * trees > NC_MAX_WORK)
        return NXT_X509_ERR_BUDGET;
    /* directoryName constraints apply to every subject DN; not supported. */
    if (subtrees_have(ca->nc_permitted, GN_DIRNAME) || subtrees_have(ca->nc_excluded, GN_DIRNAME))
        return NXT_X509_ERR_NAME_CONSTRAINTS;
    if (!(cert->ext_present & NXT_EXT_SUBJECT_ALT_NAME)) return NXT_X509_OK;
    for (int pass = 0; pass < 2; pass++) {
        nc_ctx c = { pass ? ca->nc_excluded : ca->nc_permitted, pass, NXT_X509_OK };
        if (c.subtrees.p == NULL) continue;
        if (each_name(cert->san, nc_name_cb, &c) < 0) return NXT_X509_ERR_NAME_CONSTRAINTS;
        if (c.status != NXT_X509_OK) return c.status;
    }
    return NXT_X509_OK;
}

/* ------------------------------------------------------------------ */
/* Per-certificate checks                                              */
/* ------------------------------------------------------------------ */

static int self_issued(const nxt_x509_cert *c) { return nxt_x509_name_equal(c->issuer, c->subject); }

/* RFC 5280 4.1.2.2: serial numbers are positive. */
static int serial_is_zero(const nxt_x509_cert *c) {
    return c->serial.len == 1 && c->serial.p[0] == 0;
}

static int check_time(const nxt_x509_cert *c, int64_t now) {
    if (now < c->not_before) return NXT_X509_ERR_NOT_YET_VALID;
    if (now > c->not_after) return NXT_X509_ERR_EXPIRED;
    return NXT_X509_OK;
}

static int check_leaf(const nxt_x509_cert *c, const nxt_verify_opts *o) {
    int rc = check_time(c, o->now);
    if (rc != NXT_X509_OK) return rc;
    if (c->is_ca) return NXT_X509_ERR_LEAF_IS_CA;
    if (c->ext_present & NXT_EXT_KEY_USAGE) {
        if (!(c->key_usage & NXT_KU_DIGITAL_SIGNATURE)) return NXT_X509_ERR_KEY_USAGE;
        if (c->key_usage & NXT_KU_KEY_CERT_SIGN) return NXT_X509_ERR_KEY_USAGE;  /* only for CAs */
    }
    /* BR 7.1.2.7.10: extKeyUsage present, not critical, serverAuth, never anyExtendedKeyUsage. */
    if (!(c->ext_present & NXT_EXT_EXT_KEY_USAGE) || (c->ext_critical & NXT_EXT_EXT_KEY_USAGE) ||
        !(c->ext_key_usage & NXT_EKU_SERVER_AUTH) || (c->ext_key_usage & NXT_EKU_ANY))
        return NXT_X509_ERR_EKU;
    if (c->ext_present & NXT_EXT_NAME_CONSTRAINTS) return NXT_X509_ERR_PROFILE;   /* CAs only */
    if (c->authority_key_id.p == NULL) return NXT_X509_ERR_PROFILE;              /* 4.2.1.1 */
    if (serial_is_zero(c)) return NXT_X509_ERR_PROFILE;
    /* BR 7.1.2.7.12: subjectAltName is critical only when the subject is empty. */
    if ((c->ext_critical & NXT_EXT_SUBJECT_ALT_NAME) && c->subject.len > 2)
        return NXT_X509_ERR_PROFILE;
    /* The subject commonName is never used as a host name, so it is not
     * checked against the subjectAltName (BR 7.1.4.3 asks CAs for that). */
    return nxt_x509_check_host(c, o->host);
}

/* Rules every CA certificate on the path (intermediate or root) follows. */
static int check_ca_common(const nxt_x509_cert *c, int64_t now) {
    int rc = check_time(c, now);
    if (rc != NXT_X509_OK) return rc;
    if (!c->is_ca) return NXT_X509_ERR_NOT_CA;
    /* RFC 5280 4.2.1.9 and 4.2.1.2: basicConstraints critical, SKI present. */
    if (!(c->ext_critical & NXT_EXT_BASIC_CONSTRAINTS) || c->subject_key_id.p == NULL)
        return NXT_X509_ERR_PROFILE;
    if ((c->ext_present & NXT_EXT_KEY_USAGE) && !(c->key_usage & NXT_KU_KEY_CERT_SIGN))
        return NXT_X509_ERR_KEY_USAGE;
    return NXT_X509_OK;
}

/*
 * A root is used for its name, key and name constraints, but it must
 * still be a well-formed, unexpired CA certificate: no extKeyUsage
 * (Mozilla root store policy), and an authorityKeyIdentifier, if any,
 * holds only a keyIdentifier equal to its own SKI (BR 7.1.2.1.3).
 */
static int check_anchor(const nxt_x509_cert *c, int64_t now) {
    int rc = check_ca_common(c, now);
    if (rc != NXT_X509_OK) return rc;
    if (c->ext_present & NXT_EXT_EXT_KEY_USAGE) return NXT_X509_ERR_EKU;
    /* A self-signed root's AKI, if any, names its own key. (For a
     * cross-signed CA used as an anchor, the AKI names its other issuer,
     * which plays no part here.) */
    if (self_issued(c) && (c->ext_present & NXT_EXT_AUTHORITY_KEY_ID)) {
        if ((c->ext_present & NXT_EXT_AKI_EXTRA) || c->authority_key_id.p == NULL ||
            !nxt_x509_name_equal(c->authority_key_id, c->subject_key_id))
            return NXT_X509_ERR_PROFILE;
    }
    return NXT_X509_OK;
}

static int check_intermediate(const nxt_x509_cert *c, int64_t now) {
    int rc = check_ca_common(c, now);
    if (rc != NXT_X509_OK) return rc;
    if (c->authority_key_id.p == NULL || serial_is_zero(c)) return NXT_X509_ERR_PROFILE;
    if ((c->ext_present & NXT_EXT_EXT_KEY_USAGE) &&
        !(c->ext_key_usage & (NXT_EKU_SERVER_AUTH | NXT_EKU_ANY)))
        return NXT_X509_ERR_EKU;
    return NXT_X509_OK;
}


/* ------------------------------------------------------------------ */
/* Path building                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    const nxt_x509_cert *chain;
    size_t chain_len;
    const nxt_trust_store *roots;
    const nxt_verify_opts *opts;
    unsigned max_depth;
    unsigned sig_budget;
    size_t path[NXT_X509_MAX_DEPTH];   /* chain[] indexes, path[0] = 0 (leaf) */
    int best_err;
    size_t best_depth;
    int best_index;
    size_t anchor_index;
    size_t path_length;
} builder;

static void note(builder *b, int err, size_t depth, int index) {
    if (b->best_err == NXT_X509_OK || depth > b->best_depth) {
        b->best_err = err;
        b->best_depth = depth;
        b->best_index = index;
    }
}

/* Checks that need the whole path: pathLenConstraint and name constraints. */
static int check_path(builder *b, size_t depth, const nxt_x509_cert *anchor, int *bad_index) {
    /* path[1..depth] are intermediates, top one at depth. */
    for (size_t k = 1; k <= depth; k++) {
        const nxt_x509_cert *ca = &b->chain[b->path[k]];
        if (ca->path_len >= 0) {
            size_t below = 0;
            for (size_t j = 1; j < k; j++)
                if (!self_issued(&b->chain[b->path[j]])) below++;
            if (below > (size_t)ca->path_len) {
                *bad_index = (int)b->path[k];
                return NXT_X509_ERR_PATH_LEN;
            }
        }
    }
    /* Constraints of each CA (and the anchor) apply to every certificate
     * below it; self-issued intermediates are exempt (RFC 5280 6.1.3 (b)). */
    for (size_t k = 1; k <= depth + 1; k++) {
        const nxt_x509_cert *ca = k <= depth ? &b->chain[b->path[k]] : anchor;
        for (size_t j = 0; j < k; j++) {
            const nxt_x509_cert *c = &b->chain[b->path[j]];
            if (j > 0 && self_issued(c)) continue;
            int rc = check_name_constraints(ca, c);
            if (rc != NXT_X509_OK) {
                *bad_index = (int)b->path[j];
                return rc;
            }
        }
    }
    return NXT_X509_OK;
}

static int same_key_and_name(const nxt_x509_cert *a, const nxt_x509_cert *c) {
    return nxt_x509_name_equal(a->subject, c->subject) && nxt_x509_name_equal(a->spki, c->spki);
}

/* Find an issuer for path[depth]; 1 on success, 0 on failure, -1 if out of budget. */
static int extend(builder *b, size_t depth) {
    const nxt_x509_cert *child = &b->chain[b->path[depth]];
    int found_candidate = 0;

    for (size_t a = 0; a < b->roots->count; a++) {
        const nxt_x509_cert *anchor = &b->roots->anchors[a];
        if (!nxt_x509_name_equal(anchor->subject, child->issuer)) continue;
        found_candidate = 1;
        int arc = check_anchor(anchor, b->opts->now);
        if (arc != NXT_X509_OK) {
            note(b, arc, depth + 1, -1);
            continue;
        }
        if (b->sig_budget == 0) return -1;
        b->sig_budget--;
        int rc = nxt_x509_check_signature(child, anchor);
        if (rc != NXT_X509_OK) {
            note(b, rc, depth, (int)b->path[depth]);
            continue;
        }
        int bad = -1;
        rc = check_path(b, depth, anchor, &bad);
        if (rc == NXT_X509_OK) {
            b->anchor_index = a;
            b->path_length = depth + 1;
            return 1;
        }
        note(b, rc, depth, bad);
    }

    for (size_t i = 1; i < b->chain_len; i++) {
        const nxt_x509_cert *cand = &b->chain[i];
        if (!nxt_x509_name_equal(cand->subject, child->issuer)) continue;
        /* No certificate (or same name and key) twice on one path. */
        int loop = 0;
        for (size_t j = 0; j <= depth; j++)
            if (b->path[j] == i || same_key_and_name(&b->chain[b->path[j]], cand)) loop = 1;
        if (loop) continue;
        found_candidate = 1;
        if (depth + 1 >= b->max_depth) {
            note(b, NXT_X509_ERR_CHAIN_TOO_LONG, depth + 1, (int)i);
            continue;
        }
        int rc = check_intermediate(cand, b->opts->now);
        if (rc != NXT_X509_OK) {
            note(b, rc, depth + 1, (int)i);
            continue;
        }
        if (b->sig_budget == 0) return -1;
        b->sig_budget--;
        rc = nxt_x509_check_signature(child, cand);
        if (rc != NXT_X509_OK) {
            note(b, rc, depth, (int)b->path[depth]);
            continue;
        }
        b->path[depth + 1] = i;
        int r = extend(b, depth + 1);
        if (r != 0) return r;
    }

    if (!found_candidate) note(b, NXT_X509_ERR_UNKNOWN_ISSUER, depth, (int)b->path[depth]);
    return 0;
}

int nxt_x509_verify_server(const nxt_x509_cert *chain, size_t chain_len,
                           const nxt_trust_store *roots, const nxt_verify_opts *opts,
                           nxt_verify_result *result) {
    nxt_verify_result local;
    nxt_verify_result *res = result ? result : &local;
    res->err = NXT_X509_ERR_ARG;
    res->failed_index = -1;
    res->path_length = 0;
    res->anchor_index = 0;
    if (chain == NULL || chain_len == 0 || chain_len > NXT_X509_MAX_CHAIN || roots == NULL ||
        (roots->anchors == NULL && roots->count != 0) || opts == NULL || opts->host == NULL)
        return NXT_X509_ERR_ARG;
    for (size_t i = 0; i < chain_len; i++)
        if (chain[i].raw.p == NULL) return NXT_X509_ERR_ARG;   /* not parsed */

    int rc = check_leaf(&chain[0], opts);
    if (rc != NXT_X509_OK) {
        res->err = rc;
        res->failed_index = 0;
        return rc;
    }

    builder b;
    b.chain = chain;
    b.chain_len = chain_len;
    b.roots = roots;
    b.opts = opts;
    b.max_depth = opts->max_depth == 0 || opts->max_depth > NXT_X509_MAX_DEPTH
                      ? NXT_X509_MAX_DEPTH : opts->max_depth;
    b.sig_budget = NXT_X509_MAX_SIG_CHECKS;
    b.path[0] = 0;
    b.best_err = NXT_X509_OK;
    b.best_depth = 0;
    b.best_index = -1;
    b.anchor_index = 0;
    b.path_length = 0;

    int r = extend(&b, 0);
    if (r == 1) {
        res->err = NXT_X509_OK;
        res->path_length = b.path_length;
        res->anchor_index = b.anchor_index;
        return NXT_X509_OK;
    }
    res->err = r < 0 ? NXT_X509_ERR_BUDGET
                     : (b.best_err != NXT_X509_OK ? b.best_err : NXT_X509_ERR_UNKNOWN_ISSUER);
    res->failed_index = r < 0 ? -1 : b.best_index;
    return res->err;
}
