/*
 * NeolyxOS PNG Decoder (nxpng.c)
 * 
 * Minimal PNG decoder for userspace. Based on pnglite concepts from FreeBSD.
 * Supports: 8-bit RGB, RGBA, Grayscale, Indexed color
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "nximage.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* PNG signature */
static const uint8_t PNG_SIG[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

/* Chunk types */
#define CHUNK_IHDR 0x49484452
#define CHUNK_IDAT 0x49444154
#define CHUNK_IEND 0x49454E44
#define CHUNK_PLTE 0x504C5445

/* Color types */
#define PNG_GRAYSCALE       0
#define PNG_RGB             2
#define PNG_INDEXED         3
#define PNG_GRAYSCALE_ALPHA 4
#define PNG_RGBA            6

/* Filter types */
#define FILTER_NONE    0
#define FILTER_SUB     1
#define FILTER_UP      2
#define FILTER_AVERAGE 3
#define FILTER_PAETH   4

/* ============ DEFLATE Mini-Decompressor ============ */

typedef struct {
    const uint8_t *src;
    size_t src_len;
    size_t src_pos;
    uint32_t bit_buf;
    int bit_cnt;
} inflate_state_t;

static inline int inf_get_bit(inflate_state_t *s) {
    if (s->bit_cnt == 0) {
        if (s->src_pos >= s->src_len) return -1;
        s->bit_buf = s->src[s->src_pos++];
        s->bit_cnt = 8;
    }
    int bit = s->bit_buf & 1;
    s->bit_buf >>= 1;
    s->bit_cnt--;
    return bit;
}

static inline int inf_get_bits(inflate_state_t *s, int n) {
    int val = 0;
    for (int i = 0; i < n; i++) {
        int bit = inf_get_bit(s);
        if (bit < 0) return -1;
        val |= (bit << i);
    }
    return val;
}

/* Fixed Huffman code lengths */
static const uint8_t fixed_lit_lengths[288] = {
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,8,8,8,8,8,8,8,8
};

/* Length base values */
static const uint16_t len_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const uint8_t len_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};

/* Distance base values */
static const uint16_t dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
    1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const uint8_t dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

/* Huffman decode table */
typedef struct {
    uint16_t counts[16];
    uint16_t symbols[288];
} huff_table_t;

static void build_huffman(huff_table_t *t, const uint8_t *lengths, int num) {
    memset(t->counts, 0, sizeof(t->counts));
    for (int i = 0; i < num; i++) {
        if (lengths[i]) t->counts[lengths[i]]++;
    }
    
    uint16_t offsets[16];
    offsets[0] = 0;
    for (int i = 1; i < 16; i++) {
        offsets[i] = offsets[i-1] + t->counts[i-1];
    }
    
    for (int i = 0; i < num; i++) {
        if (lengths[i]) {
            t->symbols[offsets[lengths[i]]++] = i;
        }
    }
}

static int decode_symbol(inflate_state_t *s, huff_table_t *t) {
    int code = 0, first = 0, index = 0;
    
    for (int len = 1; len <= 15; len++) {
        int bit = inf_get_bit(s);
        if (bit < 0) return -1;
        code = (code << 1) | bit;
        int count = t->counts[len];
        if (code - first < count) {
            return t->symbols[index + (code - first)];
        }
        first = (first + count) << 1;
        index += count;
    }
    return -1;
}

