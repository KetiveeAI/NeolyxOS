/*
 * NXZIP - NeolyxOS Compression Library
 * DEFLATE Compression Implementation
 * 
 * Simplified DEFLATE using LZ77 + Huffman (compatible with ZIP)
 * 
 * Copyright (c) 2025-2026 KetiveeAI. All Rights Reserved.
 */

#include <stdlib.h>
#include <string.h>
#include "../include/nxzip.h"

/* ========================================================================== */
/* Bit Stream Writer                                                           */
/* ========================================================================== */

typedef struct {
    uint8_t* data;
    size_t capacity;
    size_t byte_pos;
    int bit_pos;
} bit_writer_t;

static void bw_init(bit_writer_t* bw, size_t initial_size) {
    bw->data = malloc(initial_size);
    bw->capacity = initial_size;
    bw->byte_pos = 0;
    bw->bit_pos = 0;
    if (bw->data) memset(bw->data, 0, initial_size);
}

static void bw_ensure(bit_writer_t* bw, size_t needed) {
    size_t total = bw->byte_pos + needed + 16;
    if (total > bw->capacity) {
        size_t new_cap = bw->capacity * 2;
        if (new_cap < total) new_cap = total;
        bw->data = realloc(bw->data, new_cap);
        memset(bw->data + bw->capacity, 0, new_cap - bw->capacity);
        bw->capacity = new_cap;
    }
}

static void bw_write_bits(bit_writer_t* bw, uint32_t value, int bits) {
    bw_ensure(bw, (bits + 7) / 8 + 1);
    
    while (bits > 0) {
        int available = 8 - bw->bit_pos;
        int write = (bits < available) ? bits : available;
        uint8_t mask = (1 << write) - 1;
        
        bw->data[bw->byte_pos] |= (value & mask) << bw->bit_pos;
        value >>= write;
        bits -= write;
        bw->bit_pos += write;
        
        if (bw->bit_pos >= 8) {
            bw->byte_pos++;
            bw->bit_pos = 0;
        }
    }
}

static void bw_flush(bit_writer_t* bw) {
    if (bw->bit_pos > 0) {
        bw->byte_pos++;
        bw->bit_pos = 0;
    }
}

/* ========================================================================== */
/* Bit Stream Reader                                                           */
/* ========================================================================== */

typedef struct {
    const uint8_t* data;
    size_t size;
    size_t byte_pos;
    int bit_pos;
} bit_reader_t;

static void br_init(bit_reader_t* br, const void* data, size_t size) {
    br->data = (const uint8_t*)data;
    br->size = size;
    br->byte_pos = 0;
    br->bit_pos = 0;
}

static uint32_t br_read_bits(bit_reader_t* br, int bits) {
    uint32_t result = 0;
    int shift = 0;
    
    while (bits > 0 && br->byte_pos < br->size) {
        int available = 8 - br->bit_pos;
        int read = (bits < available) ? bits : available;
        uint8_t mask = (1 << read) - 1;
        
        result |= ((br->data[br->byte_pos] >> br->bit_pos) & mask) << shift;
        shift += read;
        bits -= read;
        br->bit_pos += read;
        
        if (br->bit_pos >= 8) {
            br->byte_pos++;
            br->bit_pos = 0;
        }
    }
    
    return result;
}

/* ========================================================================== */
/* Fixed Huffman Tables (DEFLATE standard)                                     */
/* ========================================================================== */

/* Literal/Length code lengths for fixed Huffman */
static const int fixed_lit_lengths[288] = {
    /* 0-143: 8 bits */
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    /* 144-255: 9 bits */
    9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    /* 256-279: 7 bits */
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, 7,7,7,7,7,7,7,7,
    /* 280-287: 8 bits */
    8,8,8,8,8,8,8,8
};

/* Length extra bits */
static const int len_extra_bits[29] = {
    0,0,0,0,0,0,0,0, 1,1,1,1, 2,2,2,2, 3,3,3,3, 4,4,4,4, 5,5,5,5, 0
};

/* Length base values */
static const int len_base[29] = {
    3,4,5,6,7,8,9,10, 11,13,15,17, 19,23,27,31, 35,43,51,59,
    67,83,99,115, 131,163,195,227, 258
};

/* Distance extra bits */
static const int dist_extra_bits[30] = {
    0,0,0,0, 1,1,2,2, 3,3,4,4, 5,5,6,6, 7,7,8,8, 9,9,10,10, 11,11,12,12, 13,13
};

