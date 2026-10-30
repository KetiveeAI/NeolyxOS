/*
 * NeolyxOS - NXCanvas host test harness
 *
 * Built with the host compiler (hosted libc allowed here only) so engine
 * modules can be exercised under AddressSanitizer/UBSan and their output
 * inspected as PNG files before they run inside NeolyxOS.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXC_TEST_H
#define NXC_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../include/nxcanvas.h"

/* Initialize the engine with a host allocator (max surface w x h). */
void nxc_test_init(int32_t max_w, int32_t max_h);
/* Write a surface as an 8-bit RGBA PNG (un-premultiplied). Returns 0 on success. */
int nxc_test_write_png(const nxc_surface_t *s, const char *path);
/* Directory for test output images (NXC_TEST_OUT or ./out). */
const char *nxc_test_out_dir(void);
/* Build "<out_dir>/<name>" into a static buffer. */
const char *nxc_test_path(const char *name);

/* Read pixel (x, y) as straight-alpha 0xAARRGGBB. */
uint32_t nxc_test_pixel(const nxc_surface_t *s, int32_t x, int32_t y);

extern int nxc_test_failures;

#define NXC_CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        nxc_test_failures++; \
    } \
} while (0)

#define NXC_CHECK_OK(expr) do { \
    nxc_status_t _st = (expr); \
    if (_st != NXC_OK) { \
        fprintf(stderr, "FAIL %s:%d: %s -> %s\n", __FILE__, __LINE__, #expr, nxc_status_str(_st)); \
        nxc_test_failures++; \
    } \
} while (0)

/* |a - b| <= tol per channel */
int nxc_test_color_near(uint32_t a, uint32_t b, int tol);

/* Print summary and return process exit code. */
int nxc_test_finish(const char *name);

#endif /* NXC_TEST_H */
