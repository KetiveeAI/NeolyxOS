/*
 * nxcrypto - AES-128/256 encryption (FIPS 197), constant time.
 *
 * Table-driven AES leaks key bits through cache timing, because S-box
 * lookups are indexed by secret bytes. Here the S-box is computed:
 * SubBytes takes the multiplicative inverse in GF(2^8) (as x^254) and
 * applies the affine map, on eight bytes at once packed in a 64-bit word
 * ("lanes"). Every step is shifts, ANDs and XORs with fixed masks, so
 * timing and memory access never depend on data or key. It is slower
 * than tables; TLS prefers ChaCha20-Poly1305 for that reason.
 *
 * Only the encryption direction exists: GCM (and CTR) never decrypt
 * blocks.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "nxcrypto/aead.h"
#include "internal.h"

#define LANES_01 0x0101010101010101ULL
#define LANES_7F 0x7f7f7f7f7f7f7f7fULL

/* Multiply every byte lane by x (0x02) in GF(2^8), poly x^8+x^4+x^3+x+1. */
static inline uint64_t xtime8(uint64_t x) {
    uint64_t hi = (x >> 7) & LANES_01;
    return ((x & LANES_7F) << 1) ^ (hi * 0x1b);
}

/* Lane-wise a * b in GF(2^8). */
static uint64_t gmul8(uint64_t a, uint64_t b) {
    uint64_t r = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t m = ((b >> i) & LANES_01) * 0xff;   /* lane = 0x00 or 0xff */
        r ^= a & m;
        a = xtime8(a);
    }
    return r;
}

/* Lane-wise rotate left by n (1..7) within each byte. */
static inline uint64_t rotl8(uint64_t x, int n) {
    uint64_t hi_mask = (uint64_t)((0xffu << n) & 0xffu) * LANES_01;
    uint64_t lo_mask = (uint64_t)(0xffu >> (8 - n)) * LANES_01;
    return ((x << n) & hi_mask) | ((x >> (8 - n)) & lo_mask);
}

/* S-box on eight bytes: inverse (x^254, with 0 -> 0), then affine map. */
static uint64_t sub8(uint64_t x) {
    uint64_t x2 = gmul8(x, x);
    uint64_t x3 = gmul8(x2, x);
    uint64_t x6 = gmul8(x3, x3);
    uint64_t x12 = gmul8(x6, x6);
    uint64_t x15 = gmul8(x12, x3);
    uint64_t x30 = gmul8(x15, x15);
    uint64_t x60 = gmul8(x30, x30);
    uint64_t x120 = gmul8(x60, x60);
    uint64_t x240 = gmul8(x120, x120);
    uint64_t x252 = gmul8(x240, x12);
    uint64_t inv = gmul8(x252, x2);                  /* x^254 */
    return inv ^ rotl8(inv, 1) ^ rotl8(inv, 2) ^ rotl8(inv, 3) ^ rotl8(inv, 4) ^
           (0x63 * LANES_01);
}

static uint32_t sub_word(uint32_t w) {
    return (uint32_t)sub8((uint64_t)w);   /* upper lanes are 0 -> 0x63, dropped */
}

static inline uint8_t xtime(uint8_t b) {
    return (uint8_t)((b << 1) ^ (0x1b & (uint8_t)(0u - (uint32_t)(b >> 7))));
}

int nxc_aes_init(nxc_aes_ctx *ctx, const uint8_t *key, size_t key_len) {
    if (ctx == NULL || key == NULL) return NXC_ERR_ARG;
    if (key_len != 16 && key_len != 32) return NXC_ERR_ARG;
    unsigned nk = (unsigned)key_len / 4;
    ctx->rounds = nk + 6;
    unsigned total = 4 * (ctx->rounds + 1);

    for (unsigned i = 0; i < nk; i++) ctx->rk[i] = nxc_load32_be(key + 4 * i);
    uint8_t rcon = 1;
    for (unsigned i = nk; i < total; i++) {
        uint32_t t = ctx->rk[i - 1];
        if (i % nk == 0) {
            t = sub_word((t << 8) | (t >> 24)) ^ ((uint32_t)rcon << 24);
            rcon = xtime(rcon);
        } else if (nk > 6 && i % nk == 4) {
            t = sub_word(t);
        }
        ctx->rk[i] = ctx->rk[i - nk] ^ t;
    }
    ctx->magic = NXC_CTX_MAGIC;
    return NXC_OK;
}

void nxc_aes_wipe(nxc_aes_ctx *ctx) {
    if (ctx != NULL) nxc_memzero(ctx, sizeof(*ctx));
}

static void add_round_key(uint8_t s[16], const uint32_t *rk) {
    for (int c = 0; c < 4; c++) {
        s[4 * c + 0] ^= (uint8_t)(rk[c] >> 24);
        s[4 * c + 1] ^= (uint8_t)(rk[c] >> 16);
        s[4 * c + 2] ^= (uint8_t)(rk[c] >> 8);
        s[4 * c + 3] ^= (uint8_t)rk[c];
    }
}

static void sub_bytes(uint8_t s[16]) {
    uint64_t a = nxc_load64_le(s), b = nxc_load64_le(s + 8);
    nxc_store64_le(s, sub8(a));
    nxc_store64_le(s + 8, sub8(b));
}

/* Row r moves left by r columns; byte (r, c) is s[r + 4c]. */
static void shift_rows(uint8_t s[16]) {
    uint8_t t[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            t[r + 4 * c] = s[r + 4 * ((c + r) & 3)];
    nxc_copy(s, t, 16);
}

static void mix_columns(uint8_t s[16]) {
    for (int c = 0; c < 4; c++) {
        uint8_t *col = s + 4 * c;
        uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        uint8_t all = (uint8_t)(a0 ^ a1 ^ a2 ^ a3);
        /* b_i = a_i ^ all ^ 2(a_i ^ a_{i+1}) */
        col[0] = (uint8_t)(a0 ^ all ^ xtime((uint8_t)(a0 ^ a1)));
        col[1] = (uint8_t)(a1 ^ all ^ xtime((uint8_t)(a1 ^ a2)));
        col[2] = (uint8_t)(a2 ^ all ^ xtime((uint8_t)(a2 ^ a3)));
        col[3] = (uint8_t)(a3 ^ all ^ xtime((uint8_t)(a3 ^ a0)));
    }
}

int nxc_aes_encrypt_block(const nxc_aes_ctx *ctx, uint8_t out[16], const uint8_t in[16]) {
    if (ctx == NULL || out == NULL || in == NULL) return NXC_ERR_ARG;
    if (ctx->magic != NXC_CTX_MAGIC || (ctx->rounds != 10 && ctx->rounds != 14))
        return NXC_ERR_STATE;
    uint8_t s[16];
    nxc_copy(s, in, 16);
    add_round_key(s, ctx->rk);
    for (uint32_t round = 1; round < ctx->rounds; round++) {
        sub_bytes(s);
        shift_rows(s);
        mix_columns(s);
        add_round_key(s, ctx->rk + 4 * round);
    }
    sub_bytes(s);
    shift_rows(s);
    add_round_key(s, ctx->rk + 4 * ctx->rounds);
    nxc_copy(out, s, 16);
    nxc_memzero(s, sizeof(s));
    return NXC_OK;
}