/* Distance base values */
static const int dist_base[30] = {
    1,2,3,4, 5,7,9,13, 17,25,33,49, 65,97,129,193, 257,385,513,769,
    1025,1537,2049,3073, 4097,6145,8193,12289, 16385,24577
};

/* ========================================================================== */
/* LZ77 Compression                                                            */
/* ========================================================================== */

#define LZ77_WINDOW_SIZE  32768
#define LZ77_MIN_MATCH    3
#define LZ77_MAX_MATCH    258
#define LZ77_HASH_BITS    15
#define LZ77_HASH_SIZE    (1 << LZ77_HASH_BITS)

typedef struct {
    int length;    /* Match length (0 = literal) */
    int distance;  /* Match distance */
    uint8_t literal;  /* Literal byte if length == 0 */
} lz77_token_t;

/* Simple hash function for LZ77 */
static uint32_t lz77_hash(const uint8_t* data) {
    return ((data[0] << 10) ^ (data[1] << 5) ^ data[2]) & (LZ77_HASH_SIZE - 1);
}

/* Find best match at current position */
static int lz77_find_match(const uint8_t* data, size_t pos, size_t size,
                           int* out_distance, int* hash_table, int* hash_prev) {
    if (pos + LZ77_MIN_MATCH > size) return 0;
    
    uint32_t hash = lz77_hash(data + pos);
    int best_len = 0;
    int best_dist = 0;
    
    int chain = hash_table[hash];
    int chain_limit = 128;  /* Limit chain search for speed */
    
    while (chain >= 0 && chain_limit-- > 0) {
        int dist = pos - chain;
        if (dist > LZ77_WINDOW_SIZE) break;
        
        /* Check match length */
        int len = 0;
        size_t max_len = size - pos;
        if (max_len > LZ77_MAX_MATCH) max_len = LZ77_MAX_MATCH;
        
        while (len < (int)max_len && data[chain + len] == data[pos + len]) {
            len++;
        }
        
        if (len > best_len && len >= LZ77_MIN_MATCH) {
            best_len = len;
            best_dist = dist;
            if (len >= LZ77_MAX_MATCH) break;
        }
        
        chain = hash_prev[chain & (LZ77_WINDOW_SIZE - 1)];
    }
    
    *out_distance = best_dist;
    return best_len;
}

/* ========================================================================== */
/* DEFLATE Encoder                                                             */
/* ========================================================================== */

/* Reverse bits for canonical Huffman */
static uint32_t reverse_bits(uint32_t value, int bits) {
    uint32_t result = 0;
    for (int i = 0; i < bits; i++) {
        result = (result << 1) | (value & 1);
        value >>= 1;
    }
    return result;
}

/* Write fixed Huffman literal/length code */
static void write_lit_code(bit_writer_t* bw, int value) {
    if (value < 144) {
        bw_write_bits(bw, reverse_bits(0x30 + value, 8), 8);
    } else if (value < 256) {
        bw_write_bits(bw, reverse_bits(0x190 + value - 144, 9), 9);
    } else if (value < 280) {
        bw_write_bits(bw, reverse_bits(value - 256, 7), 7);
    } else {
        bw_write_bits(bw, reverse_bits(0xC0 + value - 280, 8), 8);
    }
}

