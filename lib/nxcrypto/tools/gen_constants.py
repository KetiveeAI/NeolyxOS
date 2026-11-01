#!/usr/bin/env python3
"""
Derive the numeric constants used by lib/nxcrypto from their definitions,
so no constant table is typed in by hand.

  SHA-2 (FIPS 180-4 section 4.2 and 5.3): round constants are the first
  32/64 bits of the fractional parts of the cube roots of the first 64/80
  primes; initial hash values are the first 32/64 bits of the fractional
  parts of the square roots of the first 8 primes (SHA-256, SHA-512) or of
  the 9th to 16th primes (SHA-384).

  Curve25519 / edwards25519 (RFC 7748 section 4.1, RFC 8032 section 5.1):
  p = 2^255 - 19, d = -121665/121666, sqrt(-1) = 2^((p-1)/4), base point
  y = 4/5 with even x, group order L.

Usage: python3 gen_constants.py > /tmp/constants.txt
The output is pasted into the sources; the host tests check the values
again at run time against independent properties (d*121666 == -121665,
sqrtm1^2 == -1, B on the curve).

Copyright (c) 2025 KetiveeAI
SPDX-License-Identifier: MIT
"""


def primes(n):
    out, c = [], 2
    while len(out) < n:
        if all(c % q for q in out if q * q <= c):
            out.append(c)
        c += 1
    return out


def iroot(x, k):
    """Floor of the k-th root of a non-negative integer x."""
    lo, hi = 0, 1
    while hi ** k <= x:
        hi *= 2
    while lo < hi - 1:
        mid = (lo + hi) // 2
        if mid ** k <= x:
            lo = mid
        else:
            hi = mid
    return lo


def frac_root_bits(p, k, bits):
    # floor(frac(p^(1/k)) * 2^bits) = floor(root(p * 2^(bits*k))) mod 2^bits
    return iroot(p << (bits * k), k) & ((1 << bits) - 1)


def table(name, vals, width):
    fmt = "0x%0" + str(width // 4) + "x" + ("ULL" if width == 64 else "u")
    print("%s = {" % name)
    per = 4 if width == 64 else 8
    for i in range(0, len(vals), per):
        print("    " + ", ".join(fmt % v for v in vals[i:i + per]) + ",")
    print("};\n")


def le_bytes(x):
    return ", ".join("0x%02x" % b for b in x.to_bytes(32, "little"))


def limbs51(x):
    m = (1 << 51) - 1
    return ", ".join("0x%013xULL" % ((x >> (51 * i)) & m) for i in range(5))


def main():
    ps = primes(80)
    table("sha256 K[64]", [frac_root_bits(p, 3, 32) for p in ps[:64]], 32)
    table("sha256 H0[8]", [frac_root_bits(p, 2, 32) for p in ps[:8]], 32)
    table("sha512 K[80]", [frac_root_bits(p, 3, 64) for p in ps[:80]], 64)
    table("sha512 H0[8]", [frac_root_bits(p, 2, 64) for p in ps[:8]], 64)
    table("sha384 H0[8]", [frac_root_bits(p, 2, 64) for p in ps[8:16]], 64)

    p = 2 ** 255 - 19
    inv = lambda a: pow(a, p - 2, p)
    d = (-121665 * inv(121666)) % p
    sqrtm1 = pow(2, (p - 1) // 4, p)
    assert sqrtm1 * sqrtm1 % p == p - 1
    by = 4 * inv(5) % p
    # x^2 = (y^2 - 1) / (d y^2 + 1)
    xx = (by * by - 1) * inv(d * by * by + 1) % p
    bx = pow(xx, (p + 3) // 8, p)
    if (bx * bx - xx) % p != 0:
        bx = bx * sqrtm1 % p
    assert (bx * bx - xx) % p == 0
    if bx & 1:
        bx = p - bx
    L = 2 ** 252 + 27742317777372353535851937790883648493

    print("p       =", hex(p))
    print("L       =", hex(L))
    print("L bytes =", le_bytes(L))
    print("d limbs      =", limbs51(d))
    print("2d limbs     =", limbs51(2 * d % p))
    print("sqrtm1 limbs =", limbs51(sqrtm1))
    print("Bx limbs     =", limbs51(bx))
    print("By limbs     =", limbs51(by))
    print("Bt limbs     =", limbs51(bx * by % p))
    print("B encoded    =", le_bytes(by | ((bx & 1) << 255)))


if __name__ == "__main__":
    main()
