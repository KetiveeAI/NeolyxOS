/*
 * NeolyxOS Crypto Implementation
 * 
 * Cryptographic primitives.
 * NOTE: This contains simplified/reference implementations.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "security/crypto.h"

/* Helper macros for SHA256 */
#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))
#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

static const uint32_t k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

/* SHA256 Transform */
static void sha256_transform(sha256_ctx_t *ctx, const uint8_t *data) {
    uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];

    for (i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);
    for (; i < 64; ++i)
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e, f, g) + k[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void sha256_init(sha256_ctx_t *ctx) {
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count[0] = 0;
    ctx->count[1] = 0;
}

void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len) {
    uint32_t i, j;

    j = (ctx->count[0] >> 3) & 63;
    if ((ctx->count[0] += len << 3) < (len << 3)) ctx->count[1]++;
    ctx->count[1] += (len >> 29);
    if ((j + len) > 63) {
        for (i = 0; i < 64 - j; i++) ctx->buffer[j + i] = data[i];
        sha256_transform(ctx, ctx->buffer);
        for (; i + 63 < len; i += 64) sha256_transform(ctx, &data[i]);
        j = 0;
    }
    else i = 0;
    for (; i < len; i++) ctx->buffer[j + i - 0] = data[i];
}

void sha256_final(sha256_ctx_t *ctx, uint8_t hash[32]) {
    uint32_t i, j, lp;
    uint8_t finalcount[8];

    for (i = 0; i < 8; i++) {
        finalcount[i] = (uint8_t)((ctx->count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255);
    }
    j = (ctx->count[0] >> 3) & 63;
    ctx->buffer[j++] = 0x80;
    if (j > 56) {
        /* Pad to correct length */
        while (j < 64) ctx->buffer[j++] = 0;
        sha256_transform(ctx, ctx->buffer);
        j = 0;
    }
    while (j < 56) ctx->buffer[j++] = 0;
    for (i = 0; i < 8; i++) ctx->buffer[j + i] = finalcount[i];
    sha256_transform(ctx, ctx->buffer);
    
    for (i = 0; i < 32; i++) {
        hash[i] = (uint8_t)((ctx->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
    }
}

/* ============ AES-256 Implementation ============ */
/* Rijndael AES-256 (14 rounds, 32-byte key, 16-byte block) */

/* Rijndael S-box (forward) */
static const uint8_t aes_sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

/* Rijndael inverse S-box (for decryption) */
static const uint8_t aes_rsbox[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

/* Round constants for key expansion */
static const uint8_t aes_rcon[11] = {
    0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

/* GF(2^8) multiplication */
static uint8_t aes_xtime(uint8_t x) {
    return (x << 1) ^ ((x >> 7) * 0x1b);
}

static uint8_t aes_mul(uint8_t a, uint8_t b) {
    uint8_t result = 0;
    while (b) {
        if (b & 1) result ^= a;
        a = aes_xtime(a);
        b >>= 1;
    }
    return result;
}

/* AES-256 Key Expansion (Rijndael schedule) */
void aes256_init(aes256_ctx_t *ctx, const uint8_t *key) {
    uint32_t i;
    uint32_t *rk = ctx->rk;
    
    /* Copy the 8 key words directly */
    for (i = 0; i < 8; i++) {
        rk[i] = ((uint32_t)key[4*i] << 24) |
                ((uint32_t)key[4*i+1] << 16) |
                ((uint32_t)key[4*i+2] << 8) |
                ((uint32_t)key[4*i+3]);
    }
    
    /* Generate round keys (AES-256 = 14 rounds = 60 words total) */
    for (i = 8; i < 60; i++) {
        uint32_t temp = rk[i - 1];
        
        if (i % 8 == 0) {
            /* RotWord + SubWord + Rcon */
            temp = ((uint32_t)aes_sbox[(temp >> 16) & 0xff] << 24) |
                   ((uint32_t)aes_sbox[(temp >> 8) & 0xff] << 16) |
                   ((uint32_t)aes_sbox[temp & 0xff] << 8) |
                   ((uint32_t)aes_sbox[(temp >> 24) & 0xff]);
            temp ^= ((uint32_t)aes_rcon[i / 8] << 24);
        } else if (i % 8 == 4) {
            /* SubWord only for AES-256 */
            temp = ((uint32_t)aes_sbox[(temp >> 24) & 0xff] << 24) |
                   ((uint32_t)aes_sbox[(temp >> 16) & 0xff] << 16) |
                   ((uint32_t)aes_sbox[(temp >> 8) & 0xff] << 8) |
                   ((uint32_t)aes_sbox[temp & 0xff]);
        }
        
        rk[i] = rk[i - 8] ^ temp;
    }
}

/* AES-256 Encrypt Block (16 bytes) */
void aes256_encrypt(aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out) {
    uint8_t state[16];
    uint32_t *rk = ctx->rk;
    int round;
    
    /* Copy input to state and add initial round key */
    for (int i = 0; i < 16; i++) {
        state[i] = in[i] ^ ((rk[i / 4] >> (24 - 8 * (i % 4))) & 0xff);
    }
    
    /* 13 main rounds */
    for (round = 1; round < 14; round++) {
        uint8_t tmp[16];
        
        /* SubBytes */
        for (int i = 0; i < 16; i++) {
            tmp[i] = aes_sbox[state[i]];
        }
        
        /* ShiftRows */
        state[0] = tmp[0];  state[4] = tmp[4];  state[8] = tmp[8];   state[12] = tmp[12];
        state[1] = tmp[5];  state[5] = tmp[9];  state[9] = tmp[13];  state[13] = tmp[1];
        state[2] = tmp[10]; state[6] = tmp[14]; state[10] = tmp[2];  state[14] = tmp[6];
        state[3] = tmp[15]; state[7] = tmp[3];  state[11] = tmp[7];  state[15] = tmp[11];
        
        /* MixColumns */
        for (int c = 0; c < 4; c++) {
            uint8_t a = state[c*4], b = state[c*4+1], c_val = state[c*4+2], d = state[c*4+3];
            state[c*4]   = aes_mul(a, 2) ^ aes_mul(b, 3) ^ c_val ^ d;
            state[c*4+1] = a ^ aes_mul(b, 2) ^ aes_mul(c_val, 3) ^ d;
            state[c*4+2] = a ^ b ^ aes_mul(c_val, 2) ^ aes_mul(d, 3);
            state[c*4+3] = aes_mul(a, 3) ^ b ^ c_val ^ aes_mul(d, 2);
        }
        
        /* AddRoundKey */
        for (int i = 0; i < 16; i++) {
            state[i] ^= ((rk[round * 4 + i / 4] >> (24 - 8 * (i % 4))) & 0xff);
        }
    }
    
    /* Final round (no MixColumns) */
    uint8_t tmp[16];
    for (int i = 0; i < 16; i++) tmp[i] = aes_sbox[state[i]];
    
    state[0] = tmp[0];  state[4] = tmp[4];  state[8] = tmp[8];   state[12] = tmp[12];
    state[1] = tmp[5];  state[5] = tmp[9];  state[9] = tmp[13];  state[13] = tmp[1];
    state[2] = tmp[10]; state[6] = tmp[14]; state[10] = tmp[2];  state[14] = tmp[6];
    state[3] = tmp[15]; state[7] = tmp[3];  state[11] = tmp[7];  state[15] = tmp[11];
    
    for (int i = 0; i < 16; i++) {
        out[i] = state[i] ^ ((rk[56 + i / 4] >> (24 - 8 * (i % 4))) & 0xff);
    }
}

/* AES-256 Decrypt Block (16 bytes) */
void aes256_decrypt(aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out) {
    uint8_t state[16];
    uint32_t *rk = ctx->rk;
    int round;
    
    /* Copy input to state and add final round key */
    for (int i = 0; i < 16; i++) {
        state[i] = in[i] ^ ((rk[56 + i / 4] >> (24 - 8 * (i % 4))) & 0xff);
    }
    
    /* 13 inverse main rounds */
    for (round = 13; round > 0; round--) {
        uint8_t tmp[16];
        
        /* InvShiftRows */
        tmp[0] = state[0];  tmp[4] = state[4];  tmp[8] = state[8];   tmp[12] = state[12];
        tmp[1] = state[13]; tmp[5] = state[1];  tmp[9] = state[5];   tmp[13] = state[9];
        tmp[2] = state[10]; tmp[6] = state[14]; tmp[10] = state[2];  tmp[14] = state[6];
        tmp[3] = state[7];  tmp[7] = state[11]; tmp[11] = state[15]; tmp[15] = state[3];
        
        /* InvSubBytes */
        for (int i = 0; i < 16; i++) {
            state[i] = aes_rsbox[tmp[i]];
        }
        
        /* AddRoundKey */
        for (int i = 0; i < 16; i++) {
            state[i] ^= ((rk[round * 4 + i / 4] >> (24 - 8 * (i % 4))) & 0xff);
        }
        
        /* InvMixColumns */
        for (int c = 0; c < 4; c++) {
            uint8_t a = state[c*4], b = state[c*4+1], cv = state[c*4+2], d = state[c*4+3];
            state[c*4]   = aes_mul(a, 0x0e) ^ aes_mul(b, 0x0b) ^ aes_mul(cv, 0x0d) ^ aes_mul(d, 0x09);
            state[c*4+1] = aes_mul(a, 0x09) ^ aes_mul(b, 0x0e) ^ aes_mul(cv, 0x0b) ^ aes_mul(d, 0x0d);
            state[c*4+2] = aes_mul(a, 0x0d) ^ aes_mul(b, 0x09) ^ aes_mul(cv, 0x0e) ^ aes_mul(d, 0x0b);
            state[c*4+3] = aes_mul(a, 0x0b) ^ aes_mul(b, 0x0d) ^ aes_mul(cv, 0x09) ^ aes_mul(d, 0x0e);
        }
    }
    
    /* Final inverse round (no InvMixColumns) */
    uint8_t tmp[16];
    tmp[0] = state[0];  tmp[4] = state[4];  tmp[8] = state[8];   tmp[12] = state[12];
    tmp[1] = state[13]; tmp[5] = state[1];  tmp[9] = state[5];   tmp[13] = state[9];
    tmp[2] = state[10]; tmp[6] = state[14]; tmp[10] = state[2];  tmp[14] = state[6];
    tmp[3] = state[7];  tmp[7] = state[11]; tmp[11] = state[15]; tmp[15] = state[3];
    
    for (int i = 0; i < 16; i++) {
        state[i] = aes_rsbox[tmp[i]];
    }
    
    for (int i = 0; i < 16; i++) {
        out[i] = state[i] ^ ((rk[i / 4] >> (24 - 8 * (i % 4))) & 0xff);
    }
}

/* ============ PRNG ============ */

static uint32_t rand_seed = 123456789;

void crypto_srand(uint32_t seed) {
    rand_seed = seed;
}

uint32_t crypto_rand(void) {
    /* Simple LCG */
    rand_seed = rand_seed * 1103515245 + 12345;
    return (uint32_t)(rand_seed / 65536) % 32768;
}

void crypto_rand_bytes(uint8_t *buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(crypto_rand() & 0xFF);
    }
}