/* Compress data using DEFLATE */
int nxzip_deflate(const void* src, size_t src_size, void** dst, size_t* dst_size) {
    if (!src || !dst || !dst_size) return NXZIP_ERR_MEMORY;
    if (src_size == 0) {
        *dst = NULL;
        *dst_size = 0;
        return NXZIP_OK;
    }
    
    const uint8_t* input = (const uint8_t*)src;
    
    /* Allocate hash tables */
    int* hash_table = malloc(LZ77_HASH_SIZE * sizeof(int));
    int* hash_prev = malloc(LZ77_WINDOW_SIZE * sizeof(int));
    if (!hash_table || !hash_prev) {
        free(hash_table);
        free(hash_prev);
        return NXZIP_ERR_MEMORY;
    }
    memset(hash_table, -1, LZ77_HASH_SIZE * sizeof(int));
    
    /* Initialize bit writer */
    bit_writer_t bw;
    bw_init(&bw, src_size + 256);  /* May grow larger if incompressible */
    if (!bw.data) {
        free(hash_table);
        free(hash_prev);
        return NXZIP_ERR_MEMORY;
    }
    
    /* Write DEFLATE block header: final block, fixed Huffman */
    bw_write_bits(&bw, 1, 1);  /* BFINAL = 1 */
    bw_write_bits(&bw, 1, 2);  /* BTYPE = 01 (fixed Huffman) */
    
    /* LZ77 encode and output */
    size_t pos = 0;
    while (pos < src_size) {
        int distance = 0;
        int length = lz77_find_match(input, pos, src_size, &distance, hash_table, hash_prev);
        
        if (length >= LZ77_MIN_MATCH) {
            /* Output length code */
            int len_code = 0;
            for (int i = 0; i < 29; i++) {
                if (length >= len_base[i] && (i == 28 || length < len_base[i+1])) {
                    len_code = i;
                    break;
                }
            }
            write_lit_code(&bw, 257 + len_code);
            if (len_extra_bits[len_code] > 0) {
                bw_write_bits(&bw, length - len_base[len_code], len_extra_bits[len_code]);
            }
            
            /* Output distance code (5 bits fixed) */
            int dist_code = 0;
            for (int i = 0; i < 30; i++) {
                if (distance >= dist_base[i] && (i == 29 || distance < dist_base[i+1])) {
                    dist_code = i;
                    break;
                }
            }
            bw_write_bits(&bw, reverse_bits(dist_code, 5), 5);
            if (dist_extra_bits[dist_code] > 0) {
                bw_write_bits(&bw, distance - dist_base[dist_code], dist_extra_bits[dist_code]);
            }
            
            /* Update hash for matched bytes */
            for (int i = 0; i < length; i++) {
                if (pos + i + LZ77_MIN_MATCH <= src_size) {
                    uint32_t h = lz77_hash(input + pos + i);
                    hash_prev[(pos + i) & (LZ77_WINDOW_SIZE - 1)] = hash_table[h];
                    hash_table[h] = pos + i;
                }
            }
            pos += length;
        } else {
            /* Output literal */
            write_lit_code(&bw, input[pos]);
            
            /* Update hash */
            if (pos + LZ77_MIN_MATCH <= src_size) {
                uint32_t h = lz77_hash(input + pos);
                hash_prev[pos & (LZ77_WINDOW_SIZE - 1)] = hash_table[h];
                hash_table[h] = pos;
            }
            pos++;
        }
    }
    
    /* Write end-of-block code (256) */
    write_lit_code(&bw, 256);
    bw_flush(&bw);
    
    free(hash_table);
    free(hash_prev);
    
    *dst = bw.data;
    *dst_size = bw.byte_pos;
    return NXZIP_OK;
}

/* ========================================================================== */
/* DEFLATE Decoder                                                             */
/* ========================================================================== */

/* Code length alphabet order (RFC 1951) */
static const int codelen_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

/* Huffman table for dynamic decoding */
#define HUFFMAN_MAX_BITS 15
#define HUFFMAN_MAIN_SIZE 512   /* 2^9 for fast lookup */
typedef struct {
    uint16_t counts[HUFFMAN_MAX_BITS + 1];   /* Count of codes per length */
    uint16_t symbols[288];                    /* Symbol table */
    int max_len;                              /* Maximum code length */
} huffman_table_t;

/* Build Huffman table from code lengths (RFC 1951 algorithm) */
static int huffman_build(huffman_table_t* ht, const uint8_t* lengths, int count) {
    /* Clear counts */
    memset(ht->counts, 0, sizeof(ht->counts));
    ht->max_len = 0;
    
    /* Count code lengths */
    for (int i = 0; i < count; i++) {
        if (lengths[i] > 0) {
            ht->counts[lengths[i]]++;
            if (lengths[i] > ht->max_len) ht->max_len = lengths[i];
        }
    }
    
    /* Compute offsets for each length */
    uint16_t offsets[HUFFMAN_MAX_BITS + 1];
    offsets[1] = 0;
    for (int i = 1; i < HUFFMAN_MAX_BITS; i++) {
        offsets[i + 1] = offsets[i] + ht->counts[i];
    }
    
    /* Build symbol table sorted by code length then symbol */
    for (int i = 0; i < count; i++) {
        if (lengths[i] > 0) {
            ht->symbols[offsets[lengths[i]]++] = i;
        }
    }
    
    return NXZIP_OK;
}

/* Decode a symbol using Huffman table */
static int huffman_decode(bit_reader_t* br, const huffman_table_t* ht, const uint8_t* lengths) {
    uint32_t code = 0;
    int first = 0;
    int index = 0;
    
    for (int len = 1; len <= ht->max_len; len++) {
        code = (code << 1) | br_read_bits(br, 1);
        int count = ht->counts[len];
        if ((int)(code - first) < count) {
            return ht->symbols[index + (code - first)];
        }
        index += count;
        first = (first + count) << 1;
    }
    
    return -1;  /* Invalid code */
}