/* Decompress DEFLATE stream */
static int inflate_data(const uint8_t *src, size_t src_len, 
                        uint8_t *dst, size_t dst_len, size_t *out_len) {
    inflate_state_t state = {src, src_len, 0, 0, 0};
    size_t dst_pos = 0;
    
    int bfinal = 0;
    while (!bfinal) {
        bfinal = inf_get_bit(&state);
        int btype = inf_get_bits(&state, 2);
        
        if (btype == 0) {
            /* Stored block */
            state.bit_cnt = 0;
            if (state.src_pos + 4 > src_len) return NXI_ERR_DECODE;
            uint16_t len = state.src[state.src_pos] | (state.src[state.src_pos+1] << 8);
            state.src_pos += 4;
            if (state.src_pos + len > src_len) return NXI_ERR_DECODE;
            if (dst_pos + len > dst_len) return NXI_ERR_MEMORY;
            memcpy(dst + dst_pos, state.src + state.src_pos, len);
            state.src_pos += len;
            dst_pos += len;
        } else if (btype == 1 || btype == 2) {
            /* Fixed or dynamic Huffman */
            huff_table_t lit_table, dist_table;
            
            if (btype == 1) {
                /* Fixed codes */
                build_huffman(&lit_table, fixed_lit_lengths, 288);
                uint8_t dist_lengths[32];
                for (int i = 0; i < 32; i++) dist_lengths[i] = 5;
                build_huffman(&dist_table, dist_lengths, 32);
            } else {
                /* Dynamic codes - simplified */
                int hlit = inf_get_bits(&state, 5) + 257;
                int hdist = inf_get_bits(&state, 5) + 1;
                int hclen = inf_get_bits(&state, 4) + 4;
                
                static const uint8_t cl_order[19] = {
                    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
                };
                uint8_t cl_lengths[19] = {0};
                for (int i = 0; i < hclen; i++) {
                    cl_lengths[cl_order[i]] = inf_get_bits(&state, 3);
                }
                
                huff_table_t cl_table;
                build_huffman(&cl_table, cl_lengths, 19);
                
                uint8_t lengths[320] = {0};
                int num = hlit + hdist;
                for (int i = 0; i < num;) {
                    int sym = decode_symbol(&state, &cl_table);
                    if (sym < 0) return NXI_ERR_DECODE;
                    if (sym < 16) {
                        lengths[i++] = sym;
                    } else if (sym == 16) {
                        int rep = inf_get_bits(&state, 2) + 3;
                        uint8_t prev = i > 0 ? lengths[i-1] : 0;
                        while (rep-- && i < num) lengths[i++] = prev;
                    } else if (sym == 17) {
                        int rep = inf_get_bits(&state, 3) + 3;
                        while (rep-- && i < num) lengths[i++] = 0;
                    } else {
                        int rep = inf_get_bits(&state, 7) + 11;
                        while (rep-- && i < num) lengths[i++] = 0;
                    }
                }
                
                build_huffman(&lit_table, lengths, hlit);
                build_huffman(&dist_table, lengths + hlit, hdist);
            }
            
            /* Decode symbols */
            while (1) {
                int sym = decode_symbol(&state, &lit_table);
                if (sym < 0) return NXI_ERR_DECODE;
                
                if (sym < 256) {
                    if (dst_pos >= dst_len) return NXI_ERR_MEMORY;
                    dst[dst_pos++] = sym;
                } else if (sym == 256) {
                    break;
                } else {
                    int len_idx = sym - 257;
                    if (len_idx >= 29) return NXI_ERR_DECODE;
                    int length = len_base[len_idx] + inf_get_bits(&state, len_extra[len_idx]);
                    
                    int dist_sym = decode_symbol(&state, &dist_table);
                    if (dist_sym < 0 || dist_sym >= 30) return NXI_ERR_DECODE;
                    int distance = dist_base[dist_sym] + inf_get_bits(&state, dist_extra[dist_sym]);
                    
                    if (dst_pos + length > dst_len) return NXI_ERR_MEMORY;
                    if (distance > (int)dst_pos) return NXI_ERR_DECODE;
                    
                    for (int i = 0; i < length; i++) {
                        dst[dst_pos] = dst[dst_pos - distance];
                        dst_pos++;
                    }
                }
            }
        } else {
            return NXI_ERR_FORMAT;
        }
    }
    
    *out_len = dst_pos;
    return NXI_OK;
}

/* ============ PNG Filter Reconstruction ============ */

