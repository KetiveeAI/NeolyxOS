#!/usr/bin/env bash
# Download the published test vectors used by the nxcrypto host tests.
#
# The vectors (NIST CAVP response files, RFC 4231/5869/7748/8032/8439
# examples, Project Wycheproof edge cases, x509-limbo path-validation
# cases, the RFC 8448 TLS 1.3 traces) are data, not code, and are not
# stored in the tree. The NIST and RFC sets come from the
# "cryptography_vectors" package on PyPI, which collects them in one
# archive; the others come from their GitHub repositories. All are
# pinned (by version or commit) and checked by SHA-256 so every run tests
# against identical data; a mismatch stops the script.
#
# Usage: tests/host/crypto_fetch_vectors.sh [dest_dir]
#        (default dest: build/crypto-vectors under the repo root)
# Prints the vector directory to pass to the tests.
#
# Copyright (c) 2025 KetiveeAI
# SPDX-License-Identifier: MIT

set -euo pipefail

VERSION="50.0.2"
URL="https://files.pythonhosted.org/packages/83/a4/a95842e91abd69710682f6e552bdb271c959ad3793b8d5d1e00ca6dc10d5/cryptography_vectors-${VERSION}-py3-none-any.whl"
SHA256="51641f03a3eb4edbe9fb68e3a3574d25f86aa502d06391fffa886330d02778a0"

# Project Wycheproof (https://github.com/C2SP/wycheproof): edge-case
# vectors for signatures, X25519 and the AEADs, read by
# crypto_wycheproof_test. Pinned to one commit; every file has a SHA-256.
WP_COMMIT="d17cc89fa5c8f4f4d378b5385ebf75da5d0926c9"
WP_URL="https://raw.githubusercontent.com/C2SP/wycheproof/$WP_COMMIT/testvectors_v1"
# x509-limbo (https://github.com/C2SP/x509-limbo): X.509 path-validation
# cases with expected results, read by crypto_x509_limbo_test.
LIMBO_COMMIT="1080e565f26e544bbe9cf99953be8a57c08e7e20"
LIMBO_URL="https://raw.githubusercontent.com/C2SP/x509-limbo/$LIMBO_COMMIT/limbo.json"
LIMBO_SHA256="611e337b9fb477b927bae65434650692cb9d2070ff5b05f975d0403510acd4de"

# The RFC 8448 TLS 1.3 example handshakes, read by
# crypto_tls13_trace_test. The IETF site is not reachable from every build
# machine, so the trace comes from two independent transcriptions of the
# RFC: Botan's machine-readable copy of every record (the test replays
# it) and Mbed TLS's test data, which quotes RFC 8448 secrets and records
# (the test checks the two agree, and checks its own key schedule output
# against the Mbed TLS copy). Pinned to one commit each, with SHA-256.
TRACE_COMMIT="6107761857d6d9b1eaeb971d076f12898e599ed0"
TRACE_URL="https://raw.githubusercontent.com/randombit/botan/$TRACE_COMMIT/src/tests/data/tls_13_rfc8448/transcripts.vec"
TRACE_SHA256="aff741f819c2742905af5e5221701074a1456720b2b542bdec62ae9697350fd5"
XCHECK_COMMIT="eff4330ae387a694b49848777a8a426daf74fdb2"
XCHECK_URL="https://raw.githubusercontent.com/Mbed-TLS/mbedtls/$XCHECK_COMMIT/tests/suites/test_suite_ssl.data"
XCHECK_SHA256="1c50ef73683f5cb5e9d975432e0982f52954518c698e71ab057ee48154c272a0"

WP_FILES="
985e5ecc172e181eaf49e89508b9470dcf478002eb7e8559c707eb42dc97dfe7 aes_gcm_test.json
fe61d25f90e1bde4461d00eafe61049e5f29bd999f36b766df9cda90906ad53d chacha20_poly1305_test.json
182db4f3e230f6f9fa9f800d2a614dede30284b8e8438bbfe1171905402e9332 ecdsa_secp256r1_sha256_test.json
8e940baad7c6172c58c8f354b10366d307be43a3cb82a5880cb46686c3e8f858 ecdsa_secp256r1_sha512_test.json
8a5b3ae1760975143414811f13588c24d951d9d8c904195087ba327591dfe9cc ecdsa_secp384r1_sha384_test.json
f44bae32e419fc75c5b5af751cff4e35145cfbcc24944e378d5f30889d310513 ecdsa_secp384r1_sha512_test.json
ea80ffebac085e21cb9d3e903e7fa2b448d96702da5b31f061d0b83b430b74b3 ed25519_test.json
7f6efafc160f4816b96cbf1c12188a31051d7e3f001e27505d9edb5f2a0e325c rsa_pss_2048_sha256_mgf1_32_test.json
cca48433e6d1accb57f65b3396c4f4dd93c776467101c3f0948e74c283fd4749 rsa_pss_3072_sha256_mgf1_32_test.json
c93ceaa56a190c9fd4707441c5c6a75839f108202d88aec891697f9623042547 rsa_pss_4096_sha512_mgf1_64_test.json
94a917b01ff50fb874cfc05bf29b4af44868d944a6558201cf18380da93fb393 rsa_signature_2048_sha256_test.json
73499ee649d8fb703b3f0f455fa36f7682fde677c590a736958ebce05ab27dae rsa_signature_3072_sha384_test.json
9d6e67b1eb0d7040db411835238efd20e9302399a258bdff82de3f38e0f2855a rsa_signature_4096_sha512_test.json
35c3f5231cf25cc640b524d403461deee9e49441d5d915a3a25b2c8ff5adbe7d x25519_test.json
"


ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="${1:-$ROOT/build/crypto-vectors}"
VECDIR="$DEST/cryptography_vectors"

