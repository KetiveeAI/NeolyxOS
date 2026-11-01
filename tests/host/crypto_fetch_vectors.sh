#!/usr/bin/env bash
# Download the published test vectors used by the nxcrypto host tests.
#
# The vectors (NIST CAVP response files, RFC 4231/5869/7748/8032/8439
# examples) are data, not code, and are not stored in the tree. They come
# from the "cryptography_vectors" package on PyPI, which collects them in
# one archive. The archive is pinned by version and SHA-256 so every run
# tests against identical data; a mismatch stops the script.
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

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="${1:-$ROOT/build/crypto-vectors}"
VECDIR="$DEST/cryptography_vectors"

if [ -f "$DEST/.ok-$VERSION" ] && [ -d "$VECDIR" ]; then
    echo "$VECDIR"
    exit 0
fi

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
echo "$VECDIR"