static inline uint8_t paeth_predictor(uint8_t a, uint8_t b, uint8_t c) {
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static int reconstruct_scanlines(uint8_t *raw, uint32_t w, uint32_t h, 
                                  int bpp, uint8_t *out) {
    int row_bytes = w * bpp;
    
    for (uint32_t y = 0; y < h; y++) {
        uint8_t filter = raw[y * (row_bytes + 1)];
        uint8_t *row = raw + y * (row_bytes + 1) + 1;
        uint8_t *prev = (y > 0) ? out + (y - 1) * row_bytes : NULL;
        uint8_t *dst = out + y * row_bytes;
        
        for (int x = 0; x < row_bytes; x++) {
            uint8_t a = (x >= bpp) ? dst[x - bpp] : 0;
            uint8_t b = prev ? prev[x] : 0;
            uint8_t c = (prev && x >= bpp) ? prev[x - bpp] : 0;
            
            switch (filter) {
                case FILTER_NONE:
                    dst[x] = row[x];
                    break;
                case FILTER_SUB:
                    dst[x] = row[x] + a;
                    break;
                case FILTER_UP:
                    dst[x] = row[x] + b;
                    break;
                case FILTER_AVERAGE:
                    dst[x] = row[x] + ((a + b) >> 1);
                    break;
                case FILTER_PAETH:
                    dst[x] = row[x] + paeth_predictor(a, b, c);
                    break;
                default:
                    return NXI_ERR_FORMAT;
            }
        }
    }
    return NXI_OK;
}

/* ============ PNG Chunk Parsing ============ */

static inline uint32_t read_be32(const uint8_t *p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

int nxpng_load(const uint8_t *data, size_t len, nximage_t *img) {
    if (len < 8 || memcmp(data, PNG_SIG, 8) != 0) {
        return NXI_ERR_FORMAT;
    }
    
    /* Parse IHDR */
    if (len < 8 + 8 + 13) return NXI_ERR_FORMAT;
    
    const uint8_t *ihdr = data + 8 + 8;
    uint32_t width = read_be32(ihdr);
    uint32_t height = read_be32(ihdr + 4);
    uint8_t bit_depth = ihdr[8];
    uint8_t color_type = ihdr[9];
    
    if (bit_depth != 8) return NXI_ERR_UNSUPPORTED;
    
    int bpp;
    switch (color_type) {
        case PNG_GRAYSCALE: bpp = 1; break;
        case PNG_RGB: bpp = 3; break;
        case PNG_RGBA: bpp = 4; break;
        case PNG_GRAYSCALE_ALPHA: bpp = 2; break;
        case PNG_INDEXED: bpp = 1; break;
        default: return NXI_ERR_UNSUPPORTED;
    }
    
    /* Collect IDAT chunks */
    size_t idat_total = 0;
    size_t pos = 8;
    while (pos + 12 <= len) {
        uint32_t chunk_len = read_be32(data + pos);
        uint32_t chunk_type = read_be32(data + pos + 4);
        if (chunk_type == CHUNK_IDAT) {
            idat_total += chunk_len;
        }
        pos += 12 + chunk_len;
    }
    
    if (idat_total == 0) return NXI_ERR_FORMAT;
    
    /* Allocate and copy IDAT data */
    uint8_t *compressed = (uint8_t *)img->pixels;  /* Temporary reuse */
    size_t raw_size = (width * bpp + 1) * height;
    uint8_t *raw = (uint8_t *)malloc(raw_size + idat_total + 65536);
    if (!raw) return NXI_ERR_MEMORY;
    
    compressed = raw + raw_size;
    size_t idat_pos = 0;
    pos = 8;
    while (pos + 12 <= len) {
        uint32_t chunk_len = read_be32(data + pos);
        uint32_t chunk_type = read_be32(data + pos + 4);
        if (chunk_type == CHUNK_IDAT) {
            memcpy(compressed + idat_pos, data + pos + 8, chunk_len);
            idat_pos += chunk_len;
        }
        pos += 12 + chunk_len;
    }
    
    /* Skip zlib header (2 bytes) */
    if (idat_pos < 2) {
        free(raw);
        return NXI_ERR_FORMAT;
    }
    
    /* Decompress */
    size_t decompressed_len;
    int err = inflate_data(compressed + 2, idat_pos - 2, raw, raw_size, &decompressed_len);
    if (err != NXI_OK) {
        free(raw);
        return err;
    }
    
    /* Allocate output */
    img->width = width;
    img->height = height;
    img->bpp = 4;
    img->format = NXIMG_RGBA32;
    img->type = NXIMG_TYPE_PNG;
    img->pixel_size = width * height * 4;
    img->pixels = (uint8_t *)malloc(img->pixel_size);
    if (!img->pixels) {
        free(raw);
        return NXI_ERR_MEMORY;
    }
    
    /* Reconstruct filtered scanlines */
    uint8_t *filtered = (uint8_t *)malloc(width * bpp * height);
    if (!filtered) {
        free(raw);
        free(img->pixels);
        return NXI_ERR_MEMORY;
    }
    
    err = reconstruct_scanlines(raw, width, height, bpp, filtered);
    free(raw);
    if (err != NXI_OK) {
        free(filtered);
        free(img->pixels);
        return err;
    }
    
    /* Convert to RGBA32 */
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint8_t *src = filtered + (y * width + x) * bpp;
            uint8_t *dst = img->pixels + (y * width + x) * 4;
            
            switch (color_type) {
                case PNG_GRAYSCALE:
                    dst[0] = dst[1] = dst[2] = src[0];
                    dst[3] = 255;
                    break;
                case PNG_RGB:
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                    dst[3] = 255;
                    break;
                case PNG_RGBA:
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                    dst[3] = src[3];
                    break;
                case PNG_GRAYSCALE_ALPHA:
                    dst[0] = dst[1] = dst[2] = src[0];
                    dst[3] = src[1];
                    break;
                default:
                    dst[0] = dst[1] = dst[2] = dst[3] = 0;
            }
        }
    }
    
    free(filtered);
    return NXI_OK;
}

int nxpng_info(const uint8_t *data, size_t len, uint32_t *w, uint32_t *h) {
    if (len < 8 + 8 + 13 || memcmp(data, PNG_SIG, 8) != 0) {
        return NXI_ERR_FORMAT;
    }
    
    const uint8_t *ihdr = data + 8 + 8;
    if (w) *w = read_be32(ihdr);
    if (h) *h = read_be32(ihdr + 4);
    return NXI_OK;
}

/* External malloc/free for freestanding */
#ifndef malloc
extern void *nx_malloc(size_t size);
extern void nx_free(void *ptr);
#define malloc nx_malloc
#define free nx_free
#endif