# --- 1. NIST CAVP and RFC vectors (cryptography_vectors wheel) ---------
if [ ! -f "$DEST/.ok-$VERSION" ] || [ ! -d "$VECDIR" ]; then
    mkdir -p "$DEST"
    ARCHIVE="$DEST/vectors-$VERSION.zip"
    if [ ! -f "$ARCHIVE" ]; then
        echo "crypto_fetch_vectors: downloading vectors $VERSION" >&2
        curl -fsSL -o "$ARCHIVE.part" "$URL"
        mv "$ARCHIVE.part" "$ARCHIVE"
    fi

    GOT="$(sha256sum "$ARCHIVE" | cut -d' ' -f1)"
    if [ "$GOT" != "$SHA256" ]; then
        echo "crypto_fetch_vectors: checksum mismatch for $ARCHIVE" >&2
        echo "  expected $SHA256" >&2
        echo "  got      $GOT" >&2
        rm -f "$ARCHIVE"
        exit 1
    fi

    rm -rf "$VECDIR"
    # The archive is a zip; extract only the vector data, never run anything from it.
    python3 -I -c 'import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
for n in z.namelist():
    if n.startswith("cryptography_vectors/") and not n.endswith(".py"):
        z.extract(n, sys.argv[2])' "$ARCHIVE" "$DEST"
    touch "$DEST/.ok-$VERSION"
fi

# --- 2. Project Wycheproof edge cases (pinned commit, per-file SHA-256) -
# Kept inside VECDIR so a fresh extraction above also refreshes these.
WP_DIR="$VECDIR/wycheproof"
if [ ! -f "$WP_DIR/.ok-$WP_COMMIT" ]; then
    mkdir -p "$WP_DIR"
    echo "crypto_fetch_vectors: downloading Wycheproof ${WP_COMMIT:0:12}" >&2
    while read -r sum name; do
        [ -n "$name" ] || continue
        curl -fsSL -o "$WP_DIR/$name.part" "$WP_URL/$name"
        GOT="$(sha256sum "$WP_DIR/$name.part" | cut -d' ' -f1)"
        if [ "$GOT" != "$sum" ]; then
            echo "crypto_fetch_vectors: checksum mismatch for wycheproof/$name" >&2
            echo "  expected $sum" >&2
            echo "  got      $GOT" >&2
            rm -f "$WP_DIR/$name.part"
            exit 1
        fi
        mv "$WP_DIR/$name.part" "$WP_DIR/$name"
    done <<< "$WP_FILES"
    touch "$WP_DIR/.ok-$WP_COMMIT"
fi

# --- 3. x509-limbo (pinned commit, SHA-256) ---------------------------
LIMBO_DIR="$VECDIR/x509-limbo"
if [ ! -f "$LIMBO_DIR/.ok-$LIMBO_COMMIT" ]; then
    mkdir -p "$LIMBO_DIR"
    echo "crypto_fetch_vectors: downloading x509-limbo ${LIMBO_COMMIT:0:12}" >&2
    curl -fsSL -o "$LIMBO_DIR/limbo.json.part" "$LIMBO_URL"
    GOT="$(sha256sum "$LIMBO_DIR/limbo.json.part" | cut -d' ' -f1)"
    if [ "$GOT" != "$LIMBO_SHA256" ]; then
        echo "crypto_fetch_vectors: checksum mismatch for x509-limbo/limbo.json" >&2
        echo "  expected $LIMBO_SHA256" >&2
        echo "  got      $GOT" >&2
        rm -f "$LIMBO_DIR/limbo.json.part"
        exit 1
    fi
    mv "$LIMBO_DIR/limbo.json.part" "$LIMBO_DIR/limbo.json"
    touch "$LIMBO_DIR/.ok-$LIMBO_COMMIT"
fi

# --- 4. RFC 8448 TLS 1.3 traces (two pinned transcriptions) ----------
TRACE_DIR="$VECDIR/rfc8448"
if [ ! -f "$TRACE_DIR/.ok-$TRACE_COMMIT-$XCHECK_COMMIT" ]; then
    mkdir -p "$TRACE_DIR"
    echo "crypto_fetch_vectors: downloading RFC 8448 traces" >&2
    while read -r name url sum; do
        [ -n "$name" ] || continue
        curl -fsSL -o "$TRACE_DIR/$name.part" "$url"
        GOT="$(sha256sum "$TRACE_DIR/$name.part" | cut -d' ' -f1)"
        if [ "$GOT" != "$sum" ]; then
            echo "crypto_fetch_vectors: checksum mismatch for rfc8448/$name" >&2
            echo "  expected $sum" >&2
            echo "  got      $GOT" >&2
            rm -f "$TRACE_DIR/$name.part"
            exit 1
        fi
        mv "$TRACE_DIR/$name.part" "$TRACE_DIR/$name"
    done <<EOT
transcripts.vec $TRACE_URL $TRACE_SHA256
mbedtls_ssl.data $XCHECK_URL $XCHECK_SHA256
EOT
    touch "$TRACE_DIR/.ok-$TRACE_COMMIT-$XCHECK_COMMIT"
fi

echo "$VECDIR"