/* Decode dynamic Huffman tables from stream */
static int decode_dynamic_tables(bit_reader_t* br, huffman_table_t* lit_ht, 
                                  huffman_table_t* dist_ht,
                                  uint8_t* lit_lengths, uint8_t* dist_lengths) {
    /* Read header */
    int hlit = br_read_bits(br, 5) + 257;   /* Literal/length codes */
    int hdist = br_read_bits(br, 5) + 1;    /* Distance codes */
    int hclen = br_read_bits(br, 4) + 4;    /* Code length codes */
    
    /* Read code length code lengths */
    uint8_t codelen_lengths[19] = {0};
    for (int i = 0; i < hclen; i++) {
        codelen_lengths[codelen_order[i]] = br_read_bits(br, 3);
    }
    
    /* Build code length Huffman table */
    huffman_table_t codelen_ht;
    huffman_build(&codelen_ht, codelen_lengths, 19);
    
    /* Decode literal/length and distance code lengths */
    uint8_t all_lengths[286 + 32];
    int total = hlit + hdist;
    int i = 0;
    
    while (i < total) {
        int sym = huffman_decode(br, &codelen_ht, codelen_lengths);
        if (sym < 0) return NXZIP_ERR_FORMAT;
        
        if (sym < 16) {
            all_lengths[i++] = sym;
        } else if (sym == 16) {
            /* Repeat previous 3-6 times */
            int repeat = br_read_bits(br, 2) + 3;
            if (i == 0) return NXZIP_ERR_FORMAT;
            uint8_t prev = all_lengths[i - 1];
            while (repeat-- > 0 && i < total) all_lengths[i++] = prev;
        } else if (sym == 17) {
            /* Repeat 0 for 3-10 times */
            int repeat = br_read_bits(br, 3) + 3;
            while (repeat-- > 0 && i < total) all_lengths[i++] = 0;
        } else if (sym == 18) {
            /* Repeat 0 for 11-138 times */
            int repeat = br_read_bits(br, 7) + 11;
            while (repeat-- > 0 && i < total) all_lengths[i++] = 0;
        }
    }
    
    /* Split into literal and distance lengths */
    memcpy(lit_lengths, all_lengths, hlit);
    memcpy(dist_lengths, all_lengths + hlit, hdist);
    
    /* Build tables */
    huffman_build(lit_ht, lit_lengths, hlit);
    huffman_build(dist_ht, dist_lengths, hdist);
    
    return NXZIP_OK;
}

/* Decode a single symbol from fixed Huffman codes */
static int decode_fixed_huffman(bit_reader_t* br) {
    /* Fixed Huffman table:
     *   0-143:   8-bit codes starting at 00110000 (0x30)
     *   144-255: 9-bit codes starting at 110010000 (0x190)
     *   256-279: 7-bit codes starting at 0000000 (0x00)
     *   280-287: 8-bit codes starting at 11000000 (0xC0)
     */
    
    /* Read 7 bits first */
    uint32_t code = 0;
    for (int i = 0; i < 7; i++) {
        code = (code << 1) | br_read_bits(br, 1);
    }
    
    /* Check 7-bit range: 0x00-0x17 → symbols 256-279 */
    if (code <= 0x17) {
        return 256 + code;
    }
    
    /* Read 8th bit */
    code = (code << 1) | br_read_bits(br, 1);
    
    /* Check 8-bit ranges */
    if (code >= 0x30 && code <= 0xBF) {
        /* 0x30-0xBF → symbols 0-143 */
        return code - 0x30;
    }
    if (code >= 0xC0 && code <= 0xC7) {
        /* 0xC0-0xC7 → symbols 280-287 */
        return 280 + (code - 0xC0);
    }
    
    /* Read 9th bit */
    code = (code << 1) | br_read_bits(br, 1);
    
    /* 9-bit range: 0x190-0x1FF → symbols 144-255 */
    if (code >= 0x190 && code <= 0x1FF) {
        return 144 + (code - 0x190);
    }
    
    /* Invalid code */
    return -1;
}

