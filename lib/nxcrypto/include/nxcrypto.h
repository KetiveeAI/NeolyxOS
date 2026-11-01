/*
 * nxcrypto - the NeolyxOS cryptography library (umbrella header)
 *
 * Written from scratch for NeolyxOS. Freestanding C11: no allocation, no
 * C library, no global mutable state, so the same objects link into the
 * kernel and into user programs. Every primitive is checked against
 * published test vectors by the host tests in tests/host/crypto_*_test.c
 * before anything uses it. Code that handles secrets runs in time that
 * does not depend on the secret.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXCRYPTO_H
#define NXCRYPTO_H

#include "nxcrypto/common.h"
#include "nxcrypto/sha2.h"
#include "nxcrypto/hmac.h"
#include "nxcrypto/curve25519.h"

#endif /* NXCRYPTO_H */
