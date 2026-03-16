/*
 * NeolyxOS Image Library (libnximage)
 * 
 * Userspace image loading library supporting PNG, NXI, SVG, AVIF.
 * 
 * Copyright (c) 2025 KetiveeAI
 * Author: KetiveeAI
 * This is proprietary software. All rights reserved. To use this library, you must agree to the terms of the
 * license.
 */

#ifndef NXIMAGE_H
#define NXIMAGE_H

#include <stdint.h>
#include <stddef.h>

/* Image pixel formats */
typedef enum {
    NXIMG_RGBA32 = 0,   /* 32-bit RGBA (8 bits per channel) */
    NXIMG_RGB24  = 1,   /* 24-bit RGB (8 bits per channel) */
    NXIMG_GRAY8  = 2,   /* 8-bit grayscale */
    NXIMG_GRAYA16 = 3,  /* 16-bit grayscale + alpha */
} nximg_format_t;

/* Image type detection */
typedef enum {
    NXIMG_TYPE_UNKNOWN = 0,
    NXIMG_TYPE_PNG     = 1,
    NXIMG_TYPE_NXI     = 2,  /* Native NeolyxOS icon */
    NXIMG_TYPE_SVG     = 3,
    NXIMG_TYPE_AVIF    = 4,
    NXIMG_TYPE_JPEG    = 5,
} nximg_type_t;

/* Error codes */
typedef enum {
    NXI_OK             = 0,
    NXI_ERR_FILE       = -1,  /* File not found or unreadable */
    NXI_ERR_FORMAT     = -2,  /* Invalid or unsupported format */
    NXI_ERR_MEMORY     = -3,  /* Memory allocation failed */
    NXI_ERR_DECODE     = -4,  /* Decoding error */
    NXI_ERR_UNSUPPORTED = -5, /* Feature not supported */
} nxi_error_t;

/* Image structure */
typedef struct {
    uint32_t width;         /* Image width in pixels */
    uint32_t height;        /* Image height in pixels */
    nximg_format_t format;  /* Pixel format */
    uint8_t bpp;            /* Bytes per pixel */
    uint8_t *pixels;        /* Raw pixel data (heap allocated) */
    size_t pixel_size;      /* Total size of pixel buffer */
    nximg_type_t type;      /* Original image type */
} nximage_t;

/* ============ Public API ============ */

/*
 * nxi_load - Load image from file path
 * 
 * Automatically detects format from magic bytes.
 * Allocates pixel buffer internally (caller must free with nxi_free).
 * 
 * Parameters:
 *   path - Path to image file
 *   img  - Output image structure
 * 
 * Returns: NXI_OK on success, error code otherwise
 */
int nxi_load(const char *path, nximage_t *img);

/*
 * nxi_load_mem - Load image from memory buffer
 * 
 * Parameters:
 *   data - Pointer to image data
 *   len  - Length of data in bytes
 *   img  - Output image structure
 * 
 * Returns: NXI_OK on success, error code otherwise
 */
int nxi_load_mem(const uint8_t *data, size_t len, nximage_t *img);

/*
 * nxi_free - Free image resources
 * 
 * Frees the pixel buffer. Does not free the nximage_t struct itself.
 */
void nxi_free(nximage_t *img);

/*
 * nxi_info - Get image info without loading pixels
 * 
 * Fast header-only read to get dimensions and format.
 * 
 * Parameters:
 *   path   - Path to image file
 *   width  - Output width (can be NULL)
 *   height - Output height (can be NULL)
 *   format - Output format (can be NULL)
 * 
 * Returns: NXI_OK on success, error code otherwise
 */
int nxi_info(const char *path, uint32_t *width, uint32_t *height, nximg_format_t *format);

/*
 * nxi_detect_type - Detect image type from magic bytes
 * 
 * Parameters:
 *   data - First 8+ bytes of file
 *   len  - Length of data
 * 
 * Returns: Detected image type
 */
nximg_type_t nxi_detect_type(const uint8_t *data, size_t len);

/*
 * nxi_error_string - Get human-readable error message
 */
const char *nxi_error_string(int error);

/* ============ Format-Specific (Internal) ============ */

/* PNG decoder */
int nxpng_load(const uint8_t *data, size_t len, nximage_t *img);
int nxpng_info(const uint8_t *data, size_t len, uint32_t *w, uint32_t *h);

/* NXI (native icon) decoder */
int nxnxi_load(const uint8_t *data, size_t len, nximage_t *img);
int nxnxi_info(const uint8_t *data, size_t len, uint32_t *w, uint32_t *h);

#endif /* NXIMAGE_H */