int nxzip_inflate(const void* src, size_t src_size, void* dst, size_t dst_size) {
    if (!src || !dst) return NXZIP_ERR_MEMORY;
    
    bit_reader_t br;
    br_init(&br, src, src_size);
    
    uint8_t* output = (uint8_t*)dst;
    size_t out_pos = 0;
    
    int bfinal = 0;
    
    do {
        /* Read block header */
        bfinal = br_read_bits(&br, 1);
        int btype = br_read_bits(&br, 2);
        
        if (btype == 0) {
            /* Stored block (no compression) */
            /* Align to byte boundary */
            if (br.bit_pos > 0) {
                br.byte_pos++;
                br.bit_pos = 0;
            }
            
            uint16_t len = br.data[br.byte_pos] | (br.data[br.byte_pos + 1] << 8);
            br.byte_pos += 4;  /* Skip LEN and NLEN */
            
            if (out_pos + len > dst_size) return NXZIP_ERR_MEMORY;
            memcpy(output + out_pos, br.data + br.byte_pos, len);
            out_pos += len;
            br.byte_pos += len;
            
        } else if (btype == 1) {
            /* Fixed Huffman codes */
            while (1) {
                int symbol = decode_fixed_huffman(&br);
                if (symbol < 0) return NXZIP_ERR_FORMAT;
                
                if (symbol == 256) {
                    /* End of block */
                    break;
                } else if (symbol < 256) {
                    /* Literal byte */
                    if (out_pos >= dst_size) return NXZIP_ERR_MEMORY;
                    output[out_pos++] = (uint8_t)symbol;
                } else {
                    /* Length/distance pair */
                    int len_code = symbol - 257;
                    if (len_code >= 29) return NXZIP_ERR_FORMAT;
                    
                    int length = len_base[len_code];
                    if (len_extra_bits[len_code] > 0) {
                        length += br_read_bits(&br, len_extra_bits[len_code]);
                    }
                    
                    /* Read distance code (5 bits, MSB first) */
                    uint32_t dist_code = 0;
                    for (int i = 0; i < 5; i++) {
                        dist_code = (dist_code << 1) | br_read_bits(&br, 1);
                    }
                    if (dist_code >= 30) return NXZIP_ERR_FORMAT;
                    
                    int distance = dist_base[dist_code];
                    if (dist_extra_bits[dist_code] > 0) {
                        distance += br_read_bits(&br, dist_extra_bits[dist_code]);
                    }
                    
                    /* Validate */
                    if (distance > (int)out_pos) return NXZIP_ERR_FORMAT;
                    if (out_pos + length > dst_size) return NXZIP_ERR_MEMORY;
                    
                    /* Copy from back-reference */
                    for (int i = 0; i < length; i++) {
                        output[out_pos] = output[out_pos - distance];
                        out_pos++;
                    }
                }
            }
        } else if (btype == 2) {
            /* Dynamic Huffman codes */
            huffman_table_t lit_ht, dist_ht;
            uint8_t lit_lengths[286] = {0};
            uint8_t dist_lengths[32] = {0};
            
            int result = decode_dynamic_tables(&br, &lit_ht, &dist_ht, lit_lengths, dist_lengths);
            if (result != NXZIP_OK) return result;
            
            /* Decode data using dynamic tables */
            while (1) {
                int symbol = huffman_decode(&br, &lit_ht, lit_lengths);
                if (symbol < 0) return NXZIP_ERR_FORMAT;
                
                if (symbol == 256) {
                    /* End of block */
                    break;
                } else if (symbol < 256) {
                    /* Literal byte */
                    if (out_pos >= dst_size) return NXZIP_ERR_MEMORY;
                    output[out_pos++] = (uint8_t)symbol;
                } else {
                    /* Length/distance pair */
                    int len_code = symbol - 257;
                    if (len_code >= 29) return NXZIP_ERR_FORMAT;
                    
                    int length = len_base[len_code];
                    if (len_extra_bits[len_code] > 0) {
                        length += br_read_bits(&br, len_extra_bits[len_code]);
                    }
                    
                    /* Decode distance using dynamic table */
                    int dist_code = huffman_decode(&br, &dist_ht, dist_lengths);
                    if (dist_code < 0 || dist_code >= 30) return NXZIP_ERR_FORMAT;
                    
                    int distance = dist_base[dist_code];
                    if (dist_extra_bits[dist_code] > 0) {
                        distance += br_read_bits(&br, dist_extra_bits[dist_code]);
                    }
                    
                    /* Validate */
                    if (distance > (int)out_pos) return NXZIP_ERR_FORMAT;
                    if (out_pos + length > dst_size) return NXZIP_ERR_MEMORY;
                    
                    /* Copy from back-reference */
                    for (int i = 0; i < length; i++) {
                        output[out_pos] = output[out_pos - distance];
                        out_pos++;
                    }
                }
            }
        } else {
            return NXZIP_ERR_FORMAT;
        }
        
    } while (!bfinal);
    
    return NXZIP_OK;
}
