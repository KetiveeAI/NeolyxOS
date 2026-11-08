#!/usr/bin/env python3
"""
Print the NIST P-256 and P-384 domain parameters (FIPS 186-4 appendix D.1.2,
SEC 2 section 2.4) as C byte arrays for lib/nxcrypto/src/ecdsa.c.

These are published constants; the script transcribes them from
`openssl ecparam -param_enc explicit -text` so nothing is typed by hand.
The host test checks them again at run time: G is on the curve, n*G is
the point at infinity, and the NIST CAVP SigVer vectors pass.

Usage: python3 gen_ec_params.py

Copyright (c) 2025 KetiveeAI
SPDX-License-Identifier: MIT
"""
import re
import subprocess


def params(name):
    txt = subprocess.run(["openssl", "ecparam", "-name", name, "-param_enc", "explicit",
                          "-text", "-noout"], capture_output=True, text=True, check=True).stdout
    out, key = {}, None
    for line in txt.splitlines():
        m = re.match(r"^(Prime|A|B|Generator \(uncompressed\)|Order|Cofactor|Seed):", line)
        if m:
            key = m.group(1)
            out[key] = ""
            continue
        if key and re.match(r"^\s+[0-9a-f:]+$", line):
            out[key] += line.strip().replace(":", "")
    return out


def carr(name, hexstr, size):
    v = int(hexstr, 16)
    b = v.to_bytes(size, "big")
    rows = [", ".join("0x%02x" % x for x in b[i:i + 12]) for i in range(0, size, 12)]
    print("static const uint8_t %s[%d] = {\n    %s\n};" % (name, size, ",\n    ".join(rows)))


for curve, tag, size in (("prime256v1", "P256", 32), ("secp384r1", "P384", 48)):
    p = params(curve)
    g = p["Generator (uncompressed)"]
    assert g.startswith("04")
    g = g[2:]
    assert int(p["A"], 16) == int(p["Prime"], 16) - 3
    carr(tag + "_P", p["Prime"], size)
    carr(tag + "_B", p["B"], size)
    carr(tag + "_N", p["Order"], size)
    carr(tag + "_GX", g[:2 * size], size)
    carr(tag + "_GY", g[2 * size:], size)
