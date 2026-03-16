/*
 * NeolyxOS Image Library - Main Dispatcher (nximage.c)
 * 
 * Routes image loading to format-specific decoders.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "nximage.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Forward declarations for decoders */
extern int nxpng_load(const uint8_t *data, size_t len, nximage_t *img);
extern int nxpng_info(const uint8_t *data, size_t len, uint32_t *w, uint32_t *h);
extern int nxnxi_load(const uint8_t *data, size_t len, nximage_t *img);
extern int nxnxi_info(const uint8_t *data, size_t len, uint32_t *w, uint32_t *h);

/* File I/O - platform specific */
#ifdef __NEOLYXOS__
#include <nxsyscall.h>
#define fopen(p, m) ((void*)nx_open(p, 0))
#define fclose(f) nx_close((int)(uintptr_t)f)
#define fread(b, s, n, f) nx_read((int)(uintptr_t)f, b, s*n)
#define fseek(f, o, w) nx_lseek((int)(uintptr_t)f, o, w)
#define ftell(f) nx_lseek((int)(uintptr_t)f, 0, 1)
#else
#include <stdio.h>
#include <stdlib.h>
#endif

/* Magic byte signatures */
static const uint8_t PNG_SIG[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
static const uint8_t AVIF_SIG[8] = {'f', 't', 'y', 'p', 'a', 'v', 'i', 'f'}; /* at offset 4 */

/* Detect image type from magic bytes */
nximg_type_t nxi_detect_type(const uint8_t *data, size_t len) {
    if (len < 8) return NXIMG_TYPE_UNKNOWN;
    
    /* PNG: 89 50 4E 47 0D 0A 1A 0A */
    if (memcmp(data, PNG_SIG, 8) == 0) {
        return NXIMG_TYPE_PNG;
    }
    
    /* NXI: # NeolyxOS NXI */
    if (len >= 14 && data[0] == '#' && data[1] == ' ') {
        if (memcmp(data + 2, "NeolyxOS NXI", 12) == 0) {
            return NXIMG_TYPE_NXI;
        }
    }
    
    /* NXI alternate: [nxi] section header */
    if (len >= 5 && memcmp(data, "[nxi]", 5) == 0) {
        return NXIMG_TYPE_NXI;
    }
    
    /* SVG: starts with <svg or <?xml */
    if (len >= 5) {
        const char *s = (const char *)data;
        /* Skip whitespace */
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
        if (memcmp(s, "<svg", 4) == 0 || memcmp(s, "<?xml", 5) == 0) {
            return NXIMG_TYPE_SVG;
        }
    }
    
    /* AVIF: ftyp at offset 4 */
    if (len >= 12 && memcmp(data + 4, "ftyp", 4) == 0) {
        if (memcmp(data + 8, "avif", 4) == 0 || 
            memcmp(data + 8, "avis", 4) == 0 ||
            memcmp(data + 8, "mif1", 4) == 0) {
            return NXIMG_TYPE_AVIF;
        }
    }
    
    /* JPEG: FF D8 FF */
    if (len >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        return NXIMG_TYPE_JPEG;
    }
    
    return NXIMG_TYPE_UNKNOWN;
}

/* Load image from memory buffer */
int nxi_load_mem(const uint8_t *data, size_t len, nximage_t *img) {
    if (!data || len == 0 || !img) return NXI_ERR_FORMAT;
    
    memset(img, 0, sizeof(*img));
    
    nximg_type_t type = nxi_detect_type(data, len);
    
    switch (type) {
        case NXIMG_TYPE_PNG:
            return nxpng_load(data, len, img);
            
        case NXIMG_TYPE_NXI:
            return nxnxi_load(data, len, img);
            
        case NXIMG_TYPE_SVG:
            /* SVG requires render context - not supported in simple loader */
            return NXI_ERR_UNSUPPORTED;
            
        case NXIMG_TYPE_AVIF:
            return NXI_ERR_UNSUPPORTED;  /* AVIF stub */
            
        case NXIMG_TYPE_JPEG:
            return NXI_ERR_UNSUPPORTED;  /* JPEG not implemented yet */
            
        default:
            return NXI_ERR_FORMAT;
    }
}

/* Load image from file path */
int nxi_load(const char *path, nximage_t *img) {
    if (!path || !img) return NXI_ERR_FILE;
    
    FILE *f = fopen(path, "rb");
    if (!f) return NXI_ERR_FILE;
    
    /* Get file size */
    fseek(f, 0, 2);  /* SEEK_END */
    long size = ftell(f);
    fseek(f, 0, 0);  /* SEEK_SET */
    
    if (size <= 0 || size > 64 * 1024 * 1024) {  /* Max 64MB */
        fclose(f);
        return NXI_ERR_FILE;
    }
    
    /* Read file */
    uint8_t *data = (uint8_t *)malloc(size);
    if (!data) {
        fclose(f);
        return NXI_ERR_MEMORY;
    }
    
    size_t read = fread(data, 1, size, f);
    fclose(f);
    
    if ((long)read != size) {
        free(data);
        return NXI_ERR_FILE;
    }
    
    /* Decode */
    int err = nxi_load_mem(data, size, img);
    free(data);
    return err;
}

/* Get image info without loading pixels */
int nxi_info(const char *path, uint32_t *width, uint32_t *height, nximg_format_t *format) {
    if (!path) return NXI_ERR_FILE;
    
    FILE *f = fopen(path, "rb");
    if (!f) return NXI_ERR_FILE;
    
    /* Read header bytes */
    uint8_t header[256];
    size_t read = fread(header, 1, sizeof(header), f);
    fclose(f);
    
    if (read < 8) return NXI_ERR_FORMAT;
    
    nximg_type_t type = nxi_detect_type(header, read);
    
    switch (type) {
        case NXIMG_TYPE_PNG:
            if (format) *format = NXIMG_RGBA32;
            return nxpng_info(header, read, width, height);
            
        case NXIMG_TYPE_NXI:
            if (format) *format = NXIMG_RGBA32;
            return nxnxi_info(header, read, width, height);
            
        default:
            return NXI_ERR_FORMAT;
    }
}

/* Free image resources */
void nxi_free(nximage_t *img) {
    if (img && img->pixels) {
        free(img->pixels);
        img->pixels = NULL;
        img->pixel_size = 0;
    }
}

/* Error string */
const char *nxi_error_string(int error) {
    switch (error) {
        case NXI_OK: return "Success";
        case NXI_ERR_FILE: return "File error";
        case NXI_ERR_FORMAT: return "Invalid format";
        case NXI_ERR_MEMORY: return "Out of memory";
        case NXI_ERR_DECODE: return "Decode error";
        case NXI_ERR_UNSUPPORTED: return "Unsupported format";
        default: return "Unknown error";
    }
}

/* External malloc/free */
#ifndef malloc
extern void *nx_malloc(size_t size);
extern void nx_free(void *ptr);
#define malloc nx_malloc
#define free nx_free
#endif
