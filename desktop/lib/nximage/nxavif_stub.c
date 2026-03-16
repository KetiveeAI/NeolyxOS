/*
 * NeolyxOS AVIF Stub (nxavif_stub.c)
 * 
 * Placeholder for AVIF decoder. Returns NXI_ERR_UNSUPPORTED.
 * AVIF requires AV1 decoder which is complex (~10k lines).
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "nximage.h"

/*
 * AVIF Format Notes:
 * - Container: HEIF (ISO Base Media File Format)
 * - Codec: AV1 (AOMedia Video 1)
 * - Advantages: 50% smaller than JPEG at same quality
 * - Complexity: Requires full AV1 intra-frame decoder
 * 
 * Future implementation options:
 * 1. Port dav1d (BSD license, ~15k lines)
 * 2. Port libgav1 (Apache, Google's decoder)
 * 3. Custom minimal decoder (4-6 weeks work)
 */

int nxavif_load(const uint8_t *data, size_t len, nximage_t *img) {
    (void)data;
    (void)len;
    (void)img;
    return NXI_ERR_UNSUPPORTED;
}

int nxavif_info(const uint8_t *data, size_t len, uint32_t *w, uint32_t *h) {
    (void)data;
    (void)len;
    (void)w;
    (void)h;
    return NXI_ERR_UNSUPPORTED;
}
